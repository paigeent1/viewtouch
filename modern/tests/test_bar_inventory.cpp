#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QDate>

#include <cmath>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
using Catch::Approx;

// The bar (HCA handoff, part B): bottles kept in ounces, received by the bottle or case, Link pours to
// bottles, the shelf count in bottles, bottle variance; draft lines, Change keg, keg variance, the pricing
// aid. Made-up drinks in the drink sheet's pattern (the store's own sheet holds its costs).

namespace {

const core::MenuItem *named(const PosService &pos, const std::string &name)
{
    for (const core::MenuItem &m : pos.shared()->menu)
        if (m.name == name)
            return &m;
    return nullptr;
}

QString idOf(const PosService &pos, const std::string &name)
{
    const core::MenuItem *m = named(pos, name);
    REQUIRE(m);
    return QString::fromStdString(m->id);
}

const core::Ingredient *good(const PosService &pos, const std::string &name)
{
    for (const core::Ingredient &g : pos.shared()->ingredients)
        if (g.name == name)
            return &g;
    return nullptr;
}

double recipeAmount(const core::MenuItem &m, const std::string &id)
{
    for (const core::RecipeLine &l : m.recipe)
        if (l.ingredientId == id)
            return l.quantity;
    return 0;
}

std::vector<std::string> rowStarting(const core::Report &r, const std::string &start)
{
    for (const core::ReportRow &x : r.rows)
        if (x.kind == core::ReportRow::Kind::Line && !x.cells.empty() && x.cells.front().starts_with(start))
            return x.cells;
    return {};
}

bool hasSection(const core::Report &r, const std::string &title)
{
    return std::ranges::any_of(r.rows, [&](const core::ReportRow &x) {
        return x.kind == core::ReportRow::Kind::Section && !x.cells.empty() && x.cells.front() == title;
    });
}

QVariantMap row(const char *name, const char *price, const char *cost, const char *category, const char *pour,
                const char *tax = "Alcohol")
{
    return {{u"name"_s, QString::fromLatin1(name)}, {u"price"_s, QString::fromLatin1(price).toDouble()},
            {u"cost"_s, QString::fromLatin1(cost)}, {u"category"_s, QString::fromLatin1(category)},
            {u"tax"_s, QString::fromLatin1(tax)}, {u"pourOz"_s, QString::fromLatin1(pour)}, {u"abv"_s, u"40"_s}};
}

// The made-up bar menu: liquor by the 1.25oz and 2oz, wine by the glass and bottle, splits, bottled beer,
// a draft beer in 12oz and 16oz, a soda and a drink with no size in its name.
void importBarMenu(PosService &pos)
{
    QVariantList rows{
        row("EXAMPLE VODKA 1.25oz", "8.00", "0.98", "Vodka", "1.25"),
        row("EXAMPLE VODKA 2oz", "12.00", "1.57", "Vodka", "2"),
        row("EXAMPLE MALBEC Glass", "10.00", "2.40", "Red Wine", "5"),
        row("EXAMPLE MALBEC Bottle", "40.00", "12.00", "Red Wine", "25.4"),
        row("EXAMPLE MALBEC 375ml", "22.00", "6.50", "Red Wine", "12.7"),
        row("EXAMPLE CAB 375ml", "24.00", "9.00", "Red Wine", "12.7"),
        row("EXAMPLE PROSECCO 187ml", "12.00", "3.00", "Sparkling", "6.3"),
        row("EXAMPLE LAGER", "5.00", "1.10", "Bottled Beer", "12"),
        row("EXAMPLE STOUT 12oz", "6.00", "", "Draft Beer", "12"),
        row("EXAMPLE STOUT 16oz", "8.00", "", "Draft Beer", "16"),
        row("EXAMPLE PUNCH", "9.00", "2.00", "Specials", "6"),
        row("EXAMPLE SODA", "3.00", "0.50", "Soft Drinks", "", "Food"),
    };
    // The soda is no drink of the bar's: no ABV.
    QVariantMap soda = rows.last().toMap();
    soda.remove(u"abv"_s);
    rows.last() = soda;
    REQUIRE(pos.importMenuRows(rows, QString(), false) == rows.size());
}

} // namespace

TEST_CASE("Link pours to bottles: each drink to its bottle by name without the size; preview; undo", "[bar][bottles]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    importBarMenu(pos);
    const std::size_t goodsBefore = pos.shared()->ingredients.size();
    const std::vector<core::RecipeLine> recipeBefore = named(pos, "EXAMPLE VODKA 2oz")->recipe;

    // The preview: what will be linked, and the new bottle goods.
    const QVariantMap preview = pos.barInventory()[u"links"_s].toMap();
    CHECK(preview[u"toLink"_s] == 10);
    CHECK(preview[u"newGoods"_s] == 6);   // vodka, malbec, malbec 375ml, cab, prosecco, lager
    CHECK(preview[u"skipped"_s] == 1);    // the punch: no size in its name
    CHECK(preview[u"same"_s] == 0);
    CHECK(pos.shared()->ingredients.size() == goodsBefore);   // nothing made by looking

    REQUIRE(pos.linkPours());
    const core::Ingredient *vodka = good(pos, "EXAMPLE VODKA");
    const core::Ingredient *malbec = good(pos, "EXAMPLE MALBEC");
    const core::Ingredient *malbecSplit = good(pos, "EXAMPLE MALBEC 375ml");   // the same wine, another size
    const core::Ingredient *cab = good(pos, "EXAMPLE CAB");                    // only a split: its name alone
    const core::Ingredient *prosecco = good(pos, "EXAMPLE PROSECCO");
    const core::Ingredient *lager = good(pos, "EXAMPLE LAGER");
    REQUIRE(vodka);
    REQUIRE(malbec);
    REQUIRE(malbecSplit);
    REQUIRE(cab);
    REQUIRE(prosecco);
    REQUIRE(lager);
    CHECK_FALSE(good(pos, "EXAMPLE STOUT"));   // draft: no bottle; it pours from the keg on its line
    CHECK(pos.shared()->ingredients.size() == goodsBefore + 6);

    // Kept in ounces, by its bottle size; bottled beer in bottles.
    CHECK(vodka->kind == "bottle");
    CHECK(vodka->unit == "oz");
    CHECK(vodka->packOz == core::kOz750ml);
    CHECK(vodka->perCase == 12);
    CHECK(vodka->category == "Vodka");
    CHECK(malbecSplit->packOz == core::kOz375ml);
    CHECK(cab->packOz == core::kOz375ml);
    CHECK(prosecco->packOz == core::kOz187ml);
    CHECK(lager->kind == "beer");
    CHECK(lager->unit == "bottle");
    CHECK(lager->perCase == 24);
    // Their system cost from the drink sheet's costs: a whole bottle's, or the biggest pour's per ounce.
    CHECK(malbec->packCost == Money::fromCents(1200));
    CHECK(vodka->packCost == Money::fromCents(1994));   // 1.57 / 2 oz x 25.4 oz
    CHECK(vodka->cost == Money::fromCents(79));         // per oz, rounded
    CHECK(lager->cost == Money::fromCents(110));

    // Each pour takes its ounces from its bottle: 1.25oz, 2oz, a glass, a whole bottle, a whole split.
    CHECK(recipeAmount(*named(pos, "EXAMPLE VODKA 1.25oz"), vodka->id) == 1.25);
    CHECK(recipeAmount(*named(pos, "EXAMPLE VODKA 2oz"), vodka->id) == 2.0);
    CHECK(recipeAmount(*named(pos, "EXAMPLE MALBEC Glass"), malbec->id) == 5.0);
    CHECK(recipeAmount(*named(pos, "EXAMPLE MALBEC Bottle"), malbec->id) == core::kOz750ml);
    CHECK(recipeAmount(*named(pos, "EXAMPLE MALBEC 375ml"), malbecSplit->id) == core::kOz375ml);
    CHECK(recipeAmount(*named(pos, "EXAMPLE CAB 375ml"), cab->id) == core::kOz375ml);
    CHECK(recipeAmount(*named(pos, "EXAMPLE PROSECCO 187ml"), prosecco->id) == core::kOz187ml);
    CHECK(recipeAmount(*named(pos, "EXAMPLE LAGER"), lager->id) == 1.0);
    CHECK(recipeAmount(*named(pos, "EXAMPLE STOUT 16oz"), "__draft__:EXAMPLE STOUT") == 16.0);
    CHECK(recipeAmount(*named(pos, "EXAMPLE STOUT 12oz"), "__draft__:EXAMPLE STOUT") == 12.0);
    // The item's own settings stay; its cost (GP) is still the Menu Builder's.
    CHECK(core::pourOzOf(*named(pos, "EXAMPLE VODKA 2oz")) == 2.0);
    CHECK(pos.itemCostNow(*named(pos, "EXAMPLE VODKA 2oz")) == Money::fromCents(157));
    CHECK(recipeAmount(*named(pos, "EXAMPLE SODA"), vodka->id) == 0.0);
    // The bottles start at nothing: the drinks don't sell out by themselves (the shelf count comes first).
    CHECK(named(pos, "EXAMPLE VODKA 2oz")->available);
    CHECK(named(pos, "EXAMPLE MALBEC Bottle")->available);
    // Saved.
    CHECK(recipeAmount(sink.menu.at(named(pos, "EXAMPLE VODKA 2oz")->id), vodka->id) == 2.0);

    // Again: nothing left to link.
    CHECK(pos.barInventory()[u"links"_s].toMap()[u"same"_s] == 10);
    CHECK_FALSE(pos.linkPours());

    // Undo: the recipes as they were, the goods it made gone.
    CHECK(pos.barInventory()[u"undo"_s] == 10);
    REQUIRE(pos.undoLinkPours());
    CHECK(pos.shared()->ingredients.size() == goodsBefore);
    CHECK_FALSE(good(pos, "EXAMPLE VODKA"));
    CHECK(named(pos, "EXAMPLE VODKA 2oz")->recipe == recipeBefore);
    CHECK(recipeAmount(*named(pos, "EXAMPLE STOUT 16oz"), "__draft__:EXAMPLE STOUT") == 0.0);
    CHECK_FALSE(pos.undoLinkPours());

    // A bottle good already there (kept, its size too): linked to it, not made again.
    REQUIRE(pos.linkPours());
    core::Ingredient &v = *pos.shared()->ingredient(good(pos, "EXAMPLE VODKA")->id);
    v.packOz = core::kOz1L;
    CHECK(pos.barInventory()[u"links"_s].toMap()[u"toLink"_s] == 0);   // 1.25 and 2 oz: the same
}

TEST_CASE("Bar bottles: received by the bottle and case, damaged lines, poured, counted, variance", "[bar][bottles][variance]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    importBarMenu(pos);
    REQUIRE(pos.linkPours());
    const std::string vodka = good(pos, "EXAMPLE VODKA")->id;
    const QString vodkaId = QString::fromStdString(vodka);
    const auto onHand = [&] { return pos.shared()->ingredient(vodka)->onHand; };

    // Received: a case of 12 and 2 bottles at $20.00 each on the invoice, one came broken (a minus line).
    CHECK_FALSE(pos.receiveDelivery({{u"lines"_s, QVariantList{QVariantMap{{u"ingredient"_s, vodkaId}, {u"bottles"_s, -1}}}}}));
    REQUIRE(pos.receiveDelivery({{u"invoice"_s, u"A100"_s},
                                 {u"lines"_s, QVariantList{
                                      QVariantMap{{u"ingredient"_s, vodkaId}, {u"bottles"_s, 2}, {u"cases"_s, 1}, {u"cost"_s, 20.0}},
                                      QVariantMap{{u"ingredient"_s, vodkaId}, {u"bottles"_s, -1}, {u"reason"_s, u"damaged"_s},
                                                  {u"cost"_s, 20.0}}}}}));
    CHECK(onHand() == Approx(13 * core::kOz750ml));
    const core::Delivery &d = pos.shared()->deliveries.back();
    REQUIRE(d.lines.size() == 2);
    CHECK(d.lines[0].packs == Approx(14));
    CHECK(d.lines[0].qty == Approx(14 * core::kOz750ml));
    CHECK(d.lines[0].packCost == Money::fromCents(2000));
    CHECK(d.lines[1].reason == "damaged");
    CHECK(d.total() == Money::fromCents(26000));   // 14 x $20 - 1 x $20
    CHECK(app::deliveryFromJson(app::toJson(d)) == d);
    // Receiving never changes the system cost: $20.00 against $19.94 goes on Cost differences.
    CHECK(pos.shared()->ingredient(vodka)->packCost == Money::fromCents(1994));
    CHECK(d.lines[0].costDifference() == Money::fromCents(6));
    CHECK(pos.costDifferences().size() == 2);
    // A reason that isn't one is refused.
    CHECK_FALSE(pos.receiveDelivery({{u"lines"_s, QVariantList{QVariantMap{{u"ingredient"_s, vodkaId}, {u"bottles"_s, 1},
                                                                           {u"reason"_s, u"lost"_s}}}}}));

    // The opening count: 13 bottles, in ounces.
    CHECK_FALSE(pos.saveBarCount({{u"lines"_s, QVariantList{QVariantMap{{u"ingredient"_s, vodkaId}, {u"count"_s, u"lots"_s}}}}}));
    REQUIRE(pos.saveBarCount({{u"lines"_s, QVariantList{QVariantMap{{u"ingredient"_s, vodkaId}, {u"count"_s, u"13"_s}}}}}));
    CHECK(onHand() == Approx(13 * core::kOz750ml));

    // Two 2oz pours, one 1.25oz voided.
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    pos.addItem(idOf(pos, "EXAMPLE VODKA 2oz"));
    pos.addItem(idOf(pos, "EXAMPLE VODKA 2oz"));
    pos.addItem(idOf(pos, "EXAMPLE VODKA 1.25oz"));
    REQUIRE(pos.sendOrder());
    CHECK(onHand() == Approx(13 * core::kOz750ml - 5.25));
    REQUIRE(pos.voidItem());
    CHECK(onHand() == Approx(13 * core::kOz750ml - 4));

    // The shelf: 12.5 bottles (the last count shown is the opening).
    const QVariantList goods = pos.barInventory()[u"goods"_s].toList();
    const auto it = std::ranges::find_if(goods, [&](const QVariant &g) { return g.toMap()[u"id"_s] == vodkaId; });
    REQUIRE(it != goods.end());
    CHECK(it->toMap()[u"lastCount"_s] == u"13"_s);
    CHECK(it->toMap()[u"size"_s] == u"750ml"_s);
    REQUIRE(pos.saveBarCount({{u"lines"_s, QVariantList{QVariantMap{{u"ingredient"_s, vodkaId}, {u"count"_s, u"12.5"_s}}}}}));
    CHECK(onHand() == Approx(12.5 * core::kOz750ml));

    // Variance: opening + received - poured - closing, in ounces and at the system cost per bottle.
    const core::Report r = pos.buildReport(u"barvariance"_s);
    CHECK(hasSection(r, "Vodka"));
    CHECK(rowStarting(r, "EXAMPLE VODKA (oz)")
          == std::vector<std::string>{"EXAMPLE VODKA (oz)", "0", "330.2", "4", "317.5", "8.7", "$6.83"});
    // Over dates (Reports, Dates...): the same counts.
    const QString today = QDate::currentDate().toString(u"yyyy-MM-dd"_s);
    REQUIRE(pos.requestRangeReport(u"barvariance"_s, u"custom"_s, today, today, false));
    bool found = false;
    for (const QVariant &v : pos.rangeReport()[u"report"_s].toMap()[u"rows"_s].toList()) {
        const QStringList c = v.toMap()[u"cells"_s].toStringList();
        if (!c.isEmpty() && c.first() == u"EXAMPLE VODKA (oz)"_s) {
            found = true;
            CHECK(c.last() == u"$6.83"_s);
        }
    }
    CHECK(found);
    REQUIRE(pos.requestRangeReport(u"barvariance"_s, u"custom"_s, u"2020-01-01"_s, u"2020-01-02"_s, false));
    CHECK(pos.rangeReport()[u"report"_s].toMap()[u"rows"_s].toList().size() == 1);   // a note: no counts then
}

TEST_CASE("Draft beer: kegs on the 18 lines, Change keg, pours from the line, keg variance, pricing aid", "[bar][draft]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    importBarMenu(pos);
    REQUIRE(pos.linkPours());

    // 18 lines, all empty to start.
    QVariantList lines = pos.draftLines()[u"lines"_s].toList();
    REQUIRE(lines.size() == 18);
    CHECK(lines.front().toMap()[u"id"_s] == u"T1-L1"_s);
    CHECK(lines.back().toMap()[u"id"_s] == u"T3-L6"_s);
    CHECK_FALSE(lines.front().toMap().contains(u"keg"_s));

    // A keg good: the beer and its size, its cost per keg.
    CHECK_FALSE(pos.addKeg({{u"beer"_s, u"EXAMPLE STOUT"_s}, {u"size"_s, u"pint"_s}}));
    REQUIRE(pos.addKeg({{u"beer"_s, u"EXAMPLE STOUT"_s}, {u"size"_s, u"half"_s}, {u"cost"_s, u"180.00"_s}}));
    CHECK_FALSE(pos.addKeg({{u"beer"_s, u"EXAMPLE STOUT"_s}, {u"size"_s, u"half"_s}}));   // there already
    const core::Ingredient *keg = good(pos, "EXAMPLE STOUT 1/2 bbl");
    REQUIRE(keg);
    CHECK(keg->kind == "keg");
    CHECK(keg->packOz == core::kOzHalfBarrel);
    CHECK(keg->cost == Money::fromCents(18000));
    const QString kegId = QString::fromStdString(keg->id);
    REQUIRE(pos.receiveDelivery({{u"lines"_s, QVariantList{QVariantMap{{u"ingredient"_s, kegId}, {u"bottles"_s, 2}}}}}));
    CHECK(keg->onHand == 2);

    // Change keg: the line, then the keg.
    CHECK_FALSE(pos.changeKeg({{u"line"_s, u"T4-L1"_s}, {u"keg"_s, kegId}}));
    CHECK_FALSE(pos.changeKeg({{u"line"_s, u"T1-L1"_s}}));   // nothing on it, nothing chosen
    REQUIRE(pos.changeKeg({{u"line"_s, u"T1-L1"_s}, {u"keg"_s, kegId}}));
    CHECK(keg->onHand == 1);   // off the shelf
    QVariantMap line = pos.draftLines()[u"lines"_s].toList().front().toMap();
    CHECK(line[u"keg"_s] == u"EXAMPLE STOUT 1/2 bbl"_s);
    CHECK(line[u"left"_s] == u"1984"_s);
    CHECK(line[u"by"_s].toString().size() > 0);

    // Pours come out of the keg on the beer's line; a void puts it back.
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    pos.addItem(idOf(pos, "EXAMPLE STOUT 16oz"));
    pos.addItem(idOf(pos, "EXAMPLE STOUT 12oz"));
    pos.addItem(idOf(pos, "EXAMPLE STOUT 16oz"));
    REQUIRE(pos.sendOrder());
    CHECK(pos.shared()->settings.draftLines.front().poured == 44);
    REQUIRE(pos.voidItem());
    CHECK(pos.shared()->settings.draftLines.front().poured == 28);
    CHECK(named(pos, "EXAMPLE STOUT 16oz")->available);

    // The pricing aid: the keg's cost per oz x the pour x 5, tax included.
    const std::int64_t ppm = pos.shared()->settings.tax.ratePpm(named(pos, "EXAMPLE STOUT 16oz")->taxClass);
    const Money target = Money::fromCents(18000).scaled(16000LL * 5 * (1'000'000 + ppm), 1'984'000LL * 1'000'000);
    const QVariantMap pricing = pos.drinkPricing(*named(pos, "EXAMPLE STOUT 16oz"));
    CHECK(pricing[u"target"_s] == pos.format(target));
    CHECK(pricing[u"pourCost"_s] == pos.format(Money::fromCents(145)));   // 180.00 x 16 / 1984
    CHECK(pricing[u"from"_s] == u"keg"_s);
    // A bottle pour: its bottle's cost per bottle.
    const QVariantMap vodka = pos.drinkPricing(*named(pos, "EXAMPLE VODKA 2oz"));
    CHECK(vodka[u"from"_s] == u"bottle"_s);
    CHECK(vodka[u"pourCost"_s] == pos.format(Money::fromCents(157)));   // 19.94 x 2 / 25.4

    // Off with 1,900 oz left (not kicked): loss = 1984 - 1900 - 28 = 56 oz; the next pours from the new keg.
    CHECK_FALSE(pos.changeKeg({{u"line"_s, u"T1-L1"_s}, {u"keg"_s, kegId}, {u"left"_s, u"2500"_s}}));
    REQUIRE(pos.changeKeg({{u"line"_s, u"T1-L1"_s}, {u"keg"_s, kegId}, {u"left"_s, u"1900"_s}}));
    REQUIRE(pos.shared()->settings.kegs.size() == 1);
    const core::KegRecord k = pos.shared()->settings.kegs.back();
    CHECK(k.line == "T1-L1");
    CHECK(k.poured == 28);
    CHECK(k.left == 1900);
    CHECK_FALSE(k.kicked);
    CHECK(k.loss() == Approx(56));
    CHECK(k.lossValue() == Money::fromCents(508));   // 180.00 x 56 / 1984
    CHECK(pos.shared()->settings.draftLines.front().poured == 0);
    CHECK(keg->onHand == 0);

    const core::Report r = pos.buildReport(u"kegs"_s);
    const auto cells = rowStarting(r, "EXAMPLE STOUT 1/2 bbl · T1-L1 · ");
    REQUIRE(cells.size() == 7);
    CHECK(cells[1] == "1984");
    CHECK(cells[2] == "28");
    CHECK(cells[3] == "1900");
    CHECK(cells[4] == "56");
    CHECK(cells[5] == "$5.08");
    CHECK(cells[6] == "66.7% !");   // over the 7% allowance
    CHECK(hasSection(r, "On tap now"));

    // Kicked: nothing left; the allowance is a setting.
    CHECK_FALSE(pos.setKegAllowance(80));
    REQUIRE(pos.setKegAllowance(5));
    REQUIRE(pos.changeKeg({{u"line"_s, u"T1-L1"_s}, {u"keg"_s, QString()}, {u"kicked"_s, true}}));
    CHECK(pos.shared()->settings.kegs.back().kicked);
    CHECK(pos.shared()->settings.draftLines.front().kegId.empty());

    // Kept with the settings.
    const core::PosSettings back = app::settingsFromJson(app::toJson(pos.shared()->settings));
    CHECK(back.draftLines == pos.shared()->settings.draftLines);
    CHECK(back.kegs == pos.shared()->settings.kegs);
    CHECK(back.kegAllowancePercent == 5);
    CHECK(app::ingredientFromJson(app::toJson(*keg)) == *keg);
    CHECK(app::ingredientFromJson(app::toJson(*good(pos, "EXAMPLE VODKA"))) == *good(pos, "EXAMPLE VODKA"));
}
