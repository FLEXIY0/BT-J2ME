/* Носители для полки в духе пакетов «3D Boxes»: коробки PS1 и PS2 с корешком слева, картридж
 * NES (фото Evan-Amos, public domain) с бумажной наклейкой-обложкой, карта microSD для J2ME.
 * Рисуем вдвое крупнее и уменьшаем с усреднением: так ровные края и мелкий текст корешка.
 * Сначала собираются плоские лицевая сторона и корешок, потом натягиваются на трапеции —
 * дальний край короче, как в перспективе. Сверху общий свет: верх светлее, низ темнее. */
#include "media.h"
#include "library.h"
#include "png.h"
#include "ttf.h"
#include "shelf_assets.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SS 2

/* Где на фото наклейка: доли ширины и высоты картинки (x0, y0, x1, y1). */
static const float NES_LABEL[4] = { 0.3888f, 0.0089f, 0.9363f, 0.7195f };
static const float SD_LABEL[4] = { 0.1893f, 0.3075f, 0.8797f, 0.9210f };

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static uint32_t shade(uint32_t c, int k)    /* k: -255 (чёрный) .. 255 (белый) */
{
    return k < 0 ? gfx_mix(c, 0xFF000000u, (unsigned)-k) : gfx_mix(c, 0xFFFFFFFFu, (unsigned)k);
}

float media_front_aspect(int kind)
{
    switch (kind) {
    case GAME_PS2: return 0.70f;
    case GAME_PSX: return 0.98f;
    case GAME_NES: return 0.69f;    /* наклейка картриджа */
    default:       return 0.83f;    /* наклейка карты памяти */
    }
}

float media_aspect(int kind)
{
    switch (kind) {
    case GAME_PS2: return 0.79f;
    case GAME_PSX: return 1.15f;
    case GAME_NES: return 1.01f;
    default:       return 0.74f;
    }
}

static Image asset(const unsigned char *png, size_t len)
{
    Image im = { NULL, 0, 0 };

    im.px = png_decode(png, len, &im.w, &im.h);
    if (!im.px)
        im.w = im.h = 0;
    return im;
}

/* Обложка, кадрированная под w × h (top — откуда брать по высоте: 0..1), в (x, y);
 * r > 0 — со скруглёнными углами. */
static void cover_fill(Image *d, const Image *src, int x, int y, int w, int h, float top, float r)
{
    int cw = src->w, ch = (int)((float)src->w * (float)h / (float)imax(1, w)), i, j;
    Image crop, sc, mask;

    if (w < 1 || h < 1)
        return;
    if (ch > src->h) {
        ch = src->h;
        cw = (int)((float)src->h * (float)w / (float)imax(1, h));
    }
    crop = image_crop(src, (src->w - cw) / 2, (int)((float)(src->h - ch) * top), cw, ch);
    sc = image_resize(&crop, w, h);
    image_free(&crop);
    mask = image_new(w, h);
    if (sc.px && mask.px) {
        memset(mask.px, 0, (size_t)w * (size_t)h * 4);
        gfx_rrect_aa(&mask, 0, 0, (float)w, (float)h, r > 0 ? r : 0.01f, 0xFFFFFFFFu);
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++) {
                int px = x + i, py = y + j;
                if (px >= 0 && py >= 0 && px < d->w && py < d->h)
                    gfx_over(&d->px[py * d->w + px], sc.px[j * w + i] & 0xFFFFFFu, mask.px[j * w + i] >> 24);
            }
    }
    image_free(&sc);
    image_free(&mask);
}

/* Мягкая косая полоса блика по прямоугольнику (x, y, w, h). */
static void gloss(Image *d, int x, int y, int w, int h, unsigned a)
{
    int i, j;
    float soft = (float)h / 30;

    for (j = 0; j < h; j++) {
        float t = (float)j / (float)h;
        float xl = ((0.55f - 0.43f * t)) * (float)w, xr = ((0.78f - 0.43f * t)) * (float)w;
        for (i = 0; i < w; i++) {
            float fi = (float)i, k = fminf(fi - xl, xr - fi) / soft;
            int px = x + i, py = y + j;
            if (k <= 0 || px < 0 || py < 0 || px >= d->w || py >= d->h || !(d->px[py * d->w + px] >> 24))
                continue;
            k = k > 1 ? 1 : k;
            d->px[py * d->w + px] = (d->px[py * d->w + px] & 0xFF000000u) |
                                    (gfx_mix(d->px[py * d->w + px], 0xFFFFFFu, (unsigned)(k * (float)a)) & 0xFFFFFFu);
        }
    }
}

/* Текст сверху вниз (повёрнут по часовой стрелке) с верхней точки; возвращает длину. */
static int vtext(Image *d, int x, int y, int font, int px, const char *t, uint32_t c, int max_len)
{
    Image im = ttf_render(font, px, t, c);
    int i, j, len = im.w;

    if (!im.px || len > max_len) {
        image_free(&im);
        return 0;
    }
    for (j = 0; j < im.h; j++)
        for (i = 0; i < im.w; i++) {
            uint32_t p = im.px[j * im.w + i];
            int dx = x + im.h - 1 - j, dy = y + i;
            if ((p >> 24) && dx >= 0 && dy >= 0 && dx < d->w && dy < d->h)
                gfx_over(&d->px[dy * d->w + dx], p & 0xFFFFFFu, p >> 24);
        }
    image_free(&im);
    return len;
}

/* Старый логотип PlayStation, упрощённо: красная «P» стоит, «S» из трёх цветов лежит. */
static void psx_logo(Image *d, float x, float y, float s)
{
    float a[] = { x, y + s * .62f, x + s * .28f, y + s * .55f, x + s * .28f, y + s * .7f, x, y + s * .78f };
    float b[] = { x + s * .47f, y + s * .55f, x + s, y + s * .62f, x + s, y + s * .74f, x + s * .47f, y + s * .68f };
    float c[] = { x + s * .47f, y + s * .8f, x + s, y + s * .78f, x + s * .8f, y + s, x + s * .3f, y + s };

    gfx_rrect_aa(d, x + s * .3f, y, s * .15f, s * .75f, 0.5f, 0xFFE8303Au);
    gfx_rrect_aa(d, x + s * .3f, y, s * .42f, s * .4f, s * .2f, 0xFFE8303Au);
    gfx_rrect_aa(d, x + s * .42f, y + s * .1f, s * .18f, s * .2f, s * .09f, 0xFF0A0A0Cu);
    gfx_poly(d, a, 4, 0xFFF2C21Bu);
    gfx_poly(d, b, 4, 0xFF2FB36Bu);
    gfx_poly(d, c, 4, 0xFF3A7FE0u);
}

/* Развёртку — на столбцы x0..x1: левый край от t0 до b0, правый от t1 до b1 (перспектива:
 * дальний край короче, и картинка к нему сжимается); dark — притемнение (торец в тени). */
static void warp(Image *d, const Image *tex, int x0, int x1, float t0, float b0, float t1, float b1, int dark)
{
    float h0 = b0 - t0, h1 = b1 - t1;
    int x, y;

    for (x = x0; x < x1; x++) {
        float t = ((float)(x - x0) + 0.5f) / (float)(x1 - x0);
        float u = t * h1 / ((1 - t) * h0 + t * h1), top = t0 + (t1 - t0) * t, bot = b0 + (b1 - b0) * t;
        int sx = imin(tex->w - 1, (int)(u * (float)tex->w));
        if (x < 0 || x >= d->w)
            continue;
        for (y = (int)top; y <= (int)bot && y < d->h; y++) {
            float v = ((float)y + 0.5f - top) / (bot - top), cov = 1;
            uint32_t p;
            unsigned a;
            if (y < 0 || v < 0 || v >= 1)
                continue;
            if ((float)y < top)
                cov = (float)y + 1 - top;
            if ((float)y + 1 > bot)
                cov = bot - (float)y;
            p = tex->px[imin(tex->h - 1, (int)(v * (float)tex->h)) * tex->w + sx];
            a = (unsigned)((float)(p >> 24) * (cov < 0 ? 0 : cov));
            if (dark)
                p = gfx_mix(p, 0xFF000000u, (unsigned)dark);
            if (a)
                d->px[y * d->w + x] = (p & 0xFFFFFFu) | a << 24;
        }
    }
}

/* Коробка PS1/PS2: лицо — обложка под плёнкой, корешок — логотип, приставка и название. */
static void build_case(const Media *m, int Hs, Image *face, Image *spine)
{
    int ps2 = m->kind == GAME_PS2, fw = (int)((float)Hs * (ps2 ? 0.70f : 0.98f)), sw, b, y, n;
    char title[64];

    sw = (int)((float)fw * (ps2 ? 0.13f : 0.17f));
    *face = image_new(fw, Hs);
    *spine = image_new(sw, Hs);
    if (!face->px || !spine->px)
        return;
    b = imax(2, Hs / 90);
    gfx_fill(face, 0, 0, fw, Hs, ps2 ? 0xFF10121Au : 0xFF24262Eu);
    if (m->front)
        cover_fill(face, m->front, b, b, fw - 2 * b, Hs - 2 * b, 0.5f, 0);
    if (!ps2) {     /* прозрачная крышка коробки компакт-диска: светлая рамка и петля */
        int k;
        for (k = 0; k < b; k++) {
            gfx_fill(face, k, k, fw - 2 * k, 1, 0xC8D2D6E2u);
            gfx_fill(face, k, Hs - 1 - k, fw - 2 * k, 1, 0xC8D2D6E2u);
            gfx_fill(face, k, k, 1, Hs - 2 * k, 0xC8D2D6E2u);
            gfx_fill(face, fw - 1 - k, k, 1, Hs - 2 * k, 0xC8D2D6E2u);
        }
        gfx_fill(face, b, b, Hs / 40, Hs - 2 * b, 0x3CFFFFFFu);
    }
    gloss(face, 0, 0, fw, Hs, 26);

    gfx_fill(spine, 0, 0, sw, Hs, 0xFF08080Au);
    psx_logo(spine, (float)sw * 0.2f, (float)sw * 0.25f, (float)sw * 0.6f);
    y = (int)((float)sw * 1.25f);
    n = vtext(spine, (sw - (int)((float)sw * 0.5f)) / 2, y, TTF_MEDIUM, (int)((float)sw * 0.5f),
              ps2 ? "PlayStation 2" : "PlayStation", 0xFFEBEBF0u, Hs / 2);
    snprintf(title, sizeof(title), "%s", m->title ? m->title : "");
    if (strchr(title, ':'))
        *strchr(title, ':') = '\0';         /* «Castlevania: Symphony…» — на корешке до двоеточия */
    if (strstr(title, " - "))
        *strstr(title, " - ") = '\0';
    {   /* не влезает — сначала мельче (до 60 %), потом укорачиваем */
        int px = (int)((float)sw * 0.5f), room = Hs - (y + n + sw * 3 / 5) - sw / 2;
        while (px > sw * 3 / 10 && ttf_width(TTF_SEMIBOLD, px, title) > room)
            px--;
        while (title[0] && !vtext(spine, (sw - px) / 2, y + n + sw * 3 / 5, TTF_SEMIBOLD, px, title,
                                  m->fg | 0xFF000000u, room))
            title[strlen(title) - 1] = '\0';
    }
}

/* Картридж NES: фото корпуса, наклейка крупнее родного углубления — бумажная, с тенью. */
static void build_cart(const Media *m, int Hs, Image *face, Image *spine)
{
    static Image cart;
    Image sc, strip;
    int fw, sw, i;
    float k, lx, ly, lw, lh, bw;

    if (!cart.px)
        cart = asset(png_nes_cart, sizeof(png_nes_cart));
    if (!cart.px)
        return;
    fw = cart.w * Hs / cart.h;
    *face = image_resize(&cart, fw, Hs);
    if (!face->px)
        return;
    k = (float)Hs / 1351.0f;                /* размеры наклейки — в точках исходного фото */
    lx = (float)fw * NES_LABEL[0];
    ly = (float)Hs * NES_LABEL[1];
    lw = (float)fw * (NES_LABEL[2] - NES_LABEL[0]);
    lh = (float)Hs * (NES_LABEL[3] - NES_LABEL[1]);
    /* тень от наклейки на пластик: размытый прямоугольник, только по корпусу */
    sc = image_new(fw, Hs);
    if (sc.px) {
        int n;
        memset(sc.px, 0, (size_t)fw * (size_t)Hs * 4);
        gfx_rrect_aa(&sc, lx + 6 * k, ly + 10 * k, lw, lh + 2 * k, 18 * k, 0x96000000u);
        image_blur(&sc, imax(1, (int)(10 * k)));
        for (n = 0; n < fw * Hs; n++)
            if (face->px[n] >> 24)
                gfx_over(&face->px[n], 0x28282Cu, (sc.px[n] >> 24) * (face->px[n] >> 24) / 255);
        image_free(&sc);
    }
    gfx_rrect_aa(face, lx, ly, lw, lh, 18 * k, 0xFFECEAE4u);     /* бумага */
    bw = 14 * k;
    if (m->front)
        cover_fill(face, m->front, (int)(lx + bw), (int)(ly + bw), (int)(lw - 2 * bw), (int)(lh - 2 * bw), 0.0f, 8 * k);
    gloss(face, (int)lx, (int)ly, (int)lw, (int)lh, 26);
    /* торец: та же пластмасса (левый край фото), сверху рифление */
    sw = (int)((float)fw * 0.13f);
    strip = image_crop(face, (int)(4 * k), 0, imax(1, (int)(56 * k)), Hs);
    *spine = image_resize(&strip, sw, Hs);
    image_free(&strip);
    for (i = Hs / 33; spine->px && i < Hs * 45 / 100; i += imax(4, Hs / 50))
        gfx_fill(spine, sw / 8, i, sw * 3 / 4, imax(2, Hs / 220), 0xFF5C5C60u);
}

/* microSD: фото корпуса, на месте наклейки — обложка. Корешка нет — у карты вырез сбоку. */
static void build_sd(const Media *m, int Hs, Image *face)
{
    static Image card;
    Image art = { NULL, 0, 0 };
    const Image *front = m->front;
    int fw;
    float lx, ly, lw, lh, k;

    if (!card.px)
        card = asset(png_microsd, sizeof(png_microsd));
    if (!card.px)
        return;
    fw = card.w * Hs / card.h;
    *face = image_resize(&card, fw, Hs);
    if (!face->px)
        return;
    k = (float)Hs / 1379.0f;
    lx = (float)fw * SD_LABEL[0];
    ly = (float)Hs * SD_LABEL[1];
    lw = (float)fw * (SD_LABEL[2] - SD_LABEL[0]);
    lh = (float)Hs * (SD_LABEL[3] - SD_LABEL[1]);
    if (!front && m->icon && m->icon->px) {
        /* своей обложки нет: наклейка в двух цветах игры, по центру — иконка целым множителем */
        int s, iw, ih;
        art = image_new((int)lw, (int)lh);
        if (art.px) {
            gfx_vgradient(&art, 0, 0, art.w, art.h, shade(m->bg, 30), shade(m->bg, -90));
            s = imax(1, imin(art.w * 3 / 5 / m->icon->w, art.h * 3 / 5 / m->icon->h));
            iw = m->icon->w * s;
            ih = m->icon->h * s;
            gfx_blit_scaled(&art, m->icon, (art.w - iw) / 2, (art.h - ih) / 2, iw, ih, 255);
            front = &art;
        }
    }
    if (front)
        cover_fill(face, front, (int)lx, (int)ly, (int)lw, (int)lh, 0.5f, 16 * k);
    gloss(face, (int)lx, (int)ly, (int)lw, (int)lh, 24);
    image_free(&art);
}

Image media_render(const Media *m, int h)
{
    int Hs = h * SS, W, sw, fw, x, y;
    Image face = { NULL, 0, 0 }, spine = { NULL, 0, 0 }, big, out = { NULL, 0, 0 };
    float ff = (float)Hs * 0.03f, fs = (float)Hs * 0.022f;
    unsigned extra = m->kind == GAME_NES ? 215 : 255;

    if (m->kind == GAME_PSX || m->kind == GAME_PS2)
        build_case(m, Hs, &face, &spine);
    else if (m->kind == GAME_NES)
        build_cart(m, Hs, &face, &spine);
    else
        build_sd(m, Hs, &face);
    if (!face.px) {
        image_free(&spine);
        return out;
    }
    fw = face.w;
    sw = spine.px ? spine.w : 0;
    W = fw + sw;
    big = image_new(W, Hs);
    if (big.px) {
        memset(big.px, 0, (size_t)W * (size_t)Hs * 4);
        if (sw)
            warp(&big, &spine, 0, sw, fs, (float)Hs - fs, 0, (float)Hs, 60);
        warp(&big, &face, sw, W, 0, (float)Hs, ff, (float)Hs - ff, 0);
        /* общий свет на все носители: сверху светлее, книзу темнее; серая пластмасса — чуть тише */
        for (y = 0; y < Hs; y++) {
            unsigned l = (255u - (unsigned)y * 70u / (unsigned)Hs) * extra / 255u;
            for (x = 0; x < W; x++) {
                uint32_t *p = &big.px[y * W + x];
                if (*p >> 24)
                    *p = (*p & 0xFF000000u) | (gfx_mix(0xFF000000u, *p, l) & 0xFFFFFFu);
            }
        }
        out = image_resize(&big, W / SS, h);
    }
    image_free(&big);
    image_free(&face);
    image_free(&spine);
    return out;
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
