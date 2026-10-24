cmake_minimum_required(VERSION 3.6)
project(BTJ2ME C ASM)

# SDL2 из исходников (папка SDL — ссылка, её создаёт tools/build-android.sh)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(SDL)

set(BT ${CMAKE_CURRENT_SOURCE_DIR}/../../../src)

# Ядро PlayStation — PCSX-ReARMed (GPL-2). Набор файлов тот же, что в third_party/pcsx_rearmed.mk
# и в его собственном jni/Android.mk: на ARM — перекомпилятор ari64 и рендер NEON,
# на x86 — интерпретатор и тот же рендер на SSE.
set(PSX ${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/pcsx_rearmed)
set(LRC ${PSX}/deps/libretro-common)
set(PSX_SRC
    ${PSX}/libpcsxcore/cdriso.c ${PSX}/libpcsxcore/cdrom.c ${PSX}/libpcsxcore/cdrom-async.c
    ${PSX}/libpcsxcore/cheat.c ${PSX}/libpcsxcore/database.c ${PSX}/libpcsxcore/decode_xa.c
    ${PSX}/libpcsxcore/mdec.c ${PSX}/libpcsxcore/misc.c ${PSX}/libpcsxcore/pad.c ${PSX}/libpcsxcore/plugins.c
    ${PSX}/libpcsxcore/ppf.c ${PSX}/libpcsxcore/psxbios.c ${PSX}/libpcsxcore/psxcommon.c
    ${PSX}/libpcsxcore/psxcounters.c ${PSX}/libpcsxcore/psxdma.c ${PSX}/libpcsxcore/psxevents.c
    ${PSX}/libpcsxcore/psxhw.c ${PSX}/libpcsxcore/psxinterpreter.c ${PSX}/libpcsxcore/psxmem.c
    ${PSX}/libpcsxcore/r3000a.c ${PSX}/libpcsxcore/sio.c ${PSX}/libpcsxcore/spu.c ${PSX}/libpcsxcore/gpu.c
    ${PSX}/libpcsxcore/gte.c ${PSX}/libpcsxcore/gte_nf.c ${PSX}/libpcsxcore/gte_divider.c
    ${PSX}/libpcsxcore/new_dynarec/emu_if.c
    ${PSX}/plugins/dfsound/dma.c ${PSX}/plugins/dfsound/freeze.c ${PSX}/plugins/dfsound/registers.c
    ${PSX}/plugins/dfsound/spu.c ${PSX}/plugins/dfsound/out.c ${PSX}/plugins/dfsound/nullsnd.c
    ${PSX}/plugins/gpulib/gpu.c ${PSX}/plugins/gpulib/prim.c ${PSX}/plugins/gpulib/vout_pl.c
    ${PSX}/plugins/gpulib/gpu_async.c ${PSX}/plugins/cdrcimg/cdrcimg.c
    ${PSX}/frontend/main.c ${PSX}/frontend/plugin.c ${PSX}/frontend/cspace.c ${PSX}/frontend/libretro.c
    ${PSX}/frontend/pcsxr-threads.c
    ${LRC}/features/features_cpu.c ${LRC}/compat/compat_posix_string.c ${LRC}/compat/fopen_utf8.c
    ${LRC}/compat/compat_strl.c ${LRC}/encodings/encoding_utf.c ${LRC}/file/file_path.c
    ${LRC}/file/file_path_io.c ${LRC}/file/retro_dirent.c ${LRC}/streams/file_stream.c
    ${LRC}/streams/file_stream_transforms.c ${LRC}/string/stdstring.c ${LRC}/string/rstrtod.c
    ${LRC}/time/rtime.c ${LRC}/vfs/vfs_implementation.c
    ${PSX}/plugins/gpu_neon/psx_gpu_if.c)
set(PSX_DEFS HAVE_LIBRETRO NO_FRONTEND REARMED ANDROID USE_LIBRETRO_VFS DISABLE_MEM_LUTS=0
    USE_ASYNC_CDROM USE_ASYNC_GPU USE_ASYNC_SPU HAVE_RTHREADS BUILTIN_GPU=builtin NDEBUG
    P_HAVE_MMAP=1 P_HAVE_PTHREAD=1 P_HAVE_POSIX_MEMALIGN=1
    GPU_NEON NEON_BUILD TEXTURE_CACHE_4BPP TEXTURE_CACHE_8BPP)
set(PSX_OPTS -O2 -ffast-math -w)
if(ANDROID_ABI STREQUAL "armeabi-v7a")
    list(APPEND PSX_SRC ${PSX}/libpcsxcore/new_dynarec/new_dynarec.c ${PSX}/libpcsxcore/new_dynarec/pcsxmem.c
        ${PSX}/libpcsxcore/new_dynarec/linkage_arm.S ${PSX}/libpcsxcore/gte_arm.S ${PSX}/libpcsxcore/gte_nf_arm.S
        ${PSX}/plugins/dfsound/arm_utils.S ${PSX}/libpcsxcore/gte_neon.S ${PSX}/libpcsxcore/gte_nf_neon.S
        ${PSX}/frontend/cspace_neon.S ${PSX}/plugins/gpu_neon/psx_gpu/psx_gpu_arm_neon.S)
    list(APPEND PSX_DEFS NDRC_THREAD HAVE_bgr555_to_rgb565 HAVE_bgr888_to_x)
    list(APPEND PSX_OPTS -marm -mfpu=neon)
elseif(ANDROID_ABI STREQUAL "arm64-v8a")
    list(APPEND PSX_SRC ${PSX}/libpcsxcore/new_dynarec/new_dynarec.c ${PSX}/libpcsxcore/new_dynarec/pcsxmem.c
        ${PSX}/libpcsxcore/new_dynarec/linkage_arm64.S ${PSX}/libpcsxcore/gte_arm64.S
        ${PSX}/libpcsxcore/gte_nf_arm64.S ${PSX}/plugins/gpu_neon/psx_gpu/psx_gpu_simd.c)
    list(APPEND PSX_DEFS NDRC_THREAD SIMD_BUILD)
else()
    list(APPEND PSX_SRC ${PSX}/plugins/gpu_neon/psx_gpu/psx_gpu_simd.c)
    list(APPEND PSX_DEFS DRC_DISABLE SIMD_BUILD)
    list(APPEND PSX_OPTS -mssse3)
endif()
add_library(psx STATIC ${PSX_SRC})
target_compile_definitions(psx PRIVATE ${PSX_DEFS})
target_compile_options(psx PRIVATE ${PSX_OPTS})
target_include_directories(psx PRIVATE ${PSX}/include ${LRC}/include)
add_library(main SHARED
    ${BT}/main.c ${BT}/jar.c ${BT}/manifest.c ${BT}/classfile.c ${BT}/png.c ${BT}/gfx.c
    ${BT}/library.c ${BT}/launcher.c ${BT}/platform_sdl2.c ${BT}/touch.c ${BT}/multitap.c ${BT}/update.c
    ${BT}/jvm_core.c ${BT}/jvm_interp.c ${BT}/jvm_lib.c ${BT}/rt.c ${BT}/midp.c ${BT}/audio.c ${BT}/compat.c
    ${BT}/nes.c ${BT}/nes_map.c ${BT}/nes_run.c ${BT}/psx_run.c ${BT}/lr_run.c ${BT}/disc.c ${BT}/ps2_run.c)
# SDLActivity ищет в libmain.so функцию SDL_main
target_compile_definitions(main PRIVATE main=SDL_main)
if(BT_VERSION)
    target_compile_definitions(main PRIVATE BT_VERSION="${BT_VERSION}")
endif()
target_compile_options(main PRIVATE -std=c99 -O2 -Wall -Wno-unused-parameter)
target_link_libraries(main SDL2::SDL2 psx z m log android)
target_include_directories(main PRIVATE ${PSX}/deps/libretro-common/include)
