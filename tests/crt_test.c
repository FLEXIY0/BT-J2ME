#define _POSIX_C_SOURCE 199309L
/* Проверка ЭЛТ-кинескопа:
 *   crt_test             — самопроверка (печатает CRT DONE fails=N);
 *   crt_test in.ppm out.ppm kx ky — рисует картинку;
 *   crt_test --bench     — время кадра на размерах экранов NES, J2ME и PS1. */
#include "../src/crt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t *read_ppm(const char *path, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    int maxv, i;
    uint32_t *px;
    unsigned char rgb[3];

    if (!f || fscanf(f, "P6 %d %d %d", w, h, &maxv) != 3 || fgetc(f) == EOF) {
        if (f)
            fclose(f);
        return NULL;
    }
    px = malloc((size_t)*w * *h * 4);
    for (i = 0; px && i < *w * *h; i++) {
        if (fread(rgb, 1, 3, f) != 3)
            break;
        px[i] = 0xFF000000u | (uint32_t)rgb[0] << 16 | (uint32_t)rgb[1] << 8 | rgb[2];
    }
    fclose(f);
    return px;
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void bench(int sw, int sh, int k, const char *name, int threads)
{
    CrtParams p;
    uint32_t *src = malloc((size_t)sw * sh * 4), *dst = malloc((size_t)sw * k * sh * k * 4);
    int i, n = 30;
    double t;

    for (i = 0; i < sw * sh; i++)
        src[i] = 0xFF000000u | (uint32_t)(i * 2654435761u >> 8);
    crt_defaults(&p);
    crt_render(src, sw, sh, dst, k, k, &p, threads);   /* таблицы */
    t = now();
    for (i = 0; i < n; i++)
        crt_render(src, sw, sh, dst, k, k, &p, threads);
    t = (now() - t) / n;
    printf("%-26s %dx%d -> %dx%d, потоков %d: %.1f мс/кадр\n", name, sw, sh, sw * k, sh * k, threads, t * 1000);
    free(src);
    free(dst);
}

static int fails;

static void expect(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
}

static int lum(uint32_t p)
{
    return (int)((p >> 16 & 255) * 77 + (p >> 8 & 255) * 150 + (p & 255) * 29) >> 8;
}

/* Самопроверка на простых картинках. */
static int self_test(void)
{
    enum { W = 32, H = 24, K = 4 };
    static uint32_t src[W * H], dst[W * K * H * K], dst2[W * K * H * K];
    CrtParams p;
    int x, y, mid = 0, edge = 0, i;
    long sum = 0;

    crt_defaults(&p);
    /* серое поле: строки развёртки — середина строки ярче краёв, средняя яркость не провалена */
    for (i = 0; i < W * H; i++)
        src[i] = 0xFF808080u;
    expect(crt_render(src, W, H, dst, K, K, &p, 1) == 0, "рисует серое поле");
    for (y = 8 * K; y < 9 * K; y++)
        for (x = 8 * K; x < 24 * K; x++) {
            int l = lum(dst[y * W * K + x]);
            sum += l;
            if (y % K == K / 2 - 1 || y % K == K / 2)
                mid += l;
            else
                edge += l;
        }
    expect(mid * 10 > edge * 14, "середина строки заметно ярче её краёв (строки развёртки)");
    expect(sum / (K * 16 * K) > 60, "средняя яркость не провалена");
    /* потоки не меняют результат */
    expect(crt_render(src, W, H, dst2, K, K, &p, 3) == 0 && !memcmp(dst, dst2, sizeof(dst)),
           "3 потока рисуют то же, что 1");
    /* одна красная точка на чёрном: цвет растекается вбок дальше, чем вверх */
    memset(src, 0, sizeof(src));
    for (i = 0; i < W * H; i++)
        src[i] = 0xFF000000u;
    src[12 * W + 16] = 0xFFFF0000u;
    crt_render(src, W, H, dst, K, K, &p, 1);
    {
        int cy = 12 * K + K / 2, cx = 16 * K + K / 2;
        int side = (int)(dst[cy * W * K + cx + 2 * K] >> 16 & 255);
        int up = (int)(dst[(cy - 2 * K) * W * K + cx] >> 16 & 255);
        expect(side > up, "красный растекается вбок, а не вверх");
    }
    /* настройки по умолчанию — внутри своих пределов */
    for (i = 0; i < CRT_NPARAM; i++)
        expect(crt_param_info[i].def >= crt_param_info[i].min && crt_param_info[i].def <= crt_param_info[i].max,
               crt_param_info[i].key);
    crt_free();
    printf("CRT DONE fails=%d\n", fails);
    return fails != 0;
}

int main(int argc, char **argv)
{
    CrtParams p;
    uint32_t *src, *dst;
    int w, h, kx, ky, i;
    FILE *f;

    if (argc == 2 && !strcmp(argv[1], "--bench")) {
        int t;
        for (t = 1; t <= 2; t++) {
            bench(256, 240, 4, "NES / Денди, x4", t);
            bench(240, 320, 3, "J2ME 240x320, x3", t);
            bench(176, 208, 4, "J2ME 176x208, x4", t);
            bench(320, 240, 3, "PlayStation 320x240, x3", t);
        }
        return 0;
    }
    if (argc == 1)
        return self_test();
    if (argc != 5 || !(src = read_ppm(argv[1], &w, &h)))
        return 2;
    kx = atoi(argv[3]);
    ky = atoi(argv[4]);
    dst = malloc((size_t)w * kx * h * ky * 4);
    crt_defaults(&p);
    if (crt_render(src, w, h, dst, kx, ky, &p, 2))
        return 1;
    f = fopen(argv[2], "wb");
    fprintf(f, "P6 %d %d 255\n", w * kx, h * ky);
    for (i = 0; i < w * kx * h * ky; i++) {
        unsigned char rgb[3] = { (unsigned char)(dst[i] >> 16), (unsigned char)(dst[i] >> 8), (unsigned char)dst[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}
