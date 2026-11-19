// PosService: Manager > Reports > Exceptions search. Every void, error correct, comp or discount, payment
// or discount taken back, reopened check, Cancel Sale, No Sale, refund and ID checked by hand or refused,
// over any days, from the database (never from files). The same checks and drawers the Exceptions report
// counts (core::exceptionEntries beside core::exceptionsReport), so the totals match it. Read away from
// the screen; 200 rows at a time; touching one opens its check in Find a Check.

#include "app/i18n.hh"
#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QDateTime>
#include <QPointer>
#include <QRegularExpression>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

constexpr int kPage = 200;

QString typeLabel(const std::string &type)
{
    static const std::map<std::string, const char *> labels = {
        {"void", QT_TR_NOOP("Void (item already sent)")},
        {"errorCorrect", QT_TR_NOOP("Error correct")},
        {"itemReturn", QT_TR_NOOP("Item return")},
        {"discount", QT_TR_NOOP("Comp / discount")},
        {"takenBack", QT_TR_NOOP("Payment taken back")},
        {"reopen", QT_TR_NOOP("Reopened check")},
        {"cancel", QT_TR_NOOP("Cancel Sale")},
        {"noSale", QT_TR_NOOP("No Sale")},
        {"refund", QT_TR_NOOP("Refund")},
        {"ageHand", QT_TR_NOOP("Date of birth typed by hand")},
        {"ageRefused", QT_TR_NOOP("ID refused")},
    };
    const auto it = labels.find(type);
    return it == labels.end() ? QString::fromStdString(type) : PosService::tr(it->second);
}

// A check number typed: "123", "#123", "HCA123", "HCA-123" (the store's prefix or not). 0: not one.
std::int64_t typedCheckNumber(QString text, const QString &prefix)
{
    text = text.trimmed();
    if (text.startsWith(u'#'))
        text.remove(0, 1);
    if (!prefix.isEmpty() && text.startsWith(prefix, Qt::CaseInsensitive))
        text.remove(0, prefix.size());
    if (text.startsWith(u'-') || text.startsWith(u'#'))
        text.remove(0, 1);
    static const QRegularExpression digits(u"^[0-9]{1,12}$"_s);
    return digits.match(text).hasMatch() ? text.toLongLong() : 0;
}

struct Filter {
    QString employee;
    std::set<std::string> types;
    std::int64_t minCents = -1;
    QString text;
    std::int64_t number = 0;
};

bool matches(const ExceptionEntry &e, const Filter &f)
{
    if (!f.employee.isEmpty() && QString::fromStdString(e.who).compare(f.employee, Qt::CaseInsensitive) != 0)
        return false;
    if (!f.types.empty() && !f.types.contains(e.type))
        return false;
    if (f.minCents >= 0 && e.amount.cents() < f.minCents)
        return false;
    if (!f.text.isEmpty()) {
        // A check number ("1234", "#1234", "HCA-1234") finds that check only (not words with the digits in them).
        if (f.number > 0)
            return e.checkId == f.number;
        const bool byWords = QString::fromStdString(e.what).contains(f.text, Qt::CaseInsensitive)
                             || QString::fromStdString(e.reason).contains(f.text, Qt::CaseInsensitive)
                             || QString::fromStdString(e.checkLabel).compare(f.text, Qt::CaseInsensitive) == 0;
        if (!byWords)
            return false;
    }
    return true;
}

} // namespace

bool PosService::searchExceptions(const QVariantMap &filter)
{
    if (!require(perm::Manager, tr("The Exceptions search")))
        return false;
    const QDate today = QDateTime::fromMSecsSinceEpoch(now()).date();
    QDate from = QDate::fromString(filter.value(u"from"_s).toString().trimmed(), u"yyyy-MM-dd"_s);
    QDate to = QDate::fromString(filter.value(u"to"_s).toString().trimmed(), u"yyyy-MM-dd"_s);
    if (!from.isValid())
        from = today;
    if (!to.isValid())
        to = from;
    if (to < from)
        std::swap(from, to);
    if (from.daysTo(to) > 366)
        return fail(tr("Choose at most a year at a time."));
    Filter f;
    f.employee = filter.value(u"employee"_s).toString().trimmed();
    for (const QString &t : filter.value(u"types"_s).toStringList())
        f.types.insert(ss(t));
    if (const QString amount = filter.value(u"minAmount"_s).toString().trimmed(); !amount.isEmpty()) {
        bool ok = false;
        const double dollars = QString(amount).remove(u'$').remove(u',').toDouble(&ok);
        if (!ok || dollars < 0)
            return fail(tr("The amount is written like 5 or 12.50."));
        f.minCents = std::llround(dollars * 100);
    }
    f.text = filter.value(u"text"_s).toString().trimmed();
    f.number = typedCheckNumber(f.text, qs(s_->settings.checkPrefix));

    const auto startOf = [](QDate d) { return QDateTime(d, QTime(0, 0)).toMSecsSinceEpoch(); };
    const std::int64_t a = startOf(from), b = startOf(to.addDays(1));
    const QString label = from == to ? i18n::locale().toString(from, u"ddd MMM d, yyyy"_s)
                                     : tr("%1 - %2").arg(i18n::locale().toString(from, u"MMM d"_s),
                                                         i18n::locale().toString(to, u"MMM d, yyyy"_s));
    const int request = ++exceptionRequest_;
    exceptionFilter_ = filter;
    exceptionFilter_.insert(u"from"_s, from.toString(u"yyyy-MM-dd"_s));
    exceptionFilter_.insert(u"to"_s, to.toString(u"yyyy-MM-dd"_s));
    exceptionLabel_ = label;
    exceptionSearch_ = {{u"loading"_s, true}, {u"filter"_s, exceptionFilter_}, {u"label"_s, label}};
    emit sessionChanged();

    // What's in memory now, as the Exceptions report has it: today's checks (and the open ones), Cancel
    // Sales, the drawers and refunds (today's may not be written yet).
    const bool withToday = a <= now() && now() < b;
    std::vector<Check> memChecks;
    std::vector<DrawerSession> memDrawers;
    std::vector<Refund> memRefunds;
    if (withToday) {
        memChecks = s_->closedToday;
        for (const auto &[id, c] : s_->open)
            if (!c.training)
                memChecks.push_back(c);
        for (const Check &c : s_->cancelledToday)
            memChecks.push_back(c);
        memDrawers = s_->drawers;
    }
    for (const Check &c : s_->closedToday)   // a day that began before the range: only its checks in it
        if (!withToday && c.closedAt >= a && c.closedAt < b)
            memChecks.push_back(c);
    for (const Refund &r : s_->refundsToday)
        if (r.at >= a && r.at < b)
            memRefunds.push_back(r);
    auto history = s_->history;
    auto cancelled = s_->cancelledHistory;
    auto drawers = s_->drawerHistory;
    auto refunds = s_->refundHistory;
    QPointer<PosService> self(this);
    QThreadPool::globalInstance()->start([=, memChecks = std::move(memChecks), memDrawers = std::move(memDrawers),
                                          memRefunds = std::move(memRefunds)]() mutable {
        std::vector<Check> checks = std::move(memChecks);
        std::set<std::int64_t> ids;
        for (const Check &c : checks)
            ids.insert(c.id);
        for (auto *read : {&history, &cancelled})
            if (*read)
                for (Check &c : (*read)(a, b))
                    if (ids.insert(c.id).second)
                        checks.push_back(std::move(c));
        std::vector<DrawerSession> ds = std::move(memDrawers);
        if (drawers)
            for (DrawerSession &d : drawers(a, b))
                if (std::ranges::none_of(ds, [&](const DrawerSession &x) { return x.id == d.id; }))
                    ds.push_back(std::move(d));
        std::vector<Refund> rs = std::move(memRefunds);
        if (refunds)
            for (Refund &r : refunds(a, b))
                if (std::ranges::none_of(rs, [&](const Refund &x) { return x.id == r.id; }))
                    rs.push_back(std::move(r));
        std::vector<const Check *> ptrs;
        for (const Check &c : checks)
            ptrs.push_back(&c);
        std::vector<ExceptionEntry> all = exceptionEntries(ptrs, ds, rs);
        std::erase_if(all, [&](const ExceptionEntry &e) { return !matches(e, f); });
        QMetaObject::invokeMethod(self, [self, request, all = std::move(all)]() mutable {
            if (!self || request != self->exceptionRequest_)
                return;   // a newer search replaced it
            self->exceptionHits_ = std::move(all);
            self->exceptionShown_ = std::min<int>(kPage, int(self->exceptionHits_.size()));
            self->showExceptions();
        }, Qt::QueuedConnection);
    });
    return true;
}

bool PosService::moreExceptions()
{
    if (!require(perm::Manager, tr("The Exceptions search")))
        return false;
    exceptionShown_ = std::min<int>(exceptionShown_ + kPage, int(exceptionHits_.size()));
    showExceptions();
    return true;
}

void PosService::showExceptions()
{
    // Totals by type, over everything found (not only the rows shown).
    std::map<std::string, std::pair<int, Money>> byType;
    for (const ExceptionEntry &e : exceptionHits_) {
        auto &t = byType[e.type];
        ++t.first;
        t.second += e.amount;
    }
    QVariantList totals;
    for (const std::string &type : exceptionTypes())
        if (const auto it = byType.find(type); it != byType.end())
            totals << QVariantMap{{u"type"_s, qs(type)}, {u"label"_s, typeLabel(type)}, {u"count"_s, it->second.first},
                                  {u"cents"_s, qint64(it->second.second.cents())}, {u"amount"_s, format(it->second.second)}};
    const QString prefix = qs(s_->settings.checkPrefix);
    QVariantList rows;
    for (int i = 0; i < exceptionShown_ && i < int(exceptionHits_.size()); ++i) {
        const ExceptionEntry &e = exceptionHits_[std::size_t(i)];
        const QDateTime at = QDateTime::fromMSecsSinceEpoch(e.at);
        rows << QVariantMap{{u"at"_s, qint64(e.at)}, {u"when"_s, i18n::dateTimeText(at)},
                            {u"who"_s, qs(e.who)}, {u"type"_s, qs(e.type)}, {u"typeLabel"_s, typeLabel(e.type)},
                            {u"checkId"_s, qint64(e.checkId)},
                            {u"check"_s, e.checkId ? (prefix.isEmpty() ? u"#%1"_s.arg(e.checkId) : prefix + u'-' + QString::number(e.checkId))
                                                   : qs(e.drawer)},
                            {u"what"_s, i18n::phrase(i18n::current(), qs(e.what))}, {u"amount"_s, e.amount.cents() ? format(e.amount) : QString()},
                            {u"reason"_s, qs(e.reason)}, {u"approvedBy"_s, qs(e.approvedBy)}};
    }
    // Who can be picked: everyone found, and the staff.
    QStringList people;
    for (const Employee &m : s_->employees)
        people << qs(m.name);
    exceptionSearch_ = {{u"loading"_s, false}, {u"filter"_s, exceptionFilter_}, {u"label"_s, exceptionLabel_},
                        {u"totals"_s, totals}, {u"results"_s, rows}, {u"shown"_s, int(rows.size())},
                        {u"count"_s, int(exceptionHits_.size())}, {u"more"_s, exceptionShown_ < int(exceptionHits_.size())},
                        {u"people"_s, people}};
    emit sessionChanged();
}

QVariantMap PosService::exceptionSearch() const
{
    return exceptionSearch_;
}

Report PosService::exceptionSearchReport() const
{
    Report r;
    r.id = "exceptionsearch";
    r.title = ss(tr("Exceptions search"));
    r.subtitle = ss(exceptionLabel_);
    r.columns = {ss(tr("When · employee · what")), ss(tr("Check")), ss(tr("Reason")), ss(tr("Approved by")), ss(tr("Amount"))};
    std::map<std::string, std::pair<int, Money>> byType;
    for (const ExceptionEntry &e : exceptionHits_) {
        auto &t = byType[e.type];
        ++t.first;
        t.second += e.amount;
    }
    r.section(ss(tr("Totals by type")));
    for (const std::string &type : exceptionTypes())
        if (const auto it = byType.find(type); it != byType.end())
            r.line({ss(typeLabel(type)) + " (" + std::to_string(it->second.first) + ")", "", "", "", ss(format(it->second.second))});
    r.section(ss(tr("Every one, newest first")));
    const QString prefix = qs(s_->settings.checkPrefix);
    for (const ExceptionEntry &e : exceptionHits_)
        r.line({ss(i18n::dateTimeText(QDateTime::fromMSecsSinceEpoch(e.at))) + " · " + e.who + " · " + ss(typeLabel(e.type))
                    + (e.what.empty() ? "" : ": " + e.what),
                e.checkId ? (prefix.isEmpty() ? "#" + std::to_string(e.checkId) : ss(prefix) + "-" + std::to_string(e.checkId)) : e.drawer,
                e.reason, e.approvedBy, e.amount.cents() ? ss(format(e.amount)) : std::string()});
    if (exceptionHits_.empty())
        r.note(ss(tr("Nothing matches.")));
    return r;
}

QVariantMap PosService::exceptionSearchVariant()
{
    if (!require(perm::Manager, tr("The Exceptions search")))
        return {};
    return reportVariant(exceptionSearchReport());
}

bool PosService::printExceptionSearch()
{
    if (!require(perm::Manager, tr("The Exceptions search")))
        return false;
    if (!s_->printer)
        return fail(tr("No printer is set up."));
    const Report r = exceptionSearchReport();
    s_->printer->printReport(s_->settings, r, receiptPrinter());
    emit notice(tr("Printing %1").arg(qs(r.title)));
    return true;
}

} // namespace vt::app
