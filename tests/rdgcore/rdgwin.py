#!/usr/bin/env python3
"""RealDMG's speed IN ITS WINDOW, on an 86Box machine: the game's frames
against real time, and the frames drawn.

    python3 tests/rdgcore/rdgwin.py GAME.GB [--vm 286] [--secs 20] [--hold KEYS]
                                  [--profile N] [--keep NAME]
    python3 tests/rdgcore/rdgwin.py GAME.GB --attach NAME ...   (a --keep machine)

docs/reports/REALDMG-CORE-COST-2026-10-19.md section 3 times the core bare;
this times the package, with os8088 under it and the picture drawn. It writes
a 1.44MB B: disk holding build/realdmg.o88 and the game, boots vm/<VM>,
opens B: and double-clicks the game, lets it run 10 emulated seconds, then
reads:

  * the game's speed, from the core's own frame counter (S_FRAMES, in the
    view) across --secs of emulated time, against the 59.7275 frames a
    second a Game Boy runs at; and
  * the frames drawn a second, from the F1 overlay's line, which the package
    composes in its own memory whether or not the overlay is up.

--press taps keys first (os88box names, comma separated: return to start
a game; w5000 runs 5 emulated seconds between two). --hold presses keys (86Box scan codes, comma separated, 0x14D for
Right) for the whole timed span. --profile N then samples CS:IP N times, one
emulated millisecond apart, and charges each to a core handler, a package
routine or a kernel symbol (by linear address, docs/plans/DISK-CPU-PLAN.md
section 1). --keep leaves the machine running under NAME.

86Box times instructions by its model and is not cycle-accurate
(docs/86BOX.md). The game is the caller's and is never written anywhere but
the scratch disk.
"""
import argparse
import bisect
import collections
import os
import re
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tests", "rdgcore"))
import os88box  # noqa: E402
import gbwindow  # noqa: E402
import rdgprof  # noqa: E402

GB_HZ = 4194304 / 70224
LST = os.environ.get("RDGWIN_LST", os.path.join(ROOT, "build", "realdmg.lst"))   # (RDGWIN_O88's own)
CORELST = os.path.join(ROOT, "build", "realdmg", "rdgcore.asm.lst")
DRIVE_B = (592, 105)            # the B: icon on the VGA desktop
CLOSE_BOX = (170, 68)           # the RealDMG window's close box, where it opens
MENU_MACHINE = (140, 9)         # RealDMG's Machine menu on the bar (REALDMG-
MENU_SCREEN = (215, 9)          #   SPEC 14.3), its Screen and CPU menus, and
MENU_CPU = (267, 9)             #   an item's centre, item i (from 0)
ROW = lambda i: 28 + 16 * i
ITEM_RESET = (150, ROW(1))      # Machine's...
ITEM_FULLSCREEN = (150, ROW(3))
ITEM_LOG = (150, ROW(7))
ITEM_ILACE = (230, ROW(0))      # Screen's...
ITEM_BW = (230, ROW(2))
ITEM_COARSE = (230, ROW(3))
CPU_STATIC = (277, ROW(1))      # CPU's...
CPU_INTERP = (277, ROW(2))
CPU_PROFILE = (277, ROW(4))
FIRST_ICON = (160, 127)         # the first file in a fresh B: window (list view)
# the ami286's POST from a fresh CMOS, answered: Esc bypasses the memory
# count (5 emulated s; the size is still counted), F1 resumes past the CMOS
# checksum message, and the setup menu it can still land in is left to boot
BIOS_PROMPTS = (("bypass MEMORY test", "esc"), ("to RESUME", "f1"),
                ("EXIT FOR BOOT", "return"))


def find(b, tries=30):
    """the package, once it has a view: a big cartridge's read off the
    floppy outlasts the boot's ten seconds (Looney Tunes, 1MB), so it is
    looked for again every five emulated seconds"""
    for _ in range(5 * (tries - 1)):            # (the same budget, a second
        try:                                    #   at a time)
            return find1(b)
        except SystemExit:
            b.cmd("step 1000")
    try:
        return find1(b)
    except SystemExit:
        if os.environ.get("RDGWIN_FAILSHOT"):   # (what the glass said when
            b.shot(os.environ["RDGWIN_FAILSHOT"])   #   it was not found)
        raise


def find1(b):
    vo = gbwindow.label_off(LST, "rdg_viewseg")
    try:                                        # THE KERNEL'S OWN TABLE first: a
        import os88geom as g                    #   region kern_mega loaded above
        n = g.INST_MAX["big"] if isinstance(g.INST_MAX, dict) else g.INST_MAX
        raw = b.memr(b.sym("inst_tab"), n * g.I_RECSZ)   # 1MB (OS88_HIGH,
        for i in range(n):                      #   REALDMG-SPEC 16.2.7) is not
            r = raw[i * g.I_RECSZ:(i + 1) * g.I_RECSZ]   # in the scan below
            if r[g.I_STATE] != 1 or not r[g.I_KIND] & g.KIND_PKG:
                continue
            if not r[g.I_NAME:g.I_CYC].split(b"\0")[0].upper().startswith(b"REALDMG"):
                continue
            base = b.lin(int.from_bytes(r[g.I_SPTR:g.I_SPTR + 2], "little"))
            v = int.from_bytes(b.memr(base + vo, 2), "little")
            if v and int.from_bytes(b.memr(b.lin(v) + 0x4012, 2), "little") == v:
                core = int.from_bytes(b.memr(base + vo + 2, 2), "little")
                return base >> 4, v, core       # (a linear paragraph: the
    except (SystemExit, KeyError, AttributeError):  #   callers read seg x 16)
        pass
    mem = b"".join(b.memr(0x10000 + k, 0x8000) for k in range(0, 0x90000, 0x8000))
    for o in range(0, len(mem) - 32, 16):
        if mem[o:o+2] == b"O8" and mem[o+16:o+23] == b"REALDMG":
            v = int.from_bytes(mem[o+vo:o+vo+2], "little")
            if v and int.from_bytes(b.memr(b.lin(v) + 0x4012, 2), "little") == v:
                core = int.from_bytes(mem[o+vo+2:o+vo+4], "little")
                return (0x10000 + o) >> 4, v, core
    raise SystemExit("rdgwin: no running REALDMG with a view found")


def frames(b, view):
    return int.from_bytes(b.memr(b.lin(view) + 0x403E, 2), "little")


def bss_off(name):
    """a bss word's offset in the package, off the listing's first operand
    naming it (equ symbols carry no address of their own there)"""
    pat = re.compile(r"\[([0-9A-F]{4})\]\S*\s+.*\[(?:cs:)?%s\]" % re.escape(name))
    with open(LST, errors="replace") as f:
        for line in f:
            m = pat.search(line)
            if m:                           # (the listing's bytes: little end first)
                v = int(m.group(1), 16)
                return ((v & 0xFF) << 8) | (v >> 8)
    return None


def bankstat(b, seg):
    """rwbank.inc's: (slots, banks fetched), or None when it is one claim"""
    o = [bss_off(n) for n in ("rw_bcache", "rw_nslot", "rw_nmiss")]
    if None in o or not b.memr(seg*16 + o[0], 1)[0]:
        return None
    rd = lambda k: int.from_bytes(b.memr(seg*16 + k, 2), "little")
    return rd(o[1]), rd(o[2])


def jitstat(b, view):
    """S_JITFL, S_JITNB and S_JITSEG (rdgdefs.inc): flushes, blocks, the cache"""
    m = b.memr(b.lin(view) + 0x40C8, 8)
    return tuple(int.from_bytes(m[k:k+2], "little") for k in (0, 2, 4, 6))


def overlay(b, seg):
    m = re.search(rb"(\d+)% (\d+)fps(?: JIT| AOT)?\0", b.memr(seg*16, 0x8000) + b.memr(seg*16 + 0x8000, 0x8000))
    return (int(m.group(1)), int(m.group(2))) if m else None


def profile(b, seg, core, n, view=0):
    import os88sym
    jit = int.from_bytes(b.memr(b.lin(view) + 0x40CC, 2), "little") if view else 0
    psyms = rdgprof.labels(LST)
    pk = [o for o, _ in psyms]
    csyms = rdgprof.labels(CORELST) if os.path.exists(CORELST) else []
    ck = [o for o, _ in csyms]
    sys.path.insert(0, os.path.join(ROOT, "apps", "realdmg"))
    import rdggen
    tails = set(rdggen.TAILS)
    size = os.path.getsize(os.path.join(ROOT, "build", "realdmg.bin")) + 20000
    lin = []
    for nm in os88sym.syms():
        try:
            a = os88sym.linear(nm)
        except Exception:
            continue
        if a is not None and "." not in nm:
            lin.append((a, nm))
    lin.sort()
    lk = [a for a, _ in lin]
    imgs = {}                                   # a precompiled package's code
    if "realdmgaot" in LST:                     #   images (REALDMG-SPEC 20.10)
        import gbaotpkg
        aoff = gbaotpkg.equ_off(LST, "rw_aseg")
        bss = b.memr(seg * 16 + aoff, 2 * 14 + 1)
        imgs = {int.from_bytes(bss[2*i:2*i+2], "little"): i for i in range(14)}
        imgs.pop(0, None)
        def w(o):
            return int.from_bytes(bss[o:o + 2], "little")
        bss = b.memr(seg * 16 + aoff, 48)
        tseg, lseg, ln = w(35), w(39), w(41)     # rw_atseg, rw_alseg, rw_aln
        ents = b.memr(tseg * 16, 0x4000) + (b.memr(lseg * 16, ln * 8) if lseg else b"")
        ablk = collections.defaultdict(list)    # image segment -> (offset, key, PC)
        for k in range(0, len(ents) - 7, 8):
            pc, key, _, off, sg = struct.unpack_from("<HBBHH", ents, k)
            if off and sg in imgs:
                ablk[sg].append((off, key, pc))
        for v in ablk.values():
            v.sort()
        print("profile: %d code images in memory (of %d claimed): %s" % (
            len(imgs), bss[28], " ".join("%04X" % s for s in sorted(imgs))))
    fine = rdgprof.labels(LST, local=True) if os.environ.get("RDGWIN_FINE") else []
    fk = [o for o, _ in fine]
    fhits = collections.Counter()
    bhits = collections.Counter()
    aper = collections.Counter()
    hits = collections.Counter()
    where = collections.Counter()
    wpc = collections.Counter()
    jips = []
    ilin = []                                   # the images by LINEAR base:
    for sg, i in imgs.items():                  #   under kern_mega a segment
        try:                                    #   is a selector, and its
            ilin.append((b.lin(sg), sg, i))     #   base is no segment x 16
        except Exception:
            pass
    for _ in range(n):
        b.cmd("step 1")
        r = b.cmd("regs")
        ip = r["cs_base"] + (r["eip"] & 0xFFFF)
        for lb, sg, i in ilin:
            if lb <= ip < lb + 0x10000:
                r = dict(r, cs_base=sg << 4)    # (as the real-mode test reads it)
                ip = (sg << 4) + (r["eip"] & 0xFFFF)
                break
        if (r["cs_base"] >> 4) in imgs:
            sg = r["cs_base"] >> 4
            hits["aot img%d" % imgs[sg]] += 1
            v = ablk.get(sg, [])
            k = bisect.bisect_right(v, (r["eip"] & 0xFFFF, 256, 0)) - 1
            if k >= 0:
                aper["%02X:%04X" % (v[k][1], v[k][2])] += 1
        elif seg*16 <= ip < seg*16 + size:
            k = bisect.bisect_right(pk, ip - seg*16) - 1
            hits["pkg " + (psyms[k][1] if k >= 0 else "?")] += 1
            if fine:                            # RDGWIN_FINE: by local label
                k = bisect.bisect_right(fk, ip - seg*16) - 1
                fhits[fine[k][1] if k >= 0 else "?"] += 1
        elif jit and b.lin(jit) <= ip < b.lin(jit) + 0x10000 and ((ip - b.lin(jit)) >> 8) in tails:
            k = bisect.bisect_right(ck, ip - b.lin(jit)) - 1  # the core's tails, copied
            hits["tail " + (csyms[k][1] if ck and k >= 0 else "?")] += 1
        elif jit and b.lin(jit) <= ip < b.lin(jit) + 0x10000:
            hits["jit code"] += 1
            jips.append(ip - b.lin(jit))
        elif b.lin(core) <= ip < b.lin(core) + 0x10000:
            k = bisect.bisect_right(ck, ip - b.lin(core)) - 1
            hits["core " + (csyms[k][1] if ck and k >= 0 else "%02X" % ((ip - b.lin(core)) >> 8))] += 1
            pc = r["esi"] & 0xFFFF                  # SI: the Game Boy's PC
            where["ROM0" if pc < 0x4000 else "ROMX" if pc < 0x8000 else
                  "HRAM" if pc >= 0xFF80 else "RAM"] += 1
            bk = b.memr(b.lin(view) + 0x4035, 1)[0] if 0x4000 <= pc < 0x8000 else None
            wpc["%s%04X" % ("%02X:" % bk if bk is not None else "", pc & 0xFFF0)] += 1
        elif ip >= 0xF0000:
            hits["BIOS"] += 1
            if fine:
                fhits["BIOS %06X" % (ip & 0xFFFFC0)] += 1
                if ip >= 0x100000:              # above 1MB: kern_mega's .cold
                    bhits["hi cs %04X ip %04X" % (r["cs"], r["eip"] & 0xFFF0)] += 1   # / .mega
                pm = r.get("cr0", 0) & 1        # who called it: real mode's
                if pm:                          #   stack is SS x 16 (in
                    ss = r["ss"]                #   kern_mega: an identity GDT
                    sb = ss * 16 if not ss & 7 else ((ss & 0xFFF8) << 2) * 16   # entry,
                    st = b.memr(sb + (r["esp"] & 0xFFFF), 12)   # or an LDT alias)
                    w = [int.from_bytes(st[i:i + 2], "little") for i in range(0, 12, 2)]
                    bhits["pm cs %04X ss %04X: %s" % (r["cs"], ss,
                        " ".join("%04X" % x for x in w))] += 1
                else:                           #   protected mode no base
                    st = b.memr(r["ss"] * 16 + (r["esp"] & 0xFFFF), 12)   # is given)
                    w = [int.from_bytes(st[i:i + 2], "little") for i in range(0, 12, 2)]
                    bhits["real ss %04X: %s" % (r["ss"], " ".join("%04X" % x for x in w))] += 1
        else:
            k = bisect.bisect_right(lk, ip) - 1
            hits["kern " + (lin[k][1] if k >= 0 else "?")] += 1
            if fine:
                fhits["kern %05X" % (ip & 0xFFFF0)] += 1
    groups = collections.Counter()
    for k, v in hits.items():
        groups[k.split()[0]] += v
    print("profile: " + "  ".join("%s %.1f%%" % (g, 100.0*v/n) for g, v in groups.most_common()))
    for k, v in hits.most_common(30):
        print("%6.1f%% %s" % (100.0*v/n, k))
    if jips and jit:
        # each translated sample to its block: the table's entries are PC,
        # bank key, count, offset (REALDMG-SPEC 16.3); a sample is charged to
        # the block with the greatest offset at or below it
        tab = b.memr((jit + 0x1000) * 16, 0x4000)   # the table, after the cache
        blocks = sorted((int.from_bytes(tab[k+4:k+6], "little"),
                         int.from_bytes(tab[k:k+2], "little"), tab[k+2])
                        for k in range(0, 0x4000, 8) if tab[k+4] | tab[k+5])
        offs = [o for o, _, _ in blocks]
        per = collections.Counter()
        for ip in jips:
            k = bisect.bisect_right(offs, ip) - 1
            if k >= 0:
                per["%02X:%04X" % (blocks[k][2], blocks[k][1])] += 1
        print("translated samples by block (bank:PC), of %d: " % len(jips) + "  ".join(
            "%s %.1f%%" % (k, 100.0 * v / len(jips)) for k, v in per.most_common(16)))
        if os.environ.get("RDGWIN_DUMP"):            # the hottest block's code,
            import subprocess                       #   with its samples
            k0 = per.most_common(1)[0][0]
            for k, (o, pc, bk) in enumerate(blocks):
                if "%02X:%04X" % (bk, pc) == k0:
                    end = offs[k + 1] if k + 1 < len(offs) else o + 512
                    code = b.memr(b.lin(jit) + o, min(end - o, 1024))
                    tmp = os.path.join(ROOT, "build", "realdmg", "rdgwin.dump")
                    open(tmp, "wb").write(code)
                    dis = subprocess.run(["ndisasm", "-b16", "-o", str(o), tmp],
                                         capture_output=True, text=True).stdout
                    cnt = collections.Counter(jips)
                    print("block %s at %04X, %d bytes:" % (k0, o, len(code)))
                    for ln in dis.splitlines():
                        print("%4s %s" % (cnt.get(int(ln[:8], 16), ""), ln))
    for k, v in bhits.most_common(25):         # the BIOS's samples by stack
        print("bios %4d %s" % (v, k))
    if os.environ.get("RDGWIN_HIDUMP"):         # the high code's bytes, to name
        for k, v in bhits.most_common(4):       #   the image they belong to
            m = re.match(r"hi cs ([0-9A-F]{4}) ip ([0-9A-F]{4})", k)
            if m:
                cs, ip = int(m.group(1), 16), int(m.group(2), 16)
                print("hidump %04X:%04X lin %06X %s" % (cs, ip, b.lin(cs),
                      b.memr(b.lin(cs) + ip, 48).hex()))
                pk = os.environ.get("RDGWIN_HIPEEK")
                if pk:
                    print("hipeek %04X:%s %s" % (cs, pk, b.memr(b.lin(cs) + int(pk, 16), 4).hex()))
    for k, v in fhits.most_common(60):
        print("fine %5.1f%% %s" % (100.0*v/n, k))
    if aper:
        print("precompiled samples by block (key:PC), of %d: " % sum(aper.values()) + "  ".join(
            "%s %.1f%%" % (k, 100.0 * v / sum(aper.values())) for k, v in aper.most_common(20)))
    if where:
        print("core samples by the Game Boy PC (SI): " + "  ".join(
            "%s %.1f%%" % (k, 100.0*v/sum(where.values())) for k, v in where.most_common()))
        print("...by 16 bytes of it (bank:PC): " + "  ".join(
            "%s %.1f%%" % (k, 100.0*v/n) for k, v in wpc.most_common(16)))


def readlog(b, o):
    """the newest *.LOG beside REALDMG.O88 on the B: disk, onto o.log"""
    import os88flush
    img = os.path.join(os88box.RUNS, o.keep or b.name, "fdd2.img")
    vol = os88flush.Volume(open(img, "rb").read())
    names = sorted(e.name for e in vol.listdir("") if e.name.upper().endswith(".LOG"))
    if not names:
        print("rdgwin: no log on the disk")
        return
    with open(o.log, "wb") as f:
        f.write(vol.read(names[-1]))
    print("rdgwin: %s -> %s" % (names[-1], o.log))


def readrbp(b, o):
    """GAME.RDP off the B: disk, onto o.rdp, its trailer checked"""
    import os88flush
    img = os.path.join(os88box.RUNS, b.name, "fdd2.img")
    vol = os88flush.Volume(open(img, "rb").read())
    names = [e.name for e in vol.listdir("") if e.name.upper().endswith(".RDP")]
    if not names:
        sys.exit("rdgwin: no .RDP on the disk - the profile was not written")
    d = vol.read(names[0])
    n = sum(1 for k in range(0, 8192, 4) if d[k + 3])
    ok = len(d) == 8201 and d[8192:8196] == b"RDP1"
    print("rdgwin: %s, %d bytes, %d entries, trailer %s" % (names[0], len(d), n, "good" if ok else "BAD"))
    with open(o.rdp, "wb") as f:
        f.write(d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--vm", default="286")
    ap.add_argument("--secs", type=float, default=20)
    ap.add_argument("--hold", default="")
    ap.add_argument("--profile", type=int, default=0)
    ap.add_argument("--keep", default="")
    ap.add_argument("--shot", default="")
    ap.add_argument("--attach", default="", help="measure a machine --keep left running")
    ap.add_argument("--press", default="", help="keys to tap first, e.g. return")
    ap.add_argument("--replay", default="", metavar="SS1,RP1",
                    help="a save state and its replay, put beside the game as "
                         "slot 1: --press w3000,f7 plays it")
    ap.add_argument("--fs", action="store_true", help="Machine > Full Screen first")
    ap.add_argument("--mem", type=int, help="the machine's RAM in KB, for the VM's own")
    ap.add_argument("--nojit", action="store_true", help="CPU > Interpreter first")
    ap.add_argument("--ilace", type=int, nargs="?", const=1, default=0,
                    help="Screen > Interlace N times first: 1 On, 2 Scrolling")
    ap.add_argument("--log", default="", help="Machine > Log over the span, written here")
    ap.add_argument("--static", type=float, default=None, metavar="SECS",
                    help="the recompiler's Static (REALDMG-SPEC 16.8), timed on the SAME span a "
                         "plain run times: after --press, --hold's keys held SECS emulated seconds "
                         "with the Recompiler recording the profile, then CPU > Static "
                         "Recompiler, Machine > Reset and --press again")
    ap.add_argument("--rdp", default="", metavar="OUT",
                    help="after the span, the window closed - which writes the profile, GAME.RDP, "
                         "beside the game (REALDMG-SPEC 16.8) - and copy it off the B: disk to OUT")
    ap.add_argument("--rdpin", default="", metavar="IN",
                    help="put IN on the B: disk as GAME.RDP, the profile a session before wrote")
    ap.add_argument("--big", action="store_true",
                    help="CPU > Cache: 128K from the start - a REALDMG.CFG saying so in the B: "
                         "disk's SYSTEM/APPDATA (REALDMG-SPEC 16.2.1)")
    ap.add_argument("--persec", action="store_true",
                    help="the game's speed second by second over the span: a stall shows")
    o = ap.parse_args()

    if o.attach:
        o.keep = o.attach
        b = os88box.Box(o.attach)
    else:
        b = boot(o)
    try:
        measure(b, o)
    finally:
        if not o.keep:
            b.quit()                            # (and waits: the disks written)
    if o.log and not o.keep:
        readlog(b, o)                           # (86Box writes the disk at quit)
    if o.rdp and not o.keep:
        readrbp(b, o)


def boot(o):
    name = o.keep or "rdgwin%d" % os.getpid()
    tmp = tempfile.mkdtemp(prefix="rdgwin")
    gb = os.path.join(tmp, os.environ.get("RDGWIN_GBNAME", "GAME.GB"))   # (a precompiled
                                                # package names its own ROM)
    with open(o.rom, "rb") as f, open(gb, "wb") as g:
        g.write(f.read())
    disk = os.path.join(tmp, "rdg144.img")
    extra = []
    if getattr(o, "rdpin", ""):                 # (gbbank's options have no
        extra = [os.path.join(tmp, "GAME.RDP")]  #   such fields)
        with open(o.rdpin, "rb") as f, open(extra[0], "wb") as g:
            g.write(f.read())
    if getattr(o, "big", False):
        cfg = os.path.join(tmp, "REALDMG.CFG")
        with open(cfg, "wb") as g:
            g.write(b"RD\x01\x01")
        extra.append("SYSTEM/APPDATA:" + cfg)
    for i, src in enumerate((getattr(o, "replay", "") or "").split(",")[:2]):
        if src:                                 # --replay: a save state and its
            dst = os.path.join(tmp, "GAME." + ("SS1", "RP1")[i])  # replay,
            with open(src, "rb") as f, open(dst, "wb") as g:      #   beside the
                g.write(f.read())               #   game as its slot 1 (F7)
            extra.append(dst)
    extra += [p for p in os.environ.get("RDGWIN_EXTRA", "").split(os.pathsep) if p]  # (more
                                                # files on the B: disk: a second game)
    os88box.subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
                            "--size", "1440", os.environ.get("RDGWIN_O88", os.path.join(ROOT, "build", "realdmg.o88")), gb]
                           + extra, check=True, stdout=os88box.subprocess.DEVNULL)
    # RDGWIN_A: another system disk - build/mega.img for kern_mega, whose
    # kernel map os88box takes from build/megak/ for a disk named mega*
    # (REALDMG-SPEC section 16.2.4's measurement, on vm/286-mega)
    a = os.environ.get("RDGWIN_A", os.path.join(ROOT, "build", "os8088.img"))
    b = os88box.launch(o.vm, name=name, hold=True, fresh=True, mem=o.mem,
                       a=a, b=disk,
                       temp=not o.keep, turbo=getattr(o, "quick", False))
    # THE BOOT IS GATED ON THE KERNEL'S STATE (os88box.Box.kernel_up): the
    # ami286's fresh CMOS stops in setup and is answered on the way. It was
    # 60 emulated s of polled steps plus a fixed 20 whatever the boot took,
    # against a desktop up at ~22 (SOAK-SPEED round 3). The mr286's MR BIOS
    # (vm/286-mega*) wants its floppy page set instead, megacdrom's way
    if "mega" in o.vm:
        import megacdrom
        megacdrom.mr_setup(b)
    b.kernel_up(prompts=BIOS_PROMPTS)
    wins = len(windows(b))
    b.to(*DRIVE_B)
    b.click(2)                                  # the B: window: open and drawn
    b.until(lambda: len(windows(b)) > wins, "the B: window", 20000, 100)
    b.ui_done(limit_ms=20000)
    b.to(FIRST_ICON[0], FIRST_ICON[1] + (16 if getattr(o, "big", False) else 0))
    b.click(2)                                  # (--big's SYSTEM folder lists first)
    if getattr(o, "quick", False):              # the caller waits on the package
        b.cmd("step 500")                       #   itself (find, the core's frames)
    else:
        b.cmd("step 10000")                     # (a measurement's lead-in, kept)
    return b


def windows(b):
    """the visible windows' W_SEGs, out of wm_wins (kern_big's geometry)"""
    import os88geom as g
    tab, out = b.memr(b.sym("wm_wins"), g.WIN_SIZE * g.MAX_WIN), []
    for i in range(g.MAX_WIN):
        r = g.WIN_SIZE * i
        if tab[r + g.W_FLAGS] & 1:
            out.append(tab[r + g.W_SEG] | tab[r + g.W_SEG + 1] << 8)
    return out


def machine_item(b, item, menu=MENU_MACHINE):
    """Machine > ITEM (or MENU's): press on the menu, drag to the item, let go"""
    b.to(*menu)
    b.mouse(0, 0, 1)
    b.pause(200)
    b.to(*item, buttons=1)
    b.pause(200)
    b.mouse(0, 0, 0)
    b.cmd("step 1500")


def press(b, o):
    for k in o.press.split(","):
        if re.match(r"w\d+$", k):              # w5000: run 5 emulated s
            b.cmd("step %s" % k[1:])
        elif k:                                 # held 300 ms: a game polls
            codes = [os88box.KEYS[c] for c in k.split("+")]   # once a frame,
            for c in codes:                     #   and a slow frame is
                b.cmd("key down 0x%X" % c)      #   longer than a tap (a
                b.cmd("step 50")                #   chord's keys one at a
            b.cmd("step 300")                   #   time)
            for c in reversed(codes):
                b.cmd("key up 0x%X" % c)
            b.cmd("step 2000")                  #   a tap (shift+f2: a chord)


def measure(b, o):
    if True:
        if (o.static is not None or o.rdp) and not o.rdpin:  # CPU >
                                                # Profile on (--rdpin's load
                                                # turned it on itself), from
            find(b)                             #   the game's start (it is off
            machine_item(b, CPU_PROFILE, MENU_CPU)  # by default) - once it has
        if o.log and o.replay:                  # (a replay's log on BEFORE F7,
            find(b)                             #   so its PLAY+ is a record's)
            machine_item(b, ITEM_LOG)
        press(b, o)                             #   loaded: a click during the
                                                #   load's read is lost
        if o.nojit:
            machine_item(b, CPU_INTERP, MENU_CPU)
        if o.static is not None:                # the span played with On,
            keys = [int(k, 0) for k in o.hold.split(",") if k]
            for k in keys:                      #   recording the profile...
                b.cmd("key down 0x%02X" % k)
            b.cmd("step %d" % int(o.static * 1000))
            for k in keys:
                b.cmd("key up 0x%02X" % k)
            machine_item(b, CPU_STATIC, MENU_CPU)  # ...then Static, the game from
            machine_item(b, ITEM_RESET)         #   its start, and the same span
            b.cmd("step 3000")
            press(b, o)
        for _ in range(o.ilace):
            machine_item(b, ITEM_ILACE, MENU_SCREEN)
        if o.log and not o.replay:
            machine_item(b, ITEM_LOG)          # Log: Off -> on
        if o.fs:                                # (a busy machine can open the
            fso = bss_off("rw_infsx")           #   menu after the drag has gone
            for _ in range(5):                  #   by: tried until the package
                b.to(*MENU_MACHINE)             #   says it is in the bracket)
                b.mouse(0, 0, 1)
                b.pause(600)
                b.to(*ITEM_FULLSCREEN, buttons=1)
                b.pause(400)
                b.mouse(0, 0, 0)
                b.cmd("step 3000")
                if fso is None or b.memr(find(b)[0] * 16 + fso, 1)[0]:
                    break
            else:
                raise SystemExit("rdgwin: Machine > Full Screen never took")
            if os.environ.get("RDGWIN_FSPRESS"):    # keys inside the bracket
                o.press, was = os.environ["RDGWIN_FSPRESS"], o.press   # (shift+f2:
                press(b, o)                         #   Steady pacing)
                o.press = was
        seg, view, core = find(b)
        keys = [int(k, 0) for k in o.hold.split(",") if k]
        for k in keys:
            b.cmd("key down 0x%02X" % k)
        b.cmd("step 2000")
        f0, t0 = frames(b, view), b.cmd("info").get("ms")
        hpo = bss_off("rw_plast")               # the draws' gaps (rw_timed):
        def gaps():                             #   rw_phist, after rw_plast
            if hpo is None:
                return None
            r = b.memr(seg*16 + hpo + 2, 16)
            return [int.from_bytes(r[2*i:2*i+2], "little") for i in range(8)]
        h0 = gaps()
        j0 = jitstat(b, view)
        rcy0 = int.from_bytes(b.memr(b.lin(view) + 0x482F, 2), "little")     # S_JITRCY
        bk0 = bankstat(b, seg)
        e0 = int.from_bytes(b.memr(b.lin(view) + 0x408A, 2), "little")     # S_EVCOUNT
        if o.persec:                            # a second at a time
            per, fp = [], f0
            ovo = bss_off("rw_ovltxt")          # ...and the F1 line, as the
            rows = []                           #   glass shows it each second
            hp = gaps()
            for _ in range(int(o.secs)):
                b.cmd("step 1000")
                f = frames(b, view)
                per.append(100.0 * ((f - fp) & 0xFFFF) / GB_HZ)
                fp = f
                ov = b.memr(seg*16 + ovo, 16).split(b"\0")[0].decode("latin-1") \
                    if ovo is not None else "?"
                hn = gaps() if os.environ.get("RDGWIN_GAPS") else None
                if hn is not None:              # this second's gaps
                    hs = [(y - x) & 0xFFFF for x, y in zip(hp, hn)]
                    hp = hn
                    pko = bss_off("rw_pk")
                    pk = b.memr(seg*16 + pko, 1)[0] if pko is not None else 0
                    pco = bss_off("rw_pace")
                    pc = b.memr(seg*16 + pco, 1)[0] if pco is not None else 9
                    pdo = bss_off("rw_pdn")
                    pdn = b.memr(seg*16 + pdo, 1)[0] if pdo is not None else 0
                    pil = bss_off("rw_pidl")
                    pi = int.from_bytes(b.memr(seg*16 + pil, 2), "little") if pil is not None else 0
                    ov = "%s p%d k%d d%d i%d [%s]" % (ov, pc, pk, pdn, pi, "".join("%d," % v for v in hs).rstrip(","))
                dlo = bss_off("rw_dlast")      # the last draw's length
                if dlo is not None:
                    dl = int.from_bytes(b.memr(seg*16 + dlo, 4), "little")
                    ov = "%s %4.1fms" % (ov, dl * 0.000838)
                rows.append((per[-1], ov,
                             int.from_bytes(b.memr(b.lin(view) + 0x482F, 2), "little"),
                             bankstat(b, seg)))
            print("rdgwin: each second, %% of real time: %s; least %.0f%%, most %.0f%%" % (
                " ".join("%.0f" % p for p in per), min(per), max(per)))
            print("rdgwin: second  frames%  F1 line            recycled  banks")
            for i, (p, ov, rc, bk) in enumerate(rows):
                print("rdgwin: %6d  %6.0f%%  %-40s  %8d  %5s" % (
                    i + 1, p, ov, (rc - rcy0) & 0xFFFF,
                    "-" if bk is None else ((bk[1] - bk0[1]) & 0xFFFF) if bk0 else bk[1]))
        else:
            b.cmd("step %d" % int(o.secs * 1000))
        f1, t1 = frames(b, view), b.cmd("info").get("ms")
        j1 = jitstat(b, view)
        e1 = int.from_bytes(b.memr(b.lin(view) + 0x408A, 2), "little")
        ov = overlay(b, seg)
        secs = (t1 - t0) / 1000.0
        n = (f1 - f0) & 0xFFFF
        print("rdgwin %s on %s: %d frames in %.1f emulated s = %.2f fps, %.1f%% of real time; "
              "overlay: %s" % (os.path.basename(o.rom), o.vm, n, secs, n / secs,
                               100.0 * n / secs / GB_HZ,
                               "%d%% %d fps drawn" % ov if ov else "none"))
        h1 = gaps()
        if h0 and h1:
            h = [(y - x) & 0xFFFF for x, y in zip(h0, h1)]
            tot = sum(h) or 1
            print("rdgwin: draws by frames since the one before (1..7, 8+): %s; "
                  "%d draws, mean gap %.2f" % (" ".join(str(v) for v in h), sum(h),
                                              sum((i + 1) * v for i, v in enumerate(h)) / tot))
        print("rdgwin: %.1f events a frame (S_EVCOUNT, which wraps at 65536)"
              % (((e1 - e0) & 0xFFFF) / max(n, 1)))
        nb = (j1[1] - j0[1]) & 0xFFFF
        print("rdgwin: recompiler %s - %d blocks translated (%d bytes each), %d flushes in the span"
              % ("on" if j1[2] else "off", nb, ((j1[3] - j0[3]) & 0xFFFF) * 16 // max(nb, 1),
                 (j1[0] - j0[0]) & 0xFFFF))
        sb = b.memr(b.lin(view) + 0x4812, 2)                        # S_JITSEGB, S_JITSW
        if int.from_bytes(sb, "little"):
            print("rdgwin: a second cache segment (REALDMG-SPEC 16.2.1), %d moves into it "
                  "so far" % int.from_bytes(b.memr(b.lin(view) + 0x4816, 2), "little"))
        xn = b.memr(b.lin(view) + 0x48BF, 1)[0]                      # S_JITXN
        rcy1 = int.from_bytes(b.memr(b.lin(view) + 0x482F, 2), "little")
        print("rdgwin: %d extra cache tiers (REALDMG-SPEC 16.2.5); %d tiers recycled in the "
              "span, %d so far" % (xn, (rcy1 - rcy0) & 0xFFFF, rcy1))
        if o.static is not None:
            fx = b.memr(b.lin(view) + 0x40F6, 1)[0]                  # S_JITFIX
            npo = bss_off("rw_npre")
            npre = int.from_bytes(b.memr(seg*16 + npo, 2), "little") if npo else -1
            print("rdgwin: Static - %d blocks preloaded, table %s" % (
                npre, "fixed" if fx else "NOT fixed"))
        bk = bankstat(b, seg)
        print("rdgwin: cartridge %s" % ("in one claim" if bk is None else
              "in the bank cache, %d slots, %d banks fetched (%d in the span)"
              % (bk + (((bk[1] - bk0[1]) & 0xFFFF) if bk0 else bk[1],))))
        if o.shot:
            b.shot(o.shot)
        if o.profile:
            profile(b, seg, core, o.profile, view)
        for k in keys:
            b.cmd("key up 0x%02X" % k)
        if o.log:
            if o.fs:
                b.tap("esc")                    # out of the bracket: the menu
                b.cmd("step 3000")
            machine_item(b, ITEM_LOG)          # ...and off: the file written
            b.cmd("step 6000")
        if o.rdp:                               # the window closed: the profile
            b.to(*CLOSE_BOX)                    #   written beside the game
            b.click(1)
            b.cmd("step 10000")


if __name__ == "__main__":
    main()
