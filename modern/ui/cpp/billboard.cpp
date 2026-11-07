#include "billboard.hh"

#include "guestslides.hh"
#include "net/remote_session.hh"

#include <QUrl>

#include <algorithm>
#include <QVariantMap>
#if defined(Q_OS_ANDROID)
#include <QCoreApplication>
#include <QJniObject>
#include <QtCore/qnativeinterface.h>
#endif

using namespace Qt::StringLiterals;

BillboardController::BillboardController(vt::net::RemoteSession *remote, QObject *parent)
    : QObject(parent)
    , remote_(remote)
{
    using vt::net::RemoteSession;
    connect(remote_, &RemoteSession::adminChanged, this, &BillboardController::changed);
    connect(remote_, &RemoteSession::onlineChanged, this, &BillboardController::changed);
    connect(remote_, &RemoteSession::videosChanged, this, &BillboardController::changed);
    // A picture arriving says so with these (RemoteSession::localImage).
    connect(remote_, &RemoteSession::checkChanged, this, &BillboardController::changed);
    connect(remote_, &RemoteSession::notice, this, [this](const QString &text) {
        message_ = text;
        emit messageChanged();
    });
}

QVariantMap BillboardController::info() const
{
    return remote_->billboard();
}

QStringList BillboardController::slides() const
{
    QStringList out;
    for (const QVariant &v : info().value(u"slides"_s).toList())
        out << v.toString();
    return out;
}

QString BillboardController::logoUrl() const
{
    const QString logo = info().value(u"logo"_s).toString();
    return logo.isEmpty() ? QString() : remote_->imageUrl(logo);
}

QString BillboardController::storeName() const
{
    const QString name = info().value(u"storeName"_s).toString();
    return name.isEmpty() ? remote_->storeName() : name;
}

QString BillboardController::accent() const { return info().value(u"accent"_s).toString(); }

bool BillboardController::showing() const
{
    // Before the store has said (no word yet): on.
    const QVariantMap i = info();
    return !i.contains(u"showing"_s) || i.value(u"showing"_s).toBool();
}

bool BillboardController::mute() const { return info().value(u"mute"_s).toBool(); }
qreal BillboardController::volume() const
{
    const QVariantMap i = info();
    return i.contains(u"volume"_s) ? std::clamp(i.value(u"volume"_s).toInt(), 0, 100) / 100.0 : 1.0;
}
bool BillboardController::identify() const { return info().value(u"identify"_s).toBool(); }
QString BillboardController::name() const
{
    const QString n = info().value(u"name"_s).toString();
    return n.isEmpty() ? remote_->terminalName() : n;
}
bool BillboardController::online() const { return remote_->online(); }
int BillboardController::revision() const { return remote_->imageRevision() + remote_->videoRevision(); }
int BillboardController::videoProgress() const { return remote_->videoProgress(); }

QString BillboardController::imageUrl(const QString &ref) const
{
    return remote_->imageUrl(ref);
}

QString BillboardController::videoUrl(const QString &line)
{
    const vt::guest::VideoSlide v = vt::guest::parseVideoSlide(line);
    if (v.path.isEmpty())
        return {};
    const QString local = remote_->billboardVideo(v.path);
    return local.isEmpty() ? QString() : QUrl::fromLocalFile(local).toString();
}

void BillboardController::leave(const QString &pin)
{
    remote_->invoke(u"leaveBillboard"_s, {pin}, [this](const QVariant &r) {
        if (r.toBool())
            emit closeRequested();
    });
}

void BillboardController::setDark(bool dark)
{
    if (dark_ == int(dark))
        return;
    dark_ = int(dark);
#if defined(Q_OS_ANDROID)
    // Billboard.kt: dark lets the screen sleep (Fire OS then turns the TV off by its own sleep
    // timer); light keeps it on and asks Android to wake the screen.
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([dark] {
        QJniObject::callStaticMethod<void>("org/viewtouch/pos/Billboard", "dark", "(Landroid/app/Activity;Z)V",
                                           QNativeInterface::QAndroidApplication::context().object(), jboolean(dark));
    });
#endif
}
