#include <catch2/catch_test_macros.hpp>

#include "core/receiving.hh"

#include <string>
#include <vector>

using namespace vt::core;

// Retail receiving (core/receiving.hh): no Qt. Made-up vendors and amounts only.

TEST_CASE("Receiving: an invoice's totals and its tie-out", "[receiving][core]")
{
    recv::Receipt r;
    // 2 cartons of 10 packs at $60.00, prepaid tax $4.00 a carton.
    r.lines.push_back({.kind = recv::kStock, .itemId = "pack", .perUom = 10, .qty = 2, .costCents = 6000, .prepaidCents = 400});
    // A box of 20 cigars at $100.00 with $12.00 OTP; one free box (B1G1): a quantity at no cost.
    r.lines.push_back({.kind = recv::kStock, .itemId = "cigar", .perUom = 20, .qty = 1, .costCents = 10000, .otpCents = 1200});
    r.lines.push_back({.kind = recv::kStock, .itemId = "cigar", .perUom = 20, .qty = 1, .costCents = 0});
    // Store use: bags, $15.00 and $0.95 tax: never in on hand.
    r.lines.push_back({.kind = recv::kStoreUse, .name = "BAGS", .qty = 1, .costCents = 1500, .taxCents = 95});
    r.lines.push_back({.kind = recv::kDelivery, .amountCents = 500});
    r.lines.push_back({.kind = recv::kCredit, .amountCents = 250});   // typed positive: taken off
    const recv::Totals t = r.totals();
    CHECK(t.stock == 2 * 6000 + 10000 + 1200);
    CHECK(t.otp == 1200);
    CHECK(t.prepaid == 800);
    CHECK(t.storeUse == 1500);
    CHECK(t.useTax == 95);
    CHECK(t.delivery == 500);
    CHECK(t.credits == -250);
    CHECK(t.units == 20 + 20 + 20);   // store use isn't counted in
    CHECK(t.total == 23200 + 800 + 1500 + 95 + 500 - 250);
    CHECK_FALSE(r.offBy());
    r.typedTotal = 25845;
    REQUIRE(r.offBy());
    CHECK(*r.offBy() == 0);
    r.typedTotal = 25900;
    CHECK(*r.offBy() == 55);
    // A unit's cost with OTP (the box: per cigar).
    CHECK(*r.lines[1].unitCostCents() == (10000.0 + 1200) / 20);
    CHECK_FALSE(recv::Line{}.unitCostCents());
}

TEST_CASE("Receiving: a vendor item learns its number, unit and newest cost", "[receiving][core]")
{
    recv::VendorItem v{.vendorId = "acme", .itemId = "pack"};
    recv::Line l{.itemId = "pack", .vendorItem = "123456", .uom = "CTN", .perUom = 10, .qty = 1, .costCents = 6000, .otpCents = 0};
    recv::learn(v, l, "2026-10-01", "A1");
    CHECK(v.vendorItem == "123456");
    CHECK(v.uom == "CTN");
    CHECK(v.lastCostCents == 6000);
    CHECK(*v.lastUnitCents() == 600);
    // An older invoice loaded later: its cost doesn't come back.
    l.costCents = 5500;
    recv::learn(v, l, "2026-09-01", "A0");
    CHECK(v.lastCostCents == 6000);
    CHECK(v.lastInvoice == "A1");
    l.costCents = 6200;
    recv::learn(v, l, "2026-11-01", "A2");
    CHECK(v.lastCostCents == 6200);
    CHECK(recv::vendorItemKey("acme", "pack", "x") == recv::vendorItemKey("acme", "pack", "y"));
    CHECK(recv::vendorItemKey("acme", "", "00123") == recv::vendorItemKey("acme", "", "123"));
    CHECK(recv::numberKey("inv-00042") == "INV00042");
    CHECK(recv::numberKey("00042") == "42");
}

TEST_CASE("Receiving: amounts and dates as invoice files write them", "[receiving][core]")
{
    CHECK(*recv::centsOf("$1,234.50") == 123450);
    CHECK(*recv::centsOf("(3.00)") == -300);
    CHECK(*recv::centsOf("-3") == -300);
    CHECK(*recv::centsOf("3.00-") == -300);
    CHECK_FALSE(recv::centsOf(""));
    CHECK_FALSE(recv::centsOf("abc"));
    CHECK(*recv::numberOf("1,200") == 1200);
    CHECK(recv::dateOf("11/27/2026") == "2026-11-27");
    CHECK(recv::dateOf("11/27/26") == "2026-11-27");
    CHECK(recv::dateOf("2026-11-27") == "2026-11-27");
    CHECK(recv::dateOf("20261127") == "2026-11-27");
    CHECK(recv::dateOf("27-Nov-2026") == "2026-11-27");
    CHECK(recv::dateOf("Nov 27, 2026") == "2026-11-27");
    CHECK(recv::dateOf("11/27/2026 0:00") == "2026-11-27");
    CHECK(recv::dateOf("46000") == "2025-12-09");
    CHECK(recv::dateOf("13/45/2026").empty());
    CHECK(recv::dateOf("soon").empty());
}

TEST_CASE("Receiving: a CSV file, its header guessed", "[receiving][core]")
{
    const auto rows = recv::readCsv("\xEF\xBB\xBF" "Acme Distributing purchase history\r\n\r\n"
                                    "Ship-To #,Invoice #,Inv Date,Item #,UPC,Description,UOM,Pack,Qty Shipped,Unit Price,"
                                    "OTP,Prepaid Tax,Extended,Type\r\n"
                                    "1001,A-1,11/01/2026,555,012345000023,\"RED, KING\",CTN,10,2,60.00,,4.00,128.00,\r\n");
    REQUIRE(rows.size() == 4);
    CHECK(rows[3][5] == "RED, KING");
    const int h = recv::headerRow(rows);
    REQUIRE(h == 2);
    const recv::Mapping m = recv::guessMapping(rows[2]);
    CHECK(m.at("account") == 0);
    CHECK(m.at("invoice") == 1);
    CHECK(m.at("date") == 2);
    CHECK(m.at("itemNo") == 3);
    CHECK(m.at("upc") == 4);
    CHECK(m.at("description") == 5);
    CHECK(m.at("uom") == 6);
    CHECK(m.at("perUom") == 7);
    CHECK(m.at("qty") == 8);
    CHECK(m.at("unitPrice") == 9);
    CHECK(m.at("otp") == 10);
    CHECK(m.at("prepaid") == 11);
    CHECK(m.at("extended") == 12);
    CHECK(m.at("lineType") == 13);
    CHECK(recv::guessField("Invoice Total").first == "invoiceTotal");
    CHECK(recv::guessField("Customer Number").first == "account");
    CHECK(recv::guessField("Freight").first == "delivery");
    CHECK(recv::guessField("Whatever").first.empty());
    // Ship-to beats customer when a file has both (two locations under one customer).
    const recv::Mapping both = recv::guessMapping({"Customer", "Ship To", "Qty", "Price"});
    CHECK(both.at("account") == 1);
    // Tabs.
    CHECK(recv::readCsv("a\tb\tc\n1\t2\t3\n")[1][2] == "3");
}

TEST_CASE("Receiving: a file's rows grouped into invoices by account and number", "[receiving][core]")
{
    const auto rows = recv::readCsv(
        "Account,Invoice,Date,Item,Description,Qty,Price,Extended,Type,Delivery\n"
        "1001,A-1,11/01/2026,555,RED KING,2,60.00,120.00,,5.00\n"
        "1001,A-1,11/01/2026,777,BAGS,1,15.00,15.00,Store use,5.00\n"
        "2002,A-1,11/01/2026,555,RED KING,1,60.00,60.00,,\n"   // the same number at the other location
        "1001,A-2,11/03/2026,555,RED KING,-1,60.00,-60.00,,\n"
        ",,,,FREIGHT,,,7.50,,\n"                                // freight, under A-2
        "\n");
    const int h = recv::headerRow(rows);
    REQUIRE(h == 0);
    const auto lines = recv::fileLines(rows, h, recv::guessMapping(rows[0]));
    const auto invs = recv::group(lines);
    REQUIRE(invs.size() == 3);
    CHECK(invs[0].account == "1001");
    CHECK(invs[0].invoice == "A-1");
    CHECK(invs[0].date == "2026-11-01");
    REQUIRE(invs[0].lines.size() == 2);
    CHECK(invs[0].delivery == 500);   // once, though on both rows
    CHECK(invs[0].typedTotal == 12000 + 1500 + 500);
    CHECK(recv::kindOf(invs[0].lines[1]) == recv::kStoreUse);
    CHECK(invs[1].account == "2002");
    CHECK(invs[1].typedTotal == 6000);
    REQUIRE(invs[2].lines.size() == 2);
    CHECK(recv::kindOf(invs[2].lines[0]) == recv::kCredit);
    CHECK(recv::kindOf(invs[2].lines[1]) == recv::kDelivery);
    CHECK(invs[2].typedTotal == -6000 + 750);
    // As invoice lines: the credit's amount, the freight's, a stock line's price.
    const recv::Line credit = recv::lineOf(invs[2].lines[0]);
    CHECK(credit.amountCents == 6000);
    CHECK(credit.extended() == -6000);
    CHECK(recv::lineOf(invs[2].lines[1]).amountCents == 750);
    const recv::Line stock = recv::lineOf(invs[0].lines[0]);
    CHECK(stock.kind == recv::kStock);
    CHECK(stock.costCents == 6000);
    CHECK(stock.qty == 2);
    CHECK(stock.vendorItem == "555");
    // No price column: the extended amount over the quantity, less OTP and prepaid tax.
    recv::FileLine f;
    f.hasQty = true;
    f.qty = 2;
    f.hasExtended = true;
    f.extended = 13000;
    f.prepaid = 400;
    CHECK(recv::lineOf(f).costCents == 6100);
}
