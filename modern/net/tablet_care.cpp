#include "net/tablet_care.hh"

#include "net/remote_session.hh"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <deque>

#if defined(Q_OS_ANDROID)
#include <QCoreApplication>
#include <QJniObject>
#endif

using namespace Qt::StringLiterals;

namespace vt::net {

namespace {

constexpr int kKeptLines = 400;
QMutex logMutex;
std::deque<QString> logLines;   // the newest last
qint64 logLast = 0;             // the newest line's number
QtMessageHandler previousHandler = nullptr;

void logHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    static const char *const kinds[] = {"D", "W", "C", "F", "I"};
    const char *kind = type >= 0 && type <= 4 ? kinds[type] : "?";
    const QString category = context.category && qstrcmp(context.category, "default") != 0
                                 ? QString::fromLatin1(context.category) + u": "_s
                                 : QString();
    TabletCare::addLogLine(u"%1 %2 %3%4"_s.arg(QDateTime::currentDateTime().toString(u"MM-dd HH:mm:ss"_s),
                                               QString::fromLatin1(kind), category, message.left(400)));
    if (previousHandler)
        previousHandler(type, context, message);
}

#if defined(Q_OS_ANDROID)
QVariantMap jsonCall(const char *method, const char *signature, auto... args)
{
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return {};
    const QJniObject s = QJniObject::callStaticObjectMethod("org/viewtouch/pos/Kiosk", method, signature,
                                                               context.object(), args...);
    return QJsonDocument::fromJson(s.toString().toUtf8()).object().toVariantMap();
}
#endif

} // namespace

void TabletCare::addLogLine(const QString &line)
{
    QMutexLocker lock(&logMutex);
    logLines.push_back(line);
    ++logLast;
    while (logLines.size() > kKeptLines)
        logLines.pop_front();
}

void TabletCare::captureLog()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    previousHandler = qInstallMessageHandler(logHandler);
}

QStringList TabletCare::logSince(qint64 after, int max, qint64 *last)
{
    QMutexLocker lock(&logMutex);
    if (last)
        *last = logLast;
    const qint64 first = logLast - qint64(logLines.size()) + 1;   // the oldest kept line's number
    const qint64 from = std::max({after + 1, first, logLast - max + 1});
    QStringList out;
    for (qint64 n = from; n <= logLast; ++n)
        out << logLines[size_t(n - first)];
    return out;
}

TabletCare::Platform TabletCare::androidPlatform()
{
    Platform p;
#if defined(Q_OS_ANDROID)
    p.debugging = [](bool connecting) {
        return jsonCall("debuggingState", "(Landroid/content/Context;Z)Ljava/lang/String;", jboolean(connecting));
    };
    p.health = [] { return jsonCall("health", "(Landroid/content/Context;)Ljava/lang/String;"); };
    p.restarts = [] {
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        return context.isValid() ? int(QJniObject::callStaticMethod<jint>("org/viewtouch/pos/Kiosk", "restarts",
                                                                          "(Landroid/content/Context;)I", context.object()))
                                 : 0;
    };
    p.restartApp = [](const QString &why) {
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (context.isValid())
            QJniObject::callStaticMethod<void>("org/viewtouch/pos/Kiosk", "restartApp",
                                               "(Landroid/content/Context;Ljava/lang/String;)V", context.object(),
                                               QJniObject::fromString(why).object<jstring>());
    };
#endif
    return p;
}

TabletCare::TabletCare(RemoteSession *remote, Platform platform, QObject *parent)
    : QObject(parent)
    , remote_(remote)
    , platform_(std::move(platform))
{
    passTimer_.setInterval(15 * 60 * 1000);
    connect(&passTimer_, &QTimer::timeout, this, [this] { pass(); });
    passTimer_.start();
    checkTimer_.setInterval(30 * 1000);
    connect(&checkTimer_, &QTimer::timeout, this, &TabletCare::check);
    checkTimer_.start();
    // The store back (just started, or after Wi-Fi came up): a pass at once.
    connect(remote_, &RemoteSession::onlineChanged, this, [this] {
        if (remote_->online())
            pass(true);
    });
    QTimer::singleShot(0, this, [this] { pass(true); });
}

void TabletCare::setTimes(int passMs, int checkMs, qint64 reconnectAfterMs, qint64 restartAfterMs)
{
    passTimer_.start(passMs);
    checkTimer_.start(checkMs);
    reconnectAfterMs_ = reconnectAfterMs;
    restartAfterMs_ = restartAfterMs;
}

void TabletCare::pass(bool connecting)
{
    // 1. Wireless debugging (on again if Android turned it off), even without the store.
    const QVariantMap adb = platform_.debugging ? platform_.debugging(connecting) : QVariantMap();
    if (!remote_->online())
        return;
    if (!adb.isEmpty())
        remote_->reportAdbState(adb);
    // 2. An update, if the store has one (installed only while nobody is signed in: Main.qml).
    const QVariantMap info = remote_->updateInfo();
    if (info.value(u"silent"_s).toBool() && info.value(u"behind"_s).toBool() && info.contains(u"update"_s))
        remote_->autoUpdate();
    // 3. Health.
    QVariantMap health = platform_.health ? platform_.health() : QVariantMap();
    health.insert(u"restarts"_s, platform_.restarts ? platform_.restarts() : 0);
    health.insert(u"reconnects"_s, reconnects_);
    remote_->reportHealth(health);
    // 4. Its log since the last pass.
    qint64 last = 0;
    const QStringList lines = logSince(logSent_, 200, &last);
    remote_->sendLog(lines);
    logSent_ = last;
}

void TabletCare::check()
{
    const qint64 since = remote_->offlineSince();
    if (since <= 0 || remote_->isRejected())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 off = now - since;
    if (off > restartAfterMs_ && !remote_->loggedIn() && !restarting_ && platform_.restartApp) {
        restarting_ = true;
        qWarning().noquote() << "no store for" << off / 60000 << "min and nobody signed in: restarting the app";
        platform_.restartApp(u"no store for %1 min"_s.arg(off / 60000));
        return;
    }
    if (off > reconnectAfterMs_ && now - lastFresh_ > reconnectAfterMs_) {
        lastFresh_ = now;
        ++reconnects_;
        remote_->reconnectFresh();
    }
}

} // namespace vt::net
