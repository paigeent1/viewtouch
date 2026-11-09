#include <catch2/catch_test_macros.hpp>

#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QSignalSpy>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// M4: drawer, business day, reports, split checks, admin, print hooks.

namespace {

struct RecordingPrinter : app::PosPrinter {
    struct Kitchen { std::string label; std::vector<std::string> lines; bool voids; };
    std::vector<Kitchen> kitchen;
    std::vector<std::int64_t> receipts;
    std::vector<std::string> reports;
    int drawerKicks = 0;

    void printKitchen(const core::PosSettings &, const core::Check &c, const std::vector<core::OrderLine> &lines,
                      bool voids) override
    {
        Kitchen k{c.label, {}, voids};
        for (const auto &l : lines)
            k.lines.push_back(l.name);
        kitchen.push_back(k);
    }
    std::string lastPrinter;
    void printReceipt(const core::PosSettings &, const core::Check &c, const std::string &printer) override
    {
        receipts.push_back(c.id);
        lastPrinter = printer;
    }
    void printReport(const core::PosSettings &, const core::Report &r, const std::string &printer) override
    {
        reports.push_back(r.title);
        lastPrinter = printer;
    }
    void openDrawer(const core::PosSettings &, const std::string &printer) override
    {
        ++drawerKicks;
        lastPrinter = printer;
    }
};

struct Pos {
    test::RecordingSink sink;
    RecordingPrinter printer;
    app::PosService pos;
    std::int64_t clock = 1'700'000'000'000;

    explicit Pos(app::PosData data = test::seedPosData())
        : pos(std::move(data), &sink)
    {
        pos.setClock([this] { return clock; });
        pos.setPrinter(&printer);
    }
    void login(const char *pin) { REQUIRE(pos.loginWithPin(QString::fromLatin1(pin))); }
    void entry(const char *digits) { pos.entryKey(QString::fromLatin1(digits)); }
    void openDrawer(const char *startingCents)
    {
        entry(startingCents);
        REQUIRE(pos.openDrawerSession());
    }
    // A quick check with one item, paid in cash and closed.
    void cashSale(const char *item, const char *tenderedCents)
    {
        REQUIRE(pos.addItem(QString::fromLatin1(item)));
        entry(tenderedCents);
        REQUIRE(pos.tender(u"cash"_s));
        REQUIRE(pos.closeCheck());
    }
    QString reportValue(const QString &id, const std::string &label) const
    {
        const auto cells = pos.buildReport(id).find(label);
        return cells.empty() ? QString() : QString::fromStdString(cells.back());
    }
};

} // namespace

TEST_CASE("A new business day is opened on first start", "[m4][day]")
{
    Pos t;
    CHECK(t.pos.currentDay().id == 1);
    CHECK(t.pos.currentDay().openedAt > 0);
    CHECK(t.pos.currentDay().open());
    CHECK(t.pos.dayInfo()[u"ready"_s].toBool());
}

TEST_CASE("Sending prints kitchen tickets; voiding a sent item prints a void", "[m4][print]")
{
    Pos t;
    t.login("1234");
    t.pos.addItem(u"classic-burger"_s);
    t.pos.addItem(u"draft-beer"_s);
    REQUIRE(t.pos.sendOrder());
    REQUIRE(t.printer.kitchen.size() == 1);   // the printer groups by station
    CHECK(t.printer.kitchen[0].lines.size() == 2);
    CHECK_FALSE(t.printer.kitchen[0].voids);

    t.pos.addItem(u"coffee"_s);
    REQUIRE(t.pos.sendOrder());
    REQUIRE(t.printer.kitchen.size() == 2);
    CHECK(t.printer.kitchen[1].lines == std::vector<std::string>{"Coffee"});   // only the new line

    REQUIRE(t.pos.voidItem());
    REQUIRE(t.printer.kitchen.size() == 3);
    CHECK(t.printer.kitchen[2].voids);

    REQUIRE(t.pos.printReceipt());
    CHECK(t.printer.receipts.size() == 1);
}

TEST_CASE("Cash needs an open drawer; the drawer tracks net cash", "[m4][drawer]")
{
    Pos t;
    t.login("2222");
    t.pos.addItem(u"soda"_s);                      // 2.95 + 0.24 = 3.19
    t.entry("500");
    REQUIRE(t.pos.tender(u"cash"_s));
    QSignalSpy notices(&t.pos, &PosService::notice);
    CHECK_FALSE(t.pos.closeCheck());
    CHECK(notices.last()[0].toString().contains(u"drawer"_s));

    t.openDrawer("10000");                          // $100 bank
    CHECK(t.printer.drawerKicks == 1);
    REQUIRE(t.pos.closeCheck());                    // change 1.81
    CHECK(t.printer.drawerKicks == 2);
    CHECK(t.pos.closedToday().back().drawerSession == 1);

    t.cashSale("coffee", "300");                    // 2.75 + 0.23 = 2.98, change 0.02
    CHECK(t.pos.drawerInfo()[u"cashSales"_s].toString() == u"$6.17"_s);
    CHECK(t.pos.drawerInfo()[u"expected"_s].toString() == u"$106.17"_s);

    // Card sales do not touch the drawer.
    t.pos.addItem(u"tea"_s);
    REQUIRE(t.pos.tender(u"credit"_s));
    REQUIRE(t.pos.closeCheck());
    CHECK(t.pos.drawerInfo()[u"expected"_s].toString() == u"$106.17"_s);

    // Count: $106.00 -> 17 cents short.
    CHECK_FALSE(t.pos.countDrawer());               // amount required
    t.entry("10600");
    REQUIRE(t.pos.countDrawer());
    const QVariantMap d = t.pos.drawerInfo();
    CHECK_FALSE(d[u"open"_s].toBool());
    CHECK(d[u"overShortCents"_s].toLongLong() == -17);
    CHECK(notices.last()[0].toString().contains(u"short"_s));
    CHECK(t.printer.reports.back() == "Drawers");
}

TEST_CASE("Reports add up the day's closed checks", "[m4][reports]")
{
    Pos t;
    t.login("1234");
    t.openDrawer("0");
    t.cashSale("classic-burger", "2000");           // 11.50 + 0.95 = 12.45
    t.pos.addItem(u"draft-beer"_s);                 // 6.00 + 0.60 = 6.60
    t.pos.addItem(u"draft-beer"_s);                 // 12.00 + 1.20 = 13.20
    REQUIRE(t.pos.tender(u"credit"_s));
    REQUIRE(t.pos.closeCheck());

    CHECK(t.reportValue(u"sales"_s, "Checks") == u"2"_s);
    CHECK(t.reportValue(u"sales"_s, "Net sales") == u"$23.50"_s);
    CHECK(t.reportValue(u"sales"_s, "Food tax") == u"$0.95"_s);
    CHECK(t.reportValue(u"sales"_s, "Alcohol tax") == u"$1.20"_s);
    CHECK(t.reportValue(u"sales"_s, "Total with tax") == u"$25.65"_s);
    CHECK(t.reportValue(u"sales"_s, "Cash") == u"$20.00"_s);
    CHECK(t.reportValue(u"sales"_s, "Change given") == u"-$7.55"_s);
    CHECK(t.reportValue(u"sales"_s, "Collected") == u"$25.65"_s);

    const auto beer = t.pos.buildReport(u"items"_s).find("Draft Beer");
    REQUIRE(beer.size() == 3);
    CHECK(beer[1] == "2");
    CHECK(beer[2] == "$12.00");
    CHECK(t.reportValue(u"servers"_s, "Morgan (Manager)") == u"$23.50"_s);

    // Open checks are not sales yet.
    t.pos.addItem(u"cobb"_s);
    CHECK(t.reportValue(u"sales"_s, "Checks") == u"2"_s);

    const QVariantMap onScreen = t.pos.report(u"sales"_s);
    CHECK(onScreen[u"title"_s].toString() == u"Sales Summary"_s);
    CHECK_FALSE(onScreen[u"rows"_s].toList().isEmpty());
}

TEST_CASE("Labor report counts hours on the clock", "[m4][reports]")
{
    Pos t;
    t.pos.pinKey(u"1"_s); t.pos.pinKey(u"1"_s); t.pos.pinKey(u"1"_s); t.pos.pinKey(u"1"_s);
    REQUIRE(t.pos.clockIn());
    t.clock += 90 * 60'000;                          // 1.5 hours
    CHECK(t.reportValue(u"labor"_s, "Total hours") == u"1.50"_s);
    const auto sam = t.pos.buildReport(u"labor"_s).find("Sam");
    REQUIRE(sam.size() == 5);   // name, in, out, breaks, hours
    CHECK(sam[2] == "on clock");
}

TEST_CASE("End of day: blockers, then close, archive, and start over", "[m4][day]")
{
    Pos t;
    t.login("1234");
    t.openDrawer("5000");
    t.cashSale("pancakes", "2000");
    t.pos.addItem(u"coffee"_s);                     // left open

    QVariantMap day = t.pos.dayInfo();
    CHECK_FALSE(day[u"ready"_s].toBool());
    CHECK(day[u"blockers"_s].toStringList().size() == 2);
    CHECK_FALSE(t.pos.endOfDay());

    REQUIRE(t.pos.tender(u"credit"_s));
    REQUIRE(t.pos.closeCheck());
    CHECK_FALSE(t.pos.endOfDay());                  // drawer still open
    t.entry("6028");                                 // 50 + 9.50 + 0.78 = 60.28
    REQUIRE(t.pos.countDrawer());
    CHECK(t.pos.drawerInfo()[u"overShortCents"_s].toLongLong() == 0);

    // Servers may not close the day.
    t.pos.logout();
    t.login("1111");
    CHECK_FALSE(t.pos.endOfDay());
    t.pos.logout();
    t.login("1234");

    t.clock += 3'600'000;
    REQUIRE(t.pos.endOfDay());
    CHECK(t.pos.currentDay().id == 2);
    CHECK(t.pos.closedToday().empty());
    CHECK_FALSE(t.pos.drawerInfo()[u"exists"_s].toBool());
    CHECK(t.printer.reports.back() == "Sales Summary");

    // Yesterday's reports are kept; today's start empty.
    const QVariantList days = t.pos.days();
    REQUIRE(days.size() == 2);
    const qint64 yesterday = days[1].toMap()[u"id"_s].toLongLong();
    CHECK(yesterday == 1);
    const QVariantList rows = t.pos.report(u"sales"_s, yesterday)[u"rows"_s].toList();
    bool found = false;
    for (const QVariant &r : rows) {
        const QStringList cells = r.toMap()[u"cells"_s].toStringList();
        if (cells.value(0) == u"Checks"_s) {
            CHECK(cells.value(1) == u"2"_s);
            found = true;
        }
    }
    CHECK(found);
    CHECK(t.reportValue(u"sales"_s, "Checks") == u"0"_s);
}

TEST_CASE("Split check: move items, then choose between checks at the table", "[m4][split]")
{
    Pos t;
    t.login("1111");
    REQUIRE(t.pos.selectTable(u"T5"_s) == PosService::TableNeedsGuests);
    REQUIRE(t.pos.startCheck(core::CheckType::DineIn));
    t.pos.addItem(u"cobb"_s);
    t.pos.addItem(u"greek"_s);
    const qint64 first = t.pos.checkInfo()[u"id"_s].toLongLong();

    CHECK(t.pos.splitTargets().size() == 1);        // only "New check"
    t.pos.selectLine(0);
    CHECK_FALSE(t.pos.splitLine(0));                // nothing selected
    t.pos.selectLine(t.pos.lines()[1].toMap()[u"id"_s].toLongLong());
    REQUIRE(t.pos.splitLine(0));                    // Greek -> new check
    CHECK(t.pos.lines().size() == 1);
    REQUIRE(t.pos.splitTargets().size() == 2);
    const qint64 second = t.pos.splitTargets()[0].toMap()[u"id"_s].toLongLong();
    CHECK(second != first);

    CHECK(t.pos.tableStatus(u"T5"_s)[u"checks"_s].toInt() == 2);
    t.pos.releaseCheck();
    CHECK(t.pos.selectTable(u"T5"_s) == PosService::TableChooseCheck);
    CHECK(t.pos.checkFilter() == u"T5"_s);
    REQUIRE(t.pos.openCheck(second));
    CHECK(t.pos.checkFilter().isEmpty());
    CHECK(t.pos.lines()[0].toMap()[u"name"_s].toString() == u"Greek"_s);

    // Move it back into the first check.
    t.pos.selectLine(t.pos.lines()[0].toMap()[u"id"_s].toLongLong());
    REQUIRE(t.pos.splitLine(first));
    CHECK(t.pos.lines().isEmpty());

    // Checks with payments are not split.
    REQUIRE(t.pos.openCheck(first));
    t.pos.entryKey(u"100"_s);
    REQUIRE(t.pos.tender(u"credit"_s));             // $1.00 paid
    t.pos.selectLine(t.pos.lines()[0].toMap()[u"id"_s].toLongLong());
    CHECK_FALSE(t.pos.splitLine(0));
}

TEST_CASE("Split check: one at a time, shared items, evenly", "[m4][split]")
{
    // Pieces: even parts, the first ones take the leftover cents.
    core::Check k;
    core::OrderLine &wine = k.addItem(core::MenuItem{.id = "wine", .name = "Wine", .price = Money::fromCents(1000)});
    const std::vector<std::int64_t> pieces = k.shareLine(wine.id, 3, 7);
    REQUIRE(pieces.size() == 3);
    CHECK(k.line(pieces[0])->total().cents() == 334);
    CHECK(k.line(pieces[1])->total().cents() == 333);
    CHECK(k.line(pieces[2])->total().cents() == 333);
    CHECK(k.line(pieces[1])->displayName() == "Wine (2/3)");
    CHECK(k.line(pieces[0])->forKitchen());
    CHECK_FALSE(k.line(pieces[1])->forKitchen());   // the kitchen makes it once
    CHECK(k.line(pieces[0])->counted() == 1);
    CHECK(k.line(pieces[2])->counted() == 0);
    CHECK(k.shareLine(pieces[0], 2, 8).empty());   // already shared

    Pos t;
    t.login("1111");
    REQUIRE(t.pos.selectTable(u"T5"_s) == PosService::TableNeedsGuests);
    REQUIRE(t.pos.startCheck(core::CheckType::DineIn));
    t.pos.addItem(u"cobb"_s);
    const qint64 cobb = t.pos.lines()[0].toMap()[u"id"_s].toLongLong();
    REQUIRE(t.pos.setLineQuantity(cobb, 3));
    const qint64 first = t.pos.checkInfo()[u"id"_s].toLongLong();
    const std::int64_t each = t.pos.findItem(u"cobb"_s)->price.cents();
    const auto line = [&](int i) { return t.pos.lines()[i].toMap(); };

    // 3 × Cobb: one moves, two stay (and stay chosen, for the next one).
    t.pos.selectLine(cobb);
    REQUIRE(t.pos.splitLine(0));
    CHECK(line(0)[u"quantity"_s].toInt() == 2);
    CHECK(line(0)[u"selected"_s].toBool());
    const qint64 second = t.pos.splitTargets()[0].toMap()[u"id"_s].toLongLong();
    REQUIRE(t.pos.splitLine(second));
    CHECK(line(0)[u"quantity"_s].toInt() == 1);
    // ...or all of a line.
    REQUIRE(t.pos.setLineQuantity(cobb, 2));
    t.pos.selectLine(cobb);
    REQUIRE(t.pos.splitLine(second, true));
    CHECK(t.pos.lines().isEmpty());
    REQUIRE(t.pos.openCheck(second));
    int onSecond = 0;
    for (const QVariant &l : t.pos.lines())
        onSecond += l.toMap()[u"quantity"_s].toInt();
    CHECK(onSecond == 4);

    // Share one of them 2 ways: a piece moves to the first check.
    t.pos.selectLine(t.pos.lines()[0].toMap()[u"id"_s].toLongLong());
    REQUIRE(t.pos.shareLine(2));
    int pieces2 = 0;
    for (const QVariant &l : t.pos.lines())
        pieces2 += l.toMap()[u"shared"_s].toBool();
    CHECK(pieces2 == 2);
    REQUIRE(t.pos.splitLine(first));               // the chosen piece (2/2)
    REQUIRE(t.pos.openCheck(first));
    REQUIRE(t.pos.lines().size() == 1);
    CHECK(line(0)[u"name"_s].toString().endsWith(u"(2/2)"_s));
    CHECK(t.pos.currentCheck()->lines[0].total().cents() == each / 2);   // the second piece
    CHECK_FALSE(t.pos.setLineQuantity(line(0)[u"id"_s].toLongLong(), 2));

    // Put back together: whole again, here.
    t.pos.selectLine(line(0)[u"id"_s].toLongLong());
    REQUIRE(t.pos.unshareLine());
    CHECK(t.pos.lines().size() == 1);
    CHECK_FALSE(line(0)[u"shared"_s].toBool());
    CHECK(t.pos.totals()[u"items"_s] == t.pos.format(Money::fromCents(each)));

    // Split evenly 3 ways: 4 × Cobb + a soda.
    REQUIRE(t.pos.openCheck(second));
    t.pos.addItem(u"soda"_s);
    const std::int64_t soda = t.pos.findItem(u"soda"_s)->price.cents();
    const std::int64_t whole = 4 * each + soda;
    // The cobb back here first, so it's 4 + 1 soda on one check.
    REQUIRE(t.pos.openCheck(first));
    t.pos.selectLine(line(0)[u"id"_s].toLongLong());
    REQUIRE(t.pos.splitLine(second));
    REQUIRE(t.pos.openCheck(second));
    REQUIRE(t.pos.splitEvenly(3));
    std::int64_t sum = 0;
    std::vector<std::int64_t> amounts;
    for (const QVariant &v : t.pos.openChecks()) {
        const QVariantMap c = v.toMap();
        if (c[u"label"_s] != u"T5"_s)
            continue;
        REQUIRE(t.pos.openCheck(c[u"id"_s].toLongLong()));
        std::int64_t items = 0;
        for (const core::OrderLine &l : t.pos.currentCheck()->lines)
            items += l.total().cents();
        if (t.pos.currentCheck()->lines.empty())
            continue;   // the first check, emptied above
        amounts.push_back(items);
        sum += items;
    }
    REQUIRE(amounts.size() == 3);
    CHECK(sum == whole);
    for (const std::int64_t a : amounts)
        CHECK(std::abs(a - whole / 3) <= 2);
}

TEST_CASE("Admin: menu items", "[m4][admin]")
{
    Pos t;
    t.login("1111");
    CHECK_FALSE(t.pos.adminSave(u"menu"_s, -1, {{u"name"_s, u"Nachos"_s}}));   // servers can't
    t.pos.logout();
    t.login("1234");

    QVariantMap nachos = t.pos.adminNewRecord(u"menu"_s);
    nachos[u"name"_s] = u"Loaded Nachos"_s;
    nachos[u"price"_s] = 9.95;
    nachos[u"family"_s] = u"starters"_s;
    REQUIRE(t.pos.adminSave(u"menu"_s, -1, nachos));
    REQUIRE(t.pos.findItem(u"loaded-nachos"_s));
    CHECK(t.pos.findItem(u"loaded-nachos"_s)->price.cents() == 995);

    // Sold out: can't be ordered.
    const int index = int(t.pos.menu().size()) - 1;
    QVariantMap edited = t.pos.adminRecords(u"menu"_s)[index].toMap();
    edited[u"available"_s] = false;
    REQUIRE(t.pos.adminSave(u"menu"_s, index, edited));
    CHECK_FALSE(t.pos.addItem(u"loaded-nachos"_s));

    CHECK_FALSE(t.pos.adminSave(u"menu"_s, -1, {{u"name"_s, u""_s}}));
    CHECK_FALSE(t.pos.adminSave(u"menu"_s, -1, {{u"name"_s, u"Bad"_s}, {u"price"_s, -1}}));

    REQUIRE(t.pos.adminDelete(u"menu"_s, index));
    CHECK_FALSE(t.pos.findItem(u"loaded-nachos"_s));
}

TEST_CASE("Admin: employees and PIN rules", "[m4][admin]")
{
    Pos t;
    t.login("1234");
    QVariantMap e = t.pos.adminNewRecord(u"employees"_s);
    e[u"name"_s] = u"Riley"_s;
    CHECK_FALSE(t.pos.adminSave(u"employees"_s, -1, e));       // PIN required
    e[u"pin"_s] = u"12"_s;
    CHECK_FALSE(t.pos.adminSave(u"employees"_s, -1, e));       // too short
    e[u"pin"_s] = u"1111"_s;
    CHECK_FALSE(t.pos.adminSave(u"employees"_s, -1, e));       // Sam's PIN
    e[u"pin"_s] = u"4321"_s;
    REQUIRE(t.pos.adminSave(u"employees"_s, -1, e));
    CHECK(t.pos.userName() == u"Morgan (Manager)"_s);          // still logged in correctly

    // Can't lock yourself out.
    const QVariantList staff = t.pos.adminRecords(u"employees"_s);
    int me = -1;
    for (int i = 0; i < staff.size(); ++i)
        if (staff[i].toMap()[u"id"_s].toString() == u"manager"_s) me = i;
    REQUIRE(me >= 0);
    QVariantMap self = staff[me].toMap();
    self[u"role"_s] = u"server"_s;
    CHECK_FALSE(t.pos.adminSave(u"employees"_s, me, self));
    CHECK_FALSE(t.pos.adminDelete(u"employees"_s, me));

    t.pos.logout();
    REQUIRE(t.pos.loginWithPin(u"4321"_s));
    CHECK(t.pos.userRole() == u"server"_s);
    t.pos.logout();

    // Deactivated staff cannot log in; their record stays.
    t.login("1234");
    const int riley = int(t.pos.employees().size()) - 1;
    REQUIRE(t.pos.adminDelete(u"employees"_s, riley));
    t.pos.logout();
    CHECK_FALSE(t.pos.loginWithPin(u"4321"_s));
}

TEST_CASE("Admin: a new employee from the form's First name and Last name", "[m4][admin]")
{
    Pos t;
    t.login("1234");
    QVariantMap e = t.pos.adminNewRecord(u"employees"_s);   // as the form sends it: no "name"
    e[u"pin"_s] = u"5678"_s;
    CHECK_FALSE(t.pos.adminSave(u"employees"_s, -1, e));       // no name yet
    e[u"firstName"_s] = u"Jordan"_s;
    e[u"lastName"_s] = u"Avery"_s;
    REQUIRE(t.pos.adminSave(u"employees"_s, -1, e));
    const int jordan = int(t.pos.employees().size()) - 1;
    CHECK(t.pos.employees()[jordan].name == "Jordan Avery");

    // Changing the last name of someone already on file changes it.
    QVariantMap again = t.pos.adminRecords(u"employees"_s)[jordan].toMap();
    CHECK(again[u"firstName"_s].toString() == u"Jordan"_s);
    again[u"lastName"_s] = u"Avery-Lee"_s;
    REQUIRE(t.pos.adminSave(u"employees"_s, jordan, again));
    CHECK(t.pos.employees()[jordan].name == "Jordan Avery-Lee");
}

TEST_CASE("Admin: tax change re-totals open checks; settings are saved", "[m4][admin]")
{
    struct SettingsSink : test::RecordingSink {
        int settingsSaves = 0;
        void saveSettings(const core::PosSettings &) override { ++settingsSaves; }
    };
    SettingsSink sink;
    app::PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.addItem(u"house-salad"_s);                             // 8.50 + 0.70
    CHECK(pos.totals()[u"total"_s].toString() == u"$9.20"_s);

    QVariantMap tax = pos.adminRecords(u"taxes"_s)[0].toMap();
    tax[u"food"_s] = 10.0;
    QSignalSpy changed(&pos, &PosService::checkChanged);
    REQUIRE(pos.adminSave(u"taxes"_s, 0, tax));
    CHECK(changed.size() >= 1);
    CHECK(pos.totals()[u"total"_s].toString() == u"$9.35"_s);
    CHECK(sink.settingsSaves == 1);

    tax[u"food"_s] = 150.0;
    CHECK_FALSE(pos.adminSave(u"taxes"_s, 0, tax));

    QVariantMap tender = pos.adminNewRecord(u"tenders"_s);
    tender[u"name"_s] = u"Happy Hour 25%"_s;
    tender[u"kind"_s] = u"discount"_s;
    tender[u"percent"_s] = 25.0;
    REQUIRE(pos.adminSave(u"tenders"_s, -1, tender));
    REQUIRE(pos.settings().tender("happy-hour-25"));
    CHECK(pos.settings().tender("happy-hour-25")->percentBp == 2500);

    QVariantMap printer = pos.adminNewRecord(u"printers"_s);
    printer[u"name"_s] = u"Patio"_s;
    CHECK_FALSE(pos.adminSave(u"printers"_s, -1, printer));   // network needs an address
    printer[u"host"_s] = u"192.168.1.50"_s;
    REQUIRE(pos.adminSave(u"printers"_s, -1, printer));
    CHECK(pos.settings().printer("patio"));
}

namespace {
// The row starting with `first` in the section titled `section`.
std::vector<std::string> rowIn(const core::Report &r, const std::string &section, const std::string &first)
{
    std::string current;
    for (const core::ReportRow &x : r.rows) {
        if (x.kind == core::ReportRow::Kind::Section)
            current = x.cells.empty() ? std::string() : x.cells.front();
        else if (current == section && !x.cells.empty() && x.cells.front() == first)
            return x.cells;
    }
    return {};
}
} // namespace

TEST_CASE("Exceptions report: voids, comps, payments taken back, reopened, no sales, by employee",
          "[m4][reports][exceptions]")
{
    Pos p;
    p.login("1234");                                   // Morgan, a manager
    p.openDrawer("10000");
    REQUIRE(p.pos.noSale());
    REQUIRE(p.pos.noSale());

    REQUIRE(p.pos.startCheck(core::CheckType::Quick));
    p.pos.addItem(u"cobb"_s);                          // 12.50
    p.pos.addItem(u"caesar"_s);                        // 9.75
    p.pos.finishChoosing();
    REQUIRE(p.pos.sendOrder());
    REQUIRE(p.pos.voidItem());                         // the Caesar, sent: a void worth 9.75
    REQUIRE(p.pos.tender(u"comp"_s));                  // the Cobb comped: 12.50
    REQUIRE(p.pos.removePayment());                    // ...then not
    p.entry("2000");
    REQUIRE(p.pos.tender(u"cash"_s));
    REQUIRE(p.pos.closeCheck());

    const core::Report r = p.pos.buildReport(u"exceptions"_s);

    const std::string morgan = "Morgan (Manager)";
    CHECK(rowIn(r, "Voids (items already sent)", morgan) == std::vector<std::string>{morgan, "1", "$9.75"});
    CHECK(rowIn(r, "Discounts and comps", morgan) == std::vector<std::string>{morgan, "1", "$12.50"});
    CHECK(rowIn(r, "Payments and discounts taken back", morgan) == std::vector<std::string>{morgan, "1", "$12.50"});
    CHECK(rowIn(r, "Drawer opened with no sale", morgan) == std::vector<std::string>{morgan, "2", ""});
    // The exception tickets kept as pictures: the check (void, discount) and both No Sales.
    const auto tickets = p.pos.exceptionTickets();
    REQUIRE(tickets.size() == 3);
    CHECK(tickets[0].check.has_value());
    CHECK(tickets[0].title.find("void") != std::string::npos);
    CHECK(tickets[0].title.find("discount") != std::string::npos);
    CHECK(tickets[1].file == "nosale-1");
    CHECK(tickets[2].number == 2);
    // The drawer report counts the no sales too, without a dollar line.
    CHECK(!rowIn(p.pos.buildReport(u"drawer"_s), p.pos.shared()->drawers.front().name, "Opened with no sale (2)").empty());
    // Events keep their amounts.
    const core::Check &c = p.pos.shared()->closedToday.back();
    CHECK(app::checkFromJson(app::toJson(c))->events == c.events);
}

TEST_CASE("No Sale on a screen asks why with buttons; the Exceptions report counts the reasons",
          "[m4][reports][exceptions][reasons]")
{
    Pos p;
    p.login("1234");
    p.openDrawer("10000");
    const int kicks = p.printer.drawerKicks;
    const auto run = [&](const QString &method) {
        bool ok = false;
        p.pos.invoke(method, {}, [&](const QVariant &r) { ok = r.toBool(); });
        return ok;
    };
    // Asks first: the drawer stays shut until answered.
    CHECK_FALSE(run(u"noSale"_s));
    QVariantMap ask = p.pos.reasonAsk();
    REQUIRE(ask[u"kind"_s] == u"noSale"_s);
    CHECK(ask[u"reasons"_s].toStringList().contains(u"Make Change"_s));
    CHECK(p.printer.drawerKicks == kicks);
    // Cancel: still shut, nothing recorded.
    REQUIRE(p.pos.giveReason(QString()));
    CHECK(p.printer.drawerKicks == kicks);
    // A reason: it opens, with the reason kept.
    CHECK_FALSE(run(u"noSale"_s));
    REQUIRE(p.pos.giveReason(u"Make Change"_s));
    CHECK(p.pos.reasonAsk().isEmpty());
    CHECK(p.printer.drawerKicks == kicks + 1);
    const core::CashMovement &m = p.pos.shared()->drawers.front().movements.back();
    CHECK(m.kind == core::CashMovement::Kind::NoSale);
    CHECK(m.reason == "Make Change");
    const core::Report r = p.pos.buildReport(u"exceptions"_s);
    CHECK(rowIn(r, "No sale: why", "Make Change") == std::vector<std::string>{"Make Change", "1", ""});

    // No reasons in Store Settings: it opens without asking.
    p.pos.shared()->settings.noSaleReasons.clear();
    CHECK(run(u"noSale"_s));
    CHECK(p.printer.drawerKicks == kicks + 2);
}

TEST_CASE("Cancel Sale asks why, numbers the cancel for the day, isn't a sale, and is an exception",
          "[m4][reports][exceptions][cancel]")
{
    Pos p;
    p.login("1234");
    p.openDrawer("10000");
    const auto run = [&](const QString &method) {
        bool ok = false;
        p.pos.invoke(method, {}, [&](const QVariant &r) { ok = r.toBool(); });
        return ok;
    };
    REQUIRE(p.pos.startCheck(core::CheckType::Quick));
    REQUIRE(p.pos.addItem(u"cobb"_s));
    CHECK_FALSE(run(u"cancelSale"_s));
    REQUIRE(p.pos.reasonAsk()[u"kind"_s] == u"cancel"_s);
    CHECK(p.pos.reasonAsk()[u"reasons"_s].toStringList().contains(u"No ID"_s));
    REQUIRE(p.pos.giveReason(u"No ID"_s));
    REQUIRE(p.pos.shared()->cancelledToday.size() == 1);
    CHECK(p.pos.shared()->cancelledToday.front().cancelNumber == 1);
    CHECK(p.pos.shared()->closedToday.empty());   // not a sale
    CHECK(p.pos.shared()->open.empty());

    REQUIRE(p.pos.startCheck(core::CheckType::Quick));
    REQUIRE(p.pos.addItem(u"caesar"_s));
    CHECK_FALSE(run(u"cancelSale"_s));
    REQUIRE(p.pos.giveReason(u"Price"_s));
    CHECK(p.pos.shared()->cancelledToday.back().cancelNumber == 2);

    // Paid: take the payment back first.
    REQUIRE(p.pos.startCheck(core::CheckType::Quick));
    REQUIRE(p.pos.addItem(u"cobb"_s));
    p.entry("2000");
    REQUIRE(p.pos.tender(u"cash"_s));
    CHECK_FALSE(p.pos.cancelSale());

    const core::Report r = p.pos.buildReport(u"exceptions"_s);
    CHECK(rowIn(r, "Cancelled sales", "Morgan (Manager)")[1] == "2");
    CHECK(!rowIn(r, "Cancelled sales by reason", "No ID").empty());
    const auto tickets = p.pos.exceptionTickets();
    CHECK(std::ranges::count_if(tickets, [](const auto &t) { return t.file.rfind("cancel-", 0) == 0; }) == 2);
    CHECK(p.pos.buildReport(u"sales"_s).find("Checks").at(1) == "0");
}

TEST_CASE("Retail open departments: MISC MDSE and MISC FOOD/BEV ring the amount typed, in their department and tax",
          "[m4][retail][opendept]")
{
    Pos p;
    p.login("1234");
    core::TaxRates &tax = p.pos.shared()->settings.tax;
    tax.merchandisePpm = 63500;   // a merchandise rate and a meals rate (made up for the test)
    tax.foodPpm = 73500;
    QStringList said;
    QObject::connect(&p.pos, &PosService::notice, [&](const QString &m) { said << m; });

    // No amount typed: nothing rung in.
    CHECK_FALSE(p.pos.ringOpenDepartment(u"Misc Merchandise"_s, u"merchandise"_s));
    REQUIRE_FALSE(said.isEmpty());
    CHECK(said.last().startsWith(u"Type the amount"_s));
    CHECK(p.pos.shared()->open.empty());

    const std::size_t menuBefore = p.pos.shared()->menu.size();
    p.entry("2000");
    REQUIRE(p.pos.ringOpenDepartment(u"Misc Merchandise"_s, u"merchandise"_s));
    CHECK(p.pos.entryAmount() == p.pos.format(Money()));   // the amount is used up
    p.entry("2000");
    REQUIRE(p.pos.ringOpenDepartment(u"Misc Food/Bev"_s, u"food"_s));
    REQUIRE(p.pos.shared()->open.size() == 1);
    core::Check &c = p.pos.shared()->open.begin()->second;
    REQUIRE(c.lines.size() == 2);
    CHECK(c.lines[0].name == "Misc Merchandise");
    CHECK(c.lines[0].unitPrice.cents() == 2000);
    CHECK(c.lines[0].taxClass == core::TaxClass::Merchandise);
    CHECK(c.lines[1].name == "Misc Food/Bev");
    CHECK(c.lines[1].taxClass == core::TaxClass::Food);
    CHECK_FALSE(c.lines[0].forKitchen());   // the customer has it: no ticket
    const core::Totals t = c.totals(tax);
    CHECK(t.taxByClass.at(core::TaxClass::Merchandise).cents() == 127);   // 6.35% of $20.00
    CHECK(t.taxByClass.at(core::TaxClass::Food).cents() == 147);          // 7.35% of $20.00

    // Each is a department (a Retail-only category) with one item, made once and saved.
    CHECK(p.pos.shared()->menu.size() == menuBefore + 2);
    const auto &cats = p.pos.shared()->settings.menuCategories;
    const auto dept = std::ranges::find_if(cats, [](const core::MenuCategory &m) { return m.name == "Misc Food/Bev"; });
    REQUIRE(dept != cats.end());
    CHECK(dept->retailOnly);
    CHECK(p.sink.menu.contains("open-dept-misc-food-bev"));
    p.entry("500");
    REQUIRE(p.pos.ringOpenDepartment(u"misc merchandise"_s, u"merchandise"_s));
    CHECK(p.pos.shared()->menu.size() == menuBefore + 2);   // the same department and item again
    REQUIRE(c.lines.size() == 3);
    CHECK(c.lines[2].unitPrice.cents() == 500);

    // Looked up by name (ALPHA LOOK UP): the same thing, at the amount typed.
    p.entry("300");
    REQUIRE(p.pos.addItem(u"Misc Food/Bev"_s));
    REQUIRE(c.lines.size() == 4);
    CHECK(c.lines[3].unitPrice.cents() == 300);
    CHECK(c.lines[3].taxClass == core::TaxClass::Food);

    // No meals rate set (Store Settings > Taxes > Food tax % at 0): the sales tax rate, never untaxed.
    tax.foodPpm = 0;
    p.entry("1000");
    REQUIRE(p.pos.ringOpenDepartment(u"Misc Food/Bev"_s, u"food"_s));
    REQUIRE(c.lines.size() == 5);
    CHECK(c.lines[4].taxClass == core::TaxClass::Merchandise);
}

TEST_CASE("Retail VOID ITEM takes off only the touched line; RESTORE ITEM brings it back, no exception",
          "[m4][retail][void][exceptions]")
{
    Pos p;
    p.login("1234");   // Morgan, a manager
    p.openDrawer("10000");
    QStringList said;
    QObject::connect(&p.pos, &PosService::notice, [&](const QString &m) { said << m; });
    const std::string morgan = "Morgan (Manager)";
    const std::string corrects = "Error corrects (retail, before paying)";
    const auto exceptionTicketsFor = [&](std::int64_t id) {
        const auto tickets = p.pos.exceptionTickets();
        return std::ranges::count_if(tickets, [&](const auto &t) { return t.file == "check-" + std::to_string(id); });
    };

    REQUIRE(p.pos.startCheck(core::CheckType::Quick));
    REQUIRE(p.pos.addItem(u"cobb"_s));
    REQUIRE(p.pos.addItem(u"caesar"_s));
    p.pos.finishChoosing();
    core::Check &c = p.pos.shared()->open.begin()->second;
    REQUIRE(c.lines.size() == 2);
    const Money full = c.totals(p.pos.shared()->settings.tax).total;

    // Nothing touched: a message, and nothing taken off (not even the newest).
    said.clear();
    CHECK_FALSE(p.pos.errorCorrect());
    REQUIRE_FALSE(said.isEmpty());
    CHECK(said.last() == u"Touch the item on the check first, then VOID ITEM."_s);
    CHECK(std::ranges::none_of(c.lines, [](const core::OrderLine &l) { return l.voided; }));
    CHECK_FALSE(p.pos.lines()[1].toMap()[u"touched"_s].toBool());   // the newest is not lit as touched

    // The touched line, not the newest; then nothing is touched any more.
    p.pos.selectLine(c.lines[0].id);
    CHECK(p.pos.lines()[0].toMap()[u"touched"_s].toBool());
    REQUIRE(p.pos.errorCorrect());
    CHECK(c.lines[0].voided);
    CHECK(c.lines[0].corrected);
    CHECK_FALSE(c.lines[1].voided);
    CHECK(p.pos.selectedLine() == 0);
    CHECK_FALSE(p.pos.errorCorrect());
    CHECK_FALSE(c.lines[1].voided);
    CHECK(rowIn(p.pos.buildReport(u"exceptions"_s), corrects, morgan) == std::vector<std::string>{morgan, "1", "$12.50"});

    // The struck-through line touched: the same key restores it, as it was rung.
    p.pos.selectLine(c.lines[0].id);
    REQUIRE(p.pos.restoreLine());
    CHECK_FALSE(c.lines[0].voided);
    CHECK_FALSE(c.lines[0].corrected);
    CHECK(c.totals(p.pos.shared()->settings.tax).total == full);
    CHECK(std::ranges::any_of(c.events, [](const core::CheckEvent &e) { return e.what.starts_with("Restored "); }));
    CHECK(std::ranges::any_of(c.events, [](const core::CheckEvent &e) { return e.what.starts_with("Error correct "); }));
    // Void then restore cancels out: no exception, no exception ticket.
    CHECK(rowIn(p.pos.buildReport(u"exceptions"_s), corrects, morgan).empty());
    CHECK(exceptionTicketsFor(c.id) == 0);

    // Through the void key itself (errorCorrect on a struck-through line), then voided again: one exception.
    p.pos.selectLine(c.lines[0].id);
    REQUIRE(p.pos.errorCorrect());
    p.pos.selectLine(c.lines[0].id);
    REQUIRE(p.pos.errorCorrect());   // RESTORE ITEM
    CHECK_FALSE(c.lines[0].voided);
    CHECK(rowIn(p.pos.buildReport(u"exceptions"_s), corrects, morgan).empty());
    p.pos.selectLine(c.lines[0].id);
    REQUIRE(p.pos.errorCorrect());   // voided again, and left so
    CHECK(c.lines[0].voided);
    CHECK(rowIn(p.pos.buildReport(u"exceptions"_s), corrects, morgan) == std::vector<std::string>{morgan, "1", "$12.50"});
    CHECK(exceptionTicketsFor(c.id) == 1);

    // Paid: no restoring until the payment is taken back.
    const std::int64_t id = c.id;
    const std::int64_t voidedLine = c.lines[0].id;
    p.entry("100");
    REQUIRE(p.pos.tender(u"cash"_s));
    p.pos.selectLine(voidedLine);
    CHECK_FALSE(p.pos.restoreLine());
    CHECK(c.lines[0].voided);
    // Closed: no restoring at all.
    REQUIRE(p.pos.removePayment());
    p.entry("5000");
    REQUIRE(p.pos.tender(u"cash"_s));
    REQUIRE(p.pos.closeCheck());
    p.pos.selectLine(voidedLine);
    CHECK_FALSE(p.pos.restoreLine());
    const auto closed = std::ranges::find(p.pos.shared()->closedToday, id, &core::Check::id);
    REQUIRE(closed != p.pos.shared()->closedToday.end());
    CHECK(closed->lines[0].voided);
}

TEST_CASE("Cancel Sale closes a sale whose items were all error-corrected: not left open", "[m4][retail][cancel]")
{
    Pos p;
    p.login("1234");
    p.openDrawer("10000");
    REQUIRE(p.pos.startCheck(core::CheckType::Quick));
    REQUIRE(p.pos.addItem(u"cobb"_s));
    REQUIRE(p.pos.addItem(u"caesar"_s));
    p.pos.finishChoosing();
    core::Check &c = p.pos.shared()->open.begin()->second;
    const std::int64_t id = c.id;
    for (const std::int64_t line : {c.lines[0].id, c.lines[1].id}) {
        p.pos.selectLine(line);
        REQUIRE(p.pos.errorCorrect());
    }
    CHECK_FALSE(p.pos.cancelSale());   // asks why
    REQUIRE(p.pos.reasonAsk()[u"kind"_s] == u"cancel"_s);
    REQUIRE(p.pos.giveReason(u"Price"_s));
    CHECK(p.pos.shared()->open.empty());             // not put away (suspended) for later
    CHECK_FALSE(p.pos.hasCheck());                   // the register is at an empty sale
    REQUIRE(p.pos.shared()->cancelledToday.size() == 1);
    CHECK(p.pos.shared()->cancelledToday.front().id == id);
    CHECK(p.pos.shared()->cancelledToday.front().status == core::CheckStatus::Discarded);
    CHECK(p.pos.shared()->closedToday.empty());      // not a sale

    // A sale with nothing rung in at all: just gone, no Cancel number used.
    REQUIRE(p.pos.startCheck(core::CheckType::Quick));
    REQUIRE(p.pos.cancelSale());
    CHECK(p.pos.shared()->open.empty());
    CHECK(p.pos.shared()->cancelledToday.size() == 1);
}

TEST_CASE("Price check: on and off, a scan shows the item and its box or singles", "[m4][pricecheck]")
{
    Pos p;
    p.login("1234");
    std::vector<core::MenuItem *> items;
    for (core::MenuItem &m : p.pos.shared()->menu)
        if (!m.isModifier && items.size() < 2)
            items.push_back(&m);
    REQUIRE(items.size() == 2);
    core::MenuItem &box = *items[0], &single = *items[1];
    box.barcodes = {"012345678905"};
    single.barcodes = {"098765432109"};
    single.recipe.push_back({"__parent__:012345678905", 20});
    REQUIRE(p.pos.togglePriceCheck());
    CHECK(p.pos.priceCheck()[u"on"_s].toBool());
    REQUIRE(p.pos.priceCheckCode(u"01234567890"_s));        // without its check digit
    QVariantMap item = p.pos.priceCheck()[u"item"_s].toMap();
    CHECK(item[u"name"_s].toString() == QString::fromStdString(box.name));
    QVariantList related = item[u"related"_s].toList();
    REQUIRE(related.size() == 1);
    CHECK(related[0].toMap()[u"relation"_s].toString().contains(u"20"_s));
    REQUIRE(p.pos.priceCheckCode(u"098765432109"_s));
    related = p.pos.priceCheck()[u"item"_s].toMap()[u"related"_s].toList();
    REQUIRE(related.size() == 1);
    CHECK(related[0].toMap()[u"relation"_s].toString().startsWith(u"Box"_s));
    CHECK(p.pos.shared()->open.empty());                       // nothing rung in
    p.pos.closePriceCheck();
    CHECK_FALSE(p.pos.priceCheck().contains(u"item"_s));
    REQUIRE(p.pos.togglePriceCheck());
    CHECK(p.pos.priceCheck().isEmpty());
}

TEST_CASE("X report: sales and drawers so far on paper, nothing closed", "[m4][reports][xreport]")
{
    Pos p;
    p.login("1234");
    p.openDrawer("10000");
    p.cashSale("cobb", "2000");
    const auto reports = p.printer.reports.size();
    REQUIRE(p.pos.xReport());
    REQUIRE(p.printer.reports.size() == reports + 1);
    CHECK(p.printer.reports.back() == "X REPORT");
    const core::Report x = p.pos.buildXReport();
    CHECK(std::ranges::count_if(x.rows, [](const core::ReportRow &r) { return r.kind == core::ReportRow::Kind::Section; }) == 2);
    // The drawer stays open and the day goes on.
    CHECK(p.pos.shared()->drawers.front().open());
    CHECK(p.pos.shared()->closedToday.size() == 1);
}

TEST_CASE("Deposit report: cash to the bank, card batch, and the books balance", "[m4][reports][deposit]")
{
    Pos p;
    p.login("1234");
    p.openDrawer("10000");                             // $100 starting cash
    REQUIRE(p.pos.startCheck(core::CheckType::Quick));
    p.pos.addItem(u"cobb"_s);
    p.entry("5000");
    REQUIRE(p.pos.tender(u"cash"_s));                  // $50 cash, change back
    REQUIRE(p.pos.closeCheck());
    const Money cashSale = p.pos.shared()->closedToday.back().totals(p.pos.shared()->settings.tax).cashNet();

    REQUIRE(p.pos.startCheck(core::CheckType::Quick));
    p.pos.addItem(u"caesar"_s);
    p.pos.finishChoosing();
    REQUIRE(p.pos.tender(u"credit"_s));
    REQUIRE(p.pos.closeCheck());

    core::Report r = p.pos.buildReport(u"deposit"_s);
    const std::string drawer = p.pos.shared()->drawers.front().name;
    CHECK(rowIn(r, "Cash to take to the bank", "  keep as starting cash")[1] == "-$100.00");
    CHECK(rowIn(r, "Cash to take to the bank", "Cash deposit")[1] == p.pos.format(cashSale).toStdString());
    CHECK(rowIn(r, "Book balance", "Balanced").size() == 2);

    // Counted $1 short: the deposit is what's there.
    p.entry(QString::number(10000 + cashSale.cents() - 100).toLatin1().constData());
    REQUIRE(p.pos.countDrawer());
    r = p.pos.buildReport(u"deposit"_s);
    CHECK(rowIn(r, "Cash to take to the bank", "Cash deposit")[1]
          == p.pos.format(cashSale - Money::fromCents(100)).toStdString());
    CHECK(rowIn(r, "Book balance", "Drawers short")[1] == "-$1.00");
    CHECK(!rowIn(r, "Cards to settle", "Card batch").empty());
}

TEST_CASE("Royalty and accounting: fees on net sales; a balanced journal by account", "[m4][reports][accounting]")
{
    Pos p;
    p.login("1234");
    p.openDrawer("10000");
    QVariantMap store = p.pos.adminRecords(u"store"_s).first().toMap();
    store[u"royaltyPercent"_s] = u"5"_s;
    store[u"adFundPercent"_s] = u"2.5"_s;
    store[u"chartOfAccounts"_s] = u"sales = 4000 Food sales\ntender:cash = 1000 Cash\ntax:food = 2200 Sales tax\n"
                                  u"tender:credit = 1100 Card clearing\ntips = 2300 Tips payable\ndiscounts = 4900 Comps"_s;
    REQUIRE(p.pos.adminSave(u"store"_s, 0, store));
    CHECK(p.pos.shared()->settings.royaltyBp == 500);
    CHECK(p.pos.shared()->settings.adFundBp == 250);
    store[u"chartOfAccounts"_s] = u"no equals sign"_s;
    CHECK_FALSE(p.pos.adminSave(u"store"_s, 0, store));

    REQUIRE(p.pos.startCheck(core::CheckType::Quick));
    p.pos.addItem(u"cobb"_s);                          // 12.50, cash
    p.entry("2000");
    REQUIRE(p.pos.tender(u"cash"_s));
    REQUIRE(p.pos.closeCheck());
    REQUIRE(p.pos.startCheck(core::CheckType::Quick));
    p.pos.addItem(u"caesar"_s);                        // 9.75, 10% off, card with a tip
    p.pos.finishChoosing();
    REQUIRE(p.pos.tender(u"discount"_s));
    REQUIRE(p.pos.tender(u"credit"_s));
    p.entry("200");
    REQUIRE(p.pos.addTip(0));
    REQUIRE(p.pos.closeCheck());

    const core::Report royalty = p.pos.buildReport(u"royalty"_s);
    const Money net = Money::fromCents(1250 + 975 - 98);   // 10% of 9.75 = 0.98 (rounded)
    CHECK(rowIn(royalty, "Sales", "Net sales (no tax, no gift cards sold)")[1] == p.pos.format(net).toStdString());
    CHECK(rowIn(royalty, "Owed to the franchise", "Royalty (5%)")[1] == p.pos.format(net.percent(500)).toStdString());
    CHECK(rowIn(royalty, "Owed to the franchise", "Advertising fund (2.50%)")[1]
          == p.pos.format(net.percent(250)).toStdString());

    const core::Report journal = p.pos.buildReport(u"accounting"_s);
    const auto total = rowIn(journal, "Journal", "Total");
    REQUIRE(total.size() == 3);
    CHECK(total[1] == total[2]);                       // debits = credits
    CHECK(!rowIn(journal, "Journal", "1000 Cash").empty());
    CHECK(rowIn(journal, "Journal", "2300 Tips payable")[2] == "$2.00");
    CHECK(rowIn(journal, "Journal", "4000 Food sales (salads)")[2] == "$22.25");
    CHECK(!rowIn(journal, "Journal", "4900 Comps").empty());
}
