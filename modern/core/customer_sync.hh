#pragma once
// TEMPORARY: remove when the corporate customer list exists ("One customer across all stores" in
// vt-modern-development-notes.md). Until then the stores share their customers store to store (Store Settings >
// Other stores > Share customers; owner 2026-11-26): each change to a customer's own details goes to the other
// stores as a `viewtouch-customers` file, through the transfers outbox and inbox (app/pos_customer_sync.cpp).
// This header is the plain C++ part (no Qt): which fields go, when one is newer, the phone key, and whether two
// names are clearly different people. tests/test_customer_sync.cpp checks it. Design: docs/MASS_UPDATES_STORES.md.
//
// Shared: name, phone, email, address, note, the "<store> price" switch and the preferential percent. Never shared
// (each store's own): the house account (balance, limit, ledger), points, visits, spent, the last order, the
// privacy notice acceptance.

#include "core/customer.hh"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace vt::core::csync {

// The fields that go to the other stores, in this order.
inline const std::vector<std::string> &fieldNames()
{
    static const std::vector<std::string> names{"name", "phone", "email", "address", "note", "sourcePrice",
                                                "preferentialPercent"};
    return names;
}

// One field as this store knows it: its value (text; "" also for "off" and 0 %), when it was last changed (epoch
// ms, at the store that changed it; 0: never set) and that store's short name.
struct Field {
    std::string value;
    std::int64_t at = 0;
    std::string from;
    bool operator==(const Field &) const = default;
};
using Fields = std::map<std::string, Field>;

inline std::string valueOf(const CustomerRecord &c, const std::string &field)
{
    if (field == "name")
        return c.name;
    if (field == "phone")
        return c.phone;
    if (field == "email")
        return c.email;
    if (field == "address")
        return c.address;
    if (field == "note")
        return c.note;
    if (field == "sourcePrice")
        return c.sourcePrice ? "1" : "";
    if (field == "preferentialPercent") {
        if (c.preferentialPercent == 0)
            return {};
        char b[32];
        std::snprintf(b, sizeof b, "%.6g", c.preferentialPercent);
        return b;
    }
    return {};
}

inline void setValue(CustomerRecord &c, const std::string &field, const std::string &v)
{
    if (field == "name")
        c.name = v;
    else if (field == "phone")
        c.phone = v;
    else if (field == "email")
        c.email = v;
    else if (field == "address")
        c.address = v;
    else if (field == "note")
        c.note = v;
    else if (field == "sourcePrice")
        c.sourcePrice = v == "1";
    else if (field == "preferentialPercent") {
        const double p = v.empty() ? 0.0 : std::strtod(v.c_str(), nullptr);
        c.preferentialPercent = p >= 0 && p <= 100 ? p : 0.0;
    }
}

// The match key: the phone's digits (a leading US 1 dropped); "" when there are fewer than 7 (such a customer
// isn't shared: the stores could only guess who it is).
inline std::string phoneKey(std::string_view phone)
{
    std::string d = CustomerRecord::digits(std::string(phone));
    if (d.size() == 11 && d.front() == '1')
        d.erase(0, 1);
    return d.size() >= 7 ? d : std::string();
}

// `in` is the newer change: later, or at the same moment from the store whose name sorts last (so every store
// picks the same one).
inline bool newer(const Field &in, const Field &have)
{
    if (in.at != have.at)
        return in.at > have.at;
    return in.from > have.from;
}

// The record as just saved at this store against the fields as known: each field that differs is a change made
// here now (never older than what it replaces). Returns the fields that changed.
inline std::vector<std::string> noteLocal(Fields &have, const CustomerRecord &c, std::int64_t at, const std::string &store)
{
    std::vector<std::string> changed;
    for (const std::string &n : fieldNames()) {
        const std::string v = valueOf(c, n);
        Field &h = have[n];
        if (h.value == v)
            continue;
        h = {v, std::max(at, h.at + 1), store};
        changed.push_back(n);
    }
    return changed;
}

// Another store's fields into these: the newer of each wins. A name or a phone is never emptied this way, and the
// same phone number written another way ("203-555-0101", "(203) 555 0101") isn't a change.
// Returns the fields whose value changed here.
inline std::vector<std::string> merge(Fields &have, const Fields &in)
{
    std::vector<std::string> taken;
    for (const std::string &n : fieldNames()) {
        const auto it = in.find(n);
        if (it == in.end())
            continue;
        const Field &f = it->second;
        if ((n == "name" || n == "phone") && f.value.empty())
            continue;
        Field &h = have[n];
        if (!newer(f, h))
            continue;
        if (n == "phone" && !phoneKey(h.value).empty() && phoneKey(f.value) == phoneKey(h.value)) {
            h.at = f.at;   // the same number written another way: this store's way stays
            h.from = f.from;
            continue;
        }
        if (f.value != h.value)
            taken.push_back(n);
        h = f;
    }
    return taken;
}

// A name's words, lower case, letters and digits only.
inline std::vector<std::string> nameWords(std::string_view s)
{
    std::vector<std::string> out;
    std::string w;
    for (char ch : s) {
        if (ch >= 'A' && ch <= 'Z')
            ch = char(ch - 'A' + 'a');
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || (static_cast<unsigned char>(ch) >= 0x80)) {
            w += ch;
        } else if (!w.empty()) {
            out.push_back(w);
            w.clear();
        }
    }
    if (!w.empty())
        out.push_back(w);
    return out;
}

// Could be the same person: either name empty, a word in common ("John Smith" / "J. Smith"), or one first name
// the start of the other ("Rob" / "Robert"). Otherwise clearly different people ("John Smith" / "Mary Jones"):
// staff decide (Manager > Customers > Check these customers).
inline bool sameName(std::string_view a, std::string_view b)
{
    const std::vector<std::string> wa = nameWords(a), wb = nameWords(b);
    if (wa.empty() || wb.empty())
        return true;
    for (const std::string &x : wa)
        for (const std::string &y : wb)
            if (x.size() >= 2 && x == y)
                return true;
    const std::string &fa = wa.front(), &fb = wb.front();
    const std::size_t n = std::min(fa.size(), fb.size());
    return n >= 3 && fa.compare(0, n, fb, 0, n) == 0;
}

} // namespace vt::core::csync
