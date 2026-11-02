#!/usr/bin/env python3
"""THE PICOMEM UNDER MARTYPC, ITS OWN FIRMWARE ON A 4.77 MHz 8088
(docs/plans/completed/PICOMEM-EMU-PLAN.md wave 8).

    make pmboxtest marty && tools/picomem/build.sh && python3 tests/pmmarty.py [pm1] [pm2]

tools/martypc/patches/17-picomem.patch puts libpmhost - Elendilon/ISA-PicoMEM
pm1x-pr, compiled for the host - into MartyPC as a card: it answers the ports
nobody else does, hears every write (how a 1.x snoops the 8237), maps
D0000-EFFFF with its firmware's own wait states, and its card time is the
8088's. os8088_5150_herc_pm1_gla carries a 1.x on IRQ 5 and _pm2_gla a 2.x
jumpered as 5150 #2's is, IRQ 7 and DRQ 1. Each leg boots build/pmsys360.img
(SOUND, ETHER and USBMOUSE wanted) with build/pmboxb360.img in B: and asserts:

  1. the card's BIOS ran its POST (30h, DEV_Init 09h) and SOUND.DRV attached
     in SPEC.md 34.10's order: 78h 0, 73h 1, 77h, 78h 220h, all answered 0;
  2. SBTEST's 2 s of a 1 kHz square came out of the card's own DAC - on the
     1.x by its VIRTUAL DMA (the 8237 writes snooped, every block copied by
     the 8088 in the card's ROM), on the 2.x by REAL DMA from the 8237;
  3. pm_runaway (SPEC.md 34.10.5) did NOT mask channel 1: [sbl_drqmask] is 0
     after the stream. Its 32-byte threshold was set from the field 286; this
     is the first time it has met a card's own DMA pacing on the 8088 the
     project is calibrated against, and a 2.x whose channel it masked would
     have stopped playing, which (2) would also see.

Broken on purpose (docs/WRITING-TESTS.md 1): OS88_PMHOST_NOSNOOP=1, the 1.x
deaf to the 8237, plays nothing - (2) FAILS on pm1 and pm2 is untouched,
the 2.x having no snoop to lose (both MEASURED).
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [HERE, os.path.join(ROOT, "tools")]

FAILS = []
SND_ROW, DRVR_SZ, DRVR_SEG = 0, 16, 2


def check(cond, what):
    print(("  ok    " if cond else "  FAIL  ") + what, flush=True)
    if not cond:
        FAILS.append(what)


def leg(board, tmp):
    import os88drvsym
    import os88marty
    import os88ui
    import pmbox
    print("pmmarty: the PicoMEM %d.x on os8088_5150_herc_pm%d_gla" % (board, board))
    wav = os.path.join(tmp, "pm%d.wav" % board)
    os.environ.update(OS88_PMHOST_LOG="1", OS88_PMWAV=wav)
    syms = os88drvsym.symbols("sound.bin")
    try:
        with os88ui.boot(os.path.join(ROOT, "build/pmsys360.img"),
                         apps=os.path.join(ROOT, "build/pmboxb360.img"),
                         machine="os8088_5150_herc_pm%d_gla" % board,
                         quick=True) as ui:
            m = ui.m
            cmds = pmbox.log_cmds(open(os.path.join(m.run_dir, "martypc.log"),
                                       errors="replace").read())
            names = [c for c, _, _, _ in cmds]
            att = [x for x in cmds if x[0] in (0x73, 0x77, 0x78)]
            check(0x30 in names and 0x09 in names
                  and names.index(0x30) < names.index(0x09),
                  "the card's BIOS ran its POST (30h, then DEV_Init 09h)")
            check(len(att) >= 4 and att[0][:2] == (0x78, 0)
                  and att[1][:2] == (0x73, 1) and att[2][0] == 0x77
                  and att[3][:2] == (0x78, 0x220)
                  and all(x[3] & 0xFF == 0 for x in att[:4]),
                  "SOUND.DRV: 78h 0, 73h 1, 77h, 78h 220h, all answered 0 (%s)"
                  % ["%02Xh %04Xh=%04Xh" % (c, g, r) for c, g, s, r in att[:4]])
            w = ui.path("B:/SBTEST.O88")
            x, y, ww, hh = ui._rect(w.i)
            ui.mo.click(x + ww // 2, y + hh // 2, settle=0)
            os88marty.pace(m, 4.0)
            seg = struct.unpack("<H", bytes(m.read(
                m.sym("drv_tab") + SND_ROW * DRVR_SZ + DRVR_SEG, 2)))[0]
            mask = m.read(seg * 16 + syms["sbl_drqmask"], 1)[0]
            check(seg and mask == 0, "pm_runaway left channel 1 alone "
                  "(sbl_drqmask %d)" % mask)
    except (os88marty.MartyError, os88ui.UIError) as e:
        check(False, "the machine: %s" % str(e).split(". ")[0])
    pmbox.FAILS = FAILS
    pmbox.check = check
    pmbox.check_tone(wav)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("legs", nargs="*", default=["pm1", "pm2"])
    a = ap.parse_args()
    for f in ("build/pmsys360.img", "build/pmboxb360.img",
              "build/picomem/v1/libpmhost-v1.so",
              "build/picomem/v2/libpmhost-v2.so"):
        if not os.path.exists(os.path.join(ROOT, f)):
            print("SKIP: %s is not built (make pmboxtest; tools/picomem/build.sh)" % f)
            return 0
    import tempfile
    with tempfile.TemporaryDirectory(prefix="pmmarty-") as tmp:
        for l in a.legs:
            leg(int(l[-1]), tmp)
    if FAILS:
        print("pmmarty: FAIL (%d)" % len(FAILS))
        return 1
    print("pmmarty: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
