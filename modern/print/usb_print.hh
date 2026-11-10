#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>
#include <string>

// A receipt printer plugged into a tablet's USB (through a powered USB-C hub). The store's
// computer sends the ticket to that tablet (Store Settings > Printers: "USB on a tablet", and
// which tablet), and the tablet writes it to the printer with Android's USB Host API
// (android/src/org/viewtouch/pos/UsbPrint.kt). No printer model is known here: any USB printer
// (interface class 7) works, or, when none is, the first device that takes data (a bulk OUT
// endpoint). The printer's "path" picks one when several are plugged in: empty or "usb" (the
// printer there), or "usb:<serial number or name>".
//
// What's here is plain C++ (tested on every system); usbPrint() itself only does anything on
// Android.
namespace vt::print {

// A USB device, as the tablet sees it (UsbPrint.kt devices()).
struct UsbDeviceInfo {
    QString name;           // Android's device name (/dev/bus/usb/001/002): stable while plugged in
    QString manufacturer;
    QString product;
    QString serial;         // empty until ViewTouch may use it (Android hides it before)
    int vendorId = 0;       // shown only, never matched on
    int productId = 0;
    bool printerClass = false;   // an interface of class 7 (printer)
    bool bulkOut = false;        // has a bulk OUT endpoint (it can be sent data)
    bool permitted = false;      // Android lets ViewTouch use it
};

// The printer's path for a "usb" printer -> what picks the device: "" (the printer plugged in),
// else a serial number or a name. "usb", "usb:" and "" are all "".
QString usbSelector(const QString &path);
inline QString usbSelector(const std::string &path) { return usbSelector(QString::fromStdString(path)); }

// The devices from UsbPrint.kt's JSON ([{name, manufacturer, product, serial, vendorId,
// productId, printer, bulkOut, permitted}...]); nothing for anything else.
QList<UsbDeviceInfo> parseUsbDevices(const QByteArray &json);

// Which device to print on (its index), or -1 with `error` saying why. With no selector: the
// printer-class device, or (none of them) the first that takes data; two printers and no
// selector is an error (which one?). With one: the device whose serial number is that, or
// whose name / product / "manufacturer product" is that (any case), or that contains it.
int pickUsbDevice(const QList<UsbDeviceInfo> &devices, const QString &selector, QString *error);

// Sends `data` a chunk at a time (`chunkBytes` each, the last shorter) through `transfer`,
// which sends what it's given and says how many bytes went (< 0: an error or the time ran
// out; Android's bulkTransfer). All of it must go within `totalMs`: each chunk is given what
// time is left. True only once every byte is sent (a printer that took part of a ticket
// prints that part; it is never sent again from here: no ticket is printed twice).
// A transfer that sent part of a chunk gets the rest of it next.
using UsbTransfer = std::function<int(const char *data, int length, int timeoutMs)>;
bool sendInChunks(const QByteArray &data, int chunkBytes, int totalMs, const UsbTransfer &transfer,
                  QString *error);

// Android: the bytes to the USB printer `selector` picks, blocking until every byte is sent
// (or `timeoutMs`), asking Android's permission first if it hasn't been given (the person at
// the tablet answers; that time counts). One ticket at a time per printer: a second while
// the first is still going is refused. Elsewhere: false ("not a tablet").
// Call it on a thread of its own, never the screen's.
bool usbPrint(const QString &selector, const QByteArray &data, int timeoutMs, QString *error);

// The chunk size used (16 KB: what every Android version's bulkTransfer takes at once).
inline constexpr int UsbChunkBytes = 16 * 1024;

} // namespace vt::print
