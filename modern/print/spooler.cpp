#include "print/spooler.hh"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#if QT_CONFIG(process)
#include <QProcess>
#endif
#include <QTcpSocket>
#include <QTimer>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <deque>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

namespace vt::print {

class PrintSpooler::Worker : public QObject {
public:
    struct Job {
        core::PrinterConfig printer;
        QByteArray data;
        QString description;
        int attempts = 0;
        qint64 notBefore = 0;
    };

    // A device printer's state (by printer id).
    struct Device {
        int failures = 0;        // in a row
        qint64 notBefore = 0;    // left alone until then
        bool error = false;      // given up on a ticket; cleared when it prints with nothing held
    };

    explicit Worker(PrintSpooler *owner) : owner_(owner) {}

    // Owner thread.
    void enqueue(Job job)
    {
        pending_.fetch_add(1);
        hold(job, +1);
        QMetaObject::invokeMethod(this, [this, job = std::move(job)]() mutable {
            queue_.push_back(std::move(job));
            schedule(0);
        }, Qt::QueuedConnection);
    }

    int pending() const { return pending_.load(); }

    bool busy(const QString &host, int port) const
    {
        QMutexLocker lock(&busyMutex_);
        return busy_.value(host + u':' + QString::number(port)) > 0;
    }
    int connectMs = 3000;
    int retryBaseMs = 2000;
    int deviceWriteMs = PrintSpooler::DeviceWriteMs;
    int deviceRetryMs = PrintSpooler::DeviceRetryMs;

    int held(const QString &printerId) const
    {
        QMutexLocker lock(&busyMutex_);
        return heldCount_.value(printerId);
    }

    // Owner thread: Reprint.
    void retryHeld(const QString &printerId)
    {
        // Counted as pending now, so waitIdle() waits for them.
        const int expected = held(printerId);
        pending_.fetch_add(expected);
        QMetaObject::invokeMethod(this, [this, printerId, expected] {
            std::deque<Job> &list = held_[printerId];
            Device &d = devices_[printerId];
            d.failures = 0;
            d.notBefore = 0;
            // A fresh DeviceAttempts for each; if they fail again, printerError(true) comes again.
            if (d.error) {
                d.error = false;
                const QString name = list.empty() ? printerId : QString::fromStdString(list.front().printer.name);
                emit owner_->printerError(printerId, name, false);
            }
            const bool none = list.empty();
            pending_.fetch_add(int(list.size()) - expected);
            for (Job &job : list) {
                job.attempts = 0;
                job.notBefore = 0;
                queue_.push_back(std::move(job));
            }
            list.clear();
            countHeld(printerId);
            if (!none)
                schedule(0);
        }, Qt::QueuedConnection);
    }

    // Worker thread (before it ends).
    void stop()
    {
        delete timer_;
        timer_ = nullptr;
    }

private:
    // Sooner wins: a new ticket isn't kept waiting behind a printer left
    // alone for a while (a USB printer in error waits up to two minutes).
    void schedule(int delayMs)
    {
        if (!timer_) {
            timer_ = new QTimer(this);
            timer_->setSingleShot(true);
            QObject::connect(timer_, &QTimer::timeout, this, [this] { run(); });
        }
        if (timer_->isActive() && timer_->remainingTime() <= delayMs)
            return;
        timer_->start(delayMs);
    }

    static bool onDevice(const Job &job)
    {
        return job.printer.type == "file" && PrintSpooler::isDevice(QString::fromStdString(job.printer.path));
    }

    void run()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        std::deque<Job> later;
        while (!queue_.empty()) {
            Job job = std::move(queue_.front());
            queue_.pop_front();
            const bool device = onDevice(job);
            const QString id = QString::fromStdString(job.printer.id);
            if (job.notBefore > now || (device && devices_[id].notBefore > now)) {
                if (device)
                    job.notBefore = std::max(job.notBefore, devices_[id].notBefore);
                later.push_back(std::move(job));
                continue;
            }
            QString error;
            if (deliver(job, &error)) {
                pending_.fetch_sub(1);
                hold(job, -1);
                emit owner_->jobPrinted(QString::fromStdString(job.printer.name), job.description);
                if (device) {
                    Device &d = devices_[id];
                    d.failures = 0;
                    d.notBefore = 0;
                    if (d.error && held_[id].empty()) {
                        d.error = false;
                        emit owner_->printerError(id, QString::fromStdString(job.printer.name), false);
                    }
                }
                continue;
            }
            if (device) {
                // Leave the printer alone a while (longer each time: 1, 2, 4, 8 x the wait).
                Device &d = devices_[id];
                ++d.failures;
                d.notBefore = QDateTime::currentMSecsSinceEpoch()
                              + qint64(deviceRetryMs) * (1 << std::min(d.failures - 1, 3));
                ++job.attempts;
                if (d.error || job.attempts >= PrintSpooler::DeviceAttempts) {
                    // Kept for a Reprint; never sent again on its own.
                    pending_.fetch_sub(1);
                    const QString name = QString::fromStdString(job.printer.name);
                    const QString what = job.description;
                    std::deque<Job> &list = held_[id];
                    list.push_back(std::move(job));
                    while (int(list.size()) > PrintSpooler::MaxHeld)
                        list.pop_front();
                    countHeld(id);
                    emit owner_->jobFailed(name, what, error);
                    if (!d.error) {
                        d.error = true;
                        emit owner_->printerError(id, name, true);
                    }
                    continue;
                }
                job.notBefore = d.notBefore;
                later.push_back(std::move(job));
                continue;
            }
            if (++job.attempts >= MaxAttempts) {
                pending_.fetch_sub(1);
                hold(job, -1);
                emit owner_->jobFailed(QString::fromStdString(job.printer.name), job.description, error);
                continue;
            }
            job.notBefore = QDateTime::currentMSecsSinceEpoch() + qint64(retryBaseMs) * job.attempts;
            later.push_back(std::move(job));
        }
        queue_ = std::move(later);
        if (!queue_.empty()) {
            qint64 next = queue_.front().notBefore;
            for (const Job &j : queue_)
                next = std::min(next, j.notBefore);
            schedule(int(std::max<qint64>(10, next - QDateTime::currentMSecsSinceEpoch())));
        }
    }

    void countHeld(const QString &id)
    {
        QMutexLocker lock(&busyMutex_);
        heldCount_[id] = int(held_[id].size());
    }

    bool deliver(const Job &job, QString *error)
    {
        const core::PrinterConfig &p = job.printer;
        if (p.type == "none")
            return true;
        if (p.type == "file") {
            const QString path = QString::fromStdString(p.path);
            return PrintSpooler::isDevice(path) ? toDevice(path, job.data, deviceWriteMs, error)
                                                : toFile(path, job.data, error);
        }
        if (p.type == "network")
            return toSocket(QString::fromStdString(p.host), quint16(p.port), job.data, error);
        if (p.type == "cups")
            return toCups(QString::fromStdString(p.path), job.data, error);
        *error = u"unknown printer type '%1'"_s.arg(QString::fromStdString(p.type));
        return false;
    }

    static bool toFile(const QString &path, const QByteArray &data, QString *error)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::Append)) {
            *error = f.errorString();
            return false;
        }
        if (f.write(data) != data.size()) {
            *error = f.errorString();
            return false;
        }
        return true;
    }

    // A USB printer (/dev/usb/lp0) or a pipe: never blocks, never stays open.
    // A printer in an error state stops taking data; after `timeoutMs` the
    // ticket is given up and the device closed, so it's free again.
    static bool toDevice(const QString &path, const QByteArray &data, int timeoutMs, QString *error)
    {
#ifndef Q_OS_UNIX
        Q_UNUSED(timeoutMs)
        return toFile(path, data, error);
#else
        const QByteArray name = QFile::encodeName(path);
        int fd;
        do
            fd = ::open(name.constData(), O_WRONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
        while (fd < 0 && errno == EINTR);
        if (fd < 0) {
            const int e = errno;
            const QString why = QString::fromLocal8Bit(std::strerror(e));
            *error = e == ENOENT                 ? u"the printer is not connected (%1 is missing)"_s.arg(path)
                     : e == ENXIO || e == ENODEV ? u"the printer is not ready (%1: %2)"_s.arg(path, why)
                     : e == EBUSY                ? u"the printer is in use (%1)"_s.arg(path)
                                                 : u"%1: %2"_s.arg(path, why);
            return false;
        }
        QElapsedTimer t;
        t.start();
        qint64 off = 0;
        bool ok = true;
        while (off < data.size()) {
            const ssize_t n = ::write(fd, data.constData() + off, size_t(data.size() - off));
            if (n > 0) {
                off += n;
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                *error = u"the printer stopped taking the ticket (%1)"_s.arg(QString::fromLocal8Bit(std::strerror(errno)));
                ok = false;
                break;
            }
            const qint64 left = timeoutMs - t.elapsed();
            if (left <= 0) {
                *error = u"the printer did not take the ticket in %1 s (error at the printer?)"_s.arg((timeoutMs + 999) / 1000);
                ok = false;
                break;
            }
            pollfd pfd{fd, POLLOUT, 0};
            const int r = ::poll(&pfd, 1, int(std::min<qint64>(left, 500)));
            if (r < 0 && errno != EINTR) {
                *error = QString::fromLocal8Bit(std::strerror(errno));
                ok = false;
                break;
            }
            if (r > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) && !(pfd.revents & POLLOUT)) {
                *error = u"the printer went away while printing"_s;
                ok = false;
                break;
            }
        }
        ::close(fd);
        return ok;
#endif
    }

    bool toSocket(const QString &host, quint16 port, const QByteArray &data, QString *error)
    {
        if (host.isEmpty()) {
            *error = u"no printer address"_s;
            return false;
        }
        QTcpSocket s;
        s.connectToHost(host, port);
        if (!s.waitForConnected(connectMs)) {
            *error = s.errorString();
            return false;
        }
        s.write(data);
        while (s.bytesToWrite() > 0) {
            if (!s.waitForBytesWritten(connectMs)) {
                *error = s.errorString();
                return false;
            }
        }
        s.disconnectFromHost();
        if (s.state() != QAbstractSocket::UnconnectedState)
            s.waitForDisconnected(connectMs);
        return true;
    }

    bool toCups(const QString &queue, const QByteArray &data, QString *error)
    {
#if !QT_CONFIG(process)
        Q_UNUSED(queue)
        Q_UNUSED(data)
        *error = u"CUPS printing is not available on this system"_s;
        return false;
#else
        QProcess lp;
        QStringList args{u"-o"_s, u"raw"_s};
        if (!queue.isEmpty())
            args << u"-d"_s << queue;
        lp.start(u"lp"_s, args);
        if (!lp.waitForStarted(connectMs)) {
            *error = u"cannot run lp: %1"_s.arg(lp.errorString());
            return false;
        }
        lp.write(data);
        lp.closeWriteChannel();
        if (!lp.waitForFinished(connectMs * 3)) {
            lp.kill();
            *error = u"lp did not finish"_s;
            return false;
        }
        if (lp.exitCode() != 0) {
            *error = QString::fromLocal8Bit(lp.readAllStandardError()).trimmed();
            return false;
        }
        return true;
#endif
    }

    // Network printers with tickets waiting (the status check stays away).
    void hold(const Job &job, int delta)
    {
        if (job.printer.type != "network")
            return;
        QMutexLocker lock(&busyMutex_);
        busy_[QString::fromStdString(job.printer.host) + u':' + QString::number(job.printer.port)] += delta;
    }

    mutable QMutex busyMutex_;
    QHash<QString, int> busy_;
    QHash<QString, int> heldCount_;          // under busyMutex_
    QHash<QString, std::deque<Job>> held_;   // worker thread
    QHash<QString, Device> devices_;         // worker thread
    PrintSpooler *owner_;
    std::deque<Job> queue_;
    std::atomic<int> pending_{0};
    QTimer *timer_ = nullptr;
};

PrintSpooler::PrintSpooler(QObject *parent)
    : QObject(parent)
    , worker_(new Worker(this))
{
    thread_.setObjectName(u"vt-printer"_s);
    worker_->moveToThread(&thread_);
    thread_.start();
}

PrintSpooler::~PrintSpooler()
{
    QMetaObject::invokeMethod(worker_, [w = worker_] { w->stop(); }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
    delete worker_;
}

void PrintSpooler::submit(const core::PrinterConfig &printer, const QByteArray &data, const QString &description)
{
    worker_->enqueue({printer, data, description, 0, 0});
}

int PrintSpooler::pending() const
{
    return worker_->pending();
}

bool PrintSpooler::busy(const QString &host, int port) const
{
    return worker_->busy(host, port);
}

bool PrintSpooler::waitIdle(int msec)
{
    QElapsedTimer t;
    t.start();
    while (worker_->pending() > 0 && t.elapsed() < msec) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();   // deliver queued signals
    return worker_->pending() == 0;
}

void PrintSpooler::setTimings(int connectMs, int retryBaseMs, int deviceWriteMs, int deviceRetryMs)
{
    QMetaObject::invokeMethod(worker_, [w = worker_, connectMs, retryBaseMs, deviceWriteMs, deviceRetryMs] {
        w->connectMs = connectMs;
        w->retryBaseMs = retryBaseMs;
        w->deviceWriteMs = deviceWriteMs;
        w->deviceRetryMs = deviceRetryMs;
    }, Qt::BlockingQueuedConnection);
}

int PrintSpooler::held(const QString &printerId) const
{
    return worker_->held(printerId);
}

void PrintSpooler::retryHeld(const QString &printerId)
{
    worker_->retryHeld(printerId);
}

bool PrintSpooler::isDevice(const QString &path)
{
#ifdef Q_OS_UNIX
    if (path.isEmpty())
        return false;
    struct stat st;
    if (::stat(QFile::encodeName(path).constData(), &st) != 0)
        return path.startsWith(u"/dev/"_s);   // unplugged: still a device
    return S_ISCHR(st.st_mode) || S_ISFIFO(st.st_mode);
#else
    return path.startsWith(u"/dev/"_s);
#endif
}

} // namespace vt::print
