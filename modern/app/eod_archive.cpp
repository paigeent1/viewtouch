#include "app/eod_archive.hh"

#include <QAtomicInt>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryDir>
#if QT_CONFIG(process)
#include <QProcess>
#endif

#include <algorithm>
#include <map>

using namespace Qt::StringLiterals;

namespace vt::app::eod {

namespace {

// A connection of its own (any thread), closed when it goes.
class Db {
public:
    explicit Db(const QString &path, bool readOnly)
    {
        static QAtomicInt counter;
        name_ = u"vt-eod-%1"_s.arg(counter.fetchAndAddRelaxed(1));
        QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, name_);
        db.setDatabaseName(path);
        db.setConnectOptions(readOnly ? u"QSQLITE_BUSY_TIMEOUT=5000;QSQLITE_OPEN_READONLY"_s : u"QSQLITE_BUSY_TIMEOUT=5000"_s);
        ok_ = (readOnly ? QFileInfo::exists(path) : true) && db.open();
        if (!ok_)
            error_ = db.lastError().text();
    }
    ~Db()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(name_, false);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(name_);
    }
    Db(const Db &) = delete;
    Db &operator=(const Db &) = delete;
    bool ok() const { return ok_; }
    QString error() const { return error_; }
    QSqlDatabase db() const { return QSqlDatabase::database(name_, false); }

private:
    QString name_;
    bool ok_ = false;
    QString error_;
};

bool createTable(QSqlQuery &q, QString *error)
{
    for (const char *sql : {
             "CREATE TABLE IF NOT EXISTS eod_archive (seq INTEGER PRIMARY KEY, at INTEGER NOT NULL, "
             "day_id INTEGER NOT NULL, day_date TEXT NOT NULL, kind TEXT NOT NULL, of_seq INTEGER NOT NULL DEFAULT 0, "
             "file TEXT NOT NULL, size INTEGER NOT NULL, pdf_sha256 TEXT NOT NULL, prev_hash TEXT NOT NULL, "
             "record_hash TEXT NOT NULL)",
             // Append-only: a record is never changed or removed (by ViewTouch or anything using SQLite normally).
             "CREATE TRIGGER IF NOT EXISTS eod_archive_no_update BEFORE UPDATE ON eod_archive "
             "BEGIN SELECT RAISE(ABORT, 'the End of Day archive is append-only'); END",
             "CREATE TRIGGER IF NOT EXISTS eod_archive_no_delete BEFORE DELETE ON eod_archive "
             "BEGIN SELECT RAISE(ABORT, 'the End of Day archive is append-only'); END"}) {
        if (!q.exec(QString::fromLatin1(sql))) {
            if (error)
                *error = q.lastError().text();
            return false;
        }
    }
    return true;
}

std::vector<Entry> readAll(QSqlDatabase db, QString *error)
{
    std::vector<Entry> out;
    QSqlQuery q(db);
    if (!q.exec(u"SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'eod_archive'"_s) || !q.next())
        return out;
    if (!q.exec(u"SELECT seq, at, day_id, day_date, kind, of_seq, file, size, pdf_sha256, prev_hash, record_hash "
                "FROM eod_archive ORDER BY seq"_s)) {
        if (error)
            *error = q.lastError().text();
        return out;
    }
    while (q.next()) {
        Entry e;
        e.seq = q.value(0).toLongLong();
        e.at = q.value(1).toLongLong();
        e.dayId = q.value(2).toLongLong();
        e.date = q.value(3).toString();
        e.kind = q.value(4).toString();
        e.ofSeq = q.value(5).toLongLong();
        e.file = q.value(6).toString();
        e.size = q.value(7).toLongLong();
        e.pdfSha = q.value(8).toString();
        e.prevHash = q.value(9).toString();
        e.recordHash = q.value(10).toString();
        out.push_back(std::move(e));
    }
    return out;
}

bool insert(QSqlDatabase db, const Entry &e, QString *error)
{
    QSqlQuery q(db);
    q.prepare(u"INSERT INTO eod_archive (seq, at, day_id, day_date, kind, of_seq, file, size, pdf_sha256, prev_hash, "
              "record_hash) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"_s);
    for (const QVariant &v : {QVariant(qint64(e.seq)), QVariant(qint64(e.at)), QVariant(qint64(e.dayId)), QVariant(e.date),
                              QVariant(e.kind), QVariant(qint64(e.ofSeq)), QVariant(e.file), QVariant(qint64(e.size)),
                              QVariant(e.pdfSha), QVariant(e.prevHash), QVariant(e.recordHash)})
        q.addBindValue(v);
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    return true;
}

// The next record in the chain, filled in and hashed (inside the caller's transaction).
Entry chained(const std::vector<Entry> &all, Entry e)
{
    e.seq = all.empty() ? 1 : all.back().seq + 1;
    e.prevHash = all.empty() ? QString::fromLatin1(kGenesis) : all.back().recordHash;
    e.recordHash = recordHash(e);
    return e;
}

void readOnly(const QString &path)
{
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
}

// A name not taken yet: "x.pdf", "x (2).pdf"...
QString freeName(const QString &path)
{
    if (!QFileInfo::exists(path))
        return path;
    const QFileInfo f(path);
    for (int n = 2;; ++n) {
        const QString p = f.dir().filePath(u"%1 (%2).%3"_s.arg(f.completeBaseName()).arg(n).arg(f.suffix()));
        if (!QFileInfo::exists(p))
            return p;
    }
}

bool isLocal(const Entry &e) { return e.kind != u"moved"_s; }

} // namespace

QByteArray recordText(const Entry &e)
{
    return (e.prevHash + u'\n'
            + QStringList{QString::number(e.seq), QString::number(e.at), QString::number(e.dayId), e.date, e.kind,
                          QString::number(e.ofSeq), e.file, QString::number(e.size), e.pdfSha}
                  .join(u'|'))
        .toUtf8();
}

QString recordHash(const Entry &e)
{
    return QString::fromLatin1(QCryptographicHash::hash(recordText(e), QCryptographicHash::Sha256).toHex());
}

QString sha256File(const QString &path, std::int64_t *size, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    if (size)
        *size = f.size();
    return QString::fromLatin1(h.result().toHex());
}

bool ensureTable(const QString &dbPath, QString *error)
{
    Db d(dbPath, false);
    if (!d.ok()) {
        if (error)
            *error = d.error();
        return false;
    }
    QSqlQuery q(d.db());
    return createTable(q, error);
}

std::vector<Entry> entries(const QString &dbPath, QString *error)
{
    Db d(dbPath, true);
    if (!d.ok())
        return {};
    return readAll(d.db(), error);
}

std::optional<Entry> archiveDay(const QString &dbPath, const QString &archiveDir, std::int64_t dayId, QDate date,
                                const QByteArray &pdf, std::int64_t now, QString *error)
{
    const auto failed = [error](const QString &why) {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (pdf.isEmpty())
        return failed(u"no PDF was made"_s);
    if (dbPath.isEmpty() || archiveDir.isEmpty())
        return failed(u"no archive folder"_s);
    Db d(dbPath, false);
    if (!d.ok())
        return failed(d.error());
    QSqlDatabase db = d.db();
    QSqlQuery q(db);
    QString why;
    if (!createTable(q, &why))
        return failed(why);
    // One at a time: nobody else adds a record between reading the last and adding this one.
    if (!q.exec(u"BEGIN IMMEDIATE"_s))
        return failed(q.lastError().text());
    const auto rollback = [&q] { q.exec(u"ROLLBACK"_s); };
    const std::vector<Entry> all = readAll(db, &why);
    if (!why.isEmpty()) {
        rollback();
        return failed(why);
    }
    const QString day = date.toString(u"yyyy-MM-dd"_s);
    const auto before = std::ranges::count_if(all, [&](const Entry &e) {
        return isLocal(e) && e.dayId == dayId && e.date == day;
    });
    QString name = u"%1 day %2"_s.arg(day).arg(dayId);
    if (before > 0)
        name += u" (re-archived %1)"_s.arg(before + 1);
    const QString year = QString::number(date.year());
    if (!QDir().mkpath(QDir(archiveDir).filePath(year))) {
        rollback();
        return failed(u"cannot make %1"_s.arg(QDir(archiveDir).filePath(year)));
    }
    // Never over a file that is there (whatever it is): a new name instead.
    const QString path = freeName(QDir(archiveDir).filePath(year + u'/' + name + u".pdf"_s));
    const QString part = path + u".part"_s;
    {
        QFile::remove(part);
        QFile f(part);
        if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly) || f.write(pdf) != pdf.size() || !f.flush()) {
            const QString e = f.errorString();
            f.close();
            QFile::remove(part);
            rollback();
            return failed(e);
        }
        f.close();
    }
    if (!QFile::rename(part, path)) {   // (rename never replaces a file)
        QFile::remove(part);
        rollback();
        return failed(u"cannot write %1"_s.arg(path));
    }
    readOnly(path);

    Entry e;
    e.at = now;
    e.dayId = dayId;
    e.date = day;
    e.kind = before > 0 ? u"re-archived"_s : u"archived"_s;
    e.file = QDir(archiveDir).relativeFilePath(path);
    e.size = pdf.size();
    e.pdfSha = QString::fromLatin1(QCryptographicHash::hash(pdf, QCryptographicHash::Sha256).toHex());
    e = chained(all, e);
    if (!insert(db, e, &why) || !q.exec(u"COMMIT"_s)) {
        if (why.isEmpty())
            why = q.lastError().text();
        rollback();
        // Not recorded: the file just written goes (it would only be a PDF without a record).
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QFile::remove(path);
        return failed(why);
    }
    return e;
}

QString currentPath(const std::vector<Entry> &all, const Entry &e, const QString &archiveDir)
{
    for (auto it = all.rbegin(); it != all.rend(); ++it)
        if (it->kind == u"moved"_s && it->ofSeq == e.seq)
            return it->file;
    return QDir::isAbsolutePath(e.file) ? e.file : QDir(archiveDir).filePath(e.file);
}

Check verify(const QString &dbPath, const QString &archiveDir)
{
    Check c;
    const auto problem = [&c](const QString &text) {
        if (c.ok) {
            c.ok = false;
            c.problem = text;
        }
    };
    QString error;
    const std::vector<Entry> all = entries(dbPath, &error);
    if (!error.isEmpty())
        problem(QCoreApplication::translate("PosService", "The archive's records can't be read: %1").arg(error));
    std::map<std::int64_t, const Entry *> bySeq;
    QString prev = QString::fromLatin1(kGenesis);
    std::int64_t expect = 1;
    for (const Entry &e : all) {
        bySeq[e.seq] = &e;
        if (e.seq != expect)
            problem(QCoreApplication::translate("PosService", "Record %1 is missing from the archive (a record was removed): the chain is broken there.").arg(expect));
        expect = e.seq + 1;
        if (e.prevHash != prev)
            problem(QCoreApplication::translate("PosService", "Day %1: the archive's chain is broken here (a record before it was changed or removed).").arg(e.date));
        if (recordHash(e) != e.recordHash)
            problem(QCoreApplication::translate("PosService", "Day %1: its archive record was changed since it was written.").arg(e.date));
        prev = e.recordHash;
        if (e.kind == u"moved"_s) {
            const auto of = bySeq.find(e.ofSeq);
            if (of == bySeq.end() || of->second->pdfSha != e.pdfSha)
                problem(QCoreApplication::translate("PosService", "Day %1: its long-term archive record doesn't match the day's own record.").arg(e.date));
        }
    }
    // Each day's PDF where it is now.
    QSet<QString> known;
    for (const Entry &e : all) {
        if (!isLocal(e))
            continue;
        ++c.days;
        known << QDir::cleanPath(QDir(archiveDir).filePath(e.file));
        const QString path = currentPath(all, e, archiveDir);
        DayStatus s{e.seq, u"ok"_s, path};
        if (!QFileInfo::exists(path)) {
            // In the long-term folder: its store folder not there means the folder isn't reachable (a NAS
            // that is off or not mounted); otherwise the file is missing.
            const bool away = path != QDir(archiveDir).filePath(e.file);
            if (away && !QFileInfo(QFileInfo(QFileInfo(path).absolutePath()).absolutePath()).isDir()) {
                s.state = u"unreachable"_s;
                ++c.unreachable;
            } else {
                s.state = u"missing"_s;
                problem(QCoreApplication::translate("PosService", "Day %1: the PDF is missing (%2).").arg(e.date, path));
            }
        } else {
            std::int64_t size = 0;
            const QString sha = sha256File(path, &size);
            ++c.checked;
            if (sha != e.pdfSha || size != e.size) {
                s.state = u"changed"_s;
                problem(QCoreApplication::translate("PosService", "Day %1: the PDF has changed since it was archived.").arg(e.date));
            }
        }
        c.status.push_back(s);
    }
    // A PDF in the folder with no record: its record was removed.
    if (!archiveDir.isEmpty() && QFileInfo(archiveDir).isDir()) {
        QStringList orphans;
        QDirIterator it(archiveDir, {u"*.pdf"_s}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = QDir::cleanPath(it.next());
            if (!known.contains(f))
                orphans << QDir(archiveDir).relativeFilePath(f);
        }
        orphans.sort();
        if (!orphans.isEmpty())
            problem(QCoreApplication::translate("PosService", "%1 is in the archive folder but has no record (its record was removed).").arg(orphans.front()));
    }
    return c;
}

QString folderName(const QString &storeName)
{
    static const QRegularExpression bad(u"[^A-Za-z0-9 ._-]+"_s);
    QString s = storeName;
    s.replace(bad, u"-"_s);
    s = s.trimmed();
    while (s.startsWith(u'.'))
        s.remove(0, 1);
    return s.isEmpty() ? u"Store"_s : s;
}

bool longTermReachable(const QString &longTermDir, const QString &archiveDir)
{
    const QFileInfo f(longTermDir);
    if (longTermDir.isEmpty() || !QDir::isAbsolutePath(longTermDir) || !f.isDir() || !f.isWritable())
        return false;
    const QDir dir(longTermDir);
    if (!dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
        return true;
    // Empty: only when it is a drive of its own (an unmounted share is an empty folder here).
    const QStorageInfo there(longTermDir), here(archiveDir.isEmpty() ? QDir::homePath() : archiveDir);
    return there.isValid() && (there.rootPath() != here.rootPath() || there.device() != here.device());
}

MoveResult moveOld(const QString &dbPath, const QString &archiveDir, const QString &longTermDir,
                   const QString &storeName, int days, QDate today, std::int64_t now)
{
    MoveResult r;
    if (longTermDir.trimmed().isEmpty())
        return r;
    QString error;
    std::vector<Entry> all = entries(dbPath, &error);
    const QDate cutoff = today.addDays(-std::max(0, days));
    std::vector<Entry> due;
    for (const Entry &e : all)
        if (isLocal(e) && QDate::fromString(e.date, u"yyyy-MM-dd"_s) < cutoff
            && currentPath(all, e, archiveDir) == QDir(archiveDir).filePath(e.file))
            due.push_back(e);
    if (due.empty())
        return r;
    if (!longTermReachable(longTermDir, archiveDir)) {
        r.warning = QCoreApplication::translate("PosService", "The long-term archive folder (%1) can't be reached: the PDFs stay here for now.").arg(longTermDir);
        return r;
    }
    const QString store = QDir(longTermDir).filePath(folderName(storeName));
    for (const Entry &e : due) {
        const QString local = QDir(archiveDir).filePath(e.file);
        // The file here must still be what was archived: a changed one isn't passed on as the record.
        if (sha256File(local) != e.pdfSha) {
            r.warning = QCoreApplication::translate("PosService", "Day %1 was not moved to the long-term archive: its PDF here doesn't match its record.").arg(e.date);
            continue;
        }
        const QString yearDir = QDir(store).filePath(e.date.left(4));
        if (!QDir().mkpath(yearDir)) {
            r.warning = QCoreApplication::translate("PosService", "The long-term archive folder (%1) can't be written to: the PDFs stay here for now.").arg(longTermDir);
            break;
        }
        QString target = QDir(yearDir).filePath(QFileInfo(local).fileName());
        if (QFileInfo::exists(target) && sha256File(target) != e.pdfSha)
            target = freeName(target);
        if (!QFileInfo::exists(target)) {
            const QString part = target + u".part"_s;
            QFile::remove(part);
            if (!QFile::copy(local, part) || !QFile::rename(part, target)) {
                QFile::remove(part);
                r.warning = QCoreApplication::translate("PosService", "Day %1 couldn't be copied to the long-term archive: it stays here.").arg(e.date);
                continue;
            }
        }
        // The copy, read back, must match the chain before the file here goes.
        std::int64_t size = 0;
        if (sha256File(target, &size) != e.pdfSha) {
            r.warning = QCoreApplication::translate("PosService", "Day %1: the long-term copy didn't match its record, so it stays here.").arg(e.date);
            continue;
        }
        readOnly(target);
        // A new record says where it is now (the day's own record stays as it was).
        Db d(dbPath, false);
        QString why;
        bool ok = d.ok();
        if (ok) {
            QSqlQuery q(d.db());
            ok = createTable(q, &why) && q.exec(u"BEGIN IMMEDIATE"_s);
            if (ok) {
                const std::vector<Entry> now_ = readAll(d.db(), &why);
                Entry m;
                m.at = now;
                m.dayId = e.dayId;
                m.date = e.date;
                m.kind = u"moved"_s;
                m.ofSeq = e.seq;
                m.file = QFileInfo(target).absoluteFilePath();
                m.size = size;
                m.pdfSha = e.pdfSha;
                m = chained(now_, m);
                ok = insert(d.db(), m, &why) && q.exec(u"COMMIT"_s);
                if (!ok)
                    q.exec(u"ROLLBACK"_s);
            }
        }
        if (!ok) {
            r.warning = QCoreApplication::translate("PosService", "Day %1 couldn't be recorded as moved: it stays here.").arg(e.date);
            continue;
        }
        QFile::remove(local);
        ++r.moved;
    }
    return r;
}

int pdfPageCount(const QByteArray &pdf)
{
    static const QRegularExpression page(u"/Type\\s*/Page(?![A-Za-z])"_s);
    int n = 0;
    auto it = page.globalMatch(QString::fromLatin1(pdf));
    while (it.hasNext()) {
        it.next();
        ++n;
    }
    return n;
}

QByteArray renderPage(const QString &pdfPath, int page, int dpi, QString *error)
{
#if QT_CONFIG(process)
    const QString tool = QStandardPaths::findExecutable(u"pdftoppm"_s);
    if (tool.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("PosService", "The store's computer can't show PDFs yet: install poppler-utils on it (vt-install does).");
        return {};
    }
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        if (error)
            *error = tmp.errorString();
        return {};
    }
    QProcess p;
    p.start(tool, {u"-f"_s, QString::number(page), u"-l"_s, QString::number(page), u"-r"_s, QString::number(dpi),
                   u"-jpeg"_s, u"-singlefile"_s, pdfPath, tmp.filePath(u"page"_s)});
    if (!p.waitForFinished(30000) || p.exitCode() != 0) {
        if (error)
            *error = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        p.kill();
        return {};
    }
    QFile f(tmp.filePath(u"page.jpg"_s));
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    return f.readAll();
#else
    Q_UNUSED(pdfPath)
    Q_UNUSED(page)
    Q_UNUSED(dpi)
    if (error)
        *error = QCoreApplication::translate("PosService", "The store's computer can't show PDFs yet: install poppler-utils on it (vt-install does).");
    return {};
#endif
}

} // namespace vt::app::eod
