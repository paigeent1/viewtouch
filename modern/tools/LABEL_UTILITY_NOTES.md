# ViewTouch Label Utility — notes for anyone (person or AI) changing it

**`vt_label_utility.py` is the one label program.** It replaced the earlier Gemini version on 2026-11-02
(that version is in Git history). `claude_label_utility.py` was merged into it and removed.
`beta_label_utility.py` is the older experimental copy — **do not build new features there**; it lacks the
fixes below.

## How ViewTouch launches it (keep these names)
- Manager page button **"Label Utility"** (`seed/pages/manager.json`) runs command **`openLabelUtility`**
  (also `openLabelUtility:<UPC>` or a `upc` argument).
- `ui/cpp/layoutcontroller.cpp` runs **`/usr/local/bin/vt-label-utility`** (from `scripts/bin/vt-label-utility`),
  adding **`--upc <code>`** when a UPC is given. The utility opens straight to that item's label preview.
- The idle-logout check looks for a running **`vt_label_utility.py`** (`pgrep -f`). Don't rename the file.
- `scripts/bin/vt-tmp` backs the file up to Google Drive.

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
4. **Templates live in `~/.local/share/ViewTouch/claude_label_templates.json`** (seeded once from
   `modern/seed/templates/claude_label_templates.json`). No merging of built-in defaults or other utilities'
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

## Current templates (from the ILSA system)
RECEIVING LABEL · 2X1 VINYL UPC_PRICE (printer "Shelf Labels") · 2X1 VINYL UPC_DESC ("Zebra Large Labels") ·
CIGAR STICK LABEL UPC_DESC ("Stick Labels", 192.168.1.25). ILSA `.rtm` files import via **Lookup Template**
(`import_rtm_template`).

## Files
- `vt_label_utility.py` — the program · `install_label_utility.sh` — installer (launcher, desktop icon, fonts)
- `push_label_log.sh` — uploads `/tmp/vt_label_utility.log`, `/tmp/last_label.zpl` and the templates to the
  `pi-logs` branch for remote diagnosis
