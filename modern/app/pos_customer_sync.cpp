// TEMPORARY: remove this file (and everything marked "remove when the corporate customer list exists") once the
// corporate Pi keeps one customer list for all the stores ("One customer across all stores" in
// vt-modern-development-notes.md). Until then (owner 2026-11-26) the stores share their customers store to store.
//
// Store Settings > Other stores > Share customers turns it on for one store (both stores turn it on for each
// other). A change made here to a customer's name, phone, email, address, note, "<store> price" switch or
// preferential percent goes to each such store as a `viewtouch-customers` v1 file through the transfers outbox
// (pos_transfers.cpp; vt-send-transfers carries it; it waits there while the link is down). Each store reads its
// inbox every 20 seconds and merges by the phone's digits (core/customer_sync.hh): per field, the newest change
// wins; nothing is ever deleted; the same phone with a clearly different name waits on Manager > Customers >
// Check these customers for a manager to merge or keep apart. What came from another store is never sent on (no
// loops). House accounts, points, visits, spent and the last order stay at each store. Customers without a phone
// number aren't shared. Send All Customers Now (Manager > Customers) is the first share. Design:
// docs/MASS_UPDATES_STORES.md.

#include "app/i18n.hh"
#include "app/pos_json.hh"
#include "app/pos_service.hh"
#include "core/customer_sync.hh"
#include "core/transfer.hh"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;
namespace csync = vt::core::csync;

namespace vt::app {

struct CustomerSyncState {
    bool loaded = false;
    bool applying = false;   // merging another store's change: not a change made here (never sent on)
    int seq = 0;
    struct Entry {
        int version = 0;     // goes up with each change made here (the other stores skip one they had)
        csync::Fields fields;
    };
    std::map<std::string, Entry> entries;   // our customer id ->
    struct Link {
        std::string local;   // our customer id
        int version = 0;     // the last of theirs taken
    };
    std::map<std::string, Link> links;      // "<STORE>:<their customer id>" ->
    struct Conflict {
        std::string key, from, localId;
        QJsonObject customer;                // as it came
        std::int64_t at = 0;
    };
    std::vector<Conflict> conflicts;
};

namespace {

constexpr const char *kFormat = "viewtouch-customers";
constexpr int kPerFile = 500;   // Send All: customers a file

enum Took { Skipped, Merged, Added, ToCheck };

QJsonObject fieldsJson(const csync::Fields &fields, const std::string &store)
{
    QJsonObject o;
    for (const auto &[name, f] : fields)
        if (!f.value.empty() || f.at > 0)
            o.insert(qs(name), QJsonObject{{u"value"_s, qs(f.value)}, {u"at"_s, qint64(f.at)},
                                           {u"from"_s, qs(f.from.empty() ? store : f.from)}});
    return o;
}

csync::Fields fieldsFrom(const QJsonObject &o, const std::string &sender)
{
    csync::Fields out;
    for (const std::string &n : csync::fieldNames()) {
        const QJsonObject f = o.value(qs(n)).toObject();
        if (f.isEmpty())
            continue;
        const std::string from = ss(f.value(u"from").toString());
        out[n] = {ss(f.value(u"value").toString()), f.value(u"at").toInteger(), from.empty() ? sender : from};
    }
    return out;
}

QString fieldValue(const QJsonObject &customer, const char16_t *name)
{
    return customer.value(u"fields").toObject().value(QString::fromUtf16(name)).toObject().value(u"value").toString();
}

} // namespace

QStringList PosService::customerSyncStores() const
{
    QStringList out;
    for (const OtherStore &o : s_->settings.otherStores)
        if (o.shareCustomers && !xfer::storeKey(o.name).empty())
            out << qs(xfer::storeKey(o.name));
    return out;
}

CustomerSyncState &PosService::customerSyncState(const std::string &skipId)
{
    if (!s_->customerSync)
        s_->customerSync = std::make_shared<CustomerSyncState>();
    CustomerSyncState &st = *s_->customerSync;
    if (st.loaded)
        return st;
    st.loaded = true;
    if (!s_->transferDir.isEmpty()) {
        QFile f(transferPath(u"customer-sync.json"_s));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            const QJsonObject entries = o.value(u"entries").toObject();
            for (auto it = entries.begin(); it != entries.end(); ++it) {
                const QJsonObject e = it.value().toObject();
                st.entries[ss(it.key())] = {e.value(u"version").toInt(), fieldsFrom(e.value(u"fields").toObject(), {})};
            }
            const QJsonObject links = o.value(u"links").toObject();
            for (auto it = links.begin(); it != links.end(); ++it) {
                const QJsonObject l = it.value().toObject();
                st.links[ss(it.key())] = {ss(l.value(u"local").toString()), l.value(u"version").toInt()};
            }
            for (const QJsonValue &v : o.value(u"conflicts").toArray()) {
                const QJsonObject c = v.toObject();
                st.conflicts.push_back({ss(c.value(u"key").toString()), ss(c.value(u"from").toString()),
                                        ss(c.value(u"localId").toString()), c.value(u"customer").toObject(),
                                        c.value(u"at").toInteger()});
            }
        }
    }
    // Customers on file before this (or added without a save here): as they are, changed when they were added.
    // Nothing is sent for them until they change, or Send All Customers Now.
    bool added = false;
    for (const CustomerRecord &c : s_->customers)
        if (c.id != skipId && !st.entries.contains(c.id)) {
            csync::noteLocal(st.entries[c.id].fields, c, c.createdAt, ss(storeCode()));
            added = true;
        }
    if (added)
        customerSyncSave();
    return st;
}

void PosService::customerSyncSave()
{
    if (s_->transferDir.isEmpty() || !s_->customerSync)
        return;
    const CustomerSyncState &st = *s_->customerSync;
    QJsonObject entries, links;
    for (const auto &[id, e] : st.entries)
        entries.insert(qs(id), QJsonObject{{u"version"_s, e.version}, {u"fields"_s, fieldsJson(e.fields, {})}});
    for (const auto &[key, l] : st.links)
        links.insert(qs(key), QJsonObject{{u"local"_s, qs(l.local)}, {u"version"_s, l.version}});
    QJsonArray conflicts;
    for (const CustomerSyncState::Conflict &c : st.conflicts)
        conflicts.append(QJsonObject{{u"key"_s, qs(c.key)}, {u"from"_s, qs(c.from)}, {u"localId"_s, qs(c.localId)},
                                     {u"customer"_s, c.customer}, {u"at"_s, qint64(c.at)}});
    QDir().mkpath(s_->transferDir);
    QSaveFile f(transferPath(u"customer-sync.json"_s));
    if (!f.open(QIODevice::WriteOnly)
        || f.write(QJsonDocument(QJsonObject{{u"note"_s, u"Temporary: customers shared store to store (pos_customer_sync.cpp)"_s},
                                             {u"entries"_s, entries}, {u"links"_s, links}, {u"conflicts"_s, conflicts}})
                       .toJson(QJsonDocument::Compact)) < 0
        || !f.commit())
        qWarning().noquote() << "Customers: could not write" << f.fileName() << f.errorString();
}

QJsonObject PosService::customerSyncJson(const CustomerRecord &c, CustomerSyncState &st)
{
    CustomerSyncState::Entry &e = st.entries[c.id];
    if (e.fields.empty())
        csync::noteLocal(e.fields, c, c.createdAt, ss(storeCode()));
    std::int64_t changed = 0;
    for (const auto &[n, f] : e.fields)
        changed = std::max(changed, f.at);
    // Subscription (and gift) cards linked to the customer: the other store links its copy of a card, if it has one.
    QJsonArray cards;
    for (const GiftCard &g : s_->giftCards)
        if (g.customerId == c.id && !g.closed)
            cards.append(qs(g.number));
    return {{u"id"_s, qs(c.id)}, {u"version"_s, e.version}, {u"changedAt"_s, qint64(changed)},
            {u"fields"_s, fieldsJson(e.fields, ss(storeCode()))}, {u"cards"_s, cards}};
}

// A customer saved here (saveCustomerRecord): the fields that changed go to the stores that share customers.
void PosService::customerSyncNoteLocal(const CustomerRecord &c)
{
    if (!s_->customerSync && customerSyncStores().isEmpty())
        return;   // never shared: nothing to keep (the first share starts from each customer as it is)
    CustomerSyncState &st = customerSyncState(c.id);
    if (st.applying)
        return;   // another store's change: never sent on
    const bool known = st.entries.contains(c.id);
    CustomerSyncState::Entry &e = st.entries[c.id];
    if (!known)
        e = {};   // new here: every field it has is a change
    if (csync::noteLocal(e.fields, c, now(), ss(storeCode())).empty())
        return;   // visits, points, the account...: those stay here
    ++e.version;
    customerSyncSave();
    const QStringList stores = customerSyncStores();
    if (stores.isEmpty() || storeCode().isEmpty() || csync::phoneKey(c.phone).empty())
        return;
    const QVariantMap file{{u"format"_s, QString::fromLatin1(kFormat)}, {u"version"_s, 1}, {u"store"_s, storeCode()},
                           {u"at"_s, qint64(now())},
                           {u"customers"_s, QJsonArray{customerSyncJson(c, st)}.toVariantList()}};
    const QString name = u"customers-%1-%2-%3.json"_s.arg(storeCode()).arg(now()).arg(++st.seq);
    for (const QString &store : stores)
        putInOutbox(store, name, file);
}

bool PosService::customerSyncSendAll()
{
    if (!require(perm::Manager, tr("Sharing customers")))
        return false;
    const QStringList stores = customerSyncStores();
    if (stores.isEmpty())
        return fail(tr("No store shares customers with this one: Store Settings → Other stores → Share customers."));
    if (storeCode().isEmpty())
        return fail(tr("Set this store's short name first: Store Settings → Other stores."));
    CustomerSyncState &st = customerSyncState();
    QJsonArray all;
    for (const CustomerRecord &c : s_->customers)
        if (!csync::phoneKey(c.phone).empty())
            all.append(customerSyncJson(c, st));
    if (all.isEmpty())
        return fail(tr("No customers with a phone number to send."));
    customerSyncSave();
    const qint64 at = now();
    for (qsizetype from = 0, part = 1; from < all.size(); from += kPerFile, ++part) {
        QJsonArray chunk;
        for (qsizetype i = from; i < std::min<qsizetype>(all.size(), from + kPerFile); ++i)
            chunk.append(all.at(i));
        const QVariantMap file{{u"format"_s, QString::fromLatin1(kFormat)}, {u"version"_s, 1}, {u"store"_s, storeCode()},
                               {u"at"_s, at}, {u"all"_s, true}, {u"customers"_s, chunk.toVariantList()}};
        const QString name = u"customers-%1-all-%2-%3.json"_s.arg(storeCode()).arg(at).arg(part);
        for (const QString &store : stores)
            putInOutbox(store, name, file);
    }
    logChange(tr("Customers"), tr("All customers"), tr("Sent to"), QString(), stores.join(u", "_s));
    emit notice(tr("%1 customers sent to %2 (they go as soon as the link is up).").arg(all.size()).arg(stores.join(u", "_s)));
    return true;
}

// One customer from another store: merged into ours with the same phone (or the one linked before), added as
// new, or kept to check. `intoId`: a manager said it's this customer of ours; `apart`: a manager said it's
// someone else (a customer of its own here).
int PosService::customerSyncTake(CustomerSyncState &st, const QString &from, const QJsonObject &in, const std::string &intoId,
                                 bool apart)
{
    const std::string theirId = ss(in.value(u"id").toString());
    if (theirId.empty())
        return Skipped;
    const std::string key = ss(from) + ":" + theirId;
    const int version = in.value(u"version").toInt();
    const csync::Fields fields = fieldsFrom(in.value(u"fields").toObject(), ss(from));
    const auto value = [&](const char *n) {
        const auto it = fields.find(n);
        return it == fields.end() ? std::string() : it->second.value;
    };
    CustomerRecord *c = nullptr;
    if (!intoId.empty()) {
        c = s_->customer(intoId);
        if (!c)
            return Skipped;
    } else if (!apart) {
        if (const auto link = st.links.find(key); link != st.links.end()) {
            if (version <= link->second.version)
                return Skipped;   // had it
            c = s_->customer(link->second.local);
            if (!c) {   // forgotten here: it stays forgotten
                link->second.version = version;
                return Skipped;
            }
        } else {
            const std::string phone = csync::phoneKey(value("phone"));
            if (phone.empty())
                return Skipped;
            bool samePhone = false;
            std::string firstId;
            for (CustomerRecord &r : s_->customers) {
                if (csync::phoneKey(r.phone) != phone)
                    continue;
                if (!samePhone)
                    firstId = r.id;
                samePhone = true;
                if (csync::sameName(r.name, value("name"))) {
                    c = &r;
                    break;
                }
            }
            if (samePhone && !c) {   // the same phone, clearly someone else's name: a manager decides
                const auto old = std::ranges::find_if(st.conflicts, [&](const auto &x) { return x.key == key; });
                CustomerSyncState::Conflict cf{key, ss(from), firstId, in, now()};
                if (old != st.conflicts.end())
                    *old = cf;
                else
                    st.conflicts.push_back(cf);
                return ToCheck;
            }
        }
    }
    std::erase_if(st.conflicts, [&](const CustomerSyncState::Conflict &x) { return x.key == key; });
    bool added = false;
    if (!c) {
        CustomerRecord fresh;
        fresh.id = "c" + std::to_string(now()) + "-" + std::to_string(QRandomGenerator::global()->bounded(1000, 9999));
        fresh.createdAt = now();
        s_->customers.push_back(fresh);
        c = &s_->customers.back();
        st.entries[c->id] = {};
        added = true;
    }
    if (!st.entries.contains(c->id))
        csync::noteLocal(st.entries[c->id].fields, *c, c->createdAt, ss(storeCode()));
    CustomerSyncState::Entry &e = st.entries[c->id];
    const std::vector<std::string> taken = csync::merge(e.fields, fields);
    for (const std::string &n : taken)
        csync::setValue(*c, n, e.fields[n].value);
    CustomerSyncState::Link &link = st.links[key];
    link.local = c->id;
    link.version = std::max(link.version, version);
    st.applying = true;
    for (const QJsonValue &v : in.value(u"cards").toArray())
        if (GiftCard *g = s_->giftCard(ss(v.toString())); g && g->customerId.empty()) {
            g->customerId = c->id;
            saveGiftCardRecord(*g);
        }
    if (added || !taken.empty())
        saveCustomerRecord(*c);
    st.applying = false;
    return added ? Added : taken.empty() ? Skipped : Merged;
}

QString PosService::customerSyncIncoming(const QJsonObject &o, QString *why)
{
    const auto refuse = [&](const QString &text) {
        if (why)
            *why = text;
        return QString();
    };
    if (o.value(u"version").toInt() != 1)
        return refuse(tr("not a ViewTouch customers file"));
    const QString from = qs(xfer::storeKey(ss(o.value(u"store").toString())));
    if (from.isEmpty() || from == storeCode() || !customerSyncStores().contains(from))
        return refuse(tr("customers from %1, which doesn't share customers with this store (Store Settings > Other stores)")
                          .arg(from));
    CustomerSyncState &st = customerSyncState();
    int added = 0, merged = 0, toCheck = 0;
    for (const QJsonValue &v : o.value(u"customers").toArray()) {
        switch (customerSyncTake(st, from, v.toObject(), {}, false)) {
        case Added: ++added; break;
        case Merged: ++merged; break;
        case ToCheck: ++toCheck; break;
        default: break;
        }
    }
    customerSyncSave();
    if (toCheck > 0)
        emit notice(tr("Customers from %1: %2 to check (Manager → Customers → Check These Customers).").arg(from).arg(toCheck));
    if (added + merged + toCheck > 0)
        emit s_->customersChanged();
    return {};
}

bool PosService::customerSyncResolve(const QString &key, const QString &choice)
{
    if (!require(perm::Manager, tr("Sharing customers")))
        return false;
    CustomerSyncState &st = customerSyncState();
    const auto it = std::ranges::find_if(st.conflicts, [&](const auto &x) { return qs(x.key) == key; });
    if (it == st.conflicts.end())
        return fail(tr("That customer was already checked."));
    const CustomerSyncState::Conflict cf = *it;
    const QString theirName = fieldValue(cf.customer, u"name");
    const CustomerRecord *here = s_->customer(cf.localId);
    if (choice == u"merge") {
        if (!here)
            return fail(tr("That customer isn't on file here any more: keep them apart."));
        const QString hereName = qs(here->name);
        customerSyncTake(st, qs(cf.from), cf.customer, cf.localId, false);
        logChange(tr("Customers"), hereName, tr("Merged with %1's").arg(qs(cf.from)), hereName, theirName);
        emit notice(tr("Merged: one customer at both stores."));
    } else if (choice == u"apart") {
        customerSyncTake(st, qs(cf.from), cf.customer, {}, true);
        logChange(tr("Customers"), theirName, tr("Kept apart from %1's").arg(qs(cf.from)), QString(), theirName);
        emit notice(tr("Kept apart: %1 is a customer of their own here.").arg(theirName));
    } else {
        return fail(tr("Merge or keep apart."));
    }
    customerSyncSave();
    emit s_->customersChanged();
    return true;
}

QVariantMap PosService::customerSync() const
{
    if (!user() || !can(u"manager"_s))
        return {};
    const QStringList stores = customerSyncStores();
    const CustomerSyncState *st = s_->customerSync.get();
    if (stores.isEmpty() && (!st || st->conflicts.empty()))
        return {};
    QVariantList conflicts;
    if (st)
        for (const CustomerSyncState::Conflict &cf : st->conflicts) {
            const CustomerRecord *here = s_->customer(cf.localId);
            conflicts.append(QVariantMap{
                {u"key"_s, qs(cf.key)}, {u"from"_s, qs(cf.from)},
                {u"phone"_s, fieldValue(cf.customer, u"phone")},
                {u"hereName"_s, here ? qs(here->name) : QString()}, {u"hereEmail"_s, here ? qs(here->email) : QString()},
                {u"herePhone"_s, here ? qs(here->phone) : QString()}, {u"hereVisits"_s, here ? here->visits : 0},
                {u"thereName"_s, fieldValue(cf.customer, u"name")}, {u"thereEmail"_s, fieldValue(cf.customer, u"email")},
                {u"thereAddress"_s, fieldValue(cf.customer, u"address")},
                {u"at"_s, i18n::locale().toString(QDateTime::fromMSecsSinceEpoch(cf.at), u"MMM d, h:mm AP"_s)}});
        }
    const QString note = storeCode().isEmpty()
        ? tr("Customers can't be shared until this store has a short name (Store Settings → Other stores).")
        : stores.isEmpty() ? QString()
                           : tr("Shared with %1: name, phone, email, address, note and prices. House accounts, points and "
                                "visits stay here; customers without a phone number aren't shared.")
                                 .arg(stores.join(u", "_s));
    return {{u"on"_s, !stores.isEmpty() && !storeCode().isEmpty()}, {u"stores"_s, stores.join(u", "_s)},
            {u"storeCode"_s, storeCode()}, {u"note"_s, note}, {u"conflicts"_s, conflicts}};
}

} // namespace vt::app
