#include "print/ticket_printer.hh"

#include "print/labels.hh"

#include "app/i18n.hh"
#include "app/pos_session.hh"
#include "print/raster.hh"
#include "print/ticket_image.hh"
#include "print/day_summary.hh"

#include <algorithm>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QImageWriter>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QLocale>

#include <map>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::print {

TicketPrinter::TicketPrinter(PrintSpooler &spooler, QString outputDir)
    : spooler_(spooler)
    , outputDir_(std::move(outputDir))
    , now_([] { return QDateTime::currentMSecsSinceEpoch(); })
{
}

TicketContext TicketPrinter::context(const PosSettings &settings) const
{
    return TicketContext{
        settings,
        // In the store's language, like the rest of what's printed.
        [loc = i18n::locale(i18n::printLanguage(QString::fromStdString(settings.language)))](std::int64_t ms) {
            return (loc.toString(QDateTime::fromMSecsSinceEpoch(ms).date(), QLocale::ShortFormat) + u' '
                    + loc.toString(QDateTime::fromMSecsSinceEpoch(ms).time(), u"h:mm AP"_s)).toStdString();
        },
        [loc = i18n::locale(i18n::printLanguage(QString::fromStdString(settings.language)))](std::int64_t ms) {
            return loc.toString(QDateTime::fromMSecsSinceEpoch(ms).time(), u"h:mm AP"_s).toStdString();
        },
        now_(),
        nullptr,
        [](const std::vector<std::string> &ids) {
            std::vector<std::string> out;
            for (const QString &n : app::PosSession::allergenNames(ids))
                out.push_back(n.toStdString());
            return out;
        },
        [code = i18n::printLanguage(QString::fromStdString(settings.language))](const std::string &english) {
            return i18n::phrase(code, QString::fromStdString(english)).toStdString();
        },
    };
}

void TicketPrinter::send(const PosSettings &, const PrinterConfig &printer, const Document &doc,
                         const QString &description)
{
    PrinterConfig target = printer;
    if (target.type == "file" && QDir::isRelativePath(QString::fromStdString(target.path)))
        target.path = QDir(outputDir_).filePath(QString::fromStdString(target.path)).toStdString();
    Document d = doc;
    d.cut = d.cut && printer.cutter;
    const std::string bytes = printer.effectiveFormat() == "escpos"
                                  ? renderEscPos(d, printer.width, printer.charset != "ascii")
                                  : renderText(d, printer.width);
    spooler_.submit(target, QByteArray::fromStdString(bytes), description);
}

void TicketPrinter::printKitchen(const PosSettings &settings, const Check &check,
                                 const std::vector<OrderLine> &lines, bool voids)
{
    const i18n::Scope language(i18n::printLanguage(QString::fromStdString(settings.language)));   // the store's, not the server's
    // Group by station; items without a printer (or with an unknown one) go
    // to the kitchen, and nowhere if there is no kitchen printer either.
    std::map<std::string, std::vector<OrderLine>> byStation;
    for (const OrderLine &l : lines) {
        if (!l.isComment() && !l.forKitchen())   // gift cards, hidden items: nothing to make
            continue;
        std::string station = l.printer.empty() ? "kitchen" : l.printer;
        if (!settings.printer(station)) {
            // Routed to a station that has a screen and no printer (the humidor, for a runner): no ticket.
            if (std::ranges::any_of(settings.stations, [&](const Station &s) { return s.id == station; }))
                continue;
            station = "kitchen";
        }
        byStation[station].push_back(l);
    }
    const TicketContext ctx = context(settings);
    for (const auto &[station, stationLines] : byStation) {
        const PrinterConfig *p = settings.printer(station);
        if (!p)
            continue;
        send(settings, *p, kitchenTicket(check, stationLines, p->name, voids, ctx),
             (voids ? u"Void ticket %1"_s : u"Ticket %1"_s).arg(QString::fromStdString(check.label)));
    }
}

namespace {
const PrinterConfig *receiptPrinter(const PosSettings &settings, const std::string &id)
{
    const PrinterConfig *p = settings.printer(id);
    return p ? p : settings.printer("receipt");
}
} // namespace

std::shared_ptr<const Raster> TicketPrinter::logoFor(const PosSettings &settings, int widthChars)
{
    if (!imageSource_ || settings.displayLogo.empty())
        return nullptr;
    const QByteArray picture = imageSource_(QString::fromStdString(settings.displayLogo));
    if (picture.isEmpty())
        return nullptr;
    const auto key = std::pair{QCryptographicHash::hash(picture, QCryptographicHash::Sha1), widthChars};
    if (const auto it = logos_.find(key); it != logos_.end())
        return it->second;
    // 12 dots a character (80 mm paper: 48 chars, 576 dots); the logo takes
    // three quarters of it at most, and about 2.5 cm of paper.
    const int paper = std::clamp(widthChars * 12, 192, 576);
    auto raster = rasterize(picture, paper * 3 / 4, 200);
    logos_[key] = raster;
    return raster;
}

void TicketPrinter::printReceipt(const PosSettings &settings, const Check &check, const std::string &printerId)
{
    const i18n::Scope language(i18n::printLanguage(QString::fromStdString(settings.language)));   // the guest's: the store's
    if (const PrinterConfig *p = receiptPrinter(settings, printerId)) {
        TicketContext ctx = context(settings);
        if (settings.receiptLogo && p->effectiveFormat() == "escpos")
            ctx.logo = logoFor(settings, p->width);
        send(settings, *p, receipt(check, ctx), u"Receipt #%1"_s.arg(check.id));
    }
}

void TicketPrinter::printOrderSlip(const PosSettings &settings, const Check &check, const std::string &printerId,
                                   bool sent)
{
    const i18n::Scope language(i18n::printLanguage(QString::fromStdString(settings.language)));
    if (const PrinterConfig *p = receiptPrinter(settings, printerId)) {
        TicketContext ctx = context(settings);
        if (settings.receiptLogo && p->effectiveFormat() == "escpos")
            ctx.logo = logoFor(settings, p->width);
        send(settings, *p, orderSlip(check, sent, ctx), u"Order slip #%1"_s.arg(check.id));
    }
}

QString TicketPrinter::receiptText(const PosSettings &settings, const Check &check, bool gift)
{
    const i18n::Scope language(i18n::printLanguage(QString::fromStdString(settings.language)));
    const PrinterConfig *p = receiptPrinter(settings, {});
    const TicketContext ctx = context(settings);
    return QString::fromStdString(renderText(gift ? giftReceipt(check, ctx) : receipt(check, ctx), p ? p->width : 42));
}

void TicketPrinter::printGiftReceipt(const PosSettings &settings, const Check &check, const std::string &printerId)
{
    const i18n::Scope language(i18n::printLanguage(QString::fromStdString(settings.language)));
    if (const PrinterConfig *p = receiptPrinter(settings, printerId)) {
        TicketContext ctx = context(settings);
        if (settings.receiptLogo && p->effectiveFormat() == "escpos")
            ctx.logo = logoFor(settings, p->width);
        send(settings, *p, giftReceipt(check, ctx), u"Gift receipt #%1"_s.arg(check.id));
    }
}

void TicketPrinter::printPrivacyNotice(const PosSettings &settings, const std::string &printerId)
{
    const i18n::Scope language(i18n::printLanguage(QString::fromStdString(settings.language)));
    if (const PrinterConfig *p = receiptPrinter(settings, printerId)) {
        TicketContext ctx = context(settings);
        if (settings.receiptLogo && p->effectiveFormat() == "escpos")
            ctx.logo = logoFor(settings, p->width);
        send(settings, *p, privacyNoticeTicket(ctx), u"Privacy notice"_s);
    }
}

QString TicketPrinter::savePrivacyNotice(const PosSettings &settings)
{
    // Next to the Daily Reports folder (on the Pi: the Desktop).
    const QString reports = QFileInfo(dayFolder(0, 0)).absolutePath();
    const QString path = QDir(QFileInfo(reports).absolutePath()).filePath(u"Privacy Notice.txt"_s);
    const QByteArray text = QByteArray::fromStdString(privacyText(settings, true)) + '\n';
    QFile f(path);
    if (f.open(QIODevice::ReadOnly) && f.readAll() == text)
        return path;
    f.close();
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(text) != text.size() || !out.commit())
        return {};
    return path;
}

void TicketPrinter::printReport(const PosSettings &settings, const Report &report, const std::string &printerId)
{
    if (const PrinterConfig *p = receiptPrinter(settings, printerId))
        send(settings, *p, reportTicket(report, context(settings)), QString::fromStdString(report.title));
}

void TicketPrinter::printLabels(const PosSettings &settings,
                                const std::vector<std::pair<std::string, std::vector<std::string>>> &labels,
                                const std::string &printerId)
{
    const PrinterConfig *p = settings.printer(printerId);
    if (!p || labels.empty())
        return;
    std::vector<Label> list;
    for (const auto &[title, lines] : labels)
        list.push_back({title, lines});
    const std::string format = p->effectiveFormat();
    if (format == "zpl" || format == "tspl") {
        PrinterConfig target = *p;
        if (target.type == "file" && QDir::isRelativePath(QString::fromStdString(target.path)))
            target.path = QDir(outputDir_).filePath(QString::fromStdString(target.path)).toStdString();
        const std::string bytes = format == "zpl" ? renderZpl(list, p->labelWidthMm, p->labelHeightMm)
                                                 : renderTspl(list, p->labelWidthMm, p->labelHeightMm);
        spooler_.submit(target, QByteArray::fromStdString(bytes), u"Labels"_s);
        return;
    }
    for (const Label &l : list)   // a receipt printer: a slip each
        send(settings, *p, labelSlip(l), u"Label"_s);
}

void TicketPrinter::openDrawer(const PosSettings &settings, const std::string &printerId)
{
    // The terminal's own printer when its drawer is wired there, else any
    // printer with a drawer.
    const PrinterConfig *mine = receiptPrinter(settings, printerId);
    if (mine && mine->drawerKick) {
        send(settings, *mine, drawerKick(), u"Open drawer"_s);
        return;
    }
    for (const PrinterConfig &p : settings.printers) {
        if (p.drawerKick) {
            send(settings, p, drawerKick(), u"Open drawer"_s);
            return;
        }
    }
}

void TicketPrinter::printNoSale(const PosSettings &settings, const CashMovement &m, int number,
                                const std::string &drawer, const std::string &printerId)
{
    const i18n::Scope language(i18n::printLanguage(QString::fromStdString(settings.language)));
    // On the printer the drawer is wired to (as openDrawer picks it): the slip and the kick together.
    const PrinterConfig *p = receiptPrinter(settings, printerId);
    if (!p || !p->drawerKick) {
        const auto it = std::ranges::find_if(settings.printers, [](const PrinterConfig &c) { return c.drawerKick; });
        if (it != settings.printers.end())
            p = &*it;
    }
    if (!p)
        return;
    Document d = noSaleTicket(m, number, drawer, context(settings));
    d.kickDrawer = p->drawerKick;
    send(settings, *p, d, QString::fromStdString("No Sale #" + std::to_string(number)));
}

QString TicketPrinter::saveExceptionTickets(const PosSettings &settings, const Report &exceptions,
                                           const std::vector<ExceptionTicket> &tickets, std::int64_t dayId,
                                           std::int64_t dayOpenedAt)
{
    const i18n::Scope language(i18n::printLanguage(QString::fromStdString(settings.language)));
    const QString date = QDateTime::fromMSecsSinceEpoch(dayOpenedAt ? dayOpenedAt : now_()).toString(u"yyyy-MM-dd"_s);
    const QString dir = dayFolder(dayId, dayOpenedAt);
    if (!QDir().mkpath(dir))
        return {};
    // The store's copy: every void and error correct shows, whatever the customer's receipt hides.
    PosSettings store = settings;
    store.receiptShowVoids = true;
    const TicketContext ctx = context(store);
    const PrinterConfig *rp = receiptPrinter(settings, "");
    const int width = rp ? std::max(32, rp->width) : 42;
    const bool jpeg = QImageWriter::supportedImageFormats().contains("jpg");
    const char *format = jpeg ? "JPG" : "PNG";
    const QString ext = jpeg ? u"jpg"_s : u"png"_s;

    std::vector<QImage> pictures;
    const auto keep = [&](const Document &d, const QString &name) {
        QImage img = ticketImage(d, width);
        img.save(QDir(dir).filePath(name + u'.' + ext), format, 90);
        pictures.push_back(std::move(img));
    };
    keep(reportTicket(exceptions, ctx), u"exceptions-report"_s);
    for (const ExceptionTicket &t : tickets) {
        Document d;
        if (t.check)
            d = receipt(*t.check, ctx);
        else if (t.noSale)
            d = noSaleTicket(*t.noSale, t.number, t.drawer, ctx);
        else
            continue;
        // Says what it is above the ticket itself.
        std::vector<Document::Line> head;
        head.push_back({Document::Kind::Text, ctx.tr("STORE COPY: EXCEPTION"), {}, Document::Align::Center, true});
        head.push_back({Document::Kind::Text, t.title, {}, Document::Align::Center, true});
        head.push_back({Document::Kind::Rule});
        d.lines.insert(d.lines.begin(), head.begin(), head.end());
        d.lines.erase(std::remove_if(d.lines.begin(), d.lines.end(),
                                     [](const Document::Line &l) { return l.kind == Document::Kind::Image; }),
                      d.lines.end());
        keep(d, QString::fromStdString(t.file));
    }

    // All of it in one PDF: the report first, then the tickets side by side, three across.
    const QString pdfFile = QDir(dir).filePath(u"Exceptions %1.pdf"_s.arg(date));
    {
        QPdfWriter pdf(pdfFile);
        pdf.setPageSize(QPageSize(QPageSize::Letter));
        pdf.setResolution(150);
        pdf.setTitle(u"Exceptions %1"_s.arg(date));
        pdf.setCreator(u"ViewTouch"_s);
        QPainter p;
        if (p.begin(&pdf)) {
            const int margin = 60;
            const int pageW = pdf.width() - 2 * margin, bottom = pdf.height() - margin;
            QFont title(u"DejaVu Sans"_s, 14);
            title.setBold(true);
            p.setFont(title);
            const int titleH = QFontMetrics(title, &pdf).height() + 12;
            p.drawText(QRect(margin, margin, pageW, titleH), Qt::AlignLeft | Qt::AlignVCenter,
                       QString::fromStdString(settings.storeName) + u" · "_s
                           + u"Exceptions %1 (day %2), %3 tickets"_s.arg(date).arg(dayId).arg(pictures.size() - 1));
            const int gap = 24, across = 3;
            const int colW = (pageW - gap * (across - 1)) / across;
            int x = 0, y = margin + titleH, rowH = 0;
            for (const QImage &img : pictures) {
                const double scale = double(colW) / img.width();
                int h = int(img.height() * scale);
                // A ticket taller than the page: shrunk to fit.
                const int room = bottom - (margin + titleH);
                int w = colW;
                if (h > room) {
                    w = int(w * double(room) / h);
                    h = room;
                }
                if (x >= across) {
                    x = 0;
                    y += rowH + gap;
                    rowH = 0;
                }
                if (y + h > bottom) {
                    pdf.newPage();
                    x = 0;
                    y = margin;
                    rowH = 0;
                }
                p.drawImage(QRect(margin + x * (colW + gap), y, w, h), img);
                rowH = std::max(rowH, h);
                ++x;
            }
            p.end();
        }
    }
    return dir;
}

QString TicketPrinter::dayFolder(std::int64_t dayId, std::int64_t dayOpenedAt) const
{
    QString base = exceptionsDir_;
    if (base.isEmpty())
        base = qEnvironmentVariable("VT_REPORTS_DIR");
    if (base.isEmpty())
        base = QDir::home().exists(u"Desktop"_s) ? QDir::home().filePath(u"Desktop/Daily Reports"_s)
                                                 : QDir::home().filePath(u"Daily Reports"_s);
    const QString date = QDateTime::fromMSecsSinceEpoch(dayOpenedAt ? dayOpenedAt : now_()).toString(u"yyyy-MM-dd"_s);
    return QDir(base).filePath(u"%1 day %2"_s.arg(date).arg(dayId));
}

QString TicketPrinter::saveEndOfDay(const PosSettings &settings, const std::vector<Report> &summary,
                                    const Report &exceptions, const std::vector<ExceptionTicket> &tickets,
                                    std::int64_t dayId, std::int64_t dayOpenedAt)
{
    const QString dir = saveExceptionTickets(settings, exceptions, tickets, dayId, dayOpenedAt);
    if (dir.isEmpty())
        return {};
    QStringList pictures;
    for (const ExceptionTicket &t : tickets)
        for (const char *ext : {".jpg", ".png"})
            if (const QString f = QDir(dir).filePath(QString::fromStdString(t.file) + QLatin1StringView(ext)); QFile::exists(f)) {
                pictures << f;
                break;
            }
    const QString date = QDateTime::fromMSecsSinceEpoch(dayOpenedAt ? dayOpenedAt : now_()).toString(u"yyyy-MM-dd"_s);
    const QString heading = QString::fromStdString(settings.storeName) + u" · End of day %1 (day %2)"_s.arg(date).arg(dayId);
    writeDaySummaryPdf(QDir(dir).filePath(u"EOD Summary %1.pdf"_s.arg(date)), heading, summary, pictures);
    return dir;
}

bool TicketPrinter::printTestPage(const PosSettings &settings, const std::string &printerId, bool kickDrawer)
{
    const auto it = std::ranges::find_if(settings.printers, [&](const PrinterConfig &p) { return p.id == printerId; });
    if (it == settings.printers.end())
        return false;
    const PrinterConfig &p = *it;
    const TicketContext ctx = context(settings);
    const int width = std::max(16, p.width);
    Document d;
    if (p.effectiveFormat() == "escpos")
        d.image(logoFor(settings, width));   // nothing when no logo is set
    d.text(settings.storeName, Document::Align::Center, true, true);
    d.text(ctx.tr("Printer test: %1", {p.name}), Document::Align::Center, true);
    d.text(QDateTime::fromMSecsSinceEpoch(now_ ? now_() : QDateTime::currentMSecsSinceEpoch())
               .toString(u"yyyy-MM-dd hh:mm"_s).toStdString(), Document::Align::Center);
    d.rule();
    d.text(ctx.tr("Normal text"));
    d.text(ctx.tr("Bold text"), Document::Align::Left, true);
    d.text(ctx.tr("Big text"), Document::Align::Left, true, true);
    d.text(ctx.tr("Right"), Document::Align::Right);
    d.columns("2 x Bacon Burger", ctx.money(Money::fromCents(2650)));
    d.columns("    Medium Rare", "");
    d.columns(ctx.tr("Total"), ctx.money(Money::fromCents(2869)), true);
    d.rule();
    // Every column: the paper width setting is right when this row exactly fills a line.
    std::string ruler;
    for (int i = 1; i <= width; ++i)
        ruler += char('0' + i % 10);
    d.text(ruler);
    d.text(ctx.tr("%1 characters per line: the row above should fill exactly one line.", {std::to_string(width)}));
    // Accents (Letters: accented, PC858), else plain letters.
    d.text(p.charset == "ascii" ? "Cafe, nino, senor (plain letters)" : "Café, niño, señor, ¿Qué tal? £5 €5");
    d.blank();
    d.cut = p.cutter;
    d.kickDrawer = kickDrawer && p.drawerKick;
    send(settings, p, d, u"Test page"_s);
    return true;
}

} // namespace vt::print
