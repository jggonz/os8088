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
     press re-adopts it (mod_need.held) and makes NO int 13h call: the scan
     is kept below the image's scratch tail (SPEC.md 110.12.2), so there is
     no media check to make. It was ONE (the boot sector - a 5150 has no
     change line, and a human gap stops the motor), and before that TWO,
     drv_vol_back remounting the volume to come back out of STIKAPPS\\;
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
Now a re-adopting press makes no int 13h call on either drive.

H-K, a third boot (floppy_steps): a floppy-only 5150, the kept scan, and
the ACTIONS that still go to the disk and refuse as they always did.

Every count is EVERY int 13h the trace caught (nint): the floppy-read
filter hid the fixed disk's calls, two or three a press on E-G's machine.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1), each measured red on
2026-11-12: stm_scan's `je .kept` made `jmp short .scan` (every press
scans, the old behaviour) and B, D, E's second press, G and H's second
press all FAIL (1, 4, 3, 4 and 1 int 13h calls), and I and J with them;
`je .kept` made an unconditional jump (the kept flag alone, no [dsk_wgen]
compare) and K FAILS - no rescan after SYSTEM.CFG's write. Before the kept
scan: drv_vol_back back on dsk_chdir_q_x and B reads [(0, 1), (0, 1)]
- the second the remount; mod_drop's `mov word [ss:si+MC_RLOC], ...` out,
or mem_cp_walk's direction test back on `jc .pinned`, and C finds MODR_SEG
0 - the compaction DISSOLVED the image - so D has nothing to re-adopt; the
mem_rr_tab row for mod_tab out and C finds MODR_SEG still 0x9a60 naming no
record, the image moved from under it.
"""
import os
import shutil
import struct
import subprocess
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


def nint(tr):
    """EVERY int 13h the trace caught - any function, either drive. A kept
    scan (SPEC.md 110.12.2) owes none at all, so the floppy-only filter
    above would let a fixed disk's reads, or a reset, hide in the count."""
    return sum(1 for h in tr.hits
               if h.get("type") == "int" or h["name"].startswith("?"))


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
                            "mod_need.sized", "mod_need.held", "dsk_find_x",
                            *HEAP, regs=True, cap=4000) as tr:
        slabs = menu.drop()
        n = menu.b("stm_n")
        menu.release_off()
        menu.gone()
    rd = reads(tr, spt, heads)
    mine = sum(1 for lba, k in rd for x in range(lba, lba + k) if x in drv)
    print("  %s: %d int 13h call(s), %d floppy read(s) %r, %d of them "
          "STIKMENU.DRV's sectors; mod_need.sized %d, .held %d; %d listed"
          % (label, nint(tr), len(rd), rd, mine, tr.count("mod_need.sized"),
             tr.count("mod_need.held"), n))
    return dict(rd=rd, mine=mine, sized=tr.count("mod_need.sized"),
                held=tr.count("mod_need.held"), n=n, slabs=slabs,
                heap=sum(tr.count(h) for h in HEAP), n13=nint(tr),
                scan=tr.count("dsk_find_x"))


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
            check(e2["n13"] == 0 and e2["n"] == 3, "NO int 13h, with C: "
                  "in the kept scan too (%d call(s), %r; %d listed)"
                  % (e2["n13"], e2["rd"], e2["n"]))

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
            check(g["n13"] == 0, "NO int 13h after the Task Manager's "
                  "visit (%d call(s), %r)" % (g["n13"], g["rd"]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def pick_app(m, ui, menu, fname):
    """Drop the panel, release on FNAME's row (its Add) and let the posted
    act run and drop the image. The panel's own drop is traced like a press;
    the act's refusals - stm_a_launch's beep when STIKAPPS\\ is not on the
    disk now in the drive, the loader's toast when the file is not - are
    counted."""
    os88marty.until(m, lambda mm: mm.read(0x43F, 1)[0] & 1 == 0,
                    "drive A's motor to stop", guest=30.0)
    with os88marty.bp_trace(m, {"type": "int", "addr": 0x13},
                            cap=4000) as tp:
        slabs = menu.drop()
    ents = SM.entries(menu)
    k = [i for i, e in enumerate(ents) if e["file"] == fname]
    apps = [i for i, x in enumerate(slabs) if x["kind"] == SM.SK_APP]
    if not k or not apps:
        menu.release_off()
        menu.gone()
        return dict(n13=nint(tp), ok=False, ents=ents)
    with os88marty.bp_trace(m, "snd_beep", "ld_say_status",
                            "mod_need.sized", cap=100) as ta:
        menu.release_on(slabs, apps[k[0] - menu.b("stm_top")])
        menu.gone()
        ui.settle()
    return dict(n13=nint(tp), ok=True, ents=ents, beep=ta.count("snd_beep"),
                said=ta.count("ld_say_status"),
                sized=ta.count("mod_need.sized"))


def floppy_steps():
    """H-K: a FLOPPY-ONLY 5150 (Hercules, the 360KB system disk in A:, no C:)
    - the kept scan, and the ACTIONS that must still go to the disk:

      H  the first press loads and scans; the second, after the motor has
         stopped, makes NO int 13h call at all - not the media check, not a
         directory sector (SPEC.md 110.12.2's kept scan);
      I  a copy of the APPS disk goes in A: (no STIKAPPS\\ on it). A press
         still makes no call and lists the kept three; Add on the Desk
         Calendar walks to the disk - which is NOT the one scanned - and
         refuses as it always has: stm_a_launch's beep, no window;
      J  a copy of the system disk with a new serial and STIKAPPS\\
         DESKCAL.O88 DELETED: Add finds the folder, the loader finds no file,
         and says so (ld_say_status's toast); no window;
      K  the system disk back: Add LAUNCHES it - the act's reads find the
         file - and its registration WRITES SYSTEM.CFG to A:, which moves
         [dsk_wgen]: the next press SCANS again (dsk_find_x runs) and lists
         it running.

    **BREAK IT ON PURPOSE**: stm_scan's `je .kept` out (every press scans)
    and H's second press makes int 13h calls again; the stm_kgen compare out
    (the kept flag alone) and K's press does not rescan."""
    tmp = tempfile.mkdtemp(prefix="modcache-fd-")
    img = os.path.join(tmp, "sys.img")
    shutil.copy(os88marty.system_disk(os.path.join(BDIR, "os8088-360.img")),
                img)
    apps = os.path.join(tmp, "apps.img")
    shutil.copy(os88marty.apps_disk(os.path.join(BDIR, "apps360.img")), apps)
    gone = os.path.join(tmp, "gone.img")
    shutil.copy(img, gone)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    subprocess.run(["mdel", "-i", gone, "::/STIKAPPS/DESKCAL.O88"],
                   check=True, env=env)
    with open(gone, "r+b") as f:            # a NEW serial: another disk to
        f.seek(0x27)                        # the mount's swap detector
        ser = bytearray(f.read(4))
        ser[0] ^= 0x5A
        f.seek(0x27)
        f.write(ser)
    drv, spt, heads = lba_set(img, "STIKMENU.DRV")
    try:
        with os88marty.launch(img, machine=os88marty.machine(
                "os8088_5150_herc_gla", None), boot=True) as m:
            ui = os88ui.UI(m)
            ui.up(limit=180.0)
            os88marty.no_saver(m)
            menu = SM.Menu(m, ui)

            print("H: a floppy-only 5150 - the first press scans, the second "
                  "reads nothing")
            h1 = press(m, menu, "first", spt, heads, drv)
            check(h1["sized"] == 1 and h1["n"] == 3 and h1["n13"] > 0,
                  "the first press loaded and scanned (%d int 13h)"
                  % h1["n13"])
            menu.gone()
            h2 = press(m, menu, "second", spt, heads, drv)
            check(h2["held"] == 1 and h2["n13"] == 0 and h2["n"] == 3,
                  "the second: re-adopted, NO int 13h, three listed "
                  "(%d, %r)" % (h2["n13"], h2["rd"]))

            print("I: the apps disk in A:, then Add")
            m.mount(0, apps)
            r = pick_app(m, ui, menu, b"DESKCAL.O88")
            check(r["ok"] and r["n13"] == 0,
                  "the press made no call and listed the kept three "
                  "(%d int 13h, %r)" % (r["n13"],
                                        [e["file"] for e in r["ents"]]))
            check(r.get("beep") == 1 and not SM.is_open(ui, SM.TITLE),
                  "Add refused - the act's walk found no STIKAPPS\\: "
                  "%d beep(s), no Desk Calendar" % r.get("beep", 0))

            print("J: the calendar deleted off a system disk, then Add")
            m.mount(0, gone)
            r = pick_app(m, ui, menu, b"DESKCAL.O88")
            txt, on = ui.toast()
            check(r["ok"] and r["n13"] == 0 and r.get("said") == 1
                  and not SM.is_open(ui, SM.TITLE),
                  "Add refused by the loader, which says so: %r "
                  "(%d int 13h at the press, %d said)"
                  % (txt, r["n13"], r.get("said", 0)))

            print("K: the system disk back; Add launches, and the write it "
                  "makes stales the scan")
            m.mount(0, img)
            r = pick_app(m, ui, menu, b"DESKCAL.O88")
            w = ui.wait_window(SM.TITLE, limit=T)
            check(r["ok"] and w is not None, "the Desk Calendar is up")
            ui._wait(lambda: SM.stik_rows(m, ui), "its row registered", T)
            ui.settle()
            desktop(ui)
            k = press(m, menu, "after the write", spt, heads, drv)
            ents = SM.entries(menu)
            run = [e["run"] for e in ents if e["file"] == b"DESKCAL.O88"]
            check(k["scan"] > 0 and k["n13"] > 0,
                  "SYSTEM.CFG's write moved [dsk_wgen]: the press scanned "
                  "(%d dsk_find_x, %d int 13h)" % (k["scan"], k["n13"]))
            check(run == [1], "...and lists the calendar running (%r)" % run)
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
            check(b["n13"] == 0, "NO int 13h at all: the scan is KEPT "
                  "(SPEC.md 110.12.2) - %d call(s), %r" % (b["n13"], b["rd"]))
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
                check(d["n13"] == 0, "...out of the scan the MOVE carried: "
                      "no int 13h (%d, %r)" % (d["n13"], d["rd"]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    owner_steps()
    floppy_steps()
    print("modcache: %s" % ("PASS" if not fails else
                           "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
