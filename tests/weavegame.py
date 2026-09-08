#!/usr/bin/env python3
"""What the canvas COSTS and what a player SEES (WEAVE-SPEC 6.10, 12.3, 14).

    make weavedisk && python3 tests/weavegame.py
    python3 tests/weavegame.py --machine os8088_5150_herc_gla --png shots/

PONG.WAB is the load, and the two questions are `wirefps`'s and `wireflick`'s
asked of a sprite canvas instead of a wireframe (SPEC.md 78.9):

  1. HOW MANY GFX CALLS A FRAME, which is the only honest way to price a
     redraw on this machine (CLAUDE.md: a redraw costs what it CALLS, not what
     it covers). WEAVE-SPEC 14 prices a two-sprite frame at 2-4 calls and
     ~2-5 ms, and 6.10.2's whole dirty-band design exists to hit it. The
     canvas core keeps its own `frames` and `blits` counters in WEAVE.WSM's
     state block and this reads the quotient - a COUNTED number, not a felt
     one, and one an emulator is exact about (Part 4: QEMU and MartyPC are
     exact about how much work the guest does).
  2. WHAT THE GLASS SHOWS BETWEEN THE ERASE AND THE DRAW. `m.flicker()` is
     the wrong instrument here for `wireflick`'s reason and it says so: it
     waits for the screen to SETTLE and a running game never does again. So
     this samples the canvas rectangle once per displayed frame and counts its
     ink, the way tests/wireflick.py does - a frame caught mid-compose has
     fewer lit sprite pixels, and how many fewer, how often, is the flicker.
     A dirty-band composer that got it right shows NONE, because the compose
     happens in RAM and the band reaches the glass in one blit.

AND THE THIRD DEFECT, which is the one no emulator shows at all: INPUT
OVERRUN. The staging ring counts every record it could not take (6.10.6), and
that counter is read here and asserted at zero. It is the only one of
CLAUDE.md's three invisible defects that CAN be turned into a number, and this
is the number.

IT IS A MEASUREMENT WITH THREE STRUCTURAL ASSERTIONS AND NO THRESHOLD ON
TIME. `wirefps` says why: a number that fails a build when a harness gets
slower teaches nobody anything, and the host under this is not a 4.77 MHz
8088. What IS asserted is what does not depend on the host - that frames ran
at all, that the calls-per-frame is inside 14's own row, and that nothing was
dropped. The fps figure is printed and belongs in PERFORMANCE.md; the field
run (docs/FIELD-MACHINES.md) is what turns it into a claim.

HOW IT FINDS THE COUNTERS, and it is not a bss offset. WEAVE.WSM's claim
begins with its own three-word stamp (WEAVE-SPEC 1.2.2) - 'WS', the ABI
number and the module's byte count - and a claim base is KB-aligned
(SPEC.md 50), so a six-byte signature at a 64-paragraph boundary finds it.
A bss offset read out of build/weave.gen.asm would be wrong by however much
crt0's and os88ui's own .bss take, because they share one section.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import weavesmoke                                           # noqa: E402
from harness import check, done                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODULE = "build/WEAVE.WSM"

# WEAVE.WSM's state block, in bytes from WSM_H_STATE (WEAVE-SPEC 6.10.4).
WSS_RUN, WSS_ACK, WSS_SLEEP = 0, 1, 2
WSS_FRAME, WSS_CVSEG, WSS_WIN = 6, 8, 10
WSS_BLITS, WSS_FRAMES, WSS_OVF = 18, 20, 22
WSS_PEN = 71 + 32 * 6           # 6.10.7's pen: paper colour, then `colored`
# ...and the canvas claim's sprite records (wsmabi.inc WSC_SPR / WSR_*): the
# records start at +16 of the claim, 24 bytes each, y at +6 of a record.
WSC_SPR, WSR_SIZE, WSR_Y = 16, 24, 6


def find_modules(m, img):
    """Every WEAVE.WSM image in the heap.  Answers [(segment, state offset)].

    A BYTE-IDENTICAL COPY IS NOT A SECOND CLAIM, and the difference cost this
    row a false failure before it was understood. The floppy driver caches
    what it reads (SPEC.md 18.95) and tools/os88disk.py lays files out
    contiguously, so WEAVE.WSM's nine sectors sit in the cache after any read
    NEAR them - and they do: opening FORM.WAB, a bundle with no canvas that
    never asks for the module at all, puts a complete copy of it in memory at
    a KB boundary. Matching the stamp, or even the whole image, therefore
    counts caches as claims.

    "A REAL ONE HAS BEEN WRITTEN" IS NOT ENOUGH, and that is what this row
    was getting wrong. The filter used to be "WSMV_BIND stamps a canvas
    segment into the state block, and a cached copy still carries the file's
    zeros" - so the caller kept every candidate with a non-zero WSS_CVSEG.
    Measured on `os8088_5150_cga_gla`, TWO images answered that, and the one
    the scan reached first was the wrong one:

      seg 2b80  RUN=1 SLEEP=256 CVSEG=0202 WIN=0c01 NSPR=8 BLITS=770 FRAMES=7693 OVF=15
      seg 3500  RUN=0 SLEEP=1   CVSEG=3640 WIN=0000 NSPR=3 BLITS=1   FRAMES=0    OVF=0

    Every number in the first row is nonsense - a canvas claim below the heap,
    770 blits against 7,693 frames, eight sprites in a bundle that declares
    three - and every number in the second is PONG's (ball, pad, cpu; a canvas
    claim at 3640, which is 3500 plus the module's own 5,087 bytes). The row
    then read its six counters out of the first and reported six product
    defects: `0 frames`, `sleep = 256`, `ovf = 15`, a paddle that never moved.
    Its four PASSES came off the same garbage.

    **WHAT SEPARATES THEM IS THE CODE, not the state.** A live module differs
    from the file only from WSM_H_STATE onward - that is where its writable
    data begins, and a flat image has nothing else to write. Diffed whole
    against build/WEAVE.WSM: seg 3500 differs in 59 bytes and every one is at
    or above 3676, while seg 2b80 differs in 707 and 589 of them are BELOW it,
    from offset 3584 - a 512 boundary, so it is disk sectors, which is exactly
    the cache this docstring already describes. The tail matched only because
    the sectors past it had not been reused.

    So the scan compares the whole code region and this returns only images
    that ARE the module. The caller still filters on WSS_CVSEG, because
    "loaded" and "bound" are different questions and it asks both.
    """
    so = int.from_bytes(img[6:8], "little")     # WSM_H_STATE, from the FILE
    out = []
    for seg in range(0x0800, 0xA000, 0x40):     # a claim base is KB-aligned
        b = m.readseg(seg, 0, 8)
        # The magic, the ABI and the size, taken FROM THE IMAGE rather than
        # spelled here: this used to hard-code ABI 1 (`b[2] == 1`), and
        # WEAVE-SPEC 6.10.7's palette bumped WSM_ABI to 2 - so the scan
        # matched nothing, found "0 bound of 0 image(s)", and reported a
        # module that had loaded perfectly as one that never loaded at all.
        # A number that is pinned in wsmabi.inc and copied into a test is a
        # number that goes stale on the one wave that changes it; reading it
        # out of the file the test already opened cannot.
        if bytes(b[0:6]) != img[0:6]:
            continue
        if bytes(m.readseg(seg, len(img) - 16, 16)) != img[-16:]:
            continue
        mem = b"".join(bytes(m.readseg(seg, o, min(1024, so - o)))
                       for o in range(0, so, 1024))
        if mem != img[:so]:                     # ...and the code is intact
            continue
        out.append((seg, so))
    return out


def canvas_ink(rows, x0, y0, w, h):
    """Ink inside the canvas rectangle, from one decoded frame.

    The canvas's paper is white and its sprites are black (6.10.2's
    polarity), so INK is an unlit pixel - which is weavegfx's own `_ink` and
    wireflick's count, said about a sprite field instead of a wireframe.

    THAT IS THE 1bpp READING AND IT IS THE ONE THAT DISCRIMINATES.  Under
    6.10.7's palette PONG's paper is BLACK on VGA, so every pixel of the
    field is unlit and this saturates at w x h - the half-composed check
    below still passes but no longer tells the two apart.  It keeps its
    meaning on the adapter the row is really for, and is left here rather
    than made colour-aware because a second reading of the same pixels is a
    second thing to keep true.
    """
    n = 0
    for y in range(y0, min(y0 + h, len(rows))):
        r = rows[y]
        for x in range(x0, min(x0 + w, len(r))):
            if not r[x]:
                n += 1
    return n


def session(machine, png_dir=None):
    S = os88sym.linear
    img = open(os.path.join(ROOT, MODULE), "rb").read()
    weavesmoke.BUNDLE = "PONG.WAB"
    with os88marty.launch("build/os8088-360.img", apps=weavesmoke.DISK,
                          machine=machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        before, after = weavesmoke._open_bundle(m, mo, S, machine)
        win = sorted(set(after) - set(before))[-1]
        wx, wy, ww, wh = dispcp.win_rect(m, S, win)

        mods = find_modules(m, img)
        check(bool(mods), "%s: WEAVE.WSM was read into a claim" % machine)
        live = [(a, b) for a, b in mods
                if int.from_bytes(m.readseg(a, b + WSS_CVSEG, 2), "little")]
        check(len(live) == 1,
              "%s: exactly one BOUND module - 1.2.2 reads it once at open "
              "and keeps it for the life of the instance" % machine,
              "found %d bound of %d INTACT image(s). find_modules already "
              "drops the floppy cache's copies; more than one bound here "
              "means a second instance, not a cache" % (len(live), len(mods)))
        if not live:
            return
        ms, so = live[0]

        def sw(off):
            return int.from_bytes(m.readseg(ms, so + off, 2), "little")

        print("      %s: module at %04x, state +%d, canvas claim %04x, "
              "%d image(s) in the heap"
              % (machine, ms, so, sw(WSS_CVSEG), len(mods)))

        # --- the rally ------------------------------------------------------
        # The Serve button is on the row after the canvas; the walk puts the
        # content origin one pixel below the title strip (7.1.1).
        ox, oy = wx, wy + 19
        bx, by = ox + 20, oy + 16 * 8 + 4
        mo.to(bx, by)                   # the POINTER first, and the click's
                                        # own settle set to nothing: os88mouse
                                        # sleeps 1.5 HOST seconds after a
                                        # click and MartyPC free-runs through
                                        # it, which is nine guest seconds -
                                        # long enough for the whole rally to
                                        # play out before the first sample.
                                        # Measured: 169 frames gone
        mo.click(bx, by, settle=0.0)
        m.advance(frames=6)             # the click is an EVENT: the handler
        m.run()                         # runs in a wake slice, not in the
        m.pause()                       # callback (4.10)
        check(sw(WSS_RUN) & 0xFF,
              "%s: start() hired the worker and it is running" % machine,
              "run = %d" % (sw(WSS_RUN) & 0xFF))
        check(sw(WSS_WIN), "%s: the worker knows its window" % machine,
              "wsm_win = %04x - TASK_ALIVE and WM_WAKE both take it"
              % sw(WSS_WIN))
        check(sw(WSS_SLEEP) == 1,
              "%s: 18 fps asks for one frame a tick" % machine,
              "sleep = %d" % sw(WSS_SLEEP))

        # The computer's paddle is PONG's third sprite (demos/pong.wml: ball,
        # pad, cpu) and its y lives in the canvas claim's sprite record. It
        # is read here and again after the frames because its handler is an
        # ONTICK handler and this is the only row that can say ontick fired
        # more than once: WEAVE.WSM shipped from wave 5 to wave 7 delivering
        # exactly one ontick per start() (6.10.6's pending flag was never
        # re-armed by the drain), and no counter shows that - frames ran,
        # blits ran, and a paddle that should have chased the ball sat where
        # it was born.
        cv = sw(WSS_CVSEG)

        def spr_y(i):
            return int.from_bytes(m.readseg(cv, WSC_SPR + i * WSR_SIZE + WSR_Y,
                                            2), "little", signed=True)
        cpu0 = spr_y(2)
        c0, f0, b0 = m.status()["cycles"], sw(WSS_FRAMES), sw(WSS_BLITS)
        m.advance(frames=60)
        m.run()
        m.pause()
        c1, f1, b1 = m.status()["cycles"], sw(WSS_FRAMES), sw(WSS_BLITS)
        secs = (c1 - c0) / 4772727.0
        df, db = f1 - f0, b1 - b0
        check(df > 0, "%s: frames ran" % machine, "%d frames" % df)
        cpu1 = spr_y(2)
        check(cpu1 != cpu0,
              "%s: the computer's paddle moved (ontick fired more than once)"
              % machine,
              "cpu.y %d -> %d over %d frames; onTick moves it 1 px a frame "
              "towards the ball, which is a different row from `frames ran` "
              "(6.10.6, 4.11.1)" % (cpu0, cpu1, df))
        if df:
            fps = df / secs
            per = db / float(df)
            print("      %s: %d frames in %.2f guest-seconds = %.1f fps; "
                  "%d blits = %.2f gfx calls a frame (14 prices 2-4)"
                  % (machine, df, secs, fps, db, per))
            check(0.5 <= per <= 4.0,
                  "%s: a frame is inside 14's own row" % machine,
                  "%.2f calls a frame against 14's 2-4" % per)
        check(sw(WSS_OVF) == 0,
              "%s: the staging ring dropped nothing" % machine,
              "ovf = %d (6.10.6's input-overrun counter)" % sw(WSS_OVF))

        # 9.2.1's sentence, ASSERTED rather than counted.  The palette is a
        # VGA feature because OSAPI_GFX_BLIT1_PEN is not read on one plane
        # (SPEC.md 5.4.2.2), so the load path must not read ANY of 6.10.7's
        # three props on a 1bpp adapter - and if it reads even one of them,
        # the sprite nibbles are set, `colored` is raised, and the composer
        # cuts a full-width run into colour spans on an adapter that cannot
        # show a colour: the same picture at 2.84 gfx calls a frame instead
        # of 1.06.  That is what happened, it is invisible in a screenshot,
        # and counting alone would not have named it - so the flag itself is
        # the check.
        colored = (sw(WSS_PEN) >> 8) & 0xFF
        kind = m.video()["type"]                # 'vga', 'cga', 'mda'/'herc'
        want = 1 if kind == "vga" else 0        # PONG.WML DOES declare one,
        check(colored == want,                  # so on VGA it must be ON too
              "%s: the palette is on where the pen is read and off where it "
              "is not" % machine,
              "`colored` = %d on a %s adapter, want %d - PONG declares "
              "paper/ink/color, so this is both halves of 9.2.1: read on "
              "VGA, and not read at all on one plane" % (colored, kind, want))

        # --- what the glass showed, frame by frame (wireflick's shape) ------
        cx, cy = ox, oy + 8             # the canvas starts on row 1 (br="1")
        cw, ch = 240, 120               # PONG's own w and h (demos/pong.wml)
        m.pause()
        counts = []
        for _ in range(24):
            m.advance(frames=1)
            counts.append(canvas_ink(m.vram()[2], cx, cy, cw, ch))
        full = max(counts) if counts else 0
        floor = min(counts) if counts else 0
        blank = sum(1 for c in counts if full and c < full // 2)
        print("      %s: canvas ink per displayed frame - full %d, floor %d, "
              "%d of %d frames under half full"
              % (machine, full, floor, blank, len(counts)))
        check(blank == 0, "%s: no frame was caught half-composed" % machine,
              "%d of %d frames under half full - a dirty-band composer that "
              "reaches the glass in one blit shows none" % (blank, len(counts)))

        if png_dir:
            vw, vh, rows = m.vram()
            weavesmoke._shot(png_dir, "game-" + machine, vw, vh, rows,
                             (wx, wy, ww, wh))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--png", metavar="DIR")
    a = ap.parse_args()
    os.chdir(ROOT)
    if not os.path.exists(MODULE):
        sys.exit("weavegame: no %s - run `make weavedisk` first" % MODULE)
    session(a.machine, a.png)
    return done("weavegame")


if __name__ == "__main__":
    sys.exit(main())
