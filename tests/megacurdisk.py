#!/usr/bin/env python3
"""megacurdisk - the pointer through a REFLECTED disk transfer on kern_mega
leaves nothing on the glass (docs/plans/KERN-MEGA-PLAN.md 14.75; SPEC.md 7.4).

    make mega && python3 tests/megacurdisk.py [--keep]

On kern_big the mouse ISR moves the arrow through every `int 13h` (SPEC.md
7.4). On kern_mega that `int 13h` is REFLECTED - the CPU goes back to real
mode for the BIOS - and mega_rmaskf leaves the mouse's line open so the ISR
runs there too (KERN-MEGA-PLAN 14.1 answer 18). Since MC7.4 the frame buffer
the ISR draws through is a SELECTOR ([vid_seg], VSEG's [k_vsa]), and real
mode loads a selector as a PARAGRAPH: the owner's 286 left arrow fragments
all over the desktop through a disk operation, the arrow on the glass never
erased while its moves went into kernel memory at selector x 16.

This boots build/mega.img on 86Box's 286 (vm/286-mega, the owner's mr286,
whose MR BIOS waits for each floppy transfer on IRQ6 with interrupts on),
turns the native floppy path off for the session ([mega_fdoff], the state a
machine whose 765 does not answer the native driver is put in), so EVERY
transfer is a reflected `int 13h` - the shape of the owner's hard disk - and
then copies BIG.DAT (120KB) from a scratch B: to A: with Copy and Paste,
the pointer wiggling across a strip of bare desktop for the whole copy.

  1. the copy happens: A:'s BIG.DAT is B:'s, byte for byte;
  2. the arrow TRACKED inside the freeze: of the sample pairs (a pointer
     packet and 40 emulated ms apart) whose two ends both read the progress
     widget up and the gfx lock held - SPEC.md 7.4.5's teardown cannot fake
     that - at least TRACK of them saw [cur_drawn_*] move. So the fix did
     not simply shut the line: shut, the arrow moves only where a
     reflection ends, and that arm reads well under it;
  3. the strip is exactly as it was before the copy: no pixel differs once
     the pointer has been parked outside it (the defect left 200-250);
  4. nothing charged an exception to a loaded image.

THE FAILING CASE (docs/WRITING-TESTS.md 1): kernel/mega.inc as it was before
14.75, built with `make mega`, is red on 3 - arrow fragments where the
pointer stood when a reflection began.

Needs a built 86Box (python3 tools/os88box.py build) and its ROM set.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
ap.add_argument("--keep", action="store_true")
A = ap.parse_args()
import megacdrom as MC                                        # noqa: E402
import megacz as CZ                                           # noqa: E402
from PIL import Image                                         # noqa: E402

BIGKB = 120
STRIP = (0, 300, 570, 446)      # bare desktop under the windows, left of the
                                # drive zones and above the dock
PARK = (630, 440)               # ...and where the pointer waits outside it
TRACK = 0.5
QUIET = 120000                  # emulated ms the copy may take (~10 s here)
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def strip_diff(a, b):
    a = Image.open(a).convert("RGB")
    b = Image.open(b).convert("RGB")
    x0, y0, x1, y1 = STRIP
    return [(x, y) for y in range(y0, y1) for x in range(x0, x1)
            if a.getpixel((x, y)) != b.getpixel((x, y))]


def main():
    for f in ("mega.img", "megak/kernel.map"):
        if not os.path.exists(os.path.join(CZ.B, f)):
            sys.exit("megacurdisk: build/%s is not built - `make mega`" % f)
    tmp = tempfile.mkdtemp(prefix="megacurdisk")
    img = os.path.join(tmp, "mega.img")         # (named mega*: os88box then
    shutil.copy(os.path.join(CZ.B, "mega.img"), img)    # reads megak/)
    data = bytes((i * 7 + (i >> 9)) & 0xFF for i in range(BIGKB * 1024))
    big = os.path.join(tmp, "BIG.DAT")
    with open(big, "wb") as f:
        f.write(data)
    bimg = os.path.join(tmp, "megab.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", bimg, "--size", "1440", big], check=True,
                   stdout=subprocess.DEVNULL)
    S = CZ.S
    b = CZ.boot(img, bimg, "megacurdisk%d" % os.getpid(), A.keep)
    copied = False
    try:
        b.cmd("memw 0x%X 01" % S("mega_fdoff"))     # every transfer to the BIOS
        slot, _ = CZ.disk_window(b, "A")
        slotb, blkb = CZ.disk_window(b, "B")
        err = CZ.select(b, slotb, blkb, "BIG.DAT")
        if err:
            raise SystemExit("B:'s BIG.DAT: " + err)
        b.tap("ctrl+c")
        b.ui_done(cap_ms=3000)
        x, y = MC.win_recs(b)[slot][:2]             # A: to the front, the
        b.to(x + 60, y + 5)                         # paste's target
        b.click()
        b.ui_done(cap_ms=3000)
        b.to(*PARK)
        b.ui_done(cap_ms=2000)
        s0 = os.path.join(tmp, "before.png")
        b.shot(s0)
        b.tap("ctrl+v")
        for _ in range(5):              # into the strip OPEN LOOP: inside the
            b.mouse(-96, -12)           # freeze a closed loop waits on the
            b.cmd("step 40")            # very thing check 2 measures
        spans = [(S("fpg_on"), 1), (S("gfx_lock_flag"), 1),
                 (S("cur_drawn_x"), 4)]

        def sample():
            on, lk, xy = (b.memr(a, n) for a, n in spans)
            return bool(on[0] and lk[0]), bytes(xy)
        t0, k, seen, pairs, moved, last = b.ms(), 0, False, 0, 0, None
        while b.ms() - t0 < QUIET:
            b.mouse(20 if (k // 8) % 2 == 0 else -20, 6 if k % 2 else -6)
            b.cmd("step 40")
            k += 1
            fr, xy = sample()
            if fr:
                seen = True
                if last is not None and last[0]:
                    pairs += 1
                    moved += last[1] != xy
            elif seen:
                break                               # the widget is down
            last = (fr, xy)
        took = b.ms() - t0
        b.ui_done(cap_ms=30000)
        b.to(*PARK)
        b.ui_done(cap_ms=3000)
        s1 = os.path.join(tmp, "after.png")
        b.shot(s1)
        print("   the copy took %.1f emulated s, %d pointer packets" % (
            took / 1000.0, k))
        want(seen, "the copy ran with the progress widget up (the freeze)")
        want(pairs and moved >= TRACK * pairs,
             "2. the arrow tracked INSIDE the freeze: it moved across %d of "
             "%d frozen sample pairs (at least %d%%)" % (moved, pairs,
                                                        TRACK * 100))
        d = strip_diff(s0, s1)
        want(not d, "3. the desktop strip is as it was: %d pixel(s) differ%s"
             % (len(d), (" (first %s)" % (d[:4],)) if d else ""))
        copied = True
    except SystemExit as e:
        want(False, "the machine stopped answering or a gesture missed: %s"
             % e)
    finally:
        f = CZ.faults(b)
        want(not f, "4. exceptions charged to a loaded image: %d%s" % (
            len(f), "".join("\n        " + x for x in f[:6])))
        b.quit(remove=not A.keep)
    if copied:
        got = os.path.join(tmp, "got.dat")
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                         "os88fat.py"),
                            "cat", "-o", got, img, "BIG.DAT"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        same = r.returncode == 0 and open(got, "rb").read() == data
        want(same, "1. A:'s BIG.DAT is B:'s, byte for byte")
    if A.keep:
        print("megacurdisk: kept %s" % tmp)
    else:
        shutil.rmtree(tmp, ignore_errors=True)
    print("megacurdisk: %s" % ("FAIL - " + "; ".join(FAILS) if FAILS
                               else "ok"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
