#!/usr/bin/env python3
"""kfzread: decode the KFZ heartbeat strip out of a screenshot.

    make KFZ=1                       # ...and boot THAT kernel
    python3 tools/kfzread.py shot.png [shot2.png ...]

A `KFZ=1` kernel paints fifteen bytes of kernel state into the top-left of
the menu bar from IRQ0, twice per tick (SPEC.md 9.6.5, kernel/sched.inc).
**That strip is the only instrument that survives a hard freeze**: the field's
last one took the timer interrupt with it, so the 30-second watchdog never
fired and the LAST PICTURE ON THE GLASS was the whole of the evidence.

This turns that picture back into numbers, so a photograph is read
mechanically instead of by eye - which is how `beat` one ahead of `chain` got
noticed at all.

--- THE ENCODING, and every part of it is for the camera -------------------
Two bits per framebuffer byte, four pixels per bit, high bit first; a set bit
is BLACK on the white bar; four banks tall, so the strip is four screen rows
of identical pixels. Four bytes make one 8-bit value, then one blank byte,
then the next: 15 values, 75 bytes, 600 pixels.

At one pixel per bit a photographed cell was unreadable, which is why the
kernel spends four.

--- WHAT IT TAKES ----------------------------------------------------------
Any PNG of the guest: 86Box's own screenshot (clean, origin 0,0) or a capture
of the whole emulator window with its chrome. The strip is FOUND rather than
assumed - the run-length structure is strong enough to lock onto, and a wrong
lock cannot produce it - so no offsets have to be passed. `--at X,Y,SCALE`
overrides if the search ever fails.
"""
import argparse
import struct
import sys
import zlib

NVAL = 15
NAMES = ["beat", "chain", "kfz", "sch_cur", "sch_lock", "gfx_lock_flag",
         "gfx_lock_own", "SP hi", "SP lo", "stk0 bad", "CS hi", "IP hi",
         "IP lo", "PIC mask", "PIC in-service"]


def load_png(path):
    """Minimal PNG reader - greyscale/RGB/palette, 8-bit, no interlace."""
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("%s: not a PNG" % path)
    pos, idat, pal, w, h, depth, ctype = 8, b"", None, 0, 0, 0, 0
    while pos < len(d):
        ln, = struct.unpack(">I", d[pos:pos + 4])
        typ = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
        elif typ == b"PLTE":
            pal = body
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        pos += 12 + ln
    if depth != 8:
        sys.exit("%s: %d-bit PNG, only 8-bit is handled" % (path, depth))
    nch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * nch
    out, prev = [], bytearray(stride)
    p = 0
    for _ in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        for i in range(stride):
            a = line[i - nch] if i >= nch else 0
            b = prev[i]
            c = prev[i - nch] if i >= nch else 0
            if f == 1: line[i] = (line[i] + a) & 0xFF
            elif f == 2: line[i] = (line[i] + b) & 0xFF
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif f == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out.append(bytes(line)); prev = line
    # ...to one luminance byte per pixel. The BRIGHTEST channel, not the red
    # one: a Hercules capture off a green monitor has R around 0x39 for the
    # LIT background and 0x00 for black, so a red-channel reading calls the
    # whole picture dark and the strip is never found.
    rows = []
    for line in out:
        if ctype == 3:
            rows.append([max(pal[3 * v], pal[3 * v + 1], pal[3 * v + 2])
                         for v in line])
        elif ctype in (0, 4):
            rows.append([line[i * nch] for i in range(w)])
        else:
            rows.append([max(line[i * 3], line[i * 3 + 1], line[i * 3 + 2])
                         for i in range(w)])
    return w, h, rows


def bits_from(row, x0, scale, th=128):
    """600 pixels -> 15 bytes, or None if the run structure does not hold.

    THE MIDDLE OF EACH BIT DECIDES IT, not the whole run. 86Box's own PNG is
    nearest-neighbour and every bit really is four solid pixels, but the field
    also sends captures of the monitor WINDOW, which are filtered: each edge
    ramps over three or four pixels and an all-or-nothing test rejects the
    strip outright. So the middle half of each bit is read and has to agree
    with itself - which still refuses anything that is not four-pixel cells,
    because a run of ordinary pixels disagrees somewhere within the first few
    bits.
    """
    vals, x = [], x0
    span = 4 * scale
    lo = span // 4                             # the middle half, so a ramp at
    hi = span - lo                             # either edge is never read
    if lo == hi:                               # scale 1: one pixel IS the bit
        lo, hi = 0, span
    for v in range(NVAL):
        byte = 0
        for b in range(8):
            run = row[x + lo:x + hi]
            if len(run) < hi - lo:
                return None
            dark = sum(1 for p in run if p < th)
            if dark not in (0, len(run)):
                return None
            byte = (byte << 1) | (1 if dark else 0)
            x += span
        vals.append(byte)
        x += 2 * span                          # ...then one blank byte, which
                                               # is NOT checked: sch_stkdie
                                               # writes a solid bar straight
                                               # across four cells and their
                                               # gaps, and a strip carrying
                                               # that report is exactly the
                                               # one worth reading
    return vals


KHB_STK = 20                    # sch_stkdie's bar: byte 20, eight bytes of
                                # solid black, then one skipped byte and
                                # ~sch_cur (kernel/sched.inc). It lands on top
                                # of cells 4 and 5, which is why those two are
                                # reported as OVERWRITTEN rather than believed


def find(rows, w, h, th=128):
    """Search the whole image, not just the corner.

    A clean 86Box screenshot puts the strip at (0, 0); a capture of the whole
    emulator WINDOW puts it a hundred-odd pixels down and some pixels in, and
    the field sends both. `bits_from` bails on the first bit that is not solid,
    so almost every candidate costs a handful of compares and the wide search
    is affordable.
    """
    for scale in (4, 3, 2, 1):          # LARGEST FIRST: a genuine 2x strip must
        need = 600 * scale              # win before a 1x coincidence somewhere
        if w < need:                    # else in the image
            continue
        tall = 4 * scale                # four banks, so four guest rows
        for y in range(0, h - tall + 1):
            for x0 in range(0, w - need + 1):
                v = bits_from(rows[y], x0, scale, th)
                if v is None:
                    continue
                if len(set(v)) == 1:    # fifteen identical values is a flat
                    continue            # region, not a strip
                # **AND IT MUST REPEAT DOWN THE BANKS.** The first version took
                # the first thing whose run structure held and locked onto the
                # emulator's own window chrome, reporting beat FF chain 00 off
                # a picture of a border. The kernel paints the strip into four
                # banks precisely so a photograph is legible, and that
                # repetition is the signature. A MAJORITY of the rows, not all
                # of them: a filtered 2x capture blends the row either side of
                # each guest-row boundary, and demanding every row rejected the
                # very captures the filtering exists on.
                same = sum(1 for d in range(1, tall)
                           if bits_from(rows[y + d], x0, scale, th) == v)
                if same * 2 >= tall:
                    return v, x0, y, scale
    return None, 0, 0, 0


def threshold(rows):
    """Halfway between the darkest and the brightest pixel in the image.

    A green-phosphor capture is not black-and-white: the LIT background can sit
    anywhere depending on how the monitor window was captured, and a fixed 128
    reads the whole picture as one colour.
    """
    lo, hi = 255, 0
    for r in rows:
        for v in r:
            if v < lo: lo = v
            if v > hi: hi = v
    return (lo + hi) // 2


def report(path, at=None):
    w, h, rows = load_png(path)
    th = threshold(rows)
    if at:
        x0, y, scale = at
        vals = bits_from(rows[y], x0, scale, th)
    else:
        vals, x0, y, scale = find(rows, w, h, th)
    print("== %s ==" % path)
    if vals is None:
        print("   NO STRIP FOUND. Is this a KFZ=1 kernel (`make KFZ=1`), on a")
        print("   MONO adapter? The strip is Hercules/CGA banked and is not")
        print("   painted on VGA at all. --at X,Y,SCALE forces a position.")
        return 1
    # --- did the canary die? ------------------------------------------------
    # sch_stkdie paints eight solid black bytes at KHB_STK and then ~sch_cur,
    # and it does it AFTER the last heartbeat and before the cli/hlt - so a
    # strip carrying that bar is not a strip that failed to decode, it is the
    # kernel's own verdict written across two of its cells.
    span = 4 * scale
    def px(byte, bit):                         # the middle of ONE PIXEL of one
        return x0 + (byte * 8 + bit) * scale + scale // 2   # framebuffer byte
    bar = [rows[y][px(KHB_STK + b, i)] for b in range(8) for i in range(8)]
    stk = sum(1 for v in bar if v < th) * 10 >= len(bar) * 9   # 90%, not all:
                                # a filtered capture ramps across the first and
                                # last pixel of any solid run, and the bar's
                                # verdict must not turn on those two
    print("   strip at x=%d y=%d, %dx scale (threshold %d)" % (x0, y, scale, th))
    for n, v in zip(NAMES, vals):
        note = ""
        if stk and n in ("sch_lock", "gfx_lock_flag", "gfx_lock_own"):
            note = "  <- UNDER sch_stkdie's bar, not a reading"
        print("   %-16s %02X  (%d)%s" % (n, v, v, note))
    beat, chain = vals[0], vals[1]
    csh, iph, ipl = vals[10], vals[11], vals[12]
    print("   ---")
    if stk:
        print("   *** sch_stkdie's BAR IS ON THE SCREEN: A TASK OVERRAN ITS")
        print("   *** STACK SLICE and the kernel halted (cli/hlt).")
        print("   *** The task is %d - sch_cur, cell 3, which the bar does not"
              % vals[3])
        print("   *** reach. Its slice is sch_stacks + (%d-1)*SCH_STACK in"
              % vals[3])
        print("   *** LOW_SEG, and SP below says how deep the machine already")
        print("   *** was at the last tick.")
    print("   stopped at  %02X:%02X%02X   (CS high: 00 kernel, 0D cold, "
          "else a package)" % (csh, iph, ipl))
    d = (beat - chain) & 0xFF
    if d == 0:
        print("   beat == chain: the timer interrupt RAN TO THE END, so "
              "whatever stopped")
        print("   the machine is out in task code.")
    elif d == 1:
        print("   beat is ONE AHEAD of chain: the machine died INSIDE the "
              "timer interrupt")
        print("   and never came back from the BIOS int 08h chain.")
    else:
        print("   beat - chain = %d, which is neither 0 nor 1 - the strip was "
              "caught mid-paint" % d)
    print("   SP %02X%02X   (subtract it from the slice TOP for the depth; "
          "the slice" % (vals[7], vals[8]))
    print("        size is SCH_STACK - `python3 tools/stkwater.py` reads it "
          "off the kernel)")
    print("   PIC mask %02X in-service %02X   (IRQ0 is bit 0 of each)"
          % (vals[13], vals[14]))
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("png", nargs="+")
    ap.add_argument("--at", help="X,Y,SCALE - force the strip's position")
    a = ap.parse_args()
    at = tuple(int(v) for v in a.at.split(",")) if a.at else None
    rc = 0
    for p in a.png:
        rc |= report(p, at)
        print()
    sys.exit(rc)
