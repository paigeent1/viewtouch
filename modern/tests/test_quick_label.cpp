#include <catch2/catch_test_macros.hpp>
#include "layout_fixture.hh"
#include "layoutcontroller.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"
#include <QCoreApplication>
#include <algorithm>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Quick Label (owner 2026-11-20): typed text on a label from any screen, printed by the label utility on the
// main computer (tools/vt_label_utility.py --quick-rolls / --quick-preview / --quick-job). Here a stand-in
// for the utility (VT_LABEL_LAUNCHER) keeps the job files it's given and answers as the utility does.

namespace {

template <typename F>
bool waitFor(F condition, int msec = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!condition() && t.elapsed() < msec) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

// A home folder with two label templates, and the stand-in label utility (restored when it goes).
struct QuickLabelBench {
    QTemporaryDir dir;
    QByteArray oldHome = qgetenv("HOME");
    QByteArray oldLauncher = qgetenv("VT_LABEL_LAUNCHER");

    QuickLabelBench()
    {
        REQUIRE(dir.isValid());
        qputenv("HOME", dir.path().toUtf8());
        const QString share = dir.path() + u"/.local/share/ViewTouch"_s;
        QDir().mkpath(share);
        QFile t(share + u"/label_templates.json"_s);
        REQUIRE(t.open(QIODevice::WriteOnly));
        t.write(QJsonDocument(QJsonObject{
                    {u"2X1 VINYL"_s, QJsonObject{{u"width_in"_s, 2.0}, {u"height_in"_s, 1.0}, {u"elements"_s, QJsonArray{}}}},
                    {u"CIGAR STICK"_s, QJsonObject{{u"width_in"_s, 2.0}, {u"height_in"_s, 0.56}, {u"elements"_s, QJsonArray{}}}}})
                    .toJson());
        t.close();
        // Keeps each job file (as print-job.json / preview-job.json) and its arguments; FAILME: the printer fails.
        const QString d = dir.path();
        QFile f(launcher());
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QString::fromUtf8(
                    "#!/bin/sh\n"
                    "echo \"$@\" >> '%1/calls.txt'\n"
                    "case \"$1\" in\n"
                    "  --quick-rolls) printf '%s' '{\"rolls\":[{\"key\":\"2X1 VINYL\",\"name\":\"2 x 1 in\",\"size\":\"2 x 1 in\",\"printer\":\"Zebra Large Labels\"},"
                    "{\"key\":\"CIGAR STICK\",\"name\":\"2 x 0.56 in\",\"size\":\"2 x 0.56 in\",\"printer\":\"Stick Labels\"}],\"last\":\"CIGAR STICK\"}' > \"$3\" ;;\n"
                    "  --quick-preview) cp \"$2\" '%1/preview-job.json'; printf 'PNG' > \"$4\" ;;\n"
                    "  --quick-job) cp \"$2\" '%1/print-job.json'\n"
                    "    if grep -q FAILME \"$2\"; then\n"
                    "      printf '%s' '{\"ok\":false,\"message\":\"Could not print on Zebra Large Labels: timed out\",\"printer\":\"Zebra Large Labels\",\"copies\":0}' > \"$4\"; exit 1\n"
                    "    fi\n"
                    "    printf '%s' '{\"ok\":true,\"message\":\"Printed\",\"printer\":\"Zebra Large Labels\",\"copies\":2}' > \"$4\" ;;\n"
                    "esac\n")
                    .arg(d)
                    .toUtf8());
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv("VT_LABEL_LAUNCHER", launcher().toUtf8());
    }
    ~QuickLabelBench()
    {
        qputenv("HOME", oldHome);
        if (oldLauncher.isEmpty())
            qunsetenv("VT_LABEL_LAUNCHER");
        else
            qputenv("VT_LABEL_LAUNCHER", oldLauncher);
    }
    QString launcher() const { return dir.path() + u"/fake-label-utility"_s; }
    QJsonObject job(const QString &name) const
    {
        QFile f(dir.path() + u'/' + name);
        return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
    }
    QStringList calls() const
    {
        QFile f(dir.path() + u"/calls.txt"_s);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).split(u'\n', Qt::SkipEmptyParts) : QStringList();
    }
};

QStringList jobLines(const QJsonObject &job)
{
    QStringList out;
    for (const QJsonValue &v : job.value(u"lines").toArray())
        out << v.toString();
    return out;
}

} // namespace

TEST_CASE("Quick Label: anyone logged in prints typed lines on a label roll; the job says lines, roll, copies", "[labels][quick]")
{
    QuickLabelBench bench;
    PosService pos(test::seedPosData(), nullptr);

    // Not logged in: nothing.
    CHECK_FALSE(pos.quickLabelRolls());
    CHECK_FALSE(pos.printQuickLabel({u"cosmic"_s}, u"2X1 VINYL"_s, 1));
    CHECK(bench.calls().isEmpty());

    // A server (not a manager): the rolls, the last one used first.
    REQUIRE(pos.loginWithPin(u"1111"_s));
    CHECK(pos.quickLabel()[u"ready"_s].toBool());
    REQUIRE(pos.quickLabelRolls());
    REQUIRE(waitFor([&] { return !pos.quickLabel()[u"loading"_s].toBool(); }));
    const QVariantList rolls = pos.quickLabel()[u"rolls"_s].toList();
    REQUIRE(rolls.size() == 2);
    CHECK(rolls.at(0).toMap()[u"printer"_s].toString() == u"Zebra Large Labels"_s);
    CHECK(pos.quickLabel()[u"roll"_s].toString() == u"CIGAR STICK"_s);

    // Printing: up to 3 lines of 40 characters, trimmed, control characters dropped; copies up to 50.
    REQUIRE(pos.printQuickLabel({u"  cosmic  "_s, QString(60, u'x'), u"du\tst"_s, u"a fourth line"_s}, u"2X1 VINYL"_s, 99));
    CHECK(pos.quickLabel()[u"printing"_s].toBool());
    CHECK_FALSE(pos.printQuickLabel({u"again"_s}, u"2X1 VINYL"_s, 1));          // one at a time
    REQUIRE(waitFor([&] { return !pos.quickLabel()[u"printing"_s].toBool(); }));
    CHECK(pos.quickLabel()[u"statusOk"_s].toBool());
    CHECK(pos.quickLabel()[u"status"_s].toString() == u"Printed 2 on Zebra Large Labels"_s);
    CHECK(pos.quickLabel()[u"roll"_s].toString() == u"2X1 VINYL"_s);
    const QJsonObject job = bench.job(u"print-job.json"_s);
    CHECK(jobLines(job) == QStringList{u"cosmic"_s, QString(40, u'x'), u"dust"_s});
    CHECK(job.value(u"roll").toString() == u"2X1 VINYL"_s);
    CHECK(job.value(u"copies").toInt() == 50);
    // The job file (it holds the text) is gone once printed; the text never goes on the command line.
    const QStringList printCall = bench.calls().filter(u"--quick-job"_s);
    REQUIRE(printCall.size() == 1);
    CHECK_FALSE(printCall.first().contains(u"cosmic"_s));
    CHECK_FALSE(QFile::exists(printCall.first().split(u' ').value(1)));

    // At least one copy.
    REQUIRE(pos.printQuickLabel({u"one"_s}, u"CIGAR STICK"_s, 0));
    REQUIRE(waitFor([&] { return !pos.quickLabel()[u"printing"_s].toBool(); }));
    CHECK(bench.job(u"print-job.json"_s).value(u"copies").toInt() == 1);

    // Nothing typed, no roll, a roll that isn't a label template: refused, nothing run.
    const qsizetype before = bench.calls().size();
    CHECK_FALSE(pos.printQuickLabel({u" "_s, QString()}, u"2X1 VINYL"_s, 1));
    CHECK_FALSE(pos.printQuickLabel({u"cosmic"_s}, QString(), 1));
    CHECK_FALSE(pos.printQuickLabel({u"cosmic"_s}, u"NO SUCH ROLL"_s, 1));
    CHECK(bench.calls().size() == before);

    // The printer fails: its words.
    REQUIRE(pos.printQuickLabel({u"FAILME"_s}, u"2X1 VINYL"_s, 1));
    REQUIRE(waitFor([&] { return !pos.quickLabel()[u"printing"_s].toBool(); }));
    CHECK_FALSE(pos.quickLabel()[u"statusOk"_s].toBool());
    CHECK(pos.quickLabel()[u"status"_s].toString().contains(u"timed out"_s));

    // Done: the status and picture are forgotten.
    REQUIRE(pos.closeQuickLabel());
    CHECK(pos.quickLabel()[u"status"_s].toString().isEmpty());

    // No label utility on the main computer: said, nothing printed.
    qputenv("VT_LABEL_LAUNCHER", (bench.dir.path() + u"/missing"_s).toUtf8());
    CHECK_FALSE(pos.quickLabel()[u"ready"_s].toBool());
    CHECK_FALSE(pos.printQuickLabel({u"cosmic"_s}, u"2X1 VINYL"_s, 1));
}

TEST_CASE("Quick Label: the preview is the label utility's picture of what's typed now", "[labels][quick]")
{
    QuickLabelBench bench;
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));

    REQUIRE(pos.quickLabelPreview({u"cosmic"_s, QString(), QString()}, u"2X1 VINYL"_s));
    CHECK(pos.quickLabel()[u"previewBusy"_s].toBool());
    REQUIRE(waitFor([&] { return !pos.quickLabel()[u"previewBusy"_s].toBool(); }));
    CHECK(pos.quickLabel()[u"preview"_s].toString() == u"data:image/png;base64,"_s + QString::fromLatin1(QByteArray("PNG").toBase64()));
    // What it shows, as the screen writes it (JSON.stringify([lines, roll])).
    CHECK(pos.quickLabel()[u"previewFor"_s].toString() == uR"([["cosmic","",""],"2X1 VINYL"])"_s);
    const QJsonObject job = bench.job(u"preview-job.json"_s);
    CHECK(jobLines(job) == QStringList{u"cosmic"_s, QString(), QString()});
    CHECK(job.value(u"roll").toString() == u"2X1 VINYL"_s);
    CHECK_FALSE(job.contains(u"copies"_s));

    // Nothing typed: no picture, and the utility isn't run.
    const qsizetype before = bench.calls().size();
    REQUIRE(pos.quickLabelPreview({QString(), u"  "_s}, u"2X1 VINYL"_s));
    CHECK(pos.quickLabel()[u"preview"_s].toString().isEmpty());
    CHECK(bench.calls().size() == before);
    CHECK_FALSE(pos.quickLabelPreview({u"cosmic"_s}, u"NO SUCH ROLL"_s));
}

TEST_CASE("Quick Label: the quickLabel command opens the sheet for anyone logged in; Cashier Duty has the button", "[labels][quick]")
{
    QuickLabelBench bench;
    auto layout = test::loadTestLayout();
    REQUIRE(layout);
    // The Cashier Duty screen's QUICK LABEL button, under LOOK UP CHECKS.
    const layout::Page *duty = layout->page(u"cashier-duty"_s);
    REQUIRE(duty);
    const auto zone = std::ranges::find_if(duty->zones, [](const layout::Zone &z) { return z.id == u"cd-quicklabel"; });
    REQUIRE(zone != duty->zones.end());
    REQUIRE(zone->actions.size() == 1);
    CHECK(zone->actions.front().type() == u"command"_s);
    CHECK(zone->actions.front().str(u"name") == u"quickLabel"_s);

    test::RecordingSink sink;
    PosService pos{test::seedPosData(), &sink};
    LayoutController c{*layout};
    c.setMealPeriod(u"lunch"_s);
    c.setPos(&pos);
    c.command(u"quickLabel"_s);
    CHECK_FALSE(c.quickLabelOpen());                     // nobody logged in
    REQUIRE(pos.loginWithPin(u"1111"_s));                // a server
    REQUIRE(c.jumpTo(u"cashier-duty"_s));
    c.activate(u"cd-quicklabel"_s);
    CHECK(c.quickLabelOpen());
    c.closeQuickLabel();
    CHECK_FALSE(c.quickLabelOpen());
    CHECK(c.describeAction(zone->actions.front()).contains(u"Quick Label"_s));
}
