/* Тестовая утилита: декодирует PNG и пишет "W H\n" + пиксели ARGB (big-endian) в stdout. */
#include "../src/png.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    static unsigned char buf[1 << 22];
    FILE *f;
    size_t len;
    uint32_t *px;
    int w, h, i;

    if (argc != 2 || !(f = fopen(argv[1], "rb")))
        return 2;
    len = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    px = png_decode(buf, len, &w, &h);
    if (!px)
        return 1;
    printf("%d %d\n", w, h);
    for (i = 0; i < w * h; i++) {
        putchar((int)(px[i] >> 24));
        putchar((int)(px[i] >> 16) & 255);
        putchar((int)(px[i] >> 8) & 255);
        putchar((int)px[i] & 255);
    }
    free(px);
    return 0;
}
