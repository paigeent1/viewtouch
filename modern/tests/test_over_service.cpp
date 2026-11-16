#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core/over_service.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QDate>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace vt;
using Catch::Approx;
using vt::app::PosService;

// Responsible service: standard drinks per guest (a seat, or the customer linked to it), the caution
// and stop levels at ring-in, and the audit file (app/pos_over_service.cpp, core/over_service.hh).

namespace {

constexpr std::int64_t kHour = 3'600'000;

// A bar: the policy on (caution 4, stop 6 within 3 hours), a beer (12 oz at 5% = 1.0), a bottle of wine
// (25.4 oz at 14% = 5.93) and a soda (no ABV), with a clock the test moves.
struct Bar {
    PosService pos{test::seedPosData(), nullptr};
    std::int64_t t = 1'800'000'000'000;   // a fixed moment
    QString beer, wine, soda;

    explicit Bar(bool subtract = false)
    {
        pos.shared()->setClock([this] { return t; });
        REQUIRE(pos.loginWithPin(u"1234"_s));   // Morgan, a manager
        pos.entryKey(u"10000"_s);
        REQUIRE(pos.openDrawerSession());
        beer = item(u"EXAMPLE LAGER 12OZ"_s, u"5"_s, u"12"_s);
        wine = item(u"EXAMPLE RED BOTTLE"_s, u"14"_s, u"25.4"_s);
        soda = item(u"EXAMPLE SODA"_s, QString(), u"12"_s);
        core::ServicePolicy &p = pos.shared()->settings.service;
        p = core::ServicePolicy{};
        p.on = true;
        p.subtract = subtract;
    }
    QString item(const QString &name, const QString &abv, const QString &oz)
    {
        REQUIRE(pos.saveMenuItemCard({{u"name"_s, name}, {u"price"_s, u"6.00"_s}, {u"family"_s, u"burgers"_s},
                                      {u"abv"_s, abv}, {u"pourOz"_s, oz}}));
        for (const core::MenuItem &m : pos.shared()->menu)
            if (m.name == name.toStdString())
                return QString::fromStdString(m.id);
        FAIL("no item");
        return {};
    }
    // Each guest's count on the open check, by label.
    QString count(const QString &label) const
    {
        for (const QVariant &v : pos.drinkCounts())
            if (v.toMap()[u"label"_s].toString() == label)
                return v.toMap()[u"drinks"_s].toString();
        return {};
    }
    QString level() const { return pos.serviceAsk()[u"level"_s].toString(); }
    int lines() const { return int(pos.lines().size()); }
    std::vector<QJsonObject> audit() const { return pos.serviceAuditEntries(0, t + 1); }
    core::Check *check() { return const_cast<core::Check *>(pos.currentCheck()); }
};

// The barcode on the back of a license (AAMVA): this ID number, born this many years ago.
QString license(const QString &number, int yearsOld)
{
    const QString dob = QDate::currentDate().addYears(-yearsOld).toString(u"MMddyyyy"_s);
    const QString expires = QDate::currentDate().addYears(3).toString(u"MMddyyyy"_s);
    return u"@\n\x1e\rANSI 636000090002DL00410200ZC02410012DLDAQ"_s + number + u"\nDCSSAMPLE\nDACTEST\nDBB"_s + dob
         + u"\nDBA"_s + expires + u"\nDAJCT\n"_s;
}

} // namespace

TEST_CASE("Responsible service: the counting rules (window, subtraction, levels)", "[service]")
{
    core::ServicePolicy p;
    p.subtract = false;
    const std::int64_t t0 = 1'800'000'000'000;
    const std::vector<core::DrinkRing> rings = {{t0, 1.0}, {t0 + kHour, 1.0}, {t0 + 2 * kHour, 1.5}};
    CHECK(core::drinksNow(rings, p, t0 + 2 * kHour) == Approx(3.5));
    // The window is 3 hours: at t0 + 3h30 the first one no longer counts; past 5h none do.
    CHECK(core::drinksNow(rings, p, t0 + 3 * kHour + kHour / 2) == Approx(2.5));
    CHECK(core::drinksNow(rings, p, t0 + 5 * kHour + 1) == Approx(0.0));
    // Less one an hour since the first drink in the window, never below 0.
    p.subtract = true;
    CHECK(core::drinksNow(rings, p, t0 + 2 * kHour) == Approx(1.5));
    p.perHour = 0.5;
    CHECK(core::drinksNow(rings, p, t0 + 2 * kHour) == Approx(2.5));
    CHECK(core::drinksNow({{t0, 1.0}}, p, t0 + 2 * kHour + 1) == Approx(0.0));

    // Levels: reaching them counts (4.0 made of fractions too).
    core::ServicePolicy q;
    CHECK(core::levelOf(3.9, q) == core::ServiceLevel::Ok);
    CHECK(core::levelOf(0.83 + 1.17 + 0.83 + 1.17, q) == core::ServiceLevel::Caution);
    CHECK(core::levelOf(6.0, q) == core::ServiceLevel::Stop);
    q.caution = 0;   // no caution level
    CHECK(core::levelOf(5.0, q) == core::ServiceLevel::Ok);

    // A line: its quantity; a piece of a shared item its part; voided nothing; no ABV nothing.
    core::OrderLine l;
    l.itemId = "beer";
    l.stdDrinks = 1.0;
    l.quantity = 2;
    CHECK(core::lineDrinks(l) == Approx(2.0));
    l.share = 1;
    l.shareOf = 4;
    CHECK(core::lineDrinks(l) == Approx(0.5));
    l.unshare();
    l.voided = true;
    CHECK(core::lineDrinks(l) == 0.0);
    l.voided = false;
    l.stdDrinks = 0;
    CHECK(core::lineDrinks(l) == 0.0);
}

TEST_CASE("Responsible service: counted by seat, and moved with the item", "[service]")
{
    Bar bar;
    REQUIRE(bar.pos.startCheck(core::CheckType::Takeout));
    bar.check()->guests = 4;
    REQUIRE(bar.pos.setSeat(1));
    REQUIRE(bar.pos.addItem(bar.beer));
    REQUIRE(bar.pos.addItem(bar.beer));
    REQUIRE(bar.pos.setSeat(2));
    REQUIRE(bar.pos.addItem(bar.beer));
    CHECK(bar.count(u"Seat 1"_s) == u"2.0"_s);
    CHECK(bar.count(u"Seat 2"_s) == u"1.0"_s);
    // Each ring: its time, its drinks, who rang it.
    const core::Check &c = *bar.pos.currentCheck();
    const auto rings = core::drinkRings({&c}, 0);
    const auto &seat1 = rings.at(core::drinkGuest(c, 1).key);
    REQUIRE(seat1.size() == 2);
    CHECK(seat1[0].at == bar.t);
    CHECK(seat1[0].drinks == Approx(1.0));
    CHECK_FALSE(seat1[0].byName.empty());

    // The line on another check (a transfer, a split): its drinks go with it, to that check's seat.
    core::Check other;
    other.id = 9999;
    core::Check copy = c;
    REQUIRE(copy.lines.size() == 3);
    other.adoptLine(*copy.takeLine(copy.lines.front().id));
    const auto moved = core::drinkRings({&copy, &other}, 0);
    CHECK(core::drinksNow(moved.at(core::drinkGuest(copy, 1).key), {}, bar.t) == Approx(1.0));
    CHECK(core::drinksNow(moved.at(core::drinkGuest(other, 1).key), {}, bar.t) == Approx(1.0));
}

TEST_CASE("Responsible service: counted by customer across checks (an ID scanned for a seat)", "[service]")
{
    Bar bar;
    REQUIRE(bar.pos.startCheck(core::CheckType::Takeout));
    bar.check()->guests = 2;
    REQUIRE(bar.pos.setSeat(2));
    REQUIRE(bar.pos.verifyAgeScan(license(u"T11112222"_s, 30)));   // seat 2's ID
    for (int i = 0; i < 3; ++i)
        REQUIRE(bar.pos.addItem(bar.beer));
    CHECK(bar.count(u"Seat 2 (ID ••2222)"_s) == u"3.0"_s);
    bar.pos.releaseCheck();

    // The same person on another check: their drinks count together, so the next one is the 4th.
    REQUIRE(bar.pos.startCheck(core::CheckType::Takeout));
    bar.check()->guests = 3;
    REQUIRE(bar.pos.setSeat(1));
    REQUIRE(bar.pos.verifyAgeScan(license(u"T11112222"_s, 30)));
    CHECK_FALSE(bar.pos.addItem(bar.beer));
    CHECK(bar.level() == u"caution"_s);
    CHECK(bar.pos.serviceAsk()[u"drinks"_s] == u"4.0"_s);
    REQUIRE(bar.pos.cancelService());
    // Someone else at seat 3 of that check starts at 0.
    REQUIRE(bar.pos.setSeat(3));
    REQUIRE(bar.pos.addItem(bar.beer));
    CHECK(bar.count(u"Seat 3"_s) == u"1.0"_s);
    bar.pos.releaseCheck();

    // A saved customer linked to a check (one guest): every drink on it is theirs, on any check.
    REQUIRE(bar.pos.startCheck(core::CheckType::Takeout));
    bar.check()->customerId = "cust-1";
    bar.check()->customer.name = "Pat Example";
    for (int i = 0; i < 2; ++i)
        REQUIRE(bar.pos.addItem(bar.beer));
    bar.pos.releaseCheck();
    REQUIRE(bar.pos.startCheck(core::CheckType::Takeout));
    bar.check()->customerId = "cust-1";
    bar.check()->customer.name = "Pat Example";
    REQUIRE(bar.pos.addItem(bar.beer));
    CHECK(bar.count(u"Pat Example"_s) == u"3.0"_s);
}

TEST_CASE("Responsible service: the time window and the hourly subtraction", "[service]")
{
    SECTION("without subtraction: only drinks within the window")
    {
        Bar bar;
        REQUIRE(bar.pos.startCheck(core::CheckType::Takeout));
        for (int i = 0; i < 3; ++i)
            REQUIRE(bar.pos.addItem(bar.beer));
        bar.t += 3 * kHour + 60'000;   // past the 3 hours
        CHECK(bar.count(u"No seat"_s) == u"0.0"_s);   // nothing counts
        REQUIRE(bar.pos.addItem(bar.beer));
        CHECK(bar.count(u"No seat"_s) == u"1.0"_s);
    }
    SECTION("with subtraction: one less each hour since the first")
    {
        Bar bar(true);
        REQUIRE(bar.pos.startCheck(core::CheckType::Takeout));
        for (int i = 0; i < 3; ++i)
            REQUIRE(bar.pos.addItem(bar.beer));
        bar.t += 2 * kHour;
        CHECK(bar.count(u"No seat"_s) == u"1.0"_s);
        // So three more go in before the caution level (1 + 3 = 4).
        REQUIRE(bar.pos.addItem(bar.beer));
        REQUIRE(bar.pos.addItem(bar.beer));
        CHECK_FALSE(bar.pos.addItem(bar.beer));
        CHECK(bar.level() == u"caution"_s);
    }
}

TEST_CASE("Responsible service: a void takes the drink off the count", "[service]")
{
    Bar bar;
    REQUIRE(bar.pos.startCheck(core::CheckType::Takeout));
    for (int i = 0; i < 3; ++i)
        REQUIRE(bar.pos.addItem(bar.beer));
    CHECK(bar.count(u"No seat"_s) == u"3.0"_s);
    REQUIRE(bar.pos.voidItem());   // not sent: off the check
    CHECK(bar.count(u"No seat"_s) == u"2.0"_s);
    // Sent and voided: kept on the check, counted as nothing.
    REQUIRE(bar.pos.sendOrder());
    bar.pos.selectLine(bar.pos.lines().front().toMap()[u"id"_s].toLongLong());
    REQUIRE(bar.pos.voidItem());
    CHECK(bar.count(u"No seat"_s) == u"1.0"_s);
    // So the next two go in without a caution.
    REQUIRE(bar.pos.addItem(bar.beer));
    REQUIRE(bar.pos.addItem(bar.beer));
    CHECK(bar.pos.serviceAsk().isEmpty());
}

TEST_CASE("Responsible service: caution waits for one tap; it and the warning are audited", "[service]")
{
    QTemporaryDir dir;
    Bar bar;
    bar.pos.shared()->serviceAuditFile = dir.filePath(u"alcohol_policy_audit.jsonl"_s);
    REQUIRE(bar.pos.startCheck(core::CheckType::Takeout));
    bar.check()->guests = 2;
    REQUIRE(bar.pos.setSeat(1));
    for (int i = 0; i < 3; ++i)
        REQUIRE(bar.pos.addItem(bar.beer));
    CHECK(bar.pos.serviceAsk().isEmpty());

    // The 4th: caution. It isn't on the check until acknowledged.
    CHECK_FALSE(bar.pos.addItem(bar.beer));
    CHECK(bar.level() == u"caution"_s);
    CHECK(bar.pos.serviceAsk()[u"guest"_s] == u"Seat 1"_s);
    CHECK(bar.pos.serviceAsk()[u"drinks"_s] == u"4.0"_s);
    CHECK(bar.lines() == 3);
    REQUIRE(bar.pos.acknowledgeService());
    CHECK(bar.pos.serviceAsk().isEmpty());
    CHECK(bar.lines() == 4);
    CHECK(bar.count(u"Seat 1"_s) == u"4.0"_s);

    // The file: one line each, with who, where, the count, the level and what was done.
    QFile f(bar.pos.shared()->serviceAuditFile);
    REQUIRE(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QList<QByteArray> lines = f.readAll().trimmed().split('\n');
    REQUIRE(lines.size() == 2);
    const QJsonObject warning = QJsonDocument::fromJson(lines[0]).object();
    const QJsonObject ack = QJsonDocument::fromJson(lines[1]).object();
    CHECK(warning[u"action"_s] == u"warning"_s);
    CHECK(ack[u"action"_s] == u"acknowledged"_s);
    for (const QJsonObject &e : {warning, ack}) {
        CHECK(e[u"at"_s].toInteger() == bar.t);
        CHECK_FALSE(e[u"time"_s].toString().isEmpty());
        CHECK_FALSE(e[u"employee"_s].toString().isEmpty());
        CHECK(e[u"check"_s].toString() == QString::fromStdString(bar.pos.currentCheck()->label));
        CHECK(e[u"checkId"_s].toInteger() == bar.pos.currentCheck()->id);
        CHECK(e[u"seat"_s].toInt() == 1);
        CHECK(e[u"guest"_s] == u"Seat 1"_s);
        CHECK(e[u"drinks"_s].toDouble() == Approx(4.0));
        CHECK(e[u"level"_s] == u"caution"_s);
        CHECK(e.contains(u"manager"_s));
        CHECK(e.contains(u"reason"_s));
    }
    // Read back for the report.
    REQUIRE(bar.audit().size() == 2);
    const QVariantMap report = bar.pos.report(u"service"_s, 0);
    CHECK(report[u"title"_s].toString() == u"Responsible Service"_s);
    bool acknowledged = false;
    for (const QVariant &r : report[u"rows"_s].toList())
        acknowledged = acknowledged || r.toMap()[u"cells"_s].toStringList().contains(u"Acknowledged"_s);
    CHECK(acknowledged);
}

TEST_CASE("Responsible service: stop blocks the drink until a manager overrides it", "[service]")
{
    Bar bar;
    REQUIRE(bar.pos.startCheck(core::CheckType::Takeout));
    for (int i = 0; i < 5; ++i) {
        if (!bar.pos.addItem(bar.beer))
            REQUIRE(bar.pos.acknowledgeService());   // the 4th and 5th: caution
    }
    CHECK(bar.lines() == 5);

    // The 6th is a stop.
    CHECK_FALSE(bar.pos.addItem(bar.beer));
    CHECK(bar.level() == u"stop"_s);
    CHECK_FALSE(bar.pos.serviceAsk()[u"reasons"_s].toStringList().isEmpty());
    CHECK_FALSE(bar.pos.acknowledgeService());                              // one tap isn't enough
    CHECK_FALSE(bar.pos.overrideService(u"1111"_s, u"Guest is fine"_s));    // not a manager's PIN
    CHECK_FALSE(bar.pos.overrideService(u"1234"_s, u"  "_s));               // no reason
    CHECK(bar.lines() == 5);
    CHECK(bar.level() == u"stop"_s);
    REQUIRE(bar.pos.overrideService(u"1234"_s, u"Count is wrong (shared or not theirs)"_s));
    CHECK(bar.pos.serviceAsk().isEmpty());
    CHECK(bar.lines() == 6);
    const QJsonObject over = bar.audit().back();
    CHECK(over[u"action"_s] == u"override"_s);
    CHECK(over[u"level"_s] == u"stop"_s);
    CHECK_FALSE(over[u"manager"_s].toString().isEmpty());
    CHECK(over[u"reason"_s] == u"Count is wrong (shared or not theirs)"_s);
    CHECK(over[u"drinks"_s].toDouble() == Approx(6.0));

    // Not rung in: nothing added, and that's audited too.
    CHECK_FALSE(bar.pos.addItem(bar.beer));
    REQUIRE(bar.pos.cancelService());
    CHECK(bar.lines() == 6);
    CHECK(bar.audit().back()[u"action"_s] == u"cancelled"_s);

    // "+" on a drink and Again are counted too.
    const qint64 last = bar.pos.lines().back().toMap()[u"id"_s].toLongLong();
    CHECK_FALSE(bar.pos.repeatLine(last));
    CHECK(bar.level() == u"stop"_s);
    REQUIRE(bar.pos.cancelService());
    CHECK_FALSE(bar.pos.setLineQuantity(last, 2));
    CHECK(bar.level() == u"stop"_s);
    REQUIRE(bar.pos.overrideService(u"1234"_s, u"Manager checked on the guest"_s));
    CHECK(bar.pos.lines().back().toMap()[u"quantity"_s] == 2);
}

TEST_CASE("Responsible service: a bottle counts against its seat, and can be shared across seats", "[service]")
{
    Bar bar;
    REQUIRE(bar.pos.startCheck(core::CheckType::Takeout));
    bar.check()->guests = 3;
    REQUIRE(bar.pos.setSeat(1));
    CHECK_FALSE(bar.pos.addItem(bar.wine));   // 5.9 for seat 1: caution
    CHECK(bar.level() == u"caution"_s);
    REQUIRE(bar.pos.acknowledgeService());
    CHECK(bar.count(u"Seat 1"_s) == u"5.9"_s);
    const qint64 bottle = bar.pos.lines().back().toMap()[u"id"_s].toLongLong();
    REQUIRE(bar.pos.shareDrink(bottle, {1, 2, 3}));
    CHECK(bar.count(u"Seat 1"_s) == u"2.0"_s);
    CHECK(bar.count(u"Seat 2"_s) == u"2.0"_s);
    CHECK(bar.count(u"Seat 3"_s) == u"2.0"_s);
    CHECK(bar.audit().back()[u"action"_s] == u"shared"_s);
    // Back to its own seat.
    REQUIRE(bar.pos.shareDrink(bottle, {}));
    CHECK(bar.count(u"Seat 1"_s) == u"5.9"_s);
    CHECK(bar.count(u"Seat 2"_s).isEmpty());
}

TEST_CASE("Responsible service: a drink with no ABV counts 0; off means no warnings", "[service]")
{
    SECTION("no ABV")
    {
        Bar bar;
        REQUIRE(bar.pos.startCheck(core::CheckType::Takeout));
        for (int i = 0; i < 10; ++i)
            REQUIRE(bar.pos.addItem(bar.soda));
        CHECK(bar.pos.serviceAsk().isEmpty());
        CHECK(bar.pos.drinkCounts().isEmpty());
        CHECK(bar.pos.lines().front().toMap()[u"drinks"_s].toDouble() == 0.0);
    }
    SECTION("the setting off")
    {
        Bar bar;
        bar.pos.shared()->settings.service.on = false;
        REQUIRE(bar.pos.startCheck(core::CheckType::Takeout));
        for (int i = 0; i < 10; ++i)
            REQUIRE(bar.pos.addItem(bar.beer));
        CHECK(bar.pos.serviceAsk().isEmpty());
        CHECK(bar.pos.drinkCounts().isEmpty());
        CHECK(bar.lines() == 10);
        CHECK(bar.audit().empty());
    }
}

TEST_CASE("Responsible service: the settings, saved and read back", "[service][settings]")
{
    core::PosSettings s;
    CHECK_FALSE(s.service.on);   // off by default
    CHECK(s.service.caution == 4.0);
    CHECK(s.service.stop == 6.0);
    CHECK(s.service.windowHours == 3.0);
    CHECK(s.service.subtract);
    CHECK(s.service.perHour == 1.0);
    s.service.on = true;
    s.service.caution = 3.5;
    s.service.overrideReasons = {"Designated driver confirmed"};
    const core::PosSettings back = app::settingsFromJson(app::toJson(s));
    CHECK(back.service == s.service);

    // Manager -> Store Settings: the caution level must be under the stop level.
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    QVariantMap rec = pos.adminRecords(u"store"_s).value(0).toMap();
    REQUIRE(rec.contains(u"serviceOn"_s));
    rec[u"serviceOn"_s] = true;
    rec[u"serviceCaution"_s] = 7.0;
    CHECK_FALSE(pos.saveAdminRecord(u"store"_s, 0, rec));
    CHECK_FALSE(pos.shared()->settings.service.on);
    rec[u"serviceCaution"_s] = 3.0;
    rec[u"serviceWindowHours"_s] = 2.0;
    REQUIRE(pos.saveAdminRecord(u"store"_s, 0, rec));
    CHECK(pos.shared()->settings.service.on);
    CHECK(pos.shared()->settings.service.caution == 3.0);
    CHECK(pos.shared()->settings.service.windowHours == 2.0);
}
