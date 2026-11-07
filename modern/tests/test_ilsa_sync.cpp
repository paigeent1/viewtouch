#include <catch2/catch_test_macros.hpp>

#include "app/ilsa_sync.hh"
#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// The ILSA price book's automatic sync (owner's rules, 2026-11-07): new items added, prices follow ILSA,
// nothing set in ViewTouch is changed or removed, differences only reported.

namespace {

QVariantMap book(const QVariantList &items)
{
    const QVariantList cats{
        QVariantMap{{u"id"_s, u"ilsa-cigars"_s}, {u"name"_s, u"CIGARS"_s}, {u"taxClass"_s, u"merchandise"_s}, {u"group"_s, u"CIGARS"_s}},
        QVariantMap{{u"id"_s, u"ilsa-pipes"_s}, {u"name"_s, u"PIPES"_s}, {u"taxClass"_s, u"merchandise"_s}, {u"group"_s, u"TOBACCO"_s},
                    {u"idCheck"_s, true}}};
    return {{u"format"_s, u"viewtouch-menu"_s}, {u"version"_s, 1}, {u"store"_s, u"ILSA price book"_s}, {u"exported"_s, u"2026-11-07 10:00"_s},
            {u"categories"_s, cats}, {u"choiceGroups"_s, QVariantList{}}, {u"items"_s, items}, {u"pictures"_s, QVariantMap{}}};
}

QVariantMap ilsaItem(const QString &plu, const QString &name, double price, const QString &dept, const QStringList &codes)
{
    return {{u"id"_s, u"ilsa-"_s + plu}, {u"name"_s, name}, {u"price"_s, price}, {u"taxClass"_s, u"merchandise"_s},
            {u"family"_s, dept}, {u"barcodes"_s, codes}};
}

core::MenuItem here(const std::string &id, const std::string &name, std::int64_t cents, std::vector<std::string> codes)
{
    core::MenuItem m;
    m.id = id;
    m.name = name;
    m.family = "cigars";
    m.price = core::Money::fromCents(cents);
    m.taxClass = core::TaxClass::Merchandise;
    m.barcodes = std::move(codes);
    return m;
}

const core::MenuItem *byId(PosService &pos, const std::string &id)
{
    const auto &menu = pos.shared()->menu;
    const auto it = std::ranges::find(menu, id, &core::MenuItem::id);
    return it == menu.end() ? nullptr : &*it;
}

int rowsOf(const QVariantMap &r, const QString &kind)
{
    return int(std::ranges::count_if(r[u"rows"_s].toList(), [&](const QVariant &v) { return v.toMap()[u"kind"_s] == kind; }));
}

QStringList linesOf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).split(u'\n', Qt::SkipEmptyParts) : QStringList();
}

} // namespace

TEST_CASE("ILSA sync: adds new items, follows prices, keeps what ViewTouch set", "[ilsasync]")
{
    test::RecordingSink sink;
    PosService pos{test::seedPosData(true), &sink};   // nobody logged in: the store's own job
    auto &s = *pos.shared();
    core::MenuCategory cigars;
    cigars.id = "cigars";
    cigars.name = "CIGARS";
    cigars.group = "CIGARS";
    s.settings.menuCategories.push_back(cigars);
    // The name fixed in ViewTouch (5+ is 5 1/8), its size filled in, and a primary UPC swapped here:
    // ILSA's PLU (222) is now its second barcode.
    core::MenuItem padron = here("padron-2000", "PADRON 2000 5⅛ X 50", 1000, {"111", "222"});
    padron.length = 5.125;
    padron.ringGauge = 50;
    padron.ringGauge2 = 52;
    s.menu.push_back(padron);
    s.menu.push_back(here("ilsa-555", "TUBE", 300, {}));          // known by its ILSA id only
    s.menu.push_back(here("old-cigar", "OLD CIGAR", 500, {"999"}));  // ILSA no longer has it
    const std::size_t before = s.menu.size();
    const int revision = s.adminRevision;

    const QVariantMap file = book({ilsaItem(u"222"_s, u"PADRON 2000 5+ X 50"_s, 11.50, u"ilsa-cigars"_s, {u"222"_s, u"333"_s}),
                                   ilsaItem(u"555"_s, u"TUBE"_s, 3.00, u"ilsa-pipes"_s, {u"555"_s}),
                                   ilsaItem(u"777"_s, u"NEW PIPE"_s, 40.00, u"ilsa-pipes"_s, {u"777"_s, u"0777"_s})});
    const QVariantMap r = pos.syncPriceBook(file);
    REQUIRE(r[u"ok"_s].toBool());
    CHECK(r[u"added"_s].toInt() == 1);
    CHECK(r[u"prices"_s].toInt() == 1);
    CHECK(r[u"barcodes"_s].toInt() == 2);   // 333 on the Padron, 555 on the tube
    CHECK(r[u"names"_s].toInt() == 1);
    CHECK(r[u"departments"_s].toInt() == 1);   // the tube: CIGARS here, PIPES in ILSA
    CHECK(r[u"gone"_s].toInt() == 1);
    CHECK(r[u"taxes"_s].toInt() == 0);
    CHECK(s.adminRevision == revision + 1);    // one new menu for every screen

    // Matched by its second barcode: the price follows ILSA; the name, sizes and barcodes stay.
    const core::MenuItem *p = byId(pos, "padron-2000");
    REQUIRE(p);
    CHECK(p->price.cents() == 1150);
    CHECK(p->name == "PADRON 2000 5⅛ X 50");
    CHECK(p->length == 5.125);
    CHECK(p->ringGauge == 50);
    CHECK(p->ringGauge2 == 52);
    CHECK(p->barcodes == std::vector<std::string>{"111", "222", "333"});
    CHECK(sink.menu.contains("padron-2000"));   // saved
    const QVariantList rows = r[u"rows"_s].toList();
    const auto nameRow = std::ranges::find_if(rows, [](const QVariant &v) { return v.toMap()[u"kind"_s] == u"name"_s; });
    REQUIRE(nameRow != rows.end());
    CHECK(nameRow->toMap()[u"after"_s] == u"PADRON 2000 5+ X 50"_s);

    // Matched by its ILSA id: its barcode added, its category kept.
    const core::MenuItem *tube = byId(pos, "ilsa-555");
    REQUIRE(tube);
    CHECK(tube->barcodes == std::vector<std::string>{"555"});
    CHECK(tube->family == "cigars");

    // Gone from ILSA: only reported.
    CHECK(byId(pos, "old-cigar"));
    // The new item, in its department (made here, on the ID-check list), known by its ILSA id.
    const core::MenuItem *pipe = byId(pos, "ilsa-777");
    REQUIRE(pipe);
    CHECK(pipe->price.cents() == 4000);
    CHECK(pipe->barcodes == std::vector<std::string>{"777", "0777"});
    const auto pipes = std::ranges::find(s.settings.menuCategories, std::string("PIPES"), &core::MenuCategory::name);
    REQUIRE(pipes != s.settings.menuCategories.end());
    CHECK(pipe->family == pipes->id);
    CHECK(pipes->retailOnly);
    CHECK(std::ranges::find(s.settings.tobaccoCategories, pipes->id) != s.settings.tobaccoCategories.end());
    CHECK(s.menu.size() == before + 1);
    // In the Changes report as the ILSA sync's.
    REQUIRE(!pos.changesToday().empty());
    CHECK(pos.changesToday().back().who == "ILSA sync");

    // The same book again: nothing to do, no new menu.
    const QVariantMap again = pos.syncPriceBook(file);
    CHECK(again[u"added"_s].toInt() == 0);
    CHECK(again[u"prices"_s].toInt() == 0);
    CHECK(again[u"barcodes"_s].toInt() == 0);
    CHECK(s.adminRevision == revision + 1);
    CHECK(s.menu.size() == before + 1);

    // Two ILSA items on one item here (merged by the owner) that disagree: the price stays, reported.
    const QVariantMap twice = pos.syncPriceBook(book({ilsaItem(u"222"_s, u"PADRON 2000 5+ X 50"_s, 12.00, u"ilsa-cigars"_s, {u"222"_s}),
                                                     ilsaItem(u"111"_s, u"PADRON 2000"_s, 13.00, u"ilsa-cigars"_s, {u"111"_s})}));
    CHECK(twice[u"conflicts"_s].toInt() == 1);
    CHECK(byId(pos, "padron-2000")->price.cents() == 1150);
    // A file that isn't a price book changes nothing.
    CHECK_FALSE(pos.syncPriceBook({{u"format"_s, u"other"_s}})[u"ok"_s].toBool());
}

TEST_CASE("ILSA sync: the logs, each difference once", "[ilsasync]")
{
    QTemporaryDir dir;
    PosService pos{test::seedPosData(true), nullptr};
    auto &s = *pos.shared();
    s.menu.push_back(here("padron-2000", "PADRON 2000 5⅛ X 50", 1000, {"222"}));
    app::IlsaSync sync(&pos);
    sync.setLogDir(dir.path());
    CHECK(sync.on());   // on by default

    const QDateTime at(QDate(2026, 11, 7), QTime(10, 15));
    const QString first = sync.writeLogs(pos.syncPriceBook(book({ilsaItem(u"222"_s, u"PADRON 2000 5+ X 50"_s, 11.50, u"ilsa-cigars"_s, {u"222"_s}),
                                                                  ilsaItem(u"777"_s, u"NEW PIPE"_s, 40.00, u"ilsa-pipes"_s, {u"777"_s})})), at);
    CHECK(first.contains(u"1 added"_s));
    CHECK(first.contains(u"1 prices changed"_s));
    CHECK(first.contains(u"1 names differ"_s));
    const QDir d(dir.path());
    const QStringList log = linesOf(d.filePath(u"sync-log.csv"_s));
    REQUIRE(log.size() == 4);   // header, added, price, name
    CHECK(log[0] == u"Time,Item,UPC,What,Before,After"_s);
    CHECK(std::ranges::any_of(log, [](const QString &l) { return l.contains(u"ILSA name differs"_s); }));
    CHECK(std::ranges::any_of(log, [](const QString &l) { return l.contains(u",price,"_s) && l.contains(u"11.50"_s); }));
    const QStringList prices = linesOf(d.filePath(u"price-changes-20261107-1015.csv"_s));
    REQUIRE(prices.size() == 2);
    CHECK(prices[1].startsWith(u"PADRON 2000 5⅛ X 50,222,"_s));

    // The next run: the name difference isn't written again; no price changes, no price file.
    const QString second = sync.writeLogs(pos.syncPriceBook(book({ilsaItem(u"222"_s, u"PADRON 2000 5+ X 50"_s, 11.50, u"ilsa-cigars"_s, {u"222"_s}),
                                                                   ilsaItem(u"777"_s, u"NEW PIPE"_s, 40.00, u"ilsa-pipes"_s, {u"777"_s})})),
                                          at.addSecs(900));
    CHECK(second == u"no changes"_s);
    CHECK(linesOf(d.filePath(u"sync-log.csv"_s)).size() == 4);
    CHECK_FALSE(QFile::exists(d.filePath(u"price-changes-20261107-1030.csv"_s)));
    CHECK(linesOf(d.filePath(u"runs.log"_s)).size() == 2);
    CHECK(sync.logTail(50).size() >= 5);

    sync.setOn(false);   // Site Setup's switch: kept in the store's settings
    CHECK_FALSE(s.settings.ilsaSyncOn);
    CHECK_FALSE(app::settingsFromJson(app::toJson(s.settings)).ilsaSyncOn);
}

// The whole price book (follows test_stress.cpp's catalog test): 33,000 items added by the sync, then a
// sync with 3,300 price changes. The limits only catch something far too slow for a Pi.
TEST_CASE("ILSA sync: 33,000 items, quick", "[catalog][ilsasync]")
{
    constexpr int kItems = 33000;
    QVariantList items;
    for (int i = 0; i < kItems; ++i)
        items << ilsaItem(QString::number(700000000 + i), u"CIGAR BRAND %1 ROBUSTO %2"_s.arg(i / 25).arg(i), 5.0 + (i % 400) / 10.0,
                          i % 2 ? u"ilsa-cigars"_s : u"ilsa-pipes"_s,
                          {QString::number(700000000LL + i), QString::number(800000000LL + i)});
    test::RecordingSink sink;
    PosService pos{test::seedPosData(true), &sink};
    const int revision = pos.shared()->adminRevision;
    QElapsedTimer t;
    t.start();
    const QVariantMap first = pos.syncPriceBook(book(items));
    const qint64 firstMs = t.elapsed();
    CHECK(first[u"added"_s].toInt() == kItems);

    for (int i = 0; i < kItems; i += 10) {
        QVariantMap m = items[i].toMap();
        m[u"price"_s] = m[u"price"_s].toDouble() + 0.25;
        items[i] = m;
    }
    t.restart();
    const QVariantMap next = pos.syncPriceBook(book(items));
    const qint64 nextMs = t.elapsed();
    CHECK(next[u"added"_s].toInt() == 0);
    CHECK(next[u"prices"_s].toInt() == kItems / 10);
    CHECK(rowsOf(next, u"name"_s) == 0);
    CHECK(pos.shared()->adminRevision == revision + 2);   // one per sync
    WARN("ILSA sync, 33,000 items: first (all added) " << firstMs << " ms, next (3,300 prices) " << nextMs << " ms");
    CHECK(firstMs < 8000);
    CHECK(nextMs < 5000);
}
