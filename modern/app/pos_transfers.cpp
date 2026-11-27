// PosService: inventory transfers between the stores (Manager > Inventory > Transfers; owner 2026-11-26). Retail
// items only (the bar's drinks, food and bottles never leave it): scanned or found, a quantity a line, sent to
// another store from Store Settings > Other stores. Sending takes them off this store's on hand at once, keeps a
// transfer "in transit", prints a slip and puts a file in <data>/transfers/outbox/<STORE>/. vt-send-transfers
// (started from here, detached: a slow link never holds the register) copies it into the other store's
// <data>/transfers/inbox/ over ssh, with the keys vt-update-stores uses. Each store looks in its inbox every
// 20 seconds: a transfer waits there to be counted in (Receive), and the count goes back to the sender as a
// result. Items are matched by UPC (core/transfer.hh). The same outbox and inbox carry the retail store's price
// changes to the bar (pos_mass_update.cpp). Design: docs/MASS_UPDATES_STORES.md.

#include "app/i18n.hh"
#include "app/pos_json.hh"
#include "app/pos_service.hh"
#include "core/mass_update.hh"
#include "core/transfer.hh"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using namespace vt::core;
namespace mass = vt::core::mass;
using vt::core::xfer::Line;
using vt::core::xfer::Transfer;

namespace vt::app {

namespace {

constexpr const char *kFormat = "viewtouch-transfer";
constexpr std::int64_t kSendEveryMs = 60'000;

std::int64_t costCentsOf(const MenuItem &m)
{
    for (const RecipeLine &rl : m.recipe)
        if (rl.ingredientId == "__cost__" && rl.quantity > 0.0)
            return std::llround(rl.quantity * 100.0);
    return 0;
}

QString qtyText(double q) { return QString::number(std::round(q * 1000) / 1000, 'g', 10); }

QJsonObject lineJson(const Line &l)
{
    return {{u"upc"_s, qs(l.upc)}, {u"name"_s, qs(l.name)}, {u"qty"_s, l.qty}, {u"cost"_s, qint64(l.costCents)},
            {u"price"_s, qint64(l.priceCents)}, {u"packUpc"_s, qs(l.packUpc)}, {u"packSize"_s, l.packSize},
            {u"category"_s, qs(l.category)}, {u"itemId"_s, qs(l.itemId)}, {u"perUnit"_s, l.perUnit},
            {u"received"_s, l.received}, {u"reason"_s, qs(l.reason)}};
}

Line lineFrom(const QJsonObject &o)
{
    Line l;
    l.upc = ss(o.value(u"upc").toString());
    l.name = ss(o.value(u"name").toString());
    l.qty = o.value(u"qty").toDouble();
    l.costCents = o.value(u"cost").toInteger();
    l.priceCents = o.value(u"price").toInteger();
    l.packUpc = ss(o.value(u"packUpc").toString());
    l.packSize = o.value(u"packSize").toInt();
    l.category = ss(o.value(u"category").toString());
    l.itemId = ss(o.value(u"itemId").toString());
    l.perUnit = o.value(u"perUnit").toDouble(1);
    l.received = o.value(u"received").toDouble(-1);
    l.reason = ss(o.value(u"reason").toString());
    return l;
}

} // namespace

QJsonObject toJson(const Transfer &t)
{
    QJsonArray lines;
    for (const Line &l : t.lines)
        lines.append(lineJson(l));
    return {{u"id"_s, qint64(t.id)}, {u"key"_s, qs(t.key)}, {u"number"_s, t.number}, {u"direction"_s, qs(t.direction)},
            {u"from"_s, qs(t.from)}, {u"to"_s, qs(t.to)}, {u"who"_s, qs(t.who)}, {u"note"_s, qs(t.note)},
            {u"at"_s, qint64(t.at)}, {u"status"_s, qs(t.status)}, {u"delivered"_s, t.delivered},
            {u"receivedAt"_s, qint64(t.receivedAt)}, {u"receivedBy"_s, qs(t.receivedBy)},
            {u"voidedAt"_s, qint64(t.voidedAt)}, {u"voidedBy"_s, qs(t.voidedBy)}, {u"lines"_s, lines}};
}

Transfer transferFromJson(const QJsonObject &o)
{
    Transfer t;
    t.id = o.value(u"id").toInteger();
    t.key = ss(o.value(u"key").toString());
    t.number = o.value(u"number").toInt();
    t.direction = ss(o.value(u"direction").toString());
    t.from = ss(o.value(u"from").toString());
    t.to = ss(o.value(u"to").toString());
    t.who = ss(o.value(u"who").toString());
    t.note = ss(o.value(u"note").toString());
    t.at = o.value(u"at").toInteger();
    t.status = ss(o.value(u"status").toString());
    t.delivered = o.value(u"delivered").toBool();
    t.receivedAt = o.value(u"receivedAt").toInteger();
    t.receivedBy = ss(o.value(u"receivedBy").toString());
    t.voidedAt = o.value(u"voidedAt").toInteger();
    t.voidedBy = ss(o.value(u"voidedBy").toString());
    for (const QJsonValue &v : o.value(u"lines").toArray())
        t.lines.push_back(lineFrom(v.toObject()));
    return t;
}

// --- one screen's -------------------------------------------------------------------------------------

struct PosService::TransferView {
    bool active = false;
    QString tab = u"out"_s;   // out | in | list
    struct Draft {
        std::string itemId;
        double qty = 1;
    };
    std::vector<Draft> lines;
    QString to, note;
    QVariantList choices;     // Find's answers (several items)
    QString message;
    // Receiving one (a record's id), with the counts typed so far.
    std::int64_t receiving = 0;
    std::vector<double> counts;
    std::vector<QString> reasons;
    std::vector<bool> checked;   // ticked off (touched or scanned)
    std::vector<int> add;     // a line not on file: -1 not decided, 0 left out, 1 added from the transfer
    QString from, until;      // the list's dates (yyyy-MM-dd; empty: the last 30 days)
};

PosService::TransferView &PosService::transferView() const
{
    if (!xfer_)
        xfer_ = std::make_shared<TransferView>();
    return *xfer_;
}

// --- the store's name and the other stores ----------------------------------------------------------

QString PosService::storeCode() const { return qs(xfer::storeKey(s_->settings.storeCode)); }

const OtherStore *PosService::otherStore(const QString &name) const
{
    const std::string k = xfer::storeKey(ss(name));
    for (const OtherStore &o : s_->settings.otherStores)
        if (!k.empty() && xfer::storeKey(o.name) == k)
            return &o;
    return nullptr;
}

// --- retail on hand -----------------------------------------------------------------------------------

void PosService::addRetailStock(const std::string &itemId, double delta)
{
    if (itemId.empty() || delta == 0)
        return;
    double &v = s_->retailStock[itemId];
    v = std::round((v + delta) * 1000) / 1000;
    if (s_->sink)
        s_->sink->saveRetailStock(itemId, v);
}

void PosService::retailStockSold(const std::vector<OrderLine> &lines, int sign)
{
    if (s_->retailStock.empty())
        return;
    for (const OrderLine &l : lines) {
        if ((l.voided && sign > 0) || l.isComment() || l.isGiftCard())
            continue;
        if (s_->retailStock.contains(l.itemId))
            addRetailStock(l.itemId, -double(l.counted()) * sign);
    }
}

std::optional<double> PosService::retailOnHand(const std::string &itemId) const
{
    const auto it = s_->retailStock.find(itemId);
    if (it == s_->retailStock.end())
        return std::nullopt;
    return it->second;
}

// --- files in and out ---------------------------------------------------------------------------------

QString PosService::transferPath(const QString &sub) const
{
    return s_->transferDir.isEmpty() ? QString() : QDir(s_->transferDir).filePath(sub);
}

bool PosService::putInOutbox(const QString &store, const QString &name, const QVariantMap &content)
{
    const QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(content)).toJson(QJsonDocument::Compact);
    const QString to = qs(xfer::storeKey(ss(store)));
    if (s_->transferDir.isEmpty()) {
        s_->transferMemoryOut[ss(to)][ss(name)] = bytes;
        return true;
    }
    const QString dir = transferPath(u"outbox/"_s + to);
    QDir().mkpath(dir);
    QSaveFile f(QDir(dir).filePath(name));
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) < 0 || !f.commit()) {
        qWarning().noquote() << "Transfers: could not write" << f.fileName() << f.errorString();
        return false;
    }
    startSending(true);
    return true;
}

bool PosService::inOutbox(const QString &store, const QString &name) const
{
    const QString to = qs(xfer::storeKey(ss(store)));
    if (s_->transferDir.isEmpty()) {
        const auto it = s_->transferMemoryOut.find(ss(to));
        return it != s_->transferMemoryOut.end() && it->second.contains(ss(name));
    }
    return QFileInfo::exists(QDir(transferPath(u"outbox/"_s + to)).filePath(name));
}

int PosService::waitingToSend() const
{
    int n = 0;
    if (s_->transferDir.isEmpty()) {
        for (const auto &[store, files] : s_->transferMemoryOut)
            n += int(files.size());
        return n;
    }
    const QDir out(transferPath(u"outbox"_s));
    for (const QString &d : out.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        n += int(QDir(out.filePath(d)).entryList({u"*.json"_s}, QDir::Files).size());
    return n;
}

void PosService::writeStoreList()
{
    if (s_->transferDir.isEmpty())
        return;
    // For vt-send-transfers: "NAME user@address on|off", one store a line.
    QByteArray text = "# Written by ViewTouch from Store Settings > Other stores; read by vt-send-transfers.\n";
    for (const OtherStore &o : s_->settings.otherStores)
        if (!xfer::storeKey(o.name).empty() && !o.host.empty())
            text += QByteArray::fromStdString(xfer::storeKey(o.name) + ' ' + o.host + (o.on ? " on" : " off")) + '\n';
    QDir().mkpath(s_->transferDir);
    const QString file = transferPath(u"stores.conf"_s);
    QFile was(file);
    if (was.open(QIODevice::ReadOnly) && was.readAll() == text)
        return;
    was.close();
    QSaveFile f(file);
    if (f.open(QIODevice::WriteOnly) && f.write(text) >= 0)
        f.commit();
}

void PosService::startSending(bool now)
{
    if (s_->transferDir.isEmpty() || s_->transferScript.isEmpty() || !QFileInfo(s_->transferScript).isExecutable())
        return;
    const std::int64_t t = QDateTime::currentMSecsSinceEpoch();
    if (!now && t - s_->transferSentAt < kSendEveryMs)
        return;
    if (t - s_->transferSentAt < 5'000)
        return;   // (just started: it takes what's there)
    if (waitingToSend() == 0)
        return;
    writeStoreList();
    s_->transferSentAt = t;
    if (!QProcess::startDetached(s_->transferScript, {s_->transferDir}))
        qWarning().noquote() << "Transfers: could not start" << s_->transferScript;
}

QString PosService::sendStatus() const
{
    // vt-send-transfers' last word: "ok" or "fail <STORE> <what it said>".
    QFile f(transferPath(u"send-status"_s));
    if (s_->transferDir.isEmpty() || !f.open(QIODevice::ReadOnly))
        return {};
    const QString line = QString::fromUtf8(f.readAll()).trimmed();
    if (!line.startsWith(u"fail"_s))
        return {};
    const QString when = i18n::locale().toString(QFileInfo(f).lastModified(), u"ddd h:mm AP"_s);
    return tr("Couldn't reach %1 (%2): the files wait here and go when the link is back.")
        .arg(line.section(u' ', 1, 1), when);
}

void PosService::transfersTick()
{
    if (s_->transferDir.isEmpty())
        return;
    writeStoreList();
    bool changedAny = false;
    const QDir in(transferPath(u"inbox"_s));
    // Price changes kept for a person, from before a restart.
    if (!s_->pricesWaitingLoaded) {
        s_->pricesWaitingLoaded = true;
        const QDir w(in.filePath(u"waiting"_s));
        for (const QString &name : w.entryList({u"*.json"_s}, QDir::Files, QDir::Name)) {
            QFile f(w.filePath(name));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            QVariantMap m = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
            m.insert(u"_file"_s, w.filePath(name));
            s_->pricesWaiting.push_back(m);
        }
    }
    // What came in.
    for (const QString &name : in.entryList({u"*.json"_s}, QDir::Files, QDir::Name)) {
        QFile f(in.filePath(name));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = f.readAll();
        f.close();
        QString why;
        const QString waiting = transferIncoming(bytes, &why);
        const QString sub = waiting == u"waiting" ? u"waiting"_s : why.isEmpty() ? u"done"_s : u"refused"_s;
        if (!why.isEmpty())
            qWarning().noquote() << "Transfers:" << name << why;
        QDir().mkpath(in.filePath(sub));
        QFile::remove(QDir(in.filePath(sub)).filePath(name));
        QFile::rename(in.filePath(name), QDir(in.filePath(sub)).filePath(name));
        if (sub == u"waiting" && !s_->pricesWaiting.empty())
            s_->pricesWaiting.back().insert(u"_file"_s, QDir(in.filePath(sub)).filePath(name));
        changedAny = true;
    }
    // What went out: a transfer whose file left the outbox reached the other store.
    for (Transfer &t : s_->transfers)
        if (t.direction == "out" && !t.delivered && !inOutbox(qs(t.to), u"transfer-"_s + qs(t.key) + u".json"_s)) {
            t.delivered = true;
            if (s_->sink)
                s_->sink->saveTransfer(t);
            changedAny = true;
        }
    startSending(false);
    if (changedAny) {
        emit adminChanged();
        emit s_->adminChanged();
    }
}

// A file from another store: a transfer, its result, its void, or a change set. "waiting": kept for a person
// (a change set while Store Settings says ask); "" done; `why` set when refused.
QString PosService::transferIncoming(const QByteArray &bytes, QString *why)
{
    const QJsonObject o = QJsonDocument::fromJson(bytes).object();
    const QString format = o.value(u"format").toString();
    if (format == u"viewtouch-change-set")
        return takeStoreChangeSet(o.toVariantMap(), why);
    if (format != QLatin1String(kFormat) || o.value(u"version").toInt() != 1) {
        if (why)
            *why = tr("not a ViewTouch transfer file");
        return {};
    }
    const QString kind = o.value(u"kind").toString();
    const Transfer sent = transferFromJson(o.value(u"transfer").toObject());
    if (xfer::storeKey(sent.key).empty()) {
        if (why)
            *why = tr("a transfer without its number");
        return {};
    }
    const auto known = [&](const char *direction) -> Transfer * {
        for (Transfer &t : s_->transfers)
            if (t.direction == direction && t.key == sent.key)
                return &t;
        return nullptr;
    };
    if (kind == u"transfer") {
        if (!otherStore(qs(sent.from))) {
            if (why)
                *why = tr("from %1, which isn't in Store Settings > Other stores").arg(qs(sent.from));
            return {};
        }
        if (known("in"))
            return {};   // (sent again)
        Transfer t = sent;
        t.id = ++s_->lastTransferId;
        t.direction = "in";
        t.status = "waiting";
        t.delivered = true;
        for (Line &l : t.lines) {
            l.received = -1;
            l.itemId.clear();
        }
        s_->transfers.push_back(t);
        if (s_->sink)
            s_->sink->saveTransfer(t);
        emit notice(tr("Transfer #%1 from %2 is here: Manager → Inventory → Transfers → Receive.").arg(t.number).arg(qs(t.from)));
    } else if (kind == u"result") {
        Transfer *t = known("out");
        if (!t)
            return {};
        const bool wasVoid = t->status == "void";
        for (std::size_t i = 0; i < t->lines.size() && i < sent.lines.size(); ++i) {
            t->lines[i].received = sent.lines[i].received;
            t->lines[i].reason = sent.lines[i].reason;
        }
        t->receivedAt = sent.receivedAt;
        t->receivedBy = sent.receivedBy;
        t->delivered = true;
        t->status = t->differences() > 0 ? "differences" : "received";
        if (wasVoid) {   // voided here after the other store had counted it in: it left after all
            for (const Line &l : t->lines)
                addRetailStock(l.itemId, -l.qty);
            t->note += t->note.empty() ? "" : " ";
            t->note += ss(tr("(Voided here, but %1 had already received it: taken out of stock again.)").arg(qs(t->to)));
        }
        if (s_->sink)
            s_->sink->saveTransfer(*t);
        emit notice(t->status == "received" ? tr("Transfer #%1: %2 received it.").arg(t->number).arg(qs(t->to))
                                            : tr("Transfer #%1: %2 received it with differences.").arg(t->number).arg(qs(t->to)));
    } else if (kind == u"void") {
        Transfer *t = known("in");
        if (t && t->status == "waiting") {
            t->status = "void";
            t->voidedAt = sent.voidedAt;
            t->voidedBy = sent.voidedBy;
            if (s_->sink)
                s_->sink->saveTransfer(*t);
        }
    } else {
        if (why)
            *why = tr("not a ViewTouch transfer file");
        return {};
    }
    emit adminChanged();
    emit s_->adminChanged();
    return {};
}

QVariantMap PosService::transferFile(const QString &kind, const Transfer &t) const
{
    return {{u"format"_s, QString::fromLatin1(kFormat)}, {u"version"_s, 1}, {u"kind"_s, kind},
            {u"store"_s, storeCode()}, {u"transfer"_s, toJson(t).toVariantMap()}};
}

// --- looking items up ---------------------------------------------------------------------------------

struct PosService::TransferIndex {
    std::uint64_t shared = 0;
    int revision = -1;
    std::size_t size = 0;
    mass::UpcIndex upc;
    std::unordered_map<std::string, std::pair<std::string, int>> packOf;   // a box's UPC key -> its single's UPC, count
};

const PosService::TransferIndex &PosService::transferIndex() const
{
    static TransferIndex x;
    if (x.shared == s_->serial && x.revision == s_->adminRevision && x.size == s_->menu.size())
        return x;
    x.shared = s_->serial;
    x.revision = s_->adminRevision;
    x.size = s_->menu.size();
    const auto &menu = s_->menu;
    x.upc.build(menu.size(), [&](std::size_t i) { return menu[i].barcodes; });
    x.packOf.clear();
    for (const MenuItem &m : menu)
        for (const RecipeLine &rl : m.recipe)
            if (rl.ingredientId.starts_with("__parent__:") && !m.barcodes.empty() && rl.quantity >= 1)
                x.packOf.try_emplace(BuilderIndex::codeKey(rl.ingredientId.substr(11)), m.barcodes.front(), int(rl.quantity));
    return x;
}

// A retail item for a scan or a typed UPC / item number (nullptr: none, or not a retail item).
const MenuItem *PosService::transferItemFor(const QString &code) const
{
    const TransferIndex &x = transferIndex();
    const std::int64_t at = x.upc.find(ss(code.trimmed()));
    if (at >= 0)
        return &s_->menu[std::size_t(at)];
    for (const MenuItem &m : s_->menu)
        if (!m.number.empty() && qs(m.number) == code.trimmed())
            return &m;
    return nullptr;
}

Line PosService::transferLineOf(const MenuItem &m, double qty) const
{
    Line l;
    l.upc = m.barcodes.empty() ? std::string() : m.barcodes.front();
    l.name = m.name;
    l.qty = qty;
    l.costCents = costCentsOf(m);
    l.priceCents = m.price.cents();
    l.itemId = m.id;
    for (const MenuCategory &c : s_->categories())
        if (c.id == m.family)
            l.category = c.name;
    const TransferIndex &x = transferIndex();
    for (const std::string &b : m.barcodes)
        if (const auto p = x.packOf.find(BuilderIndex::codeKey(b)); p != x.packOf.end()) {
            l.packUpc = p->second.first;
            l.packSize = p->second.second;
            break;
        }
    return l;
}

// --- the screen ---------------------------------------------------------------------------------------

bool PosService::transferOpen(const QString &tab)
{
    if (!user())
        return fail(tr("Log in first."));
    if (!can(u"manager"_s))
        return fail(tr("Transfers are for managers."));
    TransferView &v = transferView();
    v.active = true;
    if (tab == u"out" || tab == u"in" || tab == u"list")
        v.tab = tab;
    v.message.clear();
    if (v.to.isEmpty())
        for (const OtherStore &o : s_->settings.otherStores)
            if (o.on && v.to.isEmpty())
                v.to = qs(xfer::storeKey(o.name));
    emit adminChanged();
    return true;
}

bool PosService::transferClose()
{
    if (xfer_)
        xfer_->active = false;
    emit adminChanged();
    return true;
}

bool PosService::transferAddItem(const QString &itemId, double qty)
{
    TransferView &v = transferView();
    const MenuItem *m = findItem(itemId);
    if (!m || m->isModifier)
        return fail(tr("No item %1.").arg(itemId));
    if (!itemIsRetail(*m) || m->barcodes.empty())
        return fail(tr("%1 isn't a retail item with a UPC: only retail items go to another store.").arg(qs(m->name)));
    if (!(qty > 0) || qty > 100000)
        return fail(tr("Type how many, like 1 or 10."));
    v.choices.clear();
    v.message.clear();
    // The same item again: one more on its line (as a scan at the register).
    for (TransferView::Draft &d : v.lines)
        if (d.itemId == m->id) {
            d.qty += qty;
            emit adminChanged();
            return true;
        }
    v.lines.push_back({m->id, qty});
    emit adminChanged();
    return true;
}

bool PosService::transferScan(const QString &code)
{
    TransferView &v = transferView();
    if (!v.active)
        return false;
    const QString c = code.trimmed();
    if (v.tab == u"in" && v.receiving) {
        // Counting in: a scan ticks one more of that line.
        Transfer *t = transferRecord(v.receiving);
        const TransferIndex &x = transferIndex();
        const std::int64_t scanned = x.upc.find(ss(c));
        for (std::size_t i = 0; t && i < t->lines.size() && i < v.counts.size(); ++i) {
            const Line &l = t->lines[i];
            const bool same = BuilderIndex::codeKey(l.upc) == BuilderIndex::codeKey(ss(c))
                              || (scanned >= 0 && x.upc.find(l.upc) == scanned);
            if (!same)
                continue;
            v.checked[i] = true;   // (the count stays as sent until it's corrected)
            emit adminChanged();
            return true;
        }
        return fail(tr("%1 isn't on this transfer.").arg(c));
    }
    if (const MenuItem *m = transferItemFor(c))
        return transferAddItem(qs(m->id), 1);
    return transferFind(c);
}

bool PosService::transferFind(const QString &text)
{
    TransferView &v = transferView();
    const QString t = text.trimmed();
    v.choices.clear();
    if (t.isEmpty()) {
        emit adminChanged();
        return true;
    }
    if (const MenuItem *m = transferItemFor(t))
        return transferAddItem(qs(m->id), 1);
    const QString low = t.toLower();
    for (const MenuItem &m : s_->menu) {
        if (m.isModifier || m.barcodes.empty() || !itemIsRetail(m) || !qs(m.name).toLower().contains(low))
            continue;
        v.choices.append(QVariantMap{{u"id"_s, qs(m.id)}, {u"name"_s, qs(m.name)}, {u"upc"_s, qs(m.barcodes.front())},
                                     {u"price"_s, format(m.price)}});
        if (v.choices.size() >= 30)
            break;
    }
    v.message = v.choices.isEmpty() ? tr("No retail item has \"%1\" in its name or UPC.").arg(t) : QString();
    emit adminChanged();
    return !v.choices.isEmpty();
}

bool PosService::transferSetQty(int index, double qty)
{
    TransferView &v = transferView();
    if (index < 0 || index >= int(v.lines.size()))
        return false;
    if (qty <= 0) {
        v.lines.erase(v.lines.begin() + index);
    } else if (qty > 100000) {
        return fail(tr("Type how many, like 1 or 10."));
    } else {
        v.lines[std::size_t(index)].qty = qty;
    }
    emit adminChanged();
    return true;
}

bool PosService::transferSetTo(const QString &store)
{
    if (!otherStore(store))
        return fail(tr("%1 isn't in Store Settings > Other stores.").arg(store));
    transferView().to = qs(xfer::storeKey(ss(store)));
    emit adminChanged();
    return true;
}

bool PosService::transferSetNote(const QString &note)
{
    transferView().note = note.left(300);
    return true;
}

bool PosService::transferClear()
{
    TransferView &v = transferView();
    v.lines.clear();
    v.note.clear();
    v.choices.clear();
    v.message.clear();
    emit adminChanged();
    return true;
}

Transfer *PosService::transferRecord(std::int64_t id)
{
    for (Transfer &t : s_->transfers)
        if (t.id == id)
            return &t;
    return nullptr;
}

bool PosService::transferSend()
{
    TransferView &v = transferView();
    if (storeCode().isEmpty())
        return fail(tr("Give this store its short name first: Store Settings > Other stores > This store's short name."));
    const OtherStore *to = otherStore(v.to);
    if (!to || !to->on)
        return fail(tr("Choose the store it goes to (Store Settings > Other stores)."));
    if (xfer::storeKey(to->name) == ss(storeCode()))
        return fail(tr("That's this store."));
    if (v.lines.empty())
        return fail(tr("Scan or find the items first."));
    if (!require(perm::Manager, tr("Sending a transfer")))
        return false;
    Transfer t;
    t.id = ++s_->lastTransferId;
    int number = 0;
    for (const Transfer &o : s_->transfers)
        if (o.direction == "out")
            number = std::max(number, o.number);
    t.number = number + 1;
    t.from = ss(storeCode());
    t.to = xfer::storeKey(to->name);
    t.key = t.from + "-" + std::to_string(t.number);
    t.direction = "out";
    t.who = user() ? user()->name : ss(terminal_);
    t.note = ss(v.note.trimmed());
    t.at = now();
    t.status = "transit";
    for (const TransferView::Draft &d : v.lines)
        if (const MenuItem *m = findItem(qs(d.itemId)))
            t.lines.push_back(transferLineOf(*m, d.qty));
    if (t.lines.empty())
        return fail(tr("Scan or find the items first."));
    // Off the shelf here at once.
    for (const Line &l : t.lines)
        addRetailStock(l.itemId, -l.qty);
    t.delivered = false;
    s_->transfers.push_back(t);
    if (s_->sink)
        s_->sink->saveTransfer(t);
    putInOutbox(qs(t.to), u"transfer-"_s + qs(t.key) + u".json"_s, transferFile(u"transfer"_s, t));
    logChange(tr("Inventory"), tr("Transfer #%1 to %2").arg(t.number).arg(qs(t.to)), tr("Sent"), QString(),
              tr("%n line(s), %1 at cost", "", int(t.lines.size())).arg(format(Money::fromCents(t.sentCost()))));
    printTransferSlip(t);
    v.lines.clear();
    v.note.clear();
    v.choices.clear();
    v.message = tr("Transfer #%1 to %2: sent (%3 at cost). The slip is printing.")
                    .arg(t.number).arg(qs(t.to), format(Money::fromCents(t.sentCost())));
    emit notice(v.message);
    emit adminChanged();
    emit s_->adminChanged();
    return true;
}

bool PosService::transferVoid(qint64 id)
{
    Transfer *t = transferRecord(id);
    if (!t || t->direction != "out")
        return fail(tr("No such transfer."));
    if (t->status != "transit")
        return fail(tr("Transfer #%1 was already received (or voided): it can't be voided.").arg(t->number));
    if (!require(perm::Manager, tr("Voiding a transfer")))
        return false;
    t = transferRecord(id);
    for (const Line &l : t->lines)
        addRetailStock(l.itemId, l.qty);   // back on the shelf
    t->status = "void";
    t->voidedAt = now();
    t->voidedBy = user() ? user()->name : ss(terminal_);
    const QString file = u"transfer-"_s + qs(t->key) + u".json"_s;
    // Never sent: the file just goes; else the other store hears of it.
    bool unsent = false;
    if (s_->transferDir.isEmpty()) {
        if (auto it = s_->transferMemoryOut.find(t->to); it != s_->transferMemoryOut.end())
            unsent = it->second.erase(ss(file)) > 0;
    } else {
        unsent = QFile::remove(QDir(transferPath(u"outbox/"_s + qs(t->to))).filePath(file));
    }
    if (!unsent)
        putInOutbox(qs(t->to), u"void-"_s + qs(t->key) + u".json"_s, transferFile(u"void"_s, *t));
    t->delivered = true;
    if (s_->sink)
        s_->sink->saveTransfer(*t);
    logChange(tr("Inventory"), tr("Transfer #%1 to %2").arg(t->number).arg(qs(t->to)), tr("Voided"), QString(),
              tr("back in stock: %1 at cost").arg(format(Money::fromCents(t->sentCost()))));
    emit notice(tr("Transfer #%1 voided: the items are back in stock here.").arg(t->number));
    emit adminChanged();
    emit s_->adminChanged();
    return true;
}

bool PosService::transferReprint(qint64 id)
{
    const Transfer *t = transferRecord(id);
    if (!t)
        return fail(tr("No such transfer."));
    printTransferSlip(*t);
    return true;
}

Report PosService::transferSlip(const Transfer &t) const
{
    const ReportContext ctx = reportContext(QString());
    Report r;
    r.id = "transfer";
    r.title = ss(tr("Transfer #%1").arg(t.number));
    r.subtitle = ss(tr("%1 to %2").arg(qs(t.from), qs(t.to)));
    r.columns = {ss(tr("Item")), ss(tr("Qty")), ss(tr("Cost"))};
    r.note(ss(i18n::locale().toString(QDateTime::fromMSecsSinceEpoch(t.at), u"ddd MMM d, yyyy h:mm AP"_s)));
    r.note(ss(tr("Sent by %1").arg(qs(t.who))));
    if (!t.note.empty())
        r.note(ss(tr("Note: %1").arg(qs(t.note))));
    double units = 0;
    for (const Line &l : t.lines) {
        r.line({l.name, ss(qtyText(l.qty)), ctx.money(Money::fromCents(std::llround(l.qty * double(l.costCents))))});
        r.note(l.upc + (l.packSize > 0 ? ss(tr(" (%1 x %2)").arg(l.packSize).arg(qs(l.packUpc))) : std::string()));
        units += l.qty;
    }
    r.total({ss(tr("Total (%n line(s))", "", int(t.lines.size()))), ss(qtyText(units)), ctx.money(Money::fromCents(t.sentCost()))});
    if (t.status == "void")
        r.note(ss(tr("VOIDED %1 by %2").arg(i18n::locale().toString(QDateTime::fromMSecsSinceEpoch(t.voidedAt), u"M/d h:mm AP"_s),
                                             qs(t.voidedBy))));
    r.note(" ");
    r.note(ss(tr("Received by: ______________________")));
    r.note(" ");
    r.note(ss(tr("Signature: ________________________")));
    return r;
}

void PosService::printTransferSlip(const Transfer &t)
{
    if (s_->printer)
        s_->printer->printReport(s_->settings, transferSlip(t), receiptPrinter());
}

// --- receiving ----------------------------------------------------------------------------------------

bool PosService::transferStartReceive(qint64 id)
{
    Transfer *t = transferRecord(id);
    if (!t || t->direction != "in")
        return fail(tr("No such transfer."));
    if (t->status != "waiting")
        return fail(tr("Transfer #%1 from %2 isn't waiting to be received.").arg(t->number).arg(qs(t->from)));
    TransferView &v = transferView();
    v.tab = u"in"_s;
    v.receiving = id;
    v.counts.assign(t->lines.size(), -1);
    v.reasons.assign(t->lines.size(), QString());
    v.add.assign(t->lines.size(), -1);
    v.checked.assign(t->lines.size(), false);
    // Everything as sent, to start with: the manager corrects what's different.
    for (std::size_t i = 0; i < t->lines.size(); ++i)
        v.counts[i] = t->lines[i].qty;
    v.message.clear();
    emit adminChanged();
    return true;
}

bool PosService::transferCount(int index, double qty, const QString &reason)
{
    TransferView &v = transferView();
    if (!v.receiving || index < 0 || index >= int(v.counts.size()))
        return false;
    if (!(qty >= 0) || qty > 100000)
        return fail(tr("Type how many came, like 0, 1 or 10."));
    v.counts[std::size_t(index)] = qty;
    v.reasons[std::size_t(index)] = reason.trimmed().left(120);
    v.checked[std::size_t(index)] = true;
    emit adminChanged();
    return true;
}

bool PosService::transferCheck(int index, bool on)
{
    TransferView &v = transferView();
    if (!v.receiving || index < 0 || index >= int(v.checked.size()))
        return false;
    v.checked[std::size_t(index)] = on;
    emit adminChanged();
    return true;
}

bool PosService::transferAddMissing(int index, bool add)
{
    TransferView &v = transferView();
    if (!v.receiving || index < 0 || index >= int(v.add.size()))
        return false;
    v.add[std::size_t(index)] = add ? 1 : 0;
    emit adminChanged();
    return true;
}

bool PosService::transferCancelReceive()
{
    TransferView &v = transferView();
    v.receiving = 0;
    v.counts.clear();
    v.reasons.clear();
    v.add.clear();
    emit adminChanged();
    return true;
}

bool PosService::transferReceive()
{
    TransferView &v = transferView();
    Transfer *t = transferRecord(v.receiving);
    if (!t || t->status != "waiting")
        return fail(tr("Open a transfer that's waiting first."));
    const TransferIndex &x0 = transferIndex();
    for (std::size_t i = 0; i < t->lines.size(); ++i) {
        const Line &l = t->lines[i];
        if (xfer::match(x0.upc, l).at < 0 && v.add[i] < 0)
            return fail(tr("%1 (%2) isn't on file here: Add it or Leave it out.").arg(qs(l.name), qs(l.upc)));
        if (std::abs(v.counts[i] - l.qty) > 1e-9 && v.reasons[i].isEmpty())
            return fail(tr("%1: %2 sent, %3 counted. Say why (short, over, damaged...).")
                            .arg(qs(l.name), qtyText(l.qty), qtyText(v.counts[i])));
    }
    if (!require(perm::Manager, tr("Receiving a transfer")))
        return false;
    t = transferRecord(v.receiving);
    if (!t)
        return false;
    // Items not on file, added from the transfer (its name, price, cost and category) when asked.
    bool added = false;
    for (std::size_t i = 0; i < t->lines.size(); ++i) {
        const Line &l = t->lines[i];
        if (v.add[i] != 1 || xfer::match(transferIndex().upc, l).at >= 0)
            continue;
        MenuItem m;
        m.id = "xfer-" + BuilderIndex::codeKey(l.upc);
        while (findItem(qs(m.id)))
            m.id += "-1";
        m.name = l.name;
        m.price = Money::fromCents(l.priceCents);
        m.taxClass = TaxClass::Merchandise;
        m.kitchenHide = true;
        m.barcodes = {l.upc};
        m.recipe.push_back({"__retail__", 1.0});
        if (l.costCents > 0)
            m.recipe.push_back({"__cost__", double(l.costCents) / 100.0});
        for (const MenuCategory &c : s_->categories())
            if (QString::compare(qs(c.name), qs(l.category), Qt::CaseInsensitive) == 0)
                m.family = c.id;
        s_->menu.push_back(m);
        if (s_->sink)
            s_->sink->saveMenuItem(s_->menu.back(), int(s_->menu.size()) - 1);
        logChange(tr("Menu"), qs(m.name), tr("Added"), QString(), tr("from transfer #%1 (%2)").arg(t->number).arg(qs(t->from)));
        added = true;
    }
    if (added) {
        ++s_->adminRevision;
        menuChanged();
    }
    const TransferIndex &x = transferIndex();
    t = transferRecord(v.receiving);
    for (std::size_t i = 0; i < t->lines.size(); ++i) {
        Line &l = t->lines[i];
        l.received = v.counts[i];
        l.reason = ss(v.reasons[i]);
        const xfer::Match mt = xfer::match(x.upc, l);
        if (mt.at < 0) {
            l.itemId.clear();
            if (l.reason.empty())
                l.reason = ss(tr("left out: not on file"));
            continue;
        }
        l.itemId = s_->menu[std::size_t(mt.at)].id;
        l.perUnit = mt.perUnit;
        addRetailStock(l.itemId, l.received * mt.perUnit);
    }
    t->receivedAt = now();
    t->receivedBy = user() ? user()->name : ss(terminal_);
    t->status = t->differences() > 0 ? "differences" : "received";
    if (s_->sink)
        s_->sink->saveTransfer(*t);
    putInOutbox(qs(t->from), u"result-"_s + qs(t->key) + u".json"_s, transferFile(u"result"_s, *t));
    logChange(tr("Inventory"), tr("Transfer #%1 from %2").arg(t->number).arg(qs(t->from)), tr("Received"), QString(),
              tr("%n line(s), %1 at cost", "", int(t->lines.size())).arg(format(Money::fromCents(t->receivedCost()))));
    v.message = t->status == "received" ? tr("Transfer #%1 from %2 received: %3 at cost.").arg(t->number).arg(qs(t->from), format(Money::fromCents(t->receivedCost())))
                                        : tr("Transfer #%1 from %2 received with %n difference(s).", "", t->differences()).arg(t->number).arg(qs(t->from));
    emit notice(v.message);
    v.receiving = 0;
    v.counts.clear();
    v.reasons.clear();
    v.add.clear();
    emit adminChanged();
    emit s_->adminChanged();
    return true;
}

// --- what the screen shows ----------------------------------------------------------------------------

QString PosService::transferStatusText(const Transfer &t) const
{
    if (t.direction == "out") {
        if (t.status == "transit")
            return t.delivered ? tr("In transit (at %1)").arg(qs(t.to)) : tr("Waiting to send");
        if (t.status == "received")
            return tr("Received");
        if (t.status == "differences")
            return tr("Received with differences");
        return tr("Void");
    }
    if (t.status == "waiting")
        return tr("Waiting to be received");
    if (t.status == "received")
        return tr("Received");
    if (t.status == "differences")
        return tr("Received with differences");
    return tr("Voided by %1").arg(qs(t.from));
}

QVariantMap PosService::transfers() const
{
    if (!xfer_ || !xfer_->active || !user() || !can(u"manager"_s))
        return {};
    const TransferView &v = *xfer_;
    QVariantList stores;
    for (const OtherStore &o : s_->settings.otherStores)
        if (o.on && !xfer::storeKey(o.name).empty())
            stores.append(QVariantMap{{u"name"_s, qs(xfer::storeKey(o.name))}});
    QVariantList lines;
    std::int64_t total = 0;
    for (std::size_t i = 0; i < v.lines.size(); ++i) {
        const MenuItem *m = findItem(qs(v.lines[i].itemId));
        if (!m)
            continue;
        const Line l = transferLineOf(*m, v.lines[i].qty);
        const std::int64_t c = std::llround(l.qty * double(l.costCents));
        total += c;
        const auto onHand = retailOnHand(m->id);
        lines.append(QVariantMap{{u"index"_s, int(i)}, {u"id"_s, qs(m->id)}, {u"upc"_s, qs(l.upc)}, {u"name"_s, qs(m->name)},
                                 {u"qty"_s, l.qty}, {u"cost"_s, l.costCents > 0 ? format(Money::fromCents(l.costCents)) : tr("no cost")},
                                 {u"lineCost"_s, format(Money::fromCents(c))},
                                 {u"pack"_s, l.packSize > 0 ? tr("%1 in each").arg(l.packSize) : QString()},
                                 {u"onHand"_s, onHand ? qtyText(*onHand) : QString()}});
    }
    QVariantList incoming, outgoing;
    std::int64_t from = 0, until = now() + 1;
    if (const QDate d = QDate::fromString(v.from, u"yyyy-MM-dd"_s); d.isValid())
        from = QDateTime(d, QTime(0, 0)).toMSecsSinceEpoch();
    else
        from = now() - 30LL * 86'400'000;
    if (const QDate d = QDate::fromString(v.until, u"yyyy-MM-dd"_s); d.isValid())
        until = QDateTime(d.addDays(1), QTime(0, 0)).toMSecsSinceEpoch();
    for (auto it = s_->transfers.rbegin(); it != s_->transfers.rend(); ++it) {
        const Transfer &t = *it;
        if (!t.open() && (t.at < from || t.at >= until))
            continue;
        const QVariantMap row{{u"id"_s, qint64(t.id)}, {u"key"_s, qs(t.key)}, {u"number"_s, t.number},
                              {u"from"_s, qs(t.from)}, {u"to"_s, qs(t.to)}, {u"lines"_s, int(t.lines.size())},
                              {u"cost"_s, format(Money::fromCents(t.sentCost()))},
                              {u"when"_s, i18n::locale().toString(QDateTime::fromMSecsSinceEpoch(t.at), u"ddd M/d h:mm AP"_s)},
                              {u"who"_s, qs(t.who)}, {u"status"_s, qs(t.status)}, {u"statusText"_s, transferStatusText(t)},
                              {u"delivered"_s, t.delivered}, {u"differences"_s, t.differences()},
                              {u"canVoid"_s, t.direction == "out" && t.status == "transit"},
                              {u"canReceive"_s, t.direction == "in" && t.status == "waiting"},
                              {u"text"_s, t.direction == "in"
                                              ? tr("From %1, %n line(s), %2 at cost", "", int(t.lines.size())).arg(qs(t.from), format(Money::fromCents(t.sentCost())))
                                              : tr("To %1, %n line(s), %2 at cost", "", int(t.lines.size())).arg(qs(t.to), format(Money::fromCents(t.sentCost())))}};
        (t.direction == "in" ? incoming : outgoing).append(row);
    }
    QVariantMap receiving;
    if (v.receiving) {
        for (const Transfer &t : s_->transfers) {
            if (t.id != v.receiving)
                continue;
            const TransferIndex &x = transferIndex();
            QVariantList rl;
            for (std::size_t i = 0; i < t.lines.size(); ++i) {
                const Line &l = t.lines[i];
                const xfer::Match mt = xfer::match(x.upc, l);
                QString here;
                if (mt.at >= 0)
                    here = mt.perUnit != 1 ? tr("%1 (x %2 each)").arg(qs(s_->menu[std::size_t(mt.at)].name)).arg(qtyText(mt.perUnit))
                                           : qs(s_->menu[std::size_t(mt.at)].name);
                const double counted = i < v.counts.size() ? v.counts[i] : l.qty;
                rl.append(QVariantMap{{u"index"_s, int(i)}, {u"upc"_s, qs(l.upc)}, {u"name"_s, qs(l.name)}, {u"qty"_s, l.qty},
                                      {u"counted"_s, counted}, {u"reason"_s, i < v.reasons.size() ? v.reasons[i] : QString()},
                                      {u"found"_s, mt.at >= 0}, {u"here"_s, here},
                                      {u"add"_s, i < v.add.size() ? v.add[i] : -1},
                                      {u"checked"_s, i < v.checked.size() && v.checked[i]},
                                      {u"different"_s, std::abs(counted - l.qty) > 1e-9},
                                      {u"cost"_s, format(Money::fromCents(std::llround(l.qty * double(l.costCents))))}});
            }
            receiving = {{u"id"_s, qint64(t.id)}, {u"number"_s, t.number}, {u"from"_s, qs(t.from)}, {u"who"_s, qs(t.who)},
                         {u"note"_s, qs(t.note)}, {u"cost"_s, format(Money::fromCents(t.sentCost()))},
                         {u"when"_s, i18n::locale().toString(QDateTime::fromMSecsSinceEpoch(t.at), u"ddd M/d h:mm AP"_s)},
                         {u"lines"_s, rl}};
        }
    }
    const int queued = waitingToSend();
    return {{u"active"_s, true}, {u"tab"_s, v.tab}, {u"storeCode"_s, storeCode()}, {u"stores"_s, stores},
            {u"to"_s, v.to}, {u"note"_s, v.note}, {u"lines"_s, lines}, {u"total"_s, format(Money::fromCents(total))},
            {u"choices"_s, v.choices}, {u"message"_s, v.message}, {u"incoming"_s, incoming}, {u"outgoing"_s, outgoing},
            {u"waitingIn"_s, int(std::ranges::count_if(s_->transfers, [](const Transfer &t) { return t.direction == "in" && t.status == "waiting"; }))},
            {u"receiving"_s, receiving}, {u"queued"_s, queued},
            {u"link"_s, queued > 0 ? (sendStatus().isEmpty() ? tr("%n file(s) waiting to send.", "", queued) : sendStatus()) : QString()},
            {u"from"_s, v.from}, {u"until"_s, v.until}};
}

bool PosService::transferRange(const QString &from, const QString &until)
{
    TransferView &v = transferView();
    v.from = from.trimmed();
    v.until = until.trimmed();
    emit adminChanged();
    return true;
}

// --- the Transfers report -----------------------------------------------------------------------------

Report PosService::transfersReport(std::int64_t from, std::int64_t to, const ReportContext &ctx) const
{
    Report r;
    r.id = "transfers";
    r.title = "Transfers";
    r.subtitle = ctx.period;
    r.columns = {"", "Lines", "Status", "At cost"};
    std::int64_t outCost = 0, inCost = 0, transitCost = 0, diffCost = 0;
    const auto when = [&](std::int64_t at) { return ctx.date ? ctx.date(at) : ctx.clock(at); };
    const auto row = [&](const Transfer &t, std::int64_t cost) {
        const std::string who = t.direction == "out" ? "#" + std::to_string(t.number) + " to " + t.to
                                                     : "#" + std::to_string(t.number) + " from " + t.from;
        r.line({who + ", " + when(t.at), std::to_string(t.lines.size()), ss(transferStatusText(t)), ctx.money(Money::fromCents(cost))});
    };
    r.section("Sent");
    int n = 0;
    for (const Transfer &t : s_->transfers)
        if (t.direction == "out" && t.at >= from && t.at < to && t.status != "void") {
            row(t, t.sentCost());
            outCost += t.sentCost();
            ++n;
        }
    if (n == 0)
        r.note("None sent.");
    r.total({"Sent", "", "", ctx.money(Money::fromCents(outCost))});
    r.section("Received");
    n = 0;
    for (const Transfer &t : s_->transfers)
        if (t.direction == "in" && t.receivedAt >= from && t.receivedAt < to && (t.status == "received" || t.status == "differences")) {
            row(t, t.receivedCost());
            inCost += t.receivedCost();
            ++n;
        }
    if (n == 0)
        r.note("None received.");
    r.total({"Received", "", "", ctx.money(Money::fromCents(inCost))});
    r.section("In transit now");
    n = 0;
    for (const Transfer &t : s_->transfers)
        if (t.open()) {
            row(t, t.sentCost());
            transitCost += t.sentCost();
            ++n;
        }
    if (n == 0)
        r.note("Nothing in transit.");
    r.total({"In transit", "", "", ctx.money(Money::fromCents(transitCost))});
    r.section("Differences (sent - received)");
    n = 0;
    for (const Transfer &t : s_->transfers) {
        if (t.status != "differences" || t.receivedAt < from || t.receivedAt >= to)
            continue;
        for (const Line &l : t.lines) {
            if (l.received < 0 || std::abs(l.received - l.qty) < 1e-9)
                continue;
            const std::int64_t c = std::llround((l.qty - l.received) * double(l.costCents));
            diffCost += c;
            char q[64];
            std::snprintf(q, sizeof q, "%g of %g", l.received, l.qty);
            r.line({"#" + std::to_string(t.number) + " " + l.name + (l.reason.empty() ? "" : " (" + l.reason + ")"), q,
                    t.direction == "out" ? "to " + t.to : "from " + t.from, ctx.money(Money::fromCents(c))});
            ++n;
        }
    }
    if (n == 0)
        r.note("No differences.");
    r.total({"Differences", "", "", ctx.money(Money::fromCents(diffCost))});
    // What the retail items counted here are worth (their on hand from transfers and sales since).
    std::int64_t value = 0;
    int items = 0;
    for (const auto &[id, onHand] : s_->retailStock)
        if (const MenuItem *m = findItem(qs(id)); m && onHand > 0) {
            value += std::llround(onHand * double(costCentsOf(*m)));
            ++items;
        }
    r.section("Retail stock kept here");
    r.line({"Items with an on hand", std::to_string(items), "", ctx.money(Money::fromCents(value))});
    return r;
}

// --- price changes to the other stores, and the price source's prices here -----------------------------

void PosService::sendChangeSetToStores(const QVariantMap &changeSet)
{
    std::vector<const OtherStore *> to;
    for (const OtherStore &o : s_->settings.otherStores)
        if (o.on && o.sendPrices && !xfer::storeKey(o.name).empty())
            to.push_back(&o);
    if (to.empty())
        return;
    if (storeCode().isEmpty()) {
        emit notice(tr("Price changes weren't sent to the other stores: give this store its short name (Store Settings > Other stores)."));
        return;
    }
    // Retail items' prices, costs and departments only (the bar's own items never go anywhere).
    const TransferIndex &x = transferIndex();
    QVariantList changes;
    for (const QVariant &cv : changeSet.value(u"changes"_s).toList()) {
        const QVariantMap c = cv.toMap();
        const QString field = c.value(u"field"_s).toString();
        if (field != u"price" && field != u"cost" && field != u"category")
            continue;
        const std::int64_t at = x.upc.find(ss(c.value(u"upc"_s).toString()));
        if (at < 0 || !itemIsRetail(s_->menu[std::size_t(at)]))
            continue;
        changes.append(c);
    }
    if (changes.isEmpty())
        return;
    QVariantMap out = changeSet;
    out.insert(u"store"_s, storeCode());
    out.insert(u"from"_s, storeCode());
    out.insert(u"changes"_s, changes);
    const QString id = out.value(u"id"_s).toString();
    for (const OtherStore *o : to)
        putInOutbox(qs(xfer::storeKey(o->name)), u"changes-"_s + id + u".json"_s, out);
}

void PosService::shareItemChange(const MenuItem &before, const MenuItem &after)
{
    if (after.barcodes.empty() || !itemIsRetail(after))
        return;
    if (std::ranges::none_of(s_->settings.otherStores, [](const OtherStore &o) { return o.on && o.sendPrices; }))
        return;
    QVariantList changes;
    const QString upc = qs(after.barcodes.front()), name = qs(after.name);
    if (before.price != after.price)
        changes.append(QVariantMap{{u"upc"_s, upc}, {u"item"_s, name}, {u"field"_s, u"price"_s},
                                   {u"before"_s, qs(mass::centsText(before.price.cents()))}, {u"after"_s, qs(mass::centsText(after.price.cents()))}});
    if (costCentsOf(before) != costCentsOf(after))
        changes.append(QVariantMap{{u"upc"_s, upc}, {u"item"_s, name}, {u"field"_s, u"cost"_s},
                                   {u"before"_s, costCentsOf(before) > 0 ? qs(mass::centsText(costCentsOf(before))) : QString()},
                                   {u"after"_s, costCentsOf(after) > 0 ? qs(mass::centsText(costCentsOf(after))) : QString()}});
    if (changes.isEmpty())
        return;
    const std::int64_t at = now();
    sendChangeSetToStores({{u"format"_s, u"viewtouch-change-set"_s}, {u"version"_s, 1},
                           {u"id"_s, QDateTime::fromMSecsSinceEpoch(at).toString(u"yyyyMMdd-HHmmss-zzz"_s) + u"-card"_s},
                           {u"label"_s, tr("Item card: %1").arg(name)}, {u"who"_s, user() ? qs(user()->name) : terminal_},
                           {u"why"_s, QString()}, {u"at"_s, qint64(at)}, {u"undoOf"_s, QString()}, {u"changes"_s, changes}});
}

void PosService::loadBasePrices() const
{
    if (s_->basePricesLoaded)
        return;
    s_->basePricesLoaded = true;
    QFile f(transferPath(u"base-prices.json"_s));
    if (s_->transferDir.isEmpty() || !f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    s_->basePricesFrom = ss(o.value(u"from").toString());
    const QJsonObject prices = o.value(u"prices").toObject();
    for (auto it = prices.begin(); it != prices.end(); ++it)
        s_->basePrices[ss(it.key())] = it.value().toInteger();
    ++s_->basePricesRevision;
}

void PosService::saveBasePrices() const
{
    if (s_->transferDir.isEmpty())
        return;
    QJsonObject prices;
    for (const auto &[k, cents] : s_->basePrices)
        prices.insert(qs(k), qint64(cents));
    QDir().mkpath(s_->transferDir);
    QSaveFile f(transferPath(u"base-prices.json"_s));
    if (f.open(QIODevice::WriteOnly)
        && f.write(QJsonDocument(QJsonObject{{u"from"_s, qs(s_->basePricesFrom)}, {u"prices"_s, prices}}).toJson(QJsonDocument::Compact)) >= 0)
        f.commit();
}

std::optional<std::int64_t> PosService::basePriceOf(const MenuItem &m) const
{
    loadBasePrices();
    for (const std::string &b : m.barcodes)
        if (const auto it = s_->basePrices.find(BuilderIndex::codeKey(b)); it != s_->basePrices.end())
            return it->second;
    return std::nullopt;
}

// --- "<store> price": the price source's price on the sale's retail lines (owner 2026-11-26) ------------

bool PosService::storePriceDiscount()
{
    Check *c = current();
    if (!c)
        return fail(tr("No check is open."));
    const QString source = qs(xfer::storeKey(s_->settings.storePricesFrom));
    if (source.isEmpty())
        return fail(tr("This store takes its prices from no other store (Store Settings > Other stores)."));
    // Each retail line priced above the source's price (its zone's markup): the difference, once a line.
    struct Off { std::int64_t lineId; Money amount; QString name; };
    std::vector<Off> offs;
    for (const OrderLine &l : c->lines) {
        if (l.voided || l.isComment() || l.isGiftCard() || l.isReturn() || l.quantity <= 0)
            continue;
        if (std::ranges::any_of(c->payments, [&](const Payment &p) { return p.tenderId == kStorePriceTender && p.lineId == l.id; }))
            continue;
        const MenuItem *m = findItem(qs(l.itemId));
        if (!m || !itemIsRetail(*m))
            continue;
        const auto base = basePriceOf(*m);
        if (!base || *base >= l.unitPrice.cents())
            continue;
        offs.push_back({l.id, Money::fromCents((l.unitPrice.cents() - *base) * l.counted()), qs(l.displayName())});
    }
    if (offs.empty())
        return fail(tr("No retail item on this sale is above %1's price.").arg(source));
    if (!haveReason("comp", tr("%1 price").arg(source)))
        return false;
    if (!require(perm::Discount, tr("Discounts and comps")))
        return false;
    c = current();
    if (!c)
        return false;
    const Totals before = c->totals(s_->settings.tax);
    Tender t;
    t.id = kStorePriceTender;
    t.kind = TenderKind::Discount;
    t.name = ss(tr("%1 price").arg(source));
    for (const Off &o : offs) {
        Payment &p = c->addPayment(t, Money());
        stampPayment(p);
        p.amount = o.amount;
        p.lineId = o.lineId;
    }
    const Totals after = c->totals(s_->settings.tax);
    noteEvent(*c, tr("Discount: %1").arg(qs(t.name)), "discount", after.discounts - before.discounts, takeReason());
    emit notice(tr("%1: %2 off").arg(qs(t.name), format(after.discounts - before.discounts)));
    changed(*c);
    return true;
}

} // namespace vt::app
