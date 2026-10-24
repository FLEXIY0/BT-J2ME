# OpenPS2BIOS

Экспериментальная свободная замена BIOS PlayStation 2 для эмуляторов —
[TamaribaEmu/OpenPS2BIOS](https://github.com/TamaribaEmu/OpenPS2BIOS) (MIT, см. `LICENSE`),
кода Sony в ней нет. В bt-j2me вшита как `src/openps2bios_bin.h` — образ 4 МБ, сжатый zlib.

Пересборка: `tools/build-openps2bios.sh` (clang с MIPS, ld.lld, llvm-objcopy, python3).
Версия 0.3, коммит `bb1457af4f876e3bdc7509fadd6db389e281355c`.
