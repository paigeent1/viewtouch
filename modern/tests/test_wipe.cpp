// The cutover step: scripts/bin/vt-wipe-transactions on a made-up store's
// database. The beta's transactions go; items, staff, customers, settings,
// pictures, price changes and the ILSA history file stay; new checks start
// at 100001, after a restart too.

#include <catch2/catch_test_macros.hpp>

#include "pos_fixture.hh"
#include "qt_catch.hh"
#include "storage/async_writer.hh"
#include "storage/pos_store.hh"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

namespace {

const QString kScript = QStringLiteral(VTM_SCRIPTS_DIR "/vt-wipe-transactions");

struct Run {
    int code = -1;
    QString out;
};

Run wipeTool(const QStringList &args)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(u"python3"_s, QStringList{kScript} + args);
    Run r;
    if (!p.waitForStarted(10000) || !p.waitForFinished(120000))
        return r;
    r.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    r.out = QString::fromUtf8(p.readAll());
    return r;
}

// Runs SQL on a database with a connection of its own (closed after).
template <class F>
void withDb(const QString &path, F &&f)
{
    const QString name = u"wipe-test-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, name);
        db.setDatabaseName(path);
        REQUIRE(db.open());
        QSqlQuery q(db);
        f(q);
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
}

qint64 count(const QString &path, const QString &table)
{
    qint64 n = -1;
    withDb(path, [&](QSqlQuery &q) {
        if (q.exec(u"SELECT COUNT(*) FROM "_s + table) && q.next())
            n = q.value(0).toLongLong();
    });
    return n;
}

QByteArray fileHash(const QString &path)
{
    QFile f(path);
    REQUIRE(f.open(QIODevice::ReadOnly));
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
}

// Every table's rows, for "nothing changed".
QString dump(const QString &path)
{
    QString out;
    withDb(path, [&](QSqlQuery &q) {
        QStringList names;
        if (q.exec(u"SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name"_s))
            while (q.next())
                names << q.value(0).toString();
        for (const QString &t : names) {
            out += u"# "_s + t + u'\n';
            if (q.exec(u"SELECT * FROM "_s + t + u" ORDER BY 1"_s))
                while (q.next()) {
                    for (int i = 0; i < q.record().count(); ++i)
                        out += q.value(i).toString() + u'|';
                    out += u'\n';
                }
        }
    });
    return out;
}

void cashCheck(PosService &pos, const char *item)
{
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(QString::fromLatin1(item));
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
}

// A beta's worth of transactions on a made-up store (made-up names only).
void ringUpBeta(const QString &path)
{
    const auto seed = test::seedPosData();
    {
        storage::PosStore store(path);
        REQUIRE(store.open());
        REQUIRE(store.seed(seed.settings, seed.menu, seed.employees, nullptr, seed.ingredients));
    }
    storage::PosStore store(path);
    REQUIRE(store.open());
    storage::AsyncWriter writer(path);
    storage::SqlPosSink sink(writer);
    PosService pos(*store.load(), &sink);
    REQUIRE(pos.loginWithPin(u"1111"_s));
    REQUIRE(pos.clockIn());
    pos.logout();
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());

    REQUIRE(pos.saveCustomer({{u"name"_s, u"Testy McTestface"_s}, {u"phone"_s, u"555-010-9999"_s},
                              {u"email"_s, u"testy@example.com"_s}}));
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    REQUIRE(pos.useCustomer(QString::fromStdString(pos.shared()->customers.front().id)));
    pos.addItem(u"cobb"_s);
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());

    REQUIRE(pos.sellGiftCard(u"600011112221"_s, 5000));   // $50 card, then $10 of it spent
    REQUIRE(pos.tender(u"cash"_s));
    pos.sendOrder();
    REQUIRE(pos.closeCheck());
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"cobb"_s);
    REQUIRE(pos.payWithGiftCard(u"600011112221"_s, 1000));
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());

    cashCheck(pos, "soda");
    const core::Check &paid = pos.shared()->closedToday.back();
    REQUIRE(pos.refundPayment(paid.id, paid.payments.front().id, 100, u"Wrong item"_s));

    pos.selectTable(u"T2"_s);                              // left open
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"caesar"_s);
    pos.sendOrder();
    pos.releaseCheck();
}

} // namespace

TEST_CASE("Cutover: vt-wipe-transactions wipes the beta, keeps the store, checks start at 100001", "[wipe]")
{
    if (QStandardPaths::findExecutable(u"python3"_s).isEmpty())
        SKIP("python3 is not installed");
    REQUIRE(QFile::exists(kScript));
    QTemporaryDir dir;
    const QString data = dir.filePath(u"data"_s), backups = dir.filePath(u"backups"_s);
    REQUIRE(QDir().mkpath(data));
    const QString db = data + u"/viewtouch.db"_s;
    ringUpBeta(db);

    // Things the wipe must keep: a picture, a price change, and the ILSA history file.
    withDb(db, [](QSqlQuery &q) {
        REQUIRE(q.exec(u"INSERT INTO images (name, data) VALUES ('logo', 'aGVsbG8=')"_s));
        REQUIRE(q.exec(u"INSERT INTO changes (id, at, day, json) VALUES (900, 1, 7, "
                       "'{\"id\":900,\"area\":\"Menu items\",\"item\":\"Cobb\",\"field\":\"Price\",\"before\":\"$11.00\",\"after\":\"$12.50\"}')"_s));
        REQUIRE(q.exec(u"INSERT INTO changes (id, at, day, json) VALUES (901, 2, 7, "
                       "'{\"id\":901,\"area\":\"Time punches\",\"item\":\"Pat Sample\",\"field\":\"Clock in\"}')"_s));
    });
    const QString history = data + u"/ilsa-history.db"_s;
    withDb(history, [](QSqlQuery &q) {
        REQUIRE(q.exec(u"CREATE TABLE receipts (id INTEGER PRIMARY KEY, total INTEGER)"_s));
        REQUIRE(q.exec(u"INSERT INTO receipts (id, total) VALUES (1, 1234)"_s));
    });
    const QByteArray historyHash = fileHash(history);
    const qint64 employees = count(db, u"employees"_s), items = count(db, u"menu_items"_s);
    REQUIRE(count(db, u"checks"_s) >= 5);
    REQUIRE(count(db, u"refunds"_s) == 1);
    REQUIRE(count(db, u"drawer_sessions"_s) == 1);
    REQUIRE(count(db, u"time_punches"_s) == 1);
    REQUIRE(count(db, u"business_days"_s) == 1);

    const QStringList common{u"--data-dir"_s, data, u"--backup-root"_s, backups, u"--no-stop"_s, u"--no-start"_s, u"--yes"_s};

    // A dry run says what would go and changes nothing (and points at the open check).
    const QString was = dump(db);
    const Run dry = wipeTool(common + QStringList{u"--dry-run"_s});
    INFO(dry.out.toStdString());
    CHECK(dry.code == 1);                                   // an open check
    CHECK(dry.out.contains(u"DRY RUN"_s));
    CHECK(dry.out.contains(u"T2"_s));
    CHECK(dump(db) == was);
    CHECK_FALSE(QDir(backups).exists());

    // With an open check it refuses, and lists it.
    const Run refused = wipeTool(common);
    INFO(refused.out.toStdString());
    CHECK(refused.code != 0);
    CHECK(refused.out.contains(u"T2"_s));
    CHECK(dump(db) == was);

    // The open check is closed; now it runs.
    {
        storage::PosStore store(db);
        REQUIRE(store.open());
        storage::AsyncWriter writer(db);
        storage::SqlPosSink sink(writer);
        PosService pos(*store.load(), &sink);
        REQUIRE(pos.loginWithPin(u"1234"_s));
        REQUIRE(pos.openCheck(pos.shared()->open.begin()->first));
        REQUIRE(pos.tender(u"cash"_s));
        REQUIRE(pos.closeCheck());
    }
    const Run done = wipeTool(common);
    INFO(done.out.toStdString());
    REQUIRE(done.code == 0);

    // The transactions are gone.
    for (const QString &t : {u"checks"_s, u"refunds"_s, u"business_days"_s, u"drawer_sessions"_s, u"time_punches"_s,
                             u"parties"_s})
        CHECK(count(db, t) == 0);

    // The store is kept.
    CHECK(count(db, u"employees"_s) == employees);
    CHECK(count(db, u"menu_items"_s) == items);
    CHECK(count(db, u"images"_s) == 1);
    CHECK(count(db, u"changes"_s) == 1);                    // the price change; not the punch edit
    CHECK(fileHash(history) == historyHash);

    {
        storage::PosStore store(db);
        REQUIRE(store.open());
        const auto loaded = store.load();
        REQUIRE(loaded);
        CHECK(loaded->openChecks.empty());
        CHECK(loaded->lastCheckId == 100000);
        CHECK(loaded->lastDayId >= 7);                      // the kept change's day is never used again
        CHECK(loaded->employees.size() == std::size_t(employees));
        REQUIRE(loaded->customers.size() == 1);
        const core::CustomerRecord &c = loaded->customers.front();
        CHECK(c.name == "Testy McTestface");
        CHECK(c.email == "testy@example.com");
        CHECK(core::CustomerRecord::digits(c.phone) == "5550109999");
        CHECK(c.visits == 0);
        CHECK(c.spent.cents() == 0);
        CHECK(c.lastOrder.empty());
        REQUIRE(loaded->giftCards.size() == 1);             // its balance kept (the default), activity cleared
        const core::GiftCard &g = loaded->giftCards.front();
        CHECK(g.balance == Money::fromCents(4000));
        Money sum;
        for (const core::LedgerEntry &e : g.history) {
            CHECK(e.checkId == 0);
            sum += e.amount;
        }
        CHECK(sum == g.balance);

        // New checks start at 100001, and go on from there.
        storage::AsyncWriter writer(db);
        storage::SqlPosSink sink(writer);
        PosService pos(*loaded, &sink);
        CHECK(pos.shared()->day.id > 7);
        REQUIRE(pos.loginWithPin(u"1234"_s));
        pos.entryKey(u"10000"_s);
        REQUIRE(pos.openDrawerSession());
        REQUIRE(pos.startCheck(core::CheckType::Takeout));
        CHECK(pos.checkInfo().value(u"id"_s).toLongLong() == 100001);
        pos.addItem(u"soda"_s);
        REQUIRE(pos.tender(u"cash"_s));
        REQUIRE(pos.closeCheck());
    }
    {   // ...after a restart too.
        storage::PosStore store(db);
        REQUIRE(store.open());
        const auto loaded = store.load();
        REQUIRE(loaded);
        CHECK(loaded->lastCheckId == 100001);
        PosService pos(*loaded, nullptr);
        REQUIRE(pos.loginWithPin(u"1234"_s));
        REQUIRE(pos.startCheck(core::CheckType::Takeout));
        CHECK(pos.checkInfo().value(u"id"_s).toLongLong() == 100002);
    }

    // The backup: the whole data folder as it was, checked.
    const QStringList made = QDir(backups).entryList({u"before-wipe-*"_s}, QDir::Dirs);
    REQUIRE(made.size() == 1);
    const QString backup = backups + u'/' + made.first();
    CHECK(QFile::exists(backup + u"/ilsa-history.db"_s));
    CHECK(QFile::exists(backup + u"/gift-cards-before-wipe.csv"_s));
    CHECK(count(backup + u"/viewtouch.db"_s, u"checks"_s) >= 5);
    CHECK(count(backup + u"/viewtouch.db"_s, u"time_punches"_s) == 1);
    CHECK(count(backup + u"/viewtouch.db"_s, u"changes"_s) == 2);

    // --keep-gift-balances no, from that backup: the card goes back to before the beta (an unsold card).
    const QString again = dir.filePath(u"again"_s);
    REQUIRE(QDir().mkpath(again));
    REQUIRE(QFile::copy(backup + u"/viewtouch.db"_s, again + u"/viewtouch.db"_s));
    const Run no = wipeTool({u"--data-dir"_s, again, u"--backup-root"_s, dir.filePath(u"backups2"_s), u"--no-stop"_s,
                             u"--no-start"_s, u"--yes"_s, u"--keep-gift-balances"_s, u"no"_s, u"--next-check"_s, u"200001"_s});
    INFO(no.out.toStdString());
    REQUIRE(no.code == 0);
    storage::PosStore store(again + u"/viewtouch.db"_s);
    REQUIRE(store.open());
    const auto loaded = store.load();
    REQUIRE(loaded);
    REQUIRE(loaded->giftCards.size() == 1);
    CHECK(loaded->giftCards.front().balance.cents() == 0);
    CHECK(loaded->giftCards.front().history.empty());
    CHECK(loaded->giftCards.front().issuedAt == 0);
    CHECK(loaded->lastCheckId == 200000);
}
