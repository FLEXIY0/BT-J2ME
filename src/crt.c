/* ЭЛТ-кинескоп на процессоре — см. crt.h.
 *
 * Кадр считается в два шага, оба по таблицам, без exp/pow в цикле:
 *   1. каждая строка игры «прочерчивается» лучом на ширину выхода:
 *      яркость Y — узким гауссом по 6 соседним пикселям (отдельно для лучей
 *      R, G, B — у них своё сведение); цвет I/Q — широким гауссом по 9
 *      пикселям, один раз на пиксель игры, дальше — линейно между ними;
 *      результат переводится в линейный свет;
 *   2. каждая экранная строка — сумма 2 ближайших строк игры с весом
 *      гауссова профиля строки (дальние дают меньше 1/1000); толщина профиля
 *      зависит от яркости; потом маска, яркость, насыщенность и гамма экрана.
 *      Этот шаг — самый частый, он целиком в целых числах.
 * Строки делятся между ядрами процессора (pthreads). */
#include "crt.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

const CrtParamInfo crt_param_info[CRT_NPARAM] = {
    { "scan_dark",   "Строка на тёмном",   0.132f, 0.05f, 0.60f, 0.01f },
    { "scan_bright", "Строка на светлом",  0.362f, 0.05f, 0.80f, 0.01f },
    { "sharp",       "Луч яркости",        0.633f, 0.20f, 1.50f, 0.05f },
    { "smear",       "Растекание цвета",   1.947f, 0.20f, 3.00f, 0.05f },
    { "color",       "Цветность сигнала",  2.00f, 0.00f, 3.00f, 0.10f },
    { "saturation",  "Насыщенность",       0.95f, 0.00f, 2.00f, 0.05f },
    { "brightness",  "Яркость",            1.45f, 0.50f, 3.00f, 0.05f },
    { "contrast",    "Контраст (гамма)",   2.80f, 1.50f, 4.00f, 0.10f },
    { "mask",        "Маска кинескопа",    0.441f, 0.00f, 1.00f, 0.02f },
    { "mask_pitch",  "Шаг маски",          0.607f, 0.30f, 1.50f, 0.01f },
    { "conv_r",      "Сведение красного",  0.157f, -1.0f, 1.00f, 0.02f },
    { "conv_b",      "Сведение синего",   -0.206f, -1.0f, 1.00f, 0.02f },
    { "gain_r",      "Баланс: красный",    1.05f, 0.70f, 1.40f, 0.01f },
    { "gain_b",      "Баланс: синий",      1.158f, 0.70f, 1.40f, 0.01f },
};

#define GAMMA_OUT 2.2f
#define LIN_N     1024      /* сигнал 0..1 → линейный свет */
#define ONE       4095      /* линейный свет в целых: 0..4095 */
#define HQ_N      256       /* яркость строки → толщина профиля */
#define MAX_THREADS 8
#define TAP_Y     6
#define TAP_C     4         /* полуширина размытия цвета: 2·4+1 пикселей */

static struct {
    int sw, sh, kx, ky, ow;
    CrtParams p;
    int valid;
    /* по выходным x: колонки и веса лучей (Y — на каждый луч R, G, B) */
    short *ycol;    /* [ow][3][TAP_Y] */
    float *yw;      /* [ow][3][TAP_Y] */
    short *cidx;    /* [ow] — пиксель игры слева от выходного x (для цвета) */
    float *cfrac;   /* [ow] — доля правого соседа */
    float  ck[2 * TAP_C + 1];  /* гаусс цвета по соседям */
    int   *mmul;    /* [ow][3] — маска × яркость × баланс, ×256 */
    uint16_t lin[LIN_N + 1];   /* сигнал → линейный свет 0..ONE */
    uint8_t out[ONE + 1];      /* линейный свет → байт экрана */
    int   *beam;    /* [ky][2][HQ_N] — вес строки ×4096 по положению внутри неё и яркости */
    int   *row0;    /* [ky] — первая из 2 строк игры относительно текущей */
    int   sat;      /* насыщенность ×256 */
    /* по кадру */
    uint16_t *h;    /* [sh][ow][3] — строки, прочерченные лучом, линейный свет 0..ONE */
} C;

/* Быстрое округление float → int без смены режима x87 (старые процессоры). */
static int fround(float v)
{
    union { float f; int32_t i; } u;
    u.f = v + 12582912.0f;      /* 1.5·2^23: целая часть оказывается в мантиссе */
    return u.i - 0x4B400000;
}

void crt_defaults(CrtParams *p)
{
    int i;

    for (i = 0; i < CRT_NPARAM; i++)
        p->v[i] = crt_param_info[i].def;
}

void crt_free(void)
{
    free(C.ycol);
    free(C.yw);
    free(C.cidx);
    free(C.cfrac);
    free(C.mmul);
    free(C.beam);
    free(C.row0);
    free(C.h);
    memset(&C, 0, sizeof(C));
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Таблицы под размеры и настройки. */
static int setup(int sw, int sh, int kx, int ky, const CrtParams *p)
{
    int ow = sw * kx, x, g, t, s, d, q, i;
    float hw, hwc, wmin, wmax, gi, pitch;

    if (C.valid && C.sw == sw && C.sh == sh && C.kx == kx && C.ky == ky && !memcmp(&C.p, p, sizeof(*p)))
        return 0;
    crt_free();
    C.ycol = malloc((size_t)ow * 3 * TAP_Y * sizeof(short));
    C.yw = malloc((size_t)ow * 3 * TAP_Y * sizeof(float));
    C.cidx = malloc((size_t)ow * sizeof(short));
    C.cfrac = malloc((size_t)ow * sizeof(float));
    C.mmul = malloc((size_t)ow * 3 * sizeof(int));
    C.beam = malloc((size_t)ky * 2 * HQ_N * sizeof(int));
    C.row0 = malloc((size_t)ky * sizeof(int));
    C.h = malloc((size_t)sh * ow * 3 * sizeof(uint16_t));
    if (!C.ycol || !C.yw || !C.cidx || !C.cfrac || !C.mmul || !C.beam || !C.row0 || !C.h) {
        crt_free();
        return -1;
    }
    C.sw = sw;
    C.sh = sh;
    C.kx = kx;
    C.ky = ky;
    C.ow = ow;
    C.p = *p;

    hw = p->v[CRT_SHARP] > 0.05f ? p->v[CRT_SHARP] : 0.05f;
    hwc = p->v[CRT_SMEAR] > 0.05f ? p->v[CRT_SMEAR] : 0.05f;
    /* горизонталь: центр выходного пикселя в пикселях игры, у R и B — со сведением */
    for (x = 0; x < ow; x++) {
        float sx = ((float)x + 0.5f) / (float)kx, sum = 0;
        int c0;
        for (g = 0; g < 3; g++) {
            float shift = g == 0 ? p->v[CRT_CONV_R] : g == 2 ? p->v[CRT_CONV_B] : 0.0f, xs = sx - shift;
            int base = (int)floorf(xs - 0.5f) - 2;
            short *col = C.ycol + ((size_t)x * 3 + g) * TAP_Y;
            float *w = C.yw + ((size_t)x * 3 + g) * TAP_Y;
            sum = 0;
            for (t = 0; t < TAP_Y; t++) {
                float dd = (xs - ((float)(base + t) + 0.5f)) / hw;
                w[t] = expf(-0.5f * dd * dd);
                col[t] = (short)clampi(base + t, 0, sw - 1);
                sum += w[t];
            }
            for (t = 0; t < TAP_Y; t++)
                w[t] /= sum > 1e-6f ? sum : 1e-6f;
        }
        c0 = (int)floorf(sx - 0.5f);
        C.cidx[x] = (short)c0;
        C.cfrac[x] = sx - 0.5f - (float)c0;
    }
    {
        float sum = 0;
        for (t = -TAP_C; t <= TAP_C; t++)
            sum += C.ck[t + TAP_C] = expf(-0.5f * (float)(t * t) / (hwc * hwc));
        for (t = 0; t <= 2 * TAP_C; t++)
            C.ck[t] /= sum;
    }
    /* апертурная решётка: шаг — в строках, фазы R, G, B через треть шага */
    pitch = p->v[CRT_MASK_PITCH] * (float)ky;
    if (pitch < 3.0f)           /* мельче трёх пикселей (R, G, B) экран не покажет — была бы рябь */
        pitch = 3.0f;
    for (x = 0; x < ow; x++)
        for (g = 0; g < 3; g++) {
            float m = 1.0f + p->v[CRT_MASK] * cosf(6.2831853f * ((float)x + 0.5f) / pitch - 2.0943951f * (float)g);
            float gain = g == 0 ? p->v[CRT_GAIN_R] : g == 2 ? p->v[CRT_GAIN_B] : 1.0f;
            C.mmul[x * 3 + g] = (int)(m * p->v[CRT_BRIGHT] * gain * 256.0f + 0.5f);
        }
    C.sat = (int)(p->v[CRT_SAT] * 256.0f + 0.5f);
    /* гамма сигнала и экрана */
    gi = p->v[CRT_CONTRAST];
    for (i = 0; i <= LIN_N; i++)
        C.lin[i] = (uint16_t)(powf((float)i / LIN_N, gi) * ONE + 0.5f);
    for (i = 0; i <= ONE; i++)
        C.out[i] = (uint8_t)(powf((float)i / ONE, 1.0f / GAMMA_OUT) * 255.0f + 0.5f);
    /* профиль строки: для каждой из ky экранных строк внутри строки игры
     * берём 2 ближайшие строки игры; вес — гаусс, нормированный по площади */
    wmin = p->v[CRT_SCAN_DARK];
    wmax = p->v[CRT_SCAN_BRIGHT];
    for (s = 0; s < ky; s++) {
        float sy = ((float)s + 0.5f) / (float)ky;          /* положение внутри строки, 0..1 */
        int r0 = (int)floorf(sy - 0.5f);                    /* -1 или 0 */
        C.row0[s] = r0;
        for (d = 0; d < 2; d++) {
            float dy = sy - ((float)(r0 + d) + 0.5f);
            for (q = 0; q < HQ_N; q++) {
                float w = wmin + (wmax - wmin) * sqrtf((float)q / (HQ_N - 1));
                if (w < 0.03f)
                    w = 0.03f;
                C.beam[((size_t)s * 2 + d) * HQ_N + q] =
                    (int)(expf(-0.5f * dy * dy / (w * w)) / (w * 2.5066283f) * 4096.0f + 0.5f);
            }
        }
    }
    C.valid = 1;
    return 0;
}

/* Шаг 1 для строк игры [j0, j1). */
static void trace_rows(const uint32_t *src, int j0, int j1, float color)
{
    int sw = C.sw, ow = C.ow, j, x, g, t;
    float *yiq = malloc((size_t)sw * 3 * sizeof(float)), *iq = malloc((size_t)(sw + 2) * 2 * sizeof(float));

    if (!yiq || !iq) {
        free(yiq);
        free(iq);
        return;
    }
    for (j = j0; j < j1; j++) {
        const uint32_t *row = src + (size_t)j * sw;
        uint16_t *hrow = C.h + (size_t)j * ow * 3;
        for (x = 0; x < sw; x++) {
            float r = (float)(row[x] >> 16 & 255) * (1.0f / 255), gg = (float)(row[x] >> 8 & 255) * (1.0f / 255),
                  b = (float)(row[x] & 255) * (1.0f / 255);
            yiq[x * 3] = 0.299f * r + 0.587f * gg + 0.114f * b;
            yiq[x * 3 + 1] = (0.596f * r - 0.274f * gg - 0.322f * b) * color;
            yiq[x * 3 + 2] = (0.211f * r - 0.523f * gg + 0.312f * b) * color;
        }
        /* цвет через широкий луч — на каждый пиксель игры (iq[0] и iq[sw+1] — края) */
        for (x = 0; x < sw; x++) {
            float ci = 0, cq = 0;
            for (t = -TAP_C; t <= TAP_C; t++) {
                int c = clampi(x + t, 0, sw - 1);
                ci += C.ck[t + TAP_C] * yiq[c * 3 + 1];
                cq += C.ck[t + TAP_C] * yiq[c * 3 + 2];
            }
            iq[(x + 1) * 2] = ci;
            iq[(x + 1) * 2 + 1] = cq;
        }
        iq[0] = iq[2];
        iq[1] = iq[3];
        iq[(sw + 1) * 2] = iq[sw * 2];
        iq[(sw + 1) * 2 + 1] = iq[sw * 2 + 1];
        for (x = 0; x < ow; x++) {
            int c0 = clampi(C.cidx[x], -1, sw - 1) + 1;
            float f = C.cfrac[x], c3[3];
            float ci = iq[c0 * 2] + (iq[c0 * 2 + 2] - iq[c0 * 2]) * f;
            float cq = iq[c0 * 2 + 1] + (iq[c0 * 2 + 3] - iq[c0 * 2 + 1]) * f;
            for (g = 0; g < 3; g++) {
                const short *yc = C.ycol + ((size_t)x * 3 + g) * TAP_Y;
                const float *yw = C.yw + ((size_t)x * 3 + g) * TAP_Y;
                float v = 0;
                for (t = 0; t < TAP_Y; t++)
                    v += yw[t] * yiq[yc[t] * 3];
                c3[g] = v;
            }
            c3[0] += 0.956f * ci + 0.621f * cq;
            c3[1] += -0.272f * ci - 0.647f * cq;
            c3[2] += -1.106f * ci + 1.703f * cq;
            for (g = 0; g < 3; g++) {
                float v = c3[g];
                v = v < 0 ? 0 : v > 1 ? 1 : v;
                hrow[x * 3 + g] = C.lin[fround(v * LIN_N)];
            }
        }
    }
    free(yiq);
    free(iq);
}

/* Шаг 2 для экранных строк [y0, y1). */
static void scan_rows(uint32_t *dst, int y0, int y1)
{
    int ow = C.ow, ky = C.ky, sh = C.sh, sat = C.sat, y, x, g;

    for (y = y0; y < y1; y++) {
        int j0 = y / ky, s = y - j0 * ky;
        int ra = clampi(j0 + C.row0[s], 0, sh - 1), rb = clampi(j0 + C.row0[s] + 1, 0, sh - 1);
        const uint16_t *ha = C.h + (size_t)ra * ow * 3, *hb = C.h + (size_t)rb * ow * 3;
        const int *ba = C.beam + (size_t)s * 2 * HQ_N, *bb = ba + HQ_N;
        uint32_t *out = dst + (size_t)y * ow;
        for (x = 0; x < ow; x++) {
            int c[3], o[3], l, k = x * 3;
            for (g = 0; g < 3; g++) {
                int va = ha[k + g], vb = hb[k + g];
                int acc = (va * ba[va >> 4] + vb * bb[vb >> 4]) >> 12;      /* линейный свет, ×ONE */
                c[g] = acc * C.mmul[k + g] >> 8;
            }
            l = (54 * c[0] + 183 * c[1] + 19 * c[2]) >> 8;
            for (g = 0; g < 3; g++) {
                int v = l + ((c[g] - l) * sat >> 8);
                o[g] = C.out[v < 0 ? 0 : v > ONE ? ONE : v];
            }
            out[x] = 0xFF000000u | (uint32_t)o[0] << 16 | (uint32_t)o[1] << 8 | (uint32_t)o[2];
        }
    }
}

typedef struct {
    const uint32_t *src;
    uint32_t *dst;
    int a, b, step;
    float color;
} Job;

static void *job_run(void *arg)
{
    Job *jb = arg;
    if (jb->step == 1)
        trace_rows(jb->src, jb->a, jb->b, jb->color);
    else
        scan_rows(jb->dst, jb->a, jb->b);
    return NULL;
}

/* Делит [0, n) на части по потокам; первая часть — в этом потоке. */
static void run_split(int step, const uint32_t *src, uint32_t *dst, int n, int threads, float color)
{
    pthread_t th[MAX_THREADS];
    Job jobs[MAX_THREADS];
    int started[MAX_THREADS] = { 0 }, i;

    if (threads < 1)
        threads = 1;
    if (threads > MAX_THREADS)
        threads = MAX_THREADS;
    if (threads > n)
        threads = n;
    for (i = 0; i < threads; i++) {
        jobs[i].src = src;
        jobs[i].dst = dst;
        jobs[i].a = n * i / threads;
        jobs[i].b = n * (i + 1) / threads;
        jobs[i].step = step;
        jobs[i].color = color;
    }
    for (i = 1; i < threads; i++)
        started[i] = pthread_create(&th[i], NULL, job_run, &jobs[i]) == 0;
    job_run(&jobs[0]);
    for (i = 1; i < threads; i++) {
        if (started[i])
            pthread_join(th[i], NULL);
        else
            job_run(&jobs[i]);      /* поток не создался — делаем сами */
    }
}

int crt_render(const uint32_t *src, int sw, int sh, uint32_t *dst, int kx, int ky, const CrtParams *p, int threads)
{
    if (sw <= 0 || sh <= 0 || kx <= 0 || ky <= 0 || setup(sw, sh, kx, ky, p))
        return -1;
    run_split(1, src, dst, sh, threads, p->v[CRT_COLOR]);
    run_split(2, src, dst, sh * ky, threads, 0);
    return 0;
}
