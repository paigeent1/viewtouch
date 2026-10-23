#!/bin/bash
set -e
cd ~/Desktop/viewtouch-main
mkdir -p apks
NOTES="${1:-POS progressive code update}"

git add -f modern/cmake/* 2>/dev/null || true
if [ -n "$(git status --porcelain)" ]; then
    git add -A && git commit -m "$NOTES"
fi

COMMITS=$(git rev-list --count HEAD)
SHA=$(git rev-parse --short HEAD)
VER="0.9.${COMMITS}"
TAG="modern-v${VER}"

echo "=== Pushing Commit #${COMMITS} (${SHA}) & Tag ${TAG} to GitHub ==="
git push myfork main
git tag -f "$TAG" && git push myfork "$TAG" --force
sleep 8

RUN_ID=$(gh run list -R paigeent1/viewtouch --workflow=modern-android.yml -L 1 --json databaseId -q '.[0].databaseId')
RUN_NUM=$(gh run list -R paigeent1/viewtouch --workflow=modern-android.yml -L 1 --json number -q '.[0].number')
echo "=== Watching Cloud Build #b${RUN_NUM} (Run ID: ${RUN_ID}) ==="
gh run watch "$RUN_ID" -R paigeent1/viewtouch --exit-status

TMP_DL=$(mktemp -d)
gh run download "$RUN_ID" -R paigeent1/viewtouch -n ViewTouch-android-arm64-v8a -D "$TMP_DL"
RAW_APK=$(find "$TMP_DL" -name "*.apk" | head -n 1)
APK_FILE="apks/ViewTouch-v${VER}-b${RUN_NUM}-${SHA}-arm64-v8a.apk"
cp -f "$RAW_APK" "$APK_FILE"
rm -rf "$TMP_DL"

gh release create "$TAG" "$APK_FILE" -R paigeent1/viewtouch -t "ViewTouch ${TAG} (b${RUN_NUM})" -n "$NOTES" 2>/dev/null || \
gh release upload "$TAG" "$APK_FILE" -R paigeent1/viewtouch --clobber

LIST="apks/APK_BUILD_LIST.md"
[ -f "$LIST" ] || printf "# ViewTouch APK Build List\n\n| Version | GitHub Tag | Build | Commit | Date | File in apks/ |\n|---|---|---|---|---|---|\n" > "$LIST"
echo "| **v${VER}** | \`${TAG}\` | **b${RUN_NUM}** | \`${SHA}\` | $(date +'%Y-%m-%d %H:%M') | \`$(basename "$APK_FILE")\` — ${NOTES} |" >> "$LIST"
git add "$LIST" build_apk.sh && git commit -m "Log APK ${TAG} (b${RUN_NUM})" && git push myfork main || true
echo "✓ Saved locally: ~/Desktop/viewtouch-main/$APK_FILE"
echo "✓ Saved on GitHub Releases: https://github.com/paigeent1/viewtouch/releases/tag/$TAG"
