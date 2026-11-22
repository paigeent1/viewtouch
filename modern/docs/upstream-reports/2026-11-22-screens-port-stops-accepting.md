# Terminals stuck on "Reconnecting": the server's listener stops accepting while the server sits idle

Report for official ViewTouch (github.com/ViewTouch/viewtouch), from the paigeent1/viewtouch fork.
Found 2026-11-22 at one of the fork's stores; fixed in the fork in commit 4d11fba.
Official main as of 46f1395 (2026-11-19) has the same code.

## What happened

On a store server (Raspberry Pi 5, Qt 6.12.0, `vtmodern --serve`) with three Android tablets and a
Fire TV billboard, every screen went to "Reconnecting to server" and never got back in. Restarting the
tablets didn't help. Restarting the server fixed it at once.

## Evidence, taken while it was stuck

- `ss -tlnp` on the server: `LISTEN 51 50 *:7719 users:(("vtmodern",...))`. The accept queue was over
  its backlog, so vtmodern wasn't taking connections.
- The server was idle (load 0.12). The main thread was waiting in its event loop
  (`poll_schedule_timeout`), not busy or blocked on a lock.
- The tablet's logcat showed `vt.remote: no answer from the server; trying again` every ~14 s for hours.
- The network was fine: the tablets pinged and showed Excellent Wi-Fi.
- The server log's last entries were all four screens disconnecting together, then nothing.

## Likely cause

`PosServer` (`modern/net/pos_server.cpp`) uses `QSslServer` with the default `maxPendingConnections`
(30), and nothing ever re-arms the listener. Our best guess: connections dropped during the TLS-PSK
handshake (screens reconnecting, unknown devices) leave `QSslServer` counting toward its pending limit
until it stops accepting. Then the kernel backlog fills. We haven't proven that part. The fact is that
the listener stopped accepting while the event loop was alive. `modern/net/standby.cpp` uses the same
listener pattern.

## The fix in the fork (commit 4d11fba)

In `PosServer::listen()`:

- `server_.setMaxPendingConnections(256)`
- a 60 s timer: if no connection has been accepted for 120 s, `server_.close()` then `server_.listen()`
  on the same address and port. Accepted sockets aren't affected. When paired screens are missing it
  logs `renewed the screens' port: N of M screens in`.
- `lastAccepted_` is updated in `PosServer::onNewConnection()`.

## If it happens again

On the server, before restarting:

    ss -tlnp | grep 7719          # Recv-Q at the backlog = the listener isn't accepting
    gdb -p $(pgrep -f -- --serve) -batch -ex 'thread 1' -ex bt

The main thread's backtrace will show whether the event loop was running or something held it.
