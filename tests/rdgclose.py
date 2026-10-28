#!/usr/bin/env python3
"""RealDMG's BANK CACHE under a move, and a close while it fetches (86Box).

    make && python3 tests/rdgclose.py [--vm 286] [--leg move|close] [--red carve|slots]

docs/REALDMG-SPEC.md sections 10.3 and 14.4. GBBANK.GB (tools/gbprof/
gbbank.asm, ours) is a 1MB MBC5 cartridge that switches among all 64 of its
banks in an LFSR's order, checking each switch by the bank's bytes and by a
routine run from 4000-7FFF. On 86Box's 286 with 4MB it goes to the bank
cache (section 10.3): the cartridge above 1MB and a few 16KB slots in
conventional memory, and since the LFSR visits every bank in turn the
least-recently-used slot is always the wrong one - almost every switch is a
FETCH, so the worker spends most of its time in rw_bmiss waiting on the UI
task. That is the path MartyPC cannot host (no memory above 1MB) and both
legs are about it.

MOVE: a compaction is posted from outside, twice (the first lifts the region
into the hole the B: window's raise caches leave, and the region's restart
clears the worker's park, so the parts carve under it waits for the second -
tests/rdgmove.py says why), and the SLOTS and the PARTS CARVE must both move:
every word follows - [rw_slotseg], [op_base], rdg_viewseg, rdg_coreseg, and
in the moved view S_VIEW, the bank table (every resident bank inside the
slots' new range, bank 0 at the view less 400h), S_ROMSEG and [rw_romseg]
(the view, in the cache's load) - and the cartridge runs on with not one
wrong bank and fetches still coming.

CLOSE: the window's close box is clicked while the cartridge fetches, and
the window must be GONE within 20 emulated seconds. Before the fix rw_bmiss
tested [rw_closing] before [rw_bankreq], so a worker waiting on a bank when
the close came looped at OSAPI_TASK_ALIVE for ever while rw_onclose waited
for [rw_incore] to fall: the window never closed and the UI task was stuck
in the negotiator. RED: RDGWIN_O88/RDGWIN_LST naming the package before the
fix (the close hangs); `--red carve|slots` pokes a ret over rw_partsmv or
rw_mreloc before the move (the move leg fails).

86Box is not cycle-accurate (docs/86BOX.md): nothing here is a speed.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests"),
                os.path.join(ROOT, "tests", "rdgcore")]
import gbwindow  # noqa: E402
import os88geom as G  # noqa: E402
import os88sym  # noqa: E402
import rdgwin  # noqa: E402

RDG_BTAB, S_VIEW, S_ROMSEG = 0x6800, 0x4012, 0x4016
GAME_ROW = (150, 143)           # GAME.GB, under A.O88 in the B: list
CALC_CLOSE = (183, 56)          # the Calculator's close box, where it opens


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def label(name):
    return gbwindow.label_off(rdgwin.LST, name)


def code(name):
    """a code label's offset in the package being run: nasm's own map of the
    source (a red run pokes the build of this tree, never RDGWIN_O88's)"""
    import dispapps
    return dispapps._map("realdmg")[name]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--vm", default="286")
    ap.add_argument("--leg", choices=("move", "close"), help="one leg (both by default)")
    ap.add_argument("--red", choices=("carve", "slots"))
    ap.add_argument("--secs", type=float, default=10)
    a = ap.parse_args()
    rom = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "gbours.py"),
                                   "gbbank"], text=True).strip().splitlines()[-1]
    o = argparse.Namespace(rom=rom, vm=a.vm, keep="", mem=4096, press="", fs=False,
                           quick=True)
    off = dict((n, rdgwin.bss_off(n)) for n in
               ("rw_slotseg", "rw_nslot", "rw_nmiss", "rw_romseg", "op_base",
                "rw_pause"))
    vo = label("rdg_viewseg")
    fails = []
    tmp = tempfile.mkdtemp(prefix="rdgclose-")  # THE CALCULATOR FIRST: A.O88
    calc = os.path.join(tmp, "A.O88")            #   lists first, so the boot's
    shutil.copy(os.path.join(ROOT, "build", "calc.o88"), calc)   # double-
    os.environ["RDGWIN_EXTRA"] = calc            #   click opens it; its region
    b = rdgwin.boot(o)                           #   tops the heap and, closed,
    try:                                         #   leaves the hole the region
        b.to(*GAME_ROW)                          #   and the carve rise into
        b.click(1)                               # (the Disk window fronted,
        b.cmd("step 1000")                       #   then the game opened)
        b.click(2)
        seg, view, core = rdgwin.find(b)
        for _ in range(3):                       # (the first click on a
            b.to(*CALC_CLOSE)                    #   window behind fronts it)
            b.click(1)
            if b.until(lambda: len(rdgwin.windows(b)) == 2, "the Calculator "
                       "closed", 5000, 250, need=False) is not None:
                break
        else:
            b.shot(os.path.join(tmp, "calc.png"))
            sys.exit("rdgclose: the Calculator would not close (%s)"
                     % os.path.join(tmp, "calc.png"))

        def wram():
            m = b.memr(view * 16 + 0xC000, 16)
            return u16(m), m[2], m[3], m[15]

        def word(s, k):
            return u16(b.memr(s * 16 + k, 2))
        b.until(lambda: wram()[3] == 0xA5, "the first switch checked", 60000, 500)
        bk = rdgwin.bankstat(b, seg)
        if bk is None:
            fails.append("the cartridge did not go to the bank cache")
            return 1
        b.cmd("step 3000")
        print("rdgclose: package %04x view %04x, the cache: %d slots, %d fetched"
              % (seg, view, bk[0], rdgwin.bankstat(b, seg)[1]))

        def winptr():                   # RealDMG's window: the one whose W_SEG
            tab = b.memr(b.sym("wm_wins"), G.WIN_SIZE * G.MAX_WIN)    # is it
            for i in range(G.MAX_WIN):
                r = G.WIN_SIZE * i
                if tab[r + G.W_FLAGS] & 1 and u16(tab, r + G.W_SEG) == seg:
                    return b.sym("wm_wins") + r
            return None
        if a.leg in (None, "move"):
            # PAUSED FIRST, and that is the finding rather than a convenience:
            # a running cartridge on this path keeps its worker inside a frame
            # for good - each fetch is the UI task's to serve (rw_bmiss), and
            # the UI task is the one waiting in the compactor's park, so the
            # worker never reaches OSAPI_TASK_ALIVE and every claim of the
            # package stays pinned (bucket C: it is mid-frame). Paused, the
            # worker sleeps at the top of its loop and parks there
            for _ in range(4):
                if b.memr(seg * 16 + off["rw_pause"], 1)[0]:
                    break
                rdgwin.machine_item(b, (150, rdgwin.ROW(2)))   # Machine > Pause
            if not b.memr(seg * 16 + off["rw_pause"], 1)[0]:
                fails.append("Machine > Pause never took")
                return 1
            slot0, carve0 = word(seg, off["rw_slotseg"]), word(seg, off["op_base"])
            nslot = word(seg, off["rw_nslot"])
            if a.red:
                at = code("rw_partsmv" if a.red == "carve" else "rw_mreloc")
                b.cmd("memw 0x%X C3" % (seg * 16 + at))
                print("  RED RUN: a ret over %s" % ("rw_partsmv" if a.red == "carve"
                                                   else "rw_mreloc"))
            for _ in range(2):
                wp = winptr()
                b.cmd("memw 0x%X FC" % b.sym("mem_cpq_lvl"))
                b.cmd("memw 0x%X %02X%02X" % (b.sym("mem_cpq"), wp & 0xFF, wp >> 8))
                b.until(lambda: b.word("mem_cpq") == 0, "the posted compaction",
                        30000, 100)
                tab = None              # (not ui_done: the UI task is never
                for _ in range(40):     #   idle while it fetches banks) - the
                    b.cmd("step 500")   #   claim map read until it holds
                    now = b.memr(os88sym.linear("mem_tab"), G.MEM_MAX * G.MC_SIZE)
                    if now == tab:
                        break
                    tab = now
                try:                    # (the region may have moved)
                    seg = rdgwin.find(b, tries=2)[0]
                except SystemExit:
                    fails.append("after the move no REALDMG has a view whose "
                                 "S_VIEW names itself: the carve's words are stale")
                    return 1
            view = word(seg, vo)
            slot1, carve1 = word(seg, off["rw_slotseg"]), word(seg, off["op_base"])
            core1 = word(seg, vo + 2)
            print("  slots %04x -> %04x, carve %04x -> %04x, view -> %04x, core -> %04x"
                  % (slot0, slot1, carve0, carve1, view, core1))
            if slot1 == slot0 and not a.red:
                fails.append("the slots did not move: the run proves nothing for them")
            if carve1 == carve0 and not a.red:
                fails.append("the carve did not move: the run proves nothing for it")
            bad = []
            if word(view, S_VIEW) != view:
                bad.append("S_VIEW %04x" % word(view, S_VIEW))
            if word(view, S_ROMSEG) != view or word(seg, off["rw_romseg"]) != view:
                bad.append("S_ROMSEG %04x rw_romseg %04x" % (word(view, S_ROMSEG),
                                                            word(seg, off["rw_romseg"])))
            bt = [u16(b.memr(view * 16 + RDG_BTAB, 128), 2 * n) for n in range(64)]
            if bt[0] != (view - 0x400) & 0xFFFF:
                bad.append("bank 0's word %04x" % bt[0])
            okset = set((slot1 + k * 0x400 - 0x400) & 0xFFFF for k in range(nslot))
            stray = [n for n in range(1, 64) if bt[n] and bt[n] not in okset]
            if stray:
                bad.append("banks %s outside the slots" % stray[:6])
            print("  the copies followed  %s" % ("yes" if not bad else "NO: %s" % "; ".join(bad)))
            if bad:
                fails.append("the copies: %s" % "; ".join(bad))
            for _ in range(4):          # ...and Go again
                if not b.memr(seg * 16 + off["rw_pause"], 1)[0]:
                    break
                rdgwin.machine_item(b, (150, rdgwin.ROW(2)))
            n1, bad1, _, _ = wram()
            f1 = rdgwin.bankstat(b, seg)[1]
            b.cmd("step %d" % int(a.secs * 1000))
            n2, bad2, first2, _ = wram()
            f2 = rdgwin.bankstat(b, seg)[1]
            run = (n2 - n1) & 0xFFFF
            print("  after %gs: switches +%d, wrong %d, fetches +%d"
                  % (a.secs, run, bad2, (f2 - f1) & 0xFFFF))
            if run < 100 or bad2 or f2 == f1:
                fails.append("the cartridge did not run on correctly after the move "
                             "(switches +%d, wrong %d, fetches +%d)"
                             % (run, bad2, (f2 - f1) & 0xFFFF))
        if a.leg in (None, "close") and not (a.red and a.leg is None):
            n0 = len(rdgwin.windows(b))
            b.to(*rdgwin.CLOSE_BOX)     # (the window has not moved since it opened)
            b.click(1)
            gone = b.until(lambda: winptr() is None, "the window closed", 20000, 250,
                           need=False)
            print("  close while fetching: %s" % ("closed in %.1f emulated s"
                                                  % (gone / 1000.0) if gone is not None
                                                  else "STILL OPEN after 20 s"))
            if gone is None:
                fails.append("the window did not close: the worker is stuck in "
                             "rw_bmiss and rw_onclose waits on it (%d windows)" % n0)
    finally:
        b.quit(remove=True)
        for f in fails:
            print("FAIL rdgclose: " + f)
        print("rdgclose: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
