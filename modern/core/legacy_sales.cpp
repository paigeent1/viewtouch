#include "core/legacy_sales.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <set>

namespace vt::core::legacy {

namespace {

std::string units(double v)
{
    char buf[32];
    if (std::abs(v - std::round(v)) < 0.0005)
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(std::llround(v)));
    else
        std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

std::string count(std::int64_t n) { return std::to_string(n); }

std::string lower(std::string s)
{
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The columns of the sales-by reports. Cost is as ILSA counts it (lines without a cost at their
// retail); with buydowns: ILSA's Department Sales columns.
const std::vector<std::string> kGpColumns = {"Units", "Retail", "Cost", "GP% as ILSA", "GP% w/o no-cost", "No-cost retail"};
const std::vector<std::string> kDeptColumns = {"Units", "Retail", "Discount", "Cost", "Buydowns", "Adj. cost",
                                               "GP% as ILSA", "GP% w/o no-cost", "No-cost retail"};

struct Sums {
    double units = 0;
    Money sales, cost, noCost, buydowns, discounts;
    void add(double u, Money s, Money c, Money n, Money b, Money d)
    {
        units += u; sales += s; cost += c; noCost += n; buydowns += b; discounts += d;
    }
};

std::vector<std::string> gpCells(const std::string &label, const Sums &t, bool withBuydowns, const ReportContext &ctx)
{
    const Money ilsaCost = t.cost + t.noCost;
    if (!withBuydowns)
        return {label, units(t.units), ctx.money(t.sales), ctx.money(ilsaCost), gpAsIlsa(t.sales, t.cost, t.noCost),
                gpTrue(t.sales, t.cost, t.noCost), ctx.money(t.noCost)};
    return {label, units(t.units), ctx.money(t.sales), ctx.money(t.discounts), ctx.money(ilsaCost), ctx.money(t.buydowns),
            ctx.money(ilsaCost + t.buydowns), gpAsIlsa(t.sales, t.cost, t.noCost, t.buydowns, t.discounts),
            gpTrue(t.sales, t.cost, t.noCost, t.buydowns, t.discounts), ctx.money(t.noCost)};
}

void gpNotes(Report &r, bool withBuydowns)
{
    r.note("Cost: each line's cost at the time of sale x its quantity; lines without a cost (dept 260 open rings) at their retail, as ILSA counts them (0% margin).");
    r.note("GP% as ILSA: those lines in, as ILSA's printouts. GP% w/o no-cost: those lines left out (their retail is in No-cost retail): the true margin.");
    if (withBuydowns)
        r.note("Buydowns: promotions (ILSA's buydowns). Adj. cost = Cost + Buydowns; GP on Retail + Discount.");
    r.note("Lottery is history only: not in sales or GP. Gift cards and coupons are not sales.");
}

Money centsOf(const std::map<std::string, std::int64_t> &m, const std::string &key)
{
    const auto it = m.find(key);
    return Money::fromCents(it == m.end() ? 0 : it->second);
}

std::string fit(std::string left, const std::string &right, int width)
{
    if (right.empty())
        return left.size() > std::size_t(width) ? left.substr(0, std::size_t(width)) : left;
    const int room = width - int(right.size()) - 1;
    if (room < 1)
        return right;
    if (int(left.size()) > room)
        left = left.substr(0, std::size_t(room));
    return left + std::string(std::size_t(width - int(left.size()) - int(right.size())), ' ') + right;
}

std::string centered(const std::string &s, int width)
{
    if (int(s.size()) >= width)
        return s.substr(0, std::size_t(width));
    const int pad = (width - int(s.size())) / 2;
    return std::string(std::size_t(pad), ' ') + s;
}

std::string plain(Money m)   // 1,234.56 / -1,234.56, as ILSA printed
{
    const std::int64_t c = m.cents() < 0 ? -m.cents() : m.cents();
    std::string whole = std::to_string(c / 100);
    for (int i = int(whole.size()) - 3; i > 0; i -= 3)
        whole.insert(std::size_t(i), ",");
    std::string frac = std::to_string(c % 100);
    if (frac.size() < 2)
        frac.insert(0, "0");
    return (m.cents() < 0 ? "-" : "") + whole + "." + frac;
}

} // namespace

Money ZTotals::tender(const std::string &name) const
{
    Money sum;
    for (const auto &[n, m] : tenders)
        if (n == name)
            sum += m;
    return sum;
}

std::string percent(Money part, Money whole)
{
    if (whole.cents() == 0)
        return "—";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f%%", 100.0 * double(part.cents()) / double(whole.cents()));
    return buf;
}

std::string gp(Money sales, Money cost)
{
    return percent(sales - cost, sales);
}

std::string gpAsIlsa(Money sales, Money cost, Money noCostSales, Money buydowns, Money discounts)
{
    return gp(sales + discounts, cost + noCostSales + buydowns);
}

std::string gpTrue(Money sales, Money cost, Money noCostSales, Money buydowns, Money discounts)
{
    return gp(sales - noCostSales + discounts, cost + buydowns);
}

std::string tenderGroup(int ilsaType)
{
    switch (ilsaType) {
    case 1: return "Cash";
    case 10: return "Check";
    case 15: return "Gift card";
    case 40: case 70: case 80: return "Card";
    case 12: return "House charge";
    default: return "Other (" + std::to_string(ilsaType) + ")";
    }
}

std::string tenderGroup(const Payment &p)
{
    switch (p.kind) {
    case TenderKind::Cash: return lower(p.tenderName).find("check") != std::string::npos ? "Check" : "Cash";
    case TenderKind::Card: return "Card";
    case TenderKind::GiftCard: return "Gift card";
    case TenderKind::HouseAccount: return "House charge";
    case TenderKind::Discount: break;
    }
    return "Other";
}

ZTotals fromChecks(const std::vector<Check> &closed, const TaxRates &rates)
{
    ZTotals z;
    z.costKnown = false;
    std::map<std::string, Money> byTender;
    std::vector<std::string> order;
    Money change;
    for (const Check &c : closed) {
        if (c.training || c.status != CheckStatus::Closed)
            continue;
        const Totals t = c.totals(rates);
        ++z.receipts;
        z.merchandise += t.items;
        z.promotions -= t.discounts;
        z.tax += t.tax;
        z.receiptTotals += t.total;
        change += t.change;
        for (const Payment &p : c.payments) {
            if (p.kind == TenderKind::Discount)
                continue;
            const std::string g = tenderGroup(p);
            if (!byTender.contains(g))
                order.push_back(g);
            byTender[g] += p.amount;
        }
        for (const OrderLine &l : c.lines) {
            if (l.voided) {
                OrderLine live = l;
                live.voided = false;
                z.voided += live.total();
                ++z.voidLines;
            } else if (!l.itemId.empty() && !l.isGiftCard()) {
                z.units += l.quantity;
                ++z.itemLines;
            }
        }
        for (const Refund &r : c.refunds) {
            ++z.returnLines;
            z.returned -= r.amount;
        }
    }
    if (change.cents() != 0) {
        if (!byTender.contains("Cash"))
            order.push_back("Cash");
        byTender["Cash"] -= change;
    }
    for (const std::string &n : order)
        z.tenders.emplace_back(n, byTender[n]);
    return z;
}

Report dailyZ(const ZTotals &t, const std::string &source, const ReportContext &ctx)
{
    Report r;
    r.id = "ilsa-z";
    r.title = "Daily Z (" + source + ")";
    r.subtitle = ctx.period;
    r.columns = {"", "Amount"};
    r.section("Sales");
    r.line({"Receipts", count(t.receipts)});
    if (t.notSales)
        r.line({"Cancelled / no sale (not sales)", count(t.notSales)});
    r.line({"Merchandise", ctx.money(t.merchandise)});
    r.line({source == "ViewTouch" ? "Discounts & promotions" : "Buydowns / promotions", ctx.money(t.promotions)});
    if (t.discounts.cents() != 0)
        r.line({"Discounts", ctx.money(t.discounts)});
    r.total({"Department sales", ctx.money(t.departmentSales())});
    if (t.entries.cents() != 0)
        r.line({"Gift cards, coupons (not sales)", ctx.money(t.entries)});
    if (t.other.cents() != 0)
        r.line({"Other lines", ctx.money(t.other)});
    if (t.lottery.cents() != 0)
        r.line({"Lottery (history only)", ctx.money(t.lottery)});
    r.total({"Net on the receipts", ctx.money(t.net())});
    r.line({"Tax", ctx.money(t.tax)});
    r.total({"Total with tax", ctx.money(t.gross())});
    if (t.receiptTotals != t.gross())
        r.line({"Receipts' own totals", ctx.money(t.receiptTotals)});

    r.section("Tenders");
    Money collected;
    for (const auto &[name, amount] : t.tenders) {
        r.line({name, ctx.money(amount)});
        collected += amount;
    }
    r.total({"Collected", ctx.money(collected)});
    if (t.payouts.cents() != 0)
        r.line({"Paid outs (not sales)", ctx.money(t.payouts)});

    r.section("Voids and returns");
    r.line({"Voided lines", count(t.voidLines)});
    r.line({"Voided (worth)", ctx.money(t.voided)});
    r.line({"Returns", count(t.returnLines)});
    r.line({"Returned", ctx.money(t.returned)});

    r.section("Items");
    r.line({"Item lines", count(t.itemLines)});
    r.line({"Units sold (net)", units(t.units)});
    if (t.costKnown) {
        r.section("Gross profit (merchandise)");
        r.line({"Cost (as ILSA)", ctx.money(t.cost + t.noCostSales)});
        r.line({"Adjusted cost (less buydowns)", ctx.money(t.cost + t.noCostSales + t.promotions)});
        r.line({"GP% as ILSA", gpAsIlsa(t.merchandise, t.cost, t.noCostSales, t.promotions, t.discounts)});
        r.line({"No-cost retail (open rings)", ctx.money(t.noCostSales)});
        r.line({"GP% without no-cost lines", gpTrue(t.merchandise, t.cost, t.noCostSales, t.promotions, t.discounts)});
    } else {
        r.note("No cost per line on ViewTouch's checks yet: GP comes with the cost kept at the time of sale.");
    }
    if (source != "ViewTouch")
        r.note("From the imported ILSA history: not in today's drawers, X/Z or end of day.");
    return r;
}

Report departmentSales(const std::vector<GroupRow> &depts, const std::vector<GroupRow> &lottery, const ReportContext &ctx,
                       bool current)
{
    Report r;
    r.id = "ilsa-depts";
    r.title = current ? "Sales by Department (ILSA, as ILSA: items' current departments)" : "Sales by Department (ILSA)";
    r.subtitle = ctx.period;
    r.columns = {"Department"};
    r.columns.insert(r.columns.end(), kDeptColumns.begin(), kDeptColumns.end());
    Sums all;
    for (const GroupRow &g : depts) {
        Sums one;
        one.add(g.units, g.sales, g.cost, g.noCostSales, g.buydowns, g.discounts);
        all.add(g.units, g.sales, g.cost, g.noCostSales, g.buydowns, g.discounts);
        r.line(gpCells(g.key + " " + g.label, one, true, ctx));
    }
    r.total(gpCells("Merchandise", all, true, ctx));
    if (!lottery.empty()) {
        r.section("Lottery (history only: not in the totals)");
        for (const GroupRow &g : lottery) {
            std::vector<std::string> cells(kDeptColumns.size() + 1);
            cells[0] = g.key + " " + g.label;
            cells[1] = units(g.units);
            cells[2] = ctx.money(g.sales);
            r.line(std::move(cells));
        }
    }
    r.note(current ? "Departments: each item's department in ILSA now (as ILSA's own Department Sales report)."
                   : "Departments: the one each line was sold in.");
    gpNotes(r, true);
    return r;
}

Report itemMovement(const std::vector<GroupRow> &rows, const std::string &by, bool more, const ReportContext &ctx)
{
    Report r;
    r.id = "ilsa-items";
    const std::string what = by == "upc" ? "UPC" : by == "brand" ? "Brand" : by == "dept" ? "Department" : "Item";
    r.title = "Item Movement by " + what + " (ILSA)";
    r.subtitle = ctx.period;
    r.columns = {what};
    if (by == "item" || by == "upc")
        r.columns.push_back(by == "item" ? "UPC" : "Department");
    r.columns.insert(r.columns.end(), kGpColumns.begin(), kGpColumns.end());
    Sums all;
    for (const GroupRow &g : rows) {
        Sums one;
        one.add(g.units, g.sales, g.cost, g.noCostSales, Money(), Money());
        all.add(g.units, g.sales, g.cost, g.noCostSales, Money(), Money());
        std::vector<std::string> cells = gpCells(g.label.empty() ? g.key : g.label, one, false, ctx);
        if (by == "item" || by == "upc")
            cells.insert(cells.begin() + 1, g.extra);
        r.line(std::move(cells));
    }
    std::vector<std::string> total = gpCells(more ? "Shown" : "Total", all, false, ctx);
    if (by == "item" || by == "upc")
        total.insert(total.begin() + 1, "");
    r.total(std::move(total));
    if (more)
        r.note("The biggest sellers first; there are more (a shorter range, or the CSV, shows the rest).");
    r.note("Buydowns (promotions) aren't kept per item: the Departments and GP reports take them off the cost.");
    gpNotes(r, false);
    return r;
}

Report gpByPeriod(const std::vector<PeriodRow> &rows, bool byYear, const ReportContext &ctx)
{
    Report r;
    r.id = byYear ? "ilsa-gp-year" : "ilsa-gp-month";
    r.title = byYear ? "GP by Year (ILSA)" : "GP by Month (ILSA)";
    r.subtitle = ctx.period;
    r.columns = {byYear ? "Year" : "Month"};
    r.columns.insert(r.columns.end(), kDeptColumns.begin(), kDeptColumns.end());
    Sums all;
    Money lottery;
    for (const PeriodRow &p : rows) {
        Sums one;
        one.add(p.units, p.sales, p.cost, p.noCostSales, p.buydowns, p.discounts);
        all.add(p.units, p.sales, p.cost, p.noCostSales, p.buydowns, p.discounts);
        r.line(gpCells(p.period, one, true, ctx));
        lottery += p.lottery;
    }
    r.total(gpCells("Total", all, true, ctx));
    if (lottery.cents() != 0)
        r.note("Lottery sales (history only, not above): " + ctx.money(lottery));
    gpNotes(r, true);
    return r;
}

Report sideBySide(const std::map<std::string, std::int64_t> &ilsaZ, const ZTotals &imported, const ZTotals *viewtouch,
                  const ReportContext &ctx)
{
    Report r;
    r.id = "ilsa-compare";
    r.title = viewtouch ? "ILSA vs ViewTouch" : "ILSA Z vs Imported Receipts";
    r.subtitle = ctx.period;
    r.columns = {"", "ILSA Z", "Imported", "Variance"};
    if (viewtouch) {
        r.columns.push_back("ViewTouch");
        r.columns.push_back("VT variance");
    }
    // ILSA Z (empty: ILSA's Z doesn't keep it), the imported receipts, ViewTouch.
    const auto row = [&](const std::string &label, std::optional<Money> z, Money ours, std::optional<Money> vt) {
        std::vector<std::string> cells = {label, z ? ctx.money(*z) : "", ctx.money(ours),
                                          z ? ctx.money(ours - *z) : ""};
        if (viewtouch) {
            cells.push_back(vt ? ctx.money(*vt) : "");
            cells.push_back(vt && z ? ctx.money(*vt - *z) : vt ? ctx.money(*vt - ours) : "");
        }
        r.line(std::move(cells));
    };
    const auto has = [&](const std::string &k) { return ilsaZ.contains(k); };
    const auto vtOr = [&](auto get) -> std::optional<Money> {
        if (!viewtouch)
            return std::nullopt;
        return get(*viewtouch);
    };

    r.section("Sales");
    {
        std::vector<std::string> cells = {"Receipts", "", count(imported.receipts), ""};
        if (viewtouch) {
            cells.push_back(count(viewtouch->receipts));
            cells.push_back("");
        }
        r.line(std::move(cells));
    }
    row("Merchandise", std::nullopt, imported.merchandise, vtOr([](const ZTotals &v) { return v.merchandise; }));
    row("Promotions / discounts", std::nullopt, imported.promotions, vtOr([](const ZTotals &v) { return v.promotions; }));
    if (imported.entries.cents() != 0)
        row("Department entries", std::nullopt, imported.entries, std::nullopt);
    if (imported.lottery.cents() != 0)
        row("Lottery (history only)", std::nullopt, imported.lottery, std::nullopt);
    if (imported.other.cents() != 0)
        row("Other lines", std::nullopt, imported.other, std::nullopt);
    row("Net sales", std::nullopt, imported.net(), vtOr([](const ZTotals &v) { return v.net(); }));
    row("Tax", has("tax") ? std::optional<Money>(centsOf(ilsaZ, "tax")) : std::nullopt, imported.tax,
        vtOr([](const ZTotals &v) { return v.tax; }));
    row("Total with tax", std::nullopt, imported.gross(), vtOr([](const ZTotals &v) { return v.gross(); }));

    // Tenders by group: ILSA's Z types summed the way ViewTouch groups payments.
    r.section("Tenders");
    std::map<std::string, Money> zTender;
    for (const auto &[k, v] : ilsaZ)
        if (k.starts_with("tender:"))
            zTender[tenderGroup(std::atoi(k.c_str() + 7))] += Money::fromCents(v);
    std::vector<std::string> names;
    for (const auto &[n, m] : imported.tenders)
        if (std::ranges::find(names, n) == names.end())
            names.push_back(n);
    for (const auto &[n, m] : zTender)
        if (std::ranges::find(names, n) == names.end())
            names.push_back(n);
    if (viewtouch)
        for (const auto &[n, m] : viewtouch->tenders)
            if (std::ranges::find(names, n) == names.end())
                names.push_back(n);
    Money zSum, oursSum;
    for (const std::string &n : names) {
        const bool inZ = zTender.contains(n);
        row(n, inZ ? std::optional<Money>(zTender[n]) : std::optional<Money>(Money()), imported.tender(n),
            vtOr([&](const ZTotals &v) { return v.tender(n); }));
        zSum += inZ ? zTender[n] : Money();
        oursSum += imported.tender(n);
    }
    {
        Money vtSum;
        if (viewtouch)
            for (const auto &[n, m] : viewtouch->tenders)
                vtSum += m;
        std::vector<std::string> cells = {"Collected", ctx.money(zSum), ctx.money(oursSum), ctx.money(oursSum - zSum)};
        if (viewtouch) {
            cells.push_back(ctx.money(vtSum));
            cells.push_back(ctx.money(vtSum - zSum));
        }
        r.total(std::move(cells));
    }
    if (has("payouts") || imported.payouts.cents() != 0) {
        // Shown with ILSA's Z's sign (its receipts may keep them the other way round).
        const Money z = centsOf(ilsaZ, "payouts");
        Money ours = imported.payouts;
        if (z.cents() != 0 && (ours.cents() < 0) != (z.cents() < 0))
            ours = -ours;
        row("Paid outs", has("payouts") ? std::optional<Money>(z) : std::nullopt, ours, std::nullopt);
    }

    r.section("Voids and returns");
    {
        std::vector<std::string> cells = {"Voided lines", "", count(imported.voidLines), ""};
        if (viewtouch) {
            cells.push_back(count(viewtouch->voidLines));
            cells.push_back("");
        }
        r.line(std::move(cells));
    }
    row("Returned", std::nullopt, imported.returned, vtOr([](const ZTotals &v) { return v.returned; }));

    // ILSA's other Z totals as it saved them (the importer learns which of ours each one equals).
    bool first = true;
    for (const auto &[k, v] : ilsaZ) {
        if (!k.starts_with("totals:") || v == 0)
            continue;
        if (first) {
            r.section("ILSA's Z totals, as saved");
            first = false;
        }
        row(k.substr(7), Money::fromCents(v), Money(), std::nullopt);
        r.rows.back().cells[2] = "";
        r.rows.back().cells[3] = "";
    }
    r.note("Variance: imported - ILSA's Z (0.00 everywhere = the importer matches ILSA's own Z).");
    if (viewtouch)
        r.note("VT variance: ViewTouch's own checks on the same days (the parallel beta) - ILSA's Z (or - the imported receipts where ILSA's Z doesn't keep it).");
    return r;
}

Report differences(const std::vector<Variance> &list, const ReportContext &ctx)
{
    Report r;
    r.id = "ilsa-differences";
    r.title = "Known ILSA Differences";
    r.subtitle = ctx.period;
    r.columns = {"Day", "Z", "What", "ILSA Z", "Imported", "Difference"};
    std::set<std::string> days;
    for (const Variance &v : list) {
        const bool n = v.what.ends_with("(count)");
        const auto show = [&](std::int64_t c) { return n ? count(c) : ctx.money(Money::fromCents(c)); };
        r.line({v.day, count(v.z), v.what, show(v.ilsa), show(v.ours), show(v.ours - v.ilsa)});
        days.insert(v.day);
    }
    r.total({count(std::int64_t(days.size())) + " day(s)", "", count(std::int64_t(list.size())) + " difference(s)", "", "", ""});
    if (list.empty())
        r.note("None: every imported day matches ILSA's own Z on tax, tenders and paid outs.");
    else
        r.note("Days where ILSA's own Z and its receipts disagree (crash days, a mistyped amount): the receipts are kept as they are.");
    return r;
}

std::vector<std::pair<std::string, std::string>> receiptRows(const Receipt &rc, bool withDob)
{
    std::vector<std::pair<std::string, std::string>> out;
    const auto rule = [&] { out.emplace_back("-", "-"); };
    out.emplace_back("Receipt " + std::to_string(rc.number), rc.at.substr(0, 16));
    out.emplace_back("Register " + std::to_string(rc.reg), "Cashier " + std::to_string(rc.cashier));
    rule();
    Money subtotal;
    for (const ReceiptLine &l : rc.lines) {
        std::string name = l.kind == "promo" ? "  " + l.name : l.name;
        const double q = std::abs(l.qty);
        if (l.kind == "item" && std::abs(q - 1) > 0.0005)
            name = units(q) + " " + name;
        if (l.voided)
            name += " VOID";
        else if (l.returned)
            name += " RETURN";
        out.emplace_back(name, plain(l.amount));
        if (l.kind == "item" && l.upc.size() >= 8)
            out.emplace_back("   UPC " + l.upc, "");
        subtotal += l.amount;
    }
    rule();
    out.emplace_back("Subtotal", plain(subtotal));
    if (rc.tax.cents() != 0)
        out.emplace_back("Tax", plain(rc.tax));
    out.emplace_back("TOTAL", plain(rc.total));
    Money change;
    for (const ReceiptTender &t : rc.tenders) {
        out.emplace_back(t.name, plain(t.amount));
        change += t.change;
    }
    if (change.cents() != 0)
        out.emplace_back("Change", plain(change));
    if (rc.ageVerified > 0) {
        out.emplace_back("", "");
        out.emplace_back("Age checked: " + std::to_string(rc.ageVerified) + "+", "");
        if (withDob && !rc.dob.empty())
            out.emplace_back("Date of birth: " + rc.dob, "");
    }
    if (rc.mode != 0) {
        out.emplace_back("", "");
        out.emplace_back("(ILSA mode " + std::to_string(rc.mode) + ": not counted as a sale)", "");
    }
    out.emplace_back("", "");
    out.emplace_back("ILSA receipt (history, read-only)", "");
    if (!rc.source.empty())
        out.emplace_back("From " + rc.source, "");
    return out;
}

std::string receiptText(const Receipt &rc, bool withDob, int width)
{
    std::string text;
    for (const auto &[left, right] : receiptRows(rc, withDob)) {
        if (left == "-" && right == "-")
            text += std::string(std::size_t(width), '-');
        else if (left.starts_with("ILSA receipt") || left.starts_with("From "))
            text += centered(left, width);
        else
            text += fit(left, right, width);
        text += "\n";
    }
    return text;
}

Report receiptReport(const Receipt &rc, bool withDob)
{
    Report r;
    r.id = "ilsa-receipt";
    r.title = "ILSA receipt #" + std::to_string(rc.number);
    r.subtitle = withDob ? "Reprint, store's copy" : "Reprint";
    for (const auto &[left, right] : receiptRows(rc, withDob)) {
        if (left == "-" && right == "-")
            r.note(std::string(32, '-'));
        else if (right.empty())
            r.note(left.empty() ? " " : left);
        else
            r.line({left, right});
    }
    return r;
}

} // namespace vt::core::legacy
