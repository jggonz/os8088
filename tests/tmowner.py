#!/usr/bin/env python3
"""A raise cache is listed under PURGEABLE, whoever caused it (SPEC.md 28.4.10)

    make && python3 tests/tmowner.py [--machine os8088_xt_vga]

MEM_P_WSAVE + slot is a KERNEL tag, so the heap page's System arm once
swallowed every raise cache: tens of KB of a 640KB machine appeared on the one
page that reports memory as a `WinSave` row belonging to the kernel. SPEC.md
28.4.5 then filed it under the package that owns the window. Since the memory
and heap pages merged (KERN-MEGA-PLAN 14.70.2) a purgeable claim is its OWN
group, `Purgeable`: it is in nobody's total (SPEC.md 28.4.1), so filing it
under an owner made that owner's rows add to more than its heading.

  KERNEL   cover the Disk window - a window the KERNEL made, with no instance
           behind it - and its cache must be in Purgeable, not System
  PACKAGE  cover the Calculator and its cache must be in Purgeable too, not
           in the CALCULATOR's group

READ OFF THE PAGE'S OWN CLASSIFICATION, not off the glass. Every paint of the
memory page takes the claim table (`tm_claims`) and files each record into a
group (`tm_cgid`, one byte per record), and the list draws a group's rows as
exactly the records whose byte names it (`tm_mbrs`) - so "which group is this
cache's row under" IS that byte. Both are read with the guest PAUSED, so the
two come from one paint. This row used to answer it by breaking on every
`font_run` the page made and reassembling the drawn rows, which cost ~450 s,
most of it spent stopping the emulator for each lettered chunk and cycling the
page round until a capture came back whole (KERN-MEGA-PLAN 14.70.3).

The two halves are in this order and cannot be swapped: the drag that makes
the Calculator hold a cache raises the Disk window, and its cache goes with
the redraw.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88ui, os88marty, os88sym, dispapps

MACHINE = "os8088_xt_vga"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

INST_MAX, MEM_MAX, CLS_RECSZ = 12, 32, 6    # apps/os88api.inc
MEM_P_WSAVE, MEM_P_WSAVE_N = 0xFB10, 16     # apps/os88api.inc
XMW_SIZE = 6                                # ...and OSAPI_XMEM_WALK's record
XMW_LSIZE = 8                               # ...LONG, as the kern_mega arm asks
                                            # (BX = XMW_LONG, KERN-MEGA-PLAN 14.92)
TMG_PURGE = INST_MAX + 2                    # apps/taskmgr/taskmgr.asm
TITLE_H = 18
fails = []

with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                 machine=MACHINE,
                 quick=True) as ui:
    ui.still(0)     # booted on STATE; the first paint done (SOAK-SPEED 4)
    m = ui.m
    S = os88sym.linear

    ui.path("B:/APPS/CALC.O88")
    calc = ui.window("Calculator").i
    disk = ui.window("APPS").i
    ui.path("A:/SYSTEM/TASKMGR.O88")
    got, i = None, 0
    while dispapps.pkg_seg(m, i) is not None:
        got = dispapps.pkg_seg(m, i)
        i += 1
    if got is None:
        sys.exit("the Task Manager did not open - %r" % ui.titles())
    slot, seg = got
    mega = getattr(m, "bochs", False)
    # (on kern_mega the region is an LDT selector: its base out of the LDT)
    base = (m.lin(seg) if mega else seg << 4) + dispapps.img_size("taskmgr")

    def key(v):
        """A claim's word as the claim map keys it: on kern_mega
        (KERN-MEGA-MEMORY-PLAN MC7.4) a raise cache is NAMED by its selector
        (wm_su_segs) while the claim snapshot carries the block's paragraph,
        so the two meet at the paragraph under the selector"""
        if mega and v and 0x100 <= v < 0xA000 and v & 4:
            b = m.selector(v)
            return b[0] >> 4 if b else v
        return v

    def tmw(name, n=2):
        return int.from_bytes(m.read(base + dispapps.bss_off("taskmgr", name),
                                     n), "little")

    def su(i):
        b = m.read(S("wm_su_segs") + i * 2, 2)
        return b[0] | (b[1] << 8)

    def W(i):
        return [x for x in ui.windows() if x.i == i][0]

    # Park all three clear: the page top right, Disk bottom left, Calc top
    # left - so nothing covers anything until a leg below means it to.
    ui.move_window(W(slot), 396, 26)
    ui.move_window(W(disk), 8, 248)
    ui.move_window(W(calc), 8, 40)

    # ...onto the memory page: one click on the content cycles 0 -> 1.
    ui.raise_window(W(slot))            # (a click on a window behind
    w = W(slot)                         # others only raises it)
    ui.mo.click(w.x + 20, w.y + TITLE_H + 40)
    ui._wait(lambda: tmw("tm_view", 1) == 1, "the memory page", guest=10,
             snapshot=lambda: "tm_view %d" % tmw("tm_view", 1))

    def group_of(cache):
        """The group the page filed `cache` under, once a paint has seen it.

        Polled on the GUEST clock: the page repaints on its own interval, and
        the record only appears in its snapshot after the claim does."""
        box = {}

        def seen():
            m.pause()
            try:
                snap = m.read(base + dispapps.bss_off("taskmgr", "tm_claims"),
                              MEM_MAX * CLS_RECSZ)
                gid = m.read(base + dispapps.bss_off("taskmgr", "tm_cgid"),
                             MEM_MAX)
                if mega and key(cache) >= 0x10000:
                    # ...ABOVE 1MB: on a kern_mega machine with a pool the
                    # cache is a pool block, which the page walks into tm_xw
                    # (OSAPI_XMEM_WALK, XMW_OFF its KB from the pool's base)
                    # and files in tm_xgid, its classification beside tm_cgid
                    xn = tmw("tm_xwn")
                    xw = m.read(base + dispapps.bss_off("taskmgr", "tm_xw"),
                                xn * XMW_LSIZE)
                    xg = m.read(base + dispapps.bss_off("taskmgr", "tm_xgid"),
                                xn)
            finally:
                m.run()
            if mega and key(cache) >= 0x10000:
                kb = ((key(cache) << 4) - 0x100000) >> 10
                for r in range(xn):
                    if xw[r * XMW_LSIZE] | (xw[r * XMW_LSIZE + 1] << 8) == kb:
                        box["gid"] = xg[r]
                        return True
                return False
            for r in range(MEM_MAX):
                rec = snap[r * CLS_RECSZ:(r + 1) * CLS_RECSZ]
                own = rec[4] | (rec[5] << 8)            # CLS_OWN
                # ...AND IT IS A RAISE CACHE'S RECORD: a snapshot taken
                # before the claim can hold another tenant of the same
                # paragraph - one of the Calculator's own claims, freed when
                # it moved - and that is group 3, read as this row's
                # failure on the one run in many where the page had not yet
                # repainted. The owner names a raise cache or the page has
                # not seen the claim yet, and the wait goes on.
                if key(rec[0] | (rec[1] << 8)) == key(cache) and \
                        MEM_P_WSAVE <= own < MEM_P_WSAVE + MEM_P_WSAVE_N:
                    box["gid"] = gid[r]
                    return True
            return False
        try:
            ui._wait(seen, "the page's snapshot holding %04X" % cache, guest=20)
        except os88ui.UIError:
            return None
        return box["gid"]

    # --- KERNEL ------------------------------------------------------------
    d = W(disk)
    ui.move_window(W(calc), d.x + 40, d.y + 20)
    ds = su(disk)
    dg = group_of(ds) if ds else None
    print("KERNEL  : Disk window cache %04X -> group %r (Purgeable is %d)"
          % (ds, dg, TMG_PURGE))
    if not ds:
        fails.append("KERNEL: the Disk window banked nothing when the "
                     "Calculator landed on it, so there is no row to file")
    elif dg is None:
        fails.append("KERNEL: the page never listed the cache %04X - its "
                     "snapshot did not take the claim within 20 guest "
                     "seconds" % ds)
    elif dg != TMG_PURGE:
        fails.append("KERNEL: a raise cache is purgeable, so it is listed "
                     "under Purgeable whoever caused it (SPEC.md 28.4.10) - "
                     "the Disk window's is in group %d" % dg)

    # --- PACKAGE -----------------------------------------------------------
    c = W(calc)
    ui.move_window(W(disk), c.x + 40, c.y + 20)
    cs = su(calc)
    cg = group_of(cs) if cs else None
    print("PACKAGE : Calc cache %04X -> group %r (the Calculator's slot is "
          "%d)" % (cs, cg, calc))
    if not cs:
        fails.append("PACKAGE: the Calculator banked nothing when the Disk "
                     "window landed on it, so there is no row to file")
    elif cg is None:
        fails.append("PACKAGE: the page never listed the cache %04X" % cs)
    elif cg != TMG_PURGE:
        fails.append("PACKAGE: a raise cache is purgeable and in nobody's "
                     "total, so it is listed under Purgeable and not under "
                     "its window's package (SPEC.md 28.4.10) - the "
                     "Calculator's is in group %d%s"
                     % (cg, " (its own slot)" if cg == calc else ""))

    print()
    for f in fails:
        print("FAIL  " + f)
    if not fails:
        print("PASS  both raise caches are Purgeable's, the kernel window's "
              "and the package's")
    sys.exit(1 if fails else 0)
