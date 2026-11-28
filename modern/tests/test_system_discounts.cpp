#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "print/document.hh"
#include "print/tickets.hh"
#include "qt_catch.hh"

#include <QDateTime>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// System discounts at the store (app/pos_system_discounts.cpp, core/system_discount.hh): set up on Mass Updates >
// System Discounts, kept right on an open check by themselves (a line of their own under the item, on the screen
// and the receipt), worked out again when the quantity changes, Copy to New for several at once, ILSA's running
// discounts read from a file. Made-up items only.

namespace {

constexpr const char *kBox = "000000000031";
constexpr const char *kStick = "000000000048";

std::int64_t at(int y, int mo, int d, int h = 10, int mi = 0) { return QDateTime(QDate(y, mo, d), QTime(h, mi)).toMSecsSinceEpoch(); }

app::PosData store(bool withDefinition = true)
{
    app::PosData d = test::seedPosData();
    d.settings.tax.merchandisePpm = 63500;   // 6.35%
    d.settings.tax.cashRoundingCents = 0;
    core::MenuItem box;
    box.id = "cigar-box";
    box.name = "SAMPLE TORO 6X52 BX20";
    box.price = Money::fromCents(16999);
    box.taxClass = core::TaxClass::Merchandise;
    box.barcodes = {kBox};
    box.kitchenHide = true;
    box.recipe = {{"__retail__", 1.0}};
    core::MenuItem stick = box;
    stick.id = "cigar-stick";
    stick.name = "SAMPLE TORO 6X52";
    stick.price = Money::fromCents(999);
    stick.barcodes = {kStick};
    stick.recipe = {{"__retail__", 1.0}, {std::string("__parent__:") + kBox, 20}};
    d.menu.push_back(box);
    d.menu.push_back(stick);
    if (withDefinition) {
        core::SystemDiscount s;
        s.id = "sd-1";
        s.number = 1;
        s.name = "D26 SAMPLE TORO";
        s.start = "2026-01-01 00:00";
        s.end = "2027-01-01 00:00";
        s.levels = {{1, true, Money::fromCents(2500), 0, "DIRTY DOZEN SAVINGS!"}};
        core::sysdisc::fillLevels(s);
        s.items = {{kBox, "cigar-box", "SAMPLE TORO 6X52 BX20"}};
        d.settings.systemDiscounts.push_back(s);
    }
    return d;
}

const core::Check &current(PosService &pos)
{
    return pos.shared()->open.at(pos.checkInfo()[u"id"_s].toLongLong());
}

} // namespace

TEST_CASE("System discounts at the register: their own line under the item, on the screen and the receipt", "[sysdisc]")
{
    PosService pos(store(), nullptr);
    std::int64_t clock = at(2026, 11, 27);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.addItem(u"cigar-box"_s));
    QVariantList lines = pos.lines();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].toMap()[u"price"_s] == u"$169.99"_s);   // the item at its regular price
    CHECK(lines[0].toMap()[u"sysDiscount"_s] == u"DIRTY DOZEN SAVINGS!"_s);
    CHECK(lines[0].toMap()[u"sysDiscountPrice"_s] == u"-$25.00"_s);
    core::Totals t = current(pos).totals(pos.shared()->settings.tax);
    CHECK(t.discounts.cents() == 2500);
    CHECK(t.tax.cents() == 921);   // on the reduced price ($144.99)

    // Two boxes: $25 off each.
    const qint64 lineId = lines[0].toMap()[u"id"_s].toLongLong();
    REQUIRE(pos.setLineQuantity(lineId, 2));
    CHECK(pos.lines()[0].toMap()[u"sysDiscountPrice"_s] == u"-$50.00"_s);

    // The receipt: the line under its item, not among the check's discounts.
    const print::TicketContext ctx{pos.shared()->settings, [](std::int64_t) { return std::string("Nov 27"); },
                                   [](std::int64_t) { return std::string("10:00 AM"); }, 0};
    const std::string r = print::renderText(print::receipt(current(pos), ctx), 42);
    const auto item = r.find("SAMPLE TORO 6X52 BX20"), text = r.find("DIRTY DOZEN SAVINGS!");
    REQUIRE(item != std::string::npos);
    REQUIRE(text != std::string::npos);
    CHECK(text > item);
    CHECK(r.find("-$50.00") != std::string::npos);
    CHECK(r.find("289.98") != std::string::npos);   // the subtotal under the lines: $339.98 less $50
    CHECK(r.find("DIRTY DOZEN SAVINGS!", text + 1) == std::string::npos);   // once

    // A single stick isn't in it.
    REQUIRE(pos.addItem(u"cigar-stick"_s));
    CHECK(pos.lines()[1].toMap()[u"sysDiscount"_s].toString().isEmpty());

    // Once it's over (1/1/2027), an open check's lines lose it the next time the check changes.
    clock = at(2027, 1, 2);
    REQUIRE(pos.addItem(u"cigar-stick"_s));
    CHECK(pos.lines()[0].toMap()[u"sysDiscount"_s].toString().isEmpty());
    CHECK(current(pos).totals(pos.shared()->settings.tax).discounts.cents() == 0);
}

TEST_CASE("System discounts setup: a new one, its levels and items, Apply", "[sysdisc]")
{
    PosService pos(store(false), nullptr);
    std::int64_t clock = at(2026, 11, 27);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.sysDiscOpen(true));
    REQUIRE(pos.sysDiscNew());
    CHECK_FALSE(pos.sysDiscApply());   // no name, no amount, no items
    REQUIRE(pos.sysDiscSet({{u"name"_s, u"D26 SAMPLE TORO AND ITS STICKS 1234567890"_s}, {u"end"_s, u"01/01/2027 12:00 AM"_s}}));
    QVariantMap draft = pos.systemDiscounts()[u"draft"_s].toMap();
    CHECK(draft[u"name"_s].toString().size() == 30);   // 30 characters at most
    CHECK(draft[u"end"_s] == u"2027-01-01 00:00"_s);
    REQUIRE(pos.sysDiscSetLevel(0, {{u"qty"_s, u"1"_s}, {u"orMore"_s, true}, {u"amount"_s, u"25.00"_s},
                                    {u"receiptText"_s, u"DIRTY DOZEN SAVINGS!"_s}}));
    REQUIRE(pos.sysDiscAddItems({QString::fromLatin1(kBox)}));
    REQUIRE(pos.sysDiscAddLinked());   // the box's sticks too
    draft = pos.systemDiscounts()[u"draft"_s].toMap();
    CHECK(draft[u"itemList"_s].toList().size() == 2);
    CHECK(draft[u"level1"_s] == u"1+  $25.00"_s);
    REQUIRE(pos.sysDiscFind(u"sample toro"_s));
    const QVariantList found = pos.systemDiscounts()[u"found"_s].toList();
    REQUIRE(found.size() == 2);
    CHECK(found[0].toMap()[u"inIt"_s].toBool());
    REQUIRE(pos.sysDiscApply());
    const auto &list = pos.shared()->settings.systemDiscounts;
    REQUIRE(list.size() == 1);
    CHECK(list[0].levels[0].amount.cents() == 2500);
    CHECK(list[0].changedAt == clock);
    CHECK_FALSE(pos.systemDiscounts().contains(u"draft"_s));
    // Saved in the settings and read back the same.
    const core::SystemDiscount back = app::systemDiscountFromJson(app::toJson(list[0]));
    CHECK(back == list[0]);
}

TEST_CASE("System discounts: Copy to New for several (D26 -> D27), Delete", "[sysdisc]")
{
    app::PosData d = store();
    core::SystemDiscount other = d.settings.systemDiscounts[0];
    other.id = "sd-2";
    other.number = 2;
    other.name = "D26 ANOTHER BOX";
    d.settings.systemDiscounts.push_back(other);
    PosService pos(std::move(d), nullptr);
    pos.shared()->setClock([] { return at(2026, 12, 20); });
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.sysDiscOpen(true));
    REQUIRE(pos.sysDiscCopy({u"sd-1"_s, u"sd-2"_s}, u"D26"_s, u"D27"_s, u"01/01/2027"_s, u"01/01/2028"_s));
    const auto &list = pos.shared()->settings.systemDiscounts;
    REQUIRE(list.size() == 4);
    CHECK(list[2].name == "D27 SAMPLE TORO");
    CHECK(list[3].name == "D27 ANOTHER BOX");
    CHECK(list[2].start == "2027-01-01 00:00");
    CHECK(list[2].end == "2028-01-01 00:00");
    CHECK(list[2].items == list[0].items);
    CHECK(pos.systemDiscounts()[u"list"_s].toList().size() == 4);
    REQUIRE(pos.sysDiscShowReport({u"sd-3"_s}));
    CHECK(pos.systemDiscounts()[u"report"_s].toMap()[u"title"_s] == u"System Discount Definition Report"_s);
    REQUIRE(pos.sysDiscDelete({u"sd-1"_s, u"sd-2"_s}));
    CHECK(list.size() == 2);
}

TEST_CASE("System discounts: ILSA's running ones from a file, matched by UPC; again: replaced", "[sysdisc]")
{
    PosService pos(store(false), nullptr);
    pos.shared()->setClock([] { return at(2026, 11, 27); });
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.sysDiscOpen(true));
    const auto discount = [](const QString &id, const QString &name, const QString &end, const QString &upc) {
        return QVariantMap{{u"ilsaId"_s, id}, {u"name"_s, name}, {u"start"_s, u"2026-01-01 00:00"_s}, {u"end"_s, end},
                           {u"levels"_s, QVariantList{QVariantMap{{u"qty"_s, 1}, {u"orMore"_s, true}, {u"amount"_s, 2500},
                                                                  {u"receiptText"_s, u"DIRTY DOZEN SAVINGS!"_s}}}},
                           {u"items"_s, QVariantList{QVariantMap{{u"upc"_s, upc}, {u"name"_s, u"ILSA NAME"_s}}}}};
    };
    const QVariantMap file{{u"format"_s, u"viewtouch-system-discounts"_s}, {u"from"_s, u"ILSA BHDAN"_s},
                           {u"discounts"_s, QVariantList{discount(u"601047425"_s, u"D26 SAMPLE TORO"_s, u"2027-01-01 00:00"_s, QString::fromLatin1(kBox)),
                                                         discount(u"702286218"_s, u"708694456058"_s, u"2007-02-28 11:14"_s, QString::fromLatin1(kBox)),
                                                         discount(u"601047999"_s, u"D26 GONE"_s, u"2027-01-01 00:00"_s, u"099999999999"_s)}}};
    REQUIRE(pos.sysDiscImport(file));
    QVariantMap importing = pos.systemDiscounts()[u"importing"_s].toMap();
    CHECK(importing[u"count"_s] == 2);
    CHECK(importing[u"skipped"_s] == 1);   // the 2007 one is over
    CHECK(importing[u"notFound"_s].toList().size() == 1);
    REQUIRE(pos.sysDiscImportApply());
    const auto &list = pos.shared()->settings.systemDiscounts;
    REQUIRE(list.size() == 2);
    CHECK(list[0].items.size() == 1);
    CHECK(list[0].items[0].itemId == "cigar-box");
    CHECK(list[0].ilsaId == "601047425");
    // The same file again: replaced, not doubled.
    REQUIRE(pos.sysDiscImport(file));
    REQUIRE(pos.sysDiscImportApply());
    CHECK(list.size() == 2);
    // Not a system discount file.
    CHECK_FALSE(pos.sysDiscImport({{u"format"_s, u"viewtouch-buydown"_s}}));
}
