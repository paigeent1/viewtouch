// PosService: Find a Tablet (End of shift, any staff). A tablet left lying around beeps at alarm
// volume and shows "Here I am" until it's found, stopped here, or about 2 minutes have passed.
// The message goes over the tablet's own connection to the store's server (PosServer:
// PosShared::sendToScreen); the tablet's app does the rest (Main.qml, FindMeOverlay.qml, FindMe.kt).

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QJsonObject>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

// A tablet: a paired screen (not a billboard's TV) that runs Android, or hasn't said yet what it
// runs (paired before ViewTouch kept it; the store's own screens aren't paired).
bool isTablet(const TerminalConfig &t)
{
    return !t.id.empty() && !t.key.empty() && t.screen != "billboard"
           && (t.platform.empty() || t.platform.starts_with("android"));
}

QJsonObject findMessage(bool on, const QString &from, const QString &by)
{
    return QJsonObject{{u"t"_s, u"event"_s}, {u"e"_s, u"findMe"_s},
                       {u"v"_s, QJsonObject{{u"on"_s, on}, {u"from"_s, from}, {u"by"_s, by},
                                             {u"seconds"_s, PosService::findSeconds}}}};
}

} // namespace

QVariantList PosService::tablets() const
{
    QVariantList out;
    const std::int64_t at = now();
    for (const TerminalConfig &t : s_->settings.terminals) {
        if (!isTablet(t) || qs(t.name) == terminalName())
            continue;
        const auto link = s_->screenLinks.find(t.id);
        const bool connected = link != s_->screenLinks.end() && link->second.links > 0;
        const std::int64_t seen = link == s_->screenLinks.end() ? 0 : connected ? at : link->second.lastSeen;
        const auto f = s_->finding.find(t.id);
        const bool beeping = connected && f != s_->finding.end() && f->second.until > at;
        out << QVariantMap{{u"id"_s, qs(t.id)}, {u"name"_s, qs(t.name)}, {u"connected"_s, connected},
                           {u"lastSeen"_s, qint64(seen)},
                           {u"lastSeenText"_s, connected ? QString() : seen > 0 ? dueText(seen) : QString()},
                           {u"beeping"_s, beeping}, {u"from"_s, beeping ? f->second.from : QString()}};
    }
    std::ranges::sort(out, [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(u"name"_s).toString().localeAwareCompare(b.toMap().value(u"name"_s).toString()) < 0;
    });
    return out;
}

bool PosService::findTablet(const QString &terminalId)
{
    if (!loggedIn())
        return fail(tr("Log in first."));
    const TerminalConfig *t = s_->settings.pairedTerminal(terminalId.toStdString());
    if (!t || !isTablet(*t))
        return fail(tr("That tablet isn't paired with this store any more."));
    const QString name = qs(t->name);
    if (!s_->sendToScreen || !s_->sendToScreen(t->id, findMessage(true, terminalName(), userName())))
        return fail(tr("%1 is not connected — it may be off, asleep with Wi-Fi off, or out of battery.").arg(name));
    const std::int64_t until = now() + findSeconds * 1000LL;
    s_->finding[t->id] = {terminalName(), until};
    // It stops by itself then (the tablet does too); the list here says so.
    QTimer::singleShot(findSeconds * 1000 + 500, s_, [shared = s_, id = t->id, until] {
        const auto it = shared->finding.find(id);
        if (it != shared->finding.end() && it->second.until == until) {
            shared->finding.erase(it);
            emit shared->networkChanged();
        }
    });
    emit s_->networkChanged();
    emit notice(tr("%1 is beeping. Tap Stop once you've found it.").arg(name));
    return true;
}

bool PosService::stopFindingTablet(const QString &terminalId)
{
    const TerminalConfig *t = s_->settings.pairedTerminal(terminalId.toStdString());
    if (!t)
        return false;
    if (s_->sendToScreen)
        s_->sendToScreen(t->id, findMessage(false, terminalName(), userName()));
    s_->finding.erase(t->id);
    emit s_->networkChanged();
    return true;
}

void PosService::foundTablet()
{
    for (const TerminalConfig &t : s_->settings.terminals)
        if (!t.id.empty() && qs(t.name) == terminalName() && s_->finding.erase(t.id) > 0)
            emit s_->networkChanged();
}

} // namespace vt::app
