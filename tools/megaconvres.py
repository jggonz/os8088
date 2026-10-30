#!/usr/bin/env python3
"""tools/megaconvres.py - what kern_mega's KERNEL claims in conventional memory when the pool above
1MB has nothing left - the reserve a whole-memory claim must leave
(KERN-MEGA-MEMORY-PLAN 2, MC5). One-off measurement, not a suite row.

Boots kern_mega under Bochs (16MB, SB16, SOUND.DRV + HDD.DRV, off C: with the
system files so HIBER.DRV can write there), reads mem_tab/mx_tab, then FILLS
every free pool KB with pinned FFh blocks, as a program holding everything
would, and drives ordinary actions while sampling mem_tab for the PEAK of
every non-program conventional claim.

    make mega build/megarun.o88
    MEGS=4 python3 tools/megaconvres.py          # the owner's 4MB example
    python3 tools/megaconvres.py --nofill        # the same, pool left free

KERN-MEGA-MEMORY-PLAN 2 is what it read. Output in build/convres/.
"""
import os
import shutil
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88bochs as ob  # noqa: E402

MC_SIZE, MEM_MAX = 10, 32
MX_BLKSZ, MX_NBLK = 10, 96       # kernel/mega.inc (10 since KERN-MEGA-MEMORY-PLAN MC1)
INST_MAX = 12
NAMES = {0xFF03: "K_DRV", 0xFF04: "K_COPY", 0xFB02: "P_MSAVE",
         0xFB01: "P_ICO", 0xFF06: "K_ASC", 0xFF08: "K_MOD", 0xFF09: "K_CLONE",
         0xFF0A: "K_BAND", 0xFF0C: "K_HIB", 0xFF0D: "K_CMPR",
         0xFF10: "K_FONT", 0xFF11: "K_SC", 0xFF12: "K_IMG", 0xFF13: "K_MTBL",
         0xFF07: "K_CLIP", 0xFE02: "P_DIRW", 0xFE03: "P_RAHB",
         0xFD30: "P_FLIST"}
for k in range(8):
    NAMES[0xFD20 + k] = "P_FATW%d" % k
for k in range(16):
    NAMES[0xFB10 + k] = "P_WSAVE%d" % k


def name(o):
    if o < INST_MAX:
        return "inst%d" % o
    return NAMES.get(o, "%04X" % o)


def w16(b, i):
    return b[i] | b[i + 1] << 8


def conv(M):
    out = []
    for k in range(MEM_MAX):
        r = M[k * MC_SIZE:(k + 1) * MC_SIZE]
        if w16(r, 0):
            out.append((w16(r, 0), w16(r, 2), w16(r, 4)))
    return out


def blocks(X):
    out = []
    for k in range(MX_NBLK):
        e = X[k * MX_BLKSZ:(k + 1) * MX_BLKSZ]
        out.append((k, w16(e, 0), w16(e, 2), e[4]))
    return out


def show(tag, recs):
    tot = 0
    print("  %s:" % tag)
    for seg, para, own in sorted(recs):
        print("    %04X  %6d B  %s" % (seg, para * 16, name(own)))
        if own >= INST_MAX:
            tot += para * 16
    print("    kernel/driver total %d B (%.1fK)" % (tot, tot / 1024))
    return tot


def main():
    out = os.path.join(ROOT, "build", "convres")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    fill = "--nofill" not in sys.argv
    box = ob.Box(out, [], sb=True, cfg=3, bootc=True, noa=True, csys=True,
                 bimg=os.path.join(ROOT, "build", "megaapps.img"),
                 btype="1_44", megs=int(os.environ.get("MEGS", "16")))
    peak_all = {}

    def snap():
        M, = box.reads([("mem_tab", MC_SIZE * MEM_MAX)])
        return conv(M)

    def watch(what, secs=120):
        """sample mem_tab until the UI is idle twice 0.25 s apart; return
        the peak kernel total and the records at it"""
        best, at, first = -1, [], None
        end = time.time() + secs
        while time.time() < end:
            M, = box.reads([("mem_tab", MC_SIZE * MEM_MAX)])
            recs = conv(M)
            tot = sum(p * 16 for _, p, o in recs if o >= INST_MAX)
            for s, p, o in recs:
                if o >= INST_MAX:
                    peak_all[name(o)] = max(peak_all.get(name(o), 0), p * 16)
            if tot > best:
                best, at = tot, recs
            up, idle, tk = box._ui_state()
            if idle:
                if first is None:
                    first = (time.time(), tk)
                elif time.time() - first[0] > 0.3 and tk != first[1]:
                    break
            else:
                first = None
            f = box.fault()
            if f:
                print("FAULT", f)
                break
        print("== %s: peak kernel conventional %d B (%.1fK)" %
              (what, best, best / 1024))
        show("at the peak", at)
        return best

    def menu(dy):
        box.goto(10, 5)
        box.xd("mousedown", 1)
        left = dy * 2               # one guest pixel per two X pixels
        while left > 30:
            box.xd("mousemove_relative", "--", 0, 30)
            time.sleep(0.04)
            left -= 30
        box.xd("mousemove_relative", "--", 0, left)
        time.sleep(0.5)
        box.xd("mouseup", 1)

    try:
        box.up()
        box.capture()
        lo = box.sym("mem_tab")
        mxa = box.sym("mx_tab")
        M, X, K, B, T = box.reads([("mem_tab", MC_SIZE * MEM_MAX),
                                   ("mx_tab", MX_BLKSZ * MX_NBLK),
                                   ("xm_kb", 2), ("mem_base", 2),
                                   ("mem_top", 2)])
        xkb = w16(K, 0)
        print("pool %d KB; conventional heap %04X..%04X (%d KB)" %
              (xkb, w16(B, 0), w16(T, 0), (w16(T, 0) - w16(B, 0)) // 64))
        show("boot, conventional", conv(M))
        bl = blocks(X)
        used = sorted((o, o + kb) for _, o, kb, _ in bl if kb)
        print("  pool blocks:", ["%d+%d own %02X" % (o, kb, ow)
                                  for _, o, kb, ow in bl if kb])
        if fill:
            gaps, at = [], 0
            for a, b in used:
                if a > at:
                    gaps.append((at, a - at))
                at = max(at, b)
            if at < xkb:
                gaps.append((at, xkb - at))
            free = [k for k, o, kb, ow in bl if not kb]
            cmds = []
            for (o, kb), k in zip(gaps, free):
                e = mxa + k * MX_BLKSZ
                cmds += ["setpmem 0x%x 2 0x%x" % (e, o),
                         "setpmem 0x%x 2 0x%x" % (e + 4, 0x08FE),
                         "setpmem 0x%x 2 0" % (e + 6),
                         "setpmem 0x%x 2 0" % (e + 8),
                         "setpmem 0x%x 2 0x%x" % (e + 2, kb)]
            box.dbg(cmds)
            print("filled %d gap(s): %s KB" % (len(gaps),
                                                [g[1] for g in gaps]))
        box.click(300, 300, "a click on the desktop")
        watch("idle after fill")
        box.dbl(591, 105, "B:'s Disk window")
        watch("open B:")
        box.shot(os.path.join(out, "b.png"))
        box.dbl(150, 123, "the first row")
        watch("open B:'s first row")
        box.shot(os.path.join(out, "row.png"))
        menu(35)
        watch("Control Panel")
        box.shot(os.path.join(out, "cp.png"))
        menu(83)
        watch("Hibernate dialog")
        box.shot(os.path.join(out, "hibdlg.png"))
        box.goto(225, 225)
        time.sleep(0.5)
        box.xd("mousedown", 1)
        time.sleep(0.25)
        box.xd("mouseup", 1)
        time.sleep(2.0)
        t0 = time.time()
        hib = False
        while time.time() - t0 < 1500:
            M, = box.reads([("mem_tab", MC_SIZE * MEM_MAX)])
            for s_, p_, o_ in conv(M):
                if o_ >= INST_MAX:
                    peak_all[name(o_)] = max(peak_all.get(name(o_), 0),
                                             p_ * 16)
            t = box.read(0xB8000, 160 * 6)[0::2].decode("latin-1")
            if "hibernated" in t:
                hib = True
                break
            if time.time() - t0 > 20:
                up, idle, tk = box._ui_state()
                if up and idle:
                    break
            time.sleep(0.2)
        print("hibernate:", "DONE" if hib else "did not finish")
        box.shot(os.path.join(out, "hib.png"))
    finally:
        print("per-tag peaks:", peak_all)
        print("e9 tail:", "\n".join(box.lines()[-15:]))
        box.stop()


main()
