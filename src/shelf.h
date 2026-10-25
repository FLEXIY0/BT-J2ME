/* Полка в разрешении экрана: фон — размытый арт выбранной игры, слева сверху название и
 * сведения (как PlayStation-X, строго, как у Analogue 3D), внизу лента носителей с отражением,
 * выбранный крупнее. На вертикальном экране — носитель по центру, сведения и кнопка снизу. */
#ifndef BT_SHELF_H
#define BT_SHELF_H

#include <stdint.h>

#include "gfx.h"
#include "library.h"

typedef struct {
    int   W, H, portrait;
    float u;                    /* масштаб: точек на «точку макета» (макет — 1080 по короткой стороне) */
    int   hi, lo;               /* высота носителя: выбранного и соседних */
    int   base;                 /* линия «пола» ленты (низ носителей) */
    Image bg, bg_old;           /* фон выбранной игры и прежний — для плавной смены */
    int   bg_game, bg_want;
    uint32_t bg_t, sel_t;
    /* где кнопки — для касаний */
    int   play[4], cover[4], tab_l[4], tab_r[4];
} Shelf;

enum { SHELF_HINTS_NONE, SHELF_HINTS_PAD, SHELF_HINTS_KEYS };
enum { SHELF_HIT_NONE, SHELF_HIT_PLAY, SHELF_HIT_COVER, SHELF_HIT_TAB_L, SHELF_HIT_TAB_R, SHELF_HIT_GAME };

/* Размер экрана поменялся (или первый раз). */
void shelf_layout(Shelf *s, int w, int h);
/* Кадр полки. pos — плавная позиция ленты (к sel стремится снаружи). */
void shelf_draw(Shelf *s, Image *scr, Library *lib, int sel, float pos, uint32_t now, int hints);
/* Что под точкой (x, y); *game — номер игры для SHELF_HIT_GAME. */
int  shelf_hit(const Shelf *s, const Library *lib, float pos, int x, int y, int *game);
void shelf_free(Shelf *s);

#endif
