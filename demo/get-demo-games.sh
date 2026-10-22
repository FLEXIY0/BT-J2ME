#!/bin/sh
# Скачивает несколько свободных (GPLv2) J2ME-игр с SourceForge и кладёт их на полку.
#   sh demo/get-demo-games.sh [папка]   (по умолчанию ~/.local/share/bt-j2me/games)
# Игры: jtReversi (Jataka), Mobile Freecell и SudokuME (проект freecell), Mobitetris (Fuxx).
set -eu
DEST=${1:-${BT_J2ME_GAMES:-${XDG_DATA_HOME:-$HOME/.local/share}/bt-j2me/games}}
HERE=$(cd "$(dirname "$0")" && pwd)
SF=https://downloads.sourceforge.net/project
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$DEST/covers"
fetch() { curl -fsSL -o "$TMP/$1" "$2"; }

fetch jtReversi.jar "$SF/j2mereversi/jtReversi-jar/0.87.00/jtReversi.jar"
fetch freecell.zip  "$SF/freecell/Freecell/Mobile%20Freecell%20V0.6/Freecell_V0.6_bin.zip"
fetch sudoku.zip    "$SF/freecell/SudokuMe/SudokuME%20V1.32/Sudoku_bin.zip"
fetch tetris.zip    "$SF/mobitetris/mobitetris/mobitetris-0.0.2/mobitetris-0.0.2.zip"

cp "$TMP/jtReversi.jar" "$DEST/"
unzip -o -j -q "$TMP/freecell.zip" Freecell.jar -d "$DEST"
unzip -o -j -q "$TMP/sudoku.zip" Sudoku.jar -d "$DEST"
unzip -o -j -q "$TMP/tetris.zip" mobitetris-0.0.2/bin/Tetris.jar -d "$DEST"
cp "$HERE"/covers/*.png "$DEST/covers/"

echo "Готово: $DEST"
ls -1 "$DEST"
