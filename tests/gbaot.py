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

TEN NEGATIVE CONTROLS, every one of which must go red:
  - the most-entered block of GBRMW.GB with its first byte changed: the
    check must see the precompiled code at all;
  - GBFLAGS.GB translated by a driver built with RT_DEADBUG (a flag's read
    unseen by the dead-flags lookahead, REALDMG-SPEC 16.1.2): its sum must
    differ from the interpreter's, so the gate sees a translator defect;
  - GBBANKRD.GB run with a driver built with RDG_NOTABFILT (the filter
    tr_ejf asks first without the table's blocks, REALDMG-SPEC 20.9): the
    block its interpreted bank routine jumps to must never be entered, the
    one thing that defect changes;
  - GBBANK.GB with every block in the list (the table left empty, so the
    search and its cache are what run) under a driver built with
    RDG_AOTCACHEBUG (the cache's bank key forgotten): it must differ;
  - the same list cut in four parts under a driver built with RDG_LXBUG
    (part 0 searched whatever the key, REALDMG-SPEC 20.17.1): it must enter
    fewer blocks than four parts searched properly do;
  - GBSEAM4.GB with tr_ejf's choice of DS taken out of the precompiled core
    (a JR from the bank back to a bank-0 PC with no block): it must differ;
  - GBRET.GB with tr_rmeg's megamorphic site jumping on through tr_lno, its
    saved HL another return's: its sums must differ;
  - GBBANKRD.GB with rd_rom's check of a lazy bank jumped over, in every
    segment's copy: a read of a bank not yet fetched must differ;
  - GBBANKRD.GB under a driver built with RDG_RXDSBUG (a fetch leaves the
    core's DS to rdg_setbank) with S_PC made the bank's at every fetch: the
    interpreter reading the bank from bank-0 code must go wrong;
  - GBBANK.GB precompiled with bank 0's code alone and its misses
    translated (REALDMG-SPEC 20.16) under a driver built with RDG_HYBUG (a
    runtime block filed under the next bank's key): it must differ.

The misses' leg (REALDMG-SPEC 20.16) precompiles GBBANK, GBRET and
cpu_instrs from BANK 0'S ENTRIES ALONE - everything in a switchable bank is
what the walk did not find - and runs the machine that translates what it
misses beside the interpreter: identical, with blocks made at run time.
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
        "gbseam", "gbseam2", "gbseam3", "gbseam4", "gbseam5", "gblcdoff", "gbret",
        "gbbankrd"]
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
    # one RET returning to eight places, four of them in a bank: its site
    # made megamorphic (tr_rmeg, REALDMG-SPEC 16.4.1), a lookup a return
    ("GBRET (one RET, eight places)", "gbret", 300, True),
    # banks switched to lazily and read every way the core has, in a cache
    # of three slots (rd_rom, pop_slow, tr_int; REALDMG-SPEC 10.3.2)
    ("GBBANKRD (lazy banks)", "gbbankrd", 900, False),
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


def core_labels():
    """The precompiled core's map: {label: offset}"""
    labels = {}
    for ln in open(os.path.join(BUILD, "gbaot", "rdgcore.map")):
        p = ln.split()
        if len(p) == 3:
            try:
                labels[p[2]] = int(p[0], 16)
            except ValueError:
                pass
    return labels


def rmeg_count(m):
    """The largest count in S_RMEG at the end of a run: RT_RMEGN once a
    site was made megamorphic"""
    raw = bytes(m.u.mem_read(m.view * 16 + m.sym["S_RMEG"], 64))
    return max(raw[i + 2] for i in range(0, 64, 4))


def rmeg_leg(rom, out, frames):
    """GBRET's RET made megamorphic - by the precompiled code and by the
    dynamic recompiler - and the machine still the interpreter's"""
    n = gbaot.symbols(BUILD)["RT_RMEGN"]
    got = []
    for mode, img in ((3, gbaot.load(out)), (1, None)):
        for f, x, st, m in gbaot.run_arm(BUILD, rom, mode, frames, img):
            pass
        got.append(rmeg_count(m))
    ok1, what = gbaot.compare(BUILD, rom, None, frames, ref="interp", arm="dyn", serial=True)
    ok = ok1 and got == [n, n]
    return ok, "megamorphic: precompiled %s, translated %s (RT_RMEGN %d); dyn %s" % (
        got[0], got[1], n, what)


def rmeg_control(rom, out, frames):
    """compare() with tr_rmeg's rewritten site jumping to tr_lno instead of
    tr_enter: on with BX = S_SAVBX, whatever the last lookup left there - in
    every segment's copy of the page, the image's own being what a site in
    it jumps to"""
    def fn(b, lab):
        at, want = lab["tr_rmeg.mega"], (lab["tr_enter"] - 41) & 0xFFFF
        i = b.index(bytes([0xBA, want & 0xFF, want >> 8]), at, at + 40)
        v = (lab["tr_lno"] - 41) & 0xFFFF
        b[i + 1:i + 3] = bytes([v & 0xFF, v >> 8])
        return bytes(b)
    labels = core_labels()
    meta, segs, table = gbaot.load(out)      # (every segment's copy of the
    segs = [fn(bytearray(sg), labels) for sg in segs]   # page: the site's own
    return gbaot.compare(BUILD, rom, (meta, segs, table), frames, serial=True)   # runs)


LAZY_SLOTS = 3


def lazy_leg(rom, out, frames):
    """GBBANKRD in a three-slot cache: identical to the interpreter on the
    link port too, under the dynamic recompiler as well, and every reader
    rx_get serves having fetched a bank (S_RXGO, sampled at rdg_rx)"""
    import collections
    from unicorn import UC_HOOK_CODE
    lab = core_labels()
    rev = {v: k for k, v in lab.items()}
    got = collections.Counter()
    for f, x, st, m in gbaot.run_arm(BUILD, rom, 3, frames, gbaot.load(out),
                                     slots=LAZY_SLOTS):
        if f == 0:
            v = m.sym["S_RXVEC"]           # rdg_rx, as rdg_init set it
            at = m.r16(m.view, v + 2) * 16 + m.r16(m.view, v)

            def hk(uc, a, sz, u, m=m):
                got[rev.get(m.r16(m.view, m.sym["S_RXGO"]), "?")] += 1
            m.u.hook_add(UC_HOOK_CODE, hk, begin=at, end=at)
            m.u.ctl_remove_cache(0, 0x110000)   # (a hook added mid-run)
    want = {"rd_rom", "pop_slow", "tr_int"}
    ok1, w1 = gbaot.compare(BUILD, rom, gbaot.load(out), frames, serial=True, slots=LAZY_SLOTS)
    ok3, w3 = lazy_stale(rom, out, frames)
    ok2, w2 = gbaot.compare(BUILD, rom, None, frames, ref="interp", arm="dyn",
                            slots=LAZY_SLOTS)
    ok = ok1 and ok2 and ok3 and want <= set(got)
    return ok, "fetched for %s; %s; dyn %s; %s" % (
        ", ".join("%s %d" % kv for kv in sorted(got.items())), w1, w2.split(";")[0], w3)


def lazy_stale(rom, out, frames, fix=True):
    """A fetch must not depend on S_PC, which is whatever the last trip out
    left: every rdg_rx is entered with S_PC made a PC in the bank (4123h),
    and the frames must still be the interpreter's. rdg_setbank points the
    core's DS at the bank for such a PC, and a fetch that let it (fix=False,
    the control) sends the interpreter reading the bank from bank-0 code -
    RdZero - on with DS the bank's"""
    from unicorn import UC_HOOK_CODE
    r = gbaot.run_arm(BUILD, rom, 0, frames, None, slots=LAZY_SLOTS)
    k = gbaot.run_arm(BUILD, rom, 3, frames, gbaot.load(out), slots=LAZY_SLOTS,
                      defines=() if fix else ("RDG_RXDSBUG",))
    n = [0]
    for (f, x0, s0, m0), (_, x1, s1, m1) in zip(r, k):
        if f == 0:
            v = m1.sym["S_RXVEC"]
            at = m1.r16(m1.view, v + 2) * 16 + m1.r16(m1.view, v)

            def hk(uc, a, sz, u, m=m1):
                m.w16(m.view, m.sym["S_PC"], 0x4123)
                n[0] += 1
            m1.u.hook_add(UC_HOOK_CODE, hk, begin=at, end=at)
            m1.u.ctl_remove_cache(0, 0x110000)
        if (x0, s0) != (x1, s1):
            return False, "S_PC stale at %d fetches: frame %d DIFFERS" % (n[0], f)
    return n[0] > 0, "S_PC stale at %d fetches: %d frames identical" % (n[0], frames)


def entered(rom, out, frames, lparts, defines=()):
    """precompiled blocks entered over `frames`, the list in `lparts` parts"""
    for f, x, st, m in gbaot.run_arm(BUILD, rom, 3, frames, gbaot.load(out), prove=True,
                                     defines=defines, lparts=lparts):
        pass
    return sum(v for k, v in m.ran.items() if k != "code")


def notab_count(rom, out, frames, defines):
    """The entries of GBBANKRD's Add9 block (ADD A,9 / RET in bank 0)"""
    pc = bytes(rom.d[:0x4000]).index(b"\xC6\x09\xC9")
    m = None
    for f, x, st, m in gbaot.run_arm(BUILD, rom, 3, frames, gbaot.load(out), prove=True,
                                     defines=defines, slots=LAZY_SLOTS):
        pass
    return m.ran.get((pc, 0xFF), 0)


def lazy_control(rom, out, frames):
    """compare() with rd_rom's je .rx made two NOPs in every segment's copy:
    a lazy bank read through segment 0"""
    def fn(b, lab):
        at = lab["rd_rom"]
        i = b.index(bytes([0x36, 0x83, 0x3E]), at, at + 8) + 6
        assert b[i] == 0x74, hex(b[i])
        b[i:i + 2] = b"\x90\x90"
        return bytes(b)
    labels = core_labels()
    meta, segs, table = gbaot.load(out)
    segs = [fn(bytearray(sg), labels) for sg in segs]
    return gbaot.compare(BUILD, rom, (meta, segs, table), frames, slots=LAZY_SLOTS)


# the misses translated (REALDMG-SPEC 20.16): the rows whose banked code a
# bank-0-only image leaves to the run, and the frames each needs to make some
HYBLEG = ("GBBANK", "GBRET", "cpu_instrs")


def hyb_leg(rom, found, extra, out, frames, serial, defines=()):
    lead = {(b, pc) for b, pc in gbaot.leaders(rom, found, extra) if pc < 0x4000}
    img = gbaot.translate(BUILD, rom, lead, log=lambda *x: None)
    out2 = out + "-bank0"
    gbaot.save(img, out2, rom)
    ok, what = gbaot.compare(BUILD, rom, gbaot.load(out2), frames, arm="hyb", serial=serial,
                             defines=defines)
    if ok and not serial and " 0 misses made blocks" in what:
        return False, "no miss was translated: the leg tests nothing - " + what
    return ok, what


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
        ok, what = gbaot.compare(BUILD, rom, gbaot.load(out), frames, ref=ref, serial=serial,
                                 slots=LAZY_SLOTS if label.startswith("GBBANKRD") else None)
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
        if label.startswith("GBBANK ("):
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
            # THE LIST IN PARTS (REALDMG-SPEC 20.17.1): the same list cut in
            # four, the search choosing a part by its first entry - identical,
            # and every block one part entered entered from four. Part 0
            # searched whatever the key must lose blocks
            ok, what = gbaot.compare(BUILD, rom, gbaot.load(out3), 300, lparts=4)
            n1, n4 = (entered(rom, out3, 300, lp) for lp in (1, 4))
            ok = ok and n1 == n4
            print("%-4s %-28s %s; %d blocks entered from one part, %d from four" % (
                "ok" if ok else "FAIL", "GBBANK, the list in 4 parts", what, n1, n4))
            bad += not ok
            if not a.k:
                nb = entered(rom, out3, 300, 4, ("RDG_LXBUG",))
                ok = nb < n4
                print("%-4s %-28s %s" % ("ok" if ok else "FAIL", "control: part 0 only",
                                         "%d blocks entered, as it must be fewer" % nb if ok
                                         else "%d entered - the gate cannot go red" % nb))
                bad += not ok
        if label.startswith("GBSEAM4") and not a.k:
            # its JR back to 3FF8 has no block: tr_ejf's no-block way must
            # choose DS. The image's core without that choice must part
            ok, what = ejf_control(rom, out, frames)
            print("%-4s %-28s %s" % ("FAIL" if ok else "ok", "control: tr_ejf's DS left",
                                     "passed - the gate cannot go red" if ok
                                     else what + ", as it must"))
            bad += ok
        if label.startswith("GBBANKRD"):
            ok, what = lazy_leg(rom, out, frames)
            print("%-4s %-28s %s" % ("ok" if ok else "FAIL", "GBBANKRD, every reader", what))
            bad += not ok
            if not a.k:
                ok, what = lazy_control(rom, out, frames)
                print("%-4s %-28s %s" % ("FAIL" if ok else "ok", "control: rd_rom unchecked",
                                         "passed - the gate cannot go red" if ok
                                         else what + ", as it must"))
                bad += ok
                # tr_ejf asks the filter BEFORE the table (REALDMG-SPEC
                # 20.9), so the filter must name the table's blocks too -
                # which no state can show, a block turned away being
                # interpreted the same. Without them the interpreter's jumps
                # never re-enter the table's code: Add9, which the
                # interpreted bank routine jumps to, is entered only by its
                # precompiled callers (this was GBBANK's control, whose
                # interpreter ran only through the eager switch 10.3.2 took)
                n1, n2 = (notab_count(rom, out, frames, d) for d in ((), ("RDG_NOTABFILT",)))
                ok = n1 > 100 and n2 * 10 < n1
                print("%-4s %-28s %s" % ("ok" if ok else "FAIL", "control: table not filtered",
                                         "Add9 entered %d times against %d%s" % (
                                             n2, n1, ", as it must" if ok else
                                             " - the gate cannot see the filter")))
                bad += not ok
                ok, what = lazy_stale(rom, out, frames, fix=False)
                print("%-4s %-28s %s" % ("FAIL" if ok else "ok", "control: DS left to setbank",
                                         "passed - the gate cannot go red" if ok
                                         else what + ", as it must"))
                bad += ok
        if label.startswith("GBRET"):
            ok, what = rmeg_leg(rom, out, frames)
            print("%-4s %-28s %s" % ("ok" if ok else "FAIL", "GBRET, megamorphic", what))
            bad += not ok
            if not a.k:
                ok, what = rmeg_control(rom, out, frames)
                print("%-4s %-28s %s" % ("FAIL" if ok else "ok", "control: stale HL on",
                                         "passed - the gate cannot go red" if ok
                                         else what + ", as it must"))
                bad += ok
        if label.split()[0] in HYBLEG:
            ok, what = hyb_leg(rom, found, extra, out, frames, serial)
            print("%-4s %-28s %s" % ("ok" if ok else "FAIL", "%s, misses translated" % label.split()[0],
                                     what))
            bad += not ok
            if label.startswith("GBBANK (") and not a.k:
                ok, what = hyb_leg(rom, found, extra, out, frames, serial, ("RDG_HYBUG",))
                print("%-4s %-28s %s" % ("FAIL" if ok else "ok", "control: RDG_HYBUG",
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
