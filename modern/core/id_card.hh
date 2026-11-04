#pragma once
// What the barcode on the back of a US or Canadian driver's license or ID card says (AAMVA PDF417):
// only what an age check needs. A seller may keep no more than the name, date of birth, expiration
// date and ID number from such a scan (Connecticut CGS 53-344(e)); ViewTouch keeps the age passed and
// the ID's last 4 characters.
#include <cctype>
#include <optional>
#include <string>

namespace vt::core {

struct IdDate {
    int year = 0, month = 0, day = 0;
    bool valid() const { return year >= 1900 && year <= 2200 && month >= 1 && month <= 12 && day >= 1 && day <= 31; }
};

struct IdCard {
    IdDate birth;
    IdDate expires;        // year 0: not on the card
    std::string number;    // DAQ
};

// Looks like a license barcode (scanners send it whole, or a keyboard-mode scanner types it).
inline bool looksLikeIdCard(const std::string &raw)
{
    return raw.find("ANSI ") != std::string::npos || raw.find("AAMVA") != std::string::npos
        || (raw.rfind('@', 0) == 0 && raw.find("DBB") != std::string::npos);
}

namespace detail {
// A field's value: "DBB01151990" -> "01151990". Fields start a line (LF, CR or RS), or follow the
// subfile header ("DL" / "ID") on its first line.
inline std::string idField(const std::string &raw, const char *code)
{
    for (std::size_t at = raw.find(code); at != std::string::npos; at = raw.find(code, at + 1)) {
        const char before = at ? raw[at - 1] : '\n';
        const bool lineStart = before == '\n' || before == '\r' || before == '\x1e' || before == '\x1d';
        const bool afterHeader = at >= 2 && ((raw.compare(at - 2, 2, "DL") == 0) || (raw.compare(at - 2, 2, "ID") == 0));
        if (!lineStart && !afterHeader)
            continue;
        std::size_t end = raw.find_first_of("\n\r\x1e\x1d", at + 3);
        std::string v = raw.substr(at + 3, end == std::string::npos ? std::string::npos : end - at - 3);
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back())))
            v.pop_back();
        return v;
    }
    return {};
}
// MMDDCCYY (US) or CCYYMMDD (Canada): told apart by where a plausible year sits.
inline IdDate idDate(const std::string &v)
{
    if (v.size() < 8)
        return {};
    for (std::size_t i = 0; i < 8; ++i)
        if (!std::isdigit(static_cast<unsigned char>(v[i])))
            return {};
    const auto num = [&](std::size_t from, std::size_t n) { return std::stoi(v.substr(from, n)); };
    IdDate us{num(4, 4), num(0, 2), num(2, 2)};
    if (us.valid())
        return us;
    IdDate ca{num(0, 4), num(4, 2), num(6, 2)};
    return ca.valid() ? ca : IdDate{};
}
} // namespace detail

inline std::optional<IdCard> parseIdCard(const std::string &raw)
{
    if (!looksLikeIdCard(raw))
        return std::nullopt;
    IdCard card;
    card.birth = detail::idDate(detail::idField(raw, "DBB"));
    if (!card.birth.valid())
        return std::nullopt;
    card.expires = detail::idDate(detail::idField(raw, "DBA"));
    card.number = detail::idField(raw, "DAQ");
    return card;
}

// Whole years old on a day (today: year, month, day).
inline int ageOn(const IdDate &birth, int year, int month, int day)
{
    int age = year - birth.year;
    if (month < birth.month || (month == birth.month && day < birth.day))
        --age;
    return age;
}

// Expired before that day.
inline bool expiredOn(const IdDate &expires, int year, int month, int day)
{
    if (!expires.valid())
        return false;
    if (expires.year != year)
        return expires.year < year;
    if (expires.month != month)
        return expires.month < month;
    return expires.day < day;
}

} // namespace vt::core
