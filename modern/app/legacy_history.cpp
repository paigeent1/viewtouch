#include "app/legacy_history.hh"

#include <QAtomicInt>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core::legacy;

namespace vt::app::legacy {

namespace {

// Runs `f(db)` on a read-only connection of its own (any thread); `fallback` when there's no file.
template <typename R, typename F>
R withHistory(const QString &path, QString *error, R fallback, F &&f)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        if (error)
            *error = u"No ILSA history has been imported yet (vt-ilsa-history)."_s;
        return fallback;
    }
    static QAtomicInt counter;
    const QString name = u"vt-legacy-%1"_s.arg(counter.fetchAndAddRelaxed(1));
    R out = std::move(fallback);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, name);
        db.setDatabaseName(path);
        db.setConnectOptions(u"QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"_s);
        if (!db.open()) {
            if (error)
                *error = db.lastError().text();
        } else {
            out = f(db);
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return out;
}

bool run(QSqlQuery &q, const QString &sql, const QVariantList &args, QString *error)
{
    q.setForwardOnly(true);
    if (!q.prepare(sql)) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    for (const QVariant &a : args)
        q.addBindValue(a);
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    return true;
}

Money cents(const QSqlQuery &q, int i) { return Money::fromCents(q.value(i).toLongLong()); }
// Discounts as ILSA prints them: taken off (negative), whichever sign the export kept.
Money discount(const QSqlQuery &q, int i) { const std::int64_t c = q.value(i).toLongLong(); return Money::fromCents(c > 0 ? -c : c); }
std::string text(const QSqlQuery &q, int i) { return q.value(i).toString().toStdString(); }

// Receipts' columns, in this order, for findReceipts and receipt.
const QString kReceiptColumns = u"r.id, r.receipt_num, r.at, r.day, r.total_cents, r.tax_cents, r.mode, r.reg_num, "
                                "r.cashier, r.age_verified, r.dob, COALESCE(s.name, '')"_s;

Receipt receiptOf(const QSqlQuery &q)
{
    Receipt r;
    r.id = q.value(0).toLongLong();
    r.number = q.value(1).toLongLong();
    r.at = text(q, 2);
    r.day = text(q, 3);
    r.total = cents(q, 4);
    r.tax = cents(q, 5);
    r.mode = q.value(6).toInt();
    r.reg = q.value(7).toInt();
    r.cashier = q.value(8).toInt();
    r.ageVerified = q.value(9).toInt();
    r.dob = text(q, 10);
    r.source = text(q, 11);
    return r;
}

} // namespace

QString historyPath(const QString &posDatabase)
{
    return QFileInfo(posDatabase).absoluteDir().filePath(u"ilsa-history.db"_s);
}

Status status(const QString &path, QString *error)
{
    return withHistory<Status>(path, error, Status{}, [&](QSqlDatabase &db) {
        Status s;
        QSqlQuery q(db);
        if (run(q, u"SELECT MIN(day), MAX(day), COUNT(*), COALESCE(SUM(receipts), 0) FROM legacy_days"_s, {}, error) && q.next()) {
            s.first = q.value(0).toString();
            s.last = q.value(1).toString();
            s.days = q.value(2).toLongLong();
            s.receipts = q.value(3).toLongLong();
            s.present = s.days > 0;
        }
        return s;
    });
}

ZTotals totals(const QString &path, const QString &from, const QString &to, QString *error)
{
    return withHistory<ZTotals>(path, error, ZTotals{}, [&](QSqlDatabase &db) {
        ZTotals z;
        {
            QSqlQuery q(db);
            if (run(q, u"SELECT COUNT(*), COALESCE(SUM(CASE WHEN mode = 0 THEN 1 ELSE 0 END), 0), "
                        "COALESCE(SUM(CASE WHEN mode = 0 THEN tax_cents ELSE 0 END), 0), "
                        "COALESCE(SUM(CASE WHEN mode = 0 THEN total_cents ELSE 0 END), 0), "
                        "COALESCE(SUM(CASE WHEN mode = 0 THEN disc_cents ELSE 0 END), 0) "
                        "FROM legacy_receipts WHERE day BETWEEN ? AND ?"_s, {from, to}, error) && q.next()) {
                z.receipts = q.value(1).toLongLong();
                z.notSales = q.value(0).toLongLong() - z.receipts;
                z.tax = cents(q, 2);
                z.receiptTotals = cents(q, 3);
                z.discounts = discount(q, 4);
            }
        }
        {
            // Sales receipts' lines (paid outs on any receipt).
            QSqlQuery q(db);
            if (run(q, u"SELECT kind, lottery, SUM(amount_cents), SUM(cost_cents), "
                        "SUM(CASE WHEN has_cost = 0 THEN amount_cents ELSE 0 END), SUM(qty), COUNT(*), "
                        "SUM(voided), SUM(CASE WHEN voided = 1 AND amount_cents > 0 THEN amount_cents ELSE 0 END), "
                        "SUM(CASE WHEN returned = 1 AND voided = 0 THEN 1 ELSE 0 END), "
                        "SUM(CASE WHEN returned = 1 AND voided = 0 THEN amount_cents ELSE 0 END), SUM(disc_cents) "
                        "FROM legacy_lines WHERE day BETWEEN ? AND ? AND (mode = 0 OR kind = 'payout') "
                        "GROUP BY kind, lottery"_s, {from, to}, error)) {
                Money lineDiscounts;
                while (q.next()) {
                    const QString kind = q.value(0).toString();
                    if (q.value(1).toInt() == 0 && kind != u"payout")
                        lineDiscounts += discount(q, 11);
                    const bool lottery = q.value(1).toInt() != 0;
                    const Money amount = cents(q, 2);
                    z.voidLines += q.value(7).toLongLong();
                    z.voided += cents(q, 8);
                    z.returnLines += q.value(9).toLongLong();
                    z.returned += cents(q, 10);
                    if (kind == u"payout") {
                        z.payouts += amount;
                    } else if (lottery) {
                        z.lottery += amount;
                    } else if (kind == u"item") {
                        z.merchandise += amount;
                        z.cost += cents(q, 3);
                        z.noCostSales += cents(q, 4);
                        z.units += q.value(5).toDouble();
                        z.itemLines += q.value(6).toLongLong();
                    } else if (kind == u"entry") {
                        z.entries += amount;
                    } else if (kind == u"promo") {
                        z.promotions += amount;
                    } else {
                        z.other += amount;
                    }
                }
                if (z.discounts.cents() == 0)   // the receipts' own, else their lines'
                    z.discounts = lineDiscounts;
            }
        }
        {
            // Tenders by ViewTouch's groups; all the change given comes out of cash (as ILSA's Z).
            QSqlQuery q(db);
            std::vector<std::pair<std::string, Money>> list;
            Money change;
            if (run(q, u"SELECT t.type, SUM(t.amount_cents), SUM(t.change_cents) FROM legacy_tenders t "
                        "JOIN legacy_receipts r ON r.id = t.receipt_id WHERE r.day BETWEEN ? AND ? "
                        "GROUP BY t.type ORDER BY t.type"_s, {from, to}, error)) {
                while (q.next()) {
                    const std::string name = tenderGroup(q.value(0).toInt());
                    auto it = std::ranges::find_if(list, [&](const auto &p) { return p.first == name; });
                    if (it == list.end()) {
                        list.emplace_back(name, Money());
                        it = list.end() - 1;
                    }
                    it->second += cents(q, 1);
                    change += cents(q, 2);
                }
            }
            if (change.cents() != 0) {
                auto it = std::ranges::find_if(list, [](const auto &p) { return p.first == "Cash"; });
                if (it == list.end())
                    list.insert(list.begin(), std::pair<std::string, Money>{"Cash", -change});
                else
                    it->second -= change;
            }
            z.tenders = std::move(list);
        }
        return z;
    });
}

std::map<std::string, std::int64_t> ilsaZ(const QString &path, const QString &from, const QString &to, QString *error)
{
    using Map = std::map<std::string, std::int64_t>;
    return withHistory<Map>(path, error, Map{}, [&](QSqlDatabase &db) {
        Map out;
        QSqlQuery q(db);
        if (run(q, u"SELECT v.name, SUM(v.cents) FROM legacy_z z JOIN legacy_z_values v ON v.z_id = z.id "
                    "WHERE z.day BETWEEN ? AND ? AND v.name LIKE 'ilsa:%' GROUP BY v.name"_s, {from, to}, error)) {
            while (q.next())
                out[text(q, 0).substr(5)] = q.value(1).toLongLong();
        }
        return out;
    });
}

std::vector<GroupRow> groups(const QString &path, const QString &from, const QString &to, const QString &by, bool lottery,
                             int limit, bool *more, QString *error)
{
    if (more)
        *more = false;
    using Rows = std::vector<GroupRow>;
    return withHistory<Rows>(path, error, Rows{}, [&](QSqlDatabase &db) {
        // key, label, extra
        QString select, group, order = u"SUM(CASE WHEN l.kind = 'item' THEN l.amount_cents ELSE 0 END) DESC"_s;
        QString join = u"d.dept = l.dept"_s;
        const bool depts = by == u"dept" || by == u"current";
        if (by == u"current") {   // as ILSA's own report: the item's department in ILSA now
            select = u"CAST(l.cur_dept AS TEXT), COALESCE(MAX(d.name), ''), ''"_s;
            group = u"l.cur_dept"_s;
            order = u"l.cur_dept"_s;
            join = u"d.dept = l.cur_dept"_s;
        } else if (by == u"dept") {   // the department each line was sold in
            select = u"CAST(l.dept AS TEXT), COALESCE(MAX(d.name), ''), ''"_s;
            group = u"l.dept"_s;
            order = u"l.dept"_s;
        } else if (by == u"upc") {
            select = u"l.upc, MAX(l.name), CAST(MAX(l.dept) AS TEXT)"_s;
            group = u"l.upc"_s;
        } else if (by == u"brand") {
            select = u"l.brand, CASE WHEN l.brand = '' THEN '(no brand)' ELSE l.brand END, ''"_s;
            group = u"l.brand"_s;
        } else {   // item: each name and UPC as it was sold (no merging)
            select = u"l.upc, l.name, l.upc"_s;
            group = u"l.upc, l.name"_s;
        }
        const int cap = limit > 0 ? limit : 1000000;
        // Item lines; by department also its promotions (ILSA's buydowns) and line discounts.
        const QString sql = u"SELECT %1, SUM(CASE WHEN l.kind = 'item' THEN l.qty ELSE 0 END), "
                            "SUM(CASE WHEN l.kind = 'item' THEN l.amount_cents ELSE 0 END), "
                            "SUM(CASE WHEN l.kind = 'item' THEN l.cost_cents ELSE 0 END), "
                            "SUM(CASE WHEN l.kind = 'item' AND l.has_cost = 0 THEN l.amount_cents ELSE 0 END), "
                            "SUM(CASE WHEN l.kind = 'item' THEN 1 ELSE 0 END), "
                            "SUM(CASE WHEN l.kind = 'promo' THEN l.amount_cents ELSE 0 END), SUM(l.disc_cents) "
                            "FROM legacy_lines l LEFT JOIN legacy_depts d ON %4 "
                            "WHERE l.day BETWEEN ? AND ? AND l.kind IN (%5) AND l.mode = 0 AND l.lottery = ? "
                            "GROUP BY %2 ORDER BY %3 LIMIT ?"_s.arg(select, group, order, join,
                                                                     depts ? u"'item', 'promo'"_s : u"'item'"_s);
        Rows out;
        QSqlQuery q(db);
        if (run(q, sql, {from, to, lottery ? 1 : 0, cap + 1}, error)) {
            while (q.next()) {
                if (int(out.size()) == cap) {
                    if (more)
                        *more = true;
                    break;
                }
                GroupRow g;
                g.key = text(q, 0);
                g.label = text(q, 1);
                g.extra = text(q, 2);
                g.units = q.value(3).toDouble();
                g.sales = cents(q, 4);
                g.cost = cents(q, 5);
                g.noCostSales = cents(q, 6);
                g.lines = q.value(7).toLongLong();
                g.buydowns = cents(q, 8);
                g.discounts = discount(q, 9);
                out.push_back(std::move(g));
            }
        }
        return out;
    });
}

std::vector<PeriodRow> periods(const QString &path, const QString &from, const QString &to, bool byYear, QString *error)
{
    using Rows = std::vector<PeriodRow>;
    return withHistory<Rows>(path, error, Rows{}, [&](QSqlDatabase &db) {
        Rows out;
        QSqlQuery q(db);
        const QString sql = u"SELECT substr(day, 1, %1), "
                            "SUM(CASE WHEN lottery = 0 AND kind = 'item' THEN qty ELSE 0 END), "
                            "SUM(CASE WHEN lottery = 0 AND kind = 'item' THEN amount_cents ELSE 0 END), "
                            "SUM(CASE WHEN lottery = 0 AND kind = 'item' THEN cost_cents ELSE 0 END), "
                            "SUM(CASE WHEN lottery = 0 AND kind = 'item' AND has_cost = 0 THEN amount_cents ELSE 0 END), "
                            "SUM(CASE WHEN lottery = 1 AND kind = 'item' THEN amount_cents ELSE 0 END), "
                            "SUM(CASE WHEN lottery = 0 AND kind = 'promo' THEN amount_cents ELSE 0 END), "
                            "SUM(CASE WHEN lottery = 0 THEN disc_cents ELSE 0 END) "
                            "FROM legacy_lines WHERE day BETWEEN ? AND ? AND kind IN ('item', 'promo') AND mode = 0 "
                            "GROUP BY 1 ORDER BY 1"_s.arg(byYear ? 4 : 7);
        if (run(q, sql, {from, to}, error)) {
            while (q.next()) {
                PeriodRow p;
                p.period = text(q, 0);
                p.units = q.value(1).toDouble();
                p.sales = cents(q, 2);
                p.cost = cents(q, 3);
                p.noCostSales = cents(q, 4);
                p.lottery = cents(q, 5);
                p.buydowns = cents(q, 6);
                p.discounts = discount(q, 7);
                out.push_back(std::move(p));
            }
        }
        return out;
    });
}

std::vector<Variance> variances(const QString &path, const QString &from, const QString &to, QString *error)
{
    using Rows = std::vector<Variance>;
    return withHistory<Rows>(path, error, Rows{}, [&](QSqlDatabase &db) {
        Rows out;
        QSqlQuery q(db);
        if (run(q, u"SELECT v.day, COALESCE(z.reg_num, 0), COALESCE(z.z_counter, 0), v.what, v.ilsa_cents, v.ours_cents "
                    "FROM legacy_variances v LEFT JOIN legacy_z z ON z.id = v.z_id "
                    "WHERE v.day BETWEEN ? AND ? ORDER BY v.day, v.what"_s, {from, to}, error)) {
            while (q.next()) {
                Variance v;
                v.day = text(q, 0);
                v.reg = q.value(1).toInt();
                v.z = q.value(2).toLongLong();
                v.what = text(q, 3);
                v.ilsa = q.value(4).toLongLong();
                v.ours = q.value(5).toLongLong();
                out.push_back(std::move(v));
            }
        }
        return out;
    });
}

std::vector<Receipt> findReceipts(const QString &path, const QString &query, int limit, int offset, QString *error)
{
    using Rows = std::vector<Receipt>;
    return withHistory<Rows>(path, error, Rows{}, [&](QSqlDatabase &db) {
        static const QRegularExpression day(u"^\\d{4}-\\d{2}-\\d{2}$"_s);
        static const QRegularExpression number(u"^#?(\\d{1,7})$"_s);
        static const QRegularExpression upc(u"^\\d{8,14}$"_s);
        static const QRegularExpression amount(u"^\\$?(\\d+)\\.(\\d\\d)$"_s);
        const QString qy = query.trimmed();
        QString where = u"1"_s;
        QVariantList args;
        if (qy.isEmpty()) {
        } else if (day.match(qy).hasMatch()) {
            where = u"r.day = ?"_s;
            args << qy;
        } else if (const auto m = number.match(qy); m.hasMatch()) {
            where = u"r.receipt_num = ?"_s;
            args << m.captured(1).toLongLong();
        } else if (upc.match(qy).hasMatch()) {
            // ILSA kept codes as numbers (a UPC's leading 0 lost): either way.
            QString bare = qy;
            while (bare.startsWith(u'0'))
                bare.remove(0, 1);
            where = u"r.id IN (SELECT receipt_id FROM legacy_lines WHERE upc IN (?, ?, ?))"_s;
            args << qy << bare << (u"0"_s + bare);
        } else if (const auto m = amount.match(qy); m.hasMatch()) {
            where = u"r.total_cents = ?"_s;
            args << (m.captured(1).toLongLong() * 100 + m.captured(2).toLongLong());
        } else {
            QString w = qy;
            w.replace(u'\\', u"\\\\"_s).replace(u'%', u"\\%"_s).replace(u'_', u"\\_"_s);
            where = u"r.id IN (SELECT receipt_id FROM legacy_lines WHERE name LIKE ? ESCAPE '\\')"_s;
            args << (u'%' + w + u'%');
        }
        args << limit << offset;
        Rows out;
        QSqlQuery q(db);
        if (run(q, u"SELECT %1 FROM legacy_receipts r LEFT JOIN legacy_sources s ON s.id = r.source_id WHERE %2 "
                    "ORDER BY r.day DESC, r.at DESC, r.receipt_num DESC LIMIT ? OFFSET ?"_s.arg(kReceiptColumns, where),
                args, error)) {
            while (q.next())
                out.push_back(receiptOf(q));
        }
        return out;
    });
}

std::optional<Receipt> receipt(const QString &path, std::int64_t id, QString *error)
{
    using R = std::optional<Receipt>;
    return withHistory<R>(path, error, R{}, [&](QSqlDatabase &db) -> R {
        Receipt r;
        {
            QSqlQuery q(db);
            if (!run(q, u"SELECT %1 FROM legacy_receipts r LEFT JOIN legacy_sources s ON s.id = r.source_id WHERE r.id = ?"_s
                            .arg(kReceiptColumns), {qint64(id)}, error) || !q.next())
                return std::nullopt;
            r = receiptOf(q);
        }
        {
            QSqlQuery q(db);
            if (run(q, u"SELECT name, upc, kind, dept, qty, unit_cents, amount_cents, voided, returned FROM legacy_lines "
                        "WHERE receipt_id = ? ORDER BY seq"_s, {qint64(id)}, error)) {
                while (q.next()) {
                    ReceiptLine l;
                    l.name = text(q, 0);
                    l.upc = text(q, 1);
                    l.kind = text(q, 2);
                    l.dept = q.value(3).toInt();
                    l.qty = q.value(4).toDouble();
                    l.unit = cents(q, 5);
                    l.amount = cents(q, 6);
                    l.voided = q.value(7).toInt() != 0;
                    l.returned = q.value(8).toInt() != 0;
                    r.lines.push_back(std::move(l));
                }
            }
        }
        {
            QSqlQuery q(db);
            if (run(q, u"SELECT type, name, amount_cents, change_cents FROM legacy_tenders WHERE receipt_id = ? ORDER BY seq"_s,
                    {qint64(id)}, error)) {
                while (q.next()) {
                    ReceiptTender t;
                    t.type = q.value(0).toInt();
                    t.name = text(q, 1);
                    t.amount = cents(q, 2);
                    t.change = cents(q, 3);
                    r.tenders.push_back(std::move(t));
                }
            }
        }
        return r;
    });
}

} // namespace vt::app::legacy
