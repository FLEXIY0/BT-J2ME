#!/bin/sh
# Кросс-сборка bt-j2me.exe для 64-битной Windows из Linux: один файл без DLL
# (SDL2 и zlib вшиты статически). Нужны mingw-w64 и zlib для него:
#   sudo apt install gcc-mingw-w64-x86-64 libz-mingw-w64-dev
# SDL2 для MinGW скачивается один раз в ~/.cache/bt-j2me.
# Результат: dist/bt-j2me.exe (без консоли) и dist/bt-j2me-console.exe (с логами).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDL_VER=2.30.9
CROSS=x86_64-w64-mingw32-
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/bt-j2me
SD="$CACHE/SDL2-$SDL_VER/x86_64-w64-mingw32"

if [ ! -f "$SD/lib/libSDL2.a" ]; then
    mkdir -p "$CACHE"
    curl -sSLf -o "$CACHE/sdl2.tar.gz" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-devel-$SDL_VER-mingw.tar.gz"
    tar xzf "$CACHE/sdl2.tar.gz" -C "$CACHE"
fi

LIBS="-L$SD/lib -lmingw32 -lSDL2main -lSDL2 -lm -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 \
-loleaut32 -lversion -luuid -lwinhttp -ladvapi32 -lsetupapi -lshell32 -ldinput8 -lpthread"
CFLAGS="-Os -pipe -I$SD/include/SDL2 -Dmain=SDL_main"

cd "$ROOT"
mkdir -p dist
for kind in windows console; do
    make -s clean
    make -s -j4 CROSS=$CROSS PKG=false SDL_CFLAGS= "SDL_LIBS=$LIBS" CFLAGS="$CFLAGS" \
         LDFLAGS="-static -m$kind" bt-j2me
    ${CROSS}strip -s bt-j2me.exe
    [ $kind = windows ] && mv bt-j2me.exe dist/bt-j2me.exe || mv bt-j2me.exe dist/bt-j2me-console.exe
done
make -s clean
ls -l dist/*.exe
