#pragma once
// Retail receiving (Manager > Inventory > Receive retail…; owner decisions 2026-11-04 and 2026-11-27): what came
// on a vendor's invoice and in the box, counted into the retail items' on hand. No purchase orders, no manager
// PIN (who received it is kept). Receiving never changes an item's system cost or its price: the invoice's cost
// is kept on the line, and per vendor as that vendor's last cost, for the Cost Differences review.
//
// An invoice's lines:
//  - stock: an item, its quantity in the vendor's unit (a carton, a box) and how many of the item one is
//    (perUom: a carton of 10 packs: 10), the invoice's price per unit, OTP / excise per unit (part of the item's
//    cost) and cigarette prepaid sales tax per unit (not cost: on the invoice so its total ties out, reported
//    apart for the bookkeeper). Free goods: a quantity at no cost.
//  - storeuse: supplies the store uses itself: never on hand; their cost and the sales / use tax on them are
//    store expenses.
//  - delivery, credit, other: invoice-level amounts (freight is not spread into item costs; a later
//    landed-cost step could spread it from these lines).
// The invoice's total, typed from the paper (or the file's), ties out against the lines.
//
// Invoice files (CSV): the columns guessed from their headers (guessField), the rows grouped into invoices by
// account and invoice number (group), each routed to the vendor with that account number.
// Plain C++ (no Qt): app/pos_receiving.cpp uses it, tests/test_receiving_core.cpp checks it.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vt::core::recv {

// --- the lines and the invoice ---------------------------------------------------------------------------

inline constexpr const char *kStock = "stock";
inline constexpr const char *kStoreUse = "storeuse";
inline constexpr const char *kDelivery = "delivery";
inline constexpr const char *kCredit = "credit";
inline constexpr const char *kOther = "other";

inline bool validKind(std::string_view k)
{
    return k == kStock || k == kStoreUse || k == kDelivery || k == kCredit || k == kOther;
}
// A line with an item (or a store-use description) and a quantity; the others are an amount.
inline bool itemKind(std::string_view k) { return k == kStock || k == kStoreUse; }

struct Line {
    std::string kind = kStock;
    std::string itemId;            // the item here (empty: not on file, or store use without an item)
    std::string upc;               // the code scanned or on the invoice
    std::string vendorItem;        // the vendor's item number
    std::string name;              // the item's name, or the invoice's description
    std::string uom;               // the vendor's unit ("CTN", "BOX", "EA")
    double perUom = 1;             // the item's units in one of the vendor's (a carton: 10 packs)
    double qty = 0;                // in the vendor's units
    std::int64_t costCents = -1;   // the invoice's price per vendor unit, before OTP (-1: not typed)
    std::int64_t otpCents = 0;     // OTP / excise per vendor unit: part of the cost
    std::int64_t prepaidCents = 0; // prepaid sales tax per vendor unit: not cost
    std::int64_t taxCents = 0;     // store use: the sales / use tax on the line (the line's)
    std::int64_t amountCents = 0;  // delivery / credit / other: the line's amount (a credit is taken off)
    std::int64_t systemCostCents = 0;   // the item's system cost each when it came (0: none)
    bool leftOut = false;          // not on file and left out: not in on hand (its amount still counts)
    bool operator==(const Line &) const = default;

    // Units of the item that came (on hand goes up by this).
    double units() const { return qty * (perUom > 0 ? perUom : 1); }
    std::int64_t goodsCents() const { return costCents < 0 ? 0 : std::llround(qty * double(costCents)); }
    std::int64_t otpTotal() const { return std::llround(qty * double(otpCents)); }
    std::int64_t prepaidTotal() const { return std::llround(qty * double(prepaidCents)); }
    // The line on the invoice: its goods, OTP, prepaid tax and tax; an amount line's amount (a credit: less).
    std::int64_t extended() const
    {
        if (itemKind(kind))
            return goodsCents() + otpTotal() + prepaidTotal() + taxCents;
        if (kind == kCredit)
            return -std::llabs(amountCents);
        return amountCents;
    }
    // Cost of one unit of the item, with OTP (none: no price typed).
    std::optional<double> unitCostCents() const
    {
        if (costCents < 0 || !(perUom > 0))
            return std::nullopt;
        return double(costCents + otpCents) / perUom;
    }
};

struct Totals {
    std::int64_t stock = 0;      // stock lines' goods and OTP
    std::int64_t otp = 0;        // of which OTP / excise
    std::int64_t prepaid = 0;    // prepaid sales tax (any line)
    std::int64_t storeUse = 0;   // store use goods (with any OTP)
    std::int64_t useTax = 0;     // tax on store use
    std::int64_t delivery = 0;
    std::int64_t credits = 0;    // less than nothing
    std::int64_t other = 0;
    std::int64_t total = 0;
    double units = 0;            // stock units counted in
};

// A change to an invoice after it was received (Invoices received > Open > Edit): one line of it, as it was and
// as it is (a line added: before 0; taken off: after 0).
struct EditLine {
    std::string name;
    double qtyBefore = 0, qtyAfter = 0;
    std::int64_t extBefore = 0, extAfter = 0;
    bool operator==(const EditLine &) const = default;
};

// One change to a received invoice: who, when, where, its total and units before and after, the lines changed.
struct Edit {
    std::int64_t at = 0;
    std::string by, terminal;
    std::int64_t totalBefore = 0, totalAfter = 0;
    double unitsBefore = 0, unitsAfter = 0;
    std::vector<EditLine> lines;
    bool operator==(const Edit &) const = default;
};

struct Receipt {
    std::int64_t id = 0;
    std::string vendorId, vendorName;
    std::string invoice;           // the vendor's invoice number
    std::string date;              // the invoice's date, yyyy-MM-dd
    std::int64_t at = 0;           // saved (epoch ms)
    std::string by;                // who received it
    bool historyOnly = false;      // from a file of past invoices: item numbers and costs only, no stock
    std::string source;            // "" typed on the screen, else the file it came from
    std::int64_t typedTotal = -1;  // the invoice's total from the paper or the file (-1: none)
    std::string note;
    std::vector<Line> lines;
    std::vector<Edit> edits;       // received: the changes made after (oldest first)
    // An unfinished invoice (a draft: nothing on hand yet; kept in its own table as it's worked on).
    std::int64_t changedAt = 0;    // its last change (epoch ms)
    std::string terminal;          // the terminal it was last changed at
    bool parked = false;           // Save for later (else: left as it was, e.g. by a restart)
    std::int64_t reopenOf = 0;     // changes to this received invoice (its id; 0: a new invoice)
    int baseEdits = 0;             // ...which had this many changes when it was opened
    bool operator==(const Receipt &) const = default;

    Totals totals() const
    {
        Totals t;
        for (const Line &l : lines) {
            t.prepaid += l.prepaidTotal();
            if (l.kind == kStock) {
                t.stock += l.goodsCents() + l.otpTotal();
                t.otp += l.otpTotal();
                if (!l.itemId.empty() && !l.leftOut)
                    t.units += l.units();
            } else if (l.kind == kStoreUse) {
                t.storeUse += l.goodsCents() + l.otpTotal();
                t.useTax += l.taxCents;
            } else if (l.kind == kDelivery) {
                t.delivery += l.amountCents;
            } else if (l.kind == kCredit) {
                t.credits -= std::llabs(l.amountCents);
            } else {
                t.other += l.amountCents;
            }
        }
        t.total = t.stock + t.prepaid + t.storeUse + t.useTax + t.delivery + t.credits + t.other;
        return t;
    }
    // Typed total less the lines' (0: ties out); none when no total was typed.
    std::optional<std::int64_t> offBy() const
    {
        if (typedTotal < 0)
            return std::nullopt;
        return typedTotal - totals().total;
    }
};

// What a line is, to compare an invoice before and after a change: its kind, its item (else the vendor's item
// number, the code, the description) and its unit.
inline std::string lineKey(const Line &l)
{
    std::string k = l.kind + '\x1f';
    if (!l.itemId.empty())
        k += l.itemId;
    else if (!l.vendorItem.empty())
        k += "#" + l.vendorItem;
    else if (!l.upc.empty())
        k += "~" + l.upc;
    else
        k += "=" + l.name;
    if (itemKind(l.kind)) {
        char b[32];
        std::snprintf(b, sizeof b, "\x1f%g", l.perUom);
        k += b;
    }
    return k;
}

// The lines that changed between two versions of an invoice (the same line by lineKey, its quantities and
// amounts added up), in the order they come (before's, then the ones added).
inline std::vector<EditLine> diffLines(const std::vector<Line> &before, const std::vector<Line> &after)
{
    std::vector<std::string> order;
    std::map<std::string, EditLine> by;
    const auto at = [&](const Line &l) -> EditLine & {
        const std::string k = lineKey(l);
        auto it = by.find(k);
        if (it == by.end()) {
            order.push_back(k);
            it = by.emplace(k, EditLine{}).first;
            it->second.name = !l.name.empty() ? l.name : !l.upc.empty() ? l.upc : l.vendorItem.empty() ? l.kind : l.vendorItem;
        }
        return it->second;
    };
    for (const Line &l : before) {
        EditLine &e = at(l);
        e.qtyBefore += itemKind(l.kind) ? l.qty : 0;
        e.extBefore += l.extended();
    }
    for (const Line &l : after) {
        EditLine &e = at(l);
        e.qtyAfter += itemKind(l.kind) ? l.qty : 0;
        e.extAfter += l.extended();
    }
    std::vector<EditLine> out;
    for (const std::string &k : order) {
        const EditLine &e = by[k];
        if (std::abs(e.qtyBefore - e.qtyAfter) > 1e-9 || e.extBefore != e.extAfter)
            out.push_back(e);
    }
    return out;
}

// --- a vendor's item: its number, unit and last cost -----------------------------------------------------

struct VendorItem {
    std::string vendorId;
    std::string itemId;            // the item here (empty: a store-use item with no item)
    std::string vendorItem;        // their item number
    std::string uom;               // their unit
    double perUom = 1;             // the item's units in it
    std::int64_t lastCostCents = -1;   // their last price per their unit, with OTP (-1: none yet)
    std::string lastDate;          // its invoice's date (yyyy-MM-dd)
    std::string lastInvoice;
    bool storeUse = false;         // it's always store use
    std::string description;       // their description
    bool operator==(const VendorItem &) const = default;

    std::optional<double> lastUnitCents() const
    {
        if (lastCostCents < 0 || !(perUom > 0))
            return std::nullopt;
        return double(lastCostCents) / perUom;
    }
};

// An item number, invoice number or account number to compare: letters and digits, upper case, no leading
// zeros ("00123-a" and "123A" are one).
inline std::string numberKey(std::string_view s)
{
    std::string out;
    for (char c : s) {
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            out += c;
    }
    std::size_t i = 0;
    while (i + 1 < out.size() && out[i] == '0')
        ++i;
    return out.substr(i);
}

inline std::string vendorItemKey(std::string_view vendorId, std::string_view itemId, std::string_view vendorItem)
{
    std::string k(vendorId);
    k += '\x1f';
    if (!itemId.empty())
        k += itemId;
    else {
        k += '#';
        k += numberKey(vendorItem);
    }
    return k;
}
inline std::string keyOf(const VendorItem &v) { return vendorItemKey(v.vendorId, v.itemId, v.vendorItem); }

// What a line teaches about the vendor's item: its number and unit, and its cost when this invoice is the
// newest one seen (an older invoice loaded later doesn't put an old cost back).
inline void learn(VendorItem &v, const Line &l, std::string_view date, std::string_view invoice)
{
    if (!l.vendorItem.empty())
        v.vendorItem = l.vendorItem;
    if (!l.uom.empty())
        v.uom = l.uom;
    if (l.perUom > 0)
        v.perUom = l.perUom;
    if (l.kind == kStoreUse && l.itemId.empty())
        v.storeUse = true;
    if (!l.name.empty() && l.itemId.empty())
        v.description = l.name;
    if (l.costCents >= 0 && (v.lastDate.empty() || std::string_view(v.lastDate) <= date)) {
        v.lastCostCents = l.costCents + l.otpCents;
        v.lastDate = std::string(date);
        v.lastInvoice = std::string(invoice);
    }
}

// --- amounts and dates as files write them ---------------------------------------------------------------

// "12.99", "$1,234.50", "-3.00", "(3.00)", "12": cents; none if it isn't an amount (empty too).
inline std::optional<std::int64_t> centsOf(std::string_view text)
{
    std::string t;
    bool minus = false;
    for (char ch : text) {
        if (ch == '(' || ch == ')') {
            minus = minus || ch == '(';
            continue;
        }
        if (ch == '$' || ch == ' ' || ch == ',' || ch == '\t')
            continue;
        t += ch;
    }
    if (!t.empty() && t.front() == '-') {
        minus = !minus;
        t.erase(0, 1);
    }
    if (!t.empty() && t.back() == '-') {   // "3.00-": a credit, as some invoices print it
        minus = !minus;
        t.pop_back();
    }
    if (t.empty())
        return std::nullopt;
    char *end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size() || !std::isfinite(v) || v >= 100'000'000)
        return std::nullopt;
    const std::int64_t c = std::llround(v * 100.0);
    return minus ? -c : c;
}

// "3", "2.5", "1,200": a number; none if not.
inline std::optional<double> numberOf(std::string_view text)
{
    std::string t;
    for (char ch : text)
        if (ch != ',' && ch != ' ' && ch != '\t')
            t += ch;
    if (t.empty())
        return std::nullopt;
    char *end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size() || !std::isfinite(v))
        return std::nullopt;
    return v;
}

namespace detail {
inline bool validDate(int y, int m, int d)
{
    return y >= 1990 && y <= 2200 && std::chrono::year_month_day(std::chrono::year(y), std::chrono::month(unsigned(m)),
                                                                   std::chrono::day(unsigned(d))).ok();
}
inline std::string ymd(int y, int m, int d)
{
    char b[16];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d", y, m, d);
    return b;
}
inline int monthOf(std::string_view w)
{
    static const char *names[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
    std::string l;
    for (char c : w.substr(0, 3))
        l += char(std::tolower(static_cast<unsigned char>(c)));
    for (int i = 0; i < 12; ++i)
        if (l == names[i])
            return i + 1;
    return 0;
}
} // namespace detail

// A date as an invoice file writes it: 2026-11-27, 11/27/2026, 11/27/26, 11-27-2026, 20261127, 27-Nov-2026,
// Nov 27 2026, or a spreadsheet's day number (46000); a time after it is ignored. yyyy-MM-dd; empty if not one.
inline std::string dateOf(std::string_view text)
{
    std::string t(text);
    while (!t.empty() && (t.front() == ' ' || t.front() == '"'))
        t.erase(0, 1);
    if (const auto sp = t.find_first_of(" T"); sp != std::string::npos && sp >= 8 && std::isdigit(static_cast<unsigned char>(t[0])))
        t = t.substr(0, sp);   // "11/27/2026 0:00"
    // Pieces: numbers and words.
    std::vector<std::string> parts;
    std::string cur;
    for (char c : t) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            if (!cur.empty() && bool(std::isdigit(static_cast<unsigned char>(cur.back()))) != bool(std::isdigit(static_cast<unsigned char>(c)))) {
                parts.push_back(cur);
                cur.clear();
            }
            cur += c;
        } else if (!cur.empty()) {
            parts.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty())
        parts.push_back(cur);
    const auto num = [](const std::string &s) { return std::atoi(s.c_str()); };
    const auto digits = [](const std::string &s) {
        return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
    };
    if (parts.size() == 1 && digits(parts[0])) {
        const std::string &p = parts[0];
        if (p.size() == 8) {   // 20261127
            const int y = num(p.substr(0, 4)), m = num(p.substr(4, 2)), d = num(p.substr(6, 2));
            return detail::validDate(y, m, d) ? detail::ymd(y, m, d) : std::string();
        }
        if (p.size() == 5) {   // a spreadsheet's day: days since 1899-12-30
            const std::chrono::sys_days base = std::chrono::year(1899) / std::chrono::December / std::chrono::day(30);
            const std::chrono::year_month_day ymd{base + std::chrono::days(num(p))};
            return detail::ymd(int(ymd.year()), int(unsigned(ymd.month())), int(unsigned(ymd.day())));
        }
        return {};
    }
    if (parts.size() < 3)
        return {};
    int y = 0, m = 0, d = 0;
    if (digits(parts[0]) && parts[0].size() == 4 && digits(parts[1]) && digits(parts[2])) {   // 2026-11-27
        y = num(parts[0]);
        m = num(parts[1]);
        d = num(parts[2]);
    } else if (digits(parts[0]) && digits(parts[1]) && digits(parts[2])) {   // 11/27/2026, 11/27/26
        m = num(parts[0]);
        d = num(parts[1]);
        y = num(parts[2]);
    } else if (digits(parts[0]) && !digits(parts[1]) && digits(parts[2])) {   // 27-Nov-2026
        d = num(parts[0]);
        m = detail::monthOf(parts[1]);
        y = num(parts[2]);
    } else if (!digits(parts[0]) && digits(parts[1]) && digits(parts[2])) {   // Nov 27 2026
        m = detail::monthOf(parts[0]);
        d = num(parts[1]);
        y = num(parts[2]);
    } else {
        return {};
    }
    if (y < 100)
        y += 2000;
    return detail::validDate(y, m, d) ? detail::ymd(y, m, d) : std::string();
}

// --- a CSV file -----------------------------------------------------------------------------------------

// Rows of cells: quotes ("a, b", "say ""hi"""), a cell over several lines in quotes, \r\n or \n, a UTF-8 mark at
// the start. The separator is the first line's: a comma, else a tab or a semicolon if it has more of those.
inline std::vector<std::vector<std::string>> readCsv(std::string_view text)
{
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);
    char sep = ',';
    {
        const std::string_view first = text.substr(0, text.find('\n'));
        const auto n = [&](char c) { return std::ranges::count(first, c); };
        if (n('\t') > n(',') && n('\t') >= n(';'))
            sep = '\t';
        else if (n(';') > n(','))
            sep = ';';
    }
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string cell;
    bool quoted = false, any = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    cell += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell += c;
            }
            continue;
        }
        if (c == '"') {
            quoted = true;
            any = true;
        } else if (c == sep) {
            row.push_back(std::move(cell));
            cell.clear();
            any = true;
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            row.push_back(std::move(cell));
            cell.clear();
            rows.push_back(std::move(row));
            row.clear();
            any = false;
        } else {
            cell += c;
            any = true;
        }
    }
    if (any || !cell.empty() || !row.empty()) {
        row.push_back(std::move(cell));
        rows.push_back(std::move(row));
    }
    return rows;
}

// --- the columns -----------------------------------------------------------------------------------------

// What a column can be. Amounts per unit are per the vendor's unit (the row's), use tax and extended the line's.
inline const std::vector<std::string> &fields()
{
    static const std::vector<std::string> f = {"invoice", "date", "account", "itemNo", "upc", "description", "uom",
                                               "perUom", "qty", "unitPrice", "otp", "prepaid", "extended", "lineType",
                                               "delivery", "useTax", "invoiceTotal"};
    return f;
}

// A header as plain words: "Item #" -> "item no", "Qty. Shipped" -> "qty shipped", "UPC-Code" -> "upc code".
inline std::string headerWords(std::string_view header)
{
    std::string out;
    bool space = true;
    const auto put = [&](std::string_view w) {
        if (!out.empty())
            out += ' ';
        out += w;
        space = false;
    };
    for (char ch : header) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) {
            if (space && !out.empty())
                out += ' ';
            out += char(std::tolower(c));
            space = false;
        } else if (ch == '#') {
            put("no");
            space = true;
        } else {
            space = true;
        }
    }
    return out;
}

// The field a header is (empty: none), and how sure (0 best).
inline std::pair<std::string, int> guessField(std::string_view header)
{
    static const std::vector<std::pair<std::string, std::vector<std::string>>> names = {
        {"invoiceTotal", {"invoice total", "inv total", "total invoice", "invoice amount", "invoice amt", "total due",
                          "amount due", "invoice total amount"}},
        {"invoice", {"invoice", "invoice no", "invoice number", "invoice num", "inv", "inv no", "inv number",
                     "invoice id", "document no", "document number", "doc no", "document"}},
        {"date", {"invoice date", "inv date", "date", "ship date", "delivery date", "doc date", "document date",
                  "order date"}},
        {"account", {"ship to", "ship to no", "ship to number", "shipto", "ship to account", "ship to acct",
                     "account", "account no", "account number", "acct", "acct no", "customer", "customer no",
                     "customer number", "cust no", "cust", "customer id", "location", "store", "store no"}},
        {"itemNo", {"item", "item no", "item number", "item code", "vendor item", "vendor item no", "product code",
                    "product no", "prod no", "sku", "part", "part no", "item id", "product id", "catalog no", "code"}},
        {"upc", {"upc", "upc no", "upc code", "barcode", "bar code", "ean", "gtin", "unit upc", "pack upc", "upc pack",
                 "case upc", "carton upc", "retail upc", "upc number"}},
        {"description", {"description", "desc", "item description", "product description", "product", "product name",
                         "item name", "name"}},
        {"uom", {"uom", "unit of measure", "um", "u m", "unit", "unit type", "sell unit", "ship unit", "pack type"}},
        {"perUom", {"units per uom", "pack size", "units per case", "units per carton", "per carton", "per case",
                    "units", "pack qty", "pack", "inner pack", "qty per uom", "units per unit", "count", "pk", "per"}},
        {"qty", {"qty shipped", "shipped", "ship qty", "qty ship", "qty invoiced", "invoiced", "qty", "quantity",
                 "qty ordered", "ordered", "order qty"}},
        {"unitPrice", {"unit price", "unit cost", "price", "cost", "price each", "net price", "net cost", "each",
                       "price per unit", "invoice price", "cost each"}},
        {"otp", {"otp", "otp tax", "otp per unit", "excise", "excise tax", "excise per unit", "tobacco tax",
                 "other tobacco tax", "otp excise"}},
        {"prepaid", {"prepaid", "prepaid tax", "prepaid sales tax", "pre paid tax", "ppd tax", "sales tax prepaid",
                     "cig tax prepaid", "prepaid cig tax"}},
        {"extended", {"extended", "extended price", "ext price", "ext", "extension", "extended amount", "ext amount",
                      "line total", "amount", "net amount", "line amount", "total"}},
        {"lineType", {"line type", "type", "line kind", "kind", "store use", "use"}},
        {"delivery", {"delivery", "delivery charge", "delivery fee", "freight", "shipping", "fuel", "fuel surcharge"}},
        {"useTax", {"use tax", "sales tax", "tax"}},
    };
    std::string h = headerWords(header);
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto &[field, words] : names) {
            const auto it = std::ranges::find(words, h);
            if (it != words.end())
                return {field, int(it - words.begin()) + pass * 100};
        }
        // "Invoice Number", "Qty No": the same without a trailing number word.
        bool cut = false;
        for (const char *tail : {" number", " num", " no"})
            if (h.size() > std::string_view(tail).size() && h.ends_with(tail)) {
                h.resize(h.size() - std::string_view(tail).size());
                cut = true;
                break;
            }
        if (!cut)
            break;
    }
    return {};
}

// field -> its column (-1: none).
using Mapping = std::map<std::string, int>;

// The columns of a header row, each field to the header that names it best.
inline Mapping guessMapping(const std::vector<std::string> &header)
{
    Mapping m;
    std::map<std::string, int> score;
    for (int i = 0; i < int(header.size()); ++i) {
        const auto [field, sure] = guessField(header[std::size_t(i)]);
        if (field.empty())
            continue;
        if (const auto it = score.find(field); it == score.end() || sure < it->second) {
            score[field] = sure;
            m[field] = i;
        }
    }
    return m;
}

// The header row: the first of the first 30 with at least 3 fields, one of them a quantity or an amount
// (a title or blank rows above it are passed over); -1: none.
inline int headerRow(const std::vector<std::vector<std::string>> &rows)
{
    for (int r = 0; r < int(rows.size()) && r < 30; ++r) {
        const Mapping m = guessMapping(rows[std::size_t(r)]);
        if (m.size() >= 3 && (m.contains("qty") || m.contains("extended") || m.contains("unitPrice")))
            return r;
    }
    return -1;
}

// --- the rows, as invoices -------------------------------------------------------------------------------

struct FileLine {
    int row = 0;                   // its row in the file (1 = the first)
    std::string invoice, date, account, itemNo, upc, description, uom, lineType;
    double perUom = 0;             // 0: not in the file
    double qty = 0;
    bool hasQty = false;
    std::int64_t unitPrice = -1;   // -1: none
    std::int64_t otp = 0, prepaid = 0, useTax = 0, delivery = 0;
    std::int64_t extended = 0;
    bool hasExtended = false;
    std::int64_t invoiceTotal = -1;
};

struct FileInvoice {
    std::string account, invoice, date;
    std::vector<FileLine> lines;
    std::int64_t delivery = 0;     // from a Delivery column (once an invoice)
    std::int64_t typedTotal = -1;  // the file's invoice total, else its lines' extended plus delivery (-1: none)
};

// The kind a row is: its line type's words, else a row with no item that says freight or delivery, else a
// row less than nothing (a credit), else stock.
inline std::string kindOf(const FileLine &l)
{
    std::string t;
    for (char c : l.lineType)
        t += char(std::tolower(static_cast<unsigned char>(c)));
    const auto has = [&](const char *w) { return t.find(w) != std::string::npos; };
    if (has("store") || has("suppl") || t == "s" || t == "su" || t == "use" || t == "expense")
        return kStoreUse;
    if (has("credit") || has("return") || t == "cr" || t == "rtn")
        return kCredit;
    if (has("deliver") || has("freight") || has("fuel") || has("shipping"))
        return kDelivery;
    if (has("other") || has("fee") || has("charge") || has("misc"))
        return kOther;
    if (l.itemNo.empty() && l.upc.empty()) {
        std::string d;
        for (char c : l.description)
            d += char(std::tolower(static_cast<unsigned char>(c)));
        if (d.find("freight") != std::string::npos || d.find("deliver") != std::string::npos
            || d.find("fuel") != std::string::npos)
            return kDelivery;
    }
    if ((l.hasQty && l.qty < 0) || (l.hasExtended && l.extended < 0))
        return kCredit;
    return kStock;
}

// The file's rows read by the mapping, from the row after the header; blank rows and rows with nothing to
// count (no quantity, amount or delivery) are passed over.
inline std::vector<FileLine> fileLines(const std::vector<std::vector<std::string>> &rows, int header, const Mapping &m)
{
    std::vector<FileLine> out;
    const auto cell = [&](const std::vector<std::string> &r, const char *field) -> std::string {
        const auto it = m.find(field);
        if (it == m.end() || it->second < 0 || it->second >= int(r.size()))
            return {};
        std::string s = r[std::size_t(it->second)];
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.erase(0, 1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
            s.pop_back();
        return s;
    };
    for (int i = header + 1; i < int(rows.size()); ++i) {
        const auto &r = rows[std::size_t(i)];
        FileLine l;
        l.row = i + 1;
        l.invoice = cell(r, "invoice");
        l.date = dateOf(cell(r, "date"));
        l.account = cell(r, "account");
        l.itemNo = cell(r, "itemNo");
        l.upc = cell(r, "upc");
        l.description = cell(r, "description");
        l.uom = cell(r, "uom");
        l.lineType = cell(r, "lineType");
        if (const auto v = numberOf(cell(r, "perUom")); v && *v > 0)
            l.perUom = *v;
        if (const auto v = numberOf(cell(r, "qty"))) {
            l.qty = *v;
            l.hasQty = true;
        }
        if (const auto v = centsOf(cell(r, "unitPrice")))
            l.unitPrice = std::llabs(*v);
        l.otp = centsOf(cell(r, "otp")).value_or(0);
        l.prepaid = centsOf(cell(r, "prepaid")).value_or(0);
        l.useTax = centsOf(cell(r, "useTax")).value_or(0);
        l.delivery = centsOf(cell(r, "delivery")).value_or(0);
        if (const auto v = centsOf(cell(r, "extended"))) {
            l.extended = *v;
            l.hasExtended = true;
        }
        if (const auto v = centsOf(cell(r, "invoiceTotal")))
            l.invoiceTotal = *v;
        const bool something = l.hasQty || l.hasExtended || l.unitPrice >= 0 || l.delivery != 0 || l.invoiceTotal >= 0;
        if (!something && l.itemNo.empty() && l.upc.empty())
            continue;
        if (!something)
            continue;
        out.push_back(std::move(l));
    }
    return out;
}

// The rows as invoices, by account and invoice number, in the file's order. A row without an invoice number
// belongs to the invoice above it (a file that writes the number once). An invoice's date: its first row's.
inline std::vector<FileInvoice> group(const std::vector<FileLine> &lines)
{
    std::vector<FileInvoice> out;
    std::map<std::string, std::size_t> at;
    std::string lastKey;
    for (const FileLine &l0 : lines) {
        FileLine l = l0;
        std::string key = numberKey(l.account) + '\x1f' + numberKey(l.invoice);
        if (l.invoice.empty() && !lastKey.empty())
            key = lastKey;
        auto it = at.find(key);
        if (it == at.end()) {
            FileInvoice inv;
            inv.account = l.account;
            inv.invoice = l.invoice;
            inv.date = l.date;
            at[key] = out.size();
            out.push_back(std::move(inv));
            it = at.find(key);
        }
        FileInvoice &inv = out[it->second];
        if (inv.date.empty())
            inv.date = l.date;
        if (inv.account.empty())
            inv.account = l.account;
        if (inv.delivery == 0 && l.delivery != 0)
            inv.delivery = l.delivery;   // (a column repeated on every row: once)
        if (l.invoiceTotal >= 0 && inv.typedTotal < 0)
            inv.typedTotal = l.invoiceTotal;
        lastKey = key;
        // A row that only carries the delivery or the total adds no line.
        if (!l.hasQty && !l.hasExtended && l.unitPrice < 0)
            continue;
        inv.lines.push_back(std::move(l));
    }
    // No total column: the lines' extended amounts (when the file has them) and the delivery.
    for (FileInvoice &inv : out) {
        if (inv.typedTotal >= 0)
            continue;
        bool anyExtended = false;
        std::int64_t sum = 0;
        for (const FileLine &l : inv.lines) {
            if (!l.hasExtended)
                continue;
            anyExtended = true;
            sum += kindOf(l) == kCredit ? -std::llabs(l.extended) : l.extended;
        }
        if (anyExtended)
            inv.typedTotal = sum + inv.delivery;
    }
    return out;
}

// A file's row as an invoice line (the item it is found by the app: itemId, perUom when the file has none).
// The price per unit: the file's, else its extended amount over the quantity less OTP and prepaid tax.
inline Line lineOf(const FileLine &f)
{
    Line l;
    l.kind = kindOf(f);
    l.upc = f.upc;
    l.vendorItem = f.itemNo;
    l.name = f.description;
    l.uom = f.uom;
    l.perUom = f.perUom > 0 ? f.perUom : 1;
    if (itemKind(l.kind)) {
        l.qty = f.hasQty ? f.qty : 1;
        if (f.unitPrice >= 0)
            l.costCents = f.unitPrice;
        else if (f.hasExtended && l.qty != 0)
            l.costCents = std::max<std::int64_t>(0, std::llround(double(f.extended) / l.qty) - f.otp - f.prepaid);
        l.otpCents = f.otp;
        l.prepaidCents = f.prepaid;
        l.taxCents = l.kind == kStoreUse ? f.useTax : 0;
    } else {
        const std::int64_t amount = f.hasExtended ? f.extended
                                    : f.unitPrice >= 0 ? std::llround(double(f.unitPrice) * (f.hasQty ? f.qty : 1))
                                                       : 0;
        l.amountCents = l.kind == kCredit ? std::llabs(amount) : amount;
        l.qty = f.hasQty ? f.qty : 0;
    }
    return l;
}

} // namespace vt::core::recv
