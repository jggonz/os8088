#!/usr/bin/env python3
"""A SEEDED WALK MUST NOT EAT THE ROW ABOVE IT (SPEC.md 68.6.2).

    make worddisk && python3 tests/wdcourier.py

The field: *"when in courier, sometimes selecting a line - via click, or
arrow - will erase half of the line above it."* Both of those gestures SEED.
SPEC.md 27.4.10's checkpoint lets the walk start at a known row instead of
walking down to it, and the seed then has to reconstruct that row's glyph y
and its BAND TOP by hand. That reconstruction was `wd_advy`'s arithmetic
written a second time with the kernel's cell height as a LITERAL 8, so in a
chosen face it came out `[wd_gh] - 8` too high - and SPEC.md 68.6's
leading-gap fill, which is FULL WIDTH, then ran from inside the row above and
took the bottom of its glyphs with it.

In the kernel's 8x8 face 8 IS `[wd_gh]`, which is why the defect belongs to a
chosen face alone and why every Pica row in the suite is silent about it.

  leg A  the invariant `wd_ryb`'s own declaration publishes - the band top of
         row r is `ryb[r-1] + [wd_gh]` - read at every FLUSH during a click
         that seeds. This is the assertion: backed out it reads **-4 on every
         flushed row**, against a face whose gh is 12, which is gh - 8 to the
         pixel
  leg B  ...and a per-row ink RATCHET over the same gesture: no visible row
         may hold less ink than it held before. It is a cross-check and it is
         honestly weaker - **it did not go red on this defect**, because the
         four rows the fill eats are a row's DESCENDERS and the row above the
         one this lands on had none. It is kept because it is the field's own
         sentence and costs two screen reads

  legs E-H  the LAST line (FIELD-NOTES 60). E: the flush-right paragraph is
         ONE row - wd_rowmeasure measured every character as a space, so it
         wrapped and left an EMPTY row (SPEC.md 68.13.2). F: Down walks from
         the top to the note's last row, which it could not past that empty
         row. G: every row starts [wd_gh] below the row above it, blank rows
         included. H: the last line keeps ink below its eighth pixel row -
         a blank row stepped 8 and its erase cut it (SPEC.md 68.6.2.1)
  leg I  FIELD-NOTES 58: PageDown to the end, then the scroll bar's own
         record must put the thumb at the bar's end - its page was the 8px
         [wd_vrows] while the view's clamp used [wd_vfit] (SPEC.md 68.6.3)

The tolerance on leg B is ONE CARET BAR and not zero: the bar is `[wd_gh]`
pixels of a single column, and the row the caret leaves gives exactly that
many back.
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


# the ribbon's Font combo and the dropdown record, as tests/wdcombo.py reads them
WD_MENU_H = 14
WD_RB_FBX, WD_RB_FBW = 56, 96
DR_ITEMS, DR_N, DR_SEL, DR_OPEN, DR_HOT, DR_SEG, DR_TOP = 8, 10, 12, 16, 17, 18, 22
OS88UI_DRIH = 10

ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_5150_herc_gla")
a = ap.parse_args()
syms, image = pkg_syms()
DISK = "build/wdcourier.img"
M.scratch_disk(DISK, "build/word.o88", "build/WELCOME.DOC")

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    M.settle(m); mo = Mouse(marty=m); S = lambda n: m.sym(n)
    print("== Word: a seeded walk and the row above it (SPEC.md 68.6.2) ==")
    dispcp.open_drive(m, mo, S, M.settle, "B")
    w = dispcp.win_list(m, S)[-1]; dx, dy = dispcp.win_rect(m, S, w)[:2]
    dispcp.open_named(m, mo, S, M.settle, dx, dy, "WELCOME.DOC")
    M.ui_done(m, "Word to open WELCOME.DOC"); M.settle(m)

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
    for _ in range(400):
        if rb("wd_hdirty") == 0:
            break
        m.advance(cycles=2_000_000)
    M.settle(m)

    def waits(cond, what, limit=40.0):
        M.until(m, lambda mm: cond(), what, poll=0.15, limit=limit)

    # --- Courier, off the machine's own SYSTEM/FONTS ------------------------
    Rf = base + syms["wd_dfont"]
    nfont = rb("wd_nfont")
    cl, ct = rw("wd_cl"), rw("wd_ct")
    fopen = lambda: m.read(Rf + DR_OPEN, 1)[0]
    # wd_fontscan walks SYSTEM/FONTS the FIRST time the combo comes down, so
    # [wd_nfont] is 0 until then: open it once, then ask.
    bx, boxtop = cl + WD_RB_FBX + WD_RB_FBW // 2, ct + WD_MENU_H + 2
    mo.to(bx, boxtop + 6); mo._edge(True)
    waits(lambda: fopen() == 1, "the Font list to come down")
    nfont = rb("wd_nfont")
    if nfont < 1:
        mo._edge(False); M.pace(m, 1.0)
        print("   this disk carries no faces - nothing to test")
        sys.exit(0)
    top = u16(m.read(Rf + DR_TOP, 2))
    mo.to(bx, top + OS88UI_DRIH + 5, l=True)        # item 1: the first face
    waits(lambda: m.read(Rf + DR_HOT, 1)[0] == 1, "the pointer to land on it")
    drows_before = rw("wd_drows")
    nmark = [0]
    def marked(mm, rec):
        nmark[0] += 1
        return None
    # THE DROPDOWN CLOSES BEFORE THE ACTION RUNS - wd_mclose clears DR_OPEN
    # and only then does wd_mfire call wd_a_csel - so the trace has to stay
    # open past the pick's own redraw, not just past the menu.
    with M.bp_trace(m, base + syms["wd_hmark"], on_hit=marked, cap=64):
        mo._edge(False)
        waits(lambda: fopen() == 0, "the face pick to complete")
        waits(lambda: rb("wd_prop") != 0, "ty_openfam to take the face")
        M.settle(m)
    mo.to(4, 4); M.settle(m)
    for _ in range(600):                            # let the height worker
        if rb("wd_hdirty") == 0:                    # finish the new count
            break
        m.advance(cycles=2_000_000)
    M.settle(m)

    gh, ghb, prop = rw("wd_gh"), rw("wd_ghb"), rb("wd_prop")
    check("a chosen face is in (case, not assertion)", prop != 0,
          "[wd_prop]=0 - ty_openfam refused, so this is still the 8x8 cell")
    print("   face: [wd_prop]=%d [wd_gh]=%d [wd_ghb]=%d vrows=%d"
          % (prop, gh, ghb, rw("wd_vrows")))
    if prop == 0:
        print("\nFAILED: no face"); sys.exit(1)

    vrows, tx, rgt = rw("wd_vrows"), rw("wd_tx"), rw("wd_rgt")
    ryb = lambda r: u16(m.read(P("wd_ryb") + 2*r, 2))

    def shot():
        if m.cards()[0]["type"] in ("cga", "mda"):
            w2, h, rr = m.vram(); return w2, bytes(b for r in rr for b in r)
        w2, h, px = m.fbuf()
        return w2, bytes(1 if px[i] or px[i+1] or px[i+2] else 0
                         for i in range(0, len(px), 3))

    def profile():
        """Ink per VISIBLE ROW, over that row's own glyph band."""
        w2, px = shot()
        out = []
        for r in range(vrows):
            y = ryb(r)
            if y < rw("wd_ty") or y + gh - 1 > rw("wd_bot"):
                out.append(None); continue
            out.append(sum(1 for yy in range(y, y + gh)
                           for x in range(tx, rgt + 1)
                           if not px[yy*w2 + x]))
        return out

    # --- leg E: an aligned row is measured in the FACE (SPEC.md 68.13.2) ----
    # wd_rowmeasure asked wd_advof about the ADVANCE wd_penadv had left in AL,
    # so every character of a centred or flush right row measured as a space.
    # The flush right line then sat 32px too far right, wrapped although it
    # fits, and left its continuation row EMPTY - which is where Down stuck.
    # Read off the row table: the whole paragraph must be ONE row.
    dnote = m.read(rw("wd_dseg") * 16, rw("wd_len"))
    rows_ = lambda r: u16(m.read(P("wd_rows") + 2*r, 2))
    fmark = dnote.find(b"flush right")
    fps = dnote.rfind(b"\r", 0, fmark) + 1
    fpe = dnote.find(b"\r", fmark)
    nrows = min(rw("wd_rowsn"), vrows)
    frow = [r for r in range(nrows - 1) if rows_(r) <= fmark < rows_(r + 1)]
    check("the flush-right line is in the row table (case, not assertion)",
          fmark >= 0 and bool(frow) and rb("wd_rowsok"), "mark=%d rows=%s" % (fmark, frow))
    if frow:
        r = frow[0]
        check("E: the flush-right line is ONE row in a chosen face",
              rows_(r) == fps and rows_(r + 1) > fpe,
              "its paragraph %d..%d laid out as a row %d..%d - the row "
              "measure read an advance as a character" % (fps, fpe, rows_(r), rows_(r + 1)))

    # --- leg A: the invariant wd_ryb's own comment publishes ---------------
    # "band top of row r is ryb[r-1] + [wd_gh] (r=0: wd_ty)". wd_advy computes
    # it that way; the SEED had to compute it again by hand and did it with
    # the kernel's cell height as a literal. Read it at the flush, where the
    # row's y and its band top are both current.
    SEEN = []
    def on_flush(mm, rec):
        r = u16(mm.read(P("wd_row"), 2))
        return (r, u16(mm.read(P("wd_rbandt"), 2)), u16(mm.read(P("wd_rby"), 2)),
                u16(mm.read(P("wd_ryb") + 2*((r-1) & 0xFFFF), 2)) if 0 < r < vrows else None)

    before = profile()
    tops = [ryb(r) for r in range(vrows)]
    print("   rows: " + " ".join("%s" % ("-" if v is None else v)
                                 for v in before[:12]))

    # --- the gesture: a click well down the view, which SEEDS ---------------
    # §27.4.10's checkpoint is what a click uses instead of walking down to
    # its row, so this is the one gesture that exercises the reconstruction.
    target = None
    for r in range(vrows - 2, 2, -1):
        if before[r] and before[r] > 40 and before[r-1] and before[r-1] > 40:
            target = r; break
    check("a pair of inked rows was found (case, not assertion)", target is not None)
    if target is None:
        print("\nFAILED: nothing to click on"); sys.exit(1)
    print("   clicking row %d at y=%d (the row above it is %d, ink %d)"
          % (target, ryb(target), target - 1, before[target-1]))
    mo.to(tx + 40, ryb(target) + gh // 2); M.pace(m, 0.3)
    with M.bp_trace(m, base + syms["wd_rflush"], on_hit=on_flush, cap=4000) as tr:
        m.mouse(l=True); M.pace(m, 0.12); m.mouse(l=False)
        M.ui_done(m, "Word to handle the click")
    SEEN = [h["hit"] for h in tr.hits if h.get("hit")]
    M.settle(m)

    bad = [(r, bt, by, py) for (r, bt, by, py) in SEEN
           if py is not None and bt != (py + gh) & 0xFFFF]
    check("every flushed row's BAND TOP is ryb[row-1] + [wd_gh]", not bad,
          "%d of %d rows disagree: %s" % (len(bad), len(SEEN),
          "; ".join("row %d bandt %d, ryb[%d]+gh %d (%+d)"
                    % (r, bt, r-1, py+gh, bt-(py+gh)) for r, bt, by, py in bad[:3])))
    print("      %d rows flushed, %d with a band top to check"
          % (len(SEEN), sum(1 for x in SEEN if x[3] is not None)))

    moved = [r for r in range(vrows) if ryb(r) != tops[r]]
    check("the click did not scroll (case, not assertion)", not moved,
          "rows %s moved, so the profiles name different text" % moved[:4])

    # THE TOLERANCE IS ONE CARET BAR and not zero: the bar is [wd_gh] pixels
    # of a single column, and the row it LEFT gives exactly that many back.
    # The defect this row is for loses a row's whole lower half - hundreds.
    after = profile()
    lost = [(r, before[r], after[r]) for r in range(vrows)
            if before[r] and after[r] is not None and after[r] < before[r] - gh]
    check("NO row lost ink to the seeded walk", not lost,
          "; ".join("row %d %d -> %d" % t for t in lost[:4]))
    check("...and the row ABOVE the clicked one kept its glyphs",
          after[target-1] is None or before[target-1] is None
          or after[target-1] >= before[target-1] - gh,
          "row %d went %s -> %s, which is the field's own sentence"
          % (target-1, before[target-1], after[target-1]))
    print("   rows: " + " ".join("%s" % ("-" if v is None else v)
                                 for v in after[:12]))

    # --- leg C: a face change is a WRAP change (SPEC.md 68.13.1) ------------
    # [wd_drows] is the scroll bar's whole range. A face moves the advance of
    # every character, so the note wraps into a different number of rows -
    # and wd_a_csel's .reflow dropped the four caches that describe a layout
    # and not the HEIGHT. The field: *"the scrollbar doesn't represent the
    # actual bottom of the page... you can still click or arrow to scroll
    # further down, but it isn't on the bar."*
    check("C: the face pick marked the height dirty", nmark[0] > 0,
          "wd_hmark never ran, so [wd_drows] is still the OLD face's count "
          "and the scroll bar's range with it")
    print("      [wd_drows] %d -> %d over the face change, wd_hmark x%d"
          % (drows_before, rw("wd_drows"), nmark[0]))

    # --- leg D: ty_flush's carry is an ARGUMENT (SPEC.md 6.5.4) -------------
    # It used to `call OSAPI_GFX_PEN` and read the carry back as though that
    # were a query, so it armed [gfx_dis] with whatever its caller arrived
    # with - and by 68.2.5 that lives for the rest of the lock hold. Read the
    # byte where the damage showed: the status line's delta draw, which is
    # the LAST thing a click's redraw letters.
    dis = [0, 0]
    def atdiff(mm, rec):
        dis[0] += 1
        if mm.read(S("gfx_dis"), 1)[0]:
            dis[1] += 1
        return None
    with M.bp_trace(m, base + syms["wd_stdiff"], on_hit=atdiff, cap=256):
        for i in range(5):
            mo.to(tx + 40 + i * 24, ryb(3) + gh // 2 + i * ghb)
            M.pace(m, 0.25)
            m.mouse(l=True); M.pace(m, 0.1); m.mouse(l=False); M.pace(m, 1.0)
    M.settle(m)
    check("D: the status line is drawn with the pen LIVE in a chosen face",
          dis[0] > 0 and dis[1] == 0,
          "%d of %d status draws had [gfx_dis] set - ty_flush armed it and "
          "nothing put it back" % (dis[1], dis[0]))
    print("      %d status draws, %d with [gfx_dis] set" % (dis[0], dis[1]))

    # ...and the same for an ARROW, which seeds through the other door
    before = profile(); tops = [ryb(r) for r in range(vrows)]
    for _ in range(3):
        m.key("ArrowDown"); M.pace(m, 0.6)
    M.settle(m)
    if [r for r in range(vrows) if ryb(r) != tops[r]]:
        print("   the arrows scrolled - the ratchet is the click's alone")
    else:
        after = profile()
        lost = [(r, before[r], after[r]) for r in range(vrows)
                if before[r] and after[r] is not None and after[r] < before[r] - gh]
        check("...nor to three Down arrows", not lost,
              "; ".join("row %d %d -> %d" % t for t in lost[:4]))

    # --- legs F-H: Down to the LAST line (SPEC.md 68.6.2.1, 68.13.2) --------
    # The field: *"also seems fixed, except for when you arrow down to the
    # very last line in the file"*. Two defects stood between the top of the
    # note and that sentence. F is the first - Down stalled on the empty row
    # E is about. G and H are the second: a BLANK row stepped a literal 8, so
    # the first one below the last real row sat inside its glyphs and its
    # erase took the bottom off - "Files too." with its lower rows cut.
    mo.to(tx + 8, ryb(0) + 2); M.pace(m, 0.3)
    m.mouse(l=True); M.pace(m, 0.1); m.mouse(l=False); M.pace(m, 1.0)
    mo.to(4, 4); M.settle(m)
    absrow = lambda: (lambda v: v - 0x10000 if v & 0x8000 else v)(rw("wd_currow")) + rw("wd_top")
    stall = None
    for _ in range(rw("wd_drows") + 4):
        c0 = rw("wd_cur")
        m.key("ArrowDown")
        M.quiesce(m, lambda: (rw("wd_cur"), rw("wd_top"), rb("wd_hdirty")))
        if rw("wd_cur") == c0:
            if absrow() < rw("wd_drows") - 1:
                stall = (c0, absrow())
            break
    M.settle(m)
    check("F: Down reaches the note's last row in a chosen face", stall is None,
          "Down stopped moving at [wd_cur]=%d on absolute row %s of %d"
          % (stall[0], stall[1], rw("wd_drows")) if stall else "")
    top = rw("wd_top")
    short = [(r, ryb(r - 1), ryb(r)) for r in range(1, vrows)
             if ryb(r) < ryb(r - 1) + gh]
    check("G: no row starts inside the glyphs of the row above it", not short,
          "; ".join("row %d at %d, row above at %d (gh %d)" % (r, b, a, gh)
                    for r, a, b in short[:3]))
    lr = rw("wd_drows") - 1 - top
    if 0 <= lr < vrows and ryb(lr) + gh - 1 <= rw("wd_bot"):
        w2, px = shot()
        y = ryb(lr)
        low = sum(1 for yy in range(y + 8, y + gh) for x in range(tx, rgt + 1)
                  if not px[yy*w2 + x])
        # ONE CARET BAR of tolerance, as leg B: the bar is drawn after the
        # erase and is [wd_gh] tall, so a gutted row still reads a few px
        check("H: the LAST line keeps the rows below its first eight",
              gh <= 8 or low > gh,
              "rows %d..%d of the last line hold no ink - its descenders "
              "were erased by the blank row below it" % (y + 8, y + gh - 1))
        print("      last row %d at y=%d: %d ink px below row 8" % (lr, y, low))
    else:
        check("the last row is on the glass (case, not assertion)", False,
              "row %d, top %d" % (lr, top))

    # --- leg I: the bar and the view agree where the END is (SPEC.md 68.6.3)
    # The field: *"the scrollbar doesn't represent the actual bottom of the
    # page... you can still click or arrow to scroll further down, but it
    # isn't on the bar."* wd_sbset handed the bar [wd_vrows] as its PAGE -
    # an 8px count, 25 here - while wd_scrollmax let the view go on to
    # drows - [wd_vfit]. So the thumb hit the bottom with the view still
    # rows short of it. Page to the end the way a hand does, then read the
    # bar's own record: its position must be its own maximum.
    for _ in range(rw("wd_drows") + 2):
        t0 = rw("wd_top")
        m.key("PageDown")
        M.quiesce(m, lambda: (rw("wd_top"), rw("wd_cur"), rb("wd_hdirty")))
        if rw("wd_top") == t0:
            break
    M.settle(m)
    sb = lambda k: u16(m.read(P("wd_sb") + k, 2))
    top, rows, page, pos = rw("wd_top"), sb(8), sb(10), sb(12)
    check("the bar's record names this view (case, not assertion)", pos == top,
          "[wd_sb+12]=%d, [wd_top]=%d" % (pos, top))
    check("I: at the end of the note the thumb is at the end of the bar",
          pos == max(0, rows - page),
          "the view stopped at top %d of %d rows, and the bar's page of %d "
          "puts its own end at %d" % (top, rows, page, max(0, rows - page)))
    print("      end: top=%d rows=%d page=%d vfit=%d vrows=%d"
          % (top, rows, page, rw("wd_vfit"), rw("wd_vrows")))

print()
if FAIL:
    print("FAILED: " + ", ".join(FAIL)); sys.exit(1)
print("ok"); sys.exit(0)
