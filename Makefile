# BT-J2ME — сборка без autotools/cmake: нужен только C-компилятор, make и zlib.
#
#   make                          SDL2-бэкенд, оптимизация по размеру
#   make BACKEND=null             без графики (проверка JAR, CI)
#   make CROSS=arm-linux-gnueabihf-   кросс-сборка (armhf, aarch64, i686, mipsel ...)
#   make CFLAGS="-O2 -march=pentium2" сборка под конкретный старый процессор
#   make STATIC=1                 статическая сборка (если есть статические libSDL2/libz)
#   make VERSION=0.1.42           номер версии (его ставит CI)

CROSS    ?=
ifeq ($(origin CC),default)
CC       := $(if $(CROSS),$(CROSS)gcc,cc)
endif
PKG      ?= $(CROSS)pkg-config
BACKEND  ?= sdl2

CFLAGS   ?= -Os -pipe
override CFLAGS += -std=c99 -Wall -Wextra -Wno-unused-parameter
override LDLIBS += -lz -lm -lpthread
ifdef VERSION
override CFLAGS += -DBT_VERSION='"$(VERSION)"'
endif

ifeq ($(BACKEND),sdl2)
SDL_CFLAGS := $(shell $(PKG) --cflags sdl2 2>/dev/null || sdl2-config --cflags)
SDL_LIBS   := $(shell $(PKG) --libs sdl2 2>/dev/null || sdl2-config --libs)
override CFLAGS += $(SDL_CFLAGS)
override LDLIBS += $(SDL_LIBS)
endif

ifeq ($(STATIC),1)
override LDFLAGS += -static
endif

SRC := src/main.c src/jar.c src/manifest.c src/classfile.c src/png.c src/gfx.c \
       src/library.c src/launcher.c src/platform_$(BACKEND).c \
       src/jvm_core.c src/jvm_interp.c src/jvm_lib.c src/rt.c src/midp.c src/audio.c src/compat.c \
       src/nes.c src/nes_map.c src/nes_run.c src/psx_run.c src/lr_run.c src/disc.c \
       src/boxart.c src/media.c src/ttf.c src/shelf.c src/stats.c \
       $(if $(filter sdl2,$(BACKEND)),src/touch.c src/multitap.c src/update.c src/ps2_run.c src/boxart_net.c)
OBJ := $(SRC:.c=.o)

# ядро PlayStation (PCSX-ReARMed, GPL-2): свой набор флагов, см. third_party/pcsx_rearmed.mk
include third_party/pcsx_rearmed.mk
OBJ += $(PSX_OBJ)
src/psx_run.o src/lr_run.o src/ps2_run.o: override CFLAGS += -I$(PSX_DIR)/deps/libretro-common/include

bt-j2me: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -c -o $@ $<

tests/png_dump: tests/png_dump.c src/png.c src/png.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/png_dump.c src/png.c -lz

tests/pad_test: tests/pad_test.c src/platform_sdl2.c src/gfx.c src/touch.c src/multitap.c src/compat.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/pad_test.c src/platform_sdl2.c src/gfx.c src/touch.c src/multitap.c src/compat.c $(LDLIBS)

tests/psx_test: tests/psx_test.c src/psx_run.o src/lr_run.o src/gfx.o src/compat.o $(PSX_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/psx_test.c src/psx_run.o src/lr_run.o src/gfx.o src/compat.o $(PSX_OBJ) -lz -lm -lpthread

tests/ps2_test: tests/ps2_test.c src/ps2_run.o src/lr_run.o src/update.o src/jar.o src/gfx.o src/compat.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/ps2_test.c src/ps2_run.o src/lr_run.o src/update.o src/jar.o src/gfx.o \
	    src/compat.o $(LDLIBS)

tests/boxart_test: tests/boxart_test.c src/boxart.o src/media.o src/ttf.o src/png.o src/gfx.o src/compat.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/boxart_test.c src/boxart.o src/media.o src/ttf.o src/png.o src/gfx.o src/compat.o -lz -lm

TEST_BINS := tests/png_dump tests/psx_test tests/boxart_test $(if $(filter sdl2,$(BACKEND)),tests/pad_test tests/ps2_test)

test: bt-j2me $(TEST_BINS)
	BT_BACKEND=$(BACKEND) sh tests/run.sh ./bt-j2me

strip: bt-j2me
	$(CROSS)strip -s bt-j2me

clean:
	rm -f bt-j2me bt-j2me.exe src/*.o tests/png_dump tests/pad_test tests/psx_test tests/ps2_test tests/boxart_test
	find $(PSX_DIR) -name "*.o" -delete
	rm -rf tests/out

.PHONY: test strip clean
