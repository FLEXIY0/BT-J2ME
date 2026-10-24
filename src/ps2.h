/* PlayStation 2: диски .iso, .cue/.bin, .chd, .cso и .zso на полке. Ядро — ParaLLEl PS2
 * (PCSX2 для libretro, GPL-3): не вшито, а скачивается с официальных сборок libretro при
 * первом запуске игры PS2 в папку данных и подгружается оттуда. BIOS не нужен — вшит
 * свободный OpenPS2BIOS (MIT); свой BIOS приставки в games/bios главнее. */
#ifndef BT_PS2_H
#define BT_PS2_H

#include <stddef.h>

#include "midp.h"

/* Запуск с полки: как psx_run, только для диска PS2. */
MidpResult ps2_run(const char *path, const char *save_id, ScaleMode scale, char *err, size_t errlen);

#endif
