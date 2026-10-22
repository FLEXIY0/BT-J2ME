/* Слой платформы: всё, что зависит от ОС и графики, живёт за этим интерфейсом.
 * Ядро (VM + MIDP) и лаунчер рисуют в обычный буфер 0xAARRGGBB своего размера,
 * а бэкенд масштабирует его на весь экран. Бэкенды: sdl2 (основной), null (без экрана),
 * в планах — fbdev для машин без X11/Wayland. */
#ifndef BT_PLATFORM_H
#define BT_PLATFORM_H

#include <stdint.h>

#include "gfx.h"

/* Коды клавиш MIDP (Canvas.KEY_*) и «игровые» коды в стиле Nokia/Sony Ericsson. */
enum {
    KEY_NUM0 = 48, KEY_NUM9 = 57, KEY_STAR = 42, KEY_POUND = 35,
    KEY_UP = -1, KEY_DOWN = -2, KEY_LEFT = -3, KEY_RIGHT = -4, KEY_FIRE = -5,
    KEY_SOFT_LEFT = -6, KEY_SOFT_RIGHT = -7
};

typedef enum {
    EV_NONE, EV_KEY_DOWN, EV_KEY_UP, EV_POINTER_DOWN, EV_POINTER_UP, EV_POINTER_MOVE,
    EV_BACK,        /* Esc или кнопка Xbox: из игры — в меню, из меню — выход */
    EV_PAD_ADDED,   /* подключён геймпад, name — его название */
    EV_QUIT         /* закрыли окно / F10 */
} EventType;

typedef struct {
    EventType   type;
    int         key;    /* для EV_KEY_* */
    int         x, y;   /* для EV_POINTER_*, уже в координатах виртуального экрана */
    const char *name;   /* для EV_PAD_ADDED */
    int         pad;    /* 1 — событие пришло с геймпада (лаунчер подстраивает подсказки) */
} Event;

typedef enum {
    SCALE_INTEGER,  /* только целые множители — пиксели остаются чёткими */
    SCALE_FIT,      /* максимально большой с сохранением пропорций */
    SCALE_AUTO      /* целый, если он заполняет ≥85% высоты, иначе FIT */
} ScaleMode;

typedef struct {
    int         width, height;  /* начальный виртуальный экран */
    int         fullscreen;
    ScaleMode   scale;
    int         software;       /* программный рендер для старых видеокарт */
    const char *title;
} PlatformConfig;

int      platform_init(const PlatformConfig *cfg);
void     platform_shutdown(void);
/* Размер реального экрана (окна) в пикселях. */
void     platform_display_size(int *w, int *h);
/* Сменить виртуальный экран: лаунчер и игры живут в разных разрешениях. */
int      platform_set_screen(int w, int h, ScaleMode mode);
Image   *platform_screen(void);
void     platform_present(void);
int      platform_poll(Event *ev);   /* 1 — событие получено */
uint32_t platform_ticks_ms(void);
void     platform_sleep_ms(uint32_t ms);

#endif
