# Moving the store Pis from Debian's Qt 6.8 to the official Qt 6.12

Owner decision 2026-11-13: the fork moves to the official Qt 6.12 (as official ViewTouch did on
2026-11-02, commit 4f08fd8) **before BHDAN goes live**, so it can keep taking official fixes.

Phase 1 (done on branch `helper/qt612`): the fork builds and passes its tests on Qt 6.12 on GitHub
(`modern-checks-qt612.yml`), and the tablet APKs (arm64-v8a, armeabi-v7a) build on Qt 6.12
(`modern-android.yml`). The code still builds on Qt 6.8 (`modern-checks.yml`), so the Pis keep working
until each one is moved.

This page is the plan for the Pi step. **Nothing here has been done on a Pi yet.**

## 1. What changes and what doesn't

- Debian's Qt 6.8 packages (`qt6-*`, `qml6-*`, `libqt6*`) **stay installed**. Nothing is removed.
- The official Qt 6.12 goes **beside** it, in `/opt/qt/6.12.0` (the default of `modern/tools/get-qt.sh`).
- The 6.12 build gets **its own build folder**, `modern/build-qt612`. The 6.8 build in `modern/build`
  is left as it is: it is the rollback.
- The database, `site.conf`, tablets list, scanners and printers are not touched by the move. Qt's
  version doesn't change what ViewTouch writes in its database; the commit you build does.

## 2. Getting Qt 6.12 onto a Pi (aarch64)

`modern/tools/get-qt.sh` already knows the Pi: on `aarch64` it fetches the `linux_arm64` / `gcc_arm64`
binaries from download.qt.io, only the parts ViewTouch uses (Qt Base, Declarative, SVG, Wayland,
image formats, Lottie, Quick Timeline, Multimedia, ICU), checks each archive against Qt's SHA-1 and
unpacks into `/opt/qt/6.12.0` (about 300 MB of disk; a few minutes to download).

Extra Debian packages it and the official Qt need (none of them replace anything):

| Package | Why |
|---|---|
| `7zip`, `curl`, `python3` | get-qt.sh unpacks Qt's .7z archives and reads Qt's index |
| `libsm6`, `libice6` | the **aarch64** Qt 6.12 xcb (X11) plugin links them (official 8698838; x86_64's doesn't) |
| `libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-xkb1 libxkbcommon-x11-0` | the xcb plugin, if ViewTouch ever runs under X11 / Xwayland |
| `libwayland-client0 libwayland-cursor0 libwayland-egl1` | the Wayland plugin (the Pi desktop is Wayland); already there on Raspberry Pi OS / Debian 13 desktop |
| `libgl-dev libegl-dev` | Qt's CMake files look for OpenGL headers when building |
| `libpulse0` | the official Qt Multimedia links PulseAudio's client library (CI's link failed without it; on the Pi PipeWire's Pulse layer uses it, normally already installed) |
| `libssl-dev` | TLS (Qt's own TLS plugin loads the system OpenSSL 3) |
| `libfontconfig1 libfreetype6 libdbus-1-3 libglib2.0-0t64 libdrm2 libgbm1 libgssapi-krb5-2 libbrotli1 libzstd1 libopengl0` | libraries the official Qt binaries link; normally already installed |

Suggested commands for the owner (when the time comes; not yet):

```
sudo apt-get install -y 7zip curl python3 libsm6 libice6 libpulse0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-xkb1 libxkbcommon-x11-0 libgl-dev libegl-dev libssl-dev && sudo sh ~/viewtouch-main/modern/tools/get-qt.sh && ls /opt/qt/6.12.0/lib/cmake/Qt6/Qt6Config.cmake; beep3
```

Check before building (things CI can't see for the Pi):

- `ls /opt/qt/6.12.0/plugins/multimedia/` shows the FFmpeg backend (`libffmpegmediaplugin.so`) and
  `ls /opt/qt/6.12.0/lib/ | grep -E 'libav|libsw'` shows Qt's own FFmpeg libraries. Debian's
  `qt6-ffmpeg-multimedia-plugin` / GStreamer plugin are **not** used by the official Qt. If the
  backend is missing, video on the customer display won't play (pictures and GIFs still work).
- `ls /opt/qt/6.12.0/plugins/platforms/` shows `libqwayland*.so` / the Wayland plugin and `libqxcb.so`.

## 3. Changes the scripts in `scripts/bin` need (a separate commit, before the Pi step)

Today every script builds and runs `modern/build` with whatever Qt CMake finds first (Debian's).
Proposed, so one site setting picks the Qt:

1. **`vt-site-lib`**: a new `site.conf` key `QT_PREFIX` (not set = Debian's Qt, as today). It sets
   `VT_BUILD="$VT_REPO/modern/build"` when empty, and `VT_BUILD="$VT_REPO/modern/build-qt612"`
   (or `build-qt<version>`) when `QT_PREFIX=/opt/qt/6.12.0`. `vt-site set QT_PREFIX /opt/qt/6.12.0`
   switches a Pi; `vt-site set QT_PREFIX ""` switches it back.
2. **`vt-server`**:
   - If `$VT_BUILD/build.ninja` is missing: `cmake -S modern -B "$VT_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
     -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_PREFIX_PATH="$QT_PREFIX"` (the first 6.12 build is a
     full one: about 15-25 minutes on a Pi 5; ccache doesn't help across Qt versions).
   - The Debian Qt Multimedia block (installs `qt6-multimedia-dev`, `qt6-base-private-dev`,
     `qt6-ffmpeg-multimedia-plugin`, re-runs cmake on `modern/build`) runs **only when `QT_PREFIX` is
     empty**; the official Qt carries Multimedia itself. Its two `cmake --fresh ... -B modern/build`
     lines and the `Qt6Multimedia_DIR ... NOTFOUND` check use `$VT_BUILD`.
   - `cmake --build modern/build`, the copy to `/usr/local/bin/vtmodern` and `BIN=` use `$VT_BUILD`.
3. **`vt-start`**: `BIN="$VT_BUILD/vtmodern"` (fallback `/usr/local/bin/vtmodern` as now).
4. **`vt-fast`**: build and run `$VT_BUILD` (source vt-site-lib first; it hard-codes `build` today).
5. **`vt-install`** (new Pis): optionally run get-qt.sh and configure `modern/build-qt612` with
   `-DCMAKE_PREFIX_PATH`, once the stores are on 6.12.
6. **`vt-pi-updates`**: its `SENSITIVE` list holds back `qt6|libqt6|qml6` updates; on a 6.12 Pi those
   Debian packages no longer matter to ViewTouch, but keep them held (they are the rollback).
7. `/usr/local/bin/vtmodern` from the 6.12 build finds Qt through its RUNPATH (`/opt/qt/6.12.0/lib`,
   set by CMake in the build folder); `/opt/qt/6.12.0` must stay where it is. Qt's plugins (screens,
   SQLite, images, TLS, multimedia) come from `/opt/qt/6.12.0/plugins` and QML from
   `/opt/qt/6.12.0/qml`, not from Debian's folders.

Not needed for the Pi step: `modern/packaging/*` (the .deb) still builds with Debian's Qt; official
ViewTouch's packages carry Qt 6.12 inside (4f08fd8). Bring that in later if the stores move to .debs.

## 4. Order of the move

1. Merge `helper/qt612` to `main` once checks (6.8 and 6.12) and Android are green. The 6.8 Pis are not
   affected (the code builds on both).
2. Commit the script changes in section 3. With `QT_PREFIX` unset, every Pi behaves exactly as today.
3. On a test Pi or **HCA** first (not BHDAN): back up the database (`vt-share` / the usual backup),
   install packages + get-qt.sh (section 2), `vt-site set QT_PREFIX /opt/qt/6.12.0`, `vt-server`.
4. Check on that Pi (see section 6), a full day of use, then **BHDAN** before it goes live.
5. Tablets: the APKs from `modern-android.yml` are Qt 6.12 already; install them as usual
   (vt-tablets / the updater). The server and tablets talk ViewTouch's own protocol, not Qt's, so a 6.12
   tablet works with a 6.8 server of the same build and the other way round; still update both together.

## 5. Rollback

- `vt-site set QT_PREFIX ""` then `vt-server`: back to Debian's Qt 6.8 and `modern/build`, which is
  still there (it is rebuilt from the same commit if the code moved on; it needs nothing new).
- If even that fails: `vt-start` runs whatever is in `modern/build/vtmodern`.
- Nothing to uninstall. Once a Pi has run 6.12 for a few weeks, `/opt/qt/6.12.0` and `modern/build`
  can both stay; removing Debian's Qt is a later, separate decision.
- The database: same commit = same database on either Qt. Rolling back to an **older commit** is the
  usual risk (a newer database may not open in older code): that is what the backup in step 3 is for.

## 6. What to check on the first Pi (things CI can't test)

- Screens and touch: the main screen at the store's resolution, the customer display on the second
  monitor (`vt-guest-display`), touches land where pressed on both (`vt-touch-map`), the on-screen
  keyboard. Qt 6.12 is a different Wayland plugin build than Debian's 6.8.
- Fonts: the official Qt uses fontconfig, same fonts; check the emoji icons in the top bar.
- Printing: receipts and kitchen tickets (ESC/POS over the network), the label printer (the label
  utility is Python, not Qt, so not affected), the menu PDF and report exports.
- Scanners: USB-serial scanners (ViewTouch reads the port itself, no Qt Serial Port), keyboard-style
  scanners.
- Video on the customer display (Qt Multimedia, FFmpeg backend: see section 2), guest pictures, GIFs.
- TLS between server, standby and tablets (Qt's OpenSSL plugin, system OpenSSL 3).
- Android kiosk (separately, any time before): Kiosk mode, USB printing, the camera barcode scanner,
  Stripe, the Fire TV billboard (armeabi-v7a) all on the 6.12 APKs.
