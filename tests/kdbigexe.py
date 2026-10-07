#!/usr/bin/env python3
"""The read-ahead ladder, under the LOAD (SPEC.md 96.44.11.1).

`tests/kdarena.py` covers the handover, where `kd_giveback` runs the ladder to
the bottom - and the bottom is all it can ever reach there, because
`dos_build_psp` hands a program everything and `dos_exe_setup` reads MINALLOC
and not MAXALLOC. The RUNGS only mean something at the other site: `.loadtry`,
where `dos_load` is refused, one rung of the cache is shed, and the read is
made again.

Nothing already in the tree can reach that path. It needs a file between the
arena's capacity WITH the cache and its capacity without - 569,952 and 602,720
bytes on a 640KB machine - so `tests/dosbig/big.asm` is a fixture sized to sit
in the middle of that band. Its own header carries the table.

WHAT IT WOULD CATCH:

  - `.loadtry` deleted, so a refusal    -> the program never loads at all and
    stands                                 the box stays in DST_ERR. THIS IS
                                           THE BREAK-ON-PURPOSE CHECK: the row
                                           passes only because the retry is
                                           there
  - a refusal that WROTE before it       -> the head marker 585KB below the
    refused                                 code comes back wrong, because the
                                            partial read landed on the cache
  - `kd_arena` not re-read between        -> the second attempt is bounded by
    tries                                    the same `[dos_ldpara]` and the
                                             loop spins to the bottom and fails
  - `dos_movedown` binding at 64KB        -> the head marker is wrong while the
                                             program still runs, which is the
                                             one failure a "did it start" row
                                             would pass
  - a .COM's stack word written into      -> the marker at image offset 0FEFCh
    an .EXE (SPEC.md 96.3.1)                 comes back zeroed. It is the
                                             defect this fixture SAT THROUGH
                                             for a cycle, because a pad of
                                             zeros cannot see a zero written
                                             into it
  - the ladder collapsing to one step     -> the recorded widths are [7, 0]
                                             rather than a prefix of
                                             [7, 4, 2, 0]

THE WINDOWED RUN IS THE CONTROL and it is expected to FAIL: 586KB does not fit
the windowed box's ~437KB arena, so the box lands in DST_ERR before the third
arm is ever picked. That is what makes this fixture a fair question rather than
a program that would have run anywhere.

It runs on `os8088_5150_cga_gla_mix`, the tree's only machine with two drives
of different types: 586KB does not fit a 360KB floppy, so B: has to be the
1.44MB one. MartyPC, for every other kd* row's reason. `make kdostest` builds
the disk.
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88build                                               # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402
from os88geom import KD_SEG                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYS = "build/os8088-360.img"
BIG = "build/dosbig144.img"
# **THROUGH THE RUN'S TREE** (tools/os88build.at). A soak reads a FROZEN
# tree and not `build/`, and these two are the only paths here the host
# opens directly rather than handing to os88marty, which resolves them
# itself - so a literal `build/` path reports "not built" about an
# artefact the run has, which reads as `make kdostest` never having been
# typed.  os.path.join keeps it ROOT-anchored when no tree is applied and
# leaves an absolute tree path alone, join discarding everything before
# an absolute component.
EXE = os.path.join(ROOT, os88build.at("build/BIG.EXE"))
KDBIN = os.path.join(ROOT, os88build.at("build/kerndos.bin"))
MACH = "os8088_5150_cga_gla_mix"

RD_N, RD_SEL, RD_PITCH, RD_DIS = 10, 12, 14, 16
WHOLE, NARM = 1, 2
DST_ERR = 3
DOS_PSPP = 10
DSK_RAH_RUNS = 7                # kernel/disk.inc's ceiling, which is the width
                                # a kern_dos mount always arms - it solves the
                                # width from the whole arena
# **THE LADDER STOPS AT `KD_RAH_KEEP`** (SPEC.md 96.44.11.4) and no longer
# runs to nothing, so the widths are scraped from the kernel rather than
# spelled here: a fourth entry would be a rung `kd_giveback` never takes, and
# the row would wait for an attempt that cannot arrive.
RUNG_KB = {7: 32, 4: 18, 2: 9, 0: 0}    # DSK_RAH_RUNS, KD_RAH_L1, KD_RAH_L2
A_BW, A_BG, A_BH, A_BTNY, TITLE_H = 72, 12, 13, 46, 18

WANT = ("kd_top", "kd_floor", "dos_arena", "dos_apara", "dos_wbytes",
        "dsk_rah_runs", "kd_entry.loadtry")


def fail(msg):
    print("kdbigexe: FAIL: %s" % msg)
    sys.exit(1)


def rows(m):
    return [r.rstrip() for r in (m.screen() or [])]


def wait_text(m, want, secs=240, what=""):
    """`secs` is an idle-box figure, spent as a GUEST budget."""
    got = []

    def there(mm):
        got[:] = rows(mm)
        return any(want in r for r in got)
    try:
        os88marty.until(m, there, what or want, poll=0.25, limit=secs)
        return got
    except os88marty.MartyError:
        pass
    fail("%s: %r never appeared. The last screen was %r"
         % (what or want, want, [r for r in rows(m) if r.strip()][:10]))


def symbols():
    """{name: offset} for the SHIPPED kern_dos, proved to be its own.

    kdarena.py's, and for its reason: the Makefile emits no map for
    build/kerndos.bin, so this assembles the same root again and compares the
    BINARY before an offset is trusted - `tools/os88sym.py`'s discipline, a
    map of another build resolving every name to a plausible wrong address.
    """
    if not os.path.exists(KDBIN):
        fail("build/kerndos.bin is not built - `make kdostest`")
    with tempfile.TemporaryDirectory() as td:
        binp = os.path.join(td, "k.bin")
        mapp = os.path.join(td, "k.map")
        root = os.path.join(td, "root.asm")
        with open(root, "w") as f:
            f.write("[map all %s]\n%%include \"%s\"\n"
                    % (mapp, os.path.join(ROOT, "kerndos", "kdos.asm")))
        cmd = ["nasm", "-f", "bin", "-w+error", "-DDOS_EXTCORE"]
        for inc in ("kernel", "kerndos", "apps", "apps/dos", "drivers/net"):
            cmd += ["-I", os.path.join(ROOT, inc) + os.sep]
        cmd += ["-o", binp, root]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            fail("kern_dos would not assemble for its map:\n%s" % r.stderr)
        if open(binp, "rb").read() != open(KDBIN, "rb").read():
            fail("the map describes a DIFFERENT kern_dos to the one on the "
                 "disk. Rebuild (`make kdostest`) before trusting this row")
        out = {}
        for ln in open(mapp):
            p = ln.split()
            if len(p) == 2 and p[1] in WANT:
                out[p[1]] = int(p[0], 16)
            elif len(p) == 3 and p[2] in WANT:
                out[p[2]] = int(p[1], 16)
    missing = [w for w in WANT if w not in out]
    if missing:
        fail("nasm's map has no %s" % ", ".join(missing))
    return out


def rec(m, pseg, dm, off):
    return int.from_bytes(m.read((pseg << 4) + dm["dos_mrad"] + off, 2),
                          "little")


def alert_up(m, base, dm):
    return int.from_bytes(m.read(base + dm["os88ui_awin"], 2), "little")


def alert_button(m, base, dm, i, n=2):
    w = alert_up(m, base, dm)
    if not w:
        fail("no alert is up to click")
    r = m.read((KD_SEG << 4) + w, 8)
    wx = int.from_bytes(r[2:4], "little")
    wy = int.from_bytes(r[4:6], "little")
    ww = int.from_bytes(r[6:8], "little")
    row = n * (A_BW + A_BG) - A_BG
    left = wx + (ww - row) // 2 + i * (A_BW + A_BG)
    return left + A_BW // 2, wy + TITLE_H + A_BTNY + A_BH // 2


def capacity(top, arena, wpara, kb, kept_kb):
    """dos_load's capacity in FILE BYTES with `kb` of cache still held.

    `dos_load` reads the whole file into [dos_ldpsp]+16 with a capacity of
    ([dos_ldpara] - 16) * 16, and every rung of the ladder moves the ceiling
    the arena is cut from.

    **`top` IS READ AFTER THE RUN, SO IT IS THE CEILING AT `KD_RAH_KEEP`'s
    WIDTH** - which is what this got wrong. It used to subtract the whole of
    `kb` from `[kd_top]`, which was right while the ladder shed to nothing and
    the ceiling ended up above the whole cache. Since SPEC.md 96.44.11.3 put
    the cache ABOVE the ceiling and 96.44.11.4 stopped the ladder at a width,
    `top` already excludes the kept rungs - so subtracting them again counts
    them twice and every capacity comes out one rung too small. BIG.EXE then
    loaded on the attempt the model said was too small, and the row reported
    the machine for the model's error.
    """
    t = top - (((kb - kept_kb) * 1024) >> 4)
    return ((t - arena - wpara) - DOS_PSPP - 16) * 16


def main():
    syms = symbols()
    if not os.path.exists(EXE):
        fail("build/BIG.EXE is not built - `make kdostest`")
    size = os.path.getsize(EXE)
    print("kdbigexe: the map is this kern_dos's own; BIG.EXE is %d bytes"
          % size)
    trybp = (KD_SEG << 4) + syms["kd_entry.loadtry"]

    with os88ui.boot(SYS, apps=BIG, machine=MACH) as ui:
        m = ui.m
        if not ui.path("B:/BIG.EXE"):
            fail("BIG.EXE did not open a DOS window")
        os88marty.settle(m)
        dm = dosmap.package()
        pseg = dosmap.instance(m)
        base = pseg << 4
        mo = os88mouse.Mouse(marty=m)

        # --- the control: it does NOT fit the windowed box -------------------
        st = m.read(base + dm["dos_state"], 1)[0]
        if st != DST_ERR:
            fail("the windowed run left [dos_state] at %d and this fixture is "
                 "supposed to be too big for the windowed arena (~437 KB "
                 "against a %d-byte file). If it now FITS, the fixture has "
                 "drifted small and the row is no longer asking anything"
                 % (st, size))
        print("kdbigexe: 1/5 the windowed box refused it, which is the control")

        # --- the Shut down the OS arm ---------------------------------------------------
        mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
        os88marty.settle(m)
        if rec(m, pseg, dm, RD_N) != NARM:
            fail("the Memory page has %d arms" % rec(m, pseg, dm, RD_N))
        if rec(m, pseg, dm, RD_DIS) & (1 << WHOLE):
            fail("the Shut down the OS arm is GREYED after a failed windowed load: a "
                 "program too big for the window is exactly the one that "
                 "wants the whole machine")
        x1, y1, x2, _ = dosmap.rect(m, pseg, dm, "dos_mrad")
        pitch = rec(m, pseg, dm, RD_PITCH)
        mo.click((x1 + x2) // 2, y1 + WHOLE * pitch + pitch // 2)
        os88marty.settle(m)
        if rec(m, pseg, dm, RD_SEL) != WHOLE:
            fail("clicking the Shut down the OS arm left OS88UI_RD_SEL at %d"
                 % rec(m, pseg, dm, RD_SEL))
        mo.click(*dosmap.centre(m, pseg, dm, "dos_trect"))
        os88marty.settle(m)
        mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))
        os88marty.settle(m)
        if not alert_up(m, base, dm):
            fail("Run on the Shut down the OS arm went straight to the launch "
                 "(SPEC.md 96.42)")

        # --- ...and the ladder, counted at the top of the retry loop ---------
        # ARMED ONLY ACROSS THE LAUNCH. KD_SEG and KERNEL_SEG are the same
        # paragraph, so this flat address is live kernel code until the handoff
        # replaces it - `wm_strad_fit.clamp`, as it happens. Armed for the
        # whole session it would be counting somebody else's instruction; armed
        # from the Proceed click it can only be reached by kern_dos, and the
        # recorded widths below say so out loud.
        runs_at = lambda mm, _r: int.from_bytes(                # noqa: E731
            mm.readseg(KD_SEG, syms["dsk_rah_runs"], 2), "little")
        with os88marty.bp_trace(m, trybp, on_hit=runs_at) as tr:
            mo.click(*alert_button(m, base, dm, 1))             # Proceed
            rs = wait_text(m, "READY", secs=240,
                           what="the run under kern_dos")
        widths = [h["hit"] for h in tr.hits]

        # --- what the program itself says ------------------------------------
        for want in ("os8088 DOS gate - BIG.EXE",
                     "image head 585KB down: OK",
                     "relocation at the far end: OK",
                     "the .COM stack word: OK"):
            if not any(want in r for r in rs):
                fail("%r is not on the screen. The program started but did "
                     "not verify: a partial read or a move that gave up would "
                     "look exactly like this. Screen: %r"
                     % (want, [r for r in rs if r.strip()]))
        print("kdbigexe: 2/5 it ran, the image head 585KB below the code "
              "reads back, and PSP:FFFC still holds the fixture's own marker")

        # --- the numbers kern_dos laid out ------------------------------------
        def w(name):
            b = m.readseg(KD_SEG, syms[name], 2)
            return b[0] | (b[1] << 8)

        top, arena = w("kd_top"), w("dos_arena")
        wpara = (w("dos_wbytes") + 15) >> 4
        keep = dosmap.kd_const("KD_RAH_KEEP")
        ladder = tuple(k for k in (7, 4, 2, 0) if k >= keep)
        caps = [(k, capacity(top, arena, wpara, RUNG_KB[k], RUNG_KB[keep]))
                for k in ladder]
        print("kdbigexe: dos_load capacity per rung: "
              + ", ".join("%d runs %d" % (k, c) for k, c in caps)
              + "  (kd_top corrected for the %d KB kd_giveback kept)"
              % RUNG_KB[keep])

        # 3: the retry was NECESSARY - the file does not fit with the cache.
        if size <= caps[0][1]:
            fail("BIG.EXE is %d bytes and fits in %d with the whole 32 KB "
                 "cache still held, so no rung was ever needed and this row "
                 "tested nothing. The fixture has drifted below the band: "
                 "re-size tests/dosbig/big.asm's BIGSZ towards %d, the middle "
                 "of %d .. %d"
                 % (size, caps[0][1], (caps[0][1] + caps[-1][1]) // 2,
                    caps[0][1], caps[-1][1]))
        print("kdbigexe: 3/5 %d bytes does not fit the %d the mount's cache "
              "leaves - a rung was required" % (size, caps[0][1]))

        # 4: ...and the ladder is what produced it.
        if len(widths) < 2:
            fail("`kd_entry.loadtry` was reached %d time(s) and the file "
                 "needed at least one retry. Either the breakpoint never "
                 "fired or the load was not bounded by [dos_ldpara]: "
                 "recorded %r" % (len(widths), widths))
        if tuple(widths) != ladder[:len(widths)]:
            fail("the cache widths at each load attempt were %r and the "
                 "ladder is %r: a rung was skipped, repeated, or the trace "
                 "caught an instruction that is not kern_dos's"
                 % (widths, list(ladder)))
        print("kdbigexe: 4/5 %d load attempts, cache %s - a prefix of the "
              "ladder" % (len(widths),
                          " -> ".join("%dKB" % RUNG_KB[x] for x in widths)))

        # 5: the width it loaded at is the FIRST one that could hold it.
        #
        # The point of shedding a rung at a time rather than all of it: the
        # load keeps as much cache as the image leaves room for.
        need = next(i for i, (_, c) in enumerate(caps) if size <= c)
        if len(widths) != need + 1:
            fail("it loaded on attempt %d and the first rung with room for "
                 "%d bytes is attempt %d (capacities %r): the ladder is "
                 "shedding more than it has to, or less"
                 % (len(widths), size, need + 1, [c for _, c in caps]))
        print("kdbigexe: 5/5 it loaded on the first rung with room for it, "
              "keeping %d KB of cache for the read" % RUNG_KB[widths[-1]])

    print("kdbigexe: ok")


if __name__ == "__main__":
    main()
