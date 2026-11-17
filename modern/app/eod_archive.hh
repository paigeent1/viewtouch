#pragma once

// The End of Day archive: each closed day's summary PDF, kept write-once beside the database
// (<data>/eod-archive/<YYYY>/<date> day <id>.pdf, read-only) and recorded in the database table
// eod_archive as a hash chain, so any later change to a file or a record shows.
//
// Each record: the day (id, date), the file, its size and SHA-256, the record before's hash, and
// record_hash = SHA-256(prev_hash + "\n" + this record's fields + "|" + pdf_sha256). The table is
// append-only: SQLite triggers refuse UPDATE and DELETE, and nothing here updates or deletes a row
// or a file it wrote. A day archived again (a day closed twice) gets a NEW record ("re-archived")
// and a new file; the first stays. A file moved to the long-term archive folder (a NAS) gets a NEW
// record ("moved") naming where it is now; the original record stays as it was.
//
// This makes a change detectable, not impossible: someone with the computer's administrator
// rights can still edit files. verify() finds it (the screens say "any change would show").
//
// QtCore + QtSql only (no GUI): the server, tests and scripts/bin/vt-eod-verify (Python, the same
// rules) all check the same way.

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

namespace vt::app::eod {

inline constexpr const char *kGenesis = "0000000000000000000000000000000000000000000000000000000000000000";

struct Entry {
    std::int64_t seq = 0;
    std::int64_t at = 0;          // when it was recorded (ms since epoch)
    std::int64_t dayId = 0;
    QString date;                 // the day it was opened, yyyy-MM-dd
    QString kind;                 // archived | re-archived | moved
    std::int64_t ofSeq = 0;       // moved: the record whose PDF this is
    QString file;                 // relative to the archive folder; moved: an absolute path
    std::int64_t size = 0;
    QString pdfSha;               // SHA-256 of the PDF, hex
    QString prevHash;
    QString recordHash;
};

// What a record's hash is made of (scripts/bin/vt-eod-verify does the same).
QByteArray recordText(const Entry &e);
QString recordHash(const Entry &e);
QString sha256File(const QString &path, std::int64_t *size = nullptr, QString *error = nullptr);

// The table and its triggers (CREATE ... IF NOT EXISTS: an older ViewTouch opens the database the same).
bool ensureTable(const QString &dbPath, QString *error = nullptr);
// Every record, oldest first (none: no table yet).
std::vector<Entry> entries(const QString &dbPath, QString *error = nullptr);

// Keep `pdf` as the day's archived PDF: written under `archiveDir` (never over a file), made
// read-only (0444), recorded. A day already archived gets a "re-archived" record and file.
std::optional<Entry> archiveDay(const QString &dbPath, const QString &archiveDir, std::int64_t dayId, QDate date,
                                const QByteArray &pdf, std::int64_t now, QString *error = nullptr);

// Where a record's PDF is now: the newest "moved" record for it, else its own file.
QString currentPath(const std::vector<Entry> &all, const Entry &e, const QString &archiveDir);

// The integrity check: every record's hash, the chain, every PDF's SHA-256 where it is now,
// no record missing (seq 1, 2, 3...) and no PDF in the folder without a record.
struct DayStatus {
    std::int64_t seq = 0;
    QString state;   // ok | changed | missing | unreachable | record
    QString path;
};
struct Check {
    bool ok = true;            // nothing wrong found (unreachable long-term files aren't wrong)
    QString problem;           // the first one: "Day 2026-11-14: the PDF has changed since it was archived"
    int days = 0;              // archived days
    int checked = 0;           // PDFs whose fingerprint was checked
    int unreachable = 0;       // in the long-term folder, which can't be reached now
    std::vector<DayStatus> status;
};
Check verify(const QString &dbPath, const QString &archiveDir);

// Days older than `days` days (by date, before `today`) copied to <longTermDir>/<store>/<YYYY>/,
// the copy's SHA-256 checked against the chain, a "moved" record added, then the local file removed.
// longTermDir empty: nothing (not an error). Not reachable: nothing moves, `warning` says so.
struct MoveResult {
    int moved = 0;
    QString warning;
};
MoveResult moveOld(const QString &dbPath, const QString &archiveDir, const QString &longTermDir,
                   const QString &storeName, int days, QDate today, std::int64_t now);
// Whether the long-term folder can take files now: it exists, and is a mounted drive or has
// something in it already (an unmounted NAS share is an empty folder on the SD card).
bool longTermReachable(const QString &longTermDir, const QString &archiveDir);
// A store name as a folder name.
QString folderName(const QString &storeName);

// The PDF's pages: how many (a ViewTouch PDF), and page `page` (1-based) as a JPEG, drawn by
// pdftoppm (poppler-utils) at `dpi`. Empty, with `error`, when it can't be drawn here.
int pdfPageCount(const QByteArray &pdf);
QByteArray renderPage(const QString &pdfPath, int page, int dpi, QString *error = nullptr);

} // namespace vt::app::eod
