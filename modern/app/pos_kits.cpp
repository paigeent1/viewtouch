// PosService: sampler kits (owner 2026-11-27, after ILSA's KIT table). Menu Builder > Mass Updates > Sampler
// Kits (KitsPanel.qml): the kits (items with contents), one being made or changed: the kit item (made first like
// any item, with its UPC and kit price), its contents by UPC and quantity (Find, a UPC, the Mass Updates ticks),
// "Print contents on the receipt"; Apply saves (the price book's permission: an admin, or an admin's PIN) and
// sets the kit's cost to its contents'; "Not a kit" takes its contents off (the item stays). When a kit sells,
// its contents leave stock (retailStockSold) and the sale keeps what was in it (stampNewLine). Rules: core/kit.hh.

#include "app/pos_json.hh"
#include "app/pos_service.hh"
#include "core/buydown.hh"
#include "core/kit.hh"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

constexpr int kMostFound = 200;

std::int64_t costCents(const MenuItem &m)
{
    for (const RecipeLine &rl : m.recipe)
        if (rl.ingredientId == "__cost__" && rl.quantity > 0.0)
            return std::llround(rl.quantity * 100.0);
    return 0;
}

QString qtyText(double q)
{
    return q == std::floor(q) ? QString::number(qint64(q)) : QString::number(q, 'f', 2);
}

} // namespace

bool PosService::kitsOpen(bool on)
{
    if (on && !sysDiscMayLook())
        return false;
    kit_ = {};
    kit_.open = on;
    emit sessionChanged();
    return true;
}

bool PosService::kitEdit(const QString &itemId)
{
    if (!sysDiscMayLook())
        return false;
    const MenuItem *m = findItem(itemId);
    if (!m || m->isModifier)
        return fail(tr("That item is gone."));
    KitView::Draft d;
    d.itemId = m->id;
    d.parts = kit::partsOf(*m);
    d.print = kit::printsContents(*m);
    d.isNew = !kit::isKit(*m);
    kit_.draft = d;
    kit_.found.clear();
    kit_.message = d.isNew ? tr("Add what's in %1, then Apply.").arg(qs(m->name)) : QString();
    emit sessionChanged();
    return true;
}

bool PosService::kitFind(const QString &text)
{
    const QString t = text.trimmed();
    kit_.found.clear();
    if (t.size() < 2) {
        emit sessionChanged();
        return t.isEmpty() ? true : fail(tr("Type at least two letters or numbers."));
    }
    const std::string digits = buydown::upcKey(ss(t));
    const bool code = !digits.empty() && digits.size() + 2 >= std::size_t(t.size());
    const auto scopes = scopesNow();
    std::vector<const MenuItem *> hits;
    for (const MenuItem &m : s_->menu) {
        if (m.isModifier || !inPriceBook(m, scopes))
            continue;
        bool hit = qs(m.name).contains(t, Qt::CaseInsensitive);
        if (!hit && code)
            hit = std::ranges::any_of(m.barcodes, [&](const std::string &b) { return buydown::sameUpc(b, digits); });
        if (hit)
            hits.push_back(&m);
    }
    std::ranges::sort(hits, [](const MenuItem *a, const MenuItem *b) { return a->name < b->name; });
    for (const MenuItem *m : hits) {
        if (kit_.found.size() >= kMostFound)
            break;
        const std::optional<double> onHand = retailOnHand(m->id);
        kit_.found.append(QVariantMap{{u"id"_s, qs(m->id)}, {u"upc"_s, m->barcodes.empty() ? QString() : qs(m->barcodes.front())},
                                      {u"name"_s, qs(m->name)}, {u"price"_s, format(m->price)}, {u"isKit"_s, kit::isKit(*m)},
                                      {u"onHand"_s, onHand ? qtyText(*onHand) : QString()}});
    }
    kit_.message = hits.empty() ? tr("Nothing matches %1.").arg(t)
                   : int(hits.size()) > kMostFound ? tr("%1 match; the first %2 are shown.").arg(hits.size()).arg(kMostFound)
                                                   : QString();
    emit sessionChanged();
    return true;
}

bool PosService::kitAddParts(const QStringList &codes)
{
    if (!kit_.draft)
        return fail(tr("Choose the kit item first."));
    KitView::Draft &d = *kit_.draft;
    QStringList missing;
    int added = 0;
    for (const QString &c : codes) {
        const MenuItem *m = c.trimmed().isEmpty() ? nullptr : findItem(c.trimmed());
        if (!m || m->isModifier) {
            if (!c.trimmed().isEmpty())
                missing << c.trimmed();
            continue;
        }
        if (m->id == d.itemId)
            return fail(tr("A kit can't hold itself."));
        if (kit::isKit(*m))
            return fail(tr("%1 is a kit itself: add what's in it instead.").arg(qs(m->name)));
        if (m->barcodes.empty())
            return fail(tr("%1 has no UPC: give it one in the Menu Builder first.").arg(qs(m->name)));
        const auto it = std::ranges::find_if(d.parts, [&](const kit::Part &p) {
            return std::ranges::any_of(m->barcodes, [&](const std::string &b) { return buydown::sameUpc(b, p.upc); });
        });
        if (it != d.parts.end())
            it->qty += 1;
        else if (int(d.parts.size()) < kit::kMostParts)
            d.parts.push_back({m->barcodes.front(), 1});
        ++added;
    }
    kit_.message = tr("%1 added.").arg(added);
    if (!missing.isEmpty())
        kit_.message += u"  "_s + tr("Not found: %1").arg(missing.join(u", "_s));
    emit sessionChanged();
    return true;
}

bool PosService::kitAddChosen()
{
    const QStringList ids = massChosenIds();
    if (ids.isEmpty())
        return fail(tr("Tick items in Mass Updates first."));
    return kitAddParts(ids);
}

bool PosService::kitSetQty(const QString &upc, const QString &qty)
{
    if (!kit_.draft)
        return fail(tr("Choose the kit item first."));
    bool ok = false;
    const double q = qty.trimmed().toDouble(&ok);
    if (!ok || q < 0 || q > 999)
        return fail(tr("Type how many, like 2."));
    auto &parts = kit_.draft->parts;
    const auto it = std::ranges::find(parts, ss(upc), &kit::Part::upc);
    if (it == parts.end())
        return fail(tr("That isn't in the kit."));
    if (q == 0)
        parts.erase(it);
    else
        it->qty = q;
    kit_.message.clear();
    emit sessionChanged();
    return true;
}

bool PosService::kitSetPrint(bool on)
{
    if (!kit_.draft)
        return fail(tr("Choose the kit item first."));
    kit_.draft->print = on;
    emit sessionChanged();
    return true;
}

bool PosService::kitApply()
{
    if (!kit_.draft)
        return fail(tr("Choose the kit item first."));
    const KitView::Draft d = *kit_.draft;
    if (d.parts.empty())
        return fail(tr("Add what's in the kit (or use Not a kit)."));
    const auto pos = std::ranges::find(s_->menu, d.itemId, &MenuItem::id);
    if (pos == s_->menu.end())
        return fail(tr("That item is gone."));
    if (!requirePriceBook([] { return true; }))
        return false;
    MenuItem &m = *pos;
    // Its cost: the contents' (when every one has a cost).
    std::int64_t cost = 0;
    bool allCosted = true;
    QStringList names;
    for (const kit::Part &p : d.parts) {
        const MenuItem *part = findItem(qs(p.upc));
        const std::int64_t c = part ? costCents(*part) : 0;
        allCosted = allCosted && c > 0;
        cost += std::llround(double(c) * p.qty);
        names << qs(kit::partText(p.qty, part ? part->name : p.upc));
    }
    kit::setParts(m, d.parts, d.print);
    if (allCosted) {
        std::erase_if(m.recipe, [](const RecipeLine &rl) { return rl.ingredientId == "__cost__"; });
        m.recipe.push_back({"__cost__", double(cost) / 100.0});
    }
    // A kit is retail (its contents are).
    if (!itemIsRetail(m))
        m.recipe.insert(m.recipe.begin(), {"__retail__", 1.0});
    if (s_->sink)
        s_->sink->saveMenuItem(m, int(pos - s_->menu.begin()));
    logChange(tr("Sampler kits"), qs(m.name), d.isNew ? tr("Made a kit") : tr("Contents"), {}, names.join(u", "_s));
    kit_.draft.reset();
    kit_.found.clear();
    kit_.message = allCosted ? tr("%1 saved; its cost is now %2.").arg(qs(m.name), format(Money::fromCents(cost)))
                             : tr("%1 saved. Some of its contents have no cost, so the kit's cost was left as it was.").arg(qs(m.name));
    emit notice(kit_.message);
    emit sessionChanged();
    return true;
}

bool PosService::kitAbort()
{
    kit_.draft.reset();
    kit_.found.clear();
    kit_.message.clear();
    emit sessionChanged();
    return true;
}

bool PosService::kitUnmake(const QString &itemId)
{
    if (!sysDiscMayLook())
        return false;
    const auto pos = std::ranges::find(s_->menu, ss(itemId), &MenuItem::id);
    if (pos == s_->menu.end() || !kit::isKit(*pos))
        return fail(tr("That isn't a kit."));
    if (!requirePriceBook([] { return true; }))
        return false;
    kit::setParts(*pos, {}, false);
    if (s_->sink)
        s_->sink->saveMenuItem(*pos, int(pos - s_->menu.begin()));
    logChange(tr("Sampler kits"), qs(pos->name), tr("Not a kit"), {}, {});
    if (kit_.draft && kit_.draft->itemId == ss(itemId))
        kit_.draft.reset();
    kit_.message = tr("%1 is no longer a kit (the item stays).").arg(qs(pos->name));
    emit sessionChanged();
    return true;
}

QVariantMap PosService::kits() const
{
    if (!kit_.open || !user() || !permissionsOf(*user()).contains(perm::Manager))
        return {};
    const auto partRows = [&](const std::vector<kit::Part> &parts, Money &value, Money &cost, bool &allCosted) {
        QVariantList rows;
        for (const kit::Part &p : parts) {
            const MenuItem *m = findItem(qs(p.upc));
            const std::optional<double> onHand = m ? retailOnHand(m->id) : std::nullopt;
            if (m) {
                value += Money::fromCents(std::llround(double(m->price.cents()) * p.qty));
                const std::int64_t c = costCents(*m);
                allCosted = allCosted && c > 0;
                cost += Money::fromCents(std::llround(double(c) * p.qty));
            } else {
                allCosted = false;
            }
            rows.append(QVariantMap{{u"upc"_s, qs(p.upc)}, {u"qty"_s, qtyText(p.qty)}, {u"name"_s, m ? qs(m->name) : tr("(not in this store)")},
                                    {u"found"_s, m != nullptr}, {u"price"_s, m ? format(m->price) : QString()},
                                    {u"onHand"_s, onHand ? qtyText(*onHand) : QString()}});
        }
        return rows;
    };
    QVariantList list;
    for (const MenuItem &m : s_->menu) {
        if (!kit::isKit(m))
            continue;
        Money value, cost;
        bool allCosted = true;
        const QVariantList rows = partRows(kit::partsOf(m), value, cost, allCosted);
        list.append(QVariantMap{{u"id"_s, qs(m.id)}, {u"name"_s, qs(m.name)}, {u"upc"_s, m.barcodes.empty() ? QString() : qs(m.barcodes.front())},
                                {u"price"_s, format(m.price)}, {u"parts"_s, int(rows.size())}, {u"value"_s, format(value)},
                                {u"cost"_s, allCosted ? format(cost) : QString()}, {u"print"_s, kit::printsContents(m)}});
    }
    std::ranges::sort(list, [](const QVariant &a, const QVariant &b) { return a.toMap()[u"name"_s].toString() < b.toMap()[u"name"_s].toString(); });
    QVariantMap out{{u"active"_s, true}, {u"canApply"_s, mayChangePriceBook()}, {u"list"_s, list}, {u"message"_s, kit_.message},
                    {u"found"_s, kit_.found}, {u"chosenInMass"_s, int(massChosenIds().size())}};
    if (kit_.draft) {
        const MenuItem *m = findItem(qs(kit_.draft->itemId));
        Money value, cost;
        bool allCosted = true;
        const QVariantList rows = partRows(kit_.draft->parts, value, cost, allCosted);
        out.insert(u"draft"_s, QVariantMap{{u"id"_s, qs(kit_.draft->itemId)}, {u"name"_s, m ? qs(m->name) : QString()},
                                           {u"upc"_s, m && !m->barcodes.empty() ? qs(m->barcodes.front()) : QString()},
                                           {u"price"_s, m ? format(m->price) : QString()}, {u"isNew"_s, kit_.draft->isNew},
                                           {u"print"_s, kit_.draft->print}, {u"parts"_s, rows}, {u"value"_s, format(value)},
                                           {u"cost"_s, allCosted ? format(cost) : QString()}});
    }
    return out;
}

} // namespace vt::app
