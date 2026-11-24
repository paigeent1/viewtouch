#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT_H 1
#define _GLIBCXX_RANGES_H 1
#endif
#pragma once

#include "core/check.hh"
#include "core/id_card.hh"
#include "core/customer.hh"
#include "core/day.hh"
#include "core/employee.hh"
#include "core/inventory.hh"
#include "core/menu.hh"
#include "core/report.hh"
#include "core/scan_data.hh"
#include "core/settings.hh"
#include "core/waitlist.hh"
#include "app/pos_session.hh"

#include <QByteArray>
#include <QJsonObject>
#include <QSet>
#include <QTimer>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <set>
#include <unordered_set>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace vt::core::legacy {
struct Receipt;   // core/legacy_sales.hh
}

namespace vt::app {

// "11.50", "$11.50", "11,50" (with `symbol`, the currency's): a price in
// cents; -1 if it isn't one.
qint64 priceCents(QString text, const QString &symbol);

// Where the service hands off changes. Implementations must not block: the
// SQLite one queues writes to a worker thread.
class PosSink {
public:
    virtual ~PosSink() = default;
    virtual void saveCheck(const core::Check &check) = 0;
    virtual void savePunch(const core::TimePunch &punch) = 0;
    // `reports` holds the day's final reports (set when the day is closed).
    virtual void saveDay(const core::BusinessDay &, const QJsonObject &reports) { Q_UNUSED(reports) }
    virtual void saveDrawer(const core::DrawerSession &) {}
    virtual void saveSettings(const core::PosSettings &) {}
    virtual void saveMenuItem(const core::MenuItem &, int position) { Q_UNUSED(position) }
    virtual void deleteMenuItem(const std::string &id) { Q_UNUSED(id) }
    virtual void saveEmployee(const core::Employee &) {}
    virtual void saveCustomer(const core::CustomerRecord &) {}
    virtual void saveGiftCard(const core::GiftCard &) {}
    virtual void saveParty(const core::Party &) {}
    virtual void removeCustomer(const std::string &) {}
    // Saved, and no old copies left in the journal (privacy).
    virtual void purge() {}
    virtual void saveIngredient(const core::Ingredient &, int position) { Q_UNUSED(position) }
    virtual void deleteIngredient(const std::string &id) { Q_UNUSED(id) }
    virtual void saveShift(const core::Shift &) {}
    virtual void deletePunch(std::int64_t id) { Q_UNUSED(id) }
    virtual void deleteShift(std::int64_t id) { Q_UNUSED(id) }
    virtual void saveDelivery(const core::Delivery &) {}
    virtual void saveRefund(const core::Refund &) {}
    virtual void saveChange(const core::Change &) {}
    virtual void saveStockCount(const core::StockCount &) {}
    virtual void saveWaste(const core::Waste &) {}
    virtual void saveProblem(const core::Problem &) {}
    // The store's pictures (logo, buttons, backgrounds): name -> file bytes.
    virtual void saveImage(const std::string &name, const QByteArray &data) { Q_UNUSED(name) Q_UNUSED(data) }
    virtual void deleteImage(const std::string &name) { Q_UNUSED(name) }
};

// Where tickets go. Implementations must not block (see print::PrintSpooler).
class PosPrinter {
public:
    virtual ~PosPrinter() = default;
    // `voids`: the lines were cancelled after being sent.
    virtual void printKitchen(const core::PosSettings &settings, const core::Check &check,
                              const std::vector<core::OrderLine> &lines, bool voids) = 0;
    // `printerId`: the terminal's receipt printer (see PosSettings::receiptPrinterFor).
    virtual void printReceipt(const core::PosSettings &settings, const core::Check &check,
                              const std::string &printerId) = 0;
    virtual void printReport(const core::PosSettings &settings, const core::Report &report,
                             const std::string &printerId) = 0;
    virtual void openDrawer(const core::PosSettings &settings, const std::string &printerId) = 0;
    // No Sale: the drawer opened, with a slip saying so (no logo, like a kitchen ticket):
    // "****No Sale****", its number for the day, who, why, when. Kept as a ticket in the print log.
    virtual void printNoSale(const core::PosSettings &settings, const core::CashMovement &noSale, int number,
                             const std::string &drawer, const std::string &printerId)
    {
        (void)noSale; (void)number; (void)drawer;
        // Only the terminal's own printer's drawer, when it has one (as TicketPrinter does).
        if (const core::PrinterConfig *p = settings.printer(printerId); p && p->drawerKick)
            openDrawer(settings, printerId);
    }
    // A test page on printer `printerId` (its logo, text sizes, a cut), and
    // the drawer wired to it opened when `kickDrawer`. False: no such printer.
    virtual bool printTestPage(const core::PosSettings &, const std::string &, bool) { return false; }
    // Prep labels (title, then lines) on printer `printerId`: a label printer
    // in its own language, or a receipt printer as slips.
    virtual void printLabels(const core::PosSettings &, const std::vector<std::pair<std::string, std::vector<std::string>>> &,
                             const std::string &) {}
    // A self-order kiosk's slip for the guest: the order number to show at
    // the counter and what they ordered; `sent`: already being made.
    // The day's exception tickets: the store's copy of a check with voids, error corrects or an
    // employee's discount, a cancelled sale, each No Sale slip (pictures in the End of Day PDF).
    struct ExceptionTicket {
        std::string file;    // a name for it: "check-123", "nosale-3"
        std::string title;   // what made it an exception: "Check #123: void, discount"
        std::optional<core::Check> check;
        std::optional<core::CashMovement> noSale;
        int number = 0;      // No Sale #
        std::string drawer;
    };
    // End of Day: the day's summary as a PDF (`summary`: the reports in order, then the exception
    // tickets as pictures). Empty: none can be made here. PosService keeps it in the End of Day archive.
    virtual QByteArray endOfDayPdf(const core::PosSettings &, const std::vector<core::Report> &,
                                   const std::vector<ExceptionTicket> &, std::int64_t dayId, std::int64_t dayOpenedAt)
    {
        (void)dayId; (void)dayOpenedAt;
        return {};
    }
    // A PDF on a full-page printer (a printer whose format is "pdf": a CUPS queue).
    virtual void printPdf(const core::PosSettings &, const QByteArray &pdf, const QString &title, const std::string &printerId)
    {
        (void)pdf; (void)title; (void)printerId;
    }
    // Pictures (JPEG/PNG pages) on the receipt printer, one under the other.
    virtual void printPictures(const core::PosSettings &, const std::vector<QByteArray> &pages, const QString &title,
                               const std::string &printerId)
    {
        (void)pages; (void)title; (void)printerId;
    }
    // A copy of a receipt as text (on screen: Look Up Checks), or the gift receipt's.
    virtual QString receiptText(const core::PosSettings &, const core::Check &, bool gift) { (void)gift; return {}; }
    virtual void printGiftReceipt(const core::PosSettings &, const core::Check &, const std::string &) {}
    // An ILSA receipt from the imported history (rows: core::legacy::receiptRows), under the store's
    // receipt header; without a ticket printer, as a report. Its text for the screen (empty: none).
    virtual void printLegacyReceipt(const core::PosSettings &settings, const core::Report &asReport,
                                    const std::vector<std::pair<std::string, std::string>> &rows, const std::string &printerId)
    {
        (void)rows;
        printReport(settings, asReport, printerId);
    }
    virtual QString legacyReceiptText(const core::PosSettings &, const std::vector<std::pair<std::string, std::string>> &) { return {}; }
    // The full privacy notice, for a customer who asks (Customers screen).
    virtual void printPrivacyNotice(const core::PosSettings &, const std::string &) {}
    // The full privacy notice as a text file on the server (Desktop/Privacy Notice.txt): where it went.
    virtual QString savePrivacyNotice(const core::PosSettings &) { return {}; }
    virtual void printOrderSlip(const core::PosSettings &settings, const core::Check &check,
                                const std::string &printerId, bool sent)
    {
        (void)sent;
        printReceipt(settings, check, printerId);
    }
};

// A closed business day and its final reports (report id -> report JSON).
struct PastDay {
    core::BusinessDay day;
    QJsonObject reports;
};

// The built-in user a self-order kiosk takes orders as (role "kiosk": it
// may only order). Not one of the staff: it can't log in with a PIN.
const core::Employee &kioskEmployee();

// Everything the running POS needs at startup.
struct PosData {
    core::PosSettings settings;
    std::vector<core::MenuItem> menu;
    std::vector<core::Employee> employees;
    std::vector<core::Check> openChecks;
    std::vector<core::TimePunch> punches;        // today's, plus any still open
    std::vector<core::TimePunch> earlierPunches; // finished, from the days before (a week or so)
    std::vector<core::CustomerRecord> customers;
    std::vector<core::GiftCard> giftCards;
    std::vector<core::Party> parties;
    std::int64_t lastPartyId = 0;
    std::vector<core::Ingredient> ingredients;
    std::vector<core::Shift> shifts;
    std::int64_t lastShiftId = 0;
    std::vector<core::Delivery> deliveries;       // the last 60 days
    std::int64_t lastDeliveryId = 0;
    std::map<std::string, QByteArray> images;     // the store's pictures, by name
    std::int64_t lastCheckId = 0;
    std::int64_t lastPunchId = 0;
    std::optional<core::BusinessDay> currentDay;  // none: the service opens one
    std::int64_t lastDayId = 0;
    std::vector<core::Check> closedToday;
    std::vector<core::DrawerSession> drawers;     // today's, plus any still open
    std::int64_t lastDrawerId = 0;
    std::vector<PastDay> pastDays;                // newest first
    std::vector<core::Refund> refundsToday;       // this business day's refunds (any check's)
    std::vector<core::Check> cancelledToday;      // this business day's cancelled sales (Cancel #n)
    std::int64_t lastRefundId = 0;
    std::vector<core::Change> changesToday;       // this business day's changes (the Changes report)
    std::int64_t lastChangeId = 0;
    std::vector<core::StockCount> stockCounts;    // the latest ones, oldest first
    std::int64_t lastStockCountId = 0;
    std::vector<core::Waste> waste;               // the last 60 days, oldest first
    std::int64_t lastWasteId = 0;
    std::vector<core::Problem> problems;          // the last 7 days, oldest first
    std::int64_t lastProblemId = 0;
};

// State shared by every terminal of one store: open checks, today's closed
// checks, menu, staff, settings, the drawer and business day, and which
// terminal holds which check. Sessions change it and it tells all of them.
class PosShared : public QObject {
    Q_OBJECT

public:
    PosShared(PosData data, PosSink *sink, QObject *parent = nullptr);

    std::int64_t now() const { return now_(); }
    void setClock(std::function<std::int64_t()> now) { now_ = std::move(now); }
    // Which store data this is (caches kept outside it tell one from another, even at the same address).
    const std::uint64_t serial = [] { static std::uint64_t n = 0; return ++n; }();

    // While the server makes every screen's update at once (nothing changes
    // meanwhile), what's the same for all of them (the kitchen's tickets, the
    // menu) is made once: `memo` keeps it until `endMemo`.
    void beginMemo() { memo_.emplace(); tipSharesMemo.reset(); }
    void endMemo() { memo_.reset(); tipSharesMemo.reset(); }
    bool memoOn() const { return memo_.has_value(); }
    // Closed checks with nothing left for the kitchen, or for the expediter:
    // skipped when making those screens (a busy night has a thousand). Done
    // stays done, except a recalled ticket, or a check reopened and closed
    // again (both forget it here).
    mutable std::unordered_set<qint64> kitchenDone, expoDone;
    void kitchenAgain(qint64 checkId) { kitchenDone.erase(checkId); expoDone.erase(checkId); }
    // Everyone's tip share, made once a round too (each server's screen shows theirs).
    mutable std::optional<std::map<std::string, core::TipShare>> tipSharesMemo;
    QVariant memo(const QString &key, const std::function<QVariant()> &make) const
    {
        if (!memo_)
            return make();
        if (const auto it = memo_->constFind(key); it != memo_->cend())
            return *it;
        return *memo_->insert(key, make());
    }

    const core::Employee *employee(const std::string &id) const;

    core::PosSettings settings;
    std::vector<core::MenuItem> menu;
    // The Menu Builder's changes, to undo (last first): the menu, its
    // categories and choice groups before, and after (undone only if the
    // menu is still as that change left it).
    struct MenuState {
        std::vector<core::MenuItem> menu;
        std::vector<core::MenuCategory> categories;
        std::vector<core::ModifierGroup> groups;
        bool operator==(const MenuState &) const = default;
    };
    struct MenuUndo {
        QString label;
        MenuState before, after;
    };
    std::vector<MenuUndo> menuUndo;
    int menuUndoDepth = 0;   // inside a change (an import's many saves): one step
    std::vector<core::Employee> employees;
    std::map<std::int64_t, core::Check> open;
    std::vector<core::TimePunch> punches;   // today's, plus any still open
    std::vector<core::TimePunch> earlierPunches;   // finished, from the last days (weekly overtime)
    std::vector<core::CustomerRecord> customers;
    std::vector<core::GiftCard> giftCards;
    std::vector<core::Party> parties;   // waiting, booked, and today's
    std::int64_t lastPartyId = 0;
    std::vector<core::Ingredient> ingredients;
    core::Ingredient *ingredient(const std::string &id);
    // Closed checks in [from, to) from the database (set up by main; may run
    // on a worker thread). Unset: reports over a range use today's only.
    std::function<std::vector<core::Check>(std::int64_t from, std::int64_t to)> history;
    // Every check and party in the database that may be a customer's (forgetting them).
    std::function<std::pair<std::vector<core::Check>, std::vector<core::Party>>(const std::string &id,
                                                                                 const std::string &digits)> customerTrail;
    // Items ordered together over the past four weeks (from history, loaded
    // once in the background): checks with each item, and with each pair.
    std::map<std::string, int> pastChecksWith;
    std::map<std::string, std::map<std::string, int>> pastTogether;
    bool pastTogetherAsked = false;
    // ...and today's, counted as checks close (not again for every screen).
    std::map<std::string, int> todayChecksWith;
    std::map<std::string, std::map<std::string, int>> todayTogether;
    size_t todayCounted = 0;
    std::int64_t todayLastId = 0;
    // Older time punches from the store (Time Punches reaches a month back).
    std::function<std::vector<core::TimePunch>(std::int64_t from, std::int64_t to)> punchHistory;
    // Drawers opened in [from, to) (the Exceptions and Loss Watch reports' no sales).
    std::function<std::vector<core::DrawerSession>(std::int64_t from, std::int64_t to)> drawerHistory;
    std::vector<core::TimePunch> olderPunches;   // loaded from it for editing
    std::vector<core::Shift> shifts;   // from two weeks back on
    std::int64_t lastShiftId = 0;
    std::vector<core::Delivery> deliveries;
    std::int64_t lastDeliveryId = 0;
    // The store's pictures (Manager -> Pictures, or any picture field): kept in
    // the database, so backups, the standby and every screen have them.
    std::map<std::string, QByteArray> images;
    // The check each person had open when they logged out (switching to the
    // next person): open again when they log back in.
    std::map<std::string, std::int64_t> resumeChecks;
    // Where pictures are written out for this computer's screens to load
    // (main sets it under the app's data; else a temporary folder).
    QString imageCacheDir;
    // A picture as a file here: its name, or a path ("logo:" is the store's logo).
    QString imageFile(const QString &ref);
    // Texts a guest (set up by main when a texting service is configured).
    std::function<void(const QString &phone, const QString &message)> sendText;
    // Stripe, on the store's computer (net/stripe_api.cpp, set by main): a
    // card reader's connection token, and refunds. Each answers once,
    // with an error text when it didn't work.
    std::function<void(std::function<void(const QString &token, const QString &error)>)> stripeConnectionToken;
    // Emailed receipts waiting for Stripe (a card taken offline, the internet
    // down): one terminal's service sends them (PosService::sendWaitingReceipts).
    QObject *receiptSender = nullptr;
    std::map<std::int64_t, std::int64_t> receiptTried;   // check id -> when last tried (ms)
    std::function<void(const QString &paymentId, std::int64_t cents,
                       std::function<void(const QString &refundId, const QString &error)>)> stripeRefund;
    // Any Stripe call (countertop readers, pairing): "GET" / "POST", a path
    // ("/v1/terminal/readers/tmr_1"), a form ("a=1&b[c]=2").
    std::function<void(const QString &method, const QString &path, const QString &form,
                       std::function<void(const QJsonObject &reply, const QString &error)>)> stripeCall;
    core::CustomerRecord *customer(const std::string &id);
    core::GiftCard *giftCard(const std::string &number);
    std::int64_t lastCheckId = 0;
    std::int64_t lastPunchId = 0;
    PosSink *sink = nullptr;
    PosPrinter *printer = nullptr;

    core::BusinessDay day;
    std::int64_t lastDayId = 0;
    std::vector<core::Check> closedToday;
    std::vector<core::DrawerSession> drawers;   // drawers and server banks; today's plus open ones
    std::int64_t lastDrawerId = 0;
    std::vector<PastDay> pastDays;
    std::vector<core::Refund> refundsToday;
    std::vector<core::Check> cancelledToday;   // Cancel Sale: Cancel #1, #2... (not sales)
    std::int64_t lastRefundId = 0;
    std::vector<core::Change> changesToday;
    std::int64_t lastChangeId = 0;
    std::vector<core::StockCount> stockCounts;   // the latest ones, oldest first
    std::int64_t lastStockCountId = 0;
    std::vector<core::Waste> waste;              // the last 60 days, oldest first
    std::int64_t lastWasteId = 0;
    // What went wrong lately (Store Check), and how many of them the
    // managers have seen (problem ids up to settings.problemsSeen).
    std::vector<core::Problem> problems;
    std::int64_t lastProblemId = 0;
    // Kept, unless the same thing was noted in the last 10 minutes.
    void noteProblem(const std::string &kind, const std::string &subject, const std::string &detail = {});
    // Guessing PINs: wrong ones in a row on each screen, and the store's
    // lately. After 5 in a row a screen waits (30 seconds, doubling to 15
    // minutes); 30 in the store within 10 minutes and every screen waits a minute.
    struct PinGuard {
        int wrong = 0;
        std::int64_t waitUntil = 0;
    };
    QHash<QString, PinGuard> pinGuards;
    std::vector<std::int64_t> wrongPinsLately;
    // A newer ViewTouch on the release page ("0.9.1360"; empty: none known).
    QString newVersion;
    std::int64_t storeWaitUntil = 0;
    // Refunds sent to Stripe and not answered yet ("check/payment"), from any terminal.
    QSet<QString> refundsUnderway;
    // How much of a payment has gone back: on the check, and today's from any terminal.
    vt::Money refundedSoFar(const core::Check &check, std::int64_t paymentId) const;
    // A refund done: on the check (today's here, or `found`: one from an
    // earlier day, as it was read), saved, in today's refunds; every
    // terminal is told (refundRecorded). Doesn't need the terminal that
    // started it to still be there.
    void recordRefund(core::Refund refund, const core::Check &found, const QString &what);
    // Closed checks back to the first one (Find a Check): the newest first,
    // in [from, to) (to = 0: up to now), those whose saved record contains
    // any of `words` (none: all), `limit` from `offset`. Set by main (storage).
    struct CheckFind {
        std::int64_t from = 0, to = 0;
        QStringList words;
        int limit = 50, offset = 0;
    };
    std::function<std::vector<core::Check>(const CheckFind &)> findChecks;
    // Refunds made in [from, to), whatever day their check was (range reports).
    std::function<std::vector<core::Refund>(std::int64_t from, std::int64_t to)> refundHistory;
    // ILSA's sales history file (ilsa-history.db beside the database; set by main): read-only, for
    // the ILSA History reports and Receipt Lookup (pos_legacy.cpp). Empty: none.
    QString legacyDb;
    // The End of Day archive (app/eod_archive.hh): the database its records are in and its folder,
    // <data>/eod-archive (set by main). Empty: End of Day keeps no archive (tests that don't ask for one).
    QString archiveDb;
    QString archiveDir;
    // The archive's last integrity check, and the last End of Day's news about the archive
    // ({at, file, moved, warning, error}): for the archive screen.
    QVariantMap archiveCheck;
    QVariantMap archiveNews;
    bool archiveChecking = false;
    // Where the archive screen's pages are drawn for screens to fetch (default: a cache folder).
    QString archivePagesDir;
    // Cancel Sale's checks (status discarded, a Cancel #) closed in [from, to), from the database
    // (set by main): the Exceptions report over days and the Exceptions search.
    std::function<std::vector<core::Check>(std::int64_t from, std::int64_t to)> cancelledHistory;
    // Responsible service: alcohol_policy_audit.jsonl in the store's data folder (set by main). Empty
    // (tests, a new folder): the entries are kept in memory only (serviceAuditMemory).
    QString serviceAuditFile;
    std::vector<QJsonObject> serviceAuditMemory;
    // Mass Updates (pos_mass_update.cpp): its folder, <data>/mass-updates (set by main: the change sets applied
    // here and when each item was first had, for New items). Empty (tests): kept in memory only.
    QString massUpdateDir;
    struct MassShared;
    std::shared_ptr<MassShared> mass;

    // The terminal's open drawer / its most recent one today.
    core::DrawerSession *openDrawerFor(const std::string &terminal);
    const core::DrawerSession *latestDrawerFor(const std::string &terminal) const;
    // An employee's open server bank / their most recent one today.
    core::DrawerSession *openBankFor(const std::string &employeeId);
    const core::DrawerSession *latestBankFor(const std::string &employeeId) const;
    int adminRevision = 0;

    // Check locks: a check open on one terminal cannot be opened on another.
    std::map<std::int64_t, const QObject *> lockedBy;
    // Bumped kitchen tickets, newest last, for Recall.
    struct Bump { std::int64_t checkId; std::int64_t sentAt; std::string station; };
    std::vector<Bump> bumped;
    std::vector<Bump> served;   // expediter bumps, newest last
    // Messages between screens ("86 salmon", "need a runner"), newest last.
    struct Message { std::int64_t id; std::int64_t at; std::string from; std::string to; std::string text; };
    std::vector<Message> messages;
    std::int64_t lastMessageId = 0;

    // A device pairing a manager has started: the code the device must
    // type, until it is used or expires. One at a time.
    // standby: this code may pair the store's standby server (which gets a
    // full copy of the data); otherwise only screens.
    struct Pairing { QString code; std::int64_t expires = 0; bool standby = false; };
    std::optional<Pairing> pairing;
    const Pairing *activePairing() const
    {
        return pairing && pairing->expires > now() ? &*pairing : nullptr;
    }
    // Save the settings and tell every terminal (paired devices changed...).
    void saveSettings();
    // The kitchen's learned prep times: saved quietly, at most once a minute
    // (no screen needs telling; the tickets show them anyway). Any other
    // save of the settings saves them too.
    void savePrepTimes();
    std::int64_t prepSavedAt = 0;
    // Draft pours (each keg's ounces rung up): saved quietly, at most once a minute, and a minute after the
    // last one at the latest (pos_bar.cpp). Changing a keg saves at once.
    void saveDraftPours();
    std::int64_t draftSavedAt = 0;
    bool draftSaveQueued = false;
    // Link pours to bottles: what the last linking changed, to undo it (items' recipes before, goods it added).
    struct PourLinkUndo {
        std::vector<std::pair<std::string, std::vector<core::RecipeLine>>> recipes;
        std::vector<std::string> goodsAdded;
        int links = 0;
    };
    std::optional<PourLinkUndo> pourLinkUndo;
    // Put draft beers on empty lines: what it did, to undo it (lines filled, keg goods made, whether it
    // linked pours first).
    struct DraftFillUndo {
        std::vector<std::pair<std::string, std::string>> lines;   // line -> keg good put on it
        std::vector<std::string> kegsAdded;
        bool linked = false;
    };
    std::optional<DraftFillUndo> draftFillUndo;
    // Items sold over the past four weeks (from history, in the background; again after an hour).
    std::map<std::string, int> pastSold;
    std::int64_t pastSoldAt = 0;
    bool pastSoldAsked = false;
    // Open a pairing (replacing any open one) and return its code.
    QString startPairing(bool standby = false);

    void startDay();

    // Power (Store Settings > Billboards: Power the TV off outside its hours): what this computer
    // last did to each TV - put it to sleep, or woke it and opened the billboard again (then it
    // should connect within 60 seconds: tried once more, then said on its row).
    struct BillboardPower {
        bool asleep = false;        // put to sleep here (and not woken since)
        bool waking = false;        // woken; waiting for the billboard to connect
        std::int64_t wokeAt = 0;
        int tries = 0;
        bool noWake = false;        // woken twice, never came back
        std::int64_t sleptAt = 0;
        std::int64_t sleepFailedAt = 0;   // tried and failed: again 10 minutes later
    };
    std::map<std::string, BillboardPower> billboardPower;
    // Powering the TVs is this computer's job (main: the store's server); off elsewhere.
    bool billboardPowerOn = false;

    // Store Settings > Billboards: the TVs' deploy / update / restart, run here on the store's
    // computer in the background (vt-add-billboard), one at a time for each billboard: what it
    // said (its plain lines, newest last), how far it got, how it ended.
    struct BillboardJob {
        QString action;            // "deploy" | "restart"
        bool running = false;
        bool ok = false;
        int step = 0, steps = 0;   // "[2/6] ..." lines from the script
        QStringList lines;
        std::int64_t at = 0;
        bool refused = false;      // the TV answered but wouldn't let adb in (ADB debugging off)
        bool unreachable = false;  // no answer at all (off, another network, the VPN down)
    };
    std::map<std::string, BillboardJob> billboardJobs;
    // Identify: the billboard shows its name until then (ms).
    std::map<std::string, std::int64_t> billboardIdentify;
    // vt-add-billboard (main: /usr/local/bin, or $VT_ADD_BILLBOARD; tests: a stand-in), and the
    // list it keeps of the TVs it set up ($VT_CONF_DIR/billboards: "name|address" lines).
    QString billboardScript;
    QString billboardsFile;
    // Tobacco scan data for Altria (pos_scan_data.cpp): this store's settings, kept in their own file on
    // this computer ($VT_CONF_DIR/scan-data-site.json), never in the database, so they are never copied
    // to another store's computer or a standby. Empty file name (tests): in memory only.
    QString scanDataFile;
    core::scandata::Settings scanData;

    // Backups (set up by main on the store server): take one now, and how
    // the last one went - {at, ok, error, copy, copyOk}.
    std::function<bool()> requestBackup;
    // A backup key for a password: {key, salt}, both empty when this
    // computer can't encrypt (the app sets it; see storage/sealed.hh).
    std::function<std::pair<QByteArray, QByteArray>(const QString &password)> backupKeyFor;
    // Factory reset from the Manager page: main backs up, closes, deletes the
    // database and starts again. False: not possible here.
    std::function<bool()> requestFactoryReset;
    // The store's computers and printers, for Manager -> Network (set by
    // the app): {role, term, terminals, standby, printers}.
    std::function<QVariantMap()> network;
    // Find My Printers (set by the app: print/printer_finder.hh), on any thread: the printers on the
    // store's network [{host, port, answered}] (noProbe: addresses only connected to, sent nothing: the
    // label printers); and a page on one saying its address ("" when it went, else why not).
    std::function<QVariantList(const QStringList &noProbe)> searchPrinters;
    std::function<QString(const QString &host, int port, const QString &title, const QString &line)> printWhoAmI;
    // Find a Tablet (pos_find.cpp). The paired screens connected now, kept by the store's server
    // (PosServer): terminal id -> how many connections it has open, since when, and when it was
    // last heard (ms). Only in memory: "last seen" starts over when ViewTouch restarts.
    struct ScreenLink {
        int links = 0;
        std::int64_t since = 0;
        std::int64_t lastSeen = 0;
    };
    std::map<std::string, ScreenLink> screenLinks;
    // A message to one paired screen over its connection (set by PosServer); false: it isn't
    // connected (or this computer serves no screens).
    std::function<bool(const std::string &terminalId, const QJsonObject &message)> sendToScreen;
    // The screens beeping now: terminal id -> the screen that asked, and until when (ms).
    struct Finding {
        QString from;
        std::int64_t until = 0;
    };
    std::map<std::string, Finding> finding;
    // The menu's categories: those set up, in order, then any family an item
    // names that isn't (older menus), named after it.
    std::vector<core::MenuCategory> categories() const;
    // What each printer says is wrong (print::PrinterMonitor): printer id ->
    // "paperOut", "coverOpen", "paperLow", "error", "offline"; none: fine.
    std::map<std::string, std::string> printerProblems;
    void setPrinterProblem(const std::string &printerId, const std::string &problem);
    // A printer's tickets kept after it failed ("notPrinting"): send them
    // again (set by the app: print::PrintSpooler::retryHeld).
    std::function<void(const std::string &printerId)> reprintHeld;
    // Screens whose cash drawer didn't open (their printer has no drawer, or
    // isn't set up): terminal name -> why. Their own printer alert says so
    // until it's fixed (Terminals or Printers) or their drawer opens.
    std::map<std::string, std::string> drawerMisses;
    QVariantMap backup;
    void setBackupStatus(QVariantMap status)
    {
        backup = std::move(status);
        emit dayChanged();
    }

signals:
    void checksChanged();    // any check: open, closed, made
    void refundRecorded(const core::Refund &refund, const QString &what);
    void dayChanged();
    void drawerChanged();
    void adminChanged();     // menu, settings, tenders, printers, taxes
    void staffChanged();
    void customersChanged();   // customers, gift cards, house accounts
    void networkChanged();     // a screen or the standby came or went; a printer worked or failed

private:
    std::function<std::int64_t()> now_;

private:
    mutable std::optional<QHash<QString, QVariant>> memo_;
};

// One terminal's POS session: who is logged in, the check being worked on,
// the keypad entry, and every order/payment operation, over the shared
// store. Pages reach it through zone actions and widgets (PosSession API);
// C++ callers and tests may use the typed methods directly.
class PosService : public PosSession {
    Q_OBJECT

public:
    // Single-terminal convenience: owns its own shared state.
    PosService(PosData data, PosSink *sink, QObject *parent = nullptr);
    // One of several terminals on a shared store.
    PosService(PosShared *shared, QString terminalName, QObject *parent = nullptr);
    ~PosService() override;

    PosShared *shared() const { return s_; }
    void setPrinter(PosPrinter *printer) { s_->printer = printer; }
    // Test hook: replace the wall clock (epoch ms) for the shared store.
    void setClock(std::function<std::int64_t()> now) { s_->setClock(std::move(now)); }

    void invoke(const QString &method, const QVariantList &args = {}, Reply reply = {}) override;
    // Every operation invoke() takes by name (for the tests that try them all).
    static QStringList commandNames();

    const core::PosSettings &settings() const { return s_->settings; }
    const core::Check *currentCheck() const;
    const core::Employee *user() const;
    QString nameOf(const core::Employee &e) const;
    const core::MenuItem *findItem(const QString &idOrName) const;
    // Barcodes typed in a field ("012345678905, 4006381333931"), and why
    // they can't be item `itemId`'s (empty: they can).
    static QStringList barcodeList(const QString &text);
    QString barcodeProblem(const QStringList &codes, const std::string &itemId) const;
    // A scanned code is this stored barcode: the same, or the same UPC-A read
    // another way (13 digits with a 0 in front: a scanner reading it as an
    // EAN-13; 11 digits: the check digit left off).
    static bool sameBarcode(const std::string &stored, const std::string &scanned);
    // An item's place in the label tree below its category: brand, then product group, sub group (when it
    // has them). The Labels screen's tree and Menu Builder's tree of a price book department.
    static QStringList labelPathBelowCategory(const core::MenuItem &m);
    QString format(Money amount) const;

    // --- session -----------------------------------------------------------
    void pinKey(const QString &key);   // "0".."9", "clear", "back"
    bool login();                      // with the PIN entered
    bool loginWithPin(const QString &pin);
    void logout();
    bool clockIn();                    // logged-in user, else PIN entered
    // Someone with more than one job is asked which one first (clockInJobs),
    // then clocks in as it.
    bool clockInAs(const QString &role);
    // What the next pay out is for (Store Settings: expense categories);
    // touching the chosen one again clears it.
    void setExpenseCategory(const QString &category);
    // A delivery from a vendor (managers): {vendor, invoice, lines: [{ingredient,
    // qty, cost}]}. Adds to stock. `cost` is the invoice's: kept on the line beside the system cost
    // (costDifferences), never made the system cost.
    bool receiveDelivery(const QVariantMap &delivery);
    // Find checks, open or closed, from the last `days`: "#123", "17.62",
    // or text (table, customer, phone, server, item, gift card). Results
    // arrive in checkSearch (loading until then).
    // Closed checks back to the first: `query` (empty: all of them) in the
    // last `days` (0: ever), a page from `offset` (checkSearch: nextOffset).
    bool searchChecks(const QString &query, int days = 365, int offset = 0);
    // A receipt's barcode (Store Settings > Print a barcode on receipts) scanned at the register:
    // that check found (any day) and shown on Find a check, for a return or a reprint; from another
    // store (its prefix isn't ours): says so. A receipt's reference typed in searchChecks does the same.
    bool openReceiptCode(const QString &code);
    void selectFoundCheck(qint64 id);
    // A copy of a found check's receipt, a gift receipt, or a copy with the buyer's date of birth
    // (from the ID check; Store Settings: a manager's, if dobReprintNeedsManager).
    bool reprintCheck(qint64 id, bool gift = false, bool withDob = false);
    QVariantMap checkSearch() const override;
    // --- the Exceptions search (pos_exceptions.cpp; managers) ----------------------------------
    // From the database, never files: the days from `from` to `to` (yyyy-MM-dd; empty: today), and of
    // those the ones matching `employee` (a name), `types` (exceptionTypes(); none: all), `minAmount`
    // (dollars, "5" or "5.00") and `text` (a check number, with or without the store's prefix, or words of
    // the item or the reason). Read away from the screen; the answer lands in exceptionSearch().
    bool searchExceptions(const QVariantMap &filter);
    bool moreExceptions();   // the next 200
    QVariantMap exceptionSearch() const override;
    // Everything the last search found, as a report (Print, PDF).
    core::Report exceptionSearchReport() const;
    QVariantMap exceptionSearchVariant();   // managers: the same, for the screen's PDF
    // --- Mass Updates (pos_mass_update.cpp; Menu Builder > Mass Updates, managers) -----------------
    // The retail items as a grid, narrowed, sorted and paged here (a page of 200 is sent): `view` {scope
    // (retail), find, category, path [brand, product group, sub group], noCategory, newDays, field, from,
    // to (a range), chosenOnly, sort, desc, page}. Opening the screen starts it; massClose stops it.
    bool massView(const QVariantMap &view);
    bool massClose();
    bool massChoose(const QStringList &ids, bool on);   // ticked or not
    bool massChooseAll();                               // every item the grid is narrowed to (every page)
    bool massChooseNone();
    bool massChooseOnly(const QStringList &ids);        // these (Menu Builder's Select), shown alone
    // What an action would do to the chosen items, old -> new, changing nothing: {kind: margin | price | cost
    // | copy | field, how: add | percent | set, amount, round: none | 5up | 9up | 9near | 5or9up, from (copy:
    // a UPC, item number or id), field + value (field), why}. Cost needs the price book permission.
    bool massPreview(const QVariantMap &action);
    bool massPreviewPage(int page);
    bool massCancel();
    // The preview applied as one change set (the price book permission: an admin, or an admin's PIN);
    // massUndo puts the last one back.
    bool massApply();
    bool massUndo();
    // A change set file from another store (or this one): matched by UPC, shown as a preview.
    bool massImport(const QVariantMap &file);
    // A change set applied here, as its file ({format: viewtouch-change-set, ...}); empty: none such.
    QVariantMap massChangeSetFile(const QString &id);
    QVariantMap massUpdate() const override;
    bool printExceptionSearch();
    QVariantMap printerSearch() const override;
    bool findPrinters();
    bool printWhoAmI(const QString &host);
    bool useFoundPrinter(const QString &kind, const QString &host, bool replace);
    QVariantMap receiving() const override;   // also costDifferences (below)
    // Lines received at an invoice cost other than the system cost (newest first, at most 200): when,
    // vendor, invoice, item, systemCost, invoiceCost, difference. Receiving never changes the system cost.
    QVariantList costDifferences() const;
    QVariantMap stockCounting() const override;
    // A stock count: each item counted becomes what's on hand, and the count
    // (against what should be there) is kept for the Stock Count report.
    bool saveStockCount(const QVariantMap &count);
    core::Report stockCountReport(const core::ReportContext &ctx) const;
    void stockExtras(core::Report &r, const core::ReportContext &ctx) const;
    QVariantMap wasteLog() const override;
    // Store Check: {checks: [{id, ok, info, title, detail}], problems: [{when, kind, text, new}], unseen, ready}.
    QVariantMap storeCheck() const override;
    bool problemsSeen();   // the problems listed now have been seen
    static QString printerProblemText(const QString &name, const std::string &what);
    QString problemText(const core::Problem &p) const;
    // Thrown away: a menu item (its recipe) or an ingredient, how much, why.
    bool logWaste(const QVariantMap &waste);
    // Prep labels: {ingredients: [{id, name, hours}], printer (its name; empty: none)}.
    QVariantMap prepLabels() const override;
    // {ingredient | name, hours (use by; 0: none), count, note}.
    bool printPrepLabels(const QVariantMap &labels);
    QVariantMap retailLabels() const override;
    // Items for the Labels screen: by name or any of their UPCs.
    bool findRetailLabels(const QString &query);
    // Find on the order screens: every word typed somewhere in the name (any order, as Item Look Up finds
    // them), or an item number / UPC; the answer in itemSearch(): the best `limit` (50; More… asks for more,
    // up to 1000).
    bool findItems(const QString &text, int limit = 50);
    QVariantMap itemSearch() const override;
    // The tree: [group, category, brand] (fewer: that level's children).
    bool browseRetailLabels(const QStringList &path);
    // Every item under the path (Show Items), up to 300; the levels below are still listed.
    bool showRetailLabelItems(const QStringList &path);
    // Hands the job to the label utility on this (the main) computer, which prints it.
    bool printRetailLabels(const QVariantMap &job);
    // A picture of one label, drawn by the label utility exactly as it prints ({template, upc}); it
    // arrives in retailLabels().preview a few seconds later.
    bool previewRetailLabel(const QVariantMap &job);
    bool closeRetailLabelPreview();
    // Quick Label (as the label utility's Quick Label window; anyone logged in): 1-3 typed lines on a label
    // roll (a label template's size and printer), drawn and printed by the label utility on this computer.
    // State in quickLabel(): {ready, rolls: [{key, name, size, printer}], roll (last used), loading,
    // preview (data: PNG), previewFor (the lines and roll it shows), previewBusy, previewError, printing,
    // status, statusOk, maxLines, maxChars, maxCopies}.
    QVariantMap quickLabel() const override;
    bool quickLabelRolls();                                                          // asks for the rolls
    bool quickLabelPreview(const QStringList &lines, const QString &roll);
    bool printQuickLabel(const QStringList &lines, const QString &roll, int copies);
    bool closeQuickLabel();                                                          // forgets the preview and status
    static constexpr int kQuickLabelLines = 3, kQuickLabelChars = 40, kQuickLabelCopies = 50;
    // TV buttons (the pages' "directv" command): this (the main) computer runs
    // /usr/local/bin/vt-directv <box> <op> <val> (VT_DIRECTV_LAUNCHER stands in for it: tests) in the
    // background, killed after 10 s, and its one-line answer comes back as a notice on this terminal's
    // screen only, so a tablet's TV buttons work as the Pi's own. Anyone logged in.
    // The button's own TV (optional): ip a store network IPv4 (10/8, 172.16/12, 192.168/16), kind
    // directv, roku or samsung; given to vt-directv as --ip <ip> --kind <kind> before the box.
    bool directv(const QString &box, const QString &op, const QString &val, const QString &ip = {},
                 const QString &kind = {});
    // Empty when ip and kind are right for the box (both may be empty), else what's wrong (translated).
    static QString directvTvProblem(const QString &box, const QString &ip, const QString &kind);
    static bool directvPrivateIp(const QString &ip);
    // vt-directv's own rules for its arguments: box and val [A-Za-z0-9_]{1,24}, op tune or key.
    static bool directvArgsOk(const QString &box, const QString &op, const QString &val);
    static QString directvProgram();
    static constexpr int kDirectvTimeoutMs = 10'000;
    std::string labelPrinterId() const;
    const std::vector<core::Waste> &wasteEntries() const { return s_->waste; }
    const std::vector<core::StockCount> &stockCounts() const { return s_->stockCounts; }
    void cancelClockIn();
    QVariantMap clockInJobs() const override;
    QVariantMap cashTipsAsk() const override;
    QVariantMap reasonAsk() const override { return reasonAsk_; }
    QVariantMap priceCheck() const override { return priceCheck_; }
    // PRICE CHECK: on (a banner flashes), or off again; while on, a scanned code shows its item.
    bool togglePriceCheck();
    // The item for a code (barcode or number), with its box or its singles ("parent"/"child"):
    // shown in a window and on the customer display. False: no such item.
    bool priceCheckCode(const QString &code);
    void closePriceCheck();
    // ITEM RETURN (pos_returns.cpp): a small window asks "the old sale, or scan the item?". Scanned (or a
    // UPC / part of the name typed), it shows the item at its price-book price, which the cashier may
    // change, how many and why; Add puts a RETURN line on the sale (negative, taxed as the item, a
    // manager's approval, an exception). The sale is then paid as usual: less than nothing is a cash refund.
    QVariantMap itemReturn() const override { return itemReturn_; }
    bool startItemReturn();
    bool itemReturnStep(const QString &step);   // "choose" | "scan"
    bool itemReturnCode(const QString &code);   // a scan, a UPC or part of a name typed
    bool itemReturnPick(const QString &itemId); // one of the names found
    bool addItemReturn(const QString &itemId, qint64 priceCents, int quantity, const QString &reason, const QString &upc);
    void cancelItemReturn();
    // The reasons ITEM RETURN offers (in the screen's language).
    static QStringList itemReturnReasons();
    // Answers reasonAsk: the void or comp goes ahead with it ("": it doesn't).
    bool giveReason(const QString &reason);
    // The cash tips kept on the shift just clocked out ("12.50"; "" or "0": none).
    bool declareCashTips(const QString &amount);
    QVariantMap timeClock() const override;
    bool timeClockStart(const QString &pin);
    bool timeClockAct(const QString &action);   // "in", "out", "break"
    // How long an item should take the kitchen (minutes): set, else learned; 0: unknown.
    int prepMinutesFor(const std::string &itemId) const;
    // Time off and shift swaps (pos_requests.cpp).
    bool timeClockRequestOff(const QString &date, const QString &reason);
    bool timeClockGiveAway(qint64 shiftId);
    bool timeClockTake(qint64 requestId);
    bool timeClockCancelRequest(qint64 requestId);
    void timeClockDone();
    bool clockOut();

    // --- keypads -------------------------------------------------------------
    void entryKey(const QString &key);   // digits, "00", "clear", "back"
    void adjustGuests(int delta);
    void textKey(const QString &key);    // characters, "space", "back", "clear"
    void clearEntry();

    // --- checks ---------------------------------------------------------------
    TableResult selectTable(const QString &label);
    bool startCheck(core::CheckType type);   // dine-in uses the pending table + guest entry
    bool openCheck(std::int64_t checkId);
    bool newTableCheck();
    // A bar tab under this name (empty: the name typed on the keyboard).
    bool openTab(const QString &name = {});
    void releaseCheck();

    bool addItem(const QString &idOrName);
    // --- ID checks for age-restricted items (pos_age.cpp) ---
    // The item waiting for an ID: {item, name, age, handNeedsManager}; {} when none.
    QVariantMap ageAsk() const override { return ageAsk_; }
    // The barcode on the back of a license (AAMVA): old enough and not expired -> the item goes in.
    bool verifyAgeScan(const QString &raw);
    // An ID that won't scan: its date of birth typed (MMDDYYYY); logged as the exception.
    bool verifyAgeHand(const QString &mmddyyyy);
    void cancelAgeCheck();
    // Site Setup > ID checks: {tobacco, alcohol, ageTobacco, ageAlcohol, tobaccoCategories, handNeedsManager}.
    QVariantMap idChecks() const override;
    bool saveIdChecks(const QVariantMap &values);
    // Menu Builder -> Price check (pos_menu_builder.cpp): the tax rates and the cash discount; its
    // cash discount % saved from there (managers).
    QVariantMap pricing() const override;
    bool saveCashDiscount(double percent);
    int requiredAge(const core::MenuItem &item) const;

    // --- Responsible service (pos_over_service.cpp) ---------------------------------------------
    // Standard drinks for each guest (a seat, or the customer linked to it or to the check), against the
    // store's caution and stop levels (Store Settings -> Responsible service). A drink that brings a
    // guest to a level waits: {level: caution | stop, guest, drinks, before, adding, item, check,
    // windowHours, caution, stop, reasons}; {} when none. Staff screens only (never the guest's).
    QVariantMap serviceAsk() const override { return serviceAsk_; }
    // Caution: the bartender has seen it (one tap); the drink goes in.
    bool acknowledgeService();
    // Stop: a manager's PIN and a reason (picked or typed); the drink goes in.
    bool overrideService(const QString &pin, const QString &reason);
    // Not rung in after all (caution or stop).
    bool cancelService();
    // A bottle or a bucket shared across seats: its standard drinks split evenly between them
    // (its own seat only: an empty list or one seat).
    bool shareDrink(qint64 lineId, const QVariantList &seats);
    // The guests on the open check and their counts now: [{key, label, drinks ("4.2"), value, level}].
    QVariantList drinkCounts() const;

    // --- The bar menu (pos_bar_menu.cpp) ---------------------------------------------------------
    // After a spirit is rung (Store Settings -> Bar menu: its categories, else ABV at least the minimum), a
    // mixer from the Mixers modifier group, added to the drink as a choice (it prints and charges); none in
    // the group: never asked. {active, lineId, item, mixers: [{index, name, price}], notes: [{name, chosen}]}.
    QVariantMap mixerAsk() const override;
    // A pour rung as addItem rings it, its notes (names from the Bar notes group: Neat, Rocks...) added to
    // it at no charge once it is on the check (after an ID check or a responsible-service ask too).
    bool orderPour(const QString &itemId, const QVariantList &notes);
    // The mixer asked for: its index in the Mixers group, or -1 for none; the notes chosen then replace the
    // drink's notes. Ends the ask.
    bool answerMixer(int index, const QVariantList &notes);
    // A drink that asks for a mixer when rung (the store's rule above).
    bool mixerWanted(const core::MenuItem &item) const;
    // A drink (ABV or pour given, taxed as alcohol) or something the bar makes (prints to the bar).
    static bool barItem(const core::MenuItem &item);
    // The pours of one bottle, for the bar's grids: item id -> {key (shared by the bottle's pours; only
    // where it has two or more), bottle (its name), pour (the size: "1.25 oz", "Glass")}.
    struct PourOf {
        QString key, bottle, pour;
    };
    std::map<std::string, PourOf> pourGroups() const;
    // Given once to a store (new or not): an empty Mixers group and a Bar notes group, names only and no
    // prices (the owner prices them in Manager -> Modifiers). True when it changed the settings.
    static bool seedBarGroups(core::PosSettings &settings);
    // A drink's name split from its size ("Tito's 1.25oz": Tito's, oz, 1.25; "... Glass": glass); pos_bar.cpp.
    struct DrinkSize {
        QString base, size;
        double value = 0;
    };
    static DrinkSize drinkSizeOf(const QString &name);
    // The audit file's entries in [from, to) (epoch ms), oldest first; and the Responsible Service report.
    std::vector<QJsonObject> serviceAuditEntries(std::int64_t from, std::int64_t to) const;
    core::Report serviceReport(std::int64_t from, std::int64_t to, const QString &label) const;
    void setQualifier(const QString &qualifier);   // same one again clears it
    void selectLine(qint64 lineId) override;
    void selectPayment(qint64 paymentId) override;
    bool voidItem();
    // Retail screen VOID ITEM: the line the cashier touched (never the newest by itself) comes off the sale
    // but stays on the check, voided and marked as an error correct, for the cashier to see; the Exceptions
    // report counts it. A line already sent is voided as usual (voidItem). On a struck-through line it is
    // RESTORE ITEM (restoreLine).
    bool errorCorrect();
    // RESTORE ITEM: the touched error-corrected line back as it was rung (price, modifiers, how many),
    // while the check is open and unpaid; "Restored <item>" in its history and on the Exceptions report.
    bool restoreLine();
    // Retail open-department key (MISC MDSE, MISC FOOD/BEV): the amount typed on the keypad (cents) rings
    // as one line in that department (a category of that name; made, with its item, the first time),
    // taxed in taxClass ("merchandise", "food"...); a class whose rate is 0 in Store Settings > Taxes
    // rings at the Merchandise rate instead.
    bool ringOpenDepartment(const QString &department, const QString &taxClass);
    // Where retail items (cigars, merchandise) go when ordered on a bar or table check, for a runner to
    // bring them out: the humidor's printer and/or station screen (id or name "humidor"); empty when the
    // store has neither (then they go nowhere, not to the kitchen).
    std::string humidorPrinter() const;
    std::string humidorStation() const;
    // The barcode about to ring in itemId (the scanner): the line keeps that code (receipts print it).
    // retail: rung on the Retail screen, so the customer has it (OrderLine::handed: no ticket, no screen).
    bool noteScan(const QString &code, const QString &itemId, bool retail = false);
    // How many of an unsent line (1-99); 0: the selected line. A sent line
    // can't change: more of it is a new line (repeatLine).
    bool setLineQuantity(qint64 lineId, int quantity);
    bool changeLineQuantity(qint64 lineId, int by);
    // One more the same way (its choices too), as a new unsent line.
    bool repeatLine(qint64 lineId);
    // Puts back the item last taken off (or made fewer), for a little while.
    bool undoLast();
    // The drinks sent last (one Send), ordered again as new lines.
    bool anotherRound();
    std::vector<const core::OrderLine *> lastRound(const core::Check &c) const;
    // Speed row (bar): REPEAT ROUND rings again the drinks of this check's most recent Send (what
    // lastRound() finds), each with its choices: "Repeated 4 drinks". SAME AGAIN rings the newest drink
    // on the check again (sent or not), with its choices. A drink: made at the bar, in a drinks
    // family, or with an ABV (standard drinks). Sold-out items are skipped / refused.
    bool repeatRound();
    bool sameAgain();
    bool isDrinkLine(const core::OrderLine &l) const;
    bool ringRoundAgain(const QString &op);
    // The Draft page: a pour of `itemId` from draft line `lineId` (T1-L1). Refused when nothing is on
    // the line, its keg is kicked (all of it rung up), or the item doesn't pour the line's beer; then
    // rung exactly as addItem (keg depletion, ABV / standard drinks, age check).
    bool orderDraft(const QString &lineId, const QString &itemId);
    // The menu items that pour `beer` (its __draft__ recipe line): {id, oz}, smallest first.
    QVariantList draftItemsOf(const std::string &beer) const;
    // Put draft beers on empty lines (managers): Link pours first when a draft item isn't linked yet; a
    // keg good (1/2 barrel, no cost) for each draft beer that has none; each draft beer not on a line
    // onto the next empty line, T1-L1 onward, as a Change keg by whoever is signed in. Beers already on
    // a line stay. Plan: [{line, beer, keg, newKeg}]. Undo takes them off again (nothing poured since).
    QVariantList fillDraftLinesPlan() const;
    bool fillDraftLines();
    bool undoFillDraftLines();
    bool sendOrder();
    // Orders for later: ready at `at` (epoch ms; 0 = as soon as possible).
    bool setDueAt(qint64 at);
    // Sends the orders for later whose time has come (the server calls it
    // every half minute); returns how many went.
    int fireDueOrders();
    // Kitchen stations: the store's, and the one this screen shows.
    QVariantList kitchenStations() const override;
    QString kitchenStation() const override;
    bool setKitchenStation(const QString &station);
    QString stationName(const std::string &id) const;
    // Event tickets: sold so far (all days), and left (-1: not an event).
    int ticketsSold(const core::MenuItem &item) const;
    int ticketsLeft(const core::MenuItem &item) const;
    bool knownStation(const std::string &id) const;
    std::string stationOf(const core::OrderLine &l) const;
    // Made where the expediter doesn't look (Store Settings -> Kitchen -> Expo doesn't show).
    bool expoSkips(const core::OrderLine &l) const;
    bool isPart(const core::OrderLine &l, const core::Modifier &m) const;
    bool allMade(const core::OrderLine &l) const;
    bool atStation(const core::OrderLine &l, const std::string &station) const;
    bool partAt(const core::OrderLine &l, const core::Modifier &m, const std::string &station) const;
    bool addComment();
    // Amount: explicit cents, else the keypad entry, else the balance due.
    bool tender(const QString &tenderId, std::optional<std::int64_t> amountCents = std::nullopt);
    bool removePayment();
    // A discount of the amount typed: dollars (cents typed) or a percent.
    bool customDiscount(bool percent);
    // A percent off (`percentBp`: 500 = 5%, 10000 = comp; 0: the percent typed): on the
    // selected (else the last rung) item when `item`, else on the whole sale. Items marked
    // No discounts (cigarettes at the state minimum) are never discounted either way.
    bool discountPercent(qint64 percentBp, bool item);
    // Cancel Sale: the sale in progress is cancelled (asks why), kept as Cancel #n for the day:
    // not a sale, nothing taken out of stock (put back if it was), an exception. Payments must be
    // taken back first.
    bool cancelSale();
    bool closeCheck();
    // The current check, else the last one closed on this terminal.
    bool printReceipt();
    bool noSale();   // open the cash drawer without a sale
    bool setCustomer(const QVariantMap &customer);

    // --- the menu while ordering (app/pos_menu.cpp) -------------------------------------
    // An item with modifier groups opens choosing: choose options, then
    // finish (required groups must be satisfied) or cancel (the item comes off).
    bool chooseOption(const QString &groupId, int index);
    // The same, as "no", "lite", "extra" or "side" (held down on the Choose page).
    bool chooseOptionAs(const QString &groupId, int index, const QString &qualifier);
    // Exactly how a choice is had: "off" (as it comes / not added), "" (an
    // add-on, plain), "no" (what comes on it), "lite", "extra", "side".
    bool setChoice(const QString &groupId, int index, const QString &how);
    bool finishChoosing();
    bool cancelChoosing();
    // Choose again for an unsent item already on the check.
    bool chooseLine(qint64 lineId);
    // "House Salad needs a Dressing..." for the first of `lines` missing a
    // required choice; empty when they are complete.
    QString missingChoice(const std::vector<core::OrderLine> &lines) const;
    // 86 / un-86 an item (anyone taking orders).
    bool setAvailable(const QString &itemId, bool available);
    std::string currentMealPeriod() const;

    // Start a break, or end the one going on (clocked-in staff).
    bool toggleBreak();

    // Rush ("rush") or VIP ("vip") on the current check, on or off.
    bool toggleFlag(const QString &flag);

    // --- seats and courses --------------------------------------------------------
    // The seat / course new items go to; also changes the selected line.
    bool setSeat(int seat);
    bool setCourse(int course);
    // Send the next course that is on hold.
    bool fireCourse();
    // Course pacing: fire the next held course in `minutes` (0: now; -1: never mind).
    bool fireCourseIn(int minutes);
    bool fireCourseOn(core::Check &c, bool paced);

    // --- managing checks (app/pos_checks.cpp) ----------------------------------------
    // On the current check. Servers may do this to their own checks,
    // managers to anyone's; each is kept in the check's history.
    bool transferCheck(const QString &employeeId);
    bool moveCheck(const QString &table);
    bool mergeCheck(qint64 otherId);   // the other open check comes onto this one
    bool reopenCheck(qint64 checkId);  // manager: a check closed today, made current

    // --- tips, gratuity, cash in and out ------------------------------------------
    // Tip on the selected card payment (else the last one): a percentage of
    // the check (bp, e.g. 1800) or, with percentBp 0, the keypad amount.
    bool addTip(std::int64_t percentBp);
    // The keypad amount as a cash tip for the check's server, handed to whoever
    // takes the payment (nothing typed: none).
    bool setCashTip();
    // An open check printed for the guests: noted once (the host stand's "nearly done").
    void notePrinted(std::int64_t checkId);
    // The customer display: ask the guest for a tip; their choice (a
    // percent, an amount in cents, or none) goes on the card payment, now
    // or when it is added.
    bool askForTip();
    bool customerTip(const QString &kind, std::int64_t value);   // "percent" | "amount" | "none"
    QVariantMap customerPrompt() const override;

    // --- loyalty and promotions (pos_loyalty.cpp) ---------------------------------
    // Spend reward `index` (PosSettings::rewards) of the check's customer.
    bool redeemReward(int index);
    // The guest types their phone on the customer display: found or signed up.
    // privacyAccepted: the guest accepted the privacy notice on the customer display (kept on their record).
    bool customerJoin(const QString &phone, bool privacyAccepted = false);
    // The full privacy notice on this terminal's receipt printer (a customer asked).
    bool printPrivacyNotice();
    // The guest's email (typed on the customer display), kept with their rewards.
    bool customerEmail(const QString &email, bool privacyAccepted = false);
    // After paying: "print", "text" (to a phone) or "none".
    bool sendReceipt(const QString &how, const QString &to = {});
    // A code a scanner read (it types it, then Enter), on whatever screen:
    // a coupon goes on the open check, a gift card is looked up ("giftCard";
    // its page shows it), else says nothing has it. Items: the screen rings
    // them in itself (findItem finds them by barcode).
    QString scanCode(const QString &code);
    // The item a scanned or typed code is: its number, or one of its barcodes (an 11-digit code also as
    // the UPC-A with its check digit); only its number when `numberOnly`. {id, name}, or empty. Read
    // straight from the menu (not the screens' item list), so a 30,000-item price book answers at once,
    // and a tablet asks the main computer instead of holding the whole list.
    QVariantMap itemForCode(const QString &code, bool numberOnly = false) const;
    // Names of the promotions running now (not coupons).
    QVariantList promotionsNow() const;
    // A coupon (a promotion with a code) typed or scanned onto the current
    // check: says why not when it's unknown, over, used up or covers nothing.
    bool applyCoupon(const QString &code);
    // --- a bar tab's card, held (pos_holds.cpp) ---------------------------------
    // The hold for the current check, for this screen's card reader (like
    // cardCharge, with hold: true): PosSettings::tabHold, set aside, not charged.
    QVariantMap holdCharge();
    // The reader's answer: the card held ({reference, processor, brand, last4, amountCents, checkId}).
    bool recordCardHold(const QVariantMap &r);
    // A reader beside the screen holds it (the store's computer runs it).
    bool startCounterHold();
    // Charges the held card for what's owed and a tip (a percent of the
    // check, else `tipCents`); more than is held: the bank is asked first.
    bool chargeHold(std::int64_t percentBp, std::int64_t tipCents);
    // Nothing charged: the card's hold is let go.
    bool releaseHold();
    QVariantMap holdInfo(const core::Check &c) const;
    // Takes coupon `id` (its promotion's) off the current check.
    bool removeCoupon(const QString &id);
    // The coupons on the current check: id, name, code, off, working.
    QVariantList checkCoupons() const;
    // Gratuity on the current check (bp; 0 removes). Removing an automatic
    // one needs a manager.
    bool setGratuity(std::int64_t percentBp);
    // Pay out of / into this terminal's drawer: keypad amount, typed reason.
    bool payout(core::CashMovement::Kind kind);
    // Pay the logged-in employee the tips they are owed, from this drawer.
    bool cashOutTips();

    // --- split check -------------------------------------------------------------
    // Move the selected line to another check (0 = a new one at the table).
    // Moves one of the chosen line's quantity to that check (0: a new one); all: the whole line.
    bool splitLine(qint64 targetCheckId, bool all = false);
    // The chosen item in even pieces on this check, to move to other checks.
    bool shareLine(int parts);
    // This check into `ways` checks of the same amount.
    bool splitEvenly(int ways);
    // The chosen shared item whole again, here (its pieces off the other checks).
    bool unshareLine();
    // A table's checks: one per seat (lines with no seat stay); every one's
    // receipt; all of them back into this one.
    bool splitBySeat();
    bool printTableChecks();
    // Manager -> Printers: a test page (and the drawer wired to it, opened).
    bool testPrinter(const QString &printerId, bool kickDrawer);
    // A cash drawer is wired to this printer (Printers -> Cash drawer on), saved: from Terminals.
    bool printerHasDrawer(const QString &printerId);
    bool combineTableChecks();
    QString checkFilter() const override { return checkFilter_; }
    void setCheckFilter(const QString &label) override;

    // --- kitchen display -----------------------------------------------------------
    // Mark the lines sent at `sentAt` on the check as made; with a station,
    // only the lines printed there (a bar screen leaves kitchen lines alone).
    bool bumpTicket(qint64 checkId, qint64 sentAt, const QString &station = {});
    // The expediter: every ticket across the stations until it's run out.
    QVariantList expoTickets() const override;
    bool expoBump(qint64 checkId, qint64 sentAt);   // out to the table (made, if a station forgot)
    bool expoRecall();
    bool recallTicket();   // undo the latest bump

    // --- drawer and business day ---------------------------------------------------
    // "My drawer" is this terminal's drawer, or with server banks the
    // logged-in employee's own bank.
    bool openDrawerSession();   // starting cash from the keypad entry
    bool countDrawer();         // counted cash from the keypad entry (bank: check out)
    // Manager: count someone else's drawer or bank (a server who left).
    bool countDrawerById(qint64 drawerId);
    // Counted bill by bill: how many of each of the store's bills and coins
    // (PosSettings::cashDenominations, in that order); drawerId 0: mine.
    bool countDrawerPieces(qint64 drawerId, const QVariantList &counts);
    // This person counts drawers blind (not seeing what should be there).
    bool blindCount() const;
    bool endOfDay();
    // Retired (exception tickets are kept in the End of Day archive's PDF and found with the
    // Exceptions search): says where to look. A page button may still call it.
    bool saveExceptionTickets();

    // --- the End of Day archive (pos_eod_archive.cpp; managers) --------------------------------
    // The archived days, newest first: [{seq, dayId, date, kind, size, sha, place: here | longTerm,
    // path, state (from the last check)}], the last integrity check {ok, problem, days, checked,
    // unreachable, at}, checking, longTerm {folder, days, reachable}, news (the last End of Day's).
    QVariantMap eodArchive();
    // The integrity check now (sync: tests, vt-eod-verify's twin); also kept for the screen.
    QVariantMap checkEodArchiveNow();
    // The same away from the screen; the result lands in eodArchive().check.
    bool checkEodArchive();
    // A page of a day's PDF as a JPEG, a part at a time (from byte `at`): {seq, page, pages, size, at,
    // data (base64), error}. Drawn on the store's computer, so tablets need no PDF viewer.
    QVariantMap eodArchivePagePart(qint64 seq, int page, qint64 at);
    // A day's PDF on a full-page printer (format "pdf"), else its pages on the receipt printer.
    bool printEodArchive(qint64 seq);
    // End of Day: today's PDF into the archive, then (away from the screen) old ones moved to the
    // long-term folder and the whole archive checked. The result is said on the screen.
    void archiveEndOfDay();
    // X report: today's sales and the drawers so far on the receipt printer; nothing closes or resets
    // (End of day is the Z). Anyone who may open the drawer.
    bool xReport();
    core::Report buildXReport() const;
    std::vector<PosPrinter::ExceptionTicket> exceptionTickets() const;
    const core::BusinessDay &currentDay() const { return s_->day; }
    const std::vector<core::Check> &closedToday() const { return s_->closedToday; }

    // --- reports -------------------------------------------------------------------
    // id: sales | items | servers | labor | drawer. dayId 0 = today (live).
    QVariantMap report(const QString &id, qint64 dayId = 0) override;
    bool printReport(const QString &id, qint64 dayId = 0);
    core::Report buildReport(const QString &id) const;
    // The reports saved at End of Day (every one Reports lists but the Forecast), in order; also what
    // `vtmodern --export-reports` writes for the web reporting page.
    static QStringList endOfDayReportIds();

    // --- admin (manager) -------------------------------------------------------------
    // panel: menu | employees | tenders | printers | taxes | store
    QVariantList adminFields(const QString &panel) override;
    QVariantList adminRecords(const QString &panel) override;
    QVariantMap adminNewRecord(const QString &panel) override;
    // index -1 adds a record. Returns false (with a notice) when invalid.
    bool adminSave(const QString &panel, int index, const QVariantMap &record);
    bool adminDelete(const QString &panel, int index);
    // Kept for the Changes report: who changed what, from what to what.
    void logChange(const QString &area, const QString &item, const QString &field, const QString &before,
                   const QString &after);
    const std::vector<core::Change> &changesToday() const { return s_->changesToday; }
    bool logPageEdits(const QStringList &added, const QStringList &removed, const QStringList &changed);
    const std::vector<core::MenuItem> &menu() const { return s_->menu; }
    const std::vector<core::Employee> &employees() const { return s_->employees; }

    // --- PosSession state ------------------------------------------------------------
    QString terminalName() const override { return terminal_; }
    bool loggedIn() const override { return user() != nullptr; }
    QString userName() const override;
    QString userRole() const override;
    QStringList permissions() const override;
    // What someone may do here: their job's permissions, their own changes, and
    // what the store lets their job do (hosts taking payments).
    std::set<std::string> permissionsOf(const core::Employee &e) const;
    bool clockedIn() const override;
    QString clockedInSince() const override;
    QString storeName() const override;
    QString storeZip() const override { return QString::fromStdString(s_->settings.storeZip); }
    bool topRibbonShowWeather() const override { return s_->settings.topRibbonShowWeather; }
    QString currencySymbol() const override;
    QStringList customColors() const override;
    QVariantList priceChanges() const override;
    int pinLength() const override { return int(pin_.size()); }
    QString entry() const override { return entry_; }
    QString entryAmount() const override;
    int entryGuests() const override;
    QString textEntry() const override { return text_; }
    QString pendingQualifier() const override;
    QString pendingTable() const override { return pendingTable_; }
    bool hasCheck() const override { return currentCheck() != nullptr; }
    QVariantMap checkInfo() const override;
    QVariantList tableChecks() const override;
    QString undoText() const override;
    QVariantList lines() const override;
    QVariantMap totals() const override;
    QVariantList payments() const override;
    qint64 selectedLine() const override { return selectedLine_; }
    qint64 selectedPayment() const override { return selectedPayment_; }
    QVariantList openChecks() const override;
    QVariantList openChecksShared() const override;
    QVariantMap openChecksViewer() const override;
    QVariantList openChecksSharedNow() const;
    QVariantList customerResultsNow() const;
    QVariantMap floor() const override;
    QVariantList deliveries() const override;
    QVariantList drivers() const override;
    QStringList popularItems() const override;
    // The store's best sellers over the past four weeks and today (item ids, most sold first, at most
    // 24): the speed row's top 12.
    QStringList topSellers() const override;
    // Often ordered with what's on this check (a hint on the order screen): {id, name}.
    QVariantList goesWith(const core::Check &c) const;
    // Arranging the self-filling menu by touch (managers): one place earlier
    // or later within its family, and its button color.
    bool moveMenuItem(const QString &id, int by);
    bool setMenuItemColor(const QString &id, const QString &color);
    bool addCustomColor(const QString &color);   // kept for the store's color pickers
    QVariantMap stockLeft() const override;
    QVariantMap dashboard() const override;
    QVariantList lossAlerts() const override;
    QVariantList lossAlertsNow() const;
    // The profit coach (app/pos_coach.cpp): {tips: [{id, kind, title, why,
    // worth, worthText, item, price}], days} or {waiting} or {busy}.
    QVariantMap coach() const override;
    QVariantMap coachFrom(const std::vector<core::Check> &closed) const;
    bool coachNotNow(const QString &id);
    core::Report coachReport(const core::ReportContext &ctx) const;
    bool alertsSeen();
    static bool watchesEvent(const char *kind);
    QVariantMap checklists() const override;
    bool tickChecklist(const QString &list, int index);
    core::Report checklistReport(const core::ReportContext &ctx) const;
    QVariantList closedChecks() const override;
    QVariantList staff() const override;
    QVariantList checkHistory() const override;
    QVariantMap choosingInfo() const override;
    // Items sold by weight: the one waiting for its weight (the Weigh page).
    QVariantMap weighingInfo() const override;
    // The setup guide (managers): what's set so far, and each step.
    QVariantMap setupInfo() const override;
    bool setupStore(const QString &name, const QString &receiptLines);
    bool setupLogo(const QString &ref, bool onReceipts);
    bool setupTaxes(double foodPercent, double alcoholPercent);
    bool setupAddItem(const QString &name, double price, const QString &family);
    bool setupAddEmployee(const QString &name, const QString &role, const QString &pin);
    bool setupRetireSamples();
    bool setupFinish(bool done = true);
    bool addWeighed();
    bool cancelWeighing();
    QString onBreakSince() const override;
    QStringList soldOut() const override;
    QVariantList menuItems() const override;
    QVariantList orderItems() const override;
    QVariantList builderItems() const override;
    QString builderRevision() const override;
    QVariantMap builderTree() const override;
    // `view` (newer screens): {path: the level open below a price book department, showItems, scope: the
    // Menu Builder mode}. None (an older screen): a big department's first items, its search as before.
    bool setBuilderView(const QString &categoryId, const QString &search, const QString &itemId, const QString &parentUpc,
                        const QVariantMap &view = {});
    // Menu Builder shows at most this many items of a big price book department at once (a level's
    // Show Items), and finds at most kBuilderFindCap with its search.
    static constexpr int kBuilderShowCap = 300;
    static constexpr int kBuilderFindCap = 200;
    QVariantList menuCategories() const override;
    QVariantList choiceGroups() const override;
    // The Menu Builder (pos_menu_builder.cpp).
    bool saveCategory(const QVariantMap &record);   // {id ("" new), name, color, periods, printer, station, taxClass}
    bool moveCategory(const QString &id, int by);
    bool moveCategoryTo(const QString &id, int position);   // dragged
    bool moveMenuItemTo(const QString &id, int position);   // among its category's items
    bool deleteCategory(const QString &id);          // only an empty one
    // {id ("" new), name, price, family, image, description, available, kioskHide,
    //  taxClass, printer, station, groups: [ids], onIt: [ingredients]}
    bool saveMenuItemCard(const QVariantMap &card);
    bool deleteMenuItemCard(const QString &id);
    bool setMenuPrices(const QVariantList &prices);   // [{id, price, ...}]: one Undo step
    // The same, at `at` (epoch ms, later than now); applied by itself then.
    bool schedulePrices(const QVariantList &prices, qint64 at, const QString &label);
    bool cancelPriceChange(qint64 id);
    // Those due: put on the menu (every so often, and at start).
    void applyDuePriceChanges();
    // Emailed receipts whose cards have all reached Stripe now: Stripe sends them.
    void sendWaitingReceipts();
    // Ask Stripe to email check `c`'s receipt (each card payment's); marks it sent when Stripe took it.
    void emailReceiptNow(std::int64_t checkId);
    bool setItemSizes(const QString &itemId, const QVariantList &sizes);   // [{name, price}]
    bool setCategoryColors(const QVariantMap &colors);   // {id: color}: one Undo step
    bool changeMenuItems(const QStringList &ids, const QVariantMap &changes);   // one Undo step
    bool removeMenuItems(const QStringList &ids);   // [{id, price}]: one Undo step
    bool duplicateMenuItem(const QString &id);   // "Fish Tacos 2", right after it
    // "Tacos: Carne Asada 3.50, Al Pastor 3.25" / one per line; returns how many were added.
    int addMenuItemsFromText(const QString &categoryId, const QString &text);
    // Rows read from a spreadsheet: [{name, price, category, description, onIt}].
    int importMenuRows(const QVariantList &rows, const QString &categoryId, bool updatePrices);
    // Starter menus (menu_templates.cpp): [{id, name, description, categories, items}].
    QVariantList menuTemplates() const override;
    // What would trip up service: [{text, item, group, category, serious}].
    QVariantList menuProblems() const override;
    QVariantMap menuExport();
    bool setAllergies(const QStringList &ids);
    bool cardForwarded(const QVariantMap &r);
    QVariantList cardAlerts() const override;
    bool seeCardAlert(const QString &ref);
    // The printer alert's Reprint: tickets a USB printer didn't take go again.
    bool reprintHeld(const QString &printerId);
    void setStripeOffline(bool on);
    std::vector<core::Check *> checksToday();
    void saveAnyCheck(core::Check &c);
    // A line's allergens the guest is allergic to.
    static std::vector<std::string> allergyHits(const core::Check &c, const core::OrderLine &l);
    QString menuUndoText() const override;
    bool undoMenuChange();
    QString putMenuPicture(const QString &fileName, const QByteArray &data);
    int importMenuFile(const QVariantMap &file);
    // The ILSA price book's automatic sync (the store's own job: no login needed): new items added, prices
    // and new barcodes of the items here updated, nothing else changed (names, sizes, categories kept);
    // what differs is reported. {ok, error, items, added, prices, barcodes, names, departments, taxes,
    // gone, conflicts, skipped, rows: [{kind (added | price | barcode | name | department | tax | gone |
    // conflict | skipped), id, item, upc, before, after}]}. One save, one new menu for every screen.
    QVariantMap syncPriceBook(const QVariantMap &file);
    bool applyMenuTemplate(const QString &id);
    // {id ("" new), name, min, max, askHow, options: [{name, price, included, kitchenName}]}
    bool saveChoiceGroup(const QVariantMap &record);
    bool deleteChoiceGroup(const QString &id);   // items stop asking for it
    QVariantList kitchenTickets() const override;
    // The same for every screen: made once per round of updates (PosShared::memo).
    QVariantList kitchenTicketsNow() const;
    QVariantList expoTicketsNow() const;
    QVariantMap dashboardNow() const;
    QVariantList menuItemsNow() const;
    QVariantList orderItemsNow() const;
    // The item list as last made, for this computer's own screens (every binding that shows it asks
    // again; with a 30,000-item price book making it each time froze the Pi): made again after any
    // change (the signals below), a new meal period or language.
    mutable std::optional<QVariantList> menuItemsCache_;
    mutable std::string menuItemsCachePeriod_;
    mutable QString menuItemsCacheLanguage_;
    mutable int menuItemsCacheRevision_ = -1;
    // Menu Builder's view (setBuilderView): what builderItems adds to the menu less the price book.
    QString builderCategory_, builderSearch_, builderItem_, builderParent_;
    QStringList builderPath_;            // the level open below a big department ([brand, product group...])
    QString builderScope_;               // the screen's mode (retail, hospitality, event): its search looks there
    bool builderShowItems_ = false;      // Show Items on that level
    bool builderTreeAware_ = false;      // the screen shows the tree (an older one: never told so)
    // Menu Builder's quick index (core/builder_index.hh), once per menu change, and what this screen's
    // view picks from it (pos_menu.cpp).
    struct BuilderCache;
    mutable std::shared_ptr<BuilderCache> builderCache_;
    const BuilderCache &builderCache() const;
    struct BuilderPick;
    mutable std::shared_ptr<BuilderPick> builderPick_;
    const BuilderPick &builderPick() const;
    bool builderViewChanging_ = false;   // its adminChanged leaves the item list's cache alone
    int adminTicks_ = 0;                 // menu changes seen here (sold out, availability...): in builderRevision
    mutable std::size_t menuItemsCacheSize_ = 0;
    QVariantList menuCategoriesNow() const;
    QVariantList choiceGroupsNow() const;
    QVariantList closedChecksNow() const;
    QVariantMap waitlistInfoNow() const;
    QVariantMap floorNow() const;
    QVariantMap stockLeftNow() const;
    QVariantMap dayInfoNow() const;
    QVariantList daysNow() const;
    QVariantMap checklistsNow() const;
    QStringList popularItemsNow() const;
    QVariantMap drawerInfo() const override;
    QVariantMap dayInfo() const override;
    QVariantList days() const override;
    int adminRevision() const override { return s_->adminRevision; }
    QString tipsOwed() const override;
    QVariantList mealPeriods() const override;
    QVariantMap pairingInfo() const override;
    QString screenMode() const override;
    QString terminalLook() const override;
    QString terminalKeyboard() const override;
    QString terminalCustomerDisplay() const override;
    QString terminalStartCategory() const override;

    // --- pairing devices (manager) -------------------------------------------------
    // Start a pairing: a 10-character code, good for 10 minutes and one device.
    bool startPairing(bool standby = false);
    // Back the database up now (managers).
    bool backupNow();

    // --- messages between screens -------------------------------------------------
    // to: "all", "kitchen" (kitchen, bar and expo screens), "floor" (every
    // other screen) or a person's name. Needs no login (kitchen screens).
    // until > 0: posted, shown to everyone it's for until then (epoch ms).
    bool sendMessage(const QString &to, const QString &text, qint64 until = 0);
    bool removeMessage(const QString &id);
    QVariantList messages() const override;
    // Shift notes: the ones this manager hasn't read (newest first), leaving one, reading them.
    QVariantList handoff() const override;
    bool leaveHandoffNote(const QString &text);
    bool readHandoffNotes();
    QVariantMap networkInfo() const override;
    // --- Find a Tablet (End of shift; any staff; pos_find.cpp) ---------------------------------
    // The store's tablets: [{id, name, connected, lastSeen (ms, 0: not since ViewTouch started),
    // lastSeenText, beeping, from}], by name. Paired screens only, never a billboard, a computer's
    // screen or this one.
    QVariantList tablets() const override;
    // That tablet beeps at alarm volume and shows "Here I am" for findSeconds, or until stopped
    // (here, or tapped there). False (and says so) when it isn't connected.
    bool findTablet(const QString &terminalId);
    bool stopFindingTablet(const QString &terminalId);
    // This screen was found ("Here I am" tapped): the asking screen stops showing it beeping.
    void foundTablet();
    static constexpr int findSeconds = 120;
    QVariantList printerAlerts() const override;
    QString language() const override;
    QVariantMap userPrefs() const override;
    QString storeLanguage() const override { return QString::fromStdString(s_->settings.language); }
    // Tonight's floor plan (PosSettings::floorPlan; empty: the usual one), and a manager choosing it.
    QString storeFloorPlan() const override { return QString::fromStdString(s_->settings.floorPlan); }
    bool setFloorPlan(const QString &pageId);

    // --- manager approval ---------------------------------------------------------
    // A manager's PIN lets the waiting operation (a void, a discount...) through once.
    bool approve(const QString &pin);
    bool cancelApproval();
    QVariantMap approvalInfo() const override { return approval_; }
    int autoLogoutMinutes() const override { return s_->settings.autoLogoutMinutes; }
    int phoneLockSeconds() const override { return s_->settings.phoneLockSeconds; }
    int screenSaverMinutes() const override { return s_->settings.screenSaverMinutes; }
    QString scanAddPermission() const override { return QString::fromStdString(s_->settings.scanAddPermission); }
    bool topRibbonEnabled() const override { return s_->settings.topRibbonEnabled; }
    bool topRibbonShowTime() const override { return s_->settings.topRibbonShowTime; }
    bool topRibbonShowBattery() const override { return s_->settings.topRibbonShowBattery; }
    bool topRibbonShowWifi() const override { return s_->settings.topRibbonShowWifi; }
    bool topRibbonShowCustomerCount() const override { return s_->settings.topRibbonShowCustomerCount; }

    // --- practice (training) --------------------------------------------------------
    // This screen's checks are practice: the person is in training, or a
    // manager switched it on here.
    bool training() const override;
    bool setTraining(bool on);

    // Self-order kiosk: this screen is for guests ordering on their own (no
    // staff logged in; the built-in "Self-order kiosk" user takes the
    // orders). A manager turns it on here (setSelfOrder), the terminal's
    // setting does at connect (enableSelfOrder), and a manager's PIN turns
    // it off (leaveSelfOrder).
    bool setSelfOrder(bool on);
    void enableSelfOrder();
    bool leaveSelfOrder(const QString &managerPin);
    bool selfOrder() const { return selfOrder_; }
    // A guest's order: start (for here / to go), add and remove items,
    // finish with the name to call it by (it waits for the counter, or goes
    // to the kitchen at once), or cancel.
    bool kioskStart(bool toGo);
    bool kioskAdd(const QString &itemId);
    bool kioskRemove(qint64 lineId);
    bool kioskFinish(const QVariantMap &guest);
    void kioskCancel();
    QVariantMap selfOrderInfo() const override;
    QVariantMap kioskMenu() const override;
    // A billboard screen (Manager -> Terminals: Billboard): the store's slides and logo, nothing
    // else (pos_kiosk.cpp). Its video slides are files in the server's guest-screen folder.
    QVariantMap billboard() const override;
    // A manager's PIN closes a billboard's app (nobody is ever logged in there).
    bool leaveBillboard(const QString &managerPin);
    // Whether a billboard with these hours (Monday first: "09:00-21:00", "closed", "" all day; none:
    // always) shows at `minute` (0-1439) of `weekday` (1 Monday .. 7 Sunday). Hours past midnight
    // ("18:00-02:00") run into the next day.
    static bool billboardOpenAt(const std::vector<std::string> &hours, int weekday, int minute);
    // "09:00-21:00", "closed" or "" as they're kept (a time typed as 9:00 is 09:00); nullopt if it isn't one.
    static std::optional<std::string> billboardDayHours(const QString &typed);
    // Hardware Configuration > Billboards (managers): the paired billboard screens,
    // [{name, enabled, mute, hours: [7], showing}], and a change to one of them
    // ({enabled, mute, hours: [7 strings]}). The billboards follow at once. (On every screen:
    // adminRecords("billboards"), PosSession::setBillboard.)
    QVariantList billboards() const;
    bool setBillboard(const QString &name, const QVariantMap &settings);
    // Store Settings > Billboards (managers; pos_billboards.cpp). "deploy" (install or update the
    // app, pair it the first time: vt-add-billboard in the background; args: pairPort, pairCode for
    // a Google TV's wireless debugging), "restart" (its app, over adb), "identify" (its name on
    // the TV for 10 seconds, over its own connection). Progress: the record's job.
    bool billboardAction(const QString &name, const QString &action, const QVariantMap &args = {});
    // Removed for good (unpaired: it can't connect again until deployed anew), with a manager's PIN.
    bool removeBillboard(const QString &name, const QString &managerPin);
    // When these hours next turn a billboard on or off after `now` (ms, the store's clock); 0: never
    // (always on, or always closed).
    static std::int64_t billboardNextChange(const std::vector<std::string> &hours, std::int64_t now);
    // Showing at `now`: switched on, and its on / off now if one is set (until it runs out), else its
    // hours (its own, or the store's billboard defaults).
    static bool billboardShowingAt(const core::TerminalConfig &t, const core::PosSettings &s, std::int64_t now);
    // What a billboard goes by: its own hours, volume, mute and slides, else the defaults / the guest screen's.
    static const std::vector<std::string> &billboardHoursOf(const core::TerminalConfig &t, const core::PosSettings &s);
    static int billboardVolumeOf(const core::TerminalConfig &t, const core::PosSettings &s);
    static bool billboardMuteOf(const core::TerminalConfig &t, const core::PosSettings &s);
    // Powered off outside its hours: its own setting, else the defaults' for a Fire TV (one not
    // known to be another make counts as one), off for any other TV.
    static bool billboardPowerOf(const core::TerminalConfig &t, const core::PosSettings &s);
    // The store's computer, every 30 seconds and at once after a change (main.cpp): each TV powered
    // to match - asleep outside its hours (or Off now), woken and its billboard opened at their
    // start (or On now), retried once if it doesn't connect within 60 seconds. In the background
    // (vt-add-billboard --sleep / --wake); each step logged.
    static void billboardPowerTick(PosShared *shared);
    static const std::vector<std::string> &billboardSlidesOf(const core::TerminalConfig &t, const core::PosSettings &s);
    // Apply to...: this billboard's messages, pictures and videos copied to these billboards (by
    // name; "@guest": the guest screen's list too). Managers.
    bool applyBillboardContent(const QString &name, const QStringList &targets);
    // The TVs vt-add-billboard set up before this page (its list: "name|address" lines, and a
    // .new left by one stopped half way): their addresses, kept on the billboards. True if any changed.
    static bool migrateBillboardsFile(core::PosSettings &settings, const QString &file);
    // A billboard's day as the page shows it: "9:00 AM-9:00 PM", "closed", "" all day.
    static QString billboardDayText(const std::string &kept);
    // A picture the store uses (a menu item's photo, the display logo or a
    // slide), base64, for screens on other computers; empty for anything else.
    QString storeImage(const QString &path) const;
    QString imageUrl(const QString &ref) const override;
    QVariantList storeImages() const override;
    QString storeLogo() const override { return QString::fromStdString(s_->settings.displayLogo); }
    bool addStoreImage(const QString &fileName, const QString &base64);
    bool removeStoreImage(const QString &ref);
    // Everything back to a fresh install, after a backup (managers; `confirm`
    // must be "RESET"). ViewTouch restarts with the starter set.
    bool factoryReset(const QString &confirm);

    // --- customers, gift cards, house accounts (pos_customers.cpp) ---------------
    // Search by phone digits or name ("" = the most recent).
    bool findCustomers(const QString &query);
    bool selectCustomer(const QString &id);
    // Put a customer (default: the selected one) on the open check.
    bool useCustomer(const QString &id = {});
    // Add or update (by id); goes on the open check. A new customer with the
    // same phone, email or full name as one on file (or an edit that takes
    // someone else's phone or email) isn't saved: customerDuplicate() lists
    // the matches and resolveCustomerDuplicate() says what to do. The record's
    // "onDuplicate": "new" saves a new customer anyway, "skip" saves without
    // checking, "match" (typed on a takeout) reuses the one with that phone.
    bool saveCustomer(const QVariantMap &record);
    QVariantMap customerDuplicate() const override;
    // choice: "use" (put that customer on the check, change nothing), "update"
    // (that customer gets the details typed), "new" (save a new customer
    // anyway) or "cancel".
    bool resolveCustomerDuplicate(const QString &choice, const QString &id = {});
    // A customer asked to be forgotten (privacy laws): their record goes, and
    // their name, phone, address, email and note come off every check and
    // waitlist entry (the sales stay, with no one's name on them).
    bool forgetCustomer(const QString &id);
    // Sell or reload a card on the check (a Quick check if none is open);
    // no number: a new one. Amount 0: the keypad. Live once the check is paid.
    bool sellGiftCard(const QString &number, qint64 amountCents = 0, const QString &purchasedBy = {}, const QString &issuedTo = {});
    bool lookupGiftCard(const QString &number);
    // Pay from a card (default: the looked-up one); 0 = keypad, else as much as it covers.
    bool payWithGiftCard(const QString &number = {}, qint64 amountCents = 0);
    // The looked-up card's small balance in cash, when the guest asks (state
    // law: California under $10); paid from the drawer (or the person's bank).
    bool cashOutGiftCard();
    // Managers: move a card's whole balance to another card (lost card, or combining two); the old one closes.
    bool transferGiftCard(const QString &fromNumber, const QString &toNumber);
    // A payment on the selected customer's account: "cash" | "card" (keypad amount, else all).
    bool payOnAccount(const QString &method, qint64 amountCents = 0);
    QVariantList customerResults() const override;
    QVariantMap customerInfo() const override;
    QVariantMap giftCardInfo() const override;

    // --- the host stand (pos_waitlist.cpp) ----------------------------------------
    // {name, phone, size, quote?, note, customerId?} -> the party's id (0: refused).
    qint64 addToWaitlist(const QVariantMap &party) { return addParty(party, false); }
    // As above with at: epoch ms, or "yyyy-MM-dd HH:mm".
    qint64 addReservation(const QVariantMap &party) { return addParty(party, true); }
    bool updateParty(qint64 id, const QVariantMap &changes);
    bool checkInParty(qint64 id);      // a reservation has arrived: into the line
    bool notifyParty(qint64 id);       // "your table is ready" (texted when set up)
    // Seat them: opens their table's check for `serverId` (default: you).
    bool seatParty(qint64 id, const QString &table, const QString &serverId = {});
    // The guest's receipt (pos_receipts.cpp): printed where chosen, after
    // paying by itself or offered, emailed by Stripe for a Stripe card.
    QVariantMap receiptOffer() const override { return receiptOffer_; }
    bool printReceiptOn(qint64 checkId, const QString &printerId);
    bool emailReceipt(qint64 checkId, const QString &email);
    void noReceipt();
    QVariantList receiptPrinters() const;
    // Refunds on closed checks, today's or found (pos_refunds.cpp): a
    // manager's; `cents` 0: all that's left of that payment.
    bool refundPayment(qint64 checkId, qint64 paymentId, qint64 cents, const QString &reason);
    const std::vector<core::Refund> &refundsToday() const { return s_->refundsToday; }
    // Cards on a reader (pos_cards.cpp).
    QString terminalCardReader() const override;
    QVariantMap readerToken() const override { return readerToken_; }
    QVariantMap cardCharge(const QString &tenderId);   // {ok, amountCents, tipCents, currency...} or {ok: false}
    bool recordCardPayment(const QVariantMap &result);
    bool addCardPayment(const QVariantMap &result);   // recordCardPayment, without asking who
    void requestReaderToken();
    // A Stripe reader beside this screen, run from the store's computer.
    QVariantMap counterCharge() const override { return counter_; }
    bool startCounterCharge(const QString &tenderId);
    bool cancelCounterCharge();
    bool presentTestCard(bool decline);   // Stripe test mode: a card "tapped" on the reader
    void pairCounterReader(const QVariantMap &record);
    QString counterReaderId() const;
    QString counterReaderLabel(const QString &id) const;
    enum class RefundStart { NotNeeded, Started, CantNow };
    RefundStart refundCardPayment(const core::Check &c, const core::Payment &p);
    // Phone orders and deliveries (pos_phone_orders.cpp).
    bool sameAsLastTime();
    bool sendOut(const QVariantList &checkIds, const QString &driverId);
    bool deliveryBack(qint64 checkId);
    bool nameRequired() const;
    QString missingWho(const core::Check &c) const;   // empty: it may go to the kitchen
    int readyQuote(const core::Check &c) const;        // minutes, from how busy the kitchen is
    void applyDeliveryFee(core::Check &c);
    void rememberOrder(core::CustomerRecord &r, const core::Check &c);
    QString lastOrderText(const core::Check &c) const;
    // A regular on file: "Regular · 7 visits", and their note ("allergic to nuts").
    QString regularText(const std::string &customerId) const;
    // The host stand (pos_waitlist.cpp).
    bool seatPartyAt(qint64 partyId, const QStringList &tables, const QString &serverId);
    bool seatWalkIn(int size, const QStringList &tables, const QString &serverId);
    bool reserveTables(qint64 partyId, const QStringList &tables);
    bool setTableState(const QString &table, const QString &state);
    bool tableTaken(const QString &table) const;   // an open table check is on it
    void tableEmptied(const std::string &table);   // its last check closed: dirty
    // Left the line, or (a reservation) never came.
    bool partyGone(qint64 id, bool noShow = false);
    QVariantMap waitlistInfo() const override;

    // --- inventory (pos_inventory.cpp) ------------------------------------------
    // What a line uses up (ingredient id -> amount).
    std::map<std::string, double> stockUse(const core::OrderLine &line) const;
    // Ingredients at or below their low mark.
    QVariantList lowStock() const;
    core::Report foodCostReport(const std::vector<core::Check> &closed, const core::ReportContext &ctx) const;
    core::Report menuEngineeringReport(const std::vector<core::Check> &closed, const core::ReportContext &ctx) const;
    // What a line cost: its cost at the time of sale when it has one (costAtSale), else (lines rung before
    // costs were kept) its item's cost today. `source` (when given): 2 at the time of sale, 1 today's
    // (an old line), 0 no cost at all.
    Money lineCost(const core::OrderLine &l, int *source = nullptr) const;
    // An item's cost today: the Menu Builder cost (__cost__, retail), else its recipe at today's
    // ingredient costs; nullopt when it has neither.
    std::optional<Money> itemCostNow(const core::MenuItem &m) const;
    // A line just rung in (or another round of one): who rang it on this terminal and when, and its
    // cost (and its modifiers') frozen from the menu as it is now.
    void stampNewLine(core::OrderLine &l) const;
    // A modifier just put on a line: its cost now (its linked item's), frozen.
    void stampModifier(core::Modifier &m) const;
    // A payment just added (tender, discount, gift card, card reader): who took it on this terminal, and when.
    void stampPayment(core::Payment &p) const;
    // Who is ringing on this terminal: {id, name}; the self-order kiosk: {"", "Kiosk"}.
    std::pair<std::string, std::string> ringer() const;
    // This screen's terminal name ("Bar 1"; the self-order kiosk: its own screen's name), recorded on
    // what is rung, paid, refunded, opened and closed here (OrderLine::addedOn, Payment::takenOn...).
    std::string terminalHere() const { return terminal_.toStdString(); }
    // A menu item's recipe at today's costs, and the items over the store's
    // food cost target: {name, price, cost, percent, suggested} (most over first).
    Money recipeCost(const core::MenuItem &m) const;
    QVariantList overFoodCost() const;
    // Deliveries received today: by vendor, then each one.
    core::Report purchasesReport(const core::ReportContext &ctx) const;
    // Today's deliveries' lines at an invoice cost other than the system cost (Reports: costdiffs).
    core::Report costDifferencesReport(const core::ReportContext &ctx) const;

    // --- the bar: bottles, bottled beer, draft lines and kegs (pos_bar.cpp) ------------------
    // Managers: {goods: [{id, name, kind, category, unit, packOz, perCase, size, onHand, onHandBottles, lastCount,
    // lastCountWhen, cost}], categories, kegs: [{id, name, beer, size, onHand, cost}], beers (draft beers on the
    // menu), allowance, links: {rows: [{item, good, amount, status}], toLink, newGoods, same, skipped}, undo}.
    QVariantMap barInventory() const override;
    // Anyone working (Change keg): {lines: [{id, tower, kegId, keg, beer, kegOz, poured, left, percentLeft,
    // since, by}], kegs: [{id, name, onHand}]}.
    QVariantMap draftLines() const override;
    // Link pours to bottles (managers): every drink item ("Tito's 1.25oz", "Kaiken Malbec Glass", a 375ml split,
    // bottled beer) to its bottle good, by name without the size; the goods made when missing. Draft pours
    // ("Guinness 16oz") pour from the keg on their beer's line. Undoable (undoLinkPours).
    bool linkPours();
    bool undoLinkPours();
    // The plan linkPours() follows (its preview).
    struct PourLink {
        int item = -1;              // index in the menu
        std::string goodKey;        // the good it pours from ("" for draft)
        std::string goodName;
        std::string goodId;         // an existing good; empty: made
        std::string kind;           // bottle | beer | draft
        double packOz = 0;
        double amount = 0;          // oz (bottle, draft) or bottles (beer) per pour
        std::string status;         // link | same | skip
        std::string note;
    };
    std::vector<PourLink> pourLinkPlan() const;
    // A shelf count in bottles and tenths (3.4), kegs, or bottles of beer: {lines: [{ingredient, count}]};
    // kept in ounces as a stock count (the opening count of the next).
    bool saveBarCount(const QVariantMap &count);
    // Change keg: {line, keg (a keg good's id; empty: none), kicked (the old one is empty) or left (oz in it)}.
    bool changeKeg(const QVariantMap &change);
    // A keg good: {beer, size (half | quarter | sixth), cost (per keg)}.
    bool addKeg(const QVariantMap &keg);
    // The foam / waste allowed on a keg (percent, 0-50).
    bool setKegAllowance(int percent);
    // Draft pours (takeStock): `oz` of `beer` from the keg on its line (sign -1: a void).
    void pourDraft(const std::string &beer, double oz);
    // Bottle variance: each bottle good's opening count + received - poured - closing count, over the counts
    // made from `from` to `to` (ms); kegs: each keg taken off then, and those on now.
    core::Report barVarianceReport(std::int64_t from, std::int64_t to, const core::ReportContext &ctx) const;
    core::Report kegReport(std::int64_t from, std::int64_t to, const core::ReportContext &ctx) const;
    // A drink's pricing aid: its cost per oz (its bottle's, its keg's) and 5x that for its pour, tax
    // included: {costPerOz, target} as text; empty when not known.
    QVariantMap drinkPricing(const core::MenuItem &m) const;

    // --- the schedule (pos_schedule.cpp) ----------------------------------------
    // {employeeId, start, end (ms or "yyyy-MM-dd HH:mm"; an end before the
    // start runs past midnight), note}. Managers.
    bool addShift(const QVariantMap &shift);
    bool removeShift(qint64 id);
    // A manager clocks someone in (off the schedule, or forgot).
    bool clockInEmployee(const QString &employeeId);
    // The week the schedule shows: 0 this week, 1 next...
    bool setScheduleWeek(int offset);
    QVariantMap scheduleInfo() const override;
    QString nextShift() const override;

    // --- reports over a range of days (pos_day.cpp) -------------------------------
    // period: week | lastWeek | month | lastMonth | year | custom (from / to:
    // yyyy-MM-dd); compare: beside the same days a year before. The answer
    // comes in rangeReport ({loading, report}) when the checks are read.
    bool requestRangeReport(const QString &id, const QString &period, const QString &from = {},
                            const QString &to = {}, bool compare = false);
    // --- tobacco scan data for Altria (pos_scan_data.cpp) -----------------------------------------
    // Reports > Scan Data: the week ending Saturday `weekEnding` (yyyy-MM-dd; empty: the last whole
    // week, Eastern time) built and checked; the answer comes in rangeReport ({id: scandata, report,
    // fileName, records, errors, warnings, settingsReady}). Save File writes it to ~/ViewTouch/scan-data
    // (VT_SCAN_DATA_DIR): never while the store's scan data settings are unset; with errors only `anyway`.
    bool requestScanData(const QString &weekEnding = {});
    bool saveScanData(bool anyway = false);
    // Fill in makers: every item's empty scan data maker and type from the rules (`overwrite`: all);
    // how many changed (-1: refused).
    int fillScanMakers(bool overwrite = false);
    // Store Settings > Tobacco scan data: its fields, values, and saving them (to this computer's file).
    QVariantList scanDataFields() const;
    QVariantMap scanDataValues() const;
    void saveScanDataRecord(const QVariantMap &record);
    static QJsonObject scanDataJson(const core::scandata::Settings &s);
    static core::scandata::Settings scanDataFromJson(const QJsonObject &o);
    // main: the settings from PosShared::scanDataFile (false: none yet).
    static bool loadScanDataSettings(PosShared &shared);
    core::Report scanDataReport(const core::scandata::Result &r, const QString &label) const;
    QVariantMap rangeReport() const override { return rangeReport_; }
    // A coming day (0 today ... 6), from the same weekday in past weeks
    // (pos_forecast.cpp); the answer comes in rangeReport ({id: forecast, report}).
    bool requestForecast(int daysAhead);
    core::Report forecastReport(QDate target, const std::vector<core::Check> &checks, const core::ReportContext &ctx) const;
    QVariantMap reportVariant(const core::Report &r) const;   // as the screen shows it
    // ILSA's sales history (pos_legacy.cpp; PosShared::legacyDb): kind z | depts | items | gpMonth |
    // gpYear | compare | differences over the business days from..to (yyyy-MM-dd; to empty: one
    // day; or week | lastWeek | month | lastMonth | year | day:<business day id>); kinds employees (by:
    // "vt|<filter>" with ViewTouch's sales, "|<filter>" without) and cashiers too; by (items): item | upc | brand | dept; (depts) dept: the department each line was sold in
    // (owner 2026-11-07), current: the items' departments now (as ILSA's printouts). The answer comes
    // in rangeReport ({id: ilsa-<kind>}).
    // store: an ILSA store (BHDAN, BHNM), "*" every store's, empty this site's (Store Settings' ILSA history store).
    bool requestLegacyReport(const QString &kind, const QString &from, const QString &to = {}, const QString &by = {},
                             const QString &store = {});
    QString legacyStore(const QString &asked) const;   // the stores asked for ("BHDAN,BHNM"): empty = every store's, "-" none
    // "<stores>|rollup": the ticked stores as one; otherwise a section per store (By store).
    static bool legacyByStore(const QString &asked);
    bool printLegacyReport();   // the one last shown
    // Reports > ILSA History > Cashiers: ILSA cashier `number` of copy `sourceId` is ViewTouch employee
    // `employeeId` (empty: a former employee); their ILSA sales then show under that name in By Employee.
    // allStores: the same cashier (number and name) in every store's copies too.
    bool setLegacyCashier(int sourceId, int number, const QString &employeeId, bool allStores = false);
    // Its receipts, read-only (Receipt Lookup > ILSA): found (rangeReport {id: ilsa-receipts, results,
    // more, nextOffset}), one shown (selected: {id, number, text, hasDob}), reprinted (withDob: as ILSA
    // printed it, the buyer's date of birth on it: Store Settings' dobReprintNeedsManager applies).
    bool findLegacyReceipts(const QString &query, int offset = 0, const QString &store = {});
    bool selectLegacyReceipt(qint64 id);
    bool reprintLegacyReceipt(qint64 id, bool withDob = false);
    QString legacyText(const core::legacy::Receipt &r) const;   // an ILSA receipt as it prints

    // Tips after tip-outs and pools (see core::tipShares), everyone / one person.
    std::map<std::string, core::TipShare> allTipShares() const;
    std::map<std::string, core::TipShare> allTipSharesNow() const;
    core::TipShare tipShareFor(const std::string &employeeId) const;
    bool stopPairing();

private:
    // The profit coach's tips, when they were worked out, and for which day.
    void refreshCoach() const;
    mutable QVariantMap coach_;
    mutable std::int64_t coachAt_ = 0;
    mutable std::int64_t coachDay_ = 0;
    // Why the last PIN was refused when it's because of guessing (wait), else `usual`.
    QString pinRefused(const QString &usual) const { return pinWait_.isEmpty() ? usual : pinWait_; }
    QString pinWait_;

    // One Menu Builder change, as one step to undo (PosShared::menuUndo).
    class MenuStep {
    public:
        MenuStep(PosService *pos, QString label);
        ~MenuStep();
        MenuStep(const MenuStep &) = delete;
        MenuStep &operator=(const MenuStep &) = delete;
    private:
        PosService *pos_;
        QString label_;
        bool outer_;
        PosShared::MenuState before_;
    };
    PosShared::MenuState menuState() const;

    // Stock out (sign 1) or back (-1) for these lines; then sold-out marks.
    void takeStock(const std::vector<core::OrderLine> &lines, int sign);
    // Sold out by itself when an ingredient runs short; back when restocked.
    void refreshSoldOut();
    // The shift `employeeId` may clock in for now (see clockInEarlyMinutes).
    const core::Shift *shiftNow(const std::string &employeeId) const;
    // Why `e` can't clock in now ("" = they can).
    QString scheduleCheck(const core::Employee &e) const;
    int scheduleWeek_ = 0;
    QVariantMap rangeReport_;
    int rangeRequest_ = 0;
    // The scan data file last built (Reports > Scan Data), for Save File.
    bool writeScanDataSettings();
    QByteArray scanFile_;
    QString scanFileName_;
    bool scanFileErrors_ = true;
    bool scanSettingsReady_ = false;
    core::Report forecast_;   // the forecast last shown (Print)
    core::Report legacyReport_;   // the ILSA History report last shown (Print)
    core::Report timeClockPaper_;   // the Time Clock report last shown over a range, for receipt paper (Print)
    // The Time Clock report (core::timeClockReport) over [from, to) from these punches; id "timeclock" or
    // "timeclock|<employee id>" (only theirs); `paper`: for the receipt printer.
    core::Report timeClockReport(const QString &id, std::int64_t from, std::int64_t to,
                                 const std::vector<core::TimePunch> &punches, bool paper) const;
    // Mass Updates' screen here (pos_mass_update.cpp).
    friend struct MassImpl;
    struct MassView;
    mutable std::shared_ptr<MassView> mass_;
    void massRememberItems();   // new items get today as when they were first had (New items)
    QVariantMap checkSearch_;
    QVariantMap exceptionSearch_;
    std::vector<core::ExceptionEntry> exceptionHits_;
    QVariantMap exceptionFilter_;
    QString exceptionLabel_;
    int exceptionShown_ = 0;
    int exceptionRequest_ = 0;
    void showExceptions();   // exceptionSearch_ from the hits and how many are shown
    QVariantMap printerSearch_;   // Find My Printers: {busy} | {done, found}
    std::vector<core::Check> searchHits_;
    qint64 selectedHit_ = 0;
    int searchRequest_ = 0;
    // A report that can cover several days, over `closed`.
    // `refunds`: made in the range (by date); none: those on these checks.
    core::Report rangeCapableReport(const QString &id, const std::vector<core::Check> &closed,
                                    const core::ReportContext &ctx, const std::vector<core::Refund> *refunds = nullptr,
                                    const std::vector<core::DrawerSession> *drawers = nullptr) const;
    qint64 addParty(const QVariantMap &r, bool reservation);
    core::Party *party(qint64 id);
    void saveParty(const core::Party &p);
    int quoteFor(int ahead) const;
    QVariantMap promotionRecord(const core::PosSettings::Promotion &p) const;
    bool savePromotionRecord(int index, const QVariantMap &record);
    QVariantMap customerSummary(const core::CustomerRecord &c) const;
    void saveCustomerRecord(const core::CustomerRecord &c);
    void saveGiftCardRecord(const core::GiftCard &g);
    void rememberCustomer(core::Check &c);
    const core::Tender &tenderOfKind(core::TenderKind kind, const char *id, const QString &name) const;
    bool chargeHouseAccount(core::Check &c, const core::Tender &t, Money amount);
    // A removed gift card / house account payment goes back where it came from.
    void returnPayment(const core::Check &c, const core::Payment &p);
    // Closing: gift cards sold go live, the customer's visit counts.
    void applyCloseEffects(core::Check &c);
    QString reopenBlocked(const core::Check &c) const;
    void undoCloseEffects(core::Check &c);
    Money promotionAmount(const core::PosSettings::Promotion &p, const core::Check &c) const;
    void applyPromotions(core::Check &c);
    // A closed check's coupons count as used (by 1), or not anymore (-1: reopened).
    void countCouponUses(const core::Check &c, int by);
    bool addCardHold(const QVariantMap &r);
    void letHoldGo(core::Check &c, const QString &why);
    bool startCounter(const QVariantMap &charge);
    QSet<qint64> holdCharging_;   // checks whose held card is being charged
    int pointsFor(const core::Check &c) const;
    void earnPoints(core::Check &c);
    void takeBackPoints(core::Check &c);
    // The tip the guest chose on the customer display, until a card takes it.
    struct TipChoice { bool asked = false; bool chosen = false; bool none = false; std::int64_t percentBp = 0;
                       Money amount; std::int64_t checkId = 0; };
    TipChoice tipChoice_;
    Money tipFor(const core::Check &c) const;
    QString customerQuery_;
    std::string selectedCustomer_;
    QString giftCardNumber_;
    QVariantMap pendingCustomer_;     // waiting on the duplicate window
    QVariantList customerMatches_;
    struct PendingScan { QString itemId, code; bool retail = false; };
    PendingScan pendingScan_;   // for the next addItem
    QVariantMap ageAsk_;              // an item waiting for the buyer's ID (pos_age.cpp)
    // Responsible service (pos_over_service.cpp): the drinks waiting, and how to ring them in again.
    struct PendingDrink {
        core::DrinkGuest guest;
        double drinks = 0;
    };
    QVariantMap serviceAsk_;
    std::vector<PendingDrink> serviceDrinks_;   // what the ask was for (guests, drinks)
    QString serviceMethod_;
    QVariantList serviceArgs_;
    bool servicePassed_ = false;                // the drink acknowledged / overridden: once
    std::map<std::string, std::vector<core::DrinkRing>> serviceRings() const;
    QString guestLabel(const core::DrinkGuest &g) const;
    // Before drinks go on: true to go ahead; false when it waits for the bartender or a manager
    // (serviceAsk_), or was refused. `method` / `args`: how to ring them in again (else the operation
    // running through invoke).
    bool serviceGate(const core::Check *c, const std::vector<PendingDrink> &adds, const QString &what,
                     const QString &method, const QVariantList &args);
    void serviceAudit(const QString &action, const QString &manager = {}, const QString &reason = {});
    bool serviceProceed();
    PendingScan ageAskScan_;          // the scan that rang it in, kept for when the ID passes
    bool askForAge(const core::MenuItem &item, int age, const PendingScan &scan);
    bool agePassed(int age, const char *how, const std::string &idNumber, bool expired = false, const core::IdDate &birth = {});
    bool checkAge(const core::IdDate &birth, const core::IdDate &expires, const char *how, const std::string &idNumber);
    QString labelQuery_;
    QStringList labelPath_;
    bool labelBrowsing_ = false;   // the Labels screen is showing the tree (worked out only then)
    bool labelShowItems_ = false;  // ... and the items under its path
    QString labelPreview_;         // data:image/png;base64,... of the last preview
    QString labelPreviewName_;     // what it shows
    QString labelPreviewError_;
    int labelPreviewRun_ = 0;      // a newer preview wins over one still being drawn
    bool labelPreviewBusy_ = false;
    // Quick Label (pos_inventory.cpp): what quickLabel() answers.
    QVariantList quickRolls_;
    QString quickRoll_;              // the roll last used (the label utility remembers it)
    bool quickLoading_ = false;
    QString quickPreview_, quickPreviewFor_, quickPreviewError_;
    int quickPreviewRun_ = 0;
    bool quickPreviewBusy_ = false;
    bool quickPrinting_ = false;
    QString quickStatus_;
    bool quickStatusOk_ = false;
    // The Labels screen / Item Look Up answer last given, and the last search by name (pos_inventory.cpp):
    // given again as it is while nothing it comes from has changed; a longer search starts from the last.
    struct LabelLookup;
    mutable std::shared_ptr<LabelLookup> labelLookup_;
    QString itemQuery_;               // Find (findItems)
    int itemLimit_ = 50;              // how many of the best it shows
    // Every item's id, number, barcodes and name, to the item's place in the menu (findItem, scans,
    // price check): made again when the menu may have changed (pos_service.cpp).
    struct CodeIndex;
    mutable std::shared_ptr<CodeIndex> codeIndex_;
    const CodeIndex &codeIndex() const;
    struct ItemFind;
    mutable std::shared_ptr<ItemFind> itemFind_;
    std::string receiptPrinter() const { return s_->settings.receiptPrinterFor(terminal_.toStdString()); }
    // Why this terminal's drawer can't open (its printer has no drawer, or isn't
    // set up); empty: it opens. No other printer's drawer is ever opened instead.
    QString drawerProblem() const { return drawerProblemFor(terminal_.toStdString()); }
    QString drawerProblemFor(const std::string &terminal) const;
    // The drawer opened on this terminal's printer, else that said on its screen
    // (its printer alert, a notice, Store Check's problems). False: it didn't open.
    bool openCashDrawer();
    void noteDrawerMiss(const QString &why);
    // Screens seen (this one, those connected) with no line in Manager -> Terminals,
    // and the printer they fall back to ("Front, Patio use Receipt").
    QString unlistedScreensText() const;
    // Cash handling for the logged-in person: their own choice, else the store's.
    bool serverBank() const;
    bool terminalHasDrawer() const { return s_->settings.hasDrawer(terminal_.toStdString()); }
    // Why cash can't go in this terminal's drawer (none, or not started).
    QString noDrawerMessage() const;
    // A cash refund on a terminal without a cash drawer (a tablet): suspend the sale, the register gives it.
    QString noDrawerRefundMessage() const;
    // Whether the logged-in person must close their checks before checking out.
    bool mustCloseChecksToCheckOut() const;
    core::DrawerSession *myDrawer();
    const core::DrawerSession *latestMyDrawer() const;
    // Server banks: my open bank, started at `start` if I have none.
    core::DrawerSession *ensureMyBank(Money start = {});
    bool closeDrawer(core::DrawerSession &d);   // counted = keypad entry
    Money expectedNow(const core::DrawerSession &d) const;
    void connectShared();
    core::Check *current();
    // A new, empty check at the same table as check `fromId` (same server, type).
    // Sizes typed in ([{name, price}]): checked (empty rows dropped), or
    // nullopt having said what's wrong.
    std::optional<std::vector<std::pair<QString, qint64>>> sizeList(const QVariantList &sizes);
    core::Check &splitOff(qint64 fromId);
    // `noteOn`: the check the approval is noted on (else the open one).
    bool require(const char *permission, const QString &action, core::Check *noteOn = nullptr);
    // The retail price book (perm::PriceBook, owner 2026-11-24): an admin's to change, or a manager's
    // with an admin's PIN (require()). `touches` says whether the change reaches the price book (retail
    // items and Retail categories; worked out only when it matters). Hospitality and event items: true,
    // as before. Scripts (syncPriceBook) don't come through here.
    bool requirePriceBook(const std::function<bool()> &touches);
    bool mayChangePriceBook() const;
    // Every category's kind now, and whether an item or a category is the price book's.
    std::map<std::string, std::string> scopesNow() const;
    bool priceBookItem(const core::MenuItem &m) const { return core::inPriceBook(m, scopesNow()); }
    bool priceBookCategory(const std::string &id) const;
    // Allowed once for an operation that does several (Select's changes, an import): the steps inside
    // don't ask again.
    int priceBookGrant_ = 0;
    struct PriceBookGrant {
        PosService *pos;
        explicit PriceBookGrant(PosService *p) : pos(p) { ++pos->priceBookGrant_; }
        ~PriceBookGrant() { --pos->priceBookGrant_; }
        PriceBookGrant(const PriceBookGrant &) = delete;
        PriceBookGrant &operator=(const PriceBookGrant &) = delete;
    };
    // The operation running through invoke(), to try again once approved.
    struct Running { QString method; QVariantList args; };
    std::optional<Running> running_;
    // Waiting for a manager's PIN: {needed, action, permission}; and the
    // one-time approval that lets the operation through.
    QVariantMap approval_;
    // Why: a void or comp made on a screen waits for its reason (reasonAsk_),
    // then runs again with it (reason_, for that operation only).
    struct GivenReason { QString method; std::string reason; };
    std::optional<GivenReason> reason_;
    QVariantMap reasonAsk_;
    QVariantMap priceCheck_;   // {on, item}
    QVariantMap itemReturn_;   // ITEM RETURN's window (itemReturn())
    // A sale made less than nothing by an item return: what is still to give back in cash (0: none).
    // (Ordinary change, from cash handed over, is never this.)
    static Money refundDue(const core::Totals &t)
    {
        const Money cashIn = t.cashPaid.cents() > 0 ? t.cashPaid : Money();
        return t.change > cashIn ? t.change - cashIn : Money();
    }
    bool showItemReturn(const core::MenuItem &m, const QString &code);
    QString reasonMethod_;
    QVariantList reasonArgs_;
    // Whether this can go ahead: no reason needed, or given. Else asks (false).
    bool haveReason(const char *kind, const QString &what);
    std::string takeReason();
    QVariantList approvalArgs_;
    QString approvalMethod_;
    struct Approved { std::string permission; std::string by; };
    std::optional<Approved> approved_;
    bool trainingOn_ = false;
    bool selfOrder_ = false;
    bool kioskToGo_ = false;
    QVariantMap lastKioskOrder_;   // the confirmation the guest sees
    bool closePractice(core::Check &c);   // a manager switched this screen to practice
    bool fail(const QString &message);
    // A receipt reference of this store (its check prefix); else says where it's from (false).
    bool receiptIsOurs(const core::ReceiptReference &ref);
    void changed(core::Check &check);   // persist + notify
    const core::Employee *employeeByPin(const QString &pin) const;
    core::TimePunch *openPunch(const std::string &employeeId);
    // Start a shift: the job and its pay recorded on the punch.
    bool punchIn(const core::Employee &e, const core::Job &job, const QString &by = {});
    std::string jobChoice_;   // waiting for this person to pick a job
    std::int64_t cashTipsPunch_ = 0;   // waiting for the cash tips kept on this shift
    std::string clockWho_;
    // The dashboard's "same day last week, by this time" (read from the store; kept a few minutes).
    mutable std::int64_t lastWeekAt_ = 0;
    mutable Money lastWeekNet_;
    mutable int lastWeekChecks_ = -1;    // the Time Clock screen: whose PIN was typed (not logged in)
    bool clockOutFor(const core::Employee &e);
    // Hours this pay week and today, and how long until overtime (daily or
    // weekly rule, whichever comes first): {weekHours, todayHours, leftMinutes, state ok|soon|over}.
    QVariantMap overtimeFor(const std::string &employeeId) const;
    // Time off and shift swaps (pos_requests.cpp).
    core::PosSettings::StaffRequest *staffRequest(std::int64_t id);
    const core::Shift *shiftById(std::int64_t id) const;
    QString requestText(const core::PosSettings::StaffRequest &r) const;
    QString requestStatusText(const core::PosSettings::StaffRequest &r) const;
    void addStaffRequest(core::PosSettings::StaffRequest r);
    QVariantMap requestsFor(const std::string &employeeId) const;
    std::vector<core::PosSettings::StaffRequest *> requestList();
    QVariantList requestFields();
    QVariantList requestRecords();
    bool saveRequestRecord(int index, const QVariantMap &record);
    int requestsWaiting() const;
    // Manager -> Time Punches: the last week's, newest first; change, add, remove.
    std::vector<core::TimePunch *> punchList();
    QVariantList punchFields();
    QVariantList punchRecords();
    QVariantMap punchNewRecord();
    bool savePunchRecord(int index, const QVariantMap &record);
    bool deletePunchRecord(int index, const QString &why);
    // End of Day: a manager clocks out someone still on the clock (logged as a change).
    bool clockOutPunch(qint64 punchId);
    // invoke()'s operations by name (a member: they reach the private ones).
    using Command = std::function<QVariant(PosService &, const QVariantList &)>;
    static const QHash<QString, Command> &commandTable();
    void logPunchChange(const core::TimePunch &p, const QString &what, const QString &why);
    bool toggleBreakFor(const core::Employee &e);
    QString expenseCategory_;
    std::int64_t now() const { return s_->now(); }
    bool lockCheck(std::int64_t checkId);   // false: open on another terminal
    void unlockCheck(std::int64_t checkId);
    QString lockHolder(std::int64_t checkId) const;
    QString dueText(std::int64_t at) const;                 // "6:30 PM", "tomorrow 6:30 PM"
    bool waitingForLater(const core::Check &c) const;       // not the kitchen's yet
    bool forAnotherDay(const core::Check &c) const;
    bool saveAdminRecord(const QString &panel, int index, const QVariantMap &record);
    bool deleteAdminRecord(const QString &panel, int index);
    // A manager screen's record saved: each field changed (records before the save).
    void logRecordChanges(const QString &panel, int index, const QVariantList &before, const QVariantMap &saved);
    void logMenuChanges(const PosShared::MenuState &before, const PosShared::MenuState &after, const QString &step);
    void noteEvent(core::Check &c, const QString &what, const char *kind, Money amount = {}, const std::string &reason = {});
    bool mayManage(const core::Check &c, const QString &action);
    bool mayOpen(const core::Check &c) const;
    QStringList onItOf(const core::MenuItem &m) const;
    // Choices priced by size: the size on a line, an option's price on it,
    // and its choices' prices brought in line with it.
    std::string sizeOf(const core::OrderLine &l) const;
    Money optionPrice(const core::ModifierOption &o, const core::OrderLine &l) const;
    void repriceForSize(core::OrderLine &l) const;
    QVariantList sizesOf(const core::MenuItem &m) const;
    core::ReportContext reportContext(const QString &period) const;
    QString dayLabel(const core::BusinessDay &day) const;
    bool saveMenuRecord(int index, const QVariantMap &record);
    bool saveBillboardRecord(int index, const QVariantMap &record);
    QVariantList billboardFields() const;
    bool saveEmployeeRecord(int index, const QVariantMap &record);
    bool saveTenderRecord(int index, const QVariantMap &record);
    bool saveMealPeriodRecord(int index, const QVariantMap &record);
    bool saveModifierGroupRecord(int index, const QVariantMap &record);
    QVariantMap menuRecord(const core::MenuItem &m) const;
    QStringList groupIds() const;
    QStringList periodIds() const;
    // The chart of accounts' keys, named: sales, sales:<family>, tender:<id>...
    QVariantList accountKeys() const;
    // Store Settings -> About: the store's program, where its data is, its
    // backups ("about.*", read only).
    QVariantMap aboutValues() const;
    bool savePrinterRecord(int index, const QVariantMap &record);
    void settingsChanged();
    void menuChanged();   // the menu changed: sold out again, every screen's menu

    PosShared *s_;
    std::unique_ptr<PosShared> owned_;
    QString terminal_;

    std::string userId_;
    std::int64_t currentId_ = 0;
    int seat_ = 0;      // seat for new items (0: none)
    int course_ = 1;    // course for new items
    bool lineTouched_ = false;   // the selected line was touched (not just added)
    std::int64_t choosingLine_ = 0;   // the line whose modifiers are being chosen
    std::int64_t mixerLine_ = 0;      // the bar: the spirit waiting for its mixer (pos_bar_menu.cpp)
    QString pourNotesItem_;           // orderPour: the item whose notes wait to go on when it's rung
    QStringList pourNotes_;
    void addBarNotes(core::OrderLine &line, const QStringList &notes);
    // An item already on the check (Choose): as it was, for Cancel Changes;
    // empty: just added (Cancel Item takes it off).
    std::optional<core::OrderLine> choosingBefore_;
    QString weighing_;                // an item sold by weight, waiting for its weight
    bool retireMeAtFinish_ = false;   // setup guide: this sample manager goes off at Finish
    qint64 selectedLine_ = 0;
    QVariantMap readerToken_;          // the latest connection token for this terminal's reader
    int readerTokenSeq_ = 0;
    std::set<std::int64_t> refunding_; // card payments whose refund is on its way
    core::Check *closedCheckFor(qint64 checkId);
    // The card a countertop reader is taking: {status: starting | waiting,
    // reader, readerLabel, paymentIntent, amount, test...}; empty when none.
    QVariantMap counter_;
    QVariantMap receiptOffer_;   // {checkId, label, total, printers, choosePrinter, canEmail, email}; empty: none
    const core::TerminalConfig *terminalConfig() const;
    bool askReceiptPrinter() const;
    const core::Check *checkForReceipt(qint64 checkId) const;
    void offerReceipt(const core::Check &c, bool choosePrinter);
    void receiptAfterClosing(const core::Check &c);
    QTimer counterPoll_;
    QTimer priceClock_;   // price changes set for later
    bool applyPrices(const QVariantList &prices, const QString &label);
    int counterPolls_ = 0;
    bool counterAsking_ = false;
    void pollCounter();
    bool counterHeir_ = false;   // watching a card for a terminal that went away
    void adoptCounterCharge(const QVariantMap &state);
    void counterPaid(const QString &paymentIntent);
    void counterDone(const QString &message);
    void stripe(const QString &method, const QString &path, const QString &form,
                std::function<void(const QJsonObject &, const QString &)> done);
    // The last item taken off (or made fewer) on this terminal's check, for Undo.
    struct LastChange {
        std::int64_t checkId = 0;
        core::OrderLine before;   // the line as it was
        std::size_t index = 0;    // where it was on the check
        bool removed = false;
        std::int64_t at = 0;
        QString text;
    };
    std::optional<LastChange> lastChange_;
    void rememberChange(const core::Check &c, const core::OrderLine &before, bool removed, const QString &text);
    qint64 selectedPayment_ = 0;
    core::Qualifier qualifier_ = core::Qualifier::None;
    std::int64_t lastClosedId_ = 0;
    QString checkFilter_;
    QString pin_;
    QString entry_;
    std::vector<std::pair<std::int64_t, int>> countedPieces_;   // countDrawerPieces -> closeDrawer
    QString text_;
    QString pendingTable_;
};

} // namespace vt::app
