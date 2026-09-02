#!/usr/bin/env python3
"""stkwater: how deep every task stack has actually been (SPEC.md 8).

    python3 tests/ftpd.py --kfz     # drives a real FTP session and reports
    python3 tools/stkwater.py build/qmp.sock       # ...or read a live guest

`task_spawn` fills every slice with 0xCC before it writes the canary and carves
the frame (SPEC.md 8.3) - on the SHIPPING kernel, not only a `KFZ=1` one - so
each slice carries its own high water and this only has to read them out of
`LOW_SEG`. `KFZ=1` adds the deepest-tick sample (8.4) that names the code.

**The slice size comes off the kernel, never from here.** `SCH_STACK` was 256
until the field measured 220 of it in use during an FTP upload
(docs/FIELD-NOTES.md 27.7) and is 384 now; `slice_len()` asks `os88sym` for the
equate rather than mirroring a number that would go stale the day it mattered.

**Read the DEPTH, not the occupancy.** A slice is written at both ends by
construction - the canary at the bottom, the spawn frame at the top - so the
fill only ever answers "how far down from the top did anything reach", and a
slot that was never scheduled still shows its 24-byte frame. A slot reading the
whole slice has gone through the canary and `sch_switch` has already halted the
machine.

**QEMU understates a real BIOS by ~46 bytes**, measured (docs/KERNEL-MEMORY.md):
SeaBIOS services its interrupt entries on a stack of its own, where an IBM ROM
runs int 08h on whichever task stack is current - and under QEMU a worker's pass
can finish between ticks and never pay the interrupt at all. Add it before
deciding a number, and take the deciding one on the 5150 with
`tests/stackprobe`, which reads every slice while the thing under suspicion
runs.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88sym                                              # noqa: E402

FILL = 0xCC
MAGIC = b"\x57\x5A"           # SCH_MAGIC = 0x5A57, at the slice BOTTOM
DEF = ("KFZTRACE",)


def slice_len(defines=DEF):
    """SCH_STACK, out of the kernel that was actually built.

    NOT a mirrored 256. `syms()` answers labels only, so the first version of
    this asked it for SCH_STACK, got nothing, fell back to its own default and
    reported a 256-byte slice whatever the kernel had - which is the exact
    failure a probe exists to prevent.
    """
    return int(os88sym.equates(defines).get("SCH_STACK", 256)) or 256


def slots(defines=DEF):
    """MAX_TASKS - 1: slot 0 is the UI task and owns no slice."""
    return int(os88sym.equates(defines).get("MAX_TASKS", 12)) - 1


def water(mem, nslots, n):
    """[(slot, used, untouched)] - `mem` is the slices, back to back.

    **THE CANARY IS AT THE BOTTOM OF EVERY SLICE**, and the first version of
    this walked straight into it: `SCH_MAGIC` is not the fill byte, so every
    slot read 256 of 256 and the report said the machine was dead eleven times
    over while the gate it ran inside passed every assertion. The scan starts
    ABOVE the canary word, and a slot whose canary is not there was never
    spawned (or has already gone through the floor, which `sch_switch` halts
    on) and is reported as such rather than measured.
    """
    out = []
    for i in range(nslots):
        blk = mem[i * n:(i + 1) * n]
        if len(blk) < n:
            break
        if blk[0:2] != MAGIC:
            out.append((i + 1, None, None))  # never spawned - no fill either
            continue
        j = 2
        while j < n and blk[j] == FILL:      # the slice grows DOWN, so the
            j += 1                           # first non-fill ABOVE the canary
        out.append((i + 1, n - j, j))        # is how far anything ever reached
    return out


def report(mem, nslots, n, note="", base=None):
    print("== task stack high water %s ==" % note)
    if base is not None:
        print("   slice %d bytes, sch_stacks at linear 0x%05X" % (n, base))
    worst, worst_slot = 0, 0
    for slot, used, free in water(mem, nslots, n):
        if used is None:
            print("   slot %-2d  -   never spawned (no canary)" % slot)
            continue
        bar = "#" * (used * 40 // n)
        print("   slot %-2d  %3d used  %3d free  %s" % (slot, used, free, bar))
        if used > worst:
            worst, worst_slot = used, slot
    print("   ---")
    if not worst:
        print("   NO SLICE WAS EVER FILLED. Is this a `make KFZ=1` kernel? "
              "Nothing else fills them.")
        return 0
    print("   deepest %d of %d (%.0f%%) on slot %d"
          % (worst, n, 100.0 * worst / n, worst_slot))
    if worst >= n - 2:
        print("   *** AT OR THROUGH THE CANARY: sch_switch halts the machine")
    return worst


def _near(syms, off, window=1024):
    """The symbol `off` is inside, as name+delta - or None if nothing is near.

    `window` bounds it: a stack word that happens to be a small number is not a
    call into whatever label sits 30KB below it, and a resolver that says so
    anyway turns a dump into a page of confident noise.
    """
    best, bestd = None, window + 1
    for name, addr in syms.items():
        d = off - addr
        if 0 <= d < bestd:
            best, bestd = name, d
    if best is None:
        return None
    return best if bestd == 0 else "%s+%d" % (best, bestd)


def is_call_site(img, w):
    """Does a `call` end exactly at offset `w` in `img`?

    **`img` MUST BE INDEXED THE WAY `w` IS.** `w` comes off a guest stack, so
    it is a segment-relative offset; for the kernel that means `.text`, and
    since SPEC.md 2.9 the raw kernel.bin is NOT that image - stage 2 sits in
    front of it. Handed the raw file this recognised 126 of 3,000 real
    near-call sites, which does not read as a bug: the dump simply loses its
    "<- call" attributions and keeps a few coincidences, which is the false
    confidence the guard below exists to prevent, arriving by another road.
    os88layout.kernel_text() is the image to pass. A driver image needs no
    such adjustment - `.text` is where it starts.

    **This is what turns the dump into a chain.** A near return address points
    at the byte after a call, so the bytes just before it have to BE a call -
    and almost no random word passes. Without it every stack word matched some
    label within a kilobyte and the page read as a confident list of routines
    the machine had never been in.

        E8 lo hi          call rel16       (3 bytes)
        FF /2  mod r/m    call r/m16       (2-4)
        9A lo hi sg sg    call far ptr     (5)
        FF /3  mod r/m    call far r/m     (2-4)
    """
    if w < 3 or w > len(img):
        return None
    if img[w - 3] == 0xE8:
        return "near"
    if img[w - 5:w - 4] == b"\x9A":
        return "far"
    for n in (2, 3, 4):                      # FF with a mod r/m of 2 or 3
        if w - n >= 0 and img[w - n] == 0xFF:
            if ((img[w - n + 1] >> 3) & 7) in (2, 3):
                return "indirect"
    return None


def annotate(blk, used, n, kern=None, drv=None, seg=None,
             kimg=None, dimg=None):
    """The touched part of one slice, word by word, with the code it names.

    **This is the point of the fill.** Between the high-water mark and wherever
    SP is now, the bytes are DEAD - left there by the deepest excursion and
    overwritten by nothing shallower - so the return addresses of the chain
    that went deepest are still sitting in them. Reading them back is how the
    bytes get attributed to routines instead of argued about.

    A word is only called a return address if a `call` really ends at it
    (`is_call_site`); everything else is printed as a bare value, because a
    saved register that happens to land near a label is not a frame.
    """
    kern, drv = kern or {}, drv or {}
    top = n
    print("   --- the deepest excursion, top of slice first ---")
    print("   (only words a `call` actually ends at are named: everything "
          "else is a saved register or a local)")
    for off in range(top - 2, max(n - used - 2, 0), -2):
        w = blk[off] | (blk[off + 1] << 8)
        depth = top - off
        if blk[off] == FILL and blk[off + 1] == FILL:
            continue
        tags = []
        if kimg and is_call_site(kimg, w):
            nm = _near(kern, w, 4096)
            tags.append("KERNEL %s <- %s call" % (nm, is_call_site(kimg, w)))
        if dimg and is_call_site(dimg, w):
            nm = _near(drv, w, 4096)
            tags.append("ETHER  %s <- %s call" % (nm, is_call_site(dimg, w)))
        if seg and w == seg:
            tags.append("the driver's SEGMENT")
        if not tags:
            continue
        print("   -%-3d  %04X   %s" % (depth, w, "   |   ".join(tags)))


def deepest_seen(read, base, drv=None, dimg=None, pkg=None, pimg=None,
                 pseg=None):
    """What the kernel itself saw: the deepest tick, and the code at it.

    The 0xCC fill says HOW DEEP the slice went and nothing about what was
    running. Those two answers disagreed - 32 bytes came off the driver's
    static worst chain and the measured water moved six - because the runtime
    path is not the static one: an ARP cache that hits and a DHCP timer with
    nothing to do are both shallower than the worst case `stkdepth.py` prices.
    A `KFZ=1` `sch_isr` keeps the deepest tick it has seen and the interrupted
    CS:IP at it, so the next cut can be aimed at the chain the machine is
    actually in.

    SAMPLED, at 18.2Hz - so it is a lower bound on the depth and a good name
    for the path, where the fill is an exact depth and no name at all. Read
    them together.
    """
    deep, cur, csh, iph, ipl = read(base, 5)    # `base` is LINEAR, not an
    deep *= 2                                   # stored HALVED so one byte
                                                # holds a 384-byte slice
                                                # (sched.inc's deep sampler)
                                                # offset: os88sym.syms() answers
                                                # within the symbol's own
                                                # segment, and the first version
                                                # handed that straight to a
                                                # physical read and reported
                                                # slot 34
    print("== the deepest TICK the kernel saw ==")
    if not deep:
        print("   nothing recorded - no background task was ever sampled")
        return
    print("   %d of %d bytes, on slot %d" % (deep, slice_len(), cur))
    where = "%02X:%02X%02X" % (csh, iph, ipl)
    off = (iph << 8) | ipl
    tag = "kernel" if csh == 0 else ("the package" if pseg and csh == (pseg >> 8)
                                     else "a package or the driver")
    print("   at %s (%s)" % (where, tag))
    for name, syms, img in (("ETHER", drv, dimg), ("package", pkg, pimg)):
        if not syms or not img:
            continue
        near = _near(syms, off, 4096)
        if near:
            print("   %-8s %s%s" % (name, near,
                                    "" if off >= len(img) else ""))


def read_live(sock="build/qmp.sock"):
    """The slices out of a running QEMU, through tests/ethernet.py's Qemu."""
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sys.path.insert(0, os.path.join(here, "tests"))
    import ethernet as eth
    n = slice_len()
    base = os88sym.linear("sch_stacks", DEF)
    return eth.Qemu(sock).read(base, slots() * n), base, n


def main(argv):
    sock = argv[1] if len(argv) > 1 else "build/qmp.sock"
    mem, base, n = read_live(sock)
    return 0 if report(mem, slots(), n, "(live, %s)" % sock, base) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
