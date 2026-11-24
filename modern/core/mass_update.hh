#pragma once
// Mass Updates (Manager > Menu Builder > Mass Updates; owner 2026-11-24, after ILSA's "Mass Updates" screen):
// the retail items as a grid that the server narrows, sorts and pages (a price book is ~25,000 items, so
// the screen is sent one page), and changes to many items at once: a margin price, a price or cost added
// to or set, a field, prices copied. Each is shown first (old -> new), then applied as one change set:
// {UPC, field, before, after} with who, when and why, keyed by UPC so another store can apply it too.
// Plain C++ (no Qt): app/pos_mass_update.cpp uses it, tests/test_mass_update_core.cpp checks it.

#include "core/builder_index.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vt::core::mass {

// --- rounding (ILSA's "Round to") --------------------------------------------------------------------

enum class Round {
    None,     // as worked out
    Up5,      // "5 (up)": the next price ending in 5 cents, at or above (12.31 -> 12.35, 12.36 -> 12.45)
    Up9,      // "9 (up)": the next price ending in 9 cents, at or above (12.31 -> 12.39)
    Near9,    // "9 (up/down)": the nearest price ending in 9 cents (12.33 -> 12.29, 12.34 -> 12.39: a tie goes up)
    Up5or9,   // "5 or 9 (up)": the next price ending in 5 or 9 cents (12.31 -> 12.35, 12.36 -> 12.39)
};

inline std::optional<Round> roundFrom(std::string_view s)
{
    if (s.empty() || s == "none")
        return Round::None;
    if (s == "5up")
        return Round::Up5;
    if (s == "9up")
        return Round::Up9;
    if (s == "9near")
        return Round::Near9;
    if (s == "5or9up")
        return Round::Up5or9;
    return std::nullopt;
}

inline const char *toString(Round r)
{
    switch (r) {
    case Round::None: return "none";
    case Round::Up5: return "5up";
    case Round::Up9: return "9up";
    case Round::Near9: return "9near";
    case Round::Up5or9: return "5or9up";
    }
    return "none";
}

inline std::int64_t roundCents(std::int64_t cents, Round r)
{
    if (cents < 0)
        return cents;
    const std::int64_t last = cents % 10, tens = cents - last;
    switch (r) {
    case Round::None:
        return cents;
    case Round::Up5:
        return last <= 5 ? tens + 5 : tens + 15;
    case Round::Up9:
        return tens + 9;
    case Round::Near9: {
        if (last == 9)
            return cents;
        const std::int64_t up = tens + 9, down = tens - 1;
        if (down < 0)
            return up;
        return cents - down < up - cents ? down : up;
    }
    case Round::Up5or9:
        return last <= 5 ? tens + 5 : tens + 9;
    }
    return cents;
}

// --- margin and prices ---------------------------------------------------------------------------------

// The price for a margin on cost, as the Menu Builder card works it out (price = cost / (1 - margin/100),
// to the nearest cent), then rounded. -1: can't be (no cost, or a margin under 0 or at 100% or more).
inline std::int64_t marginPriceCents(std::int64_t costCents, double marginPct, Round r = Round::None)
{
    if (costCents <= 0 || !std::isfinite(marginPct) || marginPct < 0 || marginPct >= 100)
        return -1;
    return roundCents(std::llround(double(costCents) / (1.0 - marginPct / 100.0)), r);
}

// The margin of a price over its cost in percent ((price - cost) / price, as the card shows it); NaN: not
// known (no price or no cost).
inline double marginOf(std::int64_t priceCents, std::int64_t costCents)
{
    if (priceCents <= 0 || costCents <= 0)
        return std::numeric_limits<double>::quiet_NaN();
    return double(priceCents - costCents) * 100.0 / double(priceCents);
}

enum class How { AddDollars, AddPercent, Set };

inline std::optional<How> howFrom(std::string_view s)
{
    if (s == "add")
        return How::AddDollars;
    if (s == "percent")
        return How::AddPercent;
    if (s == "set")
        return How::Set;
    return std::nullopt;
}

// A price or cost after Add $ (`amount` dollars, less when negative), Add % (`amount` percent) or Set
// (`amount` dollars); never below 0; then rounded.
inline std::int64_t adjustCents(std::int64_t was, How how, double amount, Round r = Round::None)
{
    std::int64_t now = was;
    switch (how) {
    case How::AddDollars: now = was + std::llround(amount * 100.0); break;
    case How::AddPercent: now = std::llround(double(was) * (1.0 + amount / 100.0)); break;
    case How::Set: now = std::llround(amount * 100.0); break;
    }
    return roundCents(std::max<std::int64_t>(0, now), r);
}

// "12.99": cents as a plain amount (what a change set keeps: no currency sign).
inline std::string centsText(std::int64_t cents)
{
    const bool minus = cents < 0;
    const std::uint64_t c = minus ? std::uint64_t(-cents) : std::uint64_t(cents);
    std::string s = std::to_string(c / 100) + '.';
    s += char('0' + (c % 100) / 10);
    s += char('0' + c % 10);
    return minus ? "-" + s : s;
}
// "12.99", "$12.99", "12": cents; -1 if it isn't an amount.
inline std::int64_t centsFrom(std::string_view text)
{
    std::string t;
    for (char ch : text)
        if (ch != '$' && ch != ' ' && ch != ',')
            t += ch;
    if (t.empty())
        return -1;
    char *end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size() || !std::isfinite(v) || v < 0 || v >= 10'000'000)
        return -1;
    return std::llround(v * 100.0);
}

// --- the grid -------------------------------------------------------------------------------------------

// One retail item, as the grid knows it (made once per menu change).
struct Row {
    std::uint32_t at = 0;              // its place in the menu
    std::string id, upc, name, lowerName, number;
    std::string categoryId, category;  // its category (name); empty name: none
    std::string brand, group, sub;     // the tree below it (brand > product group > sub group)
    std::int64_t cost = 0, price = 0;  // cents (cost 0: none)
    bool soldOut = false;
    bool noCategory = false;           // in no category the store has set up
    std::int64_t firstSeen = 0;        // when this ViewTouch first had it (epoch ms; 0: before that was kept)
    std::vector<std::string> codes;    // its barcodes as BuilderIndex::codeKey keeps them
    double margin() const { return marginOf(price, cost); }
};

enum class Field { Upc, Name, Category, Brand, Group, Sub, Cost, Price, Margin, SoldOut };

inline std::optional<Field> fieldFrom(std::string_view s)
{
    static const std::pair<const char *, Field> all[] = {
        {"upc", Field::Upc}, {"name", Field::Name}, {"category", Field::Category}, {"brand", Field::Brand},
        {"group", Field::Group}, {"sub", Field::Sub}, {"cost", Field::Cost}, {"price", Field::Price},
        {"margin", Field::Margin}, {"soldOut", Field::SoldOut}};
    for (const auto &[name, f] : all)
        if (s == name)
            return f;
    return std::nullopt;
}

inline bool numeric(Field f) { return f == Field::Cost || f == Field::Price || f == Field::Margin; }

inline std::string lowerAscii(std::string_view s) { return BuilderIndex::asciiLower(s); }

inline std::string textOf(const Row &r, Field f)
{
    switch (f) {
    case Field::Upc: return r.upc;
    case Field::Name: return r.lowerName.empty() ? lowerAscii(r.name) : r.lowerName;
    case Field::Category: return lowerAscii(r.category);
    case Field::Brand: return lowerAscii(r.brand);
    case Field::Group: return lowerAscii(r.group);
    case Field::Sub: return lowerAscii(r.sub);
    case Field::SoldOut: return r.soldOut ? "1" : "0";
    default: return {};
    }
}
// Dollars (cost, price) or percent (margin); NaN: not known.
inline double numberOf(const Row &r, Field f)
{
    switch (f) {
    case Field::Cost: return double(r.cost) / 100.0;
    case Field::Price: return double(r.price) / 100.0;
    case Field::Margin: return r.margin();
    default: return std::numeric_limits<double>::quiet_NaN();
    }
}

// What the grid is narrowed to. Everything given must match.
struct Filter {
    std::string find;                    // trimmed, small letters: a name's part, an item number, a barcode
    std::string categoryId;              // one category (empty: all)
    std::vector<std::string> path;       // ...and in it this brand (> product group > sub group)
    bool noCategory = false;             // only items in no category
    std::int64_t newSince = 0;           // only items first had since then (epoch ms; 0: any)
    std::optional<Field> rangeField;     // a range on a field ("Apply range")
    std::string from, to;                // its start and end as typed (empty: open)
    const std::unordered_set<std::string> *only = nullptr;   // only these items (ids: the chosen ones)
};

namespace detail {
inline char upcCheckDigit(const std::string &eleven)
{
    int sum = 0;
    for (int i = 0; i < 11; ++i)
        sum += (eleven[std::size_t(10 - i)] - '0') * (i % 2 == 0 ? 3 : 1);
    return char('0' + (10 - sum % 10) % 10);
}
inline bool allDigits(std::string_view s)
{
    return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}
inline bool parse(const std::string &text, double &out)
{
    std::string t;
    for (char ch : text)
        if (ch != '$' && ch != '%' && ch != ' ' && ch != ',')
            t += ch;
    if (t.empty())
        return false;
    char *end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end == t.c_str() + t.size() && std::isfinite(out);
}
} // namespace detail

// Find: an item number or a barcode exactly (a UPC with or without its leading zeros or check digit), or
// part of the name. Eight digits or more is a barcode, never looked for in the names (as Menu Builder's).
inline bool found(const Row &r, std::string_view lowerText)
{
    if (lowerText.empty())
        return true;
    const std::string text(lowerText);
    if (!r.number.empty() && lowerAscii(r.number) == text)
        return true;
    if (text.size() >= 4) {
        const std::string k = BuilderIndex::codeKey(text);
        std::string k2;
        if (detail::allDigits(text) && text.size() == 11)
            k2 = BuilderIndex::codeKey(text + detail::upcCheckDigit(text));
        for (const std::string &c : r.codes)
            if (c == k || (!k2.empty() && c == k2))
                return true;
    }
    if (detail::allDigits(text) && text.size() >= 8)
        return false;
    const std::string &name = r.lowerName.empty() ? lowerAscii(r.name) : r.lowerName;
    return name.find(text) != std::string::npos;
}

// A range: numbers from..to (cost and price in dollars, margin in percent; an item whose margin isn't known
// is never in one); words from..to in A-Z order, the end taken as a start ("MONTE YEL" to "MONTE YEZ" has
// MONTE YELLOW; "A" to "B" has BRAVO too).
inline bool inRange(const Row &r, Field f, const std::string &from, const std::string &to)
{
    if (numeric(f)) {
        const double v = numberOf(r, f);
        if (!std::isfinite(v))
            return false;
        double a = 0, b = 0;
        if (detail::parse(from, a) && v < a - 1e-9)
            return false;
        if (detail::parse(to, b) && v > b + 1e-9)
            return false;
        return true;
    }
    const std::string v = textOf(r, f);
    const std::string a = lowerAscii(from), b = lowerAscii(to);
    if (!a.empty() && v < a)
        return false;
    if (!b.empty() && v.substr(0, b.size()) > b)
        return false;
    return true;
}

inline bool matches(const Row &r, const Filter &f)
{
    if (f.only && !f.only->contains(r.id))
        return false;
    if (f.noCategory && !r.noCategory)
        return false;
    if (!f.categoryId.empty() && r.categoryId != f.categoryId)
        return false;
    const std::string *levels[] = {&r.brand, &r.group, &r.sub};
    for (std::size_t d = 0; d < f.path.size() && d < 3; ++d)
        if (!f.path[d].empty() && *levels[d] != f.path[d])
            return false;
    if (f.newSince > 0 && r.firstSeen < f.newSince)
        return false;
    if (f.rangeField && (!f.from.empty() || !f.to.empty()) && !inRange(r, *f.rangeField, f.from, f.to))
        return false;
    return found(r, f.find);
}

// The rows that match, sorted by a field (A-Z, low to high; `descending` the other way), then by name.
// Places in `rows`.
inline std::vector<std::uint32_t> pick(const std::vector<Row> &rows, const Filter &f, Field sort, bool descending)
{
    std::vector<std::uint32_t> out;
    out.reserve(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (matches(rows[i], f))
            out.push_back(std::uint32_t(i));
    const auto byName = [&](std::uint32_t a, std::uint32_t b) {
        const Row &x = rows[a], &y = rows[b];
        const std::string &nx = x.lowerName.empty() ? x.name : x.lowerName;
        const std::string &ny = y.lowerName.empty() ? y.name : y.lowerName;
        return nx != ny ? nx < ny : x.at < y.at;
    };
    if (numeric(sort)) {
        std::ranges::stable_sort(out, [&](std::uint32_t a, std::uint32_t b) {
            double x = numberOf(rows[a], sort), y = numberOf(rows[b], sort);
            // (not known: first from low to high, last from high to low)
            if (!std::isfinite(x)) x = -1e18;
            if (!std::isfinite(y)) y = -1e18;
            if (x != y)
                return descending ? x > y : x < y;
            return byName(a, b);
        });
    } else if (sort == Field::Name) {
        std::ranges::stable_sort(out, [&](std::uint32_t a, std::uint32_t b) { return descending ? byName(b, a) : byName(a, b); });
    } else {
        // (each row's text once, not at every comparison)
        std::vector<std::string> key(rows.size());
        for (std::uint32_t i : out)
            key[i] = textOf(rows[i], sort);
        std::ranges::stable_sort(out, [&](std::uint32_t a, std::uint32_t b) {
            if (key[a] != key[b])
                return descending ? key[a] > key[b] : key[a] < key[b];
            return byName(a, b);
        });
    }
    return out;
}

// --- changes ---------------------------------------------------------------------------------------------

// One item's field from one value to another. `field`: "price", "cost" (plain amounts, "12.99"; cost ""
// none), "category" (its name), "taxClass", "soldOut" / "noDiscount" ("yes", "no"), "buttonColor".
struct Change {
    std::string upc, itemId, name, field, before, after;
    bool operator==(const Change &) const = default;
};
// An item left out, and why ("no cost", "not found"...).
struct Skip {
    std::string upc, name, why;
    bool operator==(const Skip &) const = default;
};
struct Plan {
    std::vector<Change> changes;
    std::vector<Skip> skipped;
};

// What a price or cost action does to these rows. kind "margin" (amount: the margin %, on cost: items with
// no cost are skipped), "price" (how: add, percent, set; rounded), "cost" (how; never rounded), "copy" (the
// price of `from`, a place in rows, to the others). Items whose value wouldn't change aren't listed.
struct Action {
    std::string kind;
    How how = How::Set;
    double amount = 0;
    Round round = Round::None;
    std::int64_t from = -1;
};

inline Plan plan(const std::vector<Row> &rows, const std::vector<std::uint32_t> &chosen, const Action &a)
{
    Plan p;
    const auto add = [&](const Row &r, const char *field, std::int64_t was, std::int64_t now, bool costField) {
        if (now == was)
            return;
        p.changes.push_back({r.upc, r.id, r.name, field, costField && was <= 0 ? std::string() : centsText(was),
                             costField && now <= 0 ? std::string() : centsText(now)});
    };
    const Row *source = a.kind == "copy" && a.from >= 0 && std::size_t(a.from) < rows.size() ? &rows[std::size_t(a.from)] : nullptr;
    for (std::uint32_t i : chosen) {
        if (i >= rows.size())
            continue;
        const Row &r = rows[i];
        if (a.kind == "margin") {
            if (r.cost <= 0) {
                p.skipped.push_back({r.upc, r.name, "no cost"});
                continue;
            }
            add(r, "price", r.price, marginPriceCents(r.cost, a.amount, a.round), false);
        } else if (a.kind == "price") {
            add(r, "price", r.price, adjustCents(r.price, a.how, a.amount, a.round), false);
        } else if (a.kind == "cost") {
            if (a.how != How::Set && r.cost <= 0) {
                p.skipped.push_back({r.upc, r.name, "no cost"});
                continue;
            }
            add(r, "cost", r.cost, adjustCents(r.cost, a.how, a.amount), true);
        } else if (a.kind == "copy" && source) {
            if (&r == source)
                continue;
            add(r, "price", r.price, source->price, false);
        }
    }
    return p;
}

// The same changes the other way (an Undo).
inline std::vector<Change> reversed(const std::vector<Change> &changes)
{
    std::vector<Change> out;
    out.reserve(changes.size());
    for (auto it = changes.rbegin(); it != changes.rend(); ++it)
        out.push_back({it->upc, it->itemId, it->name, it->field, it->after, it->before});
    return out;
}

// Items by barcode, for a change set from another store (its item ids aren't ours): codeKey -> place.
class UpcIndex {
public:
    template <typename CodesOf>
    void build(std::size_t count, CodesOf codesOf)
    {
        map_.clear();
        for (std::size_t i = 0; i < count; ++i)
            for (const std::string &c : codesOf(i))
                if (const std::string k = BuilderIndex::codeKey(c); !k.empty())
                    map_.try_emplace(k, std::uint32_t(i));
    }
    // Its place, or -1: a UPC with or without its leading zeros (or, 11 digits, its check digit).
    std::int64_t find(std::string_view upc) const
    {
        if (upc.empty())
            return -1;
        if (const auto it = map_.find(BuilderIndex::codeKey(upc)); it != map_.end())
            return it->second;
        const std::string s(upc);
        if (s.size() == 11 && detail::allDigits(s))
            if (const auto it = map_.find(BuilderIndex::codeKey(s + detail::upcCheckDigit(s))); it != map_.end())
                return it->second;
        return -1;
    }

private:
    std::unordered_map<std::string, std::uint32_t> map_;
};

} // namespace vt::core::mass
