#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace vt::app {

class PosService;

// The ILSA price book, kept in step by itself (owner 2026-11-07): on the store's Pi (the computer that
// serves the terminals), every 15 minutes, `vt-ilsa-pricebook` pulls what the ILSA computer sent to
// Dropbox; when it is a new export, `vt-ilsa-items` converts it and PosService::syncPriceBook applies it.
// Off the UI thread: the tools run as processes, the 30 MB file is read on a worker thread.
// What it did goes to ~/ilsa-sync: sync-log.csv (each change, and each difference once), runs.log (a line
// per run), price-changes-<time>.csv (each run's price changes, for shelf labels).
class IlsaSync : public QObject {
    Q_OBJECT

public:
    explicit IlsaSync(PosService *pos, QObject *parent = nullptr);

    // The first run a minute from now, then every 15 minutes (while on).
    void start();
    // Now, on or off (Site Setup > Sync now). False: one is running already.
    bool runNow();
    bool on() const;
    void setOn(bool on);
    bool busy() const { return busy_; }
    // {on, busy, step, lastRun (ms), result}: Site Setup's box.
    QVariantMap info() const;
    // The last runs and the last changes (Show log).
    QStringList logTail(int lines = 50) const;

    // ~/ilsa-sync by default (tests: a folder of their own).
    void setLogDir(const QString &dir) { logDir_ = dir; }
    QString logDir() const { return logDir_; }
    // One run's result (PosService::syncPriceBook) into the logs; differences already noted on an
    // earlier run are not written again. Returns the run's line ("3 added, 12 prices changed...").
    QString writeLogs(const QVariantMap &result, const QDateTime &at);

signals:
    void changed();

private:
    void run(bool byHand);
    void apply(const QVariantMap &file, const QString &exported, bool byHand);
    void finish(const QString &result, bool logIt);

    PosService *pos_;
    QTimer timer_;
    QString logDir_;
    bool busy_ = false;
    QString step_;
    QDateTime lastRun_;
    QString result_;
};

} // namespace vt::app
