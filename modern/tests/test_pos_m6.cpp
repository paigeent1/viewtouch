#include <catch2/catch_test_macros.hpp>

#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QSignalSpy>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
using vt::app::PosShared;
using vt::core::CashMovement;

// M6: tips, gratuity, per-terminal drawers, pay-outs, tip cash-out.

namespace {

Money usd(std::int64_t c) { return Money::fromCents(c); }

app::PosData withGratuity(std::int64_t bp, int minGuests)
{
    app::PosData d = test::seedPosData();
    d.settings.gratuityBp = bp;
    d.settings.gratuityMinGuests = minGuests;
    return d;
}

} // namespace

TEST_CASE("Core: gratuity is added after tax; tips ride on top", "[m6][check]")
{
    core::Check c;
    core::MenuItem steak{"steak", "Steak", "", usd(10000), core::TaxClass::Food};
    c.addItem(steak);
    c.gratuityBp = 1800;
    core::TaxRates r;
    r.foodPpm = 82500;
    core::Totals t = c.totals(r);
    CHECK(t.tax == usd(825));
    CHECK(t.gratuity == usd(1800));          // 18% of the subtotal, untaxed
    CHECK(t.total == usd(10000 + 825 + 1800));

    core::Tender card{"credit", "Credit Card", core::TenderKind::Card, 0};
    auto &p = c.addPayment(card, t.total);
    p.tip = usd(500);
    t = c.totals(r);
    CHECK(t.balance == usd(0));              // the tip is not part of the balance
    CHECK(t.tips == usd(500));
}

TEST_CASE("Tips on card payments: percent or keypad amount", "[m6][tips]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));
    pos.addItem(u"cobb"_s);                   // 12.50 + 1.03 = 13.53
    QSignalSpy notices(&pos, &PosService::notice);
    CHECK_FALSE(pos.addTip(1800));            // no card payment yet
    CHECK(notices.last()[0].toString().contains(u"card"_s));

    REQUIRE(pos.tender(u"credit"_s));
    REQUIRE(pos.addTip(2000));                // 20% of 13.53 = 2.706 -> 2.71
    CHECK(pos.totals()[u"tips"_s].toString() == u"$2.71"_s);
    CHECK(pos.payments()[0].toMap()[u"tip"_s].toString() == u"$2.71"_s);

    pos.entryKey(u"300"_s);
    REQUIRE(pos.addTip(0));                   // $3.00 typed, replaces the 20%
    CHECK(pos.totals()[u"tips"_s].toString() == u"$3.00"_s);
    CHECK(pos.totals()[u"balanceCents"_s].toLongLong() == 0);
}

TEST_CASE("Auto-gratuity for big tables; lowering it needs a manager", "[m6][gratuity]")
{
    PosService pos(withGratuity(1800, 6), nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));

    pos.selectTable(u"T1"_s);
    pos.entryKey(u"4"_s);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    CHECK_FALSE(pos.totals()[u"hasGratuity"_s].toBool());   // 4 guests: none
    pos.releaseCheck();

    pos.selectTable(u"T5"_s);
    pos.entryKey(u"8"_s);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"cobb"_s);                    // 12.50: gratuity 2.25
    CHECK(pos.totals()[u"gratuity"_s].toString() == u"$2.25"_s);
    CHECK(pos.totals()[u"autoGratuity"_s].toBool());
    CHECK_FALSE(pos.setGratuity(0));           // server can't remove the party gratuity
    REQUIRE(pos.setGratuity(2000));            // raising is fine
    CHECK(pos.totals()[u"gratuity"_s].toString() == u"$2.50"_s);

    const qint64 id = pos.checkInfo()[u"id"_s].toLongLong();
    pos.logout();
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.openCheck(id));
    REQUIRE(pos.setGratuity(0));
    CHECK_FALSE(pos.totals()[u"hasGratuity"_s].toBool());
}

TEST_CASE("Each terminal has its own drawer and receipt printer", "[m6][drawers]")
{
    app::PosData data = test::seedPosData();
    data.settings.terminals = {{"Bar", "bar"}};
    for (core::PrinterConfig &p : data.settings.printers)
        if (p.id == "bar")
            p.drawerKick = true;   // the bar's drawer is wired to the bar's printer
    PosShared shared(data, nullptr);
    struct Kicks : app::PosPrinter {
        std::vector<std::string> printers;
        void printKitchen(const core::PosSettings &, const core::Check &, const std::vector<core::OrderLine> &, bool) override {}
        void printReceipt(const core::PosSettings &, const core::Check &, const std::string &p) override { printers.push_back(p); }
        void printReport(const core::PosSettings &, const core::Report &, const std::string &) override {}
        void openDrawer(const core::PosSettings &, const std::string &p) override { printers.push_back(p); }
    } kicks;
    shared.printer = &kicks;
    PosService front(&shared, u"Front"_s);
    PosService bar(&shared, u"Bar"_s);
    REQUIRE(front.loginWithPin(u"2222"_s));
    REQUIRE(bar.loginWithPin(u"1234"_s));

    front.entryKey(u"10000"_s);
    REQUIRE(front.openDrawerSession());
    CHECK(kicks.printers.back() == "receipt");   // Front: default receipt printer
    CHECK(front.drawerInfo()[u"name"_s].toString() == u"Front drawer"_s);
    CHECK_FALSE(bar.drawerInfo()[u"exists"_s].toBool());

    // Bar can't take cash into Front's drawer.
    bar.addItem(u"draft-beer"_s);
    REQUIRE(bar.tender(u"cash"_s));
    CHECK_FALSE(bar.closeCheck());
    bar.entryKey(u"5000"_s);
    REQUIRE(bar.openDrawerSession());
    CHECK(kicks.printers.back() == "bar");       // Bar: its own printer
    REQUIRE(bar.closeCheck());
    CHECK(bar.drawerInfo()[u"cashSales"_s].toString() == u"$6.60"_s);
    CHECK(front.drawerInfo()[u"cashSales"_s].toString() == u"$0.00"_s);

    // End of day waits for every drawer.
    CHECK(bar.dayInfo()[u"blockers"_s].toStringList().size() == 2);
    bar.entryKey(u"5660"_s);
    REQUIRE(bar.countDrawer());
    CHECK(bar.dayInfo()[u"blockers"_s].toStringList().size() == 1);
    CHECK_FALSE(bar.endOfDay());
}

TEST_CASE("Pay-outs and paid-ins change the expected cash", "[m6][drawers]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
    pos.entryKey(u"2000"_s);
    CHECK_FALSE(pos.payout(CashMovement::Kind::Payout));   // servers can't
    pos.logout();

    REQUIRE(pos.loginWithPin(u"1234"_s));
    for (const char *k : {"B", "a", "g", "s"})
        pos.textKey(QString::fromLatin1(k));
    pos.entryKey(u"2000"_s);
    CHECK_FALSE(pos.payout(CashMovement::Kind::Payout));    // what was it for?
    pos.setExpenseCategory(u"Ice"_s);
    REQUIRE(pos.payout(CashMovement::Kind::Payout));        // $20 for ice
    pos.entryKey(u"500"_s);
    REQUIRE(pos.payout(CashMovement::Kind::PaidIn));
    const QVariantMap d = pos.drawerInfo();
    CHECK(d[u"expected"_s].toString() == u"$85.00"_s);
    REQUIRE(d[u"movements"_s].toList().size() == 2);
    CHECK(d[u"movements"_s].toList()[0].toMap()[u"what"_s].toString() == u"Paid out · Ice: Bags"_s);
    // Reports -> Expenses adds them up by category.
    const auto expenses = pos.buildReport(u"expenses"_s);
    bool ice = false;
    for (const core::ReportRow &row : expenses.rows)
        ice = ice || (row.cells.size() == 4 && row.cells[0] == "Ice" && row.cells[3] == "$20.00");
    CHECK(ice);
    CHECK(pos.entry().isEmpty());
    CHECK(pos.textEntry().isEmpty());

    pos.entryKey(u"8500"_s);
    REQUIRE(pos.countDrawer());
    CHECK(pos.drawerInfo()[u"overShortCents"_s].toLongLong() == 0);
}

TEST_CASE("Servers cash out their tips and gratuity from the drawer", "[m6][tips]")
{
    PosService pos(withGratuity(1800, 2), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
    pos.logout();

    REQUIRE(pos.loginWithPin(u"1111"_s));   // Sam
    CHECK(pos.tipsOwed() == u"$0.00"_s);
    CHECK_FALSE(pos.cashOutTips());
    pos.selectTable(u"T3"_s);
    pos.entryKey(u"2"_s);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));  // 2 guests: 18% gratuity
    pos.addItem(u"cobb"_s);                             // 12.50 + 1.03 + 2.25 = 15.78
    REQUIRE(pos.tender(u"credit"_s));
    pos.entryKey(u"200"_s);
    REQUIRE(pos.addTip(0));                             // + $2 tip
    REQUIRE(pos.closeCheck());
    CHECK(pos.tipsOwed() == u"$4.25"_s);

    REQUIRE(pos.cashOutTips());
    CHECK(pos.tipsOwed() == u"$0.00"_s);
    CHECK(pos.drawerInfo()[u"expected"_s].toString() == u"$95.75"_s);   // 100 - 4.25

    const auto sam = pos.buildReport(u"tips"_s).find("Sam");
    REQUIRE(sam.size() == 5);
    CHECK(sam[1] == "$2.00");
    CHECK(sam[2] == "$2.25");
    CHECK(sam[3] == "$4.25");
    CHECK(sam[4] == "$0.00");
    CHECK(pos.buildReport(u"sales"_s).find("Gratuity").back() == "$2.25");
    CHECK(pos.buildReport(u"sales"_s).find("Card tips (owed to staff)").back() == "$2.00");
}

TEST_CASE("Admin: gratuity rules and terminal printers", "[m6][admin]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    QVariantMap store = pos.adminRecords(u"store"_s)[0].toMap();
    store[u"gratuityPercent"_s] = 20.0;
    store[u"gratuityMinGuests"_s] = 8;
    REQUIRE(pos.adminSave(u"store"_s, 0, store));
    CHECK(pos.settings().gratuityBp == 2000);
    CHECK(pos.settings().gratuityMinGuests == 8);

    QVariantMap t = pos.adminNewRecord(u"terminals"_s);
    t[u"name"_s] = u"Patio"_s;
    t[u"receiptPrinter"_s] = u"bar"_s;
    REQUIRE(pos.adminSave(u"terminals"_s, -1, t));
    CHECK(pos.settings().receiptPrinterFor("Patio") == "bar");
    CHECK(pos.settings().receiptPrinterFor("Front") == "receipt");
    CHECK_FALSE(pos.adminSave(u"terminals"_s, -1, t));   // duplicate name
}

TEST_CASE("Cash rounding at the register: Cash takes the rounded balance; the report shows it", "[m6][rounding]")
{
    app::PosData data = test::seedPosData();
    data.settings.tax = {};                      // no tax: easy totals
    data.settings.tax.cashRoundingCents = 5;
    PosService pos(data, nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.openDrawerSession());
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"soda"_s);                      // $2.95
    pos.finishChoosing();
    pos.addItem(u"coffee"_s);                    // $2.75: $5.70
    pos.finishChoosing();
    pos.addItem(u"juice"_s);                     // $3.50: $9.20
    pos.finishChoosing();
    pos.addItem(u"lemonade"_s);                  // $3.25: $12.45
    pos.finishChoosing();
    pos.addItem(u"tea"_s);                       // $2.50: $14.95
    pos.finishChoosing();
    pos.addItem(u"water"_s);
    // Make it end in 3 cents: a $0.08 item.
    core::MenuItem mint;
    mint.id = "mint";
    mint.name = "Mint";
    mint.price = Money::fromCents(8);
    pos.shared()->menu.push_back(mint);
    pos.addItem(u"mint"_s);                      // $15.03
    REQUIRE(pos.totals()[u"total"_s] == u"$15.03"_s);
    REQUIRE(pos.tender(u"cash"_s));              // takes $15.05
    const QVariantMap t = pos.totals();
    CHECK(t[u"hasRounding"_s].toBool());
    CHECK(t[u"rounding"_s] == u"$0.02"_s);
    CHECK(t[u"balanceCents"_s].toLongLong() == 0);
    REQUIRE(pos.closeCheck());
    bool line = false;
    for (const core::ReportRow &row : pos.buildReport(u"sales"_s).rows)
        line = line || (row.cells.size() == 2 && row.cells[0] == "Cash rounding" && row.cells[1] == "$0.02");
    CHECK(line);
}

namespace {

// BHDAN, 2026-11-09: "Terminal" prints on a USB printer with no drawer wired to it,
// while the old network "Receipt" printer still has one.
app::PosData usbWithoutDrawer()
{
    app::PosData data = test::seedPosData();
    core::PrinterConfig usb;
    usb.id = "usb";
    usb.name = "VT-BHDAN-RECEIPT";
    usb.type = "file";
    usb.path = "usb.txt";
    usb.drawerKick = false;
    data.settings.printers.push_back(usb);
    data.settings.terminals = {{"Terminal", "usb"}};
    data.settings.noSaleReasons.clear();   // No Sale opens without asking why
    return data;
}

struct DrawerKicks : app::PosPrinter {
    std::vector<std::string> kicks;
    std::vector<std::string> noSales;
    void printKitchen(const core::PosSettings &, const core::Check &, const std::vector<core::OrderLine> &, bool) override {}
    void printReceipt(const core::PosSettings &, const core::Check &, const std::string &) override {}
    void printReport(const core::PosSettings &, const core::Report &, const std::string &) override {}
    void openDrawer(const core::PosSettings &, const std::string &p) override { kicks.push_back(p); }
    void printNoSale(const core::PosSettings &s, const core::CashMovement &m, int n, const std::string &d,
                     const std::string &p) override
    {
        noSales.push_back(p);
        PosPrinter::printNoSale(s, m, n, d, p);   // the kick: only on that printer, when it has a drawer
    }
};

QVariantMap drawerAlert(const PosService &pos)
{
    for (const QVariant &v : pos.printerAlerts())
        if (v.toMap()[u"id"_s] == u"drawer"_s)
            return v.toMap();
    return {};
}

} // namespace

TEST_CASE("No borrowing: the drawer opens only on the terminal's own printer, and the screen says when it didn't",
          "[m6][drawers][nosale]")
{
    PosShared shared(usbWithoutDrawer(), nullptr);
    DrawerKicks printer;
    shared.printer = &printer;
    PosService pos(&shared, u"Terminal"_s);
    QStringList said;
    QObject::connect(&pos, &PosService::notice, [&](const QString &n) { said << n; });
    REQUIRE(pos.loginWithPin(u"1234"_s));

    // Starting the drawer: no kick on Receipt (it has a drawer, but it isn't this terminal's printer).
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
    CHECK(printer.kicks.empty());
    const QString why = u"No cash drawer on VT-BHDAN-RECEIPT (Printers > Cash drawer is Off) — the drawer was not opened."_s;
    CHECK(drawerAlert(pos)[u"text"_s] == why);
    CHECK(drawerAlert(pos)[u"urgent"_s].toBool());

    // No Sale: still recorded, its slip on this terminal's printer, but no drawer opened anywhere.
    REQUIRE(pos.noSale());
    CHECK(printer.noSales == std::vector<std::string>{"usb"});
    CHECK(printer.kicks.empty());
    REQUIRE(!said.isEmpty());
    CHECK(said.last().contains(u"did not open"_s));
    const core::DrawerSession *d = shared.openDrawerFor("Terminal");
    REQUIRE(d);
    CHECK(std::ranges::count_if(d->movements, [](const CashMovement &m) { return m.kind == CashMovement::Kind::NoSale; }) == 1);

    // A cash sale: the same.
    pos.addItem(u"coffee"_s);
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    CHECK(printer.kicks.empty());

    // Only this screen is told; Store Check keeps it for the managers.
    PosService bar(&shared, u"Bar"_s);
    CHECK(drawerAlert(bar).isEmpty());
    bool kept = false;
    for (const QVariant &p : pos.storeCheck()[u"problems"_s].toList())
        kept = kept || p.toMap()[u"text"_s].toString().contains(u"No cash drawer on VT-BHDAN-RECEIPT"_s);
    CHECK(kept);

    // Terminals: "This printer has the drawer". The alert goes, and the drawer opens there.
    REQUIRE(pos.printerHasDrawer(u"usb"_s));
    CHECK(shared.settings.printer("usb")->drawerKick);
    CHECK(drawerAlert(pos).isEmpty());
    REQUIRE(pos.noSale());
    CHECK(printer.kicks == std::vector<std::string>{"usb"});
    CHECK(said.last().startsWith(u"Drawer opened"_s));
}

TEST_CASE("A printer with the drawer still opens it; tablets without drawers are unchanged", "[m6][drawers][nosale]")
{
    PosShared shared(usbWithoutDrawer(), nullptr);
    shared.settings.terminals.push_back({"Patio", "usb", "no"});   // a tablet: no drawer by design
    DrawerKicks printer;
    shared.printer = &printer;

    // Not in Terminals: the Receipt printer is its own (it has the drawer), as before.
    PosService front(&shared, u"Front"_s);
    REQUIRE(front.loginWithPin(u"1234"_s));
    front.entryKey(u"10000"_s);
    REQUIRE(front.openDrawerSession());
    CHECK(printer.kicks == std::vector<std::string>{"receipt"});
    REQUIRE(front.noSale());
    CHECK(printer.kicks == std::vector<std::string>{"receipt", "receipt"});
    CHECK(drawerAlert(front).isEmpty());

    // The tablet: No Sale is off there, and nothing is kicked or alerted, as before.
    PosService patio(&shared, u"Patio"_s);
    REQUIRE(patio.loginWithPin(u"2222"_s));
    CHECK_FALSE(patio.noSale());
    CHECK(printer.kicks.size() == 2);
    CHECK(drawerAlert(patio).isEmpty());
}

TEST_CASE("Terminals: each printer says if it has a cash drawer; screens not listed are named", "[m6][drawers][admin]")
{
    PosShared shared(usbWithoutDrawer(), nullptr);
    shared.network = [] { return QVariantMap{{u"terminals"_s, QVariantList{QVariantMap{{u"name"_s, u"Back Office"_s}}}}}; };
    PosService pos(&shared, u"Terminal"_s);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    QVariantMap picker;
    for (const QVariant &f : pos.adminFields(u"terminals"_s))
        if (f.toMap()[u"path"_s] == u"receiptPrinter"_s)
            picker = f.toMap();
    REQUIRE(!picker.isEmpty());
    CHECK(picker[u"label"_s] == u"Receipts (and cash drawer) print on"_s);
    QHash<QString, QVariantMap> options;
    for (const QVariant &o : picker[u"options"_s].toList())
        options.insert(o.toMap()[u"value"_s].toString(), o.toMap());
    CHECK(options[u"usb"_s][u"text"_s] == u"VT-BHDAN-RECEIPT · no cash drawer"_s);
    CHECK_FALSE(options[u"usb"_s][u"drawer"_s].toBool());
    CHECK(options[u"receipt"_s][u"text"_s] == u"Receipt · has cash drawer"_s);
    CHECK(options[u""_s][u"text"_s] == u"Receipt (default) · has cash drawer"_s);
    // Screens with no line here, and where they print.
    CHECK(picker[u"hint"_s].toString().contains(
        u"Not listed in Terminals: Back Office. They print receipts on Receipt (has cash drawer)."_s));
    const QVariantList checks = pos.storeCheck()[u"checks"_s].toList();
    const auto detail = [&](const QString &id) {
        for (const QVariant &c : checks)
            if (c.toMap()[u"id"_s] == id)
                return c.toMap()[u"detail"_s].toString();
        return QString();
    };
    CHECK(detail(u"unlistedScreens"_s).contains(u"Back Office"_s));
    CHECK(detail(u"drawerPrinters"_s).contains(u"Terminal prints on VT-BHDAN-RECEIPT, which has no cash drawer"_s));

    // Printers: who uses each one.
    for (const QVariant &r : pos.adminRecords(u"printers"_s)) {
        const QVariantMap m = r.toMap();
        if (m[u"id"_s] == u"usb"_s)
            CHECK(m[u"_usedBy"_s].toString() == u"Terminals using it for receipts and the cash drawer: Terminal"_s);
        if (m[u"id"_s] == u"kitchen"_s)
            CHECK(m[u"_usedBy"_s].toString() == u"No terminal prints its receipts here."_s);
    }
}
