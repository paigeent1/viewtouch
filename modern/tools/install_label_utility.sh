#!/usr/bin/env bash
# Sets up the ViewTouch Label Utility (modern/tools/vt_label_utility.py):
#  - the launcher ViewTouch's "Label Utility" button runs (/usr/local/bin/vt-label-utility)
#  - a desktop icon and Office-menu entry
#  - the Python libraries and the fonts the ILSA label layouts use
#
# Run on the Pi (safe to run again any time):   bash modern/tools/install_label_utility.sh
set -e

TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="$TOOLS_DIR/vt_label_utility.py"
MODERN_DIR="$(dirname "$TOOLS_DIR")"
REPO_DIR="$(dirname "$MODERN_DIR")"
LAUNCHER=/usr/local/bin/vt-label-utility

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

# The ILSA label layouts use Arial, Arial Black and Courier New. Install Microsoft's core
# fonts so labels print with the same letter shapes and spacing (falls back to Liberation fonts).
if ! fc-list | grep -qi "Arial Black"; then
    echo "Installing Microsoft core fonts (Arial, Arial Black, Courier New)..."
    echo "ttf-mscorefonts-installer msttcorefonts/accepted-mscorefonts-eula select true" | sudo debconf-set-selections || true
    if sudo apt-get install -y ttf-mscorefonts-installer && fc-cache -f >/dev/null 2>&1; then
        echo "  Fonts installed."
    else
        echo "  Could not install the Microsoft fonts; labels will use Liberation fonts instead."
    fi
fi

# The launcher ViewTouch's "Label Utility" button (command openLabelUtility) runs
if [ -f "$REPO_DIR/scripts/bin/vt-label-utility" ] && ! cmp -s "$REPO_DIR/scripts/bin/vt-label-utility" "$LAUNCHER"; then
    echo "Installing $LAUNCHER (used by ViewTouch's Label Utility button)..."
    sudo install -m 755 "$REPO_DIR/scripts/bin/vt-label-utility" "$LAUNCHER"
fi

DESKTOP_DIR="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
[ -n "$DESKTOP_DIR" ] && [ -d "$DESKTOP_DIR" ] || DESKTOP_DIR="$HOME/Desktop"
mkdir -p "$DESKTOP_DIR" "$HOME/.local/share/applications"

# Remove the old separate "Claude Label Utility" icon (now the ViewTouch Label Utility)
rm -f "$DESKTOP_DIR/claude-label-utility.desktop" "$HOME/.local/share/applications/claude-label-utility.desktop"

ENTRY="[Desktop Entry]
Type=Application
Version=1.0
Name=ViewTouch Label Utility
Comment=Print ViewTouch shelf, price, cigar and receiving labels
Exec=python3 \"$APP\"
Path=$MODERN_DIR
Icon=printer
Terminal=false
Categories=Office;
StartupNotify=true
"

DESKTOP_FILE="$DESKTOP_DIR/vt-label-utility.desktop"
MENU_FILE="$HOME/.local/share/applications/vt-label-utility.desktop"
printf '%s' "$ENTRY" > "$DESKTOP_FILE"
printf '%s' "$ENTRY" > "$MENU_FILE"
chmod +x "$DESKTOP_FILE" "$MENU_FILE"

# Mark the launcher as trusted where the desktop supports it (quietly ignored otherwise)
gio set "$DESKTOP_FILE" metadata::trusted true 2>/dev/null || true

echo "Installed:"
echo "  Desktop icon : $DESKTOP_FILE"
echo "  Menu entry   : $MENU_FILE (Office menu)"
echo "  ViewTouch    : Label Utility button -> $LAUNCHER -> $APP"
echo "Errors are logged to /tmp/vt_label_utility.log"
