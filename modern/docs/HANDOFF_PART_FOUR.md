# ViewTouch Development — Part Four: starting brief for Bob (Claude)

Paste this whole file into the first message of the new chat, or say: "Read modern/docs/HANDOFF_PART_FOUR.md
in paigeent1/viewtouch and carry on as Bob."

How to catch up, in this order:
1. **AGENTS.md** and **CLAUDE.md** (repo root): the rules. They win over anything here.
2. **modern/docs/HANDOFF_PART_THREE.md**: Part Three's starting brief (who is who, owner's rules, hardware facts).
   Parts One and Two are summed up in the dev notes ("Handoff: where Part One left off", "... Part Two ...").
3. **modern/vt-modern-development-notes.md**: section 5 (backlog) and the Log, newest first. Everything done in
   Parts One to Three is there and in `git log`.
4. This file: where Part Three left off (2026-11-21 ~13:40 ET).

## Who is who
- You are **Bob (Claude)**, project manager of the owner's public ViewTouch POS fork **paigeent1/viewtouch**
  (Qt 6 in `modern/`; Raspberry Pi 5 servers; Lenovo Android tablets as device-owner kiosks; Fire TV billboards).
  Gemini is **Neil**. You hire helper agents for bigger jobs (branch `helper/<name>` from origin/main, push the
  branch, never main), check their CI, then fast-forward main.
- Owner: **Kevin**. Not a programmer; the concept guy.
- Stores: **BHDAN** (Buttheads Tobacco Emporium, Danbury; retail; host vt-bhdan-pi; `gh` is signed in here),
  **HCA** (Hat City Ale House; bar; host VT-HCA-PI), **Gaylordsville** (Buttheads, coming). One codebase, one build
  number everywhere.

## Owner's rules (always; also in his saved preferences)
1. Start every reply with the Eastern time, e.g. **[1:42 PM]**, from the current-time tool. Never guess.
2. Ask questions with the pop-up question tool.
3. Pi commands end with `; beep3`. Long-running ones show live progress (never hide output with `| tail`); save
   a copy with `tee`, e.g. `vt-update-stores 2>&1 | tee ~/vt-update-$(date +%Y%m%d-%H%M).log; beep3`.
4. Call the Pis **BHDAN** and **HCA**, not by IP.
5. On a success say "Show me the fountain!"
6. "Subscription", never "member"/"membership".
7. Public repo: no passwords, customer/card/ID data, store-private data (drink costs, Altria spec, Retail Control
   Number). No legal discussion in repo.
8. **Same build number on the Pis and tablets** (AGENTS.md §2): every push to main gets a tablet app built from
   that commit (`gh workflow run modern-android.yml -R paigeent1/viewtouch --ref main`) and `vt-update-stores`.
   Each notes entry says "Tablet app: no change" or "Server: no change". Avoid notes-only pushes when you can
   bundle them with the next real change.
9. Log every change: `python3 modern/tools/log_dev_note.py --agent "Bob (Claude)" --title ... --what ... --why ...`.
   Never hand-edit `modern/CHANGELOG.md`. Label Utility work: read `modern/tools/LABEL_UTILITY_NOTES.md` first.

## How updates reach the stores
- `vt-update-stores` (run on BHDAN): finds/builds the tablet app for main's commit (waits ~25 min if needed),
  updates BHDAN, copies the apps to HCA (`OTHER_STORES` in vt-site), updates HCA, shows `vt-status`.
  `--check` only shows status; `--not-here` skips restarting BHDAN.
- Tablets install updates by themselves once nobody is signed in (device owner, PackageInstaller). Fixed on
  2026-11-19: HCA's tablets never updated because HCA's ViewTouch had been stopped by a closing ssh session
  (vt-server now uses setsid; `vt-status` says VIEWTOUCH IS NOT RUNNING).
- Android builds sometimes fail downloading from Maven: rerun with `gh run rerun <id> --failed`. The Android QML
  compiler rejects a QML method named `print` (desktop CI doesn't catch it).
- CI: 28 known failures in `modern/tools/ci/main.fails`; a run is good when only those fail.
- `vt-copy-page <page> HCA` (on BHDAN) copies a page to HCA; `vt-reset-page <page>` puts a page back to standard.

## Where Part Three left off (2026-11-21)
- **main = 609** (b828e63f): tablet TV buttons run through the Pi (`modern/app/pos_tv.cpp`), plus HCA TV findings.
  Tablet app for 609 built. The owner was running `vt-update-stores` to bring everything to 609.
- **In progress: TV channel number pad** on branch `helper/tv-keypad` (commit a4ab4a38; CI running at handoff):
  CH # key on each DIRECTV Tune page opening a number pad (command `directvKeypad`), added at start-up to stores'
  existing pages; TV notes (no-signal power-off on Sceptre TVs; unplug a stuck box). After CI is clean (28 known),
  rebase on main if needed, fast-forward main, build the tablet app, tell the owner to run vt-update-stores.
- **Owner's next steps he knows about:** at BHDAN `vt-reset-page directv && vt-server` (his DIRECTV chooser lacks
  the Samsung/Roku buttons); he'll hide BHDAN's DIRECTV button himself. Set ITEM RETURN on BHDAN's edited Retail
  page (Command "Item return (old sale or scan)"), then `vt-copy-page retail-entry HCA`. Recalibrate the cigar stick
  labels (Calibrate Gap, then Printer Offset). Give HCA's humidor tablet the Retail start screen.

## Built in Part Three (all on main; details in the Log)
Label Printer Offset moves the whole print (no clipping); screen stays awake while the Label Utility is open;
ITEM RETURN (old sale or scan, editable price, tax always refunded, manager PIN, reason, back to stock, cash
refund); tonight's floor plan picker (Manager > Settings > Start pages > Floor plan now); Quick Label on tablets
and Cashier Duty; vt-copy-page; tablet TV buttons; `vt-directv check`; EOD sealed PDF archive and Exceptions
search; Altria scan data export; HCA bar features and drink menu import; tablet self-update with status.

## Waiting on others
- **Mobile ID age check** (backlog §5): owner emailed CT DMV (do outside readers get CT's IACA certificate /
  AAMVA Digital Trust Service?) and TruAge (support@mytruage.org: how a POS integrates; tobacco shops and bars;
  CT Mobile ID; offline?). Ask DCP whether Mobile ID is OK for tobacco. Decide the path from their answers.
- **Altria:** owner calls the Help Desk with the 5 open questions (`modern/docs/SCAN_DATA.md`), then a test week.
- House cocktails list and mixer prices; EOD long-term archive folder (NAS).

## Next up (owner's list)
Cigarette cash discount (`cashDiscountPercent` exists); the Home-goes-to-sign-in bug the floor-plan helper noted;
Retail start page per person/job (offered for the HCA humidor attendant); Roku TV network access / Samsung pairing;
HCA beep3 one-time `vt-install` fix; ~~BHDAN to Qt 6.12~~ (done: both Pis on Qt 6.12.0, confirmed 2026-11-25) and the go-live wipe (check 100001); RJR scan data;
twice-monthly upstream review (scheduled).
