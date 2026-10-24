#!/bin/sh
# Сборка проверочного диска PS1: psxtest.iso (ISO 9660, 2048 байт на сектор) и его сжатая копия
# psxtest.iso.gz, которую берут тесты. Нужны mipsel-linux-gnu-gcc и genisoimage:
#   sudo apt install gcc-mipsel-linux-gnu genisoimage
set -eu
cd "$(dirname "$0")"
OUT=${OUT:-../out/psx}
mkdir -p "$OUT/cd"
CC=mipsel-linux-gnu-gcc
$CC -O2 -G0 -march=r3000 -mabi=32 -mfp32 -msoft-float -mno-abicalls -fno-pic -ffreestanding -nostdlib -fno-builtin \
    -Wl,-T,psxtest.ld -Wl,--build-id=none -o "$OUT/psxtest.elf" crt0.S psxtest.c
mipsel-linux-gnu-objcopy -O binary "$OUT/psxtest.elf" "$OUT/psxtest.bin"
python3 - "$OUT/psxtest.bin" "$OUT/cd/PSXTEST.EXE" <<'PY'
import struct, sys
body = open(sys.argv[1], "rb").read()
body += b"\0" * (-len(body) % 2048)
hdr = bytearray(2048)
hdr[0:8] = b"PS-X EXE"
struct.pack_into("<IIII", hdr, 0x10, 0x80010000, 0, 0x80010000, len(body))
struct.pack_into("<II", hdr, 0x30, 0x801FFF00, 0)
hdr[0x4C:0x4C + 31] = b"Sony Computer Entertainment Inc"[:31]
open(sys.argv[2], "wb").write(bytes(hdr) + body)
PY
printf 'BOOT = cdrom:\\PSXTEST.EXE;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n' > "$OUT/cd/SYSTEM.CNF"
genisoimage -quiet -V PSXTEST -sysid PLAYSTATION -o psxtest.iso "$OUT/cd"
gzip -9nc psxtest.iso > psxtest.iso.gz
rm -f psxtest.iso
ls -l psxtest.iso.gz
