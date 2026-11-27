#include <catch2/catch_test_macros.hpp>

#include "core/check.hh"
#include "core/transfer.hh"

#include <string>
#include <vector>

using namespace vt::core;

// Transfers between stores (core/transfer.hh): no Qt.

TEST_CASE("Transfers: a line is matched by UPC, else a carton by its packs", "[transfers][core]")
{
    // The receiving store has a pack (UPC 012345000016) and a cigar box, not the carton.
    const std::vector<std::vector<std::string>> codes = {{"012345000016"}, {"0099887766554"}};
    mass::UpcIndex here;
    here.build(codes.size(), [&](std::size_t i) { return codes[i]; });

    xfer::Line pack;
    pack.upc = "12345000016";   // without its leading zero
    CHECK(xfer::match(here, pack).at == 0);
    CHECK(xfer::match(here, pack).perUnit == 1);

    xfer::Line carton;
    carton.upc = "012345000023";   // not on file there
    carton.qty = 2;
    carton.packUpc = "012345000016";
    carton.packSize = 10;
    const xfer::Match m = xfer::match(here, carton);
    CHECK(m.at == 0);
    CHECK(m.perUnit == 10);   // 2 cartons in as 20 packs

    xfer::Line unknown;
    unknown.upc = "011111111117";
    CHECK(xfer::match(here, unknown).at == -1);
    unknown.packUpc = "022222222224";
    unknown.packSize = 20;
    CHECK(xfer::match(here, unknown).at == -1);
}

TEST_CASE("Transfers: totals at cost, differences, open", "[transfers][core]")
{
    xfer::Transfer t;
    t.status = "transit";
    t.lines = {{.upc = "1", .qty = 3, .costCents = 650}, {.upc = "2", .qty = 1.5, .costCents = 1000}};
    CHECK(t.sentCost() == 3 * 650 + 1500);
    CHECK(t.receivedCost() == 0);
    CHECK(t.differences() == 0);
    CHECK(t.open());
    t.lines[0].received = 2;   // one short
    t.lines[1].received = 1.5;
    CHECK(t.differences() == 1);
    CHECK(t.receivedCost() == 2 * 650 + 1500);
    t.status = "differences";
    CHECK_FALSE(t.open());
}

TEST_CASE("Transfers: the bar's markup on the retail store's price, rounded up to .49 / .99", "[transfers][core]")
{
    // 0% and no rounding: the same price.
    CHECK(xfer::markedUp(1012, 0, mass::Round::None) == 1012);
    // Rounding alone (the owner's examples).
    CHECK(xfer::markedUp(1012, 0, mass::Round::Up49or99) == 1049);
    CHECK(xfer::markedUp(1055, 0, mass::Round::Up49or99) == 1099);
    CHECK(xfer::markedUp(1049, 0, mass::Round::Up49or99) == 1049);
    CHECK(xfer::markedUp(1050, 0, mass::Round::Up49or99) == 1099);
    // 10% on 10.00 = 11.00 -> 11.49; 5% on 9.99 = 10.49 (10.4895) -> 10.49.
    CHECK(xfer::markedUp(1000, 10, mass::Round::None) == 1100);
    CHECK(xfer::markedUp(1000, 10, mass::Round::Up49or99) == 1149);
    CHECK(xfer::markedUp(999, 5, mass::Round::Up49or99) == 1049);
}

TEST_CASE("Transfers: a store's short name", "[transfers][core]")
{
    CHECK(xfer::storeKey("hca") == "HCA");
    CHECK(xfer::storeKey(" BH Dan ") == "BHDAN");
    CHECK(xfer::storeKey("bh-nm_2") == "BH-NM_2");
    CHECK(xfer::storeKey("  ") == "");
}

TEST_CASE("Transfers: the store price discount takes the markup off, even on a No discounts line", "[transfers][core]")
{
    MenuItem cigs;
    cigs.id = "cigs";
    cigs.name = "PACK";
    cigs.price = vt::Money::fromCents(1149);   // the bar's price: the retail store's 10.49 + its zone
    cigs.noDiscount = true;
    cigs.taxClass = TaxClass::Merchandise;
    MenuItem beer;
    beer.id = "beer";
    beer.name = "BEER";
    beer.price = vt::Money::fromCents(600);
    Check c;
    OrderLine &l = c.addItem(cigs);
    l.noDiscount = true;
    const std::int64_t lineId = l.id;
    c.addItem(beer);
    Tender t;
    t.id = kStorePriceTender;
    t.kind = TenderKind::Discount;
    t.name = "BH price";
    Payment &p = c.addPayment(t, vt::Money());
    p.amount = vt::Money::fromCents(100);   // (a discount's fixed amount is set on the payment)
    p.lineId = lineId;
    const TaxRates none;
    Totals tt = c.totals(none);
    CHECK(tt.discounts.cents() == 100);
    CHECK(tt.subtotal.cents() == 1149 + 600 - 100);
    // An ordinary 10% off the sale still only reaches the beer (the pack may not be discounted).
    Tender pct;
    pct.id = "subtotal-discount";
    pct.kind = TenderKind::Discount;
    pct.percentBp = 1000;
    c.addPayment(pct, vt::Money());
    tt = c.totals(none);
    CHECK(tt.discounts.cents() == 100 + 60);
    // Never more than the line.
    c.payments.front().amount = vt::Money::fromCents(5000);
    CHECK(c.totals(none).discounts.cents() == 1149 + 60);
}
