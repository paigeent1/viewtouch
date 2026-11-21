#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// The bar menu (owner 2026-11-21): the Bar index's tiles from the menu data, one button per bottle with a
// pour picker, and the mixer offered after a spirit. Made-up drinks (a store's own drink sheet holds its
// costs and vendors: never in the repo).

namespace {

QString idOf(const PosService &pos, const std::string &name)
{
    for (const core::MenuItem &m : pos.shared()->menu)
        if (m.name == name)
            return QString::fromStdString(m.id);
    FAIL("no item " << name);
    return {};
}

QVariantMap itemEntry(const PosService &pos, const QString &id)
{
    for (const QVariant &v : pos.menuItems())
        if (v.toMap()[u"id"_s].toString() == id)
            return v.toMap();
    return {};
}

QVariantMap categoryEntry(const PosService &pos, const QString &name)
{
    for (const QVariant &v : pos.menuCategories())
        if (v.toMap()[u"name"_s].toString() == name)
            return v.toMap();
    return {};
}

QVariantMap row(const char *name, double price, const char *category, const char *group, const char *pour,
                const char *abv)
{
    QVariantMap r{{u"name"_s, QString::fromLatin1(name)}, {u"price"_s, price},
                  {u"category"_s, QString::fromLatin1(category)}, {u"tax"_s, u"Alcohol"_s},
                  {u"productGroup"_s, QString::fromLatin1(group)}};
    if (*pour)
        r.insert(u"pourOz"_s, QString::fromLatin1(pour));
    if (*abv)
        r.insert(u"abv"_s, QString::fromLatin1(abv));
    else
        r.insert(u"tax"_s, u"Food"_s);
    return r;
}

void importBar(PosService &pos)
{
    const QVariantList rows{
        row("SAMPLE BOURBON 1.25oz", 9.00, "Liquor", "Bourbon", "1.25", "45"),
        row("SAMPLE BOURBON 2oz", 13.00, "Liquor", "Bourbon", "2", "45"),
        row("SAMPLE SCOTCH 2oz", 15.00, "Liquor", "Scotch", "2", "43"),
        row("SAMPLE GIN 1.25oz", 8.00, "Liquor", "Gin", "1.25", "40"),
        row("SAMPLE GIN 2oz", 12.00, "Liquor", "Gin", "2", "40"),
        row("SAMPLE LAGER", 6.00, "Bottles & Cans", "", "12", "5"),
        row("SAMPLE GINGER BEER", 4.00, "NA / Mixers", "", "", ""),
    };
    REQUIRE(pos.importMenuRows(rows, QString(), false) == rows.size());
}

// The Mixers group a store fills in (Manager -> Modifiers), and its notes.
void giveMixers(PosService &pos, bool priced = true)
{
    core::PosSettings &s = pos.shared()->settings;
    std::erase_if(s.modifierGroups, [](const core::ModifierGroup &g) { return g.id == "mixers" || g.id == "bar-notes"; });
    s.barMenu.groupsSeeded = false;
    REQUIRE(PosService::seedBarGroups(s));
    REQUIRE_FALSE(PosService::seedBarGroups(s));   // once only
    for (core::ModifierGroup &g : s.modifierGroups)
        if (g.id == "mixers" && priced)
            for (core::ModifierOption &o : g.options)
                if (o.name == "Tonic")
                    o.price = Money::fromCents(150);
}

} // namespace

TEST_CASE("Bar menu: the Bar index's tiles come from the data", "[bar][barmenu]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    importBar(pos);

    const QVariantMap liquor = categoryEntry(pos, u"Liquor"_s);
    REQUIRE_FALSE(liquor.isEmpty());
    CHECK(liquor[u"bar"_s].toBool());
    // Split into its product groups, in the menu's order.
    QStringList groups;
    for (const QVariant &g : liquor[u"barGroups"_s].toList())
        groups << g.toMap()[u"name"_s].toString();
    CHECK(groups == QStringList{u"Bourbon"_s, u"Scotch"_s, u"Gin"_s});
    // One product group (or none): one tile, the category's.
    const QVariantMap beer = categoryEntry(pos, u"Bottles & Cans"_s);
    CHECK(beer[u"bar"_s].toBool());
    CHECK(beer[u"barGroups"_s].toList().isEmpty());
    // Not a drink and not printed at the bar: not on the Bar index.
    CHECK_FALSE(categoryEntry(pos, u"NA / Mixers"_s)[u"bar"_s].toBool());
    // A new drink in a new category shows up by itself.
    REQUIRE(pos.importMenuRows({row("SAMPLE MERLOT Glass", 10.00, "Wine", "", "5", "13")}, QString(), false) == 1);
    CHECK(categoryEntry(pos, u"Wine"_s)[u"bar"_s].toBool());
    // The restaurant's food is not.
    for (const QVariant &v : pos.menuCategories()) {
        const QVariantMap c = v.toMap();
        if (c[u"name"_s].toString() == u"Burgers"_s)
            CHECK_FALSE(c[u"bar"_s].toBool());
    }
}

TEST_CASE("Bar menu: one button per bottle; a bottle with one pour rings directly", "[bar][barmenu]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    importBar(pos);

    // Not linked yet: grouped by the name without its size.
    const QVariantMap small = itemEntry(pos, idOf(pos, "SAMPLE BOURBON 1.25oz"));
    const QVariantMap big = itemEntry(pos, idOf(pos, "SAMPLE BOURBON 2oz"));
    CHECK_FALSE(small[u"bottle"_s].toString().isEmpty());
    CHECK(small[u"bottle"_s] == big[u"bottle"_s]);
    CHECK(small[u"bottleName"_s] == u"SAMPLE BOURBON"_s);
    CHECK(small[u"pourName"_s] == u"1.25 oz"_s);
    CHECK(big[u"pourName"_s] == u"2 oz"_s);
    CHECK(small[u"priceCents"_s].toLongLong() == 900);
    CHECK(small[u"productGroup"_s] == u"Bourbon"_s);
    CHECK(small[u"bottle"_s] != itemEntry(pos, idOf(pos, "SAMPLE GIN 2oz"))[u"bottle"_s]);
    // One pour of its bottle: no group, its own button (rung at once).
    CHECK(itemEntry(pos, idOf(pos, "SAMPLE SCOTCH 2oz"))[u"bottle"_s].toString().isEmpty());
    CHECK(itemEntry(pos, idOf(pos, "SAMPLE LAGER"))[u"bottle"_s].toString().isEmpty());

    // Linked to their bottles (Bar & Kegs -> Link pours): grouped by the bottle they pour from.
    REQUIRE(pos.linkPours());
    const QVariantMap linked = itemEntry(pos, idOf(pos, "SAMPLE BOURBON 1.25oz"));
    CHECK(linked[u"bottle"_s].toString().startsWith(u"g:"_s));
    CHECK(linked[u"bottle"_s] == itemEntry(pos, idOf(pos, "SAMPLE BOURBON 2oz"))[u"bottle"_s]);
    CHECK(linked[u"bottleName"_s] == u"SAMPLE BOURBON"_s);

    // A pour from the picker rings that item exactly as its own button: its price, with its notes, free.
    giveMixers(pos);
    REQUIRE(pos.orderPour(idOf(pos, "SAMPLE BOURBON 2oz"), {u"Rocks"_s, u"Twist"_s, u"Not a note"_s}));
    const core::OrderLine &l = pos.currentCheck()->lines.back();
    CHECK(l.name == "SAMPLE BOURBON 2oz");
    CHECK(l.unitPrice == Money::fromCents(1300));
    REQUIRE(l.modifiers.size() == 2);
    CHECK(l.modifiers[0].name == "Rocks");
    CHECK(l.modifiers[1].name == "Twist");
    CHECK(l.total() == Money::fromCents(1300));
    CHECK(l.stdDrinks > 0);   // standard drinks counted as ever
}

TEST_CASE("Bar menu: a spirit asks for its mixer, added and charged; none when the group is empty", "[bar][barmenu]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    importBar(pos);

    SECTION("no Mixers group (or an empty one): never asked") {
        std::erase_if(pos.shared()->settings.modifierGroups, [](const core::ModifierGroup &g) { return g.id == "mixers"; });
        REQUIRE(pos.addItem(idOf(pos, "SAMPLE GIN 2oz")));
        CHECK_FALSE(pos.mixerAsk()[u"active"_s].toBool());
        pos.shared()->settings.modifierGroups.push_back({"mixers", "Mixers", 0, 1, {}, false, false});
        REQUIRE(pos.addItem(idOf(pos, "SAMPLE GIN 2oz")));
        CHECK_FALSE(pos.mixerAsk()[u"active"_s].toBool());
    }

    SECTION("a spirit asks; beer doesn't; the mixer goes on the drink at its price") {
        giveMixers(pos);
        REQUIRE(pos.addItem(idOf(pos, "SAMPLE LAGER")));   // 5%: no
        CHECK_FALSE(pos.mixerAsk()[u"active"_s].toBool());
        REQUIRE(pos.addItem(idOf(pos, "SAMPLE GIN 2oz")));   // 40%: yes
        const QVariantMap ask = pos.mixerAsk();
        REQUIRE(ask[u"active"_s].toBool());
        CHECK(ask[u"item"_s] == u"SAMPLE GIN 2oz"_s);
        int tonic = -1;
        const QVariantList mixers = ask[u"mixers"_s].toList();
        for (const QVariant &v : mixers)
            if (v.toMap()[u"name"_s] == u"Tonic"_s)
                tonic = v.toMap()[u"index"_s].toInt();
        REQUIRE(tonic >= 0);
        CHECK(mixers[tonic].toMap()[u"price"_s] == pos.format(Money::fromCents(150)));
        CHECK_FALSE(ask[u"notes"_s].toList().isEmpty());

        REQUIRE(pos.answerMixer(tonic, {u"Up"_s}));
        CHECK_FALSE(pos.mixerAsk()[u"active"_s].toBool());
        const core::OrderLine &l = pos.currentCheck()->lines.back();
        REQUIRE(l.modifiers.size() == 2);
        CHECK(l.modifiers[0].name == "Tonic");
        CHECK(l.modifiers[0].group == "mixers");
        CHECK(l.modifiers[1].name == "Up");
        CHECK(l.total() == Money::fromCents(1200 + 150));   // the gin and the tonic
        // Answered: asking again is refused.
        CHECK_FALSE(pos.answerMixer(tonic, {}));
    }

    SECTION("No mixer: the drink as rung") {
        giveMixers(pos);
        REQUIRE(pos.addItem(idOf(pos, "SAMPLE GIN 1.25oz")));
        REQUIRE(pos.mixerAsk()[u"active"_s].toBool());
        REQUIRE(pos.answerMixer(-1, {}));
        CHECK(pos.currentCheck()->lines.back().modifiers.empty());
        CHECK(pos.currentCheck()->lines.back().total() == Money::fromCents(800));
    }

    SECTION("which drinks ask is the store's: categories listed, else the ABV") {
        giveMixers(pos);
        pos.shared()->settings.barMenu.mixerMinAbv = 44;
        REQUIRE(pos.addItem(idOf(pos, "SAMPLE GIN 2oz")));   // 40%: under 44
        CHECK_FALSE(pos.mixerAsk()[u"active"_s].toBool());
        REQUIRE(pos.addItem(idOf(pos, "SAMPLE BOURBON 2oz")));   // 45%
        CHECK(pos.mixerAsk()[u"active"_s].toBool());
        // Another item rung: the ask is over.
        REQUIRE(pos.addItem(idOf(pos, "SAMPLE LAGER")));
        CHECK_FALSE(pos.mixerAsk()[u"active"_s].toBool());
        // Only the beer's category listed: the beer asks, the bourbon doesn't.
        const core::MenuItem *lager = pos.findItem(idOf(pos, "SAMPLE LAGER"));
        REQUIRE(lager);
        pos.shared()->settings.barMenu.mixerCategories = {lager->family};
        REQUIRE(pos.addItem(idOf(pos, "SAMPLE BOURBON 2oz")));
        CHECK_FALSE(pos.mixerAsk()[u"active"_s].toBool());
        REQUIRE(pos.addItem(idOf(pos, "SAMPLE LAGER")));
        CHECK(pos.mixerAsk()[u"active"_s].toBool());
    }
}

TEST_CASE("Bar menu: settings saved and read back", "[bar][barmenu]")
{
    core::PosSettings s;
    s.barMenu.mixerGroup = "spirits-mixers";
    s.barMenu.notesGroup = "";
    s.barMenu.mixerMinAbv = 30;
    s.barMenu.mixerCategories = {"vodka", "rum"};
    s.barMenu.groupsSeeded = true;
    const core::PosSettings back = app::settingsFromJson(app::toJson(s));
    CHECK(back.barMenu == s.barMenu);
    // Settings from before the bar menu: the usual groups and 20%, not seeded yet.
    const core::PosSettings old = app::settingsFromJson(QJsonObject{});
    CHECK(old.barMenu.mixerGroup == "mixers");
    CHECK(old.barMenu.notesGroup == "bar-notes");
    CHECK(old.barMenu.mixerMinAbv == 20);
    CHECK_FALSE(old.barMenu.groupsSeeded);
}
