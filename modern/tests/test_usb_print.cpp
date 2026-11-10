// A receipt printer plugged into a tablet's USB (print/usb_print.hh): which device a printer
// setting picks, the ticket sent a chunk at a time, the store's spooler waiting for the tablet,
// and the ticket's trip to the tablet and back. The Android USB calls themselves (UsbPrint.kt)
// need a tablet; everything around them is here.
#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "layout_fixture.hh"
#include "net/layout_hub.hh"
#include "net/pos_server.hh"
#include "net/remote_session.hh"
#include "pos_fixture.hh"
#include "print/spooler.hh"
#include "print/ticket_printer.hh"
#include "print/usb_print.hh"
#include "qt_catch.hh"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

using namespace Qt::StringLiterals;
using namespace vt;
using namespace vt::print;

namespace {

template <typename F>
bool waitUntil(F condition, int msec = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!condition() && t.elapsed() < msec) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

UsbDeviceInfo device(const char *name, const char *maker, const char *product, bool printer, bool bulkOut,
                     const char *serial = "")
{
    UsbDeviceInfo d;
    d.name = QString::fromLatin1(name);
    d.manufacturer = QString::fromLatin1(maker);
    d.product = QString::fromLatin1(product);
    d.printerClass = printer;
    d.bulkOut = bulkOut;
    d.serial = QString::fromLatin1(serial);
    d.permitted = !d.serial.isEmpty();
    return d;
}

core::PrinterConfig usbPrinter(const char *terminal = "Bar Tablet")
{
    core::PrinterConfig p;
    p.id = "tablet-bar";
    p.name = "Bar receipt";
    p.type = "usb";
    p.terminal = terminal;
    p.drawerKick = true;
    return p;
}

} // namespace

TEST_CASE("USB on a tablet: the printer setting says which printer", "[print][usb]")
{
    CHECK(usbSelector(QString()).isEmpty());
    CHECK(usbSelector(u"usb"_s).isEmpty());
    CHECK(usbSelector(u" USB "_s).isEmpty());
    CHECK(usbSelector(u"usb:"_s).isEmpty());
    CHECK(usbSelector(u"usb:J8KF012345"_s) == u"J8KF012345"_s);
    CHECK(usbSelector(u"USB: TM-T20II "_s) == u"TM-T20II"_s);
    CHECK(usbSelector(std::string("Receipt Printer")) == u"Receipt Printer"_s);

    // Saved with the store's settings, and back.
    core::PrinterConfig p = usbPrinter();
    p.path = "usb:J8KF012345";
    const core::PrinterConfig back = app::printerFromJson(app::toJson(p));
    CHECK(back.type == "usb");
    CHECK(back.terminal == "Bar Tablet");
    CHECK(back.path == "usb:J8KF012345");
    CHECK(back == p);
    // A receipt printer: ESC/POS unless told otherwise.
    CHECK(back.effectiveFormat() == "escpos");
    core::PrinterConfig file;
    file.type = "file";
    file.path = "/dev/usb/lp0";
    CHECK(file.effectiveFormat() == "text");   // the Pi's USB printer path is as it was
    CHECK(app::printerFromJson(app::toJson(file)).terminal.empty());
}

TEST_CASE("USB on a tablet: the devices the tablet lists", "[print][usb]")
{
    const QByteArray json = R"([
        {"name": "/dev/bus/usb/001/002", "manufacturer": "EPSON", "product": "TM-T20II", "serial": "J8KF0",
         "vendorId": 1208, "productId": 3605, "printer": true, "bulkOut": true, "permitted": true},
        {"name": "/dev/bus/usb/001/003", "manufacturer": "", "product": "USB Keyboard", "printer": false,
         "bulkOut": false, "permitted": false},
        {"product": "no name: left out"},
        42
    ])";
    const QList<UsbDeviceInfo> d = parseUsbDevices(json);
    REQUIRE(d.size() == 2);
    CHECK(d[0].name == u"/dev/bus/usb/001/002"_s);
    CHECK(d[0].product == u"TM-T20II"_s);
    CHECK(d[0].serial == u"J8KF0"_s);
    CHECK(d[0].vendorId == 1208);
    CHECK(d[0].printerClass);
    CHECK(d[0].bulkOut);
    CHECK(d[0].permitted);
    CHECK_FALSE(d[1].printerClass);
    CHECK(parseUsbDevices("not json").isEmpty());
    CHECK(parseUsbDevices("{}").isEmpty());
}

TEST_CASE("USB on a tablet: any printer is picked, no model named anywhere", "[print][usb]")
{
    QString error;
    const UsbDeviceInfo keyboard = device("/dev/bus/usb/001/002", "Logi", "Keyboard", false, false);
    const UsbDeviceInfo stick = device("/dev/bus/usb/001/003", "SanDisk", "Cruzer", false, true);
    const UsbDeviceInfo epson = device("/dev/bus/usb/001/004", "EPSON", "TM-T20II", true, true, "J8KF0");
    const UsbDeviceInfo other = device("/dev/bus/usb/001/005", "Generic", "POS-80", true, true);

    // Nothing that takes data: no printer.
    CHECK(pickUsbDevice({}, {}, &error) == -1);
    CHECK(error.contains(u"no USB printer"_s));
    CHECK(pickUsbDevice({keyboard}, {}, &error) == -1);

    // The printer, whatever else is on the hub.
    CHECK(pickUsbDevice({keyboard, stick, epson}, {}, &error) == 2);
    CHECK(pickUsbDevice({keyboard, other}, {}, &error) == 1);
    // No printer-class device: the first that takes data.
    CHECK(pickUsbDevice({keyboard, stick}, {}, &error) == 1);

    // Two printers: which one?
    error.clear();
    CHECK(pickUsbDevice({epson, other}, {}, &error) == -1);
    CHECK(error.contains(u"2 USB printers"_s));
    CHECK(error.contains(u"usb:<serial number or name>"_s));
    CHECK(error.contains(u"EPSON TM-T20II (serial J8KF0)"_s));

    // By serial number, by name (any case), or part of it.
    CHECK(pickUsbDevice({epson, other}, u"J8KF0"_s, &error) == 0);
    CHECK(pickUsbDevice({epson, other}, u"pos-80"_s, &error) == 1);
    CHECK(pickUsbDevice({epson, other}, u"Generic POS-80"_s, &error) == 1);
    CHECK(pickUsbDevice({epson, other}, u"epson"_s, &error) == 0);
    CHECK(pickUsbDevice({epson, other}, u"/dev/bus/usb/001/005"_s, &error) == 1);
    // Never a keyboard, even by its name.
    CHECK(pickUsbDevice({keyboard, epson}, u"Keyboard"_s, &error) == -1);
    error.clear();
    CHECK(pickUsbDevice({epson, other}, u"Star"_s, &error) == -1);
    CHECK(error.contains(u"\"Star\""_s));
    CHECK(error.contains(u"a serial number shows only once"_s));   // POS-80's isn't known yet
}

TEST_CASE("USB on a tablet: a ticket goes out a chunk at a time, all of it or it failed", "[print][usb]")
{
    QByteArray ticket;
    for (int i = 0; i < 40'000; ++i)
        ticket += char(i * 7);   // every byte value, NULs too
    QString error;

    // 16 KB chunks, the last shorter; every byte once, in order.
    std::vector<int> sizes;
    QByteArray got;
    REQUIRE(sendInChunks(ticket, UsbChunkBytes, 60'000, [&](const char *p, int n, int ms) {
        CHECK(ms > 0);
        CHECK(ms <= 60'000);
        sizes.push_back(n);
        got.append(p, n);
        return n;
    }, &error));
    CHECK(sizes == std::vector<int>{16384, 16384, 7232});
    CHECK(got == ticket);

    // A printer that takes part of a chunk (and sometimes nothing): the rest goes next.
    got.clear();
    int calls = 0;
    REQUIRE(sendInChunks(ticket, UsbChunkBytes, 60'000, [&](const char *p, int n, int) {
        if (++calls % 3 == 0)
            return 0;
        const int took = std::max(1, n / 2);
        got.append(p, took);
        return took;
    }, &error));
    CHECK(got == ticket);

    // It stops taking it (unplugged): not printed, and never sent again from here.
    got.clear();
    error.clear();
    calls = 0;
    CHECK_FALSE(sendInChunks(ticket, UsbChunkBytes, 60'000, [&](const char *p, int n, int) {
        if (++calls > 1)
            return -1;
        got.append(p, n);
        return n;
    }, &error));
    CHECK(calls == 2);
    CHECK(got.size() == 16384);
    CHECK(error.contains(u"stopped taking the ticket"_s));
    CHECK(error.contains(u"16384 of 40000 bytes sent"_s));

    // Out of time (a printer in error takes nothing): failed within the time, never "printed".
    error.clear();
    QElapsedTimer t;
    t.start();
    CHECK_FALSE(sendInChunks(ticket, UsbChunkBytes, 300, [&](const char *, int, int ms) {
        QThread::msleep(ms);   // bulkTransfer waits its timeout, then -1
        return -1;
    }, &error));
    CHECK(t.elapsed() < 2000);
    CHECK(error.contains(u"did not take the ticket"_s));

    // Nothing to send: done.
    CHECK(sendInChunks(QByteArray(), UsbChunkBytes, 1000, [](const char *, int, int) { return -1; }, &error));

    // Not a tablet: no USB printing here (and no hang).
    error.clear();
    CHECK_FALSE(usbPrint(QString(), ticket, 1000, &error));
    CHECK_FALSE(error.isEmpty());
}

TEST_CASE("USB on a tablet: the store's spooler waits for the tablet, one ticket at a time", "[print][usb][spooler]")
{
    PrintSpooler spooler;
    spooler.setTimings(200, 50, 2000, 100);
    QSignalSpy printed(&spooler, &PrintSpooler::jobPrinted);
    QSignalSpy failed(&spooler, &PrintSpooler::jobFailed);
    QSignalSpy inError(&spooler, &PrintSpooler::printerError);

    // No tablet sender (this computer doesn't serve tablets): failed, held for a Reprint.
    spooler.submit(usbPrinter(), "ticket", u"Check 1"_s);
    REQUIRE(spooler.waitIdle(5000));
    REQUIRE(failed.count() == 1);
    CHECK(failed.at(0).at(2).toString().contains(u"Bar Tablet"_s));
    CHECK(spooler.held(u"tablet-bar"_s) == 1);
    REQUIRE(inError.count() == 1);
    CHECK(inError.at(0).at(2).toBool());

    // The tablet answers a while later, from its own thread; never two at once.
    std::mutex m;
    std::vector<QByteArray> sent;
    std::vector<int> times;
    std::vector<std::string> tablets;
    std::atomic<int> going{0}, most{0};
    std::vector<std::thread> threads;
    spooler.setTabletSender([&](const core::PrinterConfig &p, const QByteArray &data, int timeoutMs,
                                PrintSpooler::TabletDone done) {
        const int now = ++going;
        most = std::max(most.load(), now);
        std::lock_guard lock(m);
        tablets.push_back(p.terminal);
        sent.push_back(data);
        times.push_back(timeoutMs);
        threads.emplace_back([&going, done = std::move(done)] {
            QThread::msleep(30);
            --going;
            done(true, {});
        });
    });
    spooler.retryHeld(u"tablet-bar"_s);   // Reprint: the held one goes
    spooler.submit(usbPrinter(), "two", u"Check 2"_s);
    spooler.submit(usbPrinter(), "three", u"Check 3"_s);
    REQUIRE(spooler.waitIdle(5000));
    for (std::thread &t : threads)
        t.join();
    threads.clear();
    CHECK(printed.count() == 3);
    CHECK(most.load() == 1);
    REQUIRE(sent.size() == 3);
    CHECK(sent[0] == "ticket");
    CHECK(sent[1] == "two");
    CHECK(sent[2] == "three");
    CHECK(times[0] == 2000);   // the device time (60 s in the store)
    CHECK(tablets == std::vector<std::string>(3, "Bar Tablet"));
    CHECK(spooler.held(u"tablet-bar"_s) == 0);
    REQUIRE(inError.count() == 2);
    CHECK_FALSE(inError.at(1).at(2).toBool());   // printing again

    // The tablet says it didn't print (paper out, unplugged): tried again once, then held,
    // the printer's error on every screen.
    failed.clear();
    int tries = 0;
    spooler.setTabletSender([&](const core::PrinterConfig &, const QByteArray &, int, PrintSpooler::TabletDone done) {
        ++tries;
        done(false, u"the printer stopped taking the ticket (0 of 4 bytes sent; unplugged?)"_s);
    });
    spooler.submit(usbPrinter(), "four", u"Check 4"_s);
    REQUIRE(spooler.waitIdle(5000));
    CHECK(tries == PrintSpooler::DeviceAttempts);
    REQUIRE(failed.count() == 1);
    CHECK(failed.at(0).at(2).toString().contains(u"unplugged"_s));
    CHECK(spooler.held(u"tablet-bar"_s) == 1);
    CHECK(inError.last().at(2).toBool());

    // No tablet named: says so.
    failed.clear();
    spooler.submit(usbPrinter(""), "five", u"Check 5"_s);
    REQUIRE(spooler.waitIdle(5000));
    REQUIRE(failed.count() == 1);
    CHECK(failed.at(0).at(2).toString().contains(u"which tablet"_s));
}

TEST_CASE("USB on a tablet: the cash drawer opens through it too", "[print][usb][drawers]")
{
    QTemporaryDir dir;
    auto seed = test::seedPosData();
    seed.settings.printers.push_back(usbPrinter());
    PrintSpooler spooler;
    std::mutex m;
    QByteArray got;
    spooler.setTabletSender([&](const core::PrinterConfig &, const QByteArray &data, int, PrintSpooler::TabletDone done) {
        {
            std::lock_guard lock(m);
            got += data;
        }
        done(true, {});
    });
    TicketPrinter printer(spooler, dir.path());
    printer.openDrawer(seed.settings, "tablet-bar");
    REQUIRE(spooler.waitIdle(5000));
    std::lock_guard lock(m);
    CHECK(got.contains("\x1bp"));   // ESC p: the drawer's pulse
}

TEST_CASE("USB on a tablet: the ticket goes to that tablet and its answer comes back", "[print][usb][net]")
{
    test::RecordingSink sink;
    app::PosShared shared{test::seedPosData(), &sink};
    auto layout = test::loadTestLayout();
    REQUIRE(layout);
    net::LayoutHub hub{*layout};
    net::PosServer server{&shared, &hub};
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));
    const auto connect = [&](const QString &name) {
        core::TerminalConfig t;
        t.name = name.toStdString();
        t.id = net::newDeviceId().toStdString();
        const QByteArray key = net::newDeviceKey();
        t.key = key.toBase64().toStdString();
        shared.settings.terminals.push_back(t);
        net::Credentials c;
        c.host = u"127.0.0.1"_s;
        c.port = server.port();
        c.terminalId = QString::fromStdString(t.id);
        c.terminalName = name;
        c.key = key;
        auto r = std::make_unique<net::RemoteSession>(name);
        r->setCredentials(c);
        r->connectTo(u"127.0.0.1"_s, server.port());
        REQUIRE(r->waitForWelcome(5000));
        return r;
    };
    auto tablet = connect(u"Bar Tablet"_s);
    auto other = connect(u"Register"_s);   // a screen with no USB printing

    QString gotSelector;
    QByteArray gotData;
    int gotMs = 0;
    bool answer = true;
    tablet->setUsbPrinter([&](const QString &selector, const QByteArray &data, int ms,
                              std::function<void(bool, const QString &)> done) {
        gotSelector = selector;
        gotData = data;
        gotMs = ms;
        std::thread([done = std::move(done), ok = answer] { done(ok, ok ? QString() : u"paper out"_s); }).detach();
    });

    struct Result {
        bool came = false, ok = false;
        QString error;
    };
    const auto print = [&](const QString &terminal, const QString &selector, const QByteArray &data) {
        auto r = std::make_shared<Result>();
        server.printOnTablet(terminal, selector, data, 60'000, [r](bool ok, const QString &error) {
            r->came = true;
            r->ok = ok;
            r->error = error;
        });
        REQUIRE(waitUntil([&] { return r->came; }));
        return *r;
    };

    // Binary bytes arrive as they were; the tablet's name in any case.
    const QByteArray ticket = QByteArray("\x1b@Hello\0\xff\x1bp\0\x19\xfa", 15);
    Result r = print(u"bar tablet"_s, u"J8KF0"_s, ticket);
    CHECK(r.ok);
    CHECK(gotData == ticket);
    CHECK(gotSelector == u"J8KF0"_s);
    CHECK(gotMs == 60'000);

    // The tablet's answer, when it didn't print.
    answer = false;
    r = print(u"Bar Tablet"_s, QString(), "x");
    CHECK_FALSE(r.ok);
    CHECK(r.error == u"paper out"_s);

    // A screen that can't print to USB says so; a tablet that isn't there: at once.
    r = print(u"Register"_s, QString(), "x");
    CHECK_FALSE(r.ok);
    CHECK(r.error.contains(u"can't print to a USB printer"_s));
    r = print(u"Patio Tablet"_s, QString(), "x");
    CHECK_FALSE(r.ok);
    CHECK(r.error.contains(u"not connected"_s));

    // It goes away while printing: not printed (the spooler holds it for a Reprint).
    tablet->setUsbPrinter([](const QString &, const QByteArray &, int, std::function<void(bool, const QString &)>) {});
    auto lost = std::make_shared<Result>();
    server.printOnTablet(u"Bar Tablet"_s, QString(), "x", 60'000, [lost](bool ok, const QString &error) {
        lost->came = true;
        lost->ok = ok;
        lost->error = error;
    });
    QCoreApplication::processEvents();
    tablet.reset();
    REQUIRE(waitUntil([&] { return lost->came; }));
    CHECK_FALSE(lost->ok);
    CHECK(lost->error.contains(u"lost its connection"_s));
}
