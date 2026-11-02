#!/usr/bin/env python3
"""WIN16's in-guest census against tools/os88ne.py's (SPEC.md 124.4,
docs/plans/WIN16-PLAN.md S11).

    python3 tests/unit/t_w16cens.py [-v] [FILE.EXE ...]

`apps/win16/w16cens.inc` is os88ne.py's recursive descent, run on the 8088
that has to decide whether a program may run (SPEC.md 124.4.1). Its header
promises that it follows the host "decision for decision", and this row is
where that is held: the WHOLE of WIN16 is assembled, loaded into unicorn (the
census's code at a segment of its own, as part 5, SPEC.md 124.18.7),
and its own `w16_census` is called on each program the way the loader
calls it - the file read, every segment copied to a paragraph of its own,
the relocations applied and the OS fixups patched for a machine with no
coprocessor - with OSAPI_MEM_CLAIM and OSAPI_MEM_FREE answered here.

Two things are compared, for every code segment of every program:

  * WHICH BYTES the walk reached - the census's own bitmap, read out of its
    claim, against the bytes the host's instructions and switch tables
    cover. Equal sets mean the two walks took the same path, so a decoder
    that sizes one instruction wrongly cannot hide behind a lucky verdict;
  * THE VERDICT - run, or refused with the tier the host names.

The programs are what exists here: the Open Watcom fixtures `make
win16fixtures` builds into build/win16/ and the Microsoft samples the
win16-reference skill copies beside them (row `w16cens`, fast); `--all`
adds every program under win16ref/samples/ - the whole of Windows 3.0's
directory, CLOCK and REVERSI among them (row `w16censall`, full); or the
files named on the command line. None at all is a SKIP. A file over 64 KB
is one WIN16 does not load yet (WIN16-PLAN R2) and is passed over.

It FAILED on the decoder S9 found (WIN16-PLAN S11): `w16c_mrlen` answers in
AX, so the opcode table's byte in AH was gone by the time the immediate was
counted, and every instruction with a modrm AND an immediate (`83 EC 60`,
`sub sp, 60h`) was walked two bytes short. On FPU.EXE that read as *Needs
an 80386 or better.*
"""
import argparse
import glob
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, eq, done                       # noqa: E402
import os88ne                                              # noqa: E402

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_DX, UC_X86_REG_SP,
                                   UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES,
                                   UC_X86_REG_CS, UC_X86_REG_IP, UC_X86_REG_FLAGS)
except Exception:                                          # pragma: no cover
    print("t_w16cens: SKIP - unicorn is not importable here")
    sys.exit(77)

IMG = 0x1000            # WIN16's segment: DS
PSEG = 0x7000           # part 5, the loader and the census: CS (SPEC.md 124.18.7)
FSEG = 0x2000           # the file, as w16_load reads it
SEG0 = 0x3000           # the program's segments, one after another
CLAIM = 0x8000          # what OSAPI_MEM_CLAIM answers
SSEG = 0x9000
KSEG = 0x0060           # KERNEL_SEG: every API cell is an `int 0F0h` here
STOP = 0xFFF0           # w16_census returns here


def assemble():
    """WIN16's image and its symbols, as `make win16` assembles it. Cached in
    build/win16/ under a hash of every file the assembly can include, so the
    fast tier pays nasm's three seconds only after an edit"""
    import hashlib
    h = hashlib.sha256()
    for f in sorted(glob.glob(os.path.join(ROOT, "apps", "win16", "*.*"))
                    + glob.glob(os.path.join(ROOT, "apps", "*.inc"))):
        h.update(f.encode() + b"\0" + open(f, "rb").read())
    key = h.hexdigest()[:16]
    cache = os.path.join(ROOT, "build", "win16", "census-%s" % key)
    if not (os.path.exists(cache + ".bin") and os.path.exists(cache + ".map")):
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        for old in glob.glob(os.path.join(os.path.dirname(cache), "census-*")):
            os.remove(old)
        with tempfile.TemporaryDirectory() as d:
            w, m, o = (os.path.join(d, n) for n in ("w.asm", "w.map", "w.bin"))
            with open(w, "w") as f:
                f.write('%%include "win16/win16.asm"\n[map symbols %s]\n' % m)
            subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                            "-I", "apps/win16/", "-I", "apps/dos/", "-o", o, w], check=True, cwd=ROOT)
            open(cache + ".map", "w").write(open(m).read())
            open(cache + ".bin", "wb").write(open(o, "rb").read())
    syms = {}
    for line in open(cache + ".map"):
        f = line.split()
        if len(f) == 3 and re.fullmatch(r"[0-9A-F]+", f[0]):
            syms[f[2]] = int(f[1], 16)      # VIRTUAL: a part's own offset
    raw = open(cache + ".bin", "rb").read()
    at = raw.find(b"W16LOADR") - 2           # part 5 is the last section
    return raw[:syms["os88_image_end"]], raw[at:], syms


def api(name):
    src = open(os.path.join(ROOT, "apps", "os88api.inc")).read()
    return int(re.search(r"%%define %s\s+KERNEL_SEG:(0x[0-9A-Fa-f]+)" % name,
                         src).group(1), 16)


IMAGE, PART, S = assemble()
CLAIM_CELL, FREE_CELL = api("OSAPI_MEM_CLAIM"), api("OSAPI_MEM_FREE")
REFUSAL = {}
for k, tier in (("w16_r_c186", "186"), ("w16_r_c286", "286"),
                ("w16_r_c386", "386"), ("w16_r_cbig", "too large"),
                ("w16_r_mem", "no memory")):
    if k in S:
        REFUSAL[S[k]] = tier


def rd(mu, seg, off, n):
    return bytes(mu.mem_read((seg << 4) + off, n))


def w16(mu, name, off=0):
    return struct.unpack("<H", rd(mu, IMG, S[name] + off, 2))[0]


def put(mu, name, val, off=0):
    mu.mem_write((IMG << 4) + S[name] + off, struct.pack("<H", val))


def noret_slots():
    """KERNEL.1 and KERNEL.137 (FatalExit, FatalAppExit) as the loader binds
    them (SPEC.md 124.19.1): a slot each past the image's four fixed ones, its
    part byte carrying W16B_NORET - which is what the census reads, so a far
    call to their stubs does not return, to either census"""
    return {1: 4, 137: 5}


NORET = noret_slots()


def fixtab():
    """w16x_osfix's two tables, read out of the image: the patch is WIN16's"""
    a, b = S["w16x_fix1"], S["w16x_fix2"]
    return (struct.unpack_from("<6H", IMAGE, a), struct.unpack_from("<6H", IMAGE, b))


FIX1, FIX2 = fixtab()


def addw(buf, off, v):
    if 0 <= off and off + 2 <= len(buf):
        struct.pack_into("<H", buf, off, (struct.unpack_from("<H", buf, off)[0] + v) & 0xFFFF)


def tables(data):
    """what w16_load leaves in [w16_fseg] (SPEC.md 124.20.1): the NE header
    and its tables at offset 0, then each segment's relocation records, and
    where those start. Out (bytes, the tables' extent, {segment: offset})"""
    at = struct.unpack_from("<I", data, 0x3C)[0]
    h = data[at:at + 0x40]
    u = lambda o: struct.unpack_from("<H", h, o)[0]
    end = max(u(0x22) + 8 * u(0x1C), u(0x28) + 2 * u(0x1E), u(0x04) + u(0x06),
              u(0x24), u(0x26), u(0x2A) + 1, 0x40)
    buf = bytearray(data[at:at + end])
    buf += b"\0" * (end - len(buf) + (end & 1))
    align = u(0x32) or 9
    rtoff = {}
    for n in range(1, u(0x1C) + 1):
        sec, ln, fl, _mn = struct.unpack_from("<4H", data, at + u(0x22) + 8 * (n - 1))
        if not sec or not fl & 0x100:
            continue
        r = (sec << align) + (ln or 0x10000)
        cnt = struct.unpack_from("<H", data, r)[0]
        rtoff[n] = len(buf)
        buf += data[r:r + 2 + 8 * cnt]
    return bytes(buf), end, rtoff


def layout(ne):
    """each segment's paragraph, as the loader gives them out"""
    base, p = {}, SEG0
    for s in ne.segs:
        base[s.n] = p
        p += (max(s.length, s.minalloc or 0, 1) + 15) >> 4
    return base


def segimage(ne, s, base):
    """segment s's bytes as the census sees them: relocated, and its OS
    fixups patched for the emulator (an 8088 has no coprocessor)"""
    buf = bytearray(s.data or b"")
    stub0 = S["w16_stubs"]
    adds = {x for _at, _k, add, sites, _t in s.relocs if add for x in sites}
    for site, (at, tgt) in sorted(s.site.items()):
        kind = tgt[0]
        if kind == "os":
            k = tgt[1]
            if 1 <= k <= 6:
                addw(buf, site, FIX1[k - 1])
                if FIX2[k - 1]:
                    addw(buf, site + 1, FIX2[k - 1])
            continue
        if kind == "internal":
            r = ne.resolve(tgt)
            if not r:
                continue
            seg, off = base[r[0]], r[1]
        elif kind == "import":
            _, mod, o = tgt
            seg, off = IMG, 0           # a stub that returns: below w16_stubs
            if mod.upper() == "KERNEL" and o in NORET:
                off = stub0 + 6 * NORET[o]
        else:
            continue
        words = {os88ne.RA_FARPTR: ((0, off), (2, seg)),
                 os88ne.RA_SEGMENT: ((0, seg),), os88ne.RA_OFFSET: ((0, off),)}
        for k, v in words.get(at, ()):
            if site + k + 2 > len(buf):
                continue
            if site in adds:
                addw(buf, site + k, v)
            else:
                struct.pack_into("<H", buf, site + k, v)
    return bytes(buf)


class Machine:
    def __init__(self):
        mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        mu.mem_write(KSEG << 4, b"\xCD\xF0" * 0x800)
        mu.hook_add(UC_HOOK_INTR, self._int)
        self.mu = mu
        self.pend = None

    def _int(self, uc, n, ud):
        self.pend = n
        uc.emu_stop()

    def census(self, data, ne):
        mu = self.mu
        mu.mem_write(IMG << 4, IMAGE + b"\0" * (0x10000 - len(IMAGE)))
        mu.mem_write(PSEG << 4, PART + b"\0" * (0x10000 - len(PART)))
        tab, tend, rtoff = tables(data)
        mu.mem_write(FSEG << 4, tab + b"\0" * (0x10000 - len(tab)))
        mu.mem_write(CLAIM << 4, b"\xAA" * 0x10000)       # the census clears it
        base = layout(ne)
        for s in ne.segs:
            mu.mem_write(base[s.n] << 4, segimage(ne, s, base))
        put(mu, "w16_fseg", FSEG)
        put(mu, "w16_fend", tend)
        put(mu, "w16_ne", 0)
        mu.mem_write((IMG << 4) + S["w16_rtoff"], b"\0" * 2 * 253)
        for n, o in rtoff.items():
            put(mu, "w16_rtoff", o, 2 * (n - 1))
        put(mu, "w16_nseg", len(ne.segs))
        put(mu, "w16_nmod", 0)
        for slot in NORET.values():                 # the bind table's two
            mu.mem_write((IMG << 4) + S["w16_bind"] + 4 * slot,   # no-return
                         bytes((0, 0x80, 0, 0)))                  # slots
        for s in ne.segs:
            put(mu, "w16_sbase", base[s.n], 2 * (s.n - 1))
        mu.mem_write((IMG << 4) + S["w16_name"], b"TEST.EXE\0")
        put(mu, "w16_ibv", S["w16_ibgate"])     # ICALL's way back, as the
        put(mu, "w16_ibv", IMG, 2)              #   entry proc sets it
        sp = 0xFFFE - 2
        mu.mem_write((SSEG << 4) + sp, struct.pack("<H", STOP))
        for r, v in ((UC_X86_REG_SS, SSEG), (UC_X86_REG_SP, sp), (UC_X86_REG_DS, IMG),
                     (UC_X86_REG_ES, IMG), (UC_X86_REG_CS, PSEG)):
            mu.reg_write(r, v)
        lin = (PSEG << 4) + S["w16_census"]
        while True:
            self.pend = None
            mu.emu_start(lin, (PSEG << 4) + STOP, count=400000000)
            if self.pend is None:
                break
            if self.pend != 0xF0:
                raise RuntimeError("int %02Xh" % self.pend)
            ip = mu.reg_read(UC_X86_REG_IP)
            cell = ip - 2
            fl = mu.reg_read(UC_X86_REG_FLAGS)
            if cell == CLAIM_CELL:
                mu.reg_write(UC_X86_REG_DX, CLAIM)
                fl &= ~1
            elif cell == FREE_CELL:
                fl &= ~1
            else:
                raise RuntimeError("the census called the API cell %04Xh" % cell)
            mu.reg_write(UC_X86_REG_FLAGS, fl)
            # retf: the far call's return
            sp = mu.reg_read(UC_X86_REG_SP)
            rip, rcs = struct.unpack("<HH", rd(mu, SSEG, sp, 4))
            mu.reg_write(UC_X86_REG_SP, sp + 4)
            mu.reg_write(UC_X86_REG_CS, rcs)
            lin = (rcs << 4) + rip
        cf = mu.reg_read(UC_X86_REG_FLAGS) & 1
        l1 = rd(mu, IMG, S["w16_l1"], 48).split(b"\0")[0].decode("latin-1")
        reached = {}
        for s in ne.segs:
            b = w16(mu, "w16_cbm", 2 * (s.n - 1))
            if b == 0xFFFF:
                continue
            n = w16(mu, "w16_clast", 2 * (s.n - 1)) + 1
            bits = rd(mu, CLAIM, b, (n + 7) >> 3)
            reached[s.n] = {o for o in range(n) if bits[o >> 3] >> (o & 7) & 1}
        return cf, l1, reached


SPECS = os88ne.load_specs()
_census, _last = os88ne.census, {}


def _keep(ne):
    _last["sc"] = r = _census(ne)
    return r


os88ne.census = _keep           # analyse() walks once, and both answers keep


def host(path):
    data = open(path, "rb").read()
    ne = os88ne.NE(data, os.path.basename(path))      # NEError: not one
    _last.clear()
    r = os88ne.analyse(path, specs=SPECS)
    sc = _last["sc"]
    want = {n: set(c.owner) | set(c.data) for n, c in sc.items()}
    return data, ne, want, r["cpu"]


def inputs(args, every):
    if args:
        return args
    out = sorted(glob.glob(os.path.join(ROOT, "build", "win16", "*.EXE")))
    if every:
        out += sorted(glob.glob(os.path.join(ROOT, "win16ref", "samples", "**",
                                             "*.EXE"), recursive=True))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--all", action="store_true",
                    help="and every program under win16ref/samples/")
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()
    files = inputs(a.files, a.all)
    if not files:
        print("t_w16cens: SKIP - no Windows program here (make win16fixtures, "
              "or the win16-reference skill's samples)")
        sys.exit(77)
    m = Machine()
    ran = 0
    for p in files:
        name = os.path.relpath(p, ROOT)
        data = open(p, "rb").read()
        if len(data) >= 0x10000:
            if a.v:
                print("  %s: %d bytes, over 64 KB - not loaded yet (R2)" % (name, len(data)))
            continue
        try:
            data, ne, want, cpu = host(p)
        except os88ne.NEError as e:
            if a.v:
                print("  %s: not an NE WIN16 loads (%s)" % (name, e))
            continue
        cf, l1, got = m.census(data, ne)
        ran += 1
        for n in sorted(want):
            g, w = got.get(n, set()), want[n]
            if not check(g == w, "%s segment %d: the guest walk reached the bytes "
                         "the host's did" % (name, n),
                         "%d bytes only the guest reached (first %s), %d only the "
                         "host (first %s)" % (len(g - w), hex(min(g - w)) if g - w else "-",
                                              len(w - g), hex(min(w - g)) if w - g else "-")):
                pass
        verdict = "8086" if not cf else REFUSAL.get(S.get("w16_r_c186"), "?")
        if cf:
            verdict = next((t for k, t in REFUSAL.items()
                            if rd(m.mu, IMG, k, 48).split(b"\0")[0].decode("latin-1") == l1), l1)
        eq(verdict, cpu, "%s: the guest's verdict is the host's" % name)
        if a.v:
            print("  %-48s %-6s %s" % (name, cpu, "run" if not cf else l1))
    check(ran > 0, "at least one program was checked")
    done("t_w16cens (%d programs)" % ran)


if __name__ == "__main__":
    main()
