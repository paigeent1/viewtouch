// PosService: ITEM RETURN on the Retail screen (owner 2026-11-19). The button asks "the old sale, or
// scan the item?". The old sale is Find a Check (Refund… there, as before). Scanned (or a UPC or part
// of the name typed), the item is shown at its price-book price, which the cashier may change if it
// was sold for another amount, with how many and why. Add puts a RETURN line on the sale: a negative
// quantity at that price, taxed in the item's own class so the tax comes back too (always: no date
// check, owner 2026-11-19), never discounted, a manager's approval, an exception (EOD, Loss Watch).
// The sale is paid as usual; less than nothing is a cash refund out of the drawer (tender()), and
// closing it puts the items back in stock the way a sale takes them out (takeStock on the negative).
// Owner 2026-11-22: a return that leaves the sale owing the customer takes the Retail screen to Pay
// (refundToGive), where Cash gives the refund, closes the sale and opens the drawer. Only a terminal
// with a cash drawer gives cash back; on one without (a tablet) the cashier suspends the sale and the
// register recalls it and gives the refund there.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <algorithm>
#include <cstdlib>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {
constexpr qint64 kMostCents = 1'000'000;   // $10,000.00: more is a typo
constexpr int kMostMatches = 8;
} // namespace

QStringList PosService::itemReturnReasons()
{
    return {tr("Defective"), tr("Wrong item"), tr("Changed mind"), tr("Other")};
}

bool PosService::startItemReturn()
{
    if (!require(perm::Order, tr("Item returns")))
        return false;
    priceCheck_.remove(u"item"_s);   // one window at a time
    itemReturn_ = {{u"on"_s, true}, {u"step"_s, u"choose"_s}};
    emit sessionChanged();
    return true;
}

bool PosService::itemReturnStep(const QString &step)
{
    if (!itemReturn_.value(u"on"_s).toBool())
        return false;
    if (step != u"choose" && step != u"scan")
        return false;
    itemReturn_ = {{u"on"_s, true}, {u"step"_s, step}};
    emit sessionChanged();
    return true;
}

void PosService::cancelItemReturn()
{
    if (itemReturn_.isEmpty())
        return;
    itemReturn_.clear();
    emit sessionChanged();
}

bool PosService::showItemReturn(const MenuItem &m, const QString &code)
{
    if (m.isModifier)
        return fail(tr("%1 is a choice, not an item.").arg(qs(m.name)));
    if (m.byWeight)
        return fail(tr("%1 is sold by weight: find the old sale instead.").arg(qs(m.name)));
    const QString upc = !code.isEmpty() && std::ranges::any_of(m.barcodes, [&](const std::string &b) { return sameBarcode(b, ss(code)); })
                            ? code
                            : m.barcodes.empty() ? QString() : qs(m.barcodes.front());
    itemReturn_ = {{u"on"_s, true}, {u"step"_s, u"price"_s},
                   {u"item"_s, QVariantMap{{u"id"_s, qs(m.id)}, {u"name"_s, qs(m.name)}, {u"upc"_s, upc},
                                           {u"priceCents"_s, qint64(m.price.cents())}, {u"price"_s, format(m.price)},
                                           {u"taxIncluded"_s, s_->settings.tax.pricesIncludeTax}}},
                   {u"reasons"_s, itemReturnReasons()}};
    emit sessionChanged();
    return true;
}

bool PosService::itemReturnCode(const QString &code)
{
    if (!itemReturn_.value(u"on"_s).toBool())
        return false;
    const QString c = code.trimmed();
    if (c.isEmpty())
        return fail(tr("Scan the item, or type its UPC or name."));
    if (const MenuItem *m = findItem(c); m && !m->isModifier)
        return showItemReturn(*m, c);
    // Part of a name: the items it fits (a few), to touch one.
    const bool digits = std::ranges::all_of(c, [](QChar ch) { return ch.isDigit(); });
    QVariantList matches;
    const MenuItem *only = nullptr;
    if (!digits && c.size() >= 2) {
        for (const MenuItem &m : s_->menu) {
            if (m.isModifier || m.byWeight || !qs(m.name).contains(c, Qt::CaseInsensitive))
                continue;
            only = matches.isEmpty() ? &m : nullptr;
            matches.append(QVariantMap{{u"id"_s, qs(m.id)}, {u"name"_s, qs(m.name)}, {u"price"_s, format(m.price)},
                                       {u"upc"_s, m.barcodes.empty() ? QString() : qs(m.barcodes.front())}});
            if (matches.size() >= kMostMatches)
                break;
        }
    }
    if (matches.size() == 1 && only)
        return showItemReturn(*only, {});
    if (matches.isEmpty())
        return fail(tr("No item for %1").arg(c));
    itemReturn_ = {{u"on"_s, true}, {u"step"_s, u"scan"_s}, {u"typed"_s, c}, {u"matches"_s, matches}};
    emit sessionChanged();
    return true;
}

bool PosService::itemReturnPick(const QString &itemId)
{
    if (!itemReturn_.value(u"on"_s).toBool())
        return false;
    const MenuItem *m = findItem(itemId);
    if (!m)
        return fail(tr("That item is gone from the menu."));
    return showItemReturn(*m, {});
}

bool PosService::addItemReturn(const QString &itemId, qint64 priceCents, int quantity, const QString &reason,
                               const QString &upc)
{
    if (!require(perm::Order, tr("Item returns")))
        return false;
    const MenuItem *item = findItem(itemId);
    if (!item || item->isModifier)
        return fail(tr("Scan the item first."));
    if (item->byWeight)
        return fail(tr("%1 is sold by weight: find the old sale instead.").arg(qs(item->name)));
    if (priceCents <= 0 || priceCents > kMostCents)
        return fail(tr("Type the price it was sold for."));
    if (quantity < 1 || quantity > 99)
        return fail(tr("How many: 1 to 99."));
    const QString why = reason.trimmed().left(80);
    if (why.isEmpty())
        return fail(tr("Touch a reason for the return."));
    if (const Check *c = current(); c && c->status != CheckStatus::Open)
        return fail(tr("This check is closed."));
    // A manager's call (owner's default; Store Settings could change it later), approved on the spot
    // like a void. The approval is noted on the sale (the Exceptions search shows who).
    const bool hadCheck = current() != nullptr;
    const std::string approver = approved_ ? approved_->by : std::string();
    if (!require(perm::Manager, tr("Item returns")))
        return false;
    if (!current() && !startCheck(CheckType::Quick))
        return false;
    Check &c = *current();
    if (!hadCheck && !approver.empty())
        noteEvent(c, tr("%1: approved by %2").arg(tr("Item returns"), qs(approver)), "approval", {}, approver);

    MenuItem priced = *item;
    priced.price = Money::fromCents(priceCents);
    OrderLine &line = c.addItem(priced);
    stampNewLine(line);
    line.quantity = -quantity;
    line.returnReason = ss(why);
    if (!upc.trimmed().isEmpty())
        line.upc = ss(upc.trimmed().left(40));
    line.handed = true;          // the customer brought it: nothing for a printer or a station
    line.kitchenHide = true;
    line.printer.clear();
    line.station.clear();
    line.noDiscount = true;      // a percent off a return would charge the customer
    line.stdDrinks = 0;
    line.drinkSeats.clear();
    line.seat = seat_;
    line.course = course_;
    selectedLine_ = line.id;
    lineTouched_ = false;
    const Money worth = -line.total();
    const QString name = quantity > 1 ? u"%1 x %2"_s.arg(quantity).arg(qs(item->name)) : qs(item->name);
    noteEvent(c, tr("Item return %1 (%2)").arg(name, format(worth)), "itemReturn", worth, ss(why));
    itemReturn_.clear();
    emit sessionChanged();
    emit notice(tr("Return: %1").arg(name));
    changed(c);
    // Less than nothing now: a refund to give. The Retail screen goes to Pay (owner 2026-11-22), where
    // Cash gives it and opens the drawer; a terminal without a drawer says to suspend the sale.
    // An exchange that still owes money stays where it is.
    if (refundDue(c.totals(s_->settings.tax)).cents() > 0)
        emit refundToGive(c.id);
    return true;
}

} // namespace vt::app
