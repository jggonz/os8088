#!/usr/bin/env python3
"""What a STATIC recompiler of a Game Boy game would find, and what it could save.

    python3 tools/gbstatic.py GAME.GB [--frames N] [--cov FILE] [--show N]

The instrument behind docs/reports/REALDMG-STATIC-2026-10-20.md, on the HOST.
It answers the two questions that decide whether translating a cartridge
AHEAD OF TIME is worth building beside RealDMG's dynamic recompiler
(REALDMG-SPEC section 16):

1. COVERAGE. A recursive-descent disassembler walks the ROM from the entry
   point, the RST and interrupt vectors, following every direct JR/JP/CALL
   and tracking the bank a call into 4000-7FFF lands in (a constant written
   to 2000-3FFF on the way). tools/gbprof/gbcover.c runs the game under
   Peanut-GB (gbprof's platformer script, the same one every RealDMG report
   uses) and records every instruction that RAN, by bank. What the
   disassembler cannot reach is what a static translator would hand to an
   interpreter at run time, and the edges into it say why: a JP HL, a RET
   that is not a return, a jump table behind an RST, a bank the walk could
   not know.

2. WHAT THE AHEAD-OF-TIME VIEW BUYS over translating a block at a time:
   flags written and never read (whole-program liveness can drop the LAHF,
   the OR AH,20h, the SAHF), memory accesses whose region a constant proves
   (the four compares a (HL) template carries), conditional branches that
   fall through (a block layout needs no jump there), and the dynamic
   instruction mix those are shares of. All are counted over the busy
   instructions only - the spin-waits and countdowns RealDMG skips are left
   out, gbprof's classifier.

No ROM is committed; the games are the user's (`$OS88_GB_ROMS`).
"""
import argparse
import collections
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gbprof  # noqa: E402

# --- SM83 lengths ----------------------------------------------------------
LEN = [1] * 256
for o in (0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36, 0x3E, 0x18, 0x20, 0x28, 0x30, 0x38,
          0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE, 0xE0, 0xF0, 0xE8, 0xF8, 0xCB, 0x10):
    LEN[o] = 2
for o in (0x01, 0x11, 0x21, 0x31, 0x08, 0xC2, 0xC3, 0xCA, 0xD2, 0xDA, 0xC4, 0xCC, 0xCD,
          0xD4, 0xDC, 0xEA, 0xFA):
    LEN[o] = 3
INVALID = {0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, 0xFD}
JR = {0x18, 0x20, 0x28, 0x30, 0x38}
JRCC = {0x20, 0x28, 0x30, 0x38}
JPCC = {0xC2, 0xCA, 0xD2, 0xDA}
CALLCC = {0xC4, 0xCC, 0xD4, 0xDC}
RETCC = {0xC0, 0xC8, 0xD0, 0xD8}
RSTS = {0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF}
ENDS = {0x18, 0xC3, 0xC9, 0xD9, 0xE9}            # no fall-through
CT = JR | JPCC | CALLCC | RETCC | RSTS | {0xC3, 0xCD, 0xC9, 0xD9, 0xE9}


class Rom:
    def __init__(self, data):
        self.d = data
        self.banks = max(1, len(data) // 0x4000)

    def byte(self, bank, pc):
        if pc < 0x4000:
            return self.d[pc]
        o = bank * 0x4000 + pc - 0x4000
        return self.d[o] if o < len(self.d) else 0xFF


def mbc_kind(rom):
    t = rom.d[0x147]
    return {0: 0, 1: 1, 2: 1, 3: 1, 5: 2, 6: 2, 0x0F: 3, 0x10: 3, 0x11: 3, 0x12: 3,
            0x13: 3}.get(t, 5 if 0x19 <= t <= 0x1E else 1)


def bank_of_write(rom, addr, val):
    """The ROM bank a write of VAL to ADDR selects, or None if it selects none."""
    k = mbc_kind(rom)
    if k == 0:
        return None
    if k == 2:                                   # MBC2: 0000-3FFF, address bit 8
        if addr < 0x4000 and addr & 0x100:
            return (val & 0x0F) or 1
        return None
    if not 0x2000 <= addr < 0x4000:
        return None
    if k == 5:
        return val if addr < 0x3000 else None   # the 9th bit is never set by these games
    b = val & (0x7F if k == 3 else 0x1F)
    return (b or 1) % rom.banks


def writes_a(op, cb):
    if op == 0xCB:
        return (cb & 7) == 7 and not 0x40 <= cb < 0x80
    return (op in (0x0A, 0x1A, 0x2A, 0x3A, 0xF0, 0xF2, 0xFA, 0x07, 0x0F, 0x17, 0x1F, 0x27,
                   0x2F, 0x3C, 0x3D, 0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xF1)
            or 0x78 <= op <= 0x7E or (0x80 <= op < 0xB8))


def discover(rom, extra=(), tables=True):
    """Recursive descent. Returns {(bank, pc): op} of instruction starts found,
    and the reasons a walk stopped: JP HL sites, RETs, RST sites whose routine
    pops its return address (a jump table follows - not code), and calls into
    4000-7FFF from bank 0 whose bank no constant decided."""
    found = {}
    stops = collections.Counter()
    unknown_bank = set()
    table_words = [0]
    jtabs = {}
    # an RST's routine that pops its return address is a jump-table dispatcher:
    # the bytes after the RST are data
    jt_rst = set()
    for v in range(0, 0x40, 8):
        pc = v
        pushes = 0
        for _ in range(12):
            op = rom.d[pc]
            if op in (0xC5, 0xD5, 0xE5, 0xF5):
                pushes += 1
            if op in (0xE1, 0xD1, 0xC1) and pc < v + 8:        # POP within the vector
                if not pushes:                                 # of the return address
                    jt_rst.add(0xC7 + v)
                    break
                pushes -= 1
            if op in (0xC3,):
                pc = rom.d[pc + 1] | rom.d[pc + 2] << 8
                if pc >= 0x4000:
                    break
                continue
            if op in CT or op in INVALID:
                break
            pc += LEN[op] if op != 0xCB else 2
    work = [(0, 0x100, None)] + [(0, v, None) for v in range(0x40, 0x68, 8)]
    work += [(b, pc, None) for b, pc in extra]
    while work:
        bank, pc, cb = work.pop()                # cb: the bank last written to 2000-3FFF
        acc = None                               # A, when a constant
        while True:
            if pc >= 0x8000:
                stops["into RAM"] += 1
                break
            key = (bank if pc >= 0x4000 else 0, pc)
            if key in found:
                break
            op = rom.byte(bank, pc)
            if op in INVALID:
                stops["invalid opcode"] += 1
                break
            n = LEN[op]
            found[key] = op
            a1 = rom.byte(bank, pc + 1)
            a2 = rom.byte(bank, pc + 2)
            imm = a1 | a2 << 8
            # a constant bank write: A known from an LD A,n earlier in this
            # straight line, then LD (nn),A to 2000-3FFF - the shape every
            # cartridge here uses (Super Mario Land puts an LDH between)
            if op == 0xEA and acc is not None:
                b = bank_of_write(rom, imm, acc)
                if b is not None:
                    cb = b
            if op == 0x3E:
                acc = a1
            elif op == 0xAF:
                acc = 0
            elif writes_a(op, a1) or op in CT:
                acc = None
            nxt = pc + n

            def go(t, newcb=cb):
                if t >= 0x8000:
                    stops["into RAM"] += 1
                    return
                if t >= 0x4000:
                    if pc < 0x4000:              # bank 0 into ROMX: which bank?
                        if newcb is None:
                            unknown_bank.add((pc, t))
                            for b in range(1, rom.banks):
                                work.append((b, t, None))
                            return
                        work.append((newcb, t, newcb))
                    else:
                        work.append((bank, t, cb))
                else:
                    work.append((0, t, newcb))

            if op in JR:
                d = a1 - 256 if a1 >= 128 else a1
                go(nxt + d)
                if op == 0x18:
                    break
            elif op in {0xC3} | JPCC:
                go(imm)
                if op == 0xC3:
                    break
            elif op == 0xCD or op in CALLCC:
                go(imm)
            elif op in RSTS:
                go(op & 0x38)
                if op in jt_rst:
                    stops["RST jump table"] += 1
                    if tables:                   # read the table: words while
                        t = nxt                  #   they are ROM code addresses
                        while True:              #   and not code already found
                            w = rom.byte(bank, t) | rom.byte(bank, t + 1) << 8
                            if not 0x150 <= w < 0x8000 or (key[0], t) in found or t >= 0x8000:
                                break
                            if (pc < 0x4000) != (t < 0x4000) and t >= 0x4000 and pc < 0x4000:
                                break
                            go(w)
                            jtabs.setdefault(key, []).append((bank if w >= 0x4000 else 0, w))
                            table_words[0] += 1
                            t += 2
                    break
            elif op in (0xC9, 0xD9):
                stops["RET"] += 1
                break
            elif op == 0xE9:
                stops["JP HL"] += 1
                break
            if pc < 0x4000 and nxt >= 0x4000:
                stops["runs into 4000"] += 1
                break
            pc = nxt
    stops["table words read"] = table_words[0]
    discover.jtabs = jtabs
    return found, stops, unknown_bank, jt_rst


FW = {}                                          # flag bits written / read: Z1 N2 H4 C8


def flags_of(op, cb):
    """(written, read) flag bits, gbcover.c's flags_of()."""
    if op == 0xCB:
        if cb < 0x40:
            return 15, 8 if (cb & 0xF0) == 0x10 else 0
        return (7, 0) if cb < 0x80 else (0, 0)
    if 0x80 <= op < 0xC0:
        return 15, 8 if (op & 0xF0) in (0x80, 0x90) and op & 8 else 0
    t = {0xC6: (15, 0), 0xD6: (15, 0), 0xE6: (15, 0), 0xEE: (15, 0), 0xF6: (15, 0), 0xFE: (15, 0),
         0xCE: (15, 8), 0xDE: (15, 8), 0x07: (15, 0), 0x0F: (15, 0), 0x17: (15, 8), 0x1F: (15, 8),
         0x09: (14, 0), 0x19: (14, 0), 0x29: (14, 0), 0x39: (14, 0), 0xE8: (15, 0), 0xF8: (15, 0),
         0x27: (13, 14), 0x2F: (6, 0), 0x37: (14, 0), 0x3F: (14, 8), 0xF1: (15, 0), 0xF5: (0, 15)}
    if op in t:
        return t[op]
    if op in (0x20, 0x28, 0xC2, 0xCA, 0xC4, 0xCC, 0xC0, 0xC8):
        return 0, 1
    if op in (0x30, 0x38, 0xD2, 0xDA, 0xD4, 0xDC, 0xD0, 0xD8):
        return 0, 8
    if (op & 0xC7) in (0x04, 0x05):
        return 7, 0
    return 0, 0


def supergraph(rom, found, jtabs):
    """Successors of every found instruction over the WHOLE program: a CALL or
    RST to its callee, a RET to every place a CALL or RST to a function that
    reaches it returns to (context-insensitive), a jump table to its entries,
    JP HL and a RET of no known function to None (anything)."""
    def key(bank, pc):
        return (bank if 0x4000 <= pc < 0x8000 else 0, pc)
    succ, calls = {}, collections.defaultdict(set)
    for (b, pc), op in found.items():
        cb = rom.byte(b, pc + 1)
        n = LEN[op]
        imm = rom.byte(b, pc + 1) | rom.byte(b, pc + 2) << 8
        nx = key(b, pc + n)
        out = []
        if op in JR:
            d = cb - 256 if cb >= 128 else cb
            out.append(key(b, pc + n + d))
            if op != 0x18:
                out.append(nx)
        elif op == 0xC3 or op in JPCC:
            out.append(key(b, imm))
            if op != 0xC3:
                out.append(nx)
        elif op == 0xCD or op in CALLCC:
            out.append(key(b, imm))
            calls[key(b, imm)].add(nx)
            if op in CALLCC:
                out.append(nx)
        elif op in RSTS:
            out.append((0, op & 0x38))
            if (b, pc) in jtabs:
                out += jtabs[(b, pc)]
            else:
                calls[(0, op & 0x38)].add(nx)
        elif op in (0xC9, 0xD9) or op in RETCC:
            out.append("RET")
            if op in RETCC:
                out.append(nx)
        elif op == 0xE9:
            out.append(None)
        elif op in (0x76, 0x10):
            out.append(nx)
        else:
            out.append(nx)
        succ[(b if pc >= 0x4000 else 0, pc)] = out
    # which functions reach each RET (intraprocedurally: calls are stepped over)
    rets = collections.defaultdict(set)
    for f in calls:
        seen, st = set(), [f]
        while st:
            k = st.pop()
            if k in seen or k not in found:
                continue
            seen.add(k)
            op = found[k]
            for t in succ[k]:
                if t == "RET":
                    rets[k] |= calls[f]
                elif t is None:
                    continue
                elif op == 0xCD or op in CALLCC or (op in RSTS and k not in jtabs):
                    pass
                else:
                    st.append(t)
            if op == 0xCD or op in CALLCC or (op in RSTS and k not in jtabs):
                n = LEN[op]
                st.append((k[0] if 0x4000 <= k[1] + n < 0x8000 else 0, k[1] + n))
    return succ, rets


def liveness(found, succ, rets, rom):
    """Flag bits live AFTER each instruction, whole-program, backward to a
    fixed point. Anything unknown (JP HL, a RET of no known caller, code not
    found) counts as reading every flag."""
    live_in = {k: 0 for k in found}
    def lin(t):
        if t is None or t not in found:
            return 15
        return live_in[t]
    def out_of(k):
        m = 0
        for t in succ[k]:
            if t == "RET":
                r = rets.get(k)
                if not r:
                    m |= 15
                for x in r or ():
                    m |= lin(x)
            else:
                m |= lin(t)
        return m
    changed = True
    while changed:
        changed = False
        for k, op in found.items():
            w, r = flags_of(op, rom.byte(k[0], k[1] + 1))
            v = (out_of(k) & ~w) | r
            if v != live_in[k]:
                live_in[k] = v
                changed = True
    return {k: out_of(k) for k in found}


def lookahead(found, succ, rom, k, depth):
    """Flag bits a JIT looking DEPTH instructions past the block's end could
    prove live after K: direct successors only, a RET or JP HL is unknown."""
    w0, _ = flags_of(found[k], rom.byte(k[0], k[1] + 1))
    need = 0
    st = [(t, w0, depth) for t in succ[k]]
    best = {}
    while st:
        t, pend, d = st.pop()
        if not pend or best.get((t, pend), -1) >= d:
            continue
        best[(t, pend)] = d
        if t == "RET" or t is None or t not in found or d == 0:
            need |= pend
            continue
        w, r = flags_of(found[t], rom.byte(t[0], t[1] + 1))
        need |= r & pend
        pend &= ~w
        for u in succ[t]:
            st.append((u, pend, d - 1))
    return need & w0


HL_OPS = {0x7E, 0x46, 0x4E, 0x56, 0x5E, 0x66, 0x6E, 0x2A, 0x3A, 0x34, 0x35, 0x86, 0x8E, 0x96, 0x9E,
          0xA6, 0xAE, 0xB6, 0xBE, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x77, 0x22, 0x32, 0x36}
HL_WR = {0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x77, 0x22, 0x32, 0x36, 0x34, 0x35}


def pointer_highs(found, succ, rom, block_only=False):
    """The high byte of HL, DE and BC where each (HL)/(DE)/(BC) access runs,
    when constant propagation knows it - forward over each function, a CALL
    or RST forgetting all three (the callee may change them), INC/DEC of the
    pair forgetting it (a loop walks the pointer). BLOCK_ONLY: what a
    translator that sees one block at a time knows - nothing at its head."""
    U = "?"                                       # unknown
    state = {}                                    # key -> (H, D, B) on entry
    preds = collections.Counter()
    for k in found:
        for t in succ[k]:
            if t not in (None, "RET"):
                preds[t] += 1
    heads = set(k for k in found if preds[k] != 1) | {(0, 0x100)}

    def step(k, st):
        op = found[k]
        b, pc = k
        a1 = rom.byte(b, pc + 1)
        a2 = rom.byte(b, pc + 2)
        H, D, B = st
        if op == 0x21: H = a2
        elif op == 0x11: D = a2
        elif op == 0x01: B = a2
        elif op == 0x26: H = a1
        elif op == 0x16: D = a1
        elif op == 0x06: B = a1
        elif 0x60 <= op <= 0x67: H = {0x60: B, 0x62: D, 0x64: H}.get(op, U)
        elif 0x50 <= op <= 0x57: D = {0x50: B, 0x52: D, 0x54: H}.get(op, U)
        elif 0x40 <= op <= 0x47: B = {0x40: B, 0x42: D, 0x44: H}.get(op, U)
        elif op in (0x23, 0x2B, 0x2A, 0x3A, 0x22, 0x32, 0x29, 0x09, 0x19, 0x39, 0xE1, 0xF8, 0x24, 0x25):
            H = U if op not in (0x24, 0x25) or H == U else (H + (1 if op == 0x24 else -1)) & 0xFF
        elif op in (0x13, 0x1B, 0xD1, 0x14, 0x15):
            D = U if op not in (0x14, 0x15) or D == U else (D + (1 if op == 0x14 else -1)) & 0xFF
        elif op in (0x03, 0x0B, 0xC1, 0x04, 0x05):
            B = U if op not in (0x04, 0x05) or B == U else (B + (1 if op == 0x04 else -1)) & 0xFF
        elif op == 0xCB and (a1 & 7) in (0, 2, 4) and not 0x40 <= a1 < 0x80:
            r = a1 & 7
            H, D, B = (U if r == 4 else H), (U if r == 2 else D), (U if r == 0 else B)
        if op in (0xCD,) or op in CALLCC or op in RSTS:
            H = D = B = U
        return (H, D, B)
    # forward to a fixed point; a block head starts unknown when block_only
    ins = {k: None for k in found}
    for k in found:
        if block_only and k in heads:
            ins[k] = (U, U, U)
    ins[(0, 0x100)] = (U, U, U)
    for v in range(0, 0x68, 8):
        if (0, v) in ins:
            ins[(0, v)] = (U, U, U)
    for k, op in found.items():                   # every call target, table entry
        for t in succ[k]:
            if t not in (None, "RET") and t in found and (op in (0xCD,) or op in CALLCC or op in RSTS):
                ins[t] = (U, U, U)
    work = [k for k in found if ins[k] is not None]
    while work:
        k = work.pop()
        out = step(k, ins[k])
        op = found[k]
        for t in succ[k]:
            if t in (None, "RET") or t not in found:
                continue
            if op in (0xCD,) or op in CALLCC or op in RSTS:
                n = LEN[op]
                if t[1] != (k[1] + n):            # the callee: entered unknown above
                    continue
            if block_only and t in heads:
                continue
            old = ins[t]
            new = out if old is None else tuple(x if x == y else U for x, y in zip(old, out))
            if new != old:
                ins[t] = new
                work.append(t)
        if op in (0xCD,) or op in CALLCC or op in RSTS:   # and the return site
            t = (k[0] if 0x4000 <= k[1] + LEN[op] < 0x8000 else 0, k[1] + LEN[op])
            if t in found and ins[t] != (U, U, U) and not (block_only and t in heads):
                ins[t] = (U, U, U)
                work.append(t)
    return ins


def load_cov(path):
    I, X, E = {}, set(), collections.Counter()
    nf = 1
    for ln in open(path):
        p = ln.split()
        if p[0] == "F":
            nf = int(p[1])
        elif p[0] == "I":
            v = [int(x, 16) for x in p[1:5]] + [int(x) for x in p[5:]]
            I[(v[0], v[1])] = v[2:]
        elif p[0] == "X":
            X.add((int(p[1], 16), int(p[2], 16)))
        elif p[0] == "E":
            E[(int(p[1], 16), int(p[2], 16), int(p[3], 16), int(p[4], 16))] += int(p[5])
    return nf, I, X, E


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("rom")
    ap.add_argument("--frames", type=int, default=7200)
    ap.add_argument("--cov", help="read this gbcover output instead of running")
    ap.add_argument("--show", type=int, default=12, help="list the N hottest unfound entries")
    a = ap.parse_args()
    data = open(a.rom, "rb").read()
    rom = Rom(data)
    cov = a.cov
    if not cov:
        gbprof.patch(gbprof.fetch())
        exe = os.path.join(gbprof.WORK, "gbcover")
        subprocess.check_call([os.environ.get("CC", "cc"), "-O2", "-w", "-I", gbprof.WORK, "-o",
                               exe, os.path.join(ROOT, "tools", "gbprof", "gbcover.c")])
        cov = os.path.join(gbprof.WORK, os.path.basename(a.rom) + ".cov")
        subprocess.check_call([exe, a.rom, str(a.frames), cov])
    nf, I, X, E = load_cov(cov)
    # gbcover counts a site's accesses, flags and edges over every execution;
    # weight them to its BUSY ones, so a skipped spin-wait counts for nothing
    for k, r in I.items():
        f = r[3] / r[2] if r[2] else 0
        for i in range(5, len(r)):
            r[i] = r[i] * f
    for e in list(E):
        r = I.get((e[0], e[1]))
        E[e] = E[e] * (r[3] / r[2]) if r and r[2] else E[e]
    name = os.path.basename(a.rom)
    print("%s: %d KB, %d banks, MBC kind %d; %d frames measured (%.0f s of Game Boy)"
          % (name, len(data) // 1024, rom.banks, mbc_kind(rom), nf, nf / 59.7275))

    # field indices in an I record after (bank, pc)
    OP, CB, ALL, BUSY, CYC, RDF, RDS, WRF, WRS = range(9)
    busy = sum(r[BUSY] for r in I.values())
    allx = sum(r[ALL] for r in I.values())
    ram = sum(r[BUSY] for k, r in I.items() if k[0] == 0x1FF)
    print("instructions a frame: %.0f run, %.0f busy (the rest spin-waits and countdowns "
          "RealDMG skips); %.1f busy a frame from RAM" % (allx / nf, busy / nf, ram / nf))

    # 1. coverage --------------------------------------------------------
    ran = {k for k in I if k[0] != 0x1FF} | {k for k in X if k[0] != 0x1FF}
    for label, extra, tables in (("the disassembler alone, jump tables not read", (), False),
                         ("the disassembler reading RST jump tables", (), True),
                         ("plus the targets of JP HL and RET seen running", None, True)):
        if extra is None:
            extra = set()
            for (sb, sp, db, dp), n in E.items():
                if sb == 0xFFFF or db == 0x1FF:
                    continue
                src = I.get((sb, sp))
                if src and src[OP] in {0xE9, 0xC9, 0xD9} | RETCC:
                    extra.add((db, dp))
        found, stops, unk, jt = discover(rom, extra, tables)
        fk = set(found)
        miss = ran - fk
        wmiss = sum(I[k][BUSY] for k in miss if k in I)
        print("\nCOVERAGE, %s: %d instruction starts found; of %d sites that ran, %d not "
              "found = %.2f%% of busy executions" % (label, len(fk), len(ran), len(miss),
                                                      100.0 * wmiss / max(busy, 1)))
        print("  walks stopped at: " + ", ".join("%s %d" % kv for kv in stops.most_common()))
        print("  RSTs read as jump tables: %s; bank-0 calls into 4000-7FFF of no known bank: %d"
              % (" ".join("%02X" % o for o in sorted(jt)) or "none", len(unk)))
        # how the missed code is entered: edges from found or ran code into it
        why = collections.Counter()
        hot = collections.Counter()
        for (sb, sp, db, dp), n in E.items():
            if (db, dp) in miss and (sb == 0xFFFF or (sb, sp) in fk):
                op = I.get((sb, sp), [None])[0] if sb != 0xFFFF else None
                kind = ("interrupt" if sb == 0xFFFF else "JP HL" if op == 0xE9 else
                        "RET" if op in {0xC9, 0xD9} | RETCC else
                        "RST" if op in RSTS else "JP/CALL/JR" if op is not None else "?")
                why[kind] += n
                hot[(db, dp, kind)] += n
        if why:
            print("  entered from found code (edges a frame): " + ", ".join(
                "%s %.1f" % (k, n / nf) for k, n in why.most_common()))
        for (db, dp, kind), n in hot.most_common(a.show if tables else 0):
            sub = sum(I[k][BUSY] for k in miss if k in I and k[0] == db and 0 <= k[1] - dp < 64)
            print("    %02X:%04X via %-10s %7.1f a frame (~%.0f busy instructions a frame near it)"
                  % (db, dp, kind, n / nf, sub / nf))

    # 2. what an ahead-of-time view buys ------------------------------------
    print("\nTHE DYNAMIC MIX (busy, a frame):")
    def per(f):
        return sum(r[BUSY] for r in I.values() if f(r)) / nf
    cond = per(lambda r: r[OP] in JRCC | JPCC | CALLCC | RETCC)
    uncond = per(lambda r: r[OP] in {0x18, 0xC3, 0xCD} | RSTS)
    rets = per(lambda r: r[OP] in (0xC9, 0xD9))
    jphl = per(lambda r: r[OP] == 0xE9)
    print("  conditional transfers %.0f, JR/JP/CALL/RST %.0f, RET/RETI %.0f, JP HL %.1f"
          % (cond, uncond, rets, jphl))
    taken = nt = 0
    for (sb, sp, db, dp), n in E.items():
        r = I.get((sb, sp))
        if r and r[OP] in JRCC | JPCC | CALLCC | RETCC:
            ln = LEN[r[OP]]
            if dp == (sp + ln) & 0xFFFF and r[OP] not in RETCC:
                nt += n
            elif r[OP] in RETCC and dp == (sp + 1) & 0xFFFF:
                nt += n
            else:
                taken += n
    print("  of the conditionals: %.0f taken, %.0f fall through (%.0f%%)"
          % (taken / nf, nt / nf, 100.0 * nt / max(taken + nt, 1)))
    # returns that are not to the instruction after a CALL or RST
    odd = tot = 0
    for (sb, sp, db, dp), n in E.items():
        r = I.get((sb, sp))
        if r and r[OP] in {0xC9, 0xD9} | RETCC and not (r[OP] in RETCC and dp == sp + 1):
            tot += n
            p3 = I.get((db, (dp - 3) & 0xFFFF)) or ([rom.byte(db, dp - 3)] if db != 0x1FF else None)
            p1 = I.get((db, (dp - 1) & 0xFFFF)) or ([rom.byte(db, dp - 1)] if db != 0x1FF else None)
            ok = (p3 and p3[0] in {0xCD} | CALLCC) or (p1 and p1[0] in RSTS)
            if not ok:
                odd += n
    print("  RETs a frame %.0f, of which %.1f do not return after a CALL/RST" % (tot / nf, odd / nf))

    fl = [0] * 5
    for r in I.values():
        for i in range(5):
            fl[i] += r[9 + i]
    tf = sum(fl)
    print("\nFLAGS: %.0f flag-writing instructions a frame; their flags read for: nothing %.1f%%, "
          "Z only %.1f%%, C only %.1f%%, Z and C %.1f%%, N or H %.1f%%"
          % (tf / nf, *(100.0 * x / max(tf, 1) for x in fl)))

    # what a compiler can PROVE about those flags, against what happened
    found2, _, _, _ = discover(rom, (), True)
    succ, rets = supergraph(rom, found2, discover.jtabs)
    lout = liveness(found2, succ, rets, rom)

    def cls(m):
        return 0 if not m else 4 if m & 6 else 1 if m == 1 else 2 if m == 8 else 3
    names = ("nothing", "Z only", "C only", "Z and C", "N or H")
    for label, need in (("whole-program", lambda k: lout[k]),
                        ("a JIT looking 16 instructions on", lambda k: lookahead(found2, succ, rom, k, 16)),
                        ("a JIT looking 4 instructions on", lambda k: lookahead(found2, succ, rom, k, 4))):
        got = [0.0] * 5
        unsound = 0.0
        for k, r in I.items():
            n = sum(r[9:14])
            if not n or k not in found2:
                continue
            w, _ = flags_of(r[OP], r[CB])
            m = need(k) & w
            got[cls(m)] += n
            dyn = r[9:14]
            # a proof weaker than what happened is fine; one STRONGER is a bug
            if cls(m) == 0 and sum(dyn[1:]) > 0:
                unsound += sum(dyn[1:])
        t = sum(got)
        print("  proved needed, %s: %s%s" % (label, ", ".join(
            "%s %.1f%%" % (names[i], 100 * got[i] / max(t, 1)) for i in range(5)),
            "" if not unsound else "  UNSOUND on %.0f a frame" % (unsound / nf)))
    def fast(hi, write):
        return hi != "?" and ((0x80 <= hi <= 0xFE) or (not write and hi < 0x40))
    for label, bo in (("whole functions", False), ("one block at a time", True)):
        ins = pointer_highs(found2, succ, rom, bo)
        tot = prov = 0.0
        for k, r in I.items():
            op = r[OP]
            if k not in found2:
                continue
            H, D, B = ins.get(k) or ("?", "?", "?")   # reached by no known path
            if op in HL_OPS or (op == 0xCB and (r[CB] & 7) == 6):
                hi, wr = H, op in HL_WR or (op == 0xCB and r[CB] >= 0x80)
            elif op in (0x0A, 0x02):
                hi, wr = B, op == 0x02
            elif op in (0x1A, 0x12):
                hi, wr = D, op == 0x12
            else:
                continue
            tot += r[BUSY]
            if fast(hi, wr):
                prov += r[BUSY]
        print("POINTERS: (HL)/(DE)/(BC) accesses whose region constant propagation proves, "
              "%s: %.0f of %.0f a frame (%.0f%%)" % (label, prov / nf, tot / nf, 100 * prov / max(tot, 1)))
    memrd = [0, 0]
    sites = collections.Counter()
    for k, r in I.items():
        op = r[OP]
        hl = op in (0x7E, 0x46, 0x4E, 0x56, 0x5E, 0x66, 0x6E, 0x2A, 0x3A, 0x34, 0x35, 0x86, 0x8E,
                    0x96, 0x9E, 0xA6, 0xAE, 0xB6, 0xBE, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x77,
                    0x22, 0x32, 0x36) or (op == 0xCB and (r[CB] & 7) == 6)
        de = op in (0x0A, 0x1A, 0x02, 0x12)
        if hl or de:
            kind = "(HL)" if hl else "(BC)/(DE)"
            fast = r[RDF] + r[WRF]
            slow = r[RDS] + r[WRS]
            sites[(kind, "always fast" if not slow else "never fast" if not fast else "mixed")] += r[BUSY]
            memrd[0] += fast
            memrd[1] += slow
    print("MEMORY through a register pair, busy executions a frame by site: " + ", ".join(
        "%s %s %.0f" % (k[0], k[1], n / nf) for k, n in sorted(sites.items())))
    print("  accesses: %.0f fast path, %.0f slow (ROMX, an I/O register, the MBC)"
          % (memrd[0] / nf, memrd[1] / nf))

    # static size: what the ROM's code would be translated into
    print("\nCODE: %d instruction starts found statically (the last walk, seeded), %d bytes of Game Boy code; %d sites "
          "ever ran (%d before the measured frames only)" % (
              len(found), sum(LEN[o] if o != 0xCB else 2 for o in found.values()),
              len(ran), len(X)))


if __name__ == "__main__":
    main()
