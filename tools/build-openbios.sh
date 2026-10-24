#!/bin/sh
# Сборка встроенного BIOS PlayStation — OpenBIOS из проекта PCSX-Redux (MIT), зеркало nugget.
# Результат: src/openbios_bin.h — сжатый zlib образ как C-массив,
# и third_party/openbios/LICENSE.
# Нужны mipsel-linux-gnu-gcc и python3:  sudo apt install gcc-mipsel-linux-gnu
#
# BOOT_MODE=fast — без оболочки с логотипом и меню: BIOS сразу грузит диск. Оболочка
# всё равно линкуется, поэтому берём старую на C (shell): новой (shello) нужен C++.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
COMMIT=e2264e0dd55f81c09118d24c02fa363532c9d949
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/bt-j2me
SRC="$CACHE/nugget"

if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$CACHE"
    git clone -q https://github.com/pcsx-redux/nugget.git "$SRC"
fi
git -C "$SRC" fetch -q origin "$COMMIT" 2>/dev/null || true
git -C "$SRC" checkout -q "$COMMIT"
make -s -C "$SRC/openbios" deepclean PREFIX=mipsel-linux-gnu >/dev/null 2>&1 || true
make -s -C "$SRC/openbios" -j4 PREFIX=mipsel-linux-gnu FORMAT=elf32-tradlittlemips \
     BOOT_MODE=fast OPENBIOS_SHELL=shell
python3 - "$SRC/openbios/openbios.bin" "$CACHE/openbios.bin.z" <<'PY'
import sys, zlib
data = open(sys.argv[1], "rb").read()
assert len(data) == 512 * 1024, len(data)
open(sys.argv[2], "wb").write(zlib.compress(data, 9))
PY
python3 "$ROOT/tools/bin2c.py" openbios_z "$CACHE/openbios.bin.z" > "$ROOT/src/openbios_bin.h"
cp "$SRC/LICENSE" "$ROOT/third_party/openbios/LICENSE"
ls -l "$ROOT/src/openbios_bin.h"
