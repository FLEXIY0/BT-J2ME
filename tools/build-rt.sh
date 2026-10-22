#!/bin/sh
# Собирает библиотеку классов (runtime/*.java) во встроенный src/rt_jar.h.
# Нужен JDK (javac + jar). Пользователю для сборки bt-j2me этого не нужно:
# готовый src/rt_jar.h лежит в репозитории.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
mkdir -p "$OUT/classes" "$OUT/empty"
find "$ROOT/runtime" -name '*.java' > "$OUT/sources"
# Своя java.lang вместо JDK: собираем «в пустоту» под формат class-файлов Java 8.
javac -nowarn -Xlint:-options -encoding UTF-8 -source 8 -target 8 -g:lines,source \
      -bootclasspath "$OUT/empty" -d "$OUT/classes" @"$OUT/sources"
(cd "$OUT/classes" && jar cfM "$OUT/rt.jar" .)
python3 "$ROOT/tools/bin2c.py" rt_jar "$OUT/rt.jar" > "$ROOT/src/rt_jar.h"
echo "rt.jar: $(wc -c < "$OUT/rt.jar") байт, классов: $(find "$OUT/classes" -name '*.class' | wc -l)"
