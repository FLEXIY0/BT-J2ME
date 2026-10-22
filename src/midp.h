/* Запуск MIDlet: Java-машина + нативная часть MIDP (графика, события, сохранения). */
#ifndef BT_MIDP_H
#define BT_MIDP_H

#include <stddef.h>

#include "platform.h"

typedef enum {
    MIDP_BACK,    /* игрок вышел в меню (Esc / кнопка Xbox) или игра завершилась сама */
    MIDP_QUIT,    /* закрыли окно — выходим совсем */
    MIDP_ERROR    /* игра не запустилась; причина в err */
} MidpResult;

/* jar — путь к игре; save_id — имя папки сохранений (обычно имя файла без .jar). */
MidpResult midp_run(const char *jar, const char *save_id, ScaleMode scale, char *err, size_t errlen);

#endif
