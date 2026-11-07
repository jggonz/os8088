"""Equivalence of two builds of kernel/lz.inc under unicorn: same CF, same DI on
success, same bytes everywhere in the output arena (poisoned), same DS/ES, on a
corpus of real streams, in-place placements, and hostile mutations.
Also counts instructions executed (a WORK count, not a time)."""
import os, sys, random, struct, subprocess, tempfile, glob
from unicorn import *
from unicorn.x86_const import *
S = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, S)
ROOT = os.getcwd(); sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88lz

def build(incdir):
    out = tempfile.mktemp(suffix=".bin", dir=os.path.join(S, ".."))
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-I", incdir + "/",
                           "-o", out, os.path.join(S, "lzharn.asm")])
    b = open(out, "rb").read(); os.unlink(out)
    return b

CODESEG = 0x1000          # harness at 1000:0
STK = 0x9000              # SS
class Dec:
    def __init__(self, binary):
        self.bin = binary
        self.big_off = None
    def run(self, src_lin, srclen, out_seg, explen, fmt, mem_init, big=False, ds=None, si=None, count=False):
        mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        for addr, data in mem_init:
            mu.mem_write(addr, data)
        mu.mem_write(CODESEG * 16, self.bin)
        # 'big' flag byte: find it as the byte after the second HLT... simpler: patch
        mu.mem_write(CODESEG * 16 + self.bigoff(), bytes([1 if big else 0]))
        if ds is None:
            ds = src_lin >> 4; si = src_lin & 15
        mu.reg_write(UC_X86_REG_CS, CODESEG); mu.reg_write(UC_X86_REG_IP, 0)
        mu.reg_write(UC_X86_REG_SS, STK); mu.reg_write(UC_X86_REG_SP, 0xFFF0)
        mu.reg_write(UC_X86_REG_DS, ds); mu.reg_write(UC_X86_REG_SI, si)
        mu.reg_write(UC_X86_REG_ES, out_seg); mu.reg_write(UC_X86_REG_DI, 0)
        if big:
            mu.reg_write(UC_X86_REG_CX, srclen & 0xFFFF)
            mu.reg_write(UC_X86_REG_AX, ((srclen >> 16) << 8) | fmt)
        else:
            mu.reg_write(UC_X86_REG_CX, srclen)
            mu.reg_write(UC_X86_REG_AX, 0x5500 | fmt)
        mu.reg_write(UC_X86_REG_BX, explen >> 16); mu.reg_write(UC_X86_REG_DX, explen & 0xFFFF)
        mu.reg_write(UC_X86_REG_BP, 0x1234)
        n = [0]
        if count:
            def hk(uc, a, sz, ud): n[0] += 1
            mu.hook_add(UC_HOOK_CODE, hk)
        end = CODESEG * 16 + 0x10000
        try:
            mu.emu_start(CODESEG * 16, end, timeout=20_000_000, count=200_000_000)
        except UcError as e:
            return ("ERR", str(e))
        ip = mu.reg_read(UC_X86_REG_IP)
        fl = mu.reg_read(UC_X86_REG_EFLAGS)
        regs = tuple(mu.reg_read(r) for r in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SP, UC_X86_REG_BP))
        di = mu.reg_read(UC_X86_REG_DI)
        cf = fl & 1
        mem = bytes(mu.mem_read(0x20000, 0x60000))
        return (cf, di if not cf else None, regs, ip, mem, n[0])
    def bigoff(self):
        if self.big_off is None:
            # entry: 2E 80 3E <off16> 00 -> cmp byte [cs:big],0
            self.big_off = struct.unpack_from("<H", self.bin, 3)[0]
        return self.big_off

def corpus():
    sys.path.insert(0, S)
    import pkgstreams
    for name, fid, st, n in pkgstreams.streams():
        yield name, fid, st, n
    # CZ files in build (data files packed for the shipped disks)
    for p in sorted(glob.glob("build/**/*", recursive=True)):
        if not os.path.isfile(p) or os.path.getsize(p) > 200000: continue
        try:
            b = open(p, "rb").read(6)
        except Exception: continue
        if b[:2] == b"CZ":
            try:
                fmt, stream, outlen = parse_cz(open(p, "rb").read())
                yield os.path.basename(p), fmt, stream, outlen
            except Exception:
                pass

def parse_cz(blob):
    h = os88lz.cz_parse(blob)
    return h

def main():
    base = Dec(build(sys.argv[1])); tip = Dec(build(sys.argv[2]))
    rnd = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    nfuzz = int(sys.argv[4]) if len(sys.argv) > 4 else 20
    cases = 0; bad = 0; ib = it = 0
    def cmp(tag, *a, **k):
        nonlocal cases, bad, ib, it
        rb = base.run(*a, count=True, **k); rt = tip.run(*a, count=True, **k)
        cases += 1
        if rb[0] == "ERR" or rt[0] == "ERR":
            if rb[0] != rt[0]:
                bad += 1; print("DIFF(err)", tag, rb[:2], rt[:2])
            return rb
        if rb[:5] != rt[:5]:
            bad += 1
            d = next((i for i in range(len(rb[4])) if rb[4][i] != rt[4][i]), None)
            print("DIFF", tag, "cf", rb[0], rt[0], "di", rb[1], rt[1], "regs", rb[2], rt[2], "first mem diff at", hex(0x20000 + d) if d is not None else None)
        ib += rb[5]; it += rt[5]
        return rb
    poison = [(0x20000, b"\xCC" * 0x60000)]
    real = list(corpus())
    print("real streams:", len(real))
    for name, fid, st, n in real:
        # 1) separate buffers: source at 0x70000, output at 0x2000:0
        r = cmp(name + " sep", 0x70000, len(st), 0x2000, n, fid, poison + [(0x70000, st)])
        if r[0] != 0: print("  base refuses real stream", name)
        # 2) in place, the loader's way: output at 0x3000:0, source R above
        R = (n - len(st) + 511) & ~511
        if R < 0: R = 0
        src = 0x30000 + R
        cmp(name + " inplace", src, len(st), 0x3000, n, fid, poison + [(src, st)])
        # 3) declared length wrong (+1/-1) -> must refuse identically
        cmp(name + " len-1", 0x70000, len(st), 0x2000, n - 1, fid, poison + [(0x70000, st)])
        cmp(name + " len+1", 0x70000, len(st), 0x2000, n + 1, fid, poison + [(0x70000, st)])
        # 4) truncated
        for cut in (1, 2, 3, len(st) // 2):
            if cut < len(st):
                cmp(name + " trunc%d" % cut, 0x70000, len(st) - cut, 0x2000, n, fid, poison + [(0x70000, st)])
        # 5) mutations
        for k in range(nfuzz):
            m = bytearray(st)
            for _ in range(rnd.randint(1, 4)):
                i = rnd.randrange(len(m))
                m[i] = rnd.randrange(256) if rnd.random() < 0.5 else m[i] ^ (1 << rnd.randrange(8))
            cmp(name + " fuzz%d" % k, 0x70000, len(m), 0x2000, n, fid, poison + [(0x70000, bytes(m))])
        # 6) odd output segment offset (unaligned es base irrelevant; vary DI? DI must be 0)
    # big outputs crossing 64KB: synthesize
    for fid in (os88lz.LZ4, os88lz.LZB):
        for seed in range(3):
            r2 = random.Random(seed)
            data = bytearray()
            while len(data) < 70000 + seed * 9000:
                if r2.random() < 0.3:
                    data += bytes(r2.randrange(256) for _ in range(r2.randrange(1, 40)))
                else:
                    k = r2.randrange(1, 300)
                    if len(data) > k:
                        st0 = len(data) - r2.randrange(1, min(len(data), 60000))
                        data += data[st0:st0 + r2.randrange(4, 80)]
            data = bytes(data)
            st = os88lz.compress(data, fid) if fid == os88lz.LZ4 else os88lz.compress(data, fid)
            if len(st) >= 0x10000:
                cmp("big%d-%d" % (fid, seed), 0x70000, len(st), 0x2000, len(data), fid | 0x80, poison + [(0x70000, st)], big=False)
            else:
                cmp("big%d-%d" % (fid, seed), 0x70000, len(st), 0x2000, len(data), fid, poison + [(0x70000, st)])
            for k in range(nfuzz):
                m = bytearray(st)
                i = rnd.randrange(len(m)); m[i] ^= 1 << rnd.randrange(8)
                cmp("big%d-%d fuzz%d" % (fid, seed, k), 0x70000, len(m), 0x2000, len(data), fid, poison + [(0x70000, bytes(m))])
    # LONG LITERAL RUNS (LZ4's .litx 16KB pieces) and runs of one byte value
    for seed in range(3):
        r3 = random.Random(100 + seed)
        noise = bytes(r3.randrange(256) for _ in range(17000 + 9000 * seed))
        data = b"ABCD" * 40 + noise + bytes(500) + noise[:3000] + b"U" * 300
        for fid in (os88lz.LZ4, os88lz.LZB):
            st = os88lz.compress(data, fid)
            cmp("longlit%d-%d" % (fid, seed), 0x70000, len(st), 0x2000, len(data), fid, poison + [(0x70000, st)])
            R = (len(data) - len(st) + 511) & ~511
            src = 0x30000 + R
            cmp("longlit%d-%d inplace" % (fid, seed), src, len(st), 0x3000, len(data), fid, poison + [(src, st)])
            for k in range(nfuzz):
                m = bytearray(st)
                i = rnd.randrange(min(len(m), 64)); m[i] ^= 1 << rnd.randrange(8)
                cmp("longlit%d-%d fuzz%d" % (fid, seed, k), 0x70000, len(m), 0x2000, len(data), fid, poison + [(0x70000, bytes(m))])
    # garbage
    for k in range(200):
        g = bytes(rnd.randrange(256) for _ in range(rnd.randrange(2, 300)))
        cmp("garbage%d" % k, 0x70000, len(g), 0x2000, rnd.randrange(0, 2000), rnd.randrange(2), poison + [(0x70000, g)])
    print("cases %d, DIFFERENT %d; instructions base %d tip %d (%.1f%%)" % (cases, bad, ib, it, 100.0 * it / max(ib, 1)))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
