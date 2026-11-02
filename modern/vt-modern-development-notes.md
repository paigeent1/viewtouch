# ViewTouch Modern — development notes

A running, time-stamped record of changes made to this repository by each contributor
(people, Bob (Claude), Gemini). It complements `CHANGELOG.md`, which is generated automatically from
commits by `tools/changelog.py` — don't hand-edit that one.

**How to add an entry**
- Add new entries at the **top** of the log (newest first).
- Heading: `### YYYY-MM-DD HH:MM ET — <who> — <short title>` (Eastern time, the store's clock).
- Say **what** changed, **why**, the **files**, and the **commit** (short hash) once pushed.
- Mention anything the next person must know (breaking changes, steps to run on the Pi).
- Pull before editing and commit this file together with the change it describes.
- Quickest: stage your change, then run
  `python3 modern/tools/log_dev_note.py --agent <who> --title "..." --what "..." --why "..." [--next "..."]`.
- **Enforced:** `.githooks/pre-commit` blocks any commit that changes files without a new, complete entry at
  the top of this Log. Turn it on once per clone with `bash modern/tools/setup_dev_hooks.sh`.
  Rules for everyone (people, Bob (Claude), Gemini) are in `AGENTS.md` (also `CLAUDE.md`, `GEMINI.md`).

---

## Log

### 2026-11-02 10:54 ET — Bob (Claude) — Gift card manual: real screenshots from the Pi
- **What:** Added the 8 demo-store screenshots from pi-vt-shots (10:52 capture) to modern/docs/gift-cards/screens and rebuilt the manual (15 pages): Gift Card screen, check options, payment screen, Reports > Gift Cards. Corrected 'Payment Types' to 'Tenders' and the card title to 'Card ••1001' to match the screens; headings kept with their tables.
- **Why:** Draft 1 had grey placeholders; the screenshot test now runs on the Pi after 1ccb69f.
- **Files:** `modern/docs/gift-cards/ViewTouch_Gift_Cards_User_Manual.pdf`, `modern/docs/gift-cards/build_manual.py`, `modern/docs/gift-cards/screens/m10-check-options.png`, `modern/docs/gift-cards/screens/m15-takeout-customer.png`, `modern/docs/gift-cards/screens/m16-customers.png`, `modern/docs/gift-cards/screens/m17-gift-card.png`, `modern/docs/gift-cards/screens/m18-settle.png`, `modern/docs/gift-cards/screens/m30-manager.png`, `modern/docs/gift-cards/screens/m46-report-sales.png`, `modern/docs/gift-cards/screens/m48-report-gift-cards.png`

### 2026-11-02 10:48 ET — Bob (Claude) — UI tests: load Main.qml the way the app does
- **What:** test_pos_flow.cpp's Screen now loads Main.qml through loadViewTouchQml(): adds qrc:/ and qrc:/qt/qml import paths and loads the file from :/qt/qml/ViewTouch/ui/qml/ or :/ViewTouch/ui/qml/ (as main.cpp's vtmLoadModuleQml), falling back to loadFromModule.
- **Why:** After b247aa1 the Pi still failed with 'Module ViewTouch contains no type named Main': without the QTP0001 policy the module lives under qrc:/ViewTouch, which loadFromModule doesn't search. The app already works around this; the tests didn't.
- **Files:** `modern/tests/test_pos_flow.cpp`
- **Next person must know:** Other UI tests (test_pages_ui.cpp, test_editor_ui.cpp) still call loadFromModule and will fail the same way on the Pi; only the screenshot path is fixed.

### 2026-11-02 10:40 ET — Bob (Claude) — Tests: register the static ViewTouch QML module like the app does
- **What:** tests/main.cpp now calls qml_register_types_ViewTouch() and Q_INIT_RESOURCE(qmake_ViewTouch / vtm_ui_raw_qml_0), mirroring main.cpp (store commit 0e4a663). capture_vt_screenshots.sh now always rebuilds vtm_tests (incremental, -j3) before running.
- **Why:** On the Pi every UI test failed with 'Module ViewTouch contains no type named Main': the linker dropped the static QML module's registration/resources in vtm_tests, the same problem main.cpp was fixed for. Blocked the gift card manual screenshots.
- **Files:** `modern/tests/main.cpp`, `modern/tools/capture_vt_screenshots.sh`
- **Next person must know:** Untested here (no Qt in Bob's workspace); the first Pi run compiles it. If it fails to compile, the app build is unaffected only when building the ViewTouch target alone.

### 2026-11-02 10:38 ET — Bob (Claude) — Label manual: new Pi screenshots (screen 1 template actions, printer settings)
- **What:** Rebuilt the label utility manual with the 10:36 pi-shots capture: screen 1 now shows Delete Template and Restore Standard Labels; printer settings shot trimmed of window bleed at the edges.
- **Why:** The earlier screen 1 shot predated the new Template Actions buttons.
- **Files:** `modern/docs/label-utility/ViewTouch_Label_Utility_User_Manual.pdf`, `modern/docs/label-utility/screens/printer_settings.png`, `modern/docs/label-utility/screens/screen1_start.png`
- **Next person must know:** Pi repo is ~/viewtouch-main (not ~/viewtouch); use that path in Pi commands.

### 2026-11-02 10:35 ET — Bob (Claude) — update_label_utility.sh: exit 0 on success
- **What:** The script now ends with its own success status instead of the status of the beep3 check.
- **Why:** beep3 is a ~/.bashrc function, missing inside a script, so the script exited 1 even after a good update and 'update && capture' skipped the screenshot capture.
- **Files:** `modern/tools/update_label_utility.sh`

### 2026-11-02 10:21 ET — Bob (Claude) — Gift card user manual + ViewTouch screenshot script
- **What:** New 13-page PDF manual for gift cards in ViewTouch Modern (screen tour, look up, sell/reload, pay, cash out, fixing mistakes, reports, permissions, quick reference), written from WidgetGiftCard.qml, app/pos_customers.cpp and core/report.cpp. New tools/capture_vt_screenshots.sh runs the test suite's 'Manual: a screenshot of every screen' case off screen (demo store in memory; viewtouch.db untouched) and pushes the gift-card-related shots to the pi-vt-shots branch.
- **Why:** Owner asked for a gift card user manual. Qt can't run in Bob's workspace, so screenshots must be taken on the Pi.
- **Files:** `modern/docs/gift-cards/ViewTouch_Gift_Cards_User_Manual.pdf`, `modern/docs/gift-cards/build_manual.py`, `modern/docs/gift-cards/screens/.gitkeep`, `modern/tools/capture_vt_screenshots.sh`
- **Next person must know:** Screenshots show as grey placeholders until the Pi runs capture_vt_screenshots.sh; Bob then copies pi-vt-shots into modern/docs/gift-cards/screens/ and rebuilds. Seed page retail-entry.json button 'Gift Cards' jumps to page 'gift-cards', which doesn't exist (the page is 'gift-card').

### 2026-11-02 10:13 ET — Bob (Claude) — Neil's manual review: Delete/Restore templates, update script, manual additions
- **What:** Label utility: new Delete Template and Restore Standard Labels buttons on screen 1 (both ask first; the last template can't be deleted; restore keeps other templates and the RECEIVING LABEL tweaks from the original utility). New modern/tools/update_label_utility.sh: pulls main and claude-label-utility (merging), runs the installer, enables the notes hook, pushes main, beeps. .gitignore: __pycache__/ and *.pyc. Manual: update via the desktop icon / update script (sections 11, 13); deleting and restoring templates (sections 3, 10, 13); Properties window and status bar with fine-tuning tips (8.1); print quantity applies to every ticked item (5).
- **Why:** Neil (Gemini) reviewed the manual and suggested these six improvements; deleting templates wasn't possible in the program, so it was added rather than documented.
- **Files:** `.gitignore`, `modern/docs/label-utility/ViewTouch_Label_Utility_User_Manual.pdf`, `modern/docs/label-utility/build_manual.py`, `modern/tools/update_label_utility.sh`, `modern/tools/vt_label_utility.py`
- **Next person must know:** The Pi's desktop Update Label Utility icon should run modern/tools/update_label_utility.sh (no screenshots). The screen-1 screenshot predates the two new buttons; re-run capture_manual_screenshots.sh and rebuild the manual to refresh it.

### 2026-11-02 10:02 ET — Bob (Claude) — Fix blocked-commit message in log_dev_note.py
- **What:** Escaped the quotes in the example command shown when a commit is blocked.
- **Why:** The previous commit's name change left unescaped quotes inside a Python string; it compiled but would have crashed (NameError) instead of showing the blocked-commit help.
- **Files:** `modern/tools/log_dev_note.py`

### 2026-11-02 10:01 ET — Bob (Claude) — Notes refer to Bob (Claude) and Neil (Gemini)
- **What:** Renamed every contributor reference in this file (entry headings and mentions) from Claude/Gemini to Bob (Claude)/Neil (Gemini); product and file names (Claude Label Utility, claude_label_utility.py, CLAUDE.md, GEMINI.md) unchanged. log_dev_note.py examples now use the new names.
- **Why:** The owner asked that references in the development notes use Neil (Gemini) and Bob (Claude).
- **Files:** `modern/tools/log_dev_note.py`

### 2026-11-02 09:59 ET — Bob (Claude) — Contributor names: Bob (Claude) and Neil (Gemini)
- **What:** Recorded the owner's names for the AI contributors in AGENTS.md, CLAUDE.md, GEMINI.md and .gemini/styleguide.md: Claude is Bob, Gemini is Neil. Notes headings now use '— Bob (Claude) —' / '— Neil (Gemini) —'.
- **Why:** The owner asked Gemini to refer to itself as Neil and Claude as Bob.
- **Files:** `.gemini/styleguide.md`, `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`

### 2026-11-02 09:58 ET — Bob (Claude) — User manual with screenshots from the Pi
- **What:** Added 10 screenshots taken on the Pi (1360x768) to the label utility manual: every screen, the print window, Printer Offset, the label designer, and each designer toolbar above the table that explains its buttons (cut row by row so the text is readable). Headings kept with their content. build_manual.py now cuts the toolbar screenshots itself. Widened the position readout in Printer Offset, which was clipped.
- **Why:** The owner asked for screenshots in the manual, especially the designer's top toolbars next to their descriptions.
- **Files:** `modern/docs/label-utility/ViewTouch_Label_Utility_User_Manual.pdf`, `modern/docs/label-utility/build_manual.py`, `modern/docs/label-utility/screens/designer_full.png`, `modern/docs/label-utility/screens/designer_palette.png`, `modern/docs/label-utility/screens/designer_palette_stacked.png`, `modern/docs/label-utility/screens/designer_toolbar1.png`, `modern/docs/label-utility/screens/designer_toolbar1_stacked.png`, `modern/docs/label-utility/screens/designer_toolbar2.png`, `modern/docs/label-utility/screens/designer_toolbar2_stacked.png`, `modern/docs/label-utility/screens/print_window.png`, `modern/docs/label-utility/screens/printer_settings.png`, `modern/docs/label-utility/screens/screen1_start.png`, `modern/docs/label-utility/screens/screen2_categories.png`, `modern/docs/label-utility/screens/screen3_items.png`, `modern/docs/label-utility/screens/screen4_preview.png`, `modern/tools/vt_label_utility.py`
- **Next person must know:** To refresh the screenshots: run bash modern/tools/capture_manual_screenshots.sh on the Pi, copy the pi-shots branch PNGs into modern/docs/label-utility/screens/, then rebuild with python3 build_manual.py ViewTouch_Label_Utility_User_Manual.pdf.

### 2026-11-02 09:51 ET — Bob (Claude) — Label utility fits the Pi's 1360x768 screen; no missing-emoji boxes
- **What:** Toolbars (label designer x3, items screen, preview screen) now wrap onto extra rows instead of running off the right edge (flow_wrap). All 8 dialogs size themselves to their contents and centre on screen (fit_dialog). Windows use the real screen size instead of a fixed 1920x1080. Removed 31 emoji the Pi has no font for (they showed as empty boxes); kept the symbols it displays. Shortened the preview's printer badge. Screenshot script no longer prints grim's 'failed to create display'.
- **Why:** Screenshots taken on the Pi (1360x768) showed Code 128/UPC-A, Del and Printer Offset cut off, the printer-settings window missing Print Lower, Shift Right, 600 dpi and Cancel, and empty boxes in place of emoji.
- **Files:** `modern/tools/capture_manual_screenshots.py`, `modern/tools/vt_label_utility.py`
- **Next person must know:** Test layout on the Pi's real screen; the cloud workspace's fonts are narrower. Avoid emoji outside the Basic Multilingual Plane in UI text (Tk on the Pi can't draw them).

### 2026-11-02 09:44 ET — Bob (Claude) — Enforce the development notes on every commit
- **What:** Added a pre-commit hook (.githooks/pre-commit) that blocks commits changing files without a new, complete entry (What, Why, Files) at the top of this Log; modern/tools/log_dev_note.py writes entries in the shared format and does the check; modern/tools/setup_dev_hooks.sh turns the hook on per clone. Added AGENTS.md (rules for people and AI agents) with CLAUDE.md, GEMINI.md and .gemini/styleguide.md pointing to it, so Bob (Claude) and Neil (Gemini) read the rules at the start of every session.
- **Why:** The owner asked that all changes be logged this way automatically, by every contributor.
- **Files:** `.gemini/styleguide.md`, `.githooks/pre-commit`, `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`, `modern/tools/log_dev_note.py`, `modern/tools/setup_dev_hooks.sh`
- **Next person must know:** Run bash modern/tools/setup_dev_hooks.sh once in every clone (the Pi included) or the hook is not active. Emergency bypass: git commit --no-verify. Notes-only and CHANGELOG-only commits are allowed.

### 2026-11-02 09:41 ET — Bob (Claude) — Keep the operator's Update icon; manual title for v1.0
- **What:** removed the wildcard `rm -f /home/pi/Desktop/*label*.desktop …` line that `9f1c09f` added to
  `install_label_utility.sh`. The installer still removes the old label-utility icons by exact name
  (`claude-label-utility.desktop`, `vt-label-utility.desktop`, and their menu entries). Manual updated
  for the new window title "ViewTouch Label Utility v1.0" and rebuilt.
- **Why:** the wildcard also deleted the operator's **Update Label Utility** icon
  (`~/Desktop/update-label-utility.desktop`), which runs this installer — so it deleted itself on first use.
- **Files:** `modern/tools/install_label_utility.sh`, `modern/docs/label-utility/build_manual.py`,
  `modern/docs/label-utility/ViewTouch_Label_Utility_User_Manual.pdf`, `modern/vt-modern-development-notes.md`.
- **Commit:** this commit (see `git log -- modern/tools/install_label_utility.sh`).
- **Next person must know:** don't delete desktop files by wildcard. `update-label-utility.desktop` /
  `update-label-utility.sh` on the Pi desktop are the operator's own update icon (pull, install, sync,
  screenshots) and must stay. Only the label utility itself has no desktop icon.

### 2026-11-02 09:40 ET — Neil (Gemini) — Label Utility v1.0 release & desktop shortcut removal

- **What**: Merged `claude-label-utility` into `main`, stamped `__version__ = "1.0"` and window title `ViewTouch Label Utility v1.0`, tagged `label-utility-v1.0`, and removed Pi desktop `.desktop` shortcuts so the utility launches exclusively from inside ViewTouch (`/usr/local/bin/vt-label-utility`).
- **Why**: Unify both branches on the single official 1.0 release (`modern/tools/vt_label_utility.py`) while keeping the Pi desktop clean per operator preference.
- **Files**: `modern/tools/vt_label_utility.py`, `modern/tools/install_label_utility.sh`, `modern/tools/LABEL_UTILITY_NOTES.md`, `modern/vt-modern-development-notes.md`.
- **Commit**: `9f1c09fd` (tagged `label-utility-v1.0`)
- **Next person must know**:
  - Do not recreate `/home/pi/Desktop/*label*.desktop` shortcuts; the operator launches the Label Utility exclusively from ViewTouch (`Manager -> Label Utility` -> `/usr/local/bin/vt-label-utility`).
  - Always pull before editing `modern/vt-modern-development-notes.md`, prepend new entries newest-first under the Log section using `### YYYY-MM-DD HH:MM ET — <Agent> — <short title>`, and never hand-edit `CHANGELOG.md` (generated by `tools/changelog.py`).

### 2026-11-02 09:29 ET — Bob (Claude) — Start the development notes
- **What:** created this file with time-stamped entries for all of Bob (Claude)'s changes so far (from the
  commit times). From now on every change gets an entry here. Pointer added to `LABEL_UTILITY_NOTES.md`.
- **Files:** `modern/vt-modern-development-notes.md`, `modern/tools/LABEL_UTILITY_NOTES.md`.

### 2026-11-02 09:26 ET — Bob (Claude) — Fix the label utility installer
- **What:** `install_label_utility.sh` no longer creates desktop or menu icons, and removes old ones
  (`claude-label-utility.desktop`, `vt-label-utility.desktop`). The label utility is opened from
  ViewTouch: Manager screen > **Label Utility**.
- **Why:** Neil (Gemini)'s commit `772ddbc` (09:12) removed the `DESKTOP_FILE`/`MENU_FILE` definitions but kept
  the lines writing to them, so the installer stopped with `mkdir: cannot create directory ''`.
- **Files:** `modern/tools/install_label_utility.sh`, `modern/tools/LABEL_UTILITY_NOTES.md`,
  `modern/docs/label-utility/` (manual rebuilt). **Commit:** `fc6bbe8`

### 2026-11-02 09:18 ET — Bob (Claude) — Screenshot script for the manual
- **What:** `capture_manual_screenshots.sh` opens the label utility on the Pi, steps through its
  screens (start, categories, items, preview, label designer and its three toolbars, printer
  settings, print window), saves PNGs to `/tmp/vt_label_shots` and uploads them to the `pi-shots`
  branch. Read-only: never prints or saves templates.
- **Why:** real screenshots for the manual (the cloud workspace can't render the Pi's fonts).
- **Files:** `modern/tools/capture_manual_screenshots.py`, `capture_manual_screenshots.sh`. **Commit:** `32d567b`

### 2026-11-02 09:09 ET — Bob (Claude) — Merge Neil (Gemini)'s beta retirement
- **What:** merged `71443a6` (Neil (Gemini) deleted `beta_label_utility.py`); removed remaining beta
  mentions from the notes and manual.
- **Files:** `modern/tools/LABEL_UTILITY_NOTES.md`, `modern/docs/label-utility/`. **Commit:** `35f33b2`

### 2026-11-02 09:05 ET — Bob (Claude) — Label utility user manual
- **What:** 15-page PDF manual (how it works, every screen and button, printing, printer settings,
  label designer, creating and importing labels, templates, updating, troubleshooting, quick reference)
  plus its source and sample label images.
- **Files:** `modern/docs/label-utility/ViewTouch_Label_Utility_User_Manual.pdf`, `build_manual.py`,
  `img/`. Rebuild: `cd modern/docs/label-utility && python3 build_manual.py ViewTouch_Label_Utility_User_Manual.pdf`.
  **Commit:** `942c496`

### 2026-11-02 09:01 ET — Bob (Claude) — Claude Label Utility becomes `vt_label_utility.py`; `--upc` support
- **What:** `modern/tools/vt_label_utility.py` now contains the Claude Label Utility, titled
  "ViewTouch Label Utility". ViewTouch's references are unchanged: `openLabelUtility` →
  `/usr/local/bin/vt-label-utility` → `vt_label_utility.py`, and the idle-logout `pgrep` check.
  The utility now honours `--upc <code>` (sent by `openLabelUtility:<UPC>`) and opens straight to that
  item's preview (matches primary, alternate or carton UPC). `claude_label_utility.py` removed (merged).
  New installer `install_label_utility.sh` (old name forwards to it). Log is `/tmp/vt_label_utility.log`.
  Added `LABEL_UTILITY_NOTES.md` (design rules for anyone editing the label utility).
- **Files:** `modern/tools/vt_label_utility.py`, `install_label_utility.sh`,
  `install_claude_label_utility.sh`, `push_label_log.sh`, `LABEL_UTILITY_NOTES.md`. **Commit:** `0958845`

### 2026-11-02 08:43 ET — Bob (Claude) — Import ILSA `.rtm` labels
- **What:** **Lookup Template** imports ILSA ReportBuilder `.rtm` label layouts (size, positions, fonts,
  alignment, barcode type, bar width, digits under the bars) as templates; asks before replacing.
- **Files:** `modern/tools/claude_label_utility.py` (now `vt_label_utility.py`). **Commit:** `1d10852`

### 2026-11-02 08:37 ET — Bob (Claude) — Choose any of an item's UPCs for a label
- **What:** collects every code on a ViewTouch item (barcodes list + `__parent__:` carton UPC). Items
  screen: **Print UPC** (Primary / Alt UPC 1–3 / Carton UPC), **Next UPC**, "UPC on Label" and
  "Other UPCs" columns; search matches any code. Print screen: **UPC** drop-down. Read-only — nothing
  is written to `viewtouch.db` (the owner does not want alt UPCs promoted to primary).
- **Commit:** `c8366e4`

### 2026-11-02 08:18 ET — Bob (Claude) — Remove dotted line on buttons
- **What:** rounded buttons' top highlight was a white `gray25` stipple (a row of dots); now a solid
  lighter shade of the button colour. **Commit:** `d77779d`

### 2026-11-01 16:49 ET — Bob (Claude) — Templates follow their printer by name
- **What:** if a template's saved printer is no longer listed (e.g. its IP changed in ViewTouch), the
  same printer name is used instead of silently falling back to the first printer. Cigar template set
  to "Stick Labels" (192.168.1.25), price label to "Shelf Labels". **Commit:** `5fbdce0`

### 2026-11-01 16:40 ET — Bob (Claude) — Send ZPL to label printers regardless of name
- **What:** printers named "Shelf Labels"/"Stick Labels" were sent ESC/POS receipt commands and printed
  nothing. Label printers now get ZPL unless the name says receipt/Epson/kitchen; a template may force
  `"printer_language": "zpl"|"escpos"`. **Commit:** `3e86ad3`

### 2026-11-01 16:26 ET — Bob (Claude) — Sharp print preview
- **What:** preview re-drawn at screen resolution with smooth text; double-click toggles the exact
  printer-dot view. Printed output unchanged. **Commit:** `c74d28c`

### 2026-11-01 16:15 ET — Bob (Claude) — `push_label_log.sh`
- **What:** uploads the label utility log, the last label sent (`/tmp/last_label.zpl`) and the templates
  to the `pi-logs` branch for remote diagnosis, without touching working files. **Commit:** `7ef66c2`

### 2026-11-01 16:11 ET — Bob (Claude) — ILSA label templates and ILSA-style barcodes
- **What:** the label utility ships only RECEIVING LABEL plus the three ILSA layouts (2X1 VINYL UPC_PRICE,
  2X1 VINYL UPC_DESC, CIGAR STICK LABEL UPC_DESC) converted exactly from the `.rtm` files. Own template
  file `~/.local/share/ViewTouch/claude_label_templates.json` (no merging of defaults, deletions stick;
  keeps the existing RECEIVING LABEL tweaks). Barcodes: fixed ILSA bar width, alignment, digits under the
  bars; top-anchored text; Arial / Arial Black / Courier New via fontconfig (`resolve_font_file`,
  `pdf_font_name_for` were referenced but missing). Installer adds Microsoft core fonts.
- **Files:** `modern/tools/claude_label_utility.py`, `install_claude_label_utility.sh`,
  `modern/seed/templates/claude_label_templates.json`. **Commit:** `76c84f2`

### 2026-11-01 15:41 ET — Bob (Claude) — Claude Label Utility (bitmap label rendering)
- **What:** new label program based on `beta_label_utility.py`: every label (text and barcodes) is one
  1-bit picture at the printer's DPI, sent as ZPL `^GFA` or an image PDF — no printer fonts or printer
  barcodes. Barcode bars in whole printer dots (UPC-A, EAN-13, UPC-E, Code 39, Code 128; correct check
  digits). Same picture for Zebra, CUPS and PDF. Exact print image in the preview. Rotation applied to
  printed labels. Printer resolution setting (203/300/600 dpi). Desktop launcher installer.
- **Files:** `modern/tools/claude_label_utility.py`, `install_claude_label_utility.sh`. **Commit:** `18d0996`
