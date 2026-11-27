#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "core/customer_sync.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// TEMPORARY: remove with app/pos_customer_sync.cpp when the corporate customer list exists. Customers shared store
// to store (Store Settings > Other stores > Share customers): a change goes out, the other store merges it by
// phone (newest change wins per field), a clearly different name waits to be checked, nothing comes back (no
// loops), and the store's own numbers (house account, points, visits) never travel.

namespace {

app::PosData store(const char *code, const char *other, bool share = true)
{
    app::PosData d = test::seedPosData();
    d.settings.askReasons = false;
    d.settings.storeCode = code;
    core::OtherStore o{other, std::string("pi@") + other + ".example", true, false};
    o.shareCustomers = share;
    d.settings.otherStores = {o};
    return d;
}

// Every customers file the store put in its outbox for `to` (in memory in the tests), taken out of it.
std::vector<QByteArray> takeFiles(PosService &pos, const char *to)
{
    std::vector<QByteArray> out;
    auto &box = pos.shared()->transferMemoryOut[to];
    for (auto it = box.begin(); it != box.end();) {
        if (QString::fromStdString(it->first).startsWith(u"customers-"_s)) {
            out.push_back(it->second);
            it = box.erase(it);
        } else {
            ++it;
        }
    }
    return out;
}

// What `from` sent to `to`, read there as its inbox would be. Returns how many files went.
int deliver(PosService &from, const char *toName, PosService &to)
{
    int n = 0;
    for (const QByteArray &b : takeFiles(from, toName)) {
        QString why;
        to.transferIncoming(b, &why);
        INFO(why.toStdString());
        CHECK(why.isEmpty());
        ++n;
    }
    return n;
}

const core::CustomerRecord *byPhone(PosService &pos, const std::string &digits)
{
    for (const core::CustomerRecord &c : pos.shared()->customers)
        if (core::csync::phoneKey(c.phone) == digits)
            return &c;
    return nullptr;
}

} // namespace

TEST_CASE("Customer sharing (temporary): the fields, the phone key, newer wins, clearly different names", "[customersync]")
{
    namespace cs = core::csync;
    CHECK(cs::phoneKey("(203) 555-0101") == "2035550101");
    CHECK(cs::phoneKey("+1 203 555 0101") == "2035550101");
    CHECK(cs::phoneKey("555") == "");
    CHECK(cs::sameName("John Smith", "J. Smith"));
    CHECK(cs::sameName("Rob Jones", "Robert K"));
    CHECK(cs::sameName("", "Anyone"));
    CHECK_FALSE(cs::sameName("John Smith", "Mary Jones"));

    core::CustomerRecord c;
    c.name = "Pat Doe";
    c.phone = "2035550101";
    c.points = 40;
    c.visits = 9;
    c.houseAccount = true;
    cs::Fields have;
    CHECK(cs::noteLocal(have, c, 1000, "BH") == std::vector<std::string>{"name", "phone"});
    c.points = 80;   // not a shared field
    c.visits = 10;
    CHECK(cs::noteLocal(have, c, 2000, "BH").empty());
    // Newer wins; an older change is kept out; a name is never emptied.
    cs::Fields in{{"email", {"pat@example.com", 3000, "HCA"}}, {"name", {"", 4000, "HCA"}}, {"phone", {"999", 500, "HCA"}}};
    CHECK(cs::merge(have, in) == std::vector<std::string>{"email"});
    CHECK(have["name"].value == "Pat Doe");
    CHECK(have["phone"].value == "2035550101");
}

TEST_CASE("Customer sharing (temporary): a new and a changed customer go to the other store, merged by phone", "[customersync]")
{
    std::int64_t t = 1'000'000;
    PosService bh(store("BH", "HCA"), nullptr);
    app::PosData hd = store("HCA", "BH");
    core::CustomerRecord regular;   // already at the bar, with the bar's own numbers
    regular.id = "h-1";
    regular.name = "Pat Doe";
    regular.phone = "(203) 555-0101";
    regular.note = "likes the window seat";
    regular.points = 50;
    regular.visits = 12;
    regular.houseAccount = true;
    regular.accountBalance = Money::fromCents(2500);
    hd.customers.push_back(regular);
    PosService hca(std::move(hd), nullptr);
    bh.setClock([&] { return t; });
    hca.setClock([&] { return t; });
    REQUIRE(bh.loginWithPin(u"1234"_s));
    REQUIRE(hca.loginWithPin(u"1234"_s));

    // New at the retail store: sent.
    REQUIRE(bh.saveCustomer({{u"name"_s, u"Pat Doe"_s}, {u"phone"_s, u"203-555-0101"_s}, {u"email"_s, u"pat@example.com"_s},
                             {u"onDuplicate"_s, u"skip"_s}}));
    auto files = takeFiles(bh, "HCA");
    REQUIRE(files.size() == 1);
    const QJsonObject file = QJsonDocument::fromJson(files[0]).object();
    CHECK(file[u"format"_s].toString() == u"viewtouch-customers"_s);
    CHECK(file[u"version"_s].toInt() == 1);
    CHECK(file[u"store"_s].toString() == u"BH"_s);
    const QJsonObject sent = file[u"customers"_s].toArray().at(0).toObject();
    CHECK(sent[u"version"_s].toInt() == 1);
    // The store's own numbers are not in it.
    const QString text = QString::fromUtf8(files[0]);
    for (const char *local : {"points", "visits", "spent", "houseAccount", "accountBalance", "account", "lastOrder"})
        CHECK_FALSE(sent[u"fields"_s].toObject().contains(QString::fromLatin1(local)));
    CHECK_FALSE(text.contains(u"points"_s));

    QString why;
    hca.transferIncoming(files[0], &why);
    CHECK(why.isEmpty());
    // Merged into the bar's Pat by phone: one customer, the email added, the bar's note and numbers kept.
    REQUIRE(hca.shared()->customers.size() == 1);
    const core::CustomerRecord &pat = hca.shared()->customers.front();
    CHECK(pat.id == "h-1");
    CHECK(pat.email == "pat@example.com");
    CHECK(pat.note == "likes the window seat");
    CHECK(pat.points == 50);
    CHECK(pat.visits == 12);
    CHECK(pat.houseAccount);
    CHECK(pat.accountBalance.cents() == 2500);
    // No loop: nothing goes back to the retail store.
    CHECK(takeFiles(hca, "BH").empty());

    // Changed at the retail store later: the change comes over. A house account there stays there.
    t += 60'000;
    const QString bhId = QString::fromStdString(byPhone(bh, "2035550101")->id);
    REQUIRE(bh.saveCustomer({{u"id"_s, bhId}, {u"name"_s, u"Pat Doe"_s}, {u"phone"_s, u"203-555-0101"_s},
                             {u"email"_s, u"pat@new.example"_s}, {u"preferentialPercent"_s, 10}, {u"sourcePrice"_s, true},
                             {u"onDuplicate"_s, u"skip"_s}}));
    CHECK(deliver(bh, "HCA", hca) == 1);
    CHECK(pat.email == "pat@new.example");
    CHECK(pat.preferentialPercent == 10);
    CHECK(pat.sourcePrice);
    t += 60'000;
    REQUIRE(bh.saveCustomer({{u"id"_s, bhId}, {u"name"_s, u"Pat Doe"_s}, {u"phone"_s, u"203-555-0101"_s},
                             {u"houseAccount"_s, true}, {u"accountLimit"_s, 10000}, {u"onDuplicate"_s, u"skip"_s}}));
    CHECK(takeFiles(bh, "HCA").empty());

    // Changed at the bar: goes to the retail store, which merges it into its own Pat (linked now).
    t += 60'000;
    REQUIRE(hca.saveCustomer({{u"id"_s, u"h-1"_s}, {u"name"_s, u"Pat Doe"_s}, {u"phone"_s, u"(203) 555-0101"_s},
                              {u"note"_s, u"window seat, no ice"_s}, {u"onDuplicate"_s, u"skip"_s}}));
    CHECK(deliver(hca, "BH", bh) == 1);
    CHECK(bh.shared()->customers.size() == 1);
    CHECK(byPhone(bh, "2035550101")->note == "window seat, no ice");
    CHECK(takeFiles(bh, "HCA").empty());   // and not sent back
}

TEST_CASE("Customer sharing (temporary): the newest change of each field wins, whichever arrives last", "[customersync]")
{
    std::int64_t t = 2'000'000;
    PosService bh(store("BH", "HCA"), nullptr);
    PosService hca(store("HCA", "BH"), nullptr);
    bh.setClock([&] { return t; });
    hca.setClock([&] { return t; });
    REQUIRE(bh.loginWithPin(u"1234"_s));
    REQUIRE(hca.loginWithPin(u"1234"_s));
    REQUIRE(bh.saveCustomer({{u"name"_s, u"Sam Lee"_s}, {u"phone"_s, u"2035550199"_s}, {u"onDuplicate"_s, u"skip"_s}}));
    CHECK(deliver(bh, "HCA", hca) == 1);
    REQUIRE(byPhone(hca, "2035550199"));
    const QString bhId = QString::fromStdString(byPhone(bh, "2035550199")->id);
    const QString hcaId = QString::fromStdString(byPhone(hca, "2035550199")->id);
    // The retail store changes the note first, the bar a minute later; the bar's file arrives first.
    t += 1000;
    REQUIRE(bh.saveCustomer({{u"id"_s, bhId}, {u"name"_s, u"Sam Lee"_s}, {u"phone"_s, u"2035550199"_s},
                             {u"note"_s, u"older"_s}, {u"onDuplicate"_s, u"skip"_s}}));
    t += 60'000;
    REQUIRE(hca.saveCustomer({{u"id"_s, hcaId}, {u"name"_s, u"Sam Lee"_s}, {u"phone"_s, u"2035550199"_s},
                              {u"note"_s, u"newer"_s}, {u"onDuplicate"_s, u"skip"_s}}));
    CHECK(deliver(hca, "BH", bh) == 1);
    CHECK(deliver(bh, "HCA", hca) == 1);
    CHECK(byPhone(bh, "2035550199")->note == "newer");
    CHECK(byPhone(hca, "2035550199")->note == "newer");
    // The same file again changes nothing, and nothing ever loops.
    CHECK(takeFiles(bh, "HCA").empty());
    CHECK(takeFiles(hca, "BH").empty());
    CHECK(bh.shared()->customers.size() == 1);
    CHECK(hca.shared()->customers.size() == 1);
}

TEST_CASE("Customer sharing (temporary): the same phone with a clearly different name is checked, not merged", "[customersync]")
{
    std::int64_t t = 3'000'000;
    PosService bh(store("BH", "HCA"), nullptr);
    app::PosData hd = store("HCA", "BH");
    core::CustomerRecord mary;
    mary.id = "h-9";
    mary.name = "Mary Jones";
    mary.phone = "2035550142";
    mary.email = "mary@example.com";
    hd.customers.push_back(mary);
    PosService hca(std::move(hd), nullptr);
    bh.setClock([&] { return t; });
    hca.setClock([&] { return t; });
    REQUIRE(bh.loginWithPin(u"1234"_s));
    REQUIRE(hca.loginWithPin(u"1234"_s));

    REQUIRE(bh.saveCustomer({{u"name"_s, u"John Smith"_s}, {u"phone"_s, u"203-555-0142"_s},
                             {u"email"_s, u"john@example.com"_s}, {u"onDuplicate"_s, u"skip"_s}}));
    REQUIRE(bh.saveCustomer({{u"name"_s, u"Lou Reed"_s}, {u"phone"_s, u"203-555-0143"_s}, {u"onDuplicate"_s, u"skip"_s}}));
    CHECK(deliver(bh, "HCA", hca) == 2);
    // Mary is untouched; John waits on Check These Customers; Lou (a new phone) is added.
    REQUIRE(hca.shared()->customers.size() == 2);
    CHECK(hca.shared()->customer("h-9")->name == "Mary Jones");
    CHECK(hca.shared()->customer("h-9")->email == "mary@example.com");
    QVariantMap sync = hca.customerSync();
    CHECK(sync[u"on"_s].toBool());
    QVariantList list = sync[u"conflicts"_s].toList();
    REQUIRE(list.size() == 1);
    const QVariantMap cf = list[0].toMap();
    CHECK(cf[u"hereName"_s].toString() == u"Mary Jones"_s);
    CHECK(cf[u"thereName"_s].toString() == u"John Smith"_s);
    CHECK(cf[u"from"_s].toString() == u"BH"_s);

    // A manager says they're different people: John becomes a customer of his own at the bar.
    REQUIRE(hca.customerSyncResolve(cf[u"key"_s].toString(), u"apart"_s));
    CHECK(hca.customerSync()[u"conflicts"_s].toList().isEmpty());
    REQUIRE(hca.shared()->customers.size() == 3);
    CHECK(hca.shared()->customer("h-9")->name == "Mary Jones");
    CHECK(std::ranges::count_if(hca.shared()->customers, [](const core::CustomerRecord &c) { return c.name == "John Smith"; }) == 1);
    CHECK(takeFiles(hca, "BH").empty());
    // His next change goes to his own record, not Mary's.
    t += 60'000;
    const QString johnId = QString::fromStdString(byPhone(bh, "2035550142")->id);
    REQUIRE(bh.saveCustomer({{u"id"_s, johnId}, {u"name"_s, u"John Smith"_s}, {u"phone"_s, u"203-555-0142"_s},
                             {u"note"_s, u"cigars"_s}, {u"onDuplicate"_s, u"skip"_s}}));
    CHECK(deliver(bh, "HCA", hca) == 1);
    CHECK(hca.customerSync()[u"conflicts"_s].toList().isEmpty());
    CHECK(hca.shared()->customer("h-9")->note.empty());
    for (const core::CustomerRecord &c : hca.shared()->customers)
        if (c.name == "John Smith")
            CHECK(c.note == "cigars");
}

TEST_CASE("Customer sharing (temporary): Merge, Send All Customers Now, and a store that doesn't share", "[customersync]")
{
    std::int64_t t = 4'000'000;
    app::PosData bd = store("BH", "HCA");
    core::CustomerRecord kim;   // on file before sharing: only Send All sends her
    kim.id = "b-1";
    kim.name = "Kim Park";
    kim.phone = "2035550177";
    kim.createdAt = 3'500'000;   // added at the retail store after the bar's record
    bd.customers.push_back(kim);
    core::CustomerRecord walkIn;   // no phone: never shared
    walkIn.id = "b-2";
    walkIn.name = "Walk In";
    bd.customers.push_back(walkIn);
    PosService bh(std::move(bd), nullptr);
    app::PosData hd = store("HCA", "BH");
    core::CustomerRecord kp;
    kp.id = "h-1";
    kp.name = "K. P.";   // initials: could be anyone; "Kimberly"? a manager decides
    kp.phone = "203.555.0177";
    kp.createdAt = 1'000;
    hd.customers.push_back(kp);
    PosService hca(std::move(hd), nullptr);
    bh.setClock([&] { return t; });
    hca.setClock([&] { return t; });
    REQUIRE(bh.loginWithPin(u"1234"_s));
    REQUIRE(hca.loginWithPin(u"1234"_s));

    REQUIRE(bh.customerSyncSendAll());
    auto files = takeFiles(bh, "HCA");
    REQUIRE(files.size() == 1);
    CHECK(QJsonDocument::fromJson(files[0]).object()[u"customers"_s].toArray().size() == 1);   // Kim, not the walk-in
    QString why;
    hca.transferIncoming(files[0], &why);
    CHECK(why.isEmpty());
    const QVariantList list = hca.customerSync()[u"conflicts"_s].toList();
    REQUIRE(list.size() == 1);
    REQUIRE(hca.customerSyncResolve(list[0].toMap()[u"key"_s].toString(), u"merge"_s));
    CHECK(hca.shared()->customers.size() == 1);
    CHECK(hca.shared()->customer("h-1")->name == "Kim Park");   // the newer of the two
    CHECK(hca.shared()->customer("h-1")->phone == "203.555.0177");   // the same number: the bar's own way of writing it
    CHECK(takeFiles(hca, "BH").empty());

    // A store not marked Share customers here: its file is refused, nothing changes.
    PosService other(store("HCA", "BHNM", false), nullptr);
    REQUIRE(other.loginWithPin(u"1234"_s));
    CHECK(other.customerSync().isEmpty());
    CHECK_FALSE(other.customerSyncSendAll());
    why.clear();
    other.transferIncoming(files[0], &why);
    CHECK_FALSE(why.isEmpty());
    CHECK(other.shared()->customers.empty());
}
