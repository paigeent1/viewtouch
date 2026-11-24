#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"
#include "storage/backup.hh"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Menu Builder > Mass Updates (app/pos_mass_update.cpp): the grid paged on the server, previews that change
// nothing, Apply and one Undo, change sets matched by UPC at another store, the price book permission.

namespace {

// A UPC-A for a number (its check digit worked out).
std::string upcOf(long long n)
{
    std::string s = std::to_string(n);
    s.insert(0, 11 - s.size(), '0');
    int sum = 0;
    for (int i = 0; i < 11; ++i)
        sum += (s[std::size_t(10 - i)] - '0') * (i % 2 == 0 ? 3 : 1);
    return s + char('0' + (10 - sum % 10) % 10);
}

// The seed's staff and menu, plus an admin (PIN 9999) and `count` retail items in CIGARETTES (most with a
// cost; every tenth with none), over three brands.
app::PosData retailStore(int count, long long firstUpc = 10000000000LL)
{
    app::PosData data = test::seedPosData();
    core::Employee admin;
    admin.id = "admin-1";
    admin.name = "Alex (Admin)";
    admin.role = "admin";
    admin.pinSalt = "salt";
    admin.pinHash = app::hashPin(u"9999"_s, admin.pinSalt);
    data.employees.push_back(admin);
    core::MenuCategory c;
    c.id = "cigarettes";
    c.name = "CIGARETTES";
    c.scope = "retail";
    c.retailOnly = true;
    c.taxClass = "merchandise";
    data.settings.menuCategories.push_back(c);
    core::MenuCategory b = c;
    b.id = "cigars";
    b.name = "CIGARS";
    data.settings.menuCategories.push_back(b);
    const char *brands[] = {"MONTE", "ALPHA", "BRAVO"};
    for (int i = 0; i < count; ++i) {
        core::MenuItem m;
        m.id = "pb-" + std::to_string(i);
        m.manufacturer = brands[i % 3];
        m.name = std::string(brands[i % 3]) + " PACK " + std::to_string(i);
        m.family = "cigarettes";
        m.taxClass = core::TaxClass::Merchandise;
        m.barcodes = {upcOf(firstUpc + i)};
        m.price = Money::fromCents(1000 + (i % 20) * 100);   // 10.00 .. 29.00
        if (i % 10 != 9)
            m.recipe.push_back({"__cost__", 6.0 + double(i % 5)});
        data.menu.push_back(m);
    }
    return data;
}

QVariantMap state(PosService &pos) { return pos.massUpdate(); }

QStringList ids(const QVariantList &rows)
{
    QStringList out;
    for (const QVariant &r : rows)
        out << r.toMap()[u"id"_s].toString();
    return out;
}

const core::MenuItem &item(PosService &pos, const QString &id)
{
    const core::MenuItem *m = pos.findItem(id);
    REQUIRE(m);
    return *m;
}

std::int64_t costCents(const core::MenuItem &m)
{
    for (const core::RecipeLine &rl : m.recipe)
        if (rl.ingredientId == "__cost__")
            return std::llround(rl.quantity * 100.0);
    return 0;
}

} // namespace

TEST_CASE("Mass updates: 20,000 retail items, one page of 200 sent; find, sort, ranges", "[massupdates]")
{
    PosService pos(retailStore(20000), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    QElapsedTimer t;
    t.start();
    REQUIRE(pos.massView({{u"scope"_s, u"retail"_s}}));
    QVariantMap s = state(pos);
    CHECK(s[u"total"_s].toInt() == 20000);
    CHECK(s[u"count"_s].toInt() == 20000);
    CHECK(s[u"rows"_s].toList().size() == 200);         // only one page
    CHECK(s[u"pages"_s].toInt() == 100);
    REQUIRE(pos.massView({{u"page"_s, 99}}));
    CHECK(state(pos)[u"rows"_s].toList().size() == 200);
    CHECK(state(pos)[u"page"_s].toInt() == 99);
    INFO("first look and paging took " << t.elapsed() << " ms");
    CHECK(t.elapsed() < 20000);

    // Find by UPC (without its check digit), by name.
    REQUIRE(pos.massView({{u"find"_s, QString::fromStdString(upcOf(10000000123LL)).left(11)}}));
    s = state(pos);
    REQUIRE(s[u"count"_s].toInt() == 1);
    CHECK(ids(s[u"rows"_s].toList()) == QStringList{u"pb-123"_s});

    // A range on the description, then on the price; sorted by price high to low.
    // (the end taken as a start: 1998 and 19980, 19983, 19986, 19989)
    REQUIRE(pos.massView({{u"field"_s, u"name"_s}, {u"from"_s, u"MONTE PACK 1998"_s}, {u"to"_s, u"MONTE PACK 1998"_s}}));
    s = state(pos);
    for (const QVariant &r : s[u"rows"_s].toList())
        CHECK(r.toMap()[u"name"_s].toString().startsWith(u"MONTE PACK 1998"_s));
    CHECK(s[u"count"_s].toInt() == 5);
    REQUIRE(pos.massView({{u"field"_s, u"price"_s}, {u"from"_s, u"10"_s}, {u"to"_s, u"12"_s}, {u"sort"_s, u"price"_s}, {u"desc"_s, true}}));
    s = state(pos);
    CHECK(s[u"count"_s].toInt() == 3000);              // 10.00, 11.00, 12.00: 3 of every 20
    CHECK(s[u"rows"_s].toList().first().toMap()[u"price"_s] == u"12.00"_s);
    // The tree: a brand.
    REQUIRE(pos.massView({{u"category"_s, u"cigarettes"_s}, {u"path"_s, QStringList{u"ALPHA"_s}}}));
    CHECK(state(pos)[u"count"_s].toInt() == 6667);
    CHECK(state(pos)[u"brands"_s].toStringList() == QStringList({u"ALPHA"_s, u"BRAVO"_s, u"MONTE"_s}));
}

TEST_CASE("Mass updates: All ticks every item found, not only the page; None clears", "[massupdates]")
{
    PosService pos(retailStore(1000), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.massView({{u"category"_s, u"cigarettes"_s}, {u"path"_s, QStringList{u"MONTE"_s}}}));
    REQUIRE(pos.massChooseAll());
    CHECK(state(pos)[u"chosen"_s].toInt() == 334);
    REQUIRE(pos.massChooseNone());
    CHECK(state(pos)[u"chosen"_s].toInt() == 0);
    REQUIRE(pos.massChoose({u"pb-1"_s, u"pb-2"_s}, true));
    CHECK(state(pos)[u"chosen"_s].toInt() == 2);
}

TEST_CASE("Mass updates: a preview changes nothing; a margin price skips and lists items with no cost", "[massupdates]")
{
    PosService pos(retailStore(30), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.massView({}));
    REQUIRE(pos.massChoose({u"pb-0"_s, u"pb-1"_s, u"pb-9"_s}, true));   // pb-9: no cost
    const std::vector<core::MenuItem> before = pos.menu();
    const std::size_t changesBefore = pos.changesToday().size();
    REQUIRE(pos.massPreview({{u"kind"_s, u"margin"_s}, {u"amount"_s, u"40"_s}, {u"round"_s, u"9up"_s}}));
    CHECK(pos.menu() == before);                       // nothing changed
    CHECK(pos.changesToday().size() == changesBefore);
    const QVariantMap p = state(pos)[u"preview"_s].toMap();
    REQUIRE(p[u"count"_s].toInt() == 2);
    QVariantMap first;
    for (const QVariant &l : p[u"lines"_s].toList())
        if (l.toMap()[u"item"_s] == u"MONTE PACK 0"_s)
            first = l.toMap();
    CHECK(first[u"item"_s] == u"MONTE PACK 0"_s);
    CHECK(first[u"before"_s] == u"10.00"_s);
    CHECK(first[u"after"_s] == u"10.09"_s);            // 6.00 / 0.6 = 10.00 -> 9 up
    REQUIRE(p[u"skipped"_s].toList().size() == 1);
    CHECK(p[u"skipped"_s].toList()[0].toMap()[u"item"_s] == u"MONTE PACK 9"_s);
    REQUIRE(pos.massCancel());
    CHECK_FALSE(state(pos).contains(u"preview"_s));
    CHECK(pos.menu() == before);
}

TEST_CASE("Mass updates: a manager without the price book can't apply; an admin can", "[massupdates][pricebook]")
{
    PosService pos(retailStore(30), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));               // Morgan: a manager
    REQUIRE(pos.massView({}));
    REQUIRE(pos.massChoose({u"pb-0"_s}, true));
    REQUIRE(pos.massPreview({{u"kind"_s, u"price"_s}, {u"how"_s, u"set"_s}, {u"amount"_s, u"12.34"_s}}));
    CHECK_FALSE(state(pos)[u"canApply"_s].toBool());
    CHECK_FALSE(pos.massApply());
    CHECK(item(pos, u"pb-0"_s).price.cents() == 1000);
    // A cost change can't even be shown without it.
    CHECK_FALSE(pos.massPreview({{u"kind"_s, u"cost"_s}, {u"how"_s, u"set"_s}, {u"amount"_s, u"5"_s}}));
    // From the screen: an admin's PIN is asked for (a manager's won't do), then it's applied.
    pos.invoke(u"massApply"_s);
    REQUIRE(pos.approvalInfo()[u"needed"_s].toBool());
    CHECK_FALSE(pos.approve(u"1234"_s));
    REQUIRE(pos.approve(u"9999"_s));
    CHECK(item(pos, u"pb-0"_s).price.cents() == 1234);
    pos.logout();

    // An admin applies by themselves.
    REQUIRE(pos.loginWithPin(u"9999"_s));
    REQUIRE(pos.massView({}));
    REQUIRE(state(pos)[u"canApply"_s].toBool());
    REQUIRE(pos.massChooseNone());
    REQUIRE(pos.massChoose({u"pb-1"_s}, true));
    REQUIRE(pos.massPreview({{u"kind"_s, u"price"_s}, {u"how"_s, u"add"_s}, {u"amount"_s, u"1.00"_s}}));
    REQUIRE(pos.massApply());
    CHECK(item(pos, u"pb-1"_s).price.cents() == 1200);
}

TEST_CASE("Mass updates: add $, add %, set price; a cost update; recorded; Apply then one Undo", "[massupdates]")
{
    PosService pos(retailStore(30), nullptr);
    REQUIRE(pos.loginWithPin(u"9999"_s));
    REQUIRE(pos.massView({}));
    REQUIRE(pos.massChoose({u"pb-0"_s, u"pb-1"_s, u"pb-2"_s}, true));   // 10.00, 11.00, 12.00
    const std::vector<core::MenuItem> original = pos.menu();

    REQUIRE(pos.massPreview({{u"kind"_s, u"price"_s}, {u"how"_s, u"add"_s}, {u"amount"_s, u"0.50"_s}}));
    REQUIRE(pos.massApply());
    CHECK(item(pos, u"pb-0"_s).price.cents() == 1050);
    REQUIRE(pos.massPreview({{u"kind"_s, u"price"_s}, {u"how"_s, u"percent"_s}, {u"amount"_s, u"10"_s}, {u"round"_s, u"5or9up"_s}}));
    REQUIRE(pos.massApply());
    CHECK(item(pos, u"pb-0"_s).price.cents() == 1155);  // 10.50 + 10% = 11.55 (5 or 9 up: as it is)
    CHECK(item(pos, u"pb-2"_s).price.cents() == 1375);  // 12.50 + 10%
    REQUIRE(pos.massPreview({{u"kind"_s, u"price"_s}, {u"how"_s, u"set"_s}, {u"amount"_s, u"9.99"_s}}));
    REQUIRE(pos.massApply());
    CHECK(item(pos, u"pb-2"_s).price.cents() == 999);
    REQUIRE(pos.massPreview({{u"kind"_s, u"cost"_s}, {u"how"_s, u"percent"_s}, {u"amount"_s, u"-50"_s}}));
    REQUIRE(pos.massApply());
    CHECK(costCents(item(pos, u"pb-0"_s)) == 300);       // 6.00 - 50%
    CHECK(costCents(item(pos, u"pb-1"_s)) == 350);

    // Recorded in the Changes report, like other price changes.
    bool price = false, cost = false;
    for (const core::Change &c : pos.changesToday()) {
        price = price || (c.field == "Price" && c.item == "MONTE PACK 0");
        cost = cost || (c.field == "Cost" && c.item == "ALPHA PACK 1");
    }
    CHECK(price);
    CHECK(cost);
    // Four change sets applied here.
    CHECK(state(pos)[u"sets"_s].toList().size() == 4);

    // Undo: the last batch (the costs), all of it.
    REQUIRE(pos.massUndo());
    CHECK(costCents(item(pos, u"pb-0"_s)) == 600);
    CHECK(costCents(item(pos, u"pb-1"_s)) == 700);
    CHECK(item(pos, u"pb-2"_s).price.cents() == 999);   // (the price set before stays)
    CHECK_FALSE(pos.massUndo());                         // once
}

TEST_CASE("Mass updates: Apply then Undo puts back every item, fields too", "[massupdates]")
{
    PosService pos(retailStore(500), nullptr);
    REQUIRE(pos.loginWithPin(u"9999"_s));
    REQUIRE(pos.massView({{u"path"_s, QStringList{u"BRAVO"_s}}}));
    REQUIRE(pos.massChooseAll());
    const std::vector<core::MenuItem> original = pos.menu();
    REQUIRE(pos.massPreview({{u"kind"_s, u"margin"_s}, {u"amount"_s, u"35"_s}, {u"round"_s, u"9near"_s}}));
    const int count = state(pos)[u"preview"_s].toMap()[u"count"_s].toInt();
    CHECK(count > 100);
    REQUIRE(pos.massApply());
    CHECK_FALSE(pos.menu() == original);
    REQUIRE(pos.massUndo());
    CHECK(pos.menu() == original);

    REQUIRE(pos.massPreview({{u"kind"_s, u"field"_s}, {u"field"_s, u"category"_s}, {u"value"_s, u"cigars"_s}}));
    REQUIRE(pos.massApply());
    CHECK(item(pos, u"pb-2"_s).family == "cigars");
    REQUIRE(pos.massPreview({{u"kind"_s, u"field"_s}, {u"field"_s, u"soldOut"_s}, {u"value"_s, u"yes"_s}}));
    REQUIRE(pos.massApply());
    CHECK_FALSE(item(pos, u"pb-2"_s).available);
    REQUIRE(pos.massUndo());
    CHECK(item(pos, u"pb-2"_s).available);
    CHECK(item(pos, u"pb-2"_s).family == "cigars");     // only the last batch
}

TEST_CASE("Mass updates: a change set exported here, imported at another store by UPC", "[massupdates][stores]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    PosService here(retailStore(50), nullptr);
    here.shared()->massUpdateDir = dir.filePath(u"here"_s);
    REQUIRE(here.loginWithPin(u"9999"_s));
    REQUIRE(here.massView({}));
    REQUIRE(here.massChoose({u"pb-0"_s, u"pb-1"_s, u"pb-49"_s}, true));
    REQUIRE(here.massPreview({{u"kind"_s, u"price"_s}, {u"how"_s, u"set"_s}, {u"amount"_s, u"15.00"_s}, {u"why"_s, u"New state price"_s}}));
    REQUIRE(here.massApply());
    const QVariantList sets = state(here)[u"sets"_s].toList();
    REQUIRE(sets.size() == 1);
    const QVariantMap file = here.massChangeSetFile(sets[0].toMap()[u"id"_s].toString());
    REQUIRE(file[u"format"_s] == u"viewtouch-change-set"_s);
    CHECK(file[u"why"_s] == u"New state price"_s);
    CHECK(file[u"who"_s] == u"Alex (Admin)"_s);
    CHECK(file[u"changes"_s].toList().size() == 3);
    CHECK(QDir(dir.filePath(u"here/change-sets"_s)).entryList({u"*.vtchanges.json"_s}).size() == 1);

    // The other store: the same UPCs under other item ids, and no pb-49 (its UPC isn't there).
    app::PosData other = retailStore(40);
    for (core::MenuItem &m : other.menu)
        if (m.id.starts_with("pb-"))
            m.id = "other-" + m.id.substr(3);
    PosService there(other, nullptr);
    REQUIRE(there.loginWithPin(u"9999"_s));
    REQUIRE(there.massView({}));
    const std::vector<core::MenuItem> before = there.menu();
    REQUIRE(there.massImport(file));
    CHECK(there.menu() == before);                     // a preview first
    const QVariantMap p = state(there)[u"preview"_s].toMap();
    CHECK(p[u"kind"_s] == u"import"_s);
    CHECK(p[u"count"_s].toInt() == 2);
    CHECK(p[u"notFound"_s].toStringList() == QStringList{QString::fromStdString(upcOf(10000000049LL))});
    REQUIRE(there.massApply());
    CHECK(item(there, u"other-0"_s).price.cents() == 1500);
    CHECK(item(there, u"other-1"_s).price.cents() == 1500);
    CHECK(item(there, u"other-2"_s).price.cents() == 1200);
    // Listed there too, as from the first store.
    CHECK(state(there)[u"sets"_s].toList().size() == 1);

    // A store that opens again reads the change sets back.
    PosService again(retailStore(50), nullptr);
    again.shared()->massUpdateDir = dir.filePath(u"here"_s);
    REQUIRE(again.loginWithPin(u"1234"_s));
    REQUIRE(again.massView({}));
    CHECK(state(again)[u"sets"_s].toList().size() == 1);
}

TEST_CASE("Mass updates: New items and No category", "[massupdates]")
{
    PosService pos(retailStore(20), nullptr);
    REQUIRE(pos.loginWithPin(u"9999"_s));
    REQUIRE(pos.massView({{u"newDays"_s, 7}}));
    CHECK(state(pos)[u"count"_s].toInt() == 0);        // all here before
    REQUIRE(pos.saveMenuItemCard({{u"name"_s, u"NEW PACK"_s}, {u"price"_s, u"9.00"_s}, {u"family"_s, u"cigarettes"_s},
                                  {u"isRetail"_s, true}, {u"upc"_s, QString::fromStdString(upcOf(20000000001LL))}}));
    REQUIRE(pos.massView({{u"newDays"_s, 7}}));
    QVariantMap s = state(pos);
    REQUIRE(s[u"count"_s].toInt() == 1);
    CHECK(s[u"rows"_s].toList()[0].toMap()[u"name"_s] == u"NEW PACK"_s);
    REQUIRE(pos.massView({{u"noCategory"_s, true}}));
    CHECK(state(pos)[u"count"_s].toInt() == 0);
}

TEST_CASE("Mass updates: backups take the mass-updates folder too", "[massupdates][backup]")
{
    QTemporaryDir data, backups;
    REQUIRE(data.isValid());
    REQUIRE(backups.isValid());
    const QString mass = QDir(data.path()).filePath(u"mass-updates"_s);
    REQUIRE(QDir().mkpath(mass + u"/change-sets"_s));
    auto write = [](const QString &path, const QByteArray &bytes) {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(bytes);
    };
    write(mass + u"/first-seen.json"_s, "{}");
    write(mass + u"/change-sets/a.vtchanges.json"_s, "{\"id\":\"a\"}");

    const QString dest = QDir(backups.path()).filePath(u"mass-updates"_s);
    CHECK(storage::mirrorFolder(mass, dest) == 2);
    CHECK(QFileInfo::exists(dest + u"/change-sets/a.vtchanges.json"_s));
    CHECK(storage::mirrorFolder(mass, dest) == 0);   // nothing new

    write(mass + u"/first-seen.json"_s, "{\"item-1\":1}");   // changed: copied again
    CHECK(storage::mirrorFolder(mass, dest) == 1);
    QFile copy(dest + u"/first-seen.json"_s);
    REQUIRE(copy.open(QIODevice::ReadOnly));
    CHECK(copy.readAll() == "{\"item-1\":1}");
}
