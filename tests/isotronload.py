#!/usr/bin/env python3
"""isoTron's launch to play, and the game's rate and load on the XT (PERFORMANCE.md Set 159).

    make isotrondisk
    python3 tests/isotronload.py [--tag cga|herc|vga|all] [--launch] [--rate] [--json FILE]

Two measurements, MartyPC's cycle counter throughout (4,772,727 a guest second):

LAUNCH. The double-clicked ISOTRON.O88 walked by breakpoints, each a stop that
costs the guest no cycle (tests/excitebike_load.py's recipe): the loader's
entry (`ld_run_body_x`), the image read and expanded (`ld_start`), the
package's entry proc and its three builds (`it_lut_build`, `it_title_build`,
`it_box_clear`), the worker's first wake, and the title's whole present out
(`it_fdone`) - the title is drawn and takes keys. Then the `1` key a player
presses: the first arena built OUTSIDE the lock (`it_bld0`/`it_bld1`), the
sweep (`it_sw0`/`it_sw1`), READY's half second (`it_rdy1`) and the first wall
moving (`it_step0`). No harness flag is set on this leg: it is the launch a
player gets.

RATE. A harness BOT round in play at Rookie h1 (p = 2, the footage's pace)
and at Lunatic h1 with p poked to 9 (the worst rate anywhere, SPEC.md
108.4.1), windowed and in the full-screen bracket: every head-step
(`it_step0`) and every computer head-step (`it_hs0`) counted between two
frame ends over up to two guest seconds, against SPEC.md 108.4.1's model;
each step's own price (`it_step0 .. it_step1`: the move and its picture,
with the bracket's span hook inside it); and the share of the machine the
game's frames hold - windowed the worker's lock holds (`it_wake0 ..
it_wake1`: the iterations AND the present), in the bracket each frame's work
and sound (`it_fsf0 .. it_fssnd1`). The window stops at the first frame
whose head count or speed moved, so every count is of one steady state.

The rows ASSERT only that the title comes up, the round starts, and the
measured rates sit within 3% of the model (Set 157 holds the pace to 2% on
its own row); the figures are the record.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M                               # noqa: E402
import os88ui                                       # noqa: E402
import isotronlib as L                              # noqa: E402
import isotronsim as S                              # noqa: E402
import isotron as T                                 # noqa: E402

HZ = M.GUEST_HZ
TAGS = ("cga", "herc", "vga")
# the entry proc's builds: code labels, read off the source like the marks
L.MARKS = L.MARKS + ("it_entry", "it_lut_build", "it_title_build", "it_box_clear",
                     "it_paint_win.wp")
SEED = 2425                                         # isotronperf's Rookie game


def ms(c):
    return 1000.0 * c / HZ


# =============================================================================
# LAUNCH
# =============================================================================

def launch_leg(tag):
    system, apps, machine = T.boot_for(tag)
    _, code = L.syms()
    with os88ui.boot(system, apps=apps, machine=machine) as ui:
        m = ui.m
        ui.open_drive("B")
        ui.settle()
        m.bp_exec("ld_run_body_x")
        ui.open(L.FILE, expect=None)
        assert m.wait_stop(60) == "breakpoint", "the loader was never entered"
        c0 = int(m.status()["cycles"])
        m.disk(reset=True)
        m.bp_exec("ld_start")
        m.run()
        assert m.wait_stop(60) == "breakpoint", "ld_start was never reached"
        c_ld = int(m.status()["cycles"])
        base = m.regs()["es"] << 4          # the image is expanded at offset 0
        d_ld = m.disk()
        m.disk(reset=True)
        tl = [("loader entry (the double-click taken)", c0),
              ("image read + lz4-expanded, bss zeroed (ld_start)", c_ld)]
        # the package's own milestones, first hit of each, to the title's
        # whole present out
        # (the title's first whole present is the window's first W_PAINT,
        # itwin.inc it_paint_win's .wp; a banded one ends at it_fdone)
        names = ("it_entry", "it_lut_build", "it_title_build", "it_rb0", "it_rb1",
                 "it_box_clear", "it_wake0", "it_paint_win.wp", "it_fdone")
        what = {"it_entry": "package entry proc",
                "it_lut_build": "window + claims made; presenters' tables",
                "it_title_build": "title's canvas: the grid cleared",
                "it_rb0": "...the slab and its rims (it_rebuild)",
                "it_rb1": "...the word, 79 cells of 4 x 4 floor cells",
                "it_box_clear": "title built (entry proc returns)",
                "it_wake0": "the worker's first wake",
                "it_paint_win.wp": "TITLE UP: its whole present out (W_PAINT), taking keys",
                "it_fdone": "TITLE UP: its whole present out (bands), taking keys"}
        flat = {base + code[n]: n for n in names}
        seen = set()
        m.bp_exec(*flat.keys())
        m.run()
        while True:
            assert m.wait_stop(120) == "breakpoint", tl
            rg = m.regs()
            n = flat.get((rg["cs"] << 4) + rg["ip"])
            if n and n not in seen:
                seen.add(n)
                tl.append((what[n], int(m.status()["cycles"])))
                if n in ("it_fdone", "it_paint_win.wp"):
                    break
                left = [a for a, k in flat.items() if k not in seen]
                m.bp_exec(*left)
            m.run()
        d1 = m.disk()
        m.disk(reset=True)
        m.bp_exec()
        m.run()
        g = L.Game(ui)
        M.until(m, lambda _: g.b("it_spawned") == 1 and not g.owed(), "the title", poll=0.1,
                guest=30)
        assert g.b("it_state") == L.ST_TITLE
        # --- the `1` key: the first round, as a player starts it ----------------
        # (measured from the key's arrival at the package, it_onkey: the wait
        # between the title coming up and the harness pressing is the host's)
        names2 = ("it_onkey", "it_bld0", "it_bld1", "it_sw0", "it_sw1", "it_rdy1", "it_step0")
        what2 = {"it_onkey": "the `1` key reaches the package (W_ONKEY)",
                 "it_bld0": "arena build starts (worker, outside the lock)",
                 "it_bld1": "arena built",
                 "it_sw0": "sweep starts",
                 "it_sw1": "sweep done: slab, tower and rims on the glass",
                 "it_rdy1": "READY's half second over",
                 "it_step0": "PLAY: the first wall moves"}
        flat2 = {base + code[n]: n for n in names2}
        seen = set()
        m.bp_exec(*flat2.keys())
        m.key("Digit1", up=False)
        M.pace(m, .04)
        m.key("Digit1", down=False)
        c_key = None
        c_up = tl[-1][1]
        tl.append(("-- the `1` key pressed: from its arrival --", None))
        while True:
            assert m.wait_stop(120) == "breakpoint", tl
            rg = m.regs()
            n = flat2.get((rg["cs"] << 4) + rg["ip"])
            if n and n not in seen:
                seen.add(n)
                c = int(m.status()["cycles"])
                if c_key is None:
                    assert n == "it_onkey", "%s before the key reached the package" % n
                    c_key = c
                # phase 2 is placed as if the key arrived the instant the
                # title came up: launch to play is then the two phases' sum
                tl.append((what2[n], c - c_key + c_up))
                if n == "it_step0":
                    break
                left = [a for a, k in flat2.items() if k not in seen]
                m.bp_exec(*left)
            m.run()
        d2 = m.disk()
        m.bp_exec()
        m.run()
    return dict(tl=tl, disk=dict(load=d_ld, title=d1, play=d2))


def print_launch(tag, r):
    tl = r["tl"]
    print("%s: launch (seconds on the 4.77 MHz machine since the loader's entry; step in "
          "brackets)" % tag)
    prev = None
    for name, c in tl:
        if c is None:
            print("   " + name)
            continue
        print("   %-58s %6.3f s%s" % (name, (c - tl[0][1]) / HZ,
                                     "" if prev is None else "  (%6.3f)" % ((c - prev) / HZ)))
        prev = c
    for n, dd in (("loader entry -> ld_start", r["disk"]["load"]),
                  ("ld_start -> title up", r["disk"]["title"]),
                  ("`1` -> play", r["disk"]["play"])):
        print("   floppy, %-26s %3d sectors in %2d reads, transfer %5.0f ms, seek %4.0f ms"
              % (n, dd["read_sectors"], dd["reads"], dd["transfer_ms"], dd["seek_ms"]))
    sys.stdout.flush()


# =============================================================================
# RATE
# =============================================================================

CASES = (("Rookie h1, bot's p", 1, None), ("Lunatic h1, p 9", 3, 9))
AFTER = 5                                           # iterations in before the window
SOUND = 1                                           # it_snd: audible, as a player has it


def rate_case(g, fs, name, tier, poke_p, secs=2.0):
    """One steady state. The RATE trace stops at the frame marks ALONE and
    reads the counters there (`it_iter`, `it_nsteps`): every live computer
    head steps once an iteration, so its steps are iterations x heads and the
    player's the rest - a stop at every step as well would be ~640 a guest
    second at the worst rate, and the window is cleaner without them. The
    STEP PRICE is a second, short trace of `it_step0 .. it_step1` after it."""
    m = g.m
    B = g.base
    F0 = B + g.code["it_fsf0" if fs else "it_wake0"]
    F1 = B + g.code["it_fssnd1" if fs else "it_wake1"]
    S0, S1 = B + g.code["it_step0"], B + g.code["it_step1"]
    st_a, lv_a, ps_a = g.a("it_state"), g.a("it_live"), g.a("it_pspeed")
    it_a, ns_a = g.a("it_iter"), g.a("it_nsteps")
    T.start_game(g, SEED, tier, L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
    if fs:
        M.until(m, lambda _: g.b("it_state") == L.ST_PLAY and not g.fs_owed()
                and g.w("it_iter") >= AFTER, "play", poll=0.05, guest=30)
    else:
        M.until(m, lambda _: g.b("it_state") == L.ST_PLAY and not g.b("it_vsync")
                and g.w("it_iter") >= AFTER, "play", poll=0.05, guest=30)
    m.pause()
    if poke_p:
        g.put("it_pspeed", poke_p)
    g.put("it_snd", SOUND)              # audible: the frame's sound is in the hold
    p_want = poke_p or g.b("it_pspeed")
    m.run()

    def onhit(mm, rec):
        if rec["addr"] == F1:
            w = lambda a: int.from_bytes(mm.read(a, 2), "little")   # noqa: E731
            return (mm.read(st_a, 1)[0], mm.read(lv_a, 1)[0], mm.read(ps_a, 1)[0],
                    w(it_a), w(ns_a))
        return None

    def enough(tr):
        ends = [h for h in tr.hits if h["addr"] == F1]
        if not ends:
            return False
        if ends[-1]["hit"][0] != L.ST_PLAY:
            return True
        return ends[-1]["cycles"] - ends[0]["cycles"] >= (secs + 0.1) * HZ
    with M.bp_trace(m, F0, F1, cap=4000, on_hit=onhit) as tr:
        tr.until(lambda: enough(tr), "%s: %.1f guest seconds" % (name, secs), limit=600)
    assert not tr.overflowed, "%s: the trace overflowed" % name
    hs = tr.hits
    ends = [i for i, h in enumerate(hs) if h["addr"] == F1]
    # the steady state: from the first frame end whose speed is p_want, while
    # the state, the live count and the speed all hold
    i0 = next(i for i in ends if hs[i]["hit"][2] == p_want and hs[i]["hit"][0] == L.ST_PLAY)
    live = hs[i0]["hit"][1]
    i1 = i0
    for i in ends:
        if i <= i0:
            continue
        if hs[i]["hit"][:3] != (L.ST_PLAY, live, p_want):
            break
        i1 = i
        if hs[i]["cycles"] - hs[i0]["cycles"] >= secs * HZ:
            break
    c0, c1 = hs[i0]["cycles"], hs[i1]["cycles"]
    span = c1 - c0
    assert span >= 0.4 * HZ, "%s: only %.2f guest s of steady play (live %d)" \
        % (name, span / HZ, live)
    its = (hs[i1]["hit"][3] - hs[i0]["hit"][3]) & 0xFFFF
    steps = (hs[i1]["hit"][4] - hs[i0]["hit"][4]) & 0xFFFF
    comp = its * live
    holds = [c for c, _ in T.pairs(hs[i0:i1 + 1], F0, F1)]
    # --- each step's own price, a short trace after the window --------------
    with M.bp_trace(m, S0, S1, cap=2000) as tr2:
        tr2.until(lambda: len(T.pairs(tr2.hits, S0, S1)) >= 300 or
                  g.b("it_state") != L.ST_PLAY, "%s: steps" % name, limit=300)
    stepc = [c for c, _ in T.pairs(tr2.hits, S0, S1)] or [0]   # (the round over: none)
    st = T.stats(stepc)
    hold = T.stats(holds)
    gs = span / HZ
    m_its, prate, hsrate = S.Pace.rates(tier, live, p_want)
    r = dict(case=name, mode="fs" if fs else "win", live=live, p=p_want, secs=gs,
             its=its, steps=steps, comp=comp, player=steps - comp,
             steps_s=steps / gs, comp_s=comp / gs, player_s=(steps - comp) / gs,
             model_steps_s=hsrate, model_comp_s=m_its * live, model_player_s=prate,
             frames_s=len(holds) / gs, busy=sum(holds) / span,
             step_med=st["med"], step_p90=st["p90"], step_max=st["max"], step_n=st["n"],
             hold_med_ms=ms(hold["med"]), hold_p90_ms=ms(hold["p90"]),
             hold_max_ms=ms(hold["max"]))
    r["err"] = (r["steps_s"] - hsrate) / hsrate
    print("  %-4s %-17s %d live, p %d, %.2f guest s: %.1f head-steps/s (computer %.1f a head "
          "x %d, player %.1f) against the model's %.1f (%+.1f%%); a step (move + picture) "
          "median %d cycles, p90 %d (n %d); %.1f frames/s, the hold median %.1f ms, p90 %.1f, "
          "most %.1f; the game holds %.1f%% of the machine"
          % ("fs" if fs else "win", name, live, p_want, gs, r["steps_s"], r["comp_s"] / live,
             live, r["player_s"], hsrate, 100 * r["err"], r["step_med"], r["step_p90"],
             r["step_n"], r["frames_s"], r["hold_med_ms"], r["hold_p90_ms"], r["hold_max_ms"],
             100 * r["busy"]), flush=True)
    T.to_title(g)
    return r


def rate_leg(tag):
    system, apps, machine = T.boot_for(tag)
    out = []
    with os88ui.boot(system, apps=apps, machine=machine) as ui:
        g = L.open_game(ui)
        M.until(ui.m, lambda _: g.b("it_spawned") == 1, "the worker to start", poll=0.2,
                guest=20)
        T.presented(ui, g)
        for fs in (False, True):
            if fs:
                T.fs_enter(ui, g, "F")
            for name, tier, p in CASES:
                out.append(rate_case(g, fs, name, tier, p))
        T.fs_leave(ui, g, "esc")
    bad = [r for r in out if abs(r["err"]) > 0.03]
    assert not bad, "%s: a rate off the model by more than 3%%: %s" % (tag, bad)
    return out


# =============================================================================
# PROFILE (an instrument for the audit, not a row): where the guest's time
# goes in a Lunatic h1 p 9 round, by MartyPC's flat IP sampled from the host
# (tools/os88prof.py's method - uncorrelated with the guest, so a share of
# the samples is a share of the cycles), bucketed by the package's labels out
# of a listing of the -DIT_MARKS assembly (the shipped image, isotronlib
# checks) and by the kernel's .text symbols
# =============================================================================

def _pkg_labels():
    import re
    import subprocess
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        lst = os.path.join(td, "i.lst")
        cmd = ["nasm", "-f", "bin", "-w+error", "-DIT_MARKS"]
        for i in L.INCS:
            cmd += ["-I", i]
        subprocess.run(cmd + ["-o", os.path.join(td, "i.bin"), "-l", lst, L.SRC], check=True,
                       cwd=ROOT)
        out, pending, last = [], [], None
        for ln in open(lst, encoding="latin-1"):
            mm = re.match(r"\s*\d+\s+([0-9A-F]{8})\s", ln)
            addr = int(mm.group(1), 16) if mm else None
            lab = re.search(r"^\s*\d+\s+(?:[0-9A-F]{8}\s+\S+\s+)?(?:<\d+>\s*)?"
                            r"(\.?[A-Za-z_][A-Za-z0-9_.]*):", ln)
            if lab:
                n = lab.group(1)
                if n.startswith("."):
                    n = (last or "?") + n
                else:
                    last = n
                if addr is not None:
                    out.append((addr, n))
                else:
                    pending.append(n)
                continue
            if addr is not None and pending:
                out += [(addr, n) for n in pending]
                pending = []
    out.sort()
    return out


def _kern_labels():
    import os88sym
    sy, sec = os88sym.syms(), os88sym.sections()
    return sorted((0x600 + o, n) for n, o in sy.items() if sec.get(n) == ".text")


def _bucket(labs, off):
    import bisect
    k = bisect.bisect_right(labs, (off, "￿")) - 1
    return labs[k][1] if k >= 0 else "?"


def profile_leg(tag, fs, samples=20000, fine=False):
    import time
    system, apps, machine = T.boot_for(tag)
    plabs, klabs = _pkg_labels(), _kern_labels()
    if not fine:        # whole routines: drop the local labels
        plabs = [(a, n) for a, n in plabs if "." not in n]
        klabs = [(a, n) for a, n in klabs if "." not in n]
    hits, tot = {}, 0
    with os88ui.boot(system, apps=apps, machine=machine) as ui:
        g = L.open_game(ui)
        m = ui.m
        M.until(m, lambda _: g.b("it_spawned") == 1, "the worker to start", poll=0.2, guest=20)
        T.presented(ui, g)
        if fs:
            T.fs_enter(ui, g, "F")
        while tot < samples:
            T.start_game(g, SEED, 3, L.TF_BOT | L.TF_NOFLOW | L.TF_HOLD)
            M.until(m, lambda _: g.b("it_state") == L.ST_PLAY and g.w("it_iter") >= 5,
                    "play", poll=0.05, guest=30)
            m.pause()
            g.put("it_pspeed", 9)
            g.put("it_snd", 1)
            m.run()
            st = g.a("it_state")
            n = 0
            while tot < samples:
                ip = m.status()["flat_ip"]
                n += 1
                if n % 200 == 0 and (m.read(st, 1)[0] != L.ST_PLAY):
                    break
                tot += 1
                if g.base <= ip < g.base + 0x10000:
                    b = "pkg " + _bucket(plabs, ip - g.base)
                elif 0x600 <= ip < 0x10600:
                    b = "kern " + _bucket(klabs, ip)
                elif ip >= 0xF0000:
                    b = "ROM"
                else:
                    b = "other %05X" % (ip & 0xFFF00)
                hits[b] = hits.get(b, 0) + 1
            T.to_title(g)
        if fs:
            T.fs_leave(ui, g, "esc")
    print("%s %s: %d samples, Lunatic h1 p 9 in play" % (tag, "fs" if fs else "win", tot))
    for b, n in sorted(hits.items(), key=lambda x: -x[1])[:40]:
        print("   %5.1f%%  %s" % (100.0 * n / tot, b))
    sys.stdout.flush()
    return hits


def main():
    global AFTER, SOUND
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--tag", default="all", choices=TAGS + ("all",))
    ap.add_argument("--launch", action="store_true", help="the launch leg alone")
    ap.add_argument("--rate", action="store_true", help="the rate leg alone")
    ap.add_argument("--json", metavar="FILE", help="keep the numbers")
    ap.add_argument("--profile", metavar="MODE", choices=("win", "fs"),
                    help="sample where a Lunatic h1 p 9 round's time goes (an instrument)")
    ap.add_argument("--fine", action="store_true", help="--profile: by local label too")
    ap.add_argument("--samples", type=int, default=20000)
    ap.add_argument("--after", type=int, default=AFTER,
                    help="--rate: iterations into the round before the window")
    ap.add_argument("--silent", action="store_true", help="--rate: it_snd off (the M key)")
    a = ap.parse_args()
    AFTER = a.after
    SOUND = 0 if a.silent else 1
    if a.profile:
        for tag in (TAGS if a.tag == "all" else (a.tag,)):
            profile_leg(tag, a.profile == "fs", a.samples, a.fine)
        return 0
    both = not (a.launch or a.rate)
    res = {}
    for tag in (TAGS if a.tag == "all" else (a.tag,)):
        if a.launch or both:
            r = launch_leg(tag)
            print_launch(tag, r)
            res["launch_" + tag] = r
        if a.rate or both:
            print("%s: the rate" % tag, flush=True)
            res["rate_" + tag] = rate_leg(tag)
    if a.json:
        json.dump(res, open(a.json, "w"), indent=1)
    print("isotronload: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
