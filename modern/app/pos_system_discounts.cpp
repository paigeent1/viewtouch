// PosService: system discounts (owner 2026-11-27, after ILSA's "System Discount Setup Form"). The list and its
// editor live on Menu Builder > Mass Updates > System Discounts (SystemDiscountsPanel.qml): Add, Edit, Copy to New
// for every ticked one at once (D26 -> D27: the name's start changed, new dates), Delete, Print the Definition
// report, Apply / Abort; the items by UPC, added from Find, the Mass Updates ticks, "Add items that start with",
// boxes and their sticks, or a UPC; ILSA's running discounts read from a file (vt-ilsa-sysdisc). Saving is the
// price book's (an admin, or an admin's PIN). The register: while a check is open its discount lines are kept
// right by themselves (changed() -> applySystemDiscounts), one per line that gets one, reference
// sysdisc:<definition>:<line>; a closed check keeps them as they were. The rules are core/system_discount.hh.

#include "app/pos_json.hh"
#include "app/pos_service.hh"
#include "core/system_discount.hh"

#include <QDateTime>

#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

constexpr const char *kFileFormat = "viewtouch-system-discounts";
constexpr int kMostItems = 5000;
constexpr int kMostFound = 200;

// "2026-10-01 00:00" from the screen ("2026-10-01T00:00", "2026-10-01", "10/01/2026 12:00 AM"...).
std::string whenFrom(const QString &text, bool endOfDay)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return {};
    for (const QString &f : {u"yyyy-MM-dd HH:mm"_s, u"yyyy-MM-dd h:mm AP"_s, u"yyyy-MM-ddTHH:mm"_s, u"yyyy-MM-ddTHH:mm:ss"_s,
                             u"MM/dd/yyyy h:mm AP"_s, u"MM/dd/yyyy hh:mmAP"_s, u"M/d/yyyy h:mm AP"_s, u"MM/dd/yy h:mm AP"_s}) {
        const QDateTime d = QDateTime::fromString(t, f);
        if (d.isValid())
            return d.toString(u"yyyy-MM-dd HH:mm"_s).toStdString();
    }
    for (const QString &f : {u"yyyy-MM-dd"_s, u"MM/dd/yyyy"_s, u"M/d/yyyy"_s, u"MM/dd/yy"_s, u"M/d/yy"_s}) {
        QDate d = QDate::fromString(t, f);
        if (d.isValid() && d.year() < 2000)   // "1/1/27": 2027, not 1927
            d = d.addYears(100);
        if (d.isValid())
            return (d.toString(u"yyyy-MM-dd"_s) + (endOfDay ? u" 23:59"_s : u" 00:00"_s)).toStdString();
    }
    return "?";   // not a date: problems() says so
}

QString whenText(const std::string &w)
{
    if (w.empty())
        return {};
    const QDateTime d = QDateTime::fromString(QString::fromStdString(w), u"yyyy-MM-dd HH:mm"_s);
    return d.isValid() ? d.toString(u"MM/dd/yyyy h:mm AP"_s) : QString::fromStdString(w);
}

QString itemKey(const SystemDiscountItem &i)
{
    return !i.itemId.empty() ? QString::fromStdString(i.itemId) : u"upc:"_s + QString::fromStdString(i.upc);
}

QString percentText(std::int64_t bp)
{
    QString t = QString::number(double(bp) / 100.0, 'f', 2);
    while (t.contains(u'.') && (t.endsWith(u'0') || t.endsWith(u'.')))
        t.chop(1);
    return t;
}

// Words from core/system_discount.hh, for the screens' languages.
[[maybe_unused]] const char *const kCoreWords[] = {
    QT_TR_NOOP("Give it a name."), QT_TR_NOOP("The name is longer than 30 characters."),
    QT_TR_NOOP("The start date isn't a date."), QT_TR_NOOP("The end date isn't a date."), QT_TR_NOOP("It ends before it starts."),
    QT_TR_NOOP("A level with a quantity needs its percent off."), QT_TR_NOOP("A level with a quantity needs its amount off."),
    QT_TR_NOOP("A percent off can't be more than 100."), QT_TR_NOOP("Each level needs a larger quantity than the one before it."),
    QT_TR_NOOP("Fill in level 1: its quantity and amount."), QT_TR_NOOP("Add the items it's for."),
    QT_TR_NOOP("Fill in which item starts it and how many items it changes."), QT_TR_NOOP("Type a percent off from 1 to 100."),
    QT_TR_NOOP("Type the amount."),
};

} // namespace

// --- the register ----------------------------------------------------------------------------------------

void PosService::applySystemDiscounts(Check &c)
{
    if (c.status != CheckStatus::Open)
        return;
    std::vector<sysdisc::LineOff> want;
    if (!s_->settings.systemDiscounts.empty())
        want = sysdisc::wanted(s_->settings.systemDiscounts, c.lines,
                               [this](const OrderLine &l) { return findItem(QString::fromStdString(l.itemId)); }, buydownNow());
    // Already as wanted: the payments (and their ids) left alone.
    std::vector<std::pair<std::string, std::int64_t>> have, now;
    for (const Payment &p : c.payments)
        if (p.reference.starts_with(sysdisc::kRef))
            have.emplace_back(p.reference, p.amount.cents());
    for (const sysdisc::LineOff &o : want)
        now.emplace_back(sysdisc::reference(o), o.amount.cents());
    if (have == now)
        return;
    std::erase_if(c.payments, [](const Payment &p) { return p.reference.starts_with(sysdisc::kRef); });
    for (const sysdisc::LineOff &o : want) {
        Payment p;
        p.id = c.nextPaymentId++;
        p.tenderId = sysdisc::kTender;
        p.tenderName = o.text;
        p.kind = TenderKind::Discount;
        p.amount = o.amount;
        p.lineId = o.lineId;
        p.taxBefore = o.taxBefore;
        p.reference = sysdisc::reference(o);
        stampPayment(p);
        c.payments.push_back(p);
    }
}

// --- the screen ------------------------------------------------------------------------------------------

bool PosService::sysDiscMayLook()
{
    if (!user())
        return fail(tr("Log in first."));
    if (!permissionsOf(*user()).contains(perm::Manager))
        return fail(tr("System discounts are for managers."));
    return true;
}

bool PosService::sysDiscOpen(bool on)
{
    if (on && !sysDiscMayLook())
        return false;
    sd_ = {};
    sd_.open = on;
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscNew(const QString &kind)
{
    if (!sysDiscMayLook())
        return false;
    SystemDiscount d;
    d.kind = kind == QLatin1String(sysdisc::kMixMatch) ? sysdisc::kMixMatch : "";
    d.number = sysdisc::nextNumber(s_->settings.systemDiscounts);
    d.id = "sd-" + std::to_string(d.number);
    const QDate today = QDateTime::fromMSecsSinceEpoch(now()).date();
    d.start = (today.toString(u"yyyy-MM-dd"_s) + u" 00:00"_s).toStdString();
    d.end = (QDate(today.year() + 1, 1, 1).toString(u"yyyy-MM-dd"_s) + u" 00:00"_s).toStdString();   // to New Year's
    sysdisc::fillLevels(d);
    d.levels[0].qty = 1;
    d.levels[0].orMore = true;
    sd_.draft = d;
    sd_.isNew = true;
    sd_.found.clear();
    sd_.message.clear();
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscEdit(const QString &id)
{
    if (!sysDiscMayLook())
        return false;
    const auto it = std::ranges::find(s_->settings.systemDiscounts, ss(id), &SystemDiscount::id);
    if (it == s_->settings.systemDiscounts.end())
        return fail(tr("That system discount is gone."));
    sd_.draft = *it;
    sysdisc::fillLevels(*sd_.draft);
    sd_.isNew = false;
    sd_.found.clear();
    sd_.message.clear();
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscCopy(const QStringList &ids, const QString &from, const QString &to, const QString &start,
                             const QString &end)
{
    if (!sysDiscMayLook())
        return false;
    if (ids.isEmpty())
        return fail(tr("Tick the discounts to copy first."));
    const std::string s = whenFrom(start, false), e = whenFrom(end, false);
    if (s == "?" || e == "?")
        return fail(tr("Type the dates like 01/01/2027."));
    if (!s.empty() && !e.empty() && e <= s)
        return fail(tr("It ends before it starts."));
    std::vector<std::string> wanted;
    for (const QString &id : ids)
        wanted.push_back(ss(id));
    std::vector<SystemDiscount> made = sysdisc::copies(s_->settings.systemDiscounts, wanted, ss(from.trimmed()),
                                                       ss(to.trimmed()), s, e);
    if (made.empty())
        return fail(tr("Those discounts are gone."));
    if (!requirePriceBook([] { return true; }))
        return false;
    const std::string who = user() ? user()->name : std::string();
    for (SystemDiscount &d : made) {
        d.createdBy = d.changedBy = who;
        d.createdAt = d.changedAt = now();
        logChange(tr("System discounts"), qs(d.name), tr("Added (copy)"), {}, whenText(d.start) + u" - "_s + whenText(d.end));
        s_->settings.systemDiscounts.push_back(std::move(d));
    }
    sd_.message = tr("%1 copied. Each can be changed with Edit.").arg(made.size());
    s_->saveSettings();
    emit notice(sd_.message);
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscSet(const QVariantMap &f)
{
    if (!sd_.draft)
        return fail(tr("Add or edit a system discount first."));
    SystemDiscount &d = *sd_.draft;
    if (f.contains(u"name"_s))
        d.name = ss(f.value(u"name"_s).toString().trimmed().left(SystemDiscount::kNameLength));
    if (f.contains(u"percent"_s))
        d.percent = f.value(u"percent"_s).toBool();
    if (f.contains(u"sameItemOnly"_s))
        d.sameItemOnly = f.value(u"sameItemOnly"_s).toBool();
    if (f.contains(u"taxBefore"_s))
        d.taxBefore = f.value(u"taxBefore"_s).toBool();
    if (f.contains(u"start"_s))
        d.start = whenFrom(f.value(u"start"_s).toString(), false);
    if (f.contains(u"end"_s))
        d.end = whenFrom(f.value(u"end"_s).toString(), true);
    // Mix-Match: starting with item #, how many change, how, the amount, the receipt line.
    for (const auto &[key, field] : {std::pair{u"mixStart"_s, &d.mixStart}, {u"mixCount"_s, &d.mixCount}}) {
        if (!f.contains(key))
            continue;
        bool ok = false;
        const int v = f.value(key).toString().trimmed().toInt(&ok);
        if (!ok || v < 1 || v > 999)
            return fail(tr("Type a number of items, like 2."));
        *field = v;
    }
    if (f.contains(u"mixHow"_s)) {
        const QString how = f.value(u"mixHow"_s).toString();
        d.mixHow = how == u"percent"_s || how == u"price"_s ? ss(how) : "off";
    }
    if (f.contains(u"mixAmount"_s)) {
        QString a = f.value(u"mixAmount"_s).toString().trimmed();
        a.remove(u'$');
        a.remove(u'%');
        bool ok = a.isEmpty();
        const double v = a.isEmpty() ? 0.0 : a.toDouble(&ok);
        if (!ok || v < 0 || v > 100000)
            return fail(d.mixHow == "percent" ? tr("Type the percent off, like 50.") : tr("Type the amount, like 0.41."));
        d.mixPercentBp = d.mixHow == "percent" ? std::llround(v * 100.0) : 0;
        d.mixAmount = d.mixHow == "percent" ? Money() : Money::fromCents(std::llround(v * 100.0));
    }
    if (f.contains(u"mixText"_s))
        d.mixText = ss(f.value(u"mixText"_s).toString().trimmed().left(SystemDiscount::kReceiptLength));
    sd_.message.clear();
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscSetLevel(int level, const QVariantMap &f)
{
    if (!sd_.draft)
        return fail(tr("Add or edit a system discount first."));
    if (level < 0 || level >= SystemDiscount::kMostLevels)
        return fail(tr("There are five levels."));
    SystemDiscount &d = *sd_.draft;
    sysdisc::fillLevels(d);
    SystemDiscountLevel &l = d.levels[std::size_t(level)];
    if (f.contains(u"qty"_s)) {
        const QString q = f.value(u"qty"_s).toString().trimmed();
        bool ok = q.isEmpty();
        const int v = q.isEmpty() ? 0 : q.toInt(&ok);
        if (!ok || v < 0 || v > 9999)
            return fail(tr("Type the quantity, like 1."));
        l.qty = v;
    }
    if (f.contains(u"orMore"_s))
        l.orMore = f.value(u"orMore"_s).toBool();
    if (f.contains(u"amount"_s)) {
        QString a = f.value(u"amount"_s).toString().trimmed();
        a.remove(u'$');
        a.remove(u'%');
        bool ok = a.isEmpty();
        const double v = a.isEmpty() ? 0.0 : a.toDouble(&ok);
        if (!ok || v < 0 || v > 100000)
            return fail(d.percent ? tr("Type the percent off, like 10.") : tr("Type the amount off each item, like 25.00."));
        if (d.percent) {
            l.percentBp = std::llround(v * 100.0);
            l.amount = Money();
        } else {
            l.amount = Money::fromCents(std::llround(v * 100.0));
            l.percentBp = 0;
        }
    }
    if (f.contains(u"receiptText"_s))
        l.receiptText = ss(f.value(u"receiptText"_s).toString().trimmed().left(SystemDiscount::kReceiptLength));
    sd_.message.clear();
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscFind(const QString &text)
{
    if (!sd_.draft)
        return fail(tr("Add or edit a system discount first."));
    const QString t = text.trimmed();
    sd_.found.clear();
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
        if (sd_.found.size() >= kMostFound)
            break;
        sd_.found.append(QVariantMap{{u"id"_s, qs(m->id)}, {u"upc"_s, m->barcodes.empty() ? QString() : qs(m->barcodes.front())},
                                     {u"name"_s, qs(m->name)}, {u"price"_s, format(m->price)},
                                     {u"inIt"_s, sysdisc::entryFor(*sd_.draft, *m) != nullptr}});
    }
    sd_.message = hits.empty() ? tr("Nothing matches %1.").arg(t)
                  : int(hits.size()) > kMostFound ? tr("%1 match; the first %2 are shown.").arg(hits.size()).arg(kMostFound)
                                                  : QString();
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscAddMenuItems(const std::vector<const MenuItem *> &items)
{
    if (!sd_.draft)
        return fail(tr("Add or edit a system discount first."));
    SystemDiscount &d = *sd_.draft;
    int added = 0;
    for (const MenuItem *m : items) {
        if (!m || m->isModifier || sysdisc::entryFor(d, *m))
            continue;
        if (int(d.items.size()) >= kMostItems)
            break;
        d.items.push_back({m->barcodes.empty() ? std::string() : m->barcodes.front(), m->id, m->name});
        ++added;
    }
    // Find's list marks what's in it now.
    for (QVariant &v : sd_.found) {
        QVariantMap row = v.toMap();
        if (const MenuItem *m = findItem(row.value(u"id"_s).toString()))
            row.insert(u"inIt"_s, sysdisc::entryFor(d, *m) != nullptr);
        v = row;
    }
    sd_.message = tr("%1 item(s) added.").arg(added);
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscAddItems(const QStringList &codes)
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
    if (!sysDiscAddMenuItems(found))
        return false;
    if (!missing.isEmpty())
        sd_.message += u"  "_s + tr("Not found: %1").arg(missing.join(u", "_s));
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscAddChosen()
{
    const QStringList ids = massChosenIds();
    if (ids.isEmpty())
        return fail(tr("Tick items in Mass Updates first (Find, a category, the brand tree, then their boxes)."));
    return sysDiscAddItems(ids);
}

bool PosService::sysDiscAddStartingWith(const QString &text)
{
    const QString t = text.trimmed();
    if (t.size() < 2)
        return fail(tr("Type the start of the names, like 1875."));
    std::vector<const MenuItem *> found;
    const auto scopes = scopesNow();
    for (const MenuItem &m : s_->menu)
        if (!m.isModifier && inPriceBook(m, scopes) && qs(m.name).startsWith(t, Qt::CaseInsensitive))
            found.push_back(&m);
    if (found.empty())
        return fail(tr("No retail item's name starts with %1.").arg(t));
    return sysDiscAddMenuItems(found);
}

bool PosService::sysDiscAddLinked()
{
    if (!sd_.draft)
        return fail(tr("Add or edit a system discount first."));
    std::set<std::string> have;
    for (const MenuItem &m : s_->menu)
        if (sysdisc::entryFor(*sd_.draft, m))
            have.insert(m.id);
    const std::vector<const MenuItem *> linked = sysdisc::linkedItems(s_->menu, have);
    if (linked.empty())
        return fail(tr("None of its items has a box or sticks linked to it."));
    return sysDiscAddMenuItems(linked);
}

bool PosService::sysDiscRemoveItems(const QStringList &keys)
{
    if (!sd_.draft)
        return fail(tr("Add or edit a system discount first."));
    const qsizetype before = qsizetype(sd_.draft->items.size());
    if (keys.contains(u"*"_s))
        sd_.draft->items.clear();
    else
        std::erase_if(sd_.draft->items, [&](const SystemDiscountItem &i) { return keys.contains(itemKey(i)); });
    sd_.message = tr("%1 item(s) taken out.").arg(before - qsizetype(sd_.draft->items.size()));
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscApply()
{
    if (!sd_.draft)
        return fail(tr("Add or edit a system discount first."));
    if (const auto bad = sysdisc::problems(*sd_.draft); !bad.empty())
        return fail(tr(bad.front().c_str()));
    if (!requirePriceBook([] { return true; }))
        return false;
    SystemDiscount d = *sd_.draft;
    const std::string who = user() ? user()->name : std::string();
    d.changedBy = who;
    d.changedAt = now();
    auto &list = s_->settings.systemDiscounts;
    const auto it = std::ranges::find(list, d.id, &SystemDiscount::id);
    if (it == list.end()) {
        if (std::ranges::any_of(list, [&](const SystemDiscount &x) { return x.number == d.number; })) {
            d.number = sysdisc::nextNumber(list);
            d.id = "sd-" + std::to_string(d.number);
        }
        d.createdBy = who;
        d.createdAt = now();
        list.push_back(d);
        logChange(tr("System discounts"), qs(d.name), tr("Added"), {}, whenText(d.start) + u" - "_s + whenText(d.end));
    } else {
        if (it->levels != d.levels || it->percent != d.percent)
            logChange(tr("System discounts"), qs(d.name), tr("Levels"), {}, {});
        if (it->start != d.start || it->end != d.end)
            logChange(tr("System discounts"), qs(d.name), tr("Dates"), whenText(it->start) + u" - "_s + whenText(it->end),
                      whenText(d.start) + u" - "_s + whenText(d.end));
        if (it->items.size() != d.items.size())
            logChange(tr("System discounts"), qs(d.name), tr("Items"), QString::number(it->items.size()), QString::number(d.items.size()));
        *it = d;
    }
    sd_.draft.reset();
    sd_.isNew = false;
    sd_.found.clear();
    sd_.message = tr("%1 saved.").arg(qs(d.name));
    s_->saveSettings();
    emit notice(sd_.message);
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscAbort()
{
    sd_.draft.reset();
    sd_.isNew = false;
    sd_.found.clear();
    sd_.message.clear();
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscDelete(const QStringList &ids)
{
    if (!sysDiscMayLook())
        return false;
    if (ids.isEmpty())
        return fail(tr("Tick the discounts to delete first."));
    if (!requirePriceBook([] { return true; }))
        return false;
    auto &list = s_->settings.systemDiscounts;
    int gone = 0;
    for (auto it = list.begin(); it != list.end();) {
        if (ids.contains(qs(it->id))) {
            logChange(tr("System discounts"), qs(it->name), tr("Deleted"), {}, {});
            it = list.erase(it);
            ++gone;
        } else {
            ++it;
        }
    }
    if (sd_.draft && ids.contains(qs(sd_.draft->id)))
        sd_.draft.reset();
    sd_.message = tr("%1 deleted.").arg(gone);
    s_->saveSettings();
    emit sessionChanged();
    return true;
}

// --- the Definition report -------------------------------------------------------------------------------

bool PosService::sysDiscShowReport(const QStringList &ids)
{
    if (!sysDiscMayLook())
        return false;
    if (ids.isEmpty())
        return fail(tr("Choose a system discount first."));
    std::vector<const SystemDiscount *> chosen;
    for (const SystemDiscount &d : s_->settings.systemDiscounts)
        if (ids.contains(u"*"_s) || ids.contains(qs(d.id)))
            chosen.push_back(&d);
    sd_.report = systemDiscountDefinitionReport(chosen, s_->menu, reportContext(tr("System discount definitions")));
    sd_.reportShown = true;
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscPrintReport()
{
    if (!sysDiscMayLook())
        return false;
    if (!sd_.reportShown || sd_.report.rows.empty())
        return fail(tr("Show the report first."));
    if (!s_->printer)
        return fail(tr("No printer is set up."));
    s_->printer->printReport(s_->settings, sd_.report, receiptPrinter());
    emit notice(tr("Printing %1").arg(qs(sd_.report.title)));
    return true;
}

bool PosService::sysDiscCloseReport()
{
    sd_.reportShown = false;
    sd_.report = {};
    emit sessionChanged();
    return true;
}

// --- an import file (ILSA's running discounts) -----------------------------------------------------------

bool PosService::sysDiscImport(const QVariantMap &file)
{
    if (!sysDiscMayLook())
        return false;
    if (file.value(u"format"_s).toString() != QLatin1String(kFileFormat))
        return fail(tr("That isn't a ViewTouch system discount file."));
    sd_.importing.clear();
    sd_.importNotFound.clear();
    sd_.importSkipped = 0;
    const std::string nowText = buydownNow();
    std::set<QString> missingSeen;
    for (const QVariant &v : file.value(u"discounts"_s).toList()) {
        SystemDiscount d = systemDiscountFromJson(QJsonObject::fromVariantMap(v.toMap()));
        if (d.name.empty() || sysdisc::stateAt(d, nowText) == sysdisc::State::Ended) {
            ++sd_.importSkipped;   // over already: not brought in
            continue;
        }
        std::vector<SystemDiscountItem> items = std::move(d.items);
        d.items.clear();
        for (SystemDiscountItem &i : items) {
            const MenuItem *m = i.upc.empty() ? nullptr : findItem(qs(i.upc));
            if (!m || m->isModifier) {
                if (missingSeen.insert(qs(i.upc)).second)
                    sd_.importNotFound.append(QVariantMap{{u"upc"_s, qs(i.upc)}, {u"name"_s, qs(i.name)}, {u"discount"_s, qs(d.name)}});
                continue;
            }
            if (sysdisc::entryFor(d, *m))
                continue;
            i.itemId = m->id;
            i.name = m->name;
            d.items.push_back(std::move(i));
        }
        sd_.importing.push_back(std::move(d));
    }
    sd_.importFrom = file.value(u"from"_s).toString();
    if (sd_.importing.empty())
        return fail(tr("Nothing in that file is still running."));
    sd_.message = tr("%1 discount(s) read; %2 over already (left out). Check them, then Add them.")
                      .arg(sd_.importing.size()).arg(sd_.importSkipped);
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscImportApply()
{
    if (!sysDiscMayLook())
        return false;
    if (sd_.importing.empty())
        return fail(tr("Read a system discount file first."));
    if (!requirePriceBook([] { return true; }))
        return false;
    auto &list = s_->settings.systemDiscounts;
    const std::string who = user() ? user()->name : std::string();
    int added = 0, replaced = 0;
    for (SystemDiscount d : sd_.importing) {
        sysdisc::fillLevels(d);
        d.changedBy = who;
        d.changedAt = now();
        const auto same = d.ilsaId.empty() ? list.end()
                                           : std::ranges::find_if(list, [&](const SystemDiscount &x) { return x.ilsaId == d.ilsaId; });
        if (same != list.end()) {
            d.id = same->id;
            d.number = same->number;
            d.createdBy = same->createdBy;
            d.createdAt = same->createdAt;
            *same = d;
            ++replaced;
        } else {
            d.number = sysdisc::nextNumber(list);
            d.id = "sd-" + std::to_string(d.number);
            d.createdBy = who;
            d.createdAt = now();
            list.push_back(d);
            ++added;
        }
    }
    logChange(tr("System discounts"), sd_.importFrom.isEmpty() ? tr("Import") : sd_.importFrom, tr("Imported"), {},
              tr("%1 added, %2 replaced").arg(added).arg(replaced));
    sd_.importing.clear();
    sd_.importNotFound.clear();
    sd_.message = tr("%1 added, %2 replaced.").arg(added).arg(replaced);
    s_->saveSettings();
    emit notice(sd_.message);
    emit sessionChanged();
    return true;
}

bool PosService::sysDiscImportCancel()
{
    sd_.importing.clear();
    sd_.importNotFound.clear();
    sd_.importSkipped = 0;
    sd_.message.clear();
    emit sessionChanged();
    return true;
}

// --- the state -------------------------------------------------------------------------------------------

QVariantMap PosService::sysDiscVariant(const SystemDiscount &d) const
{
    const sysdisc::State st = sysdisc::stateAt(d, buydownNow());
    const QString stateLabel = st == sysdisc::State::Coming ? tr("Coming") : st == sysdisc::State::Running ? tr("Running") : tr("Ended");
    QString level1, receipt;
    const bool mix = sysdisc::isMixMatch(d);
    if (mix) {
        // "Item 2: 1 at $0.41"
        const QString change = d.mixHow == "percent" ? percentText(d.mixPercentBp) + u"% off"_s
                               : d.mixHow == "price" ? format(d.mixAmount) : format(d.mixAmount) + u" off"_s;
        level1 = tr("Item %1: %2 at %3").arg(d.mixStart).arg(d.mixCount).arg(change);
        receipt = qs(d.mixText);
    }
    for (const SystemDiscountLevel &l : d.levels)
        if (!mix && sysdisc::used(d, l)) {
            level1 = QString::number(l.qty) + (l.orMore ? u"+"_s : QString()) + u"  "_s
                     + (d.percent ? percentText(l.percentBp) + u"%"_s : format(l.amount));
            receipt = qs(l.receiptText);
            break;
        }
    return {{u"id"_s, qs(d.id)}, {u"number"_s, d.number}, {u"name"_s, qs(d.name)}, {u"percent"_s, d.percent},
            {u"typeLabel"_s, mix ? tr("Mix-Match") : d.percent ? tr("% off each item") : tr("$ off each item")},
            {u"kind"_s, qs(d.kind)}, {u"mixStart"_s, QString::number(d.mixStart)}, {u"mixCount"_s, QString::number(d.mixCount)},
            {u"mixHow"_s, qs(d.mixHow)},
            {u"mixAmount"_s, d.mixHow == "percent" ? percentText(d.mixPercentBp) : QString::number(double(d.mixAmount.cents()) / 100.0, 'f', 2)},
            {u"mixText"_s, qs(d.mixText)},
            {u"sameItemOnly"_s, d.sameItemOnly}, {u"taxBefore"_s, d.taxBefore},
            {u"start"_s, qs(d.start)}, {u"end"_s, qs(d.end)}, {u"startText"_s, whenText(d.start)}, {u"endText"_s, whenText(d.end)},
            {u"state"_s, QString::fromLatin1(sysdisc::stateName(st))}, {u"stateLabel"_s, stateLabel},
            {u"level1"_s, level1}, {u"receipt"_s, receipt}, {u"items"_s, int(d.items.size())}, {u"ilsaId"_s, qs(d.ilsaId)},
            {u"changedBy"_s, qs(d.changedBy)},
            {u"changedAt"_s, d.changedAt ? QDateTime::fromMSecsSinceEpoch(d.changedAt).toString(u"MM/dd/yyyy h:mm AP"_s) : QString()}};
}

QVariantMap PosService::systemDiscounts() const
{
    if (!sd_.open || !user() || !permissionsOf(*user()).contains(perm::Manager))
        return {};
    const std::string nowText = buydownNow();
    const auto order = [](sysdisc::State s) { return s == sysdisc::State::Running ? 0 : s == sysdisc::State::Coming ? 1 : 2; };
    std::vector<const SystemDiscount *> sorted;
    for (const SystemDiscount &d : s_->settings.systemDiscounts)
        sorted.push_back(&d);
    std::ranges::stable_sort(sorted, [&](const SystemDiscount *a, const SystemDiscount *b) {
        const int x = order(sysdisc::stateAt(*a, nowText)), y = order(sysdisc::stateAt(*b, nowText));
        return x != y ? x < y : a->name < b->name;
    });
    QVariantList list;
    for (const SystemDiscount *d : sorted)
        list.append(sysDiscVariant(*d));
    QVariantMap out{{u"active"_s, true}, {u"canApply"_s, mayChangePriceBook()}, {u"list"_s, list}, {u"message"_s, sd_.message},
                    {u"chosenInMass"_s, int(massChosenIds().size())}, {u"nameLength"_s, SystemDiscount::kNameLength}};
    if (sd_.draft) {
        const SystemDiscount &d = *sd_.draft;
        QVariantMap v = sysDiscVariant(d);
        v.insert(u"isNew"_s, sd_.isNew);
        QVariantList levels;
        for (const SystemDiscountLevel &l : d.levels) {
            const bool any = l.qty > 0 || l.amount.cents() > 0 || l.percentBp > 0;
            levels.append(QVariantMap{
                {u"qty"_s, l.qty > 0 ? QString::number(l.qty) : QString()}, {u"orMore"_s, l.orMore},
                {u"amount"_s, !any ? QString() : d.percent ? percentText(l.percentBp) : QString::number(double(l.amount.cents()) / 100.0, 'f', 2)},
                {u"receiptText"_s, qs(l.receiptText)}});
        }
        v.insert(u"levels"_s, levels);
        QVariantList items;
        for (const SystemDiscountItem &i : d.items) {
            const MenuItem *m = !i.itemId.empty() ? findItem(qs(i.itemId)) : (i.upc.empty() ? nullptr : findItem(qs(i.upc)));
            items.append(QVariantMap{{u"key"_s, itemKey(i)}, {u"upc"_s, qs(i.upc)}, {u"name"_s, qs(m ? m->name : i.name)},
                                     {u"found"_s, m != nullptr}, {u"price"_s, m ? format(m->price) : QString()}});
        }
        v.insert(u"itemList"_s, items);
        QStringList problems;
        for (const std::string &p : sysdisc::problems(d))
            problems << tr(p.c_str());
        v.insert(u"problems"_s, problems);
        out.insert(u"draft"_s, v);
        out.insert(u"found"_s, sd_.found);
    }
    if (!sd_.importing.empty()) {
        QVariantList rows;
        for (const SystemDiscount &d : sd_.importing) {
            QVariantMap r = sysDiscVariant(d);
            r.insert(u"replaces"_s, !d.ilsaId.empty() && std::ranges::any_of(s_->settings.systemDiscounts, [&](const SystemDiscount &x) {
                                        return x.ilsaId == d.ilsaId; }));
            rows.append(r);
        }
        out.insert(u"importing"_s, QVariantMap{{u"rows"_s, rows}, {u"count"_s, int(sd_.importing.size())},
                                               {u"skipped"_s, sd_.importSkipped}, {u"notFound"_s, sd_.importNotFound},
                                               {u"from"_s, sd_.importFrom}});
    }
    if (sd_.reportShown)
        out.insert(u"report"_s, reportVariant(sd_.report));
    return out;
}

} // namespace vt::app
