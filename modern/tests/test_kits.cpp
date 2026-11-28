#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "print/document.hh"
#include "print/tickets.hh"
#include "qt_catch.hh"

#include <QDateTime>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Sampler kits (app/pos_kits.cpp, core/kit.hh): made on Mass Updates > Sampler Kits from a kit item and its
// contents; its cost becomes the contents'; when it sells, the contents leave stock, the sale keeps what was in
// it and the receipt prints it. And Mix-Match at the register (the System Discounts screen's other kind): the
// cheapest units get the changed price. Made-up items only.

namespace {

constexpr const char *kRobusto = "077777000014";
constexpr const char *kToro = "077777000038";
constexpr const char *kKit = "077777000045";

core::MenuItem retailItem(const char *id, const char *name, const char *upc, std::int64_t price, double cost)
{
    core::MenuItem m;
    m.id = id;
    m.name = name;
    m.taxClass = core::TaxClass::Merchandise;
    m.barcodes = {upc};
    m.price = Money::fromCents(price);
    m.kitchenHide = true;
    m.recipe = {{"__retail__", 1.0}};
    if (cost > 0)
        m.recipe.push_back({"__cost__", cost});
    return m;
}

app::PosData store()
{
    app::PosData d = test::seedPosData();
    d.settings.askReasons = false;
    d.settings.tax.merchandisePpm = 63500;
    d.settings.tax.cashRoundingCents = 0;
    d.menu.push_back(retailItem("robusto", "ACME ROBUSTO", kRobusto, 999, 5.00));
    d.menu.push_back(retailItem("toro", "ACME TORO", kToro, 1199, 6.00));
    d.menu.push_back(retailItem("sampler", "ACME 5 CIGAR SAMPLER", kKit, 3999, 0));
    return d;
}

std::int64_t at(int y, int mo, int d) { return QDateTime(QDate(y, mo, d), QTime(10, 0)).toMSecsSinceEpoch(); }

const core::Check &current(PosService &pos) { return pos.shared()->open.at(pos.checkInfo()[u"id"_s].toLongLong()); }

} // namespace

TEST_CASE("Sampler kits: made from a kit item, its cost the contents', contents leave stock when it sells", "[kits]")
{
    PosService pos(store(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.retailSetCount(u"robusto"_s, 30, 0));
    REQUIRE(pos.retailSetCount(u"toro"_s, 20, 0));
    REQUIRE(pos.kitsOpen(true));
    REQUIRE(pos.kitFind(u"sampler"_s));
    REQUIRE(pos.kits()[u"found"_s].toList().size() == 1);
    REQUIRE(pos.kitEdit(u"sampler"_s));
    CHECK_FALSE(pos.kitApply());                        // nothing in it yet
    CHECK_FALSE(pos.kitAddParts({u"sampler"_s}));       // not itself
    REQUIRE(pos.kitAddParts({QString::fromLatin1(kRobusto)}));
    REQUIRE(pos.kitAddParts({QString::fromLatin1(kToro)}));
    REQUIRE(pos.kitSetQty(QString::fromLatin1(kRobusto), u"3"_s));
    REQUIRE(pos.kitSetQty(QString::fromLatin1(kToro), u"2"_s));
    REQUIRE(pos.kitSetPrint(true));
    QVariantMap draft = pos.kits()[u"draft"_s].toMap();
    CHECK(draft[u"value"_s] == u"$53.95"_s);   // 3 x $9.99 + 2 x $11.99 alone
    CHECK(draft[u"cost"_s] == u"$27.00"_s);
    REQUIRE(pos.kitApply());
    const core::MenuItem *kit = nullptr;
    for (const core::MenuItem &m : pos.shared()->menu)
        if (m.id == "sampler")
            kit = &m;
    REQUIRE(kit);
    CHECK(core::kit::isKit(*kit));
    CHECK(core::kit::partsOf(*kit) == std::vector<core::kit::Part>{{kRobusto, 3}, {kToro, 2}});
    CHECK(core::recipeValue(*kit, "__cost__") == 27.0);
    CHECK(pos.kits()[u"list"_s].toList().size() == 1);

    // Sold: 3 robustos and 2 toros leave stock; the sale keeps what was in it; the receipt prints it.
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    REQUIRE(pos.addItem(u"sampler"_s));
    REQUIRE(pos.sendOrder());
    CHECK(pos.retailOnHand("robusto") == 27.0);
    CHECK(pos.retailOnHand("toro") == 18.0);
    const core::OrderLine &line = current(pos).lines.front();
    CHECK(line.kitContents == std::vector<std::string>{"3 x ACME ROBUSTO", "2 x ACME TORO"});
    CHECK(line.kitPrint);
    CHECK(line.unitCost.cents() == 2700);
    const print::TicketContext ctx{pos.shared()->settings, [](std::int64_t) { return std::string("Nov 27"); },
                                   [](std::int64_t) { return std::string("10:00 AM"); }, 0};
    const std::string r = print::renderText(print::receipt(current(pos), ctx), 42);
    CHECK(r.find("3 x ACME ROBUSTO") != std::string::npos);
    CHECK(r.find("2 x ACME TORO") != std::string::npos);
    // Kept through the check's JSON.
    const std::optional<core::Check> back = app::checkFromJson(app::toJson(current(pos)));
    REQUIRE(back);
    CHECK(back->lines.front().kitContents == line.kitContents);
    // Voided: back on hand.
    REQUIRE(pos.voidItem());
    CHECK(pos.retailOnHand("robusto") == 30.0);
    CHECK(pos.retailOnHand("toro") == 20.0);

    // Not a kit any more: the item stays, its contents come off.
    REQUIRE(pos.kitUnmake(u"sampler"_s));
    CHECK(pos.kits()[u"list"_s].toList().isEmpty());
}

TEST_CASE("Mix-Match at the register: buy one get one, the cheaper one free", "[kits][sysdisc]")
{
    PosService pos(store(), nullptr);
    pos.shared()->setClock([] { return at(2026, 11, 27); });
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.sysDiscOpen(true));
    REQUIRE(pos.sysDiscNew(u"mixmatch"_s));
    REQUIRE(pos.sysDiscSet({{u"name"_s, u"ACME BOGO"_s}, {u"mixStart"_s, u"2"_s}, {u"mixCount"_s, u"1"_s},
                            {u"mixHow"_s, u"percent"_s}, {u"mixAmount"_s, u"100"_s}, {u"mixText"_s, u"BUY ONE GET ONE"_s}}));
    REQUIRE(pos.sysDiscAddItems({QString::fromLatin1(kRobusto), QString::fromLatin1(kToro)}));
    CHECK(pos.systemDiscounts()[u"draft"_s].toMap()[u"level1"_s] == u"Item 2: 1 at 100% off"_s);
    REQUIRE(pos.sysDiscApply());
    REQUIRE(pos.shared()->settings.systemDiscounts.size() == 1);
    CHECK(pos.shared()->settings.systemDiscounts[0].kind == "mixmatch");

    REQUIRE(pos.startCheck(core::CheckType::Quick));
    REQUIRE(pos.addItem(u"toro"_s));
    CHECK(pos.lines()[0].toMap()[u"sysDiscount"_s].toString().isEmpty());   // one: nothing yet
    REQUIRE(pos.addItem(u"robusto"_s));
    const QVariantList lines = pos.lines();
    CHECK(lines[0].toMap()[u"sysDiscount"_s].toString().isEmpty());
    CHECK(lines[1].toMap()[u"sysDiscount"_s] == u"BUY ONE GET ONE"_s);
    CHECK(lines[1].toMap()[u"sysDiscountPrice"_s] == u"-$9.99"_s);
    CHECK(current(pos).totals(pos.shared()->settings.tax).discounts.cents() == 999);
}
