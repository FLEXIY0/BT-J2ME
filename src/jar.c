#include "jar.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define EOCD_SIG  0x06054b50u
#define CDIR_SIG  0x02014b50u
#define LOCAL_SIG 0x04034b50u

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL;
    long len;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)len);
        if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
            free(buf);
            buf = NULL;
        }
        *size = (size_t)len;
    }
    fclose(f);
    return buf;
}

/* End Of Central Directory лежит в последних 22..65557 байтах файла. */
static const uint8_t *find_eocd(const uint8_t *data, size_t size)
{
    size_t min = size > 65557 ? size - 65557 : 0;
    size_t i;

    if (size < 22)
        return NULL;
    for (i = size - 22 + 1; i-- > min;)
        if (rd_le32(data + i) == EOCD_SIG)
            return data + i;
    return NULL;
}

int jar_open(Jar *jar, const char *path)
{
    const uint8_t *eocd, *p, *end;
    uint32_t cd_off, cd_size;
    uint16_t total;
    size_t i;

    memset(jar, 0, sizeof(*jar));
    jar->data = read_file(path, &jar->size);
    if (!jar->data) {
        BT_LOG("не удалось прочитать %s\n", path);
        return -1;
    }
    eocd = find_eocd(jar->data, jar->size);
    if (!eocd) {
        BT_LOG("%s: не ZIP/JAR архив\n", path);
        goto fail;
    }
    total   = rd_le16(eocd + 10);
    cd_size = rd_le32(eocd + 12);
    cd_off  = rd_le32(eocd + 16);
    if ((size_t)cd_off + cd_size > jar->size) {
        BT_LOG("%s: повреждён центральный каталог\n", path);
        goto fail;
    }

    jar->entries = calloc(total ? total : 1, sizeof(JarEntry));
    if (!jar->entries)
        goto fail;

    p = jar->data + cd_off;
    end = p + cd_size;
    for (i = 0; i < total; i++) {
        uint16_t nlen, xlen, clen;
        JarEntry *e = &jar->entries[i];

        if (p + 46 > end || rd_le32(p) != CDIR_SIG)
            break;
        nlen = rd_le16(p + 28);
        xlen = rd_le16(p + 30);
        clen = rd_le16(p + 32);
        if (p + 46 + nlen > end)
            break;
        e->method       = rd_le16(p + 10);
        e->csize        = rd_le32(p + 20);
        e->usize        = rd_le32(p + 24);
        e->local_offset = rd_le32(p + 42);
        e->name = malloc(nlen + 1u);
        if (!e->name)
            break;
        memcpy(e->name, p + 46, nlen);
        e->name[nlen] = '\0';
        p += 46 + nlen + xlen + clen;
        jar->count++;
    }
    if (jar->count != total)
        BT_LOG("%s: прочитано %zu из %u записей\n", path, jar->count, (unsigned)total);
    return 0;

fail:
    jar_close(jar);
    return -1;
}

void jar_close(Jar *jar)
{
    size_t i;

    for (i = 0; i < jar->count; i++)
        free(jar->entries[i].name);
    free(jar->entries);
    free(jar->data);
    memset(jar, 0, sizeof(*jar));
}

const JarEntry *jar_find(const Jar *jar, const char *name)
{
    size_t i;

    for (i = 0; i < jar->count; i++)
        if (strcmp(jar->entries[i].name, name) == 0)
            return &jar->entries[i];
    /* Некоторые старые сборщики пишут имена в другом регистре. */
    for (i = 0; i < jar->count; i++) {
        const char *a = jar->entries[i].name, *b = name;
        while (*a && *b && ((*a | 0x20) == (*b | 0x20)))
            a++, b++;
        if (!*a && !*b)
            return &jar->entries[i];
    }
    return NULL;
}

uint8_t *jar_read(const Jar *jar, const JarEntry *e, size_t *out_size)
{
    const uint8_t *lh = jar->data + e->local_offset;
    const uint8_t *src;
    uint8_t *out;

    if ((size_t)e->local_offset + 30 > jar->size || rd_le32(lh) != LOCAL_SIG)
        return NULL;
    src = lh + 30 + rd_le16(lh + 26) + rd_le16(lh + 28);
    if (src + e->csize > jar->data + jar->size)
        return NULL;

    out = malloc(e->usize ? e->usize : 1);
    if (!out)
        return NULL;

    if (e->method == 0) {
        memcpy(out, src, e->usize);
    } else if (e->method == 8) {
        z_stream zs;
        int rc;

        memset(&zs, 0, sizeof(zs));
        zs.next_in   = (Bytef *)src;
        zs.avail_in  = e->csize;
        zs.next_out  = out;
        zs.avail_out = e->usize;
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
            free(out);
            return NULL;
        }
        rc = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);
        if (rc != Z_STREAM_END || zs.total_out != e->usize) {
            free(out);
            return NULL;
        }
    } else {
        BT_LOG("%s: неподдерживаемый метод сжатия %u\n", e->name, e->method);
        free(out);
        return NULL;
    }
    if (out_size)
        *out_size = e->usize;
    return out;
}
