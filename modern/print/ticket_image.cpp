#include "print/ticket_image.hh"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QString>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace vt::print {

QImage ticketImage(const Document &doc, int widthChars)
{
    const int width = std::max(16, widthChars);
    QFont font(u"DejaVu Sans Mono"_s);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPixelSize(22);
    QFont bigFont = font;
    bigFont.setPixelSize(44);
    const QFontMetrics fm(font);
    const int cw = std::max(1, fm.horizontalAdvance(u'M'));
    const int lh = fm.height() + 2;

    struct Row {
        QString text;
        bool bold = false;
        bool big = false;
    };
    std::vector<Row> rows;
    for (const Document::Line &l : doc.lines) {
        if (l.kind == Document::Kind::Image)
            continue;
        // The printer's own layout, one line at a time (wrapping, columns, rules).
        Document one;
        one.cut = false;
        one.lines.push_back(l);
        const QStringList out = QString::fromStdString(renderText(one, width)).split(u'\n');
        for (qsizetype i = 0; i + 1 < out.size(); ++i)   // the last is after the final newline
            rows.push_back({out[i], l.bold, l.big});
    }
    if (doc.kickDrawer)
        rows.push_back({u"[drawer opened]"_s, false, false});

    const int pad = 28;
    int height = 2 * pad;
    for (const Row &r : rows)
        height += r.big ? 2 * lh : lh;
    QImage img(cw * width + 2 * pad, std::max(height, 2 * pad + lh), QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setPen(Qt::black);
    int y = pad;
    for (const Row &r : rows) {
        QFont f = r.big ? bigFont : font;
        f.setBold(r.bold || r.big);
        p.setFont(f);
        const int h = r.big ? 2 * lh : lh;
        // Character by character on the paper's grid, so columns line up whatever the font.
        const int step = r.big ? 2 * cw : cw;
        int x = pad;
        for (const QChar ch : r.text) {
            if (ch != u' ')
                p.drawText(QRect(x, y, step, h), Qt::AlignHCenter | Qt::AlignVCenter, QString(ch));
            x += step;
        }
        y += h;
    }
    // A torn edge's hint: a light rule top and bottom.
    p.setPen(QColor(0xcc, 0xcc, 0xcc));
    p.drawRect(0, 0, img.width() - 1, img.height() - 1);
    p.end();
    return img;
}

} // namespace vt::print
