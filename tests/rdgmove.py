#!/usr/bin/env python3
"""RealDMG's claims MOVE while a game runs, and the game runs on (NO-PINNING A1).

    make && python3 tests/rdgmove.py [--leg dynamic|static] [--red bare|nojit]

docs/REALDMG-SPEC.md section 14.4 and docs/plans/NO-PINNING-PLAN.md A1.
tools/gbours.py builds GBBANK8.GB - tools/gbprof/gbbank.asm at 8 banks, a
128KB MBC5 with 32KB of battery RAM - which switches among its banks in an
LFSR's order and checks EVERY switch two ways: the bank's own bytes, and a
routine at 4010h run from 4000-7FFF, where the core's ROMX segment, the bank
table and the recompiler's bank keys all meet. It counts switches checked at
C000h and wrong ones at C002h. On an 8088 the recompiler is on, so the
cartridge's banked code is running TRANSLATED when its claims move.

Two legs, a boot each. DYNAMIC: the recompiler as it starts. STATIC: Profile
for Static on, then CPU > Static Recompiler, so the table is FIXED from a
recorded profile and the profile's own claim is live. Each opens the game in
RealDMG on MartyPC, records the words naming each claim and the ROM's bytes,
then posts a compaction from OUTSIDE (tests/pkgmove.py's door: [mem_cpq] at
the trivial purge rank, which drops the raise caches the window's opening
put under the game's claims).

  1. the frames, the damage memory, the recompiler's claim, the ROM, the RAM
     (and Static's profile) are declared movable (MC_RLOC, read from outside);
  2. the ROM and the recompiler's claim (and the profile) MOVED, else the run
     proves nothing;
  3. every word names a live claim afterwards, rw_fseg 720 paragraphs into
     rw_pseg's;
  4. the ROM's 128KB are the same bytes at the new address;
  5. the core's own copies followed: S_ROMSEG, every bank in the bank table,
     S_ROMX, S_RAMSEG, S_PRFSEG, S_JITSEGA, S_JITHSEG and the translator's
     door S_JITVEC - and the cache was FLUSHED (S_JITFL counted one), since a
     chain into the cache is a `jmp far` with the old segment in its bytes;
  6. the game runs on: switches checked keep climbing, not one reads the
     wrong bank, the frames advance and the recompiler translates again -
     and Static's table is fixed and preloaded again.

RED RUNS: `--red bare` pokes a `ret` over rw_mreloc in the running package
before the post (the machine stops answering: the frames' words name the old
base); `--red nojit` one over rw_mjit (5 fails on the core's cache segment,
the stale block table's flush lands in the ROM's new place, and the game
stops dead).
"""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import dispapps                                         # noqa: E402
import gbwindow                                         # noqa: E402
import os88geom                                         # noqa: E402
import os88marty as M                                   # noqa: E402
import os88sym                                          # noqa: E402
import os88ui                                           # noqa: E402

S = os88sym.linear
MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
ST = 0x4000
S_ROMX, S_ROMSEG, S_FRAMES = ST + 0x14, ST + 0x16, ST + 0x3E
S_RAMSEG, S_JITON, S_JITVEC = ST + 0x66, ST + 0xB7, ST + 0xB8
S_JITFL, S_JITNB, S_JITHSEG = ST + 0xC8, ST + 0xCA, ST + 0xEE
S_JITFIX, S_PRFSEG = ST + 0xF6, ST + 0xF8
S_VIEW, S_RETFAR, S_DISP, S_RESVEC = ST + 0x12, ST + 0x28, ST + 0x2C, ST + 0xB0
S_JITSEGA, RDG_BTAB = 0x4810, 0x6800
BANKS = 8
WORDS = ("rw_pseg", "rdg_dmgseg", "rw_jitseg", "rw_romseg", "rw_ramseg",
         "rw_prfseg")


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def claims(m):
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    return {u16(raw, i * MC_SIZE): (u16(raw, i * MC_SIZE + 2),
                                    u16(raw, i * MC_SIZE + 8))
            for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)}


def dump(m, label):
    """mem_tab, every live record: base, paragraphs, owner, MC_DMA, MC_RLOC."""
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    rows = sorted(tuple(u16(raw, i * MC_SIZE + k) for k in (0, 2, 4, 6, 8))
                  for i in range(MEM_MAX) if u16(raw, i * MC_SIZE))
    print("  mem_tab %s: pinseg %04x" % (label, u16(m.read(S("mem_pinseg"), 2))))
    for r in rows:
        print("    %04x +%04x own %04x dma %04x rloc %04x" % r)


def leg(a, disk, MAP, lab, static):
    """One boot: the dynamic recompiler, or (static) Static's fixed table with
    its profile claim. A list of failures, empty when every check held."""
    name = "static" if static else "dynamic"
    off = dict((w, MAP[w]) for w in WORDS)
    fails = []
    with os88ui.boot(os.path.join(ROOT, "build", "os8088-720.img"), apps=disk,
                     machine=a.machine, quick=True, verbose=False) as ui:
        m = ui.m
        calc = ui.path("B:/CALC.O88")   # FIRST, so its region tops the heap:
        ui.path("B:/GBBANK8.GB")        #   closed, it leaves the hole the
        st = {}                         #   region and the carve rise into

        def word(seg, o):
            return u16(m.read(seg * 16 + o, 2))

        def byte(seg, o):
            return m.read(seg * 16 + o, 1)[0]

        def wram(view):
            b = m.read(view * 16 + 0xC000, 16)
            return u16(b), b[2], b[3], b[15]

        def started(_):
            st["seg"] = st.get("seg") or gbwindow.find_seg(m, b"REALDMG",
                                                           lab["rdg_viewseg"])
            if st["seg"]:
                st["view"] = word(st["seg"], lab["rdg_viewseg"])
            return st.get("view") and wram(st["view"])[3] == 0xA5
        M.until(m, started, "GBBANK8 checking its first switch", poll=0.5,
                guest=120.0)
        wptr = os88geom.winptr(m, ui.window("RealDMG"), S)
        ui.close(calc)

        def pseg():                     # the region, through the window: it
            return word(0, wptr + os88geom.W_SEG)       # may move as well
        view = st["view"]
        def pick(menu, item):           # GBBANK8 never HALTs: the 8088 is
            for _ in range(5):          #   the core's with interrupts off a
                try:                    #   budget at a time, and the 1200-
                    return ui.menu_pick(menu, item)     # baud mouse loses a
                except (M.MartyError, os88ui.UIError):  # packet now and then.
                    M.guest_sleep(m, 1)                 # Again
            return ui.menu_pick(menu, item)
        def until_picked(menu, item, done, what):
            for _ in range(4):          # each pick CONFIRMED off the guest:
                if done():              #   a dropped release picks nothing
                    return              #   and raises nothing
                pick(menu, item)
                try:
                    M.until(m, lambda _: done(), what, poll=0.3, guest=8.0)
                except M.MartyError:
                    pass
            if not done():
                raise M.MartyError("%s > %s never took: %s" % (menu, item, what))

        def weighed():                  # the profile's entries that weigh
            ps = word(st["seg"], MAP["rw_prfseg"])      # something
            return ps and sum(1 for i in range(3, 2048 * 4, 4)
                              if m.read(ps * 16 + i, 1)[0]) or 0
        if static:                      # the profile records what the
            until_picked("CPU", "Profile for Static",   # dynamic recompiler
                         lambda: byte(st["seg"], MAP["rw_prfon"]) and
                         word(st["seg"], MAP["rw_prfseg"]), "the profile on")
            fr = word(view, S_FRAMES)   # translates from a reset (its
            until_picked("Machine", "Reset",            # frames from 0)...
                         lambda: word(view, S_FRAMES) < fr, "the reset")
            try:
                M.until(m, lambda _: weighed() >= 10, "ten blocks in the "
                        "profile", poll=0.5, guest=30.0)
            except M.MartyError:
                fails.append("the profile recorded %d blocks in 30 s (S_PRFSEG "
                             "%04x rw_prfseg %04x, blocks %d, JIT on %d)"
                             % (weighed(), word(view, S_PRFSEG),
                                word(st["seg"], MAP["rw_prfseg"]),
                                word(view, S_JITNB), byte(view, S_JITON)))
                return fails
            until_picked("CPU", "Static Recompiler",    # ...and Static
                         lambda: byte(view, S_JITFIX) == 1,  # preloads them
                         "Static's table fixed")
        M.guest_sleep(m, 6)             # the recompiler warm: blocks in it
        M.quiesce(m, lambda: sorted(claims(m)), guest=2.0, what="the claims")
        seg0 = pseg()
        w0 = dict((w, word(seg0, o)) for w, o in off.items())
        carve0, core0 = word(seg0, MAP["op_base"]), word(seg0, MAP["rdg_coreseg"])
        npre0 = word(seg0, MAP["rw_npre"])
        held = claims(m)
        print("rdgmove %s: package %04x carve %04x core %04x view %04x  %s"
              % (name, seg0, carve0, core0, view, "  ".join(
                  "%s=%04x" % kv for kv in w0.items())))
        need = [w for w in WORDS if w0[w]]
        if not w0["rw_jitseg"] or not w0["rw_romseg"]:
            fails.append("no recompiler or no ROM claim (%s): the load did not "
                         "plan as this row needs" % w0)
            return fails
        if static and (not w0["rw_prfseg"] or not npre0):
            fails.append("Static has no profile claim or preloaded nothing "
                         "(rw_prfseg %04x, %d blocks)" % (w0["rw_prfseg"], npre0))
        undecl = [w for w in need if w0[w] not in held or not held[w0[w]][1]]
        if carve0 not in held or not held[carve0][1]:
            undecl.append("the carve")
        print("  1 declared movable   %d of %d%s" % (len(need) + 1 - len(undecl), len(need) + 1,
              "" if not undecl else "  NOT: %s" % undecl))
        if undecl:
            fails.append("not declared movable: %s" % undecl)
        romhash = hashlib.md5(m.read(w0["rw_romseg"] * 16, BANKS * 16384)).digest()
        n0, bad0, first0, _ = wram(view)
        fl0, nb0 = word(view, S_JITFL), word(view, S_JITNB)
        print("  before: switches %d, wrong %d; JIT on %d, fixed %d, blocks %d, "
              "flushes %d, preloaded %d" % (n0, bad0, byte(view, S_JITON),
                                            byte(view, S_JITFIX), nb0, fl0, npre0))
        if a.dump:
            dump(m, "before")
        if a.red:
            bug = {"bare": "rw_mreloc", "nojit": "rw_mjit",
                   "carve": "rw_partsmv"}[a.red]
            m.write(seg0 * 16 + lab[bug], b"\xC3")
            print("  RED RUN: a ret over %s" % bug)
        try:
            for _ in range(2):          # TWO passes: the first lifts the region
                m.write(S("mem_cpq_lvl"), bytes([0xFC]))    # into Calc's hole,
                m.write(S("mem_cpq"), bytes([wptr & 0xFF, wptr >> 8]))
                M.until(m, lambda _: u16(m.read(S("mem_cpq"), 2)) == 0,
                        "the posted compaction", poll=0.2, guest=30.0)
                M.ui_done(m)            # and the restart that re-enters the
                M.quiesce(m, lambda: sorted(claims(m)),     # worker clears its
                          guest=1.0, what="the compacted claims")   # park, so
                # the carve under the region waits for the second
                # (mem_region_reloc: [sch_parked] = 0)
        except M.MartyError as e:
            fails.append("the machine stopped answering after the move: %s"
                         % str(e).split(".")[0])
            return fails
        if a.dump:
            dump(m, "after")
        seg1 = pseg()
        w1 = dict((w, word(seg1, o)) for w, o in off.items())
        carve1 = word(seg1, MAP["op_base"])
        view0, view = view, word(seg1, lab["rdg_viewseg"])
        core1 = word(seg1, MAP["rdg_coreseg"])
        held = claims(m)
        print("rdgmove %s: package %04x carve %04x core %04x view %04x  %s"
              % (name, seg1, carve1, core1, view, "  ".join(
                  "%s=%04x" % kv for kv in w1.items())))
        moved = [w for w in w1 if w1[w] != w0[w]] + (
            ["the carve"] if carve1 != carve0 else [])
        print("  2 moved              %s" % (", ".join(moved) or "NOTHING"))
        for w in ("rw_romseg", "rw_jitseg") + (("rw_prfseg",) if static else ()):
            if w1[w] == w0[w] and not a.red:
                fails.append("%s did not move: the run proves nothing for it" % w)
        if carve1 == carve0 and not a.red:
            fails.append("the carve did not move: the run proves nothing for it")
        stale = [w for w in need if w1[w] not in held]
        if carve1 not in held:
            stale.append("op_base %04x" % carve1)
        d = (carve1 - carve0) & 0xFFFF
        if view != (view0 + d) & 0xFFFF or core1 != (core0 + d) & 0xFFFF:
            stale.append("rdg_viewseg %04x / rdg_coreseg %04x (the carve moved "
                         "%04x)" % (view, core1, d))
        fseg = word(seg1, MAP["rw_fseg"])
        if fseg != (w1["rw_pseg"] + 720) & 0xFFFF:
            stale.append("rw_fseg %04x" % fseg)
        print("  3 every word live    %s" % ("yes" if not stale else "STALE: %s" % stale))
        if stale:
            fails.append("stale words: %s" % stale)
        same = w1["rw_romseg"] in held and hashlib.md5(
            m.read(w1["rw_romseg"] * 16, BANKS * 16384)).digest() == romhash
        print("  4 ROM survived       %s" % ("yes" if same else "NO"))
        if not same:
            fails.append("the ROM's bytes are not the same at %04x" % w1["rw_romseg"])
        core = []
        if word(view, S_VIEW) != view:
            core.append("S_VIEW %04x" % word(view, S_VIEW))
        for nm, o in (("S_DISP", S_DISP), ("S_RESVEC", S_RESVEC)):
            if word(view, o + 2) != core1:
                core.append("%s's segment %04x" % (nm, word(view, o + 2)))
        rs = w1["rw_romseg"]
        if word(view, S_ROMSEG) != rs:
            core.append("S_ROMSEG %04x" % word(view, S_ROMSEG))
        btab = [word(view, RDG_BTAB + 2 * n) for n in range(BANKS)]
        want = [(view - 0x400) & 0xFFFF] + [(rs + n * 0x400 - 0x400) & 0xFFFF
                                             for n in range(1, BANKS)]
        if btab != want:
            core.append("bank table %s against %s" % (
                " ".join("%04x" % x for x in btab), " ".join("%04x" % x for x in want)))
        if word(view, S_ROMX) not in want:
            core.append("S_ROMX %04x" % word(view, S_ROMX))
        if w1["rw_ramseg"] and word(view, S_RAMSEG) != w1["rw_ramseg"]:
            core.append("S_RAMSEG %04x" % word(view, S_RAMSEG))
        if w1["rw_prfseg"] and word(view, S_PRFSEG) != w1["rw_prfseg"]:
            core.append("S_PRFSEG %04x" % word(view, S_PRFSEG))
        js = w1["rw_jitseg"]
        if word(view, S_JITSEGA) != js:
            core.append("S_JITSEGA %04x" % word(view, S_JITSEGA))
        if word(view, S_JITHSEG) != (js + 0x1000) & 0xFFFF:
            core.append("S_JITHSEG %04x" % word(view, S_JITHSEG))
        vec = word(view, S_JITVEC + 2)
        if vec != word(seg1, lab["rw_jinit"] + 2) or \
                not js <= vec < js + held.get(js, (0,))[0]:
            core.append("S_JITVEC's segment %04x" % vec)
        fl1 = word(view, S_JITFL)
        if fl1 == fl0:
            core.append("no flush (S_JITFL %d)" % fl1)
        print("  5 the core followed  %s" % ("yes" if not core else "NO: %s" % "; ".join(core)))
        if core:
            fails.append("the core's copies: %s" % "; ".join(core))
        n1, _, _, _ = wram(view)
        f1, nb1 = word(view, S_FRAMES), word(view, S_JITNB)
        M.guest_sleep(m, a.secs)
        n2, bad2, first2, _ = wram(view)
        f2, nb2 = word(view, S_FRAMES), word(view, S_JITNB)
        state = byte(seg1, MAP["rw_state"])
        fix = byte(view, S_JITFIX)
        npre = word(seg1, MAP["rw_npre"])
        runs = (n2 - n1) & 0xFFFF
        print("  6 after %gs: switches +%d, wrong %d (first bank %d), frames +%d, "
              "blocks %d -> %d -> %d, fixed %d, preloaded %d, state %d"
              % (a.secs, runs, bad2, first2, (f2 - f1) & 0xFFFF, nb0, nb1, nb2,
                 fix, npre, state))
        if runs < 50 or bad2 or state != 1 or f2 == f1 or nb2 == nb0:
            fails.append("the game did not run on correctly after the move "
                         "(switches +%d, wrong %d, state %d, frames %d -> %d, "
                         "blocks %d -> %d)" % (runs, bad2, state, f1, f2, nb0, nb2))
        if static and (fix != 1 or npre == 0):
            fails.append("Static's table was not preloaded again after the "
                         "move (fixed %d, %d blocks)" % (fix, npre))
    return fails


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--machine", default="os8088_xt_vga_720b")
    ap.add_argument("--red", choices=("bare", "nojit", "carve"))
    ap.add_argument("--leg", choices=("dynamic", "static"),
                    help="one leg only (both by default)")
    ap.add_argument("--secs", type=float, default=10)
    ap.add_argument("--dump", action="store_true", help="mem_tab before and after")
    a = ap.parse_args()
    rom = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "gbours.py"),
                                   "gbbank8"], text=True).strip().splitlines()[-1]
    tmp = tempfile.mkdtemp(prefix="rdgmove-")
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                           "-o", disk, "--size", "720",
                           os.path.join(ROOT, "build", "realdmg.o88"), rom,
                           os.path.join(ROOT, "build", "calc.o88")],
                          stdout=subprocess.DEVNULL)
    MAP = dispapps._map("realdmg")      # bss equates and code labels alike
    lab = dict((n, MAP[n]) for n in
               ("rdg_viewseg", "rdg_dmgseg", "rw_jinit", "rw_mreloc", "rw_mjit",
                "rw_partsmv"))
    if lab["rdg_viewseg"] != gbwindow.label_off(
            os.path.join(ROOT, "build", "realdmg.lst"), "rdg_viewseg"):
        sys.exit("rdgmove: the map is not of build/realdmg.o88 - make first")
    fails = []
    for static in (False, True):
        name = "static" if static else "dynamic"
        if a.leg and a.leg != name:
            continue
        fails += ["%s: %s" % (name, f) for f in leg(a, disk, MAP, lab, static)]
    for f in fails:
        print("FAIL rdgmove: " + f)
    print("rdgmove: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
