#!/usr/bin/env python3
"""What ch_xthunk costs SHEET on real work, in guest cycles (SPEC.md 82.16.11).

    make && python3 tests/sheetxbench.py
    python3 tests/sheetxbench.py --pkg DIR --src DIR/apps    # another build

NO-PINNING P1b put every call a code module makes back into SHEET's resident
code through ch_xthunk, priced at ~400 clocks a crossing over the old
`call / retf` shim. Those crossings sit on formula and drawing paths, so the
price has to be MEASURED on the work a user does, not counted. This row does
four pieces of it on a 4.77MHz 5150 (MartyPC, guest cycles - host load cannot
move them) and prints each figure, so the same command run against two
builds is the A/B:

  recalc   F9 (Calculate Now) - `sh_drawall`, every visible formula
           recomputed and the grid drawn;
  save     Ctrl+S - `sh_recalc_all`, every formula off the screen recomputed
           before the writer reads it (the write itself is not in it);
  chart    Data > Chart Column... again over a live chart - `sh_chart_render`,
           CHART.OVL's draw (a changed value re-renders only an UNOBSCURED
           chart, and on 640x200 SHEET and its chart always overlap);
  macro    Macro > Run of a 40-turn FOR loop - `sh_m2call`, MACRO.OVL's run.
           Not its caller sh_macro_onalert: bracketed there, the new build
           read 2.3M cycles against the base's 115M for runs that computed the
           same total, a bracket artefact not chased here; sh_m2call reads
           both builds alike, run after run.

Each is bracketed EXACTLY: a breakpoint on the routine's entry, then one on
the return address read off the stack at that entry (and the same SP), and
the guest cycle counter read at both. Interrupts that land inside are in the
figure, which is the honest number for a user and the same on both builds.

The sheet is formula-heavy on purpose - LN/SIN/SQRT, PMT/FV and LEN/MID/UPPER
are all module functions, so a cell is several crossings - with one column of
resident-only arithmetic beside them. It asserts only that every bracket
closed, that every formula SHEET saved is the host's value (a recalc that
computed nothing would be cheap on both builds), and that the loop's total
in K2 is the host's after every run; the cycles are REPORTED, not gated.

MEASURED (SPEC.md 82.16.11), base no-pinning 1b933ed4 -> P1b, medians of 3:
recalc +0.47%, save +0.73%, chart +0.01%, macro +0.70%.
--pkg/--src run it against another build of SHEET (sheet.o88 in DIR - with
CHART.OVL and MACRO.OVL beside it in a tree before SPEC.md 82.16.12 - and the
apps/ tree its symbols come from).
"""
import argparse
import math
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import cmemmove                                         # noqa: E402
import dispcp                                           # noqa: E402
import os88fixture                                      # noqa: E402
import os88flush                                        # noqa: E402
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sheetfmt as F                                # noqa: E402
import os88sym                                          # noqa: E402
from sheetmove import pkg_syms                          # noqa: E402

import os88build                                        # noqa: E402

# BOTH resolved through the run's tree (os88build.at): in a soak lane the
# emulator stages `build/...` out of the frozen tree, so a scratch disk written
# relative to the checkout is a file that does not exist where it is read
WORK = os88build.at("build/sheetxbench")
NAME = "BENCH.SLK"
ROWS = 48                           # 60 overruns the SYLK staging buffer: the
                                    # save is then truncated (sh_trunc) and
                                    # its last rows cannot be checked
u16 = cmemmove.u16
LOOP = ['FOR(K1,1,40)', 'SET.VALUE(K2,K2+LEN("abcdef")+SQRT(K1))', 'NEXT()',
        'RETURN()']


def cells():
    c = {}
    for r in range(ROWS):
        a = "A%d" % (r + 1)
        c[(r, 0)] = float(r + 1)
        c[(r, 1)] = ('formula', 'LN(%s)+SIN(%s)+SQRT(%s)' % (a, a, a), 0.0)
        c[(r, 2)] = ('formula', 'PMT(0.01,%s,-1000)' % a, 0.0)
        c[(r, 3)] = ('formula', 'LEN(MID("abcdefghijklmnopqrst",1,%s))'
                     '+LEN(UPPER("ab"))' % a, 0.0)
        c[(r, 4)] = ('formula', '%s*2+B%d-C%d/7' % (a, r + 1, r + 1), 0.0)
        c[(r, 5)] = ('formula', 'FV(0.05,%s,-100)' % a, 0.0)
    for r, f in enumerate(LOOP):
        c[(r, 9)] = ('formula', f, 0.0)
    return c


def host(r):
    n = r + 1
    pmt = 1000 * 0.01 / (1 - 1.01 ** -n)
    return {1: math.log(n) + math.sin(n) + math.sqrt(n), 2: pmt,
            3: min(n, 20) + 2, 5: 100 * (1.05 ** n - 1) / 0.05}


def cellval(m, seg, SH, row, col):
    """A number out of SHEET's own cell store (sheet 0), or None."""
    import struct
    cs = u16(m.read(seg * 16 + SH["sh_cellseg"], 2))
    n = u16(m.read(seg * 16 + SH["sh_ncells"], 2))
    raw = m.read(cs * 16, n * 20)
    for i in range(n):
        r, c = u16(raw, i * 20), u16(raw, i * 20 + 2)
        if r == row and c == col and raw[i * 20 + 6] == 1:
            return struct.unpack_from("<d", raw, i * 20 + 8)[0]
    return None


def build_disk(pkg):
    os.makedirs(WORK, exist_ok=True)
    body = F.write_sylk(cells()).decode('latin-1').split('\r\n')
    body[1:1] = ['NN;NLOOP;ER1C10']
    src = os.path.join(WORK, NAME)
    open(src, "wb").write('\r\n'.join(body).encode('latin-1'))
    disk = os.path.join(WORK, "bench.img")
    # CHART.OVL and MACRO.OVL ride along only from a tree that still has them:
    # since SPEC.md 82.16.12 both modules are parts of SHEET.O88, and an A/B
    # against a commit before that needs its two files beside it
    tree = os88build.at(pkg)
    files = [os.path.join(tree, n) for n in ("sheet.o88", "CHART.OVL",
                                             "MACRO.OVL")]
    files = [f for f in files if f.endswith(".o88") or os.path.exists(f)]
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk, "--size",
                    "360"] + files + [src], check=True,
                   stdout=subprocess.DEVNULL)
    return disk


COUNTS = {}


def bracket(m, seg, off, fire, what, count=()):
    """Guest cycles from the routine's entry to its return, one invocation.

    `count` are (name, region offset) pairs whose entries inside the bracket
    are counted into COUNTS[what]: a diagnostic (SXB_COUNT="sym sym"), off by
    default, because every hit is a host round trip."""
    m.bp_exec(seg * 16 + off)
    fire()
    for _ in range(20):
        if m.wait_stop(120) is None:
            raise RuntimeError("%s: never entered" % what)
        r = m.regs()
        if r["cs"] == seg and r["ip"] == off:
            break
        m.go()
    c0 = int(m.status()["cycles"])
    sp = r["sp"]
    ret = u16(m.read(r["ss"] * 16 + sp, 2))
    m.bp_exec(seg * 16 + ret, *[seg * 16 + o for _, o in count])
    names = dict((o, n) for n, o in count)
    tally = COUNTS.setdefault(what, {}) if count else {}
    m.go()
    for _ in range(100000):
        if m.wait_stop(600) is None:
            raise RuntimeError("%s: never returned" % what)
        r2 = m.regs()
        if r2["cs"] == seg and r2["ip"] == ret and r2["sp"] == sp + 2:
            break
        if r2["cs"] == seg and r2["ip"] in names:
            tally[names[r2["ip"]]] = tally.get(names[r2["ip"]], 0) + 1
        m.go()
    c1 = int(m.status()["cycles"])
    m.bp_exec()
    m.go()
    return c1 - c0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--pkg", default="build")
    ap.add_argument("--src", default="apps")
    ap.add_argument("--reps", type=int, default=3)
    a = ap.parse_args()
    os.chdir(os.path.join(HERE, ".."))
    os88fixture.need("build/os8088-360.img")
    disk = build_disk(a.pkg)
    SH = pkg_syms(os.path.join(a.src, "sheet", "sheet.asm"), incs=(a.src + "/",))
    S = os88sym.linear
    got = {}
    with os88marty.launch("build/os8088-360.img", apps=disk,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.kernel_up(m)
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        ds = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, ds)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, name=NAME)
        os88marty.settle(m, limit=240)
        seg, sw = cmemmove.pkg_seg(m, S, "Sheet")
        x0, y0 = sw.x, sw.y
        print("SHEET at (%d,%d) %dx%d" % (sw.x, sw.y, sw.w, sw.h))
        # the bar's titles, off the CGA screen itself (this face is not
        # sh_mtab_calc's 8 x len + 16 on this adapter): Data and Macro's
        # centres sit 255 and 380 right of the frame
        X = {"Data": x0 + 255, "Macro": x0 + 380}
        bar = y0 + 18 + 7
        item = lambda t, i: (X[t] + 6, y0 + 18 + 14 + 2 + 12 * i + 6)
        park = (sw.x + sw.w - 30, sw.y + sw.h - 30)

        def ctrl(k):
            m.key("ControlLeft", up=False)
            m.key(k)
            m.key("ControlLeft", down=False)

        for i in range(a.reps):
            got.setdefault("recalc", []).append(
                bracket(m, seg, SH["sh_drawall"], lambda: m.key("F9"),
                        "recalc"))
            os88marty.settle(m)
            got.setdefault("save", []).append(
                bracket(m, seg, SH["sh_recalc_all"], lambda: ctrl("KeyS"),
                        "save"))
            os88marty.settle(m, limit=240)

        # THE CHART: Data > Chart Column... over column A makes the window,
        # and the same command again re-renders it - sh_docmd_chart calls
        # sh_chart_render whatever covers the window, where a changed value
        # re-renders only an UNOBSCURED chart, and on a 640x200 screen SHEET
        # and its chart cannot both be clear of each other
        def pick(t, i, settle=None):
            mo.menu(X[t], bar, 0, 0, settle=settle,
                    aim=lambda _: (os88marty.settle(m), item(t, i))[1])

        def pick_hit(t, i):              # the release runs the command and
            try:                         # the breakpoint stops the guest
                pick(t, i, settle=0)     # under the gesture's own wait
            except os88marty.MartyError:
                pass

        mo.click(x0 + 1 + 40 + 36, y0 + 18 + 14 + 26 + 7)   # A1
        os88marty.settle(m)
        pick("Data", 10)
        os88marty.until(m, lambda _: u16(m.read(seg * 16 + SH["sh_chartwin"],
                                                2)), "the Chart window",
                        poll=0.5, limit=120)
        os88marty.settle(m)
        for i in range(a.reps):
            mo.click(x0 + sw.w // 2, y0 + 9)                # SHEET forward
            os88marty.settle(m)
            got.setdefault("chart", []).append(
                bracket(m, seg, SH["sh_chart_render"],
                        lambda: pick_hit("Data", 10), "chart"))
            os88marty.settle(m, limit=240)
        mo.click(x0 + sw.w // 2, y0 + 9)
        os88marty.settle(m)

        for i in range(a.reps):
            pick("Macro", 1)
            os88marty.settle(m)
            m.type_text("\b" * 8 + "LOOP")
            got.setdefault("macro", []).append(
                bracket(m, seg, SH["sh_m2call"],
                        lambda: m.key("Enter"), "macro",
                        count=[(n, SH[n]) for n in
                               os.environ.get("SXB_COUNT", "").split()]))
            os88marty.settle(m, limit=240)
            msg = u16(m.read(seg * 16 + SH["sh_msg"], 2))
            got.setdefault("macromsg", []).append(
                "done" if msg == SH["sh_s_macrodone"] else
                [k for k, v in SH.items() if v == msg and k.startswith("sh_s")]
                or "%04x" % msg)
        running = m.read(seg * 16 + SH["sh_macro_running"], 1)[0]
        kv = cellval(m, seg, SH, 1, 10)             # K2, the loop's total
        mo.to(*park)
        vol = os88flush.Flush(marty=m).volume(1)    # what Ctrl+S wrote
        raw = vol.read(NAME) if NAME in vol.names() else b""
        open(os.path.join(WORK, "SAVED.SLK"), "wb").write(raw)
    saved = F.read(NAME, data=raw, kind="sylk") if raw else {}
    wrong = []
    for r in range(ROWS):
        for c, want in host(r).items():
            v = saved.get((r, c))
            v = v[2] if isinstance(v, tuple) and v and v[0] == "formula" else v
            if not isinstance(v, float) or abs(v - want) > 1e-6 * max(1, abs(want)):
                wrong.append(((r, c), v, want))

    bad = 0
    for k in ("recalc", "save", "chart", "macro"):
        v = sorted(got.get(k, []))
        if not v:
            print("%-7s not measured" % k)
            bad += 1
            continue
        print("%-7s %s  median %d cycles = %.1f ms"
              % (k, v, v[len(v) // 2], v[len(v) // 2] / 4772.727))
    print("values  %d of %d formulas the host's%s" % (
        ROWS * 4 - len(wrong), ROWS * 4,
        "" if not wrong else "; first wrong %r" % (wrong[0],)))
    bad += bool(wrong)
    print("macro   ended with %s" % got.get("macromsg"))
    if COUNTS:
        print("counted %r" % COUNTS)
    runs = len(got.get("macromsg", []))
    want = runs * sum(6 + math.sqrt(k) for k in range(1, 41))
    ok = kv is not None and abs(kv - want) < 1e-6 * want
    print("macro   K2 = %r after %d run(s), the host says %r%s"
          % (kv, runs, want, "" if ok else "  <-- WRONG"))
    bad += not ok
    if any(x != "done" for x in got.get("macromsg", [])):
        print("FAIL: a macro run did not end 'Macro done'")
        bad += 1
    if running:
        print("FAIL: the macro was still running when the row ended")
        bad += 1
    print("sheetxbench: %s" % ("FAIL (%d)" % bad if bad else "measured"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
