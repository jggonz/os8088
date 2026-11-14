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

  iconmove  a fourth boot (Hercules): A:/APPS open, then PAINT.O88 - and
            the icon store and the window's listing SURVIVE the launch that
            used to evict them (SPEC.md 50.6.13.1): each at a new base, its
            bytes equal, ico_seg / the window's FS_VSEG (and fm_vseg, when it
            named it) re-pointed. The moves' guest cycles are printed
  dirwmove  ...and the READ-AHEAD moves too (SPEC.md 50.6.13.2): every
            mem_evmv of the MEM_P_DIRW record copies its 13.5KB to a new
            base byte for byte (checked at each move, stopped at its entry
            and its exit - the store's contents legitimately change BETWEEN
            moves, a fill in the launch), and after the launch the record
            stands and dsk_rah_seg names it
  ifwindow  ...and no move holds the interrupts off for a serial byte: every
            IF = 0 stretch of a claim or a grow that copied - from its `cli`
            to mem_bcopy's `sti`, from mem_bcopy's `cli` to the next `sti` or
            the claim's `popf` - is under 39,772 cycles, one 8N1 byte at 1200
            baud on a 4.77 MHz 8088 (an original 8250 holds ONE byte).
            Logged breakpoints on labels that cost no byte, and a stop at
            mem_bcopy's `sti` that reads the guest's own FLAGS; the copying
            claims are printed end to end beside it, which is what each would
            hold at IF = 0 without the windows
  iconrepaint  ...then Paint closed: the Disk window repaints with 0 int 13h
  dirwserve ...then `..`: the first read after the launch is ONE int 13h,
            a fill INTO the moved read-ahead that serves the mount's boot
            sector and FAT; evicted, the same step is 2 (the boot sector
            alone, then the next mount's re-claim fills it)
  ctrlkept  ...then the Control Panel opened and closed twice: the second
            re-adopts CTRL.DRV (mod_need.held, no .sized) with 0 int 13h
            (SPEC.md 2.8.3.4.1)
  shedorder a fifth boot (Hercules): A:/APPS open, so the read-ahead and the
            icon store both stand, and mem_claim_1 made to refuse in guest
            RAM (`stc / ret`) so Paint's launch runs mem_claim_x's
            SHED-AND-RETRY: the read-ahead is shed before the icon store
            (SPEC.md 50.6.13.2's ranks, TRIV below LOW). mem_claim_1 is put
            back at the first shed of either, and Paint opens

**How to make it go red** (WRITING-TESTS 1, done at the fix): build kern_small
with `%define MEM_EVICT` taken out of kernel/kernel.asm and both verdicts fail
on Hercules - onehole reads two runs (17.5KB and 16.5KB) and paint2 reads
ld_status 5 - and onehole fails on CGA (17.5KB and 25.5KB). Take kern_small's
`%define MOD_CACHE` out and fdlgkept fails twice: no kept record after the
first open, and the second LOADS the image (mod_need.sized, 4 int 13h).
Make mem_evmv answer CF = 1 at once (kernel/memory.inc) and iconmove fails
twice (both caches evicted) and iconrepaint reads 11 int 13h; put CTRL.DRV's
MOD_H_SCR back to 0 (kernel/ctrl.inc) and ctrlkept finds no kept row.
SPEC.md 50.6.13.2, each broken on purpose and seen red: `--red dirw` (the
read-ahead's mem_evw entry pointed at a 0 word in guest RAM: no word names
it, so it is EVICTED, as before) fails dirwmove and dirwserve; mem_bcopy's
`sti` taken out fails ifwindow; MEM_P_ICO and MEM_P_DIRW back at the SDK's
TRIV and HIGH on kern_small fails shedorder (the icon store shed first).
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
RED = set()                     # --red dirw: the read-ahead's mem_evw entry off
# One 8N1 byte at 1200 baud (10 bits) on a 4.77 MHz 8088: what an original
# 8250, which buffers ONE received byte, gives the CPU to read it in.
BYTE_1200 = int(10 / 1200.0 * 4772727)          # 39,772 cycles, 8.33 ms
# The IF = 0 meter (SPEC.md 50.6.13.2): labels that cost no byte. A window
# OPENS at a claim's or a grow's `cli` and at mem_bcopy's `cli`, and CLOSES at
# mem_bcopy's `sti` and at the claim's or the grow's `popf`.
IF_OPEN = ("mem_claim_1.ifoff", "mem_regrowp.ifoff", "mem_bcopy.ifoff")
IF_SHUT = ("mem_bcopy.ifon", "mem_claim_1.pf", "mem_regrowp.pf",
           "mem_regrowp.fail")
# ...all of them LOGGED (no round trip) but mem_bcopy's `sti`, which is a STOP
# so the window's end is read off the guest's own FLAGS there rather than
# assumed from the label: a `sti` that is not there leaves IF = 0
IF_LOG = tuple(n for n in IF_OPEN + IF_SHUT if n != "mem_bcopy.ifon")


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


def cache_state(m, slot):
    """{owner: (base, bytes)} for the icon store and the listing of fm_pool
    block `slot`, and the words that name them."""
    eq = os88sym.equates(ARM)
    S = lambda n: m.sym(n, ARM)                              # noqa: E731
    want = {eq["MEM_P_ICO"]: "ICO", eq["MEM_P_VIEW"] + slot: "VIEW"}
    out = {}
    for seg, para, own in rows(m):
        if own in want:
            out[want[own]] = (seg, bytes(m.read(seg * 16, para * 16)))
    words = {"ico_seg": u16(m.read(S("ico_seg"), 2)),
             "FS_VSEG": u16(m.read(S("fm_pool") + slot * eq["FS_SIZE"]
                                   + eq["FS_VSEG"], 2)),
             "fm_vseg": u16(m.read(S("fm_vseg"), 2))}
    return out, words


def int13(tr):
    return [h for h in tr.hits
            if h.get("type") == "int" or h["name"].startswith("?")]


def motor_off(m):
    os88marty.until(m, lambda mm: mm.read(0x43F, 1)[0] & 3 == 0,
                    "the floppy motor to stop", guest=30.0)


def move_leg(machine):
    """SPEC.md 50.6.13.1: a program launch that lands on the Disk window's
    icon store and listing MOVES them into a free hole - same bytes, a new
    base, the owner's words re-pointed - and closing the program repaints
    the window with no int 13h at all. Then SPEC.md 2.8.3.4.1: the Control
    Panel opened twice, the second open re-adopting CTRL.DRV with no read."""
    bad = []
    eq = os88sym.equates(ARM)
    with os88ui.boot("build/small360.img", machine=machine, limit=240) as ui:
        m = ui.m
        w = ui.path("A:/APPS")
        os88marty.ui_done(m)
        motor_off(m)
        slot = (ui._fsblk(w) - m.sym("fm_pool", ARM)) // eq["FS_SIZE"]
        before, wb = cache_state(m, slot)
        if set(before) != {"ICO", "VIEW"}:
            return ["iconmove (%s): before Paint the icon store and the "
                    "listing are not both standing (%r)" % (machine,
                                                             sorted(before))]
        if "dirw" in RED:
            red_dirw(m)
        dirw = eq["MEM_P_DIRW"]
        low = eq["LOW_SEG"] * 16
        mv = {}                 # the DIRW move in flight: record, old base, bytes

        def snap(mm, r):
            """At mem_evmv's entry and exit, while the guest is stopped: the
            read-ahead's bytes before a move and where they went."""
            rg = r.get("regs") or {}
            if r["name"] == "mem_evmv" and rg.get("bx") == dirw:
                rec = low + rg["si"]
                seg = u16(mm.read(rec, 2))
                para = u16(mm.read(rec + 2, 2))
                mv.clear()
                mv.update(rec=rec, old=seg, data=bytes(mm.read(seg * 16,
                                                               para * 16)))
                return ("dirw-in", seg)
            if r["name"] == "mem_evmv.out" and mv:
                seg = u16(mm.read(mv["rec"], 2))
                same = (seg not in (0, mv["old"]) and bytes(
                    mm.read(seg * 16, len(mv["data"]))) == mv["data"])
                out = ("dirw-out", mv["old"], seg, same, len(mv["data"]))
                mv.clear()
                return out
            return None
        with os88marty.bp_trace(m, "mem_evmv", "mem_evmv.out", "mem_bcopy",
                                *(IF_OPEN + IF_SHUT), log=IF_LOG,
                                regs=True, on_hit=snap, cap=8000) as tr:
            ui.open("PAINT.O88")
            os88marty.ui_done(m)
        runs, txt = heap(m, "%s: Paint open, after the moves" % machine)
        after, wa = cache_state(m, slot)
        cyc, t0, copied = [], None, False
        for h in tr.hits:
            if h["name"] == "mem_evmv":
                t0, copied = h["cycles"], False
            elif h["name"] == "mem_bcopy":
                copied = True
            elif h["name"] == "mem_evmv.out" and t0 is not None:
                if copied:
                    cyc.append(h["cycles"] - t0)
                t0 = None
        bad += dirw_verdict(m, tr, machine)
        bad += if_verdict(tr, machine)
        for k, word in (("ICO", "ico_seg"), ("VIEW", "FS_VSEG")):
            if k not in after:
                bad.append("iconmove (%s): the launch EVICTED the %s cache "
                           "(%04X) - it was not moved\n%s" % (
                               machine, k, before[k][0], "\n".join(txt)))
                continue
            b0, d0 = before[k]
            b1, d1 = after[k]
            ok = (b1 != b0 and d1 == d0 and wa[word] == b1)
            if k == "VIEW" and wb["fm_vseg"] == b0:
                ok = ok and wa["fm_vseg"] == b1
            if not ok:
                bad.append("iconmove (%s): %s %04X -> %04X, %d bytes %s, "
                           "%s %04X, fm_vseg %04X" % (
                               machine, k, b0, b1, len(d0),
                               "equal" if d1 == d0 else "CHANGED", word,
                               wa[word], wa["fm_vseg"]))
            else:
                print("ok   iconmove (%s): %s moved %04X -> %04X, its %d "
                      "bytes equal, %s re-pointed" % (machine, k, b0, b1,
                                                      len(d0), word))
        print("     mem_evmv moves: %s cycles each (%s ms at 4.77 MHz)" % (
            cyc, ["%.1f" % (c / 4772.727) for c in cyc]))
        motor_off(m)
        with os88marty.bp_trace(m, {"type": "int", "addr": 0x13},
                                regs=True, cap=4000) as tr:
            ui.close(ui.window("Paint"))
            os88marty.ui_done(m)
            motor_off(m)
        n13 = len(int13(tr))
        if n13:
            bad.append("iconrepaint (%s): closing Paint made %d int 13h "
                       "call(s) - the Disk window re-fetched what the move "
                       "kept" % (machine, n13))
        else:
            print("ok   iconrepaint (%s): Paint closed, the Disk window "
                  "repainted with 0 int 13h" % machine)
        bad += dirw_serve(ui, machine)
        bad += ctrl_twice(ui, machine)
    return bad


def red_dirw(m):
    """--red dirw: the read-ahead's mem_evw entry pointed at the 0 word in
    front of the table, so no word names the store and mem_evmv refuses -
    what the kernel did before SPEC.md 50.6.13.2 (the store EVICTED)."""
    S = lambda n: m.sym(n, ARM)                              # noqa: E731
    ev = S("mem_evw")
    kseg = os88sym.KERNEL_SEG * 16
    rah = S("dsk_rah_seg") - kseg
    for i in range(0, 32, 2):
        if u16(m.read(ev + i, 2)) == rah:
            m.write(ev + i, (ev - 2 - kseg).to_bytes(2, "little"))
            return
    raise SystemExit("smallfrag --red dirw: dsk_rah_seg is not in mem_evw")


def dirw_verdict(m, tr, machine):
    """dirwmove: each DIRW move copied its bytes, and the store stands after."""
    eq = os88sym.equates(ARM)
    moves = [h["hit"] for h in tr.hits
             if isinstance(h.get("hit"), tuple) and h["hit"][0] == "dirw-out"]
    entered = [h for h in tr.hits
               if isinstance(h.get("hit"), tuple) and h["hit"][0] == "dirw-in"]
    rec = [(s_, p) for s_, p, o in rows(m) if o == eq["MEM_P_DIRW"]]
    rah = u16(m.read(m.sym("dsk_rah_seg", ARM), 2))
    bad = []
    for _t, old, new, same, n in moves:
        print("     dirwmove: %04X -> %04X, %d bytes %s" % (
            old, new, n, "equal" if same else "NOT EQUAL"))
    good = [mv for mv in moves if mv[3]]
    if not entered:
        bad.append("dirwmove (%s): the launch never reached mem_evmv with "
                   "the read-ahead - nothing landed on it, so this verdict "
                   "says nothing" % machine)
    elif len(good) < len(entered):
        bad.append("dirwmove (%s): %d of %d attempts moved the read-ahead "
                   "with its bytes intact (%r) - it was EVICTED or copied "
                   "wrong" % (machine, len(good), len(entered), moves))
    elif not rec or rec[0][0] != rah:
        bad.append("dirwmove (%s): after the launch the read-ahead's record "
                   "is %r and dsk_rah_seg %04X" % (machine, rec, rah))
    else:
        print("ok   dirwmove (%s): the read-ahead moved %d time(s), each "
              "copy byte-equal; it stands at %04X and dsk_rah_seg names it"
              % (machine, len(moves), rah))
    return bad


def if_verdict(tr, machine):
    """ifwindow: the longest IF = 0 stretch of any claim or grow that copied."""
    ev = sorted((h for h in tr.hits if h["name"] in IF_OPEN + IF_SHUT),
                key=lambda h: h["cycles"])
    wins, opened, op = [], None, None
    claim, spans = None, []
    ends = ("mem_claim_1.pf", "mem_regrowp.pf", "mem_regrowp.fail")
    for h in ev:
        n, c = h["name"], h["cycles"]
        if n in ("mem_claim_1.ifoff", "mem_regrowp.ifoff"):
            claim = {"t0": c, "copied": False, "wins": []}
        if n == "mem_bcopy.ifon":
            if claim is not None:
                claim["copied"] = True
            if not (h.get("regs") or {}).get("flags", 0) & 0x200:
                continue            # IF still 0 past the `sti`: no window
        if n in IF_OPEN:
            if opened is None:      # (already shut stays shut from its `cli`)
                opened, op = c, n
        elif opened is not None:
            if claim is not None:
                claim["wins"].append((c - opened, op, n))
            opened = None
        if n in ends and claim is not None:
            if claim["copied"]:
                wins += claim["wins"]
                spans.append(c - claim["t0"])
            claim = None
    if not wins:
        return ["ifwindow (%s): no claim or grow that copied was seen - "
                "the meter's labels never fired" % machine]
    worst = max(wins)
    print("     ifwindow: %d IF = 0 stretches in %d copying claim(s); the "
          "longest %d cycles = %.2f ms (%s -> %s); the claims end to end, "
          "what each would hold at IF = 0 without the windows: %s cycles" % (
              len(wins), len(spans), worst[0], worst[0] / 4772.727,
              worst[1], worst[2], spans))
    if worst[0] >= BYTE_1200:
        return ["ifwindow (%s): an IF = 0 stretch of %d cycles (%.2f ms, "
                "%s -> %s) - over one 1200-baud byte, %d cycles: a serial "
                "mouse byte is lost" % (machine, worst[0], worst[0] / 4772.727,
                                        worst[1], worst[2], BYTE_1200)]
    print("ok   ifwindow (%s): no IF = 0 stretch of a copying claim reaches a "
          "1200-baud byte (%d of %d cycles)" % (machine, worst[0], BYTE_1200))
    return []


def dirw_serve(ui, machine):
    """dirwserve: the next read after the launch fills INTO the moved store
    and is served out of it - one int 13h for the parent's mount."""
    m = ui.m
    rah = u16(m.read(m.sym("dsk_rah_seg", ARM), 2))
    with os88marty.bp_trace(m, {"type": "int", "addr": 0x13},
                            regs=True, cap=4000) as tr:
        ui.open("..")
        os88marty.ui_done(m)
        motor_off(m)
    c = int13(tr)
    calls = ["AX=%04X CX=%04X" % (h["regs"]["ax"], h["regs"]["cx"])
             for h in c if h.get("regs")]
    if not rah or len(c) > 1:
        return ["dirwserve (%s): with dsk_rah_seg %04X the parent's mount made "
                "%d int 13h (%s) - one fill into a standing read-ahead serves "
                "the boot sector and the FAT; two is the read-ahead gone and "
                "re-claimed mid-mount" % (machine, rah, len(c), calls)]
    print("ok   dirwserve (%s): the read-ahead at %04X served the parent's "
          "mount - %d int 13h (%s)" % (machine, rah, len(c), calls))
    return []


def shed_leg(machine):
    """shedorder: a REFUSED claim sheds the read-ahead before the icon store
    (SPEC.md 50.6.13.2) - mem_claim_1 made to refuse in guest RAM, so Paint's
    launch runs mem_claim_x's shed-and-retry, and put back at the first shed
    of either."""
    eq = os88sym.equates(ARM)
    dirw, ico = eq["MEM_P_DIRW"], eq["MEM_P_ICO"]
    low = eq["LOW_SEG"] * 16
    with os88ui.boot("build/small360.img", machine=machine, limit=240) as ui:
        m = ui.m
        ui.path("A:/APPS")
        os88marty.ui_done(m)
        motor_off(m)
        owners = [o for _s, _p, o in rows(m)]
        if dirw not in owners or ico not in owners:
            return ["shedorder (%s): with A:/APPS open the read-ahead and the "
                    "icon store are not both standing (%s)" % (
                        machine, ["%04X" % o for o in owners])]
        c1 = m.sym("mem_claim_1", ARM)
        orig = bytes(m.read(c1, 2))
        seq = []

        def shed(mm, r):
            rg = r.get("regs") or {}
            di = rg.get("di", 0)
            if not di:
                return None
            own = u16(mm.read(low + di + eq["MC_OWN"], 2))
            seq.append((rg.get("bx", 0), own))
            if own in (dirw, ico):
                mm.write(c1, orig)          # the claim succeeds from here
                mm.breakpoints([])
            return own
        m.write(c1, bytes((0xF9, 0xC3)))            # stc / ret: every claim refused
        try:
            with os88marty.bp_trace(m, "mem_shed_one.done", regs=True,
                                    on_hit=shed, cap=200):
                ui.open("PAINT.O88")
                os88marty.ui_done(m)
        finally:
            m.write(c1, orig)
    shown = ", ".join("%04X by %04X" % (o, b) for b, o in seq)
    first = [o for _b, o in seq if o in (dirw, ico)]
    if not first or first[0] != dirw:
        return ["shedorder (%s): the refused claim shed %s - the icon store "
                "(%04X) went before the read-ahead (%04X)" % (
                    machine, shown or "nothing", ico, dirw)]
    print("ok   shedorder (%s): the refusal shed %s - the read-ahead before "
          "the icon store" % (machine, shown))
    return []


def ctrl_twice(ui, machine):
    m = ui.m
    eq = os88sym.equates(ARM)
    ui.menu_pick("Apple", "Control Panel")
    ui.close(ui.wait_window("Control Panel"))
    os88marty.ui_done(m)
    motor_off(m)
    kept = eq["MEM_P_MOD"] + 2 * eq["MOD_CTRL"]
    if kept not in [own for _s, _p, own in rows(m)]:
        return ["ctrlkept (%s): after the first Control Panel no record is "
                "MEM_P_MOD + 2 x MOD_CTRL (%04X) - CTRL.DRV was freed at "
                "its drop" % (machine, kept)]
    arms = ["mod_need.sized", "mod_need.held"]
    with os88marty.bp_trace(m, {"type": "int", "addr": 0x13}, *arms,
                            regs=True, cap=4000) as tr:
        ui.menu_pick("Apple", "Control Panel")
        w = ui.wait_window("Control Panel")
        os88marty.ui_done(m)
    n13 = len(int13(tr))

    def ids(name):
        return [h["regs"]["di"] & 0xFF for h in tr.hits
                if h["name"] == name and h.get("regs")]
    held, sized = ids("mod_need.held"), ids("mod_need.sized")
    ui.close(w)
    cp = eq["MOD_CTRL"]
    if n13 or cp in sized or cp not in held:
        return ["ctrlkept (%s): the second Control Panel made %d int 13h, "
                "mod_need held %r sized %r" % (machine, n13, held, sized)]
    print("ok   ctrlkept (%s): CTRL.DRV re-adopted, 0 int 13h for the "
          "second Control Panel" % machine)
    return []


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
        elif argv[i] == "--red":
            RED.add(argv[i + 1])
            i += 2
        else:
            sys.exit("usage: smallfrag.py [--machine M ...] [-v] [--red dirw]")
    os.chdir(ROOT)
    bad = []
    for mach in machines or MACHINES:
        bad += leg(mach)
    bad += fdlg_leg(machines[0] if machines else MACHINES[1])
    bad += move_leg(machines[0] if machines else MACHINES[0])
    bad += shed_leg(machines[0] if machines else MACHINES[0])
    for b in bad:
        print("FAIL " + b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
