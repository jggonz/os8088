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
  4. so kern_big's hold less kern_mega's is at most kern_mega's FIXED AREA
     above 1MB (extended memory less the pool: `.cold`, `.mega`, the park's
     scratch and the tables, read off the machine) beyond kern_big's 64 KB
     HMA, plus that granule - EVERY KB of the difference is that fixed
     area's, none of it a driver's or the program's (KERN-MEGA-PLAN 14.180:
     it was the fixed area AND the drivers' 15 KB before);
  5. no pool block of the KERNEL'S in the play: the drivers the bracket
     carries (SOUND.DRV, HDD.DRV) were brought down by FSXF_PREP (SPEC.md
     53.2.3.1), not held above 1MB beside their low copies;
  6. vb: the rings left the door its own KB and no guess - the largest free
     conventional run in the play is VB_RKEEP and at most a 4 KB unit more
     (VB_RKEEPM kept 24 for 18) - and the door's KB held where the door
     claims them while the rings are sized, not left in ring 0's run when
     they land in the card ring's page gap (KERN-MEGA-PLAN 14.187: RED at
     the bookmarks' merge, 14 KB, ring 0 316; green, 10 KB, ring 0 320);
     vp: the door answered FSXF_PREP ([vp_rbkb] is not FFFFh);
  7. out of the play and the window closed, the drivers are back UP: the
     kernel's pool blocks more than in the play (mx_relinst's mega_drvup).

  8. (vb) kern_mega's hold is the CLIP CACHE (SPEC.md 126.4.6) wherever
     kern_big's is - the idle loop in its block, a ring given a clip in
     place and records read there, the A/B gate open - so the resident
     claim has no part for the idle loop and the rings take its KB, as on
     kern_big (KERN-MEGA-PLAN 14.179: the cache planned at the door, checked
     in the bracket where OSAPI_XMEM_COPY's twin answers where the block is
     now). RED on mega-release, whose vb_ctry refused the cache under
     CPU_F_PM (the demo at 640K + 384K: kern_big's cache, kern_mega's
     copying hold, 0 records in place); green after.

The table prints the conventional side as well (the rings, the free runs),
which kern_mega pays the bracket's service area and the drivers' low copies
out of (SPEC.md 53.2.3) - reported, not asserted: see KERN-MEGA-PLAN 14.178
for the accounting of every KB.

RED on mega-release at 14.178 (its tree, this file run there): vb - the region (48 KB)
and the title picture (12 KB) in the pool, the hold 220 of the 284 KB the
kernel leaves (2, 3, 4); vp - the arm's region (64 KB) in the pool, the bank
6 slots (192 KB) of 284 (2, 3, 4). Green with OSAPI_SEG SEG_LOW (the region
down before the hold is sized) and the picture given back before the hold
opens. RED again on mega-release at 14.179 for legs 4-6 (KERN-MEGA-PLAN
14.180): vb - SOUND.DRV and HDD.DRV 15 KB in the pool, kern_big's hold less
kern_mega's 36 KB against the fixed area's 21 + 8, the largest free run
16 KB; vp - the same 15 KB, 64 against 21 + 32, [vp_rbkb] absent. Green with
FSXF_PREP. The owner's targets skip, saying so, when the variables are unset.

    --map    each kernel's conventional heap in the play, and kern_mega's
             tables (OS88_LDT_FREE0 = MEGA_LDT_FREE0 to split the LDT's
             entries: identity and allocator)
    --clip   vp: the demo clip - 05-LASTX.V88 (37 MB) is banked at 4 MB too
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
VB_RKEEP = 8            # apps/vbattle/vbattle.asm: KB its rings leave free


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
                 ("vb_cmode", 1), ("vb_iinp", 1), ("vb_irings", 2),
                 ("vb_icalls", 2), ("vb_noinp", 1)]
    else:
        pkg = os88build.at("build/megak/apps/video.o88" if kern == "mega"
                           else "build/video.o88")
        sym = os88sym._parse_map(pkg[:-4] + ".map")[0]
        files = ["VIDEO.O88=" + pkg, "CLIP.V88=" + a.clip]
        path = "C:/CLIP.V88"
        names = [("vp_xbn", 2), ("vp_xbank", 1), ("vp_k", 2), ("vp_done", 2)]
        if kern == "mega":                  # (FSXF_PREP's answer: 14.180;
            names.append(("vp_rbkb", 2))    # an arm before it has none)
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
        if kern == "mega":                  # ...read as "none" when absent
            names = [(n, l) for n, l in names if n in sym]
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
                       title=s["vb_tplay"], cache=s["vb_cmode"],
                       idle=s["vb_iinp"], irings=s["vb_irings"],
                       icalls=s["vb_icalls"], noinp=s["vb_noinp"])
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
                       title=1 if s["vp_done"] else 0, err=0, cache=0,
                       prep=s.get("vp_rbkb", 0xFFFF))
        got["rmode"] = rmode[0]
        m.pause()

        class SY(dict):
            def __getitem__(self, k):
                return S(k)
        mp = heapmap.Map(m, SY())
        if a.map:
            mp.report("%s: the conventional heap in the play" % kern)
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
            # THE FIXED AREA above 1MB (mega_layoutf: `.cold`, `.mega`, the
            # park's scratch and the tables), which no pool KB is: extended
            # memory less the pool. kern_big's HMA (64 KB) is the same kind
            # of KB on kern_big - outside its XMS pool - so its excess over
            # 64 is the part of kern_big's hold less kern_mega's that is the
            # kernel's own, by construction (KERN-MEGA-PLAN 14.180, item 4)
            got["fixed"] = u16(m.read(S("mega_extkb"), 2)) - got["xm_kb"]
            if a.map:                       # ...and what the tables hold
                tl = struct.unpack("<I", m.read(S("mega_tbllin"), 4))[0]
                n = u16(m.read(S("mega_ldtn"), 2))
                t = m.read(tl, n * 8)
                free0 = int(os.environ.get("OS88_LDT_FREE0", "0"), 0)
                used = [i for i in range(n) if t[i * 8 + 5]]
                print("  tables: %d KB at %06X, %d entries, %d present "
                      "(%d of them below the allocator's first, entry %d)"
                      % (u16(m.read(S("mega_tblkb"), 2)), tl, n, len(used),
                         len([i for i in used if i < free0]), free0))
        m.run()
        if kern == "mega":
            # ...AND THE DRIVERS GO BACK UP when the instance ends (mx_relinst's
            # mega_drvup, SPEC.md 53.2.3.1): out of the play, the window
            # closed, the kernel's pool blocks read again
            m.key("Escape")
            om.guest_sleep(m, 4)
            try:
                ui.close(w)
                om.guest_sleep(m, 2)
                m.pause()
                got["kernel_after"] = sum(
                    int(c.kb) for c in heapmap.pool(m)
                    if c.own >= 0xFF00 and not c.purgeable)
                m.run()
            except Exception as e:          # (reported by leg 7)
                print("  (the close: %s)" % e, flush=True)
                got["kernel_after"] = None
    return got


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prog", choices=("vb", "vp"), default="vb")
    ap.add_argument("--megs", default="1.375")
    ap.add_argument("--kern", choices=("both", "big", "mega"), default="both")
    ap.add_argument("--clip", default="08-WILLY.V88",
                    help="vp: the demo clip (apps/video/demo/vga/) - "
                    "05-LASTX.V88 (37 MB) is banked at 4 MB too, where "
                    "08-WILLY.V88 is held whole")
    ap.add_argument("--map", action="store_true",
                    help="print each kernel's conventional heap in the play")
    a = ap.parse_args()
    os.environ.setdefault("OS88_86BOX_ROMS", os.path.join(
        ROOT, "build", "86box", "roms"))
    a.tmp = tempfile.mkdtemp(prefix="megapar-", dir=os.path.join(ROOT, "build"))
    if a.prog == "vb":
        a.bundle, note = bundle_of(a)
    else:
        a.clip = os.path.join(ROOT, "apps/video/demo/vga", a.clip)
        note = os.path.basename(a.clip)
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
        ok(r["kernel_kb"] == 0,
           "5. no pool block of the KERNEL'S in the play: %d KB - the "
           "drivers the bracket carries (SOUND.DRV 11, HDD.DRV 4) brought "
           "down by FSXF_PREP rather than held above 1MB beside their low "
           "copies (SPEC.md 53.2.3.1)" % r["kernel_kb"])
        if a.prog == "vb":
            big = max(r["runs"]) if r["runs"] else 0
            ok(big <= VB_RKEEP + 4,
               "6. the rings leave the door its own KB and no guess: the "
               "largest free conventional run in the play %.1f KB, at most "
               "VB_RKEEP %d + a 4 KB unit (FSXF_PREP's answer, not "
               "VB_RKEEPM's 24)" % (big, VB_RKEEP))
        else:
            ok(r.get("prep", 0xFFFF) != 0xFFFF,
               "6. the door answered FSXF_PREP: [vp_rbkb] %s KB held for it "
               "(VP_RBRKB's guess, 16, when refused)" % (
                   r.get("prep") if r.get("prep", 0xFFFF) != 0xFFFF
                   else "none - FFFFh"))
        ok(r.get("kernel_after") and r["kernel_after"] > r["kernel_kb"],
           "7. the drivers went back UP when the instance ended: the "
           "kernel's pool blocks %s KB after the close (mega_drvup)" % (
               r.get("kernel_after"),))
    if len(res) == 2:
        b, r = res["big"], res["mega"]
        own = r["fixed"] - 64
        ok(b["hold"] - r["hold"] <= own + g,
           "4. kern_big's hold %d - kern_mega's %d = %d KB, at most the "
           "kernel's own %d (kern_mega's fixed area above 1MB, %d KB, less "
           "kern_big's 64 KB HMA) + %d" % (
               b["hold"], r["hold"], b["hold"] - r["hold"], own,
               r["fixed"], g))
        print("  conventional: kern_big %d KB held (%s free), kern_mega %d "
              "(%s free)" % (b["conv"], b["runs"], r["conv"], r["runs"]))
        if a.prog == "vb":
            ok(not b["cache"] or (r["cache"] and r["idle"] and
                                  r["irings"] and r["icalls"] and
                                  not r["noinp"]),
               "8. kern_mega's hold is the CLIP CACHE wherever kern_big's is "
               "(kern_big %s, kern_mega %s): the idle loop in its block (%d), "
               "%d ring(s) given a clip in place, %d records read there" % (
                   "cache" if b["cache"] else "no cache",
                   "cache" if r["cache"] else "the copying hold",
                   r["idle"], r["irings"], r["icalls"]))
    if fails:
        print("megapar: FAIL (%d)" % len(fails))
        return 1
    print("megapar: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
