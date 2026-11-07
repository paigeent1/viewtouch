#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vt::core {

// The barcode at the bottom of a receipt (Store Settings > Receipts > Print a barcode on receipts):
// "R", the store's check prefix (Store Settings > The store; empty for none), "-", the check number:
// "R-5012", "RHCA-5012". It has letters and a dash, so it can never be an item's UPC (digits only);
// scanned at the register it opens that check on Find a check (for a return).

// A check prefix as kept and printed: letters and digits only, in capitals, at most 8 ("hca " -> "HCA").
inline std::string normalCheckPrefix(std::string_view typed)
{
    std::string out;
    for (const char ch : typed) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) && c < 0x80 && out.size() < 8)
            out += char(std::toupper(c));
    }
    return out;
}

inline std::string receiptReference(std::string_view prefix, std::int64_t checkId)
{
    return "R" + normalCheckPrefix(prefix) + "-" + std::to_string(checkId);
}

struct ReceiptReference {
    std::string prefix;     // "" for a store without one
    std::int64_t checkId = 0;
};

// A scanned or typed code that is a receipt's reference (either case, spaces around it ignored);
// anything else (an item's UPC, a coupon, a name): none.
inline std::optional<ReceiptReference> parseReceiptReference(std::string_view code)
{
    while (!code.empty() && std::isspace(static_cast<unsigned char>(code.front())))
        code.remove_prefix(1);
    while (!code.empty() && std::isspace(static_cast<unsigned char>(code.back())))
        code.remove_suffix(1);
    if (code.size() < 3 || (code.front() != 'R' && code.front() != 'r'))
        return std::nullopt;
    const std::size_t dash = code.find('-');
    if (dash == std::string_view::npos || dash > 9)
        return std::nullopt;
    ReceiptReference out;
    for (std::size_t i = 1; i < dash; ++i) {
        const auto c = static_cast<unsigned char>(code[i]);
        if (!std::isalnum(c) || c >= 0x80)
            return std::nullopt;
        out.prefix += char(std::toupper(c));
    }
    const std::string_view number = code.substr(dash + 1);
    if (number.empty() || number.size() > 12)
        return std::nullopt;
    for (const char ch : number) {
        if (ch < '0' || ch > '9')
            return std::nullopt;
        out.checkId = out.checkId * 10 + (ch - '0');
    }
    if (out.checkId <= 0)
        return std::nullopt;
    return out;
}

} // namespace vt::core
