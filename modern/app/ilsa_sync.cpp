// The ILSA price book's automatic sync on the Pi (see ilsa_sync.hh).
#include "app/ilsa_sync.hh"

#include "app/pos_service.hh"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QThreadPool>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace vt::app {

namespace {

constexpr int kEveryMs = 15 * 60 * 1000;
constexpr int kFirstMs = 60 * 1000;
constexpr int kSameBook = 3;   // vt-ilsa-pricebook --exit-status: the same export as last time

// A vt-* tool: installed in /usr/local/bin, or (one added since this Pi linked them) in the repo.
QString toolPath(const QString &name)
{
    const QString installed = u"/usr/local/bin/"_s + name;
    if (QFile::exists(installed))
        return installed;
    const QString repo = QDir::home().filePath(u"viewtouch-main/scripts/bin/"_s + name);
    return QFile::exists(repo) ? repo : installed;
}

// A tool run to its end (on the worker thread): its exit code, -1 if it didn't run or finish.
int runTool(const QString &name, const QStringList &args, int timeoutMs, QString *output)
{
    const QString program = toolPath(name);
    if (!QFile::exists(program)) {
        *output = QObject::tr("%1 isn't installed on this computer.").arg(name);
        return -1;
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(u"python3"_s, QStringList{program} + args);
    if (!p.waitForStarted(10'000)) {
        *output = QObject::tr("%1 didn't start.").arg(name);
        return -1;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(5'000);
        *output = QObject::tr("%1 took too long.").arg(name);
        return -1;
    }
    *output = QString::fromUtf8(p.readAll());
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

QString lastLines(const QString &text, int n)
{
    QStringList lines = text.trimmed().split(u'\n', Qt::SkipEmptyParts);
    return lines.mid(std::max<qsizetype>(0, lines.size() - n)).join(u" / "_s);
}

QString firstLineOf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readLine()).trimmed() : QString();
}

// The price book now in ~/ilsa-pricebook/latest: its manifest's exported=...
QString exportedNow()
{
    QFile f(QDir::home().filePath(u"ilsa-pricebook/latest/manifest.txt"_s));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    for (const QByteArray &raw : f.readAll().split('\n'))
        if (const QString l = QString::fromUtf8(raw).trimmed(); l.startsWith(u"exported="_s))
            return l.mid(9);
    return {};
}

QString csv(QString v)
{
    v.replace(u'\n', u' ').replace(u'\r', u' ');
    if (v.contains(u',') || v.contains(u'"'))
        v = u'"' + v.replace(u"\""_s, u"\"\""_s) + u'"';
    return v;
}

bool append(const QString &path, const QStringList &lines, const QString &header = {})
{
    QFile f(path);
    const bool fresh = !f.exists() || f.size() == 0;
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append))
        return false;
    if (fresh && !header.isEmpty())
        f.write((header + u'\n').toUtf8());
    for (const QString &l : lines)
        f.write((l + u'\n').toUtf8());
    return true;
}

// The end of a file (it grows all year): its last `n` lines.
QStringList tail(const QString &path, int n)
{
    QFile f(path);
    if (n <= 0 || !f.open(QIODevice::ReadOnly))
        return {};
    const qint64 from = std::max<qint64>(0, f.size() - 64 * 1024);
    f.seek(from);
    QStringList lines = QString::fromUtf8(f.readAll()).split(u'\n', Qt::SkipEmptyParts);
    if (from > 0 && !lines.isEmpty())
        lines.removeFirst();   // cut in the middle
    return lines.mid(std::max<qsizetype>(0, lines.size() - n));
}

} // namespace

IlsaSync::IlsaSync(PosService *pos, QObject *parent)
    : QObject(parent), pos_(pos), logDir_(QDir::home().filePath(u"ilsa-sync"_s))
{
    timer_.setInterval(kEveryMs);
    connect(&timer_, &QTimer::timeout, this, [this] { run(false); });
}

void IlsaSync::start()
{
    // What the last run said, from before this start.
    if (const QStringList last = tail(QDir(logDir_).filePath(u"runs.log"_s), 1); !last.isEmpty()) {
        lastRun_ = QDateTime::fromString(last.first().left(16), u"yyyy-MM-dd HH:mm"_s);
        result_ = last.first().mid(18);
    }
    QTimer::singleShot(kFirstMs, this, [this] { run(false); });
    timer_.start();
}

bool IlsaSync::on() const
{
    return pos_ && pos_->shared()->settings.ilsaSyncOn;
}

void IlsaSync::setOn(bool on)
{
    if (!pos_ || pos_->shared()->settings.ilsaSyncOn == on)
        return;
    pos_->shared()->settings.ilsaSyncOn = on;
    pos_->shared()->saveSettings();
    emit changed();
}

bool IlsaSync::runNow()
{
    if (busy_)
        return false;
    run(true);
    return true;
}

QVariantMap IlsaSync::info() const
{
    return {{u"available"_s, true}, {u"on"_s, on()}, {u"busy"_s, busy_}, {u"step"_s, step_},
            {u"lastRun"_s, lastRun_.isValid() ? lastRun_.toMSecsSinceEpoch() : qint64(0)}, {u"result"_s, result_}};
}

QStringList IlsaSync::logTail(int lines) const
{
    const QDir dir(logDir_);
    QStringList out = tail(dir.filePath(u"runs.log"_s), 10);
    const QStringList changes = tail(dir.filePath(u"sync-log.csv"_s), std::max(0, lines - int(out.size())));
    if (!out.isEmpty() && !changes.isEmpty())
        out << QString();
    out << changes;
    return out;
}

void IlsaSync::run(bool byHand)
{
    if (busy_ || !pos_ || (!byHand && !on()))
        return;
    busy_ = true;
    step_ = tr("Getting the price book from Dropbox…");
    emit changed();
    QPointer<IlsaSync> self(this);
    const QString synced = firstLineOf(QDir(logDir_).filePath(u"last-synced.txt"_s));
    // The tools and the 30 MB file on a worker thread: the screen keeps going.
    QThreadPool::globalInstance()->start([self, synced, byHand] {
        QString out, error, exported;
        QVariantMap file;
        const int pulled = runTool(u"vt-ilsa-pricebook"_s, {u"--exit-status"_s}, 5 * 60 * 1000, &out);
        exported = exportedNow();
        if (pulled != 0 && pulled != kSameBook) {
            error = tr("Couldn't get the price book: %1").arg(lastLines(out, 2));
        } else if (exported.isEmpty() || exported == synced) {
            // Nothing new (a book already synced, even if someone pulled it by hand since).
        } else if (runTool(u"vt-ilsa-items"_s, {}, 10 * 60 * 1000, &out) != 0) {
            error = tr("Couldn't convert the price book: %1").arg(lastLines(out, 2));
        } else {
            QFile f(QDir::home().filePath(u"ilsa-items/viewtouch-menu.json"_s));
            QJsonParseError parse{};
            const QJsonDocument doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &parse) : QJsonDocument();
            file = doc.isObject() ? doc.object().toVariantMap() : QVariantMap();
            if (file.isEmpty())
                error = tr("Couldn't read ~/ilsa-items/viewtouch-menu.json.");
        }
        QMetaObject::invokeMethod(self, [self, file, exported, error, byHand] {
            if (!self)
                return;
            if (!error.isEmpty())
                self->finish(error, true);
            else if (file.isEmpty())
                self->finish(tr("No new price book."), false);
            else
                self->apply(file, exported, byHand);
        });
    });
}

void IlsaSync::apply(const QVariantMap &file, const QString &exported, bool byHand)
{
    if (!pos_ || (!byHand && !on())) {   // turned off meanwhile
        finish(tr("Off."), false);
        return;
    }
    step_ = tr("Updating the items…");
    emit changed();
    const QVariantMap result = pos_->syncPriceBook(file);
    if (!result.value(u"ok"_s).toBool()) {
        finish(result.value(u"error"_s).toString(), true);
        return;
    }
    QVariantMap withBook = result;
    withBook.insert(u"exported"_s, exported);
    const QString line = writeLogs(withBook, QDateTime::currentDateTime());
    QFile stamp(QDir(logDir_).filePath(u"last-synced.txt"_s));
    if (stamp.open(QIODevice::WriteOnly | QIODevice::Truncate))
        stamp.write((exported + u'\n').toUtf8());
    finish(line, false);   // writeLogs wrote its line
}

void IlsaSync::finish(const QString &result, bool logIt)
{
    busy_ = false;
    step_.clear();
    const bool again = result == result_;
    lastRun_ = QDateTime::currentDateTime();
    result_ = result;
    if (logIt && !again) {   // a problem: once, not every 15 minutes while it lasts
        QDir().mkpath(logDir_);
        append(QDir(logDir_).filePath(u"runs.log"_s), {lastRun_.toString(u"yyyy-MM-dd HH:mm"_s) + u"  "_s + result});
    }
    emit changed();
}

QString IlsaSync::writeLogs(const QVariantMap &result, const QDateTime &at)
{
    QDir().mkpath(logDir_);
    const QDir dir(logDir_);
    // Differences are written once (noted.txt holds today's); changes every time.
    static const QHash<QString, QString> what{
        {u"added"_s, u"added"_s}, {u"price"_s, u"price"_s}, {u"cost"_s, u"cost"_s}, {u"barcode"_s, u"barcode added"_s},
        {u"name"_s, u"ILSA name differs"_s}, {u"department"_s, u"department differs"_s}, {u"tax"_s, u"tax differs"_s},
        {u"gone"_s, u"gone from ILSA"_s}, {u"conflict"_s, u"ILSA items differ on price"_s},
        {u"skipped"_s, u"not added: no barcode of its own"_s}};
    QSet<QString> noted;
    {
        QFile f(dir.filePath(u"noted.txt"_s));
        if (f.open(QIODevice::ReadOnly))
            for (const QByteArray &l : f.readAll().split('\n'))
                if (!l.isEmpty())
                    noted.insert(QString::fromUtf8(l));
    }
    QSet<QString> nowNoted;
    QHash<QString, int> count;
    QStringList lines, priceLines;
    const QString time = at.toString(u"yyyy-MM-dd HH:mm"_s);
    for (const QVariant &v : result.value(u"rows"_s).toList()) {
        const QVariantMap r = v.toMap();
        const QString kind = r.value(u"kind"_s).toString();
        const bool change = kind == u"added" || kind == u"price" || kind == u"cost" || kind == u"barcode";
        if (!change) {
            const QString key = kind + u'\t' + r.value(u"id"_s).toString() + u'\t' + r.value(u"after"_s).toString();
            nowNoted.insert(key);
            if (noted.contains(key))
                continue;
        }
        ++count[kind];
        lines << QStringList{time, csv(r.value(u"item"_s).toString()), csv(r.value(u"upc"_s).toString()), what.value(kind, kind),
                             csv(r.value(u"before"_s).toString()), csv(r.value(u"after"_s).toString())}.join(u',');
        if (kind == u"price")
            priceLines << QStringList{csv(r.value(u"item"_s).toString()), csv(r.value(u"upc"_s).toString()),
                                      csv(r.value(u"before"_s).toString()), csv(r.value(u"after"_s).toString()),
                                      csv(r.value(u"id"_s).toString())}.join(u',');
    }
    append(dir.filePath(u"sync-log.csv"_s), lines, u"Time,Item,UPC,What,Before,After"_s);
    if (!priceLines.isEmpty())
        append(dir.filePath(u"price-changes-%1.csv"_s.arg(at.toString(u"yyyyMMdd-HHmm"_s))), priceLines,
               u"Item,UPC,Old price,New price,ViewTouch id"_s);
    QFile keep(dir.filePath(u"noted.txt"_s));
    if (keep.open(QIODevice::WriteOnly | QIODevice::Truncate))
        for (const QString &k : nowNoted)
            keep.write((k + u'\n').toUtf8());

    QStringList said;
    const auto part = [&](const char *kind, const QString &text) {
        if (const int n = count.value(QString::fromLatin1(kind)); n > 0)
            said << text.arg(n);
    };
    part("added", tr("%1 added"));
    part("price", tr("%1 prices changed"));
    part("cost", tr("%1 costs changed"));
    part("barcode", tr("%1 barcodes added"));
    part("name", tr("%1 names differ"));
    part("department", tr("%1 departments differ"));
    part("tax", tr("%1 taxes differ"));
    part("gone", tr("%1 gone from ILSA"));
    part("conflict", tr("%1 with ILSA prices that disagree"));
    part("skipped", tr("%1 not added (no barcode)"));
    const QString summary = said.isEmpty() ? tr("no changes") : said.join(u", "_s);
    append(dir.filePath(u"runs.log"_s), {u"%1  price book %2: %3 (%4 items)"_s.arg(time, result.value(u"exported"_s).toString(), summary)
                                             .arg(result.value(u"items"_s).toInt())});
    return summary;
}

} // namespace vt::app
