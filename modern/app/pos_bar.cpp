// PosService: the bar (HCA, owner 2026-11-16) - liquor and wine kept in ounces and received by the bottle
// or the case, bottled beer by the bottle; Link pours to bottles (each drink item to its bottle, by name);
// the shelf count in bottles and tenths; bottle variance. Draft beer: 18 lines (T1-L1 ... T3-L6), a keg
// on each, pours out of it, Change keg, keg variance. A drink's pricing aid (5x its cost, tax included).
//
// Receiving never changes the system cost (pos_inventory.cpp): the cost per bottle or keg here is the
// system's, set in Manager -> Inventory (or made from the drink sheet's costs when a good is first made).

#include "app/pos_admin_shared.hh"
#include "app/i18n.hh"
#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <map>
#include <iterator>
#include <set>
#include <tuple>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

constexpr const char *kDraftTag = "__draft__:";

// A drink's name, split from its size: "Tito's 1.25oz" -> Tito's, oz 1.25; "Kaiken Malbec Glass" -> glass;
// "... Bottle" -> bottle; "J. Lohr Cabernet 375ml" -> ml 375; otherwise no size.
struct DrinkName {
    QString base;
    QString size;    // oz | glass | bottle | ml | (empty)
    double value = 0;
};

QString tidy(QString s)
{
    s.replace(QChar(0x2019), u'\'');
    s.replace(QChar(0x2018), u'\'');
    return s.simplified();
}

DrinkName parseDrink(const QString &name)
{
    static const QRegularExpression oz(uR"(^(.*\S)\s+(\d+(?:\.\d+)?)\s*(?:oz|ounces?)\.?$)"_s,
                                       QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression ml(uR"(^(.*\S)\s+(\d+(?:\.\d+)?)\s*ml$)"_s, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression glass(uR"(^(.*\S)\s+(?:glass|gls)$)"_s, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bottle(uR"(^(.*\S)\s+(?:bottle|btl)$)"_s, QRegularExpression::CaseInsensitiveOption);
    const QString n = tidy(name);
    if (const auto m = oz.match(n); m.hasMatch())
        return {m.captured(1), u"oz"_s, m.captured(2).toDouble()};
    if (const auto m = ml.match(n); m.hasMatch())
        return {m.captured(1), u"ml"_s, m.captured(2).toDouble()};
    if (const auto m = glass.match(n); m.hasMatch())
        return {m.captured(1), u"glass"_s, 0};
    if (const auto m = bottle.match(n); m.hasMatch())
        return {m.captured(1), u"bottle"_s, 0};
    return {n, QString(), 0};
}

std::string keyOf(const QString &name) { return ss(tidy(name).toLower()); }

// A bottle of `ml` in ounces, to a tenth (375 -> 12.7, 187 -> 6.3, 750 -> 25.4).
double ozOfMl(double ml) { return std::round(ml / 29.5735 * 10.0) / 10.0; }

QString number(double v, int decimals = 1)
{
    QString s = QString::number(v, 'f', decimals);
    if (s.contains(u'.')) {
        while (s.endsWith(u'0'))
            s.chop(1);
        if (s.endsWith(u'.'))
            s.chop(1);
    }
    return s == u"-0"_s ? u"0"_s : s;
}

// "750ml", "1L", "1.75L", "375ml", "187ml"; others in oz.
QString bottleSizeText(double oz)
{
    const struct { double oz; const char16_t *text; } sizes[] = {
        {kOz750ml, u"750ml"}, {kOz1L, u"1L"}, {kOz1750ml, u"1.75L"}, {kOz375ml, u"375ml"}, {kOz187ml, u"187ml"}};
    for (const auto &s : sizes)
        if (std::abs(s.oz - oz) < 0.05)
            return QString::fromUtf16(s.text);
    return number(oz) + u" oz"_s;
}

QString kegSizeText(double oz)
{
    if (std::abs(oz - kOzHalfBarrel) < 1) return u"1/2 bbl"_s;
    if (std::abs(oz - kOzQuarterBarrel) < 1) return u"1/4 bbl"_s;
    if (std::abs(oz - kOzSixthBarrel) < 1) return u"1/6 bbl"_s;
    return number(oz, 0) + u" oz"_s;
}

int perCaseFor(const std::string &kind, double packOz)
{
    if (kind == "beer")
        return 24;
    return packOz >= 50 ? 6 : packOz <= 13 ? 24 : 12;
}

bool beerish(const QString &category)
{
    const QString c = category.toLower();
    return c.contains(u"beer"_s) || c.contains(u"cider"_s) || c.contains(u"seltzer"_s);
}

bool draftish(const QString &category)
{
    const QString c = category.toLower();
    return c.contains(u"draft"_s) || c.contains(u"draught"_s) || c.contains(u"tap"_s);
}

Money costTag(const MenuItem &m)
{
    return Money::fromCents(std::llround(recipeValue(m, "__cost__") * 100.0));
}

} // namespace

// --- Link pours to bottles ---------------------------------------------------------------

std::vector<PosService::PourLink> PosService::pourLinkPlan() const
{
    std::map<std::string, QString> categoryName;
    for (const MenuCategory &c : s_->categories())
        categoryName[c.id] = qs(c.name);
    // First each drink by its name; then the goods they pour from, by name (a size of its own when the same
    // wine also comes in another size: "J. Lohr Cabernet 375ml" beside "J. Lohr Cabernet").
    struct Drink {
        PourLink link;
        QString base;
        double size = 0;      // a bottle size from the name (ml); 0: the good's own (750ml when new)
        bool whole = false;   // takes the whole bottle (a wine Bottle, a split)
    };
    std::vector<Drink> drinks;
    for (int i = 0; i < int(s_->menu.size()); ++i) {
        const MenuItem &m = s_->menu[i];
        if (m.isModifier || (m.taxClass != TaxClass::Alcohol && pourOzOf(m) <= 0 && abvOf(m) <= 0))
            continue;
        const QString category = categoryName.contains(m.family) ? categoryName[m.family] : qs(m.family);
        const DrinkName n = parseDrink(qs(m.name));
        Drink d;
        d.link.item = i;
        d.link.status = "link";
        d.base = n.base;
        if (n.size == u"oz" && (n.value >= 8 || draftish(category))) {
            d.link.kind = "draft";
            d.link.amount = n.value;
        } else if (n.size == u"oz") {
            d.link.kind = "bottle";
            d.link.amount = n.value;
        } else if (n.size == u"glass") {
            d.link.kind = "bottle";
            d.link.amount = pourOzOf(m) > 0 ? pourOzOf(m) : 5.0;
        } else if (n.size == u"bottle") {
            d.link.kind = "bottle";
            d.whole = true;
        } else if (n.size == u"ml" && n.value > 0) {
            d.link.kind = "bottle";
            d.size = ozOfMl(n.value);
            d.whole = true;
        } else if (pourOzOf(m) > 0 && draftish(category)) {
            d.link.kind = "draft";
            d.link.amount = pourOzOf(m);
        } else if (pourOzOf(m) > 0 && beerish(category)) {
            d.link.kind = "beer";
            d.link.amount = 1;
        } else {
            if (pourOzOf(m) <= 0)
                continue;   // not a pour (no size, no pour)
            d.link.status = "skip";
            d.link.note = "no size in its name";
        }
        drinks.push_back(d);
    }
    // Sizes each bottle name comes in (0: the standard one).
    std::map<std::string, std::set<double>> sizes;
    for (const Drink &d : drinks)
        if (d.link.kind == "bottle")
            sizes[keyOf(d.base)].insert(d.size);
    const auto existing = [&](const std::string &key, const std::string &kind) -> const Ingredient * {
        for (const Ingredient &g : s_->ingredients)
            if (g.kind == kind && keyOf(qs(g.name)) == key)
                return &g;
        return nullptr;
    };
    std::vector<PourLink> plan;
    for (Drink &d : drinks) {
        PourLink &l = d.link;
        const MenuItem &m = s_->menu[l.item];
        if (l.status == "skip") {
            l.goodName = ss(d.base);
            plan.push_back(l);
            continue;
        }
        std::string lineId;   // the recipe line it should have
        if (l.kind == "draft") {
            l.goodName = ss(d.base);
            lineId = kDraftTag + l.goodName;
        } else {
            QString name = d.base;
            const auto &all = sizes[keyOf(d.base)];
            if (l.kind == "bottle" && d.size > 0 && all.size() > 1)
                name += u' ' + bottleSizeText(d.size);
            l.goodName = ss(name);
            l.goodKey = keyOf(name);
            const Ingredient *g = existing(l.goodKey, l.kind);
            l.goodId = g ? g->id : std::string();
            l.packOz = l.kind == "beer" ? 0 : g && g->packOz > 0 ? g->packOz : d.size > 0 ? d.size : kOz750ml;
            if (d.whole)
                l.amount = l.packOz;
            lineId = l.goodId;
        }
        // Linked already: that line and no other bottle or draft line.
        if (!lineId.empty()) {
            bool has = false, other = false;
            for (const RecipeLine &rl : m.recipe) {
                const bool barLine = rl.ingredientId.starts_with(kDraftTag)
                                     || [&] { const Ingredient *x = const_cast<PosShared *>(s_)->ingredient(rl.ingredientId);
                                              return x && x->bar(); }();
                if (!barLine)
                    continue;
                if (rl.ingredientId == lineId && std::abs(rl.quantity - l.amount) < 1e-6)
                    has = true;
                else
                    other = true;
            }
            if (has && !other)
                l.status = "same";
        }
        plan.push_back(l);
    }
    return plan;
}

bool PosService::linkPours()
{
    if (!require(perm::Manager, tr("Linking pours to bottles")))
        return false;
    const std::vector<PourLink> plan = pourLinkPlan();
    if (std::ranges::none_of(plan, [](const PourLink &l) { return l.status == "link"; }))
        return fail(tr("Every pour is linked already."));
    // The cost of each new good, from the drink sheet's costs (the Menu Builder's): a whole bottle's cost,
    // else the biggest pour's cost per ounce times the bottle.
    std::map<std::string, Money> packCost;
    std::map<std::string, double> fromPour;
    for (const PourLink &l : plan) {
        if (l.status != "link" || !l.goodId.empty() || l.kind == "draft")
            continue;
        const Money c = costTag(s_->menu[l.item]);
        if (c.cents() <= 0 || l.amount <= 0)
            continue;
        const bool whole = l.kind == "beer" || std::abs(l.amount - l.packOz) < 1e-6;
        if (whole) {
            packCost[l.goodKey] = c;
            fromPour[l.goodKey] = 1e9;
        } else if (l.amount > fromPour[l.goodKey]) {
            packCost[l.goodKey] = c.scaled(milli(l.packOz), milli(l.amount));
            fromPour[l.goodKey] = l.amount;
        }
    }
    PosShared::PourLinkUndo undo;
    std::map<std::string, std::string> made;   // good key -> id
    std::set<std::string> touched;
    for (const PourLink &l : plan) {
        if (l.status != "link")
            continue;
        std::string id = l.goodId;
        if (l.kind != "draft" && id.empty()) {
            if (const auto it = made.find(l.goodKey); it != made.end()) {
                id = it->second;
            } else {
                Ingredient g;
                g.name = l.goodName;
                g.kind = l.kind;
                g.unit = l.kind == "beer" ? "bottle" : "oz";
                g.packOz = l.packOz;
                g.perCase = perCaseFor(l.kind, l.packOz);
                const MenuItem &m = s_->menu[l.item];
                for (const MenuCategory &c : s_->categories())
                    if (c.id == m.family)
                        g.category = c.name;
                if (g.category.empty())
                    g.category = m.family;
                const Money c = packCost.contains(l.goodKey) ? packCost[l.goodKey] : Money();
                if (l.kind == "beer") {
                    g.cost = c;
                } else {
                    g.packCost = c;
                    g.cost = c.scaled(1000, milli(g.packOz));
                }
                g.id = ss(admin::uniqueId(qs(l.goodName), s_->ingredients, [](const Ingredient &x) { return x.id; }, -1));
                s_->ingredients.push_back(g);
                if (s_->sink)
                    s_->sink->saveIngredient(g, int(s_->ingredients.size()) - 1);
                made[l.goodKey] = id = g.id;
                undo.goodsAdded.push_back(id);
            }
        }
        MenuItem &m = s_->menu[l.item];
        if (touched.insert(m.id).second)
            undo.recipes.emplace_back(m.id, m.recipe);
        std::erase_if(m.recipe, [&](const RecipeLine &rl) {
            if (rl.ingredientId.starts_with(kDraftTag))
                return true;
            const Ingredient *x = s_->ingredient(rl.ingredientId);
            return x && x->bar();
        });
        m.recipe.push_back({l.kind == "draft" ? kDraftTag + l.goodName : id, l.amount});
        ++undo.links;
        if (s_->sink)
            s_->sink->saveMenuItem(m, l.item);
    }
    const int links = undo.links;
    const int goods = int(undo.goodsAdded.size());
    s_->pourLinkUndo = std::move(undo);
    menuChanged();
    emit s_->dayChanged();
    emit notice(tr("Linked %n pour(s) to their bottles or kegs", "", links)
                + (goods ? tr("; %n new bottle good(s)", "", goods) : QString()));
    return true;
}

bool PosService::undoLinkPours()
{
    if (!require(perm::Manager, tr("Linking pours to bottles")))
        return false;
    if (!s_->pourLinkUndo)
        return fail(tr("There is no linking to undo."));
    const PosShared::PourLinkUndo undo = *s_->pourLinkUndo;
    s_->pourLinkUndo.reset();
    for (const auto &[id, recipe] : undo.recipes)
        for (int i = 0; i < int(s_->menu.size()); ++i)
            if (s_->menu[i].id == id) {
                s_->menu[i].recipe = recipe;
                if (s_->sink)
                    s_->sink->saveMenuItem(s_->menu[i], i);
            }
    // The goods it made, unless counted or received since (those stay, unlinked).
    int removed = 0;
    for (const std::string &id : undo.goodsAdded) {
        const auto it = std::ranges::find(s_->ingredients, id, &Ingredient::id);
        if (it == s_->ingredients.end() || it->countedAt != 0 || it->received != 0)
            continue;
        s_->ingredients.erase(it);
        if (s_->sink)
            s_->sink->deleteIngredient(id);
        ++removed;
    }
    if (removed && s_->sink)
        for (int i = 0; i < int(s_->ingredients.size()); ++i)
            s_->sink->saveIngredient(s_->ingredients[i], i);
    menuChanged();
    emit s_->dayChanged();
    emit notice(tr("Linking undone: %n pour(s) as they were", "", undo.links));
    return true;
}

// --- what the screens show ---------------------------------------------------------------

QVariantMap PosService::barInventory() const
{
    if (!can(u"manager"_s))
        return {};
    const auto date = [](std::int64_t at) {
        return i18n::locale().toString(QDateTime::fromMSecsSinceEpoch(at), u"ddd M/d h:mm AP"_s);
    };
    std::vector<const Ingredient *> list;
    for (const Ingredient &g : s_->ingredients)
        if (g.bar())
            list.push_back(&g);
    std::ranges::stable_sort(list, [](const Ingredient *a, const Ingredient *b) {
        return std::tie(a->category, a->name) < std::tie(b->category, b->name);
    });
    QVariantList goods, kegs;
    QStringList categories;
    for (const Ingredient *g : list) {
        const bool bottle = g->kind == "bottle" && g->packOz > 0;
        const auto inPacks = [&](double v) { return number(bottle ? v / g->packOz : v); };
        const QString size = g->kind == "keg" ? kegSizeText(g->packOz) : bottle ? bottleSizeText(g->packOz) : tr("bottle");
        const Money each = bottle ? g->packCost : g->cost;
        if (g->kind == "keg")
            kegs.append(QVariantMap{{u"id"_s, qs(g->id)}, {u"name"_s, qs(g->name)}, {u"beer"_s, qs(g->beer)},
                                    {u"size"_s, size}, {u"onHand"_s, number(g->onHand)}, {u"cost"_s, format(g->cost)}});
        const QString category = g->category.empty() ? (g->kind == "keg" ? tr("Kegs") : tr("Other")) : qs(g->category);
        if (!categories.contains(category))
            categories << category;
        goods.append(QVariantMap{{u"id"_s, qs(g->id)}, {u"name"_s, qs(g->name)}, {u"kind"_s, qs(g->kind)},
                                 {u"category"_s, category}, {u"unit"_s, qs(g->unit)}, {u"packOz"_s, g->packOz},
                                 {u"perCase"_s, g->perCase}, {u"size"_s, size},
                                 {u"counts"_s, g->kind == "keg" ? tr("kegs") : tr("bottles")},
                                 {u"onHand"_s, number(g->onHand)}, {u"onHandPacks"_s, inPacks(g->onHand)},
                                 {u"lastCount"_s, g->countedAt ? inPacks(g->start) : QString()},
                                 {u"lastCountWhen"_s, g->countedAt ? date(g->countedAt) : tr("never")},
                                 {u"cost"_s, format(each)}});
    }
    // Link pours to bottles: the preview.
    QVariantList rows;
    int toLink = 0, same = 0, skipped = 0;
    std::set<std::string> newGoods;
    QStringList beers;
    for (const PourLink &l : pourLinkPlan()) {
        const MenuItem &m = s_->menu[l.item];
        QString good, amount;
        if (l.kind == "draft") {
            good = tr("Draft: %1").arg(qs(l.goodName));
            amount = tr("%1 oz").arg(number(l.amount, 2));
            if (!beers.contains(qs(l.goodName)))
                beers << qs(l.goodName);
        } else if (l.kind == "beer") {
            good = qs(l.goodName);
            amount = tr("1 bottle");
        } else if (!l.kind.empty()) {
            good = qs(l.goodName) + u" ("_s + bottleSizeText(l.packOz) + u')';
            amount = tr("%1 oz").arg(number(l.amount, 2));
        }
        if (l.status == "link") {
            ++toLink;
            if (l.goodId.empty() && l.kind != "draft")
                newGoods.insert(l.goodKey);
        } else if (l.status == "same") {
            ++same;
        } else {
            ++skipped;
        }
        rows.append(QVariantMap{{u"item"_s, qs(m.name)}, {u"good"_s, good}, {u"amount"_s, amount},
                                {u"status"_s, qs(l.status)}, {u"isNew"_s, l.goodId.empty() && l.kind != "draft" && l.status == "link"},
                                {u"note"_s, l.note.empty() ? QString() : tr("No size in its name")}});
    }
    for (const Ingredient *g : list)
        if (g->kind == "keg" && !g->beer.empty() && !beers.contains(qs(g->beer)))
            beers << qs(g->beer);
    return {{u"goods"_s, goods}, {u"categories"_s, categories}, {u"kegs"_s, kegs}, {u"beers"_s, beers},
            {u"allowance"_s, s_->settings.kegAllowancePercent},
            {u"links"_s, QVariantMap{{u"rows"_s, rows}, {u"toLink"_s, toLink}, {u"newGoods"_s, int(newGoods.size())},
                                     {u"same"_s, same}, {u"skipped"_s, skipped}}},
            {u"undo"_s, s_->pourLinkUndo ? s_->pourLinkUndo->links : 0},
            {u"countId"_s, qint64(s_->lastStockCountId)}};
}

QVariantMap PosService::draftLines() const
{
    if (!user())
        return {};
    QVariantList lines;
    for (const std::string &id : draftLineIds()) {
        const auto it = std::ranges::find(s_->settings.draftLines, id, &DraftLine::id);
        QVariantMap l{{u"id"_s, qs(id)}, {u"tower"_s, qs(id.substr(0, 2))}};
        if (it != s_->settings.draftLines.end() && !it->kegId.empty()) {
            const double left = std::max(0.0, it->left());
            l.insert(u"kegId"_s, qs(it->kegId));
            l.insert(u"keg"_s, qs(it->kegName));
            l.insert(u"beer"_s, qs(it->beer));
            l.insert(u"kegOz"_s, it->kegOz);
            l.insert(u"poured"_s, number(it->poured, 0));
            l.insert(u"left"_s, number(left, 0));
            l.insert(u"percentLeft"_s, it->kegOz > 0 ? int(std::lround(100.0 * left / it->kegOz)) : 0);
            l.insert(u"since"_s, i18n::locale().toString(QDateTime::fromMSecsSinceEpoch(it->tappedAt), u"ddd M/d h:mm AP"_s));
            l.insert(u"by"_s, qs(it->tappedBy));
        }
        lines.append(l);
    }
    QVariantList kegs;
    for (const Ingredient &g : s_->ingredients)
        if (g.kind == "keg")
            kegs.append(QVariantMap{{u"id"_s, qs(g.id)}, {u"name"_s, qs(g.name)}, {u"beer"_s, qs(g.beer)},
                                    {u"onHand"_s, number(g.onHand)}});
    return {{u"lines"_s, lines}, {u"kegs"_s, kegs}, {u"allowance"_s, s_->settings.kegAllowancePercent}};
}

// --- the shelf count ---------------------------------------------------------------------

bool PosService::saveBarCount(const QVariantMap &r)
{
    if (!require(perm::Manager, tr("Counting stock")))
        return false;
    QVariantList lines;
    for (const QVariant &v : r.value(u"lines"_s).toList()) {
        const QVariantMap l = v.toMap();
        const QString typed = l.value(u"count"_s).toString().trimmed().replace(u',', u'.');
        if (typed.isEmpty())
            continue;
        const Ingredient *g = s_->ingredient(ss(l.value(u"ingredient"_s).toString()));
        if (!g || !g->bar())
            return fail(tr("There is no bottle or keg '%1' (see Manager → Inventory).").arg(l.value(u"ingredient"_s).toString()));
        bool ok = false;
        const double count = typed.toDouble(&ok);
        if (!ok || count < 0 || !std::isfinite(count))
            return fail(tr("Type how many there are of %1, like 3.4 (0 or more).").arg(qs(g->name)));
        // Bottles (and tenths) of a bottle good in ounces; bottled beer and kegs as they are.
        const double counted = g->kind == "bottle" && g->packOz > 0 ? std::round(count * g->packOz * 1000) / 1000 : count;
        lines.append(QVariantMap{{u"ingredient"_s, qs(g->id)}, {u"counted"_s, QString::number(counted, 'g', 12)}});
    }
    if (lines.isEmpty())
        return fail(tr("Type how many there are of at least one item."));
    if (!saveStockCount({{u"lines"_s, lines}}))
        return false;
    emit s_->dayChanged();
    return true;
}

// --- draft lines and kegs ----------------------------------------------------------------

void PosShared::saveDraftPours()
{
    if (!sink)
        return;
    if (now() - draftSavedAt >= 60'000) {
        draftSavedAt = now();
        sink->saveSettings(settings);
        return;
    }
    if (draftSaveQueued)
        return;
    draftSaveQueued = true;   // the last pours of a rush: a minute later
    QTimer::singleShot(61'000, this, [this] {
        draftSaveQueued = false;
        draftSavedAt = now();
        if (sink)
            sink->saveSettings(settings);
    });
}

void PosService::pourDraft(const std::string &beer, double oz)
{
    const std::string key = keyOf(qs(beer));
    for (DraftLine &l : s_->settings.draftLines)
        if (!l.kegId.empty() && keyOf(qs(l.beer)) == key) {
            l.poured += oz;
            s_->saveDraftPours();
            return;
        }
}

bool PosService::changeKeg(const QVariantMap &r)
{
    if (!require(perm::Order, tr("Changing a keg")))
        return false;
    const std::string lineId = ss(r.value(u"line"_s).toString().trimmed().toUpper());
    if (std::ranges::find(draftLineIds(), lineId) == draftLineIds().end())
        return fail(tr("Choose the line (T1-L1 to T3-L6)."));
    const QString kegId = r.value(u"keg"_s).toString();
    Ingredient *g = kegId.isEmpty() ? nullptr : s_->ingredient(ss(kegId));
    if (!kegId.isEmpty() && (!g || g->kind != "keg"))
        return fail(tr("There is no keg '%1' (add it under Draft lines, Add a keg).").arg(kegId));
    auto it = std::ranges::find(s_->settings.draftLines, lineId, &DraftLine::id);
    const bool had = it != s_->settings.draftLines.end() && !it->kegId.empty();
    if (!had && !g)
        return fail(tr("Nothing is on %1: choose the keg to put on it.").arg(qs(lineId)));
    const std::string who = user() ? user()->name : ss(terminal_);
    QString said;
    if (had) {
        // What's left in the old keg: kicked (empty), or the ounces typed.
        double left = 0;
        const bool kicked = r.value(u"kicked"_s).toBool();
        if (!kicked) {
            bool ok = false;
            left = r.value(u"left"_s).toString().trimmed().replace(u',', u'.').toDouble(&ok);
            if (!ok || left < 0 || left > it->kegOz)
                return fail(tr("Type the ounces left in the old keg (0 to %1), or choose Kicked.").arg(number(it->kegOz, 0)));
        }
        KegRecord k;
        k.id = ++s_->settings.lastKegId;
        k.line = lineId;
        k.kegId = it->kegId;
        k.kegName = it->kegName;
        k.beer = it->beer;
        k.kegOz = it->kegOz;
        k.kegCost = it->kegCost;
        k.tappedAt = it->tappedAt;
        k.tappedBy = it->tappedBy;
        k.offAt = now();
        k.offBy = who;
        k.poured = it->poured;
        k.left = left;
        k.kicked = kicked || left == 0;
        s_->settings.kegs.push_back(k);
        if (s_->settings.kegs.size() > 300)
            s_->settings.kegs.erase(s_->settings.kegs.begin());
        said = tr("Old keg: %1 oz poured, %2% loss.").arg(number(k.poured, 0), number(k.lossPercent()));
    }
    if (it == s_->settings.draftLines.end()) {
        s_->settings.draftLines.push_back({});
        it = std::prev(s_->settings.draftLines.end());
    }
    *it = DraftLine{};
    it->id = lineId;
    if (g) {
        it->kegId = g->id;
        it->kegName = g->name;
        it->beer = g->beer.empty() ? g->name : g->beer;
        it->kegOz = g->packOz;
        it->kegCost = g->cost;
        it->tappedAt = now();
        it->tappedBy = who;
        // Off the shelf: one keg used.
        g->onHand -= 1;
        g->sold += 1;
        if (s_->sink)
            s_->sink->saveIngredient(*g, int(g - s_->ingredients.data()));
    }
    s_->draftSavedAt = now();
    s_->saveSettings();
    emit s_->dayChanged();
    emit notice((g ? tr("%1: %2 on.").arg(qs(lineId), qs(g->name)) : tr("%1: nothing on it now.").arg(qs(lineId)))
                + (said.isEmpty() ? QString() : u' ' + said));
    return true;
}

bool PosService::addKeg(const QVariantMap &r)
{
    if (!require(perm::Manager, tr("Adding a keg")))
        return false;
    const QString beer = tidy(r.value(u"beer"_s).toString());
    if (beer.isEmpty())
        return fail(tr("Type the beer's name."));
    const QString size = r.value(u"size"_s).toString();
    const double oz = size == u"half" ? kOzHalfBarrel : size == u"quarter" ? kOzQuarterBarrel
                    : size == u"sixth" ? kOzSixthBarrel : 0;
    if (oz <= 0)
        return fail(tr("Choose the keg's size: 1/2, 1/4 or 1/6 barrel."));
    const QString costText = r.value(u"cost"_s).toString().trimmed();
    const qint64 cents = costText.isEmpty() ? 0 : priceCents(costText, currencySymbol());
    if (cents < 0)
        return fail(tr("Type the keg's cost, like 185.00."));
    const QString name = beer + u' ' + kegSizeText(oz);
    for (const Ingredient &x : s_->ingredients)
        if (keyOf(qs(x.name)) == keyOf(name))
            return fail(tr("There's already a %1.").arg(name));
    Ingredient g;
    g.id = ss(admin::uniqueId(u"keg "_s + name, s_->ingredients, [](const Ingredient &x) { return x.id; }, -1));
    g.name = ss(name);
    g.kind = "keg";
    g.unit = "keg";
    g.packOz = oz;
    g.beer = ss(beer);
    g.category = "Kegs";
    g.cost = Money::fromCents(cents);
    s_->ingredients.push_back(g);
    if (s_->sink)
        s_->sink->saveIngredient(g, int(s_->ingredients.size()) - 1);
    ++s_->adminRevision;
    emit s_->adminChanged();
    emit s_->dayChanged();
    emit notice(tr("Added %1: receive kegs of it on Receive a Delivery.").arg(name));
    return true;
}

bool PosService::setKegAllowance(int percent)
{
    if (!require(perm::Manager, tr("Changing the keg allowance")))
        return false;
    if (percent < 0 || percent > 50)
        return fail(tr("The allowance is a percent from 0 to 50."));
    s_->settings.kegAllowancePercent = percent;
    s_->saveSettings();
    emit s_->dayChanged();
    emit notice(tr("Keg allowance: %1%").arg(percent));
    return true;
}

// --- pricing aid ---------------------------------------------------------------------------

QVariantMap PosService::drinkPricing(const MenuItem &m) const
{
    // What one pour costs as `base` (a bottle's, a keg's, a bottle of beer's) x amount / per.
    Money base;
    double amount = 0, per = 0;
    QString from;
    for (const RecipeLine &rl : m.recipe) {
        if (rl.ingredientId.starts_with(kDraftTag)) {
            const std::string key = keyOf(qs(rl.ingredientId.substr(std::string(kDraftTag).size())));
            for (const DraftLine &l : s_->settings.draftLines)
                if (!l.kegId.empty() && keyOf(qs(l.beer)) == key && l.kegOz > 0) {
                    base = l.kegCost;
                    per = l.kegOz;
                    break;
                }
            if (per <= 0)
                for (const Ingredient &g : s_->ingredients)
                    if (g.kind == "keg" && keyOf(qs(g.beer)) == key && g.packOz > 0 && g.cost.cents() > 0) {
                        base = g.cost;
                        per = g.packOz;
                        break;
                    }
            amount = rl.quantity;
            from = u"keg"_s;
        } else if (const Ingredient *g = const_cast<PosShared *>(s_)->ingredient(rl.ingredientId); g && g->bar()) {
            if (g->kind == "bottle" && g->packOz > 0 && g->packCost.cents() > 0) {
                base = g->packCost;
                per = g->packOz;
            } else {
                base = g->cost;
                per = 1;
            }
            amount = rl.quantity;
            from = u"bottle"_s;
        }
    }
    if (base.cents() <= 0 || per <= 0 || amount <= 0) {   // not linked: the item's own cost (one pour)
        base = costTag(m);
        amount = per = 1;
        from = u"cost"_s;
    }
    if (base.cents() <= 0)
        return {};
    const std::int64_t ppm = s_->settings.tax.ratePpm(m.taxClass);
    const Money pour = base.scaled(milli(amount), milli(per));
    const Money target = base.scaled(milli(amount) * 5 * (1'000'000 + ppm), milli(per) * 1'000'000);
    const double oz = from == u"bottle" && per == 1 ? pourOzOf(m) : from == u"cost" ? pourOzOf(m) : amount;
    QVariantMap out{{u"pourCost"_s, format(pour)}, {u"target"_s, format(target)}, {u"from"_s, from},
                    {u"taxPercent"_s, number(double(ppm) / 10000.0, 3)}};
    if (oz > 0)
        out.insert(u"costPerOz"_s, format(pour.scaled(1000, milli(oz))));
    return out;
}

// --- reports -------------------------------------------------------------------------------

Report PosService::barVarianceReport(std::int64_t from, std::int64_t to, const ReportContext &ctx) const
{
    Report r;
    r.id = "barvariance";
    r.title = "Bar Variance";
    r.subtitle = ctx.period;
    r.columns = {"", "Opening", "Received", "Poured", "Closing", "Short (oz)", "Short ($)"};
    struct Sum {
        const Ingredient *g = nullptr;
        double opening = 0, received = 0, poured = 0, closing = 0, shortBy = 0;
        Money value;
    };
    std::map<std::string, std::vector<Sum>> byCategory;
    std::map<std::string, Sum> sums;
    std::vector<std::string> order;
    for (const StockCount &c : s_->stockCounts) {
        if (c.at < from || c.at >= to)
            continue;
        for (const StockCount::Line &l : c.lines) {
            const Ingredient *g = const_cast<PosShared *>(s_)->ingredient(l.ingredientId);
            if (!g || !g->bar() || g->kind == "keg")
                continue;
            auto [it, first] = sums.try_emplace(g->id);
            Sum &s = it->second;
            if (first) {   // the count before: what it opened at
                s.g = g;
                s.opening = l.start;
                order.push_back(g->id);
            }
            s.received += l.received + l.adjusted;
            s.poured += l.sold + l.wasted;
            s.closing = l.counted;
            s.shortBy += l.expected - l.counted;
        }
    }
    if (order.empty()) {
        r.note("No bar counts in these days (Manager -> Bar & Kegs -> Shelf count).");
        return r;
    }
    for (const std::string &id : order) {
        Sum s = sums[id];
        s.value = s.g->costOf(s.shortBy);
        byCategory[s.g->category.empty() ? std::string("Other") : s.g->category].push_back(s);
    }
    const auto amount = [](double v) { return number(std::round(v * 10) / 10).toStdString(); };
    Money all;
    for (auto &[category, list] : byCategory) {
        r.section(category);
        std::ranges::stable_sort(list, [](const Sum &a, const Sum &b) { return a.value > b.value; });   // biggest loss first
        Money total;
        for (const Sum &s : list) {
            r.line({s.g->name + (s.g->kind == "bottle" ? " (oz)" : " (bottles)"), amount(s.opening), amount(s.received),
                    amount(s.poured), amount(s.closing), amount(s.shortBy), ctx.money(s.value)});
            total += s.value;
        }
        r.total({category, "", "", "", "", "", ctx.money(total)});
        all += total;
    }
    r.total({"All", "", "", "", "", "", ctx.money(all)});
    r.note("Opening: the count before (the previous count). Short: opening + received - poured - closing.");
    r.note("Poured: what the pours rung up took from the bottle, and waste logged. Received: deliveries, "
           "damaged and swapped lines, and changes by hand.");
    r.note("Short ($): at the system cost per bottle (Manager -> Inventory). Less than 0: more on the shelf "
           "than expected.");
    return r;
}

Report PosService::kegReport(std::int64_t from, std::int64_t to, const ReportContext &ctx) const
{
    Report r;
    r.id = "kegs";
    r.title = "Kegs";
    r.subtitle = ctx.period;
    r.columns = {"", "Keg (oz)", "Poured", "Left", "Loss (oz)", "Loss ($)", "Loss %"};
    const int allowance = s_->settings.kegAllowancePercent;
    const auto day = [&](std::int64_t at) { return ctx.date ? ctx.date(at) : ctx.clock(at); };
    const auto oz = [](double v) { return number(std::round(v), 0).toStdString(); };
    double lost = 0, used = 0;
    Money value;
    int over = 0;
    bool any = false;
    for (const KegRecord &k : s_->settings.kegs) {
        if (k.offAt < from || k.offAt >= to)
            continue;
        if (!any)
            r.section("Taken off");
        any = true;
        const bool tooMuch = k.lossPercent() > allowance + 1e-9;
        over += tooMuch ? 1 : 0;
        r.line({k.kegName + " · " + k.line + " · " + day(k.tappedAt) + " - " + day(k.offAt), oz(k.kegOz), oz(k.poured),
                k.kicked ? std::string("kicked") : oz(k.left), oz(k.loss()), ctx.money(k.lossValue()),
                number(k.lossPercent()).toStdString() + "%" + (tooMuch ? " !" : "")});
        lost += k.loss();
        used += k.used();
        value += k.lossValue();
    }
    if (any)
        r.total({"All", "", "", "", oz(lost), ctx.money(value), used > 0 ? number(100.0 * lost / used).toStdString() + "%" : ""});
    else
        r.note("No kegs taken off in these days.");
    bool onTap = false;
    for (const std::string &id : draftLineIds()) {
        const auto it = std::ranges::find(s_->settings.draftLines, id, &DraftLine::id);
        if (it == s_->settings.draftLines.end() || it->kegId.empty())
            continue;
        if (!onTap)
            r.section("On tap now");
        onTap = true;
        r.line({it->kegName + " · " + id + " · " + day(it->tappedAt), oz(it->kegOz), oz(it->poured), oz(std::max(0.0, it->left())),
                "", "", ""});
    }
    r.note(QString::fromStdString("Loss: what the keg held - what was rung up - what was left in it. Allowance for foam "
                                  "and waste: %1%; ! over it.").arg(allowance).toStdString());
    if (over)
        r.note("! Over the allowance: look at the pours, the lines and free drinks.");
    r.note("Loss ($): at the keg's system cost.");
    return r;
}

} // namespace vt::app
