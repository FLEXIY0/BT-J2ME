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
    if [ "$got" -eq "$want" ] && grep -q -- "$pat" "$OUT/log"; then
        echo "ok   $name"
    else
        echo "FAIL $name (код $got, ждали $want)"; cat "$OUT/log"; fails=$((fails + 1))
    fi
}

rm -rf "$OUT" && mkdir -p "$OUT/classes"
printf 'not a zip' >"$OUT/broken.jar"
check "битый файл" 1 "не ZIP" "$BIN" info "$OUT/broken.jar"
check "без аргументов" 2 "bt-j2me info" "$BIN"

if ! command -v javac >/dev/null 2>&1 || ! command -v jar >/dev/null 2>&1; then
    echo "skip JAR-тесты: нет javac/jar"
else
    javac -nowarn --release 8 -d "$OUT/classes" "$DIR/fixtures/hello/com/example/Hello.java" 2>/dev/null
    (cd "$OUT/classes" && jar cfm "$OUT/hello.jar" "$DIR/fixtures/hello/MANIFEST.MF" .)
    (cd "$OUT/classes" && jar cfm0 "$OUT/hello-stored.jar" "$DIR/fixtures/hello/MANIFEST.MF" .)
    check "deflate JAR" 0 "Hello -> com.example.Hello" "$BIN" info "$OUT/hello.jar"
    check "stored JAR" 0 "Классы:       1, с ошибками: 0" "$BIN" info "$OUT/hello-stored.jar"
    check "run пока без VM" 3 "этап 2" "$BIN" run "$OUT/hello.jar"
fi

[ "$fails" -eq 0 ] && echo "все тесты прошли" || { echo "провалено: $fails"; exit 1; }
