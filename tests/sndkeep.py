#!/usr/bin/env python3
"""SPEC.md 34.15.1 / 51.1.3: SOUND.DRV hands back what this machine cannot run.

    make covoxtest mixertest && python3 tests/sndkeep.py

Two legs, one per answer the attach can give:

  nomix  the Covox 5150 (os8088_5150_herc_covox_720_gla): SOUND.DRV attaches
         on the parallel port alone, so mix_probe never runs and [mix_kind] is
         0. The attach must write snd_keep into the header's DRV_H_KEEP and the
         kernel must shrink the image's claim to it - DRVR_KB and the mem_tab
         record both ceil(snd_keep / 1024) KB - with the Covox tier still
         published and SND_CAP_MIXER clear.
  mix    the Sound Blaster 5150 (os8088_5150_sb_gla), whose card has a mixer:
         DRV_H_KEEP is snd_keepmix and the claim ceil(snd_keepmix / 1024) KB -
         the popup and the mixer verbs kept, the attach-only code past
         snd_keepmix handed back, SND_CAP_MIXER published.

Break it on purpose: take the `mov [DRV_H_KEEP], ax` out of sound.asm, or
the `call %2 / dw mem_regrow` out of driver.inc's %1_att, and both legs go
red at the image's full 11KB; make the attach write snd_keep unconditionally
and mix goes red at 7KB.
"""
import argparse
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "tests")]
import os88marty as M
import os88ui
import os88drvsym
import heapmap

LEGS = {
    "nomix": ("os8088_5150_herc_covox_720_gla", "build/covoxsys720.img"),
    "mix": ("os8088_5150_sb_gla", "build/mixer360.img"),
}
DRVR_SEG, DRVR_KB = 2, 8                # kernel/driver.inc: a drv_tab row
DRV_H_KEEP, DRV_H_IMG = 4, 8            # ...and a .drv header
DSV_CAPS = 0                            # drivers/os88drv.inc
SND_CAP_MIXER = 0x0200                  # kernel/snd.inc
SND_CAP_LPTDAC = 0x0100                 # apps/os88api.inc


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def fail(msg):
    print("FAIL %s" % msg, flush=True)
    sys.exit(1)


def leg(name, keep, keepmix):
    machine, img = LEGS[name]
    with os88ui.boot(img, machine=machine, quick=True) as ui:
        m = ui.m
        sym = {n: m.sym(n) for n in ("mem_base", "mem_top", "spl_live",
                                     "mem_tab")}

        def sample():
            # PAUSED, because the image MOVES: the boot's last compaction
            # packs the ceiling (SPEC.md 66.9) and a running guest can carry
            # the claim away between reading the row and reading the map
            m.pause()
            try:
                row = u16(m.read(m.sym("drv_owner"), 2))
                if not row:
                    fail("%s: SOUND.DRV did not attach" % name)
                seg = u16(m.readseg(M.KERNEL_SEG, row + DRVR_SEG, 2))
                kb = u16(m.readseg(M.KERNEL_SEG, row + DRVR_KB, 2))
                hdr = bytes(m.readseg(seg, 0, 16))
                caps = u16(m.read(m.sym("drv_svc") + DSV_CAPS, 2))
                hm = heapmap.Map(m, sym)
            finally:
                m.go()
            return seg, kb, hdr, caps, hm

        got = {}

        def settled(_):
            got["s"] = sample()
            seg, hm = got["s"][0], got["s"][4]
            return [c for c in hm.claims if c.seg == seg]
        M.until(m, settled, "a mem_tab record at the driver's base",
                limit=60.0, guest=5.0)
        seg, kb, hdr, caps, hm = got["s"]
        said, image = u16(hdr, DRV_H_KEEP), u16(hdr, DRV_H_IMG)
        c = [c for c in hm.claims if c.seg == seg][0]
        above = [r for r in hm.runs() if r[0] == c.end]
        print("      %s: image %d bytes at %04X, DRV_H_KEEP %d, DRVR_KB %d, "
              "claim %.1fK, caps %04X, free run above it %s"
              % (name, image, seg, said, kb, c.kb, caps,
                 "%.1fK" % (above[0][1] / 64.0) if above else "none"),
              flush=True)
        full = math.ceil(image / 1024)
        if name == "nomix":
            want = math.ceil(keep / 1024)
            if caps & SND_CAP_MIXER:
                fail("nomix: SND_CAP_MIXER published on a machine with no card")
            if not caps & SND_CAP_LPTDAC:
                fail("nomix: the Covox tier did not publish - the driver "
                     "that kept 8KB is not the driver that works")
            if said != keep:
                fail("nomix: DRV_H_KEEP %d, want snd_keep %d - the attach did "
                     "not disown its tail" % (said, keep))
            if want >= full:
                fail("nomix: snd_keep %d rounds to %dK, the whole image's %dK "
                     "- nothing to give back" % (keep, want, full))
        else:
            want = math.ceil(keepmix / 1024)
            if not caps & SND_CAP_MIXER:
                fail("mix: SND_CAP_MIXER clear on the Sound Blaster machine - "
                     "the leg tests nothing")
            if said != keepmix:
                fail("mix: DRV_H_KEEP %d, want snd_keepmix %d - with a mixer "
                     "only the attach-only code may go, never the popup"
                     % (said, keepmix))
        if kb != want or c.para != want * 64:
            fail("%s: DRVR_KB %d and the claim %.1fK, want %dK"
                 % (name, kb, c.kb, want))
        print("PASS %s: SOUND.DRV holds %dK of a %dK image%s"
              % (name, want, full,
                 ", the mixer, the popup and the attach-only code handed back"
                 if name == "nomix" else
                 ", the popup kept and the attach-only code handed back"),
              flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", choices=sorted(LEGS), action="append")
    a = ap.parse_args()
    sym = os88drvsym.symbols("sound.bin")
    for name in a.leg or sorted(LEGS, reverse=True):
        leg(name, sym["snd_keep"], sym["snd_keepmix"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
