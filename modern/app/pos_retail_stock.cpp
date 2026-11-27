// PosService: retail on hand for a box / carton and its single (owner 2026-11-27, "I sell most by carton; pack
// sales should break cartons automatically"). A family (the single's Menu Builder link, __parent__:<box UPC>
// with its pack size) keeps two counts: sealed boxes / cartons on the box's own item (retail_stock row) and loose
// singles on the single's item. The rules are core/retail_stock.hh: a single sold takes one loose and opens a
// sealed one by itself only when none is loose; a box sold takes one sealed (or a box's worth loose when none is
// sealed); Open boxes moves sealed to loose (the humidor). Shown everywhere as "4 box 27 ea". Items without a
// link keep their own simple count (as before). Manual: chapter 35, "Cartons, boxes and loose singles".

#include "app/i18n.hh"
#include "app/pos_json.hh"
#include "app/pos_service.hh"
#include "core/mass_update.hh"
#include "core/retail_stock.hh"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>

using namespace Qt::StringLiterals;
using namespace vt::core;
namespace mass = vt::core::mass;

namespace vt::app {

namespace {

constexpr const char *kMigrated = "__families_v2__";   // a retail_stock row: the counts were settled once

std::int64_t costCentsOf(const MenuItem &m)
{
    for (const RecipeLine &rl : m.recipe)
        if (rl.ingredientId == "__cost__" && rl.quantity > 0.0)
            return std::llround(rl.quantity * 100.0);
    return 0;
}

QString shortUnit(const QString &s) { return s.trimmed().left(8); }

} // namespace

// Every family on the menu: the single's link to its box (the first single linked to a box wins).
std::vector<RetailFamily> retailFamiliesOf(const std::vector<MenuItem> &menu, const std::vector<MenuCategory> &categories)
{
    std::vector<RetailFamily> out;
    mass::UpcIndex upc;
    upc.build(menu.size(), [&](std::size_t i) { return menu[i].barcodes; });
    std::set<std::string> parents;
    for (const MenuItem &m : menu) {
        if (m.isModifier)
            continue;
        std::string units;
        const RecipeLine *link = nullptr;
        for (const RecipeLine &rl : m.recipe) {
            if (rl.ingredientId.starts_with("__parent__:") && rl.quantity >= 1)
                link = &rl;
            if (rl.ingredientId.starts_with("__units__:"))
                units = rl.ingredientId.substr(10);
        }
        if (!link)
            continue;
        const std::int64_t at = upc.find(link->ingredientId.substr(11));
        if (at < 0)
            continue;
        const MenuItem &p = menu[std::size_t(at)];
        if (p.id == m.id || !parents.insert(p.id).second)
            continue;
        RetailFamily f;
        f.parentId = p.id;
        f.singleId = m.id;
        f.packSize = int(std::lround(link->quantity));
        std::string category;
        for (const MenuCategory &c : categories)
            if (c.id == p.family || c.id == m.family)
                category += c.name + " ";
        const bool box = stock::looksLikeBox(p.name, category);
        f.parentUnit = box ? u"box"_s : u"ctn"_s;
        f.singleUnit = box ? u"ea"_s : u"pk"_s;
        f.unitsSet = !units.empty();
        if (const auto slash = units.find('/'); slash != std::string::npos) {
            if (const QString a = shortUnit(qs(units.substr(0, slash))); !a.isEmpty())
                f.parentUnit = a;
            if (const QString b = shortUnit(qs(units.substr(slash + 1))); !b.isEmpty())
                f.singleUnit = b;
        }
        out.push_back(std::move(f));
    }
    return out;
}

// --- once: the counts kept before a family had two (logged) -------------------------------------------

void PosShared::migrateRetailFamilies()
{
    if (retailStock.erase(kMigrated) > 0)
        return;   // done before
    for (const RetailFamily &f : retailFamiliesOf(menu, categories())) {
        const auto ps = retailStock.find(f.parentId), ls = retailStock.find(f.singleId);
        if (ps == retailStock.end() && ls == retailStock.end())
            continue;
        const stock::Counts was{ps == retailStock.end() ? 0 : ps->second, ls == retailStock.end() ? 0 : ls->second};
        const stock::Counts settled = stock::settle(was, f.packSize, f.parentUnit != u"box");
        if (settled == was)
            continue;
        retailStock[f.parentId] = settled.sealed;
        retailStock[f.singleId] = settled.loose;
        std::string name;
        for (const MenuItem &m : menu)
            if (m.id == f.parentId)
                name = m.name;
        core::Change c{++lastChangeId, now(), day.id, "ViewTouch", ss(PosService::tr("Inventory")), name,
                 ss(PosService::tr("On hand: sealed and loose apart")),
                 stock::text(was, ss(f.parentUnit), ss(f.singleUnit)), stock::text(settled, ss(f.parentUnit), ss(f.singleUnit))};
        changesToday.push_back(c);
        qInfo().noquote() << "retail stock:" << qs(name) << qs(c.before) << "->" << qs(c.after);
        if (sink) {
            sink->saveRetailStock(f.parentId, settled.sealed);
            sink->saveRetailStock(f.singleId, settled.loose);
            sink->saveChange(c);
        }
    }
    if (sink)
        sink->saveRetailStock(kMigrated, 1);
}

// --- the families -------------------------------------------------------------------------------------

struct PosService::RetailFamilyIndex {
    std::uint64_t shared = 0;
    int revision = -1;
    std::size_t size = 0;
    std::vector<RetailFamily> families;
    std::unordered_map<std::string, std::size_t> of;   // a parent's or single's id -> its family
};

const PosService::RetailFamilyIndex &PosService::retailFamilyIndex() const
{
    static RetailFamilyIndex x;
    if (x.shared == s_->serial && x.revision == s_->adminRevision && x.size == s_->menu.size())
        return x;
    x.shared = s_->serial;
    x.revision = s_->adminRevision;
    x.size = s_->menu.size();
    x.families = retailFamiliesOf(s_->menu, s_->categories());
    x.of.clear();
    for (std::size_t i = 0; i < x.families.size(); ++i) {
        x.of.try_emplace(x.families[i].parentId, i);
        x.of.try_emplace(x.families[i].singleId, i);
    }
    return x;
}

const RetailFamily *PosService::retailFamilyOf(const std::string &itemId) const
{
    const RetailFamilyIndex &x = retailFamilyIndex();
    const auto it = x.of.find(itemId);
    return it == x.of.end() ? nullptr : &x.families[it->second];
}

stock::Counts PosService::retailCounts(const RetailFamily &f) const
{
    return {retailOnHand(f.parentId).value_or(0), retailOnHand(f.singleId).value_or(0)};
}

bool PosService::retailTracked(const std::string &itemId) const
{
    if (s_->retailStock.contains(itemId))
        return true;
    const RetailFamily *f = retailFamilyOf(itemId);
    return f && (s_->retailStock.contains(f->parentId) || s_->retailStock.contains(f->singleId));
}

void PosService::setRetailCounts(const RetailFamily &f, const stock::Counts &c)
{
    for (const auto &[id, v] : {std::pair{f.parentId, c.sealed}, {f.singleId, c.loose}}) {
        s_->retailStock[id] = stock::tidy(v);
        if (s_->sink)
            s_->sink->saveRetailStock(id, s_->retailStock[id]);
    }
}

QString PosService::retailFamilyText(const RetailFamily &f, const stock::Counts &c) const
{
    return qs(stock::text(c, ss(f.parentUnit), ss(f.singleUnit)));
}

QString PosService::retailOnHandText(const std::string &itemId) const
{
    if (!retailTracked(itemId))
        return {};
    if (const RetailFamily *f = retailFamilyOf(itemId))
        return retailFamilyText(*f, retailCounts(*f));
    return qs(stock::number(retailOnHand(itemId).value_or(0)));
}

QString PosService::retailUnitOf(const std::string &itemId) const
{
    if (const RetailFamily *f = retailFamilyOf(itemId))
        return itemId == f->parentId ? f->parentUnit : f->singleUnit;
    return {};
}

// Out of the on hand (a sale, a transfer sent): `qty` of the item as it sells (a carton: cartons). qty < 0:
// back in (a void, a return). A family by its rules; logged when a sealed one was opened by itself, a box was
// taken out of loose, or a box sold with none sealed.
void PosService::retailTake(const std::string &itemId, double qty)
{
    if (itemId.empty() || qty == 0)
        return;
    const RetailFamily *f = retailFamilyOf(itemId);
    if (!f) {
        addRetailStock(itemId, -qty);
        return;
    }
    const RetailFamily fam = *f;
    const stock::Counts was = retailCounts(fam);
    const bool parent = itemId == fam.parentId;
    const stock::Take t = parent ? stock::takeParents(was, qty, fam.packSize) : stock::takeSingles(was, qty, fam.packSize);
    setRetailCounts(fam, t.after);
    const MenuItem *p = findItem(qs(fam.parentId));
    const QString name = p ? qs(p->name) : qs(fam.parentId);
    const QString before = retailFamilyText(fam, was), after = retailFamilyText(fam, t.after);
    if (t.opened > 0)
        logChange(tr("Inventory"), name, tr("%n opened by itself (a single sold, none loose)", "", t.opened), before, after);
    if (t.fromLoose > 0)
        logChange(tr("Inventory"), name, tr("%n sold from loose (none sealed)", "", t.fromLoose), before, after);
    if (t.short_ > 0)
        logChange(tr("Inventory"), name, tr("%n sold with none on hand (below 0)", "", t.short_), before, after);
}

// --- the count and Open boxes (Menu Builder's item card; managers) --------------------------------------

bool PosService::retailSetCount(const QString &itemId, double sealed, double loose)
{
    if (!user() || !can(u"manager"_s))
        return fail(tr("Counting stock is for managers."));
    const MenuItem *m = findItem(itemId);
    if (!m || !itemIsRetail(*m))
        return fail(tr("No retail item %1.").arg(itemId));
    if (!(sealed >= 0) || !(loose >= 0) || sealed > 1e6 || loose > 1e6)
        return fail(tr("Type how many, like 0, 1 or 10."));
    if (const RetailFamily *f = retailFamilyOf(m->id)) {
        const RetailFamily fam = *f;
        const QString before = retailTracked(m->id) ? retailOnHandText(m->id) : tr("not kept");
        setRetailCounts(fam, {sealed, loose});
        const MenuItem *p = findItem(qs(fam.parentId));
        logChange(tr("Inventory"), p ? qs(p->name) : qs(m->name), tr("Counted"), before, retailOnHandText(m->id));
    } else {
        const QString before = retailTracked(m->id) ? retailOnHandText(m->id) : tr("not kept");
        s_->retailStock[m->id] = stock::tidy(sealed);
        if (s_->sink)
            s_->sink->saveRetailStock(m->id, s_->retailStock[m->id]);
        logChange(tr("Inventory"), qs(m->name), tr("Counted"), before, retailOnHandText(m->id));
    }
    retailStockMessage_ = tr("Counted: %1.").arg(retailOnHandText(m->id));
    emit adminChanged();
    emit s_->adminChanged();
    return true;
}

bool PosService::retailOpenParents(const QString &itemId, int count)
{
    if (!user() || !can(u"manager"_s))
        return fail(tr("Counting stock is for managers."));
    const MenuItem *m = findItem(itemId);
    const RetailFamily *f = m ? retailFamilyOf(m->id) : nullptr;
    if (!f)
        return fail(tr("Only a box or carton with its single linked (Parent Master UPC) can be opened."));
    const RetailFamily fam = *f;
    stock::Counts c = retailCounts(fam);
    const stock::Counts was = c;
    if (!stock::openParents(c, count, fam.packSize))
        return fail(count < 1 ? tr("Type how many to open, like 1 or 2.")
                              : tr("Only %1 sealed: count them first.").arg(retailFamilyText(fam, {was.sealed, 0})));
    setRetailCounts(fam, c);
    const MenuItem *p = findItem(qs(fam.parentId));
    logChange(tr("Inventory"), p ? qs(p->name) : qs(m->name), tr("Opened %1 %2").arg(count).arg(fam.parentUnit),
              retailFamilyText(fam, was), retailFamilyText(fam, c));
    retailStockMessage_ = tr("Opened %1 %2: %3.").arg(count).arg(fam.parentUnit, retailFamilyText(fam, c));
    emit adminChanged();
    emit s_->adminChanged();
    return true;
}

bool PosService::retailStockShow(const QString &itemId)
{
    if (retailStockItem_ != itemId)
        retailStockMessage_.clear();
    retailStockItem_ = itemId;
    emit adminChanged();
    return true;
}

// The item card's stock: {id, kept, family, text, sealed, loose, parentUnit, singleUnit, packSize, parentName,
// singleName, value, message}.
QVariantMap PosService::retailStockCard() const
{
    if (retailStockItem_.isEmpty() || !user() || !can(u"manager"_s))
        return {};
    const MenuItem *m = findItem(retailStockItem_);
    if (!m || !itemIsRetail(*m))
        return {};
    QVariantMap r{{u"id"_s, qs(m->id)}, {u"kept"_s, retailTracked(m->id)}, {u"text"_s, retailOnHandText(m->id)},
                  {u"message"_s, retailStockMessage_}};
    if (const RetailFamily *f = retailFamilyOf(m->id)) {
        const stock::Counts c = retailCounts(*f);
        const MenuItem *p = findItem(qs(f->parentId));
        const MenuItem *s = findItem(qs(f->singleId));
        r.insert(QVariantMap{{u"family"_s, true}, {u"sealed"_s, c.sealed}, {u"loose"_s, c.loose}, {u"parentUnit"_s, f->parentUnit},
                  {u"singleUnit"_s, f->singleUnit}, {u"packSize"_s, f->packSize},
                  {u"parentName"_s, p ? qs(p->name) : QString()}, {u"singleName"_s, s ? qs(s->name) : QString()},
                  {u"value"_s, format(Money::fromCents(std::llround(
                                   stock::value(c, p ? double(costCentsOf(*p)) : 0, s ? double(costCentsOf(*s)) : 0))))}});
        if (!r.value(u"kept"_s).toBool())
            r.insert(u"text"_s, QString());
    } else {
        const double n = retailOnHand(m->id).value_or(0);
        r.insert(QVariantMap{{u"family"_s, false}, {u"sealed"_s, n}, {u"loose"_s, 0},
                  {u"value"_s, format(Money::fromCents(std::llround(std::max(0.0, n) * double(costCentsOf(*m)))))}});
    }
    return r;
}

} // namespace vt::app
