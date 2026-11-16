// Responsible service: standard drinks counted for each guest at ring-in, against the store's caution
// and stop levels (Manager -> Store Settings -> Responsible service; off by default).
//  - Caution (yellow): the drink waits until the bartender acknowledges it (one tap).
//  - Stop (flashing red): the drink waits until a manager's PIN and a reason override it, or it is
//    not rung in.
// The server decides (a tablet's RemoteSession only shows serviceAsk); the guest's screens never see it.
// Every warning, acknowledgment, override and cancel goes to alcohol_policy_audit.jsonl in the store's
// data folder, and Reports -> Responsible Service lists them. The counting rules: core/over_service.hh.
#include "app/i18n.hh"
#include "app/pos_json.hh"
#include "app/pos_service.hh"
#include "core/over_service.hh"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {
QString drinksText(double d) { return QString::number(std::round(d * 10.0) / 10.0, 'f', 1); }
double rounded(double d) { return std::round(d * 100.0) / 100.0; }

// A manager's reasons when the store has none of its own (in the screen's language).
constexpr const char *kOverrideReasons[] = {
    QT_TR_NOOP("Count is wrong (shared or not theirs)"), QT_TR_NOOP("Guest is eating and staying a while"),
    QT_TR_NOOP("Non-alcoholic or a smaller pour"), QT_TR_NOOP("Manager checked on the guest"),
};
} // namespace

std::map<std::string, std::vector<DrinkRing>> PosService::serviceRings() const
{
    std::vector<const Check *> checks;
    for (const auto &[id, c] : s_->open)
        checks.push_back(&c);
    for (const Check &c : s_->closedToday)
        checks.push_back(&c);
    return drinkRings(checks, windowStart(s_->settings.service, now()));
}

QString PosService::guestLabel(const DrinkGuest &g) const
{
    if (g.kind == "customer")
        return g.name.empty() ? tr("Customer") : qs(g.name);
    if (g.kind == "subscription")
        return tr("Subscription ••%1").arg(qs(g.name));
    if (g.kind == "id")
        return g.seat > 0 ? tr("Seat %1 (ID ••%2)").arg(g.seat).arg(qs(g.name)) : tr("ID ••%1").arg(qs(g.name));
    return g.seat > 0 ? tr("Seat %1").arg(g.seat) : tr("No seat");
}

QVariantList PosService::drinkCounts() const
{
    const ServicePolicy &p = s_->settings.service;
    const Check *c = currentCheck();
    if (!p.on || !c || c->training)
        return {};
    std::map<std::string, DrinkGuest> guests;
    for (const OrderLine &l : c->lines)
        for (const auto &[g, d] : lineGuests(*c, l))
            guests.emplace(g.key, g);
    if (guests.empty())
        return {};
    const auto rings = serviceRings();
    std::vector<std::pair<DrinkGuest, double>> counts;
    for (const auto &[key, g] : guests) {
        const auto it = rings.find(key);
        counts.emplace_back(g, it == rings.end() ? 0.0 : drinksNow(it->second, p, now()));
    }
    std::ranges::sort(counts, [](const auto &a, const auto &b) { return a.first.seat < b.first.seat; });
    QVariantList out;
    for (const auto &[g, d] : counts)
        out.append(QVariantMap{{u"key"_s, qs(g.key)}, {u"label"_s, guestLabel(g)}, {u"seat"_s, g.seat},
                               {u"drinks"_s, drinksText(d)}, {u"value"_s, d},
                               {u"level"_s, qs(toString(levelOf(d, p)))}});
    return out;
}

bool PosService::serviceGate(const Check *c, const std::vector<PendingDrink> &adds, const QString &what,
                             const QString &method, const QVariantList &args)
{
    const ServicePolicy &p = s_->settings.service;
    if (!p.on || adds.empty() || training() || (c && c->training))
        return true;
    if (std::exchange(servicePassed_, false))   // acknowledged / overridden: this once
        return true;
    // Each guest's drinks now, and with these.
    std::map<std::string, PendingDrink> byGuest;
    for (const PendingDrink &a : adds) {
        if (a.drinks <= 0)
            continue;
        auto [it, fresh] = byGuest.try_emplace(a.guest.key, a);
        if (!fresh)
            it->second.drinks += a.drinks;
    }
    if (byGuest.empty())
        return true;
    const auto rings = serviceRings();
    ServiceLevel worst = ServiceLevel::Ok;
    const PendingDrink *who = nullptr;
    double before = 0, after = 0;
    for (const auto &[key, a] : byGuest) {
        const auto it = rings.find(key);
        const double b = it == rings.end() ? 0.0 : drinksNow(it->second, p, now());
        const ServiceLevel l = levelOf(b + a.drinks, p);
        if (!who || l > worst || (l == worst && b + a.drinks > after)) {
            worst = l;
            who = &a;
            before = b;
            after = b + a.drinks;
        }
    }
    if (worst == ServiceLevel::Ok)
        return true;
    serviceAsk_ = {{u"level"_s, qs(toString(worst))}, {u"guest"_s, guestLabel(who->guest)},
                   {u"guestKey"_s, qs(who->guest.key)}, {u"seat"_s, who->guest.seat},
                   {u"drinks"_s, drinksText(after)}, {u"before"_s, drinksText(before)},
                   {u"adding"_s, drinksText(who->drinks)}, {u"value"_s, rounded(after)},
                   {u"valueBefore"_s, rounded(before)}, {u"valueAdding"_s, rounded(who->drinks)},
                   {u"item"_s, what}, {u"check"_s, c ? qs(c->label) : QString()},
                   {u"checkId"_s, c ? qint64(c->id) : qint64(0)},
                   {u"windowHours"_s, p.windowHours}, {u"caution"_s, p.caution}, {u"stop"_s, p.stop}};
    // The self-order kiosk: a guest's screen shows nothing of this. A stop is sent to the bar.
    if (selfOrder_) {
        if (worst != ServiceLevel::Stop) {
            serviceAsk_.clear();
            return true;
        }
        serviceAudit(u"referred"_s);
        serviceAsk_.clear();
        return fail(tr("Please order this one at the bar."));
    }
    QStringList reasons;
    for (const std::string &r : p.overrideReasons)
        reasons << qs(r);
    if (reasons.isEmpty())
        for (const char *r : kOverrideReasons)
            reasons << tr(r);
    serviceAsk_.insert(u"reasons"_s, reasons);
    serviceDrinks_ = adds;
    serviceMethod_ = method;
    serviceArgs_ = args;
    serviceAudit(u"warning"_s);
    emit checkChanged();
    emit notice(worst == ServiceLevel::Stop
                    ? tr("Stop: %1 is at %2 standard drinks. A manager's PIN is needed.").arg(guestLabel(who->guest), drinksText(after))
                    : tr("Caution: %1 is at %2 standard drinks.").arg(guestLabel(who->guest), drinksText(after)));
    return false;
}

void PosService::serviceAudit(const QString &action, const QString &manager, const QString &reason)
{
    const QString level = serviceAsk_.value(u"level"_s).toString();
    QJsonObject e{
        {u"at"_s, qint64(now())},
        {u"time"_s, QDateTime::fromMSecsSinceEpoch(now()).toString(Qt::ISODate)},
        {u"terminal"_s, terminal_},
        {u"employee"_s, user() ? qs(user()->name) : (selfOrder_ ? u"Kiosk"_s : QString())},
        {u"employeeId"_s, user() ? qs(user()->id) : QString()},
        {u"check"_s, serviceAsk_.value(u"check"_s).toString()},
        {u"checkId"_s, serviceAsk_.value(u"checkId"_s).toLongLong()},
        {u"seat"_s, serviceAsk_.value(u"seat"_s).toInt()},
        {u"guest"_s, serviceAsk_.value(u"guest"_s).toString()},
        {u"guestKey"_s, serviceAsk_.value(u"guestKey"_s).toString()},
        {u"drinks"_s, serviceAsk_.value(u"value"_s).toDouble()},
        {u"before"_s, serviceAsk_.value(u"valueBefore"_s).toDouble()},
        {u"adding"_s, serviceAsk_.value(u"valueAdding"_s).toDouble()},
        {u"item"_s, serviceAsk_.value(u"item"_s).toString()},
        {u"level"_s, level},
        {u"action"_s, action},
        {u"manager"_s, manager},
        {u"reason"_s, reason},
    };
    s_->serviceAuditMemory.push_back(e);
    if (s_->serviceAuditMemory.size() > 2000)   // the file keeps them all
        s_->serviceAuditMemory.erase(s_->serviceAuditMemory.begin());
    if (s_->serviceAuditFile.isEmpty())
        return;
    QFile f(s_->serviceAuditFile);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write(QJsonDocument(e).toJson(QJsonDocument::Compact) + '\n');
        f.flush();
    } else {
        s_->noteProblem("serviceAudit", "alcohol_policy_audit.jsonl", f.errorString().toStdString());
    }
}

bool PosService::serviceProceed()
{
    const QString method = std::exchange(serviceMethod_, {});
    const QVariantList args = std::exchange(serviceArgs_, {});
    serviceAsk_.clear();
    serviceDrinks_.clear();
    emit checkChanged();
    if (method.isEmpty())
        return true;
    servicePassed_ = true;
    invoke(method, args);   // once more: the gate lets it through this time
    servicePassed_ = false;
    return true;
}

bool PosService::acknowledgeService()
{
    if (serviceAsk_.isEmpty())
        return fail(tr("Nothing is waiting."));
    if (serviceAsk_.value(u"level"_s).toString() != u"caution")
        return fail(tr("This one needs a manager's PIN."));
    serviceAudit(u"acknowledged"_s);
    return serviceProceed();
}

bool PosService::overrideService(const QString &pin, const QString &reason)
{
    if (serviceAsk_.isEmpty())
        return fail(tr("Nothing is waiting."));
    const QString why = reason.trimmed().left(120);
    if (why.isEmpty())
        return fail(tr("Pick or type a reason."));
    const Employee *m = employeeByPin(pin.trimmed());
    if (!m || !m->active || !(m->can(perm::Manager) || m->role == "admin"))
        return fail(pinRefused(tr("That PIN can't override this: a manager's PIN is needed.")));
    serviceAudit(u"override"_s, qs(m->name), why);
    if (Check *c = current(); c && qint64(c->id) == serviceAsk_.value(u"checkId"_s).toLongLong())
        noteEvent(*c, tr("Responsible service: %1 at %2 drinks, overridden by %3 (%4)")
                          .arg(serviceAsk_.value(u"guest"_s).toString(), serviceAsk_.value(u"drinks"_s).toString(),
                               qs(m->name), why),
                  "service", {}, ss(why));
    return serviceProceed();
}

bool PosService::cancelService()
{
    if (serviceAsk_.isEmpty())
        return true;
    serviceAudit(u"cancelled"_s);
    serviceAsk_.clear();
    serviceDrinks_.clear();
    serviceMethod_.clear();
    serviceArgs_.clear();
    pendingScan_ = {};
    emit checkChanged();
    emit notice(tr("Not rung in."));
    return true;
}

bool PosService::shareDrink(qint64 lineId, const QVariantList &seats)
{
    if (!require(perm::Order, tr("Ordering")))
        return false;
    Check *c = current();
    OrderLine *l = c ? c->line(lineId ? lineId : selectedLine_) : nullptr;
    if (!l || lineDrinks(*l) <= 0)
        return fail(tr("Touch a drink on the check first."));
    std::set<int> picked;
    for (const QVariant &v : seats)
        if (const int s = v.toInt(); s > 0 && s < 100)
            picked.insert(s);
    l->drinkSeats.clear();
    if (picked.size() > 1)
        l->drinkSeats.assign(picked.begin(), picked.end());
    QStringList names;
    for (const int s : picked)
        names << QString::number(s);
    const QString text = l->drinkSeats.empty()
                             ? tr("%1: counted for its own seat").arg(qs(l->displayName()))
                             : tr("%1 shared across seats %2").arg(qs(l->displayName()), names.join(u", "_s));
    if (s_->settings.service.on) {   // on the audit, as the counts change
        const QVariantMap keep = serviceAsk_;
        serviceAsk_ = {{u"check"_s, qs(c->label)}, {u"checkId"_s, qint64(c->id)}, {u"seat"_s, l->seat},
                       {u"item"_s, qs(l->displayName())}, {u"value"_s, rounded(lineDrinks(*l))},
                       {u"guest"_s, names.isEmpty() ? guestLabel(drinkGuest(*c, l->seat)) : tr("Seats %1").arg(names.join(u", "_s))}};
        serviceAudit(u"shared"_s);
        serviceAsk_ = keep;
    }
    selectedLine_ = l->id;
    emit notice(text);
    changed(*c);
    return true;
}

// --- the audit file and its report -------------------------------------------------------------

std::vector<QJsonObject> PosService::serviceAuditEntries(std::int64_t from, std::int64_t to) const
{
    std::vector<QJsonObject> out;
    const auto keep = [&](const QJsonObject &e) {
        const std::int64_t at = e.value(u"at").toInteger();
        if (at >= from && at < to)
            out.push_back(e);
    };
    QFile f(s_->serviceAuditFile);
    if (s_->serviceAuditFile.isEmpty() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const QJsonObject &e : s_->serviceAuditMemory)
            keep(e);
        return out;
    }
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty())
            continue;
        const QJsonDocument d = QJsonDocument::fromJson(line);
        if (d.isObject())
            keep(d.object());
    }
    std::ranges::stable_sort(out, {}, [](const QJsonObject &e) { return e.value(u"at").toInteger(); });
    return out;
}

Report PosService::serviceReport(std::int64_t from, std::int64_t to, const QString &label) const
{
    const ReportContext ctx = reportContext(label);
    const std::vector<QJsonObject> entries = serviceAuditEntries(from, to);
    Report r;
    r.id = "service";
    r.title = "Responsible Service";
    r.subtitle = ctx.period;
    r.columns = {"Time", "Employee", "Check", "Guest", "Drinks", "Level", "Action", "Manager", "Reason"};
    std::map<std::string, int> count;
    for (const QJsonObject &e : entries)
        ++count[ss(e.value(u"action").toString())];
    const auto actionName = [](const std::string &a) -> std::string {
        if (a == "warning") return "Warning";
        if (a == "acknowledged") return "Acknowledged";
        if (a == "override") return "Manager override";
        if (a == "cancelled") return "Not rung in";
        if (a == "referred") return "Sent to the bar (kiosk)";
        if (a == "shared") return "Shared across seats";
        return a;
    };
    const auto levelName = [](const std::string &l) -> std::string {
        if (l == "caution") return "Caution";
        if (l == "stop") return "Stop";
        return l;
    };
    r.section("Summary");
    for (const char *a : {"warning", "acknowledged", "override", "cancelled", "referred", "shared"})
        if (count[a] > 0)
            r.line({actionName(a), "", "", "", std::to_string(count[a])});
    if (entries.empty()) {
        r.note("No responsible service warnings in these days.");
        return r;
    }
    r.section("Each warning and what was done");
    const bool oneDay = to - from <= 36LL * 3'600'000;
    for (const QJsonObject &e : entries) {
        const std::int64_t at = e.value(u"at").toInteger();
        const double d = e.value(u"drinks").toDouble();
        r.line({oneDay ? ctx.clock(at) : ctx.date(at) + " " + ctx.clock(at), ss(e.value(u"employee").toString()),
                ss(e.value(u"check").toString()), ss(e.value(u"guest").toString()),
                ss(QString::number(d, 'f', 1)), levelName(ss(e.value(u"level").toString())),
                actionName(ss(e.value(u"action").toString())), ss(e.value(u"manager").toString()),
                ss(e.value(u"reason").toString())});
    }
    r.note("Standard drinks: pour oz x ABV / 100 / 0.6, counted within the store's time window "
           "(Store Settings -> Responsible service).");
    return r;
}

} // namespace vt::app
