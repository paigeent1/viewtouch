#include "net/update_download.hh"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSslPreSharedKeyAuthenticator>

#include <algorithm>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcDownload, "vt.update")

namespace vt::net {

namespace {

// Nothing at all for this long on a connection: it's dead (Wi-Fi gone without it closing). Try again.
constexpr qint64 kStuckMs = 30'000;

} // namespace

UpdateDownload::UpdateDownload(const Credentials &credentials, const QString &host, quint16 port,
                               const UpdateTicket &ticket, const QString &dir, QObject *parent)
    : QObject(parent)
    , credentials_(credentials)
    , host_(host)
    , port_(port)
    , ticket_(ticket)
    , path_(QDir(dir).filePath(QFileInfo(ticket.name).fileName()))
{
    socket_.setObjectName(u"vtUpdateDownload"_s);
    socket_.setParent(this);   // findChild (the tests cut it off)
    socket_.setSslConfiguration(tlsConfiguration());
    connect(&socket_, &QSslSocket::preSharedKeyAuthenticationRequired, this, [this](QSslPreSharedKeyAuthenticator *a) {
        a->setIdentity(deviceIdentity(credentials_.terminalId));
        a->setPreSharedKey(credentials_.key);
    });
    connect(&socket_, &QSslSocket::encrypted, this, &UpdateDownload::onEncrypted);
    connect(&socket_, &QSslSocket::readyRead, this, &UpdateDownload::onReadyRead);
    connect(&socket_, &QSslSocket::disconnected, this, &UpdateDownload::onDropped);
    connect(&socket_, &QSslSocket::errorOccurred, this, [this] {
        if (socket_.state() == QAbstractSocket::UnconnectedState)
            onDropped();
    });
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &UpdateDownload::connectNow);
    stuck_.setInterval(5000);
    connect(&stuck_, &QTimer::timeout, this, [this] {
        if (socket_.state() != QAbstractSocket::UnconnectedState
            && QDateTime::currentMSecsSinceEpoch() - lastBytes_ > kStuckMs) {
            qCInfo(lcDownload) << "the update download stopped moving; trying again";
            socket_.abort();
            onDropped();
        }
    });
}

UpdateDownload::~UpdateDownload()
{
    socket_.disconnect(this);
    socket_.abort();
    socket_.setParent(nullptr);   // a member: not deleted again as a child
}

void UpdateDownload::start()
{
    if (ticket_.name.isEmpty() || QFileInfo(ticket_.name).fileName() != ticket_.name || ticket_.size < 0)
        return fail(tr("The store sent an update with no proper name."));
    QDir().mkpath(QFileInfo(path_).absolutePath());
    // What the part on hand is of: the same file (size and fingerprint) goes on; anything else starts over.
    QFile meta(path_ + u".part.json"_s);
    QJsonObject was;
    if (meta.open(QIODevice::ReadOnly))
        was = QJsonDocument::fromJson(meta.readAll()).object();
    meta.close();
    if (was.value(u"sha256"_s).toString() != ticket_.sha256 || was.value(u"size"_s).toInteger() != ticket_.size)
        QFile::remove(path_ + u".part"_s);
    if (meta.open(QIODevice::WriteOnly | QIODevice::Truncate))
        meta.write(QJsonDocument(QJsonObject{{u"sha256"_s, ticket_.sha256}, {u"size"_s, ticket_.size}}).toJson());
    meta.close();
    part_.setFileName(path_ + u".part"_s);
    if (!part_.open(QIODevice::ReadWrite))
        return fail(tr("Couldn't save the update: %1").arg(part_.errorString()));
    if (part_.size() > ticket_.size)
        part_.resize(0);
    got_ = part_.size();
    part_.seek(got_);
    emit progress(got_, ticket_.size);
    if (got_ == ticket_.size)
        return complete();
    stuck_.start();
    connectNow();
}

void UpdateDownload::connectNow()
{
    if (done_ || socket_.state() != QAbstractSocket::UnconnectedState)
        return;
    header_.clear();
    started_ = false;
    lastBytes_ = QDateTime::currentMSecsSinceEpoch();
    socket_.connectToHostEncrypted(host_, port_);
}

void UpdateDownload::onEncrypted()
{
    QByteArray ask = QJsonDocument(QJsonObject{{u"t"_s, u"download"_s}, {u"token"_s, ticket_.token}, {u"from"_s, got_}})
                         .toJson(QJsonDocument::Compact);
    socket_.write(ask + '\n');
}

void UpdateDownload::onReadyRead()
{
    lastBytes_ = QDateTime::currentMSecsSinceEpoch();
    QByteArray bytes = socket_.readAll();
    if (!started_) {
        // First a line saying what comes (or why not), then the file's bytes.
        header_ += bytes;
        const qsizetype nl = header_.indexOf('\n');
        if (nl < 0) {
            if (header_.size() > 64 * 1024)
                socket_.abort();
            return;
        }
        const QJsonObject m = QJsonDocument::fromJson(header_.left(nl)).object();
        bytes = header_.mid(nl + 1);
        header_.clear();
        if (m.value(u"t"_s).toString() == u"downloadError"_s)
            return fail(m.value(u"text"_s).toString());
        if (m.value(u"t"_s).toString() != u"downloadStart"_s || m.value(u"size"_s).toInteger() != ticket_.size
            || m.value(u"sha256"_s).toString() != ticket_.sha256)
            return fail(tr("The update on the store's computer has changed. A manager can tap Update Now again."));
        const qint64 from = m.value(u"from"_s).toInteger();
        if (from != got_) {   // the store starts it over
            part_.resize(from);
            got_ = from;
            part_.seek(got_);
        }
        started_ = true;
        tries_ = 0;
        if (waiting_) {
            waiting_ = false;
            emit waitingChanged();
        }
    }
    take(bytes);
}

void UpdateDownload::take(const QByteArray &bytes)
{
    if (bytes.isEmpty() || done_)
        return;
    const QByteArray use = bytes.left(ticket_.size - got_);
    if (part_.write(use) != use.size())
        return fail(tr("Couldn't save the update: %1").arg(part_.errorString()));
    got_ += use.size();
    emit progress(got_, ticket_.size);
    if (got_ >= ticket_.size)
        complete();
}

void UpdateDownload::onDropped()
{
    if (done_ || retry_.isActive())
        return;
    part_.flush();
    if (tries_ >= 60)   // half an hour or so: what it has is kept for the next Update Now
        return fail(tr("Can't reach the store's computer for the update. A manager can tap Update Now again."));
    // Again after a pause, longer each time (up to retryMaxMs_), from where it got to.
    const int wait = std::min(retryMaxMs_, retryMs_ << std::min(tries_, 5));
    ++tries_;
    qCInfo(lcDownload) << "the update download was cut off at" << got_ << "of" << ticket_.size << "bytes; again in"
                       << wait << "ms";
    if (!waiting_) {
        waiting_ = true;
        emit waitingChanged();
    }
    retry_.start(wait);
}

void UpdateDownload::complete()
{
    done_ = true;
    stuck_.stop();
    retry_.stop();
    socket_.disconnect(this);
    socket_.abort();
    part_.close();
    // The whole file, checked.
    QFile f(path_ + u".part"_s);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const bool read = f.open(QIODevice::ReadOnly) && hash.addData(&f);
    f.close();
    if (!read || QString::fromLatin1(hash.result().toHex()) != ticket_.sha256) {
        QFile::remove(path_ + u".part"_s);
        QFile::remove(path_ + u".part.json"_s);
        done_ = false;
        return fail(tr("The update didn't arrive whole. Try again."));
    }
    QFile::remove(path_);
    if (!QFile::rename(path_ + u".part"_s, path_)) {
        done_ = false;
        return fail(tr("Couldn't save the update."));
    }
    QFile::remove(path_ + u".part.json"_s);
    qCInfo(lcDownload).noquote() << "update downloaded:" << path_;
    emit finished(path_);
}

void UpdateDownload::fail(const QString &why)
{
    const bool wasDone = done_;
    done_ = true;
    stuck_.stop();
    retry_.stop();
    socket_.disconnect(this);
    socket_.abort();
    part_.close();
    if (!wasDone)
        qCWarning(lcDownload).noquote() << "update download failed:" << why;
    emit failed(why);
}

} // namespace vt::net
