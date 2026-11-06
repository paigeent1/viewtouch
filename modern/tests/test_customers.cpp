#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"
#include "storage/async_writer.hh"
#include "storage/pos_store.hh"

#include <QJsonArray>
#include <QJsonObject>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Customers, gift cards and house accounts.

namespace {

void openDrawer(PosService &pos)
{
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
}

QStringList names(const QVariantList &list)
{
    QStringList out;
    for (const QVariant &v : list)
        out << v.toMap()[u"name"_s].toString();
    return out;
}

QString rowAmount(const QVariantMap &report, const QString &first)
{
    for (const QVariant &v : report[u"rows"_s].toList()) {
        const QStringList cells = v.toMap()[u"cells"_s].toStringList();
        if (!cells.isEmpty() && cells.first() == first)
            return cells.last();
    }
    return {};
}

} // namespace

TEST_CASE("Customers: saved, found by phone or name, counted when their checks close", "[customers]")
{
    PosService pos(test::seedPosData(), nullptr);
    openDrawer(pos);
    CHECK_FALSE(pos.saveCustomer({}));                        // a name or a phone
    REQUIRE(pos.saveCustomer({{u"name"_s, u"Dana Lee"_s}, {u"phone"_s, u"(555) 010-1234"_s},
                              {u"note"_s, u"no onions"_s}}));
    REQUIRE(pos.saveCustomer({{u"name"_s, u"Alex Kim"_s}, {u"phone"_s, u"555-777-0000"_s}}));
    REQUIRE(pos.shared()->customers.size() == 2);

    REQUIRE(pos.findCustomers(u"0101"_s));                     // digits anywhere in the number
    CHECK(names(pos.customerResults()) == QStringList{u"Dana Lee"_s});
    REQUIRE(pos.findCustomers(u"kim"_s));
    CHECK(names(pos.customerResults()) == QStringList{u"Alex Kim"_s});
    // The same phone written differently: asked about, then Dana is updated.
    CHECK_FALSE(pos.saveCustomer({{u"name"_s, u"Dana Lee-Park"_s}, {u"phone"_s, u"555.010.1234"_s}}));
    REQUIRE(pos.customerDuplicate()[u"matches"_s].toList().size() == 1);
    REQUIRE(pos.resolveCustomerDuplicate(u"update"_s, QString::fromStdString(pos.shared()->customers.front().id)));
    CHECK(pos.customerDuplicate().isEmpty());
    CHECK(pos.shared()->customers.size() == 2);

    // A takeout for Dana: picked from the list, counted at close.
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    const QString dana = QString::fromStdString(pos.shared()->customers.front().id);
    REQUIRE(pos.useCustomer(dana));
    CHECK(pos.checkInfo()[u"customer"_s].toMap()[u"name"_s] == u"Dana Lee-Park"_s);
    pos.addItem(u"cobb"_s);
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    REQUIRE(pos.selectCustomer(dana));
    CHECK(pos.customerInfo()[u"visits"_s] == 1);
    CHECK(pos.customerInfo()[u"spent"_s] == u"$13.53"_s);

    // A delivery typed in by hand goes on file too.
    REQUIRE(pos.startCheck(core::CheckType::Delivery));
    REQUIRE(pos.setCustomer({{u"name"_s, u"Robin"_s}, {u"phone"_s, u"555 222 3333"_s}, {u"address"_s, u"9 Elm"_s}}));
    pos.addItem(u"coffee"_s);
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    REQUIRE(pos.findCustomers(u"5552223333"_s));
    REQUIRE(pos.customerResults().size() == 1);
    CHECK(pos.customerResults().first().toMap()[u"address"_s] == u"9 Elm"_s);
    CHECK(pos.customerResults().first().toMap()[u"visits"_s] == 1);

    // Reopening takes the visit back.
    REQUIRE(pos.reopenCheck(1));
    REQUIRE(pos.selectCustomer(dana));
    CHECK(pos.customerInfo()[u"visits"_s] == 0);
}

TEST_CASE("Gift cards: sold on a check, live once it's paid, spent, and given back", "[customers][giftcards]")
{
    PosService pos(test::seedPosData(), nullptr);
    openDrawer(pos);

    // Sell $50 on card 6000 1111 2221 (no check open: a quick one starts).
    CHECK_FALSE(pos.sellGiftCard(u"6000 1111 2221"_s, 0));    // no amount
    REQUIRE(pos.sellGiftCard(u"6000 1111 2221"_s, 5000));
    CHECK(pos.lines().size() == 1);
    CHECK(pos.totals()[u"tax"_s].toString() == u"$0.00"_s);              // no tax on gift cards
    CHECK(pos.kitchenTickets().isEmpty());
    REQUIRE(pos.lookupGiftCard(u"600011112221"_s));
    CHECK_FALSE(pos.giftCardInfo()[u"found"_s].toBool());     // not before it's paid
    REQUIRE(pos.tender(u"cash"_s));
    pos.sendOrder();                                           // nothing prints, nothing to make
    REQUIRE(pos.closeCheck());
    CHECK(pos.kitchenTickets().isEmpty());
    REQUIRE(pos.lookupGiftCard(u"600011112221"_s));
    CHECK(pos.giftCardInfo()[u"found"_s].toBool());
    CHECK(pos.giftCardInfo()[u"balance"_s] == u"$50.00"_s);

    // Spend $13.53 of it on a Cobb.
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"cobb"_s);
    REQUIRE(pos.tender(u"gift"_s));                           // the card looked up
    CHECK(pos.totals()[u"balance"_s].toString() == u"$0.00"_s);
    CHECK(pos.giftCardInfo()[u"balance"_s] == u"$36.47"_s);
    // Undo the payment: the money goes back on the card.
    REQUIRE(pos.removePayment());
    CHECK(pos.giftCardInfo()[u"balance"_s] == u"$50.00"_s);
    REQUIRE(pos.payWithGiftCard({}, 1000));                    // $10 of it
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    CHECK(pos.giftCardInfo()[u"balance"_s] == u"$40.00"_s);

    // A card can't pay more than it holds, and an unknown one isn't accepted.
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    for (int i = 0; i < 5; ++i)
        pos.addItem(u"cobb"_s);                                // $67.66
    REQUIRE(pos.payWithGiftCard(u"600011112221"_s));
    CHECK(pos.totals()[u"balance"_s].toString() == u"$27.66"_s);
    CHECK(pos.giftCardInfo()[u"balance"_s] == u"$0.00"_s);
    CHECK_FALSE(pos.payWithGiftCard(u"600011112221"_s));       // empty now
    CHECK_FALSE(pos.payWithGiftCard(u"999999"_s));
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());

    // Reload it; a card sold on a check that's reopened is taken back.
    REQUIRE(pos.sellGiftCard(u"600011112221"_s, 2500));
    CHECK(pos.lines().first().toMap()[u"name"_s].toString().contains(u"reload"_s));
    const qint64 reload = pos.checkInfo()[u"id"_s].toLongLong();
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    CHECK(pos.giftCardInfo()[u"balance"_s] == u"$25.00"_s);
    REQUIRE(pos.reopenCheck(reload));
    CHECK(pos.giftCardInfo()[u"balance"_s] == u"$0.00"_s);

    // A new card with no number gets one.
    pos.releaseCheck();
    REQUIRE(pos.sellGiftCard({}, 2000));
    CHECK(pos.lines().last().toMap()[u"name"_s].toString().startsWith(u"Gift Card"_s));

    // The report: sold, spent, still on cards.
    const QVariantMap report = pos.report(u"accounts"_s);
    CHECK(rowAmount(report, u"Sold and reloaded"_s) == u"$50.00"_s);   // $50 + $25 - $25 taken back
    CHECK(rowAmount(report, u"Spent"_s) == u"$50.00"_s);
    CHECK(rowAmount(report, u"Still on cards (owed by the store)"_s) == u"$0.00"_s);

    // Saved with their history.
    const core::GiftCard &card = pos.shared()->giftCards.front();
    CHECK(app::giftCardFromJson(app::toJson(card)) == card);
    CHECK(card.history.size() >= 6);
}

TEST_CASE("House accounts: managers open them, charges stay under the limit, payments", "[customers][house]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));                      // Sam, a server
    REQUIRE(pos.saveCustomer({{u"name"_s, u"Acme Office"_s}, {u"phone"_s, u"5550001111"_s}}));
    const QString acme = QString::fromStdString(pos.shared()->customers.front().id);
    CHECK_FALSE(pos.saveCustomer({{u"id"_s, acme}, {u"name"_s, u"Acme Office"_s}, {u"houseAccount"_s, true}}));
    pos.logout();

    openDrawer(pos);                                           // Morgan, a manager
    REQUIRE(pos.saveCustomer({{u"id"_s, acme}, {u"name"_s, u"Acme Office"_s}, {u"phone"_s, u"5550001111"_s},
                              {u"houseAccount"_s, true}, {u"accountLimit"_s, 3000}}));

    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"cobb"_s);
    CHECK_FALSE(pos.tender(u"house"_s));                       // whose account?
    REQUIRE(pos.useCustomer(acme));
    REQUIRE(pos.tender(u"house"_s));                           // $13.53
    REQUIRE(pos.closeCheck());

    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"cobb"_s);
    pos.addItem(u"cobb"_s);                                    // $27.06: over the $30 limit
    REQUIRE(pos.useCustomer(acme));
    CHECK_FALSE(pos.tender(u"house"_s));
    REQUIRE(pos.tender(u"house"_s, 1000));                     // part of it fits
    REQUIRE(pos.removePayment());                              // and comes off again
    REQUIRE(pos.selectCustomer(acme));
    CHECK(pos.customerInfo()[u"balance"_s] == u"$13.53"_s);
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());

    // A manager can't close the account while it's owed.
    CHECK_FALSE(pos.saveCustomer({{u"id"_s, acme}, {u"name"_s, u"Acme Office"_s}, {u"houseAccount"_s, false}}));

    // Paying: $5 in cash (into the drawer), the rest by card.
    const QString before = pos.drawerInfo()[u"expected"_s].toString();
    REQUIRE(pos.payOnAccount(u"cash"_s, 500));
    CHECK(pos.drawerInfo()[u"expected"_s].toString() != before);
    CHECK(pos.customerInfo()[u"balance"_s] == u"$8.53"_s);
    REQUIRE(pos.payOnAccount(u"card"_s));
    CHECK(pos.customerInfo()[u"balance"_s] == u"$0.00"_s);
    CHECK_FALSE(pos.payOnAccount(u"card"_s));                  // nothing owed
    CHECK(pos.customerInfo()[u"account"_s].toList().size() == 5);

    const QVariantMap report = pos.report(u"accounts"_s);
    CHECK(rowAmount(report, u"Charged"_s) == u"$13.53"_s);
    CHECK(rowAmount(report, u"Paid"_s) == u"$13.53"_s);
    CHECK(rowAmount(report, u"Owed to the store"_s) == u"$0.00"_s);

    const core::CustomerRecord &r = pos.shared()->customers.front();
    CHECK(app::customerFromJson(app::toJson(r)) == r);
}

TEST_CASE("Gift cards: a manager moves a balance to a new card; the old one closes", "[customers][giftcards]")
{
    PosService pos(test::seedPosData(), nullptr);
    openDrawer(pos);                                           // Morgan, a manager
    pos.shared()->settings.giftCardLocation = "HCA";

    REQUIRE(pos.sellGiftCard(u"6000 1111 2221"_s, 5000));
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    REQUIRE(pos.lookupGiftCard(u"600011112221"_s));
    CHECK(pos.giftCardInfo()[u"location"_s] == u"HCA"_s);     // where it was sold

    // A typo in a new 12-digit number is caught by its check digit.
    CHECK_FALSE(pos.transferGiftCard(u"600011112221"_s, u"600011113335"_s));
    CHECK_FALSE(pos.sellGiftCard(u"600011113335"_s, 1000));
    CHECK_FALSE(pos.transferGiftCard(u"600011112221"_s, u"600011112221"_s));   // the same card

    REQUIRE(pos.transferGiftCard(u"600011112221"_s, u"6000 1111 3336"_s));
    CHECK(pos.giftCardInfo()[u"number"_s] == u"600011113336"_s);          // now showing the new card
    CHECK(pos.giftCardInfo()[u"balance"_s] == u"$50.00"_s);
    REQUIRE(pos.lookupGiftCard(u"600011112221"_s));
    CHECK(pos.giftCardInfo()[u"closed"_s].toBool());
    CHECK(pos.giftCardInfo()[u"balance"_s] == u"$0.00"_s);
    CHECK(pos.giftCardInfo()[u"replacedBy"_s].toString().endsWith(u"3336"_s));
    CHECK_FALSE(pos.transferGiftCard(u"600011112221"_s, u"900012345675"_s));  // already moved
    CHECK_FALSE(pos.sellGiftCard(u"600011112221"_s, 1000));                   // can't be reloaded

    // The old card can't pay; the new one can.
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"cobb"_s);
    CHECK_FALSE(pos.payWithGiftCard(u"600011112221"_s));
    REQUIRE(pos.payWithGiftCard(u"600011113336"_s));
    REQUIRE(pos.closeCheck());

    // Not counted as a sale or a spend in the report.
    const QVariantMap report = pos.report(u"accounts"_s);
    CHECK(rowAmount(report, u"Sold and reloaded"_s) == u"$50.00"_s);
}

TEST_CASE("Gift cards: only managers see a card's PIN or find it by PIN", "[customers][giftcards]")
{
    PosService pos(test::seedPosData(), nullptr);
    openDrawer(pos);                                           // Morgan, a manager
    REQUIRE(pos.sellGiftCard(u"6000 1111 2221"_s, 5000));
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    for (auto &card : pos.shared()->giftCards)
        if (card.number == "600011112221")
            card.pin = "4821";

    REQUIRE(pos.lookupGiftCard(u"600011112221"_s));
    CHECK(pos.giftCardInfo()[u"pin"_s] == u"4821"_s);
    REQUIRE(pos.lookupGiftCard(u"4821"_s));                    // a manager may search by PIN
    CHECK(pos.giftCardInfo()[u"found"_s].toBool());

    pos.logout();
    REQUIRE(pos.loginWithPin(u"1111"_s));                     // a server
    REQUIRE(pos.lookupGiftCard(u"600011112221"_s));
    CHECK(pos.giftCardInfo()[u"found"_s].toBool());
    CHECK(pos.giftCardInfo()[u"pin"_s] == u"••••"_s);
    for (const QVariant &v : pos.giftCardInfo()[u"matches"_s].toList())
        CHECK(v.toMap()[u"pin"_s] == u"••••"_s);
    pos.lookupGiftCard(u"4821"_s);
    CHECK_FALSE(pos.giftCardInfo()[u"found"_s].toBool());     // not by its PIN
}

TEST_CASE("Customers: a new one with the same phone, email or name opens the duplicate window", "[customers]")
{
    PosService pos(test::seedPosData(), nullptr);
    openDrawer(pos);
    REQUIRE(pos.saveCustomer({{u"name"_s, u"Dana Lee"_s}, {u"phone"_s, u"555-010-1234"_s}, {u"email"_s, u"dana@example.com"_s}}));
    REQUIRE(pos.saveCustomer({{u"name"_s, u"Alex Kim"_s}, {u"phone"_s, u"555-777-0000"_s}}));
    const auto &list = pos.shared()->customers;
    const QString dana = QString::fromStdString(list.front().id);
    const auto matchWhy = [&] { return pos.customerDuplicate()[u"matches"_s].toList().value(0).toMap()[u"why"_s].toString(); };

    // Same name, no phone: asked. Cancel saves nothing.
    CHECK_FALSE(pos.saveCustomer({{u"name"_s, u"dana lee"_s}}));
    CHECK(matchWhy() == u"same name"_s);
    REQUIRE(pos.resolveCustomerDuplicate(u"cancel"_s));
    CHECK(pos.customerDuplicate().isEmpty());
    CHECK(list.size() == 2);

    // Same email, another name: a different Dana gets saved as new on purpose.
    CHECK_FALSE(pos.saveCustomer({{u"name"_s, u"Dana Smith"_s}, {u"email"_s, u"DANA@example.com"_s}}));
    CHECK(matchWhy() == u"same email"_s);
    CHECK_FALSE(pos.resolveCustomerDuplicate(u"update"_s, u"nobody"_s));          // only one listed
    REQUIRE(pos.resolveCustomerDuplicate(u"new"_s));
    CHECK(list.size() == 3);

    // Same phone and name: "use" puts the one on file on the check, unchanged.
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    CHECK_FALSE(pos.saveCustomer({{u"name"_s, u"Dana Lee"_s}, {u"phone"_s, u"(555) 010 1234"_s}}));
    CHECK(matchWhy().contains(u"same phone"_s));
    REQUIRE(pos.resolveCustomerDuplicate(u"use"_s, dana));
    CHECK(pos.checkInfo()[u"customer"_s].toMap()[u"name"_s] == u"Dana Lee"_s);
    CHECK(list.front().email == "dana@example.com");
    CHECK(list.size() == 3);

    // Editing Alex to Dana's phone: asked too (a name alone isn't, for an edit).
    const QString alex = QString::fromStdString(list[1].id);
    CHECK_FALSE(pos.saveCustomer({{u"id"_s, alex}, {u"name"_s, u"Alex Kim"_s}, {u"phone"_s, u"555-010-1234"_s}}));
    CHECK(pos.customerDuplicate()[u"editing"_s].toBool());
    REQUIRE(pos.resolveCustomerDuplicate(u"cancel"_s));
    REQUIRE(pos.saveCustomer({{u"id"_s, alex}, {u"name"_s, u"Alex Kim"_s}, {u"phone"_s, u"555-777-0001"_s}}));
}

TEST_CASE("Subscription cards: subscription-only promotions come off once the card is shown", "[customers][giftcards]")
{
    PosService pos(test::seedPosData(), nullptr);
    openDrawer(pos);
    core::PosSettings::Promotion promo;
    promo.id = "subscription10";
    promo.name = "Subscription 10%";
    promo.percentBp = 1000;
    promo.subscriptionOnly = true;                              // the whole order, subscriptions only
    pos.shared()->settings.promotions.push_back(promo);

    // A new subscription card needs its customer on the check.
    CHECK_FALSE(pos.sellGiftCard(u"9000 1234 5675"_s, 2000));
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    REQUIRE(pos.saveCustomer({{u"name"_s, u"Pat Sullivan"_s}, {u"phone"_s, u"555-010-9999"_s}}));
    REQUIRE(pos.sellGiftCard(u"9000 1234 5675"_s, 2000, {}, u"Pat"_s));
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    REQUIRE(pos.lookupGiftCard(u"900012345675"_s));
    CHECK(pos.giftCardInfo()[u"purchasedBy"_s] == u"Pat Sullivan"_s);   // the customer on the check
    CHECK(pos.giftCardInfo()[u"issuedTo"_s] == u"Pat"_s);              // Purchased For, as typed

    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"cobb"_s);
    const QString before = pos.totals()[u"balance"_s].toString();
    REQUIRE(pos.lookupGiftCard(u"900012345675"_s));             // the customer shows their card
    CHECK(pos.checkInfo()[u"subscription"_s].toString().endsWith(u"5675"_s));
    CHECK(pos.checkInfo()[u"customer"_s].toMap()[u"name"_s] == u"Pat Sullivan"_s);   // the customer came with the card
    CHECK(pos.totals()[u"balance"_s].toString() != before);    // 10% off now
    REQUIRE(pos.payWithGiftCard(u"900012345675"_s));
    REQUIRE(pos.closeCheck());

    // Another check, no subscription card: no subscription discount.
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"cobb"_s);
    CHECK(pos.checkInfo()[u"subscription"_s].toString().isEmpty());
    CHECK(pos.totals()[u"balance"_s].toString() == before);
}

TEST_CASE("Customers and gift cards are saved and come back", "[customers][store]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"vt.db"_s);
    const auto seed = test::seedPosData();
    {
        storage::PosStore store(path);
        REQUIRE(store.open());
        REQUIRE(store.seed(seed.settings, seed.menu, seed.employees));
    }
    {
        storage::PosStore store(path);
        REQUIRE(store.open());
        storage::AsyncWriter writer(path);
        storage::SqlPosSink sink(writer);
        PosService pos(*store.load(), &sink);
        openDrawer(pos);
        REQUIRE(pos.saveCustomer({{u"name"_s, u"Dana"_s}, {u"phone"_s, u"5550101"_s}}));
        REQUIRE(pos.sellGiftCard(u"1234567"_s, 3000));
        REQUIRE(pos.tender(u"cash"_s));
        REQUIRE(pos.closeCheck());
    }
    storage::PosStore store(path);
    REQUIRE(store.open());
    const auto data = store.load();
    REQUIRE(data);
    REQUIRE(data->customers.size() == 1);
    CHECK(data->customers[0].name == "Dana");
    REQUIRE(data->giftCards.size() == 1);
    CHECK(data->giftCards[0].balance == Money::fromCents(3000));
}

TEST_CASE("Customer display: the guest chooses a tip; it goes on their card", "[customers][display]")
{
    PosService pos(test::seedPosData(), nullptr);
    openDrawer(pos);
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"cobb"_s);                                   // $12.50 + tax = $13.53
    CHECK_FALSE(pos.customerTip(u"percent"_s, 2000));         // nobody asked
    CHECK_FALSE(pos.customerPrompt()[u"askingTip"_s].toBool());
    REQUIRE(pos.askForTip());
    QVariantMap prompt = pos.customerPrompt();
    CHECK(prompt[u"askingTip"_s].toBool());
    REQUIRE(prompt[u"choices"_s].toList().size() == 4);
    CHECK(prompt[u"choices"_s].toList()[1].toMap()[u"amount"_s] == u"$2.44"_s);   // 18% of $13.53

    REQUIRE(pos.customerTip(u"percent"_s, 1800));
    prompt = pos.customerPrompt();
    CHECK_FALSE(prompt[u"askingTip"_s].toBool());
    CHECK(prompt[u"tip"_s] == u"$2.44"_s);
    REQUIRE(pos.tender(u"credit"_s));                         // the card takes it
    CHECK(pos.totals()[u"tips"_s].toString() == u"$2.44"_s);
    REQUIRE(pos.closeCheck());

    // A card already there takes the tip right away; "no tip" is fine too.
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"coffee"_s);
    REQUIRE(pos.tender(u"credit"_s));
    REQUIRE(pos.askForTip());
    REQUIRE(pos.customerTip(u"amount"_s, 100));
    CHECK(pos.totals()[u"tips"_s].toString() == u"$1.00"_s);
    REQUIRE(pos.askForTip());
    REQUIRE(pos.customerTip(u"none"_s, 0));
    CHECK_FALSE(pos.totals()[u"hasTips"_s].toBool());
    REQUIRE(pos.closeCheck());
    CHECK_FALSE(pos.customerPrompt()[u"tipChosen"_s].toBool());   // the next check starts clean
}

TEST_CASE("Older stores: the gift tender becomes a gift card tender, House Account is added, once", "[customers][upgrade]")
{
    QJsonObject old{{u"tenders"_s, QJsonArray{QJsonObject{{u"id"_s, u"cash"_s}, {u"name"_s, u"Cash"_s}, {u"kind"_s, u"cash"_s}},
                                               QJsonObject{{u"id"_s, u"gift"_s}, {u"name"_s, u"Gift Card"_s}, {u"kind"_s, u"card"_s}}}}};
    core::PosSettings s = app::settingsFromJson(old);
    REQUIRE(s.tender("gift"));
    CHECK(s.tender("gift")->kind == core::TenderKind::GiftCard);
    REQUIRE(s.tender("house"));
    CHECK(s.tender("house")->kind == core::TenderKind::HouseAccount);

    // Saved once, a manager's later choices stand (House Account removed).
    s.tenders.pop_back();
    const core::PosSettings again = app::settingsFromJson(app::toJson(s));
    CHECK_FALSE(again.tender("house"));
}

TEST_CASE("Customer detail report: visits, spending, last visit and favorite, best first", "[customers][reports]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.saveCustomer({{u"name"_s, u"Dana Lee"_s}, {u"phone"_s, u"555-0101"_s}}));
    const QString dana = QString::fromStdString(pos.shared()->customers.front().id);
    for (int visit = 0; visit < 2; ++visit) {
        REQUIRE(pos.startCheck(core::CheckType::Takeout));
        REQUIRE(pos.useCustomer(dana));
        pos.addItem(u"cobb"_s);
        REQUIRE(pos.tender(u"credit"_s));
        REQUIRE(pos.closeCheck());
    }
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.setCustomer({{u"name"_s, u"Walk-in Sam"_s}});
    pos.addItem(u"soda"_s);
    pos.chooseOption(u"drink-size"_s, 0);
    pos.finishChoosing();
    REQUIRE(pos.tender(u"credit"_s));
    REQUIRE(pos.closeCheck());

    const core::Report r = pos.buildReport(u"customers"_s);
    std::vector<std::vector<std::string>> lines;
    for (const core::ReportRow &x : r.rows)
        if (x.kind == core::ReportRow::Kind::Line)
            lines.push_back(x.cells);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0][0] == "Dana Lee  ·  Cobb");        // spent the most: first; what they order most
    CHECK(lines[0][1] == "2");
    CHECK(lines[1][0].starts_with("Walk-in Sam"));
    CHECK(lines[1][1] == "1");
}

TEST_CASE("Labels screen: lists Neil's ILSA price book while it's there", "[labels]")
{
    QTemporaryDir home;
    REQUIRE(home.isValid());
    const QByteArray oldHome = qgetenv("HOME");
    qputenv("HOME", home.path().toUtf8());
    QDir().mkpath(home.path() + u"/.local/share/ViewTouch"_s);
    const QString book = home.path() + u"/.local/share/ViewTouch/ilsa_temp_pricebook.db"_s;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, u"test_pricebook"_s);
        db.setDatabaseName(book);
        REQUIRE(db.open());
        QSqlQuery q(db);
        REQUIRE(q.exec(u"CREATE TABLE items(upc, name, dept_num, dept_grp, category, price, cost, mfg, prod_grp, sub_grp, pack_qty, altupc)"_s));
        REQUIRE(q.exec(u"INSERT INTO items VALUES('076452361243','RYJ 1875 Clem Tubo','20','CIGARS','PREMIUM CIGARS',120.0,8,"
                       "'ROMEO Y JULIETA','Churchill','Tubo',1,'076452361250')"_s));
        REQUIRE(q.exec(u"INSERT INTO items VALUES('01234567890','ZIPPO CLASSIC','30','','LIGHTERS',22.5,10,'GENERAL','','',1,NULL)"_s));
        db.close();
    }
    QSqlDatabase::removeDatabase(u"test_pricebook"_s);

    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.browseRetailLabels({}));
    CHECK(names(pos.retailLabels()[u"children"_s].toList()) == QStringList{u"CIGARS"_s, u"RETAIL"_s});
    CHECK(pos.retailLabels()[u"levelTitle"_s].toString() == u"Department groups"_s);
    REQUIRE(pos.browseRetailLabels({u"RETAIL"_s, u"LIGHTERS"_s}));
    CHECK(names(pos.retailLabels()[u"children"_s].toList()) == QStringList{u"ZIPPO"_s});   // GENERAL: brand from the name
    REQUIRE(pos.showRetailLabelItems({}));                                               // Show Items at the top: everything
    CHECK(pos.retailLabels()[u"showingItems"_s].toBool());
    CHECK(pos.retailLabels()[u"matches"_s].toList().size() == 2);
    REQUIRE(pos.browseRetailLabels({u"CIGARS"_s, u"PREMIUM CIGARS"_s, u"ROMEO Y JULIETA"_s}));
    CHECK_FALSE(pos.retailLabels()[u"showingItems"_s].toBool());                         // ILSA's product group below the brand
    CHECK(names(pos.retailLabels()[u"children"_s].toList()) == QStringList{u"CHURCHILL"_s});
    CHECK(pos.retailLabels()[u"levelTitle"_s].toString() == u"Product groups"_s);
    REQUIRE(pos.browseRetailLabels({u"CIGARS"_s, u"PREMIUM CIGARS"_s, u"ROMEO Y JULIETA"_s, u"CHURCHILL"_s, u"TUBO"_s}));
    CHECK(pos.retailLabels()[u"showingItems"_s].toBool());                               // the bottom: its items
    REQUIRE(pos.showRetailLabelItems({u"CIGARS"_s, u"PREMIUM CIGARS"_s, u"ROMEO Y JULIETA"_s}));
    CHECK(pos.retailLabels()[u"itemCount"_s].toInt() == 1);
    const QVariantList at = pos.retailLabels()[u"matches"_s].toList();
    REQUIRE(at.size() == 1);
    CHECK(at.first().toMap()[u"price"_s].toString() == u"$120.00"_s);
    CHECK(at.first().toMap()[u"upcs"_s].toList().size() == 2);
    REQUIRE(pos.findRetailLabels(u"076452361250"_s));                                    // an alternate UPC scans
    CHECK(names(pos.retailLabels()[u"matches"_s].toList()) == QStringList{u"RYJ 1875 CLEM TUBO"_s});
    REQUIRE(pos.findRetailLabels(u"01234567890"_s));                                     // codes exactly as the price book has them
    CHECK(names(pos.retailLabels()[u"matches"_s].toList()) == QStringList{u"ZIPPO CLASSIC"_s});
    CHECK(pos.retailLabels()[u"matches"_s].toList().first().toMap()[u"upcs"_s].toList().first().toMap()[u"code"_s].toString()
          == u"01234567890"_s);                                                           // no check digit added

    QFile::remove(book);                                                                  // gone: back to ViewTouch's menu
    REQUIRE(pos.findRetailLabels(u"zippo"_s));
    CHECK(pos.retailLabels()[u"matches"_s].toList().isEmpty());
    qputenv("HOME", oldHome);
}

TEST_CASE("Labels screen: once the price book is in ViewTouch, it lists ViewTouch's items, not Neil's file", "[labels]")
{
    QTemporaryDir home;
    REQUIRE(home.isValid());
    const QByteArray oldHome = qgetenv("HOME");
    qputenv("HOME", home.path().toUtf8());
    QDir().mkpath(home.path() + u"/.local/share/ViewTouch"_s);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, u"test_pricebook2"_s);
        db.setDatabaseName(home.path() + u"/.local/share/ViewTouch/ilsa_temp_pricebook.db"_s);
        REQUIRE(db.open());
        QSqlQuery q(db);
        REQUIRE(q.exec(u"CREATE TABLE items(upc, name, dept_num, dept_grp, category, price, cost, mfg, prod_grp, sub_grp, pack_qty, altupc)"_s));
        REQUIRE(q.exec(u"INSERT INTO items VALUES('01234567890','ZIPPO CLASSIC','30','','LIGHTERS',22.5,10,'GENERAL','','',1,NULL)"_s));
        db.close();
    }
    QSqlDatabase::removeDatabase(u"test_pricebook2"_s);

    app::PosData data = test::seedPosData();
    for (int i = 0; i < 1000; ++i) {                                                     // the imported price book
        core::MenuItem m;
        m.id = "ilsa-" + std::to_string(i);
        m.name = "ITEM " + std::to_string(i);
        m.family = data.menu.front().family;
        m.barcodes = {std::to_string(70000000000LL + i)};
        data.menu.push_back(m);
    }
    PosService pos(std::move(data), nullptr);
    REQUIRE(pos.findRetailLabels(u"01234567890"_s));                                     // Neil's file: not read
    CHECK(pos.retailLabels()[u"matches"_s].toList().isEmpty());
    REQUIRE(pos.findRetailLabels(u"70000000005"_s));                                     // ViewTouch's own item
    CHECK(names(pos.retailLabels()[u"matches"_s].toList()) == QStringList{u"ITEM 5"_s});
    qputenv("HOME", oldHome);
}

TEST_CASE("Price book: maker, product group, sub group and department group; filled in on a later import", "[labels][import]")
{
    QTemporaryDir home;
    REQUIRE(home.isValid());
    const QByteArray oldHome = qgetenv("HOME");
    qputenv("HOME", home.path().toUtf8());   // no ILSA temp price book here

    // Kept as it's saved.
    core::MenuItem m;
    m.id = "zen";
    m.name = "ZEN TUBE";
    m.manufacturer = "ZEN";
    m.productGroup = "TUBES";
    m.subGroup = "KING";
    const core::MenuItem back = app::menuItemFromJson(app::toJson(m));
    CHECK(back.manufacturer == "ZEN");
    CHECK(back.productGroup == "TUBES");
    CHECK(back.subGroup == "KING");

    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const auto file = [](bool withGroups) {
        QVariantMap cat{{u"id"_s, u"ilsa-tobacco-acc"_s}, {u"name"_s, u"TOBACCO ACC"_s}, {u"taxClass"_s, u"merchandise"_s},
                        {u"idCheck"_s, true}};
        QVariantMap zen{{u"id"_s, u"ilsa-16165009116"_s}, {u"name"_s, u"ZEN TUBE"_s}, {u"price"_s, 9.99}, {u"taxClass"_s, u"merchandise"_s},
                        {u"family"_s, u"ilsa-tobacco-acc"_s}, {u"barcodes"_s, QVariantList{u"016165009116"_s}}};
        QVariantMap other{{u"id"_s, u"ilsa-12345678901"_s}, {u"name"_s, u"ZEN TUBE"_s}, {u"price"_s, 8.99}, {u"taxClass"_s, u"merchandise"_s},
                          {u"family"_s, u"ilsa-tobacco-acc"_s}, {u"barcodes"_s, QVariantList{u"012345678901"_s}}};
        if (withGroups) {
            cat.insert(u"group"_s, u"TOBACCO"_s);
            zen.insert(u"manufacturer"_s, u"ZEN"_s);
            zen.insert(u"productGroup"_s, u"TUBES"_s);
            zen.insert(u"subGroup"_s, u"KING"_s);
            other.insert(u"manufacturer"_s, u"TOP"_s);
        }
        return QVariantMap{{u"format"_s, u"viewtouch-menu"_s}, {u"version"_s, 1}, {u"categories"_s, QVariantList{cat}},
                           {u"choiceGroups"_s, QVariantList{}}, {u"items"_s, QVariantList{zen, other}}, {u"pictures"_s, QVariantMap{}}};
    };
    CHECK(pos.importMenuFile(file(false)) == 2);              // tonight's import: no makers or groups yet
    CHECK(pos.importMenuFile(file(true)) == 0);               // the next one: nothing added...
    REQUIRE(pos.browseRetailLabels({}));                      // ...but the tree has them now
    CHECK(names(pos.retailLabels()[u"children"_s].toList()).contains(u"TOBACCO"_s));
    REQUIRE(pos.browseRetailLabels({u"TOBACCO"_s, u"TOBACCO ACC"_s}));
    CHECK(names(pos.retailLabels()[u"children"_s].toList()) == QStringList{u"TOP"_s, u"ZEN"_s});   // each same-name item its own
    REQUIRE(pos.browseRetailLabels({u"TOBACCO"_s, u"TOBACCO ACC"_s, u"ZEN"_s}));
    CHECK(names(pos.retailLabels()[u"children"_s].toList()) == QStringList{u"TUBES"_s});
    REQUIRE(pos.browseRetailLabels({u"TOBACCO"_s, u"TOBACCO ACC"_s, u"ZEN"_s, u"TUBES"_s}));
    CHECK(names(pos.retailLabels()[u"children"_s].toList()) == QStringList{u"KING"_s});

    // A price book's department (it has a department group): the Retail screen only. The order screens'
    // list leaves its items out; the whole list, scans and lookups still have them.
    bool retailOnly = false;
    for (const QVariant &c : pos.menuCategories())
        if (c.toMap()[u"name"_s] == u"TOBACCO ACC"_s)
            retailOnly = c.toMap()[u"retailOnly"_s].toBool();
    CHECK(retailOnly);
    // ILSA's age flag: its items ask for an ID.
    bool idChecked = false;
    for (const QVariant &c : pos.menuCategories())
        if (c.toMap()[u"name"_s] == u"TOBACCO ACC"_s)
            idChecked = std::ranges::find(pos.shared()->settings.tobaccoCategories, c.toMap()[u"id"_s].toString().toStdString())
                        != pos.shared()->settings.tobaccoCategories.end();
    CHECK(idChecked);
    CHECK(pos.orderItems().size() == pos.menuItems().size() - 2);
    CHECK_FALSE(names(pos.orderItems()).contains(u"ZEN TUBE"_s));
    CHECK(names(pos.menuItems()).contains(u"ZEN TUBE"_s));
    CHECK(pos.itemForCode(u"016165009116"_s).value(u"name"_s) == u"ZEN TUBE"_s);
    // The same UPC read as an EAN-13 (a 0 in front), or without its check digit: still that item.
    CHECK(pos.itemForCode(u"0016165009116"_s).value(u"id"_s) == u"ilsa-16165009116"_s);
    CHECK(pos.itemForCode(u"01616500911"_s).value(u"id"_s) == u"ilsa-16165009116"_s);
    CHECK(pos.itemForCode(u"1016165009116"_s).isEmpty());   // another digit in front: another code
    CHECK(pos.itemForCode(u"0016165009116"_s, true).isEmpty());   // a typed item number: exact only
    CHECK(PosService::sameBarcode("0016165009116", "016165009116"));   // stored as EAN-13, scanned as UPC-A
    CHECK_FALSE(PosService::sameBarcode("016165009116", "01616500911X"));

    // Item Look Up and Labels: found by name; an item added later is found too (the list is made again).
    REQUIRE(pos.findRetailLabels(u"zen"_s));
    CHECK(pos.retailLabels()[u"matches"_s].toList().size() == 2);
    CHECK(pos.importMenuRows({QVariantMap{{u"name"_s, u"ZEN WIDE TUBE"_s}, {u"price"_s, 10.5}, {u"category"_s, u"TOBACCO ACC"_s},
                                          {u"barcodes"_s, u"016165009123"_s}}}, QString(), false) == 1);
    REQUIRE(pos.findRetailLabels(u"zen"_s));
    CHECK(pos.retailLabels()[u"matches"_s].toList().size() == 3);

    // Menu Builder's list: the price book's departments only when open, searched, or on the card.
    const auto builderNames = [&] { return names(pos.builderItems()); };
    CHECK_FALSE(builderNames().contains(u"ZEN TUBE"_s));
    const QString before = pos.builderRevision();
    QString tobaccoAcc;
    for (const QVariant &c : pos.menuCategories())
        if (c.toMap()[u"name"_s] == u"TOBACCO ACC"_s)
            tobaccoAcc = c.toMap()[u"id"_s].toString();
    REQUIRE(pos.setBuilderView(tobaccoAcc, QString(), QString(), QString()));
    CHECK(pos.builderRevision() != before);                                    // the screen reads it again
    CHECK(builderNames().count(u"ZEN TUBE"_s) == 2);
    REQUIRE(pos.setBuilderView(QString(), u"wide"_s, QString(), QString()));  // a search
    CHECK(builderNames().contains(u"ZEN WIDE TUBE"_s));
    CHECK_FALSE(builderNames().contains(u"ZEN TUBE"_s));
    REQUIRE(pos.setBuilderView(QString(), QString(), QString(), u"016165009116"_s));   // the card's box UPC
    CHECK(builderNames().contains(u"ZEN TUBE"_s));
    qputenv("HOME", oldHome);
}

namespace {
// The barcode on the back of a license (AAMVA), with this date of birth and expiration (MMDDYYYY).
QString license(const QString &dob, const QString &expires)
{
    return u"@\n\x1e\rANSI 636000090002DL00410200ZC02410012DLDAQT64235789\nDCSSAMPLE\nDACTEST\nDBB"_s + dob
         + u"\nDBA"_s + expires + u"\nDAJCT\n"_s;
}
QString mmddyyyy(int yearsAgo) { return QDate::currentDate().addYears(-yearsAgo).toString(u"MMddyyyy"_s); }
} // namespace

TEST_CASE("ID checks: alcohol asks for the ID; a scan of the license lets it in, once per check", "[customers][age]")
{
    PosService pos(test::seedPosData(), nullptr);
    openDrawer(pos);
    pos.shared()->settings.idCheckAlcohol = true;
    REQUIRE(pos.startCheck(core::CheckType::Takeout));

    // Ringing in a beer: it waits for the ID.
    CHECK_FALSE(pos.addItem(u"draft-beer"_s));
    CHECK(pos.ageAsk()[u"age"_s] == 21);
    CHECK(pos.lines().isEmpty());

    // Under 21: refused, nothing rung in.
    CHECK_FALSE(pos.verifyAgeScan(license(mmddyyyy(19), mmddyyyy(-3))));
    CHECK(pos.ageAsk().isEmpty());
    CHECK(pos.lines().isEmpty());

    CHECK_FALSE(pos.addItem(u"draft-beer"_s));
    CHECK_FALSE(pos.verifyAgeScan(u"012345678905"_s));                    // a UPC isn't an ID

    // 30 years old, good until 3 years from now: the beer goes in; the check remembers.
    REQUIRE(pos.verifyAgeScan(license(mmddyyyy(30), mmddyyyy(-3))));
    CHECK(pos.ageAsk().isEmpty());
    CHECK(pos.lines().size() == 1);
    const core::Check *c = pos.currentCheck();
    REQUIRE(c);
    CHECK(c->ageVerified == 21);
    CHECK(c->ageHow == "scan");
    CHECK(c->ageId4 == "5789");                                          // only the last 4 of the number
    CHECK(c->ageDob.size() == 10);                                       // the date of birth, for a "with DOB" reprint
    CHECK_FALSE(c->ageIdExpired);
    REQUIRE(pos.addItem(u"draft-beer"_s));                               // no second ask
    CHECK(pos.lines().size() == 2);

    // An expired ID still shows their photo and date of birth: accepted, noted for the receipt.
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    CHECK_FALSE(pos.addItem(u"draft-beer"_s));
    REQUIRE(pos.verifyAgeScan(license(mmddyyyy(40), mmddyyyy(1))));
    CHECK(pos.currentCheck()->ageIdExpired);
    CHECK(pos.lines().size() == 1);

    // A new check: an ID that won't scan, the date of birth typed (the exception).
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    CHECK_FALSE(pos.addItem(u"draft-beer"_s));
    CHECK_FALSE(pos.verifyAgeHand(u"0101"_s));                           // not a whole date
    REQUIRE(pos.verifyAgeHand(mmddyyyy(25)));
    CHECK(pos.currentCheck()->ageHow == "hand");
    CHECK(pos.lines().size() == 1);

    // Not an alcohol check store: no ask.
    pos.shared()->settings.idCheckAlcohol = false;
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    REQUIRE(pos.addItem(u"draft-beer"_s));
}

TEST_CASE("ID checks: tobacco by category, settings saved from Site Setup", "[customers][age]")
{
    PosService pos(test::seedPosData(), nullptr);
    openDrawer(pos);                                                       // a manager
    REQUIRE(pos.saveIdChecks({{u"tobacco"_s, true}, {u"ageTobacco"_s, 21}, {u"tobaccoCategories"_s, QStringList{u"drinks"_s}}}));
    CHECK(pos.idChecks()[u"tobaccoCategories"_s].toStringList() == QStringList{u"drinks"_s});
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    CHECK_FALSE(pos.addItem(u"draft-beer"_s));                             // in a ticked category
    pos.cancelAgeCheck();
    CHECK(pos.ageAsk().isEmpty());
    CHECK(pos.lines().isEmpty());
    REQUIRE(pos.addItem(u"cobb"_s));                                       // not age-restricted
}
