// PosService: tobacco scan data for Altria (core/scan_data.hh): the store's scan data settings (kept
// in a file on this computer only, never in the database: another store's Pi, a copy made with
// vt-copy-store or the standby never gets them), Fill in makers, and the weekly file (Reports > Scan
// Data: built from the week's checks, checked, saved to ~/ViewTouch/scan-data for vt-scan-data to send).

#include "app/i18n.hh"
#include "app/pos_admin_shared.hh"
#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>
#include <QTimeZone>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace sd = core::scandata;

namespace {

QJsonArray strings(const std::vector<std::string> &v)
{
    QJsonArray a;
    for (const std::string &s : v)
        a.append(qs(s));
    return a;
}

std::vector<std::string> strings(const QJsonValue &v)
{
    std::vector<std::string> out;
    for (const QJsonValue &x : v.toArray())
        if (!x.toString().trimmed().isEmpty())
            out.push_back(ss(x.toString().trimmed()));
    return out;
}

// "Marl Coup\nBOGO" -> its lines; and back.
std::vector<std::string> lines(const QString &text)
{
    std::vector<std::string> out;
    for (const QString &l : text.split(u'\n'))
        if (!l.trimmed().isEmpty())
            out.push_back(ss(l.trimmed()));
    return out;
}

QString joinLines(const std::vector<std::string> &v)
{
    QStringList l;
    for (const std::string &s : v)
        l << qs(s);
    return l.join(u'\n');
}

// The store's clock for scan data: US Eastern (the stores are in Connecticut); this computer's own if
// it has no time zone data.
QTimeZone storeZone()
{
    QTimeZone z(QByteArrayLiteral("America/New_York"));
    return z.isValid() ? z : QTimeZone(QTimeZone::LocalTime);
}

QString scanDataDir()
{
    return qEnvironmentVariable("VT_SCAN_DATA_DIR", QDir::homePath() + u"/ViewTouch/scan-data"_s);
}

const char *const kOptional[][2] = {
    {"cig", "scanOptCig"}, {"mst", "scanOptMst"}, {"cigar", "scanOptCigar"}, {"onp", "scanOptOnp"},
    {"vapor", "scanOptVapor"}, {"ryo", "scanOptRyo"}, {"pipe", "scanOptPipe"}, {"other", "scanOptOther"}};

} // namespace

QJsonObject PosService::scanDataJson(const sd::Settings &s)
{
    return {{u"mode"_s, qs(s.mode)}, {u"accountNumber"_s, qs(s.accountNumber)}, {u"storeNumber"_s, qs(s.storeNumber)},
            {u"accountName"_s, qs(s.accountName)}, {u"storeName"_s, qs(s.storeName)}, {u"street"_s, qs(s.street)},
            {u"city"_s, qs(s.city)}, {u"state"_s, qs(s.state)}, {u"zip4"_s, qs(s.zip4)}, {u"phone"_s, qs(s.phone)},
            {u"contact"_s, qs(s.contact)}, {u"email"_s, qs(s.email)}, {u"provider"_s, qs(s.provider)},
            {u"optional"_s, strings(s.optional)}, {u"encoding"_s, qs(s.encoding)}, {u"lineEnding"_s, qs(s.lineEnding)},
            {u"couponNames"_s, strings(s.couponNames)}, {u"multiNames"_s, strings(s.multiNames)},
            {u"otherMfrNames"_s, strings(s.otherMfrNames)}, {u"fillRules"_s, qs(s.fillRules)}};
}

sd::Settings PosService::scanDataFromJson(const QJsonObject &o)
{
    sd::Settings s;
    const auto text = [&](const char16_t *key, std::string &into) {
        if (o.contains(QStringView(key)))
            into = ss(o.value(QStringView(key)).toString().trimmed());
    };
    text(u"mode", s.mode);
    text(u"accountNumber", s.accountNumber);
    text(u"storeNumber", s.storeNumber);
    text(u"accountName", s.accountName);
    text(u"storeName", s.storeName);
    text(u"street", s.street);
    text(u"city", s.city);
    text(u"state", s.state);
    text(u"zip4", s.zip4);
    text(u"phone", s.phone);
    text(u"contact", s.contact);
    text(u"email", s.email);
    text(u"provider", s.provider);
    text(u"encoding", s.encoding);
    text(u"lineEnding", s.lineEnding);
    s.fillRules = ss(o.value(u"fillRules").toString());
    s.optional = strings(o.value(u"optional"));
    s.couponNames = strings(o.value(u"couponNames"));
    s.multiNames = strings(o.value(u"multiNames"));
    s.otherMfrNames = strings(o.value(u"otherMfrNames"));
    if (s.mode != "chain")
        s.mode = "independent";
    if (s.encoding != "utf8")
        s.encoding = "ascii";
    if (s.lineEnding != "lf")
        s.lineEnding = "crlf";
    return s;
}

bool PosService::loadScanDataSettings(PosShared &shared)
{
    if (shared.scanDataFile.isEmpty())
        return false;
    QFile f(shared.scanDataFile);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    shared.scanData = scanDataFromJson(QJsonDocument::fromJson(f.readAll()).object());
    return true;
}

bool PosService::writeScanDataSettings()
{
    if (s_->scanDataFile.isEmpty())
        return true;   // tests: in memory only
    QDir().mkpath(QFileInfo(s_->scanDataFile).absolutePath());
    QSaveFile out(s_->scanDataFile);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(QJsonDocument(scanDataJson(s_->scanData)).toJson());
    if (!out.commit())
        return false;
    QFile::setPermissions(s_->scanDataFile, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

// --- Store Settings > Tobacco scan data -------------------------------------------------------

QVariantList PosService::scanDataFields() const
{
    using admin::field;
    using admin::with;
    using admin::options;
    const QString confirm = tr("To confirm with the Scan Data Help Desk.");
    QVariantList f{
        field(u"scanStatus"_s, tr("Scan data status"), u"info"_s,
              tr("Kept on this computer only: each store sets up its own (never copied to another store's computer).")),
        with(field(u"scanMode"_s, tr("Altria account type"), u"enum"_s,
                   tr("Independent: a Retail Control Number. Chain: a Management Account Number and this store's Store Number.")),
             u"options"_s, options({{"independent", "Independent (Retail Control Number)"}, {"chain", "Chain (Management Account Number)"}})),
        field(u"scanAccount"_s, tr("Retail Control Number (or Management Account Number)"), u"string"_s,
              tr("From the Altria representative: 6 digits for an independent store, 5 for a chain.")),
        field(u"scanStoreNumber"_s, tr("Store Number (chain only)"), u"string"_s,
              tr("A chain's number for this store, as Altria has it. An independent store leaves it empty.")),
        field(u"scanAccountName"_s, tr("Account name for the file name"), u"string"_s,
              tr("Letters and digits only, the same every week (the file is this name and the week's Saturday).")),
        field(u"scanStoreName"_s, tr("Store name (as Altria has it)"), u"string"_s),
        field(u"scanStreet"_s, tr("Street address (as Altria has it)"), u"string"_s, tr("No PO Box.")),
        field(u"scanCity"_s, tr("City"), u"string"_s),
        field(u"scanState"_s, tr("State (2 letters)"), u"string"_s),
        field(u"scanZip4"_s, tr("ZIP+4"), u"string"_s, tr("All nine digits with the hyphen, like 06810-1234.")),
        field(u"scanPhone"_s, tr("Store phone (optional)"), u"string"_s),
        field(u"scanContact"_s, tr("Store contact (optional)"), u"string"_s),
        field(u"scanEmail"_s, tr("Contact e-mail (optional)"), u"string"_s),
        field(u"scanProvider"_s, tr("Summary line name (to confirm)"), u"string"_s,
              tr("The name at the end of the file's first line. Empty: the store name above.") + u" "_s + confirm),
        with(field(u"scanEncoding"_s, tr("File letters (to confirm)"), u"enum"_s, confirm),
             u"options"_s, options({{"ascii", "Plain ASCII"}, {"utf8", "UTF-8 (no BOM)"}})),
        with(field(u"scanLineEnding"_s, tr("Line endings (to confirm)"), u"enum"_s, confirm),
             u"options"_s, options({{"crlf", "CRLF (Windows)"}, {"lf", "LF"}})),
        field(u"scanOptCig"_s, tr("Also send other makers' cigarettes"), u"bool"_s,
              tr("Altria's own products are always sent. A type turned on here is sent complete: every sale of it.")),
        field(u"scanOptMst"_s, tr("Also send other makers' smokeless"), u"bool"_s),
        field(u"scanOptCigar"_s, tr("Also send other makers' cigars"), u"bool"_s),
        field(u"scanOptOnp"_s, tr("Also send other makers' nicotine pouches"), u"bool"_s),
        field(u"scanOptVapor"_s, tr("Also send other makers' vapor"), u"bool"_s),
        field(u"scanOptRyo"_s, tr("Also send roll-your-own tobacco"), u"bool"_s),
        field(u"scanOptPipe"_s, tr("Also send pipe tobacco"), u"bool"_s),
        field(u"scanOptOther"_s, tr("Also send other tobacco"), u"bool"_s),
        field(u"scanCoupons"_s, tr("Manufacturer coupons"), u"text"_s,
              tr("Discounts (their names, or a coupon's code) the manufacturer pays back, one per line. Every other discount is the store's own.")),
        field(u"scanMultiDeals"_s, tr("Manufacturer multi-unit deals"), u"text"_s,
              tr("Promotions or discounts that are a manufacturer's multi-pack deal (buy 2 save...), one per line.")),
        field(u"scanOtherMfr"_s, tr("Other manufacturer discounts"), u"text"_s,
              tr("Other discounts a manufacturer pays for, one per line.")),
        field(u"scanFillRules"_s, tr("Fill in makers: rules"), u"text"_s,
              tr("One per line: a UPC start, then maker and type (028200 pmusa cig), or dept:NAME then maker and type. "
                 "Makers: pmusa usstc jmc helix njoy other. Types: cig mst cigar onp vapor ryo pipe other cbd none. "
                 "Empty: the built-in rules.")),
    };
    return f;
}

QVariantMap PosService::scanDataValues() const
{
    const sd::Settings &s = s_->scanData;
    const std::vector<std::string> problems = sd::settingsProblems(s);
    QString status = problems.empty() ? tr("Ready. Send a test week to the Help Desk before it counts.")
                                      : tr("Not set up: %1").arg(qs(problems.front()));
    QVariantMap v{{u"scanStatus"_s, status}, {u"scanMode"_s, qs(s.mode)}, {u"scanAccount"_s, qs(s.accountNumber)},
                  {u"scanStoreNumber"_s, qs(s.storeNumber)}, {u"scanAccountName"_s, qs(s.accountName)},
                  {u"scanStoreName"_s, qs(s.storeName)}, {u"scanStreet"_s, qs(s.street)}, {u"scanCity"_s, qs(s.city)},
                  {u"scanState"_s, qs(s.state)}, {u"scanZip4"_s, qs(s.zip4)}, {u"scanPhone"_s, qs(s.phone)},
                  {u"scanContact"_s, qs(s.contact)}, {u"scanEmail"_s, qs(s.email)}, {u"scanProvider"_s, qs(s.provider)},
                  {u"scanEncoding"_s, qs(s.encoding)}, {u"scanLineEnding"_s, qs(s.lineEnding)},
                  {u"scanCoupons"_s, joinLines(s.couponNames)}, {u"scanMultiDeals"_s, joinLines(s.multiNames)},
                  {u"scanOtherMfr"_s, joinLines(s.otherMfrNames)},
                  {u"scanFillRules"_s, s.fillRules.empty() ? qs(sd::defaultRulesText()).trimmed() : qs(s.fillRules)}};
    for (const auto &[type, key] : kOptional)
        v.insert(QLatin1StringView(key), std::ranges::find(s.optional, std::string(type)) != s.optional.end());
    return v;
}

void PosService::saveScanDataRecord(const QVariantMap &record)
{
    sd::Settings s = s_->scanData;
    const auto text = [&](const QString &key, std::string &into) {
        if (record.contains(key))
            into = ss(record.value(key).toString().trimmed());
    };
    text(u"scanMode"_s, s.mode);
    text(u"scanAccount"_s, s.accountNumber);
    text(u"scanStoreNumber"_s, s.storeNumber);
    text(u"scanAccountName"_s, s.accountName);
    text(u"scanStoreName"_s, s.storeName);
    text(u"scanStreet"_s, s.street);
    text(u"scanCity"_s, s.city);
    text(u"scanState"_s, s.state);
    text(u"scanZip4"_s, s.zip4);
    text(u"scanPhone"_s, s.phone);
    text(u"scanContact"_s, s.contact);
    text(u"scanEmail"_s, s.email);
    text(u"scanProvider"_s, s.provider);
    text(u"scanEncoding"_s, s.encoding);
    text(u"scanLineEnding"_s, s.lineEnding);
    s.state = ss(qs(s.state).toUpper());
    if (record.contains(u"scanCoupons"_s))
        s.couponNames = lines(record.value(u"scanCoupons"_s).toString());
    if (record.contains(u"scanMultiDeals"_s))
        s.multiNames = lines(record.value(u"scanMultiDeals"_s).toString());
    if (record.contains(u"scanOtherMfr"_s))
        s.otherMfrNames = lines(record.value(u"scanOtherMfr"_s).toString());
    if (record.contains(u"scanFillRules"_s)) {
        const QString rules = record.value(u"scanFillRules"_s).toString().trimmed();
        s.fillRules = rules == qs(sd::defaultRulesText()).trimmed() ? std::string() : ss(rules);
    }
    for (const auto &[type, key] : kOptional) {
        const QString k = QString::fromLatin1(key);
        if (!record.contains(k))
            continue;
        std::erase(s.optional, std::string(type));
        if (record.value(k).toBool())
            s.optional.push_back(type);
    }
    s = scanDataFromJson(scanDataJson(s));   // known values only
    if (s == s_->scanData)
        return;
    s_->scanData = s;
    if (!writeScanDataSettings())
        emit notice(tr("The scan data settings couldn't be written to %1.").arg(s_->scanDataFile));
}

// --- Fill in makers ---------------------------------------------------------------------------

int PosService::fillScanMakers(bool overwrite)
{
    if (!require(perm::Manager, tr("Fill in makers")))
        return -1;
    const std::vector<sd::FillRule> rules =
        sd::parseRules(s_->scanData.fillRules.empty() ? sd::defaultRulesText() : s_->scanData.fillRules);
    if (rules.empty()) {
        fail(tr("There are no fill rules (Store Settings > Tobacco scan data)."));
        return -1;
    }
    // Only when something changes: one step to undo.
    std::vector<std::size_t> changed;
    {
        const std::vector<MenuCategory> &depts = s_->categories();
        for (std::size_t i = 0; i < s_->menu.size(); ++i) {
            MenuItem copy = s_->menu[i];
            if (sd::fillItem(copy, rules, depts, overwrite))
                changed.push_back(i);
        }
    }
    if (changed.empty()) {
        emit notice(tr("Fill in makers: nothing to fill."));
        return 0;
    }
    const MenuStep step(this, tr("Fill in makers"));
    const std::vector<MenuCategory> depts = s_->categories();
    for (std::size_t i : changed) {
        sd::fillItem(s_->menu[i], rules, depts, overwrite);
        if (s_->sink)
            s_->sink->saveMenuItem(s_->menu[i], int(i));
    }
    menuChanged();
    emit notice(tr("Fill in makers: %1 items filled.").arg(changed.size()));
    return int(changed.size());
}

// --- Reports > Scan Data ------------------------------------------------------------------------

bool PosService::requestScanData(const QString &weekEnding)
{
    if (!require(perm::Manager, tr("Reports")))
        return false;
    const QTimeZone zone = storeZone();
    std::string sat = ss(weekEnding.trimmed());
    std::erase(sat, '-');
    if (sat.empty())
        sat = sd::lastSaturday(ss(QDateTime::fromMSecsSinceEpoch(now(), zone).date().toString(u"yyyyMMdd"_s)));
    if (!sd::isSaturday(sat))
        return fail(tr("A scan data week ends on a Saturday."));
    const QDate saturday = QDate::fromString(qs(sat), u"yyyyMMdd"_s);
    const std::int64_t a = QDateTime(saturday.addDays(-6), QTime(0, 0), zone).toMSecsSinceEpoch();
    const std::int64_t b = QDateTime(saturday.addDays(1), QTime(0, 0), zone).toMSecsSinceEpoch();
    const QString label = tr("Week ending %1").arg(i18n::locale().toString(saturday, u"ddd MMM d, yyyy"_s));

    const int request = ++rangeRequest_;
    rangeReport_ = {{u"loading"_s, true}, {u"id"_s, u"scandata"_s}, {u"label"_s, label},
                    {u"weekEnding"_s, saturday.toString(u"yyyy-MM-dd"_s)}};
    scanFile_.clear();
    emit sessionChanged();

    auto history = s_->history;
    std::vector<Check> today_ = s_->closedToday;
    QPointer<PosService> self(this);
    QThreadPool::globalInstance()->start([=, today_ = std::move(today_)]() mutable {
        std::vector<Check> checks = history ? history(a, b) : std::vector<Check>{};
        for (Check &c : today_)
            if (c.closedAt >= a && c.closedAt < b && std::ranges::none_of(checks, [&](const Check &x) { return x.id == c.id; }))
                checks.push_back(std::move(c));
        QMetaObject::invokeMethod(self, [self, request, a, b, sat, label, zone, checks = std::move(checks)] {
            if (!self || request != self->rangeRequest_)
                return;
            sd::Options o;
            o.from = a;
            o.to = b;
            o.weekEnding = sat;
            o.date = [zone](std::int64_t ms) {
                return QDateTime::fromMSecsSinceEpoch(ms, zone).toString(u"yyyyMMdd"_s).toStdString();
            };
            o.time = [zone](std::int64_t ms) {
                return QDateTime::fromMSecsSinceEpoch(ms, zone).toString(u"HH:mm:ss"_s).toStdString();
            };
            const sd::Settings &settings = self->s_->scanData;
            const sd::Result r = sd::build(checks, self->s_->menu, self->s_->settings.promotions,
                                           self->s_->settings.tax, settings, o);
            const bool settingsReady = sd::settingsProblems(settings).empty();
            self->scanFile_ = QByteArray::fromStdString(r.text(settings));
            self->scanFileName_ = qs(r.fileName);
            self->scanFileErrors_ = r.hasErrors();
            self->scanSettingsReady_ = settingsReady;
            QVariantMap out = self->rangeReport_;
            out.insert(u"loading"_s, false);
            out.insert(u"report"_s, self->reportVariant(self->scanDataReport(r, label)));
            out.insert(u"fileName"_s, qs(r.fileName));
            out.insert(u"records"_s, int(r.records.size()));
            out.insert(u"errors"_s, int(std::ranges::count_if(r.issues, [](const sd::Issue &i) { return i.error; })));
            out.insert(u"warnings"_s, int(std::ranges::count_if(r.issues, [](const sd::Issue &i) { return !i.error; })));
            out.insert(u"settingsReady"_s, settingsReady);
            out.insert(u"folder"_s, scanDataDir());
            self->rangeReport_ = out;
            emit self->sessionChanged();
        }, Qt::QueuedConnection);
    });
    return true;
}

Report PosService::scanDataReport(const sd::Result &r, const QString &label) const
{
    Report out;
    out.id = "scandata";
    out.title = tr("Scan Data").toStdString() + " - " + ss(label);
    out.columns = {tr("Item").toStdString(), tr("UPC").toStdString(), tr("Qty").toStdString(),
                   tr("Discounts").toStdString(), tr("Final").toStdString()};
    const auto t = [](const QString &s) { return s.toStdString(); };
    out.section(t(tr("The file")));
    out.line({t(tr("File name")), "", "", "", r.fileName});
    out.line({t(tr("Records")), "", "", "", std::to_string(r.records.size())});
    out.line({t(tr("Sales with tobacco reported")), "", "", "", std::to_string(r.checks)});
    out.line({t(tr("Quantity (returns less)")), "", "", "", std::to_string(r.quantity)});
    out.total({t(tr("Final sales prices, before tax")), "", "", "", r.total.toString()});
    out.note(t(tr("First line: %1").arg(qs(r.summary))));

    out.section(t(tr("Checks before sending")));
    const int errors = int(std::ranges::count_if(r.issues, [](const sd::Issue &i) { return i.error; }));
    if (r.issues.empty())
        out.note(t(tr("Every check passed.")));
    else if (errors == 0)
        out.note(t(tr("No errors. Warnings to look at:")));
    else
        out.note(t(tr("%1 errors: fix them before the file is sent.").arg(errors)));
    for (const sd::Issue &i : r.issues)
        out.note((i.error ? t(tr("ERROR")) : t(tr("Warning"))) + ": " + i.text);

    std::string last;
    for (const sd::Record &rec : r.records) {
        const auto &f = rec.f;
        if (f[4] != last) {
            last = f[4];
            const QString when = QString::fromStdString(f[2]).insert(6, u'-').insert(4, u'-') + u" "_s + qs(f[3]);
            out.section(t(tr("Sale %1").arg(qs(f[4]))) + " - " + ss(when) + (f[38].empty() ? "" : " - " + f[38]));
        }
        std::string discounts;
        const auto add = [&](const std::string &name, const std::string &amount) {
            if (!amount.empty())
                discounts += (discounts.empty() ? "" : "; ") + (name.empty() ? std::string() : name + " ") + amount;
        };
        if (f[19] == "Y")
            add(t(tr("Mfr deal")) + " " + f[20] + "u", f[21]);
        add(f[22], f[23]);
        add(f[24].size() > 14 ? f[24].substr(0, 14) + "..." : f[24], f[25]);
        add(f[26], f[27]);
        out.line({f[15] + " (" + f[11] + ", " + f[12] + ")", f[14], f[17] + " " + f[16] + (f[18] != "1" ? " x" + f[18] : ""),
                  discounts, f[30]});
    }
    return out;
}

bool PosService::saveScanData(bool anyway)
{
    if (!require(perm::Manager, tr("Saving the scan data file")))
        return false;
    if (rangeReport_.value(u"id"_s).toString() != u"scandata"_s || rangeReport_.value(u"loading"_s).toBool()
        || scanFileName_.isEmpty() || scanFile_.isEmpty())
        return fail(tr("Show the week's scan data first."));
    // Never with the store's Altria details unset or left at their defaults, whatever is pressed.
    if (!scanSettingsReady_)
        return fail(tr("Set up this store's scan data first (Store Settings > Tobacco scan data)."));
    if (scanFileErrors_ && !anyway)
        return fail(tr("The file has errors (see Checks before sending). Fix them, or Save Anyway."));
    const QString dir = scanDataDir();
    if (!QDir().mkpath(dir))
        return fail(tr("Can't make the folder %1.").arg(dir));
    const QString path = QDir(dir).filePath(scanFileName_);
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(scanFile_) != scanFile_.size() || !out.commit())
        return fail(tr("Couldn't write %1.").arg(path));
    logChange(tr("Scan data"), scanFileName_, QString(), QString(),
              scanFileErrors_ ? tr("Saved with errors (Save Anyway)") : tr("Saved"));
    QVariantMap r = rangeReport_;
    r.insert(u"saved"_s, path);
    rangeReport_ = r;
    emit sessionChanged();
    emit notice(tr("Saved %1. Send it with: vt-scan-data send").arg(path));
    return true;
}

} // namespace vt::app
