#!/bin/sh
# Сборка APK для Android: dist/bt-j2me.apk (armeabi-v7a, arm64-v8a, x86, x86_64; Android 4.1+).
#
# Нужны JDK 17+, Android SDK (platforms;android-34, build-tools;34.0.0, cmake;3.22.1)
# и NDK 23.2.8568313 (r23c — последний, что собирает под Android 4.1). Путь к SDK — ANDROID_HOME (или ANDROID_SDK_ROOT).
# Исходники SDL2 скачиваются один раз в ~/.cache/bt-j2me. Gradle — из PATH или GRADLE.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDL_VER=2.30.9
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/bt-j2me
SDL_SRC="$CACHE/SDL2-src-$SDL_VER"
: "${ANDROID_HOME:=${ANDROID_SDK_ROOT:-/opt/android-sdk}}"
export ANDROID_HOME

if [ ! -f "$SDL_SRC/CMakeLists.txt" ]; then
    mkdir -p "$CACHE"
    curl -sSLf -o "$CACHE/sdl-src.tar.gz" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz"
    tar xzf "$CACHE/sdl-src.tar.gz" -C "$CACHE"
    rm -rf "$SDL_SRC"
    mv "$CACHE/SDL2-$SDL_VER" "$SDL_SRC"
fi
ln -sfn "$SDL_SRC" "$ROOT/android/app/jni/SDL"
echo "sdk.dir=$ANDROID_HOME" > "$ROOT/android/local.properties"

cd "$ROOT/android"
${GRADLE:-gradle} --no-daemon -q assembleRelease
mkdir -p "$ROOT/dist"
cp app/build/outputs/apk/release/app-release.apk "$ROOT/dist/bt-j2me.apk"
ls -l "$ROOT/dist/bt-j2me.apk"
