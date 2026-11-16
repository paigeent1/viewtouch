# Tobacco scan data for Altria

ViewTouch builds the weekly **scan data file** that a store in Altria's Digital Trade Program sends to
Altria (AGDC). The file lists every sale of the store's Altria products for one week, line by line, so
Altria can check the store's prices and promotions. This page is for the owner (set-up and the weekly
routine) and for the next developer (how ViewTouch's data becomes the file).

Altria's requirements document is for trade use only, so it is **not** in this repository. Keep it with
the store's Altria papers. Nothing here replaces it.

Nothing sends by itself yet. The owner saves and sends each week by hand until Altria's Help Desk has
approved a test week.

## What it does

- **Reports → Scan Data** shows a week (Sunday 00:00 to Saturday 23:59:59, Eastern time). It starts on
  last week; ◀ ▶ go back and forth a week at a time.
  - **The file**: its name, how many records, the quantity and the dollars (final prices, before tax),
    and the file's first line.
  - **Checks before sending**: every problem found. *ERROR* lines must be fixed before the file goes out;
    *Warning* lines are worth a look.
  - Then every sale, with its lines as they'll be sent (item, UPC, quantity, discounts, final price).
- **Save File** writes the file to `~/ViewTouch/scan-data/` on the store's computer. It is named with the
  account name and the week's Saturday, like `ExampleStore20260110` (no extension).
  - It refuses while this store's scan data settings are unset or still at their defaults. Nothing can
    get past that, not even Save Anyway.
  - It refuses while the file has errors. **Save Anyway** saves it anyway (for a test week, say). The
    Changes report records that it was saved with errors.
- **Fill in Makers** (on the same screen) gives every item without a scan data maker and type one,
  from the rules in Store Settings. An item's own choice in the Menu Builder is never overwritten.
  New items from the ILSA price book get them the same way.
- `vt-scan-data send` uploads the saved file to Altria's sFTP site. `vt-scan-data status` lists the weeks
  saved and sent.

## Set-up (once per store)

1. **From the Altria representative**: the store's **Retail Control Number** (6 digits, for an
   independent store), and the store's name and address **exactly as Altria has them on file**.
2. **Store Settings → Tobacco scan data** (on that store's computer):
   - Account type *Independent*, the Retail Control Number, an account name for the file name (letters and
     digits, never changed afterwards without telling the Help Desk), the store name, street (no PO Box),
     city, state, and the full ZIP+4 with its hyphen.
   - Phone, contact and e-mail are optional.
   - Which other makers' tobacco to send too (off to start). Altria's own products are always sent. A type
     turned on is sent complete, every sale of it.
   - Which discounts the manufacturer pays for (see *How the store's data is reported* below). With
     nothing listed, every discount counts as the store's own.
   - The settings marked *(to confirm)* stay as they are until the Help Desk answers (see *Open
     questions*).
   - The status line at the top says *Ready* or what is missing.
   - These settings are kept on this computer only (`~/.config/viewtouch/scan-data-site.json`), never in
     the database. A store copied with `vt-copy-store`, another store's Pi, or the standby never gets them.
3. **Items**: Reports → Scan Data → **Fill in Makers**. The built-in rules know these UPC company
   prefixes: 028200 (PM USA cigarettes), 073100 (USSTC smokeless), 070137 (John Middleton cigars) and
   855022 (Helix on! pouches). Add NJOY's prefix, and department rules for other makers, in Store Settings
   → *Fill in makers: rules*. Then look at the items the report's checks name and fix them in the Menu
   Builder (*Scan data* on a retail item's card: maker, type, how it's sold, consumer units).
4. **From the Scan Data Help Desk**: the sFTP login. On the store's computer:

   ```sh
   mkdir -p ~/.config/viewtouch && nano ~/.config/viewtouch/scan-data.conf && chmod 600 ~/.config/viewtouch/scan-data.conf; beep3
   ```

   Put in it, one per line, what the Help Desk gives: `SFTP_HOST=...`, `SFTP_PORT=22`, `SFTP_USER=...`,
   `SFTP_PASSWORD=...` (or `SFTP_KEY=` a key file), and `SFTP_DIR=` if they name a folder. This file never
   goes in Git and is never copied to another store.

   The first time only, link the command:

   ```sh
   sudo ln -sf ~/viewtouch-main/scripts/bin/vt-scan-data /usr/local/bin/; beep3
   ```

5. **The test week**: save a full week and send it (`vt-scan-data send`). Then wait for the Help Desk's
   approval e-mail. Until then the files don't count.

## The weekly routine

The file for a week is due by the **Tuesday** after it. A corrected file can still replace it for a few
days after that (Altria's papers give the final date).

1. Monday or Tuesday: Manager → Reports → **Scan Data**. It shows last week.
2. Read **Checks before sending**. Fix every ERROR (usually an item's maker, type or UPC in the Menu
   Builder), then ◀ ▶ to show the week again.
3. **Save File**.
4. On the store's computer:

   ```sh
   vt-scan-data send; beep3
   ```

5. Check the file's status on the sFTP site. A rejected file is e-mailed to the store's technical contact.

**Sending a week again.** Every upload counts against Altria's cap: no more than two files per week of a
promotion period, or the period's scan data payment is lost. So `vt-scan-data send` refuses a week
already sent. To replace it (it must again hold *all* that week's sales):

```sh
vt-scan-data send 2026-01-10 --again; beep3
```

Tell the Help Desk before sending several old weeks at once.

## New location / chain checklist

Every new store (Gaylordsville / BHG, or any other) sets up and tests its own Altria site info before it
counts. It never reuses another store's settings.

- [ ] Ask the Altria representative for the new store's own Retail Control Number, and its name and address
      as Altria has them on file.
- [ ] On the new store's computer: Store Settings → Tobacco scan data, filled in for that store.
- [ ] Its own sFTP login from the Help Desk, in its own `~/.config/viewtouch/scan-data.conf`.
- [ ] Fill in Makers on its items, and fix what the checks name.
- [ ] A test week sent and **approved by the Help Desk** before its files count.

**If Altria makes the owner a chain again** (several stores under one Management Account):

- [ ] Recheck every store's settings. Account type *Chain*: field 1 becomes the 5-digit **Management
      Account Number**.
- [ ] Each store needs its **Store Number** (field 6) as Altria has it.
- [ ] Confirm with Altria whether each store sends its own file or one combined file goes for all. Today
      ViewTouch makes one file per store.
- [ ] A new test week for each store, approved by the Help Desk.

## Open questions for the Scan Data Help Desk

None of these is answered yet. The matching settings keep their defaults and are marked *(to confirm)*.

1. **The sFTP login**: host, port, user name and password (or key), and the folder to put files in.
2. **The summary line's 4th value**: the store's name (the default, as a store sending its own file) or the
   software's name. Store Settings → *Summary line name (to confirm)*.
3. **The encoding and line endings**: plain ASCII or UTF-8, CRLF or LF. Defaults: ASCII and CRLF. Store
   Settings → *File letters* and *Line endings (to confirm)*.
4. **How to send the test week and get it approved**: which week, where to put it, and how long approval
   takes.
5. **Whether voided lines are left out.** ViewTouch leaves out voided lines, error corrects, cancelled
   sales and training checks. Returns are sent as negative lines.

## How the store's data is reported (for the next developer)

Code: `modern/core/scan_data.{hh,cpp}` (plain C++: the file, its checks, the fill rules, week dates),
`modern/app/pos_scan_data.cpp` (settings file, Fill in Makers, the report, Save File),
`modern/ui/qml/WidgetReportView.qml` (Reports → Scan Data), the Menu Builder card
(`WidgetMenuBuilder.qml`), `scripts/bin/vt-scan-data`. Tests: `modern/tests/test_scan_data.cpp` (core) and
`test_scan_data_app.cpp`.

- **Which sales**: closed checks whose close time is in the week. Training checks, cancelled sales, voided
  lines, error corrects, gift cards and fees are left out.
- **Which lines**: items whose maker is an Altria company (PM USA, USSTC, John Middleton, Helix, NJOY):
  always. Other makers: only for the product types turned on, and then all of them. **CBD is never sent.**
  An item with an Altria UPC prefix but without an Altria maker is an error.
- **Records**: one per item line of each sale. Lines of one sale are merged only when the UPC, the price
  and every discount per unit are the same.
- **Transaction**: field 5 is the check number. Fields 3 and 4 are the check's close date and time
  (Eastern), the same on all its lines.
- **How it's sold** (fields 17 and 19): from the item's *Scan data* choice, else from its pack/carton link.
  An item that packs or cans are linked to is a *Carton* (cigarettes), a *Roll* (smokeless and pouches) or
  a *Box* (others), holding as many units as the link says (10 and 5 usually). Otherwise it is a *Pack*,
  a *Can* or *Each*, 1 unit.
- **Returns**: a line with a negative quantity is sent with negative quantity (18) and negative final
  price (31). Money refunded after a check closed isn't by item, so the file shows that sale as rung, with a
  warning.
- **Final price (31)**: the line's total less its share of every discount, **before sales tax**.
- **Per-pack buydowns** (the Menu Builder's *Buydown*): already in the shelf price, so they show only as
  that lower price. Multi-unit indicator *N*, no discount fields.
- **Discounts**, sorted by who pays, by their names in Store Settings (a manual discount's name, or a
  promotion's name, id or coupon code):
  - *Manufacturer multi-unit deals* → fields 20–22. 20 = Y on each line in the deal, 21 = the consumer
    units in it (whole buy-N groups only for a buy-N-get-M promotion), 22 = the whole deal's amount,
    repeated on each of its lines. Each line's final price carries its share, split by value with the
    leftover cents to the largest remainders, so the shares add up exactly. A line outside the deal is N.
  - *Manufacturer coupons* → 25/26 (a coupon promotion's code if it has one, else its name).
  - *Other manufacturer discounts* → 27/28.
  - Everything else (manager discounts, the store's promotions and coupons, rewards, staff meals) is the
    store's own → 23/24.
  - A whole-check discount is shared over the lines it covered (a promotion: only its items).
- **Not used**: loyalty fields (29, 30, 37, 38) and the manufacturer offer fields (40, 41). The store has no
  Altria loyalty program. 35–36 and 43–45 are blank.
- **Age check (39)**: an ID scanned is *Scanned ID*; a birth date typed by hand is *Manual Entry*;
  otherwise blank. **No date of birth or ID data is ever in the file.**
- **Purchase type (42)**: *In Store* for counter, table and tab sales; blank for takeout, delivery and the
  kiosk.
- **Text**: no `|`, no line breaks; ASCII only unless the setting says UTF-8; cut to each field's length.
  Money always has two decimals and no `$`. UPCs are 12 digits with the leading zero (an 11-digit UPC
  gets it back, 13/14-digit codes lose their extra zeros, UPC-E is expanded).
- **Settings checks**: an unset or sample-looking Retail Control Number, account name, store name, street
  (and no PO Box), state, ZIP+4 (all nine digits and the hyphen); chain mode needs the 5-digit number and a
  Store Number.
