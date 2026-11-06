// Site Setup: this Pi's hardware settings (the Hardware Setup page, WidgetSiteSetup.qml), kept in the
// same files the vt-site / vt-directv scripts read, so the screen and the terminal always agree:
//   ~/.config/viewtouch/site.conf   SITE_NAME, SERVER_ADDR, SERVER_PORT, SERIAL_SCANNER, SERIAL_SCANNER_BAUD
//   ~/.config/viewtouch/tablets     Name|IP per line
//   ~/.config/viewtouch/tvs.json    TVs and DIRECTV receivers (vt-directv scan --save)
// Per Pi, never in Git and not in the database: the store-wide settings (printers, receipts, taxes)
// stay in Store Settings.
#include "guestslides.hh"
#include "layoutcontroller.hh"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

QString siteDir() { return QDir::home().filePath(u".config/viewtouch"_s); }

const QStringList kSiteKeys{u"SITE_NAME"_s, u"SERVER_ADDR"_s, u"SERVER_PORT"_s, u"SERIAL_SCANNER"_s,
                            u"SERIAL_SCANNER_BAUD"_s};

// KEY="value" lines (comments and anything else skipped).
QMap<QString, QString> readSiteConf()
{
    QMap<QString, QString> out;
    QFile f(QDir(siteDir()).filePath(u"site.conf"_s));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    static const QRegularExpression line(uR"(^\s*([A-Z_]+)=(.*)$)"_s);
    for (const QByteArray &raw : f.readAll().split('\n')) {
        const auto m = line.match(QString::fromUtf8(raw));
        if (!m.hasMatch())
            continue;
        QString v = m.captured(2).trimmed();
        if (v.size() >= 2 && (v.startsWith(u'"') || v.startsWith(u'\'')) && v.endsWith(v.front()))
            v = v.mid(1, v.size() - 2);
        out.insert(m.captured(1), v);
    }
    return out;
}

// What a shell file can hold safely between double quotes.
QString clean(QString v)
{
    v.remove(QRegularExpression(uR"(["\\$`|\r\n])"_s));
    return v.trimmed().left(120);
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
}

QVariantList readTablets()
{
    QVariantList out;
    const QString dir = siteDir();
    QFile f(QDir(dir).filePath(u"tablets"_s));
    if (f.open(QIODevice::ReadOnly)) {
        for (const QByteArray &raw : f.readAll().split('\n')) {
            const QString l = QString::fromUtf8(raw).trimmed();
            if (l.isEmpty() || l.startsWith(u'#') || !l.contains(u'|'))
                continue;
            out.append(QVariantMap{{u"name"_s, l.section(u'|', 0, 0).trimmed()}, {u"ip"_s, l.section(u'|', 1).trimmed()}});
        }
        return out;
    }
    QFile old(QDir::home().filePath(u".config/viewtouch_tablets.list"_s));   // before vt-site
    if (old.open(QIODevice::ReadOnly)) {
        int n = 0;
        for (const QByteArray &raw : old.readAll().split('\n'))
            if (const QString ip = QString::fromUtf8(raw).trimmed(); !ip.isEmpty())
                out.append(QVariantMap{{u"name"_s, u"Tablet %1"_s.arg(++n)}, {u"ip"_s, ip}});
    }
    return out;
}

QString firstLine(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readLine()).trimmed() : QString();
}

// A vt-* tool: installed in /usr/local/bin, or (one added since this Pi linked them) in the repo.
QString toolPath(const QString &name)
{
    const QString installed = u"/usr/local/bin/"_s + name;
    if (QFile::exists(installed))
        return installed;
    const QString repo = QDir::home().filePath(u"viewtouch-main/scripts/bin/"_s + name);
    return QFile::exists(repo) ? repo : installed;
}

QString thisAddress()
{
    for (const QHostAddress &a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() && !a.isLinkLocal())
            return a.toString();
    return {};
}

} // namespace

QVariantMap LayoutController::siteSettings() const
{
#ifdef Q_OS_ANDROID
    return {{u"android"_s, true}};
#else
    const QMap<QString, QString> conf = readSiteConf();
    QVariantMap out{{u"android"_s, false},
                    {u"siteName"_s, conf.value(u"SITE_NAME"_s)},
                    {u"serverAddr"_s, conf.value(u"SERVER_ADDR"_s)},
                    {u"serverPort"_s, conf.value(u"SERVER_PORT"_s, u"7719"_s).toInt()},
                    {u"thisAddress"_s, thisAddress()},
                    {u"scanner"_s, conf.value(u"SERIAL_SCANNER"_s)},
                    {u"scannerBaud"_s, conf.value(u"SERIAL_SCANNER_BAUD"_s, u"9600"_s).toInt()},
                    {u"configured"_s, QFile::exists(QDir(siteDir()).filePath(u"site.conf"_s))},
                    {u"lastScan"_s, lastScan_}, {u"lastScanAt"_s, lastScanAt_}};
    // The serial ports this Pi has now: by their stable names first, then any ttyUSB/ttyACM
    // without one.
    QVariantList ports;
    QStringList seen;
    const QDir byId(u"/dev/serial/by-id"_s);
    for (const QFileInfo &fi : byId.entryInfoList(QDir::System | QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString target = fi.symLinkTarget();
        seen << target;
        QString label = fi.fileName();
        label.remove(QRegularExpression(u"^usb-"_s)).replace(u'_', u' ');
        ports.append(QVariantMap{{u"path"_s, fi.absoluteFilePath()},
                                 {u"label"_s, u"%1 (%2)"_s.arg(label.left(60), QFileInfo(target).fileName())}});
    }
    for (const QFileInfo &fi : QDir(u"/dev"_s).entryInfoList({u"ttyUSB*"_s, u"ttyACM*"_s}, QDir::System, QDir::Name))
        if (!seen.contains(fi.absoluteFilePath()))
            ports.append(QVariantMap{{u"path"_s, fi.absoluteFilePath()}, {u"label"_s, fi.fileName()}});
    out.insert(u"scannerPorts"_s, ports);
    out.insert(u"tablets"_s, readTablets());
    QVariantList tvs;
    QFile tf(QDir(siteDir()).filePath(u"tvs.json"_s));
    if (tf.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(tf.readAll()).object();
        for (auto it = o.begin(); it != o.end(); ++it) {
            const QJsonObject tv = it.value().toObject();
            tvs.append(QVariantMap{{u"key"_s, it.key()}, {u"name"_s, tv.value(u"name").toString()},
                                   {u"kind"_s, tv.value(u"kind").toString(u"directv"_s)}, {u"ip"_s, tv.value(u"ip").toString()}});
        }
    }
    out.insert(u"tvs"_s, tvs);
    const QString guest = firstLine(QDir::home().filePath(u".vt-customer-display"_s));
    out.insert(u"guestDisplay"_s, !guest.isEmpty() && guest != u"off");
    out.insert(u"guestTouchPort"_s, firstLine(QDir::home().filePath(u".vt-guest-touch"_s)));
    return out;
#endif
}

bool LayoutController::saveSiteSettings(const QVariantMap &values)
{
#ifdef Q_OS_ANDROID
    Q_UNUSED(values)
    return false;
#else
    if (pos_ && !pos_->can(u"admin"_s)) {
        setStatus(tr("Admin required."));
        return false;
    }
    // site.conf: the keys given replaced in place (comments and other lines kept), new ones added.
    QMap<QString, QString> set;
    const auto take = [&](const QString &field, const QString &key) {
        if (values.contains(field))
            set.insert(key, clean(values.value(field).toString()));
    };
    take(u"siteName"_s, u"SITE_NAME"_s);
    take(u"serverAddr"_s, u"SERVER_ADDR"_s);
    take(u"scanner"_s, u"SERIAL_SCANNER"_s);
    if (values.contains(u"serverPort"_s)) {
        const int port = values.value(u"serverPort"_s).toInt();
        if (port < 1 || port > 65535) {
            setStatus(tr("The server port is a number from 1 to 65535."));
            return false;
        }
        set.insert(u"SERVER_PORT"_s, QString::number(port));
    }
    if (values.contains(u"scannerBaud"_s)) {
        static const QList<int> bauds{1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
        const int baud = values.value(u"scannerBaud"_s).toInt();
        if (!bauds.contains(baud)) {
            setStatus(tr("The scanner speed is one of 1200 to 115200 baud."));
            return false;
        }
        set.insert(u"SERIAL_SCANNER_BAUD"_s, QString::number(baud));
    }
    const QString dir = siteDir();
    const QString confPath = QDir(dir).filePath(u"site.conf"_s);
    QStringList lines;
    QFile in(confPath);
    if (in.open(QIODevice::ReadOnly))
        lines = QString::fromUtf8(in.readAll()).split(u'\n');
    else
        lines = {u"# This site's hardware for the ViewTouch scripts (vt-site, Site Setup). Not in Git."_s};
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    for (auto it = set.begin(); it != set.end(); ++it) {
        const QString line = u"%1=\"%2\""_s.arg(it.key(), it.value());
        bool replaced = false;
        for (QString &l : lines)
            if (l.trimmed().startsWith(it.key() + u'=')) {
                l = line;
                replaced = true;
            }
        if (!replaced)
            lines << line;
    }
    if (!set.isEmpty() && !writeFile(confPath, (lines.join(u'\n') + u'\n').toUtf8())) {
        setStatus(tr("Couldn't save %1.").arg(confPath));
        return false;
    }
    // Tablets: the whole list as given.
    if (values.contains(u"tablets"_s)) {
        static const QRegularExpression ipv4(uR"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$)"_s);
        QString text = u"# This site's tablets, one per line: Name|IP (vt-site tablet add / remove, Site Setup)\n"_s;
        for (const QVariant &v : values.value(u"tablets"_s).toList()) {
            const QVariantMap t = v.toMap();
            const QString name = clean(t.value(u"name"_s).toString());
            const QString ip = t.value(u"ip"_s).toString().trimmed();
            if (name.isEmpty() && ip.isEmpty())
                continue;
            const auto m = ipv4.match(ip);
            bool ok = m.hasMatch();
            for (int i = 1; ok && i <= 4; ++i)
                ok = m.captured(i).toInt() <= 255;
            if (name.isEmpty() || !ok) {
                setStatus(tr("Each tablet needs a name and an address like 192.168.1.220."));
                return false;
            }
            text += name + u'|' + ip + u'\n';
        }
        if (!writeFile(QDir(dir).filePath(u"tablets"_s), text.toUtf8())) {
            setStatus(tr("Couldn't save the tablets."));
            return false;
        }
    }
    ++siteRevision_;
    emit siteChanged();
    setStatus(set.contains(u"SERIAL_SCANNER"_s) || set.contains(u"SERIAL_SCANNER_BAUD"_s)
                  ? tr("Site settings saved. Restart ViewTouch for a new scanner port or speed.")
                  : tr("Site settings saved."));
    return true;
#endif
}

void LayoutController::siteAction(const QString &action)
{
#ifdef Q_OS_ANDROID
    Q_UNUSED(action)
    setStatus(tr("Site Setup works on the Pi."));
#else
    if (pos_ && !pos_->can(u"admin"_s)) {
        setStatus(tr("Admin required."));
        return;
    }
    struct Tool { const char16_t *action; const char16_t *program; QStringList args; const char *busy; bool restarts; };
    // Screen check: a monitor's size and rate (vt-screen-mode): "best:HDMI-1" or
    // "mode:HDMI-1:1920x1080:60". Only a monitor, size and rate the last look found.
    QStringList modeArgs;
    if (action.startsWith(u"best:"_s) || action.startsWith(u"mode:"_s)) {
        const QStringList parts = action.split(u':');
        QVariantMap output;
        for (const QVariant &o : screens_.value(u"outputs"_s).toList())
            if (o.toMap().value(u"name"_s).toString() == parts.value(1))
                output = o.toMap();
        bool ok = !output.isEmpty() && (parts.size() == 2 ? parts[0] == u"best" : (parts.size() == 4 && parts[0] == u"mode"));
        if (ok && parts[0] == u"mode") {
            ok = false;
            for (const QVariant &m : output.value(u"modes"_s).toList())
                if (m.toMap().value(u"size"_s).toString() == parts[2])
                    for (const QVariant &r : m.toMap().value(u"rates"_s).toList())
                        ok = ok || QString::number(r.toDouble(), 'f', 2) == parts[3];
        }
        if (!ok) {
            setStatus(tr("That size isn't offered by that monitor. Look again."));
            return;
        }
        modeArgs = parts[0] == u"best" ? QStringList{parts[1], u"best"_s} : QStringList{parts[1], parts[2], parts[3]};
    }
    // Screen check: this monitor is the register (vt-guest-display on HDMI-1). Only a monitor the
    // last look found, so nothing else reaches the command line.
    QStringList registerArgs;
    if (action.startsWith(u"register:"_s)) {
        const QString output = action.mid(9);
        bool known = false;
        for (const QVariant &o : screens_.value(u"outputs"_s).toList())
            known = known || o.toMap().value(u"name"_s).toString() == output;
        static const QRegularExpression safe(uR"(^[A-Za-z0-9-]{1,24}$)"_s);
        if (!known || !safe.match(output).hasMatch()) {
            setStatus(tr("No monitor called %1 is plugged in. Look again.").arg(output));
            return;
        }
        registerArgs = {u"on"_s, output};
    }
    const Tool tools[] = {
        {u"screen-mode", u"vt-screen-mode", modeArgs, QT_TR_NOOP("Changing the monitor's size..."), false},
        {u"register", u"vt-guest-display", registerArgs,
         QT_TR_NOOP("Making that monitor the register: ViewTouch restarts..."), true},
        {u"touch-map", u"vt-touch-map", {u"--verbose"_s}, QT_TR_NOOP("Tying each touchscreen to its monitor..."), false},
        {u"check-tablets", u"vt-status", {}, QT_TR_NOOP("Checking the tablets..."), false},
        {u"push-tablets", u"vt-tablets", {}, QT_TR_NOOP("Sending the app to the tablets..."), false},
        {u"scan-tvs", u"vt-directv", {u"scan"_s, u"--save"_s},
         QT_TR_NOOP("Looking for DIRECTV receivers and smart TVs on this network (about 30 seconds)..."), false},
        {u"learn-touch", u"vt-guest-display", {u"touch"_s, u"--or-map"_s},
         QT_TR_NOOP("Touch the GUEST screen now (20 seconds)... tap a blank spot."), false},
        {u"guest-on", u"vt-guest-display", {u"on"_s}, QT_TR_NOOP("Turning the guest display on: ViewTouch restarts..."), true},
        {u"guest-off", u"vt-guest-display", {u"off"_s}, QT_TR_NOOP("Turning the guest display off: ViewTouch restarts..."), true},
        {u"restart", u"vt-server", {}, QT_TR_NOOP("Restarting ViewTouch..."), true},
    };
    const Tool *tool = nullptr;
    for (const Tool &t : tools)
        if (QStringView(t.action) == u"register"      ? !registerArgs.isEmpty()
            : QStringView(t.action) == u"screen-mode" ? !modeArgs.isEmpty()
                                                      : action == QStringView(t.action))
            tool = &t;
    if (!tool) {
        setStatus(tr("Unknown site tool: %1").arg(action));
        return;
    }
    const QString program = toolPath(QString::fromUtf16(tool->program));
    if (!QFile::exists(program)) {
        setStatus(tr("%1 isn't installed on this computer.").arg(QFileInfo(program).fileName()));
        return;
    }
    setStatus(tr(tool->busy));
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!env.contains(u"DISPLAY"_s))
        env.insert(u"DISPLAY"_s, u":0"_s);
    if (tool->restarts) {
        // It stops this ViewTouch: run on its own, outliving it.
        QProcess p;
        p.setProcessEnvironment(env);
        p.setProgram(u"setsid"_s);
        p.setArguments(QStringList{program} + tool->args);
        p.startDetached();
        return;
    }
    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(env);
    const bool screensAfter = action == u"touch-map" || action == u"learn-touch" || !modeArgs.isEmpty();
    connect(proc, &QProcess::finished, this, [this, proc, screensAfter](int, QProcess::ExitStatus) {
        QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        const QStringList rows = out.split(u'\n', Qt::SkipEmptyParts);
        // The outcome: the last line or two, not the whole log.
        out = rows.mid(std::max<qsizetype>(0, rows.size() - 2)).join(u" | "_s);
        setStatus(out.isEmpty() ? tr("Done.") : out);
        ++siteRevision_;
        emit siteChanged();
        proc->deleteLater();
        if (screensAfter)
            refreshScreens();   // the Screen check shows where the touches land now
    });
    QTimer::singleShot(10 * 60 * 1000, proc, [proc] { proc->kill(); });
    proc->start(program, tool->args);
#endif
}

QVariantMap LayoutController::screenCheck() const
{
#ifdef Q_OS_ANDROID
    return {{u"android"_s, true}};
#else
    QVariantMap out = screens_;
    out.insert(u"android"_s, false);
    out.insert(u"busy"_s, screensBusy_);
    // What ViewTouch itself sees, and which of its windows is on which monitor.
    QVariantList qt;
    const QScreen *primary = QGuiApplication::primaryScreen();
    for (QScreen *sc : QGuiApplication::screens()) {
        QVariantList windows;
        for (QWindow *win : QGuiApplication::topLevelWindows())
            if (win->isVisible() && win->screen() == sc && !win->title().isEmpty())
                windows.append(win->title());
        const QRect g = sc->geometry();
        qt.append(QVariantMap{{u"name"_s, sc->name()}, {u"x"_s, g.x()}, {u"y"_s, g.y()}, {u"w"_s, g.width()},
                              {u"h"_s, g.height()}, {u"primary"_s, sc == primary},
                              {u"model"_s, QStringList{sc->manufacturer(), sc->model()}.join(u' ').trimmed()},
                              {u"windows"_s, windows}});
    }
    out.insert(u"qtScreens"_s, qt);
    return out;
#endif
}

void LayoutController::refreshScreens()
{
#ifndef Q_OS_ANDROID
    if (screensBusy_)
        return;
    const QString program = toolPath(u"vt-screens"_s);
    if (!QFile::exists(program)) {
        screens_ = {{u"problems"_s, QStringList{tr("vt-screens isn't installed on this computer.")}}};
        ++siteRevision_;
        emit siteChanged();
        return;
    }
    screensBusy_ = true;
    auto *proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!env.contains(u"DISPLAY"_s))
        env.insert(u"DISPLAY"_s, u":0"_s);
    proc->setProcessEnvironment(env);
    connect(proc, &QProcess::finished, this, [this, proc](int, QProcess::ExitStatus) {
        const QJsonDocument doc = QJsonDocument::fromJson(proc->readAllStandardOutput());
        screens_ = doc.isObject() ? doc.object().toVariantMap()
                                  : QVariantMap{{u"problems"_s, QStringList{tr("Couldn't read the screens.")}}};
        screens_.insert(u"checkedAt"_s, QDateTime::currentMSecsSinceEpoch());
        screensBusy_ = false;
        // Something new to fix: said once on the status line, so whoever is at the register sees it.
        const QStringList problems = screens_.value(u"problems"_s).toStringList();
        const QString said = problems.join(u'\n');
        if (!problems.isEmpty() && said != screenProblemsSaid_)
            setStatus(tr("Screen check: %n thing(s) to fix (Manager > Hardware Configuration > Screen check). %1", "",
                         problems.size()).arg(problems.first()));
        screenProblemsSaid_ = said;
        ++siteRevision_;
        emit siteChanged();
        proc->deleteLater();
    });
    QTimer::singleShot(30 * 1000, proc, [proc] { proc->kill(); });
    proc->start(u"python3"_s, {program, u"--json"_s});
    ++siteRevision_;
    emit siteChanged();
#endif
}

void LayoutController::startScreenSelfCheck()
{
#ifndef Q_OS_ANDROID
    // Only on the computer that drives the monitors (the Pi): vt-screens there and a screen to check.
    if (!QFile::exists(toolPath(u"vt-screens"_s)) || QGuiApplication::screens().isEmpty()
        || QGuiApplication::platformName() == u"offscreen" || QGuiApplication::platformName() == u"minimal")
        return;
    QTimer::singleShot(20 * 1000, this, [this] { refreshScreens(); });
    screenCheckTimer_.setInterval(10 * 60 * 1000);
    connect(&screenCheckTimer_, &QTimer::timeout, this, &LayoutController::refreshScreens, Qt::UniqueConnection);
    screenCheckTimer_.start();
#endif
}

// --- Guest screen pictures: from Dropbox > POS project > Guest Screen (vt-guest-pictures) ---

namespace {

// Where vt-guest-pictures puts what it copies from Dropbox, and where pictures can be put by hand.
QStringList guestPictureFolders()
{
    return {QDir::home().filePath(u".cache/vt-guest-pictures"_s), QDir::home().filePath(u"Pictures/Guest Screen"_s)};
}

// A file right in one of those folders (not through a link to somewhere else).
bool inGuestPictureFolder(const QString &path)
{
    const QString file = QFileInfo(path).canonicalFilePath();
    if (file.isEmpty())
        return false;
    for (const QString &folder : guestPictureFolders()) {
        const QString dir = QFileInfo(folder).canonicalFilePath();
        if (!dir.isEmpty() && QFileInfo(file).absolutePath() == dir)
            return true;
    }
    return false;
}

const QStringList &guestVideoKinds()
{
    static const QStringList kinds{u"mp4"_s, u"mov"_s, u"m4v"_s};
    return kinds;
}

constexpr qint64 kGuestVideoMaxBytes = 500LL * 1024 * 1024;

} // namespace

void LayoutController::refreshGuestPictures()
{
#ifdef Q_OS_ANDROID
    guestPictures_ = {{u"error"_s, tr("Pictures for the guest screen are added on the Pi.")}};
    ++siteRevision_;
    emit siteChanged();
#else
    if (guestPicturesBusy_)
        return;
    const QString program = toolPath(u"vt-guest-pictures"_s);
    if (!QFile::exists(program)) {
        guestPictures_ = {{u"error"_s, tr("vt-guest-pictures isn't installed on this computer.")}};
        ++siteRevision_;
        emit siteChanged();
        return;
    }
    guestPicturesBusy_ = true;
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int, QProcess::ExitStatus) {
        const QJsonDocument doc = QJsonDocument::fromJson(proc->readAllStandardOutput());
        guestPictures_ = doc.isObject() ? doc.object().toVariantMap()
                                        : QVariantMap{{u"error"_s, tr("Couldn't read the list of pictures.")}};
        guestPicturesBusy_ = false;
        ++siteRevision_;
        emit siteChanged();
        proc->deleteLater();
    });
    QTimer::singleShot(30 * 60 * 1000, proc, [proc] { proc->kill(); });   // videos from Dropbox take longer
    proc->start(u"python3"_s, {program, u"list"_s});
    ++siteRevision_;
    emit siteChanged();
#endif
}

QVariantMap LayoutController::guestPictures() const
{
    QVariantMap out = guestPictures_;
    out.insert(u"busy"_s, guestPicturesBusy_);
    return out;
}

QString LayoutController::addGuestPicture(const QString &fileOrUrl)
{
    if (!pos_)
        return {};
    if (!pos_->can(u"manager"_s)) {
        setStatus(tr("A manager adds pictures to the guest screen."));
        return {};
    }
    const QString path = fileOrUrl.startsWith(u"file:") ? QUrl(fileOrUrl).toLocalFile() : fileOrUrl;
    const QFileInfo info(path);
    if (guestVideoKinds().contains(info.suffix().toLower()))
        return addGuestVideo(path);
    if (!inGuestPictureFolder(path)) {
        setStatus(tr("Pictures for the guest screen come from Dropbox > POS project > Guest Screen."));
        return {};
    }
    static const QStringList kinds{u"jpg"_s, u"jpeg"_s, u"png"_s, u"webp"_s};
    if (!kinds.contains(info.suffix().toLower()) || info.size() > 25 * 1024 * 1024) {
        setStatus(tr("The guest screen shows JPEG, PNG or WebP pictures of 25 MB at most."));
        return {};
    }
    // Upright as the phone took it, made to fit the guest screen (never larger than it was).
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        setStatus(tr("Can't open %1.").arg(info.fileName()));
        return {};
    }
    if (image.width() > 1920 || image.height() > 1080)
        image = image.scaled(QSize(1920, 1080), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // A JPEG has no see-through parts: those are black, like the guest screen.
    QImage flat(image.size(), QImage::Format_RGB32);
    flat.fill(Qt::black);
    {
        QPainter p(&flat);
        p.drawImage(0, 0, image);
    }
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    if (!buffer.open(QIODevice::WriteOnly) || !flat.save(&buffer, "JPEG", 85) || jpeg.isEmpty()) {
        setStatus(tr("Couldn't make %1 into a picture for the guest screen.").arg(info.fileName()));
        return {};
    }
    const QString ref = vt::app::PosSession::storeImageRef(u"guest-"_s + info.completeBaseName() + u".jpg"_s);
    const QString line = u"image:"_s + ref;
    // Kept by the store (so the standby, the backups and paired screens have it), then on the list
    // of slides: Store Settings saved as they are with that one more, so the change log has it.
    // (Here the answer comes at once: a refusal returns ""; a remote server's comes later.)
    QPointer<LayoutController> self(this);
    auto refused = std::make_shared<bool>(false);
    pos_->invoke(u"addStoreImage"_s, {ref.mid(6), QString::fromLatin1(jpeg.toBase64())}, [self, line, refused](const QVariant &ok) {
        *refused = !ok.toBool();
        if (!self || !self->pos_ || *refused)
            return;
        const QVariantList records = self->pos_->adminRecords(u"store"_s);
        if (records.isEmpty())
            return;
        QVariantMap record = records.first().toMap();
        QStringList slides = record.value(u"displaySlides"_s).toString().split(u'\n', Qt::SkipEmptyParts);
        if (slides.contains(line))
            return;   // already showing
        slides << line;
        record.insert(u"displaySlides"_s, slides.join(u'\n'));
        self->pos_->adminSave(u"store"_s, 0, record);
    });
    return *refused ? QString() : line;
}

QString LayoutController::addGuestVideo(const QString &fileOrUrl)
{
    if (!pos_)
        return {};
    if (!pos_->can(u"manager"_s)) {
        setStatus(tr("A manager adds videos to the guest screen."));
        return {};
    }
    const QString path = fileOrUrl.startsWith(u"file:") ? QUrl(fileOrUrl).toLocalFile() : fileOrUrl;
    const QFileInfo info(path);
    if (!inGuestPictureFolder(path)) {
        setStatus(tr("Videos for the guest screen come from Dropbox > POS project > Guest Screen."));
        return {};
    }
    if (!guestVideoKinds().contains(info.suffix().toLower()) || info.size() <= 0 || info.size() > kGuestVideoMaxBytes) {
        setStatus(tr("The guest screen plays MP4, MOV or M4V videos of 500 MB at most."));
        return {};
    }
    // Kept in ~/Pictures/Guest Screen as an MP4 made ready for the guest screen (vt-guest-video-prep:
    // a phone's HEVC converted to H.264, an upright video given blurred sides to fill the wide screen;
    // owner 2026-11-06), by its own name: without a '#' ("#sound" marks a video with its sound) and
    // without spaces at the ends (lines are trimmed). The Dropbox original is never changed.
    QDir keep(guestPictureFolders().last());
    if (!keep.mkpath(u"."_s)) {
        setStatus(tr("Can't make the folder %1.").arg(keep.path()));
        return {};
    }
    const QString name = info.completeBaseName().replace(u'#', u'-').trimmed() + u".mp4"_s;
    const QString source = info.canonicalFilePath();
    const QString target = QFileInfo(keep.absolutePath()).canonicalFilePath() + u'/' + name;
    if (target != source && !QFileInfo::exists(target)) {
        // Off the screen's thread (a minute or two on the Pi), under another name until it's all
        // there: the guest screen skips a video it can't open yet, and tries again later. Without
        // ffmpeg, or when it can't convert this one, the video is copied as it is.
        setStatus(tr("Preparing %1 for the guest screen (a minute or two); it plays once it's ready.").arg(name));
        QPointer<LayoutController> self(this);
        const QString prep = toolPath(u"vt-guest-video-prep"_s);
        QThreadPool::globalInstance()->start([self, source, target, name, prep] {
            bool ok = false;
            if (QFile::exists(prep)) {
                QProcess p;
                p.start(u"bash"_s, {prep, source, target});
                ok = p.waitForFinished(30 * 60 * 1000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0
                     && QFileInfo::exists(target);
            }
            if (!ok) {
                const QString part = target + u".part"_s;
                QFile::remove(part);
                ok = QFile::copy(source, part);
                if (ok) {
                    QFile::remove(target);
                    ok = QFile::rename(part, target);
                }
                if (!ok)
                    QFile::remove(part);
            }
            QMetaObject::invokeMethod(self, [self, name, ok] {
                if (self)
                    self->setStatus(ok ? tr("%1 is ready for the guest screen.").arg(name)
                                       : tr("Couldn't copy %1 to Pictures/Guest Screen. Is the Pi's card full?").arg(name));
            }, Qt::QueuedConnection);
        });
    }
    // On the list of slides (sound off), saved as a manager's change; one there already stays as it is.
    const QVariantList records = pos_->adminRecords(u"store"_s);
    if (records.isEmpty())
        return {};
    QVariantMap record = records.first().toMap();
    QStringList slides = record.value(u"displaySlides"_s).toString().split(u'\n', Qt::SkipEmptyParts);
    for (const QString &slide : slides)
        if (vt::guest::parseVideoSlide(slide.trimmed()).path == target)
            return slide.trimmed();
    const QString line = vt::guest::videoSlide(target, false);
    slides << line;
    record.insert(u"displaySlides"_s, slides.join(u'\n'));
    pos_->adminSave(u"store"_s, 0, record);
    return line;
}

QString LayoutController::guestVideoSound(const QString &slide, bool sound) const
{
    return vt::guest::withSound(slide, sound);
}
