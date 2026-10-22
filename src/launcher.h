/* Лаунчер-«консоль»: полка CD-боксов с обложками, листание как в меню приставки. */
#ifndef BT_LAUNCHER_H
#define BT_LAUNCHER_H

#include "platform.h"

int launcher_run(const char *games_dir, PlatformConfig *cfg);

#endif
