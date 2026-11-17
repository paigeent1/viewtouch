#include <catch2/catch_test_macros.hpp>

#include "core/report.hh"
#include "pos_fixture.hh"
#include "print/spooler.hh"
#include "print/ticket_printer.hh"
#include "qt_catch.hh"

#include <QDateTime>
#include <QTemporaryDir>
#include <QTest>

#include <map>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Manager > Reports > Exceptions search (app/pos_exceptions.cpp): from the database, filtered, with
// totals by type that match the Exceptions report exactly.

namespace {

// A row of a report section: {first cell, ...}.
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

struct Store {
    QTemporaryDir dir;
    print::PrintSpooler spooler;
    print::TicketPrinter printer{spooler, dir.filePath(u"printouts"_s)};
    PosService pos{test::seedPosData(), nullptr};
    Store() { pos.setPrinter(&printer); }

    QVariantMap search(const QVariantMap &filter)
    {
        REQUIRE(pos.searchExceptions(filter));
        REQUIRE(QTest::qWaitFor([&] { return !pos.exceptionSearch().value(u"loading"_s).toBool(); }, 20000));
        return pos.exceptionSearch();
    }
    static QVariantMap totalFor(const QVariantMap &found, const QString &type)
    {
        for (const QVariant &v : found.value(u"totals"_s).toList())
            if (v.toMap().value(u"type"_s).toString() == type)
                return v.toMap();
        return {};
    }
    // Voids, a comp taken back, No Sales and a Cancel Sale, by Morgan (a manager).
    std::int64_t busyDay()
    {
        REQUIRE(pos.loginWithPin(u"1234"_s));
        pos.entryKey(u"10000"_s);
        REQUIRE(pos.openDrawerSession());
        REQUIRE(pos.noSale());
        REQUIRE(pos.noSale());
        REQUIRE(pos.startCheck(core::CheckType::Quick));
        pos.addItem(u"cobb"_s);     // 12.50
        pos.addItem(u"caesar"_s);   // 9.75
        pos.finishChoosing();
        REQUIRE(pos.sendOrder());
        REQUIRE(pos.voidItem());                   // the Caesar: a void worth 9.75
        REQUIRE(pos.tender(u"comp"_s));            // the Cobb comped...
        REQUIRE(pos.removePayment());              // ...then not
        const std::int64_t id = pos.checkInfo()[u"id"_s].toLongLong();
        pos.entryKey(u"2000"_s);
        REQUIRE(pos.tender(u"cash"_s));
        REQUIRE(pos.closeCheck());
        REQUIRE(pos.reopenCheck(id));
        pos.releaseCheck();
        REQUIRE(pos.startCheck(core::CheckType::Quick));
        pos.addItem(u"cobb"_s);
        pos.finishChoosing();
        REQUIRE(pos.cancelSale());
        return id;
    }
};

} // namespace

TEST_CASE("Exceptions search: totals by type match the Exceptions report", "[exceptionsearch]")
{
    Store s;
    s.busyDay();
    const QVariantMap found = s.search({});   // today, everything
    const core::Report report = s.pos.buildReport(u"exceptions"_s);
    int compared = 0;
    for (const std::string &type : core::exceptionTypes()) {
        const std::string section = core::exceptionReportSection(type);
        const QVariantMap mine = Store::totalFor(found, QString::fromStdString(type));
        const std::vector<std::string> all = section.empty() ? std::vector<std::string>{} : rowIn(report, section, "All");
        if (section.empty())
            continue;
        if (all.empty()) {
            CHECK(mine.isEmpty());
            continue;
        }
        INFO(type);
        REQUIRE_FALSE(mine.isEmpty());
        CHECK(std::to_string(mine.value(u"count"_s).toInt()) == all[1]);
        if (!all[2].empty())
            CHECK(mine.value(u"amount"_s).toString().toStdString() == all[2]);
        ++compared;
    }
    CHECK(compared >= 5);   // voids, comps, taken back, reopened, cancelled, no sales
    CHECK(Store::totalFor(found, u"void"_s).value(u"count"_s).toInt() == 1);
    CHECK(Store::totalFor(found, u"noSale"_s).value(u"count"_s).toInt() == 2);
    CHECK(Store::totalFor(found, u"cancel"_s).value(u"count"_s).toInt() == 1);
    // Every row says who, what, which check.
    const QVariantList rows = found.value(u"results"_s).toList();
    CHECK(rows.size() == found.value(u"count"_s).toInt());
    for (const QVariant &v : rows) {
        const QVariantMap r = v.toMap();
        CHECK_FALSE(r.value(u"who"_s).toString().isEmpty());
        CHECK_FALSE(r.value(u"when"_s).toString().isEmpty());
        CHECK_FALSE(r.value(u"typeLabel"_s).toString().isEmpty());
    }
    // The whole list as a report, for Print and PDF.
    const core::Report paper = s.pos.exceptionSearchReport();
    CHECK(paper.rows.size() > rows.size());
}

TEST_CASE("Exceptions search: by type, employee, amount and text (a check number with or without the prefix)",
          "[exceptionsearch]")
{
    Store s;
    const std::int64_t id = s.busyDay();
    const int everything = s.search({}).value(u"count"_s).toInt();
    REQUIRE(everything >= 6);

    QVariantMap f = s.search({{u"types"_s, QStringList{u"void"_s}}});
    CHECK(f.value(u"count"_s).toInt() == 1);
    CHECK(f.value(u"results"_s).toList().front().toMap().value(u"amount"_s).toString() == u"$9.75"_s);

    f = s.search({{u"types"_s, QStringList{u"noSale"_s, u"cancel"_s}}});
    CHECK(f.value(u"count"_s).toInt() == 3);

    CHECK(s.search({{u"employee"_s, u"Morgan (Manager)"_s}}).value(u"count"_s).toInt() == everything);
    CHECK(s.search({{u"employee"_s, u"Nobody Here"_s}}).value(u"count"_s).toInt() == 0);

    // At least $10: not the $9.75 void, not the No Sales.
    f = s.search({{u"minAmount"_s, u"10"_s}});
    CHECK(f.value(u"count"_s).toInt() > 0);
    for (const QVariant &v : f.value(u"results"_s).toList())
        CHECK(v.toMap().value(u"type"_s).toString() != u"void"_s);
    CHECK(Store::totalFor(f, u"noSale"_s).isEmpty());
    CHECK_FALSE(s.pos.searchExceptions({{u"minAmount"_s, u"ten"_s}}));

    // Text: the item, a check number, the store's prefix.
    f = s.search({{u"text"_s, u"caesar"_s}});
    CHECK(f.value(u"count"_s).toInt() == 1);
    const int onCheck = s.search({{u"text"_s, u"#%1"_s.arg(id)}}).value(u"count"_s).toInt();
    CHECK(onCheck >= 3);   // the void, the comp, taken back, reopened
    for (const QVariant &v : s.pos.exceptionSearch().value(u"results"_s).toList())
        CHECK(v.toMap().value(u"checkId"_s).toLongLong() == id);
    s.pos.shared()->settings.checkPrefix = "HCA";
    CHECK(s.search({{u"text"_s, u"HCA-%1"_s.arg(id)}}).value(u"count"_s).toInt() == onCheck);
    CHECK(s.search({{u"text"_s, u"hca%1"_s.arg(id)}}).value(u"count"_s).toInt() == onCheck);
    CHECK(s.search({{u"text"_s, QString::number(id)}}).value(u"count"_s).toInt() == onCheck);
    CHECK(s.pos.exceptionSearch().value(u"results"_s).toList().front().toMap().value(u"check"_s).toString()
          == u"HCA-%1"_s.arg(id));
}

TEST_CASE("Exceptions search over days reads the database's checks, Cancel Sales and drawers, matching the report",
          "[exceptionsearch]")
{
    Store s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    const QDate today = QDateTime::currentDateTime().date();
    const std::int64_t yesterday = QDateTime(today.addDays(-1), QTime(15, 0)).toMSecsSinceEpoch();
    core::Check old;
    old.id = 900;
    old.label = "Quick";
    old.status = core::CheckStatus::Closed;
    old.closedAt = yesterday;
    old.note(yesterday - 1000, "Ana", "Void: approved by Morgan (Manager)", "approval", {}, "Morgan (Manager)");
    old.note(yesterday, "Ana", "Voided Cobb ($12.50)", "void", Money::fromCents(1250), "Wrong item");
    core::Check cancelled;
    cancelled.id = 901;
    cancelled.label = "Quick";
    cancelled.status = core::CheckStatus::Discarded;
    cancelled.cancelNumber = 1;
    cancelled.closedAt = yesterday;
    cancelled.note(yesterday, "Ana", "Cancel #1: $5.00", "cancel", Money::fromCents(500), "No ID");
    core::DrawerSession drawer;
    drawer.id = 77;
    drawer.name = "Drawer 1";
    drawer.openedAt = yesterday - 3'600'000;
    core::CashMovement m;
    m.kind = core::CashMovement::Kind::NoSale;
    m.at = yesterday;
    m.by = "Ana";
    drawer.movements.push_back(m);
    s.pos.shared()->history = [=](std::int64_t a, std::int64_t b) {
        return old.closedAt >= a && old.closedAt < b ? std::vector<core::Check>{old} : std::vector<core::Check>{};
    };
    s.pos.shared()->cancelledHistory = [=](std::int64_t a, std::int64_t b) {
        return yesterday >= a && yesterday < b ? std::vector<core::Check>{cancelled} : std::vector<core::Check>{};
    };
    s.pos.shared()->drawerHistory = [=](std::int64_t a, std::int64_t b) {
        return drawer.openedAt >= a && drawer.openedAt < b ? std::vector<core::DrawerSession>{drawer}
                                                           : std::vector<core::DrawerSession>{};
    };
    const QString day = today.addDays(-1).toString(u"yyyy-MM-dd"_s);
    const QVariantMap found = s.search({{u"from"_s, day}, {u"to"_s, day}});
    CHECK(found.value(u"count"_s).toInt() == 3);
    const QVariantMap voided = Store::totalFor(found, u"void"_s);
    CHECK(voided.value(u"count"_s).toInt() == 1);
    // Approved on the spot: who approved it.
    for (const QVariant &v : found.value(u"results"_s).toList())
        if (v.toMap().value(u"type"_s).toString() == u"void"_s)
            CHECK(v.toMap().value(u"approvedBy"_s).toString() == u"Morgan (Manager)"_s);
    // Today only: none of yesterday's.
    CHECK(s.search({}).value(u"count"_s).toInt() == 0);

    // The Exceptions report over the same day counts the same (the Cancel Sale too).
    REQUIRE(s.pos.requestRangeReport(u"exceptions"_s, u"custom"_s, day, day, false));
    REQUIRE(QTest::qWaitFor([&] { return !s.pos.rangeReport().value(u"loading"_s).toBool(); }, 20000));
    const QVariantList rows = s.pos.rangeReport().value(u"report"_s).toMap().value(u"rows"_s).toList();
    std::map<std::string, std::string> allRows;   // section -> its "All" count
    std::string section;
    for (const QVariant &v : rows) {
        const QVariantMap r = v.toMap();
        const QStringList cells = r.value(u"cells"_s).toStringList();
        if (r.value(u"kind"_s).toString() == u"section"_s)
            section = cells.value(0).toStdString();
        else if (cells.value(0) == u"All"_s)
            allRows[section] = cells.value(1).toStdString();
    }
    for (const char *type : {"void", "cancel", "noSale"})
        CHECK(allRows[core::exceptionReportSection(type)]
              == std::to_string(Store::totalFor(found, QString::fromLatin1(type)).value(u"count"_s).toInt()));
}

TEST_CASE("Exceptions search: managers only", "[exceptionsearch]")
{
    Store s;
    CHECK_FALSE(s.pos.searchExceptions({}));   // nobody logged in
    REQUIRE(s.pos.loginWithPin(u"1111"_s));      // a server
    CHECK_FALSE(s.pos.searchExceptions({}));
    CHECK_FALSE(s.pos.moreExceptions());
    CHECK_FALSE(s.pos.printExceptionSearch());
    CHECK(s.pos.exceptionSearchVariant().isEmpty());
}
