#pragma once

#include "core/check.hh"
#include "core/day.hh"
#include "core/report.hh"
#include "core/settings.hh"
#include "print/document.hh"

#include <functional>
#include <string>
#include <vector>

namespace vt::print {

struct TicketContext {
    const core::PosSettings &settings;
    std::function<std::string(std::int64_t)> dateTime;   // epoch ms -> "Sep 29, 2026 10:31 AM"
    std::function<std::string(std::int64_t)> time;       // epoch ms -> "10:31 AM"
    std::int64_t now = 0;
    std::shared_ptr<const Raster> logo;   // receipts: the store logo at the top (null: none)
    // Allergen ids -> their names in the store's language (none: the ids).
    std::function<std::vector<std::string>(const std::vector<std::string> &)> allergenNames;
    // An English phrase in the store's language (none, or not translated: as is).
    std::function<std::string(const std::string &)> words;

    std::string money(Money m) const;
    // A printed phrase in the store's language; %1, %2... filled with `args`.
    std::string tr(const std::string &english, std::initializer_list<std::string> args = {}) const;
    std::vector<std::string> allergens(const std::vector<std::string> &ids) const
    {
        return allergenNames ? allergenNames(ids) : ids;
    }
};

// Customer receipt (legacy SubCheck::PrintReceipt).
Document receipt(const core::Check &check, const TicketContext &ctx);

// A self-order kiosk's slip: the order number, big, to show at the
// counter; the name, what was ordered and the total; `sent`: being made.
Document orderSlip(const core::Check &check, bool sent, const TicketContext &ctx);

// Kitchen / bar ticket for the given lines (legacy Check::PrintWorkOrder).
// `voids` prints the lines as cancelled.
Document kitchenTicket(const core::Check &check, const std::vector<core::OrderLine> &lines,
                       const std::string &station, bool voids, const TicketContext &ctx);

// Any report, e.g. the end-of-day summary.
Document reportTicket(const core::Report &report, const TicketContext &ctx);

// Just open the cash drawer.
Document drawerKick();

// No Sale: a slip like a kitchen ticket (no logo) that opens the drawer too:
// "****No Sale****", No Sale #number, the drawer, who, why and when.
Document noSaleTicket(const core::CashMovement &noSale, int number, const std::string &drawer,
                      const TicketContext &ctx);

// The full privacy notice on the receipt printer, for a customer who asks.
Document privacyNoticeTicket(const TicketContext &ctx);
// A gift receipt: what was bought (names and quantities), no prices or payments.
Document giftReceipt(const core::Check &check, const TicketContext &ctx);
// Store Settings > Receipts > Print a barcode on receipts: the check's reference ("R-42", "RHCA-42") as a
// Code 128 barcode at the bottom (receipt and giftReceipt end with it); off: nothing.
void receiptBarcode(Document &d, const core::Check &check, const TicketContext &ctx);
// A receipt carries the short notice when the check has a customer's details.
bool receiptNeedsPrivacy(const core::Check &check, const core::PosSettings &settings);

} // namespace vt::print
