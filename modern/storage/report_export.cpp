#include "storage/report_export.hh"

#include "app/pos_json.hh"
#include "app/pos_service.hh"
#include "storage/backup.hh"
#include "storage/pos_store.hh"

#include <QAtomicInt>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimeZone>

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>

using namespace Qt::StringLiterals;

namespace vt::storage {

namespace {

constexpr int kDayStartsAtHour = 6;   // a business day closed before 6 AM counts for the day before
const QString kDot = u"••••"_s;   // ••••

QString nextConnectionName()
{
    static QAtomicInt counter;
    return u"vt-report-export-%1"_s.arg(counter.fetchAndAddRelaxed(1));
}

// A consistent copy of the live database at `target`, the live one opened read only (VACUUM
// INTO leaves its source as it was). If SQLite can't open it read only here, the way --backup
// copies it (its own connection, also VACUUM INTO).
bool snapshot(const QString &db, const QString &target, QString *error)
{
    if (!QFileInfo(db).isFile()) {
        *error = u"There is no database at %1."_s.arg(db);
        return false;
    }
    const QString name = nextConnectionName();
    bool ok = false;
    {
        QSqlDatabase c = QSqlDatabase::addDatabase(u"QSQLITE"_s, name);
        c.setDatabaseName(db);
        c.setConnectOptions(u"QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"_s);
        if (c.open()) {
            QSqlQuery q(c);
            q.prepare(u"VACUUM INTO ?"_s);
            q.addBindValue(target);
            ok = q.exec();
            q.finish();
            c.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    if (ok)
        return true;
    QFile::remove(target);
    return backupDatabase(db, target, error);
}

std::string lower(std::string s)
{
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

QString digitsOf(const QString &s)
{
    QString d;
    for (QChar c : s)
        if (c.isDigit())
            d += c;
    return d;
}

// "6012345678901234" -> "••••1234"; short numbers show nothing.
std::string maskCard(const std::string &cell)
{
    const QString text = QString::fromStdString(cell).trimmed();
    if (text.isEmpty() || text == u"-" || text == u"----")
        return cell;
    const QString d = digitsOf(text);
    return (d.size() >= 8 ? kDot + d.right(4) : kDot).toStdString();
}

// Any 15 to 19 digits in a row (spaces or dashes between allowed): a card number.
std::string maskLongNumbers(const std::string &cell)
{
    static const QRegularExpression card(u"(?<!\\d)(?:\\d[ -]?){14,18}\\d(?!\\d)"_s);
    QString text = QString::fromStdString(cell);
    if (!text.contains(card))
        return cell;
    QString out;
    qsizetype at = 0;
    for (auto it = card.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(at, m.capturedStart() - at) + kDot + digitsOf(m.captured()).right(4);
        at = m.capturedEnd();
    }
    out += text.mid(at);
    return out.toStdString();
}

QJsonArray strings(const std::vector<std::string> &list)
{
    QJsonArray out;
    for (const std::string &s : list)
        out.append(QString::fromStdString(s));
    return out;
}

// Local time with its offset: "2026-11-09T23:05:00-05:00".
QString iso(std::int64_t ms)
{
    if (ms <= 0)
        return {};
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(ms);
    return t.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(t.offsetFromUtc())).toString(Qt::ISODate);
}

struct ClosedDay {
    std::int64_t id = 0, openedAt = 0, closedAt = 0;
    QJsonObject reports;
};

} // namespace

QDate businessDate(std::int64_t closedAt)
{
    return QDateTime::fromMSecsSinceEpoch(closedAt).addSecs(-kDayStartsAtHour * 3600).date();
}

core::Report maskPrivate(core::Report r)
{
    std::vector<std::size_t> pins, cards, ids;
    for (std::size_t i = 0; i < r.columns.size(); ++i) {
        const std::string c = lower(r.columns[i]);
        if (c == "pin")
            pins.push_back(i);
        else if (c.starts_with("card") && (c.find('#') != std::string::npos || c.find("number") != std::string::npos))
            cards.push_back(i);
        else if (c == "dob" || c.find("birth") != std::string::npos || c.find("license") != std::string::npos
                 || c == "id #" || c == "id number")
            ids.push_back(i);
    }
    for (core::ReportRow &row : r.rows) {
        if (row.kind == core::ReportRow::Kind::Line) {   // totals carry labels and counts there
            for (std::size_t i : pins)
                if (i < row.cells.size() && !row.cells[i].empty() && row.cells[i] != "----")
                    row.cells[i] = kDot.toStdString();
            for (std::size_t i : cards)
                if (i < row.cells.size())
                    row.cells[i] = maskCard(row.cells[i]);
            for (std::size_t i : ids)
                if (i < row.cells.size())
                    row.cells[i].clear();
        }
        for (std::string &cell : row.cells)
            cell = maskLongNumbers(cell);
    }
    return r;
}

QJsonObject reportFile(const QString &id, const core::Report &report, const QString &storeName,
                       const QString &checkPrefix, std::int64_t dayId, QDate date, std::int64_t openedAt,
                       std::int64_t closedAt, std::int64_t generatedAt)
{
    QJsonArray rows;
    for (const core::ReportRow &row : report.rows) {
        const char *kind = row.kind == core::ReportRow::Kind::Section ? "section"
                           : row.kind == core::ReportRow::Kind::Total ? "total"
                           : row.kind == core::ReportRow::Kind::Note  ? "note"
                                                                      : "line";
        rows.append(QJsonObject{{u"kind"_s, QString::fromLatin1(kind)}, {u"cells"_s, strings(row.cells)}});
    }
    return {
        {u"format"_s, u"viewtouch-report/1"_s},
        {u"report"_s, QJsonObject{{u"id"_s, id}, {u"title"_s, QString::fromStdString(report.title)},
                                  {u"subtitle"_s, QString::fromStdString(report.subtitle)},
                                  {u"columns"_s, strings(report.columns)}, {u"rows"_s, rows}}},
        {u"store"_s, QJsonObject{{u"name"_s, storeName}, {u"checkPrefix"_s, checkPrefix}}},
        {u"day"_s, QJsonObject{{u"id"_s, qint64(dayId)}, {u"date"_s, date.toString(u"yyyy-MM-dd"_s)},
                               {u"open"_s, closedAt == 0}, {u"openedAt"_s, iso(openedAt)},
                               {u"closedAt"_s, iso(closedAt)}, {u"openedAtMs"_s, qint64(openedAt)},
                               {u"closedAtMs"_s, qint64(closedAt)}}},
        {u"generatedAt"_s, iso(generatedAt)},
        {u"generatedAtMs"_s, qint64(generatedAt)},
    };
}

ReportExportResult exportReports(const ReportExportOptions &options)
{
    ReportExportResult result;
    const std::int64_t now = options.now > 0 ? options.now : QDateTime::currentMSecsSinceEpoch();
    QTemporaryDir work;
    if (!work.isValid()) {
        result.error = u"Cannot make a folder for the copy of the database."_s;
        return result;
    }
    const QString copy = work.filePath(u"reports.db"_s);
    if (!snapshot(options.database, copy, &result.error))
        return result;

    std::optional<app::PosData> data;
    {
        PosStore store(copy);
        if (!store.open(&result.error))
            return result;
        data = store.load();
    }
    if (!data) {
        result.error = u"Cannot read the database copy."_s;
        return result;
    }

    // The day still open: today's date, and after the last closed day. Then the closed days, from
    // the copy (any age, not only the 60 kept in memory).
    std::vector<ClosedDay> closed;
    QDate openDate = businessDate(now), from, to;
    {
        const QString name = nextConnectionName();
        {
            QSqlDatabase c = QSqlDatabase::addDatabase(u"QSQLITE"_s, name);
            c.setDatabaseName(copy);
            c.setConnectOptions(u"QSQLITE_OPEN_READONLY"_s);
            if (c.open()) {
                QSqlQuery q(c);
                if (q.exec(u"SELECT COALESCE(MAX(closed_at), 0) FROM business_days WHERE closed_at != 0"_s) && q.next()
                    && q.value(0).toLongLong() > 0)
                    openDate = std::max(openDate, businessDate(q.value(0).toLongLong()).addDays(1));
                to = options.to.isValid() ? options.to : openDate;
                from = options.from.isValid() ? options.from : to;
                if (to < from)
                    std::swap(from, to);
                // A margin each side; the business date decides.
                q.prepare(u"SELECT id, opened_at, closed_at, reports FROM business_days "
                          "WHERE closed_at != 0 AND closed_at >= ? AND closed_at < ? ORDER BY id"_s);
                q.addBindValue(QDateTime(from.addDays(-2), QTime(0, 0)).toMSecsSinceEpoch());
                q.addBindValue(QDateTime(to.addDays(3), QTime(0, 0)).toMSecsSinceEpoch());
                if (q.exec()) {
                    while (q.next()) {
                        ClosedDay d{q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toLongLong(),
                                    QJsonDocument::fromJson(q.value(3).toByteArray()).object()};
                        const QDate date = businessDate(d.closedAt);
                        if (date >= from && date <= to)
                            closed.push_back(std::move(d));
                    }
                } else {
                    result.error = q.lastError().text();
                }
                q.finish();
                c.close();
            } else {
                result.error = c.lastError().text();
            }
        }
        QSqlDatabase::removeDatabase(name);
        if (!result.error.isEmpty())
            return result;
    }
    const bool withOpen = data->currentDay && data->currentDay->open() && openDate >= from && openDate <= to;
    const core::BusinessDay openDay = data->currentDay.value_or(core::BusinessDay{});

    const QString storeName = QString::fromStdString(data->settings.storeName);
    const QString checkPrefix = QString::fromStdString(data->settings.checkPrefix);
    const QStringList ids = app::PosService::endOfDayReportIds();
    const QDir out(options.outDir);
    if (!QDir().mkpath(options.outDir)) {
        result.error = u"Cannot make %1."_s.arg(options.outDir);
        return result;
    }

    auto write = [&](const QString &id, const core::Report &report, std::int64_t dayId, QDate date,
                     std::int64_t openedAt, std::int64_t closedAt) {
        const QString rel = u"%1/%2_%3.json"_s.arg(id, date.toString(u"yyyy-MM-dd"_s)).arg(dayId);
        if (!out.mkpath(id)) {
            result.error = u"Cannot make %1."_s.arg(out.filePath(id));
            return false;
        }
        QSaveFile f(out.filePath(rel));
        if (!f.open(QIODevice::WriteOnly)) {
            result.error = u"Cannot write %1: %2"_s.arg(out.filePath(rel), f.errorString());
            return false;
        }
        f.write(QJsonDocument(reportFile(id, maskPrivate(report), storeName, checkPrefix, dayId, date, openedAt,
                                         closedAt, now)).toJson(QJsonDocument::Compact));
        if (!f.commit()) {
            result.error = u"Cannot write %1: %2"_s.arg(out.filePath(rel), f.errorString());
            return false;
        }
        result.files << rel;
        return true;
    };

    for (const ClosedDay &d : closed) {
        const QDate date = businessDate(d.closedAt);
        bool any = false;
        for (const QString &id : ids) {
            const QJsonObject stored = d.reports.value(id).toObject();
            if (stored.isEmpty())
                continue;   // a day from before that report was kept
            if (!write(id, app::reportFromJson(stored), d.id, date, d.openedAt, d.closedAt))
                return result;
            any = true;
        }
        if (any)
            result.days << u"%1_%2"_s.arg(date.toString(u"yyyy-MM-dd"_s)).arg(d.id);
    }

    if (withOpen) {
        // The Reports screen's Today, from the same code; no sink: nothing is written anywhere.
        app::PosService pos(std::move(*data), nullptr);
        pos.setClock([now] { return now; });
        for (const QString &id : ids)
            if (!write(id, pos.buildReport(id), openDay.id, openDate, openDay.openedAt, 0))
                return result;
        result.days << u"%1_%2"_s.arg(openDate.toString(u"yyyy-MM-dd"_s)).arg(openDay.id);
    }
    result.ok = true;
    return result;
}

} // namespace vt::storage
