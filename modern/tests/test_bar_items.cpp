#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/menu_import.hh"
#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// The bar's drinks: ABV (%) and pour (oz) on each item, the standard drinks in one, and the drink
// sheet's columns in the menu importer. (Made-up rows; the store's own sheet holds its costs.)

namespace {

// The drink sheet's columns, in its order.
const QString kHeader = u"Name,Price,Cost,ABV %,Pour oz,Std drinks,Category,Tax,Product group,Sub group,Maker,"
                        "Prints to,Vendor,5x target (incl tax),Note\n"_s;

const core::MenuItem *named(const PosService &pos, const std::string &name)
{
    for (const core::MenuItem &m : pos.shared()->menu)
        if (m.name == name)
            return &m;
    return nullptr;
}

} // namespace

TEST_CASE("Standard drinks: pour oz x ABV / 100 / 0.6", "[bar][abv]")
{
    using Catch::Approx;
    CHECK(core::standardDrinks(12, 5) == Approx(1.0));        // a bottle of beer
    CHECK(core::standardDrinks(1.25, 40) == Approx(0.8333).epsilon(0.001));
    CHECK(core::standardDrinks(2, 40) == Approx(1.3333).epsilon(0.001));
    CHECK(core::standardDrinks(5, 14) == Approx(1.1667).epsilon(0.001));
    CHECK(core::standardDrinks(25.4, 14) == Approx(5.93).epsilon(0.01));   // a 750ml bottle of wine: about 6
    CHECK(core::standardDrinks(16, 5) == Approx(1.3333).epsilon(0.001));   // a draft pint
    CHECK(core::standardDrinks(0, 40) == 0.0);
    CHECK(core::standardDrinks(1.25, 0) == 0.0);
}

TEST_CASE("Menu import: the drink sheet's ABV % and Pour oz; its other columns are let be", "[bar][abv][import]")
{
    const app::MenuImport r = app::readMenuCsv(
        kHeader
        + u"EXAMPLE LAGER,5.00,0.90,5,12,1.00,Bottled Beer,Alcohol,Beer,Bottle,,Bar,Example Beer Co,4.83,\n"
          "EXAMPLE BOURBON 1.25OZ,8.00,1.40,45,1.25,0.94,Whiskey,Alcohol,Liquor,Bourbon,Example Distillery,Bar,"
          "Example Spirits,7.51,\"Neat, rocks, or up; ask first\"\n"
          "EXAMPLE NA BEER,5.00,1.00,,12,,Bottled Beer,Food,Beer,Bottle,,Bar,Example Beer Co,5.37,\n"_s);
    INFO(r.problems.join(u"; "_s).toStdString());
    REQUIRE(r.problems.isEmpty());
    REQUIRE(r.items.size() == 3);
    CHECK(r.columns.contains(u"abv"_s));
    CHECK(r.columns.contains(u"pourOz"_s));
    CHECK(r.items[0].name == u"EXAMPLE LAGER"_s);
    CHECK(r.items[0].price == 5.0);
    CHECK(r.items[0].cost == u"0.90"_s);
    CHECK(r.items[0].abv == u"5"_s);
    CHECK(r.items[0].pourOz == u"12"_s);
    CHECK(r.items[0].category == u"Bottled Beer"_s);
    CHECK(r.items[0].printer == u"Bar"_s);
    // The quoted note with commas stays in its own column: everything after it is where it was.
    CHECK(r.items[1].abv == u"45"_s);
    CHECK(r.items[1].pourOz == u"1.25"_s);
    CHECK(r.items[1].maker == u"Example Distillery"_s);
    CHECK(r.items[1].tax == u"Alcohol"_s);
    // No ABV: none, the rest read.
    CHECK(r.items[2].abv.isEmpty());
    CHECK(r.items[2].pourOz == u"12"_s);
    CHECK(r.items[2].tax == u"Food"_s);

    // The other names for the columns, with a % or "oz" in the cell.
    for (const QString &head : {u"Name,Price,ABV,Pour\n"_s, u"Name,Price,Alcohol %,Serving oz\n"_s}) {
        const app::MenuImport o = app::readMenuCsv(head + u"EXAMPLE RYE,9.00,50%,2 oz\n"_s);
        REQUIRE(o.items.size() == 1);
        CHECK(o.items[0].abv == u"50"_s);
        CHECK(o.items[0].pourOz == u"2"_s);
    }
    // Not a number: that row is a problem, the others come in.
    const app::MenuImport bad = app::readMenuCsv(u"Name,Price,ABV %,Pour oz\nEXAMPLE A,5.00,strong,12\nEXAMPLE B,5.00,5,12\n"_s);
    CHECK(bad.items.size() == 1);
    CHECK(bad.problems.size() == 1);

    // Into the menu: the drinks carry them; one already on the menu gets them when it has none.
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    QVariantList rows;
    for (const app::ImportedItem &i : r.items)
        rows << QVariantMap{{u"name"_s, i.name}, {u"price"_s, i.price}, {u"category"_s, i.category}, {u"tax"_s, i.tax},
                            {u"abv"_s, i.abv}, {u"pourOz"_s, i.pourOz}};
    CHECK(pos.importMenuRows(rows, QString(), false) == 3);
    const core::MenuItem *bourbon = named(pos, "EXAMPLE BOURBON 1.25OZ");
    REQUIRE(bourbon);
    CHECK(core::abvOf(*bourbon) == 45.0);
    CHECK(core::pourOzOf(*bourbon) == 1.25);
    CHECK(core::standardDrinksOf(*bourbon) == Catch::Approx(0.9375));
    const core::MenuItem *na = named(pos, "EXAMPLE NA BEER");
    REQUIRE(na);
    CHECK(core::abvOf(*na) == 0.0);
    CHECK(core::pourOzOf(*na) == 12.0);

    QVariantMap again = rows[2].toMap();
    again[u"abv"_s] = u"0.5"_s;
    CHECK(pos.importMenuRows({again}, QString(), false) == 1);
    CHECK(core::abvOf(*named(pos, "EXAMPLE NA BEER")) == 0.5);
}

TEST_CASE("ABV and pour on the item card: saved, kept, sent to the screens", "[bar][abv][menubuilder]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.saveMenuItemCard({{u"name"_s, u"EXAMPLE IPA 16OZ"_s}, {u"price"_s, u"8.00"_s}, {u"family"_s, u"burgers"_s},
                                  {u"abv"_s, u"6.5"_s}, {u"pourOz"_s, u"16"_s}}));
    const core::MenuItem *ipa = named(pos, "EXAMPLE IPA 16OZ");
    REQUIRE(ipa);
    CHECK(core::abvOf(*ipa) == 6.5);
    CHECK(core::pourOzOf(*ipa) == 16.0);
    // Saved (the store keeps it as JSON), and with nothing taken out of stock for it.
    REQUIRE(sink.menu.contains(ipa->id));
    const core::MenuItem back = app::menuItemFromJson(app::toJson(sink.menu.at(ipa->id)));
    CHECK(core::abvOf(back) == 6.5);
    CHECK(core::pourOzOf(back) == 16.0);
    CHECK_FALSE(pos.itemCostNow(*ipa).has_value());

    // What the Menu Builder and the tablets (RemoteSession) get.
    bool seen = false;
    for (const QVariant &v : pos.menuItems())
        if (v.toMap()[u"name"_s] == u"EXAMPLE IPA 16OZ"_s) {
            seen = true;
            CHECK(v.toMap()[u"abv"_s] == u"6.5"_s);
            CHECK(v.toMap()[u"pourOz"_s] == u"16"_s);
            CHECK(v.toMap()[u"stdDrinks"_s] == u"1.73"_s);   // 16 x 6.5 / 100 / 0.6
        }
    CHECK(seen);

    // Cleared when emptied; a word is refused.
    const QString id = QString::fromStdString(ipa->id);
    REQUIRE(pos.saveMenuItemCard({{u"id"_s, id}, {u"name"_s, u"EXAMPLE IPA 16OZ"_s}, {u"abv"_s, QString()}}));
    CHECK(core::abvOf(*named(pos, "EXAMPLE IPA 16OZ")) == 0.0);
    CHECK(core::pourOzOf(*named(pos, "EXAMPLE IPA 16OZ")) == 16.0);
    CHECK_FALSE(pos.saveMenuItemCard({{u"id"_s, id}, {u"name"_s, u"EXAMPLE IPA 16OZ"_s}, {u"abv"_s, u"strong"_s}}));
    CHECK_FALSE(pos.saveMenuItemCard({{u"id"_s, id}, {u"name"_s, u"EXAMPLE IPA 16OZ"_s}, {u"abv"_s, u"140"_s}}));
}
