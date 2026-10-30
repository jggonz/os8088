#!/usr/bin/env python3
"""Compact the heap out from under a LIVE Sheet (SPEC.md 66.2, 24).

    make && make build/sheetmove360.img && python3 tests/sheetmove.py

SHEET WAS THE LARGEST UNDECLARED HOLDER IN THE TREE
(docs/plans/HEAP-UNPIN-PLAN.md 2.1.1 item 2): six unconditional claims taken at
its entry proc, ~99KB, every one pinned for the session. SPEC.md 66.5.10.2's
closing line - "the arena below the top now has no barrier in it at all - every
claim there is movable or purgeable" - was true of the configuration it was
measured on and false the moment a sheet opened.

All six are declared now. The sixth, `sh_stgseg`, is the ES:BX of every
OSAPI_FILE_READ/WRITE the package makes (SPEC.md 66.9 reason 4), so it is
PINNED across each of those calls and movable the rest of the time
(NO-PINNING 1, sh_stgpin) - and this leg asserts it now MOVES with the rest.
The pin itself is the `--leg rt` row's: it stops INSIDE a save and a load,
reads the staging's MC_RLOC there, and round-trips the file after the move.

paintmove.py's recipe, and it is the recipe rather than a preference: claims
are first fit from the bottom, so a Sheet launched into an empty arena sits on
the floor with no hole under it and compaction correctly leaves it exactly
where it is. heapfrag goes first, Sheet lands above it, heapfrag's death opens
the floor, and heapfrag re-run makes the claim that forces the pass.

FOUR ASSERTIONS:

  1. SHEET'S CELL CLAIM MOVED. Without it the rest is vacuous.
  2. ITS CONTENTS SURVIVED, hashed either side at the two addresses.
  3. EVERY ONE OF THE SIX WORDS FOLLOWED - read out of the package's own bss
     at offsets nasm computed, not guessed.
     This is the assertion `sh_reloc` is a table for: SPEC.md 66.1 is the
     record of a word-poke design that failed because a claim had a SECOND
     naming word, and Sheet has three of those in os88chart.inc's borrowed
     copies.
  4. SHEET REPAINTS THE SAME PICTURE. The grid is drawn out of the cell store
     through `sh_cellseg`, so a stale word here draws a plausible wrong sheet
     rather than crashing - which is the whole reason assertion 2 is not
     enough.

**AND THE ROW'S OWN TRAP, WHICH COST IT ITS POINT: SHEET'S REGION MOVES TOO.**
Every read of the package's words is `sh_seg * 16 + offset`, and Sheet declares
its region movable (SPEC.md 66.6.1), so the pass this row FORCES can relocate
the region as well as the claims under it.  Reading through the base captured
before the compaction neither crashes nor answers empty - a move COPIES and
does not erase, so the old base still holds the PRE-MOVE bytes - and the row
then reports "a declared claim moved NO <-- the run proves nothing" about a
pass that moved three of them.  Measured on the kernel that made the compaction
reach further (SPEC.md 66.10.4): region 7c60 -> 92c0, with cellseg 5060 ->
4b20, txtseg 5860 -> 5320 and bordseg 4da0 -> 4a20, while this row printed all
six words unchanged and `Sheet now holds []`.  So Sheet is re-found after the
launch and every read below uses the base it is at NOW.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88fixture                                      # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402
import dispcp                                           # noqa: E402

MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
PKG_HEAPFRAG, PKG_SHEET = "HEAPFRAG.O88", "SHEET.O88"
DISK = "build/sheetmove360.img"

# the six that are declared - Undo's joined them in SPEC.md 81.57, the
# border and note tables became ONE carved claim in 81.94, and the STAGING
# joined them in NO-PINNING 1 (pinned only across each file call, sh_stgpin).
# sh_noteseg is not a claim of its own any more: it is the carved claim's
# second part, and is checked below to follow it
MOVABLE = ["sh_cellseg", "sh_txtseg", "sh_bordseg",
           "sh_chartseg", "sh_undoseg", "sh_stgseg"]
STAGING = "sh_stgseg"


def pkg_syms(src, incs=("apps/",)):
    """A package's symbols, by re-assembling it - paintmove.py's trick.

    Sheet's bss offsets are an `equ` chain off `os88_image_end`, so they are
    neither greppable nor stable against an edit. A wrong offset here reads a
    plausible word out of the middle of another variable and the row then
    reports the kernel as broken.
    """
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + sum([["-I", i] for i in incs], [])
                       + ["-o", os.path.join(d, "p.bin"), cp], check=True)
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


def seg_key(m, v):
    """A segment WORD as mem_tab keys its claim - the identity on a real-mode
    kernel. On kern_mega (KERN-MEGA-MEMORY-PLAN MC7.4) a region's W_SEG and
    I_SPTR, a claim's value and the owner word of every claim a package holds
    are an LDT SELECTOR that a move REBASES and never changes, so "it moved"
    and "nothing still holds the old one" are asked of the PARAGRAPH under it
    (tests/emumove.py's para()). An instance slot, a kernel tag, a real
    segment and 0 are themselves"""
    if v and getattr(m, "bochs", False) and 0x100 <= v < 0xA000 and v & 4:
        b = m.selector(v)
        return b[0] >> 4 if b else v
    return v


def mine(cl, seg):
    return sorted([c for c in cl if c[2] == seg], key=lambda c: -c[1])


def uncovered(m, S, win, prefer_title=False):
    """A pixel of `win` that no window ABOVE it covers, or None."""
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


def park(mo, m):
    """Put the POINTER somewhere neither capture is looking.

    The arrow is drawn into the framebuffer, so it is part of any picture
    compared - and the two captures below are taken after different gestures,
    so it is somewhere different in each. This row failed on exactly that and
    the failure was six pixels wide: `differs at screen x 115..120, y 63..64`,
    which is the tail of the arrow the Sheet drag left behind, one row inside
    the content rectangle. Bottom-left of the desktop is clear of every window
    this row opens.
    """
    mo.to(5, 195)
    os88marty.settle(m)


def pkg_seg(m, S, title):
    for w in os88geom.windows(m, S):
        if w.title.startswith(title):
            raw = m.read(os88geom.winptr(m, w.i, S) + os88geom.W_SEG, 2)
            return u16(raw), w
    return None, None


def heapfrag_ran(m, S):
    """HEAPFRAG's suite has RETURNED, and the compaction it posts has landed.

    The suite runs inside its first W_PAINT with the gfx lock held, so the
    screen is stillest exactly while it works and a settle alone returns
    mid-run. [hf_done] is set as the run starts, the lock comes free when the
    paint that ran it returns, and [hf_woke] is the posted pass arriving.
    """
    seg, _ = pkg_seg(m, S, "Heap")
    if not seg:
        return False
    img = u16(m.read(seg * 16 + 8, 2))
    b = m.read(seg * 16 + img, 48)
    done, posted, woke = b[28], b[46], b[47]      # heapfrag.asm's bss table
    locked = m.read(S("gfx_lock_flag"), 1)[0]
    return bool(done and not locked and (woke or not posted))


def opened(m, S, title):
    """Wait for a package's window, then for the claims it makes to land."""
    try:
        os88marty.until(m, lambda _: pkg_seg(m, S, title)[0],
                        "%s's window" % title, poll=0.3, limit=60)
    except os88marty.MartyError as e:
        print("  (%s)" % e)             # the row's own check says the rest
        return
    os88marty.quiesce(m, lambda: claims(m, S), guest=1.0,
                      what="%s's claims" % title)


DISK_RT = "build/sheetovl360.img"
RT_NAME = "SHEET1.SLK"                  # sh_defname: Save As keeps it
RT_VALUE = 12345


def rt_poison(m, S, old, new, para):
    """0xFF over every paragraph of the old staging no live claim holds now.

    ovlmove.py's lesson, for data: a move COPIES and does not erase, so a
    transfer handed the OLD address still finds the right bytes there and a
    missing pin "works". Poisoned, a stale save writes 0xFF and a stale load
    fills a block nobody reads, leaving the parser on these bytes.
    """
    held = [(new, new + para)]
    held += [(b, b + p) for b, p, _, _ in claims(m, S) if b != old]
    n, q = 0, old
    while q < old + para:
        if any(lo <= q < hi for lo, hi in held):
            q += 1
            continue
        e = q
        while e < old + para and not any(lo <= e < hi for lo, hi in held):
            e += 1
        m.write(q * 16, b"\xFF" * ((e - q) * 16))
        n += (e - q) * 16
        q = e
    return n


def rt_free_runs(m, S):
    lo = u16(m.read(S("mem_base"), 2))
    hi = u16(m.read(S("mem_top"), 2))
    runs, at = [], lo
    for base, para, _, _ in sorted(claims(m, S)):
        if base > at:
            runs.append(base - at)
        at = max(at, base + para)
    if hi > at:
        runs.append(hi - at)
    return runs


def rt_watch(m, S, SH, seg, stub, trigger):
    """Run `trigger` and stop INSIDE `stub`'s file call (NO-PINNING 1).

    `<stub>.go` is the instruction that makes the OSAPI_FILE_READ/WRITE: the
    staging's MC_RLOC is read there and must be 0 - sh_stgpin has run - and
    the FIRST claim the kernel makes before `sh_stgunpin` is reached is
    INFLATED to one KB past the largest free run, so it can be granted only
    by a compaction, inside the transfer the staging is the ES:BX of. A
    staging that then moves has its old home poisoned (rt_poison), so a
    kernel walking the stale address writes or reads junk rather than the
    copy a move leaves behind.
    """
    go = seg * 16 + SH[stub + ".go"]
    out = seg * 16 + SH["sh_stgunpin"]
    # mem_claim_x.go and not mem_claim: every door (plain, hi, DMA) passes
    # through it, where `mem_claim` is only .text's trampoline - the disk
    # layer is in .cold and calls mem_claim_x direct, so a breakpoint on the
    # trampoline saw none of the caches a file call claims back
    # ...through m.sym() and NOT S(): both are `.cold`, and on this row's
    # CGA kmain_o slid `.cold` down by VGA_PARA at boot, so os88sym.linear's
    # addresses are 0x7F0 bytes above the live code and neither stop ever
    # matched (MOVABLE-COLD-PLAN 2; tests/fddpark.py found the class)
    claim, reloc = m.sym("mem_claim_x.go"), m.sym("mem_reloc_call")
    stg = lambda: u16(m.read(seg * 16 + SH["sh_stgseg"], 2))
    res = {"in": False, "rloc": None, "stg": None, "claims": 0, "forced": 0,
           "moved": 0, "out": False}
    m.bp_exec(go)
    trigger()
    for _ in range(400):
        if m.wait_stop(60) is None:
            break
        r = m.regs()
        at = r["cs"] * 16 + r["ip"]
        if at == go and not res["in"]:
            res["in"] = True
            s0 = stg()
            rec = [c for c in claims(m, S) if c[0] == s0]
            res["stg"], res["rloc"] = s0, (rec[0][3] if rec else None)
            if os.environ.get("SHM_DEBUG"):
                print("     at %s.go: claims %s" % (stub, ["%04x/%d/%04x/%04x"
                      % c for c in sorted(claims(m, S))]))
            m.bp_exec(out, claim, reloc)
        elif at == claim and res["in"]:
            res["claims"] += 1
            if res["forced"]:           # .go is the retry's target too: one
                m.go()                  # inflated claim is the experiment
                continue
            runs = rt_free_runs(m, S)
            kb = max(runs) // 64 + 1
            if sum(runs) >= kb * 64:
                m.setreg("ax", kb)
                res["forced"] += 1
        elif at == reloc and res["in"] and r["bx"] == res["stg"]:
            res["moved"] += 1
            para = [p for b, p, _, _ in claims(m, S) if b == r["dx"]]
            rt_poison(m, S, r["bx"], r["dx"], para[0] if para else 0x800)
        elif at == out:
            res["out"] = True
            break
        m.go()
    m.bp_exec()
    m.go()
    return res


def roundtrip(a):
    """--leg rt: the staging's PIN, seen inside a save and a load, and the
    save + reload after the staging has moved (NO-PINNING 1).

    sheetovl360's disk and sheetovlmove's arrangement: Note Pad owns the
    floor, SHEET lands above it with both modules (the file formats are
    CHART.OVL's, 82.16.9), Note Pad closes, and a compaction posted from
    outside at the trivial rank (pkgmove's door, `[mem_cpq]`) slides SHEET's
    data down - the staging with it.

    SIX ASSERTIONS:
      1. the staging is declared movable (MC_RLOC != 0, read from outside);
      2. the posted pass MOVED it, and [sh_stgseg] names the new claim;
      3. INSIDE the save's OSAPI_FILE_WRITE its MC_RLOC is 0 - pinned;
      4. the file the save wrote holds the cell, read by a host parser;
      5. INSIDE the load's OSAPI_FILE_READ its MC_RLOC is 0 - pinned;
      6. the load put the cell back into an EMPTIED store, and afterwards
         the staging is movable again.

    The first claim the kernel makes inside either call is inflated so that
    only a compaction can grant it (rt_watch), and the row prints how many
    there were. ON THIS DISK THERE ARE NONE, and that is measured rather than
    assumed: the read-ahead window is claimed at the MOUNT (dsk_rah), a 360KB
    FAT12 volume takes no FAT window, and even with every cache shed by a
    0xFF-rank pass just before the keystroke the chooser's own listing has
    claimed them back before SHEET's call is reached - so no compaction can
    land inside the transfer here, and 3 and 5 are what carry the pin. The
    machinery stays because a volume that does claim inside the call (a
    FAT16 disk's window) gets the stronger experiment for free.
    """
    import os88flush
    import os88sheetfmt
    import os88ui
    import sheetfmt as SF
    S = os88sym.linear
    os88fixture.need(DISK_RT)
    SH = pkg_syms("apps/sheet/sheet.asm")
    bad = 0

    def check(n, ok, what, why=""):
        print("  %s %-32s %s" % (n, what, "OK" if ok else "FAIL %s" % why))
        return 0 if ok else 1

    with os88marty.launch("build/os8088-360.img", apps=DISK_RT,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.kernel_up(m)
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)
        os88marty.settle(m)
        disk = dispcp.win_rect(m, S, dslot)[:2]

        def win(slot):
            return [x for x in os88geom.windows(m, S) if x.i == slot][0]

        def raise_win(slot, title=False):
            pt = uncovered(m, S, win(slot), prefer_title=title)
            if pt is None:
                raise RuntimeError("window %d is wholly covered" % slot)
            mo.click(*pt)
            os88marty.settle(m)

        dispcp.open_named(m, mo, S, os88marty.settle, *disk,
                          name="NOTEPAD.O88")
        np_slot = dispcp.win_list(m, S)[-1]
        os88marty.quiesce(m, lambda: claims(m, S), guest=1.0,
                          what="Note Pad's claims")
        npw = win(np_slot)
        mo.drag(npw.x + 60, npw.y + 9, 60, npw.y + 9)
        os88marty.settle(m)

        raise_win(dslot)
        dispcp.open_named(m, mo, S, os88marty.settle, *disk,
                          name="SHEET.O88")
        opened(m, S, "Sheet")
        seg, sh_win = pkg_seg(m, S, "Sheet")
        if seg is None:
            print("FAIL: SHEET never opened a window")
            return 1

        def word(n):
            return u16(m.read(seg * 16 + SH[n], 2))

        col, row = word("sh_selcol"), word("sh_selrow")
        m.type_text(str(RT_VALUE))
        os88marty.settle(m)
        m.key("Enter")
        os88marty.settle(m)

        raise_win(np_slot, title=True)
        npw = win(np_slot)
        mo.click(npw.x + 8, npw.y + 9)
        os88marty.settle(m)
        if any(w.i == np_slot and w.visible for w in os88geom.windows(m, S)):
            print("FAIL: Note Pad did not close, so no hole opened")
            return 1
        raise_win(sh_win.i, title=True)

        s0 = word("sh_stgseg")
        rec = [c for c in claims(m, S) if c[0] == s0]
        bad += check(1, bool(rec) and rec[0][3] != 0,
                     "the staging is declared", "(%r)" % (rec,))
        wptr = os88geom.winptr(m, sh_win.i, S)
        m.write(S("mem_cpq_lvl"), bytes([0xFC]))
        m.write(S("mem_cpq"), bytes([wptr & 0xFF, wptr >> 8]))
        os88marty.until(m, lambda _: u16(m.read(S("mem_cpq"), 2)) == 0,
                        "the posted compaction", poll=0.2, limit=30)
        os88marty.ui_done(m)
        seg, sh_win = pkg_seg(m, S, "Sheet")
        s1 = word("sh_stgseg")
        live = set(c[0] for c in claims(m, S))
        bad += check(2, s1 != s0 and s1 in live, "the staging moved, followed",
                     "(%04x -> %04x, live %s)" % (s0, s1, s1 in live))
        print("     staging %04x -> %04x" % (s0, s1))

        # --- Save As SLK, stopped inside the write --------------------------
        ui = os88ui.UI(m, mouse=mo)
        SF.menu_pick(m, mo, seg, SH, "sh_i_file", "saveas")
        os88marty.settle(m, limit=180)
        mo.click(*SF.fdlg_xy(m, SF.FMT_ROW['slk']))
        os88marty.settle(m)
        mo.click(*SF.fdlg_okxy(m))
        os88marty.settle(m, limit=180)
        ui.chooser()
        for _ in range(13):
            m.key("Backspace")
        m.type_text(RT_NAME)
        w = rt_watch(m, S, SH, seg, "sh_k_file_write",
                     lambda: m.key("Enter"))
        print("     the save: %d claim(s) inside the write, %d forced, the "
              "staging moved %d time(s) in it" % (w["claims"], w["forced"],
                                                    w["moved"]))
        bad += check(3, w["in"] and w["rloc"] == 0 and w["out"],
                     "pinned INSIDE the write",
                     "(reached %s, MC_RLOC %r, returned %s)"
                     % (w["in"], w["rloc"], w["out"]))
        ui.chooser_gone(limit=120)
        os88marty.settle(m, limit=120)
        data = None
        for _ in range(30):
            v = os88flush.Flush(marty=m).volume(1)
            if RT_NAME in v.names():
                data = v.read(RT_NAME)
                if data:
                    break
            os88marty.settle(m, quiet=2.0, stable=2, limit=60)
        cells = os88sheetfmt.read_sylk(data) if data else {}
        got = cells.get((row, col))
        bad += check(4, got == RT_VALUE, "the saved file holds the cell",
                     "(%r at %r, %d bytes)" % (got, (row, col),
                                               len(data or b"")))

        # --- ...and Open it into an EMPTIED store, stopped inside the read --
        m.write(seg * 16 + SH["sh_ncells"], b"\0\0")
        SF.menu_pick(m, mo, seg, SH, "sh_i_file", "open")
        os88marty.settle(m, limit=180)
        ui.chooser()
        ui.chooser_select(RT_NAME)
        r = rt_watch(m, S, SH, seg, "sh_k_file_read",
                     lambda: m.key("Enter"))
        print("     the load: %d claim(s) inside the read, %d forced, the "
              "staging moved %d time(s) in it" % (r["claims"], r["forced"],
                                                    r["moved"]))
        bad += check(5, r["in"] and r["rloc"] == 0 and r["out"],
                     "pinned INSIDE the read",
                     "(reached %s, MC_RLOC %r, returned %s)"
                     % (r["in"], r["rloc"], r["out"]))
        ui.chooser_gone(limit=120)
        os88marty.settle(m, limit=180)
        seg, _ = pkg_seg(m, S, "Sheet")
        back = SF.cell_val(m, seg, SH, col, row)
        s2 = word("sh_stgseg")
        rec = [c for c in claims(m, S) if c[0] == s2]
        bad += check(6, back == RT_VALUE and bool(rec) and rec[0][3] != 0,
                     "reloaded, and movable again",
                     "(cell %r, staging %r)" % (back, rec))
    print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--leg", choices=("move", "rt"), default="move",
                    help="move: the six claims under heapfrag's recipe; rt: "
                    "the staging's pin inside a save and a load, and the "
                    "round trip after it moved (roundtrip's docstring)")
    a = ap.parse_args()
    if a.leg == "rt":
        return roundtrip(a)
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
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)   # out of the way
        os88marty.settle(m)
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        disk = (wx, wy)
        print("disk window moved to (%d,%d)" % disk)

        def raise_disk():
            dw = [w for w in os88geom.windows(m, S) if w.i == dslot][0]
            pt = uncovered(m, S, dw)
            if pt is None:
                raise RuntimeError("the Disk window is wholly covered")
            mo.click(*pt)
            os88marty.settle(m)

        # --- heapfrag first, so it owns the floor of the arena --------------
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=PKG_HEAPFRAG)
        os88marty.until(m, lambda _: heapfrag_ran(m, S),
                        "heapfrag's suite and its posted pass", poll=0.5,
                        limit=120)
        os88marty.settle(m)
        hf_seg, hf_win = pkg_seg(m, S, "Heap")
        # SHOVE IT TO THE LEFT EDGE, before Sheet opens. Sheet's window is
        # 562 wide at x=55 on a 640x200 screen and heapfrag's is 250 at 135:
        # it is covered ENTIRELY, close box and all, and every later attempt
        # to raise it is a click on Sheet's grid instead - which edits a cell,
        # changes nothing about the heap, and leaves the rest of the run
        # measuring an arena that never opened a hole. At x=0 its left 55
        # columns stay clear of Sheet whatever Sheet does.
        mo.drag(hf_win.x + 60, hf_win.y + 9, 60, hf_win.y + 9)
        os88marty.settle(m)
        hf_seg, hf_win = pkg_seg(m, S, "Heap")
        print("heapfrag at %04x rect %s"
              % (hf_seg or 0, (hf_win.x, hf_win.y, hf_win.w, hf_win.h)))

        # --- then Sheet, which lands ABOVE it -------------------------------
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=PKG_SHEET)
        opened(m, S, "Sheet")
        os88marty.settle(m)
        sh_seg, sh_win = pkg_seg(m, S, "Sheet")
        if sh_seg is None:
            print("FAIL: Sheet never opened a window")
            return 1
        # AND SHOVE SHEET DOWN. Its window is 562x155 on a 640x200 screen, so
        # in x it can cover everything except a strip narrower than either
        # other window - there is no arrangement in x alone where BOTH the
        # Disk window and heapfrag keep a pixel of their own. In y there is
        # room for exactly one step, and one is enough: Sheet at y=45 leaves
        # rows 20..44 to the two windows behind it, one at each side.
        mo.drag(sh_win.x + 60, sh_win.y + 9, sh_win.x + 60, sh_win.y + 9 + 25)
        os88marty.settle(m)
        sh_seg, sh_win = pkg_seg(m, S, "Sheet")
        print("sheet rect %s" % ((sh_win.x, sh_win.y, sh_win.w, sh_win.h),))
        before = mine(claims(m, S), sh_seg)
        print("Sheet at %04x holds %s"
              % (sh_seg, ["%04x/%dKB%s" % (b, p // 64, "*" if r else "")
                          for b, p, _, r in before]))
        # SHEET'S TWO CODE MODULES ARE PARTS OF SHEET.O88 since SPEC.md
        # 82.16.12, so this disk can no longer leave them off: whichever the
        # heap had room for is loaded with the package. They are
        # sheetovlmove's subject and not this row's - its six are the DATA
        # claims - so a module's claim is set aside here, by the words that
        # name it ([ch_ovseg], [sh_m2far+2])
        SH = pkg_syms("apps/sheet/sheet.asm")
        mods = set(u16(m.read(sh_seg * 16 + SH[n] + o, 2))
                   for n, o in (("ch_ovseg", 0), ("sh_m2far", 2))) - {0}
        if mods:
            print("Sheet's modules (parts, 82.16.12) at %s - not counted"
                  % ["%04x" % b for b in sorted(mods)])
        before = [c for c in before if c[0] not in mods]
        ndecl = sum(1 for _, _, _, r in before if r)
        if ndecl != len(MOVABLE):
            print("FAIL: Sheet declared %d claims movable and this row expects "
                  "%d - a declaration mem_movable REFUSED writes no MC_RLOC "
                  "and looks identical from inside the package (SPEC.md "
                  "66.5.6.2)" % (ndecl, len(MOVABLE)))
            return 1

        # --- put something in the store a stale segment would lose ----------
        cx0, cy0, cx1, cy1 = sh_win.content
        m.type_text("12345")
        os88marty.settle(m)
        m.key("Enter")
        os88marty.settle(m)

        def sword(name):
            return u16(m.read(sh_seg * 16 + SH[name], 2))

        w0 = dict((n, sword(n)) for n in MOVABLE)
        sizes = dict((c[0], c[1]) for c in before)
        for n in MOVABLE:
            if w0[n] not in sizes:
                print("FAIL: [%s] = %04x is not a claim Sheet holds"
                      % (n, w0[n]))
                return 1
        h0 = dict((n, hashlib.md5(m.read(w0[n] * 16, sizes[w0[n]] * 16))
                   .hexdigest()) for n in MOVABLE)
        print("  " + "  ".join("%s=%04x" % (n[3:], w0[n])
                               for n in MOVABLE))
        park(mo, m)
        shot0 = m.vram("cga")

        # --- close heapfrag: the floor under Sheet opens up -----------------
        pt = uncovered(m, S, hf_win, prefer_title=True)
        if pt is None:
            print("FAIL: heapfrag is wholly covered; cannot raise it")
            return 1
        mo.click(*pt)
        os88marty.settle(m)
        mo.click(hf_win.x + 8, hf_win.y + 9)          # its close box
        os88marty.settle(m)
        if any(w.i == hf_win.i and w.visible
               for w in os88geom.windows(m, S)):
            print("FAIL: heapfrag did not close, so no hole opened under Sheet")
            return 1

        # --- and run it again, whose big claim forces the compaction --------
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=PKG_HEAPFRAG)
        os88marty.until(m, lambda _: heapfrag_ran(m, S),
                        "heapfrag's suite and its posted pass", poll=0.5,
                        limit=120)
        os88marty.settle(m)

        # **RE-FIND SHEET FIRST: THE REGION MOVES TOO** (SPEC.md 66.6.1).
        # Sheet declares its region movable, so the pass this row forces can
        # relocate the region as well as the claims under it - and every read
        # below is `sh_seg * 16 + offset`.  Reading through the base captured
        # before the compaction is not a crash and not an empty answer: a move
        # COPIES and does not erase, so the old base still holds the pre-move
        # bytes and the row reports "nothing moved" about a pass that moved
        # three claims.  Measured: region 7c60 -> 92c0 with cellseg 5060 ->
        # 4b20, txtseg 5860 -> 5320 and bordseg 4da0 -> 4a20, while this row
        # printed all six unchanged and `Sheet now holds []`.
        sh_seg1, _ = pkg_seg(m, S, "Sheet")
        if sh_seg1 != sh_seg:
            print("  0 Sheet's REGION moved     %04x -> %04x" % (sh_seg, sh_seg1))

        def sword1(name):
            return u16(m.read(sh_seg1 * 16 + SH[name], 2))

        after = mine(claims(m, S), sh_seg1)
        live = set(c[0] for c in after)
        w1 = dict((n, sword1(n)) for n in MOVABLE)
        print("Sheet now holds %s"
              % ["%04x/%dKB" % (b, p // 64) for b, p, _, _ in after])
        print("  " + "  ".join("%s=%04x" % (n[3:], w1[n])
                               for n in MOVABLE))

        bad = 0
        moved = [n for n in MOVABLE if w1[n] != w0[n]]
        print("  1 a declared claim moved   %s"
              % (", ".join(n for n in moved) if moved
                 else "NO  <-- the run proves nothing"))
        bad += not moved

        for n in moved:
            h1 = hashlib.md5(m.read(w1[n] * 16, sizes[w0[n]] * 16)).hexdigest()
            if h1 != h0[n]:
                print("  2 %-22s CORRUPT (%s -> %s)"
                      % (n, h0[n][:12], h1[:12]))
                bad += 1
        if not bad:
            print("  2 contents survived        OK  (%d block(s) hashed)"
                  % len(moved))

        stale = [n for n in MOVABLE if w1[n] not in live]
        print("  3 every word names a claim %s"
              % ("OK" if not stale else "STALE: " + ", ".join(stale)))
        bad += bool(stale)
        nt = sword1("sh_noteseg")             # 81.94: carved, so it FOLLOWS
        nt_ok = nt == w1["sh_bordseg"] + 4 * 64   # the border table's move
        print("  3a sh_noteseg follows      %s" % ("OK" if nt_ok else
              "STALE: %04x, border table at %04x" % (nt, w1["sh_bordseg"])))
        bad += not nt_ok
        # 3c THE MODULES' VECTOR TABLES FOLLOWED THE REGION (NO-PINNING P1):
        # ch_v_* and sh_v_* hold OUR segment, stamped by the binds, and the
        # modules far-call back through them - a stale stamp lands in whatever
        # took the region's old place. sh_rgnreloc re-runs both binds.
        if sh_seg1 != sh_seg:
            vs = [u16(m.read(sh_seg1 * 16 + SH[t] + 2 + 4 * i, 2))
                  for t, n in (("ch_v_first", 9), ("sh_v_first", 2))
                  for i in range(n)]
            vec_ok = all(v == sh_seg1 for v in vs)
            print("  3c vector stamps follow    %s"
                  % ("OK (%d)" % len(vs) if vec_ok else
                     "STALE: %s, region at %04x"
                     % (["%04x" % v for v in vs], sh_seg1)))
            bad += not vec_ok
        else:
            print("  3c vector stamps follow    (not exercised: the region "
                  "did not move)")
        # 3b THE STAGING MOVED TOO (NO-PINNING 1). It sits third of the six
        # in the claim order, with cells and text under it and three claims
        # over it, so a pass that slid the claims above it down and left it
        # where it was is the picture of a declaration that did not take
        stg_ok = w1[STAGING] != w0[STAGING] and w1[STAGING] in live
        print("  3b %s moved and followed %s"
              % (STAGING, "OK  %04x -> %04x" % (w0[STAGING], w1[STAGING])
                 if stg_ok else "NO  (%04x -> %04x)"
                 % (w0[STAGING], w1[STAGING])))
        bad += not stg_ok

        # --- raise Sheet: the repaint is what reads sh_cellseg --------------
        # ON AN UNCOVERED PIECE OF ITS TITLE BAR, and both halves of that
        # sentence cost a run. By now heapfrag is front and left and the Disk
        # window front and right, so a click at the conventional (x+60, y+9)
        # lands on heapfrag and raises THAT - and the "repaint" compared below
        # is heapfrag's window sitting on top of Sheet's grid. And an
        # uncovered pixel of the CONTENT is worse: raising a Sheet by clicking
        # its grid moves the cell cursor, so the picture legitimately differs.
        # Both failed looking exactly like a stale sh_cellseg.
        pt = uncovered(m, S, sh_win, prefer_title=True)
        if pt is None:
            print("FAIL: Sheet's title bar is wholly covered; cannot raise it")
            return 1
        mo.click(*pt)
        os88marty.settle(m)
        park(mo, m)
        shot1 = m.vram("cga")

        def band(v):
            """Sheet's content rectangle, ONE BYTE PER PIXEL.

            m.vram() answers a row per scanline and a BYTE per pixel - the
            colour index, not packed bits - so the slice is x directly. The
            `x // 8` spelling that reads like a byte offset is what several
            older rows here use, and against a 640-wide frame it silently
            compares the leftmost 70 PIXELS of each row instead of the window:
            this row failed that way with 129 of 135 rows differing, and what
            it was comparing was the desktop where heapfrag's window used to
            be.
            """
            _, _, rows = v
            return b"".join(bytes(rows[y][cx0:cx1])
                            for y in range(cy0, min(cy1, len(rows))))

        b0, b1 = band(shot0), band(shot1)
        same = b0 == b1
        if not same:
            zn = m.read(S("wm_zn"), 1)[0]
            print("      z-order %s (top last), sheet is slot %d"
                  % (list(m.read(S("wm_zord"), zn)), sh_win.i))
            w = cx1 - cx0
            rows = [y for y in range(len(b0) // w)
                    if b0[y * w:(y + 1) * w] != b1[y * w:(y + 1) * w]]
            xs = set()
            for y in rows:
                r0, r1 = b0[y * w:(y + 1) * w], b1[y * w:(y + 1) * w]
                xs |= set(i for i in range(w) if r0[i] != r1[i])
            print("      differs at screen x %d..%d, y %d..%d (%d of %d rows)"
                  % (cx0 + min(xs), cx0 + max(xs), cy0 + rows[0],
                     cy0 + rows[-1], len(rows), len(b0) // w))
        print("  4 repaint identical        %s"
              % ("OK" if same else "DIFFERENT"))
        bad += not same

        # --- ...and the GROW BOX is still in the corner ---------------------
        # SPEC.md 11.1: wm_grow_paint draws a 13x13 box at the bottom-right
        # of the frontmost visible WF_SIZABLE window, and SHEET asks for the
        # flag since 81.112. Sheet is frontmost in BOTH shots, so the box
        # belongs to both.
        #
        # It did not. The day the flag went on this row failed "12 of 135
        # rows" at x 603..614, y 186..197 - which is that corner exactly -
        # because sh_drawstatus' white fill runs to the content's right edge
        # and ate the box the kernel had drawn, while wm_hit went on
        # reporting AL=4 from geometry alone: an invisible corner that still
        # resizes. The band saw it as a DIFFERENCE because the two shots lose
        # it differently - at rest SHEET's own status-bar repaint had run
        # last, and after the raise the kernel's wm_draw_win had - so the
        # pixels moved while the defect sat in both.
        #
        # So this names it. It is not redundant with the band: a box missing
        # from both shots is identical to itself.
        shl = [w for w in os88geom.windows(m, S) if w.i == sh_win.i][0]
        if shl.flags & os88geom.WF_SIZABLE:
            gx0, gy0 = shl.x + shl.w - 14, shl.y + shl.h - 14
            x0, x1 = max(gx0, cx0), min(gx0 + 13, cx1)
            y0, y1 = max(gy0, cy0), min(gy0 + 13, cy1)

            def boxed(v):
                """thm_bg fill, thm_ink frame: the box's top row and left
                column are one ink colour and its interior is not that
                colour. Clipped to the band, so normally 12x12 of the 13."""
                _, _, rows = v
                g = [bytes(rows[y][x0:x1]) for y in range(y0, min(y1,
                                                                 len(rows)))]
                if len(g) < 3 or len(g[0]) < 3:
                    return None                 # the frame clipped it away
                ink = set(g[0]) | set(r[0] for r in g)
                return len(ink) == 1 and not (set(g[1][1:-1]) & ink)

            b4, after = boxed(shot0), boxed(shot1)
            if b4 is None or after is None:
                print("  4a grow box                SKIPPED: clipped away at "
                      "(%d,%d)" % (gx0, gy0))
            else:
                miss = " and ".join(w for w, g in (("at rest", b4),
                                                    ("after the raise", after))
                                    if not g)
                print("  4a grow box in both shots  %s"
                      % ("OK" if not miss else
                         "MISSING %s - a sizable window's own repaint owes "
                         "OSAPI_WM_GROW (apps/os88api.inc)" % miss))
                bad += bool(miss)
        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
