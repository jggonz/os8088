#!/usr/bin/env python3
"""SPEC.md 2.8.3: a dropped module KEPT by the cache comes back from RAM - on
the next press, after the Task Manager has been and gone, and across a
compaction that has to MOVE it.

    make && python3 tests/modcache.py

The field report this row is for (Sticky Apps on the owner's 5150): the
second press of the menu showed a disk access, and a press after a visit to
the Task Manager took the whole load time again although the Task Manager
listed the module in RAM as purgeable. One boot of MartyPC's VGA XT on a
scratch copy of the shipped 360KB system disk, a Disk window open (the
window a posted compaction wakes) and the Task Manager opened FIRST, so its
region is the top-down claim above where STIKMENU.DRV's image lands:

  A  the first press LOADS the image: mod_need takes its disk arm
     (mod_need.sized) and int 13h reads >0 sectors of STIKMENU.DRV;
  B  closed, it is KEPT (MEM_P_MOD + 2 x its id, slots disarmed); the second
     press re-adopts it (mod_need.held) and spends EXACTLY ONE int 13h: the
     boot sector, which is the scan's own media check (SPEC.md 18.9.1.1 -
     a 5150 has no change line, and a human gap stops the motor). It was
     TWO: drv_vol_back remounted the volume to come back out of STIKAPPS\\
     (dsk_chdir_q where dsk_goto moves with no I/O);
  C  the Task Manager closes - a hole ABOVE the kept image - and a
     compaction is posted at rank 0FFh, the rank that DISSOLVES every
     cheaper pinned cache: the image MOVES into the hole (its MODR_SEG
     re-pointed, its record still MEM_P_MOD + 2 x the id) instead of being
     dropped. It was dropped twice over: the kept record was PINNED, so a
     barrier the compactor dissolves, and the ascending pass sent a movable
     claim of the other door to the same dissolve;
  D  the third press re-adopts it at the NEW base: no mod_need.sized, not
     one sector of STIKMENU.DRV read, the panel lists the disk's three
     Sticky Apps out of the moved image.

E-G, a second boot (owner_steps): the owner's steps as reported, on the
owner's own machine's twin (5150, Hercules, an ST-225 as C:), with NOTHING
forced. No compaction or shed runs there at all; the "whole load time" was
the menu's scan, and a fixed disk made it read the boot sector twice a press.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1), each measured red on
2026-11-12: drv_vol_back back on dsk_chdir_q_x and B reads [(0, 1), (0, 1)]
- the second the remount; mod_drop's `mov word [ss:si+MC_RLOC], ...` out,
or mem_cp_walk's direction test back on `jc .pinned`, and C finds MODR_SEG
0 - the compaction DISSOLVED the image - so D has nothing to re-adopt; the
mem_rr_tab row for mod_tab out and C finds MODR_SEG still 0x9a60 naming no
record, the image moved from under it.
"""
import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88marty   # noqa: E402
import os88ui      # noqa: E402
import os88fat     # noqa: E402
import os88geom as geom  # noqa: E402
import heapmap     # noqa: E402
import stikmenu as SM  # noqa: E402

BDIR = os.path.join(HERE, "..", os.environ.get("OS88_BUILD", "build"))
T = 120
fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def lba_set(img, name):
    """Every LBA the file occupies on the scratch image."""
    f = os88fat.Fat12(img)
    _, _, e = f.find(name)
    first = struct.unpack_from("<H", e, 26)[0]
    out = set()
    for c in f.chain(first):
        l0 = f.cluster_lba(c)
        out.update(range(l0, l0 + f.spc))
    return out, f.spt, f.heads


def reads(tr, spt, heads):
    """The int 13h READS a trace caught, as (lba, count)."""
    out = []
    for h in tr.hits:
        if h.get("type") != "int" and not h["name"].startswith("?"):
            continue
        r = h.get("regs") or {}
        ax, cx, dx = r.get("ax", 0), r.get("cx", 0), r.get("dx", 0)
        if ax >> 8 != 2 or dx & 0x80:
            continue
        cyl = (cx >> 8) | ((cx & 0xC0) << 2)
        sec, head = cx & 0x3F, dx >> 8
        out.append(((cyl * heads + head) * spt + sec - 1, ax & 0xFF))
    return out


def desktop(ui):
    """Make the DESKTOP the active surface - its bar carries the Sticky Apps
    cell (SPEC.md 22.28.1) - by clicking a point no window covers, left of
    the drive zones' column and above the dock."""
    wins = [w for w in ui.windows() if w.visible]
    for x in range(40, ui._word("vid_pw") // 2, 40):
        for y in range(ui._word("vid_ph") - 80, geom.MBAR_H + 20, -40):
            if not any(w.covers(x, y) for w in wins):
                ui.mo.click(x, y)
                ui._wait(lambda: "Sticky Apps" in [c[0]
                                                   for c in ui.menus()],
                         "the desktop's bar", T)
                return
    raise os88ui.UIError("no bare desktop to click: %r" % ui.titles())


HEAP = ("mem_compact", "mem_shed_one")    # a pass, or a shed, ran at all


def press(m, menu, label, spt, heads, drv):
    """Drop the panel and close it, tracing int 13h and mod_need's arms.
    The floppy motor is let stop first (drive A's bit in 0040:003F, which
    dsk_media_ok reads beside the countdown), so the scan's media check
    READS every time, as it does after any human gap, and the count does
    not depend on how fast the harness pressed (SPEC.md 18.9.1.1)."""
    os88marty.until(m, lambda mm: mm.read(0x43F, 1)[0] & 1 == 0,
                    "drive A's motor to stop", guest=30.0)
    with os88marty.bp_trace(m, {"type": "int", "addr": 0x13},
                            "mod_need.sized", "mod_need.held", *HEAP,
                            regs=True, cap=4000) as tr:
        slabs = menu.drop()
        n = menu.b("stm_n")
        menu.release_off()
        menu.gone()
    rd = reads(tr, spt, heads)
    mine = sum(1 for lba, k in rd for x in range(lba, lba + k) if x in drv)
    print("  %s: %d int 13h read(s) %r, %d of them STIKMENU.DRV's sectors; "
          "mod_need.sized %d, .held %d; %d listed"
          % (label, len(rd), rd, mine, tr.count("mod_need.sized"),
             tr.count("mod_need.held"), n))
    return dict(rd=rd, mine=mine, sized=tr.count("mod_need.sized"),
                held=tr.count("mod_need.held"), n=n, slabs=slabs,
                heap=sum(tr.count(h) for h in HEAP))


def owner_steps():
    """THE OWNER'S STEPS, as reported, on the owner's machine: a 5150 with
    the Hercules card, 640KB, the 360KB system disk in A: and an ST-225 as
    C: (MartyPC's GLaBIOS twin of it). Nothing is forced - no compaction is
    posted - and nothing else is open:

      E  the first press loads STIKMENU.DRV; the second re-adopts it;
      F  the Task Manager opens and the desktop is clicked (the Task
         Manager left listing the kept image as a purgeable);
      G  the third press re-adopts it again.

    The finding this pins (2026-11-12): NO compaction and NO shed runs in
    these steps, and the image is never dropped - mod_need.held both times,
    not one sector of the file read. What the owner timed as "the full load"
    was the scan's floppy work: the media check, the card tracks the Task
    Manager's own load evicted from 18.95's round-robin cache, and - with a
    fixed disk to scan - a SECOND boot-sector read every press, the return
    from C:\\STIKAPPS to A: remounting it (drv_vol_back) with the head out
    at the cards' cylinder. stm_scan's batch (SPEC.md 18.9.3, 110.12.2) is
    what makes that return a banked BPB, so each re-adopting press reads the
    boot sector ONCE.

    **BREAK IT ON PURPOSE**, measured red on 2026-11-12: stm_scan's
    `dkf_dsk_batch_begin` out, and E's second press and G read the boot
    sector TWICE."""
    tmp = tempfile.mkdtemp(prefix="modcache-owner-")
    img = os.path.join(tmp, "owner.img")
    shutil.copy(os88marty.system_disk(os.path.join(BDIR, "os8088-360.img")),
                img)
    drv, spt, heads = lba_set(img, "STIKMENU.DRV")
    try:
        with os88marty.launch(img, machine=os88marty.machine(
                "os8088_5150_herc_hdd_gla", None), boot=True) as m:
            ui = os88ui.UI(m)
            ui.up(limit=180.0)
            os88marty.no_saver(m)
            menu = SM.Menu(m, ui)
            row = ui._S("mod_tab") + menu.mod * SM.MODR_SIZE

            def seg():
                return struct.unpack("<H", bytes(m.read(row, 2)))[0]

            def boots(r):
                return sum(1 for x in r["rd"] if x == (0, 1))

            print("E: the first press loads, the second re-adopts")
            e1 = press(m, menu, "first", spt, heads, drv)
            check(e1["sized"] == 1 and e1["mine"] > 0,
                  "the first press read the file (%d of its sectors)"
                  % e1["mine"])
            menu.gone()
            s0 = seg()
            e2 = press(m, menu, "second", spt, heads, drv)
            check(e2["sized"] == 0 and e2["held"] == 1 and e2["mine"] == 0,
                  "the second re-adopted it (sized %d, held %d)"
                  % (e2["sized"], e2["held"]))
            check(boots(e2) == 1, "ONE boot-sector read, the media check, "
                  "with C: scanned too (%d: %r)" % (boots(e2), e2["rd"]))

            print("F: the Task Manager opens; the desktop is clicked")
            with os88marty.bp_trace(m, *HEAP, cap=100) as tr:
                ui.menu_pick("Apple", "Task Manager")
                ui.wait_window("Task Manager", limit=T)
                desktop(ui)
            check(tr.count() == 0, "no compaction and no shed (%d)"
                  % tr.count())
            check(seg() == s0 and menu.dropped() == "kept",
                  "still kept at %#06x (%s)" % (s0, menu.why()))

            print("G: the third press re-adopts it")
            g = press(m, menu, "third", spt, heads, drv)
            check(g["sized"] == 0 and g["held"] == 1 and g["mine"] == 0
                  and seg() == s0 and g["n"] == 3,
                  "re-adopted at %#06x, no sector of the file, %d listed "
                  "(sized %d, held %d)" % (seg(), g["n"], g["sized"],
                                           g["held"]))
            check(e2["heap"] + g["heap"] == 0,
                  "no compaction and no shed in either press")
            check(boots(g) == 1, "ONE boot-sector read (%d: %r)"
                  % (boots(g), g["rd"]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    tmp = tempfile.mkdtemp(prefix="modcache-")
    img = os.path.join(tmp, "modcache.img")
    shutil.copy(os88marty.system_disk(os.path.join(BDIR, "os8088-360.img")),
                img)
    drv, spt, heads = lba_set(img, "STIKMENU.DRV")
    try:
        with os88marty.launch(img, machine=os88marty.machine(
                "os8088_xt_vga", None), boot=True) as m:
            ui = os88ui.UI(m)
            ui.up(limit=180.0)
            os88marty.no_saver(m)
            menu = SM.Menu(m, ui)
            row = ui._S("mod_tab") + menu.mod * SM.MODR_SIZE
            kept = SM.MEM_P_MOD + 2 * menu.mod

            def seg():
                return struct.unpack("<H", bytes(m.read(row, 2)))[0]

            def owner(s):
                return [c.own for c in heapmap.Map(m, SM._Syms(ui)).claims
                        if s and c.seg == s]

            ui.menu_pick("Apple", "Task Manager")
            tm = ui.wait_window("Task Manager", limit=T)
            desktop(ui)

            print("A: the first press loads STIKMENU.DRV")
            a = press(m, menu, "first", spt, heads, drv)
            check(a["sized"] == 1 and a["mine"] > 0,
                  "mod_need read the file (%d of its sectors)" % a["mine"])
            menu.gone()
            s0 = seg()
            check(menu.dropped() == "kept" and owner(s0) == [kept],
                  "closed, it is KEPT at %#06x as %#06x (%r)"
                  % (s0, kept, [hex(o) for o in owner(s0)]))

            print("B: the second press comes from RAM")
            b = press(m, menu, "second", spt, heads, drv)
            check(b["sized"] == 0 and b["held"] == 1 and b["mine"] == 0,
                  "re-adopted, no load (sized %d, held %d)"
                  % (b["sized"], b["held"]))
            check(len(b["rd"]) == 1 and b["rd"][0] == (0, 1),
                  "ONE int 13h, the boot sector's media check (%r)" % b["rd"])
            check(seg() == s0 and b["n"] == 3, "same claim, three listed")

            print("C: the Task Manager goes; a compaction must MOVE the image")
            ui.close(tm)
            check(seg() == s0 and owner(s0) == [kept],
                  "still kept at %#06x after the Task Manager closed" % s0)
            dw = ui.open_drive("A")         # the window the pass wakes
            geom.post_compact(m, dw, 0xFF, ui._S)
            ui._wait(lambda: ui._word("mem_cpq") == 0, "the posted "
                     "compaction to run", T)
            ui.settle()
            s1 = seg()
            check(s1 != 0 and owner(s1) == [kept],
                  "the compaction kept it: MODR_SEG %#06x, its record %r"
                  % (s1, [hex(o) for o in owner(s1)]))
            check(s1 != s0, "...and MOVED it into the hole (%#06x -> %#06x)"
                  % (s0, s1))
            ui.close(dw)                    # the desktop's bar again
            desktop(ui)

            if not (s1 and owner(s1) == [kept]):
                check(False, "D not run: nothing kept to re-adopt")
            else:
                print("D: the third press re-adopts it where it went")
                d = press(m, menu, "third", spt, heads, drv)
                check(d["sized"] == 0 and d["held"] == 1 and d["mine"] == 0,
                      "re-adopted from RAM (sized %d, held %d, %d sectors of "
                      "the file)" % (d["sized"], d["held"], d["mine"]))
                check(seg() == s1 and d["n"] == 3,
                      "the moved image ran: %d listed at %#06x"
                      % (d["n"], seg()))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    owner_steps()
    print("modcache: %s" % ("PASS" if not fails else
                           "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
