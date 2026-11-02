#!/usr/bin/env bash
# Brings the Pi up to date: pulls the latest from GitHub (main),
# runs the label utility installer, turns on the dev-notes hook, and pushes main so GitHub matches the Pi.
# The desktop "Update Label Utility" icon runs this.
#
#   bash modern/tools/update_label_utility.sh
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
REMOTE=myfork
git remote get-url "$REMOTE" >/dev/null 2>&1 || REMOTE=origin

ok=1
echo "== Pulling the latest changes from GitHub ($REMOTE)..."
git pull --no-rebase --no-edit "$REMOTE" main || ok=0
if [ $ok = 1 ]; then
    echo "== Installing / checking the label utility..."
    bash modern/tools/install_label_utility.sh || ok=0
    bash modern/tools/setup_dev_hooks.sh >/dev/null 2>&1 || true
fi
if [ $ok = 1 ]; then
    echo "== Updating GitHub's main..."
    git push "$REMOTE" HEAD:main || ok=0
fi

if [ $ok = 1 ]; then
    echo; echo "✓ Up to date: $(git log -1 --format='%h  %s')"
else
    echo; echo "✖ Something went wrong above. Nothing was lost; send Bob a photo of this window."
fi
type beep3 >/dev/null 2>&1 && beep3
[ $ok = 1 ]   # exit 0 when the update worked (beep3 only exists in an interactive shell)
