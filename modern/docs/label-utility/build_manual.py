# Builds ViewTouch_Label_Utility_User_Manual.pdf (the label utility user manual).
# Rebuild after changing the label utility:  cd modern/docs/label-utility && python3 build_manual.py ViewTouch_Label_Utility_User_Manual.pdf
# img/ holds sample labels rendered by vt_label_utility.py; screens/ holds screenshots taken on the Pi
# (bash modern/tools/capture_manual_screenshots.sh, then copy the pi-shots branch files into screens/).
from reportlab.lib.pagesizes import letter
from reportlab.lib.units import inch
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_CENTER
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Table, TableStyle,
                                PageBreak, Image, KeepTogether, NextPageTemplate)
from reportlab.platypus.tableofcontents import TableOfContents
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1]

NAVY = colors.HexColor("#0f172a"); BLUE = colors.HexColor("#1d4ed8"); TEAL = colors.HexColor("#0f766e")
GREY = colors.HexColor("#475569"); LIGHT = colors.HexColor("#f1f5f9"); LINE = colors.HexColor("#cbd5e1")
TIPBG = colors.HexColor("#ecfdf5"); TIPLN = colors.HexColor("#059669"); WARNBG = colors.HexColor("#fff7ed"); WARNLN = colors.HexColor("#ea580c")

body = ParagraphStyle("body", fontName="Helvetica", fontSize=10, leading=14, spaceAfter=6, textColor=colors.HexColor("#1e293b"))
small = ParagraphStyle("small", parent=body, fontSize=8.5, leading=11, spaceAfter=0)
cell = ParagraphStyle("cell", parent=body, fontSize=9, leading=12, spaceAfter=0)
cellb = ParagraphStyle("cellb", parent=cell, fontName="Helvetica-Bold")
head = ParagraphStyle("head", parent=cell, fontName="Helvetica-Bold", textColor=colors.white)
h1 = ParagraphStyle("h1", fontName="Helvetica-Bold", fontSize=18, leading=22, textColor=NAVY, spaceBefore=4, spaceAfter=10)
h2 = ParagraphStyle("h2", fontName="Helvetica-Bold", fontSize=13, leading=16, textColor=BLUE, spaceBefore=10, spaceAfter=5)
h3 = ParagraphStyle("h3", fontName="Helvetica-Bold", fontSize=10.5, leading=13, textColor=NAVY, spaceBefore=6, spaceAfter=3)
h2.keepWithNext = 1   # never leave a heading alone at the bottom of a page
h3.keepWithNext = 1
bullet = ParagraphStyle("bullet", parent=body, leftIndent=14, bulletIndent=4, spaceAfter=3)
num = ParagraphStyle("num", parent=body, leftIndent=18, bulletIndent=2, spaceAfter=4)
code = ParagraphStyle("code", fontName="Courier", fontSize=9, leading=12, textColor=colors.HexColor("#0f172a"),
                      backColor=LIGHT, borderColor=LINE, borderWidth=0.5, borderPadding=6, leftIndent=6, rightIndent=6,
                      spaceBefore=4, spaceAfter=10)
toc1 = ParagraphStyle("toc1", fontName="Helvetica", fontSize=10.5, leading=17, leftIndent=0)


class ManualDoc(BaseDocTemplate):
    def afterFlowable(self, f):
        if isinstance(f, Paragraph) and f.style.name == "h1":
            key = "h1-" + "".join(ch for ch in f.getPlainText() if ch.isalnum())
            self.canv.bookmarkPage(key)
            self.notify("TOCEntry", (0, f.getPlainText(), self.page, key))


def footer(canv, doc):
    canv.saveState()
    canv.setStrokeColor(LINE); canv.setLineWidth(0.5)
    canv.line(0.75 * inch, 0.6 * inch, 7.75 * inch, 0.6 * inch)
    canv.setFont("Helvetica", 8); canv.setFillColor(GREY)
    canv.drawString(0.75 * inch, 0.42 * inch, "ViewTouch Label Utility — User Manual")
    canv.drawRightString(7.75 * inch, 0.42 * inch, "Page %d" % doc.page)
    canv.restoreState()


def cover(canv, doc):
    canv.saveState()
    canv.setFillColor(NAVY); canv.rect(0, 0, 8.5 * inch, 11 * inch, stroke=0, fill=1)
    canv.setFillColor(BLUE); canv.rect(0, 6.1 * inch, 8.5 * inch, 0.08 * inch, stroke=0, fill=1)
    canv.setFillColor(colors.white); canv.setFont("Helvetica-Bold", 34)
    canv.drawString(0.9 * inch, 7.4 * inch, "ViewTouch Label Utility")
    canv.setFont("Helvetica", 18); canv.setFillColor(colors.HexColor("#93c5fd"))
    canv.drawString(0.9 * inch, 6.85 * inch, "User Manual")
    canv.setFont("Helvetica", 11); canv.setFillColor(colors.HexColor("#cbd5e1"))
    y = 5.6 * inch
    for line in ["Shelf, price, cigar and receiving labels for ViewTouch POS",
                 "Raspberry Pi label station  ·  Zebra label printers",
                 "", "Version 1.2  ·  November 2026"]:
        canv.drawString(0.9 * inch, y, line); y -= 0.3 * inch
    img = os.path.join(HERE, "img", "2X1_VINYL_UPC_PRICE.png")
    if os.path.exists(img):
        w = 3.0 * inch; h = w * 300 / 691.0
        canv.setFillColor(colors.white); canv.roundRect(0.9 * inch - 8, 1.5 * inch - 8, w + 16, h + 16, 6, stroke=0, fill=1)
        canv.drawImage(img, 0.9 * inch, 1.5 * inch, w, h)
    canv.restoreState()


def P(t, s=body): return Paragraph(t, s)
def B(t): return Paragraph(t, bullet, bulletText="•")
def N(i, t): return Paragraph(t, num, bulletText="%d." % i)
def C(t): return Paragraph(t.replace("\n", "<br/>").replace("  ", "&nbsp;&nbsp;"), code)


def box(title, text, kind="tip"):
    bg, ln = (TIPBG, TIPLN) if kind == "tip" else (WARNBG, WARNLN)
    t = Table([[Paragraph("<b>%s</b>  %s" % (title, text), cell)]], colWidths=[6.9 * inch])
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), bg), ("LINEBEFORE", (0, 0), (0, -1), 3, ln),
                           ("LEFTPADDING", (0, 0), (-1, -1), 10), ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                           ("TOPPADDING", (0, 0), (-1, -1), 6), ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
    return KeepTogether([Spacer(1, 2), t, Spacer(1, 8)])


def table(rows, widths, header=True):
    data = []
    for r, row in enumerate(rows):
        st = head if (header and r == 0) else cell
        data.append([Paragraph(c, cellb if (c_i == 0 and not (header and r == 0)) else st) for c_i, c in enumerate(row)])
    t = Table(data, colWidths=[w * inch for w in widths], repeatRows=1 if header else 0)
    style = [("GRID", (0, 0), (-1, -1), 0.4, LINE), ("VALIGN", (0, 0), (-1, -1), "TOP"),
             ("LEFTPADDING", (0, 0), (-1, -1), 6), ("RIGHTPADDING", (0, 0), (-1, -1), 6),
             ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 4)]
    if header:
        style += [("BACKGROUND", (0, 0), (-1, 0), NAVY)]
    for r in range(1 if header else 0, len(rows)):
        if r % 2 == 0:
            style.append(("BACKGROUND", (0, r), (-1, r), LIGHT))
    t.setStyle(TableStyle(style))
    return KeepTogether([t, Spacer(1, 10)]) if len(rows) <= 8 else t


def stack_toolbar(name):
    """Cut a wide toolbar screenshot (screens/<name>.png) row by row, splitting long rows in two, and save
    screens/<name>_stacked.png so its buttons stay readable on a page. Run on every build."""
    from PIL import Image as PI
    src = os.path.join(HERE, "screens", name + ".png")
    if not os.path.exists(src):
        return
    im = PI.open(src).convert("RGB"); W, H = im.size; bg = im.getpixel((W - 2, H - 2))
    uniform_row = lambda y: len({im.getpixel((x, y)) for x in range(0, W, 2)}) <= 2
    uniform_col = lambda x, y0, y1: len({im.getpixel((x, y)) for y in range(y0, y1)}) <= 2
    rows, y = [], 0
    while y < H:
        while y < H and uniform_row(y): y += 1
        if y >= H: break
        y0 = y
        while y < H and not uniform_row(y): y += 1
        rows.append((max(0, y0 - 3), min(H, y + 3)))
    pieces = []
    for y0, y1 in rows:
        right = W - 1
        while right > 0 and all(im.getpixel((right, yy)) == bg for yy in range(y0, y1)): right -= 1
        right = min(W, right + 7)
        if right <= W // 2 + 40:
            pieces.append(im.crop((0, y0, right, y1))); continue
        mid, split = right // 2, None
        for d in range(right // 3):
            for x in (mid - d, mid + d):
                if 0 < x < right - 1 and uniform_col(x, y0, y1) and uniform_col(x + 1, y0, y1):
                    split = x; break
            if split: break
        split = split or mid
        pieces += [im.crop((0, y0, split, y1)), im.crop((split, y0, right, y1))]
    out = PI.new("RGB", (max(p.width for p in pieces) + 8, sum(p.height + 4 for p in pieces) + 8), bg)
    y = 4
    for p in pieces:
        out.paste(p, (4, y)); y += p.height + 4
    out.save(os.path.join(HERE, "screens", name + "_stacked.png"))


for _tb in ("designer_toolbar1", "designer_palette", "designer_toolbar2"):
    stack_toolbar(_tb)


def shot(name, width_in=6.9, caption=None):
    """A screenshot taken on the Pi (screens/), framed, with an optional caption."""
    p = os.path.join(HERE, "screens", name)
    from PIL import Image as PI
    w, h = PI.open(p).size
    im = Image(p, width=width_in * inch, height=width_in * inch * h / w)
    rows = [[im]] + ([[Paragraph(caption, ParagraphStyle("cap", parent=small, textColor=GREY, alignment=TA_CENTER))]] if caption else [])
    t = Table(rows, colWidths=[width_in * inch + 8])
    t.setStyle(TableStyle([("BOX", (0, 0), (0, 0), 0.6, GREY), ("ALIGN", (0, 0), (-1, -1), "CENTER"),
                           ("LEFTPADDING", (0, 0), (-1, -1), 4), ("RIGHTPADDING", (0, 0), (-1, -1), 4),
                           ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 4)]))
    return KeepTogether([t, Spacer(1, 8)])


def label_img(name, width_in):
    p = os.path.join(HERE, "img", name)
    from PIL import Image as PI
    w, h = PI.open(p).size
    im = Image(p, width=width_in * inch, height=width_in * inch * h / w)
    t = Table([[im]], colWidths=[width_in * inch + 12])
    t.setStyle(TableStyle([("BOX", (0, 0), (-1, -1), 0.6, GREY), ("ALIGN", (0, 0), (-1, -1), "CENTER"),
                           ("TOPPADDING", (0, 0), (-1, -1), 6), ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
    return t


story = []
story.append(NextPageTemplate("body"))
story.append(PageBreak())

# ---------------------------------------------------------------- contents
story.append(P("Contents", ParagraphStyle("ct", parent=h1)))
toc = TableOfContents(); toc.levelStyles = [toc1]; toc.dotsMinLevel = 0
story.append(toc)
story.append(PageBreak())

# ---------------------------------------------------------------- 1
story.append(P("1. How the program works", h1))
story.append(P("ViewTouch Label Utility prints shelf, price, cigar-stick and receiving labels for the items in your "
               "ViewTouch POS. It runs on the Raspberry Pi next to ViewTouch and sends labels to your Zebra label printers."))
story.append(P("What it does, in order", h3))
for i, t in enumerate([
    "<b>Reads your items from ViewTouch.</b> Names, prices, UPCs (including alternate and carton UPCs), brands and categories "
    "come straight from ViewTouch's database. The program only <i>reads</i> it — nothing you do here changes an item in ViewTouch.",
    "<b>Uses a label template.</b> A template is a saved label design: its size, the printer it goes to, and where each "
    "piece (description, price, barcode, date…) sits on the label.",
    "<b>Draws the whole label as one picture</b> at the printer's own resolution (203 dots per inch on most Zebras). "
    "Text and barcodes are drawn by the program, bar by bar, so every bar is an exact number of printer dots wide.",
    "<b>Sends that picture to the printer.</b> The printer only prints the dots it is given; it never substitutes its own "
    "fonts or barcodes. This is the same way the old ILSA system printed, which is why labels look identical every time.",
], 1):
    story.append(N(i, t))
story.append(box("Good to know:", "What you see on the print-preview screen is drawn from the same layout as the printout. "
                 "Double-click the preview to see the exact printer dots."))
story.append(P("Where things are kept", h3))
story.append(table([
    ["What", "Where"],
    ["The program", "<font face='Courier'>~/viewtouch-main/modern/tools/vt_label_utility.py</font>"],
    ["Your label templates (live copy)", "<font face='Courier'>~/.local/share/ViewTouch/label_templates.json</font>"],
    ["Starting templates shipped with the program", "<font face='Courier'>~/viewtouch-main/modern/seed/templates/label_templates.json</font>"],
    ["Items, prices, UPCs, printers", "ViewTouch's own database (read only)"],
    ["Error log", "<font face='Courier'>/tmp/vt_label_utility.log</font> (cleared when the Pi restarts)"],
    ["Last label sent to a printer", "<font face='Courier'>/tmp/last_label.zpl</font> and <font face='Courier'>/tmp/VT_Labels.pdf</font>"],
], [2.4, 4.5]))
story.append(P("The label utility keeps its own template list, so templates you delete stay deleted."))

# ---------------------------------------------------------------- 2
story.append(PageBreak())
story.append(P("2. Starting and closing", h1))
story.append(B("<b>From ViewTouch:</b> on the Manager screen, press the <b>Label Utility</b> button. While the label utility is "
               "open, ViewTouch will not log you out for being idle."))
story.append(B("<b>For one item:</b> if ViewTouch opens the label utility for a particular item (a button set up with the "
               "<font face='Courier'>openLabelUtility</font> command and a UPC), it goes straight to that item's label preview. "
               "Any of the item's codes works — primary, alternate or carton UPC."))
story.append(B("The program opens full-screen. To close it, press <b>Close</b> (red, bottom-left)."))
story.append(B("The title bar says <i>ViewTouch Label Utility v1.2</i>; the start screen shows how many ViewTouch items it "
               "loaded."))
story.append(box("Note:", "Item changes made in ViewTouch (new items, price changes) appear here the next time the program "
                 "opens, or straight away if you press <b>Reload viewtouch.db</b>."))
story.append(P("The four screens", h2))
story.append(P("The program works like a wizard. You move forward with <b>Next &gt;&gt;</b> and back with "
               "<b>&lt;&lt; Previous</b> (bottom of the window)."))
story.append(table([
    ["Screen", "What you do there"],
    ["1. Start", "Choose which items to work with, or jump straight to a saved label template."],
    ["2. Categories", "Narrow the items down by group, category or brand (Tree View only)."],
    ["3. Items", "Tick the items to print, set quantities, and choose which UPC goes on each label."],
    ["4. Preview & Print", "Pick the template, check the preview, edit the label design, and print."],
], [1.6, 5.3]))
story.append(P("Bottom bar (always visible)", h3))
story.append(table([
    ["Button", "What it does"],
    ["Close", "Closes the program."],
    ["Reload viewtouch.db", "Re-reads all items from ViewTouch (use after changing items in ViewTouch). This also resets any UPC choices made on screen 3."],
    ["&lt;&lt; Previous", "Goes back one screen."],
    ["Next &gt;&gt;", "Goes forward one screen."],
], [1.8, 5.1]))

# ---------------------------------------------------------------- 3
story.append(PageBreak())
story.append(P("3. Screen 1 — Start", h1))
story.append(shot("screen1_start.png", 6.9, "Screen 1 — choose items or a saved template"))
story.append(P("Select ViewTouch data (top row)", h2))
story.append(P("Each big button chooses which items you will work with and moves you on immediately."))
story.append(table([
    ["Button", "Shows", "Goes to"],
    ["Tree View — Categories &amp; Brands", "All items, organised by group, category and brand so you can pick a section.", "Screen 2"],
    ["Retail Items — Cigars &amp; Goods Only", "Only retail items (items with a real barcode, merchandise, or cigar/tobacco/retail categories).", "Screen 3"],
    ["Receiving Orders", "Items sold in packs or cartons, plus retail items — handy after a delivery.", "Screen 3"],
    ["All Catalog — Full Inventory", "Every item.", "Screen 3"],
], [2.1, 3.8, 1.0]))
story.append(P("Select saved label template (left)", h2))
story.append(P("Lists every label template. To go straight to a label's preview:"))
story.append(B("Tap a template in the list (or choose it in the <b>Template</b> drop-down), then press "
               "<b>Open &amp; Preview Selected Label</b> — or simply double-click the template."))
story.append(B("This opens screen 4 with that template. If no items have been ticked yet, the preview uses the first item in "
               "the list. This is the quickest way to print a <b>RECEIVING LABEL</b>, which does not need an item."))
story.append(P("Database status and template actions (right)", h2))
story.append(table([
    ["Item", "What it means / does"],
    ["Database status", "Confirms ViewTouch is connected and shows how many items and templates were loaded."],
    ["Lookup Template", "Imports a label template from a file — including ILSA <b>.rtm</b> labels. See section 9."],
    ["Open Template Folder", "Opens the <font face='Courier'>seed/templates</font> folder in the file manager — a handy place to keep files you want to import."],
    ["Rename Template", "Gives the template selected in the list a new name. Its layout, printer and offsets are kept and it stays "
     "selected. An empty name, or one another template already has, is refused."],
    ["Delete Template", "Deletes the template selected in the list on the left (it asks first). The last remaining template can't be deleted."],
    ["Restore Standard Labels", "Puts the four standard labels (RECEIVING LABEL and the three ILSA labels) back to their original layout and printer, "
     "replacing any changes made to them. Your other templates are kept. It asks first."],
], [1.7, 5.2]))
story.append(P("Who can change templates", h2))
story.append(P("Everyone can pick a template and print labels. Only a <b>manager</b> (anyone ViewTouch lets onto the manager "
               "screens) can change templates: <b>Lookup Template</b>, <b>Rename Template</b>, <b>Delete Template</b>, "
               "<b>Restore Standard Labels</b>, and on screen 4 <b>Edit Report</b> and <b>Create Report</b>."))
story.append(B("The program knows who opened it from ViewTouch. For anyone else those buttons say <b>(manager)</b> under their name."))
story.append(B("Tapping one opens <b>Manager needed</b>: a manager can type their ViewTouch PIN there to allow that one change, "
               "or press Cancel. After 5 wrong PINs it waits a minute. The PIN is checked against ViewTouch's staff list and is "
               "never saved."))
story.append(box("Note:", "If the PIN can't be checked (ViewTouch's staff list can't be read), the change is not allowed. "
                 "A manager can log in to ViewTouch and open the Label Utility instead."))

# ---------------------------------------------------------------- 4
story.append(PageBreak())
story.append(P("4. Screen 2 — Categories (Tree View)", h1))
story.append(shot("screen2_categories.png", 6.2, "Screen 2 — groups, categories and brands"))
story.append(B("<b>Left list — ViewTouch Department Groups:</b> tap a category to show only that category in the tree. "
               "<b>ALL VIEWTOUCH CATEGORIES</b> shows everything."))
story.append(B("<b>Right — Category &amp; Brand Hierarchy:</b> groups (Cigars, Retail…) contain categories, which contain brands "
               "(shown with an <b>M</b> in front)."))
story.append(B("Tap a group, category or brand and press <b>Next &gt;&gt;</b> — or just double-click it — to list only those items "
               "on screen 3. Selecting the top line (or nothing) lists every item."))

# ---------------------------------------------------------------- 5
story.append(PageBreak())
story.append(P("5. Screen 3 — Items", h1))
story.append(P("This screen lists the items. Tick the ones you want labels for."))
story.append(shot("screen3_items.png", 6.9, "Screen 3 — two items ticked; the highlighted row will print its primary UPC"))
story.append(P("Selecting items", h2))
story.append(B("<b>Tap a row</b> to tick it (the empty box in Selected gets a tick). Tap again to untick. Ticking a row also sets its <b>Print Qty</b> to the "
               "number in <b>Print Qty for Selected</b>."))
story.append(B("<b>Select All</b> / <b>Deselect All</b> tick or untick every item in the list."))
story.append(B("<b>Quick Search</b> filters the list as you type. It matches description, brand, category and <i>any</i> of an "
               "item's UPCs — so you can scan or type an alternate UPC to find an item."))
story.append(B("<b>Print Qty for Selected</b> (right) sets the quantity on every ticked item."))
story.append(box("Quantities:", "The number you enter in the print window (section 7.2) is printed for <b>every</b> ticked item. "
                 "The Print Qty column only sets the number the print window starts with — items can't have different quantities "
                 "in the same print run. To print different amounts, print the items in separate runs."))
story.append(B("Press <b>Next &gt;&gt;</b> to go to the preview with the ticked items. If nothing is ticked, the first item is used."))
story.append(KeepTogether([P("Columns", h2), table([
    ["Column", "Meaning"],
    ["Selected / Print Qty", "Whether the item is ticked and its quantity."],
    ["UPC on Label", "The code that will be printed. If it is not the primary UPC, it is tagged, e.g. <i>04252614 [Alt UPC 1]</i>."],
    ["Other UPCs", "The item's other codes you could print instead."],
    ["Description, Price, Category, Item #, Group, Family, Brand / Mfg", "Item details from ViewTouch."],
], [2.2, 4.7])]))
story.append(P("Choosing which UPC goes on the label", h2))
story.append(P("Many items have more than one code in ViewTouch — a primary UPC, alternate UPCs, and for packs the carton UPC. "
               "You can print any of them. <b>This only affects the label; it never changes the item in ViewTouch.</b>"))
story.append(table([
    ["Control", "What it does"],
    ["Print UPC (drop-down)", "Switches the ticked items (or every listed item if none are ticked) to <b>Primary UPC</b>, <b>Alt UPC 1–3</b> "
     "or <b>Carton UPC</b>. Items that don't have that code keep their primary UPC."],
    ["Next UPC", "Steps the highlighted row (or the ticked rows) to its next code: Primary, Alt 1, Alt 2…, Carton, then back to Primary."],
], [1.8, 5.1]))
story.append(box("Note:", "The chosen code is used for both the barcode and the printed UPC number on the label. "
                 "Choices reset to the primary UPC when you press Reload viewtouch.db or restart the program."))

# ---------------------------------------------------------------- 6
story.append(PageBreak())
story.append(P("6. Screen 4 — Preview &amp; Print", h1))
story.append(P("The large area shows the label exactly as it will print, for one item at a time."))
story.append(shot("screen4_preview.png", 6.9, "Screen 4 — toolbar (wraps onto a second row on the Pi's screen) and the label preview"))
story.append(table([
    ["Control", "What it does"],
    ["Template", "Chooses which label design to use. The preview updates immediately."],
    ["Print Labels", "Opens the print window (section 7)."],
    ["Arrow buttons and 1/5", "Steps through the ticked items. The number shows which item is previewed."],
    ["UPC", "Lists every code for the previewed item (e.g. <i>Alt UPC 1: 04252614</i>). Pick one to print that code on this item's label."],
    ["Edit Report", "Opens the label designer for the current template (section 8). Managers only (see section 3)."],
    ["Create Report", "Makes a new template as a copy of the current one (section 8.6). Managers only."],
    ["Printer Offset", "Printer position, darkness, resolution and calibration for this template (section 7.3)."],
    ["Printer badge", "Shows which printer and label size this template uses."],
    ["Double-click the preview", "Switches between the sharp preview and the exact printer-dot view. The caption under the label says which one you are seeing."],
], [1.9, 5.0]))

# ---------------------------------------------------------------- 7
story.append(P("7. Printing labels", h1))
story.append(P("7.1 Step by step", h2))
for i, t in enumerate([
    "On screen 1 choose how to find your items (e.g. <b>Retail Items</b>), or use <b>Tree View</b> and pick a brand.",
    "On screen 3 tick the items. Use <b>Print UPC</b> or <b>Next UPC</b> if a label needs an alternate or carton UPC.",
    "Press <b>Next &gt;&gt;</b>. On screen 4 choose the <b>Template</b> (e.g. <i>2X1 VINYL UPC_PRICE</i>).",
    "Check the preview. Use the arrow buttons either side of the item counter to look at each item.",
    "Press <b>Print Labels</b>. If the template has a Variable field, you are first asked for the text (press OK, or Skip).",
    "Enter <b>how many labels</b> per item and press <b>Print Now</b> (or Enter). A green message confirms the labels were sent.",
], 1):
    story.append(N(i, t))
story.append(P("7.2 The print window", h2))
story.append(shot("print_window.png", 4.6, "The print window"))
story.append(table([
    ["Control", "What it does"],
    ["How many labels?", "Number of labels printed <b>for each selected item</b>. Use - and +, type a number, or tap a Quick Qty button (1, 5, 10, 20, 25, 50). "
     "It starts at the first item's Print Qty."],
    ["Target / Media line", "The printer and label size the job will go to. Check this before printing."],
    ["Print Now", "Sends the labels. Pressing Enter does the same."],
    ["Adjust Higher / Lower", "Closes this window and opens Printer Offset (section 7.3)."],
    ["Cancel", "Closes without printing."],
], [1.8, 5.1]))
story.append(box("Example:", "3 items ticked and 2 entered here prints 6 labels — 2 of each item."))
story.append(P("Where labels go", h3))
story.append(P("Each template remembers its printer. The printers come from ViewTouch's printer settings."))
story.append(table([
    ["Printer type", "How the label is sent"],
    ["Network label printer (e.g. Zebra Large Labels, Shelf Labels, Stick Labels)", "Sent directly to the printer's IP address as a ZPL picture."],
    ["CUPS printer queue", "Label printers get the same ZPL picture; ordinary printers get a PDF page the size of the label."],
    ["PDF Preview (Desktop)", "Opens a PDF of the labels on screen instead of printing."],
    ["Receipt printers (Epson, receipt, kitchen…)", "Get a plain text version. Don't use these for labels."],
], [2.9, 4.0]))
story.append(box("Changing a printer's IP:", "Change it in ViewTouch's printer settings. Templates follow the printer by name, so "
                 "a template set to <b>Stick Labels</b> automatically uses Stick Labels' new address.", "tip"))

story.append(P("7.3 Printer Offset (printer settings)", h2))
story.append(P("Open with <b>Printer Offset</b> on screen 4 or <b>Adjust Higher / Lower</b> in the print window. "
               "Settings are saved <b>per template</b>."))
story.append(shot("printer_settings.png", 5.2, "Printer Offset — printer position, darkness, resolution and calibration"))
story.append(table([
    ["Setting", "What it does"],
    ["Vertical Position", "<b>Print Higher (-8)</b> / <b>Print Lower (+8)</b> move everything up or down by 8 dots (about 1 mm). -2 / +2 make finer moves. "
     "Use this if the top or bottom line is cut off."],
    ["Horizontal Position", "<b>Shift Left</b> / <b>Shift Right</b> move everything sideways, the same way."],
    ["Darkness (1–30)", "Higher is darker. Raise it if bars or text look faint; lower it if bars bleed together."],
    ["Tear-Off", "Adjusts where the label stops for tearing off. Leave at 0 unless labels tear in the wrong place."],
    ["Printer Resolution", "203, 300 or 600 dpi. <b>Must match the printer</b> (shown on the printer's label) or labels print the wrong size."],
    ["Save &amp; Close", "Saves and closes."],
    ["Save &amp; Test Print 1 Label", "Saves and prints one label of the previewed item, so you can check the position."],
    ["Calibrate Gap", "Makes the printer feed a few labels to measure the label length. Do this after loading a new roll or a different label size."],
    ["Cancel", "Closes without saving."],
], [1.8, 5.1]))
story.append(box("Note:", "Test Print and Calibrate Gap only work with network printers (ones with an IP address)."))

# ---------------------------------------------------------------- 8
story.append(PageBreak())
story.append(P("8. Editing labels (the label designer)", h1))
story.append(P("Press <b>Edit Report</b> on screen 4. The designer opens the current template, filled in with the item being "
               "previewed, so you can see real data while you work."))
story.append(shot("designer_full.png", 6.9, "The label designer — three toolbars above the label; the description is selected (blue box and handles)"))
story.append(P("8.1 Moving and sizing pieces with the mouse or touch", h2))
story.append(B("<b>Click</b> a piece (description, price, barcode…) to select it. A blue dashed box with 8 handles appears."))
story.append(B("<b>Drag</b> the piece to move it. <b>Drag a handle</b> to change its box size."))
story.append(B("<b>Ctrl-click</b> or <b>Shift-click</b> to select several pieces; <b>drag on an empty area</b> to draw a selection box around them."))
story.append(B("<b>Double-click</b> a piece for its Properties window (field, text, exact position, rotation)."))
story.append(B("<b>Right-click</b> for a menu: Add Field, Bring to Front, Send to Back, Position / Properties, Duplicate, Rotate 90°, Delete Component."))
story.append(KeepTogether([P("Properties window (double-click a piece)", h3), table([
    ["Field", "What it does"],
    ["Data Field / Type", "What the piece shows — the same list as the Field box in the top toolbar (section 8.5)."],
    ["Custom / Static Text", "The words for a Label or Variable piece."],
    ["Left / Top (in)", "The exact position of the piece's top-left corner, in inches from the label's top-left corner."],
    ["Rotation (deg)", "0, 90, 180 or 270."],
    ["Save Properties / Cancel", "Apply the changes, or close without changing anything."],
], [1.9, 5.0])]))
story.append(KeepTogether([P("Status bar (bottom of the designer)", h3),
    P("Shows the selected piece's exact size and position, updated as you drag: <font face='Courier'>Object: #2 (name) | "
      "Left: 0.062 in  Top: 0.583 in  Width: 2.208 in  Height: 0.406 in | Font: 10pt</font>."),
    box("Fine-tuning small labels:", "For millimetre-critical labels such as the cigar stick label, use the status bar together with "
        "the arrow keys (0.01 in per press, 0.05 in with Shift), <b>Left / Top</b> in the Properties window, and <b>Box W / H</b> in "
        "the second toolbar for exact width and height. 0.01 in is about 0.25 mm, or 2 printer dots at 203 dpi.")]))
story.append(P("8.2 Top toolbar", h2))
story.append(shot("designer_toolbar1_stacked.png", 5.6, "Top toolbar (shown in two parts)"))
story.append(table([
    ["Control", "What it does"],
    ["Save", "Saves your changes to this template. The designer stays open; press Close when finished."],
    ["Save As…", "Saves your changes as a <b>new</b> template under a name you type (an empty or already-used name is refused). "
     "The template you opened stays as it was last saved; the designer then goes on with the new one."],
    ["Undo / Redo", "Steps back or forward through your changes."],
    ["Object", "Picks a piece by name (useful when pieces overlap)."],
    ["Field", "Changes what the selected piece shows (see the field list in 8.5)."],
    ["Text", "The words for a static Label or Variable piece. Press Enter to apply."],
    ["Font / size / B / I", "Font (Sans, Serif, Mono, Arial, Arial Black, Courier New), point size, bold, italic."],
    ["Alignment", "left, center or right inside the piece's box."],
    ["Close", "Closes the designer. If there are unsaved changes it asks: <b>Yes, Save</b>, <b>No, Discard</b> or <b>Cancel</b>."],
], [1.7, 5.2]))
story.append(P("8.3 Palette — adding new pieces", h2))
story.append(P("Tap a palette button, then tap on the label where the new piece should go. Press <b>Esc</b> to cancel."))
story.append(shot("designer_palette_stacked.png", 5.6, "Palette (shown row by row)"))
story.append(table([
    ["Palette button", "Adds"],
    ["Label", "Fixed text you type (e.g. SALE)."],
    ["DBText (Desc) / (Price) / (Brand) / (UPC)", "The item's description, price, brand or UPC number."],
    ["SysVar (Date) / SysVar (Emp)", "Today's date (YYMMDD) or the employee's name as F LAST."],
    ["Variable", "Text you are asked for each time you print (e.g. RECEIVED)."],
    ["DBCalc", "Pack calculation, e.g. PK:10 ($1.20/EA)."],
    ["Code 128 / UPC-A", "A barcode of the item's UPC (the one chosen on screen 3 or 4)."],
], [2.6, 4.3]))
story.append(KeepTogether([P("8.4 Second toolbar", h2), shot("designer_toolbar2_stacked.png", 5.4, "Second toolbar (shown row by row)"), table([
    ["Control", "What it does"],
    ["Box W / H", "Exact width and height of the selected piece, in inches."],
    ["AutoFont", "On: text shrinks or grows to fill its box. Off: uses the exact font size you set (the ILSA labels use exact sizes)."],
    ["Wrap", "Lets long text use two lines."],
    ["Front / Back", "Brings the piece in front of, or sends it behind, overlapping pieces."],
    ["Rotate", "Turns the piece 90°."],
    ["Dup / Del", "Duplicates or deletes the selected piece."],
    ["Target", "The printer this template prints to."],
    ["Stock", "Picks a standard label size (2.0×1.0, 2.5×0.75, 3.0×2.0, 1.5×1.0, 2.25×1.25) or Custom."],
    ["W × H + Apply Size", "Type an exact label size in inches and press Apply Size."],
], [1.7, 5.2])]))
story.append(P("Keyboard shortcuts", h3))
story.append(table([
    ["Keys", "Action", "Keys", "Action"],
    ["Ctrl+Z / Ctrl+Y", "Undo / Redo", "Arrow keys", "Nudge 0.01 in"],
    ["Ctrl+C / X / V", "Copy / Cut / Paste", "Shift+Arrows", "Nudge 0.05 in"],
    ["Ctrl+D", "Duplicate", "R", "Rotate 90°"],
    ["Ctrl+A", "Select all pieces", "Delete", "Delete selected"],
    ["Esc", "Cancel palette tool", "", ""],
], [1.4, 2.05, 1.4, 2.05]))
story.append(KeepTogether([P("8.5 Field list", h2), table([
    ["Field", "Prints"],
    ["Label (Static Text)", "The text you type."],
    ["Variable", "Text asked for when printing (default RECEIVED)."],
    ["Date (YYMMDD) / Date (MM/DD/YYYY) / Year (YY)", "Today's date in that format."],
    ["Employee (F LAST)", "The active ViewTouch employee's name, e.g. P SMITH."],
    ["Description, Retail Price, Manufacturer / Brand, Category, UPC / PLU", "The item's details from ViewTouch."],
    ["DBCalc", "Pack quantity and per-unit price."],
    ["Code 128 / UPC-A / UPC-E / Code 39", "A barcode. UPC fields automatically use the right type for the code chosen (UPC-A, UPC-E, EAN-13), "
     "and the check digit is always calculated correctly."],
], [2.9, 4.0])]))
story.append(box("ILSA labels:", "Labels imported from ILSA keep ILSA's bar width, the digits printed under the bars, and their exact "
                 "text placement. These settings are carried over automatically and are not shown in the designer."))
story.append(P("8.6 Creating a new label (Create Report)", h2))
for i, t in enumerate([
    "On screen 4 choose the template closest to what you want, then press <b>Create Report</b>.",
    "Enter a <b>Label / Store Name</b> (this becomes the template name).",
    "Choose the <b>Target Printer</b> and the <b>Media Stock / Size</b>.",
    "Press <b>Create &amp; Open Designer</b>. The new template is a copy of the one you started from, ready to edit. "
    "A name another template already has is refused.",
], 1):
    story.append(N(i, t))
story.append(box("Tip:", "Already in the designer? <b>Save As…</b> saves what you see as a new template and leaves the original alone."))

# ---------------------------------------------------------------- 9
story.append(P("9. Importing labels", h1))
story.append(P("Use <b>Lookup Template</b> on screen 1. A file window opens; choose the file and press Open."))
story.append(P("ILSA labels (.rtm)", h2))
story.append(P("Label designs saved from the old ILSA system (ReportBuilder <b>.rtm</b> files) import directly. The program reads the "
               "label size, the position and size of every piece, fonts, alignment, barcode type, bar width and the digits under "
               "the barcode, and creates a template with the same name as the file."))
for i, t in enumerate([
    "Copy the .rtm file to the Pi (a USB stick, or put it in the folder that <b>Open Template Folder</b> opens).",
    "Press <b>Lookup Template</b> and choose the file.",
    "If a template with that name already exists you are asked whether to replace it.",
    "A message confirms the import. The new label opens in the preview.",
    "Open <b>Printer Offset</b>: set the right printer, check the resolution, and print a test label.",
], 1):
    story.append(N(i, t))
story.append(P("Fields understood in ILSA files: description (PLU_Desc), UPC (PLU_Num), price, manufacturer, category, dates "
               "(yymmdd, yy, mm/dd/yyyy), fixed text labels, and UPC-A, UPC-E, EAN-13, Code 39 and Code 128 barcodes."))
story.append(box("After importing:", "ILSA saved some labels a little wider than the actual stock (for example 2.3 in for a 2 in label). "
                 "This is kept as-is so the layout matches ILSA. If the right edge is cut off, adjust the size with W × H + Apply Size "
                 "in the designer.", "warn"))
story.append(P("Other files", h2))
story.append(table([
    ["File", "Result"],
    [".json", "Template file(s) from this program or the other ViewTouch label programs. Every template in the file is added."],
    [".prn, .nlbl, .slnx", "ZebraDesigner / NiceLabel / BarTender files can't be read. A new template with the file's name is created as a "
     "copy of RECEIVING LABEL for you to design in the designer."],
], [1.5, 5.4]))

# ---------------------------------------------------------------- 10
story.append(P("10. Your label templates", h1))
story.append(P("These four templates are installed. They match the labels from the ILSA system."))
for name, title, desc, w in [
    ("2X1_VINYL_UPC_PRICE.png", "2X1 VINYL UPC_PRICE — shelf price label",
     "Brand, UPC, date (YYMMDD), barcode, year code, large price in Arial Black, and description. Printer: Shelf Labels.", 3.2),
    ("2X1_VINYL_UPC_DESC.png", "2X1 VINYL UPC_DESC — description and barcode",
     "Description with a full-size UPC-A barcode and the digits underneath. Printer: Zebra Large Labels.", 3.0),
    ("CIGAR_STICK_LABEL_UPC_DESC.png", "CIGAR STICK LABEL UPC_DESC — 2.0 × 0.56 in",
     "Description and UPC-A barcode for cigar sticks. Printer: Stick Labels (the cigar printer at 192.168.1.25).", 2.8),
    ("RECEIVING_LABEL.png", "RECEIVING LABEL — 2.2 × 0.75 in, two across",
     "RECEIVED, the employee's name and today's date, twice side by side. Needs no item.", 3.0),
]:
    story.append(KeepTogether([P(title, h3), P(desc), label_img(name, w), Spacer(1, 8)]))
story.append(KeepTogether([P("Renaming, deleting and restoring templates (managers)", h3),
    B("<b>Rename a template:</b> on screen 1, select it in the template list and press <b>Rename Template</b>. "
      "A tablet's Labels screen shows the new name the next time it is opened. Renaming a standard label means "
      "Restore Standard Labels adds the original back under its old name."),
    B("<b>Delete a template:</b> on screen 1, select it in the template list and press <b>Delete Template</b>."),
    B("<b>Undo changes to a standard label, or bring back one you deleted:</b> on screen 1 press <b>Restore Standard Labels</b>. "
      "RECEIVING LABEL and the three ILSA labels return to their original layout and printer; your other templates stay as they are."),
    B("<b>Bring back a single ILSA label</b> without touching the others: <b>Lookup Template</b> and choose its .rtm file (section 9)."),
    Spacer(1, 6)]))
story.append(box("Back up your templates:", "Your adjusted templates live in "
                 "<font face='Courier'>~/.local/share/ViewTouch/label_templates.json</font>. Copy that file somewhere safe after "
                 "making changes you want to keep, or run the upload command in section 12."))

# ---------------------------------------------------------------- 11
story.append(PageBreak())
story.append(P("11. Updating the program", h1))
story.append(P("New versions are published to your GitHub repository. To update the Pi:"))
story.append(B("<b>Double-click the Update Label Utility icon</b> on the Pi's desktop (choose <b>Execute</b> if asked). A window opens, "
               "shows each step, beeps three times when it's finished, and says <b>Up to date</b>."))
story.append(B("Or, in a Terminal:"))
story.append(C("bash ~/viewtouch-main/modern/tools/update_label_utility.sh"))
story.append(P("Either way it: pulls the latest changes from GitHub (<font face='Courier'>main</font>), runs the installer, turns on the development-notes check, and "
               "updates GitHub's <font face='Courier'>main</font> to match the Pi. Then close and reopen the label utility. Your "
               "templates and printer settings are kept."))
story.append(P("The installer sets up the launcher ViewTouch's Label Utility button uses, installs any missing Python parts, and installs Microsoft's "
               "Arial, Arial Black and Courier New fonts used by the ILSA labels. It may ask for the Pi's password. It can also be run on its own:"))
story.append(C("bash ~/viewtouch-main/modern/tools/install_label_utility.sh"))
story.append(box("If the update stops with an error:", "nothing is lost. Take a photo of the window and send it to Bob.", "warn"))

# ---------------------------------------------------------------- 12
story.append(P("12. Troubleshooting", h1))
story.append(table([
    ["Problem", "What to do"],
    ["Print says it was sent, but nothing prints",
     "Check the Target line names the right printer. Then test the printer itself from a Terminal (replace the IP):<br/>"
     "<font face='Courier'>printf '^XA^FO40,40^A0N,50,50^FDTEST^FS^XZ' &gt; /dev/tcp/192.168.1.24/9100</font><br/>"
     "If TEST doesn't print, the printer is paused, out of labels/ribbon, open, or off."],
    ["Labels go to the wrong printer", "Open Printer Offset or the designer and check Target. Check the printer's IP in ViewTouch's printer settings."],
    ["Top or bottom line cut off", "Printer Offset: Print Higher or Print Lower, then Save &amp; Test Print."],
    ["Right edge cut off", "The template is wider than the labels. Reduce the label width in the designer (W × H, Apply Size)."],
    ["Labels print too small or too large", "Printer Resolution in Printer Offset doesn't match the printer (203 vs 300 dpi)."],
    ["Labels skip, or print across two labels", "New roll or different size: Printer Offset, Calibrate Gap."],
    ["Faint print, or bars run together", "Raise or lower Darkness in Printer Offset."],
    ["Barcode won't scan", "Check the right UPC was chosen (screen 3, UPC on Label). Make sure the barcode isn't cut off and Darkness is right."],
    ["Big price touches the barcode", "The Arial Black font isn't installed. Run the installer (section 11)."],
    ["An item is missing or has an old price", "Press Reload viewtouch.db. Items marked inactive in ViewTouch, and restaurant menu items, are not listed."],
    ["Program won't open or closes by itself", "Send the log (below) for diagnosis."],
], [2.1, 4.8]))
story.append(KeepTogether([P("Sending the log for help", h3), P("Right after a problem (before restarting the Pi), run:"),
    C("bash ~/viewtouch-main/modern/tools/push_label_log.sh"), P("This uploads the error log, the last label sent to a printer, and your current templates to the <b>pi-logs</b> "
               "branch of your GitHub repository, where they can be reviewed. It doesn't change any of your files.")]))

# ---------------------------------------------------------------- 13
story.append(PageBreak())
story.append(P("13. Quick reference", h1))
story.append(table([
    ["To…", "Do this"],
    ["Open the label utility", "ViewTouch Manager screen &gt; Label Utility"],
    ["Print shelf labels for some items", "Retail Items &gt; tick items &gt; Next &gt;&gt; &gt; Template &gt; Print Labels &gt; quantity &gt; Print Now"],
    ["Print labels for one brand", "Tree View &gt; double-click the brand &gt; tick items &gt; Next &gt;&gt; &gt; Print Labels"],
    ["Print receiving labels", "Screen 1: double-click RECEIVING LABEL &gt; Print Labels &gt; quantity &gt; Print Now"],
    ["Print a carton or alternate UPC", "Screen 3: Print UPC or Next UPC — or screen 4: UPC drop-down"],
    ["Find an item by any of its UPCs", "Screen 3: type or scan the code into Quick Search"],
    ["Move the print up or down", "Screen 4: Printer Offset &gt; Print Higher / Lower &gt; Save &amp; Test Print"],
    ["Set up a new label roll", "Printer Offset &gt; Calibrate Gap"],
    ["Change a label's layout", "Screen 4: Edit Report &gt; drag pieces &gt; Save"],
    ["Make a new label from an existing one", "Screen 4: Create Report &gt; name, printer, size &gt; Create &amp; Open Designer"],
    ["Bring in an ILSA label", "Screen 1: Lookup Template &gt; choose the .rtm file"],
    ["See exact printer dots", "Screen 4: double-click the preview"],
    ["Save a changed label under a new name", "Designer: Save As… &gt; type the name &gt; OK"],
    ["Rename a template", "Screen 1: select it &gt; Rename Template (manager)"],
    ["Delete a template", "Screen 1: select it &gt; Delete Template (manager)"],
    ["Undo changes to the standard labels", "Screen 1: Restore Standard Labels"],
    ["Update the program", "Double-click Update Label Utility on the desktop — or <font face='Courier'>bash ~/viewtouch-main/modern/tools/update_label_utility.sh</font>"],
    ["Send logs for help", "<font face='Courier'>bash ~/viewtouch-main/modern/tools/push_label_log.sh</font>"],
], [2.4, 4.5]))


def build():
    doc = ManualDoc(OUT, pagesize=letter, leftMargin=0.8 * inch, rightMargin=0.8 * inch, topMargin=0.8 * inch,
                    bottomMargin=0.85 * inch, title="ViewTouch Label Utility — User Manual", author="ViewTouch",
                    subject="User manual for ViewTouch Label Utility (ViewTouch label printing)")
    doc._h1n = 0
    frame = Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f")
    doc.addPageTemplates([PageTemplate(id="cover", frames=[frame], onPage=cover),
                          PageTemplate(id="body", frames=[frame], onPage=footer)])
    doc.multiBuild(story)

build()
print("written", OUT)
