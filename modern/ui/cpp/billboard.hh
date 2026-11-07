#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace vt::net { class RemoteSession; }

// A billboard: a TV around the store (a Fire TV / Android TV, or any screen running
// `vtmodern --connect ... --screen billboard`) that shows the guest screen's turn of messages,
// pictures and videos full screen, and nothing else (Billboard.qml). Signage only: the server
// sends a billboard the slides, pictures and logo, never a check or a customer (PosServer).
// Its settings (on/off, hours, mute, volume) are kept on the server: Store Settings > Billboards.
// A manager closes it: five taps in the top-left corner (or five presses of the remote's Back or
// OK button) and their PIN.
class BillboardController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Made by main()")

    // The slide lines as in Store Settings (guestslides.hh), and the store's running promotions.
    Q_PROPERTY(QStringList slides READ slides NOTIFY changed)
    Q_PROPERTY(QString logoUrl READ logoUrl NOTIFY changed)
    Q_PROPERTY(QString storeName READ storeName NOTIFY changed)
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    // Inside its hours and switched on (the store's clock decides); else the screen goes dark.
    Q_PROPERTY(bool showing READ showing NOTIFY changed)
    // Every video silent here, whatever its own Sound switch says.
    Q_PROPERTY(bool mute READ mute NOTIFY changed)
    // Its videos' volume, 0-1 (Store Settings > Billboards).
    Q_PROPERTY(qreal volume READ volume NOTIFY changed)
    // Identify (Store Settings > Billboards): its name across the screen while true (10 seconds).
    Q_PROPERTY(bool identify READ identify NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool online READ online NOTIFY changed)
    // Changes when a picture or video has arrived (bind imageUrl / videoUrl to it).
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // A video on its way: how much has come (0-100), or -1.
    Q_PROPERTY(int videoProgress READ videoProgress NOTIFY changed)
    // The last thing the store said (a refused PIN).
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

public:
    explicit BillboardController(vt::net::RemoteSession *remote, QObject *parent = nullptr);

    QStringList slides() const;
    QString logoUrl() const;
    QString storeName() const;
    QString accent() const;
    bool showing() const;
    bool mute() const;
    qreal volume() const;
    bool identify() const;
    QString name() const;
    bool online() const;
    int revision() const;
    int videoProgress() const;
    QString message() const { return message_; }

    Q_INVOKABLE QString imageUrl(const QString &ref) const;
    // A "video:<path>[#sound]" slide: this device's copy as a URL, or "" while it's on its way.
    Q_INVOKABLE QString videoUrl(const QString &line);
    // A manager's PIN: the app closes (closeRequested) if it is one.
    Q_INVOKABLE void leave(const QString &pin);
    // Outside its hours: the screen may sleep (and the TV with it); back inside, awake and on.
    Q_INVOKABLE void setDark(bool dark);

signals:
    void changed();
    void messageChanged();
    void closeRequested();

private:
    QVariantMap info() const;

    vt::net::RemoteSession *remote_;
    QString message_;
    int dark_ = -1;   // what setDark last did (-1: not yet)
};
