#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"
#include "storage/async_writer.hh"
#include "storage/pos_store.hh"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Manager > Inventory > Receive retail (app/pos_receiving.cpp): an invoice scanned in (a carton's UPC brings its
// packs), store use, invoice amounts, the tie-out, codes not on file, duplicates refused; an invoice file's
// columns, invoices grouped and routed by account, history only; the reports. Receiving never changes a
// system cost or a price. Made-up vendors and amounts only.

namespace {

constexpr const char *kPack = "012345000016";     // a pack, the single of the carton
constexpr const char *kCarton = "012345000023";   // its carton of 10 (on the pack as its Parent UPC)
constexpr const char *kCigar = "077777000014";
constexpr const char *kUnknown = "099999000017";  // on no item

core::MenuItem retailItem(const char *id, const char *name, const char *upc, std::int64_t price, double cost)
{
    core::MenuItem m;
    m.id = id;
    m.name = name;
    m.family = "cigarettes";
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
    core::MenuCategory c;
    c.id = "cigarettes";
    c.name = "CIGARETTES";
    c.scope = "retail";
    c.retailOnly = true;
    c.taxClass = "merchandise";
    d.settings.menuCategories.push_back(c);
    core::MenuItem pack = retailItem("pack", "ACME RED PACK", kPack, 1049, 8.10);
    pack.recipe.push_back({std::string("__parent__:") + kCarton, 10});
    d.menu.push_back(pack);
    d.menu.push_back(retailItem("cigar", "ACME ROBUSTO", kCigar, 1250, 6.25));
    // One distributor, two accounts (two locations): two vendors in one group.
    d.settings.vendors.push_back({"acme-n", "Acme Distributing North", "", "1001", "", "Acme Distributing", ""});
    d.settings.vendors.push_back({"acme-s", "Acme Distributing South", "", "002002", "", "Acme Distributing", ""});
    return d;
}

QVariantList lines(PosService &pos) { return pos.retailReceiving()[u"lines"_s].toList(); }

std::int64_t costOf(const core::MenuItem *m)
{
    for (const core::RecipeLine &rl : m->recipe)
        if (rl.ingredientId == "__cost__")
            return std::llround(rl.quantity * 100);
    return 0;
}

bool reportHas(const core::Report &r, const std::string &text)
{
    for (const core::ReportRow &row : r.rows)
        for (const std::string &c : row.cells)
            if (c.find(text) != std::string::npos)
                return true;
    return false;
}

const char *kFile =
    "Acme Distributing invoice history\n"
    "Ship To,Invoice #,Invoice Date,Item #,UPC,Description,UOM,Qty Shipped,Unit Price,Prepaid Tax,Extended,Type\n"
    "1001,5001,11/20/2026,A100,012345000023,RED CARTON,CTN,3,60.00,4.00,192.00,\n"
    "1001,5001,11/20/2026,S200,,BAGS,EA,1,15.00,,15.00,Store use\n"
    "2002,5001,11/21/2026,A300,077777000014,ROBUSTO,EA,4,5.00,,20.00,\n"
    "2002,5002,11/22/2026,A999,099999000017,NEW THING,EA,2,3.00,,6.00,\n";

} // namespace

TEST_CASE("Receive retail: scan an invoice in, cartons as packs, store use, the tie-out; costs and prices unchanged",
          "[receiving]")
{
    PosService pos(store(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.recvOpen(u"receive"_s));
    REQUIRE(pos.recvSetHeader({{u"vendor"_s, u"acme-n"_s}, {u"invoice"_s, u"INV-1"_s}, {u"date"_s, u"11/27/2026"_s}}));
    CHECK(pos.retailReceiving()[u"date"_s].toString() == u"2026-11-27"_s);

    // The counter scanner: a carton twice, a pack once.
    REQUIRE(pos.recvScan(QString::fromLatin1(kCarton)));
    REQUIRE(pos.recvScan(QString::fromLatin1(kCarton)));
    REQUIRE(pos.recvScan(QString::fromLatin1(kPack)));
    QVariantList ls = lines(pos);
    REQUIRE(ls.size() == 2);
    CHECK(ls[0].toMap()[u"itemId"_s].toString() == u"pack"_s);
    CHECK(ls[0].toMap()[u"perUom"_s].toDouble() == 10);
    CHECK(ls[0].toMap()[u"qty"_s].toDouble() == 2);
    CHECK(ls[1].toMap()[u"perUom"_s].toDouble() == 1);
    REQUIRE(pos.recvSetLine(0, {{u"cost"_s, u"60.00"_s}, {u"prepaid"_s, u"4.00"_s}, {u"uom"_s, u"CTN"_s}, {u"vendorItem"_s, u"A100"_s}}));
    REQUIRE(pos.recvSetLine(1, {{u"cost"_s, u"6.00"_s}}));
    // A cigar, then marked store use: never on hand.
    REQUIRE(pos.recvScan(QString::fromLatin1(kCigar)));
    REQUIRE(pos.recvSetLine(2, {{u"kind"_s, u"storeuse"_s}, {u"cost"_s, u"5.00"_s}}));
    // Supplies with their tax; the delivery charge.
    REQUIRE(pos.recvAddCharge(u"storeuse"_s));
    REQUIRE(pos.recvSetLine(3, {{u"name"_s, u"BAGS"_s}, {u"cost"_s, u"15.00"_s}, {u"tax"_s, u"0.95"_s}}));
    REQUIRE(pos.recvAddCharge(u"delivery"_s));
    REQUIRE(pos.recvSetLine(4, {{u"amount"_s, u"5.00"_s}}));
    // 2 x (60 + 4) + 6 + 5 + 15 + 0.95 + 5 = 159.95
    REQUIRE(pos.recvSetHeader({{u"total"_s, u"160.00"_s}}));
    CHECK(pos.retailReceiving()[u"ties"_s].toBool() == false);
    CHECK(pos.retailReceiving()[u"tie"_s].toString().contains(u"0.05"_s));
    REQUIRE(pos.recvSetHeader({{u"total"_s, u"159.95"_s}}));
    CHECK(pos.retailReceiving()[u"ties"_s].toBool());

    // A code not on file: set aside; Save waits for it; linked, it becomes the item's alt UPC.
    REQUIRE(pos.recvScan(QString::fromLatin1(kUnknown)));
    ls = lines(pos);
    REQUIRE(ls.size() == 6);
    CHECK(ls[5].toMap()[u"aside"_s].toBool());
    CHECK_FALSE(pos.recvReceive(true));
    REQUIRE(pos.recvLinkStart(5));
    REQUIRE(pos.recvLink(5, u"cigar"_s));
    const core::MenuItem *cigar = pos.findItem(u"cigar"_s);
    REQUIRE(cigar);
    CHECK(std::ranges::find(cigar->barcodes, std::string(kUnknown)) != cigar->barcodes.end());
    REQUIRE(pos.recvSetLine(5, {{u"cost"_s, u"5.00"_s}}));
    REQUIRE(pos.recvSetHeader({{u"total"_s, u"164.95"_s}}));

    REQUIRE(pos.recvReceive(false));   // (it ties out: nothing asked)
    CHECK(pos.retailOnHand("pack") == 21.0);    // 2 cartons of 10 and a pack
    CHECK(pos.retailOnHand("cigar") == 1.0);    // the linked line; the store-use one isn't counted
    REQUIRE(pos.retailReceipts().size() == 1);
    const core::recv::Receipt &r = pos.retailReceipts().front();
    CHECK(r.vendorName == "Acme Distributing North");
    CHECK(r.date == "2026-11-27");
    CHECK(r.offBy() == 0);
    CHECK(r.totals().prepaid == 800);
    CHECK(r.totals().storeUse == 2000);
    CHECK(r.totals().useTax == 95);
    CHECK(r.totals().delivery == 500);
    CHECK(lines(pos).isEmpty());   // ready for the next invoice
    // Never the system cost or the price.
    CHECK(pos.findItem(u"pack"_s)->price.cents() == 1049);
    CHECK(costOf(pos.findItem(u"pack"_s)) == 810);
    CHECK(pos.findItem(u"cigar"_s)->price.cents() == 1250);
    CHECK(costOf(pos.findItem(u"cigar"_s)) == 625);
    // The vendor's last cost and item number.
    const core::recv::VendorItem *vi = pos.vendorItemOf("acme-n", "pack");
    REQUIRE(vi);
    CHECK(*vi->lastUnitCents() == 600);
    CHECK(vi->lastDate == "2026-11-27");
    REQUIRE(pos.vendorItemByNumber("acme-n", "A100"));
    CHECK(pos.vendorItemByNumber("acme-n", "A100")->itemId == "pack");

    // The same invoice again: refused.
    REQUIRE(pos.recvSetHeader({{u"invoice"_s, u"inv-1"_s}}));
    REQUIRE(pos.recvScan(QString::fromLatin1(kPack)));
    CHECK_FALSE(pos.recvReceive(true));
    CHECK(pos.retailReceiving()[u"confirm"_s].toMap()[u"kind"_s].toString() == u"duplicate"_s);   // (offers to open it)
    CHECK(pos.retailReceipts().size() == 1);
    CHECK(pos.retailOnHand("pack") == 21.0);
    // The vendor's item number typed: its item.
    REQUIRE(pos.recvClear());
    REQUIRE(pos.recvScan(u"A100"_s));
    REQUIRE(lines(pos).size() == 1);
    CHECK(lines(pos)[0].toMap()[u"itemId"_s].toString() == u"pack"_s);

    // The reports: purchases by vendor group and vendor; the cost differences (6.00 a pack vs 8.10).
    const core::Report pr = pos.buildReport(u"retailpurchases"_s);
    CHECK(reportHas(pr, "By vendor group"));
    CHECK(reportHas(pr, "Acme Distributing North"));
    CHECK(reportHas(pr, "INV-1"));
    const core::Report cd = pos.buildReport(u"retailcostdiffs"_s);
    CHECK(reportHas(cd, "ACME RED PACK"));
    CHECK(reportHas(cd, "-$2.10"));
}

TEST_CASE("Receive retail: an invoice file, both accounts, routed by account; history only; duplicates refused",
          "[receiving]")
{
    PosService pos(store(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.recvOpen(u"import"_s));
    REQUIRE(pos.recvImportText(QString::fromLatin1(kFile), u"acme.csv"_s));
    REQUIRE(pos.recvImportVendor(u"acme-n"_s));
    QVariantMap imp = pos.retailReceiving()[u"importFile"_s].toMap();
    REQUIRE(imp[u"loaded"_s].toBool());
    // The columns guessed from the header (the title row above it passed over).
    for (const QVariant &f : imp[u"fields"_s].toList()) {
        const QVariantMap m = f.toMap();
        if (m[u"field"_s] == u"account"_s)
            CHECK(m[u"column"_s].toInt() == 0);
        if (m[u"field"_s] == u"prepaid"_s)
            CHECK(m[u"column"_s].toInt() == 9);
        if (m[u"field"_s] == u"perUom"_s)
            CHECK(m[u"column"_s].toInt() == -1);
    }
    // Three invoices: 5001 at each account (two vendors), 5002 waiting for an item not on file.
    QVariantList invs = imp[u"invoices"_s].toList();
    REQUIRE(invs.size() == 3);
    CHECK(invs[0].toMap()[u"vendor"_s].toString() == u"Acme Distributing North"_s);
    CHECK(invs[0].toMap()[u"ties"_s].toBool());
    CHECK(invs[1].toMap()[u"vendor"_s].toString() == u"Acme Distributing South"_s);   // 2002 = 002002
    CHECK(invs[2].toMap()[u"ready"_s].toBool() == false);
    REQUIRE(imp[u"unmatched"_s].toList().size() == 1);
    const QString code = imp[u"unmatched"_s].toList()[0].toMap()[u"code"_s].toString();
    REQUIRE(pos.recvImportDecide(code, u"skip"_s, QString()));
    imp = pos.retailReceiving()[u"importFile"_s].toMap();
    CHECK(imp[u"ready"_s].toInt() == 3);

    REQUIRE(pos.recvImportCommit());
    REQUIRE(pos.retailReceipts().size() == 3);
    CHECK(pos.retailOnHand("pack") == 30.0);   // 3 cartons
    CHECK(pos.retailOnHand("cigar") == 4.0);
    const core::recv::Receipt &north = pos.retailReceipts()[0];
    CHECK(north.vendorId == "acme-n");
    CHECK(north.typedTotal == 20700);
    CHECK(north.offBy() == 0);
    CHECK(north.totals().storeUse == 1500);
    CHECK(pos.retailReceipts()[1].vendorId == "acme-s");
    CHECK(pos.retailReceipts()[2].lines[0].leftOut);
    // The columns kept for the vendor.
    const auto vendorN = std::ranges::find_if(pos.shared()->settings.vendors, [](const core::Vendor &v) { return v.id == "acme-n"; });
    CHECK(QJsonDocument::fromJson(QByteArray::fromStdString(vendorN->importMap)).object().value(u"prepaid").toString() == u"Prepaid Tax"_s);
    // The store-use item by number: store use from now on.
    REQUIRE(pos.vendorItemByNumber("acme-n", "S200"));
    CHECK(pos.vendorItemByNumber("acme-n", "S200")->storeUse);

    // The same file again: every invoice already received; nothing changes.
    REQUIRE(pos.recvImportText(QString::fromLatin1(kFile), u"acme.csv"_s));
    REQUIRE(pos.recvImportDecide(code, u"skip"_s, QString()));
    imp = pos.retailReceiving()[u"importFile"_s].toMap();
    CHECK(imp[u"duplicates"_s].toInt() == 3);
    CHECK(imp[u"ready"_s].toInt() == 0);
    REQUIRE(pos.recvImportCommit());
    CHECK(pos.retailReceipts().size() == 3);
    CHECK(pos.retailOnHand("pack") == 30.0);

    // History only: past invoices teach item numbers and costs; on hand stays; an older cost doesn't come back.
    const char *history =
        "Ship To,Invoice #,Invoice Date,Item #,UPC,Description,UOM,Qty Shipped,Unit Price,Extended\n"
        "1001,4001,06/01/2026,A100,012345000023,RED CARTON,CTN,5,58.00,290.00\n"
        "1001,4002,06/02/2026,C777,077777000014,ROBUSTO,EA,2,5.50,11.00\n";
    REQUIRE(pos.recvImportText(QString::fromLatin1(history), u"history.csv"_s));
    REQUIRE(pos.recvImportMode(u"history"_s));
    imp = pos.retailReceiving()[u"importFile"_s].toMap();
    REQUIRE(imp[u"ready"_s].toInt() == 2);
    REQUIRE(pos.recvImportCommit());
    CHECK(pos.retailReceipts().size() == 5);
    CHECK(pos.retailReceipts()[3].historyOnly);
    CHECK(pos.retailOnHand("pack") == 30.0);
    CHECK(pos.retailOnHand("cigar") == 4.0);
    REQUIRE(pos.vendorItemOf("acme-n", "pack"));
    CHECK(pos.vendorItemOf("acme-n", "pack")->lastCostCents == 6000);   // November's, not June's
    REQUIRE(pos.vendorItemByNumber("acme-n", "C777"));
    CHECK(pos.vendorItemByNumber("acme-n", "C777")->itemId == "cigar");
    CHECK(*pos.vendorItemOf("acme-n", "cigar")->lastUnitCents() == 550);
    // Never the system cost or the price.
    CHECK(costOf(pos.findItem(u"pack"_s)) == 810);
    CHECK(pos.findItem(u"pack"_s)->price.cents() == 1049);
    CHECK(costOf(pos.findItem(u"cigar"_s)) == 625);
    // History-only invoices are on Purchases, marked; not on Cost differences.
    const core::Report pr = pos.buildReport(u"retailpurchases"_s);
    CHECK(reportHas(pr, "(history)"));
    CHECK(reportHas(pr, "Acme Distributing South"));
    const core::Report cd = pos.buildReport(u"retailcostdiffs"_s);
    CHECK_FALSE(reportHas(cd, "#4001"));
    CHECK(reportHas(cd, "#5001"));   // the robusto at 5.00 vs 6.25
}

TEST_CASE("Receive retail: quick add an item not on file; a code left out isn't counted", "[receiving]")
{
    PosService pos(store(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.recvOpen(u"receive"_s));
    REQUIRE(pos.recvSetHeader({{u"vendor"_s, u"acme-s"_s}, {u"invoice"_s, u"77"_s}}));
    REQUIRE(pos.recvScan(QString::fromLatin1(kUnknown)));
    REQUIRE(pos.recvScan(QString::fromLatin1(kUnknown)));   // again: 2
    REQUIRE(pos.recvScan(u"088888000013"_s));
    REQUIRE(lines(pos).size() == 2);
    CHECK(lines(pos)[0].toMap()[u"qty"_s].toDouble() == 2);
    REQUIRE(pos.recvQuickAdd({{u"index"_s, 0}, {u"upc"_s, QString::fromLatin1(kUnknown)}, {u"name"_s, u"ACME NEW BOX"_s},
                              {u"category"_s, u"cigarettes"_s}, {u"cost"_s, u"40.00"_s}, {u"price"_s, u"79.99"_s},
                              {u"packSize"_s, 20}}));
    const QVariantMap l0 = lines(pos)[0].toMap();
    CHECK(l0[u"aside"_s].toBool() == false);
    CHECK(l0[u"perUom"_s].toDouble() == 20);
    REQUIRE(pos.recvLeaveOut(1, true));
    REQUIRE(pos.recvReceive(true));
    const QString id = lines(pos).isEmpty() ? QString::fromStdString(pos.retailReceipts().back().lines[0].itemId) : QString();
    REQUIRE_FALSE(id.isEmpty());
    const core::MenuItem *m = pos.findItem(id);
    REQUIRE(m);
    CHECK(m->name == "ACME NEW BOX");
    CHECK(m->family == "cigarettes");
    CHECK(m->price.cents() == 7999);
    CHECK(pos.retailOnHand(m->id) == 40.0);   // 2 boxes of 20
    CHECK(pos.retailReceipts().back().lines[1].leftOut);
}

TEST_CASE("Receive retail: the invoice is kept as it's worked on (a restart loses nothing); Save for later adds no "
          "stock; Unfinished > Open; Receive asks when it doesn't tie out, then adds the stock", "[receiving][store]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"vt.db"_s);
    const app::PosData seed = store();
    {
        storage::PosStore st(path);
        REQUIRE(st.open());
        REQUIRE(st.seed(seed.settings, seed.menu, seed.employees, nullptr, seed.ingredients));
    }
    {   // Typed in, and ViewTouch stops (an update) before anything is pressed.
        storage::PosStore st(path);
        REQUIRE(st.open());
        storage::AsyncWriter writer(path);
        storage::SqlPosSink sink(writer);
        PosService pos(*st.load(), &sink);
        REQUIRE(pos.loginWithPin(u"1234"_s));
        REQUIRE(pos.recvOpen(u"receive"_s));
        REQUIRE(pos.recvSetHeader({{u"vendor"_s, u"acme-n"_s}, {u"invoice"_s, u"D-1"_s}, {u"date"_s, u"11/27/2026"_s}}));
        REQUIRE(pos.recvScan(QString::fromLatin1(kCarton)));
        REQUIRE(pos.recvSetLine(0, {{u"cost"_s, u"60.00"_s}}));
        CHECK(pos.retailDrafts().size() == 1);
        CHECK(pos.retailOnHand("pack").value_or(0) == 0);
    }
    {
        storage::PosStore st(path);
        REQUIRE(st.open());
        storage::AsyncWriter writer(path);
        storage::SqlPosSink sink(writer);
        PosService pos(*st.load(), &sink);
        REQUIRE(pos.retailDrafts().size() == 1);
        REQUIRE(pos.loginWithPin(u"1234"_s));
        REQUIRE(pos.recvOpen(u"receive"_s));
        // Back as it was left on this terminal.
        QVariantMap info = pos.retailReceiving();
        CHECK(info[u"invoice"_s].toString() == u"D-1"_s);
        REQUIRE(lines(pos).size() == 1);
        CHECK(lines(pos)[0].toMap()[u"cost"_s].toString() == u"60.00"_s);
        CHECK(pos.retailOnHand("pack").value_or(0) == 0);

        // Save for later: off the screen, on Unfinished; nothing on hand.
        REQUIRE(pos.recvSaveForLater());
        CHECK(lines(pos).isEmpty());
        CHECK(pos.retailOnHand("pack").value_or(0) == 0);
        CHECK(pos.retailReceipts().empty());
        QVariantList unfinished = pos.retailReceiving()[u"unfinished"_s].toList();
        REQUIRE(unfinished.size() == 1);
        const QVariantMap u0 = unfinished[0].toMap();
        CHECK(u0[u"invoice"_s].toString() == u"D-1"_s);
        CHECK(u0[u"vendor"_s].toString() == u"Acme Distributing North"_s);
        CHECK(u0[u"lines"_s].toInt() == 1);
        CHECK(u0[u"units"_s].toString() == u"10"_s);
        // Saved for later: not brought back by itself.
        REQUIRE(pos.recvClose());
        REQUIRE(pos.recvOpen(u"receive"_s));
        CHECK(lines(pos).isEmpty());

        // Another screen can't take it while this one has it open (unless it takes it over).
        REQUIRE(pos.recvOpenDraft(u0[u"id"_s].toLongLong(), false));
        REQUIRE(lines(pos).size() == 1);
        {
            PosService other(pos.shared(), u"BAR2"_s);
            REQUIRE(other.loginWithPin(u"1234"_s));
            REQUIRE(other.recvOpen(u"unfinished"_s));
            const QVariantMap seen = other.retailReceiving()[u"unfinished"_s].toList()[0].toMap();
            CHECK_FALSE(seen[u"openAt"_s].toString().isEmpty());
            CHECK_FALSE(other.recvOpenDraft(u0[u"id"_s].toLongLong(), false));
            CHECK_FALSE(other.recvDeleteDraft(u0[u"id"_s].toLongLong()));
        }

        // Receive: no total typed, so it asks; Go back leaves it as it is.
        CHECK_FALSE(pos.recvReceive(false));
        CHECK(pos.retailReceiving()[u"confirm"_s].toMap()[u"kind"_s].toString() == u"nototal"_s);
        REQUIRE(pos.recvConfirmCancel());
        CHECK(pos.retailOnHand("pack").value_or(0) == 0);
        // Off by: asks too.
        REQUIRE(pos.recvSetHeader({{u"total"_s, u"61.00"_s}}));
        CHECK_FALSE(pos.recvReceive(false));
        const QVariantMap ask = pos.retailReceiving()[u"confirm"_s].toMap();
        CHECK(ask[u"kind"_s].toString() == u"off"_s);
        CHECK(ask[u"text"_s].toString().contains(u"1.00"_s));
        CHECK(pos.retailReceipts().empty());
        // Receive anyway: on hand, no longer unfinished.
        REQUIRE(pos.recvReceive(true));
        CHECK(pos.retailOnHand("pack") == 10.0);
        CHECK(pos.retailReceipts().size() == 1);
        CHECK(pos.retailDrafts().empty());
    }
    storage::PosStore st(path);
    REQUIRE(st.open());
    const auto data = st.load();
    REQUIRE(data);
    CHECK(data->retailDrafts.empty());
    REQUIRE(data->retailReceipts.size() == 1);
    CHECK(data->retailReceipts[0].invoice == "D-1");
    CHECK(data->retailStock.at("pack") == 10.0);
}

TEST_CASE("Receive retail: Unfinished > Delete; New invoice keeps the one on the screen", "[receiving]")
{
    PosService pos(store(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.recvOpen(u"receive"_s));
    REQUIRE(pos.recvSetHeader({{u"vendor"_s, u"acme-s"_s}, {u"invoice"_s, u"E-1"_s}}));
    REQUIRE(pos.recvScan(QString::fromLatin1(kCigar)));
    REQUIRE(pos.recvClear());   // New invoice
    CHECK(lines(pos).isEmpty());
    REQUIRE(pos.retailDrafts().size() == 1);
    CHECK(pos.retailDrafts()[0].parked);
    REQUIRE(pos.recvSetHeader({{u"vendor"_s, u"acme-s"_s}, {u"invoice"_s, u"E-2"_s}}));
    REQUIRE(pos.recvScan(QString::fromLatin1(kPack)));
    REQUIRE(pos.retailDrafts().size() == 2);
    // E-1 opened: E-2 stays on Unfinished.
    REQUIRE(pos.recvOpenDraft(pos.retailDrafts()[0].id, false));
    CHECK(pos.retailReceiving()[u"invoice"_s].toString() == u"E-1"_s);
    REQUIRE(pos.retailDrafts().size() == 2);
    REQUIRE(pos.recvDeleteDraft(pos.retailDrafts()[1].id));
    REQUIRE(pos.retailDrafts().size() == 1);
    CHECK(pos.retailDrafts()[0].invoice == "E-1");
    CHECK(pos.retailOnHand("pack").value_or(0) == 0);
    CHECK(pos.retailOnHand("cigar").value_or(0) == 0);
}
