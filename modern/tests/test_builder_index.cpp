#include <catch2/catch_test_macros.hpp>

#include "core/builder_index.hh"

#include <string>
#include <vector>

using vt::core::BuilderIndex;
using vt::core::MenuItem;

// Menu Builder's quick index (core/builder_index.hh): a big price book department as a tree, searched
// without looking at every item.

namespace {

// 20,000 cigars in one department, over four brands (brand A has product groups, one with sub groups),
// then a few drinks and a choice.
std::vector<MenuItem> bigMenu()
{
    std::vector<MenuItem> menu;
    const char *brands[] = {"ALPHA", "BRAVO", "CHARLIE", "DELTA"};
    for (int i = 0; i < 20000; ++i) {
        MenuItem m;
        m.id = "c" + std::to_string(i);
        m.name = std::string(brands[i % 4]) + " CIGAR " + std::to_string(i);
        m.family = "cigars";
        m.manufacturer = brands[i % 4];
        if (i % 4 == 0) {
            m.productGroup = i % 8 == 0 ? "ROBUSTO" : "TORO";
            if (i % 8 == 0)
                m.subGroup = i % 16 == 0 ? "MADURO" : "NATURAL";
        }
        m.barcodes = {std::to_string(700000000000LL + i)};
        menu.push_back(m);
    }
    for (const char *n : {"Beer", "Root Beer", "Wine"}) {
        MenuItem m;
        m.id = n;
        m.name = n;
        m.family = "drinks";
        m.number = n[0] == 'B' ? "12" : "";
        menu.push_back(m);
    }
    MenuItem choice;
    choice.id = "lime";
    choice.name = "Beer lime";
    choice.family = "drinks";
    choice.isModifier = true;
    menu.push_back(choice);
    return menu;
}

std::vector<std::string> pathOf(const MenuItem &m)
{
    return {m.manufacturer, m.productGroup, m.subGroup};
}

} // namespace

TEST_CASE("Builder index: a department's tree, level by level", "[builderindex]")
{
    const std::vector<MenuItem> menu = bigMenu();
    BuilderIndex x;
    x.build(menu, {"cigars"}, pathOf);
    CHECK(x.size() == menu.size());
    CHECK(x.count("cigars") == 20000);
    CHECK(x.count("drinks") == 3);          // not the choice
    CHECK(x.family("drinks").size() == 4);  // (every item, the choice too)
    CHECK(x.hasTree("cigars"));
    CHECK_FALSE(x.hasTree("drinks"));

    const BuilderIndex::Node *top = x.node("cigars", {});
    REQUIRE(top);
    CHECK(top->count == 20000);
    CHECK(top->children == std::map<std::string, int>{{"ALPHA", 5000}, {"BRAVO", 5000}, {"CHARLIE", 5000}, {"DELTA", 5000}});

    const BuilderIndex::Node *bravo = x.node("cigars", {"BRAVO"});
    REQUIRE(bravo);
    CHECK(bravo->count == 5000);
    CHECK(bravo->children.empty());          // the bottom: no product groups
    REQUIRE(bravo->items.size() == 5000);
    CHECK(menu[bravo->items.front()].name == "BRAVO CIGAR 1");   // in menu order
    for (std::uint32_t i : bravo->items)
        CHECK(menu[i].manufacturer == "BRAVO");

    const BuilderIndex::Node *alpha = x.node("cigars", {"ALPHA"});
    REQUIRE(alpha);
    CHECK(alpha->children == std::map<std::string, int>{{"ROBUSTO", 2500}, {"TORO", 2500}});
    const BuilderIndex::Node *robusto = x.node("cigars", {"ALPHA", "ROBUSTO"});
    REQUIRE(robusto);
    CHECK(robusto->children == std::map<std::string, int>{{"MADURO", 1250}, {"NATURAL", 1250}});
    CHECK(x.node("cigars", {"ALPHA", "TORO"})->children.empty());   // an empty sub group: the path stops
    CHECK(x.node("cigars", {"ALPHA", "ROBUSTO", "MADURO"})->items.size() == 1250);

    // A path that isn't there (a brand renamed meanwhile): none, and how much of it is.
    CHECK(x.node("cigars", {"ALPHA", "GONE"}) == nullptr);
    CHECK(x.reach("cigars", {"ALPHA", "GONE"}) == 1);
    CHECK(x.reach("cigars", {"ALPHA", "ROBUSTO", "MADURO"}) == 3);
    CHECK(x.reach("cigars", {"NOBODY"}) == 0);
}

TEST_CASE("Builder index: a UPC is looked up, a name searched and capped, in the given categories only", "[builderindex]")
{
    const std::vector<MenuItem> menu = bigMenu();
    BuilderIndex x;
    x.build(menu, {"cigars"}, pathOf);

    // A UPC: exactly the one item, typed as printed, as an EAN-13 (a 0 in front) or without its check digit.
    BuilderIndex::Found f = x.search("700000012345", {"cigars"}, 200);
    REQUIRE(f.items.size() == 1);
    CHECK(menu[f.items[0]].id == "c12345");
    CHECK_FALSE(f.more);
    CHECK(x.search("0700000012345", {"cigars"}, 200).items == f.items);
    // (no check digit: the 11 digits with their check digit made again)
    MenuItem upc;
    upc.id = "zen";
    upc.name = "ZEN TUBE";
    upc.family = "cigars";
    upc.barcodes = {"016165009116"};
    std::vector<MenuItem> withZen = menu;
    withZen.push_back(upc);
    BuilderIndex y;
    y.build(withZen, {"cigars"}, pathOf);
    f = y.search("01616500911", {"cigars"}, 200);
    REQUIRE(f.items.size() == 1);
    CHECK(withZen[f.items[0]].id == "zen");
    // Not in the categories looked in: not found.
    CHECK(x.search("700000012345", {"drinks"}, 200).items.empty());
    // A code on no item: nothing (and no names looked through).
    CHECK(x.search("799999999999", {"cigars"}, 200).items.empty());

    // A name: the first 200, and that there are more.
    f = x.search("delta", {"cigars"}, 200);
    CHECK(f.items.size() == 200);
    CHECK(f.more);
    for (std::uint32_t i : f.items)
        CHECK(menu[i].manufacturer == "DELTA");
    f = x.search("cigar 1999", {"cigars"}, 200);   // part of a name
    CHECK(f.items.size() == 11);                   // 1999, 19990..19999
    CHECK_FALSE(f.more);

    // An item number (exactly), and a name, in the drinks; never a choice.
    f = x.search("12", {"drinks"}, 200);
    REQUIRE(f.items.size() == 1);
    CHECK(menu[f.items[0]].name == "Beer");
    f = x.search("beer", {"drinks"}, 200);
    REQUIRE(f.items.size() == 2);
    CHECK(menu[f.items[0]].name == "Beer");
    CHECK(menu[f.items[1]].name == "Root Beer");
    CHECK(x.search("beer", {"cigars"}, 200).items.empty());
    CHECK(x.search("", {"drinks"}, 200).items.empty());
}

TEST_CASE("Builder index: barcodes kept without leading zeros; letters in small letters", "[builderindex]")
{
    CHECK(BuilderIndex::codeKey("0016165009116") == "16165009116");
    CHECK(BuilderIndex::codeKey("016165009116") == "16165009116");
    CHECK(BuilderIndex::codeKey("0") == "0");
    CHECK(BuilderIndex::codeKey("AB-12") == "ab-12");
    CHECK(BuilderIndex::codeKey("").empty());
}
