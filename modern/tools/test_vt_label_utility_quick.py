#!/usr/bin/env python3
"""Checks the label utility's Quick Label modes with no window (what ViewTouch's tablets use):
--quick-rolls, --quick-preview and --quick-job, in a throw-away home folder (the shipped templates,
no ViewTouch database, so the printer is PDF Preview and nothing reaches a real printer).

    python3 modern/tools/test_vt_label_utility_quick.py

Needs Pillow and reportlab (as the utility itself); not tkinter.
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

UTILITY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vt_label_utility.py")


class QuickLabelHeadless(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.home = self.tmp.name
        self.prefs = os.path.join(self.home, ".local/share/ViewTouch/quick_label.json")

    def tearDown(self):
        self.tmp.cleanup()

    def run_utility(self, *args):
        env = dict(os.environ, HOME=self.home)
        env.pop("DISPLAY", None)   # never a window
        return subprocess.run([sys.executable, UTILITY, *args], env=env, capture_output=True, text=True, timeout=120)

    def job(self, data):
        path = os.path.join(self.home, "job.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f)
        return path

    def rolls(self):
        out = os.path.join(self.home, "rolls.json")
        r = self.run_utility("--quick-rolls", "--out", out)
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(out, encoding="utf-8") as f:
            return json.load(f)

    def test_rolls_list_every_label_size_with_its_printer(self):
        got = self.rolls()
        self.assertTrue(got["rolls"])
        for roll in got["rolls"]:
            self.assertEqual(set(roll), {"key", "name", "size", "printer"})
            self.assertTrue(roll["size"].endswith(" in"))
        self.assertIn(got["last"], [r["key"] for r in got["rolls"]])
        # Without --out: the same JSON on standard output.
        r = self.run_utility("--quick-rolls")
        self.assertEqual(json.loads(r.stdout.strip().splitlines()[-1]), got)

    def test_preview_is_the_printed_picture(self):
        roll = self.rolls()["rolls"][0]["key"]
        png = os.path.join(self.home, "label.png")
        r = self.run_utility("--quick-preview", self.job({"lines": ["cosmic", "", ""], "roll": roll}), "--out", png)
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(png, "rb") as f:
            self.assertEqual(f.read(8), b"\x89PNG\r\n\x1a\n")
        # Nothing typed: nothing to draw.
        r = self.run_utility("--quick-preview", self.job({"lines": ["  ", ""], "roll": roll}), "--out", png + "2")
        self.assertEqual(r.returncode, 2)

    def test_print_remembers_the_roll_never_the_text(self):
        got = self.rolls()
        roll = got["rolls"][-1]["key"]
        out = os.path.join(self.home, "result.json")
        r = self.run_utility("--quick-job", self.job({"lines": ["cosmic", "dust", "SECRET-WORD", "a 4th line"],
                                                      "roll": roll, "copies": 2}), "--out", out)
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(out, encoding="utf-8") as f:
            result = json.load(f)
        self.assertTrue(result["ok"])
        self.assertEqual(result["copies"], 2)
        with open(self.prefs, encoding="utf-8") as f:
            prefs = json.load(f)
        self.assertEqual(prefs["roll"], roll)
        self.assertNotIn("SECRET-WORD", json.dumps(prefs))
        self.assertEqual(self.rolls()["last"], roll)          # the tablets and the Pi start on it next time
        try:
            with open("/tmp/vt_label_utility.log", encoding="utf-8", errors="replace") as f:
                self.assertNotIn("SECRET-WORD", f.read())
        except FileNotFoundError:
            pass

    def test_nothing_typed_prints_nothing(self):
        out = os.path.join(self.home, "result.json")
        r = self.run_utility("--quick-job", self.job({"lines": ["", " "], "roll": "", "copies": 1}), "--out", out)
        self.assertEqual(r.returncode, 2)
        with open(out, encoding="utf-8") as f:
            self.assertFalse(json.load(f)["ok"])

    def test_limits(self):
        sys.path.insert(0, os.path.dirname(UTILITY))
        import vt_label_utility as u   # imports without tkinter or a display
        self.assertEqual(u.quick_job_lines(["x" * 60, "a\tb\nc", None, "four"]), ["x" * 40, "abc", ""])
        self.assertEqual(u.quick_job_lines("not a list"), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
