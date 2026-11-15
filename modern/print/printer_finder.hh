#pragma once

#include <QHostAddress>
#include <QVariantList>
#include <QList>
#include <QString>
#include <QStringList>

namespace vt::app { class PosShared; }

namespace vt::print {

// Network receipt and kitchen printers listen on port 9100 (raw, "JetDirect").
// Finding them: try that port on every address of this computer's own local
// network, all at once (a /24 takes about a second), then ask each one that
// answers for its ESC/POS status (DLE EOT 1): a printer says a byte back.
// Only private addresses (10.x, 172.16-31.x, 192.168.x): nothing outside
// the store is tried. (Fork) Addresses in noProbe (the store's label
// printers: Zebra ZPL, TSPL) are only connected to and let go: nothing is
// sent to them, so a label printer never sees a byte it didn't ask for.
// USB printers (a Rongta on /dev/usb/lp0) aren't on the network: never touched.
struct FoundPrinter {
    QString host;
    int port = 9100;
    bool answered = false;   // said its status: an ESC/POS printer
};

// Every other address on this computer's private IPv4 networks (each a /24
// around its own address; at most 4 of them).
QList<QHostAddress> nearbyHosts();

// Blocks the calling thread (a worker's): connectMs for all the connections
// together, then answerMs for the status answers.
QList<FoundPrinter> findPrinters(const QList<QHostAddress> &hosts, int port = 9100, int connectMs = 1500,
                                 int answerMs = 700, const QStringList &noProbe = {});

// A page saying "This printer is <host>", big, then a cut: which printer is
// which. Blocks; false (and why) when it couldn't be sent.
bool printWhoAmI(const QString &host, int port, const QString &title, const QString &line, QString *error = nullptr);

// The store's hooks (PosShared::searchPrinters, printWhoAmI) from these.
void installFinder(app::PosShared &shared);

// Tests: where findPrinters looks instead of the network (empty: the network).
void setSearchForTests(const QList<QHostAddress> &hosts, int port);
QList<QHostAddress> searchHostsForTests();
int searchPortForTests();

} // namespace vt::print
