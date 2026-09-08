#!/usr/bin/env python3
"""Does a 32-wide icon HANGING OFF THE RIGHT EDGE still clip byte for byte?
(SPEC.md 25.6, 32/39.5 - `ico_pass_bb`'s per-byte column test)

    make && python3 tests/icoclip.py

`ico_core` clips an icon VERTICALLY and, when a clip region is armed, refuses
the whole shape - but it does **not** refuse one that hangs off the right
edge. The horizontal clip is per BYTE COLUMN and lives inside the pass:
`ico_pass_bb` walks a running column and drops the three-byte window's bytes
one at a time as they go past the row stride. That is the only thing standing
between a 32-wide icon at x = w-8 and a write that lands on the NEXT SCAN
LINE.

**Nothing on a stock desktop reaches it.** The volume zone is right-hung at
`vid_w - 56` and a 32-wide icon sits four byte columns inside the stride, so a
boot-and-diff harness is a null A/B that looks exactly like a pass. The case
has to be ARRANGED, and this file arranges it by CALLING `icon_draw` directly
through the debugger with an x the desktop never uses - which is also the only
way to sweep all eight shift phases, since the phase is `x & 7` and a drawn
icon's x is whatever its owner chose.

THE BACKGROUND IS ZEROED FIRST, and that is not tidiness. The two passes are
`or [es:di],mask` and `and [es:di],~data`, so the bytes that land depend on
what was under them; over a zeroed row the result is `mask & ~data`, a pure
function of the record and the phase, and two draws at two different columns
are then comparable. Over the desktop's 50% dither they are not.

THREE ASSERTIONS, per adapter, per phase:

  1. **Nothing outside the icon's own byte columns is touched** - the whole
     row range is zeroed and every byte outside `col0 .. min(col0+4, stride-1)`
     must still read zero afterwards, INCLUDING the row after the last one,
     which is where a broken row advance lands.
  2. **A clipped icon is the left part of an unclipped one.** The same record
     at the same phase is drawn clear of the edge and again at each of the
     four columns that hang off it; column j of the clipped draw must equal
     column j of the reference, for every j the stride still holds.
  3. **The columns that fall off are DROPPED, not wrapped** - implied by 1,
     asserted separately so a failure says which.

BOTH 1bpp ADAPTERS, because the clip limit IS the stride and the two differ:
CGA is 80 bytes a row and Hercules 90. A limit that has been left at a
constant passes on one and fails on the other.

VGA IS NOT TESTED HERE. `ico_pass` (the planar twin) has its own column
bookkeeping and its own `ico_col` cell; this file is about the software
renderer's, which is the one every 1bpp machine draws every icon through.

`--records` AND `--entry`: THE ART GATE
---------------------------------------
The default sweep is `ico_disk32` through `icon_draw`, which is what this row
has always done and what the three assertions above were written for. The two
options widen it into the gate a change to the ICON ART needs, which is a
different question: *does the picture survive?*

    # BEFORE the change, on the tree it will land on:
    python3 tests/icoclip.py --dump before.json \
        --records ico_disk32,ico_disk14,ico_hdd32,ico_hdd14
    # AFTER it, through whatever entry now draws them:
    python3 tests/icoclip.py --dump after.json --entry icon_draw_ix \
        --records ico_disk32,ico_disk14,ico_hdd32,ico_hdd14
    python3 -c "import json,sys; a=json.load(open('before.json')); \
                b=json.load(open('after.json')); \
                d=[k for k in a if a[k]!=b.get(k)]; \
                print(d or 'identical'); sys.exit(1 if d else 0)"

Four records, two adapters, eight phases, every clipped column - the dump is
keyed `adapter/record/phase/column`, so a diff names the icon and the phase
rather than saying that something changed. A change that compresses the art
claims the SAME PIXELS out of fewer bytes, and that diff is that claim: it
does not care how the record is encoded, only what reaches the framebuffer.

The three assertions still run on the new entry, and on this change they are
worth more than the diff. A decoder that stages a record into a buffer and
gets the sizing wrong writes PAST it, and `ico_stage` is 66 bytes with the
kernel's own cells (`ico_ww`, `ico_h`, `ico_rows`, ...) immediately after it -
so an overrun is a corruption bug rather than a drawing one and the pixels can
be perfect while it happens. That is why this row also reports the SPAN of
`.bss` the draws moved, from `ico_stage` across 512 bytes: the claim a staging
decoder makes is "I write my own buffer and nothing else", and the span is
that claim measured rather than assumed. It is printed on every run and
carried in the dump, so the before/after diff sees it too.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

KERNEL_SEG = 0x60
KB = KERNEL_SEG << 4

# THE GLaBIOS TWINS - `icon_draw` is called through the debugger over a
# zeroed background, so nothing here reaches the BIOS (os88marty.machine).
MACHINES = (("cga", os88marty.machine("os8088_5150_cga"), 0xB800, 0x4000),
            ("herc", os88marty.machine("os8088_5150_herc"), 0xB000, 0x8000))

ICON_Y = 64                     # a blank band of desktop, well clear of the
                                # menu bar and of the volume zone's own icons


class Caller(object):
    """Call a near kernel routine on a PAUSED machine.

    The return address is a kernel symbol nothing on the path executes, armed
    as an exec breakpoint: the routine's own `ret` jumps there and the
    breakpoint fires before a single instruction of it runs.

    THE ENTRY IS `park`, NOT `setreg("ip")`, and the emulator refuses the
    second on purpose: `pc` is the FETCH pointer, so a bare write leaves the
    bytes the 8088 had already prefetched from the old address sitting in
    front of the new ones (`tools/martypc/debug_server.rs`, `reg_of`). `park`
    goes through the reset vector, which flushes the queue - and CLEARS EVERY
    REGISTER, so all of them are written after it and not before. It also
    leaves IF clear, which is what this harness wants: no tick, no mouse, and
    nothing else drawing between the clear and the read-back.

    The guest is never resumed into its own scheduler afterwards. Each call
    starts from `park` again, so the only state that carries between them is
    MEMORY - which is the thing being measured.
    """

    def __init__(self, m, trap="icon_draw_x"):
        self.m = m
        self.trap_off = os88sym.linear(trap) - KB
        r = m.regs()
        self.ss, self.sp = r["ss"], (r["sp"] - 64) & 0xFFFF
        m.bp_exec(os88sym.linear(trap))

    def call(self, name, **regs):
        m = self.m
        m.cmd(cmd="park", cs=KERNEL_SEG, ip=os88sym.linear(name) - KB)
        sp = (self.sp - 2) & 0xFFFF
        m.write((self.ss << 4) + sp,
                bytes((self.trap_off & 0xFF, self.trap_off >> 8)))
        m.setreg("ss", self.ss)
        m.setreg("sp", sp)
        m.setreg("ds", KERNEL_SEG)
        m.setreg("es", KERNEL_SEG)
        for r, v in regs.items():
            m.setreg(r, v & 0xFFFF)
        m.run()
        if m.wait_stop(30.0) is None:
            raise SystemExit("icoclip: %s never returned - the machine is "
                             "still running after 30s" % name)
        return m.regs()


def u16(m, name):
    b = m.read(os88sym.linear(name), 2)
    return b[0] | (b[1] << 8)


def run(machine, tag, fbseg, fbsize, verbose, dump=None,
        records=("ico_disk32",), entry="icon_draw"):
    bad = []
    with os88marty.launch(os.path.join(ROOT, "build", "os8088-360.img"),
                          apps=os.path.join(ROOT, "build", "apps360.img"),
                          machine=machine, label="icoclip") as m:
        m.pause()
        stride = u16(m, "vid_stride")
        mono = m.read(os88sym.linear("vid_mono"), 1)[0]
        if not mono:
            raise SystemExit("icoclip: %s came up NOT mono (vid_mono=%d) - "
                             "this row is about the software renderer"
                             % (tag, mono))
        # `icon_draw_x` is the return trap for every entry this row
        # drives, and stays off the path for all of them: the trap is an
        # exec breakpoint at that symbol's OWN address, so an entry that
        # merely falls into the same common tail never re-enters it.
        c = Caller(m, trap="icon_draw_x")

        # The .bss window the decode is allowed to write.  It is read
        # whole before the first draw and again after the last, and the
        # CHANGED SPAN is reported rather than whitelisted: the claim a
        # staging decoder makes is "I write my own buffer and nothing
        # else", and a span is that claim measured.  The machine is
        # never resumed into its own code after this, so a byte the
        # harness observes moving harms nothing here - it is evidence.
        bss_lo = os88sym.linear("ico_stage")
        bss_len = 512
        bss_before = m.read(bss_lo, bss_len)

        for recname in records:
            rec = os88sym.linear(recname) - KB
            hdr = m.read(os88sym.linear(recname), 2)
            ww, ih = hdr[0], hdr[1]
            if not (1 <= ww <= 4 and 1 <= ih <= 64):
                raise SystemExit("icoclip: %s reads as %dx%d words/rows, "
                                 "which is not an icon record - the symbol "
                                 "is wrong or the kind has changed"
                                 % (recname, ww, ih))
            # The row table, from the kernel's own gfx_rowbase - the banking is
            # the adapter's and is not re-derived here.
            rowbase = [c.call("gfx_rowbase", ax=y)["ax"]
                       for y in range(ICON_Y, ICON_Y + ih + 1)]
            fb = fbseg << 4

            def clear():
                for rb in rowbase:
                    m.write(fb + rb, bytes(stride))

            def draw(x):
                clear()
                c.call(entry, cx=x, dx=ICON_Y, si=rec)
                rows = [m.read(fb + rb, stride) for rb in rowbase]
                return rows

            # ico_pass_bb's window is three bytes per 16-pixel word and the words
            # step by two, so a ww=2 record covers col0 .. col0+4 - five columns
            # at any non-zero phase and four at phase 0.
            span = 2 * ww + 1

            for phase in range(8):
                ref_col = 4                       # nowhere near either edge
                ref = draw(ref_col * 8 + phase)
                if dump is not None:
                    dump["%s/%s/%d/ref"
                         % (tag, recname, phase)] = b"".join(ref).hex()
                # 0. THE POSITIVE CONTROL, and it is not decoration: every
                #    assertion below is of the form "nothing outside" or "the same
                #    as the reference", and all of them pass on a harness that
                #    draws NOTHING AT ALL. A 32x32 diskette lights most of its own
                #    footprint, so this is a floor with a lot of headroom rather
                #    than a fitted number.
                #
                #    IT IS PER ROW, and it was a bare 64 until the sweep grew
                #    past one record. 64 is `2 * ih` for the 32-row diskette it
                #    was fitted to - two bytes of a five-byte window a row -
                #    so the default sweep's floor is unchanged to the number,
                #    while the two 14-row records (SPEC.md 26.4) get 28 rather
                #    than a bar 14 rows CANNOT clear. Measured on this tree,
                #    over all 8 phases and both strides: ico_disk32 117..148,
                #    ico_hdd32 74..96, ico_disk14 45..58, ico_hdd14 32..42 -
                #    the last is the thin one, and it is a deterministic
                #    number (the band is zeroed first, so the ink is a pure
                #    function of the record and the phase), not a sampled one.
                ink = sum(1 for row in ref[:ih] for col in range(stride) if row[col])
                if ink < 2 * ih:
                    bad.append("%s %s phase %d: the unclipped icon wrote %d "
                               "non-zero bytes of a floor of %d (2 a row) - the "
                               "harness is not drawing, and every assertion "
                               "below passes on that"
                               % (tag, recname, phase, ink, 2 * ih))
                # 1. the reference itself must stay inside its own columns.
                for r, row in enumerate(ref):
                    for col in range(stride):
                        if ref_col <= col < ref_col + span:
                            continue
                        if row[col]:
                            bad.append("%s phase %d: the UNCLIPPED icon wrote "
                                       "column %d of row %d (its own columns are "
                                       "%d..%d)" % (tag, phase, col, r, ref_col,
                                                    ref_col + span - 1))
                if len(ref) != ih + 1 or any(ref[ih]):
                    bad.append("%s phase %d: the unclipped icon wrote the row "
                               "AFTER its last (%d) - the row advance is wrong"
                               % (tag, phase, ICON_Y + ih))

                for col0 in range(stride - span + 1, stride):
                    got = draw(col0 * 8 + phase)
                    if dump is not None:
                        dump["%s/%s/%d/%d"
                             % (tag, recname, phase, col0)] = \
                            b"".join(got).hex()
                    kept = stride - col0          # columns the stride still holds
                    for r in range(ih):
                        for j in range(span):
                            col = col0 + j
                            if col >= stride:
                                continue
                            if got[r][col] != ref[r][ref_col + j]:
                                bad.append(
                                    "%s phase %d col0 %d: row %d column %d is "
                                    "0x%02X, the unclipped icon's column %d is "
                                    "0x%02X" % (tag, phase, col0, r, col,
                                                got[r][col], j,
                                                ref[r][ref_col + j]))
                                break
                        # 2/3. nothing outside, and nothing wrapped onto the next
                        #      scan line.
                        for col in range(stride):
                            if col0 <= col < col0 + span:
                                continue
                            if got[r][col]:
                                bad.append(
                                    "%s phase %d col0 %d: row %d column %d was "
                                    "written and is not the icon's (%d columns "
                                    "fit)" % (tag, phase, col0, r, col, kept))
                                break
                    if any(got[ih]):
                        bad.append("%s phase %d col0 %d: the row AFTER the icon "
                                   "was written - a dropped byte still has to "
                                   "advance DI" % (tag, phase, col0))
                    if verbose:
                        print("  %s phase %d col0 %-3d %d of %d columns fit, "
                              "reference ink %d  ok"
                              % (tag, phase, col0, kept, span, ink))
            print("%s %s (%dx%d): stride %d, 8 phases x %d clipped columns"
                  % (tag, recname, ww * 16, ih, stride, span - 1))

        bss_after = m.read(bss_lo, bss_len)
        moved = [i for i in range(bss_len) if bss_before[i] != bss_after[i]]
        if moved:
            lo, hi = moved[0], moved[-1]
            span_txt = "ico_stage+%d .. +%d (%d bytes)" % (lo, hi, hi - lo + 1)
        else:
            span_txt = "none"
        print("  %s: the draws moved .bss %s, of a %d-byte window"
              % (tag, span_txt, bss_len))
        if dump is not None:
            dump["%s/bss" % tag] = span_txt
        print("%s: entry %s, %d record(s), %d findings"
              % (tag, entry, len(records), len(bad)))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--machine", help="one adapter only (cga | herc)")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--records", default="ico_disk32",
                    help="comma list of icon records to sweep (default "
                         "ico_disk32, which is what this row has always "
                         "done). `ico_disk32,ico_disk14,ico_hdd32,ico_hdd14` "
                         "is the four the DESKTOP draws, and is the set a "
                         "change to the ART has to hold identical")
    ap.add_argument("--entry", default="icon_draw",
                    help="the kernel entry to draw them through (default "
                         "icon_draw). A record kind with its own entry is "
                         "swept by naming it here - the pixels are then "
                         "comparable across the change, which is the claim")
    ap.add_argument("--dump", metavar="PATH",
                    help="also write every frame this row drew to PATH as "
                         "JSON, so two KERNELS can be diffed against each "
                         "other and not only against their own reference - "
                         "which is how F-icons-2's rewrite of this clip was "
                         "checked before it landed")
    a = ap.parse_args()
    os88sym.default_defines()
    bad = []
    dump = {} if a.dump else None
    for tag, machine, seg, size in MACHINES:
        if a.machine and a.machine != tag:
            continue
        bad += run(machine, tag, seg, size, a.verbose, dump,
                   records=tuple(r for r in a.records.split(",") if r),
                   entry=a.entry)
    if a.dump:
        import json
        with open(a.dump, "w") as f:
            json.dump(dump, f, indent=0, sort_keys=True)
        print("icoclip: %d frames -> %s" % (len(dump), a.dump))
    if bad:
        print("\nicoclip: FAIL")
        for b in bad[:40]:
            print("  " + b)
        if len(bad) > 40:
            print("  ... and %d more" % (len(bad) - 40))
        return 1
    print("\nicoclip: ok - a clipped icon is the unclipped one's left part, "
          "on both strides, at every phase")
    return 0


if __name__ == "__main__":
    sys.exit(main())
