// PosService: the self-order kiosk - guests order on their own, then pay at
// the counter (or the order goes straight to the kitchen).

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTime>
#include <QUrl>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

const Employee &kioskEmployee()
{
    static const Employee e = [] {
        Employee k;
        k.id = "kiosk";
        k.name = "Self-order kiosk";
        k.role = "kiosk";
        return k;
    }();
    return e;
}

namespace {

// What guests may order on their own: not modifiers (they come as choices),
// not what the store keeps off the kiosk, and never alcohol (ID check).
bool onKiosk(const MenuItem &m)
{
    return !m.isModifier && !m.kioskHide && !m.byWeight && m.taxClass != TaxClass::Alcohol;   // no scale there
}

} // namespace

void PosService::enableSelfOrder()
{
    if (selfOrder_)
        return;
    releaseCheck();
    selfOrder_ = true;
    userId_ = kioskEmployee().id;
    trainingOn_ = false;
    lastKioskOrder_.clear();
    emit sessionChanged();
    emit loggedInChanged(true);
    emit checkChanged();
    emit adminChanged();   // the kiosk's menu
}

bool PosService::setSelfOrder(bool on)
{
    if (!on)
        return fail(tr("A manager's PIN ends self-order mode."));
    if (!require(perm::Manager, tr("Self-order kiosk")))
        return false;
    if (current())
        return fail(tr("Close or put away the check first."));
    enableSelfOrder();
    return true;
}

bool PosService::leaveSelfOrder(const QString &managerPin)
{
    if (!selfOrder_)
        return true;
    const Employee *m = employeeByPin(managerPin);
    if (!m || !m->can(perm::Manager))
        return fail(pinRefused(tr("That isn't a manager's PIN.")));
    kioskCancel();
    selfOrder_ = false;
    userId_ = m->id;   // the manager, logged in here now
    lastKioskOrder_.clear();
    emit sessionChanged();
    emit checkChanged();
    emit adminChanged();
    emit notice(tr("Self-order kiosk off"));
    return true;
}

bool PosService::kioskStart(bool toGo)
{
    if (!selfOrder_)
        return fail(tr("This screen is not a self-order kiosk."));
    if (current())
        kioskCancel();
    lastKioskOrder_.clear();
    kioskToGo_ = toGo;
    if (!startCheck(toGo ? CheckType::Takeout : CheckType::Quick))
        return false;
    Check &c = *current();
    c.kiosk = true;
    c.label = ss(tr("Kiosk %1").arg(c.id));
    changed(c);
    return true;
}

bool PosService::kioskAdd(const QString &itemId)
{
    if (!selfOrder_)
        return fail(tr("This screen is not a self-order kiosk."));
    const MenuItem *item = findItem(itemId);
    if (!item || !onKiosk(*item))
        return fail(tr("'%1' is not on the menu.").arg(itemId));
    if (!current() && !kioskStart(kioskToGo_))
        return false;
    return addItem(itemId);
}

bool PosService::kioskRemove(qint64 lineId)
{
    Check *c = current();
    if (!selfOrder_ || !c)
        return fail(tr("No check is open."));
    const OrderLine *l = c->line(lineId);
    if (!l || l->sent)
        return fail(tr("Only items not yet sent can be changed."));
    if (choosingLine_ == lineId)
        choosingLine_ = 0;
    c->removeLine(lineId);
    selectedLine_ = 0;
    changed(*c);
    return true;
}

bool PosService::kioskFinish(const QVariantMap &guest)
{
    Check *c = current();
    if (!selfOrder_ || !c || c->lines.empty())
        return fail(tr("Add something to your order first."));
    const QString name = guest.value(u"name"_s).toString().trimmed();
    if (name.isEmpty() && s_->settings.kioskLook.askName)   // otherwise called by number
        return fail(tr("Type a name so we can call your order."));
    if (const QString missing = missingChoice(c->lines); !missing.isEmpty())
        return fail(missing);
    c->customer.name = ss(name.left(40));
    c->customer.phone = ss(guest.value(u"phone"_s).toString().trimmed().left(20));
    // What the guest avoided (Allergies): the kitchen sees it as an allergy.
    const QStringList avoid = guest.value(u"avoid"_s).toStringList();
    c->allergies.clear();
    for (const std::string &a : allergenIds())
        if (avoid.contains(qs(a)))
            c->allergies.push_back(a);
    changed(*c);
    const bool sendNow = s_->settings.kioskSendNow;
    if (sendNow && !sendOrder())
        return false;
    const Totals t = c->totals(s_->settings.tax);
    // Their slip: the number to show at the counter, and what they ordered.
    const bool slip = s_->settings.kioskSlip && s_->printer && s_->settings.printer(receiptPrinter());
    if (slip)
        s_->printer->printOrderSlip(s_->settings, *c, receiptPrinter(), sendNow);
    lastKioskOrder_ = {{u"number"_s, qint64(c->id)}, {u"name"_s, name}, {u"sent"_s, sendNow},
                       {u"total"_s, format(t.total)}, {u"slip"_s, slip}};
    choosingLine_ = 0;
    releaseCheck();   // for the counter now
    emit checkChanged();
    return true;
}

void PosService::kioskCancel()
{
    if (!selfOrder_)
        return;   // a kiosk's own: never a check staff have open
    Check *c = current();
    if (c && c->kiosk) {
        // Nothing of it was sent: it was never really an order.
        std::vector<std::int64_t> unsent;
        for (const OrderLine &l : c->lines)
            if (!l.sent)
                unsent.push_back(l.id);
        for (std::int64_t id : unsent)
            c->removeLine(id);
    }
    choosingLine_ = 0;
    releaseCheck();
    lastKioskOrder_.clear();
    emit checkChanged();
}

QVariantMap PosService::selfOrderInfo() const
{
    if (!selfOrder_)
        return {{u"on"_s, false}};
    const Check *c = currentCheck();
    return {{u"on"_s, true}, {u"ordering"_s, c != nullptr}, {u"toGo"_s, kioskToGo_},
            {u"idleSeconds"_s, s_->settings.kioskIdleSeconds}, {u"lastOrder"_s, lastKioskOrder_},
            {u"items"_s, c ? qint64(c->lines.size()) : 0},
            {u"look"_s, QVariantMap{{u"background"_s, qs(s_->settings.kioskLook.background)},
                                    {u"card"_s, qs(s_->settings.kioskLook.card)}, {u"go"_s, qs(s_->settings.kioskLook.go)},
                                    {u"text"_s, qs(s_->settings.kioskLook.text)}, {u"font"_s, qs(s_->settings.kioskLook.font)},
                                    {u"welcome"_s, qs(s_->settings.kioskLook.welcome)},
                                    {u"size"_s, s_->settings.kioskLook.sizePercent / 100.0},
                                    {u"askWhere"_s, s_->settings.kioskLook.askWhere},
                                    {u"askName"_s, s_->settings.kioskLook.askName},
                                    {u"easyReach"_s, s_->settings.kioskLook.easyReach}}}};
}

QString PosService::storeImage(const QString &path) const
{
    // The store's pictures, by name.
    if (path.startsWith(u"store:")) {
        const auto it = s_->images.find(ss(path.mid(6)));
        return it == s_->images.end() ? QString() : QString::fromLatin1(it->second.toBase64());
    }
    // Only the pictures the store has set up: never any other file.
    const std::string p = ss(path);
    bool known = !p.empty() && (p == s_->settings.displayLogo);
    for (const MenuItem &m : s_->menu)
        known = known || m.image == p;
    for (const std::string &slide : s_->settings.displaySlides)
        known = known || slide == "image:" + p;
    if (!known)
        return {};
    QFile f(path);
    constexpr qint64 kMaxBytes = 8 * 1024 * 1024;
    if (f.size() > kMaxBytes || !f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromLatin1(f.readAll().toBase64());
}

QVariantMap PosService::kioskMenu() const
{
    if (!selfOrder_)
        return {};
    const std::string period = currentMealPeriod();
    QStringList families;
    QVariantList items;
    for (const MenuItem &m : s_->menu) {
        if (!onKiosk(m))
            continue;
        const QString family = m.family.empty() ? tr("Menu") : qs(m.family);
        if (!families.contains(family))
            families << family;
        items.append(QVariantMap{
            {u"id"_s, qs(m.id)}, {u"name"_s, qs(m.name)}, {u"family"_s, family},
            {u"price"_s, format(m.priceFor(period, kioskToGo_, false))}, {u"description"_s, qs(m.description)},
            {u"image"_s, qs(m.image)},   // a ref: each screen shows its own copy (imageUrl)
            {u"available"_s, m.available && ticketsLeft(m) != 0}, {u"choices"_s, !m.modifierGroups.empty()},
            {u"left"_s, ticketsLeft(m)},
            {u"allergens"_s, [&m] { QStringList a; for (const std::string &x : m.allergens) a << qs(x); return a; }()},
            {u"contains"_s, allergenNames(m.allergens)}});
    }
    return {{u"families"_s, families}, {u"items"_s, items}};
}

// --- billboard: a TV around the store showing the guest-screen turn (signage only) -----------

QVariantMap PosService::billboard() const
{
    // The same slides as the guest screen between guests (customerPrompt), and nothing about a
    // check or a customer: the store's lines, its running promotions, its logo and colour.
    QVariantList slides;
    QVariantMap videos;
    for (const std::string &sl : s_->settings.displaySlides) {
        const QString line = qs(sl);
        slides.append(line);
        if (!line.startsWith(u"video:"))
            continue;
        // A video is a file on this computer: its name, size and date, so a billboard keeps its copy
        // until the file changes (the server sends it: PosServer, getVideo).
        QString path = line.mid(6);
        if (path.endsWith(u"#sound"))
            path.chop(6);
        const QFileInfo f(path);
        videos.insert(path, QVariantMap{{u"name"_s, f.fileName()}, {u"size"_s, f.exists() ? f.size() : qint64(0)},
                                        {u"modified"_s, f.exists() ? f.lastModified().toMSecsSinceEpoch() : qint64(0)}});
    }
    for (const QVariant &p : promotionsNow())
        slides.append(tr("Now: %1").arg(p.toString()));
    // This billboard's own settings (Hardware Configuration > Billboards), its hours by the store's
    // clock: outside them it goes dark.
    bool enabled = true, mute = false, showing = true;
    for (const TerminalConfig &t : s_->settings.terminals)
        if (qs(t.name) == terminal_) {
            enabled = t.billboardOn;
            mute = t.billboardMute;
            const QDateTime now = QDateTime::fromMSecsSinceEpoch(s_->now());
            showing = enabled && billboardOpenAt(t.billboardHours, now.date().dayOfWeek(),
                                                 now.time().hour() * 60 + now.time().minute());
        }
    return {{u"slides"_s, slides}, {u"videos"_s, videos}, {u"logo"_s, qs(s_->settings.displayLogo)},
            {u"accent"_s, qs(s_->settings.displayAccent)}, {u"storeName"_s, qs(s_->settings.storeName)},
            {u"enabled"_s, enabled}, {u"mute"_s, mute}, {u"showing"_s, showing}};
}

namespace {

// "09:00-21:00" -> {540, 1260}; anything else: nullopt.
std::optional<std::pair<int, int>> parseHours(const std::string &h)
{
    const QStringList parts = qs(h).split(u'-');
    if (parts.size() != 2)
        return std::nullopt;
    const QTime a = QTime::fromString(parts[0].trimmed(), u"H:mm"_s);
    const QTime b = QTime::fromString(parts[1].trimmed(), u"H:mm"_s);
    if (!a.isValid() || !b.isValid() || a == b)
        return std::nullopt;
    return std::pair{a.hour() * 60 + a.minute(), b.hour() * 60 + b.minute()};
}

// On during `minute` of the day with these hours ("" all day, "closed" never), counting only that
// day's own part of hours past midnight.
bool openOnDay(const std::string &h, int minute, bool tail)
{
    if (h.empty())
        return !tail;
    const auto r = parseHours(h);
    if (!r)
        return false;   // "closed"
    const auto [from, to] = *r;
    if (tail)   // yesterday's hours that ran past midnight
        return to < from && minute < to;
    return to > from ? minute >= from && minute < to : minute >= from;
}

} // namespace

bool PosService::billboardOpenAt(const std::vector<std::string> &hours, int weekday, int minute)
{
    if (hours.empty())
        return true;   // no hours set: always on
    const auto day = [&](int wd) { return hours.size() == 7 ? hours[size_t((wd + 6) % 7)] : std::string(); };
    // weekday 1 = Monday = hours[0]; yesterday's "18:00-02:00" still on at 01:00 today.
    return openOnDay(day(weekday), minute, false) || openOnDay(day(weekday == 1 ? 7 : weekday - 1), minute, true);
}

std::optional<std::string> PosService::billboardDayHours(const QString &typed)
{
    const QString t = typed.trimmed().toLower();
    if (t.isEmpty())
        return std::string();
    if (t == u"closed")
        return std::string("closed");
    const auto r = parseHours(ss(t));
    if (!r)
        return std::nullopt;
    const auto hhmm = [](int m) { return QTime(m / 60, m % 60).toString(u"HH:mm"_s); };
    return ss(hhmm(r->first) + u'-' + hhmm(r->second));
}

QVariantList PosService::billboards() const
{
    QVariantList out;
    if (!can(QString::fromLatin1(perm::Manager)))
        return out;
    const QDateTime now = QDateTime::fromMSecsSinceEpoch(s_->now());
    for (const TerminalConfig &t : s_->settings.terminals) {
        if (t.screen != "billboard")
            continue;
        QStringList hours;
        for (int d = 0; d < 7; ++d)
            hours << (size_t(d) < t.billboardHours.size() ? qs(t.billboardHours[size_t(d)]) : QString());
        out.append(QVariantMap{{u"name"_s, qs(t.name)}, {u"enabled"_s, t.billboardOn}, {u"mute"_s, t.billboardMute},
                               {u"hours"_s, hours},
                               {u"showing"_s, t.billboardOn && billboardOpenAt(t.billboardHours, now.date().dayOfWeek(),
                                                                               now.time().hour() * 60 + now.time().minute())}});
    }
    return out;
}

bool PosService::setBillboard(const QString &name, const QVariantMap &settings)
{
    if (!require(perm::Manager, tr("Billboard settings")))
        return false;
    auto it = std::ranges::find_if(s_->settings.terminals, [&](const TerminalConfig &t) {
        return qs(t.name) == name && t.screen == "billboard";
    });
    if (it == s_->settings.terminals.end())
        return fail(tr("%1 is not a billboard.").arg(name));
    std::vector<std::string> hours;
    if (settings.contains(u"hours"_s)) {
        const QVariantList typed = settings.value(u"hours"_s).toList();
        if (!typed.isEmpty() && typed.size() != 7)
            return fail(tr("Give the hours for each day of the week."));
        bool any = false;
        for (const QVariant &v : typed) {
            const auto h = billboardDayHours(v.toString());
            if (!h)
                return fail(tr("\"%1\" isn't a time like 9:00-21:00, or closed.").arg(v.toString()));
            any = any || !h->empty();
            hours.push_back(*h);
        }
        if (!any)
            hours.clear();   // every day all day: always on
        it->billboardHours = hours;
    }
    if (settings.contains(u"enabled"_s))
        it->billboardOn = settings.value(u"enabled"_s).toBool();
    if (settings.contains(u"mute"_s))
        it->billboardMute = settings.value(u"mute"_s).toBool();
    settingsChanged();
    emit s_->adminChanged();   // the billboard follows at once
    return true;
}

bool PosService::leaveBillboard(const QString &managerPin)
{
    const Employee *m = employeeByPin(managerPin);
    if (!m || !m->can(perm::Manager))
        return fail(pinRefused(tr("That isn't a manager's PIN.")));
    return true;   // the billboard closes its app; nobody is logged in here
}

} // namespace vt::app
