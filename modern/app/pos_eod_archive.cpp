// PosService: the End of Day archive (app/eod_archive.hh). At End of Day the day's summary PDF goes
// into <data>/eod-archive/<year>/, read-only, with a record in the database's hash chain; old days
// move to the long-term folder (Store Settings > Backups) when one is set; the whole archive is
// checked. Manager > Reports > End of Day archive lists the days and shows a day's PDF, page by
// page, drawn on the store's computer (tablets fetch the pages a part at a time).

#include "app/eod_archive.hh"
#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>
#include <QThreadPool>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

constexpr qint64 kPagePart = 256 * 1024;   // a page's JPEG goes to a tablet this much at a time
constexpr int kPageDpi = 110;

QVariantMap checkVariant(const eod::Check &c, std::int64_t at)
{
    QVariantMap status;
    for (const eod::DayStatus &s : c.status)
        status.insert(QString::number(s.seq), s.state);
    return {{u"ok"_s, c.ok}, {u"problem"_s, c.problem}, {u"days"_s, c.days}, {u"checked"_s, c.checked},
            {u"unreachable"_s, c.unreachable}, {u"at"_s, qint64(at)}, {u"status"_s, status}};
}

QString pagesDir(const PosShared *s)
{
    if (!s->archivePagesDir.isEmpty())
        return s->archivePagesDir;
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + u"/eod-archive-pages"_s;
}

} // namespace

void PosService::archiveEndOfDay()
{
    if (s_->archiveDir.isEmpty() || s_->archiveDb.isEmpty())
        return;
    QVariantMap news{{u"at"_s, qint64(now())}, {u"dayId"_s, qint64(s_->day.id)}};
    const QDate date = QDateTime::fromMSecsSinceEpoch(s_->day.openedAt ? s_->day.openedAt : now()).date();
    std::vector<Report> summary;
    for (const QString &id : {u"sales"_s, u"deposit"_s, u"drawer"_s, u"servers"_s, u"tips"_s, u"labor"_s,
                              u"categories"_s, u"items"_s, u"hourly"_s, u"expenses"_s, u"exceptions"_s})
        summary.push_back(buildReport(id));
    const QByteArray pdf = s_->printer ? s_->printer->endOfDayPdf(s_->settings, summary, exceptionTickets(), s_->day.id,
                                                                  s_->day.openedAt)
                                       : QByteArray();
    QString error;
    const auto kept = pdf.isEmpty() ? std::nullopt
                                    : eod::archiveDay(s_->archiveDb, s_->archiveDir, s_->day.id, date, pdf, now(), &error);
    if (kept) {
        news.insert(u"file"_s, kept->file);
        news.insert(u"kind"_s, kept->kind);
    } else {
        news.insert(u"error"_s, pdf.isEmpty() ? tr("The End of Day PDF couldn't be made.") : error);
        s_->noteProblem("eodArchive", {}, ss(news.value(u"error"_s).toString()));
        emit notice(tr("The day was closed, but its PDF was not archived: %1").arg(news.value(u"error"_s).toString()));
    }
    s_->archiveNews = news;

    // Away from the screen: older days to the long-term folder, then every day checked.
    const QString db = s_->archiveDb, dir = s_->archiveDir;
    const QString longTerm = qs(s_->settings.longTermArchiveDir);
    const QString store = qs(s_->settings.storeName);
    const int days = s_->settings.longTermArchiveDays;
    const QDate today = QDateTime::fromMSecsSinceEpoch(now()).date();
    const std::int64_t at = now();
    s_->archiveChecking = true;
    QPointer<PosService> self(this);
    QThreadPool::globalInstance()->start([=] {
        const eod::MoveResult moved = eod::moveOld(db, dir, longTerm, store, days, today, at);
        const eod::Check check = eod::verify(db, dir);
        QMetaObject::invokeMethod(self, [self, moved, check, at] {
            if (!self)
                return;
            PosShared *s = self->s_;
            s->archiveChecking = false;
            s->archiveCheck = checkVariant(check, at);
            s->archiveNews.insert(u"moved"_s, moved.moved);
            s->archiveNews.insert(u"warning"_s, moved.warning);
            s->archiveNews.insert(u"checked"_s, true);
            if (!moved.warning.isEmpty()) {
                s->noteProblem("eodArchiveMove", {}, ss(moved.warning));
                emit self->notice(tr("End of Day archive: %1").arg(moved.warning));
            }
            if (!check.ok) {
                s->noteProblem("eodArchive", {}, ss(check.problem));
                emit self->notice(tr("End of Day archive check: %1").arg(check.problem));
            } else {
                emit self->notice(tr("End of Day archive: the day's PDF is kept, and all %n archived day(s) check out.", nullptr,
                                     check.days)
                                  + (moved.moved > 0 ? u' ' + tr("%n moved to the long-term folder.", nullptr, moved.moved) : QString()));
            }
            emit s->dayChanged();
        }, Qt::QueuedConnection);
    });
}

QVariantMap PosService::eodArchive()
{
    if (!require(perm::Manager, tr("The End of Day archive")))
        return {{u"error"_s, tr("Managers only.")}};
    if (s_->archiveDb.isEmpty())
        return {{u"error"_s, tr("This computer keeps no End of Day archive (it isn't the store's server).")}};
    QString error;
    const std::vector<eod::Entry> all = eod::entries(s_->archiveDb, &error);
    const QVariantMap status = s_->archiveCheck.value(u"status"_s).toMap();
    QVariantList days;
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        const eod::Entry &e = *it;
        if (e.kind == u"moved"_s)
            continue;
        const QString path = eod::currentPath(all, e, s_->archiveDir);
        const bool away = QDir::isAbsolutePath(path) && path != QDir(s_->archiveDir).filePath(e.file);
        days << QVariantMap{{u"seq"_s, qint64(e.seq)}, {u"dayId"_s, qint64(e.dayId)}, {u"date"_s, e.date},
                            {u"kind"_s, e.kind}, {u"size"_s, qint64(e.size)}, {u"sha"_s, e.pdfSha},
                            {u"place"_s, away ? u"longTerm"_s : u"here"_s},
                            {u"reachable"_s, QFileInfo::exists(path)},
                            {u"file"_s, QFileInfo(path).fileName()},
                            {u"state"_s, status.value(QString::number(e.seq)).toString()},
                            {u"at"_s, qint64(e.at)}};
    }
    const QString longTerm = qs(s_->settings.longTermArchiveDir);
    return {{u"days"_s, days}, {u"error"_s, error}, {u"check"_s, s_->archiveCheck},
            {u"checking"_s, s_->archiveChecking}, {u"news"_s, s_->archiveNews},
            {u"longTerm"_s, QVariantMap{{u"folder"_s, longTerm}, {u"days"_s, s_->settings.longTermArchiveDays},
                                        {u"reachable"_s, !longTerm.isEmpty() && eod::longTermReachable(longTerm, s_->archiveDir)}}}};
}

QVariantMap PosService::checkEodArchiveNow()
{
    if (!require(perm::Manager, tr("Checking the End of Day archive")))
        return {{u"error"_s, tr("Managers only.")}};
    s_->archiveCheck = checkVariant(eod::verify(s_->archiveDb, s_->archiveDir), now());
    return s_->archiveCheck;
}

bool PosService::checkEodArchive()
{
    if (!require(perm::Manager, tr("Checking the End of Day archive")))
        return false;
    if (s_->archiveDb.isEmpty() || s_->archiveChecking)
        return true;
    s_->archiveChecking = true;
    const QString db = s_->archiveDb, dir = s_->archiveDir;
    const std::int64_t at = now();
    QPointer<PosService> self(this);
    QThreadPool::globalInstance()->start([=] {
        const eod::Check check = eod::verify(db, dir);
        QMetaObject::invokeMethod(self, [self, check, at] {
            if (!self)
                return;
            self->s_->archiveChecking = false;
            self->s_->archiveCheck = checkVariant(check, at);
            emit self->s_->dayChanged();
        }, Qt::QueuedConnection);
    });
    return true;
}

QVariantMap PosService::eodArchivePagePart(qint64 seq, int page, qint64 at)
{
    QVariantMap out{{u"seq"_s, seq}, {u"page"_s, page}, {u"at"_s, at}};
    if (!require(perm::Manager, tr("The End of Day archive"))) {
        out.insert(u"error"_s, tr("Managers only."));
        return out;
    }
    const std::vector<eod::Entry> all = eod::entries(s_->archiveDb);
    const auto e = std::ranges::find_if(all, [&](const eod::Entry &x) { return x.seq == seq && x.kind != u"moved"_s; });
    if (e == all.end()) {
        out.insert(u"error"_s, tr("No such archived day."));
        return out;
    }
    const QString path = eod::currentPath(all, *e, s_->archiveDir);
    if (!QFileInfo::exists(path)) {
        out.insert(u"error"_s, path != QDir(s_->archiveDir).filePath(e->file)
                                   ? tr("In the long-term archive, which is not reachable now.")
                                   : tr("The PDF is missing."));
        return out;
    }
    // Drawn once per page (an archived PDF never changes; a changed one shows on the check).
    const QString dir = pagesDir(s_);
    QDir().mkpath(dir);
    const QString count = QDir(dir).filePath(u"%1-%2.pages"_s.arg(seq).arg(e->pdfSha.left(16)));
    int pages = 0;
    {
        QFile f(count);
        if (f.open(QIODevice::ReadOnly))
            pages = f.readAll().trimmed().toInt();
    }
    if (pages <= 0) {
        QFile pdf(path);
        if (pdf.open(QIODevice::ReadOnly))
            pages = std::max(1, eod::pdfPageCount(pdf.readAll()));
        QFile f(count);
        if (f.open(QIODevice::WriteOnly))
            f.write(QByteArray::number(pages));
    }
    out.insert(u"pages"_s, pages);
    page = std::clamp(page, 1, pages);
    out.insert(u"page"_s, page);
    const QString jpeg = QDir(dir).filePath(u"%1-%2-%3.jpg"_s.arg(seq).arg(e->pdfSha.left(16)).arg(page));
    if (!QFileInfo::exists(jpeg)) {
        QString error;
        const QByteArray drawn = eod::renderPage(path, page, kPageDpi, &error);
        QFile f(jpeg);
        if (drawn.isEmpty() || !f.open(QIODevice::WriteOnly) || f.write(drawn) != drawn.size()) {
            f.remove();
            out.insert(u"error"_s, error.isEmpty() ? tr("The page couldn't be drawn.") : error);
            return out;
        }
    }
    QFile f(jpeg);
    if (!f.open(QIODevice::ReadOnly) || at < 0 || at > f.size() || !f.seek(at)) {
        out.insert(u"error"_s, tr("The page couldn't be read."));
        return out;
    }
    out.insert(u"size"_s, f.size());
    out.insert(u"data"_s, QString::fromLatin1(f.read(kPagePart).toBase64()));
    return out;
}

bool PosService::printEodArchive(qint64 seq)
{
    if (!require(perm::Manager, tr("Printing from the End of Day archive")))
        return false;
    if (!s_->printer)
        return fail(tr("No printer is set up."));
    const std::vector<eod::Entry> all = eod::entries(s_->archiveDb);
    const auto e = std::ranges::find_if(all, [&](const eod::Entry &x) { return x.seq == seq && x.kind != u"moved"_s; });
    if (e == all.end())
        return fail(tr("No such archived day."));
    const QString path = eod::currentPath(all, *e, s_->archiveDir);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(tr("The PDF can't be read now: %1").arg(f.errorString()));
    const QByteArray pdf = f.readAll();
    const QString title = tr("End of Day %1 (day %2)").arg(e->date).arg(e->dayId);
    // A full-page printer when the store has one; else the pages, small, on the receipt printer.
    for (const PrinterConfig &p : s_->settings.printers)
        if (p.effectiveFormat() == "pdf") {
            s_->printer->printPdf(s_->settings, pdf, title, p.id);
            emit notice(tr("Printing %1 on %2").arg(title, qs(p.name)));
            return true;
        }
    std::vector<QByteArray> pages;
    const int n = std::max(1, eod::pdfPageCount(pdf));
    QString error;
    for (int i = 1; i <= n; ++i) {
        const QByteArray jpeg = eod::renderPage(path, i, 100, &error);
        if (jpeg.isEmpty())
            return fail(error);
        pages.push_back(jpeg);
    }
    s_->printer->printPictures(s_->settings, pages, title, receiptPrinter());
    emit notice(tr("Printing %1").arg(title));
    return true;
}

} // namespace vt::app
