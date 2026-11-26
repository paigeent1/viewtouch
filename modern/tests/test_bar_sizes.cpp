#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QJsonArray>
#include <QJsonObject>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
using Catch::Approx;

// The bar: the sizes a liquor is bought in (750ml, 1L, 1.75L...). One good kept in ounces; each size its own
// cost per bottle, bottles per case and UPCs; received by the size; Import costs from a spreadsheet (CSV).
// Made-up liquors and costs.

namespace {

const core::Ingredient *good(const PosService &pos, const std::string &name)
{
    for (const core::Ingredient &g : pos.shared()->ingredients)
        if (g.name == name)
            return &g;
    return nullptr;
}

int indexOf(const PosService &pos, const std::string &name)
{
    for (int i = 0; i < int(pos.shared()->ingredients.size()); ++i)
        if (pos.shared()->ingredients[i].name == name)
            return i;
    return -1;
}

// A bottle good: 750ml at $20.00, 12 a case, and the sizes typed in Manager -> Inventory.
void addVodka(PosService &pos, const QString &sizes)
{
    QVariantMap g = pos.adminNewRecord(u"inventory"_s);
    g[u"name"_s] = u"EXAMPLE VODKA"_s;
    g[u"kind"_s] = u"bottle"_s;
    g[u"packOz"_s] = core::kOz750ml;
    g[u"packCost"_s] = 20.0;
    g[u"perCase"_s] = 12;
    g[u"category"_s] = u"Vodka"_s;
    g[u"upcText"_s] = u"000000075001"_s;
    g[u"sizesText"_s] = sizes;
    REQUIRE(pos.adminSave(u"inventory"_s, -1, g));
}

} // namespace

TEST_CASE("Bottle sizes: the size words, the Other sizes lines", "[bar][sizes]")
{
    CHECK(core::parseBottleSize("750ml") == Approx(core::kOz750ml));
    CHECK(core::parseBottleSize("750 ML") == Approx(core::kOz750ml));
    CHECK(core::parseBottleSize("1L") == Approx(core::kOz1L));
    CHECK(core::parseBottleSize("1.75L") == Approx(core::kOz1750ml));
    CHECK(core::parseBottleSize("1.75 liter") == Approx(core::kOz1750ml));
    CHECK(core::parseBottleSize("375ml") == Approx(core::kOz375ml));
    CHECK(core::parseBottleSize("50ml") == Approx(1.7));
    CHECK(core::parseBottleSize("1.75") == Approx(core::kOz1750ml));   // a bare number: liters up to 5
    CHECK(core::parseBottleSize("750") == Approx(core::kOz750ml));     // ... ml from 50
    CHECK(core::parseBottleSize("25.4 oz") == Approx(25.4));
    CHECK(core::parseBottleSize("big") == 0);
    CHECK(core::parseBottleSize("20") == 0);   // 20 what?
    CHECK(core::bottleSizeLabel(core::kOz1750ml) == "1.75L");
    CHECK(core::bottleSizeLabel(core::kOz1L) == "1L");
    CHECK(core::bottleSizeLabel(1.7) == "50ml");
    CHECK(core::bottleSizeLabel(20) == "20 oz");

    std::vector<core::BottleSize> sizes;
    std::string bad;
    REQUIRE(core::parseBottleSizes("1.75L $42.99 6 000000175001 000000175002\n\n1 L 30\n", &sizes, &bad));
    REQUIRE(sizes.size() == 2);
    CHECK(sizes[0].oz == Approx(core::kOz1750ml));
    CHECK(sizes[0].cost == Money::fromCents(4299));
    CHECK(sizes[0].perCase == 6);
    CHECK(sizes[0].upcs == std::vector<std::string>{"000000175001", "000000175002"});
    CHECK(sizes[1].oz == Approx(core::kOz1L));
    CHECK(sizes[1].cost == Money::fromCents(3000));
    CHECK(sizes[1].perCase == 0);
    std::vector<core::BottleSize> again;
    REQUIRE(core::parseBottleSizes(core::bottleSizesText(sizes), &again, &bad));
    CHECK(again == sizes);
    CHECK_FALSE(core::parseBottleSizes("1.75L 42.99\nmagnum lots", &sizes, &bad));
    CHECK(bad == "magnum lots");
}

TEST_CASE("Bottle sizes: older data loads as one size; sizes round trip", "[bar][sizes]")
{
    // A bottle good saved before sizes: no sizes, no UPCs, the same JSON back.
    const QJsonObject old{{u"id"_s, u"example-gin"_s}, {u"name"_s, u"EXAMPLE GIN"_s}, {u"unit"_s, u"oz"_s},
                          {u"onHand"_s, 50.8}, {u"lowAt"_s, 0}, {u"cost"_s, 0.79}, {u"vendor"_s, u""_s},
                          {u"kind"_s, u"bottle"_s}, {u"packOz"_s, 25.4}, {u"perCase"_s, 12}, {u"category"_s, u"Gin"_s},
                          {u"packCost"_s, 20.0}};
    const core::Ingredient g = app::ingredientFromJson(old);
    CHECK(g.kind == "bottle");
    CHECK(g.packOz == Approx(25.4));
    CHECK(g.packCost == Money::fromCents(2000));
    CHECK(g.sizes.empty());
    CHECK(g.upcs.empty());
    const QJsonObject back = app::toJson(g);
    CHECK_FALSE(back.contains(u"sizes"_s));
    CHECK_FALSE(back.contains(u"upcs"_s));
    // With sizes: kept as they were.
    core::Ingredient s = g;
    s.upcs = {"000000075001"};
    s.sizes.push_back({core::kOz1750ml, Money::fromCents(4200), 6, {"000000175001"}});
    s.sizes.push_back({core::kOz1L, Money(), 12, {}});
    CHECK(app::ingredientFromJson(app::toJson(s)) == s);
    // A delivery line received before sizes: no size of its own.
    const core::Delivery d = app::deliveryFromJson(QJsonObject{
        {u"id"_s, 1}, {u"at"_s, 0}, {u"lines"_s, QJsonArray{QJsonObject{{u"ingredientId"_s, u"example-gin"_s}, {u"qty"_s, 25.4},
                                                                      {u"unitCost"_s, 79}, {u"packs"_s, 1}, {u"packCost"_s, 2000}}}}});
    REQUIRE(d.lines.size() == 1);
    CHECK(d.lines[0].packOz == 0);
    CHECK(d.lines[0].total() == Money::fromCents(2000));
}

TEST_CASE("Bottle sizes: Manager -> Inventory, receiving two sizes of one liquor", "[bar][sizes][receiving]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    // Other sizes that aren't: refused.
    QVariantMap bad = pos.adminNewRecord(u"inventory"_s);
    bad[u"name"_s] = u"EXAMPLE RUM"_s;
    bad[u"kind"_s] = u"bottle"_s;
    bad[u"packOz"_s] = core::kOz750ml;
    bad[u"sizesText"_s] = u"750ml 20 12"_s;   // its main size
    CHECK_FALSE(pos.adminSave(u"inventory"_s, -1, bad));
    bad[u"sizesText"_s] = u"1L 30\n1 L 31"_s;  // twice
    CHECK_FALSE(pos.adminSave(u"inventory"_s, -1, bad));
    bad[u"sizesText"_s] = u"a jug"_s;
    CHECK_FALSE(pos.adminSave(u"inventory"_s, -1, bad));

    addVodka(pos, u"1.75L 42.00 6 000000175001"_s);
    const core::Ingredient *v = good(pos, "EXAMPLE VODKA");
    REQUIRE(v);
    REQUIRE(v->sizes.size() == 1);
    CHECK(v->sizes[0].oz == Approx(core::kOz1750ml));
    CHECK(v->sizes[0].cost == Money::fromCents(4200));
    CHECK(v->upcs == std::vector<std::string>{"000000075001"});
    CHECK(v->cost == Money::fromCents(79));   // per oz: the main size's ($20.00 / 25.4)
    const std::string id = v->id;
    const QString vid = QString::fromStdString(id);
    // The editor shows them as typed.
    const QVariantMap rec = pos.adminRecords(u"inventory"_s)[indexOf(pos, "EXAMPLE VODKA")].toMap();
    CHECK(rec[u"sizesText"_s] == u"1.75L 42.00 6 000000175001"_s);
    CHECK(rec[u"upcText"_s] == u"000000075001"_s);

    // Receive a Delivery: a row a size, each with its UPCs and its case.
    int rows = 0;
    for (const QVariant &x : pos.receiving()[u"ingredients"_s].toList()) {
        const QVariantMap r = x.toMap();
        if (r[u"id"_s] != vid)
            continue;
        ++rows;
        if (r[u"key"_s] == vid) {
            CHECK(r[u"name"_s] == u"EXAMPLE VODKA (750ml)"_s);
            CHECK(r[u"perCase"_s] == 12);
            CHECK(r[u"cost"_s].toDouble() == Approx(20.0));
        } else {
            CHECK(r[u"key"_s] == vid + u"@1.75L"_s);
            CHECK(r[u"name"_s] == u"EXAMPLE VODKA (1.75L)"_s);
            CHECK(r[u"size"_s].toDouble() == Approx(core::kOz1750ml));
            CHECK(r[u"perCase"_s] == 6);
            CHECK(r[u"cost"_s].toDouble() == Approx(42.0));
            CHECK(r[u"upcs"_s].toStringList() == QStringList{u"000000175001"_s});
        }
    }
    CHECK(rows == 2);

    // A size it doesn't come in: refused.
    CHECK_FALSE(pos.receiveDelivery({{u"lines"_s, QVariantList{QVariantMap{{u"ingredient"_s, vid}, {u"bottles"_s, 1},
                                                                           {u"size"_s, core::kOz1L}}}}}));
    // The same liquor on the invoice twice: 2 bottles of 750ml at $20.00, a case of 1.75L at $45.00 a bottle.
    REQUIRE(pos.receiveDelivery({{u"invoice"_s, u"S1"_s},
                                 {u"lines"_s, QVariantList{
                                      QVariantMap{{u"ingredient"_s, vid}, {u"bottles"_s, 2}, {u"cost"_s, 20.0}},
                                      QVariantMap{{u"ingredient"_s, vid}, {u"cases"_s, 1}, {u"size"_s, core::kOz1750ml},
                                                  {u"cost"_s, 45.0}}}}}));
    const core::Ingredient *after = pos.shared()->ingredient(id);
    CHECK(after->onHand == Approx(2 * core::kOz750ml + 6 * core::kOz1750ml));
    CHECK(after->received == Approx(2 * core::kOz750ml + 6 * core::kOz1750ml));
    const core::Delivery &d = pos.shared()->deliveries.back();
    REQUIRE(d.lines.size() == 2);
    CHECK(d.lines[0].name == "EXAMPLE VODKA (750ml)");
    CHECK(d.lines[0].packs == Approx(2));
    CHECK(d.lines[0].packOz == 0);
    CHECK(d.lines[0].costDifference() == Money());
    CHECK(d.lines[1].name == "EXAMPLE VODKA (1.75L)");
    CHECK(d.lines[1].packs == Approx(6));
    CHECK(d.lines[1].qty == Approx(6 * core::kOz1750ml));
    CHECK(d.lines[1].packOz == Approx(core::kOz1750ml));
    CHECK(d.lines[1].packCost == Money::fromCents(4500));
    CHECK(d.lines[1].packSystemCost == Money::fromCents(4200));   // that size's system cost
    CHECK(d.lines[1].unitCost == Money::fromCents(76));           // $45.00 / 59.2 oz
    CHECK(d.lines[1].costDifference() == Money::fromCents(300));
    CHECK(d.total() == Money::fromCents(2 * 2000 + 6 * 4500));
    CHECK(app::deliveryFromJson(app::toJson(d)) == d);
    // Receiving changes no system cost; the good's cost per oz stays the main size's.
    CHECK(after->packCost == Money::fromCents(2000));
    CHECK(after->sizes[0].cost == Money::fromCents(4200));
    CHECK(after->cost == Money::fromCents(79));
    const QVariantList diffs = pos.costDifferences();
    REQUIRE(diffs.size() == 1);
    CHECK(diffs[0].toMap()[u"item"_s] == u"EXAMPLE VODKA (1.75L)"_s);
    CHECK(diffs[0].toMap()[u"systemCost"_s] == u"$42.00"_s);
    CHECK(diffs[0].toMap()[u"invoiceCost"_s] == u"$45.00"_s);

    // The shelf count stays in bottles of the main size.
    REQUIRE(pos.saveBarCount({{u"lines"_s, QVariantList{QVariantMap{{u"ingredient"_s, vid}, {u"count"_s, u"16"_s}}}}}));
    CHECK(pos.shared()->ingredient(id)->onHand == Approx(16 * core::kOz750ml));
}

TEST_CASE("Bottle sizes: Import costs from a spreadsheet, preview then Apply", "[bar][sizes][import]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    addVodka(pos, QString());
    const std::size_t goodsBefore = pos.shared()->ingredients.size();
    const std::size_t vendorsBefore = pos.shared()->settings.vendors.size();

    CHECK_FALSE(pos.barCostImport(u"Name,Price\nX,1\n"_s));   // no Size column
    CHECK_FALSE(pos.barCostImportApply());                      // nothing read

    const QString csv = u"Liquor,Category,Size,Bottles per case,Cost per bottle,Vendor,Drink price\n"
                        "example vodka,Vodka,750ml,12,$21.50,Example Spirits,8.00\n"   // the main size's cost
                        "EXAMPLE VODKA,Vodka,1.75L,6,44.00,Example Spirits,\n"         // a new size
                        "\"EXAMPLE  GIN\",Gin,1L,12,28.00,Example Spirits,\n"          // a new good...
                        "EXAMPLE GIN,Gin,750ml,,22.00,,\n"                             // ... in two sizes
                        "EXAMPLE TEQUILA,Tequila,a jug,6,30,,\n"                       // not a size
                        "\n"_s;
    REQUIRE(pos.barCostImport(csv));
    const QVariantMap preview = pos.barInventory()[u"costImport"_s].toMap();
    CHECK(preview[u"newGoods"_s] == 1);
    CHECK(preview[u"changed"_s] == 3);
    CHECK(preview[u"same"_s] == 0);
    CHECK(preview[u"problemCount"_s] == 1);
    CHECK(preview[u"vendorsMade"_s] == 1);
    CHECK(preview[u"drinkPrices"_s] == true);
    CHECK(preview[u"toApply"_s] == 2);
    CHECK(preview[u"rows"_s].toList().size() == 4);
    CHECK(preview[u"rows"_s].toList()[0].toMap()[u"what"_s].toString().contains(u"$20.00 → $21.50"_s));
    // Nothing changed by looking.
    CHECK(pos.shared()->ingredients.size() == goodsBefore);
    CHECK(pos.shared()->settings.vendors.size() == vendorsBefore);
    CHECK(good(pos, "EXAMPLE VODKA")->packCost == Money::fromCents(2000));
    CHECK(good(pos, "EXAMPLE VODKA")->sizes.empty());

    REQUIRE(pos.barCostImportApply());
    CHECK(pos.barInventory()[u"costImport"_s].toMap().isEmpty());   // done
    const core::Ingredient *v = good(pos, "EXAMPLE VODKA");
    REQUIRE(v);
    CHECK(v->packCost == Money::fromCents(2150));
    CHECK(v->cost == Money::fromCents(85));   // $21.50 / 25.4 oz
    REQUIRE(v->sizes.size() == 1);
    CHECK(v->sizes[0].oz == Approx(core::kOz1750ml));
    CHECK(v->sizes[0].cost == Money::fromCents(4400));
    CHECK(v->sizes[0].perCase == 6);
    const core::Ingredient *gin = good(pos, "EXAMPLE GIN");
    REQUIRE(gin);
    CHECK(gin->kind == "bottle");
    CHECK(gin->unit == "oz");
    CHECK(gin->packOz == Approx(core::kOz1L));   // its first size: the main one
    CHECK(gin->packCost == Money::fromCents(2800));
    CHECK(gin->perCase == 12);
    CHECK(gin->category == "Gin");
    REQUIRE(gin->sizes.size() == 1);
    CHECK(gin->sizes[0].oz == Approx(core::kOz750ml));
    CHECK(gin->sizes[0].cost == Money::fromCents(2200));
    CHECK(gin->sizes[0].perCase == 12);   // none given: the usual for the size
    CHECK(pos.shared()->settings.vendors.size() == vendorsBefore + 1);
    CHECK(gin->vendor == v->vendor);
    CHECK_FALSE(v->vendor.empty());
    CHECK_FALSE(good(pos, "EXAMPLE TEQUILA"));

    // The same file again: no change.
    REQUIRE(pos.barCostImport(csv));
    const QVariantMap again = pos.barInventory()[u"costImport"_s].toMap();
    CHECK(again[u"same"_s] == 4);
    CHECK(again[u"toApply"_s] == 0);
    REQUIRE(pos.barCostImportCancel());
    CHECK(pos.barInventory()[u"costImport"_s].toMap().isEmpty());
}

TEST_CASE("Bottle sizes: Import costs needs the price book to apply", "[bar][sizes][import][pricebook]")
{
    app::PosData data = test::seedPosData();
    for (core::Employee &e : data.employees)
        e.allow.erase(core::perm::PriceBook);
    PosService pos(std::move(data), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));   // a manager
    REQUIRE(pos.barCostImport(u"Liquor,Size,Cost per bottle\nEXAMPLE RYE,750ml,30\n"_s));
    CHECK(pos.barInventory()[u"costImport"_s].toMap()[u"canApply"_s] == false);
    CHECK_FALSE(pos.barCostImportApply());
    CHECK_FALSE(good(pos, "EXAMPLE RYE"));
}
