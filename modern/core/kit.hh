#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif

// Sampler kits (owner 2026-11-27, after ILSA's KIT table: KIT_UPC, ITEM_UPC, ITEM_QTY, PRINT_KIT_ITEM): an item
// sold at its own price (a 5-cigar sampler, $39.99) whose contents leave stock when it sells. The contents are
// recipe lines on the kit item, "__kit__:<UPC>" with the quantity (UPCs, as a box's "__parent__:", so the same
// kit means the same thing at another store); "__kitprint__" 1: its contents print under it on the receipt.
// The kit itself has no stock of its own. Its cost is the contents' (set when it's saved). Plain C++ (no Qt).

#include "core/menu.hh"

#include <cstdio>
#include <string>
#include <vector>

namespace vt::core::kit {

constexpr const char *kTag = "__kit__:";
constexpr const char *kPrintTag = "__kitprint__";
constexpr int kMostParts = 50;

struct Part {
    std::string upc;
    double qty = 1;
    bool operator==(const Part &) const = default;
};

inline std::vector<Part> partsOf(const MenuItem &m)
{
    std::vector<Part> out;
    for (const RecipeLine &rl : m.recipe)
        if (rl.ingredientId.starts_with(kTag) && rl.quantity > 0)
            out.push_back({rl.ingredientId.substr(std::string_view(kTag).size()), rl.quantity});
    return out;
}

inline bool isKit(const MenuItem &m)
{
    for (const RecipeLine &rl : m.recipe)
        if (rl.ingredientId.starts_with(kTag))
            return true;
    return false;
}

inline bool printsContents(const MenuItem &m) { return recipeValue(m, kPrintTag) > 0.5; }

// Its contents (none: no longer a kit) and whether they print.
inline void setParts(MenuItem &m, const std::vector<Part> &parts, bool print)
{
    std::erase_if(m.recipe, [](const RecipeLine &rl) { return rl.ingredientId.starts_with(kTag) || rl.ingredientId == kPrintTag; });
    for (const Part &p : parts)
        if (!p.upc.empty() && p.qty > 0)
            m.recipe.push_back({kTag + p.upc, p.qty});
    if (print && !parts.empty())
        m.recipe.push_back({kPrintTag, 1.0});
}

// "2 x SAMPLE TORO 6X52" (whole quantities without the decimals).
inline std::string partText(double qty, const std::string &name)
{
    char q[32];
    if (qty == double(static_cast<long long>(qty)))
        std::snprintf(q, sizeof q, "%lld", static_cast<long long>(qty));
    else
        std::snprintf(q, sizeof q, "%.2f", qty);
    return std::string(q) + " x " + name;
}

} // namespace vt::core::kit
