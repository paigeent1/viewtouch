#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif

// ILSA's sales history (2014-2026), imported by scripts/bin/vt-ilsa-history into its own file,
// ilsa-history.db next to viewtouch.db. History only: never in today's drawers, X/Z, end of day or
// cash. These are its reports, built from totals the history file's queries give
// (app/legacy_history.cpp): Daily Z, sales by department, item movement, GP by month / year,
// ILSA's Z beside the imported receipts (and ViewTouch's own checks), and the days that differ.
//
// What counts (vt-ilsa-history's rules, checked against the owner's ILSA printouts 2026-11-07):
// sales receipts only (ILSA mode 0); item lines are merchandise ("Total Retail"), except lottery
// departments (history only: never in merchandise or GP); gift cards and BH / MFG coupons (1,x
// entries) are not sales; cost = cost at the time x quantity, and a line without a cost (dept 260
// open rings) costs its retail in ILSA (0% margin: it skews ILSA's GP down): GP "as ILSA" counts it
// so, GP "without no-cost lines" leaves those lines out (the true margin). Promotions are ILSA's
// "Buydowns": Adjusted Cost = Cost + Buydowns; Adjusted Retail = Retail + Discounts; GP =
// (Adjusted Retail - Adjusted Cost) / Adjusted Retail. Departments: the one the line was sold in
// (owner 2026-11-07), or the item's current one ("as ILSA", to match its printouts).

#include "core/check.hh"
#include "core/report.hh"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace vt::core::legacy {

// A day's or a range's totals: what a Daily Z shows.
struct ZTotals {
    std::int64_t receipts = 0;   // sales
    std::int64_t notSales = 0;   // cancelled sales / no sales (ILSA modes 1 and 2)
    Money merchandise;           // item lines, lottery out
    Money lottery;               // lottery departments (items and entries): history only
    Money entries;               // other department entries (coupons, redemptions), lottery out
    Money promotions;            // promotions (ILSA's buydowns) and their reversals; discounts on ViewTouch checks
    Money discounts;             // receipt discounts ILSA kept apart from the lines (when the export has them)
    Money other;                 // anything else ILSA put on a receipt (its 30/30 lines)
    Money tax;
    Money receiptTotals;         // the receipts' totals as saved
    Money payouts;               // paid outs (not sales)
    std::vector<std::pair<std::string, Money>> tenders;   // name -> net (cash: less all change given)
    std::int64_t voidLines = 0;  // voided lines (an ILSA void is a pair: both halves counted)
    Money voided;                // what the voided lines were worth (each pair once)
    std::int64_t returnLines = 0;
    Money returned;              // negative
    double units = 0;            // merchandise units sold (net of returns)
    std::int64_t itemLines = 0;
    Money cost;                  // merchandise cost: cost at the time x quantity
    Money noCostSales;           // merchandise sales on lines without a cost
    bool costKnown = true;       // false: no cost per line (ViewTouch's checks, for now)

    Money net() const { return merchandise + lottery + entries + promotions + other; }
    // ILSA's department sales: merchandise and its promotions (not gift cards, coupons, lottery).
    Money departmentSales() const { return merchandise + promotions + discounts; }
    Money gross() const { return net() + tax; }
    Money tender(const std::string &name) const;
};

// One row of a sales-by report: a department, item, UPC, brand.
struct GroupRow {
    std::string key;     // department number, UPC, brand...
    std::string label;   // its name
    std::string extra;   // by item: its UPC; by UPC: its department
    double units = 0;
    Money sales, cost, noCostSales;   // retail; cost of the lines with a cost; retail of those without
    Money buydowns, discounts;        // promotions (negative) and discounts on its lines (departments)
    std::int64_t lines = 0;
};

// GP over a month ("2026-10") or a year ("2026").
struct PeriodRow {
    std::string period;
    double units = 0;
    Money sales, cost, noCostSales, lottery, buydowns, discounts;
};

// A day ILSA's own Z and the imported receipts don't agree on (vt-ilsa-history's self-check).
struct Variance {
    std::string day;
    int reg = 0;
    std::int64_t z = 0;
    std::string what;
    std::int64_t ilsa = 0, ours = 0;   // cents (a count when `what` ends with "(count)")
};

// An old receipt, read-only, as ILSA rang it.
struct ReceiptLine {
    std::string name, upc, kind;   // kind: item | entry | promo | payout | other
    int dept = 0;
    double qty = 1;
    Money unit, amount;
    bool voided = false, returned = false;
};
struct ReceiptTender {
    int type = 0;
    std::string name;
    Money amount, change;
};
struct Receipt {
    std::int64_t id = 0;        // in the history file
    std::string source;         // the ILSA database it came from
    int reg = 0;
    std::int64_t number = 0;    // ILSA's receipt number
    std::string day, at;        // business day; when it was rung (YYYY-MM-DD HH:MM:SS)
    int mode = 0;               // 0: a sale
    int cashier = 0;
    Money total, tax;
    int ageVerified = 0;        // 18, 21; 0: not checked
    std::string dob;            // the buyer's date of birth as ILSA had it (YYYY-MM-DD); empty: none
    std::vector<ReceiptLine> lines;
    std::vector<ReceiptTender> tenders;
};

// "31.2%" (part of whole); "—" when whole is 0.
std::string percent(Money part, Money whole);
// A gross profit: (sales - cost) / sales.
std::string gp(Money sales, Money cost);
// ILSA's GP: lines without a cost at their retail, buydowns off the cost, discounts off the retail.
std::string gpAsIlsa(Money sales, Money cost, Money noCostSales, Money buydowns = {}, Money discounts = {});
// The true margin: the lines without a cost left out.
std::string gpTrue(Money sales, Money cost, Money noCostSales, Money buydowns = {}, Money discounts = {});
// ILSA's tender type as ViewTouch groups payments: Cash, Check, Gift card, Card, House charge.
std::string tenderGroup(int ilsaType);
// The same groups for a ViewTouch payment (a cash tender named "Check" is a check).
std::string tenderGroup(const Payment &p);

// ViewTouch's own closed checks as the same totals (the side-by-side on parallel beta days).
ZTotals fromChecks(const std::vector<Check> &closed, const TaxRates &rates);

// `source`: "ILSA" or "ViewTouch"; ctx.period: the day or range.
Report dailyZ(const ZTotals &t, const std::string &source, const ReportContext &ctx);
// Departments (lottery departments listed apart, out of the totals and GP); `current`: by the items'
// departments in ILSA now (as ILSA's own report), else the department each line was sold in.
Report departmentSales(const std::vector<GroupRow> &depts, const std::vector<GroupRow> &lottery, const ReportContext &ctx,
                       bool current = false);
// by: item | upc | brand | dept. `more`: more rows than shown (the biggest sellers first).
Report itemMovement(const std::vector<GroupRow> &rows, const std::string &by, bool more, const ReportContext &ctx);
Report gpByPeriod(const std::vector<PeriodRow> &rows, bool byYear, const ReportContext &ctx);
// ILSA's Z numbers (name -> cents: "tax", "tender:<type>", "payouts", "totals:<column>") beside the
// imported receipts and, when given, ViewTouch's own checks on the same days.
Report sideBySide(const std::map<std::string, std::int64_t> &ilsaZ, const ZTotals &imported,
                  const ZTotals *viewtouch, const ReportContext &ctx);
Report differences(const std::vector<Variance> &list, const ReportContext &ctx);

// The receipt as ILSA printed it, `width` characters wide (vt-ilsa-receipt's layout); the buyer's
// date of birth only on the store's copy (`withDob`), as ILSA printed it.
std::string receiptText(const Receipt &r, bool withDob, int width = 40);
// The same as rows (left, right; ("-", "-") a rule), and as a report for the receipt printer.
std::vector<std::pair<std::string, std::string>> receiptRows(const Receipt &r, bool withDob);
Report receiptReport(const Receipt &r, bool withDob);

} // namespace vt::core::legacy
