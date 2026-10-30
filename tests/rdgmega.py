#!/usr/bin/env python3
"""RealDMG's recompiler on kern_mega: right, and with no #GP in the core
(docs/REALDMG-SPEC.md section 16.2.4).

    python3 tests/rdgmega.py                 the row
    python3 tests/rdgmega.py --keep          ...and leave the E9 log and shots
    python3 tests/rdgmega.py --pkg F.O88     run another REALDMG.O88 (the one
                                             before a change) - reports, does
                                             not gate

kern_mega COMPLETES a far jump through a data segment (mega_gpfix) and a
write through a code selector (mega_gpw), each with an exception - ~1,200
clocks on a 286. RealDMG's core runs with SS:SP the GAME BOY's stack, so a
fault taken in the core also pushes its frame into the game's own memory:
the core entered through its data segment once an event, and the recompiler
chained through its cache's data segment and patched its links through CS.
Before 16.2.4, blargg's cpu_instrs under the recompiler took 87,703 traps in
ten seconds and then looped in test 01 for good; Kirby's Dream Land's
attract took two million in a minute.

It boots `make megatrap`'s kernel (a private tree; every completed #GP is a
line on E9) under Bochs, the one emulator here that enforces a 286's segment
rules (docs/BOCHS.md), with B: holding REALDMG.O88 and cpu_instrs
(tools/getgbtest.py's, never committed), double-clicks the ROM - the
recompiler is the default CPU - and reads the bytes the game sent on its
link port out of the package's view (RDG_SERBUF, through rdg_viewseg off
the package's listing) until it says "Passed all tests" or LIMIT runs out:

  * cpu_instrs must PASS - all eleven, under the recompiler;
  * no trap may come from the core or a cache segment - a trap whose CS is
    one of the code selectors the view names for them (S_DISP+2, S_JITCSA,
    S_JITCSB); the package's own, at init and a lossy toggle, and the
    kernel's are allowed up to REGION_ALLOW;
  * and no kern_mega FAULT.

THE SHIPPED BUILD REFUSES on kern_mega (REALDMG-SPEC section 10.3.4): its
claims are segments it adds paragraphs to, which under KERN-MEGA-MEMORY-PLAN
MC7 are selectors, so its entry proc says "Use the MEGA apps disk" and starts
nothing (OS88_MEGA_REFUSE). Its leg double-clicks the same ROM and passes
when no RealDMG instance comes up and nothing faults. Every other leg runs
kern_mega's ARM (`make mega`'s build/megak/apps/realdmg.o88, PKG_MEGA), which
asks OSAPI_SEG for every "claim + N" it uses:

  * ARM - its banks above 1MB in place (REALDMG-SPEC section 10.3.1), which
    it must say: bank 1's word in the bank table is a selector whose BASE is
    at or above 1MB. Every arm leg also asserts 16.2.6: at least one extra
    tier, every one based above 1MB and 64KB apart (one pool block), and
    cache B above 1MB (the arm declares OS88_HIGHMEM);
  * ONECLAIM - the arm built again here with RW_NOXPLAN (never shipped), so
    the cartridge goes in ONE claim as on a machine with no room for it in
    place, and its banks are SEG_SUB windows into it (rw_pmfill): banks 1
    and 2 16KB apart, where the in-place run's windows step 32KB. The claim
    is cpu_instrs' exact 64KB, which OS88_HIGHMEM puts in the pool - what
    found KERN-MEGA-MEMORY-PLAN 8.6's file read;
  * SQUEEZED - the arm built again here with RT_SQUEEZE (its image and its
    recompiler part, -DRDG_XTIER -DRT_SQUEEZE - never shipped) and two
    extra tiers rather than five (RW_XDEF=2: 0.5KB a tier, more of them
    hold the whole test and the turn hardly comes round), so every
    cache segment is 0.5KB and the core's holes sixteen, and cpu_instrs
    recycles round every tier, the EXTRA tiers kern_mega's arm takes out of
    the extended pool (REALDMG-SPEC section 16.2.6) among them - each a
    SEG_SUBC pair in one pool block. It must pass, with
    at least one extra tier, and RECYCLE at least as many times as there
    are tiers: the first turn fills them fresh, so that many recycles is
    every tier, each extra one among them, written, recycled with its links
    undone, and written again - a link written into an extra tier through
    the wrong segment, or a tier the turn skipped, is a failure and not a
    slow second.

The view and the bank table's words are SELECTORS on kern_mega (MC7): every
read goes through the descriptor's base (os88bochs's sel_base), and the
"above 1MB" test is that base, not the selector's bit 2, which every claim
has now.

THE FAILING CASE (docs/WRITING-TESTS.md 1): `--pkg` with the REALDMG.O88
from before 16.2.4 (the base of this change) - measured red when this was
written: tens of thousands of traps charged to the core and the cache, and
cpu_instrs stuck in test 01.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402
import os88build  # noqa: E402
import megatraps  # noqa: E402

LIMIT = 150                     # seconds for cpu_instrs to finish (~45 s)
REGION_ALLOW = 200              # the region's own: rdg_init's two words, the
                                # lossy picture's patch (rdg_lpatch, ~90)
RDG_STATE = 0x4000
S_SERN = RDG_STATE + 0x64
RDG_SERBUF = 0x4700
S_DISP = RDG_STATE + 0x2C
S_JITCSA = 0x48E2
S_JITSEGA = 0x4810
S_JITSEGB = 0x4812
S_JITCSB = 0x48E4
TRAP = re.compile(r"^T [0-9A-F]{4} ([0-9A-F]{4}):[0-9A-F]{4} ")


def label_off(lst, name):
    for line in open(lst, errors="replace"):
        m = re.match(r"\s*\d+\s+([0-9A-F]{8})\s+\S+\s+(?:<\d+>\s+)?%s\s"
                     % name, line)
        if m:
            return int(m.group(1), 16)
    sys.exit("rdgmega: %s not in %s" % (name, lst))


S_BTAB1 = 0x6800 + 2             # RDG_BTAB's bank 1
S_JITRCY = 0x482F
S_JITXN = 0x48BF
S_JITXT = 0x7120                # (REALDMG-HIGHCACHE-PLAN R1: 55 of them)
RT_XMAX = 55


def lin(box, sel):
    """A segment's or a selector's LINEAR base (MC7: every claim is an LDT
    selector under kern_mega's protected mode)."""
    return box.sel_base(sel) if sel & 4 else sel * 16


def a_pkg(pkg):
    """True for --pkg's package, which is reported and not held to the
    tier assertions (it may be from before them)"""
    return os.path.basename(os.path.dirname(pkg)) not in (
        "apps", "rdgone", "rdgsqz")


def leg(t, out, name, pkg, lst, rom, where, squeeze=False):
    """One boot: PKG with cpu_instrs on B:, under the recompiler. WHERE is
    "high" (the banks above 1MB), "one" (one conventional claim) or
    "refuse" (the shipped build: it must not start). -> fails"""
    os.makedirs(out)
    shutil.copy(pkg, os.path.join(out, "REALDMG.O88"))
    gb = os.path.join(out, "CPUINSTR.GB")
    shutil.copy(rom, gb)
    viewoff = label_off(lst, "rdg_viewseg")
    disk = os.path.join(out, "rdgmega.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", disk, "--size", "1440", "--folder", "SYSTEM/APPDATA",
                    "REALDMG:" + os.path.join(out, "REALDMG.O88"),
                    "REALDMG:" + gb], check=True, stdout=subprocess.DEVNULL)
    t0 = time.time()
    box = os88bochs.Box(out, [], bimg=disk, btype="1_44", build=t.dir)
    fails, text, bank1, b1lin, xn, rcy = [], "", 0, 0, 0, 0
    bstep = 0
    xlin, blin, rbase = [], 0, None
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            return ["%s: kern_mega never reached protected mode" % name]
        box.up()
        box.capture()
        box.dbl(591, 105, "B:'s Disk window")
        box.dbl(150, 123, "the REALDMG folder")
        mark = os.path.getsize(box.e9)
        box.dbl(160, 139, "the game to start", lock=False)   # CPUINSTR.GB
        if where == "refuse":
            time.sleep(20)
            if box.pkg_base("RealDMG") is not None:
                fails.append("the shipped build STARTED under kern_mega's "
                             "protected mode (OS88_MEGA_REFUSE)")
            if any(l.startswith("kern_mega FAULT") for l in box.lines()):
                fails.append("a kern_mega FAULT")
            box.shot(os.path.join(out, "end.png"))
            print("rdgmega: %s: %s" % (name, "refused" if not fails
                                       else "; ".join(fails)))
            return ["%s: %s" % (name, f) for f in fails]
        end = time.time() + LIMIT
        view = None
        coded = set()
        while time.time() < end:
            time.sleep(3)
            if any(l.startswith("kern_mega FAULT") for l in box.lines()):
                break
            if view is None:
                base = box.pkg_base("RealDMG")
                if base is None:
                    continue
                rbase = base
                view = int.from_bytes(box.read(base + viewoff, 2), "little")
                if not view:
                    view = None
                    continue
            try:
                vb = lin(box, view)     # (a rebase can move it: each time)
                n = box.read(vb + S_SERN, 1)[0]
                text = box.read(vb + RDG_SERBUF, max(n, 1))[:n] \
                    .decode("latin-1")
            except RuntimeError as e:   # a machine buried in fixups (the
                fails.append("the debugger could not stop the machine: "
                             "%s" % e)  #   failing case) answers late
                break
            if "Passed" in text or "Failed" in text:
                break
        box.shot(os.path.join(out, "end.png"))
        # the core's and the cache segments' CODE selectors, as the view
        # names them (REALDMG-SPEC section 16.2.4): what a trap in the core
        # or in translated code would carry as its CS - and each segment's
        # identity alias, (S >> 2) | 4, which is what a package from before
        # 16.2.4 (--pkg) ran its core at, its view naming only data segments
        if view is not None and not fails:
            vb = lin(box, view)
            xn = box.read(vb + S_JITXN, 1)[0]
            rcy = int.from_bytes(box.read(vb + S_JITRCY, 2), "little")
            for off in (S_DISP + 2, S_JITCSA, S_JITCSB, S_JITSEGA, S_JITSEGB) + \
                    tuple(S_JITXT + 4 * i + k for i in range(min(xn, RT_XMAX))
                          for k in (0, 2)):
                w = int.from_bytes(box.read(vb + off, 2), "little")
                if w:
                    coded.add(w)
                    if not w & 4:
                        coded.add((w >> 2) | 4)
            segb = int.from_bytes(box.read(vb + S_JITSEGB, 2), "little")
            blin = lin(box, segb) if segb else 0
            for i in range(min(xn, RT_XMAX)):   # each extra tier's base
                w = int.from_bytes(box.read(vb + S_JITXT + 4 * i, 2), "little")
                xlin.append(lin(box, w) if w else 0)
            bank1 = int.from_bytes(box.read(vb + S_BTAB1, 2), "little")
            b1lin = lin(box, bank1) if bank1 else 0
            bank2 = int.from_bytes(box.read(vb + S_BTAB1 + 2, 2), "little")
            bstep = (lin(box, bank2) - b1lin) if bank2 and bank1 else 0
    finally:
        box.stop()
    with open(box.e9, "rb") as f:
        f.seek(mark)
        tail = f.read().decode("latin-1").splitlines()
    for l in tail:
        if l.startswith("kern_mega FAULT"):
            fails.append("a kern_mega FAULT: %s" % l)
    # T <err> <CS:IP> <image> ... - by the CS alone: the debugger's own
    # lines can land in the middle of one (Bochs and E9 share the file)
    traps = [(int(m.group(1), 16), l) for l in tail
             for m in [TRAP.match(l)] if m]
    other = [l for cs, l in traps if cs in coded]
    region = [l for cs, l in traps if cs not in coded]
    print("rdgmega: %s: the core and the caches run at %s; bank 1 at %04X "
          "(linear %06X)" % (name, " ".join("%04X" % c for c in sorted(coded)),
                             bank1, b1lin))
    print("rdgmega: %s: serial %r" % (name, " ".join(text.split())))
    print("rdgmega: %s: the region at linear %06X, cache B at %06X"
          % (name, rbase or 0, blin))
    print("rdgmega: %s: %d extra cache tiers%s, %d tiers recycled"
          % (name, xn, (" at " + " ".join("%06X" % x for x in xlin[:4]) +
                        (" ..." if len(xlin) > 4 else "")) if xlin else "",
             rcy))
    print("rdgmega: %s: %d traps after the launch - %d the region's and the "
          "kernel's, %d the core's or a cache's; %.0fs under Bochs"
          % (name, len(traps), len(region), len(other), time.time() - t0))
    if not coded:
        fails.append("the view was never found: no code selector to charge")
    if "Passed all tests" not in text:
        fails.append("cpu_instrs did not pass under the recompiler in %ds "
                     "(its link port said %r)" % (LIMIT, text[-60:]))
    if other:
        fails.append("%d traps charged to the core or a cache segment, the "
                     "first `%s` (tools/megatraps.py %s)"
                     % (len(other), other[0], box.e9))
    if len(region) > REGION_ALLOW:
        fails.append("%d traps outside the core and the caches (%d allowed)"
                     % (len(region), REGION_ALLOW))
    # kern_mega's arm (REALDMG-SPEC section 10.3.1): the banks IN PLACE, so
    # bank 1's table entry is one of OSAPI_XMEM_RUN's selectors, based at or
    # above 1MB; RW_NOXPLAN's is a SEG_SUB window into a conventional claim
    if where == "high" and b1lin < 0x100000:
        fails.append("the arm's bank 1 is at %04X (linear %06X), below 1MB: "
                     "the banks were not put in place" % (bank1, b1lin))
    # ...and in ONE claim the banks lie 16KB apart, a window each into it
    # (rw_pmfill), where the in-place run's step 32KB: the claim itself may
    # be anywhere, a 64KB one in the pool (OS88_HIGHMEM)
    if where == "one" and bstep != 0x4000:
        fails.append("the one-claim arm's banks 1 and 2 are %X apart, not 16KB: "
                     "not one claim" % bstep)
    if where == "high" and bstep != 0x8000:
        fails.append("the in-place banks 1 and 2 are %X apart, not the run's "
                     "32KB" % bstep)
    # REALDMG-HIGHCACHE-PLAN R2 (REALDMG-SPEC 16.2.6): the extra tiers are
    # 64KB windows of ONE extended-pool block - every one based at or above
    # 1MB, 64KB apart - and an arm with the pool behind it has some
    if where != "refuse" and not a_pkg(pkg) and xn < 1:
        fails.append("no extra cache tier was taken from the pool")
    # REALDMG-SPEC 16.2.7: the arm declares OS88_HIGH, so its region - the
    # package's code and data - is loaded above 1MB under CPU_F_PM
    if where != "refuse" and not a_pkg(pkg) and (rbase or 0) < 0x100000:
        fails.append("the package's region is at linear %06X, below 1MB: "
                     "the arm declares OS88_HIGH" % (rbase or 0))
    if where != "refuse" and not a_pkg(pkg) and blin < 0x100000:
        fails.append("cache B is at %06X, below 1MB: the arm declares "
                     "OS88_HIGHMEM and its 64KB claims go to the pool" % blin)
    if any(x < 0x100000 for x in xlin):
        fails.append("an extra tier is based below 1MB: %s"
                     % " ".join("%06X" % x for x in xlin))
    if any(b - a != 0x10000 for a, b in zip(xlin, xlin[1:])):
        fails.append("the extra tiers are not 64KB apart in one block: %s"
                     % " ".join("%06X" % x for x in xlin))
    if squeeze and xn < 1:
        fails.append("no extra cache tier was claimed (REALDMG-SPEC 16.2.5)")
    if squeeze and rcy < 3 + xn:
        fails.append("%d tiers recycled, fewer than the %d tiers there are"
                     % (rcy, 3 + xn))
    return ["%s: %s" % (name, f) for f in fails]


def variant(t, sub, *defines):
    """the arm built again with DEFINES (RT_SQUEEZE, RW_NOXPLAN), into the
    private tree"""
    d = os.path.join(t.dir, sub)
    os.makedirs(d, exist_ok=True)
    nasm = ["nasm", "-f", "bin", "-w+error"] + ["-D" + x for x in defines]
    subprocess.check_call(nasm + [
        "-I", "apps/", "-I", "apps/realdmg/", "-I", "drivers/sound/",
        "-I", t.dir + "/", "-DPKG_MEGA", "-l", os.path.join(d, "realdmg.lst"),
        "-o", os.path.join(d, "realdmg.bin"), "apps/realdmg/realdmg.asm"],
        cwd=ROOT)
    subprocess.check_call(nasm + [
        "-I", "apps/realdmg/", "-I", t.dir + "/", "-DRDG_XTIER",
        "-o", os.path.join(d, "rdgjitp.bin"), "apps/realdmg/rdgjitp.asm"],
        cwd=ROOT)
    o88 = os.path.join(d, "realdmg.o88")
    subprocess.check_call([
        sys.executable, os.path.join(ROOT, "tools", "os88pkg.py"),
        os.path.join(d, "realdmg.bin"), "-o", o88,
        "--part", t.img("rdgcore0.bin"), "--part", t.img("rdgcore1.bin"),
        "--part", os.path.join(d, "rdgjitp.bin")], cwd=ROOT,
        stdout=subprocess.DEVNULL)
    return o88, os.path.join(d, "realdmg.lst")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--leg", choices=("shipped", "arm", "oneclaim",
                                      "squeezed"),
                    action="append", help="run only these legs")
    ap.add_argument("--pkg", help="run this REALDMG.O88 alone (the one "
                    "before a change) - reports, does not gate")
    a = ap.parse_args()
    subprocess.check_call([sys.executable,
                           os.path.join(ROOT, "tools", "getgbtest.py")],
                          stdout=subprocess.DEVNULL)
    import getgbtest
    rom = os.path.join(getgbtest.DEST, "cpu_instrs", "cpu_instrs.gb")
    t = os88build.tree("MEGATRAP=1", targets=(
        "mega.img", "megarun.o88", "mbr.bin", "realdmg.o88",
        "megak/apps/realdmg.o88", "rdgcore0.bin", "rdgcore1.bin"))
    out = os.path.join(t.dir, "rdgmega")
    shutil.rmtree(out, ignore_errors=True)
    shipped = (t.img("realdmg.o88"), t.img("realdmg.lst"))
    if a.pkg:
        legs = [("given", a.pkg, shipped[1], "high")]
    else:
        legs = [("shipped", shipped[0], shipped[1], "refuse"),
                ("arm", t.img("megak/apps/realdmg.o88"),
                 t.img("megak/apps/realdmg.lst"), "high")]
        if not a.leg or "oneclaim" in a.leg:
            legs.append(("oneclaim",) + variant(t, "rdgone", "RW_NOXPLAN")
                        + ("one",))
        if not a.leg or "squeezed" in a.leg:
            legs.append(("squeezed",) + variant(t, "rdgsqz", "RT_SQUEEZE",
                                             "RW_XDEF=2")
                        + ("high",))
        if a.leg:
            legs = [l for l in legs if l[0] in a.leg]
    fails = []
    for name, pkg, lst, where in legs:
        fails += leg(t, os.path.join(out, name), name, pkg, lst, rom, where,
                     squeeze=name == "squeezed")
    if a.pkg:
        for f in fails:
            print("would FAIL: " + f)
        return 0
    for f in fails:
        print("FAIL: " + f)
    if not fails and not a.keep:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
