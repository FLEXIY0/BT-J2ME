#!/bin/sh
# Сборка встроенного BIOS PlayStation 2 — OpenPS2BIOS (Tamariba Devs, MIT).
# Результат: src/openps2bios_bin.h — образ 4 МБ, сжатый zlib, как C-массив,
# и third_party/openps2bios/LICENSE. Нужны clang с MIPS, ld.lld, llvm-objcopy и python3.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
COMMIT=bb1457af4f876e3bdc7509fadd6db389e281355c     # версия 0.3
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/bt-j2me
SRC="$CACHE/OpenPS2BIOS"

if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$CACHE"
    git clone -q https://github.com/TamaribaEmu/OpenPS2BIOS.git "$SRC"
fi
git -C "$SRC" fetch -q origin "$COMMIT" 2>/dev/null || true
git -C "$SRC" checkout -q "$COMMIT"
make -s -C "$SRC" clean >/dev/null 2>&1 || true
# -Wno-unknown-attributes: clang старше 20 не знает атрибут nonstring, а проект собирается с -Werror
make -s -C "$SRC" -j4 CFLAGS="--target=mipsel-none-elf -mabi=32 -mno-abicalls -fno-pic -G0 \
    -ffreestanding -fno-builtin -fno-stack-protector -fno-common -msoft-float -Os -Wall -Wextra -Werror \
    -Wno-unknown-attributes -Iinclude -DOB_VERSION='\"0.3\"'" 2>&1 | grep -v "MIPS-I support is experimental" || true
python3 - "$SRC/build/OpenPS2BIOS.bin" "$CACHE/openps2bios.bin.z" <<'PY'
import sys, zlib
data = open(sys.argv[1], "rb").read()
assert len(data) == 4 << 20, len(data)
open(sys.argv[2], "wb").write(zlib.compress(data, 9))
PY
python3 "$ROOT/tools/bin2c.py" openps2bios_z "$CACHE/openps2bios.bin.z" > "$ROOT/src/openps2bios_bin.h"
cp "$SRC/LICENSE" "$ROOT/third_party/openps2bios/LICENSE"
ls -l "$ROOT/src/openps2bios_bin.h"
