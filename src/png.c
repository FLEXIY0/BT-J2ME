#include "png.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define PNG_MAX_SIDE 4096

typedef struct {
    int      w, h, depth, ctype, channels;
    uint32_t palette[256];       /* ARGB с учётом tRNS */
    int      trns_gray;          /* -1 если нет */
    int      trns_r, trns_g, trns_b;
} PngInfo;

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);

    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

/* Снимает фильтры со строк одного прохода; cur указывает на байт типа фильтра. */
static int unfilter(uint8_t *data, size_t stride, int rows, int bpp)
{
    uint8_t *prev = NULL;
    int y;
    size_t i;

    for (y = 0; y < rows; y++) {
        uint8_t ft = data[0], *row = data + 1;

        for (i = 0; i < stride; i++) {
            int a = i >= (size_t)bpp ? row[i - bpp] : 0;
            int b = prev ? prev[i] : 0;
            int c = prev && i >= (size_t)bpp ? prev[i - bpp] : 0;

            switch (ft) {
            case 0: break;
            case 1: row[i] = (uint8_t)(row[i] + a); break;
            case 2: row[i] = (uint8_t)(row[i] + b); break;
            case 3: row[i] = (uint8_t)(row[i] + ((a + b) >> 1)); break;
            case 4: row[i] = (uint8_t)(row[i] + paeth(a, b, c)); break;
            default: return -1;
            }
        }
        prev = row;
        data += stride + 1;
    }
    return 0;
}

static unsigned sample(const uint8_t *row, int idx, int depth)
{
    switch (depth) {
    case 1:  return (row[idx >> 3] >> (7 - (idx & 7))) & 1;
    case 2:  return (row[idx >> 2] >> (6 - 2 * (idx & 3))) & 3;
    case 4:  return (row[idx >> 1] >> (4 - 4 * (idx & 1))) & 15;
    case 8:  return row[idx];
    default: return rd_be16(row + 2 * idx);
    }
}

static uint32_t to_argb(const PngInfo *in, const uint8_t *row, int x)
{
    int d = in->depth, n = in->channels;
    unsigned max = (1u << d) - 1, s[4] = { 0, 0, 0, 0 }, k;
    /* 16 бит — берём старший байт (как libpng), меньше 8 — растягиваем до 0..255 */
#define SCALE(v) (d == 16 ? (v) >> 8 : (v) * 255 / max)

    for (k = 0; k < (unsigned)n; k++)
        s[k] = sample(row, x * n + (int)k, d);

    if (in->ctype == 3)
        return in->palette[s[0] & 255];
    if (in->ctype == 0 || in->ctype == 4) {
        unsigned g = SCALE(s[0]);
        unsigned a = in->ctype == 4 ? SCALE(s[1]) : ((int)s[0] == in->trns_gray ? 0 : 255);
        return (a << 24) | (g << 16) | (g << 8) | g;
    }
    {
        unsigned r = SCALE(s[0]), g = SCALE(s[1]), b = SCALE(s[2]), a = 255;

        if (in->ctype == 6)
            a = SCALE(s[3]);
        else if ((int)s[0] == in->trns_r && (int)s[1] == in->trns_g && (int)s[2] == in->trns_b)
            a = 0;
        return (a << 24) | (r << 16) | (g << 8) | b;
    }
#undef SCALE
}

static const int adam7[7][4] = {   /* x0, y0, dx, dy */
    { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 },
    { 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 }
};

uint32_t *png_decode(const uint8_t *buf, size_t len, int *out_w, int *out_h)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    PngInfo in;
    const uint8_t *p = buf + 8, *end = buf + len;
    uint8_t *idat = NULL, *raw = NULL;
    size_t idat_len = 0, raw_len = 0, pos;
    uint32_t *px = NULL;
    int interlace = 0, have_hdr = 0, pass, bits, bpp, i;

    if (len < 8 || memcmp(buf, sig, 8) != 0)
        return NULL;
    memset(&in, 0, sizeof(in));
    in.trns_gray = in.trns_r = -1;
    for (i = 0; i < 256; i++)
        in.palette[i] = 0xFF000000u;

    while (p + 12 <= end) {
        uint32_t clen = rd_be32(p);
        const uint8_t *type = p + 4, *d = p + 8;

        if (clen > (size_t)(end - d) - 4)
            goto fail;
        if (!memcmp(type, "IHDR", 4) && clen >= 13) {
            in.w = (int)rd_be32(d);
            in.h = (int)rd_be32(d + 4);
            in.depth = d[8];
            in.ctype = d[9];
            interlace = d[12];
            have_hdr = 1;
        } else if (!memcmp(type, "PLTE", 4)) {
            for (i = 0; i < (int)(clen / 3) && i < 256; i++)
                in.palette[i] = 0xFF000000u | ((uint32_t)d[i * 3] << 16) | ((uint32_t)d[i * 3 + 1] << 8) | d[i * 3 + 2];
        } else if (!memcmp(type, "tRNS", 4)) {
            if (in.ctype == 3)
                for (i = 0; i < (int)clen && i < 256; i++)
                    in.palette[i] = (in.palette[i] & 0x00FFFFFFu) | ((uint32_t)d[i] << 24);
            else if (in.ctype == 0 && clen >= 2)
                in.trns_gray = rd_be16(d);
            else if (in.ctype == 2 && clen >= 6) {
                in.trns_r = rd_be16(d);
                in.trns_g = rd_be16(d + 2);
                in.trns_b = rd_be16(d + 4);
            }
        } else if (!memcmp(type, "IDAT", 4)) {
            uint8_t *n = realloc(idat, idat_len + clen + 1);
            if (!n)
                goto fail;
            idat = n;
            memcpy(idat + idat_len, d, clen);
            idat_len += clen;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        p = d + clen + 4;   /* CRC не проверяем: битые картинки всё равно отсекает zlib */
    }

    if (!have_hdr || !idat || in.w <= 0 || in.h <= 0 || in.w > PNG_MAX_SIDE || in.h > PNG_MAX_SIDE)
        goto fail;
    switch (in.ctype) {
    case 0: in.channels = 1; break;
    case 2: in.channels = 3; break;
    case 3: in.channels = 1; break;
    case 4: in.channels = 2; break;
    case 6: in.channels = 4; break;
    default: goto fail;
    }
    if (in.depth != 1 && in.depth != 2 && in.depth != 4 && in.depth != 8 && in.depth != 16)
        goto fail;
    bits = in.channels * in.depth;
    bpp = bits >= 8 ? bits / 8 : 1;

    /* Размер распакованных данных: по строке на каждый проход + байт фильтра. */
    for (pass = 0; pass < (interlace ? 7 : 1); pass++) {
        const int *a = interlace ? adam7[pass] : NULL;
        size_t pw = a ? (size_t)(in.w - a[0] + a[2] - 1) / a[2] : (size_t)in.w;
        size_t ph = a ? (size_t)(in.h - a[1] + a[3] - 1) / a[3] : (size_t)in.h;
        if (pw && ph)
            raw_len += ph * (1 + (pw * bits + 7) / 8);
    }
    raw = malloc(raw_len);
    px = malloc((size_t)in.w * in.h * sizeof(uint32_t));
    if (!raw || !px)
        goto fail;
    {
        z_stream zs;
        int rc;

        memset(&zs, 0, sizeof(zs));
        zs.next_in = idat;
        zs.avail_in = (uInt)idat_len;
        zs.next_out = raw;
        zs.avail_out = (uInt)raw_len;
        if (inflateInit(&zs) != Z_OK)
            goto fail;
        rc = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);
        if ((rc != Z_STREAM_END && rc != Z_BUF_ERROR) || zs.total_out != raw_len)
            goto fail;
    }

    pos = 0;
    for (pass = 0; pass < (interlace ? 7 : 1); pass++) {
        int x0 = 0, y0 = 0, dx = 1, dy = 1, pw, ph, x, y;
        size_t stride;

        if (interlace) {
            x0 = adam7[pass][0];
            y0 = adam7[pass][1];
            dx = adam7[pass][2];
            dy = adam7[pass][3];
        }
        pw = (in.w - x0 + dx - 1) / dx;
        ph = (in.h - y0 + dy - 1) / dy;
        if (pw <= 0 || ph <= 0)
            continue;
        stride = ((size_t)pw * bits + 7) / 8;
        if (unfilter(raw + pos, stride, ph, bpp))
            goto fail;
        for (y = 0; y < ph; y++) {
            const uint8_t *row = raw + pos + (size_t)y * (stride + 1) + 1;
            for (x = 0; x < pw; x++)
                px[(size_t)(y0 + y * dy) * in.w + x0 + x * dx] = to_argb(&in, row, x);
        }
        pos += (size_t)ph * (stride + 1);
    }

    free(idat);
    free(raw);
    *out_w = in.w;
    *out_h = in.h;
    return px;

fail:
    free(idat);
    free(raw);
    free(px);
    return NULL;
}

/* ---------- запись (скриншоты игр) ---------- */

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Чанк: длина, тип, данные, CRC по типу и данным. */
static uint8_t *put_chunk(uint8_t *p, const char *type, const uint8_t *data, uint32_t n)
{
    uLong crc;

    put_be32(p, n);
    memcpy(p + 4, type, 4);
    if (n)
        memcpy(p + 8, data, n);
    crc = crc32(0L, p + 4, n + 4);
    put_be32(p + 8 + n, (uint32_t)crc);
    return p + 12 + n;
}

uint8_t *png_encode(const uint32_t *px, int w, int h, size_t *len)
{
    static const uint8_t sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    size_t raw_len = (size_t)h * ((size_t)w * 3 + 1);
    uLongf zlen = compressBound((uLong)raw_len);
    uint8_t *raw, *z, *out, *p, ihdr[13];
    int x, y;

    if (w <= 0 || h <= 0 || w > PNG_MAX_SIDE || h > PNG_MAX_SIDE)
        return NULL;
    raw = malloc(raw_len);
    z = malloc(zlen);
    out = malloc(zlen + 64);
    if (!raw || !z || !out) {
        free(raw);
        free(z);
        free(out);
        return NULL;
    }
    for (y = 0, p = raw; y < h; y++) {
        *p++ = 0;                           /* фильтр None */
        for (x = 0; x < w; x++) {
            uint32_t c = px[(size_t)y * (size_t)w + (size_t)x];
            *p++ = (uint8_t)(c >> 16);
            *p++ = (uint8_t)(c >> 8);
            *p++ = (uint8_t)c;
        }
    }
    if (compress2(z, &zlen, raw, (uLong)raw_len, 9) != Z_OK) {
        free(raw);
        free(z);
        free(out);
        return NULL;
    }
    put_be32(ihdr, (uint32_t)w);
    put_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;        /* 8 бит */
    ihdr[9] = 2;        /* RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    memcpy(out, sig, 8);
    p = put_chunk(out + 8, "IHDR", ihdr, 13);
    p = put_chunk(p, "IDAT", z, (uint32_t)zlen);
    p = put_chunk(p, "IEND", NULL, 0);
    *len = (size_t)(p - out);
    free(raw);
    free(z);
    return out;
}
