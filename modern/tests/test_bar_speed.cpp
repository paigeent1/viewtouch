#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QSignalSpy>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// The bar's speed (owner 2026-11-21): the Draft page (a pour from a draft line), Put draft beers on
// empty lines, REPEAT ROUND / SAME AGAIN, best sellers, sold-out items refused. Made-up beers.

namespace {

QString idOf(const PosService &pos, const std::string &name)
{
    for (const core::MenuItem &m : pos.shared()->menu)
        if (m.name == name)
            return QString::fromStdString(m.id);
    FAIL("no item " << name);
    return {};
}

const core::DraftLine *lineOf(const PosService &pos, const std::string &id)
{
    for (const core::DraftLine &l : pos.shared()->settings.draftLines)
        if (l.id == id)
            return &l;
    return nullptr;
}

QVariantMap draftRow(const char *name, const char *price, const char *pour)
{
    return {{u"name"_s, QString::fromLatin1(name)}, {u"price"_s, QString::fromLatin1(price).toDouble()},
            {u"category"_s, u"Draft"_s}, {u"tax"_s, u"Alcohol"_s}, {u"pourOz"_s, QString::fromLatin1(pour)},
            {u"abv"_s, u"5"_s}};
}

// Three made-up draft beers, each as 12oz and 16oz.
void importDrafts(PosService &pos)
{
    const QVariantList rows{
        draftRow("TEST ALE 12oz", "6.00", "12"),   draftRow("TEST ALE 16oz", "8.00", "16"),
        draftRow("TEST PILS 12oz", "5.00", "12"),  draftRow("TEST PILS 16oz", "7.00", "16"),
        draftRow("TEST PORTER 12oz", "7.00", "12"), draftRow("TEST PORTER 16oz", "9.00", "16"),
    };
    REQUIRE(pos.importMenuRows(rows, QString(), false) == rows.size());
}

QStringList unsentNames(const PosService &pos)
{
    QStringList out;
    for (const QVariant &v : pos.lines())
        if (!v.toMap()[u"sent"_s].toBool())
            out << v.toMap()[u"name"_s].toString();
    return out;
}

} // namespace

TEST_CASE("Put draft beers on empty lines: links pours, makes 1/2 bbl kegs, fills T1-L1 onward; undo", "[bar][draft][speed]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    importDrafts(pos);

    // The preview before anything is linked: the three beers, the next empty lines, new kegs.
    QVariantList plan = pos.draftLines()[u"fill"_s].toList();
    REQUIRE(plan.size() == 3);
    CHECK(plan[0].toMap()[u"line"_s] == u"T1-L1"_s);
    CHECK(plan[0].toMap()[u"newKeg"_s].toBool());

    // One beer already on a line stays where it is; the others go on the empty lines around it.
    REQUIRE(pos.linkPours());
    REQUIRE(pos.addKeg({{u"beer"_s, u"TEST PILS"_s}, {u"size"_s, u"sixth"_s}}));
    QString pilsKeg;
    for (const core::Ingredient &g : pos.shared()->ingredients)
        if (g.kind == "keg" && g.beer == "TEST PILS")
            pilsKeg = QString::fromStdString(g.id);
    REQUIRE(pos.changeKeg({{u"line"_s, u"T1-L2"_s}, {u"keg"_s, pilsKeg}}));
    plan = pos.draftLines()[u"fill"_s].toList();
    REQUIRE(plan.size() == 2);

    REQUIRE(pos.fillDraftLines());
    REQUIRE(lineOf(pos, "T1-L1"));
    CHECK(lineOf(pos, "T1-L1")->beer == "TEST ALE");
    CHECK(lineOf(pos, "T1-L1")->kegOz == core::kOzHalfBarrel);
    CHECK(lineOf(pos, "T1-L1")->kegCost.cents() == 0);           // left for the manager
    CHECK_FALSE(lineOf(pos, "T1-L1")->tappedBy.empty());         // who did it, as Change keg
    CHECK(lineOf(pos, "T1-L2")->beer == "TEST PILS");            // left alone
    CHECK(lineOf(pos, "T1-L3")->beer == "TEST PORTER");
    CHECK((!lineOf(pos, "T1-L4") || lineOf(pos, "T1-L4")->kegId.empty()));
    CHECK(pos.draftLines()[u"fill"_s].toList().isEmpty());      // nothing more to do
    CHECK_FALSE(pos.fillDraftLines());

    // Undo: the two lines empty again, their new kegs gone; T1-L2 as it was.
    REQUIRE(pos.undoFillDraftLines());
    CHECK(lineOf(pos, "T1-L1")->kegId.empty());
    CHECK(lineOf(pos, "T1-L3")->kegId.empty());
    CHECK(lineOf(pos, "T1-L2")->beer == "TEST PILS");
    int kegs = 0;
    for (const core::Ingredient &g : pos.shared()->ingredients)
        kegs += g.kind == "keg";
    CHECK(kegs == 1);
}

TEST_CASE("Draft page: a line rings its beer's size and pours from its keg; empty, kicked, wrong item refused", "[bar][draft][speed]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    importDrafts(pos);
    REQUIRE(pos.fillDraftLines());   // ALE T1-L1, PILS T1-L2, PORTER T1-L3

    // Each line lists the sizes that pour from it, smallest first.
    const QVariantMap t1 = pos.draftLines()[u"lines"_s].toList().front().toMap();
    const QVariantList sizes = t1[u"items"_s].toList();
    REQUIRE(sizes.size() == 2);
    CHECK(sizes[0].toMap()[u"id"_s] == idOf(pos, "TEST ALE 12oz"));
    CHECK(sizes[1].toMap()[u"oz"_s].toDouble() == 16.0);
    CHECK_FALSE(t1[u"kicked"_s].toBool());

    REQUIRE(pos.startCheck(core::CheckType::Quick));
    REQUIRE(pos.orderDraft(u"T1-L1"_s, idOf(pos, "TEST ALE 16oz")));
    CHECK(unsentNames(pos) == QStringList{u"TEST ALE 16oz"_s});
    CHECK(pos.lines().last().toMap()[u"name"_s] == u"TEST ALE 16oz"_s);
    // Refused: an empty line, a line that doesn't pour that beer, a line that isn't there.
    CHECK_FALSE(pos.orderDraft(u"T2-L1"_s, idOf(pos, "TEST ALE 12oz")));
    CHECK_FALSE(pos.orderDraft(u"T1-L2"_s, idOf(pos, "TEST ALE 12oz")));
    CHECK_FALSE(pos.orderDraft(u"T9-L9"_s, idOf(pos, "TEST ALE 12oz")));
    CHECK(pos.lines().size() == 1);

    // The keg depletes as before: the pour comes out of the ALE's line when sent.
    REQUIRE(pos.sendOrder());
    CHECK(lineOf(pos, "T1-L1")->poured == 16.0);
    // Standard drinks frozen on the line, as any drink.
    double drinks = 0;
    for (const auto &[id, c] : pos.shared()->open)
        for (const core::OrderLine &l : c.lines)
            drinks += l.stdDrinks;
    CHECK(drinks > 1.0);   // 16 oz at 5%: 1.33

    // Sold out: refused here as on the menu.
    REQUIRE(pos.setAvailable(idOf(pos, "TEST ALE 12oz"), false));
    CHECK_FALSE(pos.orderDraft(u"T1-L1"_s, idOf(pos, "TEST ALE 12oz")));
    CHECK_FALSE(pos.addItem(idOf(pos, "TEST ALE 12oz")));

    // Kicked: all of the keg rung up.
    for (core::DraftLine &l : pos.shared()->settings.draftLines)
        if (l.id == "T1-L1")
            l.poured = l.kegOz;
    CHECK(pos.draftLines()[u"lines"_s].toList().front().toMap()[u"kicked"_s].toBool());
    CHECK_FALSE(pos.orderDraft(u"T1-L1"_s, idOf(pos, "TEST ALE 16oz")));
}

TEST_CASE("REPEAT ROUND and SAME AGAIN ring the right drinks with their choices", "[bar][speed][round]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    CHECK_FALSE(pos.sameAgain());     // no drink yet
    CHECK_FALSE(pos.repeatRound());   // nothing sent yet

    pos.addItem(u"soda"_s);
    REQUIRE(pos.chooseOption(u"drink-size"_s, 1));
    REQUIRE(pos.finishChoosing());
    pos.addItem(u"draft-beer"_s);
    REQUIRE(pos.chooseOption(u"draft"_s, 0));
    REQUIRE(pos.finishChoosing());
    REQUIRE(pos.setLineQuantity(0, 2));   // two beers
    pos.addItem(u"bacon-burger"_s);       // not a drink
    REQUIRE(pos.sendOrder());
    const QVariantList sent = pos.lines();

    // REPEAT ROUND: the soda and the two beers again (not the burger), each with its choice.
    QSignalSpy notices(&pos, &app::PosSession::notice);
    REQUIRE(pos.repeatRound());
    QStringList again = unsentNames(pos);
    REQUIRE(again.size() == 2);
    CHECK(again == QStringList{sent[0].toMap()[u"name"_s].toString(), sent[1].toMap()[u"name"_s].toString()});
    const QVariantList now = pos.lines();
    CHECK(now.last().toMap()[u"quantity"_s].toInt() == 2);
    CHECK(now.last().toMap()[u"modifiers"_s].toList().size() == sent[1].toMap()[u"modifiers"_s].toList().size());
    CHECK_FALSE(now.last().toMap()[u"modifiers"_s].toList().isEmpty());
    REQUIRE_FALSE(notices.isEmpty());
    CHECK(notices.last().first().toString().startsWith(u"Repeated 3 drink"_s));

    // SAME AGAIN: the newest drink (the beers just repeated), one more, same pour.
    const int before = int(pos.lines().size());
    REQUIRE(pos.sameAgain());
    REQUIRE(int(pos.lines().size()) == before + 1);
    const QVariantMap last = pos.lines().last().toMap();
    CHECK(last[u"name"_s] == sent[1].toMap()[u"name"_s]);
    CHECK(last[u"quantity"_s].toInt() == 1);
    CHECK(last[u"modifiers"_s].toList().size() == sent[1].toMap()[u"modifiers"_s].toList().size());

    // Sold out: SAME AGAIN refuses it; REPEAT ROUND skips it.
    REQUIRE(pos.setAvailable(u"draft-beer"_s, false));
    CHECK_FALSE(pos.sameAgain());
    REQUIRE(pos.sendOrder());
    REQUIRE(pos.repeatRound());
    CHECK(unsentNames(pos).size() == 1);   // the soda only

    // Best sellers: what sold, most first.
    const QStringList top = pos.topSellers();
    REQUIRE_FALSE(top.isEmpty());
    CHECK(top.front() == u"draft-beer"_s);
}

TEST_CASE("A tap list of one pour per beer: Link pours finds each beer; lines filled in import order", "[bar][draft][speed]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    // As a real tap list: one serving per beer, most 16oz, a few 12oz (made-up names).
    const QVariantList rows{
        draftRow("ZULU LAGER 16oz", "8.00", "16"), draftRow("ALPHA SOUR 12oz", "8.00", "12"),
        draftRow("MIKE IPA 16oz", "8.00", "16"),   draftRow("BRAVO CIDER 12oz", "7.00", "12"),
    };
    REQUIRE(pos.importMenuRows(rows, QString(), false) == rows.size());
    REQUIRE(pos.fillDraftLines());
    // The beer is the name less its size; the first imported goes on T1-L1, the next on T1-L2...
    CHECK(lineOf(pos, "T1-L1")->beer == "ZULU LAGER");
    CHECK(lineOf(pos, "T1-L2")->beer == "ALPHA SOUR");
    CHECK(lineOf(pos, "T1-L3")->beer == "MIKE IPA");
    CHECK(lineOf(pos, "T1-L4")->beer == "BRAVO CIDER");
    CHECK((!lineOf(pos, "T1-L5") || lineOf(pos, "T1-L5")->kegId.empty()));
    // Each line has its one pour, which rings from it.
    const QVariantList lines = pos.draftLines()[u"lines"_s].toList();
    const QVariantList sour = lines[1].toMap()[u"items"_s].toList();
    REQUIRE(sour.size() == 1);
    CHECK(sour[0].toMap()[u"id"_s] == idOf(pos, "ALPHA SOUR 12oz"));
    CHECK(sour[0].toMap()[u"oz"_s].toDouble() == 12.0);
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    REQUIRE(pos.orderDraft(u"T1-L2"_s, idOf(pos, "ALPHA SOUR 12oz")));
    REQUIRE(pos.sendOrder());
    CHECK(lineOf(pos, "T1-L2")->poured == 12.0);
    CHECK(lineOf(pos, "T1-L1")->poured == 0.0);
}
