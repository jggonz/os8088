#!/usr/bin/env python3
"""EVERY PIXEL OF THE TEXT BAND NAMES A ROW (SPEC.md 27.11.2).

    make worddisk && python3 tests/wdreach.py

`wd_penadv` gives a paragraph mark a whole 8px cell in the kernel's face, so
a row that exactly FILLS the measure used to wrap its own terminator onto a
row of its own — and a FLUSH RIGHT paragraph fills the measure by
construction, its alignment offset putting the pen at the right edge.
`WELCOME.DOC`'s *"This one is flush right (Ctrl-R)."* made a row out of one
invisible character, and that row was **unreachable**: a click anywhere in its
eight pixels resolved to the END of the document (47 rows walked, 2,123 ms on
a 5150), and Down off the row above it did not move the caret at all, because
a walk RESUMED there lays it out eight pixels lower than the row table says
and neither the hit query nor the want query ever matches.

  leg A  the SWEEP, because the defect is a GAP and a spot check walks past
         it: click every y from the flush-right row's band to the end of the
         row below it, and every single one must name a row. [wd_hitset] is
         the exact question - it is the byte the walk sets when a row claims
         the point, and the end-of-document answer is what a click gets when
         nothing does
  leg B  ...and Down off that line moves the caret, which is the same defect
         asked through the other query
  leg C  End on that line, then Down, lands on the SOFT-WRAPPED row below
         it and not at the left margin of the one after (SPEC.md 27.11.3):
         the want query ran past the wrapped row's end onto the next row's
         first index
  leg D  ...and that Down never reaches wd_redraw.full. FIELD-NOTES 57: the
         skip put the caret past the one-pass walk's bound, so the walk drew
         nothing and .p1bad repainted the window, chrome included
  leg E  the hit query's version of C: a click right of the wrapped row's end
         keeps the caret on that row

This is its own row rather than a leg of `wdclick` because it SCROLLS to find
the paragraph, and every cost leg in that file measures against the view it
was left in.
"""
import os, sys, subprocess, tempfile, argparse, functools
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tools"); sys.path.insert(0, "tests")
import os88marty as M
from os88mouse import Mouse
import dispcp

u16 = lambda b, i=0: b[i] | (b[i+1] << 8)
FAIL = []


def check(name, ok, detail=""):
    print("   %-52s %s%s" % (name, "ok" if ok else "FAIL", "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


def pkg_syms(src="apps/word/word.asm", incs=("apps/", "apps/word/")):
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"] + sum([["-I", i] for i in incs], [])
                       + ["-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for L in open(mp):
            f = L.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[1], 16)   # VIRTUAL: part 1 is assembled at WD_P1ORG
        return out, open(os.path.join(d, "p.bin"), "rb").read()


ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_5150_cga_gla")
a = ap.parse_args()
syms, image = pkg_syms()
DISK = "build/wdreachgate.img"
M.scratch_disk(DISK, "build/word.o88", "build/WELCOME.DOC")
S = lambda n: m.sym(n)

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    M.settle(m); mo = Mouse(marty=m)
    print("== Word: every pixel of the band names a row (SPEC.md 27.11.2) ==")
    dispcp.open_drive(m, mo, S, M.settle, "B")
    w = dispcp.win_list(m, S)[-1]; dx, dy = dispcp.win_rect(m, S, w)[:2]
    nwin = len(dispcp.win_list(m, S))
    dispcp.open_named(m, mo, S, M.settle, dx, dy, "WELCOME.DOC")
    try:
        M.until(m, lambda _: len(dispcp.win_list(m, S)) > nwin,
                "Word's window", poll=0.25, limit=60)
    except M.MartyError:
        pass                            # ...judged by the image hunt below
    M.settle(m)

    raw = m.read(S("inst_tab"), 32*12); seg = None
    for i in range(12):
        b = i*32
        if raw[b] == 1 and (raw[b+2] & 0x80):
            c = u16(raw, b+6)
            if m.read(c*16+syms["wd_mact"], 48) == image[syms["wd_mact"]:syms["wd_mact"]+48]:
                seg = c; break
    if seg is None:
        sys.exit("could not locate the running package (stale build/word.o88?)")
    base = seg*16; P = lambda n: base + syms[n]
    rw = lambda n: u16(m.read(P(n), 2)); rb = lambda n: m.read(P(n), 1)[0]
    caret = lambda: (rw("wd_cur"), rw("wd_currow"), rw("wd_top"))

    def settle_height():
        for _ in range(400):
            if rb("wd_hdirty") == 0:
                return
            m.advance(cycles=2_000_000)

    settle_height()
    tx, ty, vrows = rw("wd_tx"), rw("wd_ty"), rw("wd_vrows")
    bot, sbr, sbb = rw("wd_bot"), rw("wd_sbr"), rw("wd_sbb")
    sbx = sbr - 7
    ydn = (ty+sbb)//2 + (sbb-ty)//4
    ryb = lambda r: u16(m.read(P("wd_ryb") + 2*r, 2))
    rows = lambda r: u16(m.read(P("wd_rows") + 2*r, 2))
    dlen = rw("wd_len")
    note = m.read(rw("wd_dseg") * 16, dlen)
    print("   vrows=%d len=%d pxon=%d hasfmt=%d"
          % (vrows, dlen, rb("wd_pxon"), rb("wd_hasfmt")))

    mark = note.find(b"flush right")
    check("the note still has the flush-right line (case, not assertion)",
          mark >= 0, "no 'flush right' in WELCOME.DOC")

    def page_down():
        m.run()                         # settle_height may have left it paused
        mo.to(sbx, ydn); M.pace(m, 0.25)
        m.mouse(l=True); M.pace(m, 0.08); m.mouse(l=False)
        M.quiesce(m, lambda: (rw("wd_top"), rb("wd_hdirty")))
        mo.to(4, 4); M.settle(m)

    FR = None
    for _ in range(40):
        if mark < 0:
            break
        settle_height()
        n = min(rw("wd_rowsn"), vrows)
        st = [rows(r) for r in range(n)]
        for r in range(n - 1):
            if st[r] <= mark < st[r+1] and ryb(r) + 7 <= bot:
                FR = r; break
        if FR is not None:
            break
        page_down()
    check("...and it is on screen (case, not assertion)", FR is not None,
          "could not bring the flush-right row into view")

    if FR is not None:
        print("   flush-right row %d at y=%d, top=%d" % (FR, ryb(FR), rw("wd_top")))
        y0 = ryb(FR)
        y1 = min(ryb(FR + 1) + 7 if FR + 1 < vrows else y0 + 15, bot)
        # THE CARET'S INDEX, not [wd_hitset]: the flag is set PART WAY
        # THROUGH the walk, so reading it on a fixed sleep is a race that a
        # loaded box loses - scattered single ys, in no geometric pattern,
        # which is not the shape a GAP has. [wd_cur] is the settled answer
        # and is the user-visible one: a y that names no row sends the caret
        # to the end of the document.
        dead = []
        for y in range(y0, y1 + 1):
            m.run(); mo.to(tx + 8, y); M.pace(m, 0.25)
            m.mouse(l=True); M.pace(m, 0.08); m.mouse(l=False)
            M.quiesce(m, lambda: (rw("wd_cur"), rb("wd_hitset")))
            if rw("wd_cur") >= dlen:
                dead.append(y)
        check("A: every y of the flush-right row and the next names a row",
              not dead,
              "y=%s send the caret to the END of the document (%d), so they "
              "name no row at all" % (dead, dlen))
        print("      swept y %d..%d" % (y0, y1))

        m.run(); mo.to(tx + 8, ryb(FR) + 2); M.pace(m, 0.3)
        m.mouse(l=True); M.pace(m, 0.08); m.mouse(l=False)
        M.quiesce(m, lambda: (rw("wd_cur"), rb("wd_hitset")))
        M.settle(m)
        was = rw("wd_cur")
        check("the caret landed on the flush-right row (case, not assertion)",
              rows(FR) <= was < rows(FR + 1) if FR + 1 < vrows else True,
              "[wd_cur] = %d, row %d is %d.." % (was, FR, rows(FR)))
        m.key("ArrowDown"); M.quiesce(m, caret); M.settle(m)
        check("B: Down off a flush-right line moves the caret",
              rw("wd_cur") != was, "[wd_cur] stayed at %d" % was)
        print("      Down: %d -> %d" % (was, rw("wd_cur")))

        # C/D/E: the row BELOW the flush-right one is soft-wrapped, and the
        # flush-right line ends further right than it does - so Down from its
        # END aims past the wrapped row's last character. SPEC.md 27.11.3.
        m.key("ArrowUp"); M.quiesce(m, caret); M.settle(m)
        m.key("End"); M.quiesce(m, caret); M.settle(m)
        top0, row0 = rw("wd_top"), rw("wd_currow")
        full = syms["wd_redraw.full"]
        hits = []
        with M.bp_trace(m, base + full, on_hit=lambda mm, rec: hits.append(1), cap=100):
            m.key("ArrowDown"); M.pace(m, 1.6)
        M.settle(m)
        cur, crow, top1 = rw("wd_cur"), rw("wd_currow"), rw("wd_top")
        absrow = lambda r, t: (r - 0x10000 if r & 0x8000 else r) + t
        nxt = rows(crow + 1) if 0 <= crow + 1 < vrows else None
        check("C: Down from a line's END lands on the wrapped row below it",
              absrow(crow, top1) == absrow(row0, top0) + 1,
              "caret went from absolute row %d to %d (cur=%d) - it skipped "
              "the wrapped row and landed at the START of the one after"
              % (absrow(row0, top0), absrow(crow, top1), cur))
        check("D: ...without repainting the whole window (FIELD-NOTES 57)",
              not hits, "wd_redraw.full reached %d time(s)" % len(hits))
        print("      End+Down: row %d -> %d, cur=%d, next row starts %s"
              % (absrow(row0, top0), absrow(crow, top1), cur, nxt))

        # E: the same query through the POINT: a click right of the wrapped
        # row's end names that row's end, not the next row's first index
        wr = absrow(row0, top0) + 1 - top1  # the WRAPPED row, wherever the
        nxt = rows(wr + 1) if 0 <= wr + 1 < vrows else None  # caret went
        mo.to(sbx - 12, ryb(wr) + 2); M.pace(m, 0.3)
        m.mouse(l=True); M.pace(m, 0.08); m.mouse(l=False)
        M.quiesce(m, lambda: (rw("wd_cur"), rb("wd_hitset")))
        M.settle(m)
        cur2, crow2 = rw("wd_cur"), rw("wd_currow")
        check("E: a click right of a wrapped row keeps the caret on it",
              crow2 == wr and (nxt is None or cur2 < nxt),
              "clicked row %d at y=%d, caret went to row %d, cur=%d (next "
              "row starts %s)" % (wr, ryb(wr) + 2, crow2, cur2, nxt))

print()
if FAIL:
    print("FAILED: " + ", ".join(FAIL)); sys.exit(1)
print("ok"); sys.exit(0)
