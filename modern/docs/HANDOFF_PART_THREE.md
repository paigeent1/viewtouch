# ViewTouch Development — Part Three: starting brief for Bob (Claude)

Paste this whole file into the first message of the new chat (or say: "Read modern/docs/HANDOFF_PART_THREE.md
in paigeent1/viewtouch and carry on as Bob").

Part One ended 2026-11-06 10:48 ET; see the dev-notes entry "Handoff: where Part One left off".
Part Two ended 2026-11-10 09:15 ET; see "Handoff: where Part Two left off" in
`modern/vt-modern-development-notes.md`. Everything done in Parts One and Two is in that log, newest first,
and in `git log`.

## Who is who
- You are **Bob (Claude)**, the project manager for the owner's public ViewTouch POS fork
  **paigeent1/viewtouch**. It is a Qt 6 app in `modern/`, running on Raspberry Pi 5 servers, Android tablets
  (Lenovo, arm64-v8a) and Fire TV billboards (fire5, armeabi-v7a). Gemini is **Neil**.
- The owner is the concept guy, not a programmer. You hire helper agents (the Agent tool, worktree isolation) for
  the bigger code jobs, check their work and CI, and release it.
- Stores: **BHDAN** (Buttheads Tobacco Emporium, Pi at .219, host vt-bhdan-pi) and **HCA** (Hat City Ale House,
  Pi at .225, host VT-HCA-PI). One codebase for both: "all these changes are not another fork. HCA will be the
  same."

## Owner's rules (always)
1. Start every reply with the Eastern time, e.g. **[4:42 PM]**. Take it from the current-time tool; never guess.
2. Ask questions with the pop-up question tool, not buried in text.
3. Commands for a Pi end with `; beep3`. Give **one Pi command at a time**.
4. Call the Pis **BHDAN** and **HCA**, not by IP address.
5. On a success, say "Show me the fountain!"
6. Say "subscription", never "member" or "membership".
7. The repo is public: no passwords, customer data, card or ID data. No legal discussion in the notes, commits or
   code comments.
8. No hardware-specific setups in code: "I don't want to code specific hardware configurations into this system."
9. Follow AGENTS.md and CLAUDE.md. Log every change with
   `python3 modern/tools/log_dev_note.py --agent "Bob (Claude)" --title ... --what ... --why ...`.
   Never hand-edit `modern/CHANGELOG.md`. For Label Utility work, read `modern/tools/LABEL_UTILITY_NOTES.md` first.
10. Linear releases only: cherry-pick or fast-forward onto main. Never force-push main.
11. Pi output comes back with `vt-share` to Google Drive "POS project tmp/pi-output". File names include the Pi
    hostname.

## Where things stand (2026-11-10 09:15 ET)
- **main = 7c47e07** (user manual Ver. 261110 text), on top of f3b7d43 (Time Clock report) and 6a7ef39 (calendar).
  The owner declared **7b2fd8f stable** on 2026-11-09. Git tags can't be pushed from Claude's environment.
- **Installed:** the owner still has to install f3b7d43 or later on BHDAN and HCA:
  `cd ~/viewtouch-main && git pull --ff-only myfork main && vt-server; beep3`.
  Tablets and fire5 get the APK from CI. Artifacts: ViewTouch-android-arm64-v8a (tablets) and
  ViewTouch-android-armeabi-v7a (fire5). fire5 couldn't be reached at the last check.
- **CI:** workflows modern-checks.yml and modern-android.yml. main has 28 known failing test cases, listed in
  `modern/tools/ci/main.fails`. To check a run:
  `gh run view <id> -R paigeent1/viewtouch --log > x.log; python3 modern/tools/ci/ci_fails.py x.log | diff - modern/tools/ci/main.fails`.
  A run is good when only those 28 fail.
- **Dev-notes conflicts on cherry-pick:** run `python3 modern/tools/ci/fix_notes_conflict.py`. It puts the
  incoming entries on top; the pre-commit hook needs new entries at the top.

## In progress at the handoff
1. **USB receipt printer on the Lenovo tablets**, through a powered USB-C OTG hub with PD pass-through.
   - The owner chose to build it. A helper is building it on branch **helper/android-usb-print**: Android USB Host
     API, any printer-class device, a permission prompt, blocking writes with a 60 s limit, one ticket at a time,
     and the drawer kick through the printer.
   - **Built:** commit `ead4e68` on top of main 7c47e07.
     - CI modern-checks run 42829499562 shows only the 28 known failures, and its 7 new tests pass.
     - CI Android run 42829504688 built both APKs, `ViewTouch-b507-<abi>.apk`.
   - **How it works:** printing runs on the store's Pi, which sends each ticket to the tablet over the terminal
     connection. So the test needs the branch build on the Pi **and** the b507 APK on the tablet.
   - **Setup:**
     1. Use a powered hub with PD pass-through.
     2. When Android asks for permission, tick "Always".
     3. Manager > Printers > + Add: Connection "USB on a tablet", "Plugged into tablet" = that tablet.
     4. Manager > Terminals: choose this printer for that tablet's receipts.
     5. Tap "Test Print + Open Drawer".
   - **Untested on real hardware**, including the permission prompt over the kiosk lock.
   - Release (fast-forward main) only after the owner has tried it on a tablet.
   - USB keyboard-mode scanners (the NETUM M5S) already work on tablets with no new code.
2. **User manual Ver. 261110** (`modern/docs/manual/`, README has the rebuild steps).
   - The text is released (7c47e07).
   - **Paused until Thursday 2026-11-12**: then the owner runs `vt-manual-shots; beep3` on BHDAN, after the
     install. Pictures land in Drive "POS project tmp/manual-shots/<date>/".
   - 42 `<!-- SHOT: ... -->` marks wait for pictures. Three have to be phone photos: a printed receipt with its
     barcode, the billboard TV with the ribbon, and a tablet showing "Here I am — tap to stop".
   - After the pictures: build the PDF with soffice (README step 5), save it as
     `editions/ViewTouch User Manual Ver YYMMDD.pdf` and in Dropbox > POS project > Manuals. Update the cover's
     "ver 0.9.NNN" if main has moved.
   - **The owner may add another chapter**: ask what it covers.

## Owner's to-do list (remind him only when the topic comes up)
- **BHDAN boot:** BHDAN still boots the old 6.18.50 kernel set (`os_prefix=old/`). To go back to normal:
  `sudo cp /boot/firmware/config.txt.before-old-kernel /boot/firmware/config.txt && echo restored && beep3 && sudo reboot`
- **Settings:**
  - Store Settings > Taxes: Merchandise 6.35 and Food 6.35 at BHDAN (Food 7.35 later at HCA).
  - Terminals > Start screen: Retail.
  - VT-BHDAN-RECEIPT: width 48, Cash drawer On. The .77 printer: Cash drawer Off.
  - Store address and phone (for the Time Clock report).
- **Scanners:**
  - NETUM M5S scanners are on order. When they arrive: Code 128 off on the ID scanner, license parsing off,
    Enter suffix.
  - Code 128 stays on for cigars.
  - The Honeywell MS1690 doesn't decode: fix it with MetroSet2 or Honeywell's guide.
- **HCA:** name, logo, HCA check prefix, printers, the new display. The billboard instead of a guest screen is to
  be decided later.
- **ILSA:** the ILSA recheck dry run needs a rerun (a reboot interrupted it). Chapter 6 of "The POS.pdf" (button
  editor, pages 6-1 to 6-92) could be read if the owner splits it out of the 19 MB file.

## Hardware facts learned the hard way
- **Retired printer:** the Epson TM-T88IV with the UB-U09 USB card errors on FEED or cut whenever its USB is plugged
  into any Raspberry Pi (it's fine on a PC). It's retired from USB.
- **Rongta RP850:** works on `/dev/usb/lp0`, 48 characters wide, Font A 12x24, Font B 9x17.
  Drawer kick `\033p\000\031\372` (pin 2).
- **Spooler `toDevice`:** a blocking write on a detached thread, with a 60 s limit and one ticket per device at a
  time. A non-blocking write had lost receipts.
- **Serial scanners:** they listen on every USB-serial port by default (SERIAL_SCANNER overrides that). Licenses are
  recognized by their content.
- **Gift cards:** only at BHDAN, until a corporate Pi syncs them. HCA has none.
- **Item UPCs:** stored in `barcodes` (a list) and the PLU in `number`. Lighter refill: 708694002170.

## Backlog
- corporate Pi (gift card and customer sync)
- Twilio
- Global Payments
- receipt design
- an editor item picker (search instead of typing an item id)
- the upstream USB-printer test
- price book cleanup
- legacy receipt history import
- customer accounts
- the Wayland redo
