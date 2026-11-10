#!/usr/bin/env python3
"""
os8088 - tests/pdcrypt/pdc_e2e.py       PageDesk's encryption against qpdf (PAGEDESK-SPEC 20.22.5)

For every fixture of tools/os88pdfx_crypt.py (made here, in WORKDIR):
  1. the file key PDXK finds (hosttest/pdc_e2e.c: the real PDENG + the real PDXK,
     on the host) must equal `qpdf --show-encryption-key`, for the user password
     and, when there is a non-empty one, for the OWNER password too;
  2. a wrong password must be PDE_PASSWORD (23) and never a key;
  3. everything PDENG then decrypts - every stream's decoded CRC, every page's
     content streams, every string a dictionary gives, every annotation - must
     equal the SAME driver's lines over the file `qpdf --decrypt` wrote.

    python3 tests/pdcrypt/pdc_e2e.py [E2E_BINARY] [WORKDIR]
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
E2E = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "pdc_e2e")
WORK = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "build", "pdcrypt", "e2e")
fails = 0

def fail(m):
    global fails
    fails += 1
    print("FAIL", m)

def run(*a):
    p = subprocess.run([E2E, *a], capture_output=True, text=True, timeout=1800)
    return p.returncode, p.stdout, p.stderr

def qkey(path, pw):
    p = subprocess.run(["qpdf", "--password=" + pw, "--check", "--show-encryption-key", path], capture_output=True, text=True)
    m = re.search(r"Encryption key = ([0-9a-f]+)", p.stdout)
    return m.group(1) if m else None

def main():
    os.makedirs(WORK, exist_ok=True)
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "os88pdfx_crypt.py"), WORK])
    n = 0
    for line in open(os.path.join(WORK, "MANIFEST.TXT")):
        name, base, upw, opw, desc = line.split()
        upw = "" if upw == "-" else upw
        path = os.path.join(WORK, name)
        pws = [(upw, 1)] + ([(opw, 2)] if upw else [])
        for pw, who in pws:
            rc, out, err = run("auth", path, pw or "-")
            m = re.search(r"AUTH ok auth=(\d) keylen=(\d+) .* key=([0-9a-f]+)", out)
            want = qkey(path, pw)
            if not m: fail("%s: AUTH with %r: %s %s" % (name, pw, out.strip(), err.strip())); continue
            if m.group(3) != want: fail("%s: key %s != qpdf's %s" % (name, m.group(3), want))
            if int(m.group(1)) != who and not (who == 1 and not upw): fail("%s: auth %s, wanted %d" % (name, m.group(1), who))
            n += 1
        rc, out, err = run("auth", path, "wrongpw")
        if "AUTH rc=23" not in out: fail("%s: a wrong password gave %r" % (name, out.strip()))
        n += 1
        plain = os.path.join(WORK, "plain_" + name)
        subprocess.check_call(["qpdf", "--password=" + upw, "--decrypt", "--static-id", path, plain])
        rc, a, err = run("dump", path, upw or "-")
        rc2, b, err2 = run("dump", plain)
        if rc or rc2: fail("%s: dump rc %d/%d %s %s" % (name, rc, rc2, a[-200:], err[-200:])); continue
        a = [l for l in a.splitlines() if not l.startswith("AUTH ")]
        b = b.splitlines()
        if a != b:
            fail("%s: decrypted differs from qpdf's: %s" % (name, [(x, y) for x, y in zip(a, b) if x != y][:3] or (len(a), len(b))))
        elif not any(l.startswith("STREAM") for l in a):
            fail("%s: nothing was compared" % name)
        else:
            print("ok   %-11s %s (%d lines, %d streams, %d strings)" % (name, desc, len(a), sum(l.startswith("STREAM") for l in a), sum(l.startswith(("STR ", "ANNOT")) for l in a)))
        n += 1
    print("pdc_e2e: %d checks, %d failures" % (n, fails))
    return 1 if fails else 0

sys.exit(main())
