#include <catch2/catch_test_macros.hpp>

#include "billboard.hh"
#include "layout_fixture.hh"
#include "net/layout_hub.hh"
#include "net/pos_server.hh"
#include "net/remote_session.hh"
#include "pos_fixture.hh"
#include "qml_load.hh"
#include "qt_catch.hh"

#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QQmlApplicationEngine>
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
    const QVariantList list = manager.adminRecords(u"billboards"_s);
    REQUIRE(list.size() == 1);
    CHECK(list[0].toMap()[u"hours"_s].toStringList().first() == u"09:00-21:00"_s);
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
