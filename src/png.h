/* Декодер PNG на zlib: все типы цвета, глубины 1–16 бит, прозрачность tRNS, Adam7.
 * MIDP требует PNG для Image.createImage, лаунчеру он нужен для иконок и обложек. */
#ifndef BT_PNG_H
#define BT_PNG_H

#include <stddef.h>
#include <stdint.h>

/* Возвращает пиксели 0xAARRGGBB (освобождать free()) или NULL. */
uint32_t *png_decode(const uint8_t *buf, size_t len, int *w, int *h);

#endif
