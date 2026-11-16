// Tobacco scan data: see scan_data.hh and modern/docs/SCAN_DATA.md.

#include "core/scan_data.hh"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace vt::core::scandata {

namespace {

std::string lower(std::string s)
{
    for (char &c : s)
        c = char(std::tolower((unsigned char)c));
    return s;
}

std::string upper(std::string s)
{
    for (char &c : s)
        c = char(std::toupper((unsigned char)c));
    return s;
}

std::string trim(const std::string &s)
{
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::string digitsOf(const std::string &s)
{
    std::string d;
    for (char c : s)
        if (c >= '0' && c <= '9')
            d += c;
    return d;
}

bool allDigits(const std::string &s)
{
    return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

// The Altria companies' UPC company prefixes the store's items are known to carry (the default fill
// rules use the same ones); an item with one of these must have an Altria maker.
const std::vector<std::pair<std::string, std::string>> &altriaPrefixes()
{
    static const std::vector<std::pair<std::string, std::string>> p{
        {"028200", "pmusa"}, {"073100", "usstc"}, {"070137", "jmc"}, {"855022", "helix"}};
    return p;
}

std::string two(int v)
{
    return (v < 10 ? "0" : "") + std::to_string(v);
}

bool moneyText(const std::string &s, bool mayBeNegative)
{
    std::size_t i = 0;
    if (!s.empty() && s[0] == '-') {
        if (!mayBeNegative)
            return false;
        i = 1;
    }
    const auto dot = s.find('.');
    if (dot == std::string::npos || dot == i || s.size() - dot != 3)
        return false;
    for (std::size_t k = i; k < s.size(); ++k)
        if (k != dot && !(s[k] >= '0' && s[k] <= '9'))
            return false;
    return true;
}

bool sameName(const std::vector<std::string> &list, const std::vector<std::string> &names)
{
    for (const std::string &want : list) {
        const std::string w = lower(trim(want));
        if (w.empty())
            continue;
        for (const std::string &n : names)
            if (!n.empty() && lower(trim(n)) == w)
                return true;
    }
    return false;
}

// Shares of `amount` in proportion to `weights` (largest remainders get the leftover cents, the
// earlier line first on a tie): they always add up to `amount`.
std::vector<std::int64_t> allocate(std::int64_t amount, const std::vector<std::int64_t> &weights)
{
    std::vector<std::int64_t> out(weights.size(), 0);
    std::int64_t sum = 0;
    for (std::int64_t w : weights)
        sum += std::max<std::int64_t>(0, w);
    if (sum <= 0 || amount == 0)
        return out;
    std::vector<std::pair<std::int64_t, std::size_t>> rest;
    std::int64_t given = 0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        const std::int64_t w = std::max<std::int64_t>(0, weights[i]);
        const std::int64_t num = amount * w;   // cents x cents: far inside 64 bits
        out[i] = std::int64_t(num / sum);
        given += out[i];
        rest.emplace_back(std::int64_t(num % sum), i);
    }
    std::stable_sort(rest.begin(), rest.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (std::size_t k = 0; given < amount && k < rest.size(); ++k, ++given)
        ++out[rest[k].second];
    return out;
}

} // namespace

// --- makers and product types ---------------------------------------------------------------

const std::vector<std::string> &makers()
{
    static const std::vector<std::string> m{"pmusa", "usstc", "jmc", "helix", "njoy", "other"};
    return m;
}

const std::vector<std::string> &categories()
{
    static const std::vector<std::string> c{"cig", "mst", "cigar", "onp", "vapor", "ryo", "pipe", "other", "cbd", "none"};
    return c;
}

const std::vector<std::string> &optionalCategories()
{
    static const std::vector<std::string> c{"cig", "mst", "cigar", "onp", "vapor", "ryo", "pipe", "other"};
    return c;
}

bool isAltriaMaker(const std::string &maker)
{
    return maker == "pmusa" || maker == "usstc" || maker == "jmc" || maker == "helix" || maker == "njoy";
}

std::string makerName(const std::string &maker, const std::string &itemManufacturer)
{
    if (maker == "pmusa") return "PM USA";
    if (maker == "usstc") return "USSTC";
    if (maker == "jmc") return "John Middleton";
    if (maker == "helix") return "Helix";
    if (maker == "njoy") return "NJOY";
    const std::string own = cleanText(itemManufacturer, 20);
    return own.empty() ? std::string("Other") : own;
}

std::string categoryCode(const std::string &category)
{
    if (category == "none" || category.empty())
        return {};
    return upper(category);
}

// --- text -----------------------------------------------------------------------------------

std::string normalUpc(const std::string &code)
{
    std::string d = digitsOf(code);
    if (d.size() == 11)
        return "0" + d;
    if (d.size() == 13 && d[0] == '0')
        return d.substr(1);
    if (d.size() == 14 && d.starts_with("00"))
        return d.substr(2);
    if (d.size() == 8 && (d[0] == '0' || d[0] == '1')) {   // UPC-E
        const char ns = d[0], check = d[7];
        const std::string x = d.substr(1, 6);
        std::string maker, product;
        switch (x[5]) {
        case '0': case '1': case '2':
            maker = std::string() + x[0] + x[1] + x[5] + "00";
            product = std::string("00") + x[2] + x[3] + x[4];
            break;
        case '3':
            maker = std::string() + x[0] + x[1] + x[2] + "00";
            product = std::string("000") + x[3] + x[4];
            break;
        case '4':
            maker = std::string() + x[0] + x[1] + x[2] + x[3] + "0";
            product = std::string("0000") + x[4];
            break;
        default:
            maker = x.substr(0, 5);
            product = std::string("0000") + x[5];
        }
        return std::string(1, ns) + maker + product + check;
    }
    return d;
}

std::string cleanText(const std::string &text, std::size_t max, bool utf8)
{
    std::string out;
    bool space = false;
    for (unsigned char c : text) {
        if (c == '|' || c == '\r' || c == '\n' || c == '\t' || c == ' ' || c < 0x20 || c == 0x7f) {
            space = !out.empty();
            continue;
        }
        if (c >= 0x80 && !utf8)
            continue;
        if (space)
            out += ' ';
        space = false;
        out += char(c);
    }
    if (out.size() > max) {
        out.resize(max);
        // Not in the middle of a UTF-8 letter.
        while (utf8 && !out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
            out.pop_back();
        if (utf8 && !out.empty() && static_cast<unsigned char>(out.back()) >= 0xC0)
            out.pop_back();
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
    }
    return out;
}

// --- settings -------------------------------------------------------------------------------

std::vector<std::string> settingsProblems(const Settings &s)
{
    std::vector<std::string> p;
    const std::string acct = trim(s.accountNumber);
    const bool chain = s.mode == "chain";
    if (s.mode != "independent" && !chain)
        p.push_back("Scan data: choose independent (Retail Control Number) or chain (Management Account Number).");
    const auto sample = [](const std::string &v) {
        if (v.empty())
            return true;
        if (std::ranges::all_of(v, [&](char c) { return c == v[0]; }))
            return true;   // 000000, 111111...
        return v == "123456" || v == "12345" || v == "1234567890" || v == "012345";
    };
    if (chain) {
        if (acct.size() != 5 || !allDigits(acct) || sample(acct))
            p.push_back("Scan data: the Management Account Number (5 digits, from the Altria representative) isn't set.");
        if (!allDigits(trim(s.storeNumber)) || trim(s.storeNumber).size() > 10)
            p.push_back("Scan data: a chain needs this store's Store Number (digits, up to 10).");
    } else {
        if (acct.size() != 6 || !allDigits(acct) || sample(acct))
            p.push_back("Scan data: the Retail Control Number (6 digits, from the Altria representative) isn't set.");
        if (!trim(s.storeNumber).empty() && !allDigits(trim(s.storeNumber)))
            p.push_back("Scan data: the store number may only have digits (an independent store leaves it empty).");
    }
    const std::string name = cleanText(s.accountName, 50);
    if (name.empty() || !std::ranges::all_of(name, [](char c) { return std::isalnum((unsigned char)c); })
        || lower(name) == "quickgas" || lower(name) == "stopnshop" || lower(name) == "viewtouch")
        p.push_back("Scan data: the account name for the file name (letters and digits only, the same every week) isn't set.");
    const std::string store = cleanText(s.storeName, 50);
    if (store.empty() || lower(store) == "viewtouch" || lower(store).starts_with("quick gas"))
        p.push_back("Scan data: the store name (exactly as Altria has it) isn't set.");
    std::string street = upper(cleanText(s.street, 60));
    if (street.empty() || street == "123 MAIN ST")
        p.push_back("Scan data: the store's street address isn't set.");
    std::string compact;
    for (char c : street)
        if (std::isalnum((unsigned char)c))
            compact += c;
    if (compact.find("POBOX") != std::string::npos || compact.find("POSTOFFICEBOX") != std::string::npos)
        p.push_back("Scan data: the street address may not be a PO Box.");
    if (cleanText(s.city, 50).empty() || upper(trim(s.city)) == "RICHMOND")
        p.push_back("Scan data: the store's city isn't set.");
    static const std::set<std::string> states{
        "AL", "AK", "AZ", "AR", "CA", "CO", "CT", "DE", "FL", "GA", "HI", "ID", "IL", "IN", "IA", "KS", "KY",
        "LA", "ME", "MD", "MA", "MI", "MN", "MS", "MO", "MT", "NE", "NV", "NH", "NJ", "NM", "NY", "NC", "ND",
        "OH", "OK", "OR", "PA", "RI", "SC", "SD", "TN", "TX", "UT", "VT", "VA", "WA", "WV", "WI", "WY"};
    if (!states.contains(upper(trim(s.state))))
        p.push_back("Scan data: the state must be a 2-letter US state (CT).");
    const std::string zip = trim(s.zip4);
    const bool zipShape = zip.size() == 10 && zip[5] == '-' && allDigits(zip.substr(0, 5)) && allDigits(zip.substr(6));
    if (!zipShape || zip == "12345-0000" || zip.starts_with("00000") || zip.ends_with("-0000"))
        p.push_back("Scan data: the ZIP+4 must be all nine digits with the hyphen (06810-1234).");
    if (!trim(s.phone).empty()) {
        std::string d = digitsOf(s.phone);
        if (d.size() == 11 && d[0] == '1')
            d = d.substr(1);
        if (d.size() != 10)
            p.push_back("Scan data: the store phone must have 10 digits (or be left empty).");
    }
    if (!trim(s.email).empty() && trim(s.email).find('@') == std::string::npos)
        p.push_back("Scan data: the contact e-mail doesn't look like an e-mail address.");
    if (s.encoding != "ascii" && s.encoding != "utf8")
        p.push_back("Scan data: the file's letters must be ASCII or UTF-8.");
    if (s.lineEnding != "crlf" && s.lineEnding != "lf")
        p.push_back("Scan data: lines end CRLF or LF.");
    for (const std::string &c : s.optional)
        if (std::ranges::find(optionalCategories(), c) == optionalCategories().end())
            p.push_back("Scan data: unknown product type " + c + ".");
    return p;
}

// --- items ----------------------------------------------------------------------------------

ItemIndex::ItemIndex(const std::vector<MenuItem> &menu) : menu_(menu)
{
    for (std::size_t i = 0; i < menu.size(); ++i) {
        byId_.emplace(menu[i].id, i);
        for (const RecipeLine &r : menu[i].recipe)
            if (r.ingredientId.starts_with("__parent__:")) {
                const std::string parent = normalUpc(r.ingredientId.substr(11));
                if (!parent.empty())
                    packsIn_[parent] = std::max(1, int(r.quantity > 0 ? r.quantity + 0.5 : 10));
            }
    }
}

const MenuItem *ItemIndex::byId(const std::string &id) const
{
    const auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : &menu_[it->second];
}

ItemInfo ItemIndex::info(const MenuItem &m) const
{
    ItemInfo i;
    i.maker = m.scanMaker;
    i.category = m.scanCategory;
    const bool cans = m.scanCategory == "mst" || m.scanCategory == "onp";
    const bool cigs = m.scanCategory == "cig";
    int inside = 0;
    for (const std::string &b : m.barcodes)
        if (const auto it = packsIn_.find(normalUpc(b)); it != packsIn_.end())
            inside = it->second;
    std::string uom = lower(m.scanUom);
    if (uom.empty())
        uom = inside > 0 ? (cigs ? "carton" : cans ? "roll" : "box") : (cigs ? "pack" : cans ? "can" : "each");
    int units = m.scanUnits;
    if (units <= 0) {
        if (inside > 0 && (uom == "carton" || uom == "roll" || uom == "box"))
            units = inside;
        else
            units = uom == "carton" ? 10 : uom == "roll" ? 5 : 1;
    }
    i.uom = uom.empty() ? std::string() : std::string(1, char(std::toupper((unsigned char)uom[0]))) + uom.substr(1);
    i.units = units;
    return i;
}

// --- bulk fill ------------------------------------------------------------------------------

const std::string &defaultRulesText()
{
    static const std::string t =
        "028200 pmusa cig\n"
        "073100 usstc mst\n"
        "070137 jmc cigar\n"
        "855022 helix onp\n";
    return t;
}

std::vector<FillRule> parseRules(const std::string &text)
{
    std::vector<FillRule> out;
    std::size_t at = 0;
    while (at < text.size()) {
        const auto end = text.find('\n', at);
        const std::string line = trim(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end == std::string::npos ? text.size() : end + 1;
        if (line.empty() || line[0] == '#')
            continue;
        // The last two words are the maker and the product type; everything before them is the match
        // (a department's name may have spaces).
        std::vector<std::string> words;
        std::string word;
        for (char c : line + " ") {
            if (c == ' ' || c == '\t') {
                if (!word.empty())
                    words.push_back(word);
                word.clear();
            } else {
                word += c;
            }
        }
        if (words.size() < 3)
            continue;
        FillRule r;
        r.category = lower(words.back());
        r.maker = lower(words[words.size() - 2]);
        std::string match;
        for (std::size_t i = 0; i + 2 < words.size(); ++i)
            match += (i ? " " : "") + words[i];
        if (lower(match).starts_with("dept:")) {
            r.dept = true;
            r.match = trim(match.substr(5));
        } else {
            r.match = digitsOf(match);
        }
        const bool knownMaker = std::ranges::find(makers(), r.maker) != makers().end();
        const bool knownType = std::ranges::find(categories(), r.category) != categories().end();
        if (!r.match.empty() && knownMaker && knownType)
            out.push_back(r);
    }
    return out;
}

bool fillItem(MenuItem &m, const std::vector<FillRule> &rules, const std::vector<MenuCategory> &departments,
              bool overwrite)
{
    if (!overwrite && !m.scanMaker.empty() && !m.scanCategory.empty())
        return false;
    const FillRule *hit = nullptr;
    for (const FillRule &r : rules) {
        if (r.dept)
            continue;
        for (const std::string &b : m.barcodes)
            if (normalUpc(b).starts_with(r.match)) {
                hit = &r;
                break;
            }
        if (hit)
            break;
    }
    if (!hit) {
        const auto dept = std::ranges::find(departments, m.family, &MenuCategory::id);
        for (const FillRule &r : rules) {
            if (!r.dept)
                continue;
            const std::string want = lower(r.match);
            if (lower(m.family) == want
                || (dept != departments.end() && (lower(dept->name) == want || lower(dept->group) == want))) {
                hit = &r;
                break;
            }
        }
    }
    if (!hit)
        return false;
    bool changed = false;
    if ((overwrite || m.scanMaker.empty()) && m.scanMaker != hit->maker) {
        m.scanMaker = hit->maker;
        changed = true;
    }
    if ((overwrite || m.scanCategory.empty()) && m.scanCategory != hit->category) {
        m.scanCategory = hit->category;
        changed = true;
    }
    return changed;
}

// --- dates ----------------------------------------------------------------------------------

std::int64_t daysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = unsigned((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + std::int64_t(doe) - 719468;
}

void civilFromDays(std::int64_t z, int &y, int &m, int &d)
{
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = int(doy - (153 * mp + 2) / 5 + 1);
    m = int(mp < 10 ? mp + 3 : mp - 9);
    y = int(std::int64_t(yoe) + era * 400 + (m <= 2));
}

bool parseYmd(const std::string &ymd, int &y, int &m, int &d)
{
    const std::string s = digitsOf(ymd);
    if (s.size() != 8)
        return false;
    y = std::stoi(s.substr(0, 4));
    m = std::stoi(s.substr(4, 2));
    d = std::stoi(s.substr(6, 2));
    if (m < 1 || m > 12 || d < 1 || d > 31)
        return false;
    int y2, m2, d2;
    civilFromDays(daysFromCivil(y, m, d), y2, m2, d2);
    return y2 == y && m2 == m && d2 == d;
}

std::string ymdText(int y, int m, int d)
{
    return std::to_string(y) + two(m) + two(d);
}

std::string addDays(const std::string &ymd, int days)
{
    int y, m, d;
    if (!parseYmd(ymd, y, m, d))
        return {};
    civilFromDays(daysFromCivil(y, m, d) + days, y, m, d);
    return ymdText(y, m, d);
}

bool isSaturday(const std::string &ymd)
{
    int y, m, d;
    if (!parseYmd(ymd, y, m, d))
        return false;
    // 1970-01-01 was a Thursday (4); Saturday is 6.
    const std::int64_t z = daysFromCivil(y, m, d);
    return ((z % 7 + 7 + 4) % 7) == 6;
}

std::string lastSaturday(const std::string &today)
{
    int y, m, d;
    if (!parseYmd(today, y, m, d))
        return {};
    const std::int64_t z = daysFromCivil(y, m, d);
    const int weekday = int((z % 7 + 7 + 4) % 7);   // 0 Sunday ... 6 Saturday
    civilFromDays(z - (weekday + 1), y, m, d);     // the Saturday before this week's Sunday
    return ymdText(y, m, d);
}

// --- the file -------------------------------------------------------------------------------

bool Result::hasErrors() const
{
    return std::ranges::any_of(issues, [](const Issue &i) { return i.error; });
}

std::string Result::line(const Record &r) const
{
    std::string out;
    for (int i = 0; i < kFields; ++i) {
        if (i)
            out += '|';
        out += r.f[std::size_t(i)];
    }
    return out;
}

std::string Result::text(const Settings &s) const
{
    const std::string end = s.lineEnding == "lf" ? "\n" : "\r\n";
    std::string out = summary + end;
    for (const Record &r : records)
        out += line(r) + end;
    return out;
}

namespace {

enum class Funding { Store, Coupon, Multi, OtherMfr };

struct Deal {
    Money total;
    int consumerUnits = 0;
};

struct Work {
    const OrderLine *line = nullptr;
    const MenuItem *item = nullptr;
    ItemInfo info;
    std::string upc;
    bool report = false;
    bool returned = false;
    int qty = 0;          // signed
    Money extended;       // the line's total before discounts
    Money store, coupon, other, multi;
    std::vector<std::string> storeNames, couponNames, otherNames;
    std::vector<std::size_t> deals;   // into the check's deals
    std::map<std::size_t, int> dealUnits;   // deal -> units of this line in it
};

void addName(std::vector<std::string> &names, const std::string &n)
{
    if (std::ranges::find(names, n) == names.end())
        names.push_back(n);
}

std::string joined(const std::vector<std::string> &names, std::size_t max, bool utf8)
{
    std::string all;
    for (const std::string &n : names)
        all += (all.empty() ? "" : " + ") + n;
    return cleanText(all, max, utf8);
}

} // namespace

Result build(const std::vector<Check> &checks, const std::vector<MenuItem> &menu,
             const std::vector<PosSettings::Promotion> &promotions, const TaxRates &rates, const Settings &settings,
             const Options &options)
{
    Result res;
    const bool utf8 = settings.encoding == "utf8";
    for (const std::string &p : settingsProblems(settings))
        res.issues.push_back({true, p, 0});
    if (!isSaturday(options.weekEnding))
        res.issues.push_back({true, "The week must end on a Saturday.", 0});
    res.fileName = cleanText(settings.accountName, 50);
    std::erase_if(res.fileName, [](char c) { return !std::isalnum((unsigned char)c); });
    res.fileName += options.weekEnding;

    const ItemIndex index(menu);
    std::set<std::string> warnedItems;
    const auto warnItem = [&](const std::string &key, bool error, const std::string &text, std::int64_t check) {
        if (warnedItems.insert(key).second)
            res.issues.push_back({error, text, check});
    };

    std::vector<const Check *> week;
    for (const Check &c : checks)
        if (c.status == CheckStatus::Closed && !c.training && c.cancelNumber == 0 && c.closedAt >= options.from
            && c.closedAt < options.to)
            week.push_back(&c);
    std::ranges::stable_sort(week, [](const Check *a, const Check *b) {
        return a->closedAt != b->closedAt ? a->closedAt < b->closedAt : a->id < b->id;
    });

    std::set<std::int64_t> ids;
    for (const Check *c : week) {
        if (!ids.insert(c->id).second) {
            res.issues.push_back({true, "Check " + std::to_string(c->id) + " is in the week twice.", c->id});
            continue;
        }
        std::vector<Work> work(c->lines.size());
        bool any = false;
        for (std::size_t i = 0; i < c->lines.size(); ++i) {
            const OrderLine &l = c->lines[i];
            Work &w = work[i];
            w.line = &l;
            if (l.isComment() || l.voided || l.corrected || l.isGiftCard() || l.isFee() || l.quantity == 0)
                continue;
            w.item = index.byId(l.itemId);
            if (w.item)
                w.info = index.info(*w.item);
            w.upc = normalUpc(!l.upc.empty() ? l.upc : w.item && !w.item->barcodes.empty() ? w.item->barcodes.front()
                                                                                           : std::string());
            w.extended = l.total();
            w.returned = w.extended.cents() < 0 || l.quantity < 0;
            w.qty = (w.returned ? -1 : 1) * std::abs(l.quantity);
            const std::string what = "\"" + l.name + "\"" + (w.upc.empty() ? "" : " (" + w.upc + ")");
            // An Altria company prefix on an item not set up as Altria's: never left out quietly.
            for (const auto &[prefix, maker] : altriaPrefixes())
                if (w.upc.size() == 12 && w.upc.starts_with(prefix) && !isAltriaMaker(w.info.maker))
                    warnItem("prefix:" + l.itemId, true,
                             what + " has an Altria company UPC but its scan data maker isn't " + makerName(maker)
                                 + ": set it in the Menu Builder (or Fill in makers).",
                             c->id);
            if (w.info.category == "cbd" || w.info.category == "none")
                continue;   // CBD is never sent
            if (isAltriaMaker(w.info.maker)) {
                w.report = true;
                if (w.info.category.empty())
                    warnItem("type:" + l.itemId, true, what + " has no scan data product type.", c->id);
            } else if (w.info.maker == "other" || (w.info.maker.empty() && !w.info.category.empty())) {
                if (w.info.category.empty()) {
                    warnItem("type:" + l.itemId, false, what + " has a maker but no product type: left out.", c->id);
                } else if (std::ranges::find(settings.optional, w.info.category) != settings.optional.end()) {
                    w.report = true;
                    if (w.info.maker.empty())
                        warnItem("maker:" + l.itemId, true, what + " is a " + categoryCode(w.info.category)
                                     + " (sent) but has no scan data maker.", c->id);
                }
            }
            if (!w.report)
                continue;
            any = true;
            if (w.upc.size() != 12)
                warnItem("upc:" + l.itemId, true,
                         what + (w.upc.empty() ? " has no UPC." : " has a UPC that isn't 12 digits."), c->id);
        }
        if (!any)
            continue;

        // Discounts: each one's amount (as the check's totals took it off), shared over the lines it
        // covers, then sorted by who pays for it.
        const Totals totals = c->totals(rates);
        std::vector<Deal> deals;
        const auto mayDiscount = [](const OrderLine &l) {
            return !l.isComment() && !l.voided && !l.corrected && !l.isGiftCard() && !l.noDiscount && l.total().cents() > 0;
        };
        for (const Payment &p : c->payments) {
            if (p.kind != TenderKind::Discount)
                continue;
            const auto off = totals.discountBy.find(p.id);
            if (off == totals.discountBy.end() || off->second.cents() <= 0)
                continue;
            const PosSettings::Promotion *promo = nullptr;
            if (p.reference.starts_with("promo:")) {
                const std::string id = p.reference.substr(6);
                const auto it = std::ranges::find(promotions, id, &PosSettings::Promotion::id);
                if (it != promotions.end())
                    promo = &*it;
            }
            std::vector<std::string> names{p.tenderName, p.tenderId};
            if (promo) {
                names.push_back(promo->name);
                names.push_back(promo->id);
                names.push_back(promo->code);
            }
            const Funding funding = sameName(settings.couponNames, names) ? Funding::Coupon
                                    : sameName(settings.multiNames, names) ? Funding::Multi
                                    : sameName(settings.otherMfrNames, names) ? Funding::OtherMfr
                                                                            : Funding::Store;
            std::string name = promo ? (funding == Funding::Coupon && !promo->code.empty() ? promo->code : promo->name)
                                     : p.tenderName;
            if (p.staffMeal && name.empty())
                name = "Staff meal";
            if (name.empty())
                name = "Discount";

            std::vector<std::size_t> covered;
            for (std::size_t i = 0; i < c->lines.size(); ++i) {
                const OrderLine &l = c->lines[i];
                if (!mayDiscount(l))
                    continue;
                if (p.lineId != 0 && l.id != p.lineId)
                    continue;
                if (p.lineId == 0 && p.staffMeal && l.noStaffDiscount)
                    continue;
                if (p.lineId == 0 && promo && (!promo->families.empty() || !promo->items.empty())) {
                    const MenuItem *m = index.byId(l.itemId);
                    const bool fam = m && std::ranges::find(promo->families, m->family) != promo->families.end();
                    const bool item = std::ranges::find(promo->items, l.itemId) != promo->items.end();
                    if (!fam && !item)
                        continue;
                }
                covered.push_back(i);
            }
            if (covered.empty())
                continue;
            // Which units are in the deal: a buy-N-get-M promotion's whole groups (dearest first, as the
            // promotion counts them); otherwise every unit it covers.
            std::vector<std::int64_t> weights(covered.size(), 0);
            std::vector<int> unitsIn(covered.size(), 0);
            const int group = promo && promo->buy > 0 ? promo->buy + std::max(1, promo->get) : 0;
            if (group > 0) {
                std::vector<std::pair<std::int64_t, std::size_t>> units;   // value, covered index
                for (std::size_t k = 0; k < covered.size(); ++k) {
                    const OrderLine &l = c->lines[covered[k]];
                    const int q = std::max(1, l.quantity);
                    for (int u = 0; u < q; ++u)
                        units.emplace_back(l.total().cents() / q, k);
                }
                std::ranges::stable_sort(units, [](const auto &a, const auto &b) { return a.first > b.first; });
                const std::size_t inDeal = units.size() / std::size_t(group) * std::size_t(group);
                for (std::size_t u = 0; u < inDeal; ++u) {
                    weights[units[u].second] += units[u].first;
                    ++unitsIn[units[u].second];
                }
            } else {
                for (std::size_t k = 0; k < covered.size(); ++k) {
                    weights[k] = c->lines[covered[k]].total().cents();
                    unitsIn[k] = std::max(1, c->lines[covered[k]].quantity);
                }
            }
            const std::vector<std::int64_t> shares = allocate(off->second.cents(), weights);
            std::size_t deal = 0;
            if (funding == Funding::Multi) {
                deal = deals.size();
                deals.push_back({off->second, 0});
            }
            for (std::size_t k = 0; k < covered.size(); ++k) {
                Work &w = work[covered[k]];
                const Money share = Money::fromCents(shares[k]);
                if (share.cents() == 0 && unitsIn[k] == 0)
                    continue;
                switch (funding) {
                case Funding::Store: w.store += share; addName(w.storeNames, name); break;
                case Funding::Coupon: w.coupon += share; addName(w.couponNames, name); break;
                case Funding::OtherMfr: w.other += share; addName(w.otherNames, name); break;
                case Funding::Multi:
                    w.multi += share;
                    w.deals.push_back(deal);
                    w.dealUnits[deal] = unitsIn[k];
                    if (w.report)
                        deals[deal].consumerUnits += unitsIn[k] * std::max(1, w.info.units);
                    break;
                }
            }
        }

        // The records, identical ones merged (same UPC, price, discounts per unit, deals).
        std::vector<Record> out;
        std::vector<std::string> keys;
        const std::string date = options.date ? options.date(c->closedAt) : std::string();
        const std::string time = options.time ? options.time(c->closedAt) : std::string();
        std::string age;
        if (c->ageVerified > 0)
            age = c->ageHow == "scan" ? "Scanned ID" : c->ageHow == "hand" ? "Manual Entry" : std::string();
        std::string phone = digitsOf(settings.phone);
        if (phone.size() == 11 && phone[0] == '1')
            phone = phone.substr(1);
        const bool inStore = !c->kiosk && (c->type == CheckType::Quick || c->type == CheckType::DineIn
                                           || c->type == CheckType::Tab);
        for (const Work &w : work) {
            if (!w.report)
                continue;
            const OrderLine &l = *w.line;
            const std::int64_t q = std::abs(w.qty);
            const auto perUnit = [&](Money m) {
                return m.cents() % q == 0 ? std::to_string(m.cents() / q) : "x" + std::to_string(m.cents()) + "/" + std::to_string(l.id);
            };
            std::string dealKey;
            for (std::size_t d : w.deals)
                dealKey += std::to_string(d) + ",";
            const std::string key = w.upc + "|" + l.itemId + "|" + perUnit(w.extended) + "|" + (w.returned ? "-" : "+") + "|"
                                    + perUnit(w.store) + joined(w.storeNames, 99, true) + "|" + perUnit(w.coupon)
                                    + joined(w.couponNames, 99, true) + "|" + perUnit(w.other)
                                    + joined(w.otherNames, 99, true) + "|" + perUnit(w.multi) + "|" + dealKey;
            Money fsp = w.extended - w.store - w.coupon - w.other - w.multi;
            const auto found = std::ranges::find(keys, key);
            if (found != keys.end()) {
                Record &r = out[std::size_t(found - keys.begin())];
                r.quantity += w.qty;
                r.finalPrice += fsp;
                r.f[17] = std::to_string(r.quantity);
                r.f[30] = r.finalPrice.toString();
                const auto addMoney = [](std::string &field, Money m) {
                    if (m.cents() == 0)
                        return;
                    std::int64_t now = 0;
                    if (!field.empty()) {
                        const auto dot = field.find('.');
                        now = std::stoll(field.substr(0, dot)) * 100 + std::stoll(field.substr(dot + 1));
                    }
                    field = Money::fromCents(now + m.cents()).toString();
                };
                addMoney(r.f[23], w.store);
                addMoney(r.f[25], w.coupon);
                addMoney(r.f[27], w.other);
                continue;
            }
            Record r;
            r.checkId = c->id;
            r.quantity = w.qty;
            r.finalPrice = fsp;
            auto &f = r.f;
            const bool chain = settings.mode == "chain";
            f[0] = digitsOf(settings.accountNumber);
            f[1] = options.weekEnding;
            f[2] = date;
            f[3] = time;
            f[4] = std::to_string(c->id);
            f[5] = chain ? digitsOf(settings.storeNumber) : cleanText(digitsOf(settings.storeNumber), 10);
            f[6] = cleanText(settings.storeName, 50, utf8);
            f[7] = cleanText(settings.street, 60, utf8);
            f[8] = cleanText(settings.city, 50, utf8);
            f[9] = upper(trim(settings.state)).substr(0, 2);
            f[10] = trim(settings.zip4);
            f[11] = cleanText(categoryCode(w.info.category), 20);
            f[12] = makerName(w.info.maker, w.item ? w.item->manufacturer : std::string());
            if (utf8 && w.info.maker == "other" && w.item)
                f[12] = cleanText(w.item->manufacturer.empty() ? "Other" : w.item->manufacturer, 20, true);
            f[13] = !w.upc.empty() ? w.upc : cleanText(l.itemId, 50);
            f[14] = w.upc;
            f[15] = cleanText(l.name, 50, utf8);
            f[16] = cleanText(w.info.uom, 15);
            f[17] = std::to_string(w.qty);
            f[18] = std::to_string(std::max(1, w.info.units));
            if (!w.deals.empty() && !w.returned) {
                Money dealTotal;
                int units = 0;
                for (std::size_t d : w.deals) {
                    dealTotal += deals[d].total;
                    units += deals[d].consumerUnits;
                }
                f[19] = "Y";
                if (units > 99) {
                    res.issues.push_back({false, "Check " + f[4] + ": a multi-unit deal on more than 99 units: 99 reported.", c->id});
                    units = 99;
                }
                f[20] = std::to_string(std::max(1, units));
                f[21] = dealTotal.toString();
                if (w.deals.size() > 1)
                    res.issues.push_back({false, "Check " + f[4] + ": two manufacturer multi-unit deals on \"" + l.name
                                                     + "\": reported as one.", c->id});
            } else {
                f[19] = "N";
            }
            if (!w.returned) {
                if (w.store.cents() > 0) { f[22] = joined(w.storeNames, 20, utf8); f[23] = w.store.toString(); }
                if (w.coupon.cents() > 0) { f[24] = joined(w.couponNames, 50, utf8); f[25] = w.coupon.toString(); }
                if (w.other.cents() > 0) { f[26] = joined(w.otherNames, 20, utf8); f[27] = w.other.toString(); }
            }
            f[30] = fsp.toString();
            f[31] = phone.size() == 10 ? phone : std::string();
            f[32] = cleanText(settings.contact, 20, utf8);
            f[33] = cleanText(settings.email, 50);
            f[38] = age;
            f[41] = inStore ? "In Store" : std::string();
            out.push_back(r);
            keys.push_back(key);
        }
        if (!c->refunds.empty())
            res.issues.push_back({false, "Check " + std::to_string(c->id)
                                             + " had money refunded after it closed: refunds aren't by item, so the file shows the sale as rung.",
                                  c->id});
        ++res.checks;
        for (Record &r : out) {
            if (r.quantity == 0)
                continue;   // a sale and its return on the same check cancel out
            res.quantity += r.quantity;
            res.total += r.finalPrice;
            res.records.push_back(std::move(r));
        }
    }
    if (res.records.empty())
        res.issues.push_back({false, "Nothing to report this week: the file has only its summary line.", 0});

    std::string provider = cleanText(settings.provider.empty() ? settings.storeName : settings.provider, 50);
    res.summary = std::to_string(res.records.size()) + "|" + std::to_string(res.quantity) + "|" + res.total.toString()
                  + "|" + provider;
    if (provider.empty())
        res.issues.push_back({true, "The summary line needs the provider's or the store's name.", 0});
    for (Issue &i : checkFile(res))
        res.issues.push_back(std::move(i));
    return res;
}

std::vector<Issue> checkFile(const Result &r)
{
    std::vector<Issue> out;
    std::map<std::string, std::pair<std::string, std::string>> when;   // transaction -> date, time
    int qty = 0;
    Money total;
    for (const Record &rec : r.records) {
        const auto &f = rec.f;
        const std::string where = "Check " + f[4] + ": ";
        const std::string line = r.line(rec);
        if (std::ranges::count(line, '|') != kFields - 1)
            out.push_back({true, where + "a record without exactly 45 fields.", rec.checkId});
        if (!allDigits(f[0]) || (f[0].size() != 5 && f[0].size() != 6))
            out.push_back({true, where + "field 1 must be the 5- or 6-digit account number.", rec.checkId});
        if (f[1].size() != 8 || !allDigits(f[1]) || f[1] != (r.fileName.size() >= 8 ? r.fileName.substr(r.fileName.size() - 8) : std::string()))
            out.push_back({true, where + "field 2 must be the week's Saturday.", rec.checkId});
        if (f[2].size() != 8 || !allDigits(f[2]))
            out.push_back({true, where + "field 3 must be the date as YYYYMMDD.", rec.checkId});
        const bool timeOk = f[3].size() == 8 && f[3][2] == ':' && f[3][5] == ':' && allDigits(f[3].substr(0, 2))
                            && allDigits(f[3].substr(3, 2)) && allDigits(f[3].substr(6, 2));
        if (!timeOk)
            out.push_back({true, where + "field 4 must be the time as HH:MM:SS.", rec.checkId});
        if (f[4].empty() || f[4].size() > 50)
            out.push_back({true, where + "field 5 (transaction) is missing.", rec.checkId});
        const auto [it, fresh] = when.emplace(f[4], std::pair{f[2], f[3]});
        if (!fresh && (it->second.first != f[2] || it->second.second != f[3]))
            out.push_back({true, where + "fields 3 and 4 differ between its lines.", rec.checkId});
        if (f[14].size() != 12 || !allDigits(f[14]))
            out.push_back({true, where + """ + f[15] + "" field 15 must be a 12-digit UPC.", rec.checkId});
        if (f[11].empty())
            out.push_back({true, where + """ + f[15] + "" has no category (field 12).", rec.checkId});
        if (f[12].empty())
            out.push_back({true, where + """ + f[15] + "" has no manufacturer (field 13).", rec.checkId});
        const auto money = [&](int field, bool negative) {
            const std::string &v = f[std::size_t(field - 1)];
            if (!v.empty() && !moneyText(v, negative))
                out.push_back({true, where + "field " + std::to_string(field) + " (" + v + ") must be money with 2 decimals"
                                         + (negative ? "." : ", never negative."), rec.checkId});
        };
        for (int field : {22, 24, 26, 28, 30, 41})
            money(field, false);
        money(31, true);
        if (f[30].empty())
            out.push_back({true, where + "field 31 (final price) is missing.", rec.checkId});
        if (f[17].empty() || f[17] == "0" || !allDigits(f[17][0] == '-' ? f[17].substr(1) : f[17]))
            out.push_back({true, where + "field 18 (quantity) must be a whole number, not 0.", rec.checkId});
        if ((f[17][0] == '-') != (!f[30].empty() && f[30][0] == '-') && f[30] != "0.00")
            out.push_back({true, where + "a return must be negative in both 18 and 31.", rec.checkId});
        if (f[19] != "Y" && f[19] != "N")
            out.push_back({true, where + "field 20 must be Y or N.", rec.checkId});
        if (f[19] == "Y" && (f[20].empty() || f[20] == "0" || f[21].empty()))
            out.push_back({true, where + "a multi-unit deal needs fields 21 and 22.", rec.checkId});
        if (!f[37].empty() && (f[37].size() != 6 || !allDigits(f[37])))
            out.push_back({true, where + "field 38 must be exactly 6 digits.", rec.checkId});
        if (!f[38].empty() && f[38] != "Scanned ID" && f[38] != "Manual Entry" && f[38] != "Other")
            out.push_back({true, where + "field 39 must be Scanned ID, Manual Entry or Other.", rec.checkId});
        for (std::size_t i = 42; i < 45; ++i)
            if (!f[i].empty())
                out.push_back({true, where + "fields 43-45 must be blank.", rec.checkId});
        qty += rec.quantity;
        total += rec.finalPrice;
    }
    if (qty != r.quantity || total != r.total)
        out.push_back({true, "The summary line's totals don't match the records.", 0});
    if (r.summary.substr(0, r.summary.find('|')) != std::to_string(r.records.size()))
        out.push_back({true, "The summary line's record count doesn't match.", 0});
    return out;
}

} // namespace vt::core::scandata
