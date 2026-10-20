#!/usr/bin/env python3
"""PIXELSTEIN 3D's frame table, re-derived from the MEASURED units and solved
as a FIXED POINT (SPEC.md 97.1; docs/reports/PXS-FRAME-2026-09-13.md 2).

    python3 tools/pxsframe.py

The counts are docs/plans/PIXELSTEIN-PLAN.md 3's (scene A: mean wall 32
rows, 10 crossings a column, 3 sprites, the weapon, ~700 delta-fill stores at
64 x 80; scene B: ~2,500 delta-fill stores), scaled to the rung; the units
are tests/pxsbench.py's off MartyPC's cycle-exact 5150, copied here by hand
from the report with the date. WHY A SCRIPT: the first take of the table was
evaluated once with the simulation at "1.3 ticks" - DOT DELIRIUM's own frame
/ tick, i.e. the quantity being solved for. tk_steps (97.8) returns ELAPSED
TICKS capped at 3, so the simulation is a function of the frame it is part
of:

    steps a frame = frame / 54.925 ms, capped at 3
    F = N + s * (F / T)   ->   F = N / (1 - s / T)      (T = one tick)

and past the cap F = N + 3s. N is everything but the simulation, s one tick
of it. s is the one term the fixed point GEARS - dF/ds = F / (T - s), 2.6x at
the default rung - so it is MEASURED (the bench's two SIM rows: one tick at
E1M1's own counts, 7 actors and 22 doors, and one at the plan's caps, 32 and
64) and every row below is priced at both. WHICH WAY THE HEDGE RUNS: an
optimistic s makes the FRAME (clk, ms) a FLOOR and only the fps a ceiling; a
frame quoted here can only get longer if s was under-counted, never shorter.

THE VIEW: Size columns of the 80-byte row, ONE BYTE A COLUMN a row at Full
resolution (Size rays) and TWO at Low res (Size / 2 rays, a word store a
row), so the band the present copies is Size bytes wide at either resolution
and the delta-fill is Size / 64 of the plan's count. The three dither arms
are priced per backend: 'bare' charges no odd-row phase at all (what Mode X
and C160 pay - 16 solid colours or a DAC, no dither); 'ror' the rotate AS
THE 1992 ENGINE TURNS IT (WL_SCALE.C's dithershift): `ror al, 1` x 2 on CGA
320x200x4 (one 2-bit pixel), one `ror al, cl` with CL = 3 on Hercules and
WIN1 - the arm 97.3 TAKES; 'word' the dual-phase word load, the named
FALLBACK (part 3 doubled, no rotate). Every number SPEC.md 97.1's frame
paragraph quotes is a line of this script's output.

THE TEXEL TERMS ARE THE GENERATOR'S, NOT A ROW COUNT (review, wave 2): a
compiled scaler pays a load PER TEXEL RUN that covers a view row, a store
per row, the phase only for a run holding an ODD row, and at Low res one
`mov ah, al` per PARITY the run covers - so scene A's mean wall (h = 32, one
row a run) is 32 loads, 32 stores, 16 phases and 32 `mov ah, al`, where the
first cut of this file charged a phase and two `mov ah, al` per run (~595
clk a column high) and its whole-game branch charged all three PER ROW
(~3,700 a column high at h = 80). scaler_terms() walks tools/pxsgen.py's
texel_rows() and counts what the emitted code holds.
"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pxsgen                      # noqa: E402  (the scaler's row walk)

HZ = 4772727.0                     # the 5150's 8088
TICK = HZ / 18.2065                # one PIT tick, 262,150 clk = 54.925 ms
CAP = 3                            # tk_steps' cap

# --- the units, M (tests/pxsbench.py, 2026-09-13, r5) -------------------------
CROSS = 128.2                      # a DDA crossing, the body of 97.2.2
SETUP = 1094.0                     # 97.2.1 whole, net of the bench's scaffold
HIT = 1294.5                       # 97.2.5 whole, net of the scaffold (its
                                   # own multiplies, divide and stores are
                                   # ~800: the FLOOR a hit can reach, not 630)
STORE = 24.3                       # mov [di + r*80], al into RAM
WSTORE = 28.3                      # ...the word form (Resolution: Low res)
TEXEL = 46.0                       # mov al, [es:si + v] + the store
LADDER = 24.1                      # a static-ladder row's STORE - DERIVED from
                                   # the two measured rows (80 x 25.5 = 2,040
                                   # and 40 x 26.9 = 1,076 give 24.1 a store
                                   # and 112 the entry); 25.5 is the 80-row
                                   # reading with the entry amortised in it,
                                   # and charging 25.5 AND the entry counted
                                   # the entry twice
LADDER_ENTRY = 112.0               # ...and the entry the bench could see: the
                                   # computed jump into the ladder and its ret,
                                   # amortised out of the two row readings
LADDER_CALL = LADDER_ENTRY + 168.0 # THE WHOLE px_lad CALL, prologue to first
                                   # store: the entry above plus ~168 D for
                                   # what wraps it in the package and the bench
                                   # did not run - the near call and ret (43),
                                   # the cs:-prefixed row-table and base loads,
                                   # the parity swap, the count-to-entry shifts
                                   # and the byte/word fork (~20 instructions
                                   # at the 8088's fetch floor). 280 is the one
                                   # figure pxcomp.inc, SPEC.md 97.5/97.8 and
                                   # the frame report quote for "a ladder
                                   # entry"; 112 alone is the bench's row
ROR = {"cga4": 18.0,               # `ror al, 1` x 2 after the load: +18.0 a
                                   # texel M (the datasheet's 2 x 8.7 = 17.4)
       "herc": 15.2, "mono": 15.2, "vga": 15.2,   # ONE `ror al, cl`, CL = 3:
                                   # +15.2 M (the datasheet's 8 + 4 x 3 = 20
                                   # overlaps the fetch of the next byte)
       "cga16": 0.0, "modex": 0.0} # 16 solid colours / a DAC: no dither
WORD = 4.8                         # the dual-phase word load over the byte load
LOWTEX = 59.5                      # the Low res row: load + mov ah,al + word
                                   # store, one texel to two shadow bytes
MOVAHAL = 9.6                      # ...of which the `mov ah, al` is 9.6 (the
                                   # word store's own delta over the byte
                                   # store, rows 16 and 0, comes out first)
COPY = {"cga4": 19.4, "herc": 19.3}            # rep movsw to VRAM, clk a byte
EXPAND = 43.3                                  # the C160 expand, clk a byte
STORE_VRAM = {"cga4": 30.1, "herc": 32.1}
# THE XT-VGA MACHINE IS A CORRECTNESS INSTRUMENT AND NOT A TIMING ONE
# (docs/MARTYPC-DEBUG.md's list: "the field has no VGA"), and its store row
# reads 25.2 - 0.9 over a RAM store where the two calibrated cards pay 5.8
# and 7.8, which no 8-bit ISA card answers. Mode X is therefore priced at
# the CGA-class range, both ends printed, until wave 2's pxsperf.py on 86Box
STORE_VRAM_MODEX = (30.1, 32.1)
BLIT1_FIXED = 0.40e-3 * HZ         # OSAPI_GFX_BLIT1 on a 1bpp desktop: the
BLIT1_BYTE = 4.74e-6 * HZ          # call (D) + a band byte (M: 24.67 ms for
                                   # 512 x 80 = 5,120 B, the split the plan's)
TAX = {"mono": 3.8e-3 * HZ}        # the windowed round trip, M on Hercules
TAX_VGA = 6.7e-3 * HZ              # ...and on the XT-VGA: NOT a timing machine,
                                   # so WIN1 XT-VGA is D pending a timeable card
KEY = 541.0                        # OSAPI_KEY_DOWN, a call
RETRACE_VGA = HZ / 60.0            # Mode X presents on a retrace: 16.67 ms buckets
# THE SIM TERM, one tick, M (the bench's rows (j)): E1M1's own counts - 7
# actors, 22 doors, 4 movers, 4 line-of-sight tile walks - and the plan's
# CAPS, 32 and 64, 16 movers, 12 walks. The same on all three machines (it is
# RAM and registers). DOT DELIRIUM's five-dot step, 6.81 ms, stood in for it
# through three takes of this table
SIM = {"level": 4.60e-3 * HZ, "cap": 16.98e-3 * HZ}
# --- WAVE 1's RESIDUALS, MEASURED (tests/pixelstein.py --stages, 2026-09-14,
# docs/reports/PXS-FRAME-2026-09-14.md 3): five breakpoints split the
# package's own frame into cast / compose / present / loop, so the gap between
# this model and the measured frame has a STAGE and a per-column figure -----
CAST_RESID = 1250.0                # a RAY: the cast measured 4,830 clk a ray
                                   # on scene A (9.3 crossings) against the
                                   # units' 3,581 - the far call and its retf
                                   # (~90), px_col and px_qcur through memory,
                                   # the tables by q*2, and a hit that is
                                   # longer than the bench's (the jamb test,
                                   # the mirror, the pushes). Textured
                                   # inherits ALL of it: the cast is the same
FLAT_RESID = 1400.0                # a COLUMN of the Flat rung: px_compose's
                                   # per-column bookkeeping around its two or
                                   # three px_lad calls (~90 cs:-prefixed
                                   # instructions, fetch-bound), measured
                                   # 4,538 a column at Low res against 3,104
                                   # of stores + entries. The generated
                                   # driver of wave 2 replaces this shape, so
                                   # it is the Flat rung's and not the design's
# THE TEXTURED COLUMN'S RESIDUAL, FITTED from the measured compose of the
# review round's tree (docs/reports/PXS-FRAME-2026-09-21.md 3: 198,365 clk
# over 32 columns at Low res, 345,029 over 64 at Full, scene A's full repaint)
# less what scaler_terms(32) + the ground + the queue entry price: the
# six-byte skip and its banking, the queue entry's shifts and adds, the
# column prologue and, at Low res, px_lad's word-path pair setup - the ~180
# between the two. The first take of this file charged FLAT_RESID here and
# read "0.8% under", which was this residual ~470 low and the texel terms
# ~595 high, cancelling (review, wave 2). Refit when the compose changes
TEX_RESID = {True: 1916.0, False: 1735.0}          # by lowres
TEX_QUEUE = 170.0                  # the queue entry (~120) + the driver's
                                   # near call and ret (~50), a column
TEX_MEANH = 32                     # scene A's mean wall, rows (the plan's 3)


def scaler_terms(h, lowres, backend):
    """One column of a wall h rows tall through the compiled scaler of that
    height, clk: what the emitted code holds (tools/pxsgen.py's texel_rows,
    pxgen.inc's px_gen_one) - a load per texel run on the view, a store per
    row, the phase per run with an odd row, and at Low res a `mov ah, al`
    per parity a run covers."""
    n = ROR[backend]
    loads = stores = phases = ahal = 0
    for rows in pxsgen.texel_rows(pxsgen.quantise(h)):
        if not rows:
            continue
        loads += 1
        stores += len(rows)
        odd = any(r & 1 for r in rows)
        even = any(not (r & 1) for r in rows)
        phases += odd
        if lowres:
            ahal += even + odd
    return (loads * (TEXEL - STORE) + stores * (WSTORE if lowres else STORE)
            + phases * n + ahal * MOVAHAL)


def texel_run_cost(rows, lowres, backend, phase):
    """`rows` texel ROWS drawn at scene A's mean-wall shape - one row a run
    (h = 32 is 32 runs of one row): a load, a store, a `mov ah, al` at Low
    res and the phase on the odd half. What the sprites' and weapon's
    texels are priced at until wave 3 measures them (a taller post has
    FEWER loads a row, so this is their ceiling)."""
    n = ROR[backend]
    per = (TEXEL - STORE) + (WSTORE if lowres else STORE) + (MOVAHAL if lowres else 0)
    if phase == "ror" and n:
        per += n / 2.0
    elif phase == "word" and n:
        per += WORD                                # a word load a run, no rotate
    return rows * per


def stages(cols=64, rows=80, crossings=10, dfill=700, lowres=False,
           flat=False, phase=None, backend="cga4"):
    """The non-sim stages of one frame, clk (the plan's 3 counts, scaled)."""
    k = cols / 64.0
    r = rows / 80.0
    rays = cols // 2 if lowres else cols
    cast = rays * (SETUP + crossings * CROSS + HIT + CAST_RESID)
    walk = 11000.0                                 # candidates + graft 4
    wall, spr, wpn = 2048 * k * r, 1800 * k * r, 384 * k   # BYTES stored
    stores = wall + spr + wpn
    if flat:
        if lowres:                                 # the ladder in word stores
            tex = rays * LADDER_ENTRY + stores / 2 * (LADDER - STORE + WSTORE)
        else:
            tex = cols * LADDER_ENTRY + stores * LADDER
        tex += rays * FLAT_RESID
    else:
        # every texel row at the mean-wall shape (one run a row), the
        # bytes halved to rows at Low res (a word store a row)
        trows = stores / 2 if lowres else stores
        tex = texel_run_cost(trows, lowres, backend, phase)
        tex += rays * (TEX_RESID[lowres] + TEX_QUEUE)   # the column's own
    # the delta-fill writes the same BYTES at either resolution: word stores
    # at Low res, half as many
    dfl = dfill * k * r * (WSTORE / 2 if lowres else STORE)
    posts = (75 * k + 16) * 350
    over = rays * 240 + (posts / 2 if lowres else posts) + 8 * 1000 + 50
    hud = 5000.0
    # TEN OSAPI_KEY_DOWNs a frame, not eight: with nothing held every jc
    # falls through to the second reader - LEFT, RIGHT, LSHIFT, RSHIFT, UP,
    # W, DOWN, S, A, D (px_keys_read; wave 1's review counted them)
    inp = 10 * KEY + 2000 + 10 * 223
    return dict(cast=cast, walk=walk, tex=tex, dfill=dfl, over=over, hud=hud,
                inp=inp)


def present(backend, cols=64, rows=80):
    """The band is Size bytes wide at either resolution (97.3)."""
    if backend in ("cga4", "herc"):
        return cols * rows * COPY[backend]
    if backend == "cga16":
        return cols * rows * EXPAND
    if backend == "modex":
        return 0.0                                 # priced in full() below
    if backend == "mono":
        return BLIT1_FIXED + cols * rows * BLIT1_BYTE + TAX["mono"]
    if backend == "vga":
        return BLIT1_FIXED + cols * rows * BLIT1_BYTE + TAX_VGA   # D: see above
    raise ValueError(backend)


def converge(n, s):
    f = n / (1 - s / TICK)
    if f / TICK > CAP:
        f = n + CAP * s
    return f


def row(name, n, quantise=False):
    """One line: the frame at E1M1's tick, and the fps at the caps beside."""
    f = converge(n, SIM["level"])
    fc = converge(n, SIM["cap"])
    draw = f
    tail = ""
    if quantise:
        buckets = -(-f // RETRACE_VGA)
        f = buckets * RETRACE_VGA
        tail = "  (draw %.1f ms, %d retraces)" % (draw / HZ * 1e3, buckets)
    print("%-34s nonsim %7.0f  frame %7.0f = %6.1f ms = %5.2f fps  (caps %5.2f)"
          "  steps %.2f%s" % (name, n, f, f / HZ * 1e3, HZ / f, HZ / fc,
                              draw / TICK, tail))
    return f


def full(name, backend, quant=False, arms=(None, "ror", "word"), **kw):
    for ph in arms:
        n_ror = ROR[backend]
        if ph in ("ror", "word") and not n_ror:
            continue                    # no phase to turn on Mode X / C160
        n = sum(stages(phase=ph, backend=backend, **kw).values())
        cols, rows = kw.get("cols", 64), kw.get("rows", 80)
        if backend == "modex":
            # every store into VRAM, the delta-fill on two pages, 64 outs -
            # at BOTH ends of the CGA-class range (the XT-VGA row is not a
            # timing)
            stores = 4932 * cols / 64.0 * rows / 80.0
            for sv in STORE_VRAM_MODEX:
                nn = n + stores * (sv - STORE) + 17000 + 2700
                arm = {None: "bare", "ror": "ror", "word": "word"}[ph]
                row("%s %s @%.1f" % (name, arm, sv), nn, quantise=quant)
            continue
        n += present(backend, cols, rows)
        arm = {None: "bare", "ror": "ror x%d" % {18.0: 2, 15.2: 1}.get(n_ror, 0),
               "word": "word"}[ph]
        row("%s %s" % (name, arm), n, quantise=quant)


def main():
    a = stages(lowres=True, phase="ror")
    print("scene A stages, 64x80 Low res, ror:",
          {k: round(v) for k, v in a.items()}, "sum", round(sum(a.values())))
    w = stages(phase="ror")
    print("scene A stages, 64x80 Full, ror x2:",
          {k: round(v) for k, v in w.items()}, "sum", round(sum(w.values())))
    print("--- the XT default: Size 64 x Rows 80 x Resolution Low res (32 rays)")
    full("CGA4 64x80 Low res A", "cga4", lowres=True)
    full("CGA4 64x80 Low res B", "cga4", lowres=True, dfill=2500)
    full("Herc 64x80 Low res A", "herc", lowres=True)
    full("Herc 64x80 Low res B", "herc", lowres=True, dfill=2500)
    print("--- the Low res fallback rungs")
    full("CGA4 56x80 Low res A", "cga4", lowres=True, cols=56, arms=("ror",))
    full("CGA4 56x80 Low res B", "cga4", lowres=True, cols=56, dfill=2500, arms=("ror",))
    full("CGA4 48x80 Low res A", "cga4", lowres=True, cols=48, arms=("ror",))
    full("CGA4 48x80 Low res B", "cga4", lowres=True, cols=48, dfill=2500, arms=("ror",))
    full("Herc 48x80 Low res A", "herc", lowres=True, cols=48, arms=("ror",))
    full("Herc 48x80 Low res B", "herc", lowres=True, cols=48, dfill=2500, arms=("ror",))
    print("--- Full resolution, OFFERED on the Size row and reported, never promised")
    full("CGA4 64x80 Full A", "cga4")
    full("CGA4 64x80 Full B", "cga4", dfill=2500)
    full("Herc 64x80 Full A", "herc")
    full("Herc 64x80 Full B", "herc", dfill=2500)
    full("CGA4 56x80 Full A", "cga4", cols=56, arms=("ror",))
    full("CGA4 56x80 Full B", "cga4", cols=56, dfill=2500, arms=("ror",))
    full("Herc 56x80 Full A", "herc", cols=56, arms=("ror",))
    full("CGA4 48x80 Full A", "cga4", cols=48, arms=("ror",))
    full("CGA4 48x80 Full B", "cga4", cols=48, dfill=2500, arms=("ror",))
    full("Herc 48x80 Full A", "herc", cols=48, arms=("ror",))
    full("Herc 48x80 Full B", "herc", cols=48, dfill=2500, arms=("ror",))
    full("CGA4 64x100 Full A", "cga4", rows=100, arms=("ror",))
    print("--- the other backends (C160 and Mode X have no phase; XT-VGA rows are D)")
    full("CGA16 48x80 Full A", "cga16", cols=48)
    full("CGA16 48x80 Low res A", "cga16", cols=48, lowres=True)
    full("ModeX 64x80 Full A", "modex", quant=True)
    full("ModeX 64x80 Low res A", "modex", quant=True, lowres=True)
    full("WIN1 mono 64x80 Full A", "mono", arms=("ror",))
    full("WIN1 mono 64x80 Low res A", "mono", lowres=True, arms=("ror",))
    full("WIN1 XT-VGA 64x80 Full A (D)", "vga", arms=("ror",))
    full("WIN1 XT-VGA 64x80 Low res A (D)", "vga", lowres=True, arms=("ror",))
    print("--- the static rungs")
    fl = stages(flat=True)
    row("CGA4 Flat 64x80 Full A", sum(fl.values()) + present("cga4"))
    row("CGA4 Flat 64x80 Full B",
        sum(stages(flat=True, dfill=2500).values()) + present("cga4"))
    row("Herc Flat 64x80 Full A", sum(fl.values()) + present("herc"))
    row("CGA4 Flat 64x80 Low res A",
        sum(stages(flat=True, lowres=True).values()) + present("cga4"))
    row("CGA4 Flat 48x80 Full A",
        sum(stages(flat=True, cols=48).values()) + present("cga4", 48))
    # NO WIRE ROW: the rung is built and measured (docs/reports/PXS-FRAME-
    # 2026-09-14.md), and the row this file carried - ~10 stores a column and
    # a ~30,000-clk present - priced a frame no forced repaint can have (the
    # history seeded to the whole column makes the first Wire frame lay 80
    # rows of ground, and the review priced it to 0.6%). A model row for a
    # measured rung is a second number for one fact.
    print("--- the FULL REPAINT with no sim, no sprites, no HUD (what the gates measure):")
    print("    the whole view a column - 80 rows - and the loop's 10 key reads; the row")
    print("    to read tests/pixelstein.py's full-repaint figure AGAINST, because the")
    print("    rows above charge a whole game (the sprite walk, 91 posts, the HUD, the")
    print("    sim tick) that no measured frame draws yet. Wave 1's Flat measured 88.3")
    print("    ms Low res / 145.8 Full on _cga_gla; wave 2's Textured 100.3 Low res /")
    print("    165.0 Full (docs/reports/PXS-FRAME-2026-09-21.md)")
    loop = 10 * KEY + 2000 + 10 * 223
    for lowres, cols_, label in ((True, 32, "CGA4 Flat 64x80 Low res A, full repaint"),
                                 (False, 64, "CGA4 Flat 64x80 Full A, full repaint")):
        c = stages(lowres=lowres, flat=True)["cast"]
        st = WSTORE if lowres else STORE
        comp = cols_ * (80 * st + 3 * LADDER_CALL + FLAT_RESID)
        n = c + comp + present("cga4") + loop
        print("%-42s cast %7.0f  compose %7.0f  present %7.0f  loop %5.0f  = %7.0f clk = %6.1f ms"
              % (label, c, comp, present("cga4"), loop, n, n / HZ * 1e3))
    # THE TEXTURED ARMS, walls only: every column a wall of scene A's mean 32
    # rows through the compiled scaler of that height - scaler_terms(): what
    # the emitted code holds, a load a texel RUN, a store a row, the phase
    # per run with an odd row, the Low-res `mov ah, al` per parity a run
    # covers - the queue entry and the driver's near call (TEX_QUEUE), and the
    # column's bookkeeping (TEX_RESID, FITTED from the Low res 64 A and Full
    # 64 A rows below, so those two rows are the fit and the other two are
    # the check). The ground: a forced frame seeds every column to the whole
    # view (px_force_all), so what the wall does not cover is put back by the
    # ladders - on scene A ~48 rows x 32 columns: 2 ladder entries + the
    # stores. MEASURED (PXS-FRAME-2026-09-21.md 3, the review round's tree)
    # beside each row where there is a measurement
    measured = {"CGA4 Textured 64x80 Low res A, full repaint": (164895, 198365, 99249, 7945, 653),
                "CGA4 Textured 64x80 Full A, full repaint": (326607, 345029, 99249, 8150, 653),
                "CGA4 Textured 48x80 Low res A, full repaint": (128998, 146702, 76206, 7732, 653),
                "Herc Textured 64x80 Low res A, full repaint": None}   # (cast, compose,
                                                    # present, loop, prologue)
    frames = {}
    for lowres, cols_, be, label in (
            (True, 32, "cga4", "CGA4 Textured 64x80 Low res A, full repaint"),
            (False, 64, "cga4", "CGA4 Textured 64x80 Full A, full repaint"),
            (True, 32, "herc", "Herc Textured 64x80 Low res A, full repaint"),
            (True, 24, "cga4", "CGA4 Textured 48x80 Low res A, full repaint")):
        k = cols_ * (2 if lowres else 1) / 64.0
        c = stages(lowres=lowres, cols=int(64 * k))["cast"]
        wall = scaler_terms(TEX_MEANH, lowres, be)
        ground = (80 - TEX_MEANH) * (WSTORE if lowres else STORE) + 2 * LADDER_CALL
        col = wall + ground + TEX_QUEUE + TEX_RESID[lowres]
        comp = cols_ * col
        pres = present(be, int(64 * k))
        n = c + comp + pres + loop
        frames[label] = n
        print("%-42s cast %7.0f  compose %7.0f  present %7.0f  loop %5.0f  = %7.0f clk = %6.1f ms"
              % (label, c, comp, pres, loop, n, n / HZ * 1e3))
        print("%-42s   a column: scaler %5.0f (h=%d) + ground %5.0f + queue %3.0f + resid %5.0f = %5.0f"
              % ("", wall, TEX_MEANH, ground, TEX_QUEUE, TEX_RESID[lowres], col))
        m = measured.get(label)
        if m:
            mn = sum(m)
            print("%-42s   measured: cast %7.0f  compose %7.0f  present %7.0f  loop %5.0f +%3.0f = %7.0f clk = %6.1f ms"
                  "  (model %+.1f%%: cast %+.1f%%, compose %+.1f%%)"
                  % ("", m[0], m[1], m[2], m[3], m[4], mn, mn / HZ * 1e3, (n - mn) / mn * 100,
                     (c - m[0]) / m[0] * 100, (comp - m[1]) / m[1] * 100))
    # THE PROJECTION (SPEC.md 97.1's fork): the MEASURED walls-only frame plus
    # what this frame does not draw yet, priced on the plan's 3 counts -
    # the candidate walk, the sprites' and weapon's texel rows at the
    # mean-wall shape (texel_run_cost), the posts and transforms (the plan's
    # per-post 350 and per-sprite 1,000; the rays x 240 of `over` is
    # bookkeeping the measured column already carries), the HUD - then the
    # sim tick as the fixed point. NOT a measurement: wave 3's own row
    # replaces it
    print("--- the PROJECTION of the finished frame: the measured walls-only frame plus")
    print("    the sprite walk, the sprites' and weapon's texels, the posts and the HUD on")
    print("    the plan's counts, then E1M1's tick - the number SPEC.md 97.1's fork is read")
    print("    against, and the reason its 64x80 default is PROVISIONAL until wave 3 measures")
    for label, k, meas in (("CGA4 Textured 64x80 Low res A", 1.0, 471107),
                           ("CGA4 Textured 64x80 Low res B", 1.0, 474409),
                           ("Herc Textured 64x80 Low res A", 1.0, 477750),
                           ("CGA4 Textured 48x80 Low res A", 0.75, 354136)):
        be = "herc" if label.startswith("Herc") else "cga4"
        rays = int(32 * k)
        spr_rows = (1800 * k + 384 * k) / 2
        missing = (11000.0 + texel_run_cost(spr_rows, True, be, "ror")
                   + (75 * k + 16) * 350 / 2 + 8 * 1000 + 50 + 5000.0)
        n = meas + missing
        f = converge(n, SIM["level"])
        fc = converge(n, SIM["cap"])
        print("%-34s measured %7.0f + missing %6.0f = %7.0f clk = %6.1f ms = %5.2f fps pre-sim;"
              " at E1M1's tick %7.0f = %6.1f ms = %5.2f fps (caps %5.2f)  [%d rays]"
              % (label, meas, missing, n, n / HZ * 1e3, HZ / n, f, f / HZ * 1e3, HZ / f,
                 HZ / fc, rays))
    print("tick %.0f clk; the cap binds at %.1f ms = %.2f fps; s = %.2f ms "
          "(E1M1) / %.2f ms (caps): dF/ds at the default = %.2f"
          % (TICK, 3 * TICK / HZ * 1e3, HZ / (3 * TICK), SIM["level"] / HZ * 1e3,
             SIM["cap"] / HZ * 1e3,
             converge(sum(a.values()) + present("cga4"), SIM["level"])
             / (TICK - SIM["level"])))


if __name__ == "__main__":
    main()
