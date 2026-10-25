/* Полка в разрешении экрана. Все размеры — от «макета» 1920×1080 (по короткой стороне 1080),
 * множитель u. Фон строится один раз на выбранную игру (маленький арт → размытие → растянуть
 * и притушить к левому и нижнему краю) и плавно сменяется. */
#include "shelf.h"
#include "media.h"
#include "ttf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BG_FADE_MS  260
#define BG_SETTLE   120     /* листают быстро — фон не перестраиваем на каждой игре */

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
#define U(v) ((int)((float)(v) * s->u + 0.5f))

void shelf_layout(Shelf *s, int w, int h)
{
    s->W = w;
    s->H = h;
    s->portrait = h > w;
    s->u = (float)(s->portrait ? w : h) / 1080.0f;
    if (s->portrait) {
        s->hi = imin(U(640), h * 36 / 100);
        s->lo = s->hi * 470 / 640;
    } else {
        s->hi = imin(U(400), h * 37 / 100);
        s->lo = s->hi * 230 / 400;
    }
    image_free(&s->bg);
    image_free(&s->bg_old);
    s->bg_game = -1;
}

void shelf_free(Shelf *s)
{
    image_free(&s->bg);
    image_free(&s->bg_old);
    s->bg_game = -1;
}

/* ---------- фон ---------- */

static Image build_bg(const Shelf *s, const Image *art)
{
    int sw = imax(8, s->W / 12), sh = imax(8, s->H / 12), x, y, cw, ch;
    float aspect = (float)s->W / (float)s->H;
    Image crop, small, out = image_new(s->W, s->H);

    if (!out.px)
        return out;
    /* кусок арта в пропорциях экрана, ближе к верху — там обычно логотип и герой */
    cw = art->w;
    ch = (int)((float)cw / aspect);
    if (ch > art->h) {
        ch = art->h;
        cw = (int)((float)ch * aspect);
    }
    crop = image_crop(art, (art->w - cw) / 2, (art->h - ch) * 3 / 10, imax(1, cw), imax(1, ch));
    small = image_resize(&crop, sw, sh);
    image_free(&crop);
    if (!small.px) {
        memset(out.px, 0, (size_t)s->W * (size_t)s->H * 4);
        return out;
    }
    image_blur(&small, 1);
    /* растягиваем билинейно и гасим: слева (там текст) почти чёрное, книзу темнее */
    for (y = 0; y < s->H; y++) {
        float fy = ((float)y + 0.5f) * (float)sh / (float)s->H - 0.5f, ty;
        int y0 = (int)floorf(fy), y1;
        float vm = 1.0f - 0.9f * (float)y / (float)s->H;
        ty = fy - (float)y0;
        y1 = imin(sh - 1, y0 + 1);
        y0 = imax(0, y0);
        for (x = 0; x < s->W; x++) {
            float fx = ((float)x + 0.5f) * (float)sw / (float)s->W - 0.5f, tx, hm, k;
            int x0 = (int)floorf(fx), x1, c;
            uint32_t p00, p01, p10, p11, o = 0xFF000000u;
            tx = fx - (float)x0;
            x1 = imin(sw - 1, x0 + 1);
            x0 = imax(0, x0);
            hm = s->portrait ? 0.35f + 0.65f * clampf((float)x / (float)s->W, 0, 1)
                             : powf(clampf(((float)x / (float)s->W - 0.15f) / 0.6f, 0, 1), 1.5f);
            k = 0.55f * hm * vm;
            p00 = small.px[y0 * sw + x0];
            p01 = small.px[y0 * sw + x1];
            p10 = small.px[y1 * sw + x0];
            p11 = small.px[y1 * sw + x1];
            for (c = 0; c < 24; c += 8) {
                float v = ((float)((p00 >> c) & 255) * (1 - tx) + (float)((p01 >> c) & 255) * tx) * (1 - ty) +
                          ((float)((p10 >> c) & 255) * (1 - tx) + (float)((p11 >> c) & 255) * tx) * ty;
                o |= (uint32_t)(v * k) << c;
            }
            out.px[y * s->W + x] = o;
        }
    }
    image_free(&small);
    return out;
}

static void draw_bg(Shelf *s, Image *scr, Library *lib, int sel, uint32_t now)
{
    Game *g = lib->count ? &lib->games[sel] : NULL;

    if (sel != s->bg_want) {
        s->bg_want = sel;
        s->sel_t = now;
    }
    if (g && sel != s->bg_game && g->cover_ready && now - s->sel_t >= BG_SETTLE) {
        image_free(&s->bg_old);
        s->bg_old = s->bg;
        s->bg.px = NULL;
        s->bg = g->art.px ? build_bg(s, &g->art) : image_new(s->W, s->H);
        if (s->bg.px && !g->art.px)
            memset(s->bg.px, 0, (size_t)s->W * (size_t)s->H * 4);
        s->bg_game = sel;
        s->bg_t = now;
    }
    if (s->bg.px && s->bg.w == scr->w && s->bg.h == scr->h)
        memcpy(scr->px, s->bg.px, (size_t)scr->w * (size_t)scr->h * 4);
    else
        gfx_fill(scr, 0, 0, scr->w, scr->h, 0xFF000000u);
    if (s->bg_old.px && s->bg_old.w == scr->w && s->bg_old.h == scr->h && now - s->bg_t < BG_FADE_MS) {
        unsigned a = 255 - (unsigned)((now - s->bg_t) * 255 / BG_FADE_MS);
        size_t i, n = (size_t)scr->w * (size_t)scr->h;
        for (i = 0; i < n; i++)
            scr->px[i] = gfx_mix(scr->px[i], s->bg_old.px[i], a);
    } else if (s->bg_old.px && now - s->bg_t >= BG_FADE_MS) {
        image_free(&s->bg_old);
    }
}

/* ---------- текст и мелочи ---------- */

/* Строка, укороченная многоточием под ширину. */
static void fit(int font, int px, const char *t, int max_w, char *out, size_t cap)
{
    size_t n;

    snprintf(out, cap, "%s", t);
    if (ttf_width(font, px, out) <= max_w)
        return;
    for (n = strlen(out); n > 0; n--) {
        if (((unsigned char)out[n] & 0xC0) == 0x80)
            continue;               /* не режем букву UTF-8 пополам */
        out[n] = '\0';
        if (n + 4 < cap) {
            char tmp[300];
            snprintf(tmp, sizeof(tmp), "%s…", out);
            if (ttf_width(font, px, tmp) <= max_w) {
                snprintf(out, cap, "%s", tmp);
                return;
            }
        }
    }
}

static uint32_t accent(const Game *g)
{
    uint32_t c = g->tone_fg;
    float r = (float)((c >> 16) & 255) / 255, gg = (float)((c >> 8) & 255) / 255, b = (float)(c & 255) / 255;
    float mx = fmaxf(r, fmaxf(gg, b)), mn = fminf(r, fminf(gg, b)), d = mx - mn, h = 0, sat, k[3];
    int i;

    if (d > 0)
        h = mx == r ? fmodf((gg - b) / d + 6, 6) : mx == gg ? (b - r) / d + 2 : (r - gg) / d + 4;
    sat = clampf(mx > 0 ? d / mx : 0, 0.55f, 0.85f);
    for (i = 0; i < 3; i++) {   /* hsv → rgb при v = 0.98 */
        float kk = fmodf((float)(5 - i * 2) + h, 6);
        k[i] = 0.98f - 0.98f * sat * fmaxf(0, fminf(fminf(kk, 4 - kk), 1));
    }
    return 0xFF000000u | (uint32_t)(k[0] * 255) << 16 | (uint32_t)(k[1] * 255) << 8 | (uint32_t)(k[2] * 255);
}

static const char *system_name(int kind)
{
    return kind == GAME_PS2 ? "PlayStation 2" : kind == GAME_PSX ? "PlayStation" : kind == GAME_NES ? "NES" : "J2ME";
}

static void played_text(const Game *g, char *out, size_t cap)
{
    long m = g->play_secs / 60;

    if (g->play_secs <= 0)
        snprintf(out, cap, "—");
    else if (m < 60)
        snprintf(out, cap, "%ld мин", m < 1 ? 1 : m);
    else
        snprintf(out, cap, "%ld ч %02ld мин", m / 60, m % 60);
}

static void last_text(const Game *g, char *out, size_t cap)
{
    time_t now = time(NULL), then = (time_t)g->last_played;
    struct tm a, b;
    long days;

    if (!g->last_played) {
        snprintf(out, cap, "не запускали");
        return;
    }
    a = *localtime(&now);
    b = *localtime(&then);
    a.tm_hour = b.tm_hour = 12;
    a.tm_min = b.tm_min = a.tm_sec = b.tm_sec = 0;
    days = (long)((mktime(&a) - mktime(&b)) / 86400);
    if (days <= 0)
        snprintf(out, cap, "сегодня");
    else if (days == 1)
        snprintf(out, cap, "вчера");
    else if (days < 7)
        snprintf(out, cap, "%ld %s назад", days, days < 5 ? "дня" : "дней");
    else if (days < 14)
        snprintf(out, cap, "неделю назад");
    else if (days < 31)
        snprintf(out, cap, "%ld недели назад", days / 7);
    else if (days < 62)
        snprintf(out, cap, "месяц назад");
    else if (days < 365)
        snprintf(out, cap, "%ld мес. назад", days / 30);
    else
        snprintf(out, cap, "больше года назад");
}

/* Таблица сведений: подписи серым моноширинным, значения белым. Возвращает низ таблицы. */
static int spec(Shelf *s, Image *scr, const Game *g, int x, int y, int px, int gap, int max_w)
{
    const char *lab[7], *val[7];
    char played[48], last[48], v[200];
    int n = 0, i, lw = ttf_width(TTF_MONO, px, "ПОСЛЕДНИЙ ЗАПУСК") + U(80);

    if (g->year[0]) { lab[n] = "ГОД"; val[n++] = g->year; }
    if (g->publisher[0]) { lab[n] = g->kind == GAME_J2ME ? "РАЗРАБОТЧИК" : "ИЗДАТЕЛЬ"; val[n++] = g->publisher; }
    if (g->genre[0]) { lab[n] = "ЖАНР"; val[n++] = g->genre; }
    if (g->region[0]) { lab[n] = "РЕГИОН"; val[n++] = g->region; }
    if (g->kind == GAME_J2ME && g->version[0]) { lab[n] = "ВЕРСИЯ"; val[n++] = g->version; }
    played_text(g, played, sizeof(played));
    last_text(g, last, sizeof(last));
    lab[n] = "В ИГРЕ"; val[n++] = played;
    lab[n] = "ПОСЛЕДНИЙ ЗАПУСК"; val[n++] = last;
    for (i = 0; i < n; i++) {
        ttf_draw(scr, TTF_MONO, px, x, y + i * gap, lab[i], 0xFF78787Fu);
        fit(TTF_MONO, px, val[i], max_w - lw, v, sizeof(v));
        ttf_draw(scr, TTF_MONO, px, x + lw, y + i * gap, v, 0xFFECECF0u);
    }
    return y + n * gap;
}

static void set_rect(int *r, int x, int y, int w, int h)
{
    r[0] = x; r[1] = y; r[2] = w; r[3] = h;
}

/* Кнопка-«пилюля»: белая залитая (filled) или с тонкой рамкой; play — с треугольником. */
static void pill(Shelf *s, Image *scr, int x, int y, int w, int h, const char *t, int filled, int play, int px)
{
    int tw = ttf_width(TTF_SEMIBOLD, px, t), tri = play ? h * 2 / 5 : 0, gap = play ? h / 4 : 0;
    int tx = x + (w - tw - tri - gap) / 2, ty = y + (h - ttf_line(TTF_SEMIBOLD, px)) / 2 + U(2);

    if (filled) {
        gfx_rrect_aa(scr, (float)x, (float)y, (float)w, (float)h, (float)h / 2, 0xFFFFFFFFu);
    } else {
        gfx_rrect_aa(scr, (float)x, (float)y, (float)w, (float)h, (float)h / 2, 0xFF78787Fu);
        gfx_rrect_aa(scr, (float)x + 2, (float)y + 2, (float)w - 4, (float)h - 4, (float)h / 2 - 2, 0xD8101014u);
    }
    if (play) {
        float cy = (float)y + (float)h / 2, tx0 = (float)tx;
        float v[6] = { tx0, cy - (float)tri / 2, tx0 + (float)tri * 0.87f, cy, tx0, cy + (float)tri / 2 };
        gfx_poly(scr, v, 3, filled ? 0xFF0A0A0Cu : 0xFFDCDCE2u);
    }
    ttf_draw(scr, filled ? TTF_SEMIBOLD : TTF_MEDIUM, px, tx + tri + gap, ty, t, filled ? 0xFF0A0A0Cu : 0xFFDCDCE2u);
}

/* Плашки L и R (соседние вкладки), как у Analogue. */
static void chips(Shelf *s, Image *scr, int x, int y)
{
    int w = U(38), h = U(40), i;

    for (i = 0; i < 2; i++) {
        int cx = x + i * (w + U(6));
        gfx_rrect_aa(scr, (float)cx, (float)y, (float)w, (float)h, (float)U(6), 0xFF46464Cu);
        ttf_draw(scr, TTF_MONO, U(26), cx + (w - ttf_width(TTF_MONO, U(26), i ? "R" : "L")) / 2, y + U(4),
                 i ? "R" : "L", 0xFF141418u);
        set_rect(i ? s->tab_r : s->tab_l, cx, y, w, h);
    }
}

static void hints(Shelf *s, Image *scr, int y, int mode)
{
    static const char *const pad[][2] = { { "A", "Играть" }, { "B", "Назад" }, { "X", "Обложка" }, { "Y", "Сброс" } };
    static const char *const keys[][2] = { { "Enter", "Играть" }, { "Esc", "Назад" }, { "0", "Обложка" },
                                           { "Del", "Сброс" } };
    int x = U(80), i, px = U(24);

    gfx_fill(scr, 0, y - U(20), s->W, 1, 0xFF28282Eu);
    for (i = 0; i < 4; i++) {
        const char *k = mode == SHELF_HINTS_PAD ? pad[i][0] : keys[i][0], *t = mode == SHELF_HINTS_PAD ? pad[i][1] : keys[i][1];
        int kh = U(36), kw = mode == SHELF_HINTS_PAD ? kh : imax(kh, ttf_width(TTF_SEMIBOLD, U(19), k) + U(18));
        gfx_rrect_aa(scr, (float)x, (float)y, (float)kw, (float)kh, mode == SHELF_HINTS_PAD ? (float)kh / 2 : (float)U(7),
                     0xFFE6E6ECu);
        gfx_rrect_aa(scr, (float)x + 2, (float)y + 2, (float)kw - 4, (float)kh - 4,
                     mode == SHELF_HINTS_PAD ? (float)kh / 2 - 2 : (float)U(7) - 2, 0xFF101014u);
        ttf_draw(scr, TTF_SEMIBOLD, U(19), x + (kw - ttf_width(TTF_SEMIBOLD, U(19), k)) / 2, y + U(7), k, 0xFFE6E6ECu);
        x += kw + U(12);
        x += ttf_draw(scr, TTF_REGULAR, px, x, y + U(3), t, 0xFFE6E6ECu) + U(44);
    }
}

/* ---------- лента носителей ---------- */

/* Слот игры на расстоянии d от выбранной: центр по x, высота носителя, притемнение. */
static void slot(const Shelf *s, float d, int *cx, int *h, int *dim)
{
    float ad = fabsf(d), near = clampf(ad, 0, 1), off1, step, off;

    if (s->portrait) {
        off1 = (float)s->W * 0.62f;     /* соседи выглядывают из-за краёв экрана */
        step = off1;
    } else {
        off1 = (float)s->hi * 0.85f;
        step = (float)s->hi * 0.71f;
    }
    off = near * off1 + (ad > 1 ? (ad - 1) * step : 0);
    *cx = s->W / 2 + (int)(d < 0 ? -off : off);
    *h = (int)((float)s->hi + (float)(s->lo - s->hi) * near);
    *dim = (int)(near * 150);
}

static int media_w(const Game *g, int h)
{
    if (g->cover_ready && g->cover.px)
        return g->cover.w * h / imax(1, g->cover.h);
    return (int)((float)h * media_aspect(g->kind));
}

/* Отражение на «глянцевом полу»: перевёрнутый носитель, гаснет вниз. */
static void reflection(Image *scr, const Image *m, int x, int y, int w, int h, int a0)
{
    int rh = h * 35 / 100, i, j;

    for (j = 0; j < rh; j++) {
        int dy = y + j, sy = m->h - 1 - j * m->h / h;
        unsigned a = (unsigned)(a0 * (rh - j) / rh);
        if (dy < 0 || dy >= scr->h || sy < 0)
            continue;
        for (i = 0; i < w; i++) {
            int dx = x + i;
            uint32_t p;
            if (dx < 0 || dx >= scr->w)
                continue;
            p = m->px[sy * m->w + i * m->w / w];
            if (p >> 24)
                gfx_over(&scr->px[dy * scr->w + dx], p & 0xFFFFFFu, (p >> 24) * a / 255);
        }
    }
}

static void carousel(Shelf *s, Image *scr, Library *lib, float pos, int base, int range)
{
    int order, i;

    s->base = base;
    for (order = range; order >= 0; order--)
        for (i = 0; i < lib->count; i++) {
            Game *g = &lib->games[i];
            float d = (float)i - pos;
            int cx, h, dim, w, x;
            if ((int)(fabsf(d) + 0.5f) != order)
                continue;
            slot(s, d, &cx, &h, &dim);
            w = media_w(g, h);
            x = cx - w / 2;
            if (x > s->W || x + w < 0)
                continue;
            if (g->cover_ready && g->cover.px) {
                gfx_blit_smooth(scr, &g->cover, x, base - h, w, h, 255, dim);
                reflection(scr, &g->cover, x, base + 2, w, h, dim ? 30 : 60);
            } else {
                gfx_rrect_aa(scr, (float)x, (float)(base - h), (float)w, (float)h, (float)U(10), 0x30FFFFFFu);
            }
        }
}

/* ---------- кадр ---------- */

static void top_bar(Shelf *s, Image *scr, const Game *g, int sel, int count, uint32_t acc)
{
    char cnt[32];
    int y = U(58);

    gfx_rrect_aa(scr, (float)U(60), (float)(y + U(12)), (float)U(11), (float)U(11), (float)U(5.5), acc);
    ttf_draw(scr, TTF_MEDIUM, U(30), U(82), y, system_name(g->kind), 0xFFECECF0u);
    snprintf(cnt, sizeof(cnt), "%d / %d", sel + 1, count);
    ttf_draw(scr, TTF_MONO, U(26), s->W - U(180) - ttf_width(TTF_MONO, U(26), cnt), y + U(2), cnt, 0xFF8C8C94u);
    chips(s, scr, s->W - U(130), y - U(2));
}

void shelf_draw(Shelf *s, Image *scr, Library *lib, int sel, float pos, uint32_t now, int hint_mode)
{
    Game *g;
    uint32_t acc;
    char t[300];
    int px, y;

    draw_bg(s, scr, lib, sel, now);
    if (!lib->count)
        return;
    g = &lib->games[sel];
    acc = accent(g);
    top_bar(s, scr, g, sel, lib->count, acc);

    if (!s->portrait) {
        int maxw = s->W - U(160);
        /* название: крупно, если длинное — помельче, потом с многоточием */
        for (px = U(64); px > U(40) && ttf_width(TTF_SEMIBOLD, px, g->name) > maxw; px -= U(4))
            ;
        fit(TTF_SEMIBOLD, px, g->name, maxw, t, sizeof(t));
        ttf_draw(scr, TTF_SEMIBOLD, px, U(80), U(150), t, 0xFFFFFFFFu);
        gfx_fill(scr, U(82), U(150) + ttf_line(TTF_SEMIBOLD, px) + U(6), U(60), imax(2, U(4)), acc);
        y = spec(s, scr, g, U(84), U(150) + ttf_line(TTF_SEMIBOLD, px) + U(32), U(26), U(44), s->W / 2 - U(40));
        y += U(20);
        pill(s, scr, U(80), y, U(220), U(60), "Играть", 1, 1, U(30));
        pill(s, scr, U(320), y, U(180), U(60), "Обложка", 0, 0, U(30));
        set_rect(s->play, U(80), y, U(220), U(60));
        set_rect(s->cover, U(320), y, U(180), U(60));
        carousel(s, scr, lib, pos, s->H - U(150), 4);
        if (hint_mode != SHELF_HINTS_NONE)
            hints(s, scr, s->H - U(56), hint_mode);
    } else {
        int btn_h = U(90), btn_y = s->H - U(110) - btn_h, rows = 2, top, dots_y, i, base;
        rows += !!g->year[0] + !!g->publisher[0] + !!g->genre[0] + !!g->region[0] +
                (g->kind == GAME_J2ME && g->version[0]);
        top = btn_y - U(40) - rows * U(50);
        fit(TTF_SEMIBOLD, U(56), g->name, s->W - U(160), t, sizeof(t));
        ttf_draw(scr, TTF_SEMIBOLD, U(56), U(80), top - U(90), t, 0xFFFFFFFFu);
        spec(s, scr, g, U(84), top, U(30), U(50), s->W - U(160));
        pill(s, scr, U(80), btn_y, s->W - U(160) - btn_h - U(20), btn_h, "Играть", 1, 1, U(40));
        set_rect(s->play, U(80), btn_y, s->W - U(160) - btn_h - U(20), btn_h);
        /* квадратная кнопка «обложка»: рамка и картинка-горы */
        {
            int bx = s->W - U(80) - btn_h;
            float v[6] = { (float)(bx + btn_h * 28 / 100), (float)(btn_y + btn_h * 66 / 100),
                           (float)(bx + btn_h * 44 / 100), (float)(btn_y + btn_h * 44 / 100),
                           (float)(bx + btn_h * 60 / 100), (float)(btn_y + btn_h * 66 / 100) };
            gfx_rrect_aa(scr, (float)bx, (float)btn_y, (float)btn_h, (float)btn_h, (float)btn_h / 2, 0xFF78787Fu);
            gfx_rrect_aa(scr, (float)bx + 2, (float)btn_y + 2, (float)btn_h - 4, (float)btn_h - 4,
                         (float)btn_h / 2 - 2, 0xD8101014u);
            gfx_rrect_aa(scr, (float)(bx + btn_h / 4), (float)(btn_y + btn_h * 3 / 10), (float)(btn_h / 2),
                         (float)(btn_h * 2 / 5), (float)U(4), 0xFFDCDCE2u);
            gfx_rrect_aa(scr, (float)(bx + btn_h / 4 + U(3)), (float)(btn_y + btn_h * 3 / 10 + U(3)),
                         (float)(btn_h / 2 - U(6)), (float)(btn_h * 2 / 5 - U(6)), (float)U(2), 0xFF101014u);
            gfx_poly(scr, v, 3, 0xFFDCDCE2u);
            set_rect(s->cover, bx, btn_y, btn_h, btn_h);
        }
        dots_y = top - U(150);
        for (i = 0; i < lib->count && lib->count <= 40; i++) {
            int dx = s->W / 2 - lib->count * U(14) + i * U(28);
            gfx_rrect_aa(scr, (float)dx, (float)dots_y, (float)U(10), (float)U(10), (float)U(5),
                         i == sel ? 0xFFFFFFFFu : 0xFF46464Cu);
        }
        base = dots_y - U(40) - s->hi * 35 / 100;   /* под носителем — место для отражения */
        carousel(s, scr, lib, pos, base, 1);
    }
}

int shelf_hit(const Shelf *s, const Library *lib, float pos, int x, int y, int *game)
{
    int i, best = -1;
    float bestd = 1e9f;

#define IN(r) (x >= (r)[0] && x < (r)[0] + (r)[2] && y >= (r)[1] && y < (r)[1] + (r)[3])
    if (IN(s->play))
        return SHELF_HIT_PLAY;
    if (IN(s->cover))
        return SHELF_HIT_COVER;
    if (IN(s->tab_l))
        return SHELF_HIT_TAB_L;
    if (IN(s->tab_r))
        return SHELF_HIT_TAB_R;
#undef IN
    for (i = 0; i < lib->count; i++) {
        int cx, h, dim, w;
        float d = (float)i - pos;
        slot(s, d, &cx, &h, &dim);
        w = media_w(&lib->games[i], h);
        if (x >= cx - w / 2 && x < cx + w / 2 && y >= s->base - h && y < s->base && fabsf(d) < bestd) {
            best = i;
            bestd = fabsf(d);
        }
    }
    if (best >= 0) {
        *game = best;
        return SHELF_HIT_GAME;
    }
    return SHELF_HIT_NONE;
}
