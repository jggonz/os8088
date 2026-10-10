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

THE HOLE UNDER THE STORE IS BUILT, NOT FOUND. Claims are first fit from the
BOTTOM, so the store has to have a hole UNDER it before compaction has anything
to do, and the row builds one out of two instances of heapfrag:

  1. heapfrag A opens on a fresh heap, and its comb's movable survivors end up
     packed at the FLOOR of the arena (its own big claim compacts them there);
  2. the driver is ticked and the volume MOUNTED, which claims the store - the
     first run big enough is the one directly above A's survivors;
  3. heapfrag B opens WHILE A IS STILL OPEN, so every claim B makes (its comb,
     its pinned block, its region) lands ABOVE the store - there is no free
     ground below it to land in;
  4. A closes, and everything under the store that was A's is free;
  5. a keystroke in B posts OSAPI_MEM_COMPACT (heapfrag's hf_key, SPEC.md
     66.4.3): a pass at MEM_LVL_TOP, run at ui_task's step 0 with nothing
     held, that CLAIMS NOTHING - so the hole A left is still a hole when the
     pass looks at it, and any cache still standing in it is cheaper than
     the poster's rank, so it is dropped rather than a wall.

It used to be heapfrag AGAIN after A closed, and that is why this row went red
on a kernel that had done nothing wrong. A second heapfrag's comb is first fit
too, so it went straight into the hole it was meant to expose - and whether
its PINNED block then sat flush against the store or a few KB short of it was
decided by L/8 of whatever the heap measured that day. 57KB blocks left a 4KB
gap and the store "moved" 4KB; 58KB blocks left none and it did not move at
all. A size pass that gave the heap 512 more bytes was enough to flip it.

The precondition is ASSERTED before the hole is opened: A must hold claims
under the store, or the run cannot prove anything and says so.

Four assertions, and the first is the one that makes the rest mean anything:
`arena moved` NO means the run measured nothing - and it must have moved
DOWN, into the hole.

The A/B is `--expect-nomove`, which builds HEAPCOMPACT=0 into a private tree
of its own (tools/os88build.py) and asserts the store stays where it was.
"""
import sys, os, re, hashlib, argparse, subprocess, tempfile
# THIS TREE'S root, DERIVED - never a hard-coded path. A literal is right in the
# checkout it was written in and wrong in a git worktree, which is how parallel
# work is done here: os88sym re-assembles ROOT/kernel/kernel.asm and compares it
# against ROOT/build/kernel.bin, so a literal ROOT answers about a DIFFERENT
# kernel from the image being booted.
_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
import os88build
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests"))
import os88marty, os88mouse, os88sym, os88geom, os88ui, dispcp

MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
PKG_HEAPFRAG = "HEAPFRAG.O88"
# how many checks heapfrag's suite records ([hf_n] once it has run them all),
# read from its source so there is no second copy of the number to go stale
with open(os.path.join(_OS88_ROOT, "tests", "heapfrag", "heapfrag.asm")) as _f:
    _src = _f.read()
HF_ROWS = int(re.search(r"^HF_ROWS\s+equ\s+(\d+)", _src, re.M).group(1))
# ...and the KEY-driven region suite's, and where its two words live in bss
HF_RROWS = int(re.search(r"^HF_RROWS\s+equ\s+(\d+)", _src, re.M).group(1))
HF_RRES = int(re.search(r"^hf_rres\s+equ\s+os88_image_end\s*\+\s*(\d+)",
                        _src, re.M).group(1))
HF_RN = int(re.search(r"^hf_rn\s+equ\s+os88_image_end\s*\+\s*(\d+)",
                      _src, re.M).group(1))


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
        kb = (os.path.getsize(os88build.at("build/rampage.bin")) + 1023) // 1024
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


def heap_map(m, S, tag):
    """The arena, bottom to top, with its holes - printed at every step that
    changes it, so a failure reads off the log rather than off a re-run."""
    base_p = u16(m.read(S("mem_base"), 2))
    top_p = u16(m.read(S("mem_top"), 2))
    print("  heap, %s:" % tag)
    fill = base_p
    for bs, pa, ow, rl in sorted(claims(m, S)):
        if bs > fill:
            print("              %5d KB HOLE" % ((bs - fill) // 64))
        print("   %04x..%04x %5d KB owner %04x%s"
              % (bs, bs + pa, pa // 64, ow, "  MOVABLE" if rl else ""))
        fill = max(fill, bs + pa)
    if top_p > fill:
        print("              %5d KB HOLE (to the top)" % ((top_p - fill) // 64))


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

    if a.expect_nomove:
        # A PRIVATE TREE, never `build/` (docs/WRITING-TESTS.md): the knob
        # kernel and the disk this row boots. apply() points the symbol
        # reader and every `build/...` path below at it.
        os88build.tree("HEAPCOMPACT=0",
                       targets=("os8088-360.img", "heapfrag360.img")).apply()

    def S(name):
        return os88sym.linear(name)

    R = drv_syms()

    with os88marty.launch("build/os8088-360.img", apps="build/heapfrag360.img",
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        def hmap(tag):
            heap_map(m, S, tag)

        def rd_seg():
            return u16(m.read(S("drv_tab") + RD_ROW * DRVR_SZ + DRVR_SEG, 2))

        def reads():
            return m.disk().get("reads")

        def floppy_done(r0):
            """A floppy load that began, or is about to, has ENDED: the read
            count moved past `r0` and then held still, in GUEST time."""
            try:
                os88marty.until(m, lambda _: reads() != r0,
                                "the floppy to be read", poll=0.1, limit=5.0)
            except os88marty.MartyError:
                pass                    # nothing to read: it was all there
            os88marty.quiesce(m, reads, guest=1.0,
                              what="the floppy to go quiet")

        def hf_bss(slot, off, n=2):
            """`n` bytes of a heapfrag instance's bss, off the window's W_SEG
            READ NOW - a posted pass may have moved the region since."""
            sg = u16(m.read(os88geom.winptr(m, slot, S) + os88geom.W_SEG, 2))
            if not sg:
                return None
            img = u16(m.read(sg * 16 + 8, 2))
            return m.read(sg * 16 + img + off, n)

        def heap_wins():
            return [w for w in os88geom.windows(m, S)
                    if (w.title or "").startswith("Heap")]

        def heapfrag_ran(slot):
            """heapfrag's suite has recorded every check: [hf_n] (bss +0)
            reaches HF_ROWS, read out of heapfrag.asm rather than restated.
            A suite that stops short is left to the reads after this."""
            def done(_):
                try:
                    b = hf_bss(slot, 0)
                    return b is not None and u16(b) >= HF_ROWS
                except Exception:
                    return False
            try:
                os88marty.until(m, done, "heapfrag's suite to finish",
                                poll=0.5, limit=40.0)
            except os88marty.MartyError:
                pass

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

        # --- heapfrag A first, so it owns the floor of the arena ------------
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, PKG_HEAPFRAG)
        hfa = heap_wins()[0]
        heapfrag_ran(hfa.i)
        os88marty.settle(m)
        hf_seg = u16(m.read(os88geom.winptr(m, hfa.i, S)
                            + os88geom.W_SEG, 2))
        print("heapfrag A at %04x" % hf_seg)

        # --- then TICK the RAM disk, which claims the store ABOVE A ---------
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
        r0 = reads()
        mo.click(x0 + CP_RX + 40,
                 y0 + CP_DBY1 + RD_ROW * CP_DROWH + CP_DROWH // 2)
        try:                                # the row's segment is written
            os88marty.until(m, lambda _: rd_seg(),  # at the claim...
                            "the RAM disk driver's claim", poll=0.1,
                            limit=20.0)
        except os88marty.MartyError:
            pass                            # ...the FAIL below says so
        floppy_done(r0)                     # ...and the load follows it
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
        r0 = reads()
        mo.click(x0 + 40, y0 + CP_I0Y + nst * CP_IROWH + 7)
        floppy_done(r0)                         # the first paint LOADS the
        os88marty.settle(m)                     # page image off the floppy
        mo.click(x0 + CP_RX + RP_MNTX + RP_MNTW // 2, y0 + RP_B0Y + RP_BH // 2)
        try:            # the mount claims the arena (rd_store_get), bounded
            os88marty.until(        # by what an idle box's pause gave it
                m, lambda _: rd_seg() and u16(m.read(
                    rd_seg() * 16 + R["rd_arena"], 2)),
                "the RAM disk's arena", poll=0.1,
                guest=6 * os88marty.GUEST_PACE)
        except os88marty.MartyError:
            pass                            # ...check 1 below says so
        os88marty.settle(m)                 # ...and the page repainting it

        # CLOSE the panel: SPEC.md 31.8 writes SYSTEM.CFG on the close, and
        # leaving it open would sit a modal-ish window over everything below.
        mo.click(cp.x + 8, cp.y + 9)
        os88marty.settle(m)

        # THE DRIVER'S SEGMENT IS RE-READ EVERY TIME, never the one banked
        # above, and that is not tidiness: since SPEC.md 66.6.3 a driver IMAGE
        # moves too, and this row's own compaction moves it. Reading
        # [rd_arena] at the old base returned 0x4712 - a plausible segment that
        # names no claim - so checks 1, 2 and 3 all failed against a kernel
        # that had done everything right (docs/WRITING-TESTS.md 13, #29's
        # shape). `drv_tab` is the kernel's own answer and the move updates it.
        def arena():
            return u16(m.read(rd_seg() * 16 + R["rd_arena"], 2))

        # --- heapfrag B, WHILE A IS STILL OPEN --------------------------------
        # Everything under the store is A's or a cache, so every claim B makes
        # - comb, pinned block, region, its own big claim - is above the
        # store. That is the whole point of opening it HERE: opened after A
        # closes, its comb is first fit into the very hole this row needs.
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=PKG_HEAPFRAG)
        hfb = [w for w in heap_wins() if w.i != hfa.i]
        if not hfb:
            print("FAIL: the second heapfrag did not open")
            return 1
        hfb = hfb[0]
        heapfrag_ran(hfb.i)                 # ...and its own posted pass has
        os88marty.settle(m)                 # run: 17 and 18 are on the wake
        hmap("A, the store and B")

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

        # THE PRECONDITION, asserted rather than hoped for: A holds ground
        # UNDER the store, so closing it is a hole there. Without this the
        # move below could not happen on a correct kernel and the row would
        # be measuring the heap's byte sizes instead of the relocation proc.
        under = sum(c[1] for c in claims(m, S)
                    if c[2] == hf_seg and c[0] < base) // 64
        print("heapfrag A holds %d KB under the store" % under)
        if not under:
            print("FAIL: nothing of A's is under the store - no hole can be "
                  "opened, so the run cannot prove anything")
            return 1

        # --- close A: the floor under the store opens up ---------------------
        ui = os88ui.UI(m, mouse=mo, verbose=False)
        ui.close(hfa.i)
        os88marty.settle(m)
        if any(c[2] == hf_seg for c in claims(m, S)):
            print("FAIL: heapfrag A closed but still holds claims - no hole")
            return 1
        hmap("A closed - the hole")

        # --- and a keystroke in B posts the pass that has to move it --------
        # heapfrag's hf_key (SPEC.md 66.4.3): R1 the what-if, R2 the post, and
        # R3/R4 on the WAKE - so [hf_rn] reaching HF_RROWS means the pass has
        # RUN. Should the post ever be refused, R2 fails and no wake comes,
        # which is the second way out of the wait. (HEAPCOMPACT=0 does NOT
        # refuse it, measured: the post is recorded and woken, and the pass
        # it runs simply moves nothing - which is the A/B's whole point.)
        ui.raise_window(hfb.i)
        m.key("KeyR")

        def rdone(_):
            try:
                rn = u16(hf_bss(hfb.i, HF_RN))
                r2 = hf_bss(hfb.i, HF_RRES + 1, 1)[0]
                return rn >= HF_RROWS or (rn >= 2 and r2)
            except Exception:
                return False
        try:                                # ...on the GUEST's clock
            os88marty.until(m, rdone, "heapfrag B's posted pass",
                            poll=0.25, limit=20.0)
        except os88marty.MartyError:
            pass                            # ...check 1 says what happened
        os88marty.settle(m)
        print("heapfrag B's region suite: %d of %d recorded"
              % (u16(hf_bss(hfb.i, HF_RN)), HF_RROWS))
        hmap("after the pass")

        bad = 0
        new = arena()
        moved = new != base
        if moved and not a.expect_nomove and new > base:
            print("      ...UP, not into the hole under it")
            bad += 1
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
        vol = m.read(rd_seg() * 16 + R["rd_vol"], 1)[0]   # ...and here, for
                                                         # arena()'s reason
        letter = chr(ord("A") + vol) if vol != 0xFF else None
        n3 = 3
        if letter is None:
            print("  %d ram volume listed   FAIL (the volume never mounted)"
                  % n3)
            bad += 1
        else:
            print("  (the RAM disk is drive %s:)" % letter)
            # CLEAR THE DESKTOP FIRST. The Disk window this row opened on B:
            # near the top sits over the RAM disk's zone, and a desktop zone
            # is BEHIND every window - so the double-click below would land on
            # that window and the drive would never open. The harness names
            # this now ("drive D:'s desktop zone at (600,113) is COVERED by
            # ['Disk']") where it used to be a wait that simply ended.
            os88ui.UI(m, mouse=mo, verbose=False).clear_desktop()
            # **THE ASSERTION IS THE MOUNT, NOT A FILE COUNT.** This volume is
            # created by the Control Panel's Mount button a few lines up and
            # nothing ever writes to it, so a freshly formatted FAT12 root is
            # EMPTY by construction - `disk_nfiles > 0` was asking for
            # something that cannot happen and reporting the right answer as a
            # failure.
            #
            # What a stale [rd_arena] does is serve GARBAGE (rd_blit_user
            # loads DS from that word), and garbage does not pass SPEC.md
            # 18.2's mount rules - so [FS_MOK], the acting window's own "that
            # listing came from a good mount", is the thing that is false when
            # the arena is wrong and true when it is right. `open_drive` waits
            # on exactly that and raises if it never comes, so reaching this
            # line is most of the assertion; reading it back makes it explicit
            # rather than implied.
            dispcp.open_drive(m, mo, S, os88marty.settle, letter)
            vp = u16(m.read(S("fm_vp"), 2))
            mok = m.read((0x60 << 4) + vp + os88geom.FS_MOK, 1)[0]
            nf = u16(m.read(S("disk_nfiles"), 2))
            okv = bool(mok)
            print("  %d ram volume listed   %s  (mounted=%d, %d files - an "
                  "empty root is correct here)"
                  % (n3, "OK" if okv else "NOT MOUNTED", mok, nf))
            bad += not okv
        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
