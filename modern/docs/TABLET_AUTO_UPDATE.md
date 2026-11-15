# Tablets that update themselves

*Added 2026-11-15 by Bob (Claude). Owner: Kevin. Stores: BHDAN (Danbury retail), HCA (the bar).*

## What it does

A ViewTouch tablet set up as a kiosk (the app is the **device owner**, set by `vt-add-tablet`) now
updates itself. Nobody has to tap anything, plug in a cable or use adb.

1. A newer app is put on the store's Pi. `vt-server` copies the newest arm64-v8a / armeabi-v7a APKs
   from `apks/` into the updates folder (`~/.local/share/ViewTouch/ViewTouch/updates`).
2. In its `welcome`, the server offers that update to every screen that is behind.
3. The tablet downloads the update on its own, on a second connection (the fast, resumable path
   from 0d2061e, checked with sha256). People can keep using the tablet while it downloads.
4. Once nobody has been signed in for two 15-second checks in a row, and no guest self-order is open,
   the tablet installs the update **silently** through `PackageInstaller`.
   Android closes ViewTouch, installs the new version and reopens it (`MY_PACKAGE_REPLACED` →
   `Kiosk.boot`). Because it waits for that idle moment, an order is never cut off.

Screens that aren't device owners don't change: Fire TV billboards, phones, desktop builds and
tablets that were never provisioned. They show the usual strip, a manager taps **Update Now**, and
Android asks "Install?".

## Why

adb over Wi-Fi (`adb tcpip 5555`) is lost every time a tablet restarts, so pushing builds from the Pi
kept needing a USB cable (HCA 1–3, 2026-11-15). A device-owner app can install its own updates
without asking anyone, which takes adb out of normal updates altogether. adb is still useful for
setting up a tablet and for repairs.

## Where it lives

| Part | File | What changed |
|---|---|---|
| Silent install | `modern/android/src/org/viewtouch/pos/Updater.kt` | `silent()` returns whether the app is the device owner. `install()` uses a `PackageInstaller` session (`USER_ACTION_NOT_REQUIRED` on API 31+) and falls back to the old install intent if that fails. `UpdateInstalled` receives the result and shows Android's confirm screen if Android still asks. |
| Reopen after install | `Kiosk.kt` (`KioskBoot`), `AndroidManifest.xml` | `KioskBoot` handles `MY_PACKAGE_REPLACED` the same way as boot. Adds the `UpdateInstalled` receiver (not exported). |
| Client | `modern/net/remote_session.{hh,cpp}`, `app/pos_session.hh` | `autoUpdate()` sends `getUpdate` with `fast: true, auto: true` and does **not** open the file when it arrives. `updateInfo()` gains `silent: true` on device-owner tablets. If the store refuses or the download fails, nothing is shown on screen; it tries again on the next connection. Calling `getUpdate()` while the file is ready installs it. |
| Server | `modern/net/pos_server.{hh,cpp}` `sendUpdate(c, fast, byItself)` | A **paired** device (with a `terminalId`) that asks the fast way with `auto` gets a ticket without a manager signed in. Without `auto` it still needs a manager, and the old path in parts always does. |
| Screen | `modern/ui/qml/Main.qml` (`updateStrip`, Timer `autoUpdate`) | Downloads the update, waits until the tablet is idle, then installs it. Tries once per file. If the app is still running afterwards, the install didn't work, and the manager's **Install** button is still there. The strip text says what is happening. |
| Test | `modern/tests/test_updates.cpp` "Updates by themselves…" | A screen with nobody signed in gets the update when it asks by itself, and doesn't open it. Asking by hand without `auto`, or the old way, is still refused. |

## Security notes

- Only devices the store has paired can connect at all (TLS-PSK with the device key). The ticket is
  tied to that device, and the device only gets the update the store chose for its own platform and
  build. Asking by itself doesn't let it do anything else.
- Android only accepts a silent self-update signed with the **same key** as the installed app. A
  wrong or tampered APK fails to install, and the old version keeps running.

## Rolling it out

The first build with this feature has to be installed the old way, once per tablet: a manager taps
**Update Now**, or install it over USB. After that, each new build installs itself. On older
servers a tablet asking by itself is told no, quietly, so update the Pi (`git pull` + `vt-server`)
before or together with the tablets.

## Checking it on a tablet

```
adb logcat -s ViewTouch | grep update     # "update: installing … by itself"
```

On the Pi, Manager → Network lists each screen's build.

## Known limits / next ideas

- An install that fails isn't reported back to the server. You can see it on the tablet: the strip
  shows **Install** again. A later version could send the status in `hello`.
- There's no "only after hours" window yet. Idle means nobody signed in, so a quiet moment during
  business hours counts. A store setting for quiet hours would be easy to add on top.
