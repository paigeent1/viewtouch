# Moving the store Pis from Debian's Qt 6.8 to the official Qt 6.12

Owner decision 2026-11-13: the fork moves to the official Qt 6.12 (as official ViewTouch did on
2026-11-02, commit 4f08fd8) **before BHDAN goes live**, so it can keep taking official fixes.

Phase 1 (done on branch `helper/qt612`): the fork builds and passes its tests on Qt 6.12 on GitHub
(`modern-checks-qt612.yml`), and the tablet APKs (arm64-v8a, armeabi-v7a) build on Qt 6.12
(`modern-android.yml`). The code still builds on Qt 6.8 (`modern-checks.yml`), so the Pis keep working
until each one is moved.

This page is the plan for the Pi step. **Nothing here has been done on a Pi yet.** The scripts for it are
ready (section 3): on a Pi, `vt-qt612 install` does the move and `vt-qt612 off` undoes it.

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

`vt-qt612 install` (section 3) installs whichever of these are missing (it leaves installed ones at
their version), runs get-qt.sh and makes the checks below. By hand, the same is:

```
sudo apt-get install -y --no-upgrade 7zip curl python3 libsm6 libice6 libpulse0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-xkb1 libxkbcommon-x11-0 libgl-dev libegl-dev libssl-dev && sudo sh ~/viewtouch-main/modern/tools/get-qt.sh && ls /opt/qt/6.12.0/lib/cmake/Qt6/Qt6Config.cmake; beep3
```

Checks before building (things CI can't see for the Pi; `vt-qt612 install` makes them and says
OK / PROBLEM / WARNING for each):

- The screen plugins in `/opt/qt/6.12.0/plugins/platforms/`: Wayland (`libqwayland*.so`) and X11
  (`libqxcb.so`) both there, and `ldd` finds every library they need (on aarch64 the X11 one needs
  `libsm6`/`libice6`). Both, because Qt picks one or the other depending on how ViewTouch is started.
  A PROBLEM here stops `vt-qt612 install` before anything is switched.
- `/opt/qt/6.12.0/plugins/multimedia/libffmpegmediaplugin.so` (the FFmpeg backend) there and loading,
  with Qt's own FFmpeg libraries (`lib/libav*`, `lib/libsw*`). Debian's `qt6-ffmpeg-multimedia-plugin` /
  GStreamer plugin are **not** used by the official Qt. If the backend is missing, video on the customer
  display won't play (pictures and GIFs still work): a WARNING, the move goes on.

## 3. The scripts (`scripts/bin`): one site setting picks the Qt

Done (branch `helper/pi-qt612`). With `QT_PREFIX` not set, every script does exactly what it did before.

- **`vt-site-lib`**: `site.conf` key `QT_PREFIX`. Empty / not set: Debian's Qt, `VT_BUILD=$VT_REPO/modern/build`.
  `/opt/qt/6.12.0`: `VT_BUILD=$VT_REPO/modern/build-qt612` (`build-qt<major><minor>` from the folder's name).
  `vt_configure [--fresh]` runs cmake for `$VT_BUILD`, adding `-DCMAKE_PREFIX_PATH=$QT_PREFIX` when it is set.
  `vt-site set QT_PREFIX /opt/qt/6.12.0` refuses a folder with no Qt in it; `vt-site set QT_PREFIX ""` clears it.
- **`vt-server`**: stops (leaving the running ViewTouch alone) if `QT_PREFIX` names a folder with no Qt.
  The Debian Qt Multimedia steps (`qt6-multimedia-dev`, `qt6-base-private-dev`, `qt6-ffmpeg-multimedia-plugin`,
  the `Qt6Multimedia_DIR ... NOTFOUND` re-check) run **only when `QT_PREFIX` is empty**. With `QT_PREFIX` set
  it makes `modern/build-qt612` the first time (full build, about 15-25 minutes on a Pi 5; ccache doesn't
  help across Qt versions), and again with `--fresh` if that folder was made for another Qt folder. It then
  builds `$VT_BUILD`, copies `$VT_BUILD/vtmodern` to `/usr/local/bin/vtmodern` and starts `$VT_BUILD/vtmodern`.
- **`vt-start`**, **`vt-fast`**, **`vt-manual-shots`**, **`vt-web-reports`**, **`vt-add-tablet`**,
  **`vt-add-billboard`** (pairing), **`vt-status`** / **`vt-check-tablets`** (build number): use `$VT_BUILD`.
  `vt-fast` no longer hard-codes `~/viewtouch-main/modern/build`.
- **`vt-install`** (new Pis): configures `$VT_BUILD` with `vt_configure` (a new Pi has no `QT_PREFIX`: Debian's
  Qt, as before). Moving a new Pi to 6.12 is `vt-qt612 install` afterwards.
- **`vt-pi-updates`**: unchanged. Its `SENSITIVE` list keeps holding back `qt6|libqt6|qml6` updates: on a 6.12
  Pi those Debian packages are the rollback.
- **How the 6.12 program finds its Qt** (nothing is set when it starts): CMake writes `/opt/qt/6.12.0/lib`
  into the program as its RUNPATH (the build folder's rpath, kept in the copy in `/usr/local/bin`); Qt's own
  libraries carry `$ORIGIN` RUNPATHs, and Qt finds its plugins (`/opt/qt/6.12.0/plugins`: screens, SQLite,
  images, TLS, multimedia) and QML (`/opt/qt/6.12.0/qml`) beside its own libraries. Official ViewTouch's
  packages do the same with Qt in `/usr/lib/vtmodern` (RUNPATH `$ORIGIN/../lib` plus a qt.conf). So
  `/opt/qt/6.12.0` must stay where it is. With `QT_PREFIX` set the scripts also drop any `QT_PLUGIN_PATH` /
  `QML_IMPORT_PATH` / `QML2_IMPORT_PATH` from the environment, so Debian's 6.8 plugins can't be mixed in.
- **`vt-qt612`** (new), for the owner:
  - `vt-qt612 install`: (1) backs up every `.db` in the data folder to `~/vt-backups/before-qt-6.12.0-<time>/`
    (SQLite's online backup, checked); (2) installs the missing packages of section 2 (`--no-upgrade`);
    (3) `sudo sh modern/tools/get-qt.sh 6.12.0 /opt/qt/6.12.0`; (4) the checks of section 2 (a PROBLEM stops
    here, nothing switched); (5) `vt-site set QT_PREFIX /opt/qt/6.12.0` and `vt-server`, then `vt-qt612 status`.
    If the build or the start fails, it sets `QT_PREFIX` back to empty and runs `vt-server` on Debian's Qt.
  - `vt-qt612 off`: `QT_PREFIX` empty, `vt-server` (Debian's Qt, `modern/build`). `/opt/qt/6.12.0` stays.
  - `vt-qt612 status`: the setting, and which `libQt6Core` the running `vtmodern` really loaded
    (`/proc/<pid>/maps`; when it isn't running, what `ldd` says the build would load).

Not needed for the Pi step: `modern/packaging/*` (the .deb) still builds with Debian's Qt; official
ViewTouch's packages carry Qt 6.12 inside (4f08fd8). Bring that in later if the stores move to .debs.

## 4. Order of the move

1. Merge `helper/qt612` to `main` once checks (6.8 and 6.12) and Android are green. The 6.8 Pis are not
   affected (the code builds on both).
2. Merge `helper/pi-qt612` (section 3). With `QT_PREFIX` unset, every Pi behaves exactly as today.
3. On a test Pi or **HCA** first (not BHDAN), after `git pull`: `vt-qt612 install` (it backs up the database,
   installs, checks, switches and rebuilds; about 20-30 minutes in all the first time). The first time it
   is run by its full path: `vt-server` links new `vt-*` commands into `/usr/local/bin` afterwards.

   ```
   cd ~/viewtouch-main && git pull myfork main && ~/viewtouch-main/scripts/bin/vt-qt612 install; beep3
   ```

4. Check on that Pi (see section 6), a full day of use, then **BHDAN** before it goes live (same command).
5. Tablets: the APKs from `modern-android.yml` are Qt 6.12 already; install them as usual
   (vt-tablets / the updater). The server and tablets talk ViewTouch's own protocol, not Qt's, so a 6.12
   tablet works with a 6.8 server of the same build and the other way round; still update both together.

## 5. Rollback

- `vt-qt612 off`: back to Debian's Qt 6.8 and `modern/build`, which 6.12 never touched (it is rebuilt from
  the same commit if the code moved on; it needs nothing new). `vt-qt612 status` says which Qt is running.
- If even that fails: `vt-start` runs whatever is in the build folder of the current setting
  (`modern/build/vtmodern` after `vt-qt612 off`).
- Nothing to uninstall. Once a Pi has run 6.12 for a few weeks, `/opt/qt/6.12.0` and `modern/build`
  can both stay; removing Debian's Qt is a later, separate decision.
- The database: same commit = same database on either Qt. Rolling back to an **older commit** is the
  usual risk (a newer database may not open in older code): that is what the backup in
  `~/vt-backups/before-qt-6.12.0-<time>/` is for.

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
