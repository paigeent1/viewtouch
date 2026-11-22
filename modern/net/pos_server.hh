#pragma once

#include <QDir>
#include <QHostAddress>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
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
    // How a screen gets its update (HCA 2026-11-15: a 160 MB app sent in one burst on the store's
    // connection queued the pings behind it, so the store took the tablet for gone, cut it off and the
    // download never came back):
    // - a screen that knows how ("updates": 2 in its hello) asks a manager's "getUpdate" with fast:
    //   it gets a ticket for its own device (updateTicket: token, name, build, size, sha256) and fetches
    //   the file on a second connection to this port (the same paired-device key), a plain stream of
    //   the file's bytes from where it got to: {t: "download", token, from} -> {t: "downloadStart",
    //   size, from, sha256} + the bytes, or {t: "downloadError", text}. Nothing else waits behind it.
    // - an older screen gets the parts as before, but one at a time as the last has gone out, so the
    //   pings and everything else still get through.
    // Downloads begun, and of those, picked up part way (for the Network screen and the tests).
    int downloadsStarted() const { return downloadsStarted_; }
    int downloadsResumed() const { return downloadsResumed_; }
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
    // The store's screens for the Pi's scripts (vt-status, vt-adb-ensure): every paired screen, connected
    // or not, as a JSON list [{name, ip, connected, platform, appVersion, build, updateState, adb, lastSeen}]
    // (no keys or other secrets), written (atomically) to this file whenever a screen connects, goes or
    // says something new, and once a minute. Empty: none written.
    void setScreensFile(const QString &path);
    QString screensFile() const { return screensFile_; }
    // Where tablets' own log lines go (TabletCare): <dir>/<screen name>.log, the older half in .log.1
    // past about 1 MB.
    void setTabletLogDir(const QString &dir) { tabletLogDir_ = dir; }
    QString tabletLogDir() const { return tabletLogDir_; }
    static QString tabletLogName(const QString &screen);   // a safe file name for it
    static constexpr qint64 kTabletLogBytes = 1024 * 1024;
    QVariantList screens() const;
    bool writeScreensFile();

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
    // The screen whose call is being answered: its notices go out after the reply (as always); any
    // other notice (one that comes later, e.g. a TV's answer) goes out with the next round.
    Connection *calling_ = nullptr;
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
    // The listener, renewed when nothing has come in for a while (HCA 2026-11-22: Qt stopped taking
    // connections - 51 queued at the door - while the screens kept trying; a fresh listener takes them).
    QTimer listenTimer_;
    QHostAddress listenAddress_;
    quint16 listenPort_ = 0;
    qint64 lastAccepted_ = 0;
    qint64 silentMs_ = 15'000;
    QString updatesDir_;
    QString screensFile_, tabletLogDir_;
    void appendTabletLog(const QString &screen, const QJsonArray &lines);
    QTimer screensTimer_;   // once a minute
    QTimer screensSoon_;    // soon after a change (several at once: one write)
    void screensChanged();
    // What each paired screen (terminal id) last said, kept after it goes: where it was and what it ran.
    struct Heard {
        QString ip, version, platform, updateState;
        int build = 0;
        QVariantMap adb, health;
    };
    QHash<QString, Heard> heard_;
    void remember(const Connection *c);
    QString guestVideoDir_ = QDir::home().filePath(QStringLiteral("Pictures/Guest Screen"));
    void sendUpdate(Connection *c, bool fast, bool byItself = false);
    void pumpPacedUpdate(Connection *c);
    void startDownload(Connection *c, const QJsonObject &message);
    void pumpDownload(Connection *c);
    // The file's sha256 (kept until the file changes: worked out once per file, not per screen).
    QString fingerprint(const QString &path);
    struct Fingerprint {
        qint64 size = -1;
        qint64 modified = 0;
        QString sha256;
    };
    QHash<QString, Fingerprint> fingerprints_;
    // Download tickets: for one device and one file, a day long (resumed downloads use it again).
    struct Ticket {
        QString terminalId, path, sha256;
        qint64 size = 0;
        qint64 expires = 0;
    };
    QHash<QString, Ticket> tickets_;
    // Older screens whose update the old way was cut off (told to tap Update Now again when back),
    // and those told once since this server started (the strip of one cut off before stays stuck).
    QSet<QString> cutUpdates_, toldOld_;
    int downloadsStarted_ = 0, downloadsResumed_ = 0;
    std::function<QByteArray()> snapshot_;
};

} // namespace vt::net
