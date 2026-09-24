#!/usr/bin/env python3
"""PIXELSTEIN 3D's unit costs, read off a cycle-exact 5150 (SPEC.md 97.10).

    make bench && python3 tests/pxsbench.py [--machine os8088_5150_cga_gla]
                                            [--shot build/pxs-shots/bench.png]

AN INSTRUMENT with one assertion. tests/pxsbench/pxsbench.asm runs its rows
inside the guest - benchlib's PIT-bracketed method, which MartyPC counts to
the cycle - and leaves each row's hundredths of a microsecond per iteration
and a flag byte in its own bss (pb_res / pb_resf). This boots the bench disk,
launches PXSBENCH.O88 off the Disk window, presses R, waits for the run to
finish on the GUEST's clock and reads the two arrays back. What it prints is
the row table with the unit each row is priced in - cycles a store, a
crossing, a byte - and the derived figures docs/plans/PIXELSTEIN-PLAN.md 3
used as DERIVED, so that SPEC.md 97.1 can carry them as MEASURED.

What it asserts is that EVERY ROW THIS ADAPTER OWES PRODUCED A NUMBER: not
lapped, not refused, not skipped, not zero. The numbers themselves are
reported and never gated (tests/tank.py's rule: a number that fails a build
when a container gets slower teaches nobody anything) - they go to
docs/reports/ with the date and the machine.

The bench measures the same package on every machine; `--machine` picks the
adapter, and the VRAM rows run in that adapter's game mode (CGA 320x200x4,
Hercules, Mode X), the two C160 rows on a genuine CGA only. The VRAM store
rows set DS = the framebuffer and store with NO segment override - the
instruction the generator emits (97.3) - so their delta against the RAM row
is the card's wait states and nothing else. The DDA rows carry 97.2.1's
whole setup and a fourth row runs only the harness's scaffolding, so the
setup figure below is net of it.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))   # LAST, so it wins: tests/
                                    # has a pxssim.py of its own (the gate),
                                    # and tools/pxssim.py is the renderer the
                                    # crossings are cast through (SPEC.md
                                    # 97.10; this row broke on it while the
                                    # bench could not build, wave 6)
import pxssim, pxstab           # noqa: E402 - BEFORE cycweb, which puts
                                # tests/ back at the head of sys.path
import os88marty, os88mouse, os88sym, os88build, dispcp   # noqa: E402
from cycweb import pkg_syms, u16                               # noqa: E402
import pxslib                   # noqa: E402 - B: and the watched launch, shared

HZ = 4772727.0                  # the 5150's 8088, PERFORMANCE.md Part 2
NRES = 26                       # PB_NRES
ROWS = 80                       # PB_ROWS
# THE BENCH'S OWN GEOMETRY (pxsbench.asm's PB_* constants), so that the
# crossing counts the DDA rows are divided by are CAST on the host through
# tools/pxssim.py's walker and asserted, rather than derived by hand once
# and trusted: a wall moved by a tile makes the divisor 9 or 11 and moves
# every consumer of 97.1's crossing figure by 10% without a word said.
EYEX, EYEY = 8 * 256 + 64, 8 * 256 + 128
COL = 32
HEAD45, HEADA = 512 - 6, 100 - 6
WALL10, WALL20 = (13, 13), (18, 18)
WALLA = ((17, 8), (17, 9), (17, 10))
# row index -> (label, the unit to divide by, the unit's name)
TABLE = [
    (0, "STORE 80 rows RAM", ROWS, "store"),
    (12, "STORE 80 rows VRAM", ROWS, "store"),
    (16, "STORE word 80 rows RAM", ROWS, "store"),
    (17, "STORE word 80 rows VRAM", ROWS, "store"),
    (1, "LADDER 80 Duff entry", ROWS, "row"),
    (2, "LADDER 40 Duff entry", 40, "row"),
    (18, "DDA scaffold only", 1, "column"),
    (3, "DDA col 10 crossings", 1, "column"),
    (4, "DDA col 20 crossings", 1, "column"),
    (5, "DDA col 10 near-axial", 1, "column"),
    (21, "HIT col (97.2.5)", 1, "column"),
    (6, "TEXEL 80 rows plain", ROWS, "texel"),
    (7, "TEXEL 80 rows xlat", ROWS, "texel"),
    (20, "TEXEL 80 rows ror x2", ROWS, "texel"),
    (22, "TEXEL 80 rows ror cl3", ROWS, "texel"),
    (19, "TEXEL 80 rows word", ROWS, "texel"),
    (23, "TEXEL 80 rows lowres", ROWS, "texel"),
    (24, "SIM tick 32a 64d cap", 1, "tick"),
    (25, "SIM tick 7a 22d E1M1", 1, "tick"),
    (8, "KEY_DOWN x8", 8, "call"),
    (9, "BLIT1 512x80 desktop", 1, "blit"),
    (13, "COPY 5120B to VRAM", 5120, "byte"),
    (14, "EXPAND 3840B C160", 3840, "byte"),
    (15, "STORE 80 rows str160", ROWS, "store"),
    (10, "GEN scaler set x1", 1, "generation"),
    (11, "BT_BUILD 15x2x1K", 1, "transpose"),
]
CGA_ONLY = (14, 15)
FAIL = []


def check(ok, what):
    print("   %-62s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def bench_map(*walls):
    """pb_mapclear + pb_wall: a 64x64 map with a solid border and the given
    cells solid, as bytes the walker reads (bit 0 SOLID)."""
    cells = bytearray(64 * 64)
    for i in range(64):
        cells[i] = cells[63 * 64 + i] = cells[i * 64] = cells[i * 64 + 63] = 1
    for x, y in walls:
        cells[y * 64 + x] = 1
    return cells


def host_crossings():
    """The three DDA rows' crossing counts, cast through tools/pxssim.py on
    the bench's own map and eye: (10-row, 20-row, near-axial row)."""
    fan = pxssim.FANS[64]
    out = []
    for walls, head in ((WALL10,), HEAD45), ((WALL20,), HEAD45), (WALLA, HEADA):
        a = (head + fan[COL]) & (pxstab.ANG - 1)
        r = pxssim.cast_ray(bench_map(*walls), EYEX, EYEY, a)
        out.append((r["crossings"], r["side"], r["cell"]))
    return out


def open_bench(m, mo, S):
    """B: and PXSBENCH.O88 through tests/pxslib.py - its open_b (the B:
    icon's double-click retried once) and its watched launch (clicked again
    only when neither a window appeared nor the floppy controller read a
    sector). The first cut hand-rolled both and died in wave 6's
    verification soak on the B: icon's double-click ("the two presses were
    10 ticks apart and the window is 9"), which pxslib had already survived
    since wave 5 (wave 6's close)."""
    pxslib.open_b(m, mo, S)
    disk = dispcp.win_list(m, S)[-1]
    bx, by = dispcp.win_rect(m, S, disk)[:2]
    row = dispcp.scroll_to(m, mo, S, os88marty.settle, bx, by,
                           dispcp.row_of(m, S, "PXSBENCH.O88"))
    rx, ry = dispcp.row_xy(bx, by, row)
    got = pxslib.launch_row(m, mo, S, rx, ry, "Pixelstein Bench")
    if got is None:
        sys.exit("pxsbench: PXSBENCH.O88 did not open")
    return got[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/bench360.img")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--shot", help="write the report window's picture here")
    ap.add_argument("--label", default="")
    a = ap.parse_args()
    os.chdir(ROOT)
    S = os88sym.linear

    syms, image = pkg_syms("tests/pxsbench/pxsbench.asm", ("apps/", "tests/"))
    try:
        built = open(os88build.at("build/pxsbench.bin"), "rb").read()
    except OSError:
        sys.exit("pxsbench: no build/pxsbench.bin - run `make bench`")
    if built != image:
        sys.exit("pxsbench: build/pxsbench.bin is %d bytes and the source "
                 "assembles to %d - the disk is BEHIND THE TREE. Run "
                 "`make bench`." % (len(built), len(image)))

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        seg = open_bench(m, mo, S)
        base = seg << 4
        os88marty.settle(m)
        mo.to(4, 4)                 # the pointer parked off the window: the
        os88marty.settle(m)         # bracket's exit repaint and the blit row
                                    # would otherwise carry the arrow

        def rw(name):
            return u16(m.read(base + syms[name], 2))

        m.type_text("r")
        # THE WAIT IS ON THE GUEST'S CLOCK. The run is seconds of 8088 time -
        # the two method-T rows alone are several - and a host-timed wait
        # allows a different amount of it on every box.
        os88marty.until(m, lambda mm: rw("pb_done") >= 1,
                        "the bench run to finish", poll=0.5, limit=900.0,
                        guest=240.0)
        res = m.read(base + syms["pb_res"], NRES * 4)
        flags = m.read(base + syms["pb_resf"], NRES)
        fsxm = m.read(base + syms["pb_fsxm"], 1)[0]
        vkind = m.read(base + syms["pb_vkind"], 1)[0]
        side10 = m.read(base + syms["pb_side10"], 1)[0]
        side20 = m.read(base + syms["pb_side20"], 1)[0]
        ddab = rw("pb_ddabytes")
        hith = rw("pb_hith")
        genb = rw("pb_genend") - 0x4200         # PB_GENOFS
        devoff = m.read(base + syms["pb_devoff"], ROWS * 2)
        devoff1 = m.read(base + syms["pb_devoff1"], ROWS * 2)
        if a.shot:
            os.makedirs(os.path.dirname(a.shot) or ".", exist_ok=True)
            os88marty.settle(m)
            m.pause()
            w, h, px = m.fbuf(0)
            os88marty.write_png_rgb(a.shot, w, h, px)
            m.run()

    us = {}
    for i in range(NRES):
        v = int.from_bytes(res[i * 4:i * 4 + 4], "little") / 100.0   # us / iteration
        us[i] = (v, chr(flags[i]) if flags[i] else "?")
    kind = {0: "VGA", 1: "HERC", 2: "CGA", 3: "EGA"}.get(vkind, str(vkind))
    mode = {2: "CGA320", 4: "HERC", 8: "MODEX"}.get(fsxm, str(fsxm))

    print()
    if a.label:
        print("   arm: %s" % a.label)
    print("   machine %s: adapter %s, bracket mode %s, DDA body %d bytes"
          % (a.machine, kind, mode, ddab))
    print("   %-24s %4s %10s %11s   %s" % ("row", "flag", "us/iter", "cycles",
                                          "per unit"))
    print("   " + "-" * 70)
    for i, label, unit, uname in TABLE:
        v, f = us[i]
        cyc = v * HZ / 1e6
        if f == "-":
            print("   %-24s %4s %10s %11s   (skipped)" % (label, f, "-", "-"))
            continue
        print("   %-24s %4s %10.2f %11.0f   %8.1f cyc / %s"
              % (label, f, v, cyc, cyc / unit, uname))
    print()

    # --- the derived units, which are what the plan priced as D ------------
    d10, d20, d10a, scaf = us[3][0], us[4][0], us[5][0], us[18][0]
    # THE DIVISORS ARE CAST, NOT ASSUMED: the host walks the bench's own map
    # and eye through tools/pxssim.py (the same walker 97.2.2 pins) and the
    # two 45-degree rows must come to exactly 10 and 20 crossings, else the
    # headline crossing figure is (d20 - d10) / something-else. The near-
    # axial row takes the sim's count as its divisor.
    (n10, s10, c10), (n20, s20, c20), (na, sa, ca) = host_crossings()
    cross = (d20 - d10) / 10.0
    # THE SETUP IS REPORTED NET OF THE HARNESS: pb_b_ddasc runs the push/pop
    # bp, the DS switch and the row's own call that a DDA row carries and the
    # package's column driver does not (it holds DS and BP for the frame),
    # and nothing else. What is left is 97.2.1's setup - the angle, the
    # quadrant, the two px_tan reads, the four patches, the two muls, the
    # pointers and keys - which is what wave 1 shapes the driver around.
    setup = d10 - 10 * cross - scaf
    # ...and the hit row carries the same scaffold (push/pop bp, the DS
    # switch, the call), so 97.2.5's figure is net of it too
    hit = us[21][0] - scaf
    print("   DERIVED (the units SPEC.md 97.1 carries as M):")
    print("     store, RAM             %6.1f cyc   (plan D: 25)"
          % (us[0][0] * HZ / 1e6 / ROWS))
    if us[12][1] not in "-x":
        print("     store, VRAM (%s)     %6.1f cyc   (plan D: 31)"
              % (mode, us[12][0] * HZ / 1e6 / ROWS))
    print("     word store, RAM        %6.1f cyc   (plan D: ~29; Resolution Low)"
          % (us[16][0] * HZ / 1e6 / ROWS))
    if us[17][1] not in "-x":
        print("     word store, VRAM       %6.1f cyc" % (us[17][0] * HZ / 1e6 / ROWS))
    print("     ladder row, entered    %6.1f cyc   (plan D: ~27)"
          % (us[1][0] * HZ / 1e6 / ROWS))
    print("     DDA crossing, 45 deg   %6.1f cyc   (plan D: 130, anchor 155)"
          % (cross * HZ / 1e6))
    print("     DDA column setup       %6.1f cyc   (97.2.1 whole: fan, quadrant, "
          "2 tan, 4 patches, 2 mul, keys; NET of the scaffold's %.0f; hit "
          "side 10:%d 20:%d; plan D: 516)"
          % (setup * HZ / 1e6, scaf * HZ / 1e6, side10, side20))
    print("     DDA crossing, axial    %6.1f cyc   (the same walker nine of %d; "
          "the sim's count)" % ((d10a - setup - scaf) / na * HZ / 1e6, na))
    if us[21][1] not in "-x":
        print("     hit, 97.2.5 whole      %6.1f cyc   (NET of the scaffold; h = %d "
              "rows; two MUL14, the clamp, the div, sctab, top/bot, seven "
              "stores; first take D: 630)" % (hit * HZ / 1e6, hith))
    print("     texel load + store     %6.1f cyc   (plan D: 23 + 25)"
          % (us[6][0] * HZ / 1e6 / ROWS))
    print("     ...with an xlat        %6.1f cyc   (+%.1f a texel)"
          % (us[7][0] * HZ / 1e6 / ROWS, (us[7][0] - us[6][0]) * HZ / 1e6 / ROWS))
    print("     ...with ror al,1 x2    %6.1f cyc   (+%.1f a texel: CGA4's dither "
          "phase, N = 2; a rotate is %.1f)"
          % (us[20][0] * HZ / 1e6 / ROWS, (us[20][0] - us[6][0]) * HZ / 1e6 / ROWS,
             (us[20][0] - us[6][0]) * HZ / 1e6 / ROWS / 2))
    print("     ...with ror al,cl (3)  %6.1f cyc   (+%.1f a texel: the Hercules/WIN1 "
          "phase, one instruction; D 20)"
          % (us[22][0] * HZ / 1e6 / ROWS, (us[22][0] - us[6][0]) * HZ / 1e6 / ROWS))
    print("     ...dual-phase word load%6.1f cyc   (+%.1f a texel: the named fallback "
          "from the rotate, part 3 doubled)"
          % (us[19][0] * HZ / 1e6 / ROWS, (us[19][0] - us[6][0]) * HZ / 1e6 / ROWS))
    # THE LOW RES ROW: load + `mov ah, al` + the word store. The duplication
    # is what is left after the word store's own delta over the byte store
    # (rows 16 and 0) comes out, and it is charged again for the odd phase
    lowtex = us[23][0] * HZ / 1e6 / ROWS
    dup = lowtex - us[6][0] * HZ / 1e6 / ROWS - (us[16][0] - us[0][0]) * HZ / 1e6 / ROWS
    print("     Low res texel row      %6.1f cyc   (load + mov ah,al + word store; "
          "the mov ah,al is %.1f)" % (lowtex, dup))
    # THE SIM TERM, s, at the plan's caps and at E1M1's own counts: the one
    # frame term the fixed point gears (dF/ds = F / (T - s)), which was DOT
    # DELIRIUM's five-dot 6.8 ms until this row
    if us[24][1] not in "-x":
        print("     SIM tick, the caps     %6.2f ms    (%.0f clk: 32 actors, 64 doors, "
              "16 movers, 12 tile walks; DOT DELIRIUM's five-dot s was 6.8)"
              % (us[24][0] / 1000.0, us[24][0] * HZ / 1e6))
    if us[25][1] not in "-x":
        print("     SIM tick, E1M1         %6.2f ms    (%.0f clk: 7 actors, 22 doors, "
              "4 movers, 4 walks)" % (us[25][0] / 1000.0, us[25][0] * HZ / 1e6))
    print("     KEY_DOWN               %6.1f us    (plan D: ~232 us)"
          % (us[8][0] / 8.0))
    print("     BLIT1 512x80           %6.2f ms    (%.2f us a band byte over "
          "%d)" % (us[9][0] / 1000.0, us[9][0] / 5120.0, 5120))
    if us[13][1] not in "-x":
        print("     span copy to VRAM      %6.1f cyc / byte  (plan D: 18.6)"
              % (us[13][0] * HZ / 1e6 / 5120))
    if us[14][1] not in "-x":
        print("     C160 expand            %6.1f cyc / byte  (plan D: 48)"
              % (us[14][0] * HZ / 1e6 / 3840))
        print("     store, attribute stride%6.1f cyc" % (us[15][0] * HZ / 1e6 / ROWS))
    print("     scaler-set generation  %6.0f ms    (%d bytes, 43 scalers; plan D: ~150)"
          % (us[10][0] / 1000.0, genb))
    print("     bt_build transpose     %6.0f ms    (30,720 texels, %.1f cyc a "
          "texel, both nibbles a load; plan D: ~300)"
          % (us[11][0] / 1000.0, us[11][0] * HZ / 1e6 / 30720))
    print()

    # --- the one assertion: every row this adapter owes produced a number ---
    for i, label, unit, uname in TABLE:
        v, f = us[i]
        if i in CGA_ONLY and vkind != 2:
            check(f == "-", "%s is marked not-measured off a CGA" % label)
            continue
        check(f not in "-x?", "%s ran (flag %r)" % (label, f))
        # 'w' is a LAP - the row was re-run tick-timed and the figure is
        # coarse; '!' is benchlib's "one iteration passed 33 ms", which the
        # 3,840-byte expand legitimately does (34.9 ms M) and the tick check
        # behind it still passed, so it is reported rather than failed.
        check(f != "w", "%s did not lap the PIT (flag %r)" % (label, f))
        if f == "!":
            print("     (%s: an iteration over 33 ms - near the PIT wrap, but "
                  "the tick check passed; the figure stands)" % label)
        if i not in (10, 11):
            check(v > 0, "%s is not zero" % label)
    check(d20 > d10, "twenty crossings cost more than ten (%.1f vs %.1f us)"
          % (d20, d10))
    check(side10 == 0 and side20 == 0,
          "both 45-degree walks hit on the V walker (10:%d 20:%d)" % (side10, side20))
    # THE DIVISOR IS A MEASUREMENT: the host's walker on the bench's map
    check((n10, n20) == (10, 20),
          "the host walker counts the two 45-degree rows at 10 and 20 crossings "
          "(got %d at %s and %d at %s)" % (n10, c10, n20, c20))
    check((s10, s20) == (side10, side20),
          "...and hits on the walker the guest reports (host V=0: %d/%d)" % (s10, s20))
    check(na >= 8, "the near-axial row walks at least eight crossings (host: %d)" % na)
    check(hith == 32 or us[21][1] in "-x",
          "the hit row's h is 32 rows (nx 1,584 at 45 degrees; got %d)" % hith)
    # THE DEVICE ROW TABLE THE COPY WROTE THROUGH: pb_devrows is the shape
    # wave 1's present copies (apps/skies' cs_devrows), and its first cut
    # lost the bank to the `mul` that computes the row - every row landed in
    # bank 0 and the picture was half its height. TWO tables come back: the
    # GAME mode's (pb_devoff1, copied right after the bracket computed it:
    # the two-bank CGA 320x200 arm on a CGA or an EGA, the four-bank
    # Hercules arm with the box at row 60, the linear Mode X arm) and the
    # one the bracket LAST computed (pb_devoff: on a genuine CGA the C160
    # text arm - no bank, 160 bytes a row, the attribute byte - which the
    # retime recomputes IN PLACE; elsewhere the same table). The first cut
    # checked only the second, so the arm the >= 7.0 promise is made on was
    # the one arm never checked. The expectation is keyed on the MODE the
    # guest reports, not the adapter, so an EGA (which takes CGA320) is
    # checked by the arm it ran.
    def want_row(mode, y):
        if mode == 2:                           # CGA320: bank = y & 1, 80 a row
            return (y & 1) * 0x2000 + (y >> 1) * 80
        if mode == 4:                           # HERC: bank = y & 3, 90 a row
            return (y & 3) * 0x2000 + (y >> 2) * 90 + 5 + 60 * 90 // 4
        if mode == 8:                           # Mode X: linear, 80 a plane row
            return y * 80
        if mode == 1:                           # TEXT80 retimed to C160
            return y * 160 + 1
        raise ValueError("no device-row arm for FSXM %d" % mode)

    def check_rows(name, mode, raw):
        got = [u16(raw[i * 2:i * 2 + 2]) for i in range(ROWS)]
        want = [want_row(mode, y) for y in range(ROWS)]
        bad = [y for y in range(ROWS) if got[y] != want[y]]
        check(not bad, "%s banks every view row in %s (%s)"
              % (name, {2: "CGA320", 4: "HERC", 8: "MODEX", 1: "C160"}[mode],
                 "all %d right" % ROWS if not bad else
                 "row %d is 0x%04X, want 0x%04X" % (bad[0], got[bad[0]], want[bad[0]])))
        check(len(set(got)) == ROWS, "...and no two rows share a device offset")
    check_rows("pb_devrows (the game mode)", fsxm, devoff1)
    check_rows("pb_devrows (the last mode)", 1 if vkind == 2 else fsxm, devoff)

    if FAIL:
        print("pxsbench: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pxsbench: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
