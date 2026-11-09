#include <catch2/catch_test_macros.hpp>
#include <QDir>
#include <QImage>
#include <QBuffer>
#include "print/raster.hh"

#include "pos_fixture.hh"
#include "print/document.hh"
#include "print/spooler.hh"
#include "print/ticket_printer.hh"
#include "print/tickets.hh"
#include "app/pos_json.hh"
#include "print/ticket_image.hh"
#include <QFileInfo>
#include "qt_catch.hh"

#include <QElapsedTimer>
#include <atomic>
#include <QFile>
#include <QLocale>
#include <QDateTime>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;
using namespace vt;
using namespace vt::print;

namespace {

bool contains(const std::string &hay, const std::string &needle) { return hay.find(needle) != std::string::npos; }

TicketContext ctx(const core::PosSettings &s)
{
    return {s, [](std::int64_t) { return std::string("Sep 29, 2026 10:31 AM"); },
            [](std::int64_t) { return std::string("10:31 AM"); }, 0};
}

core::Check burgerCheck(const core::PosSettings &s, const std::vector<core::MenuItem> &menu)
{
    auto item = [&](const char *id) {
        for (const auto &m : menu)
            if (m.id == id) return m;
        FAIL("no item " << id);
        return core::MenuItem{};
    };
    core::Check c;
    c.id = 42;
    c.label = "T3";
    c.guests = 2;
    c.serverName = "Sam";
    const auto &b = c.addItem(item("classic-burger"));
    c.addModifier(b.id, item("medium-rare"));
    c.addModifier(b.id, item("onion-rings"));
    c.addItem(item("draft-beer"));
    c.addComment("No pickles");
    c.addPayment(*s.tender("cash"), Money::fromCents(3000));
    return c;
}

QString readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

TEST_CASE("Text rendering: columns, wrapping, centering", "[print]")
{
    Document d;
    d.center("Café");
    d.columns("Classic Burger", "$11.50");
    d.columns("A very long item name that has to wrap onto the next line", "$1.00");
    d.rule();
    const std::string out = renderText(d, 24);
    CHECK(contains(out, "          Café\n"));                 // (24 - 4) / 2 spaces
    CHECK(contains(out, "Classic Burger    $11.50\n"));      // exactly 24 wide
    CHECK(contains(out, std::string(24, '-')));
    for (const char *line : {"A very long item", "$1.00"})
        CHECK(contains(out, line));
    std::istringstream in(out);
    for (std::string row; std::getline(in, row);)
        CHECK(displayWidth(row) <= 24);
}

TEST_CASE("ESC/POS: init, emphasis, cut, drawer kick, letters", "[print]")
{
    Document d;
    d.text("Crème brûlée", Document::Align::Left, true);
    d.kickDrawer = true;
    // Accented letters: the PC858 character set.
    const std::string bytes = renderEscPos(d, 42);
    CHECK(bytes.rfind("\x1b@\x1bt\x13", 0) == 0);              // ESC @, then ESC t 19
    CHECK(contains(bytes, std::string("\x1b" "E" "\x01", 3)));  // bold on
    CHECK(contains(bytes, "Cr\x8A" "me br\x96" "l\x82" "e"));     // è û é in PC858
    CHECK(contains(bytes, std::string("\x1bp\x00\x19\xfa", 5)));
    CHECK(contains(bytes, std::string("\x1dV\x42\x00", 4)));   // partial cut
    // Plain letters: no character set, ASCII only.
    const std::string plain = renderEscPos(d, 42, false);
    CHECK_FALSE(contains(plain, "\x1bt"));
    CHECK(contains(plain, "Creme brulee"));
    CHECK(std::ranges::none_of(plain, [](char c) { return static_cast<unsigned char>(c) >= 0x80 && c != '\xfa'; }));
}

TEST_CASE("Customer on receipts: none by default, first name or everything when the store asks", "[print][customer]")
{
    auto seed = test::seedPosData();
    core::Check c = burgerCheck(seed.settings, seed.menu);
    c.customer.name = "Pat Example";
    c.customer.phone = "555-010-9999";
    c.customer.address = "1 Test Lane";
    std::string r = renderText(receipt(c, ctx(seed.settings)), 42);
    CHECK_FALSE(contains(r, "Pat"));
    CHECK_FALSE(contains(r, "555-010-9999"));
    CHECK_FALSE(contains(r, "1 Test Lane"));
    seed.settings.receiptCustomer = "first";
    r = renderText(receipt(c, ctx(seed.settings)), 42);
    CHECK(contains(r, "Pat"));
    CHECK_FALSE(contains(r, "Example"));
    CHECK_FALSE(contains(r, "555-010-9999"));
    seed.settings.receiptCustomer = "full";
    r = renderText(receipt(c, ctx(seed.settings)), 42);
    CHECK(contains(r, "Pat Example"));
    CHECK(contains(r, "555-010-9999"));
    CHECK(contains(r, "1 Test Lane"));
    seed.settings.receiptCustomer.clear();
    c.type = core::CheckType::Delivery;   // a delivery keeps its address
    r = renderText(receipt(c, ctx(seed.settings)), 42);
    CHECK(contains(r, "1 Test Lane"));
    CHECK_FALSE(contains(r, "555-010-9999"));
}

TEST_CASE("Privacy notice: on receipts with a customer's details, the full one on its own", "[print][privacy]")
{
    auto seed = test::seedPosData();
    core::Check c = burgerCheck(seed.settings, seed.menu);
    CHECK_FALSE(contains(renderText(receipt(c, ctx(seed.settings)), 42), "PRIVACY"));   // nobody's details
    c.customer.phone = "555-010-1234";
    std::string r = renderText(receipt(c, ctx(seed.settings)), 42);
    CHECK(contains(r, "PRIVACY: ViewTouch Caf"));                  // {store} filled in
    {   // in the printer's small font (ESC M 1 just before it)
        const std::string esc = renderEscPos(receipt(c, ctx(seed.settings)), 42, false);
        const auto at = esc.find("PRIVACY");
        REQUIRE(at != std::string::npos);
        CHECK(esc.rfind(std::string("\x1bM\x01"), at) != std::string::npos);
        CHECK(esc.rfind(std::string("\x1bM\x01"), at) > esc.rfind(std::string("\x1bM") + std::string(1, '\0'), at));
    }
    CHECK_FALSE(contains(r, "{store}"));
    seed.settings.privacyOnReceipts = false;
    CHECK_FALSE(contains(renderText(receipt(c, ctx(seed.settings)), 42), "PRIVACY"));
    seed.settings.privacyOnReceipts = true;
    seed.settings.privacyShort = "We keep it safe at {store}.";
    CHECK(contains(renderText(receipt(c, ctx(seed.settings)), 42), "We keep it safe at ViewTouch"));

    const std::string full = renderText(privacyNoticeTicket(ctx(seed.settings)), 42);
    CHECK(contains(full, "PRIVACY NOTICE"));
    CHECK(contains(full, "123 Main Street"));                     // {contact}: the receipt header
    CHECK(contains(full, "Effective November 4, 2026"));
    CHECK(contains(full, "do not sell"));
}

TEST_CASE("ID check on receipts: the customer's copy says checked 21+; the date of birth only when asked", "[print][age]")
{
    const app::PosData seed = test::seedPosData();
    core::Check check = burgerCheck(seed.settings, seed.menu);
    check.ageVerified = 21;
    check.ageHow = "scan";
    check.ageDob = "1987-01-25";   // made up
    const std::string customer = renderText(receipt(check, ctx(seed.settings)), 42);
    CHECK(contains(customer, "ID checked 21+"));
    CHECK_FALSE(contains(customer, "Date of birth"));
    CHECK_FALSE(contains(customer, "1987"));
    check.printDob = true;   // Reprint Receipt's "with DOB": the store's copy
    const std::string store = renderText(receipt(check, ctx(seed.settings)), 42);
    CHECK(contains(store, "Date of birth: 01/25/1987"));
    check.printDob = false;
    check.ageIdExpired = true;
    CHECK(contains(renderText(receipt(check, ctx(seed.settings)), 42), "ID expired"));
    // Never saved: the printing flag doesn't survive a save and load; the date does.
    check.printDob = true;
    const std::optional<core::Check> back = app::checkFromJson(app::toJson(check));
    REQUIRE(back);
    CHECK_FALSE(back->printDob);
    CHECK(back->ageDob == "1987-01-25");
}

TEST_CASE("Receipt and kitchen ticket contents", "[print]")
{
    const auto seed = test::seedPosData();
    const core::Check c = burgerCheck(seed.settings, seed.menu);

    const std::string r = renderText(receipt(c, ctx(seed.settings)), 42);
    CHECK(contains(r, "ViewTouch Café"));
    CHECK(contains(r, "123 Main Street"));
    CHECK(contains(r, "Check #42"));
    CHECK(contains(r, "Classic Burger"));
    CHECK(contains(r, "Onion Rings"));
    CHECK_FALSE(contains(r, "No pickles"));    // kitchen notes stay off the receipt
    // 11.50 + 1.00 food, 6.00 beer: tax 1.03 + 0.60 = 1.63; total 20.13; change 9.87
    CHECK(contains(r, "$20.13"));
    CHECK(contains(r, "Change"));
    CHECK(contains(r, "$9.87"));
    CHECK(contains(r, "Thank you for visiting!"));

    const std::string k = renderText(kitchenTicket(c, {c.lines[0], c.lines[2]}, "Kitchen", false, ctx(seed.settings)), 42);
    CHECK(contains(k, "KITCHEN"));
    CHECK(contains(k, "T3"));
    CHECK(contains(k, "1 Classic Burger"));
    CHECK(contains(k, "> MR"));
    CHECK(contains(k, "** No pickles **"));
    CHECK_FALSE(contains(k, "$"));             // no prices in the kitchen
    CHECK(contains(renderText(kitchenTicket(c, {c.lines[0]}, "Kitchen", true, ctx(seed.settings)), 42), "*** VOID ***"));
}

TEST_CASE("Report ticket lays out columns", "[print]")
{
    const auto seed = test::seedPosData();
    core::Report rep;
    rep.title = "Item Sales";
    rep.section("Burgers");
    rep.line({"Classic Burger", "3", "$34.50"});
    rep.total({"Burgers total", "3", "$34.50"});
    const std::string out = renderText(reportTicket(rep, ctx(seed.settings)), 42);
    CHECK(contains(out, "Item Sales"));
    // 42 wide: name, padding, then each value right-aligned in 9 columns.
    CHECK(contains(out, "Classic Burger" + std::string(18, ' ') + "3   $34.50\n"));
}

TEST_CASE("TicketPrinter routes kitchen lines by printer and writes files", "[print]")
{
    QTemporaryDir dir;
    auto seed = test::seedPosData();
    PrintSpooler spooler;
    TicketPrinter printer(spooler, dir.path());
    const core::Check c = burgerCheck(seed.settings, seed.menu);

    printer.printKitchen(seed.settings, c, c.lines, false);
    printer.printReceipt(seed.settings, c, "receipt");
    printer.openDrawer(seed.settings, "receipt");
    REQUIRE(spooler.waitIdle(5000));

    const QString kitchen = readFile(dir.filePath(u"kitchen.txt"_s));
    const QString bar = readFile(dir.filePath(u"bar.txt"_s));
    const QString receiptText = readFile(dir.filePath(u"receipt.txt"_s));
    CHECK(kitchen.contains(u"Classic Burger"_s));
    CHECK(kitchen.contains(u"No pickles"_s));        // comments follow the kitchen
    CHECK_FALSE(kitchen.contains(u"Draft Beer"_s));
    CHECK(bar.contains(u"Draft Beer"_s));
    CHECK(receiptText.contains(u"TOTAL"_s));
    CHECK(receiptText.contains(u"[drawer opened]"_s));   // receipt printer has the drawer
}

TEST_CASE("No Sale prints a slip like a kitchen ticket (no logo) and opens the drawer", "[print][nosale]")
{
    QTemporaryDir dir;
    auto seed = test::seedPosData();
    PrintSpooler spooler;
    TicketPrinter printer(spooler, dir.path());
    core::CashMovement m;
    m.kind = core::CashMovement::Kind::NoSale;
    m.by = "Morgan";
    m.reason = "Make Change";
    m.at = 1'700'000'000'000;
    const Document d = noSaleTicket(m, 3, "Drawer 1", ctx(seed.settings));
    CHECK(d.kickDrawer);
    CHECK(std::ranges::none_of(d.lines, [](const Document::Line &l) { return l.kind == Document::Kind::Image; }));
    printer.printNoSale(seed.settings, m, 3, "Drawer 1", "receipt");
    REQUIRE(spooler.waitIdle(5000));
    const QString out = readFile(dir.filePath(u"receipt.txt"_s));
    CHECK(out.contains(u"****No Sale****"_s));
    CHECK(out.contains(u"No Sale #3"_s));
    CHECK(out.contains(u"Make Change"_s));
    CHECK(out.contains(u"Morgan"_s));
    CHECK(out.contains(u"[drawer opened]"_s));
}

TEST_CASE("No borrowing: a drawer opens only on the printer asked for, never another one with a drawer",
          "[print][nosale][drawers]")
{
    QTemporaryDir dir;
    auto seed = test::seedPosData();   // Receipt has the drawer
    core::PrinterConfig usb;
    usb.id = "usb";
    usb.name = "VT-BHDAN-RECEIPT";
    usb.type = "file";
    usb.path = "usb.txt";
    usb.drawerKick = false;
    seed.settings.printers.push_back(usb);
    PrintSpooler spooler;
    TicketPrinter printer(spooler, dir.path());
    core::CashMovement m;
    m.kind = core::CashMovement::Kind::NoSale;
    m.by = "Morgan";
    m.at = 1'700'000'000'000;

    // The terminal's printer has no drawer: the slip there, no kick, and nothing on Receipt.
    printer.openDrawer(seed.settings, "usb");
    printer.printNoSale(seed.settings, m, 1, "Terminal drawer", "usb");
    REQUIRE(spooler.waitIdle(5000));
    const QString onUsb = readFile(dir.filePath(u"usb.txt"_s));
    CHECK(onUsb.contains(u"****No Sale****"_s));
    CHECK_FALSE(onUsb.contains(u"[drawer opened]"_s));
    CHECK(readFile(dir.filePath(u"receipt.txt"_s)).isEmpty());

    // A printer that isn't set up: no drawer opened anywhere.
    printer.openDrawer(seed.settings, "gone");
    REQUIRE(spooler.waitIdle(5000));
    CHECK_FALSE(readFile(dir.filePath(u"receipt.txt"_s)).contains(u"[drawer opened]"_s));

    // With the drawer wired to it, it opens there.
    seed.settings.printers.back().drawerKick = true;
    printer.printNoSale(seed.settings, m, 2, "Terminal drawer", "usb");
    printer.openDrawer(seed.settings, "usb");
    REQUIRE(spooler.waitIdle(5000));
    const QString after = readFile(dir.filePath(u"usb.txt"_s));
    const qsizetype first = after.indexOf(u"[drawer opened]"_s);
    CHECK(first >= 0);
    CHECK(after.indexOf(u"[drawer opened]"_s, first + 1) > first);   // the slip's and the kick's
    CHECK_FALSE(readFile(dir.filePath(u"receipt.txt"_s)).contains(u"[drawer opened]"_s));
}

TEST_CASE("Exception tickets are saved as pictures and one PDF, the store's copy showing voids",
          "[print][exceptions]")
{
    QTemporaryDir dir;
    auto seed = test::seedPosData();
    seed.settings.receiptShowVoids = false;   // the customer's receipt hides them; the store's copy doesn't
    PrintSpooler spooler;
    TicketPrinter printer(spooler, dir.path());
    printer.setExceptionsDir(dir.filePath(u"exceptions"_s));
    core::Check c = burgerCheck(seed.settings, seed.menu);
    c.lines.front().voided = true;
    core::CashMovement m;
    m.kind = core::CashMovement::Kind::NoSale;
    m.by = "Morgan";
    m.reason = "Make Change";
    m.at = 1'700'000'000'000;
    core::Report r;
    r.title = "Exceptions";
    r.section("Drawer opened with no sale");
    r.line({"Morgan", "1", ""});
    std::vector<app::PosPrinter::ExceptionTicket> tickets;
    tickets.push_back({"check-" + std::to_string(c.id), "Check #" + std::to_string(c.id) + ": void", c, std::nullopt, 0, {}});
    tickets.push_back({"nosale-1", "No Sale #1: Make Change", std::nullopt, m, 1, "Drawer 1"});
    const QString out = printer.saveExceptionTickets(seed.settings, r, tickets, 7, 1'700'000'000'000);
    REQUIRE(!out.isEmpty());
    const QDir d(out);
    const QStringList pictures = d.entryList({u"*.jpg"_s, u"*.png"_s}, QDir::Files);
    CHECK(pictures.size() == 3);   // the report, the check, the No Sale
    CHECK(d.entryList({u"*.pdf"_s}, QDir::Files).size() == 1);
    const QString ext = pictures.isEmpty() ? u"jpg"_s : QFileInfo(pictures.front()).suffix();
    const QImage check(d.filePath(u"check-%1.%2"_s.arg(c.id).arg(ext)));
    REQUIRE(!check.isNull());
    CHECK(check.height() > check.width());   // a strip of paper

    // The picture is the printer's layout: the voided line is on the store's copy.
    const Document text = receipt(c, [&] {
        static core::PosSettings store;
        store = seed.settings;
        store.receiptShowVoids = true;
        return ctx(store);
    }());
    CHECK(contains(renderText(text, 42), "VOID"));
    const QImage drawn = ticketImage(text, 42);
    CHECK(!drawn.isNull());
    CHECK(drawn.pixelColor(2, 2) == QColor(Qt::white));

    // End of day: the same folder gets the summary PDF (the reports, then the ticket pictures).
    const QString eod = printer.saveEndOfDay(seed.settings, {r}, r, tickets, 7, 1'700'000'000'000);
    REQUIRE(eod == out);
    const QStringList pdfs = QDir(eod).entryList({u"EOD Summary *.pdf"_s}, QDir::Files);
    REQUIRE(pdfs.size() == 1);
    CHECK(QFileInfo(QDir(eod).filePath(pdfs.front())).size() > 1000);
}

TEST_CASE("TicketPrinter sends labels in the label printer's language, or as slips", "[print][labels]")
{
    QTemporaryDir dir;
    auto seed = test::seedPosData();
    core::PrinterConfig zpl;
    zpl.id = "labels";
    zpl.name = "Labels";
    zpl.type = "file";
    zpl.path = "labels.zpl";
    zpl.format = "zpl";
    seed.settings.printers.push_back(zpl);
    PrintSpooler spooler;
    TicketPrinter printer(spooler, dir.path());
    printer.printLabels(seed.settings, {{"Pico de gallo", {"Made Mon 9:00 AM"}}, {"Salsa", {"Made Mon 9:00 AM"}}}, "labels");
    printer.printLabels(seed.settings, {{"Rice", {"Made Mon 9:00 AM"}}}, "receipt");
    REQUIRE(spooler.waitIdle(5000));
    const QString labels = readFile(dir.filePath(u"labels.zpl"_s));
    CHECK(labels.count(u"^XA"_s) == 2);
    CHECK(labels.contains(u"^FDSalsa^FS"_s));
    CHECK(readFile(dir.filePath(u"receipt.txt"_s)).contains(u"Rice"_s));
}

TEST_CASE("A French store prints in French; a Chinese store's tickets are in English", "[print][i18n]")
{
    for (const char *lang : {"fr", "zh"}) {
        QTemporaryDir dir;
        auto seed = test::seedPosData();
        seed.settings.language = lang;
        PrintSpooler spooler;
        TicketPrinter printer(spooler, dir.path());
        core::Check c = burgerCheck(seed.settings, seed.menu);
        c.type = core::CheckType::Takeout;
        printer.printKitchen(seed.settings, c, {c.lines[0]}, true);
        REQUIRE(spooler.waitIdle(5000));
        const QString kitchen = readFile(dir.filePath(u"kitchen.txt"_s));
        INFO(lang);
        if (std::string(lang) == "fr")
            CHECK(kitchen.contains(u"*** ANNULÉ ***"_s));
        else
            CHECK(kitchen.contains(u"*** VOID ***"_s));
    }
}

TEST_CASE("A Spanish store prints in Spanish: receipts, kitchen tickets, reports", "[print][i18n]")
{
    QTemporaryDir dir;
    auto seed = test::seedPosData();
    seed.settings.language = "es";
    PrintSpooler spooler;
    TicketPrinter printer(spooler, dir.path());
    core::Check c = burgerCheck(seed.settings, seed.menu);
    c.type = core::CheckType::Takeout;
    c.rush = true;
    c.customer.note = "puerta de atrás";
    c.lines[0].seat = 2;

    printer.printKitchen(seed.settings, c, c.lines, false);
    printer.printKitchen(seed.settings, c, {c.lines[0]}, true);
    printer.printReceipt(seed.settings, c, "receipt");
    core::Report rep;
    rep.title = "Sales Summary";
    rep.line({"Net sales", "$34.50"});
    rep.line({"Voided items (3)", "$4.00"});
    printer.printReport(seed.settings, rep, "receipt");
    REQUIRE(spooler.waitIdle(5000));

    const QString kitchen = readFile(dir.filePath(u"kitchen.txt"_s));
    const QString receiptText = readFile(dir.filePath(u"receipt.txt"_s));
    CHECK(kitchen.contains(u"*** URGENTE ***"_s));
    CHECK(kitchen.contains(u"PARA LLEVAR"_s));
    CHECK(kitchen.contains(u"NOTA: puerta de atrás"_s));
    CHECK(kitchen.contains(u"A2 1 Classic Burger"_s));       // the seat: asiento
    CHECK(kitchen.contains(u"*** ANULADO ***"_s));
    CHECK_FALSE(kitchen.contains(u"RUSH"_s));
    CHECK(receiptText.contains(u"Cuenta #42"_s));
    CHECK(receiptText.contains(u"Mesero: Sam"_s));
    CHECK(receiptText.contains(u"Impuesto de comida"_s));
    CHECK(receiptText.contains(u"Cambio"_s));
    CHECK_FALSE(receiptText.contains(u"Subtotal "_s + u"Food"_s));
    CHECK(receiptText.contains(u"Resumen de ventas"_s));
    CHECK(receiptText.contains(u"Ventas netas"_s));
    CHECK(receiptText.contains(u"(3)"_s));
    CHECK_FALSE(receiptText.contains(u"Voided items"_s));
    // The store's own words (items, the footer) print as typed.
    CHECK(receiptText.contains(u"Classic Burger"_s));
}

TEST_CASE("Network printer receives ESC/POS over TCP", "[print]")
{
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost));
    QByteArray received;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        QTcpSocket *s = server.nextPendingConnection();
        QObject::connect(s, &QTcpSocket::readyRead, [s, &received] { received += s->readAll(); });
    });

    core::PrinterConfig p;
    p.id = "kitchen";
    p.name = "Line";
    p.type = "network";
    p.host = "127.0.0.1";
    p.port = server.serverPort();
    PrintSpooler spooler;
    QSignalSpy printed(&spooler, &PrintSpooler::jobPrinted);
    spooler.submit(p, QByteArray::fromStdString(renderEscPos(Document().text("Hello kitchen"), 42)), u"test"_s);
    REQUIRE(spooler.waitIdle(5000));
    QElapsedTimer t;
    t.start();
    while (!received.contains("Hello kitchen") && t.elapsed() < 3000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK(received.startsWith("\x1b@"));
    CHECK(received.contains("Hello kitchen"));
    CHECK(printed.size() == 1);
}

TEST_CASE("A dead printer never blocks the caller and is reported", "[print]")
{
    // A port nothing listens on: connection refused, retried, then reported.
    QTcpServer probe;
    REQUIRE(probe.listen(QHostAddress::LocalHost));
    const quint16 port = probe.serverPort();
    probe.close();

    core::PrinterConfig p;
    p.id = "kitchen";
    p.name = "Dead";
    p.type = "network";
    p.host = "127.0.0.1";
    p.port = port;
    PrintSpooler spooler;
    spooler.setTimings(300, 50);
    QSignalSpy failed(&spooler, &PrintSpooler::jobFailed);

    QElapsedTimer t;
    t.start();
    for (int i = 0; i < 20; ++i)
        spooler.submit(p, "ticket", u"ticket %1"_s.arg(i));
    CHECK(t.elapsed() < 100);   // queuing is instant

    // A file printer keeps working meanwhile.
    QTemporaryDir dir;
    core::PrinterConfig f;
    f.id = "receipt";
    f.name = "File";
    f.type = "file";
    f.path = dir.filePath(u"out.txt"_s).toStdString();
    spooler.submit(f, "receipt\n", u"receipt"_s);

    REQUIRE(spooler.waitIdle(20000));
    CHECK(failed.size() == 20);
    CHECK(failed.first()[0].toString() == u"Dead"_s);
    CHECK(readFile(dir.filePath(u"out.txt"_s)) == u"receipt\n"_s);
}

#ifdef Q_OS_UNIX
TEST_CASE("A USB printer in error: never blocks, two tries, then kept for Reprint", "[print][device]")
{
    // A pipe stands in for /dev/usb/lp0: nobody reading = the printer won't take data.
    QTemporaryDir dir;
    const QString fifo = dir.filePath(u"lp0"_s);
    REQUIRE(::mkfifo(QFile::encodeName(fifo).constData(), 0600) == 0);
    CHECK(PrintSpooler::isDevice(fifo));
    CHECK(PrintSpooler::isDevice(u"/dev/usb/lp9"_s));   // unplugged: still a device
    CHECK_FALSE(PrintSpooler::isDevice(dir.filePath(u"out.txt"_s)));

    core::PrinterConfig p;
    p.id = "receipt";
    p.name = "Epson";
    p.type = "file";
    p.path = fifo.toStdString();
    PrintSpooler spooler;
    spooler.setTimings(300, 50, 300, 200);   // write time 0.3 s, wait 0.2 s after a failure
    QSignalSpy printed(&spooler, &PrintSpooler::jobPrinted);
    QSignalSpy failed(&spooler, &PrintSpooler::jobFailed);
    QSignalSpy errors(&spooler, &PrintSpooler::printerError);

    // Not taking data: tried twice (with a wait between), then kept and the printer is in error.
    QElapsedTimer t;
    t.start();
    spooler.submit(p, "ticket 1\n", u"Receipt 1"_s);
    CHECK(t.elapsed() < 100);   // queuing is instant
    REQUIRE(spooler.waitIdle(5000));
    CHECK(t.elapsed() >= 190);
    REQUIRE(failed.size() == 1);
    CHECK(failed[0][1].toString() == u"Receipt 1"_s);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0][0].toString() == u"receipt"_s);
    CHECK(errors[0][1].toString() == u"Epson"_s);
    CHECK(errors[0][2].toBool());
    CHECK(spooler.held(u"receipt"_s) == 1);

    // A new ticket while it's in error: one try (after the wait), then kept too; nothing is re-sent on its own.
    spooler.submit(p, "ticket 2\n", u"Receipt 2"_s);
    REQUIRE(spooler.waitIdle(5000));
    CHECK(failed.size() == 2);
    CHECK(errors.size() == 1);
    CHECK(spooler.held(u"receipt"_s) == 2);
    CHECK(printed.isEmpty());

    // Fixed (someone reads the pipe), then Reprint: both go, in order, and the error clears.
    const int rd = ::open(QFile::encodeName(fifo).constData(), O_RDONLY | O_NONBLOCK);
    REQUIRE(rd >= 0);
    spooler.retryHeld(u"receipt"_s);
    REQUIRE(spooler.waitIdle(5000));
    CHECK(printed.size() == 2);
    CHECK(spooler.held(u"receipt"_s) == 0);
    REQUIRE(errors.size() == 2);
    CHECK_FALSE(errors[1][2].toBool());
    char buf[256];
    const ssize_t n = ::read(rd, buf, sizeof buf);
    CHECK(QByteArray(buf, int(std::max<ssize_t>(n, 0))) == "ticket 1\nticket 2\n");

    // Stuck mid-ticket (the printer stops taking data: the pipe fills up): given up after the write time,
    // the device closed, never a frozen print thread.
    const QByteArray big(1 << 20, 'x');
    t.restart();
    spooler.submit(p, big, u"Big"_s);
    CHECK(t.elapsed() < 100);
    REQUIRE(spooler.waitIdle(5000));
    CHECK(t.elapsed() >= 500);   // two tries of 0.3 s
    CHECK(failed.size() == 3);
    CHECK(spooler.held(u"receipt"_s) == 1);
    REQUIRE(errors.size() == 3);
    CHECK(errors[2][2].toBool());
    ::close(rd);

    // A plain file printer beside it is never held up by it.
    core::PrinterConfig f;
    f.id = "kitchen";
    f.name = "File";
    f.type = "file";
    f.path = dir.filePath(u"out.txt"_s).toStdString();
    spooler.submit(f, "kitchen\n", u"kitchen"_s);
    REQUIRE(spooler.waitIdle(5000));
    CHECK(readFile(dir.filePath(u"out.txt"_s)) == u"kitchen\n"_s);
}
#endif

TEST_CASE("Logo: a picture becomes black-and-white dots, fitted to the paper", "[print][logo]")
{
    // Left half black, right half white, the bottom row transparent.
    QImage img(100, 50, QImage::Format_ARGB32);
    img.fill(Qt::white);
    for (int y = 0; y < 50; ++y)
        for (int x = 0; x < 50; ++x)
            img.setPixel(x, y, qRgb(0, 0, 0));
    for (int x = 0; x < 100; ++x)
        img.setPixel(x, 49, qRgba(0, 0, 0, 0));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    const auto r = print::rasterize(png, 400, 200);
    REQUIRE(r);
    CHECK(r->width == 200);                       // at most twice its size
    CHECK(r->height == 100);
    CHECK(r->bits.size() == std::size_t(r->rowBytes() * r->height));
    CHECK(r->dot(10, 10));                        // black stays black
    CHECK_FALSE(r->dot(190, 10));                 // white stays paper
    CHECK_FALSE(r->dot(150, 99));                 // transparent is paper
    const auto small = print::rasterize(png, 60, 200);
    REQUIRE(small);
    CHECK(small->width == 60);                    // fitted
    CHECK(small->height == 30);
    CHECK_FALSE(print::rasterize("not a picture", 100, 100));
}

TEST_CASE("Logo: printed at the top of receipts on ESC/POS printers, as GS v 0", "[print][logo]")
{
    QImage img(64, 32, QImage::Format_RGB32);
    img.fill(Qt::black);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    // Bytes on paper: GS v 0, then width in bytes and height in rows (little-endian).
    print::Document d;
    d.image(print::rasterize(png, 64, 32));
    d.center("Cafe");
    d.cut = false;
    const std::string bytes = print::renderEscPos(d, 42);
    const std::string head = std::string("\x1dv0\0", 4) + char(8) + char(0) + char(32) + char(0);
    REQUIRE(bytes.find(head) != std::string::npos);
    CHECK(bytes.find(std::string(8 * 32, '\xff')) != std::string::npos);   // all black
    CHECK(print::renderText(d, 42).find("Cafe") != std::string::npos);      // plain text: no picture

    QTemporaryDir dir;
    auto seed = test::seedPosData();
    seed.settings.displayLogo = "store:logo.png";
    seed.settings.receiptLogo = true;
    for (core::PrinterConfig &p : seed.settings.printers)
        if (p.id == "receipt")
            p.format = "escpos";
    PrintSpooler spooler;
    TicketPrinter printer(spooler, dir.path());
    printer.setImageSource([&](const QString &ref) { return ref == u"store:logo.png"_s ? png : QByteArray(); });
    const core::Check c = burgerCheck(seed.settings, seed.menu);
    printer.printReceipt(seed.settings, c, "receipt");
    REQUIRE(spooler.waitIdle(5000));
    QFile out(dir.filePath(u"receipt.txt"_s));
    REQUIRE(out.open(QIODevice::ReadOnly));
    const QByteArray printed = out.readAll();
    CHECK(printed.indexOf(QByteArray("\x1dv0\0", 4)) >= 0);
    CHECK(printed.indexOf(QByteArray("\x1dv0\0", 4)) < printed.indexOf("TOTAL"));   // at the top

    // Off: no picture.
    seed.settings.receiptLogo = false;
    out.close();
    QFile::remove(dir.filePath(u"receipt.txt"_s));
    printer.printReceipt(seed.settings, c, "receipt");
    REQUIRE(spooler.waitIdle(5000));
    REQUIRE(out.open(QIODevice::ReadOnly));
    CHECK(out.readAll().indexOf(QByteArray("\x1dv0", 3)) < 0);
}

TEST_CASE("A printer's test page: logo, text, the width ruler, a cut, the drawer", "[print][testpage]")
{
    QTemporaryDir dir;
    auto seed = test::seedPosData();
    seed.settings.displayLogo = "store:logo.png";
    for (core::PrinterConfig &p : seed.settings.printers)
        if (p.id == "receipt") {
            p.format = "escpos";
            p.drawerKick = true;
            p.cutter = true;
        }
    QImage img(64, 32, QImage::Format_RGB32);
    img.fill(Qt::black);
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    PrintSpooler spooler;
    TicketPrinter printer(spooler, dir.path());
    printer.setImageSource([&](const QString &ref) { return ref == u"store:logo.png"_s ? png : QByteArray(); });
    CHECK_FALSE(printer.printTestPage(seed.settings, "no-such-printer", false));
    REQUIRE(printer.printTestPage(seed.settings, "receipt", true));
    REQUIRE(spooler.waitIdle(5000));
    QFile out(dir.filePath(u"receipt.txt"_s));
    REQUIRE(out.open(QIODevice::ReadOnly));
    const QByteArray printed = out.readAll();
    CHECK(printed.contains("Printer test"));
    CHECK(printed.contains("characters per line"));
    CHECK(printed.indexOf(QByteArray("\x1dv0\0", 4)) >= 0);       // the logo
    CHECK(printed.contains(QByteArray("\x1bp\0", 3)));             // drawer kick
    CHECK(printed.contains(QByteArray("\x1dV\x42", 3)));           // cut
}

// A real printer on the network (hidden; run with the printer's address:
// VTM_PRINTER_HOST=192.168.1.101 vtm_tests "[printerlive]"). Prints the
// Test Print page: logo, text sizes, a sample item and total, the ruler.
TEST_CASE("A network printer, live: the Test Print page", "[.][printerlive]")
{
    const QString host = qEnvironmentVariable("VTM_PRINTER_HOST");
    if (host.isEmpty())
        SKIP("Set VTM_PRINTER_HOST to the printer's address.");
    core::PosSettings settings;
    settings.storeName = "ViewTouch";
    core::PrinterConfig p;
    p.id = "receipt";
    p.name = "Receipt";
    p.type = "network";
    p.host = host.toStdString();
    p.port = qEnvironmentVariableIntValue("VTM_PRINTER_PORT") ? qEnvironmentVariableIntValue("VTM_PRINTER_PORT") : 9100;
    p.width = qEnvironmentVariableIntValue("VTM_PRINTER_WIDTH") ? qEnvironmentVariableIntValue("VTM_PRINTER_WIDTH") : 42;
    settings.printers = {p};
    print::PrintSpooler spooler;
    print::TicketPrinter printer(spooler, QDir::tempPath());
    QString result;
    QObject::connect(&spooler, &print::PrintSpooler::jobPrinted, [&](const QString &, const QString &) { result = u"printed"_s; });
    QObject::connect(&spooler, &print::PrintSpooler::jobFailed,
                     [&](const QString &, const QString &, const QString &error) { result = u"failed: "_s + error; });
    REQUIRE(printer.printTestPage(settings, "receipt", false));
    for (int i = 0; i < 300 && result.isEmpty(); ++i)
        QTest::qWait(50);
    INFO(result.toStdString());
    CHECK(result == u"printed"_s);
}

namespace {

// The text rows of ESC/POS bytes: commands taken out, one entry a line.
std::vector<std::string> escPosRows(const std::string &bytes)
{
    std::vector<std::string> rows;
    std::string row;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const char c = bytes[i];
        if (c == '\x1b' || c == '\x1d') {
            const char cmd = i + 1 < bytes.size() ? bytes[i + 1] : 0;
            if (cmd == '@') { i += 1; continue; }
            if (c == '\x1d' && cmd == 'v') {   // a picture: GS v 0 m xL xH yL yH data
                const std::size_t w = std::size_t((unsigned char)bytes[i + 4] | (unsigned char)bytes[i + 5] << 8);
                const std::size_t h = std::size_t((unsigned char)bytes[i + 6] | (unsigned char)bytes[i + 7] << 8);
                i += 7 + w * h;
                continue;
            }
            if (cmd == 'p') { i += 4; continue; }
            if (cmd == 'V') { i += 3; continue; }
            i += 2;   // ESC/GS x n
            continue;
        }
        if (c == '\n') {
            rows.push_back(row);
            row.clear();
        } else {
            row += c;
        }
    }
    return rows;
}

core::Check awkwardCheck()
{
    core::Check c;
    c.id = 3465;
    c.label = "Patio 12 — the long table by the fountain";
    c.serverName = "Maria-José Fernández de la Peña";
    c.guests = 12;
    c.customer.name = "Ñoño “The Regular” O’Brien";
    c.customer.phone = "(555) 123-4567";
    c.customer.address = "1234 Avenida de los Insurgentes Sur\nApt 5B\tGate code #4321\x1b@";
    c.customer.note = "Leave at the door 🍕🚪 — ring twice…";
    core::OrderLine l;
    l.id = 1;
    l.itemId = "burger";
    l.name = "Super Deluxe Triple Bacon Cheeseburger with Jalapeños and Extra Everything";
    l.unitPrice = Money::fromCents(123456);
    l.quantity = 12;
    core::Modifier free;
    free.name = "No onion";
    core::Modifier extra;
    extra.name = "Extra bacon";
    extra.unitPrice = Money::fromCents(250);
    core::Modifier temp;
    temp.name = "Medium rare";
    l.modifiers = {free, extra, temp};
    c.lines.push_back(l);
    core::OrderLine crepe;
    crepe.id = 2;
    crepe.itemId = "creme";
    crepe.name = "Crème brûlée ½ · café";
    crepe.unitPrice = Money::fromCents(899);
    crepe.quantity = 1;
    c.lines.push_back(crepe);
    core::Payment pay;
    pay.id = 1;
    pay.kind = core::TenderKind::Card;
    pay.tenderName = "Credit Card";
    pay.amount = Money::fromCents(1600000);
    pay.tip = Money::fromCents(250000);
    pay.cardBrand = "mastercard";
    pay.last4 = "4242";
    pay.processor = "stripe";
    pay.reference = "pi_3PqLongStripeReferenceThatIsLongerThanAnyPaperWidth";
    c.payments.push_back(pay);
    return c;
}

} // namespace

TEST_CASE("Every printout fits the paper and stays readable", "[print][fit]")
{
    auto seed = test::seedPosData();
    seed.settings.storeName = "La Taquería del Barrio — Since 1987";
    seed.settings.receiptHeader = "123 Main St\nSpringfield ☎ 555-0100";
    seed.settings.receiptFooter = "¡Gracias! Vuelva pronto 😊";
    seed.settings.receiptCustomer = "full";   // the customer's name, phone and address on the receipt too
    const core::Check c = awkwardCheck();
    core::Report report;
    report.title = "Server Sales (comparison)";
    report.subtitle = "This week vs. last week";
    report.rows.push_back({core::ReportRow::Kind::Section, {"Servers"}});
    report.rows.push_back({core::ReportRow::Kind::Line, {"Maria-José Fernández", "$12,345.67", "$11,000.00", "+12.2%", "142"}});
    report.rows.push_back({core::ReportRow::Kind::Total, {"Total", "$123,456.78", "$110,000.00", "+12.2%"}});
    report.rows.push_back({core::ReportRow::Kind::Note, {"A note that is longer than any paper is wide, so it must wrap neatly."}});
    for (const char *currency : {"$", "€", "£"}) {
        seed.settings.currencySymbol = currency;
        for (const int width : {32, 42, 48}) {
            const auto context = ctx(seed.settings);
            const std::vector<Document> docs = {receipt(c, context), kitchenTicket(c, c.lines, "Kitchen", false, context),
                                                kitchenTicket(c, c.lines, "Bar", true, context), reportTicket(report, context)};
            for (const Document &d : docs) {
                const std::string text = renderText(d, width);
                std::istringstream in(text);
                for (std::string row; std::getline(in, row);) {
                    INFO(width << " text: " << row);
                    CHECK(displayWidth(row) <= std::size_t(width));
                    CHECK(row.find('\x1b') == std::string::npos);
                    CHECK(row.find('\t') == std::string::npos);
                    CHECK(row.find('?') == std::string::npos);
                }
                for (const bool accents : {true, false}) {
                    for (const std::string &row : escPosRows(renderEscPos(d, width, accents))) {
                        INFO(width << (accents ? " PC858: " : " plain: ") << row);
                        // One byte, one column; rows in the smaller Font B (a choice, "Opened ...") fit 4/3 as
                        // many across the same paper (print/document.cpp renderEscPos).
                        CHECK(row.size() <= std::size_t(width) * 4 / 3);
                        CHECK(row.find('?') == std::string::npos);
                        if (!accents)
                            CHECK(std::ranges::none_of(row, [](char ch) { return static_cast<unsigned char>(ch) >= 0x80; }));
                    }
                }
            }
        }
    }
    // Readable: the currency and the accents survive, the address keeps its lines.
    seed.settings.currencySymbol = "€";
    const std::string r = renderText(receipt(c, ctx(seed.settings)), 42);
    CHECK(contains(r, "€"));
    CHECK(contains(r, "Crème brûlée ½ · café"));
    CHECK(contains(r, "\nApt 5B Gate code #4321"));   // its own row; the tab and ESC gone
    CHECK_FALSE(contains(r, "\n \n"));                // no row left with only an indent
    CHECK(contains(r, "Ñoño \"The Regular\" O'Brien"));
    CHECK(contains(renderEscPos(receipt(c, ctx(seed.settings)), 42, false), "EUR"));
}

TEST_CASE("Receipts print every choice, free ones too (the store's rule); the kitchen gets them all", "[print][fit]")
{
    auto seed = test::seedPosData();
    const core::Check c = awkwardCheck();
    std::string r = renderText(receipt(c, ctx(seed.settings)), 42);
    CHECK(contains(r, "Extra bacon"));
    CHECK(contains(r, "No onion"));
    CHECK(contains(r, "Medium rare"));
    const std::string k = renderText(kitchenTicket(c, c.lines, "Kitchen", false, ctx(seed.settings)), 42);
    CHECK(contains(k, "No onion"));
    CHECK(contains(k, "Medium rare"));
    CHECK(contains(k, "Extra bacon"));
    seed.settings.receiptFreeChoices = true;   // Store Settings: shown
    r = renderText(receipt(c, ctx(seed.settings)), 42);
    CHECK(contains(r, "No onion"));
    CHECK(contains(r, "Medium rare"));
}

TEST_CASE("Every kind of space prints as a space (the time before AM/PM)", "[print][fit]")
{
    Document d;
    d.text("11:23 PM");                            // how Qt writes the time
    d.text("a b c d　e");            // no-break, thin, figure, ideographic
    for (const bool accents : {true, false}) {
        const std::string bytes = renderEscPos(d, 42, accents);
        CHECK(contains(bytes, "11:23 PM"));
        CHECK(contains(bytes, "a b c d e"));
        CHECK_FALSE(contains(bytes, "?"));
    }
    // The real clock, as tickets print it.
    Document now;
    now.text(QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat).toStdString());
    CHECK_FALSE(contains(renderEscPos(now, 42), "?"));
}

// --- out of paper, cover open, not answering --------------------------------------------

#include "print/printer_monitor.hh"

TEST_CASE("Printer status: what the ESC/POS answers mean", "[print][status]")
{
    const auto b = [](unsigned char c) { return QByteArray(1, char(c)); };
    CHECK(problemFromStatus(b(0x12), b(0x12)).isEmpty());          // all fine
    CHECK(problemFromStatus(b(0x16), b(0x12)) == u"coverOpen"_s);   // bit 2 of DLE EOT 2
    CHECK(problemFromStatus(b(0x32), b(0x12)) == u"paperOut"_s);    // stopped at the end of the paper
    CHECK(problemFromStatus(b(0x12), b(0x72)) == u"paperOut"_s);    // the paper sensor: out
    CHECK(problemFromStatus(b(0x52), b(0x12)) == u"error"_s);       // a jam, the cutter
    CHECK(problemFromStatus(b(0x12), b(0x1E)) == u"paperLow"_s);    // near the end of the roll
    CHECK(problemFromStatus(b(0x36), b(0x7E)) == u"coverOpen"_s);   // the cover first: that's what to fix
    CHECK(problemFromStatus({}, {}).isEmpty());                     // a printer that doesn't say
    CHECK(problemFromStatus(b(0xFF), b(0xFF)).isEmpty());           // not a status byte: ignored
}

namespace {

// A network printer that answers DLE EOT 2 and 4 with what the test sets.
struct FakeStatusPrinter {
    QTcpServer server;
    unsigned char offline = 0x12, paper = 0x12;
    bool quiet = false;   // takes the connection, says nothing (stuck)
    int connections = 0;
    FakeStatusPrinter()
    {
        REQUIRE(server.listen(QHostAddress::LocalHost));
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                ++connections;
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    const QByteArray in = s->readAll();
                    if (quiet)
                        return;
                    for (int i = 0; i + 2 < in.size(); ++i)
                        if (in[i] == 0x10 && in[i + 1] == 0x04)
                            s->write(QByteArray(1, char(in[i + 2] == 2 ? offline : paper)));
                });
                QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    core::PrinterConfig config() const
    {
        core::PrinterConfig p;
        p.id = "kitchen";
        p.name = "Kitchen";
        p.type = "network";
        p.host = "127.0.0.1";
        p.port = server.serverPort();
        return p;
    }
};

} // namespace

TEST_CASE("Printer status: out of paper is seen, then fixed, then unplugged", "[print][status]")
{
    FakeStatusPrinter printer;
    PrinterMonitor monitor;
    monitor.setTimings(150, 400);
    QStringList seen;
    QObject::connect(&monitor, &PrinterMonitor::statusChanged,
                     [&](const QString &id, const QString &problem) { seen << id + u'=' + problem; });
    const auto waitFor = [&](const QString &what) {
        for (int i = 0; i < 100 && !seen.contains(what); ++i)
            QTest::qWait(20);
        return seen.contains(what);
    };
    monitor.setPrinters({printer.config()});
    QTest::qWait(400);
    CHECK(seen.isEmpty());                                // fine: nothing to say
    printer.paper = 0x72;                                 // the roll runs out
    REQUIRE(waitFor(u"kitchen=paperOut"_s));
    printer.paper = 0x12;                                 // a new roll
    REQUIRE(waitFor(u"kitchen="_s));
    printer.offline = 0x16;
    REQUIRE(waitFor(u"kitchen=coverOpen"_s));
    printer.offline = 0x12;
    seen.clear();
    REQUIRE(waitFor(u"kitchen="_s));
    printer.server.close();                               // unplugged
    REQUIRE(waitFor(u"kitchen=offline"_s));
}

TEST_CASE("Printer status: never while a ticket is on its way; not for printers turned off", "[print][status]")
{
    FakeStatusPrinter printer;
    PrinterMonitor monitor;
    std::atomic<bool> busy = true;
    monitor.setBusy([&](const QString &, int) { return busy.load(); });
    monitor.setTimings(100, 300);
    monitor.setPrinters({printer.config()});
    QTest::qWait(500);
    CHECK(printer.connections == 0);   // a ticket is going: it waits
    busy = false;
    for (int i = 0; i < 50 && printer.connections == 0; ++i)
        QTest::qWait(20);
    CHECK(printer.connections > 0);

    core::PrinterConfig off = printer.config();
    off.watch = false;                 // Manager -> Printers: don't warn
    monitor.setPrinters({off});
    QTest::qWait(100);
    const int before = printer.connections;
    QTest::qWait(400);
    CHECK(printer.connections == before);
}

// A real printer's status (hidden): VTM_PRINTER_HOST=192.168.1.101 vtm_tests "[printerstatuslive]"
TEST_CASE("A network printer, live: its status", "[.][printerstatuslive]")
{
    const QString host = qEnvironmentVariable("VTM_PRINTER_HOST");
    if (host.isEmpty())
        SKIP("Set VTM_PRINTER_HOST to the printer's address.");
    core::PrinterConfig p;
    p.id = "receipt";
    p.name = "Receipt";
    p.type = "network";
    p.host = host.toStdString();
    PrinterMonitor monitor;
    monitor.setTimings(1000, 1500);
    QString last = u"(fine)"_s;
    QObject::connect(&monitor, &PrinterMonitor::statusChanged, [&](const QString &, const QString &problem) {
        last = problem.isEmpty() ? u"(fine)"_s : problem;
    });
    monitor.setPrinters({p});
    QTest::qWait(5000);
    WARN("Printer says: " << last.toStdString());
}

TEST_CASE("Printer status: one that stops responding while still connected", "[print][status][silent]")
{
    FakeStatusPrinter printer;
    PrinterMonitor monitor;
    monitor.setTimings(150, 200);
    QStringList seen;
    QObject::connect(&monitor, &PrinterMonitor::statusChanged,
                     [&](const QString &id, const QString &problem) { seen << id + u'=' + problem; });
    const auto waitFor = [&](const QString &what) {
        for (int i = 0; i < 150 && !seen.contains(what); ++i)
            QTest::qWait(20);
        return seen.contains(what);
    };
    // A printer that never says: nothing to warn about.
    printer.quiet = true;
    monitor.setPrinters({printer.config()});
    QTest::qWait(1200);
    CHECK_FALSE(seen.contains(u"kitchen=silent"_s));
    // One that has said, then stops: warned; answering again: cleared.
    printer.quiet = false;
    for (int i = 0; i < 50 && printer.connections < 8; ++i)
        QTest::qWait(20);
    printer.quiet = true;
    REQUIRE(waitFor(u"kitchen=silent"_s));
    printer.quiet = false;
    seen.clear();
    REQUIRE(waitFor(u"kitchen="_s));
}

TEST_CASE("Printer status: paper low, then no connections: probably out of paper", "[print][status][offlinepaper]")
{
    FakeStatusPrinter printer;
    PrinterMonitor monitor;
    monitor.setTimings(150, 300);
    QStringList seen;
    QObject::connect(&monitor, &PrinterMonitor::statusChanged,
                     [&](const QString &id, const QString &problem) { seen << id + u'=' + problem; });
    const auto waitFor = [&](const QString &what) {
        for (int i = 0; i < 150 && !seen.contains(what); ++i)
            QTest::qWait(20);
        return seen.contains(what);
    };
    printer.paper = 0x1E;                 // near the end of the roll
    monitor.setPrinters({printer.config()});
    REQUIRE(waitFor(u"kitchen=paperLow"_s));
    printer.server.close();               // out: like an Epson, it takes no connections
    REQUIRE(waitFor(u"kitchen=offlinePaper"_s));
    CHECK_FALSE(seen.contains(u"kitchen=offline"_s));
}

TEST_CASE("Printer status: paper low, then printed to the end (connected, silent): probably out of paper", "[print][status][offlinepaper]")
{
    FakeStatusPrinter printer;
    PrinterMonitor monitor;
    monitor.setTimings(150, 200);
    QStringList seen;
    QObject::connect(&monitor, &PrinterMonitor::statusChanged,
                     [&](const QString &id, const QString &problem) { seen << id + u'=' + problem; });
    const auto waitFor = [&](const QString &what) {
        for (int i = 0; i < 150 && !seen.contains(what); ++i)
            QTest::qWait(20);
        return seen.contains(what);
    };
    printer.paper = 0x1E;
    monitor.setPrinters({printer.config()});
    REQUIRE(waitFor(u"kitchen=paperLow"_s));
    printer.quiet = true;                 // the roll printed to its end
    REQUIRE(waitFor(u"kitchen=offlinePaper"_s));
    CHECK_FALSE(seen.contains(u"kitchen=silent"_s));
    printer.quiet = false;                // a new roll
    printer.paper = 0x12;
    REQUIRE(waitFor(u"kitchen="_s));
}

TEST_CASE("Printer status: learned that it answers; silent from the start after a restart", "[print][status][learned]")
{
    FakeStatusPrinter printer;
    QStringList learned, seen;
    {
        PrinterMonitor monitor;
        monitor.setTimings(150, 200);
        QObject::connect(&monitor, &PrinterMonitor::answersStatus, [&](const QString &id) { learned << id; });
        monitor.setPrinters({printer.config()});
        for (int i = 0; i < 50 && learned.isEmpty(); ++i)
            QTest::qWait(20);
        QTest::qWait(400);
        CHECK(learned == QStringList{u"kitchen"_s});   // once
    }
    // The next day it's stuck before the screens start.
    printer.quiet = true;
    core::PrinterConfig known = printer.config();
    known.reportsStatus = true;
    PrinterMonitor monitor;
    monitor.setTimings(150, 200);
    QObject::connect(&monitor, &PrinterMonitor::statusChanged,
                     [&](const QString &id, const QString &problem) { seen << id + u'=' + problem; });
    monitor.setPrinters({known});
    for (int i = 0; i < 150 && !seen.contains(u"kitchen=silent"_s); ++i)
        QTest::qWait(20);
    CHECK(seen.contains(u"kitchen=silent"_s));
}

TEST_CASE("Receipts say what a service charge and a delivery fee are (fee laws)", "[print][law]")
{
    const auto seed = test::seedPosData();
    core::Check c = burgerCheck(seed.settings, seed.menu);
    c.gratuityBp = 1800;
    c.autoGratuity = true;
    core::OrderLine fee;
    fee.id = 99;
    fee.itemId = "fee:delivery";
    fee.name = "Delivery fee";
    fee.unitPrice = Money::fromCents(350);
    c.lines.push_back(fee);
    core::PosSettings s = seed.settings;
    s.gratuityMinGuests = 6;
    std::string r = renderText(receipt(c, ctx(s)), 42);
    CHECK(contains(r, "For 6+ guests; goes to the server."));
    CHECK(contains(r, "Store's fee, not the driver's tip."));
    s.deliveryFeeToDriver = true;
    c.autoGratuity = false;
    r = renderText(receipt(c, ctx(s)), 42);
    CHECK(contains(r, "Goes to the driver."));
    CHECK(contains(r, "Goes to the server."));
}
