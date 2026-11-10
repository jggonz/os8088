#!/usr/bin/env python3
"""pdc_diff.py - pdcrypto.h: the host twin (pdctwin.c) and the shipping 8086 text
(pdc*.inc, under Unicorn) each held to the published vectors (pdc_vec.py), then to
each other on random keys, lengths and chains; and a negative control proving the
vectors can fail.  Needs nasm, cc and unicorn (exit 77 without unicorn).

    python3 tests/pdcrypt/pdc_diff.py [--cycles]
"""
import os, sys, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pdc_vec, pdc_twin

class Broken:                                   # the negative control: one flipped bit in AES-128 encrypt
    def __init__(self, be): self.be = be
    def __getattr__(self, n): return getattr(self.be, n)
    def aes_enc(self, rk, nr, blk):
        o = bytearray(self.be.aes_enc(rk, nr, blk)); o[3] ^= 1; return bytes(o)

def main():
    try:
        import pdc_asm
    except ImportError:
        print("pdc_diff: SKIP - unicorn is not installed"); return 77
    tw = pdc_twin.Twin()
    print("twin : %d vector checks" % pdc_vec.check(tw, "twin"))
    asm = pdc_asm.Asm()
    print("asm  : %d vector checks" % pdc_vec.check(asm, "asm", slow=False))
    try:
        pdc_vec.check(Broken(tw), "broken"); print("NEGATIVE CONTROL FAILED: a broken AES passed"); return 1
    except AssertionError:
        print("negative control: a flipped bit in AES-128 is caught")
    rnd = random.Random(2088); n = 0
    for _ in range(40):
        nk = rnd.choice((4, 8)); key = bytes(rnd.randrange(256) for _ in range(4 * nk))
        rkt, rka = tw.aes_ks(key, nk), asm.aes_ks(key, nk)
        assert rkt == rka, ("aes_ks", nk); nr = nk + 6
        blk = bytes(rnd.randrange(256) for _ in range(16))
        assert tw.aes_enc(rkt, nr, blk) == asm.aes_enc(rka, nr, blk) and tw.aes_dec(rkt, nr, blk) == asm.aes_dec(rka, nr, blk)
        iv = bytes(rnd.randrange(256) for _ in range(16))
        data = bytes(rnd.randrange(256) for _ in range(16 * rnd.randrange(0, 9)))
        assert tw.cbc_enc(rkt, nr, iv, data) == asm.cbc_enc(rka, nr, iv, data), "cbc_enc"
        assert tw.cbc_dec(rkt, nr, iv, data) == asm.cbc_dec(rka, nr, iv, data), "cbc_dec"
        rk = bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 33)))
        d = bytes(rnd.randrange(256) for _ in range(rnd.randrange(0, 600)))
        assert tw.rc4(rk, d) == asm.rc4(rk, d), "rc4"
        n += 6
    print("random: %d twin/asm agreements" % n)
    if "--cycles" in sys.argv:
        a = pdc_asm.Asm(measure=True)
        def clk(f, *args):
            f(*args); return a.clocks
        key = bytes(range(16)); rk = a.aes_ks(key, 4); rk32 = a.aes_ks(bytes(range(32)), 8)
        blk = bytes(range(16))
        a.aes_enc(rk, 10, blk); print("aes128 enc block  %9.0f clocks %6d insns" % (a.clocks, a.insns))
        a.aes_dec(rk, 10, blk); print("aes128 dec block  %9.0f clocks %6d insns" % (a.clocks, a.insns))
        a.aes_enc(rk32, 14, blk); print("aes256 enc block  %9.0f clocks %6d insns" % (a.clocks, a.insns))
        a.aes_dec(rk32, 14, blk); print("aes256 dec block  %9.0f clocks %6d insns" % (a.clocks, a.insns))
        a.aes_ks(key, 4); print("aes128 key sched  %9.0f clocks" % a.clocks)
        a.aes_ks(bytes(range(32)), 8); print("aes256 key sched  %9.0f clocks" % a.clocks)
        a.md5_block(a.md5_init(), bytes(64)); print("md5 block         %9.0f clocks" % a.clocks)
        a.sha256_block(a.sha256_init(), bytes(64)); print("sha256 block      %9.0f clocks" % a.clocks)
        a.sha512_block(a.sha512_init(), bytes(128)); print("sha512 block      %9.0f clocks" % a.clocks)
        a.rc4(b"k" * 16, bytes(255)); print("rc4 255 bytes     %9.0f clocks" % a.clocks)
    return 0
sys.exit(main())
