#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif


#include "core/money.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vt::core {

// The bar's sizes, in US fluid ounces: bottles (750ml, 1L, 1.75L, the 375ml and 187ml splits) and kegs
// (1/2, 1/4 and 1/6 barrel).
inline constexpr double kOz750ml = 25.4, kOz1L = 33.8, kOz1750ml = 59.2, kOz375ml = 12.7, kOz187ml = 6.3;
inline constexpr double kOzHalfBarrel = 1984, kOzQuarterBarrel = 992, kOzSixthBarrel = 661;
// Thousandths of an ounce (or of a bottle, a keg): money on ounces in exact integer math (Money::scaled).
inline std::int64_t milli(double v) { return std::llround(v * 1000.0); }
// A bottle of `ml` in ounces, to a tenth (375 -> 12.7, 187 -> 6.3, 750 -> 25.4).
inline double ozOfMl(double ml) { return std::round(ml / 29.5735 * 10.0) / 10.0; }
// Two bottle sizes the same (to the tenth of an ounce they're kept in).
inline bool sameBottleSize(double a, double b) { return std::abs(a - b) < 0.05; }

// A bottle size as the store says it: "750ml", "1L", "1.75L", "375ml", "50ml"; others in oz ("20 oz").
inline std::string bottleSizeLabel(double oz)
{
    static const struct { int ml; const char *text; } known[] = {
        {50, "50ml"}, {100, "100ml"}, {187, "187ml"}, {200, "200ml"}, {375, "375ml"}, {500, "500ml"},
        {700, "700ml"}, {750, "750ml"}, {1000, "1L"}, {1500, "1.5L"}, {1750, "1.75L"}, {3000, "3L"}};
    for (const auto &k : known)
        if (sameBottleSize(ozOfMl(k.ml), oz))
            return k.text;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g oz", std::round(oz * 10) / 10);
    return buf;
}

// "750ml", "750 ML", "1L", "1.75 L", "1.75 liter", "25.4oz", "25.4 oz"; a bare number is liters up to 5
// (1.75), ml from 50 (750). Its ounces; 0: not a size.
inline double parseBottleSize(std::string_view text)
{
    std::string t;
    for (char c : text)
        if (c != ' ' && c != '\t')
            t += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    std::size_t n = 0;
    while (n < t.size() && ((t[n] >= '0' && t[n] <= '9') || t[n] == '.'))
        ++n;
    if (n == 0)
        return 0;
    char *end = nullptr;
    const std::string num = t.substr(0, n);
    const double v = std::strtod(num.c_str(), &end);
    if (end != num.c_str() + num.size() || !(v > 0) || !std::isfinite(v))
        return 0;
    const std::string unit = t.substr(n);
    double oz = 0;
    if (unit == "ml" || unit == "mls")
        oz = ozOfMl(v);
    else if (unit == "l" || unit == "liter" || unit == "liters" || unit == "litre" || unit == "litres" || unit == "lt" || unit == "ltr")
        oz = ozOfMl(v * 1000);
    else if (unit == "oz" || unit == "ounce" || unit == "ounces" || unit == "floz")
        oz = std::round(v * 10) / 10;
    else if (unit.empty() && v <= 5)
        oz = ozOfMl(v * 1000);
    else if (unit.empty() && v >= 50)
        oz = ozOfMl(v);
    return oz > 0 && oz <= 1000 ? oz : 0;
}

// A size a bottle good also comes in (the bar: the same liquor as 750ml, 1L and 1.75L), beside its main size
// (Ingredient::packOz, packCost, perCase): its cost per bottle, bottles in its case, and its barcodes (a scan
// at receiving picks the size). It all goes into the same ounces on hand.
struct BottleSize {
    double oz = 0;
    Money cost;                     // per bottle (0: not known)
    int perCase = 0;
    std::vector<std::string> upcs;
    std::string label() const { return bottleSizeLabel(oz); }
    bool operator==(const BottleSize &) const = default;
};

// Manager -> Inventory's "Other sizes", a size a line: "1.75L 42.99 6 012345678905" (the size, its cost per
// bottle, bottles in its case, its UPCs; all but the size may be left off).
inline std::string bottleSizesText(const std::vector<BottleSize> &sizes)
{
    std::string out;
    for (const BottleSize &s : sizes) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s %lld.%02lld %d", s.label().c_str(), (long long)(s.cost.cents() / 100),
                      (long long)(s.cost.cents() % 100), s.perCase);
        out += buf;
        for (const std::string &u : s.upcs)
            out += ' ' + u;
        out += '\n';
    }
    return out;
}

// The lines above back into sizes; false and `bad` (the line) when one isn't a size.
inline bool parseBottleSizes(std::string_view text, std::vector<BottleSize> *out, std::string *bad)
{
    out->clear();
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t nl = text.find('\n', at);
        if (nl == std::string_view::npos)
            nl = text.size();
        const std::string_view line = text.substr(at, nl - at);
        at = nl + 1;
        std::vector<std::string> words;
        std::string w;
        for (char c : line) {
            if (c == ' ' || c == '\t' || c == ',' || c == '\r') {
                if (!w.empty())
                    words.push_back(w);
                w.clear();
            } else {
                w += c;
            }
        }
        if (!w.empty())
            words.push_back(w);
        if (words.empty())
            continue;
        // "1.75 L": the unit apart from the number.
        if (words.size() > 1 && parseBottleSize(words[0] + words[1]) > 0 && parseBottleSize(words[1]) == 0
            && words[1].find_first_of("0123456789") == std::string::npos) {
            words[0] += words[1];
            words.erase(words.begin() + 1);
        }
        BottleSize s;
        s.oz = parseBottleSize(words[0]);
        bool ok = s.oz > 0;
        int numbers = 0;
        for (std::size_t i = 1; ok && i < words.size(); ++i) {
            std::string t = words[i];
            if (!t.empty() && t[0] == '$')
                t.erase(0, 1);
            const bool digits = !t.empty() && t.find_first_not_of("0123456789") == std::string::npos;
            if (digits && t.size() >= 8) {
                s.upcs.push_back(t);
                continue;
            }
            char *end = nullptr;
            const double v = std::strtod(t.c_str(), &end);
            if (t.empty() || end != t.c_str() + t.size() || !std::isfinite(v) || v < 0 || v > 100000) {
                ok = false;
            } else if (numbers == 0) {
                s.cost = Money::fromCents(std::llround(v * 100));
                ++numbers;
            } else if (numbers == 1 && digits && v <= 1000) {
                s.perCase = int(v);
                ++numbers;
            } else {
                ok = false;
            }
        }
        if (!ok) {
            if (bad)
                *bad = std::string(line);
            return false;
        }
        out->push_back(std::move(s));
    }
    return true;
}

// Something the kitchen uses up: buns, patties, oz of coffee... Stock goes
// down as recipes are sent to the kitchen and back up on a void; a manager
// counts or receives it in Manager -> Inventory.
struct Ingredient {
    std::string id;
    std::string name;
    std::string unit = "each";   // each, oz, lb, slice...
    double onHand = 0;
    double lowAt = 0;            // low-stock warning at or below this
    Money cost;                  // per unit, for food cost
    std::string vendor;          // who it usually comes from (a Vendor id)
    // How long it keeps once prepped (hours), for its prep label's use-by;
    // 0: asked each time.
    int shelfLifeHours = 0;
    // Kept on hand: below it, the Stock report's order list asks for more
    // (up to this). 0: not ordered by par.
    double par = 0;
    // Since it was last counted (a stock count): what there was then, and
    // what came in, went out in what was sold or thrown away (the waste
    // log), or was changed by hand. What should be there is
    // onHand = start + received - sold - wasted + adjusted.
    std::int64_t countedAt = 0;  // 0: never counted
    double start = 0;
    double received = 0;
    double sold = 0;
    double wasted = 0;
    double adjusted = 0;
    // The bar (HCA): kind "bottle" is liquor or wine, kept in ounces (unit "oz"; packOz a bottle, 25.4 for a
    // 750ml); "beer" is bottled beer, counted in bottles (unit "bottle"); "keg" is a beer in a keg size,
    // counted in kegs (unit "keg"; packOz the keg, beer its beer). Empty: a kitchen ingredient. perCase:
    // bottles in a case (received by the case); category: its group on the shelf count (Vodka, Red Wine).
    // packCost: a bottle good's system cost per bottle (exact; `cost` is that per ounce, rounded).
    std::string kind;
    double packOz = 0;
    int perCase = 0;
    std::string category;
    std::string beer;
    Money packCost;
    // A bottle good: the main size's barcodes, and other sizes it comes in (none: one size, as before). The
    // cost per oz (`cost`, pours' cost, food cost) stays the main size's.
    std::vector<std::string> upcs;
    std::vector<BottleSize> sizes;

    bool low() const { return onHand <= lowAt; }
    bool bar() const { return !kind.empty(); }
    // `qty` of it (in its unit) at the system cost: a bottle good's from its cost per bottle, exactly.
    Money costOf(double qty) const
    {
        if (kind == "bottle" && packOz > 0 && packCost.cents() != 0)
            return packCost.scaled(milli(qty), milli(packOz));
        return cost.scaled(milli(qty), 1000);
    }
    // The other size of `oz` (nullptr: none; the main size is packOz).
    const BottleSize *otherSize(double oz) const
    {
        for (const BottleSize &s : sizes)
            if (sameBottleSize(s.oz, oz))
                return &s;
        return nullptr;
    }
    BottleSize *otherSize(double oz) { return const_cast<BottleSize *>(std::as_const(*this).otherSize(oz)); }
    bool operator==(const Ingredient &) const = default;
};

// Who the store buys from (Manager -> Vendors).
struct Vendor {
    std::string id;
    std::string name;
    std::string phone;
    std::string account;   // the store's account number with them
    std::string note;      // delivery days, the rep...
    bool operator==(const Vendor &) const = default;
};

// A delivery received (Manager -> Inventory -> Receive a Delivery): what came,
// at what cost. Receiving adds it to stock; it never changes an item's system cost (that comes only
// from the item editor or the price book / ILSA sync). A cost typed while receiving is the invoice's,
// kept on the line beside the system cost then, for the Cost differences review.
struct Delivery {
    std::int64_t id = 0;
    std::int64_t at = 0;
    std::string vendorId;
    std::string vendorName;
    std::string invoice;   // their invoice number
    std::string by;
    struct Line {
        std::string ingredientId;
        std::string name;
        std::string unit;
        double qty = 0;
        Money unitCost;          // the invoice's cost each (as typed; none typed: the system cost)
        Money systemCost;        // the item's system cost when it came (systemCostKnown: older lines didn't keep it)
        bool systemCostKnown = false;
        // By the bottle (a bottle good, kept in oz): how many bottles (qty is their ounces), the invoice's
        // cost per bottle and the system's then. 0 bottles: by the unit, as above.
        double packs = 0;
        Money packCost;
        Money packSystemCost;
        // The bottle size received, in oz (a good bought in more than one size); 0: its main size then.
        double packOz = 0;
        // A plus/minus line (owner's rule, retail and bar): "damaged" (came broken: less) or "swap" (one
        // product sent back for another). Only these may be less than nothing.
        std::string reason;
        Money total() const
        {
            return packs != 0 ? packCost.scaled(milli(packs), 1000)
                              : Money::fromCents(std::llround(double(unitCost.cents()) * qty));
        }
        // Invoice cost less system cost (each, or each bottle); 0 when the same or not known.
        Money costDifference() const
        {
            return !systemCostKnown ? Money() : packs != 0 ? packCost - packSystemCost : unitCost - systemCost;
        }
        bool operator==(const Line &) const = default;
    };
    std::vector<Line> lines;

    Money total() const
    {
        Money t;
        for (const Line &l : lines)
            t += l.total();
        return t;
    }
    bool operator==(const Delivery &) const = default;
};

// Counting what's on the shelves (Manager -> Inventory -> Count Stock): each
// item counted, against what should be there by the sales and deliveries
// since its last count. The difference is waste, portions, theft or mistakes.
struct StockCount {
    std::int64_t id = 0;
    std::int64_t at = 0;
    std::string by;
    struct Line {
        std::string ingredientId;
        std::string name;
        std::string unit;
        std::int64_t since = 0;   // its last count (0: never)
        double start = 0;
        double received = 0;
        double sold = 0;
        double adjusted = 0;
        double expected = 0;      // start + received - sold - wasted + adjusted
        double counted = 0;
        Money unitCost;
        double wasted = 0;        // thrown away, and logged (already out of `expected`)
        double off() const { return counted - expected; }   // < 0: missing, not explained
        Money value() const { return Money::fromCents(std::llround(double(unitCost.cents()) * off())); }
        Money wasteValue() const { return Money::fromCents(std::llround(double(unitCost.cents()) * wasted)); }
        bool operator==(const Line &) const = default;
    };
    std::vector<Line> lines;

    Money wasteValue() const
    {
        Money t;
        for (const Line &l : lines)
            t += l.wasteValue();
        return t;
    }
    Money value() const
    {
        Money t;
        for (const Line &l : lines)
            t += l.value();
        return t;
    }
    bool operator==(const StockCount &) const = default;
};

// Thrown away, and said so (Log Waste): a dropped burger (its recipe), two
// pounds of lettuce gone bad. It comes off what's on hand, and the Stock
// report tells it apart from what went missing without a word.
struct Waste {
    std::int64_t id = 0;
    std::int64_t at = 0;
    std::string by;
    std::string what;     // the item or ingredient, as named then
    double qty = 0;
    std::string unit;     // an ingredient's; empty for a menu item
    std::string reason;
    struct Line {
        std::string ingredientId;
        std::string name;
        std::string unit;
        double qty = 0;
        Money unitCost;
        Money value() const { return Money::fromCents(std::llround(double(unitCost.cents()) * qty)); }
        bool operator==(const Line &) const = default;
    };
    std::vector<Line> lines;

    Money value() const
    {
        Money t;
        for (const Line &l : lines)
            t += l.value();
        return t;
    }
    bool operator==(const Waste &) const = default;
};

// A draft line at the bar (3 towers of 6: "T1-L1" ... "T3-L6") and the keg on it now. Each draft pour
// ("Guinness 16oz") comes out of the keg on its beer's line.
struct DraftLine {
    std::string id;
    std::string kegId;       // the keg good (Ingredient, kind "keg"); empty: nothing on it
    std::string kegName;
    std::string beer;
    double kegOz = 0;
    Money kegCost;           // its system cost when it went on
    std::int64_t tappedAt = 0;
    std::string tappedBy;
    double poured = 0;       // ounces rung up from it since
    double left() const { return kegOz - poured; }
    bool operator==(const DraftLine &) const = default;
};

// The 18 lines, in order.
inline const std::vector<std::string> &draftLineIds()
{
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> out;
        for (int t = 1; t <= 3; ++t)
            for (int l = 1; l <= 6; ++l)
                out.push_back("T" + std::to_string(t) + "-L" + std::to_string(l));
        return out;
    }();
    return ids;
}

// One keg, from going on a line to coming off (Change keg): what it held, what was rung up from it, and
// what was left in it (kicked: empty). The rest is the loss: foam, spills, free pours.
struct KegRecord {
    std::int64_t id = 0;
    std::string line;
    std::string kegId;
    std::string kegName;
    std::string beer;
    double kegOz = 0;
    Money kegCost;
    std::int64_t tappedAt = 0;
    std::string tappedBy;
    std::int64_t offAt = 0;
    std::string offBy;
    double poured = 0;
    double left = 0;
    bool kicked = false;
    double used() const { return std::max(0.0, kegOz - left); }
    double loss() const { return used() - poured; }
    double lossPercent() const { return used() > 0 ? 100.0 * loss() / used() : 0.0; }
    Money lossValue() const { return kegOz > 0 ? kegCost.scaled(milli(loss()), milli(kegOz)) : Money(); }
    bool operator==(const KegRecord &) const = default;
};

} // namespace vt::core
