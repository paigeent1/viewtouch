#include <algorithm>
#include <limits>
#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "layout_fixture.hh"
#include "layoutcontroller.hh"
#include "net/build_info.hh"
#include "net/layout_hub.hh"
#include "net/pos_server.hh"
#include "net/remote_session.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QThread>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
using vt::app::PosShared;

// M5: several terminals on one store.

namespace {

template <typename F>
bool waitFor(F condition, int msec = 3000)
{
    QElapsedTimer t;
    t.start();
    while (!condition() && t.elapsed() < msec) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

layout::Layout seedLayout()
{
    auto l = vt::test::loadTestLayout();
    REQUIRE(l);
    return *l;
}

// A server with its shared store, and a way to add remote terminals.
struct Store {
    test::RecordingSink sink;
    PosShared shared{test::seedPosData(), &sink};
    net::LayoutHub hub{seedLayout()};
    net::PosServer server{&shared, &hub};

    Store()
    {
        shared.setClock([] { return std::int64_t(1'700'000'000'000); });
        REQUIRE(server.listen(QHostAddress::LocalHost, 0));
    }

    // A device already paired under `name`.
    net::Credentials pairedDevice(const QString &name)
    {
        core::TerminalConfig t;
        t.name = name.toStdString();
        t.id = net::newDeviceId().toStdString();
        const QByteArray key = net::newDeviceKey();
        t.key = key.toBase64().toStdString();
        shared.settings.terminals.push_back(t);
        net::Credentials c;
        c.host = u"127.0.0.1"_s;
        c.port = server.port();
        c.terminalId = QString::fromStdString(t.id);
        c.terminalName = name;
        c.key = key;
        return c;
    }

    std::unique_ptr<net::RemoteSession> terminal(const QString &name)
    {
        auto t = std::make_unique<net::RemoteSession>(name);
        t->setCredentials(pairedDevice(name));
        t->connectTo(u"127.0.0.1"_s, server.port());
        REQUIRE(t->waitForWelcome(5000));
        return t;
    }
};

// A remote terminal with its page controller.
struct Screen {
    std::unique_ptr<net::RemoteSession> remote;
    LayoutController c;

    explicit Screen(std::unique_ptr<net::RemoteSession> r)
        : remote(std::move(r))
        , c(remote->layout())
    {
        c.setMealPeriod(u"lunch"_s);
        c.setPos(remote.get());
        c.setSaver([this](const layout::Layout &l, QString *) { remote->saveLayout(l); return true; });
        QObject::connect(remote.get(), &net::RemoteSession::layoutReceived, &c,
                         [this](const layout::Layout &l) { c.replaceLayout(l); });
    }

    void settle() { waitFor([&] { return !c.busy(); }); QCoreApplication::processEvents(); }
    void tap(const QString &zone)
    {
        c.activate(zone);
        settle();
    }
    void pin(const char *digits)
    {
        for (const char *d = digits; *d; ++d)
            remote->pinKey(QString(QChar(*d)));
        c.login();
        REQUIRE(waitFor([&] { return remote->loggedIn(); }));
        settle();
    }
};

} // namespace

TEST_CASE("Two terminals on one store: locks and live updates", "[net][shared]")
{
    PosShared shared(test::seedPosData(), nullptr);
    PosService front(&shared, u"Front"_s);
    PosService bar(&shared, u"Bar"_s);
    REQUIRE(front.loginWithPin(u"1111"_s));
    REQUIRE(bar.loginWithPin(u"2222"_s));

    QSignalSpy barSees(&bar, &PosService::openChecksChanged);
    REQUIRE(front.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(front.startCheck(core::CheckType::DineIn));
    front.addItem(u"coffee"_s);
    CHECK(barSees.size() >= 1);
    CHECK(bar.openChecks().size() == 1);
    CHECK(bar.tableStatus(u"T1"_s)[u"busyOn"_s].toString() == u"Front"_s);

    // The check is Front's while it is open there.
    QSignalSpy notices(&bar, &PosService::notice);
    CHECK(bar.selectTable(u"T1"_s) == PosService::TableFailed);
    CHECK(notices.last()[0].toString().contains(u"Front"_s));

    front.releaseCheck();
    CHECK(bar.selectTable(u"T1"_s) == PosService::TableOpened);
    CHECK(bar.lines().size() == 1);
    CHECK(front.tableStatus(u"T1"_s)[u"busyOn"_s].toString() == u"Bar"_s);

    // One sequence of check numbers across terminals.
    front.startCheck(core::CheckType::Takeout);
    CHECK(front.checkInfo()[u"id"_s].toLongLong() == 2);
}

TEST_CASE("A terminal going away frees its check", "[net][shared]")
{
    PosShared shared(test::seedPosData(), nullptr);
    PosService bar(&shared, u"Bar"_s);
    REQUIRE(bar.loginWithPin(u"2222"_s));
    qint64 id = 0;
    {
        PosService front(&shared, u"Front"_s);
        REQUIRE(front.loginWithPin(u"1111"_s));
        front.startCheck(core::CheckType::Takeout);
        id = front.checkInfo()[u"id"_s].toLongLong();
        CHECK_FALSE(bar.openCheck(id));
    }
    CHECK(bar.openCheck(id));
}

TEST_CASE("Deactivating someone logs them out everywhere", "[net][shared]")
{
    PosShared shared(test::seedPosData(), nullptr);
    PosService manager(&shared, u"Office"_s);
    PosService floor(&shared, u"Floor"_s);
    REQUIRE(manager.loginWithPin(u"1234"_s));
    REQUIRE(floor.loginWithPin(u"1111"_s));
    QSignalSpy loggedOut(&floor, &PosService::loggedInChanged);

    int sam = -1;
    for (int i = 0; i < int(shared.employees.size()); ++i)
        if (shared.employees[i].id == "sam") sam = i;
    REQUIRE(manager.adminDelete(u"employees"_s, sam));
    CHECK_FALSE(floor.loggedIn());
    REQUIRE(loggedOut.size() == 1);
    CHECK_FALSE(loggedOut[0][0].toBool());
}

TEST_CASE("Kitchen display: tickets, bump, recall", "[net][kitchen]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.addItem(u"classic-burger"_s);
    pos.addItem(u"medium"_s);
    pos.addItem(u"draft-beer"_s);
    CHECK(pos.kitchenTickets().isEmpty());          // nothing sent yet
    REQUIRE(pos.sendOrder());
    pos.addItem(u"coffee"_s);
    REQUIRE(pos.sendOrder());

    QVariantList tickets = pos.kitchenTickets();
    REQUIRE(tickets.size() == 2);                    // one per send
    const QVariantMap first = tickets[0].toMap();
    const QVariantList lines = first[u"lines"_s].toList();
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].toMap()[u"name"_s].toString() == u"Classic Burger"_s);
    CHECK(lines[0].toMap()[u"modifiers"_s].toStringList() == QStringList{u"M"_s});
    CHECK(lines[1].toMap()[u"printer"_s].toString() == u"bar"_s);

    REQUIRE(pos.bumpTicket(first[u"checkId"_s].toLongLong(), first[u"sentAt"_s].toLongLong()));
    CHECK(pos.kitchenTickets().size() == 1);
    CHECK_FALSE(pos.bumpTicket(first[u"checkId"_s].toLongLong(), first[u"sentAt"_s].toLongLong()));
    REQUIRE(pos.recallTicket());
    CHECK(pos.kitchenTickets().size() == 2);

    // Paying does not take tickets off the screen; bumping does.
    pos.entryKey(u"0"_s);
    REQUIRE(pos.openDrawerSession());
    REQUIRE(pos.tender(u"cash"_s, 100000));
    REQUIRE(pos.closeCheck());
    tickets = pos.kitchenTickets();
    CHECK(tickets.size() == 2);
    for (const QVariant &t : tickets)
        REQUIRE(pos.bumpTicket(t.toMap()[u"checkId"_s].toLongLong(), t.toMap()[u"sentAt"_s].toLongLong()));
    CHECK(pos.kitchenTickets().isEmpty());
    CHECK(sink.checks.begin()->second.lines[0].made);
}

TEST_CASE("Takeout and delivery carry customer details", "[net][customer]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));
    REQUIRE(pos.startCheck(core::CheckType::Delivery));
    CHECK(pos.checkInfo()[u"label"_s].toString().startsWith(u"Delivery"_s));
    REQUIRE(pos.setCustomer({{u"name"_s, u" Ana Ruiz "_s}, {u"phone"_s, u"555-0100"_s},
                             {u"address"_s, u"12 Oak St"_s}, {u"note"_s, u"Ring twice"_s}}));
    const QVariantMap customer = pos.checkInfo()[u"customer"_s].toMap();
    CHECK(customer[u"name"_s].toString() == u"Ana Ruiz"_s);   // trimmed
    CHECK(customer[u"address"_s].toString() == u"12 Oak St"_s);
    CHECK(pos.openChecks()[0].toMap()[u"customer"_s].toString() == u"Ana Ruiz"_s);

    pos.releaseCheck();
    CHECK_FALSE(pos.setCustomer({{u"name"_s, u"x"_s}}));
}

TEST_CASE("Remote terminal: full order flow through the server", "[net][remote]")
{
    Store store;
    Screen front(store.terminal(u"Front"_s));
    CHECK(store.server.terminalCount() == 1);
    CHECK(front.remote->terminalName() == u"Front"_s);
    CHECK(front.c.pageId() == u"login"_s);

    front.pin("1111");
    CHECK(front.remote->userName() == u"Sam"_s);
    CHECK(front.c.pageId() == u"tables"_s);

    front.c.selectTable(u"T2"_s);
    front.settle();
    CHECK(front.c.pageId() == u"guest-count"_s);
    front.remote->entryKey(u"3"_s);
    front.settle();
    front.tap(u"start"_s);                             // startCheck, then the menu
    CHECK(front.c.pageId() == u"index-lunch"_s);
    CHECK(front.remote->checkInfo()[u"guests"_s].toInt() == 3);

    REQUIRE(front.c.jumpTo(u"items-burgers"_s));      // a page of buttons placed by hand (a fixture)
    front.tap(u"item-1"_s);                            // Classic Burger -> its choices
    CHECK(front.c.pageId() == u"modifiers"_s);
    front.remote->chooseOption(u"temperature"_s, 1);   // Medium Rare
    front.settle();
    front.c.finishChoosing();
    front.settle();
    CHECK(front.c.pageId() == u"items-burgers"_s);
    REQUIRE(front.remote->lines().size() == 1);
    CHECK(front.remote->lines()[0].toMap()[u"modifiers"_s].toList().size() == 1);

    // The server holds the truth.
    REQUIRE(store.shared.open.size() == 1);
    CHECK(store.shared.open.begin()->second.label == "T2");

    front.tap(u"flow-send"_s);
    CHECK(front.remote->kitchenTickets().size() == 1);

    // A second terminal sees the table busy on Front.
    Screen bar(store.terminal(u"Bar"_s));
    bar.pin("2222");
    CHECK(bar.remote->tableStatus(u"T2"_s)[u"busyOn"_s].toString() == u"Front"_s);
    bar.c.selectTable(u"T2"_s);
    bar.settle();
    CHECK(bar.c.pageId() == u"tables"_s);
    CHECK(bar.c.statusText().contains(u"Front"_s));

    // Pay and close on Front.
    front.remote->entryKey(u"0"_s);
    front.settle();
    front.remote->openDrawerSession();
    front.settle();
    front.tap(u"flow-pay"_s);
    CHECK(front.c.pageId() == u"settle"_s);
    front.tap(u"tender-credit"_s);
    front.tap(u"close"_s);
    REQUIRE(waitFor([&] { return front.c.pageId() == u"tables"_s; }));
    CHECK(store.shared.open.empty());
    CHECK(store.shared.closedToday.size() == 1);
    CHECK(waitFor([&] { return bar.remote->openChecks().isEmpty(); }));
}

TEST_CASE("Remote terminal: reports and admin answers arrive asynchronously", "[net][remote]")
{
    Store store;
    Screen office(store.terminal(u"Office"_s));
    office.pin("1234");

    QSignalSpy answered(office.remote.get(), &app::PosSession::queriesChanged);
    const QVariantMap first = office.remote->report(u"sales"_s);
    CHECK(first.isEmpty());                            // not here yet
    REQUIRE(waitFor([&] { return !office.remote->report(u"sales"_s).isEmpty(); }));
    CHECK(office.remote->report(u"sales"_s)[u"title"_s].toString() == u"Sales Summary"_s);

    REQUIRE(waitFor([&] { return !office.remote->adminRecords(u"menu"_s).isEmpty(); }));
    const int items = int(office.remote->adminRecords(u"menu"_s).size());
    QVariantMap nachos = {{u"name"_s, u"Nachos"_s}, {u"price"_s, 7.5}, {u"taxClass"_s, u"food"_s}};
    office.remote->adminSave(u"menu"_s, -1, nachos);
    REQUIRE(waitFor([&] { return office.remote->adminRecords(u"menu"_s).size() == items + 1; }));
    CHECK(store.shared.menu.back().name == "Nachos");
}

TEST_CASE("Page edits on one terminal reach the others", "[net][remote]")
{
    Store store;
    Screen a(store.terminal(u"A"_s));
    Screen b(store.terminal(u"B"_s));
    a.pin("1234");
    QSignalSpy bGot(b.remote.get(), &net::RemoteSession::layoutReceived);

    a.c.enterEditMode();
    const QString page = a.c.pageId();
    a.c.editor()->addZone(u"button"_s);
    REQUIRE(a.c.saveEdits());
    REQUIRE(waitFor([&] { return bGot.size() == 1; }));
    CHECK(store.hub.layout().page(page)->zone(u"button"_s));
    CHECK(b.c.layout().page(page)->zone(u"button"_s));

    // Saving needs the layout.edit permission on the server too.
    b.pin("1111");
    QSignalSpy saved(b.remote.get(), &net::RemoteSession::layoutSaved);
    b.remote->saveLayout(b.c.layout());
    REQUIRE(waitFor([&] { return saved.size() == 1; }));
    CHECK_FALSE(saved[0][0].toBool());
}

TEST_CASE("Remote terminal reconnects and starts at the login page", "[net][remote]")
{
    auto store = std::make_unique<Store>();
    const quint16 port = store->server.port();
    Screen t(store->terminal(u"Front"_s));
    t.pin("1111");
    CHECK(t.c.pageId() == u"tables"_s);
    const auto pairedDevices = store->shared.settings.terminals;

    // Server goes away: the terminal says so and keeps trying.
    store.reset();
    REQUIRE(waitFor([&] { return !t.remote->online(); }));
    QSignalSpy notices(t.remote.get(), &app::PosSession::notice);
    t.c.selectTable(u"T1"_s);
    t.settle();
    CHECK_FALSE(notices.isEmpty());

    // The server restarts on the same port (its paired devices are saved):
    // back online, logged out.
    test::RecordingSink sink;
    PosShared shared(test::seedPosData(), &sink);
    shared.settings.terminals = pairedDevices;
    net::LayoutHub hub(seedLayout());
    net::PosServer server(&shared, &hub);
    REQUIRE(server.listen(QHostAddress::LocalHost, port));
    REQUIRE(waitFor([&] { return t.remote->online(); }, 8000));
    CHECK_FALSE(t.remote->loggedIn());
    CHECK(waitFor([&] { return t.c.pageId() == u"login"_s; }));
}

TEST_CASE("Kitchen display: a station bumps only its own lines", "[net][kitchen]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.addItem(u"cobb"_s);          // kitchen
    pos.addItem(u"house-wine"_s);    // bar
    REQUIRE(pos.sendOrder());
    const QVariantMap t = pos.kitchenTickets()[0].toMap();

    REQUIRE(pos.bumpTicket(t[u"checkId"_s].toLongLong(), t[u"sentAt"_s].toLongLong(), u"bar"_s));
    QVariantList left = pos.kitchenTickets();
    REQUIRE(left.size() == 1);
    REQUIRE(left[0].toMap()[u"lines"_s].toList().size() == 1);
    CHECK(left[0].toMap()[u"lines"_s].toList()[0].toMap()[u"name"_s].toString() == u"Cobb"_s);

    REQUIRE(pos.recallTicket());     // brings the wine back only
    CHECK(pos.kitchenTickets()[0].toMap()[u"lines"_s].toList().size() == 2);
}

TEST_CASE("An empty takeout that is put away is discarded", "[net][customer]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1111"_s));
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.releaseCheck();
    CHECK(pos.openChecks().isEmpty());
    CHECK(sink.checks.begin()->second.status == core::CheckStatus::Discarded);

    // A table keeps its (still empty) check.
    pos.selectTable(u"T1"_s);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.releaseCheck();
    CHECK(pos.openChecks().size() == 1);
}

namespace {

// Between a terminal and the store, like Wi-Fi: passes everything on until
// cut, then swallows everything both ways without closing anything (out of
// range: no socket ever hears the other side has gone).
struct WifiRelay {
    QTcpServer server;
    bool cut = false;
    std::vector<std::pair<QTcpSocket *, QTcpSocket *>> pairs;
    explicit WifiRelay(quint16 storePort)
    {
        REQUIRE(server.listen(QHostAddress::LocalHost));
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this, storePort] {
            while (QTcpSocket *in = server.nextPendingConnection()) {
                auto *out = new QTcpSocket(&server);
                out->connectToHost(QHostAddress::LocalHost, storePort);
                const auto pass = [this](QTcpSocket *from, QTcpSocket *to) {
                    QObject::connect(from, &QTcpSocket::readyRead, from, [this, from, to] {
                        const QByteArray bytes = from->readAll();
                        if (!cut)
                            to->write(bytes);
                    });
                };
                pass(in, out);
                pass(out, in);
                pairs.emplace_back(in, out);
            }
        });
    }
};

} // namespace

TEST_CASE("Wi-Fi gone without a word: the handheld says so; the store frees its check", "[net][remote][heartbeat]")
{
    Store store;
    store.server.setHeartbeat(200, 1500);
    WifiRelay wifi(store.server.port());
    auto remote = std::make_unique<net::RemoteSession>(u"Handheld"_s);
    net::Credentials creds = store.pairedDevice(u"Handheld"_s);
    creds.port = wifi.server.serverPort();
    remote->setCredentials(creds);
    remote->setWatchdog(1000, 100);
    remote->connectTo(u"127.0.0.1"_s, wifi.server.serverPort());
    REQUIRE(remote->waitForWelcome(5000));
    Screen h(std::move(remote));
    h.pin("1111");
    h.c.selectTable(u"T2"_s);
    h.settle();
    h.remote->entryKey(u"2"_s);
    h.settle();
    h.tap(u"start"_s);
    REQUIRE(waitFor([&] { return h.remote->hasCheck(); }));
    const qint64 id = h.remote->checkInfo()[u"id"_s].toLongLong();
    PosService bar(&store.shared, u"Bar"_s);
    REQUIRE(bar.loginWithPin(u"2222"_s));
    CHECK_FALSE(bar.openCheck(id));                     // open on the handheld
    waitFor([] { return false; }, 600);                 // pings answered: it's a terminal that pongs

    wifi.cut = true;                                    // out of range
    CHECK(waitFor([&] { return !h.remote->online(); }, 4000));    // the banner, not a hang
    QSignalSpy notices(h.remote.get(), &app::PosSession::notice);
    h.remote->invoke(u"releaseCheck"_s);                // a tap now: refused at once
    CHECK_FALSE(notices.isEmpty());
    CHECK(waitFor([&] { return bar.openCheck(id); }, 5000));     // the store let go of it
    wifi.cut = false;                                   // (it keeps trying: a new connection, not this one)
}

TEST_CASE("Updates: a screen behind the store is told, and a manager there gets the update", "[net][remote][updates]")
{
    Store store;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    store.server.setUpdatesDir(dir.path());
    const net::AppBuild me = net::appBuild();
    // An update for screens like this one, newer than this build; one for another kind; one unnamed.
    const QString arch = me.platform.section(u'-', 1);
    const QString mine = u"viewtouch-modern_0.7.0_%1-b%2.deb"_s.arg(arch == u"x86_64"_s ? u"amd64"_s : u"arm64"_s).arg(me.number + 5);
    QByteArray payload(3 * 1024 * 1024 + 123, '\0');   // more than one part
    for (int i = 0; i < payload.size(); ++i)
        payload[i] = char(i * 7);
    for (const auto &[name, bytes] : std::initializer_list<std::pair<QString, QByteArray>>{
             {mine, payload}, {u"ViewTouch-b%1-armeabi-v7a.apk"_s.arg(me.number + 9), "apk"}, {u"notes.txt"_s, "x"}}) {
        QFile f(dir.filePath(name));
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(bytes);
    }
    REQUIRE(me.platform.startsWith(u"linux-"_s));
    CHECK(store.server.updateFiles().size() == 2);
    CHECK(store.server.updateFor(me.platform, me.number).build == me.number + 5);
    CHECK(store.server.updateFor(me.platform, me.number + 5).path.isEmpty());   // nothing newer than that

    Screen h(store.terminal(u"Handheld"_s));
    // Same build as the store: not behind (the store's computer runs this build too).
    QVariantMap info = h.remote->updateInfo();
    CHECK(info[u"build"_s] == me.number);
    CHECK(info[u"storeBuild"_s] == me.number);
    CHECK_FALSE(info[u"behind"_s].toBool());
    CHECK(info[u"update"_s].toMap()[u"name"_s] == mine);   // newer than this screen: offered
    // The store's list of screens: the build, and that an update is ready.
    const QVariantList screens = store.server.connections();
    REQUIRE(screens.size() == 1);
    CHECK(screens[0].toMap()[u"build"_s] == me.number);
    CHECK(screens[0].toMap()[u"platform"_s] == me.platform);
    CHECK(screens[0].toMap()[u"hasUpdate"_s].toBool());
    // What its self-update is doing, as the screen says it, shows on the store's list (Network).
    CHECK(screens[0].toMap()[u"updateState"_s].toString().isEmpty());
    h.remote->reportUpdateState(u"update here; waiting for nobody signed in"_s);
    REQUIRE(waitFor([&] {
        return store.server.connections().value(0).toMap()[u"updateState"_s].toString()
            == u"update here; waiting for nobody signed in"_s;
    }));

    // A server can't fetch it; a manager can.
    h.pin("1111");
    QSignalSpy notices(h.remote.get(), &app::PosSession::notice);
    h.remote->getUpdate();
    REQUIRE(waitFor([&] { return h.remote->updateInfo().contains(u"error"_s); }));
    CHECK(h.remote->updateInfo()[u"error"_s].toString().contains(u"manager"_s));
    h.remote->invoke(u"logout"_s);
    REQUIRE(waitFor([&] { return !h.remote->loggedIn(); }));
    h.settle();
    h.pin("1234");
    h.remote->getUpdate();
    REQUIRE(waitFor([&] { return h.remote->updateInfo().contains(u"ready"_s); }, 10000));
    QFile got(h.remote->updateInfo()[u"ready"_s].toString());
    REQUIRE(got.open(QIODevice::ReadOnly));
    CHECK(QFileInfo(got).fileName() == mine);
    CHECK(got.readAll() == payload);
    got.remove();
}

TEST_CASE("Versions: MAJOR.MINOR and the build number, by every build; the recent builds built in", "[net][version]")
{
    const net::AppBuild me = net::appBuild();
    // 0.9.1283: the build number is the version's last part.
    CHECK(me.version.endsWith(u'.' + QString::number(me.number)));
    CHECK(me.version.count(u'.') == 2);
    CHECK(net::buildText(u"0.9.1283"_s, 1283, u"688654a"_s) == u"0.9.1283 (688654a)"_s);
    CHECK(net::buildText(u"0.9.1283"_s, 1283, u"688654a"_s, true) == u"0.9.1283 (688654a, modified)"_s);
    CHECK(net::buildText(u"0.7.0"_s, 812, u"a13bde2"_s) == u"0.7.0, build 812 (a13bde2)"_s);   // an older screen

    // The recent builds, newest first, each with its version and what changed.
    const QVariantList history = net::appHistory();
    REQUIRE(history.size() >= 2);
    int last = std::numeric_limits<int>::max();
    for (const QVariant &v : history) {
        const QVariantMap b = v.toMap();
        CHECK(b[u"number"_s].toInt() < last);
        last = b[u"number"_s].toInt();
        CHECK(b[u"version"_s].toString().endsWith(u'.' + b[u"number"_s].toString()));
        CHECK_FALSE(b[u"change"_s].toString().isEmpty());
        CHECK(b[u"at"_s].toLongLong() > 0);
    }
    CHECK(history.first().toMap()[u"number"_s].toInt() <= me.number);
}

TEST_CASE("Versions: the builds a store has run are kept with its settings", "[net][version]")
{
    core::PosSettings s;
    s.builds = {{"0.9.1283", 1283, "688654a", 1'760'000'000'000}, {"0.9.1284", 1284, "abc1234", 1'760'100'000'000}};
    const core::PosSettings back = app::settingsFromJson(app::toJson(s));
    CHECK(back.builds == s.builds);
}

TEST_CASE("Handhelds get the closed checks only while a list shows them", "[net][remote][want]")
{
    Store store;
    Screen office(store.terminal(u"Office"_s));
    office.pin("1234");
    // A check closed on the main computer's own session.
    PosService counter(&store.shared, u"Counter"_s);
    REQUIRE(counter.loginWithPin(u"1234"_s));
    counter.entryKey(u"10000"_s);
    REQUIRE(counter.openDrawerSession());
    const auto sell = [&] {
        REQUIRE(counter.startCheck(core::CheckType::Quick));
        counter.addItem(u"coffee"_s);
        REQUIRE(counter.tender(u"cash"_s));
        if (counter.hasCheck())
            REQUIRE(counter.closeCheck());
    };
    sell();
    waitFor([] { return false; }, 300);
    CHECK(office.remote->closedChecks().isEmpty());          // not asked for

    office.remote->want(u"closedChecks"_s, true);            // the Closed Checks list is shown
    REQUIRE(waitFor([&] { return office.remote->closedChecks().size() == 1; }));
    sell();                                                   // and kept up to date
    REQUIRE(waitFor([&] { return office.remote->closedChecks().size() == 2; }));

    office.remote->want(u"closedChecks"_s, false);           // gone from the screen
    CHECK(office.remote->closedChecks().isEmpty());
    sell();
    waitFor([] { return false; }, 300);
    CHECK(office.remote->closedChecks().isEmpty());
}

TEST_CASE("Tablets get the item list only while a screen lists it; scans ask the main computer", "[net][remote][want]")
{
    Store store;
    store.shared.menu.front().barcodes = {"016165009116"};   // a retail UPC-A
    Screen office(store.terminal(u"Office"_s));
    office.pin("1234");
    waitFor([] { return false; }, 300);
    CHECK(office.remote->menuItems().isEmpty());            // not asked for: a price book stays on the Pi

    // The 12 digits a scanner sends, the 11 of a UPC-A without its check digit, and the item's number.
    for (const QString &code : {u"016165009116"_s, u"01616500911"_s, u"101"_s}) {
        QVariant found;
        office.remote->invoke(u"itemForCode"_s, {code}, [&](const QVariant &v) { found = v; });
        REQUIRE(waitFor([&] { return found.isValid(); }));
        CHECK(found.toMap().value(u"id"_s).toString() == u"classic-burger"_s);
    }
    QVariant none;
    office.remote->invoke(u"itemForCode"_s, {u"999999999999"_s}, [&](const QVariant &v) { none = v; });
    REQUIRE(waitFor([&] { return none.isValid(); }));
    CHECK(none.toMap().isEmpty());
    QVariant numberOnly;
    office.remote->invoke(u"itemForCode"_s, {u"016165009116"_s, true}, [&](const QVariant &v) { numberOnly = v; });
    REQUIRE(waitFor([&] { return numberOnly.isValid(); }));
    CHECK(numberOnly.toMap().isEmpty());                    // a typed number is only a number

    office.remote->want(u"menuItems"_s, true);              // Menu Builder is shown
    REQUIRE(waitFor([&] { return !office.remote->menuItems().isEmpty(); }));
    office.remote->want(u"menuItems"_s, false);
    CHECK(office.remote->menuItems().isEmpty());
}

TEST_CASE("Handhelds put the open checks together from the shared part and their own", "[net][remote][want]")
{
    Store store;
    Screen front(store.terminal(u"Front"_s));
    Screen bar(store.terminal(u"Bar"_s));
    front.pin("1111");
    bar.pin("2222");
    front.c.selectTable(u"T2"_s);
    front.settle();
    front.remote->entryKey(u"3"_s);
    front.settle();
    front.tap(u"start"_s);                                   // Sam's check, open on Front
    REQUIRE(waitFor([&] { return bar.remote->openChecks().size() == 1; }));
    const QVariantMap mineOnFront = front.remote->openChecks().first().toMap();
    const QVariantMap seenAtBar = bar.remote->openChecks().first().toMap();
    CHECK(mineOnFront[u"mine"_s].toBool());
    CHECK(mineOnFront[u"current"_s].toBool());
    CHECK(mineOnFront[u"forMe"_s].toBool());
    CHECK(mineOnFront[u"label"_s] == u"T2"_s);
    CHECK_FALSE(seenAtBar[u"mine"_s].toBool());
    CHECK_FALSE(seenAtBar[u"current"_s].toBool());
    CHECK(seenAtBar[u"busyOn"_s] == u"Front"_s);
    // The same as the store works out for that person on that screen.
    PosService casey(&store.shared, u"Bar"_s);
    REQUIRE(casey.loginWithPin(u"2222"_s));
    const QVariantMap store_ = casey.openChecks().first().toMap();
    for (const QString &k : store_.keys())
        if (k != u"minutes"_s)
            CHECK(seenAtBar.value(k) == store_.value(k));
}

TEST_CASE("The screens' updates as JSON text: the same as Qt's, written straight", "[net][json]")
{
    const auto same = [](const QVariant &v) {
        const QByteArray mine = net::jsonText(v);
        const QJsonValue want = QJsonValue::fromVariant(v);
        // Read back, it's the very value Qt would have sent.
        const QJsonDocument doc = QJsonDocument::fromJson("[" + mine + "]");
        INFO(mine.toStdString());
        REQUIRE(doc.isArray());
        CHECK(doc.array().at(0) == want);
    };
    same(QVariant());
    same(true);
    same(false);
    same(0);
    same(-42);
    same(qint64(9'007'199'254'740'993));   // past a double's whole numbers
    same(3.5);
    same(2.0);
    same(0.1 + 0.2);
    same(-1e-7);
    same(std::numeric_limits<double>::quiet_NaN());
    same(u""_s);
    same(u"Café \"Ana\" \\ tab\there\nnew line\r\x01\x1f / 日本 🍔"_s);
    same(QStringList{u"a"_s, u"b\"c"_s});
    same(QVariantList{});
    same(QVariantMap{});
    same(QVariantList{1, u"x"_s, QVariantMap{{u"z"_s, 1}, {u"a"_s, QVariantList{true, QVariant()}}}, 2.25});
    same(QVariantMap{{u"total"_s, u"$12.50"_s}, {u"guests"_s, 4}, {u"lines"_s, QVariantList{QVariantMap{{u"name"_s, u"Burger"_s}}}}});
    // Anything unusual goes Qt's way, the same.
    same(QVariantMap{{u"at"_s, QDateTime::fromMSecsSinceEpoch(1'700'000'000'000, QTimeZone::UTC)}});
    same(QVariantHash{{u"b"_s, 1}, {u"a"_s, 2}});
}

TEST_CASE("Kitchen and expo screens skip finished closed checks, and see recalled ones again", "[net][kitchen]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    PosShared &shared = *pos.shared();
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.addItem(u"classic-burger"_s);
    pos.addItem(u"medium"_s);
    REQUIRE(pos.sendOrder());
    const QVariantMap ticket = pos.kitchenTicketsNow().constFirst().toMap();
    const qint64 id = ticket[u"checkId"_s].toLongLong(), sentAt = ticket[u"sentAt"_s].toLongLong();
    // Paid before it's made (a counter): still on both screens.
    pos.entryKey(u"0"_s);
    REQUIRE(pos.openDrawerSession());
    REQUIRE(pos.tender(u"cash"_s, 100000));
    REQUIRE(pos.closeCheck());
    CHECK(pos.kitchenTicketsNow().size() == 1);
    CHECK(pos.expoTicketsNow().size() == 1);
    CHECK_FALSE(shared.kitchenDone.contains(id));
    // Made: off the kitchen's screen, and remembered as done there.
    REQUIRE(pos.bumpTicket(id, sentAt));
    CHECK(pos.kitchenTicketsNow().isEmpty());
    CHECK(shared.kitchenDone.contains(id));
    CHECK(pos.expoTicketsNow().size() == 1);   // not run to the table yet
    // Recalled: back on the kitchen's screen.
    REQUIRE(pos.recallTicket());
    CHECK_FALSE(shared.kitchenDone.contains(id));
    CHECK(pos.kitchenTicketsNow().size() == 1);
    // A change to the menu or settings looks at every closed check afresh.
    REQUIRE(pos.bumpTicket(id, sentAt));
    CHECK(pos.kitchenTicketsNow().isEmpty());
    emit shared.adminChanged();
    CHECK(shared.kitchenDone.empty());
    CHECK(pos.kitchenTicketsNow().isEmpty());
}

TEST_CASE("Quick Label from a tablet: the main computer prints it, and the tablet sees how it went", "[net][remote][labels][quick]")
{
    // The label utility's stand-in on the main computer (see test_quick_label.cpp), and a label template.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QByteArray oldHome = qgetenv("HOME");
    const QByteArray oldLauncher = qgetenv("VT_LABEL_LAUNCHER");
    qputenv("HOME", dir.path().toUtf8());
    QDir().mkpath(dir.path() + u"/.local/share/ViewTouch"_s);
    {
        QFile t(dir.path() + u"/.local/share/ViewTouch/label_templates.json"_s);
        REQUIRE(t.open(QIODevice::WriteOnly));
        t.write(R"({"2X1 VINYL": {"width_in": 2.0, "height_in": 1.0, "elements": []}})");
        QFile f(dir.path() + u"/fake"_s);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QString::fromUtf8(
                    "#!/bin/sh\n"
                    "case \"$1\" in\n"
                    "  --quick-rolls) printf '%s' '{\"rolls\":[{\"key\":\"2X1 VINYL\",\"name\":\"2 x 1 in\",\"size\":\"2 x 1 in\",\"printer\":\"Zebra Large Labels\"}],\"last\":\"2X1 VINYL\"}' > \"$3\" ;;\n"
                    "  --quick-job) cp \"$2\" '%1/print-job.json'\n"
                    "    printf '%s' '{\"ok\":true,\"message\":\"Printed\",\"printer\":\"Zebra Large Labels\",\"copies\":3}' > \"$4\" ;;\n"
                    "esac\n").arg(dir.path()).toUtf8());
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    qputenv("VT_LABEL_LAUNCHER", (dir.path() + u"/fake"_s).toUtf8());

    {
        Store store;
        Screen stockRoom(store.terminal(u"Stock Room"_s));
        stockRoom.pin("1111");                                      // a server
        stockRoom.remote->quickLabelRolls();
        waitFor([] { return false; }, 300);
        CHECK(stockRoom.remote->quickLabel().isEmpty());            // not asked for: not sent
        stockRoom.remote->want(u"quickLabel"_s, true);              // the Quick Label sheet is up
        REQUIRE(waitFor([&] { return stockRoom.remote->quickLabel()[u"rolls"_s].toList().size() == 1; }));
        CHECK(stockRoom.remote->quickLabel()[u"roll"_s].toString() == u"2X1 VINYL"_s);

        stockRoom.remote->printQuickLabel({u"cosmic"_s, QString(), QString()}, u"2X1 VINYL"_s, 3);
        REQUIRE(waitFor([&] { return stockRoom.remote->quickLabel()[u"status"_s].toString() == u"Printed 3 on Zebra Large Labels"_s; }));
        CHECK(stockRoom.remote->quickLabel()[u"statusOk"_s].toBool());
        QFile job(dir.path() + u"/print-job.json"_s);
        REQUIRE(job.open(QIODevice::ReadOnly));
        const QJsonObject o = QJsonDocument::fromJson(job.readAll()).object();
        CHECK(o.value(u"lines").toArray().first().toString() == u"cosmic"_s);
        CHECK(o.value(u"copies").toInt() == 3);
        stockRoom.remote->want(u"quickLabel"_s, false);
    }
    qputenv("HOME", oldHome);
    if (oldLauncher.isEmpty())
        qunsetenv("VT_LABEL_LAUNCHER");
    else
        qputenv("VT_LABEL_LAUNCHER", oldLauncher);
}

TEST_CASE("TV buttons: vt-directv's argument rules", "[tv][directv]")
{
    CHECK(PosService::directvArgsOk(u"bar3"_s, u"key"_s, u"poweron"_s));
    CHECK(PosService::directvArgsOk(u"all"_s, u"tune"_s, u"206"_s));
    CHECK(PosService::directvArgsOk(u"BAR_3"_s, u"tune"_s, u"206"_s));
    CHECK_FALSE(PosService::directvArgsOk(u"bar3"_s, u"scan"_s, u"206"_s));           // tune or key only
    CHECK_FALSE(PosService::directvArgsOk(u"bar3"_s, u"pair"_s, u"206"_s));
    CHECK_FALSE(PosService::directvArgsOk(QString(), u"tune"_s, u"206"_s));
    CHECK_FALSE(PosService::directvArgsOk(u"bar3"_s, u"tune"_s, QString()));
    CHECK_FALSE(PosService::directvArgsOk(u"bar3; rm -rf ~"_s, u"tune"_s, u"206"_s));  // never a shell
    CHECK_FALSE(PosService::directvArgsOk(u"bar3"_s, u"tune"_s, u"206 && reboot"_s));
    CHECK_FALSE(PosService::directvArgsOk(u"-v"_s, u"tune"_s, u"206"_s));
    CHECK_FALSE(PosService::directvArgsOk(u"bar3"_s, u"key"_s, u"x"_s.repeated(25)));
    CHECK(PosService::directvArgsOk(u"bar3"_s, u"key"_s, u"x"_s.repeated(24)));
}

TEST_CASE("TV buttons on a tablet: the main computer works the TV, the answer shows on that tablet", "[net][remote][tv][directv]")
{
    // vt-directv's stand-in on the main computer: keeps what it was given, answers as vt-directv does.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString calls = dir.path() + u"/calls.txt"_s;
    {
        QFile f(dir.path() + u"/fake"_s);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QString::fromUtf8("#!/bin/sh\n"
                                  "printf '%s|%s|%s\\n' \"$1\" \"$2\" \"$3\" >> '%1'\n"
                                  "echo \"BAR 3: $3 sent\"\n").arg(calls).toUtf8());
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    const QByteArray oldLauncher = qgetenv("VT_DIRECTV_LAUNCHER");
    qputenv("VT_DIRECTV_LAUNCHER", (dir.path() + u"/fake"_s).toUtf8());
    const auto called = [&] {
        QFile f(calls);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
    };
    {
        Store store;
        Screen bar(store.terminal(u"Bar Tablet"_s));
        Screen floor(store.terminal(u"Floor Tablet"_s));

        // Nobody signed in: refused, nothing run.
        bar.c.command(u"directv"_s, {{u"box"_s, u"bar3"_s}, {u"op"_s, u"key"_s}, {u"val"_s, u"poweron"_s}});
        REQUIRE(waitFor([&] { return bar.c.statusText() == u"Log in first."_s; }));

        bar.pin("1111");   // a server: anyone signed in may use the TVs
        floor.pin("2222");
        // A button set up wrong (or a page made by hand with something else in it): refused, nothing run.
        bar.c.command(u"directv"_s, {{u"box"_s, u"bar3;reboot"_s}, {u"op"_s, u"key"_s}, {u"val"_s, u"poweron"_s}});
        REQUIRE(waitFor([&] { return bar.c.statusText().contains(u"isn't set up right"_s); }));
        bar.c.command(u"directv"_s, {{u"box"_s, u"bar3"_s}, {u"op"_s, u"scan"_s}, {u"val"_s, u"x"_s}});
        REQUIRE(waitFor([&] { return bar.c.statusText().contains(u"isn't set up right"_s); }));
        CHECK(called().isEmpty());

        // TV on, BAR 3: run on the main computer with exactly those arguments; the answer comes back
        // to this tablet's status line (after it was answered, on its own), not to the other tablet's.
        const QString floorWas = floor.c.statusText();
        bar.c.command(u"directv"_s, {{u"box"_s, u"bar3"_s}, {u"op"_s, u"key"_s}, {u"val"_s, u"poweron"_s}});
        CHECK(bar.c.statusText() == u"Sending to the TV..."_s);
        REQUIRE(waitFor([&] { return bar.c.statusText() == u"BAR 3: poweron sent"_s; }, 5000));
        CHECK(called() == u"bar3|key|poweron"_s);
        CHECK(floor.c.statusText() == floorWas);

        // A channel, with the button's defaults (bar1, tune 206) for what it leaves out.
        bar.c.command(u"directv"_s, {{u"val"_s, u"219"_s}});
        REQUIRE(waitFor([&] { return bar.c.statusText() == u"BAR 3: 219 sent"_s; }, 5000));
        CHECK(called().endsWith(u"bar1|tune|219"_s));

        // No vt-directv on the main computer: said so on the tablet.
        qputenv("VT_DIRECTV_LAUNCHER", (dir.path() + u"/missing"_s).toUtf8());
        bar.c.command(u"directv"_s, {{u"box"_s, u"bar3"_s}, {u"op"_s, u"key"_s}, {u"val"_s, u"poweroff"_s}});
        REQUIRE(waitFor([&] { return bar.c.statusText().contains(u"isn't set up on the main computer"_s); }));
    }
    {
        // The Pi's own screen: the same path, run right there.
        qputenv("VT_DIRECTV_LAUNCHER", (dir.path() + u"/fake"_s).toUtf8());
        PosShared shared(test::seedPosData(), nullptr);
        PosService pi(&shared, u"Bar"_s);
        LayoutController c(seedLayout());
        c.setPos(&pi);
        c.command(u"directv"_s, {{u"box"_s, u"bar3"_s}, {u"op"_s, u"tune"_s}, {u"val"_s, u"206"_s}});
        CHECK(c.statusText() == u"Log in first."_s);
        REQUIRE(pi.loginWithPin(u"1111"_s));
        c.command(u"directv"_s, {{u"box"_s, u"bar3"_s}, {u"op"_s, u"tune"_s}, {u"val"_s, u"206"_s}});
        REQUIRE(waitFor([&] { return c.statusText() == u"BAR 3: 206 sent"_s; }, 5000));
        CHECK(called().endsWith(u"bar3|tune|206"_s));
    }
    if (oldLauncher.isEmpty())
        qunsetenv("VT_DIRECTV_LAUNCHER");
    else
        qputenv("VT_DIRECTV_LAUNCHER", oldLauncher);
}

TEST_CASE("TV buttons: the button's own TV address and type are checked", "[tv][directv]")
{
    // A store network address, dotted: 10/8, 172.16/12, 192.168/16.
    for (const char *ok : {"192.168.6.77", "10.0.0.1", "172.16.0.1", "172.31.255.254", "192.168.0.0"})
        CHECK(PosService::directvPrivateIp(QString::fromLatin1(ok)));
    for (const char *bad : {"8.8.8.8", "172.32.0.1", "172.15.9.9", "192.169.1.1", "192.168.6", "192.168.6.77.1",
                            "192.168.6.256", "192.168.06.77", "192.168.6.77 ", " 192.168.6.77", "192.168.6.77;reboot",
                            "-v", "garbage", "", "::1", "0x0a.0.0.1"})
        CHECK_FALSE(PosService::directvPrivateIp(QString::fromLatin1(bad)));
    CHECK(PosService::directvTvProblem(u"samsung"_s, QString(), QString()).isEmpty());   // as before: the TV list
    CHECK(PosService::directvTvProblem(u"samsung"_s, u"192.168.6.77"_s, u"samsung"_s).isEmpty());
    CHECK(PosService::directvTvProblem(u"bar1"_s, u"192.168.6.74"_s, QString()).isEmpty());
    CHECK(PosService::directvTvProblem(u"bar1"_s, QString(), u"directv"_s).isEmpty());
    CHECK(PosService::directvTvProblem(u"r"_s, u"10.1.2.3"_s, u"roku"_s).isEmpty());
    CHECK(PosService::directvTvProblem(u"samsung"_s, u"8.8.8.8"_s, u"samsung"_s).contains(u"store network address"_s));
    CHECK(PosService::directvTvProblem(u"samsung"_s, u"garbage"_s, QString()).contains(u"store network address"_s));
    CHECK(PosService::directvTvProblem(u"samsung"_s, u"192.168.6.77"_s, u"lg"_s).contains(u"TV type"_s));
    CHECK(PosService::directvTvProblem(u"samsung"_s, QString(), u"--ip"_s).contains(u"TV type"_s));
    CHECK(PosService::directvTvProblem(u"all"_s, u"192.168.6.77"_s, QString()).contains(u"every box"_s));
}

TEST_CASE("TV buttons with their own address: given to vt-directv on the Pi's screen and from a tablet", "[net][remote][tv][directv]")
{
    // vt-directv's stand-in: keeps every argument it was given, one call a line.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString calls = dir.path() + u"/calls.txt"_s;
    {
        QFile f(dir.path() + u"/fake"_s);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QString::fromUtf8("#!/bin/sh\n"
                                  "(IFS='|'; echo \"$*\") >> '%1'\n"
                                  "echo \"TV: sent\"\n").arg(calls).toUtf8());
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    const QByteArray oldLauncher = qgetenv("VT_DIRECTV_LAUNCHER");
    qputenv("VT_DIRECTV_LAUNCHER", (dir.path() + u"/fake"_s).toUtf8());
    const auto called = [&] {
        QFile f(calls);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
    };
    const auto lastCall = [&] { return called().section(u'\n', -1); };
    const QVariantMap samsung{{u"box"_s, u"samsung"_s}, {u"kind"_s, u"samsung"_s}, {u"ip"_s, u"192.168.6.77"_s},
                              {u"op"_s, u"key"_s}, {u"val"_s, u"volup"_s}};
    const auto with = [](QVariantMap m, const QString &key, const QVariant &v) {
        m.insert(key, v);
        return m;
    };
    {
        // The Pi's own screen.
        PosShared shared(test::seedPosData(), nullptr);
        PosService pi(&shared, u"Bar"_s);
        LayoutController c(seedLayout());
        c.setPos(&pi);
        REQUIRE(pi.loginWithPin(u"1111"_s));
        c.command(u"directv"_s, samsung);
        REQUIRE(waitFor([&] { return c.statusText() == u"TV: sent"_s; }, 5000));
        CHECK(lastCall() == u"--ip|192.168.6.77|--kind|samsung|samsung|key|volup"_s);
        // Only an address: only --ip.
        c.command(u"directv"_s, with(with(samsung, u"kind"_s, QString()), u"ip"_s, u" 10.0.0.5 "_s));
        REQUIRE(waitFor([&] { return lastCall() == u"--ip|10.0.0.5|samsung|key|volup"_s; }, 5000));
        // A button without them: exactly as before (the TV list has it).
        c.command(u"directv"_s, {{u"box"_s, u"bar3"_s}, {u"op"_s, u"key"_s}, {u"val"_s, u"poweron"_s}});
        REQUIRE(waitFor([&] { return lastCall() == u"bar3|key|poweron"_s; }, 5000));
        // Refused, said why, nothing run: a public address, garbage, a TV type it doesn't know, every box.
        const int before = called().count(u'\n');
        c.command(u"directv"_s, with(samsung, u"ip"_s, u"8.8.8.8"_s));
        CHECK(c.statusText().contains(u"store network address"_s));
        c.command(u"directv"_s, with(samsung, u"ip"_s, u"192.168.6.77; reboot"_s));
        CHECK(c.statusText().contains(u"store network address"_s));
        c.command(u"directv"_s, with(samsung, u"kind"_s, u"lg"_s));
        CHECK(c.statusText().contains(u"TV type"_s));
        c.command(u"directv"_s, with(samsung, u"box"_s, u"all"_s));
        CHECK(c.statusText().contains(u"every box"_s));
        waitFor([] { return false; }, 300);   // (anything started would have written by now)
        CHECK(called().count(u'\n') == before);

        // The number pad of a button with its own TV tunes that TV.
        c.command(u"directvKeypad"_s, {{u"box"_s, u"samsung"_s}, {u"kind"_s, u"samsung"_s}, {u"ip"_s, u"192.168.6.77"_s}});
        REQUIRE(c.tvPadOpen());
        CHECK(c.tvPadIp() == u"192.168.6.77"_s);
        CHECK(c.tvPadKind() == u"samsung"_s);
        REQUIRE(c.tvPadTune(u"206"_s));
        REQUIRE(waitFor([&] { return lastCall() == u"--ip|192.168.6.77|--kind|samsung|samsung|tune|206"_s; }, 5000));
        c.command(u"directvKeypad"_s, {{u"box"_s, u"bar1"_s}});
        REQUIRE(c.tvPadTune(u"219"_s));
        REQUIRE(waitFor([&] { return lastCall() == u"bar1|tune|219"_s; }, 5000));
    }
    {
        // A tablet: the main computer gets the address and type too.
        Store store;
        Screen bar(store.terminal(u"Bar Tablet"_s));
        bar.pin("1111");
        bar.c.command(u"directv"_s, samsung);
        REQUIRE(waitFor([&] { return lastCall() == u"--ip|192.168.6.77|--kind|samsung|samsung|key|volup"_s; }, 5000));
        REQUIRE(waitFor([&] { return bar.c.statusText() == u"TV: sent"_s; }, 5000));
        bar.c.command(u"directv"_s, {{u"box"_s, u"bar3"_s}, {u"op"_s, u"tune"_s}, {u"val"_s, u"206"_s}});
        REQUIRE(waitFor([&] { return lastCall() == u"bar3|tune|206"_s; }, 5000));
        const int before = called().count(u'\n');
        bar.c.command(u"directv"_s, with(samsung, u"ip"_s, u"1.1.1.1"_s));
        REQUIRE(waitFor([&] { return bar.c.statusText().contains(u"store network address"_s); }));
        bar.c.command(u"directv"_s, with(samsung, u"kind"_s, u"sony"_s));
        REQUIRE(waitFor([&] { return bar.c.statusText().contains(u"TV type"_s); }));
        // The pad on a tablet.
        bar.c.command(u"directvKeypad"_s, {{u"box"_s, u"samsung"_s}, {u"kind"_s, u"samsung"_s}, {u"ip"_s, u"192.168.6.77"_s}});
        REQUIRE(bar.c.tvPadOpen());
        REQUIRE(bar.c.tvPadTune(u"634"_s));
        REQUIRE(waitFor([&] { return lastCall() == u"--ip|192.168.6.77|--kind|samsung|samsung|tune|634"_s; }, 5000));
        CHECK(called().count(u'\n') == before + 1);
    }
    if (oldLauncher.isEmpty())
        qunsetenv("VT_DIRECTV_LAUNCHER");
    else
        qputenv("VT_DIRECTV_LAUNCHER", oldLauncher);
}

TEST_CASE("TV channel number pad: the DIRECTV pages' CH # key, tuned through the directv command", "[net][remote][tv][directv][tvpad]")
{
    // Every Tune page has the CH # key, for its own box, under BOX off, over nothing else.
    const layout::Layout pages = seedLayout();
    for (const QString &box : {u"all"_s, u"bar1"_s, u"bar2"_s, u"box72"_s, u"front"_s, u"vip"_s}) {
        INFO(box.toStdString());
        const layout::Page *page = pages.page(u"directv-tune-"_s + box);
        REQUIRE(page);
        const auto pad = std::ranges::find_if(page->zones, [](const layout::Zone &z) { return z.id == u"rk-chpad"; });
        REQUIRE(pad != page->zones.end());
        CHECK(pad->label == u"CH # (number pad)"_s);
        REQUIRE(pad->actions.size() == 1);
        CHECK(pad->actions.front().type() == u"command"_s);
        CHECK(pad->actions.front().str(u"name") == u"directvKeypad"_s);
        CHECK(pad->actions.front().data.value(u"args"_s).toObject().value(u"box"_s).toString() == box);
        for (const layout::Zone &z : page->zones)
            if (z.id != pad->id)
                CHECK_FALSE(z.rect.intersects(pad->rect));
    }

    // vt-directv's stand-in: keeps what it was given, answers as vt-directv does.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString calls = dir.path() + u"/calls.txt"_s;
    {
        QFile f(dir.path() + u"/fake"_s);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QString::fromUtf8("#!/bin/sh\n"
                                  "printf '%s|%s|%s\\n' \"$1\" \"$2\" \"$3\" >> '%1'\n"
                                  "echo \"TV: $3 sent\"\n").arg(calls).toUtf8());
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    const QByteArray oldLauncher = qgetenv("VT_DIRECTV_LAUNCHER");
    qputenv("VT_DIRECTV_LAUNCHER", (dir.path() + u"/fake"_s).toUtf8());
    const auto called = [&] {
        QFile f(calls);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
    };
    {
        // The Pi's own screen: the BAR 3 page's key opens the pad for box72.
        PosShared shared(test::seedPosData(), nullptr);
        PosService pi(&shared, u"Bar"_s);
        LayoutController c(seedLayout());
        c.setPos(&pi);
        c.command(u"directvKeypad"_s, {{u"box"_s, u"box72"_s}});
        CHECK_FALSE(c.tvPadOpen());                       // nobody logged in
        CHECK(c.statusText() == u"Log in first."_s);
        REQUIRE(pi.loginWithPin(u"1111"_s));              // a server: anyone signed in may use the TVs
        REQUIRE(c.jumpTo(u"directv-tune-box72"_s));
        c.activate(u"rk-chpad"_s);
        REQUIRE(c.tvPadOpen());
        CHECK(c.tvPadBox() == u"box72"_s);
        CHECK(c.tvPadTitle() == u"Channel for BAR 3"_s);  // the page's name, without "Tune" and the address

        // The channel's name as it's typed, when it's one of the pages' channels.
        CHECK(c.tvChannelName(u"206"_s) == u"ESPN"_s);
        CHECK(c.tvChannelName(u"634"_s) == u"MSG"_s);
        CHECK(c.tvChannelName(u"20"_s).isEmpty());
        CHECK(c.tvChannelName(u"9999"_s).isEmpty());

        // Nothing typed, too long, or not a number: refused, said so, the pad stays, nothing sent.
        CHECK_FALSE(c.tvPadTune(QString()));
        CHECK(c.statusText() == u"Type the channel number first."_s);
        CHECK_FALSE(c.tvPadTune(u"12345"_s));
        CHECK(c.statusText() == u"A channel is 1 to 4 digits."_s);
        CHECK_FALSE(c.tvPadTune(u"2a"_s));
        CHECK_FALSE(c.tvPadTune(u"20;6"_s));
        CHECK(c.tvPadOpen());
        CHECK(called().isEmpty());

        // Tune: directv tune with the digits typed, for the page's box; the pad closes, the answer shows.
        CHECK(c.tvPadTune(u"634"_s));
        CHECK_FALSE(c.tvPadOpen());
        REQUIRE(waitFor([&] { return c.statusText() == u"TV: 634 sent"_s; }, 5000));
        CHECK(called() == u"box72|tune|634"_s);
        CHECK_FALSE(c.tvPadTune(u"206"_s));               // closed: nothing more sent
        CHECK(called() == u"box72|tune|634"_s);

        // Close: nothing sent. The ALL page's pad is for every box.
        REQUIRE(c.jumpTo(u"directv-tune-all"_s));
        c.activate(u"rk-chpad"_s);
        REQUIRE(c.tvPadOpen());
        CHECK(c.tvPadBox() == u"all"_s);
        CHECK(c.tvPadTitle() == u"Channel for ALL 5 BOXES"_s);
        c.closeTvPad();
        CHECK_FALSE(c.tvPadOpen());
        CHECK(called() == u"box72|tune|634"_s);
        // A pad key given in the page editor (no box): the Tune page's own box.
        REQUIRE(c.jumpTo(u"directv-tune-vip"_s));
        c.command(u"directvKeypad"_s);
        REQUIRE(c.tvPadOpen());
        CHECK(c.tvPadBox() == u"vip"_s);
        CHECK(c.tvPadTitle() == u"Channel for VIP"_s);
        c.closeTvPad();
        CHECK(c.describeAction(layout::Action{QJsonObject{{u"type"_s, u"command"_s}, {u"name"_s, u"directvKeypad"_s}}})
                  .contains(u"channel number pad"_s));
    }
    {
        // A tablet: the same pad, tuned by the main computer; a button off the Tune pages names its box.
        Store store;
        Screen bar(store.terminal(u"Bar Tablet"_s));
        bar.pin("1111");
        bar.c.command(u"directvKeypad"_s, {{u"box"_s, u"bar1"_s}});
        REQUIRE(bar.c.tvPadOpen());
        CHECK(bar.c.tvPadTitle() == u"Channel for BAR1"_s);
        CHECK(bar.c.tvPadTune(u"703"_s));
        REQUIRE(waitFor([&] { return bar.c.statusText() == u"TV: 703 sent"_s; }, 5000));
        CHECK(called().endsWith(u"bar1|tune|703"_s));
    }
    if (oldLauncher.isEmpty())
        qunsetenv("VT_DIRECTV_LAUNCHER");
    else
        qputenv("VT_DIRECTV_LAUNCHER", oldLauncher);
}
