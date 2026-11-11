#!/usr/bin/env python3
"""
os8088 - tests/pdcrypt/pdc_fuzz.py       ASan/UBSan fuzz of the encryption path (PAGEDESK-SPEC 20.22.5)

Builds hosttest/pdc_fuzz.c (the real PDENG and PDXK, -fsanitize=address,undefined) and runs it over mutated
copies of every fixture of tools/os88pdfx_crypt.py: bit flips, runs of 00/FF, /Length digits (a lying length),
a cut tail, the data after `stream` cut short (short ciphertext), a corrupted /O or /U. Any answer is
fine; a crash, a hang (the engine's read budget) or a sanitizer report fails.

    python3 tests/pdcrypt/pdc_fuzz.py [ITERATIONS] [SEED]
"""
import os, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AD = os.path.join(ROOT, "apps", "pagedesk")
BIN = os.path.join(ROOT, "build", "pdcrypt", "pdc_fuzz")
WORK = os.path.join(ROOT, "build", "pdcrypt", "e2e")

def main():
    iters = sys.argv[1] if len(sys.argv) > 1 else "300"
    seed = sys.argv[2] if len(sys.argv) > 2 else "1"
    os.makedirs(os.path.dirname(BIN), exist_ok=True)
    subprocess.check_call(["cc", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined", "-Wno-unused-function",
        "-D_GNU_SOURCE", "-DPD_HOST", "-I", AD, "-I", os.path.join(AD, "hosttest"), os.path.join(AD, "pdeng.c"), os.path.join(AD, "pdxk.c"),
        os.path.join(AD, "hosttest", "pdc_fuzz.c"), os.path.join(AD, "hosttest", "pde_host.c"), os.path.join(AD, "pdmath.c"),
        os.path.join(AD, "hosttest", "pdhost.c"), "-o", BIN], stderr=subprocess.DEVNULL)
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "os88pdfx_crypt.py"), WORK])
    files = sorted(f for f in os.listdir(WORK) if f.endswith(".PDF") and not f.startswith("plain_"))
    bad = 0
    for pw, pick in (("-", [f for f in files if not f.endswith("_PW.PDF")]), ("secret", [f for f in files if f.endswith("_PW.PDF")])):
        p = subprocess.run([BIN, seed, iters, pw] + [os.path.join(WORK, f) for f in pick], capture_output=True, text=True, timeout=3000)
        print("pw=%-6s %2d files x %s: %s" % (pw, len(pick), iters, (p.stderr.strip().splitlines() or ["(no output)"])[-1]))
        if p.returncode:
            bad += 1
            print(p.stderr[-3000:])
    print("pdc_fuzz: %s" % ("FAILED" if bad else "no crash, no hang, no sanitizer report"))
    return 1 if bad else 0

sys.exit(main())
