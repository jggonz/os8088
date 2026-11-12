"""SPEC.md 2.8.3 for EVERY module: a second use comes from RAM, the dropped
image rests KEPT and byte for byte the file's, and a compaction moves it
rather than losing it. tests/modcache.py's second half (its main() runs
this after the Sticky Apps legs); the module is here so the legs can be
read, and run, apart:

    python3 tests/modkeep.py [--legs host,vga,hdd,dual]

HOST (no emulator): every opted-in image on the shipped system disk ends in
MOD_H_SCR bytes that are ZERO in its file - so the drop's re-zeroing makes
the tail the file's own - and every `[cs:x]`/`[CSEG x]` store a module's
source makes lands in that tail or in the image's WRITTEN-BEFORE-READ list
(WBR below, SPEC.md 2.8.3's table). A store added outside both fails here
before any machine boots.

VGA (os8088_xt_vga, the 360KB system disk, a scratch B:): each module used
twice - the Control Panel opened and closed (CTRL.DRV); its Theme page's
Windows row then Bright (WIN95.DRV); its Dock page's Left row then Bottom
(DOCK.DRV); Disk > Format Disk... and Esc (FORMAT.DRV); File > Compress on
a text file of B: (DISKOPS.DRV); the Sticky Apps menu's Desk Calendar added
and removed (DESK.DRV). For each use: the int 13h calls, named by file, and
whether mod_need LOADED (.sized) or re-adopted (.held) that module. Then:

  1  the first use loads it; after it the row is KEPT - held, its slots on
     mod_gone, its claim MEM_P_MOD + 2 x the id - and the kept bytes are the
     FILE's: the tail zero, everything else equal but the WBR cells;
  2  the second use re-adopts it (.held, no .sized) and reads NOT ONE sector
     of its file; for CTRL.DRV, WIN95.DRV and DOCK.DRV no int 13h at all
     (the owner's report: "opening it twice causes disk reads");
  3  a compaction is posted at rank 0FFh with all of them kept: every row is
     still KEPT afterwards, wherever the pass put it (a move is reported);
  4  the third use re-adopts each again, at its new home, with no read of
     its file.

HDD (os8088_5150_herc_hdd_gla, the owner's machine's twin): Hibernate...
opened and cancelled twice (HIBER.DRV), the same three questions.

DUAL (os8088_5150_both_gla, CGA + Hercules): the Display page's Extend
Right, then Single, twice (EXTD.DRV).

SERIAL.DRV has no leg: no package on the shipped disks opens a lease, and
MartyPC's COM1 is the mouse. Its image is held to HOST's two checks; its
lease lives wholly in its tail (srl_base = 0 is "no lease").

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1), each measured red on
2026-11-12 with MODKEEP_ONLY=MOD_CTRL (CTRL.DRV and DESK.DRV alone):

  * CTRL.DRV's header back to MOD_H_SCR 0 (ctrl.inc) - the owner's report
    reproduced: 11 FAILs, HOST's two, the row 'none' after every use, the
    second open LOADING it again (4 int 13h, 2 runs of its sectors), and
    the settings core never kept;
  * mod_live's slot test out (mod.inc) - one FAIL, the registration check:
    MOD_SETS 'none' with the panel kept, the gesture having far-called the
    kept panel's disarmed slot (and the calendar's first use made 23 int 13h
    calls where it makes 46: no SYSTEM.CFG was written);
  * EXT_SCRATCH emitted where the cells used to be, mid-image (extmod.inc) -
    HOST: EXTD.DRV's 1004-byte "tail" is not zeros in its file.
"""
import argparse
import os
import re
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty   # noqa: E402
import os88ui      # noqa: E402
import os88fat     # noqa: E402
import os88sym     # noqa: E402
import os88drv     # noqa: E402
import os88mouse   # noqa: E402
import os88geom as geom  # noqa: E402
import heapmap     # noqa: E402
import modstate    # noqa: E402

BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
T = 120
S = os88sym.linear

# id -> (file, its module SECTIONS in the source, the WRITTEN-BEFORE-READ
# cells outside its tail). SPEC.md 2.8.3's table is the argument for each
# WBR cell; this is the list the HOST leg holds the source to
MODS = {
    "MOD_CTRL": ("CTRL.DRV", (".modc", ".modu", ".modcb"),
                 ("cp_s_dsdrv", "cp_vvesa_buf",
                  # shutdown's TERMINAL path, which never returns: each set
                  # before it is read, and nothing runs the image after
                  "sd_last", "sd_l_cx", "sd_l_spn", "sd_l_spx", "sd_l_spp")),
    "MOD_FMT": ("FORMAT.DRV", (".modf",), ()),
    "MOD_DISKOPS": ("DISKOPS.DRV", (".modl",),
                    ("clo_mseg", "clo_mvol", "cmz_otok", "cmz_tsh", "cmz_tfp",
                     "cmz_utot", "cmz_done", "cmz_z", "cmz_w", "cmz_u",
                     "cmz_p", "cmz_b", "cmz_o", "cmz_x", "cmz_k", "cmz_buf")),
    "MOD_HIBER": ("HIBER.DRV", (".modh", ".modhb"),
                  ("hbm_ptrfix", "hbm_s_a2", "hbm_psv")),
    "MOD_DOCK": ("DOCK.DRV", (".modk",), ()),
    "MOD_EXT": ("EXTD.DRV", (".modx",), ()),
    "MOD_WIN": ("WIN95.DRV", (".modw", ".modwb"), ()),
    "MOD_DESK": ("DESK.DRV", (".modj",), ()),
    "MOD_STIKM": ("STIKMENU.DRV", (".modn",), None),   # its own row's
    "MOD_SER": ("SERIAL.DRV", (".mods",), ()),
}
fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def image(fname):
    """The image as the file unpacks to: what a load puts in the claim."""
    return os88drv.image_unwrap(open(os.path.join(BDIR, fname.lower()),
                                     "rb").read())


def hdr(img):
    return dict(img=struct.unpack_from("<H", img, 8)[0],
                scr=struct.unpack_from("<H", img, 12)[0])


def spans(names):
    """Each label's [start, end): to the next non-local label of its section."""
    sy, sec = os88sym.syms(), os88sym.sections()
    out = []
    for n in names:
        s = sec[n]
        nxt = sorted(o for k, o in sy.items()
                     if sec.get(k) == s and "." not in k and o > sy[n])
        out.append((sy[n], nxt[0] if nxt else sy[n] + 1, n))
    return out


# --- HOST ---------------------------------------------------------------------
WRITE = re.compile(
    r"^\s*(?:[A-Za-z_.$@?][\w.$@?#~]*:\s*)?(?:CSDOF?\s+)?"
    r"(?:mov|add|sub|adc|sbb|inc|dec|or|and|xor|not|neg|shl|shr|sal|sar|rol|"
    r"ror|rcl|rcr|pop|xchg)\s+(?:byte\s+|word\s+)?\[\s*(?:byte\s+)?"
    r"(?:CSEG\s*|cs:\s*)([A-Za-z_][\w.]*)", re.I)
CSDO_SP = re.compile(r"^\s*CSDO_SP\s+([A-Za-z_]\w*)")
SECT = re.compile(r"^\s*section\s+(\.\w+)", re.I)


def stores():
    """{section: {label}} - every DIRECT store through CS in kernel/'s module
    sections. A store through a register ([cs:di]) names no label; the VGA
    legs' byte compare is what sees those."""
    out = {}
    kd = os.path.join(ROOT, "kernel")
    for fn in sorted(os.listdir(kd)):
        if not fn.endswith((".inc", ".asm")):
            continue
        sec = ".text"
        for ln in open(os.path.join(kd, fn), errors="replace"):
            code = ln.split(";")[0]
            m = SECT.match(code)
            if m:
                sec = m.group(1)
                continue
            m = WRITE.match(code) or CSDO_SP.match(code)
            if m and m.group(1).lower() not in ("bx", "di", "si", "bp"):
                out.setdefault(sec, set()).add(m.group(1))
    return out


def host():
    print("HOST: every kept image's tail is zeros in its file, and every "
          "direct store lands in it or in a WBR cell")
    eq = os88sym.equates()
    sy, sec = os88sym.syms(), os88sym.sections()
    st = stores()
    for mid, (fname, sects, wbr) in MODS.items():
        if mid not in eq:
            continue
        img = image(fname)
        h = hdr(img)
        check(h["scr"] > 0 and h["img"] == len(img),
              "%s opts in: MOD_H_SCR %d, MOD_H_IMG %d of %d bytes"
              % (fname, h["scr"], h["img"], len(img)))
        t0 = h["img"] - h["scr"]
        check(not any(img[t0:h["img"]]),
              "%s: its %d-byte tail is zeros in the file" % (fname, h["scr"]))
        if wbr is None:
            continue
        ok = [(a, b) for a, b, _ in spans(wbr)]
        bad = []
        for s in sects:
            for lab in sorted(st.get(s, ())):
                if lab not in sy:
                    continue            # an equ (MOD_H_SCR) or a macro arg
                o = sy[lab]
                if sec.get(lab) not in sects:
                    continue            # the kernel's own, through CS
                if o >= t0 or any(a <= o < b for a, b in ok):
                    continue
                bad.append("%s@%#x" % (lab, o))
        check(not bad, "%s: no store outside its tail and WBR cells%s"
              % (fname, (": " + ", ".join(bad)) if bad else ""))


# --- the machine legs -----------------------------------------------------------
class Lba:
    """Every sector of a FAT12 image, named by the file that holds it."""

    def __init__(self, img):
        f = os88fat.Fat12(img)
        self.f, self.m = f, {}
        self._walk(None, "")

    def _ents(self, clus):
        f = self.f
        if clus is None:
            for _, _, e in f.entries():
                yield e
            return
        for c in f.chain(clus):
            o = f.cluster_off(c)
            for k in range(f.spc * f.bps // 32):
                e = bytes(f.img[o + 32 * k:o + 32 * k + 32])
                if e[0] == 0:
                    return
                if e[0] != 0xE5:
                    yield e

    def _walk(self, clus, pre):
        f = self.f
        for e in self._ents(clus):
            n = f.pretty(e[:11])
            first = struct.unpack_from("<H", e, 26)[0]
            if e[11] & 8 or n in (".", "..") or not first:
                continue
            for c in f.chain(first):
                for x in range(f.cluster_lba(c), f.cluster_lba(c) + f.spc):
                    self.m[x] = pre + n
            if e[11] & 0x10:
                self._walk(first, pre + n + "/")

    def names(self, lba, n):
        f = self.f
        return sorted({self.m.get(x, "boot/FAT" if x < f.root_lba else
                                  "root" if x < f.data_lba else "free")
                       for x in range(lba, lba + n)})


def calls(tr, lba):
    """Every int 13h the trace caught: (function, drive, the files)."""
    out = []
    for h in tr.hits:
        if h.get("type") != "int" and not h["name"].startswith("?"):
            continue
        r = h.get("regs") or {}
        ax, cx, dx = r.get("ax", 0), r.get("cx", 0), r.get("dx", 0)
        fn, drv = ax >> 8, dx & 0xFF
        names = []
        if fn in (2, 3) and lba and drv == 0:
            f = lba.f
            cyl = (cx >> 8) | ((cx & 0xC0) << 2)
            x = (cyl * f.heads + (dx >> 8)) * f.spt + (cx & 0x3F) - 1
            names = lba.names(x, ax & 0xFF)
        out.append((fn, drv, names))
    return out


def use(m, lba, mid, act):
    """Run `act` once with int 13h and mod_need's two arms traced."""
    os88marty.until(m, lambda mm: mm.read(0x43F, 1)[0] & 3 == 0,
                    "the floppy motors to stop", guest=30.0)
    with os88marty.bp_trace(m, {"type": "int", "addr": 0x13},
                            "mod_need.sized", "mod_need.held", regs=True,
                            cap=6000) as tr:
        act()
    eq = os88sym.equates()
    i = eq[mid]

    def ids(name):
        return [h["regs"]["di"] & 0xFF for h in tr.hits
                if h["name"] == name and h.get("regs")]
    c = calls(tr, lba)
    fname = MODS[mid][0]
    return dict(n13=len(c), calls=c,
                mine=sum(1 for _, _, n in c if fname in n),
                sized=ids("mod_need.sized").count(i),
                held=ids("mod_need.held").count(i))


def owner(m, ui, seg):
    import stikmenu as SM
    return [c.own for c in heapmap.Map(m, SM._Syms(ui)).claims
            if seg and c.seg == seg]


def rests(m, ui, mid, tag):
    """1: the row KEPT, its claim the kept tag, its bytes the file's."""
    st = modstate.state(m, mid)
    seg = modstate.seg(m, mid)
    fname, _, wbr = MODS[mid]
    ok = check(st == "kept" and owner(m, ui, seg) == [modstate.kept_tag(mid)],
               "%s %s: KEPT at %#06x (%s, owner %r)"
               % (fname, tag, seg, st,
                  [hex(o) for o in owner(m, ui, seg)]))
    if not ok:
        return seg
    ref = image(fname)
    h = hdr(ref)
    got = bytes(m.read(seg << 4, h["img"]))
    t0 = h["img"] - h["scr"]
    skip = [(a, b) for a, b, _ in spans(wbr or ())]
    # mod_cfix's words (SPEC.md 2.8.3): on a machine whose `.cold` slid at
    # boot (CGA, Hercules) the load re-pointed every far call's COLD_SEG at
    # [api_coldseg], once, and a re-adopt runs neither fix again - so those
    # words are the file's AS THE LOAD FIXED THEM
    cold = os88sym.equates()["COLD_SEG"]
    now = struct.unpack("<H", bytes(m.read(S("api_coldseg"), 2)))[0]

    def fixed(o):
        return any(0 <= w <= t0 - 2
                   and struct.unpack_from("<H", ref, w)[0] == cold
                   and struct.unpack_from("<H", got, w)[0] == now
                   for w in (o, o - 1))
    diff = [o for o in range(t0) if got[o] != ref[o]
            and not any(a <= o < b for a, b in skip) and not fixed(o)]
    check(not any(got[t0:]) and not diff,
          "%s %s: the kept bytes are the file's (tail %s, %d other byte(s) "
          "differ%s)" % (fname, tag, "zero" if not any(got[t0:]) else
                         "NOT zero", len(diff),
                         (" at " + ", ".join("%#x" % o for o in diff[:8]))
                         if diff else ""))
    return seg


def report(fname, n, r):
    print("    %s use %d: %d int 13h, %d of them %s's; loaded %d, "
          "re-adopted %d" % (fname, n, r["n13"], r["mine"], fname,
                             r["sized"], r["held"]))
    for fn, drv, names in r["calls"]:
        print("        int 13h AH=%02X drive %02X %s" % (fn, drv,
                                                         " ".join(names)))


def twice(m, ui, lba, mid, act, quiet=()):
    """1 and 2 for one module; `quiet` names the uses that owe NO int 13h."""
    fname = MODS[mid][0]
    print("%s:" % fname)
    pre = modstate.state(m, mid)
    a = use(m, lba, mid, act)
    report(fname, 1, a)
    if pre == "kept":
        # already used, and kept, before this leg: FORMAT.DRV is, by a
        # mount's ASSOC.DAT flush (its ascw half, SPEC.md 54.7.6.4)
        check(a["sized"] == 0 and a["held"] >= 1,
              "%s: KEPT before its first use here (an earlier caller's), "
              "so even this one re-adopted it (loaded %d)"
              % (fname, a["sized"]))
    else:
        check(a["sized"] >= 1, "%s: the first use LOADED it (%d int 13h "
              "run(s) of its sectors - 0 when the track cache had them)"
              % (fname, a["mine"]))
    rests(m, ui, mid, "after the first use")
    b = use(m, lba, mid, act)
    report(fname, 2, b)
    check(b["sized"] == 0 and b["held"] >= 1 and b["mine"] == 0,
          "%s: the second use re-adopted it - no load, none of its sectors "
          "(loaded %d, re-adopted %d, %d sector run(s) of it)"
          % (fname, b["sized"], b["held"], b["mine"]))
    if 2 in quiet:
        check(b["n13"] == 0, "%s: the second use made NO int 13h call (%d)"
              % (fname, b["n13"]))
    rests(m, ui, mid, "after the second use")
    return a, b


def third(m, ui, lba, mid, act, quiet):
    fname = MODS[mid][0]
    c = use(m, lba, mid, act)
    report(fname, 3, c)
    check(c["sized"] == 0 and c["held"] >= 1 and c["mine"] == 0,
          "%s: re-adopted where the compaction left it (loaded %d, "
          "re-adopted %d)" % (fname, c["sized"], c["held"]))
    if quiet:
        check(c["n13"] == 0, "%s: ...with NO int 13h (%d)"
              % (fname, c["n13"]))
    rests(m, ui, mid, "after the third use")
    return c


def bdisk(tmp, texts=()):
    """B:: a copy of the utility disk (262 clusters free), the Calculator
    on it - the HOLE MAKER - and any text files the compressor wants."""
    path = os.path.join(tmp, "b.img")
    shutil.copy(os.path.join(BDIR, "util360.img"), path)
    f = os88fat.Fat12(path)
    f.add(os.path.join(BDIR, "calc.o88"), "CALC.O88")
    text = b"".join(b"line %04d of a plain file the compressor packs\r\n"
                    % i for i in range(80))
    for n in texts:
        src = os.path.join(tmp, n)
        open(src, "wb").write(text)
        f.add(src, n)
    f.save()
    return path


def hole_open(ui):
    """The Calculator, opened FIRST: its region is the top-down claim the
    modules' images land under, so closing it leaves the hole a compaction
    moves them up into (modcache's leg C used the Task Manager, whose
    counters keep the screen from ever settling under these legs' verbs)."""
    w = ui.path("B:/CALC.O88")
    seg = struct.unpack("<H", bytes(ui.m.read(
        S("wm_wins") + w.i * geom.WIN_SIZE + geom.W_SEG, 2)))[0]
    close_disks(ui)
    return w, seg


def close_disks(ui):
    """Every Disk window, so the next verb opens its own."""
    for d in list(ui.windows()):
        if d.visible and (d.title == "Disk" or d.title[1:2] == ":"):
            ui.close(d)
    ui.settle()


def compact(m, ui, mids, tm=None):
    """3: post a compaction at 0FFh; every kept row must still be KEPT."""
    hole = 0
    if tm is not None:
        ui.close(tm[0])
        ui.settle()
        hole = tm[1]
    before = {mid: modstate.seg(m, mid) for mid in mids}
    dw = ui.open_drive("A")
    with os88marty.bp_trace(m, "mem_compact", cap=50) as tr:
        geom.post_compact(m, dw, 0xFF, ui._S)
        ui._wait(lambda: ui._word("mem_cpq") == 0,
                 "the posted compaction to run", T)
        ui.settle()
    check(tr.count("mem_compact") > 0, "a compaction ran (%d)"
          % tr.count("mem_compact"))
    for mid in mids:
        s = modstate.seg(m, mid)
        st = modstate.state(m, mid)
        # an image BELOW the Calculator's region must move up into the hole
        # it left; one above it (loaded before the Calculator - FORMAT.DRV
        # is, by the mount's ASSOC.DAT flush) has nowhere to go
        below = before[mid] < hole
        check(st == "kept" and owner(m, ui, s) == [modstate.kept_tag(mid)]
              and (s != before[mid] or not below),
              "%s: still KEPT after the compaction%s: %#06x -> %#06x"
              % (MODS[mid][0], ", MOVED into the hole the Calculator left"
                 if below else " (above the hole: nowhere to go)",
                 before[mid], s))
    ui.close(dw)


# --- the uses ----------------------------------------------------------------
def cp_open(ui):
    ui.menu_pick("Apple", "Control Panel")
    return ui.wait_window("Control Panel", limit=T)


def act_ctrl(ui):
    def go():
        w = cp_open(ui)
        ui.settle()
        ui.close(w)
        ui.settle()
    return go


def act_theme(m, ui):
    import dispcp
    import thm95

    def go():
        pick = (lambda r: thm95.pick(m, ui.mo, r))
        pick(3)                                     # the Windows row
        ui._wait(lambda: m.read(S("thm_kind"), 1)[0] == thm95.THM_WIN,
                 "the Windows row to take", T)
        pick(0)                                     # Bright: the drop
        ui._wait(lambda: m.read(S("thm_kind"), 1)[0] == 0,
                 "the Bright row to take", T)
        ui.settle()
    return go


def act_dock(m, ui):
    import dockpos

    def go():
        dockpos.click_row(m, ui.mo, dockpos.CPK_R0Y + dockpos.P_LEFT
                          * dockpos.CPK_ROWH)
        ui._wait(lambda: m.read(S("dock_cfg"), 1)[0] & 3 == dockpos.P_LEFT,
                 "the Left row to take", T)
        dockpos.click_row(m, ui.mo, dockpos.CPK_R0Y + dockpos.P_BOTTOM
                          * dockpos.CPK_ROWH)
        ui._wait(lambda: m.read(S("dock_cfg"), 1)[0] & 3 == dockpos.P_BOTTOM,
                 "the Bottom row to take", T)
        ui.settle()
    return go


def fm_edit(m):
    eq = os88sym.equates()
    pool = S("fm_pool")
    for slot in range(eq["FM_NSLOT"]):
        b = m.read(pool + slot * eq["FS_SIZE"], eq["FS_SIZE"])
        if b[eq["FS_EDIT"]]:
            return b[eq["FS_EDIT"]]
    return 0


def act_format(m, ui):
    def go():
        ui.open_drive("B")
        ui.menu_pick("Disk", "Format Disk...")
        ui._wait(lambda: fm_edit(m) == 5, "the format confirmation", T)
        m.key("Escape")
        ui._wait(lambda: fm_edit(m) == 0, "the confirmation to go", T)
        ui.settle()
    return go


def act_compress(m, ui, names):
    import dispcp
    import lzcomp
    it = iter(names)

    def go():
        w = ui.open_drive("B")
        lzcomp.compress(m, ui.mo, w.x, w.y, next(it), fails, quiet=60)
        ui.settle()
    return go


def stik_pick(m, ui, menu, fname):
    """modcache.pick_app's gesture with NO trace of its own: a bp_trace
    REPLACES the whole breakpoint set, so one inside `use` would blind it."""
    import stikmenu as SM
    slabs = menu.drop()
    ents = SM.entries(menu)
    k = [i for i, e in enumerate(ents) if e["file"] == fname]
    apps = [i for i, x in enumerate(slabs) if x["kind"] == SM.SK_APP]
    if not k or not apps:
        menu.release_off()
        menu.gone()
        raise os88ui.UIError("%s is not on the Sticky Apps menu: %r"
                             % (fname, [e["file"] for e in ents]))
    menu.release_on(slabs, apps[k[0] - menu.b("stm_top")])
    menu.gone()
    ui.settle()


def act_desk(m, ui):
    import stikmenu as SM
    import modcache

    def go():
        menu = SM.Menu(m, ui)
        modcache.desktop(ui)
        stik_pick(m, ui, menu, b"DESKCAL.O88")          # Add: it launches
        ui.wait_window(SM.TITLE, limit=T)
        ui.settle()
        modcache.desktop(ui)
        stik_pick(m, ui, menu, b"DESKCAL.O88")          # the running row
        ui._wait(lambda: not SM.is_open(ui, SM.TITLE),
                 "the Desk Calendar to close", T)
        ui.settle()
    return go


def want(mid):
    """MODKEEP_ONLY=MOD_CTRL,... narrows the VGA leg (CTRL.DRV and DESK.DRV
    always run: the gesture check needs both) - for a red run, not a row"""
    only = os.environ.get("MODKEEP_ONLY")
    return not only or mid in only.split(",")


def vga():
    tmp = tempfile.mkdtemp(prefix="modkeep-")
    img = os.path.join(tmp, "sys.img")
    shutil.copy(os88marty.system_disk(os.path.join(BDIR, "os8088-360.img")),
                img)
    names = ["PLAIN%d.TXT" % i for i in (1, 2, 3)]
    bimg = bdisk(tmp, names)
    lba = Lba(img)
    try:
        with os88marty.launch(img, apps=bimg, machine=os88marty.machine(
                "os8088_xt_vga", None), boot=True) as m:
            ui = os88ui.UI(m)
            ui.up(limit=180.0)
            os88marty.no_saver(m)
            import dispcp
            import modcache
            mods, acts = [], {}
            tm = hole_open(ui)

            acts["MOD_CTRL"] = (act_ctrl(ui), (2, 3))
            twice(m, ui, lba, "MOD_CTRL", acts["MOD_CTRL"][0], (2,))
            mods.append("MOD_CTRL")

            if want("MOD_WIN"):
                dispcp.open_panel(m, ui.mo, S, lambda mm, **k:
                                  os88marty.ui_done(mm), page=dispcp.CP_ITHM)
                acts["MOD_WIN"] = (act_theme(m, ui), (2, 3))
                twice(m, ui, lba, "MOD_WIN", acts["MOD_WIN"][0], (2,))
                mods.append("MOD_WIN")
            if want("MOD_DOCK"):
                dispcp.open_panel(m, ui.mo, S, lambda mm, **k:
                                  os88marty.ui_done(mm), page=dispcp.CP_IDOCK)
                acts["MOD_DOCK"] = (act_dock(m, ui), (2, 3))
                twice(m, ui, lba, "MOD_DOCK", acts["MOD_DOCK"][0], (2,))
                mods.append("MOD_DOCK")
            if want("MOD_WIN") or want("MOD_DOCK"):
                ui.close(ui.window("Control Panel"))
                ui.settle()

            if want("MOD_FMT"):
                acts["MOD_FMT"] = (act_format(m, ui), ())
                twice(m, ui, lba, "MOD_FMT", acts["MOD_FMT"][0])
                mods.append("MOD_FMT")
                close_disks(ui)

            if want("MOD_DISKOPS"):
                acts["MOD_DISKOPS"] = (act_compress(m, ui, names), ())
                twice(m, ui, lba, "MOD_DISKOPS", acts["MOD_DISKOPS"][0])
                mods.append("MOD_DISKOPS")
                close_disks(ui)

            modcache.desktop(ui)
            acts["MOD_DESK"] = (act_desk(m, ui), ())
            twice(m, ui, lba, "MOD_DESK", acts["MOD_DESK"][0])
            mods.append("MOD_DESK")
            # THE GESTURE A KEPT PANEL MUST NOT SWALLOW: the calendar's
            # launch registers its Sticky App row, a SYSTEM.CFG write through
            # sc_modcall - which asks mod_live whether a Control Panel is
            # open. With CTRL.DRV KEPT the answer must be no (its slots rest
            # on mod_gone), so the gesture loads the settings core; a
            # mod_live that read the held row as open far-called the panel's
            # disarmed slot and registered nothing
            check(modstate.state(m, "MOD_SETS") == "kept"
                  and modstate.state(m, "MOD_CTRL") == "kept",
                  "with the panel KEPT, the launch's registration and the "
                  "close's removal ran in the settings core (MOD_SETS %s, "
                  "MOD_CTRL %s)" % (modstate.state(m, "MOD_SETS"),
                                    modstate.state(m, "MOD_CTRL")))

            print("3: a compaction with every one of them kept")
            compact(m, ui, mods, tm)
            print("4: each used again, from wherever the pass left it")
            for mid in mods:
                act, q = acts[mid]
                if mid == "MOD_WIN":
                    dispcp.open_panel(m, ui.mo, S, lambda mm, **k:
                                      os88marty.ui_done(mm),
                                      page=dispcp.CP_ITHM)
                elif mid == "MOD_DOCK":
                    dispcp.open_panel(m, ui.mo, S, lambda mm, **k:
                                      os88marty.ui_done(mm),
                                      page=dispcp.CP_IDOCK)
                elif mid == "MOD_DESK":
                    modcache.desktop(ui)
                third(m, ui, lba, mid, act, 3 in q)
                if mid in ("MOD_WIN", "MOD_DOCK"):
                    ui.close(ui.window("Control Panel"))
                    ui.settle()
                close_disks(ui)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def hdd():
    tmp = tempfile.mkdtemp(prefix="modkeep-hd-")
    img = os.path.join(tmp, "sys.img")
    shutil.copy(os88marty.system_disk(os.path.join(BDIR, "os8088-360.img")),
                img)
    lba = Lba(img)
    try:
        with os88marty.launch(img, apps=bdisk(tmp), machine=os88marty.machine(
                "os8088_5150_herc_hdd_gla", None), boot=True) as m:
            ui = os88ui.UI(m)
            ui.up(limit=180.0)
            os88marty.no_saver(m)

            def go():
                ui.menu_pick("Apple", "Hibernate...")
                w = ui.wait_window("Hibernate", limit=T)
                ui.settle()
                m.key("Escape")
                ui._wait(lambda: "Hibernate" not in ui.titles(),
                         "the Hibernate window to close", T)
                ui.settle()
            tm = hole_open(ui)
            twice(m, ui, lba, "MOD_HIBER", go)
            compact(m, ui, ["MOD_HIBER"], tm)
            third(m, ui, lba, "MOD_HIBER", go, False)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def dual():
    import dispcp
    tmp = tempfile.mkdtemp(prefix="modkeep-x-")
    img = os.path.join(tmp, "sys.img")
    shutil.copy(os88marty.system_disk(os.path.join(BDIR, "os8088-360.img")),
                img)
    lba = Lba(img)
    try:
        with os88marty.launch(img, apps=bdisk(tmp), machine=os88marty.machine(
                "os8088_5150_both_gla", None), boot=True) as m:
            ui = os88ui.UI(m)
            ui.up(limit=180.0)
            os88marty.no_saver(m)
            tm = hole_open(ui)
            dispcp.open_panel(m, ui.mo, S, os88marty.settle)

            def go():
                dispcp.set_mode(m, ui.mo, S, os88marty.settle, "right")
                dispcp.set_mode(m, ui.mo, S, os88marty.settle, "single")
                ui.settle()
            twice(m, ui, lba, "MOD_EXT", go, (2,))
            # the panel closes first: LIVE it is MEM_K_MOD, a pinned claim
            # between EXTD.DRV's image and the hole, which nothing moves past
            ui.close(ui.window("Control Panel"))
            ui.settle()
            compact(m, ui, ["MOD_EXT", "MOD_CTRL"], tm)
            dispcp.open_panel(m, ui.mo, S, os88marty.settle)
            third(m, ui, lba, "MOD_EXT", go, True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


LEGS = {"host": host, "vga": vga, "hdd": hdd, "dual": dual}


def run(legs=("host", "vga", "hdd", "dual")):
    for k in legs:
        LEGS[k]()
    return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--legs", default="host,vga,hdd,dual")
    a = ap.parse_args()
    run(a.legs.split(","))
    print("modkeep: %s" % ("PASS" if not fails else
                          "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
