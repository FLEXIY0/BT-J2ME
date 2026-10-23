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
    KEY_SOFT_LEFT = -6, KEY_SOFT_RIGHT = -7, KEY_CLEAR = -8
};

typedef enum {
    EV_NONE, EV_KEY_DOWN, EV_KEY_UP, EV_POINTER_DOWN, EV_POINTER_UP, EV_POINTER_MOVE,
    EV_BACK,        /* Esc или кнопка Xbox: из игры — в меню, из меню — выход */
    EV_PAD_ADDED,   /* подключён геймпад, name — его название */
    EV_CHAR,        /* набран символ (поля ввода в играх), ch — код */
    EV_QUIT,        /* закрыли окно / F10 */
    EV_REFRESH,     /* в папке игр что-то поменялось (импорт на Android) — пересканировать полку */
    EV_RESTART,     /* перезапустить игру (сменили размер экрана в настройках кнопок) */
    EV_SCREENSHOT,  /* снимок экрана игры (F12 или кнопка с фотоаппаратом) */
    EV_HOVER        /* мышь сдвинулась без нажатия (только в режиме курсора) */
} EventType;

typedef struct {
    EventType   type;
    int         key;    /* для EV_KEY_* */
    int         x, y;   /* для EV_POINTER_*, уже в координатах виртуального экрана */
    const char *name;   /* для EV_PAD_ADDED */
    int         pad;    /* 1 — событие пришло с геймпада (лаунчер подстраивает подсказки) */
    int         printable; /* клавиша печатная — её символ придёт ещё и как EV_CHAR */
    int         ch;     /* для EV_CHAR */
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
    int         touch;          /* сенсорные кнопки поверх игры (на Android — всегда) */
    int         mute;           /* --mute: без звука, что бы ни стояло в настройках */
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
/* Профиль сенсорных кнопок игры (раскладка запоминается для каждой игры); NULL — без кнопок. */
void     platform_touch_profile(const char *game_id);
/* Широкоэкранный режим выбран для этой игры (после platform_touch_profile). */
int      platform_touch_wide(void);
/* Добавить игры системным выбором файлов (Android); по готовности придёт EV_REFRESH. */
int      platform_can_import(void);
void     platform_import_games(void);
/* Экранная клавиатура для полей ввода в играх (на ПК не нужна — там своя клавиатура). */
void     platform_text_input(int on);
void     platform_vibrate(int ms);
/* Следующие кадры проявляются из черноты за ms (запуск игры после анимации). */
void     platform_fade_in(int ms);
/* Режим курсора (лаунчер): системный указатель скрыт — лаунчер рисует свой, мышь шлёт
 * EV_HOVER, а левый стик геймпада не жмёт стрелки, а читается через platform_stick. */
void     platform_cursor_mode(int on);
/* Отклонение левого стика, -1..1 по каждой оси. */
void     platform_stick(float *x, float *y);
/* Открыть ссылку системным браузером (platformRequest); 0 — не получилось. */
int      platform_open_url(const char *url);

/* Звук: моно 16 бит. mix зовётся из потока звука и заполняет frames сэмплов.
 * Возвращает настоящую частоту устройства или 0, если звука нет. */
typedef void (*AudioMixFn)(int16_t *out, int frames);
int      platform_audio_open(int rate, AudioMixFn mix);
void     platform_audio_lock(int on);   /* 1 — не звать mix, пока не отпустят */
void     platform_audio_close(void);

#endif
