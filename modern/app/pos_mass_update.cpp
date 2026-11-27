// PosService: Mass Updates (Menu Builder > Mass Updates; owner 2026-11-24, after ILSA's Mass Updates screen).
// The retail items as a grid: narrowed (find, a range on a field, a category and its tree, no category, new
// items), sorted and paged here, so a screen is sent 200 rows of a 25,000-item price book. Ticked items get
// one change at a time: a margin price, a price or cost added to or set, a field, prices copied. Each is a
// preview first (old -> new, nothing changed); Apply makes it one change set (keyed by UPC, with who, when
// and why), saved beside the database, in the Changes report, exportable for another store; Undo puts the
// last one back. Changing anything is the price book's ("pricebook": admins, or an admin's PIN), the
// corporate office's later (docs/MASS_UPDATES_STORES.md). The arithmetic is core/mass_update.hh.

#include "app/pos_json.hh"
#include "app/pos_service.hh"
#include "core/mass_update.hh"
#include "core/transfer.hh"

#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>
#include <unordered_set>

using namespace Qt::StringLiterals;
using namespace vt::core;
namespace mass = vt::core::mass;

namespace vt::app {

namespace {

constexpr int kPage = 200;          // rows sent at a time
constexpr int kPreviewPage = 200;   // preview lines sent at a time
constexpr int kLogged = 2000;       // changes put in the Changes report one by one, per change set
constexpr const char *kFormat = "viewtouch-change-set";

std::int64_t costOf(const MenuItem &m)
{
    for (const RecipeLine &rl : m.recipe)
        if (rl.ingredientId == "__cost__" && rl.quantity > 0.0)
            return std::llround(rl.quantity * 100.0);
    return 0;
}

void setCost(MenuItem &m, std::int64_t cents)
{
    std::erase_if(m.recipe, [](const RecipeLine &rl) { return rl.ingredientId == "__cost__"; });
    if (cents > 0)
        m.recipe.push_back({"__cost__", double(cents) / 100.0});
}

bool soldOutOf(const MenuItem &m) { return !m.available || m.soldOutToday; }

const QStringList &fieldKeys()
{
    static const QStringList keys{u"price"_s, u"cost"_s, u"category"_s, u"taxClass"_s, u"soldOut"_s, u"buttonColor"_s, u"noDiscount"_s};
    return keys;
}

QString fieldLabel(const std::string &field)
{
    if (field == "price") return PosService::tr("Price");
    if (field == "cost") return PosService::tr("Cost");
    if (field == "category") return PosService::tr("Category");
    if (field == "taxClass") return PosService::tr("Tax");
    if (field == "soldOut") return PosService::tr("Sold out");
    if (field == "buttonColor") return PosService::tr("Button color");
    if (field == "noDiscount") return PosService::tr("No discounts");
    return qs(field);
}

QString whyLabel(const std::string &why)
{
    if (why == "no cost") return PosService::tr("no cost");
    if (why == "changed since") return PosService::tr("changed since");
    if (why == "not found") return PosService::tr("UPC not found here");
    if (why == "no such category") return PosService::tr("no such category here");
    if (why == "not a value") return PosService::tr("not a value this store takes");
    return qs(why);
}

double amountFrom(const QVariant &v, bool *ok)
{
    QString t = v.toString().trimmed();
    t.remove(u'$');
    t.remove(u'%');
    t.remove(u' ');
    if (!t.contains(u'.') && t.count(u',') == 1)
        t.replace(u',', u'.');
    else
        t.remove(u',');
    const double d = t.toDouble(ok);
    if (*ok && !std::isfinite(d))
        *ok = false;
    return d;
}

QVariantMap changeMap(const mass::Change &c)
{
    return {{u"upc"_s, qs(c.upc)}, {u"item"_s, qs(c.name)}, {u"field"_s, qs(c.field)}, {u"before"_s, qs(c.before)},
            {u"after"_s, qs(c.after)}};
}

} // namespace

// --- shared by every screen of the store -------------------------------------------------------------

struct PosShared::MassShared {
    // When each item was first had here (New items): id -> epoch ms (0: before this was kept).
    bool seenLoaded = false;
    std::size_t seenSize = 0;
    std::unordered_map<std::string, std::int64_t> firstSeen;
    // The change sets applied here, newest last (the list; each one's file is read when it's wanted).
    struct SetInfo {
        QString id, label, who, why, from, file, undoOf;
        qint64 at = 0;
        int count = 0;
        bool undone = false;
    };
    bool setsLoaded = false;
    std::vector<SetInfo> sets;
    QHash<QString, QVariantMap> memory;   // their files, when there's no folder (tests)
    // The last one applied, for Undo.
    QString lastId;
    QString lastLabel;
    std::vector<mass::Change> lastChanges;
    bool lastUndone = true;
    // The grid's rows, made once per menu change, for one scope (retail...).
    struct Rows {
        std::uint64_t shared = 0;
        int revision = -1;
        std::size_t size = 0;
        std::string scope;
        std::vector<mass::Row> rows;
        std::unordered_map<std::string, std::uint32_t> rowOfId;
        mass::UpcIndex upc;   // every item of the menu (not only the grid's), by barcode: menu places
        std::unordered_map<std::string, std::vector<std::uint32_t>> childrenOf;   // a box's UPC -> its singles
        std::vector<std::pair<std::string, std::string>> categories;   // id, name: this scope's
        std::int64_t generation = 0;
        int baseRevision = -1;   // the price source's prices (basePrices) as they were
    } rows;
};

// --- one screen's -------------------------------------------------------------------------------------

struct PosService::MassView {
    bool active = false;
    std::string scope = kScopeRetail;
    QString find, category, field, from, to;
    QStringList path;
    bool noCategory = false, chosenOnly = false;
    int newDays = 0;
    QString sort = u"name"_s;
    bool desc = false;
    int page = 0;
    std::unordered_set<std::string> chosen;
    struct Preview {
        QString label, why, from, kind;   // kind: here, import
        std::vector<mass::Change> changes;
        std::vector<mass::Skip> skipped;
        int page = 0;
        // Price zone: the base price each change is made from (UPC key -> cents), and the bases first taken
        // from the item's own price (none received from the price source yet), kept at Apply.
        std::unordered_map<std::string, std::int64_t> bases;
        std::vector<std::pair<std::string, std::int64_t>> newBases;
    };
    std::optional<Preview> preview;
    QString message;
    // The state as last made (every binding asks again).
    mutable QString key;
    mutable QVariantMap state;
    int ticks = 0;
};

struct MassImpl {
    using Shared = PosShared::MassShared;

    static Shared &shared(PosService &p)
    {
        if (!p.s_->mass)
            p.s_->mass = std::make_shared<Shared>();
        return *p.s_->mass;
    }
    static Shared &shared(const PosService &p) { return shared(const_cast<PosService &>(p)); }
    static PosService::MassView &view(const PosService &p)
    {
        if (!p.mass_)
            p.mass_ = std::make_shared<PosService::MassView>();
        return *p.mass_;
    }
    static QString dir(const PosService &p) { return p.s_->massUpdateDir; }

    // --- when items were first had ---
    static void remember(PosService &p)
    {
        Shared &m = shared(p);
        const auto &menu = p.s_->menu;
        if (m.seenLoaded && menu.size() == m.seenSize)
            return;
        bool dirty = false;
        const QString file = dir(p).isEmpty() ? QString() : QDir(dir(p)).filePath(u"first-seen.json"_s);
        if (!m.seenLoaded) {
            m.seenLoaded = true;
            QFile f(file);
            if (!file.isEmpty() && f.open(QIODevice::ReadOnly)) {
                const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
                for (auto it = o.begin(); it != o.end(); ++it)
                    m.firstSeen[ss(it.key())] = qint64(it.value().toDouble());
            } else {
                // The first time: everything here now was here before this was kept.
                for (const MenuItem &i : menu)
                    m.firstSeen.try_emplace(i.id, 0);
                dirty = true;
            }
        }
        const std::int64_t now = p.now();
        for (const MenuItem &i : menu)
            if (m.firstSeen.try_emplace(i.id, now).second)
                dirty = true;
        m.seenSize = menu.size();
        if (dirty && !file.isEmpty()) {
            QJsonObject o;
            for (const auto &[id, at] : m.firstSeen)
                o.insert(qs(id), double(at));
            QDir().mkpath(dir(p));
            QSaveFile f(file);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
                f.commit();
            }
        }
    }

    // --- the grid's rows ---
    static const Shared::Rows &rows(const PosService &cp, const std::string &scope)
    {
        PosService &p = const_cast<PosService &>(cp);
        Shared &m = shared(p);
        Shared::Rows &x = m.rows;
        p.loadBasePrices();
        if (x.shared == p.s_->serial && x.revision == p.s_->adminRevision && x.size == p.s_->menu.size() && x.scope == scope
            && x.baseRevision == p.s_->basePricesRevision)
            return x;
        remember(p);
        x.baseRevision = p.s_->basePricesRevision;
        x.shared = p.s_->serial;
        x.revision = p.s_->adminRevision;
        x.size = p.s_->menu.size();
        x.scope = scope;
        ++x.generation;
        x.rows.clear();
        x.rowOfId.clear();
        x.childrenOf.clear();
        x.categories.clear();
        const std::vector<MenuCategory> cats = p.s_->categories();
        const auto scopes = categoryScopes(cats, p.s_->menu);
        std::unordered_map<std::string, std::string> nameOf;
        for (const MenuCategory &c : cats) {
            nameOf[c.id] = c.name;
            if (scopes.at(c.id) == scope)
                x.categories.emplace_back(c.id, c.name);
        }
        std::unordered_set<std::string> setUp;
        for (const MenuCategory &c : p.s_->settings.menuCategories)
            setUp.insert(c.id);
        const auto &menu = p.s_->menu;
        x.upc.build(menu.size(), [&](std::size_t i) { return menu[i].barcodes; });
        for (std::size_t i = 0; i < menu.size(); ++i) {
            const MenuItem &it = menu[i];
            for (const RecipeLine &rl : it.recipe)
                if (rl.ingredientId.starts_with("__parent__:"))
                    x.childrenOf[BuilderIndex::codeKey(rl.ingredientId.substr(11))].push_back(std::uint32_t(i));
            if (it.isModifier)
                continue;
            const auto sc = scopes.find(it.family);
            const bool inScope = scope == kScopeRetail ? itemIsRetail(it) || (sc != scopes.end() && sc->second == scope)
                                                       : sc != scopes.end() && sc->second == scope;
            if (!inScope)
                continue;
            mass::Row r;
            r.at = std::uint32_t(i);
            r.id = it.id;
            r.upc = it.barcodes.empty() ? std::string() : it.barcodes.front();
            r.name = it.name;
            r.lowerName = ss(qs(it.name).toLower());
            r.number = it.number;
            r.categoryId = it.family;
            r.noCategory = it.family.empty() || !setUp.contains(it.family);
            if (const auto n = nameOf.find(it.family); n != nameOf.end())
                r.category = n->second;
            const QStringList path = PosService::labelPathBelowCategory(it);
            r.brand = path.size() > 0 ? ss(path[0]) : std::string();
            r.group = path.size() > 1 ? ss(path[1]) : std::string();
            r.sub = path.size() > 2 ? ss(path[2]) : std::string();
            r.cost = costOf(it);
            r.price = it.price.cents();
            if (!p.s_->basePrices.empty() && !r.upc.empty())
                if (const auto b = p.s_->basePrices.find(BuilderIndex::codeKey(r.upc)); b != p.s_->basePrices.end())
                    r.base = b->second;
            r.soldOut = soldOutOf(it);
            if (const auto s = m.firstSeen.find(it.id); s != m.firstSeen.end())
                r.firstSeen = s->second;
            for (const std::string &b : it.barcodes)
                r.codes.push_back(BuilderIndex::codeKey(b));
            x.rowOfId[r.id] = std::uint32_t(x.rows.size());
            x.rows.push_back(std::move(r));
        }
        return x;
    }

    static mass::Filter filterOf(const PosService &p, const PosService::MassView &v)
    {
        mass::Filter f;
        f.find = ss(v.find.trimmed().toLower());
        f.categoryId = ss(v.category);
        for (const QString &s : v.path)
            f.path.push_back(ss(s));
        f.noCategory = v.noCategory;
        f.newSince = v.newDays > 0 ? p.now() - std::int64_t(v.newDays) * 86'400'000 : 0;
        f.rangeField = mass::fieldFrom(ss(v.field));
        f.from = ss(v.from.trimmed());
        f.to = ss(v.to.trimmed());
        if (v.chosenOnly)
            f.only = &v.chosen;
        return f;
    }

    static mass::Field sortOf(const PosService::MassView &v)
    {
        return mass::fieldFrom(ss(v.sort)).value_or(mass::Field::Name);
    }

    // The chosen items in the grid's order (all of them, whatever the grid is narrowed to).
    static std::vector<std::uint32_t> chosenRows(const PosService &p)
    {
        PosService::MassView &v = view(p);
        const Shared::Rows &x = rows(p, v.scope);
        mass::Filter f;
        f.only = &v.chosen;
        return mass::pick(x.rows, f, sortOf(v), v.desc);
    }

    // --- an item's fields as a change set keeps them ---
    static std::string valueOf(const std::vector<MenuCategory> &cats, const MenuItem &m, const std::string &field)
    {
        if (field == "price")
            return mass::centsText(m.price.cents());
        if (field == "cost")
            return costOf(m) > 0 ? mass::centsText(costOf(m)) : std::string();
        if (field == "category") {
            for (const MenuCategory &c : cats)
                if (c.id == m.family)
                    return c.name;
            return m.family;
        }
        if (field == "taxClass")
            return toString(m.taxClass);
        if (field == "soldOut")
            return soldOutOf(m) ? "yes" : "no";
        if (field == "buttonColor")
            return m.buttonColor;
        if (field == "noDiscount")
            return m.noDiscount ? "yes" : "no";
        return {};
    }

    static const MenuCategory *categoryNamed(const std::vector<MenuCategory> &cats, const std::string &name)
    {
        for (const MenuCategory &c : cats)
            if (QString::compare(qs(c.name), qs(name), Qt::CaseInsensitive) == 0)
                return &c;
        for (const MenuCategory &c : cats)
            if (c.id == name)
                return &c;
        return nullptr;
    }

    // A value this store can take for the field ("" if fine, else why not).
    static std::string problem(const std::vector<MenuCategory> &cats, const std::string &field, const std::string &value)
    {
        if (field == "price")
            return mass::centsFrom(value) < 0 ? "not a value" : "";
        if (field == "cost")
            return value.empty() || mass::centsFrom(value) >= 0 ? "" : "not a value";
        if (field == "category")
            return categoryNamed(cats, value) ? "" : "no such category";
        if (field == "taxClass")
            return std::ranges::any_of(kTaxClasses, [&](TaxClass t) { return toString(t) == value; }) ? "" : "not a value";
        if (field == "soldOut" || field == "noDiscount")
            return value == "yes" || value == "no" ? "" : "not a value";
        if (field == "buttonColor")
            return value.empty() || (value.size() == 7 && value[0] == '#') ? "" : "not a value";
        return "not a value";
    }

    static void setValue(const std::vector<MenuCategory> &cats, MenuItem &m, const std::string &field, const std::string &value)
    {
        if (field == "price") {
            m.price = Money::fromCents(mass::centsFrom(value));
        } else if (field == "cost") {
            setCost(m, value.empty() ? 0 : mass::centsFrom(value));
        } else if (field == "category") {
            const MenuCategory *to = categoryNamed(cats, value);
            if (!to || to->id == m.family)
                return;
            // As the card moves one: its kitchen comes along only if it was the old category's.
            for (const MenuCategory &old : cats)
                if (old.id == m.family) {
                    if (m.printer == (old.printer.empty() ? "kitchen" : old.printer) && !to->printer.empty())
                        m.printer = to->printer;
                    if (m.station == old.station)
                        m.station = to->station;
                }
            m.family = to->id;
        } else if (field == "taxClass") {
            m.taxClass = taxClassFromString(value);
        } else if (field == "soldOut") {
            m.available = value != "yes";
            m.soldOutToday = false;   // decided here: until changed here (as the card)
        } else if (field == "buttonColor") {
            m.buttonColor = value;
        } else if (field == "noDiscount") {
            m.noDiscount = value == "yes";
        }
    }

    // --- planning ---
    static void addCascades(const PosService &p, const Shared::Rows &x, std::vector<mass::Change> &changes)
    {
        // A box's cost changed: its singles' too (their cost from it, as the card does), unless they're in it.
        std::unordered_set<std::string> in;
        for (const mass::Change &c : changes)
            if (c.field == "cost")
                in.insert(c.itemId);
        std::vector<mass::Change> more;
        for (const mass::Change &c : changes) {
            if (c.field != "cost" || c.upc.empty() || c.after.empty())
                continue;
            const auto kids = x.childrenOf.find(BuilderIndex::codeKey(c.upc));
            if (kids == x.childrenOf.end())
                continue;
            const std::int64_t cost = mass::centsFrom(c.after);
            for (std::uint32_t k : kids->second) {
                const MenuItem &child = p.s_->menu[k];
                if (in.contains(child.id))
                    continue;
                double div = 1;
                for (const RecipeLine &rl : child.recipe)
                    if (rl.ingredientId.starts_with("__parent__:")
                        && BuilderIndex::codeKey(rl.ingredientId.substr(11)) == BuilderIndex::codeKey(c.upc))
                        div = std::max(1.0, rl.quantity);
                const std::int64_t now = std::llround(double(cost) / div);
                if (now == costOf(child))
                    continue;
                in.insert(child.id);
                more.push_back({child.barcodes.empty() ? std::string() : child.barcodes.front(), child.id, child.name, "cost",
                                costOf(child) > 0 ? mass::centsText(costOf(child)) : std::string(), mass::centsText(now)});
            }
        }
        changes.insert(changes.end(), more.begin(), more.end());
    }

    static QString roundLabel(mass::Round r)
    {
        switch (r) {
        case mass::Round::None: return {};
        case mass::Round::Up5: return PosService::tr("round 5 up");
        case mass::Round::Up9: return PosService::tr("round 9 up");
        case mass::Round::Near9: return PosService::tr("round to nearest 9");
        case mass::Round::Up5or9: return PosService::tr("round 5 or 9 up");
        case mass::Round::Up49or99: return PosService::tr("round up to .49 or .99");
        }
        return {};
    }

    static void changed(PosService &p, PosService::MassView &v)
    {
        ++v.ticks;
        // (this screen's view only: the item lists' caches stay as they are)
        p.builderViewChanging_ = true;
        emit p.adminChanged();
        p.builderViewChanging_ = false;
    }

    static bool mayLook(PosService &p)
    {
        if (!p.user())
            return p.fail(PosService::tr("Log in first."));
        if (!p.permissionsOf(*p.user()).contains(perm::Manager))
            return p.fail(PosService::tr("Mass Updates is for managers."));
        return true;
    }

    // --- change sets ---
    static QVariantMap setFile(const PosService &p, const Shared::SetInfo &i, const std::vector<mass::Change> &changes)
    {
        QVariantList list;
        list.reserve(qsizetype(changes.size()));
        for (const mass::Change &c : changes)
            list.append(changeMap(c));
        return {{u"format"_s, QString::fromLatin1(kFormat)}, {u"version"_s, 1}, {u"id"_s, i.id}, {u"store"_s, p.storeName()},
                {u"label"_s, i.label}, {u"who"_s, i.who}, {u"why"_s, i.why}, {u"at"_s, i.at}, {u"from"_s, i.from},
                {u"undoOf"_s, i.undoOf}, {u"changes"_s, list}};
    }

    static void loadSets(PosService &p)
    {
        Shared &m = shared(p);
        if (m.setsLoaded)
            return;
        m.setsLoaded = true;
        if (dir(p).isEmpty())
            return;
        const QDir d(QDir(dir(p)).filePath(u"change-sets"_s));
        for (const QString &name : d.entryList({u"*.vtchanges.json"_s}, QDir::Files, QDir::Name)) {
            QFile f(d.filePath(name));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QVariantMap s = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
            if (s.value(u"format"_s).toString() != QLatin1String(kFormat))
                continue;
            m.sets.push_back({s.value(u"id"_s).toString(), s.value(u"label"_s).toString(), s.value(u"who"_s).toString(),
                              s.value(u"why"_s).toString(), s.value(u"from"_s).toString(), d.filePath(name),
                              s.value(u"undoOf"_s).toString(), s.value(u"at"_s).toLongLong(),
                              int(s.value(u"changes"_s).toList().size()), false});
        }
        std::ranges::sort(m.sets, {}, &Shared::SetInfo::at);
        for (const Shared::SetInfo &s : m.sets)
            if (!s.undoOf.isEmpty())
                for (Shared::SetInfo &o : m.sets)
                    if (o.id == s.undoOf)
                        o.undone = true;
    }

    // Applies `changes` (each only if the item still has its `before`): saved, in the Changes report, kept
    // as a change set. The ones applied; `skipped` gets the rest.
    static std::vector<mass::Change> commit(PosService &p, const QString &label, const QString &why, const QString &from,
                                            const std::vector<mass::Change> &changes, const QString &undoOf,
                                            std::vector<mass::Skip> &skipped)
    {
        const std::vector<MenuCategory> cats = p.s_->categories();
        auto &menu = p.s_->menu;
        std::unordered_map<std::string, std::uint32_t> placeOf;
        placeOf.reserve(menu.size());
        for (std::size_t i = 0; i < menu.size(); ++i)
            placeOf.emplace(menu[i].id, std::uint32_t(i));
        std::vector<mass::Change> done;
        std::unordered_set<std::uint32_t> touched;
        int logged = 0;
        const QString area = from.isEmpty() || from == p.storeName() ? PosService::tr("Menu") : PosService::tr("Menu, from %1").arg(from);
        for (const mass::Change &c : changes) {
            const auto at = placeOf.find(c.itemId);
            if (at == placeOf.end()) {
                skipped.push_back({c.upc, c.name, "not found"});
                continue;
            }
            MenuItem &m = menu[at->second];
            const std::string now = valueOf(cats, m, c.field);
            if (now == c.after)
                continue;   // (already so)
            if (now != c.before || !problem(cats, c.field, c.after).empty()) {
                skipped.push_back({c.upc, c.name, now != c.before ? "changed since" : problem(cats, c.field, c.after)});
                continue;
            }
            setValue(cats, m, c.field, c.after);
            touched.insert(at->second);
            done.push_back(c);
            if (++logged <= kLogged) {
                const bool money = c.field == "price" || c.field == "cost";
                const auto shown = [&](const std::string &v) {
                    return money && !v.empty() ? p.format(Money::fromCents(mass::centsFrom(v))) : qs(v);
                };
                p.logChange(area, qs(m.name), fieldLabel(c.field), shown(c.before), shown(c.after));
            }
        }
        if (logged > kLogged)
            p.logChange(area, QString(), QString(), QString(), PosService::tr("%1 (and %2 more changes)").arg(label).arg(logged - kLogged));
        if (done.empty())
            return done;
        if (p.s_->sink)
            for (std::uint32_t i : touched)
                p.s_->sink->saveMenuItem(menu[i], int(i));
        // Kept: in memory, and as a file beside the database.
        Shared &m = shared(p);
        loadSets(p);
        Shared::SetInfo info;
        info.at = p.now();
        info.id = QDateTime::fromMSecsSinceEpoch(info.at).toString(u"yyyyMMdd-HHmmss"_s) + u'-'
                  + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        info.label = label;
        info.who = p.user() ? qs(p.user()->name) : p.terminalName();
        info.why = why;
        info.from = from.isEmpty() ? p.storeName() : from;
        info.undoOf = undoOf;
        info.count = int(done.size());
        const QVariantMap file = setFile(p, info, done);
        if (!dir(p).isEmpty()) {
            const QString d = QDir(dir(p)).filePath(u"change-sets"_s);
            QDir().mkpath(d);
            info.file = QDir(d).filePath(info.id + u".vtchanges.json"_s);
            QSaveFile f(info.file);
            if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(QJsonObject::fromVariantMap(file)).toJson(QJsonDocument::Compact)) < 0
                || !f.commit())
                qWarning().noquote() << "Mass Updates: could not save the change set" << info.file << f.errorString();
        } else {
            m.memory.insert(info.id, file);
        }
        if (!undoOf.isEmpty())
            for (Shared::SetInfo &o : m.sets)
                if (o.id == undoOf)
                    o.undone = true;
        m.sets.push_back(info);
        // Made here (not from another store, never sent back: no loops): to the stores ticked for price changes.
        if (from.isEmpty())
            p.sendChangeSetToStores(file);
        if (undoOf.isEmpty()) {
            m.lastId = info.id;
            m.lastLabel = label;
            m.lastChanges = done;
            m.lastUndone = false;
        } else {
            m.lastUndone = true;
        }
        p.menuChanged();
        return done;
    }

    // --- the screen's state ---
    static QVariantMap state(const PosService &p)
    {
        PosService::MassView &v = view(p);
        const Shared::Rows &x = rows(p, v.scope);
        Shared &m = shared(p);
        const bool canApply = p.mayChangePriceBook();
        const std::vector<std::uint32_t> picked = mass::pick(x.rows, filterOf(p, v), sortOf(v), v.desc);
        const int count = int(picked.size());
        const int pages = std::max(1, (count + kPage - 1) / kPage);
        const int page = std::clamp(v.page, 0, pages - 1);
        QVariantList rowList;
        for (int k = page * kPage; k < std::min(count, (page + 1) * kPage); ++k) {
            const mass::Row &r = x.rows[picked[std::size_t(k)]];
            const double mg = r.margin();
            rowList.append(QVariantMap{{u"id"_s, qs(r.id)}, {u"upc"_s, qs(r.upc)}, {u"name"_s, qs(r.name)},
                                       {u"category"_s, r.noCategory && r.category.empty() ? PosService::tr("(none)") : qs(r.category)},
                                       {u"brand"_s, qs(r.brand)}, {u"group"_s, qs(r.group)}, {u"sub"_s, qs(r.sub)},
                                       {u"cost"_s, r.cost > 0 ? qs(mass::centsText(r.cost)) : QString()},
                                       {u"price"_s, qs(mass::centsText(r.price))},
                                       {u"base"_s, r.base >= 0 ? qs(mass::centsText(r.base)) : QString()},
                                       {u"margin"_s, std::isfinite(mg) ? QString::number(mg, 'f', 1) : QString()},
                                       {u"soldOut"_s, r.soldOut}, {u"chosen"_s, v.chosen.contains(r.id)}});
        }
        int chosen = 0;
        for (const std::string &id : v.chosen)
            chosen += x.rowOfId.contains(id) ? 1 : 0;
        // The tree's choices: in the category picked (or all), each level under the one above it.
        std::set<std::string> brands, groups, subs;
        int noCategory = 0;
        for (const mass::Row &r : x.rows) {
            noCategory += r.noCategory ? 1 : 0;
            if (!v.category.isEmpty() && r.categoryId != ss(v.category))
                continue;
            if (!r.brand.empty())
                brands.insert(r.brand);
            if (v.path.size() >= 1 && r.brand == ss(v.path[0])) {
                if (!r.group.empty())
                    groups.insert(r.group);
                if (v.path.size() >= 2 && r.group == ss(v.path[1]) && !r.sub.empty())
                    subs.insert(r.sub);
            }
        }
        const auto list = [](const std::set<std::string> &s) {
            QStringList out;
            for (const std::string &e : s)
                out << qs(e);
            return out;
        };
        QVariantList categories;
        for (const auto &[id, name] : x.categories)
            categories.append(QVariantMap{{u"id"_s, qs(id)}, {u"name"_s, qs(name)}});
        QVariantMap out{{u"active"_s, true}, {u"canApply"_s, canApply}, {u"scope"_s, qs(v.scope)},
                        {u"total"_s, int(x.rows.size())}, {u"count"_s, count}, {u"chosen"_s, chosen},
                        {u"page"_s, page}, {u"pages"_s, pages}, {u"pageSize"_s, kPage}, {u"sort"_s, v.sort}, {u"desc"_s, v.desc},
                        {u"rows"_s, rowList}, {u"categories"_s, categories}, {u"brands"_s, list(brands)},
                        {u"groups"_s, list(groups)}, {u"subs"_s, list(subs)}, {u"noCategoryCount"_s, noCategory},
                        {u"view"_s, QVariantMap{{u"find"_s, v.find}, {u"category"_s, v.category}, {u"path"_s, v.path},
                                               {u"noCategory"_s, v.noCategory}, {u"newDays"_s, v.newDays},
                                               {u"field"_s, v.field}, {u"from"_s, v.from}, {u"to"_s, v.to},
                                               {u"chosenOnly"_s, v.chosenOnly}}},
                        {u"message"_s, v.message}};
        // The price source and this store's price zone (Mass Updates > Price zone…).
        const QString source = qs(xfer::storeKey(p.s_->settings.storePricesFrom.empty() ? p.s_->basePricesFrom
                                                                                       : p.s_->settings.storePricesFrom));
        out.insert(u"priceSource"_s, source);
        out.insert(u"baseLabel"_s, source.isEmpty() ? QString() : PosService::tr("%1 price").arg(source));
        out.insert(u"zone"_s, QVariantMap{{u"name"_s, qs(p.s_->settings.priceZone.name)},
                                          {u"markup"_s, p.s_->settings.priceZone.markupPercent},
                                          {u"round"_s, qs(p.s_->settings.priceZone.round)}});
        out.insert(u"basesKnown"_s, int(p.s_->basePrices.size()));
        out.insert(u"waitingPrices"_s, int(p.s_->pricesWaiting.size()));
        if (v.preview) {
            const auto &pv = *v.preview;
            const int n = int(pv.changes.size());
            const int pp = std::max(1, (n + kPreviewPage - 1) / kPreviewPage);
            const int at = std::clamp(pv.page, 0, pp - 1);
            QVariantList lines;
            for (int k = at * kPreviewPage; k < std::min(n, (at + 1) * kPreviewPage); ++k) {
                const mass::Change &c = pv.changes[std::size_t(k)];
                QVariantMap l = changeMap(c);
                l.insert(u"fieldLabel"_s, fieldLabel(c.field));
                if (const auto b = pv.bases.find(BuilderIndex::codeKey(c.upc)); b != pv.bases.end())
                    l.insert(u"base"_s, p.format(Money::fromCents(b->second)));
                lines.append(l);
            }
            QVariantList skipped;
            for (const mass::Skip &s : pv.skipped)
                if (skipped.size() < 500)
                    skipped.append(QVariantMap{{u"upc"_s, qs(s.upc)}, {u"item"_s, qs(s.name)}, {u"why"_s, whyLabel(s.why)}});
            QStringList notFound;
            for (const mass::Skip &s : pv.skipped)
                if (s.why == "not found")
                    notFound << qs(s.upc);
            std::set<std::string> items;
            for (const mass::Change &c : pv.changes)
                items.insert(c.itemId);
            out.insert(u"preview"_s, QVariantMap{{u"label"_s, pv.label}, {u"why"_s, pv.why}, {u"from"_s, pv.from},
                                                 {u"kind"_s, pv.kind}, {u"count"_s, n}, {u"items"_s, int(items.size())},
                                                 {u"lines"_s, lines}, {u"page"_s, at}, {u"pages"_s, pp},
                                                 {u"skipped"_s, skipped}, {u"skippedCount"_s, int(pv.skipped.size())},
                                                 {u"notFound"_s, notFound}});
        }
        if (!m.lastId.isEmpty())
            out.insert(u"last"_s, QVariantMap{{u"id"_s, m.lastId}, {u"label"_s, m.lastLabel},
                                              {u"count"_s, int(m.lastChanges.size())}, {u"undone"_s, m.lastUndone}});
        loadSets(const_cast<PosService &>(p));
        QVariantList sets;
        for (auto it = m.sets.rbegin(); it != m.sets.rend() && sets.size() < 50; ++it)
            sets.append(QVariantMap{{u"id"_s, it->id}, {u"label"_s, it->label}, {u"who"_s, it->who}, {u"why"_s, it->why},
                                    {u"from"_s, it->from}, {u"count"_s, it->count}, {u"undone"_s, it->undone},
                                    {u"undo"_s, !it->undoOf.isEmpty()},
                                    {u"when"_s, QLocale().toString(QDateTime::fromMSecsSinceEpoch(it->at), u"MMM d, yyyy h:mm AP"_s)}});
        out.insert(u"sets"_s, sets);
        return out;
    }
};

// --- PosService ----------------------------------------------------------------------------------------

void PosService::massRememberItems()
{
    // Only once Mass Updates has been opened, or the menu has grown since (cheap: a look at its size).
    if (s_->mass || !s_->massUpdateDir.isEmpty())
        MassImpl::remember(*this);
}

bool PosService::massView(const QVariantMap &view)
{
    if (!MassImpl::mayLook(*this))
        return false;
    MassView &v = MassImpl::view(*this);
    v.active = true;
    const QString scope = view.value(u"scope"_s).toString().trimmed().toLower();
    v.scope = validScope(ss(scope)) ? ss(scope) : std::string(kScopeRetail);
    v.find = view.value(u"find"_s).toString();
    v.category = view.value(u"category"_s).toString();
    v.path = view.value(u"path"_s).toStringList().mid(0, 3);
    while (!v.path.isEmpty() && v.path.last().isEmpty())
        v.path.removeLast();
    v.noCategory = view.value(u"noCategory"_s).toBool();
    v.newDays = std::max(0, view.value(u"newDays"_s).toInt());
    v.field = mass::fieldFrom(ss(view.value(u"field"_s).toString())) ? view.value(u"field"_s).toString() : QString();
    v.from = view.value(u"from"_s).toString();
    v.to = view.value(u"to"_s).toString();
    v.chosenOnly = view.value(u"chosenOnly"_s).toBool();
    if (const QString sort = view.value(u"sort"_s).toString(); mass::fieldFrom(ss(sort)))
        v.sort = sort;
    v.desc = view.value(u"desc"_s).toBool();
    v.page = std::max(0, view.value(u"page"_s).toInt());
    MassImpl::changed(*this, v);
    return true;
}

bool PosService::massClose()
{
    if (mass_) {
        mass_->active = false;
        mass_->preview.reset();
        MassImpl::changed(*this, *mass_);
    }
    return true;
}

bool PosService::massChoose(const QStringList &ids, bool on)
{
    if (!MassImpl::mayLook(*this))
        return false;
    MassView &v = MassImpl::view(*this);
    for (const QString &id : ids) {
        if (on)
            v.chosen.insert(ss(id));
        else
            v.chosen.erase(ss(id));
    }
    MassImpl::changed(*this, v);
    return true;
}

bool PosService::massChooseAll()
{
    if (!MassImpl::mayLook(*this))
        return false;
    MassView &v = MassImpl::view(*this);
    const auto &x = MassImpl::rows(*this, v.scope);
    for (std::uint32_t i : mass::pick(x.rows, MassImpl::filterOf(*this, v), mass::Field::Name, false))
        v.chosen.insert(x.rows[i].id);
    MassImpl::changed(*this, v);
    return true;
}

bool PosService::massChooseNone()
{
    if (!MassImpl::mayLook(*this))
        return false;
    MassView &v = MassImpl::view(*this);
    v.chosen.clear();
    v.chosenOnly = false;
    MassImpl::changed(*this, v);
    return true;
}

bool PosService::massChooseOnly(const QStringList &ids)
{
    if (!MassImpl::mayLook(*this))
        return false;
    MassView &v = MassImpl::view(*this);
    v.chosen.clear();
    for (const QString &id : ids)
        v.chosen.insert(ss(id));
    v.chosenOnly = !ids.isEmpty();
    v.page = 0;
    MassImpl::changed(*this, v);
    return true;
}

bool PosService::massPreview(const QVariantMap &action)
{
    if (!MassImpl::mayLook(*this))
        return false;
    MassView &v = MassImpl::view(*this);
    const std::string kind = ss(action.value(u"kind"_s).toString());
    // Costs are the price book's to see changed too (an admin, or an admin's PIN).
    if (kind == "cost" && !requirePriceBook([] { return true; }))
        return false;
    if (kind == "zone")
        return massZonePreview();
    const std::vector<std::uint32_t> chosen = MassImpl::chosenRows(*this);
    if (chosen.empty())
        return fail(tr("Tick the items to change first."));
    const auto &x = MassImpl::rows(*this, v.scope);
    MassView::Preview pv;
    pv.kind = u"here"_s;
    pv.why = action.value(u"why"_s).toString().trimmed();
    const auto round = mass::roundFrom(ss(action.value(u"round"_s).toString()));
    if (!round)
        return fail(tr("Choose how to round."));
    bool ok = true;
    const double amount = kind == "field" || kind == "copy" ? 0 : amountFrom(action.value(u"amount"_s), &ok);
    if (!ok)
        return fail(tr("Type an amount, like 10 or 1.25."));
    const QString symbol = currencySymbol();
    mass::Action a;
    a.kind = kind;
    a.amount = amount;
    a.round = *round;
    const QString roundText = MassImpl::roundLabel(*round);
    const auto withRound = [&](const QString &s) { return roundText.isEmpty() ? s : QString(s + u" · "_s + roundText); };
    if (kind == "margin") {
        if (amount < 0 || amount >= 100)
            return fail(tr("The margin must be at least 0 and under 100%."));
        pv.label = withRound(tr("Margin %1%").arg(QString::number(amount, 'g', 6)));
    } else if (kind == "price" || kind == "cost") {
        const auto how = mass::howFrom(ss(action.value(u"how"_s).toString()));
        if (!how)
            return fail(tr("Choose Add $, Add % or Set."));
        a.how = *how;
        if (*how == mass::How::Set && amount < 0)
            return fail(tr("Type an amount, like 10 or 1.25."));
        if (kind == "cost")
            a.round = mass::Round::None;
        const QString what = kind == "price" ? tr("Price") : tr("Cost");
        QString by;
        if (*how == mass::How::AddPercent)
            by = QString(amount >= 0 ? u"+"_s : QString()) + QString::number(amount, 'g', 6) + u"%"_s;
        else if (*how == mass::How::AddDollars)
            by = QString(amount >= 0 ? u"+"_s : u"-"_s) + symbol + QString::number(std::abs(amount), 'f', 2);
        else
            by = tr("set to %1").arg(symbol + QString::number(amount, 'f', 2));
        pv.label = kind == "price" ? withRound(what + u" "_s + by) : QString(what + u" "_s + by);
    } else if (kind == "copy") {
        const std::string from = ss(action.value(u"from"_s).toString().trimmed().toLower());
        std::int64_t at = -1;
        if (const auto it = x.rowOfId.find(ss(action.value(u"from"_s).toString().trimmed())); it != x.rowOfId.end())
            at = it->second;
        for (std::size_t i = 0; at < 0 && !from.empty() && i < x.rows.size(); ++i)
            if (mass::found(x.rows[i], from) && (std::ranges::all_of(from, [](char c) { return c >= '0' && c <= '9'; })
                                                 || x.rows[i].lowerName == from || BuilderIndex::asciiLower(x.rows[i].number) == from))
                at = std::int64_t(i);
        if (at < 0)
            return fail(tr("Type or scan the UPC of the item to copy the price from."));
        a.from = at;
        pv.label = tr("Copy the price of %1 (%2)").arg(qs(x.rows[std::size_t(at)].name), format(Money::fromCents(x.rows[std::size_t(at)].price)));
    } else if (kind == "field") {
        const std::string field = ss(action.value(u"field"_s).toString());
        std::string value = ss(action.value(u"value"_s).toString().trimmed());
        if (!fieldKeys().contains(qs(field)) || field == "price" || field == "cost")
            return fail(tr("Choose what to change."));
        const std::vector<MenuCategory> cats = s_->categories();
        if (field == "category")   // (picked by id: kept by name, which another store has too)
            for (const MenuCategory &c : cats)
                if (c.id == value)
                    value = c.name;
        if (const std::string why = MassImpl::problem(cats, field, value); !why.empty())
            return fail(tr("Choose a value for %1.").arg(fieldLabel(field)));
        for (std::uint32_t i : chosen) {
            const mass::Row &r = x.rows[i];
            const std::string was = MassImpl::valueOf(cats, s_->menu[r.at], field);
            if (was != value)
                pv.changes.push_back({r.upc, r.id, r.name, field, was, value});
        }
        pv.label = fieldLabel(field) + u" → "_s + (value.empty() ? tr("(none)") : qs(value));
    } else {
        return fail(tr("Choose what to change."));
    }
    if (kind != "field") {
        mass::Plan plan = mass::plan(x.rows, chosen, a);
        pv.changes = std::move(plan.changes);
        pv.skipped = std::move(plan.skipped);
        if (kind == "cost")
            MassImpl::addCascades(*this, x, pv.changes);
    }
    v.preview = std::move(pv);
    v.message = v.preview->changes.empty() ? tr("Nothing would change.") : QString();
    MassImpl::changed(*this, v);
    return true;
}

bool PosService::massPreviewPage(int page)
{
    MassView &v = MassImpl::view(*this);
    if (!v.preview)
        return false;
    v.preview->page = std::max(0, page);
    MassImpl::changed(*this, v);
    return true;
}

bool PosService::massCancel()
{
    MassView &v = MassImpl::view(*this);
    v.preview.reset();
    v.message.clear();
    MassImpl::changed(*this, v);
    return true;
}

bool PosService::massApply()
{
    if (!MassImpl::mayLook(*this))
        return false;
    MassView &v = MassImpl::view(*this);
    if (!v.preview || v.preview->changes.empty())
        return fail(tr("Preview a change first."));
    if (!requirePriceBook([] { return true; }))
        return false;
    MassView::Preview pv = std::move(*v.preview);
    v.preview.reset();
    if (!pv.newBases.empty()) {   // Price zone: the items' own prices kept as their base from now on
        loadBasePrices();
        for (const auto &[key, cents] : pv.newBases)
            s_->basePrices.try_emplace(key, cents);
        ++s_->basePricesRevision;
        saveBasePrices();
    }
    // A retail price changed here at a store that takes its prices from another: the next change from there wins.
    const bool overwritten = !s_->settings.storePricesFrom.empty() && pv.kind == u"here"
                             && std::ranges::any_of(pv.changes, [](const mass::Change &c) { return c.field == "price"; });
    std::vector<mass::Skip> skipped;
    const std::vector<mass::Change> done = MassImpl::commit(*this, pv.label, pv.why, pv.kind == u"import" ? pv.from : QString(),
                                                           pv.changes, {}, skipped);
    v.message = skipped.empty() ? tr("%n item(s) changed.", "", int(done.size()))
                                : tr("%n item(s) changed; %1 left as they were (changed since).", "", int(done.size())).arg(int(skipped.size()));
    if (overwritten)
        v.message += u' ' + tr("Note: %1's next price change for these items will replace these prices (Store Settings > Take retail prices from).")
                                .arg(qs(xfer::storeKey(s_->settings.storePricesFrom)));
    emit notice(v.message);
    MassImpl::changed(*this, v);
    return true;
}

bool PosService::massUndo()
{
    if (!MassImpl::mayLook(*this))
        return false;
    auto &m = MassImpl::shared(*this);
    if (m.lastId.isEmpty() || m.lastUndone)
        return fail(tr("Nothing to undo."));
    if (!requirePriceBook([] { return true; }))
        return false;
    MassView &v = MassImpl::view(*this);
    std::vector<mass::Skip> skipped;
    const QString id = m.lastId;
    const std::vector<mass::Change> done = MassImpl::commit(*this, tr("Undone: %1").arg(m.lastLabel), QString(), QString(),
                                                           mass::reversed(m.lastChanges), id, skipped);
    m.lastUndone = true;
    v.message = skipped.empty() ? tr("Undone: %n item(s) as they were.", "", int(done.size()))
                                : tr("Undone: %n item(s) as they were; %1 changed since, left as they are.", "", int(done.size())).arg(int(skipped.size()));
    emit notice(v.message);
    MassImpl::changed(*this, v);
    return true;
}

bool PosService::massImport(const QVariantMap &file)
{
    if (!MassImpl::mayLook(*this))
        return false;
    if (file.value(u"format"_s).toString() != QLatin1String(kFormat))
        return fail(tr("That isn't a ViewTouch change set file."));
    if (!requirePriceBook([] { return true; }))
        return false;
    MassView &v = MassImpl::view(*this);
    const auto &x = MassImpl::rows(*this, v.scope);
    const std::vector<MenuCategory> cats = s_->categories();
    MassView::Preview pv;
    pv.kind = u"import"_s;
    pv.from = file.value(u"store"_s).toString();
    pv.label = file.value(u"label"_s).toString();
    pv.why = file.value(u"why"_s).toString();
    std::set<std::string> notFound;
    for (const QVariant &cv : file.value(u"changes"_s).toList()) {
        const QVariantMap c = cv.toMap();
        const std::string upc = ss(c.value(u"upc"_s).toString().trimmed()), field = ss(c.value(u"field"_s).toString());
        const std::string after = ss(c.value(u"after"_s).toString()), name = ss(c.value(u"item"_s).toString());
        if (!fieldKeys().contains(qs(field)))
            continue;
        const std::int64_t at = x.upc.find(upc);
        if (at < 0) {
            if (notFound.insert(upc.empty() ? name : upc).second)
                pv.skipped.push_back({upc, name, "not found"});
            continue;
        }
        const MenuItem &m = s_->menu[std::size_t(at)];
        if (const std::string why = MassImpl::problem(cats, field, after); !why.empty()) {
            pv.skipped.push_back({upc, m.name, why});
            continue;
        }
        const std::string now = MassImpl::valueOf(cats, m, field);
        if (now != after)
            pv.changes.push_back({upc, m.id, m.name, field, now, after});
    }
    v.preview = std::move(pv);
    v.message = v.preview->changes.empty() ? tr("Nothing would change.") : QString();
    MassImpl::changed(*this, v);
    return true;
}

QVariantMap PosService::massChangeSetFile(const QString &id)
{
    if (!MassImpl::mayLook(*this))
        return {};
    MassImpl::loadSets(*this);
    auto &m = MassImpl::shared(*this);
    if (const auto it = m.memory.constFind(id); it != m.memory.cend())
        return *it;
    for (const auto &s : m.sets)
        if (s.id == id && !s.file.isEmpty()) {
            QFile f(s.file);
            if (f.open(QIODevice::ReadOnly))
                return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
        }
    fail(tr("That change set is gone."));
    return {};
}

QStringList PosService::massChosenIds() const
{
    QStringList out;
    if (mass_)
        for (const std::string &id : mass_->chosen)
            out << qs(id);
    out.sort();
    return out;
}

// --- prices from another store, and this store's price zone (owner 2026-11-26) -------------------------

// The changes a change set from the price source makes here: retail items by UPC, prices under this store's
// price zone (costs and categories as sent), each one's base price noted for the preview.
std::vector<mass::Change> PosService::storeChanges(const QVariantMap &file, std::vector<mass::Skip> &skipped,
                                                   std::unordered_map<std::string, std::int64_t> *bases)
{
    const auto &x = MassImpl::rows(*this, kScopeRetail);
    const std::vector<MenuCategory> cats = s_->categories();
    const auto round = mass::roundFrom(s_->settings.priceZone.round).value_or(mass::Round::None);
    std::vector<mass::Change> out;
    std::set<std::string> notFound;
    for (const QVariant &cv : file.value(u"changes"_s).toList()) {
        const QVariantMap c = cv.toMap();
        const std::string upc = ss(c.value(u"upc"_s).toString().trimmed()), field = ss(c.value(u"field"_s).toString());
        std::string after = ss(c.value(u"after"_s).toString());
        if (field != "price" && field != "cost" && field != "category")
            continue;   // (the price book's prices, costs and departments only)
        const std::int64_t at = x.upc.find(upc);
        if (at < 0 || !itemIsRetail(s_->menu[std::size_t(at)])) {
            if (notFound.insert(upc).second)
                skipped.push_back({upc, ss(c.value(u"item"_s).toString()), "not found"});
            continue;
        }
        const MenuItem &m = s_->menu[std::size_t(at)];
        if (field == "price" && mass::centsFrom(after) >= 0) {
            const std::int64_t base = mass::centsFrom(after);
            after = mass::centsText(xfer::markedUp(base, s_->settings.priceZone.markupPercent, round));
            if (bases)
                (*bases)[BuilderIndex::codeKey(upc)] = base;
        }
        if (const std::string why = MassImpl::problem(cats, field, after); !why.empty()) {
            skipped.push_back({upc, m.name, why});
            continue;
        }
        const std::string now = MassImpl::valueOf(cats, m, field);
        if (now != after)
            out.push_back({upc, m.id, m.name, field, now, after});
    }
    return out;
}

void PosService::noteBasePrices(const QVariantMap &file, const QString &from)
{
    loadBasePrices();
    for (const QVariant &cv : file.value(u"changes"_s).toList()) {
        const QVariantMap c = cv.toMap();
        if (c.value(u"field"_s).toString() != u"price")
            continue;
        const std::int64_t cents = mass::centsFrom(ss(c.value(u"after"_s).toString()));
        const std::string key = BuilderIndex::codeKey(ss(c.value(u"upc"_s).toString()));
        if (cents >= 0 && !key.empty())
            s_->basePrices[key] = cents;
    }
    s_->basePricesFrom = ss(from);
    ++s_->basePricesRevision;
    saveBasePrices();
}

QString PosService::takeStoreChangeSet(const QVariantMap &file, QString *why)
{
    const QString from = qs(xfer::storeKey(ss(file.value(u"store"_s).toString())));
    const QString source = qs(xfer::storeKey(s_->settings.storePricesFrom));
    if (from.isEmpty() || from != source) {
        if (why)
            *why = source.isEmpty() ? tr("price changes from %1, but this store takes its prices from no store (Store Settings > Other stores)").arg(from)
                                    : tr("price changes from %1, but this store takes its prices from %2").arg(from, source);
        return {};
    }
    noteBasePrices(file, from);
    if (s_->settings.storePricesApply == "ask") {
        s_->pricesWaiting.push_back(file);
        emit notice(tr("Price changes from %1 are waiting: Menu Builder → Mass Updates.").arg(from));
        emit s_->adminChanged();
        return u"waiting"_s;
    }
    std::vector<mass::Skip> skipped;
    const std::vector<mass::Change> changes = storeChanges(file, skipped, nullptr);
    if (changes.empty())
        return {};
    std::vector<mass::Skip> later;
    const std::vector<mass::Change> done = MassImpl::commit(*this, tr("From %1: %2").arg(from, file.value(u"label"_s).toString()),
                                                           file.value(u"why"_s).toString(), from, changes, {}, later);
    emit notice(tr("Price changes from %1: %n item(s) changed.", "", int(done.size())).arg(from));
    return {};
}

bool PosService::massReviewWaiting()
{
    if (!MassImpl::mayLook(*this))
        return false;
    if (s_->pricesWaiting.empty())
        return fail(tr("No price changes are waiting."));
    MassView &v = MassImpl::view(*this);
    const QVariantMap file = s_->pricesWaiting.front();
    s_->pricesWaiting.erase(s_->pricesWaiting.begin());
    if (const QString f = file.value(u"_file"_s).toString(); !f.isEmpty()) {   // reviewed: kept with the others done
        QDir().mkpath(QFileInfo(f).dir().filePath(u"../done"_s));
        QFile::rename(f, QFileInfo(f).dir().filePath(u"../done/"_s + QFileInfo(f).fileName()));
    }
    MassView::Preview pv;
    pv.kind = u"import"_s;
    pv.from = qs(xfer::storeKey(ss(file.value(u"store"_s).toString())));
    pv.label = tr("From %1: %2").arg(pv.from, file.value(u"label"_s).toString());
    pv.why = file.value(u"why"_s).toString();
    pv.changes = storeChanges(file, pv.skipped, &pv.bases);
    v.preview = std::move(pv);
    v.message = v.preview->changes.empty() ? tr("Nothing would change.") : QString();
    MassImpl::changed(*this, v);
    return true;
}

bool PosService::massSetPriceZone(const QVariantMap &zone)
{
    if (!MassImpl::mayLook(*this))
        return false;
    bool ok = true;
    const double pct = amountFrom(zone.value(u"markup"_s, 0), &ok);
    if (!ok || pct < 0 || pct > 500)
        return fail(tr("Type the markup as a percent, like 0 or 10."));
    const QString round = zone.value(u"round"_s, u"none"_s).toString();
    if (round != u"none" && round != u"49or99up")
        return fail(tr("Choose how to round."));
    const QString name = zone.value(u"name"_s).toString().trimmed().left(40);
    if (!requirePriceBook([] { return true; }))
        return false;
    const PriceZone was = s_->settings.priceZone;
    PriceZone z{name.isEmpty() ? std::string("Base") : ss(name), pct, ss(round)};
    if (z == was)
        return true;
    s_->settings.priceZone = z;
    logChange(tr("Store"), tr("Price zone"), QString(),
              u"%1 +%2% %3"_s.arg(qs(was.name)).arg(was.markupPercent).arg(qs(was.round)),
              u"%1 +%2% %3"_s.arg(qs(z.name)).arg(z.markupPercent).arg(qs(z.round)));
    s_->saveSettings();
    MassView &v = MassImpl::view(*this);
    v.message = tr("Price zone %1: +%2%. New prices from %3 get it; Reapply to all retail items now to change the ones here.")
                    .arg(qs(z.name)).arg(z.markupPercent).arg(qs(xfer::storeKey(s_->settings.storePricesFrom)));
    MassImpl::changed(*this, v);
    return true;
}

// Price zone > Reapply to all retail items now: every retail item's price from its base (the price source's
// price; none received yet: its own price, kept as its base from Apply on) under the zone's rule.
bool PosService::massZonePreview()
{
    MassView &v = MassImpl::view(*this);
    loadBasePrices();
    const auto &x = MassImpl::rows(*this, kScopeRetail);
    const PriceZone &z = s_->settings.priceZone;
    const auto round = mass::roundFrom(z.round).value_or(mass::Round::None);
    MassView::Preview pv;
    pv.kind = u"zone"_s;
    pv.label = tr("Price zone %1: +%2%").arg(qs(z.name)).arg(z.markupPercent)
               + (round == mass::Round::None ? QString() : u" · "_s + MassImpl::roundLabel(round));
    int own = 0;
    for (const mass::Row &r : x.rows) {
        if (r.upc.empty())
            continue;
        const std::string key = BuilderIndex::codeKey(r.upc);
        std::int64_t base = r.base;
        if (base < 0) {
            base = r.price;
            pv.newBases.emplace_back(key, base);
            ++own;
        }
        pv.bases[key] = base;
        const std::int64_t now = xfer::markedUp(base, z.markupPercent, round);
        if (now != r.price)
            pv.changes.push_back({r.upc, r.id, r.name, "price", mass::centsText(r.price), mass::centsText(now)});
    }
    if (own > 0)
        pv.why = tr("%n item(s) had no price from %1 yet: their price now is their base (kept from Apply on).", "", own)
                     .arg(qs(xfer::storeKey(s_->settings.storePricesFrom)));
    v.preview = std::move(pv);
    v.message = v.preview->changes.empty() ? tr("Nothing would change.") : QString();
    MassImpl::changed(*this, v);
    return true;
}

QVariantMap PosService::massUpdate() const
{
    if (!mass_ || !mass_->active || !user() || !permissionsOf(*user()).contains(perm::Manager))
        return {};
    const MassView &v = *mass_;
    const auto &x = MassImpl::rows(*this, v.scope);
    const auto &m = MassImpl::shared(*this);
    const QString key = u"%1|%2|%3|%4|%5|%6|%7"_s.arg(x.generation).arg(v.ticks).arg(m.sets.size()).arg(qs(user()->id))
                            .arg(language()).arg(now() / 3'600'000).arg(m.lastUndone ? 1 : 0);
    if (v.key == key)
        return v.state;
    v.key = key;
    v.state = MassImpl::state(*this);
    return v.state;
}

} // namespace vt::app
