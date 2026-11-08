#!/usr/bin/env python3
"""kern_mega: EVERY WAY INTO A BRIDGED CLAIM AGREES WITH ITS STEP WALK
(docs/plans/MEGA-HUGE-PLAN.md F1, F2, F9; SPEC.md 50.3.5, 50.6.11).

    python3 tests/megabrdg.py            the row: Bochs, a 4MB machine
    python3 tests/megabrdg.py --box      ...86Box's at286_vga_sb, 640K + 384K
    python3 tests/megabrdg.py --keep     ...and leave the boot's directory

On kern_mega in protected mode an OSAPI_MEM_HUGE claim that neither the pool
nor the conventional heap holds alone BRIDGES them: its first P windows over
a pool block above 1MB, the rest over a conventional claim, ONE run of
selectors stepped by 8. Window k (DX + 8k) is right by construction; the
survey (MEGA-HUGE-PLAN 2, F1) read that everything computing a place from a
BASE plus an offset - SEG_SUB, SEG_AT, the kernel's own file I/O stepping a
buffer past 64KB - reaches past the pool piece into whatever pool memory
follows it. MEGABRDG (tests/megabrdg) claims the largest bridge with an
80KB conventional claim standing under it (freed straight after), finds the
cut by the walk itself, writes a pattern through windows P-1, P and P+1, and
reads it back every other way. This asserts:

  1. the claim is a bridge (P >= 1) and answers BX = P (F2: where the cut is);
  2. SEG_SUB, SEG_AT and SEG_HND from window P-1 at 256 bytes before the cut,
     1230h past it and 10040h past it read the pattern;
  3. SEG_TILE across the cut is REFUSED (no 64KB tile can be two pieces), and
     at the cut reads the conventional piece's first bytes;
  4. SEG_LIN by the rule - window k's own base plus the offset in it - reads
     the pattern (the naive first-base-plus-offset is wrong by construction
     on a bridge, and is printed, not asserted);
  5. a 96KB file written from 32KB before the cut and read back to 16KB
     before it (a read over 64KB, the selector arm's own walk) is byte-exact, and again at odd offsets with an odd length (a
     sector straddling the cut both ways);
  6. F2: the ask with BX = HUGE_NOBRIDGE answers less than the bridge, a
     claim of the bridge's size with it is refused, and a claim of the
     no-bridge answer with it is one piece;
  7. F9: OSAPI_MEM_MOVABLE of the bridge's DX is accepted, and (86Box, which
     reads the heap) the conventional piece's record is movable and was not
     placed in the MIDDLE of the conventional heap: with it counted as a pin,
     no free KB on both sides of it;
  8. no kern_mega FAULT (Bochs) / the probe answered and the machine is up.

THE FAILING CASE (docs/WRITING-TESTS.md 1): mega-release before the fix -
SEG_SUB/SEG_AT/SEG_HND past the cut read the wrong memory, the tile across
it is given, the file's write carried the wrong bytes and the read across
the cut was refused, BX came back 0, BX = HUGE_NOBRIDGE was ignored (the bridge given),
OSAPI_MEM_MOVABLE refused, and the conventional piece sat at the fill point
over the 80KB hole. Measured when the row was written (KERN-MEGA-PLAN 14.169).
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)

fails = []
FIELDS = ["max", "ccf", "nwin", "p", "bx", "subA", "subB", "subC", "atA",
          "atB", "atC", "hnd", "tilex", "tilec", "linr", "linn", "f1", "f1at",
          "f2", "f2at", "nbmax", "nbbig", "nbclaim", "movcf", "plo", "phi",
          "done", "b1lo", "b1hi", "stage"]
CODE = {0: "not asked", 1: "the pattern", 2: "WRONG memory", 3: "refused"}


def check(what, ok, more=""):
    print("   %s  %s%s" % ("ok  " if ok else "FAIL", what,
                           (" - " + str(more)) if more != "" and not ok
                           else ""), flush=True)
    if not ok:
        fails.append(what)
    return ok


def verdict(r, piece=None):
    """the assertions over MB_RES (a dict of FIELDS), and over the
    conventional piece's record when the heap was read (piece)"""
    print("megabrdg: %s" % " ".join("%s=%X" % (k, r[k]) for k in FIELDS))
    if not check("the claim of the ask's %dK granted" % r["max"],
                 r["ccf"] == 0):
        return
    if not check("a BRIDGE: the walk finds the cut at window %d of %d"
                 % (r["p"], r["nwin"]), r["p"] not in (0, 0xFFFF)):
        return
    check("F2: the claim answers BX = the pool piece's windows",
          r["bx"] == r["p"], "BX %X, the walk's P %X" % (r["bx"], r["p"]))
    for k, what in (("subA", "SEG_SUB 256 bytes before the cut"),
                    ("subB", "SEG_SUB 1230h past the cut"),
                    ("subC", "SEG_SUB 10040h past the cut"),
                    ("atA", "SEG_AT 256 bytes before the cut"),
                    ("atB", "SEG_AT 1230h past the cut"),
                    ("atC", "SEG_AT 10040h past the cut"),
                    ("hnd", "SEG_HND 1230h past the cut"),
                    ("tilec", "SEG_TILE at the cut"),
                    ("linr", "SEG_LIN by the rule (window k's base + the "
                             "offset in it)")):
        if r[k] == 0 and k.endswith("C"):
            print("   --    %s: no window P+1" % what)
            continue
        check(what + ": the pattern", r[k] == 1, CODE.get(r[k], r[k]))
    check("SEG_TILE across the cut REFUSED", r["tilex"] == 1,
          {2: "given, its second tile WRONG memory", 3: "given"}.get(
              r["tilex"], r["tilex"]))
    print("   --    SEG_LIN naively (the first base + the whole offset): %s "
          "(wrong by construction on a bridge: the rule is the answer)"
          % CODE.get(r["linn"], r["linn"]))
    for k, what in (("f1", "a 96KB file across the cut, aligned (a read over 64KB)"),
                    ("f2", "a file across the cut at odd offsets, odd length")):
        check(what + ": written and read back byte-exact", r[k] == 1,
              {2: "byte %X differs" % r[k + "at"],
               3: "the write REFUSED (AX %X)" % r[k + "at"],
               4: "the read REFUSED (AX %X)" % r[k + "at"]}.get(r[k], r[k]))
    check("F2: the ask with BX = HUGE_NOBRIDGE answers less than the bridge",
          0 < r["nbmax"] < r["max"], "%dK against %dK" % (r["nbmax"], r["max"]))
    check("F2: a claim of the bridge's %dK with BX = HUGE_NOBRIDGE refused"
          % r["max"], r["nbbig"] == 1, {2: "GRANTED"}.get(r["nbbig"], r["nbbig"]))
    check("F2: a claim of the no-bridge answer with it is one piece",
          r["nbclaim"] == 1, {2: "BRIDGED", 3: "refused"}.get(r["nbclaim"],
                                                              r["nbclaim"]))
    check("F9: OSAPI_MEM_MOVABLE of the bridge's DX accepted", r["movcf"] == 0,
          "CF=1")
    b1 = r["b1lo"] | r["b1hi"] << 16
    check("the 80KB claim under it was conventional", 0 < b1 < 0xA0000,
          "at %X" % b1)
    if piece is not None:
        rec, lo_f, hi_f = piece
        check("F9: the conventional piece's record is movable once declared",
              rec is not None and not rec[2], "record %s" % (rec,))
        check("F9: the conventional piece not placed in the MIDDLE of the "
              "heap (counted as a pin: %.1fK free under it, %.1fK over it)"
              % (lo_f, hi_f), not (lo_f > 4 and hi_f > 4))


def unpack(raw, partial=False):
    at = raw.find(b"MBRRES01")
    if at < 0:
        return None
    w = struct.unpack_from("<%dH" % len(FIELDS), raw, at + 8)
    r = dict(zip(FIELDS, w))
    return r if r["done"] == 1 or partial else None


def run_bochs(keep):
    import os88bochs
    from os88fixture import need
    for p in ["build/megabrdg.o88", "build/megarun.o88", "build/mega.img",
              "build/mbr.bin"]:
        need(p)
    out = tempfile.mkdtemp(prefix="megabrdg-")
    os88bochs.MEGS = 4
    box = os88bochs.Box(out, [os.path.join(ROOT, "build/megabrdg.o88")])
    lines = []
    try:
        if box.wait_pm(240):
            box.up()
            box.megarun()
            end = time.time() + 300
            while time.time() < end:
                lines = box.said()
                if any(l.startswith(("kern_mega FAULT", "MEGABRDG: "))
                       for l in lines):
                    break
                time.sleep(0.3)
            box.guest_sleep(1)
            lines = box.said()
        else:
            fails.append("kern_mega never switched to protected mode in 240s")
    finally:
        box.stop()
        if not keep:
            shutil.rmtree(out, ignore_errors=True)
        else:
            print("megabrdg: kept %s" % out)
    for l in lines:
        if l.startswith(("kern_mega FAULT", "kern_mega CLOSED")):
            fails.append(l)
    got = [l for l in lines if l.startswith("MEGABRDG: ")]
    if not got:                 # the line before the file legs, at least
        got = [l for l in lines if l.startswith("MEGABRDG0: ")]
        if got:
            print("megabrdg: the machine went down in the file legs; the "
                  "line before them:")
    if not check("MEGABRDG answered", bool(got)):
        return
    w = [int(x, 16) for x in got[-1].split()[1:]]
    if len(w) < len(FIELDS):            # +8..+65 in order, `done` among them
        fails.append("short line: %s" % got[-1])
        return
    r = dict(zip(FIELDS, w))
    verdict(r)


def run_box(keep):
    os.environ.setdefault("OS88_MEGA", "1")
    os.environ.setdefault("OS88_MEGA_EMU", "86box")
    import os88build
    import os88marty as M
    import os88ui
    import megajw as J
    B = lambda p: os88build.at(os.path.join("build", p))     # noqa: E731
    tmp = tempfile.mkdtemp(prefix="megabrdg-")
    try:
        disk = os.path.join(tmp, "b.img")
        subprocess.check_call(
            [sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o",
             disk, "--size", "1440", B("megabrdg.o88"), "--folder",
             "SYSTEM/APPDATA"], stdout=subprocess.DEVNULL)
        os.environ["OS88_MEGA_MEGS"] = os.environ.get("OS88_MEGA_MEGS", "1.375")
        with os88ui.boot(B("mega.img"), apps=disk, machine=J.MACHINE,
                         label="megabrdg") as ui:
            m = ui.m
            ext = J.u16(m.read(m.sym("mega_extkb"), 2), 0)
            print("megabrdg: %sMB, %dK above 1MB" % (
                os.environ["OS88_MEGA_MEGS"], ext))
            ui.open_drive("B")
            # expect=None: the program works inside its entry proc, so its
            # window is shown only when that returns - and on the tree before
            # the fix it never did. MB_RES is polled instead
            ui.open("MEGABRDG.O88", expect=None)
            def live():
                # the program does its work INSIDE ITS ENTRY PROC, before its
                # inst_tab row names it, so the loader's [ld_base] - the
                # region whose entry proc is running - is asked first
                out = [("MEGABRDG", J.u16(m.read(m.sym("ld_base"), 2), 0))]
                return out + list(J.insts(m).values())
            r, last, heap = None, None, None
            for _ in range(240):
                for nm, sp in live():
                    if nm == "MEGABRDG":
                        r = unpack(m.readseg(sp, 0, 2048))
                        last = unpack(m.readseg(sp, 0, 2048), True) or last
                if heap is None and last and last["stage"] >= 1:
                    # the heap with the bridge held, before the file legs
                    # (which took the tree before the fix down)
                    J.show(m, "the bridge held")
                    heap = J.conv(m)
                if r:
                    break
                M.guest_sleep(m, 1)
            if r is None and last is not None:
                print("megabrdg: MEGABRDG never finished (the machine's "
                      "titles now: %s); what it had written:" % ui.titles())
                fails.append("MEGABRDG never finished")
                for l in m.faults()[-6:]:
                    print("   fault: " + l)
                r = last
            if not check("MEGABRDG answered", r is not None,
                         "instances %s, titles %s" % (J.insts(m), ui.titles())):
                for l in m.faults()[-12:]:
                    print("   fault: " + l)
                return
            cb, ct, cl = heap or J.conv(m)
            plin = r["plo"] | r["phi"] << 16
            rec = next((c for c in cl if c[0] << 4 == plin), None)
            piece = (rec, 0.0, 0.0)
            if rec is not None:
                # the placement, with the piece counted as a PIN whatever its
                # declaration says now: what it would cut if it never moved
                pinned = [c if c is not rec else c[:2] + (True,) + c[3:]
                          for c in cl]
                st = J.cstretches(cb, ct, pinned)
                lo_f = sum(f for lo, hi, f in st if hi <= rec[0])
                hi_f = sum(f for lo, hi, f in st if lo >= rec[0] + rec[1])
                piece = (rec, lo_f, hi_f)
            verdict(r, piece)
    finally:
        if not keep:
            shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--box", action="store_true", help="86Box, 640K + 384K")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    t0 = time.time()
    (run_box if a.box else run_bochs)(a.keep)
    print("megabrdg: %s in %.0fs" % ("PASS" if not fails else "FAIL (%s)"
                                     % "; ".join(fails), time.time() - t0))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
