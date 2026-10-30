"""RealDMG precompiler: STATIC BOUNDS for two ideas, over a game's precompiled blocks
weighted by its heat (docs/reports/REALDMG-AOT-BOUNDS-2026-10-27.md): (1) dead flags across
block exits, (2) block checks a superblock could fold. Usage: aotbounds.py ROM AOTDIR HEAT.json...
Host-side, on demand: nothing in make runs it."""
import sys, os, struct, collections
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools")); sys.path.insert(0, os.path.join(ROOT, "apps", "realdmg"))
import gbaot as A, rdggen
rom_path, d, heatfiles = sys.argv[1], sys.argv[2], sys.argv[3:]
tab = open(os.path.join(ROOT, "build", "gbaot", "rdgtmpl.bin"), "rb").read()
import rdgtmpl
FLG = rdgtmpl.HEAD
data = open(rom_path, "rb").read()
meta, segs, table = A.load(d)
starts = set()
for tb in (table, meta["list"]):
    for e in range(0, len(tb), 8):
        pc, key, c, off, sg = struct.unpack_from("<HBBHH", tb, e)
        if off:
            starts.add((pc, key))
hram = {}
heat, more = A.load_heat(heatfiles, hram)
def byte(key, pc):
    if pc < 0x4000: return data[pc]
    if pc < 0x8000:
        o = key * 0x4000 + (pc - 0x4000)
        return data[o] if o < len(data) else 0
    return None
def rec(i):
    off = struct.unpack_from("<H", tab, i * 2)[0]
    return off
def fmask(key, pc):
    op = byte(key, pc)
    if op is None: return None
    if op == 0xCB:
        cb = byte(key, pc + 1)
        w, r = rdggen.flag_masks_cb(cb)
        return op, cb, w, r, rec(256 + cb), 2
    w, r = rdggen.flag_masks(op)
    ln = 3 if op in rdggen.IMM16_OPS else 2 if op in rdggen.IMM8_OPS else 1
    return op, None, w, r, rec(op), ln
CT = {0x18, 0x20, 0x28, 0x30, 0x38, 0xC3, 0xC2, 0xCA, 0xD2, 0xDA, 0xCD, 0xC4, 0xCC, 0xD4, 0xDC,
      0xC9, 0xC0, 0xC8, 0xD0, 0xD8, 0xD9, 0xE9} | {0xC7 + 8 * k for k in range(8)}
CONDR = {0x20: 1, 0x28: 1, 0x30: 8, 0x38: 8, 0xC2: 1, 0xCA: 1, 0xD2: 8, 0xDA: 8,
         0xC4: 1, 0xCC: 1, 0xD4: 8, 0xDC: 8, 0xC0: 1, 0xC8: 1, 0xD0: 8, 0xD8: 8}
def kkey(key, pc):
    return 0xFF if pc < 0x4000 else key
def succ(key, pc, op, ln):
    """targets of a control transfer, or None when unknown"""
    nxt = pc + ln
    if op in (0x18, 0x20, 0x28, 0x30, 0x38):
        e = byte(key, pc + 1); e = e - 256 if e >= 128 else e
        t = (nxt + e) & 0xFFFF
        return [t] + ([nxt] if op != 0x18 else [])
    if op in (0xC3, 0xC2, 0xCA, 0xD2, 0xDA, 0xCD, 0xC4, 0xCC, 0xD4, 0xDC):
        t = byte(key, pc + 1) | byte(key, pc + 2) << 8
        uncond = op in (0xC3, 0xCD)
        return [t] + ([] if uncond else [nxt])
    return None
def look(key, pc, live, n, cross, depth=0):
    """CF: True when every path overwrites `live` before reading it"""
    while True:
        if n <= 0 or pc >= 0x8000: return False
        if (pc, kkey(key, pc)) in starts and depth > 0 and not cross: return False   # block end
        if (pc, kkey(key, pc)) in starts and depth > 0: pass
        f = fmask(key, pc)
        if f is None: return False
        op, cb, w, r, ri, ln = f
        if op in CT:
            if not cross: return False
            rd = CONDR.get(op, 0)
            if rd & live: return False
            ts = succ(key, pc, op, ln)
            if ts is None: return False
            return all(look(key, t, live, n - 1, cross, depth + 1) for t in ts)
        if r & live: return False
        live &= ~w & 0xF
        if not live: return True
        if ri == 0: return False
        if tab[ri + 4] & (1 + 2 + 4 + 16 + 64): return False
        pc += ln; n -= 1; depth += 1
def blocks_instrs(key, pc, cap=64):
    out = []
    first = True
    while cap:
        if pc >= 0x8000: break
        if not first and (pc, kkey(key, pc)) in starts: break
        first = False
        f = fmask(key, pc)
        if f is None: break
        out.append((pc, f))
        if f[0] in CT: break
        pc += f[5]; cap -= 1
    return out
NOF = set(rdggen.NOF_OPS); CBN = set(rdggen.CB_NOF)
SAVE = {}   # x86 instructions a second record saves: INC/DEC r ~3, ADD HL ~5, CB ~3
tot = dead0 = dead1 = dead1b = 0; nopf = 0
saved0 = saved1 = 0
for (pc, key), c in heat.items():
    if (pc, key) not in starts or pc >= 0x8000: continue
    for ipc, f in blocks_instrs(key, pc):
        op, cb, w, r, ri, ln = f
        if not ((cb is None and op in NOF and op not in (0x34, 0x35)) or (cb is not None and cb in CBN)):
            continue
        s = 5 if (cb is None and op & 0xF == 9) else 3
        tot += c
        if look(key, ipc + ln, w, 6, False, 1): dead0 += c; saved0 += c * s
        elif look(key, ipc + ln, w, 6, True, 1): dead1 += c; saved1 += c * s
        elif look(key, ipc + ln, w, 12, True, 1): dead1b += c
print("%s: %d executions of an op with a second record; dead in-block %.1f%%, +across exits %.1f%% (+%.1f%% at 12)"
      % (rom_path.split('/')[-1], tot, 100.0 * dead0 / max(tot, 1), 100.0 * dead1 / max(tot, 1), 100.0 * dead1b / max(tot, 1)))
print("  x86 instructions saved, est.: in-block %d, across exits %d more" % (saved0, saved1))
# (2) chains: a block's executions where it and its static successor chain hold no
# access that may read the time (any memory read/write that is not a known HRAM/WRAM
# constant: (HL), (BC), (DE), (C), LDH (n) with n < 80h, POP? (SP is RAM) treated safe)
SAFE_MEM = set()
def timing(key, pc, f):
    op, cb, w, r, ri, ln = f
    if cb is not None:
        return (cb & 7) == 6                       # (HL)
    if op in (0x0A, 0x1A, 0x02, 0x12, 0x22, 0x2A, 0x32, 0x3A, 0x34, 0x35, 0x36, 0xF2, 0xE2): return True
    if 0x40 <= op < 0x80 and (op & 7 == 6 or (op >> 3) & 7 == 6) and op != 0x76: return True
    if op in (0x86, 0x8E, 0x96, 0x9E, 0xA6, 0xAE, 0xB6, 0xBE): return True
    if op in (0xF0, 0xE0):
        return byte(key, pc + 1) < 0x80
    if op in (0xFA, 0xEA):
        a = byte(key, pc + 1) | byte(key, pc + 2) << 8
        return a >= 0xFF00 and a < 0xFF80
    if op in (0x76, 0x10, 0xFB): return True
    return False
blk = collections.Counter(); totb = 0
for (pc, key), c in heat.items():
    if (pc, key) not in starts or pc >= 0x8000: continue
    ins = blocks_instrs(key, pc)
    totb += c
    blk["timing-free" if not any(timing(key, ip, f) for ip, f in ins) else "has a possible time read"] += c
print("  block executions: %s" % ", ".join("%s %.1f%%" % (k, 100.0 * v / totb) for k, v in blk.items()))
# chains: a timing-free block B entered from a timing-free predecessor P that
# links straight to it (fall-through, JR/JP/CALL target): B's end check could be
# folded into a superblock headed by P's chain. Upper bound by B's heat.
term = {}
tf = {}
for (pc, key), c in heat.items():
    if (pc, key) not in starts or pc >= 0x8000: continue
    ins = blocks_instrs(key, pc)
    tf[(pc, key)] = bool(ins) and not any(timing(key, ip, f) for ip, f in ins) \
        and not any(f[0] in (0xEA,) and byte(key, ip + 2) < 0x80 for ip, f in ins)
    if not ins: continue
    lp, lf = ins[-1]
    op, cb, w, r, ri, ln = lf
    if op in CT:
        ts = succ(key, lp, op, ln) or []
    else:
        ts = [lp + ln]
    term[(pc, key)] = [(t, kkey(key, t)) for t in ts if t < 0x8000]
preds = collections.defaultdict(list)
for p, ts in term.items():
    for t in ts:
        preds[t].append(p)
rem = 0
for b, c in heat.items():
    if not tf.get(b): continue
    if any(tf.get(p) for p in preds.get(b, [])):
        rem += c
print("  block executions whose end check a superblock could fold: <= %.1f%%" % (100.0 * rem / max(totb, 1)))
