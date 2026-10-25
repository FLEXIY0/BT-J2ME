/* Носитель игры на полке: PS1 — коробка компакт-диска, PS2 — коробка DVD (обе с корешком
 * слева), NES — картридж с наклейкой-обложкой, J2ME — карта microSD. Фон прозрачный. */
#ifndef BT_MEDIA_H
#define BT_MEDIA_H

#include <stdint.h>

#include "gfx.h"

typedef struct {
    int          kind;       /* GAME_J2ME … GAME_PS2 */
    const Image *front;      /* лицевая сторона (обложка) или NULL */
    const Image *icon;       /* J2ME: иконка игры для наклейки карты, если обложки нет */
    const char  *title;      /* на корешок коробки */
    uint32_t     bg, fg;     /* два цвета игры (media_tones) */
} Media;

/* Примерные ширина / высота носителя — для заглушки, пока он не нарисован. */
float media_aspect(int kind);
/* Пропорции лицевой стороны: под них кадрируется обложка. */
float media_front_aspect(int kind);
/* Носитель высотой h точек (ширина — по виду носителя), фон прозрачный. */
Image media_render(const Media *m, int h);
/* Два цвета картинки: основной (фон) и самый заметный другой (акцент). */
void  media_tones(const Image *img, uint32_t *bg, uint32_t *fg);

#endif
