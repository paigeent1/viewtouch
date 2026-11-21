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

bool PosService::directvPrivateIp(const QString &ip)
{
    // Dotted, four numbers 0-255 without leading zeros, on a store network: 10/8, 172.16/12, 192.168/16.
    static const QRegularExpression dotted(u"^(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})$"_s);
    const QRegularExpressionMatch m = dotted.match(ip);
    if (!m.hasMatch())
        return false;
    int n[4];
    for (int i = 0; i < 4; ++i) {
        n[i] = m.captured(i + 1).toInt();
        if (n[i] > 255)
            return false;
    }
    return n[0] == 10 || (n[0] == 172 && n[1] >= 16 && n[1] <= 31) || (n[0] == 192 && n[1] == 168);
}

QString PosService::directvTvProblem(const QString &box, const QString &ip, const QString &kind)
{
    if (!kind.isEmpty() && kind != u"directv" && kind != u"roku" && kind != u"samsung")
        return tr("This TV button's TV type isn't one ViewTouch knows (DIRECTV receiver, Samsung TV or Roku TV).");
    if (ip.isEmpty())
        return {};
    if (!directvPrivateIp(ip))
        return tr("This TV button's IP address isn't a store network address (like 192.168.6.77).");
    if (box == u"all")
        return tr("A button for every box can't have one TV's IP address.");
    return {};
}

bool PosService::directv(const QString &box, const QString &op, const QString &val, const QString &ip,
                         const QString &kind)
{
    // As on the Pi's own screen: anyone signed in.
    if (!user())
        return fail(tr("Log in first."));
    if (!directvArgsOk(box, op, val))
        return fail(tr("This TV button isn't set up right (box, tune or key, and a channel or key)."));
    // The button's own address and type (owner at HCA 2026-11-21: "ip settings should be part of the
    // button properties"); none: vt-directv finds the TV by name in the Pi's TV list, as before.
    if (const QString problem = directvTvProblem(box, ip, kind); !problem.isEmpty())
        return fail(problem);
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
    QStringList args;
    if (!ip.isEmpty())
        args << u"--ip"_s << ip;
    if (!kind.isEmpty())
        args << u"--kind"_s << kind;
    args << box << op << val;
    proc->start(program, args);
    return true;
}

} // namespace vt::app
