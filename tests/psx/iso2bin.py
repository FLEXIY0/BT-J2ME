#!/usr/bin/env python3
"""ISO (2048 байт на сектор) → образ .bin MODE2/2352, как у настоящих дисков PS1:
синхрометка, адрес MSF, подзаголовок Form 1; EDC/ECC — нулями (эмуляторы их не проверяют)."""
import sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
bcd = lambda v: (v // 10) << 4 | v % 10
with open(dst, "wb") as out:
    for lba in range(len(data) // 2048):
        a = lba + 150
        out.write(b"\x00" + b"\xff" * 10 + b"\x00")
        out.write(bytes([bcd(a // 4500), bcd(a // 75 % 60), bcd(a % 75), 2]))
        out.write(b"\x00\x00\x08\x00\x00\x00\x08\x00")
        out.write(data[lba * 2048:(lba + 1) * 2048])
        out.write(b"\x00" * 280)
