#!/usr/bin/env python3
"""DIAGNOSTIC PROBE, not a suite row: kern_small's heap around Paint.

The owner's report (2026-11-12): on kern_small, "even right after boot
slightly fragmented ram means the second paint claim fails even though there
is otherwise enough ram". This boots build/small360.img (single-disk at 128KB,
SPEC.md 24.5.6) on the named machine, prints the claim map at each step, opens
the small Paint, and reports Paint's own view of its four claims.

    python3 tests/smallfrag.py [--machine M] [--seq boot|cp|disk|cp2]

`--seq` is what happens BEFORE Paint opens:
    boot   nothing (Paint straight after the desktop)
    cp     open and close the Control Panel (CTRL.DRV loaded, then dropped)
    disk   open a Disk window on A: and close it
    paint2 open Paint, close it, open it again
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

import os88build as _B                                   # noqa: E402
_B.use_build("build/smallk")
os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")

import os88geom                                          # noqa: E402
import os88marty                                         # noqa: E402
import os88sym                                           # noqa: E402
import os88ui                                            # noqa: E402
import dispapps                                          # noqa: E402
import importlib.util as _iu                              # noqa: E402
_sp = _iu.spec_from_file_location("heapmap_t", os.path.join(ROOT, "tools", "heapmap.py"))
heapmap = _iu.module_from_spec(_sp)
_sp.loader.exec_module(heapmap)

ARM = ("KERN_SMALL",)
PT = ["pt_mode", "pt_base", "pt_smaxp", "pt_needp", "pt_growp", "pt_tabseg",
      "pt_tbparas", "pt_scseg", "pt_undoseg", "pt_clipseg", "pt_cw", "pt_ch"]


def u16(b, i=0):
    return int.from_bytes(b[i:i + 2], "little")


def heap(m, label):
    S = lambda n: m.sym(n, ARM)                              # noqa: E731
    eq = os88sym.equates(ARM)
    base = u16(m.read(S("mem_base"), 2))
    top = u16(m.read(S("mem_top"), 2))
    sz = os88geom.MC_SIZE
    tab = m.read(S("mem_tab"), eq["MEM_MAX"] * sz)
    rows = []
    for i in range(eq["MEM_MAX"]):
        r = tab[i * sz:(i + 1) * sz]
        seg = u16(r, 0)
        if seg:
            rows.append((seg, u16(r, 2), u16(r, 4), u16(r, 6)))
    rows.sort()
    print("== %s: heap %04X..%04X = %.1f KB" % (label, base, top,
                                                 (top - base) / 64.0))
    cur, free, run, purge = base, 0, 0, 0
    for seg, para, own, dma in rows:
        if seg > cur:
            free += seg - cur
            run = max(run, seg - cur)
            print("   %04X  %6.2fKB  -- hole --" % (cur, (seg - cur) / 64.0))
        pg = eq["MEM_PG_MIN"] <= (own >> 8) <= eq["MEM_PG_MAX"]
        if pg:
            purge += para
        print("   %04X  %6.2fKB  %-22s %s%s" % (
            seg, para / 64.0, heapmap.owner(own) + " (%04X)" % own,
            "hi " if dma & 0x8000 else "lo ", "PURGEABLE" if pg else "pinned"))
        cur = max(cur, seg + para)
    if top > cur:
        free += top - cur
        run = max(run, top - cur)
        print("   %04X  %6.2fKB  -- hole (top) --" % (cur, (top - cur) / 64.0))
    print("   free %.2fKB, largest hole %.2fKB, in caches %.2fKB"
          % (free / 64.0, run / 64.0, purge / 64.0))


def paint(m, small=True):
    got = dispapps.pkg_seg(m, 0)
    if got is None:
        print("== Paint: no window")
        return
    seg = got[1]
    img = dispapps.img_size("paint", small)
    out = {}
    for n in PT:
        try:
            o = (int(seg) << 4) + img + dispapps.bss_off("paint", n, small)
        except SystemExit:
            continue
        out[n] = u16(m.read(o, 2))
    out["pt_mode"] &= 0xFF
    print("== Paint at %04X: %s" % (int(seg), " ".join(
        "%s=%04X" % (k, v) for k, v in out.items())))


def main(argv):
    machine = "os8088_5150_gla_128k"
    seq = "boot"
    if "--machine" in argv:
        machine = argv[argv.index("--machine") + 1]
    if "--seq" in argv:
        seq = argv[argv.index("--seq") + 1]
    os.chdir(ROOT)
    with os88ui.boot("build/small360.img", machine=machine, limit=240) as ui:
        m = ui.m
        heap(m, "desktop (%s)" % machine)
        if seq == "cp":
            ui.menu_pick("Apple", "Control Panel")
            os88marty.ui_done(m)
            heap(m, "Control Panel open")
            print("   windows: %r" % (ui.titles(),))
            ui.close(ui.front())
            os88marty.ui_done(m)
            heap(m, "Control Panel closed")
        elif seq == "disk":
            w = ui.open_drive("A")
            heap(m, "Disk A: open")
            ui.close(w)
            os88marty.ui_done(m)
            heap(m, "Disk A: closed")
        ui.path("A:/APPS/PAINT.O88")
        os88marty.ui_done(m)
        heap(m, "Paint open")
        paint(m)
        if seq in ("paint2", "two"):
            if seq == "paint2":
                ui.close(ui.window("Paint"))
                os88marty.ui_done(m)
            heap(m, "Paint closed")
            ui.path("A:/APPS/PAINT.O88")
            os88marty.ui_done(m)
            heap(m, "Paint open again")
            paint(m)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
