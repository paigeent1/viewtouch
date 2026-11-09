// Find a Tablet: this screen is being looked for (FindMeOverlay.qml shows "Here I am"). The sound:
//   Android: FindMe.kt - beeps on the alarm stream with the alarm volume all the way up (put back
//            after), the screen woken and kept on.
//   elsewhere: a beep sound made here, played over and over (Qt Multimedia where it's installed,
//            else aplay / paplay every second).
// Stops with on = false, or by itself after `seconds`.

#include "layoutcontroller.hh"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <numbers>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#elif defined(VT_HAVE_VIDEO)
#include <QSoundEffect>
#endif

using namespace Qt::StringLiterals;

namespace {

#ifndef Q_OS_ANDROID
// Three short, high beeps and a pause (one second), as a WAV file: 16-bit mono.
QString beepFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = QDir(dir.isEmpty() ? QDir::tempPath() : dir).filePath(u"viewtouch-find-me.wav"_s);
    if (QFile::exists(path))
        return path;
    constexpr int rate = 22050;
    QByteArray pcm;
    QDataStream out(&pcm, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    for (int i = 0; i < rate; ++i) {
        const double t = double(i) / rate;
        const double inBeep = std::fmod(t, 0.25);
        const bool on = t < 0.75 && inBeep < 0.15;
        // A square-ish wave at 2.8 kHz: cuts through a noisy room.
        const double v = on ? (std::sin(2 * std::numbers::pi * 2800 * t) > 0 ? 0.9 : -0.9) : 0.0;
        out << qint16(v * 32767);
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    QDataStream h(&f);
    h.setByteOrder(QDataStream::LittleEndian);
    h.writeRawData("RIFF", 4);
    h << quint32(36 + pcm.size());
    h.writeRawData("WAVEfmt ", 8);
    h << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
    h.writeRawData("data", 4);
    h << quint32(pcm.size());
    h.writeRawData(pcm.constData(), int(pcm.size()));
    return path;
}
#endif

#if !defined(Q_OS_ANDROID) && !defined(VT_HAVE_VIDEO)
void beepOnce()
{
    const QString file = beepFile();
    if (file.isEmpty())
        return;
    if (!QProcess::startDetached(u"aplay"_s, {u"-q"_s, file}))
        QProcess::startDetached(u"paplay"_s, {file});
}
#endif

} // namespace

void LayoutController::findMeAlarm(bool on, int seconds)
{
    if (!findAlarmEnd_) {
        findAlarmEnd_ = new QTimer(this);
        findAlarmEnd_->setSingleShot(true);
        connect(findAlarmEnd_, &QTimer::timeout, this, [this] { findMeAlarm(false); });
    }
    if (on) {
        findAlarmEnd_->start(std::clamp(seconds, 5, 600) * 1000);
        wake();   // ViewTouch's own dimmed screen too
    } else {
        findAlarmEnd_->stop();
    }
#ifdef Q_OS_ANDROID
    const int secs = std::clamp(seconds, 5, 600);
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([on, secs] {
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid())
            return;
        if (on)
            QJniObject::callStaticMethod<void>("org/viewtouch/pos/FindMe", "start", "(Landroid/app/Activity;I)V",
                                               activity.object<jobject>(), jint(secs));
        else
            QJniObject::callStaticMethod<void>("org/viewtouch/pos/FindMe", "stop", "(Landroid/app/Activity;)V",
                                               activity.object<jobject>());
    });
#elif defined(VT_HAVE_VIDEO)
    if (!findSound_) {
        auto *sound = new QSoundEffect(this);
        sound->setSource(QUrl::fromLocalFile(beepFile()));
        sound->setLoopCount(QSoundEffect::Infinite);
        sound->setVolume(1.0);
        findSound_ = sound;
    }
    auto *sound = static_cast<QSoundEffect *>(findSound_);
    if (on)
        sound->play();
    else
        sound->stop();
#else
    // No Qt Multimedia here: the system's player, once a second.
    if (!findSound_) {
        auto *every = new QTimer(this);
        every->setInterval(1000);
        connect(every, &QTimer::timeout, this, &beepOnce);
        findSound_ = every;
    }
    auto *every = static_cast<QTimer *>(findSound_);
    if (on) {
        if (!every->isActive()) {
            every->start();
            beepOnce();
        }
    } else {
        every->stop();
    }
#endif
}
