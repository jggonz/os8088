#!/usr/bin/env python3
"""kern_mega: ONE CLAIM FOR ALL OF IT (docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md MC5,
D6-D8).

    python3 tests/megahuge.py            the row
    python3 tests/megahuge.py --keep     ...and leave the boot's directory

OSAPI_MEM_HUGE with AX = 0 ASKS: the most one claim of the cell's can be given
- the pool alone, the heap alone, or the BRIDGE, the pool's best run cut to
whole 64KB windows and then the heap less its 64KB reserve, under ONE run of
selectors. This boots kern_mega under Bochs at 4MB with tests/megahuge's
MEGAHUGE on C:'s RUN folder, lets MEGARUN start it, and asserts:

  1. `MEGAHUGE: max N` - the query answered, more than the pool alone can
     hold (so the answer is a bridge: the pool's KB from the walk is less);
  2. the claim of exactly N succeeded, step 8, its first window above 1MB and
     its last below - the bridge, pool first;
  3. `MEGAHUGE: ok` - a pattern written through every window read back, the
     seam included, so no two windows alias;
  4. `MEGAHUGE: again N` - the free gave both pieces back, and `over
     refused` - a claim of N + 1KB is refused, so N is EXACT: the boot's
     read-ahead store and C:'s FAT window are standing at the query
     (asserted) and are counted as the room the claim sheds, never as
     room it lacks (KERN-MEGA-PLAN 14.121);
  5. no kern_mega FAULT or CLOSED;
  6. a WHOLE 64KB file read into a 64KB claim in the pool (KERN-MEGA-MEMORY-
     PLAN 8.6): `claim64 at` a base above 1MB, then `read64 ok` with bytes at
     both ends and the middle the file's. Red before the fix: `read64
     refused, AX 0002` - dskw_rbody's window test read the file's end,
     65,536, as a misfit and sent the read to the conventional fallback,
     which refuses a block above 1MB.

THE FAILING CASE (docs/WRITING-TESTS.md 1): the tree before MC5 answers AX = 0
with a refusal (a claim of 0KB) - red on (1), `MEGAHUGE: no 01`. Verified when
the row was written.
"""
import os
import re
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402
from os88fixture import need  # noqa: E402

PKGS = ["build/megahuge.o88"]


def cache_state(box):
    """KERN-MEGA-PLAN 14.121: the boot's caches standing when the query is
    asked - the read-ahead store and C:'s FAT window (volume 2), each a
    selector, and where it is. The answer counts them as room a claim
    sheds (mem_avail_lvl_x's mem_pg_cheap below 1MB, mx_maxrun above), and
    the claim of exactly it, and the refusal of one KB more, say so."""
    r, w = box.reads([("dsk_rah_seg", 2), ("dsk_fatwc", 16)])
    out = {}
    for k, v in (("dirw", int.from_bytes(r, "little")),
                 ("fatw", int.from_bytes(w[4:6], "little"))):
        if v & 4:
            out[k] = "%04X@%X" % (v, box.sel_base(v))
    print("megahuge: caches standing at the query: %s" % out)
    return out


def main():
    keep = "--keep" in sys.argv
    for p in PKGS + ["build/megarun.o88", "build/mega.img",
                     "build/mbr.bin"]:
        need(p)
    out = tempfile.mkdtemp(prefix="megahuge-")
    t0 = time.time()
    os88bochs.MEGS = 4
    b64 = os.path.join(out, "B64K.BIN")     # k xor k >> 8, 65,536 bytes
    with open(b64, "wb") as fh:
        fh.write(bytes((k ^ (k >> 8)) & 0xFF for k in range(65536)))
    box = os88bochs.Box(out, [os.path.join(ROOT, p) for p in PKGS],
                        put=("RUN:" + b64,))
    fails, lines, xkb = [], [], None
    try:
        if box.wait_pm(240):
            box.up()
            xkb = int.from_bytes(box.read("xm_kb", 2), "little")
            caches = cache_state(box)
            box.megarun()
            end = time.time() + 240
            while time.time() < end:
                lines = box.said()
                if any(l.startswith("kern_mega FAULT") or
                       l.startswith(("MEGAHUGE: ok", "MEGAHUGE: no",
                                     "MEGAHUGE: bad")) for l in lines):
                    break
                time.sleep(0.2)
            box.guest_sleep(1)          # the lines after the verdict
            lines = box.said()
        else:
            fails.append("kern_mega never switched to protected mode in 240s")
    finally:
        box.stop()
    for l in lines:
        if l.startswith("kern_mega FAULT") or l.startswith(
                "kern_mega CLOSED"):
            fails.append(l)
        if l.startswith("MEGAHUGE"):
            print("megahuge: " + l)
    got = {}
    for l in lines:
        for k, rx in (("max", r"MEGAHUGE: max (\w+)"),
                      ("again", r"MEGAHUGE: again (\w+)"),
                      ("claim", r"MEGAHUGE: claim (\w+) step (\w+)"),
                      ("lin", r"MEGAHUGE: first (\w+) last (\w+)"),
                      ("c64", r"MEGAHUGE: claim64 at (\w+)")):
            m = re.match(rx, l)
            if m:
                got[k] = [int(g, 16) for g in m.groups()]
    if "max" not in got:
        fails.append("the query never answered: %s" % [
            l for l in lines if l.startswith("MEGAHUGE: no")])
    else:
        n = got["max"][0]
        print("megahuge: the pool is %sK, the answer %dK" % (xkb, n))
        if xkb is not None and n <= xkb:
            fails.append("the answer %dK is no more than the pool's %dK: "
                         "no bridge" % (n, xkb))
        if "claim" not in got or got["claim"][1] != 8:
            fails.append("the claim of the answer: %s" % got.get("claim"))
        if "lin" not in got:
            fails.append("no window bases")
        elif not (got["lin"][0] >= 0x100000 and got["lin"][1] < 0xA0000):
            fails.append("first window at %X, last at %X: not pool first "
                         "then the heap" % tuple(got["lin"]))
        if "MEGAHUGE: ok" not in lines:
            fails.append("the pattern did not read back: %s" % [
                l for l in lines if l.startswith("MEGAHUGE: bad")])
        if "c64" not in got or got["c64"][0] < 0x100000:
            fails.append("the 64KB claim is not a pool block above 1MB: %s"
                         % got.get("c64"))
        if "MEGAHUGE: read64 ok" not in lines:
            fails.append("a whole 64KB file read into a 64KB pool claim: %s"
                         % [l for l in lines if "read64" in l])
        if "MEGAHUGE: over refused" not in lines:
            fails.append("a claim of the answer + 1KB was not refused: %s - "
                         "the answer read SHORT" % [
                             l for l in lines if "over" in l])
        if not caches or not caches.get("dirw") or not caches.get("fatw"):
            fails.append("the boot's read-ahead store and C:'s FAT window "
                         "were not both standing at the query: %s" % caches)
        if got.get("again", [None])[0] != n:
            fails.append("asked again after the free: %s, not %d"
                         % (got.get("again"), n))
    print("megahuge: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if keep or fails:
        print("megahuge: the boot is in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
