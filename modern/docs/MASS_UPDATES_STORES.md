# Mass Updates and other stores (design note, 2026-11-24)

Mass Updates (Menu Builder > Mass Updates…, `app/pos_mass_update.cpp`, `core/mass_update.hh`) changes many
retail items at once. The owner asked to "plan for other stores now". This note covers what is built and
what comes later. **Built:** change sets, export to a file, import at another store, and (2026-11-26) sending
them straight to another store over the network, with inventory transfers on the same link (below).

## Who may change the price book

- Changing items, prices or costs from Mass Updates needs the `pricebook` permission (`perm::PriceBook`,
  `core/employee.hh`). Admins have it. A manager can open the screen, find items and preview a change, but
  Apply, Undo, cost previews and importing a change set ask for an admin's PIN (`PosService::requirePriceBook`, the
  usual approval pad).
- The owner (2026-11-24): "Eventually this will be a corp function managing the entire organization."
  This permission is the hook for that later **corporate office**. A head office would hold `pricebook` for
  every store and the stores would not. Each store's change sets would then come from the corporate office,
  by the same import path as now (and later by sending, below). Nothing else needs to change: every change
  to the price book already goes through this one permission and this one change set format.

## The change set (what every Apply makes)

The format is `viewtouch-change-set`, version 1, JSON. Mass Updates keeps one per Apply in
`<data>/mass-updates/change-sets/<id>.vtchanges.json`, beside `viewtouch.db`, so it outlives a restart.
Store Updating > Export writes a copy to the device's "ViewTouch Exports" folder.

```json
{ "format": "viewtouch-change-set", "version": 1, "id": "20261124-101500-1a2b3c4d",
  "store": "<store name>", "label": "Margin 40% · round 9 up", "who": "<employee>", "why": "<typed>",
  "at": 1795000000000, "from": "<the store it first came from>", "undoOf": "",
  "changes": [ { "upc": "012345678905", "item": "<name>", "field": "price", "before": "10.00", "after": "10.69" } ] }
```

- Changes are **keyed by UPC** (the item's first barcode). Item ids differ between stores. When a file is
  imported, each UPC is looked up among all of that store's barcodes: with or without leading zeros, an
  11-digit UPC with its check digit added. UPCs that aren't found are listed in the preview and left out.
- The fields are `price` and `cost` (plain amounts, an empty cost means none), `category` (by **name**,
  since ids differ too), `taxClass`, `soldOut` and `noDiscount` (yes/no), and `buttonColor`.
- At import, "before" is what the receiving store has now, not what the file says. The preview shows that
  store's own old → new. An item already at the new value isn't listed.
- An Undo is a change set too (`undoOf` names the one it undid), so another store can be sent the undo.
- Every applied change also goes to the Changes report (`logChange`): price and cost lines one by one, up
  to 2,000 per change set, then one summary line.

## Sending to the other stores directly (built 2026-11-26, branch helper/store-transfers)

Two stores of the same owner, each with its own Pi and database: the retail store (the price book) and the bar
(which sells the same retail items, plus its own drinks and food). Owner's decisions: inventory transfers both
ways; the retail store's price changes flow to the bar by themselves, **one way**; the bar's own hospitality
prices never go anywhere.

1. **Store list.** Store Settings > Other stores: *This store's short name* (letters and digits: the files name
   the stores by it), the *Other stores* rows (short name as that store calls itself, the ssh address
   `vt-update-stores` uses, Transfers on/off, Price changes *Not sent* / *Sent there*), *Take retail prices from*
   (the one store this one takes prices from: the bar names the retail store) and *Its price changes are applied*
   (Automatically, or When someone applies them on Mass Updates). No store name is in the code.
2. **The link.** Everything goes as a file: ViewTouch writes it to `<data>/transfers/outbox/<STORE>/` (`<data>` is
   the folder of `viewtouch.db`) and starts `scripts/bin/vt-send-transfers` detached (on each new file and about
   once a minute while files wait), so a slow or down link never holds the register. The script copies each file
   over ssh (the `vt-update-stores` keys; no new port or password) into the other Pi's `<data>/transfers/inbox/`
   under a temporary name and renames it there, then moves it to `<data>/transfers/sent/<STORE>/`. A file that
   can't go stays in the outbox (the screens say *Waiting to send* / *Couldn't reach X*). Addresses: ViewTouch
   writes `<data>/transfers/stores.conf` from the store list; `OTHER_STORES` in `vt-site` is the fallback. The
   store's ViewTouch server reads its inbox every 20 seconds (`PosService::transfersTick`); files read go to
   `inbox/done/` (refused ones to `inbox/refused/`, change sets kept for a person to `inbox/waiting/`).
3. **Inventory transfers** (Manager > Inventory > Transfers…, `app/pos_transfers.cpp`, `core/transfer.hh`):
   retail items only (a UPC, `itemIsRetail`); scanned, typed or found; Send takes them off on hand at once, keeps a
   transfer record (table `transfers`, both directions), prints a slip and sends `viewtouch-transfer` v1:
   `{format, version: 1, kind: transfer | result | void, store: <sender's short name>, transfer: {key: "<FROM>-<n>",
   number, from, to, who, note, at, status, lines: [{upc, name, qty, cost (cents each), price, packUpc, packSize,
   category, received, reason}]}}`. The receiving store matches each line by UPC (leading zeros, 11-digit UPC-A),
   else a carton by its pack's UPC times the pack size; a UPC not on file is added from the line (name, price,
   cost, category) or left out. Receiving sends a `result` back; voiding a transfer not yet received sends a
   `void` (or just removes the file when it hadn't gone yet). Retail on hand is kept per item (table
   `retail_stock`) from an item's first transfer; sales take it down. There is no retail count yet, so the sending
   store's numbers start at 0 and go negative until retail receiving / counting exists.
4. **Price changes.** Every change set made at a store (Mass Updates Apply and Undo, the Menu Builder card's price or
   cost save, the ILSA price book sync) goes to each store marked *Sent there*, as the same `viewtouch-change-set`
   file with `store` and `from` the sender's short name: retail items' price, cost and category only. The
   receiving store takes change sets only from its *Take retail prices from* store, keeps each UPC's **base price**
   (`<data>/transfers/base-prices.json`), applies its **price zone** to the price and either applies by itself
   (open checks keep their prices; the Changes report says *Menu, from <store>*) or keeps it for *Prices from X (n)…*
   on Mass Updates. A change set applied from another store is never sent on (`from` set: no loops). A retail
   price changed at the bar itself is not sent back; Mass Updates says the source's next change will replace it.
5. **Price zone** (`PosSettings::priceZone`, Mass Updates > Price zone…): a name, a markup in percent (0 allowed)
   and a rounding (None, or up to the next .49 / .99: 10.12 -> 10.49, 10.50 -> 10.99). Price = base x (1 + %/100),
   then rounded; costs are never marked up. *Reapply to all retail items now* previews every retail item's price
   from its base (an item with no base yet: its own price becomes its base at Apply), then Apply (price book
   permission). The base shows as its own column on Mass Updates ("BH price") and on the Menu Builder card. The
   bar's *OTHER STORE'S PRICE* (retail Discounts page, command `storePrice`) gives a regular the source's price:
   one discount line per retail line, "<store> price", discount permission, allowed on No discounts items
   (cigarettes: the source's price is at or above the state minimum).
6. **Never over a sale.** Applying changes only the menu. Open checks keep their prices.

## Customers shared store to store (TEMPORARY, built 2026-11-26, branch helper/customer-sync)

**Remove when the corporate customer list exists** ("One customer across all stores" in the notes): delete
`app/pos_customer_sync.cpp`, `core/customer_sync.hh`, `ui/qml/CustomerSyncPanel.qml`, `tests/test_customer_sync.cpp`
and every line marked *temporary* / *remove when the corporate customer list exists* (the `shareCustomers` flag
of `OtherStore` and its Store Settings column, the hook in `saveCustomerRecord`, the `viewtouch-customers` case in
`transferIncoming`, the `customerSync` property and its two commands), and `<data>/transfers/customer-sync.json`.

- **Turned on** per store: Store Settings > Other stores > *Share customers* (*Not shared* / *Shared*), at both
  stores for each other. It uses the same link as transfers (a store with Transfers off but Share customers on is
  still written to `stores.conf` as `on`, so the script sends its files).
- **What goes:** name, phone, email, address, note, the *"<store> price"* switch and the preferential percent, and
  the numbers of the gift / subscription cards linked to the customer (the other store links its copy of a card,
  if it has one and it isn't linked yet). **What never goes** (each store's own): the house account (on/off, limit,
  balance, ledger), points, visits, spent, the last order, the privacy notice acceptance. Customers without a
  phone number (fewer than 7 digits) aren't shared.
- **When:** each save of a customer here that changes one of those fields (a visit or points don't) writes
  `customers-<FROM>-<ms>-<n>.json` to `outbox/<STORE>/` for every store marked *Shared*; it waits there while the
  link is down. *Send All Customers Now* (Manager > Customers, managers) is the first share: every customer with a
  phone, in files of 500.
- **The file:** `{format: "viewtouch-customers", version: 1, store: <sender>, at, all?, customers: [{id (the
  sender's), version (goes up with each change there), changedAt, fields: {<field>: {value, at, from}}, cards}]}`.
  Each field carries when it was last changed and at which store.
- **The merge** (`core/customer_sync.hh`): by the phone's digits (a leading US 1 dropped), or the customer this
  store linked to that sender's id before. Per field the newest change wins (a tie: the store name that sorts last);
  a name or phone is never emptied; the same number written another way isn't a change; a version already taken is
  skipped. Nothing is ever deleted (Forget at one store forgets there only; that customer stays forgotten there).
  **Same phone, clearly different name** (no word in common, no first name the start of the other): nothing is
  merged; it waits on *Manager > Customers > Check These Customers* for a manager: **Merge** (the same person: the
  newest of each field) or **Keep Apart** (a customer of their own here; later changes go to that one).
- **No loops:** a change taken from another store is never sent on (each store sends only changes made at it).
- **State:** `<data>/transfers/customer-sync.json` (each field's last change, the links, the ones to check).
  Customers already on file when it's first read count as changed when they were added.

## Corporate Pi later (not built)

The corporate office would be one more entry in each store's Other stores list, and nothing else changes:

- **Prices.** Each store's *Take retail prices from* would name the corporate Pi instead of the retail store; the
  corporate Pi makes change sets (with `pricebook`, the one permission that gates price changes) and sends them to
  every store marked *Sent there*. The files, the base prices and each store's price zone stay as they are. A
  store's zone (`{name, markup, round}` in the settings) is the shape a corporate Pi would own and send later
  (per-category or per-item overrides would be more fields of it).
- **Transfers.** Every file names its sender (`store`) and the transfer its `from` and `to`, so a corporate Pi can
  sit in the middle: receive from one store, keep the master record (every store's in transit), and pass the file
  on unchanged. No format change.
- **Customers.** The planned single customer list ("One customer across all stores" in the notes) would use the
  same link. Anything built store-to-store for customers before then is a stand-in, to be removed when the
  corporate customer list exists (today: *Customers shared store to store* above).

## Buydowns and promotions between stores (2026-11-25)

Manufacturer buydowns and promotional sales (Mass Updates > Buydowns / Promotions, `app/pos_buydowns.cpp`,
`core/buydown.hh`) travel the same way as change sets: by UPC, never by this store's item ids.

- **Export.** *Send to stores (file)…* writes one definition as `Buydown - <name> - <date>.vtbuydown.json`:
  `{format: "viewtouch-buydown", version: 1, store, exportedAt, buydown: {type, name, receiptText, amount (cents per
  carton), taxBefore, usePartCtn, style, qty, start, end ("YYYY-MM-DD HH:MM", the store's local time), manufacturer,
  rptCode, account, items: [{upc, partCtn, name}]}}`. The item ids and this store's count of units used are left out.
- **Import.** *Import…* opens it as a new definition (a new ID here). Each UPC is looked up the way a scan is (11/12/13
  digits); UPCs not found are listed and left out. Nothing is saved until Apply, which is the price book permission.
- **Corporate later.** A head office would make the definition once (Copy to New each quarter), export it and send the
  file to every store, the same path as change sets above (and the same permission). A store's own Part/Ctn edits
  stay with the store; a later file replaces nothing by itself (it is always a new definition to check and Apply).
