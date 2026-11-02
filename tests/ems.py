#!/usr/bin/env python3
"""EMS.DRV, as a package sees it - SPEC.md 107.7.

    make emstest && python3 tests/ems.py [--leg board|none|both]

ON MARTYPC, which models a LO-TECH 2 MB EMS BOARD
(`os8088_5150_herc_hdd_sb_ems_gla`: registers 260h-263h, frame E000h) - an
8088 with expanded memory, the machine class this driver is for. Nothing here
is timed; every wait is on guest state.

LEG board (that machine):
  1. the driver's row is LOADED, its error DRVE_OK - the probe found the
     board at the first base of the first frame it tried;
  2. EMSTEST's first instance: IDENT, CAPS (128 pages, all free, frame
     E000h, four quarters free), ALLOC 3 -> handle 1, ALLOC 0 refused BAD,
     ALLOC past the board refused ROOM, FRAME all four (the recipe: 260h, a
     step of 1, nothing to OR), pages 0..2 mapped and signed through the
     frame, page 0 seen through quarter 3, BASE, page 1 mapped into quarter 3
     by the PACKAGE'S OWN OUT, and CAPS after (125 free, no quarter free);
  3. the frame READ OFF THE EMULATOR, not taken from the package: E000:0000
     holds page 0's signature and E000:C000 page 1's;
  4. a SECOND instance is refused the quarters (BUSY) and the first's handle
     (BAD), and its BX comes back DRVC_EMS:verb - the kernel handed the
     driver its instance slot in BH, and the driver put the class back;
  5. both closed WITHOUT freeing anything, then a THIRD instance finds every
     page and every quarter free again - EMSV_GONE, from xm_release_rec.
LEG pico (`os8088_5150_herc_hdd_sb_ems288_gla`): leg board's checks with the
  board where a PicoMEM puts its EMS by default - registers at 288h, the frame
  at D000h. The owner's 286 answered `No hardware found` with its PicoMEM
  there while the probe knew only 260h-26Ch; with 288h off the list this leg
  reproduces it (DRVE_HW).
LEG card (86Box, vm/xt-picomem1): leg board's checks against a REAL PicoMEM
  1.x's EMS - its own firmware (tools/picomem) - at 288h and E000h, 256
  pages. A first boot primes the card's config (its BIOS turns EMS off on a
  config whose memory map is not the machine's: MEASURED, DRVE_HW).
LEG none (`os8088_5150_herc_hdd_sb_gla`, the same machine with no board):
  the row is not loaded, its error DRVE_HW, and EMSTEST says nobody answered.

VERIFIED TO FAIL: with xm_release_rec's EMSV_GONE call taken out, step 5's
third instance is a LATER one (125 free); with drv_pkg_call_x's slot swap
taken out, step 4's FRAME and FREE are both ACCEPTED - every caller is
"class 7" and so every caller owns every handle and every quarter.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [HERE, os.path.join(ROOT, "tools")]
import os88build, os88ui, os88geom as geom                    # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

IMG = "build/emstest.img"
BOARD = "os8088_5150_herc_hdd_sb_ems_gla"
NONE = "os8088_5150_herc_hdd_sb_gla"
PICO = "os8088_5150_herc_hdd_sb_ems288_gla"  # the board at a PicoMEM's 288h
CARD_PAGES = 256                # leg card: 4 MB, an 8-bit page register
ROW_EMS = 6                     # drv_tab's row on kern_big (SPEC.md 107.1)
DRVR_SIZE, DRVR_SEG, DRVR_ERR = 16, 2, 14
DRVE_OK, DRVE_HW = 0, 5
EMSE_ROOM, EMSE_BAD, EMSE_BUSY = 1, 3, 4
DRVC_EMS, EMSV_IDENT = 7, 0
R = ("ICF IAX FREE0 TOTAL FRAME QFREE0 HND HCF A0 ABIG FRCF PORT STEP OR "
     "ALIAS BASE BLEN RECIPE FREE1 QFREE1 BUSY FREEX BX").split()


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


class Fail(Exception):
    pass


def want(bad, what, got, exp):
    ok = got == exp
    print("   %-44s %s%s" % (what, "ok " if ok else "BAD",
                             "" if ok else "  got %r, want %r" % (got, exp)))
    if not ok:
        bad.append(what)


def instance(ui, m, syms, before):
    """open EMSTEST.O88 once more: (window, phase, {name: word})"""
    w = ui.path("A:/EMSTEST.O88")
    if w.i in before:
        raise Fail("EMSTEST opened no NEW window")
    rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
    base = u16(rec, geom.W_SEG) << 4
    raw = m.read(base + syms["et_res"], 2 * len(R))
    res = dict(zip(R, struct.unpack("<%dH" % len(R), raw)))
    return w, m.read(base + syms["et_phase"], 1)[0], res


def row(ui, m):
    r = m.read(ui._S("drv_tab") + ROW_EMS * DRVR_SIZE, DRVR_SIZE)
    return u16(r, DRVR_SEG), r[DRVR_ERR]


def leg_board(syms, machine=BOARD, frame=0xE000, port=0x260, pages=128):
    bad = []
    print("\n== leg board: %s" % machine)
    with os88ui.boot(IMG, machine=machine, quick=True) as ui:
        m = ui.m
        seg, err = row(ui, m)
        want(bad, "EMS.DRV loaded", seg != 0, True)
        want(bad, "...its error", err, DRVE_OK)
        before = set(w.i for w in ui.windows())
        w1, ph, r = instance(ui, m, syms, before)
        want(bad, "first instance", ph, 1)
        want(bad, "IDENT", (r["ICF"], r["IAX"]), (0, 0x4D45))
        want(bad, "CAPS: pages free / on the board", (r["FREE0"], r["TOTAL"]),
             (pages, pages))
        want(bad, "CAPS: frame / quarters free", (r["FRAME"], r["QFREE0"]),
             (frame, 0x0F))
        want(bad, "ALLOC 3: CF / handle", (r["HCF"], r["HND"]), (0, 1))
        want(bad, "ALLOC 0 refused BAD", r["A0"], 0x100 | EMSE_BAD)
        want(bad, "ALLOC past the board refused ROOM", r["ABIG"],
             0x100 | EMSE_ROOM)
        want(bad, "FRAME 0Fh: CF / port / step / OR",
             (r["FRCF"], r["PORT"], r["STEP"], r["OR"]), (0, port, 1, 0))
        want(bad, "page 0 through quarter 3", r["ALIAS"], 1)
        want(bad, "BASE: first page / length", (r["BASE"], r["BLEN"]), (0, 3))
        want(bad, "the recipe: page 1 by the package's OUT", r["RECIPE"], 1)
        want(bad, "CAPS after: free / quarters free",
             (r["FREE1"], r["QFREE1"]), (pages - 3, 0))
        q0 = u16(m.read(frame << 4, 2))
        q3 = u16(m.read((frame << 4) + 0xC000, 2))
        want(bad, "%04X:0000 read off the machine (page 0)" % frame, q0,
             0xE500)
        want(bad, "%04X:C000 read off the machine (page 1)" % frame, q3,
             0xE501)
        w2, ph, r = instance(ui, m, syms, set(w.i for w in ui.windows()))
        want(bad, "second instance", ph, 2)
        want(bad, "...FRAME 1 refused BUSY", r["BUSY"], 0x100 | EMSE_BUSY)
        want(bad, "...FREE of the first's handle refused BAD", r["FREEX"],
             0x100 | EMSE_BAD)
        want(bad, "...BX back as it went", r["BX"],
             DRVC_EMS * 256 + EMSV_IDENT)
        ui.close(w2)
        ui.close(w1)
        w3, ph, r = instance(ui, m, syms, set(w.i for w in ui.windows()))
        want(bad, "third instance, after both closed", ph, 1)
        want(bad, "...every page and quarter was free",
             (r["FREE0"], r["QFREE0"]), (pages, 0x0F))
        want(bad, "...and it got handle 1 again", r["HND"], 1)
    return bad


def leg_none(syms):
    bad = []
    print("\n== leg none: %s" % NONE)
    with os88ui.boot(IMG, machine=NONE, quick=True) as ui:
        m = ui.m
        seg, err = row(ui, m)
        want(bad, "EMS.DRV not loaded", seg, 0)
        want(bad, "...its error", err, DRVE_HW)
        _, ph, r = instance(ui, m, syms, set(w.i for w in ui.windows()))
        want(bad, "EMSTEST: nobody answered", ph, 0xFF)
        want(bad, "...IDENT's CF", r["ICF"], 1)
    return bad


def leg_card(syms, vm="xt-picomem1"):
    """leg board's checks against a REAL PicoMEM 1.x's EMS: the card's own
    firmware under 86Box (tools/picomem, vm/xt-picomem1), its microSD set
    for EMS at 288h and E000h - E000h because this firmware's BIOS sits at
    D000h. What answers is the card's LTEMS port decode and its frame in
    PSRAM (dev_memory.cpp), so this is the field card's EMS as the driver
    meets it, not a model of a Lo-tech."""
    import subprocess
    import tempfile
    tmp = tempfile.mkdtemp(prefix="emscard-")
    box = [sys.executable, "tools/os88box.py"]
    # THE FIRST BOOT, as on a new card: its BIOS finds no config, maps the
    # machine (RAM, the VGA's ROM, its own at D000h) and saves PICOMEM.CFG
    # (08h). EMS set on a config whose map is not the machine's is turned
    # off again by the BIOS's "Memory change" check, so it is set on THAT.
    blank = os.path.join(tmp, "blank.img")
    subprocess.check_call([sys.executable, "tools/picomem/mksd.py", blank],
                          stdout=subprocess.DEVNULL)
    env = dict(os.environ, OS88_PMSD=blank, OS88_PMHOST_LOG="1")
    name = "emscard%d" % os.getpid()
    subprocess.check_call(box + ["launch", vm, "--name", name,
                                 "--fresh", "--temp", "--turbo"],
                          env=env, stdout=subprocess.DEVNULL)
    run = os.path.join(ROOT, "build/86box/run", name)
    try:
        for _ in range(30):
            subprocess.check_call(box + [name, "step", "2000"],
                                  stdout=subprocess.DEVNULL)
            if "cmd 08h done" in open(os.path.join(run, "86box.log")).read():
                break
        else:
            raise Fail("the card's first boot never saved its config (08h)")
    finally:
        subprocess.call(box + [name, "quit"], stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL)
    sd = os.path.join(tmp, "sd.img")
    subprocess.check_call([sys.executable, "tools/picomem/mksd.py", sd,
                           "--cfg-from", os.path.join(run, "picomem-sd.img"),
                           "--cfg", "EMS_Port=2", "--cfg", "EMS_Addr=1"],
                          stdout=subprocess.DEVNULL)
    os.environ.update(OS88_EMU="86box", OS88_86BOX_VM=vm, OS88_PMSD=sd)
    return leg_board(syms, "os8088_xt_vga", 0xE000, 0x288, CARD_PAGES)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", choices=("board", "none", "pico", "card", "card2", "both"),
                    default="both")
    a = ap.parse_args()
    os.chdir(ROOT)
    if not os.path.exists(os88build.at(IMG)):
        sys.exit("ems: no %s - run `make emstest`" % IMG)
    syms, image = pkg_syms("tests/emstest/emstest.asm", ("apps/",))
    if open(os88build.at("build/emstest.bin"), "rb").read() != image:
        sys.exit("ems: build/emstest.bin is behind the tree - `make emstest`")
    bad = []
    try:
        if a.leg in ("board", "both"):
            bad += leg_board(syms)
        if a.leg in ("none", "both"):
            bad += leg_none(syms)
        if a.leg == "pico":
            bad += leg_board(syms, PICO, 0xD000, 0x288)
        if a.leg == "card":
            bad += leg_card(syms)
        if a.leg == "card2":
            bad += leg_card(syms, "xt-picomem2")
    except Fail as e:
        bad.append(str(e))
    if bad:
        print("\nems: FAIL - %s" % "; ".join(bad))
        sys.exit(1)
    print("\nems: ok")


if __name__ == "__main__":
    main()
