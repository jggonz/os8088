#!/usr/bin/env python3
"""DOS's trace part MOVES at the prompt, and is pinned while a program runs.

    make && make build/DOSHELLO.COM && python3 tests/dostrmove.py

SPEC.md 96.29.1.2 (docs/plans/NO-PINNING-PLAN.md, item 2 of the follow-up).
The `DOSTRACE` build's ring and rendered dump are one 35KB `OP_ZERO` part of
DOS.O88 - a parts CARVE, held for the window's life, born pinned. It opts
into the parts standard's `OP_ONMOVE` (SPEC.md 20.12.12): `dos_trmove`
re-asks `op_seg` for the two words that name it, `[dos_trseg]` and the core's
`[dos_m33seg]`, and `dos_run` pins it from `.ready` to the end of the dump.

THE DISK IS THIS ROW'S OWN (docs/WRITING-TESTS.md 5.5): the DOSTRACE package
is assembled into build/dostrmove/ and put at the root of a private B: beside
DOSHELLO.COM and Note Pad - nothing in the shared tree is written, where
`tools/os88dosdbg.py build` swaps $(SYSROOT) under `make`.

THE ARRANGEMENT. Regions and the carve are claimed TOP-DOWN (SPEC.md
20.12.10.8), so Note Pad opened first takes the ceiling, DOS's region lands
under it and the carve under that. Note Pad closes, the ceiling is a hole,
and a compaction posted from outside (`[mem_cpq]`, pkgmove's door) packs the
ceiling: DOS's region and its carve slide UP.

SIX ASSERTIONS:
  1. the carve is declared movable (MC_RLOC != 0, read from outside);
  2. the posted pass MOVED it;
  3. [dos_trseg] and [dos_m33seg] name the same place in the NEW carve that
     they named in the old one - the hook's whole job;
  4. DOSHELLO.COM typed at the prompt RUNS (its READY on the bracket's text
     screen), and WHILE IT RUNS the carve's MC_RLOC is 0 - pinned;
  5. it exits back to the window with DST_RAN, and the carve is movable again;
  6. the trace it wrote is in the NEW part: the ring at the carve's new base
     holds its INT 21h AH=30h, and TRACE.LOG reached the floppy.

VERIFIED TO FAIL, both halves: `dos_trmove` a bare `ret` -> 3 reads both
words at the OLD base and 6 finds no AH=30h in the new part (the program
traced through the stale segment); the `op_pin` at `dos_run`'s `.ready`
removed -> 4 reads the carve's MC_RLOC non-zero while DOSHELLO runs.
"""
import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]
import dispcp                                           # noqa: E402
import os88dosdbg                                       # noqa: E402
import os88fixture                                      # noqa: E402
import os88flush                                        # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402
import os88ui                                           # noqa: E402

S = os88sym.linear
MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
WORK = os.path.join(ROOT, "build", "dostrmove")
DISK = os.path.join(WORK, "dostr360.img")
HELLO = "build/DOSHELLO.COM"
DST_RAN = 2


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def claims(m):
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    return [tuple(u16(raw, i * MC_SIZE + k) for k in (0, 2, 4, 8))
            for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)]


def build_disk():
    """The DOSTRACE package, privately: os88dosdbg's own assembly, into
    build/dostrmove/, then a B: of DOS.O88 + DOSHELLO.COM + Note Pad."""
    os.makedirs(WORK, exist_ok=True)
    binp = os.path.join(WORK, "dos-trace.bin")
    os88dosdbg.nasm(os88dosdbg.DOS_ASM, binp, defines=("DOSTRACE",),
                    incs=tuple(os.path.join(ROOT, *i.split("/"))
                               for i in os88dosdbg.DOS_INCS))
    pkg = os.path.join(WORK, "DOS.O88")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88pkg.py"),
                    binp, "-o", pkg, "--compress-if=lz4"], check=True,
                   cwd=ROOT, stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", DISK, "--size", "360", pkg, HELLO,
                    "build/notepad.o88"], check=True, cwd=ROOT,
                   stdout=subprocess.DEVNULL)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    os.chdir(ROOT)
    os88fixture.need(HELLO)
    build_disk()
    imgsz = os88dosdbg.package_image_size(DISK)
    B = os88dosdbg.dos_bss(["dos_trseg", "dos_m33seg", "dos_tracen",
                            "dos_tracew", "dos_state", "dos_inbr"])
    K = os88dosdbg.dos_syms(["DOS_TRB_OFF", "DOS_TRACE_SZ", "DOS_TRACEN"])
    bad = 0

    def check(n, ok, what, why=""):
        print("  %s %-34s %s" % (n, what, "OK" if ok else "FAIL %s" % why))
        return 0 if ok else 1

    with os88marty.launch("build/os8088-360.img", apps=DISK,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.kernel_up(m)
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        done = lambda mm, **k: os88marty.ui_done(mm)
        dispcp.open_drive(m, mo, S, done, "B")
        dslot = dispcp.win_list(m, S)[-1]

        def win(slot):
            return [w for w in os88geom.windows(m, S) if w.i == slot][0]

        ui = os88ui.UI(m, mouse=mo)

        def front(slot):
            ui.raise_window(win(slot))       # a point on the glass, proven

        def launch(name):
            front(dslot)
            wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
            before = dispcp.win_list(m, S)
            dispcp.open_named(m, mo, S, done, wx, wy, name)
            os88marty.until(m, lambda _: len(dispcp.win_list(m, S))
                            > len(before), "%s's window" % name, poll=0.3,
                            limit=60)
            os88marty.quiesce(m, lambda: sorted(claims(m)), guest=2.0,
                              what="the claims")
            return [i for i in dispcp.win_list(m, S) if i not in before][0]

        np_slot = launch("NOTEPAD.O88")
        dos_slot = launch("DOS.O88")
        if os.environ.get("DTM_DEBUG"):
            for o in os88geom.windows(m, S):
                print("   win %d %r at (%d,%d) %dx%d vis %s" % (
                    o.i, o.title, o.x, o.y, o.w, o.h, o.visible))
            print("   dock y0 %d" % os88geom.word(m, "vid_dock_y0", S))

        def seg():
            return u16(m.read(os88geom.winptr(m, dos_slot, S)
                              + os88geom.W_SEG, 2))

        def word(name):
            return u16(m.read((seg() << 4) + imgsz + B[name], 2))

        def carve():
            mine = [c for c in claims(m) if c[2] == seg()]
            t = word("dos_trseg")
            hit = [c for c in mine if c[0] <= t < c[0] + c[1]]
            return (hit or mine or [None])[0]

        c0, t0, k0 = carve(), word("dos_trseg"), word("dos_m33seg")
        if not t0 or c0 is None:
            print("FAIL: no trace part (dos_trseg %04x, claims %r) - the "
                  "OP_OPT part was refused, so nothing here is measured"
                  % (t0, c0))
            return 1
        print("DOS at %04x: carve %04x/%d paras, dos_trseg %04x, m33seg %04x"
              % (seg(), c0[0], c0[1], t0, k0))
        bad += check(1, c0[3] != 0, "the carve is declared movable",
                     "(MC_RLOC 0)")

        # --- Note Pad closes: the ceiling above DOS is a hole --------------
        # DOS's window is the whole width and 179 rows on this CGA - it covers
        # Note Pad's title AND the dock - so it is dragged DOWN first, which
        # uncovers the title. A drag, not the window's own geometry: nothing
        # of DOS's is touched by where its frame sits
        ui.move_window(win(dos_slot), 0, 100)
        front(np_slot)
        ui.close(win(np_slot))
        front(dos_slot)
        wptr = os88geom.winptr(m, dos_slot, S)
        m.write(S("mem_cpq_lvl"), bytes([0xFC]))
        m.write(S("mem_cpq"), bytes([wptr & 0xFF, wptr >> 8]))
        os88marty.until(m, lambda _: u16(m.read(S("mem_cpq"), 2)) == 0,
                        "the posted compaction", poll=0.2, limit=30)
        os88marty.ui_done(m)

        c1, t1, k1 = carve(), word("dos_trseg"), word("dos_m33seg")
        print("DOS at %04x: carve %04x/%d paras, dos_trseg %04x, m33seg %04x"
              % (seg(), c1[0], c1[1], t1, k1))
        bad += check(2, c1[0] != c0[0], "the carve moved",
                     "(%04x -> %04x)" % (c0[0], c1[0]))
        want = c1[0] + (t0 - c0[0])
        bad += check(3, t1 == want and k1 == want, "both words follow it",
                     "(want %04x, dos_trseg %04x, dos_m33seg %04x)"
                     % (want, t1, k1))

        # --- a program, typed at the prompt -------------------------------
        m.type_text("DOSHELLO\n")
        rows = []

        def ready(mm):
            rows[:] = mm.screen() or []
            return any("READY" in r for r in rows)
        try:
            os88marty.until(m, ready, "DOSHELLO's READY", poll=0.2, limit=30)
            ran = True
        except os88marty.MartyError:
            ran = False
        inrec = [c for c in claims(m) if c[0] == c1[0]]
        bad += check(4, ran and inrec and inrec[0][3] == 0,
                     "it runs, and the carve is PINNED",
                     "(READY %s, carve %r)" % (ran, inrec))
        m.type_text("x")
        try:
            os88marty.until(m, lambda _: m.read(
                (seg() << 4) + imgsz + B["dos_inbr"], 1)[0] == 0
                and m.read((seg() << 4) + imgsz + B["dos_state"], 1)[0]
                == DST_RAN, "the program back out", poll=0.3, limit=30)
        except os88marty.MartyError:
            pass
        os88marty.settle(m)
        st = m.read((seg() << 4) + imgsz + B["dos_state"], 1)[0]
        c2 = carve()
        bad += check(5, st == DST_RAN and c2 is not None and c2[3] != 0,
                     "it exits, and movable again",
                     "(dos_state %d, carve %r)" % (st, c2))

        # --- the trace is in the NEW part ----------------------------------
        base = c2[0] + (t0 - c0[0]) if c2 else t1
        total = word("dos_tracen")
        ring = bytes(m.read((base << 4) + K["DOS_TRB_OFF"],
                            K["DOS_TRACEN"] * K["DOS_TRACE_SZ"]))
        ahs = set(struct.unpack_from("<H", ring, i)[0] >> 8
                  for i in range(0, len(ring), K["DOS_TRACE_SZ"]))
        log = None
        for _ in range(10):
            v = os88flush.Flush(marty=m).volume(1)
            if "TRACE.LOG" in v.names():
                log = v.read("TRACE.LOG")
                if log:
                    break
            os88marty.settle(m, quiet=2.0, stable=2, limit=60)
        bad += check(6, total > 0 and 0x30 in ahs and bool(log),
                     "the trace is in the new part",
                     "(%d call(s), AH=30h %s, TRACE.LOG %d bytes)"
                     % (total, 0x30 in ahs, len(log or b"")))
        print("     %d call(s) traced; TRACE.LOG %d bytes"
              % (total, len(log or b"")))
    print("dostrmove: %s" % ("FAIL (%d)" % bad if bad else
                             "all checks passed"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
