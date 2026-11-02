#!/usr/bin/env python3
"""Looks for the pattern in the store's gift card numbers and PINs, without showing any.

Reads viewtouch.db read-only and prints only counts and statistics: lengths, leading digits, which check-digit
rule (if any) the numbers follow, which digit positions never change, whether numbers run in sequence, and how
PINs relate to their numbers. Examples are masked (first 4 and last 2 digits). Nothing is written anywhere.

  python3 modern/tools/gift_card_number_report.py [path/to/viewtouch.db]
"""
import collections, json, os, sqlite3, statistics, sys

DB = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/.local/share/ViewTouch/ViewTouch/viewtouch.db")


def luhn_ok(s):
    total = 0
    for i, ch in enumerate(reversed(s)):
        d = int(ch)
        if i % 2 == 1:
            d = d * 2 - 9 if d > 4 else d * 2
        total += d
    return total % 10 == 0


def gs1_ok(s):   # UPC/EAN style: weights 3,1 from the right, excluding the check digit
    body, check = s[:-1], int(s[-1])
    total = sum(int(ch) * (3 if i % 2 == 0 else 1) for i, ch in enumerate(reversed(body)))
    return (10 - total % 10) % 10 == check


def mod11_ok(s):
    body, check = s[:-1], int(s[-1])
    total = sum(int(ch) * (i % 6 + 2) for i, ch in enumerate(reversed(body)))
    r = (11 - total % 11) % 11
    return r < 10 and r == check


def sum10_ok(s):
    return sum(int(c) for c in s[:-1]) % 10 == int(s[-1])


def mask(n):
    return n[:4] + "•" * max(0, len(n) - 6) + n[-2:] if len(n) > 6 else "•" * len(n)


def pct(a, b):
    return "%5.1f%%" % (100.0 * a / b) if b else "  -  "


def main():
    con = sqlite3.connect(f"file:{DB}?mode=ro", uri=True, timeout=2)
    cards = []
    for (raw,) in con.execute("SELECT json FROM gift_cards"):
        try:
            cards.append(json.loads(raw))
        except Exception:
            pass
    con.close()
    nums = [str(c.get("number", "")).strip() for c in cards]
    digits = [n for n in nums if n.isdigit()]
    print(f"Gift cards on file: {len(cards)}   all-digit numbers: {len(digits)}   other: {len(nums) - len(digits)}")
    if not digits:
        return

    by_len = collections.Counter(len(n) for n in digits)
    print("\nNumber lengths:", ", ".join(f"{k} digits: {v}" for k, v in sorted(by_len.items())))

    for k in (1, 2, 3, 4):
        top = collections.Counter(n[:k] for n in digits).most_common(8)
        print(f"Leading {k} digit(s):", ", ".join(f"{p}…: {c}" for p, c in top))

    print("\nCheck-digit rules (last digit). Random numbers pass each about 10% of the time:")
    for name, fn in (("Luhn (credit-card style)", luhn_ok), ("GS1/UPC weights 3-1", gs1_ok),
                     ("Mod 11", mod11_ok), ("Sum of digits mod 10", sum10_ok)):
        ok = sum(1 for n in digits if len(n) > 1 and fn(n))
        print(f"  {name:28s} {pct(ok, len(digits))}")

    main_len = by_len.most_common(1)[0][0]
    same = [n for n in digits if len(n) == main_len]
    print(f"\nDigit positions for the {len(same)} cards of {main_len} digits (1 = leftmost):")
    for i in range(main_len):
        c = collections.Counter(n[i] for n in same)
        top = ", ".join(f"{d}:{pct(v, len(same)).strip()}" for d, v in c.most_common(3))
        print(f"  pos {i + 1:2d}: {len(c):2d} different digits   most common {top}")

    vals = sorted(int(n) for n in same)
    gaps = [b - a for a, b in zip(vals, vals[1:])]
    if gaps:
        print(f"\nIn sequence: {sum(1 for g in gaps if g == 1)} of {len(gaps)} neighbours differ by exactly 1;"
              f" median gap {statistics.median(gaps):,.0f}")
        runs, run = [], 1
        for g in gaps:
            if g == 1:
                run += 1
            else:
                runs.append(run); run = 1
        runs.append(run)
        print(f"  longest run of consecutive numbers: {max(runs)};  runs of 10+: {sum(1 for r in runs if r >= 10)}")

    pins = [(str(c.get("number", "")), str(c.get("pin", "")).strip()) for c in cards]
    with_pin = [(n, p) for n, p in pins if p]
    print(f"\nPINs: {len(with_pin)} of {len(cards)} cards have one")
    if with_pin:
        print("  PIN lengths:", ", ".join(f"{k}: {v}" for k, v in sorted(collections.Counter(len(p) for _, p in with_pin).items())))
        checks = {
            "PIN = last digits of the number": lambda n, p: n.endswith(p),
            "PIN = first digits of the number": lambda n, p: n.startswith(p),
            "PIN appears inside the number": lambda n, p: p in n,
            "PIN = reversed last digits": lambda n, p: n[::-1].startswith(p),
            "PIN all digits": lambda n, p: p.isdigit(),
        }
        for label, fn in checks.items():
            print(f"  {label:34s} {pct(sum(1 for n, p in with_pin if fn(n, p)), len(with_pin))}")
        dup = sum(v - 1 for v in collections.Counter(p for _, p in with_pin).values() if v > 1)
        print(f"  PINs shared with another card: {dup}")

    types = collections.Counter(str(c.get("typeCode", "")) or "(none)" for c in cards)
    print("\nType codes:", ", ".join(f"{t}: {v}" for t, v in types.most_common(10)))
    if len(types) > 1:
        cross = collections.Counter((str(c.get("typeCode", "")) or "(none)", str(c.get("number", ""))[:1]) for c in cards)
        print("  type code × first digit:", ", ".join(f"{t}/{d}…: {v}" for (t, d), v in cross.most_common(12)))

    live = sum(1 for c in cards if int(c.get("balance", 0) or 0) > 0)
    print(f"\nWith money on them: {live}   empty: {len(cards) - live}")
    print("\nMasked examples, one per leading digit:")
    seen = set()
    for n in digits:
        if n[0] not in seen:
            seen.add(n[0]); print("  ", mask(n))


if __name__ == "__main__":
    main()
