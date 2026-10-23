#!/usr/bin/env python3
"""Boot vm/286-realdmg - the owner's mr286 with RealDMG's hard disk - and
open a cartridge off it.

    python3 tests/rdgcore/rdghd.py GAMEBOY/POKEMONG.GBC [--secs 20] [--shot OUT.png]

docs/REALDMG-SPEC.md section 18. `make realdmghd GBROMS=<dir>` builds the
disk (build/REALDMG-HD.VHD): the whole install, REALDMG/ and the user's
cartridges in GAMEBOY/, named by tools/gbromdir.py. This is rdgwin.py's boot
for it, and what rdgwin.py cannot do, the cartridges over 1MB that no floppy
holds.

A fresh run has no CMOS, so the MR BIOS 1.65 stops on its battery and then
in setup (docs/86BOX.md, AT machines start in BIOS setup once): `setup`
puts both floppies at 3.5" 1.44M and the fixed disk at TYPE 3 (615/6/17,
which is why the disk is cut to that geometry) and records it with F10.
The cartridge is opened from the keyboard - the C: window, then the
folder, then the file, each by its place in the listing (Down, then Enter)
- which the disk's own directory decides: os88disk.py writes a folder's
files in the order it is given them, and the Makefile gives them sorted.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests", "rdgcore"))
import os88box  # noqa: E402

VM = "286-realdmg"


def screen(b):
    try:
        return [bytes.fromhex(x).decode("cp437").rstrip()
                for x in b.cmd("text").get("chars", [])]
    except SystemExit:
        return []


def row(b, label):
    """the setup screen's line holding LABEL, or '' """
    for ln in screen(b):
        if label in ln:
            return ln
    return ""


def until(b, text, keyname=None, tries=20, wait=300):
    """step (tapping KEYNAME between looks) until TEXT is on the screen"""
    for _ in range(tries):
        if text in "\n".join(screen(b)):
            return
        if keyname:
            b.tap(keyname)
        b.cmd("step %d" % wait)
    raise SystemExit("rdghd: never saw %r:\n%s" % (text, "\n".join(screen(b))))


def setup(b):
    """the MR BIOS's setup, from its 'Press Any Key': floppies 1.44M, the
    fixed disk type 3, F10 to record and exit. Each step waits for the
    screen it wants, and each field is found by its label: the BIOS takes a
    key early as nothing, and its pages keep no fixed rows"""
    b.cmd("step 2000")
    until(b, "Summary", "return", wait=1000)
    until(b, "Floppy Drive Configuration", "right")
    b.tap("down")
    b.cmd("step 300")
    for label in ("Floppy 0", "Floppy 1"):
        for _ in range(12):
            if "1.4" in row(b, label):
                break
            b.tap("space")
            b.cmd("step 150")
        else:
            raise SystemExit("rdghd: %s never read 1.44M: %r" % (label, row(b, label)))
        b.tap("down")
        b.cmd("step 150")
    b.tap("esc")
    b.cmd("step 300")
    until(b, "Fixed Disk 80", "right")
    b.tap("down")
    b.cmd("step 300")
    for _ in range(50):
        if "Type ........... 3 " in row(b, "Type ..."):
            break
        b.tap("space")
        b.cmd("step 150")
    else:
        raise SystemExit("rdghd: the MR BIOS never offered type 3:\n" + "\n".join(screen(b)))
    b.tap("esc")
    b.cmd("step 300")
    b.tap("f10")


def boot(name="rdghd", keep=False):
    """launch, set the BIOS up if it asks, and run to the desktop"""
    vhd = os.path.join(ROOT, "build", "REALDMG-HD.VHD")
    if not os.path.exists(vhd):
        raise SystemExit("rdghd: no %s - make realdmghd GBROMS=<dir>" % vhd)
    b = os88box.launch(VM, name=name, hold=True, fresh=not keep)
    try:
        for _ in range(30):
            b.cmd("step 500")
            t = "\n".join(screen(b))
            if "Press Any Key" in t or "Battery" in t:
                setup(b)
                break
        b.cmd("step 25000")                 # the hard disk's boot, to the desktop
    except BaseException:
        b.cmd("quit")
        raise
    return b


DRIVE_C = (592, 165)            # the C: icon, under A: and B:
ROW0 = (160, 127)               # a fresh Disk window's first entry (list view)
ROWH = 16


def key83(name):
    """the Disk window's order: the 8.3 name padded to eleven, as on disk"""
    stem, _, ext = name.upper().partition(".")
    return stem.ljust(8) + ext.ljust(3)


def open_game(b, path):
    """C:, then the folder by its row (folders list first, sorted), then the
    file by its place in the folder - Down that many times, Enter - from the
    VHD's own directory, which says what the window lists"""
    folder, name = path.upper().split("/")
    b.to(*DRIVE_C)
    b.click(2)
    b.cmd("step 5000")
    folders = sorted(["APPS", "GAMEBOY", "GAMES", "MEDIA", "REALDMG", "SYSTEM"])
    b.to(ROW0[0], ROW0[1] + ROWH * folders.index(folder))
    b.click(2)
    b.cmd("step 5000")
    src = {"GAMEBOY": os.path.join(ROOT, "build", "realdmghd", "games")}.get(folder)
    names = sorted(os.listdir(src), key=key83) if src else []
    if name not in names:
        raise SystemExit("rdghd: %s is not on the disk (%s)" % (path, ", ".join(names)))
    b.to(*ROW0)                             # the first entry selected, so
    b.click(1)                              #   the arrows move a selection -
    b.cmd("step 2500")                      #   and past the double-click's
    for _ in range(names.index(name) + 1):  #   time, or the first Down is lost;
        b.tap("down")                       #   +1 for the folder's '..', first
        b.cmd("step 300")
    b.tap("return")
    b.cmd("step 3000")
    return name


def opened(b, name):
    """the running cartridge's own title (0134h, the view's bank 0) against
    the file's, once the package has a view - a wrong row opens another game
    and measures it under this one's name"""
    import rdgwin
    seg, view, core = rdgwin.find(b)
    rom = open(os.path.join(ROOT, "build", "realdmghd", "games", name), "rb").read(0x150)
    want, got = rom[0x134:0x143], b.memr(view * 16 + 0x134, 15)
    if want != got:
        raise SystemExit("rdghd: opened %r, not %s (%r)" % (got, name, want))
    return seg, view, core


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("path", nargs="?", default="")
    ap.add_argument("--secs", type=float, default=0)
    ap.add_argument("--shot", default="")
    a = ap.parse_args()
    b = boot()
    try:
        if a.path:
            name = open_game(b, a.path)
            opened(b, name)
            b.cmd("step %d" % int(a.secs * 1000))
        if a.shot:
            b.shot(a.shot)
    finally:
        b.cmd("quit")


if __name__ == "__main__":
    main()
