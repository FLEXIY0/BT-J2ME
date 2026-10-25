/* Сколько играли и когда запускали: <данные>/playtime.txt, строка на игру —
 * «имя<TAB>секунды<TAB>время Unix последнего запуска». */
#ifndef BT_STATS_H
#define BT_STATS_H

#include "library.h"

/* Прочитать для всех игр полки (play_secs, last_played). */
void stats_load(Library *lib);
/* Игра закрыта: добавить secs к времени, запомнить запуск сейчас и сохранить файл. */
void stats_add(Game *g, long secs);

#endif
