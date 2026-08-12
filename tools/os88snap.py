#!/usr/bin/env python3
"""Read the SPEC.md 11.94.2 glyph-alignment histograms out of a running os8088.

`make SNAPAUDIT=1` compiles two 8-word tables into the kernel - snap_hchar and
snap_hrun - counting the `x & 7` of every glyph font_char and font_run are asked
to draw. This reads them, and resets them, so a host script can attribute the
counts to ONE app: reset, drive the app, read.

KNOWN ARTIFACT, and it is why this docstring says so before it says anything
else: every window's callback reports a CONSTANT 4 glyphs in bucket 7 per forced
repaint, whatever the app, and it has not been chased down. A count under about
ten therefore says nothing at all. Chase that before trusting small numbers out
of this, and delete this paragraph when it is gone.

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
    return 0


if __name__ == "__main__":
    sys.exit(main())
