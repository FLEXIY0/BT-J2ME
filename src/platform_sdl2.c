#include "platform.h"
#include "compat.h"
#include "cursor.h"
#include "multitap.h"
#include "touch.h"
#include "util.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __ANDROID__
#include <jni.h>
#endif

#define QUEUE_LEN   32
#define STICK_PRESS 16000   /* гистерезис, чтобы стик не «дребезжал» у порога */
#define STICK_FREE  10000

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static Image         screen;
static ScaleMode     scale_mode;
static int           touch_on;
static volatile int  games_changed;   /* выставляет Java-часть Android после импорта */
static Event         queue[QUEUE_LEN];
static int           q_head, q_len;
static int           stick_dir[4];   /* вверх, вниз, влево, вправо */
static int           cursor_mode;
static SDL_Cursor   *hand_cursor;    /* рука в играх: видна, пока мышью пользуются */
static int           mouse_shown;
static uint32_t      mouse_seen;
static void show_mouse(int on);
static void make_hand_cursor(void);
static float         pad_ax, pad_ay; /* левый стик, -1..1 */
static SDL_AudioDeviceID audio_dev;
static AudioMixFn    audio_mix;
static SDL_Rect      game_dst;      /* куда на окне ложится экран (без сенсорных кнопок) */

/* Настройки ПК: полный экран, масштаб игр, звук, широкий экран по играм — <config>/bt-j2me/pc.cfg */
#define MAX_WIDE 64
static int           pc_full = 1, pc_scale = -1, pc_mute, pc_loaded, pc_hints = 1;
static int           pad_lb, pad_rb; /* плечевые зажаты — LB+RB прячет подсказки */
/* Фильтры картинки игры: цвет меняем в кадре игры, а строки ЭЛТ и сетку ЖК — накладкой поверх. */
enum { FL_NONE, FL_CRT, FL_COARSE, FL_GAMEBOY, FL_LCD, FL_GRAY, FL_SEPIA, FL_COUNT };
static int           pc_filter;
static uint32_t     *filt_px;       /* кадр игры после фильтра */
static size_t        filt_cap;
static SDL_Texture  *ov_tex;        /* накладка: строки ЭЛТ или сетка пикселей */
static int           ov_filter, ov_w, ov_h, ov_k;
static char          pc_wide[MAX_WIDE][128];
static char          pc_latch[MAX_WIDE][128]; /* игры с залипанием стрелок */
static int           pc_nlatch;
static char          pc_size[MAX_WIDE][128]; /* игры со своим размером экрана из меню */
static short         pc_size_w[MAX_WIDE], pc_size_h[MAX_WIDE];
static int           pc_nsize, size_dirty;
static int           pc_nwide;
static char          pc_game[128];  /* игра, что сейчас идёт (для широкого экрана) */
static volatile int  menu_open;     /* меню паузы */
static void menu_close(void);
/* Ввод текста на ПК: режим букв для клавиатуры и экранная клавиатура для геймпада. */
static int           text_wanted;   /* игра показала TextBox/поле ввода — буквы идут символами */
static int           typing;        /* режим букв: буквы → мультитап, WASD не стрелки */
static uint32_t      typing_t;
static int           osk_open;
static int           osk_mouse;     /* кнопку мыши нажали на экранной клавиатуре — отпускание тоже её */
static int           pads;          /* подключено геймпадов — подсказки рисуем под них */
static void toast_draw(void);
static int  typing_key(const SDL_Event *e);
static int  osk_event(const SDL_Event *e);
static void osk_draw(void);
static void hint_draw(void);
static void menu_show(void);
static int  menu_event(const SDL_Event *e);
static void set_fullscreen(int on);
static int  pc_wide_index(const char *id);
static void pc_load(void);
static void pc_save(void);
static int  menu_dirty;
static void render(void);

static void push(EventType type, int key)
{
    Event *e;

    if (q_len == QUEUE_LEN)
        return;
    e = &queue[(q_head + q_len++) % QUEUE_LEN];
    e->type = type;
    e->key = key;
    e->x = e->y = 0;
    e->name = NULL;
    e->pad = 0;
    e->printable = 0;
    e->ch = 0;
}

static void push_pad(EventType type, int key)
{
    push(type, key);
    if (q_len)
        queue[(q_head + q_len - 1) % QUEUE_LEN].pad = 1;
}

void platform_display_size(int *w, int *h)
{
    SDL_GetRendererOutputSize(ren, w, h);
}

static void apply_scale(void)
{
    int dw, dh, k;
    float fk;
    ScaleMode mode;

    if (touch_on) {
        /* с сенсорными кнопками картинку размещает touch.c (в портрете — сверху) */
        SDL_RenderSetLogicalSize(ren, 0, 0);
        return;
    }
    /* Место считаем сами (а не SDL_RenderSetLogicalSize): так возможно «растянуть»,
     * а координаты мыши переводим в экран игры в mouse_to_screen. */
    SDL_RenderSetLogicalSize(ren, 0, 0);
    platform_display_size(&dw, &dh);
    mode = !cursor_mode && pc_scale >= 0 ? (ScaleMode)pc_scale : scale_mode;   /* выбор из меню — для игр */
    if (mode == SCALE_STRETCH) {
        game_dst.x = game_dst.y = 0;
        game_dst.w = dw;
        game_dst.h = dh;
        return;
    }
    k = dw / screen.w < dh / screen.h ? dw / screen.w : dh / screen.h;
    fk = (float)dw / (float)screen.w < (float)dh / (float)screen.h ? (float)dw / (float)screen.w
                                                                   : (float)dh / (float)screen.h;
    if (k >= 1 && (mode == SCALE_INTEGER || (mode == SCALE_AUTO && k * screen.h * 100 >= dh * 85)))
        fk = (float)k;
    game_dst.w = (int)((float)screen.w * fk);
    game_dst.h = (int)((float)screen.h * fk);
    game_dst.x = (dw - game_dst.w) / 2;
    game_dst.y = (dh - game_dst.h) / 2;
}

/* Окно (в точках) → экран игры. */
static void mouse_to_screen(int wx, int wy, int *x, int *y)
{
    int ww, wh, dw, dh;

    SDL_GetWindowSize(win, &ww, &wh);
    platform_display_size(&dw, &dh);
    if (ww > 0 && wh > 0) {         /* на HiDPI пикселей больше, чем точек */
        wx = wx * dw / ww;
        wy = wy * dh / wh;
    }
    *x = game_dst.w > 0 ? (wx - game_dst.x) * screen.w / game_dst.w : wx;
    *y = game_dst.h > 0 ? (wy - game_dst.y) * screen.h / game_dst.h : wy;
}

/* Цвет одного пикселя после фильтра; x, y — для узора смешения у Game Boy. */
static uint32_t filter_px(uint32_t p, int x, int y)
{
    static const uint32_t gb[4] = { 0xFF0F380Fu, 0xFF306230u, 0xFF8BAC0Fu, 0xFF9BBC0Fu };
    static const int bayer[4] = { 0, 32, 48, 16 };
    int r = (int)(p >> 16 & 255), g = (int)(p >> 8 & 255), b = (int)(p & 255);
    int l = (r * 77 + g * 150 + b * 29) >> 8;

    switch (pc_filter) {
    case FL_GAMEBOY:
        l = (l * 3 + bayer[(y & 1) * 2 + (x & 1)] + 8) >> 8;
        return gb[l > 3 ? 3 : l];
    case FL_GRAY:
        return 0xFF000000u | (uint32_t)l << 16 | (uint32_t)l << 8 | (uint32_t)l;
    case FL_SEPIA:
        r = l * 112 / 100 + 18;
        g = l * 95 / 100 + 8;
        b = l * 72 / 100;
        return 0xFF000000u | (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(g > 255 ? 255 : g) << 8 | (uint32_t)b;
    case FL_COARSE:     /* по 3 бита на канал — грубая палитра, как у первых цветных телефонов */
        p &= 0xE0E0E0u;
        return 0xFF000000u | p | p >> 3 | (p & 0xC0C0C0u) >> 6;
    default:
        return p;
    }
}

/* Кадр игры с фильтром или NULL, если цвет не меняется. */
static const uint32_t *filter_frame(void)
{
    int w = screen.w, h = screen.h, x, y;
    size_t n = (size_t)w * (size_t)h;
    const uint32_t *src = screen.px;
    uint32_t *d;

    if (cursor_mode || pc_filter == FL_NONE || pc_filter == FL_LCD || !src)
        return NULL;
    if (n > filt_cap) {
        uint32_t *p = realloc(filt_px, n * 4);
        if (!p)
            return NULL;
        filt_px = p;
        filt_cap = n;
    }
    d = filt_px;
    if (pc_filter == FL_CRT) {
        /* кинескоп: соседние точки чуть сливаются, к углам темнее, общий тон ярче — строки его съедят */
        for (y = 0; y < h; y++) {
            float dy = (float)(2 * y - h) / (float)h;
            for (x = 0; x < w; x++) {
                float dx = (float)(2 * x - w) / (float)w, r = (dx * dx + dy * dy) * 0.5f, v = 1.16f - 0.3f * r * r;
                uint32_t p = src[y * w + x], q = src[y * w + (x ? x - 1 : 0)];
                int c, out = 0, sh;
                for (sh = 0; sh < 24; sh += 8) {
                    c = (int)((float)((int)(p >> sh & 255) * 3 + (int)(q >> sh & 255)) * 0.25f * v);
                    out |= (c > 255 ? 255 : c < 0 ? 0 : c) << sh;
                }
                d[y * w + x] = 0xFF000000u | (uint32_t)out;
            }
        }
    } else if (pc_filter == FL_COARSE) {
        /* грубые пиксели: каждый квадрат 2×2 — одного цвета, среднего по квадрату */
        for (y = 0; y < h; y += 2)
            for (x = 0; x < w; x += 2) {
                int x1 = x + 1 < w ? x + 1 : x, y1 = y + 1 < h ? y + 1 : y, sh;
                uint32_t a = src[y * w + x], b = src[y * w + x1], c = src[y1 * w + x], e = src[y1 * w + x1], m = 0;
                for (sh = 0; sh < 24; sh += 8)
                    m |= (((a >> sh & 255) + (b >> sh & 255) + (c >> sh & 255) + (e >> sh & 255) + 2) / 4) << sh;
                m = filter_px(m, x, y);
                d[y * w + x] = d[y * w + x1] = d[y1 * w + x] = d[y1 * w + x1] = m;
            }
    } else {
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                d[y * w + x] = filter_px(src[y * w + x], x, y);
    }
    return d;
}

static void upload(void)
{
    const uint32_t *p = filter_frame();

    SDL_UpdateTexture(tex, NULL, p ? p : screen.px, screen.w * (int)sizeof(uint32_t));
}

/* Накладка поверх картинки игры: у ЭЛТ — тёмная строка под каждой строкой игры,
 * у ЖК и Game Boy — щели между пикселями. На мелком масштабе не рисуем: вышла бы рябь. */
static void filter_overlay(const SDL_Rect *dst)
{
    int k, grid = pc_filter == FL_LCD || pc_filter == FL_GAMEBOY, tw, th, x, y;
    uint32_t *px, line;

    if (cursor_mode || !screen.px || (pc_filter != FL_CRT && !grid))
        return;
    k = dst->h / screen.h < dst->w / screen.w ? dst->h / screen.h : dst->w / screen.w;
    if (k < 2)
        return;
    if (k > (grid ? 3 : 4))
        k = grid ? 3 : 4;
    tw = grid ? screen.w * k : 1;
    th = screen.h * k;
    if (!ov_tex || ov_filter != pc_filter || ov_w != tw || ov_h != th || ov_k != k) {
        if (ov_tex)
            SDL_DestroyTexture(ov_tex);
        ov_tex = NULL;
        if (!(px = calloc((size_t)tw * (size_t)th, 4)))
            return;
        line = pc_filter == FL_CRT ? (k == 2 ? 0x70000000u : 0x9A000000u)
             : pc_filter == FL_GAMEBOY ? (k == 2 ? 0x40C4CFA1u : 0x60C4CFA1u)
             : (k == 2 ? 0x38000000u : 0x58000000u);
        for (y = 0; y < th; y++)
            for (x = 0; x < tw; x++)
                if (y % k == k - 1 || (grid && x % k == k - 1))
                    px[y * tw + x] = line;
        ov_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, tw, th);
        if (ov_tex) {
            SDL_UpdateTexture(ov_tex, NULL, px, tw * 4);
            SDL_SetTextureBlendMode(ov_tex, SDL_BLENDMODE_BLEND);
        }
        free(px);
        ov_filter = pc_filter;
        ov_w = tw;
        ov_h = th;
        ov_k = k;
    }
    if (ov_tex)
        SDL_RenderCopy(ren, ov_tex, NULL, dst);
}

int platform_set_screen(int w, int h, ScaleMode mode)
{
    if (tex)
        SDL_DestroyTexture(tex);
    image_free(&screen);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    screen = image_new(w, h);
    if (!tex || !screen.px) {
        BT_LOG("не удалось создать кадровый буфер %dx%d\n", w, h);
        return -1;
    }
    scale_mode = mode;
    apply_scale();
    return 0;
}

static void open_pad(int index)
{
    SDL_GameController *gc;

    if (!SDL_IsGameController(index) || !(gc = SDL_GameControllerOpen(index)))
        return;
    pads++;
    push_pad(EV_PAD_ADDED, 0);
    queue[(q_head + q_len - 1) % QUEUE_LEN].name = SDL_GameControllerName(gc);
}

static void touch_emit(EventType type, int key, int x, int y)
{
    push(type, type == EV_CHAR ? 0 : key);
    if (q_len) {
        Event *e = &queue[(q_head + q_len - 1) % QUEUE_LEN];
        e->x = x;
        e->y = y;
        e->ch = type == EV_CHAR ? key : 0;
    }
}

static void mt_emit(EventType type, int key)
{
    push(type, key);
}

int platform_init(const PlatformConfig *cfg)
{
    Uint32 wflags;

    pc_load();
    if (!cfg->fullscreen)
        pc_full = 0;        /* --windowed главнее сохранённого */
    wflags = pc_full ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE;
    Uint32 rflags = cfg->software ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED;

    touch_on = cfg->touch;
#ifdef __ANDROID__
    touch_on = 1;
    /* окно «растягиваемое»: иначе SDL намертво закрепляет ориентацию, в которой создано окно,
     * и экран не поворачивается; так — поворот по датчику с учётом блокировки поворота */
    wflags |= SDL_WINDOW_RESIZABLE;
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");   /* «назад» — в игру, а не закрыть приложение */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait PortraitUpsideDown LandscapeLeft LandscapeRight");
#endif
    if (touch_on) {
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");      /* пальцы — только пальцами */
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");      /* мышь на ПК изображает палец */
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        BT_LOG("SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    /* Геймпад не обязателен: без него просто работаем с клавиатуры. */
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        BT_LOG("геймпады недоступны: %s\n", SDL_GetError());
    /* Ближайший сосед: никакого «мыла» на пиксель-арте. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (cfg->software) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
    }

    win = SDL_CreateWindow(cfg->title ? cfg->title : "BT-J2ME", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, cfg->width * 2, cfg->height * 2, wflags);
    if (!win) {
        BT_LOG("SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    /* программный рендер без vsync: иначе SDL выберет OpenGL и потянет за собой Mesa/LLVM */
    ren = SDL_CreateRenderer(win, -1, cfg->software ? SDL_RENDERER_SOFTWARE : rflags | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        BT_LOG("SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    if (!touch_on)
        make_hand_cursor();
    show_mouse(0);
    if (touch_on)
        touch_init(ren, touch_emit);
    mt_set_emit(mt_emit);
    /* Уже подключённые геймпады придут событиями SDL_CONTROLLERDEVICEADDED. */
    return platform_set_screen(cfg->width, cfg->height, cfg->scale);
}

void platform_shutdown(void)
{
    if (hand_cursor)
        SDL_FreeCursor(hand_cursor);
    hand_cursor = NULL;
    if (touch_on)
        touch_shutdown();
    platform_audio_close();
    image_free(&screen);
    free(filt_px);
    filt_px = NULL;
    filt_cap = 0;
    if (ov_tex)
        SDL_DestroyTexture(ov_tex);
    ov_tex = NULL;
    if (tex)
        SDL_DestroyTexture(tex);
    if (ren)
        SDL_DestroyRenderer(ren);
    if (win)
        SDL_DestroyWindow(win);
    tex = NULL;
    ren = NULL;
    win = NULL;
    SDL_Quit();
}

Image *platform_screen(void)
{
    return &screen;
}

static void show_mouse(int on)
{
    mouse_shown = on;
    SDL_ShowCursor(on ? SDL_ENABLE : SDL_DISABLE);
}

/* Системный курсор — та же пиксельная рука, что в лаунчере, крупнее на больших экранах. */
static void make_hand_cursor(void)
{
    SDL_DisplayMode dm;
    SDL_Surface *sf;
    int s = 2, x, y;

    if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
        s = dm.h >= 1400 ? 3 : dm.h >= 700 ? 2 : 1;
    sf = SDL_CreateRGBSurfaceWithFormat(0, CURSOR_W * s, CURSOR_H * s, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!sf)
        return;
    for (y = 0; y < CURSOR_H * s; y++)
        for (x = 0; x < CURSOR_W * s; x++) {
            char c = cursor_hand[y / s][x / s];
            ((Uint32 *)((Uint8 *)sf->pixels + y * sf->pitch))[x] =
                c == '#' ? 0xFF1B1622u : c == '-' ? 0xFFC9C0D6u : c == 'o' ? 0xFFFFFFFFu : 0;
        }
    hand_cursor = SDL_CreateColorCursor(sf, CURSOR_HX * s, 0);
    SDL_FreeSurface(sf);
    if (hand_cursor)
        SDL_SetCursor(hand_cursor);
}

void platform_cursor_mode(int on)
{
    int d;

    cursor_mode = on;
    for (d = 0; d < 4; d++)         /* стрелки, зажатые стиком, отпускаем */
        stick_dir[d] = 0;
    show_mouse(0);      /* в лаунчере рука рисуется сама; в игре появится, когда тронут мышь */
}

void platform_stick(float *x, float *y)
{
    *x = pad_ax;
    *y = pad_ay;
}

static int      fade_ms;
static uint32_t fade_t0;

void platform_fade_in(int ms)
{
    fade_ms = ms;
    fade_t0 = 0;        /* отсчёт — с первого кадра игры: JVM ещё грузится */
}

static void menu_draw(void);

static void render(void)
{
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);   /* поля вокруг экрана — чёрные */
    SDL_RenderClear(ren);
    if (touch_on) {
        SDL_Rect dst;
        int w, h;
        platform_display_size(&w, &h);
        touch_place(w, h, screen.w, screen.h, scale_mode, &dst);
        SDL_RenderCopy(ren, tex, NULL, &dst);
        filter_overlay(&dst);
        touch_draw();
    } else {
        SDL_RenderCopy(ren, tex, NULL, &game_dst);
        filter_overlay(&game_dst);
        if (osk_open)
            osk_draw();
        else if (typing)
            hint_draw();
        else if (pads && pc_hints && !cursor_mode && !menu_open && screen.px)
            toast_draw();
        if (menu_open)
            menu_draw();
    }
    if (fade_ms > 0) {
        uint32_t now = SDL_GetTicks();
        float f;
        if (!fade_t0)
            fade_t0 = now;
        f = 1 - (float)(now - fade_t0) / (float)fade_ms;
        if (f <= 0) {
            fade_ms = 0;
        } else {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)(f * f * 255));
            SDL_RenderFillRect(ren, NULL);
        }
    }
    SDL_RenderPresent(ren);
}

void platform_present(void)
{
    upload();
    render();
}

void platform_touch_profile(const char *game_id)
{
    if (touch_on)
        touch_profile(game_id);
    snprintf(pc_game, sizeof(pc_game), "%s", game_id ? game_id : "");
    if (!game_id) {
        menu_close();
        osk_open = 0;
        typing = 0;
    }
}

int platform_touch_wide(void)
{
    if (touch_on)
        return touch_wide();
    return pc_game[0] && pc_wide_index(pc_game) >= 0;
}

#ifdef __ANDROID__
/* Вызов метода MainActivity без аргументов. */
static void activity_call(const char *method)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    jclass cls;
    jmethodID m;

    if (!env || !act)
        return;
    cls = (*env)->GetObjectClass(env, act);
    m = (*env)->GetMethodID(env, cls, method, "()V");
    if (m)
        (*env)->CallVoidMethod(env, act, m);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
}

/* MainActivity сообщает: скопировали новые игры в папку. */
JNIEXPORT void JNICALL Java_ru_flexiy0_btj2me_MainActivity_nativeGamesChanged(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    games_changed = 1;
}

int platform_open_url(const char *url)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    jclass cls;
    jmethodID m;
    jstring s;
    int ok = 0;

    if (!env || !act)
        return 0;
    cls = (*env)->GetObjectClass(env, act);
    m = (*env)->GetMethodID(env, cls, "openUrl", "(Ljava/lang/String;)Z");
    s = (*env)->NewStringUTF(env, url);
    if (m && s)
        ok = (*env)->CallBooleanMethod(env, act, m, s);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        ok = 0;
    }
    if (s)
        (*env)->DeleteLocalRef(env, s);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
    return ok;
}

int platform_can_import(void) { return 1; }
void platform_import_games(void) { activity_call("importGames"); }

/* Своя выдвижная клавиатура вместо системной: одинаковая на всех версиях Android. */
void platform_text_input(int on)
{
    touch_text_input(on);
}
#else
/* На ПК игра браузер не открывает — только пишет, куда хотела. */
int platform_open_url(const char *url)
{
    BT_LOG("игра просит открыть %s\n", url);
    return 0;
}

int platform_can_import(void) { return 0; }
void platform_import_games(void) {}
void platform_text_input(int on)
{
    if (touch_on)
        touch_text_input(on);   /* на ПК с --touch — та же клавиатура; обычный ввод с клавиатуры есть всегда */
    text_wanted = on;
    typing = 0;
}
#endif

void platform_vibrate(int ms)
{
    if (touch_on)
        touch_vibrate(ms);
}

/* Раскладка: стрелки/WASD — джойстик, Enter/пробел — огонь, цифры и numpad — как на телефоне,
 * F1/F2 (или Q/E) — софт-клавиши. */
static int map_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP:    case SDLK_w: return KEY_UP;
    case SDLK_DOWN:  case SDLK_s: return KEY_DOWN;
    case SDLK_LEFT:  case SDLK_a: return KEY_LEFT;
    case SDLK_RIGHT: case SDLK_d: return KEY_RIGHT;
    case SDLK_RETURN: case SDLK_SPACE: case SDLK_KP_ENTER: return KEY_FIRE;
    case SDLK_F1: case SDLK_q: return KEY_SOFT_LEFT;
    case SDLK_F2: case SDLK_e: return KEY_SOFT_RIGHT;
    case SDLK_KP_MULTIPLY: case SDLK_ASTERISK: return KEY_STAR;
    case SDLK_KP_DIVIDE: case SDLK_HASH: return KEY_POUND;
    case SDLK_KP_0: return KEY_NUM0;
    case SDLK_BACKSPACE: case SDLK_DELETE: return KEY_CLEAR;
    default:
        if (k >= SDLK_0 && k <= SDLK_9)
            return KEY_NUM0 + (int)(k - SDLK_0);
        if (k >= SDLK_KP_1 && k <= SDLK_KP_9)
            return KEY_NUM0 + 1 + (int)(k - SDLK_KP_1);
        return 0;
    }
}

/* Геймпад Xbox (и любой, известный SDL): A — огонь, B — правая софт-клавиша («назад»),
 * Start — левая софт-клавиша (меню/пауза), X/Y — 1/3, LB/RB — * и #, View — 0,
 * кнопка Xbox — выход в меню. */
static int map_button(Uint8 b)
{
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:       return KEY_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return KEY_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return KEY_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return KEY_RIGHT;
    case SDL_CONTROLLER_BUTTON_A:             return KEY_FIRE;
    case SDL_CONTROLLER_BUTTON_B:             return KEY_SOFT_RIGHT;
    case SDL_CONTROLLER_BUTTON_START:         return KEY_SOFT_LEFT;
    case SDL_CONTROLLER_BUTTON_X:             return KEY_NUM0 + 1;
    case SDL_CONTROLLER_BUTTON_Y:             return KEY_NUM0 + 3;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return KEY_STAR;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return KEY_POUND;
    case SDL_CONTROLLER_BUTTON_BACK:          return KEY_NUM0;
    default:                                  return 0;
    }
}

static void stick_axis(Sint16 v, int neg_dir, int pos_dir, int neg_key, int pos_key)
{
    int want_neg = stick_dir[neg_dir] ? v < -STICK_FREE : v < -STICK_PRESS;
    int want_pos = stick_dir[pos_dir] ? v > STICK_FREE : v > STICK_PRESS;

    if (want_neg != stick_dir[neg_dir])
        push_pad(want_neg ? EV_KEY_DOWN : EV_KEY_UP, neg_key);
    if (want_pos != stick_dir[pos_dir])
        push_pad(want_pos ? EV_KEY_DOWN : EV_KEY_UP, pos_key);
    stick_dir[neg_dir] = want_neg;
    stick_dir[pos_dir] = want_pos;
}

static void translate(const SDL_Event *e)
{
    int key;

    if ((e->type == SDL_CONTROLLERBUTTONDOWN || e->type == SDL_CONTROLLERBUTTONUP) &&
        (e->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ||
         e->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
        int down = e->type == SDL_CONTROLLERBUTTONDOWN, was = pad_lb && pad_rb;
        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
            pad_lb = down;
        else
            pad_rb = down;
        if (!was && pad_lb && pad_rb) {     /* LB+RB вместе — спрятать или вернуть подсказки */
            pc_hints = !pc_hints;
            pc_save();
            menu_dirty = 1;
            if (tex)
                render();
        }
    }
    if (menu_open && menu_event(e))
        return;                         /* меню паузы забирает ввод себе */
    if (!touch_on && !cursor_mode && (osk_open || osk_mouse) && osk_event(e))
        return;                         /* экранная клавиатура геймпада */
    if (!touch_on && !cursor_mode && typing_key(e))
        return;                         /* режим букв */
    if (!touch_on && !cursor_mode) {    /* в игре рука видна, только пока мышью пользуются */
        if (e->type == SDL_MOUSEMOTION || e->type == SDL_MOUSEBUTTONDOWN) {
            mouse_seen = SDL_GetTicks();
            if (!mouse_shown)
                show_mouse(1);
        } else if (mouse_shown && (e->type == SDL_KEYDOWN || e->type == SDL_CONTROLLERBUTTONDOWN)) {
            show_mouse(0);
        }
    }
    switch (e->type) {
    case SDL_DROPFILE: {
        static char paths[4][1024];     /* путь живёт, пока событие в очереди */
        static int next;
        char *dst = paths[next++ & 3];
        snprintf(dst, sizeof(paths[0]), "%s", e->drop.file ? e->drop.file : "");
        SDL_free(e->drop.file);
        push(EV_DROP, 0);
        queue[(q_head + q_len - 1) % QUEUE_LEN].name = dst;
        break;
    }
    case SDL_QUIT:
        push(EV_QUIT, 0);
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            apply_scale();
        break;
    case SDL_APP_DIDENTERFOREGROUND:
        push(EV_REFRESH, 0);    /* вернулись из файлового менеджера — вдруг там добавили игры */
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        if (e->key.keysym.sym == SDLK_F10) {
            push(EV_QUIT, 0);
            break;
        }
        if (e->key.keysym.sym == SDLK_F12) {
            if (e->type == SDL_KEYDOWN && !e->key.repeat)
                push(EV_SCREENSHOT, 0);
            break;
        }
        if (e->key.keysym.sym == SDLK_F11 ||
            (e->key.keysym.sym == SDLK_RETURN && (e->key.keysym.mod & KMOD_ALT))) {
            if (e->type == SDL_KEYDOWN && !e->key.repeat)
                set_fullscreen(!pc_full);
            break;
        }
        if (e->key.keysym.sym == SDLK_ESCAPE || e->key.keysym.sym == SDLK_AC_BACK) {
            if (e->type == SDL_KEYDOWN && !e->key.repeat) {
                if (!touch_on && !cursor_mode && screen.px)
                    menu_show();        /* в игре на ПК — меню паузы, выход на полку в нём */
                else
                    push(EV_BACK, 0);
            }
            break;
        }
        if (e->key.repeat || !(key = map_key(e->key.keysym.sym)))
            break;
        push(e->type == SDL_KEYDOWN ? EV_KEY_DOWN : EV_KEY_UP, key);
        queue[(q_head + q_len - 1) % QUEUE_LEN].printable = e->key.keysym.sym >= 32 && e->key.keysym.sym < 127;
        break;
    case SDL_TEXTINPUT: {
        const unsigned char *u = (const unsigned char *)e->text.text;
        int cp = u[0];
        if ((u[0] & 0xE0) == 0xC0 && u[1])
            cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        else if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2])
            cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        if (cp >= 32 && cp < 0x10000) {
            push(EV_CHAR, 0);
            queue[(q_head + q_len - 1) % QUEUE_LEN].ch = cp;
        }
        break;
    }
    case SDL_CONTROLLERDEVICEADDED:
        open_pad(e->cdevice.which);
        break;
    case SDL_CONTROLLERDEVICEREMOVED: {
        SDL_GameController *gc = SDL_GameControllerFromInstanceID(e->cdevice.which);
        if (gc) {
            SDL_GameControllerClose(gc);
            if (pads > 0)
                pads--;
        }
        break;
    }
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        if ((e->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSTICK ||
             e->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) && !touch_on && !cursor_mode) {
            if (e->type == SDL_CONTROLLERBUTTONDOWN) {
                osk_open = 1;           /* нажатие на стик — экранная клавиатура */
                if (tex)
                    render();
            }
        } else if (e->cbutton.button == SDL_CONTROLLER_BUTTON_BACK && !touch_on && !cursor_mode) {
            if (e->type == SDL_CONTROLLERBUTTONDOWN)
                menu_show();            /* View в игре — меню паузы (кнопку Xbox часто забирает Windows) */
        } else if (e->cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
            if (e->type == SDL_CONTROLLERBUTTONDOWN) {
                if (!touch_on && !cursor_mode)
                    menu_show();
                else
                    push_pad(EV_BACK, 0);
            }
        } else if ((key = map_button(e->cbutton.button)) != 0) {
            push_pad(e->type == SDL_CONTROLLERBUTTONDOWN ? EV_KEY_DOWN : EV_KEY_UP, key);
        }
        break;
    case SDL_CONTROLLERAXISMOTION:
        if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
            pad_ax = (float)e->caxis.value / 32767.0f;
        else if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
            pad_ay = (float)e->caxis.value / 32767.0f;
        if (cursor_mode)
            break;      /* стик водит курсор лаунчера */
        if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
            stick_axis(e->caxis.value, 2, 3, KEY_LEFT, KEY_RIGHT);
        else if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
            stick_axis(e->caxis.value, 0, 1, KEY_UP, KEY_DOWN);
        break;
    case SDL_FINGERDOWN:
    case SDL_FINGERMOTION:
    case SDL_FINGERUP:
        if (touch_on)
            touch_event(e);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (touch_on)
            break;      /* мышь уже пришла пальцем */
        if (e->button.button != SDL_BUTTON_LEFT)
            break;
        push(e->type == SDL_MOUSEBUTTONDOWN ? EV_POINTER_DOWN : EV_POINTER_UP, 0);
        mouse_to_screen(e->button.x, e->button.y, &queue[(q_head + q_len - 1) % QUEUE_LEN].x,
                        &queue[(q_head + q_len - 1) % QUEUE_LEN].y);
        break;
    case SDL_MOUSEMOTION:
        if (!touch_on && ((e->motion.state & SDL_BUTTON_LMASK) || cursor_mode)) {
            push(e->motion.state & SDL_BUTTON_LMASK ? EV_POINTER_MOVE : EV_HOVER, 0);
            mouse_to_screen(e->motion.x, e->motion.y, &queue[(q_head + q_len - 1) % QUEUE_LEN].x,
                            &queue[(q_head + q_len - 1) % QUEUE_LEN].y);
        }
        break;
    }
}

int platform_poll(Event *ev)
{
    SDL_Event e;

    if (games_changed) {
        games_changed = 0;
        push(EV_REFRESH, 0);
    }
    while (!q_len && SDL_PollEvent(&e))
        translate(&e);
    if (mouse_shown && !cursor_mode && !menu_open && SDL_GetTicks() - mouse_seen > 2500 &&
        !(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK))
        show_mouse(0);
    mt_tick(SDL_GetTicks());
    if (typing && !osk_open && SDL_GetTicks() - typing_t > 8000) {
        typing = 0;                 /* давно не печатали — клавиши снова управление */
        if (tex)
            render();
    }
    if (touch_on) {
        if (touch_take_restart())
            push(EV_RESTART, 0);
        if (touch_take_shot())
            push(EV_SCREENSHOT, 0);
    }
    if (((touch_on && touch_dirty()) || (fade_ms > 0 && fade_t0)) && tex)
        render();       /* кнопка нажалась, стик сдвинулся или игра проявляется, а кадр игра не прислала */
    if (!q_len)
        return 0;
    *ev = queue[q_head];
    q_head = (q_head + 1) % QUEUE_LEN;
    q_len--;
    return 1;
}

uint32_t platform_ticks_ms(void)
{
    return SDL_GetTicks();
}

void platform_sleep_ms(uint32_t ms)
{
    SDL_Delay(ms);
}

/* ---------- звук ---------- */

static void SDLCALL audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    if (pc_mute || menu_open)           /* звук выключен в меню или игра на паузе */
        memset(stream, 0, (size_t)len);
    else
        audio_mix((int16_t *)stream, len / 2);
}

int platform_audio_open(int rate, AudioMixFn mix)
{
    SDL_AudioSpec want, have;

    if (audio_dev)
        return 0;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        BT_LOG("звук недоступен: %s\n", SDL_GetError());
        return 0;
    }
    SDL_zero(want);
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;    /* ~46 мс: без треска на слабых машинах, задержка незаметна */
    want.callback = audio_cb;
    audio_mix = mix;
    /* частоту подстраиваем под карту сами, формат и каналы пусть конвертирует SDL */
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!audio_dev) {
        BT_LOG("не удалось открыть звук: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }
    SDL_PauseAudioDevice(audio_dev, 0);
    return have.freq;
}

void platform_audio_lock(int on)
{
    if (!audio_dev)
        return;
    if (on)
        SDL_LockAudioDevice(audio_dev);
    else
        SDL_UnlockAudioDevice(audio_dev);
}

void platform_audio_close(void)
{
    if (!audio_dev)
        return;
    SDL_CloseAudioDevice(audio_dev);
    audio_dev = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

/* ---------- настройки ПК и меню паузы ---------- */

static void pc_path(char *out, size_t cap, int mkdirs)
{
    const char *xdg = getenv("XDG_CONFIG_HOME"), *app = bt_appdata();
    char dir[1024];

    if (xdg && *xdg)
        snprintf(dir, sizeof(dir), "%s/bt-j2me", xdg);
    else if (app)
        snprintf(dir, sizeof(dir), "%s/bt-j2me", app);
    else
        snprintf(dir, sizeof(dir), "%s/.config/bt-j2me", bt_home());
    if (mkdirs)
        bt_mkdirs(dir);
    snprintf(out, cap, "%s/pc.cfg", dir);
}

static void pc_load(void)
{
    char path[1100], line[256];
    FILE *f;

    pc_loaded = 1;
    pc_path(path, sizeof(path), 0);
    if (!(f = bt_fopen(path, "rb")))
        return;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strncmp(line, "fullscreen ", 11))
            pc_full = atoi(line + 11) != 0;
        else if (!strncmp(line, "scale ", 6))
            pc_scale = atoi(line + 6);
        else if (!strncmp(line, "hints ", 6))
            pc_hints = atoi(line + 6) != 0;
        else if (!strncmp(line, "filter ", 7))
            pc_filter = atoi(line + 7);
        else if (!strncmp(line, "mute ", 5))
            pc_mute = atoi(line + 5) != 0;
        else if (!strncmp(line, "size ", 5) && pc_nsize < MAX_WIDE) {
            int w, h, n = 0;
            if (sscanf(line + 5, "%dx%d %n", &w, &h, &n) == 2 && n > 0 && w >= 96 && h >= 64 && w <= 640 && h <= 640) {
                snprintf(pc_size[pc_nsize], sizeof(pc_size[0]), "%.127s", line + 5 + n);
                pc_size_w[pc_nsize] = (short)w;
                pc_size_h[pc_nsize++] = (short)h;
            }
        } else if (!strncmp(line, "latch ", 6) && pc_nlatch < MAX_WIDE)
            snprintf(pc_latch[pc_nlatch++], sizeof(pc_latch[0]), "%.127s", line + 6);
        else if (!strncmp(line, "wide ", 5) && pc_nwide < MAX_WIDE)
            snprintf(pc_wide[pc_nwide++], sizeof(pc_wide[0]), "%.127s", line + 5);
    }
    fclose(f);
    if (pc_scale < -1 || pc_scale > SCALE_STRETCH)
        pc_scale = -1;
    if (pc_filter < 0 || pc_filter >= FL_COUNT)
        pc_filter = FL_NONE;
}

static void pc_save(void)
{
    char path[1100];
    FILE *f;
    int i;

    pc_path(path, sizeof(path), 1);
    if (!(f = bt_fopen(path, "wb")))
        return;
    fprintf(f, "fullscreen %d\nscale %d\nmute %d\nhints %d\nfilter %d\n", pc_full, pc_scale, pc_mute, pc_hints,
            pc_filter);
    for (i = 0; i < pc_nwide; i++)
        fprintf(f, "wide %s\n", pc_wide[i]);
    for (i = 0; i < pc_nlatch; i++)
        fprintf(f, "latch %s\n", pc_latch[i]);
    for (i = 0; i < pc_nsize; i++)
        fprintf(f, "size %dx%d %s\n", pc_size_w[i], pc_size_h[i], pc_size[i]);
    fclose(f);
}

static int pc_size_index(const char *id)
{
    int i;

    for (i = 0; i < pc_nsize; i++)
        if (!strcmp(pc_size[i], id))
            return i;
    return -1;
}

int platform_game_size(const char *id, int *w, int *h)
{
    int i;

    if (!pc_loaded)
        pc_load();
    if (touch_on || !id || (i = pc_size_index(id)) < 0)
        return 0;
    *w = pc_size_w[i];
    *h = pc_size_h[i];
    return 1;
}

/* Язык системы для microedition.locale: «ru-RU», «en-US»… BT_LOCALE — подменить. */
const char *platform_locale(void)
{
    static char loc[16];
    const char *env = getenv("BT_LOCALE");
#if SDL_VERSION_ATLEAST(2, 0, 14)
    SDL_Locale *l;
#endif

    if (env && *env)
        return env;
    if (loc[0])
        return loc;
    snprintf(loc, sizeof(loc), "en-US");
#if SDL_VERSION_ATLEAST(2, 0, 14)
    if ((l = SDL_GetPreferredLocales()) != NULL) {
        if (l[0].language && strlen(l[0].language) == 2)
            snprintf(loc, sizeof(loc), "%s%s%s", l[0].language, l[0].country ? "-" : "",
                     l[0].country ? l[0].country : "");
        SDL_free(l);
    }
#endif
    return loc;
}

static int pc_latch_index(const char *id)
{
    int i;

    for (i = 0; i < pc_nlatch; i++)
        if (!strcmp(pc_latch[i], id))
            return i;
    return -1;
}

int platform_latch(void)
{
    if (touch_on)
        return touch_latch();
    return pc_game[0] && pc_latch_index(pc_game) >= 0;
}

static int pc_wide_index(const char *id)
{
    int i;

    for (i = 0; i < pc_nwide; i++)
        if (!strcmp(pc_wide[i], id))
            return i;
    return -1;
}

static void set_fullscreen(int on)
{
    pc_full = on;
    SDL_SetWindowFullscreen(win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    pc_save();
    apply_scale();
}

/* Пункты меню. */
enum { MI_RESUME, MI_FULL, MI_SCALE, MI_FILTER, MI_SIZE, MI_WIDE, MI_LATCH, MI_SOUND, MI_HINTS, MI_SHOT, MI_EXIT, MI_COUNT };

#define MENU_W     236
#define MENU_ROW   18
#define MENU_TOP   30
#define MENU_H     (MENU_TOP + MI_COUNT * MENU_ROW + 24)

static int          menu_sel, menu_k, menu_stick;
static Image        menu_img;
static SDL_Texture *menu_tex;
static SDL_Rect     menu_dst;

static void menu_label(int i, char *out, size_t cap)
{
    static const char *const scales[] = { "чёткий", "по размеру", "авто", "растянуть" };
    static const char *const filters[FL_COUNT] = { "нет", "ЭЛТ", "грубые пиксели", "Game Boy", "ЖК-экран",
                                                   "ч/б", "сепия" };
    int sc = pc_scale >= 0 ? pc_scale : (int)scale_mode;

    switch (i) {
    case MI_RESUME: snprintf(out, cap, "Продолжить"); break;
    case MI_FULL:   snprintf(out, cap, "Экран: %s", pc_full ? "полный" : "окно"); break;
    case MI_SCALE:  snprintf(out, cap, "Масштаб: %s", scales[sc < 0 || sc > 3 ? 2 : sc]); break;
    case MI_FILTER: snprintf(out, cap, "Фильтр: %s", filters[pc_filter]); break;
    case MI_SIZE: {
        int k = pc_size_index(pc_game);
        if (k < 0)
            snprintf(out, cap, "Экран игры: авто");
        else
            snprintf(out, cap, "Экран игры: %d×%d", pc_size_w[k], pc_size_h[k]);
        break;
    }
    case MI_WIDE:   snprintf(out, cap, "Широкий экран: %s", pc_wide_index(pc_game) >= 0 ? "да" : "нет"); break;
    case MI_LATCH:  snprintf(out, cap, "Залипание стрелок: %s", pc_latch_index(pc_game) >= 0 ? "да" : "нет"); break;
    case MI_SOUND:  snprintf(out, cap, "Звук: %s", pc_mute ? "выкл" : "вкл"); break;
    case MI_HINTS:  snprintf(out, cap, "Подсказки геймпада: %s", pc_hints ? "да" : "нет"); break;
    case MI_SHOT:   snprintf(out, cap, "Снимок экрана"); break;
    default:        snprintf(out, cap, "Выйти на полку"); break;
    }
}

static void menu_show(void)
{
    if (menu_open)
        return;
    menu_open = 1;
    menu_sel = MI_RESUME;
    menu_dirty = 1;
    show_mouse(1);
    if (hand_cursor)
        SDL_SetCursor(hand_cursor);
    if (tex)
        render();       /* игра на паузе кадров не шлёт — рисуем сами */
}

static void menu_close(void)
{
    if (!menu_open)
        return;
    menu_open = 0;
    show_mouse(0);
    if (size_dirty && pc_game[0])
        push(EV_RESTART, 0);    /* размер экрана игры меняется только с перезапуском */
    size_dirty = 0;
    if (tex)
        render();
}

static void menu_draw(void)
{
    int dw, dh, i;

    if (!menu_img.px) {
        menu_img = image_new(MENU_W, MENU_H);
        if (!menu_img.px)
            return;
    }
    if (!menu_tex) {
        menu_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, MENU_W, MENU_H);
        if (!menu_tex)
            return;
        SDL_SetTextureBlendMode(menu_tex, SDL_BLENDMODE_BLEND);
        menu_dirty = 1;
    }
    if (menu_dirty) {
        Image *m = &menu_img;
        char s[64];
        menu_dirty = 0;
        gfx_fill(m, 0, 0, MENU_W, MENU_H, 0xF21A1724u);
        gfx_frame(m, 0, 0, MENU_W, MENU_H, 0xFF3A3450u);
        gfx_text(m, &font_big, (MENU_W - gfx_text_width(&font_big, "Пауза", 1)) / 2, 7, "Пауза", 0xFFEDE8F2u, 1);
        gfx_fill(m, 10, MENU_TOP - 4, MENU_W - 20, 1, 0xFF3A3450u);
        for (i = 0; i < MI_COUNT; i++) {
            int y = MENU_TOP + i * MENU_ROW;
            menu_label(i, s, sizeof(s));
            if (i == menu_sel)
                gfx_fill(m, 6, y, MENU_W - 12, MENU_ROW - 2, 0xFF2E2944u);
            if (i == menu_sel)
                gfx_fill(m, 6, y, 2, MENU_ROW - 2, 0xFF9C8FF0u);
            gfx_text(m, &font_mid, 14, y + 1, s, i == menu_sel ? 0xFFF4F0FFu : 0xFF8F8899u, 1);
        }
        {   /* внизу — какие кнопки что делают */
            const char *lg = pads ? "A\tвыбрать\tB\tназад\tView\tзакрыть"
                                  : "[Enter]\tок\t[<][>]\tменять\t[Esc]\tзакрыть";
            int lw = gfx_hints(NULL, &font_small, 0, 0, lg, 0);
            gfx_hints(m, &font_small, (MENU_W - lw) / 2, MENU_H - font_small.h - 7, lg, 0xFF8F8899u);
        }
        SDL_UpdateTexture(menu_tex, NULL, menu_img.px, MENU_W * 4);
    }
    platform_display_size(&dw, &dh);
    menu_k = dw * 7 / 10 / MENU_W < dh * 8 / 10 / MENU_H ? dw * 7 / 10 / MENU_W : dh * 8 / 10 / MENU_H;
    if (menu_k < 1)
        menu_k = 1;
    menu_dst.w = MENU_W * menu_k;
    menu_dst.h = MENU_H * menu_k;
    menu_dst.x = (dw - menu_dst.w) / 2;
    menu_dst.y = (dh - menu_dst.h) / 2;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 150);  /* игра темнеет под меню */
    SDL_RenderFillRect(ren, NULL);
    SDL_RenderCopy(ren, menu_tex, NULL, &menu_dst);
}

/* Строка меню под мышью или -1. */
static int menu_hit(int wx, int wy)
{
    int ww, wh, dw, dh, row;

    SDL_GetWindowSize(win, &ww, &wh);
    platform_display_size(&dw, &dh);
    if (ww > 0 && wh > 0) {
        wx = wx * dw / ww;
        wy = wy * dh / wh;
    }
    if (menu_k < 1 || wx < menu_dst.x || wx >= menu_dst.x + menu_dst.w)
        return -1;
    row = ((wy - menu_dst.y) / menu_k - MENU_TOP);
    if (row < 0 || row / MENU_ROW >= MI_COUNT)
        return -1;
    return row / MENU_ROW;
}

static void menu_activate(int dir)
{
    switch (menu_sel) {
    case MI_RESUME:
        menu_close();
        return;
    case MI_FULL:
        set_fullscreen(!pc_full);
        break;
    case MI_SCALE: {
        /* авто → чёткий → по размеру → растянуть */
        static const int order[] = { SCALE_AUTO, SCALE_INTEGER, SCALE_FIT, SCALE_STRETCH };
        int cur = pc_scale >= 0 ? pc_scale : (int)scale_mode, i, n = 4;
        for (i = 0; i < n && order[i] != cur; i++)
            ;
        pc_scale = order[((i < n ? i : 0) + (dir < 0 ? n - 1 : 1)) % n];
        pc_save();
        apply_scale();
        break;
    }
    case MI_SIZE: {
        /* размеры экранов кнопочных телефонов; «авто» — какой нашёлся в игре */
        static const short sizes[][2] = { { 0, 0 }, { 128, 160 }, { 176, 208 }, { 176, 220 }, { 240, 320 },
                                          { 240, 400 }, { 320, 240 }, { 360, 640 }, { 480, 640 } };
        int n = (int)(sizeof(sizes) / sizeof(sizes[0])), k = pc_size_index(pc_game), cur = 0, i;
        if (!pc_game[0])
            break;
        for (i = 1; k >= 0 && i < n; i++)
            if (sizes[i][0] == pc_size_w[k] && sizes[i][1] == pc_size_h[k])
                cur = i;
        cur = (cur + (dir < 0 ? n - 1 : 1)) % n;
        if (k >= 0) {
            memmove(&pc_size[k], &pc_size[k + 1], (size_t)(pc_nsize - k - 1) * sizeof(pc_size[0]));
            memmove(&pc_size_w[k], &pc_size_w[k + 1], (size_t)(pc_nsize - k - 1) * sizeof(pc_size_w[0]));
            memmove(&pc_size_h[k], &pc_size_h[k + 1], (size_t)(pc_nsize - k - 1) * sizeof(pc_size_h[0]));
            pc_nsize--;
        }
        if (cur && pc_nsize < MAX_WIDE) {
            snprintf(pc_size[pc_nsize], sizeof(pc_size[0]), "%s", pc_game);
            pc_size_w[pc_nsize] = sizes[cur][0];
            pc_size_h[pc_nsize++] = sizes[cur][1];
        }
        pc_save();
        size_dirty = 1;         /* игра перезапустится, когда закроют меню */
        break;
    }
    case MI_FILTER:
        pc_filter = (pc_filter + (dir < 0 ? FL_COUNT - 1 : 1)) % FL_COUNT;
        pc_save();
        if (tex && screen.px)
            upload();           /* игра на паузе — перекрашиваем последний её кадр */
        break;
    case MI_WIDE: {
        int i = pc_wide_index(pc_game);
        if (!pc_game[0])
            break;
        if (i >= 0)
            memmove(pc_wide[i], pc_wide[i + 1], (size_t)(pc_nwide-- - i - 1) * sizeof(pc_wide[0]));
        else if (pc_nwide < MAX_WIDE)
            snprintf(pc_wide[pc_nwide++], sizeof(pc_wide[0]), "%s", pc_game);
        pc_save();
        menu_close();
        push(EV_RESTART, 0);        /* размер экрана игры меняется только с перезапуском */
        return;
    }
    case MI_LATCH: {
        int i = pc_latch_index(pc_game);
        if (!pc_game[0])
            break;
        if (i >= 0)
            memmove(pc_latch[i], pc_latch[i + 1], (size_t)(pc_nlatch-- - i - 1) * sizeof(pc_latch[0]));
        else if (pc_nlatch < MAX_WIDE)
            snprintf(pc_latch[pc_nlatch++], sizeof(pc_latch[0]), "%s", pc_game);
        pc_save();
        break;
    }
    case MI_SOUND:
        pc_mute = !pc_mute;
        pc_save();
        break;
    case MI_HINTS:
        pc_hints = !pc_hints;
        pc_save();
        break;
    case MI_SHOT:
        menu_close();
        push(EV_SCREENSHOT, 0);
        return;
    default:
        menu_close();
        push(EV_BACK, 0);
        return;
    }
    menu_dirty = 1;
    render();
}

/* Пункт меняется стрелками влево-вправо. */
static int menu_toggle(int i)
{
    return i == MI_SCALE || i == MI_FILTER || i == MI_SIZE || i == MI_FULL || i == MI_SOUND || i == MI_LATCH || i == MI_HINTS;
}

static void menu_move(int d)
{
    menu_sel = (menu_sel + d + MI_COUNT) % MI_COUNT;
    menu_dirty = 1;
    render();
}

/* Ввод, пока меню открыто: 1 — событие съедено. */
static int menu_event(const SDL_Event *e)
{
    int i;

    switch (e->type) {
    case SDL_KEYDOWN:
        switch (e->key.keysym.sym) {
        case SDLK_ESCAPE: case SDLK_AC_BACK: case SDLK_BACKSPACE:
            if (!e->key.repeat)
                menu_close();
            return 1;
        case SDLK_UP: case SDLK_w: menu_move(-1); return 1;
        case SDLK_DOWN: case SDLK_s: menu_move(1); return 1;
        case SDLK_LEFT: case SDLK_a:
            if (menu_toggle(menu_sel))
                menu_activate(-1);
            return 1;
        case SDLK_RIGHT: case SDLK_d:
            if (menu_toggle(menu_sel))
                menu_activate(1);
            return 1;
        case SDLK_RETURN: case SDLK_SPACE: case SDLK_KP_ENTER:
            if (e->key.keysym.mod & KMOD_ALT)
                return 0;               /* Alt+Enter — полный экран, как везде */
            if (!e->key.repeat)
                menu_activate(1);
            return 1;
        case SDLK_F11: case SDLK_F10: case SDLK_F12:
            return 0;
        default:
            return 1;
        }
    case SDL_KEYUP: case SDL_TEXTINPUT:
        return 1;
    case SDL_CONTROLLERBUTTONDOWN:
        switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: menu_move(-1); break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: menu_move(1); break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: menu_activate(-1); break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: case SDL_CONTROLLER_BUTTON_A: menu_activate(1); break;
        case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_BACK: case SDL_CONTROLLER_BUTTON_GUIDE:
            menu_close();
            break;
        }
        return 1;
    case SDL_CONTROLLERBUTTONUP:
        return 1;
    case SDL_CONTROLLERAXISMOTION:
        if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            int d = e->caxis.value < -STICK_PRESS ? -1 : e->caxis.value > STICK_PRESS ? 1 : 0;
            if (d && d != menu_stick)
                menu_move(d);
            menu_stick = d;
        }
        return 1;
    case SDL_MOUSEMOTION:
        if ((i = menu_hit(e->motion.x, e->motion.y)) >= 0 && i != menu_sel) {
            menu_sel = i;
            menu_dirty = 1;
            render();
        }
        return 1;
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button == SDL_BUTTON_LEFT && (i = menu_hit(e->button.x, e->button.y)) >= 0) {
            menu_sel = i;
            menu_activate(1);
        } else if (e->button.button == SDL_BUTTON_RIGHT) {
            menu_close();
        }
        return 1;
    case SDL_MOUSEBUTTONUP: case SDL_MOUSEWHEEL:
        return 1;
    default:
        return 0;               /* окно, выход, геймпады — как обычно */
    }
}

int platform_paused(void)
{
    return menu_open;
}

/* ---------- ввод текста на ПК ---------- */

/* Буква в игру: в поле ввода (TextBox) — символом, в самодельном поле на Canvas — мультитапом. */
static void type_char(int c)
{
    if (text_wanted) {
        push(EV_CHAR, 0);
        queue[(q_head + q_len - 1) % QUEUE_LEN].ch = c;
    } else if (c == ' ') {
        mt_key('0');
    } else if (c >= '0' && c <= '9') {
        mt_key(c);
    } else {
        mt_letter(c);
    }
}

static void type_backspace(void)
{
    if (text_wanted) {
        push(EV_KEY_DOWN, KEY_CLEAR);
        push(EV_KEY_UP, KEY_CLEAR);
    } else {
        mt_key(KEY_CLEAR);
    }
}

static int utf8_decode(const char **p)
{
    const unsigned char *u = (const unsigned char *)*p;
    int c = u[0], n = 1;

    if ((c & 0xE0) == 0xC0 && u[1]) {
        c = ((c & 0x1F) << 6) | (u[1] & 0x3F);
        n = 2;
    } else if ((c & 0xF0) == 0xE0 && u[1] && u[2]) {
        c = ((c & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        n = 3;
    }
    *p += n;
    return c;
}

static void set_typing(int on)
{
    typing = on;
    typing_t = SDL_GetTicks();
    if (tex)
        render();
}

static int      letter_armed, pend_ch;     /* первая буква — ждём вторую, тогда режим букв */
static uint32_t pend_t;

/* Клавиатура ПК в игре. Две буквы подряд (кроме WASD/QE) сами включают режим букв (Tab — вручную):
 * тогда все буквы, кириллица, пробел и цифры уходят мультитапом, а Enter, стрелки,
 * Esc и Tab режим выключают. Вне режима всё как раньше. 1 — событие съедено. */
static int typing_key(const SDL_Event *e)
{
    SDL_Keycode k;

    if (e->type == SDL_TEXTINPUT) {
        const char *p = e->text.text;
        if (text_wanted)
            return 0;               /* TextBox: символы идут как есть */
        if (!typing) {
            /* одна случайная клавиша посреди игры режим не включает — нужны две буквы подряд
             * (кириллица или латиница не с WASD/QE) */
            const char *q = p;
            int c = utf8_decode(&q), candidate = c >= 0x80 || letter_armed;
            uint32_t now = SDL_GetTicks();
            letter_armed = 0;
            if (!candidate)
                return 1;
            if (!pend_ch || now - pend_t > 800) {
                pend_ch = c;
                pend_t = now;
                return 1;
            }
            set_typing(1);
            type_char(pend_ch);
            pend_ch = 0;
        }
        while (*p)
            type_char(utf8_decode(&p));
        typing_t = SDL_GetTicks();
        return 1;
    }
    if (e->type != SDL_KEYDOWN && e->type != SDL_KEYUP)
        return 0;
    if (text_wanted)
        return 0;
    k = e->key.keysym.sym;
    if (k == SDLK_TAB) {
        if (e->type == SDL_KEYDOWN && !e->key.repeat) {
            set_typing(!typing);
            pend_ch = 0;
        }
        return 1;
    }
    if (!typing) {
        if (e->type == SDL_KEYDOWN && k >= SDLK_a && k <= SDLK_z && k != SDLK_w && k != SDLK_a &&
            k != SDLK_s && k != SDLK_d && k != SDLK_q && k != SDLK_e && !(e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT))) {
            letter_armed = 1;       /* сама буква придёт следом через SDL_TEXTINPUT */
            return 1;
        }
        return 0;
    }
    if (e->type == SDL_KEYUP)
        return 0;                   /* отпускания — в игру: вдруг клавишу зажали до режима букв */
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT:
        if (e->key.keysym.mod & KMOD_ALT)
            return 0;
        if (e->type == SDL_KEYDOWN)
            set_typing(0);
        return 0;                   /* и само нажатие — в игру (Enter — подтвердить) */
    case SDLK_ESCAPE:
        if (e->type == SDL_KEYDOWN)
            set_typing(0);
        return 1;
    case SDLK_BACKSPACE: case SDLK_DELETE:
        if (e->type == SDL_KEYDOWN)
            type_backspace();
        typing_t = SDL_GetTicks();
        return 1;
    case SDLK_F1: case SDLK_F2: case SDLK_F10: case SDLK_F11: case SDLK_F12:
        return 0;
    default:
        return 1;                   /* буквы, цифры, пробел — придут символами */
    }
}

/* Экранная клавиатура для геймпада: крестовина/стик — выбор, A — ввести, B — стереть,
 * X — пробел, Y — заглавные, LB/RB — русские/латинские, Start или нажатие на стик — готово. */
#define OSK_COLS  11
#define OSK_CELL  16
#define OSK_W     (OSK_COLS * OSK_CELL + 8)
#define OSK_H     (5 * OSK_CELL + 8)

static const char *const osk_rows[2][4] = {
    { "1234567890-", "йцукенгшщзх", "фывапролджэ", "ячсмитьбю.," },
    { "1234567890-", "qwertyuiop'", "asdfghjkl:!", "zxcvbnm.,?@" }
};
enum { OSK_SHIFT, OSK_LANG, OSK_SPACE, OSK_BACK, OSK_DONE, OSK_SPECIALS };
static const int osk_special_x[OSK_SPECIALS + 1] = { 0, 1, 3, 7, 8, 11 };   /* границы кнопок нижнего ряда */

static int          osk_r, osk_c, osk_lang, osk_shift, osk_stick_x, osk_stick_y;
static Image        osk_img;
static SDL_Texture *osk_tex;
static SDL_Rect     osk_dst;
static int          osk_k;

static int osk_char(int r, int c)
{
    const char *p = osk_rows[osk_lang][r];
    int i, ch = 0;

    for (i = 0; i <= c && *p; i++)
        ch = utf8_decode(&p);
    if (osk_shift) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 0x430 && ch <= 0x44F))
            ch -= 32;
        else if (ch == 0x451)
            ch = 0x401;
    }
    return ch;
}

static int osk_special_at(int c)
{
    int i;

    for (i = 0; i < OSK_SPECIALS; i++)
        if (c < osk_special_x[i + 1])
            return i;
    return OSK_DONE;
}

static void osk_press(void)
{
    if (osk_r < 4) {
        type_char(osk_char(osk_r, osk_c));
        if (osk_shift == 1)
            osk_shift = 0;
        return;
    }
    switch (osk_special_at(osk_c)) {
    case OSK_SHIFT: osk_shift = !osk_shift; break;
    case OSK_LANG:  osk_lang = !osk_lang; break;
    case OSK_SPACE: type_char(' '); break;
    case OSK_BACK:  type_backspace(); break;
    default:        osk_open = 0; break;
    }
}

static void osk_move(int dr, int dc)
{
    if (dr) {
        osk_r = (osk_r + dr + 5) % 5;
        if (osk_r == 4)             /* в нижнем ряду — на кнопку под курсором */
            osk_c = osk_special_x[osk_special_at(osk_c)];
    }
    if (dc) {
        if (osk_r == 4) {
            int s = (osk_special_at(osk_c) + dc + OSK_SPECIALS) % OSK_SPECIALS;
            osk_c = osk_special_x[s];
        } else {
            osk_c = (osk_c + dc + OSK_COLS) % OSK_COLS;
        }
    }
}

static void osk_draw(void)
{
    static const char *const names[OSK_SPECIALS] = { "Аа", "RU", "пробел", "<-", "готово" };
    Image *m = &osk_img;
    int dw, dh, r, c, i;
    char buf[8];

    if (!osk_img.px && !(osk_img = image_new(OSK_W, OSK_H)).px)
        return;
    if (!osk_tex) {
        osk_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, OSK_W, OSK_H);
        if (!osk_tex)
            return;
        SDL_SetTextureBlendMode(osk_tex, SDL_BLENDMODE_BLEND);
    }
    gfx_fill(m, 0, 0, OSK_W, OSK_H, 0xEE1A1724u);
    gfx_frame(m, 0, 0, OSK_W, OSK_H, 0xFF3A3450u);
    for (r = 0; r < 4; r++)
        for (c = 0; c < OSK_COLS; c++) {
            int ch = osk_char(r, c), x = 4 + c * OSK_CELL, y = 4 + r * OSK_CELL, n = 0;
            int sel = r == osk_r && c == osk_c;
            if (sel)
                gfx_fill(m, x, y, OSK_CELL - 1, OSK_CELL - 1, 0xFF9C8FF0u);
            if (ch < 0x80)
                buf[n++] = (char)ch;
            else {
                buf[n++] = (char)(0xC0 | (ch >> 6));
                buf[n++] = (char)(0x80 | (ch & 0x3F));
            }
            buf[n] = '\0';
            gfx_text(m, &font_mid, x + (OSK_CELL - 1 - gfx_text_width(&font_mid, buf, 1)) / 2, y,
                     buf, sel ? 0xFF14111Cu : 0xFFEDE8F2u, 1);
        }
    for (i = 0; i < OSK_SPECIALS; i++) {
        int x = 4 + osk_special_x[i] * OSK_CELL, w = (osk_special_x[i + 1] - osk_special_x[i]) * OSK_CELL - 1;
        int y = 4 + 4 * OSK_CELL, sel = osk_r == 4 && osk_special_at(osk_c) == i;
        const char *t = i == OSK_LANG ? (osk_lang ? "EN" : "RU") : names[i];
        gfx_fill(m, x, y, w, OSK_CELL - 1, sel ? 0xFF9C8FF0u : (i == OSK_SHIFT && osk_shift) ? 0xFF4A4266u : 0xFF2E2944u);
        gfx_text(m, &font_small, x + (w - gfx_text_width(&font_small, t, 1)) / 2, y + 2, t,
                 sel ? 0xFF14111Cu : 0xFFEDE8F2u, 1);
    }
    SDL_UpdateTexture(osk_tex, NULL, osk_img.px, OSK_W * 4);
    platform_display_size(&dw, &dh);
    osk_k = dw * 9 / 10 / OSK_W < dh * 45 / 100 / OSK_H ? dw * 9 / 10 / OSK_W : dh * 45 / 100 / OSK_H;
    if (osk_k < 1)
        osk_k = 1;
    osk_dst.w = OSK_W * osk_k;
    osk_dst.h = OSK_H * osk_k;
    osk_dst.x = (dw - osk_dst.w) / 2;
    osk_dst.y = dh - osk_dst.h - dh / 40;
    SDL_RenderCopy(ren, osk_tex, NULL, &osk_dst);
}

/* Подсказка режима букв сверху экрана. */
static void hint_draw(void)
{
    static Image img;
    static SDL_Texture *t;
    static const char *msg = "Ввод букв: сначала выберите поле в игре · Enter — готово, Tab — выкл.";
    int w = gfx_text_width(&font_small, msg, 1) + 12, h = font_small.h + 6, dw, dh, k;
    SDL_Rect d;

    if (!img.px) {
        img = image_new(w, h);
        if (!img.px)
            return;
        gfx_fill(&img, 0, 0, w, h, 0xE01A1724u);
        gfx_text(&img, &font_small, 6, 3, msg, 0xFFEDE8F2u, 1);
        t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (t) {
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_UpdateTexture(t, NULL, img.px, w * 4);
        }
    }
    if (!t)
        return;
    platform_display_size(&dw, &dh);
    k = dw / 2 / w > 0 ? dw / 2 / w : 1;
    if (k > 3)
        k = 3;
    d.w = w * k;
    d.h = h * k;
    d.x = (dw - d.w) / 2;
    d.y = dh / 60;
    SDL_RenderCopy(ren, t, NULL, &d);
}

static int osk_hit(int wx, int wy, int *r, int *c)
{
    int ww, wh, dw, dh, x, y;

    SDL_GetWindowSize(win, &ww, &wh);
    platform_display_size(&dw, &dh);
    if (ww > 0 && wh > 0) {
        wx = wx * dw / ww;
        wy = wy * dh / wh;
    }
    if (osk_k < 1)
        return 0;
    x = (wx - osk_dst.x) / osk_k - 4;
    y = (wy - osk_dst.y) / osk_k - 4;
    if (x < 0 || y < 0 || x >= OSK_COLS * OSK_CELL || y >= 5 * OSK_CELL)
        return 0;
    *r = y / OSK_CELL;
    *c = x / OSK_CELL;
    return 1;
}

static int osk_event(const SDL_Event *e)
{
    int r, c, d;

    if (!osk_open && e->type != SDL_MOUSEBUTTONUP && e->type != SDL_MOUSEMOTION)
        return 0;                   /* клавиатуру закрыли, ждём только отпускания мыши */
    switch (e->type) {
    case SDL_CONTROLLERBUTTONDOWN:
        switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: osk_move(-1, 0); break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: osk_move(1, 0); break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: osk_move(0, -1); break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: osk_move(0, 1); break;
        case SDL_CONTROLLER_BUTTON_A: osk_press(); break;
        case SDL_CONTROLLER_BUTTON_B: type_backspace(); break;
        case SDL_CONTROLLER_BUTTON_X: type_char(' '); break;
        case SDL_CONTROLLER_BUTTON_Y: osk_shift = !osk_shift; break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            osk_lang = !osk_lang;
            break;
        case SDL_CONTROLLER_BUTTON_START: case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK: case SDL_CONTROLLER_BUTTON_BACK:
            osk_open = 0;
            break;
        case SDL_CONTROLLER_BUTTON_GUIDE:
            osk_open = 0;
            return 0;               /* и меню паузы — как обычно */
        }
        if (tex)
            render();
        return 1;
    case SDL_CONTROLLERBUTTONUP:
        return 1;
    case SDL_CONTROLLERAXISMOTION:
        d = e->caxis.value < -STICK_PRESS ? -1 : e->caxis.value > STICK_PRESS ? 1 : 0;
        if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
            if (d && d != osk_stick_x)
                osk_move(0, d);
            osk_stick_x = d;
        } else if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            if (d && d != osk_stick_y)
                osk_move(d, 0);
            osk_stick_y = d;
        } else {
            return 1;
        }
        if (tex)
            render();
        return 1;
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button == SDL_BUTTON_LEFT && osk_hit(e->button.x, e->button.y, &r, &c)) {
            osk_r = r;
            osk_c = c;
            osk_mouse = 1;
            osk_press();
            if (tex)
                render();
            return 1;
        }
        return 0;
    case SDL_MOUSEBUTTONUP: case SDL_MOUSEMOTION:
        /* щелчок по клавиатуре не должен дойти до игры «тапом» (Мяусим нажал бы свою кнопку под ней) */
        if (osk_mouse) {
            if (e->type == SDL_MOUSEBUTTONUP)
                osk_mouse = 0;
            return 1;
        }
        return 0;
    default:
        return 0;                   /* клавиатура ПК работает и при открытой экранной */
    }
}

int platform_fullscreen(void)
{
    return touch_on ? -1 : pc_full;
}

void platform_set_fullscreen(int on)
{
    if (!touch_on)
        set_fullscreen(on);
}

int platform_hints(void)
{
    return pc_hints;
}

/* Подсказки геймпада поверх игры: столбиком в свободном поле сбоку, а если поля нет —
 * полосой внизу. LB+RB или меню паузы их прячут. */
static void toast_draw(void)
{
    static const char *const pairs[] = { "View\tменю", "A\tогонь (5)", "Start\tлевая софт", "B\tправая софт",
                                         "L3\tклавиатура", "LB+RB\tскрыть" };
    enum { N = sizeof(pairs) / sizeof(pairs[0]) };
    static Image col, row;
    static SDL_Texture *tcol, *trow;
    static int cw, ch, rw, rh;
    int dw, dh, k, i, side;
    SDL_Rect d;

    if (!tcol) {
        int line = font_small.h + 5;
        char all[256] = "";
        cw = 0;
        for (i = 0; i < N; i++) {
            int w = gfx_hints(NULL, &font_small, 0, 0, pairs[i], 0);
            cw = w > cw ? w : cw;
            snprintf(all + strlen(all), sizeof(all) - strlen(all), "%s%s", i ? "\t" : "", pairs[i]);
        }
        cw += 10;
        ch = N * line + 6;
        rw = gfx_hints(NULL, &font_small, 0, 0, all, 0) + 12;
        rh = font_small.h + 10;
        col = image_new(cw, ch);
        row = image_new(rw, rh);
        if (!col.px || !row.px)
            return;
        gfx_fill(&col, 0, 0, cw, ch, 0xB01A1724u);
        for (i = 0; i < N; i++)
            gfx_hints(&col, &font_small, 5, 4 + i * line, pairs[i], 0xFFEDE8F2u);
        gfx_fill(&row, 0, 0, rw, rh, 0xB01A1724u);
        gfx_hints(&row, &font_small, 6, 4, all, 0xFFEDE8F2u);
        tcol = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, cw, ch);
        trow = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, rw, rh);
        if (!tcol || !trow)
            return;
        SDL_SetTextureBlendMode(tcol, SDL_BLENDMODE_BLEND);
        SDL_SetTextureBlendMode(trow, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(tcol, NULL, col.px, cw * 4);
        SDL_UpdateTexture(trow, NULL, row.px, rw * 4);
    }
    platform_display_size(&dw, &dh);
    side = dw - (game_dst.x + game_dst.w);       /* свободное поле справа от игры */
    k = (side - dw / 60) / cw;
    if (k > dh / 2 / ch)
        k = dh / 2 / ch;
    if (k >= 1) {
        k = k > 3 ? 3 : k;
        d.w = cw * k;
        d.h = ch * k;
        d.x = game_dst.x + game_dst.w + (side - d.w) / 2;
        d.y = dh - d.h - dh / 20;
        SDL_RenderCopy(ren, tcol, NULL, &d);
        return;
    }
    k = dw * 95 / 100 / rw;
    k = k < 1 ? 1 : k > 3 ? 3 : k;
    d.w = rw * k;
    d.h = rh * k;
    d.x = (dw - d.w) / 2;
    d.y = dh - d.h - dh / 60;
    SDL_RenderCopy(ren, trow, NULL, &d);
}
