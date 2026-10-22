#include "platform.h"
#include "util.h"

#include <SDL.h>
#include <stdlib.h>

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static uint32_t     *fb;
static int           fb_w, fb_h;

int platform_init(const PlatformConfig *cfg)
{
    Uint32 wflags = 0, rflags = cfg->software ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED;
    int ww = cfg->width * 2, wh = cfg->height * 2;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        BT_LOG("SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    /* Ближайший сосед: никакого «мыла» на пиксель-арте. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (cfg->fullscreen)
        wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    else
        wflags |= SDL_WINDOW_RESIZABLE;

    win = SDL_CreateWindow(cfg->title ? cfg->title : "BT-J2ME", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, ww, wh, wflags);
    if (!win) {
        BT_LOG("SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    ren = SDL_CreateRenderer(win, -1, rflags | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        BT_LOG("SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    SDL_RenderSetLogicalSize(ren, cfg->width, cfg->height);
    SDL_RenderSetIntegerScale(ren, cfg->integer_scale ? SDL_TRUE : SDL_FALSE);
    SDL_ShowCursor(cfg->fullscreen ? SDL_DISABLE : SDL_ENABLE);

    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                            cfg->width, cfg->height);
    fb = calloc((size_t)cfg->width * (size_t)cfg->height, sizeof(uint32_t));
    if (!tex || !fb) {
        BT_LOG("не удалось создать кадровый буфер\n");
        return -1;
    }
    fb_w = cfg->width;
    fb_h = cfg->height;
    return 0;
}

void platform_shutdown(void)
{
    free(fb);
    fb = NULL;
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

uint32_t *platform_framebuffer(void)
{
    return fb;
}

void platform_present(void)
{
    SDL_UpdateTexture(tex, NULL, fb, fb_w * (int)sizeof(uint32_t));
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
    default:
        if (k >= SDLK_0 && k <= SDLK_9)
            return KEY_NUM0 + (int)(k - SDLK_0);
        if (k >= SDLK_KP_1 && k <= SDLK_KP_9)
            return KEY_NUM0 + 1 + (int)(k - SDLK_KP_1);
        return 0;
    }
}

int platform_poll(Event *ev)
{
    SDL_Event e;

    while (SDL_PollEvent(&e)) {
        ev->type = EV_NONE;
        switch (e.type) {
        case SDL_QUIT:
            ev->type = EV_QUIT;
            return 1;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_F10) {
                ev->type = EV_QUIT;
                return 1;
            }
            if (e.key.repeat)
                break;
            ev->key = map_key(e.key.keysym.sym);
            if (!ev->key)
                break;
            ev->type = e.type == SDL_KEYDOWN ? EV_KEY_DOWN : EV_KEY_UP;
            return 1;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            /* При SDL_RenderSetLogicalSize координаты уже приведены к виртуальному экрану. */
            ev->type = e.type == SDL_MOUSEBUTTONDOWN ? EV_POINTER_DOWN : EV_POINTER_UP;
            ev->x = e.button.x;
            ev->y = e.button.y;
            return 1;
        case SDL_MOUSEMOTION:
            if (!(e.motion.state & SDL_BUTTON_LMASK))
                break;
            ev->type = EV_POINTER_MOVE;
            ev->x = e.motion.x;
            ev->y = e.motion.y;
            return 1;
        }
    }
    return 0;
}

uint32_t platform_ticks_ms(void)
{
    return SDL_GetTicks();
}

void platform_sleep_ms(uint32_t ms)
{
    SDL_Delay(ms);
}
