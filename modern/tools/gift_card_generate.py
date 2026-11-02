#!/usr/bin/env python3
"""Makes new gift card / subscription card numbers for the card printer, as a CSV.

  python3 modern/tools/gift_card_generate.py --type gift --count 500 --out ~/gift_cards_batch.csv
  python3 modern/tools/gift_card_generate.py --type subscription --count 50 --out ~/hca_members.csv

Numbers are 12 digits like the store's existing cards:
  digit 1      6 = gift card, 9 = subscription (membership) card
  digits 2-11  random (not sequential, so a card number can't be guessed from another)
  digit 12     Luhn check digit (the same rule all 1,100 existing cards follow): a mistyped number is caught
Each card also gets a random 4-digit PIN.

Never repeats a number: it checks the cards in viewtouch.db (read-only) and every batch made before, which is
recorded in ~/.local/share/ViewTouch/gift_card_batches.csv (number, type, batch, date; no PINs). Nothing is
written to viewtouch.db.

The CSV is worth real money once cards are sold: keep it on Google Drive or give it to the printer, never put it
in the Git repository (it's public).
"""
import argparse, csv, datetime, json, os, secrets, sqlite3, sys

DB = os.path.expanduser("~/.local/share/ViewTouch/ViewTouch/viewtouch.db")
REGISTRY = os.path.expanduser("~/.local/share/ViewTouch/gift_card_batches.csv")
PREFIX = {"gift": "6", "subscription": "9"}
LENGTH = 12


def luhn_digit(body):
    """The check digit that makes body + digit pass the Luhn test."""
    total = 0
    for i, ch in enumerate(reversed(body)):
        d = int(ch)
        if i % 2 == 0:          # these get doubled once the check digit is appended
            d = d * 2 - 9 if d > 4 else d * 2
        total += d
    return str((10 - total % 10) % 10)


def luhn_ok(s):
    return s.isdigit() and luhn_digit(s[:-1]) == s[-1]


def existing_numbers(db_path):
    seen = set()
    if os.path.exists(db_path):
        con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True, timeout=2)
        try:
            for number, raw in con.execute("SELECT number, json FROM gift_cards"):
                seen.add(str(number))
                try:
                    seen.add(str(json.loads(raw).get("number", "")))
                except Exception:
                    pass
        finally:
            con.close()
    if os.path.exists(REGISTRY):
        with open(REGISTRY, newline="") as f:
            for row in csv.DictReader(f):
                seen.add(row.get("number", ""))
    return seen


def make_number(prefix):
    body = prefix + "".join(str(secrets.randbelow(10)) for _ in range(LENGTH - len(prefix) - 1))
    return body + luhn_digit(body)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--type", choices=sorted(PREFIX), required=True)
    ap.add_argument("--count", type=int, required=True)
    ap.add_argument("--out", required=True, help="CSV file to write (for the card printer)")
    ap.add_argument("--batch", help="batch name (default: type and date/time)")
    ap.add_argument("--db", default=DB, help="viewtouch.db to check for existing numbers (read-only)")
    a = ap.parse_args()
    if not 1 <= a.count <= 100000:
        sys.exit("--count must be between 1 and 100000")
    out = os.path.expanduser(a.out)
    if os.path.exists(out):
        sys.exit(f"{out} already exists; choose another --out")
    batch = a.batch or f"{a.type}-{datetime.datetime.now():%Y%m%d-%H%M}"
    taken = existing_numbers(os.path.expanduser(a.db))
    prefix = PREFIX[a.type]
    rows, attempts = [], 0
    while len(rows) < a.count:
        attempts += 1
        if attempts > a.count * 50:
            sys.exit("Couldn't find enough unused numbers")
        n = make_number(prefix)
        if n in taken:
            continue
        taken.add(n)
        assert luhn_ok(n) and len(n) == LENGTH
        rows.append({"card_number": n,
                     "card_number_printed": f"{n[0:4]} {n[4:8]} {n[8:12]}",
                     "pin": f"{secrets.randbelow(10000):04d}",
                     "barcode": n,
                     "magstripe_track2": f";{n}?",
                     "card_type": "Subscription" if a.type == "subscription" else "Gift Card",
                     "batch": batch})
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader(); w.writerows(rows)
    os.chmod(out, 0o600)
    new_registry = not os.path.exists(REGISTRY)
    os.makedirs(os.path.dirname(REGISTRY), exist_ok=True)
    with open(REGISTRY, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["number", "type", "batch", "made"])
        if new_registry:
            w.writeheader()
        now = datetime.datetime.now().isoformat(timespec="seconds")
        for r in rows:
            w.writerow({"number": r["card_number"], "type": a.type, "batch": batch, "made": now})
    print(f"Wrote {len(rows)} {a.type} card numbers to {out}  (batch {batch})")
    print(f"First: {rows[0]['card_number_printed'][:4]} •••• ••{rows[0]['card_number'][-2:]}   All pass the Luhn check; none match an existing card.")
    print(f"Recorded in {REGISTRY} so future batches never repeat them.")


if __name__ == "__main__":
    main()
