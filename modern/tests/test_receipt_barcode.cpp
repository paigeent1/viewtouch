#include <catch2/catch_test_macros.hpp>

#include "layout_fixture.hh"
#include "layoutcontroller.hh"
#include "pos_fixture.hh"
#include "print/document.hh"
#include "print/ticket_image.hh"
#include "print/tickets.hh"
#include "app/pos_json.hh"
#include "qt_catch.hh"

#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QImage>
#include <QSignalSpy>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Store Settings > Receipts > Print a barcode on receipts: a Code 128 barcode of the check
// ("R" + check prefix + "-" + number) at the bottom of receipts, scanned back at the register to
// open that check on Find a check (for a return). Made-up checks only.

namespace {

bool has(const std::string &hay, const std::string &needle) { return hay.find(needle) != std::string::npos; }

print::TicketContext ctx(const core::PosSettings &s)
{
    return {s, [](std::int64_t) { return std::string("Nov 7, 2026 2:15 PM"); },
            [](std::int64_t) { return std::string("2:15 PM"); }, 0};
}

core::Check madeUpCheck(std::int64_t id)
{
    core::Check c;
    c.id = id;
    c.label = "Quick";
    c.serverName = "Casey Test";
    c.status = core::CheckStatus::Closed;
    c.openedAt = QDateTime::currentMSecsSinceEpoch() - 40LL * 24 * 3'600'000;   // weeks ago
    c.closedAt = c.openedAt + 600'000;
    core::MenuItem cigar;
    cigar.id = "test-cigar";
    cigar.name = "Test Robusto";
    cigar.price = Money::fromCents(1200);
    c.addItem(cigar);
    return c;
}

// GS k 73 (Code 128) with `data` in code set B.
std::string code128Command(const std::string &data)
{
    return std::string("\x1dk") + char(73) + char(data.size() + 2) + "{B" + data;
}

bool waitFor(const std::function<bool()> &done, int msec = 5000)
{
    QDeadlineTimer deadline(msec);
    while (!done()) {
        if (deadline.hasExpired())
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

bool searchDone(PosService &pos) { return waitFor([&] { return !pos.checkSearch()[u"loading"_s].toBool(); }); }

qint64 selectedId(PosService &pos) { return pos.checkSearch()[u"selected"_s].toMap()[u"id"_s].toLongLong(); }

// A store with checks #501 and #502 in its history (no database: the history is read).
void withHistory(PosService &pos)
{
    auto history = std::make_shared<std::vector<core::Check>>();
    history->push_back(madeUpCheck(501));
    history->push_back(madeUpCheck(502));
    pos.shared()->history = [history](std::int64_t from, std::int64_t to) {
        std::vector<core::Check> out;
        for (const core::Check &c : *history)
            if (c.closedAt >= from && c.closedAt < to)
                out.push_back(c);
        return out;
    };
}

} // namespace

TEST_CASE("Receipt reference: R + check prefix + - + number; never an item's UPC", "[receipts][barcode]")
{
    CHECK(core::receiptReference("", 42) == "R-42");
    CHECK(core::receiptReference("hca", 5012) == "RHCA-5012");
    CHECK(core::normalCheckPrefix(" bh-dan ") == "BHDAN");
    CHECK(core::normalCheckPrefix("ABCDEFGHIJK") == "ABCDEFGH");   // at most 8

    const auto plain = core::parseReceiptReference("R-42");
    REQUIRE(plain);
    CHECK(plain->prefix.empty());
    CHECK(plain->checkId == 42);
    const auto other = core::parseReceiptReference(" rbhnm-5012 ");
    REQUIRE(other);
    CHECK(other->prefix == "BHNM");
    CHECK(other->checkId == 5012);
    for (const char *notOne : {"012345678905", "4006381333931", "SAVE5", "R-", "R42", "RHCA-", "R-12a", "R-0",
                               "X-42", "R HCA-42", "RABCDEFGHI-1", "", "Robusto"})
        CHECK_FALSE(core::parseReceiptReference(notOne));
}

TEST_CASE("Code 128: start B, the data, its check symbol, stop", "[receipts][barcode]")
{
    const std::vector<bool> m = print::code128Modules("R-42");
    // (start + 4 characters + check) x 11 modules, and the stop's 13.
    REQUIRE(m.size() == 6 * 11 + 13);
    const auto bits = [&](std::size_t from, std::size_t n) {
        std::string s;
        for (std::size_t i = from; i < from + n; ++i)
            s += m[i] ? '1' : '0';
        return s;
    };
    CHECK(bits(0, 11) == "11010010000");    // start B (211214)
    CHECK(bits(11, 11) == "11000101110");   // 'R' = 50 (231131)
    // Check symbol: (104 + 50*1 + 13*2 + 20*3 + 18*4) % 103 = 3 (121223).
    CHECK(bits(55, 11) == "10010011000");
    CHECK(bits(66, 13) == "1100011101011");   // stop (2331112)
}

TEST_CASE("Receipt barcode: off, none; on, at the bottom of receipts, reprints and gift receipts", "[receipts][barcode]")
{
    core::PosSettings s = test::seedPosData().settings;
    const core::Check c = madeUpCheck(42);
    REQUIRE_FALSE(s.receiptBarcode);   // off unless the store turns it on

    SECTION("off: no barcode anywhere")
    {
        for (const print::Document &d : {print::receipt(c, ctx(s)), print::giftReceipt(c, ctx(s))}) {
            CHECK_FALSE(has(print::renderEscPos(d, 42), "\x1dk"));
            CHECK_FALSE(has(print::renderText(d, 42), "R-42"));
            CHECK(std::ranges::none_of(d.lines, [](const print::Document::Line &l) { return l.kind == print::Document::Kind::Barcode; }));
        }
    }
    SECTION("on: GS k Code 128 on the thermal printer, the reference as text on screen")
    {
        s.receiptBarcode = true;
        for (const print::Document &d : {print::receipt(c, ctx(s)), print::giftReceipt(c, ctx(s))}) {
            REQUIRE(!d.lines.empty());
            CHECK(d.lines.back().kind == print::Document::Kind::Barcode);   // it ends the receipt
            for (const int width : {32, 42, 48}) {
                const std::string escpos = print::renderEscPos(d, width);
                CHECK(has(escpos, code128Command("R-42")));
                CHECK(has(escpos, std::string("\x1dH\x02")));   // its text printed under it
                CHECK(has(print::renderText(d, width), "R-42"));
            }
        }
        // The store's prefix is on it.
        s.checkPrefix = "HCA";
        const print::Document d = print::receipt(c, ctx(s));
        CHECK(has(print::renderEscPos(d, 42), code128Command("RHCA-42")));
        CHECK(has(print::renderText(d, 42), "RHCA-42"));
        // The picture of the receipt (Exceptions, PDF) draws the bars: taller, black in the middle.
        s.receiptBarcode = false;
        const QImage without = print::ticketImage(print::receipt(c, ctx(s)), 42);
        const QImage with = print::ticketImage(d, 42);
        CHECK(with.height() > without.height());
    }
}

TEST_CASE("Receipt barcode: the settings are kept, the prefix in capitals; a bad prefix is refused", "[receipts][barcode]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    QVariantMap store = pos.adminRecords(u"store"_s).first().toMap();
    CHECK(store[u"receiptBarcode"_s] == false);
    CHECK(store[u"checkPrefix"_s] == u""_s);
    store[u"receiptBarcode"_s] = true;
    store[u"checkPrefix"_s] = u"hca"_s;
    REQUIRE(pos.adminSave(u"store"_s, 0, store));
    CHECK(pos.shared()->settings.receiptBarcode);
    CHECK(pos.shared()->settings.checkPrefix == "HCA");
    const core::PosSettings back = app::settingsFromJson(app::toJson(pos.shared()->settings));
    CHECK(back.receiptBarcode);
    CHECK(back.checkPrefix == "HCA");
    store[u"checkPrefix"_s] = u"H-C!"_s;
    CHECK_FALSE(pos.adminSave(u"store"_s, 0, store));
    CHECK(pos.shared()->settings.checkPrefix == "HCA");   // unchanged
}

TEST_CASE("Receipt barcode: scanned or typed, Find a check opens that check; another store's says so", "[receipts][barcode][search]")
{
    PosService pos(test::seedPosData(), nullptr);
    withHistory(pos);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    QStringList said;
    QObject::connect(&pos, &PosService::notice, [&](const QString &m) { said << m; });

    // Typed in Find a check's search (a week's range: the reference looks back as far as it must).
    REQUIRE(pos.searchChecks(u"r-501"_s, 7));
    REQUIRE(searchDone(pos));
    CHECK(selectedId(pos) == 501);
    REQUIRE(pos.checkSearch()[u"results"_s].toList().size() == 1);

    // Scanned (the service's half; the screen's is below).
    REQUIRE(pos.openReceiptCode(u"R-502"_s));
    REQUIRE(searchDone(pos));
    CHECK(selectedId(pos) == 502);

    // Another store's receipt: said plainly, nothing looked up.
    said.clear();
    CHECK_FALSE(pos.openReceiptCode(u"RBHNM-501"_s));
    CHECK(said.join(u" "_s).contains(u"That receipt is from BHNM; look it up there."_s));

    // A number nobody has here.
    said.clear();
    REQUIRE(pos.openReceiptCode(u"R-999"_s));
    REQUIRE(searchDone(pos));
    CHECK(selectedId(pos) == 0);
    CHECK(said.join(u" "_s).contains(u"No check #999 here."_s));

    // This store has a prefix now: its own receipts carry it; one without a prefix isn't taken as ours.
    pos.shared()->settings.checkPrefix = "HCA";
    REQUIRE(pos.openReceiptCode(u"RHCA-501"_s));
    REQUIRE(searchDone(pos));
    CHECK(selectedId(pos) == 501);
    said.clear();
    CHECK_FALSE(pos.openReceiptCode(u"R-501"_s));
    CHECK(said.join(u" "_s).contains(u"#501"_s));
    CHECK_FALSE(pos.openReceiptCode(u"012345678905"_s));   // an item's UPC: not a receipt
}

TEST_CASE("Receipt barcode: a scan at the register opens Find a check on that check, not an item", "[receipts][barcode][controller]")
{
    auto l = vt::test::loadTestLayout();
    REQUIRE(l);
    PosService pos(test::seedPosData(), nullptr);
    withHistory(pos);
    LayoutController c(*l);
    c.setMealPeriod(u"lunch"_s);
    c.setPos(&pos);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    QStringList said;
    QObject::connect(&pos, &PosService::notice, [&](const QString &m) { said << m; });
    QSignalSpy notOnFile(&c, &LayoutController::unrecognizedBarcode);

    REQUIRE(pos.startCheck(core::CheckType::Quick));   // a sale started: the scan still isn't an item
    REQUIRE(c.jumpTo(u"retail-entry"_s));
    const qsizetype linesBefore = pos.lines().size();
    c.scanned(u"R-501"_s);
    REQUIRE(searchDone(pos));
    CHECK(c.pageId() == u"find-check"_s);
    CHECK(selectedId(pos) == 501);
    CHECK(pos.lines().size() == linesBefore);   // nothing rung in
    CHECK(notOnFile.isEmpty());                 // no "not on file: add it?"

    // On Look Up Checks too.
    REQUIRE(c.jumpTo(u"receipt-lookup"_s));
    c.scanned(u"R-502"_s);
    REQUIRE(searchDone(pos));
    CHECK(c.pageId() == u"find-check"_s);
    CHECK(selectedId(pos) == 502);

    // Another store's, on the order screen: said; the screen stays where it is.
    REQUIRE(c.jumpTo(u"menu-all"_s));
    said.clear();
    c.scanned(u"RHCA-501"_s);
    CHECK(c.pageId() == u"menu-all"_s);
    CHECK(said.join(u" "_s).contains(u"That receipt is from HCA; look it up there."_s));
    CHECK(notOnFile.isEmpty());
}

TEST_CASE("Retail Reprint Receipt opens Look Up Checks; Cancel goes back to where it was opened", "[receipts][controller]")
{
    auto l = vt::test::loadTestLayout();
    REQUIRE(l);
    PosService pos(test::seedPosData(), nullptr);
    LayoutController c(*l);
    c.setMealPeriod(u"lunch"_s);
    c.setPos(&pos);
    REQUIRE(pos.loginWithPin(u"1234"_s));

    // Retail Mode's Reprint Receipt: the same screen as Cashier Duty's LOOK UP CHECKS.
    REQUIRE(c.jumpTo(u"retail-entry"_s));
    c.activate(u"rtl-reprint"_s);
    CHECK(c.pageId() == u"receipt-lookup"_s);
    c.goBack();   // its Cancel
    CHECK(c.pageId() == u"retail-entry"_s);

    // From Cashier Duty it still goes back to Cashier Duty.
    REQUIRE(c.jumpTo(u"cashier-duty"_s));
    c.activate(u"cd-lookup"_s);
    CHECK(c.pageId() == u"receipt-lookup"_s);
    c.goBack();
    CHECK(c.pageId() == u"cashier-duty"_s);
}

TEST_CASE("Retail register keys: the right column on the keypad rows, MISC MDSE / MISC FOOD/BEV ring the amount typed",
          "[retail][controller][layout]")
{
    auto l = vt::test::loadTestLayout();
    REQUIRE(l);
    const vt::layout::Page *page = l->page(u"retail-entry"_s);
    REQUIRE(page);
    const auto zone = [&](const QString &id) -> const vt::layout::Zone * {
        for (const vt::layout::Zone &z : page->zones)
            if (z.id == id)
                return &z;
        return nullptr;
    };
    // SUSPEND, RESUME, CLEAR, SUB TOTAL down the right, each beside a keypad row and as tall as a key.
    const QString column[] = {u"rtl-suspend"_s, u"rtl-resume"_s, u"rtl-clear"_s, u"rtl-subtotal"_s};
    const QString rows[] = {u"rtl-key-9"_s, u"rtl-key-6"_s, u"rtl-key-3"_s, u"rtl-key-."_s};
    for (int i = 0; i < 4; ++i) {
        const vt::layout::Zone *z = zone(column[i]), *key = zone(rows[i]);
        REQUIRE(z);
        REQUIRE(key);
        CHECK(z->rect.y() == key->rect.y());
        CHECK(z->rect.height() == key->rect.height());
        CHECK(z->rect.x() >= key->rect.right());
    }
    // MISC FOOD/BEV where SUSPEND was; MISC MDSE beside it; no TIME CLOCK; the void key names itself.
    REQUIRE(zone(u"rtl-miscfood"_s));
    CHECK(zone(u"rtl-miscfood"_s)->rect == QRect(1732, 364, 172, 84));
    CHECK(zone(u"rtl-misc"_s)->label == u"MISC MDSE"_s);
    CHECK(zone(u"rtl-miscfood"_s)->label == u"MISC FOOD/BEV"_s);
    CHECK_FALSE(zone(u"rtl-clock"_s));
    CHECK(zone(u"rtl-void"_s)->label == u"{voidKey}"_s);
    CHECK(zone(u"retail-check"_s)->props.value(u"touchToSelect"_s).toBool());
    CHECK(zone(u"rtl-cancel"_s)->actions.front().str(u"name") == u"cancelCheck"_s);   // Cancel Sale (cancelSale)

    PosService pos(test::seedPosData(), nullptr);
    pos.shared()->settings.tax.merchandisePpm = 63500;
    pos.shared()->settings.tax.foodPpm = 73500;
    LayoutController c(*l);
    c.setMealPeriod(u"lunch"_s);
    c.setPos(&pos);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(c.jumpTo(u"retail-entry"_s));
    for (const QString &k : {u"rtl-key-1"_s, u"rtl-key-2"_s, u"rtl-key-00"_s})   // $12.00
        c.activate(k);
    c.activate(u"rtl-misc"_s);
    c.activate(u"rtl-key-5"_s);
    c.activate(u"rtl-key-00"_s);   // $5.00
    c.activate(u"rtl-miscfood"_s);
    const QVariantList lines = pos.lines();
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].toMap()[u"name"_s].toString() == u"Misc Merchandise"_s);
    CHECK(lines[0].toMap()[u"price"_s].toString() == pos.format(core::Money::fromCents(1200)));
    CHECK(lines[1].toMap()[u"name"_s].toString() == u"Misc Food/Bev"_s);
    REQUIRE(pos.shared()->open.size() == 1);
    const core::Check &check = pos.shared()->open.begin()->second;
    CHECK(check.lines[0].taxClass == core::TaxClass::Merchandise);
    CHECK(check.lines[1].taxClass == core::TaxClass::Food);
    CHECK(c.pageId() == u"retail-entry"_s);
}

TEST_CASE("A login lands back in the mode the screen was in: Retail Mode with its sale, or hospitality",
          "[retail][controller][login]")
{
    auto l = vt::test::loadTestLayout();
    REQUIRE(l);
    PosService pos(test::seedPosData(), nullptr);
    LayoutController c(*l);
    c.setMealPeriod(u"lunch"_s);
    c.setPos(&pos);

    // The Retail register can be a terminal's Start screen (Manager -> Terminals).
    const QVariantList starts = c.pageChoices(u"start"_s);
    CHECK(std::ranges::any_of(starts, [](const QVariant &v) { return v.toMap()[u"value"_s].toString() == u"retail-entry"_s; }));

    // Retail Mode, a sale under way, the screen times out: back in, Retail Mode with that sale.
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(c.jumpTo(u"retail-entry"_s));
    REQUIRE(pos.addItem(u"caesar"_s));
    const qint64 sale = pos.checkInfo()[u"id"_s].toLongLong();
    pos.logout();
    CHECK(c.pageId() != u"retail-entry"_s);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    CHECK(c.pageId() == u"retail-entry"_s);   // not the hospitality order screen
    REQUIRE(pos.hasCheck());
    CHECK(pos.checkInfo()[u"id"_s].toLongLong() == sale);

    // No sale open: still Retail Mode.
    pos.releaseCheck();
    pos.logout();
    REQUIRE(pos.loginWithPin(u"1234"_s));
    CHECK(c.pageId() == u"retail-entry"_s);

    // Hospitality Mode (the floor): a login lands where it always did, not on the Retail register.
    c.activate(u"hosp-mode"_s);
    REQUIRE(c.pageId() == u"tables"_s);
    pos.logout();
    REQUIRE(pos.loginWithPin(u"1234"_s));
    CHECK(c.pageId() != u"retail-entry"_s);
}
