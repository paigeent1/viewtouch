#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <functional>

class QSocketNotifier;

// Barcode scanners on serial ports (RS-232 scanners through a USB-serial adapter: /dev/ttyUSB0,
// /dev/serial/by-id/...). Each code a scanner sends (ended by CR or LF, or followed by a short
// pause) is given to scanned(), the same way a keyboard-type scanner's code is, whichever port it
// came on: what a code is (an item, a license) is decided by its content, not by the port.
// Linux only (the main computer); nothing on Android.
//
// Settings (environment, read once at start; vt-server / vt-start pass site.conf's SERIAL_SCANNER
// and SERIAL_SCANNER_BAUD as these):
//   VT_SERIAL_SCANNER       not set, empty or "all": listen on every USB-serial port this computer
//                           has (/dev/serial/by-id/*, else /dev/ttyUSB*, /dev/ttyACM*), looked for
//                           again every few seconds, so a scanner plugged into any port just works.
//                           A port a "file" printer prints to (Store Settings > Printers) is left
//                           alone, so a serial printer or pole display is never read as a scanner.
//                           "off": none.
//                           Otherwise a list of ports, separated by commas or spaces, each with its
//                           own speed if wanted: "/dev/ttyUSB0, /dev/ttyUSB1@19200" (a name without
//                           a / is under /dev: "ttyUSB1"). Only those ports are opened. "all" in a
//                           list: every port, the ones listed at their own speed.
//   VT_SERIAL_SCANNER_BAUD  the speed of a port without @speed (default 9600; 8 data bits, no
//                           parity, 1 stop bit)

// One scanner port. It can be unplugged and plugged back in: the port is tried again every few
// seconds.
class SerialScanner : public QObject {
    Q_OBJECT
public:
    SerialScanner(const QString &device, int baud, QObject *parent = nullptr);
    ~SerialScanner() override;
    void start();
    QString device() const { return device_; }
    int baud() const { return baud_; }
    bool isOpen() const { return fd_ >= 0; }

    // Every scanner port of this program now: [{device, baud, open}] (Site Setup shows them).
    static QVariantList status();

signals:
    void scanned(const QString &code);

private:
    void tryOpen();
    void readReady();
    void flush();
    void closePort();

    QString device_;
    int baud_ = 9600;
    int fd_ = -1;
    QSocketNotifier *notifier_ = nullptr;
    QByteArray buffer_;
    QTimer retry_;
    QTimer idle_;          // a scanner that sends no CR/LF: the code ends after a short pause
    bool warned_ = false;
};

// All the scanner ports of this computer, as VT_SERIAL_SCANNER says (above).
class SerialScanners : public QObject {
    Q_OBJECT
public:
    struct Port {
        QString device;
        int baud = 9600;
        bool operator==(const Port &) const = default;
    };
    struct Setting {
        bool off = false;
        bool all = false;      // every USB-serial port found (the default)
        int baud = 9600;       // for the ports found
        QList<Port> ports;     // listed (with "all": only their speeds count)
    };
    // VT_SERIAL_SCANNER's value read; empty = all.
    static Setting parse(const QString &value, int defaultBaud = 9600);
    // The USB-serial ports this computer has now, by their stable names where there are some.
    static QStringList usbSerialPorts();

    // From VT_SERIAL_SCANNER / VT_SERIAL_SCANNER_BAUD.
    explicit SerialScanners(QObject *parent = nullptr);
    SerialScanners(const QString &value, int baud, QObject *parent = nullptr);
    ~SerialScanners() override;

    // The ports not to read (a printer's), asked again at every look for ports.
    void setSkip(std::function<QStringList()> skip) { skip_ = std::move(skip); }
    // Where to look for ports (usbSerialPorts(); the tests give their own).
    void setFinder(std::function<QStringList()> finder) { finder_ = std::move(finder); }
    void start();
    // Look for ports now (every few seconds by itself when listening on all of them).
    void rescan();
    QList<SerialScanner *> scanners() const { return scanners_; }
    const Setting &setting() const { return setting_; }

signals:
    void scanned(const QString &code);

private:
    void add(const QString &device, int baud);
    int baudFor(const QString &device) const;

    Setting setting_;
    std::function<QStringList()> skip_;
    std::function<QStringList()> finder_;
    QList<SerialScanner *> scanners_;
    QTimer rescan_;
    bool started_ = false;
};
