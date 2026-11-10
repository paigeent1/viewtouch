#pragma once

#include "core/settings.hh"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QThread>

#include <functional>

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
//
// A "usb" printer is plugged into a tablet (print/usb_print.hh): its tickets are
// handed to the tablet sender (setTabletSender: the store's server sends them to
// that tablet) and wait, one at a time, until the tablet says every byte went
// out, or DeviceWriteMs. It is a device printer like the one above: held
// tickets, printerError(), Reprint.
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

    // "usb" printers (on a tablet): `send` is called on the printer thread with the
    // ticket and the time it has (DeviceWriteMs), and must call `done` once, from any
    // thread. The ticket has printed only when done(true) comes; nothing within the
    // time (+ TabletGraceMs): it failed. Without a sender, "usb" tickets fail.
    using TabletDone = std::function<void(bool ok, const QString &error)>;
    using TabletSender = std::function<void(const core::PrinterConfig &printer, const QByteArray &data,
                                            int timeoutMs, TabletDone done)>;
    static constexpr int TabletGraceMs = 5'000;
    void setTabletSender(TabletSender send);

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
