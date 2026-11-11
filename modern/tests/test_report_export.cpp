#include <catch2/catch_test_macros.hpp>

#include "pos_fixture.hh"
#include "qt_catch.hh"
#include "storage/async_writer.hh"
#include "storage/pos_store.hh"
#include "storage/report_export.hh"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <functional>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::storage::AsyncWriter;
using vt::storage::PosStore;
using vt::storage::SqlPosSink;

// vtmodern --export-reports: the web reporting page's JSON files (web/vt-reports).

namespace {

std::int64_t at(int y, int m, int d, int h, int min = 0)
{
    return QDateTime(QDate(y, m, d), QTime(h, min)).toMSecsSinceEpoch();
}

QByteArray sha(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
}

QJsonObject readJson(const QString &file)
{
    QFile f(file);
    REQUIRE(f.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(f.readAll()).object();
}

// The cells of the first row whose first cell is `label`.
QStringList rowOf(const QJsonObject &file, const QString &label)
{
    for (const QJsonValue &v : file[u"report"_s][u"rows"_s].toArray()) {
        const QJsonArray cells = v[u"cells"_s].toArray();
        if (!cells.isEmpty() && cells[0].toString() == label) {
            QStringList out;
            for (const QJsonValue &c : cells)
                out << c.toString();
            return out;
        }
    }
    return {};
}

} // namespace

TEST_CASE("Report export: a closed day and the open day as JSON, the database only read", "[reportexport]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"vt.db"_s);
    auto seed = test::seedPosData();
    seed.settings.storeName = "Test Store";
    seed.settings.checkPrefix = "TS";
    {
        PosStore store(path);
        REQUIRE(store.open());
        REQUIRE(store.seed(seed.settings, seed.menu, seed.employees));
    }
    std::int64_t clock = at(2026, 3, 10, 11);
    auto run = [&](const std::function<void(app::PosService &)> &body) {
        PosStore store(path);
        REQUIRE(store.open());
        AsyncWriter writer(path);
        SqlPosSink sink(writer);
        app::PosService pos(*store.load(), &sink);
        pos.setClock([&clock] { return clock; });
        REQUIRE(pos.loginWithPin(u"1234"_s));
        body(pos);
    };
    // Day 1, closed at 11 PM on the 10th; day 2 (opened then) sells on the 11th and is still open.
    run([&](app::PosService &pos) {
        pos.entryKey(u"5000"_s);
        REQUIRE(pos.openDrawerSession());
        pos.addItem(u"coffee"_s);
        REQUIRE(pos.tender(u"cash"_s));
        REQUIRE(pos.closeCheck());
        clock = at(2026, 3, 10, 23);
        pos.entryKey(u"5298"_s);
        REQUIRE(pos.countDrawer());
        REQUIRE(pos.endOfDay());
    });
    clock = at(2026, 3, 11, 12);
    run([&](app::PosService &pos) {
        pos.entryKey(u"5000"_s);
        REQUIRE(pos.openDrawerSession());
        pos.addItem(u"coffee"_s);
        pos.addItem(u"coffee"_s);
        REQUIRE(pos.tender(u"cash"_s));
        REQUIRE(pos.closeCheck());
    });

    // ViewTouch still has the database open while the export runs.
    PosStore live(path);
    REQUIRE(live.open());
    const auto liveData = live.load();
    REQUIRE(liveData);
    const QByteArray before = sha(path), walBefore = sha(path + u"-wal"_s);

    const QString out = dir.filePath(u"out"_s);
    vt::storage::ReportExportOptions options;
    options.database = path;
    options.outDir = out;
    options.from = QDate(2026, 3, 10);
    options.to = QDate(2026, 3, 11);
    options.now = at(2026, 3, 11, 12, 30);
    const auto result = vt::storage::exportReports(options);
    INFO(result.error.toStdString());
    REQUIRE(result.ok);
    CHECK(result.days == QStringList{u"2026-03-10_1"_s, u"2026-03-11_2"_s});
    CHECK(sha(path) == before);   // not a byte of the live database changed
    CHECK(sha(path + u"-wal"_s) == walBefore);

    // Every report the End of Day keeps, Time Clock and the gift card reports among them, for each day.
    const QStringList ids = app::PosService::endOfDayReportIds();
    for (const QString &id : {u"sales"_s, u"timeclock"_s, u"gc_activated"_s, u"gc_tx"_s, u"accounts"_s, u"labor"_s})
        CHECK(ids.contains(id));
    for (const QString &id : ids) {
        INFO(id.toStdString());
        CHECK(QFile::exists(QDir(out).filePath(id + u"/2026-03-10_1.json"_s)));
        CHECK(QFile::exists(QDir(out).filePath(id + u"/2026-03-11_2.json"_s)));
    }
    CHECK(result.files.size() == 2 * ids.size());

    const QJsonObject closedDay = readJson(QDir(out).filePath(u"sales/2026-03-10_1.json"_s));
    CHECK(closedDay[u"format"_s].toString() == u"viewtouch-report/1"_s);
    CHECK(closedDay[u"store"_s][u"name"_s].toString() == u"Test Store"_s);
    CHECK(closedDay[u"store"_s][u"checkPrefix"_s].toString() == u"TS"_s);
    CHECK(closedDay[u"report"_s][u"id"_s].toString() == u"sales"_s);
    CHECK_FALSE(closedDay[u"report"_s][u"title"_s].toString().isEmpty());
    CHECK(closedDay[u"day"_s][u"id"_s].toInt() == 1);
    CHECK(closedDay[u"day"_s][u"date"_s].toString() == u"2026-03-10"_s);
    CHECK_FALSE(closedDay[u"day"_s][u"open"_s].toBool());
    CHECK(closedDay[u"day"_s][u"closedAtMs"_s].toInteger() == at(2026, 3, 10, 23));
    CHECK(closedDay[u"generatedAtMs"_s].toInteger() == options.now);
    const QStringList net1 = rowOf(closedDay, u"Net sales"_s);
    REQUIRE(net1.size() >= 2);

    // The open day: the same numbers as the Reports screen's Today.
    const QJsonObject openDay = readJson(QDir(out).filePath(u"sales/2026-03-11_2.json"_s));
    CHECK(openDay[u"day"_s][u"open"_s].toBool());
    CHECK(openDay[u"day"_s][u"date"_s].toString() == u"2026-03-11"_s);
    app::PosService screen(*liveData, nullptr);
    screen.setClock([&] { return options.now; });
    const std::vector<std::string> net2 = screen.buildReport(u"sales"_s).find("Net sales");
    REQUIRE(net2.size() >= 2);
    CHECK(rowOf(openDay, u"Net sales"_s).last().toStdString() == net2.back());
    CHECK(rowOf(openDay, u"Net sales"_s).last() != net1.last());   // two coffees, not one

    // No dates: the open day only.
    const QString out2 = dir.filePath(u"out2"_s);
    options.outDir = out2;
    options.from = options.to = QDate();
    const auto today = vt::storage::exportReports(options);
    REQUIRE(today.ok);
    CHECK(today.days == QStringList{u"2026-03-11_2"_s});
    CHECK(sha(path) == before);
}

TEST_CASE("Report export: a business day counts from 6 AM", "[reportexport]")
{
    CHECK(vt::storage::businessDate(at(2026, 3, 11, 2)) == QDate(2026, 3, 10));    // a bar closing at 2 AM
    CHECK(vt::storage::businessDate(at(2026, 3, 10, 23)) == QDate(2026, 3, 10));
    CHECK(vt::storage::businessDate(at(2026, 3, 11, 7)) == QDate(2026, 3, 11));
}

TEST_CASE("Report export: no PINs, card numbers or ID details in the files", "[reportexport]")
{
    core::Report r;
    r.id = "gc_activated";
    r.columns = {"Card #", "PIN", "First Name", "Date of Birth", "Balance"};
    r.line({"6036001234567890", "4821", "Pat", "01/02/1990", "$25.00"});
    r.line({"77", "", "Lee", "", "$5.00"});
    r.total({"Total Matching Cards", "2", "", "", "$30.00"});
    r.note("Paid with 4111 1111 1111 1111 on the 3rd");
    const core::Report m = vt::storage::maskPrivate(r);
    CHECK(m.rows[0].cells == std::vector<std::string>{"••••7890", "••••", "Pat", "",
                                                       "$25.00"});
    CHECK(m.rows[1].cells[0] == "••••");   // a short number shows nothing
    CHECK(m.rows[1].cells[1].empty());
    CHECK(m.rows[2].cells == r.rows[2].cells);                   // totals: labels and counts as they were
    CHECK(m.rows[3].cells[0] == "Paid with ••••1111 on the 3rd");

    const QJsonObject file = vt::storage::reportFile(u"gc_activated"_s, m, u"S"_s, u"P"_s, 3, QDate(2026, 3, 10),
                                                     at(2026, 3, 10, 9), 0, at(2026, 3, 10, 12));
    const QByteArray text = QJsonDocument(file).toJson();
    CHECK_FALSE(text.contains("4821"));
    CHECK_FALSE(text.contains("1234567890"));
    CHECK_FALSE(text.contains("1990"));
    CHECK(file[u"day"_s][u"open"_s].toBool());
}
