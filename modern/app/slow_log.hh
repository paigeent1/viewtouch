#pragma once

// Slow steps, seen at the store (owner 2026-11-26: a beer's price took 5-15 s to save in Menu Builder at HCA).
// A step is timed with its parts; when it takes longer than the limit (500 ms; VT_SLOW_MS=<ms> changes it) it
// writes one line to the app's log (stderr: ~/.vt-run.log on the Pi):
//   slow: save item 'TEST ALE 16oz' 4200 ms (refresh 4100, undo 30, store 50)
// Cheap: a clock read per part. The last line of each kind is kept for the tests (SlowLog::last).

#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <utility>
#include <vector>

namespace vt::app {

class SlowLog {
public:
    SlowLog(QString kind, QString what)
        : kind_(std::move(kind)), what_(std::move(what)), outer_(current())
    {
        timer_.start();
        current() = this;
    }
    ~SlowLog()
    {
        mark("other");
        current() = outer_;
        const QString line = text();
        lastLines()[kind_] = line;
        if (timer_.elapsed() >= limitMs())
            qWarning().noquote() << line;
    }
    SlowLog(const SlowLog &) = delete;
    SlowLog &operator=(const SlowLog &) = delete;

    // The time since the last mark (or the start) goes to `part`.
    void mark(const QString &part)
    {
        const qint64 now = timer_.nsecsElapsed();
        add(part, now - marked_);
        marked_ = now;
    }
    // Time measured elsewhere (inside the time since the last mark: taken out of the next mark's part).
    void add(const QString &part, qint64 nsecs)
    {
        if (nsecs <= 0)
            return;
        const auto it = std::ranges::find(parts_, part, &std::pair<QString, qint64>::first);
        if (it != parts_.end())
            it->second += nsecs;
        else
            parts_.emplace_back(part, nsecs);
    }
    void addInside(const QString &part, qint64 nsecs)
    {
        add(part, nsecs);
        marked_ += nsecs;
    }
    qint64 elapsedMs() const { return timer_.elapsed(); }
    void setWhat(const QString &what) { what_ = what; }

    // "slow: <what> <ms> ms (<part> <ms>, ...)": the biggest parts first (at most 8; under 1 ms left out).
    QString text() const
    {
        std::vector<std::pair<QString, qint64>> parts = parts_;
        std::ranges::stable_sort(parts, [](const auto &a, const auto &b) { return a.second > b.second; });
        QStringList shown;
        for (const auto &[name, ns] : parts)
            if (ns >= 1'000'000 && shown.size() < 8)
                shown << QStringLiteral("%1 %2").arg(name).arg(ns / 1'000'000);
        return QStringLiteral("slow: %1 %2 ms").arg(what_).arg(timer_.elapsed())
               + (shown.isEmpty() ? QString() : QStringLiteral(" (") + shown.join(QStringLiteral(", ")) + u')');
    }

    // The innermost step being timed (nullptr: none).
    static SlowLog *&current()
    {
        thread_local SlowLog *now = nullptr;
        return now;
    }
    // Marks a part of the step being timed, if one is.
    static void markCurrent(const QString &part)
    {
        if (SlowLog *s = current())
            s->mark(part);
    }
    static qint64 limitMs()
    {
        static const qint64 limit = [] {
            bool ok = false;
            const qint64 v = qEnvironmentVariableIntValue("VT_SLOW_MS", &ok);
            return ok && v >= 0 ? v : qint64(500);
        }();
        return limit;
    }
    // The last line of a kind ("save", "round", "call", "refresh"), slow or not: for the tests.
    static QString last(const QString &kind) { return lastLines().value(kind); }

private:
    static QHash<QString, QString> &lastLines()
    {
        static QHash<QString, QString> lines;
        return lines;
    }
    QString kind_, what_;
    SlowLog *outer_;
    QElapsedTimer timer_;
    qint64 marked_ = 0;
    std::vector<std::pair<QString, qint64>> parts_;
};

} // namespace vt::app
