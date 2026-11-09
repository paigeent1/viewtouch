#include "serialscanner.hh"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSocketNotifier>
#include <QtGlobal>
#include <QDebug>

#include <algorithm>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

namespace {

#ifdef Q_OS_LINUX
speed_t speedFor(int baud)
{
    switch (baud) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    default: return B9600;
    }
}
#endif

// Every SerialScanner of this program (all on the GUI thread).
QList<SerialScanner *> &allScanners()
{
    static QList<SerialScanner *> list;
    return list;
}

// One name for a port, whichever of its names it is given by (a by-id link or /dev/ttyUSB0).
QString sameAs(const QString &device)
{
    const QString c = QFileInfo(device).canonicalFilePath();
    return c.isEmpty() ? device : c;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// One port
// ---------------------------------------------------------------------------------------------

SerialScanner::SerialScanner(const QString &device, int baud, QObject *parent)
    : QObject(parent), device_(device), baud_(baud > 0 ? baud : 9600)
{
    allScanners().append(this);
    retry_.setInterval(5000);
    connect(&retry_, &QTimer::timeout, this, &SerialScanner::tryOpen);
    idle_.setSingleShot(true);
    idle_.setInterval(120);
    connect(&idle_, &QTimer::timeout, this, &SerialScanner::flush);
}

SerialScanner::~SerialScanner()
{
    allScanners().removeAll(this);
    closePort();
}

void SerialScanner::start()
{
#ifdef Q_OS_LINUX
    tryOpen();
    retry_.start();
#endif
}

QVariantList SerialScanner::status()
{
    QVariantList out;
    for (const SerialScanner *s : allScanners())
        out.append(QVariantMap{{u"device"_s, s->device()}, {u"baud"_s, s->baud()}, {u"open"_s, s->isOpen()}});
    return out;
}

void SerialScanner::tryOpen()
{
#ifdef Q_OS_LINUX
    if (fd_ >= 0)
        return;
    if (!QFile::exists(device_))
        return;
    const int fd = ::open(device_.toLocal8Bit().constData(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        if (!warned_)
            qWarning().noquote() << "Serial scanner: can't open" << device_ << "(" << strerror(errno)
                                 << "): is this user in the dialout group?";
        warned_ = true;
        return;
    }
    termios t{};
    if (tcgetattr(fd, &t) == 0) {
        cfmakeraw(&t);
        cfsetispeed(&t, speedFor(baud_));
        cfsetospeed(&t, speedFor(baud_));
        t.c_cflag |= (CLOCAL | CREAD);
        t.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
        t.c_cflag |= CS8;
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(fd, TCSANOW, &t);
        tcflush(fd, TCIFLUSH);
    }
    fd_ = fd;
    warned_ = false;
    notifier_ = new QSocketNotifier(fd_, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &SerialScanner::readReady);
    qInfo().noquote() << "Serial scanner on" << device_ << "at" << baud_ << "baud";
#endif
}

void SerialScanner::readReady()
{
#ifdef Q_OS_LINUX
    char chunk[256];
    for (;;) {
        const ssize_t n = ::read(fd_, chunk, sizeof chunk);
        if (n > 0) {
            for (ssize_t i = 0; i < n; ++i) {
                const char ch = chunk[i];
                if ((ch == '\r' || ch == '\n' || ch == 0x1e) && buffer_.startsWith('@')) {
                    buffer_.append('\n');   // a license barcode: many lines, sent whole when the scanner stops
                } else if (ch == '\r' || ch == '\n') {
                    flush();
                } else if (static_cast<unsigned char>(ch) >= 0x20 && ch != 0x7f) {
                    buffer_.append(ch);
                }
                // Anything else (STX/ETX framing, other control bytes) is left out.
            }
            continue;
        }
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            // The adapter was unplugged: wait for it to come back.
            if (n < 0 || !QFile::exists(device_)) {
                qWarning().noquote() << "Serial scanner:" << device_ << "went away; waiting for it";
                closePort();
            }
        }
        break;
    }
    if (!buffer_.isEmpty())
        idle_.start();
#endif
}

void SerialScanner::flush()
{
    idle_.stop();
    const QString code = QString::fromLatin1(buffer_).trimmed();
    buffer_.clear();
    if (!code.isEmpty())
        emit scanned(code);
}

void SerialScanner::closePort()
{
#ifdef Q_OS_LINUX
    if (notifier_) {
        notifier_->setEnabled(false);
        notifier_->deleteLater();
        notifier_ = nullptr;
    }
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
    buffer_.clear();
#endif
}

// ---------------------------------------------------------------------------------------------
// All of this computer's scanner ports
// ---------------------------------------------------------------------------------------------

SerialScanners::Setting SerialScanners::parse(const QString &value, int defaultBaud)
{
    Setting s;
    s.baud = defaultBaud > 0 ? defaultBaud : 9600;
    static const QRegularExpression sep(u"[,\\s]+"_s);
    static const QRegularExpression withBaud(u"^(.*)@(\\d+)$"_s);
    struct Word { QString name; int baud = 0; };   // baud 0: none given
    QList<Word> words;
    for (const QString &w : value.split(sep, Qt::SkipEmptyParts)) {
        const auto m = withBaud.match(w);
        if (m.hasMatch() && !m.captured(1).isEmpty() && m.captured(2).toInt() > 0)
            words.append({m.captured(1), m.captured(2).toInt()});
        else
            words.append({w, 0});
    }
    // "all@19200": the speed of every port found, and of a listed one without its own.
    for (const Word &w : words) {
        const QString lower = w.name.toLower();
        if (lower == u"off"_s)
            s.off = true;
        if (lower == u"all"_s || lower == u"auto"_s) {
            s.all = true;
            if (w.baud > 0)
                s.baud = w.baud;
        }
    }
    if (s.off) {
        s.all = false;
        return s;
    }
    for (const Word &w : words) {
        const QString lower = w.name.toLower();
        if (lower == u"all"_s || lower == u"auto"_s)
            continue;
        const QString device = w.name.startsWith(u'/') ? w.name : u"/dev/"_s + w.name;
        if (std::none_of(s.ports.begin(), s.ports.end(), [&](const Port &p) { return p.device == device; }))
            s.ports.append({device, w.baud > 0 ? w.baud : s.baud});
    }
    if (s.ports.isEmpty())
        s.all = true;   // nothing set: every USB-serial port
    return s;
}

QStringList SerialScanners::usbSerialPorts()
{
    QStringList out;
    QSet<QString> seen;
    const QDir byId(u"/dev/serial/by-id"_s);
    for (const QString &name : byId.entryList(QDir::System | QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString path = byId.filePath(name);
        out << path;
        seen.insert(sameAs(path));
    }
    const QDir dev(u"/dev"_s);
    for (const QString &name : dev.entryList({u"ttyUSB*"_s, u"ttyACM*"_s}, QDir::System, QDir::Name)) {
        const QString path = dev.filePath(name);
        if (!seen.contains(sameAs(path)))
            out << path;
    }
    return out;
}

SerialScanners::SerialScanners(QObject *parent)
    : SerialScanners(qEnvironmentVariable("VT_SERIAL_SCANNER"),
                     qEnvironmentVariableIntValue("VT_SERIAL_SCANNER_BAUD"), parent)
{
}

SerialScanners::SerialScanners(const QString &value, int baud, QObject *parent)
    : QObject(parent), setting_(parse(value, baud))
{
    rescan_.setInterval(5000);
    connect(&rescan_, &QTimer::timeout, this, &SerialScanners::rescan);
}

SerialScanners::~SerialScanners()
{
    qDeleteAll(scanners_);
    scanners_.clear();
}

void SerialScanners::start()
{
#ifdef Q_OS_LINUX
    if (setting_.off || started_)
        return;
    started_ = true;
    for (const Port &p : setting_.ports)
        add(p.device, p.baud);   // listed: kept, and tried again by themselves when unplugged
    if (setting_.all) {
        rescan();
        if (scanners_.isEmpty())
            qInfo().noquote() << "Serial scanners: listening on every USB-serial port (none plugged in now)";
        rescan_.start();
    }
#endif
}

int SerialScanners::baudFor(const QString &device) const
{
    const QString key = sameAs(device);
    for (const Port &p : setting_.ports)
        if (p.device == device || sameAs(p.device) == key)
            return p.baud;
    return setting_.baud;
}

void SerialScanners::add(const QString &device, int baud)
{
    auto *s = new SerialScanner(device, baud, this);
    connect(s, &SerialScanner::scanned, this, &SerialScanners::scanned);
    scanners_.append(s);
    s->start();
}

void SerialScanners::rescan()
{
    if (!setting_.all)
        return;
    const QStringList found = finder_ ? finder_() : usbSerialPorts();
    QSet<QString> skip;
    if (skip_)
        for (const QString &p : skip_())
            if (!p.trimmed().isEmpty())
                skip.insert(sameAs(p.trimmed()));
    const auto listed = [this](const QString &device) {
        return std::any_of(setting_.ports.begin(), setting_.ports.end(), [&](const Port &p) { return p.device == device; });
    };
    // Gone (unplugged and not open) or now a printer's: let go. Listed ports stay.
    for (int i = scanners_.size() - 1; i >= 0; --i) {
        SerialScanner *s = scanners_[i];
        if (listed(s->device()))
            continue;
        const bool printer = skip.contains(sameAs(s->device()));
        if (printer || (!found.contains(s->device()) && !s->isOpen())) {
            if (printer)
                qInfo().noquote() << "Serial scanner:" << s->device() << "is a printer's port now; not read";
            scanners_.removeAt(i);
            s->disconnect(this);
            s->deleteLater();
        }
    }
    QSet<QString> have;
    for (const SerialScanner *s : scanners_)
        have << s->device() << sameAs(s->device());
    for (const QString &device : found) {
        const QString key = sameAs(device);
        if (have.contains(device) || have.contains(key) || skip.contains(key))
            continue;
        have << device << key;
        add(device, baudFor(device));
    }
}
