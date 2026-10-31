import re
#!/usr/bin/env python3
"""
ViewTouch Label Utility & Report Designer (Sandbox Edition)
Reads 100% from /home/pi/.local/share/ViewTouch/ViewTouch/viewtouch.db (Read-Only)
Includes interactive 8-handle resizable field boxes (W x H + Font Size) in the Report Designer.
"""
import datetime, glob, json, os, sqlite3, subprocess, tkinter as tk
from tkinter import ttk, messagebox
from reportlab.pdfgen import canvas
from reportlab.lib.units import inch
from reportlab.graphics.barcode import code128

SANDBOX_DIR = os.path.expanduser("~/.local/share/ViewTouch")
TEMPLATE_FILE = os.path.join(SANDBOX_DIR, "vt_label_templates_v2.json")
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

def load_templates():
    if os.path.exists(TEMPLATE_FILE):
        try:
            with open(TEMPLATE_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
                for k, v in DEFAULT_TEMPLATES.items():
                    if k not in data:
                        data[k] = v
                    ensure_box_dims(data[k])
                return data
        except Exception:
            pass
    return json.loads(json.dumps(DEFAULT_TEMPLATES))

def save_templates(tpls):
    os.makedirs(SANDBOX_DIR, exist_ok=True)
    with open(TEMPLATE_FILE, "w", encoding="utf-8") as f:
        json.dump(tpls, f, indent=2)

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

    key = (name.upper(), upc)
    if key not in seen:
        seen.add(key)
        items.append({
            "selected": False,
            "qty": 1,
            "pack_qty": pack_qty,
            "upc": upc,
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

def render_label_pil_image(tpl, item, dpi=203):
    """Renders the exact label to a 1-bit PIL Image at 203 DPI using the same TrueType fonts and box-fitting as the screen."""
    from PIL import Image, ImageDraw, ImageFont
    w_dots = max(50, int(round(float(tpl.get("width_in", 2.0)) * dpi)))
    h_dots = max(40, int(round(float(tpl.get("height_in", 1.0)) * dpi)))
    img = Image.new("L", (w_dots, h_dots), 255)
    draw = ImageDraw.Draw(img)

    v_off = int(tpl.get("v_offset_dots", 12) if "v_offset_dots" in tpl else 12)
    h_off = int(tpl.get("h_offset_dots", 0) or 0)

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

    for el in tpl.get("elements", []):
        if el.get("visible", True) is False:
            continue
        bx = int(round(float(el.get("x", 0.04)) * dpi)) + h_off
        by = int(round(float(el.get("y", 0.04)) * dpi)) + v_off
        bw = max(16, int(round(float(el.get("w", 0.8)) * dpi)))
        bh = max(12, int(round(float(el.get("h", 0.18)) * dpi)))
        field = el.get("field", "name")

        if str(field).startswith("barcode"):
            # Let build_zpl_from_template emit crisp native ^BC / ^BU barcode on top, or draw bars here
            continue

        raw_txt = str(format_item_field(item, field, el) or "").strip()
        if not raw_txt:
            continue

        fam = el.get("font_family", "Sans")
        bold = bool(el.get("bold", True))
        italic = bool(el.get("italic", False))
        do_wrap = bool(el.get("word_wrap", field == "name"))
        auto_font = bool(el.get("auto_font", True))

        # Match exact fit_text_into_box logic used by render_label_on_tk_canvas!
        if do_wrap and len(raw_txt) > 18 and " " in raw_txt:
            text_to_draw = split_two_lines(raw_txt)
            lines = text_to_draw.splitlines()
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
            text_to_draw = raw_txt
            lines = [raw_txt]
            start_px = max(10, int(bh * 0.82)) if auto_font else max(8, int(float(el.get("size", 12)) * (dpi / 72.0)))
            chosen_fnt = get_ttf(fam, bold, italic, 10)
            for px in range(start_px, 7, -1):
                fnt = get_ttf(fam, bold, italic, px)
                bb = draw.textbbox((0, 0), text_to_draw, font=fnt)
                tw = bb[2] - bb[0]
                th = bb[3] - bb[1]
                if tw <= (bw - 4) and th <= (bh - 1):
                    chosen_fnt = fnt
                    break

        align = el.get("align", "center")
        # Calculate exact bounding box of rendered text so we center it inside [bx, by, bx+bw, by+bh]
        line_bboxes = [draw.textbbox((0, 0), ln, font=chosen_fnt) for ln in lines]
        line_heights = [bb[3] - bb[1] for bb in line_bboxes]
        total_h = sum(line_heights) + (4 * (len(lines) - 1))
        cur_y = by + (bh - total_h) / 2.0

        for ln, bb, lh in zip(lines, line_bboxes, line_heights):
            lw_px = bb[2] - bb[0]
            if align == "left":
                tx = bx + 2 - bb[0]
            elif align == "right":
                tx = bx + bw - lw_px - 2 - bb[0]
            else:
                tx = bx + (bw - lw_px) / 2.0 - bb[0]
            ty = cur_y - bb[1]
            draw.text((tx, ty), ln, font=chosen_fnt, fill=0)
            cur_y += lh + 4

    return img.point(lambda p: 0 if p < 160 else 1, mode="1")


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
    dpi = int(tpl.get("printer_dpi", 203) or 203)
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
        # 1. Render all text elements with 100% WYSIWYG TrueType box-fitting via ^GFA
        try:
            img_1b = render_label_pil_image(tpl, item, dpi=dpi)
            lines.append(pil_1bit_to_zpl_gfa(img_1b))
        except Exception as e:
            print("PIL ZPL fallback note:", e)

        # 2. Overlay crisp native hardware ZPL barcodes if the template has any barcode elements
        for el in tpl.get("elements", []):
            if el.get("visible", True) is False:
                continue
            f_type = el.get("field", "name")
            if str(f_type).startswith("barcode"):
                x_d = max(0, int(round(float(el.get("x", 0.04)) * dpi)) + h_offset_dots)
                y_d = max(0, int(round(float(el.get("y", 0.04)) * dpi)) + v_offset_dots)
                h_d = max(16, int(round(float(el.get("h", 0.18)) * dpi)))
                raw_c = str(item.get("upc", "000000000000") or "000000000000").strip()
                bh_dots = max(20, int(h_d * 0.82))
                lines.append(f"^FO{x_d},{y_d}^BY2,2,{bh_dots}^BCN,{bh_dots},N,N,N^FD{raw_c}^FS")

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
                        w_bc = eanbc.UpcA(u12, barHeight=bh*0.85, barWidth=max(0.55, bw/115.0))
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

def draw_code128_on_tk_canvas(canvas_widget, value, x, y, w, h, tag=None):
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
            b_val = format_item_field(item, "upc") or "000000000000"
            if field == "barcode_upca": b_val = normalize_upc(b_val)[:12].zfill(12)
            elif field == "barcode_upce": b_val = (item.get("upce") or normalize_upc(b_val)[:8]).zfill(8)
            elif field == "barcode_code39": b_val = f"*{str(b_val).upper()}*"
            draw_code128_on_tk_canvas(canvas_widget, b_val, bx, by, bw, bh, tag=f"el_{idx}")
            if field in ("barcode_upca", "barcode_upce", "barcode_code39"):
                canvas_widget.create_rectangle(bx+4, by+bh-13, bx+bw-4, by+bh-1, fill="#ffffff", outline="", tags=(f"el_{idx}",))
                canvas_widget.create_text(bx+bw//2, by+bh-7, text=b_val, font=("Monospace", -9, "bold"), fill="#000000", tags=(f"el_{idx}",))
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
        self.geometry("1920x1080+0+0"); self.transient(master); self.attributes("-fullscreen", True); self.attributes("-topmost", True); self.lift(); self.focus_force()
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
        dlg = tk.Toplevel(self); dlg.title("Add Field to Label"); dlg.geometry("420x200+750+400"); dlg.configure(bg="#1e242d"); dlg.transient(self); dlg.attributes("-topmost", True)
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
        dlg = tk.Toplevel(self); dlg.title(f"Component Properties — {fk_cur}"); dlg.geometry("480x360+720+340"); dlg.configure(bg="#1e293b"); dlg.transient(self); dlg.attributes("-topmost", True)
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
        tbar1 = tk.Frame(self, bg="#1e242d", bd=1, relief="raised")
        tbar1.pack(fill="x", side="top")

        make_vt_btn(tbar1, text="💾 Save", bg="#16a34a", fg="#ffffff", font=("Sans", 10, "bold"), command=self.save_and_close).pack(side="left", padx=4, pady=4)
        make_vt_btn(tbar1, text="↶ Undo", bg="#334155", fg="#ffffff", font=("Sans", 10, "bold"), command=self.undo).pack(side="left", padx=2)
        make_vt_btn(tbar1, text="↷ Redo", bg="#334155", fg="#ffffff", font=("Sans", 10, "bold"), command=self.redo).pack(side="left", padx=2)

        tk.Label(tbar1, text="Object:", bg="#1e242d", fg="#38bdf8", font=("Sans", 10, "bold")).pack(side="left", padx=(6, 2))
        self.el_var = tk.StringVar()
        self.el_cb = ttk.Combobox(tbar1, textvariable=self.el_var, state="readonly", width=16, font=("Sans", 10, "bold"))
        self.el_cb.pack(side="left", padx=2)
        self.el_cb.bind("<<ComboboxSelected>>", self.on_pick_element)

        tk.Label(tbar1, text="Data Field:", bg="#1e242d", fg="#38bdf8", font=("Sans", 10, "bold")).pack(side="left", padx=(6, 2))
        self.ftype_var = tk.StringVar()
        self.ftype_cb = ttk.Combobox(tbar1, textvariable=self.ftype_var, values=GLOBAL_FIELD_LABELS, state="readonly", width=25, font=("Sans", 10))
        self.ftype_cb.pack(side="left", padx=2)
        self.ftype_cb.bind("<<ComboboxSelected>>", self.on_change_field_type)

        tk.Label(tbar1, text="Text:", bg="#1e242d", fg="#cbd5e1", font=("Sans", 10)).pack(side="left", padx=(4, 2))
        self.custom_text_var = tk.StringVar(value="")
        ct_ent = tk.Entry(tbar1, textvariable=self.custom_text_var, width=11, font=("Sans", 10, "bold"))
        ct_ent.pack(side="left", padx=2)
        ct_ent.bind("<Return>", self.apply_custom_text)
        ct_ent.bind("<FocusOut>", self.apply_custom_text)

        tk.Label(tbar1, text="Font:", bg="#1e242d", fg="#cbd5e1", font=("Sans", 10)).pack(side="left", padx=(4, 2))
        self.font_fam_var = tk.StringVar(value="Sans")
        ff_cb = ttk.Combobox(tbar1, textvariable=self.font_fam_var, values=font_family_choices(), state="readonly", width=16)
        ff_cb.pack(side="left", padx=2); ff_cb.bind("<<ComboboxSelected>>", lambda _e: self.apply_prop_change())

        self.size_var = tk.StringVar(value="12")
        sz_spin = tk.Spinbox(tbar1, from_=5, to=72, textvariable=self.size_var, width=3, font=("Sans", 10), command=self.apply_prop_change)
        sz_spin.pack(side="left", padx=2)
        sz_spin.bind("<Return>", lambda _e: self.apply_prop_change())

        self.bold_var = tk.BooleanVar(value=True)
        tk.Checkbutton(tbar1, text="B", variable=self.bold_var, bg="#1e242d", fg="white", selectcolor="#0f172a", font=("Sans", 10, "bold"), command=self.apply_prop_change).pack(side="left", padx=1)
        self.italic_var = tk.BooleanVar(value=False)
        tk.Checkbutton(tbar1, text="I", variable=self.italic_var, bg="#1e242d", fg="white", selectcolor="#0f172a", font=("Sans", 10, "italic"), command=self.apply_prop_change).pack(side="left", padx=1)

        self.align_var = tk.StringVar(value="center")
        al_cb = ttk.Combobox(tbar1, textvariable=self.align_var, values=["left", "center", "right"], state="readonly", width=6)
        al_cb.pack(side="left", padx=2)
        al_cb.bind("<<ComboboxSelected>>", lambda _e: self.apply_prop_change())

        make_vt_btn(tbar1, text="✖ Close", bg="#dc2626", fg="#ffffff", font=("Sans", 10, "bold"), command=self.on_close_request).pack(side="right", padx=6)

        # Row 2 Toolbar: ILSA-Style Component Palette (Click Icon -> Click Label Canvas to Drop Component)
        tbar_pal = tk.Frame(self, bg="#0f172a", bd=1, relief="raised")
        tbar_pal.pack(fill="x", side="top")
        tk.Label(tbar_pal, text="🧰 Component Palette (Click Tool, Then Click Label):", bg="#0f172a", fg="#38bdf8", font=("Sans", 9, "bold")).pack(side="left", padx=8, pady=3)
        palette_items = [
            ("🔤 Label", "static_label"),
            ("🏷️ DBText (Desc)", "name"),
            ("💲 DBText (Price)", "price_fmt"),
            ("🏭 DBText (Brand)", "manufacturer"),
            ("🔢 DBText (UPC)", "upc"),
            ("📅 SysVar (Date)", "date_yymmdd"),
            ("👤 SysVar (Emp)", "emp_initials"),
            ("📝 Variable", "variable_text"),
            ("🧮 DBCalc", "db_calc"),
            ("║▌ DBBarCode (128)", "barcode_img"),
            ("║▌ UPC-A", "barcode_upca"),
        ]
        for btn_lbl, fk in palette_items:
            tk.Button(
                tbar_pal, text=btn_lbl, bg="#1e293b", fg="#f8fafc", activebackground="#2563eb", activeforeground="white",
                font=("Sans", 9, "bold"), padx=5, pady=1,
                command=lambda k=fk, l=btn_lbl: self.arm_palette_tool(k, l)
            ).pack(side="left", padx=2, pady=2)

        # Row 3 Toolbar: Box Dimensions, WordWrap, Auto-Fit, Layer Order, Duplicate, Rotate, Delete, Label Size
        tbar2 = tk.Frame(self, bg="#14171c", bd=1, relief="raised")
        tbar2.pack(fill="x", side="top")

        tk.Label(tbar2, text="📐 Box W:", bg="#14171c", fg="#cbd5e1", font=("Sans", 9, "bold")).pack(side="left", padx=(6, 2), pady=3)
        self.box_w_var = tk.StringVar(value="1.00")
        bw_spin = tk.Spinbox(tbar2, from_=0.10, to=4.00, increment=0.05, format="%.2f", textvariable=self.box_w_var, width=5, font=("Sans", 10), command=self.apply_box_spin_change)
        bw_spin.pack(side="left", padx=2)
        bw_spin.bind("<Return>", lambda _e: self.apply_box_spin_change())

        tk.Label(tbar2, text="Box H:", bg="#14171c", fg="#cbd5e1", font=("Sans", 9, "bold")).pack(side="left", padx=(6, 2))
        self.box_h_var = tk.StringVar(value="0.20")
        bh_spin = tk.Spinbox(tbar2, from_=0.08, to=3.00, increment=0.02, format="%.2f", textvariable=self.box_h_var, width=5, font=("Sans", 10), command=self.apply_box_spin_change)
        bh_spin.pack(side="left", padx=2)
        bh_spin.bind("<Return>", lambda _e: self.apply_box_spin_change())

        self.autofit_font_var = tk.BooleanVar(value=True)
        tk.Checkbutton(tbar2, text="AutoSize Font", variable=self.autofit_font_var, bg="#14171c", fg="#f8fafc", selectcolor="#0f172a", font=("Sans", 9, "bold"), command=self.on_toggle_autofit).pack(side="left", padx=6)

        self.word_wrap_var = tk.BooleanVar(value=False)
        tk.Checkbutton(tbar2, text="WordWrap (2-Line)", variable=self.word_wrap_var, bg="#14171c", fg="#f8fafc", selectcolor="#0f172a", font=("Sans", 9, "bold"), command=self.toggle_word_wrap).pack(side="left", padx=4)

        make_vt_btn(tbar2, text="⬆ Front", bg="#334155", fg="#ffffff", font=("Sans", 9, "bold"), command=self.bring_to_front).pack(side="left", padx=2)
        make_vt_btn(tbar2, text="⬇ Back", bg="#334155", fg="#ffffff", font=("Sans", 9, "bold"), command=self.send_to_back).pack(side="left", padx=2)
        make_vt_btn(tbar2, text="🔄 Rotate", bg="#7c3aed", fg="#ffffff", font=("Sans", 9, "bold"), command=self.rotate_selected).pack(side="left", padx=2)
        make_vt_btn(tbar2, text="📑 Dup", bg="#d97706", fg="#ffffff", font=("Sans", 9, "bold"), command=self.duplicate_selected).pack(side="left", padx=2)
        make_vt_btn(tbar2, text="🗑️ Del", bg="#dc2626", fg="#ffffff", font=("Sans", 9, "bold"), command=self.delete_field).pack(side="left", padx=2)

        make_vt_btn(tbar2, text="Apply Size", bg="#0d9488", fg="#ffffff", font=("Sans", 9, "bold"), command=self.resize_label).pack(side="right", padx=4)
        self.lh_var = tk.StringVar(value=str(self.tpl["height_in"]))
        self.lw_var = tk.StringVar(value=str(self.tpl["width_in"]))
        tk.Entry(tbar2, textvariable=self.lh_var, width=4, font=("Sans", 9, "bold")).pack(side="right", padx=1)
        tk.Label(tbar2, text="x", bg="#14171c", fg="white").pack(side="right")
        tk.Entry(tbar2, textvariable=self.lw_var, width=4, font=("Sans", 9, "bold")).pack(side="right", padx=1)

        # Media Stock Preset Selector in Designer
        cur_w, cur_h = float(self.tpl.get("width_in", 2.0)), float(self.tpl.get("height_in", 1.0))
        init_media = next((m[0] for m in MEDIA_STOCK_PRESETS if m[1] == cur_w and m[2] == cur_h), self.tpl.get("media_stock", "Custom Size (W x H in)"))
        self.media_var = tk.StringVar(value=init_media)
        media_cb = ttk.Combobox(tbar2, textvariable=self.media_var, values=MEDIA_STOCK_LABELS, state="readonly", width=22, font=("Sans", 9, "bold"))
        media_cb.pack(side="right", padx=4)
        media_cb.bind("<<ComboboxSelected>>", self.on_pick_media_preset)
        tk.Label(tbar2, text="📏 Stock:", bg="#14171c", fg="#38bdf8", font=("Sans", 9, "bold")).pack(side="right", padx=2)

        # Target Printer Selector in Designer (Saved directly with this Label Template!)
        avail_printers = discover_all_printers()
        saved_p = self.tpl.get("default_printer", "")
        if not saved_p or saved_p not in avail_printers:
            saved_p = avail_printers[0] if avail_printers else "PDF Preview (Desktop)"
            self.tpl["default_printer"] = saved_p
        self.designer_printer_var = tk.StringVar(value=saved_p)
        prn_cb = ttk.Combobox(tbar2, textvariable=self.designer_printer_var, values=avail_printers, state="readonly", width=28, font=("Sans", 9, "bold"))
        prn_cb.pack(side="right", padx=4)
        prn_cb.bind("<<ComboboxSelected>>", self.on_pick_designer_printer)
        tk.Label(tbar2, text="🖨️ Target Printer:", bg="#14171c", fg="#14b8a6", font=("Sans", 9, "bold")).pack(side="right", padx=(8, 2))

        # Designer Gray Workspace
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
        self.status_var.set(f"🖨️ Target Printer for '{self.tpl_name}' set to: {chosen_p} (Save report to lock in)")

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
                self.status_var.set(f"📏 Media Stock set to: {choice} ({mw} in x {mh} in)")
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
        dlg.geometry("460x190+730+420")
        dlg.configure(bg="#1e293b")
        dlg.transient(self)
        dlg.attributes("-topmost", True)
        tk.Label(dlg, text=f"Do you want to save changes to '{self.tpl_name}' before closing?", bg="#1e293b", fg="#f8fafc", font=("Sans", 12, "bold"), wraplength=420, justify="center").pack(pady=(22, 18))
        bf = tk.Frame(dlg, bg="#1e293b")
        bf.pack(pady=6)
        tk.Button(bf, text="💾 Yes, Save", bg="#16a34a", fg="white", font=("Sans", 11, "bold"), width=12, command=lambda: (dlg.destroy(), self.on_save_callback(self.tpl_name, self.tpl), self.destroy())).pack(side="left", padx=6)
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
            self.create_line(bx1 + r, by1 + 1, bx2 - r, by1 + 1, fill="#ffffff", stipple="gray25")

        cx = (bx1 + bx2) / 2.0
        cy = (by1 + by2) / 2.0
        self.create_text(cx, cy, text=self.text, font=self.font, fill=cur_fg, justify="center")


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
        self.title("ViewTouch Label Utility (Live — Connected to viewtouch.db)")
        self.geometry("1920x1080+0+0"); self.attributes("-fullscreen", True)
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

        self.content = tk.Frame(self, bg="#1e242d")
        self.content.pack(fill="both", expand=True, padx=6, pady=4)

        bbar = tk.Frame(self, bg="#0c1017", height=54)
        bbar.pack(fill="x", side="bottom", padx=12, pady=6)

        self.close_btn = make_vt_btn(bbar, text="✖ Close", bg="#dc2626", fg="#ffffff", font=("Sans", 10, "bold"), width=14, pady=8, radius=12, command=self.destroy)
        self.close_btn.pack(side="left", padx=(4, 6), pady=4)

        self.reload_btn = make_vt_btn(bbar, text="↻ Reload viewtouch.db", bg="#1d4ed8", fg="#ffffff", font=("Sans", 10, "bold"), width=20, pady=8, radius=12, command=self.reload_vt_db)
        self.reload_btn.pack(side="left", padx=6, pady=4)

        self.next_btn = make_vt_btn(bbar, text="Next >>", bg="#047857", fg="#ffffff", font=("Sans", 10, "bold"), width=14, pady=8, radius=12, command=self.go_next)
        self.next_btn.pack(side="right", padx=(6, 4), pady=4)

        self.prev_btn = make_vt_btn(bbar, text="<< Previous", bg="#0f766e", fg="#ffffff", font=("Sans", 10, "bold"), width=14, pady=8, radius=12, command=self.go_prev)
        self.prev_btn.pack(side="right", padx=6, pady=4)

        self.show_step(1)

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
            parent=self, title="Select Label Template (.json, .prn, .nlbl, .slnx)",
            initialdir=init_dir,
            filetypes=[("Label Templates", "*.json *.prn *.nlbl *.slnx"), ("All Files", "*.*")]
        )
        if not path: return
        chosen_tpl = None
        if path.endswith(".json"):
            try:
                loaded = json.load(open(path, "r", encoding="utf-8"))
                if isinstance(loaded, dict) and loaded:
                    self.templates.update(loaded)
                    save_templates(self.templates)
                    chosen_tpl = list(loaded.keys())[-1]
            except Exception as e:
                messagebox.showerror("Error", f"Could not load JSON: {e}"); return
        else:
            fn = os.path.basename(path).upper()
            chosen_tpl = "DATE LABEL 2UP MR" if ("2UP" in fn and "MR" in fn) else ("DATE LABEL 2UP" if "2UP" in fn else ("DESCRIPTION LABEL" if "DESC" in fn else list(self.templates.keys())[-1]))
        if chosen_tpl and chosen_tpl in self.templates:
            self.came_from_step = self.current_step
            self.show_step(4)
            self.tpl_var.set(chosen_tpl)
            self._render_live_preview()

    def open_template_folder(self):
        folder = os.path.expanduser("~/viewtouch-main/modern/seed/templates")
        subprocess.Popen(["xdg-open", folder if os.path.exists(folder) else os.path.expanduser("~/.local/share/ViewTouch")])
    def show_step(self, step):
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
        pane.rowconfigure(0, weight=3)
        pane.rowconfigure(1, weight=5)

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
                command=lambda v=m_val: select_mode(v)
            )
            b.grid(row=0, column=col_idx, sticky="nsew", padx=4, pady=3)
            self.mode_btn_widgets[m_val] = (b, dim_c, act_c)

        # 3x3 Template Matrix
        tpl_matrix_frame = tk.LabelFrame(
            pane, text=" LABEL TEMPLATES (3x3 MATRIX) ", bg="#0c1017", fg="#38bdf8",
            font=("Sans", 10, "bold"), bd=1, relief="solid", padx=8, pady=6
        )
        tpl_matrix_frame.grid(row=1, column=0, columnspan=8, sticky="nsew", padx=(4, 6), pady=2)

        for c in range(3):
            tpl_matrix_frame.columnconfigure(c, weight=1, uniform="tpl_c")
        for r in range(3):
            tpl_matrix_frame.rowconfigure(r, weight=1, uniform="tpl_r")

        active_tpl_names = list(self.templates.keys())
        tpl_colors = [
            "#1d4ed8", "#047857", "#0f766e", "#4338ca",
            "#0284c7", "#b45309", "#0e7490", "#334155", "#1e293b"
        ]

        for slot in range(9):
            row_idx = slot // 3
            col_idx = slot % 3
            if slot < len(active_tpl_names):
                t_name = active_tpl_names[slot]
                t_data = self.templates[t_name]
                w_in = t_data.get("width_in", 2.0)
                h_in = t_data.get("height_in", 1.0)
                p_dest = t_data.get("default_printer", "Auto")
                short_name = t_name.replace("VINYL", "").replace("W/BARCODE", "").strip() or t_name
                prn_short = p_dest.split("(")[0].replace("VT TCP:", "").strip()
                lbl_text = f"{short_name}\n{w_in} in x {h_in} in\n[{prn_short}]"
                c_bg = tpl_colors[slot % len(tpl_colors)]

                tbtn = make_vt_btn(
                    tpl_matrix_frame,
                    text=lbl_text,
                    font=("Sans", 9, "bold"),
                    bg=c_bg,
                    fg="#ffffff",
                    radius=14,
                    command=lambda tn=t_name: self.select_template_and_go(tn)
                )
                tbtn.grid(row=row_idx, column=col_idx, sticky="nsew", padx=4, pady=4)
            else:
                empty_btn = make_vt_btn(
                    tpl_matrix_frame,
                    text=f"+ Slot {slot+1}\n[Create in Editor]",
                    font=("Sans", 9, "bold"),
                    bg="#1e293b",
                    fg="#94a3b8",
                    radius=14,
                    command=self.clone_report
                )
                empty_btn.grid(row=row_idx, column=col_idx, sticky="nsew", padx=4, pady=4)

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
        root_node = self.cat_tree.insert("", "end", text="VIEWTOUCH CATALOG (viewtouch.db)", open=True, values=("ALL", ""))
        tree_map = {}
        for it in self.all_items:
            if filter_cat and it["category"] != filter_cat:
                continue
            dg = it["dept_group"]
            cat = it["category"]
            mfg = it["manufacturer"]
            tree_map.setdefault(dg, {}).setdefault(cat, set()).add(mfg)

        for dg in sorted(tree_map.keys()):
            dg_id = self.cat_tree.insert(root_node, "end", text=dg, open=True, values=("DG", dg))
            for cat in sorted(tree_map[dg].keys()):
                cat_id = self.cat_tree.insert(dg_id, "end", text=cat, open=True, values=("CAT", cat))
                for mfg in sorted(tree_map[dg][cat]):
                    self.cat_tree.insert(cat_id, "end", text=f"M  {mfg}", values=("MFG", mfg))

    def _build_step3(self):
        top = tk.Frame(self.content, bg="#1e242d")
        top.pack(fill="x", padx=8, pady=6)

        tk.Label(top, text="Barcode:", bg="#1e242d", font=("Sans", 11)).pack(side="left")
        self.bc_type_var = tk.StringVar(value="UPC-A")
        ttk.Combobox(top, textvariable=self.bc_type_var, values=["UPC-A", "UPC-E", "Code-39", "Code-128"], width=10, state="readonly").pack(side="left", padx=6)

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

        cols = ("sel", "qty", "upca", "upce", "desc", "price", "dept_name", "dept_num", "dept_grp", "cat", "mfg")
        self.grid_tree = ttk.Treeview(self.content, columns=cols, show="headings", selectmode="browse")
        headers = [
            ("sel", "Selected", 70), ("qty", "Print Qty", 70), ("upca", "UPC / Barcode", 130),
            ("upce", "Alt UPC", 90), ("desc", "Description", 250), ("price", "Price", 75),
            ("dept_name", "Category", 130), ("dept_num", "Item #", 75),
            ("dept_grp", "Group", 105), ("cat", "Family", 115), ("mfg", "Brand / Mfg", 115)
        ]
        for c_id, h_txt, w in headers:
            self.grid_tree.heading(c_id, text=h_txt)
            self.grid_tree.column(c_id, width=w, anchor="center" if c_id in ("sel", "qty", "dept_num") else "w")

        self.grid_tree.pack(fill="both", expand=True, padx=8, pady=4)
        self.grid_tree.bind("<ButtonRelease-1>", self.on_toggle_row)
        self._populate_grid()

    def _populate_grid(self):
        self.grid_tree.delete(*self.grid_tree.get_children())
        q = getattr(self, "filter_var", tk.StringVar()).get().strip().lower()
        for idx, it in enumerate(self.filtered_items):
            if q and (q not in it["name"].lower() and q not in it["upc"].lower() and q not in it["manufacturer"].lower() and q not in it["category"].lower()):
                continue
            chk = "☑" if it.get("selected") else "☐"
            self.grid_tree.insert("", "end", iid=str(idx), values=(
                chk, it["qty"], it["upc"], it["upce"], it["name"], f"${it['price']:.2f}",
                it["dept_name"], it["dept_num"], it["dept_group"], it["category"], it["manufacturer"]
            ))

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

        make_vt_btn(tbar, text="🖨️ Print Labels", bg="#16a34a", fg="#ffffff", font=("Sans", 10, "bold"), width=13, pady=8, command=self.open_print_dialog).pack(side="left", padx=6)

        make_vt_btn(tbar, text="◀", bg="#334155", fg="#ffffff", font=("Sans", 10, "bold"), width=3, pady=8, command=self.prev_preview_item).pack(side="left", padx=2)
        self.page_lbl = tk.Label(tbar, text=f"1/{len(selected_list)}", bg="#0c1017", fg="#f8fafc", font=("Sans", 10, "bold"), padx=6)
        self.page_lbl.pack(side="left", padx=2)
        make_vt_btn(tbar, text="▶", bg="#334155", fg="#ffffff", font=("Sans", 10, "bold"), width=3, pady=8, command=self.next_preview_item).pack(side="left", padx=2)

        make_vt_btn(tbar, text="✏️ Edit Report", bg="#0f766e", fg="#ffffff", font=("Sans", 10, "bold"), width=13, pady=8, command=self.open_report_designer).pack(side="left", padx=6)
        make_vt_btn(tbar, text="➕ Create Report", bg="#1d4ed8", fg="#ffffff", font=("Sans", 10, "bold"), width=14, pady=8, command=self.clone_report).pack(side="left", padx=4)
        make_vt_btn(tbar, text="⚙️ Printer Offset", bg="#0284c7", fg="#ffffff", font=("Sans", 10, "bold"), width=14, pady=8, command=self.open_printer_settings_dialog).pack(side="left", padx=4)

        self.route_badge_lbl = tk.Label(tbar, text="", bg="#1e293b", fg="#38bdf8", font=("Sans", 9, "bold"), padx=10, pady=8, bd=0, relief="flat")
        self.route_badge_lbl.pack(side="right", padx=6)

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
        tpl = self.templates[self.tpl_var.get()]
        if hasattr(self, "route_badge_lbl"):
            avail = discover_all_printers()
            routed_p = tpl.get("default_printer") or (avail[0] if avail else "Default Printer")
            w_in, h_in = tpl.get("width_in", 2.0), tpl.get("height_in", 1.0)
            self.route_badge_lbl.config(text=f"🖨️ Target: {routed_p}   |   📏 Stock: {w_in} in x {h_in} in")
        cw = max(600, self.preview_canvas.winfo_width())
        ch = max(350, self.preview_canvas.winfo_height())
        scale = min((cw - 60) / tpl["width_in"], (ch - 40) / tpl["height_in"])
        ox = (cw - tpl["width_in"] * scale) / 2.0
        oy = (ch - tpl["height_in"] * scale) / 2.0
        render_label_on_tk_canvas(self.preview_canvas, tpl, item, ox, oy, scale, selected_idx=None, show_boxes=False)

    def open_printer_settings_dialog(self):
        tpl_name = self.tpl_var.get()
        tpl = self.templates[tpl_name]
        printers = discover_all_printers()
        chosen = tpl.get("default_printer") or (printers[0] if printers else "VT TCP: Zebra Large Labels (192.168.1.24:9100) [network]")

        sdlg = tk.Toplevel(self)
        sdlg.title(f"Printer Settings & Alignment — [{tpl_name}]")
        sdlg.geometry("620x470+650+260")
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
        v_lbl = tk.Label(vf, text="", bg="#1e293b", fg="#f8fafc", font=("Monospace", 11, "bold"), width=26)

        def update_v_label(*_a):
            val = int(v_var.get())
            inches = val / 203.0
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
        h_lbl = tk.Label(hf, text="", bg="#1e293b", fg="#f8fafc", font=("Monospace", 11, "bold"), width=26)

        def update_h_label(*_a):
            val = int(h_var.get())
            inches = val / 203.0
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
        make_vt_btn(btn_bar, text="🖨️ Save & Test Print 1 Label", bg="#0284c7", fg="#ffffff", font=("Sans", 10, "bold"), width=22, command=do_test_print).pack(side="right", padx=6)
        make_vt_btn(btn_bar, text="📏 Calibrate Gap (~JC)", bg="#7c3aed", fg="#ffffff", font=("Sans", 10, "bold"), width=18, command=do_calibrate_gap).pack(side="left", padx=4)
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
        cdlg.geometry("520x290+700+360")
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
        vdlg.geometry("420x170+750+420")
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
        chosen = tpl.get("default_printer", "")
        if not chosen or chosen not in printers:
            chosen = printers[0] if printers else "PDF Preview (Desktop)"
            tpl["default_printer"] = chosen

        w_in = float(tpl.get("width_in", 2.0))
        h_in = float(tpl.get("height_in", 1.0))

        # Single-Step Print Prompt: Only asks "How many labels?" and prints directly to the template's assigned printer!
        qdlg = tk.Toplevel(self)
        qdlg.title("Label Utility — Print Labels")
        qdlg.geometry("500x295+710+370")
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
        tk.Label(qdlg, text=f"🖨️ Target: {chosen}   |   📏 Media: {w_in} in x {h_in} in", bg="#0f172a", fg="#38bdf8", font=("Sans", 9, "bold"), padx=10, pady=3).pack(pady=(0, 8))

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
            res_dlg.geometry("460x165+730+430")
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
                    norm_items = [dict(it, qty=1) for it in items]
                    total_lbls = len(norm_items) * copies
                    txt_out, esc_bytes = build_escpos_and_txt_from_template(tpl_name, tpl, norm_items, copies=copies)
                    zpl_out = build_zpl_from_template(tpl_name, tpl, norm_items, copies=copies)

                    open("/tmp/vt_label_job.txt", "w", encoding="utf-8").write(txt_out)
                    open("/tmp/last_label.zpl", "w", encoding="utf-8").write(zpl_out)
                    out_pdf = "/tmp/VT_Labels.pdf"
                    batch = [dict(it, qty=copies) for it in norm_items]
                    export_pdf_labels(batch, tpl, out_pdf)

                    if "PDF" in chosen:
                        subprocess.Popen(["xdg-open", out_pdf])
                        self.after(0, qdlg.destroy)
                        return

                    if chosen.startswith("VT File:"):
                        m_f = re.search(r"\(([^)]+)\)", chosen)
                        f_target = m_f.group(1) if m_f else "/tmp/vt_label_job.txt"
                        if not os.path.isabs(f_target):
                            f_target = os.path.join(SANDBOX_DIR, "ViewTouch", f_target)
                        open(f_target, "w", encoding="utf-8").write(txt_out)
                        self.after(0, lambda: (qdlg.destroy(), show_print_result("Printed", f"✓ Wrote {total_lbls} label(s) to {f_target}!")))
                        return

                    ip_m = re.search(r"\((\d+\.\d+\.\d+\.\d+)(?::(\d+))?\)", chosen)
                    if ip_m:
                        ip = ip_m.group(1)
                        port = int(ip_m.group(2) or 9100)
                        import socket
                        is_zpl = any(k in chosen.lower() for k in ("zpl", "zebra", "zd", "lp28", "gx", "gk", "bixolon", "tspl"))
                        payload = zpl_out.encode("utf-8") if is_zpl else esc_bytes
                        with socket.create_connection((ip, port), timeout=4.0) as s:
                            s.sendall(payload)
                        self.after(0, lambda: (qdlg.destroy(), show_print_result("Printed", f"✓ Sent {total_lbls} label(s) ({w_in}x{h_in} in) to {ip}:{port}!")))
                        return

                    if chosen.startswith("VT CUPS:"):
                        m_q = re.search(r"\(([^)]+)\)", chosen)
                        q_name = m_q.group(1).strip() if m_q else chosen.split()[-1]
                    else:
                        q_name = chosen.replace("CUPS: ", "").split(" (")[0].strip()

                    v_out = subprocess.run(["lpstat", "-v", q_name], capture_output=True, text=True, timeout=2.0).stdout.lower()
                    is_thermal_q = any(k in q_name.lower() or k in v_out for k in ("tmt", "epson", "receipt", "80mm", "thermal", "zebra", "label", "192.168.1.77"))
                    if is_thermal_q:
                        is_zpl_q = any(k in q_name.lower() for k in ("zebra", "zpl", "zd", "lp28"))
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
                        self.after(0, lambda: (qdlg.destroy(), show_print_result("Printed", f"✓ Sent {total_lbls} label(s) ({w_in}x{h_in} in) to {q_name}!\n({job_info})")))
                    else:
                        err_txt = (proc.stderr or proc.stdout or f"Exit code {proc.returncode}").strip()
                        self.after(0, lambda: (qdlg.destroy(), show_print_result("Printer Error", f"CUPS could not print to {q_name}:\n{err_txt}", True)))
                except Exception as e:
                    self.after(0, lambda err=str(e): (qdlg.destroy(), show_print_result("Printer Error", f"Print failed: {err}", True)))

            threading.Thread(target=worker, daemon=True).start()

        qty_ent.bind("<Return>", do_print)
        qbtn_frame = tk.Frame(qdlg, bg="#1e293b")
        qbtn_frame.pack(pady=(6, 12))
        make_vt_btn(qbtn_frame, text="🖨️ Print Now", bg="#16a34a", fg="#ffffff", font=("Sans", 11, "bold"), width=15, command=do_print).pack(side="left", padx=8)
        make_vt_btn(qbtn_frame, text="⚙️ Adjust Higher / Lower", bg="#0284c7", fg="#ffffff", font=("Sans", 10, "bold"), command=lambda: (qdlg.destroy(), self.open_printer_settings_dialog())).pack(side="left", padx=6)
        make_vt_btn(qbtn_frame, text="Cancel", bg="#475569", fg="#ffffff", font=("Sans", 11, "bold"), width=8, command=qdlg.destroy).pack(side="left", padx=6)

        qdlg.update_idletasks()
        qdlg.lift()
        qty_ent.selection_range(0, "end")
        qty_ent.focus_force()

    def go_next(self):
        if self.current_step == 1:
            mode = self.mode_var.get()
            if mode == "retail":
                self.filtered_items = [it for it in self.all_items if it.get("is_retail")]
                self.show_step(3)
                return
            elif mode == "all":
                self.filtered_items = list(self.all_items)
                self.show_step(3)
                return
        elif self.current_step == 2:
            sel = self.cat_tree.selection()
            if sel:
                vals = self.cat_tree.item(sel[0], "values")
                if vals and vals[0] == "MFG":
                    self.filtered_items = [it for it in self.all_items if it["manufacturer"] == vals[1]]
                elif vals and vals[0] == "CAT":
                    self.filtered_items = [it for it in self.all_items if it["category"] == vals[1]]
                elif vals and vals[0] == "DG":
                    self.filtered_items = [it for it in self.all_items if it["dept_group"] == vals[1]]
                else:
                    self.filtered_items = list(self.all_items)
            else:
                self.filtered_items = list(self.all_items)
        self.show_step(min(4, self.current_step + 1))

    def go_prev(self):
        if self.current_step == 4 and getattr(self, "came_from_step", None) == 1:
            self.came_from_step = None; self.show_step(1); return
        self.show_step(max(1, self.current_step - 1))

if __name__ == "__main__":
    app = VTLabelUtility()
    app.mainloop()
