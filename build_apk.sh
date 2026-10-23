#!/bin/bash
set -e
cd ~/Desktop/viewtouch-main && mkdir -p apks
gh auth setup-git
git add -A && git commit -m "${1:-Tablet APK update}" || true
VER="0.9.$(git rev-list --count HEAD)" && TAG="modern-v${VER}" && SHA=$(git rev-parse --short HEAD)
git push myfork main && git tag -f "$TAG" && git push myfork "$TAG" --force && sleep 10
RID=$(gh run list -R paigeent1/viewtouch -w modern-android.yml -L 1 --json databaseId -q '.[0].databaseId')
RNUM=$(gh run list -R paigeent1/viewtouch -w modern-android.yml -L 1 --json number -q '.[0].number')
echo "=== Watching Cloud Build #b${RNUM} (Run ID: ${RID}) ==="
gh run watch "$RID" -R paigeent1/viewtouch --exit-status
TMP=$(mktemp -d) && gh run download "$RID" -R paigeent1/viewtouch -n ViewTouch-android-arm64-v8a -D "$TMP"
APK="apks/ViewTouch-v${VER}-b${RNUM}-${SHA}-arm64-v8a.apk" && cp -f $(find "$TMP" -name "*.apk" | head -n 1) "$APK" && rm -rf "$TMP"
gh release create "$TAG" "$APK" -R paigeent1/viewtouch -t "ViewTouch ${TAG}" -n "${1:-Update}" 2>/dev/null || gh release upload "$TAG" "$APK" -R paigeent1/viewtouch --clobber
echo "| v${VER} | ${TAG} | b${RNUM} | ${SHA} | $(date +'%Y-%m-%d %H:%M') | $(basename "$APK") |" >> apks/APK_BUILD_LIST.md
echo "✓ Saved APK: ~/Desktop/viewtouch-main/$APK"
