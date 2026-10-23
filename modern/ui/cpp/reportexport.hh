#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif


#include <QString>
#include <QVariantMap>

// Saving a report as shown on the Reports screen ({title, subtitle,
// columns, rows: [{kind, cells}]}) into `dir`, named after the report and
// the time. Returns the file written, or empty with `error` set.
QString exportReportCsv(const QVariantMap &report, const QString &dir, QString *error = nullptr);
QString exportReportPdf(const QVariantMap &report, const QString &dir, QString *error = nullptr);
