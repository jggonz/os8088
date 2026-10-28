#!/usr/bin/env python3
"""kern_mega: a C package's OVERLAY moved by a compaction, and then used
(docs/plans/KERN-MEGA-PLAN.md 14.46; SPEC.md 73.14, 66.2).

    python3 tests/megaovl.py            the row
    python3 tests/megaovl.py --keep     ...and leave the boot's directory

CWORD's overlay is PART 0 of CWORD.O88, read EAGERLY into a carve the parts
standard claims top-down in conventional memory (OSAPI_MEM_CLAIM_DMA_HI), and
declared movable with apps/cc/crt0.asm's cc_ovcarve - which re-derives
[cc_ovseg] and, through OS88_CODESEL, [cc_ovcs], the CODE selector every
vector and the thunk's retf take under kern_mega. Nothing had ever moved one
there. This boots kern_mega under Bochs with three packages on C: in this
order: MEGALOWH (tests/megaown, -DMO_LOWHOLE) holds a top-down 60KB claim at
the heap's ceiling, so CWORD's carve lands under it; MEGALOWH gives it back
twenty-five seconds later; and MEGALOWB (OS88_HIGH, so its own region is not
in the way) claims everything OSAPI_MEM_AVAIL says a compaction would leave,
which the ascending run alone cannot hold - the compactor has to slide the
carve UP into the hole. MEGALOWB prints every claim before and after.

The carve (CWORD's overlay, ~21KB, owned by CWORD's region) must be at a HIGHER
segment after the claim; then the host types into CWORD and uses File > Save
As (the overlay's dialog and its save path) onto C:, and C:\\DOC.RTF must
hold the typed line. No kern_mega FAULT or CLOSED.

THE FAILING CASE (docs/WRITING-TESTS.md 1): take cc_ovcarve's OSAPI_MEM_MOVABLE
out of apps/cc/crt0.asm (cc_ovinit) and the carve is pinned - red on "did not
move".
"""
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
import os88bochs  # noqa: E402
from os88fixture import need  # noqa: E402

PKGS = ["build/megalowh.o88", "build/cword.o88", "build/megalowb.o88"]
LINE = "Moved under me."


def claims(lines, which):
    """The which'th MEGALOWB snapshot: [(seg, paras, owner)]"""
    out, cur = [], []
    for l in lines:
        m = re.match(r"MEGALOWB: c (\w{4}) (\w{4}) (\w{4})$", l)
        if m:
            cur.append(tuple(int(x, 16) for x in m.groups()))
        elif l.strip() == "MEGALOWB: c":
            out.append(cur)
            cur = []
    return out[which] if len(out) > which else []


def main():
    keep = "--keep" in sys.argv
    for p in PKGS + ["build/megarun.o88", "build/mega.img",
                     "build/mbr.bin"]:
        need(p)
    out = tempfile.mkdtemp(prefix="megaovl-")
    t0 = time.time()
    box = os88bochs.Box(out, [os.path.join(ROOT, p) for p in PKGS])
    fails = []
    lines = []
    try:
        if box.wait_for("kern_mega:", 240):
            box.up()                    # the desktop, off kernel state
            box.megarun()               # C:, RUN, 0RUN.O88
            if not box.wait_for("MEGALOWB: done", 200):
                fails.append("MEGALOWB never finished")
            # every step below ends when the UI task is done with it (the
            # guest's own state, os88bochs.Box.ui_done) instead of the 5, 2,
            # 4, 8 and 15 s that were slept; what proves the steps worked is
            # still the line in C:\DOC.RTF, read off the disk afterwards
            box.ui_done("MEGALOWB's exit")
            box.click(300, 200, "CWORD's text taking the click")
            os88bochs.xd("type", "--delay", 60, LINE)
            os88bochs.xd("key", "Return")
            box.ui_done("CWORD to take the typed line")
            box.click(44, 60, "the File menu")
            box.click(80, 113, "Save As...'s dialog")
            box.click(400, 118, "the save onto C:\\DOC.RTF")
            box.ui_done("the save", hold=1.0)
            os88bochs.shot(os.path.join(out, "after.png"))
        else:
            fails.append("no kern_mega line in 240s")
        lines = box.lines()
    finally:
        box.stop()
    for l in lines:
        if l.startswith("kern_mega FAULT") or l.startswith(
                "kern_mega CLOSED"):
            fails.append(l)
    before, after = claims(lines, 0), claims(lines, 1)
    # the carve: the block whose owner is a selector (CWORD's region above
    # 1MB), the same owner AND size on both sides. Not MEGALOWB's own claim,
    # which is selector-owned too and is at the segment its 'got' line
    # printed. The size is not pinned here: the carve is the overlay's
    # bytes rounded up, and the overlay grows with CWORD (cc_ovmthunk's
    # module-to-module call made it 20KB -> 21KB), which a hard 0x500 read
    # as "no carve at all"
    got = [int(m.group(1), 16) for m in
           (re.match(r"MEGALOWB: got (\w{4})$", l) for l in lines) if m]
    sel = lambda cs: [c for c in cs if c[2] & 4 and c[0] not in got]
    keys = set((c[1], c[2]) for c in sel(before)) & \
        set((c[1], c[2]) for c in sel(after))
    pick = lambda cs: [c for c in sel(cs) if (c[1], c[2]) in keys]
    b, a = pick(before), pick(after)
    print("megaovl: carve before %s after %s" % (
        ["%04X" % c[0] for c in b], ["%04X" % c[0] for c in a]))
    if len(b) != 1 or len(a) != 1:
        fails.append("could not find CWORD's carve in both snapshots "
                     "(%d, %d)" % (len(b), len(a)))
    elif a[0][0] <= b[0][0]:
        fails.append("CWORD's carve did not move up: %04X before, %04X "
                     "after" % (b[0][0], a[0][0]))
    c = os.path.join(out, "c.img")
    d = open(c, "rb").read()
    lba = struct.unpack_from("<I", d, 0x1BE + 8)[0]
    part = os.path.join(out, "cpart.img")
    with open(part, "wb") as f:
        f.write(d[lba * 512:])
    r = subprocess.run(["mtype", "-i", part, "::DOC.RTF"],
                       capture_output=True, text=True)
    if LINE not in r.stdout:
        fails.append("C:\\DOC.RTF does not hold the typed line (%s)"
                     % (r.stdout.strip()[-80:] or r.stderr.strip()))
    else:
        print("megaovl: C:\\DOC.RTF holds %r" % LINE)
    print("megaovl: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if keep or fails:
        print("megaovl: the boot is in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
