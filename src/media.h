/* Носитель игры на полке — объёмная коробка, как в пакетах «3D Boxes»: слева виден корешок,
 * лицевая сторона чуть развёрнута вглубь. PS1 — коробка компакт-диска, PS2 — коробка DVD,
 * NES — картридж, J2ME — карта памяти. Фон картинки прозрачный. */
#ifndef BT_MEDIA_H
#define BT_MEDIA_H

#include <stdint.h>

#include "gfx.h"

typedef struct {
    int          kind;       /* GAME_J2ME … GAME_PS2 */
    const Image *front;      /* лицевая сторона (обложка) или NULL */
    const Image *icon;       /* J2ME: иконка игры для наклейки карты, если обложки нет */
    const char  *title;      /* на корешок */
    uint32_t     bg, fg;     /* два цвета игры (media_tones) */
} Media;

/* Ширина / высота всего носителя с корешком. */
float media_aspect(int kind);
/* Пропорции лицевой стороны: под них кадрируется обложка. */
float media_front_aspect(int kind);
/* Носитель w × h с прозрачным фоном. */
Image media_render(const Media *m, int w, int h);
/* Два цвета картинки: основной (фон) и самый заметный другой (акцент). */
void  media_tones(const Image *img, uint32_t *bg, uint32_t *fg);

#endif
