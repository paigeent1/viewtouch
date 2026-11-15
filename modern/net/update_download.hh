#pragma once

#include "net/pairing.hh"

#include <QFile>
#include <QObject>
#include <QSslSocket>
#include <QTimer>

namespace vt::net {

// What the store gives a screen to download its update with (updateTicket; see PosServer).
struct UpdateTicket {
    QString token, name, sha256;
    int build = 0;
    qint64 size = 0;
};

// Downloads a screen's update from the store on a connection of its own (the same paired-device key
// as the store's connection), into "<name>.part" in `dir`. Cut off (Wi-Fi gone, the store restarted,
// this app closed), it goes on from where it got to: by itself after a pause, or when started again
// with a new ticket for the same file. Checked against the store's sha256 before it's handed over.
class UpdateDownload : public QObject {
    Q_OBJECT

public:
    UpdateDownload(const Credentials &credentials, const QString &host, quint16 port, const UpdateTicket &ticket,
                   const QString &dir, QObject *parent = nullptr);
    ~UpdateDownload() override;

    void start();
    const UpdateTicket &ticket() const { return ticket_; }
    qint64 received() const { return got_; }
    bool waiting() const { return waiting_; }   // cut off: trying again soon
    // Pauses between tries (tests: short).
    void setRetryDelays(int firstMs, int maxMs) { retryMs_ = firstMs; retryMaxMs_ = maxMs; }

signals:
    void progress(qint64 received, qint64 size);
    void waitingChanged();
    void finished(const QString &path);
    // Can't go on (the ticket ran out, the file is wrong, no room): a manager taps Update Now again.
    void failed(const QString &why);

private:
    void connectNow();
    void onEncrypted();
    void onReadyRead();
    void onDropped();
    void take(const QByteArray &bytes);
    void complete();
    void fail(const QString &why);

    Credentials credentials_;
    QString host_;
    quint16 port_ = 0;
    UpdateTicket ticket_;
    QString path_;   // the finished file; path_ + ".part" while on its way
    QSslSocket socket_;
    QFile part_;
    QByteArray header_;
    bool started_ = false;   // the store said downloadStart on this connection
    bool done_ = false;
    bool waiting_ = false;
    qint64 got_ = 0;
    qint64 lastBytes_ = 0;
    int tries_ = 0;
    int retryMs_ = 2000, retryMaxMs_ = 30'000;
    QTimer retry_, stuck_;
};

} // namespace vt::net
