#!/usr/bin/env python3
"""The spinner turns on the WALL CLOCK, not on the progress bar (SPEC.md 15.3.6).

    make && python3 tests/splashspin.py

`spl_spin` took its angle from `[spl_done]` - how much of the kernel had
loaded - and a boot does not deliver its sectors at a constant rate. Stage 2
notches the bar once per coalesced `int 13h` run (~430 ms), the boot mount
once per SECTOR (~24 ms), and 15.3.3's mouse wait once per system tick. A
frame per 430 ms and a frame per 24 ms are an eighteen-fold difference in
angular rate, and the eye reads it as exactly what it is: the logo turns a
little, stops, and turns further.

WHY THIS NEEDS A GATE AT ALL, and it is not the look. Wall-clock time is only
as good as the clock: mask IRQ0, hook `int 08h` without chaining, or leave the
PIT reprogrammed, and the tick at 0040:006C stops - at which point the logo
sits at ONE angle for the whole boot and the machine still boots. That is
PERFORMANCE.md Part 1's invisible-defect shape: a screendump of a splash looks
correct at any angle, so nothing in this tree would notice. `splashbar` is the
same argument about the bar and is the model for this file.

WHAT IT ASSERTS, in the order the failures matter:

 1. the angle MOVES - `[spl_cos]` takes many distinct values across the load.
    A frozen clock fails here.
 2. every sample AGREES WITH THE CLOCK. For each frame on which the composed
    angle changed, the guest's own BIOS tick is read and the angle checked
    against `spl_cos_tab[(tick >> SPL_SPINSH) & 15]`. A notch-driven angle
    disagrees the moment the notch rate changes, which is most of a boot.
 3. the RATE is the same in the stage 2 stretch as in the boot mount stretch,
    measured in positions per tick. That is the property the change exists to
    buy, and it is the one an "it still animates" test passes without.

The slop in 2 is +/- one position, and it is there for one reason: the tick
can turn over between `spl_spin` composing the angle and this test reading it,
because a frame is 16.7 ms and a tick is 55. It is not a tolerance on the
mechanism - a build whose angle is the notch count misses by far more than one
position, and MAX_OFF below is what says so.

It reads `[spl_cos]` out of `.boot2`, whose segment is HEAP_SEG - stage 2
copies itself to the heap's floor before it reads a sector (SPEC.md 2.9.5), so
the blob's address is a kernel constant for the whole boot. `splashbar.py`
carries the same paragraph and for the same reason.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

MACHINE = "os8088_5150_herc_gla"
IMG = os.path.join(ROOT, "build", "os8088-360.img")

BDA_TICK = 0x46C                # the BIOS 18.2065 Hz count, linear - the same
                                # word boot.asm stamps and kmain subtracts
                                # (SPEC.md 15.4)
MIN_ANGLES = 6                  # distinct cos values across the load
MAX_OFF = 1                     # positions of slop against the clock (above)
MAX_BAD = 2                     # samples allowed to miss anyway
MIN_PHASE_SAMPLES = 3           # transitions, before a rate is worth quoting
RATE_TOL = 0.35                 # positions/tick, between the two stretches


def cos_table(blob_bytes):
    """spl_cos_tab, read out of the kernel image rather than repeated here."""
    off = os88sym.syms()["spl_cos_tab"]
    tab = []
    for k in range(16):
        v = int.from_bytes(blob_bytes[off + k * 2:off + k * 2 + 2], "little")
        tab.append(v - 65536 if v >= 32768 else v)
    return tab


def positions_for(tab, cos):
    """Which of the sixteen positions show this cos - the table repeats."""
    return [k for k, v in enumerate(tab) if v == cos]


def circ_dist(a, b):
    d = abs(a - b) % 16
    return min(d, 16 - d)


def main():
    sect = os88sym.sections()
    for n in ("spl_cos", "spl_cos_tab", "spl_done"):
        assert sect[n] == ".boot2", (n, sect[n])
    eq = os88sym.equates()
    blob = eq["HEAP_SEG"]
    shift = eq["SPL_SPINSH"]
    off_cos = os88sym.syms()["spl_cos"]
    off_done = os88sym.syms()["spl_done"]
    lin_live = os88sym.linear("spl_live")
    lin_entry = os88sym.linear("cold_entry")

    with open(os.path.join(ROOT, "build", "kernel-full.bin"), "rb") as f:
        tab = cos_table(f.read())

    samples = []                # (tick, cos, done) on each CHANGE of cos
    with os88marty.launch(IMG, apps=os.path.join(ROOT, "build", "apps360.img"),
                          machine=MACHINE, boot=0) as m:
        started, last = False, None
        for _ in range(4000):
            m.advance(frames=1)
            live = m.read(lin_live, 1)[0]
            raw = int.from_bytes(m.readseg(blob, off_cos, 2), "little")
            cos = raw - 65536 if raw >= 32768 else raw
            if not started:
                # [spl_live] means nothing until the kernel's own bytes are on
                # the machine - splashbar.py's guard, for its reason.
                if live != 1 or m.read(lin_entry, 1)[0] != 0xE9:
                    continue
                started = True
                # ...and SEED `last` HERE rather than leaving it None, which is
                # what makes the first recorded sample a DRAWN angle. `spl_cos`
                # is initialised to 64 in the module's data, so a run that
                # records its first read records that INITIALISER - an angle no
                # frame ever showed, disagreeing with the clock by whatever the
                # boot took to get here, and seeding the unwrap below at a
                # position the logo was never at. It read as the first stretch
                # turning half again as fast as the second, which is the very
                # defect this file exists to catch.
                last = cos
                continue
            if cos != last:
                tick = int.from_bytes(m.read(BDA_TICK, 2), "little")
                done = int.from_bytes(m.readseg(blob, off_done, 2), "little")
                samples.append((tick, cos, done))
                last = cos
            if live == 0:
                break
        else:
            raise SystemExit("splashspin: the splash never handed the screen "
                             "over - this machine did not finish booting, so "
                             "nothing below would mean what it says")

    fail = []
    angles = set(c for _, c, _ in samples)
    print("  the logo took %d distinct angles over %d changes, ticks %d..%d"
          % (len(angles), len(samples),
             samples[0][0] if samples else 0,
             samples[-1][0] if samples else 0))

    if len(angles) < MIN_ANGLES:
        fail.append("the logo took %d distinct angles: it is PARKED, which is "
                    "what a stopped tick at 0040:006C looks like (SPEC.md "
                    "15.3.6.1) - IRQ0 masked, an int 08h hook that does not "
                    "chain, or a reprogrammed PIT" % len(angles))

    bad = []
    for tick, cos, _ in samples:
        want = positions_for(tab, cos)
        if not want:
            bad.append((tick, cos, "not a cos_tab value at all"))
            continue
        k = (tick >> shift) & 15
        if min(circ_dist(k, w) for w in want) > MAX_OFF:
            bad.append((tick, cos, "clock says position %d, drew %s"
                        % (k, "/".join(str(w) for w in want))))
    print("  %d of %d samples disagree with the clock by more than %d position(s)"
          % (len(bad), len(samples), MAX_OFF))
    if len(bad) > MAX_BAD:
        for t, c, why in bad[:5]:
            print("      tick %d cos %d: %s" % (t, c, why))
        fail.append("%d of %d angles are not the clock's: the spinner is being "
                    "driven by something else, which before SPEC.md 15.3.6 was "
                    "[spl_done] - the progress bar"
                    % (len(bad), len(samples)))

    # --- 3. the RATE, either side of the notch rate changing -----------------
    # Stage 2 ticks the bar once per coalesced run and the boot mount once per
    # sector, so half of [spl_done]'s travel is the boundary between the two.
    # The rate is measured in positions of the sixteen per TICK and has to be
    # SPL_SPINSH's whichever stretch it is taken in - that is the whole of what
    # "a steady pace" means, and it is the property a notch-driven angle does
    # not have while still animating.
    #
    # IT IS MEASURED OFF THE ANGLE THAT WAS DRAWN, never off the tick: deriving
    # the travel from the tick is the clock checked against itself, which is a
    # tautology that passes on any build. The drawn angles are UNWRAPPED - a
    # cos value names one or two of the sixteen positions, because the table
    # repeats, so each sample is resolved to whichever candidate is the
    # shortest way FORWARD from the last one. The spinner only ever advances,
    # and at ~2.3 positions a frame it cannot lap between two samples.
    want_rate = 1.0 / (1 << shift)
    cut = max(d for _, _, d in samples) // 2 if samples else 0
    #
    # A row of `walk` is a TRANSITION and not a sample: it carries the tick it
    # started from as well as the one it ended at, so that a group's travel and
    # its span cover the same interval. Summing steps across a group while
    # measuring the span from the group's first END tick drops one transition's
    # travel and keeps its time, which reads as that stretch turning slower.
    walk, pos, prev = [], None, None
    for tick, cos, done in samples:
        cand = positions_for(tab, cos)
        if not cand:
            continue
        if pos is None:
            pos = cand[0]
        else:
            step = min((w - pos) % 16 for w in cand)    # forward travel
            pos = (pos + step) % 16
            walk.append((prev, tick, step, done))
        prev = tick
    def rate(group):
        span = group[-1][1] - group[0][0] if group else 0
        if len(group) < MIN_PHASE_SAMPLES or span <= 0:
            return None
        return sum(st for _, _, st, _ in group) / float(span)

    # The WHOLE LOAD first, because it is the statement that is always
    # available and the one with the least quantisation in it: a transition is
    # a handful of ticks and rounds hard, forty of them do not.
    whole = rate(walk)
    rates = {}
    for name, group in (("early", [w for w in walk if w[3] <= cut]),
                        ("late", [w for w in walk if w[3] > cut])):
        r = rate(group)
        if r is not None:
            rates[name] = r
    print("  positions/tick: whole %s%s (SPL_SPINSH=%d wants %.4f)"
          % ("%.4f" % whole if whole else "too few samples",
             "".join(", %s %.4f" % kv for kv in sorted(rates.items())),
             shift, want_rate))

    if whole is None:
        fail.append("the load gave %d angle transitions, too few to measure a "
                    "rate over - which is itself a spinner that is barely "
                    "moving" % len(walk))
    elif abs(whole - want_rate) > RATE_TOL * want_rate:
        fail.append("the logo turned at %.4f positions/tick over the load "
                    "against SPL_SPINSH's %.4f: whatever is driving the angle, "
                    "it is not the clock" % (whole, want_rate))
    for name, r in rates.items():
        if abs(r - want_rate) > RATE_TOL * want_rate:
            fail.append("the %s stretch turned at %.4f positions/tick against "
                        "SPL_SPINSH's %.4f: the angle is not the clock's over "
                        "that stretch" % (name, r, want_rate))
    if len(rates) == 2:
        a, b = rates["early"], rates["late"]
        if abs(a - b) > RATE_TOL * want_rate:
            fail.append("the logo turned at %.4f positions/tick while the "
                        "kernel loaded and %.4f while the disk was mounted: "
                        "THAT IS THE DEFECT - the notch rate changed and the "
                        "animation followed it" % (a, b))

    for f in fail:
        print("FAIL: %s" % f)
    print("splashspin: %s" % ("FAILED" if fail else
                              "the logo turns on the clock, at one rate"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
