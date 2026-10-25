/* Обложки игр по названию. Сначала смотрим свои картинки в games/covers (подходит и пакет
 * объёмных коробок EmuMovies: имя файла сверяется с названием игры «мягко»), потом — кэш
 * обложек, скачанных из открытой базы libretro-thumbnails (PS1, PS2, NES) в <данные>/boxart. */
#ifndef BT_BOXART_H
#define BT_BOXART_H

#include <stddef.h>

#include "library.h"

/* Папка приставки в базе libretro и в кэше: "Sony - PlayStation" и "ps1"; NULL — базы нет (J2ME). */
const char *boxart_system(int kind, const char **cache_dir);
/* Какое из имён (без .png) лучше всего подходит к игре: индекс или -1. Названия сравниваются
 * без регистра, знаков и скобок; регион и номер диска из имени файла игры — в приоритете. */
int  boxart_match(const char *const *names, int n, const char *base, const char *name);
/* Скачанная обложка игры: путь в кэше; 1 — файл есть. */
int  boxart_cached(int kind, const char *base, char *out, size_t cap);

/* Скачать в фоне обложки игр полки, для которых их ещё нет (только ПК и Android). */
void     boxart_fetch(const Library *lib);
/* Растёт, когда в кэше появилась новая обложка — полке пора перечитать незагруженные. */
unsigned boxart_generation(void);

#endif
