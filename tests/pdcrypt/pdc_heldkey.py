#!/usr/bin/env python3
"""
os8088 - tests/pdcrypt/pdc_heldkey.py     the HELD KEY's match rule, against qpdf (PAGEDESK-SPEC 20.22.2.10)

PageDesk keeps the key that opened an encrypted document and hands it back, with no question and no
derivation, to the next open whose file it recognises (pdovlplat.c's ovl_key_try). The rule is: the
first 16 bytes of /ID[0], the first 16 bytes of /U, and the methods for streams, strings and embedded
files and /EncryptMetadata, all equal. QA R20 was the rule being /ID[0] alone: GUIDE_AES, GUIDE_RC4
and GUIDE_R6 share XTGUIDE's /ID, and the key held from one drew every page of the next as garbage.

This holds the rule to the truth over every pair of fixtures in tests/pdfany/crypt: whenever two files
MATCH, qpdf must report the same file key and the same methods for both (a match that would SETKEY a
wrong key or method fails). It also checks that the corpus still contains the cases that make the
rule necessary, so the check cannot pass vacuously: a pair that /ID[0] alone matches with a different
key (R20), and a pair that /ID[0] and /U match with a different method (R3 RC4-128 against R4 AESV2
under the same passwords derive the same key and /U).

    python3 tests/pdcrypt/pdc_heldkey.py
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FX = os.path.join(ROOT, "tests", "pdfany", "crypt")


def hexstr(tok):
    """a PDF string token's bytes: <hex> or a (literal) with its escapes"""
    if tok.startswith(b"<"):
        h = re.sub(rb"\s", b"", tok[1:-1])
        if len(h) % 2:
            h += b"0"
        return bytes.fromhex(h.decode())
    out, i, s = bytearray(), 1, tok
    while i < len(s) - 1:
        c = s[i]
        if c == 0x5C:
            i += 1
            e = s[i]
            if 0x30 <= e <= 0x37:
                j = i
                while j < i + 3 and 0x30 <= s[j] <= 0x37:
                    j += 1
                out.append(int(s[i:j], 8) & 0xFF)
                i = j
                continue
            out.append({0x6E: 10, 0x72: 13, 0x74: 9, 0x62: 8, 0x66: 12}.get(e, e))
        else:
            out.append(c)
        i += 1
    return bytes(out)


STR = rb"(<[0-9a-fA-F\s]*>|\((?:\\.|[^\\)])*\))"


def params(path, pw):
    d = open(path, "rb").read()
    idm = re.findall(rb"/ID\s*\[\s*" + STR, d, re.S)
    enc = re.search(rb"/Encrypt\s+(\d+)\s+(\d+)\s+R", d)
    body = re.search(rb"(?<![0-9])" + enc.group(1) + rb"\s+" + enc.group(2) + rb"\s+obj(.*?)endobj", d, re.S).group(1)
    u = hexstr(re.search(rb"/U\s*" + STR, body, re.S).group(1))
    q = subprocess.run(["qpdf", "--show-encryption", "--show-encryption-key", "--password=" + pw, path],
                       capture_output=True, text=True).stdout
    key = re.search(r"Encryption key = (\w+)", q).group(1)
    r = int(re.search(r"^R = (\d+)", q, re.M).group(1))
    meth = tuple(re.findall(r"^(?:stream|string|file) encryption method: (\w+)", q, re.M)) or ("RC4",) * 3
    nm = re.search(rb"/EncryptMetadata\s+false", body) is not None
    return dict(id0=hexstr(idm[-1])[:16], u=u[:16], key=key, meth=meth + (nm,), r=r)


def main():
    man = {}
    for line in open(os.path.join(FX, "MANIFEST.TXT")):
        f = line.split()
        if len(f) >= 3 and f[0].endswith(".PDF"):
            man[f[0]] = "" if f[2] == "-" else f[2]
    P = {n: params(os.path.join(FX, n), pw) for n, pw in sorted(man.items())}
    fails, idonly, sameu = 0, 0, 0
    for a in P:
        for b in P:
            if a == b:
                continue
            A, B = P[a], P[b]
            if A["id0"] != B["id0"]:
                continue
            truth = A["key"] == B["key"] and A["meth"] == B["meth"]
            if not truth:
                idonly += 1                      # what /ID[0] alone would have SETKEYed wrongly (R20)
                if A["u"] == B["u"]:
                    sameu += 1                   # ...and /U alone too: only the method differs
            match = A["u"] == B["u"] and A["meth"] == B["meth"]
            if match and not truth:
                fails += 1
                print("FAIL held %s, opening %s: the rule matches but the key or method differs" % (a, b))
    print("pdc_heldkey: %d fixtures; %d pairs /ID[0] alone would key wrongly, %d of them /U alone too" %
          (len(P), idonly, sameu))
    if idonly == 0 or sameu == 0:
        fails += 1
        print("FAIL the corpus lost the pairs that make the rule necessary (R20): the check would be vacuous")
    print("pdc_heldkey: %d failures" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
