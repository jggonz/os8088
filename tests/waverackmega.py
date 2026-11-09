#!/usr/bin/env python3
"""WaveRack on kern_mega: the HOLD, and the instance's own slot
(SPEC.md 123.6, 123.7, 123.13; docs/plans/KERN-MEGA-PLAN.md 14.149).

    python3 tests/waverackmega.py                 the row
    python3 tests/waverackmega.py --pkg F.O88 --src TREE
                                                  run another WAVERACK.O88,
                                                  read by TREE's source (the
                                                  red leg: the base's)

WaveRack has no PKG_MEGA arm: the shipped package runs on kern_mega as it
is, and it declares OS88_HIGH and OS88_HIGHMEM (KERN-MEGA-PLAN 14.159), so
its REGION and every claim of 64KB or less but one kind is in the pool
ABOVE 1MB there - the I/O claim, which wo_ioclaim keeps conventional
(MEMP_CONV) because every read of the document lands in it. The DMA rings
are in the pool too since KERN-MEGA-PLAN 14.160: the DMA door gives an
OS88_HIGHMEM package's claim with a head a pool block below 16MB whose head
lies inside one 64KB page, which SOUND.DRV's 8237 reaches (sbl_ring_phys
reads the base off the descriptor, page and all). Under CPU_F_PM every claim is an LDT
SELECTOR wherever it is (KERN-MEGA-MEMORY-PLAN MC7), and two places were
looked at for treating a segment as a paragraph:

  HOLD   wt_hoff addressed the playback HOLD - the region loaded first, one
         8-bit byte a frame, when the medium cannot keep up with the play
         (SPEC.md 123.6) - as [wt_hseg] + offset >> 4. A selector plus a
         paragraph count names some other descriptor, so on the 86Box 286
         a 16-bit stereo clip (PRELUDE.WAV, which a floppy cannot stream)
         closed WaveRack with "WaveRack 0D at <sel>:<wt_hoff>" at its
         first load pass. wt_hbwin now takes a BOUND WINDOW over the claim
         under CPU_F_PM (OSAPI_SEG SEG_BWIN) and wt_hoff re-aims it (SEG_AIM);
         a real-mode kernel keeps the sum. The leg plays PRELUDE.WAV and
         asserts the play TOOK the hold, under a window, loaded all of it,
         that every byte of the hold is tools/wrsim.py's hold_bytes() for
         the region (read through the claim's descriptor), that the play
         ran to its end, and that the window is still WaveRack's.
  SLOT   wc_myslot finds the instance's row of OSAPI_SYS_SNAPSHOT by
         comparing SSI_SEG with DS - a value compared, never added to, so it
         holds on kern_mega exactly when the loader stores the region's DATA
         selector there, which is the package's DS. Nothing had run it
         (tests/megahigh.txt's note). The leg records a take at 8,000 Hz and
         reads [wc_slot] - wc_mark1's wc_myslot, the one the take's
         WAVERACK.TAK entry is stamped with - against the kernel's inst_tab:
         it must be the WaveRack row whose I_SPTR is the window's segment,
         never FFh, which would leave the take's entry naming no recorder
         and Recover Take offering another window's live take.

  WHERE  the placement itself, read off the descriptors (m.selector: the
         LDT entry's base), never off the class byte: in both legs the
         REGION's selector is based at or above 1MB; in HOLD the hold, the
         look-ahead, the waveform band, the view cache and the envelope are
         too, and the I/O claim is below 640KB; in SLOT the take's staging
         units are above 1MB, the record ring too - below 16MB, all of it
         inside one 64KB page (14.160), and the card RECORDS into it, the
         take written whole from the units (no write failed, [wc_wrote] >
         0). So the
         row fails if the package stops declaring either bit, if a claim it
         makes stops reaching the pool, if the I/O claim drifts up - and,
         with everything high, if any of the code above takes a sum on a
         selector, the hold's above all.

86Box's at286_vga_sb (the 286 with a Sound Blaster, 4MB), build/mega.img in
A: and a scratch B: of the package and the clip (OS88_MEGA_EMU=86box).

RED (docs/WRITING-TESTS.md 1), measured: `--pkg` the base's WAVERACK.O88
with `--src` the base tree - HOLD fails, the window gone at the load
(14.149's base). And on 14.159's tree, each broken on purpose and rebuilt
(`--pkg`/`--src` a scratch copy): the class byte without OS88_HIGH - WHERE's
region checks fail; without OS88_HIGHMEM - every claim check fails;
wo_ioclaim's MEMP_CONV taken out - the I/O claim check fails; wt_hoff
taking the paragraph sum always (14.149's defect, now on a pool claim) -
HOLD fails, WaveRack closed at the load; wc_myslot comparing SSI_SEG with
CS instead of DS (a real-mode identity: CS = DS for a package there) -
SLOT fails, [wc_slot] FFh.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.environ.setdefault("OS88_MEGA", "1")
os.environ.setdefault("OS88_MEGA_EMU", "86box")
import os88build  # noqa: E402
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402
import dispapps  # noqa: E402
import wrsim  # noqa: E402
from cycweb import pkg_syms  # noqa: E402

MACHINE = "at286_vga_sb"
CLIP = "PRELUDE.WAV"
DEMO = os.path.join(ROOT, "apps", "waverack", "demo")
WT_IDLE, WT_PLAY, WT_REC = 0, 1, 3
I_STATE, I_SPTR, I_RECSZ = 0, 6, 32
WS_ENV, WS_USED = 24, 27                # wrdoc.inc's source record
ONE_MB, CONV_TOP = 0x100000, 0xA0000

fails = []


def check(what, ok, more=""):
    print("%s %s%s" % ("ok  " if ok else "FAIL", what, (" - " + more) if more else ""))
    if not ok:
        fails.append(what)
    return ok


class Rack(object):
    """the WaveRack window's own variables, read through its selector"""

    def __init__(self, m, syms):
        self.m, self.s = m, syms

    def seg(self):
        got = dispapps.pkg_seg(self.m, 0)
        return got and int(got[1])

    def rd(self, name, n, off=0):
        seg = self.seg()
        if not seg:
            return None
        return int.from_bytes(self.m.readseg(seg, self.s[name] + off, n), "little")

    def base(self, sel):
        """the linear base of selector SEL, off its descriptor (None: none)"""
        d = self.m.selector(sel) if sel else None
        return d and d[0]


def where(rk, what, sel, high):
    """WHERE: selector SEL (a claim of WaveRack's, or its region) is based at
    or above 1MB (HIGH) or below 640KB (not HIGH) - the descriptor's base"""
    b = rk.base(sel)
    if high:
        ok = b is not None and b >= ONE_MB
        side = "at or above 1MB"
    else:
        ok = b is not None and b < CONV_TOP
        side = "conventional"
    return check("WHERE: %s %s" % (what, side), ok,
                 "selector %04X, base %s" % (sel or 0, "none" if b is None else "%06X" % b))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--pkg", help="a WAVERACK.O88 to run instead of build/'s")
    ap.add_argument("--src", default=ROOT,
                    help="the tree whose apps/waverack/ the package was built from")
    ap.add_argument("--arm", action="store_true",
                    help="waverackmega-arm: kern_mega's own arm (build/megak/apps/"
                    "waverack.o88, PKG_MEGA), whose hold is one huge claim")
    a = ap.parse_args()
    bin_ = "build/megak/apps/waverack.bin" if a.arm else "build/waverack.bin"
    pkg = a.pkg or os88build.at("build/megak/apps/waverack.o88" if a.arm
                                else "build/waverack.o88")
    syms, img = pkg_syms(os.path.join(a.src, "apps/waverack/waverack.asm"),
                         (os.path.join(a.src, "apps") + "/",
                          os.path.join(a.src, "apps/waverack") + "/",
                          os.path.join(a.src, "build") + "/"),
                         ("PKG_MEGA",) if a.arm else ())
    if not a.pkg and img != open(os88build.at(bin_), "rb").read():
        sys.exit("waverackmega: %s is not this source's - make %s"
                 % (bin_, "mega" if a.arm else "waverackdisk"))
    data = open(os.path.join(DEMO, CLIP), "rb").read()
    r = wrsim.parse(data)
    tmp = tempfile.mkdtemp(prefix="wrmega-")
    try:
        disk = os.path.join(tmp, "wr.img")
        shutil.copyfile(pkg, os.path.join(tmp, "WAVERACK.O88"))
        subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                               "-o", disk, "--size", "1440",
                               os.path.join(tmp, "WAVERACK.O88"),
                               "MEDIA:" + os.path.join(DEMO, CLIP),
                               "--folder", "SYSTEM/APPDATA"], stdout=subprocess.DEVNULL)
        for leg in (leg_hold, leg_slot):    # a boot each: the red leg's
            with os88ui.boot(os88build.at("build/mega.img"), apps=disk,
                             machine=MACHINE, label="wrmega") as ui:
                m = ui.m                    # HOLD takes the window with it
                w = ui.path("B:/WAVERACK.O88", limit=120)
                rk = Rack(m, syms)
                M.guest_sleep(m, 2)
                if leg is leg_hold:
                    leg_hold(ui, w, rk, data, r)
                else:
                    leg_slot(ui, w, rk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("waverackmega: %s" % ("PASS" if not fails else "FAIL (%s)" % ", ".join(fails)))
    return 1 if fails else 0


def leg_slot(ui, w, rk):
    """Record (8,000 Hz, a floppy's rate): wc_mark1's wc_myslot, with the
    instance published - at launch the entry proc's wc_probe runs before the
    loader makes the record live, so FFh there is right on every kernel"""
    m = ui.m
    if not check("SLOT: the window is there to record in", rk.seg() is not None):
        return
    ui.raise_window(w)
    for _ in range(6):
        sm = [mm for mm in ui.menus() if mm[0] == "Sound"][0][3]
        if [on for i, on in sm if i.startswith("Record")][0]:
            break
        ui.menu_pick("Sound", [i for i, on in sm if i.startswith("Rate")][0])
        M.guest_sleep(m, 1)
    m.key("KeyR")
    for _ in range(40):
        M.guest_sleep(m, 0.5)
        if rk.rd("wt_state", 1) == WT_REC:
            break
    if not check("SLOT: Record starts a take", rk.rd("wt_state", 1) == WT_REC):
        return
    seg = rk.seg()
    where(rk, "SLOT: the region", seg, True)
    where(rk, "SLOT: the take's first staging unit", rk.rd("wc_useg", 2), True)
    rsel = rk.rd("wc_rseg", 2)
    where(rk, "SLOT: the record ring (the DMA door's, 14.160)", rsel, True)
    d = m.selector(rsel) if rsel else None
    check("SLOT: the record ring is below 16MB and inside one 64KB page",
          d is not None and d[0] + d[1] < 0x1000000
          and d[0] >> 16 == (d[0] + d[1]) >> 16,
          "base, limit = %s" % (d and "%06X, %04X" % d))
    tab = m.read(m.sym("inst_tab"), 16 * I_RECSZ)
    want = None
    for i in range(16):
        rec = tab[i * I_RECSZ:(i + 1) * I_RECSZ]
        if rec[I_STATE] == 1 and int.from_bytes(rec[I_SPTR:I_SPTR + 2], "little") == seg:
            want = i
            break
    got = rk.rd("wc_slot", 1)
    check("SLOT: wc_myslot names this instance's row (%s, I_SPTR %04X = the window's "
          "segment)" % (want, seg or 0), want is not None and got == want,
          "[wc_slot] = %r" % got)
    m.key("KeyS")
    for _ in range(40):
        M.guest_sleep(m, 0.5)
        if rk.rd("wt_state", 1) == WT_IDLE:
            break
    check("SLOT: Stop ends the take", rk.rd("wt_state", 1) == WT_IDLE)
    check("SLOT: the take was written from the units above 1MB",
          rk.rd("wc_dead", 1) == 0 and (rk.rd("wc_wrote", 4) or 0) > 0,
          "[wc_dead] = %r, [wc_wrote] = %r" % (rk.rd("wc_dead", 1), rk.rd("wc_wrote", 4)))


def leg_hold(ui, w, rk, data, r):
    m = ui.m
    ui.raise_window(w)
    ui.menu_pick("File", "Open...")
    names = [row[0] for row in ui.listing(ui.chooser())]
    ui.chooser_open(CLIP if CLIP in names else "MEDIA/" + CLIP)
    # the scan: the document is open when the status says its length
    for _ in range(60):
        M.guest_sleep(m, 1)
        if rk.rd("we_busy", 1) == 0 and rk.rd("wo_rate", 2):
            break
    m.key("Space")
    held = win = 0
    for _ in range(60):                     # the load, then the play
        M.guest_sleep(m, 0.5)
        st = rk.rd("wt_state", 1)
        if st is None:
            break
        held = held or rk.rd("wt_held", 1)
        if "wt_hwin" in rk.s:               # (the base has none)
            win = win or rk.rd("wt_hwin", 2)
        if held and rk.rd("wt_hload", 1) == 0 and st == WT_PLAY:
            break
    alive = rk.seg() is not None and any("WaveRack" in t for t in ui.titles())
    if not check("HOLD: WaveRack is still open after the load", alive,
                 "titles %s, toast %r" % (ui.titles(), ui.toast())):
        return
    check("HOLD: the play took the hold", held == 1)
    where(rk, "HOLD: the region", rk.seg(), True)
    where(rk, "HOLD: the hold (wt_hseg)", rk.rd("wt_hseg", 2), True)
    where(rk, "HOLD: the look-ahead (wt_laseg)", rk.rd("wt_laseg", 2), True)
    where(rk, "HOLD: the waveform band (wr_bseg)", rk.rd("wr_bseg", 2), True)
    where(rk, "HOLD: the view cache (we_vseg)", rk.rd("we_vseg", 2), True)
    env = [rk.rd("wo_src", 2, i * 64 + WS_ENV) for i in range(4)
           if rk.rd("wo_src", 1, i * 64 + WS_USED)]
    check("HOLD: one live source", len(env) == 1, "envelopes %s" % env)
    if env:
        where(rk, "HOLD: its envelope (WS_ENV)", env[0], True)
    where(rk, "HOLD: the I/O claim (wo_ioseg, MEMP_CONV)", rk.rd("wo_ioseg", 2), False)
    if "wt_hwin" in rk.s:
        check("HOLD: under a bound window (wt_hbwin)", win != 0, "[wt_hwin] = %04X" % (win or 0))
    lo = rk.rd("wt_hlo", 4)
    hi = rk.rd("wt_hhi", 4)
    pos = rk.rd("wt_hpos", 4)
    hseg = rk.rd("wt_hseg", 2)
    check("HOLD: loaded whole, frames %d to %d" % (lo, hi), pos == hi and hi > lo + 16,
          "[wt_hpos] = %d" % pos)
    got = b""                               # window by window: the arm's
    step = 0x1000                           # hold is a huge claim (wt_hw's
    if "wt_hw" in rk.s:                     # step, os88huge.inc)
        step = rk.rd("wt_hw", 2, 2) or 8
    k = 0
    while hseg and len(got) < hi - lo:
        got += m.readseg(hseg + step * k, 0, min(0x10000, hi - lo - len(got)))
        k += 1
    want = wrsim.hold_bytes(data, r, lo, hi)
    bad = next((i for i in range(min(len(got), len(want))) if got[i] != want[i]), None)
    check("HOLD: its %d bytes are wrsim.hold_bytes()" % len(want),
          got == want, "first difference at frame %s" % bad)
    for _ in range(60):                     # ...and the play to its end
        M.guest_sleep(m, 0.5)
        if rk.rd("wt_state", 1) == WT_IDLE:
            break
    played = (rk.rd("wt_played", 4) or 0) * (rk.rd("wt_d", 1) or 1)
    check("HOLD: the play ran to its end (%d frames played of %d)" % (played, hi - lo),
          rk.rd("wt_state", 1) == WT_IDLE and played >= hi - lo)
    check("HOLD: the window is still WaveRack's at the end",
          any("WaveRack" in t for t in ui.titles()), "titles %s" % ui.titles())


if __name__ == "__main__":
    sys.exit(main())
