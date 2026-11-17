#include <catch2/catch_test_macros.hpp>

#include "app/eod_archive.hh"
#include "pos_fixture.hh"
#include "print/spooler.hh"
#include "print/ticket_printer.hh"
#include "qt_catch.hh"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <sys/stat.h>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
namespace eod = vt::app::eod;

// The End of Day archive (app/eod_archive.hh, app/pos_eod_archive.cpp): the day's PDF kept write-once
// beside the database in a hash chain, checked, moved to a long-term folder; Manager > Reports.

namespace {

int modeOf(const QString &path)
{
    struct stat st {};
    return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? int(st.st_mode & 0777) : -1;
}

// Runs SQL on the archive's database as someone with the computer's rights could (no ViewTouch).
bool sql(const QString &db, const QString &statement, QString *error = nullptr)
{
    bool ok = false;
    {
        QSqlDatabase d = QSqlDatabase::addDatabase(u"QSQLITE"_s, u"eod-test"_s);
        d.setDatabaseName(db);
        if (d.open()) {
            QSqlQuery q(d);
            ok = q.exec(statement);
            if (!ok && error)
                *error = q.lastError().text();
            d.close();
        }
    }
    QSqlDatabase::removeDatabase(u"eod-test"_s);
    return ok;
}

QByteArray fakePdf(const char *what)
{
    return QByteArray("%PDF-1.4\n1 0 obj << /Type /Page >> endobj\n% ") + what + "\n%%EOF\n";
}

struct ArchivedPos {
    QTemporaryDir dir;
    print::PrintSpooler spooler;
    print::TicketPrinter printer{spooler, dir.filePath(u"printouts"_s)};
    PosService pos{test::seedPosData(), nullptr};
    ArchivedPos()
    {
        pos.shared()->archiveDb = dir.filePath(u"viewtouch.db"_s);
        pos.shared()->archiveDir = dir.filePath(u"eod-archive"_s);
        pos.shared()->archivePagesDir = dir.filePath(u"pages"_s);
        pos.setPrinter(&printer);
    }
    QString db() const { return pos.shared()->archiveDb; }
    QString archive() const { return pos.shared()->archiveDir; }
    void waitForCheck() { REQUIRE(QTest::qWaitFor([&] { return !pos.shared()->archiveChecking; }, 20000)); }
    // A sale with a void after send (an exception), then End of Day.
    void closeDay()
    {
        REQUIRE(pos.loginWithPin(u"1234"_s));
        REQUIRE(pos.startCheck(core::CheckType::Quick));
        pos.addItem(u"cobb"_s);
        pos.addItem(u"caesar"_s);
        pos.finishChoosing();
        REQUIRE(pos.sendOrder());
        REQUIRE(pos.voidItem());
        REQUIRE(pos.tender(u"credit"_s));
        REQUIRE(pos.closeCheck());
        REQUIRE(pos.endOfDay());
        waitForCheck();
    }
};

} // namespace

TEST_CASE("End of Day keeps the day's PDF in the archive beside the database, read-only, not on the desktop",
          "[eodarchive]")
{
    const QByteArray home = qgetenv("HOME");
    QTemporaryDir fakeHome;
    QDir(fakeHome.path()).mkpath(u"Desktop"_s);
    qputenv("HOME", fakeHome.path().toLocal8Bit());
    ArchivedPos t;
    const std::int64_t dayId = t.pos.currentDay().id;
    const QString date = QDateTime::fromMSecsSinceEpoch(t.pos.currentDay().openedAt).date().toString(u"yyyy-MM-dd"_s);
    t.closeDay();
    qputenv("HOME", home);

    const std::vector<eod::Entry> all = eod::entries(t.db());
    REQUIRE(all.size() == 1);
    const eod::Entry &e = all.front();
    CHECK(e.kind == u"archived"_s);
    CHECK(e.dayId == dayId);
    CHECK(e.date == date);
    CHECK(e.file == date.left(4) + u'/' + date + u" day %1.pdf"_s.arg(dayId));
    const QString path = QDir(t.archive()).filePath(e.file);
    REQUIRE(QFile::exists(path));
    CHECK(modeOf(path) == 0444);   // read-only
    CHECK(eod::sha256File(path) == e.pdfSha);
    CHECK(QFileInfo(path).size() == e.size);
    CHECK(e.prevHash == QString::fromLatin1(eod::kGenesis));
    CHECK(e.recordHash == eod::recordHash(e));
    QFile f(path);
    REQUIRE(f.open(QIODevice::ReadOnly));
    CHECK(f.readAll().startsWith("%PDF"));

    // Nothing on the desktop, no loose pictures anywhere.
    QDirIterator it(QDir(fakeHome.path()).filePath(u"Desktop"_s), QDir::AllEntries | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    QStringList onDesktop;
    while (it.hasNext())
        onDesktop << it.next();
    CHECK(onDesktop.isEmpty());
    QDirIterator pics(t.archive(), {u"*.jpg"_s, u"*.png"_s}, QDir::Files, QDirIterator::Subdirectories);
    CHECK_FALSE(pics.hasNext());

    // Checked at End of Day, and the screen shows it.
    CHECK(t.pos.shared()->archiveCheck.value(u"ok"_s).toBool());
    CHECK(t.pos.shared()->archiveNews.value(u"file"_s).toString() == e.file);
    const QVariantMap screen = t.pos.eodArchive();
    REQUIRE(screen.value(u"days"_s).toList().size() == 1);
    const QVariantMap day = screen.value(u"days"_s).toList().front().toMap();
    CHECK(day.value(u"date"_s).toString() == date);
    CHECK(day.value(u"place"_s).toString() == u"here"_s);
    CHECK(day.value(u"sha"_s).toString() == e.pdfSha);
    CHECK(day.value(u"state"_s).toString() == u"ok"_s);
    CHECK(screen.value(u"longTerm"_s).toMap().value(u"folder"_s).toString().isEmpty());
}

TEST_CASE("The archive's records are append-only and chained; a changed PDF or a removed record shows",
          "[eodarchive]")
{
    QTemporaryDir dir;
    const QString db = dir.filePath(u"viewtouch.db"_s), archive = dir.filePath(u"eod-archive"_s);
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    REQUIRE(eod::archiveDay(db, archive, 1, QDate(2026, 11, 13), fakePdf("one"), now));
    REQUIRE(eod::archiveDay(db, archive, 2, QDate(2026, 11, 14), fakePdf("two"), now));
    REQUIRE(eod::archiveDay(db, archive, 3, QDate(2026, 11, 15), fakePdf("three"), now));
    const std::vector<eod::Entry> all = eod::entries(db);
    REQUIRE(all.size() == 3);
    CHECK(all[1].prevHash == all[0].recordHash);
    CHECK(all[2].prevHash == all[1].recordHash);
    eod::Check c = eod::verify(db, archive);
    CHECK(c.ok);
    CHECK(c.days == 3);
    CHECK(c.checked == 3);

    // SQLite itself refuses to change or remove a record.
    QString why;
    CHECK_FALSE(sql(db, u"UPDATE eod_archive SET size = 1 WHERE seq = 2"_s, &why));
    CHECK(why.contains(u"append-only"_s));
    CHECK_FALSE(sql(db, u"DELETE FROM eod_archive WHERE seq = 2"_s));
    CHECK(eod::verify(db, archive).ok);

    SECTION("a byte of a PDF changed")
    {
        const QString path = QDir(archive).filePath(all[1].file);
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QFile f(path);
        REQUIRE(f.open(QIODevice::ReadWrite));
        f.seek(12);
        f.write("X");
        f.close();
        c = eod::verify(db, archive);
        CHECK_FALSE(c.ok);
        CHECK(c.problem == u"Day 2026-11-14: the PDF has changed since it was archived."_s);
    }
    SECTION("a PDF deleted")
    {
        QFile::remove(QDir(archive).filePath(all[0].file));
        c = eod::verify(db, archive);
        CHECK_FALSE(c.ok);
        CHECK(c.problem.startsWith(u"Day 2026-11-13: the PDF is missing"_s));
    }
    SECTION("a record removed (by someone with the computer's rights)")
    {
        REQUIRE(sql(db, u"DROP TRIGGER eod_archive_no_delete"_s));
        REQUIRE(sql(db, u"DELETE FROM eod_archive WHERE seq = 2"_s));
        c = eod::verify(db, archive);
        CHECK_FALSE(c.ok);
        CHECK(c.problem.startsWith(u"Record 2 is missing from the archive"_s));
    }
    SECTION("the last record removed: its PDF has no record")
    {
        REQUIRE(sql(db, u"DROP TRIGGER eod_archive_no_delete"_s));
        REQUIRE(sql(db, u"DELETE FROM eod_archive WHERE seq = 3"_s));
        c = eod::verify(db, archive);
        CHECK_FALSE(c.ok);
        CHECK(c.problem.contains(u"has no record"_s));
    }
    SECTION("a record's fingerprint rewritten to match a changed PDF")
    {
        REQUIRE(sql(db, u"DROP TRIGGER eod_archive_no_update"_s));
        REQUIRE(sql(db, u"UPDATE eod_archive SET pdf_sha256 = '00' WHERE seq = 1"_s));
        c = eod::verify(db, archive);
        CHECK_FALSE(c.ok);
        CHECK(c.problem == u"Day 2026-11-13: its archive record was changed since it was written."_s);
    }
}

TEST_CASE("A day archived again gets a re-archived record and file; the first is kept", "[eodarchive]")
{
    ArchivedPos t;
    REQUIRE(t.pos.loginWithPin(u"1234"_s));
    t.pos.archiveEndOfDay();   // the day closed...
    t.waitForCheck();
    t.pos.archiveEndOfDay();   // ...and closed again (reopened)
    t.waitForCheck();
    const std::vector<eod::Entry> all = eod::entries(t.db());
    REQUIRE(all.size() == 2);
    CHECK(all[0].kind == u"archived"_s);
    CHECK(all[1].kind == u"re-archived"_s);
    CHECK(all[1].dayId == all[0].dayId);
    CHECK(all[1].prevHash == all[0].recordHash);
    CHECK(all[0].file != all[1].file);
    CHECK(all[1].file.contains(u"(re-archived 2)"_s));
    CHECK(QFile::exists(QDir(t.archive()).filePath(all[0].file)));
    CHECK(QFile::exists(QDir(t.archive()).filePath(all[1].file)));
    CHECK(eod::sha256File(QDir(t.archive()).filePath(all[0].file)) == all[0].pdfSha);
    CHECK(t.pos.checkEodArchiveNow().value(u"ok"_s).toBool());
    CHECK(t.pos.eodArchive().value(u"days"_s).toList().size() == 2);
}

TEST_CASE("Old days move to the long-term folder, checked first; an unreachable folder leaves them", "[eodarchive][longterm]")
{
    QTemporaryDir dir;
    const QString db = dir.filePath(u"viewtouch.db"_s), archive = dir.filePath(u"eod-archive"_s);
    const QString nas = dir.filePath(u"nas"_s);
    QDir().mkpath(nas);
    {
        QFile mark(QDir(nas).filePath(u"README"_s));   // a share in use: not an empty mount point
        REQUIRE(mark.open(QIODevice::WriteOnly));
        mark.write("NAS\n");
    }
    const QDate today(2026, 11, 17);
    const std::int64_t now = QDateTime(today, QTime(23, 0)).toMSecsSinceEpoch();
    REQUIRE(eod::archiveDay(db, archive, 10, QDate(2026, 6, 1), fakePdf("June"), now));
    REQUIRE(eod::archiveDay(db, archive, 11, QDate(2026, 11, 16), fakePdf("yesterday"), now));

    // Nowhere set: nothing moves, no warning.
    eod::MoveResult r = eod::moveOld(db, archive, QString(), u"Hat City"_s, 90, today, now);
    CHECK(r.moved == 0);
    CHECK(r.warning.isEmpty());

    // Not reachable (a NAS that is off): nothing moves, a warning.
    r = eod::moveOld(db, archive, u"/no/such/nas/share"_s, u"Hat City"_s, 90, today, now);
    CHECK(r.moved == 0);
    CHECK(r.warning.contains(u"can't be reached"_s));
    CHECK(eod::entries(db).size() == 2);
    // An empty folder on this same disk (an unmounted share): not reachable either.
    const QString empty = dir.filePath(u"empty-mount"_s);
    QDir().mkpath(empty);
    CHECK_FALSE(eod::longTermReachable(empty, archive));
    r = eod::moveOld(db, archive, empty, u"Hat City"_s, 90, today, now);
    CHECK(r.moved == 0);
    CHECK_FALSE(r.warning.isEmpty());

    // Reachable: June's goes (yesterday's stays), with a new record; June's own record is unchanged.
    const std::vector<eod::Entry> before = eod::entries(db);
    r = eod::moveOld(db, archive, nas, u"Hat City"_s, 90, today, now);
    CHECK(r.warning.isEmpty());
    CHECK(r.moved == 1);
    const std::vector<eod::Entry> all = eod::entries(db);
    REQUIRE(all.size() == 3);
    CHECK(all[0].recordHash == before[0].recordHash);
    CHECK(all[0].file == before[0].file);
    CHECK(all[2].kind == u"moved"_s);
    CHECK(all[2].ofSeq == all[0].seq);
    CHECK(all[2].pdfSha == all[0].pdfSha);
    const QString there = QDir(nas).filePath(u"Hat City/2026/2026-06-01 day 10.pdf"_s);
    CHECK(all[2].file == QFileInfo(there).absoluteFilePath());
    CHECK(QFile::exists(there));
    CHECK(modeOf(there) == 0444);
    CHECK_FALSE(QFile::exists(QDir(archive).filePath(all[0].file)));
    CHECK(QFile::exists(QDir(archive).filePath(all[1].file)));
    CHECK(eod::currentPath(all, all[0], archive) == all[2].file);
    eod::Check c = eod::verify(db, archive);
    CHECK(c.ok);
    CHECK(c.checked == 2);   // where they are now, the NAS's too

    // Moving again: nothing more.
    CHECK(eod::moveOld(db, archive, nas, u"Hat City"_s, 90, today, now).moved == 0);

    // The NAS changed under it: shows.
    QFile::setPermissions(there, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    {
        QFile f(there);
        REQUIRE(f.open(QIODevice::Append));
        f.write("x");
    }
    c = eod::verify(db, archive);
    CHECK_FALSE(c.ok);
    CHECK(c.problem == u"Day 2026-06-01: the PDF has changed since it was archived."_s);

    // The NAS gone (unmounted): the day is "not reachable now", not a broken archive.
    REQUIRE(QDir(nas).rename(u"Hat City"_s, u"away"_s));
    c = eod::verify(db, archive);
    CHECK(c.ok);
    CHECK(c.unreachable == 1);
}

TEST_CASE("The archive screen: long-term days, a page drawn for any screen, managers only", "[eodarchive][screen]")
{
    ArchivedPos t;
    t.closeDay();
    const QVariantMap screen = t.pos.eodArchive();
    REQUIRE(screen.value(u"days"_s).toList().size() == 1);
    const qint64 seq = screen.value(u"days"_s).toList().front().toMap().value(u"seq"_s).toLongLong();

    // A page, a part at a time, as a tablet fetches it (drawn by poppler's pdftoppm).
    if (!QStandardPaths::findExecutable(u"pdftoppm"_s).isEmpty()) {
        QSignalSpy loaded(&t.pos, &app::PosSession::archivePageLoaded);
        t.pos.loadArchivePage(seq, 1);
        REQUIRE(QTest::qWaitFor([&] { return loaded.size() > 0; }, 20000));
        const QList<QVariant> args = loaded.front();
        CHECK(args.at(4).toString().isEmpty());
        CHECK(args.at(2).toInt() >= 1);
        const QImage page(QUrl(args.at(3).toString()).toLocalFile());
        CHECK(!page.isNull());
        CHECK(page.height() > page.width());   // a letter page
    } else {
        const QVariantMap part = t.pos.eodArchivePagePart(seq, 1, 0);
        CHECK(part.value(u"error"_s).toString().contains(u"poppler-utils"_s));
    }

    // The long-term folder set but not reachable: the screen says so.
    t.pos.shared()->settings.longTermArchiveDir = "/no/such/nas";
    CHECK_FALSE(t.pos.eodArchive().value(u"longTerm"_s).toMap().value(u"reachable"_s).toBool());

    // Managers only.
    t.pos.logout();
    CHECK(t.pos.eodArchive().value(u"days"_s).toList().isEmpty());
    REQUIRE(t.pos.loginWithPin(u"1111"_s));   // a server
    CHECK(t.pos.eodArchive().value(u"days"_s).toList().isEmpty());
    CHECK(t.pos.eodArchivePagePart(seq, 1, 0).value(u"data"_s).toString().isEmpty());
    CHECK_FALSE(t.pos.printEodArchive(seq));
}

TEST_CASE("vt-eod-verify checks the archive the same way from the shell", "[eodarchive][script]")
{
    const QString python = QStandardPaths::findExecutable(u"python3"_s);
    if (python.isEmpty())
        SKIP("no python3 here");
    QTemporaryDir dir;
    const QString db = dir.filePath(u"viewtouch.db"_s), archive = dir.filePath(u"eod-archive"_s);
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    REQUIRE(eod::archiveDay(db, archive, 1, QDate(2026, 11, 13), fakePdf("one"), now));
    REQUIRE(eod::archiveDay(db, archive, 2, QDate(2026, 11, 14), fakePdf("two"), now));
    REQUIRE(eod::archiveDay(db, archive, 2, QDate(2026, 11, 14), fakePdf("two again"), now));   // re-archived
    const auto run = [&](QString *out) {
        QProcess p;
        p.start(python, {QStringLiteral(VTM_SCRIPTS_DIR "/vt-eod-verify"), u"--db"_s, db});
        REQUIRE(p.waitForFinished(30000));
        *out = QString::fromUtf8(p.readAllStandardOutput());
        return p.exitCode();
    };
    QString out;
    CHECK(run(&out) == 0);
    CHECK(out.contains(u"3 day(s) recorded, 3 PDF(s) checked"_s));

    const QString path = QDir(archive).filePath(eod::entries(db)[1].file);
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::Append));
        f.write("x");
    }
    CHECK(run(&out) == 1);
    const QString problem = eod::verify(db, archive).problem;
    CHECK(problem == u"Day 2026-11-14: the PDF has changed since it was archived."_s);
    CHECK(out.contains(u"PROBLEM: "_s + problem));
}
