#include "serialscanner.hh"

#include <QFile>
#include <QSocketNotifier>
#include <QtGlobal>
#include <QDebug>

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

} // namespace

SerialScanner::SerialScanner(QObject *parent)
    : QObject(parent)
{
    const QString dev = qEnvironmentVariable("VT_SERIAL_SCANNER").trimmed();
    device_ = dev.isEmpty() ? u"/dev/ttyUSB0"_s : dev;
    bool ok = false;
    const int b = qEnvironmentVariableIntValue("VT_SERIAL_SCANNER_BAUD", &ok);
    if (ok && b > 0)
        baud_ = b;
    retry_.setInterval(5000);
    connect(&retry_, &QTimer::timeout, this, &SerialScanner::tryOpen);
    idle_.setSingleShot(true);
    idle_.setInterval(120);
    connect(&idle_, &QTimer::timeout, this, &SerialScanner::flush);
}

SerialScanner::~SerialScanner()
{
    closePort();
}

void SerialScanner::start()
{
#ifdef Q_OS_LINUX
    if (device_.compare(u"off"_s, Qt::CaseInsensitive) == 0)
        return;
    tryOpen();
    retry_.start();
#endif
}

void SerialScanner::tryOpen()
{
#ifdef Q_OS_LINUX
    if (fd_ >= 0 || !QFile::exists(device_))
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
                if (ch == '\r' || ch == '\n') {
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
