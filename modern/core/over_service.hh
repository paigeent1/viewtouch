#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif

// Responsible service: standard drinks counted for each guest, and the caution / stop levels a store
// sets (Manager -> Store Settings -> Responsible service). Pure C++ (no Qt): the counting rules only;
// app/pos_over_service.cpp asks the bartender, the manager, and writes the audit file.

#include "core/check.hh"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vt::core {

// The store's levels, in US standard drinks within a time window. Off by default.
struct ServicePolicy {
    bool on = false;
    double caution = 4.0;        // yellow: the bartender acknowledges
    double stop = 6.0;           // flashing red: a manager's PIN, or not rung in
    double windowHours = 3.0;    // drinks rung in within this many hours count
    bool subtract = true;        // less about one drink an hour since the first one in the window
    double perHour = 1.0;        // ...this many an hour
    std::vector<std::string> overrideReasons;   // a manager's reasons to pick from (empty: the built-in list)

    bool operator==(const ServicePolicy &) const = default;
};

enum class ServiceLevel { Ok, Caution, Stop };
std::string toString(ServiceLevel l);
ServiceLevel levelOf(double drinks, const ServicePolicy &p);

// Who drinks are counted against: a customer linked to the check (a saved customer, a subscription
// card, an ID scanned) or to the seat (an ID scanned for that seat), else the seat of that check.
struct DrinkGuest {
    std::string key;      // "c:<customer id>", "s:<card>", "id:<hash>", "seat:<check id>:<seat>"
    std::string kind;     // customer | subscription | id | seat
    std::string name;     // the customer's name, the card's or ID's last 4 (never the whole number)
    int seat = 0;
    std::int64_t checkId = 0;
    bool customer() const { return kind != "seat"; }
    bool operator==(const DrinkGuest &) const = default;
};

// A short, stable key for an ID scanned (its date of birth and last characters): the same person
// on another check counts together, and neither is written anywhere.
std::string idGuestKey(const std::string &dob, const std::string &id4);

// The guest a check's seat counts against. Seat 0 (no seat), or a check for one guest: the check's
// customer when one is linked. Another seat: its own ID scanned, else the seat.
DrinkGuest drinkGuest(const Check &c, int seat);

// The standard drinks a line counts (its quantity; a piece of a shared item: its part); 0 voided.
double lineDrinks(const OrderLine &l);
// Each guest's part of a line: the line's seat, or shared evenly across drinkSeats.
std::vector<std::pair<DrinkGuest, double>> lineGuests(const Check &c, const OrderLine &l);

// One ring of drinks for a guest: when, how many (their part), who rang it, where.
struct DrinkRing {
    std::int64_t at = 0;
    double drinks = 0;
    std::string byId;
    std::string byName;
    std::int64_t checkId = 0;
    std::int64_t lineId = 0;
};

// Every guest's rings on these checks rung in at or after `since` (practice, discarded and merged
// checks, and voided lines leave nothing).
std::map<std::string, std::vector<DrinkRing>> drinkRings(const std::vector<const Check *> &checks, std::int64_t since);

// A guest's count now: their drinks within the window, less perHour for each hour since the first
// of them (when subtract is on), never below 0.
double drinksNow(const std::vector<DrinkRing> &rings, const ServicePolicy &p, std::int64_t now);

// The start of the window at `now` (epoch ms).
std::int64_t windowStart(const ServicePolicy &p, std::int64_t now);

} // namespace vt::core
