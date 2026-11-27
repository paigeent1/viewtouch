#include <catch2/catch_test_macros.hpp>

#include "core/mass_update.hh"

#include <cmath>
#include <string>
#include <vector>

using namespace vt::core::mass;
using vt::core::BuilderIndex;

// Mass Updates' own arithmetic and narrowing (core/mass_update.hh): no Qt.

TEST_CASE("Mass updates: every Round to, table by table", "[massupdate][core]")
{
    struct Case {
        std::int64_t in;
        Round round;
        std::int64_t out;
    };
    const Case cases[] = {
        {1231, Round::None, 1231},
        {1230, Round::Up5, 1235}, {1235, Round::Up5, 1235}, {1236, Round::Up5, 1245}, {1239, Round::Up5, 1245},
        {1230, Round::Up9, 1239}, {1239, Round::Up9, 1239}, {1201, Round::Up9, 1209}, {0, Round::Up9, 9},
        {1233, Round::Near9, 1229}, {1234, Round::Near9, 1239}, {1230, Round::Near9, 1229}, {1235, Round::Near9, 1239},
        {1239, Round::Near9, 1239}, {1240, Round::Near9, 1239}, {4, Round::Near9, 9},
        {1231, Round::Up5or9, 1235}, {1235, Round::Up5or9, 1235}, {1236, Round::Up5or9, 1239}, {1239, Round::Up5or9, 1239},
        {1230, Round::Up5or9, 1235},
        // .49 / .99 up (the bar's markup on the retail store's prices, owner 2026-11-26)
        {1012, Round::Up49or99, 1049}, {1055, Round::Up49or99, 1099}, {1049, Round::Up49or99, 1049},
        {1050, Round::Up49or99, 1099}, {1099, Round::Up49or99, 1099}, {1000, Round::Up49or99, 1049}, {0, Round::Up49or99, 49},
    };
    for (const Case &c : cases) {
        INFO(c.in << " " << toString(c.round));
        CHECK(roundCents(c.in, c.round) == c.out);
    }
    CHECK(roundFrom("9near") == Round::Near9);
    CHECK(roundFrom("49or99up") == Round::Up49or99);
    CHECK(roundFrom("") == Round::None);
    CHECK_FALSE(roundFrom("7up"));
}

TEST_CASE("Mass updates: margin price as the card works it out, then rounded", "[massupdate][core]")
{
    CHECK(marginPriceCents(850, 30) == 1214);                 // 8.50 / 0.70 = 12.142...
    CHECK(marginPriceCents(850, 30, Round::Up9) == 1219);
    CHECK(marginPriceCents(1000, 50) == 2000);
    CHECK(marginPriceCents(0, 30) == -1);                     // no cost
    CHECK(marginPriceCents(850, 100) == -1);
    CHECK(marginPriceCents(850, -5) == -1);
    CHECK(std::isnan(marginOf(1000, 0)));
    CHECK(std::abs(marginOf(2000, 1000) - 50.0) < 1e-9);
}

TEST_CASE("Mass updates: add $, add %, set", "[massupdate][core]")
{
    CHECK(adjustCents(1000, How::AddDollars, 1.25) == 1125);
    CHECK(adjustCents(1000, How::AddDollars, -20) == 0);      // never below 0
    CHECK(adjustCents(1000, How::AddPercent, 10) == 1100);
    CHECK(adjustCents(1000, How::AddPercent, 10, Round::Up9) == 1109);
    CHECK(adjustCents(1000, How::Set, 7.5) == 750);
    CHECK(adjustCents(1000, How::Set, 7.5, Round::Near9) == 749);
    CHECK(centsText(1299) == "12.99");
    CHECK(centsText(5) == "0.05");
    CHECK(centsFrom("$12.99") == 1299);
    CHECK(centsFrom("abc") == -1);
}

namespace {

std::vector<Row> sampleRows()
{
    std::vector<Row> rows;
    const auto row = [&](std::string name, std::string upc, std::int64_t price, std::int64_t cost, std::string brand) {
        Row r;
        r.at = std::uint32_t(rows.size());
        r.id = "i" + std::to_string(rows.size());
        r.name = name;
        r.lowerName = lowerAscii(name);
        r.upc = upc;
        r.codes = {BuilderIndex::codeKey(upc)};
        r.price = price;
        r.cost = cost;
        r.brand = brand;
        r.categoryId = "cigarettes";
        r.category = "CIGARETTES";
        rows.push_back(r);
    };
    row("MONTE CARLO", "012345678905", 1500, 1000, "MONTE");
    row("MONTE YELLOW", "012345678912", 1200, 0, "MONTE");
    row("MONTE ZEBRA", "012345678929", 2500, 2000, "MONTE");
    row("ALPHA BOX", "012345678936", 900, 450, "ALPHA");
    return rows;
}

} // namespace

TEST_CASE("Mass updates: a margin price leaves out and lists the items with no cost", "[massupdate][core]")
{
    const std::vector<Row> rows = sampleRows();
    const Plan p = plan(rows, {0, 1, 2, 3}, {"margin", How::Set, 40, Round::Up9});
    REQUIRE(p.skipped.size() == 1);
    CHECK(p.skipped[0].name == "MONTE YELLOW");
    CHECK(p.skipped[0].why == "no cost");
    REQUIRE(p.changes.size() == 3);
    CHECK(p.changes[0].field == "price");
    CHECK(p.changes[0].before == "15.00");
    CHECK(p.changes[0].after == "16.69");               // 10.00 / 0.6 = 16.67 -> 9 up
    CHECK(p.changes[0].upc == "012345678905");
}

TEST_CASE("Mass updates: price, cost and copied prices planned", "[massupdate][core]")
{
    const std::vector<Row> rows = sampleRows();
    Plan p = plan(rows, {0, 3}, {"price", How::AddPercent, 10, Round::None});
    REQUIRE(p.changes.size() == 2);
    CHECK(p.changes[1].after == "9.90");
    p = plan(rows, {0, 1}, {"cost", How::Set, 11.5});
    REQUIRE(p.changes.size() == 2);
    CHECK(p.changes[1].before.empty());                 // had none
    CHECK(p.changes[1].after == "11.50");
    p = plan(rows, {0, 1}, {"cost", How::AddDollars, 1});
    CHECK(p.changes.size() == 1);
    CHECK(p.skipped.size() == 1);                       // nothing to add to
    p = plan(rows, {0, 1, 2}, {"copy", How::Set, 0, Round::None, 2});
    REQUIRE(p.changes.size() == 2);
    CHECK(p.changes[0].after == "25.00");
    // Undo: the same the other way, last first.
    const std::vector<Change> back = reversed(p.changes);
    CHECK(back[0].itemId == p.changes[1].itemId);
    CHECK(back[0].after == p.changes[1].before);
}

TEST_CASE("Mass updates: a range on the description and on the price; find by UPC", "[massupdate][core]")
{
    const std::vector<Row> rows = sampleRows();
    Filter f;
    f.rangeField = Field::Name;
    f.from = "MONTE YEL";
    f.to = "MONTE YEZ";
    std::vector<std::uint32_t> got = pick(rows, f, Field::Name, false);
    REQUIRE(got.size() == 1);
    CHECK(rows[got[0]].name == "MONTE YELLOW");
    f.from = "M";
    f.to = "MONTE";                 // the end as a start: every MONTE
    CHECK(pick(rows, f, Field::Name, false).size() == 3);

    f = {};
    f.rangeField = Field::Price;
    f.from = "10";
    f.to = "20";
    got = pick(rows, f, Field::Price, true);
    REQUIRE(got.size() == 2);
    CHECK(rows[got[0]].name == "MONTE CARLO");          // high to low
    f.rangeField = Field::Margin;
    f.from = "0";
    f.to = "30";
    got = pick(rows, f, Field::Name, false);
    REQUIRE(got.size() == 1);                           // 20% only; no cost: margin not known
    CHECK(rows[got[0]].name == "MONTE ZEBRA");

    f = {};
    f.find = "12345678912";        // without its leading zero
    CHECK(pick(rows, f, Field::Name, false).size() == 1);
    f.find = "01234567891";        // 11 digits: no check digit
    CHECK(pick(rows, f, Field::Name, false).size() == 1);
    f.find = "monte";
    CHECK(pick(rows, f, Field::Name, false).size() == 3);
    f = {};
    f.path = {"ALPHA"};
    CHECK(pick(rows, f, Field::Name, false).size() == 1);

    UpcIndex x;
    x.build(rows.size(), [&](std::size_t i) { return std::vector<std::string>{rows[i].upc}; });
    CHECK(x.find("12345678929") == 2);
    CHECK(x.find("99999") == -1);
}
