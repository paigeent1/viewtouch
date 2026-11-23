#pragma once
// Menu Builder's quick index (owner, 2026-11-23: "pressing premium cigars on the menu builder is taking
// forever" - a 21,000-item price book department was sent whole to the screen and filtered there).
// Made once per menu change: each category's items, the tree below a price book department (brand >
// product group > sub group, the Labels screen's levels), names in small letters, item numbers and
// barcodes. Then opening a level, Show Items and the search look only at what they need.
// Plain C++ (no Qt): app/pos_menu.cpp makes it, tests/test_builder_index.cpp checks it.

#include "core/menu.hh"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vt::core {

class BuilderIndex {
public:
    // One level of the tree below a category (the category itself: the empty path).
    struct Node {
        int count = 0;                          // its items (not choices), at it or anywhere below it
        std::map<std::string, int> children;    // the next level down, A-Z: how many items under each
        std::vector<std::uint32_t> items;       // those items' places in the menu, in menu order
    };
    // What a search found: places in the menu, best first (a number or barcode typed exactly, then by
    // name in the categories' order), at most the number asked for; `more`: there were more.
    struct Found {
        std::vector<std::uint32_t> items;
        bool more = false;
    };
    // An item's place below its category (its brand, then its product group and sub group when it has them).
    using PathOf = std::function<std::vector<std::string>(const MenuItem &)>;
    // An item's name in small letters (the app's: Unicode; none given: A-Z only).
    using LowerOf = std::function<std::string(const MenuItem &)>;

    // `treeFamilies`: the categories that get a tree (pathOf is asked only about their items).
    void build(const std::vector<MenuItem> &menu, const std::unordered_set<std::string> &treeFamilies,
               const PathOf &pathOf, const LowerOf &lowerOf = {})
    {
        *this = {};
        lowerName_.reserve(menu.size());
        modifier_.reserve(menu.size());
        familyOfItem_.reserve(menu.size());
        for (std::size_t i = 0; i < menu.size(); ++i) {
            const MenuItem &m = menu[i];
            const auto at = std::uint32_t(i);
            lowerName_.push_back(lowerOf ? lowerOf(m) : asciiLower(m.name));
            modifier_.push_back(m.isModifier ? 1 : 0);
            familyOfItem_.push_back(m.family);
            family_[m.family].push_back(at);
            if (m.isModifier)
                continue;
            ++count_[m.family];
            if (!m.number.empty())
                number_[asciiLower(m.number)].push_back(at);
            for (const std::string &b : m.barcodes)
                if (const std::string k = codeKey(b); !k.empty()) {
                    auto &list = code_[k];
                    if (list.empty() || list.back() != at)   // (two of its codes the same: once)
                        list.push_back(at);
                }
            if (!treeFamilies.contains(m.family))
                continue;
            // The path, up to its first empty level; the item counts at every level on the way down.
            std::vector<std::string> path = pathOf ? pathOf(m) : std::vector<std::string>{};
            for (std::size_t d = 0; d < path.size(); ++d)
                if (path[d].empty()) {
                    path.resize(d);
                    break;
                }
            std::string k = m.family;
            for (std::size_t d = 0;; ++d) {
                Node &n = tree_[k];
                ++n.count;
                n.items.push_back(at);
                if (d == path.size())
                    break;
                ++n.children[path[d]];
                k += kSep;
                k += path[d];
            }
        }
        for (const std::string &f : treeFamilies)
            tree_.try_emplace(f);   // (a category with no items yet: an empty tree)
    }

    std::size_t size() const { return lowerName_.size(); }
    // Every item of a category (choices too), in menu order.
    const std::vector<std::uint32_t> &family(const std::string &id) const
    {
        static const std::vector<std::uint32_t> none;
        const auto it = family_.find(id);
        return it == family_.end() ? none : it->second;
    }
    // How many items (not choices) a category has.
    int count(const std::string &family) const
    {
        const auto it = count_.find(family);
        return it == count_.end() ? 0 : it->second;
    }
    bool hasTree(const std::string &family) const { return tree_.contains(family); }
    // The level at `path` below the category, or nullptr (no such level).
    const Node *node(const std::string &family, const std::vector<std::string> &path) const
    {
        const auto it = tree_.find(key(family, path, path.size()));
        return it == tree_.end() ? nullptr : &it->second;
    }
    // How much of `path` is there: its longest start that is a level (0: just the category).
    std::size_t reach(const std::string &family, const std::vector<std::string> &path) const
    {
        std::size_t d = 0;
        while (d < path.size() && tree_.contains(key(family, path, d + 1)))
            ++d;
        return d;
    }

    // Find items in these categories only: `lowerText` (trimmed, in small letters) is an item number or a
    // barcode (exactly: a UPC with or without its leading zeros, or with no check digit), or part of a name.
    // Eight digits or more is a barcode: looked up, never searched for in the names.
    Found search(std::string_view lowerText, const std::vector<std::string> &families, std::size_t limit) const
    {
        Found out;
        if (lowerText.empty() || limit == 0)
            return out;
        const std::unordered_set<std::string> in(families.begin(), families.end());
        std::unordered_set<std::uint32_t> taken;
        const auto take = [&](std::uint32_t i) {
            if (taken.contains(i))
                return true;
            if (out.items.size() >= limit) {
                out.more = true;
                return false;
            }
            taken.insert(i);
            out.items.push_back(i);
            return true;
        };
        const auto exact = [&](const std::unordered_map<std::string, std::vector<std::uint32_t>> &map, const std::string &k) {
            if (const auto it = map.find(k); it != map.end())
                for (std::uint32_t i : it->second)
                    if (in.contains(familyOf(i)))
                        take(i);
        };
        const std::string text(lowerText);
        const bool digits = std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
        exact(number_, text);
        if (text.size() >= 4) {
            exact(code_, codeKey(text));
            if (digits && text.size() == 11)   // a UPC-A without its check digit
                exact(code_, codeKey(text + checkDigit(text)));
        }
        if (digits && text.size() >= 8)
            return out;
        for (const std::string &f : families) {
            const auto it = family_.find(f);
            if (it == family_.end())
                continue;
            for (std::uint32_t i : it->second)
                if (!modifier_[i] && lowerName_[i].find(text) != std::string::npos && !take(i))
                    return out;
        }
        return out;
    }

    // A barcode as the index keeps it: digits without their leading zeros (a UPC-A and the same code read
    // as an EAN-13 are one), anything else in small letters.
    static std::string codeKey(std::string_view code)
    {
        if (code.empty())
            return {};
        if (!std::ranges::all_of(code, [](char c) { return c >= '0' && c <= '9'; }))
            return asciiLower(code);
        std::size_t i = 0;
        while (i + 1 < code.size() && code[i] == '0')
            ++i;
        return std::string(code.substr(i));
    }
    static std::string asciiLower(std::string_view s)
    {
        std::string out(s);
        for (char &c : out)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }

private:
    static constexpr char kSep = '\x1f';
    static std::string key(const std::string &family, const std::vector<std::string> &path, std::size_t depth)
    {
        std::string k = family;
        for (std::size_t d = 0; d < depth && d < path.size(); ++d) {
            k += kSep;
            k += path[d];
        }
        return k;
    }
    // The GS1 check digit of an 11-digit UPC-A.
    static char checkDigit(const std::string &eleven)
    {
        int sum = 0;
        for (int i = 0; i < 11; ++i)
            sum += (eleven[std::size_t(10 - i)] - '0') * (i % 2 == 0 ? 3 : 1);
        return char('0' + (10 - sum % 10) % 10);
    }
    const std::string &familyOf(std::uint32_t i) const { return familyOfItem_.at(i); }

    std::vector<std::string> lowerName_;
    std::vector<char> modifier_;
    std::vector<std::string> familyOfItem_;
    std::unordered_map<std::string, std::vector<std::uint32_t>> family_;
    std::unordered_map<std::string, int> count_;
    std::unordered_map<std::string, std::vector<std::uint32_t>> number_, code_;
    std::unordered_map<std::string, Node> tree_;
};

} // namespace vt::core
