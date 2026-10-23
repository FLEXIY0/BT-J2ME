/* Нативная часть MIDP: рисование для javax.microedition.lcdui.Graphics, картинки, текст,
 * события ввода, сохранения RMS и цикл, который крутит Java-машину вместе с экраном. */
#define _DEFAULT_SOURCE
#include "midp.h"
#include "audio.h"
#include "gfx.h"
#include "jvm.h"
#include "manifest.h"
#include "png.h"
#include "rt.h"
#include "compat.h"
#include "util.h"

#include <errno.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ---------- состояние запуска ---------- */

#define EVQ_LEN 64
#define KEY_SLOTS 160   /* коды клавиш MIDP от -16 до 143 */
#define MIN_PRESS_MS 100

typedef struct {
    int type, a, b;
} MEvent;

static struct {
    int       w, h;
    Manifest  mf, jad;
    int       have_mf, have_jad;
    long      jar_size;     /* MIDlet-Jar-Size, если JAD нет */
    int       wide, ow, oh;     /* широкоэкранный режим: исходный размер игры */
    char      shots[1100];      /* папка скриншотов игры */
    uint32_t  t_start;
    int       title_done;       /* снимок «нажмите, чтобы играть» уже есть */
    char      rms_dir[1024];
    MEvent    q[EVQ_LEN];
    int       q_head, q_len;
    int       wake_flag;
    Thread   *waiter;
    uint32_t  wait_deadline;
    unsigned  frames;
    uint32_t  pressed_at[KEY_SLOTS];  /* когда нажали (0 — не нажата) */
    uint32_t  release_at[KEY_SLOTS];  /* отложенное отпускание (0 — нет) */
    int       g_slots_ready;
    int       s_buf, s_bw, s_bh, s_cx, s_cy, s_cw, s_ch, s_color, s_stroke;
} M;

enum { MEV_KEY_DOWN = 1, MEV_KEY_UP, MEV_PTR_DOWN, MEV_PTR_UP, MEV_PTR_MOVE, MEV_CHAR, MEV_EXIT };

static void evq_push(int type, int a, int b)
{
    MEvent *e;

    if (M.q_len == EVQ_LEN)
        return;
    e = &M.q[(M.q_head + M.q_len++) % EVQ_LEN];
    e->type = type;
    e->a = a;
    e->b = b;
}

/* Игры часто опрашивают клавиши раз в кадр: слишком короткое нажатие они не заметят.
 * На телефоне кнопку держали минимум 50–100 мс, поэтому отпускание придерживаем. */
static void key_down(int key, int printable)
{
    int k = key + 16;

    if (k >= 0 && k < KEY_SLOTS) {
        if (M.release_at[k]) {          /* нажали снова, пока ждали отпускания */
            evq_push(MEV_KEY_UP, key, 0);
            M.release_at[k] = 0;
        }
        M.pressed_at[k] = jvm_now() | 1;
    }
    evq_push(MEV_KEY_DOWN, key, printable);
}

static void key_up(int key)
{
    int k = key + 16;
    uint32_t now = jvm_now();

    if (k >= 0 && k < KEY_SLOTS && M.pressed_at[k] && now - M.pressed_at[k] < MIN_PRESS_MS) {
        M.release_at[k] = M.pressed_at[k] + MIN_PRESS_MS;
        return;
    }
    if (k >= 0 && k < KEY_SLOTS)
        M.pressed_at[k] = 0;
    evq_push(MEV_KEY_UP, key, 0);
}

static void deliver_releases(void)
{
    uint32_t now = jvm_now();
    int k;

    for (k = 0; k < KEY_SLOTS; k++)
        if (M.release_at[k] && (int32_t)(now - M.release_at[k]) >= 0) {
            M.release_at[k] = 0;
            M.pressed_at[k] = 0;
            evq_push(MEV_KEY_UP, k - 16, 0);
        }
}

/* ---------- контекст рисования из полей объекта Graphics ---------- */

typedef struct {
    uint32_t *buf;
    int       bw, bh;
    int       x0, y0, x1, y1;   /* отсечение, x1/y1 не включительно */
    uint32_t  color;
    int       dotted;
} G;

static int load_g(Object *g, G *o)
{
    Value *f = OBJ_FIELDS(g);
    Object *buf;

    if (!M.g_slots_ready) {
        Class *c = g->cls;
        while (c && strcmp(c->name, "javax/microedition/lcdui/Graphics"))
            c = c->super;
        if (!c)
            return 0;
        M.s_buf = jvm_field_slot(c, "buf");
        M.s_bw = jvm_field_slot(c, "bw");
        M.s_bh = jvm_field_slot(c, "bh");
        M.s_cx = jvm_field_slot(c, "cx");
        M.s_cy = jvm_field_slot(c, "cy");
        M.s_cw = jvm_field_slot(c, "cw");
        M.s_ch = jvm_field_slot(c, "ch");
        M.s_color = jvm_field_slot(c, "color");
        M.s_stroke = jvm_field_slot(c, "stroke");
        M.g_slots_ready = 1;
    }
    buf = f[M.s_buf].l;
    if (!buf)
        return 0;
    o->buf = ARR_OF(buf, uint32_t);
    o->bw = f[M.s_bw].i;
    o->bh = f[M.s_bh].i;
    if ((int64_t)o->bw * o->bh > buf->length)
        return 0;
    o->x0 = f[M.s_cx].i;
    o->y0 = f[M.s_cy].i;
    o->x1 = o->x0 + f[M.s_cw].i;
    o->y1 = o->y0 + f[M.s_ch].i;
    if (o->x0 < 0) o->x0 = 0;
    if (o->y0 < 0) o->y0 = 0;
    if (o->x1 > o->bw) o->x1 = o->bw;
    if (o->y1 > o->bh) o->y1 = o->bh;
    o->color = (uint32_t)f[M.s_color].i | 0xFF000000u;
    o->dotted = f[M.s_stroke].i == 1;
    return o->x0 < o->x1 && o->y0 < o->y1;
}

static inline void plot(G *g, int x, int y)
{
    if (x >= g->x0 && x < g->x1 && y >= g->y0 && y < g->y1)
        g->buf[y * g->bw + x] = g->color;
}

static void hspan(G *g, int x0, int x1, int y)   /* [x0, x1) */
{
    uint32_t *p;

    if (y < g->y0 || y >= g->y1)
        return;
    if (x0 < g->x0)
        x0 = g->x0;
    if (x1 > g->x1)
        x1 = g->x1;
    for (p = g->buf + y * g->bw + x0; x0 < x1; x0++)
        *p++ = g->color;
}

static void line(G *g, int x1, int y1, int x2, int y2)
{
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1, dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy, n = 0;

    for (;;) {
        if (!g->dotted || (n & 2) == 0)
            plot(g, x1, y1);
        n++;
        if (x1 == x2 && y1 == y2)
            break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x1 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y1 += sy;
            }
        }
    }
}

/* ---------- нативы Graphics ---------- */

static int g_fill0(Thread *t, Value *a, Value *r)
{
    G g;
    int x = a[1].i, y = a[2].i, w = a[3].i, h = a[4].i, j;

    (void)t;
    (void)r;
    if (!load_g(a[0].l, &g))
        return NATIVE_OK;
    for (j = y < g.y0 ? g.y0 : y; j < y + h && j < g.y1; j++)
        hspan(&g, x, x + w, j);
    return NATIVE_OK;
}

static int g_line0(Thread *t, Value *a, Value *r)
{
    G g;

    (void)t;
    (void)r;
    if (load_g(a[0].l, &g))
        line(&g, a[1].i, a[2].i, a[3].i, a[4].i);
    return NATIVE_OK;
}

/* Угол точки на эллипсе в градусах MIDP: 0 — «три часа», против часовой стрелки. */
static int angle_in(double ang, int start, int arc)
{
    double d;

    if (arc >= 360)
        return 1;
    d = fmod(ang - start, 360.0);
    if (d < 0)
        d += 360.0;
    return d <= arc;
}

static int g_arc0(Thread *t, Value *a, Value *r)
{
    G g;
    int x = a[1].i, y = a[2].i, w = a[3].i, h = a[4].i, start = a[5].i, arc = a[6].i, fill = a[7].i;
    double cx, cy, rx, ry;
    int i, j;

    (void)t;
    (void)r;
    if (!load_g(a[0].l, &g) || arc == 0)
        return NATIVE_OK;
    if (arc < 0) {
        start += arc;
        arc = -arc;
    }
    start %= 360;
    if (start < 0)
        start += 360;
    if (fill) {
        cx = x + w / 2.0;
        cy = y + h / 2.0;
        rx = w / 2.0;
        ry = h / 2.0;
        for (j = y; j < y + h; j++) {
            double dy = (j + 0.5 - cy) / ry;
            if (j < g.y0 || j >= g.y1 || dy * dy > 1)
                continue;
            for (i = x; i < x + w; i++) {
                double dx = (i + 0.5 - cx) / rx;
                if (dx * dx + dy * dy <= 1.0 &&
                    angle_in(atan2(-(j + 0.5 - cy) / ry, (i + 0.5 - cx) / rx) * 180.0 / M_PI, start, arc))
                    plot(&g, i, j);
            }
        }
    } else {
        /* контур: обходим эллипс (w+1)x(h+1) с шагом меньше пикселя */
        int steps = (int)((w + h + 2) * 3.2) + 8, k, lx = INT32_MIN, ly = INT32_MIN;
        cx = x + w / 2.0;
        cy = y + h / 2.0;
        rx = w / 2.0;
        ry = h / 2.0;
        for (k = 0; k <= steps; k++) {
            double ang = start + (double)(arc > 360 ? 360 : arc) * k / steps, rad = ang * M_PI / 180.0;
            int px = (int)floor(cx + rx * cos(rad) + 0.5), py = (int)floor(cy - ry * sin(rad) + 0.5);
            if (px != lx || py != ly)
                plot(&g, px, py);
            lx = px;
            ly = py;
        }
    }
    return NATIVE_OK;
}

static int g_roundRect0(Thread *t, Value *a, Value *r)
{
    G g;
    int x = a[1].i, y = a[2].i, w = a[3].i, h = a[4].i, aw = abs(a[5].i), ah = abs(a[6].i), fill = a[7].i;
    double rx, ry;
    int j;

    (void)t;
    (void)r;
    if (!load_g(a[0].l, &g))
        return NATIVE_OK;
    if (aw > w)
        aw = w;
    if (ah > h)
        ah = h;
    rx = aw / 2.0;
    ry = ah / 2.0;
    if (fill) {
        for (j = 0; j < h; j++) {
            double dy = 0;
            int in = 0;
            if (j < ry)
                dy = ry - j - 0.5;
            else if (j >= h - ry)
                dy = j + 0.5 - (h - ry);
            if (dy > 0 && ry > 0)
                in = (int)(rx - rx * sqrt(1 - (dy / ry) * (dy / ry)) + 0.5);
            hspan(&g, x + in, x + w - in, y + j);
        }
    } else {
        Value v[8];
        int i;
        hspan(&g, x + (int)rx, x + w - (int)rx + 1, y);
        hspan(&g, x + (int)rx, x + w - (int)rx + 1, y + h);
        for (i = y + (int)ry; i <= y + h - (int)ry; i++) {
            plot(&g, x, i);
            plot(&g, x + w, i);
        }
        /* углы — четверти эллипса */
        v[0] = a[0];
        v[3].i = aw;
        v[4].i = ah;
        v[6].i = 90;
        v[7].i = 0;
        v[1].i = x; v[2].i = y; v[5].i = 90;
        g_arc0(t, v, r);
        v[1].i = x + w - aw; v[2].i = y; v[5].i = 0;
        g_arc0(t, v, r);
        v[1].i = x; v[2].i = y + h - ah; v[5].i = 180;
        g_arc0(t, v, r);
        v[1].i = x + w - aw; v[2].i = y + h - ah; v[5].i = 270;
        g_arc0(t, v, r);
    }
    return NATIVE_OK;
}

static int g_triangle0(Thread *t, Value *a, Value *r)
{
    G g;
    int x1 = a[1].i, y1 = a[2].i, x2 = a[3].i, y2 = a[4].i, x3 = a[5].i, y3 = a[6].i;
    int minx, maxx, miny, maxy, i, j;
    int64_t area;

    (void)t;
    (void)r;
    if (!load_g(a[0].l, &g))
        return NATIVE_OK;
    area = (int64_t)(x2 - x1) * (y3 - y1) - (int64_t)(x3 - x1) * (y2 - y1);
    if (area == 0) {
        line(&g, x1, y1, x2, y2);
        line(&g, x2, y2, x3, y3);
        return NATIVE_OK;
    }
    minx = x1 < x2 ? (x1 < x3 ? x1 : x3) : (x2 < x3 ? x2 : x3);
    maxx = x1 > x2 ? (x1 > x3 ? x1 : x3) : (x2 > x3 ? x2 : x3);
    miny = y1 < y2 ? (y1 < y3 ? y1 : y3) : (y2 < y3 ? y2 : y3);
    maxy = y1 > y2 ? (y1 > y3 ? y1 : y3) : (y2 > y3 ? y2 : y3);
    if (minx < g.x0) minx = g.x0;
    if (miny < g.y0) miny = g.y0;
    if (maxx >= g.x1) maxx = g.x1 - 1;
    if (maxy >= g.y1) maxy = g.y1 - 1;
    for (j = miny; j <= maxy; j++)
        for (i = minx; i <= maxx; i++) {
            /* центр пикселя: удвоенные координаты, чтобы обойтись целыми */
            int64_t px = 2 * i + 1, py = 2 * j + 1;
            int64_t e1 = (2 * x2 - 2 * x1) * (py - 2 * y1) - (2 * y2 - 2 * y1) * (px - 2 * x1);
            int64_t e2 = (2 * x3 - 2 * x2) * (py - 2 * y2) - (2 * y3 - 2 * y2) * (px - 2 * x2);
            int64_t e3 = (2 * x1 - 2 * x3) * (py - 2 * y3) - (2 * y1 - 2 * y3) * (px - 2 * x3);
            if ((e1 >= 0 && e2 >= 0 && e3 >= 0) || (e1 <= 0 && e2 <= 0 && e3 <= 0))
                g.buf[j * g.bw + i] = g.color;
        }
    line(&g, x1, y1, x2, y2);
    line(&g, x2, y2, x3, y3);
    line(&g, x3, y3, x1, y1);
    return NATIVE_OK;
}

static inline void blend(uint32_t *d, uint32_t s)
{
    uint32_t al = s >> 24;

    if (al == 255)
        *d = s;
    else if (al)
        *d = gfx_mix(*d, s, al);
}

/* image0(int[] src, sw, sh, sx, sy, w, h, transform, dx, dy) */
static int g_image0(Thread *t, Value *a, Value *r)
{
    G g;
    Object *src = a[1].l;
    int sw = a[2].i, sh = a[3].i, sx = a[4].i, sy = a[5].i, w = a[6].i, h = a[7].i, tf = a[8].i;
    int dx = a[9].i, dy = a[10].i, dw, dh, i, j, i0, i1, j0, j1;
    long base, di, dj;
    const uint32_t *s;

    (void)r;
    if (!src)
        return jvm_throw(t, "java/lang/NullPointerException", NULL);
    if (!load_g(a[0].l, &g) || w <= 0 || h <= 0 || tf < 0 || tf > 7 || (int64_t)sw * sh > src->length ||
        sx < 0 || sy < 0 || sx + w > sw || sy + h > sh)
        return NATIVE_OK;
    s = ARR_OF(src, uint32_t);
    dw = tf >= 4 ? h : w;
    dh = tf >= 4 ? w : h;
    /* как пиксель (i, j) приёмника ложится на источник */
    switch (tf) {
    case 0: base = 0; di = 1; dj = sw; break;
    case 1: base = (long)(h - 1) * sw; di = 1; dj = -sw; break;
    case 2: base = w - 1; di = -1; dj = sw; break;
    case 3: base = (long)(h - 1) * sw + w - 1; di = -1; dj = -sw; break;
    case 4: base = 0; di = sw; dj = 1; break;
    case 5: base = (long)(h - 1) * sw; di = -sw; dj = 1; break;
    case 6: base = w - 1; di = sw; dj = -1; break;
    default: base = (long)(h - 1) * sw + w - 1; di = -sw; dj = -1; break;
    }
    base += (long)sy * sw + sx;
    i0 = dx < g.x0 ? g.x0 - dx : 0;
    j0 = dy < g.y0 ? g.y0 - dy : 0;
    i1 = dx + dw > g.x1 ? g.x1 - dx : dw;
    j1 = dy + dh > g.y1 ? g.y1 - dy : dh;
    for (j = j0; j < j1; j++) {
        uint32_t *d = g.buf + (dy + j) * g.bw + dx + i0;
        long si = base + j * dj + i0 * di;
        for (i = i0; i < i1; i++, si += di)
            blend(d++, s[si]);
    }
    return NATIVE_OK;
}

/* rgb0(int[] rgb, off, scan, x, y, w, h, alpha) */
static int g_rgb0(Thread *t, Value *a, Value *r)
{
    G g;
    Object *arr = a[1].l;
    int off = a[2].i, scan = a[3].i, x = a[4].i, y = a[5].i, w = a[6].i, h = a[7].i, alpha = a[8].i, i, j;
    int64_t first, last;
    const uint32_t *s;

    (void)r;
    if (!arr)
        return jvm_throw(t, "java/lang/NullPointerException", NULL);
    first = (int64_t)off + (scan < 0 ? (int64_t)(h - 1) * scan : 0);
    last = (int64_t)off + (scan < 0 ? 0 : (int64_t)(h - 1) * scan) + w - 1;
    if (first < 0 || last >= arr->length)
        return jvm_throw(t, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    if (!load_g(a[0].l, &g))
        return NATIVE_OK;
    s = ARR_OF(arr, uint32_t);
    for (j = 0; j < h; j++) {
        int py = y + j;
        if (py < g.y0 || py >= g.y1)
            continue;
        for (i = 0; i < w; i++) {
            int px = x + i;
            uint32_t p = s[off + j * scan + i];
            if (px < g.x0 || px >= g.x1)
                continue;
            if (alpha)
                blend(&g.buf[py * g.bw + px], p);
            else
                g.buf[py * g.bw + px] = p | 0xFF000000u;
        }
    }
    return NATIVE_OK;
}

static const Font *font_by_id(int id, int *fake_bold)
{
    *fake_bold = 0;
    switch (id & 0xFF) {
    case 0: return &font_small;
    case 1: *fake_bold = 1; return &font_small;
    case 2: return &font_mid;
    case 3: return &font_big;
    case 4: return &font_large;
    default: return &font_largeb;
    }
}

/* Пропорциональная ширина: пустые колонки глифа отбрасываем, между буквами — 1 пиксель.
 * Так моноширинный Terminus превращается в шрифт, похожий на телефонный. */
static void glyph_extent(const Font *f, const uint8_t *bits, int *left, int *width)
{
    int bpr = (f->w + 7) / 8, lo = f->w, hi = -1, x, y;

    for (y = 0; y < f->h; y++)
        for (x = 0; x < f->w; x++)
            if (bits[y * bpr + (x >> 3)] & (0x80 >> (x & 7))) {
                if (x < lo)
                    lo = x;
                if (x > hi)
                    hi = x;
            }
    if (hi < 0) {       /* пробел и прочие пустые */
        *left = 0;
        *width = f->w / 2;
    } else {
        *left = lo;
        *width = hi - lo + 1;
    }
}

static int char_advance(int id, unsigned c, int *left)
{
    static int8_t cache[6][0x500][2];   /* [шрифт][символ] → левый край+1, ширина */
    int bold, k = (id & 0xFF) > 5 ? 5 : (id & 0xFF), l, w;
    const Font *f = font_by_id(id, &bold);
    const uint8_t *bits;

    if (!(id & 0x200)) {
        *left = 0;
        return f->w + bold;
    }
    if (c < 0x500 && cache[k][c][0]) {
        *left = cache[k][c][0] - 1;
        return cache[k][c][1] + 1 + bold;
    }
    bits = gfx_glyph(f, c);
    if (bits)
        glyph_extent(f, bits, &l, &w);
    else {
        l = 0;
        w = f->w / 2;
    }
    if (c < 0x500) {
        cache[k][c][0] = (int8_t)(l + 1);
        cache[k][c][1] = (int8_t)w;
    }
    *left = l;
    return w + 1 + bold;
}

static int f_charWidth0(Thread *t, Value *a, Value *r)
{
    int left;

    (void)t;
    r->i = char_advance(a[0].i, (unsigned)(uint16_t)a[1].i, &left);
    return NATIVE_OK;
}

static int f_stringWidth0(Thread *t, Value *a, Value *r)
{
    int len, k, left, w = 0;
    const uint16_t *cs;

    if (!a[1].l)
        return jvm_throw(t, "java/lang/NullPointerException", NULL);
    cs = jvm_string_chars(a[1].l, &len);
    for (k = 0; cs && k < len; k++)
        w += char_advance(a[0].i, cs[k], &left);
    r->i = w;
    return NATIVE_OK;
}

/* text0(String s, x, y, fontId): x, y — левый верхний угол строки */
static int g_text0(Thread *t, Value *a, Value *r)
{
    G g;
    Object *str = a[1].l;
    int x = a[2].i, y = a[3].i, id = a[4].i, bold, len, k, gx, gy, b;
    const Font *f;
    const uint16_t *cs;
    int bpr;

    (void)r;
    if (!str)
        return jvm_throw(t, "java/lang/NullPointerException", NULL);
    if (!load_g(a[0].l, &g))
        return NATIVE_OK;
    f = font_by_id(id, &bold);
    bpr = (f->w + 7) / 8;
    cs = jvm_string_chars(str, &len);
    if (!cs)
        return NATIVE_OK;
    for (k = 0; k < len; k++) {
        const uint8_t *bits;
        int left, adv = char_advance(id, cs[k], &left), ox = x - left;
        if (x < g.x1 && x + adv > g.x0 && y < g.y1 && y + f->h > g.y0 && cs[k] != ' ' &&
            (bits = gfx_glyph(f, cs[k])) != NULL)
            for (gy = 0; gy < f->h; gy++)
                for (gx = 0; gx < f->w; gx++)
                    if (bits[gy * bpr + (gx >> 3)] & (0x80 >> (gx & 7)))
                        for (b = 0; b <= bold; b++)
                            plot(&g, ox + gx + b, y + gy);
        x += adv;
    }
    if (id & 0x100)   /* подчёркнутый */
        hspan(&g, a[2].i, x, y + f->h - 1);
    return NATIVE_OK;
}

static int g_copy0(Thread *t, Value *a, Value *r)
{
    G g;
    int sx = a[1].i, sy = a[2].i, w = a[3].i, h = a[4].i, dx = a[5].i, dy = a[6].i, j;
    uint32_t *tmp;

    (void)t;
    (void)r;
    if (!load_g(a[0].l, &g) || w <= 0 || h <= 0 || sx < 0 || sy < 0 || sx + w > g.bw || sy + h > g.bh)
        return NATIVE_OK;
    tmp = malloc((size_t)w * (size_t)h * 4);
    if (!tmp)
        return NATIVE_OK;
    for (j = 0; j < h; j++)
        memcpy(tmp + (size_t)j * w, g.buf + (sy + j) * g.bw + sx, (size_t)w * 4);
    for (j = 0; j < h; j++) {
        int py = dy + j, i;
        if (py < g.y0 || py >= g.y1)
            continue;
        for (i = 0; i < w; i++) {
            int px = dx + i;
            if (px >= g.x0 && px < g.x1)
                g.buf[py * g.bw + px] = tmp[(size_t)j * w + i];
        }
    }
    free(tmp);
    return NATIVE_OK;
}

/* ---------- нативы btvm.Native ---------- */

static int n_pollEvent(Thread *t, Value *a, Value *r)
{
    Object *out = a[0].l;
    MEvent *e;

    (void)t;
    if (!M.q_len || !out || out->length < 3) {
        r->i = 0;
        return NATIVE_OK;
    }
    e = &M.q[M.q_head];
    M.q_head = (M.q_head + 1) % EVQ_LEN;
    M.q_len--;
    ARR_OF(out, int32_t)[0] = e->a;
    ARR_OF(out, int32_t)[1] = e->b;
    ARR_OF(out, int32_t)[2] = 0;
    r->i = e->type;
    return NATIVE_OK;
}

static int n_waitEvent(Thread *t, Value *a, Value *r)
{
    uint32_t now = jvm_now();

    (void)r;
    if (M.q_len || M.wake_flag || (M.waiter == t && (int32_t)(now - M.wait_deadline) >= 0)) {
        M.wake_flag = 0;
        M.waiter = NULL;
        return NATIVE_OK;
    }
    if (M.waiter != t) {
        M.waiter = t;
        M.wait_deadline = now + (uint32_t)(a[0].i > 0 ? a[0].i : 1);
    }
    t->state = TH_WAIT_EVENT;
    t->timed = 1;
    t->wake_at = M.wait_deadline;
    return NATIVE_REEXEC;
}

static int n_wake(Thread *t, Value *a, Value *r)
{
    (void)t;
    (void)a;
    (void)r;
    M.wake_flag = 1;
    jvm_wake_event_waiters();
    return NATIVE_OK;
}

static int n_flush(Thread *t, Value *a, Value *r)
{
    Object *px = a[0].l;
    int w = a[1].i, h = a[2].i;
    Image *scr = platform_screen();

    (void)t;
    (void)r;
    if (!px || (int64_t)w * h > px->length || w != scr->w || h != scr->h)
        return NATIVE_OK;
    memcpy(scr->px, ARR_DATA(px), (size_t)w * (size_t)h * 4);
    platform_present();
    M.frames++;
    return NATIVE_OK;
}

static int n_screenWidth(Thread *t, Value *a, Value *r)
{
    (void)t;
    (void)a;
    r->i = M.w;
    return NATIVE_OK;
}

static int n_screenHeight(Thread *t, Value *a, Value *r)
{
    (void)t;
    (void)a;
    r->i = M.h;
    return NATIVE_OK;
}

static int n_exit(Thread *t, Value *a, Value *r)
{
    (void)a;
    (void)r;
    jvm_request_exit(0);
    t->yield = 1;
    return NATIVE_YIELD;
}

static int n_getAppProperty(Thread *t, Value *a, Value *r)
{
    char key[256];
    const char *v;

    jvm_string_utf8(a[0].l, key, sizeof(key));
    /* как на телефоне: сначала JAD, потом манифест */
    v = M.have_jad ? manifest_get(&M.jad, key) : NULL;
    if (!v && M.have_mf)
        v = manifest_get(&M.mf, key);
    if (!v && M.jar_size > 0 && !strcmp(key, "MIDlet-Jar-Size")) {
        /* без JAD его нет, а игры с платными SMS без него считают себя «не настроенными» */
        snprintf(key, sizeof(key), "%ld", M.jar_size);
        v = key;
    }
    r->l = v ? jvm_new_string(t, v) : NULL;
    return v && !r->l ? NATIVE_EXCEPTION : NATIVE_OK;
}

static int n_decodeImage(Thread *t, Value *a, Value *r)
{
    Object *data = a[0].l, *wh = a[3].l;
    int off = a[1].i, len = a[2].i, w, h;
    uint32_t *px;

    if (!data || !wh)
        return jvm_throw(t, "java/lang/NullPointerException", NULL);
    if (off < 0 || len < 0 || (int64_t)off + len > data->length || wh->length < 2)
        return jvm_throw(t, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    px = png_decode(ARR_OF(data, uint8_t) + off, (size_t)len, &w, &h);
    if (!px) {
        r->l = NULL;
        return NATIVE_OK;
    }
    r->l = jvm_new_array(t, 'I', NULL, w * h);
    if (r->l) {
        memcpy(ARR_DATA(r->l), px, (size_t)w * (size_t)h * 4);
        ARR_OF(wh, int32_t)[0] = w;
        ARR_OF(wh, int32_t)[1] = h;
    }
    free(px);
    return r->l ? NATIVE_OK : NATIVE_EXCEPTION;
}

/* ---------- RMS: по файлу на хранилище, имя закодировано в hex ---------- */

static void mkdirs(const char *path)
{
    char buf[1024];
    char *p;

    snprintf(buf, sizeof(buf), "%s", path);
    for (p = buf + 1; *p; p++)
        if (*p == '/') {
            *p = '\0';
            bt_mkdir(buf);
            *p = '/';
        }
    bt_mkdir(buf);
}

static void rms_path(Object *name, char *out, size_t cap)
{
    char utf[256], hex[520];
    int n = jvm_string_utf8(name, utf, sizeof(utf)), i;

    for (i = 0; i < n; i++)
        snprintf(hex + i * 2, 3, "%02x", (unsigned char)utf[i]);
    hex[n * 2] = '\0';
    snprintf(out, cap, "%s/%s.rms", M.rms_dir, hex);
}

static int n_rmsLoad(Thread *t, Value *a, Value *r)
{
    char path[1600];
    FILE *f;
    long len;

    rms_path(a[0].l, path, sizeof(path));
    r->l = NULL;
    if (!(f = bt_fopen(path, "rb")))
        return NATIVE_OK;
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        r->l = jvm_new_array(t, 'B', NULL, (int)len);
        if (r->l && fread(ARR_DATA(r->l), 1, (size_t)len, f) != (size_t)len)
            r->l = NULL;
    }
    fclose(f);
    return NATIVE_OK;
}

static int n_rmsSave(Thread *t, Value *a, Value *r)
{
    char path[1600], tmp[1700];
    Object *data = a[1].l;
    FILE *f;

    (void)t;
    mkdirs(M.rms_dir);
    rms_path(a[0].l, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    r->i = 0;
    if (!data || !(f = bt_fopen(tmp, "wb")))
        return NATIVE_OK;
    /* через временный файл: выключение посреди записи не испортит сохранение */
    r->i = fwrite(ARR_DATA(data), 1, (size_t)data->length, f) == (size_t)data->length;
    r->i &= fclose(f) == 0;
    if (r->i)
        r->i = bt_replace(tmp, path);
    return NATIVE_OK;
}

static int n_rmsDelete(Thread *t, Value *a, Value *r)
{
    char path[1600];

    (void)t;
    rms_path(a[0].l, path, sizeof(path));
    r->i = bt_remove(path);
    return NATIVE_OK;
}

static int n_rmsList(Thread *t, Value *a, Value *r)
{
    BtDir *d = bt_dir_open(M.rms_dir);
    const char *name;
    char names[64][128];
    int n = 0, i;
    Class *sc;

    (void)a;
    while (d && (name = bt_dir_next(d)) != NULL && n < 64) {
        size_t len = strlen(name), k;
        if (len < 5 || strcmp(name + len - 4, ".rms") || (len - 4) % 2 || (len - 4) / 2 >= sizeof(names[0]))
            continue;
        for (k = 0; k < (len - 4) / 2; k++) {
            unsigned v;
            sscanf(name + k * 2, "%2x", &v);
            names[n][k] = (char)v;
        }
        names[n][k] = '\0';
        n++;
    }
    if (d)
        bt_dir_close(d);
    sc = jvm_class(t, "java/lang/String");
    if (!sc || !(r->l = jvm_new_array(t, 'L', sc, n)))
        return NATIVE_EXCEPTION;
    for (i = 0; i < n; i++)
        if (!(ARR_OF(r->l, Object *)[i] = jvm_new_string(t, names[i])))
            return NATIVE_EXCEPTION;
    return NATIVE_OK;
}

/* ---------- звук (audio.c) ---------- */

static int s_create(Thread *t, Value *a, Value *r)
{
    Object *d = a[0].l;

    (void)t;
    r->i = snd_create(d ? ARR_OF(d, uint8_t) : NULL, d ? (size_t)d->length : 0, a[1].i == 1 ? SND_OTT : SND_AUTO);
    return NATIVE_OK;
}

static int s_createFreq(Thread *t, Value *a, Value *r)
{
    (void)t;
    r->i = snd_create_freq(a[0].i, a[1].i);
    return NATIVE_OK;
}

static int s_free(Thread *t, Value *a, Value *r)    { (void)t; (void)r; snd_free(a[0].i); return NATIVE_OK; }
static int s_play(Thread *t, Value *a, Value *r)    { (void)t; (void)r; snd_play(a[0].i); return NATIVE_OK; }
static int s_pause(Thread *t, Value *a, Value *r)   { (void)t; (void)r; snd_pause(a[0].i); return NATIVE_OK; }
static int s_playing(Thread *t, Value *a, Value *r) { (void)t; r->i = snd_playing(a[0].i); return NATIVE_OK; }
static int s_loops(Thread *t, Value *a, Value *r)   { (void)t; (void)r; snd_set_loops(a[0].i, a[1].i); return NATIVE_OK; }
static int s_volume(Thread *t, Value *a, Value *r)  { (void)t; (void)r; snd_set_volume(a[0].i, a[1].i); return NATIVE_OK; }
static int s_time(Thread *t, Value *a, Value *r)    { (void)t; r->j = snd_time_us(a[0].i); return NATIVE_OK; }
static int s_seek(Thread *t, Value *a, Value *r)    { (void)t; r->j = snd_seek_us(a[0].i, a[1].j); return NATIVE_OK; }
static int s_duration(Thread *t, Value *a, Value *r) { (void)t; r->j = snd_duration_us(a[0].i); return NATIVE_OK; }

static int s_tone(Thread *t, Value *a, Value *r)
{
    (void)t;
    (void)r;
    snd_tone(a[0].i, a[1].i, a[2].i);
    return NATIVE_OK;
}

static int n_textInput(Thread *t, Value *a, Value *r)
{
    (void)t;
    (void)r;
    platform_text_input(a[0].i);
    return NATIVE_OK;
}

static int n_vibrate(Thread *t, Value *a, Value *r)
{
    (void)t;
    (void)r;
    platform_vibrate(a[0].i);
    return NATIVE_OK;
}

static int n_openUrl(Thread *t, Value *a, Value *r)
{
    char url[1024];

    (void)t;
    r->i = 0;
    if (a[0].l && jvm_string_utf8(a[0].l, url, sizeof(url)) > 0)
        r->i = platform_open_url(url);
    return NATIVE_OK;
}

static const NativeDef midp_natives[] = {
    { "javax/microedition/lcdui/Graphics", "fill0", NULL, g_fill0 },
    { "javax/microedition/lcdui/Graphics", "line0", NULL, g_line0 },
    { "javax/microedition/lcdui/Graphics", "arc0", NULL, g_arc0 },
    { "javax/microedition/lcdui/Graphics", "roundRect0", NULL, g_roundRect0 },
    { "javax/microedition/lcdui/Graphics", "triangle0", NULL, g_triangle0 },
    { "javax/microedition/lcdui/Graphics", "image0", NULL, g_image0 },
    { "javax/microedition/lcdui/Graphics", "rgb0", NULL, g_rgb0 },
    { "javax/microedition/lcdui/Graphics", "text0", NULL, g_text0 },
    { "javax/microedition/lcdui/Graphics", "copy0", NULL, g_copy0 },
    { "javax/microedition/lcdui/Font", "charWidth0", NULL, f_charWidth0 },
    { "javax/microedition/lcdui/Font", "stringWidth0", NULL, f_stringWidth0 },
    { "btvm/Native", "pollEvent", NULL, n_pollEvent },
    { "btvm/Native", "waitEvent", NULL, n_waitEvent },
    { "btvm/Native", "wake", NULL, n_wake },
    { "btvm/Native", "flush", NULL, n_flush },
    { "btvm/Native", "screenWidth", NULL, n_screenWidth },
    { "btvm/Native", "screenHeight", NULL, n_screenHeight },
    { "btvm/Native", "exit", NULL, n_exit },
    { "btvm/Native", "getAppProperty", NULL, n_getAppProperty },
    { "btvm/Native", "decodeImage", NULL, n_decodeImage },
    { "btvm/Native", "rmsLoad", NULL, n_rmsLoad },
    { "btvm/Native", "rmsSave", NULL, n_rmsSave },
    { "btvm/Native", "rmsDelete", NULL, n_rmsDelete },
    { "btvm/Native", "rmsList", NULL, n_rmsList },
    { "btvm/Native", "textInput", NULL, n_textInput },
    { "btvm/Native", "vibrate", NULL, n_vibrate },
    { "btvm/Native", "openUrl", NULL, n_openUrl },
    { "btvm/Native", "sndCreate", NULL, s_create },
    { "btvm/Native", "sndCreateFreq", NULL, s_createFreq },
    { "btvm/Native", "sndFree", NULL, s_free },
    { "btvm/Native", "sndPlay", NULL, s_play },
    { "btvm/Native", "sndPause", NULL, s_pause },
    { "btvm/Native", "sndPlaying", NULL, s_playing },
    { "btvm/Native", "sndLoops", NULL, s_loops },
    { "btvm/Native", "sndVolume", NULL, s_volume },
    { "btvm/Native", "sndTime", NULL, s_time },
    { "btvm/Native", "sndSeek", NULL, s_seek },
    { "btvm/Native", "sndDuration", NULL, s_duration },
    { "btvm/Native", "sndTone", NULL, s_tone },
    { NULL, NULL, NULL, NULL }
};

/* ---------- запуск ---------- */

/* Размер экрана из манифеста: «Nokia-MIDlet-Original-Display-Size: 176,208» и похожие. */
static void screen_size_from_manifest(void)
{
    static const char *keys[] = { "Nokia-MIDlet-Original-Display-Size", "MIDlet-Screen-Size", "Display-Size" };
    size_t i;

    M.w = 240;
    M.h = 320;
    for (i = 0; M.have_mf && i < sizeof(keys) / sizeof(keys[0]); i++) {
        const char *v = manifest_get(&M.mf, keys[i]);
        int w, h;
        if (v && sscanf(v, "%d%*[ ,xX*]%d", &w, &h) == 2 && w >= 96 && h >= 64 && w <= 640 && h <= 640) {
            M.w = w;
            M.h = h;
            return;
        }
    }
}

/* Широкоэкранный режим: экран игры под форму телефона, короткая сторона — как у игры.
 * В портрете игре достаётся ~68% высоты (снизу кнопки), в альбоме — весь экран. */
static void wide_size(void)
{
    int dw, dh, s = M.w < M.h ? M.w : M.h, w, h;

    platform_display_size(&dw, &dh);
    if (dw <= 0 || dh <= 0)
        return;
    if (dh > dw) {
        w = s;
        h = (int)((float)s * (float)dh * 0.68f / (float)dw);
    } else {
        h = s;
        w = (int)((float)s * (float)dw / (float)dh);
    }
    M.ow = M.w;
    M.oh = M.h;
    M.w = (w > 640 ? 640 : w) & ~1;
    M.h = (h > 640 ? 640 : h) & ~1;
    M.wide = 1;
}

/* Многие игры берут размер экрана из своего конфига (scr_width=240 у Dynamic Pixels и др.):
 * в широкоэкранном режиме подменяем такие строки на новый размер. */
static int wide_key(const char *k, int n, const char *what)
{
    char low[64];
    int i;

    if (n <= 0 || n >= (int)sizeof(low))
        return 0;
    for (i = 0; i < n; i++)
        low[i] = (char)(k[i] >= 'A' && k[i] <= 'Z' ? k[i] + 32 : k[i]);
    low[n] = '\0';
    if (!strstr(low, "scr") && !strstr(low, "screen") && !strstr(low, "display"))
        return 0;
    return strstr(low, what) != NULL || (low[n - 1] == what[0] && (low[n - 2] == '_' || low[n - 2] == '-'));
}

static uint8_t *wide_filter(const char *path, uint8_t *data, size_t *len)
{
    char *out;
    size_t i = 0, o = 0;
    int changed = 0;

    (void)path;
    if (!M.wide || *len > 16384 || memchr(data, 0, *len) || !(out = malloc(*len + 64)))
        return data;
    while (i < *len) {
        size_t eol = i, eq, v0, v1;
        while (eol < *len && data[eol] != '\n')
            eol++;
        for (eq = i; eq < eol && data[eq] != '=' && data[eq] != ':'; eq++)
            ;
        v0 = eq + 1;
        while (v0 < eol && data[v0] == ' ')
            v0++;
        for (v1 = v0; v1 < eol && data[v1] >= '0' && data[v1] <= '9'; v1++)
            ;
        if (eq < eol && v1 > v0 && v1 - v0 < 5) {
            size_t ke = eq;
            int val = atoi((const char *)data + v0), nv = -1;
            while (ke > i && data[ke - 1] == ' ')
                ke--;
            if (val == M.ow && wide_key((const char *)data + i, (int)(ke - i), "width"))
                nv = M.w;
            else if (val == M.oh && wide_key((const char *)data + i, (int)(ke - i), "height"))
                nv = M.h;
            if (nv >= 0 && o + (v0 - i) + 8 < *len + 64) {
                memcpy(out + o, data + i, v0 - i);
                o += v0 - i;
                o += (size_t)sprintf(out + o, "%d", nv);
                i = v1;
                changed = 1;
            }
        }
        while (i <= eol && i < *len)
            out[o++] = (char)data[i++];
    }
    if (!changed) {
        free(out);
        return data;
    }
    free(data);
    *len = o;
    return (uint8_t *)out;
}

/* ---------- скриншоты: из них в лаунчере делают обложку ---------- */

static void shots_dir_for(const char *save_id)
{
    char base[800], id[256];
    size_t i;

    snprintf(id, sizeof(id), "%s", save_id);
    for (i = 0; id[i]; i++)
        if (id[i] == '/' || id[i] == '\\')
            id[i] = '_';
    bt_data_dir(base, sizeof(base));
    snprintf(M.shots, sizeof(M.shots), "%s/shots/%s", base, id);
}

/* Текущий кадр игры — в PNG. name NULL — снимок с датой. */
static int save_shot(const char *name)
{
    Image *scr = platform_screen();
    char path[1300], stamp[64];
    uint8_t *png;
    size_t len;
    FILE *f;
    int ok = 0;

    if (!name) {
        time_t now = time(NULL);
        strftime(stamp, sizeof(stamp), "shot-%Y%m%d-%H%M%S.png", localtime(&now));
        name = stamp;
    }
    bt_mkdirs(M.shots);
    snprintf(path, sizeof(path), "%s/%s", M.shots, name);
    if (!scr->px || !(png = png_encode(scr->px, scr->w, scr->h, &len)))
        return 0;
    if ((f = bt_fopen(path, "wb")) != NULL) {
        ok = fwrite(png, 1, len, f) == len;
        ok &= fclose(f) == 0;
    }
    free(png);
    if (ok)
        BT_LOG("снимок: %s\n", path);
    return ok;
}

/* Первое нажатие игрока (не раньше чем через 2,5 с) почти всегда приходится на заставку
 * «нажмите, чтобы играть» — её и запоминаем как обложку по умолчанию. */
static void maybe_title_shot(void)
{
    if (M.title_done || M.frames < 5 || jvm_now() - M.t_start < 2500)
        return;
    M.title_done = 1;
    save_shot("title.png");
}

/* <игра>.jad рядом с JAR: в нём бывают свойства, без которых игра не стартует. */
static void load_jad(const char *jar)
{
    char path[1100];
    size_t n = strlen(jar);
    FILE *f;
    char *buf;
    long len;

    if (n < 4 || n + 1 > sizeof(path))
        return;
    memcpy(path, jar, n - 4);
    strcpy(path + n - 4, jar[n - 1] == 'R' ? ".JAD" : ".jad");
    if (!(f = bt_fopen(path, "rb")))
        return;
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && len < (1 << 20) && fseek(f, 0, SEEK_SET) == 0 &&
        (buf = malloc((size_t)len)) != NULL) {
        if (fread(buf, 1, (size_t)len, f) == (size_t)len && manifest_parse(&M.jad, buf, (size_t)len) == 0)
            M.have_jad = 1;
        free(buf);
    }
    fclose(f);
}

static void rms_dir_for(const char *save_id)
{
    const char *xdg = getenv("XDG_DATA_HOME"), *app = bt_appdata();
    char id[256];
    size_t i;

    snprintf(id, sizeof(id), "%s", save_id);
    for (i = 0; id[i]; i++)
        if (id[i] == '/' || id[i] == '\\')
            id[i] = '_';
    if (xdg && *xdg)
        snprintf(M.rms_dir, sizeof(M.rms_dir), "%s/bt-j2me/rms/%s", xdg, id);
    else if (app)
        snprintf(M.rms_dir, sizeof(M.rms_dir), "%s/bt-j2me/rms/%s", app, id);
    else
        snprintf(M.rms_dir, sizeof(M.rms_dir), "%s/.local/share/bt-j2me/rms/%s", bt_home(), id);
}

MidpResult midp_run(const char *jar, const char *save_id, ScaleMode scale, char *err, size_t errlen)
{
    MidletInfo mi;
    const JarEntry *e;
    char *text = NULL;
    size_t len;
    uint32_t sleep_ms, exit_deadline = 0;
    int user_exit = 0, restart = 0, r;
    MidpResult result = MIDP_BACK;

    memset(&M, 0, sizeof(M));
    err[0] = '\0';
    if (jvm_create(bt_rt_jar, bt_rt_jar_len, jar)) {
        snprintf(err, errlen, "не удалось открыть игру");
        jvm_destroy();
        return MIDP_ERROR;
    }
    jvm_register_natives(midp_natives);

    e = jar_find(jvm_app_jar(), "META-INF/MANIFEST.MF");
    if (e && (text = (char *)jar_read(jvm_app_jar(), e, &len)) != NULL && manifest_parse(&M.mf, text, len) == 0)
        M.have_mf = 1;
    free(text);
    if (!M.have_mf || manifest_midlet(&M.mf, 1, &mi)) {
        snprintf(err, errlen, "в манифесте нет MIDlet-1");
        result = MIDP_ERROR;
        goto out;
    }
    load_jad(jar);
    {
        FILE *jf = bt_fopen(jar, "rb");
        M.jar_size = 0;
        if (jf) {
            if (fseek(jf, 0, SEEK_END) == 0)
                M.jar_size = ftell(jf);
            fclose(jf);
        }
    }
    screen_size_from_manifest();
    rms_dir_for(save_id);
    shots_dir_for(save_id);
    {
        char tp[1200];
        FILE *tf;
        snprintf(tp, sizeof(tp), "%s/title.png", M.shots);
        if ((tf = bt_fopen(tp, "rb")) != NULL) {
            M.title_done = 1;
            fclose(tf);
        }
    }
    M.t_start = jvm_now();
    platform_touch_profile(save_id);
    if (platform_touch_wide()) {
        wide_size();
        jvm_set_resource_filter(wide_filter);
    }
    platform_set_screen(M.w, M.h, scale);
    {
        Image *s = platform_screen();
        memset(s->px, 0, (size_t)s->w * (size_t)s->h * 4);
        platform_present();
    }
    if (jvm_start_main("javax/microedition/midlet/Runner", "main", "(Ljava/lang/String;)V", mi.class_name)) {
        snprintf(err, errlen, "в библиотеке нет Runner");
        result = MIDP_ERROR;
        goto out;
    }

    for (;;) {
        Event ev;

        while (platform_poll(&ev)) {
            switch (ev.type) {
            case EV_QUIT:
                result = MIDP_QUIT;
                goto out;
            case EV_RESTART:
                restart = 1;
                /* fall through */
            case EV_BACK:
                /* вежливо: destroyApp() даём полторы секунды, потом закрываем сами */
                if (!user_exit) {
                    user_exit = 1;
                    exit_deadline = jvm_now() + 1500;
                    evq_push(MEV_EXIT, 0, 0);
                }
                break;
            case EV_KEY_DOWN:
                maybe_title_shot();
                key_down(ev.key, ev.printable);
                break;
            case EV_SCREENSHOT:
                save_shot(NULL);
                break;
            case EV_KEY_UP:
                key_up(ev.key);
                break;
            case EV_POINTER_DOWN:
                maybe_title_shot();
                evq_push(MEV_PTR_DOWN, ev.x, ev.y);
                break;
            case EV_POINTER_UP:
                evq_push(MEV_PTR_UP, ev.x, ev.y);
                break;
            case EV_POINTER_MOVE:
                evq_push(MEV_PTR_MOVE, ev.x, ev.y);
                break;
            case EV_CHAR:
                evq_push(MEV_CHAR, ev.ch, 0);
                break;
            default:
                break;
            }
        }
        if (platform_paused()) {    /* меню паузы на ПК: машина стоит */
            platform_sleep_ms(16);
            continue;
        }
        deliver_releases();
        audio_pump();
        if (M.q_len)
            jvm_wake_event_waiters();
        if (user_exit && (int32_t)(jvm_now() - exit_deadline) >= 0)
            break;
        r = jvm_step(12, &sleep_ms);
        if (r < 0)
            break;
        if (r == 0)
            platform_sleep_ms(sleep_ms < 1 ? 1 : sleep_ms > 8 ? 8 : sleep_ms);
    }
    if (!user_exit && M.frames == 0) {
        snprintf(err, errlen, "игра завершилась, ничего не показав");
        result = MIDP_ERROR;
    }

out:
    platform_touch_profile(NULL);
    platform_text_input(0);
    audio_reset();     /* до jvm_destroy: звуки игры больше никому не нужны */
    if (M.have_mf)
        manifest_free(&M.mf);
    if (M.have_jad)
        manifest_free(&M.jad);
    jvm_set_resource_filter(NULL);
    jvm_destroy();
    if (restart && result == MIDP_BACK)     /* сменили размер экрана — запускаем заново */
        return midp_run(jar, save_id, scale, err, errlen);
    return result;
}
