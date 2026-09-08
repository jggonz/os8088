#!/usr/bin/env python3
"""WORD'S DOCUMENT MOVERS ARE BYTE-EXACT (SPEC.md 68.3, 27.12).

    make worddisk && python3 tests/wdmove.py

Every edit opens or closes a gap in TWO claims in lockstep - the text and its
CHP twin - so one keystroke moves the tail twice. Those moves went a WORD at a
time (`wd_mvup`/`wd_mvdn`) because `rep movsw` is 12.5 clocks a byte against
`rep movsb`'s 17, and a wrong word is a CORRUPTED DOCUMENT rather than a slow
one - which no pixel test would see, because the damage is one byte deep in a
buffer the screen shows six rows of.

So this asserts the buffers, not the glass: read both claims whole, type a
character, read them back and require exactly the expected insertion; then
Backspace and require the ORIGINAL bytes back, in both claims.

THE TEXT CHECK IS THE ONE WITH TEETH. The CHP claim is low-entropy - long
runs of one attribute byte - so a shifted copy of it can compare EQUAL to the
original: breaking `wd_mvup` on purpose failed every text assertion here and
passed every CHP one. Both are asserted because both are moved, but do not
read a green CHP line as independent evidence.

PARITY IS THE WHOLE POINT. `wd_mvup` moves the odd byte first and then steps
the pointers onto a word's low byte; `wd_mvdn` does the words and then the odd
byte. Both have a path that only runs for an odd count, so the caret is placed
at positions giving an odd AND an even tail, and at the very ends where the
count is 0 or 1 and the loops must not run at all.
"""
import os, sys, time, subprocess, tempfile, argparse, functools
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tools"); sys.path.insert(0, "tests")
import os88marty as M
from os88mouse import Mouse
import dispcp

u16 = lambda b, i=0: b[i] | (b[i+1] << 8)
FAIL = []


def check(name, ok, detail=""):
    print("   %-52s %s%s" % (name, "ok" if ok else "FAIL", "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


def pkg_syms(src="apps/word/word.asm", incs=("apps/", "apps/word/")):
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d,"p.asm"), os.path.join(d,"p.map")
        open(cp,"w").write(open(src).read()+"\n[map symbols %s]\n"%mp)
        subprocess.run(["nasm","-f","bin","-w+error"]+sum([["-I",i] for i in incs],[])
                       +["-o",os.path.join(d,"p.bin"),cp],check=True)
        out={}
        for L in open(mp):
            f=L.split()
            if len(f)==3 and all(c in "0123456789ABCDEF" for c in f[0]): out[f[2]]=int(f[0],16)
        return out, open(os.path.join(d,"p.bin"),"rb").read()


ap=argparse.ArgumentParser(); ap.add_argument("--machine",default="os8088_5150_both_gla")
a=ap.parse_args()
syms,image=pkg_syms()
DISK="build/wdmove.img"
M.scratch_disk(DISK,"build/word.o88","build/WORD.OVL","build/WELCOME.DOC")
S=lambda n: m.sym(n)

with M.launch("build/os8088-360.img",apps=DISK,machine=a.machine) as m:
    M.settle(m); mo=Mouse(marty=m)
    print("== Word's document movers, byte for byte (SPEC.md 68.3) on %s ==" % a.machine)
    dispcp.open_drive(m,mo,S,M.settle,"B")
    w=dispcp.win_list(m,S)[-1]; dx,dy=dispcp.win_rect(m,S,w)[:2]
    dispcp.open_named(m,mo,S,M.settle,dx,dy,"WELCOME.DOC")
    time.sleep(2.5); M.settle(m)

    # the package's base out of the instance table, its identity checked
    # against CODE at a named symbol (see tests/wdmenusu.py for why not the
    # head of the image).
    raw=m.read(S("inst_tab"),32*12); seg=None
    for i in range(12):
        b=i*32
        if raw[b]==1 and (raw[b+2]&0x80):
            c=u16(raw,b+6)
            if m.read(c*16+syms["wd_mact"],48)==image[syms["wd_mact"]:syms["wd_mact"]+48]:
                seg=c; break
    if seg is None:
        sys.exit("could not locate the running package image (stale build/word.o88?)")
    base=seg*16; P=lambda n: base+syms[n]
    rw=lambda n: u16(m.read(P(n),2))
    ww=lambda n,v: m.write(P(n), bytes([v&0xFF,(v>>8)&0xFF]))

    ln0=rw("wd_len")
    dseg, cseg = rw("wd_dseg"), rw("wd_cseg")
    check("the document is open and non-empty", ln0 > 64, "len=%d"%ln0)
    txt0 = m.read(dseg*16, ln0)
    chp0 = m.read(cseg*16, ln0)
    print("   len=%d  text seg 0x%04X  chp seg 0x%04X" % (ln0, dseg, cseg))

    # positions giving an odd tail, an even tail, and the two end stops
    cases = []
    for cur in (0, 1, ln0//2, ln0//2 + 1, ln0 - 1, ln0):
        cases.append(cur)

    for cur in cases:
        tail = ln0 - cur
        ww("wd_cur", cur)
        time.sleep(0.15)
        m.key("KeyZ")
        time.sleep(1.4)
        ln1 = rw("wd_len")
        t1 = m.read(rw("wd_dseg")*16, ln1)
        c1 = m.read(rw("wd_cseg")*16, ln1)
        want_t = txt0[:cur] + b"z" + txt0[cur:]
        okl = (ln1 == ln0 + 1)
        okt = (t1 == want_t) if okl else False
        okc = (c1[:cur] == chp0[:cur] and c1[cur+1:] == chp0[cur:]) if okl else False
        bad = next((i for i in range(min(len(t1),len(want_t))) if t1[i]!=want_t[i]), None)
        check("insert at %-5d (tail %4d, %s): text exact"
              % (cur, tail, "odd " if tail % 2 else "even"),
              okt, "len %d->%d, first bad byte %s" % (ln0, ln1, bad))
        check("insert at %-5d (tail %4d, %s): CHP exact"
              % (cur, tail, "odd " if tail % 2 else "even"), okc, "")

        # ...and Backspace must put the document back exactly
        ww("wd_cur", cur + 1)
        time.sleep(0.15)
        m.key("Backspace")
        time.sleep(1.4)
        ln2 = rw("wd_len")
        t2 = m.read(rw("wd_dseg")*16, ln2)
        c2 = m.read(rw("wd_cseg")*16, ln2)
        bad2 = next((i for i in range(min(len(t2),len(txt0))) if t2[i]!=txt0[i]), None)
        check("delete at %-5d: the ORIGINAL bytes are back" % cur,
              ln2 == ln0 and t2 == txt0 and c2 == chp0,
              "len %d (want %d), first bad byte %s" % (ln2, ln0, bad2))

print()
print("wdmove: %s" % ("FAILED: " + ", ".join(FAIL) if FAIL else "ok"))
sys.exit(1 if FAIL else 0)
