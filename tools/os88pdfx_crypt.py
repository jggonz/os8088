#!/usr/bin/env python3
"""os88pdfx_crypt.py - the encrypted fixtures of PageDesk's any-PDF round
(PAGEDESK-SPEC 20.22.5, tests/pdfany/crypt/).

    python3 tools/os88pdfx_crypt.py OUTDIR            make every variant
    python3 tools/os88pdfx_crypt.py --list            the variants and what is expected

Each fixture is apps/pagedesk/samples/<BASE>.PDF run through qpdf with
--static-id and --static-aes-iv, so the output is the same bytes on any host
with the same qpdf (the version is in tests/pdfany/crypt/README.TXT). RC4
needs --allow-weak-crypto. OUTDIR also gets MANIFEST.TXT: one line a file,
`file base user-password owner-password algorithm`, which tests/pdcrypt/
pdc_e2e.py reads to know what to try.
"""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAMPLES = os.path.join(ROOT, "apps", "pagedesk", "samples")

# name, base, user password, owner password, qpdf encryption arguments, description
VARIANTS = [
    ("RC4_40",     "MARKUP", "",       "owner", ["40", "--allow-weak-crypto"],                          "R2 V1 RC4-40, the empty user password"),
    ("RC4_128",    "MARKUP", "",       "owner", ["128", "--use-aes=n", "--allow-weak-crypto"],          "R3 V2 RC4-128"),
    ("RC4_V4",     "FLATE",  "",       "owner", ["128", "--use-aes=n", "--force-V4", "--allow-weak-crypto"], "R4 V4 /CFM /V2 (RC4-128 in a crypt filter), Flate inside"),
    ("AES_128",    "MARKUP", "",       "owner", ["128", "--use-aes=y"],                                 "R4 V4 AESV2"),
    ("AES_128_NM", "HELLO",  "",       "owner", ["128", "--use-aes=y", "--cleartext-metadata"],         "R4 AESV2 with /EncryptMetadata false"),
    ("AES_256_R6", "MARKUP", "",       "owner", ["256"],                                                "R6 V5 AESV3, the empty user password"),
    ("AES_256_R5", "HELLO",  "",       "owner", ["256", "--force-R5"],                                  "R5 V5 AESV3 (Adobe extension level 3)"),
    ("RC4_40_PW",  "HELLO",  "secret", "owner", ["40", "--allow-weak-crypto"],                          "R2 with a user password"),
    ("RC4_128_PW", "FLATE",  "secret", "owner", ["128", "--use-aes=n", "--allow-weak-crypto"],          "R3 with a user password"),
    ("AES_128_PW", "MARKUP", "secret", "owner", ["128", "--use-aes=y"],                                 "R4 with a user password"),
    ("AES_256_PW", "HELLO",  "secret", "owner", ["256"],                                                "R6 with a user password"),
    ("AES_256_NM", "FLATE",  "",       "owner", ["256", "--cleartext-metadata"],                        "R6 with /EncryptMetadata false"),
    ("PAGES_AES",  "PAGES",  "",       "owner", ["128", "--use-aes=y"],                                 "R4, a longer page tree"),
    ("GUIDE_RC4",  "XTGUIDE","",       "owner", ["128", "--use-aes=n", "--allow-weak-crypto"],          "R3, outlines and link annotations (strings in many objects)"),
    ("GUIDE_AES",  "XTGUIDE","",       "owner", ["128", "--use-aes=y"],                                 "R4, outlines and link annotations"),
    ("GUIDE_R6",   "XTGUIDE","",       "owner", ["256"],                                                "R6, outlines and link annotations"),
    ("FORM_RC4_40","ACROFORM","",      "owner", ["40", "--allow-weak-crypto"],                          "R2, form fields (strings in widgets)"),
]

def build(out):
    os.makedirs(out, exist_ok=True)
    man = []
    for name, base, upw, opw, args, desc in VARIANTS:
        src = os.path.join(SAMPLES, base + ".PDF")
        dst = os.path.join(out, name + ".PDF")
        weak = ["--allow-weak-crypto"] if "--allow-weak-crypto" in args else []     # before --encrypt, or qpdf refuses
        args = [a for a in args if a != "--allow-weak-crypto"]
        cmd = ["qpdf"] + weak + ["--static-id", "--static-aes-iv", "--encrypt", upw, opw] + args + ["--", src, dst]
        subprocess.check_call(cmd)
        man.append("%s.PDF %s %s %s %s" % (name, base, upw or "-", opw, desc.replace(" ", "_")))
    with open(os.path.join(out, "MANIFEST.TXT"), "w") as f:
        f.write("\n".join(man) + "\n")

if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--list":
        for v in VARIANTS: print("%-11s %-7s user=%-8r %s" % (v[0], v[1], v[2], v[5]))
    elif len(sys.argv) == 2:
        build(sys.argv[1])
    else:
        print(__doc__); sys.exit(2)
