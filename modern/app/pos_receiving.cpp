// PosService: retail receiving (Manager > Inventory > Receive retail…; owner decisions 2026-11-04 and
// 2026-11-27). A vendor's invoice counted in: pick the vendor, the invoice number and date, scan what came
// (a repeat scan is one more; a carton's or box's UPC brings its pack size of the single, from the singles'
// __parent__ link), find it, or type the vendor's item number; the invoice's price, OTP and prepaid tax per
// unit; store-use lines (supplies: never on hand); delivery, credit and other amounts; the invoice's total
// from the paper ties out. A code not on file is linked to an item (its alt UPC), added, or left out.
// The invoice is kept as it's worked on (unfinished: autosaved in retail_drafts, nothing on hand; Save for later,
// the Unfinished tab). Receive adds the stock lines to the retail on hand (the same on hand as Transfers,
// pos_transfers.cpp) and keeps each vendor's item number, unit and last cost; it asks first when the invoice
// doesn't tie out. A received invoice can be opened and changed: only the difference goes on hand, the change
// is kept on it. It never changes an item's system cost or price (owner 2026-11-27).
// Import invoice file: a CSV of a vendor's invoices (many invoices, both accounts of a distributor), its
// columns guessed and kept per vendor, received or loaded as history only (item numbers and costs, no stock).
// The rules and the file's reading: core/receiving.hh.

#include "app/i18n.hh"
#include "app/pos_json.hh"
#include "app/pos_service.hh"
#include "core/builder_index.hh"
#include "core/mass_update.hh"
#include "core/receiving.hh"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <tuple>
#include <unordered_map>

using namespace Qt::StringLiterals;
using namespace vt::core;
namespace mass = vt::core::mass;
using vt::core::recv::FileInvoice;
using vt::core::recv::FileLine;
using vt::core::recv::Line;
using vt::core::recv::Receipt;
using vt::core::recv::VendorItem;

namespace vt::app {

namespace {

std::int64_t systemCostOf(const MenuItem &m)
{
    for (const RecipeLine &rl : m.recipe)
        if (rl.ingredientId == "__cost__" && rl.quantity > 0.0)
            return std::llround(rl.quantity * 100.0);
    return 0;
}

QString qtyText(double q) { return QString::number(std::round(q * 1000) / 1000, 'g', 10); }

bool looksLikeBarcode(const QString &code)
{
    const QString c = code.trimmed();
    return c.size() >= 6 && c.size() <= 14 && std::ranges::all_of(c, [](QChar ch) { return ch.isDigit(); });
}

QJsonObject lineJson(const Line &l)
{
    QJsonObject o{{u"kind"_s, qs(l.kind)}, {u"itemId"_s, qs(l.itemId)}, {u"upc"_s, qs(l.upc)}, {u"vendorItem"_s, qs(l.vendorItem)},
            {u"name"_s, qs(l.name)}, {u"uom"_s, qs(l.uom)}, {u"perUom"_s, l.perUom}, {u"qty"_s, l.qty},
            {u"cost"_s, qint64(l.costCents)}, {u"otp"_s, qint64(l.otpCents)}, {u"prepaid"_s, qint64(l.prepaidCents)},
            {u"tax"_s, qint64(l.taxCents)}, {u"amount"_s, qint64(l.amountCents)},
            {u"systemCost"_s, qint64(l.systemCostCents)}, {u"leftOut"_s, l.leftOut}};
    if (l.allowanceCents)   // (only when there is one: a line saved before allowances reads the same)
        o.insert(u"allowance"_s, qint64(l.allowanceCents));
    return o;
}

Line lineFrom(const QJsonObject &o)
{
    Line l;
    l.kind = ss(o.value(u"kind").toString(u"stock"_s));
    l.itemId = ss(o.value(u"itemId").toString());
    l.upc = ss(o.value(u"upc").toString());
    l.vendorItem = ss(o.value(u"vendorItem").toString());
    l.name = ss(o.value(u"name").toString());
    l.uom = ss(o.value(u"uom").toString());
    l.perUom = o.value(u"perUom").toDouble(1);
    l.qty = o.value(u"qty").toDouble();
    l.costCents = o.value(u"cost").toInteger(-1);
    l.otpCents = o.value(u"otp").toInteger();
    l.prepaidCents = o.value(u"prepaid").toInteger();
    l.allowanceCents = std::llabs(o.value(u"allowance").toInteger());
    l.taxCents = o.value(u"tax").toInteger();
    l.amountCents = o.value(u"amount").toInteger();
    l.systemCostCents = o.value(u"systemCost").toInteger();
    l.leftOut = o.value(u"leftOut").toBool();
    return l;
}

// The field names the import screen shows.
QString fieldLabel(const std::string &f)
{
    static const std::map<std::string, const char *> labels = {
        {"invoice", QT_TR_NOOP("Invoice #")}, {"date", QT_TR_NOOP("Invoice date")},
        {"account", QT_TR_NOOP("Account / ship-to")}, {"itemNo", QT_TR_NOOP("Vendor item #")},
        {"upc", QT_TR_NOOP("UPC")}, {"description", QT_TR_NOOP("Description")}, {"uom", QT_TR_NOOP("Unit (UOM)")},
        {"perUom", QT_TR_NOOP("Units per UOM")}, {"qty", QT_TR_NOOP("Quantity")}, {"unitPrice", QT_TR_NOOP("Unit price")},
        {"otp", QT_TR_NOOP("OTP / excise per unit")}, {"prepaid", QT_TR_NOOP("Prepaid sales tax per unit")},
        {"prepaidTotal", QT_TR_NOOP("Prepaid tax total (the invoice's)")}, {"allowance", QT_TR_NOOP("Allowance per unit")},
        {"extended", QT_TR_NOOP("Extended (line total)")}, {"lineType", QT_TR_NOOP("Line type / store use")},
        {"delivery", QT_TR_NOOP("Delivery / freight")}, {"useTax", QT_TR_NOOP("Use tax")},
        {"invoiceTotal", QT_TR_NOOP("Invoice total")}};
    const auto it = labels.find(f);
    return it == labels.end() ? qs(f) : PosService::tr(it->second);
}

QString kindLabel(const std::string &k)
{
    if (k == recv::kStoreUse)
        return PosService::tr("Store use");
    if (k == recv::kDelivery)
        return PosService::tr("Delivery");
    if (k == recv::kPrepaid)
        return PosService::tr("Prepaid tax");
    if (k == recv::kCredit)
        return PosService::tr("Credit / return");
    if (k == recv::kOther)
        return PosService::tr("Other");
    return PosService::tr("Stock");
}

} // namespace

QJsonObject toJson(const Receipt &r)
{
    QJsonArray lines;
    for (const Line &l : r.lines)
        lines.append(lineJson(l));
    QJsonObject o{{u"id"_s, qint64(r.id)}, {u"vendorId"_s, qs(r.vendorId)}, {u"vendorName"_s, qs(r.vendorName)},
                  {u"invoice"_s, qs(r.invoice)}, {u"date"_s, qs(r.date)}, {u"at"_s, qint64(r.at)}, {u"by"_s, qs(r.by)},
                  {u"historyOnly"_s, r.historyOnly}, {u"source"_s, qs(r.source)}, {u"typedTotal"_s, qint64(r.typedTotal)},
                  {u"note"_s, qs(r.note)}, {u"lines"_s, lines}};
    // (Only when there is something: a receipt saved before these were kept reads the same.)
    if (!r.edits.empty()) {
        QJsonArray edits;
        for (const recv::Edit &e : r.edits) {
            QJsonArray el;
            for (const recv::EditLine &x : e.lines)
                el.append(QJsonObject{{u"name"_s, qs(x.name)}, {u"qtyBefore"_s, x.qtyBefore}, {u"qtyAfter"_s, x.qtyAfter},
                                      {u"extBefore"_s, qint64(x.extBefore)}, {u"extAfter"_s, qint64(x.extAfter)}});
            edits.append(QJsonObject{{u"at"_s, qint64(e.at)}, {u"by"_s, qs(e.by)}, {u"terminal"_s, qs(e.terminal)},
                                     {u"totalBefore"_s, qint64(e.totalBefore)}, {u"totalAfter"_s, qint64(e.totalAfter)},
                                     {u"unitsBefore"_s, e.unitsBefore}, {u"unitsAfter"_s, e.unitsAfter}, {u"lines"_s, el}});
        }
        o.insert(u"edits"_s, edits);
    }
    if (r.changedAt)
        o.insert(u"changedAt"_s, qint64(r.changedAt));
    if (!r.terminal.empty())
        o.insert(u"terminal"_s, qs(r.terminal));
    if (r.parked)
        o.insert(u"parked"_s, true);
    if (r.reopenOf) {
        o.insert(u"reopenOf"_s, qint64(r.reopenOf));
        o.insert(u"baseEdits"_s, r.baseEdits);
    }
    return o;
}

Receipt retailReceiptFromJson(const QJsonObject &o)
{
    Receipt r;
    r.id = o.value(u"id").toInteger();
    r.vendorId = ss(o.value(u"vendorId").toString());
    r.vendorName = ss(o.value(u"vendorName").toString());
    r.invoice = ss(o.value(u"invoice").toString());
    r.date = ss(o.value(u"date").toString());
    r.at = o.value(u"at").toInteger();
    r.by = ss(o.value(u"by").toString());
    r.historyOnly = o.value(u"historyOnly").toBool();
    r.source = ss(o.value(u"source").toString());
    r.typedTotal = o.value(u"typedTotal").toInteger(-1);
    r.note = ss(o.value(u"note").toString());
    for (const QJsonValue &v : o.value(u"lines").toArray())
        r.lines.push_back(lineFrom(v.toObject()));
    for (const QJsonValue &v : o.value(u"edits").toArray()) {
        const QJsonObject eo = v.toObject();
        recv::Edit e;
        e.at = eo.value(u"at").toInteger();
        e.by = ss(eo.value(u"by").toString());
        e.terminal = ss(eo.value(u"terminal").toString());
        e.totalBefore = eo.value(u"totalBefore").toInteger();
        e.totalAfter = eo.value(u"totalAfter").toInteger();
        e.unitsBefore = eo.value(u"unitsBefore").toDouble();
        e.unitsAfter = eo.value(u"unitsAfter").toDouble();
        for (const QJsonValue &lv : eo.value(u"lines").toArray()) {
            const QJsonObject lo = lv.toObject();
            e.lines.push_back({ss(lo.value(u"name").toString()), lo.value(u"qtyBefore").toDouble(), lo.value(u"qtyAfter").toDouble(),
                               lo.value(u"extBefore").toInteger(), lo.value(u"extAfter").toInteger()});
        }
        r.edits.push_back(std::move(e));
    }
    r.changedAt = o.value(u"changedAt").toInteger();
    r.terminal = ss(o.value(u"terminal").toString());
    r.parked = o.value(u"parked").toBool();
    r.reopenOf = o.value(u"reopenOf").toInteger();
    r.baseEdits = o.value(u"baseEdits").toInt();
    return r;
}

QJsonObject toJson(const VendorItem &v)
{
    QJsonObject o{{u"vendorId"_s, qs(v.vendorId)}, {u"itemId"_s, qs(v.itemId)}, {u"vendorItem"_s, qs(v.vendorItem)},
            {u"uom"_s, qs(v.uom)}, {u"perUom"_s, v.perUom}, {u"lastCost"_s, qint64(v.lastCostCents)},
            {u"lastDate"_s, qs(v.lastDate)}, {u"lastInvoice"_s, qs(v.lastInvoice)}, {u"storeUse"_s, v.storeUse},
            {u"description"_s, qs(v.description)}};
    if (v.lastAllowanceCents)
        o.insert(u"lastAllowance"_s, qint64(v.lastAllowanceCents));
    return o;
}

VendorItem vendorItemFromJson(const QJsonObject &o)
{
    VendorItem v;
    v.vendorId = ss(o.value(u"vendorId").toString());
    v.itemId = ss(o.value(u"itemId").toString());
    v.vendorItem = ss(o.value(u"vendorItem").toString());
    v.uom = ss(o.value(u"uom").toString());
    v.perUom = o.value(u"perUom").toDouble(1);
    v.lastCostCents = o.value(u"lastCost").toInteger(-1);
    v.lastAllowanceCents = std::llabs(o.value(u"lastAllowance").toInteger());
    v.lastDate = ss(o.value(u"lastDate").toString());
    v.lastInvoice = ss(o.value(u"lastInvoice").toString());
    v.storeUse = o.value(u"storeUse").toBool();
    v.description = ss(o.value(u"description").toString());
    return v;
}

// --- one screen's -------------------------------------------------------------------------------------

struct PosService::RecvView {
    bool active = false;
    QString tab = u"receive"_s;   // receive | unfinished | import | list
    // The invoice being received: autosaved as an unfinished one (draftId, once there is something on it); a
    // received invoice being changed (editingId, and its changes when it was opened).
    std::int64_t draftId = 0;
    std::int64_t editingId = 0;
    int baseEdits = 0;
    QVariantMap confirm;          // the question Receive asks: {kind: off | nototal | duplicate, text, receiptId}
    std::int64_t showId = 0;      // Invoices received: the one opened
    QString vendorId, invoice, date, note;
    std::int64_t typedTotal = -1;
    std::vector<Line> lines;
    int last = -1;            // the line last scanned or changed (the screen shows it)
    int linking = -1;         // a line not on file being linked: Find's choices link it
    QVariantList choices;
    QString message;
    QString from, until;      // the list's dates
    // Import invoice file.
    struct Decision {
        std::string action;   // link | skip | storeuse
        std::string itemId;
    };
    struct Import {
        bool loaded = false;
        QString fileName;
        std::vector<std::vector<std::string>> rows;
        int header = -1;
        recv::Mapping map;
        QString vendorId;
        QString mode = u"receive"_s;   // receive | history
        int show = -1;
        std::map<std::string, Decision> decisions;   // a code (recvCodeKey) -> what to do with it
        int changes = 0;
    } imp;
};

PosService::RecvView &PosService::recvView() const
{
    if (!recv_)
        recv_ = std::make_shared<RecvView>();
    return *recv_;
}

// --- looking items up ---------------------------------------------------------------------------------

struct PosService::RecvIndex {
    std::uint64_t shared = 0;
    int revision = -1, vendorRevision = -1;
    std::size_t size = 0;
    mass::UpcIndex upc;
    std::unordered_map<std::string, std::pair<std::string, int>> packOf;   // a box's UPC key -> its single's UPC, count
    std::unordered_map<std::string, std::string> byNumber;                 // vendor \x1f their item number -> key
};

const PosService::RecvIndex &PosService::recvIndex() const
{
    static RecvIndex x;
    if (x.shared == s_->serial && x.revision == s_->adminRevision && x.size == s_->menu.size()
        && x.vendorRevision == s_->vendorItemsRevision)
        return x;
    x.shared = s_->serial;
    x.revision = s_->adminRevision;
    x.vendorRevision = s_->vendorItemsRevision;
    x.size = s_->menu.size();
    const auto &menu = s_->menu;
    x.upc.build(menu.size(), [&](std::size_t i) { return menu[i].barcodes; });
    x.packOf.clear();
    for (const MenuItem &m : menu)
        for (const RecipeLine &rl : m.recipe)
            if (rl.ingredientId.starts_with("__parent__:") && !m.barcodes.empty() && rl.quantity >= 1)
                x.packOf.try_emplace(BuilderIndex::codeKey(rl.ingredientId.substr(11)), m.barcodes.front(), int(rl.quantity));
    x.byNumber.clear();
    for (const auto &[key, v] : s_->vendorItems)
        if (!v.vendorItem.empty())
            x.byNumber[v.vendorId + '\x1f' + recv::numberKey(v.vendorItem)] = key;
    return x;
}

const VendorItem *PosService::vendorItemByNumber(const std::string &vendorId, const std::string &number) const
{
    if (number.empty())
        return nullptr;
    const RecvIndex &x = recvIndex();
    const auto it = x.byNumber.find(vendorId + '\x1f' + recv::numberKey(number));
    if (it == x.byNumber.end())
        return nullptr;
    const auto v = s_->vendorItems.find(it->second);
    return v == s_->vendorItems.end() ? nullptr : &v->second;
}

const VendorItem *PosService::vendorItemOf(const std::string &vendorId, const std::string &itemId) const
{
    const auto v = s_->vendorItems.find(recv::vendorItemKey(vendorId, itemId, {}));
    return v == s_->vendorItems.end() ? nullptr : &v->second;
}

// A code scanned or typed (or a file's UPC / item number): the item and how many of it one is. A carton's or
// box's UPC: its single, times the pack size; else the item's own UPC; else the vendor's item number; else a PLU.
PosService::RecvFound PosService::recvResolve(const QString &code, const QString &vendorId) const
{
    RecvFound f;
    const std::string c = ss(code.trimmed());
    if (c.empty())
        return f;
    const RecvIndex &x = recvIndex();
    if (const auto p = x.packOf.find(BuilderIndex::codeKey(c)); p != x.packOf.end())
        if (const std::int64_t at = x.upc.find(p->second.first); at >= 0) {
            f.item = &s_->menu[std::size_t(at)];
            f.perUom = p->second.second;
            f.byPack = true;
            return f;
        }
    if (const std::int64_t at = x.upc.find(c); at >= 0) {
        f.item = &s_->menu[std::size_t(at)];
        return f;
    }
    if (const VendorItem *v = vendorItemByNumber(ss(vendorId), c)) {
        f.vendorItem = v;
        if (const MenuItem *m = findItem(qs(v->itemId)); m && !v->itemId.empty())
            f.item = m;
        f.perUom = v->perUom > 0 ? v->perUom : 1;
        return f;
    }
    for (const MenuItem &m : s_->menu)
        if (!m.number.empty() && m.number == c) {
            f.item = &m;
            return f;
        }
    return f;
}

const Vendor *PosService::vendorById(const QString &id) const
{
    for (const Vendor &v : s_->settings.vendors)
        if (qs(v.id) == id)
            return &v;
    return nullptr;
}

std::int64_t PosService::recvDuplicate(const std::string &vendorId, const std::string &invoice, std::int64_t except) const
{
    const std::string k = recv::numberKey(invoice);
    for (const Receipt &r : s_->retailReceipts)
        if (r.id != except && r.vendorId == vendorId && recv::numberKey(r.invoice) == k)
            return r.id;
    return 0;
}

// An item line as it starts: the item, its unit, the system cost now, the vendor's number and unit.
Line PosService::recvLineFor(const MenuItem &m, double perUom, const QString &vendorId) const
{
    Line l;
    l.kind = recv::kStock;
    l.itemId = m.id;
    l.name = m.name;
    l.upc = m.barcodes.empty() ? std::string() : m.barcodes.front();
    l.perUom = perUom > 0 ? perUom : 1;
    l.qty = 1;
    l.systemCostCents = systemCostOf(m);
    if (const VendorItem *v = vendorItemOf(ss(vendorId), m.id); v && std::abs(v->perUom - l.perUom) < 1e-9) {
        l.vendorItem = v->vendorItem;
        l.uom = v->uom;
    }
    return l;
}

// --- the screen ---------------------------------------------------------------------------------------

bool PosService::recvOpen(const QString &tab)
{
    // (No manager PIN anywhere in receiving, owner 2026-11-04: whoever is on duty receives; it's kept who.)
    if (!user())
        return fail(tr("Log in first."));
    RecvView &v = recvView();
    const bool opening = !v.active;
    v.active = true;
    if (tab == u"receive" || tab == u"unfinished" || tab == u"import" || tab == u"list")
        v.tab = tab;
    if (v.date.isEmpty())
        v.date = QDateTime::fromMSecsSinceEpoch(now()).date().toString(u"yyyy-MM-dd"_s);
    v.message.clear();
    if (opening) {
        if (v.draftId) {
            // Back to the screen: its invoice as it is now (another screen may have had it meanwhile).
            const Receipt *d = recvDraft(v.draftId);
            const QObject *h = recvHolder(v.draftId);
            if (!d || (h && h != this)) {
                recvReset();
                v.message = d ? tr("The invoice you were on is open at another screen now; it's on Unfinished.") : QString();
            } else {
                recvLoad(*d);
            }
        } else if (v.lines.empty() && v.invoice.isEmpty()) {
            // The invoice this terminal was on when ViewTouch stopped (not saved for later): back.
            const Receipt *found = nullptr;
            for (const Receipt &d : s_->retailDrafts)
                if (!d.parked && d.terminal == ss(terminal_) && !recvHolder(d.id) && (!found || d.changedAt >= found->changedAt))
                    found = &d;
            if (found) {
                recvLoad(*found);
                v.message = tr("Invoice #%1 is back as you left it (it's kept as you work). Nothing is on hand from it until you Receive it.")
                                .arg(qs(found->invoice));
            }
        }
    }
    emit adminChanged();
    return true;
}

bool PosService::recvClose()
{
    if (recv_) {
        recv_->active = false;
        recvRelease();   // (its invoice stays as it is: back when this screen opens again, or Open on another)
    }
    emit adminChanged();
    return true;
}

bool PosService::recvSetHeader(const QVariantMap &h)
{
    RecvView &v = recvView();
    if (h.contains(u"vendor"_s)) {
        const QString id = h.value(u"vendor"_s).toString();
        if (!id.isEmpty() && !vendorById(id))
            return fail(tr("There is no vendor '%1'.").arg(id));
        if (v.editingId && id != v.vendorId)
            return fail(tr("A received invoice keeps its vendor (delete the lines and receive it again under the other vendor)."));
        v.vendorId = id;
    }
    if (h.contains(u"invoice"_s))
        v.invoice = h.value(u"invoice"_s).toString().trimmed().left(40);
    if (h.contains(u"date"_s)) {
        const QString d = h.value(u"date"_s).toString().trimmed();
        const std::string ymd = recv::dateOf(ss(d));
        if (!d.isEmpty() && ymd.empty())
            return fail(tr("Dates are written like 2026-11-27 or 11/27/2026."));
        v.date = qs(ymd);
    }
    if (h.contains(u"total"_s)) {
        const QString t = h.value(u"total"_s).toString().trimmed();
        if (t.isEmpty()) {
            v.typedTotal = -1;
        } else if (const auto c = recv::centsOf(ss(t))) {
            v.typedTotal = *c;
        } else {
            return fail(tr("Type the invoice's total, like 1234.56."));
        }
    }
    if (h.contains(u"note"_s))
        v.note = h.value(u"note"_s).toString().trimmed().left(300);
    recvTouched();
    return true;
}

bool PosService::recvAddItem(const QString &itemId, double perUom)
{
    RecvView &v = recvView();
    const MenuItem *m = findItem(itemId);
    if (!m || m->isModifier)
        return fail(tr("No item %1.").arg(itemId));
    if (v.linking >= 0)
        return recvLink(v.linking, itemId);
    if (!itemIsRetail(*m))
        return fail(tr("%1 isn't a retail item.").arg(qs(m->name)));
    v.choices.clear();
    v.message.clear();
    const double per = perUom > 0 ? perUom : 1;
    // The same item in the same unit again: one more (as a repeat scan at the register).
    for (std::size_t i = 0; i < v.lines.size(); ++i) {
        Line &l = v.lines[i];
        if (l.kind == recv::kStock && l.itemId == m->id && std::abs(l.perUom - per) < 1e-9) {
            l.qty += 1;
            v.last = int(i);
            recvTouched();
            return true;
        }
    }
    v.lines.push_back(recvLineFor(*m, per, v.vendorId));
    v.last = int(v.lines.size()) - 1;
    recvTouched();
    return true;
}

bool PosService::recvScan(const QString &code)
{
    RecvView &v = recvView();
    if (!v.active)
        return false;
    const QString c = code.trimmed();
    if (c.isEmpty())
        return false;
    if (v.tab != u"receive") {
        v.tab = u"receive"_s;   // a scan on another tab: back to the invoice
    }
    v.linking = -1;
    const RecvFound f = recvResolve(c, v.vendorId);
    if (f.item) {
        if (!itemIsRetail(*f.item))
            return fail(tr("%1 isn't a retail item.").arg(qs(f.item->name)));
        const bool ok = recvAddItem(qs(f.item->id), f.perUom);
        if (ok && f.vendorItem && v.last >= 0 && v.last < int(v.lines.size())) {
            Line &l = v.lines[std::size_t(v.last)];
            if (l.vendorItem.empty())
                l.vendorItem = f.vendorItem->vendorItem;
            if (l.uom.empty())
                l.uom = f.vendorItem->uom;
            recvTouched();
        }
        return ok;
    }
    if (f.vendorItem && f.vendorItem->storeUse) {   // a store-use item by its number
        Line l;
        l.kind = recv::kStoreUse;
        l.vendorItem = f.vendorItem->vendorItem;
        l.name = f.vendorItem->description;
        l.uom = f.vendorItem->uom;
        l.qty = 1;
        v.lines.push_back(l);
        v.last = int(v.lines.size()) - 1;
        recvTouched();
        return true;
    }
    if (looksLikeBarcode(c)) {
        // Not on file: set aside on its own line (link it, add it, or leave it out before saving).
        const std::string key = BuilderIndex::codeKey(ss(c));
        for (std::size_t i = 0; i < v.lines.size(); ++i)
            if (v.lines[i].kind == recv::kStock && v.lines[i].itemId.empty() && BuilderIndex::codeKey(v.lines[i].upc) == key) {
                v.lines[i].qty += 1;
                v.last = int(i);
                recvTouched();
                return true;
            }
        Line l;
        l.kind = recv::kStock;
        l.upc = ss(c);
        l.qty = 1;
        v.lines.push_back(l);
        v.last = int(v.lines.size()) - 1;
        v.message = tr("%1 isn't on file: link it to an item, add it, or leave it out (it's set aside below).").arg(c);
        recvTouched();
        return true;
    }
    return recvFind(c);
}

bool PosService::recvFind(const QString &text)
{
    RecvView &v = recvView();
    const QString t = text.trimmed();
    v.choices.clear();
    if (t.isEmpty()) {
        emit adminChanged();
        return true;
    }
    if (v.linking < 0) {
        const RecvFound f = recvResolve(t, v.vendorId);
        if (f.item || (f.vendorItem && f.vendorItem->storeUse) || looksLikeBarcode(t))
            return recvScan(t);
    } else if (const RecvFound f = recvResolve(t, v.vendorId); f.item) {
        return recvLink(v.linking, qs(f.item->id));
    }
    const QString low = t.toLower();
    for (const MenuItem &m : s_->menu) {
        if (m.isModifier || !itemIsRetail(m) || !qs(m.name).toLower().contains(low))
            continue;
        v.choices.append(QVariantMap{{u"id"_s, qs(m.id)}, {u"name"_s, qs(m.name)},
                                     {u"upc"_s, m.barcodes.empty() ? QString() : qs(m.barcodes.front())},
                                     {u"price"_s, format(m.price)}});
        if (v.choices.size() >= 30)
            break;
    }
    v.message = v.choices.isEmpty() ? tr("No retail item has \"%1\" in its name or UPC.").arg(t) : QString();
    emit adminChanged();
    return !v.choices.isEmpty();
}

// Only the choices (the import's Link, and Link on a line: nothing is added by it).
bool PosService::recvSearch(const QString &text)
{
    RecvView &v = recvView();
    const QString t = text.trimmed();
    v.choices.clear();
    if (const RecvFound f = recvResolve(t, v.imp.loaded ? v.imp.vendorId : v.vendorId); f.item && itemIsRetail(*f.item))
        v.choices.append(QVariantMap{{u"id"_s, qs(f.item->id)}, {u"name"_s, qs(f.item->name)},
                                     {u"upc"_s, f.item->barcodes.empty() ? QString() : qs(f.item->barcodes.front())},
                                     {u"price"_s, format(f.item->price)}});
    const QString low = t.toLower();
    for (const MenuItem &m : s_->menu) {
        if (low.isEmpty() || v.choices.size() >= 30)
            break;
        if (m.isModifier || !itemIsRetail(m) || !qs(m.name).toLower().contains(low))
            continue;
        v.choices.append(QVariantMap{{u"id"_s, qs(m.id)}, {u"name"_s, qs(m.name)},
                                     {u"upc"_s, m.barcodes.empty() ? QString() : qs(m.barcodes.front())},
                                     {u"price"_s, format(m.price)}});
    }
    v.message = v.choices.isEmpty() && !t.isEmpty() ? tr("No retail item has \"%1\" in its name or UPC.").arg(t) : QString();
    emit adminChanged();
    return !v.choices.isEmpty();
}

bool PosService::recvSetLine(int index, const QVariantMap &f)
{
    RecvView &v = recvView();
    if (index < 0 || index >= int(v.lines.size()))
        return false;
    Line l = v.lines[std::size_t(index)];
    const auto cents = [&](const QString &key, std::int64_t &out, bool allowNone) -> bool {
        if (!f.contains(key))
            return true;
        const QString t = f.value(key).toString().trimmed();
        if (t.isEmpty()) {
            out = allowNone ? -1 : 0;
            return true;
        }
        const auto c = recv::centsOf(ss(t));
        if (!c || *c < 0)
            return false;
        out = *c;
        return true;
    };
    if (f.contains(u"qty"_s)) {
        bool ok = false;
        const double q = f.value(u"qty"_s).toString().trimmed().toDouble(&ok);
        if (!ok || q < 0 || q > 100000)
            return fail(tr("Type how many, like 1 or 10."));
        l.qty = q;
    }
    if (f.contains(u"perUom"_s)) {
        bool ok = false;
        const double p = f.value(u"perUom"_s).toString().trimmed().toDouble(&ok);
        if (!ok || !(p > 0) || p > 10000)
            return fail(tr("Units per vendor unit: how many of the item one is (a carton: 10)."));
        l.perUom = p;
    }
    if (f.contains(u"uom"_s))
        l.uom = ss(f.value(u"uom"_s).toString().trimmed().left(12));
    if (f.contains(u"vendorItem"_s))
        l.vendorItem = ss(f.value(u"vendorItem"_s).toString().trimmed().left(40));
    if (f.contains(u"name"_s) && l.itemId.empty())
        l.name = ss(f.value(u"name"_s).toString().trimmed().left(80));
    // (Prepaid sales tax is the invoice's one charge, + Prepaid tax, owner 2026-11-27: not on a line.)
    if (!cents(u"cost"_s, l.costCents, true) || !cents(u"otp"_s, l.otpCents, false)
        || !cents(u"allowance"_s, l.allowanceCents, false) || !cents(u"tax"_s, l.taxCents, false))
        return fail(tr("Type an amount, like 12.34."));
    if (f.contains(u"amount"_s)) {
        const auto c = recv::centsOf(ss(f.value(u"amount"_s).toString()));
        if (!c)
            return fail(tr("Type an amount, like 12.34."));
        l.amountCents = l.kind == recv::kCredit ? std::llabs(*c) : *c;
    }
    if (f.contains(u"kind"_s)) {
        const std::string k = ss(f.value(u"kind"_s).toString());
        if (!recv::validKind(k))
            return false;
        // Only an item's line turns into store use and back (amounts stay amounts).
        if (recv::itemKind(l.kind) != recv::itemKind(k))
            return fail(tr("Add a delivery, credit or other amount with its own button."));
        l.kind = k;
    }
    v.lines[std::size_t(index)] = l;
    v.last = index;
    recvTouched();
    return true;
}

bool PosService::recvRemoveLine(int index)
{
    RecvView &v = recvView();
    if (index < 0 || index >= int(v.lines.size()))
        return false;
    v.lines.erase(v.lines.begin() + index);
    v.linking = -1;
    v.last = -1;
    recvTouched();
    return true;
}

bool PosService::recvAddCharge(const QString &kind)
{
    RecvView &v = recvView();
    const std::string k = ss(kind);
    if (!recv::validKind(k) || k == recv::kStock)
        return false;
    Line l;
    l.kind = k;
    l.name = ss(kindLabel(k));
    if (k == recv::kStoreUse) {
        l.name.clear();
        l.qty = 1;
    }
    v.lines.push_back(l);
    v.last = int(v.lines.size()) - 1;
    recvTouched();
    return true;
}

bool PosService::recvLinkStart(int index)
{
    RecvView &v = recvView();
    if (index < 0 || index >= int(v.lines.size()) || !v.lines[std::size_t(index)].itemId.empty())
        v.linking = -1;
    else
        v.linking = index;
    v.choices.clear();
    v.message = v.linking >= 0 ? tr("Find the item it is (part of its name), then touch it: the code becomes its alt UPC.") : QString();
    emit adminChanged();
    return true;
}

// The code goes on the item as an alt UPC (Link): the next scan finds it.
bool PosService::recvAddCode(const std::string &itemId, const QString &code)
{
    if (!looksLikeBarcode(code))
        return true;
    for (std::size_t i = 0; i < s_->menu.size(); ++i) {
        MenuItem &m = s_->menu[i];
        if (m.id != itemId)
            continue;
        if (std::ranges::any_of(m.barcodes, [&](const std::string &b) { return sameBarcode(b, ss(code.trimmed())); }))
            return true;
        const QString why = barcodeProblem({code.trimmed()}, itemId);
        if (!why.isEmpty())
            return fail(why);
        m.barcodes.push_back(ss(code.trimmed()));
        if (s_->sink)
            s_->sink->saveMenuItem(m, int(i));
        logChange(tr("Menu"), qs(m.name), tr("Alt UPC"), QString(), code.trimmed() + u" "_s + tr("(Receive retail)"));
        menuChanged();
        return true;
    }
    return fail(tr("No item %1.").arg(qs(itemId)));
}

bool PosService::recvLink(int index, const QString &itemId)
{
    RecvView &v = recvView();
    if (index < 0 || index >= int(v.lines.size()))
        return false;
    const MenuItem *m = findItem(itemId);
    if (!m || !itemIsRetail(*m))
        return fail(tr("%1 isn't a retail item.").arg(itemId));
    const QString code = qs(v.lines[std::size_t(index)].upc);
    const std::string id = m->id;
    if (!recvAddCode(id, code))
        return false;
    m = findItem(qs(id));
    Line &l = v.lines[std::size_t(index)];
    const Line fresh = recvLineFor(*m, l.perUom, v.vendorId);
    l.itemId = fresh.itemId;
    l.name = fresh.name;
    l.systemCostCents = fresh.systemCostCents;
    l.leftOut = false;
    v.linking = -1;
    v.choices.clear();
    v.last = index;
    v.message = tr("%1 is now an alt UPC of %2.").arg(code, qs(m->name));
    recvTouched();
    return true;
}

bool PosService::recvLeaveOut(int index, bool out)
{
    RecvView &v = recvView();
    if (index < 0 || index >= int(v.lines.size()) || !v.lines[std::size_t(index)].itemId.empty())
        return false;
    v.lines[std::size_t(index)].leftOut = out;
    recvTouched();
    return true;
}

// Quick add: a new retail item from what was scanned (receive: {index}) or a file's code (import: {code}).
bool PosService::recvQuickAdd(const QVariantMap &r)
{
    RecvView &v = recvView();
    const QString name = r.value(u"name"_s).toString().trimmed();
    const QString upc = r.value(u"upc"_s).toString().trimmed();
    if (name.isEmpty())
        return fail(tr("The new item needs a name."));
    const auto price = recv::centsOf(ss(r.value(u"price"_s).toString()));
    if (!price || *price < 0)
        return fail(tr("Type its price, like 9.99."));
    std::int64_t cost = 0;
    if (const QString t = r.value(u"cost"_s).toString().trimmed(); !t.isEmpty()) {
        const auto c = recv::centsOf(ss(t));
        if (!c || *c < 0)
            return fail(tr("Type an amount, like 12.34."));
        cost = *c;
    }
    const double packSize = r.value(u"packSize"_s).toDouble();
    if (!upc.isEmpty()) {
        if (const QString why = barcodeProblem({upc}, std::string()); !why.isEmpty())
            return fail(why);
    }
    MenuItem m;
    m.id = "recv-" + (upc.isEmpty() ? ss(QString::number(now())) : BuilderIndex::codeKey(ss(upc)));
    while (findItem(qs(m.id)))
        m.id += "-1";
    m.name = ss(name);
    m.price = Money::fromCents(*price);
    m.taxClass = TaxClass::Merchandise;
    m.kitchenHide = true;
    if (!upc.isEmpty())
        m.barcodes = {ss(upc)};
    m.recipe.push_back({"__retail__", 1.0});
    if (cost > 0)
        m.recipe.push_back({"__cost__", double(cost) / 100.0});   // a new item: its first system cost
    const QString category = r.value(u"category"_s).toString();
    for (const MenuCategory &c : s_->categories())
        if (qs(c.id) == category) {
            m.family = c.id;
            if (!c.taxClass.empty())
                m.taxClass = taxClassFromString(c.taxClass);
        }
    s_->menu.push_back(m);
    if (s_->sink)
        s_->sink->saveMenuItem(s_->menu.back(), int(s_->menu.size()) - 1);
    logChange(tr("Menu"), name, tr("Added"), QString(), tr("from Receive retail"));
    menuChanged();
    if (r.contains(u"index"_s)) {
        const int index = r.value(u"index"_s).toInt();
        if (index >= 0 && index < int(v.lines.size())) {
            Line &l = v.lines[std::size_t(index)];
            l.itemId = m.id;
            l.name = m.name;
            l.systemCostCents = cost;
            l.leftOut = false;
            if (packSize > 0)
                l.perUom = packSize;
            v.last = index;
            recvTouched();
        }
    }
    if (r.contains(u"code"_s)) {   // the import: the code is this new item
        const std::string code = ss(r.value(u"code"_s).toString());
        v.imp.decisions[code] = {"link", m.id};
        ++v.imp.changes;
    }
    v.message = tr("%1 added.").arg(name);
    emit adminChanged();
    return true;
}

// --- unfinished invoices (owner 2026-11-27: "I pressed Save meaning save my work") ---------------------
// The invoice on the screen is kept as it's worked on (autosaved: every change), in its own table, so a
// restart or an update loses nothing; nothing goes on hand until Receive. Save for later sets it aside
// (Unfinished: any screen can open it, one screen at a time). A screen left with an invoice on it (a restart)
// finds it there again when Receive retail opens.

Receipt *PosService::recvDraft(std::int64_t id) const
{
    if (id <= 0)
        return nullptr;
    for (Receipt &d : s_->retailDrafts)
        if (d.id == id)
            return &d;
    return nullptr;
}

const QObject *PosService::recvHolder(std::int64_t id) const
{
    const auto it = s_->recvHeldBy.find(id);
    return it == s_->recvHeldBy.end() ? nullptr : it->second.first;
}

void PosService::recvRelease()
{
    RecvView &v = recvView();
    if (v.draftId && recvHolder(v.draftId) == this)
        s_->recvHeldBy.erase(v.draftId);
}

void PosService::recvReset()
{
    RecvView &v = recvView();
    recvRelease();
    // (The vendor and date stay for the next invoice, as they come in a stack; not a received one's.)
    if (v.editingId || v.date.isEmpty()) {
        v.vendorId.clear();
        v.date = QDateTime::fromMSecsSinceEpoch(now()).date().toString(u"yyyy-MM-dd"_s);
    }
    v.draftId = 0;
    v.editingId = 0;
    v.baseEdits = 0;
    v.confirm.clear();
    v.lines.clear();
    v.invoice.clear();
    v.note.clear();
    v.typedTotal = -1;
    v.choices.clear();
    v.linking = -1;
    v.last = -1;
    v.message.clear();
}

void PosService::recvLoad(const Receipt &d)
{
    RecvView &v = recvView();
    v.draftId = d.id;
    v.editingId = d.reopenOf;
    v.baseEdits = d.baseEdits;
    v.vendorId = qs(d.vendorId);
    v.invoice = qs(d.invoice);
    v.date = qs(d.date);
    v.note = qs(d.note);
    v.typedTotal = d.typedTotal;
    v.lines = d.lines;
    recv::liftPrepaid(v.lines, ss(tr("Prepaid tax")));   // (kept per unit before 2026-11-27: one charge now)
    v.confirm.clear();
    v.choices.clear();
    v.linking = -1;
    v.last = -1;
    s_->recvHeldBy[d.id] = {this, terminal_};
}

// The invoice here changed: kept as unfinished (once it has an invoice number or a line).
void PosService::recvTouched()
{
    RecvView &v = recvView();
    v.confirm.clear();
    if (v.draftId) {
        if (const QObject *h = recvHolder(v.draftId); h && h != this) {
            const QString at = s_->recvHeldBy[v.draftId].second;
            recvReset();
            v.message = tr("That invoice was opened at %1, so the change here wasn't kept. It's on Unfinished.").arg(at);
            emit adminChanged();
            return;
        }
    }
    Receipt *d = recvDraft(v.draftId);
    bool created = false;
    if (!d) {
        if (v.lines.empty() && v.invoice.trimmed().isEmpty() && !v.editingId) {
            emit adminChanged();
            return;   // nothing to keep yet
        }
        Receipt fresh;
        fresh.id = ++s_->lastRetailDraftId;
        fresh.at = now();
        s_->retailDrafts.push_back(fresh);
        d = &s_->retailDrafts.back();
        v.draftId = d->id;
        s_->recvHeldBy[d->id] = {this, terminal_};
        created = true;
    }
    d->vendorId = ss(v.vendorId);
    const Vendor *vendor = vendorById(v.vendorId);
    d->vendorName = vendor ? vendor->name : std::string();
    d->invoice = ss(v.invoice.trimmed());
    d->date = ss(v.date);
    d->note = ss(v.note);
    d->typedTotal = v.typedTotal;
    d->lines = v.lines;
    d->by = user() ? user()->name : ss(terminal_);
    d->terminal = ss(terminal_);
    d->changedAt = now();
    d->parked = false;
    d->reopenOf = v.editingId;
    d->baseEdits = v.baseEdits;
    if (s_->sink)
        s_->sink->saveRetailDraft(*d);
    if (created)
        emit s_->adminChanged();   // (on every screen's Unfinished)
    else
        emit adminChanged();
}

// A new invoice: the one here (if it has anything) stays on Unfinished.
bool PosService::recvClear()
{
    RecvView &v = recvView();
    if (Receipt *d = recvDraft(v.draftId); d && recvHolder(d->id) == this) {
        d->parked = true;
        if (s_->sink)
            s_->sink->saveRetailDraft(*d);
    }
    const bool kept = v.draftId != 0;
    recvReset();
    if (kept)
        v.message = tr("The invoice you were on is kept on Unfinished.");
    emit s_->adminChanged();
    return true;
}

bool PosService::recvSaveForLater()
{
    RecvView &v = recvView();
    if (!v.draftId && v.lines.empty() && v.invoice.trimmed().isEmpty() && !v.editingId)
        return fail(tr("There's nothing on this invoice to save yet."));
    recvTouched();
    Receipt *d = recvDraft(v.draftId);
    if (!d)
        return false;   // (taken over by another screen: recvTouched said so)
    d->parked = true;
    if (s_->sink)
        s_->sink->saveRetailDraft(*d);
    const QString what = d->invoice.empty() ? tr("The invoice") : tr("Invoice #%1").arg(qs(d->invoice));
    recvReset();
    v.message = tr("%1 is saved for later, on Unfinished. Nothing was added to on hand.").arg(what);
    emit notice(v.message);
    emit s_->adminChanged();
    return true;
}

bool PosService::recvOpenDraft(std::int64_t id, bool takeOver)
{
    RecvView &v = recvView();
    const Receipt *d = recvDraft(id);
    if (!d)
        return fail(tr("That unfinished invoice isn't there any more."));
    if (const QObject *h = recvHolder(id); h && h != this && !takeOver)
        return fail(tr("That invoice is open at %1. Open it here anyway to take it over.").arg(s_->recvHeldBy[id].second));
    if (v.draftId != id) {
        // The one here first: kept (Unfinished).
        if (Receipt *mine = recvDraft(v.draftId); mine && recvHolder(mine->id) == this) {
            mine->parked = true;
            if (s_->sink)
                s_->sink->saveRetailDraft(*mine);
        }
        recvRelease();
    }
    recvLoad(*d);
    v.tab = u"receive"_s;
    v.message = d->reopenOf ? tr("Changing invoice #%1 from %2, received before: Receive puts only the changes on hand.").arg(qs(d->invoice), qs(d->vendorName))
                            : tr("Invoice #%1 opened. Nothing is on hand from it until you Receive it.").arg(qs(d->invoice));
    emit s_->adminChanged();
    return true;
}

bool PosService::recvDeleteDraft(std::int64_t id)
{
    RecvView &v = recvView();
    const auto it = std::ranges::find_if(s_->retailDrafts, [&](const Receipt &d) { return d.id == id; });
    if (it == s_->retailDrafts.end())
        return fail(tr("That unfinished invoice isn't there any more."));
    if (const QObject *h = recvHolder(id); h && h != this)
        return fail(tr("That invoice is open at %1.").arg(s_->recvHeldBy[id].second));
    const QString what = tr("Invoice #%1 from %2").arg(qs(it->invoice), qs(it->vendorName));
    const bool changes = it->reopenOf != 0;   // (changes to a received invoice: forgotten; it stays as received)
    s_->recvHeldBy.erase(id);
    s_->retailDrafts.erase(it);
    if (s_->sink)
        s_->sink->deleteRetailDraft(id);
    if (v.draftId == id)
        recvReset();
    logChange(tr("Inventory"), what, changes ? tr("Changes not kept") : tr("Unfinished invoice deleted"), QString(), QString());
    v.message = changes ? tr("The changes to %1 were not kept; it stays as it was received.").arg(what)
                        : tr("%1 deleted (it was never received).").arg(what);
    emit s_->adminChanged();
    return true;
}

// What an invoice teaches about the vendor's items (both receiving and history only).
void PosService::recvLearn(const Receipt &r)
{
    bool any = false;
    for (const Line &l : r.lines) {
        if (!recv::itemKind(l.kind) || l.leftOut)
            continue;
        if (l.itemId.empty() && l.vendorItem.empty())
            continue;
        const std::string key = recv::vendorItemKey(r.vendorId, l.itemId, l.vendorItem);
        const bool isNew = !s_->vendorItems.contains(key);
        VendorItem &v = s_->vendorItems[key];
        v.vendorId = r.vendorId;
        v.itemId = l.itemId;
        const VendorItem before = v;
        recv::learn(v, l, r.date, r.invoice);
        if ((isNew || v != before) && s_->sink)
            s_->sink->saveVendorItem(v);
        any = true;
    }
    if (any)
        ++s_->vendorItemsRevision;
}

// A receipt saved: kept, its stock on hand (unless history only), the vendor's items learned, logged.
void PosService::recvKeep(Receipt r)
{
    r.id = ++s_->lastRetailReceiptId;
    r.at = now();
    r.by = user() ? user()->name : ss(terminal_);
    r.changedAt = 0;
    r.terminal.clear();
    r.parked = false;
    r.reopenOf = 0;
    r.baseEdits = 0;
    if (!r.historyOnly)
        for (const Line &l : r.lines)
            if (l.kind == recv::kStock && !l.itemId.empty() && !l.leftOut)
                recvStock(l);
    recvLearn(r);
    s_->retailReceipts.push_back(r);
    if (s_->sink)
        s_->sink->saveRetailReceipt(r);
}

// Where a stock line goes on hand: a box / carton and its single (pos_retail_stock.cpp) by what came, cartons
// sealed (a carton's UPC, or a vendor's unit of whole cartons: a case of 6 cartons) and packs loose; else the
// item's count. (None: not a stock line on file.)
std::pair<std::string, double> PosService::recvStockTarget(const Line &l) const
{
    if (l.kind != recv::kStock || l.itemId.empty() || l.leftOut)
        return {};
    const RetailFamily *f = retailFamilyOf(l.itemId);
    if (f && l.itemId == f->singleId && f->packSize > 1 && l.perUom >= f->packSize
        && std::abs(std::fmod(l.perUom, double(f->packSize))) < 1e-9)
        return {f->parentId, l.units() / f->packSize};
    return {l.itemId, l.units()};
}

void PosService::recvStock(const Line &l)
{
    const auto [id, n] = recvStockTarget(l);
    addRetailStock(id, n);
}

namespace {

// The lines that can be received: none still waiting for an item, store use described; lines at 0 dropped.
QString recvCheckLines(const std::vector<Line> &in, std::vector<Line> &out)
{
    out.clear();
    for (const Line &l : in) {
        if (recv::itemKind(l.kind) && l.qty == 0)
            continue;   // (a line taken down to 0)
        if (l.kind == recv::kStock && l.itemId.empty() && !l.leftOut)
            return PosService::tr("%1 isn't on file: link it to an item, add it, or leave it out.").arg(qs(l.upc.empty() ? l.name : l.upc));
        if (l.kind == recv::kStoreUse && l.itemId.empty() && l.name.empty())
            return PosService::tr("Say what the store-use line is (its description).");
        out.push_back(l);
    }
    return {};
}

} // namespace

bool PosService::recvConfirmCancel()
{
    recvView().confirm.clear();
    emit adminChanged();
    return true;
}

// Receive: the invoice finished, its stock on hand. Not tying out (or no total typed) asks first; the same
// vendor and invoice # received before offers to open that one and add to it.
bool PosService::recvReceive(bool anyway)
{
    RecvView &v = recvView();
    v.confirm.clear();
    if (v.draftId)
        if (const QObject *h = recvHolder(v.draftId); h && h != this)
            return fail(tr("That invoice is open at %1 now.").arg(s_->recvHeldBy[v.draftId].second));
    const Vendor *vendor = vendorById(v.vendorId);
    if (!vendor)
        return fail(tr("Choose the vendor first."));
    if (v.invoice.trimmed().isEmpty())
        return fail(tr("Type the invoice number."));
    if (v.date.isEmpty())
        return fail(tr("Type the invoice date."));
    if (const std::int64_t dup = recvDuplicate(vendor->id, ss(v.invoice), v.editingId)) {
        if (v.editingId)
            return fail(tr("Invoice #%1 from %2 was already received (another one).").arg(v.invoice, qs(vendor->name)));
        v.confirm = {{u"kind"_s, u"duplicate"_s}, {u"receiptId"_s, qint64(dup)},
                     {u"text"_s, tr("Invoice #%1 from %2 was already received. Open it to add these lines to it?").arg(v.invoice, qs(vendor->name))}};
        emit adminChanged();
        return false;
    }
    std::vector<Line> lines;
    if (const QString why = recvCheckLines(v.lines, lines); !why.isEmpty())
        return fail(why);
    if (lines.empty() && !v.editingId)
        return fail(tr("Scan or find the items first."));
    Receipt r;
    r.typedTotal = v.typedTotal;
    r.lines = lines;
    if (!anyway) {
        const auto off = r.offBy();
        if (!off || *off != 0) {
            v.confirm = {{u"kind"_s, off ? u"off"_s : u"nototal"_s},
                         {u"text"_s, off ? tr("Off by %1 from the invoice's total. Receive anyway?").arg(format(Money::fromCents(*off)))
                                         : tr("No invoice total typed, so it can't tie out. Receive anyway?")}};
            emit adminChanged();
            return false;
        }
    }
    if (v.editingId)
        return recvFinishEdit();
    r.vendorId = vendor->id;
    r.vendorName = vendor->name;
    r.invoice = ss(v.invoice.trimmed());
    r.date = ss(v.date);
    r.note = ss(v.note);
    for (Line &l : r.lines)
        if (const MenuItem *m = findItem(qs(l.itemId)); m && !l.itemId.empty())
            l.systemCostCents = systemCostOf(*m);   // (as it is now, beside the invoice's: never changed)
    const recv::Totals t = r.totals();
    const auto off = r.offBy();
    recvKeep(r);
    logChange(tr("Inventory"), tr("Invoice #%1 from %2").arg(qs(r.invoice), qs(r.vendorName)), tr("Received"), QString(),
              tr("%1 units in stock, invoice %2").arg(qtyText(t.units), format(Money::fromCents(t.total))));
    const QString done = !off ? tr("Invoice #%1 from %2 received: %3 units in stock, %4.").arg(qs(r.invoice), qs(r.vendorName), qtyText(t.units), format(Money::fromCents(t.total)))
                       : *off == 0 ? tr("Invoice #%1 from %2 received: %3 units in stock; it ties out at %4.").arg(qs(r.invoice), qs(r.vendorName), qtyText(t.units), format(Money::fromCents(t.total)))
                                   : tr("Invoice #%1 from %2 received: %3 units in stock; the lines are %4, off by %5 from the total typed.")
                                         .arg(qs(r.invoice), qs(r.vendorName), qtyText(t.units), format(Money::fromCents(t.total)), format(Money::fromCents(*off)));
    // Finished: no longer unfinished.
    if (const std::int64_t id = v.draftId) {
        std::erase_if(s_->retailDrafts, [&](const Receipt &d) { return d.id == id; });
        if (s_->sink)
            s_->sink->deleteRetailDraft(id);
    }
    recvReset();
    v.message = done;
    emit notice(done);
    emit s_->adminChanged();
    return true;
}

// Receive on a received invoice reopened: only the difference goes on hand (each line's item, a carton line's
// cartons sealed), the change kept on the invoice (who, when, the lines), the vendor's last cost if newer.
bool PosService::recvFinishEdit()
{
    RecvView &v = recvView();
    const auto it = std::ranges::find_if(s_->retailReceipts, [&](const Receipt &r) { return r.id == v.editingId; });
    if (it == s_->retailReceipts.end())
        return fail(tr("That received invoice isn't there any more."));
    if (int(it->edits.size()) != v.baseEdits)
        return fail(tr("Invoice #%1 was changed somewhere else since it was opened here. Delete this on Unfinished and open it again.").arg(qs(it->invoice)));
    std::vector<Line> lines;
    if (const QString why = recvCheckLines(v.lines, lines); !why.isEmpty())
        return fail(why);
    Receipt was = *it;
    Receipt now_ = was;
    now_.invoice = ss(v.invoice.trimmed());
    now_.date = ss(v.date);
    now_.note = ss(v.note);
    now_.typedTotal = v.typedTotal;
    for (Line &l : lines)   // a line added now: the system cost now (the others keep theirs, from when they came)
        if (l.systemCostCents == 0 && !l.itemId.empty())
            if (const MenuItem *m = findItem(qs(l.itemId)))
                l.systemCostCents = systemCostOf(*m);
    now_.lines = lines;
    recv::Edit e;
    e.at = now();
    e.by = user() ? user()->name : ss(terminal_);
    e.terminal = ss(terminal_);
    e.totalBefore = was.totals().total;
    e.totalAfter = now_.totals().total;
    e.unitsBefore = was.totals().units;
    e.unitsAfter = now_.totals().units;
    {
        std::vector<Line> wasLines = was.lines;   // (its prepaid tax per unit, kept before, as the one charge it is now)
        recv::liftPrepaid(wasLines, ss(tr("Prepaid tax")));
        e.lines = recv::diffLines(wasLines, now_.lines);
    }
    const bool headerSame = was.invoice == now_.invoice && was.date == now_.date && was.note == now_.note && was.typedTotal == now_.typedTotal;
    QString done;
    if (e.lines.empty() && headerSame) {
        done = tr("No changes to invoice #%1.").arg(qs(was.invoice));
    } else {
        // On hand: what each item had from this invoice, and has now.
        double moved = 0;
        if (!was.historyOnly) {
            std::map<std::string, double> delta;
            for (const Line &l : was.lines)
                if (const auto [id, n] = recvStockTarget(l); !id.empty())
                    delta[id] -= n;
            for (const Line &l : now_.lines)
                if (const auto [id, n] = recvStockTarget(l); !id.empty())
                    delta[id] += n;
            for (const auto &[id, n] : delta)
                if (std::abs(n) > 1e-9) {
                    addRetailStock(id, n);
                    moved += n;
                }
        }
        now_.edits.push_back(e);
        *it = now_;
        if (s_->sink)
            s_->sink->saveRetailReceipt(*it);
        recvLearn(*it);   // (a newer cost only: learn() keeps the newest invoice's)
        logChange(tr("Inventory"), tr("Invoice #%1 from %2").arg(qs(now_.invoice), qs(now_.vendorName)), tr("Changed after receiving"),
                  format(Money::fromCents(e.totalBefore)), format(Money::fromCents(e.totalAfter)));
        done = was.historyOnly
                   ? tr("Invoice #%1 from %2 changed (history only: on hand unchanged).").arg(qs(now_.invoice), qs(now_.vendorName))
                   : tr("Invoice #%1 from %2 changed: %3 line(s) different, %4 units on hand from the change, invoice now %5.")
                         .arg(qs(now_.invoice), qs(now_.vendorName)).arg(e.lines.size())
                         .arg((moved > 0 ? u"+"_s : QString()) + qtyText(moved), format(Money::fromCents(e.totalAfter)));
    }
    if (const std::int64_t id = v.draftId) {
        std::erase_if(s_->retailDrafts, [&](const Receipt &d) { return d.id == id; });
        if (s_->sink)
            s_->sink->deleteRetailDraft(id);
    }
    recvReset();
    v.message = done;
    v.tab = u"list"_s;
    v.showId = was.id;
    emit notice(done);
    emit s_->adminChanged();
    return true;
}

bool PosService::recvShowReceived(std::int64_t id)
{
    RecvView &v = recvView();
    v.showId = std::ranges::any_of(s_->retailReceipts, [&](const Receipt &r) { return r.id == id; }) ? id : 0;
    emit adminChanged();
    return true;
}

// Invoices received > Open > Edit: the invoice on the screen to change (an unfinished change of it already
// there: that one). The one on the screen first stays on Unfinished.
bool PosService::recvEditReceived(std::int64_t id)
{
    RecvView &v = recvView();
    const auto it = std::ranges::find_if(s_->retailReceipts, [&](const Receipt &r) { return r.id == id; });
    if (it == s_->retailReceipts.end())
        return fail(tr("That received invoice isn't there any more."));
    for (const Receipt &d : s_->retailDrafts)
        if (d.reopenOf == id)
            return recvOpenDraft(d.id, false);
    if (v.draftId || !v.lines.empty())
        recvClear();
    recvReset();
    v.editingId = id;
    v.baseEdits = int(it->edits.size());
    v.vendorId = qs(it->vendorId);
    v.invoice = qs(it->invoice);
    v.date = qs(it->date);
    v.note = qs(it->note);
    v.typedTotal = it->typedTotal;
    v.lines = it->lines;
    recv::liftPrepaid(v.lines, ss(tr("Prepaid tax")));   // (kept per unit before 2026-11-27: one charge now)
    v.tab = u"receive"_s;
    v.showId = 0;
    v.message = tr("Changing invoice #%1 from %2, received before: Receive puts only the changes on hand.").arg(qs(it->invoice), qs(it->vendorName));
    recvTouched();   // (kept as unfinished from now on)
    return true;
}

// The duplicate's offer: the received invoice opened to change, with the lines on the screen added to it.
bool PosService::recvAddToReceived(std::int64_t id)
{
    RecvView &v = recvView();
    const std::vector<Line> adding = v.lines;
    const std::int64_t was = v.draftId;
    if (was && recvHolder(was) == this) {   // (this invoice becomes part of the received one)
        std::erase_if(s_->retailDrafts, [&](const Receipt &d) { return d.id == was; });
        s_->recvHeldBy.erase(was);
        if (s_->sink)
            s_->sink->deleteRetailDraft(was);
        v.draftId = 0;
    }
    v.lines.clear();
    if (!recvEditReceived(id))
        return false;
    for (const Line &l : adding)
        v.lines.push_back(l);
    v.message = tr("%n line(s) added to the invoice received before. Check them, then Receive (only they go on hand).", "", int(adding.size()));
    recvTouched();
    return true;
}

bool PosService::recvRange(const QString &from, const QString &until)
{
    RecvView &v = recvView();
    v.from = from.trimmed();
    v.until = until.trimmed();
    emit adminChanged();
    return true;
}

// --- Import invoice file ------------------------------------------------------------------------------

// A file line's code: its UPC, else its item number, else its description (what a decision is kept by).
std::string PosService::recvCodeKey(const FileLine &f)
{
    if (!f.upc.empty())
        return BuilderIndex::codeKey(f.upc);
    if (!f.itemNo.empty())
        return "#" + recv::numberKey(f.itemNo);
    return "~" + BuilderIndex::asciiLower(f.description);
}

bool PosService::recvImportText(const QString &text, const QString &fileName)
{
    RecvView &v = recvView();
    RecvView::Import &im = v.imp;
    const auto rows = recv::readCsv(ss(text));
    const int header = recv::headerRow(rows);
    if (header < 0)
        return fail(tr("That file needs a header row naming its columns (Invoice, Date, Item, UPC, Qty, Price…). "
                       "An Excel file: save it as CSV first."));
    im = RecvView::Import{};
    im.loaded = true;
    im.fileName = fileName;
    im.rows = rows;
    im.header = header;
    im.vendorId = v.vendorId;
    im.map = recv::guessMapping(rows[std::size_t(header)]);
    // The columns this vendor's files had last time, by their headers.
    if (const Vendor *vendor = vendorById(im.vendorId); vendor && !vendor->importMap.empty())
        recvApplySavedMap(*vendor);
    v.tab = u"import"_s;
    v.message = tr("%n row(s) read. Check the columns, then the invoices below.", "", int(rows.size()) - header - 1);
    emit adminChanged();
    return true;
}

void PosService::recvApplySavedMap(const Vendor &vendor)
{
    RecvView::Import &im = recvView().imp;
    if (im.header < 0)
        return;
    const QJsonObject saved = QJsonDocument::fromJson(QByteArray::fromStdString(vendor.importMap)).object();
    const auto &header = im.rows[std::size_t(im.header)];
    for (auto it = saved.begin(); it != saved.end(); ++it) {
        const std::string field = ss(it.key());
        const std::string want = recv::headerWords(ss(it.value().toString()));
        if (want.empty()) {   // saved as "none"
            im.map.erase(field);
            continue;
        }
        for (int i = 0; i < int(header.size()); ++i)
            if (recv::headerWords(header[std::size_t(i)]) == want) {
                im.map[field] = i;
                break;
            }
    }
}

bool PosService::recvImportVendor(const QString &vendorId)
{
    RecvView::Import &im = recvView().imp;
    const Vendor *vendor = vendorById(vendorId);
    if (!vendor)
        return fail(tr("There is no vendor '%1'.").arg(vendorId));
    im.vendorId = vendorId;
    if (!vendor->importMap.empty())
        recvApplySavedMap(*vendor);
    ++im.changes;
    emit adminChanged();
    return true;
}

bool PosService::recvImportMap(const QString &field, int column)
{
    RecvView::Import &im = recvView().imp;
    const std::string f = ss(field);
    if (std::ranges::find(recv::fields(), f) == recv::fields().end())
        return false;
    if (column < 0 || im.header < 0 || column >= int(im.rows[std::size_t(im.header)].size()))
        im.map.erase(f);
    else
        im.map[f] = column;
    ++im.changes;
    emit adminChanged();
    return true;
}

bool PosService::recvImportMode(const QString &mode)
{
    RecvView::Import &im = recvView().imp;
    if (mode != u"receive" && mode != u"history")
        return false;
    im.mode = mode;
    ++im.changes;
    emit adminChanged();
    return true;
}

bool PosService::recvImportShow(int invoice)
{
    recvView().imp.show = invoice;
    emit adminChanged();
    return true;
}

bool PosService::recvImportDecide(const QString &code, const QString &action, const QString &itemId)
{
    RecvView &v = recvView();
    RecvView::Import &im = v.imp;
    const std::string c = ss(code);
    if (action.isEmpty()) {
        im.decisions.erase(c);
    } else if (action == u"link") {
        const MenuItem *m = findItem(itemId);
        if (!m || !itemIsRetail(*m))
            return fail(tr("%1 isn't a retail item.").arg(itemId));
        // A UPC: it goes on the item as an alt UPC now (an item number is kept for the vendor on import).
        if (!c.empty() && c[0] != '#' && c[0] != '~') {
            // (the code key is a UPC without its leading zeros: the file's own spelling is used when known)
            QString upc = code;
            for (const FileInvoice &inv : recvImportInvoices())
                for (const FileLine &f : inv.lines)
                    if (!f.upc.empty() && recvCodeKey(f) == c)
                        upc = qs(f.upc);
            if (!recvAddCode(m->id, upc))
                return false;
        }
        im.decisions[c] = {"link", m->id};
    } else if (action == u"skip" || action == u"storeuse") {
        im.decisions[c] = {ss(action), std::string()};
    } else {
        return false;
    }
    ++im.changes;
    emit adminChanged();
    return true;
}

bool PosService::recvImportCancel()
{
    RecvView &v = recvView();
    v.imp = RecvView::Import{};
    v.tab = u"receive"_s;
    v.message.clear();
    emit adminChanged();
    return true;
}

std::vector<FileInvoice> PosService::recvImportInvoices() const
{
    const RecvView::Import &im = recvView().imp;
    if (!im.loaded || im.header < 0)
        return {};
    return recv::group(recv::fileLines(im.rows, im.header, im.map));
}

// The file's invoices as they'd be saved: each routed to its vendor by the account column (else the vendor
// chosen), its lines matched, and what keeps it from being taken.
std::vector<PosService::RecvPreview> PosService::recvImportPreview() const
{
    const RecvView::Import &im = recvView().imp;
    std::vector<RecvPreview> out;
    const Vendor *chosen = vendorById(im.vendorId);
    for (const FileInvoice &f : recvImportInvoices()) {
        RecvPreview p;
        p.file = f;
        const Vendor *vendor = chosen;
        if (!f.account.empty()) {
            const std::string want = recv::numberKey(f.account);
            vendor = nullptr;
            for (const Vendor &vv : s_->settings.vendors)
                if (!vv.account.empty() && recv::numberKey(vv.account) == want)
                    vendor = &vv;
            if (!vendor)
                p.problem = tr("account %1 isn't any vendor's account number (Manager > Inventory > Vendors)").arg(qs(f.account));
        } else if (!vendor) {
            p.problem = tr("choose the vendor");
        }
        if (p.problem.isEmpty() && f.invoice.empty())
            p.problem = tr("no invoice number");
        if (p.problem.isEmpty() && f.date.empty())
            p.problem = tr("no date");
        p.receipt.historyOnly = im.mode == u"history";
        p.receipt.invoice = f.invoice;
        p.receipt.date = f.date;
        p.receipt.typedTotal = f.typedTotal;
        p.receipt.source = ss(im.fileName);
        if (vendor) {
            p.receipt.vendorId = vendor->id;
            p.receipt.vendorName = vendor->name;
            p.duplicate = !f.invoice.empty() && recvDuplicate(vendor->id, f.invoice);
        }
        for (const FileLine &fl : f.lines) {
            Line l = recv::lineOf(fl);
            std::string code;
            if (recv::itemKind(l.kind)) {
                RecvFound found;
                if (vendor)
                    if (const VendorItem *vi = vendorItemByNumber(vendor->id, fl.itemNo)) {
                        found.vendorItem = vi;
                        if (!vi->itemId.empty())
                            found.item = findItem(qs(vi->itemId));
                        found.perUom = vi->perUom > 0 ? vi->perUom : 1;
                    }
                if (!found.item && !(found.vendorItem && found.vendorItem->storeUse) && !fl.upc.empty()) {
                    const RecvFound byUpc = recvResolve(qs(fl.upc), QString());
                    if (byUpc.item)
                        found = byUpc;
                }
                if (!found.item && !(found.vendorItem && found.vendorItem->storeUse)) {
                    const auto d = im.decisions.find(recvCodeKey(fl));
                    if (d != im.decisions.end()) {
                        if (d->second.action == "link")
                            found.item = findItem(qs(d->second.itemId));
                        else if (d->second.action == "storeuse")
                            l.kind = recv::kStoreUse;
                        else
                            l.leftOut = true;
                    }
                }
                if (found.vendorItem && found.vendorItem->storeUse && !found.item)
                    l.kind = recv::kStoreUse;
                if (found.item) {
                    l.itemId = found.item->id;
                    if (l.name.empty())
                        l.name = found.item->name;
                    l.systemCostCents = systemCostOf(*found.item);
                    if (!(fl.perUom > 0))
                        l.perUom = found.perUom > 0 ? found.perUom : 1;
                } else if (l.kind == recv::kStock && !l.leftOut) {
                    code = recvCodeKey(fl);
                    ++p.unmatched;
                }
            }
            p.codes.push_back(code);
            p.receipt.lines.push_back(l);
        }
        if (auto pl = recv::prepaidLine(f, ss(tr("Prepaid tax")))) {   // the invoice's prepaid tax: one charge
            p.codes.emplace_back();
            p.receipt.lines.push_back(std::move(*pl));
        }
        out.push_back(std::move(p));
    }
    return out;
}

bool PosService::recvImportCommit()
{
    RecvView &v = recvView();
    RecvView::Import &im = v.imp;
    if (!im.loaded)
        return fail(tr("Choose an invoice file first."));
    const std::vector<RecvPreview> preview = recvImportPreview();
    int taken = 0, duplicates = 0, problems = 0, waiting = 0;
    double units = 0;
    std::set<std::string> vendorsUsed;
    for (const RecvPreview &p : preview) {
        if (!p.problem.isEmpty()) {
            ++problems;
            continue;
        }
        if (p.duplicate) {
            ++duplicates;
            continue;
        }
        if (p.unmatched > 0) {
            ++waiting;
            continue;
        }
        recvKeep(p.receipt);
        if (!p.receipt.historyOnly)
            units += p.receipt.totals().units;
        vendorsUsed.insert(p.receipt.vendorId);
        ++taken;
    }
    // The columns, kept for this vendor's (and the routed vendors') next file: field -> its header.
    if (im.header >= 0) {
        QJsonObject saved;
        const auto &header = im.rows[std::size_t(im.header)];
        for (const std::string &f : recv::fields()) {
            const auto it = im.map.find(f);
            saved.insert(qs(f), it != im.map.end() && it->second >= 0 && it->second < int(header.size())
                                    ? qs(header[std::size_t(it->second)]) : QString());
        }
        const std::string text = QJsonDocument(saved).toJson(QJsonDocument::Compact).toStdString();
        vendorsUsed.insert(ss(im.vendorId));
        bool changed = false;
        for (Vendor &vv : s_->settings.vendors)
            if (vendorsUsed.contains(vv.id) && vv.importMap != text) {
                vv.importMap = text;
                changed = true;
            }
        if (changed)
            settingsChanged();
    }
    if (taken > 0)
        logChange(tr("Inventory"), tr("Invoice file %1").arg(im.fileName), im.mode == u"history" ? tr("History only") : tr("Received"),
                  QString(), tr("%n invoice(s)", "", taken));
    QStringList parts;
    parts << (im.mode == u"history" ? tr("%n invoice(s) loaded as history (item numbers and costs; on hand unchanged).", "", taken)
                                    : tr("%n invoice(s) received: %1 units in stock.", "", taken).arg(qtyText(units)));
    if (duplicates)
        parts << tr("%n already received (refused).", "", duplicates);
    if (problems)
        parts << tr("%n with a problem (see the list).", "", problems);
    if (waiting)
        parts << tr("%n waiting for items not on file.", "", waiting);
    v.message = parts.join(u" "_s);
    emit notice(v.message);
    ++im.changes;
    emit adminChanged();
    emit s_->adminChanged();
    return true;
}

// --- what the screen shows ----------------------------------------------------------------------------

QVariantMap PosService::retailReceiving() const
{
    if (!recv_ || !recv_->active || !user())
        return {};
    const RecvView &v = *recv_;
    const auto money = [&](std::int64_t c) { return format(Money::fromCents(c)); };
    const auto amount = [](std::int64_t c) { return c < 0 ? QString() : qs(mass::centsText(c)); };
    QVariantList vendors;
    for (const Vendor &vv : s_->settings.vendors)
        vendors.append(QVariantMap{{u"id"_s, qs(vv.id)}, {u"name"_s, qs(vv.name)}, {u"account"_s, qs(vv.account)},
                                   {u"group"_s, qs(vv.group)}});
    QVariantList categories;
    for (const MenuCategory &c : s_->categories())
        if (c.scope == kScopeRetail || c.retailOnly)
            categories.append(QVariantMap{{u"id"_s, qs(c.id)}, {u"name"_s, qs(c.name)}});
    // The invoice being received.
    Receipt draft;
    draft.typedTotal = v.typedTotal;
    draft.lines = v.lines;
    QVariantList lines;
    for (std::size_t i = 0; i < v.lines.size(); ++i) {
        const Line &l = v.lines[i];
        QString last;
        if (const VendorItem *vi = vendorItemOf(ss(v.vendorId), l.itemId); vi && !l.itemId.empty() && vi->lastCostCents >= 0)
            last = vi->lastAllowanceCents > 0
                       ? tr("last %1 per %2 net (%3 less %4 allowance) (%5)")
                             .arg(money(*vi->lastNetCents()), vi->uom.empty() ? tr("unit") : qs(vi->uom), money(vi->lastCostCents),
                                  money(vi->lastAllowanceCents), qs(vi->lastDate))
                       : tr("last %1 per %2 (%3)").arg(money(vi->lastCostCents), vi->uom.empty() ? tr("unit") : qs(vi->uom), qs(vi->lastDate));
        const auto unit = l.unitCostCents();
        lines.append(QVariantMap{
            {u"index"_s, int(i)}, {u"kind"_s, qs(l.kind)}, {u"kindText"_s, kindLabel(l.kind)}, {u"itemId"_s, qs(l.itemId)},
            {u"name"_s, l.itemId.empty() && l.kind == recv::kStock ? tr("Not on file: %1").arg(qs(l.upc)) : qs(l.name)},
            {u"upc"_s, qs(l.upc)}, {u"vendorItem"_s, qs(l.vendorItem)}, {u"uom"_s, qs(l.uom)}, {u"perUom"_s, l.perUom},
            {u"qty"_s, l.qty}, {u"units"_s, qtyText(l.units())}, {u"cost"_s, amount(l.costCents)},
            {u"otp"_s, l.otpCents ? amount(l.otpCents) : QString()}, {u"prepaid"_s, l.prepaidCents ? amount(l.prepaidCents) : QString()},
            {u"allowance"_s, l.allowanceCents ? amount(l.allowanceCents) : QString()},
            {u"tax"_s, l.taxCents ? amount(l.taxCents) : QString()}, {u"amount"_s, amount(std::llabs(l.amountCents))},
            {u"extended"_s, money(l.extended())}, {u"onHand"_s, l.itemId.empty() ? QString() : retailOnHandText(l.itemId)},
            {u"stockAs"_s, [&] {   // what it puts on hand: "5 ctn" (sealed), "3 pk" (loose); "" not in a family
                const RetailFamily *f = retailFamilyOf(l.itemId);
                if (!f || l.itemId != f->singleId)
                    return QString();
                if (f->packSize > 1 && l.perUom >= f->packSize && std::abs(std::fmod(l.perUom, double(f->packSize))) < 1e-9)
                    return u"%1 %2"_s.arg(qtyText(l.units() / f->packSize), f->parentUnit);
                return u"%1 %2"_s.arg(qtyText(l.units()), f->singleUnit);
            }()},
            {u"systemCost"_s, l.systemCostCents > 0 ? money(l.systemCostCents) : QString()},
            {u"unitCost"_s, unit ? money(std::llround(*unit)) : QString()},
            {u"different"_s, unit && l.systemCostCents > 0 && std::llround(*unit) != l.systemCostCents},
            {u"last"_s, last}, {u"aside"_s, l.kind == recv::kStock && l.itemId.empty()}, {u"leftOut"_s, l.leftOut},
            {u"isItem"_s, recv::itemKind(l.kind)}});
    }
    const recv::Totals t = draft.totals();
    const auto off = draft.offBy();
    const QVariantMap totals{{u"stock"_s, money(t.stock)}, {u"otp"_s, money(t.otp)}, {u"prepaid"_s, money(t.prepaid)},
                             {u"allowances"_s, money(t.allowances)}, {u"hasAllowances"_s, t.allowances != 0},
                             {u"storeUse"_s, money(t.storeUse)}, {u"useTax"_s, money(t.useTax)}, {u"delivery"_s, money(t.delivery)},
                             {u"credits"_s, money(t.credits)}, {u"other"_s, money(t.other)}, {u"total"_s, money(t.total)},
                             {u"units"_s, qtyText(t.units)}};
    QString tie;
    if (off)
        tie = *off == 0 ? tr("Ties out") : tr("Off by %1").arg(money(*off));
    // The invoices received (the list).
    QVariantList recent;
    {
        QDate from = QDate::fromString(v.from, u"yyyy-MM-dd"_s), until = QDate::fromString(v.until, u"yyyy-MM-dd"_s);
        const QDate today = QDateTime::fromMSecsSinceEpoch(now()).date();
        if (!from.isValid())
            from = today.addDays(-60);
        if (!until.isValid())
            until = today;
        for (auto it = s_->retailReceipts.rbegin(); it != s_->retailReceipts.rend(); ++it) {
            const QDate d = QDate::fromString(qs(it->date), u"yyyy-MM-dd"_s);
            if (d.isValid() && (d < from || d > until))
                continue;
            const recv::Totals rt = it->totals();
            const auto ro = it->offBy();
            recent.append(QVariantMap{{u"id"_s, qint64(it->id)}, {u"date"_s, qs(it->date)}, {u"vendor"_s, qs(it->vendorName)},
                                      {u"invoice"_s, qs(it->invoice)}, {u"lines"_s, int(it->lines.size())},
                                      {u"total"_s, money(rt.total)}, {u"units"_s, qtyText(rt.units)}, {u"by"_s, qs(it->by)},
                                      {u"history"_s, it->historyOnly}, {u"changes"_s, int(it->edits.size())},
                                      {u"tie"_s, !ro ? QString() : *ro == 0 ? tr("ties out") : tr("off by %1").arg(money(*ro))}});
            if (recent.size() >= 300)
                break;
        }
    }
    // Import.
    QVariantMap importState;
    const RecvView::Import &im = v.imp;
    if (im.loaded) {
        QVariantList headers, fieldsList;
        if (im.header >= 0)
            for (const std::string &h : im.rows[std::size_t(im.header)])
                headers.append(qs(h));
        for (const std::string &f : recv::fields()) {
            const auto it = im.map.find(f);
            fieldsList.append(QVariantMap{{u"field"_s, qs(f)}, {u"label"_s, fieldLabel(f)},
                                          {u"column"_s, it == im.map.end() ? -1 : it->second}});
        }
        const std::vector<RecvPreview> preview = recvImportPreview();
        QVariantList invoices;
        int ready = 0, duplicates = 0, problems = 0, waiting = 0;
        struct Code {
            QString upc, itemNo, description, cost;
            int rows = 0;
        };
        std::map<std::string, Code> codes;
        for (std::size_t i = 0; i < preview.size(); ++i) {
            const RecvPreview &p = preview[i];
            const recv::Totals pt = p.receipt.totals();
            const auto po = p.receipt.offBy();
            QString status;
            if (!p.problem.isEmpty()) {
                status = p.problem;
                ++problems;
            } else if (p.duplicate) {
                status = tr("already received");
                ++duplicates;
            } else if (p.unmatched > 0) {
                status = tr("%n item(s) not on file", "", p.unmatched);
                ++waiting;
            } else {
                status = p.receipt.historyOnly ? tr("ready (history only)") : tr("ready");
                ++ready;
            }
            invoices.append(QVariantMap{
                {u"index"_s, int(i)}, {u"vendor"_s, qs(p.receipt.vendorName)}, {u"account"_s, qs(p.file.account)},
                {u"invoice"_s, qs(p.file.invoice)}, {u"date"_s, qs(p.file.date)}, {u"lines"_s, int(p.receipt.lines.size())},
                {u"total"_s, money(pt.total)}, {u"typed"_s, p.file.typedTotal >= 0 ? money(p.file.typedTotal) : QString()},
                {u"tie"_s, !po ? tr("no total in the file") : *po == 0 ? tr("ties out") : tr("off by %1").arg(money(*po))},
                {u"ties"_s, po && *po == 0}, {u"status"_s, status},
                {u"ready"_s, p.problem.isEmpty() && !p.duplicate && p.unmatched == 0}});
            for (std::size_t j = 0; j < p.file.lines.size() && j < p.codes.size(); ++j)
                if (!p.codes[j].empty()) {
                    Code &c = codes[p.codes[j]];
                    const FileLine &fl = p.file.lines[j];
                    c.upc = qs(fl.upc);
                    c.itemNo = qs(fl.itemNo);
                    c.description = qs(fl.description);
                    if (fl.unitPrice >= 0)
                        c.cost = money(fl.unitPrice);
                    ++c.rows;
                }
        }
        // The codes not on file, and the ones decided (so a decision can be taken back).
        QVariantList unmatched;
        for (const auto &[key, c] : codes) {
            unmatched.append(QVariantMap{{u"code"_s, qs(key)}, {u"upc"_s, c.upc}, {u"itemNo"_s, c.itemNo},
                                         {u"description"_s, c.description}, {u"cost"_s, c.cost}, {u"rows"_s, c.rows},
                                         {u"decision"_s, QString()}});
            if (unmatched.size() >= 300)
                break;
        }
        QVariantList decided;
        for (const auto &[key, d] : im.decisions) {
            const MenuItem *m = d.itemId.empty() ? nullptr : findItem(qs(d.itemId));
            decided.append(QVariantMap{{u"code"_s, qs(key)}, {u"action"_s, qs(d.action)},
                                       {u"item"_s, m ? qs(m->name) : QString()}});
        }
        QVariantList shown;
        if (im.show >= 0 && im.show < int(preview.size()))
            for (const Line &l : preview[std::size_t(im.show)].receipt.lines) {
                const auto unit = l.unitCostCents();
                shown.append(QVariantMap{{u"kind"_s, kindLabel(l.kind)}, {u"name"_s, qs(l.name)}, {u"upc"_s, qs(l.upc)},
                                         {u"vendorItem"_s, qs(l.vendorItem)}, {u"qty"_s, qtyText(l.qty)},
                                         {u"perUom"_s, qtyText(l.perUom)}, {u"uom"_s, qs(l.uom)},
                                         {u"cost"_s, amount(l.costCents)}, {u"extended"_s, money(l.extended())},
                                         {u"allowance"_s, l.allowanceCents ? amount(l.allowanceCents) : QString()},
                                         {u"found"_s, !l.itemId.empty()}, {u"leftOut"_s, l.leftOut},
                                         {u"unitCost"_s, unit ? money(std::llround(*unit)) : QString()},
                                         {u"systemCost"_s, l.systemCostCents > 0 ? money(l.systemCostCents) : QString()}});
            }
        importState = {{u"loaded"_s, true}, {u"fileName"_s, im.fileName}, {u"headers"_s, headers}, {u"fields"_s, fieldsList},
                  {u"vendorId"_s, im.vendorId}, {u"mode"_s, im.mode}, {u"invoices"_s, invoices}, {u"unmatched"_s, unmatched},
                  {u"unmatchedCount"_s, int(codes.size())}, {u"decided"_s, decided}, {u"show"_s, im.show}, {u"shown"_s, shown},
                  {u"ready"_s, ready}, {u"duplicates"_s, duplicates}, {u"problems"_s, problems}, {u"waiting"_s, waiting},
                  {u"rows"_s, int(im.rows.size()) - im.header - 1}};
    }
    const auto when = [](std::int64_t ms) { return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms).toString(u"yyyy-MM-dd hh:mm"_s) : QString(); };
    // Unfinished: the invoices not received yet, the newest change first.
    QVariantList unfinished;
    {
        std::vector<const Receipt *> ds;
        for (const Receipt &d : s_->retailDrafts)
            ds.push_back(&d);
        std::ranges::sort(ds, [](const Receipt *a, const Receipt *b) { return a->changedAt > b->changedAt; });
        for (const Receipt *d : ds) {
            const auto held = s_->recvHeldBy.find(d->id);
            const bool elsewhere = held != s_->recvHeldBy.end() && held->second.first != this;
            unfinished.append(QVariantMap{
                {u"id"_s, qint64(d->id)}, {u"vendor"_s, qs(d->vendorName)}, {u"invoice"_s, qs(d->invoice)}, {u"date"_s, qs(d->date)},
                {u"lines"_s, int(d->lines.size())}, {u"units"_s, qtyText(d->totals().units)}, {u"total"_s, money(d->totals().total)},
                {u"by"_s, qs(d->by)}, {u"changed"_s, when(d->changedAt)}, {u"terminal"_s, qs(d->terminal)},
                {u"openAt"_s, elsewhere ? held->second.second : QString()}, {u"here"_s, d->id == v.draftId},
                {u"reopen"_s, d->reopenOf != 0}, {u"saved"_s, d->parked}});
        }
    }
    // Invoices received > Open: its lines and the changes made to it.
    QVariantMap shownReceipt;
    for (const Receipt &rc : s_->retailReceipts) {
        if (rc.id != v.showId || !v.showId)
            continue;
        QVariantList rl;
        for (const Line &l : rc.lines)
            rl.append(QVariantMap{{u"kind"_s, kindLabel(l.kind)}, {u"name"_s, qs(l.name.empty() ? l.upc : l.name)},
                                  {u"qty"_s, qtyText(l.qty)}, {u"uom"_s, qs(l.uom)}, {u"perUom"_s, qtyText(l.perUom)},
                                  {u"cost"_s, amount(l.costCents)}, {u"extended"_s, money(l.extended())},
                                  {u"allowance"_s, l.allowanceCents ? amount(l.allowanceCents) : QString()},
                                  {u"isItem"_s, recv::itemKind(l.kind)}, {u"leftOut"_s, l.leftOut}});
        QVariantList el;
        for (auto e = rc.edits.rbegin(); e != rc.edits.rend(); ++e) {
            QStringList what;
            for (const recv::EditLine &x : e->lines) {
                if (x.qtyBefore == 0 && x.extBefore == 0)
                    what << tr("%1 added (%2, %3)").arg(qs(x.name), qtyText(x.qtyAfter), money(x.extAfter));
                else if (x.qtyAfter == 0 && x.extAfter == 0)
                    what << tr("%1 taken off (was %2, %3)").arg(qs(x.name), qtyText(x.qtyBefore), money(x.extBefore));
                else
                    what << tr("%1: %2 → %3 (%4 → %5)").arg(qs(x.name), qtyText(x.qtyBefore), qtyText(x.qtyAfter), money(x.extBefore), money(x.extAfter));
            }
            el.append(QVariantMap{{u"when"_s, when(e->at)}, {u"by"_s, qs(e->by)}, {u"terminal"_s, qs(e->terminal)},
                                  {u"text"_s, tr("total %1 → %2, units %3 → %4").arg(money(e->totalBefore), money(e->totalAfter),
                                                                                    qtyText(e->unitsBefore), qtyText(e->unitsAfter))},
                                  {u"lines"_s, what}});
        }
        const auto ro = rc.offBy();
        bool beingChanged = false;
        for (const Receipt &d : s_->retailDrafts)
            beingChanged = beingChanged || d.reopenOf == rc.id;
        shownReceipt = {{u"id"_s, qint64(rc.id)}, {u"vendor"_s, qs(rc.vendorName)}, {u"invoice"_s, qs(rc.invoice)},
                        {u"date"_s, qs(rc.date)}, {u"by"_s, qs(rc.by)}, {u"at"_s, when(rc.at)}, {u"total"_s, money(rc.totals().total)},
                        {u"units"_s, qtyText(rc.totals().units)}, {u"history"_s, rc.historyOnly},
                        {u"tie"_s, !ro ? QString() : *ro == 0 ? tr("ties out") : tr("off by %1").arg(money(*ro))},
                        {u"lines"_s, rl}, {u"edits"_s, el}, {u"beingChanged"_s, beingChanged}};
    }
    // The invoice here: a received one being changed; open at another screen (taken over); received before.
    QVariantMap editing;
    if (v.editingId)
        for (const Receipt &rc : s_->retailReceipts)
            if (rc.id == v.editingId)
                editing = {{u"id"_s, qint64(rc.id)}, {u"invoice"_s, qs(rc.invoice)}, {u"vendor"_s, qs(rc.vendorName)},
                           {u"at"_s, when(rc.at)}, {u"by"_s, qs(rc.by)}};
    QString heldAt;
    if (v.draftId)
        if (const auto held = s_->recvHeldBy.find(v.draftId); held != s_->recvHeldBy.end() && held->second.first != this)
            heldAt = held->second.second;
    std::int64_t duplicateOf = 0;
    if (!v.editingId && !v.invoice.trimmed().isEmpty() && !v.vendorId.isEmpty())
        duplicateOf = recvDuplicate(ss(v.vendorId), ss(v.invoice));
    return {{u"active"_s, true}, {u"tab"_s, v.tab}, {u"vendors"_s, vendors}, {u"categories"_s, categories},
            {u"draftId"_s, qint64(v.draftId)}, {u"editing"_s, editing}, {u"heldAt"_s, heldAt}, {u"confirm"_s, v.confirm},
            {u"duplicateOf"_s, qint64(duplicateOf)}, {u"unfinished"_s, unfinished}, {u"received"_s, shownReceipt},
            {u"vendorId"_s, v.vendorId}, {u"invoice"_s, v.invoice}, {u"date"_s, v.date},
            {u"total"_s, v.typedTotal >= 0 ? qs(mass::centsText(v.typedTotal)) : QString()}, {u"note"_s, v.note},
            {u"lines"_s, lines}, {u"totals"_s, totals}, {u"tie"_s, tie}, {u"ties"_s, off && *off == 0},
            {u"last"_s, v.last}, {u"linking"_s, v.linking}, {u"choices"_s, v.choices}, {u"message"_s, v.message},
            {u"recent"_s, recent}, {u"from"_s, v.from}, {u"until"_s, v.until}, {u"importFile"_s, importState}};
}

// --- reports ------------------------------------------------------------------------------------------

namespace {

std::int64_t dayStart(const std::string &ymd)
{
    const QDate d = QDate::fromString(qs(ymd), u"yyyy-MM-dd"_s);
    return d.isValid() ? QDateTime(d, QTime(0, 0)).toMSecsSinceEpoch() : -1;
}

} // namespace

// Purchases (retail): the invoices dated in [from, to) (byReceived: received then), by vendor group, by vendor
// and by invoice: the stock's cost with OTP (gross), the allowances taken off it, prepaid sales tax, delivery,
// store use and the tax on it, credits, other and the invoices' totals.
Report PosService::retailPurchasesReport(std::int64_t from, std::int64_t to, const ReportContext &ctx, bool byReceived) const
{
    Report r;
    r.id = "retailpurchases";
    r.title = "Purchases (retail)";
    r.subtitle = ctx.period;
    r.columns = {"", "Stock", "Allowances", "Prepaid tax", "Delivery", "Store use", "Use tax", "Credits", "Other", "Total"};
    std::vector<const Receipt *> in;
    for (const Receipt &rc : s_->retailReceipts) {
        const std::int64_t at = byReceived ? rc.at : dayStart(rc.date) >= 0 ? dayStart(rc.date) : rc.at;
        if (at >= from && at < to)
            in.push_back(&rc);
    }
    std::ranges::sort(in, [](const Receipt *a, const Receipt *b) { return std::tie(a->date, a->id) < std::tie(b->date, b->id); });
    if (in.empty()) {
        r.note("No retail invoices in this period (Manager -> Inventory -> Receive retail).");
        return r;
    }
    const auto row = [&](const std::string &label, const recv::Totals &t) {
        return std::vector<std::string>{label, ctx.money(Money::fromCents(t.stock)), ctx.money(Money::fromCents(t.allowances)),
                                        ctx.money(Money::fromCents(t.prepaid)),
                                        ctx.money(Money::fromCents(t.delivery)), ctx.money(Money::fromCents(t.storeUse)),
                                        ctx.money(Money::fromCents(t.useTax)), ctx.money(Money::fromCents(t.credits)),
                                        ctx.money(Money::fromCents(t.other)), ctx.money(Money::fromCents(t.total))};
    };
    const auto add = [](recv::Totals &a, const recv::Totals &b) {
        a.stock += b.stock;
        a.otp += b.otp;
        a.allowances += b.allowances;
        a.prepaid += b.prepaid;
        a.storeUse += b.storeUse;
        a.useTax += b.useTax;
        a.delivery += b.delivery;
        a.credits += b.credits;
        a.other += b.other;
        a.total += b.total;
        a.units += b.units;
    };
    std::map<std::string, recv::Totals> byGroup, byVendor;
    recv::Totals all;
    bool groups = false;
    for (const Receipt *rc : in) {
        const recv::Totals t = rc->totals();
        std::string group = rc->vendorName;
        if (const Vendor *vv = vendorById(qs(rc->vendorId)); vv && !vv->group.empty()) {
            group = vv->group;
            groups = true;
        }
        add(byGroup[group], t);
        add(byVendor[rc->vendorName.empty() ? std::string("(no vendor)") : rc->vendorName], t);
        add(all, t);
    }
    r.note("Stock: the items' cost with OTP / excise, before allowances (" + ctx.money(Money::fromCents(all.otp))
           + " OTP / excise in it). Allowances: the off-invoice allowances on the items, taken off apart ("
           + ctx.money(Money::fromCents(all.allowances)) + "); the stock's net cost is Stock plus Allowances ("
           + ctx.money(Money::fromCents(all.stock + all.allowances)) + ").");
    if (groups) {
        r.section("By vendor group");
        for (const auto &[g, t] : byGroup)
            r.line(row(g, t));
    }
    r.section("By vendor");
    for (const auto &[name, t] : byVendor)
        r.line(row(name, t));
    r.total(row("Total", all));
    r.section("Invoices");
    std::vector<std::string> offs;
    for (const Receipt *rc : in) {
        const recv::Totals t = rc->totals();
        r.line(row(rc->date + " " + rc->vendorName + " #" + rc->invoice + (rc->historyOnly ? " (history)" : "")
                       + (rc->edits.empty() ? "" : " (changed)"), t));
        if (const auto off = rc->offBy(); off && *off != 0)
            offs.push_back(rc->date + " " + rc->vendorName + " #" + rc->invoice + ": typed "
                           + ctx.money(Money::fromCents(rc->typedTotal)) + ", lines " + ctx.money(Money::fromCents(t.total))
                           + ", off by " + ctx.money(Money::fromCents(*off)));
    }
    if (!offs.empty()) {
        r.section("Invoices that don't tie out");
        for (const std::string &o : offs)
            r.note(o);
    }
    r.note("Prepaid sales tax is kept apart from the items' cost. Delivery is not spread into item costs. "
           "Store use: supplies the store uses (never in stock). (history): loaded from a file of past invoices, "
           "on hand unchanged. (changed): changed after it was received (as it is now).");
    return r;
}

// Cost differences (retail): invoice net cost per unit (with OTP, less the allowance) against the item's system cost when it came,
// for the invoices received in [from, to). A plain list: the full review (by brand, Reviewed, Adjust prices)
// is a later step.
Report PosService::retailCostDiffsReport(std::int64_t from, std::int64_t to, const ReportContext &ctx, bool byReceived) const
{
    Report r;
    r.id = "retailcostdiffs";
    r.title = "Cost differences (retail)";
    r.subtitle = ctx.period;
    r.columns = {"Item", "Invoice", "System cost", "Invoice cost", "Allowance", "Difference", "Units", "Total"};
    std::int64_t sum = 0;
    int n = 0;
    for (const Receipt &rc : s_->retailReceipts) {
        if (rc.historyOnly)
            continue;   // (their system cost is today's, not then)
        const std::int64_t at = byReceived ? rc.at : dayStart(rc.date) >= 0 ? dayStart(rc.date) : rc.at;
        if (at < from || at >= to)
            continue;
        for (const Line &l : rc.lines) {
            const auto unit = l.unitCostCents();
            if (!recv::itemKind(l.kind) || l.itemId.empty() || !unit || l.systemCostCents <= 0)
                continue;
            const double diff = *unit - double(l.systemCostCents);
            if (std::abs(diff) < 0.5)
                continue;
            const std::int64_t total = std::llround(diff * l.units());
            sum += total;
            ++n;
            char units[32];
            std::snprintf(units, sizeof units, "%g", std::round(l.units() * 1000) / 1000);
            r.line({l.name, rc.vendorName + " #" + rc.invoice + " " + rc.date, ctx.money(Money::fromCents(l.systemCostCents)),
                    ctx.money(Money::fromCents(std::llround(*unit))),
                    l.allowanceCents ? "-" + ctx.money(Money::fromCents(std::llround(l.unitAllowanceCents()))) : std::string(),
                    (diff > 0 ? "+" : "") + ctx.money(Money::fromCents(std::llround(diff))), units,
                    (total > 0 ? "+" : "") + ctx.money(Money::fromCents(total))});
        }
    }
    if (n == 0)
        r.note("No invoice costs differed from the system cost in this period.");
    else
        r.total({"Total", "", "", "", "", "", "", ctx.money(Money::fromCents(sum))});
    r.note("Receiving never changes the system cost or the price. Invoice cost: the invoice's net price per unit, with "
           "OTP / excise and less any allowance (Allowance: the allowance in it, per unit), over the units in the "
           "vendor's unit. The full Cost Differences review comes later.");
    return r;
}

} // namespace vt::app
