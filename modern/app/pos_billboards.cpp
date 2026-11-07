// PosService: Store Settings > Billboards - the TVs around the store (and at the other stores, over
// the VPN) showing the guest-screen slides. Each is a terminal with screen "billboard"; here are
// its settings as the page lists and edits them (adminRecords / adminFields / adminSave
// "billboards"), and what the page does to the TV itself: deploy or update its app and restart it
// (vt-add-billboard, run here on the store's computer in the background), identify it (its name on
// the TV, over its own connection) and remove it (unpaired, with a manager's PIN).

#include "app/pos_admin_shared.hh"

#include <QDateTime>
#include <QFile>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTime>
#include <QTimer>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

using namespace admin;

namespace {


// The billboards among the terminals, in their order (the page's list).
std::vector<TerminalConfig *> billboardList(PosSettings &s)
{
    std::vector<TerminalConfig *> out;
    for (TerminalConfig &t : s.terminals)
        if (t.screen == "billboard")
            out.push_back(&t);
    return out;
}

TerminalConfig *billboardNamed(PosSettings &s, const QString &name)
{
    for (TerminalConfig &t : s.terminals)
        if (t.screen == "billboard" && QString::fromStdString(t.name) == name)
            return &t;
    return nullptr;
}

// Every screen hears of it (the page's progress, a TV's new settings); the managers' lists too.
void touched(PosShared *sh)
{
    ++sh->adminRevision;
    emit sh->adminChanged();
}

bool overrideActive(const TerminalConfig &t, std::int64_t now)
{
    return !t.billboardOverride.empty() && (t.billboardOverrideUntil == 0 || now < t.billboardOverrideUntil);
}

QVariantMap jobMap(const PosShared::BillboardJob &j)
{
    return {{u"action"_s, j.action}, {u"running"_s, j.running}, {u"ok"_s, j.ok}, {u"step"_s, j.step},
            {u"steps"_s, j.steps}, {u"lines"_s, j.lines}, {u"at"_s, qint64(j.at)},
            {u"refused"_s, j.refused}, {u"unreachable"_s, j.unreachable}};
}

// A line from vt-add-billboard: "[2/6] Installing...", or one of its "@vt ..." notes for the page
// (not shown): who made the TV, whether it was reachable at all.
bool takeLine(PosShared *sh, const std::string &name, const QString &line)
{
    PosShared::BillboardJob &job = sh->billboardJobs[name];
    if (line.startsWith(u"@vt "_s)) {
        const QString note = line.mid(4).trimmed();
        if (note == u"unreachable")
            job.unreachable = true;
        else if (note == u"refused")
            job.refused = true;
        else if (note.startsWith(u"maker "_s)) {
            for (TerminalConfig &t : sh->settings.terminals)
                if (t.name == name && t.screen == "billboard")
                    t.billboardMaker = note.mid(6).trimmed().left(40).toStdString();
        }
        return false;
    }
    static const QRegularExpression step(uR"(^\[(\d+)/(\d+)\]\s*(.*)$)"_s);
    QString text = line;
    if (const QRegularExpressionMatch m = step.match(line); m.hasMatch()) {
        job.step = m.captured(1).toInt();
        job.steps = m.captured(2).toInt();
        text = m.captured(3);
    }
    if (text.isEmpty())
        return false;
    job.lines << text.left(400);
    while (job.lines.size() > 40)
        job.lines.removeFirst();
    return true;
}

void runBillboardScript(PosShared *sh, const std::string &name, const QString &action, const QStringList &args)
{
    PosShared::BillboardJob &job = sh->billboardJobs[name];
    job = {};
    job.action = action;
    job.running = true;
    job.at = sh->now();
    job.lines << (action == u"restart" ? QCoreApplication::translate("PosService", "Restarting its app…") : QCoreApplication::translate("PosService", "Starting…"));
    const QString program = sh->billboardScript.isEmpty() ? u"/usr/local/bin/vt-add-billboard"_s : sh->billboardScript;

    auto *proc = new QProcess(sh);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    auto buffer = std::make_shared<QByteArray>();
    const auto readLines = [sh, proc, name, buffer](bool all) {
        buffer->append(proc->readAll());
        bool any = false;
        for (qsizetype nl = buffer->indexOf('\n'); nl >= 0; nl = buffer->indexOf('\n')) {
            any = takeLine(sh, name, QString::fromUtf8(buffer->left(nl)).trimmed()) || any;
            buffer->remove(0, nl + 1);
        }
        if (all && !buffer->trimmed().isEmpty()) {
            any = takeLine(sh, name, QString::fromUtf8(*buffer).trimmed()) || any;
            buffer->clear();
        }
        return any;
    };
    QObject::connect(proc, &QProcess::readyReadStandardOutput, sh, [sh, readLines] {
        if (readLines(false))
            touched(sh);
    });
    QObject::connect(proc, &QProcess::finished, sh, [sh, proc, name, readLines](int code, QProcess::ExitStatus status) {
        readLines(true);
        PosShared::BillboardJob &j = sh->billboardJobs[name];
        j.running = false;
        j.ok = status == QProcess::NormalExit && code == 0;
        if (!j.ok && j.lines.size() <= 1)
            j.lines << QCoreApplication::translate("PosService", "It stopped without saying why.");
        proc->deleteLater();
        sh->saveSettings();   // who made it (Fire TV or not), learned on the way; and every screen told
    });
    QObject::connect(proc, &QProcess::errorOccurred, sh, [sh, proc, name, program](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        PosShared::BillboardJob &j = sh->billboardJobs[name];
        j.running = false;
        j.ok = false;
        j.lines << QCoreApplication::translate("PosService", "%1 isn't on the store's computer: install the scripts there (vt-install).").arg(program);
        proc->deleteLater();
        touched(sh);
    });
    // Never left hanging (a TV that stops answering half way).
    QTimer::singleShot(10 * 60 * 1000, proc, [proc] { proc->kill(); });
    proc->start(program, args);
    touched(sh);
}

} // namespace

bool PosService::billboardShowingAt(const TerminalConfig &t, const PosSettings &s, std::int64_t now)
{
    if (!t.billboardOn)
        return false;
    if (overrideActive(t, now))
        return t.billboardOverride == "on";
    const QDateTime at = QDateTime::fromMSecsSinceEpoch(now);
    return billboardOpenAt(billboardHoursOf(t, s), at.date().dayOfWeek(), at.time().hour() * 60 + at.time().minute());
}

std::int64_t PosService::billboardNextChange(const std::vector<std::string> &hours, std::int64_t now)
{
    if (hours.empty())
        return 0;
    const auto openAt = [&](std::int64_t ms) {
        const QDateTime at = QDateTime::fromMSecsSinceEpoch(ms);
        return billboardOpenAt(hours, at.date().dayOfWeek(), at.time().hour() * 60 + at.time().minute());
    };
    const bool open = openAt(now);
    const std::int64_t minute = 60 * 1000;
    const std::int64_t first = now - now % minute + minute;
    for (std::int64_t i = 0; i <= 8 * 24 * 60; ++i)
        if (openAt(first + i * minute) != open)
            return first + i * minute;
    return 0;
}

QString PosService::billboardDayText(const std::string &kept)
{
    if (kept.empty() || kept == "closed")
        return QString::fromStdString(kept);
    const QStringList parts = QString::fromStdString(kept).split(u'-');
    if (parts.size() != 2)
        return QString::fromStdString(kept);
    // English AM / PM in every language: typed back the same way (billboardDayHours).
    const auto text = [](const QString &hhmm) {
        return QLocale::c().toString(QTime::fromString(hhmm, u"HH:mm"_s), u"h:mm AP"_s);
    };
    return text(parts[0]) + u'-' + text(parts[1]);
}

bool PosService::migrateBillboardsFile(PosSettings &settings, const QString &file)
{
    bool changed = false;
    for (const QString &path : {file, file + u".new"_s}) {
        QFile f(path);
        if (path.isEmpty() || !f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        for (const QByteArray &raw : f.readAll().split('\n')) {
            const QString line = QString::fromUtf8(raw).trimmed();
            const qsizetype bar = line.lastIndexOf(u'|');
            if (bar <= 0)
                continue;
            const QString name = line.left(bar).trimmed();
            const QString ip = line.mid(bar + 1).trimmed();
            if (TerminalConfig *t = billboardNamed(settings, name); t && t->billboardIp.empty() && !ip.isEmpty()) {
                t->billboardIp = ip.toStdString();
                changed = true;
            }
        }
    }
    return changed;
}

const std::vector<std::string> &PosService::billboardHoursOf(const TerminalConfig &t, const PosSettings &s)
{
    return t.billboardHoursOwn ? t.billboardHours : s.billboardDefaultHours;
}

int PosService::billboardVolumeOf(const TerminalConfig &t, const PosSettings &s)
{
    return t.billboardVolumeOwn ? t.billboardVolume : s.billboardDefaultVolume;
}

bool PosService::billboardMuteOf(const TerminalConfig &t, const PosSettings &s)
{
    return t.billboardMuteOwn ? t.billboardMute : s.billboardDefaultMute;
}

const std::vector<std::string> &PosService::billboardSlidesOf(const TerminalConfig &t, const PosSettings &s)
{
    return t.billboardOwnSlides ? t.billboardSlides : s.displaySlides;
}

namespace {

QString joinLines(const std::vector<std::string> &list)
{
    QStringList l;
    for (const std::string &x : list)
        l << QString::fromStdString(x);
    return l.join(u'\n');
}

std::vector<std::string> fromLines(const QString &text)
{
    std::vector<std::string> out;
    for (const QString &line : text.split(u'\n', Qt::SkipEmptyParts))
        if (!line.trimmed().isEmpty())
            out.push_back(line.trimmed().toStdString());
    return out;
}

const char *const kDays[] = {QT_TR_NOOP("Monday"), QT_TR_NOOP("Tuesday"), QT_TR_NOOP("Wednesday"), QT_TR_NOOP("Thursday"),
                             QT_TR_NOOP("Friday"), QT_TR_NOOP("Saturday"), QT_TR_NOOP("Sunday")};

QString dayOf(const std::vector<std::string> &hours, int d)
{
    return PosService::billboardDayText(size_t(d) < hours.size() ? hours[size_t(d)] : std::string());
}

} // namespace

QVariantList PosService::billboards() const
{
    QVariantList out;
    if (!can(QString::fromLatin1(perm::Manager)))
        return out;
    const PosSettings &st = s_->settings;
    const std::int64_t now = s_->now();
    // First: the billboard defaults (the usual hours, volume and mute).
    {
        QVariantMap d{{u"_defaults"_s, true}, {u"_board"_s, false}, {u"name"_s, QString()},
                      {u"volume"_s, st.billboardDefaultVolume}, {u"mute"_s, st.billboardDefaultMute},
                      {u"_title"_s, tr("Billboard defaults")},
                      {u"_detail"_s, tr("The usual hours, volume and mute")}};
        for (int i = 0; i < 7; ++i)
            d.insert(u"day"_s + QString::number(i + 1), dayOf(st.billboardDefaultHours, i));
        out.append(d);
    }
    // Which are connected now, and the app each runs (Manager -> Network's list).
    QVariantList screens;
    if (s_->network)
        screens = s_->network().value(u"terminals"_s).toList();
    QVariantList promos;
    for (const QVariant &p : promotionsNow())
        promos.append(tr("Now: %1").arg(p.toString()));
    for (const TerminalConfig &t : st.terminals) {
        if (t.screen != "billboard")
            continue;
        const QString name = qs(t.name);
        QVariantMap seen;
        for (const QVariant &v : screens)
            if (v.toMap().value(u"name"_s).toString() == name)
                seen = v.toMap();
        const bool online = !seen.isEmpty();
        const bool showing = billboardShowingAt(t, st, now);
        const bool mute = billboardMuteOf(t, st);
        const int volume = billboardVolumeOf(t, st);
        const auto &hours = billboardHoursOf(t, st);
        // Its own values; null where it goes by the defaults (shown with "default").
        QVariantMap inherit{{u"volume"_s, st.billboardDefaultVolume}, {u"mute"_s, st.billboardDefaultMute}};
        QVariantMap r{{u"_board"_s, true}, {u"name"_s, name}, {u"ip"_s, qs(t.billboardIp)},
                      {u"port"_s, t.billboardPort ? QString::number(t.billboardPort) : QString()},
                      {u"enabled"_s, t.billboardOn},
                      {u"mute"_s, t.billboardMuteOwn ? QVariant(t.billboardMute) : QVariant()},
                      {u"volume"_s, t.billboardVolumeOwn ? QVariant(t.billboardVolume) : QVariant()},
                      {u"now"_s, overrideActive(t, now) ? qs(t.billboardOverride) : QString()},
                      {u"nowUntil"_s, overrideActive(t, now) ? qint64(t.billboardOverrideUntil) : qint64(0)},
                      {u"showing"_s, showing}, {u"online"_s, online}, {u"muted"_s, mute}, {u"loudness"_s, volume},
                      {u"version"_s, seen.value(u"version"_s).toString()},
                      {u"address"_s, seen.value(u"address"_s).toString()},
                      {u"paired"_s, !t.key.empty()}, {u"maker"_s, qs(t.billboardMaker)},
                      // A TV that isn't a Fire TV (Google TV...) pairs with a code: those fields then.
                      {u"_askPairing"_s, !t.billboardMaker.empty() && t.billboardMaker != "Amazon"},
                      {u"pairPort"_s, QString()}, {u"pairCode"_s, QString()},
                      {u"ownContent"_s, t.billboardOwnSlides},
                      {u"slides"_s, joinLines(billboardSlidesOf(t, st))},
                      // For Preview: the guest screen's list, the running promotions, the ribbon.
                      {u"_guestSlides"_s, joinLines(st.displaySlides)}, {u"_promotions"_s, promos},
                      {u"_logo"_s, qs(st.displayLogo)}, {u"_accent"_s, qs(st.displayAccent)},
                      {u"_storeName"_s, qs(st.storeName)}};
        QStringList kept;
        for (int d = 0; d < 7; ++d) {
            kept << (size_t(d) < hours.size() ? qs(hours[size_t(d)]) : QString());
            const QString key = u"day"_s + QString::number(d + 1);
            r.insert(key, t.billboardHoursOwn ? QVariant(dayOf(t.billboardHours, d)) : QVariant());
            inherit.insert(key, dayOf(st.billboardDefaultHours, d));
        }
        r.insert(u"hours"_s, kept);
        r.insert(u"_inherit"_s, inherit);
        if (const auto j = s_->billboardJobs.find(t.name); j != s_->billboardJobs.end())
            r.insert(u"job"_s, jobMap(j->second));
        const QString state = !t.billboardOn ? tr("off") : showing ? tr("on") : tr("dark");
        r.insert(u"status"_s, QStringList{online ? tr("Online") : tr("Offline"), state,
                                          mute ? tr("muted") : tr("sound %1%").arg(volume)}
                                  .join(u" · "_s)
                                  + (online && !seen.value(u"version"_s).toString().isEmpty()
                                         ? tr(" · app %1").arg(seen.value(u"version"_s).toString()) : QString()));
        r.insert(u"_title"_s, name);
        QStringList detail{t.billboardIp.empty() ? tr("no IP yet") : qs(t.billboardIp), online ? tr("online") : tr("offline"), state};
        if (mute)
            detail << tr("muted");
        r.insert(u"_detail"_s, detail.join(u" · "_s));
        out.append(r);
    }
    return out;
}

QVariantList PosService::billboardFields() const
{
    const auto board = [](QVariantMap f) { return with(f, u"when"_s, u"_board"_s); };
    const auto inherits = [](QVariantMap f) { return with(f, u"inheritable"_s, true); };
    QVariantList out{
        board(field(u"name"_s, tr("Name"), u"string"_s, tr("What it's called here: Bar TV, Front window."))),
        board(field(u"ip"_s, tr("IP address"), u"string"_s,
                    tr("On the Fire TV: Settings > Network, highlight its network, press Play/Pause. Any address the "
                       "store's computer reaches, another store's over the VPN too."))),
        board(field(u"status"_s, tr("Now"), u"info"_s)),
        board(field(u"enabled"_s, tr("On (shown at all)"), u"bool"_s, tr("Off: dark whatever its hours say."))),
        board(with(field(u"now"_s, tr("On or off now"), u"enum"_s,
                         tr("For now only: back to its hours at their next change.")),
                   u"options"_s, options({{"", "Follow its hours"}, {"on", "On now"}, {"off", "Off now"}}))),
        inherits(field(u"mute"_s, tr("Mute all videos"), u"bool"_s)),
        inherits(with(with(field(u"volume"_s, tr("Volume (percent)"), u"int"_s, tr("Its videos with sound, this loud.")),
                           u"min"_s, 0), u"max"_s, 100)),
    };
    for (int d = 0; d < 7; ++d)
        out.append(inherits(field(u"day"_s + QString::number(d + 1), tr(kDays[d]), u"string"_s,
                                  d == 0 ? tr("Like 9:00 AM-9:00 PM, or closed. Empty: all day. Every day empty: always on.") : QString())));
    // What it shows.
    out.append(board(field(u"ownContent"_s, tr("Its own messages, pictures and videos"), u"bool"_s,
                           tr("Off: the same as the guest screen's (Store Settings > Customer display), as that changes."))));
    out.append(with(with(with(with(asRows(field(u"slides"_s, tr("Content, in turn"), u"text"_s,
                                           tr("Each message 7 seconds, each picture 10, each video until it ends (with Sound "
                                              "on, unless the billboard is muted). Pictures and videos come from Dropbox > POS "
                                              "project > Guest Screen.")),
                                     tr("+ Add a message")),
                              u"addPicture"_s, tr("+ Add a picture or video")),
                         u"guestList"_s, false),   // not added to the guest screen's own list
                    u"when"_s, u"ownContent"_s),
                    u"reorder"_s, true));
    // A TV that isn't a Fire TV: Android TV / Google TV's wireless debugging, with a pairing code.
    for (const QVariantMap &f : {field(u"port"_s, tr("ADB port"), u"string"_s,
                                       tr("On the TV: Developer options > Wireless debugging: the port after the address. Empty: 5555.")),
                                 field(u"pairPort"_s, tr("Pairing port"), u"string"_s,
                                       tr("On the TV: Developer options > Wireless debugging > Pair device with pairing code.")),
                                 field(u"pairCode"_s, tr("Pairing code"), u"string"_s, tr("The 6 digits it shows (once).")) })
        out.append(with(f, u"when"_s, u"_askPairing"_s));
    return out;
}

namespace {

// Seven days typed on the page: as kept, or what's wrong (empty: fine). A day left null (going
// by the defaults) is `fallback`'s.
QString readDays(const QVariantMap &record, const std::vector<std::string> &fallback, std::vector<std::string> &hours,
                 bool &anyTyped)
{
    hours.clear();
    anyTyped = false;
    bool any = false;
    for (int d = 0; d < 7; ++d) {
        const QVariant v = record.value(u"day"_s + QString::number(d + 1));
        std::optional<std::string> h;
        if (v.isNull() || !v.isValid()) {
            h = size_t(d) < fallback.size() ? fallback[size_t(d)] : std::string();
        } else {
            anyTyped = true;
            const QString typed = v.toString();
            // In this language too ("cerrado").
            const QString plain = typed.trimmed().compare(QCoreApplication::translate("PosService", "closed"), Qt::CaseInsensitive) == 0
                                      ? u"closed"_s : typed;
            h = PosService::billboardDayHours(plain);
            if (!h)
                return QCoreApplication::translate("PosService", "%1: \"%2\" isn't hours like 9:00 AM-9:00 PM, or closed.")
                    .arg(QCoreApplication::translate("PosService", kDays[d]), typed);
        }
        any = any || !h->empty();
        hours.push_back(*h);
    }
    if (!any)
        hours.clear();   // every day all day: always on
    return {};
}

bool isSet(const QVariantMap &r, const QString &key)
{
    const QVariant v = r.value(key);
    return v.isValid() && !v.isNull();
}

} // namespace

bool PosService::saveBillboardRecord(int index, const QVariantMap &record)
{
    // The defaults first (index 0), then the billboards.
    if (index == 0) {
        std::vector<std::string> hours;
        bool typed = false;
        QVariantMap r = record;
        for (int d = 1; d <= 7; ++d)   // the defaults have every day (empty: all day)
            if (!isSet(r, u"day"_s + QString::number(d)))
                r.insert(u"day"_s + QString::number(d), QString());
        if (const QString wrong = readDays(r, {}, hours, typed); !wrong.isEmpty())
            return fail(wrong);
        s_->settings.billboardDefaultHours = hours;
        if (isSet(record, u"volume"_s))
            s_->settings.billboardDefaultVolume = std::clamp(record.value(u"volume"_s).toInt(), 0, 100);
        if (isSet(record, u"mute"_s))
            s_->settings.billboardDefaultMute = record.value(u"mute"_s).toBool();
        settingsChanged();
        return true;
    }
    auto list = billboardList(s_->settings);
    if (index > int(list.size()))
        return false;
    const QString name = record.value(u"name"_s).toString().trimmed();
    if (name.isEmpty())
        return fail(tr("The billboard needs a name."));
    TerminalConfig *mine = index > 0 ? list[size_t(index - 1)] : nullptr;
    for (const TerminalConfig &t : s_->settings.terminals)
        if (&t != mine && qs(t.name) == name)
            return fail(tr("%1 is already a screen's name.").arg(name));
    const QString ip = record.value(u"ip"_s).toString().trimmed();
    static const QRegularExpression address(uR"(^[A-Za-z0-9][A-Za-z0-9.:\-]*$)"_s);
    // Empty: one that isn't deployed from here (a computer with --screen billboard); Deploy asks for it.
    if (!ip.isEmpty() && (!address.match(ip).hasMatch() || ip.size() > 100))
        return fail(tr("Type the TV's IP address, like 192.168.1.60."));
    const QString portText = record.value(u"port"_s).toString().trimmed();
    const int port = portText.isEmpty() ? 0 : portText.toInt();
    if (!portText.isEmpty() && (port < 1 || port > 65535))
        return fail(tr("The ADB port is a number up to 65535 (or empty: 5555)."));
    std::vector<std::string> hours;
    bool hoursOwn = false;
    if (const QString wrong = readDays(record, s_->settings.billboardDefaultHours, hours, hoursOwn); !wrong.isEmpty())
        return fail(wrong);
    const QString now = record.value(u"now"_s).toString();
    if (!QStringList{QString(), u"on"_s, u"off"_s}.contains(now))
        return fail(tr("Choose whether it follows its hours."));

    TerminalConfig t = mine ? *mine : TerminalConfig{};
    const std::string oldName = t.name;
    const std::vector<std::string> oldHours = billboardHoursOf(t, s_->settings);
    t.name = ss(name);
    t.screen = "billboard";
    if (t.billboardIp != ss(ip))
        t.billboardMaker.clear();   // another TV, perhaps
    t.billboardIp = ss(ip);
    t.billboardPort = port;
    t.billboardHoursOwn = hoursOwn;
    t.billboardHours = hoursOwn ? hours : std::vector<std::string>{};
    t.billboardOn = record.value(u"enabled"_s, true).toBool();
    t.billboardMuteOwn = isSet(record, u"mute"_s);
    t.billboardMute = t.billboardMuteOwn && record.value(u"mute"_s).toBool();
    t.billboardVolumeOwn = isSet(record, u"volume"_s);
    t.billboardVolume = t.billboardVolumeOwn ? std::clamp(record.value(u"volume"_s).toInt(), 0, 100) : 100;
    if (record.contains(u"ownContent"_s)) {
        t.billboardOwnSlides = record.value(u"ownContent"_s).toBool();
        if (t.billboardOwnSlides)
            t.billboardSlides = fromLines(record.value(u"slides"_s).toString());
    }
    const std::int64_t at = s_->now();
    const QString was = overrideActive(t, at) ? qs(t.billboardOverride) : QString();
    if (now.isEmpty()) {
        t.billboardOverride.clear();
        t.billboardOverrideUntil = 0;
    } else if (now != was || oldHours != billboardHoursOf(t, s_->settings)) {
        // On or off now, until its hours next turn it on or off.
        t.billboardOverride = ss(now);
        t.billboardOverrideUntil = billboardNextChange(billboardHoursOf(t, s_->settings), at);
    }
    if (mine) {
        *mine = t;
        if (oldName != t.name) {
            if (auto j = s_->billboardJobs.extract(oldName))
                s_->billboardJobs[t.name] = j.mapped();
        }
    } else {
        s_->settings.terminals.push_back(t);
    }
    settingsChanged();
    return true;
}

bool PosService::setBillboard(const QString &name, const QVariantMap &settings)
{
    if (!require(perm::Manager, tr("Billboard settings")))
        return false;
    TerminalConfig *t = billboardNamed(s_->settings, name);
    if (!t)
        return fail(tr("%1 is not a billboard.").arg(name));
    if (settings.contains(u"hours"_s)) {
        std::vector<std::string> hours;
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
        t->billboardHours = hours;
        t->billboardHoursOwn = true;
    }
    if (settings.contains(u"enabled"_s))
        t->billboardOn = settings.value(u"enabled"_s).toBool();
    if (settings.contains(u"mute"_s)) {
        t->billboardMute = settings.value(u"mute"_s).toBool();
        t->billboardMuteOwn = true;
    }
    if (settings.contains(u"volume"_s)) {
        t->billboardVolume = std::clamp(settings.value(u"volume"_s).toInt(), 0, 100);
        t->billboardVolumeOwn = true;
    }
    if (settings.contains(u"now"_s)) {
        const QString now = settings.value(u"now"_s).toString();
        if (!QStringList{QString(), u"on"_s, u"off"_s}.contains(now))
            return fail(tr("Choose whether it follows its hours."));
        t->billboardOverride = ss(now);
        t->billboardOverrideUntil = now.isEmpty() ? 0 : billboardNextChange(billboardHoursOf(*t, s_->settings), s_->now());
    }
    settingsChanged();
    touched(s_);   // the billboard follows at once
    return true;
}

bool PosService::applyBillboardContent(const QString &name, const QStringList &targets)
{
    if (!require(perm::Manager, tr("Billboard settings")))
        return false;
    const TerminalConfig *from = billboardNamed(s_->settings, name);
    if (!from)
        return fail(tr("%1 is not a billboard.").arg(name));
    const std::vector<std::string> slides = billboardSlidesOf(*from, s_->settings);
    QStringList done;
    for (const QString &target : targets) {
        if (target == u"@guest") {
            s_->settings.displaySlides = slides;
            done << tr("the guest screen");
        } else if (TerminalConfig *t = billboardNamed(s_->settings, target); t && t != from) {
            t->billboardOwnSlides = true;
            t->billboardSlides = slides;
            done << target;
        }
    }
    if (done.isEmpty())
        return fail(tr("Choose where to copy it to."));
    settingsChanged();
    touched(s_);
    logChange(panelName(u"billboards"_s), name, tr("Content"), QString(), tr("Copied to %1").arg(done.join(u", "_s)));
    emit notice(tr("%1's content copied to %2.").arg(name, done.join(u", "_s)));
    return true;
}

bool PosService::billboardAction(const QString &name, const QString &action, const QVariantMap &args)
{
    if (!require(perm::Manager, tr("Billboard settings")))
        return false;
    TerminalConfig *t = billboardNamed(s_->settings, name);
    if (!t)
        return fail(tr("%1 is not a billboard.").arg(name));
    if (action == u"identify") {
        s_->billboardIdentify[t->name] = s_->now() + 10 * 1000;
        // Its name off the TV again after the 10 seconds.
        QTimer::singleShot(10 * 1000 + 200, s_, [sh = s_] { emit sh->adminChanged(); });
        touched(s_);
        emit notice(tr("%1 shows its name for 10 seconds.").arg(name));
        return true;
    }
    if (action != u"deploy" && action != u"restart")
        return fail(tr("That isn't something a billboard does."));
    if (t->billboardIp.empty())
        return fail(tr("Type %1's IP address first, and Save.").arg(name));
    if (const auto j = s_->billboardJobs.find(t->name); j != s_->billboardJobs.end() && j->second.running)
        return fail(tr("%1 is busy: wait for it to finish.").arg(name));
    QStringList a{name, qs(t->billboardIp)};
    if (t->billboardPort)
        a << u"--port"_s << QString::number(t->billboardPort);
    if (action == u"restart") {
        a << u"--restart"_s;
    } else {
        const QString pairPort = args.value(u"pairPort"_s).toString().trimmed();
        const QString pairCode = args.value(u"pairCode"_s).toString().trimmed();
        static const QRegularExpression digits(u"^[0-9]{1,6}$"_s);
        if (!pairPort.isEmpty() || !pairCode.isEmpty()) {
            if (!digits.match(pairPort).hasMatch() || !digits.match(pairCode).hasMatch())
                return fail(tr("The pairing port and code are the numbers the TV shows under Pair device with pairing code."));
            a << u"--pair-port"_s << pairPort << u"--pair-code"_s << pairCode;
        }
        if (args.value(u"pairAgain"_s).toBool())
            a << u"--pair-again"_s;
    }
    runBillboardScript(s_, t->name, action, a);
    return true;
}

bool PosService::removeBillboard(const QString &name, const QString &managerPin)
{
    if (!require(perm::Manager, tr("Billboard settings")))
        return false;
    const Employee *m = employeeByPin(managerPin);
    if (!m || !m->can(perm::Manager))
        return fail(pinRefused(tr("That isn't a manager's PIN.")));
    auto &list = s_->settings.terminals;
    const auto it = std::ranges::find_if(list, [&](const TerminalConfig &t) { return t.screen == "billboard" && qs(t.name) == name; });
    if (it == list.end())
        return fail(tr("%1 is not a billboard.").arg(name));
    if (const auto j = s_->billboardJobs.find(it->name); j != s_->billboardJobs.end() && j->second.running)
        return fail(tr("%1 is busy: wait for it to finish.").arg(name));
    s_->billboardJobs.erase(it->name);
    s_->billboardIdentify.erase(it->name);
    list.erase(it);   // its pairing with it: the TV can't connect again (PosServer::dropRevoked)
    // Off vt-add-billboard's list too, so it doesn't come back.
    if (!s_->billboardsFile.isEmpty()) {
        QFile in(s_->billboardsFile);
        if (in.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray kept;
            for (const QByteArray &line : in.readAll().split('\n')) {
                const qsizetype bar = line.lastIndexOf('|');
                if (line.trimmed().isEmpty() || (bar > 0 && QString::fromUtf8(line.left(bar)).trimmed() == name))
                    continue;
                kept += line + '\n';
            }
            in.close();
            QSaveFile out(s_->billboardsFile);
            if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
                out.write(kept);
                out.commit();
            }
        }
    }
    settingsChanged();
    touched(s_);
    logChange(panelName(u"billboards"_s), name, QString(), QString(), tr("Removed"));
    emit notice(tr("%1 removed. To use that TV again: + Add, then Deploy.").arg(name));
    return true;
}

} // namespace vt::app
