#include "print/usb_print.hh"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QThread>

#include <algorithm>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QSet>
#include <QtCore/qnativeinterface.h>

#include <mutex>
#endif

using namespace Qt::StringLiterals;

namespace vt::print {

QString usbSelector(const QString &path)
{
    const QString p = path.trimmed();
    if (p.compare(u"usb"_s, Qt::CaseInsensitive) == 0)
        return {};
    if (p.startsWith(u"usb:"_s, Qt::CaseInsensitive))
        return p.mid(4).trimmed();
    return p;
}

QList<UsbDeviceInfo> parseUsbDevices(const QByteArray &json)
{
    QList<UsbDeviceInfo> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        if (o.isEmpty())
            continue;
        UsbDeviceInfo d;
        d.name = o.value(u"name").toString();
        d.manufacturer = o.value(u"manufacturer").toString().trimmed();
        d.product = o.value(u"product").toString().trimmed();
        d.serial = o.value(u"serial").toString().trimmed();
        d.vendorId = o.value(u"vendorId").toInt();
        d.productId = o.value(u"productId").toInt();
        d.printerClass = o.value(u"printer").toBool();
        d.bulkOut = o.value(u"bulkOut").toBool();
        d.permitted = o.value(u"permitted").toBool();
        if (!d.name.isEmpty())
            out << d;
    }
    return out;
}

namespace {

QString label(const UsbDeviceInfo &d)
{
    QString s = (d.manufacturer + u' ' + d.product).trimmed();
    if (s.isEmpty())
        s = d.name;
    if (!d.serial.isEmpty())
        s += u" (serial %1)"_s.arg(d.serial);
    return s;
}

QString labels(const QList<UsbDeviceInfo> &devices, const QList<int> &which)
{
    QStringList l;
    for (int i : which)
        l << label(devices[i]);
    return l.join(u", "_s);
}

} // namespace

int pickUsbDevice(const QList<UsbDeviceInfo> &devices, const QString &selector, QString *error)
{
    // Only what can be sent data at all (a keyboard, a scanner: never).
    QList<int> usable;
    for (int i = 0; i < devices.size(); ++i)
        if (devices[i].printerClass || devices[i].bulkOut)
            usable << i;
    if (usable.isEmpty()) {
        *error = u"no USB printer is plugged into this tablet (is the hub powered?)"_s;
        return -1;
    }
    const QString want = selector.trimmed();
    const auto preferPrinter = [&](const QList<int> &found) {
        for (int i : found)
            if (devices[i].printerClass)
                return i;
        return found.first();
    };
    if (want.isEmpty()) {
        QList<int> printers;
        for (int i : usable)
            if (devices[i].printerClass)
                printers << i;
        if (printers.size() == 1)
            return printers.first();
        if (printers.size() > 1) {
            *error = u"%1 USB printers are plugged into this tablet: say which one in Store Settings > Printers "
                     "(usb:<serial number or name>): %2"_s.arg(printers.size()).arg(labels(devices, printers));
            return -1;
        }
        return usable.first();   // no printer-class device: the first that takes data
    }
    const auto same = [&](const QString &a) { return !a.isEmpty() && a.compare(want, Qt::CaseInsensitive) == 0; };
    QList<int> exact, partial;
    for (int i : usable) {
        const UsbDeviceInfo &d = devices[i];
        const QString full = (d.manufacturer + u' ' + d.product).trimmed();
        if (same(d.serial) || same(d.name) || same(d.product) || same(full))
            exact << i;
        else if (full.contains(want, Qt::CaseInsensitive))
            partial << i;
    }
    if (!exact.isEmpty())
        return preferPrinter(exact);
    if (!partial.isEmpty())
        return preferPrinter(partial);
    const bool hidden = std::ranges::any_of(usable, [&](int i) { return !devices[i].permitted && devices[i].serial.isEmpty(); });
    *error = u"no USB printer \"%1\" is plugged into this tablet (there: %2)%3"_s
                 .arg(want, labels(devices, usable),
                      hidden ? u"; a serial number shows only once ViewTouch may use the printer"_s : QString());
    return -1;
}

bool sendInChunks(const QByteArray &data, int chunkBytes, int totalMs, const UsbTransfer &transfer, QString *error)
{
    chunkBytes = std::max(1, chunkBytes);
    QElapsedTimer clock;
    clock.start();
    qsizetype sent = 0;
    const auto timedOut = [&] {
        *error = u"the printer did not take the ticket in %1 s (%2 of %3 bytes sent; error at the printer?)"_s
                     .arg((totalMs + 999) / 1000).arg(sent).arg(data.size());
        return false;
    };
    while (sent < data.size()) {
        const qint64 left = qint64(totalMs) - clock.elapsed();
        if (left <= 0)
            return timedOut();
        const int length = int(std::min<qsizetype>(chunkBytes, data.size() - sent));
        const int n = transfer(data.constData() + sent, length, int(std::max<qint64>(1, left)));
        if (n < 0) {
            if (clock.elapsed() >= totalMs)
                return timedOut();
            *error = u"the printer stopped taking the ticket (%1 of %2 bytes sent; unplugged?)"_s
                         .arg(sent).arg(data.size());
            return false;
        }
        if (n == 0) {   // took nothing this time: again, a moment later, while there's time
            QThread::msleep(20);
            continue;
        }
        sent += std::min(n, length);
    }
    return true;
}

bool usbPrint(const QString &selector, const QByteArray &data, int timeoutMs, QString *error)
{
#ifndef Q_OS_ANDROID
    Q_UNUSED(selector)
    Q_UNUSED(data)
    Q_UNUSED(timeoutMs)
    *error = u"USB printing is for tablets (this screen isn't one)"_s;
    return false;
#else
    static constexpr const char *kClass = "org/viewtouch/pos/UsbPrint";
    QElapsedTimer clock;
    clock.start();
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniEnvironment env;
    const QString json = QJniObject::callStaticObjectMethod(kClass, "devices", "(Landroid/content/Context;)Ljava/lang/String;",
                                                            context.object()).toString();
    env.checkAndClearExceptions();
    const QList<UsbDeviceInfo> devices = parseUsbDevices(json.toUtf8());
    const int index = pickUsbDevice(devices, selector, error);
    if (index < 0)
        return false;
    const QString name = devices[index].name;

    // A ticket given up on earlier may still be going out: never a second one beside it.
    static std::mutex busyMutex;
    static QSet<QString> busy;
    {
        std::lock_guard lock(busyMutex);
        if (busy.contains(name)) {
            *error = u"the printer is still taking the last ticket (%1)"_s.arg(label(devices[index]));
            return false;
        }
        busy.insert(name);
    }
    struct Release {
        QString name;
        ~Release()
        {
            QJniObject::callStaticMethod<void>(kClass, "close", "(Ljava/lang/String;)V",
                                               QJniObject::fromString(name).object<jstring>());
            QJniEnvironment().checkAndClearExceptions();
            std::lock_guard lock(busyMutex);
            busy.remove(name);
        }
    };
    const QJniObject jname = QJniObject::fromString(name);
    // Permission (asked on the screen the first time: the time to answer counts), the printer
    // interface claimed.
    const QString openError = QJniObject::callStaticObjectMethod(
                                  kClass, "open", "(Landroid/content/Context;Ljava/lang/String;I)Ljava/lang/String;",
                                  context.object(), jname.object<jstring>(),
                                  jint(std::max<qint64>(1000, timeoutMs - clock.elapsed())))
                                  .toString();
    if (env.checkAndClearExceptions()) {
        std::lock_guard lock(busyMutex);
        busy.remove(name);
        *error = u"the tablet could not open the USB printer"_s;
        return false;
    }
    Release release{name};
    if (!openError.isEmpty()) {
        *error = openError;
        return false;
    }
    jbyteArray bytes = env->NewByteArray(jsize(data.size()));
    if (!bytes) {
        env.checkAndClearExceptions();
        *error = u"the tablet is out of memory for the ticket"_s;
        return false;
    }
    env->SetByteArrayRegion(bytes, 0, jsize(data.size()), reinterpret_cast<const jbyte *>(data.constData()));
    const char *start = data.constData();
    const bool ok = sendInChunks(data, UsbChunkBytes, int(std::max<qint64>(1, timeoutMs - clock.elapsed())),
                                 [&](const char *p, int length, int ms) {
        const jint n = QJniObject::callStaticMethod<jint>(kClass, "transfer", "(Ljava/lang/String;[BIII)I",
                                                          jname.object<jstring>(), bytes, jint(p - start),
                                                          jint(length), jint(ms));
        return env.checkAndClearExceptions() ? -1 : int(n);
    }, error);
    env->DeleteLocalRef(bytes);
    return ok;
#endif
}

} // namespace vt::print
