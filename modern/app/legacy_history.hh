#pragma once

// ILSA's sales history file (ilsa-history.db next to viewtouch.db, written by
// scripts/bin/vt-ilsa-history): read-only queries for the ILSA History reports and Receipt Lookup.
// Each call opens its own read-only connection, so it may run on a worker thread while the POS
// runs. Days are business days, yyyy-MM-dd, from and to included. Nothing is loaded into memory
// beyond the totals asked for; the history never reaches today's drawers, X/Z or end of day.

#include "core/legacy_sales.hh"

#include <QString>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vt::app::legacy {

// The history file beside a POS database.
QString historyPath(const QString &posDatabase);

struct Status {
    bool present = false;     // a history file with days in it
    QString first, last;      // its first and last business days
    std::int64_t days = 0, receipts = 0;
};
Status status(const QString &path, QString *error = nullptr);

core::legacy::ZTotals totals(const QString &path, const QString &from, const QString &to, QString *error = nullptr);
// ILSA's own Z numbers over the days (name -> cents): tax, tender:<type>, payouts, totals:<column>.
std::map<std::string, std::int64_t> ilsaZ(const QString &path, const QString &from, const QString &to,
                                          QString *error = nullptr);
// Merchandise (lottery: the lottery departments) by: dept (the one sold in) | current (the item's
// department in ILSA now, as ILSA's own report) | item | upc | brand; the biggest sellers first
// (departments: by number, with their buydowns and discounts), at most `limit` (`more`: there were more).
std::vector<core::legacy::GroupRow> groups(const QString &path, const QString &from, const QString &to, const QString &by,
                                           bool lottery, int limit, bool *more = nullptr, QString *error = nullptr);
std::vector<core::legacy::PeriodRow> periods(const QString &path, const QString &from, const QString &to, bool byYear,
                                             QString *error = nullptr);
std::vector<core::legacy::Variance> variances(const QString &path, const QString &from, const QString &to,
                                              QString *error = nullptr);
// Sale lines no imported copy's item list names (vt-ilsa-history: name_src 'dept'), by UPC, the most sold first.
std::vector<core::legacy::UnnamedItem> unnamedItems(const QString &path, const QString &from, const QString &to,
                                                    QString *error = nullptr);
// Receipts (without their lines), the newest first: `query` empty: all; "2026-10-28": that day;
// "#412345" or up to 7 digits: that receipt number; 8 digits or more: a UPC sold on it; "12.34":
// that total; else a word in an item's name.
std::vector<core::legacy::Receipt> findReceipts(const QString &path, const QString &query, int limit, int offset,
                                                QString *error = nullptr);
std::optional<core::legacy::Receipt> receipt(const QString &path, std::int64_t id, QString *error = nullptr);

} // namespace vt::app::legacy
