# Ядро PlayStation — PCSX-ReARMed (GPL-2), собирается вместе с bt-j2me.
#
# Набор файлов повторяет его сборку для libretro (jni/Android.mk), только без лишнего:
#   - ARMv7 и AArch64: динамический перекомпилятор ari64 и рендер gpu_neon (NEON);
#   - x86 и x86-64: интерпретатор и тот же рендер на SSE (на ПК скорости хватает с запасом);
#   - ARM без NEON и прочие процессоры: интерпретатор и программный рендер P.E.Op.S.
# Подключается из Makefile; тот же набор для Android — в android/app/jni/CMakeLists.txt.
#
# Исходники — libretro/pcsx_rearmed, коммит c8816799b50388e61cfe237fe2cdbb7d8175f20a
# (2 октября 2026), только нужные для сборки файлы и без правок; include/revision.h — наш
# (у них его пишет git describe при сборке).

PSX_DIR ?= third_party/pcsx_rearmed
PSX_MACHINE := $(shell $(CC) -dumpmachine 2>/dev/null)
PSX_DEFS := $(shell $(CC) $(CFLAGS) -E -dM -x c /dev/null 2>/dev/null)

ifneq ($(findstring aarch64,$(PSX_MACHINE)),)
PSX_ARCH := arm64
else ifneq ($(findstring arm,$(PSX_MACHINE)),)
PSX_ARCH := arm
else ifneq ($(filter x86_64 i%86,$(firstword $(subst -, ,$(PSX_MACHINE)))),)
PSX_ARCH := x86
else
PSX_ARCH := other
endif
PSX_WIN := $(if $(findstring mingw,$(PSX_MACHINE)),1)
PSX_NEON32 := $(if $(filter arm,$(PSX_ARCH)),$(findstring __ARM_NEON,$(PSX_DEFS)))

P := $(PSX_DIR)
PSX_SRC := \
	$(P)/libpcsxcore/cdriso.c $(P)/libpcsxcore/cdrom.c $(P)/libpcsxcore/cdrom-async.c \
	$(P)/libpcsxcore/cheat.c $(P)/libpcsxcore/database.c $(P)/libpcsxcore/decode_xa.c \
	$(P)/libpcsxcore/mdec.c $(P)/libpcsxcore/misc.c $(P)/libpcsxcore/pad.c $(P)/libpcsxcore/plugins.c \
	$(P)/libpcsxcore/ppf.c $(P)/libpcsxcore/psxbios.c $(P)/libpcsxcore/psxcommon.c \
	$(P)/libpcsxcore/psxcounters.c $(P)/libpcsxcore/psxdma.c $(P)/libpcsxcore/psxevents.c \
	$(P)/libpcsxcore/psxhw.c $(P)/libpcsxcore/psxinterpreter.c $(P)/libpcsxcore/psxmem.c \
	$(P)/libpcsxcore/r3000a.c $(P)/libpcsxcore/sio.c $(P)/libpcsxcore/spu.c $(P)/libpcsxcore/gpu.c \
	$(P)/libpcsxcore/gte.c $(P)/libpcsxcore/gte_nf.c $(P)/libpcsxcore/gte_divider.c \
	$(P)/libpcsxcore/new_dynarec/emu_if.c \
	$(P)/plugins/dfsound/dma.c $(P)/plugins/dfsound/freeze.c $(P)/plugins/dfsound/registers.c \
	$(P)/plugins/dfsound/spu.c $(P)/plugins/dfsound/out.c $(P)/plugins/dfsound/nullsnd.c \
	$(P)/plugins/gpulib/gpu.c $(P)/plugins/gpulib/prim.c $(P)/plugins/gpulib/vout_pl.c \
	$(P)/plugins/gpulib/gpu_async.c $(P)/plugins/cdrcimg/cdrcimg.c \
	$(P)/frontend/main.c $(P)/frontend/plugin.c $(P)/frontend/cspace.c $(P)/frontend/libretro.c \
	$(P)/frontend/pcsxr-threads.c \
	$(P)/deps/libretro-common/features/features_cpu.c \
	$(P)/deps/libretro-common/compat/compat_posix_string.c $(P)/deps/libretro-common/compat/fopen_utf8.c \
	$(P)/deps/libretro-common/compat/compat_strl.c $(P)/deps/libretro-common/encodings/encoding_utf.c \
	$(P)/deps/libretro-common/file/file_path.c $(P)/deps/libretro-common/file/file_path_io.c \
	$(P)/deps/libretro-common/file/retro_dirent.c $(P)/deps/libretro-common/streams/file_stream.c \
	$(P)/deps/libretro-common/streams/file_stream_transforms.c $(P)/deps/libretro-common/string/stdstring.c \
	$(P)/deps/libretro-common/string/rstrtod.c $(P)/deps/libretro-common/time/rtime.c \
	$(P)/deps/libretro-common/vfs/vfs_implementation.c

PSX_CFLAGS := -O2 -ffast-math -std=gnu11 -w -DNDEBUG \
	-DHAVE_LIBRETRO -DNO_FRONTEND -DREARMED -DUSE_LIBRETRO_VFS -DDISABLE_MEM_LUTS=0 \
	-DUSE_ASYNC_CDROM -DUSE_ASYNC_GPU -DHAVE_RTHREADS -DBUILTIN_GPU=builtin \
	-I$(P)/include -I$(P)/deps/libretro-common/include

# long в 32 бита (Windows, 32-битный Linux) — смещения в файлах 64-битные, как в сборке libretro;
# иначе mmap из deps/mman получает мусор в старшей половине смещения
ifneq ($(findstring __SIZEOF_LONG__ 4,$(PSX_DEFS)),)
PSX_CFLAGS += -D_FILE_OFFSET_BITS=64
endif

ifeq ($(PSX_WIN),1)
# в Windows нет mmap и posix-семафоров: mmap — из deps/mman, звук без своего потока
PSX_SRC += $(P)/deps/mman/mman.c
PSX_CFLAGS += -D__WIN32__ -DNO_DYLIB -DNO_SOCKET -DP_HAVE_MMAP=1 -DP_HAVE_PTHREAD=1 -DP_HAVE_POSIX_MEMALIGN=0 \
	-I$(P)/include/mman -I$(P)/deps/mman
else
PSX_CFLAGS += -DUSE_ASYNC_SPU -DP_HAVE_MMAP=1 -DP_HAVE_PTHREAD=1 -DP_HAVE_POSIX_MEMALIGN=1 -D_GNU_SOURCE
endif

ifeq ($(PSX_ARCH),arm64)
PSX_SRC += $(P)/libpcsxcore/new_dynarec/new_dynarec.c $(P)/libpcsxcore/new_dynarec/pcsxmem.c \
	$(P)/libpcsxcore/new_dynarec/linkage_arm64.S $(P)/libpcsxcore/gte_arm64.S $(P)/libpcsxcore/gte_nf_arm64.S \
	$(P)/plugins/gpu_neon/psx_gpu_if.c $(P)/plugins/gpu_neon/psx_gpu/psx_gpu_simd.c
PSX_CFLAGS += -DNDRC_THREAD -DGPU_NEON -DNEON_BUILD -DSIMD_BUILD -DTEXTURE_CACHE_4BPP -DTEXTURE_CACHE_8BPP
else ifeq ($(PSX_ARCH),arm)
PSX_SRC += $(P)/libpcsxcore/new_dynarec/new_dynarec.c $(P)/libpcsxcore/new_dynarec/pcsxmem.c \
	$(P)/libpcsxcore/new_dynarec/linkage_arm.S $(P)/libpcsxcore/gte_arm.S $(P)/libpcsxcore/gte_nf_arm.S \
	$(P)/plugins/dfsound/arm_utils.S
PSX_CFLAGS += -DNDRC_THREAD -marm     # ассемблер ядра — в режиме ARM, не Thumb
ifneq ($(PSX_NEON32),)
PSX_SRC += $(P)/libpcsxcore/gte_neon.S $(P)/libpcsxcore/gte_nf_neon.S $(P)/frontend/cspace_neon.S \
	$(P)/plugins/gpu_neon/psx_gpu_if.c $(P)/plugins/gpu_neon/psx_gpu/psx_gpu_arm_neon.S
PSX_CFLAGS += -DGPU_NEON -DNEON_BUILD -DTEXTURE_CACHE_4BPP -DTEXTURE_CACHE_8BPP \
	-DHAVE_bgr555_to_rgb565 -DHAVE_bgr888_to_x -mfpu=neon
else
PSX_SRC += $(P)/plugins/dfxvideo/gpulib_if.c
PSX_CFLAGS += -DGPU_PEOPS -fno-strict-aliasing
endif
else ifeq ($(PSX_ARCH),x86)
PSX_SRC += $(P)/plugins/gpu_neon/psx_gpu_if.c $(P)/plugins/gpu_neon/psx_gpu/psx_gpu_simd.c
PSX_CFLAGS += -DDRC_DISABLE -DGPU_NEON -DNEON_BUILD -DSIMD_BUILD -DTEXTURE_CACHE_4BPP -DTEXTURE_CACHE_8BPP \
	-msse2 $(if $(findstring x86_64,$(PSX_MACHINE)),-mssse3)
else
PSX_SRC += $(P)/plugins/dfxvideo/gpulib_if.c
PSX_CFLAGS += -DDRC_DISABLE -DGPU_PEOPS -fno-strict-aliasing
endif

PSX_OBJ := $(patsubst %.S,%.o,$(PSX_SRC:.c=.o))

$(P)/%.o: $(P)/%.c
	$(CC) $(PSX_CFLAGS) -c -o $@ $<

$(P)/%.o: $(P)/%.S
	$(CC) $(PSX_CFLAGS) -c -o $@ $<
