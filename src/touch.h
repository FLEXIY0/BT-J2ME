/* Сенсорное управление поверх игры (Android, сенсорные экраны; на ПК — `--touch`).
 *
 *  - слева «плавающий» стик, как L3 у геймпада: появляется там, где коснулись;
 *  - справа квадратная кнопка «огонь» (центр крестовины, она же «5»), сверху софт-клавиши;
 *  - ⌨ выдвигает цифровую панель, ⚙ открывает редактор: любую клавишу телефона можно
 *    закрепить кнопкой поверх игры или убрать, двигать, менять размер и прозрачность;
 *  - раскладка запоминается для каждой игры отдельно, своя для портрета и альбома.
 *
 * Касания самой картинки игры приходят в игру как касания экрана (MIDP pointer). */
#ifndef BT_TOUCH_H
#define BT_TOUCH_H

#include <SDL.h>

#include "platform.h"

/* type — EV_KEY_DOWN/UP (key) или EV_POINTER_* (x, y в координатах экрана игры). */
typedef void (*TouchEmit)(EventType type, int key, int x, int y);

void touch_init(SDL_Renderer *ren, TouchEmit emit);
void touch_shutdown(void);
/* Профиль кнопок игры; NULL — кнопок нет (лаунчер), касания идут как указатель. */
void touch_profile(const char *game_id);
/* Куда рисовать экран игры gw×gh на выводе W×H: в портрете — сверху, под ним кнопки. */
void touch_place(int W, int H, int gw, int gh, ScaleMode mode, SDL_Rect *dst);
/* Событие SDL_FINGER*; 1 — обработано. */
int  touch_event(const SDL_Event *e);
void touch_draw(void);
/* Кнопки изменились (нажатие, стик, редактор) — кадр надо перерисовать. */
int  touch_dirty(void);
void touch_vibrate(int ms);

#endif
