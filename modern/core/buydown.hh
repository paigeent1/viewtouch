#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif

// Manufacturer buydowns and promotions (owner 2026-11-25, after ILSA's "Buydown and Promotional Sales
// Definition" screen): "Sonoma $4.10 a carton, Oct 1 to Dec 31". A definition has its dates and an amount per
// carton, and its items by UPC with their Part/Ctn (0 = a carton: the whole amount; 0.1 = a pack: a tenth).
// When an item in a running definition is rung, its line gets the amount off ("SONOMA SAVINGS! -$0.41")
// at the item's regular price: OrderLine::buydown*, frozen when it was rung, carried with the line through
// quantity changes, voids, transfers, splits and returns. Totals::buydowns sums them.
// Plain C++ (no Qt): app/pos_buydowns.cpp uses it, tests/test_buydown_core.cpp checks it.
//
// Dates and times are the store's local time as text, "YYYY-MM-DD HH:MM" (compared as text); an empty
// start: from always, an empty end: no end.

#include "core/check.hh"
#include "core/menu.hh"
#include "core/money.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace vt::core {

struct BuydownItem {
    std::string upc;          // the item's UPC (how another store finds it)
    std::string itemId;       // this store's item (empty: not found here)
    double partCtn = 0.0;     // 0 (or 1): a carton (the whole amount); 0.1: a pack (a tenth)...
    std::string name;         // as it was when added (shown when the item is gone)
    bool operator==(const BuydownItem &) const = default;
};

struct Buydown {
    std::string id;           // "bd-7"
    int number = 0;           // the ID column (1, 2, 3...)
    std::string type = "buydown";   // "buydown" | "promo"
    std::string name;         // "SONOMA 2026Q4"
    std::string receiptText;  // "SONOMA SAVINGS!"
    Money amount;             // per carton
    bool taxBefore = false;   // a promotion's "Apply taxes BEFORE discount" (default AFTER: on the reduced price);
                              // a buydown follows the store's setting (PosSettings::buydownTaxBefore)
    bool usePartCtn = true;   // Subunits: "Use Part of Carton" field; off: "Apply Full Discount Amount" to every UPC
    // ILSA's four styles. Qty is a cap: how many units (all its UPCs together) get the reduced price.
    //   1 Date Range Only: every unit sold from start to end.
    //   2 Quantity Only: the first Qty units sold from the start (no end).
    //   3 Quantity with Date Range: up to Qty units sold from start to end.
    //   4 Old Style: an end date and Qty, whichever comes first.
    int style = 1;
    int qty = 0;
    int used = 0;             // units sold with it on closed checks (returns take theirs back)
    std::string start;        // "2026-10-01 00:00" (empty: from always)
    std::string end;          // "2026-12-31 23:00" (empty: no end)
    std::string manufacturer, rptCode, account;   // account: ILSA's "Sale Acct"
    std::string createdBy, changedBy;
    std::int64_t createdAt = 0, changedAt = 0;   // epoch ms
    std::vector<BuydownItem> items;

    bool isPromo() const { return type == "promo"; }
    bool capped() const { return style >= 2 && style <= 4; }
    bool operator==(const Buydown &) const = default;
};

namespace buydown {

enum class State { Coming, Running, Ended };

inline const char *stateName(State s)
{
    return s == State::Coming ? "coming" : s == State::Running ? "running" : "ended";
}

inline const char *styleName(int style)
{
    switch (style) {
    case 2: return "Quantity Only";
    case 3: return "Quantity with Date Range";
    case 4: return "Old Style";
    default: return "Date Range Only";
    }
}

// `now`: "YYYY-MM-DD HH:MM" local. Quantity Only has no end; a capped style whose Qty is used up has ended.
inline State stateAt(const Buydown &b, const std::string &now, int inOpenChecks = 0)
{
    if (!b.start.empty() && now < b.start)
        return State::Coming;
    if (b.style != 2 && !b.end.empty() && now > b.end)
        return State::Ended;
    if (b.capped() && b.used + inOpenChecks >= b.qty)
        return State::Ended;
    return State::Running;
}

// Units it may still give (capped styles); -1: no cap.
inline int unitsLeft(const Buydown &b, int inOpenChecks = 0)
{
    return b.capped() ? std::max(0, b.qty - b.used - inOpenChecks) : -1;
}

// The amount off one of an item: the whole amount for a carton (Part/Ctn 0 or 1, as ILSA showed both) or when
// Part/Ctn isn't used ("Subunits: Apply Full Discount Amount"),
// else that part of it, to the cent ($4.10 x 0.1 = $0.41).
inline Money amountFor(const Buydown &b, const BuydownItem &i)
{
    if (!b.usePartCtn || i.partCtn <= 0.0 || i.partCtn >= 1.0)
        return b.amount;
    return Money::fromCents(std::llround(double(b.amount.cents()) * i.partCtn));
}

// Digits only, leading zeros gone: "028200003201" and "28200003201" are the same UPC.
inline std::string upcKey(std::string_view code)
{
    std::string d;
    for (char c : code)
        if (c >= '0' && c <= '9')
            d.push_back(c);
    const auto nz = d.find_first_not_of('0');
    return nz == std::string::npos ? std::string() : d.substr(nz);
}

inline bool sameUpc(std::string_view a, std::string_view b)
{
    const std::string ka = upcKey(a);
    return !ka.empty() && ka == upcKey(b);
}

// The definition's entry for a menu item: by its id, else by any of its UPCs (or the code it was rung with).
inline const BuydownItem *entryFor(const Buydown &b, const MenuItem &m, std::string_view rungUpc = {})
{
    for (const BuydownItem &i : b.items)
        if (!i.itemId.empty() && i.itemId == m.id)
            return &i;
    for (const BuydownItem &i : b.items) {
        if (i.upc.empty())
            continue;
        if (!rungUpc.empty() && sameUpc(i.upc, rungUpc))
            return &i;
        for (const std::string &code : m.barcodes)
            if (sameUpc(i.upc, code))
                return &i;
    }
    return nullptr;
}

// What applies to `units` of an item rung now: of the running definitions that have it (a capped one only
// with room for them all: `inOpen` counts the units already on open checks, by definition id), the one with
// the larger amount (never two together); the same amount: the lower ID. None: nullopt.
struct Hit {
    const Buydown *def = nullptr;
    const BuydownItem *item = nullptr;
    Money each;
};
inline std::optional<Hit> hitFor(const std::vector<Buydown> &defs, const MenuItem &m, const std::string &now,
                                 std::string_view rungUpc = {}, const std::map<std::string, int> &inOpen = {},
                                 int units = 1)
{
    std::optional<Hit> best;
    for (const Buydown &b : defs) {
        const auto open = inOpen.find(b.id);
        const int pending = open == inOpen.end() ? 0 : open->second;
        if (b.amount.cents() <= 0 || stateAt(b, now, pending) != State::Running)
            continue;
        if (b.capped() && unitsLeft(b, pending) < std::max(1, units))
            continue;
        const BuydownItem *i = entryFor(b, m, rungUpc);
        if (!i)
            continue;
        const Money each = amountFor(b, *i);
        if (each.cents() <= 0)
            continue;
        if (!best || each > best->each || (each == best->each && b.number < best->def->number))
            best = Hit{&b, i, each};
    }
    return best;
}

inline void clear(OrderLine &l)
{
    l.buydownId.clear();
    l.buydownText.clear();
    l.buydownEach = Money();
    l.buydownTaxBefore = false;
}

// Puts it on a line (rung now): the definition, its receipt text, the amount off one, and whether tax is on
// the price before it (a promotion: its own box; a buydown: the store's setting). Never more than the price.
inline bool taxBeforeFor(const Buydown &b, bool storeTaxBefore) { return b.isPromo() ? b.taxBefore : storeTaxBefore; }
inline void apply(OrderLine &l, const Hit &h, bool storeTaxBefore)
{
    l.buydownId = h.def->id;
    l.buydownText = !h.def->receiptText.empty() ? h.def->receiptText : h.def->name;
    l.buydownEach = std::min(h.each, l.unitPrice);
    l.buydownTaxBefore = taxBeforeFor(*h.def, storeTaxBefore);
    if (l.buydownEach.cents() <= 0)
        clear(l);
}

// Part/Ctn from the item's pack/carton link: a single in a box of N is 1/N; a box (or anything else) is 0.
inline double partCtnFor(const MenuItem &m)
{
    for (const RecipeLine &rl : m.recipe)
        if (rl.ingredientId.starts_with("__parent__:") && rl.quantity >= 1.0)
            return rl.quantity > 1.0 ? 1.0 / rl.quantity : 0.0;
    return 0.0;
}

// "2026-10-01 00:00" -> true (a real date and time).
inline bool validWhen(const std::string &s)
{
    if (s.size() != 16 || s[4] != '-' || s[7] != '-' || s[10] != ' ' || s[13] != ':')
        return false;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15})
        if (s[std::size_t(i)] < '0' || s[std::size_t(i)] > '9')
            return false;
    const int mo = std::stoi(s.substr(5, 2)), d = std::stoi(s.substr(8, 2)), h = std::stoi(s.substr(11, 2)),
              mi = std::stoi(s.substr(14, 2));
    return mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h <= 23 && mi <= 59;
}

// What's wrong with a definition before it's saved (empty: nothing).
inline std::vector<std::string> problems(const Buydown &b)
{
    std::vector<std::string> out;
    if (b.name.empty())
        out.push_back("Give it a name.");
    if (b.amount.cents() <= 0)
        out.push_back("Type the amount per carton.");
    if (!b.start.empty() && !validWhen(b.start))
        out.push_back("The start date isn't a date.");
    if (!b.end.empty() && !validWhen(b.end))
        out.push_back("The end date isn't a date.");
    if (!b.start.empty() && !b.end.empty() && b.end < b.start)
        out.push_back("It ends before it starts.");
    if (b.capped() && b.qty < 1)
        out.push_back("Type the quantity: how many units get it.");
    if (b.style == 4 && b.end.empty())
        out.push_back("Old Style needs an end date.");
    return out;
}

// Two definitions' dates meet (an open start or end runs forever that way).
inline bool datesMeet(const Buydown &a, const Buydown &b)
{
    const std::string aEnd = a.style == 2 ? std::string() : a.end, bEnd = b.style == 2 ? std::string() : b.end;
    const bool aBeforeB = !aEnd.empty() && !b.start.empty() && aEnd < b.start;
    const bool bBeforeA = !bEnd.empty() && !a.start.empty() && bEnd < a.start;
    return !aBeforeB && !bBeforeA;
}

// Items of `b` also in another definition running at the same time: {UPC or name, the other's name}.
struct Overlap {
    std::string upc, item, other;
    bool operator==(const Overlap &) const = default;
};
inline std::vector<Overlap> overlaps(const std::vector<Buydown> &defs, const Buydown &b)
{
    std::vector<Overlap> out;
    for (const Buydown &o : defs) {
        if (o.id == b.id || !datesMeet(o, b))
            continue;
        for (const BuydownItem &i : b.items)
            for (const BuydownItem &j : o.items)
                if ((!i.itemId.empty() && i.itemId == j.itemId) || sameUpc(i.upc, j.upc)) {
                    out.push_back({i.upc, i.name, o.name});
                    break;
                }
    }
    return out;
}

inline int nextNumber(const std::vector<Buydown> &defs)
{
    int n = 0;
    for (const Buydown &b : defs)
        n = std::max(n, b.number);
    return n + 1;
}

// Copy to New: the same items, amount, text and dates, a new ID; the name says it's a copy until changed.
inline Buydown copyOf(const Buydown &b, const std::vector<Buydown> &defs)
{
    Buydown c = b;
    c.number = nextNumber(defs);
    c.id = "bd-" + std::to_string(c.number);
    c.name = b.name + " (copy)";
    c.createdBy.clear();
    c.changedBy.clear();
    c.createdAt = c.changedAt = 0;
    c.used = 0;   // a new one: none sold with it yet
    return c;
}

// Units with each definition on these checks (open ones: still to be paid), by definition id.
inline std::map<std::string, int> unitsOn(const std::vector<const Check *> &checks)
{
    std::map<std::string, int> out;
    for (const Check *c : checks)
        if (c && !c->training)
            for (const OrderLine &l : c->lines)
                if (!l.voided && !l.buydownId.empty())
                    out[l.buydownId] += l.counted();
    return out;
}

// --- the old per-item fields (Menu Builder's Buydown, recipe tags; before 2026-11-25) ------------------

// Turns the old tags (__buydown__ amount off one, __buydown_grp__:<group>, __buydown_start__ /
// __buydown_end__ as YYYYMMDD) into definitions: one per group, dates and carton amount. A pack (a single of a
// box of N) had its own amount: the carton's is N times it. The tags are taken off the items. Returns how
// many definitions were made.
inline int migrateTags(std::vector<MenuItem> &menu, std::vector<Buydown> &defs, const std::string &by = {},
                       std::int64_t at = 0)
{
    const auto ymd = [](double v, const char *time) -> std::string {
        const long long n = std::llround(v);
        if (n < 19700101 || n > 99991231)
            return {};
        char buf[24];
        std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lld %s", n / 10000, n / 100 % 100, n % 100, time);
        return buf;
    };
    std::map<std::string, std::size_t> made;   // group|start|end|carton cents -> index in defs
    int count = 0;
    for (MenuItem &m : menu) {
        double each = 0.0, start = 0.0, end = 0.0;
        std::string group;
        bool any = false;
        for (const RecipeLine &rl : m.recipe) {
            if (!rl.ingredientId.starts_with("__buydown"))
                continue;
            any = true;
            if (rl.ingredientId == "__buydown__")
                each = rl.quantity;
            else if (rl.ingredientId.starts_with("__buydown_grp__:"))
                group = rl.ingredientId.substr(16);
            else if (rl.ingredientId == "__buydown_start__")
                start = rl.quantity;
            else if (rl.ingredientId == "__buydown_end__")
                end = rl.quantity;
        }
        if (!any)
            continue;
        std::erase_if(m.recipe, [](const RecipeLine &rl) { return rl.ingredientId.starts_with("__buydown"); });
        if (each <= 0.0)
            continue;
        const double part = partCtnFor(m);
        const std::int64_t eachCents = std::llround(each * 100.0);
        const std::int64_t cartonCents = part > 0.0 ? std::llround(double(eachCents) / part) : eachCents;
        const std::string s = ymd(start, "00:00"), e = ymd(end, "23:59");
        const std::string key = group + "|" + s + "|" + e + "|" + std::to_string(cartonCents);
        auto it = made.find(key);
        if (it == made.end()) {
            Buydown b;
            b.number = nextNumber(defs);
            b.id = "bd-" + std::to_string(b.number);
            b.name = !group.empty() ? group : "BUYDOWN " + std::to_string(b.number);
            b.receiptText = (!group.empty() ? group : std::string("BUYDOWN")) + " SAVINGS!";
            b.amount = Money::fromCents(cartonCents);
            b.start = s;
            b.end = e;
            b.manufacturer = m.manufacturer;
            b.createdBy = b.changedBy = by.empty() ? std::string("(from the item buydowns)") : by;
            b.createdAt = b.changedAt = at;
            defs.push_back(b);
            it = made.emplace(key, defs.size() - 1).first;
            ++count;
        }
        defs[it->second].items.push_back({m.barcodes.empty() ? std::string() : m.barcodes.front(), m.id, part, m.name});
    }
    return count;
}

// --- the Buydowns report (what the store claims from the manufacturer) --------------------------------

// One UPC's sales in a definition: units (net of returns), packs and cartons apart, and the amount given.
struct SoldLine {
    std::string upc, itemId, name;
    double partCtn = 0.0;
    std::int64_t units = 0;
    Money amount;
};
struct Sold {
    std::string defId;
    std::vector<SoldLine> lines;   // by UPC
    std::int64_t units = 0, packs = 0, cartons = 0;
    double cartonEquivalents = 0.0;   // cartons + packs x their Part/Ctn
    Money amount;
};

// What each definition gave on these checks (closed, not training): by definition id.
inline std::map<std::string, Sold> sold(const std::vector<Check> &checks, const std::vector<Buydown> &defs,
                                        const std::vector<MenuItem> &menu)
{
    std::map<std::string, const MenuItem *> byId;
    for (const MenuItem &m : menu)
        byId[m.id] = &m;
    std::map<std::string, Sold> out;
    for (const Check &c : checks) {
        if (c.training || c.status != CheckStatus::Closed)
            continue;
        for (const OrderLine &l : c.lines) {
            if (l.voided || l.buydownId.empty())
                continue;
            const Money given = l.buydownTotal();
            const int units = l.counted();
            if (given.cents() == 0 && units == 0)
                continue;
            Sold &s = out[l.buydownId];
            s.defId = l.buydownId;
            const auto def = std::ranges::find(defs, l.buydownId, &Buydown::id);
            const MenuItem *m = byId.contains(l.itemId) ? byId[l.itemId] : nullptr;
            double part = 0.0;
            std::string upc = l.upc;
            if (def != defs.end() && m)
                if (const BuydownItem *e = entryFor(*def, *m, l.upc)) {
                    part = def->usePartCtn && e->partCtn < 1.0 ? e->partCtn : 0.0;
                    if (upc.empty())
                        upc = e->upc;
                }
            if (upc.empty() && m && !m->barcodes.empty())
                upc = m->barcodes.front();
            auto it = std::ranges::find_if(s.lines, [&](const SoldLine &x) { return x.itemId == l.itemId && sameUpc(x.upc, upc); });
            if (it == s.lines.end()) {
                s.lines.push_back({upc, l.itemId, m ? m->name : l.name, part, 0, Money()});
                it = s.lines.end() - 1;
            }
            it->units += units;
            it->amount += given;
            s.units += units;
            s.amount += given;
            if (part > 0.0) {
                s.packs += units;
                s.cartonEquivalents += double(units) * part;
            } else {
                s.cartons += units;
                s.cartonEquivalents += double(units);
            }
        }
    }
    for (auto &[id, s] : out)
        std::ranges::sort(s.lines, [](const SoldLine &a, const SoldLine &b) { return a.upc != b.upc ? a.upc < b.upc : a.name < b.name; });
    return out;
}

} // namespace buydown
} // namespace vt::core
