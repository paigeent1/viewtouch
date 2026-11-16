#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif

// Tobacco scan data: the weekly pipe-delimited file a store sends to Altria (AGDC Digital Trade
// Program). One record per item line of each sale (identical lines of one sale merged), 45 fields,
// after a summary line. Plain C++ (no Qt): the app reads the week's checks, this builds the file and
// checks it. See modern/docs/SCAN_DATA.md for what each setting is and the weekly routine.
//
// How the store's data is reported (the owner's decisions are in the docs):
// - Per-pack manufacturer buydowns are already in the item's price: only the lowered price shows.
// - A discount named in Settings::multiNames is a manufacturer multi-unit deal (fields 20-22): its whole
//   amount is on every line it covers and each line's final price carries its share.
// - couponNames: a manufacturer coupon (25/26); otherMfrNames: another manufacturer discount (27/28);
//   every other discount (manual discounts, the store's promotions, rewards, staff meals) is the
//   store's own (23/24).
// - Final sales price (31) is before sales tax; returns are negative in 18 and 31.
// - Age check: a scanned ID is "Scanned ID", a typed birth date "Manual Entry". Never a date or ID data.

#include "core/check.hh"
#include "core/menu.hh"
#include "core/settings.hh"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vt::core::scandata {

inline constexpr int kFields = 45;

// Makers whose products are always reported (the Altria companies).
bool isAltriaMaker(const std::string &maker);
// "pmusa" -> "PM USA"; "other" (or empty): the item's own manufacturer text, else "Other".
std::string makerName(const std::string &maker, const std::string &itemManufacturer = {});
// "cig" -> "CIG"...; empty for none.
std::string categoryCode(const std::string &category);
const std::vector<std::string> &makers();       // the codes, Altria ones first, then "other"
const std::vector<std::string> &categories();   // product types, "cbd" and "none" last
// Product types another maker's items may be sent for (each a store setting).
const std::vector<std::string> &optionalCategories();

// A UPC as 12 digits: digits only, an 11-digit one gets its lost leading 0, a 13-digit EAN or 14-digit
// GTIN of a UPC loses its leading 0s, an 8-digit UPC-E is expanded. Anything else: its digits as they are.
std::string normalUpc(const std::string &code);

// Text for a field: no "|", no line breaks or tabs, ASCII only unless `utf8`, spaces collapsed, at
// most `max` characters.
std::string cleanText(const std::string &text, std::size_t max, bool utf8 = false);

// The store's scan data settings (kept per store, on that store's computer only: never copied to
// another Pi).
struct Settings {
    std::string mode = "independent";   // "independent": Retail Control Number; "chain": Management Account + store number
    std::string accountNumber;          // field 1: 6-digit Retail Control Number, or 5-digit Management Account Number
    std::string storeNumber;            // field 6: chain only (blank for an independent)
    std::string accountName;            // the file name's start: letters and digits, kept the same every week
    std::string storeName;              // fields 7-11 exactly as Altria has them on file
    std::string street;
    std::string city;
    std::string state;
    std::string zip4;                   // 12345-6789
    std::string phone;                  // optional 32-34
    std::string contact;
    std::string email;
    std::string provider;               // the summary line's 4th value (empty: the store name)
    std::vector<std::string> optional;  // other makers' product types sent too (optionalCategories())
    std::string encoding = "ascii";     // ascii | utf8 (no BOM)
    std::string lineEnding = "crlf";    // crlf | lf
    // Discounts (tender names, promotion names or codes, one per entry, any case) the manufacturer pays for.
    std::vector<std::string> couponNames;
    std::vector<std::string> multiNames;
    std::vector<std::string> otherMfrNames;
    // Fill in makers: the rules (parseRules); empty: defaultRulesText().
    std::string fillRules;

    bool operator==(const Settings &) const = default;
};

// What's wrong with the settings (empty: ready). Unset values, the spec's sample values and malformed
// ones are all refused.
std::vector<std::string> settingsProblems(const Settings &s);

// How an item is reported, worked out from its fields and its pack/carton links.
struct ItemInfo {
    std::string maker, category;   // codes
    std::string uom;               // "Pack", "Carton"...
    int units = 1;                 // consumer units in one
};
class ItemIndex {
public:
    explicit ItemIndex(const std::vector<MenuItem> &menu);
    const MenuItem *byId(const std::string &id) const;
    ItemInfo info(const MenuItem &m) const;
private:
    const std::vector<MenuItem> &menu_;
    std::map<std::string, std::size_t> byId_;
    std::map<std::string, int> packsIn_;   // a parent's UPC (12 digits) -> the units in it
};

// Filling makers in bulk: "028200 pmusa cig" (a UPC that starts with these digits) or
// "dept:CIGARETTES other cig" (a department: its id, name or group). The first rule that fits wins:
// UPC rules before department rules.
struct FillRule {
    bool dept = false;
    std::string match, maker, category;
};
std::vector<FillRule> parseRules(const std::string &text);
const std::string &defaultRulesText();
// Fills an item's empty maker / product type from the rules (`overwrite`: all of them). True: changed.
bool fillItem(MenuItem &m, const std::vector<FillRule> &rules, const std::vector<MenuCategory> &departments,
              bool overwrite = false);

// Week math (civil dates, no time zone): days since 1970-01-01 and back.
std::int64_t daysFromCivil(int y, int m, int d);
void civilFromDays(std::int64_t z, int &y, int &m, int &d);
// "20260103" -> its parts; false if it isn't a date.
bool parseYmd(const std::string &ymd, int &y, int &m, int &d);
std::string ymdText(int y, int m, int d);
// The Saturday that ends the last whole week (Sunday-Saturday) before `today` (YYYYMMDD).
std::string lastSaturday(const std::string &today);
bool isSaturday(const std::string &ymd);
// The YYYYMMDD `days` after (or before) a date.
std::string addDays(const std::string &ymd, int days);

struct Options {
    std::int64_t from = 0, to = 0;      // the week, epoch ms: Sunday 00:00 to the next Sunday 00:00 (Eastern)
    std::string weekEnding;             // YYYYMMDD, the Saturday
    // epoch ms -> "YYYYMMDD" and "HH:MM:SS" in the store's time (Eastern).
    std::function<std::string(std::int64_t)> date, time;
};

struct Issue {
    bool error = true;      // false: a warning (saving is allowed)
    std::string text;
    std::int64_t checkId = 0;
};

struct Record {
    std::array<std::string, kFields> f;
    std::int64_t checkId = 0;
    int quantity = 0;
    Money finalPrice;
};

struct Result {
    std::vector<Record> records;
    std::vector<Issue> issues;
    int quantity = 0;
    Money total;
    int checks = 0;          // sales with something reported
    std::string fileName;    // <account name><Saturday>
    std::string summary;     // the first line
    bool hasErrors() const;
    std::string line(const Record &r) const;
    // The whole file: summary first, each line ended as the settings say.
    std::string text(const Settings &s) const;
};

// The file for one week. `checks`: the week's closed checks (others are left out by status and time).
Result build(const std::vector<Check> &checks, const std::vector<MenuItem> &menu,
             const std::vector<PosSettings::Promotion> &promotions, const TaxRates &rates, const Settings &settings, const Options &options);

// Checks a finished file the way the Help Desk would (field count, money format, one time and date per
// transaction, summary totals). Used by build(); open for tests.
std::vector<Issue> checkFile(const Result &r);

} // namespace vt::core::scandata
