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
// `store`, everywhere below: those stores' copies only ("BHDAN", "BHDAN,BHNM": legacy_sources.store); empty: every
// store's; "-": none.
Status status(const QString &path, QString *error = nullptr, const QString &store = {});
// The stores the history file has copies of (BHDAN, BHNM...), in order.
QStringList stores(const QString &path, QString *error = nullptr);

core::legacy::ZTotals totals(const QString &path, const QString &from, const QString &to, QString *error = nullptr,
    const QString &store = {});
// ILSA's own Z numbers over the days (name -> cents): tax, tender:<type>, payouts, totals:<column>.
std::map<std::string, std::int64_t> ilsaZ(const QString &path, const QString &from, const QString &to,
                                          QString *error = nullptr,
    const QString &store = {});
// Merchandise (lottery: the lottery departments) by: dept (the one sold in) | current (the item's
// department in ILSA now, as ILSA's own report) | item | upc | brand; the biggest sellers first
// (departments: by number, with their buydowns and discounts), at most `limit` (`more`: there were more).
std::vector<core::legacy::GroupRow> groups(const QString &path, const QString &from, const QString &to, const QString &by,
                                           bool lottery, int limit, bool *more = nullptr, QString *error = nullptr,
    const QString &store = {});
std::vector<core::legacy::PeriodRow> periods(const QString &path, const QString &from, const QString &to, bool byYear,
                                             QString *error = nullptr,
    const QString &store = {});
std::vector<core::legacy::Variance> variances(const QString &path, const QString &from, const QString &to,
                                              QString *error = nullptr,
    const QString &store = {});
// Sale lines no imported copy's item list names (vt-ilsa-history: name_src 'dept'), by UPC, the most sold first.
std::vector<core::legacy::UnnamedItem> unnamedItems(const QString &path, const QString &from, const QString &to,
                                                    QString *error = nullptr,
    const QString &store = {});
// ILSA's cashiers (every copy's, with or without a cashier list) with their sales and who they are in
// ViewTouch.
std::vector<core::legacy::Cashier> cashiers(const QString &path, QString *error = nullptr,
    const QString &store = {});
// Ties ILSA cashier `number` of copy `sourceId` to ViewTouch employee `employeeId` (empty: a former
// employee, untied); also the same number with the same name in that store's other copies (allStores:
// in every store's copies) when they aren't tied yet. Ties are per copy. Writes the history file
// (Reports > ILSA History > Cashiers).
bool setCashier(const QString &path, int sourceId, int number, const std::string &employeeId,
                const std::string &employeeName, QString *error = nullptr, bool allStores = false);
// What each cashier sold of each item (sales receipts; voided lines and lottery out); `filter`: an
// item name, UPC or department name containing it (empty: all).
std::vector<core::legacy::EmployeeItemRow> employeeItems(const QString &path, const QString &from, const QString &to,
                                                         const QString &filter, QString *error = nullptr,
    const QString &store = {});
// Receipts (without their lines), the newest first: `query` empty: all; "2026-10-28": that day;
// "#412345" or up to 7 digits: that receipt number ("BHDAN-412345", "bhdan412345": of that store's copies); 8 digits or more: a UPC sold on it; "12.34":
// that total; else a word in an item's name.
std::vector<core::legacy::Receipt> findReceipts(const QString &path, const QString &query, int limit, int offset,
                                                QString *error = nullptr,
    const QString &store = {});
std::optional<core::legacy::Receipt> receipt(const QString &path, std::int64_t id, QString *error = nullptr);

} // namespace vt::app::legacy
