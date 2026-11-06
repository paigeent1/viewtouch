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

#include <algorithm>
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

    // ALPHA LOOK UP (Item Look Up) typing on it: the same answers as the way it was first written (every item
    // with a UPC, in menu order, the first 40 whose name has every word / whose UPC is the one typed, leading
    // zeros or not; then by name), and quick, one more letter at a time.
    const auto oldWay = [&](const QString &typed) {
        const auto digits = [](const QString &s) {
            QString out;
            for (QChar ch : s)
                if (ch.isDigit())
                    out += ch;
            return out;
        };
        const auto noZeros = [](const QString &s) {
            qsizetype i = 0;
            while (i < s.size() - 1 && s.at(i) == u'0')
                ++i;
            return s.mid(i);
        };
        const QString query = typed.trimmed(), code = digits(query);
        const bool byCode = code.size() >= 4 && code.size() == query.size();
        const QStringList words = query.toLower().split(u' ', Qt::SkipEmptyParts);
        QList<std::pair<QString, QString>> found;   // name, id
        for (const core::MenuItem &m : pos.shared()->menu) {
            QStringList codes;
            for (const std::string &b : m.barcodes)
                if (const QString c = digits(QString::fromStdString(b)); !c.isEmpty())
                    codes << c;
            for (const core::RecipeLine &r : m.recipe)
                if (r.ingredientId.starts_with("__parent__:"))
                    if (const QString c = digits(QString::fromStdString(r.ingredientId.substr(11))); !c.isEmpty())
                        codes << c;
            if (codes.isEmpty())
                continue;
            const QString name = QString::fromStdString(m.name);
            const bool hit = byCode ? std::ranges::any_of(codes, [&](const QString &c) { return noZeros(c) == noZeros(code); })
                                    : !words.isEmpty() && std::ranges::all_of(words, [&](const QString &w) { return name.toLower().contains(w); });
            if (hit)
                found.append({name, QString::fromStdString(m.id)});
            if (found.size() >= 40)
                break;
        }
        std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return QString::localeAwareCompare(a.first, b.first) < 0; });
        QStringList ids;
        for (const auto &f : found)
            ids << f.second;
        return ids;
    };
    const auto lookUp = [&](const QString &typed) {
        REQUIRE(pos.findRetailLabels(typed));
        QStringList ids;
        for (const QVariant &v : pos.retailLabels()[u"matches"_s].toList())
            ids << v.toMap()[u"id"_s].toString();
        return ids;
    };
    REQUIRE(pos.browseRetailLabels({}));   // the screen opens on the tree
    (void)pos.retailLabels();
    qint64 slowestMs = 0;
    for (const QString &typed : {u"r"_s, u"ro"_s, u"rob"_s, u"robusto 3"_s, u"robusto 32"_s, u"robusto 329"_s, u"q"_s, u"qz"_s, u"qzx"_s}) {
        t.restart();
        const QStringList ids = lookUp(typed);
        slowestMs = std::max(slowestMs, t.elapsed());
        CHECK(ids == oldWay(typed));
    }
    for (const QString &typed : {u"brand 12 "_s, u"BRAND 1"_s, u"robusto 32999"_s, u"800000123"_s, u"0800000123"_s,
                                 u"0700000005"_s, u"123"_s, u"pipe tobacco 79"_s, u"zzzz"_s})
        CHECK(lookUp(typed) == oldWay(typed));
    CHECK(lookUp(u"brand 12"_s).size() == 40);   // still the first 40
    t.restart();
    for (int n = 0; n < 200; ++n)   // asked again with nothing changed (every change of any kind asks): kept
        (void)pos.retailLabels();
    const qint64 againMs = t.elapsed();
    std::printf("\n[catalog] Item Look Up: slowest search while typing %lld ms; 200 reads unchanged %lld ms\n", slowestMs, againMs);
    CHECK(slowestMs < 50);
    CHECK(againMs < 50);

    // Find on the order screen, on the main computer: the first after a menu change gets every name ready
    // (once), then each search while typing is quick.
    t.restart();
    REQUIRE(pos.findItems(u"x"_s));
    (void)pos.itemSearch();
    const qint64 findReadyMs = t.elapsed();
    qint64 findSlowestMs = 0;
    for (const QString &typed : {u"r"_s, u"ro"_s, u"rob"_s, u"robusto 3"_s, u"robusto 32"_s, u"robusto 329"_s, u"q"_s, u"qz"_s}) {
        t.restart();
        REQUIRE(pos.findItems(typed));
        const QVariantMap found = pos.itemSearch();
        findSlowestMs = std::max(findSlowestMs, t.elapsed());
        CHECK(found[u"items"_s].toList().size() <= 300);
    }
    REQUIRE(pos.findItems(u"robusto 32999"_s));
    CHECK(pos.itemSearch()[u"items"_s].toList().size() == 1);
    REQUIRE(pos.findItems(u"0800000123"_s));   // a UPC, leading zero or not
    REQUIRE(pos.itemSearch()[u"items"_s].toList().size() == 1);
    CHECK(pos.itemSearch()[u"items"_s].toList().first().toMap()[u"name"_s].toString() == u"CIGAR BRAND 4 ROBUSTO 123"_s);   // the import gives ids of its own
    std::printf("[catalog] Find: names made ready %lld ms, slowest search while typing %lld ms\n", findReadyMs, findSlowestMs);
    std::fflush(stdout);
    CHECK(findReadyMs < 5000);
    CHECK(findSlowestMs < 50);

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

// Find on the order screen (and Item Look Up): every word typed, in any order; the best first.
TEST_CASE("Find: every word in the name, best first; numbers and UPCs", "[find]")
{
    QTemporaryDir home;   // no price book of Neil's here (Item Look Up lists ViewTouch's items)
    REQUIRE(home.isValid());
    const QByteArray oldHome = qgetenv("HOME");
    qputenv("HOME", home.path().toUtf8());
    app::PosData data = test::seedPosData();
    const auto add = [&](const char *id, const char *name, const char *number = "", std::vector<std::string> barcodes = {}) {
        core::MenuItem m;
        m.id = id;
        m.name = name;
        m.family = "retail";
        m.number = number;
        m.barcodes = std::move(barcodes);
        data.menu.push_back(m);
    };
    add("oliva-v", "OLIVA V MELANIO FIGURADO", "", {"0123456789012"});
    add("oliva-m", "OLIVA MELANIO ROBUSTO");
    add("melanio-oliva", "MELANIO OLIVA TORO");
    add("camelot", "CAMELOT OLIVAS");
    add("oliva-g", "OLIVA SERIE G");
    add("zig", "ZIG ZAG PAPERS", "9871");
    add("cones", "CONES", "98710");
    add("wraps", "AAA 9871 WRAPS");
    test::RecordingSink sink;
    app::PosService pos{std::move(data), &sink};
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const auto find = [&](const QString &typed) {
        REQUIRE(pos.findItems(typed));
        QStringList ids;
        for (const QVariant &v : pos.itemSearch()[u"items"_s].toList())
            ids << v.toMap()[u"id"_s].toString();
        return ids;
    };
    // The name starts with the first word (then A-Z), every word starts a word, a word is inside a word.
    CHECK(find(u"oliva mel"_s) == QStringList{u"oliva-m"_s, u"oliva-v"_s, u"melanio-oliva"_s, u"camelot"_s});
    CHECK(find(u"Mel  Oliva "_s) == QStringList{u"melanio-oliva"_s, u"oliva-m"_s, u"oliva-v"_s, u"camelot"_s});
    CHECK(find(u"mel oliva"_s) == QStringList{u"melanio-oliva"_s, u"oliva-m"_s, u"oliva-v"_s, u"camelot"_s});
    CHECK(find(u"mel olivas"_s) == QStringList{u"camelot"_s});   // one more letter
    CHECK(find(u"oliva"_s).size() == 5);
    CHECK(find(u"oliva xyz"_s).isEmpty());
    // Its own number first, then a number that starts so, then a name with it.
    CHECK(find(u"9871"_s) == QStringList{u"zig"_s, u"cones"_s, u"wraps"_s});
    // A UPC, with or without its leading zero.
    CHECK(find(u"0123456789012"_s) == QStringList{u"oliva-v"_s});
    CHECK(find(u"123456789012"_s) == QStringList{u"oliva-v"_s});
    CHECK(find(u""_s).isEmpty());
    // Item Look Up finds by the same words (it lists only the items with a UPC).
    REQUIRE(pos.findRetailLabels(u"mel oliva"_s));
    QStringList looked;
    for (const QVariant &v : pos.retailLabels()[u"matches"_s].toList())
        looked << v.toMap()[u"id"_s].toString();
    CHECK(looked == QStringList{u"oliva-v"_s});
    // Asked again with nothing changed: the very same answer.
    REQUIRE(pos.findItems(u"oliva mel"_s));
    const qint64 rev = pos.itemSearch()[u"rev"_s].toLongLong();
    CHECK(pos.itemSearch()[u"rev"_s].toLongLong() == rev);
    qputenv("HOME", oldHome);
}
