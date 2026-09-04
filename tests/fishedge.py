#!/usr/bin/env python3
"""Does sea life leave the reserved strip at the right edge DARK on Hercules?
(SPEC.md 79.5.10)

    make && python3 tests/fishedge.py

SPEC.md 79.5.9 puts the field's column-0 shimmer in 86Box's plain Hercules
renderer rather than in this kernel: the mark is one row DOWN from what stands
at the right edge, and no write this kernel makes can reach row y+1 column 0
from row y. 79.5.10 is the product answer to it - an unlit column at the edge
of the picture reads as the edge of the monitor and a shimmering one does not -
so sea life reserves `SV_HEDGE` pixels at the right and never lights them.

**THE ASSERTION IS AN A/B AND IT NEEDS TO BE.** A run that finds the strip
dark proves nothing on its own: a sea whose swimmers never went near the edge
is dark there too, and reads exactly like a pass. So the same swept sea runs
twice against the same guest - once with `[sv_hlim]` as the driver set it, and
once with that word poked to 0, which is precisely the "no reserve" state
every other adapter is in. The strip must be **clean under the first and
dirty under the second**; a second leg that comes back clean means the sweep
never put a swimmer there and the row says so rather than passing.

The sweep is forced rather than waited for. A swimmer crosses 720 pixels at
two to four a frame, so an unforced session spends most of its time nowhere
near the right edge; `sv_fx` is written before each frame to walk all four
across the strip at every 8-pixel alignment, which is the whole case in about
forty frames instead of several thousand.

**IT ASSERTS THE SHIPPED DEFAULT, so `make NOHEDGE=1` fails it and should.**
That knob is the strip's build A/B and keeps the unreserved arm assembling
(tests/unit/t_buildmatrix.py); the BEHAVIOUR A/B is the runtime poke below,
which needs no rebuild and so cannot be run against a tree somebody left in
the other configuration.

Nothing here is timed, so the guest's speed does not matter - but it is a
MartyPC row because Hercules is (docs/TESTING.md).
"""
import sys, os, re, time, argparse, subprocess, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                            # noqa: E402

NFISH = 4
SEA = 8                     # ss_modes' sea-life bit
HEDGE = 8                   # SV_HEDGE, and the row re-derives the rest
STEPS = 48                  # frames a leg: 2 px a step covers every alignment


def offsets(names):
    """Listing addresses out of SAVER.DRV - it loads whole at DRVR_SEG:0000
    with no relocation (SPEC.md 51), so a listing address IS the address.

    A DATA name is found by its own memory reference; a CODE label has no
    address column of its own, because nasm prints one only where bytes are
    emitted, so its address is the first emitting line after it.
    """
    with tempfile.TemporaryDirectory() as tmp:
        lst = os.path.join(tmp, "saver.lst")
        subprocess.run(["nasm", "-f", "bin", "-w+error",
                        "-I", "drivers/", "-I", "apps/", "-I", "drivers/saver/",
                        "-I", "apps/wire/", "-l", lst,
                        "-o", os.path.join(tmp, "saver.bin"),
                        "drivers/saver/saver.asm"],
                       cwd=ROOT, check=True, capture_output=True)
        text = open(lst, errors="replace").read().splitlines()
    out = {}
    for name in names:
        for line in text:
            if re.search(r"\[%s( |\]|\+)" % name, line):
                mm = re.search(r"\[([0-9A-F]{4})\]", line)
                if mm:              # the listing prints it little-endian
                    out[name] = int(mm.group(1)[2:4] + mm.group(1)[0:2], 16)
                    break
        if name in out:
            continue
        seen = False
        for line in text:
            if not seen:
                seen = re.search(r"\s%s:\s*$" % name, line) is not None
                continue
            mm = re.match(r"\s*\d+\s+([0-9A-F]{8})\s+[0-9A-F]", line)
            if mm:
                out[name] = int(mm.group(1), 16)
                break
    missing = [k for k in names if k not in out]
    if missing:
        raise SystemExit("fishedge: no listing address for %s" % ", ".join(missing))
    return out


def start_sea(m):
    os88marty.no_saver(m)                       # a boot settles for long enough
    m.key("Escape")                             # to trip the IDLE saver, and a
    time.sleep(0.5)                             # blanked screen is not a start
    m.write(m.sym("ss_modes"), bytes([SEA]))    # sea life, and only it
    m.write(m.sym("ss_secs"), b"\xff")          # one long turn: no re-pick
    m.write(m.sym("ss_idle"), b"\x1c\x00")      # ~1.5s of idle
    m.key("Space")
    t = time.time()
    while time.time() - t < 90 and m.read(m.sym("blk_sv"), 1)[0] != 1:
        time.sleep(0.2)
    if m.read(m.sym("blk_sv"), 1)[0] != 1:
        return None
    return int.from_bytes(m.read(m.sym("ss_row") + 2, 2), "little")


def sweep(m, seg, o, w, card, x0):
    """Walk all four swimmers across the right edge; return (dirty, near)."""
    dirty = near = 0
    for k in range(STEPS):
        base = x0 + 2 * k
        m.write((seg << 4) + o["sv_fx"],
                b"".join(int(base + 6 * i).to_bytes(2, "little")
                         for i in range(NFISH)))
        m.write((seg << 4) + o["sv_fs"],  bytes([2] * NFISH))
        m.write((seg << 4) + o["sv_fos"], bytes([2] * NFISH))
        m.run()
        if not m.wait_stop(40):
            raise SystemExit("fishedge: sv_step never reached")
        _, _, rows = m.vram(card)               # ONE ENTRY PER PIXEL (79.5.9)
        for r in rows:
            if any(r[w - HEDGE:w]):
                dirty += 1
            if any(r[w - 2 * HEDGE:w - HEDGE]):
                near += 1
    return dirty, near


def herc(a):
    bad = 0
    o = offsets(["sv_step", "sv_fx", "sv_fs", "sv_fos", "sv_hlim"])
    with os88marty.launch(a.image, apps=a.apps, boot=40,
                          machine="os8088_5150_herc_gla", card="herc") as m:
        seg = start_sea(m)
        if seg is None:
            print("fishedge: the saver never started")
            return 1
        w, _, _ = m.vram("herc")
        lim = int.from_bytes(m.readseg(seg, o["sv_hlim"], 2), "little")
        print("  Hercules %d wide, [sv_hlim] = %d (want %d)" % (w, lim, w - HEDGE))
        if lim != w - HEDGE:
            print("fishedge: the strip was never armed on the one adapter that "
                  "wants it (SPEC.md 79.5.10): 1 finding")
            return 1
        m.advance(frames=40)                    # let the opening settle
        m.breakpoints([{"type": "execseg", "seg": seg, "off": o["sv_step"]}])

        dirty, near = sweep(m, seg, o, w, "herc", w - 40)
        print("  reserved  ON: %d rows lit in cols %d-%d, %d in the 8 before"
              % (dirty, w - HEDGE, w - 1, near))
        if dirty:
            print("fishedge: sea life lit the reserved strip (SPEC.md 79.5.10)"
                  ": 1 finding")
            bad += 1

        m.write((seg << 4) + o["sv_hlim"], b"\x00\x00")      # ...and the A/B
        off, offnear = sweep(m, seg, o, w, "herc", w - 40)
        print("  reserved OFF: %d rows lit in cols %d-%d, %d in the 8 before"
              % (off, w - HEDGE, w - 1, offnear))
        if not off:
            print("fishedge: the sweep never put a swimmer in the strip, so the "
                  "clean leg above is evidence about this TEST and not about "
                  "the kernel: 1 finding")
            bad += 1
    return bad


def cga(a):
    """Every other adapter keeps the whole screen AND the kernel's own clip."""
    o = offsets(["sv_hlim"])
    with os88marty.launch(a.image, apps=a.apps, boot=40,
                          machine="os8088_5150_cga_gla") as m:
        seg = start_sea(m)
        if seg is None:
            print("fishedge: the saver never started on CGA")
            return 1
        lim = int.from_bytes(m.readseg(seg, o["sv_hlim"], 2), "little")
        print("  CGA: [sv_hlim] = %d (want 0 - no strip, and gfx_blit1's own "
              "right clip stays the only cut)" % lim)
        if lim != 0:
            print("fishedge: a strip was reserved on an adapter that has no "
                  "artifact to hide (SPEC.md 79.5.10): 1 finding")
            return 1
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args()
    os.chdir(ROOT)
    bad = herc(a) + cga(a)
    print("fishedge: %d finding(s)" % bad)
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
