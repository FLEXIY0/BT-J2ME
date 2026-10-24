#define _DEFAULT_SOURCE
#include "library.h"
#include "jar.h"
#include "manifest.h"
#include "png.h"
#include "psx.h"
#include "compat.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void copy_str(char *dst, size_t cap, const char *src)
{
    snprintf(dst, cap, "%s", src ? src : "");
}

static Image load_png_mem(const uint8_t *buf, size_t len)
{
    Image img = { NULL, 0, 0 };

    img.px = png_decode(buf, len, &img.w, &img.h);
    if (!img.px)
        img.w = img.h = 0;
    return img;
}

static Image load_png_file(const char *path)
{
    Image img = { NULL, 0, 0 };
    FILE *f = bt_fopen(path, "rb");
    uint8_t *buf;
    long len;

    if (!f)
        return img;
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && len < (16L << 20) && fseek(f, 0, SEEK_SET) == 0 &&
        (buf = malloc((size_t)len)) != NULL) {
        if (fread(buf, 1, (size_t)len, f) == (size_t)len)
            img = load_png_mem(buf, (size_t)len);
        free(buf);
    }
    fclose(f);
    return img;
}

static void read_game(Game *g)
{
    Jar jar;
    Manifest mf;
    MidletInfo mi;
    const JarEntry *e;
    const char *icon = NULL;
    char *text;
    size_t len;

    copy_str(g->name, sizeof(g->name), g->base);
    if (jar_open(&jar, g->path))
        return;
    g->size = (long)jar.size;
    e = jar_find(&jar, "META-INF/MANIFEST.MF");
    text = e ? (char *)jar_read(&jar, e, &len) : NULL;
    if (text && manifest_parse(&mf, text, len) == 0) {
        if (manifest_get(&mf, "MIDlet-Name"))
            copy_str(g->name, sizeof(g->name), manifest_get(&mf, "MIDlet-Name"));
        copy_str(g->vendor, sizeof(g->vendor), manifest_get(&mf, "MIDlet-Vendor"));
        copy_str(g->version, sizeof(g->version), manifest_get(&mf, "MIDlet-Version"));
        copy_str(g->profile, sizeof(g->profile), manifest_get(&mf, "MicroEdition-Profile"));
        icon = manifest_get(&mf, "MIDlet-Icon");
        if ((!icon || !*icon) && manifest_midlet(&mf, 1, &mi) == 0 && mi.icon[0])
            icon = mi.icon;
        if (icon && *icon) {
            const JarEntry *ie = jar_find(&jar, icon[0] == '/' ? icon + 1 : icon);
            uint8_t *png = ie ? jar_read(&jar, ie, &len) : NULL;
            if (png) {
                g->icon = load_png_mem(png, len);
                free(png);
            }
        }
        manifest_free(&mf);
    }
    free(text);
    jar_close(&jar);
}

/* Название из имени файла: «_» — пробелы, без пометок дампа «(U) [!]», «(USA) (Disc 1)». */
static void name_from_file(Game *g)
{
    char *p, *o;

    snprintf(g->name, sizeof(g->name), "%.127s", g->base);
    for (p = g->name; *p; p++)
        if (*p == '_')
            *p = ' ';
    if ((p = strpbrk(g->name, "([")) != NULL && p > g->name)
        *p = '\0';
    for (o = g->name + strlen(g->name); o > g->name && o[-1] == ' '; o--)
        o[-1] = '\0';
}

static long file_size(const char *path)
{
    FILE *f = bt_fopen(path, "rb");
    long n = 0;

    if (f) {
        if (fseek(f, 0, SEEK_END) == 0)
            n = ftell(f);
        fclose(f);
    }
    return n;
}

/* Картридж NES: подзаголовок — NES. */
static void read_nes(Game *g)
{
    name_from_file(g);
    copy_str(g->profile, sizeof(g->profile), "NES");
    g->size = file_size(g->path);
}

int library_cue_files(const char *cue_path, char (*out)[256], int max)
{
    FILE *f = bt_fopen(cue_path, "r");
    char line[600];
    int n = 0;

    if (!f)
        return 0;
    while (n < max && fgets(line, sizeof(line), f)) {
        char *p = line, *e;
        while (*p == ' ' || *p == '\t')
            p++;
        if (strncasecmp(p, "FILE", 4) != 0 || (p[4] != ' ' && p[4] != '\t'))
            continue;
        p += 5;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '"') {
            e = strchr(++p, '"');
        } else {
            e = p;
            while (*e && *e != ' ' && *e != '\t' && *e != '\r' && *e != '\n')
                e++;
        }
        if (!e || e == p)
            continue;
        *e = '\0';
        if (strrchr(p, '/'))
            p = strrchr(p, '/') + 1;
        if (strrchr(p, '\\'))
            p = strrchr(p, '\\') + 1;
        snprintf(out[n++], 256, "%s", p);
    }
    fclose(f);
    return n;
}

/* Диск PS1: размер — всех дорожек .cue или самого образа, подзаголовок — PS1. */
static void read_psx(Game *g, const char *dir)
{
    size_t n = strlen(g->path);

    name_from_file(g);
    copy_str(g->profile, sizeof(g->profile), "PS1");
    if (n > 4 && strcasecmp(g->path + n - 4, ".cue") == 0) {
        char files[16][256], path[1400];
        int k = library_cue_files(g->path, files, 16), i;
        for (i = 0; i < k; i++) {
            snprintf(path, sizeof(path), "%.1000s/%.255s", dir, files[i]);
            g->size += file_size(path);
        }
    } else {
        g->size = file_size(g->path);
    }
}

static int by_name(const void *a, const void *b)
{
    return strcasecmp(((const Game *)a)->name, ((const Game *)b)->name);
}

static int has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name);
    return n >= 5 && strcasecmp(name + n - 4, ext) == 0;
}

/* Образ .bin на полку не попадает, если это BIOS, дорожка из .cue или рядом лежит одноимённый .cue. */
static int lone_bin(const char *dir, const char *name, char (*names)[256], int count, char (*tracks)[256],
                    int ntracks)
{
    char path[1400];
    size_t n = strlen(name);
    int i;

    for (i = 0; i < ntracks; i++)
        if (!strcasecmp(tracks[i], name))
            return 0;
    for (i = 0; i < count; i++)
        if (has_ext(names[i], ".cue") && strlen(names[i]) == n && !strncasecmp(names[i], name, n - 4))
            return 0;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    return !psx_is_bios(path) && file_size(path) > (1L << 20);
}

#define SCAN_DEPTH 3     /* папка игр и ещё три уровня вложенных папок */

/* Одна папка: игры из неё, потом — вложенные папки (кроме bios, covers и скрытых). */
static void scan_dir(Library *lib, int *cap, const char *dir, int depth)
{
    BtDir *d;
    const char *name;
    int count = 0, ncap = 0, ntracks = 0, j;
    char (*names)[256] = NULL, (*tracks)[256] = NULL, path[1400];

    if (!(d = bt_dir_open(dir)))
        return;
    /* сначала все имена: чтобы скрыть дорожки .bin, надо знать, что называют .cue */
    while ((name = bt_dir_next(d)) != NULL) {
        if (strlen(name) >= 256 || name[0] == '.')
            continue;
        if (count == ncap) {
            char (*nn)[256] = realloc(names, (size_t)(ncap = ncap ? ncap * 2 : 64) * 256);
            if (!nn)
                break;
            names = nn;
        }
        snprintf(names[count++], 256, "%s", name);
    }
    bt_dir_close(d);
    for (j = 0; j < count; j++)
        if (has_ext(names[j], ".cue")) {
            char (*nt)[256] = realloc(tracks, (size_t)(ntracks + 16) * 256);
            if (!nt)
                break;
            tracks = nt;
            snprintf(path, sizeof(path), "%.1000s/%.255s", dir, names[j]);
            ntracks += library_cue_files(path, tracks + ntracks, 16);
        }
    for (j = 0; j < count; j++) {
        size_t n;
        int nes, psx;
        Game *g;

        name = names[j];
        n = strlen(name);
        snprintf(path, sizeof(path), "%.1000s/%.255s", dir, name);
        if (bt_is_dir(path)) {
            if (depth < SCAN_DEPTH && strcasecmp(name, "bios") && strcasecmp(name, "covers"))
                scan_dir(lib, cap, path, depth + 1);
            continue;
        }
        nes = has_ext(name, ".nes");
        psx = has_ext(name, ".cue") || has_ext(name, ".iso") || has_ext(name, ".img") || has_ext(name, ".pbp") ||
              (has_ext(name, ".bin") && lone_bin(dir, name, names, count, tracks, ntracks));
        if (n < 5 || (!has_ext(name, ".jar") && !nes && !psx) || n - 4 >= sizeof(g->base) ||
            strlen(path) >= sizeof(g->path))
            continue;
        if (lib->count == *cap) {
            Game *ng = realloc(lib->games, (size_t)(*cap = *cap ? *cap * 2 : 16) * sizeof(Game));
            if (!ng)
                break;
            lib->games = ng;
        }
        g = &lib->games[lib->count++];
        memset(g, 0, sizeof(*g));
        snprintf(g->path, sizeof(g->path), "%.1023s", path);
        memcpy(g->base, name, n - 4);
        g->base[n - 4] = '\0';
        g->kind = nes ? GAME_NES : psx ? GAME_PSX : GAME_J2ME;
        if (nes)
            read_nes(g);
        else if (psx)
            read_psx(g, dir);
        else
            read_game(g);
    }
    free(names);
    free(tracks);
}

int library_scan(Library *lib, const char *dir)
{
    int cap = 0;

    memset(lib, 0, sizeof(*lib));
    copy_str(lib->dir, sizeof(lib->dir), dir);
    if (!bt_is_dir(dir))
        return -1;
    scan_dir(lib, &cap, dir, 0);
    if (lib->count)
        qsort(lib->games, (size_t)lib->count, sizeof(Game), by_name);
    return 0;
}

void library_free(Library *lib)
{
    int i;

    for (i = 0; i < lib->count; i++) {
        image_free(&lib->games[i].icon);
        image_free(&lib->games[i].cover);
        image_free(&lib->games[i].cover_small);
    }
    free(lib->games);
    memset(lib, 0, sizeof(*lib));
}

static uint32_t name_hash(const char *s)
{
    uint32_t h = 2166136261u;

    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static uint32_t hsv(unsigned h, unsigned s, unsigned v)   /* h 0..359, s/v 0..255 */
{
    unsigned region = h / 60, rem = (h % 60) * 255 / 60;
    unsigned p = v * (255 - s) / 255, q = v * (255 - s * rem / 255) / 255;
    unsigned t = v * (255 - s * (255 - rem) / 255) / 255, r, g, b;

    switch (region) {
    case 0:  r = v; g = t; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = t; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* Обложка без надписей: цветной фон по хэшу названия, полиграфическая штриховка,
 * «логотип-овал» сверху и крупная пиксельная иконка игры (или диск, если иконки нет). */
static int imax(int a, int b) { return a > b ? a : b; }

/* Диск PS1: тёмная рабочая сторона с фиолетово-синим отливом, прозрачное кольцо у центра. */
static void psx_disc(Image *d, int cx, int cy, int r, unsigned hue)
{
    int hole = r / 7, ring = r * 2 / 5, x, y;
    uint32_t tint = hsv((hue + 240) % 360, 90, 120);

    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++) {
            int px = cx + x, py = cy + y, dd = x * x + y * y;
            uint32_t c;
            float a, sheen;
            if (px < 0 || py < 0 || px >= d->w || py >= d->h || dd > r * r || dd < hole * hole)
                continue;
            if (dd < ring * ring)
                c = dd > (ring - 2) * (ring - 2) ? 0xFF5A5E68u : 0xFFB9BDC6u;
            else if (dd > (r - 2) * (r - 2))
                c = 0xFF2A2C33u;
            else {
                a = atan2f((float)y, (float)x);
                sheen = cosf(a * 2 + 0.6f);
                sheen = sheen > 0.5f ? (sheen - 0.5f) * 2 : 0;
                c = gfx_mix(0xFF101116u, tint, (unsigned)(60 + sheen * 150));
            }
            d->px[py * d->w + px] = c;
        }
}

static Image make_cover(const Game *g, int w, int h)
{
    Image c = image_new(w, h);
    uint32_t hs = name_hash(g->name);
    unsigned hue = hs % 360;
    uint32_t top = hsv(hue, 150, 170), bottom = hsv((hue + 35) % 360, 190, 70);
    int y, k, band = h / 6, ow, oh;

    if (!c.px)
        return c;
    gfx_vgradient(&c, 0, 0, w, h, top, bottom);
    for (y = 0; y < h; y++)
        for (k = (y + (int)(hs >> 9) % 12) % 12; k < w; k += 12)
            gfx_fill(&c, k, y, 3, 1, 0x16FFFFFFu);
    /* полоса-«шапка» с овальным значком, как у коробок приставок */
    gfx_fill(&c, 0, 0, w, band, 0x50000000u);
    ow = w / 2;
    oh = band * 3 / 5;
    for (y = 0; y < oh; y++) {
        int dy = y * 2 - oh + 1, half = (int)((float)ow / 2 * sqrtf(1.0f - (float)(dy * dy) / (float)(oh * oh)));
        gfx_fill(&c, w / 2 - half, (band - oh) / 2 + y, half * 2, 1, hsv((hue + 180) % 360, 120, 230));
    }
    gfx_fill(&c, w / 2 - ow / 4, band / 2 - 1, ow / 2, 2, 0x60FFFFFFu);

    if (g->icon.px) {
        /* целый множитель, чтобы пиксель-арт иконки остался ровным */
        int area_w = w * 3 / 4, area_h = h - band * 2;
        int s = area_w / g->icon.w < area_h / g->icon.h ? area_w / g->icon.w : area_h / g->icon.h;
        int iw, ih, ix, iy;

        if (s < 1)
            s = 1;
        iw = g->icon.w * s;
        ih = g->icon.h * s;
        ix = (w - iw) / 2;
        iy = band + (h - band - ih) / 2;
        gfx_fill(&c, ix + 2, iy + 3, iw, ih, 0x40000000u);
        gfx_blit_scaled(&c, &g->icon, ix, iy, iw, ih, 255);
    } else if (g->kind == GAME_NES) {
        /* картридж: серый корпус, цветная наклейка, рёбра снизу */
        int cw = w * 3 / 5, ch = cw * 11 / 10, cx = (w - cw) / 2, cy = band + (h - band - ch) / 2, i;
        gfx_fill(&c, cx + 3, cy + 4, cw, ch, 0x40000000u);
        gfx_fill(&c, cx, cy, cw, ch, 0xFF8E8C96u);
        gfx_fill(&c, cx, cy, cw, 2, 0xFFB4B2BCu);
        gfx_fill(&c, cx + cw / 8, cy + ch / 8, cw * 3 / 4, ch / 2, hsv((hue + 180) % 360, 140, 220));
        gfx_fill(&c, cx + cw / 8, cy + ch / 8, cw * 3 / 4, ch / 12, 0x50FFFFFFu);
        for (i = 0; i < 5; i++)
            gfx_fill(&c, cx + cw / 6 + i * (cw * 2 / 3) / 5, cy + ch * 3 / 4, imax(1, cw / 24), ch / 6, 0xFF6C6A74u);
    } else if (g->kind == GAME_PSX) {
        psx_disc(&c, w / 2, band + (h - band) / 2, w * 3 / 8, hue);
    } else {
        gfx_disc(&c, w / 2, band + (h - band) / 2, w / 3, (float)(hs % 100) / 100.0f, 255);
    }
    /* нижняя «плашка» без текста — чтобы коробка выглядела законченной */
    gfx_fill(&c, 0, h - h / 14, w, h / 14, 0x40000000u);
    return c;
}

/* Вырезает из картинки центральную часть нужных пропорций — обложка заполняет бокс без искажений. */
static Image crop_to_aspect(const Image *src, int w, int h)
{
    int cw = src->w, ch = src->w * h / w, x0, y0, y;
    Image out;

    if (ch > src->h) {
        ch = src->h;
        cw = src->h * w / h;
    }
    x0 = (src->w - cw) / 2;
    y0 = (src->h - ch) / 2;
    out = image_new(cw, ch);
    for (y = 0; out.px && y < ch; y++)
        memcpy(out.px + (size_t)y * cw, src->px + (size_t)(y0 + y) * src->w + x0, (size_t)cw * sizeof(uint32_t));
    return out;
}

void library_load_cover(const Library *lib, Game *g, int w, int h)
{
    char path[1400];
    Image src = { NULL, 0, 0 };

    /* обложка рядом с игрой (в её папке), потом — в корне полки и в covers */
    snprintf(path, sizeof(path), "%.*s.png", (int)(strlen(g->path) - 4), g->path);
    src = load_png_file(path);
    if (!src.px) {
        snprintf(path, sizeof(path), "%s/%s.png", lib->dir, g->base);
        src = load_png_file(path);
    }
    if (!src.px) {
        snprintf(path, sizeof(path), "%s/covers/%s.png", lib->dir, g->base);
        src = load_png_file(path);
    }
    if (!src.px) {
        /* своей обложки нет — заставка «нажмите, чтобы играть», снятая при первом запуске */
        char data[800];
        bt_data_dir(data, sizeof(data));
        snprintf(path, sizeof(path), "%s/shots/%s/title.png", data, g->base);
        src = load_png_file(path);
    }
    image_free(&g->cover);
    if (src.px) {
        Image crop = crop_to_aspect(&src, w, h);
        g->cover = image_resize(crop.px ? &crop : &src, w, h);
        image_free(&crop);
        g->cover_custom = 1;
        image_free(&src);
    } else {
        g->cover = make_cover(g, w, h);
        g->cover_custom = 0;
    }
    g->cover_ready = 1;
}
