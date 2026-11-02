#!/usr/bin/env bash
# Takes screenshots of ViewTouch Modern screens for the manuals (gift card manual first) and uploads them to
# the "pi-vt-shots" branch on GitHub, where Bob (Claude) picks them up.
#
# It uses the test suite's "Manual: a screenshot of every screen" case: a made-up demo store in memory,
# drawn off screen. The store's real data (viewtouch.db) and the running ViewTouch are never touched,
# and nothing prints. Takes a few minutes (longer the first time, if the tests have to be built).
#
#   bash modern/tools/capture_vt_screenshots.sh
set -e
cd "$(dirname "${BASH_SOURCE[0]}")/.."          # modern/
BUILD="${BUILD_DIR:-build}"
OUT=/tmp/vt_manual_shots

if [ ! -d "$BUILD" ]; then
    echo "No ViewTouch build folder at modern/$BUILD (set BUILD_DIR=... to point at it)."; exit 1
fi
if [ ! -x "$BUILD/tests/vtm_tests" ]; then
    echo "Building the ViewTouch tests (once)..."
    cmake --build "$BUILD" --target vtm_tests -j"$(nproc)"
fi

rm -rf "$OUT"; mkdir -p "$OUT"
echo "Taking screenshots (a few minutes; nothing shows on screen)..."
VTM_SHOTS="$OUT" QT_QPA_PLATFORM=offscreen "$BUILD/tests/vtm_tests" "Manual: a screenshot of every screen" || true

# The screens the gift card manual uses.
WANT="m10-check-options m15-takeout-customer m16-customers m17-gift-card m18-settle m30-manager m46-report-sales m48-report-gift-cards"
REMOTE="${1:-myfork}"
cd ..
git remote get-url "$REMOTE" >/dev/null 2>&1 || REMOTE=origin
tree_input=""; n=0
for name in $WANT; do
    for f in "$OUT/$name".png "$OUT/$name".jpg; do
        [ -f "$f" ] || continue
        blob=$(git hash-object -w "$f")
        tree_input+="100644 blob $blob	$(basename "$f")"$'\n'; n=$((n + 1))
    done
done
if [ "$n" -eq 0 ]; then
    echo "No screenshots were made. Files in $OUT:"; ls "$OUT" | head; exit 1
fi
tree=$(printf '%s' "$tree_input" | git mktree)
commit=$(echo "ViewTouch screenshots from $(hostname) at $(date '+%Y-%m-%d %H:%M:%S')" | git commit-tree "$tree")
git push -q -f "$REMOTE" "$commit:refs/heads/pi-vt-shots"
echo "Uploaded $n screenshots to the pi-vt-shots branch."
