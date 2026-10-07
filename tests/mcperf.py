#!/usr/bin/env python3
"""MISSILE, THE SAME GAME TWICE: what a frame costs, deterministically.

    make && make mcbench && python3 tests/mcperf.py

PERFORMANCE.md Set 141.3 priced the walk conversion (SPEC.md 5.12.5) at −33.5%
median on `mc_dsc_run` over LIVE PLAY — two trees playing two different games,
medians over whatever the waves happened to do. That is enough to see a sign
and not enough to trust a size; Set 141.2 had to measure blocks-per-call
separately precisely because it varies. `apps/missile/mcbench.inc` runs ONE
game: a fixed seed, scripted shots off the frame counter, and MC_BFRAMES
frames back to back rather than one a tick.

**BACK TO BACK IS THE LOAD-BEARING PART.** `mc_worker` sleeps to a deadline, so
a faster frame does not make the game go faster — it makes the worker sleep
longer, and the whole win is invisible in wall time. Worse, a frame that
overruns a tick takes `.behind` and re-anchors, which is clock-dependent: a
fast arm and a slow arm would stop playing the same game at the first overrun.

WHAT THIS ROW ASSERTS, which is not the speed. The bench is run TWICE in one
boot and the two runs must end in the IDENTICAL game state — `mc_bsum` walks
every object's position and mode and every counter, leaving out the walk blocks
and drawn-to positions on purpose, those being the implementation the two arms
represent differently. That gates DETERMINISM rather than a magic constant, and
determinism is the property a cross-tree comparison rests on: without it the
cycle figures below are two numbers rather than a measurement.

The cycle figures are printed for the reader and for the report. The BAR is a
loose ceiling — a frame that has doubled is a regression worth a red row, and
a MartyPC cycle count is exact enough to say so without flapping.

WHAT IT WOULD CATCH, verified red: a bench that does not run (frames 0); a game
whose state stops being reproducible (the two checksums part); and a frame cost
that runs away.

FOR THE OTHER ARM, run this from inside a checkout of the older tree — the
bench is one self-contained include plus two `%ifdef` hooks in missile.asm, so
it ports to a tree that predates the conversion.
"""
import argparse
import os
import subprocess
import sys
import hashlib
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, os88geom, os88build, dispcp   # noqa: E402
from cycweb import Pkg, u16, shot                               # noqa: E402

HZ = 4772727.0                  # the 5150's 8088, PERFORMANCE.md Part 2
FRAMES = 400                    # MC_BFRAMES
BAR_MS = 120.0                  # a frame this slow is a runaway, not a cost
FAIL = []


def check(ok, what):
    print("   %-58s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def pkg_syms(src, incs, defs):
    """tests/radio.py's, plus -D: a package's symbols are not in os88sym's
    kernel map, so the source is re-assembled with `[map symbols]`."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        bp = os.path.join(d, "p.bin")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        cmd = ["nasm", "-f", "bin", "-w+error"]
        for i in incs:
            cmd += ["-I", i]
        for x in defs:
            cmd += ["-D" + x]
        cmd += ["-o", bp, cp]
        subprocess.run(cmd, check=True)
        syms = {}
        for ln in open(mp):
            f = ln.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                syms[f[2]] = int(f[0], 16)
        return syms, open(bp, "rb").read()


def stats(v):
    v = sorted(v)
    n = len(v)
    return (v[n // 2], sum(v) / float(n), v[0], v[-1])


# ONE SYSTEM TICK IN CPU CYCLES. The PIT free-runs at 1.193182 MHz / 65536 =
# 18.2065 Hz, and mc_worker's whole design is one frame inside one of these
# (SPEC.md 44.1): a frame that crosses it does not make the game sag, it HALVES
# the rate. So "did this frame miss" is the question a distribution has to
# answer, and a median cannot.
TICK = HZ / 18.2065


def pct(v, q):
    """The q'th percentile, nearest-rank. A MAX is one sample - the worst of
    several hundred - so a tail that is genuinely fatter and a tail with one
    unlucky call in it look identical in it. These do not."""
    v = sorted(v)
    if not v:
        return 0
    i = min(len(v) - 1, max(0, int(round(q / 100.0 * len(v))) - 1))
    return v[i]


def tail(v, label):
    m = pct(v, 50)
    print("     %-14s p50 %8d  p90 %8d  p95 %8d  p99 %8d  max %8d"
          % (label, m, pct(v, 90), pct(v, 95), pct(v, 99), max(v) if v else 0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/mcbench360.img")
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--runs", type=int, default=2,
                    help="bench runs in one boot; two is what gates "
                         "determinism, more only sharpens the spread")
    ap.add_argument("--label", default="", help="a name for the arm, for a "
                                                "cross-tree comparison")
    ap.add_argument("--fire", type=int, default=0,
                    help="MC_BFIRE: a scripted shot every N frames. Must "
                         "match the MCBFIRE= the disk was built with - the "
                         "staleness check below compares the two")
    ap.add_argument("--drnbud", type=int, default=0,
                    help="MC_DRNBUD the disk was built with (MCDRNBUD=), so "
                         "the staleness check matches")
    ap.add_argument("--dump", default="",
                    help="write run 0's per-frame cycles to this file, one a "
                         "line. BOTH ARMS PLAY THE SAME GAME (mc_bsum proves "
                         "it), so frame N in one is frame N in the other and "
                         "the two dumps compare PAIRWISE - which is a far "
                         "sharper question than two distributions")
    ap.add_argument("--pts", action="store_true",
                    help="bracket the KERNEL's gfx_points through one "
                         "deterministic run and read the arrays it was "
                         "handed: its real arrival/marginal split, and how "
                         "SPATIALLY COHERENT the points are - which is what "
                         "decides whether caching a row or a byte would pay")
    ap.add_argument("--dsc", action="store_true",
                    help="also bracket mc_dsc_run - THE CONVERTED CALL - "
                         "through one deterministic run, so its share of a "
                         "frame is measured rather than assumed")
    a = ap.parse_args()
    S = os88sym.linear

    defs = ["MC_BENCH"] + (["MC_BFIRE=%d" % a.fire] if a.fire else []) \
        + (["MC_DRNBUD=%d" % a.drnbud] if a.drnbud else [])
    syms, image = pkg_syms("apps/missile/missile.asm",
                           ("apps/", "apps/missile/"), defs)
    try:
        built = open(os88build.at("build/mcbench.bin"), "rb").read()
    except OSError:
        sys.exit("mcperf: no build/mcbench.bin - run `make mcbench`")
    if built != image:
        sys.exit("mcperf: build/mcbench.bin is %d bytes and the source "
                 "assembles to %d - the disk is BEHIND THE TREE. "
                 "Run `make mcbench`." % (len(built), len(image)))

    # AND THE SHIPPED PACKAGE MUST NOT HAVE MOVED. The whole claim of a knob
    # instrument is that it is not in the product; a hook that leaked out of
    # its %ifdef would make every figure below a measurement of something
    # nobody runs.
    plain, _ = None, None
    with tempfile.TemporaryDirectory() as d:
        bp = os.path.join(d, "plain.bin")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/missile/", "-o", bp,
                        "apps/missile/missile.asm"], check=True)
        plain = open(bp, "rb").read()
    shipped = open(os88build.at("build/missile.bin"), "rb").read()
    check(plain == shipped,
          "MISSILE.O88 is byte-identical without -DMC_BENCH (%d vs %d)"
          % (len(plain), len(shipped)))

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        rx, ry = dispcp.row_xy(bx, by,
                               dispcp.scroll_to(m, mo, S, os88marty.settle,
                                                bx, by,
                                                dispcp.row_of(m, S,
                                                              "MCBENCH.O88")))
        mo.dblclick(rx, ry)
        def missile():
            for w in os88geom.windows(m, S):
                if w.title.startswith("Missile"):
                    return u16(m.read(os88geom.winptr(m, w.i, S)
                                      + os88geom.W_SEG, 2))
            return None
        try:
            os88marty.until(m, lambda _: missile(), "MCBENCH's window",
                            poll=0.3, limit=180)
        except os88marty.MartyError:
            pass                            # the check below says so
        seg = missile()
        if not seg:
            sys.exit("mcperf: MCBENCH did not launch")
        p = Pkg(m, seg, syms)
        # THE POINTER OFF THE PLAYFIELD AND LEFT THERE. mc_do_cross follows the
        # mouse every frame in every mode, so a pointer that moved between the
        # two runs would move the crosshair and the runs would not be the same
        # game - which is the one nondeterminism the seed cannot cover.
        mo.to(4, 4)
        os88marty.settle(m)

        base = seg << 4
        shot_hash = None
        TOP, END = base + syms["mc_b_top"], base + syms["mc_b_end"]
        out, dsc, ptr = [], None, None
        for run in range(a.runs):
            # THE REQUEST IS POKED, NOT TYPED. `m.key` put the game through
            # TWO bench runs a press - one event per make and break reaching
            # mc_onkey - and because a run resets [mc_bfr] and [mc_bck] at its
            # top, the harness then read a fresh run's zeros and reported a
            # bench that had in fact run twice. A word written straight into
            # [mc_breq] is one request, and the key stays for a person at a
            # real machine.
            done = p.rw("mc_bdone")
            with os88marty.bp_trace(m, TOP, END, cap=4 * FRAMES + 64) as tr:
                m.write(base + syms["mc_breq"], b"\x0A\x00")   # 10 = nine
                #     ordinary frames of grace, so the arming above
                #     cannot lose the start of the run
                tr.until(lambda: p.rw("mc_bdone") > done,
                         "the %d-frame run to finish" % FRAMES, 900)
            hits = tr.hits
            # A FRAME IS A TOP FOLLOWED BY THE NEXT END, paired in order. The
            # two markers strictly alternate - they are the loop's own top and
            # bottom - so anything else means stops were dropped and the row
            # should say so rather than average over a hole.
            per, i = [], 0
            while i + 1 < len(hits):
                if hits[i]["addr"] == TOP and hits[i + 1]["addr"] == END:
                    per.append(hits[i + 1]["cycles"] - hits[i]["cycles"])
                    i += 2
                else:
                    i += 1
            out.append(dict(per=per, frames=p.rw("mc_bfr"),
                            ticks=p.rw("mc_bticks"), sum=p.rw("mc_bck"),
                            drnf=p.rw("mc_bdrnf"), drnx=p.rw("mc_bdrnx")))

        # --- THE CONVERTED CALL'S OWN SHARE, in the same deterministic run ---
        # PERFORMANCE.md Set 141.3 priced `mc_dsc_run` alone. What a whole
        # frame does with that saving is a different question and the one a
        # player feels, so this measures the bridge between them rather than
        # leaving it to arithmetic: how often the call runs and what fraction
        # of the frame it is. `mc_dsc_run.out` is reached on every path -
        # `jcxz .out` is the routine's only branch - so entry-to-.out is one
        # whole call including the empty ones.
        if a.dsc:
            D0 = base + syms["mc_dsc_run"]
            D1 = base + syms["mc_dsc_run.out"]
            DN = base + syms["mc_dscn"]
            DB = base + syms["mc_dsc"]
            # ...and WHAT each call was handed, read at the entry stop: how
            # many walks, and how many PIXELS between them. Set 141.2 had to
            # measure blocks separately and for the same reason - the
            # conversion's win is not a constant, it is a function of the
            # batch. The pixels are what say WHICH producer filled it: the
            # drain is bounded at MC_DRNBUD across the whole queue and the two
            # trail steps add a missile's speed each, so a big batch is a
            # drain batch and a small one is not.
            def blocks(mm, rec):
                if rec["addr"] != D0:
                    return None
                n = u16(mm.read(DN, 2))
                if not n:
                    return (0, 0)
                raw = mm.read(DB, 4 * n)        # `dw block, pixels` pairs
                px = sum(u16(raw[i * 4 + 2:i * 4 + 4]) for i in range(n))
                return (n, px)
            done = p.rw("mc_bdone")
            with os88marty.bp_trace(m, D0, D1, cap=24 * FRAMES,
                                    on_hit=blocks) as tr:
                m.write(base + syms["mc_breq"], b"\x0A\x00")
                tr.until(lambda: p.rw("mc_bdone") > done,
                         "the mc_dsc_run pass", 1800)
            h, calls, cyc, i, blk, empty = tr.hits, 0, 0, 0, 0, 0
            span = []
            pix = []
            while i + 1 < len(h):
                if h[i]["addr"] == D0 and h[i + 1]["addr"] == D1:
                    d = h[i + 1]["cycles"] - h[i]["cycles"]
                    cyc += d
                    span.append(d)
                    n, px = h[i].get("hit") or (0, 0)
                    blk += n
                    pix.append((px, d))
                    empty += (n == 0)
                    calls += 1
                    i += 2
                else:
                    i += 1
            dsc = (calls, cyc, blk, empty, span, pix)

        # --- the screen the run ended on, hashed ----------------------------
        mo.to(4, 4)                 # the arrow parked: it is the kernel's
        os88marty.settle(m)         # pixels and it moves with the harness
        _w, _h, _px = shot(m)
        shot_hash = hashlib.sha1(bytes(bytearray(_px))).hexdigest()[:16]

        # --- WHAT gfx_points IS ACTUALLY HANDED, in the real caller ---------
        # PERFORMANCE.md Set 140 measured this slot on gfxbench's geometry -
        # eight VERTICAL columns - and a vertical run shares a byte column and
        # never a row, which is the opposite of what a Bresenham trail does.
        # So the cost model, and any optimisation resting on it, want the
        # arrays the GAME hands over rather than a bench's.
        if a.pts:
            P0, P1 = S("gfx_points"), S("gfx_points.out")
            arrays = []

            def grab(mm, rec):
                if rec["addr"] != P0:
                    return None
                r = rec["regs"]
                n = r["cx"]
                if not n or len(arrays) >= 200:
                    return n            # cycles from every call, geometry
                k = min(n, 128)         # from a sample: the read is not free
                raw = mm.read((r["es"] << 4) + r["si"], k * 4)
                arrays.append([(u16(raw[j * 4:j * 4 + 2]),
                                u16(raw[j * 4 + 2:j * 4 + 4]))
                               for j in range(k)])
                return n

            done = p.rw("mc_bdone")
            with os88marty.bp_trace(m, P0, P1, regs=True, cap=8000,
                                    on_hit=grab) as tr:
                m.write(base + syms["mc_breq"], b"\x0A\x00")
                tr.until(lambda: p.rw("mc_bdone") > done,
                         "the gfx_points pass", 2400)
            h, i2, per = tr.hits, 0, []
            while i2 + 1 < len(h):
                if h[i2]["addr"] == P0 and h[i2 + 1]["addr"] == P1:
                    n = h[i2].get("hit") or 0
                    if n:
                        per.append((n, h[i2 + 1]["cycles"] - h[i2]["cycles"],
                                    (h[i2 + 1].get("instructions") or 0)
                                    - (h[i2].get("instructions") or 0)))
                    i2 += 2
                else:
                    i2 += 1
            ptr = (per, arrays)


    print()
    if a.label:
        print("   arm: %s" % a.label)
    print("   %-5s %7s %7s %8s %10s %10s %9s"
          % ("run", "frames", "ticks", "state", "median cyc", "mean cyc",
             "med ms"))
    print("   " + "-" * 64)
    for i, r in enumerate(out):
        if not r["per"]:
            sys.exit("mcperf: run %d paired no frames - the bench did not "
                     "run, so nothing here was measured" % i)
        med, mean, lo, hi = stats(r["per"])
        print("   %-5d %7d %7d %8s %10.0f %10.0f %9.2f"
              % (i, r["frames"], r["ticks"], "%04X" % r["sum"], med, mean,
                 1000.0 * med / HZ))
    print()
    med, mean, lo, hi = stats(out[0]["per"])
    print("   frame spread run 0: min %d, median %d, max %d cycles "
          "(%.2f - %.2f - %.2f ms)"
          % (lo, med, hi, 1000.0 * lo / HZ, 1000.0 * med / HZ,
             1000.0 * hi / HZ))
    tot = sum(out[0]["per"])
    print("   whole run: %d frames, %d cycles, %.2f guest seconds"
          % (len(out[0]["per"]), tot, tot / HZ))
    print()
    print("   drain: %d of %d frames had smoke still clearing (%.1f%%), "
          "deepest queue %d"
          % (out[0]["drnf"], FRAMES, 100.0 * out[0]["drnf"] / FRAMES,
             out[0]["drnx"]))
    print()
    tail(out[0]["per"], "frame cycles")
    # ...AND THE ONLY THRESHOLD THE GAME HAS. Everything else here is a cost;
    # this is a FAILURE: mc_worker's deadline is one tick, and a frame that
    # crosses it halves the frame rate rather than shaving it (SPEC.md 44.1).
    for k in (1, 2, 3, 5):
        n = sum(1 for d in out[0]["per"] if d > k * TICK)
        print("     frames over %d tick%s (%6.1f ms): %3d of %d  (%.1f%%)"
              % (k, " " if k == 1 else "s", k * 1000.0 * TICK / HZ, n,
                 len(out[0]["per"]), 100.0 * n / len(out[0]["per"])))
    print()

    for i, r in enumerate(out):
        check(r["frames"] == FRAMES,
              "run %d ran %d frames (counter reads %d)"
              % (i, FRAMES, r["frames"]))
        check(len(r["per"]) == FRAMES,
              "run %d paired %d of %d frames" % (i, len(r["per"]), FRAMES))
    if len(out) > 1:
        same = all(r["sum"] == out[0]["sum"] for r in out)
        check(same, "every run ends in the IDENTICAL game state (%s)"
              % ", ".join("%04X" % r["sum"] for r in out))
    check(out[0]["sum"] != 0,
          "the state checksum is not zero (%04X) - a zero would mean the "
          "table read nothing" % out[0]["sum"])
    check(1000.0 * med / HZ < BAR_MS,
          "the median frame is under %.0f ms (%.2f)" % (BAR_MS,
                                                        1000.0 * med / HZ))

    if shot_hash:
        # THE PIXEL-IDENTITY GATE, and it is nearly free. 400 deterministic
        # frames of a game end in one screen; anything that changes what a
        # drawing primitive PUTS DOWN changes this hash, and nothing that only
        # changes how long it took does. It is what a rewrite of gfx_points or
        # of any slot Missile draws through is checked against - the timings
        # are expected to move and the picture is not.
        print("   screen after the run: %s" % shot_hash)
        print()

    if ptr:
        per, arrays = ptr
        npts = sum(n for n, d, q in per)
        cyc = sum(d for n, d, q in per)
        ins = sum(q for n, d, q in per)
        print()
        print("   gfx_points, AS THE GAME CALLS IT")
        print("     %d non-empty calls, %d points (%.1f a call), %d cycles"
              % (len(per), npts, npts / float(len(per)) if per else 0, cyc))
        if len(per) > 8:                # the arrival/marginal split, least
            n_ = len(per)               # squares over (points, cycles)
            sx = sum(n for n, d, q in per)
            sxx = sum(n * n for n, d, q in per)
            den = n_ * sxx - sx * sx
            fit = {}
            for k, lbl in ((1, "cycles"), (2, "instructions")):
                sy = sum(r[k] for r in per)
                sxy = sum(r[0] * r[k] for r in per)
                if den:
                    mrg = (n_ * sxy - sx * sy) / float(den)
                    fit[lbl] = (mrg, (sy - mrg * sx) / float(n_))
            if "cycles" in fit:
                mrg, arr = fit["cycles"]
                print("     fitted: %.0f cycles ARRIVAL + %.0f a POINT "
                      "(%.1f us + %.1f us)"
                      % (arr, mrg, 1e6 * arr / HZ, 1e6 * mrg / HZ))
            if "instructions" in fit:
                im, ia = fit["instructions"]
                print("     ...and %.0f instructions arrival + %.1f a point "
                      "-> %.2f CYCLES AN INSTRUCTION in the loop"
                      % (ia, im, fit["cycles"][0] / im if im else 0))
                print("     (an 8088 averages 4-6 on ordinary register code; "
                      "much above that is memory, not instructions)")
        if arrays:
            pairs = same_row = same_byte = 0
            runs = []
            for ar in arrays:
                run = 1
                for k in range(1, len(ar)):
                    pairs += 1
                    if ar[k][1] == ar[k - 1][1]:
                        same_row += 1
                        if ar[k][0] >> 3 == ar[k - 1][0] >> 3:
                            same_byte += 1
                            run += 1
                            continue
                    runs.append(run); run = 1
                runs.append(run)
            print("     geometry, %d sampled calls, %d consecutive pairs:"
                  % (len(arrays), pairs))
            print("       same ROW  as the point before: %5.1f%%"
                  % (100.0 * same_row / pairs if pairs else 0))
            print("       same BYTE as the point before: %5.1f%%"
                  % (100.0 * same_byte / pairs if pairs else 0))
            print("       points per framebuffer byte touched: %.2f"
                  % (sum(runs) / float(len(runs)) if runs else 0))
            # THE SAMPLES ARE THE POINT, not decoration: the two percentages
            # above say the points are incoherent and only these say WHY -
            # `297,50 297,51 296,52 296,53` is a Y-MAJOR line, which is what a
            # falling missile is. y moves every point and x every second or
            # third, so a row cache never hits and a byte cache never hits,
            # and both are the obvious optimisation.
            for ar in arrays[:4]:
                print("       sample: " + " ".join("%d,%d" % q for q in ar[:14])
                      + (" ..." if len(ar) > 14 else ""))
        print()


    if a.dump:
        with open(a.dump, "w") as f:
            for d in out[0]["per"]:
                f.write("%d\n" % d)
        print("   per-frame cycles written to %s" % a.dump)
        print()

    if dsc is not None:
        calls, cyc, blk, empty, span, pix = dsc
        live = calls - empty
        print("   mc_dsc_run - THE CONVERTED CALL (SPEC.md 5.12.5)")
        print("     %d calls over %d frames (%.2f a frame), of which %d were "
              "EMPTY" % (calls, FRAMES, calls / float(FRAMES), empty))
        print("     %d walks stepped: %.2f a call, %.2f a NON-EMPTY call "
              "(Set 141.2 read 3.64 live)"
              % (blk, blk / float(calls) if calls else 0,
                 blk / float(live) if live else 0))
        print("     %d cycles - %.2f%% of the run's %d; %.0f a call, %.0f a "
              "non-empty call"
              % (cyc, 100.0 * cyc / tot, tot,
                 cyc / float(calls) if calls else 0,
                 cyc / float(live) if live else 0))
        # THE MEDIAN AS WELL AS THE MEAN, and over the NON-EMPTY calls, because
        # PERFORMANCE.md Set 141.3 quotes a median and comparing one statistic
        # with the other is how a figure gets refuted by accident. The empty
        # calls are a third of them here and cost nothing, so a median over all
        # of them would be a different number again.
        ne = sorted(d for d in span if d > 2000)
        if ne:
            m2, a2, l2, h2 = stats(ne)
            print("     non-empty calls: median %d, mean %d, min %d, max %d"
                  % (m2, a2, l2, h2))
            tail(ne, "non-empty")
        # COST AGAINST PIXELS, which is what decides whether a re-tune has a
        # knob to turn. A drain batch is up to MC_DRNBUD pixels; a trail step
        # is a missile's speed. If the arms only part at the big end, the
        # budget is the lever; if they part everywhere, it is not.
        buckets = ((1, 8), (9, 16), (17, 32), (33, 64), (65, 128), (129, 9999))
        print("     pixels in the batch -> calls, median cycles, cycles/pixel")
        for lo_, hi_ in buckets:
            v = [d for px, d in pix if lo_ <= px <= hi_]
            if not v:
                continue
            q = [px for px, d in pix if lo_ <= px <= hi_]
            m3 = pct(v, 50)
            print("       %4d-%-5s  %4d calls   median %7d   %6.0f cyc/px"
                  % (lo_, hi_ if hi_ < 9999 else "up", len(v), m3,
                     m3 / (sum(q) / float(len(q))) if q else 0))
        print()

    if FAIL:
        print("mcperf: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("mcperf: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
