#!/usr/bin/env python3
"""Compact the heap out from under a DONATED listing claim (SPEC.md 66.5.10.2).

    make && make build/heapfrag360.img && python3 tests/hdmove.py

**THE CLAIM WITH THREE HOLDERS**, and the only one in the tree: `HDD.DRV`
claims 6KB per mounted partition and *hands it over* with `osapi_vol_add`, so
the segment is written down in `HDV_LSEG` (the driver's), in `dsk_vtab`'s
`DV_SEG` (the kernel's) and in `[dsk_dseg]` (the kernel's live pointer).
`mem_reloc_call` dispatches to the claim's OWNER, which is the driver - so the
driver's proc can reach exactly one of the three, and SPEC.md 66.5.10.1 called
the block structurally unmovable on that basis. 66.5.10.2 is the other half:
the kernel fixes its own two for every move, before the owner's proc runs.

**A DECLARATION IS NOT A MECHANISM**, which is the whole reason this file
exists. `MC_RLOC` being non-zero says the proc was recorded; it says nothing
about whether the block can move, whether the three words follow it, or
whether the listing still reads afterwards. Check 1 is what makes the rest
mean anything: `moved NO` means the run measured nothing, exactly as
`tests/rdmove.py` says of its own.

THE SEQUENCE is rdmove's, and the ORDER is the whole of it: claims are first
fit from the BOTTOM, so the listing claim has to have a hole UNDER it before
compaction has anything to do. heapfrag goes first and combs the arena, the
hard disk is ticked after it (a driver row is not wanted by default - SPEC.md
51.3 - so the tick is both the request and the mount, SPEC.md 52.6.1), the
volume is OPENED so that `[dsk_dseg]` names the claim rather than the floor,
heapfrag dies to open the ground, and heapfrag again forces the compaction.

IT IS MARTY'S: an 8088 with a hard disk behind an option ROM is what
`os8088_xt_hdd` is, and nothing here is a time.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import dispcp                                               # noqa: E402
import os88geom                                             # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402

MACHINE = "os8088_xt_hdd"
PKG_HEAPFRAG = "HEAPFRAG.O88"
MC_SIZE, MEM_MAX = 10, 32
DRVR_SZ, DRVR_SEG = 16, 2
HDD_ROW = 1                             # drv_tab row 1 is the hard disk
CP_I0Y, CP_IROWH, CP_RX = 6, 14, 96
CP_DBY1, CP_DROWH, CP_IDRV = 20, 26, 2
DVOL_MAX, DV_SIZE, DV_SEG = 8, 16, 6    # disk.inc's dsk_vtab row
HD_MAXVOL, HDV_SIZE, HDV_LSEG = 4, 16, 10   # hddabi.inc's hd_vols row


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def drv_syms():
    """`hd_vols`' offset inside HDD.DRV, by re-assembling it.

    rdmove's trick pointed at the other driver. -DHDTOOL_KB is the MAKEFILE's
    and is not optional: hdtool.inc sizes the tool's claim from it, so without
    it this assembles to an error before the emulator is ever started.
    """
    src = os.path.join(ROOT, "drivers/hdd/hdd.asm")
    kb = (os.path.getsize(os.path.join(ROOT, "build/hddtool.bin")) + 1023) // 1024
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "h.asm"), os.path.join(d, "h.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error",
                        "-I", "drivers/hdd/", "-I", "drivers/", "-I", "apps/",
                        "-I", "build/", "-DHDTOOL_KB=%d" % kb,
                        "-o", os.path.join(d, "h.bin"), cp],
                       cwd=ROOT, check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


def claims(m, S):
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    return [tuple(u16(raw, i * MC_SIZE + k) for k in (0, 2, 4, 8))
            for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)]


def dump(m, S, label):
    print("  --- %s ---" % label)
    fill = u16(m.read(S("mem_base"), 2))
    top = u16(m.read(S("mem_top"), 2))
    for bs, pa, ow, rl in sorted(claims(m, S)):
        if bs > fill:
            print("        %5d KB HOLE" % ((bs - fill) // 64))
        print("   %04x %5d KB owner %04x%s"
              % (bs, pa // 64, ow, "  MOVABLE" if rl else ""))
        fill = max(fill, bs + pa)
    if top > fill:
        print("        %5d KB HOLE (to the top)" % ((top - fill) // 64))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    a = ap.parse_args()

    def S(name):
        return os88sym.linear(name)

    H = drv_syms()
    if "hd_vols" not in H:
        sys.exit("no hd_vols in HDD.DRV's map - the driver's half cannot be read")

    with os88marty.launch("build/os8088-360.img", apps="build/heapfrag360.img",
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        def hdseg():
            return u16(m.read(S("drv_tab") + HDD_ROW * DRVR_SZ + DRVR_SEG, 2))

        def lseg(seg):
            """HDV_LSEG of every live hd_vols row, in the driver's image."""
            raw = m.read(seg * 16 + H["hd_vols"], HD_MAXVOL * HDV_SIZE)
            return [u16(raw, i * HDV_SIZE + HDV_LSEG) for i in range(HD_MAXVOL)]

        def dvsegs():
            raw = m.read(S("dsk_vtab"), DVOL_MAX * DV_SIZE)
            return [u16(raw, i * DV_SIZE + DV_SEG) for i in range(DVOL_MAX)]

        # --- heapfrag first, so it owns the floor and combs the arena -------
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)     # browser right
        os88marty.settle(m)
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        disk = (wx, wy)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, PKG_HEAPFRAG)
        time.sleep(22)
        os88marty.settle(m)
        hf = [w for w in os88geom.windows(m, S) if w.title.startswith("Heap")]
        if not hf:
            print("FAIL: heapfrag did not open - nothing combed the arena")
            return 1
        hf = hf[0]
        hf_seg = u16(m.read(os88geom.winptr(m, hf.i, S) + os88geom.W_SEG, 2))
        print("heapfrag at %04x" % hf_seg)

        # --- then TICK the hard disk, which mounts and DONATES the claim ----
        mo.menu(8, 8, 8, 40)                    # chip menu -> Control Panel
        os88marty.settle(m)
        cp = [w for w in os88geom.windows(m, S)
              if w.visible and w.w >= 280 and w.h >= 100]
        if not cp:
            print("FAIL: no Control Panel")
            return 1
        cp = cp[-1]
        x0, y0 = cp.x + 1, cp.y + 18
        mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)
        os88marty.settle(m)
        mo.click(x0 + CP_RX + 40,
                 y0 + CP_DBY1 + HDD_ROW * CP_DROWH + CP_DROWH // 2)
        time.sleep(8)
        os88marty.settle(m)
        seg = hdseg()
        print("hard disk driver at %04x" % seg)
        if not seg:
            print("FAIL: the hard-disk driver did not load")
            return 1
        mo.click(cp.x + 8, cp.y + 9)            # close the panel
        os88marty.settle(m)

        live = [s for s in lseg(seg) if s]
        if not live:
            print("FAIL: no HDV_LSEG - the driver donated no listing claim, so "
                  "there is nothing here to move")
            return 1
        base = live[0]
        mine = [c for c in claims(m, S) if c[0] == base]
        if not mine:
            print("FAIL: HDV_LSEG %04x is not a claim on the heap" % base)
            return 1
        para, rloc = mine[0][1], mine[0][3]
        print("listing claim   %04x  %dKB  %s"
              % (base, para // 64, "MOVABLE" if rloc else "PINNED"))
        if not rloc:
            print("FAIL: the listing claim was not declared movable "
                  "(SPEC.md 66.5.10.2)")
            return 1

        # --- OPEN the volume, so [dsk_dseg] names the claim, not the floor --
        letter = None
        for cand in "CDEF":
            try:
                dispcp.open_drive(m, mo, S, os88marty.settle, cand)
                letter = cand
                break
            except Exception:
                continue
        if letter is None:
            print("FAIL: the hard disk mounted no browsable volume")
            return 1
        hdwin = dispcp.win_list(m, S)[-1]
        os88marty.settle(m)
        dseg0 = u16(m.read(S("dsk_dseg"), 2))
        print("opened %s:  [dsk_dseg] = %04x%s"
              % (letter, dseg0, "  (the claim)" if dseg0 == base else ""))
        h0 = hashlib.md5(m.read(base * 16, para * 16)).hexdigest()
        dump(m, S, "before")

        # --- close heapfrag: the ground under the claim opens up ------------
        for _ in range(5):
            here = [w for w in os88geom.windows(m, S) if w.i == hf.i]
            if here and here[0].visible:
                zn = m.read(S("wm_zn"), 1)[0]
                if list(m.read(S("wm_zord"), zn))[-1] == hf.i:
                    break
            mo.click(*os88geom.tile_xy(m, hf, S))
            os88marty.settle(m)
        mo.click(hf.x + 8, hf.y + 9)
        os88marty.settle(m)
        if any(c[2] == hf_seg for c in claims(m, S)):
            print("FAIL: heapfrag closed but still holds claims - no hole")
            return 1

        # --- and run it again, whose big claim forces the compaction --------
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, PKG_HEAPFRAG)
        time.sleep(22)
        os88marty.settle(m)
        dump(m, S, "after")

        bad = []
        new = [s for s in lseg(hdseg()) if s]
        new = new[0] if new else 0
        moved = new and new != base
        print("\n  1 claim moved         %s"
              % ("%04x -> %04x" % (base, new) if moved else "NO"))
        if not moved:
            bad.append("nothing moved, so checks 2-5 measure nothing about the "
                       "relocation. The claim is MOVABLE and the compactor ran; "
                       "what this means is that no free run needed the ground "
                       "it stands on (tests/rdmove.py's note, same cause)")

        # 2 - the DRIVER's word (its own proc's job)
        ok2 = new in lseg(hdseg())
        print("  2 HDV_LSEG followed   %s" % ("OK  %04x" % new if ok2 else "NO"))
        if not ok2:
            bad.append("HDV_LSEG does not name the block - hd_lst_reloc did not "
                       "run or did not find its row")

        # 3 - the KERNEL's table (dsk_dseg_reloc's job)
        dv = dvsegs()
        ok3 = (not moved) or (base not in dv and new in dv)
        print("  3 dsk_vtab DV_SEG     %s"
              % ("OK  %04x" % new if ok3 else "NO  (still %04x)" % base))
        if not ok3:
            bad.append("a dsk_vtab row still names the OLD base: a mount would "
                       "read the listing out of whatever claimed that memory")

        # 4 - the KERNEL's live pointer.
        #
        # NOT EXERCISED unless [dsk_dseg] happened to name the claim when the
        # compaction fired, and it is HARD to stage from the UI rather than
        # unimportant: dsk_list_pick points it at the donated claim at the
        # MOUNT (disk.inc, SPEC.md 22.6), and the only way this test has to
        # force a compaction is to launch heapfrag out of the B: window - which
        # mounts B: and puts the pointer back on the .lowbss floor on its way.
        # So it is reported as what it is. The arm itself is two instructions
        # on the same inputs as check 3, in the same routine, immediately after
        # the loop check 3 proves ran.
        dseg1 = u16(m.read(S("dsk_dseg"), 2))
        if dseg0 != base:
            print("  4 [dsk_dseg] followed NOT EXERCISED - it named %04x (the "
                  ".lowbss floor), not the claim, when the compaction fired"
                  % dseg0)
        elif dseg1 == new:
            print("  4 [dsk_dseg] followed OK  %04x" % dseg1)
        else:
            print("  4 [dsk_dseg] followed NO  (still %04x)" % dseg1)
            bad.append("[dsk_dseg] still names the old base - every listing "
                       "read goes to the wrong segment until the next mount")

        # 4b - THE ONE THAT IS NEVER VACUOUS: nothing anywhere still holds the
        # old base. This is the actual failure a missed holder produces - a
        # word pointing into memory the next claim owns - and it does not
        # depend on which volume happened to be mounted.
        stale = []
        if moved:
            if base in dv:
                stale.append("dsk_vtab DV_SEG")
            if dseg1 == base:
                stale.append("[dsk_dseg]")
            if base in lseg(hdseg()):
                stale.append("HDV_LSEG")
        print("  4b no stale holders    %s"
              % ("OK" if not stale else "NO  (%s)" % ", ".join(stale)))
        if stale:
            bad.append("%s still name%s %04x, which the heap has given to "
                       "somebody else" % (" and ".join(stale),
                                          "" if len(stale) > 1 else "s", base))

        # 5 - and the bytes actually travelled
        h1 = hashlib.md5(m.read(new * 16, para * 16)).hexdigest() if new else ""
        ok5 = (not moved) or h1 == h0
        print("  5 contents survived   %s" % ("OK  (%s)" % h1[:12] if ok5 else
                                              "NO  %s != %s" % (h1[:12], h0[:12])))
        if not ok5:
            bad.append("the listing's bytes changed across the move")

        for b in bad:
            print("  !! %s" % b)
        print("VERDICT: %s" % ("FAIL" if bad else "OK"))
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
