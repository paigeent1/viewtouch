// PosService: ILSA's sales history (imported by scripts/bin/vt-ilsa-history into ilsa-history.db,
// next to viewtouch.db; PosShared::legacyDb): the ILSA History reports (Daily Z, departments, item
// movement, GP by month / year, ILSA vs ViewTouch, known differences) and its receipts, read-only,
// in Receipt Lookup. Read with connections of their own, away from the screen; the answers come in
// rangeReport like the other reports over a range ({id: "ilsa-...", loading, label, report} /
// {id: "ilsa-receipts", results, selected}). Never in today's drawers, X/Z, end of day or cash.

#include "app/i18n.hh"
#include "app/legacy_history.hh"
#include "app/pos_json.hh"
#include "app/pos_service.hh"
#include "core/legacy_sales.hh"

#include <QDateTime>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

constexpr int kItemRows = 500;      // item movement: the biggest sellers shown (the CSV has those too)
constexpr int kReceiptPage = 100;   // receipts found, a page at a time

const QStringList kLegacyKinds = {u"z"_s, u"depts"_s, u"items"_s, u"gpMonth"_s, u"gpYear"_s, u"compare"_s, u"differences"_s,
                                   u"employees"_s, u"cashiers"_s};

// What the worker thread read, for the report built on the screen's thread.
struct LegacyRead {
    legacy::Status status;
    core::legacy::ZTotals ilsa, vt;
    std::map<std::string, std::int64_t> z;
    std::vector<core::legacy::GroupRow> rows, lottery;
    std::vector<core::legacy::PeriodRow> periods;
    std::vector<core::legacy::Variance> variances;
    std::vector<core::legacy::UnnamedItem> unnamed;
    std::vector<core::legacy::EmployeeItemRow> people;
    std::vector<core::legacy::Cashier> cashiers;
    std::vector<Check> vtChecks;   // By Employee with ViewTouch's sales
    QStringList stores;            // the stores the history has
    bool more = false;
    QString error;
};

std::string historyNote(const legacy::Status &s)
{
    if (!s.present)
        return "No ILSA history imported yet: on the Pi, run vt-ilsa-history with the ILSA export.";
    return "ILSA history in ViewTouch: " + std::to_string(s.days) + " days, " + s.first.toStdString() + " to "
           + s.last.toStdString() + ", " + std::to_string(s.receipts) + " receipts.";
}

} // namespace

QString PosService::legacyStore(const QString &asked) const
{
    if (asked.trimmed() == u"*")
        return {};   // every store's
    return asked.trimmed().isEmpty() ? qs(s_->settings.ilsaHistoryStore).toUpper() : asked.trimmed().toUpper();
}

bool PosService::requestLegacyReport(const QString &kind, const QString &fromText, const QString &toText, const QString &by,
                                     const QString &storeAsked)
{
    if (!require(perm::Manager, tr("Reports")))
        return false;
    if (!kLegacyKinds.contains(kind))
        return fail(tr("No such ILSA report."));
    // Days: yyyy-MM-dd (to empty: one day), or as the Reports screen picks them (By Employee with the
    // ILSA history): week | lastWeek | month | lastMonth | year, or day:<business day id> (0: today's).
    const QString f = fromText.trimmed();
    const QDate today = QDateTime::fromMSecsSinceEpoch(now()).date();
    QDate from, to = today;
    std::int64_t va = 0, vb = 0;   // ViewTouch's checks: closed in [va, vb)
    const int back = (today.dayOfWeek() % 7 - s_->settings.weekStartsOn + 7) % 7;
    if (f == u"week") {
        from = today.addDays(-back);
    } else if (f == u"lastWeek") {
        to = today.addDays(-back - 1);
        from = to.addDays(-6);
    } else if (f == u"month") {
        from = QDate(today.year(), today.month(), 1);
    } else if (f == u"lastMonth") {
        from = QDate(today.year(), today.month(), 1).addMonths(-1);
        to = QDate(today.year(), today.month(), 1).addDays(-1);
    } else if (f == u"year") {
        from = QDate(today.year(), 1, 1);
    } else if (f.startsWith(u"day:")) {   // a business day: its own hours
        const qint64 dayId = f.mid(4).toLongLong();
        const BusinessDay *d = dayId == 0 || dayId == s_->day.id ? &s_->day : nullptr;
        for (const PastDay &p : s_->pastDays)
            if (!d && p.day.id == dayId)
                d = &p.day;
        if (!d)
            return fail(tr("No such day."));
        va = d->openedAt;
        vb = d->closedAt ? d->closedAt : now() + 1;
        from = QDateTime::fromMSecsSinceEpoch(va).date();
        to = QDateTime::fromMSecsSinceEpoch(vb - 1).date();
    } else {
        from = QDate::fromString(f, u"yyyy-MM-dd"_s);
        to = toText.trimmed().isEmpty() ? from : QDate::fromString(toText.trimmed(), u"yyyy-MM-dd"_s);
    }
    if (!from.isValid() || !to.isValid())
        return fail(tr("Dates are written like 2026-09-01."));
    if (to < from)
        std::swap(from, to);
    if (va == 0) {
        va = QDateTime(from, QTime(0, 0)).toMSecsSinceEpoch();
        vb = QDateTime(to.addDays(1), QTime(0, 0)).toMSecsSinceEpoch();
    }
    const QString a = from.toString(u"yyyy-MM-dd"_s), b = to.toString(u"yyyy-MM-dd"_s);
    const QString label = from == to ? i18n::locale().toString(from, u"ddd MMM d, yyyy"_s)
                                     : tr("%1 - %2").arg(i18n::locale().toString(from, u"MMM d, yyyy"_s),
                                                         i18n::locale().toString(to, u"MMM d, yyyy"_s));
    const QString groupBy = QStringList{u"item"_s, u"upc"_s, u"brand"_s, u"dept"_s, u"current"_s}.contains(by)
                                ? by : kind == u"depts" ? u"dept"_s : u"item"_s;
    // By Employee: by = "vt|<filter>" (with ViewTouch's sales) or "|<filter>" (ILSA only).
    const bool employeesWithVt = kind == u"employees" && by.startsWith(u"vt");
    const QString filter = kind == u"employees" ? by.section(u'|', 1).trimmed() : QString();
    const QString path = s_->legacyDb;
    const QString id = u"ilsa-"_s + kind;
    const QString store = legacyStore(storeAsked);   // empty: every store's
    const QString storeShown = store.isEmpty() ? u"*"_s : store;

    // ViewTouch's own checks on those days too (Daily Z, and the side-by-side on parallel beta days).
    const bool withVt = kind == u"z" || kind == u"compare" || employeesWithVt;
    auto history = withVt ? s_->history : decltype(s_->history){};
    std::vector<Check> today_;
    if (withVt)
        for (const Check &c : s_->closedToday)
            if (c.closedAt >= va && c.closedAt < vb)
                today_.push_back(c);
    const TaxRates rates = s_->settings.tax;

    const int request = ++rangeRequest_;
    rangeReport_ = {{u"loading"_s, true}, {u"id"_s, id}, {u"label"_s, label}, {u"store"_s, storeShown}};
    emit sessionChanged();

    QPointer<PosService> self(this);
    QThreadPool::globalInstance()->start([=, today_ = std::move(today_)]() mutable {
        LegacyRead got;
        got.status = legacy::status(path, nullptr, store);
        got.stores = legacy::stores(path);
        if (got.status.present) {
            QString *e = &got.error;
            if (kind == u"z" || kind == u"compare") {
                got.ilsa = legacy::totals(path, a, b, e, store);
                got.z = legacy::ilsaZ(path, a, b, e, store);
            } else if (kind == u"depts") {
                const QString d = groupBy == u"current" ? u"current"_s : u"dept"_s;
                got.rows = legacy::groups(path, a, b, d, false, 0, nullptr, e, store);
                got.lottery = legacy::groups(path, a, b, d, true, 0, nullptr, e, store);
            } else if (kind == u"items") {
                got.rows = legacy::groups(path, a, b, groupBy, false, kItemRows, &got.more, e, store);
            } else if (kind == u"gpMonth" || kind == u"gpYear") {
                got.periods = legacy::periods(path, a, b, kind == u"gpYear", e, store);
            } else if (kind == u"differences") {
                got.variances = legacy::variances(path, a, b, e, store);
                got.unnamed = legacy::unnamedItems(path, a, b, e, store);
            } else if (kind == u"employees") {
                got.people = legacy::employeeItems(path, a, b, filter, e, store);
            } else if (kind == u"cashiers") {
                got.cashiers = legacy::cashiers(path, e, store);
            }
        }
        if (withVt) {
            std::vector<Check> checks = history ? history(va, vb) : std::vector<Check>{};
            for (Check &c : today_)   // today's may not be written yet
                if (std::ranges::none_of(checks, [&](const Check &x) { return x.id == c.id; }))
                    checks.push_back(std::move(c));
            got.vt = core::legacy::fromChecks(checks, rates);
            if (employeesWithVt)
                got.vtChecks = std::move(checks);
        }
        QMetaObject::invokeMethod(self, [self, request, kind, id, label, groupBy, filter, employeesWithVt, store, storeShown,
                                         got = std::move(got)] {
            if (!self || request != self->rangeRequest_)
                return;   // a newer request replaced it
            const ReportContext ctx = self->reportContext(label);
            const bool haveIlsa = got.ilsa.receipts + got.ilsa.notSales > 0;
            const bool haveVt = got.vt.receipts > 0;
            Report r;
            if (kind == u"z") {
                if (haveIlsa)
                    r = core::legacy::dailyZ(got.ilsa, "ILSA", ctx);
                if (haveVt) {
                    const Report v = core::legacy::dailyZ(got.vt, "ViewTouch", ctx);
                    if (!haveIlsa) {
                        r = v;
                    } else {
                        r.title = "Daily Z (ILSA and ViewTouch)";
                        r.section("ViewTouch's own checks, the same days");
                        r.rows.insert(r.rows.end(), v.rows.begin(), v.rows.end());
                    }
                }
                if (!haveIlsa && !haveVt) {
                    r.id = "ilsa-z";
                    r.title = "Daily Z";
                    r.subtitle = ctx.period;
                    r.columns = {"", "Amount"};
                    r.note("No sales on those days, in the ILSA history or in ViewTouch.");
                }
            } else if (kind == u"compare") {
                r = core::legacy::sideBySide(got.z, got.ilsa, haveVt ? &got.vt : nullptr, ctx);
            } else if (kind == u"depts") {
                r = core::legacy::departmentSales(got.rows, got.lottery, ctx, groupBy == u"current");
            } else if (kind == u"items") {
                r = core::legacy::itemMovement(got.rows, groupBy.toStdString(), got.more, ctx);
            } else if (kind == u"gpMonth" || kind == u"gpYear") {
                r = core::legacy::gpByPeriod(got.periods, kind == u"gpYear", ctx);
            } else if (kind == u"employees") {
                r = core::legacy::employeeSales(got.people, employeesWithVt ? &got.vtChecks : nullptr, filter.toStdString(), ctx);
            } else if (kind == u"cashiers") {
                r = core::legacy::cashiersReport(got.cashiers, ctx);
            } else {
                r = core::legacy::differences(got.variances, ctx, got.unnamed);
            }
            if (!got.error.isEmpty())
                r.note("Couldn't read all of the ILSA history: " + got.error.toStdString());
            r.note((store.isEmpty() ? std::string("Every store's ILSA history. ") : "Store " + store.toStdString() + ". ")
                   + historyNote(got.status));
            self->legacyReport_ = r;
            self->rangeReport_ = {{u"loading"_s, false}, {u"id"_s, id}, {u"label"_s, label},
                                  {u"report"_s, self->reportVariant(r)}, {u"store"_s, storeShown}, {u"stores"_s, got.stores}};
            if (kind == u"cashiers") {   // the Cashiers screen: each cashier and the employees to tie them to
                QVariantList list;
                for (const core::legacy::Cashier &c : got.cashiers)
                    list.append(QVariantMap{{u"sourceId"_s, c.sourceId}, {u"source"_s, qs(c.source)}, {u"number"_s, c.number},
                                            {u"label"_s, qs(core::legacy::cashierLabel(c))}, {u"receipts"_s, qint64(c.receipts)},
                                            {u"sales"_s, self->format(c.sales)}, {u"first"_s, qs(c.first)}, {u"last"_s, qs(c.last)},
                                            {u"employeeId"_s, qs(c.employeeId)}, {u"employeeName"_s, qs(c.employeeName)}});
                QVariantList staff;
                for (const Employee &e : self->s_->employees)
                staff.append(QVariantMap{{u"id"_s, qs(e.id)}, {u"name"_s, qs(e.name)}, {u"active"_s, e.active}});
                self->rangeReport_.insert(u"cashiers"_s, list);
                self->rangeReport_.insert(u"employees"_s, staff);
            }
            emit self->sessionChanged();
        }, Qt::QueuedConnection);
    });
    return true;
}

bool PosService::setLegacyCashier(int sourceId, int number, const QString &employeeId)
{
    if (!require(perm::Manager, tr("ILSA cashiers")))
        return false;
    std::string name;
    if (!employeeId.isEmpty()) {
        const auto it = std::ranges::find_if(s_->employees, [&](const Employee &e) { return qs(e.id) == employeeId; });
        if (it == s_->employees.end())
            return fail(tr("No such employee."));
        name = it->name;
    }
    QString error;
    if (!legacy::setCashier(s_->legacyDb, sourceId, number, ss(employeeId), name, &error))
        return fail(tr("Couldn't save it: %1").arg(error));
    emit notice(employeeId.isEmpty() ? tr("ILSA cashier %1: a former employee").arg(number)
                                     : tr("ILSA cashier %1 is %2").arg(number).arg(qs(name)));
    return requestLegacyReport(u"cashiers"_s, u"2000-01-01"_s, QDateTime::fromMSecsSinceEpoch(now()).date().toString(u"yyyy-MM-dd"_s),
                               {}, rangeReport_.value(u"store"_s).toString());
}

bool PosService::printLegacyReport()
{
    if (!require(perm::Manager, tr("Printing reports")))
        return false;
    if (!s_->printer)
        return fail(tr("No printer is set up."));
    if (legacyReport_.rows.empty())
        return fail(tr("Show an ILSA report first."));
    s_->printer->printReport(s_->settings, legacyReport_, receiptPrinter());
    emit notice(tr("Printing %1").arg(qs(legacyReport_.title)));
    return true;
}

bool PosService::findLegacyReceipts(const QString &query, int offset, const QString &storeAsked)
{
    if (!require(perm::Settle, tr("Finding checks")))
        return false;
    const QString q = query.trimmed();
    offset = std::max(0, offset);
    const QString path = s_->legacyDb;
    const int request = ++rangeRequest_;
    const QString store = legacyStore(storeAsked);
    const QString storeShown = store.isEmpty() ? u"*"_s : store;
    rangeReport_ = {{u"loading"_s, true}, {u"id"_s, u"ilsa-receipts"_s}, {u"query"_s, q}, {u"offset"_s, offset}, {u"store"_s, storeShown}};
    emit sessionChanged();
    QPointer<PosService> self(this);
    QThreadPool::globalInstance()->start([=] {
        QString error;
        const legacy::Status status = legacy::status(path, nullptr, store);
        const QStringList stores = legacy::stores(path);
        std::vector<core::legacy::Receipt> hits;
        if (status.present)
            hits = legacy::findReceipts(path, q, kReceiptPage + 1, offset, &error, store);
        QMetaObject::invokeMethod(self, [self, request, q, offset, status, error, storeShown, stores, hits = std::move(hits)] {
            if (!self || request != self->rangeRequest_)
                return;
            QVariantList results;
            for (std::size_t i = 0; i < hits.size() && int(i) < kReceiptPage; ++i) {
                const core::legacy::Receipt &h = hits[i];
                const QDateTime at = QDateTime::fromString(qs(h.at), u"yyyy-MM-dd HH:mm:ss"_s);
                results.append(QVariantMap{{u"id"_s, qint64(h.id)}, {u"number"_s, qint64(h.number)},
                                           {u"label"_s, qs(core::legacy::receiptLabel(h))},
                                           {u"when"_s, at.isValid() ? i18n::locale().toString(at, u"ddd MMM d yyyy, h:mm AP"_s) : qs(h.day)},
                                           {u"day"_s, qs(h.day)}, {u"total"_s, self->format(h.total)},
                                           {u"mode"_s, h.mode}, {u"source"_s, qs(h.source)},
                                           {u"cashier"_s, h.cashier}, {u"cashierName"_s, qs(h.cashierName)}});
            }
            QVariantMap out{{u"loading"_s, false}, {u"id"_s, u"ilsa-receipts"_s}, {u"query"_s, q}, {u"offset"_s, offset},
                            {u"results"_s, results}, {u"more"_s, int(hits.size()) > kReceiptPage},
                            {u"nextOffset"_s, offset + kReceiptPage}, {u"history"_s, qs(historyNote(status))},
                            {u"store"_s, storeShown}, {u"stores"_s, stores}};
            if (!error.isEmpty())
                out.insert(u"error"_s, error);
            self->rangeReport_ = out;
            emit self->sessionChanged();
        }, Qt::QueuedConnection);
    });
    return true;
}

// As it prints (the store's receipt header on top), else plain.
QString PosService::legacyText(const core::legacy::Receipt &r) const
{
    const QString printed = s_->printer ? s_->printer->legacyReceiptText(s_->settings, core::legacy::receiptRows(r, false)) : QString();
    if (!printed.isEmpty())
        return printed;
    std::string head = s_->settings.storeName + "\n";
    if (!s_->settings.receiptHeader.empty())
        head += s_->settings.receiptHeader + "\n";
    return qs(head + "\n" + core::legacy::receiptText(r, false));
}

bool PosService::selectLegacyReceipt(qint64 id)
{
    if (!require(perm::Settle, tr("Finding checks")))
        return false;
    QString error;
    const auto r = legacy::receipt(s_->legacyDb, id, &error);
    if (!r)
        return fail(tr("That ILSA receipt isn't in the history."));
    QVariantMap out = rangeReport_;
    out.insert(u"selected"_s, QVariantMap{{u"id"_s, qint64(r->id)}, {u"number"_s, qint64(r->number)},
                                          {u"label"_s, qs(core::legacy::receiptLabel(*r))},
                                          {u"text"_s, legacyText(*r)},
                                          {u"hasDob"_s, !r->dob.empty()}, {u"mode"_s, r->mode}});
    rangeReport_ = out;
    emit sessionChanged();
    return true;
}

bool PosService::reprintLegacyReceipt(qint64 id, bool withDob)
{
    if (!require(perm::Settle, tr("Printing receipts")))
        return false;
    if (!s_->printer)
        return fail(tr("No printer is set up."));
    QString error;
    const auto r = legacy::receipt(s_->legacyDb, id, &error);
    if (!r)
        return fail(tr("That ILSA receipt isn't in the history."));
    if (withDob) {   // as ILSA printed it, with the buyer's date of birth: the store's copy
        if (r->dob.empty())
            return fail(tr("No date of birth on ILSA receipt %1.").arg(qs(core::legacy::receiptLabel(*r))));
        if (s_->settings.dobReprintNeedsManager && !require(perm::Manager, tr("Reprinting with the date of birth")))
            return false;
    }
    s_->printer->printLegacyReceipt(s_->settings, core::legacy::receiptReport(*r, withDob),
                                    core::legacy::receiptRows(*r, withDob), receiptPrinter());
    emit notice(tr("Printing ILSA receipt %1").arg(qs(core::legacy::receiptLabel(*r))));
    return true;
}

} // namespace vt::app
