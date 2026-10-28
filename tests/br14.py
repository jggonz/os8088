#!/usr/bin/env python3
"""Browser 1.4's guest-side colspan, alignment and unsupported-control gate.

    make browsertest && python3 tests/br14.py [--adapter cga|herc]
"""
import argparse
import os
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, "tools"))
sys.path.insert(0, os.path.join(_ROOT, "tests"))
import dispcp
import os88marty
import os88mouse
import os88sym
from brclick import browser_syms, MACHINE

S = os88sym.linear


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    args = ap.parse_args()
    fails = []
    sy = browser_syms()
    with os88marty.launch("build/os8088-360.img", apps="build/brtest360.img",
                          machine=MACHINE[args.adapter], boot=False) as m:
        m.run()
        os88marty.kernel_up(m)          # the boot gate on state; every wait
        os88marty.no_saver(m)           # after is ui_done - the reads are
                                        # browser state and video memory
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, lambda mm, **k: os88marty.ui_done(mm), "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        before = len(wins)
        dispcp.open_named(m, mo, S, lambda mm, **k: os88marty.ui_done(mm), wx, wy, "TORTURE.HTM")
        wins = dispcp.win_list(m, S)
        if len(wins) <= before:
            sys.exit("br14: TORTURE.HTM did not open")
        rec = m.read(S("wm_wins") + wins[-1] * dispcp.WIN_SIZE,
                     dispcp.WIN_SIZE)
        seg = rec[22] | (rec[23] << 8)
        rb = lambda n: m.readseg(seg, sy[n], 1)[0]
        rw = lambda n: m.readseg(seg, sy[n], 2)[0] | (m.readseg(seg, sy[n], 2)[1] << 8)
        os88marty.until(m, lambda _m: rw("br_nlines") > 0,
                        "the torture page parsed", limit=120)
        os88marty.ui_done(m)
        docseg, doclen = rw("br_docseg"), rw("br_doclen")
        comp = rw("br_comp")
        doc = bytes(m.readseg(docseg, 0, max(doclen, comp) + 1))
        table_meta = [doc[i + 2] for i in range(doclen - 2)
                      if doc[i] == 13 and doc[i + 1] == 24]
        for value, label in ((8, "center/middle"), (16, "right"),
                             (1, "colspan=2"), (17, "colspan=2 right")):
            if value not in table_meta:
                fails.append("missing packed cell metadata for " + label)

        # Browser 1.6 widens document offsets to dwords and adds a byte to
        # each line record. Keep this renderer regression independent of the
        # current document's position in a conventional/EMS arena.
        tab = bytes(m.readseg(rw("br_lseg"), 0, rw("br_nlines") * 10))
        rendered = []
        for i in range(rw("br_nlines")):
            o = int.from_bytes(tab[i * 10:i * 10 + 4], "little")
            e = int.from_bytes(tab[i * 10 + 4:i * 10 + 8], "little")
            line = []
            j = o
            while j < e:
                b = doc[j]
                if b == 14:
                    j += 4
                    continue
                j += 1
                if b >= 0x20:
                    line.append(" " if b == 0x7f else chr(b))
            rendered.append("".join(line))
        malformed_next = next((i for i, line in enumerate(rendered)
                              if "9. Preformatted" in line), None)
        if malformed_next is None:
            fails.append("unclosed inline emphasis leaked into the next heading")
        else:
            # Headings intentionally use the same bold bit as inline <b>.
            # Check that the heading is bold, then that its following PRE
            # content is not: the latter catches malformed inline state leaking
            # past the block while allowing the heading's own style.
            if not tab[malformed_next * 10 + 9] & 0x20:
                fails.append("the next heading did not use bold styling")
            pre_start = next((i for i, line in enumerate(rendered)
                              if "column1" in line), None)
            if pre_start is None or tab[pre_start * 10 + 9] & 0x20:
                fails.append("unclosed inline emphasis leaked into following PRE text")
        aligned = next((line for line in rendered
                        if "aligned" in line and "center" in line and "right" in line), "")
        if not aligned:
            fails.append("the aligned table row was not composed")
        else:
            # br_tcompose opens EVERY cell with one separator space, whatever
            # its alignment, so strip exactly that one before measuring: with
            # it counted, "right" always had a leading space and the check was
            # a null. The fixture's wide-middle-cell / right-column row is what
            # gives columns 2 and 3 spare cells to align in - "center" and
            # "right edge" used to be their own columns' widest cells
            cells = aligned.split("|")
            center = cells[2][1:] if len(cells) == 5 else ""
            left = len(center) - len(center.lstrip())
            right = len(center) - len(center.rstrip())
            if (not center.strip().startswith("center") or left == 0 or
                    abs(left - right) > 1):
                fails.append("middle cell was not centered: %r" % aligned)
            rcell = cells[3][1:] if len(cells) == 5 else ""
            if (len(cells) != 5 or not rcell.lstrip().startswith("right") or
                    rcell == rcell.lstrip() or rcell != rcell.rstrip()):
                fails.append("right cell was not right-aligned: %r" % aligned)
        span = next((line for line in rendered
                     if "two columns" in line and "right edge" in line), "")
        if not span or len(span.split("|")) != 4:
            fails.append("colspan row did not compose two cells: %r" % span)
        else:
            edge = span.split("|")[2][1:]       # flush right: led by its spare,
            if edge == edge.lstrip() or edge != edge.rstrip():   # no trailing
                fails.append("colspan row's right cell was not right-aligned: "
                             "%r" % span)
        if not any("[unsupported]" in line for line in rendered):
            fails.append("password/file/textarea controls were not visible")
        if any("secret" in line or "private text" in line for line in rendered):
            fails.append("unsupported control content leaked onto the page")
        entityline = next((line for line in rendered
                           if "Latin-1 set:" in line), "")
        if "Latin-1 set: a c e A Y 1" not in entityline:
            fails.append("classic Latin-1 entities were not folded as expected: "
                         "%r" % entityline)
        pre_start = next((i for i, line in enumerate(rendered)
                          if "column1" in line), None)
        pre_rows = [pre_start]
        for token in ("-------", "a         b"):
            pre_rows.append(next((i for i, line in enumerate(rendered)
                                  if pre_start is not None and i > pre_start
                                  and token in line), None))
        if (None in pre_rows or pre_rows[1] != pre_rows[0] + 1
                or pre_rows[2] != pre_rows[1] + 1):
            fails.append("CRLF inside PRE did not produce one break per line: "
                         "%r" % pre_rows)
        preline = next((line for line in rendered
                        if "leading   spaces   preserved" in line), "")
        indent = len(preline) - len(preline.lstrip())
        if not preline or indent < 7:
            fails.append("<pre> leading spaces were collapsed: %r" % preline)

    for fail in fails:
        print("FAIL:", fail)
    print("br14: %d assertion(s) failed" % len(fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
