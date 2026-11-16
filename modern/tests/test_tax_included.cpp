#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "core/report.hh"
#include "pos_fixture.hh"
#include "print/document.hh"
#include "print/tickets.hh"
#include "qt_catch.hh"

using namespace Qt::StringLiterals;
using namespace vt;
using namespace vt::core;
using vt::app::PosService;

// Prices include tax (Store Settings -> Taxes): a bar's menu prices are what the guest pays; the tax
// is backed out of each tax class's part of the check total, never line by line.

namespace {

Money usd(std::int64_t cents) { return Money::fromCents(cents); }

MenuItem drink(const char *id, std::int64_t cents, TaxClass tax = TaxClass::Alcohol)
{
    MenuItem m;
    m.id = id;
    m.name = id;
    m.price = usd(cents);
    m.taxClass = tax;
    return m;
}

TaxRates ct(bool included = true)
{
    TaxRates r;
    r.foodPpm = 73500;      // CT meals tax 7.35%
    r.alcoholPpm = 73500;
    r.pricesIncludeTax = included;
    return r;
}

std::string cell(const Report &r, const std::string &label, std::size_t col = 1)
{
    const std::vector<std::string> row = r.find(label);
    return row.size() > col ? row[col] : std::string();
}

} // namespace

TEST_CASE("Prices include tax: 2 x $5 at 7.35% is exactly $10.00 (9.32 + 0.68)", "[check][tax][taxincluded]")
{
    Check c;
    c.addItem(drink("Bud", 500));
    c.addItem(drink("Bud Light", 500));
    const Totals t = c.totals(ct());
    CHECK(t.taxIncluded);
    CHECK(t.items == usd(1000));
    CHECK(t.total == usd(1000));
    CHECK(t.subtotal == usd(932));
    CHECK(t.tax == usd(68));
    CHECK(t.taxByClass.at(TaxClass::Alcohol) == usd(68));
    CHECK(t.itemsExTax == usd(932));
    CHECK(t.discountsExTax == usd(0));
    CHECK(t.balance == usd(1000));
    // Each line's sales without tax: together exactly the subtotal (not 4.66 + 4.66 = 9.32 by luck:
    // each takes its step of the running total).
    CHECK(c.lineSales(c.lines[0], ct()) + c.lineSales(c.lines[1], ct()) == usd(932));
}

TEST_CASE("Prices include tax: a comp or discount comes off the total before the tax is backed out",
          "[check][tax][taxincluded]")
{
    Check c;
    const std::int64_t first = c.addItem(drink("Bud", 500)).id;
    c.addItem(drink("Bud Light", 500));
    Payment &comp = c.addPayment({"comp", "Comp", TenderKind::Discount, 10000}, {});
    comp.lineId = first;   // one beer comped
    Totals t = c.totals(ct());
    CHECK(t.discounts == usd(500));
    CHECK(t.total == usd(500));
    CHECK(t.subtotal == usd(466));   // 5.00 / 1.0735 = 4.6576...
    CHECK(t.tax == usd(34));
    CHECK(t.itemsExTax - t.discountsExTax == t.subtotal);

    // $2 off the check instead: 8.00 is what's paid, the tax is in that.
    REQUIRE(c.removePayment(comp.id));
    c.addPayment({"two", "$2 off", TenderKind::Discount, 0}, usd(200));
    t = c.totals(ct());
    CHECK(t.total == usd(800));
    CHECK(t.subtotal == usd(745));   // 8.00 / 1.0735 = 7.4522...
    CHECK(t.tax == usd(55));
}

TEST_CASE("Prices include tax: each tax class backs out its own part; untaxed lines carry none",
          "[check][tax][taxincluded]")
{
    TaxRates r = ct();
    r.foodPpm = 63500;   // a different rate for food, to tell them apart
    Check c;
    c.addItem(drink("Bud", 500)).quantity = 2;               // 10.00 alcohol
    c.addItem(drink("Wings", 1200, TaxClass::Food));         // 12.00 food
    c.addItem(drink("Cigar cutter", 2500, TaxClass::None));  // 25.00 not taxed
    const Totals t = c.totals(r);
    CHECK(t.total == usd(4700));
    CHECK(t.taxByClass.at(TaxClass::Alcohol) == usd(68));    // 10.00 - round(10.00 / 1.0735)
    CHECK(t.taxByClass.at(TaxClass::Food) == usd(72));       // 12.00 - round(12.00 / 1.0635)
    CHECK(t.tax == usd(140));
    CHECK(t.subtotal == usd(4560));
    CHECK(t.subtotal + t.tax == t.total);
    Money lines;
    for (const OrderLine &l : c.lines)
        lines += c.lineSales(l, r);
    CHECK(lines == t.itemsExTax);
    CHECK(c.lineSales(c.lines[2], r) == usd(2500));

    // Any mix, any discount: the cents always come to exactly what the guest pays.
    for (int i = 1; i <= 60; ++i) {
        Check m;
        for (int k = 0; k < 1 + i % 5; ++k) {
            const TaxClass cls = k % 3 == 0 ? TaxClass::Alcohol : k % 3 == 1 ? TaxClass::Food : TaxClass::None;
            m.addItem(drink("x", 125 + 37 * i + 211 * k, cls));
        }
        if (i % 2)
            m.addPayment({"d", "10%", TenderKind::Discount, 1000}, {});
        if (i % 3 == 0)
            m.addPayment({"f", "$1", TenderKind::Discount, 0}, usd(100));
        const Totals mt = m.totals(r);
        INFO("case " << i);
        CHECK(mt.total == mt.items - mt.discounts);
        CHECK(mt.subtotal + mt.tax == mt.total);
        CHECK(mt.itemsExTax - mt.discountsExTax == mt.subtotal);
        Money sum;
        for (const OrderLine &l : m.lines)
            sum += m.lineSales(l, r);
        CHECK(sum == mt.itemsExTax);
    }
}

TEST_CASE("Prices include tax off (retail, BHDAN): nothing changes", "[check][tax][taxincluded]")
{
    Check c;
    c.addItem(drink("Bud", 500));
    c.addItem(drink("Bud Light", 500));
    c.addPayment({"d", "10%", TenderKind::Discount, 1000}, {});
    const Totals t = c.totals(ct(false));
    CHECK_FALSE(t.taxIncluded);
    CHECK(t.subtotal == usd(900));
    CHECK(t.tax == usd(66));   // 9.00 x 7.35% = 0.6615
    CHECK(t.total == usd(966));
    CHECK(t.itemsExTax == t.items);
    CHECK(t.discountsExTax == t.discounts);
    CHECK(c.lineSales(c.lines[0], ct(false)) == usd(500));

    // A check closed with prices including tax keeps them after the store turns it off.
    c.closedRates = ct(true);
    CHECK(c.totals(ct(false)).total == usd(900));
}

TEST_CASE("Prices include tax: reports count sales without tax", "[reports][tax][taxincluded]")
{
    PosSettings settings;
    settings.tax = ct();
    const ReportContext ctx{settings, "Today", [](std::int64_t) { return std::string("9:00 PM"); }};
    Check a;
    a.id = 1;
    a.status = CheckStatus::Closed;
    a.addItem(drink("Bud", 500));
    a.addItem(drink("Bud", 500));
    Check b;
    b.id = 2;
    b.status = CheckStatus::Closed;
    const std::int64_t comped = b.addItem(drink("Bud", 500)).id;
    b.addItem(drink("Wings", 1200, TaxClass::Food));
    b.addPayment({"comp", "Comp", TenderKind::Discount, 10000}, {}).lineId = comped;
    const Tender card{"card", "Card", TenderKind::Card, 0};
    a.addPayment(card, usd(1000));
    b.addPayment(card, usd(1200));
    const std::vector<Check> closed{a, b};

    // a: 10.00 = 9.32 + 0.68; b: 12.00 paid = 11.18 + 0.82 (the comped beer's 4.66 a discount).
    const Report sales = salesSummary(closed, ctx, nullptr);
    CHECK(cell(sales, "Item sales") == "$25.16");          // 9.32 + 4.66 + 11.18
    CHECK(cell(sales, "Discounts & comps") == "-$4.66");
    CHECK(cell(sales, "Net sales") == "$20.50");            // 9.32 + 11.18
    CHECK(cell(sales, "Alcohol tax") == "$0.68");
    CHECK(cell(sales, "Food tax") == "$0.82");
    CHECK(cell(sales, "Total with tax") == "$22.00");        // what the guests paid

    const Report items = itemSales(closed, {}, ctx);
    CHECK(cell(items, "Bud", 2) == "$13.98");                // 9.32 + 4.66: no tax in it
    CHECK(cell(items, "Wings", 2) == "$11.18");              // 12.00 / 1.0735

    // The books balance: sales and tax credited apart, discounts without their tax.
    const Report books = accountingReport(closed, {}, ctx);
    const std::vector<std::string> total = books.find("Total");
    REQUIRE(total.size() == 3);
    CHECK(total[1] == "$26.66");   // cards 22.00 + discounts 4.66
    CHECK(total[1] == total[2]);
}

TEST_CASE("Prices include tax: the receipt shows Subtotal, the tax with its rate, and Total",
          "[print][tax][taxincluded]")
{
    PosSettings settings;
    settings.tax = ct();
    Check c;
    c.id = 7;
    c.label = "Bar 3";
    c.addItem(drink("Bud", 500)).quantity = 2;
    const print::TicketContext ctx{settings, [](std::int64_t) { return std::string("Nov 16"); },
                                   [](std::int64_t) { return std::string("9:00 PM"); }, 0};
    const std::string r = print::renderText(print::receipt(c, ctx), 42);
    INFO(r);
    CHECK(r.find("Subtotal") != std::string::npos);
    CHECK(r.find("9.32") != std::string::npos);
    CHECK(r.find("Alcohol tax 7.35%") != std::string::npos);
    CHECK(r.find("0.68") != std::string::npos);
    CHECK(r.find("10.00") != std::string::npos);
    CHECK(r.find("10.01") == std::string::npos);
    CHECK(r.find("Prices include tax.") != std::string::npos);

    settings.tax.pricesIncludeTax = false;   // retail: as it always was
    const std::string off = print::renderText(print::receipt(c, ctx), 42);
    CHECK(off.find("Prices include tax.") == std::string::npos);
    CHECK(off.find("10.74") != std::string::npos);   // 10.00 + 0.735 -> 0.74
}

TEST_CASE("Prices include tax: set in Store Settings -> Taxes, kept, and shown on every screen's totals",
          "[m4][admin][tax][taxincluded]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.addItem(u"house-salad"_s);   // 8.50 food at 8.25%
    CHECK(pos.totals()[u"total"_s].toString() == u"$9.20"_s);
    CHECK_FALSE(pos.totals()[u"taxIncluded"_s].toBool());

    QVariantMap tax = pos.adminRecords(u"taxes"_s)[0].toMap();
    CHECK(tax.contains(u"pricesIncludeTax"_s));
    CHECK_FALSE(tax[u"pricesIncludeTax"_s].toBool());
    tax[u"pricesIncludeTax"_s] = true;
    REQUIRE(pos.adminSave(u"taxes"_s, 0, tax));
    CHECK(pos.settings().tax.pricesIncludeTax);

    const QVariantMap t = pos.totals();   // what tablets (RemoteSession) and the guest screen get too
    CHECK(t[u"taxIncluded"_s].toBool());
    CHECK(t[u"total"_s].toString() == u"$8.50"_s);
    CHECK(t[u"subtotal"_s].toString() == u"$7.85"_s);   // 8.50 / 1.0825 = 7.852
    CHECK(t[u"tax"_s].toString() == u"$0.65"_s);
    const QVariantList lines = t[u"taxLines"_s].toList();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].toMap()[u"name"_s].toString() == u"Food tax 8.25%"_s);

    // Saved with the settings, and with each check it closes on.
    const core::PosSettings back = app::settingsFromJson(app::toJson(pos.settings()));
    CHECK(back.tax.pricesIncludeTax);
    core::Check closed;
    closed.closedRates = pos.settings().tax;
    const std::optional<core::Check> again = app::checkFromJson(app::toJson(closed));
    REQUIRE(again);
    REQUIRE(again->closedRates);
    CHECK(again->closedRates->pricesIncludeTax);
}
