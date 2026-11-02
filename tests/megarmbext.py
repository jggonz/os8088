#!/usr/bin/env python3
"""megarmbext - kern_mega's REAL-MODE BRACKET on an EXTENDED DESKTOP, a mode
set on EITHER display (SPEC.md 53.2.3's W7 paragraph,
docs/plans/MEGA-VIDEO-REALMODE-PLAN.md W7).

    make mega megarun && python3 tests/megarmbext.py [--keep]

With EXTD.DRV loaded a real-mode bracket used to fall back to protected mode
(FSXR_EXTD): the module's image is above 1MB, and OSAPI_FSX_MODE far-calls it
for the bracket display's kind and for the collapse onto it. Since W7 the
door copies the module's claim below 1MB and the drop aims every EXFP slot
at the copy. This boots build/mega.img - with a SYSTEM.CFG asking for Extend
- on 86Box's `286-mega-multimon` (vm/286-victory's AMI 286 with a Paradise
VGA and a HERCULES, a monitor each: the pairing vid_dual_ok accepts), with
tests/megarmb/megarmbd.asm's MEGARMBD and MEGARUN on B:, and lets MEGARUN
start it. Its two legs, each FSXF_REAL:

  0  CH = 1, the VGA: FSXM_VGA13, the framebuffer filled with colour 12
  1  CH = 2, the Hercules: FSXM_HERC, its page filled with FFh

Asserted (86Box has no port E9: the package's own bss and the kernel's words):

  0. the desktop is extended before anything: [vid_ndisp] 2, EXTD.DRV's row
     loaded, every EXFP slot on its image's code selector;
  1. per leg: CF 0, SI = FSXR_REAL (0) - NOT FSXR_EXTD (3) - CPU_F_PM clear
     inside, OSAPI_FSX_MODE CF 0 with FSI_SEG A000h / B000h;
  2. INSIDE each leg: [fsx_rmode] 1 and [k_dsel] KERNEL_SEG (the CPU is in
     real mode), every EXFP slot's SEGMENT the low copy's paragraph - below
     A000h and not the selector - with the offsets the image's, and one
     display ([vid_ndisp] 1: EXTD.DRV's own collapse ran, from the copy);
  3. ON THE GLASS inside each leg: the bracket's monitor shows the fill
     (most of the VGA's pixels colour 12's light red; most of the
     Hercules's lit) and the other monitor is dark (blanked);
  4. after: two displays again, EXFP's slots byte for byte as before,
     [fsx_rmode] 0, no bracket up, the pointer moving, and no exception
     charged to a loaded image.

And then THE VIDEO PLAYER itself, on a second boot of the same machine with
kern_mega's arm of VIDEO.O88 and a short PF_MONO1 clip (LAY_LIN80, a Repeat
file) on B:: opened from its Disk window, Space plays it full screen on the
display its window is on - the VGA - and then, its window DRAGGED onto the
Hercules, on that one:

  5. each play is a REAL-MODE bracket ([fsx_rmode] 1, [k_dsel] KERNEL_SEG,
     [fsx_rwhy] FSXR_REAL) with one display inside; the bracket's monitor
     shows the picture (lit pixels) and the other is dark; Esc ends it with
     two displays back and no exception charged to a loaded image.

THE FAILING CASES (docs/WRITING-TESTS.md 1) are in MEGA-VIDEO-REALMODE-PLAN's
W7 entry. Needs a built 86Box (python3 tools/os88box.py build).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests"),
                os.path.join(ROOT, "tests", "rdgcore")]
ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
ap.add_argument("--keep", action="store_true")
ap.add_argument("--vm", default="286-mega-multimon")
ap.add_argument("--player", action="store_true",
                help="the Video Player's legs alone")
ap.add_argument("--single", action="store_true",
                help="no SYSTEM.CFG: one display (a diagnostic A/B)")
A = ap.parse_args()
import os88box                                                # noqa: E402
import os88fat                                                # noqa: E402
import os88geom as G                                          # noqa: E402

B = os.path.join(ROOT, "build")
NAME = "MEGARMBD"
KSEG = 0x0060
PROMPTS = (("bypass MEMORY test", "esc"), ("to RESUME", "f1"),
           ("EXIT FOR BOOT", "return"))
DRIVE_B = (592, 105)            # the B: zone on the VGA desktop
FIRST = (160, 127)              # the first file of a fresh B: window
MD_RSZ = 12
PLAY = (152, 118)               # the Play button, from the player's frame
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def equates():
    out = {}
    with open(os.path.join(B, "megak", "kernel.map")) as f:
        for ln in f:
            m = re.match(r"\s*([0-9A-Fa-f]{8})\s+(\w+)\s*$", ln)
            if m:
                out.setdefault(m.group(2), int(m.group(1), 16))
    return out


def resoff():
    with open(os.path.join(B, "megarmbd.bin"), "rb") as f:
        return u16(f.read(12), 8)


def insts(b):
    raw = b.memr(b.sym("inst_tab"), G.INST_MAX * G.I_RECSZ)
    out = {}
    for i in range(G.INST_MAX):
        r = raw[i * G.I_RECSZ:(i + 1) * G.I_RECSZ]
        if r[G.I_STATE] != 1 or not r[G.I_KIND] & G.KIND_PKG:
            continue
        nm = r[G.I_NAME:G.I_NAME + 16].split(b"\0")[0].decode("latin-1")
        out[nm] = u16(r, G.I_SPTR)
    return out


def lines_now(b):
    return sum(1 for _ in open(os.path.join(b.run, "stderr.log"),
                               errors="replace"))


def faults(b, since=0):
    """exceptions charged to a loaded image after stderr line SINCE
    (tests/megarmb286.py's reading: the boot's own completed #GPs are the
    kernel's)"""
    out = []
    for k, line in enumerate(open(os.path.join(b.run, "stderr.log"),
                                  errors="replace")):
        if k < since:
            continue
        m = os88box.FAULT.search(line)
        if m and int(m.group(2), 16) < 0xF000 and \
                int(m.group(2), 16) not in (0x0060, 0x0028):
            out.append(line.strip())
    return out


def shares(path):
    """(fraction of pixels light red, fraction lit, fraction dark)"""
    from PIL import Image
    im = Image.open(path).convert("RGB")
    raw = im.tobytes()
    px = [tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]
    n = float(len(px)) or 1.0
    red = sum(1 for r, g, bl in px if r > 200 and g < 140 and bl < 140)
    lit = sum(1 for r, g, bl in px if r + g + bl > 300)
    dark = sum(1 for r, g, bl in px if r + g + bl < 60)
    return red / n, lit / n, dark / n


def syscfg(path, vel):
    """SYSTEM.CFG asking for Extend - tests/extdmod.py's record: 'VM', the
    adapter FFh (the probe's choice stands), [vid_dmode] 1 Extend, [vid_dlay]
    0, and on a VID_EXT kernel the packed display's class after"""
    with open(path, "wb") as f:
        f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"VM"
                + bytes([2 + vel, 3 + vel, 0xFF, 1, 0]) + bytes(vel)
                + b"\0\0")


def clips(tmp):
    """two STREAMED files whose play takes the full screen - the real-mode
    bracket is a streamed play's (MEGA-VIDEO-REALMODE-PLAN 5.1.1; a resident
    or LIVE file plays in protected mode, 98.3.10): tests/vidv2.py's VGA8
    LIN320 clip for the VGA and its one-bit LAY_HERC clip for the Hercules"""
    import os88vid as vid
    import vidv2
    out = []
    for nm, fn, lay in (("VGA.V88", vidv2.clip, vid.LAY_LIN320),
                        ("HERC.V88", vidv2.clip1, vid.LAY_HERC)):
        d = os.path.join(tmp, nm[:-4].lower())
        os.makedirs(d)
        p = os.path.join(tmp, "pstage", nm)
        shutil.move(fn(d, lay), p)
        out.append(p)
    return out


def top_win(b):
    """wm_top's answer, off wm_zord as os88geom.top takes it: (x, y, w)"""
    n = b.byte("wm_zn")
    if not n:
        return None
    zord = b.memr(b.sym("wm_zord"), n)
    raw = b.memr(b.sym("wm_wins"), G.MAX_WIN * G.WIN_SIZE)
    for k in range(n - 1, -1, -1):
        o = zord[k] * G.WIN_SIZE
        if u16(raw, o) & (G.WF_VIS | G.WF_DESK) == G.WF_VIS:
            return u16(raw, o + G.W_X), u16(raw, o + G.W_Y), u16(raw, o + G.W_W)
    return None


def player(tmp, img):
    """the Video Player on each display: [(what, look, ndisp after), ...]"""
    stage = os.path.join(tmp, "pstage")
    os.makedirs(stage)
    vga, herc = clips(tmp)
    vid = os.path.join(stage, "VIDEO.O88")
    shutil.copy(os.path.join(B, "megak", "apps", "video.o88"), vid)
    bimg = os.path.join(tmp, "megav.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", bimg, "--size", "1440", vga, herc, vid], check=True,
                   stdout=subprocess.DEVNULL)
    b = os88box.launch(A.vm, name="megarmbextv%d" % os.getpid(), hold=True,
                       fresh=True, a=img, b=bimg, temp=not A.keep, turbo=True)
    out = []
    since = 0
    try:
        b.kernel_up(prompts=PROMPTS)
        b.until(lambda: b.byte("mega_on"), "protected mode", 30000, 250)
        b.ui_done(cap_ms=10000)
        since = lines_now(b)
        b.to(*DRIVE_B)
        b.click(2)
        b.ui_done(limit_ms=20000)
        # THE PLAYER PICKS THE DISPLAY ITS FILE IS FOR (98.4.1), wherever
        # its window is: the Disk window lists HERC.V88 first, and both
        # windows open on the VGA
        for leg, where in ((0, "the Hercules"), (1, "the VGA")):
            b.to(FIRST[0], FIRST[1] + 16 * leg)     # HERC.V88, VGA.V88
            b.click(2)
            b.until(lambda: "Video Player" in insts(b),
                    "the Video Player to start", 60000, 100)
            b.ui_done(limit_ms=60000)
            look = {"where": where, "win": top_win(b)}

            def inside():
                if b.byte("fsx_task") == 0xFF:
                    return False
                look["why"] = b.byte("fsx_rwhy")
                look["rmode"] = b.byte("fsx_rmode")
                if look["why"] == 0xFF:     # (not yet at the door's answer)
                    return False
                if look["why"] == 0 and (look["rmode"] != 1
                                         or b.word("k_dsel") != KSEG):
                    return False            # (real, not yet down)
                look["ndisp"] = b.byte("vid_ndisp")
                return True
            b.tap("r", gap=300)             # Repeat on, so the play waits
            b.tap("space", gap=300)         # for Esc; Space: the full screen
            b.until(inside, "the play on %s" % where, 60000, 50)
            b.pause(1500)                   # the picture under way
            for mon, verb in ((0, "screenshot"), (1, "screenshot1")):
                p = os.path.join(tmp, "play%d-mon%d.png" % (leg, mon))
                b.cmd("%s %s" % (verb, p))
                look["mon%d" % mon] = p
            b.tap("escape", gap=300)
            b.until(lambda: b.byte("fsx_task") == 0xFF, "the play to end",
                    60000, 100)
            b.ui_done(limit_ms=60000, lock=False)
            look["after"] = b.byte("vid_ndisp")
            out.append(look)
            x, y, w = top_win(b)            # ...and the player closed, by
            b.to(x + 9, y + 8)              # its close box, so the Disk
            b.click()                       # window's next row shows
            b.until(lambda: "Video Player" not in insts(b),
                    "the player to close", 60000, 100)
            b.ui_done(limit_ms=60000)
    except SystemExit as e:
        want(False, "the player's machine stopped answering or a step "
             "missed: %s" % e)
    finally:
        f = faults(b, since)
        b.quit(remove=not A.keep)
    return out, f


def judge_plays(plays, pf):
    for leg, lk in enumerate(plays):
        print("\n   the Video Player on %s (its window %s)" % (lk["where"],
                                                         lk.get("win")))
        want(lk.get("why") == 0 and lk.get("ndisp") == 1,
             "5. a REAL-MODE play: [fsx_rwhy] %s, one display inside (%s)"
             % (lk.get("why"), lk.get("ndisp")))
        if "mon0" in lk:
            m0, m1 = shares(lk["mon0"]), shares(lk["mon1"])
            on, off = (m1, m0) if leg == 0 else (m0, m1)
            print("     its monitor: lit %.3f dark %.2f; the other: lit %.3f "
                  "dark %.2f" % (on[1], on[2], off[1], off[2]))
            want(on[1] > 0.005 and off[2] > 0.95, "5. the picture is on %s "
                 "(%.1f%% lit) and the other monitor is dark (%.0f%%)"
                 % (lk["where"], on[1] * 100, off[2] * 100))
        want(lk.get("after") == 2, "5. Esc: two displays back (%s)"
             % lk.get("after"))
    if len(plays) != 2:
        want(False, "5. the player played on both displays (%d)" % len(plays))
    want(not pf, "5. exceptions charged to a loaded image: %d%s" % (
        len(pf), "".join("\n        " + x for x in pf[:6])))


def main():
    for f in ("mega.img", "megak/kernel.map", "megarun.o88", "megarmbd.o88",
              "megarmbd.bin", "megak/apps/video.o88"):
        if not os.path.exists(os.path.join(B, f)):
            sys.exit("megarmbext: build/%s is not built - `make mega megarun`"
                     % f)
    eq = equates()
    roff = resoff()
    tmp = tempfile.mkdtemp(prefix="megarmbext")
    img = os.path.join(tmp, "mega.img")         # (named mega*: os88box then
    shutil.copy(os.path.join(B, "mega.img"), img)   # reads megak/'s map)
    cfg = os.path.join(tmp, "system.cfg")
    syscfg(cfg, eq.get("CFG_VEL", 0))
    fat = os88fat.Fat12(img)
    fat.delete("SYSTEM.CFG")
    if not A.single:
        fat.add(cfg, "SYSTEM.CFG")
    fat.save()
    if A.player:
        judge_plays(*player(tmp, img))
        print("megarmbext: %s" % ("FAIL - %d" % len(FAILS) if FAILS else "ok"))
        return 1 if FAILS else 0
    stage = os.path.join(tmp, "stage")
    os.makedirs(stage)
    files = []
    for src, dst in (("megarun.o88", "0RUN.O88"),
                     ("megarmbd.o88", "MEGARMBD.O88")):
        shutil.copy(os.path.join(B, src), os.path.join(stage, dst))
        files.append(os.path.join(stage, dst))
    bimg = os.path.join(tmp, "megab.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", bimg, "--size", "1440"] + files, check=True,
                   stdout=subprocess.DEVNULL)
    b = os88box.launch(A.vm, name="megarmbext%d" % os.getpid(), hold=True,
                       fresh=True, a=img, b=bimg, temp=not A.keep, turbo=True)
    ext = None
    before = None
    legs = {}
    after = None
    since = 0
    nslot = eq["EXT_NENT"]
    try:
        b.kernel_up(prompts=PROMPTS)
        b.until(lambda: b.byte("mega_on"), "protected mode", 30000, 250)
        b.ui_done(cap_ms=10000)
        since = lines_now(b)
        ext = b.memr(b.sym("mod_tab") + eq["MOD_EXT"] * eq["MODR_SIZE"], 2)
        before = (b.byte("vid_ndisp"), u16(ext),
                  b.memr(b.sym("EXFP"), nslot * 4))
        b.to(*DRIVE_B)
        b.click(2)
        b.ui_done(limit_ms=20000)
        b.to(*FIRST)
        b.click(2)
        b.until(lambda: NAME in insts(b), "%s to start" % NAME, 60000, 100)
        base = b.lin(insts(b)[NAME])
        res = base + roff
        for leg in (0, 1):
            rec = res + 8 + leg * MD_RSZ
            look = {}

            def inside():
                if b.memr(rec + 10, 1)[0] == 2:
                    return True              # done before a look landed
                if b.byte("fsx_rmode") != 1 or b.word("k_dsel") != KSEG:
                    return False
                # INSIDE, the package runs from its region's LOW COPY
                # ([fsx_rseg]), which the region above 1MB only gets back
                # at the return - so its phase is read there
                low = b.word("fsx_rseg") * 16 + roff + 8 + leg * MD_RSZ
                if b.memr(low + 10, 1)[0] != 1:
                    return False
                look["slots"] = b.memr(b.sym("EXFP"), nslot * 4)
                look["ndisp"] = b.byte("vid_ndisp")
                for mon, verb in ((0, "screenshot"), (1, "screenshot1")):
                    p = os.path.join(tmp, "leg%d-mon%d.png" % (leg, mon))
                    b.cmd("%s %s" % (verb, p))
                    look["mon%d" % mon] = p
                return True
            b.pause(300)
            b.until(inside, "leg %d inside its real-mode bracket" % leg,
                    90000, 50)
            if look:                         # ...a frame drawn after the fill
                b.pause(400)
                for mon, verb in ((0, "screenshot"), (1, "screenshot1")):
                    b.cmd("%s %s" % (verb, look["mon%d" % mon]))
            b.until(lambda: b.memr(rec + 10, 1)[0] == 2,
                    "leg %d to return" % leg, 120000, 100)
            legs[leg] = (b.memr(rec, MD_RSZ), look)
        b.until(lambda: b.memr(res, 1)[0] == 1, "both legs", 30000, 100)
        b.ui_done(limit_ms=20000, lock=False)
        p0 = b.pointer()
        b.to(300, 250)
        after = (b.byte("vid_ndisp"), b.memr(b.sym("EXFP"), nslot * 4),
                 b.byte("fsx_rmode"), b.byte("fsx_task"), b.pointer() != p0)
    except SystemExit as e:
        want(False, "the machine stopped answering or a step missed: %s" % e)
    finally:
        f = faults(b, since)
        b.quit(remove=not A.keep)
    if before:
        nd, row, slots = before
        segs = {u16(slots, i * 4 + 2) for i in range(nslot)}
        print("\n   before: [vid_ndisp] %d, EXTD.DRV row %04x, slot segments %s"
              % (nd, row, " ".join("%04x" % s for s in sorted(segs))))
        want(nd == 2 and row != 0 and len(segs) == 1 and min(segs) >= 8,
             "0. the desktop is EXTENDED with EXTD.DRV loaded, every EXFP slot "
             "on its image's code selector")
    for leg, (kind, seg, what) in ((0, ("VGA", 0xA000, "VGA13")),
                                   (1, ("Hercules", 0xB000, "HERC"))):
        if leg not in legs:
            want(False, "leg %d ran" % leg)
            continue
        r, lk = legs[leg]
        print("\n   leg %d: display %d, the %s, %s" % (leg, leg, kind, what))
        want(r[0] == 0 and r[1] == 0 and not r[2] & 0x10,
             "1. a REAL-MODE bracket: cf %02x, SI %02x (FSXR_EXTD was 3), "
             "CPU_INFO AH %02x" % (r[0], r[1], r[2]))
        want(r[3] == 0 and u16(r, 4) == seg and u16(r, 8) == 0xD15C,
             "1. OSAPI_FSX_MODE took %s: cf %02x, FSI_SEG %04x, the entry ran"
             % (what, r[3], u16(r, 4)))
        if not lk:
            want(False, "2. a look landed inside leg %d" % leg)
            continue
        sl = lk["slots"]
        lseg = {u16(sl, i * 4 + 2) for i in range(nslot)}
        offs_ok = before and all(sl[i * 4:i * 4 + 2] == before[2][i * 4:i * 4 + 2]
                                 for i in range(nslot))
        want(len(lseg) == 1 and 0x100 < min(lseg) < 0xA000 and offs_ok,
             "2. inside: every EXFP slot on the LOW COPY's paragraph %s, the "
             "offsets the image's" % " ".join("%04x" % s for s in sorted(lseg)))
        want(lk["ndisp"] == 1, "2. inside: one display - EXTD.DRV's collapse "
             "ran from the copy ([vid_ndisp] %d)" % lk["ndisp"])
        m0, m1 = shares(lk["mon0"]), shares(lk["mon1"])
        print("     VGA monitor: red %.2f lit %.2f dark %.2f; Hercules: red "
              "%.2f lit %.2f dark %.2f" % (m0 + m1))
        if leg == 0:
            want(m0[0] > 0.8, "3. the VGA shows the fill (%.0f%% light red)"
                 % (m0[0] * 100))
            want(m1[2] > 0.95, "3. the Hercules is dark (%.0f%%)"
                 % (m1[2] * 100))
        else:
            want(m1[1] > 0.8, "3. the Hercules shows the fill (%.0f%% lit)"
                 % (m1[1] * 100))
            want(m0[2] > 0.95, "3. the VGA is dark (%.0f%%)" % (m0[2] * 100))
    if after:
        nd, slots, rm, ft, moved = after
        print("\n   after")
        want(nd == 2, "4. two displays again ([vid_ndisp] %d)" % nd)
        want(before and slots == before[2],
             "4. EXFP's slots byte for byte as before")
        want(rm == 0 and ft == 0xFF, "4. [fsx_rmode] 0, no bracket up")
        want(moved, "4. the pointer moves on the desktop")
    want(not f, "4. exceptions charged to a loaded image: %d%s" % (
        len(f), "".join("\n        " + x for x in f[:6])))
    judge_plays(*player(tmp, img))
    if A.keep or FAILS:
        print("megarmbext: kept %s" % tmp)
    else:
        shutil.rmtree(tmp, ignore_errors=True)
    print("megarmbext: %s" % ("FAIL - %d" % len(FAILS) if FAILS else "ok"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
