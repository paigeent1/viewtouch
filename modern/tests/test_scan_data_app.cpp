// Tobacco scan data in the app (app/pos_scan_data.cpp): the item card, Store Settings kept in this
// computer's own file (never in the database), Fill in makers, Reports > Scan Data and Save File.
// Made-up store and items only.

#include <catch2/catch_test_macros.hpp>

#include "pos_fixture.hh"
#include "app/pos_json.hh"
#include "qt_catch.hh"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

namespace {

bool waitForScan(const PosService &pos)
{
    for (int i = 0; i < 500 && pos.rangeReport()[u"loading"_s].toBool(); ++i)
        QTest::qWait(20);
    return !pos.rangeReport()[u"loading"_s].toBool() && pos.rangeReport()[u"id"_s] == u"scandata"_s;
}

core::MenuItem *find(PosService &pos, const std::string &name)
{
    for (core::MenuItem &m : pos.shared()->menu)
        if (m.name == name)
            return &m;
    return nullptr;
}

QVariantMap storeSettings()
{
    return {{u"scanMode"_s, u"independent"_s}, {u"scanAccount"_s, u"246813"_s}, {u"scanAccountName"_s, u"TestTobacco"_s},
            {u"scanStoreName"_s, u"Test Tobacco Shop"_s}, {u"scanStreet"_s, u"1 Sample Road"_s},
            {u"scanCity"_s, u"Testville"_s}, {u"scanState"_s, u"ct"_s}, {u"scanZip4"_s, u"06999-1234"_s},
            {u"scanOptCigar"_s, true}, {u"scanCoupons"_s, u"Marl Coup\n\n"_s}};
}

} // namespace

TEST_CASE("Scan data: an item's maker, type and how it's sold on its card", "[scandata]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.saveMenuItemCard({{u"name"_s, u"EXAMPLE PACK"_s}, {u"price"_s, u"9.99"_s}, {u"family"_s, u"burgers"_s},
                                  {u"scanMaker"_s, u"pmusa"_s}, {u"scanCategory"_s, u"cig"_s},
                                  {u"scanUom"_s, u"Pack"_s}, {u"scanUnits"_s, u""_s}}));
    core::MenuItem *m = find(pos, "EXAMPLE PACK");
    REQUIRE(m);
    CHECK(m->scanMaker == "pmusa");
    CHECK(m->scanCategory == "cig");
    CHECK(m->scanUom == "pack");
    const core::MenuItem back = app::menuItemFromJson(app::toJson(*m));
    CHECK(back.scanMaker == "pmusa");
    CHECK(back.scanUom == "pack");
    CHECK_FALSE(pos.saveMenuItemCard({{u"id"_s, QString::fromStdString(m->id)}, {u"name"_s, u"EXAMPLE PACK"_s},
                                      {u"price"_s, u"9.99"_s}, {u"scanMaker"_s, u"acme"_s}}));
    CHECK_FALSE(pos.saveMenuItemCard({{u"id"_s, QString::fromStdString(m->id)}, {u"name"_s, u"EXAMPLE PACK"_s},
                                      {u"price"_s, u"9.99"_s}, {u"scanUnits"_s, u"ten"_s}}));
}

TEST_CASE("Scan data: Store Settings are this computer's own, never in the database", "[scandata]")
{
    QTemporaryDir dir;
    PosService pos(test::seedPosData(), nullptr);
    pos.shared()->scanDataFile = dir.filePath(u"scan-data-site.json"_s);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    QVariantMap s = pos.adminRecords(u"store"_s).first().toMap();
    CHECK(s[u"scanStatus"_s].toString().startsWith(u"Not set up"_s));
    const QVariantMap mine = storeSettings();
    for (auto it = mine.cbegin(); it != mine.cend(); ++it)
        s.insert(it.key(), it.value());
    REQUIRE(pos.adminSave(u"store"_s, 0, s));
    const core::scandata::Settings &now = pos.shared()->scanData;
    CHECK(now.accountNumber == "246813");
    CHECK(now.state == "CT");
    CHECK(now.optional == std::vector<std::string>{"cigar"});
    CHECK(now.couponNames == std::vector<std::string>{"Marl Coup"});
    CHECK(core::scandata::settingsProblems(now).empty());
    CHECK(pos.adminRecords(u"store"_s).first().toMap()[u"scanStatus"_s].toString().startsWith(u"Ready"_s));

    // In its own file, read back as it was; never in the store's settings (the database, the standby,
    // a copied store).
    REQUIRE(QFile::exists(pos.shared()->scanDataFile));
    app::PosShared other(app::PosData{}, nullptr);
    other.scanDataFile = pos.shared()->scanDataFile;
    REQUIRE(PosService::loadScanDataSettings(other));
    CHECK(other.scanData == now);
    const QByteArray db = QJsonDocument(app::toJson(pos.shared()->settings)).toJson();
    CHECK_FALSE(db.contains("246813"));
    CHECK_FALSE(db.contains("Sample Road"));
}

TEST_CASE("Scan data: Fill in makers by UPC prefix, keeping an item's own choice", "[scandata]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    auto &menu = pos.shared()->menu;
    REQUIRE(menu.size() >= 2);
    menu[0].barcodes = {"28200003843"};
    menu[1].barcodes = {"073100000102"};
    menu[1].scanMaker = "other";
    menu[1].scanCategory = "pipe";
    CHECK(pos.fillScanMakers(false) == 1);
    CHECK(menu[0].scanMaker == "pmusa");
    CHECK(menu[0].scanCategory == "cig");
    CHECK(menu[1].scanMaker == "other");
    CHECK(pos.fillScanMakers(false) == 0);
    CHECK(pos.fillScanMakers(true) == 1);
    CHECK(menu[1].scanMaker == "usstc");
    REQUIRE(pos.loginWithPin(u"1111"_s));
    CHECK(pos.fillScanMakers(true) == -1);   // managers only
}

TEST_CASE("Scan data: last week's file, refused until the store is set up", "[scandata]")
{
    QTemporaryDir dir;
    qputenv("VT_SCAN_DATA_DIR", dir.filePath(u"out"_s).toUtf8());
    PosService pos(test::seedPosData(), nullptr);
    core::MenuItem &pack = pos.shared()->menu.front();
    pack.barcodes = {"028200003843"};
    pack.scanMaker = "pmusa";
    pack.scanCategory = "cig";

    // A sale last Wednesday at 2 pm Eastern.
    const QTimeZone et(QByteArrayLiteral("America/New_York"));
    const QDate today = QDateTime::currentDateTime(et).date();
    const QDate saturday = today.addDays(-(today.dayOfWeek() % 7) - 1);
    core::Check c;
    c.id = 4242;
    c.type = core::CheckType::Quick;
    c.status = core::CheckStatus::Closed;
    c.closedAt = QDateTime(saturday.addDays(-3), QTime(14, 0), et).toMSecsSinceEpoch();
    c.openedAt = c.closedAt - 60000;
    c.addItem(pack).quantity = 2;
    c.ageVerified = 21;
    c.ageHow = "scan";
    c.ageDob = "1970-01-02";
    pos.shared()->history = [c](std::int64_t from, std::int64_t to) {
        return c.closedAt >= from && c.closedAt < to ? std::vector<core::Check>{c} : std::vector<core::Check>{};
    };

    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.requestScanData());
    REQUIRE(waitForScan(pos));
    CHECK(pos.rangeReport()[u"weekEnding"_s] == saturday.toString(u"yyyy-MM-dd"_s));
    CHECK(pos.rangeReport()[u"records"_s] == 1);
    CHECK(pos.rangeReport()[u"settingsReady"_s] == false);
    CHECK_FALSE(pos.saveScanData(true));   // never with the store unset, not even Save Anyway

    pos.shared()->scanData = PosService::scanDataFromJson(QJsonObject{
        {u"accountNumber"_s, u"246813"_s}, {u"accountName"_s, u"TestTobacco"_s}, {u"storeName"_s, u"Test Tobacco Shop"_s},
        {u"street"_s, u"1 Sample Road"_s}, {u"city"_s, u"Testville"_s}, {u"state"_s, u"CT"_s}, {u"zip4"_s, u"06999-1234"_s}});
    REQUIRE(pos.requestScanData(saturday.toString(u"yyyy-MM-dd"_s)));
    REQUIRE(waitForScan(pos));
    CHECK(pos.rangeReport()[u"errors"_s] == 0);
    REQUIRE(pos.saveScanData());
    const QString name = u"TestTobacco"_s + saturday.toString(u"yyyyMMdd"_s);
    QFile f(QDir(dir.filePath(u"out"_s)).filePath(name));
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QByteArray text = f.readAll();
    CHECK(text.startsWith("1|2|"));
    CHECK(text.contains("|4242|"));
    CHECK(text.contains("|Scanned ID|"));
    CHECK(text.contains("\r\n"));
    CHECK_FALSE(text.contains("1970"));
    CHECK_FALSE(pos.requestScanData(u"2026-01-07"_s));   // a Wednesday
    qunsetenv("VT_SCAN_DATA_DIR");
}

TEST_CASE("Scan data: vt-scan-data sends a week once, again only when asked", "[scandata][scripts]")
{
    QTemporaryDir dir;
    const QString out = dir.filePath(u"out"_s), conf = dir.filePath(u"scan-data.conf"_s);
    REQUIRE(QDir().mkpath(out));
    const auto write = [](const QString &path, const QByteArray &text) {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(text);
    };
    write(QDir(out).filePath(u"TestTobacco20260110"_s), "1|1|5.99|Test Tobacco Shop\r\n");
    write(conf, "SFTP_HOST=example.invalid\nSFTP_USER=someone\nSFTP_PASSWORD=not-real\n");
    QFile::setPermissions(conf, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto run = [&](const QStringList &args, QString *said = nullptr) {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"VT_SCAN_DATA_DIR"_s, out);
        env.insert(u"VT_SCAN_DATA_CONF"_s, conf);
        p.setProcessEnvironment(env);
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(u"bash"_s, QStringList{QStringLiteral(VTM_SCRIPTS_DIR "/vt-scan-data")} + args);
        REQUIRE(p.waitForFinished(30000));
        if (said)
            *said = QString::fromUtf8(p.readAll());
        return p.exitCode();
    };
    QString said;
    CHECK(run({u"send"_s, u"2026-01-07"_s}) == 2);                        // not a Saturday
    CHECK(run({u"send"_s, u"2026-01-03"_s}) == 1);                        // no file saved for it
    CHECK(run({u"send"_s, u"2026-01-10"_s, u"--dry-run"_s}, &said) == 0);
    CHECK(said.contains(u"TestTobacco20260110"_s));
    CHECK_FALSE(said.contains(u"not-real"_s));                             // never shows the password
    write(QDir(out).filePath(u"sent.log"_s), "2026-01-12 10:00:00\t20260110\tTestTobacco20260110\t31\tx\tsent\tfirst\n");
    CHECK(run({u"send"_s, u"2026-01-10"_s, u"--dry-run"_s}, &said) == 3);  // already sent
    CHECK(said.contains(u"--again"_s));
    CHECK(run({u"send"_s, u"2026-01-10"_s, u"--again"_s, u"--dry-run"_s}, &said) == 0);
    CHECK(said.contains(u"cap"_s));
    CHECK(run({u"status"_s}, &said) == 0);
    CHECK(said.contains(u"sent 1x"_s));
    QFile::setPermissions(conf, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther);
    CHECK(run({u"send"_s, u"2026-01-10"_s, u"--again"_s, u"--dry-run"_s}) == 1);   // a login others can read
}
