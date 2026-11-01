#!/usr/bin/env bash
# Puts a "Claude Label Utility" icon on the desktop (and in the app menu)
# that launches modern/tools/claude_label_utility.py on its own, separate
# from ViewTouch and from vt_label_utility.py.
#
# Run once on the Pi:   bash modern/tools/install_claude_label_utility.sh
set -e

TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="$TOOLS_DIR/claude_label_utility.py"
MODERN_DIR="$(dirname "$TOOLS_DIR")"

if [ ! -f "$APP" ]; then
    echo "Cannot find $APP" >&2
    exit 1
fi
chmod +x "$APP"

# Check the Python libraries the utility needs
missing=""
python3 -c "import tkinter" 2>/dev/null || missing="$missing python3-tk"
python3 -c "import PIL.Image" 2>/dev/null || missing="$missing python3-pil"
python3 -c "import reportlab" 2>/dev/null || missing="$missing python3-reportlab"
if [ -n "$missing" ]; then
    echo "Installing missing packages:$missing"
    sudo apt-get install -y $missing
fi

DESKTOP_DIR="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
[ -n "$DESKTOP_DIR" ] && [ -d "$DESKTOP_DIR" ] || DESKTOP_DIR="$HOME/Desktop"
mkdir -p "$DESKTOP_DIR" "$HOME/.local/share/applications"

ENTRY="[Desktop Entry]
Type=Application
Version=1.0
Name=Claude Label Utility
Comment=Print ViewTouch shelf and product labels (bitmap rendering)
Exec=python3 \"$APP\"
Path=$MODERN_DIR
Icon=printer
Terminal=false
Categories=Office;
StartupNotify=true
"

DESKTOP_FILE="$DESKTOP_DIR/claude-label-utility.desktop"
MENU_FILE="$HOME/.local/share/applications/claude-label-utility.desktop"
printf '%s' "$ENTRY" > "$DESKTOP_FILE"
printf '%s' "$ENTRY" > "$MENU_FILE"
chmod +x "$DESKTOP_FILE" "$MENU_FILE"

# Mark the launcher as trusted where the desktop supports it (quietly ignored otherwise)
gio set "$DESKTOP_FILE" metadata::trusted true 2>/dev/null || true

echo "Installed:"
echo "  Desktop icon : $DESKTOP_FILE"
echo "  Menu entry   : $MENU_FILE (Office menu)"
echo "  Launches     : $APP"
echo "Errors are logged to /tmp/claude_label_utility.log"
