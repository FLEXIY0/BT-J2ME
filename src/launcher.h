/* Лаунчер-«консоль»: полка CD-боксов с обложками, листание как в меню приставки. */
#ifndef BT_LAUNCHER_H
#define BT_LAUNCHER_H

#include "platform.h"

int launcher_run(const char *games_dir, PlatformConfig *cfg);
/* Скопировать игру (.jar/.jad, вместе с одноимённой парой рядом) в папку игр.
 * Лаунчер потом откроется сразу на ней. 1 — скопировано. */
int launcher_import(const char *games_dir, const char *path);

#endif
