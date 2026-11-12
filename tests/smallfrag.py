#!/usr/bin/env python3
"""A cache never walls a pinned claim on kern_small, and Paint opens TWICE.

    make small && python3 tests/smallfrag.py [--machine M ...] [-v]

SPEC.md 50.6.13 (the eviction) and 42.6.7.1 (Paint's table hold-back), on the
128KB floor machine. The owner's field report, 2026-11-12, on a 128KB
Hercules: *boot, open the file manager, open Paint, open Paint again ->
"Out of Memory"*.

kern_small has no compactor (SPEC.md 66.0), so a pinned claim placed AROUND a
cache is a wall the shed cannot take down: shedding gives the cache's bytes
back but not its PLACE. Navigating to PAINT.O88 makes three caches low - the
Disk window's listing (MEM_P_VIEW, 2KB), the read-ahead (MEM_P_DIRW, 13.5KB)
and the icon store (MEM_P_ICO, 2KB) - and before the eviction Paint's pinned
canvas and table landed ABOVE them. Measured here before the fix, Hercules:

    0E80  VIEW 2KB | 0F00 DIRW 13.5KB | 1260 ICO 2KB | 12E0 canvas 14.5KB
    1680  table 1.5KB | 16E0 WSAVE 8KB | 18E0 free 8.5KB | 1B00 Paint 20KB

so the room a shed could make is 17.5KB below the canvas and 16.5KB above
it, and Paint #2's 20KB REGION fits neither: the loader answers LD_ENOMEM
(5), "Out of memory", with 34KB free-or-purgeable. On the CGA twin the same
walk left 17.5KB + 25.5KB and the region fit, which is why the field met it
on Hercules.

Two verdicts, each on a fresh boot of each machine:

  onehole   with Paint open, every paragraph that is free OR a cache forms
            ONE run - what the heap would be after shedding everything is a
            single hole, not two around a wall. Read off mem_tab
  paint2    a second Paint opens: two Paint windows, and the loader's status
            for PAINT.O88 is not LD_ENOMEM (Hercules; on CGA it opened
            before the fix too, so it is asserted there only as onehole)

  fdlgkept  a third boot (CGA): Note Pad, File > Open README.TXT, File >
            Open NOTE2.TXT (a fixture on a scratch copy of the disk) - and
            the second open RE-ADOPTS FDLG.DRV (mod_need.held, no
            mod_need.sized): the module cache, which SPEC.md 50.6.13 made
            safe to have on kern_small (2.8.3.4). Measured: 2 int 13h for the
            whole second open (the 5150's media check and the root's fill),
            where freeing the image at its drop made it 4 - FDLG.DRV's two
            reads gone

**How to make it go red** (WRITING-TESTS 1, done at the fix): build kern_small
with `%define MEM_EVICT` taken out of kernel/kernel.asm and both verdicts fail
on Hercules - onehole reads two runs (17.5KB and 16.5KB) and paint2 reads
ld_status 5 - and onehole fails on CGA (17.5KB and 25.5KB). Take kern_small's
`%define MOD_CACHE` out and fdlgkept fails twice: no kept record after the
first open, and the second LOADS the image (mod_need.sized, 4 int 13h).
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")
import os88build as _B                                   # noqa: E402
_B.use_build("build/smallk")

import os88geom                                          # noqa: E402
import os88marty                                         # noqa: E402
import os88sym                                           # noqa: E402
import os88ui                                            # noqa: E402
import importlib.util as _iu                             # noqa: E402
_sp = _iu.spec_from_file_location("heapmap_t", os.path.join(ROOT, "tools",
                                                             "heapmap.py"))
heapmap = _iu.module_from_spec(_sp)
_sp.loader.exec_module(heapmap)

ARM = ("KERN_SMALL",)
MACHINES = ["os8088_5150_herc_gla_128k", "os8088_5150_cga_128k"]
LD_ENOMEM = 5
VERBOSE = False


def u16(b, i=0):
    return int.from_bytes(b[i:i + 2], "little")


def heap(m, label):
    """Print the claim map; return the runs of free-or-cache paragraphs."""
    S = lambda n: m.sym(n, ARM)                              # noqa: E731
    eq = os88sym.equates(ARM)
    base = u16(m.read(S("mem_base"), 2))
    top = u16(m.read(S("mem_top"), 2))
    sz = os88geom.MC_SIZE
    tab = m.read(S("mem_tab"), eq["MEM_MAX"] * sz)
    rows = []
    for i in range(eq["MEM_MAX"]):
        r = tab[i * sz:(i + 1) * sz]
        if u16(r, 0):
            rows.append((u16(r, 0), u16(r, 2), u16(r, 4)))
    rows.sort()
    out = ["== %s: heap %04X..%04X = %.1f KB" % (label, base, top,
                                                 (top - base) / 64.0)]
    walls = []                  # pinned extents, in base order
    cur = base
    for seg, para, own in rows:
        if seg > cur:
            out.append("   %04X  %6.2fKB  -- hole --" % (cur, (seg - cur) / 64.0))
        pg = eq["MEM_PG_MIN"] <= (own >> 8) <= eq["MEM_PG_MAX"]
        out.append("   %04X  %6.2fKB  %-24s %s" % (
            seg, para / 64.0, heapmap.owner(own) + " (%04X)" % own,
            "cache" if pg else "pinned"))
        if not pg:
            walls.append((seg, seg + para))
        cur = max(cur, seg + para)
    if top > cur:
        out.append("   %04X  %6.2fKB  -- hole (top) --" % (cur, (top - cur) / 64.0))
    runs, cur = [], base
    for a, b in walls:
        if a > cur:
            runs.append(a - cur)
        cur = max(cur, b)
    if top > cur:
        runs.append(top - cur)
    out.append("   free-or-cache runs: %s KB" % ", ".join(
        "%.2f" % (r / 64.0) for r in runs))
    if VERBOSE:
        print("\n".join(out))
    return runs, out


def paints(ui):
    return sum(1 for t in ui.titles() if t == "Paint")


def leg(machine):
    bad = []
    herc = "herc" in machine
    with os88ui.boot("build/small360.img", machine=machine, limit=240) as ui:
        m = ui.m
        ui.path("A:/APPS/PAINT.O88")
        os88marty.ui_done(m)
        runs, txt = heap(m, "%s: Paint open" % machine)
        if len(runs) != 1:
            bad.append("onehole (%s): %d free-or-cache runs (%s KB) - a "
                       "pinned claim landed around a cache\n%s" % (
                           machine, len(runs),
                           ", ".join("%.2f" % (r / 64.0) for r in runs),
                           "\n".join(txt)))
        else:
            print("ok   onehole (%s): one %.2fKB run" % (machine,
                                                           runs[0] / 64.0))
        if not herc:
            return bad
        try:
            ui.path("A:/APPS/PAINT.O88")
            os88marty.ui_done(m)
        except os88ui.UIError as e:
            st = m.read(m.sym("ld_status", ARM), 1)[0]
            bad.append("paint2 (%s): the second Paint did not open, "
                       "ld_status %d%s\n  %s" % (
                           machine, st,
                           " (LD_ENOMEM)" if st == LD_ENOMEM else "",
                           str(e).splitlines()[0]))
            return bad
        n = paints(ui)
        runs, txt = heap(m, "%s: Paint #2 open" % machine)
        if n != 2:
            bad.append("paint2 (%s): %d Paint windows, want 2 (%r)\n%s" % (
                machine, n, ui.titles(), "\n".join(txt)))
        else:
            print("ok   paint2 (%s): two Paint windows" % machine)
    return bad


def fdlg_leg(machine):
    """SPEC.md 2.8.3.4: Note Pad, File > Open one file, File > Open another -
    and FDLG.DRV is still in RAM the second time: re-adopted, not read."""
    bad = []
    eq = os88sym.equates(ARM)
    kept = eq["MEM_P_MOD"] + 2 * eq["MOD_FDLG"]
    tmp = tempfile.mkdtemp(prefix="smallfrag-")
    try:
        boot = os.path.join(tmp, "boot.img")
        shutil.copy("build/small360.img", boot)
        note = os.path.join(tmp, "NOTE2.TXT")
        with open(note, "wb") as f:
            f.write(b"smallfrag: the second file Note Pad opens.\r\n")
        subprocess.check_call([sys.executable, "tools/os88fat.py", "add",
                               boot, note, "NOTE2.TXT"],
                              stdout=subprocess.DEVNULL)
        with os88ui.boot(boot, machine=machine, limit=240) as ui:
            m = ui.m
            ui.path("A:/APPS/NOTEPAD.O88")
            ui.menu_pick("File", "Open")
            ui.chooser_open("../README.TXT")    # the first opens on MEDIA
            os88marty.ui_done(m)
            owners = [own for _s, _p, own in rows(m)]
            if kept not in owners:
                bad.append("fdlgkept (%s): after the first File > Open no "
                           "record is MEM_P_MOD + 2 x MOD_FDLG (%04X) - the "
                           "image was freed at its drop" % (machine, kept))
            os88marty.until(m, lambda mm: mm.read(0x43F, 1)[0] & 3 == 0,
                            "the floppy motor to stop", guest=30.0)
            arms = ["mod_need.sized"]
            try:
                m.sym("mod_need.held", ARM)
                arms.append("mod_need.held")
            except KeyError:            # a kernel that keeps nothing: red
                pass
            with os88marty.bp_trace(m, {"type": "int", "addr": 0x13},
                                    *arms, regs=True, cap=4000) as tr:
                ui.menu_pick("File", "Open")
                ui.chooser()
                os88marty.ui_done(m)
            c13 = [h.get("regs") or {} for h in tr.hits
                   if h.get("type") == "int" or h["name"].startswith("?")]
            n13 = len(c13)
            if VERBOSE:
                for r in c13:
                    print("       int 13h AX=%04X CX=%04X DX=%04X" % (
                        r.get("ax", 0), r.get("cx", 0), r.get("dx", 0)))

            def ids(name):
                return [h["regs"]["di"] & 0xFF for h in tr.hits
                        if h["name"] == name and h.get("regs")]
            fd = eq["MOD_FDLG"]
            sized, held = ids("mod_need.sized"), ids("mod_need.held")
            print("     second File > Open: %d int 13h call(s); mod_need "
                  "held %r, sized %r" % (n13, held, sized))
            if fd in sized or fd not in held:
                bad.append("fdlgkept (%s): the second File > Open LOADED "
                           "FDLG.DRV (held %r, sized %r)" % (machine, held,
                                                              sized))
            else:
                print("ok   fdlgkept (%s): FDLG.DRV re-adopted, not read; "
                      "%d int 13h for the whole second open" % (machine, n13))
            ui.chooser_open("NOTE2.TXT")        # ...and B opens as well
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return bad


def rows(m):
    S = lambda n: m.sym(n, ARM)                              # noqa: E731
    eq = os88sym.equates(ARM)
    sz = os88geom.MC_SIZE
    tab = m.read(S("mem_tab"), eq["MEM_MAX"] * sz)
    return [(u16(tab, i * sz), u16(tab, i * sz + 2), u16(tab, i * sz + 4))
            for i in range(eq["MEM_MAX"]) if u16(tab, i * sz)]


def main(argv):
    global VERBOSE
    machines = []
    i = 0
    while i < len(argv):
        if argv[i] == "--machine":
            machines.append(argv[i + 1])
            i += 2
        elif argv[i] == "-v":
            VERBOSE = True
            i += 1
        else:
            sys.exit("usage: smallfrag.py [--machine M ...] [-v]")
    os.chdir(ROOT)
    bad = []
    for mach in machines or MACHINES:
        bad += leg(mach)
    bad += fdlg_leg(machines[0] if machines else MACHINES[1])
    for b in bad:
        print("FAIL " + b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
