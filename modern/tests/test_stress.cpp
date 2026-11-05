#include <catch2/catch_test_macros.hpp>

#include "layout_fixture.hh"
#include "net/layout_hub.hh"
#include "net/stress.hh"
#include "pos_fixture.hh"
#include "print/spooler.hh"
#include "print/ticket_printer.hh"
#include "storage/async_writer.hh"
#include "storage/pos_store.hh"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <cstdio>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>

using namespace Qt::StringLiterals;
using namespace vt;

// A short busy dinner with no screen (hidden: "[.stress]"; VTM_STRESS_MINUTES
// and VTM_STRESS_SPEED change it). Prints the report.
TEST_CASE("Stress: a busy dinner, every check balanced and saved", "[.stress]")
{
    QTemporaryDir dir;
    const QString db = dir.filePath(u"stress.db"_s);
    {
        storage::PosStore store(db);
        REQUIRE(store.open());
        const app::PosData seed = test::seedPosData();
        REQUIRE(store.seed(seed.settings, seed.menu, seed.employees, nullptr, seed.ingredients));
    }
    storage::PosStore store(db);
    REQUIRE(store.open());
    auto data = store.load();
    REQUIRE(data);
    storage::AsyncWriter writer(db);
    storage::SqlPosSink sink(writer);
    app::PosShared shared(std::move(*data), &sink);
    print::PrintSpooler spooler;
    print::TicketPrinter printer(spooler, dir.filePath(u"printouts"_s));
    shared.printer = &printer;
    auto layout = test::loadTestLayout();
    REQUIRE(layout);
    net::LayoutHub hub(*layout);

    net::StressOptions o;
    o.minutes = qEnvironmentVariableIntValue("VTM_STRESS_MINUTES") > 0 ? qEnvironmentVariableIntValue("VTM_STRESS_MINUTES") : 1;
    o.speed = qEnvironmentVariableIntValue("VTM_STRESS_SPEED") > 0 ? qEnvironmentVariableIntValue("VTM_STRESS_SPEED") : 60;
    o.hours = qEnvironmentVariable("VTM_STRESS_HOURS").toDouble();
    o.load = std::max(1, qEnvironmentVariableIntValue("VTM_STRESS_LOAD"));
    o.dbPath = db;
    o.writer = &writer;
    net::StressRunner::tablesOn(*layout, o.tables, o.barSeats);
    REQUIRE(o.tables.size() >= 4);
    net::StressRunner run(&shared, &hub, o);
    QString error;
    REQUIRE(run.start(&error));
    QElapsedTimer t;
    t.start();
    const int minutes = o.hours > 0 ? int(std::ceil(o.hours * 60 / o.speed)) : o.minutes;
    while (!run.isFinished() && t.elapsed() < (minutes + 6) * 60'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    REQUIRE(run.isFinished());
    WARN(run.report().toStdString());
    CHECK(run.passed());
    CHECK(run.stats()[u"closed"_s].toInt() > 20);
}

// A tobacco store's catalog: 33,000 retail items with barcodes, imported the way the ILSA price book
// is (Menu Builder > Import a menu), then the things a register does all day with it. Prints the
// times; the limits only catch something far too slow for a Pi.
TEST_CASE("Big catalog: 33,000 retail items import, list, scan, save, load", "[catalog]")
{
    constexpr int kItems = 33000;
    QVariantList cats, items;
    for (int c = 0; c < 40; ++c)
        cats << QVariantMap{{u"id"_s, u"ilsa-dept-%1"_s.arg(c)}, {u"name"_s, u"DEPT %1"_s.arg(c)}, {u"taxClass"_s, u"merchandise"_s}};
    for (int i = 0; i < kItems; ++i)
        items << QVariantMap{{u"id"_s, u"ilsa-%1"_s.arg(700000000 + i)}, {u"name"_s, u"CIGAR BRAND %1 ROBUSTO %2"_s.arg(i / 25).arg(i)},
                             {u"price"_s, 5.0 + (i % 400) / 10.0}, {u"taxClass"_s, u"merchandise"_s},
                             {u"family"_s, u"ilsa-dept-%1"_s.arg(i % 40)},
                             {u"barcodes"_s, QStringList{QString::number(700000000LL + i), QString::number(800000000LL + i)}}};
    const QVariantMap file{{u"format"_s, u"viewtouch-menu"_s}, {u"version"_s, 1}, {u"store"_s, u"ILSA price book"_s},
                           {u"categories"_s, cats}, {u"choiceGroups"_s, QVariantList{}}, {u"items"_s, items}, {u"pictures"_s, QVariantMap{}}};

    test::RecordingSink sink;
    app::PosService pos{test::seedPosData(true), &sink};
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const int before = int(pos.shared()->menu.size());
    QElapsedTimer t;

    t.start();
    const int added = pos.importMenuFile(file);
    const qint64 importMs = t.elapsed();
    CHECK(added == kItems);
    CHECK(int(pos.shared()->menu.size()) == before + kItems);

    t.restart();
    const QVariantList list = pos.menuItems();
    const qint64 listMs = t.elapsed();
    const qint64 listBytes = QJsonDocument(QJsonArray::fromVariantList(list)).toJson(QJsonDocument::Compact).size();
    t.restart();
    (void)pos.menuItems();   // memoized
    const qint64 listAgainMs = t.elapsed();

    // A scan, the way the register finds the item (LayoutController::scanned): the code against every
    // item's number and barcodes.
    t.restart();
    int found = 0;
    for (int n = 0; n < 20; ++n) {
        const QString code = QString::number(800000000LL + kItems - 1 - n * 7);
        for (const QVariant &v : pos.menuItems()) {
            const QVariantMap m = v.toMap();
            if (m.value(u"barcodes"_s).toString().split(u", "_s).contains(code)) { ++found; break; }
        }
    }
    const qint64 scanMs = t.elapsed() / 20;
    CHECK(found == 20);

    // Saved and loaded back (a restart).
    QTemporaryDir dir;
    const QString db = dir.filePath(u"catalog.db"_s);
    t.restart();
    {
        storage::PosStore store(db);
        REQUIRE(store.open());
        REQUIRE(store.seed(pos.shared()->settings, pos.shared()->menu, test::seedPosData().employees, nullptr, {}));
    }
    const qint64 saveMs = t.elapsed();
    t.restart();
    storage::PosStore store(db);
    REQUIRE(store.open());
    auto loaded = store.load();
    const qint64 loadMs = t.elapsed();
    REQUIRE(loaded);
    CHECK(int(loaded->menu.size()) == before + kItems);

    std::printf("\n[catalog] %d items: import %lld ms, item list %lld ms (%lld KB as JSON; again %lld ms), "
                "one scan %lld ms, save all %lld ms, load %lld ms\n",
                kItems, importMs, listMs, listBytes / 1024, listAgainMs, scanMs, saveMs, loadMs);
    std::fflush(stdout);
    CHECK(importMs < 600000);
    CHECK(scanMs < 2000);
}
