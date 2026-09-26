#!/usr/bin/env python3
"""A DRAG THAT AUTO-SCROLLS SELECTS WHAT IT SCROLLS PAST (SPEC.md 27.8.2.4).

    make worddisk && python3 tests/wddrag.py

FIELD-NOTES 59: *"the lines at the bottom that have been scrolled already
through dragging are incorrectly not selected."* The selection was never wrong:
[wd_sel0]..[wd_sel1] covered every row. What was wrong was the GLASS and the
row table, three ways:

  - wd_redraw reaches .scrolled0 when the view moved before it ran, and that
    arm said nothing was dirty - but a drag step moved the caret END of the
    selection, so the rows between the old end and the new one owed an
    inversion. The blit carried them across upright and nothing drew them
    again: one row per step, every row the drag scrolled past.
  - wd_hitpt seeded the pointer's row from wd_rows, which wd_scrollto keeps
    describing the view BEFORE the scroll, so each step resolved one row high.
  - ...and its measure walk BANKED wd_rows in between, so after a drag step
    the table named a row the glass did not show - one row behind on the tree
    this fixed, one ahead with only the second fix in.

  leg A  at each drag step, stopped at the loop's head so the redraw is
         finished: every visible row WHOLLY inside the selection is inverted
         on the glass (more than half its glyph band dark). Backed out, the
         rows the scroll brought in read ~10%
  leg B  ...and the row table names the row the glass shows: wd_rows[0] is
         the first index of absolute row [wd_top], read off the layout taken
         before the drag. Backed out it is exactly one row off
  leg E  Down through the whole note: none is a full repaint (SPEC.md
         27.4.12) - after a one-row scroll the next Down's walk read the stale
         tables past the glass as rows that moved: 1.5-2.3 s
  leg D  a click in the paper BELOW the note's last line: the caret goes to
         the end and it takes no walk (SPEC.md 27.7.14) - it walked the whole
         note from index 0, 556-857 ms on a 5150
  leg C  FIELD-NOTES 59.1's runaway, which PAGE DOWN reaches too: at the end
         of the note [wd_drows] stays the height counted before the drag and
         the view stops at its clamp. A walk seeded on one of the blank rows
         banked below the end called the note 48 rows of 36 (SPEC.md 27.7.11)
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
    print("   %-56s %s%s" % (name, "ok" if ok else "FAIL", "" if ok else "  " + detail))
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
ap.add_argument("--machine", default="os8088_5150_herc_gla")
a = ap.parse_args()
syms, image = pkg_syms()
DISK = "build/wddraggate.img"
M.scratch_disk(DISK, "build/word.o88", "build/WELCOME.DOC")

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    M.settle(m); mo = Mouse(marty=m); S = lambda n: m.sym(n)
    print("== Word: a drag that auto-scrolls (SPEC.md 27.8.2.4) ==")
    dispcp.open_drive(m, mo, S, M.settle, "B")
    w = dispcp.win_list(m, S)[-1]; dx, dy = dispcp.win_rect(m, S, w)[:2]
    dispcp.open_named(m, mo, S, M.settle, dx, dy, "WELCOME.DOC")
    # The window is up; what is left is the document read, which ends when
    # the floppy goes quiet.
    M.quiesce(m, lambda: m.disk().get("reads"), guest=1.0,
              what="Word to finish reading WELCOME.DOC")

    def find_seg():
        raw = m.read(S("inst_tab"), 32*12)
        for i in range(12):
            b = i*32
            if raw[b] == 1 and (raw[b+2] & 0x80):
                c = u16(raw, b+6)
                if m.read(c*16+syms["wd_mact"], 48) == image[syms["wd_mact"]:syms["wd_mact"]+48]:
                    return c
        return None
    try:
        M.until(m, lambda _: find_seg() is not None, "Word's instance record",
                poll=0.1, limit=10)
    except M.MartyError:
        pass
    seg = find_seg()
    if seg is None:
        sys.exit("could not locate the running package (stale build/word.o88?)")
    base = seg*16; P = lambda n: base + syms[n]
    rw = lambda n: u16(m.read(P(n), 2)); rb = lambda n: m.read(P(n), 1)[0]

    # ---- waiting on the guest, not on a clock (tests/wdtype.py's shape) ----
    # A gesture is finished when its input is out of both queues (the BIOS
    # keyboard ring and the kernel's event ring) and nobody holds the gfx
    # lock, which ui_task takes around every handler it dispatches - twice, a
    # twentieth of a guest second apart, because ui_task pops an event a few
    # instructions before it takes the lock for it.
    KBUF = 0x41A                        # 0040:001A/001C - the BIOS ring's head, tail
    evtail = lambda: m.read(S("evq_tail"), 1)[0]
    cyc = lambda: int(m.status()["cycles"])

    def ui_idle():
        kb = m.read(KBUF, 4)
        return (kb[0:2] == kb[2:4] and m.read(S("evq_count"), 1)[0] == 0
                and m.read(S("gfx_lock_flag"), 1)[0] == 0)

    def done(arrived=None, what="the UI to finish with the gesture", idle=True):
        fin = ui_idle if idle else (lambda: m.read(S("evq_count"), 1)[0] == 0)
        M.until(m, lambda _: (arrived is None or arrived()) and fin(), what,
                poll=0.05, limit=30)
        c0 = cyc()
        M.until(m, lambda _: cyc() - c0 >= M.GUEST_HZ / 20 and fin(), what,
                poll=0.05, limit=30)

    def key(k):
        h = m.read(KBUF, 2); m.key(k)
        done(lambda: m.read(KBUF, 2) != h, "the %s key to be handled" % k)

    def click(x, y):
        t = evtail(); mo.click(x, y, settle=0)
        done(lambda: evtail() != t, "the click at (%d,%d) to be handled" % (x, y))

    for _ in range(600):
        if rb("wd_hdirty") == 0:
            break
        m.advance(cycles=2_000_000)
    M.settle(m)

    tx, ty, bot, vrows = rw("wd_tx"), rw("wd_ty"), rw("wd_bot"), rw("wd_vrows")
    rgt, gh = rw("wd_rgt"), rw("wd_gh")
    rows = lambda r: u16(m.read(P("wd_rows") + 2*r, 2))
    ryb = lambda r: u16(m.read(P("wd_ryb") + 2*r, 2))
    check("the view starts at the top (case, not assertion)", rw("wd_top") == 0)
    true0 = [rows(r) for r in range(vrows)]       # absolute rows 0..vrows-1
    drows0 = rw("wd_drows")

    # press on row 1 and park the pointer just below the text band, which is
    # where a hand parks it to make the view run
    mo.to(tx + 40, ryb(1) + 2)
    t = evtail(); mo._edge(True)
    done(lambda: evtail() != t, "the press to be dispatched", idle=False)
    mo.to(tx + 200, bot + 3, l=True)
    # ...and let the view RUN. Not idle time - the guest is auto-scrolling
    # throughout - and a FIXED amount on purpose: where it leaves the view is
    # where every later leg starts (sampled from its first step instead, the
    # drag stops at top 6, PageDown then ends at 23 and leg E reads a 2 s
    # Down at top 1 - a different scenario, not this row's).
    M.guest_sleep(m, 4.5)

    def band_ink(y, cells):
        # DARK pixels over the row's own CELL AREA, wherever alignment put
        # them: an inverted cell is ~75% dark and an upright glyph ~25%, and
        # measuring against the whole band would read a short inverted row
        # as upright
        w2, h, rr = m.vram(None)
        dark = sum(1 for yy in range(y, y + gh) for x in range(tx, rgt + 1)
                   if not rr[yy][x])
        return dark / float(gh * 8 * cells)

    stops, unsel, offby = 0, [], []
    m.bp_exec(base + syms["wd_dragsel.pass"])
    for k in range(6):
        m.wait_stop(limit=30)
        top, s0, s1 = rw("wd_top"), rw("wd_sel0"), rw("wd_sel1")
        if rb("wd_rowsok") and top < len(true0) and rows(0) != true0[top]:
            offby.append((top, rows(0), true0[top]))
        n = min(rw("wd_rowsn"), vrows)
        for r in range(n - 1):
            a0, a1 = rows(r), rows(r + 1)
            y = ryb(r)
            if not (ty <= y and y + gh - 1 <= bot):
                continue
            if a1 - a0 < 4 or a0 < s0 or a1 > s1:
                continue                         # short, or not wholly selected
            f = band_ink(y, a1 - a0 - 1)   # less the paragraph mark or wrap space
            if f < 0.5:
                unsel.append((top, r, a0, round(f, 2)))
        stops += 1
        m.run(); M.pace(m, 0.05)
    m.bp_exec(); m.run()
    mo.to(tx + 200, bot - 40)
    mo._edge(False)                     # the drag loop reads the button
    done(what="the drag's release to be handled")   # itself: no event

    check("the drag scrolled through six steps (case, not assertion)", stops == 6)
    check("A: every row wholly inside the selection is inverted", not unsel,
          "%d row(s) upright on the glass, e.g. top=%d row %d (index %d) %.0f%% dark"
          % ((len(unsel),) + (unsel[0][:3]) + (100*unsel[0][3],)) if unsel else "")
    check("B: the row table names the row the glass shows", not offby,
          "; ".join("top %d: rows[0]=%d, absolute row %d starts %d" % (t, got, t, want)
                    for t, got, want in offby[:3]))

    # --- leg C: PageDown stops at the end (SPEC.md 27.7.11) ------------------
    # The blank rows below the note's end are banked in wd_rows, each starting
    # at [wd_len]; a scroll that seeded on one ended its walk at once and .done
    # called the note that many rows taller. At the clamp that is 36 -> 48,
    # and every page after it raised the ceiling by the page it moved.
    M.settle(m)
    tops = []
    for _ in range(drows0 + 4):
        t0 = rw("wd_top")
        m.key("PageDown")
        M.quiesce(m, lambda: (rw("wd_top"), rw("wd_drows"), rw("wd_cur")))
        tops.append((rw("wd_top"), rw("wd_drows")))
        if rw("wd_top") == t0:
            break
    top, dr = rw("wd_top"), rw("wd_drows")
    check("C: PageDown stops at the end of the note",
          dr == drows0 and top <= max(0, drows0 - rw("wd_vfit")),
          "[wd_drows] %d (was %d), [wd_top] %d - the pages went %s"
          % (dr, drows0, top, tops[-6:]))
    print("      PageDown ended at top %d of %d rows" % (top, dr))

    # --- leg E: no Down is a full repaint (SPEC.md 27.4.12) -------------------
    # A Down that scrolls one row runs scroll paint with the glass stop
    # (27.7.12), which leaves the tables past the glass stale; the next Down's
    # one-pass walk entered the row past its bound, read it as a row that
    # MOVED or a range that outran the drawing, and repainted the window:
    # 1.5-2.3 s where its neighbours took 0.3.
    click(tx + 8, ryb(0) + 2)
    for _ in range(12):
        if rw("wd_top") == 0:
            break
        m.key("PageUp"); M.quiesce(m, lambda: (rw("wd_top"), rw("wd_cur")))
    click(tx + 8, ryb(0) + 2)
    mo.to(4, 4); M.settle(m)
    # ...and the PAGE UP that got here must leave the table describing every
    # row on the glass (SPEC.md 27.7.2.3): the rows below the band it lettered
    # were blitted down with their entries, and the walk's stop cut
    # [wd_rowsn] to the band - 14 of 25 - so every Down from row 14 on could
    # not seed and paid 0.9-1.25 s. Stated as the table, because the timing
    # below only catches it when the rows the loop happens to land on are
    # the ones past the cut.
    glass = 0
    while glass < vrows and ryb(glass) + gh - 1 <= bot:
        glass += 1
    check("E: after PageUp to the top the table covers the glass",
          rb("wd_rowsok") and rw("wd_rowsn") >= glass,
          "[wd_rowsn] = %d, the glass shows %d rows" % (rw("wd_rowsn"), glass))
    ENT, XIT = P("wd_onkey"), P("wd_onkey.out")
    worst = (0, 0)
    for _ in range(rw("wd_drows") + 2):
        c0 = rw("wd_cur")
        T = {}
        def stampk(mm, rec, T=T):
            a = rec.get("name")
            cyc = int(mm.status().get("cycles", 0))
            if a in (ENT, "%X" % ENT, str(ENT)):
                T.setdefault("in", cyc)
            else:
                T["out"] = cyc
            return None
        with M.bp_trace(m, ENT, XIT, on_hit=stampk, cap=16):
            m.key("ArrowDown")
            M.quiesce(m, lambda: (rw("wd_cur"), rw("wd_top")))
            done(lambda: "out" in T, "the Down's handler to return")
        if "in" in T and "out" in T:
            ms = (T["out"] - T["in"]) / 4772.7
            if ms > worst[0]:
                worst = (ms, rw("wd_top"))
        if rw("wd_cur") == c0:
            break
    check("E: no Down through the note is a full repaint (all under 900 ms)",
          0 < worst[0] < 900, "the worst took %.0f ms, arriving at top %d" % worst)
    print("      the slowest Down: %.0f ms at top %d" % worst)

    # --- leg D: a click BELOW the note's end costs no walk (SPEC.md 27.7.14) --
    # Make the note shorter than the window, then click in the paper under
    # its last line. The row lookup found a blank row banked past the end, or
    # nothing, and both fell to the walk from index 0: 556-857 ms on a 5150.
    click(tx + 4, ryb(3) + 2)
    key("F8")
    for _ in range(rw("wd_drows") + 4):
        c0 = rw("wd_cur")
        m.key("ArrowDown")
        M.quiesce(m, lambda: (rw("wd_cur"), rw("wd_top")))
        if rw("wd_cur") == c0:
            break
    key("End")
    key("Delete")
    M.quiesce(m, lambda: (rw("wd_len"), rw("wd_top"), rw("wd_drows")))
    for _ in range(600):
        if rb("wd_hdirty") == 0:
            break
        m.advance(cycles=2_000_000)
    M.settle(m)
    last = rw("wd_drows") - 1 - rw("wd_top")
    check("the note is shorter than the window (case, not assertion)",
          0 <= last < vrows and ryb(last) + gh + 16 < bot,
          "last row %d of a %d-row view" % (last, vrows))
    for y in (bot - 30, bot - 6):
        T = {}
        def stamp(mm, rec, T=T):
            a = rec.get("name")
            cyc = int(mm.status().get("cycles", 0))
            if a in (P("wd_onclick"), "%X" % P("wd_onclick"), str(P("wd_onclick"))):
                T.setdefault("in", cyc)
            elif "in" in T:
                T.setdefault("out", cyc)
            return None
        mo.to(tx + 60, y)
        with M.bp_trace(m, P("wd_onclick"), P("wd_dragsel.pass"), on_hit=stamp, cap=50):
            click(tx + 60, y)
        ms = (T["out"] - T["in"]) / 4772.7 if "in" in T and "out" in T else None
        check("D: a click %dpx above the band's foot lands at the end" % (bot - y),
              rw("wd_cur") == rw("wd_len"), "[wd_cur] %d of %d" % (rw("wd_cur"), rw("wd_len")))
        check("D: ...and is resolved and redrawn in under 200 ms",
              ms is not None and ms < 200, "%s ms on a 5150" % (None if ms is None else round(ms)))
        print("      click at y=%d: %s ms" % (y, None if ms is None else round(ms)))

print()
if FAIL:
    print("FAILED: " + ", ".join(FAIL)); sys.exit(1)
print("ok"); sys.exit(0)
