#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "app/slow_log.hh"
#include "layout_fixture.hh"
#include "layoutcontroller.hh"
#include "net/layout_hub.hh"
#include "net/pairing.hh"
#include "net/pos_server.hh"
#include "net/remote_session.hh"
#include "pos_fixture.hh"
#include "qml_load.hh"
#include "qt_catch.hh"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QMetaProperty>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
using vt::app::PosShared;

// Saving one item on Menu Builder's card (owner 2026-11-26: at HCA, a beer's price took 5-15 seconds to save
// on the Pi's own screen, with 3 tablets and a billboard connected). A timing harness: an HCA-like bar menu
// and a 20,000-item price book, the save, what the Pi's screen reads again, the server's round to the
// tablets. The times are printed ("speed: ..." on stderr); only the results are checked (CI machines vary),
// with a ceiling far above what is expected, so a return of the old slowness shows.

namespace {

template <typename F>
bool waitFor(F condition, int msec = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!condition() && t.elapsed() < msec) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

void say(const QString &line)
{
    std::fprintf(stderr, "speed: %s\n", qPrintable(line));
    std::fflush(stderr);
}

double ms(qint64 ns) { return double(ns) / 1e6; }

QVariantMap row(const QString &name, double price, const QString &category, const QString &pour, const QString &abv,
                const QString &group = {})
{
    QVariantMap r{{u"name"_s, name}, {u"price"_s, price}, {u"category"_s, category}, {u"tax"_s, u"Alcohol"_s},
                  {u"pourOz"_s, pour}, {u"abv"_s, abv}};
    if (!group.isEmpty())
        r.insert(u"productGroup"_s, group);
    return r;
}

// The bar of a store like HCA (made-up names): 18 draft beers in 12oz and 16oz, 16 bottled beers, 80 liquors
// in two pours, 20 wines by the glass and the bottle; the starter food.
QVariantList barRows()
{
    QVariantList rows;
    for (int i = 1; i <= 18; ++i) {
        const QString beer = u"TEST DRAFT %1"_s.arg(i, 2, 10, QChar(u'0'));
        rows << row(beer + u" 12oz"_s, 5.0 + i % 3, u"Draft"_s, u"12"_s, u"5"_s)
             << row(beer + u" 16oz"_s, 7.0 + i % 3, u"Draft"_s, u"16"_s, u"5"_s);
    }
    for (int i = 1; i <= 16; ++i)
        rows << row(u"TEST BOTTLE %1"_s.arg(i, 2, 10, QChar(u'0')), 5.5, u"Bottled Beer"_s, u"12"_s, u"5"_s);
    const QStringList kinds{u"Vodka"_s, u"Bourbon"_s, u"Rum"_s, u"Tequila"_s, u"Gin"_s};
    for (int i = 1; i <= 80; ++i) {
        const QString kind = kinds[i % kinds.size()];
        const QString name = u"TEST %1 %2"_s.arg(kind.toUpper()).arg(i, 2, 10, QChar(u'0'));
        rows << row(name + u" 1.25oz"_s, 7.0, u"Liquor"_s, u"1.25"_s, u"40"_s, kind)
             << row(name + u" 2oz"_s, 10.0, u"Liquor"_s, u"2"_s, u"40"_s, kind);
    }
    for (int i = 1; i <= 20; ++i) {
        const QString name = u"TEST WINE %1"_s.arg(i, 2, 10, QChar(u'0'));
        rows << row(name + u" Glass"_s, 9.0, u"Wine"_s, u"5"_s, u"13"_s)
             << row(name + u" Bottle"_s, 34.0, u"Wine"_s, QString(), u"13"_s);
    }
    return rows;
}

// A price book like BHDAN's: 20,000 items in one Retail department (brands, product groups).
void addPriceBook(app::PosData &d, int count)
{
    core::MenuCategory c;
    c.id = "test-cigars";
    c.name = "TEST CIGARS";
    c.scope = core::kScopeRetail;
    c.retailOnly = true;
    c.taxClass = "merchandise";
    d.settings.menuCategories.push_back(c);
    for (int i = 0; i < count; ++i) {
        core::MenuItem m;
        m.id = u"test-cigar-%1"_s.arg(i, 5, 10, QChar(u'0')).toStdString();
        m.name = u"TEST CIGAR %1"_s.arg(i, 5, 10, QChar(u'0')).toStdString();
        m.family = c.id;
        m.price = Money::fromCents(500 + i % 900);
        m.taxClass = core::TaxClass::Merchandise;
        m.available = true;
        m.barcodes = {u"9%1"_s.arg(i, 11, 10, QChar(u'0')).toStdString()};
        m.productGroup = u"BRAND %1"_s.arg(i % 60).toStdString();
        m.subGroup = u"LINE %1"_s.arg(i % 7).toStdString();
        m.printer = "";
        m.recipe = {{"__retail__", 1.0}, {"__cost__", 3.25}};
        d.menu.push_back(std::move(m));
    }
}

// What Menu Builder's card sends on Save (WidgetMenuBuilder.qml itemDraft): every field of the item.
QVariantMap cardOf(const QVariantMap &i)
{
    QVariantMap c;
    for (const QString &k : {u"id"_s, u"name"_s, u"family"_s, u"image"_s, u"groups"_s, u"kioskHide"_s, u"description"_s,
                             u"favorite"_s, u"noDiscount"_s, u"allergens"_s, u"kitchenName"_s, u"number"_s, u"barcodes"_s,
                             u"upc"_s, u"altupc"_s, u"cost"_s, u"parentUpc"_s, u"packSize"_s, u"isRetail"_s, u"isEvent"_s,
                             u"length"_s, u"ringGauge"_s, u"ringGauge2"_s, u"abv"_s, u"pourOz"_s, u"scanMaker"_s,
                             u"scanCategory"_s, u"scanUom"_s, u"scanUnits"_s, u"buttonColor"_s, u"takeoutPrice"_s,
                             u"periodPrices"_s, u"taxClass"_s, u"printer"_s, u"station"_s, u"section"_s, u"breakBefore"_s})
        c.insert(k, i.value(k));
    c.insert(u"price"_s, QString::number(i.value(u"priceValue"_s).toDouble(), 'f', 2));
    c.insert(u"onIt"_s, i.value(u"onIt"_s).toStringList().join(u", "_s));
    c.insert(u"available"_s, i.value(u"availableSet"_s));
    c.insert(u"prepMinutes"_s, i.value(u"prepMinutes"_s).toInt() ? QString::number(i.value(u"prepMinutes"_s).toInt()) : QString());
    return c;
}

QVariantMap itemNamed(const QVariantList &items, const QString &name)
{
    for (const QVariant &v : items)
        if (v.toMap().value(u"name"_s).toString() == name)
            return v.toMap();
    return {};
}

// The store: its shared data, the Pi's own screen (a session here), the server and three tablets.
struct Store {
    test::RecordingSink sink;
    std::unique_ptr<PosShared> shared;
    std::unique_ptr<net::LayoutHub> hub;
    std::unique_ptr<net::PosServer> server;
    std::unique_ptr<PosService> pi;
    std::vector<std::unique_ptr<net::RemoteSession>> tablets;

    explicit Store(app::PosData data)
    {
        shared = std::make_unique<PosShared>(std::move(data), &sink);
        auto l = test::loadTestLayout();
        REQUIRE(l);
        hub = std::make_unique<net::LayoutHub>(*l);
        server = std::make_unique<net::PosServer>(shared.get(), hub.get());
        REQUIRE(server->listen(QHostAddress::LocalHost, 0));
        pi = std::make_unique<PosService>(shared.get(), u"Pi"_s);
        REQUIRE(pi->loginWithPin(u"1234"_s));
    }

    void addTablets(int n)
    {
        for (int k = 0; k < n; ++k) {
            const QString name = u"Bar %1"_s.arg(k + 1);
            core::TerminalConfig t;
            t.name = name.toStdString();
            t.id = net::newDeviceId().toStdString();
            const QByteArray key = net::newDeviceKey();
            t.key = key.toBase64().toStdString();
            shared->settings.terminals.push_back(t);
            net::Credentials c;
            c.host = u"127.0.0.1"_s;
            c.port = server->port();
            c.terminalId = QString::fromStdString(t.id);
            c.terminalName = name;
            c.key = key;
            auto r = std::make_unique<net::RemoteSession>(name);
            r->setCredentials(c);
            r->connectTo(u"127.0.0.1"_s, server->port());
            REQUIRE(r->waitForWelcome(5000));
            for (const char *d = "1111"; *d; ++d)
                r->pinKey(QString(QChar(*d)));
            bool done = false;
            r->invoke(u"login"_s, {}, [&done](const QVariant &) { done = true; });
            REQUIRE(waitFor([&] { return done && r->loggedIn(); }));
            r->want(u"menuItems"_s, true);   // the order screen's menu grid
            tablets.push_back(std::move(r));
        }
        settle();
    }

    // Until the server's rounds are done (none for a while).
    void settle()
    {
        int rounds = -1;
        waitFor([&] {
            const bool quiet = server->updateRounds() == rounds;
            rounds = server->updateRounds();
            return quiet;
        }, 3000);
        QTest::qWait(250);
        QCoreApplication::processEvents();
    }

    // Every piece of state the Pi's screen reads again after a change (the tablets' too), one read each: the
    // slowest, and all of them together.
    QString piReads() const
    {
        std::vector<std::pair<qint64, QString>> times;
        qint64 all = 0;
        const QMetaObject &mo = app::PosSession::staticMetaObject;
        for (const QString &key : app::PosSession::stateKeys()) {
            const QMetaProperty p = mo.property(mo.indexOfProperty(key.toLatin1().constData()));
            QElapsedTimer t;
            t.start();
            const QVariant v = p.read(pi.get());
            const qint64 ns = t.nsecsElapsed();
            all += ns;
            times.emplace_back(ns, key);
        }
        std::ranges::sort(times, std::greater<>());
        QStringList top;
        for (std::size_t i = 0; i < times.size() && i < 8; ++i)
            top << u"%1 %2"_s.arg(times[i].second).arg(ms(times[i].first), 0, 'f', 1);
        return u"all %1 ms; slowest: %2"_s.arg(ms(all), 0, 'f', 1).arg(top.join(u", "_s));
    }
};

// One save as the card does it, timed: the save (its parts), the Pi's screen reading everything again, the
// server's round to the tablets.
qint64 timedSave(Store &s, const QString &name, const QString &label, int round)
{
    const QVariantMap item = itemNamed(s.pi->builderItems(), name);
    REQUIRE_FALSE(item.isEmpty());
    QVariantMap card = cardOf(item);
    const QString price = QString::number(item.value(u"priceValue"_s).toDouble() + 0.25, 'f', 2);
    card.insert(u"price"_s, price);
    const qint64 nsBefore = s.server->updateNsecs();
    const int roundsBefore = s.server->updateRounds();
    QElapsedTimer t;
    t.start();
    REQUIRE(s.pi->saveMenuItemCard(card));
    const qint64 save = t.nsecsElapsed();
    t.restart();
    const QString reads = s.piReads();
    const qint64 readNs = t.nsecsElapsed();
    // The tablets get the new price.
    const bool reached = waitFor([&] {
        for (const auto &tab : s.tablets)   // (a Retail-only department isn't sent to them)
            if (const QVariantMap i = itemNamed(tab->menuItems(), name);
                !i.isEmpty() && i.value(u"priceValue"_s).toDouble() != price.toDouble())
                return false;
        return true;
    });
    CHECK(reached);
    s.settle();
    say(u"%1 save %2: %3 ms [%4]"_s.arg(label).arg(round).arg(ms(save), 0, 'f', 1).arg(app::SlowLog::last(u"save"_s)));
    say(u"%1 save %2: the Pi's screen reads again: %3 ms (%4)"_s.arg(label).arg(round).arg(ms(readNs), 0, 'f', 1).arg(reads));
    say(u"%1 save %2: server rounds to %3 tablets: %4 ms in %5 rounds [last: %6]"_s.arg(label).arg(round)
            .arg(s.tablets.size()).arg(ms(s.server->updateNsecs() - nsBefore), 0, 'f', 1)
            .arg(s.server->updateRounds() - roundsBefore).arg(app::SlowLog::last(u"round"_s)));
    return save + readNs + (s.server->updateNsecs() - nsBefore);
}

// A category moved up and back (Menu Builder's ▲ ▼: moveCategoryTo), timed as a save is.
void timedMoves(Store &s, const QString &categoryId, const QString &label)
{
    for (int round = 1; round <= 2; ++round) {
        const std::vector<core::MenuCategory> cats = s.shared->categories();
        const auto at = std::ranges::find(cats, categoryId.toStdString(), &core::MenuCategory::id);
        REQUIRE(at != cats.end());
        const int from = int(at - cats.begin());
        const int to = round == 1 ? std::max(0, from - 1) : from + 1;
        const qint64 nsBefore = s.server->updateNsecs();
        QElapsedTimer t;
        t.start();
        REQUIRE(s.pi->moveCategoryTo(categoryId, to));
        const qint64 move = t.nsecsElapsed();
        t.restart();
        const QString reads = s.piReads();
        const qint64 readNs = t.nsecsElapsed();
        s.settle();
        say(u"%1 move category %2: %3 ms [%4]; the Pi's screen reads again %5 ms; server rounds %6 ms [%7]"_s.arg(label)
                .arg(round).arg(ms(move), 0, 'f', 1).arg(app::SlowLog::last(u"move"_s)).arg(ms(readNs), 0, 'f', 1)
                .arg(ms(s.server->updateNsecs() - nsBefore), 0, 'f', 1).arg(app::SlowLog::last(u"round"_s)));
        Q_UNUSED(reads)
    }
}

app::PosData hcaData()
{
    app::PosData d = test::seedPosData();
    return d;
}

void fillBar(Store &s)
{
    const QVariantList rows = barRows();
    QElapsedTimer t;
    t.start();
    REQUIRE(s.pi->importMenuRows(rows, QString(), false) == rows.size());
    say(u"HCA-like bar: %1 items imported in %2 ms"_s.arg(rows.size()).arg(t.elapsed()));
    REQUIRE(s.pi->linkPours());
    REQUIRE(s.pi->fillDraftLines());   // the 18 beers on draft lines, with kegs
}

} // namespace

TEST_CASE("Speed: saving a beer's price on Menu Builder's card, HCA-like bar, 3 tablets", "[speed][savespeed]")
{
    Store s(hcaData());
    fillBar(s);
    s.addTablets(3);
    QString draftId;
    for (const core::MenuCategory &c : s.shared->categories())
        if (c.name == "Draft")
            draftId = QString::fromStdString(c.id);
    REQUIRE_FALSE(draftId.isEmpty());
    const QString beer = u"TEST DRAFT 07 16oz"_s;
    const QString beerId = itemNamed(s.pi->builderItems(), beer).value(u"id"_s).toString();
    REQUIRE_FALSE(beerId.isEmpty());
    s.pi->setBuilderView(draftId, QString(), beerId, QString(), {{u"scope"_s, u"hospitality"_s}, {u"path"_s, QStringList()}});
    s.settle();
    say(u"HCA-like: %1 items, %2 ingredients, %3 choice groups"_s.arg(s.shared->menu.size())
            .arg(s.shared->ingredients.size()).arg(s.shared->settings.modifierGroups.size()));
    qint64 worst = 0;
    for (int round = 1; round <= 3; ++round)
        worst = std::max(worst, timedSave(s, beer, u"HCA"_s, round));
    timedMoves(s, draftId, u"HCA"_s);
    CHECK(s.pi->findItem(beerId) != nullptr);
    CHECK(ms(worst) < 20000);   // (a ceiling far above what is expected: the times are in the log)
}

TEST_CASE("Speed: saving an item's price on Menu Builder's card, a 20,000-item price book", "[speed][savespeed]")
{
    app::PosData d = test::seedPosData();
    addPriceBook(d, 20000);
    Store s(std::move(d));
    s.addTablets(3);
    const QString name = u"TEST CIGAR 12345"_s;
    const QString id = u"test-cigar-12345"_s;
    s.pi->setBuilderView(u"test-cigars"_s, QString(), id, QString(),
                         {{u"scope"_s, u"retail"_s}, {u"path"_s, QStringList{u"BRAND 45"_s}}, {u"showItems"_s, false}});
    s.settle();
    qint64 worst = 0;
    for (int round = 1; round <= 3; ++round)
        worst = std::max(worst, timedSave(s, name, u"20k"_s, round));
    timedMoves(s, u"test-cigars"_s, u"20k"_s);
    CHECK(ms(worst) < 60000);
}

namespace {

// The Pi's own screen (Main.qml) on the store above: Menu Builder, the card open, Save.
struct PiScreen {
    LayoutController c;
    QQmlApplicationEngine engine;
    QQuickWindow *window = nullptr;
    QQuickItem *builder = nullptr;

    explicit PiScreen(Store &s)
        : c(*test::loadTestLayout())
    {
        c.setMealPeriod(u"lunch"_s);
        c.setPos(s.pi.get());
        engine.setInitialProperties({{u"controller"_s, QVariant::fromValue(&c)}, {u"width"_s, 1600}, {u"height"_s, 900},
                                     {u"touchKeyboard"_s, false}});
        test::loadViewTouchQml(engine, "Main");
        REQUIRE_FALSE(engine.rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        REQUIRE(window);
        REQUIRE(QTest::qWaitForWindowExposed(window));
        QTest::qWait(50);
    }

    static QQuickItem *findBy(QQuickItem *root, const char *property, const QString &value)
    {
        for (QQuickItem *item : root->childItems()) {
            if (!item->isVisible())
                continue;
            if (item->property(property).toString() == value)
                return item;
            if (QQuickItem *hit = findBy(item, property, value))
                return hit;
        }
        return nullptr;
    }
    QQuickItem *by(const QString &name) { return findBy(window->contentItem(), "objectName", name); }

    void open(const QString &scope, const QString &categoryId, const QString &itemId, const QString &search = {})
    {
        REQUIRE(c.jumpTo(u"menu-builder"_s));
        QTest::qWait(150);
        QQuickItem *w = by(u"builderScopeChooser"_s);
        while (w && !w->property("choosingScope").isValid())
            w = w->parentItem();
        REQUIRE(w);
        builder = w;
        w->setProperty("builderScope", scope);
        w->setProperty("choosingScope", false);
        w->setProperty("categoryId", categoryId);
        QTest::qWait(400);   // (the view told to the store, its list back)
        // Found by its name (as a person finds one down the list, or in a big department's tree).
        QQuickItem *tile = nullptr;
        {
            w->setProperty("search", search);
            waitFor([&] { return by(u"builderFound-"_s + itemId) != nullptr; });
            tile = by(u"builderFound-"_s + itemId);
        }
        REQUIRE(tile);
        const QPointF centre = tile->mapToScene(QPointF(tile->width() / 2, tile->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, {}, centre.toPoint());
        QTest::qWait(400);
        REQUIRE(by(u"builderPrice"_s));
    }

    // The event loop's busy time over the next `msec` (what the screen does after a touch).
    static qint64 busyFor(int msec)
    {
        qint64 busy = 0;
        QElapsedTimer all;
        all.start();
        while (all.elapsed() < msec) {
            QElapsedTimer b;
            b.start();
            QCoreApplication::sendPostedEvents();
            QCoreApplication::processEvents(QEventLoop::AllEvents);
            busy += b.nsecsElapsed();
            QThread::msleep(5);
        }
        return busy;
    }

    // Price changed, Save touched: the time until the screen is done (the save and everything after it).
    void save(Store &s, const QString &label, int round, const QString &itemId)
    {
        const double before = s.pi->findItem(itemId)->price.cents() / 100.0;
        const QString price = QString::number(before + 0.25, 'f', 2);
        QMetaObject::invokeMethod(builder, "set", Q_ARG(QVariant, QVariant(u"price"_s)), Q_ARG(QVariant, QVariant(price)));
        QTest::qWait(50);
        QQuickItem *saveButton = by(u"builderSave"_s);
        REQUIRE(saveButton);
        const QPointF centre = saveButton->mapToScene(QPointF(saveButton->width() / 2, saveButton->height() / 2));
        const qint64 nsBefore = s.server->updateNsecs();
        QElapsedTimer t;
        t.start();
        QTest::mouseClick(window, Qt::LeftButton, {}, centre.toPoint());
        const qint64 tap = t.nsecsElapsed();
        // Then what the event loop does for it (the list's delegates, the server's round), busy time only.
        const qint64 busy = busyFor(1500);
        CHECK(s.pi->findItem(itemId)->price.cents() == std::llround(price.toDouble() * 100));
        say(u"%1 UI save %2: the tap %3 ms [%4]; after it %5 ms busy (server rounds %6 ms) [%7]"_s.arg(label).arg(round)
                .arg(ms(tap), 0, 'f', 1).arg(app::SlowLog::last(u"save"_s)).arg(ms(busy), 0, 'f', 1)
                .arg(ms(s.server->updateNsecs() - nsBefore), 0, 'f', 1).arg(app::SlowLog::last(u"round"_s)));
    }
};

} // namespace

TEST_CASE("Speed: the Pi's screen saves a beer's price on Menu Builder's card, HCA-like bar, 3 tablets", "[speed][savespeed][ui]")
{
    Store s(hcaData());
    fillBar(s);
    s.addTablets(3);
    QString draftId;
    for (const core::MenuCategory &c : s.shared->categories())
        if (c.name == "Draft")
            draftId = QString::fromStdString(c.id);
    const QString beerId = itemNamed(s.pi->builderItems(), u"TEST DRAFT 07 16oz"_s).value(u"id"_s).toString();
    REQUIRE_FALSE(beerId.isEmpty());
    PiScreen screen(s);
    screen.open(u"hospitality"_s, draftId, beerId, u"TEST DRAFT 07 16oz"_s);
    for (int round = 1; round <= 3; ++round)
        screen.save(s, u"HCA"_s, round, beerId);
    // The category's ▲ and ▼ (under the category list, the card closed): each touch moves it at once.
    screen.builder->setProperty("search", QString());
    REQUIRE(QMetaObject::invokeMethod(screen.builder, "pickCategory", Q_ARG(QVariant, QVariant(draftId))));
    QTest::qWait(300);
    for (const QString &key : {u"builderCategoryUp"_s, u"builderCategoryDown"_s, u"builderCategoryUp"_s}) {
        const auto place = [&] {
            const std::vector<core::MenuCategory> cats = s.shared->categories();
            return int(std::ranges::find(cats, draftId.toStdString(), &core::MenuCategory::id) - cats.begin());
        };
        const int was = place();
        QQuickItem *button = screen.by(key);
        REQUIRE(button);
        REQUIRE(button->isEnabled());
        const QPointF centre = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
        QElapsedTimer t;
        t.start();
        QTest::mouseClick(screen.window, Qt::LeftButton, {}, centre.toPoint());
        const qint64 tap = t.nsecsElapsed();
        CHECK(place() != was);
        const qint64 busy = screen.busyFor(800);
        say(u"HCA UI %1: the tap %2 ms [%3]; after it %4 ms busy"_s.arg(key).arg(ms(tap), 0, 'f', 1)
                .arg(app::SlowLog::last(u"move"_s)).arg(ms(busy), 0, 'f', 1));
    }
}

TEST_CASE("Speed: the Pi's screen saves an item's price on Menu Builder's card, a 20,000-item price book", "[speed][savespeed][ui]")
{
    app::PosData d = test::seedPosData();
    addPriceBook(d, 20000);
    Store s(std::move(d));
    s.addTablets(3);
    PiScreen screen(s);
    // The department opens as a tree: the item found by its name.
    const QString id = u"test-cigar-12345"_s;
    screen.open(u"retail"_s, u"test-cigars"_s, id, u"TEST CIGAR 12345"_s);
    for (int round = 1; round <= 2; ++round)
        screen.save(s, u"20k"_s, round, id);
}
