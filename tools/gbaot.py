#!/usr/bin/env python3
"""RealDMG's FULL static recompiler, on the host (docs/plans/REALDMG-AOT-PLAN.md).

    python3 tools/gbaot.py cover GAME.GB              what the walk finds, no play
    python3 tools/gbaot.py build GAME.GB -o DIR       translate the whole cartridge
    python3 tools/gbaot.py check GAME.GB DIR [-n N]   run it beside the interpreter
    python3 tools/gbaot.py heat GAME.GB DIR -o H.json  count every block's entries
    python3 tools/gbaot.py package GAME.GB DIR -o GAME.O88
    python3 tools/gbaot.py make GAME.GB -o GAME.O88   all of it, with the counters

A Game Boy cartridge's code is found by WALKING it (no play, no profile):
from 0100h, the RST and interrupt vectors, every direct JR, JP, CALL and RST,
the jump tables behind RST 28h-style dispatchers and behind the
`LD HL,table / ADD HL,BC / LD A,(HL+) / LD H,(HL) / LD L,A / JP HL` idiom,
and the bank a constant wrote to the MBC before a call into 4000-7FFF
(tools/gbstatic.py's discover). Every place a block can start is then
TRANSLATED by RealDMG's own translator - rdgjit.inc, assembled with rdgsys.inc
into tools/gbaot/aothost.asm and run under an 8086 emulator (Unicorn) - so a
block is byte for byte what the machine would make, and every gate on the
recompiler is a gate on this.

`check` is the equivalence instrument: the same cartridge run under the
interpreter and on the precompiled code, the same buttons pressed (gbprof's
platformer script), and every frame's Game Boy state - 8000-FFFF, the
registers, the time, the events and the M-cycles skipped - compared. Any
difference is a defect in the precompiled code, and the frame it appears in
says where to look.

Needs `pip install unicorn` and nasm (for the driver) - or, in the bundle
`make gbaotbundle` writes (tools/gbaotbundle.py), the binaries prebuilt in
gbaot-bin/ beside this file and no nasm. No ROM is ever committed; the
games are the user's.
"""
import argparse
import collections
import hashlib
import json
import os
import struct
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gbstatic as G  # noqa: E402

# THE BUNDLE (tools/gbaotbundle.py): no os8088 tree, the binaries prebuilt
# - and the choice is made on the FILES, not on a folder's name: a stray
# apps/realdmg/ beside a bundle (RealDMG's own folder off a disk) once sent
# the bundle off to regenerate a core whose generator it does not carry, and
# the only thing the window said was "rdggen.py returned non-zero exit status 2"
PREBUILT = os.path.join(HERE, "gbaot-bin")
PREBUILT_FILES = ("aothost.bin", "rdgcore.bin", "rdgtmpl.bin", "rdgcore.inc", "rdgdefs.inc",
                  "realdmgaot.bin", "realdmgaotmega.bin", "rdgjitpaot.bin")
TREE_FILES = [os.path.join("apps", "realdmg", f) for f in
              ("rdggen.py", "rdgtmpl.py", "rdgdefs.inc", "rdgsys.inc", "rdgjit.inc",
               "rdgaot.inc", "realdmg.asm")] + [os.path.join("tools", "gbaot", "aothost.asm")]
TREE_MISSING = [f for f in TREE_FILES if not os.path.isfile(os.path.join(ROOT, f))]
PREBUILT_MISSING = [f for f in PREBUILT_FILES if not os.path.isfile(os.path.join(PREBUILT, f))]
BUNDLED = bool(TREE_MISSING) and not PREBUILT_MISSING


def need_sources():
    """Neither a whole tree nor a whole bundle: say which files are missing,
    rather than let a generator or an assembler fail on one of them"""
    if not TREE_MISSING:
        return
    raise SystemExit(
        "gbaot: this is neither an os8088 tree nor the precompiler bundle.\n"
        "  The tree at %s is missing: %s\n"
        "  The bundle's prebuilt folder %s is %s.\n"
        "  Run it from a whole os8088 checkout (with nasm on the PATH), or from the\n"
        "  GBSTATIC folder of the tools zip (`make gbaotbundle`), which needs no nasm." % (
            ROOT, ", ".join(TREE_MISSING), PREBUILT,
            "missing: " + ", ".join(PREBUILT_MISSING) if os.path.isdir(PREBUILT) else "absent"))


def run(cmd, what):
    """A build step, its failure REPORTED: the window shows no console, so the
    command's own words are what goes into the SystemExit"""
    try:
        r = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError:
        if cmd[0] == "nasm":
            raise SystemExit(
                "gbaot: %s needs nasm, and there is none on the PATH.\n"
                "  Install it (https://nasm.us; on Windows put nasm.exe's folder on the PATH),\n"
                "  or use the precompiler bundle (`make gbaotbundle`, GBSTATIC in the tools\n"
                "  zip), which carries these binaries prebuilt." % what)
        raise SystemExit("gbaot: %s: cannot run %s" % (what, cmd[0]))
    if r.returncode:
        out = (r.stdout + r.stderr).strip().splitlines()
        raise SystemExit("gbaot: %s failed (exit %d):\n  %s\n%s" % (
            what, r.returncode, " ".join('"%s"' % c if " " in c else c for c in cmd),
            "\n".join("  " + ln for ln in out[-20:]) or "  (it printed nothing)"))

# --- the walk ---------------------------------------------------------------


def jump_tables(rom, found):
    """The JP HL dispatch idiom's tables, read at their constant base: every
    word while it is a code address in the same region, until the code the
    table points at begins. Returns {(bank, pc)} of targets."""
    out = set()
    for (b, pc), op in found.items():
        if op != 0xE9:
            continue
        if [rom.byte(b, pc - i) for i in (3, 2, 1)] != [0x2A, 0x66, 0x6F]:
            continue                                  # LD A,(HL+) / LD H,(HL) / LD L,A
        base = None
        for back in range(4, 14):
            if rom.byte(b, pc - back) == 0x21:        # LD HL,nn
                base = rom.byte(b, pc - back + 1) | rom.byte(b, pc - back + 2) << 8
                break
        if base is None:
            continue
        lo, hi = (0x4000, 0x8000) if pc >= 0x4000 else (0x0150, 0x4000)
        t, first = base, 0x10000
        for _ in range(128):
            if t >= first:
                break
            w = rom.byte(b, t) | rom.byte(b, t + 1) << 8
            if not (lo <= w < hi) and not (pc >= 0x4000 and 0x150 <= w < 0x4000):
                break
            k = (b if w >= 0x4000 else 0, w)
            if rom.byte(*k) in G.INVALID:
                break
            out.add(k)
            if w > base:
                first = min(first, w)
            t += 2
    return out


WALK_STARTS = 8192                 # block starts a walk may fan out to


def walk(rom, seeds=()):
    """The cartridge's code: (found {(bank, pc): op}, extra seeds used).

    A call from bank 0 into 4000-7FFF that no constant decides the bank of is
    walked in EVERY bank - right for a cartridge of a few banks, and on a
    big one it walks data as code across the whole ROM: Pokemon Crystal
    33,256 block starts, Wario Land II 47,441, nearly all of them not code.
    So when every bank gives more than WALK_STARTS, it is walked in the banks
    some constant bank write names instead, and the heat run's misses -
    what the game really ran - are walked from (REALDMG-SPEC 20.2)."""
    def go(fan):
        extra = set(seeds)
        for _ in range(6):
            found, _, _, _ = G.discover(rom, extra, True, fanout=fan)
            new = (jump_tables(rom, found) | G.discover.seeds) - extra
            if not new:
                break
            extra |= new
        return found, extra
    found, extra = go(None)
    if len(leaders(rom, found, extra)) <= WALK_STARTS:
        return found, extra
    f0, _ = go([])
    return go(sorted({b for b, pc in f0 if pc >= 0x4000}))


def leaders(rom, found, extra, side=False):
    """Every place a block can start: the entries, every branch target, the
    instruction after a conditional transfer or a CALL/RST (a return lands
    there), the jump tables' words, and the instruction after a store that
    may take an interrupt or switch the bank (the IF/IE write and the guard
    leave there, REALDMG-SPEC 16.5) and after EI."""
    lead = {(0, 0x100)} | {(0, v) for v in range(0, 0x68, 8)} | set(extra)
    for ents in getattr(G.discover, "jtabs", {}).values():   # an RST's or a
        lead |= {(b if pc >= 0x4000 else 0, pc) for b, pc in ents}   # CALL's table
    for (b, pc), op in found.items():
        a1 = rom.byte(b, pc + 1)
        imm = a1 | rom.byte(b, pc + 2) << 8
        nxt = pc + G.LEN[op]

        def kb(t):
            if t < 0x4000:
                return (0, t)
            return (b if pc >= 0x4000 else None, t)
        if op in G.JR:
            lead.add(kb(nxt + (a1 - 256 if a1 >= 128 else a1)))
        if op in {0xC3} | G.JPCC | {0xCD} | G.CALLCC:
            lead.add(kb(imm))
        if op in G.RSTS:
            lead.add((0, op & 0x38))
        nk = (b if nxt >= 0x4000 else 0, nxt)
        if op in G.CT and op not in (0x18, 0xC3, 0xC9, 0xD9, 0xE9):
            lead.add(nk)
        st = op in (0x02, 0x12, 0x22, 0x32, 0x77, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75,
                    0x36, 0x34, 0x35, 0xE2, 0x08, 0xFB)
        st |= op == 0xEA and (imm < 0x8000 or imm >= 0xFF00)
        st |= op == 0xE0 and (a1 < 0x80 or a1 == 0xFF)
        st |= op == 0xCB and (a1 & 7) == 6 and not (0x40 <= a1 < 0x80)
        if st and side:
            lead.add(nk)
    out = set()
    for b, pc in lead:
        if b is None:                    # bank 0 into 4000-7FFF of no known bank:
            for bb in range(1, rom.banks):   # every bank the walk found it in
                if (bb, pc) in found and fits(found, bb, pc):
                    out.add((bb, pc))
        elif (b, pc) in found and pc < 0x8000 and fits(found, b, pc):
            out.add((b, pc))
    return out


def fits(found, b, pc):
    """the block's first instruction inside its own region: one whose bytes
    run past 3FFFh or 7FFFh is the interpreter's. rt_translate copies the
    region's bytes only, so it emits NO instruction there and a fall exit to
    its own PC, which the host prelinks to itself - Looney Tunes' JP NZ at
    3FFE, whose high byte is the switched-in bank's, put there by a miss,
    spun the machine for ever (REALDMG-SPEC 20.3)"""
    op = found[(b, pc)]
    return pc + (2 if op == 0xCB else G.LEN[op]) <= (0x4000 if pc < 0x4000 else 0x8000)


# --- the machine ------------------------------------------------------------

P_VIEW, P_CORE, P_ROM, P_NBANKS, P_HASH, P_PRF, P_PRFMASK, P_SEGA, P_SEGB = (
    0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E, 0x20)
P_MODE, P_HMASK, P_NOC, P_REASON, P_OUT, P_BUDGET, P_BANK, P_BMP = (
    0x22, 0x23, 0x24, 0x25, 0x26, 0x28, 0x2A, 0x2C)
E_INIT, E_PRELOAD, E_ROUND, E_FRAME, E_CONT = 0, 3, 6, 9, 12
P_AOTINT, P_STLOAD = 0x36, 0x38
STK_SEG, DRV_SEG = 0x0050, 0x0560      # the stack 0:54F0 down, aothost.bin
STK_TOP = 0x4FF0                          #   up to the core at 1000:0 (CORE in
DRV_MAX = 0x10000 - DRV_SEG * 16         #   translate and run_arm): 43,520 bytes.
                                          #   At 0900 it was 28,672, aothost had
                                          #   grown to 28,686, and the core's
                                          #   first 14 bytes were h_jitstub's
                                          #   last; at 0860, 31,232, and the CB
                                          #   group's dead flags took it 74 over
MEM_END = 0x10FFF0                       # what real mode can address, with A20
TOKEN = 0xF000                            # a block table's segment, as written:
                                          #   TOKEN + n names the image's segment n

INC = PREBUILT if BUNDLED else os.path.join(ROOT, "apps", "realdmg")


def symbols(build):
    v = {}
    for ln in open(os.path.join(aotdir(build), "rdgcore.inc")):
        p = ln.split()
        if len(p) == 3 and p[1] == "equ":
            v[p[0]] = int(p[2], 0)
    for ln in open(os.path.join(INC, "rdgdefs.inc")):
        p = ln.split(";")[0].split()
        if len(p) == 3 and p[1] == "equ":
            try:
                v[p[0]] = eval(p[2].replace("RDG_STATE", str(0x4000)), {}, v)
            except Exception:
                pass
    return v


def aotdir(build):
    return PREBUILT if BUNDLED else os.path.join(build, "gbaot")


def prerequisites(build, defines=()):
    """The PRECOMPILED core (rdggen.py's, assembled with RDG_AOTCORE: a fixed
    table's miss goes to the host's sorted list first, REALDMG-SPEC 20.9),
    its templates and offsets, and aothost.bin over them - all in
    build/gbaot/, rebuilt when a source moved. With `defines` (an
    instrument's: RT_DEADBUG, the negative control) a driver of its own."""
    d = aotdir(build)
    if BUNDLED:
        if defines:
            raise SystemExit("gbaot: the bundle has no driver built with %s" % ",".join(defines))
        return os.path.join(d, "aothost.bin")
    need_sources()
    os.makedirs(d, exist_ok=True)
    gen = os.path.join(INC, "rdggen.py")
    tmpl = os.path.join(INC, "rdgtmpl.py")
    asm, core = os.path.join(d, "rdgcore.asm"), os.path.join(d, "rdgcore.bin")
    tbin, tinc = os.path.join(d, "rdgtmpl.bin"), os.path.join(d, "rdgcore.inc")

    def stale(out, *deps):
        return not os.path.exists(out) or any(os.path.getmtime(x) > os.path.getmtime(out) for x in deps)
    defs = os.path.join(INC, "rdgdefs.inc")
    if stale(asm, gen, defs):
        run([sys.executable, gen, asm], "generating RealDMG's core (rdggen.py)")
    if stale(core, asm, defs):
        run(["nasm", "-f", "bin", "-w+error", "-DRDG_AOTCORE", "-I", INC + "/",
             "-o", core, asm], "assembling the core")
    if stale(tbin, core, tmpl, gen) or stale(tinc, core, tmpl, gen):
        run([sys.executable, tmpl, core, tbin, tinc], "cutting the templates (rdgtmpl.py)")
    drv = os.path.join(d, "aothost%s.bin" % "".join("-" + x for x in defines))
    src = os.path.join(ROOT, "tools", "gbaot", "aothost.asm")
    deps = [src, tbin, tinc] + [os.path.join(INC, f) for f in
                                ("rdgsys.inc", "rdgjit.inc", "rdgdefs.inc", "rdgaot.inc")]
    if stale(drv, *deps):
        run(["nasm", "-f", "bin", "-w+error", "-I", INC + "/", "-I", d + "/"]
            + ["-D" + x for x in defines] + ["-o", drv, src], "assembling the host driver")
    return drv


class Machine:
    """An 8086 under Unicorn with aothost.bin in it."""

    def __init__(self, build, rom, defines=()):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR
        from unicorn import x86_const as X
        self.X = X
        drv = open(prerequisites(build, defines), "rb").read()
        if len(drv) > DRV_MAX:              # (the core is written over it later)
            raise SystemExit("gbaot: aothost.bin is %d bytes, %d over the room under "
                             "the core" % (len(drv), len(drv) - DRV_MAX))
        self.sym = symbols(build)
        self.rom = rom
        self.u = Uc(UC_ARCH_X86, UC_MODE_16)
        self.u.mem_map(0, 0x110000)
        self.u.mem_write(DRV_SEG * 16, drv)
        self.core_bin = open(os.path.join(aotdir(build), "rdgcore.bin"), "rb").read()
        self.u.hook_add(UC_HOOK_INTR, self._intr)
        self.slots = []                   # (segment of the 16KB slot, bank or None)
        self.prefer = None                # the banks rdg_init's fill maps first
        self.lru = collections.OrderedDict()

    # memory
    def w(self, seg, off, data):
        self.u.mem_write(seg * 16 + off, bytes(data))

    def r(self, seg, off, n):
        return bytes(self.u.mem_read(seg * 16 + off, n))

    def w16(self, seg, off, v):
        self.w(seg, off, struct.pack("<H", v & 0xFFFF))

    def r16(self, seg, off):
        return struct.unpack("<H", self.r(seg, off, 2))[0]

    def p(self, off, v, size=2):
        self.w(DRV_SEG, off, struct.pack("<H" if size == 2 else "<B", v))

    def pr(self, off, size=2):
        d = self.r(DRV_SEG, off, size)
        return struct.unpack("<H" if size == 2 else "<B", d)[0]

    # the INT 3 stops
    def _intr(self, uc, intno, ud):
        if intno == 3:
            self.stopped = True
            uc.emu_stop()
        else:
            raise RuntimeError("interrupt %d at %04X:%04X" % (
                intno, uc.reg_read(self.X.UC_X86_REG_CS), uc.reg_read(self.X.UC_X86_REG_IP)))

    def enter(self, entry):
        X = self.X
        self.u.reg_write(X.UC_X86_REG_SS, STK_SEG)
        self.u.reg_write(X.UC_X86_REG_SP, STK_TOP)
        self.u.reg_write(X.UC_X86_REG_DS, DRV_SEG)
        self.u.reg_write(X.UC_X86_REG_ES, DRV_SEG)
        self.u.reg_write(X.UC_X86_REG_CS, DRV_SEG)
        self.u.reg_write(X.UC_X86_REG_IP, entry)
        self.run()

    def run(self):
        X = self.X
        while True:
            self.stopped = False
            cs = self.u.reg_read(X.UC_X86_REG_CS)
            ip = self.u.reg_read(X.UC_X86_REG_IP)
            self.u.emu_start(cs * 16 + ip, MEM_END + 0xF, timeout=int(os.environ.get("GBAOT_EMUTO", "60")) * 1000000)
            if not self.stopped:
                raise RuntimeError("the machine stopped at %04X:%04X" % (
                    self.u.reg_read(X.UC_X86_REG_CS), self.u.reg_read(X.UC_X86_REG_IP)))
            if self.pr(P_REASON, 1) == 1:
                self.bank_service()
                continue
            return

    # the ROM: bank 0 at p_rom, the rest in 16KB slots the host fills
    def set_slots(self, first_seg, last_seg):
        # a bank is addressed as S_ROMX:4000h with S_ROMX = slot - 400h, a
        # 16-bit segment: no slot may begin past 103FFh
        self.slots = [[s, None] for s in range(first_seg, min(last_seg - 0x3FF, 0x10400), 0x400)]

    def bank_service(self):
        view = self.view
        want = self.pr(P_BANK)
        if want == 0xFFFF:                # rdg_init's fill: what fits, the
            order = self.prefer or range(1, self.rom.banks)   # host's choice first
            for n in list(order)[:len(self.slots)]:
                self.map_bank(n)
            return
        self.map_bank(want // 2)

    def map_bank(self, n):
        view = self.view
        if n in self.lru:
            self.lru.move_to_end(n)
            return
        free = [s for s in self.slots if s[1] is None]
        if free:
            slot = free[0]
        else:                             # rwbank.inc's rw_bfetch: the slot
            now = self.r16(view, self.sym["S_FRAMES"])   # whose bank was
            use = self.sym["RDG_BUSE"]    #   switched to longest ago, by
            slot = max(self.slots, key=lambda sl: (   # frame, the first of equals
                (now - self.r16(view, use + 2 * sl[1])) & 0xFFFF, -self.slots.index(sl)))
            old = slot[1]
            del self.lru[old]
            self.w16(view, self.sym["RDG_BTAB"] + 2 * old, 0)
        slot[1] = n
        self.lru[n] = slot
        self.fetches = getattr(self, "fetches", 0) + 1   # (a bank brought in)
        self.w(slot[0], 0, self.rom.d[n * 0x4000:(n + 1) * 0x4000].ljust(0x4000, b"\xFF"))
        self.w16(view, self.sym["RDG_BTAB"] + 2 * n, slot[0] - 0x400)

    def init(self, mode, view, core, hash_, sega, segb, rom_seg, prf=0, prfmask=0, hmask=31,
             noc=0, core_image=None):
        self.view = view
        self.w(core, 0, core_image if core_image is not None else self.core_bin)
        self.w(rom_seg, 0, self.rom.d[:0x4000])
        self.p(P_VIEW, view)
        self.p(P_CORE, core)
        self.p(P_ROM, rom_seg)
        self.p(P_NBANKS, max(2, self.rom.banks))
        self.p(P_HASH, hash_)
        self.p(P_PRF, prf)
        self.p(P_PRFMASK, prfmask)
        self.p(P_SEGA, sega)
        self.p(P_SEGB, segb)
        self.p(P_MODE, mode, 1)
        self.p(P_HMASK, hmask, 1)
        self.p(P_NOC, noc, 1)
        self.lru.clear()
        for s in self.slots:
            s[1] = None
        self.enter(E_INIT)

    def frame(self, joy, budget=4000):
        self.w(self.view, self.sym["S_JOY"], [joy])
        self.p(P_BUDGET, budget)
        self.enter(E_FRAME)
        return self.pr(P_OUT)

    def serial(self):
        n = self.pr(0x2E)
        buf = self.r(DRV_SEG, self.pr(0x30), 1024)
        if n <= 1024:
            return buf[:n]
        k = n % 1024
        return buf[k:] + buf[:k]

    def state(self):
        """What two runs must agree on, frame by frame."""
        v, s = self.view, self.sym
        regs = self.r(v, s["S_AF"], 12)              # AF BC DE HL SP PC
        bp = struct.unpack("<h", self.r(v, s["S_BP"], 2))[0]   # "now", what the
        tn = struct.unpack("<I", self.r(v, s["S_TNEXT"], 4))[0]   #   game sees: where a
        timing = struct.pack("<I", (tn - bp) & 0xFFFFFFFF)    #   budget ended is not
        stats = self.r(v, s["S_SKIPPED"], 4) + self.r(v, s["S_EVCOUNT"], 2)
        mem = self.r(v, 0x8000, 0x8000)
        return regs, timing, stats, hashlib.sha1(mem).hexdigest()


def joy_script(f):
    """gbprof's platformer script, as tests/rdgcore/rdghost.asm's joy_script
    (S_JOY: Rt Lf Up Dn A B Se St from bit 0)."""
    j = 0x01
    if 150 <= f < 156 or 400 <= f < 406 or 700 <= f < 706:
        j |= 0x80
    if f % 50 < 14:
        j |= 0x10
    if f % 400 > 370:
        j |= 0x20
    return j


# --- translate --------------------------------------------------------------

def slot_of(pc, key, hmask):
    """tr_ekey's slot for a PC and its bank key (rt_preone), x 8."""
    bx = pc
    bl, bh = bx & 0xFF, bx >> 8
    bl ^= key
    bl ^= bh >> 3
    bh &= hmask
    return ((bh << 8) | bl) * 8


NEAR_DEPTH = 1                     # near_heat's control transfers (3 measured worse)


def near_heat(rom, found, entries, heat, depth=NEAR_DEPTH):
    """The uncounted block starts in tiers by how many control transfers
    away from the counted code they are - a jump, a call, a return or a
    jump table's entry each 1, running on into the next instruction 0
    (gbstatic.supergraph's edges; the last walk's tables). A list of sets of
    (pc, key), nearest first. Images fill in this order after the counted
    blocks, where they filled bank by bank: Link's Awakening's entity
    handlers are one table entry from its hot dispatch at 3952h and were
    dropped behind the low banks' cold code"""
    import collections as C
    jt = getattr(G.discover, "jtabs", {})
    succ, _ = G.supergraph(rom, found, jt)
    dist, dq = {}, C.deque()
    for pc, key in heat or {}:
        k = (0 if pc < 0x4000 else key, pc)
        if k in found and k not in dist:
            dist[k] = 0
            dq.append(k)
    while dq:
        k = dq.popleft()
        d, op = dist[k], found[k]
        nx = k[1] + (2 if op == 0xCB else G.LEN[op])
        out = [t for t in succ.get(k, ()) if t not in ("RET", None)]
        if (op == 0xCD or op in G.RSTS) and k not in jt:   # a call goes on
            out.append((k[0] if nx >= 0x4000 else 0, nx))   #   where it returns
        for t in out:                      # (not RET's edges: they are every
            for u in (t,):                 #   caller of a shared routine)
                if u not in found:
                    continue
                w = 0 if u[1] == nx and u[0] == (k[0] if nx >= 0x4000 else 0) else 1
                if d + w > depth or dist.get(u, depth + 1) <= d + w:
                    continue
                dist[u] = d + w
                (dq.appendleft if w == 0 else dq.append)(u)
    tiers = [set() for _ in range(depth)]
    for b, pc in entries:
        d = dist.get((b if pc >= 0x4000 else 0, pc))
        k = (pc, 0xFF if pc < 0x4000 else b)
        if d and (heat or {}).get(k, 0) == 0:
            tiers[d - 1].add(k)
    return tiers


def translate(build, rom, entries, hmask=31, log=print, defines=(), rhm=7, heat=None,
              hram=None, squeeze=0, listall=False, near=None, images=None):
    """Every entry (bank, pc) translated, in rounds of two cache segments
    (the first round also fills the core's holes), the code's banks mapped a
    group at a time when they do not all fit. Returns the image.

    With `heat` ({(pc, key): entries}, heat_run's) the hottest blocks are
    translated first - the first images hold them, and those are what a
    small heap keeps - and claim the table first (REALDMG-SPEC 20.11).

    With `hram` ({pc: HRAM's 128 bytes}, load_heat's) those PCs are made
    blocks too, from HRAM as the hottest of them found it: each one GUARDED,
    its bytes compared with HRAM as it is at every entry and the interpreter
    taking it when they differ (REALDMG-SPEC 20.5)."""
    if squeeze:                            # A TEST'S: every image's code cut
        defines = tuple(defines) + ("RT_SQUEEZE",)  # to its first gap's first
    m = Machine(build, rom, defines)       #   `squeeze` bytes and the core's
    s = m.sym                              #   holes to RT_SQHOLES, so a small
    CORE, VIEW, HASH, PRF, SEGA, SEGB, ROMSEG = 0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000, 0x7000
    BMP = 0x4800                           # the profile's segment's second half
    m.set_slots(0x7400, MEM_END // 16)
    m.prefer = sorted({b for b, pc in entries if pc >= 0x4000})
    keyed = [(pc, 0xFF if pc < 0x4000 else b) for b, pc in sorted(entries)]
    keyed += [(pc, 0xFF) for pc in sorted(hram or {})]
    n = len(keyed)
    mask = 1
    while mask < n and mask < 8192:        # (the profile holds 8,192: more
        mask <<= 1                         #   are offered a round at a time)
    import math

    def weight(k):                         # rt_preload's order: heaviest first
        n = (heat or {}).get(k, 0)
        return min(255, 2 + int(math.log2(n) * 12)) if n else 1

    def bitmap(banks):
        """The block starts as a bitmap, so a block ends where another
        begins: bank 0's region and each of `banks` (14 at most)"""
        bmp = bytearray(0x100 + 0x800 * (1 + len(banks)))
        for i, k in enumerate(banks):
            bmp[k] = i + 1
        for pc, key in keyed:
            if pc >= 0x8000 or (key != 0xFF and not bmp[key]):
                continue                   # (HRAM's: no bitmap, rt_aotstop's)
            base = 0x100 if key == 0xFF else 0x100 + 0x800 * bmp[key]
            o = pc & 0x3FFF
            bmp[base + (o >> 3)] |= 1 << (o & 7)
        m.w(BMP, 0, bmp.ljust(0x8000, b"\0"))
    m.p(P_BMP, BMP)
    m.init(2, VIEW, CORE, HASH, SEGA, SEGB, ROMSEG, prf=PRF, prfmask=mask - 1, hmask=hmask, noc=0)
    if squeeze:                            #   ROM fills many images, with links
        m.w16(VIEW, S_JITLIM, squeeze)     #   and a list between them
    if hram:                               # HRAM as the hottest routine's miss
        top = max(hram, key=lambda pc: ((heat or {}).get((pc, 0xFF), 0), -pc))
        m.w(VIEW, 0xFF80, hram[top])
    # the code's banks in groups that fit beside the translator: each group
    # mapped in turn, and only its entries (and bank 0's) offered
    width = min(len(m.slots), 14)          # (0x100 + 0x800 x 15 fits the 32KB)
    groups = [m.prefer[i:i + width] for i in range(0, len(m.prefer), width)] or [[]]
    segs = [None, None, None]              # the images, in order: 0 is the core
    live = {CORE: 0, SEGA: 1, SEGB: 2}     # the segments being written: image n
    blocks = {}                            # (pc, key) -> (image segment n, offset)
    dropped = set()
    rnd, cont, cleared = 0, False, False
    # With counters, every group is gone through TWICE: first for the blocks
    # the counters saw run, then for the rest. Once was in bank order, so the
    # images filled on the low banks' cold code and a high bank's hot code was
    # dropped whole - Link's Awakening's music engine in banks 1E and 1F, 37%
    # of the block entries of the owner's replay, and all of bank 1B
    # ...and the counted ones in two tiers: the blocks that hold 90% of the
    # entries before the rest, so the hottest code of every group lands in the
    # first images together - a call between two hot blocks in different
    # images is a far jump, which the Victory 286 prices at ~2.9 us
    # (cpubench) and Link's Awakening's replay ran 49 of a frame. OPT-IN
    # (GBAOT_TIER=1): measured on the four replays it took Link's Awakening
    # 60 -> 81 far transfers a frame and Kirby 31 -> 30, because a cold block
    # that a hot one exits to is then laid in a later image
    counted = sorted((k for k in keyed if (heat or {}).get(k, 0)), key=lambda k: -heat[k])
    tiers = []
    if counted:
        tot, run, top = sum(heat[k] for k in counted), 0, set()
        for k in counted:
            if run >= 0.9 * tot:
                break
            top.add(k)
            run += heat[k]
        tiers = [top, set(counted) - top] if os.environ.get("GBAOT_TIER") else [set(counted)]
    for t in near or ():                   # ...then what is NEAR the counted
        t = set(t) - set(counted)          #   code (near_heat), nearest first
        if t:
            tiers.append(t)
    passes = [(g, t) for t in (tiers + [None] if tiers else [None]) for g in groups]
    every = set().union(*tiers) if tiers else set()
    for gi, (group, tier) in enumerate(passes):
        if gi:
            for sl in m.slots:             # the last group's banks out
                if sl[1] is not None:
                    m.w16(VIEW, s["RDG_BTAB"] + 2 * sl[1], 0)
                    sl[1] = None
            m.lru.clear()
            for k in group:
                m.map_bank(k)
        gset = set(group)
        bitmap(group)
        todo = {k: None for k in keyed if ((k[1] == 0xFF and group is groups[0]) or k[1] in gset)
                and (k in tier if tier is not None else k not in every)}
        while todo:
            prf = bytearray(mask * 4)
            offered = sorted(todo, key=lambda k: -weight(k))[:mask]
            for i, (pc, key) in enumerate(offered):
                struct.pack_into("<HBB", prf, i * 4, pc, key, weight((pc, key)))
            m.w(PRF, 0, prf)
            fresh = False
            if cont:
                m.enter(E_CONT)            # on where the last round stopped
            elif rnd:
                if len(segs) + 1 > (images or HOST_MAX - 1) + 1:   # the images are
                    dropped |= set(todo)       #   full: the coldest are interpreted
                    break                      #   (`images`, at most AOT_MAX: past
                                               #   HOST_MAX - 1 the host runs only
                                               #   that many, degrade's way)
                m.w(SEGA, 0, bytes(0x10000))
                m.w(SEGB, 0, bytes(0x10000))
                m.enter(E_ROUND)
                live = {SEGA: len(segs), SEGB: len(segs) + 1}
                segs += [None, None]
                fresh = True
            m.enter(E_PRELOAD)
            # what this round made: its entries out of the profile, its
            # segments named by token in the table, the images read out
            table = bytearray(m.r(HASH, 0, (hmask + 1) * 256 * 8))
            got = 0
            for e in range(0, len(table), 8):
                pc, key, cnt, off, seg = struct.unpack_from("<HBBHH", table, e)
                if off and seg in live:
                    struct.pack_into("<H", table, e + 6, TOKEN + live[seg])
                    blocks[(pc, key)] = (live[seg], off)
                    todo.pop((pc, key), None)
                    got += 1
            m.w(HASH, 0, table)
            for sg, idx in live.items():
                segs[idx] = m.r(sg, 0, 0x10000)
            log("gbaot: round %d: %d blocks translated (%d left)" % (rnd, got, len(todo)))
            rnd += 1
            if not todo:
                cleared = False
                cont = True                # room left: the next group goes on here
                continue
            # what is left went unmade for one of two reasons: the segments
            # filled, or its slot and the slot's neighbour hold blocks. The
            # table is only the translator's bookkeeping here - the host keeps
            # every block it made and builds the machine's table itself
            # (finish) - so a collision empties it and the round goes again
            # in the same segments; a full cache takes fresh ones
            def free(k):
                e = slot_of(k[0], k[1], hmask)
                return not (struct.unpack_from("<H", table, e + 4)[0] and
                            struct.unpack_from("<H", table, (e ^ 8) + 4)[0])
            if len(offered) < len(todo) and got:   # (more to offer: on, here)
                cleared, cont = False, True
                continue
            if any(free(k) for k in todo):
                cleared, cont = False, False   # full: fresh segments
                if not got and fresh:
                    dropped |= set(todo)   # (fresh and still nothing: should not)
                    break
                continue
            if cleared and not got:
                dropped |= set(todo)       # (should not happen)
                break
            m.w(HASH, 0, bytes((hmask + 1) * 256 * 8))
            cleared, cont = True, True
    used_imgs = {n for n, _ in blocks.values()} | {0}
    while segs and (segs[-1] is None or len(segs) - 1 not in used_imgs):
        segs.pop()
    tails = {p << 8 for p in tailpages(build)}
    used = 0
    core0 = m.core_bin
    for i, sg in enumerate(segs):          # code bytes: in the core what is
        if i == 0:                         #   not its own any more, elsewhere
            used += sum(1 for a, b in zip(sg, core0) if a != b)   # what is
            continue                       #   not zero outside the tails
        for o in range(0, 0x10000, 256):
            if o in tails or o >= 0xFE00:
                continue
            used += sum(1 for x in sg[o:o + 256] if x)
    img = {"segs": segs, "blocks": blocks, "dropped": sorted(dropped), "entries": n,
           "code_bytes": used}
    finish(img, m.sym, rom, keyed, rhm, heat, listall)
    return img


def priorities(rom, keyed, heat=None):
    """The order block starts claim the machine's table in: where a lookup
    is likeliest to come from first - the vectors, then the places a RET
    lands (the instruction after a CALL or RST), then the jump tables'
    words, then the rest - and what does not fit goes to the sorted list"""
    def rank(k):
        pc, key = k
        b = 0 if key == 0xFF else key
        if pc < 0x68 and pc % 8 == 0:
            return 0
        prev3 = rom.byte(b, pc - 3)
        prev1 = rom.byte(b, pc - 1)
        if prev3 == 0xCD or prev3 in G.CALLCC or prev1 in G.RSTS:
            return 1
        return 2
    heat = heat or {}
    return sorted(keyed, key=lambda k: (-heat.get(k, 0), rank(k), k[1], k[0]))


def finish(img, sym, rom, keyed, rhm, heat=None, listall=False):
    """The machine's half of an image, made by the host:
      - the TABLE it runs with, (rhm + 1) x 256 entries, every block start
        placed by priority in its slot or the slot's neighbour (tr_ekey's
        two places), the rest in the LIST, sorted (rdgaot.inc);
      - every segment's three table masks (tr_hm0-2, rt_init's) made rhm;
      - every exit PRE-LINKED, as tr_link would on its first run - which
        also means a block the table could not hold costs a search only
        where the machine dispatches for itself (a RET, JP HL, a resume);
      - the far jumps among them as RELOCATIONS, image and offset, for the
        loader to give the segment;
      - one generation in every segment's FFF0h (16.2.3).
    With `listall` the table is left empty and every block is listed - a
    test's, so that a small ROM reaches the list's search and its cache
    (rdgaot.inc) at all."""
    blocks, segs = img["blocks"], [bytearray(x) for x in img["segs"]]
    table = bytearray((rhm + 1) * 256 * 8)
    listed = []
    for pc, key in priorities(rom, [k for k in keyed if k in blocks], heat):
        n, off = blocks[(pc, key)]
        e = slot_of(pc, key, rhm)
        for e2 in () if listall else (e, e ^ 8):
            if not struct.unpack_from("<H", table, e2 + 4)[0]:
                struct.pack_into("<HBBHH", table, e2, pc, key, 0, off, TOKEN + n)
                break
        else:
            listed.append((key, pc, off, n))
    lst = bytearray()
    for key, pc, off, n in sorted(listed):
        lst += struct.pack("<HBBHH", pc, key, 0, off, TOKEN + n)
    for sg in segs:
        for name in ("RDGC_TR_HM0", "RDGC_TR_HM1", "RDGC_TR_HM2"):
            sg[sym[name]] = rhm
        struct.pack_into("<H", sg, 0xFFF0, 1)
    # pre-linking: an exit's site is `mov dx,SITE / jmp tr_lsite`, SITE its
    # own offset, then mov si,T at SITE + 6 (bank 0) or after seven spare
    # bytes at SITE + 13 (4000-7FFF) - rdgjit.inc's rt_exit
    lsite, sbank = sym["RDGC_TR_LSITE"], sym["S_BANK"]
    by_pc = collections.defaultdict(list)
    for (pc, key), v in blocks.items():
        by_pc[pc].append((key, v))
    import bisect
    starts = collections.defaultdict(list)  # image -> sorted (offset, key):
    for (pc, key), (n, off) in blocks.items():   # a site belongs to the
        starts[n].append((off, key))       #   nearest block start below it
    for v in starts.values():
        v.sort()
    relocs, linked, lazy, jns, jshort = [], 0, 0, 0, 0
    nojns = bool(os.environ.get("GBAOT_NOJNS"))   # (the A/B)
    for n, sg in enumerate(segs):
        i = sg.find(b"\xBA")
        while i != -1 and i < 0xFFF0:
            if (struct.unpack_from("<H", sg, i + 1)[0] == i and sg[i + 3] == 0xE9
                    and (i + 6 + struct.unpack_from("<h", sg, i + 4)[0]) & 0xFFFF == lsite):
                if sg[i + 6] == 0xBE:
                    t, at, bank = struct.unpack_from("<H", sg, i + 7)[0], i, None
                else:
                    t, at = struct.unpack_from("<H", sg, i + 14)[0], i + 8
                    k = bisect.bisect_right(starts[n], (i, 0x100)) - 1
                    src = starts[n][k][1] if k >= 0 else 0xFF
                    if src != 0xFF:          # a bank's code: the same bank (a
                        bank = src           #   switch leaves the block)
                    else:                    # bank 0's: the one bank that has it
                        cand = [kv for kv in by_pc.get(t, []) if kv[0] != 0xFF]
                        bank = cand[0][0] if len(cand) == 1 else -1
                tgt = None
                if t < 0x4000:
                    tgt = blocks.get((t, 0xFF))
                elif bank is not None and bank >= 0:
                    tgt = blocks.get((t, bank))
                if tgt is None:
                    lazy += 1               # left for tr_link to make at run time
                else:
                    if at != i:             # cmp byte [ss:S_BANK],B / jne +5
                        sg[i:i + 3] = b"\x36\x80\x3E"
                        struct.pack_into("<H", sg, i + 3, sbank)
                        sg[i + 5] = bank
                        sg[i + 6:i + 8] = b"\x75\x05"
                    tn, toff = tgt
                    rel = toff - at
                    if (tn == n and at == i and sg[i - 2:i] == b"\x78\x06"
                            and -128 <= rel <= 127 and not nojns):
                        sg[i - 2] = 0x79             # js +6 / jmp near T: one
                        sg[i - 1] = rel & 0xFF       #   jns T, and the event's
                        sg[i:i + 2] = b"\xEB\x04"   #   way a jmp short to the
                        jns += 1                     #   mov si,T at SITE + 6
                    elif tn == n and -128 <= toff - (at + 2) <= 127 and not nojns:
                        sg[at] = 0xEB                # a jmp short where it
                        sg[at + 1] = (toff - (at + 2)) & 0xFF   # reaches (a bank's
                        sg[at + 2] = 0x90            #   exit, after its check)
                        jshort += 1
                    elif tn == n:
                        sg[at] = 0xE9
                        struct.pack_into("<H", sg, at + 1, (toff - (at + 3)) & 0xFFFF)
                    else:
                        sg[at] = 0xEA
                        struct.pack_into("<HH", sg, at + 1, toff, TOKEN + tn)
                        relocs.append((n, i, tn, at != i))
                    linked += 1
            i = sg.find(b"\xBA", i + 1)
    # an exit to HRAM is `mov si,T / js +3 / jmp tr_enter` (rt_exit's RAM
    # arm): RAM is never linked at run time. A precompiled HRAM block is
    # GUARDED - it compares its bytes with HRAM's at every entry (REALDMG-SPEC
    # 20.5) - so where one is in the same image, the jmp is made that block's
    # own, and the lookup tr_enter would make every time is not (Looney Tunes'
    # object loop calls its HRAM thunk twenty times a frame)
    enter, hlinked = sym["RDGC_TR_ENTER"], 0
    for n, sg in enumerate(segs):
        i = sg.find(b"\xBE")
        while i != -1 and i < 0xFFF0:
            if (sg[i + 3:i + 6] == b"\x78\x03\xE9"
                    and (i + 8 + struct.unpack_from("<h", sg, i + 6)[0]) & 0xFFFF == enter):
                t = struct.unpack_from("<H", sg, i + 1)[0]
                tgt = blocks.get((t, 0xFF)) if 0xFF80 <= t < 0xFFFF else None
                if tgt is not None and tgt[0] == n:
                    rel = tgt[1] - (i + 5)
                    if -128 <= rel <= 127 and not nojns:
                        sg[i + 3] = 0x79             # js +3 / jmp near: one
                        sg[i + 4] = rel & 0xFF       #   jns, the event's way
                        sg[i + 5:i + 8] = b"\xEB\x01\x90"   # on past it
                        jns += 1
                    else:
                        struct.pack_into("<H", sg, i + 6, (tgt[1] - (i + 8)) & 0xFFFF)
                    hlinked += 1
            i = sg.find(b"\xBE", i + 1)
    img["segs"] = [bytes(x) for x in segs]
    # THE HOT IMAGES: the first so many that, with the core, hold 99% of the
    # counted entries - claimed before the ROM's bank cache, the rest after
    # it (REALDMG-SPEC 20.10). With no counters, all of them
    hot = len(segs) - 1
    if heat:
        per = collections.Counter()
        for k, n in heat.items():
            if k in blocks:
                per[blocks[k][0]] += n
        tot, cum, hot = sum(per.values()) or 1, per[0], 0
        while cum < 0.99 * tot and hot < len(segs) - 1:
            hot += 1
            cum += per[hot]
        hot = max(hot, 1) if len(segs) > 1 else 0
    img.update(table=bytes(table), list=bytes(lst), relocs=relocs, hmask=rhm,
               linked=linked, lazy=lazy, listed=len(listed), hot=hot, hlinked=hlinked,
               jns=jns, jshort=jshort)


def tailpages(build):
    t = open(os.path.join(aotdir(build), "rdgtmpl.bin"), "rb").read()
    sym = symbols(build)
    o, n = struct.unpack_from("<HH", t, sym["RDGT_HTAIL"])
    return list(t[o:o + n])


def save(img, out, rom):
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.startswith("seg") and f.endswith(".bin"):
            os.remove(os.path.join(out, f))
    for i, seg in enumerate(img["segs"]):
        open(os.path.join(out, "seg%02d.bin" % i), "wb").write(seg)
    open(os.path.join(out, "table.bin"), "wb").write(img["table"])
    open(os.path.join(out, "list.bin"), "wb").write(img["list"])
    meta = {"segs": len(img["segs"]), "hmask": img["hmask"], "entries": img["entries"],
            "blocks": len(img["blocks"]), "listed": img["listed"], "linked": img["linked"],
            "lazy": img["lazy"], "relocs": img["relocs"], "code_bytes": img["code_bytes"],
            "hot": img.get("hot", len(img["segs"]) - 1),
            "dropped": ["%02X:%04X" % (k, p) for p, k in img["dropped"]],
            "rom_sha256": hashlib.sha256(rom.d).hexdigest(), "header": rom.d[0x134:0x150].hex()}
    json.dump(meta, open(os.path.join(out, "aot.json"), "w"), indent=1)


def load(out):
    meta = json.load(open(os.path.join(out, "aot.json")))
    segs = [open(os.path.join(out, "seg%02d.bin" % i), "rb").read() for i in range(meta["segs"])]
    table = open(os.path.join(out, "table.bin"), "rb").read()
    meta["list"] = open(os.path.join(out, "list.bin"), "rb").read()
    return meta, segs, table


# --- run --------------------------------------------------------------------

def run_arm(build, rom, mode, frames, img=None, prove=False, defines=(), joy=None,
            state=None, heat=False, slots=None, lparts=None):
    """Yields each frame's state: mode 0 the interpreter, 1 the dynamic
    recompiler, 3 the precompiled code in `img` (meta, segs, table)."""
    m = Machine(build, rom, defines)
    CORE, VIEW, HASH = 0x1000, 0x2000, 0x3000
    if mode in (3, 4):
        if len(img[1]) > HOST_MAX:         # more images than this layout
            img = degrade(build, img, HOST_MAX - 1)   # holds: the rest run as the
        meta, segs, table = img                       #   package runs an image
        seg_at = [CORE] + [0x4000 + 0x1000 * i for i in range(len(segs) - 1)]
        LIST = seg_at[-1] + 0x1000 if len(segs) > 1 else 0x4000
        segs = [bytearray(x) for x in segs]
        for n, site, tn, form in meta["relocs"]:   # the far jumps' segments
            struct.pack_into("<H", segs[n], site + (8 if form else 0) + 3, seg_at[tn])
        for i in range(1, len(segs)):
            m.w(seg_at[i], 0, segs[i])

        def reseg(b):
            t = bytearray(b)
            for e in range(0, len(t), 8):
                seg = struct.unpack_from("<H", t, e + 6)[0]
                if seg >= TOKEN and struct.unpack_from("<H", t, e + 4)[0]:
                    struct.pack_into("<H", t, e + 6, seg_at[seg - TOKEN])
            return t
        t = reseg(table)
        lst = reseg(meta["list"])
        m.w(HASH, 0, t)
        sg = LIST                          # the list in its parts (20.17), as
        lx = []                            #   the package lays them out
        per = 8 * -(-len(lst) // (8 * lparts)) if lparts else LIST_PART
        for k in range(0, max(len(lst), 1), per):   # (lparts: the list cut in
            c = lst[k:k + per]                       #   that many, the search's gate)
            m.w(sg, 0, c)
            lx.append((sg, len(c) // 8))
            sg += (len(c) + 15) // 16 + 1
        if len(lx) > LIST_PARTS:
            raise SystemExit("gbaot: %d list parts; the machine searches %d" % (len(lx), LIST_PARTS))
        for k, (a, n) in enumerate(lx):
            m.p(0x40 + 4 * k, a)
            m.p(0x42 + 4 * k, n)
        m.p(0x50, len(lx), 1)
        m.p(0x32, LIST)
        m.p(0x34, lx[0][1])
        FILT = sg
        m.p(0x3A, FILT)
        romseg = FILT + 0x280              # (the filter's 8KB, and the cache's
                                           #   2KB after it: rdgaot.inc)
        free, top = romseg + 0x400, MEM_END // 16
        if mode == 4:                      # 20.16: the runtime table, ONE
            m.p(0x3C, free)                #   bank slot, then the runtime tier
            free += 0xA00                  #   last - a segment is 16 bits, and
            top = free + 0x400             #   ten images leave ~127KB above
            m.p(0x3E, top)                 #   them (p_hyseg, p_hyr0)
        m.set_slots(free, top)
        if slots:                          # a bank cache as small as a
            m.slots = m.slots[-slots:]     #   machine's (10.3.2's gate) - and
                                           #   not under bank 0's copy: a slot's
                                           #   segment less 400h there reads
                                           #   bank 0's own bytes at 0000-3FFF,
                                           #   which hid a DS left on the bank
        m.init(mode, VIEW, CORE, HASH, seg_at[1] if len(seg_at) > 1 else CORE, 0, romseg,
               hmask=meta["hmask"], core_image=bytes(segs[0]))
        if prove:                          # count what runs in the code segments:
            from unicorn import UC_HOOK_BLOCK  # the core's own handlers are
            m.ran = collections.Counter()   #   outside the holes the code is in
            code_ranges = [(sg * 16, sg * 16 + 0x10000) for sg in seg_at[1:]]
            if heat:                        # (the core's holes too: slow)
                code_ranges.insert(0, (CORE * 16, CORE * 16 + 0x10000))

            entries = {}                    # a block's entry, linear -> its key
            for tb in (t, lst):
                for e in range(0, len(tb), 8):
                    pc, key, cnt, off, seg = struct.unpack_from("<HBBHH", tb, e)
                    if off:
                        entries[seg * 16 + off] = (pc, key)

            core0, sbank, smask = CORE * 16, m.sym["S_BANK"], m.sym["S_BMASK"]
            m.interp = collections.Counter()

            def blk(uc, addr, size, ud):
                m.ran["code"] += 1
                k = entries.get(addr)
                if k:
                    m.ran[k] += 1
                elif heat and core0 <= addr < core0 + 0x10000 and not addr & 0xFF:
                    dx = uc.reg_read(m.X.UC_X86_REG_DX)    # an instruction the
                    si = uc.reg_read(m.X.UC_X86_REG_SI) - 1    # INTERPRETER ran:
                    if dx == addr - core0 and si < 0x8000:     #   its dispatch's
                        v = uc.mem_read(VIEW * 16 + si, 1)[0] if si < 0x4000 else None
                        if si >= 0x4000:   # jmp dx, DL 0 and DH the opcode at SI-1
                            b = uc.mem_read(VIEW * 16 + sbank, 1)[0] & \
                                uc.mem_read(VIEW * 16 + smask, 1)[0]
                            m.interp[(si, b)] += 1
                        elif v == dx >> 8:
                            m.interp[(si, 0xFF)] += 1
            for lo, hi in code_ranges:
                m.u.hook_add(UC_HOOK_BLOCK, blk, begin=lo, end=hi - 1)
            if heat:                        # a ROM PC with no block, to the
                from unicorn import UC_HOOK_CODE   # interpreter: the walk's misses
                X = m.X
                m.miss = collections.Counter()
                m.hram = {}                 # an HRAM PC's routine, as HRAM held it

                def miss(uc, addr, size, ud):
                    si = uc.reg_read(X.UC_X86_REG_SI)
                    dh = uc.reg_read(X.UC_X86_REG_DX) >> 8
                    m.miss[(si, dh)] += 1
                    if si >= 0xFF80 and (si, dh) not in m.hram:
                        m.hram[(si, dh)] = bytes(uc.mem_read(VIEW * 16 + 0xFF80, 0x80))
                at = DRV_SEG * 16 + m.pr(P_AOTINT)
                m.u.hook_add(UC_HOOK_CODE, miss, begin=at, end=at)
    else:
        SEGA, SEGB, romseg = 0x4000, 0x5000, 0x6000
        m.set_slots(romseg + 0x400, MEM_END // 16)
        if slots:                          # a bank cache as small as a
            m.slots = m.slots[-slots:]     #   machine's (10.3.2's gate) - and
                                           #   not under bank 0's copy: a slot's
                                           #   segment less 400h there reads
                                           #   bank 0's own bytes at 0000-3FFF,
                                           #   which hid a DS left on the bank
        m.init(mode, VIEW, CORE, HASH, SEGA, SEGB, romseg, hmask=31)
    if state is not None:                  # a save state, rwstate.inc's file
        ram = len(state) - 0x8000 - 0x800  # its cartridge RAM part: none, or
        if ram not in (0, 0x2000):         #   ONE 8KB bank, which rdgsys keeps
            raise SystemExit("gbaot: a state with more than one bank of cartridge RAM "
                             "is not taken here")   # in A000-BFFF itself
        m.w(VIEW, 0x8000, state[:0x8000])
        rec = bytearray(state[0x8000 + ram:])
        if ram:                            # the package keeps that bank in a
            m.w(VIEW, 0xA000, state[0x8000:0x8000 + ram])   # claim of its own
            rec[5] = 0                     #   and says 8 (KB) here; the harness,
        m.w(VIEW, m.sym["RDG_STREC"], bytes(rec))   # in the window, says 0
        m.enter(m.pr(P_STLOAD))
        if m.pr(P_OUT):
            raise SystemExit("gbaot: the save state is not this game's")
    for f in range(frames):
        if joy:
            x = m.frame(joy(f))
        else:
            x = m.frame(joy_script(f) if f else 0)   # (the harness's: no buttons in frame 0)
        yield f, x, m.state(), m


def replay(path):
    """(S_JOY at the start, a function frame -> buttons, frames) out of a
    NAME.RPn (rwreplay.inc): RLE pairs of buttons and frames after 16 bytes"""
    d = open(path, "rb").read()
    if d[:4] != b"RDRP" or d[4] != 1:
        raise SystemExit("gbaot: %s is not a RealDMG replay" % path)
    n = struct.unpack_from("<H", d, 14)[0]
    seq = []
    for i in range(16, 16 + n, 2):
        seq += [d[i]] * (d[i + 1] or 1)
    j0 = d[5]
    return j0, (lambda f: seq[f] if f < len(seq) else (seq[-1] if seq else j0)), len(seq)


def heat_run(build, rom, img, frames, joy=None, state=None, log=print):
    """Every block's entries and every ROM PC the interpreter was sent to,
    over `frames` of the precompiled machine (the script, or a replay from a
    save state): the counters a host build promotes by (REALDMG-SPEC 20.11)"""
    m = None
    for f, x, st, m in run_arm(build, rom, 3, frames, img, prove=True, joy=joy, state=state,
                                heat=True):
        if f and f % 500 == 0:
            log("gbaot: heat, frame %d" % f)
    blocks = {"%02X:%04X" % (kv[1], kv[0]): n for kv, n in m.ran.items() if isinstance(kv, tuple)}
    misses = {"%02X:%04X" % (k, pc): n for (pc, k), n in m.miss.items()
              if pc < 0x8000 or pc >= 0xFF80}
    hram = {"%02X:%04X" % (k, pc): v.hex() for (pc, k), v in m.hram.items()}
    interp = {"%02X:%04X" % (k, pc): n for (pc, k), n in m.interp.items()}
    return {"frames": frames, "blocks": blocks, "misses": misses, "hram": hram,
            "interp": interp}


# --- commands ---------------------------------------------------------------

def cmd_cover(a):
    rom = G.Rom(open(a.rom, "rb").read())
    found, extra = walk(rom)
    lead = leaders(rom, found, extra)
    banks = collections.Counter(b for b, _ in found)
    print("%s: %d KB, %d banks" % (os.path.basename(a.rom), len(rom.d) // 1024, rom.banks))
    print("  %d instructions found, %d jump-table targets read, %d block starts" % (
        len(found), len(extra), len(lead)))
    print("  by bank: " + "  ".join("%02X: %d" % kv for kv in sorted(banks.items())))


def load_heat(paths, hram=None):
    """(block heat {(pc, key): n}, the misses as entries [(bank, pc)]) out of
    one or more heat files (cmd_heat's), summed. An HRAM miss is no walk's
    entry: it goes into `hram` instead ({pc: HRAM's 128 bytes as the miss
    found them}), when one is passed, for translate to make it a guarded
    block (REALDMG-SPEC 20.5)"""
    hb, extra = collections.Counter(), set()
    for p in paths:
        h = json.load(open(p))
        for k, n in h["blocks"].items():
            key, pc = k.split(":")
            hb[(int(pc, 16), int(key, 16))] += n
        for k, n in h.get("interp", {}).items():   # every instruction the
            key, pc = k.split(":")                # interpreter ran: a block
            hb[(int(pc, 16), int(key, 16))] += n  #   start's is its entries
        for k, v in h.get("hram", {}).items():
            pc = int(k.split(":")[1], 16)
            if hram is not None and pc not in hram:
                hram[pc] = bytes.fromhex(v)
        for k, n in h["misses"].items():
            key, pc = (int(x, 16) for x in k.split(":"))
            if pc >= 0xFF80:
                hb[(pc, 0xFF)] += n
                continue
            extra.add((0 if key == 0xFF else key, pc))
            hb[(pc, key)] += n             # ...and its entries are the block's: a
                                           #   miss counted only as a miss was
                                           #   placed COLD, and interpreted again
                                           #   wherever its image was not loaded
    return dict(hb), extra


def cmd_heat(a):
    rom = G.Rom(open(a.rom, "rb").read())
    img = load(a.dir)
    joy = state = None
    frames = a.frames
    if a.replay:
        j0, joy, n = replay(a.replay)
        frames = min(frames, n) if a.frames_given else n
        if not a.state:
            raise SystemExit("gbaot: a replay is played from its save state: give --state")
    if a.state:
        state = open(a.state, "rb").read()
    h = heat_run(a.build, rom, img, frames, joy, state)
    h["source"] = os.path.basename(a.replay) if a.replay else "the harness's script"
    json.dump(h, open(a.out, "w"), indent=0, sort_keys=True)
    hot = sorted(h["blocks"].items(), key=lambda kv: -kv[1])
    tot = sum(h["blocks"].values()) or 1
    top = sum(n for _, n in hot[:len(hot) // 10 or 1])
    print("gbaot: heat over %d frames (%s): %d blocks entered, the hottest tenth %.0f%% of "
          "%d entries; %d ROM PCs interpreted that the walk did not find; written to %s" % (
              frames, h["source"], len(hot), 100.0 * top / tot, tot, len(h["misses"]), a.out))


def cmd_build(a):
    rom = G.Rom(open(a.rom, "rb").read())
    found, extra = walk(rom)
    heat = None
    if a.heat:
        hram = {}
        heat, more = load_heat(a.heat, hram)
        found, extra = walk(rom, set(extra) | more)   # (from them: leaders keeps
                                                       #   only what the walk found)
        print("gbaot: heat from %d file(s): %d blocks counted, %d more block starts, "
              "%d in HRAM" % (len(a.heat), len(heat), len(more), len(hram)))
    lead = leaders(rom, found, extra, side=a.side)
    print("gbaot: %d instructions, %d block starts" % (len(found), len(lead)))
    img = translate(a.build, rom, lead, hmask=a.hmask, rhm=a.table // 2 - 1, heat=heat,
                    hram=hram if a.heat else None, squeeze=a.squeeze)
    save(img, a.out, rom)
    code = sum(len(s) for s in img["segs"])
    print("gbaot: %d blocks in %d segments (the core and %d more), %d dropped; %d code bytes, "
          "%.1f a Game Boy instruction found; table %dKB with %d in the list; %d exits linked, "
          "%d left to link, %d to HRAM blocks; written to %s" % (
              len(img["blocks"]), len(img["segs"]), len(img["segs"]) - 1, len(img["dropped"]),
              img["code_bytes"], img["code_bytes"] / max(1, len(found)),
              len(img["table"]) // 1024, img["listed"], img["linked"], img["lazy"],
              img.get("hlinked", 0), a.out))


ARMS = {"interp": 0, "dyn": 1, "aot": 3, "hyb": 4}   # hyb: 20.16


def degrade(build, img, keep):
    """`img` as the package runs it when the heap held only `keep` of its
    code images past the core (rwaot.inc's rw_afixup, the same rules): an
    entry into a missing image emptied, and a far jump into one put back to
    the exit it was before the host linked it - `mov dx,SITE / jmp
    tr_lsite` - for tr_link to find nothing behind at run time"""
    return subset(build, img, range(1, keep + 1))


def subset(build, img, keep):
    """degrade() with any images kept: `keep` the image numbers (1 the first
    past the core), renumbered in that order - so the host's ten can be ANY
    ten, and images past the tenth run on the host too (`check --images`)"""
    meta, segs, table = img
    lsite = symbols(build)["RDGC_TR_LSITE"]
    new = {0: 0}
    for i in keep:
        new[i] = len(new)
    segs = [bytearray(segs[i]) if i in new else None for i in range(len(segs))]
    relocs = []
    for n, site, tn, form in meta["relocs"]:
        if n not in new:
            continue
        if tn in new:
            relocs.append((new[n], site, new[tn], form))
            continue
        sg = segs[n]
        sg[site] = 0xBA
        struct.pack_into("<H", sg, site + 1, site)
        sg[site + 3] = 0xE9
        struct.pack_into("<H", sg, site + 4, (lsite - (site + 6)) & 0xFFFF)

    def empty(b, tab):
        t = bytearray(b)
        out = bytearray()
        for e in range(0, len(t), 8):
            seg = struct.unpack_from("<H", t, e + 6)[0]
            if seg >= TOKEN and struct.unpack_from("<H", t, e + 4)[0]:
                if seg - TOKEN not in new:
                    if not tab:            # a LIST entry goes: unlisted and
                        continue           #   offset 0 are both interpreted,
                    struct.pack_into("<H", t, e + 4, 0)   # and only one costs
                    struct.pack_into("<HB", t, e, 0x8000, 0)   # 8 bytes
                else:                      #   (the TABLE's: out of every
                    struct.pack_into("<H", t, e + 6, TOKEN + new[seg - TOKEN])
            out += t[e:e + 8]              #   lookup's reach, kept in place)
        return bytes(out)
    order = [0] + list(keep)
    hot = sum(1 for i in keep if i <= meta.get("hot", len(segs) - 1))
    meta = dict(meta, relocs=relocs, list=empty(meta["list"], False), segs=len(order), hot=hot)
    return meta, [bytes(segs[i]) for i in order], empty(table, True)


# --- the package: GAME.O88 (REALDMG-SPEC 20.10) ------------------------------

S_JITLIM = 0x4814                  # rdgdefs.inc's: RT_SQUEEZE's limit
AOT_MAX = 29                       # realdmg.asm's RWA_MAX
HOST_MAX = 11                      # ...and what run_arm's layout holds: image i
                                   #   at 4000h + 1000h x i, the list and its
                                   #   filter after the last, all under 10000h
AOT_CODE = 5                       # ...and RWA_CODE
AOT_ROWS = 64                      # ...and RWA_ROWS: the table's rows


def aot_image(build, mega=False):
    """realdmg.asm assembled as RDG_AOT, the one image every precompiled
    cartridge is: build/gbaot/realdmgaot.bin - or, `mega`, kern_mega's arm
    of it (-DPKG_MEGA, REALDMG-SPEC 20.15): build/gbaot/realdmgaotmega.bin,
    which runs on every kernel and is the one that runs on kern_mega"""
    d = aotdir(build)
    prerequisites(build)
    out = os.path.join(d, "realdmgaotmega.bin" if mega else "realdmgaot.bin")
    if BUNDLED:
        return out
    srcs = [os.path.join(INC, f) for f in os.listdir(INC) if f.endswith((".asm", ".inc"))]
    srcs += [os.path.join(ROOT, "apps", f) for f in os.listdir(os.path.join(ROOT, "apps"))
             if f.endswith(".inc")]
    if not os.path.exists(out) or any(os.path.getmtime(x) > os.path.getmtime(out) for x in srcs):
        run(["nasm", "-f", "bin", "-w+error", "-DRDG_AOT"] +
            (["-DPKG_MEGA"] if mega else []) +
            ["-I", os.path.join(ROOT, "apps") + "/", "-I", INC + "/",
             "-I", os.path.join(ROOT, "drivers", "sound") + "/",
             "-I", d + "/", "-l", out[:-4] + ".lst",
             "-o", out, os.path.join(INC, "realdmg.asm")], "assembling the package image")
    return out


def aot_jitpart(build):
    """The recompiler as a part (rdgjitp.asm), assembled against the
    PRECOMPILED core's templates and offsets with RDG_HYB: kern_mega's
    package carries it to translate what the host did not precompile
    (REALDMG-SPEC 20.16) - build/gbaot/rdgjitpaot.bin"""
    d = aotdir(build)
    prerequisites(build)
    out = os.path.join(d, "rdgjitpaot.bin")
    if BUNDLED:
        return out
    srcs = [os.path.join(INC, f) for f in ("rdgjitp.asm", "rdgjit.inc", "rdgdefs.inc")]
    srcs += [os.path.join(d, "rdgtmpl.bin"), os.path.join(d, "rdgcore.inc")]
    if not os.path.exists(out) or any(os.path.getmtime(x) > os.path.getmtime(out) for x in srcs):
        run(["nasm", "-f", "bin", "-w+error", "-DRDG_XTIER", "-DRDG_HYB",
             "-I", d + "/", "-I", INC + "/", "-o", out,
             os.path.join(INC, "rdgjitp.asm")], "assembling the recompiler's part")
    return out


def rom_name(path):
    b = os.path.basename(path).upper()
    stem, _, ext = b.rpartition(".") if "." in b else (b, "", "")
    if not stem or len(stem) > 8 or len(ext) > 3 or any(c in stem + ext for c in ' "*+,/:;<=>?[]|'):
        raise SystemExit("gbaot: %s is not an 8.3 name; give --romname" % b)
    return b


def package(build, img, rom, out, romname, image=None, log=print, embed=False, mega=False):
    """GAME.O88: the RDG_AOT image with the parts set to this cartridge's -
    and with `embed`, the ROM itself as plain 32KB parts after the code
    images, which the package reads out of its own file (REALDMG-SPEC 20.13)"""
    meta, segs, table = img
    nseg = len(segs) - 1
    if nseg > AOT_MAX:
        raise SystemExit("gbaot: %d code images; the package holds %d" % (nseg, AOT_MAX))
    romrows = len(rom.d) // 0x8000 if embed else 0
    lst = meta["list"]
    rel = b"".join(struct.pack("<BBH", n, tn | (0x80 if form else 0), site)
                   for n, site, tn, form in meta["relocs"])
    # THE RELOCATIONS AND THE LIST IN PARTS OF THEIR OWN (REALDMG-SPEC 20.17):
    # in the meta part they bound a package at ~10 images (65,024 bytes)
    relp = [rel[k:k + REL_PART] for k in range(0, len(rel), REL_PART)]
    lstp = [lst[k:k + LIST_PART] for k in range(0, len(lst), LIST_PART)]
    if len(lstp) > LIST_PARTS:
        raise SystemExit("gbaot: %d listed blocks are %d list parts; the package searches %d - "
                         "fewer images (--images, --prefer)" % (len(lst) // 8, len(lstp), LIST_PARTS))
    relrow = AOT_CODE                      # (eager carve parts come before
    lstrow = relrow + len(relp)            #   every lazy one, SPEC.md 20.12.4:
    coderow = lstrow + len(lstp)           #   the images after them)
    romrow = coderow + 2 * nseg
    jrow = romrow + romrows if mega else 0   # (20.16: the recompiler last)
    nrows = romrow + romrows + (1 if jrow else 0)
    if embed and len(rom.d) % 0x8000 or nrows > AOT_ROWS:
        raise SystemExit("gbaot: %d code images, %d relocation and %d list parts%s are %d rows; "
                         "the package holds %d - fewer images (--images, --prefer)%s" % (
                             nseg, len(relp), len(lstp),
                             " and a %d KB ROM" % (len(rom.d) // 1024) if embed else "",
                             nrows, AOT_ROWS, ", or the ROM beside it" if embed else ""))
    import os88lz
    b = bytearray(open(image or aot_image(build, mega), "rb").read())
    at = b.find(b"O88PARTS")
    rows = at + 10
    b[at + 8] = nrows
    for r in range(relrow, coderow):       # eager, into the carve: the load
        b[rows + 8 * r] = 1                #   reads them, OP_ASSET | OP_COMP
        b[rows + 8 * r + 1] = 16           #   (rwaot.inc's rw_ameta)
    hdr = struct.pack("<6sBBHHBB", b"RDAOT2", nseg, meta["hmask"], len(lst) // 8,
                      len(meta["relocs"]), relrow if relp else 0, len(relp))
    hdr += bytes.fromhex(meta["header"]) + romname.encode().ljust(13, b"\0")[:13]
    hdr = hdr.ljust(55, b"\0") + bytes([jrow]) + struct.pack(
        "<BBBBH", lstrow if lstp else 0, len(lstp), romrow if embed else 0,
        meta.get("hot", nseg), len(rom.d) // 1024 if embed else 0)
    metap = hdr.ljust(64, b"\0")
    # the filter and the list's cache (rdgaot.inc, 10KB) follow the TABLE in
    # its part: in the meta part, after a long list, they took that part past
    # 65,024 bytes - Link's Awakening's 65,264, Wario Land's 68,672
    parts = [segs[0][:0x8000], segs[0][0x8000:], table + bytes(0x2800), metap] + relp + lstp
    for sg in segs[1:]:
        parts += [sg[:0x8000], sg[0x8000:]]
    plain = set()
    for i in range(romrows):               # the ROM: plain, so its pieces lie
        parts.append(rom.d[i * 0x8000:(i + 1) * 0x8000])   # back to back
        plain.add(romrow + i)
    if jrow:                               # kern_mega's: the recompiler, for
        parts.append(open(aot_jitpart(build), "rb").read())   # the misses (20.16)
    filed = [0, 1, 3, 4] + list(range(AOT_CODE, nrows))
    work = tempfile.mkdtemp(prefix="gbaotpkg-")
    try:                                   # (the parts gone however it ends)
        paths = []
        for row, body in zip(filed, parts):
            if row in plain or len(os88lz.compress(body, os88lz.LZ4)) >= len(body):
                b[rows + 8 * row + 1] &= ~16       # plain: OP_COMP off
            p = os.path.join(work, "part%02d.bin" % row)
            open(p, "wb").write(body)
            paths.append(p)
        binp = os.path.join(work, "game.bin")
        open(binp, "wb").write(b)
        cmd = [sys.executable, os.path.join(HERE, "os88pkg.py"), "--compress-if=lz4",
               binp, "-o", out, "--part-compress", "lz4"]
        for p in paths:
            cmd += ["--part", p]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(r.stdout + r.stderr)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    size = os.path.getsize(out)
    if size >= PKG_FILE_MAX:               # PKG_FILE_HI (SPEC.md 19.1): the
        os.remove(out)                     #   mount types no package file of
        raise SystemExit(                  #   32MB or more as one
            "gbaot: %s would be %d bytes; a package file is under 32MB (SPEC.md 19.1)%s" % (
                out, size, " - make it without --embed, the ROM beside it" if embed else ""))
    carve = 64 + (len(table) + 0x2800 + 1023) // 1024 + 64 + sum(
        (len(x) + 1023) // 1024 for x in [metap] + relp + lstp)
    log("gbaot: %s, %d bytes: the core and %d code images (64 KB of heap each, %d of them "
        "hot), the table, %d listed, %d relocations; the carve is %d KB with the view; %s" % (
            out, size, nseg, meta.get("hot", nseg), len(lst) // 8, len(meta["relocs"]), carve,
            "the ROM is inside it" if embed else "it opens %s beside it" % romname)
        + ("; kern_mega's build (it runs on every kernel)" if mega else ""))
    return size


PKG_FILE_MAX = 512 << 16          # SPEC.md 19.1: a package file is under 32MB
META_MAX = 65024                  # SPEC.md 20.12.11: a part's bytes at most
REL_PART = 65024                  # a relocation part: 16,256 of 4 bytes
LIST_PART = 8128 * 8              # a list part: 8,128 entries of 8 (a part's
LIST_PARTS = 4                    #   most), RDG_LXMAX of them (rdgdefs.inc)


def package_fit(build, img, rom, out, romname, log=print, embed=False, mega=False,
                prefer=None):
    """package(), choosing what gives when the ROM inside and every code
    image will not both fit a package (the 32MB file, AOT_ROWS rows):
    `prefer` "rom" keeps the ROM inside and leaves the coldest images out
    (degrade's way - what they held is interpreted, or translated at run
    time on kern_mega, REALDMG-SPEC 20.16); "code" keeps every image and
    puts the ROM BESIDE the package when it does not fit inside. None is
    package() as it was: refused"""
    package_fit.embedded = embed           # (what came out: the GUI's floppy)
    if prefer is None:
        return package(build, img, rom, out, romname, log=log, embed=embed, mega=mega)
    nseg = len(img[1]) - 1

    def trimmed(emb):                  # the coldest images out until it fits:
        rows = AOT_ROWS - AOT_CODE - (len(rom.d) // 0x8000 if emb else 0) - (1 if mega else 0)
        keep = min(nseg, (rows - 2) // 2)   # the rows (two at least for the
                                            #   relocation and list parts), the
                                            #   list's parts, the file
        while True:
            try:
                if keep < nseg:
                    log("gbaot: %d code images of %d fit (%s)" % (
                        keep, nseg, "the ROM inside" if emb else "the ROM beside it"))
                return package(build, degrade(build, img, keep) if keep < nseg else img, rom,
                               out, romname, log=log, embed=emb, mega=mega)
            except SystemExit:
                if keep <= 1:
                    raise
                keep -= 1
    if embed and prefer == "code":     # every image the format holds, and
        try:                           #   the ROM inside only when it fits
            return package(build, img, rom, out, romname, log=log, embed=True, mega=mega)
        except SystemExit as e:
            log("gbaot: %s" % str(e).splitlines()[0])
        log("gbaot: the code first (--prefer code): the ROM beside the package")
        package_fit.embedded = False
        return trimmed(False)
    return trimmed(embed)


def hottest(build, rom, img, frames=600):
    """The block entered most often in `frames` of the precompiled code"""
    for f, x, s1, m1 in run_arm(build, rom, 3, frames, img, prove=True):
        pass
    return max(((k, v) for k, v in m1.ran.items() if k != "code"), key=lambda kv: kv[1])


def broken(img, which):
    """`img` with one block's first byte made a different instruction: the
    NEGATIVE CONTROL, which must show"""
    meta, segs, table = img
    segs = list(segs)
    for e in range(0, len(table), 8):
        pc, key, cnt, off, seg = struct.unpack_from("<HBBHH", table, e)
        if off and "%02X:%04X" % (key, pc) == which.upper():
            sb = bytearray(segs[seg - TOKEN])
            sb[off] ^= 0x01
            segs[seg - TOKEN] = bytes(sb)
    return (meta, segs, table)


def compare(build, rom, img, frames, ref="interp", arm="aot", serial=False, defines=(),
            log=print, joy=None, state=None, slots=None, lparts=None):
    """Run two arms side by side - the script, or a replay from its save
    state (`joy`, `state`). Returns (identical, what was found)."""
    r = run_arm(build, rom, ARMS[ref], frames, img if ref in ("aot", "hyb") else None, defines=defines,
                joy=joy, state=state, slots=slots, lparts=lparts)
    k = run_arm(build, rom, ARMS[arm], frames, img if arm in ("aot", "hyb") else None,
                prove=(arm in ("aot", "hyb")), defines=defines, joy=joy, state=state, slots=slots,
                lparts=lparts)
    m0 = m1 = None
    if serial:                            # a ROM that reports on its link port:
        for (f, x0, s0, m0), (_, x1, s1, m1) in zip(r, k):   # its words, not its
            pass                                               # frames
        t0, t1 = m0.serial().decode("latin-1"), m1.serial().decode("latin-1")
        tail = " ".join(t1.split())[-100:]
        return bool(t0) and t0 == t1, ("link port IDENTICAL: " if t0 == t1 else
                                       "link port DIFFERS: %s / " % " ".join(t0.split())[-60:]) + tail
    for (f, x0, s0, m0), (_, x1, s1, m1) in zip(r, k):
        if (x0, s0) != (x1, s1):
            what = [n for n, p, q in zip(("registers", "time", "events/skips", "memory"), s0, s1) if p != q]
            return False, "frame %d DIFFERS (%s)" % (f, ", ".join(what))
    ran = getattr(m1, "ran", collections.Counter())["code"]
    nbr = m1.r16(m1.view, m1.sym["S_JITNBR"])
    banks = ""
    if slots:
        banks = "; %d slots, banks fetched %d (%s) against %d (%s)" % (
            slots, getattr(m1, "fetches", 0), arm, getattr(m0, "fetches", 0), ref)
    hy = ""
    if arm == "hyb":                      # (20.16: the misses made blocks)
        hy = "; %d misses made blocks (%d counted)" % (
            m1.r16(m1.view, m1.sym["S_HYN"]), m1.r16(m1.view, m1.sym["S_HYMISS"]))
    return True, "%d frames IDENTICAL, %s against %s; %d precompiled blocks entered, %d translated " \
        "while running%s%s" % (frames, arm, ref, ran, nbr, banks, hy)


def make(build, rom_path, out, romname=None, heat_frames=3000, replays=(), check=0,
         log=print, embed=False, mega=False, images=None, prefer=None):
    """GAME.O88 from a ROM in one go: the walk, the translation, a run of it
    with the harness's script (and any replays, from their save states) for
    the counters, the translation again with the hottest blocks first and
    the misses as block starts, the package - and, with `check`, that many
    frames of it beside the interpreter. Returns the summary line"""
    data = open(rom_path, "rb").read()
    rom = G.Rom(data)
    romname = romname or rom_name(rom_path)
    found, extra = walk(rom)
    lead = leaders(rom, found, extra)
    log("gbaot: %s: %d KB, %d instructions found, %d block starts" % (
        os.path.basename(rom_path), len(data) // 1024, len(found), len(lead)))
    work = tempfile.mkdtemp(prefix="gbaotmk-")
    try:
        rhm = 15 if mega else 7            # kern_mega's table 32KB (20.17):
        img = translate(build, rom, lead, log=log, images=images, rhm=rhm)   # more held, less listed
        d0 = os.path.join(work, "first")
        save(img, d0, rom)
        heats = []
        if heat_frames:
            log("gbaot: the counters: %d frames of the harness's script" % heat_frames)
            heats.append(heat_run(build, rom, load(d0), heat_frames, log=log))
        for rp, ss in replays:
            j0, joy, n = replay(rp)
            log("gbaot: the counters: %s, %d frames from %s" % (
                os.path.basename(rp), n, os.path.basename(ss)))
            heats.append(heat_run(build, rom, load(d0), n, joy, open(ss, "rb").read(), log=log))
        if heats:
            paths = []
            for i, h in enumerate(heats):
                paths.append(os.path.join(work, "h%d.json" % i))
                json.dump(h, open(paths[-1], "w"))
            hram = {}
            heat, more = load_heat(paths, hram)
            log("gbaot: %d blocks counted, %d more block starts, %d in HRAM; translating "
                "again, the hottest first" % (len(heat), len(more), len(hram)))
            found, extra = walk(rom, set(extra) | more)   # the misses WALKED: a
            lead = leaders(rom, found, extra)               #   start the walk never
            near = near_heat(rom, found, lead, heat, int(os.environ.get(   # found is
                "GBAOT_NEAR", NEAR_DEPTH)))                  #   dropped
            img = translate(build, rom, lead, log=log, heat=heat,
                            hram=hram, near=near, images=images, rhm=rhm)
        d1 = os.path.join(work, "final")
        save(img, d1, rom)
        # GAME.O88 is written under its own name only once it is checked: a
        # package that failed the check is not left beside the ROM
        part = out + ".part"
        try:
            package_fit(build, load(d1), rom, part, romname, log=log, embed=embed, mega=mega,
                        prefer=prefer)
            if check:
                log("gbaot: checking %d frames beside the interpreter..." % check)
                ok, what = compare(build, rom, load(d1), check, log=log)
                log("gbaot: " + what)
                if not ok:
                    raise SystemExit("gbaot: the precompiled code is NOT the interpreter's machine")
            os.replace(part, out)
        finally:
            if os.path.exists(part):
                os.remove(part)
    finally:
        keep = os.environ.get("GBAOT_KEEP")    # the first and final images and
        if keep:                               #   the heat, to package again
            shutil.copytree(work, keep, dirs_exist_ok=True)
        shutil.rmtree(work, ignore_errors=True)
    return out


def cmd_make(a):
    if a.images is not None and not 1 <= a.images <= AOT_MAX:
        raise SystemExit("gbaot: --images is 1 to %d" % AOT_MAX)
    reps = []
    for r in a.replay:
        rp, _, ss = r.partition(",")
        if not ss:
            raise SystemExit("gbaot: --replay NAME.RPn,NAME.SSn: a replay and the state it starts from")
        reps.append((rp, ss))
    make(a.build, a.rom, a.out, a.romname, a.frames, reps, a.check, embed=a.embed,
         mega=a.mega, images=a.images, prefer=a.prefer)


def cmd_package(a):
    rom = G.Rom(open(a.rom, "rb").read())
    img = load(a.dir)
    if img[0]["rom_sha256"] != hashlib.sha256(rom.d).hexdigest():
        raise SystemExit("gbaot: %s was not built from %s" % (a.dir, a.rom))
    package_fit(a.build, img, rom, a.out, a.romname or rom_name(a.rom), embed=a.embed,
                mega=a.mega, prefer=a.prefer)


def cmd_check(a):
    rom = G.Rom(open(a.rom, "rb").read())
    img = load(a.dir)
    if a.only:                            # (images by number, ranges too)
        keep = []
        for r in a.only.split(","):
            lo, _, hi = r.partition("-")
            keep += range(int(lo), int(hi or lo) + 1)
        img = subset(a.build, img, keep)
        print("gbaot: images %s only, as 1 to %d" % (a.only, len(keep)))
    if a.keep is not None:
        img = degrade(a.build, img, a.keep)
        print("gbaot: as the package runs with %d code images of %d" % (a.keep, len(load(a.dir)[1]) - 1))
    if a.break_hot:
        (pc, key), n = hottest(a.build, rom, img, min(a.frames, 600))
        a.break_block = "%02X:%04X" % (key, pc)
        print("gbaot: the hottest block is %s, entered %d times" % (a.break_block, n))
    if a.break_block:
        img = broken(img, a.break_block)
        print("gbaot: block %s broken" % a.break_block)
    joy = state = None
    frames = a.frames
    if a.replay:                          # a replay, as heat plays one
        if not a.state:
            raise SystemExit("gbaot: a replay is played from its save state: give --state")
        j0, joy, n = replay(a.replay)
        frames = min(frames, n)
        state = open(a.state, "rb").read()
    ok, what = compare(a.build, rom, img, frames, a.ref, a.arm, a.serial, joy=joy, state=state,
                       slots=a.slots, lparts=a.list_parts)
    print("gbaot: " + what)
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("cover")
    c.add_argument("rom")
    b = sub.add_parser("build")
    b.add_argument("rom")
    b.add_argument("-o", "--out", required=True)
    b.add_argument("--hmask", type=int, default=31, help="the TRANSLATOR's table mask")
    b.add_argument("--table", type=int, default=16, choices=(16, 32, 64),
                   help="KB of the table the machine runs with")
    b.add_argument("--side", action="store_true", help="a block at every resume point too")
    b.add_argument("--squeeze", type=lambda x: int(x, 0), default=0, metavar="END",
                   help="TESTS ONLY: every image's code ends at END (e.g. 0x210), so a "
                   "small ROM fills many images (tests/rdgaotmega.py)")
    b.add_argument("--heat", action="append", default=[],
                   help="a heat file (gbaot.py heat): its hottest blocks first, its misses "
                   "as block starts; may be given more than once")
    h = sub.add_parser("heat", help="count every block's entries over a run of the "
                       "precompiled machine")
    h.add_argument("rom")
    h.add_argument("dir")
    h.add_argument("-o", "--out", required=True)
    h.add_argument("-n", "--frames", type=int, default=3000)
    h.add_argument("--replay", help="a NAME.RPn RealDMG recorded (Save State > Record Replay)")
    h.add_argument("--state", help="...and the NAME.SSn it starts from")
    k = sub.add_parser("check")
    k.add_argument("rom")
    k.add_argument("dir")
    k.add_argument("-n", "--frames", type=int, default=1200)
    k.add_argument("--serial", action="store_true",
                   help="compare what the game says on its link port, not each frame")
    k.add_argument("--ref", default="interp", choices=("interp", "dyn", "aot", "hyb"))
    k.add_argument("--arm", default="aot", choices=("interp", "dyn", "aot", "hyb"))
    k.add_argument("--break-block", help="BB:PPPP: break this block on purpose (must go red)")
    k.add_argument("--break-hot", action="store_true", help="break the most entered block")
    k.add_argument("--keep", type=int, help="only this many code images past the core, as the "
                   "package runs when the heap holds no more")
    k.add_argument("--only", help="these code images only (1,11-19): the rest as not loaded, "
                   "so images past the host's ten are run on it too")
    k.add_argument("--slots", type=int, help="a bank cache of this many 16KB slots, as the "
                   "package keeps one when the cartridge does not fit")
    k.add_argument("--list-parts", type=int, choices=range(1, LIST_PARTS + 1), metavar="N",
                   help="the list cut into N parts, however long it is (REALDMG-SPEC 20.17.1: "
                   "the part search's gate)")
    k.add_argument("--replay", help="a NAME.RPn to play instead of the script...")
    k.add_argument("--state", help="...from the NAME.SSn it starts from")
    mk = sub.add_parser("make", help="GAME.O88 from a ROM: build, count, build again, package")
    mk.add_argument("rom")
    mk.add_argument("-o", "--out", required=True)
    mk.add_argument("--romname", help="the 8.3 name it opens beside it (default: the ROM's)")
    mk.add_argument("-n", "--frames", type=int, default=3000,
                    help="frames of the harness's script the counters run (0: none)")
    mk.add_argument("--replay", action="append", default=[], metavar="RPn,SSn",
                    help="a replay RealDMG recorded and the save state it starts from")
    mk.add_argument("--check", type=int, default=0, metavar="FRAMES",
                    help="then run that many frames beside the interpreter")
    mk.add_argument("--embed", action="store_true",
                    help="the ROM inside the package: one file, no ROM beside it")
    mk.add_argument("--mega", action="store_true",
                    help="kern_mega's build of the package (REALDMG-SPEC 20.15): runs on "
                    "every kernel, and is the one that runs on kern_mega")
    p = sub.add_parser("package", help="GAME.O88 from a built directory")
    p.add_argument("rom")
    p.add_argument("dir")
    p.add_argument("-o", "--out", required=True)
    p.add_argument("--romname", help="the 8.3 name it opens beside it (default: the ROM's)")
    p.add_argument("--embed", action="store_true", help="the ROM inside the package")
    p.add_argument("--mega", action="store_true", help="kern_mega's build of the package")
    for x in (mk, p):
        x.add_argument("--prefer", choices=("rom", "code"),
                       help="with --embed, when the ROM and every code image do not both fit: "
                       "'rom' leaves the coldest images out, 'code' puts the ROM beside it")
    mk.add_argument("--images", type=int, metavar="N",
                    help="code images at most (default %d, what the host runs; up to %d - "
                    "past %d the host checks the first %d, as a package runs them with less "
                    "memory)" % (HOST_MAX - 1, AOT_MAX, HOST_MAX - 1, HOST_MAX - 1))
    a = ap.parse_args()
    a.frames_given = "-n" in sys.argv or "--frames" in sys.argv
    return {"cover": cmd_cover, "heat": cmd_heat, "make": cmd_make, "build": cmd_build, "check": cmd_check,
            "package": cmd_package}[a.cmd](a) or 0


if __name__ == "__main__":
    sys.exit(main())
