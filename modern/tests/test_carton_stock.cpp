#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "core/retail_stock.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
namespace stock = vt::core::stock;

// Retail on hand for a box / carton and its single (core/retail_stock.hh, app/pos_retail_stock.cpp; owner
// 2026-11-27): sealed and loose kept apart; a single sold opens a sealed one by itself only when none is loose;
// a box sold with none sealed comes out of loose; Open boxes for the humidor; receiving, transfers, voids; the
// counts kept before, settled once; items without a link keep their own count. Made-up items only.

namespace {

constexpr const char *kPack = "012345000016";
constexpr const char *kCarton = "012345000023";   // a carton of 10 packs
constexpr const char *kCigar = "077777000014";
constexpr const char *kBox = "077777000021";      // a box of 20 cigars
constexpr const char *kLighter = "066666000018";  // no link

core::MenuItem retailItem(const char *id, const char *name, const char *upc, const char *family, std::int64_t price, double cost)
{
    core::MenuItem m;
    m.id = id;
    m.name = name;
    m.family = family;
    m.taxClass = core::TaxClass::Merchandise;
    m.barcodes = {upc};
    m.price = Money::fromCents(price);
    m.kitchenHide = true;
    m.recipe = {{"__retail__", 1.0}, {"__cost__", cost}};
    return m;
}

app::PosData store()
{
    app::PosData d = test::seedPosData();
    d.settings.askReasons = false;
    for (const auto &[id, name] : {std::pair{"cigarettes", "CIGARETTES"}, {"cigars", "PREMIUM CIGARS"}}) {
        core::MenuCategory c;
        c.id = id;
        c.name = name;
        c.scope = "retail";
        c.retailOnly = true;
        c.taxClass = "merchandise";
        d.settings.menuCategories.push_back(c);
    }
    core::MenuItem pack = retailItem("pack", "ACME RED PACK", kPack, "cigarettes", 1049, 8.10);
    pack.recipe.push_back({std::string("__parent__:") + kCarton, 10});
    d.menu.push_back(pack);
    d.menu.push_back(retailItem("carton", "ACME RED CARTON", kCarton, "cigarettes", 9999, 81.00));
    core::MenuItem cigar = retailItem("cigar", "ACME ROBUSTO", kCigar, "cigars", 1250, 6.25);
    cigar.recipe.push_back({std::string("__parent__:") + kBox, 20});
    d.menu.push_back(cigar);
    d.menu.push_back(retailItem("box", "ACME ROBUSTO BOX/20", kBox, "cigars", 22000, 125.00));
    d.menu.push_back(retailItem("lighter", "ACME LIGHTER", kLighter, "cigarettes", 199, 0.50));
    return d;
}

QString text(PosService &pos, const char *id) { return pos.retailOnHandText(id); }

void sell(PosService &pos, const char *itemId, int times = 1)
{
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    for (int i = 0; i < times; ++i)
        REQUIRE(pos.addItem(QString::fromLatin1(itemId)));
    REQUIRE(pos.sendOrder());
}

bool logged(PosService &pos, const std::string &field)
{
    return std::ranges::any_of(pos.shared()->changesToday, [&](const core::Change &c) { return c.field.find(field) != std::string::npos; });
}

} // namespace

TEST_CASE("Carton stock rules: a single sold breaks a box only when none is loose; boxes from loose; open boxes", "[cartonstock]")
{
    // 10 ctn -> sell 1 pk -> 9 ctn 9 pk (the carton opened by itself).
    stock::Take t = stock::takeSingles({10, 0}, 1, 10);
    CHECK(t.after == stock::Counts{9, 9});
    CHECK(t.opened == 1);
    CHECK(stock::text(t.after, "ctn", "pk") == "9 ctn 9 pk");
    // Loose on the shelf: no carton opened.
    t = stock::takeSingles({10, 3}, 1, 10);
    CHECK(t.after == stock::Counts{10, 2});
    CHECK(t.opened == 0);
    // Cigars: more than a box loose is fine, and stays loose.
    t = stock::takeSingles({4, 27}, 1, 20);
    CHECK(t.after == stock::Counts{4, 26});
    CHECK(t.opened == 0);
    CHECK(stock::text(t.after, "box", "ea") == "4 box 26 ea");
    // Nothing sealed nor loose: below 0 loose.
    CHECK(stock::takeSingles({0, 0}, 2, 10).after == stock::Counts{0, -2});
    // Returned: back loose.
    CHECK(stock::takeSingles({9, 9}, -1, 10).after == stock::Counts{9, 10});

    // A box sold: sealed; none sealed but a box's worth loose: from loose; else sealed below 0.
    t = stock::takeParents({3, 45}, 1, 20);
    CHECK(t.after == stock::Counts{2, 45});
    t = stock::takeParents({0, 25}, 1, 20);
    CHECK(t.after == stock::Counts{0, 5});
    CHECK(t.fromLoose == 1);
    t = stock::takeParents({0, 5}, 1, 20);
    CHECK(t.after == stock::Counts{-1, 5});
    CHECK(t.short_ == 1);
    CHECK(stock::takeParents({2, 0}, -1, 10).after == stock::Counts{3, 0});

    // Open 2 boxes for the humidor: 6 box 5 ea -> 4 box 45 ea; then a box sold -> 3 box 45 ea.
    stock::Counts c{6, 5};
    REQUIRE(stock::openParents(c, 2, 20));
    CHECK(c == stock::Counts{4, 45});
    CHECK(stock::takeParents(c, 1, 20).after == stock::Counts{3, 45});
    CHECK_FALSE(stock::openParents(c, 5, 20));   // only 4 sealed

    CHECK(stock::text({10, 0}, "ctn", "pk") == "10 ctn");
    CHECK(stock::text({0, 5}, "ctn", "pk") == "5 pk");
    CHECK(stock::text({0, 0}, "ctn", "pk") == "0 ctn");
    CHECK(stock::value({4, 45}, 125.0, 6.25) == 4 * 125.0 + 45 * 6.25);
    // The counts kept before: a carton below 0 made up from loose, then whole cartons loose to sealed (not boxes).
    CHECK(stock::settle({-2, 100}, 10, true) == stock::Counts{8, 0});
    CHECK(stock::settle({-1, 47}, 20, false) == stock::Counts{0, 27});
}

TEST_CASE("Carton stock: counts sealed + loose, pack sales break cartons, carton sales, voids, unlinked items", "[cartonstock]")
{
    PosService pos(store(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const app::RetailFamily *f = pos.retailFamilyOf("pack");
    REQUIRE(f);
    CHECK(f->parentId == "carton");
    CHECK(pos.retailFamilyOf("carton") == f);
    CHECK(f->parentUnit == u"ctn"_s);
    CHECK(f->singleUnit == u"pk"_s);
    const app::RetailFamily *cf = pos.retailFamilyOf("box");
    REQUIRE(cf);
    CHECK(cf->parentUnit == u"box"_s);
    CHECK(cf->singleUnit == u"ea"_s);
    CHECK_FALSE(pos.retailFamilyOf("lighter"));

    // Count: 10 cartons, nothing loose; shown the same on the carton and the pack.
    REQUIRE(pos.retailSetCount(u"pack"_s, 10, 0));
    CHECK(text(pos, "pack") == u"10 ctn"_s);
    CHECK(text(pos, "carton") == u"10 ctn"_s);
    CHECK(logged(pos, "Counted"));

    // A pack sold: the carton opens by itself.
    sell(pos, "pack");
    CHECK(text(pos, "pack") == u"9 ctn 9 pk"_s);
    CHECK(pos.retailOnHand("carton") == 9.0);
    CHECK(pos.retailOnHand("pack") == 9.0);
    CHECK(logged(pos, "opened by itself"));
    // Another pack: from the loose ones.
    sell(pos, "pack");
    CHECK(text(pos, "carton") == u"9 ctn 8 pk"_s);
    // A carton sold: one sealed.
    sell(pos, "carton");
    CHECK(text(pos, "carton") == u"8 ctn 8 pk"_s);
    // Voided after it was sent: back sealed.
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    REQUIRE(pos.addItem(u"carton"_s));
    REQUIRE(pos.sendOrder());
    CHECK(text(pos, "carton") == u"7 ctn 8 pk"_s);
    REQUIRE(pos.voidItem());
    CHECK(text(pos, "carton") == u"8 ctn 8 pk"_s);
    // A pack voided: back loose.
    REQUIRE(pos.addItem(u"pack"_s));
    REQUIRE(pos.sendOrder());
    CHECK(text(pos, "pack") == u"8 ctn 7 pk"_s);
    REQUIRE(pos.voidItem());
    CHECK(text(pos, "pack") == u"8 ctn 8 pk"_s);

    // An item without a link keeps its own count.
    REQUIRE(pos.retailSetCount(u"lighter"_s, 12, 0));
    sell(pos, "lighter", 2);
    CHECK(pos.retailOnHand("lighter") == 10.0);
    CHECK(text(pos, "lighter") == u"10"_s);
    CHECK_FALSE(pos.retailOpenParents(u"lighter"_s, 1));

    // The item card's stock.
    REQUIRE(pos.retailStockShow(u"carton"_s));
    const QVariantMap card = pos.retailStockCard();
    CHECK(card[u"family"_s].toBool());
    CHECK(card[u"sealed"_s].toDouble() == 8);
    CHECK(card[u"loose"_s].toDouble() == 8);
    CHECK(card[u"text"_s].toString() == u"8 ctn 8 pk"_s);
}

TEST_CASE("Carton stock: cigars loose above a box, Open boxes, a box sold from loose", "[cartonstock]")
{
    PosService pos(store(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.retailSetCount(u"box"_s, 6, 5));
    CHECK(text(pos, "cigar") == u"6 box 5 ea"_s);
    REQUIRE(pos.retailOpenParents(u"cigar"_s, 2));
    CHECK(text(pos, "cigar") == u"4 box 45 ea"_s);
    CHECK(logged(pos, "Opened 2 box"));
    CHECK_FALSE(pos.retailOpenParents(u"box"_s, 5));   // only 4 sealed
    sell(pos, "box");
    CHECK(text(pos, "box") == u"3 box 45 ea"_s);
    // A cigar sold with plenty loose: no box opened.
    sell(pos, "cigar");
    CHECK(text(pos, "box") == u"3 box 44 ea"_s);
    // Worth at cost: sealed at the box's cost, loose at the cigar's.
    REQUIRE(pos.retailStockShow(u"cigar"_s));
    CHECK(pos.retailStockCard()[u"value"_s].toString().contains(u"650.00"_s));   // 3 x 125 + 44 x 6.25

    // No box sealed, 25 loose: a box sold comes out of loose (logged).
    REQUIRE(pos.retailSetCount(u"box"_s, 0, 25));
    sell(pos, "box");
    CHECK(text(pos, "box") == u"5 ea"_s);
    CHECK(logged(pos, "sold from loose"));
    // Then none: below 0, logged.
    sell(pos, "box");
    CHECK(text(pos, "box") == u"-1 box 5 ea"_s);
    CHECK(logged(pos, "below 0"));
}

TEST_CASE("Carton stock: receiving a carton adds sealed, a pack loose; transfers in cartons + packs", "[cartonstock]")
{
    app::PosData d = store();
    d.settings.vendors.push_back({"acme", "Acme Distributing", "", "1001", "", "", ""});
    d.settings.storeCode = "BH";
    d.settings.otherStores = {{"HCA", "pi@hca.example", true, false}};
    PosService pos(std::move(d), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.retailSetCount(u"carton"_s, 2, 3));

    REQUIRE(pos.recvOpen(u"receive"_s));
    REQUIRE(pos.recvSetHeader({{u"vendor"_s, u"acme"_s}, {u"invoice"_s, u"C-1"_s}}));
    REQUIRE(pos.recvScan(QString::fromLatin1(kCarton)));
    REQUIRE(pos.recvScan(QString::fromLatin1(kCarton)));   // 2 cartons
    REQUIRE(pos.recvScan(QString::fromLatin1(kPack)));     // and a pack
    const QVariantList lines = pos.retailReceiving()[u"lines"_s].toList();
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].toMap()[u"stockAs"_s].toString() == u"2 ctn"_s);
    CHECK(lines[1].toMap()[u"stockAs"_s].toString() == u"1 pk"_s);
    REQUIRE(pos.recvReceive(true));
    CHECK(text(pos, "pack") == u"4 ctn 4 pk"_s);

    // Transfer out: the carton and its packs on one line, 1 carton + 6 packs.
    REQUIRE(pos.transferOpen(u"out"_s));
    REQUIRE(pos.transferScan(QString::fromLatin1(kCarton)));
    REQUIRE(pos.transferScan(QString::fromLatin1(kPack)));
    QVariantList tl = pos.transfers()[u"lines"_s].toList();
    REQUIRE(tl.size() == 1);
    CHECK(tl[0].toMap()[u"qty"_s].toDouble() == 1);
    CHECK(tl[0].toMap()[u"loose"_s].toDouble() == 1);
    REQUIRE(pos.transferSetQty(0, 1, 6));
    REQUIRE(pos.transferSetTo(u"HCA"_s));
    REQUIRE(pos.transferSend());
    // 4 ctn 4 pk - 1 ctn = 3 ctn 4 pk; 6 packs with 4 loose: one carton opened -> 2 ctn 8 pk.
    CHECK(text(pos, "pack") == u"2 ctn 8 pk"_s);
    const auto &t = pos.shared()->transfers.back();
    REQUIRE(t.lines.size() == 2);
    CHECK(t.lines[0].itemId == "carton");
    CHECK(t.lines[0].qty == 1);
    CHECK(t.lines[1].itemId == "pack");
    CHECK(t.lines[1].qty == 6);
    // Voided: back as sent (the carton sealed, the packs loose).
    REQUIRE(pos.transferVoid(t.id));
    CHECK(text(pos, "pack") == u"3 ctn 14 pk"_s);
}

TEST_CASE("Carton stock: counts kept before (cartons received as packs, cartons sold below 0) settled once", "[cartonstock]")
{
    app::PosData d = store();
    d.retailStock["pack"] = 100;     // 10 cartons received as packs
    d.retailStock["carton"] = -2;    // 2 cartons sold as cartons
    d.retailStock["cigar"] = 47;     // 2 boxes received as cigars, 13 sold
    d.retailStock["box"] = -1;       // a box sold
    d.retailStock["lighter"] = 7;    // no link: as it is
    PosService pos(std::move(d), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    CHECK(text(pos, "pack") == u"8 ctn"_s);
    CHECK(text(pos, "box") == u"27 ea"_s);   // cigars stay loose (a box's worth made up the one sold)
    CHECK(pos.retailOnHand("lighter") == 7.0);
    CHECK(std::ranges::count_if(pos.shared()->changesToday, [](const core::Change &c) {
              return c.field.find("sealed and loose") != std::string::npos;
          }) == 2);
    // Once: the marker row is kept (in a store with records), so the next start leaves the counts alone.
    app::PosData again = store();
    again.retailStock["pack"] = 15;
    again.retailStock["__families_v2__"] = 1;
    PosService later(std::move(again), nullptr);
    REQUIRE(later.loginWithPin(u"1234"_s));
    CHECK(later.retailOnHand("pack") == 15.0);
    CHECK_FALSE(later.shared()->retailStock.contains("__families_v2__"));
}

TEST_CASE("Receive retail: a received invoice reopened; only the difference goes on hand (cartons sealed, packs "
          "loose); a line taken off comes back off; its changes kept; the same invoice # offers to add to it",
          "[receiving][cartonstock]")
{
    app::PosData d = store();
    d.settings.vendors.push_back({"acme", "Acme Distributing", "", "1001", "", "", ""});
    PosService pos(std::move(d), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.retailSetCount(u"carton"_s, 2, 3));
    REQUIRE(text(pos, "pack") == u"2 ctn 3 pk"_s);

    REQUIRE(pos.recvOpen(u"receive"_s));
    REQUIRE(pos.recvSetHeader({{u"vendor"_s, u"acme"_s}, {u"invoice"_s, u"R-1"_s}, {u"date"_s, u"2026-11-27"_s}, {u"total"_s, u"80.00"_s}}));
    REQUIRE(pos.recvScan(QString::fromLatin1(kCarton)));
    REQUIRE(pos.recvSetLine(0, {{u"cost"_s, u"80.00"_s}}));
    REQUIRE(pos.recvReceive(false));
    CHECK(text(pos, "pack") == u"3 ctn 3 pk"_s);
    REQUIRE(pos.retailReceipts().size() == 1);
    const std::int64_t id = pos.retailReceipts()[0].id;

    // Open > Edit: one more carton (the price corrected) and a pack: only those go on hand.
    REQUIRE(pos.recvOpen(u"list"_s));
    REQUIRE(pos.recvShowReceived(id));
    CHECK(pos.retailReceiving()[u"received"_s].toMap()[u"invoice"_s].toString() == u"R-1"_s);
    REQUIRE(pos.recvEditReceived(id));
    QVariantMap info = pos.retailReceiving();
    CHECK(info[u"editing"_s].toMap()[u"id"_s].toLongLong() == id);
    REQUIRE(info[u"lines"_s].toList().size() == 1);
    CHECK(pos.retailDrafts().size() == 1);   // (kept as unfinished while it's changed)
    CHECK_FALSE(pos.recvSetHeader({{u"vendor"_s, QString()}}));   // a received invoice keeps its vendor
    REQUIRE(pos.recvScan(QString::fromLatin1(kCarton)));   // 2 cartons now
    REQUIRE(pos.recvSetLine(0, {{u"cost"_s, u"79.00"_s}}));
    REQUIRE(pos.recvScan(QString::fromLatin1(kPack)));
    REQUIRE(pos.recvSetLine(1, {{u"cost"_s, u"8.50"_s}}));
    CHECK(text(pos, "pack") == u"3 ctn 3 pk"_s);   // nothing yet
    REQUIRE(pos.recvReceive(true));   // (off from the old total: asked, and yes)
    CHECK(text(pos, "pack") == u"4 ctn 4 pk"_s);
    REQUIRE(pos.retailReceipts().size() == 1);
    CHECK(pos.retailDrafts().empty());
    const core::recv::Receipt &r = pos.retailReceipts()[0];
    REQUIRE(r.lines.size() == 2);
    REQUIRE(r.edits.size() == 1);
    CHECK(r.edits[0].totalBefore == 8000);
    CHECK(r.edits[0].totalAfter == 2 * 7900 + 850);
    CHECK(r.edits[0].unitsBefore == 10);
    CHECK(r.edits[0].unitsAfter == 21);
    CHECK(r.edits[0].lines.size() == 2);
    CHECK_FALSE(r.edits[0].by.empty());
    // The list and the reports show the invoice as it is now.
    REQUIRE(pos.recvShowReceived(id));
    CHECK(pos.retailReceiving()[u"received"_s].toMap()[u"edits"_s].toList().size() == 1);
    const core::Report pr = pos.buildReport(u"retailpurchases"_s);
    bool changed = false;
    for (const core::ReportRow &row : pr.rows)
        for (const std::string &c : row.cells)
            changed = changed || c.find("R-1 (changed)") != std::string::npos;
    CHECK(changed);

    // Again: the pack taken off, one carton less: they come back off.
    REQUIRE(pos.recvEditReceived(id));
    REQUIRE(pos.recvRemoveLine(1));
    REQUIRE(pos.recvSetLine(0, {{u"qty"_s, u"1"_s}}));
    REQUIRE(pos.recvReceive(true));
    CHECK(text(pos, "pack") == u"3 ctn 3 pk"_s);
    CHECK(pos.retailReceipts()[0].edits.size() == 2);
    CHECK(pos.retailReceipts()[0].lines.size() == 1);
    // The vendor's last cost: the carton's, corrected (the same invoice: as new as the cost it had).
    REQUIRE(pos.vendorItemOf("acme", "pack"));
    CHECK(pos.vendorItemOf("acme", "pack")->lastCostCents == 7900);

    // The same vendor and invoice # as a new invoice: refused, with the offer to add to the one received.
    REQUIRE(pos.recvSetHeader({{u"vendor"_s, u"acme"_s}, {u"invoice"_s, u"r1"_s}}));
    CHECK(pos.retailReceiving()[u"duplicateOf"_s].toLongLong() == id);
    REQUIRE(pos.recvScan(QString::fromLatin1(kPack)));
    REQUIRE(pos.recvScan(QString::fromLatin1(kPack)));
    CHECK_FALSE(pos.recvReceive(true));
    const QVariantMap ask = pos.retailReceiving()[u"confirm"_s].toMap();
    CHECK(ask[u"kind"_s].toString() == u"duplicate"_s);
    CHECK(ask[u"receiptId"_s].toLongLong() == id);
    CHECK(ask[u"text"_s].toString().contains(u"already received"_s));
    CHECK(text(pos, "pack") == u"3 ctn 3 pk"_s);
    REQUIRE(pos.recvAddToReceived(id));
    REQUIRE(pos.retailReceiving()[u"lines"_s].toList().size() == 2);   // the carton line, and the packs added
    REQUIRE(pos.retailDrafts().size() == 1);   // (the new one became the change)
    REQUIRE(pos.recvReceive(true));
    CHECK(text(pos, "pack") == u"3 ctn 5 pk"_s);
    CHECK(pos.retailReceipts().size() == 1);
    CHECK(pos.retailReceipts()[0].edits.size() == 3);
    CHECK(pos.retailDrafts().empty());
}
