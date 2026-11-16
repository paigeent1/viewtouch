#include <catch2/catch_test_macros.hpp>

#include "core/price_check.hh"

using vt::Money;
using namespace vt::core::pricecheck;

// Menu Builder -> Price check: the math, in whole cents. CT: 6.35% sales, 7.35% meals and alcohol.

namespace {
Money usd(std::int64_t cents) { return Money::fromCents(cents); }
constexpr std::int64_t kSales = 63500;
constexpr std::int64_t kMeals = 73500;
} // namespace

TEST_CASE("Price check: $5.00 with 7.35% in it is 4.66 + 0.34", "[pricecheck]")
{
    const Included i = backOut(usd(500), kMeals);
    CHECK(i.preTax == usd(466));
    CHECK(i.tax == usd(34));
    // The way back: to net 4.66 before tax, charge 5.00 (the lowest price that does).
    CHECK(chargeToNet(usd(466), kMeals) == usd(500));
    CHECK(backOut(chargeToNet(usd(1000), kMeals), kMeals).preTax == usd(1000));
    CHECK(chargeToNet(usd(1000), kMeals) == usd(1073));   // 10.73 backs out to 9.9953 -> 10.00; 10.72 to 9.99
    // No tax: the price is what it nets.
    CHECK(backOut(usd(500), 0).tax == usd(0));
    CHECK(chargeToNet(usd(466), 0) == usd(466));
}

TEST_CASE("Price check: $12.99 + 6.35% is 0.82 tax, 13.81 to pay", "[pricecheck]")
{
    const Added a = addOn(usd(1299), kSales);
    CHECK(a.tax == usd(82));
    CHECK(a.guestPays == usd(1381));
    CHECK(addOn(usd(1299), 0).guestPays == usd(1299));
}

TEST_CASE("Price check: the price that makes the guest pay a round amount", "[pricecheck]")
{
    const ForGuest g = priceForGuest(usd(1400), kSales);
    CHECK(g.price == usd(1316));
    CHECK(g.guestPays == usd(1400));
    CHECK(addOn(g.price, kSales).guestPays == usd(1400));
    // Every round dollar to $60: the price found never comes to more than asked, and a cent more would.
    for (std::int64_t c = 100; c <= 6000; c += 100) {
        const ForGuest f = priceForGuest(usd(c), kSales);
        CHECK(f.guestPays <= usd(c));
        CHECK(addOn(f.price + usd(1), kSales).guestPays > usd(c));
    }
    // Prices include tax: rounding the guest price to a quarter, a half, a dollar.
    CHECK(roundTo(usd(1381), 25) == usd(1375));
    CHECK(roundTo(usd(1388), 25) == usd(1400));
    CHECK(roundTo(usd(1325), 50) == usd(1350));
    CHECK(roundTo(usd(1349), 100) == usd(1300));
    CHECK(roundTo(usd(1350), 100) == usd(1400));
    CHECK(roundTo(usd(1381), 0) == usd(1381));
}

TEST_CASE("Price check: card $13.50 at 4% cash discount is $12.96; a carton is 10 cash packs", "[pricecheck]")
{
    const Money cash = cashPrice(usd(1350), 400);
    CHECK(cash == usd(1296));
    CHECK(addOn(usd(1350), kSales).guestPays == usd(1436));   // 0.857 -> 0.86
    CHECK(addOn(cash, kSales).guestPays == usd(1378));        // 0.823 -> 0.82
    const Money carton = cartonPrice(cash);
    CHECK(carton == usd(12960));
    CHECK(carton == cash * 10);
    CHECK(addOn(carton, kSales).guestPays == usd(13783));     // 8.2296 -> 8.23
    // To the cent, halves away from zero: 9.99 at 3.5% = 9.640 -> 9.64; 10.01 at 2.5% = 9.75975 -> 9.76.
    CHECK(cashPrice(usd(999), 350) == usd(964));
    CHECK(cashPrice(usd(1001), 250) == usd(976));
}

TEST_CASE("Price check: 0% cash discount is the card price (the cash rows are hidden)", "[pricecheck]")
{
    CHECK(cashPrice(usd(1350), 0) == usd(1350));
    CHECK(cashPrice(usd(1350), -5) == usd(1350));
    CHECK(cashPrice(usd(1350), 10000) == usd(0));
}

TEST_CASE("Price check: margin before tax, at the card and the cash price", "[pricecheck]")
{
    CHECK(marginBp(usd(1350), usd(1100)) == 1852);   // 2.50 / 13.50 = 18.52%
    CHECK(marginBp(usd(1296), usd(1100)) == 1512);   // 1.96 / 12.96 = 15.12%
    CHECK(marginBp(usd(466), usd(100)) == 7854);     // a $5 drink at a bar: 4.66 before tax
    CHECK(marginBp(usd(0), usd(100)) == 0);
    CHECK(marginBp(usd(100), usd(150)) == -5000);
}
