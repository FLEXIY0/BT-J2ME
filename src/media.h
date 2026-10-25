/* Носитель игры на полке, плоский и пиксельный: PS1 — коробка компакт-диска, PS2 — коробка
 * DVD, NES — картридж, J2ME — карта памяти. Все одного размера, фон прозрачный. Рисуется в
 * маленьком разрешении и показывается крупным пикселем (MEDIA_PIXEL экранных точек). */
#ifndef BT_MEDIA_H
#define BT_MEDIA_H

#include <stdint.h>

#include "gfx.h"

#define MEDIA_PIXEL 2

typedef struct {
    int          kind;       /* GAME_J2ME … GAME_PS2 */
    const Image *front;      /* лицевая сторона (обложка) или NULL */
    const Image *icon;       /* J2ME: иконка игры для наклейки карты, если обложки нет */
    const char  *title;
    uint32_t     bg, fg;     /* два цвета игры (media_tones) */
} Media;

/* Ширина / высота носителя (у всех одинаковая). */
float media_aspect(int kind);
/* Пропорции лицевой стороны: под них кадрируется обложка. */
float media_front_aspect(int kind);
/* Носитель w × h пикселей (уже в крупном пикселе) с прозрачным фоном. */
Image media_render(const Media *m, int w, int h);
/* Два цвета картинки: основной (фон) и самый заметный другой (акцент). */
void  media_tones(const Image *img, uint32_t *bg, uint32_t *fg);

#endif
