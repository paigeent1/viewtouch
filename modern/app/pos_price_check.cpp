// Menu Builder -> an item's card -> Price check: a price with tax, and the way back; cash vs card for
// cigarettes. Only a calculator (core/price_check.hh): checks are priced and tendered as always.

#include "app/pos_service.hh"
#include "app/pos_session.hh"
#include "core/price_check.hh"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

std::int64_t rateFor(const QVariantMap &pricing, const QString &taxClass)
{
    if (taxClass == u"food" || taxClass == u"alcohol" || taxClass == u"merchandise" || taxClass == u"room")
        return std::max<qint64>(0, pricing.value(taxClass).toLongLong());
    return 0;   // none
}

// 4 -> "4%", 7.35 -> "7.35%", 9.975 -> "9.975%"
QString percentText(double percent)
{
    return QString::number(percent, 'f', 3).remove(QRegularExpression(u"\\.?0+$"_s)) + u'%';
}

// A margin in basis points, to one decimal: 1852 -> "18.5%".
QString marginText(std::int64_t bp)
{
    return QString::number(double(bp) / 100.0, 'f', 1) + u'%';
}

// For a price field: "13.16" (no currency sign).
QString plain(Money m)
{
    return QString::fromStdString(m.toString());
}

} // namespace

QVariantMap PosService::pricing() const
{
    const TaxRates &t = s_->settings.tax;
    return {{u"food"_s, qint64(t.foodPpm)}, {u"alcohol"_s, qint64(t.alcoholPpm)},
            {u"merchandise"_s, qint64(t.merchandisePpm)}, {u"room"_s, qint64(t.roomPpm)},
            {u"included"_s, t.pricesIncludeTax},
            {u"cashDiscountPercent"_s, double(s_->settings.cashDiscountBp) / 100.0}};
}

bool PosService::saveCashDiscount(double percent)
{
    if (!require(perm::Manager, tr("The cash discount")))
        return false;
    if (!(percent >= 0 && percent < 100))
        return fail(tr("The cash discount must be at least 0 and under 100%."));
    const std::int64_t bp = std::llround(percent * 100.0);
    if (bp == s_->settings.cashDiscountBp)
        return true;
    s_->settings.cashDiscountBp = bp;
    s_->saveSettings();   // every screen's Price check (and Store Settings -> Taxes) shows it
    emit notice(tr("Cash discount saved: %1").arg(percentText(double(bp) / 100.0)));
    return true;
}

QVariantMap PosSession::priceFigures(const QString &priceText, const QString &taxClass, const QString &costText,
                                     const QString &targetText, int roundCents, bool retail,
                                     const QString &cashPercent) const
{
    using namespace vt::core::pricecheck;
    const QVariantMap rates = pricing();
    const bool included = rates.value(u"included"_s).toBool();
    const std::int64_t ppm = rateFor(rates, taxClass);
    const QString symbol = currencySymbol();
    const auto money = [this](Money m) { return formatCents(m.cents()); };
    QVariantMap out{{u"included"_s, included}, {u"hasTax"_s, ppm > 0},
                    {u"rate"_s, percentText(double(ppm) / 10000.0)}};

    const qint64 priceCentsIn = priceText.trimmed().isEmpty() ? -1 : priceCents(priceText, symbol);
    if (priceCentsIn <= 0) {
        out.insert(u"valid"_s, false);
        return out;
    }
    out.insert(u"valid"_s, true);
    const Money price = Money::fromCents(priceCentsIn);
    const qint64 costIn = costText.trimmed().isEmpty() ? -1 : priceCents(costText, symbol);
    const Money cost = Money::fromCents(std::max<qint64>(0, costIn));
    const bool hasCost = costIn > 0;
    const qint64 targetIn = targetText.trimmed().isEmpty() ? -1 : priceCents(targetText, symbol);
    const std::int64_t step = roundCents > 1 ? roundCents : 0;

    // What a price comes to: with tax (what the guest pays) and before tax (what the store keeps).
    const auto guestPays = [&](Money m) { return included ? m : addOn(m, ppm).guestPays; };
    const auto beforeTax = [&](Money m) { return included ? backOut(m, ppm).preTax : m; };

    if (included) {
        const Included in = backOut(price, ppm);
        out.insert(u"preTax"_s, money(in.preTax));
        out.insert(u"tax"_s, money(in.tax));
        out.insert(u"guestPays"_s, money(price));
        if (targetIn > 0) {   // to net $X before tax, charge $Y
            const Money charge = chargeToNet(Money::fromCents(targetIn), ppm);
            out.insert(u"netTarget"_s, money(Money::fromCents(targetIn)));
            out.insert(u"chargeForNet"_s, money(charge));
            out.insert(u"chargeForNetPlain"_s, plain(charge));
        }
        if (step > 0) {   // the guest price rounded, and what it leaves before tax
            const Money r = roundTo(price, step);
            const Included rin = backOut(r, ppm);
            out.insert(u"rounded"_s, money(r));
            out.insert(u"roundedPlain"_s, plain(r));
            out.insert(u"roundedPreTax"_s, money(rin.preTax));
            out.insert(u"roundedTax"_s, money(rin.tax));
        }
    } else {
        const Added a = addOn(price, ppm);
        out.insert(u"preTax"_s, money(price));
        out.insert(u"tax"_s, money(a.tax));
        out.insert(u"guestPays"_s, money(a.guestPays));
        // To make the guest pay a round $Y: the amount typed, else what they pay now, rounded.
        const Money want = targetIn > 0 ? Money::fromCents(targetIn) : step > 0 ? roundTo(a.guestPays, step) : Money();
        if (want.cents() > 0) {
            const ForGuest g = priceForGuest(want, ppm);
            out.insert(u"guestTarget"_s, money(want));
            out.insert(u"priceForGuest"_s, money(g.price));
            out.insert(u"priceForGuestPlain"_s, plain(g.price));
            out.insert(u"guestGets"_s, money(g.guestPays));   // a cent under `want` when none is exact
            out.insert(u"guestExact"_s, g.guestPays == want);
        }
    }

    if (hasCost) {
        out.insert(u"margin"_s, marginText(marginBp(beforeTax(price), cost)));
        out.insert(u"profit"_s, money(beforeTax(price) - cost));
    }

    // Retail (cigarettes): the price on the card is the card price; cash is that less the discount;
    // a carton is 10 cash packs, the same in cash or by card. 0%: no cash rows.
    double cashPct = cashPercent.trimmed().isEmpty() ? rates.value(u"cashDiscountPercent"_s).toDouble()
                                                    : QString(cashPercent).remove(u'%').trimmed().toDouble();
    if (!(cashPct >= 0 && cashPct < 100))
        cashPct = 0;
    const std::int64_t cashBp = std::llround(cashPct * 100.0);
    const bool showCash = retail && cashBp > 0;
    out.insert(u"showCash"_s, showCash);
    if (showCash) {
        const Money cash = cashPrice(price, cashBp);
        const Money carton = cartonPrice(cash);
        out.insert(u"cashPercent"_s, percentText(double(cashBp) / 100.0));
        out.insert(u"card"_s, money(price));
        out.insert(u"cardWithTax"_s, money(guestPays(price)));
        out.insert(u"cash"_s, money(cash));
        out.insert(u"cashWithTax"_s, money(guestPays(cash)));
        out.insert(u"carton"_s, money(carton));
        out.insert(u"cartonWithTax"_s, money(guestPays(carton)));
        if (hasCost) {
            out.insert(u"marginCard"_s, marginText(marginBp(beforeTax(price), cost)));
            out.insert(u"marginCash"_s, marginText(marginBp(beforeTax(cash), cost)));
        }
    }
    return out;
}

} // namespace vt::app
