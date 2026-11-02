# Builds ViewTouch_Gift_Cards_User_Manual.pdf (the gift card user manual for ViewTouch Modern).
# Rebuild:  cd modern/docs/gift-cards && python3 build_manual.py ViewTouch_Gift_Cards_User_Manual.pdf
# screens/ holds screenshots from a demo store, taken on the Pi with  bash modern/tools/capture_vt_screenshots.sh
# (they land on the pi-vt-shots branch; copy them into screens/). A missing screenshot prints as a grey box.
from reportlab.lib.pagesizes import letter
from reportlab.lib.units import inch
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Table, TableStyle,
                                PageBreak, CondPageBreak, Image, KeepTogether, NextPageTemplate)
from reportlab.platypus.tableofcontents import TableOfContents
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "ViewTouch_Gift_Cards_User_Manual.pdf")

NAVY = colors.HexColor("#0f172a"); BLUE = colors.HexColor("#1d4ed8"); TEAL = colors.HexColor("#0f766e")
GREY = colors.HexColor("#475569"); LIGHT = colors.HexColor("#f1f5f9"); LINE = colors.HexColor("#cbd5e1")
TIPBG = colors.HexColor("#ecfdf5"); TIPLN = colors.HexColor("#059669"); WARNBG = colors.HexColor("#fff7ed"); WARNLN = colors.HexColor("#ea580c")
ORANGE = colors.HexColor("#f59e0b")

body = ParagraphStyle("body", fontName="Helvetica", fontSize=10, leading=14, spaceAfter=6, textColor=colors.HexColor("#1e293b"))
cell = ParagraphStyle("cell", parent=body, fontSize=9, leading=12, spaceAfter=0)
cellb = ParagraphStyle("cellb", parent=cell, fontName="Helvetica-Bold")
head = ParagraphStyle("head", parent=cell, fontName="Helvetica-Bold", textColor=colors.white)
cap = ParagraphStyle("cap", parent=body, fontSize=8.5, leading=11, textColor=GREY, alignment=1, spaceAfter=10)
h1 = ParagraphStyle("h1", fontName="Helvetica-Bold", fontSize=18, leading=22, textColor=NAVY, spaceBefore=4, spaceAfter=10)
h2 = ParagraphStyle("h2", fontName="Helvetica-Bold", fontSize=13, leading=16, textColor=BLUE, spaceBefore=10, spaceAfter=5)
h2.keepWithNext = 1
bullet = ParagraphStyle("bullet", parent=body, leftIndent=14, bulletIndent=4, spaceAfter=3)
num = ParagraphStyle("num", parent=body, leftIndent=18, bulletIndent=2, spaceAfter=4)
toc1 = ParagraphStyle("toc1", fontName="Helvetica", fontSize=10.5, leading=17)


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
    canv.drawString(0.75 * inch, 0.42 * inch, "ViewTouch Gift Cards — User Manual")
    canv.drawRightString(7.75 * inch, 0.42 * inch, "Page %d" % doc.page)
    canv.restoreState()


def cover(canv, doc):
    canv.saveState()
    canv.setFillColor(NAVY); canv.rect(0, 0, 8.5 * inch, 11 * inch, stroke=0, fill=1)
    canv.setFillColor(TEAL); canv.rect(0, 6.1 * inch, 8.5 * inch, 0.08 * inch, stroke=0, fill=1)
    canv.setFillColor(colors.white); canv.setFont("Helvetica-Bold", 34)
    canv.drawString(0.9 * inch, 7.4 * inch, "ViewTouch Gift Cards")
    canv.setFont("Helvetica", 18); canv.setFillColor(colors.HexColor("#5eead4"))
    canv.drawString(0.9 * inch, 6.85 * inch, "User Manual")
    canv.setFont("Helvetica", 11); canv.setFillColor(colors.HexColor("#cbd5e1"))
    y = 5.6 * inch
    for line in ["Selling, reloading, redeeming and cashing out gift cards",
                 "in ViewTouch Modern", "", "November 2026"]:
        canv.drawString(0.9 * inch, y, line); y -= 0.3 * inch
    # A simple card drawing.
    x, yb, w, h = 0.9 * inch, 1.4 * inch, 3.4 * inch, 2.1 * inch
    canv.setFillColor(TEAL); canv.roundRect(x, yb, w, h, 14, stroke=0, fill=1)
    canv.setFillColor(ORANGE); canv.rect(x, yb + h - 0.55 * inch, w, 0.22 * inch, stroke=0, fill=1)
    canv.setFillColor(colors.white); canv.setFont("Helvetica-Bold", 20)
    canv.drawString(x + 0.25 * inch, yb + 0.95 * inch, "GIFT CARD")
    canv.setFont("Courier-Bold", 14)
    canv.drawString(x + 0.25 * inch, yb + 0.4 * inch, "6000 2026 1001")
    canv.restoreState()


def P(t, s=body): return Paragraph(t, s)
def B(t): return Paragraph(t, bullet, bulletText="•")
def N(i, t): return Paragraph(t, num, bulletText="%d." % i)


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
        data.append([Paragraph(c, cellb if (i == 0 and not (header and r == 0)) else st) for i, c in enumerate(row)])
    t = Table(data, colWidths=[w * inch for w in widths], repeatRows=1 if header else 0)
    style = [("GRID", (0, 0), (-1, -1), 0.4, LINE), ("VALIGN", (0, 0), (-1, -1), "TOP"),
             ("LEFTPADDING", (0, 0), (-1, -1), 6), ("RIGHTPADDING", (0, 0), (-1, -1), 6),
             ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 4)]
    if header:
        style.append(("BACKGROUND", (0, 0), (-1, 0), NAVY))
    for r in range(1 if header else 0, len(rows)):
        if r % 2 == 0:
            style.append(("BACKGROUND", (0, r), (-1, r), LIGHT))
    t.setStyle(TableStyle(style))
    return KeepTogether([t, Spacer(1, 10)]) if len(rows) <= 8 else t


def shot(name, caption, width=6.9):
    """A screenshot from screens/ (png or jpg) with its caption; a grey box when it hasn't been taken yet."""
    for ext in (".png", ".jpg"):
        path = os.path.join(HERE, "screens", name + ext)
        if os.path.exists(path):
            from PIL import Image as PI
            w, h = PI.open(path).size
            img = Image(path, width * inch, width * inch * h / w)
            frame = Table([[img]], colWidths=[width * inch + 4])
            frame.setStyle(TableStyle([("BOX", (0, 0), (-1, -1), 0.6, LINE), ("LEFTPADDING", (0, 0), (-1, -1), 2),
                                       ("RIGHTPADDING", (0, 0), (-1, -1), 2), ("TOPPADDING", (0, 0), (-1, -1), 2),
                                       ("BOTTOMPADDING", (0, 0), (-1, -1), 2)]))
            return KeepTogether([frame, Spacer(1, 3), P(caption, cap)])
    ph = Table([[P("<i>Screenshot to come: %s</i>" % caption, cap)]], colWidths=[width * inch],
               rowHeights=[width * inch * 9 / 16 * 0.45])
    ph.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), LIGHT), ("BOX", (0, 0), (-1, -1), 0.6, LINE),
                            ("VALIGN", (0, 0), (-1, -1), "MIDDLE")]))
    return KeepTogether([ph, Spacer(1, 10)])


def build():
    doc = ManualDoc(OUT, pagesize=letter, title="ViewTouch Gift Cards — User Manual", author="Bob (Claude)",
                    leftMargin=0.8 * inch, rightMargin=0.8 * inch, topMargin=0.8 * inch, bottomMargin=0.85 * inch)
    frame = Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f")
    doc.addPageTemplates([PageTemplate("cover", [frame], onPage=cover),
                          PageTemplate("body", [frame], onPage=footer)])
    s = [NextPageTemplate("body"), PageBreak()]

    toc = TableOfContents(); toc.levelStyles = [toc1]
    s += [P("Contents", ParagraphStyle("toch", parent=h1)), toc, PageBreak()]

    # 1 ----------------------------------------------------------------------------------------
    s += [P("1. Gift cards at a glance", h1),
          P("ViewTouch keeps every gift card itself: its number, its balance and a history of everything done "
            "with it. There is no outside gift card company. A card is sold like an item on a check, and it "
            "starts working when that check is paid."),
          table([["Rule", "What it means at the counter"],
                 ["Works once paid", "A card sold or reloaded on a check has no money on it until the check is paid "
                  "and closed. Until then the screen says <i>Card … isn't active yet</i>."],
                 ["Up to $2,000", "One card can hold at most $2,000. A sale or reload that would go over is refused."],
                 ["Never expires", "Balances stay on the card until spent or cashed out."],
                 ["No tax, no discounts", "A gift card on a check is not taxed, percent discounts and staff meals "
                  "don't apply to it, and it earns no reward points."],
                 ["Not sent to the kitchen", "Gift card lines never print on kitchen tickets."],
                 ["On the receipt", "The guest's receipt prints the card line with <b>Card #:</b> and the full number, "
                  "so a card sold without a plastic card is still usable."],
                 ["Practice checks", "Training (practice) checks can't take money off a real card."]],
                [1.6, 5.3]),
          P("What you can do with a card", h2),
          B("<b>Sell</b> a new card or <b>reload</b> one that already exists (chapter 4)."),
          B("<b>Check a balance</b> and see the card's history (chapter 3)."),
          B("<b>Pay a check</b> from the card, all or part of it (chapter 5)."),
          B("<b>Cash out</b> a small leftover balance where state law requires it (chapter 6)."),
          B("<b>Report</b> on cards sold, spent and still owed (chapter 8)."),
          PageBreak()]

    # 2 ----------------------------------------------------------------------------------------
    s += [P("2. The Gift Card screen", h1),
          P("Everything about a card happens on one screen. You can reach it from:"),
          table([["From", "Touch"],
                 ["An open check", "<b>Check…</b> → <b>Sell / Check a Gift Card…</b>"],
                 ["Paying a check", "<b>Gift Card…</b> on the payment screen"],
                 ["Customers", "<b>Gift Cards…</b>"],
                 ["Manager", "<b>Gift Cards…</b>"],
                 ["A scanner", "Scan a card's barcode on almost any screen: the Gift Card screen opens with that card."]],
                [1.6, 5.3]),
          shot("m17-gift-card", "The Gift Card screen with a card looked up: balance, history and the actions for it."),
          P("Left side: finding a card and what to do with it", h2),
          table([["Part", "What it does"],
                 ["Search box", "<i>Search Card #, Purchased By, or Purchased For…</i> Type a number (spaces and dashes "
                  "are fine), swipe the card, or type part of a name. Touching it brings up the number pad."],
                 ["Look Up", "Looks up what's in the search box (Enter / Done does the same)."],
                 ["$25  $50  $75  $100", "The amount to sell or reload. The chosen one stays lit."],
                 ["Other $", "Any other amount, e.g. 35 or 12.50. Typing here replaces the button choice."],
                 ["Sell … Card on the Check", "Puts a new card for that amount on the check. Says "
                  "<b>Reload … on the Check</b> when the card already exists. Greyed out until an amount is chosen."],
                 ["Pay the Check from This Card", "Takes payment for the open check from the card (chapter 5)."],
                 ["Cash Out … (under …)", "Shows only when the store's state allows cashing out and the balance is "
                  "under the limit (chapter 6)."]],
                [1.9, 5.0]),
          CondPageBreak(2.5 * inch), P("Right side: the card", h2), table([["Part", "What it shows"],
                 ["Title", "<b>Card 1001</b> (last four digits); <i>Card 1001 isn't active yet</i> for a number with no "
                  "money on file; <i>Look up a card to see its balance</i> when nothing is looked up."],
                 ["Balance", "What's on the card now, in large type."],
                 ["Info line", "<b>Since</b> (when it was first activated) | <b>PIN</b> | <b>For</b> (who it's for) | "
                  "<b>By</b> (who bought it). PIN, For and By come with cards brought over from the old system; "
                  "they show ---- or — when empty."],
                 ["Matching Gift Cards", "When a search finds several cards, they're listed (up to 100, cards with money "
                  "first). Tap one to open it."],
                 ["History", "The last 12 things done with the card, newest first: when, what (e.g. <i>Sold on check "
                  "#212</i>, <i>Paid check #240</i>, <i>Cashed out</i>) and the amount."]],
                [1.9, 5.0]),
          PageBreak()]

    # 3 ----------------------------------------------------------------------------------------
    s += [P("3. Looking up a card and its balance", h1),
          N(1, "Open the Gift Card screen (chapter 2)."),
          N(2, "Swipe or scan the card, or touch the search box and type the number. Spaces and dashes don't matter: "
               "<b>6000 2026 1001</b> and <b>600020261001</b> find the same card."),
          N(3, "Touch <b>Look Up</b> (or Done on the number pad)."),
          N(4, "Read the balance and history on the right. Tell the guest the balance; nothing has changed on the card."),
          P("Finding a card without its number", h2),
          P("Type part of a name instead: who bought it or who it's for. Every word you type must match, so "
            "<b>smith</b> finds all Smiths, and <b>john smith</b> narrows it down. You can also type the PIN or the "
            "last digits of the number. When exactly one card matches it opens right away; otherwise tap the one "
            "you want in <b>Matching Gift Cards</b>."),
          box("Tip:", "Find a Check also searches gift card numbers, which helps answer \"which check sold this card?\""),
          box("Not active yet?", "The card was put on a check that hasn't been paid, or the number is wrong. "
              "Find the check and finish paying it, or check the number on the receipt.", "warn"),
          PageBreak()]

    # 4 ----------------------------------------------------------------------------------------
    s += [P("4. Selling or reloading a card", h1),
          P("Selling a new card", h2),
          N(1, "Start a check, or open the guest's check if they're buying other things too. (If no check is open, "
               "ViewTouch starts a quick check for the card.)"),
          N(2, "Open the Gift Card screen: <b>Check…</b> → <b>Sell / Check a Gift Card…</b>"),
          N(3, "Swipe, scan or type the number of the plastic card. <b>No card in hand?</b> Leave the box empty: "
               "ViewTouch makes up a 16-digit number starting with 6 and prints it on the receipt."),
          N(4, "Touch <b>$25</b>, <b>$50</b>, <b>$75</b> or <b>$100</b>, or type the amount in <b>Other $</b>."),
          N(5, "Touch <b>Sell $50 Card on the Check</b>. A line <b>Gift Card 1001</b> is added to the check for the amount, "
               "with no tax."),
          N(6, "Take payment for the check as usual (cash, credit card…). When the check is paid, the card is active "
               "with the amount on it, and its history says <i>Sold on check #…</i>."),
          N(7, "Hand the guest the card and the receipt. The receipt shows <b>Card #:</b> with the full number."),
          box("Important:", "The card is worth nothing until the check is paid. If the guest walks away before paying, "
              "nothing is on the card.", "warn"),
          P("Reloading (adding money to) a card", h2),
          P("Same steps: look up the existing card, choose the amount, and the button reads "
            "<b>Reload $25 on the Check</b>. The check line says <b>Gift Card reload 1001</b>. The money is added when "
            "the check is paid; the history says <i>Reloaded on check #…</i>."),
          P("Several cards on one check", h2),
          P("Fine: sell each one in turn (look up or leave the number blank, choose the amount, Sell). The same card "
            "can't be on one check twice; change the amount by removing its line and selling it again."),
          P("When a sale is refused", h2),
          table([["Message", "What to do"],
                 ["The card number is too short.", "Numbers need at least 4 digits. Re-swipe or retype."],
                 ["Enter the amount to put on the card.", "Choose an amount button or type one in Other $."],
                 ["A gift card can hold up to $2,000.00.", "Sell less, or split the value over two cards."],
                 ["Card 1001 is already on this check.", "Remove that line first if the amount is wrong."]],
                [2.7, 4.2]),
          PageBreak()]

    # 5 ----------------------------------------------------------------------------------------
    s += [P("5. Paying a check with a gift card", h1),
          shot("m18-settle", "The payment screen: Gift Card… opens the Gift Card screen for this check.", 6.2),
          N(1, "On the payment screen, touch <b>Gift Card…</b>"),
          N(2, "Swipe, scan or type the card and touch <b>Look Up</b>. Check the balance."),
          N(3, "Optional: to take only part of the check from the card, type the amount first (as on the payment "
               "screen)."),
          N(4, "Touch <b>Pay the Check from This Card</b>."),
          N(5, "If no customer is on the check, ViewTouch asks first: <b>Add Customer…</b> opens Customers so the "
               "guest goes on the check (good for their visit history); <b>Bypass &amp; Pay Now</b> pays without one."),
          N(6, "ViewTouch takes the <b>smallest</b> of: what's owed, what's on the card, and the amount you typed. "
               "A message shows how much came off and what's left, e.g. <i>$18.40 from gift card 1001; $31.60 left "
               "on it</i>."),
          N(7, "If something is still owed (the card didn't cover it), go back and take the rest by cash or credit card. "
               "Then close the check as usual."),
          box("Tip:", "Tell the guest the remaining balance from the message, or print the receipt: the payment line "
              "shows <b>Gift Card</b>."),
          P("When a payment is refused", h2),
          table([["Message", "What it means"],
                 ["Gift card 1001 isn't active. Cards work once the check that sold them is paid.",
                  "The selling check isn't paid yet, or the number is wrong."],
                 ["Gift card 1001 is empty.", "No money left; take another payment."],
                 ["Nothing is owed on this check.", "It's already paid."],
                 ["Practice checks can't use a real gift card.", "You're on a training check."],
                 ["No check is open.", "Open the guest's check first."]],
                [3.4, 3.5]),
          PageBreak()]

    # 6 ----------------------------------------------------------------------------------------
    s += [P("6. Cashing out a small balance", h1),
          P("Some states require a store to give cash for a small leftover balance when the guest asks "
            "(California: under $10). ViewTouch does this only when the store's state is chosen in "
            "<b>Store Settings → Staff hours → The store's state</b>, which sets "
            "<b>Cash out gift card balances under ($)</b>. A manager can change that amount; 0 turns cash-out off."),
          N(1, "Look up the card. If its balance is under the limit, the button <b>Cash Out $4.25 (under $10.00)</b> shows."),
          N(2, "Touch it. The cash drawer opens (or it comes from your server bank)."),
          N(3, "Give the guest the cash. The card is now empty; its history says <i>Cashed out</i>."),
          P("The cash is recorded as a pay out from your drawer (reason <i>Gift card 1001 cashed out</i>, category "
            "<i>Gift card cash back</i>), so the drawer still balances at the end of the shift."),
          table([["Message", "What it means"],
                 ["This store doesn't cash out gift cards.", "No state chosen, or the limit is 0 (Store Settings → Staff hours)."],
                 ["Only balances under $10.00 are cashed out.", "The balance is too large for cash-out; it can be spent instead."],
                 ["The card is empty.", "Nothing to cash out."],
                 ["(drawer message)", "You need a drawer or server bank assigned to take cash out."]],
                [3.0, 3.9]),
          PageBreak()]

    # 7 ----------------------------------------------------------------------------------------
    s += [P("7. Fixing mistakes", h1),
          table([["Situation", "What to do", "What happens to the card"],
                 ["Wrong card or amount, check not paid yet", "Void / remove the gift card line on the check, then sell again.",
                  "Nothing: it never had the money."],
                 ["Took a gift card payment by mistake (check still open)", "Undo that payment on the payment screen.",
                  "The amount goes back on the card (<i>Back from check #…</i>)."],
                 ["A paid check that sold a card has to be reopened", "Check… → <b>Reopen a Closed Check…</b> (managers, checks closed today).",
                  "The sold amount is taken back off the card (<i>Check #… reopened</i>). If the card has already been "
                  "spent, ViewTouch refuses: <i>Gift card … from this check has been spent; it can't be taken back.</i>"],
                 ["A gift card payment on a closed check", "Refunds can't send money back to a gift card. Reopen the "
                  "check and undo the payment instead.", "The amount goes back on the card."]],
                [1.9, 2.5, 2.5]),
          box("Why can't I refund to a gift card?", "Refunds go back the way the money came (cash or the credit card). "
              "Gift card and house account payments are put back by undoing the payment, so the card's history stays "
              "correct.", "warn"),
          PageBreak()]

    # 8 ----------------------------------------------------------------------------------------
    s += [P("8. Gift card reports", h1),
          P("Open <b>Manager → Reports</b> and touch <b>Gift Cards</b>. Choose the day or period at the top as for any "
            "report. Along the top of the gift card report are five views:"),
          shot("m48-report-gift-cards", "Reports → Gift Cards (Summary)."),
          table([["View", "Shows"],
                 ["Summary", "Gift cards <b>sold and reloaded</b> (count, amount), <b>spent</b>, and <b>still on cards</b>: the "
                  "total the store owes on all cards. Also house accounts: charged, paid and owed."],
                 ["Activated Cards", "Every card that has been used: number, PIN, first and last name, purchased by, sold by "
                  "(the server on the selling check), status (ACTIVE or USED($0)) with the date, and balance."],
                 ["Active Balances (&gt;$0)", "Only cards with money on them, with the total at the bottom."],
                 ["Inactive Cards", "Card numbers on file that were never activated (AVAIL)."],
                 ["Transactions (Staff / Buyer / Recipient)", "Every sale, reload, payment, cash-out and correction in the "
                  "period: date and time, card, names, who sold it, what happened and the amount. Best printed landscape."]],
                [2.0, 4.9]),
          box("For the books:", "<b>Still on cards (owed by the store)</b> is a liability: money taken for cards that "
              "hasn't been spent yet. Gift card sales are not sales of goods until the card is spent, which is why they "
              "carry no tax."),
          PageBreak()]

    # 9 ----------------------------------------------------------------------------------------
    s += [P("9. Who can do what, and settings", h1),
          table([["Action", "Who"],
                 ["Look up a card, sell or reload", "Anyone who can take orders."],
                 ["Pay a check from a card", "Anyone who can take payments."],
                 ["Cash out a card", "Anyone who can take payments, with a drawer or server bank."],
                 ["Reopen a check that sold a card", "A manager (checks closed today)."],
                 ["Change the cash-out limit / the store's state", "A manager (Store Settings → Staff hours)."]],
                [3.0, 3.9]),
          P("Settings that matter", h2),
          B("<b>Store Settings → Staff hours → The store's state</b>: sets the gift card cash-out rule along with overtime "
            "and breaks. Not chosen means no cash-out."),
          B("<b>Cash out gift card balances under ($)</b>: the limit (California $10); 0 = off."),
          B("<b>Manager → Payment Types</b>: the <b>Gift Card</b> payment type is what shows on receipts and in the reports."),
          B("Barcode scanners need no setting: a scanned number that belongs to a card opens it."),
          PageBreak()]

    # 10 ---------------------------------------------------------------------------------------
    s += [P("10. Quick reference", h1),
          table([["To…", "Touch"],
                 ["Check a balance", "Gift Card screen → swipe/type → Look Up"],
                 ["Sell a card", "Check… → Sell / Check a Gift Card… → number (or blank) → amount → Sell → take payment"],
                 ["Reload a card", "Look up the card → amount → Reload → take payment"],
                 ["Pay with a card", "Payment screen → Gift Card… → Look Up → Pay the Check from This Card"],
                 ["Pay part with a card", "Type the amount first, then Pay the Check from This Card"],
                 ["Cash out a small balance", "Look up → Cash Out (shows only when allowed)"],
                 ["Find a card without the number", "Type a name, PIN or last digits → tap it in Matching Gift Cards"],
                 ["See what's owed on all cards", "Manager → Reports → Gift Cards → Summary"]],
                [2.2, 4.7]),
          P("Good habits", h2),
          B("Always finish paying the check before handing over a card: it only works once the check is paid."),
          B("When selling without a plastic card, give the guest the receipt: it is their card number."),
          B("Tell guests their remaining balance after every payment."),
          B("Count gift card cash-outs as pay outs; they're already recorded in your drawer.")]

    doc.multiBuild(s)


if __name__ == "__main__":
    build()
    print("wrote", OUT)
