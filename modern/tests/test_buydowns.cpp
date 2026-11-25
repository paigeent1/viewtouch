#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "core/report.hh"
#include "core/scan_data.hh"
#include "pos_fixture.hh"
#include "print/document.hh"
#include "print/tickets.hh"
#include "qt_catch.hh"

#include <QDateTime>
#include <QJsonDocument>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Manufacturer buydowns and promotions at the store (app/pos_buydowns.cpp, core/buydown.hh): set up on Mass
// Updates > Buydowns / Promotions (admins, or an admin's PIN), rung as their own line under the item, carried
// through quantity changes, error corrects and returns, counted against a capped style when checks close, in
// the reports, on the receipt and in Price Check; the old per-item tags made into definitions; a definition as
// a file for another store. Made-up items only.

namespace {

constexpr const char *kCarton = "012345000016";
constexpr const char *kPack = "012345000023";

std::int64_t at(int y, int mo, int d, int h = 10, int mi = 0) { return QDateTime(QDate(y, mo, d), QTime(h, mi)).toMSecsSinceEpoch(); }

app::PosData store(bool withDefinition = true)
{
    app::PosData d = test::seedPosData();
    d.settings.tax.merchandisePpm = 63500;   // 6.35%
    d.settings.tax.cashRoundingCents = 0;
    core::Employee admin;
    admin.id = "admin-1";
    admin.name = "Alex (Admin)";
    admin.role = "admin";
    admin.pinSalt = "salt";
    admin.pinHash = app::hashPin(u"9999"_s, admin.pinSalt);
    d.employees.push_back(admin);
    core::MenuItem carton;
    carton.id = "sonoma-ctn";
    carton.name = "SONOMA FF BOX CTN";
    carton.price = Money::fromCents(11479);
    carton.taxClass = core::TaxClass::Merchandise;
    carton.barcodes = {kCarton};
    carton.kitchenHide = true;
    carton.recipe = {{"__retail__", 1.0}};
    core::MenuItem pack = carton;
    pack.id = "sonoma-pk";
    pack.name = "SONOMA FF BOX";
    pack.price = Money::fromCents(1148);
    pack.barcodes = {kPack};
    pack.recipe = {{"__retail__", 1.0}, {std::string("__parent__:") + kCarton, 10}};
    d.menu.push_back(carton);
    d.menu.push_back(pack);
    if (withDefinition) {
        core::Buydown b;
        b.id = "bd-1";
        b.number = 1;
        b.name = "SONOMA 2026Q4";
        b.receiptText = "SONOMA SAVINGS!";
        b.amount = Money::fromCents(410);
        b.start = "2026-10-01 00:00";
        b.end = "2026-12-31 23:00";
        b.manufacturer = "SONOMA";
        b.items = {{kCarton, "sonoma-ctn", 0.0, "SONOMA FF BOX CTN"}, {kPack, "sonoma-pk", 0.1, "SONOMA FF BOX"}};
        d.settings.buydowns.push_back(b);
    }
    return d;
}

const core::Check &current(PosService &pos)
{
    return pos.shared()->open.at(pos.checkInfo()[u"id"_s].toLongLong());
}

void openDrawer(PosService &pos)
{
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
}

} // namespace

TEST_CASE("Buydowns at the register: a pack and a carton get their lines, taxed on the reduced price or before",
          "[buydowns]")
{
    for (const bool before : {false, true}) {
        app::PosData d = store();
        d.settings.buydownTaxBefore = before;
        PosService pos(std::move(d), nullptr);
        std::int64_t clock = at(2026, 11, 25);
        pos.shared()->setClock([&] { return clock; });
        REQUIRE(pos.loginWithPin(u"1234"_s));
        REQUIRE(pos.noteScan(QString::fromLatin1(kPack), u"sonoma-pk"_s, true));
        REQUIRE(pos.addItem(u"sonoma-pk"_s));
        REQUIRE(pos.addItem(u"sonoma-ctn"_s));
        const QVariantList lines = pos.lines();
        REQUIRE(lines.size() == 2);
        CHECK(lines[0].toMap()[u"price"_s] == u"$11.48"_s);   // the item at its regular price
        CHECK(lines[0].toMap()[u"buydown"_s] == u"SONOMA SAVINGS!"_s);
        CHECK(lines[0].toMap()[u"buydownPrice"_s] == u"-$0.41"_s);
        CHECK(lines[1].toMap()[u"buydownPrice"_s] == u"-$4.10"_s);
        const core::Totals t = current(pos).totals(pos.shared()->settings.tax);
        CHECK(t.buydowns.cents() == 451);
        CHECK(t.subtotal.cents() == 12176);
        CHECK(t.tax.cents() == (before ? 802 : 773));
        CHECK(pos.totals()[u"hasBuydowns"_s].toBool());
        // The receipt: the line under its item.
        const print::TicketContext ctx{pos.shared()->settings, [](std::int64_t) { return std::string("Nov 25"); },
                                       [](std::int64_t) { return std::string("10:00 AM"); }, 0};
        const std::string r = print::renderText(print::receipt(current(pos), ctx), 42);
        CHECK(r.find("SONOMA SAVINGS!") != std::string::npos);
        CHECK(r.find("-$4.10") != std::string::npos);
        CHECK(r.find("121.76") != std::string::npos);   // the subtotal under the lines
    }
}

TEST_CASE("Buydowns at the register: not before the start, not after the end; rung before the end, kept",
          "[buydowns]")
{
    PosService pos(store(), nullptr);
    std::int64_t clock = at(2026, 9, 30, 23, 59);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1234"_s));
    openDrawer(pos);
    REQUIRE(pos.addItem(u"sonoma-pk"_s));
    CHECK(pos.lines()[0].toMap()[u"buydown"_s].toString().isEmpty());
    clock = at(2026, 12, 31, 22, 59);
    REQUIRE(pos.addItem(u"sonoma-pk"_s));
    CHECK(pos.lines()[1].toMap()[u"buydown"_s] == u"SONOMA SAVINGS!"_s);
    clock = at(2027, 1, 1, 0, 5);   // paid after the end: the line stays
    REQUIRE(pos.addItem(u"sonoma-pk"_s));
    CHECK(pos.lines()[2].toMap()[u"buydown"_s].toString().isEmpty());
    // The second, rung before the end, keeps its line now it's after.
    CHECK(pos.lines()[1].toMap()[u"buydown"_s] == u"SONOMA SAVINGS!"_s);
    CHECK(current(pos).totals(pos.shared()->settings.tax).buydowns.cents() == 41);
}

TEST_CASE("Buydowns at the register: quantity, error correct and a return carry the line", "[buydowns]")
{
    PosService pos(store(), nullptr);
    std::int64_t clock = at(2026, 11, 25);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1234"_s));
    openDrawer(pos);
    REQUIRE(pos.addItem(u"sonoma-pk"_s));
    const qint64 id = pos.lines()[0].toMap()[u"id"_s].toLongLong();
    REQUIRE(pos.setLineQuantity(id, 3));
    CHECK(pos.lines()[0].toMap()[u"buydownPrice"_s] == u"-$1.23"_s);
    CHECK(current(pos).totals(pos.shared()->settings.tax).buydowns.cents() == 123);
    pos.selectLine(id);
    REQUIRE(pos.errorCorrect());
    CHECK(current(pos).totals(pos.shared()->settings.tax).buydowns.cents() == 0);
    CHECK(pos.lines()[0].toMap()[u"buydown"_s].toString().isEmpty());

    // A return without the receipt: the buydown running now, so what comes back is what was paid.
    REQUIRE(pos.addItemReturn(u"sonoma-pk"_s, 1148, 1, u"Defective"_s, QString::fromLatin1(kPack)));
    const core::Totals t = current(pos).totals(pos.shared()->settings.tax);
    CHECK(t.buydowns.cents() == -41);
    CHECK(t.subtotal.cents() == -1107);
    CHECK(t.total.cents() == -1107 - 70);
}

TEST_CASE("Buydowns: two at once, the larger; a capped style stops when its quantity is used", "[buydowns]")
{
    app::PosData d = store();
    core::Buydown big = d.settings.buydowns[0];
    big.id = "bd-2";
    big.number = 2;
    big.name = "SONOMA EXTRA";
    big.amount = Money::fromCents(500);
    big.style = 3;   // Quantity with Date Range
    big.qty = 2;
    d.settings.buydowns.push_back(big);
    PosService pos(std::move(d), nullptr);
    std::int64_t clock = at(2026, 11, 25);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1234"_s));
    openDrawer(pos);
    REQUIRE(pos.addItem(u"sonoma-pk"_s));
    REQUIRE(pos.addItem(u"sonoma-pk"_s));
    REQUIRE(pos.addItem(u"sonoma-pk"_s));
    const core::Check &c = current(pos);
    CHECK(c.lines[0].buydownId == "bd-2");
    CHECK(c.lines[0].buydownEach.cents() == 50);
    CHECK(c.lines[1].buydownId == "bd-2");
    CHECK(c.lines[2].buydownId == "bd-1");   // the 2 of SONOMA EXTRA are used: the other one
    CHECK(c.lines[2].buydownEach.cents() == 41);
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    CHECK(pos.shared()->settings.buydowns[1].used == 2);
    // The list says so.
    REQUIRE(pos.buydownsOpen(true));
    const QVariantList list = pos.buydowns()[u"list"_s].toList();
    const auto extra = std::ranges::find_if(list, [](const QVariant &v) { return v.toMap()[u"id"_s] == u"bd-2"_s; });
    REQUIRE(extra != list.end());
    CHECK(extra->toMap()[u"usedText"_s] == u"2 of 2 used"_s);
    CHECK(extra->toMap()[u"state"_s] == u"ended"_s);
}

TEST_CASE("Buydowns setup: Copy to New keeps the items; the new name, dates and amount are applied", "[buydowns]")
{
    PosService pos(store(), nullptr);
    std::int64_t clock = at(2026, 12, 20);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.buydownsOpen(true));
    REQUIRE(pos.buydownCopy(u"bd-1"_s));
    QVariantMap draft = pos.buydowns()[u"draft"_s].toMap();
    REQUIRE(draft[u"itemList"_s].toList().size() == 2);
    CHECK(draft[u"isNew"_s].toBool());
    REQUIRE(pos.buydownSet({{u"name"_s, u"SONOMA 2027Q1"_s}, {u"amount"_s, u"5.00"_s},
                            {u"start"_s, u"2027-01-01 00:00"_s}, {u"end"_s, u"2027-03-31"_s}}));
    draft = pos.buydowns()[u"draft"_s].toMap();
    CHECK(draft[u"end"_s] == u"2027-03-31 23:59"_s);
    CHECK(draft[u"state"_s] == u"coming"_s);
    const QVariantList items = draft[u"itemList"_s].toList();
    CHECK(items[1].toMap()[u"amount"_s] == u"$0.50"_s);
    CHECK(items[1].toMap()[u"sale"_s] == u"$10.98"_s);
    REQUIRE(pos.buydownApply());
    const auto &list = pos.shared()->settings.buydowns;
    REQUIRE(list.size() == 2);
    CHECK(list[1].name == "SONOMA 2027Q1");
    CHECK(list[1].items == list[0].items);
    CHECK(list[1].amount.cents() == 500);
    CHECK_FALSE(list[1].changedBy.empty());   // who saved it, and when
    CHECK(list[1].changedAt == clock);
    CHECK_FALSE(pos.buydowns().contains(u"draft"_s));
    // In January the new one rings.
    clock = at(2027, 1, 5);
    REQUIRE(pos.addItem(u"sonoma-pk"_s));
    CHECK(current(pos).lines[0].buydownEach.cents() == 50);
    CHECK(current(pos).lines[0].buydownId == list[1].id);

    // Items by "starts with", and taken out again.
    REQUIRE(pos.buydownNew(u"promo"_s));
    REQUIRE(pos.buydownAddStartingWith(u"sonoma ff"_s));
    CHECK(pos.buydowns()[u"draft"_s].toMap()[u"itemList"_s].toList().size() == 2);
    const QString key = pos.buydowns()[u"draft"_s].toMap()[u"itemList"_s].toList()[0].toMap()[u"key"_s].toString();
    REQUIRE(pos.buydownRemoveItems({key}));
    CHECK(pos.buydowns()[u"draft"_s].toMap()[u"itemList"_s].toList().size() == 1);
    CHECK_FALSE(pos.buydownApply());   // no name, no amount
    REQUIRE(pos.buydownAbort());
    CHECK(pos.shared()->settings.buydowns.size() == 2);
}

TEST_CASE("Buydowns setup: a manager can't save without an admin's PIN", "[buydowns][pricebook]")
{
    app::PosData d = store();
    for (core::Employee &e : d.employees)
        e.allow.erase(core::perm::PriceBook);
    PosService pos(std::move(d), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));   // a manager
    REQUIRE(pos.buydownsOpen(true));
    CHECK_FALSE(pos.buydowns()[u"canApply"_s].toBool());
    REQUIRE(pos.buydownEdit(u"bd-1"_s));
    REQUIRE(pos.buydownSet({{u"amount"_s, u"4.50"_s}}));
    CHECK_FALSE(pos.buydownApply());
    CHECK(pos.shared()->settings.buydowns[0].amount.cents() == 410);
    pos.invoke(u"buydownApply"_s);
    REQUIRE(pos.approvalInfo()[u"needed"_s].toBool());
    CHECK_FALSE(pos.approve(u"1234"_s));
    REQUIRE(pos.approve(u"9999"_s));
    CHECK(pos.shared()->settings.buydowns[0].amount.cents() == 450);
    CHECK_FALSE(pos.buydownDelete(u"bd-1"_s));
    CHECK(pos.shared()->settings.buydowns.size() == 1);
}

TEST_CASE("Buydowns: the old per-item tags become a definition once, and the tags are gone", "[buydowns]")
{
    app::PosData d = store(false);
    for (core::MenuItem &m : d.menu) {
        if (m.id == "sonoma-ctn")
            m.recipe.push_back({"__buydown__", 4.10});
        else if (m.id == "sonoma-pk")
            m.recipe.push_back({"__buydown__", 0.41});
        else
            continue;
        m.recipe.push_back({"__buydown_grp__:SONOMA", 1.0});
        m.recipe.push_back({"__buydown_start__", 20261001});
        m.recipe.push_back({"__buydown_end__", 20261231});
    }
    test::RecordingSink sink;
    PosService pos(std::move(d), &sink);
    const auto &list = pos.shared()->settings.buydowns;
    REQUIRE(list.size() == 1);
    CHECK(list[0].amount.cents() == 410);
    CHECK(list[0].items.size() == 2);
    CHECK(pos.shared()->settings.buydownTagsMigrated);
    for (const core::RecipeLine &rl : pos.findItem(u"sonoma-pk"_s)->recipe)
        CHECK_FALSE(rl.ingredientId.starts_with("__buydown"));
    REQUIRE(sink.menu.contains("sonoma-pk"));   // saved without them
    // Saved settings keep it (and the mark), so it's never done twice.
    const core::PosSettings again = app::settingsFromJson(app::toJson(pos.shared()->settings));
    CHECK(again.buydowns == pos.shared()->settings.buydowns);
    CHECK(again.buydownTagsMigrated);
}

TEST_CASE("Buydowns: saved checks keep the line; the reports, scan data and Price Check show it", "[buydowns][reports]")
{
    PosService pos(store(), nullptr);
    std::int64_t clock = at(2026, 11, 25);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1234"_s));
    openDrawer(pos);
    REQUIRE(pos.addItem(u"sonoma-pk"_s));
    REQUIRE(pos.addItem(u"sonoma-pk"_s));
    REQUIRE(pos.addItem(u"sonoma-ctn"_s));
    const auto back = app::checkFromJson(app::toJson(current(pos)));
    REQUIRE(back);
    CHECK(back->lines[0].buydownId == "bd-1");
    CHECK(back->lines[0].buydownEach.cents() == 41);
    CHECK(back->lines[0].buydownText == "SONOMA SAVINGS!");
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());

    const core::Report sales = pos.buildReport(u"sales"_s);
    CHECK(sales.find("Buydowns") == std::vector<std::string>{"Buydowns", "-$4.92"});
    const core::Report b = pos.buildReport(u"buydowns"_s);
    const auto total = b.find("Total SONOMA 2026Q4");
    REQUIRE(total.size() == 6);
    CHECK(total[1] == "3");
    CHECK(total[2] == "2");
    CHECK(total[3] == "1");
    CHECK(total[4] == "1.2");
    CHECK(total[5] == "$4.92");
    CHECK(pos.buildReport(u"buydowns|sonoma"_s).find("Summary").size() == 6);
    CHECK(app::PosService::endOfDayReportIds().contains(u"buydowns"_s));   // kept at End of Day, exported

    // The definition report, one buydown.
    const core::Report def = pos.buydownDefinitions({u"bd-1"_s});
    CHECK(def.find(kPack).at(5) == "$11.07");

    // Price Check: what it rings up at.
    REQUIRE(pos.priceCheckCode(QString::fromLatin1(kPack)));
    const QVariantMap pc = pos.priceCheck()[u"item"_s].toMap()[u"buydown"_s].toMap();
    CHECK(pc[u"sale"_s] == u"$11.07"_s);
    CHECK(pc[u"label"_s] == u"Sale price (with SONOMA SAVINGS!)"_s);

    // Scan data: the buydown in its field (27/28), not in the price.
    core::MenuItem &pk = pos.shared()->menu[pos.shared()->menu.size() - 1];
    pk.scanMaker = "other";
    pk.scanCategory = "cig";
    core::scandata::Options o;
    o.from = at(2026, 11, 22, 0, 0);
    o.to = at(2026, 11, 29, 0, 0);
    o.weekEnding = "20261128";
    o.date = [](std::int64_t) { return std::string("20261125"); };
    o.time = [](std::int64_t) { return std::string("10:00:00"); };
    core::scandata::Settings ss;
    ss.optional = {"cig"};
    const auto r = core::scandata::build(pos.shared()->closedToday, pos.shared()->menu, {}, pos.shared()->settings.tax, ss, o);
    REQUIRE(r.records.size() == 1);
    CHECK(r.records[0].f[26] == "SONOMA SAVINGS!");
    CHECK(r.records[0].f[27] == "0.82");
    CHECK(r.records[0].f[30] == "22.14");
}

TEST_CASE("Buydowns: a definition as a file for another store, matched by UPC", "[buydowns]")
{
    PosService here(store(), nullptr);
    REQUIRE(here.loginWithPin(u"1234"_s));
    QVariantMap file = here.buydownFile(u"bd-1"_s);
    REQUIRE(file[u"format"_s] == u"viewtouch-buydown"_s);
    // Through JSON, as the file is.
    file = QJsonDocument::fromJson(QJsonDocument(QJsonObject::fromVariantMap(file)).toJson()).object().toVariantMap();

    app::PosData d = store(false);
    std::erase_if(d.menu, [](const core::MenuItem &m) { return m.id == "sonoma-ctn"; });   // that store has no cartons
    d.menu.back().id = "their-pack";
    PosService there(std::move(d), nullptr);
    REQUIRE(there.loginWithPin(u"1234"_s));
    REQUIRE(there.buydownImport(file));
    const QVariantMap draft = there.buydowns()[u"draft"_s].toMap();
    REQUIRE(draft[u"itemList"_s].toList().size() == 1);
    CHECK(draft[u"notFound"_s].toList().size() == 1);
    CHECK(draft[u"notFound"_s].toList()[0].toMap()[u"upc"_s] == QString::fromLatin1(kCarton));
    REQUIRE(there.buydownApply());
    REQUIRE(there.shared()->settings.buydowns.size() == 1);
    CHECK(there.shared()->settings.buydowns[0].items[0].itemId == "their-pack");
    CHECK(there.shared()->settings.buydowns[0].name == "SONOMA 2026Q4");
}
