#include <catch2/catch_test_macros.hpp>

#include "layout_fixture.hh"
#include "net/build_info.hh"
#include "net/layout_hub.hh"
#include "net/pairing.hh"
#include "net/pos_server.hh"
#include "net/protocol.hh"
#include "net/remote_session.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSslPreSharedKeyAuthenticator>
#include <QSignalSpy>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <optional>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosShared;

// Screens' updates (HCA 2026-11-15: a 160 MB app sent in one burst on the store's connection was cut
// off part way and never came back): the fast way on a connection of its own, picked up where it got
// to; only the device the ticket is for; and the old way, a part at a time, for older screens.

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

layout::Layout seedLayout()
{
    auto l = vt::test::loadTestLayout();
    REQUIRE(l);
    return *l;
}

struct Store {
    test::RecordingSink sink;
    PosShared shared{test::seedPosData(), &sink};
    net::LayoutHub hub{seedLayout()};
    net::PosServer server{&shared, &hub};
    QTemporaryDir updates;
    QString name;
    QByteArray payload;

    explicit Store(qsizetype size)
    {
        shared.setClock([] { return std::int64_t(1'700'000'000'000); });
        REQUIRE(server.listen(QHostAddress::LocalHost, 0));
        REQUIRE(updates.isValid());
        server.setUpdatesDir(updates.path());
        // An update for screens like this one (the tests run on Linux), newer than this build.
        const net::AppBuild me = net::appBuild();
        REQUIRE(me.platform.startsWith(u"linux-"_s));
        const QString arch = me.platform.section(u'-', 1);
        name = u"viewtouch-modern_0.9.0_%1-b%2.deb"_s.arg(arch == u"x86_64"_s ? u"amd64"_s : u"arm64"_s).arg(me.number + 7);
        payload.resize(size);
        for (qsizetype i = 0; i < size; ++i)
            payload[i] = char((i * 131) ^ (i >> 9));
        QFile f(updates.filePath(name));
        REQUIRE(f.open(QIODevice::WriteOnly));
        REQUIRE(f.write(payload) == payload.size());
        f.close();
        // Nothing left on this computer from an earlier run.
        for (const QString &suffix : {u""_s, u".part"_s, u".part.json"_s})
            QFile::remove(cacheFile(suffix));
    }

    QString cacheFile(const QString &suffix) const
    {
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + u"/updates/"_s + name + suffix;
    }

    net::Credentials pairedDevice(const QString &terminal)
    {
        core::TerminalConfig t;
        t.name = terminal.toStdString();
        t.id = net::newDeviceId().toStdString();
        const QByteArray key = net::newDeviceKey();
        t.key = key.toBase64().toStdString();
        shared.settings.terminals.push_back(t);
        net::Credentials c;
        c.host = u"127.0.0.1"_s;
        c.port = server.port();
        c.terminalId = QString::fromStdString(t.id);
        c.terminalName = terminal;
        c.key = key;
        return c;
    }

    // A screen of this version, connected as that device, with a manager logged in.
    std::unique_ptr<net::RemoteSession> managerScreen(const net::Credentials &c)
    {
        auto r = std::make_unique<net::RemoteSession>(c.terminalName);
        r->setCredentials(c);
        r->connectTo(c.host, c.port);
        REQUIRE(r->waitForWelcome(5000));
        for (const char *d : {"1", "2", "3", "4"})
            r->pinKey(QString::fromLatin1(d));
        r->invoke(u"login"_s);
        REQUIRE(waitFor([&] { return r->loggedIn(); }));
        return r;
    }
};

// A device talking to the store by hand (as an older screen does, or someone trying their luck).
struct Raw {
    QSslSocket socket;
    net::LineChannel channel{&socket};
    QList<QJsonObject> inbox;
    qint64 nextId = 1;

    Raw(const net::Credentials &c, quint16 port)
    {
        socket.setSslConfiguration(net::tlsConfiguration());
        QObject::connect(&socket, &QSslSocket::preSharedKeyAuthenticationRequired, [c](QSslPreSharedKeyAuthenticator *a) {
            a->setIdentity(net::deviceIdentity(c.terminalId));
            a->setPreSharedKey(c.key);
        });
        socket.connectToHostEncrypted(u"127.0.0.1"_s, port);
    }
    bool encrypted(int ms = 5000) { return waitFor([&] { return socket.isEncrypted(); }, ms); }
    void send(const QJsonObject &m) { channel.send(m); }
    void pump() { inbox += channel.receive(); }
    // The next message of that type (what came before it stays in the inbox).
    std::optional<QJsonObject> next(const QString &type, int ms = 5000)
    {
        std::optional<QJsonObject> found;
        waitFor([&] {
            if (found)   // (waitFor asks once more at the end)
                return true;
            pump();
            for (qsizetype i = 0; i < inbox.size(); ++i)
                if (inbox[i].value(u"t"_s).toString() == type) {
                    found = inbox.takeAt(i);
                    return true;
                }
            return false;
        }, ms);
        return found;
    }
    // Hello as a screen (`updates`: 2 for this version's, 0 for an older one's) and log in a manager.
    void helloAndLogin(int updates)
    {
        const net::AppBuild me = net::appBuild();
        QJsonObject hello{{u"t"_s, u"hello"_s}, {u"terminal"_s, u"x"_s}, {u"protocol"_s, net::ProtocolVersion},
                          {u"build"_s, me.number}, {u"version"_s, me.version}, {u"platform"_s, me.platform},
                          {u"subscribes"_s, true}};
        if (updates)
            hello.insert(u"updates"_s, updates);
        send(hello);
        REQUIRE(next(u"welcome"_s));
        // The manager's PIN (1234), then Login.
        for (const QString &digit : {u"1"_s, u"2"_s, u"3"_s, u"4"_s, QString()}) {
            send({{u"t"_s, u"call"_s}, {u"id"_s, nextId++}, {u"m"_s, digit.isEmpty() ? u"login"_s : u"pinKey"_s},
                  {u"a"_s, digit.isEmpty() ? QJsonArray{} : QJsonArray{digit}}});
            REQUIRE(next(u"reply"_s));
        }
    }
};

QString sha256(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}

} // namespace

TEST_CASE("Updates the fast way: cut off part way, picked up where they got to, checked", "[net][updates]")
{
    Store store(24 * 1024 * 1024 + 321);
    const net::Credentials creds = store.pairedDevice(u"Bar 1"_s);

    // Cut off at 10% or more: it goes on by itself, from where it got to.
    {
        auto screen = store.managerScreen(creds);
        bool cut = false;
        QObject::connect(screen.get(), &app::PosSession::updateChanged, screen.get(), [&] {
            const QVariantMap info = screen->updateInfo();
            if (!cut && info.value(u"downloading"_s, -1).toInt() >= 10) {
                if (auto *s = screen->findChild<QSslSocket *>(u"vtUpdateDownload"_s)) {
                    cut = true;
                    QMetaObject::invokeMethod(s, &QAbstractSocket::abort, Qt::QueuedConnection);
                }
            }
        });
        screen->getUpdate();
        REQUIRE(waitFor([&] { return screen->updateInfo().contains(u"ready"_s); }, 30000));
        CHECK(cut);
        CHECK(store.server.downloadsResumed() >= 1);
        QFile got(screen->updateInfo()[u"ready"_s].toString());
        REQUIRE(got.open(QIODevice::ReadOnly));
        CHECK(got.fileName() == store.cacheFile({}));
        CHECK(sha256(got.readAll()) == sha256(store.payload));
        CHECK_FALSE(QFile::exists(store.cacheFile(u".part"_s)));
        got.remove();
    }

    // The app closed part way: opened again, Update Now picks it up from what it had.
    const int resumed = store.server.downloadsResumed();
    {
        auto screen = store.managerScreen(creds);
        bool cut = false;
        QObject::connect(screen.get(), &app::PosSession::updateChanged, screen.get(), [&] {
            if (!cut && screen->updateInfo().value(u"downloading"_s, -1).toInt() >= 10)
                if (auto *s = screen->findChild<QSslSocket *>(u"vtUpdateDownload"_s)) {
                    cut = true;
                    QMetaObject::invokeMethod(s, &QAbstractSocket::abort, Qt::QueuedConnection);
                }
        });
        screen->getUpdate();
        REQUIRE(waitFor([&] { return cut && screen->updateInfo().value(u"waiting"_s).toBool(); }, 20000));
    }   // closed while it waits to try again
    REQUIRE(QFile(store.cacheFile(u".part"_s)).size() > 0);
    {
        auto screen = store.managerScreen(creds);
        screen->getUpdate();
        REQUIRE(waitFor([&] { return screen->updateInfo().contains(u"ready"_s); }, 30000));
        CHECK(store.server.downloadsResumed() == resumed + 1);
        QFile got(screen->updateInfo()[u"ready"_s].toString());
        REQUIRE(got.open(QIODevice::ReadOnly));
        CHECK(sha256(got.readAll()) == sha256(store.payload));
        got.remove();
    }
}

TEST_CASE("Updates the fast way: only the device the ticket is for, and only paired devices", "[net][updates][pairing]")
{
    Store store(512 * 1024);
    const net::Credentials a = store.pairedDevice(u"Bar 1"_s);
    const net::Credentials b = store.pairedDevice(u"Bar 2"_s);

    // A manager on device A gets a ticket for A.
    Raw screenA(a, store.server.port());
    REQUIRE(screenA.encrypted());
    screenA.helloAndLogin(2);
    screenA.send({{u"t"_s, u"getUpdate"_s}, {u"fast"_s, true}});
    const auto ticket = screenA.next(u"updateTicket"_s);
    REQUIRE(ticket);
    CHECK(ticket->value(u"name"_s).toString() == store.name);
    CHECK(ticket->value(u"size"_s).toInteger() == store.payload.size());
    CHECK(ticket->value(u"sha256"_s).toString() == sha256(store.payload));
    const QString token = ticket->value(u"token"_s).toString();
    REQUIRE(token.size() >= 32);

    // Device B can't use A's ticket, or one made up.
    for (const QString &t : {token, u"0123456789abcdef0123456789abcdef"_s}) {
        Raw other(b, store.server.port());
        REQUIRE(other.encrypted());
        other.send({{u"t"_s, u"download"_s}, {u"token"_s, t}, {u"from"_s, 0}});
        const auto no = other.next(u"downloadError"_s);
        REQUIRE(no);
        CHECK(no->value(u"text"_s).toString().contains(u"Update Now"_s));
        CHECK_FALSE(other.next(u"downloadStart"_s, 300));
    }
    CHECK(store.server.downloadsStarted() == 0);

    // A device the store never paired doesn't get a connection at all.
    net::Credentials stranger = a;
    stranger.key = net::newDeviceKey();
    Raw forged(stranger, store.server.port());
    CHECK_FALSE(forged.encrypted(3000));

    // Device A itself can, from any point.
    Raw download(a, store.server.port());
    REQUIRE(download.encrypted());
    const qint64 from = 100'000;
    download.send({{u"t"_s, u"download"_s}, {u"token"_s, token}, {u"from"_s, from}});
    QByteArray bytes;
    // All of it from there, then the store closes the connection.
    REQUIRE(waitFor([&] {
        bytes += download.socket.readAll();
        return download.socket.state() == QAbstractSocket::UnconnectedState;
    }, 10000));
    bytes += download.socket.readAll();
    const qsizetype nl = bytes.indexOf('\n');
    REQUIRE(nl > 0);
    const QJsonObject start = QJsonDocument::fromJson(bytes.left(nl)).object();
    CHECK(start.value(u"t"_s).toString() == u"downloadStart"_s);
    CHECK(start.value(u"from"_s).toInteger() == from);
    CHECK(bytes.mid(nl + 1) == store.payload.mid(from));
    CHECK(store.server.downloadsResumed() == 1);
}

TEST_CASE("Updates the old way, for older screens: a part at a time, the store still heard meanwhile", "[net][updates]")
{
    // More than this computer's own network buffers hold (a few MB), so what the store keeps queued shows.
    Store store(32 * 1024 * 1024 + 77);
    const net::Credentials a = store.pairedDevice(u"Old Tablet"_s);
    QString cutId;
    {
        Raw old(a, store.server.port());
        old.socket.setReadBufferSize(256 * 1024);   // read as it's used, as a tablet on Wi-Fi does
        REQUIRE(old.encrypted());
        old.helloAndLogin(0);
        // Told once (a strip stuck at "Getting the update... N%" from before gets its Update Now back).
        CHECK(old.next(u"updateError"_s));

        old.send({{u"t"_s, u"getUpdate"_s}});
        const auto first = old.next(u"updatePart"_s);
        REQUIRE(first);
        CHECK(first->value(u"i"_s).toInt() == 0);
        const int parts = first->value(u"n"_s).toInt();
        CHECK(parts >= 256);   // small parts: a ping or an answer is never far behind
        // Asked something now, it answers before the update is through.
        old.send({{u"t"_s, u"call"_s}, {u"id"_s, 99}, {u"m"_s, u"pinKey"_s}, {u"a"_s, QJsonArray{u"5"_s}}});
        QByteArray data = QByteArray::fromBase64(first->value(u"data"_s).toString().toLatin1());
        QJsonObject done;
        bool answeredMeanwhile = false;
        int expect = 1;
        REQUIRE(waitFor([&] {
            old.pump();
            while (!old.inbox.isEmpty()) {
                const QJsonObject m = old.inbox.takeFirst();
                const QString t = m.value(u"t"_s).toString();
                if (t == u"reply"_s && m.value(u"id"_s).toInteger() == 99)
                    answeredMeanwhile = done.isEmpty();
                if (t != u"updatePart"_s)
                    continue;
                CHECK(m.value(u"i"_s).toInt() == expect++);
                data += QByteArray::fromBase64(m.value(u"data"_s).toString().toLatin1());
                done = m.value(u"done"_s).toObject();
            }
            return !done.isEmpty();
        }, 30000));
        CHECK(expect == parts);
        CHECK(answeredMeanwhile);
        CHECK(done.value(u"name"_s).toString() == store.name);
        CHECK(done.value(u"size"_s).toInteger() == store.payload.size());
        CHECK(done.value(u"sha256"_s).toString() == sha256(data));
        CHECK(data == store.payload);

        // Cut off part way through another one...
        old.send({{u"t"_s, u"getUpdate"_s}});
        REQUIRE(old.next(u"updatePart"_s));
        old.socket.abort();
    }
    waitFor([] { return false; }, 300);
    // ...it's told when it's back, so its Update Now shows again.
    Raw back(a, store.server.port());
    REQUIRE(back.encrypted());
    back.helloAndLogin(0);
    CHECK(back.next(u"updateError"_s));
}

TEST_CASE("Updates by themselves: a paired tablet fetches the store's update with nobody signed in, and waits", "[net][updates]")
{
    Store store(2 * 1024 * 1024 + 5);
    const net::Credentials creds = store.pairedDevice(u"BHDAN 1"_s);

    // Nobody signed in: Update Now (a person asking) still needs a manager...
    {
        auto screen = std::make_unique<net::RemoteSession>(creds.terminalName);
        screen->setCredentials(creds);
        screen->connectTo(creds.host, creds.port);
        REQUIRE(screen->waitForWelcome(5000));
        REQUIRE_FALSE(screen->loggedIn());
        REQUIRE(screen->updateInfo().contains(u"update"_s));
        screen->getUpdate();
        REQUIRE(waitFor([&] { return screen->updateInfo().contains(u"error"_s); }));
        CHECK(screen->updateInfo()[u"error"_s].toString().contains(u"manager"_s));

        // ...but the screen fetching it by itself gets it, and doesn't open it (Main.qml installs it
        // once nobody is signed in): no "Update saved" here, nothing said at all.
        QSignalSpy notices(screen.get(), &app::PosSession::notice);
        screen->autoUpdate();
        REQUIRE(waitFor([&] { return screen->updateInfo().contains(u"ready"_s); }, 20000));
        CHECK_FALSE(screen->updateInfo().contains(u"error"_s));
        QFile got(screen->updateInfo()[u"ready"_s].toString());
        REQUIRE(got.open(QIODevice::ReadOnly));
        CHECK(sha256(got.readAll()) == sha256(store.payload));
        for (const QList<QVariant> &n : notices)
            CHECK_FALSE(n.value(0).toString().contains(u"Update saved"_s));
        // Here already: asked again by itself, nothing happens; getUpdate opens it (on a computer: said).
        screen->autoUpdate();
        CHECK(screen->updateInfo().value(u"downloading"_s, -1).toInt() == -1);
        screen->getUpdate();
        CHECK(waitFor([&] {
            for (const QList<QVariant> &n : notices)
                if (n.value(0).toString().contains(u"Update saved"_s))
                    return true;
            return false;
        }));
        got.remove();
    }

    // By hand: only the fast way, only as itself. Not signed in, "auto" gets the ticket; without it, no.
    Raw raw(creds, store.server.port());
    REQUIRE(raw.encrypted());
    const net::AppBuild me = net::appBuild();
    raw.send({{u"t"_s, u"hello"_s}, {u"terminal"_s, u"x"_s}, {u"protocol"_s, net::ProtocolVersion},
              {u"build"_s, me.number}, {u"version"_s, me.version}, {u"platform"_s, me.platform},
              {u"subscribes"_s, true}, {u"updates"_s, 2}});
    REQUIRE(raw.next(u"welcome"_s));
    raw.send({{u"t"_s, u"getUpdate"_s}, {u"fast"_s, true}});
    const auto no = raw.next(u"updateError"_s);
    REQUIRE(no);
    CHECK(no->value(u"text"_s).toString().contains(u"manager"_s));
    raw.send({{u"t"_s, u"getUpdate"_s}, {u"auto"_s, true}});   // the old way: never by itself
    CHECK(raw.next(u"updateError"_s));
    raw.send({{u"t"_s, u"getUpdate"_s}, {u"fast"_s, true}, {u"auto"_s, true}});
    const auto ticket = raw.next(u"updateTicket"_s);
    REQUIRE(ticket);
    CHECK(ticket->value(u"sha256"_s).toString() == sha256(store.payload));
}
