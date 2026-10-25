/* Загрузка обложек из libretro-thumbnails (thumbnails.libretro.com) в фоне. Список имён
 * приставки скачивается один раз (страница папки Named_Boxarts) и обновляется раз в месяц;
 * обложка — PNG, уменьшается до 400 точек по высоте и кладётся в <данные>/boxart/<приставка>.
 * Не нашли игру — рядом остаётся <имя>.none, чтобы не искать её снова до нового списка.
 * Сведения (год, издатель, жанр) — из metadat базы libretro-database, где они есть (NES),
 * в <имя>.meta вместе с найденным названием. */
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
#define META_URL    "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/"
#define INDEX_DAYS  30
#define MAX_H       400

typedef struct {
    int  kind, own;         /* own — своя обложка есть, нужны только сведения */
    char base[256], name[128];
} Job;

/* Сведения из одного файла metadat: «comment "Название"» → значение поля. */
typedef struct {
    char  *buf;
    char **name, **val;
    int    count;
} Meta;

typedef struct {
    char  *buf;
    char **names;
    int    count;
    long   stamp;       /* когда скачан список: по нему сверяются метки «.none» */
    int    failed;      /* не скачался — сети нет, остальные игры этой приставки не ищем */
    Meta   meta[3];     /* releaseyear, publisher, genre */
    int    meta_loaded;
} Index;

static const char *const meta_field[3] = { "releaseyear", "publisher", "genre" };

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

/* Файл metadat в духе clrmamepro: game ( comment "…" поле "…" ). Скачиваем раз в месяц
 * вместе со списком обложек; нет такого файла у приставки (404) — просто пусто. */
static void load_meta(Index *ix, int kind, int field)
{
    char dir[900], path[1000], url[600], esc[256];
    Meta *m = &ix->meta[field];
    long len, now = (long)time(NULL), stamp = 0;
    char *p, *cur = NULL;
    int cap = 0;
    FILE *f;

    cache_dir(kind, dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/%s.dat", dir, meta_field[field]);
    if ((f = bt_fopen(path, "rb")) != NULL) {     /* первая строка — когда скачан */
        if (fscanf(f, "# %ld", &stamp) != 1)
            stamp = 0;
        fclose(f);
    }
    if (now - stamp > INDEX_DAYS * 86400L) {
        char tmp[1010];
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        url_escape(boxart_system(kind, NULL), esc, sizeof(esc));
        snprintf(url, sizeof(url), META_URL "%s/%s.dat", meta_field[field], esc);
        if (update_fetch(url, tmp, NULL) && (m->buf = read_all(tmp, &len)) != NULL &&
            (f = bt_fopen(path, "wb")) != NULL) {
            fprintf(f, "# %ld\n", now);
            fwrite(m->buf, 1, (size_t)len, f);
            fclose(f);
        } else if ((f = bt_fopen(path, "wb")) != NULL) {
            fprintf(f, "# %ld\n", now);  /* у приставки такого файла нет — не спрашиваем месяц */
            fclose(f);
        }
        bt_remove(tmp);
        free(m->buf);
        m->buf = NULL;
    }
    if (!(m->buf = read_all(path, &len)))
        return;
    for (p = m->buf; *p; p++) {
        char *q, *val;
        if (*p != '\n')
            continue;
        q = p + 1;
        while (*q == ' ' || *q == '\t')
            q++;
        if (strncmp(q, "comment \"", 9) == 0) {
            cur = q + 9;
            continue;
        }
        if (!cur || strncmp(q, meta_field[field], strlen(meta_field[field])) || q[strlen(meta_field[field])] != ' ')
            continue;
        val = strchr(q, '"');
        if (!val || !strchr(val + 1, '"') || !strchr(cur, '"'))
            continue;
        if (m->count == cap) {
            char **nn = realloc(m->name, sizeof(char *) * (size_t)(cap = cap ? cap * 2 : 1024));
            char **nv = nn ? realloc(m->val, sizeof(char *) * (size_t)cap) : NULL;
            if (!nn || !nv) {
                if (nn)
                    m->name = nn;
                break;
            }
            m->name = nn;
            m->val = nv;
        }
        m->name[m->count] = cur;
        m->val[m->count] = val + 1;
        m->count++;
        cur = NULL;
    }
    /* строки кончаются кавычкой: обрезаем после того, как всё нашли */
    for (len = 0; len < m->count; len++) {
        *strchr(m->name[len], '"') = '\0';
        *strchr(m->val[len], '"') = '\0';
    }
}

/* Жанры libretro — по-русски; неизвестные остаются как есть. */
static const char *genre_ru(const char *g)
{
    static const char *const map[][2] = {
        { "Action", "Экшен" }, { "Adventure", "Приключения" }, { "Platform", "Платформер" },
        { "Racing", "Гонки" }, { "Driving", "Гонки" }, { "Sports", "Спорт" }, { "Puzzle", "Головоломка" },
        { "Shooter", "Шутер" }, { "Shoot'em Up", "Скролл-шутер" }, { "Fighting", "Файтинг" },
        { "Beat'em Up", "Битэмап" }, { "Role-Playing (RPG)", "RPG" }, { "Role-Playing", "RPG" },
        { "Strategy", "Стратегия" }, { "Simulation", "Симулятор" }, { "Board Game", "Настольная" },
        { "Pinball", "Пинбол" }, { "Educational", "Обучающая" }, { "Music", "Музыкальная" },
        { "Quiz", "Викторина" }, { "Compilation", "Сборник" }, { "Casino", "Казино" },
    };
    size_t i;

    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (!strcmp(g, map[i][0]))
            return map[i][1];
    return g;
}

/* Точное название, а нет — то же название с другими метками: «Super Mario Bros. (USA)». */
static const char *meta_find(const Meta *m, const char *name)
{
    const char *br = strstr(name, " (");
    size_t n = br ? (size_t)(br - name) : strlen(name);
    int i;

    for (i = 0; i < m->count; i++)
        if (!strcmp(m->name[i], name))
            return m->val[i];
    for (i = 0; i < m->count; i++)
        if (!strncmp(m->name[i], name, n) && !strncmp(m->name[i] + n, " (", 2))
            return m->val[i];
    return NULL;
}

/* <имя>.meta: найденное название и сведения о нём. */
static void write_meta(Index *ix, const Job *j, const char *dir, const char *name)
{
    char path[1300];
    const char *v;
    FILE *f;
    int k;

    if (!ix->meta_loaded) {
        ix->meta_loaded = 1;
        for (k = 0; k < 3; k++)
            load_meta(ix, j->kind, k);
    }
    snprintf(path, sizeof(path), "%s/%s.meta", dir, j->base);
    if (!(f = bt_fopen(path, "wb")))
        return;
    fprintf(f, "name=%s\n", name);
    if ((v = meta_find(&ix->meta[0], name)) != NULL)
        fprintf(f, "year=%s\n", v);
    if ((v = meta_find(&ix->meta[1], name)) != NULL)
        fprintf(f, "publisher=%s\n", v);
    if ((v = meta_find(&ix->meta[2], name)) != NULL)
        fprintf(f, "genre=%s\n", genre_ru(v));
    fclose(f);
}

static void process(Index *ix, const Job *j)
{
    char dir[900], path[1300], mark[1300], tmp[1300], url[1200], esc[700], meta[1300];
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
    snprintf(meta, sizeof(meta), "%s/%s.meta", dir, j->base);
    if ((bt_file_size(path) > 0 || j->own) && bt_file_size(meta) > 0)
        return;
    if ((f = bt_fopen(mark, "rb")) != NULL) {
        if (fscanf(f, "%ld", &stamp) != 1)
            stamp = -1;
        fclose(f);
        if (stamp == ix->stamp)
            return;
    }
    i = boxart_match((const char *const *)ix->names, ix->count, j->base, j->name);
    if (i >= 0 && (bt_file_size(path) > 0 || j->own)) {   /* картинка уже есть — нужны сведения */
        write_meta(ix, j, dir, ix->names[i]);
        return;
    }
    if (i >= 0) {
        char e1[256];
        url_escape(sys, e1, sizeof(e1));
        url_escape(ix->names[i], esc, sizeof(esc));
        snprintf(url, sizeof(url), BASE_URL "%s/Named_Boxarts/%s.png", e1, esc);
        snprintf(tmp, sizeof(tmp), "%s/%s.tmp", dir, j->base);
        if (update_fetch(url, tmp, NULL) && store(tmp, path)) {
            bt_remove(mark);
            write_meta(ix, j, dir, ix->names[i]);
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
        int m;
        free(ix[k].names);
        free(ix[k].buf);
        for (m = 0; m < 3; m++) {
            free(ix[k].meta[m].buf);
            free(ix[k].meta[m].name);
            free(ix[k].meta[m].val);
        }
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
        int have;
        if (!boxart_system(g->kind, NULL))
            continue;
        have = boxart_cached(g->kind, g->base, path, sizeof(path)) || g->cover_own;
        snprintf(path + strlen(path) - 4, 6, ".meta");
        if (have && bt_file_size(path) > 0)
            continue;               /* и обложка, и сведения уже есть */
        nj[n].kind = g->kind;
        nj[n].own = g->cover_own;
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
