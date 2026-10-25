/* Сглаженный текст встроенными шрифтами (stb_truetype): Inter для заголовков и кнопок,
 * JetBrains Mono для таблиц. Глифы кэшируются по размеру — полка рисует несколько размеров. */
#ifndef BT_TTF_H
#define BT_TTF_H

#include <stdint.h>

#include "gfx.h"

enum { TTF_SEMIBOLD, TTF_MEDIUM, TTF_REGULAR, TTF_MONO, TTF_COUNT };

/* Ширина строки в точках при высоте шрифта px. */
int   ttf_width(int font, int px, const char *utf8);
/* Строка с верхнего левого угла (x, y); возвращает ширину. Цвет с альфой. */
int   ttf_draw(Image *dst, int font, int px, int x, int y, const char *utf8, uint32_t argb);
/* Строка в отдельной картинке (прозрачный фон) — для поворота на корешке. */
Image ttf_render(int font, int px, const char *utf8, uint32_t argb);
/* Высота строки (от верха до базовой линии и вниз) для размера px. */
int   ttf_line(int font, int px);

#endif
