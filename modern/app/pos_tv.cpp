// TV buttons (the pages' "directv" command; owner at HCA 2026-11-21: they did nothing on the tablets).
// The store's main computer works the TVs with /usr/local/bin/vt-directv (scripts/bin/vt-directv), so
// every screen's button comes here: the Pi's own screen directly, a tablet over the network. The
// program gets its arguments as a list (never a shell), runs in the background (a receiver that doesn't
// answer mustn't hold anything up) and is killed after 10 s; its one-line answer goes back to the
// screen that asked, as a notice (its status line).

#include "app/i18n.hh"
#include "app/pos_service.hh"

#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace vt::app {

QString PosService::directvProgram()
{
    const QString other = qEnvironmentVariable("VT_DIRECTV_LAUNCHER");
    return other.isEmpty() ? u"/usr/local/bin/vt-directv"_s : other;
}

bool PosService::directvArgsOk(const QString &box, const QString &op, const QString &val)
{
    // The same rules vt-directv applies (a box is a name from tvs.json, or "all").
    static const QRegularExpression word(u"^[A-Za-z0-9_]{1,24}$"_s);
    return (op == u"tune" || op == u"key") && word.match(box).hasMatch() && word.match(val).hasMatch();
}

bool PosService::directv(const QString &box, const QString &op, const QString &val)
{
    // As on the Pi's own screen: anyone signed in.
    if (!user())
        return fail(tr("Log in first."));
    if (!directvArgsOk(box, op, val))
        return fail(tr("This TV button isn't set up right (box, tune or key, and a channel or key)."));
    const QString program = directvProgram();
    if (!QFile::exists(program))
        return fail(tr("TV control isn't set up on the main computer (vt-directv)."));
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int, QProcess::ExitStatus) {
        const i18n::Scope language([this] { return this->language(); });
        QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        out.replace(u'\n', u" | "_s);
        if (out.isEmpty())
            out = proc->property("vtTimedOut").toBool() ? tr("The TV didn't answer in time.") : tr("DIRECTV command sent.");
        proc->deleteLater();
        emit notice(out);
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        const i18n::Scope language([this] { return this->language(); });
        proc->deleteLater();
        emit notice(tr("Couldn't start TV control (vt-directv) on the main computer."));
    });
    QTimer::singleShot(kDirectvTimeoutMs, proc, [proc] {
        proc->setProperty("vtTimedOut", true);
        proc->kill();
    });
    proc->start(program, {box, op, val});
    return true;
}

} // namespace vt::app
