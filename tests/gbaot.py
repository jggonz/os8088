#!/usr/bin/env python3
"""The full static recompiler's gate (REALDMG-SPEC section 20, tools/gbaot.py).

    python3 tests/gbaot.py [-k NAME]

Every ROM below is translated WHOLE on the host - walked from its entries,
no play - by RealDMG's own translator (tools/gbaot/aothost.asm under
Unicorn), and then run beside the interpreter with the same buttons
pressed. The two must agree on every frame: the registers, "now", the events
and the M-cycles skipped, and 8000-FFFF. A ROM that reports on its link port
(Blargg's, gbflags' sum) is held to what it says instead, because where an
interrupt lands differs between any two of RealDMG's arms there - the
interpreter's NOP checks for an event and translated code's does not - and
only its verdict is the ROM's.

The ROMs are ours (tools/gbprof, built by tools/gbours.py with the pinned
rgbds) and the test suites tools/getgbtest.py fetches at pinned SHA-256s.
None is committed, and no commercial game is used: those are the owner's
field measurement (docs/reports/REALDMG-FULLSTATIC-2026-10-25.md).

FIVE NEGATIVE CONTROLS, every one of which must go red:
  - the most-entered block of GBRMW.GB with its first byte changed: the
    check must see the precompiled code at all;
  - GBFLAGS.GB translated by a driver built with RT_DEADBUG (a flag's read
    unseen by the dead-flags lookahead, REALDMG-SPEC 16.1.2): its sum must
    differ from the interpreter's, so the gate sees a translator defect;
  - GBBANK.GB run with a driver built with RDG_NOTABFILT (the filter tr_ejf
    asks first without the table's blocks, REALDMG-SPEC 20.9): it must enter
    fewer precompiled blocks, the one thing that defect changes;
  - GBBANK.GB with every block in the list (the table left empty, so the
    search and its cache are what run) under a driver built with
    RDG_AOTCACHEBUG (the cache's bank key forgotten): it must differ;
  - GBSEAM4.GB with tr_ejf's choice of DS taken out of the precompiled core
    (a JR from the bank back to a bank-0 PC with no block): it must differ.
"""
import argparse
import os
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gbaot  # noqa: E402
import gbstatic  # noqa: E402

BUILD = os.path.join(ROOT, "build")
OURS = ["gbrmw", "gbpop", "gbhblank", "gbbank", "gbsave", "gbjoy", "gbpad", "gbtone", "gbflags",
        "gbseam", "gbseam2", "gbseam3", "gbseam4", "gbseam5", "gblcdoff"]
# (label, ROM, frames, compare the link port's words rather than frames)
SUITE = [
    ("GBDEMO", "apps/realdmg/GBDEMO.GB", 1500, False),
    ("GBRMW", "gbrmw", 1500, "dyn"),     # (the recompiler's machine: below)
    ("GBPOP", "gbpop", 1500, False),
    ("GBHBLANK", "gbhblank", 1500, False),
    ("GBBANK (64 banks of code)", "gbbank", 1500, False),
    ("GBSAVE", "gbsave", 600, False),
    ("GBJOY", "gbjoy", 600, False),
    ("GBPAD", "gbpad", 600, False),
    ("GBTONE", "gbtone", 600, False),
    ("GBFLAGS (sum)", "gbflags", 3000, True),
    ("GBSEAM (across 3FFF)", "gbseam", 600, True),
    ("GBSEAM2 (on at 4000)", "gbseam2", 600, True),
    ("GBSEAM5 (JR at 3FFF)", "gbseam5", 600, True),
    # the LCD off for good: the harness's frame is a frame's time then,
    # where it waited for a VBlank (Prehistorik Man waiting on a button)
    ("GBLCDOFF (dark for good)", "gblcdoff", 300, True),
    # a JR across the seam: the plain interpreter's has no setds (REALDMG-SPEC
    # 3.1), so these are held to the dynamic recompiler's frames
    ("GBSEAM3 (JR into the bank)", "gbseam3", 600, "dyn"),
    ("GBSEAM4 (JR out of it)", "gbseam4", 600, "dyn"),
    ("cpu_instrs", "cpu_instrs/cpu_instrs.gb", 4000, True),
    ("instr_timing", "instr_timing/instr_timing.gb", 600, True),
    ("mem_timing", "mem_timing/mem_timing.gb", 1500, False),
    ("halt_bug", "halt_bug.gb", 1500, False),
    ("dmg-acid2", "dmg-acid2/dmg-acid2.gb", 1500, False),
]


def rom_path(name, built):
    if name.endswith((".GB", ".gb")) and "/" in name and name.startswith("apps/"):
        return os.path.join(ROOT, name)
    if name in built:
        return built[name]
    import getgbtest
    return os.path.join(getgbtest.DEST, name)


# HRAM's routines made blocks from what a heat run found there (REALDMG-SPEC
# 20.5): GBRMW rewrites its HRAM routine once it is running, so its guard
# must fail and the interpreter take the new bytes - and before that it
# passes. No other ROM of ours, nor any test suite here, runs code in HRAM
HRAMLEG = ("GBRMW",)


def hram_leg(rom, found, extra, out, frames, ref="interp"):
    import json
    h = gbaot.heat_run(BUILD, rom, gbaot.load(out), frames, log=lambda *x: None)
    path = out + "-heat.json"
    json.dump(h, open(path, "w"))
    hram = {}
    heat, more = gbaot.load_heat([path], hram)
    if not hram:
        return False, "no HRAM PC went to the interpreter: the leg tests nothing"
    f2, e2 = gbaot.walk(rom, set(extra) | more)
    img = gbaot.translate(BUILD, rom, gbaot.leaders(rom, f2, e2), log=lambda *x: None,
                          heat=heat, hram=hram)
    made = sorted(pc for pc, key in img["blocks"] if pc >= 0xFF80)
    if not made:
        return False, "%d HRAM PCs counted and none translated" % len(hram)
    if not img.get("hlinked"):
        return False, "%d HRAM blocks and no exit linked to one: the guard on a " \
            "linked entry is untested" % len(made)
    out2 = out + "-hram"
    gbaot.save(img, out2, rom)
    ok, what = gbaot.compare(BUILD, rom, gbaot.load(out2), frames, ref=ref)
    return ok, "%d HRAM blocks (%s), %d exits linked to them; %s" % (
        len(made), " ".join("%04X" % p for p in made), img["hlinked"], what)


def straddle():
    """A block start whose first instruction runs past 3FFFh is the
    interpreter's (gbaot.fits): Looney Tunes' JP NZ at 3FFE, its high byte
    the switched-in bank's, came in as a miss and spun the machine for ever.
    A 32KB ROM that falls from BIT 6,A at 3FFC into it, the miss named as a
    seed: the walk finds it, and no entry may be made there"""
    d = bytearray(0x8000)
    d[0x100:0x103] = b"\xC3\xFC\x3F"          # JP 3FFC
    d[0x3FFC:0x4000] = b"\xCB\x77\xC2\x81"    # BIT 6,A / JP NZ,xx81 (xx at 4000)
    d[0x4000] = 0x01
    rom = gbstatic.Rom(bytes(d))
    found, extra = gbaot.walk(rom, {(0, 0x3FFE)})
    lead = gbaot.leaders(rom, found, extra)
    if (0, 0x3FFE) not in found:
        return "the walk did not find the straddling instruction: the check tests nothing"
    if (0, 0x3FFE) in lead:
        return "a block start at 3FFE, whose JP's high byte is at 4000h"
    return None


def ejf_control(rom, out, frames):
    """compare() against the dynamic recompiler with tr_ejf's two loads of DS
    NOPped out of the precompiled core"""
    at = None
    for ln in open(os.path.join(BUILD, "gbaot", "rdgcore.map")):
        p = ln.split()
        if len(p) == 3 and p[2] == "tr_ejf":
            at = int(p[0], 16)
    orig = gbaot.Machine.init

    def init(self, *a, **k):
        img = k.get("core_image")
        if img is not None:
            b = bytearray(img)
            n, i = 0, at
            while i < at + 48:
                if b[i:i + 3] == b"\x36\x8e\x1e":     # mov ds,[ss:...]
                    b[i:i + 5] = b"\x90" * 5
                    n += 1
                i += 1
            assert n == 2, n
            k["core_image"] = bytes(b)
        return orig(self, *a, **k)
    gbaot.Machine.init = init
    try:
        return gbaot.compare(BUILD, rom, gbaot.load(out), frames, ref="dyn")
    finally:
        gbaot.Machine.init = orig


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-k", help="only the rows whose label contains this")
    a = ap.parse_args()
    import subprocess
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "getgbtest.py")])
    import gbours
    built = {n: gbours.build(n) for n in OURS}
    work = tempfile.mkdtemp(prefix="gbaot-")
    bad = 0
    t0 = time.time()
    why = straddle()
    print("gbaot: a block across 3FFFh: %s" % ("FAIL: " + why if why else "refused, ok"))
    bad += why is not None
    for label, name, frames, serial in SUITE:
        if a.k and a.k not in label:
            continue
        path = rom_path(name, built)
        rom = gbstatic.Rom(open(path, "rb").read())
        found, extra = gbaot.walk(rom)
        img = gbaot.translate(BUILD, rom, gbaot.leaders(rom, found, extra), log=lambda *x: None)
        out = os.path.join(work, label.split()[0])
        gbaot.save(img, out, rom)
        # GBRMW's W16s run the NOP delay (w16_ff, REALDMG-SPEC 16.6), where
        # the interpreter takes events at instructions translated code does
        # not (20.6): its frames are held to the dynamic recompiler's, which
        # shares the idioms' tails, and its own verdict - every idiom
        # against its JP NZ twin - to the interpreter's, below
        ref = serial if isinstance(serial, str) else "interp"
        serial = serial is True
        ok, what = gbaot.compare(BUILD, rom, gbaot.load(out), frames, ref=ref, serial=serial)
        if ok and not serial and " 0 precompiled blocks" in " " + what.split("; ", 1)[-1]:
            ok, what = False, "the precompiled code never ran: " + what
        print("%-4s %-28s %d blocks, %d code bytes; %s" % ("ok" if ok else "FAIL", label,
              len(img["blocks"]), img["code_bytes"], what))
        bad += not ok
        if label == "GBRMW":
            ok, what = gbaot.compare(BUILD, rom, gbaot.load(out), 2000, serial=True)
            ok = ok and "Passed" in what
            print("%-4s %-28s %s" % ("ok" if ok else "FAIL", "GBRMW's verdict", what))
            bad += not ok
        if label == "GBRMW" and not a.k:
            (pc, key), n = gbaot.hottest(BUILD, rom, gbaot.load(out))
            which = "%02X:%04X" % (key, pc)
            ok, what = gbaot.compare(BUILD, rom, gbaot.broken(gbaot.load(out), which), frames,
                                     ref=ref)
            print("%-4s %-28s %s" % ("FAIL" if ok else "ok", "control: %s broken" % which,
                                     "passed - the gate cannot go red" if ok else what + ", as it must"))
            bad += ok
        if label.startswith("GBBANK") and not a.k:
            # tr_ejf asks the filter BEFORE the table (REALDMG-SPEC 20.9), so
            # the filter must name the table's blocks too - which no state
            # can show, a block turned away being interpreted the same.
            # Without them the interpreter's jumps never re-enter the table's
            # code, and fewer blocks are entered: 22% fewer here
            ok2, what2 = gbaot.compare(BUILD, rom, gbaot.load(out), frames,
                                       defines=("RDG_NOTABFILT",))
            n1, n2 = (int(w.split("; ", 1)[-1].split()[0]) if w[0].isdigit() else 0
                      for w in (what, what2))
            ok = n2 < 0.9 * n1
            print("%-4s %-28s %s" % ("ok" if ok else "FAIL", "control: table not filtered",
                                     "%d blocks entered against %d%s" % (
                                         n2, n1, ", as it must" if ok else
                                         " - the gate cannot see the filter")))
            bad += not ok
        if label.startswith("GBBANK"):
            # the list's search and its cache (rdgaot.inc): no ROM here has a
            # list, every block fitting the table, so the table is left empty
            # and every one listed. 63 banks hold code at 4010h, so a cache
            # that forgot the bank key hands one bank another's block
            img3 = gbaot.translate(BUILD, rom, gbaot.leaders(rom, found, extra),
                                   log=lambda *x: None, listall=True)
            out3 = out + "-listed"
            gbaot.save(img3, out3, rom)
            ok, what = gbaot.compare(BUILD, rom, gbaot.load(out3), frames)
            print("%-4s %-28s %s" % ("ok" if ok else "FAIL", "GBBANK, every block listed", what))
            bad += not ok
            if not a.k:
                ok, what = gbaot.compare(BUILD, rom, gbaot.load(out3), frames,
                                         defines=("RDG_AOTCACHEBUG",))
                print("%-4s %-28s %s" % ("FAIL" if ok else "ok", "control: cache key forgotten",
                                         "passed - the gate cannot go red" if ok
                                         else what + ", as it must"))
                bad += ok
        if label.startswith("GBSEAM4") and not a.k:
            # its JR back to 3FF8 has no block: tr_ejf's no-block way must
            # choose DS. The image's core without that choice must part
            ok, what = ejf_control(rom, out, frames)
            print("%-4s %-28s %s" % ("FAIL" if ok else "ok", "control: tr_ejf's DS left",
                                     "passed - the gate cannot go red" if ok
                                     else what + ", as it must"))
            bad += ok
        if label in HRAMLEG:
            ok, what = hram_leg(rom, found, extra, out, frames, ref)
            print("%-4s %-28s %s" % ("ok" if ok else "FAIL", "%s, HRAM counted" % label, what))
            bad += not ok
        if label.startswith("GBFLAGS") and not a.k:
            img2 = gbaot.translate(BUILD, rom, gbaot.leaders(rom, found, extra),
                                   log=lambda *x: None, defines=("RT_DEADBUG",))
            out2 = out + "-deadbug"
            gbaot.save(img2, out2, rom)
            ok, what = gbaot.compare(BUILD, rom, gbaot.load(out2), frames, serial=True)
            print("%-4s %-28s %s" % ("FAIL" if ok else "ok", "control: RT_DEADBUG",
                                     "passed - the gate cannot go red" if ok else what + ", as it must"))
            bad += ok
    print("gbaot: %s in %.0fs" % ("PASS" if not bad else "%d FAILED" % bad, time.time() - t0))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
