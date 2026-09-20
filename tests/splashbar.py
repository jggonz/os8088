#!/usr/bin/env python3
"""The progress bar ADVANCES during the kernel load (SPEC.md 15, 15.3.1).

    make && python3 tests/splashbar.py

**Nothing in this tree watched the one property a progress bar has.**
`bootsmoke` asserts a desktop appears; `bootstatus` asserts SPEC.md 15.6's
status line is composed and drawn. Between them a bar that parked at a single
percentage for the whole of the kernel load - the long, loud part of the boot,
several seconds of drive noise on the target machine - passed every gate in the
tree and was found in the field, on 86Box, by somebody watching it not move.

SPEC.md 15.3.1 is the defect: `spl_tick` takes the sectors loaded in AX, and
the move into `.boot2` (2.9.4) added a `mov ds` in front of the first use of
it. An 8086 has no `mov ds, imm`, so the segment goes through a general
register - and the register it went through was the argument. `[spl_done]` was
KERNEL_SEG, every tick, so the bar sat at 96/spl_total = 44% until dsk_xfer's
own `spl_step` took over at the boot mount.

WHAT IT ASSERTS, and the second half is the half that makes it a gate rather
than a restatement of the first:

 1. `[cs:spl_done]` takes MANY values during the load, never goes backwards
    (except for a whole-load restart, which SPEC.md 18.93.1's canary is allowed
    to cause), and ends the boot at `[cs:spl_total]`.
 2. ...and the BAR ON THE GLASS follows it. A counter that advances while
    nothing is drawn is the same bug wearing different clothes, so the lit
    width of the trough is measured out of the card's own framebuffer and has
    to take many values too, and to grow.

Both halves read state that lives in `.boot2`, and its segment is **HEAP_SEG**:
since SPEC.md 2.9.5 stage 2 copies itself to the heap's floor before it reads a
sector, so the blob's address is a kernel constant for the whole boot.
`[spl_fseg]` is NOT the way in here - stage 2 publishes that word only after the
load, because it calls the loading screen NEAR and the kernel is the only
caller that needs a far pointer.

It runs on the HERCULES twin: the bar is the one piece of chrome that draws on
all three adapters (`spl_rechrome` gives a 1bpp machine the bar and nothing
else), and `vram` on a 1bpp card is exact where a rendered VGA frame is not.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

MACHINE = "os8088_5150_herc_gla"
SPL_BAR_PX = 288                # splash.inc's, and the width this asserts
IMG = os.path.join(ROOT, "build", "os8088-360.img")

# The trough's interior, in the guest's own coordinates. spl_rechrome centres
# it on a 1bpp screen: y = vid_ch/2 - 8, SPL_BAR_H rows tall. Hercules is
# 720x348, so the fill runs 166..181 and row 174 is inside it. Nothing else
# draws on that row - the mono splash is the bar and nothing else - so the
# whole row is counted rather than a computed x window, which keeps this
# independent of where spl_rechrome rounds the origin to a byte.
BAR_PX = 288
BAR_SIDES = 2           # the trough's own two vertical rules, always lit
MIN_STEPS = 20          # distinct [spl_done] values during the load
MIN_WIDTHS = 8          # ...and distinct lit widths on the glass


def bar_width(m, row):
    """Lit pixels of the BAR, off the card's own framebuffer.

    The ROW comes from the kernel's own banked layout (SPEC.md 15.3.5.1's
    `spl_l_bar`) and not from a formula repeated here. It used to be
    `h // 2 - 8 + 8`, which was the mono bar's middle row until the bar moved
    to centre the whole block - and a test that re-derives a geometry is a
    test that reads the wrong row in silence.

    ...and it counts the bar's OWN 36 bytes rather than the whole row, because
    since 15.3.5 that row also crosses the trough's two sides and the dialog's
    two frames on a mono adapter: 288 + 6 = 294 of a 288px trough, which reads
    as a bar overrunning its frame."""
    w, h, rows = m.vram("herc")
    x0 = (w - SPL_BAR_PX) // 16 * 8     # spl_bar's own arithmetic, in PIXELS -
    return sum(rows[row][x0:x0 + SPL_BAR_PX])   # vram() is a byte a pixel


def main():
    lin_live = os88sym.linear("spl_live")
    lin_entry = os88sym.linear("cold_entry")
    sect = os88sym.sections()
    for n in ("spl_done", "spl_total"):
        assert sect[n] == ".boot2", (n, sect[n])
    blob = os88sym.equates()["HEAP_SEG"]
    off_done = os88sym.syms()["spl_done"]
    off_total = os88sym.syms()["spl_total"]
    off_bar = os88sym.syms()["spl_l_bar"]

    done, widths, backwards, total = [], [], 0, 0
    with os88marty.launch(IMG, apps=os.path.join(ROOT, "build", "apps360.img"),
                          machine=MACHINE, boot=0) as m:
        started = False
        for _ in range(4000):
            m.advance(frames=1)
            live = m.read(lin_live, 1)[0]
            if not started:
                # [spl_live] means nothing until the kernel's own bytes are on
                # the machine - bootstatus.py's guard, for its reason.
                if live != 1 or m.read(lin_entry, 1)[0] != 0xE9:
                    continue
                started = True
            # **THE READS COME AFTER THE LIVE TEST, NOT BEFORE IT.** The
            # note below is right that the blob is handed back (SPEC.md 2.9.5)
            # and that its words are then somebody else's memory - but it
            # guarded only `total`, with the max() that is still here. `bar`
            # got `if bar:`, which rejects ZERO and not the arbitrary non-zero
            # word that teardown leaves: `bar_width(m, bar + 8)` then indexes a
            # framebuffer row that does not exist and the row dies on
            # `IndexError: list index out of range` with nothing said about the
            # bar. Breaking first means no sample is ever taken off a blob that
            # has stopped being ours, which retires the whole class.
            # splashspin.py had the identical defect at the identical line.
            # **THE LAST NOTCH AND THE TEARDOWN ARE THE SAME FRAME.**
            # `spl_finish` forces [spl_done] to [spl_total] and THEN clears
            # [spl_live] (SPEC.md 15.3), so a loop that breaks the moment
            # `live` reads 0 never sees the value the whole check below is
            # about: it reported `the bar ended at 179 of 180 - spl_finish
            # forces the last notch, so this is not a rounding question`
            # about a kernel that had forced it.
            #
            # So the counters get ONE more reading on that frame and the
            # LAYOUT does not. That asymmetry is the point: `off_done` and
            # `off_total` are two words of a blob the kernel has finished
            # with but has not handed back yet - `mem_unblob` is at the end
            # of `kmain` - while `bar` is a pointer this would follow into
            # `bar_width`, which is what used to die on `IndexError` off a
            # blob that had stopped being ours. Reading two words is safe
            # where dereferencing one is not.
            last = live == 0
            d = int.from_bytes(m.readseg(blob, off_done, 2), "little")
            t = int.from_bytes(m.readseg(blob, off_total, 2), "little")
            total = max(total, t)       # kept: harmless, and it is what the
                                        # half-guarded version used
            if not done or done[-1] != d:
                if done and d < done[-1]:
                    backwards += 1
                done.append(d)
            if last:
                break                   # the counters are read, the layout is
                                        # not: see above
            bar = int.from_bytes(m.readseg(blob, off_bar, 2), "little")
            if bar:             # spl_tick raises [spl_live] and THEN calls
                                # spl_chrome, so there is a window where the
                                # splash is live and the layout is not banked
                wpx = bar_width(m, bar + 8)
                if not widths or widths[-1] != wpx:
                    widths.append(wpx)
        else:
            raise SystemExit("splashbar: the splash never handed the screen "
                             "over - this machine did not finish booting, so "
                             "nothing below would mean what it says")

    fail = []
    steps = len(set(done))
    t = total
    print("  [spl_done] took %d distinct values, %d..%d of %d"
          % (steps, min(done), max(done), t))
    print("  the bar drew %d distinct widths, %d..%d of %d px"
          % (len(set(widths)), min(widths), max(widths), BAR_PX))

    if steps < MIN_STEPS:
        stuck = max(set(done), key=done.count)
        fail.append("[spl_done] took %d distinct values and the load is %d "
                    "sectors: the bar is PARKED. SPEC.md 15.3.1 is the last "
                    "time, and it parked at %d/%d = %d%%."
                    % (steps, t, stuck, t, 100 * stuck // max(t, 1)))
    if backwards > 1:
        fail.append("[spl_done] went backwards %d times: one restart is "
                    "SPEC.md 18.93.1's canary reloading the kernel, more is "
                    "the count being written from somewhere that does not own "
                    "it" % backwards)
    if max(done) != t or t == 0:
        fail.append("the bar ended at %d of %d - spl_finish forces the last "
                    "notch (SPEC.md 15.3), so this is not a rounding question"
                    % (max(done), t))
    if len(set(widths)) < MIN_WIDTHS:
        fail.append("the trough drew %d distinct widths: the COUNTER may be "
                    "advancing, but the glass is not - which is the same "
                    "defect wearing different clothes"
                    % len(set(widths)))
    if max(widths) < BAR_PX * 0.9:
        fail.append("the bar never got past %d of %d px" % (max(widths), BAR_PX))
    if max(widths) > BAR_PX + BAR_SIDES:
        fail.append("the bar drew %d px of a %d px trough - it is running "
                    "past its own frame" % (max(widths), BAR_PX))

    for f in fail:
        print("FAIL: %s" % f)
    print("splashbar: %s" % ("FAILED" if fail else
                             "the bar advances, on the counter and on the glass"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
