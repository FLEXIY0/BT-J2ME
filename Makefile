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
override LDLIBS += -lz

ifeq ($(BACKEND),sdl2)
SDL_CFLAGS := $(shell $(PKG) --cflags sdl2 2>/dev/null || sdl2-config --cflags)
SDL_LIBS   := $(shell $(PKG) --libs sdl2 2>/dev/null || sdl2-config --libs)
override CFLAGS += $(SDL_CFLAGS)
override LDLIBS += $(SDL_LIBS)
endif

ifeq ($(STATIC),1)
override LDFLAGS += -static
endif

SRC := src/main.c src/jar.c src/manifest.c src/classfile.c src/platform_$(BACKEND).c
OBJ := $(SRC:.c=.o)

bt-j2me: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -c -o $@ $<

test: bt-j2me
	sh tests/run.sh ./bt-j2me

strip: bt-j2me
	$(CROSS)strip -s bt-j2me

clean:
	rm -f bt-j2me src/*.o
	rm -rf tests/out

.PHONY: test strip clean
