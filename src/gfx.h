/* Программная 2D-графика поверх буфера 0xAARRGGBB. Её же будет использовать
 * javax.microedition.lcdui.Graphics, поэтому никакого GPU и внешних библиотек. */
#ifndef BT_GFX_H
#define BT_GFX_H

#include <stdint.h>

typedef struct {
    uint32_t *px;
    int       w, h;
} Image;

typedef struct {
    int             w, h, count;
    const uint16_t *cps;
    const uint8_t  *bits;
} Font;

extern const Font font_small;   /* 6x12 */
extern const Font font_big;     /* 8x16, жирный */
extern const Font font_mid;     /* 8x16 — средний шрифт игр */
extern const Font font_large;   /* 10x20 */
extern const Font font_largeb;  /* 10x20, жирный */

/* Строки глифа ((w+7)/8 байт на строку) или NULL; неизвестные символы заменяются на '?'. */
const uint8_t *gfx_glyph(const Font *f, unsigned cp);

Image image_new(int w, int h);
void  image_free(Image *img);
/* Уменьшение/увеличение с усреднением — для обложек, чтобы не было «лесенки». */
Image image_resize(const Image *src, int w, int h);

void gfx_fill(Image *dst, int x, int y, int w, int h, uint32_t argb);
void gfx_vgradient(Image *dst, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void gfx_frame(Image *dst, int x, int y, int w, int h, uint32_t argb);
/* alpha 0..255 умножается на альфу картинки */
void gfx_blit(Image *dst, const Image *src, int x, int y, int alpha);
void gfx_blit_scaled(Image *dst, const Image *src, int x, int y, int w, int h, int alpha);
/* Компакт-диск: радужный блик поворачивается на angle (0..1 — полный оборот). */
void gfx_disc(Image *dst, int cx, int cy, int r, float angle, int alpha);

int  gfx_text(Image *dst, const Font *f, int x, int y, const char *utf8, uint32_t argb, int scale);
int  gfx_text_width(const Font *f, const char *utf8, int scale);
/* Значок кнопки геймпада, как на Xbox: A/B/X/Y — цветные кружки, остальные (LB, View,
 * Start, L3…) — плашки с надписью. Высота f->h + 2; возвращает ширину. */
int  gfx_pad_button(Image *dst, const Font *f, int x, int y, const char *name);
/* Строка подсказок «кнопка — действие»: пары через \t, например "A\tиграть\tB\tназад".
 * Имя в квадратных скобках ("[Esc]") — клавиша клавиатуры. dst == NULL — только ширина. */
int  gfx_hints(Image *dst, const Font *f, int x, int y, const char *pairs, uint32_t text);

static inline uint32_t gfx_mix(uint32_t d, uint32_t s, unsigned a)
{
    uint32_t rb = (((s & 0xFF00FFu) * a) + ((d & 0xFF00FFu) * (255 - a))) >> 8;
    uint32_t g  = (((s & 0x00FF00u) * a) + ((d & 0x00FF00u) * (255 - a))) >> 8;

    return 0xFF000000u | (rb & 0xFF00FFu) | (g & 0x00FF00u);
}

#endif
