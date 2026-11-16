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
#include <string>
#include <vector>

namespace vt::core {

// The bar's sizes, in US fluid ounces: bottles (750ml, 1L, 1.75L, the 375ml and 187ml splits) and kegs
// (1/2, 1/4 and 1/6 barrel).
inline constexpr double kOz750ml = 25.4, kOz1L = 33.8, kOz1750ml = 59.2, kOz375ml = 12.7, kOz187ml = 6.3;
inline constexpr double kOzHalfBarrel = 1984, kOzQuarterBarrel = 992, kOzSixthBarrel = 661;
// Thousandths of an ounce (or of a bottle, a keg): money on ounces in exact integer math (Money::scaled).
inline std::int64_t milli(double v) { return std::llround(v * 1000.0); }

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

    bool low() const { return onHand <= lowAt; }
    bool bar() const { return !kind.empty(); }
    // `qty` of it (in its unit) at the system cost: a bottle good's from its cost per bottle, exactly.
    Money costOf(double qty) const
    {
        if (kind == "bottle" && packOz > 0 && packCost.cents() != 0)
            return packCost.scaled(milli(qty), milli(packOz));
        return cost.scaled(milli(qty), 1000);
    }
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
