#define _DEFAULT_SOURCE
#include "compat.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>

/* UTF-8 → UTF-16 в выделенный буфер (освободить free). */
static wchar_t *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w;

    if (n <= 0 || !(w = malloc((size_t)n * sizeof(wchar_t))))
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static void narrow(const wchar_t *w, char *out, size_t cap)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL))
        out[0] = '\0';
}

FILE *bt_fopen(const char *path, const char *mode)
{
    wchar_t *wp = wide(path), *wm = wide(mode);
    FILE *f = wp && wm ? _wfopen(wp, wm) : NULL;

    free(wp);
    free(wm);
    return f;
}

int bt_mkdir(const char *path)
{
    wchar_t *w = wide(path);
    int r = w && CreateDirectoryW(w, NULL);

    free(w);
    return r;
}

int bt_replace(const char *from, const char *to)
{
    wchar_t *a = wide(from), *b = wide(to);
    int r = a && b && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);

    free(a);
    free(b);
    return r;
}

int bt_remove(const char *path)
{
    wchar_t *w = wide(path);
    int r = w && DeleteFileW(w);

    free(w);
    return r;
}

int bt_is_dir(const char *path)
{
    wchar_t *w = wide(path);
    DWORD a = w ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;

    free(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

struct BtDir {
    HANDLE           h;
    WIN32_FIND_DATAW fd;
    int              first;
    char             name[MAX_PATH * 3];
};

BtDir *bt_dir_open(const char *path)
{
    size_t n = strlen(path);
    char *pat = malloc(n + 3);
    wchar_t *w;
    BtDir *d = calloc(1, sizeof(BtDir));

    if (!pat || !d) {
        free(pat);
        free(d);
        return NULL;
    }
    memcpy(pat, path, n);
    strcpy(pat + n, "/*");
    w = wide(pat);
    free(pat);
    d->h = w ? FindFirstFileW(w, &d->fd) : INVALID_HANDLE_VALUE;
    free(w);
    if (d->h == INVALID_HANDLE_VALUE) {
        free(d);
        return NULL;
    }
    d->first = 1;
    return d;
}

const char *bt_dir_next(BtDir *d)
{
    if (!d->first && !FindNextFileW(d->h, &d->fd))
        return NULL;
    d->first = 0;
    narrow(d->fd.cFileName, d->name, sizeof(d->name));
    return d->name;
}

void bt_dir_close(BtDir *d)
{
    if (!d)
        return;
    FindClose(d->h);
    free(d);
}

static const char *env_utf8(const wchar_t *name, char *buf, size_t cap)
{
    const wchar_t *v = _wgetenv(name);

    if (!v || !*v)
        return NULL;
    narrow(v, buf, cap);
    return buf[0] ? buf : NULL;
}

const char *bt_home(void)
{
    static char buf[MAX_PATH * 3];
    const char *h = env_utf8(L"HOME", buf, sizeof(buf));

    if (!h)
        h = env_utf8(L"USERPROFILE", buf, sizeof(buf));
    return h ? h : ".";
}

const char *bt_appdata(void)
{
    static char buf[MAX_PATH * 3];

    return env_utf8(L"APPDATA", buf, sizeof(buf));
}

#else /* POSIX */

#include <dirent.h>
#include <sys/stat.h>

FILE *bt_fopen(const char *path, const char *mode) { return fopen(path, mode); }
int bt_mkdir(const char *path) { return mkdir(path, 0755) == 0; }
int bt_replace(const char *from, const char *to) { return rename(from, to) == 0; }
int bt_remove(const char *path) { return remove(path) == 0; }

int bt_is_dir(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

BtDir *bt_dir_open(const char *path) { return (BtDir *)opendir(path); }

const char *bt_dir_next(BtDir *d)
{
    struct dirent *de = readdir((DIR *)d);

    return de ? de->d_name : NULL;
}

void bt_dir_close(BtDir *d)
{
    if (d)
        closedir((DIR *)d);
}

#ifdef __ANDROID__
#include <SDL_system.h>

/* Android: настройки и сохранения — во внутренней памяти приложения (её не видно другим). */
const char *bt_home(void)
{
    const char *p = SDL_AndroidGetInternalStoragePath();

    return p ? p : ".";
}

const char *bt_appdata(void) { return SDL_AndroidGetInternalStoragePath(); }
#else
const char *bt_home(void)
{
    const char *h = getenv("HOME");

    return h && *h ? h : ".";
}

const char *bt_appdata(void) { return NULL; }
#endif

#endif
