#pragma once
// Inventory transfers between the stores (Manager > Inventory > Transfers; owner 2026-11-26): retail items
// sent from one store to another, matched by UPC (item ids differ between stores), and the retail price
// book's changes passed on from one store to another. Plain C++ (no Qt): app/pos_transfers.cpp uses it,
// tests/test_transfer_core.cpp checks it. Files and the way they travel: docs/MASS_UPDATES_STORES.md.

#include "core/mass_update.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vt::core::xfer {

// One item of a transfer. Quantities are in the item's own unit as sold (a carton, a pack, a box, a single).
struct Line {
    std::string upc;              // the item's first barcode at the sending store
    std::string name;
    double qty = 0;               // sent
    std::int64_t costCents = 0;   // each, at the sender's system cost (0: none)
    std::int64_t priceCents = 0;  // each, the sender's price (to add an item not on file)
    // A box or carton: the UPC of the single (pack) in it and how many (from the singles' __parent__ link), so a
    // store without the carton's UPC can take it in as packs.
    std::string packUpc;
    int packSize = 0;
    std::string category;         // the sender's category name (to add an item not on file)
    // At the receiving store.
    std::string itemId;           // the item it went to here (empty: not on file / left out)
    double perUnit = 1;           // units of that item per one sent (a carton taken in as 10 packs: 10)
    double received = -1;         // counted in (sent units; -1: not received yet)
    std::string reason;           // short or over: why ("damaged", "miscount", "missing", typed...)
    bool operator==(const Line &) const = default;
};

// Where a transfer is.
//  out: "transit" (sent, or waiting to send: `delivered` says), "received", "differences", "void"
//  in:  "waiting" (here, not counted in yet), "received", "differences", "void" (the sender took it back)
struct Transfer {
    std::int64_t id = 0;          // this store's record number
    std::string key;              // the same at both stores: "<from>-<number>" ("BHDAN-12")
    int number = 0;               // the sender's transfer number (on the slip)
    std::string direction;        // "out" | "in"
    std::string from, to;         // store short names (Store Settings > Other stores)
    std::string who;              // sent by
    std::string note;
    std::int64_t at = 0;          // sent (epoch ms)
    std::string status;
    bool delivered = false;       // out: its file reached the other store (the outbox emptied)
    std::int64_t receivedAt = 0;
    std::string receivedBy;
    std::int64_t voidedAt = 0;
    std::string voidedBy;
    std::vector<Line> lines;
    bool operator==(const Transfer &) const = default;

    std::int64_t sentCost() const
    {
        std::int64_t c = 0;
        for (const Line &l : lines)
            c += std::llround(l.qty * double(l.costCents));
        return c;
    }
    std::int64_t receivedCost() const
    {
        std::int64_t c = 0;
        for (const Line &l : lines)
            c += std::llround((l.received < 0 ? 0 : l.received) * double(l.costCents));
        return c;
    }
    // Lines counted in other than sent (short, over, or left out).
    int differences() const
    {
        int n = 0;
        for (const Line &l : lines)
            if (l.received >= 0 && std::abs(l.received - l.qty) > 1e-9)
                ++n;
        return n;
    }
    bool open() const { return status == "transit" || status == "waiting"; }
};

// What the receiving store has for a line: `at` is a place in its items (-1: none), `perUnit` how many of
// that item one sent unit is (a carton matched to its packs: the pack size).
struct Match {
    std::int64_t at = -1;
    double perUnit = 1;
};

// By UPC (with or without leading zeros, an 11-digit UPC with its check digit), else a box or carton by its
// single's UPC times its pack size. The index is the receiving store's barcodes (mass::UpcIndex).
inline Match match(const mass::UpcIndex &here, const Line &l)
{
    if (const std::int64_t at = here.find(l.upc); at >= 0)
        return {at, 1};
    if (!l.packUpc.empty() && l.packSize > 0)
        if (const std::int64_t at = here.find(l.packUpc); at >= 0)
            return {at, double(l.packSize)};
    return {};
}

// The receiving store's price for a price the other store set: marked up by `percent` (0: the same), then
// rounded (owner 2026-11-26: the bar's "+ x %" and ".49 / .99 up"). Never below 0.
inline std::int64_t markedUp(std::int64_t cents, double percent, mass::Round round)
{
    if (cents < 0)
        return cents;
    const std::int64_t up = percent == 0 ? cents : std::llround(double(cents) * (1.0 + percent / 100.0));
    return mass::roundCents(std::max<std::int64_t>(0, up), round);
}

// A store's short name as the other stores know it: letters, digits, '-' and '_' only, upper case
// ("hca" -> "HCA"); empty if nothing is left.
inline std::string storeKey(std::string_view s)
{
    std::string out;
    for (char c : s) {
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
            out += c;
    }
    return out;
}

} // namespace vt::core::xfer
