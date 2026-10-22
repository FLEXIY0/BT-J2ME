#!/bin/sh
# Смоук-тесты: собираем маленький JAR (сжатый и несжатый) и проверяем `bt-j2me info`.
set -eu
BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
DIR=$(cd "$(dirname "$0")" && pwd)
OUT="$DIR/out"
fails=0

check() { # имя, ожидаемый код, фраза в выводе, команда...
    name=$1 want=$2 pat=$3
    shift 3
    set +e
    "$@" >"$OUT/log" 2>&1
    got=$?
    set -e
    if [ "$got" -eq "$want" ] && { [ -z "$pat" ] || grep -q -- "$pat" "$OUT/log"; }; then
        echo "ok   $name"
    else
        echo "FAIL $name (код $got, ждали $want)"; cat "$OUT/log"; fails=$((fails + 1))
    fi
}

rm -rf "$OUT" && mkdir -p "$OUT/classes"
printf 'not a zip' >"$OUT/broken.jar"
check "битый файл" 1 "не ZIP" "$BIN" info "$OUT/broken.jar"
check "справка" 0 "bt-j2me info" "$BIN" --help
check "неизвестная опция" 2 "неизвестная опция" "$BIN" --nope

if ! command -v javac >/dev/null 2>&1 || ! command -v jar >/dev/null 2>&1; then
    echo "skip JAR-тесты: нет javac/jar"
else
    javac -nowarn --release 8 -d "$OUT/classes" "$DIR/fixtures/hello/com/example/Hello.java" 2>/dev/null
    (cd "$OUT/classes" && jar cfm "$OUT/hello.jar" "$DIR/fixtures/hello/MANIFEST.MF" .)
    (cd "$OUT/classes" && jar cfm0 "$OUT/hello-stored.jar" "$DIR/fixtures/hello/MANIFEST.MF" .)
    check "deflate JAR" 0 "Hello -> com.example.Hello" "$BIN" info "$OUT/hello.jar"
    check "stored JAR" 0 "Классы:       1, с ошибками: 0" "$BIN" info "$OUT/hello-stored.jar"
    check "run пока без VM" 3 "этап 2" "$BIN" run "$OUT/hello.jar"

    # Меню-полка на виртуальном экране SDL: несколько секунд без падений (timeout => код 124)
    if [ "${BT_BACKEND:-sdl2}" = sdl2 ] && command -v timeout >/dev/null 2>&1; then
        mkdir -p "$OUT/shelf" && cp "$OUT/hello.jar" "$OUT/shelf/" && cp "$OUT/hello.jar" "$OUT/shelf/Другая игра.jar"
        check "полка с играми" 124 "" env SDL_VIDEODRIVER=dummy timeout 3 "$BIN" "$OUT/shelf" --windowed
        mkdir -p "$OUT/empty"
        check "пустая полка" 124 "" env SDL_VIDEODRIVER=dummy timeout 2 "$BIN" "$OUT/empty" --windowed
    fi
fi

# Таблицы шрифтов должны быть отсортированы — по ним идёт двоичный поиск
if command -v python3 >/dev/null 2>&1; then
    check "шрифты отсортированы" 0 "" python3 -c "
import re, sys
for f in sys.argv[1:]:
    cps = [int(x, 16) for x in re.findall(r'0x([0-9A-F]{4}),', open(f).read().split('_bits')[0])]
    assert cps == sorted(set(cps)) and 0x451 in cps, f
" "$DIR/../src/font_small.h" "$DIR/../src/font_big.h"
fi

if [ -x "$DIR/pad_test" ]; then
    SDL_VIDEODRIVER=dummy "$DIR/pad_test" || fails=$((fails + 1))
fi

PNG_DUMP="$DIR/png_dump"
if [ -x "$PNG_DUMP" ] && command -v python3 >/dev/null 2>&1 && python3 -c "import PIL" 2>/dev/null; then
    python3 "$DIR/png_check.py" "$PNG_DUMP" "$OUT/png" || fails=$((fails + 1))
else
    echo "skip PNG-тесты: нужен python3 + Pillow"
fi

[ "$fails" -eq 0 ] && echo "все тесты прошли" || { echo "провалено: $fails"; exit 1; }
