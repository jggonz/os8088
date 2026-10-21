#!/usr/bin/env python3
"""tests/lemplaymarty.py - LEMMINGS PLAYED on a 4.77 MHz 8088 (SPEC.md 116.7).

    make lemmingsdisk && python3 tests/lemplaymarty.py
    python3 tests/lemplaymarty.py --machine os8088_xt_hdd   # the CGA arm

THE FINAL PASS'S INSTRUMENT (wave 6). tests/lembenchmarty.py times the RASTER'S
PRIMITIVES in a bench package; this boots the SHIPPED package off
build/lemmings360.img, launches it from the Disk window like a user, plays
levels, and reads the package's own state out of the guest by symbol - so the
figures are the game's and not a bench's:

  * the launch's int 13h CALLS (the level read before the bracket) and the
    in-bracket NEXT-LEVEL read's - a retry from the postview, which re-reads
    the level inside the bracket (SPEC.md 116.4.3.1). MartyPC is
    cycle-accurate and NOT disk-accurate (tools/os88marty.py), so the CALL
    count is exact and the time is not: priced at PERFORMANCE.md's ~400 ms a
    call instead;
  * the preview PICTURE's time: cycles between the first and the last
    `lem_p_mini` row of a preview (80 rows; 64 on a CGA), scaled by n/(n-1);
  * GAME FRAMES A SECOND, quiet (Fun 2, ten lemmings) and busy (Fun 9, eighty
    at release rate 99, measured once most of them are out): `_lem_iter`,
    the game's own iteration counter, over a fixed span of guest cycles;
  * GLASS UPDATES - `lem_r_present` to `lem_r_present`, which is NOT a pass
    of the play loop: a pass that changed nothing returns from
    ovl_tick_draw before the present and is not counted, so one interval can
    span several passes (wave 6's review; the printed labels below still say
    "pass" and SPEC.md 116.7.4 / PERFORMANCE.md Set 161 name the rows
    "between two glass updates") - and on the CGA the same WHILE SCROLLING
    (an arrow held);
  * MODULE CROSSINGS a frame (hits on crt0's `cc_ovthunk`, the one door every
    module-to-resident call goes through) and the COMPOSE a drawn pass costs
    on VGA (first `lem_r_cband` to last `lem_r_cend` of the pass);
  * on xt_vga, what MartyPC's `flicker()` sees over sixty displayed frames
    of busy play - the off-glass share (PERFORMANCE.md Part 3.1).

IT IS NOT A GATE AND IT IS NOT IN THE SUITE, for tests/lembenchmarty.py's
reason: it is minutes of emulator and what it produces is a table for
PERFORMANCE.md Set 161 and SPEC.md 116.7. Breakpoint stops cost host time and
no guest cycles, so counting by breakpoint prices nothing it counts.

THE LEVEL IS CHOSEN BY WRITING `_lem_sel` before Enter, which is the
launcher's own variable and what ovl_launch reads: the selection is not what
is measured, and an arrow typed into a slow guest is the one input this
project has lost silently before (LESSONS.md 14).
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import dispapps                                             # noqa: E402

HZ = 4772727.0
S = os88sym.linear


def lemmap():
    """The package's map, assembled here: dispapps._map refuses a package
    whose file is image + PARTS (it compares the file with the assembly).
    .bss's vstart is the resident image's end, which is where the loader puts
    it; .modc's vstart is 0, the module's own base."""
    td = tempfile.mkdtemp()
    src = os.path.join(td, "l.asm")
    mp = os.path.join(td, "l.map")
    open(src, "w").write(open(os.path.join(ROOT, "apps/lemmings/lemmings.asm"))
                         .read() + "\n[map all %s]\n" % mp)
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", ROOT + "/apps/",
                    "-I", ROOT + "/build/", "-o", os.path.join(td, "l.bin"),
                    src], check=True)
    out = {}
    for line in open(mp):
        p = line.split()
        if len(p) == 3:
            try:
                out[p[2]] = int(p[0], 16)
            except ValueError:
                pass
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    # THE 360KB DISK CARRIES TWO GRAPHIC SETS AND 53 LEVELS (SPEC.md 116.9),
    # and Fun 1 is a Dirt level it does not carry - Enter on it is the
    # missing-files fact, not a launch. Fun 2 (10 lemmings) and Fun 9 (80 at
    # release rate 99) are both Squasher/Pillar levels it does.
    ap.add_argument("--quiet", type=int, default=1, help="Fun level index of "
                    "the quiet level (1 = Fun 2, 10 lemmings)")
    ap.add_argument("--busy", type=int, default=8, help="Fun level index of "
                    "the busy level (8 = Fun 9, 80 lemmings at rate 99)")
    a = ap.parse_args()
    vga = "vga" in a.machine
    sym = lemmap()
    # the SHIPPED 360KB image, copied: a played level writes LEMMINGS.SAV
    # and the build's own image must not change under the next reader
    import shutil
    shutil.copyfile(os.path.join(ROOT, "build", "lemmings360.img"),
                    os.path.join(ROOT, "build", "lemmarty360.img"))
    image = "build/lemmarty360.img"
    rep = {"machine": a.machine}
    with os88marty.launch("build/os8088-360.img", apps=image,
                          machine=a.machine, label="lemplay") as m:
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        w = dispcp.win_list(m, S)
        wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "LEMMINGS")
        w = dispcp.win_list(m, S)
        wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "LEMMINGS.O88")
        os88marty.settle(m)
        pk = dispapps.pkg_seg(m, 0)
        assert pk, "LEMMINGS did not launch"
        base = pk[1] * 16

        def rd(name, n=2):
            return int.from_bytes(m.read(base + sym[name], n), "little")

        def wr(name, v):
            m.write(base + sym[name], struct.pack("<H", v))

        def cyc():
            return m.status()["cycles"]

        assert rd("_lem_screen") == 0, "the launcher is not on its list"
        modseg = rd("cc_ovm_ovl_tick_draw" if "cc_ovm_ovl_tick_draw" in sym
                    else "cc_ovm_ovl_play_loop", 4) >> 16
        P_MINI = base + sym["_lem_p_mini"]
        PRESENT = base + sym["_lem_r_present"]
        THUNK = base + sym["cc_ovthunk"]
        CBAND = base + sym["_lem_r_cband"]
        CEND = base + sym["_lem_r_cend"]

        def pc():
            r = m.regs()
            return r["cs"] * 16 + r["ip"]

        def stop(limit=120):
            m.run()
            return m.wait_stop(limit)

        def until_picture(what):
            """int 13h calls until the first picture row, then the picture."""
            m.breakpoints([{"type": "int", "addr": 0x13},
                           {"type": "exec", "addr": P_MINI}])
            calls = 0
            while True:
                assert stop(300) == "breakpoint", what + ": no breakpoint"
                if pc() == P_MINI:
                    break
                calls += 1
            c0 = cyc()
            rows, last = 1, c0
            m.bp_exec(P_MINI)
            while rows < 200:
                st = stop(5)
                if st != "breakpoint":
                    break
                rows += 1
                last = cyc()
            m.bp_exec()
            if m.stopped():
                m.run()
            t = (last - c0) / HZ * rows / max(1, rows - 1)
            return calls, rows, t

        def go_play():
            m.key("Enter")
            # the level runs when the game iteration counter moves
            it0 = rd("_lem_iter")
            for _ in range(240):
                m.advance(frames=30)
                if rd("_lem_iter") != it0:
                    break
            m.run()

        def window(secs):
            it0, c0 = rd("_lem_iter"), cyc()
            m.advance(cycles=int(secs * HZ))
            it1, c1 = rd("_lem_iter"), cyc()
            m.run()
            s = (c1 - c0) / HZ
            return (it1 - it0) / s

        def passes(n=24):
            m.bp_exec(PRESENT)
            assert stop() == "breakpoint"
            c0 = cyc()
            for _ in range(n):
                assert stop() == "breakpoint"
            c1 = cyc()
            m.bp_exec()
            m.run()
            return (c1 - c0) / HZ / n

        def crossings(frames=3):
            """cc_ovthunk hits a game frame, and on VGA the compose a pass."""
            m.bp_exec(PRESENT, THUNK, CBAND, CEND)
            while True:                      # anchor on a pass boundary
                assert stop() == "breakpoint"
                if pc() == PRESENT:
                    break
            it0 = rd("_lem_iter")
            thunks = 0
            comp, first, lastend, cpass = [], None, None, 0
            npass = 0
            while rd("_lem_iter") - it0 < frames and npass < 400:
                assert stop() == "breakpoint"
                p = pc()
                if p == THUNK:
                    thunks += 1
                elif p == CBAND:
                    if first is None:
                        first = cyc()
                elif p == CEND:
                    lastend = cyc()
                elif p == PRESENT:
                    npass += 1
                    if first is not None and lastend is not None:
                        comp.append((lastend - first) / HZ)
                    first = lastend = None
            m.bp_exec()
            m.run()
            fr = max(1, rd("_lem_iter") - it0)
            return thunks / float(fr), comp, npass, fr

        # --- the quiet level ----------------------------------------------
        wr("_lem_sel", a.quiet)
        m.key("Enter")
        calls, rows, pic = until_picture("the launch")
        rep["launch int13 calls"] = calls
        rep["picture rows"] = rows
        rep["picture ms"] = round(pic * 1000)
        print("launch: %d int 13h calls before the preview's picture; the "
              "picture %d rows in %.0f ms" % (calls, rows, pic * 1000))
        m.advance(frames=60)
        go_play()
        m.advance(cycles=int(15 * HZ))       # the ten out of the trap door
        m.run()
        fq = window(20)
        pq = passes()
        xq, cq, nq, fr = crossings()
        rep["quiet fps"] = round(fq, 2)
        rep["quiet pass ms"] = round(pq * 1000, 1)
        rep["quiet crossings/frame"] = round(xq)
        print("quiet (released %d): %.2f game frames/s, a pass %.1f ms, %d "
              "module crossings a frame, compose a pass %s ms over %d passes"
              % (rd("_lem_released"), fq, pq * 1000, xq,
                 [round(x * 1000, 1) for x in cq][:12], nq))
        rep["quiet compose ms"] = [round(x * 1000, 1) for x in cq]
        rep["quiet released"] = rd("_lem_released")

        # --- the in-bracket next-level read: Esc, then retry -------------
        m.key("Escape")
        m.advance(frames=240)
        m.run()
        m.key("Enter")                        # the postview's retry
        calls, rows, pic2 = until_picture("the retry")
        rep["retry int13 calls"] = calls
        rep["retry picture ms"] = round(pic2 * 1000)
        print("retry inside the bracket: %d int 13h calls; picture %d rows "
              "in %.0f ms" % (calls, rows, pic2 * 1000))
        m.advance(frames=60)
        m.key("Escape")                       # the preview's Esc: the menu
        m.advance(frames=300)
        m.run()                               # advance() ends STOPPED
        os88marty.settle(m)

        # --- the busy level ----------------------------------------------
        wr("_lem_sel", a.busy)
        m.key("Enter")
        calls, rows, pic3 = until_picture("the busy launch")
        print("busy launch: %d int 13h calls; picture %d rows in %.0f ms"
              % (calls, rows, pic3 * 1000))
        rep["busy picture ms"] = round(pic3 * 1000)
        m.advance(frames=60)
        go_play()
        for _ in range(60):
            m.advance(cycles=int(10 * HZ))
            if rd("_lem_released") >= 60:
                break
        m.run()
        rel = rd("_lem_released")
        fb = window(20)
        pb = passes()
        xb, cb, nb, frb = crossings()
        rep["busy released"] = rel
        rep["busy fps"] = round(fb, 2)
        rep["busy pass ms"] = round(pb * 1000, 1)
        rep["busy crossings/frame"] = round(xb)
        rep["busy compose ms"] = [round(x * 1000, 1) for x in cb]
        print("busy (released %d): %.2f game frames/s, a pass %.1f ms, %d "
              "module crossings a frame, compose a pass %s ms over %d passes "
              "and %d frames" % (rel, fb, pb * 1000, xb,
                                 [round(x * 1000, 1) for x in cb][:16], nb,
                                 frb))
        if vga:
            m.pause()
            # 240 displayed frames, ~3.4 s: a busy VGA frame is ~1.6 s, so
            # sixty frames can fall wholly inside one compose and see nothing
            r = m.flicker(frames=240)
            pf = r.get("per_frame", [])
            rep["flicker"] = {
                "frames": r.get("frames"), "flash_frames": r.get("flash_frames"),
                "moved_frames": r.get("moved_frames"),
                "frames changed": sum(1 for f in pf if f.get("changed")),
                "frames with transient px": sum(1 for f in pf
                                                if f.get("transient")),
                "transient px": sum(f.get("transient", 0) for f in pf),
                "changed px": sum(f.get("changed", 0) for f in pf)}
            print("flicker over 240 displayed frames of busy play: %r"
                  % (rep["flicker"],))
            m.run()
        else:
            # the CGA's pass WHILE SCROLLING: an arrow held
            m.key("ArrowLeft", down=True, up=False)
            m.advance(frames=30)
            m.run()
            ps = passes(16)
            m.key("ArrowLeft", down=False, up=True)
            rep["scrolling pass ms"] = round(ps * 1000, 1)
            print("a pass while scrolling: %.1f ms" % (ps * 1000))
    out = os.path.join(ROOT, "build", "lemplay-%s.txt" % a.machine)
    open(out, "w").write(repr(rep) + "\n")
    print(rep)
    print("wrote", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
