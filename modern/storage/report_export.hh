#pragma once

#include "core/report.hh"

#include <QDate>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace vt::storage {

// The store's reports as JSON files, for the web reporting page (web/vt-reports, the
// vt-web-reports upload; `vtmodern --export-reports <dir>`). Safe while ViewTouch runs: the
// live database is only read (a read-only copy is made first, the way --backup does; the copy
// is what gets opened), nothing is printed and no terminal is contacted.
//
// One file per report per business day: <dir>/<report id>/<YYYY-MM-DD>_<day id>.json. Closed
// days carry the reports saved at End of Day (what Reports shows for a past day); the day still
// open is built now by the same code as the Reports screen's "Today".
//
// A business day's date: the day its End of Day was pressed (closed at 2 AM Saturday: Saturday's);
// the open day: today's, and never before the last closed day.
//
// Never in a file: gift card PINs (always ••••), full card numbers (•••• and the last 4), and
// anything from an ID (dates of birth, license numbers).
struct ReportExportOptions {
    QString database;          // the live database (read only)
    QString outDir;
    QDate from, to;            // business dates (invalid: the open day's)
    std::int64_t now = 0;      // epoch ms (0: the clock)
};

struct ReportExportResult {
    bool ok = false;
    QString error;
    QStringList files;         // written, relative to outDir
    QStringList days;          // "2026-11-09_41" each business day written
};

ReportExportResult exportReports(const ReportExportOptions &options);

// For the export and its tests.
// The business date of a closed day (closedAt in epoch ms, local time).
QDate businessDate(std::int64_t closedAt);
// Gift card PINs, card numbers and ID details masked, as the screen shows them to non-managers.
core::Report maskPrivate(core::Report r);
// One file's JSON.
QJsonObject reportFile(const QString &id, const core::Report &report, const QString &storeName,
                       const QString &checkPrefix, std::int64_t dayId, QDate date, std::int64_t openedAt,
                       std::int64_t closedAt, std::int64_t generatedAt);

} // namespace vt::storage
