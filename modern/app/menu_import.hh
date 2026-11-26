#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif


#include <QList>
#include <QString>
#include <QStringList>

namespace vt::app {

// One menu item read from a spreadsheet saved as CSV (or a tab- or
// semicolon-separated file).
struct ImportedItem {
    int row = 0;               // its line in the file (1 = the first)
    QString name;
    double price = 0;
    QString category;          // empty: the one chosen
    QString description;
    QString onIt;              // what's on it, "lettuce, tomato"
    QString photo;             // a picture's file name, next to the spreadsheet
    QString barcode;           // the package's barcode(s), "012345678905"
    QString moreBarcodes;      // other barcodes for the same item ("More UPCs")
    QString cost;              // the store's cost, "1.85"; empty: none given
    QString number;            // its number for ringing it in ("Item #")
    QString tax;               // Alcohol, Food, Merchandise / Retail, None
    QString maker;             // the brand
    QString productGroup;
    QString subGroup;
    QString printer;           // "Prints to": Bar, Kitchen, or blank (none)
    QString length;            // a cigar's length: "6 1/8", "6.125"
    QString ringGauge;         // "52"
    QString ringGauge2;        // a diadema's second gauge: "60"
    QString abv;               // a drink's alcohol by volume, percent: "5", "40" ("ABV %"); empty: none
    QString pourOz;            // its pour in ounces: "1.25", "12" ("Pour oz"); empty: none
};

struct MenuImport {
    QList<ImportedItem> items;
    QStringList problems;      // "Row 7: no price for Nachos"
    QStringList columns;       // what each column was taken to be
};

// Columns by their header (Name / Item, Price, Category / Section / Type,
// Description, Ingredients / On it, Photo, UPC, More UPCs, Cost, Item #, Tax, Maker,
// Product group, Sub group, Prints to, ABV / ABV % / Alcohol %, Pour oz / Pour / Serving oz; English or
// Spanish), else name, price,
// category in that order. "Cost" is the store's cost, never the price.
// Prices: "$3.50", "3,50", "3.50".
MenuImport readMenuCsv(const QString &text);

// A spreadsheet saved as CSV (or tab- or semicolon-separated: told from its first line), cell by cell; and a
// money cell ("$24.99", "24,99", "1,250.00") as a number (false: not one).
QList<QStringList> readCsvRows(const QString &text);
bool readCsvMoney(const QString &text, double *out);

} // namespace vt::app
