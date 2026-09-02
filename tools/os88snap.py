#!/usr/bin/env python3
"""Read the SPEC.md 11.94.2 glyph-alignment histograms out of a running os8088.

`make SNAPAUDIT=1` compiles four 8-word tables into the kernel - snap_hchar and
snap_hrun for what a WINDOW draws inside its own callback, snap_kchar/snap_krun
for the chrome around it - counting the `x & 7` of every glyph font_char and
font_run are asked to draw. It also logs the pen, the pen y, the CHARACTER and
the caller for the first off-grid glyphs, so this prints the text that is
off-grid rather than a bucket count. Reset, drive the app, read.

A CAPTION IS CHROME (SPEC.md 11.94.2): wm_draw_title centres its pen and no app
can influence it, so it is excluded from the window's own tables. It used to be
counted there, which showed up as a constant 4 glyphs in bucket 7 for every app -
the 'APPS' caption of the Disk window behind them - and made small counts
worthless. That is fixed; small counts mean what they say now.

A DRAG NO LONGER FORCES A REPAINT: SPEC.md 11.96.12's cache replays the content
instead of calling W_PAINT. Reset with no filter and then LAUNCH the app.

It only means anything because alignment is the DEFAULT now (SPEC.md 11.94.1).
With every window's content origin on a multiple of 8, a glyph's screen x & 7 IS
its content-relative x & 7 - so a non-zero bucket is the APP's own offset and
not the window's position. Before the inversion the same histogram measured
where the user had dragged things.

    python3 tools/os88snap.py 127.0.0.1:9001 read
    python3 tools/os88snap.py 127.0.0.1:9001 reset

Sharing a session's connection is the normal use, because the debug server takes
ONE client and a second one hangs rather than failing:

    import os88snap
    os88snap.reset(m)
    ...drive the app...
    print(os88snap.report(m))
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import os88marty                                            # noqa: E402

TABLES = ("snap_hchar", "snap_hrun", "snap_kchar", "snap_krun")
BUCKETS = 8


class SnapError(Exception):
    pass


def _addr(m, name):
    """The table's FLAT address, or a sentence about the kernel not having it."""
    try:
        return m.sym(name)
    except Exception as exc:                                # noqa: BLE001
        raise SnapError(
            "%s is not in this kernel (%s).\n"
            "  The counters are behind a knob: build with `make SNAPAUDIT=1`.\n"
            "  A plain kernel deliberately carries none of them - they sit at "
            "the top of font_char,\n  which is the innermost drawing call in "
            "the system." % (name, exc))


def read(m):
    """{table: [count per x&7]} for both histograms."""
    out = {}
    for name in TABLES:
        raw = m.read(_addr(m, name), BUCKETS * 2)
        out[name] = list(struct.unpack("<%dH" % BUCKETS, raw))
    return out


def reset(m, win=0):
    """Zero all four histograms, and aim the filter at ONE window.

    `win` is a window-record pointer (an offset in KERNEL_SEG, as `wm_wins`
    hands them out); 0 means "every window's callback counts". With a filter
    set, a glyph drawn inside a DIFFERENT window's callback goes to the
    snap_k* tables - a repaint pass calls several windows and only one of them
    is the question.
    """
    for name in TABLES:
        m.write(_addr(m, name), b"\x00" * (BUCKETS * 2))
    m.write(_addr(m, "snap_win"), struct.pack("<H", win))
    # ...AND the caller log, which is the whole point of resetting: leave
    # snap_ncall alone and the log is whatever filled it up before the filter
    # was aimed, which reads exactly like a log OF the filtered window
    m.write(_addr(m, "snap_ncall"), b"\x00\x00")


def callers(m, defines=("SNAPAUDIT",)):
    """The off-grid caller log, each entry resolved to the routine that drew it.

    SPEC.md 11.94.2: a bucket count says an app IS off-grid, not WHERE. The
    kernel records (pen x, return address) for the first SNAP_NCALL off-grid
    glyphs inside the audited window; this maps each return address back to the
    nearest preceding symbol in the same section, which names the routine.

    RESOLVED IN KERNEL_SEG, and that is a fact about this kernel rather than a
    simplification: .cold code cannot near-call font_char at all, it goes
    through a far `cw_` shim (SPEC.md 2.6), so the return address font_char sees
    is always inside .text. Resolving against .cold as well picks a nearer
    symbol by accident and names a disk routine as the thing that drew a glyph.

    It therefore names the IMMEDIATE caller, which for a string is font_str_x's
    own loop rather than the application - one frame short of the routine you
    want. Use the pen x for that: an app's pens are its layout constants.
    """
    import os88sym
    tab = os88sym.syms(defines)
    sect = os88sym.sections(defines)
    n = struct.unpack("<H", m.read(_addr(m, "snap_ncall"), 2))[0]
    if not n:
        return []
    raw = m.read(_addr(m, "snap_calls"), n)
    ENT = 8
    # candidates per segment: (offset, name) sorted, so a bisect finds the
    # nearest symbol at or below a return address
    by_seg = {}
    for name, off in tab.items():
        seg = os88sym.SECTION_SEG.get(sect.get(name, ".text"), "KERNEL_SEG")
        by_seg.setdefault(seg, []).append((off, name))
    for v in by_seg.values():
        v.sort()
    out = []
    for i in range(0, n, ENT):
        pen, peny, ch, ret = struct.unpack_from("<HHHH", raw, i)
        best = None
        for seg in ("KERNEL_SEG",):
            cand = by_seg.get(seg, [])
            lo, hi = 0, len(cand)
            while lo < hi:                       # bisect_right on the offset
                mid = (lo + hi) // 2
                if cand[mid][0] <= ret: lo = mid + 1
                else: hi = mid
            if lo:
                off, name = cand[lo - 1]
                d = ret - off
                if best is None or d < best[0]:
                    best = (d, name, seg)
        glyph = chr(ch) if 32 <= ch < 127 else "."
        if best is None:
            out.append((pen, peny, glyph, ret, "?", "?", 0))
        else:
            out.append((pen, peny, glyph, ret, best[1], best[2], best[0]))
    return out


def summarise(counts):
    """(total, aligned, pct_aligned) for one bucket list."""
    total = sum(counts)
    if not total:
        return 0, 0, None
    return total, counts[0], 100.0 * counts[0] / total


def report(m, label=None):
    """A few lines naming what fraction of each caller's glyphs were aligned."""
    data = read(m)
    lines = []
    if label:
        lines.append(label)
    for name in TABLES:
        c = data[name]
        total, aligned, pct = summarise(c)
        if not total:
            lines.append("  %-11s nothing drawn" % name)
            continue
        # the buckets that actually fired, so a skew shows as a NUMBER rather
        # than as "not 100%" - which is the difference between "this app draws
        # at +6" and "this app is a bit off"
        spread = " ".join("%d:%d" % (i, n) for i, n in enumerate(c) if n)
        lines.append("  %-11s %5d drawn, %5d aligned (%5.1f%%)   [%s]"
                     % (name, total, aligned, pct, spread))
    return "\n".join(lines)


def main():
    if len(sys.argv) < 3 or sys.argv[2] not in ("read", "reset"):
        print(__doc__)
        return 2
    with os88marty.Marty(sys.argv[1]) as m:
        if sys.argv[2] == "reset":
            reset(m)
            print("snap_hchar/snap_hrun zeroed")
        else:
            print(report(m))
            try:
                cl = callers(m)
            except Exception as exc:                        # noqa: BLE001
                print("  (caller log unavailable: %s)" % exc); cl = []
            if cl:
                print("  off-grid callers (pen, return addr, routine):")
                for pen, peny, glyph, ret, name, seg, d in cl:
                    print("    x=%4d y=%4d &7=%d %r  %s+%d"
                          % (pen, peny, pen & 7, glyph, name, d))
    return 0


if __name__ == "__main__":
    sys.exit(main())
