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
                                "r.cashier, r.age_verified, r.dob, COALESCE(s.name, ''), COALESCE(c.name, ''), %1"_s;

bool hasTable(QSqlDatabase &db, const QString &name)
{
    QSqlQuery q(db);
    return run(q, u"SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?"_s, {name}, nullptr) && q.next();
}

bool hasColumn(QSqlDatabase &db, const QString &table, const QString &column)
{
    QSqlQuery q(db);
    return run(q, u"SELECT 1 FROM pragma_table_info('%1') WHERE name = ?"_s.arg(table), {column}, nullptr) && q.next();
}

// The copy's store (its check prefix): legacy_sources.store, or BHDAN for a history file from before
// it was kept (all ILSA data then came from Butthead's Danbury).
QString prefixColumn(QSqlDatabase &db)
{
    return hasColumn(db, u"legacy_sources"_s, u"store"_s) ? u"COALESCE(s.store, '')"_s : u"'BHDAN'"_s;
}

// Only some stores' copies: `store` "BHDAN" or "BHDAN,BHNM" (empty: every store's; "-": none):
// " AND <sourceExpr> IN (...)" and its arguments.
struct Scope {
    QString sql;
    QVariantList args;
};
QStringList storeList(const QString &store)
{
    QStringList out;
    for (const QString &x : store.split(u',', Qt::SkipEmptyParts))
        if (!x.trimmed().isEmpty() && x.trimmed() != u"-")
            out << x.trimmed().toUpper();
    return out;
}
Scope storeScope(QSqlDatabase &db, const QString &store, const QString &sourceExpr, const QString &table = u"legacy_sources"_s,
                 const QString &column = u"store"_s)
{
    if (store.trimmed().isEmpty())
        return {};
    const QStringList list = storeList(store);
    if (list.isEmpty())
        return {u" AND 0"_s, {}};
    if (!hasColumn(db, table, column))   // before stores were kept: everything is BHDAN's
        return {list.contains(u"BHDAN"_s) ? QString() : u" AND 0"_s, {}};
    QStringList marks;
    QVariantList args;
    for (const QString &s : list) {
        marks << u"?"_s;
        args << s;
    }
    if (table == u"legacy_days")
        return {u" AND %1 IN (%2)"_s.arg(sourceExpr, marks.join(u", "_s)), args};
    return {u" AND %1 IN (SELECT id FROM legacy_sources WHERE store IN (%2))"_s.arg(sourceExpr, marks.join(u", "_s)), args};
}


// Receipts with their copy's name and their cashier's name (when the history file keeps cashiers).
QString receiptFrom(QSqlDatabase &db)
{
    return hasTable(db, u"legacy_cashiers"_s)
               ? u"legacy_receipts r LEFT JOIN legacy_sources s ON s.id = r.source_id "
                 "LEFT JOIN legacy_cashiers c ON c.source_id = r.source_id AND c.cashier_num = r.cashier"_s
               : u"legacy_receipts r LEFT JOIN legacy_sources s ON s.id = r.source_id "
                 "LEFT JOIN (SELECT NULL AS name) c ON 0"_s;
}

// Cashier names and ties, or nothing, for a history file from before they were kept.
QString cashierJoins(QSqlDatabase &db, QString *name, QString *employeeId, QString *employeeName)
{
    QString joins;
    *name = u"''"_s;
    *employeeId = u"''"_s;
    *employeeName = u"''"_s;
    if (hasTable(db, u"legacy_cashiers"_s)) {
        joins += u" LEFT JOIN legacy_cashiers c ON c.source_id = r.source_id AND c.cashier_num = r.cashier"_s;
        *name = u"COALESCE(MAX(c.name), '')"_s;
    }
    if (hasTable(db, u"legacy_cashier_map"_s)) {
        joins += u" LEFT JOIN legacy_cashier_map m ON m.source_id = r.source_id AND m.cashier_num = r.cashier"_s;
        *employeeId = u"COALESCE(MAX(m.employee_id), '')"_s;
        *employeeName = u"COALESCE(MAX(m.employee_name), '')"_s;
    }
    return joins;
}

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
    r.cashierName = text(q, 12);
    r.prefix = text(q, 13);
    return r;
}

} // namespace

QString historyPath(const QString &posDatabase)
{
    return QFileInfo(posDatabase).absoluteDir().filePath(u"ilsa-history.db"_s);
}

Status status(const QString &path, QString *error, const QString &store)
{
    return withHistory<Status>(path, error, Status{}, [&](QSqlDatabase &db) {
        Status s;
        QSqlQuery q(db);
        const Scope ds = storeScope(db, store, u"store"_s, u"legacy_days"_s);
        if (run(q, u"SELECT MIN(day), MAX(day), COUNT(*), COALESCE(SUM(receipts), 0) FROM legacy_days WHERE 1%1"_s.arg(ds.sql), ds.args, error)
            && q.next()) {
            s.first = q.value(0).toString();
            s.last = q.value(1).toString();
            s.days = q.value(2).toLongLong();
            s.receipts = q.value(3).toLongLong();
            s.present = s.days > 0;
        }
        return s;
    });
}

QStringList stores(const QString &path, QString *error)
{
    return withHistory<QStringList>(path, error, QStringList{}, [&](QSqlDatabase &db) {
        QStringList out;
        if (!hasColumn(db, u"legacy_sources"_s, u"store"_s))
            return QStringList{u"BHDAN"_s};
        QSqlQuery q(db);
        if (run(q, u"SELECT DISTINCT store FROM legacy_sources WHERE store <> '' ORDER BY store"_s, {}, error))
            while (q.next())
                out << q.value(0).toString();
        return out;
    });
}

ZTotals totals(const QString &path, const QString &from, const QString &to, QString *error, const QString &store)
{
    return withHistory<ZTotals>(path, error, ZTotals{}, [&](QSqlDatabase &db) {
        ZTotals z;
        const Scope rs = storeScope(db, store, u"source_id"_s);
        const Scope ls = storeScope(db, store, u"(SELECT source_id FROM legacy_receipts WHERE id = legacy_lines.receipt_id)"_s);
        const Scope ts = storeScope(db, store, u"r.source_id"_s);
        {
            QSqlQuery q(db);
            if (run(q, u"SELECT COUNT(*), COALESCE(SUM(CASE WHEN mode = 0 THEN 1 ELSE 0 END), 0), "
                        "COALESCE(SUM(CASE WHEN mode = 0 THEN tax_cents ELSE 0 END), 0), "
                        "COALESCE(SUM(CASE WHEN mode = 0 THEN total_cents ELSE 0 END), 0), "
                        "COALESCE(SUM(CASE WHEN mode = 0 THEN disc_cents ELSE 0 END), 0) "
                        "FROM legacy_receipts WHERE day BETWEEN ? AND ?%1"_s.arg(rs.sql), QVariantList{from, to} + rs.args, error)
                && q.next()) {
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
                        "FROM legacy_lines WHERE day BETWEEN ? AND ? AND (mode = 0 OR kind = 'payout')%1 "
                        "GROUP BY kind, lottery"_s.arg(ls.sql), QVariantList{from, to} + ls.args, error)) {
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
                        "JOIN legacy_receipts r ON r.id = t.receipt_id WHERE r.day BETWEEN ? AND ?%1 "
                        "GROUP BY t.type ORDER BY t.type"_s.arg(ts.sql), QVariantList{from, to} + ts.args, error)) {
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

std::map<std::string, std::int64_t> ilsaZ(const QString &path, const QString &from, const QString &to, QString *error,
                                          const QString &store)
{
    using Map = std::map<std::string, std::int64_t>;
    return withHistory<Map>(path, error, Map{}, [&](QSqlDatabase &db) {
        Map out;
        QSqlQuery q(db);
        const Scope zs = storeScope(db, store, u"z.source_id"_s);
        if (run(q, u"SELECT v.name, SUM(v.cents) FROM legacy_z z JOIN legacy_z_values v ON v.z_id = z.id "
                    "WHERE z.day BETWEEN ? AND ? AND v.name LIKE 'ilsa:%'%1 GROUP BY v.name"_s.arg(zs.sql),
                QVariantList{from, to} + zs.args, error)) {
            while (q.next())
                out[text(q, 0).substr(5)] = q.value(1).toLongLong();
        }
        return out;
    });
}

std::vector<GroupRow> groups(const QString &path, const QString &from, const QString &to, const QString &by, bool lottery,
                             int limit, bool *more, QString *error, const QString &store)
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
        const Scope ls = storeScope(db, store, u"(SELECT source_id FROM legacy_receipts WHERE id = l.receipt_id)"_s);
        // Item lines; by department also its promotions (ILSA's buydowns) and line discounts.
        const QString sql = u"SELECT %1, SUM(CASE WHEN l.kind = 'item' THEN l.qty ELSE 0 END), "
                            "SUM(CASE WHEN l.kind = 'item' THEN l.amount_cents ELSE 0 END), "
                            "SUM(CASE WHEN l.kind = 'item' THEN l.cost_cents ELSE 0 END), "
                            "SUM(CASE WHEN l.kind = 'item' AND l.has_cost = 0 THEN l.amount_cents ELSE 0 END), "
                            "SUM(CASE WHEN l.kind = 'item' THEN 1 ELSE 0 END), "
                            "SUM(CASE WHEN l.kind = 'promo' THEN l.amount_cents ELSE 0 END), SUM(l.disc_cents) "
                            "FROM legacy_lines l LEFT JOIN legacy_depts d ON %4 "
                            "WHERE l.day BETWEEN ? AND ? AND l.kind IN (%5) AND l.mode = 0 AND l.lottery = ?%6 "
                            "GROUP BY %2 ORDER BY %3 LIMIT ?"_s.arg(select, group, order, join,
                                                                     depts ? u"'item', 'promo'"_s : u"'item'"_s, ls.sql);
        Rows out;
        QSqlQuery q(db);
        if (run(q, sql, QVariantList{from, to, lottery ? 1 : 0} + ls.args + QVariantList{cap + 1}, error)) {
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

std::vector<PeriodRow> periods(const QString &path, const QString &from, const QString &to, bool byYear, QString *error,
                               const QString &store)
{
    using Rows = std::vector<PeriodRow>;
    return withHistory<Rows>(path, error, Rows{}, [&](QSqlDatabase &db) {
        Rows out;
        QSqlQuery q(db);
        const Scope ls = storeScope(db, store, u"(SELECT source_id FROM legacy_receipts WHERE id = legacy_lines.receipt_id)"_s);
        const QString sql = u"SELECT substr(day, 1, %1), "
                            "SUM(CASE WHEN lottery = 0 AND kind = 'item' THEN qty ELSE 0 END), "
                            "SUM(CASE WHEN lottery = 0 AND kind = 'item' THEN amount_cents ELSE 0 END), "
                            "SUM(CASE WHEN lottery = 0 AND kind = 'item' THEN cost_cents ELSE 0 END), "
                            "SUM(CASE WHEN lottery = 0 AND kind = 'item' AND has_cost = 0 THEN amount_cents ELSE 0 END), "
                            "SUM(CASE WHEN lottery = 1 AND kind = 'item' THEN amount_cents ELSE 0 END), "
                            "SUM(CASE WHEN lottery = 0 AND kind = 'promo' THEN amount_cents ELSE 0 END), "
                            "SUM(CASE WHEN lottery = 0 THEN disc_cents ELSE 0 END) "
                            "FROM legacy_lines WHERE day BETWEEN ? AND ? AND kind IN ('item', 'promo') AND mode = 0%2 "
                            "GROUP BY 1 ORDER BY 1"_s.arg(byYear ? 4 : 7).arg(ls.sql);
        if (run(q, sql, QVariantList{from, to} + ls.args, error)) {
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

std::vector<Variance> variances(const QString &path, const QString &from, const QString &to, QString *error,
                                const QString &store)
{
    using Rows = std::vector<Variance>;
    return withHistory<Rows>(path, error, Rows{}, [&](QSqlDatabase &db) {
        Rows out;
        QSqlQuery q(db);
        const Scope vs = storeScope(db, store, u"z.source_id"_s);
        if (run(q, u"SELECT v.day, COALESCE(z.reg_num, 0), COALESCE(z.z_counter, 0), v.what, v.ilsa_cents, v.ours_cents "
                    "FROM legacy_variances v LEFT JOIN legacy_z z ON z.id = v.z_id "
                    "WHERE v.day BETWEEN ? AND ?%1 ORDER BY v.day, v.what"_s.arg(vs.sql), QVariantList{from, to} + vs.args, error)) {
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

std::vector<UnnamedItem> unnamedItems(const QString &path, const QString &from, const QString &to, QString *error,
                                      const QString &store)
{
    using Rows = std::vector<UnnamedItem>;
    return withHistory<Rows>(path, error, Rows{}, [&](QSqlDatabase &db) {
        Rows out;
        QSqlQuery probe(db);   // a history file from before names were looked up in the other copies
        if (!run(probe, u"SELECT name_src FROM legacy_lines LIMIT 1"_s, {}, nullptr))
            return out;
        QSqlQuery q(db);
        const Scope us = storeScope(db, store, u"(SELECT source_id FROM legacy_receipts WHERE id = legacy_lines.receipt_id)"_s);
        if (run(q, u"SELECT upc, MAX(name), COUNT(*), SUM(amount_cents), MIN(day), MAX(day) FROM legacy_lines "
                    "WHERE name_src = 'dept' AND upc <> '' AND kind = 'item' AND day BETWEEN ? AND ?%1 "
                    "GROUP BY upc ORDER BY SUM(amount_cents) DESC LIMIT 2000"_s.arg(us.sql), QVariantList{from, to} + us.args, error)) {
            while (q.next()) {
                UnnamedItem u;
                u.upc = text(q, 0);
                u.name = text(q, 1);
                u.lines = q.value(2).toLongLong();
                u.sales = cents(q, 3);
                u.first = text(q, 4);
                u.last = text(q, 5);
                out.push_back(std::move(u));
            }
        }
        return out;
    });
}

std::vector<Cashier> cashiers(const QString &path, QString *error, const QString &store)
{
    using Rows = std::vector<Cashier>;
    return withHistory<Rows>(path, error, Rows{}, [&](QSqlDatabase &db) {
        Rows out;
        QString name, id, ename;
        const QString joins = cashierJoins(db, &name, &id, &ename);
        const Scope cs = storeScope(db, store, u"r.source_id"_s);
        QSqlQuery q(db);
        if (run(q, u"SELECT r.source_id, COALESCE(MAX(s.name), ''), r.cashier, %1, %2, %3, "
                    "SUM(CASE WHEN r.mode = 0 THEN 1 ELSE 0 END), SUM(CASE WHEN r.mode = 0 THEN r.total_cents - r.tax_cents ELSE 0 END), "
                    "MIN(r.day), MAX(r.day) FROM legacy_receipts r LEFT JOIN legacy_sources s ON s.id = r.source_id%4 WHERE 1%5 "
                    "GROUP BY r.source_id, r.cashier ORDER BY r.cashier, r.source_id"_s.arg(name, id, ename, joins, cs.sql), cs.args, error)) {
            while (q.next()) {
                Cashier c;
                c.sourceId = q.value(0).toInt();
                c.source = text(q, 1);
                c.number = q.value(2).toInt();
                c.name = text(q, 3);
                c.employeeId = text(q, 4);
                c.employeeName = text(q, 5);
                c.receipts = q.value(6).toLongLong();
                c.sales = cents(q, 7);
                c.first = text(q, 8);
                c.last = text(q, 9);
                out.push_back(std::move(c));
            }
        }
        return out;
    });
}

bool setCashier(const QString &path, int sourceId, int number, const std::string &employeeId,
                const std::string &employeeName, QString *error, bool allStores)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        if (error)
            *error = u"No ILSA history has been imported yet (vt-ilsa-history)."_s;
        return false;
    }
    static QAtomicInt counter;
    const QString conn = u"vt-legacy-write-%1"_s.arg(counter.fetchAndAddRelaxed(1));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, conn);
        db.setDatabaseName(path);
        db.setConnectOptions(u"QSQLITE_BUSY_TIMEOUT=5000"_s);
        if (!db.open()) {
            if (error)
                *error = db.lastError().text();
        } else {
            QSqlQuery q(db);
            ok = run(q, u"CREATE TABLE IF NOT EXISTS legacy_cashier_map (source_id INTEGER NOT NULL, cashier_num INTEGER NOT NULL, "
                        "employee_id TEXT NOT NULL, employee_name TEXT NOT NULL DEFAULT '', PRIMARY KEY (source_id, cashier_num))"_s,
                     {}, error)
                 && db.transaction();
            if (ok) {
                QSqlQuery w(db);
                if (employeeId.empty()) {
                    ok = run(w, u"DELETE FROM legacy_cashier_map WHERE source_id = ? AND cashier_num = ?"_s, {sourceId, number}, error);
                } else {
                    ok = run(w, u"INSERT OR REPLACE INTO legacy_cashier_map VALUES (?, ?, ?, ?)"_s,
                             {sourceId, number, QString::fromStdString(employeeId), QString::fromStdString(employeeName)}, error);
                    // The same person in the other copies (the same number and name): this store's other
                    // copies, or every store's (allStores: the same cashier in all stores); a tie set there stays.
                    if (ok && hasTable(db, u"legacy_cashiers"_s)) {
                        const bool stores = hasColumn(db, u"legacy_sources"_s, u"store"_s);
                        const QString sameStore = allStores || !stores ? QString()
                            : u" AND (SELECT store FROM legacy_sources WHERE id = o.source_id) = "
                              "(SELECT store FROM legacy_sources WHERE id = me.source_id)"_s;
                        QSqlQuery same(db);
                        ok = run(same, u"INSERT OR IGNORE INTO legacy_cashier_map "
                                        "SELECT o.source_id, o.cashier_num, ?, ? FROM legacy_cashiers o "
                                        "JOIN legacy_cashiers me ON me.source_id = ? AND me.cashier_num = ? "
                                        "WHERE o.cashier_num = me.cashier_num AND o.source_id <> me.source_id "
                                        "AND lower(trim(o.name)) = lower(trim(me.name))%1"_s.arg(sameStore),
                                 {QString::fromStdString(employeeId), QString::fromStdString(employeeName), sourceId, number}, error);
                    }
                }
                ok = ok ? db.commit() : (db.rollback(), false);
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return ok;
}

std::vector<EmployeeItemRow> employeeItems(const QString &path, const QString &from, const QString &to, const QString &filter,
                                           QString *error, const QString &store)
{
    using Rows = std::vector<EmployeeItemRow>;
    return withHistory<Rows>(path, error, Rows{}, [&](QSqlDatabase &db) {
        Rows out;
        QString name, id, ename;
        const QString joins = cashierJoins(db, &name, &id, &ename);
        QVariantList args{from, to};
        const Scope es = storeScope(db, store, u"r.source_id"_s);
        QString where;
        if (!filter.trimmed().isEmpty()) {
            QString w = filter.trimmed();
            w.replace(u'!', u"!!"_s).replace(u'%', u"!%"_s).replace(u'_', u"!_"_s);
            w = u'%' + w + u'%';
            where = u" AND (l.name LIKE ? ESCAPE '!' OR l.upc LIKE ? ESCAPE '!' OR l.brand LIKE ? ESCAPE '!' "
                    "OR l.dept IN (SELECT dept FROM legacy_depts WHERE name LIKE ? ESCAPE '!'))"_s;
            args << w << w << w << w;
        }
        QSqlQuery q(db);
        if (run(q, u"SELECT r.source_id, r.cashier, %1, %2, %3, l.upc, l.name, SUM(l.qty), SUM(l.amount_cents) "
                    "FROM legacy_lines l JOIN legacy_receipts r ON r.id = l.receipt_id%4 "
                    "WHERE l.day BETWEEN ? AND ? AND l.kind = 'item' AND l.mode = 0 AND l.lottery = 0 AND l.voided = 0%5 "
                    "GROUP BY r.source_id, r.cashier, l.upc, l.name"_s.arg(name, id, ename, joins, where + es.sql), args + es.args, error)) {
            while (q.next()) {
                EmployeeItemRow e;
                Cashier c;
                c.sourceId = q.value(0).toInt();
                c.number = q.value(1).toInt();
                c.name = text(q, 2);
                const std::string employee = text(q, 3);
                e.key = employee.empty() ? "ilsa:" + std::to_string(c.sourceId) + ":" + std::to_string(c.number) : employee;
                e.who = employee.empty() ? cashierLabel(c) + " (former employee)" : text(q, 4);
                e.upc = text(q, 5);
                e.item = text(q, 6);
                e.qty = q.value(7).toDouble();
                e.sales = cents(q, 8);
                out.push_back(std::move(e));
            }
        }
        return out;
    });
}

std::vector<Receipt> findReceipts(const QString &path, const QString &query, int limit, int offset, QString *error,
                                  const QString &store)
{
    using Rows = std::vector<Receipt>;
    return withHistory<Rows>(path, error, Rows{}, [&](QSqlDatabase &db) {
        static const QRegularExpression day(u"^\\d{4}-\\d{2}-\\d{2}$"_s);
        // "836100", "#836100", "BHDAN-836100", "bhdan836100": that receipt (of that store's copies).
        static const QRegularExpression number(u"^#?([A-Za-z]{2,8})?[- ]?(\\d{1,7})$"_s);
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
            args << m.captured(2).toLongLong();
            if (!m.captured(1).isEmpty()) {
                where += u" AND upper(%1) = ?"_s.arg(prefixColumn(db));
                args << m.captured(1).toUpper();
            }
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
        const Scope fs = storeScope(db, store, u"r.source_id"_s);
        where += fs.sql;
        args += fs.args;
        args << limit << offset;
        Rows out;
        QSqlQuery q(db);
        if (run(q, u"SELECT %1 FROM %3 WHERE %2 "
                    "ORDER BY r.day DESC, r.at DESC, r.receipt_num DESC LIMIT ? OFFSET ?"_s.arg(kReceiptColumns.arg(prefixColumn(db)), where,
                                                                                         receiptFrom(db)),
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
            if (!run(q, u"SELECT %1 FROM %2 WHERE r.id = ?"_s.arg(kReceiptColumns.arg(prefixColumn(db)), receiptFrom(db)), {qint64(id)}, error)
                || !q.next())
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
