#!/usr/bin/env python3
"""Build RealDMG's own Game Boy ROMs into build/gbclock/: GBTONE.GB (the
sound gate), GBSAVE.GB (battery RAM and the MBC3 clock), GBDEMO.GB (the
5150 demo), GBBANK.GB (the bank cache's gate), GBRMW.GB (the
recompiler's read-modify-writes of (HL), in tests/gbcpu.py), GBJOY.GB
(the joypad interrupt, in tests/gbheader.py), GBPOP.GB (POPs out of a
ROM bank, in tests/gbcpu.py), GBPAD.GB (the buttons a frame at a time, in
tests/gbreplay.py) and GBHBLANK.GB (Pokemon Gold's frame - an HBlank
interrupt every line - for tests/rdgcore/rdgbench.py --rom,
docs/REALDMG-SPEC.md section 6.3).

    python3 tools/gbours.py [gbtone|gbsave|gbdemo|gbbank|gbrmw|gbjoy|gbpop|gbpad|gbhblank]

tools/gbprof/gbtone.asm says what it plays (tests/gbsound.py listens to it),
tools/gbprof/gbsave.asm what it counts (tests/gbsave.py reads it), and
tools/gbprof/gbdemo.asm what it shows (tests/gbdemo.py times it). rgbds is
tools/gbclock.py's, fetched and built at the same pinned commit.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gbclock  # noqa: E402

# name: the rgbfix arguments past the title (the cartridge's header)
ROMS = {
    "gbtone": [],
    "gbsave": ["-m", "0x10", "-r", "3"],    # MBC3+TIMER+RAM+BATTERY, 32KB
    "gbdemo": [],
    "gbbank": ["-m", "0x19"],               # MBC5, 1MB: every bank there
    "gbrmw": [],
    "gbflags": [],                          # (generated: tools/gbprof/gbflags.py)
    "gbjoy": [],                            # the joypad interrupt (gbheader.py)
    "gbfm": [],                             # all four channels (gbfm.py)
    "gbpop": ["-m", "0x01"],                # POPs out of a ROM bank (gbcpu.py)
    "gbpad": [],                            # a replay's buttons (gbreplay.py)
    "gbhblank": [],                         # Gold's frame: 144 STAT interrupts
}


def build(name="gbtone"):
    asm, link, fix = gbclock.rgbds()
    os.makedirs(gbclock.OUT, exist_ok=True)
    src = os.path.join(ROOT, "tools", "gbprof", name + ".asm")
    gen = os.path.join(ROOT, "tools", "gbprof", name + ".py")
    if os.path.exists(gen):                 # a generated source: into OUT
        src = os.path.join(gbclock.OUT, name + ".%d.asm" % os.getpid())
        subprocess.check_call([sys.executable, gen, src])
    # this process's own names, then one atomic rename: two rows that build
    # the same ROM at once in a soak lane shared one .o and one .GB, and one
    # of them linked the other's half-written object
    tag = ".%d" % os.getpid()
    obj = os.path.join(gbclock.OUT, name + tag + ".o")
    rom = os.path.join(gbclock.OUT, name.upper() + ".GB")
    tmp = rom + tag
    subprocess.check_call([asm, "-o", obj, src])
    subprocess.check_call([link, "-o", tmp, obj])
    subprocess.check_call([fix, "-v", "-p", "0xFF", "-t", name.upper()] + ROMS[name] + [tmp])
    os.remove(obj)
    if os.path.exists(gen):
        os.remove(src)
    os.replace(tmp, rom)
    return rom


if __name__ == "__main__":
    print(build(sys.argv[1] if len(sys.argv) > 1 else "gbtone"))
