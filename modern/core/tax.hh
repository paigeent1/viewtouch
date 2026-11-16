#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif


#include <cstdint>
#include <string>

namespace vt::core {

// Tax classes carried over from ViewTouch: each class is taxed on the total of
// its taxable sales (not per item), rounded half away from zero, like the
// legacy SubCheck::FigureTotals / FltToPrice.
enum class TaxClass { None, Food, Alcohol, Merchandise, Room };

inline constexpr TaxClass kTaxClasses[] = {TaxClass::Food, TaxClass::Alcohol, TaxClass::Merchandise,
                                           TaxClass::Room};

struct TaxRates {
    // Parts per million: 82500 = 8.25%, 99750 = 9.975%.
    std::int64_t foodPpm = 0;
    std::int64_t alcoholPpm = 0;
    std::int64_t merchandisePpm = 0;
    std::int64_t roomPpm = 0;
    // Legacy tax_takeout_food: when false, takeout food is not taxed.
    bool taxTakeoutFood = true;
    // Where pennies are gone: a check paid in cash is rounded to the
    // nearest 5 (or 10) cents. 0: no rounding. Cards are never rounded.
    int cashRoundingCents = 0;
    // Prices include tax (a bar: $5 is what the guest pays). Off: tax is added on top (retail).
    // On: each tax class's tax is backed out of that class's part of the check total (after
    // discounts and comps), never line by line: subtotal = round(part / (1 + rate)),
    // tax = part - subtotal, so the guest pays exactly the menu prices.
    bool pricesIncludeTax = false;

    constexpr std::int64_t ratePpm(TaxClass c) const
    {
        switch (c) {
        case TaxClass::Food: return foodPpm;
        case TaxClass::Alcohol: return alcoholPpm;
        case TaxClass::Merchandise: return merchandisePpm;
        case TaxClass::Room: return roomPpm;
        case TaxClass::None: break;
        }
        return 0;
    }

    // Takeout food untaxed (taxTakeoutFood off): no tax on it, added or included.
    constexpr std::int64_t ratePpmFor(TaxClass c, bool takeout) const
    {
        if (c == TaxClass::Food && takeout && !taxTakeoutFood)
            return 0;
        return ratePpm(c);
    }

    bool operator==(const TaxRates &) const = default;
};

std::string toString(TaxClass c);
TaxClass taxClassFromString(const std::string &s);   // unknown -> Food

} // namespace vt::core
