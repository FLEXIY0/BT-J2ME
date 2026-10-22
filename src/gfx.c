#include "gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "font_big.h"
#include "font_large.h"
#include "font_largeb.h"
#include "font_mid.h"
#include "font_small.h"

const Font font_small  = { FONT_SMALL_W, FONT_SMALL_H, FONT_SMALL_COUNT, font_small_cps, font_small_bits };
const Font font_big    = { FONT_BIG_W, FONT_BIG_H, FONT_BIG_COUNT, font_big_cps, font_big_bits };
const Font font_mid    = { FONT_MID_W, FONT_MID_H, FONT_MID_COUNT, font_mid_cps, font_mid_bits };
const Font font_large  = { FONT_LARGE_W, FONT_LARGE_H, FONT_LARGE_COUNT, font_large_cps, font_large_bits };
const Font font_largeb = { FONT_LARGEB_W, FONT_LARGEB_H, FONT_LARGEB_COUNT, font_largeb_cps, font_largeb_bits };

Image image_new(int w, int h)
{
    Image img;

    img.px = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    img.w = img.px ? w : 0;
    img.h = img.px ? h : 0;
    return img;
}

void image_free(Image *img)
{
    free(img->px);
    memset(img, 0, sizeof(*img));
}

Image image_resize(const Image *src, int w, int h)
{
    Image dst = image_new(w, h);
    int x, y;

    for (y = 0; y < dst.h; y++) {
        int sy0 = y * src->h / h, sy1 = (y + 1) * src->h / h;

        if (sy1 <= sy0)
            sy1 = sy0 + 1;
        for (x = 0; x < dst.w; x++) {
            int sx0 = x * src->w / w, sx1 = (x + 1) * src->w / w, sx, sy;
            uint32_t a = 0, r = 0, g = 0, b = 0, n = 0;

            if (sx1 <= sx0)
                sx1 = sx0 + 1;
            for (sy = sy0; sy < sy1; sy++)
                for (sx = sx0; sx < sx1; sx++) {
                    uint32_t p = src->px[sy * src->w + sx], pa = p >> 24;
                    /* цвет взвешиваем по альфе, иначе у прозрачных краёв появляется ореол */
                    a += pa;
                    r += ((p >> 16) & 255) * pa;
                    g += ((p >> 8) & 255) * pa;
                    b += (p & 255) * pa;
                    n++;
                }
            dst.px[y * w + x] = a ? ((a / n) << 24) | ((r / a) << 16) | ((g / a) << 8) | (b / a) : 0;
        }
    }
    return dst;
}

static int clip(const Image *d, int *x, int *y, int *w, int *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > d->w) *w = d->w - *x;
    if (*y + *h > d->h) *h = d->h - *y;
    return *w > 0 && *h > 0;
}

void gfx_fill(Image *d, int x, int y, int w, int h, uint32_t c)
{
    unsigned a = c >> 24;
    int i, j;

    if (!a || !clip(d, &x, &y, &w, &h))
        return;
    for (j = y; j < y + h; j++) {
        uint32_t *row = d->px + j * d->w;
        for (i = x; i < x + w; i++)
            row[i] = a == 255 ? c : gfx_mix(row[i], c, a);
    }
}

static uint32_t lerp_color(uint32_t a, uint32_t b, int t, int n)
{
    uint32_t r = 0;
    int s;

    for (s = 0; s < 32; s += 8) {
        int ca = (a >> s) & 255, cb = (b >> s) & 255;
        r |= (uint32_t)(ca + (cb - ca) * t / n) << s;
    }
    return r;
}

void gfx_vgradient(Image *d, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    int j;

    for (j = 0; j < h; j++)
        gfx_fill(d, x, y + j, w, 1, lerp_color(top, bottom, j, h > 1 ? h - 1 : 1));
}

void gfx_frame(Image *d, int x, int y, int w, int h, uint32_t c)
{
    gfx_fill(d, x, y, w, 1, c);
    gfx_fill(d, x, y + h - 1, w, 1, c);
    gfx_fill(d, x, y + 1, 1, h - 2, c);
    gfx_fill(d, x + w - 1, y + 1, 1, h - 2, c);
}

void gfx_blit_scaled(Image *d, const Image *s, int x, int y, int w, int h, int alpha)
{
    int i, j;

    if (w <= 0 || h <= 0 || alpha <= 0)
        return;
    for (j = 0; j < h; j++) {
        int dy = y + j, sy = j * s->h / h;
        if (dy < 0 || dy >= d->h)
            continue;
        for (i = 0; i < w; i++) {
            int dx = x + i;
            uint32_t p;
            unsigned a;

            if (dx < 0 || dx >= d->w)
                continue;
            p = s->px[sy * s->w + i * s->w / w];
            a = (p >> 24) * (unsigned)alpha / 255;
            if (a)
                d->px[dy * d->w + dx] = a == 255 ? p : gfx_mix(d->px[dy * d->w + dx], p, a);
        }
    }
}

void gfx_blit(Image *d, const Image *s, int x, int y, int alpha)
{
    gfx_blit_scaled(d, s, x, y, s->w, s->h, alpha);
}

static uint32_t hue(float t)
{
    float r = fabsf(t * 6 - 3) - 1, g = 2 - fabsf(t * 6 - 2), b = 2 - fabsf(t * 6 - 4);

    r = r < 0 ? 0 : r > 1 ? 1 : r;
    g = g < 0 ? 0 : g > 1 ? 1 : g;
    b = b < 0 ? 0 : b > 1 ? 1 : b;
    return 0xFF000000u | ((uint32_t)(r * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(b * 255);
}

void gfx_disc(Image *d, int cx, int cy, int r, float angle, int alpha)
{
    const float two_pi = 6.2831853f;
    int hole = r / 7, ring = r * 2 / 5, x, y;

    for (y = -r; y <= r; y++) {
        int py = cy + y;
        if (py < 0 || py >= d->h)
            continue;
        for (x = -r; x <= r; x++) {
            int px = cx + x, dd = x * x + y * y;
            uint32_t c;
            float a, sheen;

            if (px < 0 || px >= d->w || dd > r * r || dd < hole * hole)
                continue;
            if (dd < ring * ring && dd > (ring - 2) * (ring - 2))
                c = 0xFFB8BEC6u;                       /* кромка прозрачного центра */
            else if (dd < ring * ring)
                c = 0xFFD8DCE2u;
            else {
                a = atan2f((float)y, (float)x) / two_pi + angle;
                a -= floorf(a);
                /* Два радужных блика напротив друг друга поверх серебра. */
                sheen = cosf(a * 2 * two_pi);
                sheen = sheen > 0.55f ? (sheen - 0.55f) / 0.45f : 0;
                c = gfx_mix(0xFFC9CED6u, hue(fmodf(a * 2 + (float)dd / (float)(r * r) * 0.5f, 1)),
                            (unsigned)(sheen * 200));
                if (dd > (r - 1) * (r - 1))
                    c = 0xFF8A9099u;
            }
            d->px[py * d->w + px] = alpha >= 255 ? c : gfx_mix(d->px[py * d->w + px], c, (unsigned)alpha);
        }
    }
}

static const char *utf8_next(const char *s, unsigned *cp)
{
    const unsigned char *u = (const unsigned char *)s;

    if (u[0] < 0x80) { *cp = u[0]; return s + 1; }
    if ((u[0] & 0xE0) == 0xC0 && u[1]) { *cp = ((u[0] & 31u) << 6) | (u[1] & 63u); return s + 2; }
    if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2]) {
        *cp = ((u[0] & 15u) << 12) | ((u[1] & 63u) << 6) | (u[2] & 63u);
        return s + 3;
    }
    *cp = '?';
    return s + 1;
}

static int glyph_index(const Font *f, unsigned cp)
{
    int lo = 0, hi = f->count - 1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->cps[mid] == cp)
            return mid;
        if (f->cps[mid] < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return cp == '?' ? -1 : glyph_index(f, '?');
}

const uint8_t *gfx_glyph(const Font *f, unsigned cp)
{
    int gi = glyph_index(f, cp);

    return gi < 0 ? NULL : f->bits + (size_t)gi * (size_t)f->h * (size_t)((f->w + 7) / 8);
}

int gfx_text_width(const Font *f, const char *s, int scale)
{
    int n = 0;
    unsigned cp;

    while (*s) {
        s = utf8_next(s, &cp);
        n++;
    }
    return n * f->w * scale;
}

int gfx_text(Image *d, const Font *f, int x, int y, const char *s, uint32_t c, int scale)
{
    int bpr = (f->w + 7) / 8, x0 = x;
    unsigned cp;

    while (*s) {
        int gi, gx, gy;

        s = utf8_next(s, &cp);
        gi = glyph_index(f, cp);
        if (gi >= 0)
            for (gy = 0; gy < f->h; gy++) {
                const uint8_t *row = f->bits + ((size_t)gi * f->h + gy) * bpr;
                for (gx = 0; gx < f->w; gx++)
                    if (row[gx >> 3] & (0x80 >> (gx & 7)))
                        gfx_fill(d, x + gx * scale, y + gy * scale, scale, scale, c);
            }
        x += f->w * scale;
    }
    return x - x0;
}
