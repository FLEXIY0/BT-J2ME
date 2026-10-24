#!/bin/sh
# Сборка проверочного диска PS2: ps2test.iso.gz (ISO 9660 с SYSTEM.CNF «BOOT2» и ELF).
# Нужны clang (с MIPS), ld.lld и genisoimage.
set -eu
cd "$(dirname "$0")"
OUT=${OUT:-../out/ps2}
mkdir -p "$OUT/cd"
clang --target=mipsel-none-elf -march=mips3 -mabi=32 -mno-abicalls -fno-pic -G0 -Os -ffreestanding -fno-builtin -msoft-float -c ps2test.c -o "$OUT/ps2test.o"
ld.lld -T ps2test.ld --no-dynamic-linker -o "$OUT/cd/SLUS_000.01" "$OUT/ps2test.o"
printf 'BOOT2 = cdrom0:\\SLUS_000.01;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' > "$OUT/cd/SYSTEM.CNF"
genisoimage -quiet -V PS2TEST -sysid PLAYSTATION -o ps2test.iso "$OUT/cd"
gzip -9nc ps2test.iso > ps2test.iso.gz
rm -f ps2test.iso
ls -l ps2test.iso.gz
