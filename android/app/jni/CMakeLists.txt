cmake_minimum_required(VERSION 3.6)
project(BTJ2ME C)

# SDL2 из исходников (папка SDL — ссылка, её создаёт tools/build-android.sh)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(SDL)

set(BT ${CMAKE_CURRENT_SOURCE_DIR}/../../../src)
add_library(main SHARED
    ${BT}/main.c ${BT}/jar.c ${BT}/manifest.c ${BT}/classfile.c ${BT}/png.c ${BT}/gfx.c
    ${BT}/library.c ${BT}/launcher.c ${BT}/platform_sdl2.c ${BT}/touch.c
    ${BT}/jvm_core.c ${BT}/jvm_interp.c ${BT}/jvm_lib.c ${BT}/rt.c ${BT}/midp.c ${BT}/audio.c ${BT}/compat.c)
# SDLActivity ищет в libmain.so функцию SDL_main
target_compile_definitions(main PRIVATE main=SDL_main)
target_compile_options(main PRIVATE -std=c99 -O2 -Wall -Wno-unused-parameter)
target_link_libraries(main SDL2::SDL2 z m log android)
