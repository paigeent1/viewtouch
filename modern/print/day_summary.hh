#pragma once

#include "core/report.hh"

#include <QImage>
#include <QIODevice>
#include <QString>

#include <vector>

namespace vt::print {

// The end-of-day summary as one PDF: a cover line (store, date, day), each
// report as a table (the way the Reports screen shows it), then the day's
// exception ticket pictures three across. Reports with nothing in them are
// left out. Written to `out` (the End of Day archive keeps the bytes). False: it couldn't be written.
bool writeDaySummaryPdf(QIODevice *out, const QString &heading, const std::vector<core::Report> &reports,
                        const std::vector<QImage> &ticketPictures);

} // namespace vt::print
