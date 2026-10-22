#include "launcher.h"
#include "library.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define COL_ACCENT  0xFF4FC3F7u
#define COL_TEXT    0xFFEDEFF3u
#define COL_DIM     0xFF8C93A0u
#define COL_PAD_A   0xFF5DBB4Au   /* цвета кнопок Xbox */
#define COL_PAD_B   0xFFE2483Du

#define REPEAT_DELAY 330
#define REPEAT_RATE  110
#define LAUNCH_MS    1500
#define KEEP_COVERS  8      /* обложки дальше этого от выбранной выгружаем — память важнее */

typedef struct {
    Library  lib;
    Image   *scr;
    int      W, H, k;            /* виртуальный экран и множитель масштаба */
    int      case_w, case_h, cover_w, cover_h, small_w, small_h, shelf_y;
    int      sel;
    float    pos;                /* плавная позиция прокрутки */
    int      held, held_since, last_repeat;
    int      pad_hints;          /* подсказки для геймпада или для клавиатуры */
    char     toast[96];
    uint32_t toast_until;
    uint32_t launch_t0;          /* 0 — не запускаем */
    ScaleMode game_scale;        /* как масштабировать экран телефона в игре */
} Ui;

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void fill_circle(Image *s, int cx, int cy, int r, uint32_t c)
{
    int y;

    for (y = -r; y <= r; y++) {
        int half = (int)sqrtf((float)(r * r - y * y) + 0.5f);
        gfx_fill(s, cx - half, cy + y, half * 2 + 1, 1, c);
    }
}

static void text_center(Image *s, const Font *f, int cx, int y, const char *t, uint32_t c)
{
    gfx_text(s, f, cx - gfx_text_width(f, t, 1) / 2, y, t, c, 1);
}

#define SIDE_SCALE 0.66f

/* Область обложки внутри бокса: слева корешок-петля (~9% ширины), по краям рамка. */
static void cover_area(int w, int h, int *cw, int *ch)
{
    *cw = w - w * 9 / 100 - 4;
    *ch = h - 4;
}

/* Разметка зависит только от размера экрана; обложки рисуются под центральный бокс. */
static void layout(Ui *ui)
{
    int dw, dh, i, old_w = ui->cover_w, old_h = ui->cover_h;

    platform_display_size(&dw, &dh);
    /* Минимум 240 строк по вертикали, целый масштаб — и при этом весь экран без полей. */
    ui->k = dh / 240 > 0 ? dh / 240 : 1;
    ui->W = dw / ui->k;
    ui->H = dh / ui->k;
    platform_set_screen(ui->W, ui->H, SCALE_INTEGER);
    ui->scr = platform_screen();

    /* Пропорции настоящего jewel case: 142 × 125 мм, корешок-петля ~9%. */
    ui->case_h = ui->H * 50 / 100;
    ui->case_w = ui->case_h * 142 / 125;
    cover_area(ui->case_w, ui->case_h, &ui->cover_w, &ui->cover_h);
    cover_area((int)((float)ui->case_w * SIDE_SCALE), (int)((float)ui->case_h * SIDE_SCALE),
               &ui->small_w, &ui->small_h);
    ui->shelf_y = ui->H * 12 / 100;
    /* обложки перерисовываем, только если поменялся их размер */
    for (i = 0; i < ui->lib.count && (old_w != ui->cover_w || old_h != ui->cover_h); i++)
        ui->lib.games[i].cover_ready = 0;
}

static void draw_background(Ui *ui, uint32_t now)
{
    Image *s = ui->scr;
    int floor_y = ui->shelf_y + ui->case_h, i;

    gfx_vgradient(s, 0, 0, ui->W, floor_y, 0xFF0B0D1Au, 0xFF231A3Cu);
    gfx_vgradient(s, 0, floor_y, ui->W, ui->H - floor_y, 0xFF120E22u, 0xFF06060Bu);
    /* Мерцающие «звёзды» — детерминированные, без rand() */
    for (i = 0; i < 48; i++) {
        uint32_t h = (uint32_t)i * 2654435761u;
        int x = (int)(h % (uint32_t)ui->W), y = (int)((h >> 12) % (uint32_t)(floor_y > 1 ? floor_y - 1 : 1));
        unsigned a = 60 + (unsigned)(90 * (0.5f + 0.5f * sinf((float)now / 700.0f + (float)i)));
        gfx_fill(s, x, y, 1, 1, (a << 24) | 0xC8D2FFu);
    }
    /* Линия «полки» с бликом */
    gfx_fill(s, 0, floor_y + 1, ui->W, 1, 0x50A0B4FFu);
    /* Лёгкие строки развёртки, как на ЭЛТ */
    for (i = 0; i < ui->H; i += 2)
        gfx_fill(s, 0, i, ui->W, 1, 0x10000000u);
}

/* Отражение уже нарисованного бокса на «глянцевом полу». */
static void reflect(Image *s, int x, int y, int w, int h, int len, int alpha)
{
    int i, j;

    for (i = 0; i < len; i++) {
        int sy = y + h - 1 - i, dy = y + h + 2 + i;
        unsigned a = (unsigned)(alpha * (len - i) / len);
        if (sy < 0 || dy >= s->h || dy < 0)
            continue;
        for (j = x < 0 ? 0 : x; j < x + w && j < s->w; j++)
            s->px[dy * s->w + j] = gfx_mix(s->px[dy * s->w + j], s->px[sy * s->w + j], a);
    }
}

static void draw_case(Ui *ui, int x, int y, int w, int h, Game *g, int dim, uint32_t now)
{
    Image *s = ui->scr;
    int spine = w * 9 / 100, cx = x + spine + 2, cy = y + 2, cw, ch, j;

    cover_area(w, h, &cw, &ch);
    /* тень */
    gfx_fill(s, x + 3, y + 4, w, h, 0x50000000u);
    /* лоток и обложка */
    gfx_fill(s, x, y, w, h, 0xFF14161Bu);
    if (g && g->cover_ready && g->cover.px) {
        int fade = (int)clampf((float)(now - g->cover_time) * 255.0f / 300.0f, 0, 255);
        const Image *src = g->cover_small.px && cw <= g->cover_small.w + 1 ? &g->cover_small : &g->cover;
        gfx_blit_scaled(s, src, cx, cy, cw, ch, fade);
    } else {
        /* обложка ещё грузится — бежит блик */
        int sweep = (int)(now / 4 % (unsigned)(cw + 40)) - 20;
        gfx_fill(s, cx, cy, cw, ch, 0xFF262A33u);
        gfx_fill(s, cx + sweep, cy, 12, ch, 0x18FFFFFFu);
    }
    /* корешок-петля: полупрозрачный пластик с рёбрами */
    gfx_fill(s, x, y, spine, h, 0x70C9D1DCu);
    for (j = x + 1; j < x + spine - 1; j += 2)
        gfx_fill(s, j, y + 2, 1, h - 4, 0x30FFFFFFu);
    gfx_fill(s, x + spine, y, 1, h, 0x90FFFFFFu);
    gfx_fill(s, x + 1, y + h / 8, spine - 2, 2, 0x60000000u);
    gfx_fill(s, x + 1, y + h - h / 8 - 2, spine - 2, 2, 0x60000000u);
    /* язычки, держащие буклет */
    gfx_fill(s, x + w - 3, y + h / 5, 2, h / 10, 0x70FFFFFFu);
    gfx_fill(s, x + w - 3, y + h - h / 5 - h / 10, 2, h / 10, 0x70FFFFFFu);
    /* рамка прозрачной крышки */
    gfx_frame(s, x, y, w, h, 0xD0DCE3ECu);
    gfx_frame(s, x + 1, y + 1, w - 2, h - 2, 0x30FFFFFFu);
    gfx_fill(s, x + 1, y + 1, w - 2, 1, 0x90FFFFFFu);
    /* диагональный блик на пластике */
    for (j = 0; j < h; j++) {
        int bx = x + w * 62 / 100 - j * 6 / 10;
        gfx_fill(s, bx, y + j, w / 9, 1, 0x1CFFFFFFu);
        gfx_fill(s, bx + w / 9 + 3, y + j, w / 40 + 1, 1, 0x14FFFFFFu);
    }
    if (dim > 0)
        gfx_fill(s, x, y, w, h, ((uint32_t)dim << 24) | 0x05060Au);
}

/* Где на экране бокс с номером i при текущей прокрутке. */
static void case_rect(const Ui *ui, int i, int *x, int *y, int *w, int *h, int *dim)
{
    float d = (float)i - ui->pos, ad = fabsf(d), near = clampf(ad, 0, 1), sc = 1.0f - (1.0f - SIDE_SCALE) * near;
    float small_w = (float)ui->case_w * SIDE_SCALE, gap = (float)ui->case_w * 0.08f;
    float off = near * ((float)ui->case_w * 0.5f + small_w * 0.5f + gap) + (ad > 1 ? (ad - 1) * (small_w + gap) : 0);
    float cx = (float)ui->W / 2 + (d < 0 ? -off : off);

    *w = (int)((float)ui->case_w * sc);
    *h = (int)((float)ui->case_h * sc);
    *x = (int)(cx - (float)*w / 2);
    *y = ui->shelf_y + ui->case_h - *h;   /* все стоят на одной полке */
    *dim = (int)(clampf(ad, 0, 2.5f) * 60);
}

static void draw_button(Image *s, int x, int y, uint32_t col, const char *letter, const char *label)
{
    fill_circle(s, x + 5, y + 6, 6, col);
    gfx_text(s, &font_small, x + 2, y, letter, 0xFFFFFFFFu, 1);
    gfx_text(s, &font_small, x + 15, y, label, COL_TEXT, 1);
}

static void draw_key(Image *s, int x, int y, const char *key, const char *label)
{
    int kw = gfx_text_width(&font_small, key, 1) + 6;

    gfx_fill(s, x, y - 1, kw, 14, 0xFF3A3F4Au);
    gfx_fill(s, x, y + 12, kw, 1, 0xFF22252Cu);
    gfx_text(s, &font_small, x + 3, y, key, COL_TEXT, 1);
    gfx_text(s, &font_small, x + kw + 4, y, label, COL_TEXT, 1);
}

static void draw_ui(Ui *ui, uint32_t now)
{
    Image *s = ui->scr;
    int i, order, y, n = ui->lib.count;
    char buf[160], clock_s[8];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    float launch = ui->launch_t0 ? (float)(now - ui->launch_t0) / LAUNCH_MS : 0;

    draw_background(ui, now);

    /* шапка */
    gfx_text(s, &font_big, 8, 5, "BT-J2ME", COL_ACCENT, 1);
    gfx_text(s, &font_small, 8 + gfx_text_width(&font_big, "BT-J2ME", 1) + 6, 8, "игровая консоль", COL_DIM, 1);
    strftime(clock_s, sizeof(clock_s), "%H:%M", tm);
    gfx_text(s, &font_big, ui->W - 8 - gfx_text_width(&font_big, clock_s, 1), 5, clock_s, COL_TEXT, 1);
    gfx_fill(s, 8, 24, ui->W - 16, 1, 0x30FFFFFFu);

    if (!n) {
        int x = (ui->W - ui->case_w) / 2;
        draw_case(ui, x, ui->shelf_y, ui->case_w, ui->case_h, NULL, 0, now);
        text_center(s, &font_big, ui->W / 2, ui->shelf_y + ui->case_h + 14, "Полка пуста", COL_TEXT);
        text_center(s, &font_small, ui->W / 2, ui->shelf_y + ui->case_h + 34, "Положите игры .jar в папку:", COL_DIM);
        text_center(s, &font_small, ui->W / 2, ui->shelf_y + ui->case_h + 48, ui->lib.dir, COL_ACCENT);
    }

    /* боксы: сначала дальние, центральный — последним */
    for (order = 4; order >= 0; order--) {
        for (i = 0; i < n; i++) {
            int x, cy, w, h, dim, dist = (int)(fabsf((float)i - ui->pos) + 0.5f);
            if (dist != order)
                continue;
            case_rect(ui, i, &x, &cy, &w, &h, &dim);
            if (x > ui->W || x + w < 0)
                continue;
            if (i == ui->sel) {
                float lift = 0, out = 0;
                if (ui->launch_t0) {
                    out = clampf(launch / 0.35f, 0, 1);
                    out = 1 - (1 - out) * (1 - out);
                    lift = out * (float)w * 0.18f;
                } else {
                    cy += (int)(1.5f * sinf((float)now / 600.0f));  /* бокс чуть «дышит» */
                }
                x -= (int)lift;
                if (out > 0) {
                    /* диск выезжает из-за бокса вправо */
                    int r = h * 44 / 100;
                    gfx_disc(s, x + w / 2 + (int)(out * (float)w * 0.62f), cy + h / 2, r,
                             (float)(now - ui->launch_t0) / 1000.0f * (0.6f + launch * 4), 255);
                }
            }
            draw_case(ui, x, cy, w, h, &ui->lib.games[i], dim, now);
            reflect(s, x, cy, w, h, h / 4, 60 - dim / 3);
        }
    }

    y = ui->shelf_y + ui->case_h + 9;   /* подпись поверх бледного отражения */
    if (n) {
        Game *g = &ui->lib.games[ui->sel];
        char meta[320];

        text_center(s, &font_big, ui->W / 2, y, g->name, COL_TEXT);
        snprintf(meta, sizeof(meta), "%s%s%s  ·  %ld КБ", g->vendor[0] ? g->vendor : "неизвестный издатель",
                 g->version[0] ? "  ·  v" : "", g->version, (g->size + 1023) / 1024);
        text_center(s, &font_small, ui->W / 2, y + 19, meta, COL_DIM);
        /* точки-индикатор позиции */
        if (n <= 24) {
            int dx = ui->W / 2 - n * 4;
            for (i = 0; i < n; i++)
                gfx_fill(s, dx + i * 8 + 2, y + 36, i == ui->sel ? 4 : 2, 2, i == ui->sel ? COL_ACCENT : 0x80FFFFFFu);
        } else {
            snprintf(buf, sizeof(buf), "%d / %d", ui->sel + 1, n);
            text_center(s, &font_small, ui->W / 2, y + 33, buf, COL_DIM);
        }
    }

    /* подсказки внизу — под геймпад или под клавиатуру, смотря чем пользуются */
    y = ui->H - 17;
    gfx_fill(s, 0, y - 5, ui->W, 22, 0x90000000u);
    if (ui->pad_hints) {
        draw_button(s, 10, y, COL_PAD_A, "A", "Играть");
        gfx_text(s, &font_small, 82, y, "◄ ►  Листать", COL_TEXT, 1);
        /* кнопка Xbox: светящееся кольцо */
        fill_circle(s, ui->W - 65, y + 6, 6, COL_PAD_A);
        fill_circle(s, ui->W - 65, y + 6, 3, 0xFF1A1D22u);
        gfx_text(s, &font_small, ui->W - 55, y, "Выход", COL_TEXT, 1);
    } else {
        draw_key(s, 10, y, "Enter", "Играть");
        draw_key(s, 98, y, "← →", "Листать");
        draw_key(s, ui->W - 70, y, "Esc", "Выход");
    }

    if (ui->toast[0] && now < ui->toast_until) {
        int tw = gfx_text_width(&font_small, ui->toast, 1) + 16;
        gfx_fill(s, (ui->W - tw) / 2, 30, tw, 18, 0xE0202530u);
        gfx_fill(s, (ui->W - tw) / 2, 30, 2, 18, COL_ACCENT);
        text_center(s, &font_small, ui->W / 2, 33, ui->toast, COL_TEXT);
    }

    if (ui->launch_t0 && launch > 0.45f) {
        /* затемнение и крутящийся диск в центре — «загрузка» */
        unsigned a = (unsigned)(clampf((launch - 0.45f) / 0.4f, 0, 1) * 255);
        gfx_fill(s, 0, 0, ui->W, ui->H, (a << 24) | 0x000000u);
        if (launch > 0.6f) {
            int r = ui->case_h * 3 / 10;
            unsigned da = (unsigned)(clampf((launch - 0.6f) / 0.2f, 0, 1) * 255);
            gfx_disc(s, ui->W / 2, ui->H / 2 - 8, r, (float)(now - ui->launch_t0) / 180.0f, (int)da);
            text_center(s, &font_small, ui->W / 2, ui->H / 2 + r + 2, "Загрузка…", (da << 24) | 0xEDEFF3u);
        }
    }
}

/* Экран «игры»: пока нет Java-машины, честно говорим об этом в разрешении телефона. */
static void run_game(Ui *ui, Game *g)
{
    int running = 1;

    platform_set_screen(240, 320, ui->game_scale);
    while (running) {
        Image *s = platform_screen();
        Event ev;
        int y = 34;

        while (platform_poll(&ev)) {
            if (ev.type == EV_QUIT || ev.type == EV_BACK ||
                (ev.type == EV_KEY_DOWN && ev.key == KEY_SOFT_RIGHT))
                running = 0;
            if (ev.pad)
                ui->pad_hints = 1;
            else if (ev.type == EV_KEY_DOWN)
                ui->pad_hints = 0;
        }
        gfx_vgradient(s, 0, 0, s->w, s->h, 0xFF101420u, 0xFF05060Au);
        if (g->icon.px) {
            int k = 64 / (g->icon.w > g->icon.h ? g->icon.w : g->icon.h);
            k = k < 1 ? 1 : k;
            gfx_blit_scaled(s, &g->icon, (s->w - g->icon.w * k) / 2, y, g->icon.w * k, g->icon.h * k, 255);
            y += g->icon.h * k + 12;
        } else {
            y += 76;
        }
        text_center(s, &font_big, s->w / 2, y, g->name, COL_TEXT);
        text_center(s, &font_small, s->w / 2, y + 20, g->profile[0] ? g->profile : "MIDP", COL_DIM);
        gfx_fill(s, 30, y + 42, s->w - 60, 1, 0x30FFFFFFu);
        text_center(s, &font_small, s->w / 2, y + 54, "Java-машина BT-J2ME", COL_TEXT);
        text_center(s, &font_small, s->w / 2, y + 68, "ещё в разработке.", COL_TEXT);
        text_center(s, &font_small, s->w / 2, y + 88, "Скоро здесь", COL_DIM);
        text_center(s, &font_small, s->w / 2, y + 102, "запустится сама игра.", COL_DIM);
        if (ui->pad_hints)
            draw_button(s, s->w / 2 - 38, s->h - 22, COL_PAD_B, "B", "В меню");
        else
            draw_key(s, s->w / 2 - 40, s->h - 22, "Esc", "В меню");
        platform_present();
        platform_sleep_ms(16);
    }
}

static void select_game(Ui *ui, int idx)
{
    if (ui->lib.count == 0)
        return;
    if (idx < 0)
        idx = 0;
    if (idx >= ui->lib.count)
        idx = ui->lib.count - 1;
    ui->sel = idx;
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

static int hit_case(const Ui *ui, int px, int py)
{
    int i, best = -1;

    for (i = 0; i < ui->lib.count; i++) {
        int x, y, w, h, dim;
        case_rect(ui, i, &x, &y, &w, &h, &dim);
        if (px >= x && px < x + w && py >= y && py < y + h &&
            (best < 0 || fabsf((float)i - ui->pos) < fabsf((float)best - ui->pos)))
            best = i;
    }
    return best;
}

int launcher_run(const char *games_dir, PlatformConfig *cfg)
{
    static Ui ui;
    uint32_t prev;
    int running = 1, dw = 0, dh = 0;

    memset(&ui, 0, sizeof(ui));
    ui.game_scale = cfg->scale;
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
        float dt = (float)(now - prev) / 1000.0f;
        Event ev;
        int cw, ch;

        prev = now;
        platform_display_size(&cw, &ch);
        if (cw != dw || ch != dh) {
            dw = cw;
            dh = ch;
            layout(&ui);
        }

        while (platform_poll(&ev)) {
            if (ev.pad)
                ui.pad_hints = 1;
            else if (ev.type == EV_KEY_DOWN || ev.type == EV_POINTER_DOWN)
                ui.pad_hints = 0;
            if (ev.type == EV_QUIT || (ev.type == EV_BACK && !ui.launch_t0))
                running = 0;
            if (ui.launch_t0)
                continue;
            if (ev.type == EV_PAD_ADDED) {
                snprintf(ui.toast, sizeof(ui.toast), "Геймпад: %s", ev.name ? ev.name : "подключён");
                ui.toast_until = now + 3000;
            } else if (ev.type == EV_KEY_DOWN) {
                switch (ev.key) {
                case KEY_LEFT:  case KEY_NUM0 + 4: select_game(&ui, ui.sel - 1); ui.held = -1; break;
                case KEY_RIGHT: case KEY_NUM0 + 6: select_game(&ui, ui.sel + 1); ui.held = 1; break;
                case KEY_STAR:  select_game(&ui, ui.sel - 5); break;
                case KEY_POUND: select_game(&ui, ui.sel + 5); break;
                case KEY_FIRE: case KEY_SOFT_LEFT: case KEY_NUM0 + 5:
                    if (ui.lib.count)
                        ui.launch_t0 = now;
                    break;
                }
                ui.held_since = ui.last_repeat = (int)now;
            } else if (ev.type == EV_KEY_UP && (ev.key == KEY_LEFT || ev.key == KEY_RIGHT ||
                                                ev.key == KEY_NUM0 + 4 || ev.key == KEY_NUM0 + 6)) {
                ui.held = 0;
            } else if (ev.type == EV_POINTER_DOWN) {
                int hit = hit_case(&ui, ev.x, ev.y);
                if (hit >= 0 && hit == ui.sel)
                    ui.launch_t0 = now;
                else if (hit >= 0)
                    select_game(&ui, hit);
            }
        }

        /* автоповтор при удержании — листать длинную полку */
        if (ui.held && !ui.launch_t0 && (int)now - ui.held_since > REPEAT_DELAY &&
            (int)now - ui.last_repeat > REPEAT_RATE) {
            select_game(&ui, ui.sel + ui.held);
            ui.last_repeat = (int)now;
        }
        ui.pos += ((float)ui.sel - ui.pos) * clampf(dt * 14.0f, 0, 1);
        if (fabsf((float)ui.sel - ui.pos) < 0.002f)
            ui.pos = (float)ui.sel;

        load_covers(&ui, now);
        draw_ui(&ui, now);
        platform_present();

        if (ui.launch_t0 && now - ui.launch_t0 >= LAUNCH_MS) {
            run_game(&ui, &ui.lib.games[ui.sel]);
            ui.launch_t0 = 0;
            ui.held = 0;
            layout(&ui);
            prev = platform_ticks_ms();
        }
        platform_sleep_ms(now + 16 > platform_ticks_ms() ? now + 16 - platform_ticks_ms() : 1);
    }
    library_free(&ui.lib);
    platform_shutdown();
    return 0;
}
