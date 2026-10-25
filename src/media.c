/* Объёмные носители для полки. Рисуем вдвое крупнее и уменьшаем с усреднением — так края
 * в перспективе и мелкий текст на корешке получаются сглаженными. Сначала собираются плоские
 * «развёртки» лицевой стороны и корешка, потом каждая натягивается на свою трапецию. */
#include "media.h"
#include "library.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SS 2    /* во сколько раз крупнее рисуем перед уменьшением */

typedef struct {
    float face_aspect;  /* ширина / высота лицевой стороны */
    float spine;        /* доля корешка в ширине всего носителя */
    float far_face;     /* насколько короче дальний край лицевой стороны (доля высоты, с каждой стороны) */
    float far_spine;    /* то же для внешнего края корешка */
} Shape;

static Shape shape(int kind)
{
    static const Shape s[] = {
        { 0.75f, 0.07f, 0.030f, 0.020f },   /* J2ME: карта памяти 24×32 мм */
        { 0.90f, 0.15f, 0.035f, 0.025f },   /* NES: картридж 120×133 мм */
        { 1.00f, 0.12f, 0.035f, 0.025f },   /* PS1: коробка компакт-диска, обложка квадратная */
        { 0.70f, 0.13f, 0.035f, 0.025f },   /* PS2: коробка DVD */
    };
    return s[kind >= 0 && kind <= GAME_PS2 ? kind : 0];
}

float media_front_aspect(int kind)
{
    return kind == GAME_NES ? 1.25f : shape(kind).face_aspect;   /* у NES — наклейка картриджа */
}

float media_aspect(int kind)
{
    Shape s = shape(kind);
    return s.face_aspect / (1.0f - s.spine);
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static uint32_t shade(uint32_t c, int k)    /* k: -255 (чёрный) .. 255 (белый) */
{
    return k < 0 ? gfx_mix(c, 0xFF000000u, (unsigned)-k) : gfx_mix(c, 0xFFFFFFFFu, (unsigned)k);
}

/* Полупрозрачная заливка только по уже нарисованному (не трогает прозрачный фон). */
static void tint(Image *d, int x, int y, int w, int h, uint32_t c)
{
    int i, j;
    unsigned a = c >> 24;

    for (j = imax(0, y); j < imin(d->h, y + h); j++)
        for (i = imax(0, x); i < imin(d->w, x + w); i++) {
            uint32_t *p = &d->px[j * d->w + i];
            if (*p >> 24)
                *p = (*p & 0xFF000000u) | (gfx_mix(*p, c, a) & 0xFFFFFFu);
        }
}

/* Прямоугольник со скруглёнными (радиус r) углами, непрозрачный. */
static void rbox(Image *d, int x, int y, int w, int h, int r, uint32_t c)
{
    int j;

    for (j = 0; j < h; j++) {
        int dy = j < r ? r - j : j >= h - r ? j - (h - r - 1) : 0, in = 0;
        if (dy)
            in = r - (int)sqrtf((float)(r * r - (dy - 0.5f) * (dy - 0.5f)));
        gfx_fill(d, x + in, y + j, w - 2 * in, 1, c);
    }
}

/* Обложка, кадрированная по центру под w × h (top — доля сверху, откуда брать: 0..1). */
static void cover_fill(Image *d, const Image *src, int x, int y, int w, int h, float top)
{
    int cw = src->w, ch = src->w * h / imax(1, w), x0, y0;
    Image crop, sc;
    int j;

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
    for (j = 0; sc.px && j < h; j++) {
        int i;
        for (i = 0; i < w; i++) {
            int px = x + i, py = y + j;
            if (px >= 0 && py >= 0 && px < d->w && py < d->h)
                d->px[py * d->w + px] = sc.px[j * w + i] | 0xFF000000u;
        }
    }
    image_free(&sc);
}

/* Текст сверху вниз (повёрнут по часовой), с верхней точки (x, y); возвращает длину. */
static int vtext(Image *d, int x, int y, const char *t, uint32_t c, int max_len)
{
    int tw = gfx_text_width(&font_small, t, 1), i, j;
    Image tmp;

    if (tw > max_len)
        return 0;
    tmp = image_new(tw, font_small.h);
    if (!tmp.px)
        return 0;
    memset(tmp.px, 0, (size_t)tw * font_small.h * 4);
    gfx_text(&tmp, &font_small, 0, 0, t, c, 1);
    for (j = 0; j < tmp.h; j++)
        for (i = 0; i < tw; i++) {
            uint32_t p = tmp.px[j * tw + i];
            int px = x + tmp.h - 1 - j, py = y + i;
            if ((p >> 24) && px >= 0 && py >= 0 && px < d->w && py < d->h)
                d->px[py * d->w + px] = p;
        }
    image_free(&tmp);
    return tw;
}

/* Старый логотип PlayStation: красная «P» стоит, «S» из трёх цветов лежит за ней. */
static void psx_logo(Image *d, int x, int y, int k)
{
    static const char *const rows[] = {
        "....rrrr....",
        "....r..rr...",
        "....r..rr...",
        "....rrrr....",
        "..yyryyyyy..",
        ".yy.r.......",
        "..gggggggg..",
        "........bb..",
        "..bbbbbbb...",
    };
    int i, j;

    for (j = 0; j < 9; j++)
        for (i = 0; rows[j][i]; i++) {
            char c = rows[j][i];
            uint32_t col = c == 'r' ? 0xFFE8303Au : c == 'y' ? 0xFFF2C21Bu : c == 'g' ? 0xFF2FB36Bu
                         : c == 'b' ? 0xFF3A7FE0u : 0;
            if (col)
                gfx_fill(d, x + i * k, y + j * k, k, k, col);
        }
}

/* ---------- развёртки ---------- */

/* Лицевая сторона и корешок коробки PS1/PS2. */
static void flat_case(const Media *m, Image *face, Image *spine)
{
    int ps2 = m->kind == GAME_PS2, w = face->w, h = face->h, b = imax(2, w / 60), i;
    uint32_t plastic = ps2 ? 0xFF10131Cu : 0xFF1A1B20u, ink = 0xFFE9EAEEu;

    /* лицо: обложка под прозрачной плёнкой, вокруг — пластик */
    rbox(face, 0, 0, w, h, b * 2, plastic);
    if (m->front)
        cover_fill(face, m->front, b, b, w - 2 * b, h - 2 * b, 0.5f);
    if (!ps2) {
        /* у коробки компакт-диска слева на крышке — петля с рёбрышками */
        int hw = imax(3, w / 26);
        tint(face, 0, 0, hw, h, 0x70E8ECF4u);
        for (i = b * 2; i < h - b * 2; i += imax(3, h / 40))
            tint(face, 1, i, hw - 2, 1, 0x90FFFFFFu);
        tint(face, hw, 0, 1, h, 0x50000000u);
    }
    /* блик пластика: светлая кромка и широкая косая полоса */
    tint(face, 0, 0, w, 1, 0x80FFFFFFu);
    tint(face, 0, 0, 1, h, 0x60FFFFFFu);
    for (i = 0; i < h; i++) {
        int x0 = w * 55 / 100 - i * w / (h * 2) * 1, bw = w / 5;
        tint(face, x0, i, bw, 1, 0x14FFFFFFu);
        tint(face, x0 + bw + w / 20, i, bw / 4, 1, 0x10FFFFFFu);
    }

    /* корешок: тёмный, сверху логотип, ниже надпись приставки и название игры */
    {
        int sw = spine->w, sh = spine->h, k = imax(1, sw / 14), y;
        rbox(spine, 0, 0, sw, sh, imax(1, b), plastic);
        gfx_fill(spine, 1, b, sw - 2, sh - 2 * b, ps2 ? 0xFF050608u : 0xFF0A0A0Cu);
        psx_logo(spine, (sw - 12 * k) / 2, b + sw / 6, k);
        y = b + sw / 6 + 9 * k + sw / 3;
        if (sw >= font_small.h + 2) {
            int l = vtext(spine, (sw - font_small.h) / 2, y, ps2 ? "PlayStation 2" : "PlayStation", ink, sh / 3);
            if (l)
                y += l + sw / 2;
            if (m->title)
                vtext(spine, (sw - font_small.h) / 2, y, m->title, m->fg | 0xFF000000u, sh - y - b * 3);
        } else
            gfx_fill(spine, sw / 2, y, imax(1, sw / 6), sh / 3, ink);
        tint(spine, 0, 0, 1, sh, 0x50FFFFFFu);
    }
}

/* Картридж NES: серый корпус, наклейка с обложкой, рёбра снизу и стрелка. */
static void flat_cart(const Media *m, Image *face, Image *spine)
{
    int w = face->w, h = face->h, r = imax(2, w / 30), i;
    uint32_t body = 0xFF8D8C93u, dark = 0xFF6F6E76u;
    int lx = w * 12 / 100, ly = h * 9 / 100, lw = w - 2 * lx, lh = (int)((float)lw / 1.25f);

    rbox(face, 0, 0, w, h, r, body);
    gfx_fill(face, 0, h * 70 / 100, w, h * 30 / 100 - r, dark);       /* нижняя часть — темнее */
    rbox(face, 0, h - r * 2, w, r * 2, r, dark);
    /* углубление под наклейку */
    gfx_fill(face, lx - 2, ly - 2, lw + 4, lh + 4, shade(body, -40));
    if (m->front)
        cover_fill(face, m->front, lx, ly, lw, lh, 0.15f);
    else
        gfx_fill(face, lx, ly, lw, lh, m->bg);
    tint(face, lx, ly, lw, 1, 0x50FFFFFFu);
    /* рёбра по бокам внизу и треугольная стрелка по центру */
    for (i = h * 74 / 100; i < h - r * 2; i += imax(3, h / 30)) {
        gfx_fill(face, w * 6 / 100, i, w * 22 / 100, imax(1, h / 90), shade(dark, -45));
        gfx_fill(face, w * 72 / 100, i, w * 22 / 100, imax(1, h / 90), shade(dark, -45));
    }
    for (i = 0; i < h / 12; i++)
        gfx_fill(face, w / 2 - (h / 12 - i), h * 78 / 100 + i, 2 * (h / 12 - i), 1, shade(dark, -30));
    tint(face, 0, 0, w, 1, 0x70FFFFFFu);
    tint(face, 0, 0, 1, h, 0x50FFFFFFu);

    /* торец картриджа: тот же пластик с поперечными рёбрами */
    rbox(spine, 0, 0, spine->w, spine->h, r, shade(body, -35));
    for (i = r * 2; i < spine->h - r * 2; i += imax(3, spine->h / 24))
        gfx_fill(spine, spine->w / 5, i, spine->w * 3 / 5, imax(1, spine->h / 100), shade(body, -70));
}

/* Карта памяти для игр J2ME: корпус и наклейка — два цвета игры, срезанный угол,
 * контакты сверху, переключатель защиты сбоку. */
static void flat_card(const Media *m, Image *face, Image *spine)
{
    int w = face->w, h = face->h, r = imax(2, w / 22), cut = w * 22 / 100, i, j;
    uint32_t body = shade(m->bg, -90), label = shade(m->fg, 30);
    int lx = w * 12 / 100, ly = h * 24 / 100, lw = w - 2 * lx, lh = h * 62 / 100;

    rbox(face, 0, 0, w, h, r, body);
    for (j = 0; j < cut; j++)          /* срезанный правый верхний угол */
        for (i = w - cut + j; i < w; i++)
            face->px[j * w + i] = 0;
    for (i = 0; i < 7; i++)            /* контакты */
        rbox(face, w * 8 / 100 + i * (w * 66 / 100) / 7, h * 3 / 100, (w * 66 / 100) / 7 - imax(2, w / 40),
             h * 13 / 100, imax(1, w / 60), shade(label, 10));
    rbox(face, lx, ly, lw, lh, imax(2, w / 30), label);
    if (m->front)
        cover_fill(face, m->front, lx + w / 30, ly + w / 30, lw - w / 15, lh - w / 15, 0.5f);
    else if (m->icon && m->icon->px) {
        /* целый множитель — пиксель-арт иконки остаётся ровным */
        int k = imin((lw - w / 8) / m->icon->w, (lh - w / 8) / m->icon->h), iw, ih;
        k = imax(1, k);
        iw = m->icon->w * k;
        ih = m->icon->h * k;
        gfx_blit_scaled(face, m->icon, lx + (lw - iw) / 2, ly + (lh - ih) / 2, iw, ih, 255);
    }
    /* надпись внизу и защёлка защиты записи слева */
    {
        const char *t = "J2ME";
        int tw = gfx_text_width(&font_small, t, 1);
        if (tw < w / 2 && h - (ly + lh) > font_small.h)
            gfx_text(face, &font_small, (w - tw) / 2, ly + lh + (h - ly - lh - font_small.h) / 2, t,
                     shade(label, -20), 1);
    }
    gfx_fill(face, 0, h * 30 / 100, imax(2, w / 25), h / 10, shade(body, 40));
    tint(face, 0, 0, w, 1, 0x60FFFFFFu);
    tint(face, 0, 0, 1, h, 0x40FFFFFFu);

    rbox(spine, 0, 0, spine->w, spine->h, imax(1, r / 2), shade(body, -50));
}

/* ---------- натягивание на трапецию ---------- */

/* Развёртка tex — на столбцы x0..x1: левый край от t0 до b0, правый от t1 до b1 (с учётом
 * перспективы: дальний край короче, и картинка к нему сжимается). */
static void warp(Image *d, const Image *tex, int x0, int x1, float t0, float b0, float t1, float b1, int dark)
{
    float h0 = b0 - t0, h1 = b1 - t1;
    int x, y;

    for (x = x0; x < x1; x++) {
        float t = ((float)(x - x0) + 0.5f) / (float)(x1 - x0);
        float u = t * h1 / ((1 - t) * h0 + t * h1), top = t0 + (t1 - t0) * t, bot = b0 + (b1 - b0) * t;
        int sx = imin(tex->w - 1, (int)(u * (float)tex->w));
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
                d->px[y * d->w + x] = (p & 0xFFFFFFu) | (a << 24);
        }
    }
}

Image media_render(const Media *m, int w, int h)
{
    Shape s = shape(m->kind);
    int W = w * SS, H = h * SS, sw = (int)((float)W * s.spine), fw = W - sw;
    Image big = image_new(W, H), face = image_new(fw, H), spine = image_new(imax(1, sw), H), out = { NULL, 0, 0 };
    float ff = (float)H * s.far_face, fs = (float)H * s.far_spine;

    if (big.px && face.px && spine.px) {
        memset(big.px, 0, (size_t)W * H * 4);
        memset(face.px, 0, (size_t)fw * H * 4);
        memset(spine.px, 0, (size_t)spine.w * H * 4);
        if (m->kind == GAME_NES)
            flat_cart(m, &face, &spine);
        else if (m->kind == GAME_J2ME)
            flat_card(m, &face, &spine);
        else
            flat_case(m, &face, &spine);
        /* корешок дальним краем влево, лицо — дальним краем вправо; корешок в тени */
        warp(&big, &spine, 0, sw, fs, (float)H - fs, 0, (float)H, 70);
        warp(&big, &face, sw, W, 0, (float)H, ff, (float)H - ff, 0);
        out = image_resize(&big, w, h);
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
