#include <catch2/catch_test_macros.hpp>

#include "billboard.hh"
#include "layout_fixture.hh"
#include "layoutcontroller.hh"
#include "net/layout_hub.hh"
#include "net/pos_server.hh"
#include "net/remote_session.hh"
#include "pos_fixture.hh"
#include "qml_load.hh"
#include "qt_catch.hh"
#include "app/pos_json.hh"

#include <functional>

#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
using vt::app::PosShared;

// Billboards: TVs around the store showing the guest-screen slides. The owner: signage only,
// never any customer or sale.

namespace {

template <typename F>
bool waitFor(F condition, int msec = 4000)
{
    QElapsedTimer t;
    t.start();
    while (!condition() && t.elapsed() < msec) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

struct Store {
    test::RecordingSink sink;
    PosShared shared{test::seedPosData(), &sink};
    net::LayoutHub hub{*test::loadTestLayout()};
    net::PosServer server{&shared, &hub};

    Store() { REQUIRE(server.listen(QHostAddress::LocalHost, 0)); }

    std::unique_ptr<net::RemoteSession> connect(const QString &name, const std::string &screen)
    {
        core::TerminalConfig t;
        t.name = name.toStdString();
        t.id = net::newDeviceId().toStdString();
        const QByteArray key = net::newDeviceKey();
        t.key = key.toBase64().toStdString();
        t.screen = screen;
        shared.settings.terminals.push_back(t);
        net::Credentials c;
        c.host = u"127.0.0.1"_s;
        c.port = server.port();
        c.terminalId = QString::fromStdString(t.id);
        c.terminalName = name;
        c.key = key;
        auto r = std::make_unique<net::RemoteSession>(name);
        r->setCredentials(c);
        r->connectTo(u"127.0.0.1"_s, server.port());
        REQUIRE(r->waitForWelcome(5000));
        return r;
    }
};

void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    REQUIRE(f.write(data) == data.size());
}

} // namespace

TEST_CASE("Billboard: only the slides, pictures and logo are ever sent to one", "[net][billboard]")
{
    // Nothing about checks, customers, sales or staff is on the list at all.
    for (const QString &key : app::PosSession::stateKeys()) {
        const bool safe = QStringList{u"terminalName"_s, u"storeName"_s, u"storeLanguage"_s, u"language"_s,
                                      u"screenMode"_s, u"adminRevision"_s, u"storeImages"_s, u"storeLogo"_s,
                                      u"billboard"_s}.contains(key);
        INFO(key.toStdString());
        CHECK(net::PosServer::sentToBillboard(key) == safe);
    }
    for (const char *key : {"check", "lines", "totals", "payments", "openChecks", "openChecksShared", "tableChecks",
                            "customers", "customer", "customerPrompt", "giftCard", "staff", "closedChecks", "day",
                            "dashboard", "menuItems", "kioskMenu", "selfOrder", "waitlist", "deliveries"})
        CHECK_FALSE(net::PosServer::sentToBillboard(QString::fromLatin1(key)));
}

TEST_CASE("Billboard: a billboard session never gets checks or customers", "[net][billboard]")
{
    Store s;
    s.shared.settings.displaySlides = {"Welcome to the store", "image:store:sale.jpg"};
    auto tv = s.connect(u"Front TV"_s, "billboard");
    CHECK(tv->screenMode() == u"billboard"_s);
    CHECK(tv->billboard()[u"slides"_s].toList().first().toString() == u"Welcome to the store"_s);
    CHECK(tv->billboard()[u"showing"_s].toBool());
    CHECK(tv->layout().pages.empty());   // no pages either

    // Meanwhile at the register: a check with a customer on it.
    PosService front(&s.shared, u"Front"_s);
    REQUIRE(front.loginWithPin(u"1111"_s));
    REQUIRE(front.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(front.startCheck(core::CheckType::DineIn));
    REQUIRE(front.addItem(u"coffee"_s));
    front.findCustomers(u"a"_s);
    REQUIRE(!s.shared.open.empty());
    QTest::qWait(400);   // several rounds of updates to every screen
    s.shared.settings.storeName = "Hat City";
    emit s.shared.adminChanged();
    REQUIRE(waitFor([&] { return tv->storeName() == u"Hat City"_s; }));

    CHECK_FALSE(tv->hasCheck());
    CHECK(tv->checkInfo().isEmpty());
    CHECK(tv->lines().isEmpty());
    CHECK(tv->openChecks().isEmpty());
    CHECK(tv->openChecksShared().isEmpty());
    CHECK(tv->tableChecks().isEmpty());
    CHECK(tv->customerResults().isEmpty());
    CHECK(tv->customerInfo().isEmpty());
    CHECK(tv->customerPrompt().isEmpty());
    CHECK(tv->staff().isEmpty());
    CHECK(tv->menuItems().isEmpty());
    CHECK(tv->dayInfo().isEmpty());
    CHECK(tv->kitchenTickets().isEmpty());

    // It may not do anything either: no order, no customer lookup, no other picture.
    const size_t open = s.shared.open.size();
    for (const auto &[method, args] : std::vector<std::pair<QString, QVariantList>>{
             {u"loginWithPin"_s, {u"1111"_s}}, {u"startCheck"_s, {0}}, {u"findCustomers"_s, {u"a"_s}},
             {u"storeImage"_s, {u"/etc/passwd"_s}}, {u"report"_s, {u"sales"_s, 0}}}) {
        QVariant answer = u"none"_s;
        tv->invoke(method, args, [&answer](const QVariant &r) { answer = r; });
        REQUIRE(waitFor([&] { return answer != QVariant(u"none"_s); }));
        CHECK(answer == QVariant(false));
    }
    CHECK_FALSE(tv->loggedIn());
    CHECK(s.shared.open.size() == open);
    CHECK(tv->customerResults().isEmpty());

    // A manager's PIN closes it; anyone else's doesn't.
    QVariant left;
    tv->invoke(u"leaveBillboard"_s, {u"1111"_s}, [&left](const QVariant &r) { left = r; });
    REQUIRE(waitFor([&] { return left.isValid(); }));
    CHECK(left == QVariant(false));
    left = {};
    tv->invoke(u"leaveBillboard"_s, {u"1234"_s}, [&left](const QVariant &r) { left = r; });
    REQUIRE(waitFor([&] { return left.isValid(); }));
    CHECK(left == QVariant(true));
    CHECK_FALSE(tv->loggedIn());
}

TEST_CASE("Billboard: made a terminal again, it reconnects with a full session", "[net][billboard]")
{
    Store s;
    auto tv = s.connect(u"Front TV"_s, "billboard");
    for (core::TerminalConfig &t : s.shared.settings.terminals)
        t.screen = "standard";
    emit s.shared.adminChanged();
    REQUIRE(waitFor([&] { return tv->isConnected() && tv->screenMode() == u"standard"_s; }, 8000));
    CHECK_FALSE(tv->layout().pages.empty());
}

TEST_CASE("Billboard: the video files it may get are in the guest-screen folder only", "[net][billboard][security]")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString folder = root.filePath(u"Guest Screen"_s);
    REQUIRE(QDir().mkpath(folder));
    writeFile(folder + u"/sale.mp4"_s, "video");
    writeFile(folder + u"/notes.txt"_s, "text");
    writeFile(folder + u"/.hidden.mp4"_s, "video");
    writeFile(root.filePath(u"secret.mp4"_s), "outside");
    REQUIRE(QDir().mkpath(folder + u"/sub"_s));
    writeFile(folder + u"/sub/inner.mp4"_s, "video");
    REQUIRE(QFile::link(root.filePath(u"secret.mp4"_s), folder + u"/link.mp4"_s));

    using net::PosServer;
    CHECK(PosServer::guestVideoFile(folder, u"sale.mp4"_s) == QFileInfo(folder + u"/sale.mp4"_s).canonicalFilePath());
    for (const QString &bad : {u"../secret.mp4"_s, u"..%2Fsecret.mp4"_s, u"/etc/passwd"_s, u"sub/inner.mp4"_s,
                               u"..\\secret.mp4"_s, u"link.mp4"_s, u"notes.txt"_s, u".hidden.mp4"_s,
                               u"missing.mp4"_s, u""_s, u".."_s, u"."_s, root.filePath(u"secret.mp4"_s)}) {
        INFO(bad.toStdString());
        CHECK(PosServer::guestVideoFile(folder, bad).isEmpty());
    }
    CHECK(PosServer::guestVideoFile(root.filePath(u"no such folder"_s), u"sale.mp4"_s).isEmpty());
}

TEST_CASE("Billboard: a video on the slides arrives a part at a time; one outside the folder never does", "[net][billboard]")
{
    QTemporaryDir root, cache;
    REQUIRE(root.isValid());
    const QString folder = root.filePath(u"Guest Screen"_s);
    REQUIRE(QDir().mkpath(folder));
    QByteArray video(1'300'000, Qt::Uninitialized);   // three parts
    for (char &c : video)
        c = char(QRandomGenerator::global()->bounded(256));
    writeFile(folder + u"/sale.mp4"_s, video);
    writeFile(root.filePath(u"outside.mp4"_s), "not for billboards");

    Store s;
    s.server.setGuestVideoDir(folder);
    const QString inside = folder + u"/sale.mp4"_s, outside = root.filePath(u"outside.mp4"_s);
    s.shared.settings.displaySlides = {("video:" + inside + "#sound").toStdString(), ("video:" + outside).toStdString()};
    auto tv = s.connect(u"Front TV"_s, "billboard");
    tv->setVideoCache(cache.path());

    CHECK(tv->billboardVideo(inside).isEmpty());   // on its way
    REQUIRE(waitFor([&] { return !tv->billboardVideo(inside).isEmpty(); }, 10000));
    QFile got(tv->billboardVideo(inside));
    REQUIRE(got.open(QIODevice::ReadOnly));
    CHECK(got.readAll() == video);

    tv->billboardVideo(outside);
    QTest::qWait(500);
    CHECK(tv->billboardVideo(outside).isEmpty());
    CHECK(QDir(cache.path()).entryList(QDir::Files).size() == 1);   // the one video, nothing else

    // A name that isn't on the slides, asked for straight: refused too.
    CHECK(net::PosServer::guestVideoFile(folder, u"sale.mp4"_s) == QFileInfo(inside).canonicalFilePath());
}

TEST_CASE("Billboard: its hours, mute and on/off, kept on the server and pushed to it", "[net][billboard]")
{
    using app::PosService;
    const std::vector<std::string> week = {"09:00-21:00", "09:00-21:00", "09:00-21:00", "09:00-21:00",
                                           "09:00-21:00", "09:00-21:00", "11:00-18:00"};
    CHECK(PosService::billboardOpenAt({}, 3, 3 * 60));                // none set: always on
    CHECK(PosService::billboardOpenAt(week, 1, 9 * 60));              // Monday 9:00
    CHECK_FALSE(PosService::billboardOpenAt(week, 1, 21 * 60));       // ... until 21:00
    CHECK_FALSE(PosService::billboardOpenAt(week, 7, 10 * 60));       // Sunday from 11:00
    CHECK(PosService::billboardOpenAt(week, 7, 11 * 60 + 30));
    std::vector<std::string> late(7, "closed");
    late[4] = "18:00-02:00";                                          // Friday into Saturday
    CHECK(PosService::billboardOpenAt(late, 5, 23 * 60));
    CHECK(PosService::billboardOpenAt(late, 6, 60));
    CHECK_FALSE(PosService::billboardOpenAt(late, 6, 3 * 60));
    CHECK_FALSE(PosService::billboardOpenAt(late, 1, 12 * 60));
    std::vector<std::string> allDay(7, "");
    allDay[6] = "closed";
    CHECK(PosService::billboardOpenAt(allDay, 6, 2));
    CHECK_FALSE(PosService::billboardOpenAt(allDay, 7, 12 * 60));
    CHECK(PosService::billboardDayHours(u"9:00-21:00"_s) == std::optional<std::string>("09:00-21:00"));
    CHECK(PosService::billboardDayHours(u" Closed "_s) == std::optional<std::string>("closed"));
    CHECK_FALSE(PosService::billboardDayHours(u"9 to 5"_s));

    Store s;
    // Monday 2026-11-02 at 22:00, the store's clock.
    s.shared.setClock([] { return QDateTime(QDate(2026, 11, 2), QTime(22, 0)).toMSecsSinceEpoch(); });
    auto tv = s.connect(u"Front TV"_s, "billboard");
    CHECK(tv->billboard()[u"showing"_s].toBool());
    CHECK_FALSE(tv->billboard()[u"mute"_s].toBool());

    PosService manager(&s.shared, u"Office"_s);
    REQUIRE(manager.loginWithPin(u"1234"_s));
    QVariantList hours;
    for (const std::string &h : week)
        hours << QString::fromStdString(h);
    REQUIRE(manager.setBillboard(u"Front TV"_s, {{u"hours"_s, hours}, {u"mute"_s, true}}));
    REQUIRE(waitFor([&] { return tv->billboard()[u"mute"_s].toBool(); }));
    CHECK_FALSE(tv->billboard()[u"showing"_s].toBool());   // dark: past 21:00
    // Store Settings > Billboards: the billboard defaults first, then each billboard.
    const QVariantList list = manager.adminRecords(u"billboards"_s);
    REQUIRE(list.size() == 2);
    CHECK(list[0].toMap()[u"_defaults"_s].toBool());
    CHECK(list[1].toMap()[u"name"_s].toString() == u"Front TV"_s);
    CHECK(list[1].toMap()[u"hours"_s].toStringList().first() == u"09:00-21:00"_s);
    CHECK_FALSE(manager.setBillboard(u"Front TV"_s, {{u"hours"_s, QVariantList{u"soon"_s, u""_s, u""_s, u""_s, u""_s, u""_s, u""_s}}}));
    REQUIRE(manager.setBillboard(u"Front TV"_s, {{u"hours"_s, QVariantList{}}, {u"enabled"_s, false}}));
    REQUIRE(waitFor([&] { return !tv->billboard()[u"enabled"_s].toBool(); }));
    CHECK_FALSE(tv->billboard()[u"showing"_s].toBool());   // off

    // Not a manager: no.
    PosService server(&s.shared, u"Bar"_s);
    REQUIRE(server.loginWithPin(u"1111"_s));
    CHECK_FALSE(server.setBillboard(u"Front TV"_s, {{u"enabled"_s, true}}));
    CHECK(server.adminRecords(u"billboards"_s).isEmpty());
}

TEST_CASE("Billboard: its screen loads with the slides and no warnings", "[ui][billboard]")
{
    Store s;
    s.shared.settings.displaySlides = {"Welcome to the store"};
    auto tv = s.connect(u"Front TV"_s, "billboard");
    BillboardController board(tv.get());
    CHECK(board.slides() == QStringList{u"Welcome to the store"_s});
    CHECK(board.showing());
    QStringList warnings;
    static QStringList *sink = nullptr;
    sink = &warnings;
    const auto previous = qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &msg) {
        if (type != QtDebugMsg && type != QtInfoMsg && sink)
            *sink << msg;
    });
    {
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"billboard"_s, QVariant::fromValue(&board)}});
        test::loadViewTouchQml(engine, "Billboard");
        REQUIRE(!engine.rootObjects().isEmpty());
        QTest::qWait(200);
    }
    qInstallMessageHandler(previous);
    sink = nullptr;
    INFO(warnings.join(u'\n').toStdString());
    CHECK(warnings.isEmpty());
}

// --- Store Settings > Billboards (pos_billboards.cpp, WidgetBillboards.qml) --------------------

namespace {

QVariantMap boardRecord(PosService &p, const QString &name)
{
    for (const QVariant &v : p.adminRecords(u"billboards"_s))
        if (v.toMap().value(u"name"_s).toString() == name)
            return v.toMap();
    return {};
}

int boardIndex(PosService &p, const QString &name)
{
    const QVariantList list = p.adminRecords(u"billboards"_s);
    for (int i = 0; i < list.size(); ++i)
        if (list[i].toMap().value(u"name"_s).toString() == name)
            return i;
    return -1;
}

// A stand-in for vt-add-billboard: says what it's told to, and keeps its arguments.
QString fakeScript(const QTemporaryDir &dir, const QByteArray &body)
{
    const QString path = dir.filePath(u"vt-add-billboard"_s);
    writeFile(path, "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$(dirname \"$0\")/args\"\n" + body);
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

QStringList argsOf(const QTemporaryDir &dir)
{
    QFile f(dir.filePath(u"args"_s));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed().split(u'\n');
}

} // namespace

TEST_CASE("Billboards page: hours typed the store's way, and when they next change", "[billboard]")
{
    CHECK(PosService::billboardDayHours(u"9am-9pm"_s) == std::optional<std::string>("09:00-21:00"));
    CHECK(PosService::billboardDayHours(u"9:30 PM - 2 AM"_s) == std::optional<std::string>("21:30-02:00"));
    CHECK(PosService::billboardDayHours(u"12am-12pm"_s) == std::optional<std::string>("00:00-12:00"));
    CHECK(PosService::billboardDayHours(u"9:00 AM-9:00 PM"_s) == std::optional<std::string>("09:00-21:00"));
    CHECK(PosService::billboardDayHours(u"All day"_s) == std::optional<std::string>(""));
    CHECK_FALSE(PosService::billboardDayHours(u"13pm-2am"_s));
    CHECK_FALSE(PosService::billboardDayHours(u"9am-9am"_s));
    CHECK(PosService::billboardDayText("21:30-02:00") == u"9:30 PM-2:00 AM"_s);
    CHECK(PosService::billboardDayText("closed") == u"closed"_s);
    CHECK(PosService::billboardDayText("").isEmpty());

    const auto at = [](int day, int h, int m) { return QDateTime(QDate(2026, 11, day), QTime(h, m)).toMSecsSinceEpoch(); };
    std::vector<std::string> week(7, "09:00-21:00");
    week[6] = "closed";   // Sunday
    CHECK(PosService::billboardNextChange({}, at(2, 12, 0)) == 0);                 // always on
    CHECK(PosService::billboardNextChange(week, at(2, 12, 0)) == at(2, 21, 0));    // Monday noon: off at 9 PM
    CHECK(PosService::billboardNextChange(week, at(2, 22, 0)) == at(3, 9, 0));     // ... on Tuesday at 9 AM
    CHECK(PosService::billboardNextChange(week, at(7, 22, 0)) == at(9, 9, 0));     // Saturday night: Monday
    CHECK(PosService::billboardNextChange(std::vector<std::string>(7, "closed"), at(2, 12, 0)) == 0);
}

TEST_CASE("Billboards page: listed like Terminals, edited there, the TV follows at once", "[net][billboard]")
{
    Store s;
    std::int64_t clock = QDateTime(QDate(2026, 11, 2), QTime(12, 0)).toMSecsSinceEpoch();   // Monday noon
    s.shared.setClock([&clock] { return clock; });
    auto tv = s.connect(u"Bar TV"_s, "billboard");
    PosService manager(&s.shared, u"Office"_s);
    REQUIRE(manager.loginWithPin(u"1234"_s));

    QStringList paths;
    for (const QVariant &f : manager.adminFields(u"billboards"_s))
        paths << f.toMap().value(u"path"_s).toString();
    for (const char *p : {"name", "ip", "status", "enabled", "now", "mute", "volume", "day1", "day7", "pairPort", "pairCode"})
        CHECK(paths.contains(QString::fromLatin1(p)));

    QVariantMap r = boardRecord(manager, u"Bar TV"_s);
    REQUIRE(!r.isEmpty());
    CHECK(r[u"_title"_s].toString() == u"Bar TV"_s);
    CHECK(r[u"_detail"_s].toString().contains(u"no IP yet"_s));
    CHECK(r[u"paired"_s].toBool());
    CHECK_FALSE(r[u"_askPairing"_s].toBool());

    r[u"ip"_s] = u"192.0.2.10"_s;
    r[u"volume"_s] = 40;
    r[u"mute"_s] = true;
    for (int d = 1; d <= 6; ++d)
        r[u"day"_s + QString::number(d)] = u"9am-9pm"_s;
    r[u"day7"_s] = u"closed"_s;
    REQUIRE(manager.adminSave(u"billboards"_s, boardIndex(manager, u"Bar TV"_s), r));
    REQUIRE(waitFor([&] { return tv->billboard()[u"volume"_s].toInt() == 40; }));
    CHECK(tv->billboard()[u"mute"_s].toBool());
    CHECK(tv->billboard()[u"showing"_s].toBool());
    r = boardRecord(manager, u"Bar TV"_s);
    CHECK(r[u"ip"_s].toString() == u"192.0.2.10"_s);
    CHECK(r[u"day1"_s].toString() == u"9:00 AM-9:00 PM"_s);
    CHECK(r[u"hours"_s].toStringList().first() == u"09:00-21:00"_s);
    CHECK(r[u"day7"_s].toString() == u"closed"_s);
    CHECK(r[u"_detail"_s].toString() == u"192.0.2.10 · offline · on · muted"_s);   // no server network list here

    // Off now: dark until its hours next change (9 PM), then its hours again.
    r[u"now"_s] = u"off"_s;
    REQUIRE(manager.adminSave(u"billboards"_s, boardIndex(manager, u"Bar TV"_s), r));
    REQUIRE(waitFor([&] { return !tv->billboard()[u"showing"_s].toBool(); }));
    r = boardRecord(manager, u"Bar TV"_s);
    CHECK(r[u"now"_s].toString() == u"off"_s);
    CHECK(r[u"nowUntil"_s].toLongLong() == QDateTime(QDate(2026, 11, 2), QTime(21, 0)).toMSecsSinceEpoch());
    clock = QDateTime(QDate(2026, 11, 3), QTime(9, 30)).toMSecsSinceEpoch();   // Tuesday morning
    r = boardRecord(manager, u"Bar TV"_s);
    CHECK(r[u"now"_s].toString().isEmpty());
    CHECK(r[u"showing"_s].toBool());
    emit s.shared.adminChanged();
    REQUIRE(waitFor([&] { return tv->billboard()[u"showing"_s].toBool(); }));

    // Wrong hours, a name that's taken, no address: said, nothing saved.
    QVariantMap bad = r;
    bad[u"day3"_s] = u"soon"_s;
    CHECK_FALSE(manager.adminSave(u"billboards"_s, boardIndex(manager, u"Bar TV"_s), bad));
    bad = r;
    bad[u"ip"_s] = QString();
    CHECK_FALSE(manager.adminSave(u"billboards"_s, boardIndex(manager, u"Bar TV"_s), bad));
    bad = r;
    bad[u"name"_s] = u"Office"_s;   // a screen of the store (listed: adminRecords("terminals") adds this one)
    manager.adminRecords(u"terminals"_s);
    CHECK_FALSE(manager.adminSave(u"billboards"_s, boardIndex(manager, u"Bar TV"_s), bad));

    // + Add: a name and an address; deployed later.
    QVariantMap fresh = manager.adminNewRecord(u"billboards"_s);
    fresh[u"name"_s] = u"Patio TV"_s;
    fresh[u"ip"_s] = u"192.0.2.11"_s;
    REQUIRE(manager.adminSave(u"billboards"_s, -1, fresh));
    const QVariantMap patio = boardRecord(manager, u"Patio TV"_s);
    REQUIRE(!patio.isEmpty());
    CHECK_FALSE(patio[u"paired"_s].toBool());
    CHECK(std::ranges::any_of(s.shared.settings.terminals,
                              [](const core::TerminalConfig &t) { return t.name == "Patio TV" && t.screen == "billboard"; }));
    CHECK_FALSE(manager.adminSave(u"billboards"_s, -1, fresh));   // there already

    // Kept as they were saved.
    const core::PosSettings back = app::settingsFromJson(app::toJson(s.shared.settings));
    const auto kept = std::ranges::find_if(back.terminals, [](const core::TerminalConfig &t) { return t.name == "Bar TV"; });
    REQUIRE(kept != back.terminals.end());
    CHECK(kept->billboardIp == "192.0.2.10");
    CHECK(kept->billboardVolume == 40);

    // Not a manager: nothing at all.
    PosService server(&s.shared, u"Bar"_s);
    REQUIRE(server.loginWithPin(u"1111"_s));
    CHECK(server.adminRecords(u"billboards"_s).isEmpty());
    CHECK_FALSE(server.billboardAction(u"Bar TV"_s, u"identify"_s));
    CHECK_FALSE(server.removeBillboard(u"Bar TV"_s, u"1234"_s));
}

TEST_CASE("Billboards page: Deploy and Restart run in the background with the script's words", "[billboard]")
{
    test::RecordingSink sink;
    PosShared shared{test::seedPosData(), &sink};
    core::TerminalConfig t;
    t.name = "Bar TV";
    t.screen = "billboard";
    t.billboardIp = "192.0.2.20";
    shared.settings.terminals.push_back(t);
    PosService manager(&shared, u"Office"_s);
    REQUIRE(manager.loginWithPin(u"1234"_s));
    const auto job = [&] { return boardRecord(manager, u"Bar TV"_s).value(u"job"_s).toMap(); };
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    SECTION("a Fire TV: installed and paired, step by step")
    {
        shared.billboardScript = fakeScript(dir, "echo '[1/6] Reaching it...'\necho '@vt maker Amazon'\nsleep 1\n"
                                                 "echo '[3/6] Installing...'\necho 'Bar TV is a billboard.'\n");
        REQUIRE(manager.billboardAction(u"Bar TV"_s, u"deploy"_s));
        CHECK(job()[u"running"_s].toBool());
        CHECK_FALSE(manager.billboardAction(u"Bar TV"_s, u"deploy"_s));   // busy
        REQUIRE(waitFor([&] { return !job()[u"running"_s].toBool(); }, 8000));
        CHECK(job()[u"ok"_s].toBool());
        CHECK(job()[u"step"_s].toInt() == 3);
        CHECK(job()[u"steps"_s].toInt() == 6);
        const QStringList lines = job()[u"lines"_s].toStringList();
        CHECK(lines.contains(u"Installing..."_s));
        CHECK(lines.contains(u"Bar TV is a billboard."_s));
        CHECK_FALSE(lines.join(u' ').contains(u"@vt"_s));   // notes for the page, never shown
        CHECK(argsOf(dir) == QStringList{u"Bar TV"_s, u"192.0.2.20"_s});
        CHECK(boardRecord(manager, u"Bar TV"_s)[u"maker"_s].toString() == u"Amazon"_s);
        CHECK_FALSE(boardRecord(manager, u"Bar TV"_s)[u"_askPairing"_s].toBool());

        REQUIRE(manager.billboardAction(u"Bar TV"_s, u"restart"_s));
        REQUIRE(waitFor([&] { return !job()[u"running"_s].toBool(); }, 8000));
        CHECK(argsOf(dir).contains(u"--restart"_s));
    }
    SECTION("not reachable: said plainly")
    {
        shared.billboardScript = fakeScript(dir, "echo '[1/6] Reaching it...'\necho '@vt unreachable'\n"
                                                 "echo \"The TV isn't reachable at 192.0.2.20.\"\nexit 1\n");
        REQUIRE(manager.billboardAction(u"Bar TV"_s, u"deploy"_s));
        REQUIRE(waitFor([&] { return !job()[u"running"_s].toBool(); }, 8000));
        CHECK_FALSE(job()[u"ok"_s].toBool());
        CHECK(job()[u"unreachable"_s].toBool());
        CHECK_FALSE(job()[u"refused"_s].toBool());
        CHECK(job()[u"lines"_s].toStringList().last() == u"The TV isn't reachable at 192.0.2.20."_s);
    }
    SECTION("a Google TV: its pairing port and code passed on")
    {
        shared.billboardScript = fakeScript(dir, "echo '@vt maker Google'\necho '@vt refused'\necho 'Refused.'\nexit 1\n");
        REQUIRE(manager.billboardAction(u"Bar TV"_s, u"deploy"_s));
        REQUIRE(waitFor([&] { return !job()[u"running"_s].toBool(); }, 8000));
        CHECK(job()[u"refused"_s].toBool());
        CHECK(boardRecord(manager, u"Bar TV"_s)[u"_askPairing"_s].toBool());
        CHECK_FALSE(manager.billboardAction(u"Bar TV"_s, u"deploy"_s, {{u"pairPort"_s, u"37"_s}, {u"pairCode"_s, u"x"_s}}));
        REQUIRE(manager.billboardAction(u"Bar TV"_s, u"deploy"_s, {{u"pairPort"_s, u"37123"_s}, {u"pairCode"_s, u"123456"_s}}));
        REQUIRE(waitFor([&] { return !job()[u"running"_s].toBool(); }, 8000));
        CHECK(argsOf(dir) == QStringList{u"Bar TV"_s, u"192.0.2.20"_s, u"--pair-port"_s, u"37123"_s, u"--pair-code"_s, u"123456"_s});
    }
    SECTION("no script on this computer")
    {
        shared.billboardScript = dir.filePath(u"not-there"_s);
        REQUIRE(manager.billboardAction(u"Bar TV"_s, u"deploy"_s));
        REQUIRE(waitFor([&] { return !job()[u"running"_s].toBool(); }, 8000));
        CHECK_FALSE(job()[u"ok"_s].toBool());
        CHECK(job()[u"lines"_s].toStringList().last().contains(u"isn't on the store's computer"_s));
    }
    SECTION("no address yet, or not a billboard")
    {
        shared.settings.terminals.back().billboardIp.clear();
        CHECK_FALSE(manager.billboardAction(u"Bar TV"_s, u"deploy"_s));
        CHECK_FALSE(manager.billboardAction(u"Register 9"_s, u"restart"_s));
        CHECK_FALSE(manager.billboardAction(u"Bar TV"_s, u"explode"_s));
    }
}

TEST_CASE("Billboards page: Identify shows its name on the TV for 10 seconds; Remove unpairs it", "[net][billboard]")
{
    Store s;
    std::int64_t clock = QDateTime(QDate(2026, 11, 2), QTime(12, 0)).toMSecsSinceEpoch();
    s.shared.setClock([&clock] { return clock; });
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    s.shared.billboardsFile = dir.filePath(u"billboards"_s);
    writeFile(s.shared.billboardsFile, "Bar TV|192.0.2.30\nPatio TV|192.0.2.31\n");
    auto tv = s.connect(u"Bar TV"_s, "billboard");
    PosService manager(&s.shared, u"Office"_s);
    REQUIRE(manager.loginWithPin(u"1234"_s));

    BillboardController board(tv.get());
    CHECK_FALSE(board.identify());
    CHECK(board.name() == u"Bar TV"_s);
    CHECK(board.volume() == 1.0);
    REQUIRE(manager.billboardAction(u"Bar TV"_s, u"identify"_s));
    REQUIRE(waitFor([&] { return board.identify(); }));
    clock += 11 * 1000;
    emit s.shared.adminChanged();
    REQUIRE(waitFor([&] { return !board.identify(); }));

    CHECK_FALSE(manager.removeBillboard(u"Bar TV"_s, u"1111"_s));   // not a manager's PIN
    CHECK(tv->isConnected());
    REQUIRE(manager.removeBillboard(u"Bar TV"_s, u"1234"_s));
    CHECK(boardRecord(manager, u"Bar TV"_s).isEmpty());
    CHECK(std::ranges::none_of(s.shared.settings.terminals, [](const core::TerminalConfig &t) { return t.name == "Bar TV"; }));
    REQUIRE(waitFor([&] { return !tv->isConnected(); }));
    QFile f(s.shared.billboardsFile);
    REQUIRE(f.open(QIODevice::ReadOnly));
    CHECK(f.readAll() == "Patio TV|192.0.2.31\n");
}

TEST_CASE("Billboards page: the TVs set up before it keep working (their addresses taken over)", "[billboard]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString file = dir.filePath(u"billboards"_s);
    writeFile(file, "Bar TV|192.0.2.40\nPatio TV|192.0.2.99\nRegister|192.0.2.42\nGone TV|192.0.2.43\n");
    writeFile(file + u".new"_s, "Side TV|192.0.2.44\n");   // a run stopped half way
    core::PosSettings settings;
    const auto add = [&](const char *name, const char *screen, const char *ip) {
        core::TerminalConfig t;
        t.name = name;
        t.screen = screen;
        t.billboardIp = ip;
        t.key = "a2V5";
        settings.terminals.push_back(t);
    };
    add("Bar TV", "billboard", "");
    add("Patio TV", "billboard", "192.0.2.50");
    add("Register", "", "");
    add("Side TV", "billboard", "");
    CHECK(PosService::migrateBillboardsFile(settings, file));
    CHECK(settings.terminals[0].billboardIp == "192.0.2.40");
    CHECK(settings.terminals[1].billboardIp == "192.0.2.50");   // set on the page already: kept
    CHECK(settings.terminals[2].billboardIp.empty());           // not a billboard
    CHECK(settings.terminals[3].billboardIp == "192.0.2.44");
    CHECK(settings.terminals[0].key == "a2V5");                 // still paired: still connects
    CHECK(settings.terminals[0].billboardOn);
    CHECK(settings.terminals[0].billboardVolume == 100);
    CHECK_FALSE(PosService::migrateBillboardsFile(settings, file));   // once
    CHECK_FALSE(PosService::migrateBillboardsFile(settings, dir.filePath(u"none"_s)));
}

TEST_CASE("Billboards page: the defaults, each one's own content, and Apply to…", "[net][billboard]")
{
    Store s;
    s.shared.setClock([] { return QDateTime(QDate(2026, 11, 2), QTime(12, 0)).toMSecsSinceEpoch(); });
    s.shared.settings.displaySlides = {"Welcome to the store"};
    auto bar = s.connect(u"Bar TV"_s, "billboard");
    auto patio = s.connect(u"Patio TV"_s, "billboard");
    PosService manager(&s.shared, u"Office"_s);
    REQUIRE(manager.loginWithPin(u"1234"_s));

    // The defaults: every billboard without its own goes by them.
    QVariantMap d = manager.adminRecords(u"billboards"_s).first().toMap();
    REQUIRE(d[u"_defaults"_s].toBool());
    d[u"volume"_s] = 30;
    d[u"mute"_s] = true;
    for (int i = 1; i <= 7; ++i)
        d[u"day"_s + QString::number(i)] = u"closed"_s;
    REQUIRE(manager.adminSave(u"billboards"_s, 0, d));
    REQUIRE(waitFor([&] { return bar->billboard()[u"volume"_s].toInt() == 30 && patio->billboard()[u"volume"_s].toInt() == 30; }));
    CHECK(bar->billboard()[u"mute"_s].toBool());
    CHECK_FALSE(bar->billboard()[u"showing"_s].toBool());   // closed every day
    QVariantMap r = boardRecord(manager, u"Bar TV"_s);
    CHECK(r[u"volume"_s].isNull());                         // "default"
    CHECK(r[u"_inherit"_s].toMap()[u"volume"_s].toInt() == 30);
    CHECK(r[u"_inherit"_s].toMap()[u"day1"_s].toString() == u"closed"_s);

    // Its own volume and hours: only this one.
    r[u"volume"_s] = 80;
    r[u"day1"_s] = u"9am-9pm"_s;   // a day typed: the week its own (the others the defaults')
    REQUIRE(manager.adminSave(u"billboards"_s, boardIndex(manager, u"Bar TV"_s), r));
    REQUIRE(waitFor([&] { return bar->billboard()[u"volume"_s].toInt() == 80; }));
    CHECK(bar->billboard()[u"showing"_s].toBool());         // Monday noon
    CHECK(bar->billboard()[u"mute"_s].toBool());            // still the defaults'
    CHECK(patio->billboard()[u"volume"_s].toInt() == 30);
    CHECK_FALSE(patio->billboard()[u"showing"_s].toBool());
    r = boardRecord(manager, u"Bar TV"_s);
    CHECK(r[u"day2"_s].toString() == u"closed"_s);
    CHECK(r[u"mute"_s].isNull());

    // Content: the guest screen's (live), until it has its own.
    CHECK(bar->billboard()[u"slides"_s].toList().first().toString() == u"Welcome to the store"_s);
    CHECK_FALSE(r[u"ownContent"_s].toBool());
    r[u"ownContent"_s] = true;
    r[u"slides"_s] = u"Happy hour 4-6\nTwo-for-one wings"_s;
    REQUIRE(manager.adminSave(u"billboards"_s, boardIndex(manager, u"Bar TV"_s), r));
    REQUIRE(waitFor([&] { return bar->billboard()[u"slides"_s].toList().first().toString() == u"Happy hour 4-6"_s; }));
    CHECK(patio->billboard()[u"slides"_s].toList().first().toString() == u"Welcome to the store"_s);

    // Apply to…: the patio and the guest screen get the bar's.
    CHECK_FALSE(manager.applyBillboardContent(u"Bar TV"_s, {}));
    REQUIRE(manager.applyBillboardContent(u"Bar TV"_s, {u"Patio TV"_s, u"@guest"_s}));
    REQUIRE(waitFor([&] { return patio->billboard()[u"slides"_s].toList().size() == 2; }));
    CHECK(patio->billboard()[u"slides"_s].toList().last().toString() == u"Two-for-one wings"_s);
    CHECK(s.shared.settings.displaySlides == std::vector<std::string>{"Happy hour 4-6", "Two-for-one wings"});

    // A new one starts as a copy of the guest screen's list, going by the defaults.
    const QVariantMap fresh = manager.adminNewRecord(u"billboards"_s);
    CHECK(fresh[u"ownContent"_s].toBool());
    CHECK(fresh[u"slides"_s].toString() == u"Happy hour 4-6\nTwo-for-one wings"_s);
    CHECK(fresh[u"volume"_s].isNull());
}

TEST_CASE("Billboards page: the page loads, a billboard open and + Add's steps, with no warnings", "[ui][billboard]")
{
    auto layout = test::loadTestLayout();
    REQUIRE(layout);
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    core::TerminalConfig t;
    t.name = "Bar TV";
    t.screen = "billboard";
    t.billboardIp = "192.0.2.60";
    t.billboardOwnSlides = true;
    t.billboardSlides = {"Happy hour 4-6"};
    pos.shared()->settings.terminals.push_back(t);
    LayoutController c(std::move(*layout));
    c.setPos(&pos);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(c.showPage(u"billboards"_s));

    QStringList warnings;
    static QStringList *sink2 = nullptr;
    sink2 = &warnings;
    const auto previous = qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &msg) {
        if (type != QtDebugMsg && type != QtInfoMsg && sink2)
            *sink2 << msg;
    });
    {
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"controller"_s, QVariant::fromValue(&c)}, {u"width"_s, 1600}, {u"height"_s, 900}});
        test::loadViewTouchQml(engine, "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        REQUIRE(window);
        QTest::qWait(100);
        std::function<QQuickItem *(QQuickItem *)> find = [&find](QQuickItem *i) -> QQuickItem * {
            if (i->property("forcedPanel").toString() == u"billboards"_s)
                return i;
            for (QQuickItem *c : i->childItems())
                if (QQuickItem *f = find(c))
                    return f;
            return nullptr;
        };
        QQuickItem *panel = find(window->contentItem());
        REQUIRE(panel);
        const QVariant records = panel->property("records");
        CHECK((records.metaType() == QMetaType::fromType<QJSValue>() ? records.value<QJSValue>().property(u"length"_s).toInt()
                                                                     : int(records.toList().size())) == 2);   // the defaults, Bar TV
        for (int i : {1, 0, 1}) {
            QMetaObject::invokeMethod(panel, "choose", Q_ARG(QVariant, i));
            QTest::qWait(50);
        }
        QMetaObject::invokeMethod(panel, "openPreview");
        QTest::qWait(100);
        QMetaObject::invokeMethod(panel, "openApplyTo");
        QTest::qWait(50);
        QMetaObject::invokeMethod(panel, "startAdding");
        for (int step : {1, 2, 3}) {
            panel->setProperty("addStep", step);
            QTest::qWait(50);
        }
        panel->setProperty("notFireTv", true);
        QTest::qWait(50);
    }
    qInstallMessageHandler(previous);
    sink2 = nullptr;
    INFO(warnings.join(u'\n').toStdString());
    CHECK(warnings.isEmpty());
}
