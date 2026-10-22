#!/usr/bin/env python3
"""Превращает консольные шрифты PSF1/PSF2 в C-заголовок с нужным набором символов.

Использование:
    tools/psf2c.py ИМЯ файл.psf[.gz] > src/font_ИМЯ.h

Берём только то, что нужно интерфейсу: ASCII, Latin-1, кириллицу и пару стрелок,
чтобы шрифт занимал единицы килобайт.
"""
import gzip
import struct
import sys

WANTED = (
    list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) +
    [0x401, 0x451] + list(range(0x410, 0x450)) +
    [0x2014, 0x2022, 0x2026, 0x2116, 0x2190, 0x2191, 0x2192, 0x2193,
     0x25B2, 0x25BA, 0x25BC, 0x25C4, 0x25CF, 0x25CB]
)


def load_psf(path):
    data = gzip.open(path).read() if path.endswith(".gz") else open(path, "rb").read()
    table = {}
    if data[:2] == b"\x36\x04":
        mode, charsize = data[2], data[3]
        count = 512 if mode & 1 else 256
        width, height, off = 8, charsize, 4
        glyphs = [data[off + i * charsize: off + (i + 1) * charsize] for i in range(count)]
        pos = off + count * charsize
        if mode & 2:
            for i in range(count):
                while True:
                    (u,) = struct.unpack_from("<H", data, pos)
                    pos += 2
                    if u == 0xFFFF:
                        break
                    if u != 0xFFFE:
                        table.setdefault(u, i)
    elif data[:4] == b"\x72\xb5\x4a\x86":
        _, hsize, flags, count, charsize, height, width = struct.unpack_from("<7I", data, 4)
        glyphs = [data[hsize + i * charsize: hsize + (i + 1) * charsize] for i in range(count)]
        pos = hsize + count * charsize
        if flags & 1:
            for i in range(count):
                end = data.index(b"\xff", pos)
                for seq in data[pos:end].split(b"\xfe")[:1]:
                    for ch in seq.decode("utf-8", "replace"):
                        table.setdefault(ord(ch), i)
                pos = end + 1
    else:
        sys.exit("не PSF-шрифт: " + path)
    if not table:
        table = {i: i for i in range(len(glyphs))}
    return width, height, glyphs, table


def main():
    name, path = sys.argv[1], sys.argv[2]
    width, height, glyphs, table = load_psf(path)
    bpr = (width + 7) // 8
    cps = sorted(cp for cp in set(WANTED) if cp in table)
    out = sys.stdout
    out.write("/* Сгенерировано tools/psf2c.py из %s.\n" % path.split("/")[-1])
    out.write(" * Шрифт производный от Terminus Font (SIL OFL 1.1), см. third_party/TERMINUS-OFL.txt */\n")
    out.write("static const uint16_t font_%s_cps[%d] = {" % (name, len(cps)))
    for i, cp in enumerate(cps):
        out.write(("\n    " if i % 12 == 0 else " ") + "0x%04X," % cp)
    out.write("\n};\n")
    out.write("static const uint8_t font_%s_bits[%d] = {\n" % (name, len(cps) * height * bpr))
    for cp in cps:
        g = glyphs[table[cp]][: height * bpr]
        out.write("    " + " ".join("0x%02X," % b for b in g) + "\n")
    out.write("};\n")
    out.write("#define FONT_%s_W %d\n#define FONT_%s_H %d\n#define FONT_%s_COUNT %d\n"
              % (name.upper(), width, name.upper(), height, name.upper(), len(cps)))


if __name__ == "__main__":
    main()
