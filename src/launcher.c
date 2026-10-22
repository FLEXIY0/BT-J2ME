#define _DEFAULT_SOURCE
#include "launcher.h"
#include "library.h"
#include "midp.h"
#include "compat.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Лаунчер без надписей: только иконки, название игры и счётчик.
 * Всё рисуется в маленьком виртуальном разрешении и масштабируется целым множителем. */

#define REPEAT_DELAY 330
#define REPEAT_RATE  110
#define LAUNCH_MS    1600
#define KEEP_COVERS  24     /* обложки дальше этого от выбранной выгружаем — память важнее */
#define BOX_ASPECT   0.72f  /* коробка как у игр для приставок: ширина / высота */
#define SIDE_SCALE   0.84f

enum { TAB_LIBRARY, TAB_PLAY, TAB_SETTINGS, TAB_COUNT };
enum { SET_THEME, SET_PIXELS, SET_SHARP, SET_COUNT };

typedef struct {
    uint32_t bg_top, bg_bottom, text, sub, accent, tab_hi, divider, rim, rim_hi, shadow;
} Theme;

static const Theme theme_light = {
    0xFFF4EDEDu, 0xFFE4DADCu, 0xFF2A2430u, 0xFF84798Au, 0xFF7E6FD6u,
    0xFFE2D9DFu, 0xFFD5CBD0u, 0xFF1B2131u, 0xFF3C4560u, 0x2E2A1A2Au
};
static const Theme theme_dark = {
    0xFF1A1724u, 0xFF0C0B12u, 0xFFEDE8F2u, 0xFF8F8899u, 0xFF9C8FF0u,
    0xFF272334u, 0xFF2C2839u, 0xFF07080Cu, 0xFF3A4256u, 0x60000000u
};

typedef struct {
    int dark;        /* тёмная тема */
    int big_pixels;  /* крупный пиксель (~180 строк) или мелкий (~270) */
    int sharp;       /* игры — только целый масштаб */
} Settings;

typedef struct {
    Library      lib;
    Image       *scr;
    const Theme *th;
    Settings     set;
    char         set_path[1024];
    ScaleMode    cli_scale;
    int          W, H, s;                 /* виртуальный экран и масштаб иконок */
    int          m, tab_h, title_y, box_top, box_w, box_h, cover_w, cover_h, small_w, small_h;
    int          top_min, bottom_max, lib_title_y;   /* зона контента между счётчиком и вкладками */
    int          cell_w, cell_h, cols, gap;
    int          tab, focus_tabs;
    int          sel, set_sel;
    float        pos, grid_scroll;
    int          held, held_since, last_repeat;
    int          pad_connected;
    uint32_t     pad_flash;
    uint32_t     launch_t0;
} Ui;

/* ---------- пиксельные иконки ---------- */

typedef struct {
    int         w, h;
    const char *rows[13];
} Icon;

static const Icon icon_list = { 11, 9, {
    "##.########", "##.########", "...........",
    "##.########", "##.########", "...........",
    "##.########", "##.########", "..........." } };
static const Icon icon_pad = { 13, 9, {
    "..#########..", ".###########.", "###.#####.###",
    "##...###.#.##", "###.#####.###", "#############",
    "#####...#####", "####.....####", ".##.......##." } };
static const Icon icon_gear = { 11, 11, {
    "....###....", ".##.###.##.", ".#########.", "..##...##..",
    "###.....###", "###.....###", "###.....###", "..##...##..",
    ".#########.", ".##.###.##.", "....###...." } };
static const Icon icon_chevron = { 6, 9, {
    "....##", "...##.", "..##..", ".##...", "##....",
    ".##...", "..##..", "...##.", "....##" } };
static const Icon icon_sun = { 11, 11, {
    ".....#.....", ".#...#...#.", "..#.....#..", "....###....",
    "...#####...", "##.#####.##", "...#####...", "....###....",
    "..#.....#..", ".#...#...#.", ".....#....." } };
static const Icon icon_moon = { 11, 11, {
    "....####...", "..####.....", ".####......", ".###.......",
    "####.......", "####.......", "####.......", ".####.....#",
    ".######..##", "..#######..", "....####..." } };
static const Icon icon_back = { 11, 7, {
    "...#.......", "..##.......", ".##########", "###########",
    ".##########", "..##.......", "...#......." } };
static const Icon icon_folder = { 13, 10, {
    "#####........", "#...#........", "#############", "#...........#",
    "#...........#", "#...........#", "#...........#", "#...........#",
    "#...........#", "#############" } };

static void draw_icon(Image *d, const Icon *ic, int x, int y, int s, uint32_t c, int flip)
{
    int i, j;

    for (j = 0; j < ic->h; j++)
        for (i = 0; i < ic->w; i++)
            if (ic->rows[j][flip ? ic->w - 1 - i : i] == '#')
                gfx_fill(d, x + i * s, y + j * s, s, s, c);
}

/* ---------- мелкие помощники ---------- */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* Прямоугольник со «срезанными» пиксельными углами. */
static void rrect(Image *d, int x, int y, int w, int h, int r, uint32_t c)
{
    int j;

    for (j = 0; j < h; j++) {
        int in = j < r ? r - j : j >= h - r ? j - (h - r) + 1 : 0;
        gfx_fill(d, x + in, y + j, w - 2 * in, 1, c);
    }
}

/* Верхний регистр для латиницы и кириллицы, как на коробках в референсе. */
static void utf8_upper(char *dst, size_t cap, const char *src)
{
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)src;

    while (*p && o + 3 < cap) {
        if (*p >= 'a' && *p <= 'z') {
            dst[o++] = (char)(*p++ - 32);
        } else if (p[0] == 0xD0 && p[1] >= 0xB0 && p[1] <= 0xBF) {      /* а..п */
            dst[o++] = (char)0xD0;
            dst[o++] = (char)(p[1] - 0x20);
            p += 2;
        } else if (p[0] == 0xD1 && p[1] >= 0x80 && p[1] <= 0x8F) {      /* р..я */
            dst[o++] = (char)0xD0;
            dst[o++] = (char)(p[1] + 0x20);
            p += 2;
        } else if (p[0] == 0xD1 && p[1] == 0x91) {                      /* ё */
            dst[o++] = (char)0xD0;
            dst[o++] = (char)0x81;
            p += 2;
        } else {
            dst[o++] = (char)*p++;
        }
    }
    dst[o] = '\0';
}

/* Текст по центру; если не влезает — мелким шрифтом, потом с обрезкой. */
static void text_fit(Image *d, int cx, int y, int max_w, const char *t, uint32_t c, int prefer_big)
{
    const Font *f = prefer_big ? &font_big : &font_small;
    char buf[160];
    size_t n;

    snprintf(buf, sizeof(buf), "%s", t);
    if (prefer_big && gfx_text_width(f, buf, 1) > max_w) {
        f = &font_small;
        y += (font_big.h - font_small.h) / 2;
    }
    while (gfx_text_width(f, buf, 1) > max_w && (n = strlen(buf)) > 4) {
        unsigned char ch;
        n -= 3;                                   /* место под «...» */
        do
            ch = (unsigned char)buf[--n];
        while (n && (ch & 0xC0) == 0x80);
        strcpy(buf + n, "...");
    }
    gfx_text(d, f, cx - gfx_text_width(f, buf, 1) / 2, y, buf, c, 1);
}

/* ---------- настройки ---------- */

static void settings_path(Ui *ui)
{
    const char *xdg = getenv("XDG_CONFIG_HOME"), *app = bt_appdata();

    if (xdg && *xdg)
        snprintf(ui->set_path, sizeof(ui->set_path), "%s/bt-j2me", xdg);
    else if (app)
        snprintf(ui->set_path, sizeof(ui->set_path), "%s/bt-j2me", app);
    else
        snprintf(ui->set_path, sizeof(ui->set_path), "%s/.config/bt-j2me", bt_home());
}

static void settings_load(Ui *ui)
{
    char path[1100], key[32];
    int val;
    FILE *f;

    ui->set.dark = 0;
    ui->set.big_pixels = 1;
    ui->set.sharp = 0;
    settings_path(ui);
    snprintf(path, sizeof(path), "%s/settings", ui->set_path);
    if (!(f = bt_fopen(path, "r")))
        return;
    while (fscanf(f, "%31[^=]=%d\n", key, &val) == 2) {
        if (!strcmp(key, "dark"))
            ui->set.dark = val != 0;
        else if (!strcmp(key, "big_pixels"))
            ui->set.big_pixels = val != 0;
        else if (!strcmp(key, "sharp"))
            ui->set.sharp = val != 0;
    }
    fclose(f);
}

static void settings_save(const Ui *ui)
{
    char path[1100], parent[1100];
    char *slash;
    FILE *f;

    snprintf(parent, sizeof(parent), "%s", ui->set_path);
    if ((slash = strrchr(parent, '/')) != NULL) {
        *slash = '\0';
        bt_mkdir(parent);
    }
    bt_mkdir(ui->set_path);
    snprintf(path, sizeof(path), "%s/settings", ui->set_path);
    if (!(f = bt_fopen(path, "w")))
        return;
    fprintf(f, "dark=%d\nbig_pixels=%d\nsharp=%d\n", ui->set.dark, ui->set.big_pixels, ui->set.sharp);
    fclose(f);
}

/* ---------- разметка ---------- */

static void cover_area(int w, int h, int *cw, int *ch)
{
    int rim = w >= 40 ? 2 : 1;

    *cw = w - rim * 2;
    *ch = h - rim * 2;
}

static void layout(Ui *ui)
{
    int dw, dh, target, k, i, old_w = ui->cover_w, old_h = ui->cover_h, region, title_h, gap, group;

    ui->th = ui->set.dark ? &theme_dark : &theme_light;
    platform_display_size(&dw, &dh);
    /* Короткая сторона экрана ≈ target пикселей, множитель — целый: весь экран без полей. */
    target = ui->set.big_pixels ? 190 : 270;
    k = (imin(dw, dh) + target / 2) / target;
    k = k < 1 ? 1 : k;
    ui->W = dw / k;
    ui->H = dh / k;
    platform_set_screen(ui->W, ui->H, SCALE_INTEGER);
    ui->scr = platform_screen();

    ui->s = imax(1, (imin(ui->W, ui->H) + 60) / 300);
    ui->m = imax(4, ui->H / 40);
    ui->tab_h = icon_pad.h * ui->s + 2 * imax(6, ui->H / 26);
    title_h = font_big.h + 4 + font_small.h;
    gap = imax(8, ui->H / 22);
    ui->top_min = ui->m + font_small.h / 2 + 2;
    ui->bottom_max = ui->H - ui->tab_h - imax(6, ui->H / 28);
    ui->lib_title_y = ui->bottom_max - title_h;
    region = ui->bottom_max - ui->top_min - gap - title_h;

    /* Коробка: по высоте свободной зоны, но не шире ~58% экрана (вертикальные экраны);
     * группа «коробка + название» центрируется в оставшемся месте. */
    ui->box_h = region;
    ui->box_w = (int)((float)ui->box_h * BOX_ASPECT);
    if (ui->box_w > ui->W * 58 / 100) {
        ui->box_w = ui->W * 58 / 100;
        ui->box_h = (int)((float)ui->box_w / BOX_ASPECT);
    }
    group = ui->box_h + gap + title_h;
    ui->box_top = ui->top_min + (ui->bottom_max - ui->top_min - group) / 2;
    ui->title_y = ui->box_top + ui->box_h + gap;
    cover_area(ui->box_w, ui->box_h, &ui->cover_w, &ui->cover_h);
    cover_area((int)((float)ui->box_w * SIDE_SCALE), (int)((float)ui->box_h * SIDE_SCALE),
               &ui->small_w, &ui->small_h);

    /* Сетка библиотеки: минимум 3 колонки и примерно 2 ряда в видимой зоне. */
    ui->gap = imax(4, ui->W / 60);
    ui->cell_h = (ui->lib_title_y - gap - ui->top_min - ui->gap) / 2;
    ui->cell_w = (int)((float)ui->cell_h * BOX_ASPECT);
    if (ui->cell_w > (ui->W - 2 * ui->m - 2 * ui->gap) / 3) {
        ui->cell_w = (ui->W - 2 * ui->m - 2 * ui->gap) / 3;
        ui->cell_h = (int)((float)ui->cell_w / BOX_ASPECT);
    }
    ui->cols = imax(1, (ui->W - 2 * ui->m + ui->gap) / (ui->cell_w + ui->gap));

    for (i = 0; i < ui->lib.count && (old_w != ui->cover_w || old_h != ui->cover_h); i++)
        ui->lib.games[i].cover_ready = 0;
}

/* ---------- фон и коробки ---------- */

static void draw_background(Ui *ui, int top, int bottom)
{
    Image *s = ui->scr;
    int bands = 10, i, x, y;

    /* «ступенчатый» градиент — как на старых приставках с ограниченной палитрой */
    for (i = 0; i < bands; i++) {
        uint32_t c = 0xFF000000u;
        int sh, y0 = imax(top, ui->H * i / bands), y1 = imin(bottom, ui->H * (i + 1) / bands);
        if (y1 <= y0)
            continue;
        for (sh = 0; sh < 24; sh += 8) {
            int a = (int)((ui->th->bg_top >> sh) & 255), b = (int)((ui->th->bg_bottom >> sh) & 255);
            c |= (uint32_t)(a + (b - a) * i / (bands - 1)) << sh;
        }
        gfx_fill(s, 0, y0, ui->W, y1 - y0, c);
    }
    /* редкая пиксельная «зернистость» */
    for (y = top + (9 - top % 6) % 6; y < bottom; y += 6)
        for (x = (y / 6) % 2 * 3; x < ui->W; x += 6)
            gfx_fill(s, x, y, 1, 1, ui->set.dark ? 0x0CFFFFFFu : 0x0A000000u);
}

/* Геометрия столбца коробки: торец или лицевая сторона, верх и высота.
 * Коробки повёрнуты «корешком» к центру: ближний край — внутренний, дальний (ниже) — внешний,
 * а со стороны центра виден тёмный торец. У центральной торец чуть виден слева. */
typedef struct {
    int   x, w, depth, face_x, face_w;
    float tilt, at;
} BoxGeom;

static void box_geom(BoxGeom *b, int x, int w, float tilt)
{
    b->x = x;
    b->w = w;
    b->tilt = tilt;
    b->at = fabsf(tilt) < 0.02f ? 0 : fabsf(tilt);
    b->depth = imax(2, (int)((float)w * (0.035f + 0.04f * b->at)));
    if (tilt < -0.02f) {                 /* левая коробка: торец справа */
        b->face_x = x;
        b->face_w = w - b->depth;
    } else if (tilt > 0.02f) {           /* правая: торец слева */
        b->face_x = x + b->depth;
        b->face_w = w - b->depth;
    } else {
        b->face_x = x + b->depth / 2;
        b->face_w = w - b->depth / 2;
    }
}

/* 1 — лицевая сторона, 0 — торец; *ch — высота столбца при полной высоте h. */
static int box_column(const BoxGeom *b, int px, int h, int *ch)
{
    if (px >= b->face_x && px < b->face_x + b->face_w) {
        float t = (float)(px - b->face_x) / (float)b->face_w;
        float outer = b->tilt < 0 ? 1 - t : t;            /* 1 у внешнего (дальнего) края */
        *ch = (int)((float)h * (1 - 0.07f * b->at * outer));
        return 1;
    } else {
        float u = px < b->face_x ? (float)(b->face_x - px) / (float)imax(1, b->face_x - b->x)
                                 : (float)(px - (b->face_x + b->face_w - 1)) / (float)imax(1, b->depth);
        *ch = (int)((float)h * (1 - 0.06f * u));
        return 0;
    }
}

/* Коробка с обложкой. tilt -1..1 — насколько она сбоку от центра. */
static void draw_box(Ui *ui, int x, int y, int w, int h, Game *g, float tilt, int dim, uint32_t now)
{
    Image *s = ui->scr;
    const Image *src = NULL;
    BoxGeom b;
    int rim = w >= 40 ? 2 : 1, fade = 255, i, j, pass;
    unsigned sa = ui->th->shadow >> 24;

    if (g && g->cover_ready && g->cover.px) {
        src = g->cover_small.px && w <= g->cover_small.w + rim * 2 + 1 ? &g->cover_small : &g->cover;
        fade = (int)clampf((float)(now - g->cover_time) * 255.0f / 300.0f, 0, 255);
    }
    box_geom(&b, x, w, tilt);

    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < w; i++) {
            int px = x + i, ch, cy, face = box_column(&b, px, h, &ch), edge;

            cy = y + (h - ch) / 2;
            if (pass == 0) {
                /* тень: сдвинута вниз-вправо и мягко тянется по «полу» */
                int k, tail = imax(2, h / 16);
                gfx_fill(s, px + 2, cy + 3, 1, ch, ui->th->shadow);
                for (k = 0; k < tail; k++)
                    gfx_fill(s, px + 2, cy + ch + 3 + k, 1, 1, (sa * (unsigned)(tail - k) / (unsigned)(tail + 1)) << 24);
                continue;
            }
            /* скруглённые пиксельные углы */
            edge = i == 0 || i == w - 1 || (face && (px == b.face_x || px == b.face_x + b.face_w - 1));
            if (edge) {
                cy += 1;
                ch -= 2;
            }
            if (!face) {
                gfx_fill(s, px, cy + 1, 1, ch - 2, ui->th->rim);
                gfx_fill(s, px, cy + 1, 1, 1, ui->th->rim_hi);
                continue;
            }
            if (px < b.face_x + rim || px >= b.face_x + b.face_w - rim) {
                gfx_fill(s, px, cy, 1, ch, ui->th->rim);
                continue;
            }
            gfx_fill(s, px, cy, 1, rim, ui->th->rim);
            gfx_fill(s, px, cy + ch - rim, 1, rim, ui->th->rim);
            if (src) {
                int sx = imin(src->w - 1, (px - b.face_x - rim) * src->w / imax(1, b.face_w - 2 * rim));
                int inner = ch - 2 * rim;
                for (j = 0; j < inner; j++) {
                    int py = cy + rim + j;
                    uint32_t p = src->px[(j * src->h / inner) * src->w + sx] | 0xFF000000u;
                    if (py >= 0 && py < s->h && px >= 0 && px < s->w)
                        s->px[py * s->w + px] = fade >= 255 ? p : gfx_mix(s->px[py * s->w + px], p, (unsigned)fade);
                }
            } else {
                /* обложка ещё не загрузилась — бежит блик */
                int sweep = (int)(now / 5 % (unsigned)(b.face_w + 30)) - 15;
                gfx_fill(s, px, cy + rim, 1, ch - 2 * rim, 0xFF3A3F4Cu);
                if (px - b.face_x >= sweep && px - b.face_x < sweep + 8)
                    gfx_fill(s, px, cy + rim, 1, ch - 2 * rim, 0x30FFFFFFu);
            }
            /* блик на пластике: светлая кромка сверху и косая полоса */
            gfx_fill(s, px, cy + rim, 1, 1, 0x40FFFFFFu);
            for (j = 0; j < ch; j++) {
                int bx = b.face_x + b.face_w * 72 / 100 - j * 4 / 10;
                if (px >= bx && px < bx + b.face_w / 12) {
                    gfx_fill(s, px, cy + j, 1, 1, 0x12FFFFFFu);
                }
            }
        }
    }
    if (dim > 0)
        gfx_fill(s, x, y, w, h, ((uint32_t)dim << 24) | (ui->set.dark ? 0x000000u : 0xF4EDEDu));
}

/* Где на экране коробка с номером i при текущей прокрутке. */
static void box_rect(const Ui *ui, int i, int *x, int *y, int *w, int *h, float *tilt)
{
    float d = (float)i - ui->pos, ad = fabsf(d), near = clampf(ad, 0, 1);
    float sc = 1.0f - (1.0f - SIDE_SCALE) * near;
    float sw = (float)ui->box_w * SIDE_SCALE, gap = (float)ui->box_w * 0.13f;
    float off = near * ((float)ui->box_w * 0.5f + sw * 0.5f + gap) + (ad > 1 ? (ad - 1) * (sw + gap) : 0);
    float cx = (float)ui->W / 2 + (d < 0 ? -off : off);

    *w = (int)((float)ui->box_w * sc);
    *h = (int)((float)ui->box_h * sc);
    *x = (int)(cx - (float)*w / 2);
    *y = ui->box_top + (ui->box_h - *h) / 2;
    *tilt = clampf(d, -1, 1);
}

/* ---------- экраны ---------- */

static void draw_title(Ui *ui, const Game *g, int ty, int arrows)
{
    Image *s = ui->scr;
    char up[160];
    int cw = font_big.h + 4 + font_small.h, ay = ty + (cw - icon_chevron.h * ui->s) / 2;
    int max_w = ui->W - 2 * (ui->m + icon_chevron.w * ui->s + 6);

    utf8_upper(up, sizeof(up), g->name);
    text_fit(s, ui->W / 2, ty, max_w, up, ui->th->text, 1);
    utf8_upper(up, sizeof(up), g->vendor[0] && strcmp(g->vendor, "Unknown") ? g->vendor : g->profile);
    text_fit(s, ui->W / 2, ty + font_big.h + 4, max_w, up, ui->th->sub, 0);
    if (arrows) {
        draw_icon(s, &icon_chevron, ui->m + 2, ay, ui->s, ui->sel > 0 ? ui->th->sub : ui->th->divider, 0);
        draw_icon(s, &icon_chevron, ui->W - ui->m - 2 - icon_chevron.w * ui->s, ay, ui->s,
                  ui->sel < ui->lib.count - 1 ? ui->th->sub : ui->th->divider, 1);
    }
}

static void draw_counter(Ui *ui, int cur, int total)
{
    char buf[24];

    snprintf(buf, sizeof(buf), "%d/%d", cur, total);
    gfx_text(ui->scr, &font_small, ui->W - ui->m - gfx_text_width(&font_small, buf, 1), ui->m - 2, buf,
             ui->th->sub, 1);
}

static void draw_play(Ui *ui, uint32_t now)
{
    Image *s = ui->scr;
    int order, i, n = ui->lib.count;
    float launch = ui->launch_t0 ? (float)(now - ui->launch_t0) / LAUNCH_MS : 0;

    if (!n) {
        int x = (ui->W - ui->box_w) / 2;
        rrect(s, x, ui->box_top, ui->box_w, ui->box_h, 3, ui->th->tab_hi);
        draw_icon(s, &icon_folder, (ui->W - icon_folder.w * 2 * ui->s) / 2,
                  ui->box_top + (ui->box_h - icon_folder.h * 2 * ui->s) / 2, 2 * ui->s, ui->th->sub, 0);
        /* путь — единственная подсказка, без него пустую полку не наполнить */
        text_fit(s, ui->W / 2, ui->title_y + 6, ui->W - 2 * ui->m, ui->lib.dir, ui->th->sub, 0);
        return;
    }

    for (order = 4; order >= 0; order--) {
        for (i = 0; i < n; i++) {
            int x, y, w, h, dist = (int)(fabsf((float)i - ui->pos) + 0.5f);
            float tilt;

            if (dist != order)
                continue;
            box_rect(ui, i, &x, &y, &w, &h, &tilt);
            if (x > ui->W || x + w < 0)
                continue;
            if (i == ui->sel) {
                float out = 0;
                if (ui->launch_t0) {
                    out = clampf(launch / 0.3f, 0, 1);
                    out = 1 - (1 - out) * (1 - out);
                    x -= (int)(out * (float)w * 0.2f);
                } else {
                    y += (int)(1.5f * sinf((float)now / 650.0f));   /* коробка чуть «дышит» */
                }
                if (out > 0)   /* диск выезжает из коробки вправо */
                    gfx_disc(s, x + w / 2 + (int)(out * (float)w * 0.6f), y + h / 2, w * 46 / 100,
                             (float)(now - ui->launch_t0) / 1000.0f * (0.5f + launch * 4), 255);
            }
            draw_box(ui, x, y, w, h, &ui->lib.games[i], tilt, dist >= 2 ? 60 : 0, now);
        }
    }
    draw_counter(ui, ui->sel + 1, n);
    draw_title(ui, &ui->lib.games[ui->sel], ui->title_y, 1);
}

static void grid_cell(const Ui *ui, int i, int *x, int *y)
{
    int row = i / ui->cols, col = i % ui->cols;
    int total_w = ui->cols * ui->cell_w + (ui->cols - 1) * ui->gap;

    *x = (ui->W - total_w) / 2 + col * (ui->cell_w + ui->gap);
    *y = ui->top_min + 3 + (int)(((float)row - ui->grid_scroll) * (float)(ui->cell_h + ui->gap));
}

static void draw_library(Ui *ui, uint32_t now)
{
    Image *s = ui->scr;
    int i, bottom = ui->lib_title_y - 4;

    if (!ui->lib.count) {
        draw_play(ui, now);
        return;
    }
    for (i = 0; i < ui->lib.count; i++) {
        int x, y, lift = i == ui->sel ? 2 : 0;
        grid_cell(ui, i, &x, &y);
        if (y + ui->cell_h < ui->top_min - 4 || y > bottom)
            continue;
        if (i == ui->sel && !ui->focus_tabs)
            rrect(s, x - 3, y - 3 - lift, ui->cell_w + 6, ui->cell_h + 6, 2, ui->th->accent);
        draw_box(ui, x, y - lift, ui->cell_w, ui->cell_h, &ui->lib.games[i], 0, 0, now);
    }
    /* сетка прокручивается под заголовок и шапку — подрезаем фоном */
    draw_background(ui, 0, ui->top_min - 2);
    draw_background(ui, bottom, ui->H);
    draw_counter(ui, ui->sel + 1, ui->lib.count);
    draw_title(ui, &ui->lib.games[ui->sel], ui->lib_title_y, 0);
}

static void draw_switch(Ui *ui, int x, int y, int on)
{
    int h = 9 * ui->s, w = h * 2;

    rrect(ui->scr, x, y, w, h, 2 * ui->s, on ? ui->th->accent : ui->th->divider);
    rrect(ui->scr, on ? x + w - h + ui->s : x + ui->s, y + ui->s, h - 2 * ui->s, h - 2 * ui->s, ui->s, 0xFFFFFFFFu);
}

/* Иконка «пикселей»: крупная или мелкая шахматка. */
static void draw_pixels_icon(Ui *ui, int x, int y, int big, uint32_t c)
{
    int cell = big ? 3 : 1, i, j;

    for (j = 0; j < 11 / cell; j++)
        for (i = 0; i < 11 / cell; i++)
            if ((i + j) % 2 == 0)
                gfx_fill(ui->scr, x + i * cell * ui->s, y + j * cell * ui->s, cell * ui->s, cell * ui->s, c);
}

/* Иконка масштабирования игр: чёткая «лесенка» или сглаженная. */
static void draw_sharp_icon(Ui *ui, int x, int y, int sharp, uint32_t c)
{
    int i;

    for (i = 0; i < 5; i++) {
        gfx_fill(ui->scr, x + i * 2 * ui->s, y + (8 - i * 2) * ui->s, 2 * ui->s, (3 + i * 2) * ui->s, c);
        if (!sharp)
            gfx_fill(ui->scr, x + i * 2 * ui->s, y + (7 - i * 2) * ui->s, 2 * ui->s, ui->s,
                     (c & 0x00FFFFFFu) | 0x60000000u);
    }
}

static int settings_row_h(const Ui *ui)
{
    return 11 * ui->s + 10;
}

static int settings_y0(const Ui *ui)
{
    return ui->top_min + (ui->bottom_max - ui->top_min - SET_COUNT * settings_row_h(ui)) / 2;
}

static void draw_settings(Ui *ui)
{
    Image *s = ui->scr;
    int row_h = settings_row_h(ui), rows = SET_COUNT, w = imin(ui->W - 4 * ui->m, 150 * ui->s), i;
    int x = (ui->W - w) / 2, y0 = settings_y0(ui);

    for (i = 0; i < rows; i++) {
        int y = y0 + i * row_h, iy = y + 5, on = 0;
        uint32_t ic = ui->th->text;

        if (i == ui->set_sel && !ui->focus_tabs)
            rrect(s, x, y, w, row_h - 2, 3, ui->th->tab_hi);
        switch (i) {
        case SET_THEME:
            on = ui->set.dark;
            draw_icon(s, on ? &icon_moon : &icon_sun, x + 8, iy, ui->s, ic, 0);
            break;
        case SET_PIXELS:
            on = ui->set.big_pixels;
            draw_pixels_icon(ui, x + 8, iy, on, ic);
            break;
        case SET_SHARP:
            on = ui->set.sharp;
            draw_sharp_icon(ui, x + 8, iy, on, ic);
            break;
        }
        draw_switch(ui, x + w - 8 - 18 * ui->s, y + (row_h - 2 - 9 * ui->s) / 2, on);
    }
}

static void draw_tabs(Ui *ui)
{
    static const Icon *icons[TAB_COUNT] = { &icon_list, &icon_pad, &icon_gear };
    Image *s = ui->scr;
    int y0 = ui->H - ui->tab_h, cw = ui->W / TAB_COUNT, i;

    gfx_fill(s, ui->m * 2, y0, ui->W - ui->m * 4, 1, ui->th->divider);
    for (i = 0; i < TAB_COUNT; i++) {
        const Icon *ic = icons[i];
        int cx = cw * i + cw / 2, iw = ic->w * ui->s, ih = ic->h * ui->s;
        int hw = imin(cw - 8, iw * 3), hh = ui->tab_h - 8;

        if (i == ui->tab) {
            rrect(s, cx - hw / 2, y0 + 4, hw, hh, 3, ui->th->tab_hi);
            if (ui->focus_tabs)
                gfx_fill(s, cx - iw / 2, y0 + 4 + hh - 3, iw, 2, ui->th->accent);
        }
        draw_icon(s, ic, cx - iw / 2, y0 + (ui->tab_h - ih) / 2 + 1, ui->s,
                  i == ui->tab ? ui->th->accent : ui->th->sub, 0);
    }
    /* значок подключённого геймпада — мигает при подключении */
    if (ui->pad_connected && (ui->pad_flash == 0 || (platform_ticks_ms() / 200) % 2 == 0 ||
                              platform_ticks_ms() > ui->pad_flash))
        draw_icon(s, &icon_pad, ui->m, ui->m - 1, 1, ui->th->sub, 0);
}

static void draw_launch_overlay(Ui *ui, uint32_t now)
{
    float launch = (float)(now - ui->launch_t0) / LAUNCH_MS;
    unsigned a = (unsigned)(clampf((launch - 0.4f) / 0.35f, 0, 1) * 255);
    int r = imin(ui->W, ui->H) / 6, i;

    if (launch <= 0.4f)
        return;
    gfx_fill(ui->scr, 0, 0, ui->W, ui->H, a << 24);
    if (launch > 0.55f) {
        int da = (int)(clampf((launch - 0.55f) / 0.2f, 0, 1) * 255);
        gfx_disc(ui->scr, ui->W / 2, ui->H / 2 - r / 3, r, (float)(now - ui->launch_t0) / 160.0f, da);
        /* три точки «загрузки» вместо надписи */
        for (i = 0; i < 3; i++) {
            int on = (int)(now / 220) % 3 == i;
            gfx_fill(ui->scr, ui->W / 2 - 8 + i * 7, ui->H / 2 + r * 2 / 3 + 6, 3, 3,
                     ((uint32_t)(da * (on ? 255 : 90) / 255) << 24) | 0xEDE8F2u);
        }
    }
}

/* Запуск игры на Java-машине. Возвращает 1, если закрыли окно и надо выйти совсем. */
static int run_game(Ui *ui, Game *g)
{
    ScaleMode mode = ui->cli_scale != SCALE_AUTO ? ui->cli_scale : ui->set.sharp ? SCALE_INTEGER : SCALE_AUTO;
    char err[200];
    MidpResult r = midp_run(g->path, g->base, mode, err, sizeof(err));
    uint32_t until;

    if (r == MIDP_QUIT)
        return 1;
    if (r != MIDP_ERROR)
        return 0;

    /* не запустилась: иконка игры, красный крест, без слов; причина — в журнал */
    BT_LOG("%s: %s\n", g->path, err);
    platform_set_screen(240, 320, mode);
    until = platform_ticks_ms() + 5000;
    while ((int32_t)(platform_ticks_ms() - until) < 0) {
        Image *s = platform_screen();
        Event ev;
        int k, y = 110, i;

        while (platform_poll(&ev)) {
            if (ev.type == EV_QUIT)
                return 1;
            if (ev.type == EV_BACK || ev.type == EV_KEY_DOWN || ev.type == EV_POINTER_DOWN)
                until = platform_ticks_ms();
        }
        gfx_fill(s, 0, 0, s->w, s->h, 0xFF0B0A10u);
        if (g->icon.px) {
            k = imax(1, 72 / imax(g->icon.w, g->icon.h));
            gfx_blit_scaled(s, &g->icon, (s->w - g->icon.w * k) / 2, y - g->icon.h * k / 2, g->icon.w * k,
                            g->icon.h * k, 120);
        }
        for (i = -14; i <= 14; i++) {
            gfx_fill(s, s->w / 2 + i - 2, y + 70 + i - 2, 5, 5, 0xFFE2483Du);
            gfx_fill(s, s->w / 2 + i - 2, y + 70 - i - 2, 5, 5, 0xFFE2483Du);
        }
        draw_icon(s, &icon_back, 12, s->h - 20, 1, 0xFF8F8899u, 0);
        platform_present();
        platform_sleep_ms(16);
    }
    return 0;
}

/* ---------- логика ---------- */

static void select_game(Ui *ui, int idx)
{
    if (!ui->lib.count)
        return;
    ui->sel = idx < 0 ? 0 : idx >= ui->lib.count ? ui->lib.count - 1 : idx;
}

/* Подгружаем по одной обложке за кадр, начиная с ближайших к выбранной. */
static void load_covers(Ui *ui, uint32_t now)
{
    int d;

    for (d = 0; d < ui->lib.count; d++) {
        Game *g = &ui->lib.games[d];
        if (g->cover_ready && (d < ui->sel - KEEP_COVERS || d > ui->sel + KEEP_COVERS)) {
            image_free(&g->cover);
            image_free(&g->cover_small);
            g->cover_ready = 0;
        }
    }
    for (d = 0; d <= KEEP_COVERS; d++) {
        int cand[2] = { ui->sel - d, ui->sel + d }, c;
        for (c = 0; c < 2; c++) {
            Game *g;
            if (cand[c] < 0 || cand[c] >= ui->lib.count)
                continue;
            g = &ui->lib.games[cand[c]];
            if (!g->cover_ready) {
                library_load_cover(&ui->lib, g, ui->cover_w, ui->cover_h);
                image_free(&g->cover_small);
                if (g->cover.px)
                    g->cover_small = image_resize(&g->cover, ui->small_w, ui->small_h);
                g->cover_time = now;
                return;
            }
        }
    }
}

static void toggle_setting(Ui *ui, int which)
{
    if (which == SET_THEME)
        ui->set.dark = !ui->set.dark;
    else if (which == SET_PIXELS)
        ui->set.big_pixels = !ui->set.big_pixels;
    else
        ui->set.sharp = !ui->set.sharp;
    settings_save(ui);
    layout(ui);
}

static void on_key(Ui *ui, int key, uint32_t now)
{
    if (key == KEY_STAR || key == KEY_POUND) {           /* LB / RB — соседняя вкладка */
        ui->tab = (ui->tab + (key == KEY_POUND ? 1 : TAB_COUNT - 1)) % TAB_COUNT;
        return;
    }
    if (ui->focus_tabs) {
        if (key == KEY_LEFT && ui->tab > 0)
            ui->tab--;
        else if (key == KEY_RIGHT && ui->tab < TAB_COUNT - 1)
            ui->tab++;
        else if (key == KEY_UP || key == KEY_FIRE)
            ui->focus_tabs = 0;
        return;
    }
    switch (ui->tab) {
    case TAB_PLAY:
        if (key == KEY_LEFT || key == KEY_NUM0 + 4) {
            select_game(ui, ui->sel - 1);
            ui->held = -1;
        } else if (key == KEY_RIGHT || key == KEY_NUM0 + 6) {
            select_game(ui, ui->sel + 1);
            ui->held = 1;
        } else if (key == KEY_DOWN || key == KEY_NUM0 + 8) {
            ui->focus_tabs = 1;
        } else if ((key == KEY_FIRE || key == KEY_SOFT_LEFT || key == KEY_NUM0 + 5) && ui->lib.count) {
            ui->launch_t0 = now;
        }
        break;
    case TAB_LIBRARY:
        if (key == KEY_LEFT) {
            select_game(ui, ui->sel - 1);
            ui->held = -1;
        } else if (key == KEY_RIGHT) {
            select_game(ui, ui->sel + 1);
            ui->held = 1;
        } else if (key == KEY_UP) {
            select_game(ui, ui->sel - ui->cols);
        } else if (key == KEY_DOWN) {
            if (ui->sel + ui->cols < ui->lib.count)
                select_game(ui, ui->sel + ui->cols);
            else
                ui->focus_tabs = 1;
        } else if (key == KEY_FIRE && ui->lib.count) {
            ui->tab = TAB_PLAY;
            ui->pos = (float)ui->sel;
        }
        break;
    case TAB_SETTINGS:
        if (key == KEY_UP && ui->set_sel > 0)
            ui->set_sel--;
        else if (key == KEY_DOWN)
            ui->set_sel < SET_COUNT - 1 ? (void)ui->set_sel++ : (void)(ui->focus_tabs = 1);
        else if (key == KEY_FIRE || key == KEY_LEFT || key == KEY_RIGHT)
            toggle_setting(ui, ui->set_sel);
        break;
    }
}

static void on_pointer(Ui *ui, int px, int py, uint32_t now)
{
    int i;

    if (py >= ui->H - ui->tab_h) {
        ui->tab = imin(TAB_COUNT - 1, px * TAB_COUNT / ui->W);
        ui->focus_tabs = 0;
        return;
    }
    if (ui->tab == TAB_PLAY) {
        int best = -1;
        if (py >= ui->title_y && px < ui->W / 4) {
            select_game(ui, ui->sel - 1);
            return;
        }
        if (py >= ui->title_y && px > ui->W * 3 / 4) {
            select_game(ui, ui->sel + 1);
            return;
        }
        for (i = 0; i < ui->lib.count; i++) {
            int x, y, w, h;
            float tilt;
            box_rect(ui, i, &x, &y, &w, &h, &tilt);
            if (px >= x && px < x + w && py >= y && py < y + h &&
                (best < 0 || fabsf((float)i - ui->pos) < fabsf((float)best - ui->pos)))
                best = i;
        }
        if (best >= 0 && best == ui->sel)
            ui->launch_t0 = now;
        else if (best >= 0)
            select_game(ui, best);
    } else if (ui->tab == TAB_LIBRARY) {
        for (i = 0; i < ui->lib.count; i++) {
            int x, y;
            grid_cell(ui, i, &x, &y);
            if (px >= x && px < x + ui->cell_w && py >= y && py < y + ui->cell_h) {
                if (i == ui->sel) {
                    ui->tab = TAB_PLAY;
                    ui->pos = (float)i;
                }
                select_game(ui, i);
            }
        }
    } else {
        int row_h = settings_row_h(ui), y0 = settings_y0(ui);
        int row = (py - y0) / row_h;
        if (py >= y0 && row < SET_COUNT) {
            ui->set_sel = row;
            toggle_setting(ui, row);
        }
    }
}

int launcher_run(const char *games_dir, PlatformConfig *cfg)
{
    static Ui ui;
    uint32_t prev;
    int running = 1, dw = 0, dh = 0;

    memset(&ui, 0, sizeof(ui));
    ui.cli_scale = cfg->scale;
    ui.tab = TAB_PLAY;
    settings_load(&ui);
    cfg->width = 400;
    cfg->height = 240;
    cfg->scale = SCALE_INTEGER;
    if (platform_init(cfg))
        return 1;
    library_scan(&ui.lib, games_dir);
    if (!ui.lib.dir[0])
        snprintf(ui.lib.dir, sizeof(ui.lib.dir), "%s", games_dir);
    layout(&ui);
    platform_display_size(&dw, &dh);
    prev = platform_ticks_ms();

    while (running) {
        uint32_t now = platform_ticks_ms();
        float dt = (float)(now - prev) / 1000.0f, row;
        Event ev;
        int cw, ch, visible_rows;

        prev = now;
        platform_display_size(&cw, &ch);
        if (cw != dw || ch != dh) {
            dw = cw;
            dh = ch;
            layout(&ui);
        }

        while (platform_poll(&ev)) {
            if (ev.type == EV_QUIT)
                running = 0;
            if (ui.launch_t0)
                continue;
            if (ev.type == EV_BACK) {
                /* «назад»: из вкладок — на полку, с полки — выход */
                if (ui.tab != TAB_PLAY || ui.focus_tabs) {
                    ui.tab = TAB_PLAY;
                    ui.focus_tabs = 0;
                } else {
                    running = 0;
                }
            } else if (ev.type == EV_PAD_ADDED) {
                ui.pad_connected = 1;
                ui.pad_flash = now + 1600;
            } else if (ev.type == EV_KEY_DOWN) {
                on_key(&ui, ev.key, now);
                ui.held_since = ui.last_repeat = (int)now;
            } else if (ev.type == EV_KEY_UP && (ev.key == KEY_LEFT || ev.key == KEY_RIGHT ||
                                                ev.key == KEY_NUM0 + 4 || ev.key == KEY_NUM0 + 6)) {
                ui.held = 0;
            } else if (ev.type == EV_POINTER_DOWN) {
                on_pointer(&ui, ev.x, ev.y, now);
            }
        }

        /* автоповтор при удержании — листать длинную полку */
        if (ui.held && !ui.launch_t0 && !ui.focus_tabs && ui.tab != TAB_SETTINGS &&
            (int)now - ui.held_since > REPEAT_DELAY && (int)now - ui.last_repeat > REPEAT_RATE) {
            select_game(&ui, ui.sel + ui.held);
            ui.last_repeat = (int)now;
        }
        ui.pos += ((float)ui.sel - ui.pos) * clampf(dt * 14.0f, 0, 1);
        if (fabsf((float)ui.sel - ui.pos) < 0.002f)
            ui.pos = (float)ui.sel;
        /* сетка библиотеки прокручивается так, чтобы выбранный ряд был виден */
        visible_rows = imax(1, (ui.lib_title_y - ui.top_min) / (ui.cell_h + ui.gap));
        row = (float)(ui.sel / ui.cols);
        if (row < ui.grid_scroll)
            ui.grid_scroll += (row - ui.grid_scroll) * clampf(dt * 14.0f, 0, 1);
        else if (row > ui.grid_scroll + (float)visible_rows - 1)
            ui.grid_scroll += (row - (float)visible_rows + 1 - ui.grid_scroll) * clampf(dt * 14.0f, 0, 1);

        load_covers(&ui, now);
        draw_background(&ui, 0, ui.H);
        if (ui.tab == TAB_PLAY)
            draw_play(&ui, now);
        else if (ui.tab == TAB_LIBRARY)
            draw_library(&ui, now);
        else
            draw_settings(&ui);
        draw_tabs(&ui);
        if (ui.launch_t0)
            draw_launch_overlay(&ui, now);
        platform_present();

        if (ui.launch_t0 && now - ui.launch_t0 >= LAUNCH_MS) {
            if (run_game(&ui, &ui.lib.games[ui.sel]))
                running = 0;
            ui.launch_t0 = 0;
            ui.held = 0;
            layout(&ui);
            prev = platform_ticks_ms();
        }
        /* 30 кадров в секунду хватает для плавного меню и вдвое экономит процессор */
        platform_sleep_ms(now + 33 > platform_ticks_ms() ? now + 33 - platform_ticks_ms() : 1);
    }
    library_free(&ui.lib);
    platform_shutdown();
    return 0;
}
