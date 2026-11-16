#include "app/menu_import.hh"

#include <QCoreApplication>
#include <QRegularExpression>
#include <cmath>
#include <tuple>

using namespace Qt::StringLiterals;

namespace vt::app {

namespace {

// The cells of one line; quotes may hold the separator and doubled quotes.
QList<QStringList> rowsOf(const QString &text, QChar sep)
{
    QList<QStringList> rows;
    QStringList row;
    QString cell;
    bool quoted = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == u'"' && i + 1 < text.size() && text[i + 1] == u'"') {
                cell += u'"';
                ++i;
            } else if (c == u'"') {
                quoted = false;
            } else {
                cell += c;
            }
        } else if (c == u'"') {
            quoted = true;
        } else if (c == sep) {
            row << cell.trimmed();
            cell.clear();
        } else if (c == u'\n' || c == u'\r') {
            if (c == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n')
                ++i;
            row << cell.trimmed();
            rows << row;
            row.clear();
            cell.clear();
        } else {
            cell += c;
        }
    }
    if (!cell.isEmpty() || !row.isEmpty()) {
        row << cell.trimmed();
        rows << row;
    }
    return rows;
}

QString what(const QString &header)
{
    QString h = header.trimmed().toLower().replace(u'#', u" number "_s);   // "Item #"
    h = h.normalized(QString::NormalizationForm_KD).remove(QRegularExpression(u"[^a-z ]"_s)).simplified();
    static const QList<std::pair<QString, QStringList>> names = {
        {u"name"_s, {u"name"_s, u"item"_s, u"item name"_s, u"product"_s, u"dish"_s, u"menu item"_s, u"nombre"_s, u"producto"_s, u"platillo"_s}},
        {u"price"_s, {u"price"_s, u"retail"_s, u"retail price"_s, u"sell price"_s, u"amount"_s, u"precio"_s}},
        // The store's cost: what the item cost it (never its price).
        {u"cost"_s, {u"cost"_s, u"my cost"_s, u"unit cost"_s, u"costo"_s}},
        {u"category"_s, {u"category"_s, u"categories"_s, u"family"_s, u"group"_s, u"section"_s, u"menu"_s, u"type"_s,
                         u"department"_s, u"categoria"_s, u"seccion"_s, u"grupo"_s, u"tipo"_s}},
        {u"moreBarcodes"_s, {u"more upcs"_s, u"more upc"_s, u"alt upc"_s, u"alt upcs"_s, u"other upcs"_s, u"more barcodes"_s}},
        {u"number"_s, {u"item number"_s, u"number"_s, u"plu"_s, u"item no"_s}},
        {u"tax"_s, {u"tax"_s, u"tax class"_s, u"taxed"_s, u"impuesto"_s}},
        {u"maker"_s, {u"maker"_s, u"manufacturer"_s, u"brand"_s, u"mfg"_s, u"marca"_s}},
        {u"productGroup"_s, {u"product group"_s}},
        {u"subGroup"_s, {u"sub group"_s, u"subgroup"_s}},
        {u"printer"_s, {u"prints to"_s, u"printer"_s}},
        {u"length"_s, {u"length"_s, u"len"_s, u"length in"_s, u"length inches"_s}},
        {u"ringGauge"_s, {u"ring gauge"_s, u"ring gauge 1"_s, u"ring gauge1"_s, u"rg"_s, u"rg 1"_s, u"rg1"_s, u"ring"_s, u"gauge"_s}},
        {u"ringGauge2"_s, {u"ring gauge 2"_s, u"ring gauge2"_s, u"rg 2"_s, u"rg2"_s}},
        {u"description"_s, {u"description"_s, u"desc"_s, u"details"_s, u"descripcion"_s}},
        {u"onIt"_s, {u"ingredients"_s, u"on it"_s, u"toppings"_s, u"whats on it"_s, u"comes with"_s, u"ingredientes"_s}},
        {u"photo"_s, {u"photo"_s, u"photos"_s, u"picture"_s, u"image"_s, u"photo file"_s, u"picture file"_s, u"image file"_s,
                      u"foto"_s, u"imagen"_s, u"fotografia"_s}},
        // A drink: "ABV %", "Alcohol %" (the % is dropped above) and "Pour oz", "Serving oz".
        {u"abv"_s, {u"abv"_s, u"alcohol"_s, u"abv percent"_s, u"alcohol percent"_s, u"alcohol by volume"_s}},
        {u"pourOz"_s, {u"pour oz"_s, u"pour"_s, u"serving oz"_s, u"pour ounces"_s, u"pour size"_s}},
        {u"barcode"_s, {u"barcode"_s, u"barcodes"_s, u"upc"_s, u"ean"_s, u"gtin"_s, u"codigo de barras"_s}},
    };
    for (const auto &[key, words] : names)
        if (words.contains(h))
            return key;
    return {};
}

// Above this, not a price (in cents near the limit of whole numbers, a
// few of it would overflow the totals). (Upstream fc15f44f.)
constexpr double kMaxPrice = 10'000'000;

bool price(QString text, double *out)
{
    text = text.trimmed();
    text.remove(QRegularExpression(u"[^0-9.,]"_s));
    if (text.isEmpty())
        return false;
    // "3,50": a comma for the cents; "1,250.00": a thousands comma.
    if (!text.contains(u'.') && text.count(u',') == 1 && text.section(u',', 1).size() <= 2)
        text.replace(u',', u'.');
    else
        text.remove(u',');
    bool ok = false;
    *out = text.toDouble(&ok);
    return ok && std::isfinite(*out) && *out >= 0 && *out < kMaxPrice;   // 99999999999999 isn't a price
}

} // namespace

MenuImport readMenuCsv(const QString &text)
{
    MenuImport out;
    const QString first = text.section(u'\n', 0, 0);
    const QChar sep = first.count(u'\t') > 0 ? u'\t'
                      : first.count(u';') > first.count(u',') ? u';' : u',';
    QList<QStringList> rows = rowsOf(text, sep);
    // Which column is which: the header, else name, price, category.
    QHash<QString, int> at{{u"name"_s, 0}, {u"price"_s, 1}, {u"category"_s, 2}};
    int firstRow = 0;
    if (!rows.isEmpty()) {
        QHash<QString, int> found;
        for (int i = 0; i < rows[0].size(); ++i)
            if (const QString w = what(rows[0][i]); !w.isEmpty() && !found.contains(w))
                found.insert(w, i);
        if (found.contains(u"name"_s)) {
            at = found;
            firstRow = 1;
        }
    }
    for (const QString &key : {u"name"_s, u"price"_s, u"category"_s, u"description"_s, u"onIt"_s, u"photo"_s, u"barcode"_s,
                               u"moreBarcodes"_s, u"cost"_s, u"number"_s, u"tax"_s, u"maker"_s, u"productGroup"_s,
                               u"subGroup"_s, u"printer"_s, u"length"_s, u"ringGauge"_s, u"ringGauge2"_s,
                               u"abv"_s, u"pourOz"_s})
        if (at.contains(key))
            out.columns << key;
    const auto cell = [&](const QStringList &r, const QString &key) {
        const int i = at.value(key, -1);
        return i >= 0 && i < r.size() ? r[i].trimmed() : QString();
    };
    for (int r = firstRow; r < rows.size(); ++r) {
        const QStringList &row = rows[r];
        if (std::all_of(row.begin(), row.end(), [](const QString &c) { return c.trimmed().isEmpty(); }))
            continue;
        ImportedItem item;
        item.row = r + 1;
        item.name = cell(row, u"name"_s);
        item.category = cell(row, u"category"_s);
        item.description = cell(row, u"description"_s);
        item.onIt = cell(row, u"onIt"_s);
        item.photo = cell(row, u"photo"_s);
        item.barcode = cell(row, u"barcode"_s);
        item.moreBarcodes = cell(row, u"moreBarcodes"_s);
        item.number = cell(row, u"number"_s);
        item.tax = cell(row, u"tax"_s);
        item.maker = cell(row, u"maker"_s);
        item.productGroup = cell(row, u"productGroup"_s);
        item.subGroup = cell(row, u"subGroup"_s);
        item.printer = cell(row, u"printer"_s);
        item.length = cell(row, u"length"_s);
        item.ringGauge = cell(row, u"ringGauge"_s);
        item.ringGauge2 = cell(row, u"ringGauge2"_s);
        // A drink's ABV and pour: numbers ("40%", "1.25 oz" are fine); anything else is a problem.
        bool bad = false;
        for (const auto &[key, field, top] : {std::tuple{u"abv"_s, &ImportedItem::abv, 100.0},
                                              {u"pourOz"_s, &ImportedItem::pourOz, 1000.0}}) {
            QString c = cell(row, key);
            c.remove(u'%');
            if (c.endsWith(u"oz"_s, Qt::CaseInsensitive))
                c.chop(2);
            c = c.trimmed();
            if (c.isEmpty())
                continue;
            bool ok = false;
            const double v = c.toDouble(&ok);
            if (!ok || !std::isfinite(v) || v < 0 || v > top) {
                out.problems << (key == u"abv"_s
                                     ? QCoreApplication::translate("MenuImport", "Row %1: the ABV of %2 isn't a percent").arg(item.row).arg(item.name)
                                     : QCoreApplication::translate("MenuImport", "Row %1: the pour of %2 isn't ounces").arg(item.row).arg(item.name));
                bad = true;
                break;
            }
            item.*field = v > 0 ? QString::number(v, 'g', 6) : QString();
        }
        if (bad)
            continue;
        if (const QString c = cell(row, u"cost"_s); !c.isEmpty()) {
            double v = 0;
            if (!price(c, &v)) {
                out.problems << QCoreApplication::translate("MenuImport", "Row %1: the cost of %2 isn't a number").arg(item.row).arg(item.name);
                continue;
            }
            item.cost = QString::number(v, 'f', 2);
        }
        if (item.name.isEmpty()) {
            out.problems << QCoreApplication::translate("MenuImport", "Row %1: no name").arg(item.row);
            continue;
        }
        if (!price(cell(row, u"price"_s), &item.price)) {
            out.problems << QCoreApplication::translate("MenuImport", "Row %1: no price for %2").arg(item.row).arg(item.name);
            continue;
        }
        out.items << item;
    }
    return out;
}

} // namespace vt::app
