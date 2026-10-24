# OpenBIOS

Свободная замена BIOS PlayStation из проекта [PCSX-Redux](https://github.com/grumpycoders/pcsx-redux)
(MIT, см. `LICENSE`). В bt-j2me вшит как `src/openbios_bin.h` — образ 512 КБ, сжатый zlib.

Пересборка: `tools/build-openbios.sh` (нужны `mipsel-linux-gnu-gcc` и python3). Исходники —
зеркало [pcsx-redux/nugget](https://github.com/pcsx-redux/nugget), коммит
`e2264e0dd55f81c09118d24c02fa363532c9d949`, параметры `BOOT_MODE=fast OPENBIOS_SHELL=shell`:
без оболочки с логотипом и меню, BIOS сразу загружает диск.
