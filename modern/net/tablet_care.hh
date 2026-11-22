#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <functional>

namespace vt::net {

class RemoteSession;

// A tablet looks after itself, so nobody has to with adb (the owner, 2026-11-22: "I can't sit here every
// day after launch being 24/7 IT support help desk!"). At start, whenever it gets the store back, and
// every 15 minutes, a pass:
//   1. Wireless debugging on again, and its state told to the store (Manager > Network, screens.json:
//      the Pi's vt-adb-ensure reaches the tablet by the port it reports).
//   2. Asks for an update (the self-update as before: installed only while nobody is signed in).
//   3. Its health: battery, charging, Wi-Fi signal, uptime, restarts and fresh reconnects.
//   4. Its own log lines since the last pass (at most 200) to the store (tablet-logs/<screen>.log).
// And meanwhile, every 30 s: "Reconnecting" for 3 minutes, it connects again from scratch; 10 minutes
// and nobody signed in, it restarts the app (counted).
// What only Android can do comes from `Platform` (androidPlatform(); tests: their own).
class TabletCare : public QObject {
    Q_OBJECT

public:
    struct Platform {
        std::function<QVariantMap(bool connecting)> debugging;   // {grant, adb, wifiAdb, port, tlsPort, tcpPort}
        std::function<QVariantMap()> health;                     // {battery, charging, rssi, uptime}
        std::function<int()> restarts;                           // app restarts so far (kept by the app)
        std::function<void(const QString &why)> restartApp;
    };
    static Platform androidPlatform();

    TabletCare(RemoteSession *remote, Platform platform, QObject *parent = nullptr);

    // Tests: short times.
    void setTimes(int passMs, int checkMs, qint64 reconnectAfterMs, qint64 restartAfterMs);
    void pass(bool connecting = false);
    int reconnects() const { return reconnects_; }

    // The app's own log lines (Qt's messages), kept from now on (the last 400), for the store.
    static void captureLog();
    // Lines after `after` (a line number), at most `max` (the newest); `last` gets the newest number.
    static QStringList logSince(qint64 after, int max, qint64 *last);
    static void addLogLine(const QString &line);   // (captureLog's handler; tests)

private:
    void check();

    RemoteSession *remote_;
    Platform platform_;
    QTimer passTimer_, checkTimer_;
    qint64 reconnectAfterMs_ = 3 * 60 * 1000;
    qint64 restartAfterMs_ = 10 * 60 * 1000;
    qint64 lastFresh_ = 0;     // when it last reconnected from scratch
    bool restarting_ = false;
    int reconnects_ = 0;
    qint64 logSent_ = 0;       // the last log line sent
};

} // namespace vt::net
