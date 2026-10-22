# BT-J2ME — сборка без autotools/cmake: нужен только C-компилятор, make и zlib.
#
#   make                          SDL2-бэкенд, оптимизация по размеру
#   make BACKEND=null             без графики (проверка JAR, CI)
#   make CROSS=arm-linux-gnueabihf-   кросс-сборка (armhf, aarch64, i686, mipsel ...)
#   make CFLAGS="-O2 -march=pentium2" сборка под конкретный старый процессор
#   make STATIC=1                 статическая сборка (если есть статические libSDL2/libz)

CROSS    ?=
ifeq ($(origin CC),default)
CC       := $(if $(CROSS),$(CROSS)gcc,cc)
endif
PKG      ?= $(CROSS)pkg-config
BACKEND  ?= sdl2

CFLAGS   ?= -Os -pipe
override CFLAGS += -std=c99 -Wall -Wextra -Wno-unused-parameter
override LDLIBS += -lz -lm

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
       src/jvm_core.c src/jvm_interp.c src/jvm_lib.c src/rt.c src/midp.c src/audio.c src/compat.c
OBJ := $(SRC:.c=.o)

bt-j2me: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -c -o $@ $<

tests/png_dump: tests/png_dump.c src/png.c src/png.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/png_dump.c src/png.c -lz

tests/pad_test: tests/pad_test.c src/platform_sdl2.c src/gfx.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/pad_test.c src/platform_sdl2.c src/gfx.c $(LDLIBS)

TEST_BINS := tests/png_dump $(if $(filter sdl2,$(BACKEND)),tests/pad_test)

test: bt-j2me $(TEST_BINS)
	BT_BACKEND=$(BACKEND) sh tests/run.sh ./bt-j2me

strip: bt-j2me
	$(CROSS)strip -s bt-j2me

clean:
	rm -f bt-j2me bt-j2me.exe src/*.o tests/png_dump tests/pad_test
	rm -rf tests/out

.PHONY: test strip clean
