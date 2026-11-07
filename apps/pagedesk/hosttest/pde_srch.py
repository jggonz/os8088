#!/usr/bin/env python3
"""PDENG SEARCH's differential test (PAGEDESK-SPEC 20.10.1).

Runs build/pde_srch over random synthetic pages and piece sizes and compares
its hits with a reference that works on the text: fold case, make a blank
between glyphs a line or a kern gap apart, collapse blanks, find the needle
leftmost and non-overlapping.  The answer must not depend on the piece size.

  pde_srch.py BUILD/pde_srch DOC.PDF [N]
"""
import random, subprocess, sys

def fold(c):
    o = ord(c)
    if 65 <= o <= 90 or (0xC0 <= o <= 0xDE and o != 0xD7):
        return chr(o + 32)
    return c

def symbols(text, skip):
    """[(char, run or None)] as the engine sees it."""
    out = []; run = 0; pend = False; first = True; lastsp = True
    for ch in text:
        if ch == '/':
            pend = True; continue
        if ch == '|':
            pend = True; continue
        if ch == '~':
            continue
        r = run; run += 1
        c = fold(ch)
        gap = pend and not first
        pend = False; first = False
        if r < skip:
            continue
        if gap and c != ' ' and not lastsp:
            out.append((' ', None)); lastsp = True
        if c == ' ':
            if lastsp: continue
            out.append((' ', r)); lastsp = True
        else:
            out.append((c, r)); lastsp = False
    return out

def reference(text, needle, skip):
    nd = ' '.join(fold(c) for c in needle).replace('  ', ' ')
    nd = ''.join(fold(c) for c in needle)
    while '  ' in nd: nd = nd.replace('  ', ' ')
    nd = nd.strip(' ')
    syms = symbols(text, skip)
    s = ''.join(c for c, _ in syms)
    hits = []; i = 0
    while nd:
        j = s.find(nd, i)
        if j < 0: break
        hits.append((syms[j][1], syms[j + len(nd) - 1][1]))
        i = j + len(nd)
    return hits

def engine(exe, doc, piece, needle, text, skip):
    args = [exe, doc, str(piece), needle.encode('latin-1'), text.encode('latin-1')]
    if skip: args.append(str(skip))
    p = subprocess.run(args, capture_output=True)
    p.stdout = p.stdout.decode(); p.stderr = p.stderr.decode()
    if p.returncode:
        raise SystemExit("pde_srch rc=%d %s %s" % (p.returncode, p.stdout, p.stderr))
    return [tuple(int(x) for x in l.split()[1:]) for l in p.stdout.splitlines() if l.startswith('H ')]

def main():
    exe, doc = sys.argv[1], sys.argv[2]
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    rnd = random.Random(7)
    words = ['the', 'same', 'baud', 'rate', 'a', 'aab', 'aaab', 'Cable', 'CABLE', 'ra', 'te', 'x', 'ab']
    fixed = [
        ("baud", "the same baud rate/is same|baud ra|te", 0),
        ("same baud", "the same/baud", 0),
        ("same baud", "the same|baud", 0),
        ("sa", "the s~a~me", 0),
        ("aab", "aaab aab", 0),
        ("aaab", "aaaab", 0),
        ("CABLE", "a cable CABLE", 0),
        ("  cable  ", "a cable", 0),
        ("e r", "the rate", 0),
        ("ab", "abab", 0),
        ("ab", "abab", 2),
        ("x y", "x   y", 0),
        ("\xe9t\xe9", "\xc9T\xc9", 0),
    ]
    cases = [(nd, tx, sk) for nd, tx, sk in fixed]
    for _ in range(n):
        toks = []
        for _ in range(rnd.randint(2, 25)):
            toks.append(rnd.choice(words))
            toks.append(rnd.choice([' ', ' ', '/', '|', '', '~']))
        text = ''.join(toks)
        nd = ' '.join(rnd.choice(words) for _ in range(rnd.randint(1, 3)))
        if rnd.random() < 0.3:
            nd = nd[:rnd.randint(1, len(nd))]
        cases.append((nd, text, rnd.choice([0, 0, 0, rnd.randint(0, 20)])))
    bad = 0
    for nd, tx, sk in cases:
        want = reference(tx, nd, sk)
        if not nd.strip(' '):
            continue
        for piece in (1, 3, 74):
            got = engine(exe, doc, piece, nd, tx, sk)
            if got != want:
                bad += 1
                print("MISMATCH piece=%d needle=%r text=%r skip=%d\n  want %s\n  got  %s" % (piece, nd, tx, sk, want, got))
                break
    print("pde_srch: %d cases, %d mismatches" % (len(cases), bad))
    sys.exit(1 if bad else 0)

main()
