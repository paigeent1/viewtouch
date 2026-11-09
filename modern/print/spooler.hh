#pragma once

#include "core/settings.hh"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QThread>

namespace vt::print {

// Sends print jobs from a worker thread. submit() returns immediately; a slow
// or unplugged printer can never freeze the screen (the legacy vt_main ran
// lpr and socket writes on its event loop and froze under load).
//
// Delivery: network = raw TCP (port 9100 / JetDirect), file = append,
// cups = `lp -d <queue> -o raw`, none = discard. Failed jobs are retried
// with a growing delay, other printers' jobs keep flowing meanwhile, and
// jobFailed() is reported once a job is given up.
//
// A "file" printer that is a device (a USB printer, /dev/usb/lp0) or a pipe
// is written without blocking, opened for each ticket and closed after it,
// with DeviceWriteMs to take it. After a failure the printer is left alone
// for DeviceRetryMs (longer after repeated failures), and after
// DeviceAttempts tries the ticket is kept (held) for a Reprint and
// printerError() says the printer is in error. While it is, a new ticket is
// tried once (no sooner than the wait) and held if it fails too. A printer
// that tripped into an error state is never sent the same ticket again and
// again (2026-11-08: a TM-T88IV on USB was re-sent its ticket every time it
// was turned back on, and went straight back into error).
class PrintSpooler : public QObject {
    Q_OBJECT

public:
    static constexpr int MaxAttempts = 3;
    static constexpr int DeviceAttempts = 2;
    static constexpr int DeviceWriteMs = 60'000;   // an old printer can take a while over a logo
    static constexpr int DeviceRetryMs = 15'000;
    // Held tickets kept per printer (the oldest go first).
    static constexpr int MaxHeld = 50;

    explicit PrintSpooler(QObject *parent = nullptr);
    ~PrintSpooler() override;

    void submit(const core::PrinterConfig &printer, const QByteArray &data, const QString &description);
    int pending() const;
    // A ticket is waiting for, or on its way to, this network printer.
    bool busy(const QString &host, int port) const;
    // Tests: process events until the queue is empty or `msec` passes.
    bool waitIdle(int msec);

    // Shortened network timeouts / retry delays (and device ones), for tests.
    void setTimings(int connectMs, int retryBaseMs, int deviceWriteMs = DeviceWriteMs,
                    int deviceRetryMs = DeviceRetryMs);

    // Tickets kept for a device printer (by printer id) after it failed.
    int held(const QString &printerId) const;
    // Reprint: send them again now (in order), with a fresh DeviceAttempts.
    // Returns at once; the outcome comes as jobPrinted / jobFailed.
    void retryHeld(const QString &printerId);

    // A device path ("file" printer) that is not a plain file: a character
    // device or a pipe (or under /dev/ and not there).
    static bool isDevice(const QString &path);

signals:
    void jobPrinted(const QString &printer, const QString &description);
    void jobFailed(const QString &printer, const QString &description, const QString &error);
    // A device printer stopped taking tickets (error true) or is printing
    // again with nothing held (false).
    void printerError(const QString &printerId, const QString &printer, bool error);

private:
    class Worker;
    QThread thread_;
    Worker *worker_;
};

} // namespace vt::print
