#!/usr/bin/env bash
# Captures screenshots of the ViewTouch Label Utility for the user manual and uploads them to the
# "pi-shots" branch on GitHub. The label utility opens for about 20 seconds and steps through its
# screens by itself — don't touch the mouse or keyboard until it closes. Nothing is printed or saved.
#
#   bash modern/tools/capture_manual_screenshots.sh
set -e
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

export DISPLAY=${DISPLAY:-:0}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}

python3 modern/tools/capture_manual_screenshots.py

REMOTE="${1:-myfork}"
git remote get-url "$REMOTE" >/dev/null 2>&1 || REMOTE=origin
tree_input=""
for f in /tmp/vt_label_shots/*.png; do
    blob=$(git hash-object -w "$f")
    tree_input+="100644 blob $blob	$(basename "$f")"$'\n'
done
tree=$(printf '%s' "$tree_input" | git mktree)
commit=$(echo "Label utility screenshots from $(hostname) at $(date '+%Y-%m-%d %H:%M:%S')" | git commit-tree "$tree")
git push -q -f "$REMOTE" "$commit:refs/heads/pi-shots"
echo "Uploaded $(ls /tmp/vt_label_shots/*.png | wc -l) screenshots to the pi-shots branch."
