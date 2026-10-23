"""Tracker's screenshots, for tools/guide/manuals/TRACKER_O88.md.

No shipped 360KB disk carries Tracker and a module together (BEVERLY.MOD is
on the media disk), so this one makes its own: run the script itself first,
then the capture on a VGA XT with a Sound Blaster.

    python3 tools/guide/capture/TRACKER_O88.py   # writes build/guide/tracker360.img
    python3 tools/guide/capture/run.py TRACKER_O88 \\
        --apps build/guide/tracker360.img --machine os8088_xt_vga_sb
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))), ""))
import os88marty  # noqa: E402

TITLE = "Tracker"
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
DISK = os.path.join(ROOT, "build", "guide", "tracker360.img")


def build():
    with tempfile.TemporaryDirectory() as tmp:
        bev = os.path.join(tmp, "BEVERLY.MOD")
        ode = os.path.join(tmp, "ODE.MOD")
        with open(os.path.join(ROOT, "apps", "tracker", "beverly.mod"), "rb") as f:
            open(bev, "wb").write(f.read())
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mkmod.py"), ode],
                       check=True)
        os.makedirs(os.path.dirname(DISK), exist_ok=True)
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                        "-o", DISK, "--size", "360",
                        "APPS:" + os.path.join(ROOT, "build", "tracker.o88"),
                        "MEDIA:" + bev, "MEDIA:" + ode], check=True)
    print(DISK)


def _menu_shot(ui, shot, title, sid):
    cells = ui.menus()
    cell = [c for c in cells if c[0].upper() == title.upper()][0]
    _, x0, x1, items = cell
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 2.0)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6), settle=False)
    ui.mo.to(600, 460, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    # A double-click on a module opens Tracker on it and plays it.
    ui.path("B:/MEDIA/BEVERLY.MOD")
    ui.wait_window(TITLE)
    print("menus:", [(c[0], c[3]) for c in ui.menus()])
    ui.mo.to(620, 460)
    os88marty.guest_sleep(ui.m, 40.0)     # the module is read whole first
    shot("tracker-window", window=TITLE, pad=8, settle=False)

    # File > Add to PlayList... puts a second module on the list.
    ui.menu_pick("File", "Add to PlayList...")
    ui.chooser_open("ODE.MOD")
    os88marty.guest_sleep(ui.m, 2.0)
    ui.m.key("KeyE")                    # the PlayList editor
    pl = ui.wait_window("PlayList")
    os88marty.guest_sleep(ui.m, 2.0)
    x, y, ww, hh = ui._rect(pl.i)
    shot("tracker-playlist", rect=(x - 8, y - 8, ww + 18, hh + 18), settle=False)
    ui.m.key("KeyE")                    # E again hides it
    os88marty.guest_sleep(ui.m, 2.0)
    ui.raise_window(ui.window(TITLE))

    _menu_shot(ui, shot, "Rate", "tracker-rate-menu")

    # Full screen. On an XT, XT mode is on and the full screen is text.
    ui.m.key("KeyF")
    os88marty.guest_sleep(ui.m, 6.0)
    shot("tracker-textscreen", settle=False)
    ui.m.key("Escape")
    os88marty.guest_sleep(ui.m, 3.0)

    # XT mode off: the full screen is the graphics pattern editor.
    ui.m.key("KeyX")
    os88marty.guest_sleep(ui.m, 3.0)
    ui.m.key("Enter")
    os88marty.guest_sleep(ui.m, 2.0)
    ui.m.key("KeyF")
    os88marty.guest_sleep(ui.m, 8.0)
    shot("tracker-fullscreen", settle=False)
    ui.m.key("Escape")
    os88marty.guest_sleep(ui.m, 3.0)


if __name__ == "__main__":
    build()
