#!/usr/bin/env python3
"""Development notes helper for modern/vt-modern-development-notes.md.

Add an entry (newest first, Eastern time, the shared format):

  python3 modern/tools/log_dev_note.py --agent "Bob (Claude)" --title "Fix printer offsets" \\
      --what "..." --why "..." [--files a.py b.sh] [--next "..."] [--commit abc1234]

  --files defaults to the files staged for commit (git add them first).

Check the staged commit (used by .githooks/pre-commit):

  python3 modern/tools/log_dev_note.py --check-staged

The check passes when nothing outside the notes / CHANGELOG.md is staged, or when the notes file
is staged with a new, complete entry at the top of its Log.
"""
import argparse, datetime, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
NOTES_REL = "modern/vt-modern-development-notes.md"
NOTES = ROOT / NOTES_REL
EXEMPT = {NOTES_REL, "modern/CHANGELOG.md"}
HEADING = re.compile(r"^### (\d{4}-\d{2}-\d{2} \d{2}:\d{2}) ET — (.+?) — (.+)$")
REQUIRED = ("What", "Why", "Files")


def git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True).stdout


def now_et():
    try:
        from zoneinfo import ZoneInfo
        return datetime.datetime.now(ZoneInfo("America/New_York")).strftime("%Y-%m-%d %H:%M")
    except Exception:
        return datetime.datetime.now().strftime("%Y-%m-%d %H:%M")


def has_bullet(text, name):
    return re.search(r"^- \*\*%s(:\*\*|\*\*:)" % name, text, re.MULTILINE) is not None


def first_entry(text):
    """Return (heading_line, body) of the newest entry under '## Log'."""
    m = re.search(r"^## Log[^\n]*\n", text, re.MULTILINE)
    if not m:
        return None, ""
    rest = text[m.end():]
    h = re.search(r"^### .*$", rest, re.MULTILINE)
    if not h:
        return None, ""
    nxt = re.search(r"^### .*$", rest[h.end():], re.MULTILINE)
    body = rest[h.end(): h.end() + nxt.start()] if nxt else rest[h.end():]
    return h.group(0), body


def add_entry(a):
    files = a.files or [f for f in git("diff", "--cached", "--name-only").split() if f not in EXEMPT]
    lines = [f"### {now_et()} ET — {a.agent} — {a.title}",
             f"- **What:** {a.what}",
             f"- **Why:** {a.why}",
             "- **Files:** " + (", ".join(f"`{f}`" for f in files) if files else "(none)")]
    if a.commit:
        lines.append(f"- **Commit:** `{a.commit}`")
    if a.next:
        lines.append(f"- **Next person must know:** {a.next}")
    entry = "\n".join(lines) + "\n\n"
    text = NOTES.read_text(encoding="utf-8")
    m = re.search(r"^## Log[^\n]*\n\n?", text, re.MULTILINE)
    if not m:
        sys.exit(f"{NOTES_REL} has no '## Log' section")
    NOTES.write_text(text[:m.end()] + entry + text[m.end():], encoding="utf-8")
    subprocess.run(["git", "-C", str(ROOT), "add", NOTES_REL])
    print(f"Added to {NOTES_REL} (and staged):\n\n{entry}")


def check_staged():
    staged = [f for f in git("diff", "--cached", "--name-only", "--diff-filter=ACMRD").split() if f]
    changed = [f for f in staged if f not in EXEMPT]
    if not changed:
        return 0
    problems = []
    if NOTES_REL not in staged:
        problems.append(f"{NOTES_REL} is not part of this commit.")
    else:
        added = {ln[1:] for ln in git("diff", "--cached", "-U0", "--", NOTES_REL).splitlines()
                 if ln.startswith("+") and not ln.startswith("+++")}
        new_heads = [ln for ln in added if HEADING.match(ln)]
        staged_text = git("show", f":{NOTES_REL}")
        top, body = first_entry(staged_text)
        if not new_heads:
            problems.append("no new entry heading was added. Use:  ### YYYY-MM-DD HH:MM ET — <who> — <short title>")
        elif top not in new_heads:
            problems.append("the new entry must be at the TOP of the Log (newest first).")
        else:
            missing = [b for b in REQUIRED if not has_bullet(body, b)]
            if missing:
                problems.append("the new entry is missing: " + ", ".join(f"**{b}:**" for b in missing))
    if not problems:
        return 0
    sys.stderr.write(
        "\n✖ Commit blocked: every change must be logged in " + NOTES_REL + "\n  - " + "\n  - ".join(problems) +
        "\n\n  Changed: " + ", ".join(changed[:8]) + (" …" if len(changed) > 8 else "") +
        "\n\n  Quick way (stages the entry for you):\n"
        "    python3 modern/tools/log_dev_note.py --agent "<Bob (Claude)|Neil (Gemini)|name>" --title \"...\" \\\n"
        "        --what \"...\" --why \"...\" [--next \"...\"]\n"
        "  then commit again.  (Emergency only: git commit --no-verify)\n\n")
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check-staged", action="store_true", help="validate the staged commit (pre-commit hook)")
    ap.add_argument("--agent"); ap.add_argument("--title"); ap.add_argument("--what"); ap.add_argument("--why")
    ap.add_argument("--files", nargs="*"); ap.add_argument("--next"); ap.add_argument("--commit")
    a = ap.parse_args()
    if a.check_staged:
        sys.exit(check_staged())
    if not (a.agent and a.title and a.what and a.why):
        ap.error("--agent, --title, --what and --why are required to add an entry")
    add_entry(a)


if __name__ == "__main__":
    main()
