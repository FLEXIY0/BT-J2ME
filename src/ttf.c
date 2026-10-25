/* Шрифты распаковываются при первом обращении; глиф (шрифт, размер, символ) растеризуется
 * один раз и хранится в простой хэш-таблице. */
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__GNUC__)       /* чужой код: неиспользуемые функции и прочие мелочи — не наши */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "../third_party/stb/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "ttf.h"
#include "shelf_assets.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct Glyph {
    struct Glyph *next;
    int           font, px, cp;
    int           w, h, xoff, yoff, adv;
    uint8_t      *a;
} Glyph;

static stbtt_fontinfo info[TTF_COUNT];
static unsigned char *data[TTF_COUNT];
static int            loaded[TTF_COUNT];
static Glyph         *table[1024];

static const stbtt_fontinfo *font_get(int f)
{
    static const unsigned char *const z[TTF_COUNT] = { font_inter_semibold_z, font_inter_medium_z,
                                                       font_inter_regular_z, font_mono_z };
    static const unsigned long zl[TTF_COUNT] = { sizeof(font_inter_semibold_z), sizeof(font_inter_medium_z),
                                                 sizeof(font_inter_regular_z), sizeof(font_mono_z) };
    static const unsigned long rl[TTF_COUNT] = { font_inter_semibold_size, font_inter_medium_size,
                                                 font_inter_regular_size, font_mono_size };
    uLongf n;

    if (f < 0 || f >= TTF_COUNT)
        f = TTF_REGULAR;
    if (loaded[f])
        return loaded[f] > 0 ? &info[f] : NULL;
    loaded[f] = -1;
    n = rl[f];
    if (!(data[f] = malloc(n)) || uncompress(data[f], &n, z[f], zl[f]) != Z_OK ||
        !stbtt_InitFont(&info[f], data[f], stbtt_GetFontOffsetForIndex(data[f], 0)))
        return NULL;
    loaded[f] = 1;
    return &info[f];
}

static const Glyph *glyph(int f, int px, int cp)
{
    unsigned h = ((unsigned)f * 31u + (unsigned)px * 131u + (unsigned)cp * 2654435761u) % 1024u;
    const stbtt_fontinfo *fi;
    Glyph *g;
    float sc;
    int adv, lsb, x0, y0, x1, y1, idx;

    for (g = table[h]; g; g = g->next)
        if (g->font == f && g->px == px && g->cp == cp)
            return g;
    if (!(fi = font_get(f)) || !(g = calloc(1, sizeof(*g))))
        return NULL;
    sc = stbtt_ScaleForPixelHeight(fi, (float)px);
    idx = stbtt_FindGlyphIndex(fi, cp);
    if (!idx && cp != ' ')
        idx = stbtt_FindGlyphIndex(fi, '?');
    stbtt_GetGlyphHMetrics(fi, idx, &adv, &lsb);
    stbtt_GetGlyphBitmapBox(fi, idx, sc, sc, &x0, &y0, &x1, &y1);
    g->font = f;
    g->px = px;
    g->cp = cp;
    g->adv = (int)((float)adv * sc + 0.5f);
    g->w = x1 - x0;
    g->h = y1 - y0;
    g->xoff = x0;
    g->yoff = y0;
    if (g->w > 0 && g->h > 0 && (g->a = malloc((size_t)g->w * (size_t)g->h)) != NULL)
        stbtt_MakeGlyphBitmap(fi, g->a, g->w, g->h, g->w, sc, sc, idx);
    g->next = table[h];
    table[h] = g;
    return g;
}

static int next_cp(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int c = *p++;

    if (c >= 0xC0 && (*p & 0xC0) == 0x80) {
        if (c < 0xE0) {
            c = (c & 0x1F) << 6 | (*p++ & 0x3F);
        } else if (c < 0xF0 && (p[1] & 0xC0) == 0x80) {
            c = (c & 0x0F) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F);
            p += 2;
        } else {
            c = '?';
            while ((*p & 0xC0) == 0x80)
                p++;
        }
    }
    *s = (const char *)p;
    return c;
}

static int ascent(int f, int px)
{
    const stbtt_fontinfo *fi = font_get(f);
    int a, d, l;

    if (!fi)
        return px;
    stbtt_GetFontVMetrics(fi, &a, &d, &l);
    return (int)((float)a * stbtt_ScaleForPixelHeight(fi, (float)px) + 0.5f);
}

int ttf_line(int font, int px)
{
    return ascent(font, px) + px / 4;
}

int ttf_width(int font, int px, const char *s)
{
    int w = 0;

    while (s && *s) {
        const Glyph *g = glyph(font, px, next_cp(&s));
        if (g)
            w += g->adv;
    }
    return w;
}

int ttf_draw(Image *d, int font, int px, int x, int y, const char *s, uint32_t argb)
{
    int x0 = x, base = y + ascent(font, px);
    unsigned ca = argb >> 24;

    while (s && *s) {
        const Glyph *g = glyph(font, px, next_cp(&s));
        int i, j;
        if (!g)
            continue;
        for (j = 0; g->a && j < g->h; j++) {
            int py = base + g->yoff + j;
            if (py < 0 || py >= d->h)
                continue;
            for (i = 0; i < g->w; i++) {
                int pxx = x + g->xoff + i;
                unsigned a = g->a[j * g->w + i] * ca / 255;
                uint32_t *p;
                if (!a || pxx < 0 || pxx >= d->w)
                    continue;
                p = &d->px[py * d->w + pxx];
                if ((*p >> 24) == 0) {      /* прозрачная картинка: кладём цвет с альфой глифа */
                    *p = (argb & 0xFFFFFFu) | (uint32_t)a << 24;
                } else {
                    uint32_t na = (*p >> 24) + a * (255 - (*p >> 24)) / 255;
                    *p = (gfx_mix(*p, argb, a) & 0xFFFFFFu) | na << 24;
                }
            }
        }
        x += g->adv;
    }
    return x - x0;
}

Image ttf_render(int font, int px, const char *s, uint32_t argb)
{
    Image im = image_new(ttf_width(font, px, s) + 2, ttf_line(font, px) + 2);

    if (im.px) {
        memset(im.px, 0, (size_t)im.w * (size_t)im.h * 4);
        ttf_draw(&im, font, px, 1, 1, s, argb);
    }
    return im;
}
