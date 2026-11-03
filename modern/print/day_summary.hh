#pragma once

#include "core/report.hh"

#include <QString>
#include <QStringList>

#include <vector>

namespace vt::print {

// The end-of-day summary as one PDF: a cover line (store, date, day), each
// report as a table (the way the Reports screen shows it), then the day's
// exception ticket pictures three across. Reports with nothing in them are
// left out. False: the file couldn't be written.
bool writeDaySummaryPdf(const QString &file, const QString &heading, const std::vector<core::Report> &reports,
                        const QStringList &ticketPictures);

} // namespace vt::print
