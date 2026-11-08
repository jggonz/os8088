#!/usr/bin/env python3
"""kern_mega: A PACKED FILE PAST 64KB INTO A HUGE CLAIM (MEGA-HUGE-PLAN F4,
docs/plans/KERN-MEGA-PLAN.md 14.171, SPEC.md 20.14.5.4).

    python3 tests/megahpk.py            the row
    python3 tests/megahpk.py --keep     ...and leave the boot's directory
    python3 tests/megahpk.py --build DIR   another tree's kernel (a red)

Every shipped data file is lz4-packed, and OSAPI_FILE_READ expands one in
place. Into a claim above 1MB whose expansion passed 64KB it was REFUSED
(FERR_IO: dskw_rbody's .czfb, the decoder walking its output by base
arithmetic, which past a bridge's cut is another program's memory). This
boots kern_mega under Bochs at 4MB with tests/megahpk's MEGAHPK on C:'s RUN
folder and three host-made 'CZ' files beside it, and asserts, per file, that
the read succeeded with the file's unpacked length and that the bytes - summed
by MEGAHPK walking the claim by its step - are the original's:

  1. HP1.BIN, 150,000 bytes packing to about 54K, matches reaching back
     across every 64KB window edge, into a ONE-RUN pool claim
     (BX = HUGE_NOBRIDGE): cut 0, the first window above 1MB;
  2. a filler then takes all but 192KB of the pool's best run, and HP2.BIN
     (620,000 bytes - past the heap alone, so a bridge - packing BADLY: its stream starts before the cut) goes
     into a BRIDGE: cut > 0 and R = U - P under the cut - THE SEAM's case,
     the 32KB input view that straddles the cut read from a copy;
  3. HP3.BIN (620,000 bytes packing well) into a bridge: R at or past the
     cut, the whole stream in the conventional piece;
  4. HP3.BIN again, 4096 bytes into a bridge: the cut is off the 64KB grid
     of X, refused FERR_PROT (0008) and the claim untouched;
  5. no kern_mega FAULT or CLOSED.

THE FAILING CASE (docs/WRITING-TESTS.md 1): mega-release d433871c's kernel
(`--build` a tree of it) refuses 1-3 `refused 0002` - FERR_IO, .czbad - and
4 the same. Verified when the row was written, and stated in the row.
"""
import os
import random
import re
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs  # noqa: E402
import os88lz  # noqa: E402
from os88fixture import need  # noqa: E402

PKGS = ["build/megahpk.o88"]


def make(seed, n, kind):
    """The three files, deterministic. `edges`: a 40,000-byte random block
    repeated after random gaps, so long matches reach back tens of KB across
    every 64KB edge; `bad`: random 3KB blocks with short matches far back
    (packs to ~85%); `good`: words (packs to ~30%)."""
    r = random.Random(seed)
    out = bytearray()
    if kind == "edges":
        a = bytes(r.getrandbits(8) for _ in range(40000))
        while len(out) < n:
            out += bytes(r.getrandbits(8)
                         for _ in range(2000 + r.randrange(3000)))
            out += a[r.randrange(1000):][:30000 + r.randrange(9000)]
    elif kind == "bad":
        while len(out) < n:
            blk = bytes(r.getrandbits(8) for _ in range(3000))
            out += blk
            if len(out) > 61000:
                out += out[-r.randrange(1000, 60000):][:r.randrange(200, 900)]
            else:
                out += blk[:500]
    else:
        words = [bytes(r.choice(b"abcdefghijklmnopqrstuvwxyz")
                       for _ in range(r.randrange(2, 9))) for _ in range(300)]
        while len(out) < n:
            out += r.choice(words) + b" "
            if r.random() < 0.002:
                out += bytes(r.getrandbits(8) for _ in range(40))
    return bytes(out[:n])


def sums(data):
    s1 = s2 = 0
    for b in data:
        s1 = (s1 + b) & 0xFFFF
        s2 = (s2 + s1) & 0xFFFF
    return s1, s2


FILES = (("HP1.BIN", 1, 150000, "edges"), ("HP2.BIN", 2, 620000, "bad"),
         ("HP3.BIN", 3, 620000, "good"))


def main():
    keep = "--keep" in sys.argv
    bld = None
    if "--build" in sys.argv:
        bld = sys.argv[sys.argv.index("--build") + 1]
    for p in PKGS + ([] if bld else ["build/megarun.o88", "build/mega.img",
                                      "build/mbr.bin"]):
        need(p)
    out = tempfile.mkdtemp(prefix="megahpk-")
    t0 = time.time()
    want, puts = {}, []
    for name, seed, n, kind in FILES:
        d = make(seed, n, kind)
        z, packed = os88lz.cz_wrap(d)
        if not packed:
            print("FAIL: %s did not pack" % name)
            return 1
        path = os.path.join(out, name)
        with open(path, "wb") as fh:
            fh.write(z)
        puts.append("RUN:" + path)
        want[name] = (n, len(z), sums(d))
        print("megahpk: %s %d bytes packed to %d (R = %d), sums %04X %04X"
              % (name, n, len(z), n - len(z), *sums(d)))
    os88bochs.MEGS = 4
    box = os88bochs.Box(out, [os.path.join(ROOT, p) for p in PKGS],
                        put=tuple(puts), build=bld)
    fails, lines = [], []
    try:
        if box.wait_pm(240):
            box.up()
            box.megarun()
            end = time.time() + 300
            while time.time() < end:
                lines = box.said()
                if any(l.startswith("kern_mega FAULT") or
                       l.startswith(("MEGAHPK: done", "MEGAHPK: no"))
                       for l in lines):
                    break
                time.sleep(0.2)
            box.guest_sleep(1)
            lines = box.said()
        else:
            fails.append("kern_mega never switched to protected mode in 240s")
    finally:
        box.stop()
    for l in lines:
        if l.startswith("kern_mega FAULT") or l.startswith(
                "kern_mega CLOSED"):
            fails.append(l)
        if l.startswith("MEGAHPK"):
            print("megahpk: " + l)
    claim, read, refused = {}, {}, {}
    for l in lines:
        m = re.match(r"MEGAHPK: (\S+)( at 4096)? kb (\w+) sel (\w+) step "
                     r"(\w+) cut (\w+) lin (\w+)$", l)
        if m:
            claim[m.group(1) + (m.group(2) or "")] = [
                int(g, 16) for g in m.group(3, 4, 5, 6, 7)]
        m = re.match(r"MEGAHPK: (\S+)( at 4096)? read (\w+) (\w+) sum (\w+) "
                     r"(\w+)$", l)
        if m:
            read[m.group(1) + (m.group(2) or "")] = [
                int(g, 16) for g in m.group(3, 4, 5, 6)]
        m = re.match(r"MEGAHPK: (\S+)( at 4096)? refused (\w+)$", l)
        if m:
            refused[m.group(1) + (m.group(2) or "")] = int(m.group(3), 16)
    if "MEGAHPK: done" not in lines:
        fails.append("MEGAHPK never finished: %s"
                     % [l for l in lines if l.startswith("MEGAHPK: no")])
    for name, _, _, _ in FILES:
        n, p, (s1, s2) = want[name]
        if name in refused:
            fails.append("%s: refused %04X (FERR_IO is 0002: the tree "
                         "before F4)" % (name, refused[name]))
            continue
        if name not in read or name not in claim:
            fails.append("%s: no read line" % name)
            continue
        hi, lo, g1, g2 = read[name]
        if (hi << 16 | lo) != n:
            fails.append("%s: read %d bytes, not %d" % (name, hi << 16 | lo,
                                                        n))
        if (g1, g2) != (s1, s2):
            fails.append("%s: sums %04X %04X, the file's %04X %04X - the "
                         "bytes are not the original's" % (name, g1, g2, s1,
                                                           s2))
        kb, sel, step, cut, lin = claim[name]
        r = n - p
        if step != 8:
            fails.append("%s: step %d, not a selector run" % (name, step))
        if lin < 0x100000:
            fails.append("%s: the first window at %X, below 1MB - the "
                         "segment arm's read, not the huge one" % (name, lin))
        if name == "HP1.BIN":
            pass                        # (BX = HUGE_NOBRIDGE comes back as
                                        # it went: one run by the ask)
        else:
            if not cut:
                fails.append("%s: no bridge (cut 0) - the filler left the "
                             "pool's run whole" % name)
            elif name == "HP2.BIN" and r >= cut << 16:
                fails.append("HP2.BIN: R %d is past the cut %d x 64KB - no "
                             "seam was needed, so the leg does not test it"
                             % (r, cut))
            elif name == "HP3.BIN" and r < cut << 16:
                fails.append("HP3.BIN: R %d is before the cut %d x 64KB - "
                             "the stream is not the conventional piece's"
                             % (r, cut))
            else:
                print("megahpk: %s bridged at %dK, R %dK: %s" % (
                    name, cut * 64, r // 1024,
                    "THE SEAM" if r < cut << 16 else "the stream past it"))
    off = "HP3.BIN at 4096"
    if refused.get(off) != 8:
        fails.append("%s: %s - want refused 0008 (FERR_PROT): a cut off the "
                     "64KB grid of X" % (off, refused.get(off, read.get(off))))
    print("megahpk: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if keep or fails:
        print("megahpk: the boot is in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
