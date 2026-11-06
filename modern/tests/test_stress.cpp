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

    // A scan, the way the register finds the item now: the main computer asks the menu (itemForCode).
    t.restart();
    int found = 0;
    for (int n = 0; n < 20; ++n) {
        const QString code = QString::number(800000000LL + kItems - 1 - n * 7);
        found += pos.itemForCode(code).isEmpty() ? 0 : 1;
    }
    const qint64 scanMs = t.elapsed() / 20;
    CHECK(found == 20);
    t.restart();
    CHECK(pos.itemForCode(u"999999999999"_s).isEmpty());   // not on file: every item looked at
    const qint64 missMs = t.elapsed();

    // The next part of a price book, into the menu that already has the first: 8,000 more items.
    QVariantList more;
    for (int i = 0; i < 8000; ++i)
        more << QVariantMap{{u"id"_s, u"ilsa-x%1"_s.arg(i)}, {u"name"_s, u"PIPE TOBACCO %1"_s.arg(i)},
                            {u"price"_s, 9.0}, {u"taxClass"_s, u"merchandise"_s}, {u"family"_s, u"ilsa-dept-1"_s},
                            {u"barcodes"_s, QStringList{QString::number(900000000LL + i)}}};
    QVariantMap part = file;
    part[u"items"_s] = more;
    t.restart();
    CHECK(pos.importMenuFile(part) == 8000);
    const qint64 partMs = t.elapsed();
    // The whole price book again, now with makers: nothing added, makers filled in.
    QVariantList again;
    for (const QVariant &v : items) {
        QVariantMap m = v.toMap();
        m[u"manufacturer"_s] = u"BRAND %1"_s.arg(m[u"id"_s].toString().right(3));
        again << m;
    }
    QVariantMap refill = file;
    refill[u"items"_s] = again;
    t.restart();
    CHECK(pos.importMenuFile(refill) == 0);
    const qint64 refillMs = t.elapsed();
    CHECK(pos.shared()->menu[std::size_t(before)].manufacturer == "BRAND 000");   // the first item of the price book

    // The Labels screen with it all: one search.
    REQUIRE(pos.findRetailLabels(u"robusto 32999"_s));
    t.restart();
    const QVariantMap labels = pos.retailLabels();
    const qint64 labelsMs = t.elapsed();
    CHECK(labels[u"matches"_s].toList().size() == 1);

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
    CHECK(int(loaded->menu.size()) == before + kItems + 8000);

    std::printf("\n[catalog] %d items: import %lld ms, item list %lld ms (%lld KB as JSON; again %lld ms), "
                "one scan %lld ms (not on file %lld ms), next 8000-item part %lld ms, makers filled in %lld ms, "
                "Labels search %lld ms, save all %lld ms, load %lld ms\n",
                kItems, importMs, listMs, listBytes / 1024, listAgainMs, scanMs, missMs, partMs, refillMs, labelsMs, saveMs, loadMs);
    std::fflush(stdout);
    CHECK(importMs < 600000);
    CHECK(scanMs < 50);
    CHECK(partMs < 60000);
}
