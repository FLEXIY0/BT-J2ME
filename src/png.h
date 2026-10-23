/* Декодер PNG на zlib: все типы цвета, глубины 1–16 бит, прозрачность tRNS, Adam7.
 * MIDP требует PNG для Image.createImage, лаунчеру он нужен для иконок и обложек. */
#ifndef BT_PNG_H
#define BT_PNG_H

#include <stddef.h>
#include <stdint.h>

/* Возвращает пиксели 0xAARRGGBB (освобождать free()) или NULL. */
uint32_t *png_decode(const uint8_t *buf, size_t len, int *w, int *h);

/* Кодирует 0xAARRGGBB в PNG (RGB, 8 бит; альфа отбрасывается). Буфер — free(), NULL при ошибке. */
uint8_t *png_encode(const uint32_t *px, int w, int h, size_t *len);

#endif
