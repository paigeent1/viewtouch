// Serial barcode scanners (ui/cpp/serialscanner.*): VT_SERIAL_SCANNER read, and scanners on
// pseudo-terminals standing in for the ports of a USB-serial adapter.
#include <catch2/catch_test_macros.hpp>

#include "qt_catch.hh"
#include "serialscanner.hh"

#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#ifdef Q_OS_LINUX
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <functional>

using namespace Qt::StringLiterals;
using Port = SerialScanners::Port;

TEST_CASE("Serial scanner setting: one port, a list, all, off", "[scanner]")
{
    SECTION("not set: every USB-serial port") {
        for (const QString &v : {u""_s, u"  "_s, u"all"_s, u"ALL"_s, u"auto"_s}) {
            const auto s = SerialScanners::parse(v, 9600);
            CHECK(s.all);
            CHECK_FALSE(s.off);
            CHECK(s.ports.isEmpty());
            CHECK(s.baud == 9600);
        }
        CHECK(SerialScanners::parse(u"all@19200"_s, 9600).baud == 19200);
        CHECK(SerialScanners::parse(u""_s, 4800).baud == 4800);   // SERIAL_SCANNER_BAUD
    }
    SECTION("off") {
        for (const QString &v : {u"off"_s, u"OFF"_s, u"off, /dev/ttyUSB0"_s}) {
            const auto s = SerialScanners::parse(v, 9600);
            CHECK(s.off);
            CHECK_FALSE(s.all);
            CHECK(s.ports.isEmpty());
        }
    }
    SECTION("one port: only it") {
        const QString one = u"/dev/serial/by-id/usb-Keyspan__a_division_of_InnoSys_Inc._Keyspan_USA-49WG-if00-port0"_s;
        const auto s = SerialScanners::parse(one, 9600);
        CHECK_FALSE(s.all);
        REQUIRE(s.ports.size() == 1);
        CHECK(s.ports[0] == Port{one, 9600});
    }
    SECTION("a list by commas or spaces, each with its own speed") {
        const auto s = SerialScanners::parse(u"/dev/ttyUSB0, ttyUSB1@19200  /dev/ttyACM0,,/dev/ttyUSB0@4800"_s, 9600);
        CHECK_FALSE(s.all);
        REQUIRE(s.ports.size() == 3);   // the same port twice: the first
        CHECK(s.ports[0] == Port{u"/dev/ttyUSB0"_s, 9600});
        CHECK(s.ports[1] == Port{u"/dev/ttyUSB1"_s, 19200});   // a name without / is under /dev
        CHECK(s.ports[2] == Port{u"/dev/ttyACM0"_s, 9600});
    }
    SECTION("all with some ports at their own speed") {
        const auto s = SerialScanners::parse(u"all@19200 /dev/ttyUSB3@9600 /dev/ttyUSB4"_s, 9600);
        CHECK(s.all);
        CHECK(s.baud == 19200);
        REQUIRE(s.ports.size() == 2);
        CHECK(s.ports[0].baud == 9600);
        CHECK(s.ports[1].baud == 19200);   // no speed of its own: the "all" one
    }
}

#ifdef Q_OS_LINUX
namespace {

// A pseudo-terminal pair: the scanner reads the port (path); the test types on the other end.
struct FakePort {
    int master = -1;
    QString path;
    FakePort()
    {
        master = posix_openpt(O_RDWR | O_NOCTTY);
        if (master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0)
            path = QString::fromLocal8Bit(ptsname(master));
    }
    ~FakePort() { close(); }
    void close()
    {
        if (master >= 0)
            ::close(master);
        master = -1;
    }
    void send(const QByteArray &bytes) const { REQUIRE(::write(master, bytes.constData(), size_t(bytes.size())) == bytes.size()); }
};

bool waitFor(const std::function<bool()> &done, int ms = 3000)
{
    for (int waited = 0; !done() && waited < ms; waited += 20)
        QTest::qWait(20);
    return done();
}

} // namespace

TEST_CASE("Two serial scanners on one register: codes from either", "[scanner]")
{
    FakePort product, id;
    REQUIRE_FALSE(product.path.isEmpty());
    REQUIRE_FALSE(id.path.isEmpty());

    SerialScanners scanners(product.path + u", "_s + id.path + u"@19200"_s, 9600);
    QSignalSpy spy(&scanners, &SerialScanners::scanned);
    scanners.start();
    REQUIRE(scanners.scanners().size() == 2);
    CHECK(waitFor([&] { return scanners.scanners()[0]->isOpen() && scanners.scanners()[1]->isOpen(); }));
    CHECK(scanners.scanners()[1]->baud() == 19200);

    const QVariantList status = SerialScanner::status();
    CHECK(std::count_if(status.begin(), status.end(), [&](const QVariant &v) {
              return v.toMap()[u"device"_s] == id.path && v.toMap()[u"baud"_s] == 19200 && v.toMap()[u"open"_s].toBool();
          }) == 1);

    product.send("012345678905\r");
    REQUIRE(waitFor([&] { return spy.size() == 1; }));
    CHECK(spy[0][0].toString() == u"012345678905"_s);

    // A license (PDF417, many lines) from the ID scanner: one code, sent whole when it stops.
    id.send("@\n\x1e\rANSI 636001080002DL00410278ZN03190008DLDAQ123456789\nDBB01011990\r");
    REQUIRE(waitFor([&] { return spy.size() == 2; }));
    const QString license = spy[1][0].toString();
    CHECK(license.startsWith(u"@"_s));
    CHECK(license.contains(u"DAQ123456789"_s));
    CHECK(license.contains(u"DBB01011990"_s));
}

TEST_CASE("Serial scanners on every port: found, a printer's port left alone, unplugged", "[scanner]")
{
    FakePort a, b;
    REQUIRE_FALSE(a.path.isEmpty());
    QStringList ports{a.path, b.path};
    QStringList printers{b.path};   // a "file" printer prints to this one (a pole display, say)

    SerialScanners scanners(u""_s, 9600);
    scanners.setFinder([&] { return ports; });
    scanners.setSkip([&] { return printers; });
    QSignalSpy spy(&scanners, &SerialScanners::scanned);
    scanners.start();
    REQUIRE(scanners.scanners().size() == 1);
    CHECK(scanners.scanners()[0]->device() == a.path);

    // The printer moved elsewhere: its port is listened on at the next look.
    printers.clear();
    scanners.rescan();
    REQUIRE(scanners.scanners().size() == 2);
    CHECK(waitFor([&] { return scanners.scanners()[1]->isOpen(); }));
    b.send("4006381333931\n");
    REQUIRE(waitFor([&] { return spy.size() == 1; }));
    CHECK(spy[0][0].toString() == u"4006381333931"_s);

    // A printer set on a port being read: let go.
    printers = {a.path};
    scanners.rescan();
    REQUIRE(scanners.scanners().size() == 1);
    CHECK(scanners.scanners()[0]->device() == b.path);
    printers.clear();

    // Unplugged (gone from the ports): closed, then let go.
    ports = {a.path};
    b.close();
    CHECK(waitFor([&] {
        scanners.rescan();
        const QList<SerialScanner *> now = scanners.scanners();   // a copy: begin and end of one list
        return std::none_of(now.begin(), now.end(), [&](const SerialScanner *s) { return s->device() == b.path; });
    }));
    // Nothing listed twice when a port is seen again.
    scanners.rescan();
    scanners.rescan();
    CHECK(scanners.scanners().size() == 1);
}
#endif
