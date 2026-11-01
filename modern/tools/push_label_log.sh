#!/usr/bin/env bash
# Uploads the Claude Label Utility log (and the last label sent to the printer)
# to the "pi-logs" branch on GitHub so it can be reviewed remotely.
# It does not touch your working files or your current branch.
#
#   bash modern/tools/push_label_log.sh
set -e
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

REMOTE="${1:-myfork}"
git remote get-url "$REMOTE" >/dev/null 2>&1 || REMOTE=origin

files=()
for f in /tmp/claude_label_utility.log /tmp/last_label.zpl \
         "$HOME/.local/share/ViewTouch/claude_label_templates.json"; do
    [ -s "$f" ] && files+=("$f")
done
if [ ${#files[@]} -eq 0 ]; then
    echo "Nothing to upload: /tmp/claude_label_utility.log is empty or missing (no errors logged yet)."
    exit 0
fi

# Build a commit straight from the files, without checking anything out
tree_input=""
for f in "${files[@]}"; do
    blob=$(git hash-object -w "$f")
    tree_input+="100644 blob $blob	$(basename "$f")"$'\n'
done
tree=$(printf '%s' "$tree_input" | git mktree)
commit=$(echo "Label utility logs from $(hostname) at $(date '+%Y-%m-%d %H:%M:%S')" | git commit-tree "$tree")
git push -q -f "$REMOTE" "$commit:refs/heads/pi-logs"

echo "Uploaded to the pi-logs branch:"
for f in "${files[@]}"; do echo "  $(basename "$f")  ($(wc -c < "$f") bytes)"; done
