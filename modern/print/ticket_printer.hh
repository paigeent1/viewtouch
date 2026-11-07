#pragma once

#include "app/pos_service.hh"
#include "print/spooler.hh"
#include "print/tickets.hh"

#include <QByteArray>
#include <QString>

#include <map>
#include <memory>

namespace vt::print {

// PosPrinter that formats tickets and hands them to the spooler.
//
// Routing: kitchen lines go to the printer named by each menu item
// ("kitchen", "bar"...), falling back to "kitchen"; receipts, reports and
// drawer kicks to the terminal's receipt printer (falling back to "receipt",
// and for kicks to any printer with a drawer attached).
// Relative file paths are resolved against `outputDir`.
class TicketPrinter : public app::PosPrinter {
public:
    TicketPrinter(PrintSpooler &spooler, QString outputDir);

    void printKitchen(const core::PosSettings &settings, const core::Check &check,
                      const std::vector<core::OrderLine> &lines, bool voids) override;
    void printReceipt(const core::PosSettings &settings, const core::Check &check,
                      const std::string &printerId) override;
    void printReport(const core::PosSettings &settings, const core::Report &report,
                     const std::string &printerId) override;
    void openDrawer(const core::PosSettings &settings, const std::string &printerId) override;
    void printNoSale(const core::PosSettings &settings, const core::CashMovement &noSale, int number,
                     const std::string &drawer, const std::string &printerId) override;
    bool printTestPage(const core::PosSettings &settings, const std::string &printerId, bool kickDrawer) override;
    void printOrderSlip(const core::PosSettings &settings, const core::Check &check, const std::string &printerId,
                        bool sent) override;
    void printPrivacyNotice(const core::PosSettings &settings, const std::string &printerId) override;
    QString receiptText(const core::PosSettings &settings, const core::Check &check, bool gift) override;
    void printGiftReceipt(const core::PosSettings &settings, const core::Check &check, const std::string &printerId) override;
    void printLegacyReceipt(const core::PosSettings &settings, const core::Report &asReport,
                            const std::vector<std::pair<std::string, std::string>> &rows, const std::string &printerId) override;
    QString legacyReceiptText(const core::PosSettings &settings, const std::vector<std::pair<std::string, std::string>> &rows) override;
    // Next to the Daily Reports folder: Desktop/Privacy Notice.txt (rewritten only when it changes).
    QString savePrivacyNotice(const core::PosSettings &settings) override;
    QString saveExceptionTickets(const core::PosSettings &settings, const core::Report &exceptions,
                                 const std::vector<ExceptionTicket> &tickets, std::int64_t dayId,
                                 std::int64_t dayOpenedAt) override;
    QString saveEndOfDay(const core::PosSettings &settings, const std::vector<core::Report> &summary,
                         const core::Report &exceptions, const std::vector<ExceptionTicket> &tickets,
                         std::int64_t dayId, std::int64_t dayOpenedAt) override;
    // Where each day's folder goes: $VT_REPORTS_DIR (an extra drive later), else "Daily Reports"
    // on the Pi's desktop (or in the home folder without one).
    void setExceptionsDir(QString dir) { exceptionsDir_ = std::move(dir); }
    QString dayFolder(std::int64_t dayId, std::int64_t dayOpenedAt) const;
    void printLabels(const core::PosSettings &settings,
                     const std::vector<std::pair<std::string, std::vector<std::string>>> &labels,
                     const std::string &printerId) override;

    // For tests: the clock used on tickets.
    void setClock(std::function<std::int64_t()> now) { now_ = std::move(now); }
    // The store's pictures (by ref, "store:logo.png" or a path), for the logo on receipts.
    void setImageSource(std::function<QByteArray(const QString &ref)> source) { imageSource_ = std::move(source); }
    // The logo as printed on a printer `widthChars` wide (null: none set, or not a picture).
    std::shared_ptr<const Raster> logoFor(const core::PosSettings &settings, int widthChars);

private:
    void send(const core::PosSettings &settings, const core::PrinterConfig &printer, const Document &doc,
              const QString &description);
    TicketContext context(const core::PosSettings &settings) const;

    PrintSpooler &spooler_;
    QString outputDir_;
    QString exceptionsDir_;
    std::function<std::int64_t()> now_;
    std::function<QByteArray(const QString &)> imageSource_;
    // Dithered once per logo and paper width.
    std::map<std::pair<QByteArray, int>, std::shared_ptr<const Raster>> logos_;
};

} // namespace vt::print
