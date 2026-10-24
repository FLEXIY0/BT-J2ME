/* PlayStation: диски .cue/.bin, .iso, .img и .pbp на полке. Внутри — ядро PCSX-ReARMed
 * (third_party/pcsx_rearmed, GPL-2) через его libretro-интерфейс: динамический
 * перекомпилятор на ARM, NEON/SSE-рендер, звук SPU с XA и CD-музыкой. Снаружи — наши
 * экран, звук, кнопки, меню паузы, быстрые сохранения и карта памяти в папке игры. */
#ifndef BT_PSX_H
#define BT_PSX_H

#include <stddef.h>

#include "midp.h"

/* Похоже ли имя на образ диска PS1 (по расширению; .bin проверяет полка — их делят с BIOS). */
int        psx_is_image(const char *path);
/* Файл похож на BIOS приставки: .bin ровно 512 КБ. */
int        psx_is_bios(const char *path);
/* Где искать BIOS: <папка игр>/bios, затем сама папка игр. В out — папка, где он нашёлся,
 * или <папка игр>/bios, если его нет нигде (тогда ядро запустит встроенный HLE-BIOS). */
int        psx_bios_dir(const char *games_dir, char *out, size_t cap);

/* Запуск с полки: как nes_run, только для диска PS1. */
MidpResult psx_run(const char *path, const char *save_id, ScaleMode scale, char *err, size_t errlen);

#endif
