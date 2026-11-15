// PosService: the setup guide - a new store's name, logo, look, taxes, first
// menu items and staff, a step at a time (SetupGuide.qml).

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QPointer>
#include <QThreadPool>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

QVariantMap PosService::setupInfo() const
{
    if (!can(QString::fromLatin1(perm::Manager)))
        return {{u"done"_s, s_->settings.setupDone}};
    QStringList families;
    int items = 0;
    for (const MenuItem &m : s_->menu) {
        if (m.isModifier)
            continue;
        ++items;
        if (!m.family.empty() && !families.contains(qs(m.family)))
            families << qs(m.family);
    }
    QVariantList staff;
    int samples = 0;
    for (const Employee &e : s_->employees) {
        if (!e.active)
            continue;
        samples += e.sample && !(retireMeAtFinish_ && user() && e.id == user()->id) ? 1 : 0;
        staff.append(QVariantMap{{u"name"_s, qs(e.name)}, {u"role"_s, qs(e.role)}, {u"sample"_s, e.sample}});
    }
    const auto pct = [](std::int64_t ppm) { return double(ppm) / 10000.0; };
    return {{u"done"_s, s_->settings.setupDone}, {u"storeName"_s, qs(s_->settings.storeName)},
            {u"receiptHeader"_s, qs(s_->settings.receiptHeader)}, {u"logo"_s, qs(s_->settings.displayLogo)},
            {u"receiptLogo"_s, s_->settings.receiptLogo}, {u"foodTax"_s, pct(s_->settings.tax.foodPpm)},
            {u"alcoholTax"_s, pct(s_->settings.tax.alcoholPpm)}, {u"families"_s, families}, {u"items"_s, items},
            {u"staff"_s, staff}, {u"samples"_s, samples}};
}

namespace {
// The first (only) record of a settings panel, with changes on top.
QVariantMap withChanges(QVariantMap record, const QVariantMap &changes)
{
    for (auto it = changes.begin(); it != changes.end(); ++it)
        record.insert(it.key(), it.value());
    return record;
}
} // namespace

bool PosService::setupStore(const QString &name, const QString &receiptLines)
{
    if (!require(perm::Manager, tr("Setting up the store")))
        return false;
    if (name.trimmed().isEmpty())
        return fail(tr("Type the store's name."));
    const QVariantMap store = adminRecords(u"store"_s).value(0).toMap();
    return adminSave(u"store"_s, 0, withChanges(store, {{u"storeName"_s, name.trimmed()}, {u"receiptHeader"_s, receiptLines}}));
}

bool PosService::setupLogo(const QString &ref, bool onReceipts)
{
    if (!require(perm::Manager, tr("Setting up the store")))
        return false;
    const QVariantMap store = adminRecords(u"store"_s).value(0).toMap();
    return adminSave(u"store"_s, 0, withChanges(store, {{u"displayLogo"_s, ref}, {u"receiptLogo"_s, onReceipts}}));
}

bool PosService::setupTaxes(double foodPercent, double alcoholPercent)
{
    if (!require(perm::Manager, tr("Setting up the store")))
        return false;
    const QVariantMap taxes = adminRecords(u"taxes"_s).value(0).toMap();
    return adminSave(u"taxes"_s, 0, withChanges(taxes, {{u"food"_s, foodPercent}, {u"alcohol"_s, alcoholPercent}}));
}

bool PosService::setupAddItem(const QString &name, double price, const QString &family)
{
    if (!require(perm::Manager, tr("Setting up the store")))
        return false;
    if (name.trimmed().isEmpty())
        return fail(tr("Type the item's name."));
    QVariantMap item = adminNewRecord(u"menu"_s);
    item.insert(u"name"_s, name.trimmed());
    item.insert(u"price"_s, price);
    item.insert(u"family"_s, family.trimmed().toLower());
    return adminSave(u"menu"_s, -1, item);
}

bool PosService::setupAddEmployee(const QString &name, const QString &role, const QString &pin)
{
    if (!require(perm::Manager, tr("Setting up the store")))
        return false;
    QVariantMap e = adminNewRecord(u"employees"_s);
    e.insert(u"name"_s, name.trimmed());
    e.insert(u"role"_s, role);
    e.insert(u"pin"_s, pin);
    return adminSave(u"employees"_s, -1, e);
}

bool PosService::setupRetireSamples()
{
    if (!require(perm::Manager, tr("Setting up the store")))
        return false;
    // Someone must still be able to run the store: a manager of their own.
    const bool ownManager = std::ranges::any_of(s_->employees, [](const Employee &e) {
        return e.active && !e.sample && (e.role == "manager" || e.role == "admin");
    });
    if (!ownManager)
        return fail(tr("First add yourself as a manager, with a PIN of your own."));
    // Whoever is running the guide goes last, at Finish (then logs in with their own PIN).
    int n = 0;
    for (Employee &e : s_->employees) {
        if (!e.active || !e.sample)
            continue;
        if (user() && e.id == user()->id) {
            retireMeAtFinish_ = true;
            continue;
        }
        e.active = false;
        if (s_->sink)
            s_->sink->saveEmployee(e);
        ++n;
    }
    ++s_->adminRevision;
    emit s_->staffChanged();
    emit s_->adminChanged();
    emit notice(tr("%n sample employee(s) turned off", nullptr, n));
    return true;
}

bool PosService::setupFinish(bool done)
{
    if (!require(perm::Manager, tr("Setting up the store")))
        return false;
    s_->settings.setupDone = done;
    s_->saveSettings();
    ++s_->adminRevision;
    emit s_->adminChanged();
    if (retireMeAtFinish_ && user()) {   // the sample manager who ran it: off now (logs out)
        retireMeAtFinish_ = false;
        const std::string me = user()->id;   // user() is gone once they're off
        for (Employee &e : s_->employees) {
            if (e.id == me) {
                e.active = false;
                if (s_->sink)
                    s_->sink->saveEmployee(e);
            }
        }
        emit notice(tr("Done. Log in with your own PIN."));
        emit s_->staffChanged();
    }
    return true;
}

// --- Find My Printers (upstream 30e6949, adapted) ---
// Manager -> Printers looks for printers on this computer's own network; one that answers like a receipt
// printer can print a page with its address, then be made the Receipts, Kitchen or Bar printer.
// (Fork) It never changes a printer that isn't on the network (the Rongta on USB stays as it is), never
// sends anything to the store's label printers (the Zebras: ZPL, TSPL), and asks before taking a
// printer's place from another address.

namespace {

bool labelFormat(const PrinterConfig &p)
{
    const std::string f = p.effectiveFormat();
    return f == "zpl" || f == "tspl";
}

QString kindName(const QString &kind)
{
    return kind == u"receipt" ? PosService::tr("Receipts") : kind == u"bar" ? PosService::tr("Bar")
                                                                             : PosService::tr("Kitchen");
}

} // namespace

QVariantMap PosService::printerSearch() const
{
    if (printerSearch_.isEmpty() || !can(QString::fromLatin1(perm::Manager)))
        return {};
    QVariantMap out = printerSearch_;
    // Which of the store's printers each found one is (as the settings are now).
    QVariantList rows;
    for (const QVariant &v : printerSearch_.value(u"found"_s).toList()) {
        QVariantMap f = v.toMap();
        QStringList names;
        bool label = false;
        for (const PrinterConfig &p : s_->settings.printers)
            if (p.type == "network" && qs(p.host) == f.value(u"host"_s).toString() && p.port == f.value(u"port"_s).toInt()) {
                names << qs(p.name);
                label = label || labelFormat(p);
            }
        if (!names.isEmpty())
            f.insert(u"printer"_s, names.join(u", "_s));
        f.insert(u"label"_s, label);
        rows.append(f);
    }
    if (out.contains(u"found"_s))
        out.insert(u"found"_s, rows);
    // What the Receipts, Kitchen and Bar printers are now.
    QVariantMap places;
    for (const QString kind : {u"receipt"_s, u"kitchen"_s, u"bar"_s})
        if (const PrinterConfig *p = s_->settings.printer(ss(kind))) {
            const QString where = p->type == "network" ? (p->port == 9100 ? qs(p->host) : u"%1:%2"_s.arg(qs(p->host)).arg(p->port))
                                : !p->path.empty() ? qs(p->path) : qs(p->type);
            places.insert(kind, QVariantMap{{u"name"_s, qs(p->name)}, {u"where"_s, where},
                                           {u"network"_s, p->type == "network" && !labelFormat(*p)}});
        }
    out.insert(u"slots"_s, places);
    return out;
}

bool PosService::findPrinters()
{
    if (!require(perm::Manager, tr("Setting up printers")))
        return false;
    if (!s_->searchPrinters)
        return fail(tr("Printers are looked for on the computer that keeps the data."));
    if (printerSearch_.value(u"busy"_s).toBool())
        return true;
    // The label printers are only knocked on, never sent a byte.
    QStringList noProbe;
    for (const PrinterConfig &p : s_->settings.printers)
        if (p.type == "network" && p.effectiveFormat() != "escpos")
            noProbe << qs(p.host);
    printerSearch_ = {{u"busy"_s, true}};
    emit sessionChanged();
    // A second or two of trying every address: away from the screen.
    QPointer<PosService> self(this);
    QThreadPool::globalInstance()->start([self, noProbe, search = s_->searchPrinters] {
        const QVariantList found = search(noProbe);
        QMetaObject::invokeMethod(self, [self, found] {
            if (!self)
                return;
            self->printerSearch_ = {{u"done"_s, true}, {u"found"_s, found}};
            emit self->sessionChanged();
            emit self->notice(found.isEmpty() ? tr("No printers found on this network.")
                                              : tr("%n printer(s) found", nullptr, int(found.size())));
        });
    });
    return true;
}

bool PosService::printWhoAmI(const QString &host)
{
    if (!require(perm::Manager, tr("Setting up printers")))
        return false;
    if (!s_->printWhoAmI)
        return fail(tr("Printers are looked for on the computer that keeps the data."));
    // Only one that was found and answered like a receipt printer (not any address someone types,
    // and never a label printer: it would print a label of nonsense).
    QVariantMap row;
    for (const QVariant &v : printerSearch().value(u"found"_s).toList())
        if (v.toMap().value(u"host"_s).toString() == host)
            row = v.toMap();
    const int port = row.value(u"port"_s).toInt();
    if (port <= 0)
        return fail(tr("Look for printers first."));
    if (row.value(u"label"_s).toBool() || !row.value(u"answered"_s).toBool())
        return fail(tr("%1 didn't answer like a receipt printer: nothing was sent to it.").arg(host));
    QPointer<PosService> self(this);
    const QString title = qs(s_->settings.storeName).trimmed().isEmpty() ? u"ViewTouch"_s : qs(s_->settings.storeName);
    const QString line = tr("This printer is %1").arg(host);
    QThreadPool::globalInstance()->start([self, host, port, title, line, print = s_->printWhoAmI] {
        const QString error = print(host, port, title, line);
        QMetaObject::invokeMethod(self, [self, host, error] {
            if (self)
                emit self->notice(error.isEmpty() ? tr("Look for the page that says %1.").arg(host)
                                                  : tr("%1 didn't take the page: %2").arg(host, error));
        });
    });
    return true;
}

bool PosService::useFoundPrinter(const QString &kind, const QString &host, bool replace)
{
    if (!require(perm::Manager, tr("Setting up printers")))
        return false;
    if (kind != u"receipt" && kind != u"kitchen" && kind != u"bar")
        return false;
    QVariantMap row;
    for (const QVariant &v : printerSearch().value(u"found"_s).toList())
        if (v.toMap().value(u"host"_s).toString() == host)
            row = v.toMap();
    const int port = row.value(u"port"_s).toInt();
    if (port <= 0)
        return fail(tr("Look for printers first."));
    if (row.value(u"label"_s).toBool() || !row.value(u"answered"_s).toBool())
        return fail(tr("%1 didn't answer like a receipt printer: add it by hand with + Add.").arg(host));
    int index = -1;
    for (int i = 0; i < int(s_->settings.printers.size()); ++i)
        if (qs(s_->settings.printers[i].id) == kind)
            index = i;
    if (index >= 0) {
        const PrinterConfig &was = s_->settings.printers[index];
        // A printer that isn't on the network (USB, a CUPS queue, a file) or a label printer is left as it is.
        if (was.type != "network" || labelFormat(was))
            return fail(tr("Your %1 printer isn't a network printer; it's left as it is. To change it, touch it in this list.")
                            .arg(qs(was.name)));
        if (qs(was.host) == host && was.port == port) {
            emit notice(tr("%1 is your %2 printer already.").arg(host, qs(was.name)));
            return true;
        }
        if (!replace)
            return fail(tr("Your %1 printer is at %2 now.").arg(qs(was.name), qs(was.host)));
    }
    QVariantMap record = index >= 0 ? adminRecords(u"printers"_s).value(index).toMap() : adminNewRecord(u"printers"_s);
    if (index < 0) {
        record.insert(u"id"_s, kind);
        record.insert(u"name"_s, kindName(kind));
        record.insert(u"receipts"_s, kind == u"receipt");
        record.insert(u"format"_s, QString());   // ESC/POS: it answered as one
    }
    record.insert(u"type"_s, u"network"_s);
    record.insert(u"host"_s, host);
    record.insert(u"port"_s, port);
    if (!adminSave(u"printers"_s, index, record))
        return false;
    emit sessionChanged();
    emit notice(tr("%1 is your %2 printer now.").arg(host, record.value(u"name"_s).toString()));
    return true;
}

} // namespace vt::app
