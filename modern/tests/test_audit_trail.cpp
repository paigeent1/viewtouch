#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "core/report.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"
#include "storage/async_writer.hh"
#include "storage/pos_store.hh"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
using vt::app::PosShared;

// The audit trail on each sale: its cost at the time of sale, who rang each line, who took each payment.

namespace {

QStringList cellsOf(const QVariantMap &report, const QString &first)
{
    for (const QVariant &v : report[u"rows"_s].toList()) {
        const QStringList cells = v.toMap()[u"cells"_s].toStringList();
        if (!cells.isEmpty() && cells.first() == first)
            return cells;
    }
    return {};
}

void setCost(PosShared &shared, const std::string &itemId, double dollars)
{
    for (core::MenuItem &m : shared.menu) {
        if (m.id != itemId)
            continue;
        std::erase_if(m.recipe, [](const core::RecipeLine &r) { return r.ingredientId == "__cost__"; });
        m.recipe.push_back({"__cost__", dollars});
    }
}

const core::OrderLine *lineFor(const core::Check &c, const std::string &itemId)
{
    for (const core::OrderLine &l : c.lines)
        if (l.itemId == itemId)
            return &l;
    return nullptr;
}

} // namespace

TEST_CASE("A sale line keeps the cost it had when it was rung", "[audit][cost]")
{
    PosShared shared(test::seedPosData(), nullptr);
    PosService pos(&shared, u"Front"_s);
    setCost(shared, "tea", 0.80);   // a Menu Builder cost (retail items, ILSA's cost)
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    REQUIRE(pos.addItem(u"classic-burger"_s));   // its recipe at today's ingredient costs: $2.01
    pos.finishChoosing();
    REQUIRE(pos.addItem(u"tea"_s));
    REQUIRE(pos.addItem(u"water"_s));            // no cost at all
    const qint64 id = pos.checkInfo()[u"id"_s].toLongLong();
    {
        const core::Check &c = shared.open.at(id);
        const core::OrderLine *burger = lineFor(c, "classic-burger");
        const core::OrderLine *tea = lineFor(c, "tea");
        const core::OrderLine *water = lineFor(c, "water");
        REQUIRE((burger && tea && water));
        CHECK(burger->costKnown);
        CHECK(burger->costAtSale() == Money::fromCents(201));
        CHECK(tea->costKnown);
        CHECK(tea->unitCost == Money::fromCents(80));
        CHECK_FALSE(water->costKnown);
        CHECK_FALSE(water->costAtSale().has_value());
    }

    // Costs go up afterwards: what was sold keeps what it cost then.
    for (core::Ingredient &g : shared.ingredients)
        g.cost = g.cost * 2;
    setCost(shared, "tea", 1.50);
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    REQUIRE(shared.closedToday.size() == 1);
    CHECK(lineFor(shared.closedToday.back(), "tea")->unitCost == Money::fromCents(80));
    CHECK(lineFor(shared.closedToday.back(), "classic-burger")->costAtSale() == Money::fromCents(201));

    const QVariantMap report = pos.report(u"foodcost"_s);
    CHECK(cellsOf(report, u"Classic Burger"_s).value(3) == u"$2.01"_s);
    CHECK(cellsOf(report, u"Tea"_s).value(3) == u"$0.80"_s);
    CHECK(cellsOf(report, u"Water"_s).value(3) == u"-"_s);

    // Reopened (and its price changed) it still keeps the cost it was sold at.
    REQUIRE(pos.reopenCheck(id));
    CHECK(lineFor(shared.open.at(id), "tea")->unitCost == Money::fromCents(80));
    CHECK(lineFor(shared.open.at(id), "classic-burger")->costAtSale() == Money::fromCents(201));

    // Another round of it is a new sale: today's cost.
    pos.selectLine(lineFor(shared.open.at(id), "tea")->id);
    REQUIRE(pos.repeatLine(0));
    CHECK(shared.open.at(id).lines.back().unitCost == Money::fromCents(150));
}

TEST_CASE("A line sold before costs were kept uses its item's cost today, and says so", "[audit][cost]")
{
    PosShared shared(test::seedPosData(), nullptr);
    PosService pos(&shared, u"Front"_s);
    setCost(shared, "tea", 0.90);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    // An old check, as saved before: no unitCost on its line.
    core::Check old;
    old.id = 900;
    old.status = core::CheckStatus::Closed;
    old.serverName = "Sam";
    core::OrderLine tea;
    tea.id = 1;
    tea.itemId = "tea";
    tea.name = "Tea";
    tea.unitPrice = Money::fromCents(250);
    tea.sent = true;
    old.lines.push_back(tea);
    const auto loaded = app::checkFromJson(app::toJson(old));
    REQUIRE(loaded);
    CHECK_FALSE(loaded->lines[0].costKnown);
    shared.closedToday.push_back(*loaded);

    const QVariantMap report = pos.report(u"foodcost"_s);
    CHECK(cellsOf(report, u"Tea +"_s).value(3) == u"$0.90"_s);
    bool said = false;
    for (const QVariant &v : report[u"rows"_s].toList())
        said = said || v.toMap()[u"cells"_s].toStringList().value(0).contains(u"before costs were kept"_s);
    CHECK(said);
}

TEST_CASE("Mill opens, Steve adds and cashes out: each is on record", "[audit][who]")
{
    // Sam (a server) is "Mill", Morgan (the manager) is "Steve".
    PosShared shared(test::seedPosData(), nullptr);
    PosService front(&shared, u"Front"_s);
    PosService bar(&shared, u"Bar"_s);
    REQUIRE(bar.loginWithPin(u"1234"_s));
    bar.entryKey(u"10000"_s);
    REQUIRE(bar.openDrawerSession());

    REQUIRE(front.loginWithPin(u"1111"_s));
    REQUIRE(front.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(front.startCheck(core::CheckType::DineIn));
    REQUIRE(front.addItem(u"coffee"_s));
    const qint64 id = front.checkInfo()[u"id"_s].toLongLong();
    front.releaseCheck();

    REQUIRE(bar.openCheck(id));
    REQUIRE(bar.addItem(u"tea"_s));
    REQUIRE(bar.tender(u"cash"_s));
    REQUIRE(bar.closeCheck());

    REQUIRE(shared.closedToday.size() == 1);
    const core::Check &c = shared.closedToday.back();
    CHECK(c.serverId == "sam");                               // the server stays the opener
    CHECK(lineFor(c, "coffee")->addedById == "sam");
    CHECK(lineFor(c, "coffee")->addedByName == "Sam");
    CHECK(lineFor(c, "tea")->addedById == "manager");
    CHECK(lineFor(c, "tea")->addedAt > 0);
    REQUIRE_FALSE(c.payments.empty());
    CHECK(c.payments.back().takenById == "manager");
    CHECK(c.payments.back().at > 0);

    // The report: each line for whoever rang it.
    const QVariantMap report = bar.report(u"employee_items"_s);
    CHECK(cellsOf(report, u"Sam total"_s).value(1) == u"1"_s);
    CHECK(cellsOf(report, u"Sam total"_s).value(2) == u"$2.75"_s);
    CHECK(cellsOf(report, u"Morgan (Manager) total"_s).value(2) == u"$2.50"_s);
    CHECK(cellsOf(report, u"All employees"_s).value(2) == u"$5.25"_s);
    // Only tea.
    const QVariantMap teaOnly = bar.report(u"employee_items|TEA"_s);
    CHECK(cellsOf(teaOnly, u"Sam total"_s).isEmpty());
    CHECK(cellsOf(teaOnly, u"Morgan (Manager) total"_s).value(1) == u"1"_s);

    // Reopened and transferred: who rang each line and who took the payment stay.
    REQUIRE(bar.reopenCheck(id));
    REQUIRE(bar.transferCheck(u"casey"_s));
    const core::Check &again = shared.open.at(id);
    CHECK(again.serverId == "casey");
    CHECK(lineFor(again, "coffee")->addedById == "sam");
    CHECK(lineFor(again, "tea")->addedById == "manager");
    CHECK(again.payments.back().takenById == "manager");

    // And saved and read back.
    const auto back = app::checkFromJson(app::toJson(again));
    REQUIRE(back);
    CHECK(lineFor(*back, "tea")->addedByName == lineFor(again, "tea")->addedByName);
    CHECK(lineFor(*back, "tea")->addedAt == lineFor(again, "tea")->addedAt);
    CHECK(lineFor(*back, "tea")->costKnown == lineFor(again, "tea")->costKnown);
    CHECK(back->payments.back().takenById == "manager");
    CHECK(back->payments.back().at == again.payments.back().at);
}

TEST_CASE("Sales by employee and item: totals per employee", "[audit][reports]")
{
    core::PosSettings settings;
    const core::ReportContext ctx{settings, "Today", [](std::int64_t) { return std::string("9:00 PM"); }};
    const auto line = [](std::int64_t id, const char *item, const char *name, std::int64_t cents, int qty,
                         const char *byId, const char *byName) {
        core::OrderLine l;
        l.id = id;
        l.itemId = item;
        l.name = name;
        l.unitPrice = Money::fromCents(cents);
        l.quantity = qty;
        l.sent = true;
        l.addedById = byId;
        l.addedByName = byName;
        return l;
    };
    std::vector<core::MenuItem> menu(2);
    menu[0].id = "coffee";
    menu[0].name = "Coffee";
    menu[0].family = "drinks";
    menu[1].id = "robusto";
    menu[1].name = "Robusto";
    menu[1].family = "cigars";
    std::vector<core::MenuCategory> categories(1);
    categories[0].id = "cigars";
    categories[0].name = "Premium Cigars";

    // A server's check: the humidor attendant sold two cigars on it and cashed it out.
    core::Check a;
    a.id = 1;
    a.label = "T4";
    a.serverId = "sam";
    a.serverName = "Sam";
    a.lines.push_back(line(1, "coffee", "Coffee", 275, 1, "sam", "Sam"));
    a.lines.push_back(line(2, "robusto", "Robusto", 1000, 2, "hank", "Hank"));
    core::OrderLine voided = line(3, "robusto", "Robusto", 5000, 1, "hank", "Hank");
    voided.voided = true;   // left out
    a.lines.push_back(voided);
    core::Payment cash;
    cash.kind = core::TenderKind::Cash;
    cash.takenById = "hank";
    cash.takenByName = "Hank";
    a.payments.push_back(cash);
    // An old check: nobody on its lines, so its server.
    core::Check b;
    b.id = 2;
    b.serverId = "rosa";
    b.serverName = "Rosa";
    b.lines.push_back(line(1, "coffee", "Coffee", 275, 1, "", ""));
    // The kiosk.
    core::Check k;
    k.id = 3;
    k.kiosk = true;
    k.lines.push_back(line(1, "coffee", "Coffee", 275, 1, "", "Kiosk"));

    const core::Report r = core::employeeItemSales({a, b, k}, menu, categories, "", ctx);
    CHECK(r.find("Hank total") == std::vector<std::string>{"Hank total", "2", "$20.00"});
    CHECK(r.find("Sam total") == std::vector<std::string>{"Sam total", "1", "$2.75"});
    CHECK(r.find("Rosa (server) total") == std::vector<std::string>{"Rosa (server) total", "1", "$2.75"});
    CHECK(r.find("Kiosk total") == std::vector<std::string>{"Kiosk total", "1", "$2.75"});
    CHECK(r.find("All employees") == std::vector<std::string>{"All employees", "5", "$28.25"});
    bool cashedOut = false;
    for (const core::ReportRow &row : r.rows)
        cashedOut = cashedOut || (row.cells.size() == 3 && row.cells[0].starts_with("T4 #1") && row.cells[2] == "cashed out by Hank");
    CHECK(cashedOut);

    // By category name: only the cigars.
    const core::Report cigars = core::employeeItemSales({a, b, k}, menu, categories, "premium", ctx);
    CHECK(cigars.find("Hank total") == std::vector<std::string>{"Hank total", "2", "$20.00"});
    CHECK(cigars.find("Sam total").empty());
    CHECK(cigars.find("All employees") == std::vector<std::string>{"All employees", "2", "$20.00"});
}

TEST_CASE("An old check read back has no cost, no ringer, no taker: and writes none", "[audit][json]")
{
    const QJsonObject line{{u"id"_s, 1}, {u"itemId"_s, u"coffee"_s}, {u"name"_s, u"Coffee"_s}, {u"unitPrice"_s, 275},
                           {u"quantity"_s, 1}, {u"taxClass"_s, u"food"_s}, {u"sent"_s, true}, {u"voided"_s, false},
                           {u"sentAt"_s, 5},
                           {u"modifiers"_s, QJsonArray{QJsonObject{{u"itemId"_s, u"oat"_s}, {u"name"_s, u"Oat milk"_s},
                                                                    {u"unitPrice"_s, 50}}}}};
    const QJsonObject payment{{u"id"_s, 1}, {u"tenderId"_s, u"cash"_s}, {u"tenderName"_s, u"Cash"_s},
                              {u"kind"_s, u"cash"_s}, {u"amount"_s, 325}};
    const QJsonObject old{{u"id"_s, 7}, {u"type"_s, u"quick"_s}, {u"status"_s, u"closed"_s}, {u"serverId"_s, u"sam"_s},
                          {u"serverName"_s, u"Sam"_s}, {u"lines"_s, QJsonArray{line}}, {u"payments"_s, QJsonArray{payment}}};
    const auto c = app::checkFromJson(old);
    REQUIRE(c);
    REQUIRE(c->lines.size() == 1);
    const core::OrderLine &l = c->lines[0];
    CHECK_FALSE(l.costKnown);
    CHECK(l.unitCost == Money());
    CHECK_FALSE(l.costAtSale().has_value());
    CHECK(l.addedById.empty());
    CHECK(l.addedByName.empty());
    CHECK(l.addedAt == 0);
    REQUIRE(l.modifiers.size() == 1);
    CHECK_FALSE(l.modifiers[0].costKnown);
    REQUIRE(c->payments.size() == 1);
    CHECK(c->payments[0].takenById.empty());
    CHECK(c->payments[0].at == 0);

    // Written again: still unknown (no 0 cost made up).
    const QJsonObject again = app::toJson(*c);
    const QJsonObject lo = again[u"lines"_s].toArray()[0].toObject();
    CHECK_FALSE(lo.contains(u"unitCost"_s));
    CHECK_FALSE(lo.contains(u"addedById"_s));
    CHECK_FALSE(lo[u"modifiers"_s].toArray()[0].toObject().contains(u"unitCost"_s));
    CHECK_FALSE(again[u"payments"_s].toArray()[0].toObject().contains(u"takenById"_s));

    // A known cost of zero stays known.
    core::Check zero = *c;
    zero.lines[0].costKnown = true;
    zero.lines[0].unitCost = Money();
    const auto z = app::checkFromJson(app::toJson(zero));
    REQUIRE(z);
    CHECK(z->lines[0].costKnown);
    CHECK(z->lines[0].costAtSale() == Money());
}

// --- which terminal (station) rang, paid, refunded, opened and closed it (owner 2026-11-23) ----------

TEST_CASE("Rung on one terminal, paid on another, refunded on a third: all three kept", "[audit][terminal]")
{
    PosShared shared(test::seedPosData(), nullptr);
    PosService front(&shared, u"Front"_s);    // terminal A
    PosService bar(&shared, u"Bar"_s);        // terminal B
    PosService office(&shared, u"Office"_s);  // terminal C
    REQUIRE(bar.loginWithPin(u"1234"_s));
    bar.entryKey(u"10000"_s);
    REQUIRE(bar.openDrawerSession());

    REQUIRE(front.loginWithPin(u"1111"_s));
    REQUIRE(front.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(front.startCheck(core::CheckType::DineIn));
    REQUIRE(front.addItem(u"tea"_s));
    const qint64 id = front.checkInfo()[u"id"_s].toLongLong();
    CHECK(shared.open.at(id).openedOn == "Front");
    front.releaseCheck();

    REQUIRE(bar.openCheck(id));
    REQUIRE(bar.tender(u"credit"_s));
    REQUIRE(bar.closeCheck());
    REQUIRE(shared.closedToday.size() == 1);
    {
        const core::Check &c = shared.closedToday.back();
        CHECK(lineFor(c, "tea")->addedOn == "Front");
        REQUIRE(c.payments.size() == 1);
        CHECK(c.payments[0].takenOn == "Bar");
        CHECK(c.openedOn == "Front");
        CHECK(c.closedOn == "Bar");
    }

    REQUIRE(office.loginWithPin(u"1234"_s));
    const qint64 paymentId = shared.closedToday.back().payments[0].id;
    REQUIRE(office.refundPayment(id, paymentId, 0, u"Wrong item"_s));
    const core::Check &c = shared.closedToday.back();
    REQUIRE(c.refunds.size() == 1);
    CHECK(c.refunds[0].givenOn == "Office");
    REQUIRE(shared.refundsToday.size() == 1);
    CHECK(shared.refundsToday[0].givenOn == "Office");
    // Still the other two.
    CHECK(lineFor(c, "tea")->addedOn == "Front");
    CHECK(c.payments[0].takenOn == "Bar");

    // Saved and read back: all of them.
    const auto back = app::checkFromJson(app::toJson(c));
    REQUIRE(back);
    CHECK(lineFor(*back, "tea")->addedOn == "Front");
    CHECK(back->payments[0].takenOn == "Bar");
    CHECK(back->refunds[0].givenOn == "Office");
    CHECK(back->openedOn == "Front");
    CHECK(back->closedOn == "Bar");
    CHECK(app::refundFromJson(app::toJson(c.refunds[0])).givenOn == "Office");

    // Find a Check shows them: rang on, paid on, refunded on, opened and closed on.
    const core::Check copy = c;
    shared.findChecks = [&](const PosShared::CheckFind &) { return std::vector<core::Check>{copy}; };
    REQUIRE(office.searchChecks(u"#%1"_s.arg(id), 0));
    for (int i = 0; i < 300 && office.checkSearch()[u"loading"_s].toBool(); ++i)
        QTest::qWait(10);
    office.selectFoundCheck(id);
    const QVariantMap seen = office.checkSearch()[u"selected"_s].toMap();
    CHECK(seen[u"openedOn"_s] == u"Front"_s);
    CHECK(seen[u"closedOn"_s] == u"Bar"_s);
    CHECK(seen[u"lines"_s].toList().value(0).toMap()[u"on"_s] == u"Front"_s);
    CHECK(seen[u"payments"_s].toList().value(0).toMap()[u"on"_s] == u"Bar"_s);
    CHECK(seen[u"refunds"_s].toList().value(0).toMap()[u"on"_s] == u"Office"_s);

    // Reopened: where it was closed is forgotten until it closes again; the rest stays.
    REQUIRE(bar.reopenCheck(id));
    const core::Check &again = shared.open.at(id);
    CHECK(again.closedOn.empty());
    CHECK(again.openedOn == "Front");
    CHECK(lineFor(again, "tea")->addedOn == "Front");
    CHECK(again.payments[0].takenOn == "Bar");
}

TEST_CASE("Where a line was rung stays through split, transfer and void", "[audit][terminal]")
{
    PosShared shared(test::seedPosData(), nullptr);
    PosService front(&shared, u"Front"_s);
    PosService bar(&shared, u"Bar"_s);
    REQUIRE(front.loginWithPin(u"1111"_s));
    REQUIRE(front.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(front.startCheck(core::CheckType::DineIn));
    REQUIRE(front.addItem(u"tea"_s));
    REQUIRE(front.addItem(u"water"_s));
    const qint64 first = front.checkInfo()[u"id"_s].toLongLong();
    front.releaseCheck();

    // On the bar terminal, the water goes onto a check of its own.
    REQUIRE(bar.loginWithPin(u"1234"_s));
    REQUIRE(bar.openCheck(first));
    bar.selectLine(lineFor(shared.open.at(first), "water")->id);
    REQUIRE(bar.splitLine(0));
    qint64 second = 0;
    for (const auto &[cid, c] : shared.open)
        if (cid != first && lineFor(c, "water"))
            second = cid;
    REQUIRE(second != 0);
    CHECK(lineFor(shared.open.at(second), "water")->addedOn == "Front");   // rung there, not here
    CHECK(shared.open.at(second).openedOn == "Bar");                       // the new check was made here
    CHECK(lineFor(shared.open.at(first), "tea")->addedOn == "Front");

    // Transferred to another server: unchanged.
    REQUIRE(bar.openCheck(first));
    REQUIRE(bar.transferCheck(u"casey"_s));
    CHECK(shared.open.at(first).serverId == "casey");
    CHECK(shared.open.at(first).openedOn == "Front");
    CHECK(lineFor(shared.open.at(first), "tea")->addedOn == "Front");

    // Saved and read back.
    const auto back = app::checkFromJson(app::toJson(shared.open.at(second)));
    REQUIRE(back);
    CHECK(lineFor(*back, "water")->addedOn == "Front");
    CHECK(back->openedOn == "Bar");
}

TEST_CASE("Terminals survive the database, and an old check has none", "[audit][terminal][posstore]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"vt.db"_s);
    const auto seed = test::seedPosData();
    {
        storage::PosStore store(path);
        REQUIRE(store.open());
        REQUIRE(store.seed(seed.settings, seed.menu, seed.employees));
    }
    qint64 id = 0;
    {
        storage::PosStore store(path);
        REQUIRE(store.open());
        storage::AsyncWriter writer(path);
        storage::SqlPosSink sink(writer);
        PosShared shared(*store.load(), &sink);
        PosService reg(&shared, u"Register 2"_s);
        REQUIRE(reg.loginWithPin(u"1234"_s));
        reg.entryKey(u"10000"_s);
        REQUIRE(reg.openDrawerSession());
        REQUIRE(reg.startCheck(core::CheckType::Takeout));
        REQUIRE(reg.addItem(u"tea"_s));
        id = reg.checkInfo()[u"id"_s].toLongLong();
        REQUIRE(reg.tender(u"cash"_s));
        REQUIRE(reg.closeCheck());
        // the writer's destructor flushes
    }
    storage::PosStore store(path);
    REQUIRE(store.open());
    const auto closed = store.checks(core::CheckStatus::Closed);
    REQUIRE(closed.size() == 1);
    CHECK(closed[0].id == id);
    CHECK(closed[0].openedOn == "Register 2");
    CHECK(closed[0].closedOn == "Register 2");
    REQUIRE(closed[0].lines.size() == 1);
    CHECK(closed[0].lines[0].addedOn == "Register 2");
    REQUIRE(closed[0].payments.size() == 1);
    CHECK(closed[0].payments[0].takenOn == "Register 2");

    // A check saved before terminals were kept: empty (unknown), and none written back.
    const QJsonObject line{{u"id"_s, 1}, {u"itemId"_s, u"coffee"_s}, {u"name"_s, u"Coffee"_s}, {u"unitPrice"_s, 275},
                           {u"quantity"_s, 1}, {u"sent"_s, true}, {u"addedById"_s, u"sam"_s}};
    const QJsonObject payment{{u"id"_s, 1}, {u"tenderId"_s, u"cash"_s}, {u"kind"_s, u"cash"_s}, {u"amount"_s, 275}};
    const QJsonObject refund{{u"id"_s, 1}, {u"paymentId"_s, 1}, {u"amount"_s, 100}, {u"method"_s, u"cash"_s}};
    const QJsonObject old{{u"id"_s, 8}, {u"type"_s, u"quick"_s}, {u"status"_s, u"closed"_s}, {u"lines"_s, QJsonArray{line}},
                          {u"payments"_s, QJsonArray{payment}}, {u"refunds"_s, QJsonArray{refund}}};
    const auto c = app::checkFromJson(old);
    REQUIRE(c);
    CHECK(c->openedOn.empty());
    CHECK(c->closedOn.empty());
    CHECK(c->lines[0].addedOn.empty());
    CHECK(c->payments[0].takenOn.empty());
    CHECK(c->refunds[0].givenOn.empty());
    const QJsonObject again = app::toJson(*c);
    CHECK_FALSE(again.contains(u"openedOn"_s));
    CHECK_FALSE(again.contains(u"closedOn"_s));
    CHECK_FALSE(again[u"lines"_s].toArray()[0].toObject().contains(u"addedOn"_s));
    CHECK_FALSE(again[u"payments"_s].toArray()[0].toObject().contains(u"takenOn"_s));
    CHECK_FALSE(again[u"refunds"_s].toArray()[0].toObject().contains(u"givenOn"_s));
}

TEST_CASE("Server Sales: the same checks by terminal", "[audit][terminal][reports]")
{
    core::PosSettings settings;
    const core::ReportContext ctx{settings, "Today", [](std::int64_t) { return std::string("9:00 PM"); }};
    const auto check = [](std::int64_t id, const char *server, const char *closedOn, std::int64_t cents) {
        core::Check c;
        c.id = id;
        c.status = core::CheckStatus::Closed;
        c.serverId = server;
        c.serverName = server;
        c.closedOn = closedOn;
        core::OrderLine l;
        l.id = 1;
        l.itemId = "x";
        l.name = "X";
        l.taxClass = core::TaxClass::Merchandise;
        l.unitPrice = Money::fromCents(cents);
        l.sent = true;
        c.lines.push_back(l);
        return c;
    };
    const core::Report r = core::serverSales(
        {check(1, "Sam", "Bar 1", 1000), check(2, "Rosa", "Bar 1", 500), check(3, "Sam", "Front", 250), check(4, "Sam", "", 100)},
        ctx);
    CHECK(r.find("Sam") == std::vector<std::string>{"Sam", "3", "3", "$13.50"});
    CHECK(r.find("Bar 1") == std::vector<std::string>{"Bar 1", "2", "2", "$15.00"});
    CHECK(r.find("Front") == std::vector<std::string>{"Front", "1", "1", "$2.50"});
    CHECK(r.find("(unknown)") == std::vector<std::string>{"(unknown)", "1", "1", "$1.00"});
    bool section = false, total = false;
    for (const core::ReportRow &row : r.rows) {
        section = section || (row.kind == core::ReportRow::Kind::Section && row.cells[0].starts_with("By terminal"));
        total = total || (row.kind == core::ReportRow::Kind::Total && row.cells == std::vector<std::string>{"All terminals", "4", "4", "$18.50"});
    }
    CHECK(section);
    CHECK(total);
}
