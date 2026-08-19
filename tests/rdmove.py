#!/usr/bin/env python3
"""Compact the heap out from under the RAM disk's store (SPEC.md 66.5.10).

    make && make build/heapfrag360.img && python3 tests/rdmove.py

The largest claim any driver in this tree makes, and live for as long as the
volume is mounted - which is the profile that strands a heap, and the reason
this one was worth doing when the HDD's two were not.

WHAT MAKES THIS DIFFERENT from every other mover here is the OWNER. A driver
has no instance record, so its worker carries T_INST = 0xFF and the kernel
cannot tell a service task that is running from one that is idle; it
therefore refuses to compact ANY driver-owned claim while ANY TF_SERVICE task
is unparked, all-or-nothing (SPEC.md 66.5.5). The RAM disk owns no worker at
all, so on a machine with no sound stream there is nothing to wait for - and
that is exactly the machine this runs on.

The sequence is paintmove.py's: claims are first fit from the BOTTOM, so the
store has to have a hole UNDER it before compaction has anything to do.
heapfrag goes first, the driver is ticked after it (a driver row is not
wanted by default - SPEC.md 51.3 - so the tick is both the request and the
moment the arena is claimed), heapfrag dies to open the floor, and heapfrag
again forces the compaction.

Four assertions, and the first is the one that makes the rest mean anything:
`arena moved` NO means the run measured nothing.
"""
import sys, os, time, hashlib, argparse, subprocess, tempfile
sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88marty, os88mouse, os88sym, os88geom, dispcp

MC_SIZE, MEM_MAX = 10, 32
PKG_HEAPFRAG = "HEAPFRAG.O88"


DRVR_SZ, DRVR_SEG = 16, 2           # driver.inc's row: 0 = not loaded
RD_ROW = 3                          # drv_tab row 3 is the RAM disk since
                                    # SPEC.md 31.1's reorder
CP_I0Y, CP_IROWH = 6, 14            # ctrl.inc: the item list
CP_RX = 96                          # ...the pane's left edge
CP_DBY1, CP_DROWH = 20, 26          # ...the Drivers page's hit bands
CP_IDRV = 2                         # SPEC.md 31.3
RP_MNTX, RP_MNTW = 2, 64            # page.inc: Mount, in the first button row
RP_B0Y, RP_BH = 52, 16


def drv_syms():
    """[rd_arena]'s offset in the driver's image, by re-assembling it.

    paintmove.py's trick, pointed at a .drv: the offset is an ordinary label
    here, but asking nasm is still the only answer that cannot go stale.
    """
    src = "drivers/ramdisk/ramdisk.asm"
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "r.asm"), os.path.join(d, "r.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        # -DRAMPAGE_KB is the MAKEFILE's, and it is not optional: rdpage.inc
        # sizes the page image's claim from it, so without it this assembles
        # to an ERROR - which is what this helper did for as long as the knob
        # has existed, and it fails before the emulator is ever started.
        kb = (os.path.getsize("build/rampage.bin") + 1023) // 1024
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "drivers/",
                        "-I", "drivers/ramdisk/", "-I", "apps/", "-I", "build/",
                        "-DRAMPAGE_KB=%d" % kb,
                        "-o", os.path.join(d, "r.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def claims(m, S):
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    return [tuple(u16(raw, i * MC_SIZE + k) for k in (0, 2, 4, 8))
            for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)]


def uncovered(m, S, win, prefer_title=False):
    zn = m.read(S("wm_zn"), 1)[0]
    zord = list(m.read(S("wm_zord"), zn))
    if win.i not in zord:
        return None
    wins = {w.i: w for w in os88geom.windows(m, S) if w.visible}
    above = [wins[i] for i in zord[zord.index(win.i) + 1:] if i in wins]
    ys = ([win.y + 1] if prefer_title
          else range(win.y + os88geom.TITLE_H, win.y + win.h - 2))
    for y in ys:
        for x in range(win.x + 4, win.x + win.w - 18):
            if not any(o.covers(x, y) for o in above):
                return x, y
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--expect-nomove", action="store_true",
                    help="the A/B: built HEAPCOMPACT=0, so nothing may move")
    a = ap.parse_args()

    def S(name):
        return os88sym.linear(name, ("NOCOMPACT",) if a.expect_nomove else ())

    R = drv_syms()

    with os88marty.launch("build/os8088-360.img", apps="build/heapfrag360.img",
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        def rd_seg():
            return u16(m.read(S("drv_tab") + RD_ROW * DRVR_SZ + DRVR_SEG, 2))

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)   # browser right
        os88marty.settle(m)
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        disk = (wx, wy)

        def raise_disk():
            dw = [w for w in os88geom.windows(m, S) if w.i == dslot][0]
            pt = uncovered(m, S, dw)
            if pt is None:
                raise RuntimeError("the Disk window is wholly covered")
            mo.click(*pt)
            os88marty.settle(m)

        # --- heapfrag first, so it owns the floor of the arena --------------
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, PKG_HEAPFRAG)
        time.sleep(22)
        os88marty.settle(m)
        hf_win = [w for w in os88geom.windows(m, S)
                  if w.title.startswith("Heap")][0]
        hf_seg = u16(m.read(os88geom.winptr(m, hf_win.i, S)
                            + os88geom.W_SEG, 2))
        print("heapfrag at %04x" % hf_seg)

        # --- then TICK the RAM disk, which claims the store ABOVE it --------
        # No driver row is wanted by default (SPEC.md 51.3), so this click is
        # both the request and the moment the arena exists.
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
                 y0 + CP_DBY1 + RD_ROW * CP_DROWH + CP_DROWH // 2)
        time.sleep(6)
        os88marty.settle(m)
        seg = rd_seg()
        print("ram disk driver at %04x" % seg)
        if not seg:
            print("FAIL: the RAM disk driver did not load")
            return 1

        # ...and MOUNT it, which is when the arena is claimed.
        #
        # TICKING THE ROW ONLY LOADS THE DRIVER (SPEC.md 51.3): rd_store_get
        # runs at the MOUNT, so without this second click [rd_arena] is 0 and
        # there is nothing on the heap for compaction to move at all. That is
        # what this script did for as long as the page has existed, and with
        # the nasm failure above it in front of it nobody had seen it.
        #
        # The page is the row past the static ones (SPEC.md 31.9), and Mount
        # is the first button of its first row.
        nst = m.read(S("cp_nst"), 1)[0]
        mo.click(x0 + 40, y0 + CP_I0Y + nst * CP_IROWH + 7)
        time.sleep(8)                           # the first paint LOADS the
        os88marty.settle(m)                     # page image off the floppy
        mo.click(x0 + CP_RX + RP_MNTX + RP_MNTW // 2, y0 + RP_B0Y + RP_BH // 2)
        time.sleep(6)
        os88marty.settle(m)

        # CLOSE the panel: SPEC.md 31.8 writes SYSTEM.CFG on the close, and
        # leaving it open would sit a modal-ish window over everything below.
        mo.click(cp.x + 8, cp.y + 9)
        os88marty.settle(m)

        def arena():
            return u16(m.read(seg * 16 + R["rd_arena"], 2))

        base = arena()
        mine = [c for c in claims(m, S) if c[0] == base]
        if not mine:
            print("FAIL: [rd_arena] %04x is not a claim on the heap" % base)
            return 1
        para, _, rl = mine[0][1], mine[0][2], mine[0][3]
        print("[rd_arena] = %04x  %dKB  %s"
              % (base, para // 64, "MOVABLE" if rl else "PINNED"))
        if not rl and not a.expect_nomove:
            print("FAIL: the store was not declared movable")
            return 1
        h0 = hashlib.md5(m.read(base * 16, para * 16)).hexdigest()

        # --- close heapfrag: the floor under the store opens up -------------
        tile = os88geom.tile_xy(m, hf_win, S)

        def hf_state():
            for w in os88geom.windows(m, S):
                if w.i == hf_win.i:
                    zn = m.read(S("wm_zn"), 1)[0]
                    z = list(m.read(S("wm_zord"), zn))
                    return w.visible, (z and z[-1] == w.i)
            return False, False

        for _ in range(5):
            vis, top = hf_state()
            if vis and top:
                break
            mo.click(*tile)
            os88marty.settle(m)
        mo.click(hf_win.x + 8, hf_win.y + 9)
        os88marty.settle(m)
        if any(c[2] == hf_seg for c in claims(m, S)):
            print("FAIL: heapfrag closed but still holds claims - no hole")
            return 1

        # --- and run it again, whose big claim forces the compaction --------
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=PKG_HEAPFRAG)
        time.sleep(22)
        os88marty.settle(m)

        base_p = u16(m.read(S("mem_base"), 2))
        top_p = u16(m.read(S("mem_top"), 2))
        fill = base_p
        for bs, pa, ow, rl2 in sorted(claims(m, S)):
            if bs > fill:
                print("        %5d KB HOLE" % ((bs - fill) // 64))
            print("   %04x %5d KB owner %04x%s"
                  % (bs, pa // 64, ow, "  MOVABLE" if rl2 else ""))
            fill = max(fill, bs + pa)
        if top_p > fill:
            print("        %5d KB HOLE (to the top)" % ((top_p - fill) // 64))

        # WHY CHECK 1 IS STILL INCONCLUSIVE, and what it is NOT.
        #
        # It is not the arena being pinned (the line above says MOVABLE) and
        # not the compactor declining to work. It is that nothing in this
        # scenario NEEDS the ground the arena is standing on, and the heap
        # dump printed above is the evidence: there is a PINNED kernel claim
        # (owner ff05, a Disk window's listing cache - SPEC.md 2.3/22.1)
        # sitting ABOVE the arena, and a pinned claim is a wall. Every free
        # run is capped by it, heapfrag asks for `largest + 1` (its own
        # hf_frag), and sliding the arena down cannot join the run below the
        # wall to the run above it - so the compactor is right to leave it
        # where it is, and a run that leaves it there measures nothing about
        # the relocation proc.
        #
        # Sizing the store was tried and does not reach it: mem_claim did not
        # put a 24KB store in any of heapfrag's 32KB comb holes either, so the
        # arena lands above the comb whatever size it is.
        #
        # What this wants is the wall on the other side - the Disk windows'
        # caches claimed BEFORE the store rather than after - and that is a
        # scenario change in a SPEC.md 66 gate rather than something to bolt
        # on here. Until then checks 2 and 3 stand on their own: the store's
        # contents are verified byte for byte and the volume is read back.
        bad = 0
        new = arena()
        moved = new != base
        print("  1 arena moved         %s"
              % ("%04x -> %04x" % (base, new) if moved
                 else "NO (expected)" if a.expect_nomove
                 else "NO  <-- the run proves nothing"))
        bad += moved if a.expect_nomove else not moved

        if new not in [c[0] for c in claims(m, S)]:
            print("      ...and it names no claim on the heap")
            bad += 1

        h1 = hashlib.md5(m.read(new * 16, para * 16)).hexdigest()
        ok2 = h1 == h0
        print("  2 contents survived   %s  (%s)"
              % ("OK" if ok2 else "CORRUPT", h1[:12]))
        bad += not ok2

        # --- and the VOLUME still works, which is what a stale [rd_arena]
        # would break: every read of a file on it goes through rd_blit_user,
        # which loads DS from that word. A wrong one serves garbage without
        # faulting, so this opens the volume and reads the listing back.
        # ASK THE DRIVER which volume it got rather than guessing a letter:
        # the kernel assigns the index (SPEC.md 26.1) and a machine whose B:
        # was retired by 18.97's probe numbers them differently.
        vol = m.read(seg * 16 + R["rd_vol"], 1)[0]
        letter = chr(ord("A") + vol) if vol != 0xFF else None
        n3 = 3
        if letter is None:
            print("  %d ram volume listed   FAIL (the volume never mounted)"
                  % n3)
            bad += 1
        else:
            print("  (the RAM disk is drive %s:)" % letter)
            dispcp.open_drive(m, mo, S, os88marty.settle, letter)
            os88marty.settle(m)
            nf = u16(m.read(S("disk_nfiles"), 2))
            okv = nf > 0
            print("  %d ram volume listed   %s  (%d files)"
                  % (n3, "OK" if okv else "EMPTY", nf))
            bad += not okv
        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
