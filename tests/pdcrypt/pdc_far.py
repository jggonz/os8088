#!/usr/bin/env python3
"""pdc_far.py - the PDC_FARST build of pdcrc4.inc / pdcaes.inc (PDENG's: RC4 states, the AES schedule and Td0
live in a far area, ES = _pdc_cseg) held to the host twin on random keys, chains and lengths; ES must come back
as it went in (pdc_asm's per-call check)."""
import os, sys, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pdc_asm, pdc_twin

TBL, RK, RCS = 0, 1024, 1264          # offsets in the area (segment ESEG): Td0, the schedule, RC4 states

class Far(pdc_asm.Asm):
    def __init__(self, measure=False):
        super().__init__(defs=("PDC_FARST", "PDC_NOENC"), measure=measure)
        self.w(self.addr["_pdc_cseg"], pdc_asm.ESEG.to_bytes(2, "little"))
        self.call("_pdc_aes_tdnew", [])
    def aw(self, off, d): self.uc.mem_write((pdc_asm.ESEG << 4) + off, bytes(d))
    def ar(self, off, n): return bytes(self.uc.mem_read((pdc_asm.ESEG << 4) + off, n))
    def ks(self, key, nk):
        B = pdc_asm.BUF; self.w(B, key); self.call("_pdc_aes_ks", [RK, B, nk]); return self.ar(RK, 16 * (nk + 7))
    def dks(self, nr): self.call("_pdc_aes_dks", [RK, nr]); return self.ar(RK, 16 * (nr + 1))
    def cbc_dec(self, iv, data, nr):
        B = pdc_asm.BUF; self.w(B, iv); self.w(B + 32, data)
        self.call("_pdc_cbc_dec", [RK, nr, B, B + 32, len(data) // 16]); return self.r(B + 32, len(data)), self.r(B, 16)
    def rc4(self, key, data, slot=0):
        B = pdc_asm.BUF; self.w(B, key); s = RCS + 258 * slot
        self.call("_pdc_rc4_ks", [s, B, len(key)]); out = b""
        for i in range(0, len(data), 255):
            c = data[i:i + 255]; self.w(B + 64, c); self.call("_pdc_rc4", [s, B + 64, len(c)]); out += self.r(B + 64, len(c))
        return out

def main():
    f = Far(); t = pdc_twin.Twin(); rnd = random.Random(7); n = 0
    for _ in range(30):
        nk = rnd.choice((4, 8)); key = bytes(rnd.randrange(256) for _ in range(4 * nk)); nr = nk + 6
        rk = t.aes_ks(key, nk)
        assert f.ks(key, nk) == rk, "ks"
        assert f.dks(nr) == t.aes_dks(rk, nr), "dks"
        iv = bytes(rnd.randrange(256) for _ in range(16)); data = bytes(rnd.randrange(256) for _ in range(16 * rnd.randrange(0, 9)))
        assert f.cbc_dec(iv, data, nr) == t.cbc_dec(rk, nr, iv, data), "cbc_dec"
        k = bytes(rnd.randrange(256) for _ in range(rnd.randrange(5, 17))); d = bytes(rnd.randrange(256) for _ in range(rnd.randrange(0, 600)))
        assert f.rc4(k, d, rnd.randrange(5)) == t.rc4(k, d), "rc4"
        n += 4
    print("pdc_far: %d far-area agreements with the twin" % n)
    if "--cycles" in sys.argv:
        f = Far(measure=True)
        for nk, nr in ((4, 10), (8, 14)):
            f.ks(bytes(range(4 * nk)), nk); f.dks(nr)
            f.cbc_dec(bytes(16), bytes(256), nr); c = f.clocks / 16
            print("AES-%d cbc dec %6.0f clk/block  %.2f KB/s on an 8088" % (32 * nk, c, 4.77e6 / c * 16 / 1024))
        f.call("_pdc_aes_tdnew", []); f.ks(bytes(16), 4); f.dks(10); print("table build + dks (AES-128) %d clk" % f.clocks)
        f.rc4(bytes(16), bytes(255)); print("rc4 255 bytes %d clk = %.1f KB/s" % (f.clocks, 4.77e6 / f.clocks * 255 / 1024))
    return 0
sys.exit(main())
