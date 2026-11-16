#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "layout_fixture.hh"
#include "net/layout_hub.hh"
#include "net/pos_server.hh"
#include "net/remote_session.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
using vt::app::PosShared;

// Menu Builder -> an item's card -> Price check: the store's rates and cash discount, the figures on
// this screen and on a tablet (RemoteSession). Only a calculator: checks are priced as before.

namespace {

template <typename F>
bool waitFor(F condition, int msec = 3000)
{
    QElapsedTimer t;
    t.start();
    while (!condition() && t.elapsed() < msec) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

// Store Settings -> Taxes: CT (6.35% sales, 7.35% meals and alcohol).
void setTaxes(PosService &pos, bool included, double cashPercent = 0)
{
    QVariantMap tax = pos.adminRecords(u"taxes"_s)[0].toMap();
    REQUIRE(tax.contains(u"cashDiscountPercent"_s));
    tax[u"food"_s] = 7.35;
    tax[u"alcohol"_s] = 7.35;
    tax[u"merchandise"_s] = 6.35;
    tax[u"pricesIncludeTax"_s] = included;
    tax[u"cashDiscountPercent"_s] = cashPercent;
    REQUIRE(pos.adminSave(u"taxes"_s, 0, tax));
}

QString s(const QVariantMap &m, const char *key) { return m.value(QString::fromLatin1(key)).toString(); }

} // namespace

TEST_CASE("Price check: prices include tax (HCA): $5.00 is 4.66 + 0.34; to net 4.66 charge 5.00",
          "[pricecheck][taxincluded]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    setTaxes(pos, true);
    CHECK(pos.pricing()[u"alcohol"_s].toLongLong() == 73500);
    CHECK(pos.pricing()[u"included"_s].toBool());

    const QVariantMap f = pos.priceFigures(u"5.00"_s, u"alcohol"_s, u"1.00"_s, u"4.66"_s, 25, false);
    CHECK(f[u"valid"_s].toBool());
    CHECK(f[u"included"_s].toBool());
    CHECK(s(f, "rate") == u"7.35%"_s);
    CHECK(s(f, "guestPays") == u"$5.00"_s);
    CHECK(s(f, "preTax") == u"$4.66"_s);
    CHECK(s(f, "tax") == u"$0.34"_s);
    CHECK(s(f, "chargeForNet") == u"$5.00"_s);
    CHECK(s(f, "chargeForNetPlain") == u"5.00"_s);
    CHECK(s(f, "rounded") == u"$5.00"_s);
    CHECK(s(f, "margin") == u"78.5%"_s);   // (4.66 - 1.00) / 4.66, before tax
    CHECK_FALSE(f[u"showCash"_s].toBool());

    // $6.10 rounded to the quarter: $6.00 (5.59 + 0.41).
    const QVariantMap r = pos.priceFigures(u"6.10"_s, u"alcohol"_s, {}, {}, 25, false);
    CHECK(s(r, "rounded") == u"$6.00"_s);
    CHECK(s(r, "roundedPreTax") == u"$5.59"_s);
    CHECK(s(r, "roundedTax") == u"$0.41"_s);
    CHECK_FALSE(r.contains(u"chargeForNet"_s));

    // Nothing typed yet, or not a price: nothing worked out.
    CHECK_FALSE(pos.priceFigures({}, u"alcohol"_s, {}, {}, 0, false)[u"valid"_s].toBool());
    CHECK_FALSE(pos.priceFigures(u"abc"_s, u"alcohol"_s, {}, {}, 0, false)[u"valid"_s].toBool());
}

TEST_CASE("Price check: tax on top (BHDAN): $12.99 + 6.35% = $13.81; $13.16 makes the guest pay $14.00",
          "[pricecheck]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    setTaxes(pos, false);

    const QVariantMap f = pos.priceFigures(u"12.99"_s, u"merchandise"_s, {}, {}, 100, true);
    CHECK_FALSE(f[u"included"_s].toBool());
    CHECK(s(f, "tax") == u"$0.82"_s);
    CHECK(s(f, "guestPays") == u"$13.81"_s);
    CHECK(s(f, "guestTarget") == u"$14.00"_s);
    CHECK(s(f, "priceForGuest") == u"$13.16"_s);
    CHECK(s(f, "priceForGuestPlain") == u"13.16"_s);
    CHECK(f[u"guestExact"_s].toBool());
    // 0% cash discount: no cash rows.
    CHECK_FALSE(f[u"showCash"_s].toBool());
    // A round amount typed in.
    CHECK(s(pos.priceFigures(u"12.99"_s, u"merchandise"_s, {}, u"$20"_s, 0, true), "priceForGuest") == u"$18.81"_s);
    // No tax on the item: the price is what the guest pays.
    const QVariantMap none = pos.priceFigures(u"12.99"_s, u"none"_s, {}, {}, 0, true);
    CHECK_FALSE(none[u"hasTax"_s].toBool());
    CHECK(s(none, "guestPays") == u"$12.99"_s);
}

TEST_CASE("Price check: cigarettes, card $13.50 with a 4% cash discount; a carton is 10 cash packs",
          "[pricecheck][m4][admin]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    setTaxes(pos, false, 4);
    CHECK(pos.settings().cashDiscountBp == 400);
    CHECK(pos.pricing()[u"cashDiscountPercent"_s].toDouble() == 4.0);

    const QVariantMap f = pos.priceFigures(u"13.50"_s, u"merchandise"_s, u"11.00"_s, {}, 0, true);
    REQUIRE(f[u"showCash"_s].toBool());
    CHECK(s(f, "cashPercent") == u"4%"_s);
    CHECK(s(f, "card") == u"$13.50"_s);
    CHECK(s(f, "cardWithTax") == u"$14.36"_s);
    CHECK(s(f, "cash") == u"$12.96"_s);
    CHECK(s(f, "cashWithTax") == u"$13.78"_s);
    CHECK(s(f, "carton") == u"$129.60"_s);
    CHECK(s(f, "cartonWithTax") == u"$137.83"_s);
    CHECK(s(f, "marginCard") == u"18.5%"_s);
    CHECK(s(f, "marginCash") == u"15.1%"_s);
    // Not a retail item (a drink): no cash rows; the panel's own % overrides the store's while typed.
    CHECK_FALSE(pos.priceFigures(u"13.50"_s, u"merchandise"_s, {}, {}, 0, false)[u"showCash"_s].toBool());
    CHECK(s(pos.priceFigures(u"13.50"_s, u"merchandise"_s, {}, {}, 0, true, u"0"_s), "cash").isEmpty());
    CHECK(s(pos.priceFigures(u"10.00"_s, u"merchandise"_s, {}, {}, 0, true, u"5"_s), "cash") == u"$9.50"_s);

    // Kept with the settings; checks are priced as before (the card price).
    const core::PosSettings back = app::settingsFromJson(app::toJson(pos.settings()));
    CHECK(back.cashDiscountBp == 400);

    // Saved from the Price check (managers), and refused out of range.
    REQUIRE(pos.saveCashDiscount(3.5));
    CHECK(pos.settings().cashDiscountBp == 350);
    CHECK(pos.adminRecords(u"taxes"_s)[0].toMap()[u"cashDiscountPercent"_s].toDouble() == 3.5);
    CHECK_FALSE(pos.saveCashDiscount(100));
    CHECK_FALSE(pos.saveCashDiscount(-1));
    CHECK(pos.settings().cashDiscountBp == 350);
    QVariantMap tax = pos.adminRecords(u"taxes"_s)[0].toMap();
    tax[u"cashDiscountPercent"_s] = 150;
    CHECK_FALSE(pos.adminSave(u"taxes"_s, 0, tax));
    CHECK(pos.settings().cashDiscountBp == 350);
}

TEST_CASE("Price check: a tablet gets the rates and the cash discount, and works the figures out itself",
          "[pricecheck][net][remote]")
{
    test::RecordingSink sink;
    PosShared shared{test::seedPosData(), &sink};
    auto layout = test::loadTestLayout();
    REQUIRE(layout);
    net::LayoutHub hub{*layout};
    net::PosServer server{&shared, &hub};
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    core::TerminalConfig t;
    t.name = "Bar 1";
    t.id = net::newDeviceId().toStdString();
    const QByteArray key = net::newDeviceKey();
    t.key = key.toBase64().toStdString();
    shared.settings.terminals.push_back(t);
    net::Credentials c;
    c.host = u"127.0.0.1"_s;
    c.port = server.port();
    c.terminalId = QString::fromStdString(t.id);
    c.terminalName = u"Bar 1"_s;
    c.key = key;
    net::RemoteSession tablet(u"Bar 1"_s);
    tablet.setCredentials(c);
    tablet.connectTo(u"127.0.0.1"_s, server.port());
    REQUIRE(tablet.waitForWelcome(5000));

    // Store Settings -> Taxes, saved on the main computer: the tablet hears of it.
    PosService office(&shared, u"Office"_s);
    REQUIRE(office.loginWithPin(u"1234"_s));
    setTaxes(office, false, 4);
    REQUIRE(waitFor([&] { return tablet.pricing()[u"cashDiscountPercent"_s].toDouble() == 4.0; }));
    CHECK(tablet.pricing()[u"merchandise"_s].toLongLong() == 63500);
    CHECK_FALSE(tablet.pricing()[u"included"_s].toBool());
    const QVariantMap f = tablet.priceFigures(u"13.50"_s, u"merchandise"_s, {}, {}, 0, true);
    CHECK(s(f, "cash") == u"$12.96"_s);
    CHECK(s(f, "cartonWithTax") == u"$137.83"_s);

    // The cash % changed on the tablet's Price check: saved on the main computer, the same everywhere.
    tablet.invoke(u"loginWithPin"_s, {u"1234"_s});
    REQUIRE(waitFor([&] { return tablet.loggedIn(); }));
    tablet.saveCashDiscount(3);
    REQUIRE(waitFor([&] { return shared.settings.cashDiscountBp == 300; }));
    REQUIRE(waitFor([&] { return tablet.pricing()[u"cashDiscountPercent"_s].toDouble() == 3.0; }));
    CHECK(office.pricing()[u"cashDiscountPercent"_s].toDouble() == 3.0);

    // Prices include tax switched on: the tablet backs the tax out.
    setTaxes(office, true, 3);
    REQUIRE(waitFor([&] { return tablet.pricing()[u"included"_s].toBool(); }));
    CHECK(s(tablet.priceFigures(u"5.00"_s, u"alcohol"_s, {}, {}, 0, false), "preTax") == u"$4.66"_s);
}
