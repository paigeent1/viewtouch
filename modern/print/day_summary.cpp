#include "print/day_summary.hh"

#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace vt::print {

namespace {

bool hasContent(const core::Report &r)
{
    return std::ranges::any_of(r.rows, [](const core::ReportRow &row) { return row.kind != core::ReportRow::Kind::Note; });
}

QString q(const std::string &s) { return QString::fromStdString(s); }

} // namespace

bool writeDaySummaryPdf(const QString &file, const QString &heading, const std::vector<core::Report> &reports,
                        const QStringList &ticketPictures)
{
    QPdfWriter pdf(file);
    pdf.setPageSize(QPageSize(QPageSize::Letter));
    pdf.setResolution(150);
    pdf.setTitle(heading);
    pdf.setCreator(u"ViewTouch"_s);
    QPainter p;
    if (!p.begin(&pdf))
        return false;
    const int margin = 90;   // 0.6 in at 150 dpi
    const int width = pdf.width() - 2 * margin;
    const int bottom = pdf.height() - margin;
    QFont body(u"DejaVu Sans"_s, 9);
    QFont bold = body;
    bold.setBold(true);
    QFont title = bold;
    title.setPointSize(13);
    QFont cover = bold;
    cover.setPointSize(17);
    const int lineH = QFontMetrics(body, &pdf).height() + 5;
    int y = margin;
    int page = 1;
    const auto footer = [&] {
        p.setFont(body);
        p.drawText(QRect(margin, bottom, width, lineH), Qt::AlignRight | Qt::AlignVCenter,
                   heading + u" · page %1"_s.arg(page));
    };
    const auto newPage = [&] {
        footer();
        pdf.newPage();
        ++page;
        y = margin;
    };
    const auto room = [&](int h) {
        if (y + h > bottom)
            newPage();
    };

    p.setFont(cover);
    p.drawText(QRect(margin, y, width, lineH * 2), Qt::AlignLeft | Qt::AlignVCenter, heading);
    y += lineH * 2;
    p.setFont(body);
    p.drawText(QRect(margin, y, width, lineH), Qt::AlignLeft | Qt::AlignVCenter,
               u"Made %1"_s.arg(QDateTime::currentDateTime().toString(u"yyyy-MM-dd h:mm AP"_s)));
    y += lineH * 2;

    for (const core::Report &r : reports) {
        if (!hasContent(r))
            continue;
        room(lineH * 6);   // a title with nothing under it: on the next page
        p.setFont(title);
        p.drawText(QRect(margin, y, width, lineH * 2), Qt::AlignLeft | Qt::AlignVCenter, q(r.title));
        y += lineH * 2;
        if (!r.subtitle.empty()) {
            p.setFont(body);
            p.drawText(QRect(margin, y, width, lineH), Qt::AlignLeft | Qt::AlignVCenter, q(r.subtitle));
            y += lineH;
        }
        // Columns: the first takes what the others leave.
        std::size_t cols = r.columns.size();
        for (const core::ReportRow &row : r.rows)
            cols = std::max(cols, row.cells.size());
        const int numberCols = std::max(0, int(cols) - 1);
        const int numberW = numberCols > 0 ? std::min(width / 5, width / (numberCols + 2)) : 0;
        const auto drawRow = [&](const std::vector<std::string> &cells, const QFont &font) {
            room(lineH);
            p.setFont(font);
            const int firstW = width - numberW * std::max(0, int(cells.size()) - 1);
            int x = margin;
            for (std::size_t i = 0; i < cells.size(); ++i) {
                const int w = i == 0 ? firstW : numberW;
                const Qt::Alignment a = (i == 0 ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter;
                p.drawText(QRect(x, y, w, lineH), a, QFontMetrics(font, &pdf).elidedText(q(cells[i]), Qt::ElideRight, w - 10));
                x += w;
            }
            y += lineH;
        };
        if (!r.columns.empty()) {
            drawRow(r.columns, bold);
            p.drawLine(margin, y, margin + width, y);
            y += 4;
        }
        for (const core::ReportRow &row : r.rows) {
            if (row.kind == core::ReportRow::Kind::Section)
                y += lineH / 3;
            if (row.kind == core::ReportRow::Kind::Total) {
                p.drawLine(margin + width / 2, y, margin + width, y);
                y += 3;
            }
            const bool strong = row.kind == core::ReportRow::Kind::Section || row.kind == core::ReportRow::Kind::Total;
            drawRow(row.cells, strong ? bold : body);
        }
        y += lineH;
    }

    // The exception tickets, as printed (the store's copy).
    if (!ticketPictures.isEmpty()) {
        newPage();
        p.setFont(title);
        p.drawText(QRect(margin, y, width, lineH * 2), Qt::AlignLeft | Qt::AlignVCenter,
                   u"Exception tickets (%1)"_s.arg(ticketPictures.size()));
        y += lineH * 2;
        const int gap = 24, across = 3;
        const int colW = (width - gap * (across - 1)) / across;
        int x = 0, rowH = 0;
        for (const QString &path : ticketPictures) {
            const QImage img(path);
            if (img.isNull())
                continue;
            int w = colW, h = int(img.height() * double(colW) / img.width());
            const int full = bottom - margin;
            if (h > full) {   // taller than a page: shrunk to fit
                w = int(w * double(full) / h);
                h = full;
            }
            if (x >= across) {
                x = 0;
                y += rowH + gap;
                rowH = 0;
            }
            if (y + h > bottom) {
                newPage();
                x = 0;
                rowH = 0;
            }
            p.drawImage(QRect(margin + x * (colW + gap), y, w, h), img);
            rowH = std::max(rowH, h);
            ++x;
        }
    }
    footer();
    p.end();
    return true;
}

} // namespace vt::print
