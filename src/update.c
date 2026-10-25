#define _DEFAULT_SOURCE
#include "update.h"
#include "compat.h"
#include "util.h"
#include "version.h"

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#elif defined(__ANDROID__)
#include <jni.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define API_URL   "https://api.github.com/repos/FLEXIY0/BT-J2ME/releases/latest"
#define DL_PREFIX "https://github.com/FLEXIY0/BT-J2ME/releases/download/"
#define CORE_PREFIX "https://buildbot.libretro.com/nightly/"   /* ядра приставок (update_fetch) */
#define ART_PREFIX  "https://thumbnails.libretro.com/"          /* обложки игр (boxart_net.c) */
#define META_PREFIX "https://raw.githubusercontent.com/libretro/libretro-database/"   /* сведения об играх */

/* Где есть готовый файл в выпуске: Windows x64, Android, Linux x86_64. */
#if defined(_WIN64) || defined(__ANDROID__) || (defined(__linux__) && defined(__x86_64__))
#define CAN_UPDATE 1
#else
#define CAN_UPDATE 0
#endif

static SDL_atomic_t st, prog;
static int  started, restart;
static char ver[48], url[600], asset[64];
static long size;
static char path_new[1100];     /* куда качаем (UTF-8) */
#if !defined(_WIN32) && !defined(__ANDROID__)
static char exe[1024];
#endif

UpdState update_state(void) { return (UpdState)SDL_AtomicGet(&st); }
int update_need_restart(void) { return restart; }
const char *update_version(void) { return ver; }

int update_progress(void)
{
#ifdef __ANDROID__
    struct stat sb;         /* качает Java — смотрим, сколько уже легло в файл */
    if (SDL_AtomicGet(&st) == UPD_DOWNLOADING && size > 0 && stat(path_new, &sb) == 0)
        return (int)(sb.st_size * 100 / size);
#endif
    return SDL_AtomicGet(&prog);
}

/* «0.1.12» новее «0.1.9»: сравниваем числа по точкам. */
static int newer(const char *a, const char *b)
{
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y)
            return x > y;
        if (*a == '.')
            a++;
        if (*b == '.')
            b++;
        if ((*a && !isdigit((unsigned char)*a)) || (*b && !isdigit((unsigned char)*b)))
            break;
    }
    return 0;
}

/* Значение "ключ": "строка" или число после from; NULL — не нашли. */
static const char *json_value(const char *from, const char *key, char *out, size_t cap)
{
    char k[48];
    const char *p;
    size_t n = 0;

    snprintf(k, sizeof(k), "\"%s\"", key);
    if (!(p = strstr(from, k)))
        return NULL;
    p += strlen(k);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p++ != ':')
        return NULL;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p == '"') {
        for (p++; *p && *p != '"' && *p != '\\' && n + 1 < cap; p++)
            out[n++] = *p;
        if (*p != '"')
            return NULL;        /* экранированные строки нам не нужны */
        p++;
    } else {
        for (; (isdigit((unsigned char)*p) || *p == '-') && n + 1 < cap; p++)
            out[n++] = *p;
    }
    out[n] = '\0';
    return p;
}

/* Ссылка только на выпуск нашего репозитория и только из «безопасных» символов
 * (она уходит в командную строку curl на Linux). */
static int url_ok(const char *u)
{
    const char *p;

    if (strncmp(u, DL_PREFIX, strlen(DL_PREFIX)) && strncmp(u, CORE_PREFIX, strlen(CORE_PREFIX)) &&
        strncmp(u, ART_PREFIX, strlen(ART_PREFIX)) && strncmp(u, META_PREFIX, strlen(META_PREFIX)))
        return 0;
    for (p = u; *p; p++)
        if (!isalnum((unsigned char)*p) && !strchr(":/._-~%+", *p))
            return 0;
    return 1;
}

/* ---------- HTTP: в память (mem) или в файл (f) ---------- */

typedef struct {
    FILE  *f;
    char  *mem;
    size_t len;
    long   expect;
    volatile int *kbytes;   /* update_fetch: сколько КБ уже скачано */
} Sink;

static int sink_put(Sink *s, const void *data, size_t n)
{
    if (s->f) {
        if (fwrite(data, 1, n, s->f) != n)
            return 0;
    } else {
        char *m;
        if (s->len + n > (1u << 20) || !(m = realloc(s->mem, s->len + n + 1)))
            return 0;
        s->mem = m;
        memcpy(s->mem + s->len, data, n);
        s->mem[s->len + n] = '\0';
    }
    s->len += n;
    if (s->kbytes)
        *s->kbytes = (int)(s->len >> 10);
    else if (s->expect > 0)
        SDL_AtomicSet(&prog, (int)((long long)s->len * 100 / s->expect));
    return 1;
}

#if defined(_WIN32)
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 0x00000200
#endif

static int http_get(const char *u, Sink *s)
{
    wchar_t wu[700], host[256], path[700], extra[300];
    URL_COMPONENTS uc;
    HINTERNET ses = NULL, con = NULL, req = NULL;
    DWORD code = 0, sz = sizeof(code), got, proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1;
    char buf[16384];
    int ok = 0;

    if (!MultiByteToWideChar(CP_UTF8, 0, u, -1, wu, 700))
        return 0;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 400;
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 300;
    if (!WinHttpCrackUrl(wu, 0, 0, &uc))
        return 0;
    wcscat(path, extra);
    if (!(ses = WinHttpOpen(L"BT-J2ME", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0)))
        return 0;
    WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof(proto));   /* Windows 7: TLS 1.2 */
    if ((con = WinHttpConnect(ses, host, uc.nPort, 0)) &&
        (req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)) &&
        WinHttpSendRequest(req, L"Accept: application/vnd.github+json, */*", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA,
                           0, 0, 0) &&
        WinHttpReceiveResponse(req, NULL) &&
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &code, &sz, WINHTTP_NO_HEADER_INDEX) &&
        code == 200) {
        ok = 1;
        while (ok && WinHttpReadData(req, buf, sizeof(buf), &got) && got > 0)
            ok = sink_put(s, buf, got);
    }
    if (req)
        WinHttpCloseHandle(req);
    if (con)
        WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return ok;
}
#elif defined(__ANDROID__)
/* Сеть на Android — через Java (MainActivity.httpGet / download / installApk). */
static jobject act_call(const char *name, const char *sig, const char *a, const char *b, int *ok)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity(), res = NULL;
    jclass cls;
    jmethodID m;
    jstring ja = NULL, jb = NULL;

    *ok = 0;
    if (!env || !act)
        return NULL;
    cls = (*env)->GetObjectClass(env, act);
    m = (*env)->GetMethodID(env, cls, name, sig);
    ja = a ? (*env)->NewStringUTF(env, a) : NULL;
    jb = b ? (*env)->NewStringUTF(env, b) : NULL;
    if (m) {
        if (sig[strlen(sig) - 1] == 'Z')
            *ok = (*env)->CallBooleanMethod(env, act, m, ja, jb);
        else if (sig[strlen(sig) - 1] == 'V') {
            (*env)->CallVoidMethod(env, act, m, ja);
            *ok = 1;
        } else {
            res = (*env)->CallObjectMethod(env, act, m, ja);
            *ok = res != NULL;
        }
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        *ok = 0;
        res = NULL;
    }
    if (ja)
        (*env)->DeleteLocalRef(env, ja);
    if (jb)
        (*env)->DeleteLocalRef(env, jb);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
    return res;
}

static int http_get(const char *u, Sink *s)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject arr;
    int ok;
    jsize n;
    jbyte *b;

    arr = act_call("httpGet", "(Ljava/lang/String;)[B", u, NULL, &ok);
    if (!ok || !env)
        return 0;
    n = (*env)->GetArrayLength(env, (jbyteArray)arr);
    b = (*env)->GetByteArrayElements(env, (jbyteArray)arr, NULL);
    ok = b && sink_put(s, b, (size_t)n);
    if (b)
        (*env)->ReleaseByteArrayElements(env, (jbyteArray)arr, b, JNI_ABORT);
    (*env)->DeleteLocalRef(env, arr);
    return ok;
}
#else
/* Linux: curl, а если его нет — wget. Ссылка проверена url_ok, кавычки её не ломают. */
static int http_get(const char *u, Sink *s)
{
    char cmd[1400], buf[16384];
    size_t n;
    FILE *p;
    int ok = 1;

    snprintf(cmd, sizeof(cmd),
             "(curl -fsSL --max-time 300 -A 'BT-J2ME/%s' -H 'Accept: application/vnd.github+json' '%s' || "
             "wget -q -T 60 -O - -U 'BT-J2ME/%s' '%s') 2>/dev/null",
             BT_VERSION, u, BT_VERSION, u);
    if (!(p = popen(cmd, "r")))
        return 0;
    while (ok && (n = fread(buf, 1, sizeof(buf), p)) > 0)
        ok = sink_put(s, buf, n);
    return pclose(p) == 0 && ok;
}
#endif

/* ---------- где лежит программа и как её подменить ---------- */

#ifdef _WIN32
static wchar_t wexe[MAX_PATH], wnew[MAX_PATH + 8], wold[MAX_PATH + 8];

static int locate(void)
{
    const wchar_t *base;
    char name[MAX_PATH];
    int i;

    if (!GetModuleFileNameW(NULL, wexe, MAX_PATH) || wcslen(wexe) + 5 >= MAX_PATH)
        return 0;
    wcscpy(wnew, wexe);
    wcscat(wnew, L".new");
    wcscpy(wold, wexe);
    wcscat(wold, L".old");
    base = wcsrchr(wexe, L'\\');
    WideCharToMultiByte(CP_UTF8, 0, base ? base + 1 : wexe, -1, name, sizeof(name), NULL, NULL);
    for (i = 0; name[i]; i++)
        name[i] = (char)tolower((unsigned char)name[i]);
    snprintf(asset, sizeof(asset), "%s", strstr(name, "console") ? "bt-j2me-console.exe" : "bt-j2me.exe");
    WideCharToMultiByte(CP_UTF8, 0, wnew, -1, path_new, sizeof(path_new), NULL, NULL);
    return 1;
}

static int apply(void)
{
    if (!MoveFileExW(wexe, wold, MOVEFILE_REPLACE_EXISTING))   /* запущенный exe переименовать можно */
        return 0;
    if (!MoveFileExW(wnew, wexe, MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(wold, wexe, MOVEFILE_REPLACE_EXISTING);
        return 0;
    }
    restart = 1;
    return 1;
}
#elif defined(__ANDROID__)
static int locate(void)
{
    const char *ext = SDL_AndroidGetExternalStoragePath();

    if (!ext)
        return 0;
    snprintf(asset, sizeof(asset), "bt-j2me.apk");
    snprintf(path_new, sizeof(path_new), "%s/update.apk", ext);
    remove(path_new);
    return 1;
}

static int apply(void)
{
    int ok;

    act_call("installApk", "(Ljava/lang/String;)V", path_new, NULL, &ok);
    return ok;
}
#else
static int locate(void)
{
    char dir[1024], *slash;
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);

    if (n <= 0)
        return 0;
    exe[n] = '\0';
    snprintf(dir, sizeof(dir), "%s", exe);
    if (!(slash = strrchr(dir, '/')))
        return 0;
    *slash = '\0';
    if (access(dir, W_OK) != 0 || access(exe, W_OK) != 0)
        return 0;               /* поставлено в системную папку — обновляет пакетный менеджер */
    snprintf(asset, sizeof(asset), "bt-j2me-linux-x86_64");
    snprintf(path_new, sizeof(path_new), "%s.new", exe);
    return 1;
}

static int apply(void)
{
    if (chmod(path_new, 0755) != 0 || rename(path_new, exe) != 0)
        return 0;
    restart = 1;
    return 1;
}
#endif

/* ---------- потоки ---------- */

static int SDLCALL check_thread(void *u)
{
    Sink s;
    const char *p;
    char tag[40], name[80], num[32];

    (void)u;
    memset(&s, 0, sizeof(s));
    if (!http_get(API_URL, &s) || !s.mem || !json_value(s.mem, "tag_name", tag, sizeof(tag))) {
        free(s.mem);
        SDL_AtomicSet(&st, UPD_IDLE);
        return 0;
    }
    snprintf(ver, sizeof(ver), "%s", tag[0] == 'v' ? tag + 1 : tag);
    url[0] = '\0';
    size = 0;
    /* ищем свой файл среди assets: "name", потом его "size" и "browser_download_url" */
    for (p = s.mem; (p = json_value(p, "name", name, sizeof(name))) != NULL;) {
        if (strcmp(name, asset))
            continue;
        if (json_value(p, "size", num, sizeof(num)))
            size = atol(num);
        json_value(p, "browser_download_url", url, sizeof(url));
        break;
    }
    free(s.mem);
    if (newer(ver, BT_VERSION) && url_ok(url) && size > 0) {
        BT_LOG("доступна новая версия %s (сейчас %s)\n", ver, BT_VERSION);
        SDL_AtomicSet(&st, UPD_AVAILABLE);
    } else {
        SDL_AtomicSet(&st, UPD_IDLE);
    }
    return 0;
}

static int SDLCALL install_thread(void *u)
{
    int ok = 0;

    (void)u;
#ifdef __ANDROID__
    act_call("download", "(Ljava/lang/String;Ljava/lang/String;)Z", url, path_new, &ok);
#else
    {
        Sink s;
#ifdef _WIN32
        FILE *f = _wfopen(wnew, L"wb");
#else
        FILE *f = fopen(path_new, "wb");
#endif
        memset(&s, 0, sizeof(s));
        s.f = f;
        s.expect = size;
        ok = f && http_get(url, &s);
        if (f && fclose(f) != 0)
            ok = 0;
    }
#endif
    if (ok) {
        FILE *f = bt_fopen(path_new, "rb");
        long got = -1;
        if (f) {
            if (fseek(f, 0, SEEK_END) == 0)
                got = ftell(f);
            fclose(f);
        }
        ok = got == size;       /* оборванная загрузка не встанет на место программы */
    }
    if (ok && apply()) {
        BT_LOG("обновление до %s установлено\n", ver);
        SDL_AtomicSet(&st, UPD_DONE);
    } else {
        BT_LOG("не удалось обновиться до %s\n", ver);
        bt_remove(path_new);
        SDL_AtomicSet(&st, UPD_FAILED);
    }
    return 0;
}

/* ---------- хвосты прошлого обновления ---------- */

/* Windows: старый exe переименован в .old, но удалить его можно, лишь когда прошлый процесс
 * закроется, — а новая версия стартует раньше. Поэтому пробуем в фоне, пока не выйдет
 * (до ~30 с). Android: скачанный update.apk после установки не нужен. */
#if defined(_WIN32) || defined(__ANDROID__)
static int SDLCALL cleanup_thread(void *u)
{
#ifdef _WIN32
    wchar_t old[MAX_PATH + 8];
    int i;

    (void)u;
    if (!GetModuleFileNameW(NULL, old, MAX_PATH) || wcslen(old) + 5 >= MAX_PATH)
        return 0;
    wcscat(old, L".old");
    for (i = 0; i < 150; i++) {
        if (DeleteFileW(old) || GetLastError() == ERROR_FILE_NOT_FOUND)
            return 0;
        SDL_Delay(200);
    }
    BT_LOG("не удалось удалить старую версию программы\n");
#elif defined(__ANDROID__)
    const char *ext = SDL_AndroidGetExternalStoragePath();
    char apk[1100];

    (void)u;
    if (ext) {
        snprintf(apk, sizeof(apk), "%s/update.apk", ext);
        remove(apk);
    }
#endif
    return 0;
}
#endif

int update_fetch(const char *url, const char *path, volatile int *kbytes)
{
    int ok = 0;

    if (!url_ok(url))
        return 0;
#ifdef __ANDROID__
    (void)kbytes;
    act_call("download", "(Ljava/lang/String;Ljava/lang/String;)Z", url, path, &ok);
#else
    {
        Sink s;
        FILE *f = bt_fopen(path, "wb");
        memset(&s, 0, sizeof(s));
        s.f = f;
        s.kbytes = kbytes;
        ok = f && http_get(url, &s);
        if (f && fclose(f) != 0)
            ok = 0;
        if (!ok)
            bt_remove(path);
    }
#endif
    return ok;
}

void update_cleanup(void)
{
#if defined(_WIN32) || defined(__ANDROID__)
    SDL_Thread *t = SDL_CreateThread(cleanup_thread, "update-cleanup", NULL);

    if (t)
        SDL_DetachThread(t);
#endif
}

void update_check(void)
{
    SDL_Thread *t;
    const char *off = getenv("BT_NO_UPDATE");

    if (started || !CAN_UPDATE || strstr(BT_VERSION, "dev") || (off && *off))
        return;
    started = 1;
    if (!locate())
        return;
    SDL_AtomicSet(&st, UPD_CHECKING);
    if ((t = SDL_CreateThread(check_thread, "update", NULL)) != NULL)
        SDL_DetachThread(t);
    else
        SDL_AtomicSet(&st, UPD_IDLE);
}

void update_install(void)
{
    SDL_Thread *t;

    if (SDL_AtomicGet(&st) != UPD_AVAILABLE)
        return;
    SDL_AtomicSet(&prog, 0);
    SDL_AtomicSet(&st, UPD_DOWNLOADING);
    if ((t = SDL_CreateThread(install_thread, "update-dl", NULL)) != NULL)
        SDL_DetachThread(t);
    else
        SDL_AtomicSet(&st, UPD_FAILED);
}

void update_relaunch(int argc, char **argv)
{
    if (!restart)
        return;
#ifdef _WIN32
    {
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        wchar_t *cmd;
        (void)argv;
        if (argc > 1) {
            cmd = _wcsdup(GetCommandLineW());
        } else if ((cmd = malloc((wcslen(wexe) + 3) * sizeof(wchar_t))) != NULL) {
            wcscpy(cmd, L"\"");
            wcscat(cmd, wexe);
            wcscat(cmd, L"\"");
        }
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        if (cmd && CreateProcessW(wexe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        free(cmd);
    }
#elif !defined(__ANDROID__)
    {
        char **av = calloc((size_t)argc + 1, sizeof(char *));
        int i;
        if (!av)
            return;
        for (i = 0; i < argc; i++)
            av[i] = argv[i];
        execv(exe, av);         /* argc — сколько аргументов передать дальше */
        free(av);
    }
    BT_LOG("не удалось перезапустить %s\n", exe);
#else
    (void)argc;
    (void)argv;
#endif
}
