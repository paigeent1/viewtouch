// PosService: the bar menu (owner 2026-11-21, HCA). The bar's order screens show one button per bottle (its
// pours grouped: the ones Link pours to bottles tied to the same bottle, else the same name with a size after
// it), a pour picker (each size with its price, then free notes: Neat, Rocks, Up...), and after a spirit is
// rung a mixer from the store's Mixers modifier group (priced there; added to the drink as a choice, so it
// prints and charges). Nothing here is store-specific: the groups, categories and ABV come from the data and
// Store Settings -> Bar menu. A pour rung here is rung exactly as addItem rings it (stock, standard drinks,
// tax-included prices unchanged).

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

QString amountText(double v)
{
    return QString::number(v, 'g', 6);
}

QStringList namesOf(const QVariantList &list)
{
    QStringList out;
    for (const QVariant &v : list) {
        const QString n = v.toString().trimmed();
        if (!n.isEmpty() && !out.contains(n))
            out << n;
    }
    return out;
}

} // namespace

bool PosService::barItem(const MenuItem &m)
{
    if (m.isModifier)
        return false;
    return abvOf(m) > 0 || pourOzOf(m) > 0 || m.taxClass == TaxClass::Alcohol
           || QString::fromStdString(m.printer).compare(u"bar"_s, Qt::CaseInsensitive) == 0;
}

bool PosService::seedBarGroups(PosSettings &settings)
{
    if (settings.barMenu.groupsSeeded)
        return false;
    settings.barMenu.groupsSeeded = true;
    // Names only, at no charge: the store sets its own prices (Manager -> Modifiers).
    const auto give = [&](const std::string &id, const std::string &name, int max, std::initializer_list<const char *> names) {
        if (settings.modifierGroup(id))
            return;
        ModifierGroup g;
        g.id = id;
        g.name = name;
        g.min = 0;
        g.max = max;
        for (const char *n : names) {
            ModifierOption o;
            o.name = n;
            g.options.push_back(o);
        }
        settings.modifierGroups.push_back(g);
    };
    give(settings.barMenu.mixerGroup.empty() ? std::string("mixers") : settings.barMenu.mixerGroup, "Mixers", 1,
         {"Cola", "Diet Cola", "Ginger Ale", "Soda", "Tonic", "Cranberry", "Orange Juice", "Pineapple"});
    give(settings.barMenu.notesGroup.empty() ? std::string("bar-notes") : settings.barMenu.notesGroup, "Bar notes", 0,
         {"Neat", "Rocks", "Up", "Twist", "Splash"});
    return true;
}

bool PosService::mixerWanted(const MenuItem &item) const
{
    if (item.isModifier || selfOrder_)
        return false;
    const PosSettings::BarMenu &bm = s_->settings.barMenu;
    const ModifierGroup *g = bm.mixerGroup.empty() ? nullptr : s_->settings.modifierGroup(bm.mixerGroup);
    if (!g || g->options.empty())
        return false;   // no mixers set up: never asked
    if (std::ranges::find(item.modifierGroups, g->id) != item.modifierGroups.end())
        return false;   // its own choices already ask for one
    if (!bm.mixerCategories.empty())
        return std::ranges::find(bm.mixerCategories, item.family) != bm.mixerCategories.end();
    return abvOf(item) > 0 && abvOf(item) >= bm.mixerMinAbv;
}

std::map<std::string, PosService::PourOf> PosService::pourGroups() const
{
    std::map<std::string, const Ingredient *> bottles;
    for (const Ingredient &g : s_->ingredients)
        if (g.kind == "bottle")
            bottles[g.id] = &g;
    std::map<std::string, PourOf> out;
    std::map<QString, int> count;
    for (const MenuItem &m : s_->menu) {
        if (!barItem(m))
            continue;
        const DrinkSize d = drinkSizeOf(qs(m.name));
        PourOf p;
        // Linked (Manager -> Bar & Kegs -> Link pours to bottles): the bottle it pours from.
        for (const RecipeLine &rl : m.recipe)
            if (const auto it = bottles.find(rl.ingredientId); it != bottles.end()) {
                p.key = u"g:"_s + qs(m.family) + u'|' + qs(it->first);
                p.bottle = qs(it->second->name);
                break;
            }
        // Not linked: the same name with a size after it (Tito's 1.25oz, Tito's 2oz).
        if (p.key.isEmpty()) {
            if (d.size.isEmpty())
                continue;
            p.key = u"n:"_s + qs(m.family) + u'|' + d.base.toLower();
            p.bottle = d.base;
        }
        if (d.size == u"oz")
            p.pour = amountText(d.value) + u" oz"_s;
        else if (d.size == u"ml")
            p.pour = amountText(d.value) + u"ml"_s;
        else if (d.size == u"glass")
            p.pour = tr("Glass");
        else if (d.size == u"bottle")
            p.pour = tr("Bottle");
        else if (pourOzOf(m) > 0)
            p.pour = amountText(pourOzOf(m)) + u" oz"_s;
        else
            p.pour = qs(m.name);
        ++count[p.key];
        out[m.id] = p;
    }
    for (auto &[id, p] : out)
        if (count[p.key] < 2)
            p.key.clear();   // one pour of its bottle: its own button, rung at once
    return out;
}

QVariantMap PosService::mixerAsk() const
{
    const Check *c = currentCheck();
    const OrderLine *l = c && mixerLine_ ? c->line(mixerLine_) : nullptr;
    const PosSettings::BarMenu &bm = s_->settings.barMenu;
    const ModifierGroup *g = bm.mixerGroup.empty() ? nullptr : s_->settings.modifierGroup(bm.mixerGroup);
    if (!l || l->sent || l->voided || !g || g->options.empty()) {
        // The notes the pour picker offers (Neat, Rocks...), with nothing waiting.
        QVariantList notes;
        if (const ModifierGroup *n = bm.notesGroup.empty() ? nullptr : s_->settings.modifierGroup(bm.notesGroup))
            for (const ModifierOption &o : n->options)
                notes.append(QVariantMap{{u"name"_s, qs(o.name)}, {u"price"_s, o.price.cents() ? format(o.price) : QString()},
                                         {u"chosen"_s, false}});
        return {{u"active"_s, false}, {u"notes"_s, notes}};
    }
    QString chosen;
    for (const Modifier &m : l->modifiers)
        if (m.group == g->id)
            chosen = qs(m.name);
    QVariantList mixers;
    for (int i = 0; i < int(g->options.size()); ++i) {
        const ModifierOption &o = g->options[i];
        const MenuItem *linked = o.itemId.empty() ? nullptr : findItem(qs(o.itemId));
        const Money price = optionPrice(o, *l);
        mixers.append(QVariantMap{{u"index"_s, i}, {u"name"_s, qs(o.name)},
                                  {u"price"_s, price.cents() ? format(price) : QString()},
                                  {u"soldOut"_s, linked && !linked->available}, {u"chosen"_s, qs(o.name) == chosen}});
    }
    QVariantList notes;
    if (const ModifierGroup *n = bm.notesGroup.empty() ? nullptr : s_->settings.modifierGroup(bm.notesGroup))
        for (const ModifierOption &o : n->options)
            notes.append(QVariantMap{{u"name"_s, qs(o.name)},
                                     {u"price"_s, o.price.cents() ? format(o.price) : QString()},
                                     {u"chosen"_s, std::ranges::any_of(l->modifiers, [&](const Modifier &m) {
                                          return m.group == n->id && m.name == o.name;
                                      })}});
    return {{u"active"_s, true}, {u"lineId"_s, qint64(l->id)}, {u"item"_s, qs(l->displayName())},
            {u"group"_s, qs(g->name)}, {u"mixers"_s, mixers}, {u"notes"_s, notes}, {u"mixer"_s, chosen}};
}

void PosService::addBarNotes(OrderLine &line, const QStringList &notes)
{
    const std::string &gid = s_->settings.barMenu.notesGroup;
    const ModifierGroup *g = gid.empty() ? nullptr : s_->settings.modifierGroup(gid);
    if (!g)
        return;
    for (const QString &name : notes) {
        const auto o = std::ranges::find_if(g->options, [&](const ModifierOption &x) { return qs(x.name) == name; });
        if (o == g->options.end() || std::ranges::any_of(line.modifiers, [&](const Modifier &m) {
                return m.group == g->id && m.name == o->name;
            }))
            continue;
        Modifier m;
        m.itemId = o->itemId;
        m.name = o->name;
        m.unitPrice = optionPrice(*o, line);
        m.group = g->id;
        m.kitchenName = o->kitchenName;
        m.kitchenHide = o->kitchenHide;
        stampModifier(m);
        line.modifiers.push_back(m);
    }
}

bool PosService::orderPour(const QString &itemId, const QVariantList &notes)
{
    const MenuItem *item = findItem(itemId);
    if (!item)
        return fail(tr("'%1' is not on the menu.").arg(itemId));
    // Kept until the pour is on the check (an ID check or a responsible-service ask may come first).
    pourNotesItem_ = qs(item->id);
    pourNotes_ = namesOf(notes);
    return addItem(qs(item->id));
}

bool PosService::answerMixer(int index, const QVariantList &notes)
{
    if (!require(perm::Order, tr("Ordering")))
        return false;
    Check *c = current();
    OrderLine *l = c && mixerLine_ ? c->line(mixerLine_) : nullptr;
    if (!l || l->sent || l->voided) {
        mixerLine_ = 0;
        emit checkChanged();
        return fail(tr("That drink isn't waiting for a mixer any more."));
    }
    const PosSettings::BarMenu &bm = s_->settings.barMenu;
    const ModifierGroup *g = bm.mixerGroup.empty() ? nullptr : s_->settings.modifierGroup(bm.mixerGroup);
    if (index >= 0) {
        if (!g || index >= int(g->options.size()))
            return fail(tr("That mixer isn't on the list (Manager -> Modifiers)."));
        const ModifierOption &o = g->options[index];
        const MenuItem *linked = o.itemId.empty() ? nullptr : findItem(qs(o.itemId));
        if (linked && !linked->available)
            return fail(tr("%1 is sold out.").arg(qs(o.name)));
        std::erase_if(l->modifiers, [&](const Modifier &m) { return m.group == g->id; });   // one mixer
        Modifier m;
        m.itemId = o.itemId;   // its stock, when it is a menu item
        m.name = o.name;
        m.unitPrice = optionPrice(o, *l);
        m.group = g->id;
        m.kitchenName = o.kitchenName;
        m.kitchenHide = o.kitchenHide;
        stampModifier(m);
        l->modifiers.push_back(m);
    }
    // The notes chosen now are the drink's notes.
    if (!bm.notesGroup.empty())
        std::erase_if(l->modifiers, [&](const Modifier &m) { return m.group == bm.notesGroup; });
    addBarNotes(*l, namesOf(notes));
    mixerLine_ = 0;
    selectedLine_ = l->id;
    changed(*c);
    return true;
}

} // namespace vt::app
