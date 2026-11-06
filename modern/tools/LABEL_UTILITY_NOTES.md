# ViewTouch Label Utility v1.1 — Architecture & Maintenance Notes

**`vt_label_utility.py` is the one label program.** It replaced the earlier Gemini version on 2026-11-02
(that version is in Git history). `claude_label_utility.py` was merged into it, and `beta_label_utility.py`
was retired — both are in Git history only. Make all label changes in `vt_label_utility.py`.


## Versions
The version is `__version__` at the top of `vt_label_utility.py` (shown in the window title). When a release
changes what the operator sees or does, raise it, add a line here, and tag the commit `label-utility-vX.Y`.
- **1.2** (2026-11-06): only managers change templates (Lookup/Rename/Delete/Restore, Edit/Create Report; others get a
  manager-PIN box); Rename Template on screen 1; Save As… in the designer; Test Print… in the designer (one label of
  the unsaved edits, printer chosen first, via `send_label_job`; nothing saved); Quick Label on screen 1 (everyone).

## Quick Label (2026-11-06)
- Screen 1's **Quick Label** button (`open_quick_label`): 1–3 typed lines, label roll + printer + copies, Enter prints.
- Built-in template `QUICK LABEL` made in memory each time (`build_quick_label_template`): one full-width band per
  line, `static_label`, bold, centred, `auto_font`. Never written to label_templates.json, so it can't be deleted.
- Label rolls come from the saved templates (`quick_label_rolls`, one per size + printer, the 2x1 Zebra at
  192.168.1.24 first); size, dpi, offsets, darkness and printer language are copied from that template.
- Prints through `send_label_job` like every label (one bitmap, ^GFA / PDF). Only the last roll (template name) and
  printer are kept, in `~/.local/share/ViewTouch/quick_label.json`; the typed text is never stored or logged.
- UI text: no emoji outside the BMP (🖨 🔒 🏷 removed again 2026-11-06; Tk on the Pi shows boxes).
- **1.1** (2026-11-02): Delete Template and Restore Standard Labels on screen 1; windows and dialogs fit the
  Pi's 1360x768 screen; no missing-emoji boxes; `update_label_utility.sh`; manual with Pi screenshots.
- **1.0** (2026-11-02, tag at `9f1c09f`): bitmap-only printing, ILSA templates, any UPC per label, `--upc`,
  launched only from ViewTouch.

## How ViewTouch launches it (keep these names)
- Manager page button **"Label Utility"** (`seed/pages/manager.json`) runs command **`openLabelUtility`**
  (also `openLabelUtility:<UPC>` or a `upc` argument).
- `ui/cpp/layoutcontroller.cpp` runs **`/usr/local/bin/vt-label-utility`** (from `scripts/bin/vt-label-utility`),
  adding **`--upc <code>`** when a UPC is given. The utility opens straight to that item's label preview.
- The idle-logout check looks for a running **`vt_label_utility.py`** or **`vt-label-utility`** (`pgrep -f 'vt[_-]label[_-]utility'`). Don't rename the file.
- **`/usr/local/bin/vt-label-utility` must stay the 5-line launcher** from `scripts/bin/vt-label-utility` (it runs the repo copy). Never copy the whole script there: ViewTouch would run that stale copy and Git updates would never show (happened 2026-11-02).
- The file's history is in Git (`git log -- modern/tools/vt_label_utility.py`); the old `vt-tmp` Drive backup was retired 2026-11-04.

## Design rules (please keep)
1. **Every label is one 1-bit picture** rendered by `render_label_pil_image()` at the printer's DPI and sent
   as ZPL `^GFA` (Zebra) or an image PDF (CUPS / PDF Preview). **Never** use printer fonts (`^A`) or printer
   barcodes (`^BC`, `^BU`) — the user requires printing that doesn't rely on printer interpretation (like ILSA).
2. **Barcode bars are whole printer dots** (`draw_barcode_dots`, `barcode_modules`): UPC-A, UPC-E (incl.
   UPC-A⇄UPC-E conversion), EAN-13, Code 39, Code 128. Check digits are always recalculated. Never draw bars
   at fractional widths.
3. **Read-only on viewtouch.db.** ViewTouch keeps the menu in memory and rewrites `menu_items` from memory, so
   direct writes get lost or overwritten. The user does **not** want alt UPCs promoted to primary.
   Choosing an alt/carton UPC is per print, in memory only (`collect_item_upcs`, `set_item_upc`).
4. **Templates live in `~/.local/share/ViewTouch/label_templates.json`** (seeded once from
   `modern/seed/templates/label_templates.json`; until 2026-11-02 the file was `claude_label_templates.json`,
   which is read once, copied over and kept as a backup). No merging of built-in defaults or other utilities'
   templates; deleted templates must stay deleted. Don't overwrite the user's templates on start-up.
5. **Printer language:** network/CUPS printers get ZPL unless the name says receipt/Epson/kitchen
   (`printer_speaks_zpl`); a template may set `"printer_language": "zpl"|"escpos"`. Templates find their
   printer by ViewTouch printer *name* (`resolve_template_printer`) so IP changes in ViewTouch carry over.
6. **Printer resolution** per template: `"printer_dpi"` (203/300/600). Offsets are in printer dots.

## Template element keys (beyond the basics x/y/w/h/field/size/bold/align)
- `font_family`: Sans | Serif | Mono | Arial | Arial Black | Courier New (resolved via fontconfig, `resolve_font_file`)
- `auto_font` (bool), `word_wrap` (bool), `rotate` (0/90/180/270 — applied to the printed picture)
- `valign: "top"` — ReportBuilder-style top-anchored text (used by ILSA imports)
- Barcodes: `module_mm` (fixed bar width, ILSA `mmBarWidth`), `human_readable` (digits under bars),
  `hri_font`, `hri_size`, `align` (left/center/right; bars may extend past the box like ReportBuilder)

## Who may change templates (2026-11-06, owner's decision)
- Everyone picks templates and prints. Only managers import, rename, delete, restore, edit (Edit Report) or create
  (Create Report / designer Save As…) templates. Printer Offset (position/darkness/dpi) stays open to everyone.
- Role: `VT_LABEL_ROLE=manager|staff`, set by `layoutcontroller.cpp` when ViewTouch opens the utility (`pos_->can("manager")`,
  the manager screens' test); else line 2 of `/tmp/vt_active_employee.txt` (line 1 = name, written at each login by
  `PosService::loginWithPin`). Unknown role counts as not a manager.
- Not a manager: the buttons show "(manager)" and open a manager-PIN box (`ask_manager_pin`) that allows that one action.
  The PIN is checked read-only against viewtouch.db `employees` (SHA-256 of pinSalt + PIN, as `hashPin`; manager = role
  manager/admin + allow − deny). Never store or log PINs. If it can't be checked, the action is refused.
- Templates are referred to by name only in `label_templates.json` itself and live by ViewTouch's Labels screen
  (`pos_inventory.cpp labelTemplates()`, re-read each time), so a rename needs nothing else updated.

## Current templates (from the ILSA system)
RECEIVING LABEL · 2X1 VINYL UPC_PRICE (printer "Shelf Labels") · 2X1 VINYL UPC_DESC ("Zebra Large Labels") ·
CIGAR STICK LABEL UPC_DESC ("Stick Labels", 192.168.1.25). ILSA `.rtm` files import via **Lookup Template**
(`import_rtm_template`).

## Files
- `vt_label_utility.py` — the program · `install_label_utility.sh` — installer (ViewTouch launcher, Python libraries, fonts; no desktop icon — open it from ViewTouch)
- `capture_manual_screenshots.sh` — screenshots the screens for the manual (uploads to `pi-shots`)
- `push_label_log.sh` — uploads `/tmp/vt_label_utility.log`, `/tmp/last_label.zpl` and the templates to the
  `pi-logs` branch for remote diagnosis

## User manual
`modern/docs/label-utility/ViewTouch_Label_Utility_User_Manual.pdf` (source: `build_manual.py` in the same folder).
If you change buttons, screens or behaviour, update and rebuild the manual too.

## Development notes
Record every change, time-stamped, at the top of `modern/vt-modern-development-notes.md` (newest first).

## Used by ViewTouch's Labels screen (tablets)
- `vt-label-utility --print-job job.json` prints with no window: `{"template", "items": [{"upc", "qty"}], "qty"}`, in the items' order.
- `vt-label-utility --preview-job job.json --out label.png` draws one label exactly as it prints: `{"template", "upc"}` (exit 3: UPC not found).
- Both look items up in the same list the window uses: ViewTouch's items once it has 1,000+ retail items (the ILSA price book imported), else the ILSA temp price book while it exists.

## Barcodes print the code exactly as stored (2026-11-03)
- Labels are scanned back in (receiving, the register), so the barcode always gives the same number the price book / ViewTouch has.
- A valid UPC-A (12), EAN-13 (13) or UPC-E (8) prints as that symbol; anything else (11 digits, a wrong check digit, letters) prints as Code 128 of the exact code. No check digit is ever added or corrected (`gs1_symbol_kind`, `barcode_modules`). Code 39 fields stay Code 39.
- There is no per-item barcode type in ViewTouch or the ILSA price book; the code decides.
