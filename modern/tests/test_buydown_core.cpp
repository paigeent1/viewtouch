// Manufacturer buydowns and promotions (core/buydown.hh): the amounts by Part/Ctn, the dates and the four
// styles, the larger of two, the line through quantity, void and return, totals and tax both ways, Copy to
// New, overlaps, the old per-item tags, the reports. Made-up items only.

#include <catch2/catch_test_macros.hpp>

#include "core/buydown.hh"
#include "core/report.hh"

using namespace vt::core;
using vt::Money;

namespace {

Money usd(std::int64_t cents) { return Money::fromCents(cents); }

MenuItem carton()
{
    MenuItem m;
    m.id = "sonoma-ctn";
    m.name = "SONOMA FF BOX CTN";
    m.price = usd(11479);
    m.taxClass = TaxClass::Merchandise;
    m.barcodes = {"012345000016"};
    m.manufacturer = "SONOMA";
    return m;
}

MenuItem pack()
{
    MenuItem m;
    m.id = "sonoma-pk";
    m.name = "SONOMA FF BOX";
    m.price = usd(1148);
    m.taxClass = TaxClass::Merchandise;
    m.barcodes = {"012345000023"};
    m.recipe.push_back({"__parent__:012345000016", 10});   // a pack of the carton
    m.manufacturer = "SONOMA";
    return m;
}

Buydown sonoma()
{
    Buydown b;
    b.id = "bd-1";
    b.number = 1;
    b.name = "SONOMA 2026Q4";
    b.receiptText = "SONOMA SAVINGS!";
    b.amount = usd(410);
    b.start = "2026-10-01 00:00";
    b.end = "2026-12-31 23:00";
    b.manufacturer = "SONOMA";
    b.items = {{"012345000016", "sonoma-ctn", buydown::partCtnFor(carton()), "SONOMA FF BOX CTN"},
               {"012345000023", "sonoma-pk", buydown::partCtnFor(pack()), "SONOMA FF BOX"}};
    return b;
}

const std::string kNow = "2026-11-25 10:00";

OrderLine &ring(Check &c, const MenuItem &m, const std::vector<Buydown> &defs, const std::string &now = kNow,
                bool taxBefore = false)
{
    OrderLine &l = c.addItem(m);
    if (const auto h = buydown::hitFor(defs, m, now))
        buydown::apply(l, *h, taxBefore);
    return l;
}

TaxRates ct()
{
    TaxRates r;
    r.merchandisePpm = 63500;   // 6.35%
    return r;
}

} // namespace

TEST_CASE("Buydowns: a pack gets a tenth, a carton the whole amount", "[buydowns]")
{
    CHECK(buydown::partCtnFor(pack()) == 0.1);
    CHECK(buydown::partCtnFor(carton()) == 0.0);
    const std::vector<Buydown> defs{sonoma()};
    Check c;
    ring(c, pack(), defs);
    ring(c, carton(), defs);
    const OrderLine &p = c.lines[0], &k = c.lines[1];
    CHECK(p.buydownEach == usd(41));
    CHECK(k.buydownEach == usd(410));
    CHECK(p.buydownText == "SONOMA SAVINGS!");
    CHECK(p.total() == usd(1148));   // the item at its regular price
    // Part/Ctn 1 (ILSA's carton) is the whole amount too; Apply Full Discount Amount: every UPC gets it.
    Buydown b = sonoma();
    b.items[1].partCtn = 1.0;
    CHECK(buydown::amountFor(b, b.items[1]) == usd(410));
    b = sonoma();
    b.usePartCtn = false;
    CHECK(buydown::amountFor(b, b.items[1]) == usd(410));
}

TEST_CASE("Buydowns: totals and tax, on the reduced price or before it", "[buydowns]")
{
    const std::vector<Buydown> defs{sonoma()};
    for (const bool before : {false, true}) {
        Check c;
        ring(c, pack(), defs, kNow, before);
        ring(c, carton(), defs, kNow, before);
        const Totals t = c.totals(ct());
        CHECK(t.items == usd(12627));
        CHECK(t.buydowns == usd(451));
        CHECK(t.discounts == Money());
        CHECK(t.subtotal == usd(12176));
        // After: 6.35% of 121.76 = 7.73; before: of 126.27 = 8.02.
        CHECK(t.tax == (before ? usd(802) : usd(773)));
        CHECK(t.total == t.subtotal + t.tax);
        CHECK(t.itemsExTax - t.discountsExTax - t.buydownsExTax == t.subtotal);
    }
    // A promotion has its own box; a buydown follows the store.
    Buydown promo = sonoma();
    promo.type = "promo";
    promo.taxBefore = true;
    CHECK(buydown::taxBeforeFor(promo, false));
    CHECK_FALSE(buydown::taxBeforeFor(sonoma(), false));
    CHECK(buydown::taxBeforeFor(sonoma(), true));
}

TEST_CASE("Buydowns: not before the start, not after the end", "[buydowns]")
{
    const std::vector<Buydown> defs{sonoma()};
    CHECK_FALSE(buydown::hitFor(defs, pack(), "2026-09-30 23:59"));
    CHECK(buydown::hitFor(defs, pack(), "2026-10-01 00:00"));
    CHECK(buydown::hitFor(defs, pack(), "2026-12-31 23:00"));
    CHECK_FALSE(buydown::hitFor(defs, pack(), "2026-12-31 23:01"));
    CHECK(buydown::stateAt(defs[0], "2026-09-01 00:00") == buydown::State::Coming);
    CHECK(buydown::stateAt(defs[0], kNow) == buydown::State::Running);
    CHECK(buydown::stateAt(defs[0], "2027-01-01 00:00") == buydown::State::Ended);
    // An item outside it gets nothing.
    MenuItem other = pack();
    other.id = "x";
    other.barcodes = {"099999000019"};
    CHECK_FALSE(buydown::hitFor(defs, other, kNow));
    // Found by UPC (another store's id), with or without its leading zero.
    Buydown byUpc = sonoma();
    byUpc.items = {{"12345000023", "", 0.1, "SONOMA FF BOX"}};
    CHECK(buydown::hitFor({byUpc}, pack(), kNow)->each == usd(41));
}

TEST_CASE("Buydowns: the quantity styles cap the units", "[buydowns]")
{
    Buydown b = sonoma();
    b.style = 3;   // Quantity with Date Range
    b.qty = 3;
    b.used = 2;
    CHECK(buydown::hitFor({b}, pack(), kNow));
    // One more on an open check uses the last.
    CHECK_FALSE(buydown::hitFor({b}, pack(), kNow, {}, {{"bd-1", 1}}));
    b.used = 3;
    CHECK_FALSE(buydown::hitFor({b}, pack(), kNow));
    CHECK(buydown::stateAt(b, kNow) == buydown::State::Ended);
    // Quantity Only: no end date.
    b.style = 2;
    b.used = 0;
    CHECK(buydown::hitFor({b}, pack(), "2030-01-01 00:00"));
    CHECK_FALSE(buydown::hitFor({b}, pack(), "2026-09-01 00:00"));   // still not before its start
    // Old Style: the end or the quantity, whichever comes first.
    b.style = 4;
    CHECK_FALSE(buydown::hitFor({b}, pack(), "2030-01-01 00:00"));
    CHECK(buydown::hitFor({b}, pack(), kNow));
    b.end.clear();
    CHECK_FALSE(buydown::problems(b).empty());   // Old Style needs an end
    // Date Range Only ignores Qty.
    b = sonoma();
    b.qty = 1;
    b.used = 50;
    CHECK(buydown::hitFor({b}, pack(), kNow));
    // Units on checks, by definition.
    Check c;
    ring(c, pack(), {sonoma()}).quantity = 4;
    ring(c, carton(), {sonoma()});
    CHECK(buydown::unitsOn({&c}).at("bd-1") == 5);
}

TEST_CASE("Buydowns: two running on one item: the larger, never both", "[buydowns]")
{
    Buydown small = sonoma(), big = sonoma();
    big.id = "bd-2";
    big.number = 2;
    big.name = "SONOMA EXTRA";
    big.amount = usd(500);
    const auto h = buydown::hitFor({small, big}, pack(), kNow);
    REQUIRE(h);
    CHECK(h->def->id == "bd-2");
    CHECK(h->each == usd(50));
    Check c;
    ring(c, pack(), {small, big});
    CHECK(c.totals(ct()).buydowns == usd(50));
    // The overlap is told.
    const auto o = buydown::overlaps({small, big}, small);
    REQUIRE(o.size() == 2);   // each of its items
    CHECK(o[0].other == "SONOMA EXTRA");
    big.start = "2027-01-01 00:00";
    big.end = "2027-03-31 23:00";
    CHECK(buydown::overlaps({small, big}, small).empty());
}

TEST_CASE("Buydowns: the line follows quantity, void, split and return", "[buydowns]")
{
    const std::vector<Buydown> defs{sonoma()};
    Check c;
    OrderLine &l = ring(c, pack(), defs);
    l.quantity = 3;
    CHECK(c.totals(ct()).buydowns == usd(123));
    const std::int64_t id = l.id;
    // Split: one taken off as its own line keeps its buydown.
    auto one = c.takeOne(id);
    REQUIRE(one);
    CHECK(one->buydownTotal() == usd(41));
    CHECK(c.totals(ct()).buydowns == usd(82));
    // Void (sent) or error correct: gone with the item.
    c.line(id)->sent = true;
    REQUIRE(c.voidLine(id));
    CHECK(c.totals(ct()).buydowns == Money());
    // A return gives back what was paid: the price less the buydown.
    Check back;
    OrderLine &r = ring(back, pack(), defs);
    r.quantity = -1;
    const Totals t = back.totals(ct());
    CHECK(t.buydowns == usd(-41));
    CHECK(t.subtotal == usd(-1107));
    CHECK(t.total == usd(-1107) + t.tax);
    CHECK(t.tax == usd(-70));
}

TEST_CASE("Buydowns: Copy to New keeps the items; its dates and amount are then changed", "[buydowns]")
{
    const std::vector<Buydown> defs{sonoma()};
    Buydown c = buydown::copyOf(defs[0], defs);
    CHECK(c.number == 2);
    CHECK(c.id == "bd-2");
    CHECK(c.items == defs[0].items);
    CHECK(c.name == "SONOMA 2026Q4 (copy)");
    c.name = "SONOMA 2027Q1";
    c.start = "2027-01-01 00:00";
    c.end = "2027-03-31 23:00";
    c.amount = usd(500);
    CHECK(buydown::problems(c).empty());
    const std::vector<Buydown> both{defs[0], c};
    CHECK(buydown::hitFor(both, pack(), kNow)->def->id == "bd-1");
    CHECK(buydown::hitFor(both, pack(), "2027-02-01 12:00")->each == usd(50));
    CHECK(buydown::overlaps(both, c).empty());
}

TEST_CASE("Buydowns: the old per-item tags become definitions", "[buydowns]")
{
    std::vector<MenuItem> menu{carton(), pack()};
    menu[0].recipe.push_back({"__buydown__", 4.10});
    menu[0].recipe.push_back({"__buydown_grp__:SONOMA", 1.0});
    menu[0].recipe.push_back({"__buydown_start__", 20261001});
    menu[0].recipe.push_back({"__buydown_end__", 20261231});
    menu[1].recipe.push_back({"__buydown__", 0.41});
    menu[1].recipe.push_back({"__buydown_grp__:SONOMA", 1.0});
    menu[1].recipe.push_back({"__buydown_start__", 20261001});
    menu[1].recipe.push_back({"__buydown_end__", 20261231});
    std::vector<Buydown> defs;
    CHECK(buydown::migrateTags(menu, defs) == 1);
    REQUIRE(defs.size() == 1);
    CHECK(defs[0].name == "SONOMA");
    CHECK(defs[0].amount == usd(410));
    CHECK(defs[0].start == "2026-10-01 00:00");
    CHECK(defs[0].end == "2026-12-31 23:59");
    REQUIRE(defs[0].items.size() == 2);
    CHECK(defs[0].items[1].partCtn == 0.1);
    for (const MenuItem &m : menu)
        for (const RecipeLine &rl : m.recipe)
            CHECK_FALSE(rl.ingredientId.starts_with("__buydown"));
    CHECK(buydown::hitFor(defs, menu[1], kNow)->each == usd(41));
    CHECK(buydown::migrateTags(menu, defs) == 0);   // once
}

TEST_CASE("Buydowns: the report adds up units, cartons and amounts", "[buydowns][reports]")
{
    PosSettings settings;
    settings.storeName = "Test Tobacco";
    Buydown promo = sonoma();
    promo.id = "bd-2";
    promo.number = 2;
    promo.type = "promo";
    promo.name = "STORE PROMO";
    promo.manufacturer.clear();
    promo.items = {{"099999000019", "lighter", 0.0, "LIGHTER"}};
    promo.amount = usd(50);
    settings.buydowns = {sonoma(), promo};
    MenuItem lighter;
    lighter.id = "lighter";
    lighter.name = "LIGHTER";
    lighter.price = usd(199);
    lighter.barcodes = {"099999000019"};
    const std::vector<MenuItem> menu{carton(), pack(), lighter};
    Check c;
    c.status = CheckStatus::Closed;
    ring(c, pack(), settings.buydowns).quantity = 3;
    ring(c, carton(), settings.buydowns);
    ring(c, lighter, settings.buydowns);
    Check v = c;
    v.id = 2;
    v.lines[0].voided = true;   // a voided pack: nothing
    ReportContext ctx{settings, "Test", [](std::int64_t) { return std::string("9:00 PM"); }};
    const Report r = buydownsReport({c, v}, settings.buydowns, menu, ctx);
    const auto all = r.find("All buydowns and promotions");
    REQUIRE(all.size() == 6);
    CHECK(all[1] == "7");
    CHECK(all[4] == "4.3");   // 0.3 + 2 cartons + 2 lighters
    CHECK(all[5] == ctx.money(usd(123 + 410 + 50 + 410 + 50)));
    const auto one = r.find("Total SONOMA 2026Q4");
    REQUIRE(one.size() == 6);
    CHECK(one[1] == "5");    // 3 packs + carton + carton (the second check)
    CHECK(one[2] == "3");
    CHECK(one[3] == "2");
    CHECK(one[5] == ctx.money(usd(943)));
    // One buydown by name: only it.
    const Report only = buydownsReport({c}, settings.buydowns, menu, ctx, "sonoma");
    CHECK_FALSE(only.find("Total SONOMA 2026Q4").empty());
    CHECK(only.find("Total STORE PROMO").empty());
    CHECK(only.find("Summary")[5] == ctx.money(usd(533)));
    // The definition report: one block, its items with price, buydown and sale price.
    const Buydown &s = settings.buydowns[0];
    const Report d = buydownDefinitionReport({&s}, menu, ctx);
    const auto row = d.find("012345000023");
    REQUIRE(row.size() == 6);
    CHECK(row[2] == "0.1");
    CHECK(row[4] == ctx.money(usd(41)));
    CHECK(row[5] == ctx.money(usd(1107)));
    CHECK(d.find("012345000016")[2] == "1");
    CHECK(d.find("012345000016")[5] == ctx.money(usd(11069)));
}
