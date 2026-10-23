#!/usr/bin/env python3
"""REALDMG's Machine > Controls... on the glass (docs/REALDMG-SPEC.md 14.6).

    python3 tests/gbkeys.py [--only vga|cga]

Boots os8088 on MartyPC with a B: holding REALDMG.O88, dmg-acid2 (as
ACID2.GB) and an empty SYSTEM\\APPDATA, opens the game, then Machine >
Controls..., and drives the window by its own layout:

  * a click on the A box makes it WAIT - the box goes white on black - and
    the next key typed (Q) is A's: rw_keymap, read out of the package's
    segment, says 10h where it said 2Dh, and the box is black on white again;
  * a RESERVED key is refused: B waits, F1 is typed, B keeps Z and still
    waits; Esc stops the wait with nothing changed;
  * a key ANOTHER button has changes places: B takes Q, and A gets Z;
  * the GAME reads the new map: with RealDMG raised again, Q held is B in
    S_JOY (bit 5) and Z held is A (bit 4), and X - A's shipped key - is
    nothing at all;
  * REALDMG.CFG keeps it: version 3, 144 bytes, the eight keys at 136, off
    the floppy as the guest left it (os88flush);
  * Default puts the shipped map back.

On the CGA's 200-line desktop it also checks the window is whole - its
frame the template's height, nothing cut to fit above the dock.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402
import os88flush  # noqa: E402
from gbwindow import find_seg, label_off, S_FRAMES  # noqa: E402

MACHINES = {
    "vga": "os8088_xt_vga_720b",
    "cga": "os8088_5150_cga_720b_gla",
}
S_JOY = 0x4000 + 0x3D
SHIPPED = bytes([0x4D, 0x4B, 0x48, 0x50, 0x2D, 0x2C, 0x39, 0x1C])
BOX_A = (166, 58)               # rwkeys.inc's rw_kboxes, in the content
BOX_B = (120, 72)
BOX_DEF = (152, 106)
KH = 134                        # RW_KH


def code_off(lst, name):
    for line in open(lst, errors="replace"):
        m = re.match(r"\s*\d+\s+([0-9A-F]{8})\s+\S+\s+%s:" % name, line)
        if m:
            return int(m.group(1), 16)
    sys.exit("gbkeys: %s not in %s" % (name, lst))


def until(m, cond, guest=8):
    """CONDITION on the guest's clock: what a key or click changes is drawn
    by the UI task, and a save to the floppy may stand between."""
    for _ in range(guest * 4):
        if cond():
            return True
        M.guest_sleep(m, 0.25)
    return cond()


class Box:
    def __init__(self, ui, m, kind):
        self.ui, self.m, self.kind = ui, m, kind

    def origin(self):
        w = self.ui.window("RealDMG Controls")
        return w.x + 1, w.y + os88ui.geom.TITLE_H, w

    def click(self, box, w=42):
        x0, y0, _ = self.origin()
        self.ui.mo.click(x0 + box[0] + w // 2, y0 + box[1] + 6)
        M.guest_sleep(self.m, 1)

    def lit(self, box, w=42):
        """The share of the box's interior that is DARK: a waiting box is
        white on black, so most of it."""
        x0, y0, _ = self.origin()
        if self.kind == "vga":
            W, H, data = self.m.fbuf(0)
            px = [sum(data[((y0 + y) * W + x0 + x) * 3:((y0 + y) * W + x0 + x) * 3 + 3]) < 384
                  for y in range(box[1] + 1, box[1] + 11) for x in range(box[0] + 1, box[0] + w - 1)]
        else:
            vw, vh, rows = self.m.vram(self.kind)
            px = [rows[y0 + y][x0 + x] == 0
                  for y in range(box[1] + 1, box[1] + 11) for x in range(box[0] + 1, box[0] + w - 1)]
        return sum(px) / len(px)


def run(kind, disk, viewoff, mapoff):
    img = os.path.join(ROOT, "build", "os8088-720.img")
    with os88ui.boot(img, apps=disk, machine=MACHINES[kind]) as ui:
        m = ui.m
        ui.path("B:/ACID2.GB")
        seg, view = None, 0
        for _ in range(60):
            M.guest_sleep(m, 2)
            seg = seg or find_seg(m, b"REALDMG", viewoff)
            if seg:
                view = int.from_bytes(m.read(seg * 16 + viewoff, 2), "little")
            if view and int.from_bytes(m.read(view * 16 + S_FRAMES, 2), "little") >= 30:
                break
        if not view:
            return "the package never started a game"
        keymap = lambda: bytes(m.read(seg * 16 + mapoff, 8))
        if keymap() != SHIPPED:
            return "the map is not the shipped one at the start: %s" % keymap().hex()
        ui.menu_pick("Machine", "Controls...")
        kw = ui.wait_window("RealDMG Controls")
        M.guest_sleep(m, 3)                     # (its zoom, and its paint)
        if kind == "cga" and kw.h != KH + os88ui.geom.TITLE_H + 2:
            return "the window was cut to %d rows tall" % kw.h
        b = Box(ui, m, kind)
        if os.environ.get("GBKEYS_SAVE"):
            W, H, data = m.fbuf(0)
            M.write_png_rgb(os.environ["GBKEYS_SAVE"] + "-%s.png" % kind, W, H, data)
        idle = b.lit(BOX_A)
        b.click(BOX_A)
        wait = b.lit(BOX_A)
        if os.environ.get("GBKEYS_SAVE"):
            W, H, data = m.fbuf(0)
            M.write_png_rgb(os.environ["GBKEYS_SAVE"] + "-%s-click.png" % kind, W, H, data)
        print("gbkeys %s: A box dark %.2f idle, %.2f waiting" % (kind, idle, wait))
        if not (idle < 0.4 < 0.6 < wait):
            return "a click on A did not make it wait (%.2f -> %.2f)" % (idle, wait)
        if os.environ.get("GBKEYS_SAVE"):
            W, H, data = m.fbuf(0)
            M.write_png_rgb(os.environ["GBKEYS_SAVE"] + "-%s-wait.png" % kind, W, H, data)
        m.key("KeyQ")
        if not until(m, lambda: keymap()[4] == 0x10):
            return "Q did not become A: %s" % keymap().hex()
        if not until(m, lambda: b.lit(BOX_A) < 0.4):
            return "A still waits after its key"
        b.click(BOX_B)                          # a reserved key: refused
        m.key("F1")
        M.guest_sleep(m, 1)
        if keymap()[5] != 0x2C or b.lit(BOX_B) < 0.6:
            return "F1 was not refused: %s, B dark %.2f" % (keymap().hex(), b.lit(BOX_B))
        m.key("Escape")
        if not until(m, lambda: b.lit(BOX_B) < 0.4) or keymap()[5] != 0x2C:
            return "Esc did not stop the wait"
        b.click(BOX_B)                          # A's key: they change places
        m.key("KeyQ")
        if not until(m, lambda: keymap()[4:6] == bytes([0x2C, 0x10])):
            return "B did not take Q from A: %s" % keymap().hex()
        print("gbkeys %s: map %s" % (kind, keymap().hex()))
        if os.environ.get("GBKEYS_SAVE"):
            W, H, data = m.fbuf(0)
            M.write_png_rgb(os.environ["GBKEYS_SAVE"] + "-%s-swapped.png" % kind, W, H, data)
        M.guest_sleep(m, 3)                     # (the save after the draw)
        cfg = os88flush.Flush(marty=m).volume(1).read("SYSTEM/APPDATA/REALDMG.CFG")
        if len(cfg) != 144 or cfg[2] != 3 or cfg[136:144] != keymap():
            return "REALDMG.CFG does not keep it: %d bytes, version %d, %s" % (
                len(cfg), cfg[2] if len(cfg) > 2 else -1, cfg[136:144].hex())
        ui.raise_window(ui.window("RealDMG"))   # the game reads the map
        joy = {}
        for key in ("KeyQ", "KeyZ", "KeyX"):
            m.key(key, down=True, up=False)
            M.guest_sleep(m, 1)
            joy[key] = m.read(view * 16 + S_JOY, 1)[0]
            m.key(key, down=False, up=True)
            M.guest_sleep(m, 1)
        print("gbkeys %s: S_JOY Q %02X, Z %02X, X %02X" % (kind, joy["KeyQ"], joy["KeyZ"], joy["KeyX"]))
        if (joy["KeyQ"], joy["KeyZ"], joy["KeyX"]) != (0x20, 0x10, 0x00):
            return "the game does not read the new map"
        ui.menu_pick("Machine", "Controls...")
        ui.wait_window("RealDMG Controls")
        b.click(BOX_DEF, 58)
        if not until(m, lambda: keymap() == SHIPPED):
            return "Default did not put the map back: %s" % keymap().hex()
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", choices=sorted(MACHINES))
    a = ap.parse_args()
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/getgbtest.py")],
                          stdout=subprocess.DEVNULL)
    rompath = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools/getgbtest.py"),
                                       "--path", "dmg-acid2.gb"], text=True).strip()
    lst = os.path.join(ROOT, "build", "realdmg.lst")
    viewoff = label_off(lst, "rdg_viewseg")
    mapoff = code_off(lst, "rw_keymap")
    tmp = tempfile.mkdtemp(prefix="gbkeys-")
    rom = os.path.join(tmp, "ACID2.GB")
    with open(rompath, "rb") as i, open(rom, "wb") as o:
        o.write(i.read())
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools/os88disk.py"), "-o", disk,
                           "--size", "720", "--folder", "SYSTEM/APPDATA",
                           os.path.join(ROOT, "build", "realdmg.o88"), rom],
                          stdout=subprocess.DEVNULL)
    fails = []
    for kind in sorted(MACHINES):
        if a.only and kind != a.only:
            continue
        err = run(kind, disk, viewoff, mapoff)
        print("%-4s gbkeys %s%s" % ("ok" if not err else "FAIL", kind, "" if not err else ": " + err))
        if err:
            fails.append(kind)
    print("gbkeys: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
