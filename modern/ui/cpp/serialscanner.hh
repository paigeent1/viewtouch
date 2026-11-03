#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

class QSocketNotifier;

// A barcode scanner on a serial port (an RS-232 scanner through a USB-serial adapter: /dev/ttyUSB0).
// Each code it sends (ended by CR or LF, or followed by a short pause) is given to scanned(), the
// same way a keyboard-type scanner's code is. The adapter can be unplugged and plugged back in: the
// port is tried again every few seconds. Linux only (the main computer); nothing on Android.
//
// Settings (environment, read once at start):
//   VT_SERIAL_SCANNER       the device (default /dev/ttyUSB0; "off" turns it off)
//   VT_SERIAL_SCANNER_BAUD  speed (default 9600; 8 data bits, no parity, 1 stop bit)
class SerialScanner : public QObject {
    Q_OBJECT
public:
    explicit SerialScanner(QObject *parent = nullptr);
    ~SerialScanner() override;
    void start();
    QString device() const { return device_; }
    bool isOpen() const { return fd_ >= 0; }

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
