# Mass Updates and other stores (design note, 2026-11-24)

Mass Updates (Menu Builder > Mass Updates…, `app/pos_mass_update.cpp`, `core/mass_update.hh`) changes many
retail items at once. The owner asked to "plan for other stores now". This note covers what is built and
what comes later. **Built:** change sets, export to a file, import at another store. **Not built:** sending
a change set straight to another store over the network.

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

## Later: sending to the other stores directly (not built)

`vt-update-stores` already reaches the other Pis over ssh. Sending would work like this:

1. **Store list.** Store Settings would get a list of the other stores: name, the ssh host
   `vt-update-stores` already uses, and on/off. ILSA's store checkboxes (and "Connect to Store") would be
   this list on the Store Updating tab.
2. **Send.** After Apply, "Send to the ticked stores" would copy the change set file to each store's
   `~/.local/share/ViewTouch/ViewTouch/mass-updates/inbox/` with `scp` (the same keys `vt-update-stores`
   uses; no new port, no new password). This would run from a script (`vt-send-changes`), not from inside
   ViewTouch, so a slow link never holds up the register.
3. **Receive.** Each store's ViewTouch would look in `inbox/` every minute or so. A file there would show
   on Mass Updates as "Waiting from <store>" with the same preview. Someone at that store with `pricebook`
   presses Apply, or, if Store Settings says so, it would apply by itself at a set time ("Apply Changes
   Immediately to Connected Store(s)"). Applied or refused files would move to `inbox/done/`, and the
   result would be written back to the sender's `outbox/<store>.result.json`.
4. **Never over a sale.** Applying changes only the menu (like the Menu Builder). Open checks keep their
   prices, as they already do.
5. **The corporate office.** It would be a ViewTouch (or just the script) with the store list. It would
   make change sets and send them. The stores would no longer give `pricebook` to their own staff.

Open questions for the owner: whether the stores apply automatically or wait for a person, and whether
each store may keep its own price for some items (a "local price" mark that imports skip).

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
