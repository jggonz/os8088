#!/usr/bin/env python3
"""A package's REGION moves, and the package keeps working (SPEC.md 66.6.1).

    make && make build/regmove360.img && python3 tests/regmove.py

§66.6 has said since it was written that a region can never move "because its
base IS its CS", and listed what a move would have to rewrite. This is the
door being opened: the eleven kernel words are `mem_region_reloc`'s to fix,
and whether a move is LEGAL is `mem_frameless`'s three questions - no frame
into this region on the callback stack, no worker, and not the one being
loaded.

WHY THREE PACKAGES AND WHY THIS ORDER. A region is claimed top-down, so the
first package to open takes the ceiling and there is nothing above it to pack
into - which is correct behaviour and proves nothing. PAINT opens first and
takes the top; SHEET opens under it and is the package whose region has to
move; closing PAINT leaves the hole; and heapfrag's own suite then makes the
claim that no single run can satisfy, which is what reaches the descending
pass at all.

THE ASSERTION THAT MATTERS IS THE LAST ONE. A region that moved with a stale
`W_SEG` does not fault - the next repaint far-calls a dispatcher in freed
memory - and a stale `I_SPTR` or `MC_OWN` is invisible until the package
closes and the kernel frees somebody else's claims. So the row uses SHEET
afterwards: it raises the window, which repaints THROUGH `W_SEG`, and reads
the cell store back through the fixed-up segment words.

FIVE ASSERTIONS:

  1. SHEET's region is declared movable at all - MC_RLOC out of the kernel's
     own table, because a declaration `mem_movable` refused looks identical
     from inside the package (SPEC.md 66.5.6.2).
  2. IT MOVED. Without this the rest is vacuous and the row says so.
  3. EVERY KERNEL WORD FOLLOWED: the window's `W_SEG`, the instance's
     `I_SPTR`, and the owner word of every claim SHEET holds.
  4. Its CONTENTS survived - the typed cell, hashed either side.
  5. IT STILL DRAWS: the repaint after the move is the same picture as before
     it, which is the only thing that exercises `W_SEG` and `W_DISP`.
"""
import argparse
import hashlib
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88fixture                                      # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402
import dispcp                                           # noqa: E402
import sheetmove                                        # noqa: E402

MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
DISK = "build/regmove360.img"
u16, claims, uncovered = sheetmove.u16, sheetmove.claims, sheetmove.uncovered
pkg_seg, park = sheetmove.pkg_seg, sheetmove.park


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    S = os88sym.linear
    os88fixture.need(DISK)

    with os88marty.launch("build/os8088-360.img", apps=DISK,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)
        os88marty.settle(m)
        disk = dispcp.win_rect(m, S, dslot)[:2]

        def raise_disk():
            dw = [w for w in os88geom.windows(m, S) if w.i == dslot][0]
            pt = uncovered(m, S, dw)
            if pt is None:
                raise RuntimeError("the Disk window is wholly covered")
            mo.click(*pt)
            os88marty.settle(m)

        # --- PAINT first: its region takes the ceiling ----------------------
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name="PAINT.O88")
        time.sleep(6)
        os88marty.settle(m)
        pt_seg, pt_win = pkg_seg(m, S, "Paint")
        if pt_seg is None:
            print("FAIL: Paint never opened a window")
            return 1
        mo.drag(pt_win.x + 60, pt_win.y + 9, 60, pt_win.y + 9)  # out of the way
        os88marty.settle(m)
        pt_seg, pt_win = pkg_seg(m, S, "Paint")
        print("paint region %04x at %s" % (pt_seg, (pt_win.x, pt_win.y)))

        # --- SHEET under it: the one that has to move -----------------------
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name="SHEET.O88")
        time.sleep(8)
        os88marty.settle(m)
        sh_seg, sh_win = pkg_seg(m, S, "Sheet")
        if sh_seg is None:
            print("FAIL: Sheet never opened a window")
            return 1
        mo.drag(sh_win.x + 60, sh_win.y + 9, sh_win.x + 60, sh_win.y + 9 + 25)
        os88marty.settle(m)
        sh_seg, sh_win = pkg_seg(m, S, "Sheet")
        print("sheet  region %04x at %s"
              % (sh_seg, (sh_win.x, sh_win.y, sh_win.w, sh_win.h)))
        if sh_seg > pt_seg:
            print("FAIL: Sheet's region %04x is ABOVE Paint's %04x, so "
                  "closing Paint leaves no hole above it and the descending "
                  "pass would have nothing to do" % (sh_seg, pt_seg))
            return 1

        reg = [c for c in claims(m, S) if c[0] == sh_seg]
        if not reg:
            print("FAIL: no claim in mem_tab starts at Sheet's segment %04x"
                  % sh_seg)
            return 1
        rbase, rpara, rown, rrloc = reg[0]
        bad = 0
        print("  1 region declared movable  %s"
              % ("MC_RLOC=%04x, %dKB owner %04x" % (rrloc, rpara // 64, rown)
                 if rrloc else
                 "NO  <-- OSAPI_MEM_MOVABLE was refused and the package "
                 "cannot tell"))
        bad += not rrloc

        m.type_text("12345")
        os88marty.settle(m)
        m.key("Enter")
        os88marty.settle(m)
        cx0, cy0, cx1, cy1 = sh_win.content
        park(mo, m)
        shot0 = m.vram("cga")
        # THE FIRST 2KB AND NOT THE WHOLE REGION. A region is code AND the
        # package's live bss, and SHEET is running: its counters, its paint
        # state and its formatter buffer all change between the two captures,
        # so hashing the whole of it compares a program with itself a few
        # seconds later and always differs. The header and the code at the
        # front of the image are what a move must carry unchanged.
        h0 = hashlib.md5(m.read(rbase * 16, 2048)).hexdigest()

        # --- close PAINT: the hole above Sheet's region ---------------------
        pt = uncovered(m, S, pt_win, prefer_title=True)
        if pt is None:
            print("FAIL: Paint is wholly covered; cannot raise it")
            return 1
        mo.click(*pt)
        os88marty.settle(m)
        mo.click(pt_win.x + 8, pt_win.y + 9)          # its close box
        os88marty.settle(m)
        if any(w.i == pt_win.i and w.visible
               for w in os88geom.windows(m, S)):
            print("FAIL: Paint did not close, so no hole opened above Sheet")
            return 1

        def dump(tag):
            base = u16(m.read(S("mem_base"), 2))
            top = u16(m.read(S("mem_top"), 2))
            print("   -- %s (wm_pkgd=%d)" % (tag, m.read(S("wm_pkgd"), 1)[0]))
            fill = base
            for b, p, o, r in claims(m, S):
                if b > fill:
                    print("        %5d KB HOLE" % ((b - fill) // 64))
                print("      %04x %5d KB own %04x%s"
                      % (b, p // 64, o, " MOV" if r else ""))
                fill = max(fill, b + p)
            if top > fill:
                print("        %5d KB HOLE (top)" % ((top - fill) // 64))

        dump("after Paint closed")

        # --- FILLER LAST, and that ordering is the experiment ---------------
        # Its REGION is claimed top-down, so it lands at the top of the hole
        # Paint left and the rest of that hole is then sealed ABOVE Sheet's
        # region by a pinned barrier - which is exactly the shape the
        # descending pass exists for. Its FILL is claimed bottom-up and takes
        # the low arena down to FL_LEAVE, so the largest single run left is
        # smaller than the two runs merged. Opened any earlier, its fill lands
        # low, Sheet's claims pack down past it and leave a hole bigger than
        # anything a merge could produce - and the ascending pass answers every
        # ask on its own.
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name="FILLER.O88")
        time.sleep(8)
        os88marty.settle(m)
        fl_seg, fl_win = pkg_seg(m, S, "Filler")
        if fl_seg is None:
            print("FAIL: Filler never opened a window")
            return 1
        print("filler region %04x at %s"
              % (fl_seg, (fl_win.x, fl_win.y, fl_win.w, fl_win.h)))
        dump("after the filler")
        raise_disk()
        # RAISE THE FILLER FIRST: a key goes to the front window, and by now
        # the Disk window is on top of it.
        pt = uncovered(m, S, fl_win, prefer_title=True)
        if pt is None:
            print("FAIL: the Filler's title bar is wholly covered")
            return 1
        mo.click(*pt)
        os88marty.settle(m)
        for i in range(5):                  # fill, ask, fill, ask...
            m.key("KeyA")
            time.sleep(6)
            os88marty.settle(m)
            if pkg_seg(m, S, "Sheet")[0] != sh_seg:
                break
        dump("after the forcing asks")
        sh_now, sh_win2 = pkg_seg(m, S, "Sheet")
        if sh_now is None:
            # THE SHAPE A STALE W_SEG MAKES, and it is worth naming rather
            # than crashing on: the window record still exists and its title
            # is read through W_SEG, so a segment the move did not fix reads
            # as line noise and the window is no longer findable by name.
            # Verified: with mem_region_reloc's body replaced by `ret`, this
            # is what the row sees.
            print("  2 the region moved         the Sheet window is GONE or "
                  "its title is unreadable - which is what a W_SEG the move "
                  "did not fix looks like from out here")
            print("      windows: %s"
                  % [w.title for w in os88geom.windows(m, S) if w.visible])
            print("VERDICT: 1 PROBLEM(S)")
            return 1
        moved = sh_now != sh_seg
        print("  2 the region moved         %s"
              % ("%04x -> %04x" % (sh_seg, sh_now) if moved
                 else "NO  <-- the run proves nothing"))
        bad += not moved

        # 3: every kernel word followed
        inst = m.read(S("inst_tab"), 12 * 32)
        sptrs = [u16(inst, i * 32 + 6) for i in range(12)]
        owners = sorted(set(c[2] for c in claims(m, S)))
        ok3 = (sh_now in sptrs) and (sh_seg not in sptrs) \
            and (sh_seg not in owners)
        print("  3 kernel words followed    %s"
              % ("OK" if ok3 else
                 "STALE: I_SPTR %s, claim owners %s"
                 % ([hex(s) for s in sptrs if s],
                    [hex(o) for o in owners])))
        bad += not ok3

        if moved:
            h1 = hashlib.md5(m.read(sh_now * 16, 2048)).hexdigest()
            ok4 = h1 == h0
            print("  4 contents survived        %s  (%s)"
                  % ("OK" if ok4 else "CORRUPT", h1[:12]))
            bad += not ok4

        # 5: it still DRAWS - the repaint goes through W_SEG and W_DISP
        pt = uncovered(m, S, sh_win2, prefer_title=True)
        if pt is None:
            print("FAIL: Sheet's title bar is wholly covered; cannot raise it")
            return 1
        mo.click(*pt)
        os88marty.settle(m)
        park(mo, m)
        shot1 = m.vram("cga")

        def band(v):
            _, _, rows = v
            return b"".join(bytes(rows[y][cx0:cx1])
                            for y in range(cy0, min(cy1, len(rows))))

        same = band(shot0) == band(shot1)
        print("  5 it still draws           %s"
              % ("OK" if same else "DIFFERENT"))
        bad += not same
        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
