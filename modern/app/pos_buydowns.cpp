// PosService: manufacturer buydowns and promotions (owner 2026-11-25, after ILSA's "Buydown and Promotional
// Sales Definition" screen). The definitions list and its editor live on Menu Builder > Mass Updates >
// Buydowns / Promotions (WidgetBuydowns.qml): Add, Edit, Copy to New (the usual way: last quarter's, then its
// name, dates and amount changed), Apply / Abort, Delete, the items by UPC with their Part/Ctn, the reports
// (the Buydown Definition report, the Sold report) shown and printed one buydown at a time, a file for another
// store. Saving is the price book's (an admin, or an admin's PIN). The register: a running definition's line
// goes on an item when it is rung (stampNewLine -> buydownOnNewLine), the larger of two, a capped style only
// while it has units left (Definition::used counts them as checks close). The rules are core/buydown.hh.

#include "app/pos_json.hh"
#include "app/pos_service.hh"
#include "core/buydown.hh"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

constexpr const char *kFileFormat = "viewtouch-buydown";
constexpr int kMostItems = 5000;

// "2026-10-01 00:00" from the screen ("2026-10-01T00:00", "2026-10-01", "10/01/2026 12:00 AM"...).
std::string whenFrom(const QString &text, bool endOfDay)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return {};
    for (const QString &f : {u"yyyy-MM-dd HH:mm"_s, u"yyyy-MM-dd h:mm AP"_s, u"yyyy-MM-ddTHH:mm"_s, u"yyyy-MM-ddTHH:mm:ss"_s, u"MM/dd/yyyy h:mm AP"_s,
                             u"MM/dd/yyyy hh:mmAP"_s, u"M/d/yyyy h:mm AP"_s}) {
        const QDateTime d = QDateTime::fromString(t, f);
        if (d.isValid())
            return d.toString(u"yyyy-MM-dd HH:mm"_s).toStdString();
    }
    for (const QString &f : {u"yyyy-MM-dd"_s, u"MM/dd/yyyy"_s, u"M/d/yyyy"_s}) {
        const QDate d = QDate::fromString(t, f);
        if (d.isValid())
            return (d.toString(u"yyyy-MM-dd"_s) + (endOfDay ? u" 23:59"_s : u" 00:00"_s)).toStdString();
    }
    return "?";   // not a date: problems() says so
}

// "2026-10-01 00:00" -> "10/01/2026 12:00 AM" for the screen.
QString whenText(const std::string &w)
{
    if (w.empty())
        return {};
    const QDateTime d = QDateTime::fromString(qs(w), u"yyyy-MM-dd HH:mm"_s);
    return d.isValid() ? d.toString(u"MM/dd/yyyy h:mm AP"_s) : qs(w);
}

std::int64_t whenMs(const std::string &w)
{
    const QDateTime d = QDateTime::fromString(qs(w), u"yyyy-MM-dd HH:mm"_s);
    return d.isValid() ? d.toMSecsSinceEpoch() : 0;
}

QString partText(double p)
{
    if (p <= 0.0 || p >= 1.0)
        return u"0"_s;
    QString t = QString::number(p, 'f', 4);
    while (t.endsWith(u'0'))
        t.chop(1);
    return t;
}

QString itemKey(const BuydownItem &i) { return !i.itemId.empty() ? qs(i.itemId) : u"upc:"_s + qs(i.upc); }

} // namespace

// --- the register ----------------------------------------------------------------------------------------

std::string PosService::buydownNow() const
{
    return QDateTime::fromMSecsSinceEpoch(now()).toString(u"yyyy-MM-dd HH:mm"_s).toStdString();
}

std::map<std::string, int> PosService::buydownUnitsOpen() const
{
    std::vector<const Check *> open;
    for (const auto &[id, c] : s_->open)
        open.push_back(&c);
    return buydown::unitsOn(open);
}

void PosService::buydownOnNewLine(OrderLine &l) const
{
    buydown::clear(l);
    if (s_->settings.buydowns.empty() || l.isComment() || l.isGiftCard() || l.isFee())
        return;
    const MenuItem *m = findItem(qs(l.itemId));
    if (!m || m->isModifier)
        return;
    if (const auto hit = buydown::hitFor(s_->settings.buydowns, *m, buydownNow(), l.upc, buydownUnitsOpen()))
        buydown::apply(l, *hit, s_->settings.buydownTaxBefore);
}

void PosService::countBuydownUses(const Check &c, int by)
{
    if (c.training || s_->settings.buydowns.empty())
        return;
    bool any = false;
    for (const auto &[id, units] : buydown::unitsOn({&c}))
        for (Buydown &b : s_->settings.buydowns)
            if (b.id == id && b.capped() && units != 0) {
                b.used += by * units;
                any = true;
            }
    if (any)
        settingsChanged();
}

QVariantMap PosService::buydownPriceCheck(const MenuItem &m, const QString &code) const
{
    const auto hit = buydown::hitFor(s_->settings.buydowns, m, buydownNow(), ss(code), buydownUnitsOpen());
    if (!hit)
        return {};
    const Money each = std::min(hit->each, m.price);
    const QString text = qs(!hit->def->receiptText.empty() ? hit->def->receiptText : hit->def->name);
    return {{u"text"_s, text}, {u"amount"_s, format(each)}, {u"sale"_s, format(m.price - each)},
            {u"label"_s, tr("Sale price (with %1)").arg(text)}};
}

// --- old data --------------------------------------------------------------------------------------------

void PosShared::migrateBuydownTags()
{
    if (settings.buydownTagsMigrated)
        return;
    std::vector<std::size_t> tagged;
    for (std::size_t i = 0; i < menu.size(); ++i)
        if (std::ranges::any_of(menu[i].recipe, [](const RecipeLine &rl) { return rl.ingredientId.starts_with("__buydown"); }))
            tagged.push_back(i);
    if (tagged.empty())
        return;   // nothing to move (a look at the menu each start: cheap)
    buydown::migrateTags(menu, settings.buydowns, {}, now());
    settings.buydownTagsMigrated = true;
    if (sink) {
        for (std::size_t i : tagged)
            sink->saveMenuItem(menu[i], int(i));
        sink->saveSettings(settings);
    }
}

// --- the setup screen ------------------------------------------------------------------------------------

namespace {
// Words from core/buydown.hh, for the screens' languages.
[[maybe_unused]] const char *const kCoreWords[] = {
    QT_TR_NOOP("Give it a name."), QT_TR_NOOP("Type the amount per carton."), QT_TR_NOOP("The start date isn't a date."),
    QT_TR_NOOP("The end date isn't a date."), QT_TR_NOOP("It ends before it starts."),
    QT_TR_NOOP("Type the quantity: how many units get it."), QT_TR_NOOP("Old Style needs an end date."),
    QT_TR_NOOP("Date Range Only"), QT_TR_NOOP("Quantity Only"), QT_TR_NOOP("Quantity with Date Range"), QT_TR_NOOP("Old Style"),
};
} // namespace

bool PosService::buydownMayLook()
{
    if (!user())
        return fail(tr("Log in first."));
    if (!permissionsOf(*user()).contains(perm::Manager))
        return fail(tr("Buydowns are for managers."));
    return true;
}

bool PosService::buydownsOpen(bool on)
{
    if (on && !buydownMayLook())
        return false;
    bd_.open = on;
    if (!on) {
        bd_ = {};
    }
    emit sessionChanged();
    return true;
}

bool PosService::buydownNew(const QString &type)
{
    if (!buydownMayLook())
        return false;
    Buydown b;
    b.type = type == u"promo" ? "promo" : "buydown";
    b.number = buydown::nextNumber(s_->settings.buydowns);
    b.id = "bd-" + std::to_string(b.number);
    const QDate today = QDateTime::fromMSecsSinceEpoch(now()).date();
    b.start = (today.toString(u"yyyy-MM-dd"_s) + u" 00:00"_s).toStdString();
    b.end = (today.addMonths(3).addDays(-1).toString(u"yyyy-MM-dd"_s) + u" 23:00"_s).toStdString();
    b.taxBefore = false;   // "Apply taxes AFTER discount", as ILSA's always were
    bd_.open = true;
    bd_.draft = b;
    bd_.isNew = true;
    bd_.notFound.clear();
    bd_.from.clear();
    bd_.message.clear();
    emit sessionChanged();
    return true;
}

bool PosService::buydownEdit(const QString &id)
{
    if (!buydownMayLook())
        return false;
    const auto it = std::ranges::find(s_->settings.buydowns, ss(id), &Buydown::id);
    if (it == s_->settings.buydowns.end())
        return fail(tr("That buydown is gone."));
    bd_.open = true;
    bd_.draft = *it;
    bd_.isNew = false;
    bd_.notFound.clear();
    bd_.from.clear();
    bd_.message.clear();
    emit sessionChanged();
    return true;
}

bool PosService::buydownCopy(const QString &id)
{
    if (!buydownMayLook())
        return false;
    const auto it = std::ranges::find(s_->settings.buydowns, ss(id), &Buydown::id);
    if (it == s_->settings.buydowns.end())
        return fail(tr("That buydown is gone."));
    bd_.open = true;
    bd_.draft = buydown::copyOf(*it, s_->settings.buydowns);
    bd_.isNew = true;
    bd_.notFound.clear();
    bd_.from.clear();
    bd_.message = tr("A copy with the same items: change its name, dates and amount, then Apply.");
    emit sessionChanged();
    return true;
}

bool PosService::buydownSet(const QVariantMap &f)
{
    if (!bd_.draft)
        return fail(tr("Add or edit a buydown first."));
    Buydown &b = *bd_.draft;
    if (f.contains(u"type"_s))
        b.type = f.value(u"type"_s).toString() == u"promo" ? "promo" : "buydown";
    if (f.contains(u"name"_s))
        b.name = ss(f.value(u"name"_s).toString().trimmed().left(60));
    if (f.contains(u"receiptText"_s))
        b.receiptText = ss(f.value(u"receiptText"_s).toString().trimmed().left(40));
    if (f.contains(u"amount"_s)) {
        QString a = f.value(u"amount"_s).toString().trimmed();
        a.remove(u'$');
        bool ok = false;
        const double v = a.toDouble(&ok);
        if (!ok || v < 0 || v > 1000)
            return fail(tr("Type the amount per carton, like 4.10."));
        b.amount = Money::fromCents(std::llround(v * 100.0));
    }
    if (f.contains(u"taxBefore"_s))
        b.taxBefore = f.value(u"taxBefore"_s).toBool();
    if (f.contains(u"usePartCtn"_s))
        b.usePartCtn = f.value(u"usePartCtn"_s).toBool();
    if (f.contains(u"style"_s))
        b.style = std::clamp(f.value(u"style"_s).toInt(), 1, 4);
    if (f.contains(u"qty"_s))
        b.qty = std::clamp(f.value(u"qty"_s).toInt(), 0, 9'999'999);
    if (f.contains(u"start"_s))
        b.start = whenFrom(f.value(u"start"_s).toString(), false);
    if (f.contains(u"end"_s))
        b.end = whenFrom(f.value(u"end"_s).toString(), true);
    if (f.contains(u"manufacturer"_s))
        b.manufacturer = ss(f.value(u"manufacturer"_s).toString().trimmed().left(60));
    if (f.contains(u"rptCode"_s))
        b.rptCode = ss(f.value(u"rptCode"_s).toString().trimmed().left(30));
    if (f.contains(u"account"_s))
        b.account = ss(f.value(u"account"_s).toString().trimmed().left(30));
    bd_.message.clear();
    emit sessionChanged();
    return true;
}

bool PosService::buydownAddMenuItems(const std::vector<const MenuItem *> &items)
{
    if (!bd_.draft)
        return fail(tr("Add or edit a buydown first."));
    Buydown &b = *bd_.draft;
    int added = 0;
    for (const MenuItem *m : items) {
        if (!m || m->isModifier)
            continue;
        if (buydown::entryFor(b, *m))
            continue;   // already in it
        if (int(b.items.size()) >= kMostItems)
            break;
        b.items.push_back({m->barcodes.empty() ? std::string() : m->barcodes.front(), m->id, buydown::partCtnFor(*m), m->name});
        ++added;
    }
    bd_.message = tr("%1 item(s) added.").arg(added);
    emit sessionChanged();
    return true;
}

bool PosService::buydownAddItems(const QStringList &codes)
{
    std::vector<const MenuItem *> found;
    QStringList missing;
    for (const QString &c : codes) {
        const QString t = c.trimmed();
        if (t.isEmpty())
            continue;
        if (const MenuItem *m = findItem(t); m && !m->isModifier)
            found.push_back(m);
        else
            missing << t;
    }
    if (!buydownAddMenuItems(found))
        return false;
    if (!missing.isEmpty())
        bd_.message += u"  "_s + tr("Not found: %1").arg(missing.join(u", "_s));
    emit sessionChanged();
    return true;
}

bool PosService::buydownAddChosen()
{
    const QStringList ids = massChosenIds();
    if (ids.isEmpty())
        return fail(tr("Tick items in Mass Updates first (Find, a category, the brand tree, then their boxes)."));
    return buydownAddItems(ids);
}

bool PosService::buydownAddStartingWith(const QString &text)
{
    const QString t = text.trimmed();
    if (t.size() < 2)
        return fail(tr("Type the start of the names, like WINSTON."));
    std::vector<const MenuItem *> found;
    const auto scopes = scopesNow();
    for (const MenuItem &m : s_->menu)
        if (!m.isModifier && inPriceBook(m, scopes) && qs(m.name).startsWith(t, Qt::CaseInsensitive))
            found.push_back(&m);
    if (found.empty())
        return fail(tr("No retail item's name starts with %1.").arg(t));
    return buydownAddMenuItems(found);
}

bool PosService::buydownRemoveItems(const QStringList &keys)
{
    if (!bd_.draft)
        return fail(tr("Add or edit a buydown first."));
    const qsizetype before = qsizetype(bd_.draft->items.size());
    std::erase_if(bd_.draft->items, [&](const BuydownItem &i) { return keys.contains(itemKey(i)); });
    bd_.message = tr("%1 item(s) taken out.").arg(before - qsizetype(bd_.draft->items.size()));
    emit sessionChanged();
    return true;
}

bool PosService::buydownSetPartCtn(const QString &k, double part)
{
    if (!bd_.draft)
        return fail(tr("Add or edit a buydown first."));
    if (part < 0 || part > 1)
        return fail(tr("Part/Ctn is 0 (a carton: the whole amount) to 1; a pack of 10 is 0.1."));
    for (BuydownItem &i : bd_.draft->items)
        if (itemKey(i) == k)
            i.partCtn = part;
    emit sessionChanged();
    return true;
}

bool PosService::buydownApply()
{
    if (!bd_.draft)
        return fail(tr("Add or edit a buydown first."));
    if (const auto bad = buydown::problems(*bd_.draft); !bad.empty())
        return fail(tr(bad.front().c_str()));
    if (!requirePriceBook([] { return true; }))
        return false;
    Buydown b = *bd_.draft;
    const std::string who = user() ? user()->name : std::string();
    b.changedBy = who;
    b.changedAt = now();
    auto &list = s_->settings.buydowns;
    const auto it = std::ranges::find(list, b.id, &Buydown::id);
    if (it == list.end()) {
        if (std::ranges::any_of(list, [&](const Buydown &x) { return x.number == b.number; })) {
            b.number = buydown::nextNumber(list);
            b.id = "bd-" + std::to_string(b.number);
        }
        b.createdBy = who;
        b.createdAt = now();
        list.push_back(b);
        logChange(tr("Buydowns"), qs(b.name), tr("Added"), {}, format(b.amount));
    } else {
        if (it->amount != b.amount)
            logChange(tr("Buydowns"), qs(b.name), tr("Amount"), format(it->amount), format(b.amount));
        if (it->start != b.start || it->end != b.end)
            logChange(tr("Buydowns"), qs(b.name), tr("Dates"), whenText(it->start) + u" - "_s + whenText(it->end),
                      whenText(b.start) + u" - "_s + whenText(b.end));
        if (it->items.size() != b.items.size())
            logChange(tr("Buydowns"), qs(b.name), tr("Items"), QString::number(it->items.size()), QString::number(b.items.size()));
        *it = b;
    }
    const auto over = buydown::overlaps(list, b);
    bd_.draft.reset();
    bd_.isNew = false;
    bd_.notFound.clear();
    bd_.from.clear();
    bd_.message = over.empty() ? tr("%1 saved.").arg(qs(b.name))
                               : tr("%1 saved. Some items are also in %2 at the same time: the larger amount applies.")
                                     .arg(qs(b.name), qs(over.front().other));
    s_->saveSettings();
    emit notice(bd_.message);
    emit sessionChanged();
    return true;
}

bool PosService::buydownAbort()
{
    bd_.draft.reset();
    bd_.isNew = false;
    bd_.notFound.clear();
    bd_.from.clear();
    bd_.message.clear();
    emit sessionChanged();
    return true;
}

bool PosService::buydownDelete(const QString &id)
{
    if (!buydownMayLook())
        return false;
    auto &list = s_->settings.buydowns;
    const auto it = std::ranges::find(list, ss(id), &Buydown::id);
    if (it == list.end())
        return fail(tr("That buydown is gone."));
    if (!requirePriceBook([] { return true; }))
        return false;
    const QString name = qs(it->name);
    list.erase(it);
    logChange(tr("Buydowns"), name, tr("Deleted"), {}, {});
    if (bd_.draft && bd_.draft->id == ss(id))
        bd_.draft.reset();
    bd_.message = tr("%1 deleted.").arg(name);
    s_->saveSettings();
    emit sessionChanged();
    return true;
}

bool PosService::buydownSetStoreTax(bool before)
{
    if (!buydownMayLook())
        return false;
    if (s_->settings.buydownTaxBefore == before)
        return true;
    if (!requirePriceBook([] { return true; }))
        return false;
    s_->settings.buydownTaxBefore = before;
    logChange(tr("Buydowns"), tr("Store setting"), tr("Tax on the price before the buydown"), before ? tr("No") : tr("Yes"),
              before ? tr("Yes") : tr("No"));
    s_->saveSettings();
    emit sessionChanged();
    return true;
}

// --- reports ---------------------------------------------------------------------------------------------

Report PosService::buydownDefinitions(const QStringList &ids) const
{
    std::vector<const Buydown *> chosen;
    for (const Buydown &b : s_->settings.buydowns)
        if (ids.contains(u"*"_s) || ids.contains(qs(b.id)))
            chosen.push_back(&b);
    return buydownDefinitionReport(chosen, s_->menu, reportContext(tr("Buydown definitions")));
}

bool PosService::buydownShowReport(const QString &kind, const QStringList &ids)
{
    if (!buydownMayLook())
        return false;
    if (ids.isEmpty())
        return fail(tr("Choose a buydown first."));
    if (kind == u"definition") {
        bd_.report = buydownDefinitions(ids);
        bd_.reportLoading = false;
        bd_.reportShown = true;
        emit sessionChanged();
        return true;
    }
    // Sold: one buydown, over its own dates (at most the last 366 days), from the saved checks.
    const auto it = std::ranges::find(s_->settings.buydowns, ss(ids.front()), &Buydown::id);
    if (it == s_->settings.buydowns.end())
        return fail(tr("That buydown is gone."));
    const Buydown b = *it;
    std::int64_t to = b.end.empty() || b.style == 2 ? now() + 1 : std::min(now() + 1, whenMs(b.end) + 60'000);
    std::int64_t from = b.start.empty() ? 0 : whenMs(b.start);
    const std::int64_t yearAgo = now() - 366LL * 24 * 3600 * 1000;
    const bool cut = from < yearAgo;
    from = std::max(from, yearAgo);
    if (to < from)
        to = from;
    const int request = ++bd_.request;
    bd_.reportLoading = true;
    bd_.reportShown = true;
    bd_.report = {};
    emit sessionChanged();
    auto history = s_->history;
    std::vector<Check> today = s_->closedToday;
    QPointer<PosService> self(this);
    const QString label = whenText(b.start) + u" - "_s + (b.end.empty() || b.style == 2 ? tr("now") : whenText(b.end));
    QThreadPool::globalInstance()->start([=, today = std::move(today)]() mutable {
        std::vector<Check> checks = history ? history(from, to) : std::vector<Check>{};
        for (Check &c : today)
            if (c.closedAt >= from && c.closedAt < to && std::ranges::none_of(checks, [&](const Check &x) { return x.id == c.id; }))
                checks.push_back(std::move(c));
        QMetaObject::invokeMethod(self, [self, request, checks = std::move(checks), b, label, cut] {
            if (!self || request != self->bd_.request)
                return;
            Report r = buydownsReport(checks, self->s_->settings.buydowns, self->s_->menu, self->reportContext(label), b.id);
            if (cut)
                r.note(tr("Only the last 366 days are read.").toStdString());
            self->bd_.report = std::move(r);
            self->bd_.reportLoading = false;
            emit self->sessionChanged();
        }, Qt::QueuedConnection);
    });
    return true;
}

bool PosService::buydownPrintReport()
{
    if (!buydownMayLook())
        return false;
    if (!bd_.reportShown || bd_.reportLoading || bd_.report.rows.empty())
        return fail(tr("Show the report first."));
    if (!s_->printer)
        return fail(tr("No printer is set up."));
    s_->printer->printReport(s_->settings, bd_.report, receiptPrinter());
    emit notice(tr("Printing %1").arg(qs(bd_.report.title)));
    return true;
}

bool PosService::buydownCloseReport()
{
    bd_.reportShown = false;
    bd_.reportLoading = false;
    bd_.report = {};
    ++bd_.request;
    emit sessionChanged();
    return true;
}

// --- another store ---------------------------------------------------------------------------------------

QVariantMap PosService::buydownFile(const QString &id)
{
    if (!buydownMayLook())
        return {};
    const auto it = std::ranges::find(s_->settings.buydowns, ss(id), &Buydown::id);
    if (it == s_->settings.buydowns.end()) {
        fail(tr("That buydown is gone."));
        return {};
    }
    Buydown b = *it;
    b.used = 0;   // this store's count stays here
    for (BuydownItem &i : b.items)
        i.itemId.clear();   // another store finds its items by UPC
    return {{u"format"_s, QString::fromLatin1(kFileFormat)}, {u"version"_s, 1}, {u"store"_s, qs(s_->settings.storeName)},
            {u"exportedAt"_s, qint64(now())}, {u"buydown"_s, toJson(b).toVariantMap()}};
}

bool PosService::buydownImport(const QVariantMap &file)
{
    if (!buydownMayLook())
        return false;
    if (file.value(u"format"_s).toString() != QLatin1String(kFileFormat))
        return fail(tr("That isn't a ViewTouch buydown file."));
    Buydown b = buydownFromJson(QJsonObject::fromVariantMap(file.value(u"buydown"_s).toMap()));
    std::vector<BuydownItem> items = std::move(b.items);
    b.items.clear();
    b.used = 0;
    b.number = buydown::nextNumber(s_->settings.buydowns);
    b.id = "bd-" + std::to_string(b.number);
    b.createdBy = b.changedBy = {};
    b.createdAt = b.changedAt = 0;
    QVariantList missing;
    for (BuydownItem &i : items) {
        const MenuItem *m = i.upc.empty() ? nullptr : findItem(qs(i.upc));
        if (!m || m->isModifier) {
            missing.append(QVariantMap{{u"upc"_s, qs(i.upc)}, {u"name"_s, qs(i.name)}});
            continue;
        }
        if (buydown::entryFor(b, *m))
            continue;
        i.itemId = m->id;
        i.name = m->name;
        b.items.push_back(std::move(i));
    }
    bd_.open = true;
    bd_.draft = std::move(b);
    bd_.isNew = true;
    bd_.notFound = missing;
    bd_.from = file.value(u"store"_s).toString();
    bd_.message = missing.isEmpty() ? tr("Every UPC was found here. Check it, then Apply.")
                                    : tr("%1 UPC(s) not found here (listed): left out. Check it, then Apply.").arg(missing.size());
    emit sessionChanged();
    return true;
}

// --- the state -------------------------------------------------------------------------------------------

QVariantMap PosService::buydownVariant(const Buydown &b, const std::map<std::string, int> &open) const
{
    const auto o = open.find(b.id);
    const int pending = o == open.end() ? 0 : o->second;
    const buydown::State st = buydown::stateAt(b, buydownNow(), pending);
    const QString stateLabel = st == buydown::State::Coming ? tr("Coming") : st == buydown::State::Running ? tr("Running now") : tr("Ended");
    return {{u"id"_s, qs(b.id)}, {u"number"_s, b.number}, {u"type"_s, qs(b.type)},
            {u"typeLabel"_s, b.isPromo() ? tr("Promo") : tr("Buydown")}, {u"name"_s, qs(b.name)},
            {u"receiptText"_s, qs(b.receiptText)}, {u"amount"_s, QString::number(double(b.amount.cents()) / 100.0, 'f', 2)},
            {u"amountText"_s, format(b.amount)}, {u"taxBefore"_s, b.taxBefore},
            {u"taxBeforeNow"_s, buydown::taxBeforeFor(b, s_->settings.buydownTaxBefore)}, {u"usePartCtn"_s, b.usePartCtn},
            {u"style"_s, b.style}, {u"styleLabel"_s, tr(buydown::styleName(b.style))}, {u"qty"_s, b.qty},
            {u"used"_s, b.used + pending}, {u"capped"_s, b.capped()},
            {u"usedText"_s, b.capped() ? tr("%1 of %2 used").arg(b.used + pending).arg(b.qty) : QString()},
            {u"start"_s, qs(b.start)}, {u"end"_s, qs(b.end)}, {u"startText"_s, whenText(b.start)}, {u"endText"_s, whenText(b.end)},
            {u"state"_s, QString::fromLatin1(buydown::stateName(st))}, {u"stateLabel"_s, stateLabel},
            {u"manufacturer"_s, qs(b.manufacturer)}, {u"rptCode"_s, qs(b.rptCode)}, {u"account"_s, qs(b.account)},
            {u"items"_s, int(b.items.size())}, {u"changedBy"_s, qs(b.changedBy)},
            {u"changedAt"_s, b.changedAt ? QDateTime::fromMSecsSinceEpoch(b.changedAt).toString(u"MM/dd/yyyy h:mm AP"_s) : QString()}};
}

QVariantMap PosService::buydowns() const
{
    if (!bd_.open || !user() || !permissionsOf(*user()).contains(perm::Manager))
        return {};
    const auto open = buydownUnitsOpen();
    QVariantList list;
    const auto order = [](buydown::State s) { return s == buydown::State::Running ? 0 : s == buydown::State::Coming ? 1 : 2; };
    std::vector<const Buydown *> sorted;
    for (const Buydown &b : s_->settings.buydowns)
        sorted.push_back(&b);
    const std::string nowText = buydownNow();
    std::ranges::stable_sort(sorted, [&](const Buydown *a, const Buydown *b) {
        const int x = order(buydown::stateAt(*a, nowText)), y = order(buydown::stateAt(*b, nowText));
        if (x != y)
            return x < y;
        return a->start != b->start ? a->start > b->start : a->number > b->number;
    });
    for (const Buydown *b : sorted) {
        QVariantMap v = buydownVariant(*b, open);
        v.insert(u"overlap"_s, buydown::stateAt(*b, nowText) != buydown::State::Ended && !buydown::overlaps(s_->settings.buydowns, *b).empty());
        list.append(v);
    }
    QVariantMap out{{u"active"_s, true}, {u"canApply"_s, mayChangePriceBook()}, {u"storeTaxBefore"_s, s_->settings.buydownTaxBefore},
                    {u"list"_s, list}, {u"message"_s, bd_.message}, {u"chosenInMass"_s, int(massChosenIds().size())}};
    if (bd_.draft) {
        const Buydown &b = *bd_.draft;
        QVariantMap d = buydownVariant(b, open);
        d.insert(u"isNew"_s, bd_.isNew);
        d.insert(u"from"_s, bd_.from);
        QVariantList items;
        for (const BuydownItem &i : b.items) {
            const MenuItem *m = !i.itemId.empty() ? findItem(qs(i.itemId)) : (i.upc.empty() ? nullptr : findItem(qs(i.upc)));
            const Money off = buydown::amountFor(b, i);
            items.append(QVariantMap{{u"key"_s, itemKey(i)}, {u"upc"_s, qs(i.upc)}, {u"name"_s, qs(m ? m->name : i.name)},
                                     {u"partCtn"_s, partText(i.partCtn)}, {u"found"_s, m != nullptr},
                                     {u"price"_s, m ? format(m->price) : QString()}, {u"amount"_s, format(off)},
                                     {u"sale"_s, m ? format(m->price - std::min(off, m->price)) : QString()}});
        }
        d.insert(u"itemList"_s, items);
        QVariantList over;
        for (const buydown::Overlap &o : buydown::overlaps(s_->settings.buydowns, b))
            over.append(QVariantMap{{u"upc"_s, qs(o.upc)}, {u"item"_s, qs(o.item)}, {u"other"_s, qs(o.other)}});
        d.insert(u"overlaps"_s, over);
        QStringList problems;
        for (const std::string &p : buydown::problems(b))
            problems << tr(p.c_str());
        d.insert(u"problems"_s, problems);
        d.insert(u"notFound"_s, bd_.notFound);
        out.insert(u"draft"_s, d);
    }
    if (bd_.reportShown)
        out.insert(u"report"_s, bd_.reportLoading ? QVariantMap{{u"loading"_s, true}} : reportVariant(bd_.report));
    return out;
}

} // namespace vt::app
