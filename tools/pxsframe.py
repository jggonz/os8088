#!/usr/bin/env python3
"""PIXELSTEIN 3D's frame table, re-derived from the MEASURED units and solved
as a FIXED POINT (SPEC.md 96.1; docs/reports/PXS-FRAME-2026-09-13.md 2).

    python3 tools/pxsframe.py

The counts are docs/plans/PIXELSTEIN-PLAN.md 3's (scene A: mean wall 32
rows, 10 crossings a column, 3 sprites, the weapon, ~700 delta-fill stores at
64 x 80; scene B: ~2,500 delta-fill stores), scaled to the rung; the units
are tests/pxsbench.py's off MartyPC's cycle-exact 5150, copied here by hand
from the report with the date. WHY A SCRIPT: the first take of the table was
evaluated once with the simulation at "1.3 ticks" - DOT DELIRIUM's own frame
/ tick, i.e. the quantity being solved for. tk_steps (96.8) returns ELAPSED
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
WIN1 - the arm 96.3 TAKES; 'word' the dual-phase word load, the named
FALLBACK (part 4 doubled, no rotate). Every number SPEC.md 96.1's frame
paragraph quotes is a line of this script's output.
"""
HZ = 4772727.0                     # the 5150's 8088
TICK = HZ / 18.2065                # one PIT tick, 262,150 clk = 54.925 ms
CAP = 3                            # tk_steps' cap

# --- the units, M (tests/pxsbench.py, 2026-09-13, r5) -------------------------
CROSS = 128.2                      # a DDA crossing, the body of 96.2.2
SETUP = 1094.0                     # 96.2.1 whole, net of the bench's scaffold
HIT = 1294.5                       # 96.2.5 whole, net of the scaffold (its
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
                                   # figure pxcomp.inc, SPEC.md 96.5/96.8 and
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
    n = ROR[backend]
    if flat:
        if lowres:                                 # the ladder in word stores
            tex = rays * LADDER_ENTRY + stores / 2 * (LADDER - STORE + WSTORE)
        else:
            tex = cols * LADDER_ENTRY + stores * LADDER
        tex += rays * FLAT_RESID
    elif lowres:
        loads = stores / 2                         # one texel, two bytes
        tex = loads * LOWTEX
        if phase == "ror" and n:
            tex += loads * (n + MOVAHAL)           # the rotate, then AH again
        elif phase == "word" and n:
            tex += loads * (WORD + 2 * MOVAHAL)    # AL/AH kept apart, then each
                                                   # duplicated in turn
    else:
        tex = stores * TEXEL
        if phase == "ror":
            tex += stores * n
        elif phase == "word" and n:
            tex += stores * WORD
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
    """The band is Size bytes wide at either resolution (96.3)."""
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
    print("--- wave 1's FULL REPAINT of the Flat rung, no sim (what the gate asserts on):")
    print("    the whole view a column - 80 rows, three ladder entries - and the loop's")
    print("    10 key reads; measured 88.3 ms Low res / 145.8 ms Full on _cga_gla")
    for lowres, cols_, label in ((True, 32, "CGA4 Flat 64x80 Low res A, full repaint"),
                                 (False, 64, "CGA4 Flat 64x80 Full A, full repaint")):
        c = stages(lowres=lowres, flat=True)["cast"]
        st = WSTORE if lowres else STORE
        comp = cols_ * (80 * st + 3 * LADDER_CALL + FLAT_RESID)
        n = c + comp + present("cga4") + 10 * KEY + 2000 + 10 * 223
        print("%-42s cast %7.0f  compose %7.0f  present %7.0f  loop %5.0f  = %7.0f clk = %6.1f ms"
              % (label, c, comp, present("cga4"), 10 * KEY + 2000 + 10 * 223, n, n / HZ * 1e3))
    print("tick %.0f clk; the cap binds at %.1f ms = %.2f fps; s = %.2f ms "
          "(E1M1) / %.2f ms (caps): dF/ds at the default = %.2f"
          % (TICK, 3 * TICK / HZ * 1e3, HZ / (3 * TICK), SIM["level"] / HZ * 1e3,
             SIM["cap"] / HZ * 1e3,
             converge(sum(a.values()) + present("cga4"), SIM["level"])
             / (TICK - SIM["level"])))


if __name__ == "__main__":
    main()
