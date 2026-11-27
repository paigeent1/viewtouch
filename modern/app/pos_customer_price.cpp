// PosService: a customer's own prices (owner 2026-11-26). A customer record can say "<source> price" (the retail
// lines at the price source's base price: this store's price zone markup taken off) and a preferential percent
// (off items in categories marked Preferential pricing; never No discounts items: cigarettes). While that customer
// is on the check, their discount lines are kept right by themselves, as promotions are: one line per item,
// "<store> price" and "<customer>'s price", worked out again whenever the check changes and gone when the
// customer is taken off. Both on one line: the base price first, then the percent off what is left.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {
constexpr const char *kStoreRef = "custprice:store:";
constexpr const char *kPrefRef = "custprice:pref:";
constexpr const char *kPrefTender = "customer-price";
} // namespace

void PosService::applyCustomerPricing(Check &c)
{
    if (c.status != CheckStatus::Open)
        return;
    const CustomerRecord *who = nullptr;
    if (!c.customerId.empty())
        for (const CustomerRecord &r : s_->customers)
            if (r.id == c.customerId)
                who = &r;
    const bool store = who && who->sourcePrice && !s_->settings.storePricesFrom.empty();
    const bool pref = who && who->preferentialPercent > 0;
    struct Want { std::string ref, tender, name; std::int64_t lineId; Money amount; };
    std::vector<Want> want;
    if (store || pref) {
        std::set<std::string> preferential;
        if (pref)
            for (const MenuCategory &cat : s_->categories())
                if (cat.preferential && !cat.noDiscount)
                    preferential.insert(cat.id);
        const std::string storeName = ss(tr("%1 price").arg(qs(s_->settings.storePricesFrom)));
        const std::string prefName = ss(tr("%1's price").arg(qs(who->name)));
        for (const OrderLine &l : c.lines) {
            if (l.voided || l.isComment() || l.isGiftCard() || l.isReturn() || l.counted() <= 0)
                continue;
            const MenuItem *m = findItem(qs(l.itemId));
            if (!m)
                continue;
            Money off;
            // (A store price given by hand, OTHER STORE'S PRICE, counts as this one.)
            const bool byHand = std::ranges::any_of(c.payments, [&](const Payment &p) {
                return p.tenderId == kStorePriceTender && p.lineId == l.id && !p.reference.starts_with(kStoreRef);
            });
            if (store && !byHand && itemIsRetail(*m))
                if (const auto base = basePriceOf(*m); base && *base < l.unitPrice.cents()) {
                    off = Money::fromCents((l.unitPrice.cents() - *base) * l.counted());
                    want.push_back({kStoreRef + std::to_string(l.id), kStorePriceTender, storeName, l.id, off});
                }
            if (pref && !l.noDiscount && !m->noDiscount && preferential.contains(m->family)) {
                const Money left = l.total() - off;
                const Money p = Money::fromCents(std::llround(double(left.cents()) * who->preferentialPercent / 100.0));
                if (p.cents() > 0)
                    want.push_back({kPrefRef + std::to_string(l.id), kPrefTender, prefName, l.id, p});
            }
        }
    }
    // Already as wanted: the payments (and their ids) left alone.
    std::vector<std::pair<std::string, std::int64_t>> have, now;
    for (const Payment &p : c.payments)
        if (p.reference.starts_with("custprice:"))
            have.emplace_back(p.reference, p.amount.cents());
    for (const Want &w : want)
        now.emplace_back(w.ref, w.amount.cents());
    if (have == now)
        return;
    std::erase_if(c.payments, [](const Payment &p) { return p.reference.starts_with("custprice:"); });
    Money total;
    for (const Want &w : want) {
        Payment p;
        p.id = c.nextPaymentId++;
        p.tenderId = w.tender;
        p.tenderName = w.name;
        p.kind = TenderKind::Discount;
        p.amount = w.amount;
        p.lineId = w.lineId;
        p.reference = w.ref;
        stampPayment(p);
        c.payments.push_back(p);
        total += w.amount;
    }
    // Logged with the customer (the audit trail and the Exceptions report: discounts).
    if (who && !want.empty())
        noteEvent(c, tr("Customer prices for %1: %2").arg(qs(who->name), format(total)), "discount", total, ss(qs(who->name)));
}

} // namespace vt::app
