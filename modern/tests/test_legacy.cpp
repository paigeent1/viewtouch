#include <catch2/catch_test_macros.hpp>

#include "app/legacy_history.hh"
#include "core/legacy_sales.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// ILSA's sales history: the history file vt-ilsa-history writes (made-up data: its --self-test
// export, two days; scripts/bin/vt-ilsa-history --self-test --write-fixture makes the fixture),
// its reports, both GPs, ILSA's Z beside the receipts, and its receipts read-only.
//   2026-10-27: a $10.00 cigar (cost $7.00, tax $0.63, cash) and $5.00 of lottery (card): matches its Z.
//   2026-10-28: 2 cigars $20.00 (cost $14.00), a lighter voided (+4.00 / -4.00), a dept 260 open
//   ring $6.00 with no cost, a promotion -$1.00, tax $1.58; a cigar returned (-$10.00, tax -$0.63);
//   a cancelled sale (mode 1, $7.00); a paid out -$20.00. ILSA's Z cash is $1.00 off its receipts.

namespace {

const char *const kFixture =
#include "legacy_fixture.inc"
    ;

QString makeHistory(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(u"ilsa-history.db"_s);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, u"legacy-fixture"_s);
        db.setDatabaseName(path);
        REQUIRE(db.open());
        QSqlQuery q(db);
        for (const QString &line : QString::fromUtf8(kFixture).split(u'\n')) {
            if (line.trimmed().isEmpty())
                continue;
            INFO(line.toStdString());
            REQUIRE(q.exec(line.trimmed()));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(u"legacy-fixture"_s);
    return path;
}

QStringList cells(const QVariantMap &report, const QString &first)
{
    for (const QVariant &v : report[u"rows"_s].toList()) {
        const QStringList c = v.toMap()[u"cells"_s].toStringList();
        if (!c.isEmpty() && c.first() == first)
            return c;
    }
    return {};
}

bool waitFor(const PosService &pos)
{
    for (int i = 0; i < 500 && pos.rangeReport()[u"loading"_s].toBool(); ++i)
        QTest::qWait(20);
    return !pos.rangeReport()[u"loading"_s].toBool();
}

} // namespace

TEST_CASE("ILSA history: a day's totals as its Daily Z, both GPs", "[legacy]")
{
    QTemporaryDir dir;
    const QString path = makeHistory(dir);
    const auto st = app::legacy::status(path);
    CHECK(st.present);
    CHECK(st.days == 4);           // BHDAN: two days of the made-up copy, one of an older one; BHNM: one day
    CHECK(st.receipts == 9);
    CHECK(st.first == u"2026-10-15"_s);
    // One store's history (BHDAN, BHNM: the store's check prefix).
    const auto dan = app::legacy::status(path, nullptr, u"BHDAN"_s);
    CHECK(dan.days == 3);
    CHECK(dan.first == u"2026-10-20"_s);
    CHECK(app::legacy::status(path, nullptr, u"bhnm"_s).days == 1);
    CHECK(app::legacy::stores(path) == QStringList{u"BHDAN"_s, u"BHNM"_s});

    QString error;
    const core::legacy::ZTotals z = app::legacy::totals(path, u"2026-10-28"_s, u"2026-10-28"_s, &error);
    CHECK(error.isEmpty());
    CHECK(z.receipts == 3);
    CHECK(z.notSales == 1);                                   // the cancelled sale: not a sale
    CHECK(z.merchandise == Money::fromCents(1600));           // 20 + 4 - 4 + 6 - 10
    CHECK(z.promotions == Money::fromCents(-100));
    CHECK(z.tax == Money::fromCents(95));                     // 1.58 - 0.63
    CHECK(z.payouts == Money::fromCents(-2000));
    CHECK(z.lottery == Money());
    CHECK(z.tender("Cash") == Money::fromCents(-2405));       // 10.00 - 10.63 - 20.00 - 3.42 change
    CHECK(z.tender("Card") == Money::fromCents(2000));
    CHECK(z.voidLines == 2);
    CHECK(z.voided == Money::fromCents(400));                 // the pair, once
    CHECK(z.returnLines == 1);
    CHECK(z.returned == Money::fromCents(-1000));
    CHECK(z.units == 2);                                      // 2 + 1 - 1 + 1 - 1
    CHECK(z.cost == Money::fromCents(700));                   // 14.00 + 2.50 - 2.50 - 7.00
    CHECK(z.noCostSales == Money::fromCents(600));
    // As ILSA: the open ring at cost = its retail, the promotion off the cost: 1 - 12.00 / 16.00.
    CHECK(core::legacy::gpAsIlsa(z.merchandise, z.cost, z.noCostSales, z.promotions) == "25.0%");
    // Without the open ring: 1 - 6.00 / 10.00.
    CHECK(core::legacy::gpTrue(z.merchandise, z.cost, z.noCostSales, z.promotions) == "40.0%");

    core::PosSettings settings;
    const core::ReportContext ctx{settings, "Wed Oct 28, 2026"};
    const core::Report r = core::legacy::dailyZ(z, "ILSA", ctx);
    CHECK(r.find("Department sales") == std::vector<std::string>{"Department sales", "$15.00"});
    CHECK(r.find("Tax") == std::vector<std::string>{"Tax", "$0.95"});
    CHECK(r.find("GP% as ILSA").back() == "25.0%");
    CHECK(r.find("GP% without no-cost lines").back() == "40.0%");

    // Lottery is history only: not merchandise.
    const core::legacy::ZTotals day1 = app::legacy::totals(path, u"2026-10-27"_s, u"2026-10-27"_s);
    CHECK(day1.merchandise == Money::fromCents(1000));
    CHECK(day1.lottery == Money::fromCents(500));
    CHECK(day1.tender("Cash") == Money::fromCents(1063));
}

TEST_CASE("ILSA history: departments when sold or as ILSA, items, GP by month", "[legacy]")
{
    QTemporaryDir dir;
    const QString path = makeHistory(dir);
    const auto depts = app::legacy::groups(path, u"2026-10-27"_s, u"2026-10-28"_s, u"dept"_s, false, 0);
    REQUIRE(depts.size() == 3);   // 10 cigars, 20 accessories (the voided lighter), 260 open ring
    CHECK(depts[0].key == "10");
    CHECK(depts[0].label == "CIGARS");
    CHECK(depts[0].sales == Money::fromCents(2000));
    CHECK(depts[0].cost == Money::fromCents(1400));
    CHECK(depts[0].buydowns == Money::fromCents(-100));
    CHECK(depts[0].units == 2);
    CHECK(depts[2].key == "260");
    CHECK(depts[2].noCostSales == Money::fromCents(600));
    const auto lottery = app::legacy::groups(path, u"2026-10-27"_s, u"2026-10-28"_s, u"dept"_s, true, 0);
    REQUIRE(lottery.size() == 1);
    CHECK(lottery[0].sales == Money::fromCents(500));
    // As ILSA's printout: the lighter is in dept 30 now.
    const auto current = app::legacy::groups(path, u"2026-10-27"_s, u"2026-10-28"_s, u"current"_s, false, 0);
    CHECK(std::ranges::any_of(current, [](const auto &g) { return g.key == "30" && g.label == "LIGHTERS"; }));
    CHECK(std::ranges::none_of(current, [](const auto &g) { return g.key == "20"; }));

    core::PosSettings settings;
    const core::ReportContext ctx{settings, "Oct 27 - Oct 28, 2026"};
    const core::Report r = core::legacy::departmentSales(depts, lottery, ctx);
    // Units, Retail, Discount, Cost, Buydowns, Adj. cost, GP% as ILSA, GP% w/o no-cost, No-cost retail
    CHECK(r.find("10 CIGARS") == std::vector<std::string>{"10 CIGARS", "2", "$20.00", "$0.00", "$14.00", "-$1.00", "$13.00",
                                                          "35.0%", "35.0%", "$0.00"});
    CHECK(r.find("260 OPEN RING").at(7) == "0.0%");        // ILSA: no cost = cost at retail
    CHECK(r.find("260 OPEN RING").at(8) == "—");           // the true margin leaves it out
    CHECK(r.find("Merchandise").at(2) == "$26.00");

    bool more = false;
    const auto items = app::legacy::groups(path, u"2026-10-27"_s, u"2026-10-28"_s, u"item"_s, false, 1, &more);
    REQUIRE(items.size() == 1);
    CHECK(more);
    CHECK(items[0].label == "MADE UP CIGAR ROBUSTO");
    CHECK(items[0].extra == "012345678905");
    const auto brands = app::legacy::groups(path, u"2026-10-27"_s, u"2026-10-28"_s, u"brand"_s, false, 0);
    CHECK(std::ranges::any_of(brands, [](const auto &g) { return g.label == "ACME CIGARS" && g.sales == Money::fromCents(2000); }));

    const auto months = app::legacy::periods(path, u"2026-10-27"_s, u"2026-10-28"_s, false);
    REQUIRE(months.size() == 1);
    CHECK(months[0].period == "2026-10");
    CHECK(months[0].sales == Money::fromCents(2600));
    CHECK(months[0].lottery == Money::fromCents(500));
    CHECK(months[0].buydowns == Money::fromCents(-100));
    const core::Report gp = core::legacy::gpByPeriod(months, false, ctx);
    // ILSA: 1 - (14.00 + 6.00 - 1.00) / 26.00; true: 1 - 13.00 / 20.00.
    CHECK(gp.find("2026-10").at(7) == "26.9%");
    CHECK(gp.find("2026-10").at(8) == "35.0%");
}

TEST_CASE("ILSA history: its Z beside the receipts, and the days that differ", "[legacy]")
{
    QTemporaryDir dir;
    const QString path = makeHistory(dir);
    const auto z = app::legacy::ilsaZ(path, u"2026-10-28"_s, u"2026-10-28"_s);
    CHECK(z.at("tax") == 95);
    CHECK(z.at("tender:1") == -2305);
    const auto ours = app::legacy::totals(path, u"2026-10-28"_s, u"2026-10-28"_s);
    core::PosSettings settings;
    const core::ReportContext ctx{settings, "Wed Oct 28, 2026"};
    const core::Report r = core::legacy::sideBySide(z, ours, nullptr, ctx);
    CHECK(r.find("Tax") == std::vector<std::string>{"Tax", "$0.95", "$0.95", "$0.00"});
    CHECK(r.find("Cash") == std::vector<std::string>{"Cash", "-$23.05", "-$24.05", "-$1.00"});
    CHECK(r.find("Card") == std::vector<std::string>{"Card", "$20.00", "$20.00", "$0.00"});

    const auto diffs = app::legacy::variances(path, u"2026-10-01"_s, u"2026-10-31"_s);
    REQUIRE(diffs.size() == 1);
    CHECK(diffs[0].day == "2026-10-28");
    CHECK(diffs[0].ilsa == -2305);
    CHECK(diffs[0].ours == -2405);

    // The older copy's lines: one item named from the newer copy's list, one no list has.
    const auto unnamed = app::legacy::unnamedItems(path, u"2026-10-01"_s, u"2026-10-31"_s);
    REQUIRE(unnamed.size() == 1);
    CHECK(unnamed[0].upc == "077777777777");
    CHECK(unnamed[0].sales == Money::fromCents(200));
    CHECK(app::legacy::findReceipts(path, u"cutter"_s, 50, 0).size() == 2);
    const core::Report d = core::legacy::differences(diffs, ctx, unnamed);
    CHECK(d.find("2026-10-20").at(2) == "UPC 077777777777 (OLD ACCESSORIES NAME)");
}

TEST_CASE("ILSA history: receipts found and shown read-only", "[legacy]")
{
    QTemporaryDir dir;
    const QString path = makeHistory(dir);
    CHECK(app::legacy::findReceipts(path, u"2026-10-27"_s, 50, 0).size() == 2);
    // Receipt 9003 is in both stores' history: BHDAN-9003 and BHNM-9003.
    CHECK(app::legacy::findReceipts(path, u"#9003"_s, 50, 0).size() == 2);
    CHECK(app::legacy::findReceipts(path, u"9003"_s, 50, 0, nullptr, u"BHDAN"_s).size() == 1);
    CHECK(app::legacy::findReceipts(path, u"bhnm9003"_s, 50, 0).size() == 1);
    CHECK(app::legacy::findReceipts(path, u"HCA-9003"_s, 50, 0).empty());
    const auto byNumber = app::legacy::findReceipts(path, u"BHDAN-9003"_s, 50, 0);
    REQUIRE(byNumber.size() == 1);
    CHECK(byNumber[0].prefix == "BHDAN");
    CHECK(core::legacy::receiptLabel(byNumber[0]) == "BHDAN-9003");
    // ILSA kept UPCs as numbers: found with or without the leading 0.
    CHECK(app::legacy::findReceipts(path, u"12345678905"_s, 50, 0).size() == 4);   // BHNM's too
    CHECK(app::legacy::findReceipts(path, u"012345678905"_s, 50, 0, nullptr, u"BHDAN"_s).size() == 3);
    CHECK(app::legacy::findReceipts(path, u"lighter"_s, 50, 0).size() == 2);
    CHECK(app::legacy::findReceipts(path, u"26.58"_s, 50, 0).size() == 1);

    const auto r = app::legacy::receipt(path, byNumber[0].id);
    REQUIRE(r);
    CHECK(r->lines.size() == 5);
    CHECK(r->dob == "1975-03-04");
    CHECK(r->cashierName == "Test Cashier Four");   // as that ILSA copy had it
    const std::string text = core::legacy::receiptText(*r, false);
    CHECK(text.find("2 MADE UP CIGAR ROBUSTO") != std::string::npos);
    CHECK(text.find("VOID") != std::string::npos);
    CHECK(text.find("Receipt BHDAN-9003") != std::string::npos);
    CHECK(text.find("Cashier 4 Test Cashier Four") != std::string::npos);
    CHECK(text.find("UPC 012345678905") != std::string::npos);
    CHECK(text.find("Age checked: 21+") != std::string::npos);
    CHECK(text.find("ILSA receipt") != std::string::npos);
    CHECK(text.find("1975") == std::string::npos);             // the date of birth: the store's copy only
    CHECK(core::legacy::receiptText(*r, true).find("Date of birth: 1975-03-04") != std::string::npos);

    // No history file: nothing found, said so.
    QString error;
    CHECK(app::legacy::findReceipts(dir.filePath(u"none.db"_s), {}, 10, 0, &error).empty());
    CHECK_FALSE(error.isEmpty());
}

TEST_CASE("ILSA History on the Reports screen and in Receipt Lookup", "[legacy][reports]")
{
    QTemporaryDir dir;
    PosService pos(test::seedPosData(), nullptr);
    pos.shared()->legacyDb = makeHistory(dir);
    REQUIRE(pos.loginWithPin(u"1234"_s));

    REQUIRE(pos.requestLegacyReport(u"depts"_s, u"2026-10-27"_s, u"2026-10-28"_s));
    REQUIRE(waitFor(pos));
    QVariantMap report = pos.rangeReport()[u"report"_s].toMap();
    CHECK(pos.rangeReport()[u"id"_s] == u"ilsa-depts"_s);
    CHECK(cells(report, u"10 CIGARS"_s).value(2) == u"$20.00"_s);

    REQUIRE(pos.requestLegacyReport(u"z"_s, u"2026-10-28"_s));
    REQUIRE(waitFor(pos));
    report = pos.rangeReport()[u"report"_s].toMap();
    CHECK(report[u"title"_s] == u"Daily Z (ILSA)"_s);
    CHECK(cells(report, u"Receipts"_s).value(1) == u"3"_s);

    REQUIRE(pos.requestLegacyReport(u"compare"_s, u"2026-10-28"_s, u"2026-10-28"_s));
    REQUIRE(waitFor(pos));
    CHECK(cells(pos.rangeReport()[u"report"_s].toMap(), u"Cash"_s).value(3) == u"-$1.00"_s);

    CHECK_FALSE(pos.requestLegacyReport(u"z"_s, u"28 Oct"_s));   // dates like 2026-10-28

    REQUIRE(pos.findLegacyReceipts(u"9003"_s, 0, u"*"_s));            // every store's
    REQUIRE(waitFor(pos));
    CHECK(pos.rangeReport()[u"results"_s].toList().size() == 2);
    REQUIRE(pos.findLegacyReceipts(u"9003"_s));                       // this site's store's
    REQUIRE(waitFor(pos));
    REQUIRE(pos.rangeReport()[u"results"_s].toList().size() == 1);
    CHECK(pos.rangeReport()[u"results"_s].toList().value(0).toMap()[u"label"_s] == u"BHDAN-9003"_s);
    pos.shared()->settings.ilsaHistoryStore = "BHNM";                 // the New Milford site
    REQUIRE(pos.requestLegacyReport(u"z"_s, u"2026-10-15"_s));
    REQUIRE(waitFor(pos));
    CHECK(cells(pos.rangeReport()[u"report"_s].toMap(), u"Receipts"_s).value(1) == u"1"_s);
    pos.shared()->settings.ilsaHistoryStore = "BHDAN";
    REQUIRE(pos.findLegacyReceipts(u"2026-10-28"_s));
    REQUIRE(waitFor(pos));
    const QVariantList results = pos.rangeReport()[u"results"_s].toList();
    REQUIRE(results.size() == 4);
    REQUIRE(pos.selectLegacyReceipt(results[0].toMap()[u"id"_s].toLongLong()));
    const QVariantMap picked = pos.rangeReport()[u"selected"_s].toMap();
    CHECK(picked[u"text"_s].toString().contains(u"ILSA receipt"_s));
    // The store's receipt header on top, as ViewTouch's own receipts.
    CHECK(picked[u"text"_s].toString().startsWith(QString::fromStdString(pos.shared()->settings.storeName)));

    // History only: nothing of it in today's sales.
    CHECK(pos.shared()->closedToday.empty());
}

TEST_CASE("ILSA history: cashiers tied to ViewTouch employees, sales by employee and item", "[legacy]")
{
    QTemporaryDir dir;
    const QString path = makeHistory(dir);
    // Made-up cashiers: 3 and 4 in the newer copy, 2 in the older one.
    CHECK(app::legacy::cashiers(path).size() == 4);          // every store's: BHNM's cashier 9 too
    auto list = app::legacy::cashiers(path, nullptr, u"BHDAN"_s);
    REQUIRE(list.size() == 3);
    CHECK(list[0].number == 2);
    CHECK(list[0].name == "Test Cashier Two");
    const auto three = std::ranges::find_if(list, [](const auto &c) { return c.number == 3; });
    REQUIRE(three != list.end());
    const int source = three->sourceId;
    CHECK(three->receipts == 2);                         // 9001, 9002 (Oct 27)
    CHECK(three->sales == Money::fromCents(1500));       // net of tax
    CHECK(three->employeeId.empty());                    // not tied yet: a former employee
    CHECK(core::legacy::cashierLabel(*three) == "Test Cashier Three (ILSA cashier 3)");

    // Not tied: shown as the ILSA cashier.
    auto rows = app::legacy::employeeItems(path, u"2026-10-27"_s, u"2026-10-27"_s, {});
    REQUIRE(rows.size() == 1);                           // the cigar (lottery left out)
    CHECK(rows[0].who == "Test Cashier Three (ILSA cashier 3) (former employee)");

    QString error;
    REQUIRE(app::legacy::setCashier(path, source, 3, "emp-7", "Pat Example", &error));
    list = app::legacy::cashiers(path, nullptr, u"BHDAN"_s);
    CHECK(std::ranges::find_if(list, [](const auto &c) { return c.number == 3; })->employeeName == "Pat Example");
    rows = app::legacy::employeeItems(path, u"2026-10-27"_s, u"2026-10-28"_s, {});
    // Cashier 3 on Oct 27 (the cigar, $10), cashier 4 on Oct 28.
    const auto pat = std::ranges::find_if(rows, [](const auto &r) { return r.key == "emp-7"; });
    REQUIRE(pat != rows.end());
    CHECK(pat->who == "Pat Example");
    CHECK(pat->item == "MADE UP CIGAR ROBUSTO");
    CHECK(pat->sales == Money::fromCents(1000));
    CHECK(app::legacy::employeeItems(path, u"2026-10-27"_s, u"2026-10-28"_s, u"lighter"_s).empty());   // voided: left out
    CHECK(app::legacy::employeeItems(path, u"2026-10-27"_s, u"2026-10-28"_s, u"cigars"_s).size() == 2); // by department

    // The same employee's ViewTouch sales go with theirs.
    core::Check c;
    c.status = core::CheckStatus::Closed;
    core::OrderLine l;
    l.itemId = "robusto";
    l.name = "Robusto";
    l.unitPrice = Money::fromCents(1200);
    l.quantity = 2;
    l.addedById = "emp-7";
    l.addedByName = "Pat Example";
    c.lines.push_back(l);
    const std::vector<core::Check> checks{c};
    core::PosSettings settings;
    const core::ReportContext ctx{settings, "Oct 27 - Oct 28, 2026"};
    const core::Report r = core::legacy::employeeSales({*pat}, &checks, "", ctx);
    CHECK(r.find("Pat Example total") == std::vector<std::string>{"Pat Example total", "3", "$34.00"});
    CHECK(r.find("  of which in ILSA").back() == "$10.00");
    CHECK(r.find("Robusto") == std::vector<std::string>{"Robusto", "2", "$24.00"});

    // Untied again: a former employee.
    REQUIRE(app::legacy::setCashier(path, source, 3, "", "", &error));
    list = app::legacy::cashiers(path, nullptr, u"BHDAN"_s);
    CHECK(std::ranges::find_if(list, [](const auto &x) { return x.number == 3; })->employeeId.empty());
}

TEST_CASE("ILSA History > Cashiers and By Employee on the screen", "[legacy][reports]")
{
    QTemporaryDir dir;
    PosService pos(test::seedPosData(), nullptr);
    pos.shared()->legacyDb = makeHistory(dir);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.requestLegacyReport(u"cashiers"_s, u"2000-01-01"_s, u"2026-12-31"_s));
    REQUIRE(waitFor(pos));
    const QVariantList cashiers = pos.rangeReport()[u"cashiers"_s].toList();
    REQUIRE(cashiers.size() == 3);                                   // this site's store: BHDAN (Store Settings)
    CHECK(pos.rangeReport()[u"store"_s] == u"BHDAN"_s);
    CHECK(pos.rangeReport()[u"stores"_s].toStringList() == QStringList{u"BHDAN"_s, u"BHNM"_s});
    const QVariantList staff = pos.rangeReport()[u"employees"_s].toList();
    REQUIRE_FALSE(staff.isEmpty());
    const QVariantMap who = staff.first().toMap();
    QVariantMap three;
    for (const QVariant &v : cashiers)
        if (v.toMap()[u"number"_s].toInt() == 3)
            three = v.toMap();
    REQUIRE(pos.setLegacyCashier(three[u"sourceId"_s].toInt(), 3, who[u"id"_s].toString()));
    REQUIRE(waitFor(pos));
    bool tied = false;
    for (const QVariant &v : pos.rangeReport()[u"cashiers"_s].toList())
        tied = tied || (v.toMap()[u"number"_s].toInt() == 3 && v.toMap()[u"employeeId"_s] == who[u"id"_s]);
    CHECK(tied);
    CHECK_FALSE(pos.setLegacyCashier(1, 3, u"no-such-employee"_s));

    REQUIRE(pos.requestLegacyReport(u"employees"_s, u"2026-10-27"_s, u"2026-10-28"_s, u"vt|"_s));
    REQUIRE(waitFor(pos));
    const QVariantMap report = pos.rangeReport()[u"report"_s].toMap();
    CHECK(pos.rangeReport()[u"id"_s] == u"ilsa-employees"_s);
    CHECK(cells(report, who[u"name"_s].toString() + u" total"_s).value(2) == u"$10.00"_s);
    // The Reports screen's By Employee asks with its period.
    CHECK(pos.requestLegacyReport(u"employees"_s, u"month"_s, {}, u"vt|"_s));
    REQUIRE(waitFor(pos));
    CHECK(pos.requestLegacyReport(u"employees"_s, u"day:0"_s, {}, u"vt|"_s));
    REQUIRE(waitFor(pos));
}
