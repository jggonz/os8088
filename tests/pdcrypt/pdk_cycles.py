#!/usr/bin/env python3
"""
os8088 - tests/pdcrypt/pdk_cycles.py     what one R6 attempt costs on a 4.77 MHz 8088 (PAGEDESK-SPEC 20.22.5.1)

  1. counts, for the R6 fixtures' real /U /O /UE /OE, every block operation of algorithm 2.B (AES-128 CBC
     blocks, SHA-256 blocks, SHA-384/512 blocks, in the rounds the algorithm really runs: the exact
     round count, the SHA variant chosen each round) for the empty password, a typical one and a wrong one;
  2. prices each operation from the shipping assembly run in Unicorn (pdc_asm.Asm(measure=True): the
     8088 rule max(4.34 x bytes, clocks) per instruction of tests/pdmath/pdcycles.py);
  3. CALIBRATES against the real build/PDXK.MOD: it is loaded and its door called as the primary will call it
     (PDV_X_INIT, PDV_KEY_AUTH slices), the first slices are costed instruction by instruction and compared
     with the prediction for the same work, so the C glue (the block feed, the finalisation, the key
     schedule) is IN the figure, not assumed away.

    python3 tests/pdcrypt/pdk_cycles.py [--calibrate]
"""
import hashlib, os, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pdc_twin, pdc_asm

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
E2E = os.path.join(ROOT, "build", "pdc_e2e")
WORK = os.path.join(ROOT, "build", "pdcrypt", "e2e")
CLK_US = 0.2096                         # one 4.77 MHz clock

def params(name):
    out = subprocess.run([E2E, "params", os.path.join(WORK, name)], capture_output=True, text=True).stdout.split("\n")
    b = bytes.fromhex(out[1].strip())
    assert len(b) == 402
    v, r, length, plo, phi, stm, strm, eff, encmeta, olen, ulen = struct.unpack_from("<11H", b, 0)
    o = b[22:70]; u = b[70:118]; oe = b[118:150]; ue = b[150:182]
    return dict(r=r, o=o, u=u, oe=oe, ue=ue, raw=b)

def measure():
    a = pdc_asm.Asm(measure=True)
    rk = a.aes_ks(bytes(16), 4)
    iv = bytes(16)
    a.cbc_enc(rk, 10, iv, bytes(16)); aes = a.clocks
    a.sha256_block(a.sha256_init(), bytes(64)); s256 = a.clocks
    a.sha512_block(a.sha512_init(), bytes(128)); s512 = a.clocks
    a.aes_ks(bytes(16), 4); ks = a.clocks
    return dict(aes=aes, s256=s256, s512=s512, ks=ks)

def run_2b(tw, pw, salt, udata, count):
    """algorithm 2.B with the twin; count = dict of block operations"""
    k = hashlib.sha256(pw + salt + udata).digest()
    i = 0
    while True:
        k1 = (pw + k + udata) * 64
        rk = tw.aes_ks(k[:16], 4)
        e, _ = tw.cbc_enc(rk, 10, k[16:32], k1)
        count["aes"] += len(k1) // 16
        count["ks"] += 1
        mod = sum(e[:16]) % 3
        if mod == 0: k = hashlib.sha256(e).digest(); count["s256"] += (len(e) + 9 + 63) // 64
        elif mod == 1: k = hashlib.sha384(e).digest(); count["s512"] += (len(e) + 17 + 127) // 128
        else: k = hashlib.sha512(e).digest(); count["s512"] += (len(e) + 17 + 127) // 128
        i += 1
        count["rounds"] += 1
        if i >= 64 and e[-1] <= i - 32: break
    count["first"] += (len(pw + salt + udata) + 9 + 63) // 64
    return k[:32]

def scenario(tw, c, name, p, pw, who):
    cnt = dict(aes=0, ks=0, s256=0, s512=0, rounds=0, first=0)
    pwb = pw.encode()
    k = run_2b(tw, pwb, p["u"][32:40], b"", cnt)            # the user password's validation
    runs = 1
    if k == p["u"][:32]:                                    # it is: the key salt, a second run
        run_2b(tw, pwb, p["u"][40:48], b"", cnt); runs = 2
    else:                                                   # it is not: the owner password's validation
        k = run_2b(tw, pwb, p["o"][32:40], p["u"][:48], cnt); runs = 2
        if k == p["o"][:32]: run_2b(tw, pwb, p["o"][40:48], p["u"][:48], cnt); runs = 3
    clk = cnt["aes"] * c["aes"] + cnt["ks"] * c["ks"] + cnt["s256"] * c["s256"] + cnt["s512"] * c["s512"] + cnt["first"] * c["s256"]
    return runs, cnt, clk

def labels():
    import re
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-I", os.path.join(ROOT, "apps") + "/", "-I", os.path.join(ROOT, "build") + "/",
                           "-l", os.path.join(ROOT, "build", "PDXK.lst"), "-o", os.path.join(ROOT, "build", "PDXK.MOD"),
                           os.path.join(ROOT, "apps/pagedesk/pdxk.asm")])
    pend = []; labs = {}
    for l in open(os.path.join(ROOT, "build", "PDXK.lst")).read().splitlines():
        m = re.match(r'\s*\d+\s+([0-9A-F]{8})\s', l)
        if m:
            for n in pend: labs[n] = int(m.group(1), 16)
            pend = []; continue
        mm = re.match(r'\s*\d+\s+(?:<\d+>)?\s*([A-Za-z_$][\w$]*):', l)
        if mm: pend.append(mm.group(1))
    return labs

def calibrate(c, slices=3):
    """the real PDXK.MOD in Unicorn, KEY_AUTH slices costed instruction by instruction, against the same work priced block by block"""
    from unicorn import UC_HOOK_CODE
    import pdc_target as T
    labs = labels()
    p = params("AES_256_R6.PDF")
    m = T.Machine(open(os.path.join(WORK, "AES_256_R6.PDF"), "rb").read())
    m.wr(0, T.init_struct().ljust(1024, b"\0"))
    assert m.door("PDXK", 0xC0, T.XBSEG)[1] == 0
    cr = bytearray(402)
    struct.pack_into("<11H", cr, 0, 5, 6, 256, 0xFFFC, 0xFFFF, 3, 3, 3, 1, 48, 48)
    cr[22:70] = p["o"]; cr[70:118] = p["u"]; cr[118:150] = p["oe"]; cr[150:182] = p["ue"]
    base = T.lin(T.KXSEG)
    ops = {"aes": labs["_pdc_cbc_enc"], "s256": labs["_pdc_sha256_block"], "s512": labs["_pdc_sha512_block"], "ks": labs["_pdc_aes_ks"]}
    inv = {base + a: k for k, a in ops.items()}
    cnt = dict(aes=0, s256=0, s512=0, ks=0); tot = [0.0, 0]
    def hook(u, addr, size, ud):
        tot[0] += pdc_asm.cost(bytes(u.mem_read(addr, min(size, 4))), size); tot[1] += 1
        k = inv.get(addr)
        if k: cnt[k] += 1
    m.uc.hook_add(UC_HOOK_CODE, hook)
    for sl in range(slices):
        m.wr(0, bytes(cr))
        ax, cf, _ = m.door("PDXK", 0xC1)
        cr[:] = m.rd(0, 402)
        assert not cf and ax == 65, ("a slice", ax, cf)
    priced = cnt["aes"] * c["aes"] + cnt["s256"] * c["s256"] + cnt["s512"] * c["s512"] + cnt["ks"] * c["ks"]
    print("calibration: %d slices of the real PDXK.MOD: %d AES blocks, %d key schedules, %d SHA-256 blocks, %d SHA-512 blocks; %d instructions"
          % (slices, cnt["aes"], cnt["ks"], cnt["s256"], cnt["s512"], tot[1]))
    print("  costed %0.f clocks; the same operations priced alone %0.f: glue and finalisation add %.1f%%" % (tot[0], priced, 100.0 * (tot[0] - priced) / priced))
    return tot[0] / priced

def stats(tw, c, glue):
    """the expectation over random salts: one run of 2.B with an n-character password and no user data (the
    validation of a user password, or the key derivation after it): rounds, block operations, seconds on an 8088"""
    import random
    rnd = random.Random(2088)
    out = []
    for n in (0, 6, 12, 24):
        pw = bytes(rnd.randrange(33, 127) for _ in range(n))
        secs, rounds, aes, s256, s512 = [], [], [], [], []
        for _ in range(60):
            cnt = dict(aes=0, ks=0, s256=0, s512=0, rounds=0, first=0)
            run_2b(tw, pw, bytes(rnd.randrange(256) for _ in range(8)), b"", cnt)
            clk = cnt["aes"] * c["aes"] + cnt["ks"] * c["ks"] + (cnt["s256"] + cnt["first"]) * c["s256"] + cnt["s512"] * c["s512"]
            secs.append(clk * CLK_US / 1e6 * glue); rounds.append(cnt["rounds"]); aes.append(cnt["aes"]); s256.append(cnt["s256"] + cnt["first"]); s512.append(cnt["s512"])
        m = lambda v: sum(v) / len(v)
        print("  %2d-char password, one run, mean of 60 salts: %5.1f rounds, %6.0f AES blocks, %5.0f SHA-256 blocks, %5.0f SHA-384/512 blocks  -> %5.0f s (min %4.0f, max %4.0f)"
              % (n, m(rounds), m(aes), m(s256), m(s512), m(secs), min(secs), max(secs)))
        out.append((n, m(secs)))
    return out

def main():
    tw = pdc_twin.Twin()
    c = measure()
    print("per operation on an 8088 (clocks / ms): AES-128 CBC block %d / %.1f   key schedule %d / %.1f   SHA-256 block %d / %.1f   SHA-512 block %d / %.1f"
          % (c["aes"], c["aes"] * CLK_US / 1000, c["ks"], c["ks"] * CLK_US / 1000, c["s256"], c["s256"] * CLK_US / 1000, c["s512"], c["s512"] * CLK_US / 1000))
    rows = []
    for fx, pw, who in (("AES_256_R6.PDF", "", "the empty password, which is the user's"),
                        ("AES_256_PW.PDF", "secret", "a typed user password (6 characters)"),
                        ("AES_256_PW.PDF", "wrongpw", "a wrong password (8 characters): user, then owner")):
        p = params(fx)
        runs, cnt, clk = scenario(tw, c, fx, p, pw, who)
        sec = clk * CLK_US / 1e6
        print("%-52s runs %d  rounds %3d  AES blocks %6d  SHA-256 blocks %5d  SHA-512 blocks %5d  -> %7.0f s = %5.1f min"
              % (who, runs, cnt["rounds"], cnt["aes"], cnt["s256"] + cnt["first"], cnt["s512"], sec, sec / 60))
        rows.append((who, runs, cnt, sec))
    if "--stats" in sys.argv:
        print("expectation over random salts (glue factor 1.062 from the calibration):")
        stats(tw, c, 1.062)
    if "--calibrate" in sys.argv:
        f = calibrate(c)
        for who, runs, cnt, sec in rows:
            print("  with the glue: %-52s %7.0f s = %5.1f min" % (who, sec * f, sec * f / 60))
    return 0

if __name__ == "__main__":
    sys.exit(main())
