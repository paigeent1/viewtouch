#pragma once

#include <QDir>
#include <QHostAddress>
#include <QHash>
#include <QObject>
#include <QSslServer>
#include <QTimer>

#include <functional>
#include <memory>
#include <vector>

namespace vt::app { class PosShared; }

namespace vt::net {

class LayoutHub;

// Serves remote terminals: each connection gets its own PosService session
// on the shared store, and receives its state as it changes. Connections
// are encrypted and only paired devices get in (see net/pairing.hh); a
// device pairing with the manager's code gets its key and is let go.
// A value as compact JSON text (what the updates to the screens are made of).
QByteArray jsonText(const QVariant &v);

class PosServer : public QObject {
    Q_OBJECT

public:
    PosServer(app::PosShared *shared, LayoutHub *layouts, QObject *parent = nullptr);
    ~PosServer() override;

    // False when this system can't make encrypted connections (errorString says why).
    // Where the store keeps updates for its screens (the app for tablets,
    // "ViewTouch-b<build>-arm64-v8a.apk"; packages for Linux screens): a
    // screen that's behind is told, and a manager there can fetch it.
    void setUpdatesDir(const QString &dir) { updatesDir_ = dir; }
    QString updatesDir() const { return updatesDir_; }
    struct UpdateFile {
        QString path;
        int build = 0;
        QString platform;   // "android-arm64", "linux-x86_64"...
    };
    // The update files there, newest build first.
    QList<UpdateFile> updateFiles() const;
    // The newest for that platform, newer than `build` (none: an empty path).
    UpdateFile updateFor(const QString &platform, int build) const;
    // Billboards (Manager -> Terminals: Billboard) play the guest screen's videos: files in this
    // folder only (~/Pictures/Guest Screen; tests: their own), sent a part at a time on request.
    void setGuestVideoDir(const QString &dir) { guestVideoDir_ = dir; }
    QString guestVideoDir() const { return guestVideoDir_; }
    // The video `name` in `dir`: its real path when it's a video file right in that folder (a plain
    // name; no other folder, no link out of it), else empty. Nothing else is ever sent.
    static QString guestVideoFile(const QString &dir, const QString &name);
    // Whether a piece of a screen's state is ever sent to a billboard (the slides, pictures, logo,
    // the store's name and language: never a check, a customer, a sale or the staff).
    static bool sentToBillboard(const QString &key);
    // Ping every `pingMs`; drop a terminal silent `silentMs` (tests: short).
    void setHeartbeat(int pingMs, qint64 silentMs) { pingTimer_.setInterval(pingMs); silentMs_ = silentMs; }
    bool listen(const QHostAddress &address, quint16 port);
    quint16 port() const { return server_.serverPort(); }
    QString errorString() const { return error_.isEmpty() ? server_.errorString() : error_; }
    int terminalCount() const;
    // Time spent sending the screens their updates (for the stress test).
    qint64 updateNsecs() const { return updateNsecs_; }
    int updateRounds() const { return updateRounds_; }

    // The store's standby: a full copy of the database when it connects
    // (`snapshot` makes one: the database file's bytes), then every change
    // (`replicate`, from the database writer's mirror).
    void setSnapshotSource(std::function<QByteArray()> snapshot) { snapshot_ = std::move(snapshot); }
    void replicate(const QJsonObject &op);
    int standbyCount() const;
    // Who is connected, for the Network screen: [{name, address, since, standby}].
    QVariantList connections() const;

    // A ticket for a receipt printer plugged into a tablet ("usb" printers, print/usb_print.hh):
    // sent to the tablet named `terminal` (Manager -> Terminals), which prints it on the USB
    // printer `selector` picks and answers. `done` is called once, on this object's thread:
    // with the tablet's answer, or false when it isn't connected, goes away, or hasn't
    // answered `timeoutMs` (+ a few seconds) later.
    using PrintDone = std::function<void(bool ok, const QString &error)>;
    void printOnTablet(const QString &terminal, const QString &selector, const QByteArray &data, int timeoutMs,
                       PrintDone done);

signals:
    void terminalsChanged();
    void standbyChanged();

private:
    struct Connection;

    void onNewConnection();
    void onPreSharedKey(QSslSocket *socket, class QSslPreSharedKeyAuthenticator *auth);
    void pair(Connection *c, const QJsonObject &message);
    // Billboards: their welcome, what they may ask, and their videos.
    void welcomeBillboard(Connection *c);
    void handleBillboard(Connection *c, const QJsonObject &message);
    bool allowedForBillboard(const QString &ref) const;
    QString billboardVideo(const QString &name) const;   // a video on the slides, in the folder
    void sendVideoPart(Connection *c, const QString &name, qint64 at);
    // Drop connections of devices that are no longer paired.
    void dropRevoked();
    void onReadyRead(Connection *c);
    void handle(Connection *c, const QJsonObject &message);
    void drop(Connection *c);
    // Send changed state to one / every terminal.
    void flush(Connection *c);
    void scheduleFlushAll();

    app::PosShared *shared_;
    qint64 updateNsecs_ = 0;
    // During a round of updates (every screen at once): what's the same for
    // all of them is compared and turned into JSON once (by the shared data
    // it points at; cleared after the round).
    bool inRound_ = false;
    QHash<const void *, QByteArray> roundJson_;
    QHash<QPair<const void *, const void *>, bool> roundSame_;
    int updateRounds_ = 0;
    LayoutHub *layouts_;
    QSslServer server_;
    QString error_;
    QHash<QString, QByteArray> pairingKeys_;   // code -> TLS key (slow to derive)
    std::vector<std::unique_ptr<Connection>> connections_;
    QTimer flushTimer_;
    QTimer tickTimer_;   // refresh time-based fields (minutes open)
    QTimer pingTimer_;   // the standby knows the main is alive; terminals, the store
    qint64 silentMs_ = 15'000;
    QString updatesDir_;
    QString guestVideoDir_ = QDir::home().filePath(QStringLiteral("Pictures/Guest Screen"));
    void sendUpdate(Connection *c);   // a terminal that answered pings, silent this long: gone
    std::function<QByteArray()> snapshot_;
    // Tickets on their way to a tablet's USB printer, by id (printOnTablet).
    struct TabletPrint {
        Connection *to = nullptr;
        PrintDone done;
        QTimer *timer = nullptr;
    };
    QHash<qint64, TabletPrint> tabletPrints_;
    qint64 nextTabletPrint_ = 1;
    void finishTabletPrint(qint64 id, bool ok, const QString &error);
};

} // namespace vt::net
