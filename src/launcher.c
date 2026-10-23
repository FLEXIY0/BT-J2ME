#define _DEFAULT_SOURCE
#include "launcher.h"
#include "library.h"
#include "midp.h"
#include "compat.h"
#include "png.h"
#include "audio.h"
#include "cursor.h"
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
#define LAUNCH_MS    (ui->fx == FX_SHATTER ? 3300 : 1700)   /* длительность анимации запуска */
#define KEEP_COVERS  24     /* обложки дальше этого от выбранной выгружаем — память важнее */
#define BOX_ASPECT   0.72f  /* коробка как у игр для приставок: ширина / высота */
#define SIDE_SCALE   0.84f

enum { TAB_LIBRARY, TAB_PLAY, TAB_SETTINGS, TAB_COUNT };
enum { SET_THEME, SET_PIXELS, SET_SHARP, SET_SOUND, SET_COUNT };

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
    int sound;       /* звук игр и лаунчера */
} Settings;

typedef struct {
    Library      lib;
    Image       *scr;
    const Theme *th;
    Settings     set;
    char         set_path[1024];
    ScaleMode    cli_scale;
    int          cli_mute;      /* --mute: звук выключен, что бы ни стояло в настройках */
    int          W, H, s;                 /* виртуальный экран и масштаб иконок */
    int          m, tab_h, title_y, box_top, box_w, box_h, cover_w, cover_h, small_w, small_h;
    int          top_min, bottom_max, lib_title_y;   /* зона контента между счётчиком и вкладками */
    int          cell_w, cell_h, cols, gap;
    int          tab, focus_tabs;
    int          sel, set_sel;
    float        pos, grid_scroll;
    int          held, held_since, last_repeat;
    int          pad_connected;
    Image        snap;          /* кадр полки в момент запуска — из него строится анимация */
    /* редактор обложки: скриншот игры и рамка кадрирования (в пикселях скриншота) */
    int          ce, ce_n, ce_i, ce_drag;
    char         ce_files[48][64];
    Image        ce_img;
    float        ce_cx, ce_cy, ce_cw, ce_dx, ce_dy;
    int          fx;            /* вид анимации запуска */
    int          ptr_down, ptr_x0, ptr_y0, ptr_x, ptr_y;   /* касание: тап или свайп решаем при отпускании */
    uint32_t     pad_flash;
    /* свой курсор-рука: мышь (1) или левый стик геймпада (2), 0 — спрятан */
    int          cur_on, cur_press;
    float        cur_x, cur_y;
    uint32_t     cur_t;
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

static const Icon icon_speaker = { 11, 11, {
    "....#......", "...##...#..", "..###....#.", "#####..#..#", "#####...#.#",
    "#####...#.#", "#####...#.#", "#####..#..#", "..###....#.", "...##...#..", "....#......" } };
static const Icon icon_speaker_off = { 11, 11, {
    "....#......", "...##......", "..###......", "#####.#...#", "#####..#.#.",
    "#####...#..", "#####..#.#.", "#####.#...#", "..###......", "...##......", "....#......" } };
static const Icon icon_picture = { 11, 9, {
    "###########", "#.........#", "#......##.#", "#......##.#", "#..#......#",
    "#.###...#.#", "#######.###", "###########", "..........." } };
static const Icon icon_plus = { 9, 9, {
    "....#....", "....#....", "....#....", "....#....", "#########",
    "....#....", "....#....", "....#....", "....#...." } };
static const Icon icon_minus = { 9, 9, {
    ".........", ".........", ".........", ".........", "#########",
    ".........", ".........", ".........", "........." } };
static const Icon icon_check = { 9, 9, {
    ".........", "........#", ".......##", "#.....##.", "##...##..",
    ".##.##...", "..###....", "...#.....", "........." } };
static const Icon icon_cross = { 9, 9, {
    "#.......#", ".#.....#.", "..#...#..", "...#.#...", "....#....",
    "...#.#...", "..#...#..", ".#.....#.", "#.......#" } };
static const Icon icon_trash = { 9, 9, {
    "...###...", "#########", ".#.....#.", ".#.#.#.#.", ".#.#.#.#.",
    ".#.#.#.#.", ".#.#.#.#.", ".#.....#.", "..#####.." } };
static const Icon icon_camera = { 11, 9, {
    "...###.....", "###########", "#...###...#", "#..#...#..#", "#..#...#..#",
    "#..#...#..#", "#...###...#", "###########", "..........." } };

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
    ui->set.sound = 1;
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
        else if (!strcmp(key, "sound"))
            ui->set.sound = val != 0;
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
    fprintf(f, "dark=%d\nbig_pixels=%d\nsharp=%d\nsound=%d\n", ui->set.dark, ui->set.big_pixels, ui->set.sharp,
            ui->set.sound);
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

static void draw_add_tile(Ui *ui, int x, int y, int w, int h);
static void ce_open(Ui *ui);

static void draw_play(Ui *ui, uint32_t now)
{
    Image *s = ui->scr;
    int order, i, n = ui->lib.count;

    if (!n && platform_can_import()) {   /* пустая полка на телефоне — сразу «+» */
        draw_add_tile(ui, (ui->W - ui->box_w) / 2, ui->box_top, ui->box_w, ui->box_h);
        return;
    }
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
            if (i == ui->sel && !ui->launch_t0)
                y += (int)(1.5f * sinf((float)now / 650.0f));   /* коробка чуть «дышит» */
            draw_box(ui, x, y, w, h, &ui->lib.games[i], tilt, dist >= 2 ? 60 : 0, now);
        }
    }
    draw_counter(ui, ui->sel + 1, n);
    /* слева вверху — «картинка»: сменить обложку на кадр из игры */
    draw_icon(s, &icon_picture, ui->m, ui->m - 1, 1, ui->th->sub, 0);
    draw_title(ui, &ui->lib.games[ui->sel], ui->title_y, 1);
}

static void grid_cell(const Ui *ui, int i, int *x, int *y)
{
    int row = i / ui->cols, col = i % ui->cols;
    int total_w = ui->cols * ui->cell_w + (ui->cols - 1) * ui->gap;

    *x = (ui->W - total_w) / 2 + col * (ui->cell_w + ui->gap);
    *y = ui->top_min + 3 + (int)(((float)row - ui->grid_scroll) * (float)(ui->cell_h + ui->gap));
}

/* Плитка «+»: добавить игры с телефона (системный выбор файлов). */
static void draw_add_tile(Ui *ui, int x, int y, int w, int h)
{
    int t = imax(2, ui->s * 2), len = imin(w, h) / 3;

    rrect(ui->scr, x, y, w, h, 3, ui->th->tab_hi);
    gfx_fill(ui->scr, x + (w - len) / 2, y + (h - t) / 2, len, t, ui->th->sub);
    gfx_fill(ui->scr, x + (w - t) / 2, y + (h - len) / 2, t, len, ui->th->sub);
}

static void draw_library(Ui *ui, uint32_t now)
{
    Image *s = ui->scr;
    int i, bottom = ui->lib_title_y - 4;

    if (!ui->lib.count) {
        draw_play(ui, now);
        return;
    }
    if (platform_can_import()) {
        int x, y;
        grid_cell(ui, ui->lib.count, &x, &y);
        if (y + ui->cell_h >= ui->top_min - 4 && y <= bottom)
            draw_add_tile(ui, x, y, ui->cell_w, ui->cell_h);
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
        case SET_SOUND:
            on = ui->set.sound;
            draw_icon(s, on ? &icon_speaker : &icon_speaker_off, x + 8, iy, ui->s, ic, 0);
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
        draw_icon(s, &icon_pad, ui->m + icon_picture.w + 6, ui->m - 1, 1, ui->th->sub, 0);   /* правее значка обложки */
}

/* ---------- анимации запуска ---------- */

enum { FX_CRT, FX_WARP, FX_DRIVE, FX_SHATTER, FX_COUNT };

static uint32_t fx_seed;
static unsigned fx_rand(void)
{
    fx_seed = fx_seed * 1103515245u + 12345u;
    return (fx_seed >> 16) & 0x7FFF;
}

static float fx_frand(void) { return (float)fx_rand() / 32767.0f; }

static void px_blend(Image *d, int x, int y, uint32_t c, unsigned a)
{
    if (x < 0 || y < 0 || x >= d->w || y >= d->h || !a)
        return;
    d->px[y * d->w + x] = gfx_mix(d->px[y * d->w + x], c, a > 255 ? 255 : a);
}

static void fx_line(Image *d, float x0, float y0, float x1, float y1, uint32_t c, unsigned a)
{
    float dx = x1 - x0, dy = y1 - y0, n = fmaxf(fabsf(dx), fabsf(dy)), i;

    if (n < 1)
        n = 1;
    for (i = 0; i <= n; i++)    /* к хвосту штрих гаснет */
        px_blend(d, (int)(x0 + dx * i / n), (int)(y0 + dy * i / n), c, (unsigned)((float)a * (0.25f + 0.75f * i / n)));
}

static float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

/* Кусок кадра src (sx,sy,sw,sh) растянуть в dst (dx,dy,dw,dh) ближайшим соседом. */
static void fx_stretch(Image *dst, const Image *src, float sx, float sy, float sw, float sh,
                       int dx, int dy, int dw, int dh)
{
    int x, y;

    for (y = 0; y < dh; y++) {
        int py = dy + y, ty = (int)(sy + (float)y * sh / (float)dh);
        if (py < 0 || py >= dst->h || ty < 0 || ty >= src->h)
            continue;
        for (x = 0; x < dw; x++) {
            int px = dx + x, tx = (int)(sx + (float)x * sw / (float)dw);
            if (px >= 0 && px < dst->w && tx >= 0 && tx < src->w)
                dst->px[py * dst->w + px] = src->px[ty * src->w + tx];
        }
    }
}

/* То же, но пиксели источника внутри прямоугольника ex (коробка) пропускаются — плитка
 * полки, задевшая коробку, падает без её кусочка. */
static void fx_stretch_ex(Image *dst, const Image *src, int sx, int sy, int sw, int sh,
                          int dx, int dy, int dw, int dh, int ex, int ey, int ew, int eh)
{
    int x, y;

    for (y = 0; y < dh; y++) {
        int py = dy + y, ty = sy + y * sh / dh;
        if (py < 0 || py >= dst->h || ty < 0 || ty >= src->h)
            continue;
        for (x = 0; x < dw; x++) {
            int px = dx + x, tx = sx + x * sw / dw;
            if (px < 0 || px >= dst->w || tx < 0 || tx >= src->w)
                continue;
            if (tx >= ex && tx < ex + ew && ty >= ey && ty < ey + eh)
                continue;
            dst->px[py * dst->w + px] = src->px[ty * src->w + tx];
        }
    }
}

/* Мозаика: блоки b×b заливаются средним цветом. */
static void fx_mosaic(Image *d, int b)
{
    int bx, by, x, y;

    if (b < 2)
        return;
    for (by = 0; by < d->h; by += b)
        for (bx = 0; bx < d->w; bx += b) {
            unsigned r = 0, g = 0, bl = 0, n = 0;
            for (y = by; y < by + b && y < d->h; y++)
                for (x = bx; x < bx + b && x < d->w; x++, n++) {
                    uint32_t c = d->px[y * d->w + x];
                    r += (c >> 16) & 255;
                    g += (c >> 8) & 255;
                    bl += c & 255;
                }
            gfx_fill(d, bx, by, b, b, 0xFF000000u | ((r / n) << 16) | ((g / n) << 8) | (bl / n));
        }
}

static void fx_flash(Image *d, float a)
{
    if (a > 0)
        gfx_fill(d, 0, 0, d->w, d->h, ((uint32_t)(clampf(a, 0, 1) * 255) << 24) | 0xFFFFFF);
}

/* Весь экран анимации запуска; t — 0..1. */
static void launch_fx(Ui *ui, float t, uint32_t now)
{
    Image *s = ui->scr, *src = &ui->snap;
    int bx, by, bw, bh;
    float tilt, cx, cy;

    box_rect(ui, ui->sel, &bx, &by, &bw, &bh, &tilt);
    cx = (float)bx + (float)bw / 2;
    cy = (float)by + (float)bh / 2;
    switch (ui->fx) {
    case FX_CRT: {
        /* коробка наезжает на весь экран → мозаика → кинескоп схлопывается в полоску и точку */
        float z = smooth(t / 0.35f);
        float sx = (float)bx * (1 - z), sy = (float)by * (1 - z);
        float sw = (float)bw + ((float)ui->W - (float)bw) * z * 0, sh;
        int lineh, linew, y;
        Image frame = image_new(ui->W, ui->H);
        if (!frame.px)
            break;
        /* видимое окно источника сужается к коробке, сохраняя пропорции экрана */
        sw = (float)ui->W + ((float)bw * 0.9f - (float)ui->W) * z;
        sh = sw * (float)ui->H / (float)ui->W;
        sx = (float)ui->W / 2 + (cx - (float)ui->W / 2) * z - sw / 2;
        sy = (float)ui->H / 2 + (cy - (float)ui->H / 2) * z - sh / 2;
        fx_stretch(&frame, src, sx, sy, sw, sh, 0, 0, ui->W, ui->H);
        if (t > 0.3f)
            fx_mosaic(&frame, 1 + (int)(smooth((t - 0.3f) / 0.25f) * 9));
        gfx_fill(s, 0, 0, ui->W, ui->H, 0xFF000000u);
        if (t < 0.55f) {
            gfx_blit(s, &frame, 0, 0, 255);
        } else {
            /* схлопывание по вертикали, потом по горизонтали; полоска светится */
            float v = smooth((t - 0.55f) / 0.2f), h2 = smooth((t - 0.75f) / 0.15f);
            lineh = imax(1, (int)((float)ui->H * (1 - v)));
            linew = imax(1, (int)((float)ui->W * (1 - h2)));
            fx_stretch(s, &frame, (float)(ui->W - linew) / 2, 0, (float)linew, (float)ui->H,
                       (ui->W - linew) / 2, (ui->H - lineh) / 2, linew, lineh);
            for (y = (ui->H - lineh) / 2; y < (ui->H + lineh) / 2; y++)
                gfx_fill(s, (ui->W - linew) / 2, y, linew, 1, ((uint32_t)(v * 230) << 24) | 0xF4F0FF);
            if (t > 0.88f) {            /* последняя точка гаснет */
                int r = imax(1, (int)(3 * (1 - smooth((t - 0.88f) / 0.12f))));
                gfx_fill(s, 0, 0, ui->W, ui->H, 0xFF000000u);
                gfx_fill(s, ui->W / 2 - r, ui->H / 2 - r, r * 2, r * 2, 0xFFF4F0FFu);
            }
        }
        image_free(&frame);
        break;
    }
    case FX_WARP: {
        /* гиперпрыжок: звёзды-штрихи из коробки, коробка улетает в точку, вспышка */
        float k = smooth(t / 0.7f), dark = smooth(t / 0.4f), scx = cx + ((float)ui->W / 2 - cx) * k;
        float scy = cy + ((float)ui->H / 2 - cy) * k, len = (float)(ui->W + ui->H);
        int i, n = 220;
        gfx_blit(s, src, 0, 0, 255);
        gfx_fill(s, 0, 0, ui->W, ui->H, ((uint32_t)(dark * 245) << 24) | 0x07060C);
        fx_seed = 77;
        for (i = 0; i < n; i++) {
            float a = fx_frand() * 6.2832f, d0 = fx_frand(), sp = 0.4f + fx_frand();
            float d = fmodf(d0 + t * t * 2.2f * sp, 1.0f), d2 = d * d * len, tail = d2 * (0.12f + 0.5f * k);
            unsigned al = (unsigned)(255 * clampf(d * 3, 0, 1) * clampf(t * 4, 0, 1));
            uint32_t c = i % 7 == 0 ? 0x9C8FF0 : i % 5 == 0 ? 0x7FD4FF : 0xF4F0FF;
            fx_line(s, scx + cosf(a) * (d2 - tail), scy + sinf(a) * (d2 - tail), scx + cosf(a) * d2,
                    scy + sinf(a) * d2, c, al);
        }
        {   /* коробка сжимается в точку */
            float sc = 1 - smooth(t / 0.6f);
            int w = (int)((float)bw * sc), h = (int)((float)bh * sc);
            if (w > 1 && h > 1)
                fx_stretch(s, src, (float)bx, (float)by, (float)bw, (float)bh, (int)(scx - w / 2), (int)(scy - h / 2), w, h);
        }
        fx_flash(s, t < 0.72f ? smooth((t - 0.6f) / 0.12f) : 1 - smooth((t - 0.72f) / 0.2f));
        if (t > 0.8f)
            gfx_fill(s, 0, 0, ui->W, ui->H, ((uint32_t)(smooth((t - 0.8f) / 0.2f) * 255) << 24));
        break;
    }
    case FX_DRIVE: {
        /* диск выскакивает, коробка падает; в центре он разгоняется с шлейфом и лучами */
        float up = smooth(t / 0.22f), fall = clampf((t - 0.15f) / 0.45f, 0, 1), go = smooth((t - 0.2f) / 0.3f);
        float dcx = cx + ((float)ui->W / 2 - cx) * go, dcy = cy - (float)bh * 0.55f * up + ((float)ui->H / 2 - (cy - (float)bh * 0.55f)) * go;
        float r = (float)bw * 0.46f * (1 + 0.5f * go), spin = t * t * 40.0f;
        int i, ghosts = 5;
        gfx_blit(s, src, 0, 0, 255);
        /* на месте коробки — фон, сама коробка падает вниз с ускорением */
        gfx_fill(s, bx - 4, by - 4, bw + 8, bh + 12, ui->th->bg_top);
        gfx_fill(s, 0, 0, ui->W, ui->H, ((uint32_t)(smooth((t - 0.25f) / 0.35f) * 235) << 24) | 0x0B0A10);
        if (t > 0.3f) {     /* лучи света за диском */
            float a0 = t * 3.0f;
            unsigned al = (unsigned)(smooth((t - 0.3f) / 0.3f) * 70);
            for (i = 0; i < 16; i++) {
                float a = a0 + (float)i * 6.2832f / 16, l = (float)ui->H;
                int w;
                for (w = -2; w <= 2; w++)
                    fx_line(s, dcx, dcy, dcx + cosf(a + w * 0.012f) * l, dcy + sinf(a + w * 0.012f) * l,
                            i % 2 ? 0x9C8FF0 : 0xF4F0FF, al);
            }
        }
        if (fall < 1) {
            int dy = (int)(fall * fall * (float)ui->H * 1.2f);
            fx_stretch(s, src, (float)bx, (float)by, (float)bw, (float)bh, bx, by + dy, bw, bh);
        }
        for (i = ghosts; i >= 0; i--)   /* шлейф: прошлые положения блика */
            gfx_disc(s, (int)dcx, (int)dcy, (int)r, spin - (float)i * 0.05f * (0.2f + t * 3), i ? 40 : 255);
        if (up < 1)     /* пока диск выходит, перед ним снова коробка */
            fx_stretch(s, src, (float)bx, (float)by, (float)bw, (float)bh, bx, by + (int)(fall * fall * (float)ui->H * 1.2f), bw, bh);
        fx_flash(s, t < 0.82f ? smooth((t - 0.68f) / 0.14f) : 1 - smooth((t - 0.82f) / 0.15f));
        if (t > 0.86f)
            gfx_fill(s, 0, 0, ui->W, ui->H, ((uint32_t)(smooth((t - 0.86f) / 0.14f) * 255) << 24));
        break;
    }
    default: {
        /* Запуск по шагам, чтобы каждое действие читалось:
         * 0–0.18   удар: коробка проседает, экран вздрагивает, от неё идёт ударная волна;
         * 0.03–0.42 полка осыпается плитками за волной, за коробкой разгораются лучи;
         * 0.4–0.5   коробка одна в темноте отрывается от полки;
         * 0.5–0.8   выплывает в центр, растёт и делает два оборота, замедляясь лицом к игроку;
         * 0.8–0.86  встаёт с лёгким покачиванием, по обложке пробегает блик (тут звучит аккорд);
         * 0.85–0.97 рассыпается в светящиеся частицы, они уплывают вверх;
         * 0.93–1    темнота; игра потом проявляется из неё сама (platform_fade_in). */
        static int buzzed;
        int b = imax(6, ui->W / 12), x, y, shx, shy;
        float maxd = sqrtf((float)(ui->W * ui->W + ui->H * ui->H)), wave = smooth((t - 0.02f) / 0.3f);
        float u = (float)imin(ui->W, ui->H) / 240.0f, quake = 1 - smooth(t / 0.18f);
        float go = smooth((t - 0.48f) / 0.3f), lift = smooth((t - 0.4f) / 0.1f);
        float spin = clampf((t - 0.5f) / 0.3f, 0, 1), ang = 6.2832f * 2 * spin * spin * (3 - 2 * spin);
        float settle = t > 0.8f ? sinf((t - 0.8f) / 0.07f * 3.1416f) * (1 - smooth((t - 0.8f) / 0.08f)) : 0;
        float press = t < 0.08f ? sinf(t / 0.08f * 3.1416f) : 0;
        float dis = clampf((t - 0.85f) / 0.1f, 0, 1);
        float ccx, ccy, k, glow, rays;

        shx = (int)(sinf(t * 140) * 5 * u * quake);
        shy = (int)(cosf(t * 110) * 4 * u * quake);
        if (t < 0.1f)
            buzzed = 0;
        else if (t > 0.8f && !buzzed) {     /* коробка встала — короткий толчок в руку */
            buzzed = 1;
            platform_vibrate(18);
        }
        gfx_fill(s, 0, 0, ui->W, ui->H, 0xFF0B0A10u);
        for (y = 0; y < ui->H; y += b)
            for (x = 0; x < ui->W; x += b) {
                float mx = (float)x + (float)b / 2, my = (float)y + (float)b / 2;
                float d = sqrtf((mx - cx) * (mx - cx) + (my - cy) * (my - cy)) / maxd;
                float delay, lt, vx, vy, ox, oy, sc;
                int sz;
                if (x >= bx && x + b <= bx + bw && y >= by && y + b <= by + bh)
                    continue;               /* плитка целиком внутри коробки — коробка рисуется отдельно */
                fx_seed = (uint32_t)(x * 7919 + y * 104729 + 1);
                vx = ((mx - cx) / maxd * 2 + (fx_frand() - 0.5f)) * 0.5f;    /* волна толкает от коробки */
                vy = -0.35f - fx_frand() * 0.5f;
                delay = 0.03f + d * 0.3f + fx_frand() * 0.03f;
                lt = (t - delay) / 0.3f;
                if (lt >= 1)
                    continue;
                if (lt < 0)
                    lt = 0;
                ox = vx * lt * (float)ui->W * 0.35f;
                oy = (vy * lt + 2.8f * lt * lt) * (float)ui->H * 0.3f;
                sc = 1 - lt * 0.5f;
                sz = imax(1, (int)((float)b * sc));
                fx_stretch_ex(s, src, x, y, b, b, (int)(mx + ox - (float)sz / 2) + shx,
                              (int)(my + oy - (float)sz / 2) + shy, sz, sz, bx, by, bw, bh);
                if (lt > 0)                 /* летящая плитка темнеет — уходит в глубину */
                    gfx_fill(s, (int)(mx + ox - (float)sz / 2) + shx, (int)(my + oy - (float)sz / 2) + shy, sz, sz,
                             ((uint32_t)(lt * 200) << 24) | 0x0B0A10);
            }
        if (wave > 0 && wave < 1) {         /* ударная волна — кольцо с мягким следом */
            float rr = wave * maxd;
            int k2, ring;
            for (ring = 0; ring < 3; ring++)
                for (k2 = 0; k2 < 360; k2 += 1 + ring) {
                    float a = (float)k2 * 0.01745f, r0 = rr - (float)ring * 7 * u;
                    if (r0 <= 0)
                        continue;
                    fx_line(s, cx + cosf(a) * (r0 - 3 * u) + (float)shx, cy + sinf(a) * (r0 - 3 * u) + (float)shy,
                            cx + cosf(a) * r0 + (float)shx, cy + sinf(a) * r0 + (float)shy,
                            ring ? 0x9C8FF0 : 0xF4F0FF, (unsigned)((ring ? 90 : 230) * (1 - wave) / (1 + ring)));
                }
        }
        if (t < 0.07f)                      /* вспышка удара */
            fx_flash(s, 0.22f * (1 - t / 0.07f));

        ccx = cx + ((float)ui->W / 2 - cx) * go + (float)shx;
        ccy = cy - lift * (float)bh * 0.08f + ((float)ui->H / 2 - cy) * go + press * 3 * u + (float)shy;
        k = (1 - 0.05f * press) * (1 + 0.45f * go) * (1 + 0.05f * settle) * (1 + 0.12f * smooth(dis));
        glow = smooth((t - 0.08f) / 0.3f) * (1 - smooth(dis * 1.4f)) + 0.6f * (dis > 0 ? 1 - dis : 0);
        rays = smooth((t - 0.15f) / 0.3f) * (1 - smooth((t - 0.86f) / 0.08f));
        if (rays > 0) {                     /* лучи из-за коробки медленно вращаются */
            int i2, w2;
            float len = maxd * 0.75f;
            for (i2 = 0; i2 < 18; i2++) {
                float a = t * 0.9f + (float)i2 * 6.2832f / 18;
                for (w2 = -4; w2 <= 4; w2++) {
                    float aa = a + (float)w2 * 0.011f;
                    fx_line(s, ccx + cosf(aa) * len, ccy + sinf(aa) * len, ccx, ccy,
                            i2 % 2 ? 0x9C8FF0 : 0xF4F0FF, (unsigned)(rays * (i2 % 2 ? 26 : 18) * (5 - abs(w2)) / 5));
                }
            }
        }
        if (glow > 0) {                     /* ореол — мягкое свечение за коробкой */
            int rmax = (int)((float)imax(bw, bh) * k * 1.05f), r;
            for (r = rmax; r > 0; r -= 2) {
                float f = 1 - (float)r / (float)rmax;
                unsigned al = (unsigned)(clampf(glow, 0, 1) * f * f * 110);
                int st, n = imax(24, r / 2);
                for (st = 0; st < n; st++) {
                    float a = (float)st * 6.2832f / (float)n;
                    px_blend(s, (int)(ccx + cosf(a) * (float)r), (int)(ccy + sinf(a) * (float)r), 0xB6ACFF, al);
                }
            }
        }
        {
            float cs = cosf(ang), persp = 0.2f * sinf(ang);
            float shine = clampf((t - 0.79f) / 0.08f, 0, 1);
            int ww = (int)((float)bw * k * fabsf(cs)), hh = (int)((float)bh * k), x0, i, j;
            int pb = imax(2, bw / 22);      /* частицы — квадраты pb×pb обложки */
            x0 = (int)(ccx - (float)ww / 2);
            for (i = 0; i < ww; i++) {
                int px = x0 + i, sx2 = cs >= 0 ? bx + i * bw / ww : bx + bw - 1 - i * bw / ww;
                float colk = 1 + persp * ((float)i / (float)ww - 0.5f) * 2;
                int ch = (int)((float)hh * colk), cy0 = (int)(ccy - (float)ch / 2);
                if (px < 0 || px >= ui->W || sx2 < 0 || sx2 >= ui->W || ch < 1)
                    continue;
                for (j = 0; j < ch; j++) {
                    int py = cy0 + j, sy2 = by + j * bh / ch;
                    uint32_t c, h;
                    if (py < 0 || py >= ui->H || sy2 < 0 || sy2 >= ui->H)
                        continue;
                    if (dis > 0) {          /* квадрат уже улетел частицей */
                        h = (uint32_t)((sx2 - bx) / pb * 73856093u ^ (sy2 - by) / pb * 19349663u);
                        h = (h ^ (h >> 13)) * 0x5bd1e995u;
                        if ((float)((h >> 8) & 1023) / 1024.0f * 0.7f + (1 - (float)(sy2 - by) / (float)bh) * 0.3f < dis)
                            continue;
                    }
                    c = src->px[sy2 * ui->W + sx2];
                    if (cs < 0)             /* обратная сторона коробки темнее */
                        c = 0xFF000000u | ((c >> 1) & 0x7F7F7Fu);
                    if (fabsf(cs) < 0.35f)  /* на ребре — блик */
                        c = gfx_mix(c, 0xFFF4F0FFu, (unsigned)((0.35f - fabsf(cs)) / 0.35f * 170));
                    if (shine > 0 && shine < 1) {   /* косой блик пробегает по обложке */
                        float band = (float)i / (float)ww + (float)j / (float)ch * 0.5f - (shine * 1.9f - 0.2f);
                        if (band > -0.12f && band < 0.12f)
                            c = gfx_mix(c, 0xFFFFFFFFu, (unsigned)((0.12f - fabsf(band)) / 0.12f * 150));
                    }
                    if (dis > 0)            /* перед распадом обложка накаляется */
                        c = gfx_mix(c, 0xFFE8E2FFu, (unsigned)(dis * 120));
                    s->px[py * ui->W + px] = c;
                }
            }
            if (dis > 0 && ww > 0) {        /* частицы: каждый улетевший квадрат светится и всплывает */
                int qx, qy;
                for (qy = 0; qy < bh; qy += pb)
                    for (qx = 0; qx < bw; qx += pb) {
                        uint32_t h = (uint32_t)(qx / pb * 73856093u ^ qy / pb * 19349663u), c;
                        float when, age, dx, dy, ps;
                        int sz2, ppx, ppy;
                        h = (h ^ (h >> 13)) * 0x5bd1e995u;
                        when = (float)((h >> 8) & 1023) / 1024.0f * 0.7f + (1 - (float)qy / (float)bh) * 0.3f;
                        age = (dis - when) / 0.35f;
                        if (age <= 0 || age >= 1)
                            continue;
                        dx = ((float)((h >> 18) & 255) / 255.0f - 0.5f) * 40 * u * age;
                        dy = -(16 + (float)((h >> 4) & 15) * 3) * u * age * (0.6f + age);
                        ps = (float)pb * k * (1 - age * 0.7f);
                        sz2 = imax(1, (int)ps);
                        ppx = (int)(ccx + ((float)qx - (float)bw / 2) * k + dx);
                        ppy = (int)(ccy + ((float)qy - (float)bh / 2) * k + dy);
                        c = src->px[imin(ui->H - 1, by + qy) * ui->W + imin(ui->W - 1, bx + qx)];
                        c = gfx_mix(c, 0xFFF4F0FFu, (unsigned)(100 + age * 155));
                        for (y = 0; y < sz2; y++)
                            for (x = 0; x < sz2; x++)
                                px_blend(s, ppx + x, ppy + y, c, (unsigned)((1 - age) * 255));
                    }
            }
        }
        if (t > 0.93f)
            gfx_fill(s, 0, 0, ui->W, ui->H, ((uint32_t)(smooth((t - 0.93f) / 0.07f) * 255) << 24));
        break;
    }
    }
    (void)now;
}

/* Нажали «играть»: отклик вибрацией и короткий «загрузочный» аккорд, как у приставок. */
static void start_launch(Ui *ui, uint32_t now)
{
    /* тоновая последовательность JSR-135: темп 240, единица 1/64 (≈16 мс); арпеджио на ударе,
     * пауза, финальный звон — ровно когда коробка встаёт лицом к игроку */
    static const uint8_t chime[] = { 0xFE, 1, 0xFD, 60, 0xFC, 64,
                                     67, 6, 72, 6, 76, 6, 79, 6, 84, 14, 0xFF, 129, 88, 22, 91, 30 };
    ui->launch_t0 = now;
    platform_vibrate(25);
    snd_play_once(chime, sizeof(chime), 55);
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
    MidpResult r;
    uint32_t until;

    if (ui->fx == FX_SHATTER)
        platform_fade_in(700);     /* анимация ушла в черноту — игра из неё проявляется */
    platform_cursor_mode(0);        /* в игре стик снова крестовина, указатель — системный */
    r = midp_run(g->path, g->base, mode, err, sizeof(err));
    platform_cursor_mode(1);
    ui->cur_press = 0;

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
    else if (which == SET_SHARP)
        ui->set.sharp = !ui->set.sharp;
    else {
        ui->set.sound = !ui->set.sound;
        if (!ui->cli_mute)
            audio_set_master(ui->set.sound ? 100 : 0);
    }
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
        } else if (key == KEY_NUM0) {       /* 0 (View на геймпаде) — сменить обложку */
            ce_open(ui);
        } else if ((key == KEY_FIRE || key == KEY_SOFT_LEFT || key == KEY_NUM0 + 5) && ui->lib.count) {
            start_launch(ui, now);
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

/* ---------- редактор обложки: свой кадр из скриншотов игры ---------- */

enum { CE_PREV, CE_NEXT, CE_SMALLER, CE_BIGGER, CE_RESET, CE_CANCEL, CE_SAVE, CE_COUNT };

static void shots_dir(const Game *g, char *out, size_t cap)
{
    char data[800];

    bt_data_dir(data, sizeof(data));
    snprintf(out, cap, "%s/shots/%s", data, g->base);
}

static float ce_aspect(const Ui *ui)
{
    return (float)ui->cover_w / (float)imax(1, ui->cover_h);
}

/* Рамка не вылезает за кадр и не больше него. */
static void ce_clamp(Ui *ui)
{
    float a = ce_aspect(ui), maxw = fminf((float)ui->ce_img.w, (float)ui->ce_img.h * a), ch;

    ui->ce_cw = clampf(ui->ce_cw, 16, maxw);
    ch = ui->ce_cw / a;
    ui->ce_cx = clampf(ui->ce_cx, ui->ce_cw / 2, (float)ui->ce_img.w - ui->ce_cw / 2);
    ui->ce_cy = clampf(ui->ce_cy, ch / 2, (float)ui->ce_img.h - ch / 2);
}

static void ce_load(Ui *ui)
{
    char dir[1100], path[1200];
    uint8_t *buf = NULL;
    long len;
    FILE *f;
    int w, h;
    uint32_t *px = NULL;

    image_free(&ui->ce_img);
    if (!ui->ce_n)
        return;
    shots_dir(&ui->lib.games[ui->sel], dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/%s", dir, ui->ce_files[ui->ce_i]);
    if ((f = bt_fopen(path, "rb")) != NULL) {
        if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0 &&
            (buf = malloc((size_t)len)) != NULL && fread(buf, 1, (size_t)len, f) == (size_t)len)
            px = png_decode(buf, (size_t)len, &w, &h);
        free(buf);
        fclose(f);
    }
    if (!px)
        return;
    ui->ce_img.px = px;
    ui->ce_img.w = w;
    ui->ce_img.h = h;
    ui->ce_cw = (float)w;               /* сначала — самая большая рамка по центру */
    ui->ce_cx = (float)w / 2;
    ui->ce_cy = (float)h / 2;
    ce_clamp(ui);
}

static int by_str_desc(const void *a, const void *b)
{
    return strcmp((const char *)b, (const char *)a);
}

static void ce_open(Ui *ui)
{
    char dir[1100];
    BtDir *d;
    const char *n;

    if (!ui->lib.count)
        return;
    ui->ce = 1;
    ui->ce_n = ui->ce_i = 0;
    ui->ce_drag = 0;
    shots_dir(&ui->lib.games[ui->sel], dir, sizeof(dir));
    if ((d = bt_dir_open(dir)) != NULL) {
        while ((n = bt_dir_next(d)) != NULL && ui->ce_n < 48) {
            size_t l = strlen(n);
            if (l > 4 && l < sizeof(ui->ce_files[0]) && !strcmp(n + l - 4, ".png"))
                snprintf(ui->ce_files[ui->ce_n++], sizeof(ui->ce_files[0]), "%s", n);
        }
        bt_dir_close(d);
    }
    /* свежие снимки первыми, заставка «title.png» — в самом начале */
    qsort(ui->ce_files, (size_t)ui->ce_n, sizeof(ui->ce_files[0]), by_str_desc);
    ce_load(ui);
}

static void ce_close(Ui *ui)
{
    ui->ce = 0;
    image_free(&ui->ce_img);
}

/* Где картинка скриншота и кнопки: превью сверху, ряд кнопок снизу. */
static void ce_geom(const Ui *ui, int *px, int *py, int *pw, int *ph, int *bar_y, int *bw)
{
    int bh = 13 * ui->s, avail_w = ui->W - 2 * ui->m, avail_h = ui->H - bh - 3 * ui->m;
    float k = 1;

    if (ui->ce_img.px) {
        k = fminf((float)avail_w / (float)ui->ce_img.w, (float)avail_h / (float)ui->ce_img.h);
        if (k >= 1)
            k = floorf(k);              /* целый масштаб — пиксели ровные */
    }
    *pw = ui->ce_img.px ? (int)((float)ui->ce_img.w * k) : avail_w;
    *ph = ui->ce_img.px ? (int)((float)ui->ce_img.h * k) : avail_h;
    *px = (ui->W - *pw) / 2;
    *py = ui->m + (avail_h - *ph) / 2;
    *bar_y = ui->H - bh - ui->m;
    *bw = (ui->W - 2 * ui->m) / CE_COUNT;
}

static void draw_cover_editor(Ui *ui)
{
    static const Icon *const icons[CE_COUNT] = { &icon_chevron, &icon_chevron, &icon_minus, &icon_plus,
                                                 &icon_trash, &icon_cross, &icon_check };
    Image *s = ui->scr;
    int px, py, pw, ph, by, bw, i;

    ce_geom(ui, &px, &py, &pw, &ph, &by, &bw);
    gfx_fill(s, 0, 0, ui->W, ui->H, 0xFF0B0A10u);
    if (ui->ce_img.px) {
        float k = (float)pw / (float)ui->ce_img.w, ch = ui->ce_cw / ce_aspect(ui);
        int fx = px + (int)((ui->ce_cx - ui->ce_cw / 2) * k), fy = py + (int)((ui->ce_cy - ch / 2) * k);
        int fw = (int)(ui->ce_cw * k), fh = (int)(ch * k);
        gfx_blit_scaled(s, &ui->ce_img, px, py, pw, ph, 255);
        /* всё, что не попадёт на обложку, притемнено */
        gfx_fill(s, px, py, pw, fy - py, 0xA0000000u);
        gfx_fill(s, px, fy + fh, pw, py + ph - fy - fh, 0xA0000000u);
        gfx_fill(s, px, fy, fx - px, fh, 0xA0000000u);
        gfx_fill(s, fx + fw, fy, px + pw - fx - fw, fh, 0xA0000000u);
        gfx_frame(s, fx, fy, fw, fh, 0xFFF4F0FFu);
        gfx_frame(s, fx - 1, fy - 1, fw + 2, fh + 2, ui->th->accent);
        /* точки-страницы: какой это снимок из скольких */
        for (i = 0; i < ui->ce_n && i < 24; i++)
            gfx_fill(s, ui->W / 2 - ui->ce_n * 3 + i * 6, py + ph + 3, 3, 3,
                     i == ui->ce_i ? 0xFFF4F0FFu : 0xFF4A4458u);
    } else {
        /* снимков нет: фотоаппарат — сделайте снимок в игре (F12 или кнопка с фотоаппаратом) */
        draw_icon(s, &icon_camera, (ui->W - icon_camera.w * 3 * ui->s) / 2, py + ph / 2 - icon_camera.h * 3 * ui->s / 2,
                  3 * ui->s, 0xFF4A4458u, 0);
    }
    for (i = 0; i < CE_COUNT; i++) {
        const Icon *ic = icons[i];
        int x = ui->m + i * bw, on = (i == CE_PREV || i == CE_NEXT) ? ui->ce_n > 1 : (i <= CE_BIGGER) ? ui->ce_img.px != NULL : 1;
        rrect(s, x + 1, by, bw - 2, 13 * ui->s, 2, i == CE_SAVE && ui->ce_img.px ? ui->th->accent : 0xFF262233u);
        draw_icon(s, ic, x + (bw - ic->w * ui->s) / 2, by + (13 * ui->s - ic->h * ui->s) / 2, ui->s,
                  on ? 0xFFF4F0FFu : 0xFF4A4458u, i == CE_NEXT);
    }
}

/* Сохранить выбранный кусок как covers/<игра>.png и перерисовать коробку. */
static void ce_save(Ui *ui)
{
    Game *g = &ui->lib.games[ui->sel];
    float ch = ui->ce_cw / ce_aspect(ui);
    int x0 = (int)(ui->ce_cx - ui->ce_cw / 2), y0 = (int)(ui->ce_cy - ch / 2), w = (int)ui->ce_cw, h = (int)ch, y;
    char dir[1100], path[1400];
    uint32_t *crop;
    uint8_t *png;
    size_t len;
    FILE *f;

    if (!ui->ce_img.px || w < 1 || h < 1 || !(crop = malloc((size_t)w * (size_t)h * 4)))
        return;
    for (y = 0; y < h; y++)
        memcpy(crop + (size_t)y * (size_t)w, ui->ce_img.px + (size_t)(y0 + y) * (size_t)ui->ce_img.w + (size_t)x0,
               (size_t)w * 4);
    png = png_encode(crop, w, h, &len);
    free(crop);
    if (!png)
        return;
    snprintf(dir, sizeof(dir), "%s/covers", ui->lib.dir);
    bt_mkdirs(dir);
    snprintf(path, sizeof(path), "%s/%s.png", dir, g->base);
    if ((f = bt_fopen(path, "wb")) != NULL) {
        fwrite(png, 1, len, f);
        fclose(f);
    }
    free(png);
    g->cover_ready = 0;         /* перезагрузится уже с новой обложкой */
}

static void ce_reset(Ui *ui)
{
    Game *g = &ui->lib.games[ui->sel];
    char path[1400];

    snprintf(path, sizeof(path), "%s/covers/%s.png", ui->lib.dir, g->base);
    bt_remove(path);
    g->cover_ready = 0;
}

static void ce_action(Ui *ui, int a)
{
    switch (a) {
    case CE_PREV: case CE_NEXT:
        if (ui->ce_n > 1) {
            ui->ce_i = (ui->ce_i + (a == CE_NEXT ? 1 : ui->ce_n - 1)) % ui->ce_n;
            ce_load(ui);
        }
        break;
    case CE_SMALLER: ui->ce_cw *= 0.88f; ce_clamp(ui); break;
    case CE_BIGGER:  ui->ce_cw *= 1.12f; ce_clamp(ui); break;
    case CE_RESET:   ce_reset(ui); ce_close(ui); break;
    case CE_CANCEL:  ce_close(ui); break;
    case CE_SAVE:
        if (ui->ce_img.px) {
            ce_save(ui);
            ce_close(ui);
        }
        break;
    }
}

static void ce_event(Ui *ui, const Event *ev)
{
    int px, py, pw, ph, by, bw;

    ce_geom(ui, &px, &py, &pw, &ph, &by, &bw);
    switch (ev->type) {
    case EV_BACK:
        ce_close(ui);
        break;
    case EV_KEY_DOWN:
        switch (ev->key) {
        case KEY_LEFT:  ce_action(ui, CE_PREV); break;
        case KEY_RIGHT: ce_action(ui, CE_NEXT); break;
        case KEY_UP:    ce_action(ui, CE_BIGGER); break;
        case KEY_DOWN:  ce_action(ui, CE_SMALLER); break;
        case KEY_FIRE: case KEY_SOFT_LEFT: ce_action(ui, CE_SAVE); break;
        case KEY_SOFT_RIGHT: ce_action(ui, CE_CANCEL); break;
        default:
            if (ev->key >= KEY_NUM0 + 1 && ev->key <= KEY_NUM0 + 9 && ui->ce_img.px) {   /* 2468 — двигать рамку */
                int d = ev->key - KEY_NUM0;
                ui->ce_cx += d == 4 ? -8 : d == 6 ? 8 : 0;
                ui->ce_cy += d == 2 ? -8 : d == 8 ? 8 : 0;
                ce_clamp(ui);
            }
        }
        break;
    case EV_POINTER_DOWN:
        if (ev->y >= by) {
            ce_action(ui, imin(CE_COUNT - 1, imax(0, (ev->x - ui->m) / imax(1, bw))));
        } else if (ui->ce_img.px && ev->x >= px && ev->x < px + pw && ev->y >= py && ev->y < py + ph) {
            float k = (float)pw / (float)ui->ce_img.w;
            ui->ce_drag = 1;            /* тянем рамку: запоминаем, где за неё взялись */
            ui->ce_dx = ui->ce_cx - (float)(ev->x - px) / k;
            ui->ce_dy = ui->ce_cy - (float)(ev->y - py) / k;
        }
        break;
    case EV_POINTER_MOVE:
        if (ui->ce_drag && ui->ce_img.px) {
            float k = (float)pw / (float)ui->ce_img.w;
            ui->ce_cx = (float)(ev->x - px) / k + ui->ce_dx;
            ui->ce_cy = (float)(ev->y - py) / k + ui->ce_dy;
            ce_clamp(ui);
        }
        break;
    case EV_POINTER_UP:
        ui->ce_drag = 0;
        break;
    default:
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
    if (!ui->lib.count && ui->tab != TAB_SETTINGS && platform_can_import()) {
        platform_import_games();
        return;
    }
    if (ui->tab == TAB_LIBRARY && platform_can_import()) {
        int x, y;
        grid_cell(ui, ui->lib.count, &x, &y);
        if (px >= x && px < x + ui->cell_w && py >= y && py < y + ui->cell_h) {
            platform_import_games();
            return;
        }
    }
    if (ui->tab == TAB_PLAY && ui->lib.count && px < ui->m + 14 && py < ui->m + 12) {
        ce_open(ui);
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
            start_launch(ui, now);
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

/* Папка игр изменилась: перечитываем полку, выбранной оставляем ту же игру, если она есть. */
static void rescan(Ui *ui, const char *games_dir)
{
    char keep[1024];
    int i;

    snprintf(keep, sizeof(keep), "%s", ui->lib.count ? ui->lib.games[ui->sel].path : "");
    library_free(&ui->lib);
    library_scan(&ui->lib, games_dir);
    if (!ui->lib.dir[0])
        snprintf(ui->lib.dir, sizeof(ui->lib.dir), "%s", games_dir);
    ui->sel = 0;
    for (i = 0; i < ui->lib.count; i++)
        if (!strcmp(ui->lib.games[i].path, keep))
            ui->sel = i;
    ui->pos = (float)ui->sel;
}

/* ---------- добавление игр перетаскиванием ---------- */

static char import_sel[1024];   /* только что добавленная игра — полка откроется на ней */

static int ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext), i;

    if (n < m)
        return 0;
    for (i = 0; i < m; i++)
        if ((s[n - m + i] | 0x20) != ext[i])
            return 0;
    return 1;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in = bt_fopen(src, "rb"), *out;
    char buf[65536];
    size_t n;
    int ok = 1;

    if (!in)
        return 0;
    if (!strcmp(src, dst) || !(out = bt_fopen(dst, "wb"))) {
        fclose(in);
        return !strcmp(src, dst);
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, out) != n)
            ok = 0;
    fclose(in);
    if (fclose(out) != 0)
        ok = 0;
    return ok;
}

int launcher_import(const char *games_dir, const char *path)
{
    const char *name = path, *p;
    char dst[1200], pair[1200], pair_dst[1200], stem[1024];
    FILE *f;
    int jar = ends_with(path, ".jar");

    if (!jar && !ends_with(path, ".jad"))
        return 0;
    for (p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            name = p + 1;
    bt_mkdirs(games_dir);
    snprintf(dst, sizeof(dst), "%s/%s", games_dir, name);
    if (!copy_file(path, dst)) {
        BT_LOG("не удалось добавить %s\n", path);
        return 0;
    }
    /* у игры бывает пара: JAR с описанием JAD — переносим вместе */
    snprintf(stem, sizeof(stem), "%.*s", (int)(strlen(path) - 4), path);
    snprintf(pair, sizeof(pair), "%s%s", stem, jar ? ".jad" : ".jar");
    if ((f = bt_fopen(pair, "rb")) != NULL) {
        fclose(f);
        snprintf(pair_dst, sizeof(pair_dst), "%.*s%s", (int)(strlen(dst) - 4), dst, jar ? ".jad" : ".jar");
        copy_file(pair, pair_dst);
    }
    snprintf(import_sel, sizeof(import_sel), "%.*s.jar", (int)(strlen(dst) - 4), dst);
    BT_LOG("добавлена игра: %s\n", dst);
    return 1;
}

/* После пересканирования полки встать на добавленную игру. */
static void select_imported(Ui *ui)
{
    const char *want = strrchr(import_sel, '/');
    int i;

    if (!import_sel[0])
        return;
    want = want ? want + 1 : import_sel;
    for (i = 0; i < ui->lib.count; i++) {
        const char *b = strrchr(ui->lib.games[i].path, '/');
        b = b ? b + 1 : ui->lib.games[i].path;
        if (!strcmp(b, want)) {
            ui->sel = i;
            ui->pos = (float)i;
            ui->tab = TAB_PLAY;
            ui->focus_tabs = 0;
        }
    }
    import_sel[0] = '\0';
}

/* ---------- курсор ---------- */

static void draw_cursor(Ui *ui, uint32_t now)
{
    int s = imax(1, ui->s), x0, y0, x, y, pass;

    if (!ui->cur_on || ui->launch_t0 || (ui->cur_on == 2 && now - ui->cur_t > 6000))
        return;
    x0 = (int)ui->cur_x - CURSOR_HX * s;
    y0 = (int)ui->cur_y + (ui->cur_press ? s : 0);     /* нажатие — палец «вдавливается» */
    for (pass = 0; pass < 2; pass++)    /* сначала мягкая тень, потом сама рука */
        for (y = 0; y < CURSOR_H; y++)
            for (x = 0; x < CURSOR_W; x++) {
                char c = cursor_hand[y][x];
                uint32_t col;
                if (c == '.')
                    continue;
                if (!pass) {
                    if (!ui->cur_press)
                        gfx_fill(ui->scr, x0 + (x + 1) * s, y0 + (y + 1) * s, s, s, 0x50000000u);
                    continue;
                }
                col = c == '#' ? 0xFF1B1622u : c == '-' ? 0xFFC9C0D6u :
                      ui->cur_press ? 0xFFE6E0EEu : 0xFFFFFFFFu;
                gfx_fill(ui->scr, x0 + x * s, y0 + y * s, s, s, col);
            }
}

/* Курсор поверх событий: мышь двигает его, кнопка A при курсоре от стика — клик под ним.
 * Возвращает 0, если событие съедено целиком. */
static int cursor_event(Ui *ui, Event *ev, uint32_t now)
{
    switch (ev->type) {
    case EV_HOVER:
        ui->cur_on = 1;
        ui->cur_x = (float)ev->x;
        ui->cur_y = (float)ev->y;
        return 0;
    case EV_POINTER_DOWN: case EV_POINTER_MOVE: case EV_POINTER_UP:
        ui->cur_on = 1;
        ui->cur_x = (float)ev->x;
        ui->cur_y = (float)ev->y;
        ui->cur_press = ev->type != EV_POINTER_UP;
        return 1;
    case EV_KEY_DOWN: case EV_KEY_UP:
        if (ev->pad && ev->key == KEY_FIRE && ui->cur_on == 2 && now - ui->cur_t <= 6000) {
            ev->type = ev->type == EV_KEY_DOWN ? EV_POINTER_DOWN : EV_POINTER_UP;
            ev->x = (int)ui->cur_x;
            ev->y = (int)ui->cur_y;
            ui->cur_press = ev->type == EV_POINTER_DOWN;
            ui->cur_t = now;
        } else if (ev->type == EV_KEY_DOWN) {
            ui->cur_on = 0;     /* взялись за клавиши или крестовину — курсор не мешает */
            ui->cur_press = 0;
        }
        return 1;
    default:
        return 1;
    }
}

/* Левый стик водит курсор: скорость растёт с наклоном, у края — мёртвая зона. */
static void cursor_stick(Ui *ui, float dt, uint32_t now)
{
    float sx, sy, mag, v;

    platform_stick(&sx, &sy);
    mag = sqrtf(sx * sx + sy * sy);
    if (mag < 0.2f)
        return;
    if (ui->cur_on != 2) {
        if (ui->cur_on == 0) {          /* появляется посередине экрана */
            ui->cur_x = (float)ui->W / 2;
            ui->cur_y = (float)ui->H / 2;
        }
        ui->cur_on = 2;
    }
    v = (float)(ui->W + ui->H) * 0.6f * (mag - 0.2f) / 0.8f * (0.35f + 0.65f * mag) / mag;
    ui->cur_x = clampf(ui->cur_x + sx * v * dt, 0, (float)ui->W - 1);
    ui->cur_y = clampf(ui->cur_y + sy * v * dt, 0, (float)ui->H - 1);
    ui->cur_t = now;
    if (ui->cur_press) {                /* A зажата — тянем, как пальцем */
        Event mv;
        memset(&mv, 0, sizeof(mv));
        mv.type = EV_POINTER_MOVE;
        mv.x = (int)ui->cur_x;
        mv.y = (int)ui->cur_y;
        if (ui->ce) {
            ce_event(ui, &mv);
        } else if (ui->ptr_down) {
            ui->ptr_x = mv.x;
            ui->ptr_y = mv.y;
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
    ui.cli_mute = cfg->mute;
    ui.tab = TAB_PLAY;
    settings_load(&ui);
    audio_set_master(ui.cli_mute || !ui.set.sound ? 0 : 100);
    {   /* вид анимации запуска; BT_LAUNCH_ANIM — посмотреть другие */
        const char *e = getenv("BT_LAUNCH_ANIM");
        ui.fx = e ? atoi(e) % FX_COUNT : FX_SHATTER;
    }
    cfg->width = 400;
    cfg->height = 240;
    cfg->scale = SCALE_INTEGER;
    if (platform_init(cfg))
        return 1;
    library_scan(&ui.lib, games_dir);
    if (!ui.lib.dir[0])
        snprintf(ui.lib.dir, sizeof(ui.lib.dir), "%s", games_dir);
    select_imported(&ui);
    layout(&ui);
    platform_display_size(&dw, &dh);
    platform_cursor_mode(1);
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
            if (!cursor_event(&ui, &ev, now))
                continue;
            if (ui.ce && ev.type != EV_QUIT) {
                ce_event(&ui, &ev);
                continue;
            }
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
            } else if (ev.type == EV_REFRESH) {
                rescan(&ui, games_dir);
                select_imported(&ui);
            } else if (ev.type == EV_DROP && ev.name) {
                /* перетащили JAR на окно — в папку игр и сразу на полку */
                if (launcher_import(ui.lib.dir[0] ? ui.lib.dir : games_dir, ev.name)) {
                    rescan(&ui, games_dir);
                    select_imported(&ui);
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
                ui.ptr_down = 1;
                ui.ptr_x0 = ui.ptr_x = ev.x;
                ui.ptr_y0 = ui.ptr_y = ev.y;
            } else if (ev.type == EV_POINTER_MOVE && ui.ptr_down) {
                ui.ptr_x = ev.x;
                ui.ptr_y = ev.y;
            } else if (ev.type == EV_POINTER_UP && ui.ptr_down) {
                int dx = ev.x - ui.ptr_x0, dy = ev.y - ui.ptr_y0;
                ui.ptr_down = 0;
                if (abs(dx) < 10 && abs(dy) < 10)
                    on_pointer(&ui, ui.ptr_x0, ui.ptr_y0, now);
                else if (abs(dx) > abs(dy) && ui.tab != TAB_SETTINGS && ui.ptr_y0 < ui.H - ui.tab_h) {
                    /* свайп по полке: чем длиннее, тем дальше листаем */
                    int n = -dx / imax(1, ui.box_w * 2 / 3);
                    select_game(&ui, ui.sel + (n ? n : dx < 0 ? 1 : -1));
                }
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

        if (!ui.launch_t0)
            cursor_stick(&ui, dt, now);
        load_covers(&ui, now);
        if (ui.ce) {
            draw_cover_editor(&ui);
        } else if (ui.launch_t0 && ui.snap.px) {
            launch_fx(&ui, (float)(now - ui.launch_t0) / (ui.fx == FX_SHATTER ? 3300 : 1700), now);
        } else {
            uint32_t l0 = ui.launch_t0;
            ui.launch_t0 = 0;           /* снимок полки — без анимации */
            draw_background(&ui, 0, ui.H);
            if (ui.tab == TAB_PLAY)
                draw_play(&ui, now);
            else if (ui.tab == TAB_LIBRARY)
                draw_library(&ui, now);
            else
                draw_settings(&ui);
            draw_tabs(&ui);
            ui.launch_t0 = l0;
            if (ui.launch_t0) {
                ui.snap = image_new(ui.W, ui.H);
                if (ui.snap.px) {
                    memcpy(ui.snap.px, ui.scr->px, (size_t)ui.W * (size_t)ui.H * 4);
                    launch_fx(&ui, 0, now);
                } else
                    draw_launch_overlay(&ui, now);
            }
        }
        draw_cursor(&ui, now);
        platform_present();

        if (ui.launch_t0 && now - ui.launch_t0 >= (uint32_t)(ui.fx == FX_SHATTER ? 3300 : 1700)) {
            image_free(&ui.snap);
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
