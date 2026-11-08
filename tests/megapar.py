#!/usr/bin/env python3
"""THE REAL-MODE BRACKET'S PARITY with kern_big: what the Video Battle and the
Video Player hold, measured on BOTH kernels on the same machine
(KERN-MEGA-PLAN 14.178, SPEC.md 53.2.3, 126.4.7, 98.3.24.2).

    make && make mega && make vbattle && python3 tests/megapar.py --prog vb|vp
        [--megs 1.375] [--kern both|big|mega]

86Box's at286_vga_sb_hdd (vm/at286-port: the owner's mr286, a VGA, a Sound
Blaster 2.0, an IDE disk) with --megs of RAM - 1.375 is 640K + 384K - booted
once on kern_big (XMEM.DRV loaded, as a user's machine loads it: 384K less the
HMA is its pool) and once on kern_mega, C: a data disk holding the program and
a file bigger than either kernel's extended memory, so each kernel's hold is
capped by memory and not by the file:

  vb  VBATTLE.O88 and a battle: $OS88_VBT_TARGET (a .VBT) or the bundle inside
      $OS88_VBT_PKG (a game .O88, re-embedded in no engine: the tree's own
      VBATTLE.O88 plays it) when set - the owner's real targets, which are
      NOT in the tree and are never checked in - else build/BATTLE.VBT, the
      demo (847,872 bytes). Opened, Enter: the play to its title, where the
      hold has filled. Read: the hold's block ([vb_hblk] 4 KBs), the pool it
      was sized from ([vb_cpool]), the two rings ([vb_rkb], [vb_rkb2]).
  vp  the Video Player (kern_mega: its arm) and apps/video/demo/vga/
      08-WILLY.V88 (1.5 MB, banked on both kernels at 1.375). Opened, Space:
      read during the play. The bank ([vp_xbn] 32 KB slots).

On kern_mega, read inside the play: the kernel's own POOL blocks (mx_tab: a
driver's image, the kernel's, a module) and the instance's. Then:

  1. kern_mega's play ran in a REAL-MODE bracket ([fsx_rmode] 1);
  2. NO POOL BLOCK OF THE INSTANCE'S BUT ITS HOLD (or bank): its region and
     its picture are not above 1MB holding KB the hold was sized without -
     the double count (a region above 1MB is copied into conventional memory
     for every real-mode bracket, its pool block idle meanwhile);
  3. the hold takes every pool KB that is not the KERNEL'S, less one granule
     (VB 8 KB: its 4 KB kept for the round-up and a 4 KB unit; VP one 32 KB
     slot);
  4. so kern_big's hold less kern_mega's is at most what kern_mega's kernel
     holds above 1MB beyond kern_big's (kern_big's XMS pool, which its HMA is
     not in, less the pool kern_mega leaves programs), plus that granule:
     EVERY KB of the difference is the kernel's own, none of it the
     program's to recover.

The table prints the conventional side as well (the rings, the free runs),
which kern_mega pays the bracket's service area and the drivers' low copies
out of (SPEC.md 53.2.3) - reported, not asserted: see KERN-MEGA-PLAN 14.178
for the accounting of every KB.

RED on mega-release (its tree, this file run there): vb - the region (48 KB)
and the title picture (12 KB) in the pool, the hold 220 of the 284 KB the
kernel leaves (2, 3, 4); vp - the arm's region (64 KB) in the pool, the bank
6 slots (192 KB) of 284 (2, 3, 4). Green with OSAPI_SEG SEG_LOW (the region
down before the hold is sized) and the picture given back before the hold
opens. The owner's targets skip, saying so, when the variables are unset.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]

fails = []


def ok(cond, what):
    print("  %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def u16(b, o=0):
    return struct.unpack_from("<H", b, o)[0]


def bundle_of(a):
    """(path, note): the battle to hold - the owner's target when named"""
    import os88build
    t = os.environ.get("OS88_VBT_TARGET")
    if t:
        if not os.path.exists(t):
            raise SystemExit("megapar: SKIP - $OS88_VBT_TARGET %s is not here" % t)
        return t, "the owner's target %s" % os.path.basename(t)
    p = os.environ.get("OS88_VBT_PKG")
    if p:
        if not os.path.exists(p):
            raise SystemExit("megapar: SKIP - $OS88_VBT_PKG %s is not here" % p)
        e = open(p, "rb").read()
        if e[:2] != b"O8" or not e[3] & 4:
            raise SystemExit("megapar: $OS88_VBT_PKG is no game package")
        off = (u16(e, 4) + 4095) // 4096 * 4096
        out = os.path.join(a.tmp, "BATTLE.VBT")
        open(out, "wb").write(e[off:])
        return out, "the bundle inside the owner's %s" % os.path.basename(p)
    return os88build.at("build/BATTLE.VBT"), "the demo bundle"


def measure(kern, a):
    """One boot of KERN: the program's hold, its pool, the kernel's pool
    blocks and the conventional heap, read in the play"""
    import functools
    for k in ("OS88_MEGA", "OS88_MEGA_EMU", "OS88_EMU", "OS88_MEGA_MEGS"):
        os.environ.pop(k, None)
    if kern == "mega":
        os.environ.update(OS88_MEGA="1", OS88_MEGA_EMU="86box",
                          OS88_MEGA_MEGS=a.megs)
    else:
        os.environ["OS88_EMU"] = "86box"
    import os88marty as om
    import os88ui
    import os88build
    import os88sym
    import heapmap
    import os88box86api
    if kern == "big" and not getattr(os88box86api, "_megapar", None):
        os88box86api._megapar = os88box86api.launch
    if kern == "big":
        os88box86api.launch = functools.partial(os88box86api._megapar,
                                                megs=a.megs)
    elif getattr(os88box86api, "_megapar", None):
        os88box86api.launch = os88box86api._megapar
    vhd = os.path.join(a.tmp, "c-%s.vhd" % kern)
    if a.prog == "vb":
        from cycweb import pkg_syms
        sym, _ = pkg_syms("apps/vbattle/vbattle.asm")
        files = ["VBATTLE.O88=" + os88build.at("build/vbattle.o88"),
                 "BATTLE.VBT=" + a.bundle]
        path = "C:/VBATTLE.O88"
        names = [("vb_hblk", 2), ("vb_cpool", 2), ("vb_rkb", 2),
                 ("vb_rkb2", 2), ("vb_tplay", 1), ("vb_err", 2),
                 ("vb_cmode", 1)]
    else:
        pkg = os88build.at("build/megak/apps/video.o88" if kern == "mega"
                           else "build/video.o88")
        sym = os88sym._parse_map(pkg[:-4] + ".map")[0]
        files = ["VIDEO.O88=" + pkg, "CLIP.V88=" + a.clip]
        path = "C:/CLIP.V88"
        names = [("vp_xbn", 2), ("vp_xbank", 1), ("vp_k", 2), ("vp_done", 2)]
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/os88hdd.py"),
                    "--noboot", "--out", vhd, "--cyls", "1024", "--heads",
                    "8", "--spt", "17", "--spc", "8"] +
                   sum([["--file", f] for f in files], []),
                   check=True, capture_output=True)
    img = os88build.at("build/mega.img" if kern == "mega"
                       else "build/os8088.img")
    got = {"kern": kern}
    with os88ui.boot(img, machine="at286_vga_sb_hdd", label="megapar" + kern,
                     extra=["--mount", "hd:0:" + vhd]) as ui:
        m = ui.m
        S = ui._S
        w = ui.path(path)
        rmode = [0]

        def st():
            r = None
            if kern == "mega" and m.read(S("fsx_rmode"), 1)[0]:
                rmode[0] = 1
                seg = u16(m.read(S("fsx_rseg"), 2))   # the bracket's copy
                r = [m.read(seg * 16 + sym[n], l) for n, l in names]
            if r is None:
                r = om.pkg_reads(m, w, [(sym[n], l) for n, l in names])
            return {n: (b[0] if l == 1 else u16(b))
                    for (n, l), b in zip(names, r)}
        if a.prog == "vb":
            om.guest_sleep(m, 2)
            m.key("Enter")
            for _ in range(120):
                om.guest_sleep(m, 1)
                s = st()
                if s["vb_tplay"] == 1 or s["vb_err"]:
                    break
            om.guest_sleep(m, 5)            # (the hold's fill, at the title)
            s = st()
            got.update(hold=s["vb_hblk"] * 4, pool=s["vb_cpool"],
                       conv=s["vb_rkb"] + s["vb_rkb2"], err=s["vb_err"],
                       title=s["vb_tplay"], cache=s["vb_cmode"])
        else:
            for _ in range(120):
                om.guest_sleep(m, 1)
                s = st()
                if s["vp_xbank"]:
                    break
            m.key("Space")
            for _ in range(10):
                om.guest_sleep(m, 1)
                s = st()
            got.update(hold=s["vp_xbn"] * 32, conv=s["vp_k"] * 32,
                       title=1 if s["vp_done"] else 0, err=0, cache=0)
        got["rmode"] = rmode[0]
        m.pause()

        class SY(dict):
            def __getitem__(self, k):
                return S(k)
        mp = heapmap.Map(m, SY())
        got["runs"] = [round(p / 64.0, 1) for _b, p in mp.runs()]
        got["xm_kb"] = u16(m.read(S("xm_kb"), 2))
        if kern == "big":
            got.setdefault("pool", got["xm_kb"])
        else:
            pool = heapmap.pool(m)
            # the instance's: owned by its region's selector (heapmap.pool
            # names the slot's region, claims and tokens by I_SPTR)
            mine = [c for c in pool if c.own < 0xFF00 and not c.purgeable]
            kernel = [c for c in pool if c.own >= 0xFF00 and not c.purgeable]
            got["mine"] = sorted(int(c.kb) for c in mine)
            got["kernel_kb"] = sum(int(c.kb) for c in kernel)
            got["free_for_programs"] = got["xm_kb"] - got["kernel_kb"]
        m.run()
    return got


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prog", choices=("vb", "vp"), default="vb")
    ap.add_argument("--megs", default="1.375")
    ap.add_argument("--kern", choices=("both", "big", "mega"), default="both")
    a = ap.parse_args()
    os.environ.setdefault("OS88_86BOX_ROMS", os.path.join(
        ROOT, "build", "86box", "roms"))
    a.tmp = tempfile.mkdtemp(prefix="megapar-", dir=os.path.join(ROOT, "build"))
    if a.prog == "vb":
        a.bundle, note = bundle_of(a)
    else:
        a.clip = os.path.join(ROOT, "apps/video/demo/vga/08-WILLY.V88")
        note = "08-WILLY.V88"
    print("megapar: %s, %s, %s MB, 86Box at286_vga_sb_hdd" % (
        {"vb": "Video Battle", "vp": "the Video Player"}[a.prog], note,
        a.megs), flush=True)
    res = {}
    for kern in (("big", "mega") if a.kern == "both" else (a.kern,)):
        res[kern] = measure(kern, a)
        print("  %s: %s" % (kern, res[kern]), flush=True)
    g = 8 if a.prog == "vb" else 32
    if "mega" in res:
        r = res["mega"]
        ok(r["rmode"] == 1 and r["title"] and not r["err"],
           "1. kern_mega's play ran in a real-mode bracket")
        ok(r["mine"] == [r["hold"]] or (r["mine"] and max(r["mine"]) ==
                                        r["hold"] and len(r["mine"]) == 1),
           "2. the instance's pool blocks are its hold alone: %s KB "
           "(hold %d)" % (r["mine"], r["hold"]))
        ok(r["hold"] >= r["free_for_programs"] - g,
           "3. the hold %d KB of the %d the kernel leaves programs "
           "(pool %d, the kernel's %d), %d KB granule" % (
               r["hold"], r["free_for_programs"], r["xm_kb"],
               r["kernel_kb"], g))
    if len(res) == 2:
        b, r = res["big"], res["mega"]
        own = b["pool"] - r["free_for_programs"]
        ok(b["hold"] - r["hold"] <= own + g,
           "4. kern_big's hold %d - kern_mega's %d = %d KB, at most the "
           "kernel's own %d (kern_big's pool %d less what kern_mega leaves "
           "programs) + %d" % (b["hold"], r["hold"], b["hold"] - r["hold"],
                               own, b["pool"], g))
        print("  conventional: kern_big %d KB held (%s free), kern_mega %d "
              "(%s free)" % (b["conv"], b["runs"], r["conv"], r["runs"]))
    if fails:
        print("megapar: FAIL (%d)" % len(fails))
        return 1
    print("megapar: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
