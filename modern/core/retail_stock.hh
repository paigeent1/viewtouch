#pragma once
// Retail on hand for a box / carton and its single (owner 2026-11-27): one family, two counts. Sealed: the
// cartons or boxes still closed (kept on the box's or carton's own item). Loose: the packs or cigars out on the
// shelf or in the humidor (kept on the single's item). Cigars often have more than a box loose (several boxes
// opened into the humidor: "4 box 27 ea"); cigarettes are opened one carton at a time. The family is the single's
// Menu Builder link (__parent__:<box UPC> with its pack size). Plain C++ (no Qt): app/pos_retail_stock.cpp uses it,
// tests/test_carton_stock.cpp checks it.
//
//  A single sold: one loose. No loose left: one sealed opened by itself (sealed -1, loose + pack size - 1).
//  A box / carton sold: one sealed. None sealed but a box's worth loose: that many loose. Else sealed below 0.
//  Received, transferred in, returned, voided: back as what it is (a carton sealed, a pack loose).
//  Open boxes: sealed -n, loose + n x pack size (stocking the humidor).

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace vt::core::stock {

struct Counts {
    double sealed = 0;
    double loose = 0;
    bool operator==(const Counts &) const = default;
};

// What a take did, besides the new counts: the sealed opened by themselves, the boxes taken out of loose,
// the boxes sold with none sealed nor loose enough (sealed below 0).
struct Take {
    Counts after;
    int opened = 0;
    int fromLoose = 0;
    int short_ = 0;
};

inline double tidy(double v) { return std::round(v * 1000) / 1000; }

// `n` singles out (sold, sent). n < 0: back in (returned, voided), as loose.
inline Take takeSingles(Counts c, double n, int packSize)
{
    Take t;
    if (n <= 0 || packSize < 1) {
        c.loose = tidy(c.loose - n);
        t.after = c;
        return t;
    }
    const double fromShelf = std::min(n, std::max(0.0, c.loose));
    const double rest = n - fromShelf;
    if (rest > 1e-9 && c.sealed >= 1) {
        const int need = int(std::ceil(rest / packSize - 1e-9));
        t.opened = std::min(need, int(std::floor(c.sealed + 1e-9)));
    }
    c.sealed = tidy(c.sealed - t.opened);
    c.loose = tidy(c.loose - n + double(t.opened) * packSize);
    t.after = c;
    return t;
}

// `n` boxes / cartons out. n < 0: back in, as sealed.
inline Take takeParents(Counts c, double n, int packSize)
{
    Take t;
    if (n <= 0 || packSize < 1) {
        c.sealed = tidy(c.sealed - n);
        t.after = c;
        return t;
    }
    double left = n;
    while (left > 1e-9) {
        const double one = std::min(1.0, left);
        if (c.sealed >= one - 1e-9) {
            c.sealed -= one;
        } else if (c.sealed <= 1e-9 && c.loose >= one * packSize - 1e-9) {
            c.loose -= one * packSize;
            ++t.fromLoose;
        } else {
            c.sealed -= one;
            ++t.short_;
        }
        left -= one;
    }
    c.sealed = tidy(c.sealed);
    c.loose = tidy(c.loose);
    t.after = c;
    return t;
}

// Open `n` sealed into loose (the humidor). False: fewer than n sealed.
inline bool openParents(Counts &c, int n, int packSize)
{
    if (n < 1 || packSize < 1 || c.sealed + 1e-9 < n)
        return false;
    c.sealed = tidy(c.sealed - n);
    c.loose = tidy(c.loose + double(n) * packSize);
    return true;
}

// Once, for counts kept before the family had two counts (receiving put a carton in as 10 loose packs; a carton
// sold took the carton below 0): a carton / box below 0 made up from whole boxes loose, then, for cigarettes
// (opened one carton at a time: `wholeToSealed`), whole cartons loose back to sealed.
inline Counts settle(Counts c, int packSize, bool wholeToSealed)
{
    if (packSize < 1)
        return c;
    while (c.sealed < -1e-9 && c.loose >= packSize - 1e-9) {
        c.sealed += 1;
        c.loose -= packSize;
    }
    if (wholeToSealed && c.loose >= packSize - 1e-9) {
        const double whole = std::floor((c.loose + 1e-9) / packSize);
        c.sealed += whole;
        c.loose -= whole * packSize;
    }
    c.sealed = tidy(c.sealed);
    c.loose = tidy(c.loose);
    return c;
}

inline std::string number(double v)
{
    v = tidy(v);
    if (std::abs(v - std::round(v)) < 1e-9)
        return std::to_string(std::llround(v));
    std::string s = std::to_string(v);
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    return s;
}

// "4 box 27 ea", "10 ctn", "9 ctn 9 pk", "5 pk", "0 ctn", "-2 ctn 3 pk".
inline std::string text(Counts c, const std::string &parentUnit, const std::string &singleUnit)
{
    const bool s = std::abs(c.sealed) > 1e-9, l = std::abs(c.loose) > 1e-9;
    if (!s && !l)
        return "0 " + parentUnit;
    std::string out;
    if (s)
        out = number(c.sealed) + " " + parentUnit;
    if (l)
        out += (out.empty() ? "" : " ") + number(c.loose) + " " + singleUnit;
    return out;
}

// What the family is worth at cost: sealed at the box's cost, loose at the single's.
inline double value(Counts c, double parentCost, double singleCost)
{
    return std::max(0.0, c.sealed) * parentCost + std::max(0.0, c.loose) * singleCost;
}

// The units' short names when none are set: a box of cigars ("box" / "ea"), else a carton ("ctn" / "pk").
inline bool looksLikeBox(const std::string &parentName, const std::string &category)
{
    auto has = [](std::string s, const char *w) {
        std::ranges::transform(s, s.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        return s.find(w) != std::string::npos;
    };
    // ("MARLBORO RED BOX" is a hard pack: a carton's name or a cigarette category says carton first.)
    if (has(parentName, "carton") || has(parentName, "ctn") || has(category, "cigarette") || has(parentName, "cigarette"))
        return false;
    return has(category, "cigar") || has(parentName, "cigar") || has(parentName, "box");
}

} // namespace vt::core::stock
