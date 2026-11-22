#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "core/report.hh"
#include "core/scan_data.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QSignalSpy>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// ITEM RETURN on the Retail screen (owner 2026-11-19): the item scanned, at its price-book price or as
// changed, comes back on the sale as a negative line taxed in its own class; the sale settles with a
// cash refund out of the drawer, and the item goes back into stock.

namespace {

constexpr const char *kUpc = "012345678905";

app::PosData returnData(bool taxIncluded = false)
{
    app::PosData d = test::seedPosData();
    d.settings.tax.merchandisePpm = 82500;   // 8.25%
    d.settings.tax.foodPpm = 82500;
    d.settings.tax.pricesIncludeTax = taxIncluded;
    d.settings.tax.cashRoundingCents = 0;
    core::MenuItem m;
    m.id = "cutter";
    m.name = "Cigar Cutter";
    m.price = Money::fromCents(1000);
    m.taxClass = core::TaxClass::Merchandise;
    m.barcodes = {kUpc};
    m.kitchenHide = true;
    m.recipe = {{"cutter-stock", 1}};
    d.menu.push_back(m);
    core::Ingredient g;
    g.id = "cutter-stock";
    g.name = "Cigar cutters";
    g.onHand = 5;
    d.ingredients.push_back(g);
    return d;
}

void openDrawer(PosService &pos)
{
    REQUIRE(pos.loginWithPin(u"1234"_s));   // a manager
    pos.entryKey(u"10000"_s);               // $100.00 to start
    REQUIRE(pos.openDrawerSession());
}

// ITEM RETURN > Scan > the code: the price window, filled in.
QVariantMap scanReturn(PosService &pos, const QString &code)
{
    REQUIRE(pos.startItemReturn());
    CHECK(pos.itemReturn()[u"step"_s] == u"choose"_s);
    REQUIRE(pos.itemReturnStep(u"scan"_s));
    REQUIRE(pos.itemReturnCode(code));
    REQUIRE(pos.itemReturn()[u"step"_s] == u"price"_s);
    return pos.itemReturn()[u"item"_s].toMap();
}

const core::Check &current(PosService &pos)
{
    const qint64 id = pos.checkInfo()[u"id"_s].toLongLong();
    return pos.shared()->open.at(id);
}

double onHand(PosService &pos, const char *id)
{
    const core::Ingredient *g = pos.shared()->ingredient(id);
    REQUIRE(g);
    return g->onHand;
}

} // namespace

TEST_CASE("Item return: the scanned item comes back negative, with its tax", "[itemreturn]")
{
    PosService pos(returnData(), nullptr);
    openDrawer(pos);

    const QVariantMap item = scanReturn(pos, QString::fromLatin1(kUpc));
    CHECK(item[u"name"_s] == u"Cigar Cutter"_s);
    CHECK(item[u"priceCents"_s].toLongLong() == 1000);   // the price book's, filled in
    CHECK(item[u"price"_s] == u"$10.00"_s);
    CHECK(pos.itemReturn()[u"reasons"_s].toStringList()
          == QStringList({u"Defective"_s, u"Wrong item"_s, u"Changed mind"_s, u"Other"_s}));

    CHECK_FALSE(pos.addItemReturn(u"cutter"_s, 1000, 1, u""_s, QString::fromLatin1(kUpc)));   // why?
    CHECK_FALSE(pos.addItemReturn(u"cutter"_s, 0, 1, u"Defective"_s, {}));                    // a price
    REQUIRE(pos.addItemReturn(u"cutter"_s, 1000, 1, u"Defective"_s, QString::fromLatin1(kUpc)));
    CHECK(pos.itemReturn().isEmpty());   // the window closes

    const core::Check &c = current(pos);
    REQUIRE(c.lines.size() == 1);
    const core::OrderLine &l = c.lines.front();
    CHECK(l.isReturn());
    CHECK(l.quantity == -1);
    CHECK(l.displayName() == "RETURN  Cigar Cutter");
    CHECK(l.returnReason == "Defective");
    CHECK(l.upc == kUpc);
    CHECK(l.taxClass == core::TaxClass::Merchandise);
    CHECK(l.total().cents() == -1000);

    // The tax comes back too, at the item's own class: $10.00 at 8.25% = $0.83.
    const core::Totals t = c.totals(pos.shared()->settings.tax);
    CHECK(t.items.cents() == -1000);
    CHECK(t.tax.cents() == -83);
    CHECK(t.taxByClass.at(core::TaxClass::Merchandise).cents() == -83);
    CHECK(t.total.cents() == -1083);

    // An exception, with its reason (Exceptions report, search, Loss Watch).
    const auto ev = std::ranges::find(c.events, std::string("itemReturn"), &core::CheckEvent::kind);
    REQUIRE(ev != c.events.end());
    CHECK(ev->amount.cents() == 1000);
    CHECK(ev->reason == "Defective");

    // It keeps its count: no + / − on it.
    pos.selectLine(l.id);
    CHECK_FALSE(pos.changeLineQuantity(l.id, 1));
    CHECK_FALSE(pos.setLineQuantity(l.id, 2));
    CHECK(current(pos).lines.front().quantity == -1);

    // Saved and read back with its reason.
    const std::optional<core::Check> back = app::checkFromJson(app::toJson(c));
    REQUIRE(back);
    CHECK(back->lines.front().returnReason == "Defective");
    CHECK(back->lines.front().quantity == -1);
}

TEST_CASE("Item return: the cashier changes the price and how many", "[itemreturn]")
{
    PosService pos(returnData(), nullptr);
    openDrawer(pos);

    // Part of the name works too (one item: straight to its price).
    REQUIRE(pos.startItemReturn());
    REQUIRE(pos.itemReturnStep(u"scan"_s));
    REQUIRE(pos.itemReturnCode(u"cigar cut"_s));
    REQUIRE(pos.itemReturn()[u"step"_s] == u"price"_s);
    CHECK(pos.itemReturn()[u"item"_s].toMap()[u"priceCents"_s].toLongLong() == 1000);

    // It sold for $7.50 (on sale then): two of them.
    REQUIRE(pos.addItemReturn(u"cutter"_s, 750, 2, u"Changed mind"_s, {}));
    const core::OrderLine &l = current(pos).lines.front();
    CHECK(l.quantity == -2);
    CHECK(l.unitPrice.cents() == 750);
    CHECK(l.total().cents() == -1500);
    const core::Totals t = current(pos).totals(pos.shared()->settings.tax);
    CHECK(t.tax.cents() == -124);   // 15.00 x 8.25% = 1.2375
    CHECK(t.total.cents() == -1624);
    // The menu's price is unchanged.
    CHECK(pos.findItem(u"cutter"_s)->price.cents() == 1000);

    // Cancel adds nothing.
    scanReturn(pos, QString::fromLatin1(kUpc));
    pos.cancelItemReturn();
    CHECK(pos.itemReturn().isEmpty());
    CHECK(current(pos).lines.size() == 1);
}

TEST_CASE("Item return: a sale less than nothing settles with a cash refund from the drawer", "[itemreturn]")
{
    PosService pos(returnData(), nullptr);
    openDrawer(pos);
    const double stock = onHand(pos, "cutter-stock");

    scanReturn(pos, QString::fromLatin1(kUpc));
    REQUIRE(pos.addItemReturn(u"cutter"_s, 1000, 1, u"Defective"_s, QString::fromLatin1(kUpc)));
    const qint64 id = pos.checkInfo()[u"id"_s].toLongLong();

    // Pay shows the refund due; it can't close until the money is given back.
    QVariantMap totals = pos.totals();
    CHECK(totals[u"hasRefundDue"_s].toBool());
    CHECK(totals[u"refundDue"_s] == u"$10.83"_s);
    CHECK_FALSE(totals[u"hasChange"_s].toBool());
    CHECK_FALSE(pos.closeCheck());
    CHECK_FALSE(pos.tender(u"credit"_s));   // a card: the old sale's Refund…

    // Cash gives it back and closes the sale in one touch (owner 2026-11-22).
    REQUIRE(pos.tender(u"cash"_s));
    CHECK_FALSE(pos.hasCheck());

    // Out of the drawer: $100.00 - $10.83.
    const auto closed = std::ranges::find(pos.shared()->closedToday, id, &core::Check::id);
    REQUIRE(closed != pos.shared()->closedToday.end());
    REQUIRE(closed->payments.size() == 1);
    CHECK(closed->payments.front().amount.cents() == -1083);
    const core::Totals done = closed->totals(pos.shared()->settings.tax);
    CHECK(done.cashPaid.cents() == -1083);
    REQUIRE(closed->drawerSession != 0);
    const auto drawer = std::ranges::find(pos.shared()->drawers, closed->drawerSession, &core::DrawerSession::id);
    REQUIRE(drawer != pos.shared()->drawers.end());
    CHECK(core::expectedCash(*drawer, pos.shared()->closedToday, pos.shared()->settings.tax).cents() == 10000 - 1083);

    // Back in stock, the way a sale takes it out.
    CHECK(onHand(pos, "cutter-stock") == stock + 1);

    // On the Exceptions report, with its reason.
    const core::Report r = pos.buildReport(u"exceptions"_s);
    bool section = false, reason = false;
    for (const core::ReportRow &row : r.rows) {
        if (row.cells.empty())
            continue;
        section = section || row.cells.front() == "Item returns (ITEM RETURN, scanned)";
        reason = reason || row.cells.front() == "Defective";
    }
    CHECK(section);
    CHECK(reason);
}

TEST_CASE("Item return: an exchange; never discounted; stock in and out", "[itemreturn]")
{
    PosService pos(returnData(), nullptr);
    openDrawer(pos);
    const double stock = onHand(pos, "cutter-stock");

    REQUIRE(pos.startCheck(core::CheckType::Quick));
    REQUIRE(pos.addItem(u"cutter"_s));   // a new one, $10.00
    scanReturn(pos, QString::fromLatin1(kUpc));
    REQUIRE(pos.addItemReturn(u"cutter"_s, 1000, 1, u"Defective"_s, {}));   // the broken one back
    core::Totals t = current(pos).totals(pos.shared()->settings.tax);
    CHECK(t.total.cents() == 0);
    // A percent off the sale comes off the new one only.
    CHECK(t.discountable.cents() == 1000);

    REQUIRE(pos.tender(u"cash"_s, 0) == false);   // nothing owed
    REQUIRE(pos.closeCheck());
    CHECK(onHand(pos, "cutter-stock") == stock);   // one out, one back
}

TEST_CASE("Item return: a cashier needs a manager's approval", "[itemreturn]")
{
    PosService pos(returnData(), nullptr);
    openDrawer(pos);
    REQUIRE(pos.loginWithPin(u"1111"_s));   // a server: no manager rights

    scanReturn(pos, QString::fromLatin1(kUpc));
    pos.invoke(u"addItemReturn"_s, {u"cutter"_s, qint64(1000), 1, u"Wrong item"_s, QString::fromLatin1(kUpc)});
    REQUIRE(pos.approvalInfo()[u"needed"_s].toBool());
    CHECK_FALSE(pos.hasCheck());
    REQUIRE(pos.approve(u"1234"_s));
    REQUIRE(pos.hasCheck());
    const core::Check &c = current(pos);
    REQUIRE(c.lines.size() == 1);
    CHECK(c.lines.front().isReturn());
    CHECK(std::ranges::any_of(c.events, [](const core::CheckEvent &e) { return e.kind == "approval"; }));
    CHECK(pos.itemReturn().isEmpty());
}

TEST_CASE("Item return: prices including tax", "[itemreturn]")
{
    PosService pos(returnData(true), nullptr);
    openDrawer(pos);
    const QVariantMap item = scanReturn(pos, QString::fromLatin1(kUpc));
    CHECK(item[u"taxIncluded"_s].toBool());
    REQUIRE(pos.addItemReturn(u"cutter"_s, 1000, 1, u"Other"_s, {}));
    const core::Totals t = current(pos).totals(pos.shared()->settings.tax);
    CHECK(t.total.cents() == -1000);   // the price, tax in it
    CHECK(t.tax.cents() == -76);       // 10.00 - 10.00 / 1.0825 = 0.76
}

// Owner 2026-11-22: "refund in retail mode should return to payment screen to pop drawer if cash.
// Cash refund should only be available at terminal with cash drawer. If no cash drawer, cashier
// should be instructed to suspend sale and send to cashier."
namespace {

struct DrawerKicks : app::PosPrinter {
    int kicks = 0;
    void printKitchen(const core::PosSettings &, const core::Check &, const std::vector<core::OrderLine> &, bool) override {}
    void printReceipt(const core::PosSettings &, const core::Check &, const std::string &) override {}
    void printReport(const core::PosSettings &, const core::Report &, const std::string &) override {}
    void openDrawer(const core::PosSettings &, const std::string &) override { ++kicks; }
};

// The register (BHDAN's counter, with the drawer) and a tablet without one.
app::PosData registerAndTablet()
{
    app::PosData d = returnData();
    d.settings.terminals = {{"Register", "receipt", "yes"}, {"Tablet", "receipt", "no"}};
    return d;
}

const QString kNoDrawer = u"No cash drawer here: touch SUSPEND and finish the refund at the register."_s;

} // namespace

TEST_CASE("Item return at the register: to Pay, Cash gives the refund, closes the sale, opens the drawer",
          "[itemreturn][drawers]")
{
    app::PosShared shared(registerAndTablet(), nullptr);
    DrawerKicks printer;
    shared.printer = &printer;
    PosService reg(&shared, u"Register"_s);
    openDrawer(reg);
    const int kicksBefore = printer.kicks;
    QSignalSpy toPay(&reg, &PosService::refundToGive);

    scanReturn(reg, QString::fromLatin1(kUpc));
    REQUIRE(reg.addItemReturn(u"cutter"_s, 1000, 1, u"Defective"_s, QString::fromLatin1(kUpc)));
    const qint64 id = reg.checkInfo()[u"id"_s].toLongLong();
    REQUIRE(toPay.count() == 1);   // the Retail screen goes to Pay
    CHECK(toPay.first()[0].toLongLong() == id);
    const QVariantMap totals = reg.totals();
    CHECK(totals[u"hasRefundDue"_s].toBool());
    CHECK(totals[u"refundDue"_s] == u"$10.83"_s);
    CHECK_FALSE(totals[u"refundNoDrawer"_s].toBool());

    REQUIRE(reg.tender(u"cash"_s));
    CHECK_FALSE(reg.hasCheck());                       // closed
    CHECK(printer.kicks == kicksBefore + 1);           // the drawer opened
    const auto closed = std::ranges::find(shared.closedToday, id, &core::Check::id);
    REQUIRE(closed != shared.closedToday.end());
    CHECK(closed->drawerSession != 0);
    REQUIRE(closed->payments.size() == 1);
    CHECK(closed->payments.front().amount.cents() == -1083);
}

TEST_CASE("Item return: an exchange that still owes money stays on the Retail screen", "[itemreturn]")
{
    PosService pos(returnData(), nullptr);
    openDrawer(pos);
    QSignalSpy toPay(&pos, &PosService::refundToGive);
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    REQUIRE(pos.addItem(u"cutter"_s));
    REQUIRE(pos.addItem(u"cutter"_s));                 // two new ones
    scanReturn(pos, QString::fromLatin1(kUpc));
    REQUIRE(pos.addItemReturn(u"cutter"_s, 1000, 1, u"Defective"_s, {}));
    CHECK(toPay.count() == 0);
    CHECK_FALSE(pos.totals()[u"hasRefundDue"_s].toBool());
}

TEST_CASE("Item return on a tablet: no cash refund there; suspended, the register gives it", "[itemreturn][drawers]")
{
    app::PosShared shared(registerAndTablet(), nullptr);
    DrawerKicks printer;
    shared.printer = &printer;
    PosService tablet(&shared, u"Tablet"_s);
    PosService reg(&shared, u"Register"_s);
    openDrawer(reg);
    const int kicksBefore = printer.kicks;

    REQUIRE(tablet.loginWithPin(u"1234"_s));   // a manager
    scanReturn(tablet, QString::fromLatin1(kUpc));
    REQUIRE(tablet.addItemReturn(u"cutter"_s, 1000, 1, u"Defective"_s, QString::fromLatin1(kUpc)));
    const qint64 id = tablet.checkInfo()[u"id"_s].toLongLong();

    // Pay says what to do instead of taking cash.
    QVariantMap totals = tablet.totals();
    CHECK(totals[u"hasRefundDue"_s].toBool());
    CHECK(totals[u"refundNoDrawer"_s].toBool());
    CHECK(totals[u"refundNoDrawerText"_s] == kNoDrawer);
    QSignalSpy notices(&tablet, &PosService::notice);
    CHECK_FALSE(tablet.tender(u"cash"_s));
    REQUIRE(notices.count() > 0);
    CHECK(notices.last()[0].toString() == kNoDrawer);
    CHECK_FALSE(tablet.closeCheck());
    CHECK(notices.last()[0].toString() == kNoDrawer);
    CHECK(shared.open.at(id).payments.empty());        // nothing given
    CHECK(printer.kicks == kicksBefore);

    // SUSPEND: put away, still open.
    tablet.releaseCheck();
    CHECK_FALSE(tablet.hasCheck());
    REQUIRE(shared.open.contains(id));

    // At the register: recalled, Pay shows the refund, Cash gives it and opens the drawer.
    REQUIRE(reg.openCheck(id));
    totals = reg.totals();
    CHECK(totals[u"hasRefundDue"_s].toBool());
    CHECK_FALSE(totals[u"refundNoDrawer"_s].toBool());
    REQUIRE(reg.tender(u"cash"_s));
    CHECK_FALSE(reg.hasCheck());
    CHECK(printer.kicks == kicksBefore + 1);
    const auto closed = std::ranges::find(shared.closedToday, id, &core::Check::id);
    REQUIRE(closed != shared.closedToday.end());
    REQUIRE(closed->payments.size() == 1);
    CHECK(closed->payments.front().amount.cents() == -1083);
}

TEST_CASE("Find a Check: a cash refund only where there's a drawer; a card anywhere", "[itemreturn][refunds][drawers]")
{
    app::PosShared shared(registerAndTablet(), nullptr);
    DrawerKicks printer;
    shared.printer = &printer;
    PosService reg(&shared, u"Register"_s);
    PosService tablet(&shared, u"Tablet"_s);
    openDrawer(reg);

    const auto sale = [&](const QString &tender) {
        REQUIRE(reg.startCheck(core::CheckType::Quick));
        REQUIRE(reg.addItem(u"cutter"_s));
        const qint64 id = reg.checkInfo()[u"id"_s].toLongLong();
        REQUIRE(reg.tender(tender));
        REQUIRE(reg.closeCheck());
        return id;
    };
    const qint64 cash = sale(u"cash"_s);
    const qint64 card = sale(u"credit"_s);
    const auto paymentOf = [&](qint64 id) {
        const auto c = std::ranges::find(shared.closedToday, id, &core::Check::id);
        REQUIRE(c != shared.closedToday.end());
        return c->payments.front().id;
    };

    REQUIRE(tablet.loginWithPin(u"1234"_s));
    QSignalSpy notices(&tablet, &PosService::notice);
    CHECK_FALSE(tablet.refundPayment(cash, paymentOf(cash), 0, u"Defective"_s));
    REQUIRE(notices.count() > 0);
    CHECK(notices.last()[0].toString() == u"No cash drawer here: give this cash refund at the register."_s);
    CHECK(tablet.refundPayment(card, paymentOf(card), 0, u"Defective"_s));   // recorded: the card machine

    REQUIRE(reg.refundPayment(cash, paymentOf(cash), 0, u"Defective"_s));     // the register gives it
    CHECK(shared.refundsToday.size() == 2);
}
