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

TEST_CASE("Receiving: an allowance on a line lowers its cost, the tie-out and the vendor's last net cost",
          "[receiving][core]")
{
    recv::Receipt r;
    // 2 cartons at 131.97, an allowance of .60 a carton ("** Allowance on Above Item": 1.20-).
    r.lines.push_back({.kind = recv::kStock, .itemId = "pack", .perUom = 10, .qty = 2, .costCents = 13197, .allowanceCents = 60});
    // Out of stock: listed, adds nothing.
    r.lines.push_back({.kind = recv::kStock, .itemId = "cigar", .perUom = 1, .qty = 0, .costCents = 500, .allowanceCents = 25});
    const recv::Line &l = r.lines[0];
    CHECK(l.allowanceTotal() == 120);
    CHECK(l.extended() == 26274);
    CHECK(r.lines[1].extended() == 0);
    const recv::Totals t = r.totals();
    CHECK(t.stock == 26394);        // gross: before the allowance
    CHECK(t.allowances == -120);
    CHECK(t.total == 26274);
    CHECK(t.units == 20);
    r.typedTotal = 26274;
    CHECK(*r.offBy() == 0);
    // Net cost per pack: (131.97 - .60) / 10.
    CHECK(*l.unitCostCents() == (13197.0 - 60) / 10);
    CHECK(l.unitAllowanceCents() == 6.0);
    // The vendor's last cost: the price and the allowance both kept; comparisons use the net.
    recv::VendorItem v{.vendorId = "acme", .itemId = "pack"};
    recv::learn(v, l, "2026-11-20", "A1");
    CHECK(v.lastCostCents == 13197);
    CHECK(v.lastAllowanceCents == 60);
    CHECK(*v.lastNetCents() == 13137);
    CHECK(*v.lastUnitCents() == 1313.7);
}

TEST_CASE("Receiving: a file's allowance, a column or a row under its item; a quantity of 0", "[receiving][core]")
{
    // An allowance row under its item (no item # or UPC; it says allowance): onto the item above, never a line.
    const auto rows = recv::readCsv(
        "Invoice,Date,Item #,Description,Qty,Unit Price,Extended\n"
        "A-1,11/20/2026,555,RED KING CTN,2,131.97,263.94\n"
        "A-1,11/20/2026,,** Allowance on Above Item,,.60,1.20-\n"
        "A-1,11/20/2026,556,GOLD KING CTN,0,131.97,0.00\n"     // out of stock
        "A-1,11/20/2026,557,BLUE BOX,3,10.00,30.00\n"
        "A-1,11/20/2026,,ALLOWANCE,,,0.75-\n");                // no unit price: .75 over the item's 3
    const auto lines = recv::fileLines(rows, recv::headerRow(rows), recv::guessMapping(rows[0]));
    const auto invs = recv::group(lines);
    REQUIRE(invs.size() == 1);
    REQUIRE(invs[0].lines.size() == 3);
    CHECK(invs[0].lines[0].allowance == 60);
    CHECK(invs[0].lines[2].allowance == 25);
    CHECK(invs[0].typedTotal == 26394 - 120 + 3000 - 75);
    recv::Receipt r;
    for (const recv::FileLine &f : invs[0].lines)
        r.lines.push_back(recv::lineOf(f));
    r.typedTotal = invs[0].typedTotal;
    CHECK(r.lines[0].allowanceCents == 60);
    CHECK(r.lines[0].extended() == 26274);
    CHECK(r.lines[1].qty == 0);
    CHECK(r.lines[1].extended() == 0);
    CHECK(r.totals().allowances == -120 - 75);
    CHECK(*r.offBy() == 0);

    // An Allowance column (per unit).
    const auto rows2 = recv::readCsv(
        "Invoice,Item #,Description,Qty,Unit Price,Allowance,Extended\n"
        "B-1,555,RED KING CTN,2,131.97,0.60,263.94\n");
    const auto m2 = recv::guessMapping(rows2[0]);
    CHECK(m2.at("allowance") == 5);
    const auto invs2 = recv::group(recv::fileLines(rows2, 0, m2));
    REQUIRE(invs2.size() == 1);
    REQUIRE(invs2[0].lines.size() == 1);
    CHECK(recv::lineOf(invs2[0].lines[0]).extended() == 26274);
    CHECK(invs2[0].typedTotal == 26274);
}

TEST_CASE("Receiving: prepaid sales tax is one charge on the invoice; kept per unit before, lifted into one",
          "[receiving][core]")
{
    recv::Receipt r;
    r.lines.push_back({.kind = recv::kStock, .itemId = "pack", .perUom = 10, .qty = 2, .costCents = 6000});
    r.lines.push_back({.kind = recv::kPrepaid, .name = "Prepaid tax", .amountCents = 800});
    CHECK(recv::validKind(recv::kPrepaid));
    CHECK_FALSE(recv::itemKind(recv::kPrepaid));
    recv::Totals t = r.totals();
    CHECK(t.stock == 12000);
    CHECK(t.prepaid == 800);
    CHECK(t.total == 12800);
    CHECK(*r.lines[0].unitCostCents() == 600);   // never cost
    // Kept per unit on a line (an invoice from before): counted the same; lifted into one Prepaid line.
    std::vector<recv::Line> old = {{.kind = recv::kStock, .itemId = "pack", .perUom = 10, .qty = 2, .costCents = 6000, .prepaidCents = 400},
                                   {.kind = recv::kStock, .itemId = "box", .qty = 1, .costCents = 500, .prepaidCents = 50}};
    recv::Receipt before{.lines = old};
    CHECK(before.totals().prepaid == 850);
    recv::liftPrepaid(old);
    REQUIRE(old.size() == 3);
    CHECK(old[0].prepaidCents == 0);
    CHECK(old[2].kind == recv::kPrepaid);
    CHECK(old[2].amountCents == 850);
    recv::Receipt after{.lines = old};
    CHECK(after.totals().prepaid == 850);
    CHECK(after.totals().total == before.totals().total);
    recv::liftPrepaid(old);   // (nothing left to lift)
    CHECK(old.size() == 3);

    // A file: prepaid tax per unit, a prepaid total column, a prepaid tax row: one amount per invoice.
    const auto rows = recv::readCsv(
        "Invoice,Item #,Description,Qty,Unit Price,Prepaid Tax,Extended\n"
        "P-1,555,RED KING CTN,2,60.00,4.00,128.00\n"
        "P-1,556,GOLD KING CTN,1,60.00,4.00,64.00\n"
        "P-2,555,RED KING CTN,1,60.00,,60.00\n"
        "P-2,,PREPAID CIG TAX,,,,4.00\n");
    const auto invs = recv::group(recv::fileLines(rows, 0, recv::guessMapping(rows[0])));
    REQUIRE(invs.size() == 2);
    CHECK(invs[0].lines.size() == 2);
    CHECK(invs[0].prepaid == 1200);
    CHECK(invs[0].typedTotal == 19200);
    CHECK(recv::lineOf(invs[0].lines[0]).costCents == 6000);
    CHECK(recv::lineOf(invs[0].lines[0]).prepaidCents == 0);
    REQUIRE(recv::prepaidLine(invs[0]));
    CHECK(recv::prepaidLine(invs[0])->amountCents == 1200);
    CHECK(invs[1].lines.size() == 1);   // the row isn't a line of its own
    CHECK(invs[1].prepaid == 400);
    CHECK(invs[1].typedTotal == 6400);
    recv::Receipt p2;
    for (const recv::FileLine &f : invs[1].lines)
        p2.lines.push_back(recv::lineOf(f));
    p2.lines.push_back(*recv::prepaidLine(invs[1]));
    p2.typedTotal = invs[1].typedTotal;
    CHECK(*p2.offBy() == 0);
    const auto rows2 = recv::readCsv(
        "Invoice,Item #,Description,Qty,Unit Price,Extended,Prepaid Tax Total\n"
        "Q-1,555,RED KING CTN,2,60.00,120.00,8.00\n"
        "Q-1,556,GOLD KING CTN,1,60.00,60.00,8.00\n");
    const auto m2 = recv::guessMapping(rows2[0]);
    CHECK(m2.at("prepaidTotal") == 6);
    const auto invs2 = recv::group(recv::fileLines(rows2, 0, m2));
    REQUIRE(invs2.size() == 1);
    CHECK(invs2[0].prepaid == 800);   // once, though on both rows
    CHECK(invs2[0].typedTotal == 18800);
    CHECK_FALSE(recv::prepaidLine(recv::FileInvoice{}));
}
