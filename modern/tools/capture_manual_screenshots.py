#!/usr/bin/env python3
"""Captures screenshots of the ViewTouch Label Utility for the user manual.

Opens the label utility, steps through its screens (start, items, preview, label designer,
printer settings, print window) and saves a PNG of each to /tmp/vt_label_shots.
It only looks: nothing is printed and nothing is saved to templates or ViewTouch.

Run from a terminal on the Pi's desktop (see capture_manual_screenshots.sh).
"""
import importlib.util, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = "/tmp/vt_label_shots"
TEMPLATE = "2X1 VINYL UPC_PRICE"

os.makedirs(OUT, exist_ok=True)
for f in os.listdir(OUT):
    os.remove(os.path.join(OUT, f))

spec = importlib.util.spec_from_file_location("vtl", os.path.join(HERE, "vt_label_utility.py"))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
m.save_templates = lambda *_a, **_k: None          # never change the user's templates while capturing
tk = m.tk
app = m.VTLabelUtility()


def settle(t=0.8):
    end = time.time() + t
    while time.time() < end:
        app.update()
        time.sleep(0.03)


def grab(path):
    """Full-screen screenshot: grim (Wayland), then scrot (X11), then Pillow."""
    if shutil.which("grim") and os.environ.get("WAYLAND_DISPLAY"):
        if subprocess.run(["grim", path], stderr=subprocess.DEVNULL).returncode == 0:
            return
    if shutil.which("scrot") and subprocess.run(["scrot", "-o", path], stderr=subprocess.DEVNULL).returncode == 0:
        return
    from PIL import ImageGrab
    ImageGrab.grab().save(path)


def shot(name, widget=None, pad=2):
    settle()
    path = os.path.join(OUT, name + ".png")
    grab(path)
    if widget is not None:
        from PIL import Image
        img = Image.open(path)
        # screen may be scaled relative to Tk's coordinates
        sx = img.width / float(app.winfo_screenwidth())
        sy = img.height / float(app.winfo_screenheight())
        x, y = widget.winfo_rootx(), widget.winfo_rooty()
        w, h = widget.winfo_width(), widget.winfo_height()
        box = (max(0, int((x - pad) * sx)), max(0, int((y - pad) * sy)),
               min(img.width, int((x + w + pad) * sx)), min(img.height, int((y + h + pad) * sy)))
        img.crop(box).save(path)
    print("  captured", name)


def toplevels():
    return [w for w in app.winfo_children() if isinstance(w, tk.Toplevel)]


try:
    settle(2.0)
    shot("screen1_start")

    # Screen 2: tree view
    app.select_mode_and_go("tree")
    settle()
    shot("screen2_categories")
    app.show_step(1)
    settle()

    # Screen 3: retail items, tick three (prefer one with alternate UPCs), highlight it
    app.select_mode_and_go("retail")
    settle()
    items = app.filtered_items
    with_alts = [it for it in items if len(it.get("upc_choices", [])) > 1]
    picks = (with_alts[:1] + [it for it in items if it not in with_alts[:1]])[:3]
    for it in picks:
        it["selected"] = True
        it["qty"] = 2
    app._populate_grid()
    if picks:
        app.grid_tree.selection_set(str(items.index(picks[0])))
        app.grid_tree.see(str(items.index(picks[0])))
    shot("screen3_items")

    # Screen 4: preview
    app.chosen_template_name = TEMPLATE if TEMPLATE in app.templates else list(app.templates)[0]
    app.go_next()
    settle()
    if TEMPLATE in app.templates:
        app.tpl_var.set(TEMPLATE)
    app._render_live_preview()
    shot("screen4_preview")

    # Label designer with the description selected
    app.open_report_designer()
    settle(1.5)
    des = [w for w in toplevels() if isinstance(w, m.ReportDesignerWindow)][0]
    idx = next((i for i, el in enumerate(des.tpl["elements"]) if el.get("field") == "name"), 0)
    des.selected_idx = idx
    des.selected_set = {idx}
    try:
        des.el_cb.current(idx)
    except Exception:
        pass
    des._load_selected_props()
    des._redraw_canvas()
    shot("designer_full", des, pad=0)
    bars = sorted((f for f in des.winfo_children() if isinstance(f, tk.Frame) and f.winfo_height() > 20
                   and f.winfo_width() > des.winfo_width() * 0.6), key=lambda f: f.winfo_rooty())
    for f, name in zip(bars, ["designer_toolbar1", "designer_palette", "designer_toolbar2"]):
        shot(name, f, pad=0)
    des.destroy()
    settle()

    # Printer settings window (closed without saving)
    app.open_printer_settings_dialog()
    settle(1.2)
    dlg = toplevels()[-1]
    shot("printer_settings", dlg)
    dlg.destroy()
    settle()

    # Print window (closed without printing)
    app.prompt_variable_text_if_needed = lambda tpl: None
    app.open_print_dialog()
    settle(1.2)
    dlg = toplevels()[-1]
    shot("print_window", dlg)
    dlg.destroy()
finally:
    try:
        app.destroy()
    except Exception:
        pass
print("Screenshots saved in", OUT)
