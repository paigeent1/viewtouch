// Tobacco scan data (core/scan_data.hh): the weekly file for Altria. Made-up store and items only.

#include <catch2/catch_test_macros.hpp>

#include "core/buydown.hh"
#include "core/scan_data.hh"

using namespace vt::core;
using namespace vt::core::scandata;
using vt::Money;

namespace {

Money usd(std::int64_t cents) { return Money::fromCents(cents); }

constexpr std::int64_t kHour = 3600LL * 1000;
constexpr std::int64_t kDay = 24 * kHour;
constexpr std::int64_t kEst = 5 * kHour;   // the tests' store clock: UTC-5

// Midnight (store time) of a date, in epoch ms.
std::int64_t midnight(int y, int m, int d) { return daysFromCivil(y, m, d) * kDay + kEst; }

std::string dateOf(std::int64_t ms)
{
    int y, m, d;
    civilFromDays((ms - kEst) / kDay - ((ms - kEst) % kDay < 0 ? 1 : 0), y, m, d);
    return ymdText(y, m, d);
}

std::string timeOf(std::int64_t ms)
{
    std::int64_t s = ((ms - kEst) % kDay + kDay) % kDay / 1000;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", int(s / 3600), int(s / 60 % 60), int(s % 60));
    return buf;
}

// The week ending Saturday 2026-01-10: Sunday the 4th to the 10th.
Options week()
{
    Options o;
    o.weekEnding = "20260110";
    o.from = midnight(2026, 1, 4);
    o.to = midnight(2026, 1, 11);
    o.date = dateOf;
    o.time = timeOf;
    return o;
}

Settings store()
{
    Settings s;
    s.accountNumber = "246813";
    s.accountName = "TestTobacco";
    s.storeName = "Test Tobacco Shop";
    s.street = "1 Sample Road";
    s.city = "Testville";
    s.state = "CT";
    s.zip4 = "06999-1234";
    return s;
}

MenuItem tobacco(const char *id, const char *name, std::int64_t cents, const char *upc, const char *maker, const char *type)
{
    MenuItem m;
    m.id = id;
    m.name = name;
    m.price = usd(cents);
    m.taxClass = TaxClass::Merchandise;
    m.barcodes = {upc};
    m.scanMaker = maker;
    m.scanCategory = type;
    return m;
}

std::vector<MenuItem> menu()
{
    std::vector<MenuItem> m{
        tobacco("gold", "Marlboro Gold Box", 599, "028200003843", "pmusa", "cig"),
        tobacco("goldctn", "Marlboro Gold Box Carton", 5490, "028200003850", "pmusa", "cig"),
        tobacco("lcmint", "Copenhagen LC Mint", 500, "073100000102", "usstc", "mst"),
        tobacco("lcwg", "Copenhagen LC Wintergreen", 499, "073100000119", "usstc", "mst"),
        tobacco("camel", "Other Brand Filters", 649, "012300000017", "other", "cig"),
        tobacco("cbd", "CBD Gummies", 1999, "098700000011", "other", "cbd"),
        tobacco("bd", "Marlboro Red Box (buydown)", 629, "028200003201", "pmusa", "cig"),
    };
    m[0].recipe.push_back({"__parent__:028200003850", 10});   // a pack of the carton
    MenuItem lighter;
    lighter.id = "lighter";
    lighter.name = "Lighter";
    lighter.price = usd(199);
    m.push_back(lighter);
    return m;
}

const MenuItem &find(const std::vector<MenuItem> &m, const char *id)
{
    return *std::ranges::find(m, std::string(id), &MenuItem::id);
}

Check sale(std::int64_t id, std::int64_t at)
{
    Check c;
    c.id = id;
    c.type = CheckType::Quick;
    c.status = CheckStatus::Closed;
    c.openedAt = at - 60000;
    c.closedAt = at;
    return c;
}

Result run(const std::vector<Check> &checks, const Settings &s = store(),
           const std::vector<PosSettings::Promotion> &promos = {})
{
    return build(checks, menu(), promos, TaxRates{}, s, week());
}

const std::int64_t kWed = midnight(2026, 1, 7) + 14 * kHour + 25 * 60000 + 46000;   // Wed 14:25:46

} // namespace

TEST_CASE("Scan data: a pack sale is one record of 45 fields", "[scandata]")
{
    const auto m = menu();
    Check c = sale(1001, kWed);
    c.addItem(find(m, "gold"));
    const Result r = run({c});
    INFO(r.summary);
    for (const Issue &i : r.issues)
        INFO(i.text);
    CHECK_FALSE(r.hasErrors());
    REQUIRE(r.records.size() == 1);
    const auto &f = r.records[0].f;
    CHECK(f[0] == "246813");
    CHECK(f[1] == "20260110");
    CHECK(f[2] == "20260107");
    CHECK(f[3] == "14:25:46");
    CHECK(f[4] == "1001");
    CHECK(f[5].empty());
    CHECK(f[11] == "CIG");
    CHECK(f[12] == "PM USA");
    CHECK(f[14] == "028200003843");
    CHECK(f[16] == "Pack");
    CHECK(f[17] == "1");
    CHECK(f[18] == "1");
    CHECK(f[19] == "N");
    CHECK(f[30] == "5.99");
    CHECK(f[41] == "In Store");
    CHECK(std::ranges::count(r.line(r.records[0]), '|') == 44);
    CHECK(r.fileName == "TestTobacco20260110");
}

TEST_CASE("Scan data: a carton is Carton, 10 consumer units", "[scandata]")
{
    const auto m = menu();
    Check c = sale(1002, kWed);
    c.addItem(find(m, "goldctn"));
    const Result r = run({c});
    REQUIRE(r.records.size() == 1);
    CHECK(r.records[0].f[16] == "Carton");
    CHECK(r.records[0].f[17] == "1");
    CHECK(r.records[0].f[18] == "10");
    CHECK(r.records[0].f[30] == "54.90");
}

TEST_CASE("Scan data: identical lines of one sale are merged", "[scandata]")
{
    const auto m = menu();
    Check c = sale(1003, kWed);
    c.addItem(find(m, "gold"));
    c.addItem(find(m, "lighter"));
    c.addItem(find(m, "gold"));
    const Result r = run({c});
    REQUIRE(r.records.size() == 1);   // the lighter isn't tobacco
    CHECK(r.records[0].f[17] == "2");
    CHECK(r.records[0].f[30] == "11.98");
}

TEST_CASE("Scan data: a return is negative in 18 and 31", "[scandata]")
{
    const auto m = menu();
    Check c = sale(1004, kWed);
    c.addItem(find(m, "gold")).quantity = -1;
    const Result r = run({c});
    CHECK_FALSE(r.hasErrors());
    REQUIRE(r.records.size() == 1);
    CHECK(r.records[0].f[17] == "-1");
    CHECK(r.records[0].f[30] == "-5.99");
    CHECK(r.summary.starts_with("1|-1|-5.99|"));
}

TEST_CASE("Scan data: a manufacturer multi-unit deal over two SKUs", "[scandata]")
{
    const auto m = menu();
    PosSettings::Promotion deal;
    deal.id = "cope2";
    deal.name = "Cope Save 75c";
    deal.items = {"lcmint", "lcwg"};
    deal.amount = usd(75);
    Check c = sale(1005, kWed);
    c.addItem(find(m, "lcmint"));
    c.addItem(find(m, "lcwg"));
    c.addItem(find(m, "gold"));   // not in the deal
    Payment p;
    p.id = c.nextPaymentId++;
    p.kind = TenderKind::Discount;
    p.tenderId = "promo";
    p.tenderName = deal.name;
    p.amount = usd(75);
    p.reference = "promo:cope2";
    c.payments.push_back(p);
    Settings s = store();
    s.multiNames = {"cope save 75c"};
    const Result r = run({c}, s, {deal});
    for (const Issue &i : r.issues)
        INFO(i.text);
    CHECK_FALSE(r.hasErrors());
    REQUIRE(r.records.size() == 3);
    const auto &a = r.records[0].f, &b = r.records[1].f, &g = r.records[2].f;
    CHECK(a[19] == "Y");
    CHECK(b[19] == "Y");
    CHECK(a[20] == "2");
    CHECK(b[20] == "2");
    CHECK(a[21] == "0.75");   // the whole deal on each line
    CHECK(b[21] == "0.75");
    CHECK(a[30] == "4.62");   // 5.00 - 0.38
    CHECK(b[30] == "4.62");   // 4.99 - 0.37
    CHECK(a[22].empty());
    CHECK(g[19] == "N");
    CHECK(g[30] == "5.99");
    CHECK(r.total == usd(462 + 462 + 599));
}

TEST_CASE("Scan data: a buy-2-get-1 deal leaves the odd unit out", "[scandata]")
{
    const auto m = menu();
    PosSettings::Promotion bogo;
    bogo.id = "bogo";
    bogo.name = "BOGO";
    bogo.items = {"lcmint", "lcwg"};
    bogo.buy = 1;
    bogo.get = 1;
    bogo.percentBp = 10000;
    Check c = sale(1006, kWed);
    c.addItem(find(m, "lcmint"));
    c.addItem(find(m, "lcmint"));
    c.addItem(find(m, "lcwg"));   // the cheapest: the unit outside the pair
    Payment p;
    p.id = c.nextPaymentId++;
    p.kind = TenderKind::Discount;
    p.tenderName = "BOGO";
    p.amount = usd(500);
    p.reference = "promo:bogo";
    c.payments.push_back(p);
    Settings s = store();
    s.multiNames = {"BOGO"};
    const Result r = run({c}, s, {bogo});
    REQUIRE(r.records.size() == 2);
    CHECK(r.records[0].f[17] == "2");
    CHECK(r.records[0].f[19] == "Y");
    CHECK(r.records[0].f[20] == "2");
    CHECK(r.records[0].f[21] == "5.00");
    CHECK(r.records[0].f[30] == "5.00");
    CHECK(r.records[1].f[19] == "N");
    CHECK(r.records[1].f[30] == "4.99");
}

TEST_CASE("Scan data: a store discount goes in 23 and 24", "[scandata]")
{
    const auto m = menu();
    Check c = sale(1007, kWed);
    const std::int64_t line = c.addItem(find(m, "gold")).id;
    Payment p;
    p.id = c.nextPaymentId++;
    p.kind = TenderKind::Discount;
    p.tenderName = "Manager 50c";
    p.amount = usd(50);
    p.lineId = line;
    c.payments.push_back(p);
    const Result r = run({c});
    REQUIRE(r.records.size() == 1);
    const auto &f = r.records[0].f;
    CHECK(f[19] == "N");
    CHECK(f[22] == "Manager 50c");
    CHECK(f[23] == "0.50");
    CHECK(f[24].empty());
    CHECK(f[30] == "5.49");
}

TEST_CASE("Scan data: a manufacturer coupon goes in 25 and 26", "[scandata]")
{
    const auto m = menu();
    Check c = sale(1008, kWed);
    c.addItem(find(m, "gold"));
    Payment p;
    p.id = c.nextPaymentId++;
    p.kind = TenderKind::Discount;
    p.tenderName = "Marl Coup";
    p.amount = usd(150);
    c.payments.push_back(p);
    Settings s = store();
    s.couponNames = {"Marl Coup"};
    const Result r = run({c}, s);
    REQUIRE(r.records.size() == 1);
    CHECK(r.records[0].f[24] == "Marl Coup");
    CHECK(r.records[0].f[25] == "1.50");
    CHECK(r.records[0].f[22].empty());
    CHECK(r.records[0].f[30] == "4.49");
}

TEST_CASE("Scan data: a buydown line is an other manufacturer discount, out of the final price", "[scandata][buydowns]")
{
    const auto m = menu();
    Buydown b;
    b.id = "bd-1";
    b.number = 1;
    b.name = "MARLBORO Q1";
    b.receiptText = "MARLBORO SAVINGS!";
    b.amount = usd(400);
    b.items = {{"028200003201", "bd", 0.1, "Marlboro Red Box"}};
    Check c = sale(1009, kWed);
    OrderLine &l = c.addItem(find(m, "bd"));
    l.quantity = 2;
    buydown::apply(l, *buydown::hitFor({b}, find(m, "bd"), "2026-01-07 14:00"), false);
    REQUIRE(l.buydownEach == usd(40));
    const Result r = run({c});
    REQUIRE(r.records.size() == 1);
    const auto &f = r.records[0].f;
    CHECK(f[17] == "2");
    CHECK(f[19] == "N");
    CHECK(f[26] == "MARLBORO SAVINGS!");
    CHECK(f[27] == "0.80");
    CHECK(f[30] == "11.78");   // 2 x 6.29 less 0.80
    CHECK_FALSE(r.hasErrors());

    // A return gives back what was paid: the buydown stays in its (negative) price, no discount fields.
    Check back = sale(1010, kWed + 60000);
    OrderLine &rl = back.addItem(find(m, "bd"));
    rl.quantity = -1;
    buydown::apply(rl, *buydown::hitFor({b}, find(m, "bd"), "2026-01-07 14:00"), false);
    const Result rr = run({back});
    REQUIRE(rr.records.size() == 1);
    CHECK(rr.records[0].f[17] == "-1");
    CHECK(rr.records[0].f[27].empty());
    CHECK(rr.records[0].f[30] == "-5.89");
}

TEST_CASE("Scan data: other makers only when their type is on; CBD never", "[scandata]")
{
    const auto m = menu();
    Check c = sale(1010, kWed);
    c.addItem(find(m, "camel"));
    c.addItem(find(m, "cbd"));
    CHECK(run({c}).records.empty());
    Settings s = store();
    s.optional = {"cig", "other"};
    const Result r = run({c}, s);
    REQUIRE(r.records.size() == 1);
    CHECK(r.records[0].f[14] == "012300000017");
    CHECK(r.records[0].f[12] == "Other");
}

TEST_CASE("Scan data: the summary line adds up the records", "[scandata]")
{
    const auto m = menu();
    Check a = sale(2001, kWed), b = sale(2002, kWed + 60000);
    a.addItem(find(m, "gold")).quantity = 3;
    a.addItem(find(m, "goldctn"));
    b.addItem(find(m, "lcmint")).quantity = -2;
    const Result r = run({a, b});
    CHECK_FALSE(r.hasErrors());
    REQUIRE(r.records.size() == 3);
    CHECK(r.summary == "3|2|62.87|Test Tobacco Shop");   // 3 + 1 - 2 units; 17.97 + 54.90 - 10.00
    Settings s = store();
    s.provider = "Some Software LLC";
    s.lineEnding = "crlf";
    const Result p = run({a, b}, s);
    const std::string text = p.text(s);
    CHECK(text.starts_with("3|2|62.87|Some Software LLC\r\n"));
    CHECK(std::ranges::count(text, '\n') == 4);
}

TEST_CASE("Scan data: never a date of birth or ID data", "[scandata]")
{
    const auto m = menu();
    Check c = sale(1011, kWed);
    c.addItem(find(m, "gold"));
    c.ageVerified = 21;
    c.ageHow = "scan";
    c.ageId4 = "Z987";
    c.ageDob = "1971-03-29";
    Check h = sale(1012, kWed + 1000);
    h.addItem(find(m, "gold"));
    h.ageVerified = 21;
    h.ageHow = "hand";
    h.ageDob = "1966-08-14";
    const Result r = run({c, h});
    REQUIRE(r.records.size() == 2);
    CHECK(r.records[0].f[38] == "Scanned ID");
    CHECK(r.records[1].f[38] == "Manual Entry");
    const std::string text = r.text(store());
    for (const char *secret : {"1971", "0329", "Z987", "1966", "0814", "19710329"})
        CHECK(text.find(secret) == std::string::npos);
}

TEST_CASE("Scan data: the week is Sunday 00:00 to Saturday 23:59:59", "[scandata]")
{
    const auto m = menu();
    Check last = sale(3001, midnight(2026, 1, 11) - 1000);   // Saturday 23:59:59
    Check next = sale(3002, midnight(2026, 1, 11));          // Sunday 00:00:00: next week
    Check first = sale(3003, midnight(2026, 1, 4));          // Sunday 00:00:00: this week
    Check before = sale(3004, midnight(2026, 1, 4) - 1000);  // the Saturday before
    for (Check *c : {&last, &next, &first, &before})
        c->addItem(find(m, "gold"));
    const Result r = run({last, next, first, before});
    REQUIRE(r.records.size() == 2);
    CHECK(r.records[0].f[4] == "3003");
    CHECK(r.records[0].f[2] == "20260104");
    CHECK(r.records[0].f[3] == "00:00:00");
    CHECK(r.records[1].f[4] == "3001");
    CHECK(r.records[1].f[2] == "20260110");
    CHECK(r.records[1].f[3] == "23:59:59");
    CHECK(lastSaturday("20260114") == "20260110");
    CHECK(lastSaturday("20260111") == "20260110");   // Sunday: the week just ended
    CHECK(lastSaturday("20260110") == "20260103");   // Saturday: that week isn't over yet
    CHECK(isSaturday("20260110"));
    CHECK_FALSE(isSaturday("20260111"));
}

TEST_CASE("Scan data: unset or sample settings refuse the file", "[scandata]")
{
    CHECK_FALSE(settingsProblems(Settings{}).empty());
    CHECK(settingsProblems(store()).empty());
    Settings s = store();
    s.accountNumber = "123456";
    CHECK_FALSE(settingsProblems(s).empty());
    s = store();
    s.zip4 = "06999";
    CHECK_FALSE(settingsProblems(s).empty());
    s = store();
    s.street = "P.O. Box 12";
    CHECK_FALSE(settingsProblems(s).empty());
    s = store();
    s.mode = "chain";   // a 6-digit number and no store number
    CHECK_FALSE(settingsProblems(s).empty());
    s.accountNumber = "24681";
    s.storeNumber = "2";
    CHECK(settingsProblems(s).empty());
    const auto m = menu();
    Check c = sale(1013, kWed);
    c.addItem(find(m, "gold"));
    const Result r = build({c}, m, {}, TaxRates{}, Settings{}, week());
    CHECK(r.hasErrors());
}

TEST_CASE("Scan data: UPCs, text and the fill rules", "[scandata]")
{
    CHECK(normalUpc("28200003843") == "028200003843");
    CHECK(normalUpc("0028200003843") == "028200003843");
    CHECK(normalUpc("0 28200 00384 3") == "028200003843");
    CHECK(normalUpc("01234505") == "012000003455");   // UPC-E
    CHECK(cleanText("on!\xC2\xAE Mint | 2mg", 50) == "on! Mint 2mg");
    CHECK(cleanText("abcdef", 3) == "abc");

    const auto rules = parseRules(defaultRulesText() + "dept:Other Cigarettes other cig\n# a note\nbad line\n");
    REQUIRE(rules.size() == 5);
    std::vector<MenuCategory> depts{{.id = "oc", .name = "Other Cigarettes"}};
    MenuItem marl;
    marl.barcodes = {"28200003843"};
    CHECK(fillItem(marl, rules, depts));
    CHECK(marl.scanMaker == "pmusa");
    CHECK(marl.scanCategory == "cig");
    MenuItem other;
    other.family = "oc";
    other.barcodes = {"012300000017"};
    CHECK(fillItem(other, rules, depts));
    CHECK(other.scanMaker == "other");
    MenuItem kept;
    kept.barcodes = {"028200003843"};
    kept.scanMaker = "other";
    kept.scanCategory = "pipe";
    CHECK_FALSE(fillItem(kept, rules, depts));   // a per-item choice stays
    CHECK(fillItem(kept, rules, depts, true));
    CHECK(kept.scanMaker == "pmusa");
}

TEST_CASE("Scan data: an Altria UPC without its maker is an error", "[scandata]")
{
    auto m = menu();
    m[0].scanMaker.clear();
    m[0].scanCategory.clear();
    Check c = sale(1014, kWed);
    c.addItem(m[0]);
    const Result r = build({c}, m, {}, TaxRates{}, store(), week());
    CHECK(r.hasErrors());
    CHECK(r.records.empty());
}

TEST_CASE("Scan data: prices that include tax are sent without it", "[scandata]")
{
    const auto m = menu();
    Check c = sale(1015, kWed);
    c.addItem(find(m, "gold"));
    const std::int64_t line = c.addItem(find(m, "lcmint")).id;
    Payment p;
    p.id = c.nextPaymentId++;
    p.kind = TenderKind::Discount;
    p.tenderName = "Manager";
    p.amount = usd(100);
    p.lineId = line;
    c.payments.push_back(p);
    TaxRates included;
    included.merchandisePpm = 63500;   // 6.35%
    included.pricesIncludeTax = true;
    const Result r = build({c}, m, {}, included, store(), week());
    REQUIRE(r.records.size() == 2);
    CHECK(r.records[0].f[30] == "5.63");   // 5.99 / 1.0635
    CHECK(r.records[1].f[23] == "0.94");   // 1.00 / 1.0635
    CHECK(r.records[1].f[30] == "3.76");   // 4.00 / 1.0635
    TaxRates added;
    added.merchandisePpm = 63500;
    CHECK(build({c}, m, {}, added, store(), week()).records[0].f[30] == "5.99");
}
