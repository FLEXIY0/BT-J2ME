/* Загрузка обложек из libretro-thumbnails (thumbnails.libretro.com) в фоне. Список имён
 * приставки скачивается один раз (страница папки Named_Boxarts) и обновляется раз в месяц;
 * обложка — PNG, уменьшается до 400 точек по высоте и кладётся в <данные>/boxart/<приставка>.
 * Не нашли игру — рядом остаётся <имя>.none, чтобы не искать её снова до нового списка. */
#include "boxart.h"
#include "compat.h"
#include "png.h"
#include "update.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BASE_URL    "https://thumbnails.libretro.com/"
#define INDEX_DAYS  30
#define MAX_H       400

typedef struct {
    int  kind;
    char base[256], name[128];
} Job;

typedef struct {
    char  *buf;
    char **names;
    int    count;
    long   stamp;       /* когда скачан список: по нему сверяются метки «.none» */
    int    failed;      /* не скачался — сети нет, остальные игры этой приставки не ищем */
} Index;

static SDL_mutex   *mx;
static Job         *jobs;
static int          njobs, next_job, running;
static SDL_atomic_t gen;

unsigned boxart_generation(void)
{
    return (unsigned)SDL_AtomicGet(&gen);
}

static void cache_dir(int kind, char *out, size_t cap)
{
    const char *dir;
    char data[800];

    boxart_system(kind, &dir);
    bt_data_dir(data, sizeof(data));
    snprintf(out, cap, "%s/boxart/%s", data, dir);
}

/* Каждый байт вне [A-Za-z0-9._~-] — %XX: пробелы, скобки, запятые, японские буквы. */
static void url_escape(const char *s, char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;

    for (; *s && n + 4 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("._~-", c)) {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    out[n] = '\0';
}

static char *read_all(const char *path, long *len)
{
    FILE *f = bt_fopen(path, "rb");
    char *buf = NULL;

    *len = 0;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (*len = ftell(f)) > 0 && *len < (32L << 20) && fseek(f, 0, SEEK_SET) == 0 &&
        (buf = malloc((size_t)*len + 1)) != NULL) {
        if (fread(buf, 1, (size_t)*len, f) == (size_t)*len)
            buf[*len] = '\0';
        else {
            free(buf);
            buf = NULL;
        }
    }
    fclose(f);
    return buf;
}

/* Имена картинок со страницы папки: текст ссылок «…png</a>», &amp; и прочее — обратно в знаки. */
static int parse_listing(const char *html, FILE *out)
{
    static const char *const ent[][2] = { { "&amp;", "&" }, { "&#39;", "'" }, { "&quot;", "\"" },
                                          { "&lt;", "<" },  { "&gt;", ">" } };
    const char *p = html;
    int count = 0;

    while ((p = strstr(p, "<a href=\"")) != NULL) {
        const char *t = strstr(p, "\">"), *e;
        char name[512];
        size_t n = 0, i;

        p += 9;
        if (!t || !(e = strstr(t, "</a>")))
            break;
        t += 2;
        if (e - t < 5 || strncmp(e - 4, ".png", 4))
            continue;
        while (t < e - 4 && n + 1 < sizeof(name)) {
            for (i = 0; i < 5; i++)
                if (!strncmp(t, ent[i][0], strlen(ent[i][0])))
                    break;
            if (i < 5) {
                name[n++] = ent[i][1][0];
                t += strlen(ent[i][0]);
            } else
                name[n++] = *t++;
        }
        name[n] = '\0';
        fprintf(out, "%s\n", name);
        count++;
    }
    return count;
}

/* Список обложек приставки: из кэша, а если его нет или он старше месяца — с сервера. */
static void load_index(Index *ix, int kind)
{
    char dir[900], path[1000], tmp[1000], ntmp[1000], url[512], esc[256];
    long len, stamp = 0, now = (long)time(NULL);
    char *buf, *p;
    int cap = 0;

    cache_dir(kind, dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/names.txt", dir);
    buf = read_all(path, &len);
    if (buf && sscanf(buf, "# %ld", &stamp) != 1)
        stamp = 0;
    if (!buf || now - stamp > INDEX_DAYS * 86400L) {
        FILE *f;
        char *html;
        int ok = 0;

        bt_mkdirs(dir);
        snprintf(tmp, sizeof(tmp), "%s/listing.tmp", dir);
        url_escape(boxart_system(kind, NULL), esc, sizeof(esc));
        snprintf(url, sizeof(url), BASE_URL "%s/Named_Boxarts/", esc);
        if (update_fetch(url, tmp, NULL) && (html = read_all(tmp, &len)) != NULL) {
            snprintf(ntmp, sizeof(ntmp), "%s/names.tmp", dir);
            if ((f = bt_fopen(ntmp, "wb")) != NULL) {
                fprintf(f, "# %ld\n", now);
                ok = parse_listing(html, f) > 100;
                ok = fclose(f) == 0 && ok;
                ok = ok && bt_replace(ntmp, path);
                if (!ok)
                    bt_remove(ntmp);
            }
            free(html);
        }
        bt_remove(tmp);
        if (ok) {
            free(buf);
            buf = read_all(path, &len);
            stamp = now;
        } else if (!buf) {
            ix->failed = 1;     /* старый список, если есть, ещё сгодится */
            return;
        }
    }
    ix->buf = buf;
    ix->stamp = stamp;
    for (p = strchr(buf, '\n'); p && p[1]; p = strchr(p + 1, '\n')) {
        if (ix->count == cap) {
            char **nn = realloc(ix->names, sizeof(char *) * (size_t)(cap = cap ? cap * 2 : 4096));
            if (!nn)
                break;
            ix->names = nn;
        }
        ix->names[ix->count++] = p + 1;
        *p = '\0';
    }
    if (p)
        *p = '\0';
    if (ix->count && (p = strchr(ix->names[ix->count - 1], '\n')) != NULL)
        *p = '\0';
}

/* Скачанная картинка: уменьшить и сохранить как обложку игры. */
static int store(const char *tmp, const char *path)
{
    long len;
    char *buf = read_all(tmp, &len);
    uint32_t *px;
    uint8_t *png = NULL;
    size_t plen = 0;
    int w, h, ok = 0;
    FILE *f;

    if (!buf)
        return 0;
    px = png_decode((const uint8_t *)buf, (size_t)len, &w, &h);
    free(buf);
    if (!px)
        return 0;
    if (h > MAX_H) {
        Image src = { px, w, h }, dst = image_resize(&src, w * MAX_H / h, MAX_H);
        free(px);
        px = dst.px;
        w = dst.w;
        h = dst.h;
    }
    if (px)
        png = png_encode(px, w, h, &plen);
    free(px);
    if (png && (f = bt_fopen(tmp, "wb")) != NULL) {
        ok = fwrite(png, 1, plen, f) == plen;
        ok = fclose(f) == 0 && ok;
        ok = ok && bt_replace(tmp, path);
    }
    free(png);
    return ok;
}

static void process(Index *ix, const Job *j)
{
    char dir[900], path[1300], mark[1300], tmp[1300], url[1200], esc[700];
    const char *sys = boxart_system(j->kind, NULL);
    long stamp = -1;
    FILE *f;
    int i;

    if (!ix->buf && !ix->failed)
        load_index(ix, j->kind);
    if (ix->failed)
        return;
    cache_dir(j->kind, dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/%s.png", dir, j->base);
    snprintf(mark, sizeof(mark), "%s/%s.none", dir, j->base);
    if (bt_file_size(path) > 0)
        return;
    if ((f = bt_fopen(mark, "rb")) != NULL) {
        if (fscanf(f, "%ld", &stamp) != 1)
            stamp = -1;
        fclose(f);
        if (stamp == ix->stamp)
            return;
    }
    i = boxart_match((const char *const *)ix->names, ix->count, j->base, j->name);
    if (i >= 0) {
        char e1[256];
        url_escape(sys, e1, sizeof(e1));
        url_escape(ix->names[i], esc, sizeof(esc));
        snprintf(url, sizeof(url), BASE_URL "%s/Named_Boxarts/%s.png", e1, esc);
        snprintf(tmp, sizeof(tmp), "%s/%s.tmp", dir, j->base);
        if (update_fetch(url, tmp, NULL) && store(tmp, path)) {
            bt_remove(mark);
            SDL_AtomicIncRef(&gen);
            return;
        }
        bt_remove(tmp);
        return;                 /* сбой сети — попробуем в следующий раз */
    }
    if ((f = bt_fopen(mark, "wb")) != NULL) {
        fprintf(f, "%ld\n", ix->stamp);
        fclose(f);
    }
}

static int worker(void *arg)
{
    Index ix[GAME_PS2 + 1];
    int k;

    (void)arg;
    memset(ix, 0, sizeof(ix));
    for (;;) {
        Job j;
        SDL_LockMutex(mx);
        if (next_job >= njobs) {
            running = 0;
            SDL_UnlockMutex(mx);
            break;
        }
        j = jobs[next_job++];
        SDL_UnlockMutex(mx);
        process(&ix[j.kind], &j);
    }
    for (k = 0; k <= GAME_PS2; k++) {
        free(ix[k].names);
        free(ix[k].buf);
    }
    return 0;
}

void boxart_fetch(const Library *lib)
{
    Job *nj;
    int i, n = 0;
    char path[1300];
    SDL_Thread *t;

    if (getenv("BT_NO_BOXART") || !lib->count)
        return;
    if (!mx && !(mx = SDL_CreateMutex()))
        return;
    if (!(nj = malloc(sizeof(Job) * (size_t)lib->count)))
        return;
    for (i = 0; i < lib->count; i++) {
        const Game *g = &lib->games[i];
        if (!boxart_system(g->kind, NULL) || g->cover_own || boxart_cached(g->kind, g->base, path, sizeof(path)))
            continue;
        nj[n].kind = g->kind;
        snprintf(nj[n].base, sizeof(nj[n].base), "%s", g->base);
        snprintf(nj[n].name, sizeof(nj[n].name), "%s", g->name);
        n++;
    }
    SDL_LockMutex(mx);
    free(jobs);
    jobs = nj;
    njobs = n;
    next_job = 0;
    if (n && !running) {
        running = 1;
        if ((t = SDL_CreateThread(worker, "boxart", NULL)) != NULL)
            SDL_DetachThread(t);
        else
            running = 0;
    }
    SDL_UnlockMutex(mx);
}
