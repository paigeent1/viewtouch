#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif

// System discounts (owner 2026-11-27, after ILSA's "System Discount Setup Form"): money off items in a group,
// by how many of them are on the check. "D26 LA AURORA CTGTOR: 1 or more, $25.00 off each, DIRTY DOZEN
// SAVINGS!", 1/1/2026 to 1/1/2027. A definition has a name (30 characters), dates, one type for all its levels
// ($ off each item, or % off each item), whether it counts any mix of its items or the same item only, whether
// tax is on the price before it, up to five levels (Qty, Or More, the amount, the receipt text) and its items by
// UPC. On an open check the highest level reached applies: Or More, every unit counted gets it; without, every
// full group of Qty does (3 at "Qty 2": 2 of them). Each line's share is a discount on that line
// (app/pos_system_discounts.cpp), worked out again whenever the check changes, kept as it was once it closes.
// Items marked No discounts (cigarettes), returns, gift cards, fees, weighed and shared lines never count.
// A line in two running definitions gets the one worth more to it (the same: the lower ID).
// Plain C++ (no Qt): tests/test_system_discount_core.cpp checks it.
//
// Dates and times are the store's local time as text, "YYYY-MM-DD HH:MM" (compared as text); an empty start:
// from always, an empty end: no end.

#include "core/buydown.hh"
#include "core/check.hh"
#include "core/menu.hh"
#include "core/money.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace vt::core {

struct SystemDiscountLevel {
    int qty = 0;                  // 0: level not used
    bool orMore = false;          // every unit from Qty up; off: every full group of Qty
    Money amount;                 // $ off each item
    std::int64_t percentBp = 0;   // % off each item (1000 = 10%)
    std::string receiptText;      // "DIRTY DOZEN SAVINGS!" (empty: the discount's name)
    bool operator==(const SystemDiscountLevel &) const = default;
};

struct SystemDiscountItem {
    std::string upc;      // how another store (or ILSA) finds it
    std::string itemId;   // this store's item (empty: not found here)
    std::string name;     // as it was when added
    bool operator==(const SystemDiscountItem &) const = default;
};

struct SystemDiscount {
    static constexpr int kMostLevels = 5;
    static constexpr int kNameLength = 30;   // ILSA's 20 plus 10 (owner 2026-11-27)
    static constexpr int kReceiptLength = 40;

    std::string id;           // "sd-7"
    int number = 0;           // the ID column
    std::string name;         // "D26 LA AURORA CTGTOR"
    bool percent = false;     // false: $ Off of Each Item; true: % Off of Each Item (one type for every level)
    bool sameItemOnly = false;   // count each item on its own (ILSA "Discount only applies to same item")
    bool taxBefore = false;   // tax on the price before the discount (off: on the reduced price)
    std::string start;        // "2026-01-01 00:00" (empty: from always)
    std::string end;          // "2027-01-01 00:00" (empty: no end)
    std::vector<SystemDiscountLevel> levels;   // up to five, in order
    std::vector<SystemDiscountItem> items;
    std::string ilsaId;       // the ILSA discount's ID, when it came from ILSA
    // Mix-Match (ILSA's price record "Mix-Match", owner 2026-11-27): kind "mixmatch" instead of the levels.
    // Starting with the sale of item # `mixStart`, the price of `mixCount` items changes, over and over: "2 for
    // $1.00" (59c items): start 2, change 1, replace price with $0.41; buy one get one: start 2, change 1, 100%
    // off; 50% off the third: start 3, change 1, 50%. The cheapest units counted are the ones changed.
    std::string kind;         // "" (quantity levels) or "mixmatch"
    int mixStart = 2;
    int mixCount = 1;
    std::string mixHow = "off";   // "off" ($ off each changed item), "percent", "price" (replace the price with)
    Money mixAmount;              // $ off, or the price it's replaced with
    std::int64_t mixPercentBp = 0;
    std::string mixText;          // the receipt line ("2 FOR $1.00"; empty: the name)
    std::string createdBy, changedBy;
    std::int64_t createdAt = 0, changedAt = 0;   // epoch ms
    bool operator==(const SystemDiscount &) const = default;
};

namespace sysdisc {

constexpr const char *kMixMatch = "mixmatch";
inline bool isMixMatch(const SystemDiscount &d) { return d.kind == kMixMatch; }

// Mix-Match: of `count` units counted, how many get the changed price. Every (start - 1 + change) units: the last
// `change` of them; a part group past item # start: as many as it reaches.
inline int mixChanged(const SystemDiscount &d, int count)
{
    const int start = std::max(1, d.mixStart), change = std::max(1, d.mixCount), cycle = start - 1 + change;
    if (count <= 0)
        return 0;
    const int rest = count % cycle;
    return count / cycle * change + std::clamp(rest - (start - 1), 0, change);
}

// Mix-Match: the amount off one changed unit priced `unit`.
inline Money mixOffEach(const SystemDiscount &d, Money unit)
{
    if (unit.cents() <= 0)
        return Money();
    if (d.mixHow == "percent")
        return Money::fromCents(std::llround(double(unit.cents()) * double(std::min<std::int64_t>(d.mixPercentBp, 10000)) / 10000.0));
    if (d.mixHow == "price")
        return d.mixAmount < unit ? unit - d.mixAmount : Money();
    return std::min(d.mixAmount, unit);
}

constexpr const char *kTender = "system-discount";   // the discount lines' tender id
constexpr const char *kRef = "sysdisc:";            // their reference: sysdisc:<definition id>:<line id>

enum class State { Coming, Running, Ended };

inline const char *stateName(State s) { return s == State::Coming ? "coming" : s == State::Running ? "running" : "ended"; }

inline State stateAt(const SystemDiscount &d, const std::string &now)
{
    if (!d.start.empty() && now < d.start)
        return State::Coming;
    if (!d.end.empty() && now >= d.end)
        return State::Ended;
    return State::Running;
}

// A level that does something: a quantity and an amount.
inline bool used(const SystemDiscount &d, const SystemDiscountLevel &l)
{
    return l.qty > 0 && (d.percent ? l.percentBp > 0 : l.amount.cents() > 0);
}

// The level for `count` units counted (index into levels; -1: none reached): the one with the largest Qty
// that `count` reaches.
inline int levelFor(const SystemDiscount &d, int count)
{
    int best = -1;
    for (int i = 0; i < int(d.levels.size()); ++i) {
        const SystemDiscountLevel &l = d.levels[std::size_t(i)];
        if (used(d, l) && count >= l.qty && (best < 0 || l.qty >= d.levels[std::size_t(best)].qty))
            best = i;
    }
    return best;
}

// How many of `count` units the level covers: Or More, all of them; else every full group of Qty.
inline int unitsCovered(const SystemDiscountLevel &l, int count)
{
    if (l.qty <= 0 || count < l.qty)
        return 0;
    return l.orMore ? count : count / l.qty * l.qty;
}

// The amount off one unit priced `unit`: $ off (never more than the unit), or % off to the cent.
inline Money offEach(const SystemDiscount &d, const SystemDiscountLevel &l, Money unit)
{
    if (unit.cents() <= 0)
        return Money();
    if (d.percent)
        return Money::fromCents(std::llround(double(unit.cents()) * double(std::min<std::int64_t>(l.percentBp, 10000)) / 10000.0));
    return std::min(l.amount, unit);
}

inline const SystemDiscountItem *entryFor(const SystemDiscount &d, const MenuItem &m, std::string_view rungUpc = {})
{
    for (const SystemDiscountItem &i : d.items)
        if (!i.itemId.empty() && i.itemId == m.id)
            return &i;
    for (const SystemDiscountItem &i : d.items) {
        if (i.upc.empty())
            continue;
        if (!rungUpc.empty() && buydown::sameUpc(i.upc, rungUpc))
            return &i;
        for (const std::string &code : m.barcodes)
            if (buydown::sameUpc(i.upc, code))
                return &i;
    }
    return nullptr;
}

// A line that may count toward a system discount.
inline bool mayCount(const OrderLine &l)
{
    return !l.voided && !l.isComment() && !l.isGiftCard() && !l.isFee() && !l.isReturn() && !l.noDiscount
           && !l.isShare() && l.weight == 0 && l.quantity > 0;
}

// One line's discount: which definition, how much off the line, its receipt text and tax choice.
struct LineOff {
    std::string defId;
    std::int64_t lineId = 0;
    Money amount;
    std::string text;
    bool taxBefore = false;
    int number = 0;   // the definition's ID (ties: the lower)
    bool operator==(const LineOff &) const = default;
};

using ItemOf = std::function<const MenuItem *(const OrderLine &)>;

// What each line of `lines` gets now: by line id, the definition worth the most to it.
inline std::vector<LineOff> wanted(const std::vector<SystemDiscount> &defs, const std::vector<OrderLine> &lines,
                                   const ItemOf &itemOf, const std::string &now)
{
    std::map<std::int64_t, LineOff> best;
    std::vector<std::pair<const OrderLine *, const MenuItem *>> eligible;
    for (const OrderLine &l : lines)
        if (mayCount(l))
            if (const MenuItem *m = itemOf(l); m && !m->isModifier && !m->noDiscount)
                eligible.emplace_back(&l, m);
    if (eligible.empty())
        return {};
    for (const SystemDiscount &d : defs) {
        if (d.items.empty() || stateAt(d, now) != State::Running)
            continue;
        // The lines it counts, grouped (any mix: one group; same item only: one per item), in the order rung.
        std::map<std::string, std::vector<const OrderLine *>> groups;
        for (const auto &[l, m] : eligible)
            if (entryFor(d, *m, l->upc))
                groups[d.sameItemOnly ? l->itemId : std::string()].push_back(l);
        for (const auto &[key, group] : groups) {
            int count = 0;
            for (const OrderLine *l : group)
                count += l->quantity;
            if (isMixMatch(d)) {
                // The cheapest units get the changed price (the lower id first on a tie).
                std::vector<std::pair<Money, const OrderLine *>> units;
                for (const OrderLine *l : group)
                    for (int u = 0; u < l->quantity; ++u)
                        units.emplace_back(Money::fromCents(l->total().cents() / std::max(1, l->quantity)), l);
                std::ranges::stable_sort(units, [](const auto &a, const auto &b) { return a.first < b.first; });
                std::map<const OrderLine *, Money> perLine;
                const int changed = mixChanged(d, count);
                for (int i = 0; i < changed && i < int(units.size()); ++i)
                    perLine[units[std::size_t(i)].second] += mixOffEach(d, units[std::size_t(i)].first);
                for (const auto &[l, total] : perLine) {
                    Money off = total;
                    const Money room = l->total() - l->buydownTotal();
                    if (off > room)
                        off = room;
                    if (off.cents() <= 0)
                        continue;
                    LineOff lo{d.id, l->id, off, !d.mixText.empty() ? d.mixText : d.name, d.taxBefore, d.number};
                    const auto it = best.find(l->id);
                    if (it == best.end() || off > it->second.amount || (off == it->second.amount && d.number < it->second.number))
                        best[l->id] = lo;
                }
                continue;
            }
            const int li = levelFor(d, count);
            if (li < 0)
                continue;
            const SystemDiscountLevel &lv = d.levels[std::size_t(li)];
            int left = unitsCovered(lv, count);
            for (const OrderLine *l : group) {
                if (left <= 0)
                    break;
                const int units = std::min(left, l->quantity);
                left -= units;
                const Money unit = Money::fromCents(l->total().cents() / std::max(1, l->quantity));
                // Never more than the line less its buydown.
                const Money room = l->total() - l->buydownTotal();
                Money off = offEach(d, lv, unit) * units;
                if (off > room)
                    off = room;
                if (off.cents() <= 0)
                    continue;
                LineOff lo{d.id, l->id, off, !lv.receiptText.empty() ? lv.receiptText : d.name, d.taxBefore, d.number};
                const auto it = best.find(l->id);
                if (it == best.end() || off > it->second.amount || (off == it->second.amount && d.number < it->second.number))
                    best[l->id] = lo;
            }
        }
    }
    std::vector<LineOff> out;
    for (const auto &[id, lo] : best)
        out.push_back(lo);
    return out;
}

// "sysdisc:sd-3:17"
inline std::string reference(const LineOff &o) { return std::string(kRef) + o.defId + ":" + std::to_string(o.lineId); }

// What's wrong with a definition before it's saved (empty: nothing).
inline std::vector<std::string> problems(const SystemDiscount &d)
{
    std::vector<std::string> out;
    if (d.name.empty())
        out.push_back("Give it a name.");
    if (int(d.name.size()) > SystemDiscount::kNameLength)
        out.push_back("The name is longer than 30 characters.");
    if (!d.start.empty() && !buydown::validWhen(d.start))
        out.push_back("The start date isn't a date.");
    if (!d.end.empty() && !buydown::validWhen(d.end))
        out.push_back("The end date isn't a date.");
    if (!d.start.empty() && !d.end.empty() && d.end <= d.start)
        out.push_back("It ends before it starts.");
    if (isMixMatch(d)) {
        if (d.mixStart < 1 || d.mixCount < 1)
            out.push_back("Fill in which item starts it and how many items it changes.");
        else if (d.mixHow == "percent" ? (d.mixPercentBp <= 0 || d.mixPercentBp > 10000)
                 : d.mixHow == "price" ? d.mixAmount.cents() < 0 : d.mixAmount.cents() <= 0)
            out.push_back(d.mixHow == "percent" ? "Type a percent off from 1 to 100." : "Type the amount.");
        if (d.items.empty())
            out.push_back("Add the items it's for.");
        return out;
    }
    bool any = false;
    int lastQty = 0;
    for (const SystemDiscountLevel &l : d.levels) {
        if (l.qty <= 0)
            continue;
        if (!used(d, l)) {
            out.push_back(d.percent ? "A level with a quantity needs its percent off." : "A level with a quantity needs its amount off.");
            break;
        }
        if (d.percent && l.percentBp > 10000) {
            out.push_back("A percent off can't be more than 100.");
            break;
        }
        if (any && l.qty <= lastQty) {
            out.push_back("Each level needs a larger quantity than the one before it.");
            break;
        }
        any = true;
        lastQty = l.qty;
    }
    if (!any)
        out.push_back("Fill in level 1: its quantity and amount.");
    if (d.items.empty())
        out.push_back("Add the items it's for.");
    return out;
}

inline int nextNumber(const std::vector<SystemDiscount> &defs)
{
    int n = 0;
    for (const SystemDiscount &d : defs)
        n = std::max(n, d.number);
    return n + 1;
}

// Five levels, the ones not set empty.
inline void fillLevels(SystemDiscount &d)
{
    if (int(d.levels.size()) > SystemDiscount::kMostLevels)
        d.levels.resize(std::size_t(SystemDiscount::kMostLevels));
    while (int(d.levels.size()) < SystemDiscount::kMostLevels)
        d.levels.push_back({});
}

// Copy to New for several at once (D26 -> D27): each gets a new ID, the same items, levels and amounts, its name
// with `from` (at its start) changed to `to` (a name without it: "(copy)" after it), and the new dates
// (empty: kept). Names are cut to 30 characters.
inline std::vector<SystemDiscount> copies(const std::vector<SystemDiscount> &defs, const std::vector<std::string> &ids,
                                          const std::string &from, const std::string &to, const std::string &start,
                                          const std::string &end)
{
    std::vector<SystemDiscount> out;
    int n = nextNumber(defs);
    for (const SystemDiscount &d : defs) {
        if (std::ranges::find(ids, d.id) == ids.end())
            continue;
        SystemDiscount c = d;
        c.number = n++;
        c.id = "sd-" + std::to_string(c.number);
        if (!from.empty() && c.name.starts_with(from))
            c.name = to + c.name.substr(from.size());
        else
            c.name += " (copy)";
        if (int(c.name.size()) > SystemDiscount::kNameLength)
            c.name.resize(std::size_t(SystemDiscount::kNameLength));
        if (!start.empty())
            c.start = start;
        if (!end.empty())
            c.end = end;
        c.ilsaId.clear();
        c.createdBy.clear();
        c.changedBy.clear();
        c.createdAt = c.changedAt = 0;
        out.push_back(std::move(c));
    }
    return out;
}

// The items linked to these as a box and its sticks, or a carton and its packs (ILSA's "Add Purchase/Subunits"):
// a child's recipe line "__parent__:<parent UPC>" ties it to the parent. Ids not already in `have`.
inline std::vector<const MenuItem *> linkedItems(const std::vector<MenuItem> &menu, const std::set<std::string> &have)
{
    std::map<std::string, const MenuItem *> byUpc;
    for (const MenuItem &m : menu)
        for (const std::string &code : m.barcodes)
            if (const std::string k = buydown::upcKey(code); !k.empty())
                byUpc.emplace(k, &m);
    const auto parentOf = [&](const MenuItem &m) -> const MenuItem * {
        for (const RecipeLine &rl : m.recipe)
            if (rl.ingredientId.starts_with("__parent__:")) {
                const auto it = byUpc.find(buydown::upcKey(rl.ingredientId.substr(11)));
                return it == byUpc.end() ? nullptr : it->second;
            }
        return nullptr;
    };
    // One pass: each item's parent, and the parents of the items already in.
    std::vector<const MenuItem *> parents(menu.size(), nullptr);
    std::set<const MenuItem *> parentsOfHave;
    for (std::size_t i = 0; i < menu.size(); ++i) {
        parents[i] = parentOf(menu[i]);
        if (parents[i] && have.contains(menu[i].id))
            parentsOfHave.insert(parents[i]);
    }
    std::vector<const MenuItem *> out;
    std::set<std::string> added;
    for (std::size_t i = 0; i < menu.size(); ++i) {
        const MenuItem &m = menu[i];
        if (m.isModifier || have.contains(m.id))
            continue;
        const bool childOfOne = parents[i] && have.contains(parents[i]->id);
        if ((childOfOne || parentsOfHave.contains(&m)) && added.insert(m.id).second)
            out.push_back(&m);
    }
    return out;
}

} // namespace sysdisc
} // namespace vt::core
