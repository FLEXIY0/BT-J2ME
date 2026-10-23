/* Сенсорное управление: кнопки поверх игры, плавающий стик и редактор раскладки. */
#include "touch.h"
#include "compat.h"
#include "gfx.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ELEMS   40
#define MAX_FINGERS 10
#define KEY_SLOTS   80          /* коды клавиш -8..57 → индекс key + 16 */
#define TEX_CACHE   96

enum { E_KEY, E_STICK, E_PAD, E_MENU };
enum { F_NONE, F_KEY, F_PANEL, F_STICK, F_POINTER, F_EDIT_DRAG, F_EDIT_UI };

/* Подписи, которых нет среди кодов клавиш */
enum { L_PAD = 1000, L_MENU, L_STICK, L_KNOB, L_PLUS, L_MINUS, L_EYE, L_CHECK, L_RESET, L_TRASH,
       L_SIZE, L_WAY4, L_WAY8 };
enum { S_NORMAL, S_PRESSED, S_CHIP, S_CHIP_ON };

typedef struct {
    int   type, key;
    float x, y, s;              /* центр — доля экрана, s — множитель размера */
} Elem;

typedef struct {
    Elem e[MAX_ELEMS];
    int  n, valid;              /* valid — раскладка своя (сохранена), иначе — по умолчанию */
} Layout;

typedef struct {
    int          used, kind, elem, key;
    SDL_FingerID id;
    float        dx, dy;        /* смещение пальца от центра при перетаскивании */
} Finger;

typedef struct {
    int      label, w, h, style;
    SDL_Texture *t;
} Tex;

/* Пункт редактора: действие и прямоугольник */
enum { A_OP_MINUS, A_OP_PLUS, A_WAY, A_RESET, A_DONE, A_SIZE_MINUS, A_SIZE_PLUS, A_DELETE, A_ADD };
typedef struct {
    int      action, arg, label;
    SDL_Rect r;
} Item;

static struct {
    SDL_Renderer *ren;
    TouchEmit     emit;
    SDL_Haptic   *haptic;
    int           game;         /* в игре: показываем кнопки */
    char          id[256];
    Layout        lay[2];       /* 0 — портрет, 1 — альбом */
    float         opacity;
    int           four_way;
    int           W, H, gw, gh;
    SDL_Rect      g;            /* где картинка игры */
    Finger        f[MAX_FINGERS];
    int           keycount[KEY_SLOTS];
    int           pad_open, ptr_down;
    int           stick_on;
    float         sox, soy, skx, sky;
    int           sdir[4];
    int           edit, sel;
    int           dirty;
    Tex           cache[TEX_CACHE];
} T;

/* ---------- раскладка ---------- */

static int land(void) { return T.W > T.H; }
static Layout *cur(void) { return &T.lay[land()]; }
static float unit(void) { return (float)(T.W < T.H ? T.W : T.H) / 100.0f; }

static void elem_size(const Elem *e, float *w, float *h)
{
    float u = unit() * e->s;

    switch (e->type) {
    case E_STICK: *w = *h = 34 * u; break;
    case E_PAD: case E_MENU: *w = *h = 10 * u; break;
    default:
        if (e->key == KEY_FIRE)
            *w = *h = 24 * u;
        else if (e->key == KEY_SOFT_LEFT || e->key == KEY_SOFT_RIGHT) {
            *w = 20 * u;
            *h = 10 * u;
        } else
            *w = *h = 14 * u;
    }
}

static SDL_Rect elem_rect(const Elem *e)
{
    float w, h;
    SDL_Rect r;

    elem_size(e, &w, &h);
    r.w = (int)w;
    r.h = (int)h;
    r.x = (int)(e->x * (float)T.W - w / 2);
    r.y = (int)(e->y * (float)T.H - h / 2);
    return r;
}

static void add(Layout *L, int type, int key, float x, float y, float s)
{
    if (L->n == MAX_ELEMS)
        return;
    L->e[L->n].type = type;
    L->e[L->n].key = key;
    L->e[L->n].x = x;
    L->e[L->n].y = y;
    L->e[L->n].s = s;
    L->n++;
}

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

/* Раскладка по умолчанию: в портрете — в полосе под игрой, в альбоме — по бокам. */
static void default_layout(int o)
{
    Layout *L = &T.lay[o];
    float W = (float)T.W, H = (float)T.H, u = unit();

    L->n = 0;
    L->valid = 0;
    if (!o) {
        float z0 = (float)(T.g.y + T.g.h), zh = H - z0, s;
        if (zh < H * 0.3f) {        /* места мало — кнопки поверх нижней части игры */
            z0 = H * 0.6f;
            zh = H - z0;
        }
        s = clampf(zh * 0.55f / (34 * u), 0.6f, 1.15f);
        add(L, E_KEY, KEY_SOFT_LEFT, 0.15f, (z0 + zh * 0.13f) / H, 1);
        add(L, E_KEY, KEY_SOFT_RIGHT, 0.85f, (z0 + zh * 0.13f) / H, 1);
        add(L, E_MENU, 0, 0.5f, (z0 + zh * 0.13f) / H, 1);
        add(L, E_STICK, 0, 0.28f, (z0 + zh * 0.58f) / H, s);
        add(L, E_KEY, KEY_FIRE, 0.76f, (z0 + zh * 0.58f) / H, s);
        add(L, E_PAD, 0, 0.5f, (z0 + zh * 0.9f) / H, 1);
    } else {
        float side = (float)T.g.x, lx, s;
        if (side < W * 0.16f)
            side = W * 0.24f;
        lx = side / 2;
        s = clampf(side * 0.85f / (34 * u), 0.6f, 1.15f);
        add(L, E_KEY, KEY_SOFT_LEFT, lx / W, 0.13f, 1);
        add(L, E_KEY, KEY_SOFT_RIGHT, 1 - lx / W, 0.13f, 1);
        add(L, E_MENU, 0, lx / W, 0.38f, 1);
        add(L, E_PAD, 0, 1 - lx / W, 0.38f, 1);
        add(L, E_STICK, 0, lx / W, 0.72f, s);
        add(L, E_KEY, KEY_FIRE, 1 - lx / W, 0.72f, s);
    }
}

static void ensure_layout(void)
{
    if (T.W > 0 && !cur()->valid && cur()->n == 0)
        default_layout(land());
}

/* ---------- профиль игры на диске ---------- */

static void profile_path(char *out, size_t cap, int mkdirs)
{
    const char *xdg = getenv("XDG_CONFIG_HOME"), *app = bt_appdata();
    char dir[1024], id[256];
    size_t i;

    if (xdg && *xdg)
        snprintf(dir, sizeof(dir), "%s/bt-j2me", xdg);
    else if (app)
        snprintf(dir, sizeof(dir), "%s/bt-j2me", app);
    else
        snprintf(dir, sizeof(dir), "%s/.config/bt-j2me", bt_home());
    snprintf(dir + strlen(dir), sizeof(dir) - strlen(dir), "/touch");
    for (i = 1; mkdirs && dir[i]; i++)     /* все папки по пути: конфиг может ещё не существовать */
        if (dir[i] == '/' || !dir[i + 1]) {
            char keep = dir[i + 1];
            if (dir[i] == '/') {
                dir[i] = '\0';
                bt_mkdir(dir);
                dir[i] = '/';
            } else {
                bt_mkdir(dir);
            }
            (void)keep;
        }
    snprintf(id, sizeof(id), "%s", T.id);
    for (i = 0; id[i]; i++)
        if (id[i] == '/' || id[i] == '\\' || id[i] == ':')
            id[i] = '_';
    snprintf(out, cap, "%s/%s.cfg", dir, id);
}

static void profile_load(void)
{
    char path[1400], line[128];
    FILE *f;
    int o = -1;

    T.opacity = 0.6f;
    T.four_way = 0;
    memset(T.lay, 0, sizeof(T.lay));
    profile_path(path, sizeof(path), 0);
    if (!(f = bt_fopen(path, "r")))
        return;
    while (fgets(line, sizeof(line), f)) {
        Elem e;
        float v;
        int n;
        if (sscanf(line, "opacity %f", &v) == 1)
            T.opacity = clampf(v, 0.15f, 1);
        else if (sscanf(line, "fourway %d", &n) == 1)
            T.four_way = n != 0;
        else if (sscanf(line, "layout %d", &n) == 1 && (n == 0 || n == 1)) {
            o = n;
            T.lay[o].valid = 1;
            T.lay[o].n = 0;
        } else if (o >= 0 && sscanf(line, "e %d %d %f %f %f", &e.type, &e.key, &e.x, &e.y, &e.s) == 5 &&
                   e.type >= E_KEY && e.type <= E_MENU)
            add(&T.lay[o], e.type, e.key, clampf(e.x, 0, 1), clampf(e.y, 0, 1), clampf(e.s, 0.4f, 2.5f));
    }
    fclose(f);
}

static void profile_save(void)
{
    char path[1400];
    FILE *f;
    int o, i;

    profile_path(path, sizeof(path), 1);
    if (!(f = bt_fopen(path, "w")))
        return;
    fprintf(f, "opacity %.2f\nfourway %d\n", T.opacity, T.four_way);
    for (o = 0; o < 2; o++) {
        if (!T.lay[o].valid)
            continue;
        fprintf(f, "layout %d\n", o);
        for (i = 0; i < T.lay[o].n; i++) {
            const Elem *e = &T.lay[o].e[i];
            fprintf(f, "e %d %d %.4f %.4f %.3f\n", e->type, e->key, e->x, e->y, e->s);
        }
    }
    fclose(f);
}

/* ---------- рисование подписей в RGBA-картинку ---------- */

static void put(Image *im, int x, int y, uint32_t c, float cov)
{
    uint32_t *p;
    float sa, da, oa;
    int k;
    uint32_t out = 0;

    if (x < 0 || y < 0 || x >= im->w || y >= im->h || cov <= 0)
        return;
    p = &im->px[y * im->w + x];
    sa = (float)(c >> 24) / 255.0f * (cov > 1 ? 1 : cov);
    da = (float)(*p >> 24) / 255.0f;
    oa = sa + da * (1 - sa);
    if (oa <= 0)
        return;
    for (k = 0; k < 24; k += 8) {
        float s = (float)((c >> k) & 255), d = (float)((*p >> k) & 255);
        out |= (uint32_t)((s * sa + d * da * (1 - sa)) / oa + 0.5f) << k;
    }
    *p = out | ((uint32_t)(oa * 255 + 0.5f) << 24);
}

/* Скруглённый прямоугольник или круг (rad ≥ половины стороны): заливка и обводка. */
static void shape(Image *im, float x0, float y0, float w, float h, float rad, uint32_t fill, uint32_t line, float t)
{
    float cx = x0 + w / 2, cy = y0 + h / 2, hw = w / 2, hh = h / 2;
    int x, y;

    if (rad > hw) rad = hw;
    if (rad > hh) rad = hh;
    for (y = (int)y0; y < (int)(y0 + h + 1); y++)
        for (x = (int)x0; x < (int)(x0 + w + 1); x++) {
            float qx = fabsf((float)x + 0.5f - cx) - (hw - rad), qy = fabsf((float)y + 0.5f - cy) - (hh - rad);
            float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
            float d = sqrtf(ox * ox + oy * oy) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - rad;
            if (fill >> 24)
                put(im, x, y, fill, 0.5f - d);
            if (t > 0 && (line >> 24))
                put(im, x, y, line, clampf(0.5f - fabsf(d + t / 2) + t / 2, 0, 1));
        }
}

static const char *const ic_gear[] = { "....#....", ".#.###.#.", "..#####..", ".##...##.", "###...###",
                                       ".##...##.", "..#####..", ".#.###.#.", "....#...." };
static const char *const ic_plus[] = { "...#...", "...#...", "...#...", "#######", "...#...", "...#...", "...#..." };
static const char *const ic_minus[] = { ".......", ".......", ".......", "#######", ".......", ".......", "......." };
static const char *const ic_check[] = { "........#", ".......##", "#.....##.", "##...##..", ".##.##...", "..###....",
                                        "...#....." };
static const char *const ic_reset[] = { "..####.#.", ".#....##.", "#....###.", "#........", "#.......#",
                                        "#.......#", ".#.....#.", "..#####.." };
static const char *const ic_trash[] = { "...###...", "#########", ".#.....#.", ".#.#.#.#.", ".#.#.#.#.",
                                        ".#.#.#.#.", ".#.....#.", "..#####.." };
static const char *const ic_eye[] = { "...#####...", ".##.....##.", "#...###...#", "#..#####..#", "#...###...#",
                                      ".##.....##.", "...#####..." };
static const char *const ic_size[] = { "###...###", "##.....##", "#.#...#.#", ".........", ".........",
                                       ".........", "#.#...#.#", "##.....##", "###...###" };
static const char *const ic_pad[] = { "##.##.##", "##.##.##", "........", "##.##.##", "##.##.##", "........",
                                      "##.##.##", "##.##.##" };
static const char *const ic_arrow[] = { "...#...", "..###..", ".#####.", "#######", "..###..", "..###..", "..###.." };

/* Значок по строкам; rot — поворот на 90°·rot по часовой. */
static void icon(Image *im, const char *const *rows, int n, int rot, int cx, int cy, int scale, uint32_t c)
{
    int w = (int)strlen(rows[0]), x, y, i, j;

    for (y = 0; y < n; y++)
        for (x = 0; x < w; x++) {
            int sx, sy;
            if (rows[y][x] != '#')
                continue;
            switch (rot & 3) {
            case 1:  sx = n - 1 - y; sy = x; break;
            case 2:  sx = w - 1 - x; sy = n - 1 - y; break;
            case 3:  sx = y; sy = w - 1 - x; break;
            default: sx = x; sy = y;
            }
            for (j = 0; j < scale; j++)
                for (i = 0; i < scale; i++)
                    put(im, cx - ((rot & 1) ? n : w) * scale / 2 + sx * scale + i,
                        cy - ((rot & 1) ? w : n) * scale / 2 + sy * scale + j, c, 1);
        }
}

/* Подпись по центру: из шрифтов и целых масштабов берём самый крупный, что влезает в ~55% кнопки. */
static void text_center(Image *im, const char *s, int cx, int cy, int h, uint32_t c)
{
    static const Font *const fonts[3] = { &font_largeb, &font_big, &font_small };
    const Font *f = fonts[2];
    int target = h * 55 / 100, best = 0, scale = 1, i;

    for (i = 0; i < 3; i++) {
        int k = target / fonts[i]->h;
        if (k >= 1 && fonts[i]->h * k > best) {
            best = fonts[i]->h * k;
            f = fonts[i];
            scale = k;
        }
    }
    gfx_text(im, f, cx - gfx_text_width(f, s, scale) / 2, cy - f->h * scale / 2, s, c, scale);
}

static void draw_label(Image *im, int label, int w, int h, uint32_t c)
{
    int m = w < h ? w : h, cx = w / 2, cy = h / 2;
    char buf[4];

    /* значок — около половины кнопки, целым масштабом, чтобы пиксели были ровными */
#define ICON(rows, rot) icon(im, rows, (int)(sizeof(rows) / sizeof(rows[0])), rot, cx, cy, \
                             (m + 9) / 2 / 9 > 1 ? (m + 9) / 2 / 9 : 1, c)
    switch (label) {
    case KEY_UP:    ICON(ic_arrow, 0); break;
    case KEY_RIGHT: ICON(ic_arrow, 1); break;
    case KEY_DOWN:  ICON(ic_arrow, 2); break;
    case KEY_LEFT:  ICON(ic_arrow, 3); break;
    case KEY_FIRE:
        shape(im, (float)cx - m * 0.22f, (float)cy - m * 0.22f, m * 0.44f, m * 0.44f, (float)m, 0, c, m * 0.06f);
        shape(im, (float)cx - m * 0.08f, (float)cy - m * 0.08f, m * 0.16f, m * 0.16f, (float)m, c, 0, 0);
        break;
    case KEY_SOFT_LEFT: case KEY_SOFT_RIGHT: {
        /* софт-клавиша — полоска с точкой с «своей» стороны, как подпись над кнопкой телефона */
        float bw = w * 0.5f, bh = h * 0.16f;
        shape(im, cx - bw / 2, cy - bh / 2, bw, bh, bh, c, 0, 0);
        shape(im, label == KEY_SOFT_LEFT ? cx - bw / 2 - bh * 2.2f : cx + bw / 2 + bh * 1.0f, cy - bh * 0.6f,
              bh * 1.2f, bh * 1.2f, bh, c, 0, 0);
        break;
    }
    case KEY_STAR:  text_center(im, "*", cx, cy, m, c); break;
    case KEY_POUND: text_center(im, "#", cx, cy, m, c); break;
    case KEY_CLEAR: text_center(im, "C", cx, cy, m, c); break;
    case L_PAD:   ICON(ic_pad, 0); break;
    case L_MENU:  ICON(ic_gear, 0); break;
    case L_PLUS:  ICON(ic_plus, 0); break;
    case L_MINUS: ICON(ic_minus, 0); break;
    case L_EYE:   ICON(ic_eye, 0); break;
    case L_CHECK: ICON(ic_check, 0); break;
    case L_RESET: ICON(ic_reset, 0); break;
    case L_TRASH: ICON(ic_trash, 0); break;
    case L_SIZE:  ICON(ic_size, 0); break;
    case L_STICK: case L_WAY4: case L_WAY8:
        shape(im, w * 0.2f, h * 0.2f, w * 0.6f, h * 0.6f, (float)w, 0, c, m * 0.06f);
        shape(im, w * 0.38f, h * 0.38f, w * 0.24f, h * 0.24f, (float)w, c, 0, 0);
        if (label != L_STICK)
            text_center(im, label == L_WAY4 ? "4" : "8", w * 3 / 4, h * 3 / 4, m * 6 / 10, c);
        break;
    default:
        if (label >= KEY_NUM0 && label <= KEY_NUM9) {
            buf[0] = (char)label;
            buf[1] = '\0';
            text_center(im, buf, cx, cy, m, c);
        }
    }
#undef ICON
}

static SDL_Texture *texture(int label, int w, int h, int style)
{
    Tex *slot = NULL;
    Image im;
    int i, round = label == L_STICK || label == L_KNOB;
    float t = (float)(w < h ? w : h) * 0.045f + 1;

    if (w < 2 || h < 2)
        return NULL;
    for (i = 0; i < TEX_CACHE; i++) {
        Tex *c = &T.cache[i];
        if (c->t && c->label == label && c->w == w && c->h == h && c->style == style)
            return c->t;
        if (!c->t && !slot)
            slot = c;
    }
    if (!slot) {                    /* кэш полон: начинаем заново */
        for (i = 0; i < TEX_CACHE; i++)
            if (T.cache[i].t)
                SDL_DestroyTexture(T.cache[i].t);
        memset(T.cache, 0, sizeof(T.cache));
        slot = &T.cache[0];
    }
    im = image_new(w, h);
    if (!im.px)
        return NULL;
    memset(im.px, 0, (size_t)w * (size_t)h * 4);
    if (label == L_KNOB)
        shape(&im, 1, 1, (float)w - 2, (float)h - 2, (float)w, 0xB0FFFFFFu, 0xFFFFFFFFu, t);
    else if (label == L_STICK && style == S_NORMAL)
        shape(&im, 1, 1, (float)w - 2, (float)h - 2, (float)w, 0x50000000u, 0xD0FFFFFFu, t);
    else {
        uint32_t fill = style == S_PRESSED ? 0x90FFFFFFu : style == S_CHIP_ON ? 0xE07E6FD6u :
                        style == S_CHIP ? 0xC0262233u : 0x70000000u;
        shape(&im, 1, 1, (float)w - 2, (float)h - 2, round ? (float)w : (float)(w < h ? w : h) * 0.22f,
              fill, 0xE0FFFFFFu, t);
        draw_label(&im, label, w, h, style == S_PRESSED ? 0xFF1A1724u : 0xFFFFFFFFu);
    }
    slot->t = SDL_CreateTexture(T.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (slot->t) {
        SDL_UpdateTexture(slot->t, NULL, im.px, w * 4);
        SDL_SetTextureBlendMode(slot->t, SDL_BLENDMODE_BLEND);
        slot->label = label;
        slot->w = w;
        slot->h = h;
        slot->style = style;
    }
    image_free(&im);
    return slot->t;
}

static void blit(int label, SDL_Rect r, int style, float alpha)
{
    SDL_Texture *t = texture(label, r.w, r.h, style);

    if (!t)
        return;
    SDL_SetTextureAlphaMod(t, (Uint8)(clampf(alpha, 0, 1) * 255));
    SDL_RenderCopy(T.ren, t, NULL, &r);
}

/* ---------- клавиши ---------- */

void touch_vibrate(int ms)
{
    if (T.haptic && ms > 0)
        SDL_HapticRumblePlay(T.haptic, ms > 30 ? 0.8f : 0.35f, (Uint32)ms);
}

static void vibrate(void)
{
    touch_vibrate(12);      /* короткий «щелчок» под пальцем */
}

static void press(int key)
{
    int k = key + 16;

    if (k < 0 || k >= KEY_SLOTS)
        return;
    if (T.keycount[k]++ == 0) {
        T.emit(EV_KEY_DOWN, key, 0, 0);
        vibrate();
    }
    T.dirty = 1;
}

static void release(int key)
{
    int k = key + 16;

    if (k < 0 || k >= KEY_SLOTS || T.keycount[k] == 0)
        return;
    if (--T.keycount[k] == 0)
        T.emit(EV_KEY_UP, key, 0, 0);
    T.dirty = 1;
}

static void release_all(void)
{
    int k;

    for (k = 0; k < KEY_SLOTS; k++)
        if (T.keycount[k]) {
            T.keycount[k] = 0;
            T.emit(EV_KEY_UP, k - 16, 0, 0);
        }
    if (T.ptr_down) {
        T.emit(EV_POINTER_UP, 0, 0, 0);
        T.ptr_down = 0;
    }
    memset(T.f, 0, sizeof(T.f));
    memset(T.sdir, 0, sizeof(T.sdir));
    T.stick_on = 0;
}

/* ---------- цифровая панель ---------- */

static const int pad_keys[12] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', KEY_STAR, '0', KEY_POUND };

static SDL_Rect pad_rect(int i)
{
    float u = unit(), b = 13 * u, gap = 2 * u, cx, cy;
    SDL_Rect r;

    if (!land()) {
        float z0 = (float)(T.g.y + T.g.h);
        cx = T.W / 2.0f;
        cy = z0 + (T.H - z0) * 0.5f;
        if (T.H - z0 < T.H * 0.3f)
            cy = T.H * 0.75f;
    } else {
        cx = T.W / 2.0f;
        cy = T.H - (b * 4 + gap * 3) / 2 - 2 * u;
    }
    r.w = r.h = (int)b;
    r.x = (int)(cx - (b * 3 + gap * 2) / 2 + (float)(i % 3) * (b + gap));
    r.y = (int)(cy - (b * 4 + gap * 3) / 2 + (float)(i / 3) * (b + gap));
    return r;
}

/* ---------- попадания ---------- */

static int inside(SDL_Rect r, float x, float y, float slop)
{
    return x >= r.x - slop && x < r.x + r.w + slop && y >= r.y - slop && y < r.y + r.h + slop;
}

static int hit_elem(float x, float y, int with_stick)
{
    Layout *L = cur();
    int i;

    for (i = L->n - 1; i >= 0; i--) {
        SDL_Rect r = elem_rect(&L->e[i]);
        if ((L->e[i].type != E_STICK || with_stick) && inside(r, x, y, r.w * 0.12f + unit()))
            return i;
    }
    return -1;
}

static int stick_index(void)
{
    Layout *L = cur();
    int i;

    for (i = 0; i < L->n; i++)
        if (L->e[i].type == E_STICK)
            return i;
    return -1;
}

/* Стик ловит касания в широкой зоне вокруг себя — палец не обязан попасть точно. */
static int in_stick_zone(float x, float y)
{
    int i = stick_index();
    SDL_Rect r;
    float ext;

    if (i < 0)
        return 0;
    r = elem_rect(&cur()->e[i]);
    ext = r.w * 0.45f;
    return x >= r.x - ext && x < r.x + r.w + ext && y >= r.y - ext && y < r.y + r.h + ext &&
           !inside(T.g, x, y, 0) ;
}

static float stick_radius(void)
{
    int i = stick_index();

    return i < 0 ? 1 : (float)elem_rect(&cur()->e[i]).w / 2;
}

static void stick_move(float x, float y)
{
    float r = stick_radius(), dx = x - T.sox, dy = y - T.soy, len = sqrtf(dx * dx + dy * dy);
    int want[4] = { 0, 0, 0, 0 }, d;
    static const int keys[4] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };

    if (len > r) {                  /* палец ушёл дальше — центр едет за ним, как у L3 */
        T.sox = x - dx / len * r;
        T.soy = y - dy / len * r;
        dx = x - T.sox;
        dy = y - T.soy;
        len = r;
    }
    T.skx = T.sox + dx;
    T.sky = T.soy + dy;
    if (len > r * 0.28f) {
        if (T.four_way) {
            if (fabsf(dx) > fabsf(dy))
                want[dx < 0 ? 2 : 3] = 1;
            else
                want[dy < 0 ? 0 : 1] = 1;
        } else {
            want[0] = dy < -0.38f * len;
            want[1] = dy > 0.38f * len;
            want[2] = dx < -0.38f * len;
            want[3] = dx > 0.38f * len;
        }
    }
    for (d = 0; d < 4; d++)
        if (want[d] != T.sdir[d]) {
            if (want[d])
                press(keys[d]);
            else
                release(keys[d]);
            T.sdir[d] = want[d];
        }
    T.dirty = 1;
}

static void stick_end(void)
{
    static const int keys[4] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
    int d;

    for (d = 0; d < 4; d++)
        if (T.sdir[d]) {
            release(keys[d]);
            T.sdir[d] = 0;
        }
    T.stick_on = 0;
    T.dirty = 1;
}

static void to_game(float x, float y, int *gx, int *gy)
{
    *gx = T.g.w > 0 ? (int)((x - (float)T.g.x) * (float)T.gw / (float)T.g.w) : 0;
    *gy = T.g.h > 0 ? (int)((y - (float)T.g.y) * (float)T.gh / (float)T.g.h) : 0;
}

/* ---------- редактор ---------- */

static const int addable[] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_FIRE, KEY_SOFT_LEFT, KEY_SOFT_RIGHT,
                               '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', KEY_STAR, KEY_POUND, KEY_CLEAR };

static int has_key(int type, int key)
{
    Layout *L = cur();
    int i;

    for (i = 0; i < L->n; i++)
        if (L->e[i].type == type && (type != E_KEY || L->e[i].key == key))
            return 1;
    return 0;
}

/* Панель редактора сверху: настройки, инструменты выбранной кнопки и палитра того, чего нет. */
static int editor_items(Item *it, int cap, SDL_Rect *panel)
{
    float u = unit();
    int b = (int)(11 * u), gap = (int)(2 * u) + 1, x = gap, y = gap, n = 0, i;
    int row1[5][3] = { { A_OP_MINUS, 0, L_MINUS }, { A_OP_PLUS, 0, L_PLUS }, { A_WAY, 0, 0 },
                       { A_RESET, 0, L_RESET }, { A_DONE, 0, L_CHECK } };

#define PUT(a, g, l) do { if (x + b > T.W - gap && x > gap) { x = gap; y += b + gap; } \
        if (n < cap) { it[n].action = (a); it[n].arg = (g); it[n].label = (l); \
        it[n].r.x = x; it[n].r.y = y; it[n].r.w = it[n].r.h = b; n++; } x += b + gap; } while (0)
    for (i = 0; i < 5; i++) {
        PUT(row1[i][0], 0, row1[i][0] == A_WAY ? (T.four_way ? L_WAY4 : L_WAY8) : row1[i][2]);
        if (i == 1)
            x += gap * 2;
    }
    if (T.sel >= 0 && T.sel < cur()->n) {
        x = gap;                    /* инструменты выбранной кнопки — своей строкой */
        y += b + gap;
        PUT(A_SIZE_MINUS, 0, L_MINUS);
        PUT(A_SIZE_PLUS, 0, L_PLUS);
        if (cur()->e[T.sel].type != E_MENU)
            PUT(A_DELETE, 0, L_TRASH);
    }
    x = gap;
    y += b + gap * 2;
    for (i = 0; i < (int)(sizeof(addable) / sizeof(addable[0])); i++)
        if (!has_key(E_KEY, addable[i]))
            PUT(A_ADD, addable[i], addable[i]);
    if (!has_key(E_STICK, 0))
        PUT(A_ADD, -100 - E_STICK, L_STICK);
    if (!has_key(E_PAD, 0))
        PUT(A_ADD, -100 - E_PAD, L_PAD);
#undef PUT
    panel->x = 0;
    panel->y = 0;
    panel->w = T.W;
    panel->h = y + (x > gap ? b : 0) + gap;
    return n;
}

static void editor_action(const Item *it)
{
    Layout *L = cur();

    switch (it->action) {
    case A_OP_MINUS: T.opacity = clampf(T.opacity - 0.1f, 0.15f, 1); break;
    case A_OP_PLUS:  T.opacity = clampf(T.opacity + 0.1f, 0.15f, 1); break;
    case A_WAY:      T.four_way = !T.four_way; break;
    case A_RESET:
        default_layout(land());
        T.opacity = 0.6f;
        T.four_way = 0;
        T.sel = -1;
        break;
    case A_DONE:
        L->valid = 1;
        profile_save();
        T.edit = 0;
        T.sel = -1;
        break;
    case A_SIZE_MINUS: case A_SIZE_PLUS:
        if (T.sel >= 0)
            L->e[T.sel].s = clampf(L->e[T.sel].s * (it->action == A_SIZE_PLUS ? 1.12f : 0.89f), 0.4f, 2.5f);
        break;
    case A_DELETE:
        if (T.sel >= 0) {
            memmove(&L->e[T.sel], &L->e[T.sel + 1], (size_t)(L->n - T.sel - 1) * sizeof(Elem));
            L->n--;
            T.sel = -1;
        }
        break;
    case A_ADD:
        if (it->arg <= -100)
            add(L, -100 - it->arg, 0, 0.5f, 0.5f, 1);
        else
            add(L, E_KEY, it->arg, 0.5f, 0.5f, 1);
        T.sel = L->n - 1;
        break;
    }
    L->valid = 1;   /* раскладка тронута — дальше она своя, а не «по умолчанию» */
    T.dirty = 1;
}

/* ---------- события ---------- */

static Finger *finger(SDL_FingerID id, int create)
{
    int i;

    for (i = 0; i < MAX_FINGERS; i++)
        if (T.f[i].used && T.f[i].id == id)
            return &T.f[i];
    for (i = 0; create && i < MAX_FINGERS; i++)
        if (!T.f[i].used) {
            memset(&T.f[i], 0, sizeof(Finger));
            T.f[i].used = 1;
            T.f[i].id = id;
            return &T.f[i];
        }
    return NULL;
}

static void edit_down(Finger *f, float x, float y)
{
    Item it[48];
    SDL_Rect panel;
    int n = editor_items(it, 48, &panel), i;

    for (i = 0; i < n; i++)
        if (inside(it[i].r, x, y, 0)) {
            f->kind = F_EDIT_UI;
            editor_action(&it[i]);
            return;
        }
    if (inside(panel, x, y, 0)) {
        f->kind = F_EDIT_UI;
        return;
    }
    i = hit_elem(x, y, 1);
    T.sel = i;
    if (i >= 0) {
        f->kind = F_EDIT_DRAG;
        f->elem = i;
        f->dx = cur()->e[i].x * (float)T.W - x;
        f->dy = cur()->e[i].y * (float)T.H - y;
    }
    T.dirty = 1;
}

static void down(Finger *f, float x, float y)
{
    int i, gx, gy;

    if (!T.game) {                  /* лаунчер: касание — это указатель */
        if (!T.ptr_down) {
            f->kind = F_POINTER;
            T.ptr_down = 1;
            to_game(x, y, &gx, &gy);
            T.emit(EV_POINTER_DOWN, 0, gx, gy);
        }
        return;
    }
    if (T.edit) {
        edit_down(f, x, y);
        return;
    }
    if (T.pad_open)
        for (i = 0; i < 12; i++)
            if (inside(pad_rect(i), x, y, unit())) {
                f->kind = F_PANEL;
                f->key = pad_keys[i];
                press(f->key);
                return;
            }
    if ((i = hit_elem(x, y, 0)) >= 0) {
        Elem *e = &cur()->e[i];
        if (e->type == E_KEY) {
            f->kind = F_KEY;
            f->elem = i;
            f->key = e->key;
            press(e->key);
        } else if (e->type == E_PAD) {
            T.pad_open = !T.pad_open;
            vibrate();
        } else if (e->type == E_MENU) {
            release_all();
            T.edit = 1;
            T.sel = -1;
            T.pad_open = 0;
        }
        T.dirty = 1;
        return;
    }
    if (!T.stick_on && in_stick_zone(x, y)) {
        f->kind = F_STICK;
        T.stick_on = 1;
        T.sox = T.skx = x;
        T.soy = T.sky = y;
        stick_move(x, y);
        return;
    }
    if (inside(T.g, x, y, 0) && !T.ptr_down) {
        f->kind = F_POINTER;
        T.ptr_down = 1;
        to_game(x, y, &gx, &gy);
        T.emit(EV_POINTER_DOWN, 0, gx, gy);
    }
}

static void move(Finger *f, float x, float y)
{
    int gx, gy, i;

    switch (f->kind) {
    case F_STICK:
        stick_move(x, y);
        break;
    case F_KEY:
        /* скольжение пальцем с кнопки на кнопку, как по крестовине */
        i = hit_elem(x, y, 0);
        if (i >= 0 && i != f->elem && cur()->e[i].type == E_KEY) {
            release(f->key);
            f->elem = i;
            f->key = cur()->e[i].key;
            press(f->key);
        }
        break;
    case F_POINTER:
        to_game(x, y, &gx, &gy);
        T.emit(EV_POINTER_MOVE, 0, gx, gy);
        break;
    case F_EDIT_DRAG:
        if (f->elem < cur()->n) {
            cur()->e[f->elem].x = clampf((x + f->dx) / (float)T.W, 0.02f, 0.98f);
            cur()->e[f->elem].y = clampf((y + f->dy) / (float)T.H, 0.02f, 0.98f);
            cur()->valid = 1;
            T.dirty = 1;
        }
        break;
    }
}

static void up(Finger *f, float x, float y)
{
    int gx, gy;

    switch (f->kind) {
    case F_STICK:   stick_end(); break;
    case F_KEY: case F_PANEL: release(f->key); break;
    case F_POINTER:
        to_game(x, y, &gx, &gy);
        T.emit(EV_POINTER_UP, 0, gx, gy);
        T.ptr_down = 0;
        break;
    }
    f->used = 0;
}

int touch_event(const SDL_Event *e)
{
    Finger *f;
    float x, y;

    if (e->type != SDL_FINGERDOWN && e->type != SDL_FINGERMOTION && e->type != SDL_FINGERUP)
        return 0;
    if (T.W <= 0)
        return 1;
    ensure_layout();
    x = e->tfinger.x * (float)T.W;
    y = e->tfinger.y * (float)T.H;
    f = finger(e->tfinger.fingerId, e->type == SDL_FINGERDOWN);
    if (!f)
        return 1;
    if (e->type == SDL_FINGERDOWN)
        down(f, x, y);
    else if (e->type == SDL_FINGERMOTION)
        move(f, x, y);
    else
        up(f, x, y);
    if (e->type == SDL_FINGERDOWN && f->kind == F_NONE)
        f->used = 0;
    return 1;
}

/* ---------- рисование ---------- */

void touch_place(int W, int H, int gw, int gh, ScaleMode mode, SDL_Rect *d)
{
    float maxh = (float)H, k;
    int portrait = H > W, ki;

    if (W != T.W || H != T.H) {     /* повернули экран — другая раскладка */
        if (T.W && land() != (W > H))
            release_all();
        T.W = W;
        T.H = H;
        T.lay[0].n = T.lay[0].valid ? T.lay[0].n : 0;   /* раскладки по умолчанию пересчитаем */
        T.lay[1].n = T.lay[1].valid ? T.lay[1].n : 0;
        T.dirty = 1;
    }
    if (T.game && portrait)
        maxh = (float)H * 0.68f;    /* треть экрана снизу — под кнопки */
    k = (float)W / (float)gw < maxh / (float)gh ? (float)W / (float)gw : maxh / (float)gh;
    ki = (int)k;
    if (ki >= 1 && (mode == SCALE_INTEGER || (mode == SCALE_AUTO && (float)ki >= k * 0.85f)))
        k = (float)ki;
    d->w = (int)((float)gw * k);
    d->h = (int)((float)gh * k);
    d->x = (W - d->w) / 2;
    d->y = T.game && portrait ? 0 : (H - d->h) / 2;
    T.g = *d;
    T.gw = gw;
    T.gh = gh;
}

void touch_draw(void)
{
    Layout *L;
    int i;
    float a;

    T.dirty = 0;
    if (!T.game || T.W <= 0)
        return;
    ensure_layout();
    L = cur();
    if (T.edit) {
        SDL_SetRenderDrawBlendMode(T.ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(T.ren, 0, 0, 0, 110);
        SDL_RenderFillRect(T.ren, NULL);
    }
    a = T.edit ? 1 : T.opacity;
    for (i = 0; i < L->n; i++) {
        const Elem *e = &L->e[i];
        SDL_Rect r = elem_rect(e);
        if (e->type == E_STICK) {
            if (T.stick_on && !T.edit) {
                SDL_Rect b = r, k;
                b.x = (int)(T.sox - (float)r.w / 2);
                b.y = (int)(T.soy - (float)r.h / 2);
                k.w = k.h = r.w * 45 / 100;
                k.x = (int)(T.skx - (float)k.w / 2);
                k.y = (int)(T.sky - (float)k.h / 2);
                blit(L_STICK, b, S_NORMAL, a);
                blit(L_KNOB, k, S_NORMAL, a);
            } else {
                SDL_Rect k;
                k.w = k.h = r.w * 45 / 100;
                k.x = r.x + (r.w - k.w) / 2;
                k.y = r.y + (r.h - k.h) / 2;
                blit(L_STICK, r, S_NORMAL, a);
                blit(L_KNOB, k, S_NORMAL, a * 0.8f);
            }
        } else {
            int label = e->type == E_PAD ? L_PAD : e->type == E_MENU ? L_MENU : e->key;
            int pressed = e->type == E_KEY && T.keycount[e->key + 16 < 0 ? 0 : e->key + 16] > 0;
            if (e->type == E_PAD && T.pad_open)
                pressed = 1;
            blit(label, r, pressed ? S_PRESSED : S_NORMAL, a);
        }
        if (T.edit && i == T.sel) {
            SDL_Rect s = r;
            int t;
            SDL_SetRenderDrawColor(T.ren, 0x9C, 0x8F, 0xF0, 255);
            for (t = 0; t < (int)(unit() * 0.8f) + 1; t++) {
                SDL_RenderDrawRect(T.ren, &s);
                s.x--; s.y--; s.w += 2; s.h += 2;
            }
        }
    }
    if (T.pad_open && !T.edit)
        for (i = 0; i < 12; i++)
            blit(pad_keys[i], pad_rect(i), T.keycount[pad_keys[i] + 16] ? S_PRESSED : S_NORMAL,
                 T.opacity + 0.2f);
    if (T.edit) {
        Item it[48];
        SDL_Rect panel;
        int n = editor_items(it, 48, &panel);
        SDL_SetRenderDrawColor(T.ren, 12, 11, 18, 200);
        SDL_RenderFillRect(T.ren, &panel);
        for (i = 0; i < n; i++)
            blit(it[i].label, it[i].r, it[i].action == A_DONE ? S_CHIP_ON : S_CHIP, 1);
        /* прозрачность — полоска под кнопками −/+ */
        if (n >= 2) {
            SDL_Rect bar = { it[0].r.x, it[0].r.y + it[0].r.h + 1, it[1].r.x + it[1].r.w - it[0].r.x,
                             (int)(unit() * 0.8f) + 1 };
            SDL_SetRenderDrawColor(T.ren, 60, 56, 80, 255);
            SDL_RenderFillRect(T.ren, &bar);
            bar.w = (int)((float)bar.w * T.opacity);
            SDL_SetRenderDrawColor(T.ren, 0x9C, 0x8F, 0xF0, 255);
            SDL_RenderFillRect(T.ren, &bar);
        }
    }
}

int touch_dirty(void)
{
    return T.dirty;
}

/* ---------- жизненный цикл ---------- */

void touch_init(SDL_Renderer *ren, TouchEmit emit)
{
    memset(&T, 0, sizeof(T));
    T.ren = ren;
    T.emit = emit;
    T.sel = -1;
    T.opacity = 0.6f;
    if (SDL_InitSubSystem(SDL_INIT_HAPTIC) == 0 && SDL_NumHaptics() > 0 && (T.haptic = SDL_HapticOpen(0)) != NULL &&
        SDL_HapticRumbleInit(T.haptic) != 0) {
        SDL_HapticClose(T.haptic);
        T.haptic = NULL;
    }
}

void touch_shutdown(void)
{
    int i;

    for (i = 0; i < TEX_CACHE; i++)
        if (T.cache[i].t)
            SDL_DestroyTexture(T.cache[i].t);
    if (T.haptic)
        SDL_HapticClose(T.haptic);
    memset(&T, 0, sizeof(T));
}

void touch_profile(const char *game_id)
{
    if (!T.ren)
        return;
    release_all();
    T.edit = 0;
    T.pad_open = 0;
    T.sel = -1;
    T.game = game_id != NULL;
    if (T.game) {
        snprintf(T.id, sizeof(T.id), "%s", game_id);
        profile_load();
    }
    T.dirty = 1;
}
