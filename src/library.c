#define _DEFAULT_SOURCE
#include "library.h"
#include "jar.h"
#include "manifest.h"
#include "png.h"
#include "util.h"

#include <dirent.h>
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
    FILE *f = fopen(path, "rb");
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

static int by_name(const void *a, const void *b)
{
    return strcasecmp(((const Game *)a)->name, ((const Game *)b)->name);
}

int library_scan(Library *lib, const char *dir)
{
    DIR *d;
    struct dirent *de;
    int cap = 0;

    memset(lib, 0, sizeof(*lib));
    copy_str(lib->dir, sizeof(lib->dir), dir);
    if (!(d = opendir(dir)))
        return -1;
    while ((de = readdir(d)) != NULL) {
        size_t n = strlen(de->d_name);
        Game *g;

        if (n < 5 || strcasecmp(de->d_name + n - 4, ".jar") != 0 || n - 4 >= sizeof(g->base))
            continue;
        if (lib->count == cap) {
            Game *ng = realloc(lib->games, (size_t)(cap = cap ? cap * 2 : 16) * sizeof(Game));
            if (!ng)
                break;
            lib->games = ng;
        }
        g = &lib->games[lib->count++];
        memset(g, 0, sizeof(*g));
        snprintf(g->path, sizeof(g->path), "%s/%s", dir, de->d_name);
        memcpy(g->base, de->d_name, n - 4);
        g->base[n - 4] = '\0';
        read_game(g);
    }
    closedir(d);
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

/* Обложка «как в магазине дисков»: цветной фон по хэшу названия, полоса с платформой,
 * крупная пиксельная иконка игры и название внизу. */
static Image make_cover(const Game *g, int w, int h)
{
    Image c = image_new(w, h);
    uint32_t hs = name_hash(g->name);
    unsigned hue = hs % 360;
    uint32_t top = hsv(hue, 170, 150), bottom = hsv((hue + 40) % 360, 200, 60), ink = 0xFFF2F2F2u;
    int band = h / 7, y, k, tw;
    char line[160];

    if (!c.px)
        return c;
    gfx_vgradient(&c, 0, 0, w, h, top, bottom);
    /* Диагональные полосы — фактура «полиграфии» */
    for (y = 0; y < h; y++)
        for (k = (y + (int)(hs >> 9) % 16) % 16; k < w; k += 16)
            gfx_fill(&c, k, y, 4, 1, 0x14FFFFFFu);

    gfx_fill(&c, 0, 0, w, band, 0xE0101014u);
    gfx_text(&c, &font_small, 4, (band - font_small.h) / 2, "J2ME", 0xFFFFD54Fu, 1);
    snprintf(line, sizeof(line), "%s", g->profile[0] ? g->profile : "MIDP");
    gfx_text(&c, &font_small, w - 4 - gfx_text_width(&font_small, line, 1), (band - font_small.h) / 2, line,
             0xFFB0B6C0u, 1);

    if (g->icon.px) {
        /* Целый множитель, чтобы пиксель-арт иконки остался ровным. */
        int area = h - band * 3, s = area / (g->icon.h > g->icon.w ? g->icon.h : g->icon.w);
        int iw, ih, ix, iy;

        if (s < 1)
            s = 1;
        iw = g->icon.w * s;
        ih = g->icon.h * s;
        ix = (w - iw) / 2;
        iy = band + (area + band - ih) / 2;
        gfx_fill(&c, ix - 3, iy - 3, iw + 6, ih + 6, 0x40000000u);
        gfx_blit_scaled(&c, &g->icon, ix, iy, iw, ih, 255);
    } else {
        gfx_disc(&c, w / 2, band + (h - band * 2) / 2, (h - band * 3) / 3, (float)(hs % 100) / 100.0f, 255);
    }

    /* Название: если не влезает — обрезаем по символам. */
    snprintf(line, sizeof(line), "%s", g->name);
    while ((tw = gfx_text_width(&font_small, line, 1)) > w - 8 && strlen(line) > 1) {
        size_t n = strlen(line);
        unsigned char ch;

        /* убираем последний символ целиком, вместе с байтами продолжения UTF-8 */
        do
            ch = (unsigned char)line[--n];
        while (n && (ch & 0xC0) == 0x80);
        line[n] = '\0';
    }
    gfx_fill(&c, 0, h - band, w, band, 0xC0000000u);
    gfx_text(&c, &font_small, (w - tw) / 2, h - band + (band - font_small.h) / 2, line, ink, 1);
    return c;
}

void library_load_cover(const Library *lib, Game *g, int w, int h)
{
    char path[1400];
    Image src = { NULL, 0, 0 };

    snprintf(path, sizeof(path), "%s/%s.png", lib->dir, g->base);
    src = load_png_file(path);
    if (!src.px) {
        snprintf(path, sizeof(path), "%s/covers/%s.png", lib->dir, g->base);
        src = load_png_file(path);
    }
    image_free(&g->cover);
    if (src.px) {
        g->cover = image_resize(&src, w, h);
        g->cover_custom = 1;
        image_free(&src);
    } else {
        g->cover = make_cover(g, w, h);
        g->cover_custom = 0;
    }
    g->cover_ready = 1;
}
