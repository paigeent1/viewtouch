#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QTcpSocket>

// Terminal <-> server messages: one compact JSON object per line over TCP.
//
// Terminal to server:
//   {t: "hello", terminal}                       first message
//   {t: "call", id, m, a: [...]}                 PosSession::invoke
//   {t: "saveLayout", id, layout}                 page editor save
//   {t: "usbPrinted", id, ok, error}              answer to usbPrint (every byte sent, or why not)
// Server to terminal:
//   {t: "usbPrint", id, sel, ms, data}           a ticket for the USB printer on this tablet
//                                                (data base64; sel picks the printer; ms to do it in)
//   {t: "welcome", layout, state}                after hello
//   {t: "state", set: {key: value...}}           changed session properties
//   {t: "reply", id, r}                          answer to call / saveLayout
//   {t: "notice", text}                          status message
//   {t: "event", e: "checkClosed", v}            one-off happenings
//   {t: "layout", layout}                        pages saved on another terminal
//
// For each call the server sends state first, then events, the reply, and
// notices, so the terminal continues with up-to-date state.
namespace vt::net {

inline constexpr quint16 DefaultPort = 7719;
inline constexpr int ProtocolVersion = 1;
inline constexpr qint64 MaxMessageBytes = 32 * 1024 * 1024;

// Frames messages on a socket.
class LineChannel {
public:
    explicit LineChannel(QTcpSocket *socket) : socket_(socket) {}

    void send(const QJsonObject &message);
    // A message already in JSON (one line, no newline).
    void sendRaw(const QByteArray &json);
    // Complete messages received so far. Sets `overflow` on a runaway line
    // (over MaxMessageBytes); a caller that doesn't ask is cut off instead.
    QList<QJsonObject> receive(bool *overflow = nullptr);

private:
    QTcpSocket *socket_;
    QByteArray buffer_;
    qsizetype searched_ = 0;   // buffer_ has no newline before this
    bool lowDelay_ = false;
};

} // namespace vt::net
