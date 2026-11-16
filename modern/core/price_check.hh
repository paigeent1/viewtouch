#pragma once

// Menu Builder -> an item's card -> Price check: what a price comes to with tax, and the way back
// (the price that gives a round guest price, or nets an amount before tax). Only a calculator: checks
// are priced and tendered as always (core.cpp). All in whole cents, rounded half away from zero.

#include <cstdint>

#include "core/money.hh"

namespace vt::core::pricecheck {

// Prices include tax (a bar): the tax in one item's price, backed out. A check backs it out of its
// whole total, so a few items together can differ from the sum of these by a cent.
struct Included {
    Money preTax;
    Money tax;
};
constexpr Included backOut(Money price, std::int64_t ratePpm)
{
    const Money pre = price.withoutTaxPpm(ratePpm);
    return {pre, price - pre};
}

// Tax added on top (retail): the tax and what the guest pays.
struct Added {
    Money tax;
    Money guestPays;
};
constexpr Added addOn(Money price, std::int64_t ratePpm)
{
    const Money tax = ratePpm > 0 ? price.ppm(ratePpm) : Money();
    return {tax, price + tax};
}

// Prices include tax: the lowest menu price whose tax backed out leaves `net` (to net $4.66 at
// 7.35%, charge $5.00). Backing out moves by 0 or 1 cent a cent, so there always is one.
constexpr Money chargeToNet(Money net, std::int64_t ratePpm)
{
    if (ratePpm <= 0 || net.cents() <= 0)
        return net;
    const Money cent = Money::fromCents(1);
    Money y = net + net.ppm(ratePpm);
    while (y.withoutTaxPpm(ratePpm) < net)
        y += cent;
    while ((y - cent).withoutTaxPpm(ratePpm) >= net)
        y -= cent;
    return y;
}

// Tax added on top: the price whose price + tax is `guest` (to have the guest pay $14.00 at 6.35%,
// price it $13.16). Where no price gives exactly that (the tax can step 2 cents), the highest price
// under it; `guestPays` says what it comes to.
struct ForGuest {
    Money price;
    Money guestPays;
};
constexpr ForGuest priceForGuest(Money guest, std::int64_t ratePpm)
{
    if (guest.cents() <= 0)
        return {};
    if (ratePpm <= 0)
        return {guest, guest};
    const Money cent = Money::fromCents(1);
    const auto total = [ratePpm](Money p) { return p + p.ppm(ratePpm); };
    Money p = guest.withoutTaxPpm(ratePpm);
    while (p.cents() > 0 && total(p) > guest)
        p -= cent;
    while (total(p + cent) <= guest)
        p += cent;
    return {p, total(p)};
}

// To the nearest `stepCents` (25, 50, 100), halves up; 0 or 1: as it is.
constexpr Money roundTo(Money m, std::int64_t stepCents)
{
    if (stepCents <= 1 || m.cents() < 0)
        return m;
    return Money::fromCents((m.cents() + stepCents / 2) / stepCents * stepCents);
}

// The cash price: the card price less `discountBp` (400 = 4%), to the cent. 0: the card price.
constexpr Money cashPrice(Money card, std::int64_t discountBp)
{
    if (discountBp <= 0)
        return card;
    if (discountBp >= 10000)
        return Money();
    return card.scaled(10000 - discountBp, 10000);
}

// Cigarettes: a carton is 10 packs at the cash pack price, the same in cash or by card.
constexpr Money cartonPrice(Money cashPack) { return cashPack * 10; }

// The margin on a price before tax, in basis points (4000 = 40.00%); 0 without a price.
constexpr std::int64_t marginBp(Money net, Money cost)
{
    if (net.cents() <= 0)
        return 0;
    return (net - cost).scaled(10000, net.cents()).cents();
}

} // namespace vt::core::pricecheck
