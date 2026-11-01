#!/bin/sh
# CRT Overlay installer for Linux (x86_64, X11).
#   curl -fsSL https://raw.githubusercontent.com/FLEXIY0/BT-J2ME/refs/heads/claude/amazing-fermi-l6s51a/crt-overlay/install.sh | sh
# Options (after "sh -s --"):  --no-autostart   --uninstall
set -eu

BASE="${CRT_OVERLAY_BASE:-https://raw.githubusercontent.com/FLEXIY0/BT-J2ME/refs/heads/claude/amazing-fermi-l6s51a/crt-overlay}"
URL="$BASE/dist/crt-overlay-linux-x64.tar.gz"
BIN_DIR="$HOME/.local/bin"
AUTOSTART="$HOME/.config/autostart/crt-overlay.desktop"
APPS="$HOME/.local/share/applications/crt-overlay.desktop"

autostart=1
for arg in "$@"; do
    case "$arg" in
        --no-autostart) autostart=0 ;;
        --uninstall)
            pkill -x crt-overlay 2>/dev/null || true
            rm -f "$BIN_DIR/crt-overlay" "$AUTOSTART" "$APPS"
            echo "CRT Overlay removed (settings kept in ~/.config/crt-overlay)."
            exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

[ "$(uname -s)" = Linux ] || { echo "This installer is for Linux. On Windows use install.ps1." >&2; exit 1; }
[ "$(uname -m)" = x86_64 ] || { echo "Only x86_64 builds are available." >&2; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
echo "Downloading $URL"
if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "$tmp/crt.tar.gz" "$URL"
elif command -v wget >/dev/null 2>&1; then
    wget -O "$tmp/crt.tar.gz" "$URL"
else
    echo "Need curl or wget." >&2; exit 1
fi
tar -xzf "$tmp/crt.tar.gz" -C "$tmp"

mkdir -p "$BIN_DIR" "$(dirname "$APPS")"
pkill -x crt-overlay 2>/dev/null || true
install -m 755 "$tmp/crt-overlay" "$BIN_DIR/crt-overlay"

desktop_entry() {
    cat <<DESKTOP
[Desktop Entry]
Type=Application
Name=CRT Overlay
Comment=CRT shader over the whole desktop (control it from the tray)
Exec=$BIN_DIR/crt-overlay
Icon=video-display
Terminal=false
Categories=Utility;
DESKTOP
}
desktop_entry > "$APPS"
if [ "$autostart" = 1 ]; then
    mkdir -p "$(dirname "$AUTOSTART")"
    desktop_entry > "$AUTOSTART"
fi

echo "Installed $BIN_DIR/crt-overlay"
[ "$autostart" = 1 ] && echo "Autostart: $AUTOSTART"
if [ -n "${DISPLAY:-}" ]; then
    nohup "$BIN_DIR/crt-overlay" >/dev/null 2>&1 &
    echo "Started. Use the tray icon to change settings or quit."
fi
