#include "platform.h"
#include "util.h"

#include <SDL.h>
#include <stdlib.h>

#define QUEUE_LEN   32
#define STICK_PRESS 16000   /* гистерезис, чтобы стик не «дребезжал» у порога */
#define STICK_FREE  10000

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static Image         screen;
static ScaleMode     scale_mode;
static Event         queue[QUEUE_LEN];
static int           q_head, q_len;
static int           stick_dir[4];   /* вверх, вниз, влево, вправо */
static SDL_AudioDeviceID audio_dev;
static AudioMixFn    audio_mix;

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

    platform_display_size(&dw, &dh);
    k = dw / screen.w < dh / screen.h ? dw / screen.w : dh / screen.h;
    /* Логический размер + целый масштаб делает SDL; «авто» решаем сами. */
    SDL_RenderSetLogicalSize(ren, screen.w, screen.h);
    SDL_RenderSetIntegerScale(ren, scale_mode == SCALE_INTEGER ||
                                   (scale_mode == SCALE_AUTO && k >= 1 && k * screen.h * 100 >= dh * 85)
                                   ? SDL_TRUE : SDL_FALSE);
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
    push_pad(EV_PAD_ADDED, 0);
    queue[(q_head + q_len - 1) % QUEUE_LEN].name = SDL_GameControllerName(gc);
}

int platform_init(const PlatformConfig *cfg)
{
    Uint32 wflags = cfg->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE;
    Uint32 rflags = cfg->software ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED;

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
    SDL_ShowCursor(cfg->fullscreen ? SDL_DISABLE : SDL_ENABLE);
    /* Уже подключённые геймпады придут событиями SDL_CONTROLLERDEVICEADDED. */
    return platform_set_screen(cfg->width, cfg->height, cfg->scale);
}

void platform_shutdown(void)
{
    platform_audio_close();
    image_free(&screen);
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

void platform_present(void)
{
    SDL_UpdateTexture(tex, NULL, screen.px, screen.w * (int)sizeof(uint32_t));
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);   /* поля вокруг экрана — чёрные */
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
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

    switch (e->type) {
    case SDL_QUIT:
        push(EV_QUIT, 0);
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            apply_scale();
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        if (e->key.keysym.sym == SDLK_F10) {
            push(EV_QUIT, 0);
            break;
        }
        if (e->key.keysym.sym == SDLK_ESCAPE) {
            if (e->type == SDL_KEYDOWN && !e->key.repeat)
                push(EV_BACK, 0);
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
        if (gc)
            SDL_GameControllerClose(gc);
        break;
    }
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
            if (e->type == SDL_CONTROLLERBUTTONDOWN)
                push_pad(EV_BACK, 0);
        } else if ((key = map_button(e->cbutton.button)) != 0) {
            push_pad(e->type == SDL_CONTROLLERBUTTONDOWN ? EV_KEY_DOWN : EV_KEY_UP, key);
        }
        break;
    case SDL_CONTROLLERAXISMOTION:
        if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
            stick_axis(e->caxis.value, 2, 3, KEY_LEFT, KEY_RIGHT);
        else if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
            stick_axis(e->caxis.value, 0, 1, KEY_UP, KEY_DOWN);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        /* При SDL_RenderSetLogicalSize координаты уже приведены к виртуальному экрану. */
        push(e->type == SDL_MOUSEBUTTONDOWN ? EV_POINTER_DOWN : EV_POINTER_UP, 0);
        queue[(q_head + q_len - 1) % QUEUE_LEN].x = e->button.x;
        queue[(q_head + q_len - 1) % QUEUE_LEN].y = e->button.y;
        break;
    case SDL_MOUSEMOTION:
        if (e->motion.state & SDL_BUTTON_LMASK) {
            push(EV_POINTER_MOVE, 0);
            queue[(q_head + q_len - 1) % QUEUE_LEN].x = e->motion.x;
            queue[(q_head + q_len - 1) % QUEUE_LEN].y = e->motion.y;
        }
        break;
    }
}

int platform_poll(Event *ev)
{
    SDL_Event e;

    while (!q_len && SDL_PollEvent(&e))
        translate(&e);
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
