/* Носители для полки в пиксельном стиле: плоские, одного размера, с тёмным контуром и
 * «толщиной» снизу. Рисуются в маленьком разрешении (MEDIA_PIXEL экранных точек на пиксель),
 * обложка уменьшается до него же — крупный пиксель у всего носителя одинаковый. */
#include "media.h"
#include "library.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define OUTLINE 0xFF14131Au

float media_aspect(int kind)
{
    (void)kind;
    return 0.8f;        /* все носители одного размера */
}

float media_front_aspect(int kind)
{
    switch (kind) {
    case GAME_NES:  return 1.25f;   /* наклейка картриджа */
    case GAME_PSX:  return 0.92f;
    case GAME_PS2:  return 0.82f;
    default:        return 0.85f;   /* наклейка карты памяти */
    }
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static uint32_t shade(uint32_t c, int k)    /* k: -255 (чёрный) .. 255 (белый) */
{
    return k < 0 ? gfx_mix(c, 0xFF000000u, (unsigned)-k) : gfx_mix(c, 0xFFFFFFFFu, (unsigned)k);
}

static int opaque(const Image *d, int x, int y)
{
    return x >= 0 && y >= 0 && x < d->w && y < d->h && (d->px[y * d->w + x] >> 24);
}

/* Прямоугольник со срезанными на r пикселей углами — пиксельное «скругление». */
static void rbox(Image *d, int x, int y, int w, int h, int r, uint32_t c)
{
    int j;

    for (j = 0; j < h; j++) {
        int in = j < r ? r - j : j >= h - r ? j - (h - r - 1) : 0;
        gfx_fill(d, x + in, y + j, w - 2 * in, 1, c);
    }
}

/* Обложка, кадрированная по центру под w × h (top — откуда брать по высоте: 0..1). */
static void cover_fill(Image *d, const Image *src, int x, int y, int w, int h, float top)
{
    int cw = src->w, ch = src->w * h / imax(1, w), x0, y0, i, j;
    Image crop, sc;

    if (w < 1 || h < 1)
        return;
    if (ch > src->h) {
        ch = src->h;
        cw = src->h * w / imax(1, h);
    }
    x0 = (src->w - cw) / 2;
    y0 = (int)((float)(src->h - ch) * top);
    crop = image_new(cw, ch);
    if (!crop.px)
        return;
    for (j = 0; j < ch; j++)
        memcpy(crop.px + (size_t)j * cw, src->px + (size_t)(y0 + j) * src->w + x0, (size_t)cw * 4);
    sc = image_resize(&crop, w, h);
    image_free(&crop);
    for (j = 0; sc.px && j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i >= 0 && y + j >= 0 && x + i < d->w && y + j < d->h)
                d->px[(y + j) * d->w + x + i] = sc.px[j * w + i] | 0xFF000000u;
    image_free(&sc);
}

/* Пиксельная картинка из строк: буква — цвет из таблицы pal ("r" → pal['r']). */
static void sprite(Image *d, int x, int y, const char *const *rows, int n, const uint32_t *pal)
{
    int i, j;

    for (j = 0; j < n; j++)
        for (i = 0; rows[j][i]; i++)
            if (rows[j][i] != '.')
                gfx_fill(d, x + i, y + j, 1, 1, pal[(unsigned char)rows[j][i]]);
}

/* Старый логотип PlayStation 5×5: красная «P» стоит, «S» из трёх цветов лежит. */
static void psx_logo(Image *d, int x, int y)
{
    static const char *const rows[] = { ".rr..", ".r.r.", ".rr..", "yrggb", ".r.bb" };
    uint32_t pal[128] = { 0 };

    pal['r'] = 0xFFE8303Au;
    pal['y'] = 0xFFF2C21Bu;
    pal['g'] = 0xFF2FB36Bu;
    pal['b'] = 0xFF3A7FE0u;
    sprite(d, x, y, rows, 5, pal);
}

/* Логотип PS2 — тонкие буквы, как на коробках. */
static void ps2_logo(Image *d, int x, int y, uint32_t c)
{
    static const char *const rows[] = {
        "ccc.ccc.ccc", "c.c.c.....c", "ccc.ccc.ccc", "c.....c.c..", "c...ccc.ccc" };
    uint32_t pal[128] = { 0 };

    pal['c'] = c;
    sprite(d, x, y, rows, 5, pal);
}

/* Контур и толщина: края фигуры — тёмные, нижние ряды — темнее (носитель «стоит»). */
static void finish(Image *d, int depth)
{
    Image c = image_new(d->w, d->h);
    int x, y;

    if (!c.px)
        return;
    memcpy(c.px, d->px, (size_t)d->w * d->h * 4);
    for (y = 0; y < d->h; y++)
        for (x = 0; x < d->w; x++) {
            uint32_t *p = &d->px[y * d->w + x];
            if (!opaque(&c, x, y))
                continue;
            if (!opaque(&c, x - 1, y) || !opaque(&c, x + 1, y) || !opaque(&c, x, y - 1) || !opaque(&c, x, y + 1))
                *p = OUTLINE;
            else if (!opaque(&c, x, y + depth + 1))
                *p = shade(*p, -110);
            else if (!opaque(&c, x, y - 2))
                *p = shade(*p, 60);         /* светлая кромка сверху */
        }
    image_free(&c);
}

/* Коробка компакт-диска PS1: чёрный корешок слева с логотипом, обложка под прозрачной крышкой. */
static void draw_ps1(const Media *m, Image *d)
{
    int w = d->w, h = d->h - 2, sw = imax(4, w * 15 / 100);

    rbox(d, 0, 0, w, h, 1, 0xFF2A2B33u);
    gfx_fill(d, 1, 1, sw, h - 2, 0xFF0B0B0Eu);
    if (sw >= 6)
        psx_logo(d, (sw - 5) / 2 + 1, 2);
    gfx_fill(d, sw / 2, 9, 1, h - 13, 0xFF9A9CA6u);      /* надпись на корешке — светлая черта */
    if (m->front)
        cover_fill(d, m->front, sw + 2, 2, w - sw - 4, h - 4, 0.5f);
    gfx_fill(d, sw + 1, 1, 1, h - 2, 0xFFC9CCD6u);       /* петля крышки */
    gfx_fill(d, sw + 2, 1, w - sw - 3, 1, 0xFFB4B8C4u);
}

/* Коробка DVD PS2: тёмно-синий пластик, чёрная шапка с логотипом, обложка ниже. */
static void draw_ps2(const Media *m, Image *d)
{
    int w = d->w, h = d->h - 2, band = imax(7, h * 13 / 100);

    rbox(d, 0, 0, w, h, 1, 0xFF15204Au);
    gfx_fill(d, 2, 2, w - 4, band - 2, 0xFF050608u);
    ps2_logo(d, 4, 2 + (band - 2 - 5) / 2, 0xFFDDE3F2u);
    gfx_fill(d, w - 6, 2 + (band - 2 - 3) / 2, 3, 3, 0xFF3A7FE0u);    /* значок справа в шапке */
    if (m->front)
        cover_fill(d, m->front, 2, band + 1, w - 4, h - band - 3, 0.5f);
}

/* Картридж NES: серый корпус, рифлёный верх, наклейка с обложкой, рёбра и стрелка внизу. */
static void draw_nes(const Media *m, Image *d)
{
    int w = d->w, h = d->h - 2, i, lx = imax(3, w / 9), ly = 4, lw = w - 2 * lx, lh = h * 58 / 100;
    uint32_t body = 0xFF8E8D95u, low = 0xFF6F6E77u;

    rbox(d, 0, 0, w, h, 1, body);
    for (i = 2; i < w - 2; i += 3)                       /* рифление сверху */
        gfx_fill(d, i, 1, 1, 2, shade(body, -50));
    gfx_fill(d, lx - 1, ly - 1, lw + 2, lh + 2, shade(body, -70));
    if (m->front)
        cover_fill(d, m->front, lx, ly, lw, lh, 0.15f);
    else
        gfx_fill(d, lx, ly, lw, lh, m->bg);
    gfx_fill(d, 1, ly + lh + 3, w - 2, h - ly - lh - 4, low);
    for (i = ly + lh + 5; i < h - 2; i += 2) {
        gfx_fill(d, 3, i, w / 4, 1, shade(low, -50));
        gfx_fill(d, w - 3 - w / 4, i, w / 4, 1, shade(low, -50));
    }
    for (i = 0; i < 3; i++)                              /* стрелка «вставлять сюда» */
        gfx_fill(d, w / 2 - 3 + i, ly + lh + 6 + i, 7 - 2 * i, 1, shade(low, -60));
}

/* Карта памяти J2ME: корпус и наклейка — два цвета игры, срезанный угол, контакты сверху. */
static void draw_sd(const Media *m, Image *d)
{
    int w = d->w, h = d->h - 2, cut = w * 22 / 100, i, j, lx = 3, ly = h * 22 / 100, lw = w - 6, lh = h * 66 / 100;
    uint32_t body = shade(m->bg, -80), label = shade(m->fg, 40);

    rbox(d, 0, 0, w, h, 1, body);
    for (j = 0; j < cut; j++)                            /* срезанный правый верхний угол */
        for (i = w - cut + j; i < w; i++)
            d->px[j * w + i] = 0;
    for (i = 3; i + 2 < w - cut; i += 4)                 /* контакты */
        gfx_fill(d, i, 2, 2, ly - 4, shade(label, 20));
    gfx_fill(d, lx, ly, lw, lh, label);
    if (m->front)
        cover_fill(d, m->front, lx + 2, ly + 2, lw - 4, lh - 4, 0.5f);
    else if (m->icon && m->icon->px) {
        /* целый множитель — пиксель-арт иконки остаётся ровным; крупная иконка — уменьшаем */
        int k = imin((lw - 4) / m->icon->w, (lh - 4) / m->icon->h), iw, ih;
        if (k >= 1) {
            iw = m->icon->w * k;
            ih = m->icon->h * k;
        } else {
            float f = fminf((float)(lw - 4) / (float)m->icon->w, (float)(lh - 4) / (float)m->icon->h);
            iw = (int)((float)m->icon->w * f);
            ih = (int)((float)m->icon->h * f);
        }
        gfx_blit_scaled(d, m->icon, lx + (lw - iw) / 2, ly + (lh - ih) / 2, iw, ih, 255);
    }
    gfx_fill(d, 0, h * 30 / 100, 1, h / 9, shade(body, 60));     /* защёлка защиты записи */
}

Image media_render(const Media *m, int w, int h)
{
    Image d = image_new(w, h);

    if (!d.px)
        return d;
    memset(d.px, 0, (size_t)w * h * 4);
    switch (m->kind) {
    case GAME_PSX: draw_ps1(m, &d); break;
    case GAME_PS2: draw_ps2(m, &d); break;
    case GAME_NES: draw_nes(m, &d); break;
    default:       draw_sd(m, &d);  break;
    }
    finish(&d, 1);
    return d;
}

/* ---------- цвета игры ---------- */

static void rgb_hsv(uint32_t c, float *h, float *s, float *v)
{
    float r = (float)((c >> 16) & 255) / 255, g = (float)((c >> 8) & 255) / 255, b = (float)(c & 255) / 255;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;

    *v = mx;
    *s = mx > 0 ? d / mx : 0;
    if (d <= 0)
        *h = 0;
    else if (mx == r)
        *h = fmodf((g - b) / d + 6, 6) * 60;
    else if (mx == g)
        *h = ((b - r) / d + 2) * 60;
    else
        *h = ((r - g) / d + 4) * 60;
}

void media_tones(const Image *img, uint32_t *bg, uint32_t *fg)
{
    /* гистограмма по 4 бита на канал; в каждой ячейке копим сумму цвета, чтобы взять средний */
    static uint32_t cnt[4096], sum[4096][3];
    int i, step = imax(1, (img->w * img->h) / 20000), best = -1, acc = -1;
    float best_sc = 0, acc_sc = 0, bh, bs, bv;

    memset(cnt, 0, sizeof(cnt));
    memset(sum, 0, sizeof(sum));
    for (i = 0; i < img->w * img->h; i += step) {
        uint32_t p = img->px[i];
        int k;
        if ((p >> 24) < 128)
            continue;
        k = (int)(((p >> 20) & 15) << 8 | ((p >> 12) & 15) << 4 | ((p >> 4) & 15));
        cnt[k]++;
        sum[k][0] += (p >> 16) & 255;
        sum[k][1] += (p >> 8) & 255;
        sum[k][2] += p & 255;
    }
    for (i = 0; i < 4096; i++) {
        float h, s, v, sc;
        uint32_t c;
        if (!cnt[i])
            continue;
        c = 0xFF000000u | (sum[i][0] / cnt[i]) << 16 | (sum[i][1] / cnt[i]) << 8 | (sum[i][2] / cnt[i]);
        rgb_hsv(c, &h, &s, &v);
        sc = (float)cnt[i] * (0.4f + s) * (v > 0.12f ? 1.0f : 0.3f);   /* почти чёрный — фон обложки, не цвет */
        if (sc > best_sc) {
            best_sc = sc;
            best = i;
        }
    }
    if (best < 0) {
        *bg = 0xFF404858u;
        *fg = 0xFF9C8FF0u;
        return;
    }
    *bg = 0xFF000000u | (sum[best][0] / cnt[best]) << 16 | (sum[best][1] / cnt[best]) << 8 | (sum[best][2] / cnt[best]);
    rgb_hsv(*bg, &bh, &bs, &bv);
    for (i = 0; i < 4096; i++) {
        float h, s, v, sc, dh;
        uint32_t c;
        if (!cnt[i] || i == best)
            continue;
        c = 0xFF000000u | (sum[i][0] / cnt[i]) << 16 | (sum[i][1] / cnt[i]) << 8 | (sum[i][2] / cnt[i]);
        rgb_hsv(c, &h, &s, &v);
        dh = fabsf(h - bh);
        dh = dh > 180 ? 360 - dh : dh;
        if (s < 0.3f || v < 0.3f || (dh < 40 && bs > 0.2f))
            continue;               /* акцент — заметный цвет, отличный от основного */
        sc = (float)cnt[i] * s * s * v;
        if (sc > acc_sc) {
            acc_sc = sc;
            acc = i;
        }
    }
    if (acc >= 0)
        *fg = 0xFF000000u | (sum[acc][0] / cnt[acc]) << 16 | (sum[acc][1] / cnt[acc]) << 8 | (sum[acc][2] / cnt[acc]);
    else
        *fg = shade(*bg, bv < 0.5f ? 150 : -120);
}
