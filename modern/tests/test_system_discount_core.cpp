// System discounts (core/system_discount.hh): the highest level reached, Or More against full groups, any mix
// against the same item only, $ and % off each, dates, the better of two for a line, No discounts items and
// returns left out, the line's buydown left room, tax before / after in the check's totals, Copy to New for
// several (D26 -> D27), boxes and their sticks, the problems before saving. Made-up items only.

#include <catch2/catch_test_macros.hpp>

#include "core/check.hh"
#include "core/system_discount.hh"

#include <algorithm>

using namespace vt::core;
using vt::Money;

namespace {

Money usd(std::int64_t cents) { return Money::fromCents(cents); }

MenuItem cigar(const std::string &id, const std::string &upc, std::int64_t cents)
{
    MenuItem m;
    m.id = id;
    m.name = id;
    m.price = usd(cents);
    m.taxClass = TaxClass::Merchandise;
    m.barcodes = {upc};
    return m;
}

SystemDiscount dirtyDozen(const std::string &id, int number, std::vector<SystemDiscountItem> items, std::int64_t offCents)
{
    SystemDiscount d;
    d.id = id;
    d.number = number;
    d.name = "D26 " + id;
    d.start = "2026-01-01 00:00";
    d.end = "2027-01-01 00:00";
    d.levels = {{1, true, usd(offCents), 0, "DIRTY DOZEN SAVINGS!"}};
    sysdisc::fillLevels(d);
    d.items = std::move(items);
    return d;
}

struct Fixture {
    std::vector<MenuItem> menu{cigar("robusto", "000000000017", 999), cigar("toro", "000000000024", 1099),
                               cigar("box", "000000000031", 16999)};
    Check check;
    const MenuItem *item(const std::string &id) const
    {
        const auto it = std::ranges::find(menu, id, &MenuItem::id);
        return it == menu.end() ? nullptr : &*it;
    }
    sysdisc::ItemOf itemOf() const
    {
        return [this](const OrderLine &l) { return item(l.itemId); };
    }
    // The new line's id (a reference into the lines wouldn't survive the next add).
    std::int64_t add(const std::string &id, int qty = 1)
    {
        OrderLine &l = check.addItem(*item(id));
        l.quantity = qty;
        return l.id;
    }
};

const std::string kNow = "2026-11-27 19:00";

} // namespace

TEST_CASE("System discount core: a box deal at 1 or more, $ off each", "[sysdisc]")
{
    Fixture f;
    const std::int64_t box = f.add("box", 2);
    const std::vector<SystemDiscount> defs{dirtyDozen("sd-1", 1, {{"000000000031", "", "box"}}, 2500)};
    const auto want = sysdisc::wanted(defs, f.check.lines, f.itemOf(), kNow);
    REQUIRE(want.size() == 1);
    CHECK(want[0].lineId == box);
    CHECK(want[0].amount == usd(5000));   // $25 off each of two
    CHECK(want[0].text == "DIRTY DOZEN SAVINGS!");
    CHECK(sysdisc::reference(want[0]) == "sysdisc:sd-1:" + std::to_string(box));
    // Over on its end (1/1/2027 12:00 AM) and not yet before its start.
    CHECK(sysdisc::wanted(defs, f.check.lines, f.itemOf(), "2027-01-01 00:00").empty());
    CHECK(sysdisc::wanted(defs, f.check.lines, f.itemOf(), "2025-12-31 23:59").empty());
    CHECK(sysdisc::stateAt(defs[0], "2025-12-31 23:59") == sysdisc::State::Coming);
    CHECK(sysdisc::stateAt(defs[0], kNow) == sysdisc::State::Running);
}

TEST_CASE("System discount core: levels, Or More, full groups, any mix or the same item", "[sysdisc]")
{
    SystemDiscount d;
    d.id = "sd-2";
    d.number = 2;
    d.name = "CIGAR MIX";
    d.percent = true;
    d.levels = {{3, false, Money(), 1000, "3 FOR 10%"}, {5, true, Money(), 1500, "5+ 15%"}};
    sysdisc::fillLevels(d);
    d.items = {{"000000000017", "", "robusto"}, {"000000000024", "", "toro"}};
    CHECK(sysdisc::levelFor(d, 2) == -1);
    CHECK(sysdisc::levelFor(d, 3) == 0);
    CHECK(sysdisc::levelFor(d, 4) == 0);
    CHECK(sysdisc::levelFor(d, 7) == 1);
    CHECK(sysdisc::unitsCovered(d.levels[0], 4) == 3);   // one full group of 3
    CHECK(sysdisc::unitsCovered(d.levels[0], 6) == 6);
    CHECK(sysdisc::unitsCovered(d.levels[1], 7) == 7);   // Or More: all of them

    {   // Any mix: 2 robusto + 2 toro = 4: level 1, the first 3 units rung get 10%.
        Fixture f;
        const std::int64_t a = f.add("robusto", 2);
        const std::int64_t b = f.add("toro", 2);
        const auto want = sysdisc::wanted({d}, f.check.lines, f.itemOf(), kNow);
        REQUIRE(want.size() == 2);
        CHECK(want[0].lineId == a);
        CHECK(want[0].amount == usd(200));   // 2 x $0.999 -> $1.00 each
        CHECK(want[1].lineId == b);
        CHECK(want[1].amount == usd(110));   // 1 of 2 toros: $1.10
        CHECK(want[0].text == "3 FOR 10%");
    }
    {   // 5 or more: every unit at 15%.
        Fixture f;
        f.add("robusto", 3);
        f.add("toro", 2);
        const auto want = sysdisc::wanted({d}, f.check.lines, f.itemOf(), kNow);
        REQUIRE(want.size() == 2);
        CHECK(want[0].amount == usd(450));   // 3 x $1.50
        CHECK(want[1].amount == usd(330));   // 2 x $1.65
        CHECK(want[1].text == "5+ 15%");
    }
    {   // Same item only: 2 + 2 reach nothing.
        SystemDiscount same = d;
        same.sameItemOnly = true;
        Fixture f;
        f.add("robusto", 2);
        f.add("toro", 2);
        CHECK(sysdisc::wanted({same}, f.check.lines, f.itemOf(), kNow).empty());
        f.add("toro", 1);   // 3 toros on two lines: they count together
        const auto want = sysdisc::wanted({same}, f.check.lines, f.itemOf(), kNow);
        REQUIRE(want.size() == 2);
        CHECK(want[0].amount + want[1].amount == usd(330));
    }
}

TEST_CASE("System discount core: what never counts, the better of two, room left by a buydown", "[sysdisc]")
{
    Fixture f;
    f.menu[0].noDiscount = true;   // like cigarettes: No discounts
    const std::vector<SystemDiscount> defs{dirtyDozen("sd-1", 1, {{"000000000017", "", ""}, {"000000000024", "", ""}}, 300),
                                           dirtyDozen("sd-2", 2, {{"000000000024", "", ""}}, 500)};
    f.add("robusto");
    const std::int64_t toroId = f.add("toro");
    f.add("toro", -1);   // a return
    OrderLine &toro = *f.check.line(toroId);
    auto want = sysdisc::wanted(defs, f.check.lines, f.itemOf(), kNow);
    REQUIRE(want.size() == 1);
    CHECK(want[0].lineId == toroId);
    CHECK(want[0].defId == "sd-2");   // $5 beats $3
    CHECK(want[0].amount == usd(500));
    // A buydown already on the line: never more than what's left.
    toro.buydownId = "bd-1";
    toro.buydownEach = usd(800);
    want = sysdisc::wanted(defs, f.check.lines, f.itemOf(), kNow);
    REQUIRE(want.size() == 1);
    CHECK(want[0].amount == usd(299));
    toro.voided = true;
    CHECK(sysdisc::wanted(defs, f.check.lines, f.itemOf(), kNow).empty());
}

TEST_CASE("System discount core: tax on the reduced price, or before the discount", "[sysdisc]")
{
    for (const bool before : {false, true}) {
        Fixture f;
        TaxRates rates;
        rates.merchandisePpm = 63500;   // 6.35%
        const std::int64_t box = f.add("box");
        Payment p;
        p.id = 1;
        p.tenderId = sysdisc::kTender;
        p.kind = TenderKind::Discount;
        p.amount = usd(2500);
        p.lineId = box;
        p.taxBefore = before;
        f.check.payments.push_back(p);
        const Totals t = f.check.totals(rates);
        CHECK(t.discounts == usd(2500));
        CHECK(t.subtotal == usd(14499));
        CHECK(t.tax == (before ? usd(1079) : usd(921)));   // 6.35% of $169.99, or of $144.99
    }
}

TEST_CASE("System discount core: Copy to New for several, names and problems", "[sysdisc]")
{
    std::vector<SystemDiscount> defs{dirtyDozen("sd-1", 1, {{"1", "", ""}}, 2500), dirtyDozen("sd-2", 2, {{"2", "", ""}}, 1200)};
    defs[1].name = "MY OWN DEAL";
    defs[0].ilsaId = "601047425";
    const auto made = sysdisc::copies(defs, {"sd-1", "sd-2"}, "D26", "D27", "2027-01-01 00:00", "2028-01-01 00:00");
    REQUIRE(made.size() == 2);
    CHECK(made[0].id == "sd-3");
    CHECK(made[0].name == "D27 sd-1");
    CHECK(made[0].start == "2027-01-01 00:00");
    CHECK(made[0].end == "2028-01-01 00:00");
    CHECK(made[0].items == defs[0].items);
    CHECK(made[0].levels == defs[0].levels);
    CHECK(made[0].ilsaId.empty());   // a copy is ours, not ILSA's
    CHECK(made[1].id == "sd-4");
    CHECK(made[1].name == "MY OWN DEAL (copy)");
    // Dates left empty stay.
    CHECK(sysdisc::copies(defs, {"sd-1"}, "D26", "D27", "", "")[0].end == "2027-01-01 00:00");
    // Never longer than 30 characters.
    defs[0].name = "D26 ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    CHECK(sysdisc::copies(defs, {"sd-1"}, "D26", "D2027", "", "")[0].name.size() == 30);

    SystemDiscount bad;
    sysdisc::fillLevels(bad);
    auto problems = sysdisc::problems(bad);
    CHECK(std::ranges::find(problems, std::string("Give it a name.")) != problems.end());
    CHECK(std::ranges::find(problems, std::string("Fill in level 1: its quantity and amount.")) != problems.end());
    CHECK(std::ranges::find(problems, std::string("Add the items it's for.")) != problems.end());
    SystemDiscount good = dirtyDozen("sd-9", 9, {{"1", "", ""}}, 2500);
    CHECK(sysdisc::problems(good).empty());
    good.levels[1] = {1, true, usd(3000), 0, ""};   // level 2 no larger than level 1
    CHECK(sysdisc::problems(good) == std::vector<std::string>{"Each level needs a larger quantity than the one before it."});
}

TEST_CASE("System discount core: a box and its sticks (Add Purchase/Subunits)", "[sysdisc]")
{
    std::vector<MenuItem> menu{cigar("box", "000000000031", 16999), cigar("stick", "000000000048", 999),
                               cigar("other", "000000000055", 500)};
    menu[1].recipe = {{"__parent__:000000000031", 20}};
    auto names = [](const std::vector<const MenuItem *> &v) {
        std::vector<std::string> out;
        for (const MenuItem *m : v)
            out.push_back(m->id);
        return out;
    };
    CHECK(names(sysdisc::linkedItems(menu, {"box"})) == std::vector<std::string>{"stick"});
    CHECK(names(sysdisc::linkedItems(menu, {"stick"})) == std::vector<std::string>{"box"});
    CHECK(sysdisc::linkedItems(menu, {"other"}).empty());
}
