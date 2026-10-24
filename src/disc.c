/* Какой это диск: PS1 или PS2 — по SYSTEM.CNF из файловой системы ISO 9660 на образе. */
#include "disc.h"
#include "compat.h"
#include "library.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static int has_ext(const char *path, const char *ext)
{
    size_t n = strlen(path), k = strlen(ext);
    return n > k && strcasecmp(path + n - k, ext) == 0;
}

int disc_is_image(const char *path)
{
    static const char *const exts[] = { ".cue", ".bin", ".iso", ".img", ".pbp", ".chd", ".cso", ".zso" };
    size_t i;

    for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
        if (has_ext(path, exts[i]))
            return 1;
    return 0;
}

typedef struct {
    FILE *f;
    long  sector, offset;   /* байт на сектор и где в нём данные: 2048/0 или 2352/16 (24) */
} Img;

static int rd(const Img *im, unsigned lba, unsigned char *buf)
{
    long pos = (long)lba * im->sector + im->offset;

    if (lba > 0x100000 || fseek(im->f, pos, SEEK_SET) != 0)  /* дальше 2 ГБ не смотрим */
        return 0;
    return fread(buf, 1, 2048, im->f) == 2048;
}

static unsigned le32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

/* SYSTEM.CNF из корня диска: DISC_PS2 / DISC_PS1, DISC_NONE — не нашли. */
static int from_cnf(const char *path)
{
    unsigned char b[2048];
    unsigned root, size, i;
    Img im;
    int kind = DISC_NONE;

    if (!(im.f = bt_fopen(path, "rb")))
        return DISC_NONE;
    im.sector = 2048;
    im.offset = 0;
    if (fread(b, 1, 16, im.f) == 16 && b[0] == 0 && b[1] == 0xFF && b[10] == 0xFF && b[11] == 0) {
        im.sector = 2352;           /* «сырой» образ: синхрометка, заголовок, у Mode 2 — подзаголовок */
        im.offset = b[15] == 2 ? 24 : 16;
    }
    if (!rd(&im, 16, b) || memcmp(b + 1, "CD001", 5) != 0)
        goto out;
    root = le32(b + 156 + 2);
    size = le32(b + 156 + 10);
    for (i = 0; i < (size + 2047) / 2048 && i < 16 && kind == DISC_NONE; i++) {
        unsigned off = 0;
        if (!rd(&im, root + i, b))
            break;
        while (off + 33 < 2048 && b[off] && kind == DISC_NONE) {
            unsigned len = b[off], nl = b[off + 32];
            if (nl >= 10 && off + 33 + nl <= 2048 && strncasecmp((const char *)b + off + 33, "SYSTEM.CNF", 10) == 0) {
                unsigned char c[2049];
                if (rd(&im, le32(b + off + 2), c)) {
                    c[2048] = '\0';
                    kind = strstr((const char *)c, "BOOT2") ? DISC_PS2 :
                           strstr((const char *)c, "BOOT") ? DISC_PS1 : DISC_NONE;
                }
                break;
            }
            off += len;
        }
    }
out:
    fclose(im.f);
    return kind;
}

int disc_system(const char *path)
{
    char data[1400];
    int kind;

    if (!disc_is_image(path))
        return DISC_NONE;
    if (has_ext(path, ".pbp"))
        return DISC_PS1;
    if (has_ext(path, ".chd") || has_ext(path, ".cso") || has_ext(path, ".zso"))
        return DISC_PS2;                    /* сжатые образы PS1-ядро не читает — это PS2 */
    snprintf(data, sizeof(data), "%s", path);
    if (has_ext(path, ".cue")) {            /* смотрим первую дорожку — на ней файловая система */
        char files[1][256];
        const char *slash = strrchr(path, '/');
#ifdef _WIN32
        if (strrchr(path, '\\') > slash)
            slash = strrchr(path, '\\');
#endif
        if (library_cue_files(path, files, 1) != 1)
            return DISC_PS1;
        snprintf(data, sizeof(data), "%.*s%.255s", slash ? (int)(slash - path + 1) : 0, path, files[0]);
    }
    if ((kind = from_cnf(data)) != DISC_NONE)
        return kind;
    return bt_file_size(data) > (1LL << 30) ? DISC_PS2 : DISC_PS1;
}
