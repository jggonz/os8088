#!/usr/bin/env python3
"""Hostile transparency (PAGEDESK-SPEC 20.22.10): byte-flipped, truncated and spliced copies of the
blend fixtures through the ASan+UBSan host build of PDREND (-DPDB_DIRECT, the real PDENG). Any
sanitizer report or signal is a failure; a refusal (non-zero exit with a PDE_ code) is fine.

    python3 pdb_fuzz.py <pdr_blend_asan> <bank dir> <fixtures dir> [-n N] [--seed S]"""
import os, random, subprocess, sys, tempfile
exe, bank, fx = sys.argv[1:4]
n = int(sys.argv[sys.argv.index("-n") + 1]) if "-n" in sys.argv else 200
R = random.Random(int(sys.argv[sys.argv.index("--seed") + 1]) if "--seed" in sys.argv else 5)
tmp = tempfile.mkdtemp(); bad = runs = 0
for f in sorted(os.listdir(fx)):
    if not f.endswith(".pdf"): continue
    src = open(os.path.join(fx, f), "rb").read()
    for i in range(n):
        b = bytearray(src)
        for _ in range(R.randint(1, 6)):
            k = R.randrange(len(b)); mode = R.random()
            if mode < .5: b[k] = R.getrandbits(8)
            elif mode < .75: b[k:k + R.randint(1, 8)] = b""
            else: b[k:k] = bytes(R.getrandbits(8) for _ in range(R.randint(1, 8)))
        if R.random() < .1: b = b[:R.randrange(len(b))]
        p = os.path.join(tmp, "m.pdf"); open(p, "wb").write(b)
        for bpp in (4, 1) if i % 4 == 0 else (4,):
            r = subprocess.run([exe, "render", p, "0", "100", "0", str(bpp), "1/1", "160", "160", "0", "0", os.path.join(tmp, "o.ppm"), bank],
                               capture_output=True, timeout=60); runs += 1
            if b"Sanitizer" in r.stderr or b"runtime error" in r.stderr or r.returncode < 0 or r.returncode >= 128:
                bad += 1; open(os.path.join(tmp, "bad%d.pdf" % bad), "wb").write(b)
                print("FAIL", f, i, bpp, r.returncode, r.stderr.splitlines()[:3]); 
print("pdb_fuzz: %d runs, %d failures" % (runs, bad)); sys.exit(1 if bad else 0)
