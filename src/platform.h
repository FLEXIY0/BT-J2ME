/* Слой платформы: всё, что зависит от ОС и графики, живёт за этим интерфейсом.
 * Ядро (VM + MIDP) рисует в обычный буфер 0xAARRGGBB размером с виртуальный экран телефона,
 * а бэкенд масштабирует его на весь экран. Бэкенды: sdl2 (основной), null (без экрана),
 * в планах — fbdev для машин без X11/Wayland. */
#ifndef BT_PLATFORM_H
#define BT_PLATFORM_H

#include <stdint.h>

/* Коды клавиш MIDP (Canvas.KEY_*) и «игровые» коды в стиле Nokia/Sony Ericsson. */
enum {
    KEY_NUM0 = 48, KEY_NUM9 = 57, KEY_STAR = 42, KEY_POUND = 35,
    KEY_UP = -1, KEY_DOWN = -2, KEY_LEFT = -3, KEY_RIGHT = -4, KEY_FIRE = -5,
    KEY_SOFT_LEFT = -6, KEY_SOFT_RIGHT = -7
};

typedef enum { EV_NONE, EV_KEY_DOWN, EV_KEY_UP, EV_POINTER_DOWN, EV_POINTER_UP, EV_POINTER_MOVE, EV_QUIT } EventType;

typedef struct {
    EventType type;
    int       key;      /* для EV_KEY_* */
    int       x, y;     /* для EV_POINTER_*, уже в координатах виртуального экрана */
} Event;

typedef struct {
    int width, height;  /* виртуальный экран телефона, напр. 240x320 */
    int fullscreen;
    int integer_scale;  /* только целые множители — пиксели остаются чёткими */
    int software;       /* программный рендер для старых видеокарт */
    const char *title;
} PlatformConfig;

int      platform_init(const PlatformConfig *cfg);
void     platform_shutdown(void);
uint32_t *platform_framebuffer(void);
void     platform_present(void);
int      platform_poll(Event *ev);   /* 1 — событие получено */
uint32_t platform_ticks_ms(void);
void     platform_sleep_ms(uint32_t ms);

#endif
