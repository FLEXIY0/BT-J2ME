/* Пиксельная рука-указатель: # — контур, o — перчатка, - — тень. Кончик пальца — (3, 0).
 * Лаунчер рисует её сам (её водит и стик), в играх она — системный курсор. */
#ifndef BT_CURSOR_H
#define BT_CURSOR_H

#define CURSOR_W   14
#define CURSOR_H   16
#define CURSOR_HX  3

static const char *const cursor_hand[CURSOR_H] = {
    "...##.........", "..#oo#........", "..#oo#........", "..#oo#........",
    "..#oo###......", "..#oo#oo###...", "..#oo#oo#oo##.", "###oooooo#oo-#",
    "#oo#ooooooooo#", "#ooooooooooo-#", ".#oooooooooo-#", ".#ooooooooo--#",
    "..#ooooooo--#.", "..#oooooo---#.", "...#oooo---#..", "...########..." };

#endif
