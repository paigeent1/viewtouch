#include <catch2/catch_test_macros.hpp>

#include "core/report.hh"
#include "pos_fixture.hh"
#include "print/document.hh"
#include "print/tickets.hh"
#include "qt_catch.hh"

#include <QDateTime>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// The Time Clock report (Manager > Reports > Time Clock): the owner's ILSA printout's layout and math.
// Made-up people only.

namespace {

std::int64_t at(int month, int day, int h, int m, int s = 0)
{
    return QDateTime(QDate(2026, month, day), QTime(h, m, s)).toMSecsSinceEpoch();
}

core::TimePunch punch(std::int64_t id, const std::string &who, std::int64_t in, std::int64_t out)
{
    core::TimePunch p;
    p.id = id;
    p.employeeId = who;
    p.clockIn = in;
    p.clockOut = out;
    p.job = "cashier";
    return p;
}

core::Employee person(const std::string &id, const std::string &name)
{
    core::Employee e;
    e.id = id;
    e.name = name;
    return e;
}

core::ReportContext context(const core::PosSettings &s, std::int64_t now)
{
    return {s, "", [](std::int64_t) { return std::string("9:00 PM"); }, now};
}

// The row whose first cell is `first` (any kind); empty if none.
std::vector<std::string> row(const core::Report &r, const std::string &first)
{
    for (const core::ReportRow &x : r.rows)
        if (!x.cells.empty() && x.cells.front() == first)
            return x.cells;
    return {};
}

bool hasNote(const core::Report &r, const std::string &start)
{
    for (const core::ReportRow &x : r.rows)
        if (x.kind == core::ReportRow::Kind::Note && !x.cells.empty() && x.cells.front().find(start) != std::string::npos)
            return true;
    return false;
}

bool hasSection(const core::Report &r, const std::string &text)
{
    for (const core::ReportRow &x : r.rows)
        if (x.kind == core::ReportRow::Kind::Section && x.cells.front() == text)
            return true;
    return false;
}

// The week on the owner's printout: employee 78's five punches.
std::vector<core::TimePunch> printoutWeek()
{
    return {
        punch(1, "78", at(11, 3, 8, 0, 57), at(11, 3, 14, 52, 20)),   // 06:51
        punch(2, "78", at(11, 4, 8, 6, 37), at(11, 4, 11, 13, 22)),   // 3:06:45: 03:07 (to the nearest minute, as ILSA)
        punch(3, "78", at(11, 5, 7, 45, 10), at(11, 5, 19, 58, 30)),  // 12:13
        punch(4, "78", at(11, 6, 7, 50, 0), at(11, 6, 20, 0, 20)),    // 12:10
        punch(5, "78", at(11, 7, 9, 0, 5), at(11, 7, 18, 36, 29)),    // 09:36
    };
}

} // namespace

TEST_CASE("Time Clock: hours and minutes, decimal hours, the owner's numbers", "[reports][timeclock]")
{
    // Floored minutes as HH:MM; minutes / 60 to two places.
    CHECK(core::clockHours(411) == "06:51");
    CHECK(core::decimalHours(411) == "6.85");
    CHECK(core::decimalHours(187) == "3.12");    // 3:06:45: 03:07 (to the nearest minute, as ILSA)
    CHECK(core::decimalHours(733) == "12.22");   // 12:13
    CHECK(core::decimalHours(730) == "12.17");   // 12:10
    CHECK(core::decimalHours(576) == "9.60");    // 09:36
    CHECK(core::clockHours(2637) == "43:57");
    CHECK(core::decimalHours(2637) == "43.95");
    CHECK(core::clockHours(0) == "00:00");
    CHECK(core::decimalHours(1) == "0.02");

    core::PosSettings s;
    s.storeName = "Example Tobacco";
    s.storeAddress = "12 Sample Street\nExampleton CT, 06000";
    s.storePhone = "(555) 555-0100";
    core::TimeClockOptions o;
    o.from = at(11, 2, 0, 0);
    o.to = at(11, 9, 0, 0);
    const core::Report r = core::timeClockReport(printoutWeek(), {person("78", "Pat Example")}, context(s, at(11, 9, 16, 55, 10)), o);

    CHECK(r.id == "timeclock");
    CHECK(r.title == "Time Clock Report");
    CHECK(r.subtitle == "11/02/2026 12:00AM thru 11/09/2026 12:00AM");
    CHECK(r.columns == std::vector<std::string>{"Begin Date and Time", "End Date and Time", "Hours & Minutes", "Total Hours"});
    CHECK_FALSE(row(r, "Store: Example Tobacco").empty());
    CHECK_FALSE(row(r, "Address: 12 Sample Street").empty());
    CHECK_FALSE(row(r, "          Exampleton CT, 06000").empty());
    CHECK_FALSE(row(r, "Phone: (555) 555-0100").empty());
    CHECK_FALSE(row(r, "Report printed: 11/09/2026 4:55:10 PM").empty());
    CHECK(hasSection(r, "Employee #: 78    Name: Pat Example"));

    CHECK(row(r, "11/03/2026 08:00:57 am")
          == std::vector<std::string>{"11/03/2026 08:00:57 am", "11/03/2026 02:52:20 pm", "06:51", "6.85"});
    CHECK(row(r, "11/04/2026 08:06:37 am")
          == std::vector<std::string>{"11/04/2026 08:06:37 am", "11/04/2026 11:13:22 am", "03:07", "3.12"});
    CHECK(row(r, "11/05/2026 07:45:10 am")[2] == "12:13");
    CHECK(row(r, "11/05/2026 07:45:10 am")[3] == "12.22");
    CHECK(row(r, "11/06/2026 07:50:00 am")[2] == "12:10");
    CHECK(row(r, "11/06/2026 07:50:00 am")[3] == "12.17");
    CHECK(row(r, "11/07/2026 09:00:05 am")[2] == "09:36");
    CHECK(row(r, "11/07/2026 09:00:05 am")[3] == "9.60");
    // The totals come from the minutes (2637 = 43:57 = 43.95), not the rounded decimals (43.96).
    CHECK(row(r, "Total") == std::vector<std::string>{"Total", "", "43:57", "43.95"});
    CHECK(row(r, "Grand Total Hours:") == std::vector<std::string>{"Grand Total Hours:", "", "43:57", "43.95"});
    CHECK_FALSE(hasNote(r, "*edited"));
    CHECK_FALSE(hasNote(r, "(still clocked in)"));

    // No address in Store Settings: the receipt header's lines.
    core::PosSettings bare;
    bare.receiptHeader = "1 Example Road\nSampletown NY";
    const core::Report r2 = core::timeClockReport(printoutWeek(), {}, context(bare, at(11, 9, 12, 0)), o);
    CHECK_FALSE(row(r2, "Address: 1 Example Road").empty());
    CHECK(hasSection(r2, "Employee #: 78    Name: 78"));   // not on file: the number
}

TEST_CASE("Time Clock: breaks, still clocked in, edited punches, one employee", "[reports][timeclock]")
{
    core::PosSettings s;
    std::vector<core::TimePunch> punches = printoutWeek();
    // Lee: 9:00 to 5:00 with an unpaid half hour: 7:30 paid.
    core::TimePunch lee = punch(10, "102", at(11, 3, 9, 0), at(11, 3, 17, 0));
    lee.breaks.push_back({at(11, 3, 12, 0), at(11, 3, 12, 30)});
    punches.push_back(lee);
    // Lee again, still on the clock.
    punches.push_back(punch(11, "102", at(11, 8, 10, 0), 0));
    // A manager fixed Pat's 11/04 punch.
    s.punchChanges.push_back({at(11, 5, 9, 0), 2, "Morgan", "Pat Example", "clock out 11:13 AM", "forgot to clock out"});
    const std::vector<core::Employee> staff{person("78", "Pat Example"), person("102", "Lee Sample")};
    core::TimeClockOptions o;
    o.from = at(11, 2, 0, 0);
    o.to = at(11, 9, 0, 0);
    const std::int64_t now = at(11, 8, 15, 0);
    const core::Report r = core::timeClockReport(punches, staff, context(s, now), o);

    // By employee number: 78, then 102.
    std::vector<std::string> sections;
    for (const core::ReportRow &x : r.rows)
        if (x.kind == core::ReportRow::Kind::Section)
            sections.push_back(x.cells.front());
    CHECK(sections == std::vector<std::string>{"Employee #: 78    Name: Pat Example", "Employee #: 102    Name: Lee Sample"});

    // Unpaid break: listed, and off the hours (as the Labor report counts it).
    CHECK(row(r, "11/03/2026 09:00:00 am") == std::vector<std::string>{"11/03/2026 09:00:00 am", "11/03/2026 05:00:00 pm", "07:30", "7.50"});
    CHECK(hasNote(r, "Unpaid break 12:00:00 pm - 12:30:00 pm (00:30), not counted"));
    CHECK(lee.workedMs(now, s.paidBreaks) / 60'000 == 450);
    // Still clocked in: shown, not counted.
    CHECK(row(r, "11/08/2026 10:00:00 am") == std::vector<std::string>{"11/08/2026 10:00:00 am", "(still clocked in)", "--:--", "--"});
    CHECK(hasNote(r, "(still clocked in): no clock-out yet, so not in the totals."));
    // Edited: marked, with the footnote.
    CHECK(row(r, "11/04/2026 08:06:37 am *edited")[2] == "03:07");
    CHECK(hasNote(r, "*edited: a manager changed this punch"));
    // Totals: Pat 43:57, Lee 07:30 (the open punch left out), everyone 51:27 = 51.45.
    std::vector<std::vector<std::string>> totals;
    for (const core::ReportRow &x : r.rows)
        if (x.kind == core::ReportRow::Kind::Total)
            totals.push_back(x.cells);
    REQUIRE(totals.size() == 3);
    CHECK(totals[0] == std::vector<std::string>{"Total", "", "43:57", "43.95"});
    CHECK(totals[1] == std::vector<std::string>{"Total", "", "07:30", "7.50"});
    CHECK(totals[2] == std::vector<std::string>{"Grand Total Hours:", "", "51:27", "51.45"});

    // Paid breaks: listed and counted.
    core::PosSettings paid = s;
    paid.paidBreaks = true;
    const core::Report rp = core::timeClockReport({lee}, staff, context(paid, now), o);
    CHECK(row(rp, "11/03/2026 09:00:00 am")[2] == "08:00");
    CHECK(hasNote(rp, "Paid break 12:00:00 pm - 12:30:00 pm (00:30), counted"));

    // One employee: only Lee, and the grand total is Lee's.
    o.employeeId = "102";
    const core::Report one = core::timeClockReport(punches, staff, context(s, now), o);
    CHECK_FALSE(hasSection(one, "Employee #: 78    Name: Pat Example"));
    CHECK(hasSection(one, "Employee #: 102    Name: Lee Sample"));
    CHECK(row(one, "Grand Total Hours:") == std::vector<std::string>{"Grand Total Hours:", "", "07:30", "7.50"});
    CHECK(row(one, "11/03/2026 08:00:57 am").empty());
    // Someone with nothing on these days.
    o.employeeId = "999";
    CHECK(hasNote(core::timeClockReport(punches, staff, context(s, now), o), "999 didn't clock in on these days."));
}

TEST_CASE("Time Clock on 80 mm paper: two lines a punch, nothing too wide", "[reports][timeclock][print]")
{
    core::PosSettings s;
    s.storeName = "Example Tobacco";
    s.storeAddress = "12 Sample Street\nExampleton CT, 06000";
    s.storePhone = "(555) 555-0100";
    s.punchChanges.push_back({at(11, 5, 9, 0), 2, "Morgan", "Pat Example", "clock out", "forgot"});
    std::vector<core::TimePunch> punches = printoutWeek();
    punches.push_back(punch(6, "78", at(11, 7, 22, 0), at(11, 8, 1, 30)));   // past midnight: 03:30
    punches.push_back(punch(7, "78", at(11, 8, 9, 0), 0));
    core::TimeClockOptions o;
    o.from = at(11, 2, 0, 0);
    o.to = at(11, 9, 0, 0);
    o.paper = true;
    const core::Report r = core::timeClockReport(punches, {person("78", "Pat Example")}, context(s, at(11, 8, 12, 0)), o);
    CHECK(row(r, "In  11/03/2026 08:00:57 am").size() == 1);
    CHECK(row(r, "Out 02:52:20 pm") == std::vector<std::string>{"Out 02:52:20 pm", "06:51", "6.85"});
    CHECK_FALSE(row(r, "In  11/04/2026 08:06:37 am *edited").empty());
    CHECK(row(r, "Out 11/08/2026 01:30:00 am")[1] == "03:30");   // another day: its date too
    CHECK(row(r, "Out (still clocked in)") == std::vector<std::string>{"Out (still clocked in)", "--:--", "--"});
    CHECK(row(r, "Grand Total Hours:") == std::vector<std::string>{"Grand Total Hours:", "47:27", "47.45"});
    CHECK(row(r, "Store: Example Tobacco").empty());   // the ticket's own heading has it

    const core::PosSettings plain;
    const print::TicketContext tc{plain, [](std::int64_t) { return std::string("Nov 8, 2026 12:00 PM"); },
                                  [](std::int64_t) { return std::string("12:00 PM"); }, 0};
    const std::string text = print::renderText(print::reportTicket(r, tc), 42);
    std::size_t start = 0, widest = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        widest = std::max(widest, print::displayWidth(text.substr(start, end - start)));
        start = end + 1;
    }
    CHECK(widest <= 42);
    CHECK(text.find("Out 02:52:20 pm") != std::string::npos);
    CHECK(text.find("06:51     6.85") != std::string::npos);   // the hours on the Out line
    CHECK(text.find("47:27    47.45") != std::string::npos);
}

namespace {

struct ReportPrinter : app::PosPrinter {
    std::vector<core::Report> printed;
    void printKitchen(const core::PosSettings &, const core::Check &, const std::vector<core::OrderLine> &, bool) override {}
    void printReceipt(const core::PosSettings &, const core::Check &, const std::string &) override {}
    void printReport(const core::PosSettings &, const core::Report &r, const std::string &) override { printed.push_back(r); }
    void openDrawer(const core::PosSettings &, const std::string &) override {}
};

bool waitForRange(const PosService &pos)
{
    for (int i = 0; i < 500 && pos.rangeReport()[u"loading"_s].toBool(); ++i)
        QTest::qWait(20);
    return !pos.rangeReport()[u"loading"_s].toBool();
}

bool hasRow(const QVariantMap &range, const QString &first)
{
    for (const QVariant &v : range[u"report"_s].toMap()[u"rows"_s].toList()) {
        const QStringList cells = v.toMap()[u"cells"_s].toStringList();
        if (!cells.isEmpty() && cells.first() == first)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("Time Clock over days: punches from the store and memory, one person, printed", "[reports][timeclock][range]")
{
    PosService pos(test::seedPosData(), nullptr);
    ReportPrinter printer;
    pos.setPrinter(&printer);
    const QDate today = QDate::currentDate();
    const auto dayAt = [&](int back, int h, int m) {
        return QDateTime(today.addDays(-back), QTime(h, m)).toMSecsSinceEpoch();
    };
    // In the store: Sam three days ago, Riley four; one long before the days asked for.
    const std::vector<core::TimePunch> stored{punch(901, "sam", dayAt(3, 9, 0), dayAt(3, 15, 0)),
                                             punch(902, "riley", dayAt(4, 10, 0), dayAt(4, 12, 30)),
                                             punch(903, "sam", dayAt(40, 9, 0), dayAt(40, 17, 0))};
    pos.shared()->punchHistory = [stored](std::int64_t from, std::int64_t to) {
        std::vector<core::TimePunch> out;
        for (const core::TimePunch &p : stored)
            if (p.clockIn >= from && p.clockIn < to)
                out.push_back(p);
        return out;
    };
    // In memory: Sam's punch as a manager just fixed it (an hour more than the store's), Jo still on.
    core::TimePunch fixed = stored[0];
    fixed.clockOut = dayAt(3, 16, 0);
    pos.shared()->earlierPunches.push_back(fixed);
    pos.shared()->punches.push_back(punch(904, "jo", dayAt(0, 0, 0) + 3'600'000, 0));

    REQUIRE(pos.loginWithPin(u"1234"_s));
    const QString from = today.addDays(-7).toString(u"yyyy-MM-dd"_s), to = today.toString(u"yyyy-MM-dd"_s);
    REQUIRE(pos.requestRangeReport(u"timeclock"_s, u"custom"_s, from, to));
    REQUIRE(waitForRange(pos));
    QVariantMap range = pos.rangeReport();
    CHECK(range[u"report"_s].toMap()[u"id"_s] == u"timeclock"_s);
    CHECK_FALSE(range.contains(u"checks"_s));
    CHECK(hasRow(range, u"Employee #: sam    Name: Sam"_s));
    CHECK(hasRow(range, u"Employee #: riley    Name: Riley"_s));
    CHECK(hasRow(range, u"Employee #: jo    Name: Jo"_s));
    QStringList grand;
    for (const QVariant &v : range[u"report"_s].toMap()[u"rows"_s].toList())
        if (v.toMap()[u"cells"_s].toStringList().value(0) == u"Grand Total Hours:"_s)
            grand = v.toMap()[u"cells"_s].toStringList();
    // 7:00 (Sam's as fixed) + 2:30; not the punch from 40 days ago, nor Jo's still open.
    CHECK(grand == QStringList{u"Grand Total Hours:"_s, QString(), u"09:30"_s, u"9.50"_s});

    // Printed: the receipt-paper layout of the days shown.
    REQUIRE(pos.printReport(u"timeclock"_s, -1));
    REQUIRE(printer.printed.size() == 1);
    CHECK(printer.printed.back().columns.size() == 3);

    // Only Sam.
    REQUIRE(pos.requestRangeReport(u"timeclock|sam"_s, u"custom"_s, from, to));
    REQUIRE(waitForRange(pos));
    range = pos.rangeReport();
    CHECK(hasRow(range, u"Employee #: sam    Name: Sam"_s));
    CHECK_FALSE(hasRow(range, u"Employee #: riley    Name: Riley"_s));

    // Today (Day): today's punches.
    const QVariantMap todayReport = pos.report(u"timeclock"_s);
    CHECK(todayReport[u"id"_s] == u"timeclock"_s);
    REQUIRE(pos.printReport(u"timeclock"_s, 0));
    CHECK(printer.printed.back().columns.size() == 3);
}
