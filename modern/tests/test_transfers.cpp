#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Manager > Inventory > Transfers (app/pos_transfers.cpp): retail items sent from one store to another and
// matched there by UPC, the result back, voids, the outbox while the link is down; the retail store's price
// changes to the bar under the bar's price zone, never sent back; the "<store> price" discount.

namespace {

constexpr const char *kPack = "012345000016";     // a pack: both stores have it
constexpr const char *kCarton = "012345000023";   // its carton of 10: only the retail store has it
constexpr const char *kCigar = "077777000014";    // a cigar only the retail store has

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

// A store: the seed's staff and menu, a retail category, and its own short name and the other store.
app::PosData store(const char *code, const char *other, bool retailStore)
{
    app::PosData d = test::seedPosData();
    d.settings.askReasons = false;
    d.settings.storeCode = code;
    d.settings.otherStores = {{other, std::string("pi@") + other + ".example", true, retailStore}};
    core::MenuCategory c;
    c.id = "cigarettes";
    c.name = "CIGARETTES";
    c.scope = "retail";
    c.retailOnly = true;
    c.taxClass = "merchandise";
    d.settings.menuCategories.push_back(c);
    d.menu.push_back(retailItem("pack", "MARLBORO RED PACK", kPack, 1049, 8.10));
    if (retailStore) {
        core::MenuItem carton = retailItem("carton", "MARLBORO RED CARTON", kCarton, 9999, 81.00);
        d.menu.push_back(carton);
        d.menu.back().id = "carton";
        // The pack is a single of the carton (Menu Builder's Parent UPC and pack size).
        for (core::MenuItem &m : d.menu)
            if (m.id == "pack")
                m.recipe.push_back({std::string("__parent__:") + kCarton, 10});
        d.menu.push_back(retailItem("cigar", "PADRON 2000", kCigar, 1250, 6.25));
    } else {
        d.settings.storePricesFrom = other;
    }
    return d;
}

// The file the store put in its outbox for `to` (in memory: no folder in the tests), taken out of it.
QByteArray takeFile(PosService &pos, const char *to, const QString &prefix)
{
    auto &out = pos.shared()->transferMemoryOut[to];
    for (auto it = out.begin(); it != out.end(); ++it)
        if (QString::fromStdString(it->first).startsWith(prefix)) {
            const QByteArray b = it->second;
            out.erase(it);
            return b;
        }
    return {};
}

const core::xfer::Transfer &record(PosService &pos, const char *direction)
{
    const auto &list = pos.shared()->transfers;
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        if (it->direction == direction)
            return *it;
    FAIL("no transfer " << direction);
    return list.front();
}

} // namespace

TEST_CASE("Transfers: out from the retail store, received at the bar with a difference, the result back", "[transfers]")
{
    PosService bh(store("BH", "HCA", true), nullptr);
    PosService hca(store("HCA", "BH", false), nullptr);
    REQUIRE(bh.loginWithPin(u"1234"_s));
    REQUIRE(hca.loginWithPin(u"1234"_s));

    REQUIRE(bh.transferOpen(u"out"_s));
    REQUIRE(bh.transferScan(QString::fromLatin1(kPack)));
    REQUIRE(bh.transferSetQty(0, 0, 5));   // the pack and its carton share a line: 0 cartons, 5 loose packs
    REQUIRE(bh.transferScan(QString::fromLatin1(kCarton)));
    REQUIRE(bh.transferScan(QString::fromLatin1(kCarton)));   // again: 2 cartons
    REQUIRE(bh.transferScan(QString::fromLatin1(kCigar)));
    // Hospitality items never go: a seed drink isn't a retail item.
    CHECK_FALSE(bh.transferAddItem(QString::fromStdString(bh.menu().front().id), 1));
    QVariantMap st = bh.transfers();
    REQUIRE(st[u"lines"_s].toList().size() == 2);   // the carton + its packs, the cigar
    CHECK(st[u"lines"_s].toList()[0].toMap()[u"family"_s].toBool());
    CHECK(st[u"lines"_s].toList()[0].toMap()[u"qty"_s].toDouble() == 2);
    CHECK(st[u"lines"_s].toList()[0].toMap()[u"loose"_s].toDouble() == 5);
    CHECK(st[u"lines"_s].toList()[0].toMap()[u"pack"_s].toString().contains(u"10"_s));
    REQUIRE(bh.transferSetTo(u"HCA"_s));
    REQUIRE(bh.transferSend());

    const core::xfer::Transfer &out = record(bh, "out");
    CHECK(out.status == "transit");
    CHECK(out.key == "BH-1");
    CHECK(out.sentCost() == 5 * 810 + 2 * 8100 + 625);
    // Off on hand at once.
    CHECK(bh.retailOnHand("pack") == -5.0);
    CHECK(bh.retailOnHand("carton") == -2.0);
    // The slip: its number, both stores, the lines, a signature line.
    const core::Report slip = bh.transferSlip(out);
    CHECK(slip.title == "Transfer #1");
    CHECK(slip.subtitle == "BH to HCA");
    bool signature = false;
    for (const core::ReportRow &r : slip.rows)
        signature = signature || (!r.cells.empty() && r.cells[0].starts_with("Signature"));
    CHECK(signature);

    // At the bar: waiting.
    const QByteArray file = takeFile(bh, "HCA", u"transfer-BH-1"_s);
    REQUIRE_FALSE(file.isEmpty());
    QString why;
    CHECK(hca.transferIncoming(file, &why).isEmpty());
    CHECK(why.isEmpty());
    const core::xfer::Transfer &in = record(hca, "in");
    CHECK(in.status == "waiting");
    CHECK(hca.transferIncoming(file).isEmpty());   // sent twice: still one
    CHECK(std::ranges::count_if(hca.shared()->transfers, [](const auto &t) { return t.direction == "in"; }) == 1);

    REQUIRE(hca.transferOpen(u"in"_s));
    REQUIRE(hca.transferStartReceive(in.id));
    QVariantList lines = hca.transfers()[u"receiving"_s].toMap()[u"lines"_s].toList();
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].toMap()[u"found"_s].toBool());   // the carton, as 10 packs each
    CHECK(lines[0].toMap()[u"here"_s].toString().contains(u"10"_s));
    CHECK(lines[1].toMap()[u"found"_s].toBool());
    CHECK_FALSE(lines[2].toMap()[u"found"_s].toBool());   // the cigar isn't on file here
    // A count that differs needs why; a UPC not on file needs Add or Leave out.
    REQUIRE(hca.transferCount(1, 4, QString()));
    CHECK_FALSE(hca.transferReceive());
    REQUIRE(hca.transferCount(1, 4, u"one crushed"_s));
    CHECK_FALSE(hca.transferReceive());
    REQUIRE(hca.transferAddMissing(2, true));
    REQUIRE(hca.transferReceive());

    CHECK(record(hca, "in").status == "differences");
    CHECK(hca.retailOnHand("pack") == 4.0 + 20.0);
    // The cigar was added from the transfer: its name, price, cost; one on hand.
    const core::MenuItem *cigar = hca.findItem(u"PADRON 2000"_s);
    REQUIRE(cigar);
    CHECK(cigar->name == "PADRON 2000");
    CHECK(cigar->price.cents() == 1250);
    CHECK(hca.retailOnHand(cigar->id) == 1.0);

    // The result back at the retail store.
    const QByteArray result = takeFile(hca, "BH", u"result-BH-1"_s);
    REQUIRE_FALSE(result.isEmpty());
    CHECK(bh.transferIncoming(result).isEmpty());
    CHECK(record(bh, "out").status == "differences");
    CHECK(record(bh, "out").differences() == 1);

    // The Transfers report, both stores.
    const core::Report rb = bh.buildReport(u"transfers"_s);
    CHECK(rb.id == "transfers");
    CHECK_FALSE(rb.find("Differences").empty());
    const core::Report rh = hca.buildReport(u"transfers"_s);
    CHECK(rh.id == "transfers");
}

TEST_CASE("Transfers: a sale takes from the on hand kept; a void puts it back and tells the other store", "[transfers]")
{
    PosService bh(store("BH", "HCA", true), nullptr);
    PosService hca(store("HCA", "BH", false), nullptr);
    REQUIRE(bh.loginWithPin(u"1234"_s));
    REQUIRE(bh.transferOpen(u"out"_s));
    REQUIRE(bh.transferScan(QString::fromLatin1(kPack)));
    REQUIRE(bh.transferSend());
    CHECK(bh.retailOnHand("pack") == -1.0);
    const QByteArray file = takeFile(bh, "HCA", u"transfer-"_s);   // (it went)
    REQUIRE(hca.transferIncoming(file).isEmpty());

    const std::int64_t id = record(bh, "out").id;
    REQUIRE(bh.transferVoid(id));
    CHECK(record(bh, "out").status == "void");
    CHECK(bh.retailOnHand("pack") == 0.0);
    CHECK_FALSE(bh.transferVoid(id));   // once
    const QByteArray v = takeFile(bh, "HCA", u"void-"_s);
    REQUIRE_FALSE(v.isEmpty());
    REQUIRE(hca.transferIncoming(v).isEmpty());
    CHECK(record(hca, "in").status == "void");

    // Not yet sent when voided: the file just goes, nothing to tell.
    REQUIRE(bh.transferScan(QString::fromLatin1(kPack)));
    REQUIRE(bh.transferSend());
    REQUIRE(bh.transferVoid(record(bh, "out").id));
    CHECK(bh.shared()->transferMemoryOut["HCA"].empty());

    // A sale of an item with an on hand takes it down.
    REQUIRE(bh.startCheck(core::CheckType::Quick));
    REQUIRE(bh.addItem(u"pack"_s));
    REQUIRE(bh.sendOrder());
    CHECK(bh.retailOnHand("pack") == -1.0);
}

TEST_CASE("Transfers: offline, the files wait in the outbox and the screen says so", "[transfers]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    PosService bh(store("BH", "HCA", true), nullptr);
    bh.shared()->transferDir = dir.path();
    bh.shared()->transferScript = QDir(dir.path()).filePath(u"no-such-script"_s);   // the link is down
    REQUIRE(bh.loginWithPin(u"1234"_s));
    REQUIRE(bh.transferOpen(u"out"_s));
    REQUIRE(bh.transferScan(QString::fromLatin1(kPack)));
    REQUIRE(bh.transferSend());
    const QString file = QDir(dir.path()).filePath(u"outbox/HCA/transfer-BH-1.json"_s);
    CHECK(QFile::exists(file));
    bh.transfersTick();
    bh.transfersTick();
    CHECK(QFile::exists(file));   // still there
    CHECK_FALSE(record(bh, "out").delivered);
    REQUIRE(bh.transferOpen(u"list"_s));
    QVariantMap st = bh.transfers();
    CHECK(st[u"queued"_s].toInt() == 1);
    CHECK(st[u"outgoing"_s].toList().front().toMap()[u"statusText"_s].toString() == u"Waiting to send"_s);
    // The store list the script reads.
    QFile conf(QDir(dir.path()).filePath(u"stores.conf"_s));
    REQUIRE(conf.open(QIODevice::ReadOnly));
    CHECK(QString::fromUtf8(conf.readAll()).contains(u"HCA pi@HCA.example on"_s));
    // The link is back: vt-send-transfers took it (moved it to sent/).
    QDir().mkpath(QDir(dir.path()).filePath(u"sent/HCA"_s));
    REQUIRE(QFile::rename(file, QDir(dir.path()).filePath(u"sent/HCA/transfer-BH-1.json"_s)));
    bh.transfersTick();
    CHECK(record(bh, "out").delivered);
    CHECK(bh.transfers()[u"queued"_s].toInt() == 0);

    // A file in the inbox is read and put away.
    QTemporaryDir other;
    PosService hca(store("HCA", "BH", false), nullptr);
    hca.shared()->transferDir = other.path();
    QDir().mkpath(QDir(other.path()).filePath(u"inbox"_s));
    QFile sent(QDir(dir.path()).filePath(u"sent/HCA/transfer-BH-1.json"_s));
    REQUIRE(sent.open(QIODevice::ReadOnly));
    QFile in(QDir(other.path()).filePath(u"inbox/transfer-BH-1.json"_s));
    REQUIRE(in.open(QIODevice::WriteOnly));
    in.write(sent.readAll());
    in.close();
    hca.transfersTick();
    CHECK_FALSE(QFile::exists(in.fileName()));
    CHECK(QFile::exists(QDir(other.path()).filePath(u"inbox/done/transfer-BH-1.json"_s)));
    CHECK(record(hca, "in").status == "waiting");
}

TEST_CASE("Transfers: the retail store's price changes reach the bar under its price zone, never back", "[transfers][prices]")
{
    PosService bh(store("BH", "HCA", true), nullptr);
    app::PosData hd = store("HCA", "BH", false);
    hd.settings.otherStores.front().sendPrices = true;   // even so: a change from BH is never sent back
    hd.settings.priceZone = {"Bar", 10, "49or99up"};
    PosService hca(std::move(hd), nullptr);
    REQUIRE(bh.loginWithPin(u"1234"_s));
    REQUIRE(hca.loginWithPin(u"1234"_s));

    // At BH: Mass Updates sets the pack to 12.00.
    REQUIRE(bh.massView({{u"scope"_s, u"retail"_s}}));
    REQUIRE(bh.massChooseOnly({u"pack"_s}));
    REQUIRE(bh.massPreview({{u"kind"_s, u"price"_s}, {u"how"_s, u"set"_s}, {u"amount"_s, u"12.00"_s}, {u"round"_s, u"none"_s}}));
    REQUIRE(bh.massApply());
    CHECK(bh.findItem(u"pack"_s)->price.cents() == 1200);
    const QByteArray file = takeFile(bh, "HCA", u"changes-"_s);
    REQUIRE_FALSE(file.isEmpty());
    CHECK(QJsonDocument::fromJson(file).object().value(u"store").toString() == u"BH"_s);

    // At the bar, by itself: 12.00 + 10% = 13.20, up to 13.49. Open checks keep their price.
    REQUIRE(hca.startCheck(core::CheckType::Quick));
    REQUIRE(hca.addItem(u"pack"_s));
    QString why;
    CHECK(hca.transferIncoming(file, &why).isEmpty());
    CHECK(why.isEmpty());
    CHECK(hca.findItem(u"pack"_s)->price.cents() == 1349);
    CHECK(hca.shared()->open.begin()->second.lines.back().unitPrice.cents() == 1049);
    CHECK(hca.basePriceOf(*hca.findItem(u"pack"_s)) == 1200);
    // Logged as from BH, and not sent anywhere (no loop).
    CHECK(std::ranges::any_of(hca.changesToday(), [](const core::Change &c) { return c.area.find("BH") != std::string::npos; }));
    CHECK(hca.shared()->transferMemoryOut["BH"].empty());

    // From a store this one doesn't take prices from: refused.
    QJsonObject o = QJsonDocument::fromJson(file).object();
    o.insert(u"store"_s, u"ELSEWHERE"_s);
    CHECK(hca.transferIncoming(QJsonDocument(o).toJson(), &why).isEmpty());
    CHECK_FALSE(why.isEmpty());

    // Reapply the zone after changing it: 0% and no rounding gives BH's prices back.
    REQUIRE(hca.massView({{u"scope"_s, u"retail"_s}}));
    hca.shared()->settings.otherStores.front().sendPrices = false;   // (the bar sends no prices: its own change stays)
    REQUIRE(hca.massSetPriceZone({{u"name"_s, u"Bar"_s}, {u"markup"_s, u"0"_s}, {u"round"_s, u"none"_s}}));
    REQUIRE(hca.massPreview({{u"kind"_s, u"zone"_s}, {u"round"_s, u"none"_s}}));
    REQUIRE(hca.massApply());
    CHECK(hca.findItem(u"pack"_s)->price.cents() == 1200);
    CHECK(hca.shared()->transferMemoryOut["BH"].empty());   // HCA's own change: BH takes no prices from the bar
}

TEST_CASE("Transfers: asked, the price changes wait on Mass Updates; the bar's store price discount", "[transfers][prices]")
{
    PosService bh(store("BH", "HCA", true), nullptr);
    app::PosData hd = store("HCA", "BH", false);
    hd.settings.storePricesApply = "ask";
    hd.settings.priceZone = {"Bar", 10, "49or99up"};
    PosService hca(std::move(hd), nullptr);
    REQUIRE(bh.loginWithPin(u"1234"_s));
    REQUIRE(hca.loginWithPin(u"1234"_s));
    REQUIRE(bh.massView({{u"scope"_s, u"retail"_s}}));
    REQUIRE(bh.massChooseOnly({u"pack"_s}));
    REQUIRE(bh.massPreview({{u"kind"_s, u"price"_s}, {u"how"_s, u"set"_s}, {u"amount"_s, u"10.00"_s}, {u"round"_s, u"none"_s}}));
    REQUIRE(bh.massApply());
    CHECK(hca.transferIncoming(takeFile(bh, "HCA", u"changes-"_s)) == u"waiting"_s);
    CHECK(hca.findItem(u"pack"_s)->price.cents() == 1049);   // not yet
    REQUIRE(hca.massView({{u"scope"_s, u"retail"_s}}));
    CHECK(hca.massUpdate()[u"waitingPrices"_s].toInt() == 1);
    CHECK(hca.massUpdate()[u"baseLabel"_s].toString() == u"BH price"_s);
    REQUIRE(hca.massReviewWaiting());
    REQUIRE(hca.massApply());
    CHECK(hca.findItem(u"pack"_s)->price.cents() == 1149);   // 10.00 + 10% = 11.00, up to 11.49

    // The bar sells a pack to a regular at BH's price: 1.49 off, its own line.
    REQUIRE(hca.startCheck(core::CheckType::Quick));
    REQUIRE(hca.addItem(u"pack"_s));
    REQUIRE(hca.storePriceDiscount());
    const core::Check &c = hca.shared()->open.begin()->second;
    const core::Totals t = c.totals(hca.shared()->settings.tax);
    CHECK(t.discounts.cents() == 149);
    REQUIRE(c.payments.size() == 1);
    CHECK(c.payments.front().tenderName == "BH price");
    CHECK_FALSE(hca.storePriceDiscount());   // once a line
}

TEST_CASE("Transfers: a customer's own prices: the source store's price and a preferential percent", "[transfers][prices][customers]")
{
    app::PosData d = store("HCA", "BH", false);
    core::MenuCategory cigars;
    cigars.id = "cigars";
    cigars.name = "CIGARS";
    cigars.scope = "retail";
    cigars.retailOnly = true;
    cigars.taxClass = "merchandise";
    cigars.preferential = true;   // the owner turns it on for cigars
    d.settings.menuCategories.push_back(cigars);
    core::MenuItem cigar = retailItem("cigar", "PADRON 2000", kCigar, 1250, 6.25);
    cigar.family = "cigars";
    d.menu.push_back(cigar);
    for (core::MenuItem &m : d.menu)
        if (m.id == "pack") {
            m.price = Money::fromCents(1149);
            m.noDiscount = true;   // cigarettes: never a preferential discount
        }
    core::CustomerRecord regular;
    regular.id = "c-1";
    regular.name = "Pat Regular";
    regular.phone = "2035550101";
    regular.sourcePrice = true;
    regular.preferentialPercent = 10;
    d.customers.push_back(regular);
    PosService hca(std::move(d), nullptr);
    hca.shared()->basePrices[core::BuilderIndex::codeKey(kPack)] = 1000;   // BH sells the pack at 10.00
    hca.shared()->basePrices[core::BuilderIndex::codeKey(kCigar)] = 1100;  // and the cigar at 11.00
    hca.shared()->basePricesLoaded = true;
    REQUIRE(hca.loginWithPin(u"1234"_s));
    REQUIRE(hca.startCheck(core::CheckType::Quick));
    core::Check &c = hca.shared()->open.begin()->second;
    c.customerId = "c-1";
    REQUIRE(hca.addItem(u"pack"_s));
    REQUIRE(hca.addItem(u"cigar"_s));
    const auto lineNamed = [&](const std::string &prefix) {
        Money m;
        for (const core::Payment &p : c.payments)
            if (p.tenderName.starts_with(prefix))
                m += c.totals(hca.shared()->settings.tax).discountBy[p.id];
        return m.cents();
    };
    // The pack: back to BH's 10.00 (1.49 off), no percent (No discounts). The cigar: 12.50 -> 11.00 (1.50 off),
    // then 10% of the 11.00 left (1.10).
    CHECK(lineNamed("BH price") == 149 + 150);
    CHECK(lineNamed("Pat Regular's price") == 110);
    CHECK(c.totals(hca.shared()->settings.tax).discounts.cents() == 149 + 150 + 110);
    // Off the check: they go.
    c.customerId.clear();
    REQUIRE(hca.addItem(u"pack"_s));
    CHECK(c.totals(hca.shared()->settings.tax).discounts.cents() == 0);
    CHECK(std::ranges::none_of(c.payments, [](const core::Payment &p) { return p.reference.starts_with("custprice:"); }));
}
