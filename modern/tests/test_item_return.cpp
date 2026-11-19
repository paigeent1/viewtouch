#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "core/report.hh"
#include "core/scan_data.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

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

    REQUIRE(pos.tender(u"cash"_s));
    totals = pos.totals();
    CHECK_FALSE(totals[u"hasRefundDue"_s].toBool());
    CHECK(totals[u"hasRefunded"_s].toBool());
    CHECK(totals[u"refunded"_s] == u"$10.83"_s);
    REQUIRE(current(pos).payments.size() == 1);
    CHECK(current(pos).payments.front().amount.cents() == -1083);
    REQUIRE(pos.closeCheck());

    // Out of the drawer: $100.00 - $10.83.
    const auto closed = std::ranges::find(pos.shared()->closedToday, id, &core::Check::id);
    REQUIRE(closed != pos.shared()->closedToday.end());
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
