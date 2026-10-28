#!/usr/bin/env python3
"""The Memory page's figure is the figure the program gets, WITH A CARD IN THE
MACHINE (SPEC.md 96.23.7.1, 96.23.7.2, 96.36.3).

    make && make ethertest && make dospkt && python3 tests/dosnetarena.py

**QEMU'S, AND ON THE CLOSED LIST FOR tests/ethernet.py's REASON**: MartyPC has
no network card of any kind, so the emulator this tree develops on cannot host
`ETHER.DRV` at all - and a card is the whole quantity under test. Every
assertion is about BEHAVIOUR and none is about speed.

WHY IT IS ITS OWN ROW AND NOT AN ASSERTION IN dosram. The defect it exists for
is invisible without a wire: `dos_pkt_bufs` claims nothing at all on a machine
with no NIC, so `tests/dosram.py` - whose fixture is a hard disk and no card -
promised 441K and handed over 441 on the build that shipped the bug. The same
build, one NE2000 later, promised 442 and handed over 400.

WHAT IT ASSERTS, and the two failures are DIFFERENT SIZES on purpose:

1.  **THE PROMISE IS THE DELIVERY, IN BOTH ARMS OF THE `Network` BOX.** The
    row under the boxes is what a user reads before they press Run, and
    `dos_akb` is what `dos_run` really claimed. A drift of ~3KB is the page
    not subtracting the packet buffers it is about to spend (96.23.7.1); a
    drift of ~40 is one of them PINNED across `dos_run`'s compaction pass, so
    that the arena - which is ONE run - cannot reach the floor under it
    (96.23.7.2).

2.  **NO CLAIM DOS HOLDS IS PINNED BELOW THE ARENA.** That is the
    mechanism behind assertion 1 and it is read out of `mem_tab` itself, so a
    build that loses the `OSAPI_MEM_MOVABLE` declaration is named rather than
    inferred from a number. A claim is born pinned (SPEC.md 66.2), so this is
    the half that goes wrong by OMISSION - somebody adds a buffer and it is a
    wall, silently, on a busy heap only.

BREAK IT ON PURPOSE (docs/WRITING-TESTS.md 1): take the `mov ax,
dos_pkt_reloc / call dos_pkt_rloc` out of `dos_pkt_bufs`'s `.decl` and
rebuild.  MEASURED on that build: assertion 2 names `2D80+3.0K` and assertion
1 reads 405 promised against 366 given - and the CLEARED arm stays 441/441,
because a card the launch is letting go has no buffers claimed for it at all,
so the wall is never built.  Taking out `dos_mem_arena`'s `sub ax, cx` instead
leaves assertion 2 green and assertion 1 at ~3KB, which is why the two drifts
are reported separately and why the slack is 2.
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import dispcp                                                  # noqa: E402
import dosmap                                                  # noqa: E402
import ethernet as eth                                         # noqa: E402
import os88build                                               # noqa: E402
import os88geom                                                # noqa: E402
import os88qemu                                                # noqa: E402

SYS = "build/ether360.img"
PKT = "build/dospkt360.img"
PROG = "DOSPKT.COM"

# KB.  The page's row is an ESTIMATE and says so with a '~' - the what-if is
# taken at paint time and the claim a moment later - so this is about DRIFT.
# Both defects it exists for are far outside it: 3KB for the unsubtracted
# buffers and ~40 for the pinned one.
SLACK = 2


def say(s):
    print(s, flush=True)


FAILS = []


def fail(msg):
    # ...AND ANYTHING ALREADY FOUND GOES WITH IT. Assertion 2 banks its
    # finding and carries on, so a hard failure after it used to swallow the
    # one line that names the routine - which is how a red run first reported
    # "the Memory page has never been painted" about a build whose pinned
    # claim it had already spotted.
    for f in FAILS:
        say("dosnetarena: " + f)
    say("dosnetarena: FAILED - " + msg)
    sys.exit(1)


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def claims(m):
    """`mem_tab`, as (base, paragraphs, owner, pinned) sorted by base."""
    raw = m.read(eth.S("mem_tab"), 32 * os88geom.MC_SIZE)
    out = []
    for i in range(32):
        r = raw[i * os88geom.MC_SIZE:(i + 1) * os88geom.MC_SIZE]
        base, para, own, _dma, rloc = struct.unpack_from("<HHHHH", r, 0)
        if base:
            out.append((base, para, own, rloc == 0))
    return sorted(out)


def pkg_seg(m, want=0):
    """The segment of a visible package window, newest last.

    **ASKED AT THE POINT OF USE** (SPEC.md 66.6.1.2): DOS's region
    moves under the compactor at every arena claim, so a base taken earlier
    names the bytes the package used to occupy - which decode as plausible
    rubbish rather than as an error.
    """
    b = m.read(eth.S("wm_wins"), os88geom.MAX_WIN * os88geom.WIN_SIZE)
    out = []
    for i in range(os88geom.MAX_WIN):
        o = i * os88geom.WIN_SIZE
        fl = u16(b, o + os88geom.W_FLAGS)
        sg = u16(b, o + os88geom.W_SEG)
        if fl & 3 == 3 and sg:
            out.append(sg)
    return out[want] if len(out) > want else None


def main():
    for p in (SYS, PKT):
        if not os.path.exists(os88build.at(p)):
            fail("%s is missing - run `make ethertest && make dospkt`" % p)

    # A PRIVATE QEMU (os88qemu.Private): what `make test ETHER=1 TESTIMG=
    # TESTAPPS=` launched, without the `make` in the shared tree or the kill
    # of whatever build/qemu.pid named; the mouse is closed-loop
    # (docs/plans/completed/SOAK-SPEED.md technique 15).
    import tempfile
    import xmcheck
    import atexit
    import shutil
    # dospkt.asm's segment goes to 10.0.2.2:8099, which is tests/dosxlat.py's
    # listener: a dosxlat beside this row would ANSWER it, so the port is
    # held for the run (SOAK-PARALLEL.md 17.3) and the row `locks=` it too
    os88qemu.lease_port(8099)
    work = tempfile.mkdtemp(prefix="dpk-")      # short: a QMP socket path
    atexit.register(shutil.rmtree, work, True)  # (runs after q's close)
    q = os88qemu.Private(work, os88build.at(SYS), os88build.at(PKT),
                         ether=True)
    eth.SOCK = q.sock
    m = eth.Qemu(q.sock)
    mouse = os88qemu.Mouse(m, eth.S, lambda *c: xmcheck.qmp(q.sock, *c))
    fails = FAILS
    try:
        # --- the card has to be THERE, or this row asserts nothing ---------
        # Every wait in this row is in the GUEST's seconds (tests/os88qemu.py).
        drvrow = (eth.S("drv_tab") + eth.ETH_ROW * eth.DRVR_SZ
                  + eth.DRVR_SEG)
        # THE BOOT FIRST (os88qemu.kernel_up): drv_tab is .bss, and until
        # stage 2 has expanded the packed kernel those bytes are the LZ4
        # stream - a non-zero JUNK word satisfied this poll at 0.7 s and the
        # row reported "never attached" off the cleared one (SOAK-PARALLEL
        # 17.5)
        os88qemu.kernel_up(m, eth.S, secs=120)
        os88qemu.acted(m, lambda: u16(m.read(drvrow, 2)) != 0, secs=60,
                       what="ETHER.DRV's drv_tab row", poll=0.4)
        seg = u16(m.read(drvrow, 2))
        if not seg:
            fail("ETHER.DRV never attached - no card was found, or "
                 "SYSTEM.CFG did not ask for it. The whole quantity this row "
                 "measures is what a mounted card costs a DOS program, so a "
                 "machine without one answers nothing (SPEC.md 51.3.1)")
        say("dosnetarena: ETHER.DRV at %04X" % seg)

        # THE DESKTOP, NOT THE DRIVER. drv_boot_x fills ETHER.DRV's row
        # during the loading screen and desk_rowcalc lays the zones out AFTER
        # it (kernel.asm's kmain), so a row that is there says nothing about
        # B:'s zone - a loaded box landed in that window and open_drive read
        # "no desktop zone". Drive B's zone is the condition, as dosxlat's.
        xmcheck.wait_desktop(q.sock, "dosnetarena")
        dispcp.open_drive(m, mouse, eth.S, eth.settle, "B")
        wins = dispcp.win_list(m, eth.S)
        wx, wy = dispcp.win_rect(m, eth.S, wins[-1])[:2]
        dispcp.open_named(m, mouse, eth.S, eth.settle, wx, wy, PROG)
        # the program up and on int 16h. It prints READY on the text screen
        # under the bracket and its next instruction is the int 16h, so that
        # word is the state - and the four guest seconds this used to sleep
        # are the bound, should the screen not be readable from here
        up = os88qemu.acted(m, lambda: b"READY" in m.read(0xB8000,
                                                          4000)[::2],
                            secs=4.0, what="DOSPKT's READY", poll=0.25)
        say("dosnetarena: %s" % ("the program is up (READY on its screen)"
                                 if up else "no READY seen in 4 guest "
                                 "seconds - going on as the sleep did"))
        dm = dosmap.package()

        def base():
            s = pkg_seg(m)
            if not s:
                fail("no DOS window - %s opened nothing" % PROG)
            return s << 4

        def word(n):
            return u16(m.read(base() + dm[n], 2))

        def arena_row():
            raw = m.read(base() + dm["dos_marn"], 8).split(b"\0")[0]
            try:
                return int(raw.decode("latin-1").rstrip("KB").strip())
            except ValueError:
                fail("the arena row reads %r and should be digits and a KB - "
                     "the Memory page has never been painted (96.36.3)" % raw)

        # --- 2: THE MECHANISM, read out of mem_tab --------------------------
        # Done first because it is the one that names a routine: assertion 1
        # is a number, and a number is what the pin looks like from outside.
        pseg = pkg_seg(m)
        got = word("dos_akb")
        rows = claims(m)
        arena = [c for c in rows if c[1] // 64 >= got - 1 and c[3]]
        abase = arena[0][0] if arena else None
        walls = [c for c in rows
                 if c[3] and abase and c[0] < abase and c[2] == pseg]
        if walls:
            fails.append(
                "DOS holds %d PINNED claim(s) below its own arena: %s."
                " A claim is born pinned (SPEC.md 66.2) and dos_pkt_bufs takes"
                " its buffers BEFORE dos_run's compaction pass, so one left"
                " undeclared is a wall with a hole under it that the arena -"
                " which is ONE run - cannot reach (96.23.7.2). The"
                " declaration is `mov ax, dos_pkt_reloc / call dos_pkt_rloc`"
                " in dos_pkt_bufs's `.decl`"
                % (len(walls), ", ".join("%04X+%.1fK" % (c[0], c[1] / 64.0)
                                         for c in walls)))
        else:
            say("dosnetarena: no claim of DOS's is pinned below the arena")

        # --- 1: the promise IS the delivery, in both arms -------------------
        # **LET THE PROGRAM OUT FIRST.** An association open of a `.COM` runs
        # it (SPEC.md 54), so DOS arrives inside its own fsx bracket with
        # the program on the screen - no window and no page. DOSPKT.COM holds
        # on int 16h, so Enter is what ends it, and what happens next is a
        # whole bracket teardown: poll for the window rather than sleeping on
        # it. Without this the Setup click lands on the program's screen and
        # the arena row is read out of a page that has never been painted.
        m.hmp("sendkey ret")
        os88qemu.acted(m, lambda: pkg_seg(m) is not None, secs=30,
                       what="the DOS window", poll=0.5)
        eth.settle(m)
        mouse.click(*dosmap.centre(m, pkg_seg(m), dm, "dos_erect"))
        eth.settle(m)
        promised = arena_row()
        say("dosnetarena: Network TICKED - page ~%d K, program got %d K"
            % (promised, got))
        if abs(promised - got) > SLACK:
            fails.append(
                "with the card KEPT the page promised ~%d K and the program "
                "was handed %d, a drift of %d against a slack of %d. ~3 is "
                "dos_mem_arena not subtracting the packet buffers it is about "
                "to spend (96.23.7.1); ~40 is one of them pinned across the "
                "compaction pass (96.23.7.2)"
                % (promised, got, got - promised, SLACK))

        r4 = [u16(m.read(base() + dm["dos_mnet"] + 2 * i, 2)) for i in range(4)]
        mouse.click(r4[0] + 4, r4[1] + 5)
        eth.settle(m)
        if m.read(base() + dm["dos_mnet"] + 10, 1)[0]:
            fail("a press inside the Network box did not clear it - the box "
                 "is live on a machine with a card (SPEC.md 96.36.7)")
        promised2 = arena_row()

        mouse.click(*dosmap.centre(m, pkg_seg(m), dm, "dos_trect"))
        eth.settle(m)
        mouse.click(*dosmap.centre(m, pkg_seg(m), dm, "dos_rrect"))

        # Run is a relaunch: the card unmounted and the arena sized again.
        # Both are the guest's own words, so they are what is waited for; an
        # arena that does NOT move is the failure below, and then this spends
        # the eight seconds the old sleep did.
        def relaunched():
            sg = pkg_seg(m)
            return bool(sg) and not u16(m.read(drvrow, 2)) and \
                u16(m.read((sg << 4) + dm["dos_akb"], 2)) != got
        if os88qemu.acted(m, relaunched, secs=8, what="the relaunch",
                          poll=0.25):
            os88qemu.quiesce(m, lambda: word("dos_akb"), secs=0.25,
                             what="the relaunched arena")
        got2 = word("dos_akb")
        say("dosnetarena: Network CLEARED - page ~%d K, program got %d K"
            % (promised2, got2))
        if u16(m.read(eth.S("drv_tab") + eth.ETH_ROW * eth.DRVR_SZ
                      + eth.DRVR_SEG, 2)):
            fails.append(
                "the Network box was cleared and ETHER.DRV is STILL MOUNTED. "
                "dos_spmask is supposed to set 1<<DRVC_NET in "
                "OSAPI_DRV_SUSPEND's BL, which takes DRVC_NET off hbm_sweep's "
                "skip list (SPEC.md 51.11.4, 96.36.7.3)")
        if abs(promised2 - got2) > SLACK:
            fails.append(
                "with the card RELEASED the page promised ~%d K and the "
                "program was handed %d, a drift of %d against a slack of %d"
                % (promised2, got2, got2 - promised2, SLACK))
        if got2 <= got:
            fails.append(
                "releasing the card moved the program's arena %d -> %d. "
                "ETHER.DRV's image and its ring are ~33KB and the launch "
                "unmounts them before it sizes the arena, so they should come "
                "back (SPEC.md 96.35)" % (got, got2))

        for f in fails:
            say("dosnetarena: " + f)
        say("dosnetarena: %s" % ("FAILED" if fails else "ok"))
        return 1 if fails else 0
    finally:
        m.quit()


if __name__ == "__main__":
    sys.exit(main())
