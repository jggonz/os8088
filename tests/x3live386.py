#!/usr/bin/env python3
"""x3live386 - XT-MP3 plays LIVE on a 386DX-25 under kern_mega, as it does
under kern_big on the same machine (KERN-MEGA-PLAN 14.150, SPEC.md 121.5).

    make && make mega && python3 tests/x3live386.py [--apps IMG] [--vm NAME]

86Box BY NAME: the subject is a MEASURE of the guest's own CPU, which Bochs
(a 4M-instruction guest, docs/BOCHS.md) and MartyPC (an 8088) cannot give,
and the defect was a protected-mode one that 86Box's fault log prints. The
machine is vm/386-mega-cdrom (a Micronics 386DX-25, OTI-067, SB16 on IRQ 5)
with its seeded CMOS (nvr-seed/), build/mega.img in A: and
build/megaxtmp3.img - kern_mega's XTMP3 disk, the PKG_MEGA arm and its songs - in B:.

XT-MP3's Auto (x3eng.inc x3l_run) MEASURES each rung on the worker and plays
live on one that keeps 1.25 audio seconds a guest second. On this machine
Quarter STANDARD measures 0.51 under both kernels, and XT Standard decides:
1.79 under kern_big, so it plays live. Under kern_mega the shipped XT part
wrote its state through CS, ~140 stores a granule that kern_mega completes
as a #GP each (mega_gpw), and XT Standard measured 0.91 - so the same song
went to "Decode First". The kern_mega arm's XT part (x3xt.asm -DPKG_MEGA)
stores through its data selector instead, and measures 1.77.

  1. NEONDRV.MP3 opened from B:\\MEDIA: Auto decides to play LIVE (x3_md =
     MD_LIVE) on XT Standard ([x3l_choice] = X3Q_XTSTD), measured at
     1.25 or better ([x3l_meas]);
  2. it decodes: granules and frames counting up, off the guest;
  3. the fault log charges NOTHING to a loaded image from the open on: no
     exception at a CS that is not the kernel's (0060, 0028, `.mega`'s
     MEGA_SEL_CODE) or the BIOS's. (Closing XT-MP3 logs api_x.done's #GP
     at 0060:0525, the kernel's own; the row reads before the close.)

THE FAILING CASE (docs/WRITING-TESTS.md 1): `--apps` with an apps disk that
carries the XT-MP3 of before 14.150 (the shipped XT part in the kern_mega
arm) fails 1 and 3 - Decode First, XT Standard measured 91, and 3,474
#GP lines at the part's selector (114C:1FFB `inc word [cs:x3x_slot]` and
its neighbours). Seen when the row was written.

It reads build/ and runs the machine in a run directory of its own.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
os.environ["OS88_MEGA"] = "1"
os.environ["OS88_MEGA_EMU"] = "86box"
os.environ.setdefault("OS88_MEGA_SND", "sb16:5")
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]

MD_LIVE = 2                     # x3face.inc's x3_md: MD_IDLE, MD_CHECK, MD_LIVE
MD_CHOOSE = 5                   # ...the "Decode First" dialog
X3Q_XTSTD = 4                   # x3face.inc's rungs
X3_SAFETY = 125                 # x3eng.inc: a rung passes at 1.25


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--apps", default="build/megaxtmp3.img",
                    help="the B: disk (kern_mega's XTMP3 disk)")
    ap.add_argument("--vm", default="386-mega-cdrom")
    a = ap.parse_args()
    os.environ["OS88_MEGA_VM"] = a.vm
    os.chdir(ROOT)
    import os88marty as om
    import os88sym
    import os88ui
    for f in ("build/mega.img", a.apps, "build/megak/apps/xtmp3.map"):
        if not os.path.exists(f):
            sys.exit("x3live386: %s is not built - `make mega`" % f)
    sym = os88sym._parse_map("build/megak/apps/xtmp3.map")[0]
    was = os.environ.get("OS88_BUILD")
    os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
    meq = os88sym.equates(("KERN_MEGA",))     # (megacdrom's _mega_equates)
    if was is None:
        os.environ.pop("OS88_BUILD")
    else:
        os.environ["OS88_BUILD"] = was
    kcs = {0x0060, 0x0028, meq["MEGA_SEL_CODE"]}
    fails = []

    def want(ok, what):
        print("   %s  %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            fails.append(what)
        return ok

    def image_faults(lines):
        bad = []
        for l in lines:
            t = l.split()
            if len(t) > 1 and t[0].startswith("#") and ":" in t[1]:
                cs = int(t[1].split(":")[0], 16)
                if cs < 0xF000 and cs not in kcs:
                    bad.append(l.strip())
        return bad

    with os88ui.boot("build/mega.img", apps=a.apps, machine="at286_vga_sb",
                     label="x3live386") as ui:
        f0 = len(ui.m.faults())
        w = ui.path("B:/MEDIA/NEONDRV.MP3")

        def st():
            md, meas, ch, gr, fr = om.pkg_reads(ui.m, w, [
                (sym["x3_md"], 1), (sym["x3l_meas"], 16),
                (sym["x3l_choice"], 1), (sym["x3_grans"], 2),
                (sym["x3_frames"], 2)])
            return dict(md=md[0], meas=struct.unpack("<8H", meas),
                        choice=ch[0], grans=struct.unpack("<H", gr)[0],
                        frames=struct.unpack("<H", fr)[0])
        s, g0 = None, None
        for _ in range(16):             # Auto's decision: ~15 guest seconds
            ui.m.b.cmd("step 2000")
            s = st()
            if s["md"] == MD_CHOOSE:
                break
            if s["md"] == MD_LIVE:
                if g0 is None:
                    g0 = s
                elif s["frames"] > g0["frames"] + 20:
                    break
        new = ui.m.faults()[f0:]
        print("   Auto: md %d, rung %d, measured %s; granules %d frames %d"
              % (s["md"], s["choice"], s["meas"], s["grans"], s["frames"]))
        want(s["md"] == MD_LIVE and s["choice"] == X3Q_XTSTD,
             "1: Auto plays NEONDRV.MP3 LIVE on XT Standard (md %d, rung %d)"
             % (s["md"], s["choice"]))
        want(s["meas"][X3Q_XTSTD] >= X3_SAFETY and
             s["meas"][X3Q_XTSTD] != 0xFFFF,
             "1: XT Standard measured %d, 125 or better"
             % s["meas"][X3Q_XTSTD])
        want(g0 is not None and s["frames"] > g0["frames"]
             and s["grans"] > g0["grans"],
             "2: granules and frames counting (%s -> %d, %d)"
             % (g0 and (g0["grans"], g0["frames"]), s["grans"], s["frames"]))
        bad = image_faults(new)
        sites = {}
        for l in bad:
            k = " ".join(l.split()[:2])
            sites[k] = sites.get(k, 0) + 1
        want(not bad, "3: no exception charged to a loaded image (%d at %d "
             "sites, the most: %s)" % (len(bad), len(sites), sorted(
                 sites.items(), key=lambda kv: -kv[1])[:6]))
    print("x3live386: %s" % ("ok" if not fails else
                             "FAILED: " + "; ".join(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
