// Responsible service: counting standard drinks for each guest (core/over_service.hh).

#include "core/over_service.hh"

#include <algorithm>
#include <cstdio>

namespace vt::core {

std::string toString(ServiceLevel l)
{
    switch (l) {
    case ServiceLevel::Ok: return "ok";
    case ServiceLevel::Caution: return "caution";
    case ServiceLevel::Stop: return "stop";
    }
    return "ok";
}

ServiceLevel levelOf(double drinks, const ServicePolicy &p)
{
    // A hair under the level (4.0 made of 0.83s and 1.17s) still reaches it.
    constexpr double eps = 1e-6;
    if (p.stop > 0 && drinks + eps >= p.stop)
        return ServiceLevel::Stop;
    if (p.caution > 0 && drinks + eps >= p.caution)
        return ServiceLevel::Caution;
    return ServiceLevel::Ok;
}

std::string idGuestKey(const std::string &dob, const std::string &id4)
{
    // FNV-1a, 64 bits: the same everywhere and every run.
    std::uint64_t h = 1469598103934665603ULL;
    for (const char ch : dob + "|" + id4) {
        h ^= static_cast<unsigned char>(ch);
        h *= 1099511628211ULL;
    }
    char out[24];
    std::snprintf(out, sizeof out, "id:%016llx", static_cast<unsigned long long>(h));
    return out;
}

namespace {
std::string last4(const std::string &s) { return s.size() > 4 ? s.substr(s.size() - 4) : s; }

// The customer linked to the whole check, if any.
DrinkGuest checkCustomer(const Check &c)
{
    if (!c.customerId.empty())
        return {"c:" + c.customerId, "customer", c.customer.name, 0, c.id};
    if (!c.subscriptionCard.empty())
        return {"s:" + c.subscriptionCard, "subscription", last4(c.subscriptionCard), 0, c.id};
    if (c.ageVerified > 0 && !c.ageId4.empty() && !c.ageDob.empty())
        return {idGuestKey(c.ageDob, c.ageId4), "id", last4(c.ageId4), 0, c.id};
    return {};
}
} // namespace

DrinkGuest drinkGuest(const Check &c, int seat)
{
    if (seat > 0)
        if (const auto it = c.seatGuests.find(seat); it != c.seatGuests.end() && !it->second.key.empty())
            return {it->second.key, "id", it->second.name, seat, c.id};
    if (seat == 0 || c.guests <= 1) {
        DrinkGuest g = checkCustomer(c);
        if (!g.key.empty()) {
            g.seat = seat;
            return g;
        }
    }
    return {"seat:" + std::to_string(c.id) + ":" + std::to_string(seat), "seat", {}, seat, c.id};
}

double lineDrinks(const OrderLine &l)
{
    if (l.voided || l.isComment() || l.stdDrinks <= 0 || l.quantity <= 0)
        return 0;
    const double whole = l.stdDrinks * l.quantity;
    return l.isShare() ? whole / l.shareOf : whole;
}

std::vector<std::pair<DrinkGuest, double>> lineGuests(const Check &c, const OrderLine &l)
{
    const double d = lineDrinks(l);
    if (d <= 0)
        return {};
    std::vector<int> seats = l.drinkSeats;
    std::ranges::sort(seats);
    seats.erase(std::ranges::unique(seats).begin(), seats.end());
    if (seats.empty())
        seats.push_back(l.seat);
    std::vector<std::pair<DrinkGuest, double>> out;
    for (const int s : seats)
        out.emplace_back(drinkGuest(c, s), d / double(seats.size()));
    return out;
}

std::map<std::string, std::vector<DrinkRing>> drinkRings(const std::vector<const Check *> &checks, std::int64_t since)
{
    std::map<std::string, std::vector<DrinkRing>> out;
    for (const Check *c : checks) {
        if (!c || c->training || c->status == CheckStatus::Discarded || c->status == CheckStatus::Merged)
            continue;
        for (const OrderLine &l : c->lines) {
            const std::int64_t at = l.addedAt ? l.addedAt : c->openedAt;
            if (at < since)
                continue;
            for (const auto &[g, d] : lineGuests(*c, l))
                out[g.key].push_back({at, d, l.addedById, l.addedByName, c->id, l.id});
        }
    }
    for (auto &[k, rings] : out)
        std::ranges::sort(rings, {}, &DrinkRing::at);
    return out;
}

std::int64_t windowStart(const ServicePolicy &p, std::int64_t now)
{
    return now - std::int64_t(std::max(0.0, p.windowHours) * 3'600'000.0);
}

double drinksNow(const std::vector<DrinkRing> &rings, const ServicePolicy &p, std::int64_t now)
{
    const std::int64_t from = windowStart(p, now);
    double sum = 0;
    std::int64_t first = 0;
    for (const DrinkRing &r : rings) {
        if (r.at < from || r.at > now || r.drinks <= 0)
            continue;
        sum += r.drinks;
        if (first == 0 || r.at < first)
            first = r.at;
    }
    if (p.subtract && first > 0 && p.perHour > 0)
        sum -= p.perHour * double(now - first) / 3'600'000.0;
    return std::max(0.0, sum);
}

} // namespace vt::core
