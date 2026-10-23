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
MENU_CPU = (203, 9)             #   SPEC 14.3), its CPU menu, and an item's
ROW = lambda i: 28 + 16 * i     #   centre, item i (from 0)
ITEM_RESET = (150, ROW(1))      # Machine's...
ITEM_FULLSCREEN = (150, ROW(3))
ITEM_ILACE = (150, ROW(7))
ITEM_LOG = (150, ROW(8))
CPU_STATIC = (213, ROW(1))      # CPU's...
CPU_INTERP = (213, ROW(2))
CPU_PROFILE = (213, ROW(4))
FIRST_ICON = (160, 127)         # the first file in a fresh B: window (list view)


def find(b, tries=30):
    """the package, once it has a view: a big cartridge's read off the
    floppy outlasts the boot's ten seconds (Looney Tunes, 1MB), so it is
    looked for again every five emulated seconds"""
    for _ in range(tries - 1):
        try:
            return find1(b)
        except SystemExit:
            b.cmd("step 5000")
    return find1(b)


def find1(b):
    vo = gbwindow.label_off(LST, "rdg_viewseg")
    mem = b"".join(b.memr(0x10000 + k, 0x8000) for k in range(0, 0x90000, 0x8000))
    for o in range(0, len(mem) - 32, 16):
        if mem[o:o+2] == b"O8" and mem[o+16:o+23] == b"REALDMG":
            v = int.from_bytes(mem[o+vo:o+vo+2], "little")
            if v and int.from_bytes(b.memr(v*16 + 0x4012, 2), "little") == v:
                core = int.from_bytes(mem[o+vo+2:o+vo+4], "little")
                return (0x10000 + o) >> 4, v, core
    raise SystemExit("rdgwin: no running REALDMG with a view found")


def frames(b, view):
    return int.from_bytes(b.memr(view*16 + 0x403E, 2), "little")


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
    m = b.memr(view*16 + 0x40C8, 8)
    return tuple(int.from_bytes(m[k:k+2], "little") for k in (0, 2, 4, 6))


def overlay(b, seg):
    m = re.search(rb"(\d+)% (\d+)fps(?: JIT| AOT)?\0", b.memr(seg*16, 0x8000) + b.memr(seg*16 + 0x8000, 0x8000))
    return (int(m.group(1)), int(m.group(2))) if m else None


def profile(b, seg, core, n, view=0):
    import os88sym
    jit = int.from_bytes(b.memr(view * 16 + 0x40CC, 2), "little") if view else 0
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
    hits = collections.Counter()
    where = collections.Counter()
    wpc = collections.Counter()
    jips = []
    for _ in range(n):
        b.cmd("step 1")
        r = b.cmd("regs")
        ip = r["cs_base"] + (r["eip"] & 0xFFFF)
        if seg*16 <= ip < seg*16 + size:
            k = bisect.bisect_right(pk, ip - seg*16) - 1
            hits["pkg " + (psyms[k][1] if k >= 0 else "?")] += 1
        elif jit and jit*16 <= ip < jit*16 + 0x10000 and ((ip - jit*16) >> 8) in tails:
            k = bisect.bisect_right(ck, ip - jit*16) - 1  # the core's tails, copied
            hits["tail " + (csyms[k][1] if ck and k >= 0 else "?")] += 1
        elif jit and jit*16 <= ip < jit*16 + 0x10000:
            hits["jit code"] += 1
            jips.append(ip - jit*16)
        elif core*16 <= ip < core*16 + 0x10000:
            k = bisect.bisect_right(ck, ip - core*16) - 1
            hits["core " + (csyms[k][1] if ck and k >= 0 else "%02X" % ((ip - core*16) >> 8))] += 1
            pc = r["esi"] & 0xFFFF                  # SI: the Game Boy's PC
            where["ROM0" if pc < 0x4000 else "ROMX" if pc < 0x8000 else
                  "HRAM" if pc >= 0xFF80 else "RAM"] += 1
            bk = b.memr(view * 16 + 0x4035, 1)[0] if 0x4000 <= pc < 0x8000 else None
            wpc["%s%04X" % ("%02X:" % bk if bk is not None else "", pc & 0xFFF0)] += 1
        elif ip >= 0xF0000:
            hits["BIOS"] += 1
        else:
            k = bisect.bisect_right(lk, ip) - 1
            hits["kern " + (lin[k][1] if k >= 0 else "?")] += 1
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
                    code = b.memr(jit * 16 + o, min(end - o, 1024))
                    tmp = os.path.join(ROOT, "build", "realdmg", "rdgwin.dump")
                    open(tmp, "wb").write(code)
                    dis = subprocess.run(["ndisasm", "-b16", "-o", str(o), tmp],
                                         capture_output=True, text=True).stdout
                    cnt = collections.Counter(jips)
                    print("block %s at %04X, %d bytes:" % (k0, o, len(code)))
                    for ln in dis.splitlines():
                        print("%4s %s" % (cnt.get(int(ln[:8], 16), ""), ln))
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
    ap.add_argument("--fs", action="store_true", help="Machine > Full Screen first")
    ap.add_argument("--mem", type=int, help="the machine's RAM in KB, for the VM's own")
    ap.add_argument("--nojit", action="store_true", help="CPU > Interpreter first")
    ap.add_argument("--ilace", type=int, nargs="?", const=1, default=0,
                    help="Machine > Interlace N times first: 1 On, 2 Scrolling")
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
            b.cmd("quit")
    if o.log and not o.keep:
        readlog(b, o)                           # (86Box writes the disk at quit)
    if o.rdp and not o.keep:
        readrbp(b, o)


def boot(o):
    name = o.keep or "rdgwin%d" % os.getpid()
    tmp = tempfile.mkdtemp(prefix="rdgwin")
    gb = os.path.join(tmp, "GAME.GB")
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
    extra += [p for p in os.environ.get("RDGWIN_EXTRA", "").split(os.pathsep) if p]  # (more
                                                # files on the B: disk: a second game)
    os88box.subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
                            "--size", "1440", os.environ.get("RDGWIN_O88", os.path.join(ROOT, "build", "realdmg.o88")), gb]
                           + extra, check=True, stdout=os88box.subprocess.DEVNULL)
    b = os88box.launch(o.vm, name=name, hold=True, fresh=True, mem=o.mem,
                       a=os.path.join(ROOT, "build", "os8088.img"), b=disk)
    if True:
        for _ in range(120):
            b.cmd("step 500")
            try:
                t = "".join(bytes.fromhex(x).decode("cp437") for x in b.cmd("text").get("chars", []))
            except SystemExit:
                t = ""
            if "EXIT FOR BOOT" in t:
                b.tap("return")
        b.cmd("step 20000")
        b.to(*DRIVE_B)
        b.click(2)
        b.cmd("step 6000")
        b.to(FIRST_ICON[0], FIRST_ICON[1] + (16 if getattr(o, "big", False) else 0))
        b.click(2)                              # (--big's SYSTEM folder lists first)
        b.cmd("step 10000")
    return b


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
            code = os88box.KEYS[k]              #   once a frame, and a slow
            b.cmd("key down 0x%X" % code)       #   frame is longer than a tap
            b.cmd("step 300")
            b.cmd("key up 0x%X" % code)
            b.cmd("step 2000")


def measure(b, o):
    if True:
        if (o.static is not None or o.rdp) and not o.rdpin:  # CPU >
                                                # Profile on (--rdpin's load
                                                # turned it on itself), from
            find(b)                             #   the game's start (it is off
            machine_item(b, CPU_PROFILE, MENU_CPU)  # by default) - once it has
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
            machine_item(b, ITEM_ILACE)
        if o.log:
            machine_item(b, ITEM_LOG)          # Log: Off -> on
        if o.fs:
            b.to(*MENU_MACHINE)
            b.mouse(0, 0, 1)
            b.pause(200)
            b.to(*ITEM_FULLSCREEN, buttons=1)
            b.pause(200)
            b.mouse(0, 0, 0)
            b.cmd("step 3000")
        seg, view, core = find(b)
        keys = [int(k, 0) for k in o.hold.split(",") if k]
        for k in keys:
            b.cmd("key down 0x%02X" % k)
        b.cmd("step 2000")
        f0, t0 = frames(b, view), b.cmd("info").get("ms")
        j0 = jitstat(b, view)
        e0 = int.from_bytes(b.memr(view*16 + 0x408A, 2), "little")     # S_EVCOUNT
        if o.persec:                            # a second at a time
            per, fp = [], f0
            for _ in range(int(o.secs)):
                b.cmd("step 1000")
                f = frames(b, view)
                per.append(100.0 * ((f - fp) & 0xFFFF) / GB_HZ)
                fp = f
            print("rdgwin: each second, %% of real time: %s; least %.0f%%, most %.0f%%" % (
                " ".join("%.0f" % p for p in per), min(per), max(per)))
        else:
            b.cmd("step %d" % int(o.secs * 1000))
        f1, t1 = frames(b, view), b.cmd("info").get("ms")
        j1 = jitstat(b, view)
        e1 = int.from_bytes(b.memr(view*16 + 0x408A, 2), "little")
        ov = overlay(b, seg)
        secs = (t1 - t0) / 1000.0
        n = (f1 - f0) & 0xFFFF
        print("rdgwin %s on %s: %d frames in %.1f emulated s = %.2f fps, %.1f%% of real time; "
              "overlay: %s" % (os.path.basename(o.rom), o.vm, n, secs, n / secs,
                               100.0 * n / secs / GB_HZ,
                               "%d%% %d fps drawn" % ov if ov else "none"))
        print("rdgwin: %.1f events a frame (S_EVCOUNT, which wraps at 65536)"
              % (((e1 - e0) & 0xFFFF) / max(n, 1)))
        nb = (j1[1] - j0[1]) & 0xFFFF
        print("rdgwin: recompiler %s - %d blocks translated (%d bytes each), %d flushes in the span"
              % ("on" if j1[2] else "off", nb, ((j1[3] - j0[3]) & 0xFFFF) * 16 // max(nb, 1),
                 (j1[0] - j0[0]) & 0xFFFF))
        sb = b.memr(view*16 + 0x4812, 2)                        # S_JITSEGB, S_JITSW
        if int.from_bytes(sb, "little"):
            print("rdgwin: a second cache segment (REALDMG-SPEC 16.2.1), %d moves into it "
                  "so far" % int.from_bytes(b.memr(view*16 + 0x4816, 2), "little"))
        if o.static is not None:
            fx = b.memr(view*16 + 0x40F6, 1)[0]                  # S_JITFIX
            npo = bss_off("rw_npre")
            npre = int.from_bytes(b.memr(seg*16 + npo, 2), "little") if npo else -1
            print("rdgwin: Static - %d blocks preloaded, table %s" % (
                npre, "fixed" if fx else "NOT fixed"))
        bk = bankstat(b, seg)
        print("rdgwin: cartridge %s" % ("in one claim" if bk is None else
              "in the bank cache, %d slots, %d banks fetched" % bk))
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
