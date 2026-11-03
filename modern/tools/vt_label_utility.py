#!/usr/bin/env python3
__version__ = "1.1"
import re
"""
ViewTouch Label Utility - label printing with full bitmap rendering.
Launched by ViewTouch's "Label Utility" button (command openLabelUtility -> /usr/local/bin/vt-label-utility),
optionally with --upc <code> to open straight to that item's label preview.
Every label (text and barcodes) is drawn as one picture at the printer's DPI and
sent as a graphic, so the printer never interprets fonts or barcodes.
Based on the ViewTouch Label Utility & Report Designer (Sandbox Edition)
Reads 100% from /home/pi/.local/share/ViewTouch/ViewTouch/viewtouch.db (Read-Only)
Includes interactive 8-handle resizable field boxes (W x H + Font Size) in the Report Designer.
"""
import sys, logging
from logging.handlers import RotatingFileHandler
import tkinter as tk

_log_handler = RotatingFileHandler('/tmp/vt_label_utility.log', maxBytes=102400, backupCount=1)
logging.basicConfig(level=logging.ERROR, handlers=[_log_handler], format='%(asctime)s [%(levelname)s] %(message)s')

def _log_err(exc, val, tb):
    logging.error("Exception caught", exc_info=(exc, val, tb))
    sys.__excepthook__(exc, val, tb)

sys.excepthook = _log_err
tk.Tk.report_callback_exception = lambda self, exc, val, tb: _log_err(exc, val, tb)
# A short trail of what was touched (Next / Back and which screen opened), so a button that seems to do
# nothing can be followed in /tmp/vt_label_utility.log even when no error happens.
_trace = logging.getLogger("vt.trace")
_trace.setLevel(logging.INFO)

import datetime, glob, json, os, sqlite3, subprocess, tkinter as tk
from tkinter import ttk, messagebox
from reportlab.pdfgen import canvas
from reportlab.lib.units import inch
from reportlab.graphics.barcode import code128

SANDBOX_DIR = os.path.expanduser("~/.local/share/ViewTouch")
TEMPLATE_FILE = os.path.join(SANDBOX_DIR, "label_templates.json")
# The same file under its name before 2026-11-02; read once, copied to TEMPLATE_FILE, left as a backup.
OLD_TEMPLATE_FILE = os.path.join(SANDBOX_DIR, "claude_label_templates.json")
# Starting templates shipped in the repo (copied to TEMPLATE_FILE on first run)
SEED_TEMPLATE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "seed", "templates", "label_templates.json")
VT_DB_PATH = os.path.expanduser("~/.local/share/ViewTouch/ViewTouch/viewtouch.db")

DEFAULT_TEMPLATES = {
    "2X1VINYL W/BARCODE": {
        "width_in": 2.0,
        "height_in": 1.0,
        "elements": [
            {"id": "mfg",     "field": "manufacturer", "x": 0.04, "y": 0.03, "w": 0.62, "h": 0.14, "size": 9,  "bold": False, "align": "left"},
            {"id": "upc_txt", "field": "upc",          "x": 0.58, "y": 0.03, "w": 0.84, "h": 0.14, "size": 10, "bold": True,  "align": "center"},
            {"id": "date",    "field": "date_yymmdd",  "x": 1.44, "y": 0.03, "w": 0.52, "h": 0.14, "size": 9,  "bold": True,  "align": "right"},
            {"id": "barcode", "field": "barcode_img",  "x": 0.22, "y": 0.18, "w": 1.38, "h": 0.22, "size": 10, "bold": False, "align": "center"},
            {"id": "code26",  "field": "year_short",   "x": 1.62, "y": 0.20, "w": 0.32, "h": 0.18, "size": 12, "bold": True,  "align": "right"},
            {"id": "price",   "field": "price_fmt",    "x": 0.15, "y": 0.42, "w": 1.70, "h": 0.34, "size": 26, "bold": True,  "align": "center"},
            {"id": "desc",    "field": "name",         "x": 0.06, "y": 0.77, "w": 1.88, "h": 0.19, "size": 10, "bold": True,  "align": "center"}
        ]
    },
    "DYMO LABEL BAND": {
        "width_in": 2.5,
        "height_in": 0.75,
        "elements": [
            {"id": "mfg",     "field": "manufacturer", "x": 0.06, "y": 0.04, "w": 0.80, "h": 0.14, "size": 8,  "bold": False, "align": "left"},
            {"id": "barcode", "field": "barcode_img",  "x": 0.20, "y": 0.18, "w": 1.25, "h": 0.24, "size": 10, "bold": False, "align": "center"},
            {"id": "price",   "field": "price_fmt",    "x": 1.50, "y": 0.14, "w": 0.90, "h": 0.30, "size": 16, "bold": True,  "align": "center"},
            {"id": "desc",    "field": "name",         "x": 0.08, "y": 0.50, "w": 2.34, "h": 0.20, "size": 8,  "bold": True,  "align": "center"}
        ]
    },
    "3X2 VINYL SHELF": {
        "width_in": 3.0,
        "height_in": 2.0,
        "elements": [
            {"id": "mfg",     "field": "manufacturer", "x": 0.10, "y": 0.08, "w": 1.30, "h": 0.22, "size": 12, "bold": True,  "align": "left"},
            {"id": "date",    "field": "date_yymmdd",  "x": 1.90, "y": 0.08, "w": 1.00, "h": 0.22, "size": 10, "bold": False, "align": "right"},
            {"id": "barcode", "field": "barcode_img",  "x": 0.50, "y": 0.34, "w": 2.00, "h": 0.36, "size": 10, "bold": False, "align": "center"},
            {"id": "price",   "field": "price_fmt",    "x": 0.25, "y": 0.80, "w": 2.50, "h": 0.60, "size": 38, "bold": True,  "align": "center"},
            {"id": "desc",    "field": "name",         "x": 0.12, "y": 1.52, "w": 2.76, "h": 0.34, "size": 13, "bold": True,  "align": "center"}
        ]
    },
    "2X1VINYL W/BARCODE DBL CIGAR": {
        "width_in": 2.0,
        "height_in": 1.0,
        "elements": [
            {"id": "mfg",     "field": "manufacturer", "x": 0.05, "y": 0.04, "w": 0.65, "h": 0.14, "size": 8,  "bold": True,  "align": "left"},
            {"id": "upc_txt", "field": "upc",          "x": 0.65, "y": 0.04, "w": 0.90, "h": 0.14, "size": 9,  "bold": True,  "align": "center"},
            {"id": "barcode", "field": "barcode_img",  "x": 0.25, "y": 0.18, "w": 1.50, "h": 0.22, "size": 10, "bold": False, "align": "center"},
            {"id": "price",   "field": "price_fmt",    "x": 0.20, "y": 0.43, "w": 1.60, "h": 0.30, "size": 22, "bold": True,  "align": "center"},
            {"id": "desc",    "field": "name",         "x": 0.06, "y": 0.76, "w": 1.88, "h": 0.19, "size": 9,  "bold": True,  "align": "center"}
        ]
    },
    "1.5X1VINYL PR LABEL": {
        "width_in": 1.5,
        "height_in": 1.0,
        "elements": [
            {"id": "mfg",   "field": "manufacturer", "x": 0.08, "y": 0.06, "w": 1.34, "h": 0.16, "size": 8,  "bold": False, "align": "center"},
            {"id": "price", "field": "price_fmt",    "x": 0.10, "y": 0.28, "w": 1.30, "h": 0.38, "size": 24, "bold": True,  "align": "center"},
            {"id": "desc",  "field": "name",         "x": 0.06, "y": 0.72, "w": 1.38, "h": 0.20, "size": 8,  "bold": True,  "align": "center"}
        ]
    }
}

def ensure_box_dims(tpl):
    """Guarantees every element has explicit x, y (top-left) and w, h (box size in inches)."""
    for el in tpl.get("elements", []):
        if "w" not in el:
            el["w"] = 1.35 if el.get("field") == "barcode_img" else 1.20
        if "h" not in el:
            el["h"] = 0.22 if el.get("field") == "barcode_img" else max(0.14, float(el.get("size", 10)) / 55.0)
    return tpl

_RECEIPT_PRINTER_WORDS = ("epson", "tm-t", "tmt", "tm-m", "receipt", "escpos", "esc/pos", "star ", "tsp1",
                          "80mm", "58mm", "kitchen", "ticket", "bar printer", "report")

def printer_speaks_zpl(printer_label, tpl=None, extra_text=""):
    """Should this printer get ZPL (label printer) or ESC/POS (receipt printer)?

    A template can force it with "printer_language": "zpl" or "escpos". Otherwise this is a label
    utility, so everything is treated as a ZPL label printer unless its name (or CUPS device URI)
    clearly says it is a receipt printer.
    """
    lang = str((tpl or {}).get("printer_language", "")).lower()
    if lang in ("zpl", "escpos"):
        return lang == "zpl"
    text = (str(printer_label) + " " + str(extra_text)).lower()
    if any(k in text for k in ("zebra", "zpl", "label")):
        return True
    return not any(k in text for k in _RECEIPT_PRINTER_WORDS)

def _printer_display_name(label):
    m = re.match(r"\s*(?:VT TCP|VT CUPS|VT File|CUPS):\s*(.*?)(?:\s*\(|$)", str(label or ""))
    return (m.group(1) if m else str(label or "")).strip().lower()

def resolve_template_printer(saved, printers):
    """Find the template's printer in the current list.

    Exact match first; then the same ViewTouch printer name (so changing a printer's IP in
    ViewTouch settings carries over to the templates using it); then the first printer.
    """
    if saved and saved in printers:
        return saved
    want = _printer_display_name(saved)
    if want:
        for p in printers:
            if _printer_display_name(p) == want:
                return p
    return printers[0] if printers else "PDF Preview (Desktop)"

def discover_all_printers():
    printers = []
    seen = set()
    try:
        con = sqlite3.connect(f"file:{VT_DB_PATH}?mode=ro", uri=True, timeout=1.5)
        cols = [r[1] for r in con.execute("PRAGMA table_info(settings)").fetchall()]
        val_col = cols[1] if len(cols) > 1 else "json"
        for (raw,) in con.execute(f'SELECT "{val_col}" FROM settings').fetchall():
            data = json.loads(raw)
            for p in data.get("printers", []):
                name = p.get("name") or p.get("id") or "VT Printer"
                ptype = str(p.get("type") or p.get("format") or "network").lower()
                host = p.get("host") or p.get("target") or p.get("address") or ""
                port = p.get("port") or 9100
                cups_q = p.get("cupsQueue") or p.get("queue") or ""
                fname = p.get("fileName") or p.get("device") or ""
                if ptype == "cups" and cups_q:
                    label = f"VT CUPS: {name} ({cups_q})"
                elif host:
                    label = f"VT TCP: {name} ({host}:{port}) [{ptype}]"
                elif fname:
                    label = f"VT File: {name} ({fname})"
                else:
                    continue
                if label not in seen:
                    seen.add(label); printers.append(label)
        con.close()
    except Exception:
        pass
    for dev in sorted(glob.glob("/dev/usb/lp*")):
        lbl = f"USB Direct ({dev})"
        if lbl not in seen:
            seen.add(lbl); printers.append(lbl)
    try:
        out = subprocess.check_output(["lpstat", "-a"], text=True, stderr=subprocess.DEVNULL, timeout=2.0)
        for line in out.splitlines():
            if line.strip():
                q = line.split()[0]
                lbl = f"CUPS: {q}"
                if lbl not in seen:
                    seen.add(lbl); printers.append(lbl)
    except Exception:
        pass
    printers.append("PDF Preview (Desktop)")
    return printers

def _unused_old_discover_all_printers():
    printers = []
    seen = set()
    # 1. Read configured printers directly from ViewTouch (viewtouch.db -> settings)
    try:
        con = sqlite3.connect(f"file:{VT_DB_PATH}?mode=ro", uri=True, timeout=1.5)
        cols = [r[1] for r in con.execute("PRAGMA table_info(settings)").fetchall()]
        val_col = cols[1] if len(cols) > 1 else "json"
        for (raw,) in con.execute(f'SELECT "{val_col}" FROM settings').fetchall():
            data = json.loads(raw)
            for p in data.get("printers", []):
                name = p.get("name") or p.get("id") or "VT Printer"
                target = p.get("target") or p.get("address") or p.get("host") or p.get("device") or ""
                fmt = p.get("format") or p.get("type") or ""
                if target:
                    label = f"VT: {name} ({target})" + (f" [{fmt}]" if fmt else "")
                    if label not in seen:
                        seen.add(label); printers.append(label)
        con.close()
    except Exception:
        pass
    # 2. Check direct USB label/thermal printer nodes (/dev/usb/lp*)
    for dev in sorted(glob.glob("/dev/usb/lp*")):
        lbl = f"USB Direct ({dev})"
        if lbl not in seen:
            seen.add(lbl); printers.append(lbl)
    # 3. Check Linux CUPS queues (lpstat -a)
    try:
        out = subprocess.check_output(["lpstat", "-a"], text=True, stderr=subprocess.DEVNULL, timeout=2.0)
        for line in out.splitlines():
            if line.strip():
                q = line.split()[0]
                lbl = f"CUPS: {q}"
                if lbl not in seen:
                    seen.add(lbl); printers.append(lbl)
    except Exception:
        pass
    printers.append("PDF Preview (Desktop)")
    return printers

_FONT_CACHE = {}

def resolve_font_file(family, bold=False, italic=False):
    """Find a TrueType file for a named font (e.g. 'Arial Black', 'Courier New') via fontconfig.
    Falls back to the closest installed font (Liberation Sans/Mono are metric clones of Arial/Courier New)."""
    key = (family, bool(bold), bool(italic))
    if key in _FONT_CACHE:
        return _FONT_CACHE[key]
    path = None
    # Metric-compatible / closest stand-ins when the Microsoft fonts aren't installed
    fallbacks = {"arial": ("Liberation Sans", bold), "arial black": ("Liberation Sans", True),
                 "courier new": ("Liberation Mono", bold), "times new roman": ("Liberation Serif", bold)}
    tries = [(family, bold)]
    if family.lower() in fallbacks:
        tries.append(fallbacks[family.lower()])
    for fam_try, bold_try in tries:
        pattern = fam_try + (":weight=bold" if bold_try else "") + (":slant=italic" if italic else "")
        try:
            out = subprocess.run(["fc-match", "-f", "%{family}|%{file}", pattern], capture_output=True, text=True, timeout=3).stdout.strip()
            got_fam, _, got_file = out.partition("|")
            # fc-match always returns *something*; only accept it if it is really the family we asked for
            if got_file and os.path.exists(got_file) and fam_try.lower() in got_fam.lower():
                path = got_file
                break
        except Exception:
            pass
    _FONT_CACHE[key] = path
    return path

def pdf_font_name_for(family, bold=False, italic=False):
    """Register a named TrueType font with reportlab and return its PDF font name (or None)."""
    path = resolve_font_file(family, bold, italic)
    if not path:
        return None
    try:
        from reportlab.pdfbase import pdfmetrics
        from reportlab.pdfbase.ttfonts import TTFont
        name = "VT_" + "".join(ch for ch in os.path.basename(path) if ch.isalnum())
        if name not in pdfmetrics.getRegisteredFontNames():
            pdfmetrics.registerFont(TTFont(name, path))
        return name
    except Exception:
        return None

# ---------------------------------------------------------------------------
# ILSA / ReportBuilder (.rtm) label import
# ---------------------------------------------------------------------------
_RTM_FIELDS = {"PLU_DESC": "name", "POS_DESC": "name", "PLU_NUM": "upc", "PRICE": "price_fmt", "LAST_PRICE": "price_fmt",
               "MANUF": "manufacturer", "CATEGORY": "category", "DEPT_GRP": "category"}
_RTM_BARCODES = {"bcUPC_A": "barcode_upca", "bcUPC_E": "barcode_upce", "bcCode39": "barcode_code39",
                 "bcCode128": "barcode_img", "bcEAN_13": "barcode_img"}
_RTM_ALIGN = {"taCentered": "center", "taCenter": "center", "taRightJustified": "right", "taLeftJustified": "left"}

def _parse_rtm(text):
    root = {"props": {}, "children": []}
    stack = [root]
    for raw in text.splitlines():
        line = raw.strip()
        m = re.match(r"object (\w+): (\w+)", line)
        if m:
            node = {"name": m.group(1), "cls": m.group(2), "props": {}, "children": []}
            stack[-1]["children"].append(node); stack.append(node); continue
        if line == "end" and len(stack) > 1:
            stack.pop(); continue
        m = re.match(r"([\w.]+) = (.*)", line)
        if m:
            v = m.group(2).strip()
            if v.startswith("'") and v.endswith("'"):
                v = v[1:-1].replace("''", "'")
            stack[-1]["props"][m.group(1)] = v
    if not root["children"]:
        raise ValueError("not a ReportBuilder .rtm text file")
    return root["children"][0]

def import_rtm_template(path):
    """Convert an ILSA ReportBuilder label layout (.rtm, ASCII format) into a template dict."""
    with open(path, "r", encoding="latin-1") as f:
        rep_ = _parse_rtm(f.read())
    P = rep_["props"]
    mm_in = lambda v: round(int(v) / 1000.0 / 25.4, 4)
    tpl = {"width_in": mm_in(P.get("PrinterSetup.mmPaperWidth", 50800)),
           "height_in": mm_in(P.get("PrinterSetup.mmPaperHeight", 25400)),
           "elements": [], "v_offset_dots": 0, "h_offset_dots": 0, "darkness": 18, "tear_off": 0, "printer_dpi": 203,
           "source": "ILSA ReportBuilder: " + os.path.basename(path)}
    bands = [c for c in rep_["children"] if c["cls"] == "TppDetailBand"]
    if not bands:
        raise ValueError("no detail band (label layout) found in the .rtm file")
    for o in bands[0]["children"]:
        p = o["props"]
        if "mmLeft" not in p:
            continue
        fam = p.get("Font.Name", "Arial"); style = p.get("Font.Style", "[]"); size = int(p.get("Font.Size", 8))
        el = {"id": o["name"], "x": mm_in(p["mmLeft"]), "y": mm_in(p["mmTop"]), "w": mm_in(p["mmWidth"]), "h": mm_in(p["mmHeight"])}
        el["w"] = round(max(0.05, min(el["w"], tpl["width_in"] - el["x"])), 4)
        if o["cls"] == "TppDBBarCode":
            el.update(field=_RTM_BARCODES.get(p.get("BarCodeType"), "barcode_img"), size=size, bold=False,
                      align=_RTM_ALIGN.get(p.get("Alignment", "taLeftJustified"), "left"),
                      module_mm=int(p.get("mmBarWidth", 330)) / 1000.0,
                      human_readable=p.get("PrintHumanReadable", "True") != "False", hri_font=fam, hri_size=size)
        elif o["cls"] in ("TppDBText", "TppSystemVariable", "TppVariable", "TppLabel"):
            if o["cls"] == "TppDBText":
                field = _RTM_FIELDS.get(p.get("DataField", "").upper(), p.get("DataField", "name").lower())
            elif o["cls"] == "TppLabel":
                field = "static:" + p.get("Caption", "")
            else:
                field = {"yymmdd": "date_yymmdd", "yy": "year_short", "mm/dd/yyyy": "date_slash"}.get(p.get("DisplayFormat", ""), "date_yymmdd")
            tall_enough = int(p["mmHeight"]) / 1000.0 > size * 0.3528 * 1.15 * 1.8
            el.update(field=field, size=size, bold="fsBold" in style, italic="fsItalic" in style, font_family=fam,
                      auto_font=False, valign="top", align=_RTM_ALIGN.get(p.get("TextAlignment", "taLeftJustified"), "left"),
                      word_wrap=p.get("WordWrap") == "True" and tall_enough)
        else:
            continue
        tpl["elements"].append(el)
    if not tpl["elements"]:
        raise ValueError("no printable fields found in the .rtm file")
    return tpl

def load_templates():
    """The label utility keeps its own template list (label_templates.json).

    First run: copies the store's templates from their old file name, else the shipped seed file, to
    ~/.local/share/ViewTouch/label_templates.json.
    After that only that file is used, so deleted templates stay deleted.
    """
    data = {}
    for src in (TEMPLATE_FILE, OLD_TEMPLATE_FILE, SEED_TEMPLATE_FILE):
        if os.path.exists(src):
            try:
                with open(src, "r", encoding="utf-8") as f:
                    loaded = json.load(f)
                if isinstance(loaded, dict) and loaded:
                    data = loaded
                    if src == OLD_TEMPLATE_FILE:
                        save_templates(data)   # moved to the new file name; the old file stays as a backup
                    if src == SEED_TEMPLATE_FILE:
                        # Keep the RECEIVING LABEL position tweaks already saved by vt_label_utility
                        try:
                            with open(os.path.join(SANDBOX_DIR, "vt_label_templates_v2.json"), "r", encoding="utf-8") as vf:
                                tuned = json.load(vf).get("RECEIVING LABEL")
                            if isinstance(tuned, dict) and tuned.get("elements"):
                                data["RECEIVING LABEL"] = tuned
                        except Exception:
                            pass
                        save_templates(data)
                    break
            except Exception as e:
                logging.error("Could not read templates from %s: %s", src, e)
    if not data:
        data = json.loads(json.dumps(DEFAULT_TEMPLATES))
    for k in data:
        ensure_box_dims(data[k])
    return data

def save_templates(tpls):
    try:
        os.makedirs(os.path.dirname(TEMPLATE_FILE), exist_ok=True)
        tmp = TEMPLATE_FILE + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(tpls, f, indent=2)
        os.replace(tmp, TEMPLATE_FILE)
    except Exception as e:
        logging.error("Could not save templates: %s", e)

def calc_gs1_check_digit(digits_11):
    total = 0
    for idx, ch in enumerate(reversed(digits_11)):
        total += int(ch) * (3 if (idx % 2 == 0) else 1)
    return str((10 - (total % 10)) % 10)

def normalize_upc(raw_code, fallback=""):
    clean = "".join(ch for ch in str(raw_code).strip() if ch.isdigit())
    if len(clean) == 11:
        clean += calc_gs1_check_digit(clean)
    return clean if clean else str(raw_code or fallback or "000000000000").strip()

def infer_brand(item_name, category):
    words = item_name.strip().split()
    if not words:
        return category.upper()
    for prefix in (
        "ALEC BRADLEY", "AJ FERNANDEZ", "CASA FERNANDEZ", "BAHAMA MAMA",
        "ARMANDO RAMOS", "BUENA VISTA", "CROWNED HEADS", "ROMEO Y",
        "MONTECRISTO", "ARTURO FUENTE", "DREW ESTATE", "LA AROMA",
        "MY FATHER", "OLIVA", "PADRON", "PERDOMO", "ROCKY PATEL", "MACANUDO"
    ):
        if item_name.upper().startswith(prefix):
            return prefix
    return words[0].upper()

def _clean_code(code):
    c = str(code or "").strip()
    if not c or c.lower() in ("0", "none", "null"):
        return ""
    d = "".join(ch for ch in c if ch.isdigit())
    if d and all(ch.isdigit() or ch in " -" for ch in c):
        return normalize_upc(d) if len(d) == 11 else d     # numeric codes: digits only, 11 digits get a check digit
    return c                                               # other codes print exactly as ViewTouch stores them

def collect_item_upcs(m):
    """All codes on a ViewTouch menu item, for picking which one a label prints.

    Returns [(label, code), ...]: ViewTouch's barcodes list in order ("Primary UPC", "Alt UPC 1", ...),
    then the carton UPC a pack item is linked to (recipe __parent__:<carton UPC>).
    Nothing is ever written back to ViewTouch.
    """
    raw = []
    bcs = m.get("barcodes", [])
    if isinstance(bcs, str):
        bcs = [x for x in re.split(r"[\s,;|]+", bcs) if x]
    if isinstance(bcs, list):
        raw.extend(bcs)
    for k in ("upc", "sku", "altupc"):
        if m.get(k):
            raw.append(m.get(k))
    for k in ("altupcs", "alt_upcs", "altBarcodes"):
        v = m.get(k)
        if isinstance(v, list):
            raw.extend(v)
    choices, seen = [], set()
    same = lambda c: c.lstrip("0") if c.isdigit() else c      # 0025900102005 == 025900102005
    for code in raw:
        c = _clean_code(code)
        if c and same(c) not in seen:
            seen.add(same(c))
            choices.append(("Primary UPC" if not choices else f"Alt UPC {len(choices)}", c))
    for rl in m.get("recipe", []) or []:
        if isinstance(rl, dict):
            ing = str(rl.get("ingredientId", rl.get("ingredient", "")))
            if ing.startswith("__parent__:"):
                c = _clean_code(ing.split(":", 1)[1])
                if c and same(c) not in seen:
                    seen.add(same(c))
                    choices.append(("Carton UPC", c))
    return choices

def set_item_upc(item, code, label=None):
    """Choose which of the item's UPCs this label prints (session only)."""
    item["upc"] = code
    item["upc_label"] = label or next((l for l, c in item.get("upc_choices", []) if c == code), "UPC")

def parse_vt_menu_dict(m, seen, items):
    if not isinstance(m, dict):
        return
    name = str(m.get("name") or "").strip()
    if not name or m.get("active", True) is False:
        return

    bcs = m.get("barcodes", [])
    raw_upc = bcs[0] if isinstance(bcs, list) and len(bcs) > 0 else (m.get("upc") or m.get("sku") or "")
    raw_upce = bcs[1] if isinstance(bcs, list) and len(bcs) > 1 else (m.get("altupc") or "")
    fallback_id = str(m.get("number") or m.get("id") or "")
    upc = normalize_upc(raw_upc, fallback_id)

    p_raw = m.get("price", m.get("priceCents", 0))
    try:
        p_num = float(p_raw or 0)
        price = p_num / 100.0 if isinstance(p_raw, int) or p_num >= 100 else p_num
    except Exception:
        price = 0.0

    cat = str(m.get("family") or m.get("category") or "GENERAL").strip().upper()
    RESTAURANT_CATS = {"BREAKFAST", "BRUNCH", "LUNCH", "DINNER", "APPETIZERS", "ENTREES", "DESSERTS", "SIDES", "SOUPS", "SALADS", "SANDWICHES", "BURGERS", "PIZZA", "PASTA", "KIDS", "DRINKS", "BEVERAGES", "COCKTAILS", "DRAFT BEER", "WINE"}
    if cat in RESTAURANT_CATS or "TOAST" in name.upper() or "PANCAKE" in name.upper() or "OMELET" in name.upper() or "BURGER" in name.upper():
        if "CIGAR" not in cat and "TOBACCO" not in cat and "CIGAR" not in name.upper():
            return
    has_real_bc = bool(raw_upc and len("".join(ch for ch in str(raw_upc) if ch.isdigit())) >= 8)
    is_retail = has_real_bc or (str(m.get("taxClass", "")).lower() == "merchandise") or any(k in cat for k in ("CIGAR", "TOBACCO", "RETAIL", "PIPE", "ACCESS", "HUMIDOR", "LIGHTER", "CUTTER", "VAPE", "SMOKE"))
    pack_qty = 1

    for rl in m.get("recipe", []):
        if isinstance(rl, dict):
            ing = str(rl.get("ingredientId", rl.get("ingredient", "")))
            qty = float(rl.get("quantity", 0) or 0)
            if ing == "__retail__" and qty > 0.5:
                is_retail = True
            elif ing.startswith("__parent__:") and qty > 0:
                pack_qty = int(qty)
                is_retail = True

    mfg = str(m.get("manufacturer") or m.get("brand") or infer_brand(name, cat)).strip().upper()
    dept_grp = "CIGARS" if ("CIGAR" in cat or "CIGAR" in name.upper()) else ("RETAIL" if is_retail else "HOSPITALITY")

    upc_choices = collect_item_upcs(m)
    if not upc_choices or upc_choices[0][1] != upc:
        rest = [c for _, c in upc_choices if c.lstrip("0") != upc.lstrip("0")]
        upc_choices = [("Primary UPC", upc)] + [(f"Alt UPC {i + 1}", c) for i, c in enumerate(rest)]
    key = (name.upper(), upc)
    if key not in seen:
        seen.add(key)
        items.append({
            "selected": False,
            "qty": 1,
            "pack_qty": pack_qty,
            "upc": upc,
            "primary_upc": upc,
            "upc_choices": upc_choices,
            "upc_label": "Primary UPC",
            "upce": str(raw_upce).strip(),
            "name": name.upper(),
            "dept_name": cat,
            "dept_num": str(m.get("number") or "80"),
            "dept_group": dept_grp,
            "category": cat,
            "manufacturer": mfg,
            "price": price,
            "is_retail": is_retail
        })

def load_from_viewtouch_db():

    # --- TEMPORARY ILSA PRICEBOOK HOOK (FOR LABEL UTILITY TRAINING BEFORE JAN 1) ---
    temp_db = os.path.expanduser("~/.local/share/ViewTouch/ilsa_temp_pricebook.db")
    if os.path.exists(temp_db):
        try:
            t_conn = sqlite3.connect(f"file:{temp_db}?mode=ro", uri=True)
            t_cur = t_conn.cursor()
            t_items, t_seen = [], set()
            cols = {r[1] for r in t_cur.execute("PRAGMA table_info(items)")}
            dg_col = "dept_grp" if "dept_grp" in cols else "''"   # Neil's newer price books have the ILSA dept group
            for row in t_cur.execute(f"SELECT upc, name, dept_num, {dg_col}, category, price, cost, mfg, prod_grp, sub_grp, pack_qty, altupc FROM items"):
                u, nm, dn, dg, cat, pr, cs, mf, pg, sg, pq, alts = row
                alt_list = [a.strip() for a in alts.split(";") if a.strip()] if alts else []
                # A manufacturer of GENERAL or a number isn't a brand: let the name decide.
                mf = str(mf or "").strip()
                if mf.upper() == "GENERAL" or mf.isdigit():
                    mf = ""
                # Made like a ViewTouch menu item, so every screen (tree, UPC choice, prices) works the same.
                try:
                    cents = int(round(float(pr or 0) * 100))
                except (TypeError, ValueError):
                    cents = 0
                before = len(t_items)
                parse_vt_menu_dict({"name": nm, "barcodes": [u] + alt_list, "priceCents": cents,
                                    "family": cat or "Retail", "manufacturer": mf, "taxClass": "merchandise",
                                    "number": dn}, t_seen, t_items)
                if len(t_items) == before:
                    continue
                it = t_items[-1]
                it["source"] = "ilsa_temp"
                it["is_retail"] = True
                if dg and str(dg).strip():
                    it["dept_group"] = str(dg).strip().upper()
                it["prod_grp"] = str(pg or "").strip().upper()
                it["sub_grp"] = str(sg or "").strip().upper()
                try:
                    it["cost"] = float(cs or 0)
                except (TypeError, ValueError):
                    it["cost"] = 0.0
                if str(pq or "").isdigit() and int(pq) > 0:
                    it["pack_qty"] = int(pq)
            t_conn.close()
            if t_items:
                return sorted(t_items, key=lambda x: (x["dept_group"], x["category"], x["manufacturer"], x["name"]))
        except Exception as e:
            print(f"Warning reading temp pricebook, falling back to viewtouch.db: {e}")
    # --------------------------------------------------------------------------------
    items = []
    seen = set()
    if os.path.exists(VT_DB_PATH):
        try:
            con = sqlite3.connect(f"file:{VT_DB_PATH}?mode=ro", uri=True, timeout=2.0)
            con.row_factory = sqlite3.Row
            tables = [r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table'").fetchall()]
            if "menu_items" in tables:
                for row in con.execute("SELECT * FROM menu_items").fetchall():
                    row_dict = {k: row[k] for k in row.keys()}
                    parsed_json = False
                    for val in row_dict.values():
                        if isinstance(val, (str, bytes)) and ("{" in str(val)):
                            try:
                                obj = json.loads(val.decode("utf-8") if isinstance(val, bytes) else val)
                                if isinstance(obj, dict):
                                    parse_vt_menu_dict(obj, seen, items)
                                    parsed_json = True
                                elif isinstance(obj, list):
                                    for sub in obj:
                                        parse_vt_menu_dict(sub, seen, items)
                                    parsed_json = True
                            except Exception:
                                pass
                    if not parsed_json and "name" in row_dict:
                        parse_vt_menu_dict(row_dict, seen, items)
            con.close()
        except Exception as e:
            print("Note reading viewtouch.db:", e)
    return sorted(items, key=lambda x: (x["category"], x["manufacturer"], x["name"]))

GLOBAL_FIELD_CATALOG = [
    ("static_label", "Label (Static Text)"),
    ("variable_text", "Variable (Prompt / Custom Text)"),
    ("date_yymmdd", "System Variable — Date (YYMMDD)"),
    ("date_slash", "System Variable — Date (MM/DD/YYYY)"),
    ("year_short", "System Variable — Year (YY)"),
    ("emp_initials", "System Variable — Employee (F LAST)"),
    ("name", "DBMemo / DBText — Description (Item Name)"),
    ("price_fmt", "DBText — Retail Price ($0.00)"),
    ("manufacturer", "DBText — Manufacturer / Brand"),
    ("category", "DBText — Category / Department"),
    ("upc", "DBText — UPC / PLU Number"),
    ("db_calc", "DBCalc — Pack / Box Calculation"),
    ("barcode_img", "DBBarCode — Code 128"),
    ("barcode_upca", "DBBarCode — UPC-A 12-Digit"),
    ("barcode_upce", "DBBarCode — UPC-E Short"),
    ("barcode_code39", "DBBarCode — Code 39"),
]
GLOBAL_FIELD_KEYS = [k for k, _ in GLOBAL_FIELD_CATALOG]
GLOBAL_FIELD_LABELS = [f"{k} — {lbl}" for k, lbl in GLOBAL_FIELD_CATALOG]

MEDIA_STOCK_PRESETS = [
    ("2.0 x 1.0 in — Vinyl / Barcode", 2.0, 1.0, "vinyl"),
    ("2.5 x 0.75 in — Dymo Cigar Band", 2.5, 0.75, "band"),
    ("3.0 x 2.0 in — Vinyl Shelf Tag", 3.0, 2.0, "shelf"),
    ("1.5 x 1.0 in — Small Price Label", 1.5, 1.0, "small"),
    ("2.25 x 1.25 in — Roll Label", 2.25, 1.25, "roll"),
    ("Custom Size (W x H in)", None, None, "custom"),
]
MEDIA_STOCK_LABELS = [m[0] for m in MEDIA_STOCK_PRESETS]

GLOBAL_VAR_TEXT = "RECEIVED"

def _fmt_flast(full_name, first="", last=""):
    fn = str(first or "").strip()
    ln = str(last or "").strip()
    if fn and ln:
        return f"{fn[0].upper()} {ln.upper()}"
    parts = [w for w in str(full_name or fn or ln).strip().split() if w]
    if len(parts) >= 2:
        return f"{parts[0][0].upper()} {parts[-1].upper()}"
    return parts[0].upper() if parts else "M MANAGER"

def get_logged_in_initials(default="M MANAGER"):
    try:
        for p in ("/tmp/vt_active_employee.txt", os.path.expanduser("~/.local/share/ViewTouch/active_employee.txt")):
            if os.path.exists(p):
                val = _fmt_flast(open(p, encoding="utf-8").read().strip())
                if val: return val
        con = sqlite3.connect(f"file:{VT_DB_PATH}?mode=ro", uri=True, timeout=1.5)
        for (raw,) in con.execute("SELECT json FROM employees").fetchall():
            d = json.loads(raw)
            if d.get("active", True) and "maintainer" not in str(d.get("id", "")).lower():
                con.close()
                return _fmt_flast(d.get("name", ""), d.get("firstName", ""), d.get("lastName", ""))
        con.close()
    except Exception:
        pass
    return default

def format_item_field(item, field_name, el=None):
    now = datetime.datetime.now()
    if str(field_name).startswith("static:"):
        return str(field_name).split(":", 1)[1]
    if field_name == "static_label":
        return (el.get("text") if isinstance(el, dict) else None) or "TEXT"
    if field_name == "variable_text":
        return (el.get("text") if isinstance(el, dict) else None) or item.get("variable_text") or GLOBAL_VAR_TEXT
    if field_name == "db_calc":
        if isinstance(el, dict) and el.get("text"):
            return str(el.get("text"))
        pk = int(item.get("pack_qty", 1) or 1)
        pr = float(item.get("price", 0) or 0)
        return f"PK:{pk} (${pr/max(1,pk):.2f}/EA)" if pk > 1 else f"${pr:.2f}"
    if field_name == "received_const":
        return "RECEIVED"
    if field_name == "emp_initials":
        return get_logged_in_initials("M ROSSI") or "M ROSSI"
    if field_name == "date_slash":
        return now.strftime("%m/%d/%Y")
    if field_name == "manufacturer":
        return item.get("manufacturer", "")
    if field_name == "upc":
        return item.get("upc", "")
    if field_name == "date_yymmdd":
        return now.strftime("%y%m%d")
    if field_name == "year_short":
        return now.strftime("%y")
    if field_name == "price_fmt":
        return f"${float(item.get('price', 0)):.2f}"
    if field_name == "name":
        return item.get("name", "")
    if field_name == "category":
        return item.get("category", "")
    return str(item.get(field_name, ""))

def split_two_lines(text):
    words = str(text or "").strip().split()
    if len(words) <= 1:
        return str(text or "").strip()
    best_i, best_diff = 1, 9999
    for i in range(1, len(words)):
        l1 = " ".join(words[:i])
        l2 = " ".join(words[i:])
        diff = abs(len(l1) - len(l2))
        if diff < best_diff:
            best_diff = diff
            best_i = i
    return " ".join(words[:best_i]) + "\n" + " ".join(words[best_i:])

# ---------------------------------------------------------------------------
# Bitmap barcode engine
# Every label is rendered to a 1-bit picture and the printer only stamps the
# pixels (like the old ILSA system). Barcodes are drawn here, bar by bar, with
# every bar a whole number of printer dots wide so they scan reliably.
# ---------------------------------------------------------------------------

_EAN_L = {'0':'0001101','1':'0011001','2':'0010011','3':'0111101','4':'0100011',
          '5':'0110001','6':'0101111','7':'0111011','8':'0110111','9':'0001011'}
_EAN_R = {'0':'1110010','1':'1100110','2':'1101100','3':'1000010','4':'1011100',
          '5':'1001110','6':'1010000','7':'1000100','8':'1001000','9':'1110100'}
_EAN_G = {d: p[::-1] for d, p in _EAN_R.items()}
_EAN13_PARITY = ['LLLLLL','LLGLGG','LLGGLG','LLGGGL','LGLLGG','LGGLLG','LGGGLL','LGLGLG','LGLGGL','LGGLGL']
# UPC-E parity for number system 0 (E = even/G-code, O = odd/L-code); NS 1 is the inverse
_UPCE_PARITY_NS0 = ['EEEOOO','EEOEOO','EEOOEO','EEOOOE','EOEEOO','EOOEEO','EOOOEE','EOEOEO','EOEOOE','EOOEOE']


def _digits(s):
    return "".join(ch for ch in str(s or "") if ch.isdigit())


def _check_digit_mod10(body):
    """GS1 check digit for UPC-A (11 digits), EAN-13 (12 digits) or UPC-A expanded from UPC-E."""
    total = 0
    for idx, ch in enumerate(reversed(body)):
        total += int(ch) * (3 if idx % 2 == 0 else 1)
    return str((10 - total % 10) % 10)


def upce_to_upca(upce8_or_7):
    """Expand UPC-E (NS + 6 digits [+ check]) to the 11-digit UPC-A body."""
    d = _digits(upce8_or_7)
    ns, m = d[0], d[1:7]
    last = m[5]
    if last in "012":
        body = m[0:2] + last + "0000" + m[2:5]
    elif last == "3":
        body = m[0:3] + "00000" + m[3:5]
    elif last == "4":
        body = m[0:4] + "00000" + m[4]
    else:
        body = m[0:5] + "0000" + last
    return ns + body


def upca_to_upce(upca):
    """Return the 8-digit UPC-E for a UPC-A if it can be zero-suppressed, else None."""
    d = _digits(upca)
    if len(d) == 11:
        d += _check_digit_mod10(d)
    if len(d) != 12 or d[0] not in "01":
        return None
    ns, mfr, prod, chk = d[0], d[1:6], d[6:11], d[11]
    cand = None
    if mfr[2] in "012" and mfr[3:5] == "00" and prod[0:2] == "00":
        cand = mfr[0:2] + prod[2:5] + mfr[2]
    elif mfr[3:5] == "00" and prod[0:3] == "000":
        cand = mfr[0:3] + prod[3:5] + "3"
    elif mfr[4] == "0" and prod[0:4] == "0000":
        cand = mfr[0:4] + prod[4] + "4"
    elif prod[0:4] == "0000" and prod[4] in "56789":
        cand = mfr[0:5] + prod[4]
    if cand and upce_to_upca(ns + cand) == d[:11]:
        return ns + cand + chk
    return None


def _modules_upca(value):
    d = _digits(value)
    if len(d) == 11:
        d += _check_digit_mod10(d)
    if len(d) != 12:
        return None
    d = d[:11] + _check_digit_mod10(d[:11])          # always print a correct check digit
    return ("101" + "".join(_EAN_L[c] for c in d[:6]) + "01010" +
            "".join(_EAN_R[c] for c in d[6:]) + "101"), d


def _modules_ean13(value):
    d = _digits(value)
    if len(d) == 12:
        d += _check_digit_mod10(d)
    if len(d) != 13:
        return None
    d = d[:12] + _check_digit_mod10(d[:12])
    par = _EAN13_PARITY[int(d[0])]
    left = "".join((_EAN_L if p == "L" else _EAN_G)[c] for p, c in zip(par, d[1:7]))
    return "101" + left + "01010" + "".join(_EAN_R[c] for c in d[7:]) + "101", d


def _modules_upce(value, fallback_upca=""):
    d = _digits(value)
    if len(d) == 6:
        d = "0" + d
    if len(d) == 7 and d[0] in "01":
        d += _check_digit_mod10(upce_to_upca(d))
    if not (len(d) == 8 and d[0] in "01"):
        d = upca_to_upce(fallback_upca) or ""
    if len(d) != 8:
        return None
    d = d[:7] + _check_digit_mod10(upce_to_upca(d[:7]))
    par = _UPCE_PARITY_NS0[int(d[7])]
    if d[0] == "1":
        par = "".join("O" if p == "E" else "E" for p in par)
    body = "".join((_EAN_L if p == "O" else _EAN_G)[c] for p, c in zip(par, d[1:7]))
    return "101" + body + "010101", d


def _modules_from_reportlab(kind, value):
    """Code 128 / Code 39 element widths from reportlab, converted to a 1/0 module string."""
    if kind == "code39":
        from reportlab.graphics.barcode import code39 as rl39
        bc = rl39.Standard39(str(value).upper(), checksum=0, stop=1)
        bc.validate(); bc.encode()
        out = []
        for ch in bc.decompose():           # b/s narrow, B/S wide (3x), i = gap
            if ch == "i":
                out.append("0")
            else:
                out.append(("1" if ch in "bB" else "0") * (3 if ch.isupper() else 1))
        return "".join(out), str(value).upper()
    from reportlab.graphics.barcode import code128 as rl128
    bc = rl128.Code128(str(value))
    bc.validate(); bc.encode()
    out, is_bar = [], True
    for ch in bc.decompose():               # alternating bar/space, widths a..d = 1..4
        out.append(("1" if is_bar else "0") * (ord(ch.lower()) - ord("a") + 1))
        is_bar = not is_bar
    return "".join(out), str(value)


def barcode_modules(field, item):
    """Pick the symbology for a barcode field and return (module_string, encoded_value) or (None, raw)."""
    raw = str(item.get("upc", "") or "").strip()
    digits = _digits(raw)
    try:
        if field == "barcode_upce":
            # Follow the UPC picked for this label; otherwise the item's own UPC-E
            picked = item.get("upc_label") not in (None, "Primary UPC")
            res = _modules_upce(raw if picked else item.get("upce", ""), fallback_upca=raw)
            if res:
                return res
            field = "barcode_upca"
        if field == "barcode_upca" and len(digits) in (11, 12):
            return _modules_upca(digits)
        if field == "barcode_code39" and raw:
            return _modules_from_reportlab("code39", raw)
        # An 8-digit UPC-E stored in the UPC column (common on cigarettes) with a valid check digit
        if len(digits) == len(raw) == 8 and digits[0] in "01" and \
                _check_digit_mod10(upce_to_upca(digits[:7])) == digits[7]:
            if field == "barcode_upca":
                return _modules_upca(upce_to_upca(digits[:7]))
            if field in ("barcode_img", "barcode"):
                return _modules_upce(digits)
        if field in ("barcode_img", "barcode_upca", "barcode_ean13", "barcode"):
            if len(digits) == len(raw) and len(digits) in (11, 12):
                return _modules_upca(digits)
            if len(digits) == len(raw) and len(digits) == 13:
                return _modules_ean13(digits)
        if raw:
            return _modules_from_reportlab("code128", raw)
    except Exception:
        pass
    return None, raw


def draw_barcode_dots(draw, modules, x, y, w, h, fill=0, mw=None, align="center"):
    """Draw bars on a PIL canvas using a whole number of dots per module.
    mw = dots per module (default: the widest that fits the box); align = left/center/right in the box."""
    n = len(modules)
    if not n:
        return 0
    mw = max(1, int(mw)) if mw else max(1, int(w // n))
    total = mw * n
    if align == "left":
        x0 = int(x)
    elif align == "right":
        x0 = int(x + w - total)
    else:
        x0 = int(x + (w - total) // 2)
    i = 0
    while i < n:
        if modules[i] == "1":
            j = i
            while j < n and modules[j] == "1":
                j += 1
            draw.rectangle([x0 + i * mw, int(y), x0 + j * mw - 1, int(y + h - 1)], fill=fill)
            i = j
        else:
            i += 1
    return mw



def render_label_pil_image(tpl, item, dpi=203, mono=True, dot_scale=1.0):
    """Render the whole label to a 1-bit PIL image at the printer's own DPI.

    This picture is exactly what gets printed: the Zebra receives it as a ZPL
    graphic (^GFA), CUPS printers receive it inside a PDF page, and the
    print preview shows it. No printer fonts or printer barcodes are used.
    """
    from PIL import Image, ImageDraw, ImageFont
    dpi = int(dpi or 203)
    w_dots = max(50, int(round(float(tpl.get("width_in", 2.0)) * dpi)))
    h_dots = max(40, int(round(float(tpl.get("height_in", 1.0)) * dpi)))
    img = Image.new("L", (w_dots, h_dots), 255)

    # Offsets are stored in printer dots; dot_scale converts them when rendering at another DPI (screen preview)
    v_off = int(round(int(tpl.get("v_offset_dots", 12) if "v_offset_dots" in tpl else 12) * dot_scale))
    h_off = int(round(int(tpl.get("h_offset_dots", 0) or 0) * dot_scale))

    def get_ttf(fam, bold, italic, px):
        px = max(8, int(px))
        candidates = []
        if fam not in ("Sans", "Serif", "Mono"):
            _p = resolve_font_file(fam, bold, italic)
            if _p:
                try:
                    return ImageFont.truetype(_p, px)
                except Exception:
                    pass
        if fam == "Mono":
            candidates = [
                "/usr/share/fonts/truetype/msttcorefonts/courbd.ttf" if bold else "/usr/share/fonts/truetype/msttcorefonts/cour.ttf",
                "/usr/share/fonts/truetype/msttcorefonts/Courier_New_Bold.ttf" if bold else "/usr/share/fonts/truetype/msttcorefonts/Courier_New.ttf",
                "/home/pi/viewtouch-main/modern/assets/fonts/LiberationMono-Bold.ttf" if bold else "/home/pi/viewtouch-main/modern/assets/fonts/LiberationMono-Regular.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
            ]
        elif fam == "Serif":
            candidates = [
                "/home/pi/viewtouch-main/modern/assets/fonts/LiberationSerif-Bold.ttf" if bold else "/home/pi/viewtouch-main/modern/assets/fonts/LiberationSerif-Regular.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
            ]
        else:
            candidates = [
                "/usr/share/fonts/truetype/msttcorefonts/Ariblk.ttf" if bold else "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf",
                "/usr/share/fonts/truetype/msttcorefonts/ariblk.ttf" if bold else "/usr/share/fonts/truetype/msttcorefonts/arial.ttf",
                "/home/pi/viewtouch-main/modern/assets/fonts/LiberationSans-Bold.ttf" if bold else "/home/pi/viewtouch-main/modern/assets/fonts/LiberationSans-Regular.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            ]
        for c in candidates:
            if os.path.exists(c):
                try:
                    return ImageFont.truetype(c, px)
                except Exception:
                    pass
        return ImageFont.load_default()

    def draw_text_box(draw, raw_txt, el, field, bw, bh):
        """Fit and draw text inside a (bw x bh) box whose top-left is (0, 0)."""
        fam = el.get("font_family", "Sans")
        bold = bool(el.get("bold", True))
        italic = bool(el.get("italic", False))
        do_wrap = bool(el.get("word_wrap", field == "name"))
        auto_font = bool(el.get("auto_font", True))

        # Same box-fitting rules as render_label_on_tk_canvas
        if do_wrap and len(raw_txt) > 18 and " " in raw_txt:
            lines = split_two_lines(raw_txt).splitlines()
            start_px = max(10, int(bh * 0.44)) if auto_font else max(8, int(float(el.get("size", 12)) * (dpi / 72.0)))
            chosen_fnt = get_ttf(fam, bold, italic, 10)
            for px in range(start_px, 7, -1):
                fnt = get_ttf(fam, bold, italic, px)
                widths = [draw.textbbox((0, 0), ln, font=fnt)[2] - draw.textbbox((0, 0), ln, font=fnt)[0] for ln in lines]
                b0 = draw.textbbox((0, 0), "Ag", font=fnt)
                lh = (b0[3] - b0[1]) * 1.15
                if max(widths) <= (bw - 4) and (lh * len(lines)) <= (bh + 4):
                    chosen_fnt = fnt
                    break
        else:
            lines = [raw_txt]
            start_px = max(10, int(bh * 0.82)) if auto_font else max(8, int(float(el.get("size", 12)) * (dpi / 72.0)))
            chosen_fnt = get_ttf(fam, bold, italic, 10)
            for px in range(start_px, 7, -1):
                fnt = get_ttf(fam, bold, italic, px)
                bb = draw.textbbox((0, 0), raw_txt, font=fnt)
                if el.get("valign") == "top":
                    a_, d_ = fnt.getmetrics()
                    fits = (bb[2] - bb[0]) <= bw and (a_ + d_) <= bh * 1.25
                else:
                    fits = (bb[2] - bb[0]) <= (bw - 4) and (bb[3] - bb[1]) <= (bh - 1)
                if fits:
                    chosen_fnt = fnt
                    break

        align = el.get("align", "center")
        if el.get("valign") == "top":
            # ReportBuilder style: each line's ascent starts at the top of the box (no vertical centring)
            try:
                asc, desc = chosen_fnt.getmetrics()
            except Exception:
                asc, desc = (10, 3)
            cur_y = 0
            for ln in lines:
                lb = draw.textbbox((0, 0), ln, font=chosen_fnt, anchor="la")
                lw_px = lb[2] - lb[0]
                if align == "left":
                    tx = -lb[0]
                elif align == "right":
                    tx = bw - lw_px - lb[0]
                else:
                    tx = (bw - lw_px) / 2.0 - lb[0]
                draw.text((tx, cur_y), ln, font=chosen_fnt, fill=0, anchor="la")
                cur_y += asc + desc
            return
        line_bboxes = [draw.textbbox((0, 0), ln, font=chosen_fnt) for ln in lines]
        line_heights = [bb[3] - bb[1] for bb in line_bboxes]
        total_h = sum(line_heights) + (4 * (len(lines) - 1))
        cur_y = (bh - total_h) / 2.0
        for ln, bb, lh in zip(lines, line_bboxes, line_heights):
            lw_px = bb[2] - bb[0]
            if align == "left":
                tx = 2 - bb[0]
            elif align == "right":
                tx = bw - lw_px - 2 - bb[0]
            else:
                tx = (bw - lw_px) / 2.0 - bb[0]
            draw.text((tx, cur_y - bb[1]), ln, font=chosen_fnt, fill=0)
            cur_y += lh + 4

    for el in tpl.get("elements", []):
        if el.get("visible", True) is False:
            continue
        bx = int(round(float(el.get("x", 0.04)) * dpi)) + h_off
        by = int(round(float(el.get("y", 0.04)) * dpi)) + v_off
        bw = max(16, int(round(float(el.get("w", 0.8)) * dpi)))
        bh = max(12, int(round(float(el.get("h", 0.18)) * dpi)))
        field = el.get("field", "name")
        rot = int(el.get("rotate", 0) or 0) % 360
        # A 90/270 rotated element is laid out in a box with width and height swapped
        eff_w, eff_h = (bh, bw) if rot in (90, 270) else (bw, bh)

        if str(field).startswith("barcode"):
            modules, b_val = barcode_modules(field, item)
            if not modules:
                continue
            n_mod = len(modules)
            # Fixed bar width (ILSA "mmBarWidth") or the widest whole-dot width that fits the box
            if el.get("module_mm"):
                mw = max(1, int(round(float(el["module_mm"]) * dpi / 25.4)))
                if rot:
                    mw = min(mw, max(1, eff_w // n_mod))
            else:
                mw = max(1, eff_w // n_mod)
            b_align = el.get("align", "center")
            need_w = mw * n_mod
            sub_w = max(eff_w, need_w)
            if need_w > eff_w and not rot:
                # Bars wider than the box spill out like they did in ReportBuilder
                if b_align == "center":
                    bx -= (need_w - eff_w) // 2
                elif b_align == "right":
                    bx -= (need_w - eff_w)
            sub = Image.new("L", (sub_w, eff_h), 255)
            sdraw = ImageDraw.Draw(sub)
            bar_h = eff_h
            hri_font = None
            if el.get("human_readable"):
                hri_px = max(8, int(round(float(el.get("hri_size", 8)) * dpi / 72.0)))
                hri_font = get_ttf(el.get("hri_font", "Sans"), False, False, hri_px)
                hb = sdraw.textbbox((0, 0), "0123456789", font=hri_font)
                bar_h = max(6, eff_h - (hb[3] - hb[1]) - 2)
            x0 = 0 if sub_w > eff_w else ({"left": 0, "right": eff_w - need_w}.get(b_align, (eff_w - need_w) // 2))
            draw_barcode_dots(sdraw, modules, x0, 0, need_w, bar_h, mw=mw, align="left")
            if hri_font is not None:
                txt = str(b_val)
                tb = sdraw.textbbox((0, 0), txt, font=hri_font)
                sdraw.text((x0 + (need_w - (tb[2] - tb[0])) / 2.0 - tb[0], bar_h + 1 - tb[1]), txt, font=hri_font, fill=0)
        else:
            sub = Image.new("L", (eff_w, eff_h), 255)
            sdraw = ImageDraw.Draw(sub)
            raw_txt = str(format_item_field(item, field, el) or "").strip()
            if not raw_txt:
                continue
            draw_text_box(sdraw, raw_txt, el, field, eff_w, eff_h)

        if rot:
            sub = sub.rotate(rot, expand=True, fillcolor=255)
        # Paste only the dark pixels so overlapping boxes don't blank each other out
        if mono:
            mask = sub.point(lambda p: 255 if p < 160 else 0)
        else:
            mask = sub.point(lambda p: 255 - p)       # keep smooth (anti-aliased) edges for the screen
        img.paste(0, (bx, by), mask)

    if not mono:
        return img
    return img.point(lambda p: 0 if p < 160 else 1, mode="1")


def label_dpi(tpl):
    return int(tpl.get("printer_dpi", 203) or 203)


def export_pdf_from_bitmaps(items, tpl, out_pdf, copies=1):
    """PDF for CUPS/desktop printers: every page is the same 1-bit label picture the Zebra gets."""
    from reportlab.lib.utils import ImageReader
    w_pt = float(tpl.get("width_in", 2.0)) * inch
    h_pt = float(tpl.get("height_in", 1.0)) * inch
    dpi = label_dpi(tpl)
    c = canvas.Canvas(out_pdf, pagesize=(w_pt, h_pt))
    first = True
    for item in items:
        img = render_label_pil_image(tpl, item, dpi=dpi).convert("L")
        reader = ImageReader(img)
        n = max(1, int(item.get("qty", 1))) * max(1, int(copies))
        for _ in range(n):
            if not first:
                c.showPage()
            first = False
            c.drawImage(reader, 0, 0, width=w_pt, height=h_pt)
    c.save()


def label_preview_photo(tpl, item, max_w, max_h, printer_dots=False):
    """Tk PhotoImage of the label for the screen.

    Default: the same layout re-drawn at the screen's own resolution with smooth text, so it is
    as sharp as the printout. printer_dots=True shows the exact printer bitmap, dot for dot.
    """
    import base64, io
    from PIL import Image
    w_in = float(tpl.get("width_in", 2.0)); h_in = float(tpl.get("height_in", 1.0))
    p_dpi = label_dpi(tpl)
    if printer_dots:
        img = render_label_pil_image(tpl, item, dpi=p_dpi).convert("L")
        scale = min(max_w / float(img.width), max_h / float(img.height))
        if scale >= 1:
            scale = max(1, int(scale))           # whole-number zoom keeps every dot identical
        nw, nh = max(1, int(img.width * scale)), max(1, int(img.height * scale))
        img = img.resize((nw, nh), Image.NEAREST)
    else:
        screen_dpi = max(72, int(min(max_w / w_in, max_h / h_in)))
        img = render_label_pil_image(tpl, item, dpi=screen_dpi, mono=False, dot_scale=screen_dpi / float(p_dpi))
        nw, nh = img.width, img.height
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return tk.PhotoImage(data=base64.b64encode(buf.getvalue()).decode("ascii")), nw, nh


def send_label_job(tpl_name, tpl, items, copies, chosen):
    """Render items with tpl and send copies of each to the printer `chosen` (as listed by discover_all_printers).

    Returns (ok, message, pdf_to_open): pdf_to_open is set for the "PDF" choice (the caller shows it).
    Used by the print window and by headless printing for ViewTouch's Labels screen (--print-job).
    """
    w_in = float(tpl.get("width_in", 2.0))
    h_in = float(tpl.get("height_in", 1.0))
    norm_items = [dict(it, qty=1) for it in items]
    total_lbls = len(norm_items) * copies
    txt_out, esc_bytes = build_escpos_and_txt_from_template(tpl_name, tpl, norm_items, copies=copies)
    zpl_out = build_zpl_from_template(tpl_name, tpl, norm_items, copies=copies)

    open("/tmp/vt_label_job.txt", "w", encoding="utf-8").write(txt_out)
    open("/tmp/last_label.zpl", "w", encoding="utf-8").write(zpl_out)
    out_pdf = "/tmp/VT_Labels.pdf"
    export_pdf_from_bitmaps(norm_items, tpl, out_pdf, copies=copies)

    if "PDF" in chosen:
        return True, f"PDF of {total_lbls} label(s): {out_pdf}", out_pdf

    if chosen.startswith("VT File:"):
        m_f = re.search(r"\(([^)]+)\)", chosen)
        f_target = m_f.group(1) if m_f else "/tmp/vt_label_job.txt"
        if not os.path.isabs(f_target):
            f_target = os.path.join(SANDBOX_DIR, "ViewTouch", f_target)
        open(f_target, "w", encoding="utf-8").write(txt_out)
        return True, f"✓ Wrote {total_lbls} label(s) to {f_target}!", None

    ip_m = re.search(r"\((\d+\.\d+\.\d+\.\d+)(?::(\d+))?\)", chosen)
    if ip_m:
        ip = ip_m.group(1)
        port = int(ip_m.group(2) or 9100)
        import socket
        is_zpl = printer_speaks_zpl(chosen, tpl)
        payload = zpl_out.encode("utf-8") if is_zpl else esc_bytes
        with socket.create_connection((ip, port), timeout=4.0) as s:
            s.sendall(payload)
        return True, f"✓ Sent {total_lbls} label(s) ({w_in}x{h_in} in) to {ip}:{port}!", None

    if chosen.startswith("VT CUPS:"):
        m_q = re.search(r"\(([^)]+)\)", chosen)
        q_name = m_q.group(1).strip() if m_q else chosen.split()[-1]
    else:
        q_name = chosen.replace("CUPS: ", "").split(" (")[0].strip()

    v_out = subprocess.run(["lpstat", "-v", q_name], capture_output=True, text=True, timeout=2.0).stdout.lower()
    is_thermal_q = any(k in q_name.lower() or k in v_out for k in ("tmt", "epson", "receipt", "80mm", "thermal", "zebra", "label", "192.168.1.77", "socket://", "zpl"))
    if is_thermal_q:
        is_zpl_q = printer_speaks_zpl(q_name, tpl, v_out)
        raw_file = "/tmp/last_label.zpl" if is_zpl_q else "/tmp/vt_label_job.escpos"
        if not is_zpl_q:
            open(raw_file, "wb").write(esc_bytes)
        proc = subprocess.run(["lp", "-d", q_name, "-o", "raw", raw_file], capture_output=True, text=True, timeout=6.0)
    else:
        media_opt = f"Custom.{w_in}x{h_in}in"
        proc = subprocess.run(["lp", "-d", q_name, "-o", f"media={media_opt}", "-o", "fit-to-page", out_pdf], capture_output=True, text=True, timeout=6.0)
        if proc.returncode != 0:
            proc = subprocess.run(["lp", "-d", q_name, "-o", "raw", "/tmp/vt_label_job.txt"], capture_output=True, text=True, timeout=6.0)

    if proc.returncode == 0:
        job_info = (proc.stdout or "").strip() or f"Queued on {q_name}"
        return True, f"✓ Sent {total_lbls} label(s) ({w_in}x{h_in} in) to {q_name}!\n({job_info})", None
    err_txt = (proc.stderr or proc.stdout or f"Exit code {proc.returncode}").strip()
    return False, f"CUPS could not print to {q_name}:\n{err_txt}", None


def find_item_by_code(all_items, code):
    """The item one of whose UPCs (primary, alt or carton) is code, and that code as printed; else (None, None)."""
    want = "".join(ch for ch in str(code) if ch.isalnum())
    key = lambda c: c.lstrip("0") if c.isdigit() else c
    for it in all_items:
        for lbl, c in it.get("upc_choices", []) or [("Primary UPC", it.get("upc", ""))]:
            if key(c) == key(want) or key(c) == key(normalize_upc(want)):
                return it, (lbl, c)
    return None, None


def run_print_job(path):
    """Headless printing for ViewTouch's Labels screen (tablets): the job file says which template and,
    per item, which of its UPCs and how many: {"template": "...", "items": [{"upc": "...", "qty": 3}, ...]}.
    A template without product fields (e.g. RECEIVING LABEL) may have no items: {"template": ..., "qty": 5}.
    Prints with the template's own printer, exactly as the print window does. Result in the log."""
    with open(path, "r", encoding="utf-8") as f:
        job = json.load(f)
    templates = load_templates()
    name = str(job.get("template", ""))
    if name not in templates:
        logging.error("Labels screen: template %r not found", name)
        return 2
    tpl = templates[name]
    chosen = resolve_template_printer(tpl.get("default_printer", ""), discover_all_printers())
    all_items = load_from_viewtouch_db()
    wanted = job.get("items") or []
    failures = 0
    if not wanted:
        sample = dict(all_items[0]) if all_items else {"name": "", "upc": "", "price": 0.0}
        wanted = [{"item": sample, "qty": job.get("qty", 1)}]
    for w in wanted:
        it = w.get("item")
        if it is None:
            found, choice = find_item_by_code(all_items, w.get("upc", ""))
            if not found:
                logging.error("Labels screen: UPC %s is not in viewtouch.db", w.get("upc"))
                failures += 1
                continue
            it = dict(found)
            set_item_upc(it, choice[1], choice[0])
        copies = max(1, min(500, int(w.get("qty", 1) or 1)))
        try:
            ok, msg, _pdf = send_label_job(name, tpl, [it], copies, chosen)
        except Exception as e:
            ok, msg = False, f"Print failed: {e}"
        (logging.info if ok else logging.error)("Labels screen: %s x%d (%s): %s", it.get("name", ""), copies, name, msg)
        failures += 0 if ok else 1
    return 1 if failures else 0


def run_preview_job(path, out_png):
    """A picture of one label for ViewTouch's Labels screen (tablets), drawn exactly as it prints:
    {"template": "...", "upc": "..."} (no upc: a label without product fields). Writes out_png."""
    with open(path, "r", encoding="utf-8") as f:
        job = json.load(f)
    templates = load_templates()
    name = str(job.get("template", ""))
    if name not in templates:
        logging.error("Labels screen preview: template %r not found", name)
        return 2
    tpl = templates[name]
    all_items = load_from_viewtouch_db()
    code = str(job.get("upc", "") or "")
    if code:
        found, choice = find_item_by_code(all_items, code)
        if not found:
            logging.error("Labels screen preview: UPC %s not found", code)
            return 3
        it = dict(found)
        set_item_upc(it, choice[1], choice[0])
    else:
        it = dict(all_items[0]) if all_items else {"name": "", "upc": "", "price": 0.0}
    img = render_label_pil_image(tpl, it, dpi=label_dpi(tpl))
    img.convert("L").save(out_png, "PNG", optimize=True)
    return 0


def pil_1bit_to_zpl_gfa(img_1bit):
    """Converts a 1-bit PIL image (0=black, 1=white) into a ZPL ^GFA command."""
    from PIL import ImageOps
    # In PIL '1' mode, 0 is black and 255/1 is white. Invert so black pixels = 1 bits for ZPL ^GFA.
    inv = ImageOps.invert(img_1bit.convert("L")).point(lambda p: 1 if p > 127 else 0, mode="1")
    raw_bytes = inv.tobytes()
    row_bytes = (img_1bit.width + 7) // 8
    total_bytes = len(raw_bytes)
    hex_str = raw_bytes.hex().upper()
    return f"^FO0,0^GFA,{total_bytes},{total_bytes},{row_bytes},{hex_str}^FS"


def build_zpl_from_template(tpl_name, tpl, items, copies=1):
    dpi = label_dpi(tpl)
    w_dots = int(round(float(tpl.get("width_in", 2.0)) * dpi))
    h_dots = int(round(float(tpl.get("height_in", 1.0)) * dpi))

    v_offset_dots = int(tpl.get("v_offset_dots", 12) if "v_offset_dots" in tpl else 12)
    h_offset_dots = int(tpl.get("h_offset_dots", 0) or 0)
    darkness = int(tpl.get("darkness", 18) or 18)
    tear_off = int(tpl.get("tear_off", 0) or 0)

    blocks = []
    if tear_off != 0:
        blocks.append(f"~TA{tear_off:+04d}")
    for item in items:
        qty = max(1, int(item.get("qty", 1))) * max(1, int(copies))
        lines = [
            "^XA",
            f"^PW{w_dots}",
            f"^LL{h_dots}",
            "^LH0,0",
            f"^MD{max(-30, min(30, darkness - 15))}",
            "^CI28"
        ]
        # The whole label (text AND barcodes) is one bitmap; the printer just stamps the dots.
        img_1b = render_label_pil_image(tpl, item, dpi=dpi)
        lines.append(pil_1bit_to_zpl_gfa(img_1b))
        lines.append(f"^PQ{qty},0,1,Y")
        lines.append("^XZ")
        blocks.append("\n".join(lines))
    return "\n".join(blocks) + "\n"


def build_escpos_and_txt_from_template(tpl_name, tpl, items, copies=1):
    """Generates both a clean .txt file (ViewTouch receipt/ticket style) and ESC/POS bytes for port 9100 or CUPS raw."""
    txt_blocks = []
    esc = bytearray(b"\x1b\x40")  # Initialize printer
    for item in items:
        total_q = max(1, int(item.get("qty", 1))) * max(1, int(copies))
        els = [e for e in tpl.get("elements", []) if e.get("visible", True) is not False]
        els.sort(key=lambda e: (round(float(e.get("y", 0)), 1), float(e.get("x", 0))))
        for _ in range(total_q):
            card_lines = []
            esc += b"\x1b\x61\x01"  # Center align
            for el in els:
                f_type = el.get("field", "name")
                if str(f_type).startswith("barcode"):
                    raw_c = "".join(ch for ch in str(item.get("upc", "") or "").strip() if ch.isalnum()) or "000000000000"
                    card_lines.append(f"||{raw_c}||")
                    payload = b"{B" + raw_c.encode("ascii", "ignore")
                    esc += b"\x1d\x68\x3c\x1d\x77\x02\x1d\x48\x02\x1d\x6b\x49" + bytes([len(payload)]) + payload + b"\n"
                else:
                    val = str(format_item_field(item, f_type, el) or "").strip()
                    if not val:
                        continue
                    card_lines.append(val)
                    if f_type == "price_fmt" or int(el.get("size", 10)) >= 18:
                        esc += b"\x1d\x21\x11" + val.encode("utf-8", "ignore") + b"\x1d\x21\x00\n"
                    else:
                        esc += val.encode("utf-8", "ignore") + b"\n"
            txt_blocks.append("\n".join(card_lines) + "\n" + ("-" * 32))
            esc += b"\n\n\n\x1d\x56\x42\x00"  # Feed and partial cut
    full_txt = "\n".join(txt_blocks) + "\n"
    return full_txt, bytes(esc)

def export_pdf_labels(items_to_print, tpl, out_pdf):
    w_pt = float(tpl["width_in"]) * inch
    h_pt = float(tpl["height_in"]) * inch
    c = canvas.Canvas(out_pdf, pagesize=(w_pt, h_pt))
    first = True
    for item in items_to_print:
        qty = max(1, int(item.get("qty", 1)))
        for _ in range(qty):
            if not first:
                c.showPage()
            first = False
            for el in tpl["elements"]:
                if el.get("visible", True) is False:
                    continue
                f_type = el["field"]
                bx = float(el["x"]) * inch
                by_top = float(el["y"]) * inch
                bw = float(el.get("w", 1.2)) * inch
                bh = float(el.get("h", 0.2)) * inch
                by_bottom = h_pt - by_top - bh

                if str(f_type).startswith("barcode"):
                    from reportlab.graphics.barcode import code39, eanbc
                    from reportlab.graphics.shapes import Drawing
                    from reportlab.graphics import renderPDF
                    raw_c = str(item.get("upc", "000000000000") or "000000000000").strip()
                    if f_type == "barcode_code39":
                        bc = code39.Standard39(raw_c.upper(), barHeight=bh*0.85, barWidth=max(0.45, bw/180.0), checksum=0)
                        bc.drawOn(c, max(2, bx + (bw - bc.width)/2.0), max(2, by_bottom))
                    elif f_type in ("barcode_upca", "barcode_upce"):
                        u12 = normalize_upc(raw_c)[:12].zfill(12)
                        w_bc = eanbc.UPCA(u12, barHeight=bh*0.85, barWidth=max(0.55, bw/115.0))
                        d = Drawing(bw, bh); d.add(w_bc); renderPDF.draw(d, c, max(2, bx + (bw - w_bc.width)/2.0), max(2, by_bottom))
                    else:
                        bc = code128.Code128(raw_c, barHeight=bh, barWidth=max(0.45, bw / 160.0))
                        bc.drawOn(c, max(2, bx + (bw - bc.width) / 2.0), max(2, by_bottom))
                else:
                    txt = format_item_field(item, f_type, el)
                    sz = int(el.get("size", 10))
                    ff, bd, it = el.get("font_family", "Sans"), bool(el.get("bold", True)), bool(el.get("italic", False))
                    fmap = {
                        ("Sans",0,0):"Helvetica",("Sans",1,0):"Helvetica-Bold",("Sans",0,1):"Helvetica-Oblique",("Sans",1,1):"Helvetica-BoldOblique",
                        ("Serif",0,0):"Times-Roman",("Serif",1,0):"Times-Bold",("Serif",0,1):"Times-Italic",("Serif",1,1):"Times-BoldItalic",
                        ("Mono",0,0):"Courier",("Mono",1,0):"Courier-Bold",("Mono",0,1):"Courier-Oblique",("Mono",1,1):"Courier-BoldOblique"
                    }
                    font_name = fmap.get((ff, int(bd), int(it))) or pdf_font_name_for(ff, bd, it) or "Helvetica-Bold"
                    c.setFont(font_name, sz)
                    align = el.get("align", "center")
                    do_wrap = bool(el.get("word_wrap", f_type == "name"))
                    if do_wrap and len(txt) > 18 and " " in txt:
                        lines_2 = split_two_lines(txt).splitlines()
                        line_h = sz * 1.05
                        top_line_y = by_bottom + (bh / 2.0) + (line_h * 0.18)
                        for li, ltxt in enumerate(lines_2[:2]):
                            ly = top_line_y - (li * line_h)
                            if align == "center": c.drawCentredString(bx + bw / 2.0, ly, ltxt)
                            elif align == "right": c.drawRightString(bx + bw, ly, ltxt)
                            else: c.drawString(bx, ly, ltxt)
                    else:
                        text_y = by_bottom + max(1, (bh - sz * 0.78) / 2.0)
                        if align == "center": c.drawCentredString(bx + bw / 2.0, text_y, txt)
                        elif align == "right": c.drawRightString(bx + bw, text_y, txt)
                        else: c.drawString(bx, text_y, txt)
    c.save()

def fit_text_into_box(text, box_w_px, box_h_px, max_font_px=None, bold=True, italic=False, family="Sans", allow_wrap=False):
    import tkinter.font as tkfont
    text = str(text or "").strip()
    if not text: return 12, ""
    wt, sl = ("bold" if bold else "normal"), ("italic" if italic else "roman")
    if allow_wrap and len(text) > 18 and " " in text:
        two_line = split_two_lines(text)
        l1, l2 = two_line.splitlines()
        for px in range(max(10, min(90, int(box_h_px * 0.44))), 7, -1):
            f = tkfont.Font(family=family or "Sans", size=-px, weight=wt, slant=sl)
            if max(f.measure(l1), f.measure(l2)) <= (box_w_px - 6) and (f.metrics("linespace") * 2) <= (box_h_px + 4):
                return px, two_line
        return 8, two_line
    for px in range(max(10, min(160, int(box_h_px * 0.78))), 7, -2):
        f = tkfont.Font(family=family or "Sans", size=-px, weight=wt, slant=sl)
        if f.measure(text) <= (box_w_px - 6) and f.metrics("linespace") <= (box_h_px - 2): return px, text
    return 8, text


UPCA_L_CODES = _EAN_L
UPCA_R_CODES = _EAN_R

def draw_barcode_on_tk_canvas(canvas_widget, field, item, x, y, w, h, tag=None, el=None):
    """Screen version of the printed barcode: same symbology and bar pattern as render_label_pil_image."""
    tags = (tag,) if tag else ()
    modules, val = barcode_modules(field, item)
    if modules and isinstance(el, dict) and el.get("human_readable"):
        th = max(8, int(h * 0.3))
        canvas_widget.create_text(x + w // 2, y + h - th // 2, text=str(val), font=(el.get("hri_font", "Sans"), -th), fill="#000000", tags=tags)
        h = max(4, h - th - 1)
    if not modules:
        canvas_widget.create_text(x + w // 2, y + h // 2, text=str(val or "NO BARCODE"), font=("Monospace", -10), fill="#b91c1c", tags=tags)
        return
    mw = float(w) / len(modules)
    i, n = 0, len(modules)
    while i < n:
        if modules[i] == "1":
            j = i
            while j < n and modules[j] == "1":
                j += 1
            canvas_widget.create_rectangle(x + i * mw, y, x + j * mw, y + h, fill="#000000", outline="", tags=tags)
            i = j
        else:
            i += 1

def draw_code128_on_tk_canvas(canvas_widget, value, x, y, w, h, tag=None):
    draw_barcode_on_tk_canvas(canvas_widget, "barcode_img", {"upc": value}, x, y, w, h, tag=tag)

def orig_draw_code128_on_tk_canvas(canvas_widget, value, x, y, w, h, tag=None):
    """Draw a Code 128 barcode as black bars inside the box (x, y, w, h) on a Tk canvas."""
    tags = (tag,) if tag else ()
    value = str(value or "").strip() or "0"
    try:
        pattern = code128.Code128(value).decomposed
        total_units = sum(int(c) for c in pattern)
    except Exception:
        canvas_widget.create_text(x + w // 2, y + h // 2, text=value,
                                  font=("Monospace", -10), fill="#000000", tags=tags)
        return
    quiet = 10
    unit = w / float(total_units + 2 * quiet)
    pos = x + quiet * unit
    for i, c in enumerate(pattern):
        width = int(c) * unit
        if i % 2 == 0:
            canvas_widget.create_rectangle(pos, y, pos + width, y + h,
                                           fill="#000000", outline="", tags=tags)
        pos += width


def render_label_on_tk_canvas(canvas_widget, tpl, item, ox=28, oy=28, scale=420, selected_idx=None, show_boxes=False):
    canvas_widget.delete("all")
    w_px, h_px = int(float(tpl.get("width_in", 2.0)) * scale), int(float(tpl.get("height_in", 1.0)) * scale)
    canvas_widget.create_rectangle(ox+4, oy+4, ox+w_px+4, oy+h_px+4, fill="#0f172a", outline="")
    canvas_widget.create_rectangle(ox, oy, ox+w_px, oy+h_px, fill="#ffffff", outline="#111827", width=2, tags=("bg_card",))
    if show_boxes and ox >= 26 and oy >= 26:
        canvas_widget.create_rectangle(ox, oy-22, ox+w_px, oy-2, fill="#f1f5f9", outline="#94a3b8")
        canvas_widget.create_rectangle(ox-24, oy, ox-2, oy+h_px, fill="#f1f5f9", outline="#94a3b8")
        for s in range(int(round(float(tpl.get("width_in", 2.0)) * 8)) + 1):
            tx, th = int(ox + (s / 8.0) * scale), (10 if s % 8 == 0 else (6 if s % 4 == 0 else 3))
            canvas_widget.create_line(tx, oy-2, tx, oy-2-th, fill="#334155")
            if s % 8 == 0: canvas_widget.create_text(tx+8, oy-12, text=str(s//8)+"in", font=("Sans", 8, "bold"), fill="#0f172a")
        for s in range(int(round(float(tpl.get("height_in", 1.0)) * 8)) + 1):
            ty, tw = int(oy + (s / 8.0) * scale), (10 if s % 8 == 0 else (6 if s % 4 == 0 else 3))
            canvas_widget.create_line(ox-2, ty, ox-2-tw, ty, fill="#334155")
            if s % 8 == 0 and s > 0: canvas_widget.create_text(ox-13, ty, text=str(s//8)+"in", font=("Sans", 8, "bold"), fill="#0f172a")
    sel_set = selected_idx if isinstance(selected_idx, (set, list, tuple)) else ({selected_idx} if selected_idx is not None else set())
    for idx, el in enumerate(tpl.get("elements", [])):
        bx, by = int(ox + float(el.get("x", 0.05)) * scale), int(oy + float(el.get("y", 0.05)) * scale)
        bw, bh = max(24, int(float(el.get("w", 0.8)) * scale)), max(16, int(float(el.get("h", 0.2)) * scale))
        is_sel = (idx in sel_set)
        canvas_widget.create_rectangle(bx, by, bx+bw, by+bh, fill="#ffffff", outline="#2563eb" if is_sel else ("#94a3b8" if show_boxes else ""), width=2 if is_sel else (1 if show_boxes else 0), dash=(4, 2) if (is_sel or show_boxes) else (), tags=(f"el_{idx}",))
        field = el.get("field", "name")
        if str(field).startswith("barcode"):
            draw_barcode_on_tk_canvas(canvas_widget, field, item, bx, by, bw, bh, tag=f"el_{idx}", el=el)
        else:
            raw_txt = format_item_field(item, field, el)
            if not str(raw_txt).strip(): raw_txt = "RECEIVED" if field == "variable_text" else f"[{field}]"
            rot = int(el.get("rotate", 0)) % 360
            fam = {"Sans": "Liberation Sans", "Serif": "Liberation Serif", "Mono": "Liberation Mono"}.get(el.get("font_family", "Sans"), el.get("font_family", "Sans"))
            eff_w, eff_h = (bh, bw) if rot in (90, 270) else (bw, bh)
            do_wrap = bool(el.get("word_wrap", field == "name"))
            fit_px, wrapped = fit_text_into_box(raw_txt, eff_w, eff_h, bold=el.get("bold", True), italic=el.get("italic", False), family=fam, allow_wrap=do_wrap) if el.get("auto_font", True) else (max(8, int(float(el.get("size", 12)) * (scale / 72.0))), split_two_lines(raw_txt) if (do_wrap and len(str(raw_txt)) > 18 and " " in str(raw_txt)) else str(raw_txt))
            align = el.get("align", "center")
            tx = bx + bw // 2 if rot in (90, 270) else (bx + 4 if align == "left" else (bx + bw - 4 if align == "right" else bx + bw // 2))
            anc = "center" if rot in (90, 270) else ("w" if align == "left" else ("e" if align == "right" else "center"))
            sty = ("bold" if el.get("bold", True) else "normal") + (" italic" if el.get("italic", False) else "")
            canvas_widget.create_text(tx, by + bh // 2, text=wrapped, justify=align if align in ("left","center","right") else "center", angle=rot, font=(fam, -fit_px, sty), fill="#000000", anchor=anc, tags=(f"el_{idx}",))
        if is_sel and len(sel_set) == 1:
            for hn, hx, hy in [("nw",bx,by),("n",bx+bw//2,by),("ne",bx+bw,by),("e",bx+bw,by+bh//2),("se",bx+bw,by+bh),("s",bx+bw//2,by+bh),("sw",bx,by+bh),("w",bx,by+bh//2)]:
                canvas_widget.create_rectangle(hx-6, hy-6, hx+6, hy+6, fill="#2563eb", outline="#ffffff", width=1, tags=(f"hdl_{hn}",))

class ReportDesignerWindow(tk.Toplevel):
    def __init__(self, master, tpl_name, tpl_data, sample_item, on_save_callback):
        super().__init__(master)
        self.title(f"Label Utility — Report Designer - [{tpl_name}]")
        go_full_screen(self); self.attributes("-topmost", True); self.lift(); self.focus_force()
        self.configure(bg="#808080")
        self.tpl_name = tpl_name
        self.tpl = ensure_box_dims(json.loads(json.dumps(tpl_data)))
        self._orig_tpl_json = json.dumps(self.tpl, sort_keys=True)
        self.protocol("WM_DELETE_WINDOW", self.on_close_request)
        self.sample_item = sample_item
        self.on_save_callback = on_save_callback
        self.scale = 210.0
        self.selected_idx = 0
        self.selected_set = {0}
        self.undo_stack = []
        self.redo_stack = []
        self.clipboard_els = []
        self.pending_tool = None
        self.interaction = {"mode": None, "start_x": 0, "start_y": 0, "orig_box": None, "undo_pushed": False}
        self._build_ui()
        self._orig_tpl_json = json.dumps(self.tpl, sort_keys=True)
        self.after(80, self._redraw_canvas)
        self._redraw_canvas()

    def _redraw_canvas(self, _e=None):
        self.update_idletasks()
        cw = self.canvas.winfo_width()
        ch = self.canvas.winfo_height()
        if cw < 200 or ch < 150: cw, ch = 1080, 520
        w_in = max(0.5, float(self.tpl.get("width_in", 2.0)))
        h_in = max(0.4, float(self.tpl.get("height_in", 1.0)))
        self.scale = max(80, int(min((cw - 100) / w_in, (ch - 80) / h_in)))
        self.ox = max(36, int((cw - w_in * self.scale) // 2))
        self.oy = max(36, int((ch - h_in * self.scale) // 2))
        sel = (set(getattr(self, "selected_set", set())) | {self.selected_idx}) if self.tpl.get("elements") else set()
        render_label_on_tk_canvas(self.canvas, self.tpl, self.sample_item, ox=self.ox, oy=self.oy, scale=self.scale, selected_idx=sel, show_boxes=True)

    def _push_undo(self):
        snap = json.dumps(self.tpl)
        if not self.undo_stack or self.undo_stack[-1] != snap:
            self.undo_stack.append(snap)
            if len(self.undo_stack) > 50:
                self.undo_stack.pop(0)
        self.redo_stack.clear()

    def undo(self, _e=None):
        if not self.undo_stack:
            self.status_var.set("Nothing to undo.")
            return
        self.redo_stack.append(json.dumps(self.tpl))
        self.tpl = json.loads(self.undo_stack.pop())
        if self.tpl.get("elements"):
            self.selected_idx = min(self.selected_idx, len(self.tpl["elements"]) - 1)
            self.selected_set = {self.selected_idx}
        else:
            self.selected_idx = 0; self.selected_set = set()
        self._refresh_element_combo()
        self._redraw_canvas()
        self.status_var.set("↶ Undid last change.")

    def redo(self, _e=None):
        if not self.redo_stack:
            self.status_var.set("Nothing to redo.")
            return
        self.undo_stack.append(json.dumps(self.tpl))
        self.tpl = json.loads(self.redo_stack.pop())
        if self.tpl.get("elements"):
            self.selected_idx = min(self.selected_idx, len(self.tpl["elements"]) - 1)
            self.selected_set = {self.selected_idx}
        self._refresh_element_combo()
        self._redraw_canvas()
        self.status_var.set("↷ Redid change.")

    def arm_palette_tool(self, field_key, label_name):
        self.pending_tool = (field_key, label_name)
        self.canvas.config(cursor="crosshair")
        self.status_var.set(f"➕ Tool Armed: [{label_name}] — Click anywhere on the white label card to place this component (or press Esc to cancel).")

    def cancel_palette_tool(self, _e=None):
        self.pending_tool = None
        self.canvas.config(cursor="")
        self._load_selected_props()

    def add_field(self):
        dlg = tk.Toplevel(self); dlg.title("Add Field to Label"); dlg.geometry("420x200"); dlg.after_idle(lambda d=dlg: fit_dialog(d, 420, 200)); dlg.configure(bg="#1e242d"); dlg.transient(self); dlg.attributes("-topmost", True)
        tk.Label(dlg, text="Choose Component / Field to Add:", bg="#1e242d", fg="#f2f4f7", font=("Sans", 11, "bold")).pack(pady=(14, 6))
        f_var = tk.StringVar(value=GLOBAL_FIELD_LABELS[0])
        ttk.Combobox(dlg, textvariable=f_var, values=GLOBAL_FIELD_LABELS, state="readonly", width=36, font=("Sans", 10)).pack(pady=4)
        def do_add():
            self._push_undo()
            fk = f_var.get().split(" — ")[0].strip()
            ny = round(min(float(self.tpl.get("height_in", 1.0)) - 0.22, 0.06 + 0.18 * (len(self.tpl["elements"]) % 4)), 3)
            ne = {"id": fk, "field": fk, "x": 0.10, "y": ny, "w": 0.95, "h": 0.20, "size": 12, "bold": True, "align": "center", "auto_font": True, "word_wrap": (fk == "name")}
            if fk in ("variable_text", "static_label"):
                ne["text"] = globals().get("GLOBAL_VAR_TEXT", "RECEIVED") if fk == "variable_text" else "TEXT"
            self.tpl["elements"].append(ne); self.selected_idx = len(self.tpl["elements"]) - 1; self.selected_set = {self.selected_idx}
            self._refresh_element_combo(); self._redraw_canvas(); dlg.destroy()
        make_vt_btn(dlg, text="➕ Add Field", bg="#16a34a", fg="#ffffff", font=("Sans", 10, "bold"), width=14, command=do_add).pack(pady=14)
        dlg.update_idletasks(); dlg.lift(); dlg.focus_force()

    def delete_field(self, _e=None):
        if not self.tpl.get("elements"): return
        self._push_undo()
        sel = sorted(getattr(self, "selected_set", {self.selected_idx}) or {self.selected_idx}, reverse=True)
        for idx in sel:
            if 0 <= idx < len(self.tpl["elements"]):
                self.tpl["elements"].pop(idx)
        self.selected_idx = max(0, min(self.selected_idx, len(self.tpl["elements"]) - 1))
        self.selected_set = {self.selected_idx} if self.tpl["elements"] else set()
        self._refresh_element_combo(); self._redraw_canvas()

    def duplicate_selected(self, _e=None):
        if not self.tpl.get("elements"): return
        self._push_undo(); src = getattr(self, "selected_set", {self.selected_idx}) or {self.selected_idx}; new_s = set()
        for i in sorted(src):
            if 0 <= i < len(self.tpl["elements"]):
                c = dict(self.tpl["elements"][i])
                c["x"] = round(min(float(self.tpl.get("width_in", 2.0)) - 0.2, float(c.get("x", 0.04)) + 0.08), 3)
                c["y"] = round(min(float(self.tpl.get("height_in", 1.0)) - 0.1, float(c.get("y", 0.04)) + 0.08), 3)
                self.tpl["elements"].append(c); new_s.add(len(self.tpl["elements"]) - 1)
        if new_s:
            self.selected_set = new_s; self.selected_idx = max(new_s)
            self._refresh_element_combo(); self._redraw_canvas()

    def bring_to_front(self):
        if not self.tpl.get("elements"): return
        self._push_undo()
        el = self.tpl["elements"].pop(self.selected_idx)
        self.tpl["elements"].append(el)
        self.selected_idx = len(self.tpl["elements"]) - 1
        self.selected_set = {self.selected_idx}
        self._refresh_element_combo(); self._redraw_canvas()

    def send_to_back(self):
        if not self.tpl.get("elements"): return
        self._push_undo()
        el = self.tpl["elements"].pop(self.selected_idx)
        self.tpl["elements"].insert(0, el)
        self.selected_idx = 0
        self.selected_set = {0}
        self._refresh_element_combo(); self._redraw_canvas()

    def toggle_word_wrap(self):
        if not self.tpl.get("elements"): return
        self._push_undo()
        el = self.tpl["elements"][self.selected_idx]
        el["word_wrap"] = not bool(el.get("word_wrap", el.get("field") == "name"))
        self.word_wrap_var.set(bool(el["word_wrap"]))
        self._load_selected_props(); self._redraw_canvas()

    def toggle_autofit_ctx(self):
        if not self.tpl.get("elements"): return
        self._push_undo()
        el = self.tpl["elements"][self.selected_idx]
        el["auto_font"] = not bool(el.get("auto_font", True))
        self.autofit_font_var.set(bool(el["auto_font"]))
        self._load_selected_props(); self._redraw_canvas()

    def select_all_fields(self, _e=None):
        self.selected_set = set(range(len(self.tpl.get("elements", [])))); self._redraw_canvas()

    def rotate_selected(self, _e=None):
        if not self.tpl.get("elements"): return
        self._push_undo()
        for i in (getattr(self, "selected_set", {self.selected_idx}) or {self.selected_idx}):
            el = self.tpl["elements"][i]; el["rotate"] = (int(el.get("rotate", 0)) + 90) % 360
            el["w"], el["h"] = round(float(el.get("h", 0.24)), 3), round(float(el.get("w", 0.90)), 3)
        self._load_selected_props(); self._redraw_canvas()

    def nudge_selected(self, dx_in, dy_in):
        if not self.tpl.get("elements"): return
        self._push_undo()
        for i in (getattr(self, "selected_set", {self.selected_idx}) or {self.selected_idx}):
            if 0 <= i < len(self.tpl["elements"]):
                el = self.tpl["elements"][i]
                el["x"] = round(max(0.0, min(float(self.tpl["width_in"]) - 0.05, float(el["x"]) + dx_in)), 3)
                el["y"] = round(max(0.0, min(float(self.tpl["height_in"]) - 0.05, float(el["y"]) + dy_in)), 3)
        self._load_selected_props(); self._redraw_canvas()

    def copy_selected(self, *_a):
        if not self.tpl.get("elements"): return
        sel = sorted(getattr(self, "selected_set", {self.selected_idx}) or {self.selected_idx})
        self.clipboard_els = [json.loads(json.dumps(self.tpl["elements"][i])) for i in sel if 0 <= i < len(self.tpl["elements"])]
        self.status_var.set(f"Copied {len(self.clipboard_els)} component(s) to clipboard.")

    def cut_selected(self, *_a):
        self.copy_selected()
        self.delete_field()

    def paste_clipboard(self, *_a):
        if not getattr(self, "clipboard_els", None): return
        self._push_undo()
        new_s = set()
        for c in self.clipboard_els:
            clone = json.loads(json.dumps(c))
            clone["x"] = round(min(float(self.tpl.get("width_in", 2.0)) - 0.15, float(clone.get("x", 0.05)) + 0.06), 3)
            clone["y"] = round(min(float(self.tpl.get("height_in", 1.0)) - 0.10, float(clone.get("y", 0.05)) + 0.06), 3)
            self.tpl["elements"].append(clone)
            new_s.add(len(self.tpl["elements"]) - 1)
        if new_s:
            self.selected_set = new_s; self.selected_idx = max(new_s)
            self._refresh_element_combo(); self._redraw_canvas()

    def on_right_click(self, event):
        if not self.tpl.get("elements"): return
        ox, oy, sc = getattr(self, "ox", 24), getattr(self, "oy", 24), self.scale
        for idx in range(len(self.tpl["elements"]) - 1, -1, -1):
            el = self.tpl["elements"][idx]
            bx1, by1 = ox + float(el.get("x", 0)) * sc, oy + float(el.get("y", 0)) * sc
            bx2, by2 = bx1 + max(18, float(el.get("w", 0.5)) * sc), by1 + max(12, float(el.get("h", 0.15)) * sc)
            if (bx1 - 4) <= event.x <= (bx2 + 4) and (by1 - 4) <= event.y <= (by2 + 4):
                self.selected_idx = idx
                self.selected_set = {idx}
                self.el_cb.current(idx)
                self._load_selected_props()
                self._redraw_canvas()
                break
        el = self.tpl["elements"][self.selected_idx]
        ww = "✓ " if el.get("word_wrap", el.get("field") == "name") else "    "
        af = "✓ " if el.get("auto_font", True) else "    "
        menu = tk.Menu(self, tearoff=0, bg="#1e293b", fg="#f8fafc", activebackground="#2563eb", activeforeground="white", font=("Sans", 10, "bold"))
        menu.add_command(label="Bring to Front", command=self.bring_to_front)
        menu.add_command(label="Send to Back", command=self.send_to_back)
        menu.add_separator()
        menu.add_command(label=f"{af}AutoSize / Auto-Fit Font", command=self.toggle_autofit_ctx)
        menu.add_command(label=f"{ww}WordWrap (2-Line Text)", command=self.toggle_word_wrap)
        menu.add_separator()
        menu.add_command(label="Position / Properties...", command=lambda: self.on_canvas_double_click(None))
        menu.add_command(label="Duplicate (Ctrl+D)", command=self.duplicate_selected)
        menu.add_command(label="Rotate 90° (R)", command=self.rotate_selected)
        menu.add_separator()
        menu.add_command(label="Delete Component (Del)", fg="#fca5a5", command=self.delete_field)
        menu.tk_popup(event.x_root, event.y_root)

    def on_canvas_double_click(self, event=None):
        if not self.tpl.get("elements"): return
        if event is not None:
            for iid in reversed(self.canvas.find_overlapping(event.x-3, event.y-3, event.x+3, event.y+3)):
                for tag in self.canvas.gettags(iid):
                    if tag.startswith("el_"):
                        self.selected_idx = int(tag.split("_", 1)[1]); self.selected_set = {self.selected_idx}
                        self.el_cb.current(self.selected_idx); self._load_selected_props(); self._redraw_canvas(); break
        el = self.tpl["elements"][self.selected_idx]
        fk_cur = el.get("field", "name")
        dlg = tk.Toplevel(self); dlg.title(f"Component Properties — {fk_cur}"); dlg.geometry("480x360"); dlg.after_idle(lambda d=dlg: fit_dialog(d, 480, 360)); dlg.configure(bg="#1e293b"); dlg.transient(self); dlg.attributes("-topmost", True)
        tk.Label(dlg, text=f"Editing Component #{self.selected_idx + 1} Properties", bg="#1e293b", fg="#38bdf8", font=("Sans", 12, "bold")).pack(pady=(12, 8))
        frm = tk.Frame(dlg, bg="#1e293b"); frm.pack(fill="x", padx=20)
        tk.Label(frm, text="Data Field / Type:", bg="#1e293b", fg="#f8fafc", font=("Sans", 10, "bold")).grid(row=0, column=0, sticky="w", pady=6)
        cur_lbl = next((x for x in GLOBAL_FIELD_LABELS if x.startswith(fk_cur + " ")), GLOBAL_FIELD_LABELS[0])
        f_var = tk.StringVar(value=cur_lbl)
        ttk.Combobox(frm, textvariable=f_var, values=GLOBAL_FIELD_LABELS, state="readonly", width=32, font=("Sans", 10)).grid(row=0, column=1, sticky="w", padx=8, pady=6)
        tk.Label(frm, text="Custom / Static Text:", bg="#1e293b", fg="#f8fafc", font=("Sans", 10, "bold")).grid(row=1, column=0, sticky="w", pady=6)
        tv = tk.StringVar(value=str(el.get("text") or globals().get("GLOBAL_VAR_TEXT", "RECEIVED")))
        ent = tk.Entry(frm, textvariable=tv, font=("Sans", 11, "bold"), width=26, bg="white", fg="black"); ent.grid(row=1, column=1, sticky="w", padx=8, pady=6)
        tk.Label(frm, text="Left / Top (in):", bg="#1e293b", fg="#f8fafc", font=("Sans", 10, "bold")).grid(row=2, column=0, sticky="w", pady=6)
        pos_f = tk.Frame(frm, bg="#1e293b"); pos_f.grid(row=2, column=1, sticky="w", padx=8, pady=6)
        xv = tk.StringVar(value=f"{float(el.get('x', 0.0)):.3f}")
        yv = tk.StringVar(value=f"{float(el.get('y', 0.0)):.3f}")
        tk.Entry(pos_f, textvariable=xv, width=7, font=("Sans", 10, "bold")).pack(side="left", padx=(0, 6))
        tk.Entry(pos_f, textvariable=yv, width=7, font=("Sans", 10, "bold")).pack(side="left")
        tk.Label(frm, text="Rotation (deg):", bg="#1e293b", fg="#f8fafc", font=("Sans", 10, "bold")).grid(row=3, column=0, sticky="w", pady=6)
        rv = tk.StringVar(value=str(int(el.get("rotate", 0)) % 360))
        ttk.Combobox(frm, textvariable=rv, values=["0", "90", "180", "270"], state="readonly", width=10, font=("Sans", 10)).grid(row=3, column=1, sticky="w", padx=8, pady=6)
        def save_props(_e=None):
            self._push_undo(); new_fk = f_var.get().split(" — ")[0].strip(); el["field"] = new_fk; el["id"] = new_fk
            if new_fk in ("variable_text", "static_label", "db_calc"):
                val = tv.get().strip() or ("RECEIVED" if new_fk == "variable_text" else "TEXT")
                el["text"] = val
                if new_fk == "variable_text": globals()["GLOBAL_VAR_TEXT"] = val.upper()
            try:
                el["x"] = round(max(0.0, float(xv.get())), 3)
                el["y"] = round(max(0.0, float(yv.get())), 3)
            except ValueError:
                pass
            old_r, new_r = int(el.get("rotate", 0)) % 360, int(rv.get()) % 360
            if (old_r in (90, 270)) != (new_r in (90, 270)): el["w"], el["h"] = round(float(el.get("h", 0.24)), 3), round(float(el.get("w", 0.90)), 3)
            el["rotate"] = new_r; self._refresh_element_combo(); self._load_selected_props(); self._redraw_canvas(); dlg.destroy()
        ent.bind("<Return>", save_props); btn_f = tk.Frame(dlg, bg="#1e293b"); btn_f.pack(pady=16)
        make_vt_btn(btn_f, text="✓ Save Properties", bg="#16a34a", fg="#ffffff", font=("Sans", 10, "bold"), width=16, command=save_props).pack(side="left", padx=8)
        make_vt_btn(btn_f, text="Cancel", bg="#64748b", fg="#ffffff", font=("Sans", 10, "bold"), width=10, command=dlg.destroy).pack(side="left", padx=8)
        dlg.update_idletasks(); dlg.lift(); ent.focus_set()

    def _build_ui(self):
        # Row 1 Toolbar: Save, Undo/Redo, Element Selector, Data Source Dropdown, Font, Bold/Italic, Align, Close
        tbar1 = tk.Frame(self, bg="#0c1017", bd=0, relief="flat", pady=4)
        tbar1.pack(fill="x", side="top")

        # 1. PACK CLOSE ON THE RIGHT FIRST SO IT IS PERMANENTLY ANCHORED AT TOP-RIGHT!
        make_vt_btn(tbar1, text="✖ Close", bg="#dc2626", fg="#ffffff", font=("Sans", 10, "bold"), width=12, pady=6, radius=12, command=self.on_close_request).pack(side="right", padx=8, pady=2)

        make_vt_btn(tbar1, text="Save", bg="#16a34a", fg="#ffffff", font=("Sans", 10, "bold"), width=9, pady=6, radius=12, command=self.save_and_close).pack(side="left", padx=4, pady=2)
        make_vt_btn(tbar1, text="↶ Undo", bg="#334155", fg="#ffffff", font=("Sans", 10, "bold"), width=7, pady=6, radius=12, command=self.undo).pack(side="left", padx=2, pady=2)
        make_vt_btn(tbar1, text="↷ Redo", bg="#334155", fg="#ffffff", font=("Sans", 10, "bold"), width=7, pady=6, radius=12, command=self.redo).pack(side="left", padx=2, pady=2)

        tk.Label(tbar1, text="Object:", bg="#0c1017", fg="#38bdf8", font=("Sans", 9, "bold")).pack(side="left", padx=(6, 2))
        self.el_var = tk.StringVar()
        self.el_cb = ttk.Combobox(tbar1, textvariable=self.el_var, state="readonly", width=12, font=("Sans", 9, "bold"))
        self.el_cb.pack(side="left", padx=2, ipady=2)
        self.el_cb.bind("<<ComboboxSelected>>", self.on_pick_element)

        tk.Label(tbar1, text="Field:", bg="#0c1017", fg="#38bdf8", font=("Sans", 9, "bold")).pack(side="left", padx=(6, 2))
        self.ftype_var = tk.StringVar()
        self.ftype_cb = ttk.Combobox(tbar1, textvariable=self.ftype_var, values=GLOBAL_FIELD_LABELS, state="readonly", width=18, font=("Sans", 9))
        self.ftype_cb.pack(side="left", padx=2, ipady=2)
        self.ftype_cb.bind("<<ComboboxSelected>>", self.on_change_field_type)

        tk.Label(tbar1, text="Text:", bg="#0c1017", fg="#cbd5e1", font=("Sans", 9, "bold")).pack(side="left", padx=(4, 2))
        self.custom_text_var = tk.StringVar(value="")
        ct_ent = tk.Entry(tbar1, textvariable=self.custom_text_var, width=9, font=("Sans", 9, "bold"), bd=0, highlightthickness=1, highlightbackground="#334155")
        ct_ent.pack(side="left", padx=2, ipady=2)
        ct_ent.bind("<Return>", self.apply_custom_text)
        ct_ent.bind("<FocusOut>", self.apply_custom_text)

        tk.Label(tbar1, text="Font:", bg="#0c1017", fg="#cbd5e1", font=("Sans", 9, "bold")).pack(side="left", padx=(4, 2))
        self.font_fam_var = tk.StringVar(value="Sans")
        ff_cb = ttk.Combobox(tbar1, textvariable=self.font_fam_var, values=["Sans", "Serif", "Mono", "Arial", "Arial Black", "Courier New"], state="readonly", width=11, font=("Sans", 9))
        ff_cb.pack(side="left", padx=2, ipady=2)
        ff_cb.bind("<<ComboboxSelected>>", lambda _e: self.apply_prop_change())

        self.size_var = tk.StringVar(value="12")
        sz_spin = tk.Spinbox(tbar1, from_=5, to=72, textvariable=self.size_var, width=3, font=("Sans", 9, "bold"), command=self.apply_prop_change)
        sz_spin.pack(side="left", padx=2, ipady=2)
        sz_spin.bind("<Return>", lambda _e: self.apply_prop_change())

        self.bold_var = tk.BooleanVar(value=True)
        tk.Checkbutton(tbar1, text="B", variable=self.bold_var, bg="#0c1017", fg="white", selectcolor="#1e293b", activebackground="#0c1017", activeforeground="white", bd=0, highlightthickness=0, font=("Sans", 9, "bold"), command=self.apply_prop_change).pack(side="left", padx=2)
        self.italic_var = tk.BooleanVar(value=False)
        tk.Checkbutton(tbar1, text="I", variable=self.italic_var, bg="#0c1017", fg="white", selectcolor="#1e293b", activebackground="#0c1017", activeforeground="white", bd=0, highlightthickness=0, font=("Sans", 9, "italic"), command=self.apply_prop_change).pack(side="left", padx=2)

        self.align_var = tk.StringVar(value="center")
        al_cb = ttk.Combobox(tbar1, textvariable=self.align_var, values=["left", "center", "right"], state="readonly", width=5, font=("Sans", 9))
        al_cb.pack(side="left", padx=2, ipady=2)
        al_cb.bind("<<ComboboxSelected>>", lambda _e: self.apply_prop_change())

        # Row 2 Toolbar: Component Palette (3x3 Matrix Button Style!)
        flow_wrap(tbar1)
        tbar_pal = tk.Frame(self, bg="#14171c", bd=0, relief="flat", pady=3)
        tbar_pal.pack(fill="x", side="top")
        tk.Label(tbar_pal, text="Palette (Tap Tool -> Tap Label):", bg="#14171c", fg="#38bdf8", font=("Sans", 9, "bold")).pack(side="left", padx=8, pady=3)
        palette_items = [
            ("Label", "static_label", "#1d4ed8"),
            ("DBText (Desc)", "name", "#047857"),
            ("DBText (Price)", "price_fmt", "#0f766e"),
            ("DBText (Brand)", "manufacturer", "#4338ca"),
            ("DBText (UPC)", "upc", "#0284c7"),
            ("SysVar (Date)", "date_yymmdd", "#b45309"),
            ("SysVar (Emp)", "emp_initials", "#0e7490"),
            ("Variable", "variable_text", "#7c3aed"),
            ("DBCalc", "db_calc", "#334155"),
            ("║▌ Code 128", "barcode_img", "#1e293b"),
            ("║▌ UPC-A", "barcode_upca", "#1e293b"),
        ]
        for btn_lbl, fk, pal_bg in palette_items:
            make_vt_btn(
                tbar_pal, text=btn_lbl, bg=pal_bg, fg="#ffffff",
                font=("Sans", 9, "bold"), padx=6, pady=4, radius=10,
                command=lambda k=fk, l=btn_lbl: self.arm_palette_tool(k, l)
            ).pack(side="left", padx=2, pady=2)

        # Row 3 Toolbar: Box Dimensions, WordWrap, Auto-Fit, Layer Order, Duplicate, Rotate, Delete, Label Size
        flow_wrap(tbar_pal)
        tbar2 = tk.Frame(self, bg="#0c1017", bd=0, relief="flat", pady=4)
        tbar2.pack(fill="x", side="top")

        # Right-side controls packed first so they stay visible
        make_vt_btn(tbar2, text="Apply Size", bg="#0f766e", fg="#ffffff", font=("Sans", 9, "bold"), padx=6, pady=5, radius=10, command=self.resize_label).pack(side="right", padx=4)
        self.lh_var = tk.StringVar(value=str(self.tpl["height_in"]))
        self.lw_var = tk.StringVar(value=str(self.tpl["width_in"]))
        tk.Entry(tbar2, textvariable=self.lh_var, width=4, font=("Sans", 9, "bold")).pack(side="right", padx=1, ipady=2)
        tk.Label(tbar2, text="x", bg="#0c1017", fg="white").pack(side="right")
        tk.Entry(tbar2, textvariable=self.lw_var, width=4, font=("Sans", 9, "bold")).pack(side="right", padx=1, ipady=2)

        cur_w, cur_h = float(self.tpl.get("width_in", 2.0)), float(self.tpl.get("height_in", 1.0))
        init_media = next((m[0] for m in MEDIA_STOCK_PRESETS if m[1] == cur_w and m[2] == cur_h), self.tpl.get("media_stock", "Custom Size (W x H in)"))
        self.media_var = tk.StringVar(value=init_media)
        media_cb = ttk.Combobox(tbar2, textvariable=self.media_var, values=MEDIA_STOCK_LABELS, state="readonly", width=18, font=("Sans", 9, "bold"))
        media_cb.pack(side="right", padx=4, ipady=2)
        media_cb.bind("<<ComboboxSelected>>", self.on_pick_media_preset)
        tk.Label(tbar2, text="Stock:", bg="#0c1017", fg="#38bdf8", font=("Sans", 9, "bold")).pack(side="right", padx=2)

        avail_printers = discover_all_printers()
        saved_p = resolve_template_printer(self.tpl.get("default_printer", ""), avail_printers)
        self.tpl["default_printer"] = saved_p
        self.designer_printer_var = tk.StringVar(value=saved_p)
        prn_cb = ttk.Combobox(tbar2, textvariable=self.designer_printer_var, values=avail_printers, state="readonly", width=22, font=("Sans", 9, "bold"))
        prn_cb.pack(side="right", padx=4, ipady=2)
        prn_cb.bind("<<ComboboxSelected>>", self.on_pick_designer_printer)
        tk.Label(tbar2, text="Target:", bg="#0c1017", fg="#14b8a6", font=("Sans", 9, "bold")).pack(side="right", padx=(6, 2))

        # Left-side box dimension & tool buttons
        tk.Label(tbar2, text="Box W:", bg="#0c1017", fg="#cbd5e1", font=("Sans", 9, "bold")).pack(side="left", padx=(6, 2), pady=3)
        self.box_w_var = tk.StringVar(value="1.00")
        bw_spin = tk.Spinbox(tbar2, from_=0.10, to=4.00, increment=0.05, format="%.2f", textvariable=self.box_w_var, width=4, font=("Sans", 9, "bold"), command=self.apply_box_spin_change)
        bw_spin.pack(side="left", padx=2, ipady=2)
        bw_spin.bind("<Return>", lambda _e: self.apply_box_spin_change())

        tk.Label(tbar2, text="H:", bg="#0c1017", fg="#cbd5e1", font=("Sans", 9, "bold")).pack(side="left", padx=(4, 2))
        self.box_h_var = tk.StringVar(value="0.20")
        bh_spin = tk.Spinbox(tbar2, from_=0.08, to=3.00, increment=0.02, format="%.2f", textvariable=self.box_h_var, width=4, font=("Sans", 9, "bold"), command=self.apply_box_spin_change)
        bh_spin.pack(side="left", padx=2, ipady=2)
        bh_spin.bind("<Return>", lambda _e: self.apply_box_spin_change())

        self.autofit_font_var = tk.BooleanVar(value=True)
        tk.Checkbutton(tbar2, text="AutoFont", variable=self.autofit_font_var, bg="#0c1017", fg="#f8fafc", selectcolor="#1e293b", activebackground="#0c1017", activeforeground="white", bd=0, highlightthickness=0, font=("Sans", 9, "bold"), command=self.on_toggle_autofit).pack(side="left", padx=4)

        self.word_wrap_var = tk.BooleanVar(value=False)
        tk.Checkbutton(tbar2, text="Wrap", variable=self.word_wrap_var, bg="#0c1017", fg="#f8fafc", selectcolor="#1e293b", activebackground="#0c1017", activeforeground="white", bd=0, highlightthickness=0, font=("Sans", 9, "bold"), command=self.toggle_word_wrap).pack(side="left", padx=4)

        make_vt_btn(tbar2, text="⬆ Front", bg="#334155", fg="#ffffff", font=("Sans", 9, "bold"), padx=6, pady=5, radius=10, command=self.bring_to_front).pack(side="left", padx=2)
        make_vt_btn(tbar2, text="⬇ Back", bg="#334155", fg="#ffffff", font=("Sans", 9, "bold"), padx=6, pady=5, radius=10, command=self.send_to_back).pack(side="left", padx=2)
        make_vt_btn(tbar2, text="Rotate", bg="#7c3aed", fg="#ffffff", font=("Sans", 9, "bold"), padx=6, pady=5, radius=10, command=self.rotate_selected).pack(side="left", padx=2)
        make_vt_btn(tbar2, text="Dup", bg="#b45309", fg="#ffffff", font=("Sans", 9, "bold"), padx=6, pady=5, radius=10, command=self.duplicate_selected).pack(side="left", padx=2)
        make_vt_btn(tbar2, text="Del", bg="#dc2626", fg="#ffffff", font=("Sans", 9, "bold"), padx=6, pady=5, radius=10, command=self.delete_field).pack(side="left", padx=2)

        # Designer Gray Workspace
        flow_wrap(tbar2)
        work = tk.Frame(self, bg="#808080")
        work.pack(fill="both", expand=True, padx=16, pady=12)

        self.canvas = tk.Canvas(work, bg="#808080", bd=0, highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)
        self.canvas.bind("<Configure>", self._redraw_canvas)
        self.canvas.bind("<ButtonPress-1>", self.on_canvas_down)
        self.canvas.bind("<B1-Motion>", self.on_canvas_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_canvas_up)
        self.canvas.bind("<Double-Button-1>", self.on_canvas_double_click)
        self.canvas.bind("<ButtonPress-3>", self.on_right_click)

        for seq, fn in [
            ("<Control-z>", self.undo), ("<Control-Z>", self.undo),
            ("<Control-y>", self.redo), ("<Control-Y>", self.redo),
            ("<Control-a>", self.select_all_fields), ("<Control-A>", self.select_all_fields),
            ("<Control-c>", self.copy_selected), ("<Control-C>", self.copy_selected),
            ("<Control-x>", self.cut_selected), ("<Control-X>", self.cut_selected),
            ("<Control-v>", self.paste_clipboard), ("<Control-V>", self.paste_clipboard),
            ("<Control-d>", self.duplicate_selected), ("<Control-D>", self.duplicate_selected),
            ("<Delete>", self.delete_field), ("<Escape>", self.cancel_palette_tool),
            ("<r>", self.rotate_selected), ("<R>", self.rotate_selected),
            ("<Left>", lambda _e: self.nudge_selected(-0.01, 0.0)),
            ("<Right>", lambda _e: self.nudge_selected(0.01, 0.0)),
            ("<Up>", lambda _e: self.nudge_selected(0.0, -0.01)),
            ("<Down>", lambda _e: self.nudge_selected(0.0, 0.01)),
            ("<Shift-Left>", lambda _e: self.nudge_selected(-0.05, 0.0)),
            ("<Shift-Right>", lambda _e: self.nudge_selected(0.05, 0.0)),
            ("<Shift-Up>", lambda _e: self.nudge_selected(0.0, -0.05)),
            ("<Shift-Down>", lambda _e: self.nudge_selected(0.0, 0.05)),
        ]:
            self.bind(seq, fn)

        self.status_var = tk.StringVar(value="Ready — Click any component to move/resize, right-click for context menu, or click a Palette tool above to drop a new field.")
        sbar = tk.Label(self, textvariable=self.status_var, bg="#0f172a", fg="#38bdf8", anchor="w", font=("Monospace", 10, "bold"), padx=10, pady=5)
        sbar.pack(fill="x", side="bottom")

        self._refresh_element_combo()

    def _refresh_element_combo(self):
        names = [f"{idx+1}: " + re.sub(r"_(left|right)$", "", str(el.get("field","")).replace("static:",""), flags=re.I) for idx, el in enumerate(self.tpl["elements"])]
        self.el_cb["values"] = names
        if names:
            self.selected_idx = min(self.selected_idx, len(names) - 1)
            self.el_cb.current(self.selected_idx)
            self._load_selected_props()

    def _load_selected_props(self):
        if not self.tpl.get("elements"):
            return
        el = self.tpl["elements"][self.selected_idx]
        self.size_var.set(str(el.get("size", 12)))
        cur_f = el.get("field", "name")
        match = [f"{k} — {lbl}" for k, lbl in GLOBAL_FIELD_CATALOG if k == cur_f]
        self.ftype_var.set(match[0] if match else cur_f)
        self.custom_text_var.set(str(el.get("text", "") or (globals().get("GLOBAL_VAR_TEXT", "RECEIVED") if cur_f == "variable_text" else "")))
        self.autofit_font_var.set(bool(el.get("auto_font", True)))
        self.word_wrap_var.set(bool(el.get("word_wrap", cur_f == "name")))
        self.bold_var.set(bool(el.get("bold", True)))
        self.italic_var.set(bool(el.get("italic", False)))
        self.font_fam_var.set(el.get("font_family", "Sans"))
        self.align_var.set(el.get("align", "center"))
        self.box_w_var.set(f"{float(el.get('w', 1.0)):.2f}")
        self.box_h_var.set(f"{float(el.get('h', 0.2)):.2f}")
        lx = float(el.get("x", 0.0))
        ly = float(el.get("y", 0.0))
        lw = float(el.get("w", 1.0))
        lh = float(el.get("h", 0.2))
        self.status_var.set(
            f"Object: #{self.selected_idx+1} ({cur_f})   |   Left: {lx:.3f} in   Top: {ly:.3f} in   Width: {lw:.3f} in   Height: {lh:.3f} in   |   Font: {el.get('size', 10)}pt   |   Ctrl+Z Undo | Right-Click Menu"
        )

    def on_pick_element(self, _e=None):
        self.selected_idx = self.el_cb.current()
        self.selected_set = {self.selected_idx}
        self._load_selected_props()
        self._redraw_canvas()

    def on_toggle_autofit(self):
        if not self.tpl.get("elements"): return
        self._push_undo()
        el = self.tpl["elements"][self.selected_idx]
        el["auto_font"] = bool(self.autofit_font_var.get())
        if el["auto_font"] and not str(el["field"]).startswith("barcode"):
            el["size"] = max(6, min(72, int(round(float(el.get("h", 0.2)) * 58))))
        self._load_selected_props()
        self._redraw_canvas()

    def apply_custom_text(self, _e=None):
        if not self.tpl.get("elements"): return
        el = self.tpl["elements"][self.selected_idx]
        val = self.custom_text_var.get().strip()
        if val != str(el.get("text", "")):
            self._push_undo()
            el["text"] = val
            if el.get("field") == "variable_text" and val:
                globals()["GLOBAL_VAR_TEXT"] = val.upper()
            self._redraw_canvas()

    def apply_prop_change(self):
        if not self.tpl.get("elements"): return
        self._push_undo()
        el = self.tpl["elements"][self.selected_idx]
        try:
            new_sz = max(5, int(self.size_var.get()))
            if new_sz != int(el.get("size", 12)):
                el["auto_font"] = False
                self.autofit_font_var.set(False)
            el["size"] = new_sz
        except ValueError:
            pass
        el["bold"] = bool(self.bold_var.get())
        el["italic"] = bool(self.italic_var.get())
        el["font_family"] = self.font_fam_var.get()
        el["align"] = self.align_var.get()
        self._load_selected_props()
        self._redraw_canvas()

    def apply_box_spin_change(self):
        if not self.tpl.get("elements"): return
        self._push_undo()
        el = self.tpl["elements"][self.selected_idx]
        try:
            el["w"] = round(max(0.10, float(self.box_w_var.get())), 3)
            el["h"] = round(max(0.08, float(self.box_h_var.get())), 3)
            if self.autofit_font_var.get() and not str(el["field"]).startswith("barcode"):
                el["size"] = max(6, min(64, int(round(el["h"] * 68))))
        except ValueError:
            pass
        self._load_selected_props()
        self._redraw_canvas()

    def on_pick_designer_printer(self, _e=None):
        self._push_undo()
        chosen_p = self.designer_printer_var.get().strip()
        self.tpl["default_printer"] = chosen_p
        self.status_var.set(f"Target Printer for '{self.tpl_name}' set to: {chosen_p} (Save report to lock in)")

    def on_pick_media_preset(self, _e=None):
        choice = self.media_var.get()
        self.tpl["media_stock"] = choice
        for lbl, mw, mh, mtype in MEDIA_STOCK_PRESETS:
            if lbl == choice and mw is not None and mh is not None:
                self._push_undo()
                self.lw_var.set(str(mw))
                self.lh_var.set(str(mh))
                self.tpl["width_in"] = float(mw)
                self.tpl["height_in"] = float(mh)
                self.tpl["media_type"] = mtype
                self._redraw_canvas()
                self.status_var.set(f"Media Stock set to: {choice} ({mw} in x {mh} in)")
                break

    def resize_label(self):
        try:
            self._push_undo()
            self.tpl["width_in"] = max(0.5, float(self.lw_var.get()))
            self.tpl["height_in"] = max(0.4, float(self.lh_var.get()))
            self.tpl["media_stock"] = f"{self.tpl['width_in']} x {self.tpl['height_in']} in"
            self._redraw_canvas()
        except ValueError:
            pass

    def on_change_field_type(self, _e=None):
        if not self.tpl.get("elements"): return
        self._push_undo()
        el = self.tpl["elements"][self.selected_idx]
        raw_choice = self.ftype_var.get().split(" — ")[0].strip()
        if raw_choice:
            el["field"] = raw_choice
            el["id"] = raw_choice
            if raw_choice == "static_label" and not el.get("text"):
                el["text"] = "TEXT"
            elif raw_choice == "variable_text" and not el.get("text"):
                el["text"] = globals().get("GLOBAL_VAR_TEXT", "RECEIVED")
            self._refresh_element_combo()
            self._load_selected_props()
            self._redraw_canvas()

    def on_canvas_down(self, event):
        self.canvas.focus_set()
        ox, oy, sc = getattr(self, "ox", 24), getattr(self, "oy", 24), self.scale

        if self.pending_tool is not None:
            fk, _lbl = self.pending_tool
            self.pending_tool = None
            self.canvas.config(cursor="")
            self._push_undo()
            cx = round(max(0.02, min(float(self.tpl["width_in"]) - 0.30, (event.x - ox) / sc)), 3)
            cy = round(max(0.02, min(float(self.tpl["height_in"]) - 0.14, (event.y - oy) / sc)), 3)
            def_w = 1.25 if str(fk).startswith("barcode") else (1.50 if fk == "name" else 0.70)
            def_h = 0.22 if str(fk).startswith("barcode") else 0.18
            ne = {
                "id": fk, "field": fk,
                "x": cx, "y": cy,
                "w": round(min(def_w, float(self.tpl["width_in"]) - cx), 3),
                "h": def_h,
                "size": 11, "bold": True, "align": "center", "auto_font": True,
                "word_wrap": (fk == "name")
            }
            if fk == "static_label": ne["text"] = "TEXT"
            elif fk == "variable_text": ne["text"] = globals().get("GLOBAL_VAR_TEXT", "RECEIVED")
            self.tpl["elements"].append(ne)
            self.selected_idx = len(self.tpl["elements"]) - 1
            self.selected_set = {self.selected_idx}
            self._refresh_element_combo()
            self._redraw_canvas()
            return

        ctrl_or_shift = bool(event.state & 0x0004) or bool(event.state & 0x0001)
        if not ctrl_or_shift:
            for item_id in reversed(self.canvas.find_overlapping(event.x - 7, event.y - 7, event.x + 7, event.y + 7)):
                for tag in self.canvas.gettags(item_id):
                    if tag.startswith("hdl_"):
                        h_dir = tag.split("_", 1)[1]
                        el = self.tpl["elements"][self.selected_idx]
                        self.interaction = {"mode": f"resize_{h_dir}", "start_x": event.x, "start_y": event.y, "orig_box": (float(el["x"]), float(el["y"]), float(el["w"]), float(el["h"])), "undo_pushed": False}
                        return
        hit_idx = None
        for idx in range(len(self.tpl["elements"]) - 1, -1, -1):
            el = self.tpl["elements"][idx]
            bx1, by1 = ox + float(el.get("x", 0)) * sc, oy + float(el.get("y", 0)) * sc
            bx2, by2 = bx1 + max(18, float(el.get("w", 0.5)) * sc), by1 + max(12, float(el.get("h", 0.15)) * sc)
            if (bx1 - 4) <= event.x <= (bx2 + 4) and (by1 - 4) <= event.y <= (by2 + 4):
                hit_idx = idx; break
        if hit_idx is not None:
            if not hasattr(self, "selected_set"): self.selected_set = {self.selected_idx}
            if ctrl_or_shift:
                if hit_idx in self.selected_set and len(self.selected_set) > 1: self.selected_set.remove(hit_idx)
                else: self.selected_set.add(hit_idx)
                self.selected_idx = hit_idx
            elif hit_idx not in self.selected_set:
                self.selected_set = {hit_idx}
                self.selected_idx = hit_idx
            else:
                self.selected_idx = hit_idx
            self.el_cb.current(self.selected_idx); self._load_selected_props(); self._redraw_canvas()
            el = self.tpl["elements"][self.selected_idx]
            orig_group = {i: (float(self.tpl["elements"][i]["x"]), float(self.tpl["elements"][i]["y"])) for i in self.selected_set if i < len(self.tpl["elements"])}
            self.interaction = {"mode": "move", "start_x": event.x, "start_y": event.y, "orig_box": (float(el["x"]), float(el["y"]), float(el["w"]), float(el["h"])), "orig_group": orig_group, "undo_pushed": False}
            return
        if not ctrl_or_shift: self.selected_set = set()
        self._redraw_canvas()
        self.interaction = {"mode": "lasso", "start_x": event.x, "start_y": event.y, "base_set": set(getattr(self, "selected_set", set())), "undo_pushed": True}

    def on_canvas_drag(self, event):
        if not self.tpl.get("elements") or not self.interaction.get("mode"):
            return
        mode = self.interaction["mode"]
        ox_c, oy_c, sc = getattr(self, "ox", 24), getattr(self, "oy", 24), self.scale

        if mode == "lasso":
            x1, y1 = min(self.interaction["start_x"], event.x), min(self.interaction["start_y"], event.y)
            x2, y2 = max(self.interaction["start_x"], event.x), max(self.interaction["start_y"], event.y)
            hits = set(self.interaction.get("base_set", set()))
            for idx, el in enumerate(self.tpl["elements"]):
                bx1 = ox_c + float(el.get("x", 0)) * sc
                by1 = oy_c + float(el.get("y", 0)) * sc
                bx2 = bx1 + max(18, float(el.get("w", 0.5)) * sc)
                by2 = by1 + max(12, float(el.get("h", 0.15)) * sc)
                if not (bx2 < x1 or bx1 > x2 or by2 < y1 or by1 > y2):
                    hits.add(idx)
            self.selected_set = hits
            if hits:
                self.selected_idx = min(hits)
            self._redraw_canvas()
            self.canvas.create_rectangle(x1, y1, x2, y2, outline="#2563eb", fill="#93c5fd", stipple="gray25", width=2, dash=(4, 2), tags=("lasso_rect",))
            return

        if not self.interaction.get("undo_pushed") and (abs(event.x - self.interaction["start_x"]) > 2 or abs(event.y - self.interaction["start_y"]) > 2):
            self._push_undo()
            self.interaction["undo_pushed"] = True

        dx = (event.x - self.interaction["start_x"]) / sc
        dy = (event.y - self.interaction["start_y"]) / sc
        ox, oy, ow, oh = self.interaction["orig_box"]
        el = self.tpl["elements"][self.selected_idx]

        if mode == "move":
            for gi, (gx, gy) in self.interaction.get("orig_group", {self.selected_idx: (ox, oy)}).items():
                if 0 <= gi < len(self.tpl["elements"]):
                    self.tpl["elements"][gi]["x"] = round(max(0.0, min(self.tpl["width_in"] - 0.05, gx + dx)), 3)
                    self.tpl["elements"][gi]["y"] = round(max(0.0, min(self.tpl["height_in"] - 0.05, gy + dy)), 3)
        elif mode.startswith("resize_"):
            h_dir = mode.split("_", 1)[1]
            nx, ny, nw, nh = ox, oy, ow, oh
            min_w, min_h = 0.12, 0.08
            if "e" in h_dir: nw = max(min_w, ow + dx)
            if "s" in h_dir: nh = max(min_h, oh + dy)
            if "w" in h_dir: nw = max(min_w, ow - dx); nx = ox + (ow - nw)
            if "n" in h_dir: nh = max(min_h, oh - dy); ny = oy + (oh - nh)
            el["x"], el["y"], el["w"], el["h"] = round(max(0.0, nx), 3), round(max(0.0, ny), 3), round(nw, 3), round(nh, 3)
            if el.get("auto_font", True) and not str(el["field"]).startswith("barcode"):
                el["size"] = max(6, min(64, int(round(nh * 68))))
        self._load_selected_props()
        self._redraw_canvas()

    def on_canvas_up(self, _event):
        self.interaction["mode"] = None
        self.canvas.delete("lasso_rect")
        self._redraw_canvas()

    def on_close_request(self, *a):
        if json.dumps(self.tpl, sort_keys=True) == getattr(self, "_orig_tpl_json", ""):
            self.destroy()
            return
        dlg = tk.Toplevel(self)
        dlg.title("Save Changes?")
        dlg.geometry("460x190"); dlg.after_idle(lambda d=dlg: fit_dialog(d, 460, 190))
        dlg.configure(bg="#1e293b")
        dlg.transient(self)
        dlg.attributes("-topmost", True)
        tk.Label(dlg, text=f"Do you want to save changes to '{self.tpl_name}' before closing?", bg="#1e293b", fg="#f8fafc", font=("Sans", 12, "bold"), wraplength=420, justify="center").pack(pady=(22, 18))
        bf = tk.Frame(dlg, bg="#1e293b")
        bf.pack(pady=6)
        tk.Button(bf, text="Yes, Save", bg="#16a34a", fg="white", font=("Sans", 11, "bold"), width=12, command=lambda: (dlg.destroy(), self.on_save_callback(self.tpl_name, self.tpl), self.destroy())).pack(side="left", padx=6)
        tk.Button(bf, text="✖ No, Discard", bg="#dc2626", fg="white", font=("Sans", 11, "bold"), width=12, command=lambda: (dlg.destroy(), self.destroy())).pack(side="left", padx=6)
        make_vt_btn(bf, text="Cancel", bg="#475569", fg="#ffffff", font=("Sans", 11, "bold"), width=9, command=dlg.destroy).pack(side="left", padx=6)
        dlg.update_idletasks()
        dlg.lift()
        dlg.focus_force()

    def save_and_close(self):
        self.on_save_callback(self.tpl_name, self.tpl)
        self._orig_tpl_json = json.dumps(self.tpl, sort_keys=True)
        self.status_var.set(f"✓ Saved '{self.tpl_name}'! Click ✖ Close when finished.")
        self.canvas.focus_set()
        self._redraw_canvas()

# --- Main 4-Step ViewTouch Label Utility ---
class VTRoundedButton(tk.Canvas):
    """ViewTouch-style rounded button with drop shadow, tactile press, and instant touchscreen response."""
    def __init__(self, parent, text="", command=None, bg="#1d4ed8", border=None,
                 fg="#ffffff", font=("Sans", 10, "bold"), radius=12,
                 shadow_color="#05070b", shadow_offset=4, active_bg=None,
                 active_border="#ffffff", is_active=False, width=None, height=None,
                 padx=10, pady=6, state="normal"):
        try:
            p_bg = parent.cget("bg")
        except Exception:
            p_bg = "#0c1017"

        import tkinter.font as tkfont
        try:
            f_obj = tkfont.Font(font=font)
            lines = str(text or "").splitlines() or [""]
            txt_w = max(f_obj.measure(ln) for ln in lines)
            txt_h = f_obj.metrics("linespace") * len(lines)
        except Exception:
            txt_w = len(str(text or "")) * 8
            txt_h = 20

        req_w = int(width * 9 + padx * 2 + 8) if width is not None else int(txt_w + padx * 2 + 16)
        req_h = int(height * 22 + pady * 2 + 8) if height is not None else int(txt_h + pady * 2 + 12)

        super().__init__(parent, width=max(36, req_w), height=max(32, req_h), bg=p_bg, bd=0, highlightthickness=0, cursor="hand2")
        self.text = text
        self.command = command
        self.bg_color = bg
        self.border_color = border if border is not None else bg
        self.fg_color = fg
        self.disabled_fg = "#64748b"
        self.font = font
        self.radius = radius
        self.shadow_color = shadow_color
        self.shadow_offset = shadow_offset
        self.active_bg = active_bg or bg
        self.active_border = active_border
        self.is_active = is_active
        self.btn_state = state
        self.pressed = False

        self.bind("<Configure>", lambda _e: self.draw())
        self.bind("<ButtonPress-1>", self._on_press)
        self.bind("<ButtonRelease-1>", self._on_release)

    def configure(self, cnf=None, **kw):
        if isinstance(cnf, dict):
            kw.update(cnf)
        redraw = False
        if "text" in kw:
            self.text = kw.pop("text"); redraw = True
        if "bg" in kw:
            self.bg_color = kw.pop("bg")
            self.active_bg = self.bg_color
            self.border_color = self.bg_color
            redraw = True
        if "activebackground" in kw:
            self.active_bg = kw.pop("activebackground"); redraw = True
        if "fg" in kw:
            self.fg_color = kw.pop("fg"); redraw = True
        if "activeforeground" in kw:
            kw.pop("activeforeground", None)
        if "state" in kw:
            self.btn_state = kw.pop("state"); redraw = True
        if "command" in kw:
            self.command = kw.pop("command")
        for ignore_k in ("relief", "bd", "highlightbackground", "highlightcolor", "highlightthickness"):
            kw.pop(ignore_k, None)
        if kw:
            super().configure(**kw)
        if redraw:
            self.draw()

    config = configure

    def __getitem__(self, key):
        if key == "state":
            return self.btn_state
        if key == "text":
            return self.text
        if key == "bg":
            return self.bg_color
        return super().__getitem__(key)

    def __setitem__(self, key, value):
        self.configure(**{key: value})

    def _on_press(self, _event=None):
        if getattr(self, "trace_name", None):
            _trace.info("%s touched (%s)", self.trace_name, self.btn_state)
        if self.btn_state == "disabled":
            return "break"
        self.pressed = True
        self.draw()
        if self.command:
            cmd = self.command
            self.after(35, lambda: self._finish_press(cmd))
        return "break"

    def _finish_press(self, cmd):
        if self.pressed:
            self.pressed = False
            if self.winfo_exists():
                self.draw()
        cmd()

    def _on_release(self, _event=None):
        if self.pressed:
            self.pressed = False
            if self.winfo_exists():
                self.draw()
        return "break"

    def _lighten(self, color, amount):
        try:
            r16, g16, b16 = self.winfo_rgb(color)
            mix = lambda c: int((c / 257) + (255 - c / 257) * amount)
            return "#%02x%02x%02x" % (mix(r16), mix(g16), mix(b16))
        except Exception:
            return color

    def _draw_rounded_rect(self, x1, y1, x2, y2, r, fill, outline=""):
        r = max(2, min(r, int((x2 - x1) / 2), int((y2 - y1) / 2)))
        self.create_arc(x1, y1, x1 + 2*r, y1 + 2*r, start=90, extent=90, fill=fill, outline=outline)
        self.create_arc(x2 - 2*r, y1, x2, y1 + 2*r, start=0, extent=90, fill=fill, outline=outline)
        self.create_arc(x1, y2 - 2*r, x1 + 2*r, y2, start=180, extent=90, fill=fill, outline=outline)
        self.create_arc(x2 - 2*r, y2 - 2*r, x2, y2, start=270, extent=90, fill=fill, outline=outline)
        self.create_rectangle(x1 + r, y1, x2 - r, y2, fill=fill, outline=outline)
        self.create_rectangle(x1, y1 + r, x2, y2 - r, fill=fill, outline=outline)

    def draw(self):
        if not self.winfo_exists():
            return
        self.delete("all")
        w = self.winfo_width()
        h = self.winfo_height()
        if w < 10 or h < 10:
            w = int(self.cget("width") or 80)
            h = int(self.cget("height") or 36)

        pad = 2
        r = max(5, min(self.radius, int(min(w, h) / 2.8)))
        so = self.shadow_offset

        if not self.pressed and so > 0:
            self._draw_rounded_rect(pad + so, pad + so, w - pad, h - pad, r, fill=self.shadow_color)

        shift = 2 if self.pressed else 0
        bx1 = pad + shift
        by1 = pad + shift
        bx2 = w - pad - (so if not self.pressed else shift)
        by2 = h - pad - (so if not self.pressed else shift)

        cur_fill = "#1e293b" if self.btn_state == "disabled" else (self.active_bg if self.is_active else self.bg_color)
        cur_fg = self.disabled_fg if self.btn_state == "disabled" else self.fg_color

        self._draw_rounded_rect(bx1, by1, bx2, by2, r, fill=cur_fill)

        if not self.pressed and self.btn_state != "disabled" and (bx2 - bx1) > 2 * r + 4:
            # Subtle solid top highlight: the button's own colour lightened (no stippled dots)
            self.create_line(bx1 + r, by1 + 1, bx2 - r, by1 + 1, fill=self._lighten(cur_fill, 0.22))

        cx = (bx1 + bx2) / 2.0
        cy = (by1 + by2) / 2.0
        self.create_text(cx, cy, text=self.text, font=self.font, fill=cur_fg, justify="center")


def go_full_screen(win):
    """Fill the screen with this window (the main window and the label designer).

    The Pi's window manager ignores a full-screen request made before the window is on screen, and for a
    window set as transient to another, so it's asked again once the window shows, with a maximised fallback."""
    sw, sh = win.winfo_screenwidth(), win.winfo_screenheight()
    win.geometry(f"{sw}x{sh}+0+0")

    def apply(_e=None):
        try:
            win.attributes("-fullscreen", True)
        except tk.TclError:
            pass
        try:
            win.attributes("-zoomed", True)
        except tk.TclError:
            pass
        win.lift()
    win.after(80, apply)
    win.after(600, apply)          # once more, after the window manager has placed it
    win.bind("<Map>", lambda e: apply() if e.widget is win else None, add="+")


def flow_wrap(frame, padx=4, pady=3):
    """Re-lay a toolbar's packed widgets so they wrap onto extra rows when the screen is too narrow
    (e.g. the Pi's 1360x768 display) instead of running off the right edge.
    Left-packed widgets keep their order; right-packed ones follow them in on-screen order."""
    items = []
    for w in frame.pack_slaves():
        items.append((w, w.pack_info().get("side", "left")))
        w.pack_forget()
    order = [w for w, side in items if side != "right"] + [w for w, side in reversed(items) if side == "right"]
    state = {"w": None}

    def relayout(_e=None):
        width = frame.winfo_width()
        if width <= 1:
            width = frame.winfo_toplevel().winfo_width()
        if width <= 1:
            width = frame.winfo_screenwidth()
        if state["w"] == width:
            return
        state["w"] = width
        rows, row, x = [], [], padx
        for w in order:
            rw = w.winfo_reqwidth()
            if row and x + rw > width - padx:
                rows.append(row); row, x = [], padx
            row.append((w, x)); x += rw + padx
        if row:
            rows.append(row)
        y = pady
        for row in rows:
            rh = max(w.winfo_reqheight() for w, _ in row)
            for w, x in row:
                w.place(x=x, y=y + (rh - w.winfo_reqheight()) // 2)
            y += rh + pady
        frame.configure(height=y)

    def child_resized(_e=None):
        # a button/label changed size (e.g. its text was filled in later): lay out again once things settle
        if not state.get("pending"):
            state["pending"] = True

            def again():
                state["pending"] = False
                state["w"] = None
                relayout()
            frame.after_idle(again)

    sizes = {}

    def on_child(e):
        key = str(e.widget)
        if sizes.get(key) != (e.widget.winfo_reqwidth(), e.widget.winfo_reqheight()):
            sizes[key] = (e.widget.winfo_reqwidth(), e.widget.winfo_reqheight())
            child_resized()

    for w in order:
        w.bind("<Configure>", on_child, add="+")
    frame.bind("<Configure>", relayout, add="+")
    frame.after_idle(relayout)
    return frame


def fit_dialog(dlg, min_w=0, min_h=0):
    """Size a dialog to its contents (never smaller than min_w x min_h, never larger than the screen)
    and centre it, so nothing is cut off on smaller screens."""
    try:
        dlg.update_idletasks()
        sw, sh = dlg.winfo_screenwidth(), dlg.winfo_screenheight()
        w = min(max(dlg.winfo_reqwidth(), min_w), sw - 20)
        h = min(max(dlg.winfo_reqheight(), min_h), sh - 60)
        dlg.geometry(f"{w}x{h}+{max(0, (sw - w) // 2)}+{max(0, (sh - h) // 2)}")
    except Exception:
        pass


def make_vt_btn(parent, text, command=None, bg="#1d4ed8", fg="#ffffff", font=("Sans", 10, "bold"), width=None, height=None, padx=10, pady=6, state="normal", radius=12):
    return VTRoundedButton(
        parent,
        text=text,
        command=command,
        bg=bg,
        fg=fg,
        font=font,
        width=width,
        height=height,
        padx=padx,
        pady=pady,
        state=state,
        radius=radius,
        shadow_offset=4,
    )


class VTLabelUtility(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f"ViewTouch Label Utility v{__version__}")
        go_full_screen(self)
        self.configure(bg="#14171c")

        self.templates = load_templates()
        self.all_items = load_from_viewtouch_db()
        self.filtered_items = list(self.all_items)
        self.current_step = 1
        self.preview_idx = 0

        self.header_banner = tk.Label(
            self,
            text=f"ViewTouch Database Connected ({len(self.all_items)} items loaded from viewtouch.db) — Choose your option...",
            bg="#0d5248", fg="#ffffff", font=("Sans", 12, "bold"), pady=6
        )
        self.header_banner.pack(fill="x", side="top")

        bbar = tk.Frame(self, bg="#0c1017", height=54)
        bbar.pack(fill="x", side="bottom", padx=12, pady=6)

        self.content = tk.Frame(self, bg="#1e242d")
        self.content.pack(fill="both", expand=True, padx=6, pady=4)

        self.close_btn = make_vt_btn(bbar, text="✖ Close", bg="#dc2626", fg="#ffffff", font=("Sans", 10, "bold"), width=14, pady=8, radius=12, command=self.destroy)
        self.close_btn.pack(side="left", padx=(4, 6), pady=4)

        self.reload_btn = make_vt_btn(bbar, text="↻ Reload viewtouch.db", bg="#1d4ed8", fg="#ffffff", font=("Sans", 10, "bold"), width=20, pady=8, radius=12, command=self.reload_vt_db)
        self.reload_btn.pack(side="left", padx=6, pady=4)

        self.next_btn = make_vt_btn(bbar, text="Next >>", bg="#047857", fg="#ffffff", font=("Sans", 10, "bold"), width=14, pady=8, radius=12, command=self.go_next)
        self.next_btn.pack(side="right", padx=(6, 4), pady=4)
        self.next_btn.trace_name = "Next"

        self.prev_btn = make_vt_btn(bbar, text="<< Previous", bg="#0f766e", fg="#ffffff", font=("Sans", 10, "bold"), width=14, pady=8, radius=12, command=self.go_prev)
        self.prev_btn.pack(side="right", padx=6, pady=4)

        self.show_step(1)
        if START_UPC:
            self.after(150, lambda: self.open_for_upc(START_UPC))

    def open_for_upc(self, code):
        """Open straight to the label preview for the item with this UPC (any of its codes)."""
        want = "".join(ch for ch in str(code) if ch.isalnum())
        key = lambda c: c.lstrip("0") if c.isdigit() else c
        for it in self.all_items:
            for lbl, c in it.get("upc_choices", []) or [("Primary UPC", it.get("upc", ""))]:
                if key(c) == key(want) or key(c) == key(normalize_upc(want)):
                    for other in self.all_items:
                        other["selected"] = False
                    it["selected"] = True
                    set_item_upc(it, c, lbl)
                    self.filtered_items = [it]
                    self.preview_idx = 0
                    self.came_from_step = 1
                    self.show_step(4)
                    return True
        self.header_banner.config(text=f"ViewTouch Label Utility — UPC {code} was not found in viewtouch.db. Choose your option...")
        return False

    def reload_vt_db(self):
        self.all_items = load_from_viewtouch_db()
        self.filtered_items = list(self.all_items)
        self.show_step(self.current_step)
        messagebox.showinfo("ViewTouch DB Reloaded", f"Loaded {len(self.all_items)} items from:\n{VT_DB_PATH}")


    def browse_template_file(self):
        from tkinter import filedialog, messagebox
        init_dir = os.path.expanduser("~/viewtouch-main/modern/seed/templates")
        if not os.path.exists(init_dir):
            init_dir = os.path.expanduser("~/.local/share/ViewTouch")
        path = filedialog.askopenfilename(
            parent=self, title="Import Label Template (ILSA .rtm, .json, .prn, .nlbl, .slnx)",
            initialdir=init_dir,
            filetypes=[("Label Templates", "*.rtm *.RTM *.json *.prn *.nlbl *.slnx"), ("ILSA ReportBuilder (.rtm)", "*.rtm *.RTM"), ("All Files", "*.*")]
        )
        if not path: return
        file_stem = os.path.splitext(os.path.basename(path))[0].strip().upper()
        chosen_tpl = None
        if path.lower().endswith(".rtm"):
            try:
                new_tpl = import_rtm_template(path)
            except Exception as e:
                messagebox.showerror("Import Failed", f"Could not import {os.path.basename(path)}:\n{e}"); return
            chosen_tpl = file_stem.replace("_", " ")
            if chosen_tpl in self.templates and not messagebox.askyesno("Replace Template?", f"A template named '{chosen_tpl}' already exists.\nReplace it with this file?"):
                return
            avail = discover_all_printers()
            new_tpl["default_printer"] = avail[0] if avail else "PDF Preview (Desktop)"
            self.templates[chosen_tpl] = ensure_box_dims(new_tpl)
            save_templates(self.templates)
            messagebox.showinfo("Template Imported", f"Imported '{chosen_tpl}' ({new_tpl['width_in']} x {new_tpl['height_in']} in, {len(new_tpl['elements'])} fields).\n\nCheck the printer under ⚙️ Printer Offset before printing.")
        elif path.endswith(".json"):
            try:
                loaded = json.load(open(path, "r", encoding="utf-8"))
                if isinstance(loaded, dict) and loaded:
                    self.templates.update(loaded)
                    save_templates(self.templates)
                    chosen_tpl = list(loaded.keys())[-1]
            except Exception as e:
                messagebox.showerror("Error", f"Could not load JSON: {e}"); return
        else:
            chosen_tpl = file_stem
            if chosen_tpl not in self.templates:
                base_ref = self.templates.get("RECEIVING LABEL") or list(self.templates.values())[0]
                self.templates[chosen_tpl] = json.loads(json.dumps(base_ref))
                save_templates(self.templates)

        if chosen_tpl and chosen_tpl in self.templates:
            self.came_from_step = self.current_step
            self.show_step(4)
            self.tpl_var.set(chosen_tpl)
            self._render_live_preview()

    def delete_selected_template(self):
        """Remove the template selected in the start screen's list (asks first; the last template can't be deleted)."""
        from tkinter import messagebox
        name = getattr(self, "step1_tpl_var", None) and self.step1_tpl_var.get()
        if not name or name not in self.templates:
            messagebox.showinfo("Delete Template", "Select a template in the list first.", parent=self); return
        if len(self.templates) <= 1:
            messagebox.showinfo("Delete Template", "This is the only template, so it can't be deleted.", parent=self); return
        if not messagebox.askyesno("Delete Template", f"Delete the template '{name}'?\n\n"
                                   "The four standard labels can be brought back with Restore Standard Labels.", parent=self):
            return
        del self.templates[name]
        save_templates(self.templates)
        if getattr(self, "chosen_template_name", None) == name:
            self.chosen_template_name = next(iter(self.templates))
        self.show_step(1)

    def restore_standard_templates(self):
        """Put the standard templates shipped with the program back (replacing ones with the same name;
        other templates are kept)."""
        from tkinter import messagebox
        try:
            with open(SEED_TEMPLATE_FILE, "r", encoding="utf-8") as f:
                std = json.load(f)
        except Exception as e:
            messagebox.showerror("Restore Standard Labels", f"Could not read the standard labels:\n{e}", parent=self); return
        try:  # same as first run: keep the RECEIVING LABEL adjustments saved by the original label utility
            with open(os.path.join(SANDBOX_DIR, "vt_label_templates_v2.json"), "r", encoding="utf-8") as vf:
                tuned = json.load(vf).get("RECEIVING LABEL")
            if isinstance(tuned, dict) and tuned.get("elements") and "RECEIVING LABEL" in std:
                std["RECEIVING LABEL"] = tuned
        except Exception:
            pass
        names = ", ".join(std.keys())
        if not messagebox.askyesno("Restore Standard Labels", f"Restore these labels to their original layout and printer?\n\n{names}\n\n"
                                   "Changes you made to them will be replaced. Other templates are kept.", parent=self):
            return
        for k, v in std.items():
            self.templates[k] = ensure_box_dims(json.loads(json.dumps(v)))
        save_templates(self.templates)
        self.show_step(1)
        messagebox.showinfo("Restore Standard Labels", f"Restored {len(std)} standard labels.", parent=self)

    def open_template_folder(self):
        folder = os.path.expanduser("~/viewtouch-main/modern/seed/templates")
        subprocess.Popen(["xdg-open", folder if os.path.exists(folder) else os.path.expanduser("~/.local/share/ViewTouch")])
    def show_step(self, step):
        _trace.info("show screen %s", step)
        self.current_step = step
        for w in self.content.winfo_children():
            w.destroy()

        if step == 1:
            self.header_banner.config(text=f"ViewTouch Label Utility ({len(self.all_items)} items in viewtouch.db) — Choose your option...")
            self.prev_btn.config(state="disabled", bg="#1e293b", activebackground="#1e293b")
            self.next_btn.config(state="normal", text="Next >>", bg="#047857", activebackground="#047857")
            self._build_step1()
        elif step == 2:
            self.header_banner.config(text="ViewTouch Label Utility — Select Category / Brand from viewtouch.db")
            self.prev_btn.config(state="normal", bg="#0f766e", activebackground="#0f766e")
            self.next_btn.config(state="normal", text="Next >>", bg="#047857", activebackground="#047857")
            self._build_step2()
        elif step == 3:
            self.header_banner.config(text="ViewTouch Label Utility — Select Items to Print (Click Row to Toggle Selected / Set Print Qty)")
            self.prev_btn.config(state="normal", bg="#0f766e", activebackground="#0f766e")
            self.next_btn.config(state="normal", text="Next >>", bg="#047857", activebackground="#047857")
            self._build_step3()
        elif step == 4:
            self.header_banner.config(text="ViewTouch Label Utility — Live Label Preview, Edit Report Designer, or Print")
            self.prev_btn.config(state="normal", bg="#0f766e", activebackground="#0f766e")
            self.next_btn.config(state="disabled", bg="#1e293b", activebackground="#1e293b")
            self._build_step4()
        _trace.info("screen %s ready", step)

    def select_mode_and_go(self, m_val):
        self.mode_var.set(m_val)
        self.mode_var_val = m_val
        self.from_top_button = True
        if m_val == 'tree':
            self.filtered_items = list(self.all_items)
            self.show_step(2)
        elif m_val == 'retail':
            self.filtered_items = [it for it in self.all_items if it.get('is_retail')]
            self.show_step(3)
        elif m_val == 'rcv':
            self.filtered_items = [it for it in self.all_items if it.get('pack_qty', 1) > 1 or it.get('is_retail')]
            self.show_step(3)
        else:
            self.filtered_items = list(self.all_items)
            self.show_step(3)

    def select_template_and_go(self, tpl_name):
        if tpl_name in self.templates:
            self.chosen_template_name = tpl_name
            self.came_from_step = 1
            self.show_step(4)
            self.tpl_var.set(tpl_name)
            self._render_live_preview()

    def _build_step1(self):
        pane = tk.Frame(self.content, bg="#0c1017")
        pane.pack(fill="both", expand=True, padx=12, pady=6)

        for c in range(12):
            pane.columnconfigure(c, weight=1, uniform="c")
        pane.rowconfigure(0, weight=1, minsize=140)
        pane.rowconfigure(1, weight=3)

        top_frame = tk.LabelFrame(
            pane, text=" SELECT VIEWTOUCH DATA ", bg="#0c1017", fg="#38bdf8",
            font=("Sans", 10, "bold"), bd=1, relief="solid", padx=8, pady=6
        )
        top_frame.grid(row=0, column=0, columnspan=12, sticky="nsew", padx=4, pady=(0, 6))

        for c in range(4):
            top_frame.columnconfigure(c, weight=1, uniform="top_btn")
        top_frame.rowconfigure(0, weight=1)

        cur_mode = getattr(self, "mode_var_val", "tree")
        self.mode_var = tk.StringVar(value=cur_mode)
        modes = [
            ("tree", "Tree View\n\nCategories & Brands", "#15803d"),
            ("retail", "Retail Items\n\nCigars & Goods Only", "#0284c7"),
            ("rcv", "Receiving Orders\n\nRecent Deliveries", "#1e3a8a"),
            ("all", "All Catalog\n\nFull Inventory", "#1e293b"),
        ]
        self.mode_btn_widgets = {}

        def select_mode(m_val):
            self.mode_var.set(m_val)
            self.mode_var_val = m_val
            for k, (btn, bg_c, act_c) in self.mode_btn_widgets.items():
                use_c = act_c if k == m_val else bg_c
                btn.configure(bg=use_c)

        mode_active_colors = {"tree": "#16a34a", "retail": "#0284c7", "rcv": "#2563eb", "all": "#334155"}
        mode_dim_colors = {"tree": "#14532d", "retail": "#0369a1", "rcv": "#1e3a8a", "all": "#1e293b"}

        for col_idx, (m_val, m_label, _m_bg) in enumerate(modes):
            is_active = (cur_mode == m_val)
            act_c = mode_active_colors.get(m_val, _m_bg)
            dim_c = mode_dim_colors.get(m_val, _m_bg)
            init_c = act_c if is_active else dim_c
            b = make_vt_btn(
                top_frame,
                text=m_label,
                font=("Sans", 10, "bold"),
                bg=init_c,
                fg="#ffffff",
                radius=12,
                command=lambda v=m_val: self.select_mode_and_go(v)
            )
            b.grid(row=0, column=col_idx, sticky="nsew", padx=4, pady=3)
            self.mode_btn_widgets[m_val] = (b, dim_c, act_c)

        # Large Format Saved Label Template Selection Window (3x Touch Height)
        tpl_sel_frame = tk.LabelFrame(
            pane, text=" SELECT SAVED LABEL TEMPLATE ", bg="#0c1017", fg="#38bdf8",
            font=("Sans", 11, "bold"), bd=1, relief="solid", padx=12, pady=10
        )
        tpl_sel_frame.grid(row=1, column=0, columnspan=8, sticky="nsew", padx=(4, 6), pady=2)
        tpl_sel_frame.columnconfigure(0, weight=1)
        tpl_sel_frame.rowconfigure(1, weight=1)

        active_tpl_names = list(self.templates.keys()) if hasattr(self, "templates") else []
        if "RECEIVING LABEL" in active_tpl_names:
            active_tpl_names.remove("RECEIVING LABEL")
            active_tpl_names.insert(0, "RECEIVING LABEL")

        self.step1_tpl_var = tk.StringVar(value=active_tpl_names[0] if active_tpl_names else "")
        self.option_add("*TCombobox*Listbox.font", ("Sans", 14, "bold"))

        top_box = tk.Frame(tpl_sel_frame, bg="#0c1017")
        top_box.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        top_box.columnconfigure(1, weight=1)

        tk.Label(top_box, text="Template:", bg="#0c1017", fg="#38bdf8", font=("Sans", 12, "bold")).grid(row=0, column=0, padx=(0, 8), sticky="w")
        cb = ttk.Combobox(top_box, textvariable=self.step1_tpl_var, values=active_tpl_names, state="readonly", font=("Sans", 14, "bold"))
        cb.grid(row=0, column=1, sticky="ew")

        # 3x Height Touch Rows (rowheight=58px)
        t_style = ttk.Style()
        t_style.configure("TplList.Treeview", rowheight=58, font=("Sans", 13, "bold"), background="#1e293b", foreground="#f8fafc", fieldbackground="#1e293b")
        t_style.configure("TplList.Treeview.Heading", font=("Sans", 11, "bold"))
        t_style.map("TplList.Treeview", background=[("selected", "#0284c7")], foreground=[("selected", "#ffffff")])

        list_frame = tk.Frame(tpl_sel_frame, bg="#0c1017")
        list_frame.grid(row=1, column=0, sticky="nsew", pady=4)
        list_frame.columnconfigure(0, weight=1)
        list_frame.rowconfigure(0, weight=1)

        cols = ("name", "dim", "prn")
        self.tpl_tree = ttk.Treeview(list_frame, columns=cols, show="headings", style="TplList.Treeview", selectmode="browse", height=4)
        self.tpl_tree.heading("name", text="Saved Label Template (File Manager Name)")
        self.tpl_tree.heading("dim", text="Stock Size")
        self.tpl_tree.heading("prn", text="Target Destination")
        self.tpl_tree.column("name", width=340, anchor="w")
        self.tpl_tree.column("dim", width=120, anchor="center")
        self.tpl_tree.column("prn", width=180, anchor="w")
        self.tpl_tree.grid(row=0, column=0, sticky="nsew")

        sb = tk.Scrollbar(list_frame, orient="vertical", command=self.tpl_tree.yview)
        sb.grid(row=0, column=1, sticky="ns")
        self.tpl_tree.config(yscrollcommand=sb.set)

        for idx, name in enumerate(active_tpl_names):
            t_obj = self.templates.get(name, {}) if hasattr(self, "templates") else {}
            w = t_obj.get("width_in", 2.0)
            h = t_obj.get("height_in", 1.0)
            prn = t_obj.get("default_printer", "Auto").split("(")[0].replace("VT TCP:", "").strip()
            self.tpl_tree.insert("", "end", iid=str(idx), values=(f" {name}", f"{w} x {h} in", f"{prn}"))

        if active_tpl_names:
            self.tpl_tree.selection_set("0")

        def on_cb_sel(_e=None):
            c = self.step1_tpl_var.get()
            if c in active_tpl_names:
                i = str(active_tpl_names.index(c))
                self.tpl_tree.selection_set(i); self.tpl_tree.see(i)

        def on_tree_sel(_e=None):
            s = self.tpl_tree.selection()
            if s:
                idx = int(s[0])
                if 0 <= idx < len(active_tpl_names): self.step1_tpl_var.set(active_tpl_names[idx])

        cb.bind("<<ComboboxSelected>>", on_cb_sel)
        self.tpl_tree.bind("<<TreeviewSelect>>", on_tree_sel)
        self.tpl_tree.bind("<Double-Button-1>", lambda _e: self.select_template_and_go(self.step1_tpl_var.get()))

        btn_box = tk.Frame(tpl_sel_frame, bg="#0c1017")
        btn_box.grid(row=2, column=0, sticky="ew", pady=(8, 2))
        make_vt_btn(btn_box, text="▶ Open & Preview Selected Label", bg="#16a34a", fg="#ffffff", font=("Sans", 12, "bold"), pady=8, radius=12, command=lambda: self.select_template_and_go(self.step1_tpl_var.get())).pack(fill="x")

        # Right Panel
        right_panel = tk.Frame(pane, bg="#0c1017")
        right_panel.grid(row=1, column=8, columnspan=4, sticky="nsew", padx=(4, 4), pady=2)
        right_panel.columnconfigure(0, weight=1)
        right_panel.rowconfigure(0, weight=3)
        right_panel.rowconfigure(1, weight=2)

        db_frame = tk.LabelFrame(
            right_panel, text=" DATABASE STATUS ", bg="#0c1017", fg="#38bdf8",
            font=("Sans", 10, "bold"), bd=1, relief="solid", padx=8, pady=4
        )
        db_frame.grid(row=0, column=0, sticky="nsew", pady=(0, 4))
        tk.Label(db_frame, text="ViewTouch POS Connected", bg="#0c1017", fg="#86efac", font=("Sans", 10, "bold"), anchor="w").pack(fill="x", pady=1)
        tk.Label(db_frame, text=f"• Catalog Items: {len(self.all_items)}", bg="#0c1017", fg="#f8fafc", font=("Sans", 9, "bold"), anchor="w").pack(fill="x", pady=1)
        tk.Label(db_frame, text=f"• Templates: {len(active_tpl_names)} active", bg="#0c1017", fg="#38bdf8", font=("Sans", 9, "bold"), anchor="w").pack(fill="x", pady=1)
        db_name = os.path.basename(VT_DB_PATH)
        tk.Label(db_frame, text=f"• Database: {db_name}", bg="#0c1017", fg="#94a3b8", font=("Monospace", 8), anchor="w").pack(fill="x", pady=1)

        tools_frame = tk.LabelFrame(
            right_panel, text=" TEMPLATE ACTIONS ", bg="#0c1017", fg="#38bdf8",
            font=("Sans", 10, "bold"), bd=1, relief="solid", padx=6, pady=4
        )
        tools_frame.grid(row=1, column=0, sticky="nsew", pady=(2, 0))
        tools_frame.columnconfigure(0, weight=1)
        tools_frame.columnconfigure(1, weight=1)
        tools_frame.rowconfigure(0, weight=1)
        tools_frame.rowconfigure(1, weight=1)

        btn_lookup = make_vt_btn(
            tools_frame,
            text="Lookup Template",
            font=("Sans", 10, "bold"),
            bg="#1d4ed8",
            fg="#ffffff",
            radius=12,
            command=self.browse_template_file
        )
        btn_lookup.grid(row=0, column=0, sticky="nsew", padx=4, pady=3)

        make_vt_btn(tools_frame, text="Delete Template", font=("Sans", 10, "bold"), bg="#b91c1c", fg="#ffffff", radius=12,
                    command=self.delete_selected_template).grid(row=0, column=1, sticky="nsew", padx=4, pady=3)
        make_vt_btn(tools_frame, text="Restore Standard Labels", font=("Sans", 10, "bold"), bg="#0f766e", fg="#ffffff", radius=12,
                    command=self.restore_standard_templates).grid(row=1, column=1, sticky="nsew", padx=4, pady=3)

        btn_folder = make_vt_btn(
            tools_frame,
            text="Open Template Folder",
            font=("Sans", 10, "bold"),
            bg="#334155",
            fg="#ffffff",
            radius=12,
            command=self.open_template_folder
        )
        btn_folder.grid(row=1, column=0, sticky="nsew", padx=4, pady=3)

    def _build_step2(self):
        left = tk.Frame(self.content, bg="#1e242d", width=290)
        left.pack(side="left", fill="y", padx=8, pady=8)

        tk.Label(left, text="ViewTouch Department Groups", bg="#1e242d", fg="#f2f4f7", font=("Sans", 11, "bold")).pack(anchor="w")
        self.nvs_list = tk.Listbox(left, font=("Sans", 11), height=18, exportselection=False)
        self.nvs_list.pack(fill="both", expand=True, pady=4)

        groups = sorted({it["category"] for it in self.all_items})
        self.nvs_list.insert("end", "ALL VIEWTOUCH CATEGORIES")
        for g in groups:
            self.nvs_list.insert("end", g)
        self.nvs_list.selection_set(0)
        self.nvs_list.bind("<<ListboxSelect>>", self._on_left_group_select)

        right = tk.Frame(self.content, bg="#1e242d")
        right.pack(side="right", fill="both", expand=True, padx=8, pady=8)
        tk.Label(right, text="ViewTouch Category & Brand Hierarchy (Double-Click or Select & Click Next >>)", bg="#1e242d", fg="#f2f4f7", font=("Sans", 11, "bold")).pack(anchor="w")

        self.cat_tree = ttk.Treeview(right, show="tree", selectmode="browse")
        self.cat_tree.pack(fill="both", expand=True, pady=4)
        self._populate_category_tree()
        self.cat_tree.bind("<Double-1>", lambda _e: self.go_next())

    def _on_left_group_select(self, _e=None):
        sel = self.nvs_list.curselection()
        if not sel:
            return
        chosen = self.nvs_list.get(sel[0])
        self._populate_category_tree(None if chosen == "ALL VIEWTOUCH CATEGORIES" else chosen)

    def _populate_category_tree(self, filter_cat=None):
        self.cat_tree.delete(*self.cat_tree.get_children())
        ilsa = any(it.get("source") == "ilsa_temp" for it in self.all_items[:1])
        root_text = "ILSA INVENTORY HIERARCHY" if ilsa else "VIEWTOUCH CATALOG (viewtouch.db)"
        root_node = self.cat_tree.insert("", "end", text=root_text, open=True, values=("ALL", ""))
        # Department group > category > brand > product group > sub group (the last two only when the
        # items have them, as ILSA's price book does).
        tree_map = {}
        for it in self.all_items:
            if filter_cat and it.get("category") != filter_cat:
                continue
            dg = it.get("dept_group") or "RETAIL"
            cat = it.get("category") or "GENERAL"
            mfg = it.get("manufacturer") or "GENERAL"
            pg = it.get("prod_grp") or ""
            sg = it.get("sub_grp") or ""
            tree_map.setdefault(dg, {}).setdefault(cat, {}).setdefault(mfg, {}).setdefault(pg, set()).add(sg)

        many = len(self.all_items) > 2000   # a big catalog starts with the categories closed
        for dg in sorted(tree_map.keys()):
            dg_id = self.cat_tree.insert(root_node, "end", text=dg, open=True, values=("DG", dg))
            for cat in sorted(tree_map[dg].keys()):
                cat_id = self.cat_tree.insert(dg_id, "end", text=cat, open=not many, values=("CAT", cat))
                for mfg in sorted(tree_map[dg][cat].keys()):
                    mfg_id = self.cat_tree.insert(cat_id, "end", text=f"M  {mfg}", open=False, values=("MFG", mfg, cat))
                    for pg in sorted(tree_map[dg][cat][mfg].keys()):
                        if not pg:
                            continue
                        pg_id = self.cat_tree.insert(mfg_id, "end", text=f"  ↳ {pg}", open=False, values=("PGRP", pg, mfg, cat))
                        for sg in sorted(tree_map[dg][cat][mfg][pg]):
                            if sg:
                                self.cat_tree.insert(pg_id, "end", text=f"    ▪ {sg}", values=("SGRP", sg, pg, mfg, cat))

    def _build_step3(self):
        top = tk.Frame(self.content, bg="#1e242d")
        top.pack(fill="x", padx=8, pady=6)

        tk.Label(top, text="Print UPC:", bg="#1e242d", font=("Sans", 11)).pack(side="left")
        self.upc_mode_var = tk.StringVar(value="Primary UPC")
        upc_mode_cb = ttk.Combobox(top, textvariable=self.upc_mode_var, state="readonly", width=12, font=("Sans", 10, "bold"),
                                   values=["Primary UPC", "Alt UPC 1", "Alt UPC 2", "Alt UPC 3", "Carton UPC"])
        upc_mode_cb.pack(side="left", padx=4)
        upc_mode_cb.bind("<<ComboboxSelected>>", lambda _e: self.apply_upc_mode(self.upc_mode_var.get()))
        make_vt_btn(top, text="↻ Next UPC", bg="#0284c7", fg="#ffffff", font=("Sans", 10, "bold"), padx=10, pady=6,
                    command=self.cycle_highlighted_upc).pack(side="left", padx=(4, 6))

        make_vt_btn(top, text="✓ Select All", bg="#047857", fg="#ffffff", font=("Sans", 10, "bold"), padx=12, pady=6, command=self.select_all).pack(side="left", padx=6)
        make_vt_btn(top, text="✖ Deselect All", bg="#334155", fg="#ffffff", font=("Sans", 10, "bold"), padx=12, pady=6, command=self.deselect_all).pack(side="left", padx=4)

        tk.Label(top, text="Quick Search:", bg="#1e242d", font=("Sans", 11)).pack(side="left", padx=(16, 4))
        self.filter_var = tk.StringVar()
        self.filter_var.trace_add("write", lambda *_: self._populate_grid())
        tk.Entry(top, textvariable=self.filter_var, width=24, font=("Sans", 11)).pack(side="left", padx=4)

        tk.Label(top, text="Print Qty for Selected:", bg="#1e242d", font=("Sans", 11)).pack(side="right", padx=4)
        self.batch_qty_var = tk.StringVar(value="1")
        sp = tk.Spinbox(top, from_=1, to=200, textvariable=self.batch_qty_var, width=5, font=("Sans", 11), command=self.update_selected_qty)
        sp.pack(side="right", padx=4)

        flow_wrap(top)
        # Six columns and tall rows, for a finger on the touch screen (the tree already chose the category/brand).
        g_style = ttk.Style(self)
        g_style.configure("ItemGrid.Treeview", rowheight=58, font=("Sans", 14))
        g_style.configure("ItemGrid.Treeview.Heading", font=("Sans", 12, "bold"))
        cols = ("sel", "qty", "upca", "upce", "desc", "price")
        grid_frame = tk.Frame(self.content, bg="#1e242d")
        grid_frame.pack(fill="both", expand=True, padx=8, pady=4)
        self.grid_tree = ttk.Treeview(grid_frame, columns=cols, show="headings", selectmode="browse", style="ItemGrid.Treeview")
        headers = [
            ("sel", "Selected", 110, False), ("qty", "Print Qty", 110, False), ("upca", "UPC on Label", 230, True),
            ("upce", "Other UPCs", 200, True), ("desc", "Description", 420, True), ("price", "Price", 110, False),
        ]
        for c_id, h_txt, w, grow in headers:
            self.grid_tree.heading(c_id, text=h_txt)
            if c_id in ("upca", "desc"):   # touch to sort; labels print in the list's order
                self.grid_tree.heading(c_id, command=lambda c=c_id: self.sort_grid(c))
            self.grid_tree.column(c_id, width=w, minwidth=w // 2, stretch=grow,
                                  anchor="center" if c_id in ("sel", "qty") else ("e" if c_id == "price" else "w"))
        g_scroll = ttk.Scrollbar(grid_frame, orient="vertical", command=self.grid_tree.yview)
        self.grid_tree.configure(yscrollcommand=g_scroll.set)
        g_scroll.pack(side="right", fill="y")
        self.grid_tree.pack(side="left", fill="both", expand=True)
        self.grid_tree.bind("<ButtonRelease-1>", self.on_toggle_row)
        self._populate_grid()

    def _populate_grid(self):
        top = self.grid_tree.yview()[0] if self.grid_tree.get_children() else 0.0   # touching a row keeps the list where it was
        self.grid_tree.delete(*self.grid_tree.get_children())
        q = getattr(self, "filter_var", tk.StringVar()).get().strip().lower()
        for idx, it in enumerate(self.filtered_items):
            all_codes = " ".join(c for _, c in it.get("upc_choices", [])).lower()
            if q and (q not in it["name"].lower() and q not in all_codes and q not in it["upc"].lower() and q not in it["manufacturer"].lower() and q not in it["category"].lower()):
                continue
            chk = "☑" if it.get("selected") else "☐"
            lbl = it.get("upc_label", "Primary UPC")
            on_label = it["upc"] if lbl == "Primary UPC" else f"{it['upc']}  [{lbl}]"
            others = ", ".join(c for _, c in it.get("upc_choices", []) if c != it["upc"])
            self.grid_tree.insert("", "end", iid=str(idx), values=(
                chk, it["qty"], on_label, others, it["name"], f"${it['price']:.2f}"
            ))
        if top:
            self.grid_tree.update_idletasks()
            self.grid_tree.yview_moveto(top)

    def sort_grid(self, col):
        """Sort the items by description or by the UPC on the label (touch again: reverse). Printing follows this order."""
        last = getattr(self, "grid_sort", None)
        reverse = bool(last and last[0] == col and not last[1])
        key = (lambda it: it["name"]) if col == "desc" else (lambda it: (len(str(it["upc"])), str(it["upc"])))
        self.filtered_items.sort(key=key, reverse=reverse)
        self.grid_sort = (col, reverse)
        for c_id, h_txt in (("upca", "UPC on Label"), ("desc", "Description")):
            arrow = (" ▼" if reverse else " ▲") if c_id == col else ""
            self.grid_tree.heading(c_id, text=h_txt + arrow)
        self.grid_tree.yview_moveto(0)
        self._populate_grid()

    def on_toggle_row(self, _event=None):
        sel = self.grid_tree.selection()
        if not sel:
            return
        idx = int(sel[0])
        it = self.filtered_items[idx]
        it["selected"] = not it.get("selected", False)
        try:
            it["qty"] = max(1, int(self.batch_qty_var.get()))
        except ValueError:
            it["qty"] = 1
        self._populate_grid()

    def apply_upc_mode(self, mode):
        """Print the chosen UPC (Primary / Alt UPC n / Carton UPC) on the checked items, or all listed items if none are checked.
        Items that don't have that UPC keep their primary."""
        targets = [it for it in self.filtered_items if it.get("selected")] or self.filtered_items
        for it in targets:
            choices = it.get("upc_choices") or [("Primary UPC", it.get("primary_upc", it["upc"]))]
            pick = next(((l, c) for l, c in choices if l == mode), choices[0])
            set_item_upc(it, pick[1], pick[0])
        self._populate_grid()

    def cycle_highlighted_upc(self):
        """Step the highlighted row (or the checked rows) to its next UPC."""
        sel_rows = [r for r in self.grid_tree.selection() if r.isdigit()]
        targets = [self.filtered_items[int(r)] for r in sel_rows] or [it for it in self.filtered_items if it.get("selected")]
        for it in targets:
            choices = it.get("upc_choices") or [("Primary UPC", it["upc"])]
            cur = next((i for i, (_, c) in enumerate(choices) if c == it["upc"]), 0)
            l, c = choices[(cur + 1) % len(choices)]
            set_item_upc(it, c, l)
        keep = sel_rows
        self._populate_grid()
        if keep:
            self.grid_tree.selection_set(keep)

    def update_selected_qty(self):
        try:
            q = max(1, int(self.batch_qty_var.get()))
        except ValueError:
            q = 1
        for it in self.filtered_items:
            if it.get("selected"):
                it["qty"] = q
        self._populate_grid()

    def select_all(self):
        for it in self.filtered_items:
            it["selected"] = True
        self._populate_grid()

    def deselect_all(self):
        for it in self.filtered_items:
            it["selected"] = False
        self._populate_grid()

    def _build_step4(self):
        selected_list = [it for it in self.filtered_items if it.get("selected")]
        if not selected_list:
            selected_list = [self.filtered_items[0]] if self.filtered_items else [{
                "name": "NO ITEMS IN VIEWTOUCH.DB YET", "upc": "000000000000", "price": 0.0,
                "manufacturer": "VIEWTOUCH", "category": "RETAIL", "qty": 1
            }]
        self.selected_for_print = selected_list
        self.preview_idx = min(self.preview_idx, len(selected_list) - 1)

        tbar = tk.Frame(self.content, bg="#0c1017", pady=6)
        tbar.pack(fill="x", padx=8, pady=4)

        tk.Label(tbar, text="Template:", bg="#0c1017", fg="#38bdf8", font=("Sans", 10, "bold")).pack(side="left", padx=(6, 4))
        init_tpl = getattr(self, "chosen_template_name", "2X1VINYL W/BARCODE")
        if init_tpl not in self.templates:
            init_tpl = list(self.templates.keys())[0]
        self.tpl_var = tk.StringVar(value=init_tpl)
        tpl_cb = ttk.Combobox(tbar, textvariable=self.tpl_var, values=list(self.templates.keys()), state="readonly", width=26, font=("Sans", 10, "bold"))
        tpl_cb.pack(side="left", padx=6, ipady=4)
        tpl_cb.bind("<<ComboboxSelected>>", lambda _e: self._render_live_preview())

        make_vt_btn(tbar, text="Print Labels", bg="#16a34a", fg="#ffffff", font=("Sans", 10, "bold"), width=13, pady=8, command=self.open_print_dialog).pack(side="left", padx=6)

        make_vt_btn(tbar, text="◀", bg="#334155", fg="#ffffff", font=("Sans", 10, "bold"), width=3, pady=8, command=self.prev_preview_item).pack(side="left", padx=2)
        self.page_lbl = tk.Label(tbar, text=f"1/{len(selected_list)}", bg="#0c1017", fg="#f8fafc", font=("Sans", 10, "bold"), padx=6)
        self.page_lbl.pack(side="left", padx=2)
        make_vt_btn(tbar, text="▶", bg="#334155", fg="#ffffff", font=("Sans", 10, "bold"), width=3, pady=8, command=self.next_preview_item).pack(side="left", padx=2)

        tk.Label(tbar, text="UPC:", bg="#0c1017", fg="#38bdf8", font=("Sans", 10, "bold")).pack(side="left", padx=(8, 2))
        self.step4_upc_var = tk.StringVar(value="")
        self.step4_upc_cb = ttk.Combobox(tbar, textvariable=self.step4_upc_var, state="readonly", width=28, font=("Sans", 10, "bold"))
        self.step4_upc_cb.pack(side="left", padx=2, ipady=4)

        def on_pick_step4_upc(_e=None):
            if not getattr(self, "selected_for_print", None):
                return
            it = self.selected_for_print[self.preview_idx]
            i = self.step4_upc_cb.current()
            choices = it.get("upc_choices") or [("Primary UPC", it["upc"])]
            if 0 <= i < len(choices):
                set_item_upc(it, choices[i][1], choices[i][0])
                self._render_live_preview()
        self.step4_upc_cb.bind("<<ComboboxSelected>>", on_pick_step4_upc)

        make_vt_btn(tbar, text="✏️ Edit Report", bg="#0f766e", fg="#ffffff", font=("Sans", 10, "bold"), width=13, pady=8, command=self.open_report_designer).pack(side="left", padx=6)
        make_vt_btn(tbar, text="➕ Create Report", bg="#1d4ed8", fg="#ffffff", font=("Sans", 10, "bold"), width=14, pady=8, command=self.clone_report).pack(side="left", padx=4)
        make_vt_btn(tbar, text="⚙️ Printer Offset", bg="#0284c7", fg="#ffffff", font=("Sans", 10, "bold"), width=14, pady=8, command=self.open_printer_settings_dialog).pack(side="left", padx=4)

        self.route_badge_lbl = tk.Label(tbar, text="", bg="#1e293b", fg="#38bdf8", font=("Sans", 9, "bold"), padx=10, pady=8, bd=0, relief="flat")
        self.route_badge_lbl.pack(side="right", padx=6)
        flow_wrap(tbar)

        self.preview_canvas = tk.Canvas(self.content, bg="#1e242d", bd=2, relief="sunken")
        self.preview_canvas.pack(fill="both", expand=True, padx=16, pady=10)
        self.preview_canvas.bind("<Configure>", lambda _e: self._render_live_preview())

    def prev_preview_item(self):
        if self.selected_for_print:
            self.preview_idx = max(0, self.preview_idx - 1)
            self._render_live_preview()

    def next_preview_item(self):
        if self.selected_for_print:
            self.preview_idx = min(len(self.selected_for_print) - 1, self.preview_idx + 1)
            self._render_live_preview()

    def _render_live_preview(self):
        if not hasattr(self, "preview_canvas") or not self.selected_for_print:
            return
        item = self.selected_for_print[self.preview_idx]
        self.page_lbl.config(text=f"{self.preview_idx + 1}/{len(self.selected_for_print)}")
        if hasattr(self, "step4_upc_cb"):
            choices = item.get("upc_choices") or [("Primary UPC", item.get("upc", ""))]
            vals = [f"{l}: {c}" for l, c in choices]
            self.step4_upc_cb["values"] = vals
            cur = next((i for i, (_, c) in enumerate(choices) if c == item.get("upc")), 0)
            self.step4_upc_var.set(vals[cur] if vals else "")
            self.step4_upc_cb.configure(state="readonly" if len(vals) > 1 else "disabled")
        tpl = self.templates[self.tpl_var.get()]
        if hasattr(self, "route_badge_lbl"):
            avail = discover_all_printers()
            routed_p = resolve_template_printer(tpl.get("default_printer", ""), avail)
            w_in, h_in = tpl.get("width_in", 2.0), tpl.get("height_in", 1.0)
            p_short = re.sub(r"\s*\[[^\]]*\]$", "", re.sub(r"^(VT TCP|VT CUPS|VT File|CUPS):\s*", "", str(routed_p)))
            self.route_badge_lbl.config(text=f"Printer: {p_short}   |   {w_in} x {h_in} in", wraplength=520, justify="left")
        cw = max(600, self.preview_canvas.winfo_width())
        ch = max(350, self.preview_canvas.winfo_height())
        try:
            # Show the exact picture that will be sent to the printer
            show_dots = bool(getattr(self, "preview_printer_dots", False))
            photo, pw, ph = label_preview_photo(tpl, item, cw - 60, ch - 40, printer_dots=show_dots)
            self._preview_photo = photo          # keep a reference or Tk drops the image
            c = self.preview_canvas
            c.delete("all")
            ox, oy = (cw - pw) // 2, (ch - ph) // 2
            c.create_rectangle(ox + 4, oy + 4, ox + pw + 4, oy + ph + 4, fill="#0f172a", outline="")
            c.create_image(ox, oy, image=photo, anchor="nw")
            c.create_rectangle(ox - 1, oy - 1, ox + pw, oy + ph, outline="#111827", width=2)
            cap = (f"Exact printer dots ({label_dpi(tpl)} dpi)  ·  double-click for sharp preview" if show_dots
                   else f"Sharp preview  ·  prints at {label_dpi(tpl)} dpi  ·  double-click to see exact printer dots")
            c.create_text(cw // 2, ch - 10, text=cap, fill="#94a3b8", font=("Sans", 9, "bold"))
            c.bind("<Double-Button-1>", lambda _e: (setattr(self, "preview_printer_dots", not show_dots), self._render_live_preview()))
        except Exception as e:
            logging.error("Bitmap preview failed, using drawn preview: %s", e)
            scale = min((cw - 60) / tpl["width_in"], (ch - 40) / tpl["height_in"])
            ox = (cw - tpl["width_in"] * scale) / 2.0
            oy = (ch - tpl["height_in"] * scale) / 2.0
            render_label_on_tk_canvas(self.preview_canvas, tpl, item, ox, oy, scale, selected_idx=None, show_boxes=False)

    def open_printer_settings_dialog(self):
        tpl_name = self.tpl_var.get()
        tpl = self.templates[tpl_name]
        printers = discover_all_printers()
        chosen = resolve_template_printer(tpl.get("default_printer", ""), printers)

        sdlg = tk.Toplevel(self)
        sdlg.title(f"Printer Settings & Alignment — [{tpl_name}]")
        sdlg.geometry("620x530"); sdlg.after_idle(lambda d=sdlg: fit_dialog(d, 620, 530))
        sdlg.configure(bg="#1e293b")
        sdlg.transient(self)
        sdlg.attributes("-topmost", True)

        tk.Label(sdlg, text="⚙️ Printer Position & Hardware Settings", bg="#1e293b", fg="#38bdf8", font=("Sans", 13, "bold")).pack(pady=(12, 2))
        tk.Label(sdlg, text=f"Template: {tpl_name}   |   Target: {chosen}", bg="#0f172a", fg="#86efac", font=("Sans", 9, "bold"), padx=10, pady=4).pack(pady=(0, 10))

        body = tk.Frame(sdlg, bg="#1e293b")
        body.pack(fill="both", expand=True, padx=20)

        # 1. Vertical Offset (Print Higher / Lower)
        vf = tk.LabelFrame(body, text=" Vertical Position (Print Higher or Lower on Label) ", bg="#1e293b", fg="#38bdf8", font=("Sans", 10, "bold"), padx=12, pady=8)
        vf.pack(fill="x", pady=5)

        cur_v = int(tpl.get("v_offset_dots", 18) if "v_offset_dots" in tpl else 18)
        v_var = tk.IntVar(value=cur_v)
        dpi_var = tk.StringVar(value=str(label_dpi(tpl)))
        v_lbl = tk.Label(vf, text="", bg="#1e293b", fg="#f8fafc", font=("Monospace", 11, "bold"), width=34)

        def update_v_label(*_a):
            val = int(v_var.get())
            inches = val / float(dpi_var.get() or 203)
            dir_txt = "LOWER ↓" if val > 0 else ("HIGHER ↑" if val < 0 else "CENTER (0)")
            v_lbl.config(text=f"{val:+d} dots ({inches:+.2f} in) [{dir_txt}]")

        def adj_v(delta):
            v_var.set(max(-160, min(160, int(v_var.get()) + delta)))
            update_v_label()

        make_vt_btn(vf, text="⬆ Print Higher (-8)", bg="#334155", fg="#ffffff", font=("Sans", 10, "bold"), command=lambda: adj_v(-8)).pack(side="left", padx=4)
        make_vt_btn(vf, text="-2", bg="#475569", fg="#ffffff", font=("Sans", 10, "bold"), width=3, command=lambda: adj_v(-2)).pack(side="left", padx=2)
        v_lbl.pack(side="left", padx=8)
        make_vt_btn(vf, text="+2", bg="#475569", fg="#ffffff", font=("Sans", 10, "bold"), width=3, command=lambda: adj_v(2)).pack(side="left", padx=2)
        make_vt_btn(vf, text="⬇ Print Lower (+8)", bg="#0284c7", fg="#ffffff", font=("Sans", 10, "bold"), command=lambda: adj_v(8)).pack(side="left", padx=4)
        update_v_label()

        # 2. Horizontal Offset (Print Left / Right)
        hf = tk.LabelFrame(body, text=" Horizontal Position (Shift Left or Right) ", bg="#1e293b", fg="#38bdf8", font=("Sans", 10, "bold"), padx=12, pady=8)
        hf.pack(fill="x", pady=5)

        cur_h = int(tpl.get("h_offset_dots", 0) or 0)
        h_var = tk.IntVar(value=cur_h)
        h_lbl = tk.Label(hf, text="", bg="#1e293b", fg="#f8fafc", font=("Monospace", 11, "bold"), width=34)

        def update_h_label(*_a):
            val = int(h_var.get())
            inches = val / float(dpi_var.get() or 203)
            dir_txt = "RIGHT ➡" if val > 0 else ("LEFT ⬅" if val < 0 else "CENTER (0)")
            h_lbl.config(text=f"{val:+d} dots ({inches:+.2f} in) [{dir_txt}]")

        def adj_h(delta):
            h_var.set(max(-160, min(160, int(h_var.get()) + delta)))
            update_h_label()

        make_vt_btn(hf, text="⬅ Shift Left (-8)", bg="#334155", fg="#ffffff", font=("Sans", 10, "bold"), command=lambda: adj_h(-8)).pack(side="left", padx=4)
        make_vt_btn(hf, text="-2", bg="#475569", fg="#ffffff", font=("Sans", 10, "bold"), width=3, command=lambda: adj_h(-2)).pack(side="left", padx=2)
        h_lbl.pack(side="left", padx=8)
        make_vt_btn(hf, text="+2", bg="#475569", fg="#ffffff", font=("Sans", 10, "bold"), width=3, command=lambda: adj_h(2)).pack(side="left", padx=2)
        make_vt_btn(hf, text="➡ Shift Right (+8)", bg="#334155", fg="#ffffff", font=("Sans", 10, "bold"), command=lambda: adj_h(8)).pack(side="left", padx=4)
        update_h_label()

        # 3. Darkness & Tear-Off Position
        df = tk.LabelFrame(body, text=" Print Darkness & Tear-Off Adjustment ", bg="#1e293b", fg="#38bdf8", font=("Sans", 10, "bold"), padx=12, pady=8)
        df.pack(fill="x", pady=5)

        tk.Label(df, text="Darkness (1-30):", bg="#1e293b", fg="#f8fafc", font=("Sans", 10, "bold")).pack(side="left", padx=(4, 6))
        dark_var = tk.StringVar(value=str(tpl.get("darkness", 18)))
        tk.Spinbox(df, from_=1, to=30, textvariable=dark_var, width=5, font=("Sans", 11, "bold")).pack(side="left", padx=4)

        tk.Label(df, text="Tear-Off (~TA dots):", bg="#1e293b", fg="#f8fafc", font=("Sans", 10, "bold")).pack(side="left", padx=(18, 6))
        tear_var = tk.StringVar(value=str(tpl.get("tear_off", 0)))
        tk.Spinbox(df, from_=-120, to=120, increment=4, textvariable=tear_var, width=6, font=("Sans", 11, "bold")).pack(side="left", padx=4)

        # 4. Printer resolution: the label picture is drawn dot-for-dot at this DPI
        rf = tk.LabelFrame(body, text=" Printer Resolution (must match the printer or labels print the wrong size) ", bg="#1e293b", fg="#38bdf8", font=("Sans", 10, "bold"), padx=12, pady=8)
        rf.pack(fill="x", pady=5)
        for d_opt, d_txt in (("203", "203 dpi (8 dots/mm)"), ("300", "300 dpi (12 dots/mm)"), ("600", "600 dpi (24 dots/mm)")):
            tk.Radiobutton(rf, text=d_txt, value=d_opt, variable=dpi_var, bg="#1e293b", fg="#f8fafc", selectcolor="#0f172a",
                           activebackground="#1e293b", activeforeground="#ffffff", font=("Sans", 10, "bold"),
                           command=lambda: (update_v_label(), update_h_label())).pack(side="left", padx=8)

        status_msg = tk.Label(sdlg, text="Tip: Positive (+) Vertical Offset moves print LOWER so the top line isn't clipped.", bg="#1e293b", fg="#94a3b8", font=("Sans", 9, "italic"))
        status_msg.pack(pady=(4, 4))

        def save_settings(close_after=False):
            tpl["v_offset_dots"] = int(v_var.get())
            tpl["h_offset_dots"] = int(h_var.get())
            try:
                tpl["darkness"] = int(dark_var.get())
            except ValueError:
                tpl["darkness"] = 18
            try:
                tpl["tear_off"] = int(tear_var.get())
            except ValueError:
                tpl["tear_off"] = 0
            try:
                tpl["printer_dpi"] = int(dpi_var.get())
            except ValueError:
                tpl["printer_dpi"] = 203
            save_templates(self.templates)
            self._render_live_preview()
            if close_after:
                sdlg.destroy()

        def send_raw_zpl(zpl_payload, description):
            ip_m = re.search(r"\((\d+\.\d+\.\d+\.\d+)(?::(\d+))?\)", chosen)
            if not ip_m:
                status_msg.config(text="No network IP found on target printer.", fg="#fca5a5")
                return
            ip = ip_m.group(1)
            port = int(ip_m.group(2) or 9100)
            import socket
            try:
                with socket.create_connection((ip, port), timeout=3.5) as s:
                    s.sendall(zpl_payload.encode("utf-8"))
                status_msg.config(text=f"✓ Sent {description} to {ip}:{port}!", fg="#86efac")
            except Exception as e:
                status_msg.config(text=f"Printer error: {e}", fg="#fca5a5")

        def do_test_print():
            save_settings(close_after=False)
            sample_item = self.selected_for_print[self.preview_idx] if getattr(self, "selected_for_print", None) else self.all_items[0]
            zpl_str = build_zpl_from_template(tpl_name, tpl, [dict(sample_item, qty=1)], copies=1)
            send_raw_zpl(zpl_str, f"1 Test Label (V:{v_var.get():+d}, H:{h_var.get():+d} dots)")

        def do_calibrate_gap():
            save_settings(close_after=False)
            send_raw_zpl("~JC\n^XA^JUS^XZ\n", "Gap Sensor Calibration (~JC)")

        btn_bar = tk.Frame(sdlg, bg="#1e293b")
        btn_bar.pack(fill="x", side="bottom", padx=18, pady=12)

        make_vt_btn(btn_bar, text="✓ Save & Close", bg="#16a34a", fg="#ffffff", font=("Sans", 10, "bold"), width=14, command=lambda: save_settings(True)).pack(side="right", padx=4)
        make_vt_btn(btn_bar, text="Save & Test Print 1 Label", bg="#0284c7", fg="#ffffff", font=("Sans", 10, "bold"), width=22, command=do_test_print).pack(side="right", padx=6)
        make_vt_btn(btn_bar, text="Calibrate Gap (~JC)", bg="#7c3aed", fg="#ffffff", font=("Sans", 10, "bold"), width=18, command=do_calibrate_gap).pack(side="left", padx=4)
        make_vt_btn(btn_bar, text="Cancel", bg="#475569", fg="#ffffff", font=("Sans", 10, "bold"), width=8, command=sdlg.destroy).pack(side="left", padx=4)

        sdlg.update_idletasks()
        sdlg.lift()
        sdlg.focus_force()

    def open_report_designer(self):
        tname = self.tpl_var.get()
        sample = self.selected_for_print[self.preview_idx]
        def on_saved(name, new_tpl):
            self.templates[name] = new_tpl
            save_templates(self.templates)
            self._render_live_preview()
        ReportDesignerWindow(self, tname, self.templates[tname], sample, on_saved)

    def clone_report(self):
        avail = discover_all_printers()
        base_tpl = json.loads(json.dumps(self.templates[self.tpl_var.get()]))

        cdlg = tk.Toplevel(self)
        cdlg.title("Create Label Report (Store / Printer Specific)")
        cdlg.geometry("520x290"); cdlg.after_idle(lambda d=cdlg: fit_dialog(d, 520, 290))
        cdlg.configure(bg="#1e293b")
        cdlg.transient(self)
        cdlg.attributes("-topmost", True)

        tk.Label(cdlg, text="Create New Label Template", bg="#1e293b", fg="#38bdf8", font=("Sans", 13, "bold")).pack(pady=(14, 8))
        frm = tk.Frame(cdlg, bg="#1e293b")
        frm.pack(fill="x", padx=20)

        tk.Label(frm, text="Label / Store Name:", bg="#1e293b", fg="#f8fafc", font=("Sans", 10, "bold")).grid(row=0, column=0, sticky="w", pady=6)
        name_var = tk.StringVar(value=f"{self.tpl_var.get()} - BHGAYLORDSVILLE")
        name_ent = tk.Entry(frm, textvariable=name_var, font=("Sans", 11, "bold"), width=30)
        name_ent.grid(row=0, column=1, sticky="w", padx=8, pady=6)
        name_ent.select_range(0, "end")

        tk.Label(frm, text="Target Printer:", bg="#1e293b", fg="#f8fafc", font=("Sans", 10, "bold")).grid(row=1, column=0, sticky="w", pady=6)
        cur_p = base_tpl.get("default_printer") or (avail[0] if avail else "PDF Preview (Desktop)")
        p_var = tk.StringVar(value=cur_p if cur_p in avail else (avail[0] if avail else "PDF Preview (Desktop)"))
        ttk.Combobox(frm, textvariable=p_var, values=avail, state="readonly", width=30, font=("Sans", 10)).grid(row=1, column=1, sticky="w", padx=8, pady=6)

        tk.Label(frm, text="Media Stock / Size:", bg="#1e293b", fg="#f8fafc", font=("Sans", 10, "bold")).grid(row=2, column=0, sticky="w", pady=6)
        cur_w, cur_h = float(base_tpl.get("width_in", 2.0)), float(base_tpl.get("height_in", 1.0))
        init_m = next((m[0] for m in MEDIA_STOCK_PRESETS if m[1] == cur_w and m[2] == cur_h), MEDIA_STOCK_LABELS[0])
        m_var = tk.StringVar(value=init_m)
        ttk.Combobox(frm, textvariable=m_var, values=MEDIA_STOCK_LABELS, state="readonly", width=30, font=("Sans", 10)).grid(row=2, column=1, sticky="w", padx=8, pady=6)

        def do_create(*_a):
            new_name = name_var.get().strip().upper() or f"CUSTOM_{len(self.templates)+1}"
            base_tpl["default_printer"] = p_var.get().strip()
            base_tpl["media_stock"] = m_var.get()
            for lbl, mw, mh, mtype in MEDIA_STOCK_PRESETS:
                if lbl == m_var.get() and mw is not None and mh is not None:
                    base_tpl["width_in"] = float(mw)
                    base_tpl["height_in"] = float(mh)
                    base_tpl["media_type"] = mtype
                    break
            self.templates[new_name] = base_tpl
            save_templates(self.templates)
            cdlg.destroy()
            self.show_step(4)
            self.tpl_var.set(new_name)
            self._render_live_preview()
            self.open_report_designer()

        name_ent.bind("<Return>", do_create)
        bf = tk.Frame(cdlg, bg="#1e293b")
        bf.pack(pady=16)
        make_vt_btn(bf, text="✓ Create & Open Designer", bg="#16a34a", fg="#ffffff", font=("Sans", 10, "bold"), width=22, command=do_create).pack(side="left", padx=8)
        make_vt_btn(bf, text="Cancel", bg="#475569", fg="#ffffff", font=("Sans", 10, "bold"), width=10, command=cdlg.destroy).pack(side="left", padx=8)
        cdlg.update_idletasks(); cdlg.lift(); name_ent.focus_force()

    def prompt_variable_text_if_needed(self, tpl):
        global GLOBAL_VAR_TEXT
        if not any(el.get("field") == "variable_text" for el in tpl.get("elements", [])):
            return
        vdlg = tk.Toplevel(self)
        vdlg.title("Variable Text")
        vdlg.geometry("420x170"); vdlg.after_idle(lambda d=vdlg: fit_dialog(d, 420, 170))
        vdlg.configure(bg="#1e293b")
        vdlg.transient(self)
        vdlg.attributes("-topmost", True)
        tk.Label(vdlg, text="Enter Line 1 Variable Text:", bg="#1e293b", fg="#f8fafc", font=("Sans", 11, "bold")).pack(pady=(18, 8))
        v_var = tk.StringVar(value=GLOBAL_VAR_TEXT)
        ent = tk.Entry(vdlg, textvariable=v_var, font=("Sans", 12, "bold"), width=28, justify="center")
        ent.pack(pady=4)
        ent.select_range(0, "end")
        def on_ok(*_a):
            global GLOBAL_VAR_TEXT
            if v_var.get().strip():
                GLOBAL_VAR_TEXT = v_var.get().strip().upper()
            vdlg.destroy()
        ent.bind("<Return>", on_ok)
        bf = tk.Frame(vdlg, bg="#1e293b")
        bf.pack(pady=12)
        make_vt_btn(bf, text="OK", bg="#16a34a", fg="#ffffff", font=("Sans", 10, "bold"), width=10, command=on_ok).pack(side="left", padx=6)
        make_vt_btn(bf, text="Skip", bg="#475569", fg="#ffffff", font=("Sans", 10, "bold"), width=10, command=vdlg.destroy).pack(side="left", padx=6)
        vdlg.update_idletasks()
        vdlg.lift()
        ent.focus_force()
        self.wait_window(vdlg)

    def open_print_dialog(self):
        tpl_name = self.tpl_var.get()
        tpl = self.templates[tpl_name]
        self.prompt_variable_text_if_needed(tpl)
        self._render_live_preview()

        items = getattr(self, "selected_for_print", None) or [self.filtered_items[0]]
        printers = discover_all_printers()
        chosen = resolve_template_printer(tpl.get("default_printer", ""), printers)
        if chosen != tpl.get("default_printer"):
            tpl["default_printer"] = chosen
            save_templates(self.templates)

        w_in = float(tpl.get("width_in", 2.0))
        h_in = float(tpl.get("height_in", 1.0))

        # Single-Step Print Prompt: Only asks "How many labels?" and prints directly to the template's assigned printer!
        qdlg = tk.Toplevel(self)
        qdlg.title("Label Utility — Print Labels")
        qdlg.geometry("500x295"); qdlg.after_idle(lambda d=qdlg: fit_dialog(d, 500, 295))
        qdlg.configure(bg="#1e293b")
        qdlg.transient(self)
        qdlg.attributes("-topmost", True)

        uses_product = any(el.get("field") in ("name", "upc", "barcode_img", "barcode_upca", "barcode_upce", "barcode_code39", "price_fmt", "manufacturer") for el in tpl.get("elements", []))
        if not uses_product:
            item_summary = f"Template: {tpl_name}"
        elif len(items) == 1:
            item_summary = items[0].get("name", tpl_name)
        else:
            item_summary = f"{len(items)} Selected Items ({tpl_name})"

        tk.Label(qdlg, text="How many labels do you want to print?", bg="#1e293b", fg="#f8fafc", font=("Sans", 13, "bold")).pack(pady=(14, 2))
        tk.Label(qdlg, text=item_summary, bg="#1e293b", fg="#14b8a6", font=("Sans", 10, "bold")).pack(pady=(0, 4))
        tk.Label(qdlg, text=f"Target: {chosen}   |   Media: {w_in} in x {h_in} in", bg="#0f172a", fg="#38bdf8", font=("Sans", 9, "bold"), padx=10, pady=3).pack(pady=(0, 8))

        default_qty = max(1, int(items[0].get("qty", 1)))
        qty_var = tk.StringVar(value=str(default_qty))

        spin_frame = tk.Frame(qdlg, bg="#1e293b")
        spin_frame.pack(pady=4)
        tk.Button(spin_frame, text=" - ", bg="#334155", fg="white", font=("Sans", 14, "bold"), width=3,
                  command=lambda: qty_var.set(str(max(1, int(qty_var.get() or 1) - 1)))).pack(side="left", padx=6)
        qty_ent = tk.Spinbox(spin_frame, from_=1, to=500, textvariable=qty_var, width=6, font=("Sans", 16, "bold"), justify="center")
        qty_ent.pack(side="left", padx=6)
        tk.Button(spin_frame, text=" + ", bg="#334155", fg="white", font=("Sans", 14, "bold"), width=3,
                  command=lambda: qty_var.set(str(min(500, int(qty_var.get() or 1) + 1)))).pack(side="left", padx=6)

        preset_frame = tk.Frame(qdlg, bg="#1e293b")
        preset_frame.pack(pady=6)
        tk.Label(preset_frame, text="Quick Qty:", bg="#1e293b", fg="#94a3b8", font=("Sans", 9, "bold")).pack(side="left", padx=4)
        for preset in (1, 5, 10, 20, 25, 50):
            make_vt_btn(preset_frame, text=str(preset), bg="#0f172a", fg="#38bdf8", font=("Sans", 10, "bold"), width=3, padx=4, pady=3, radius=8, command=lambda v=preset: qty_var.set(str(v))).pack(side="left", padx=3)

        status_lbl = tk.Label(qdlg, text="Press Enter or tap Print Now to send directly to assigned printer.", bg="#1e293b", fg="#94a3b8", font=("Sans", 9, "italic"))
        status_lbl.pack(pady=(2, 2))

        def show_print_result(title, msg, is_err=False):
            res_dlg = tk.Toplevel(self)
            res_dlg.title(title)
            res_dlg.geometry("460x165"); res_dlg.after_idle(lambda d=res_dlg: fit_dialog(d, 460, 165))
            res_dlg.configure(bg="#1e293b")
            res_dlg.transient(self)
            res_dlg.attributes("-topmost", True)
            tk.Label(res_dlg, text=msg, bg="#1e293b", fg="#fca5a5" if is_err else "#86efac", font=("Sans", 11, "bold"), wraplength=420, justify="center").pack(pady=(22, 14))
            if is_err:
                make_vt_btn(res_dlg, text="OK", bg="#2563eb", fg="#ffffff", font=("Sans", 11, "bold"), width=10, command=res_dlg.destroy).pack()
            else:
                tk.Label(res_dlg, text="Closing automatically...", bg="#1e293b", fg="#94a3b8", font=("Sans", 9, "italic")).pack()
                res_dlg.after(1400, lambda: res_dlg.destroy() if res_dlg.winfo_exists() else None)
            res_dlg.update_idletasks()
            res_dlg.lift()
            res_dlg.focus_force()

        def do_print(*_a):
            try:
                copies = max(1, int(qty_var.get() or 1))
            except ValueError:
                copies = 1
            status_lbl.config(text=f"Sending {len(items)} item(s) x {copies} label(s) to {chosen}...", fg="#38bdf8")
            qdlg.update_idletasks()

            import threading
            def worker():
                try:
                    ok, msg, pdf = send_label_job(tpl_name, tpl, items, copies, chosen)
                    if pdf:
                        subprocess.Popen(["xdg-open", pdf])
                        self.after(0, qdlg.destroy)
                    elif ok:
                        self.after(0, lambda: (qdlg.destroy(), show_print_result("Printed", msg)))
                    else:
                        self.after(0, lambda: (qdlg.destroy(), show_print_result("Printer Error", msg, True)))
                except Exception as e:
                    self.after(0, lambda err=str(e): (qdlg.destroy(), show_print_result("Printer Error", f"Print failed: {err}", True)))

            threading.Thread(target=worker, daemon=True).start()

        qty_ent.bind("<Return>", do_print)
        qbtn_frame = tk.Frame(qdlg, bg="#1e293b")
        qbtn_frame.pack(pady=(6, 12))
        make_vt_btn(qbtn_frame, text="Print Now", bg="#16a34a", fg="#ffffff", font=("Sans", 11, "bold"), width=15, command=do_print).pack(side="left", padx=8)
        make_vt_btn(qbtn_frame, text="⚙️ Adjust Higher / Lower", bg="#0284c7", fg="#ffffff", font=("Sans", 10, "bold"), command=lambda: (qdlg.destroy(), self.open_printer_settings_dialog())).pack(side="left", padx=6)
        make_vt_btn(qbtn_frame, text="Cancel", bg="#475569", fg="#ffffff", font=("Sans", 11, "bold"), width=8, command=qdlg.destroy).pack(side="left", padx=6)

        qdlg.update_idletasks()
        qdlg.lift()
        qty_ent.selection_range(0, "end")
        qty_ent.focus_force()

    def go_next(self):
        _trace.info("go_next on screen %s: %d items shown, %d ticked", self.current_step,
                    len(getattr(self, "filtered_items", []) or []),
                    sum(1 for it in (getattr(self, "filtered_items", []) or []) if it.get("selected")))
        if self.current_step == 1:
            mode = getattr(self, 'mode_var_val', 'tree')
            if hasattr(self, 'mode_var'): mode = self.mode_var.get() or mode
            self.select_mode_and_go(mode)
            return
        elif self.current_step == 2:
            sel = self.cat_tree.selection()
            if sel:
                vals = list(self.cat_tree.item(sel[0], "values") or [])
                kind = vals[0] if vals else "ALL"
                # Each level also matches the levels above it (a brand under one category, not every category).
                fields = {"SGRP": ["sub_grp", "prod_grp", "manufacturer", "category"],
                          "PGRP": ["prod_grp", "manufacturer", "category"],
                          "MFG": ["manufacturer", "category"],
                          "CAT": ["category"],
                          "DG": ["dept_group"]}.get(kind)
                blank = {"dept_group": "RETAIL", "category": "GENERAL", "manufacturer": "GENERAL"}   # as the tree shows them
                if fields:
                    want = list(zip(fields, vals[1:]))
                    self.filtered_items = [it for it in self.all_items
                                           if all((it.get(f) or blank.get(f, "")) == str(v) for f, v in want)]
                else:
                    self.filtered_items = list(self.all_items)
            else:
                self.filtered_items = list(self.all_items)
        self.show_step(min(4, self.current_step + 1))

    def go_prev(self):
        if self.current_step == 4 and getattr(self, 'came_from_step', None) == 1:
            self.came_from_step = None; self.show_step(1); return
        if self.current_step == 3 and getattr(self, 'from_top_button', False):
            self.from_top_button = False; self.show_step(1); return
        if self.current_step == 2: self.show_step(1); return
        self.show_step(max(1, self.current_step - 1))

START_UPC = ""

if __name__ == "__main__":
    import argparse
    _ap = argparse.ArgumentParser(description="ViewTouch Label Utility")
    _ap.add_argument("--upc", default="", help="open straight to the label preview for this UPC")
    _ap.add_argument("--print-job", default="", help="print a job file from ViewTouch's Labels screen, with no window")
    _ap.add_argument("--preview-job", default="", help="draw one label of a job file as a PNG (ViewTouch's Labels screen preview)")
    _ap.add_argument("--out", default="", help="the PNG --preview-job writes")
    _args, _unknown = _ap.parse_known_args()
    if _args.print_job:
        sys.exit(run_print_job(_args.print_job))
    if _args.preview_job:
        sys.exit(run_preview_job(_args.preview_job, _args.out or _args.preview_job + ".png"))
    START_UPC = _args.upc.strip()
    app = VTLabelUtility()
    app.mainloop()
