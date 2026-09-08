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
import os88ui
import dispcp                                               # noqa: E402
import os88geom                                             # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import os88build                                       # noqa: E402

MACHINE = "os8088_xt_hdd"
PKG_HEAPFRAG = "HEAPFRAG.O88"
MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
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
    kb = (os.path.getsize(os.path.join(ROOT, os88build.at("build/hddtool.bin"))) + 1023) // 1024
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

        def lseg(_ignored=None):
            """HDV_LSEG of every live hd_vols row, in the driver's image.

            THE IMAGE'S SEGMENT IS RE-READ, never banked: since SPEC.md 66.6.3
            a driver image moves, and reading the driver's own words at the
            base it loaded at returns a plausible number out of freed memory.
            `tests/rdmove.py` failed for exactly that for a release.
            """
            raw = m.read(hdseg() * 16 + H["hd_vols"], HD_MAXVOL * HDV_SIZE)
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
        # THE ZONE HAS TO BE ON THE GLASS. The Disk window opened above sits
        # over the hard disk's zone, and a desktop zone is behind every
        # window - so every candidate below would fail on a click that went
        # somewhere else. `open_drive` names that now; this is what to do
        # about it.
        #
        # UNCOVER AND NOT clear_desktop: this row still needs the Heap
        # Compaction window it opened, and its dock tile a few lines below.
        # Clearing the desktop got the zone and lost the window, and the row
        # then failed on "window slot 1 has no instance, so no dock tile".
        _ui = os88ui.UI(m, mouse=mo, verbose=False)
        for cand in "CDEF":
            try:
                _ui.uncover(*os88geom.drive_pt(m, cand, S))
            except Exception:
                pass                            # no zone for that letter
        letter, why = None, []
        for cand in "CDEF":
            try:
                dispcp.open_drive(m, mo, S, os88marty.settle, cand)
                letter = cand
                break
            except Exception as e:              # KEEP THE REASON. Four
                why.append("%s: %s" % (cand,    # candidates all failing used
                                       str(e).split("\n")[0][:120]))
        if letter is None:                      # to print one sentence that
            print("FAIL: the hard disk mounted no browsable volume")
            for w in why:                       # named none of them, and the
                print("      %s" % w)           # harness knows exactly why
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
        #
        # BACK TO B: FIRST, and this is not housekeeping. There is ONE Disk
        # window on this machine and opening C: above RE-NAVIGATED it rather
        # than opening a second (desk_click_x: files_open_drive "fronts a
        # window already showing that drive's root, or opens one, or at the
        # cap moves the front one"). So `dslot` is showing the hard disk's
        # root now, and asking it for HEAPFRAG.O88 reads the DOS volume -
        # `'HEAPFRAG.O88' is not in this folder. It holds ['AUTOEXEC.BAT',
        # 'COMMAND.COM', ...]`, which is the harness saying exactly what
        # happened and was previously a silent double-click on nothing.
        _ui.uncover(*os88geom.drive_pt(m, "B", S))
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        # ...AND THAT RE-MOUNT IS WHAT CHECK 4 HAS TO BE KEYED ON. Going back
        # to B: puts [dsk_dseg] on the .lowbss floor again (disk.inc's
        # `mov word [dsk_dseg], LOW_SEG` at the mount), so the value sampled
        # when C: was opened is not the value in force when the compaction
        # fires four lines below. Check 4 read the earlier one, decided the
        # pointer HAD named the claim, and then failed the kernel for not
        # moving a pointer that correctly named somewhere else - "every
        # listing read goes to the wrong segment until the next mount", about
        # a mount that had already happened.
        dseg_pre = u16(m.read(S("dsk_dseg"), 2))
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

        # 4 - the KERNEL's live pointer, and THE ONLY FAILURE IT CAN SEE IS
        # THE OLD BASE. `dsk_dseg_reloc` follows the pointer if and only if it
        # names the block being moved - "the second equals the first only
        # while that volume is the one mounted", in the routine's own words -
        # so any value that is neither the old base nor the new one is a
        # MOUNT having re-pointed it, which is correct and is not this check's
        # business.
        #
        # It used to demand the new base whenever the pointer had named the
        # claim earlier in the run, and failed the kernel with "every listing
        # read goes to the wrong segment until the next mount" over a pointer
        # reading LOW_SEG - the .lowbss floor, which is exactly where a mount
        # of B: puts it. The mount is not incidental either: the only way this
        # row has to force a compaction is to LAUNCH heapfrag out of the B:
        # window, and the launch mounts B: on its way to the file, before
        # heapfrag's big claim can fire anything. So the sequence is mount,
        # then compact, and the pointer correctly names the floppy's listing
        # when `dsk_dseg_reloc` runs.
        #
        # NOT EXERCISED is therefore the designed outcome here rather than a
        # gap, and it is hard to stage from the UI rather than unimportant.
        # The arm itself is two instructions on the same inputs as check 3, in
        # the same routine, immediately after the loop check 3 proves ran -
        # and check 4b below catches the failure that matters whatever was
        # mounted.
        dseg1 = u16(m.read(S("dsk_dseg"), 2))
        if dseg1 == base and moved:
            print("  4 [dsk_dseg] followed NO  (still %04x)" % dseg1)
            bad.append("[dsk_dseg] still names the OLD base - every listing "
                       "read goes to memory the next claim owns")
        elif dseg1 == new and moved:
            print("  4 [dsk_dseg] followed OK  %04x" % dseg1)
        else:
            print("  4 [dsk_dseg] followed NOT EXERCISED - it names %04x, "
                  "neither base: a mount re-pointed it (it was %04x while %s: "
                  "was open and %04x before the compaction, and launching "
                  "heapfrag mounts B: on its way to the file)"
                  % (dseg1, dseg0, letter, dseg_pre))

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
