#!/usr/bin/env python3
"""WORD IS ONE FILE, AND ITS PART 1 IS THE TOP OF ITS OWN SEGMENT (SPEC.md 68.10).

    make worddisk && python3 tests/wdparts.py

WORD.O88 is a parted package: apps/word/wdload.asm is the image the kernel
launches, part 0 is word.asm's image with its bss inside it, and part 1 is
`.modc`, ASSEMBLED at WD_P1ORG. Nothing reaches part 1 through a far pointer:
op_load lays the carve out at roundup512(len) a part and part 0 is exactly
WD_P1ORG long, so part 1 lands at that offset of the program's own segment
and a near call reaches it. If the layout ever disagrees with the assembly,
the first call into part 1 jumps into whatever the carve has there and the
program simply stops - no message, nothing to read. So every leg here is
about WHERE the bytes are, then that the code in them runs.

  leg A  the launch that matters: WELCOME.DOC double-clicked, so the
         association starts the LOADER and the document name has to survive
         the re-home into the program. [wd_len] non-zero is the document
  leg B  I_SIZE is WD_P1ORG - the region the program declared, not the
         loader's 2KB and not image + real bss (SPEC.md 20.12.10.4)
  leg C  the claim holding the region runs past part 1's end: part 1 is
         INSIDE the carve, which is what makes it move with the region
  leg D  the bytes at program:WD_P1ORG are build/word.p1.bin, exactly
  leg E  part 1's code RUNS: Edit > Search for a word, which compiles the
         pattern in wd_pcomp - part 1 - and calls back into part 0 on the way.
         A breakpoint there must fire, and the selection must be the word
"""
import os, sys, subprocess, tempfile, argparse, functools
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tests"); sys.path.insert(0, "tools")   # tools/heapmap, not tests/
import os88marty as M
import os88geom, heapmap, os88build
from os88mouse import Mouse
import dispcp

u16 = lambda b, i=0: b[i] | (b[i+1] << 8)
FAIL = []


def check(name, ok, detail=""):
    print("   %-58s %s%s" % (name, "ok" if ok else "FAIL", "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


def pkg_syms(src="apps/word/word.asm", incs=("apps/", "apps/word/")):
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"] + sum([["-I", i] for i in incs], [])
                       + ["-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for L in open(mp):
            f = L.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[1], 16)   # VIRTUAL: part 1 is assembled at WD_P1ORG
        return out, open(os.path.join(d, "p.bin"), "rb").read()


ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_5150_cga_gla")
a = ap.parse_args()
syms, image = pkg_syms()
P1ORG = syms["wd_p1org"]
p1 = open(os88build.at("build/word.p1.bin"), "rb").read()
print("== Word: one file, part 1 at the top of its segment (SPEC.md 68.10) ==")
print("   WD_P1ORG = 0x%04X, part 1 = %d bytes" % (P1ORG, len(p1)))
if p1 != image[u16(image, 8):]:
    sys.exit("build/word.p1.bin is not the tail of this tree's word.asm - "
             "run `make build/word.o88`")
DISK = "build/wdpartsgate.img"
M.scratch_disk(DISK, "build/word.o88", "build/WELCOME.DOC")
S = lambda n: m.sym(n)

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    M.settle(m); mo = Mouse(marty=m)
    dispcp.open_drive(m, mo, S, M.settle, "B")
    w = dispcp.win_list(m, S)[-1]; dx, dy = dispcp.win_rect(m, S, w)[:2]
    dispcp.open_named(m, mo, S, M.settle, dx, dy, "WELCOME.DOC")

    def word_inst():
        """(segment, slot, record) of the instance running this word.asm.
        I_SPTR is published after the entry proc - and its wd_arg - return."""
        for i in range(12):
            r = m.read(S("inst_tab") + i * os88geom.I_RECSZ, os88geom.I_RECSZ)
            c = u16(r, os88geom.I_SPTR)
            if c and m.read(c*16 + syms["wd_mact"], 48) == \
                    image[syms["wd_mact"]:syms["wd_mact"] + 48]:
                return c, i, r
        return None, None, None
    try:
        M.until(m, lambda _: word_inst()[0], "Word's instance", poll=0.3,
                limit=60)
    except M.MartyError:
        pass                            # check A says so
    M.settle(m)
    seg, slot, rec = word_inst()
    check("A: WELCOME.DOC opened Word through the loader", seg is not None,
          "no running instance carries this tree's word.asm (ld_status %d)"
          % m.read(S("ld_status"), 1)[0])
    if seg is None:
        sys.exit(1)
    base = seg * 16
    dlen = u16(m.read(base + syms["wd_len"], 2))
    check("A: ...and the document name survived the re-home", dlen > 0,
          "[wd_len] = 0: the program came up empty, so OSAPI_ARG_FILE was "
          "spent or lost before wd_arg asked")
    print("      instance %d at %04X, document %d bytes" % (slot, seg, dlen))

    isize = u16(rec, os88geom.I_SIZE)
    check("B: I_SIZE is WD_P1ORG", isize == P1ORG,
          "I_SIZE = %d, want %d - the region the program declared "
          "(20.12.10.4)" % (isize, P1ORG))

    hm = heapmap.Map(m, {n: S(n) for n in ("mem_base", "mem_top", "spl_live",
                                          "mem_tab")})
    cl = [c for c in hm.claims if c.seg == seg]
    need = (P1ORG + len(p1) + 15) // 16
    check("C: the region's claim holds part 1 too", cl and cl[0].para >= need,
          "claim at %04X is %s paragraphs, part 1 ends at paragraph %d"
          % (seg, cl[0].para if cl else "missing", need))

    got = m.read(base + P1ORG, len(p1))
    bad = [i for i in range(len(p1)) if got[i] != p1[i]]
    check("D: program:WD_P1ORG holds part 1, byte for byte", not bad,
          "%d of %d bytes differ, first at +%d" % (len(bad), len(p1),
                                                     bad[0] if bad else 0))

    # E: Edit > Search..., type a word the document has, Enter.
    note = m.read(u16(m.read(base + syms["wd_dseg"], 2)) * 16, dlen)
    word = b"flush"
    want = note.lower().find(word)
    check("the document has the word (case, not assertion)", want >= 0,
          "no %r in WELCOME.DOC" % word)
    hits = []
    m.key("ControlLeft", down=True, up=False); m.key("Home")
    m.key("ControlLeft", down=False, up=True); M.pace(m, 1.0); M.settle(m)
    with M.bp_trace(m, base + syms["wd_pcomp"],
                    on_hit=lambda mm, r: hits.append(1), cap=10):
        m.key("AltLeft", down=True, up=False); m.key("KeyE")
        m.key("AltLeft", down=False, up=True); M.pace(m, 0.8)
        m.key("KeyS"); M.pace(m, 1.2); M.settle(m)
        m.type_text(word.decode()); M.pace(m, 0.5)
        m.key("Enter"); M.ui_done(m, "the search to run")
    M.settle(m)
    s0, s1 = u16(m.read(base + syms["wd_sel0"], 2)), u16(m.read(base + syms["wd_sel1"], 2))
    check("E: wd_pcomp - part 1 - ran", bool(hits),
          "no hit at %04X:%04X" % (seg, syms["wd_pcomp"]))
    check("E: ...and the search selected the word", note[s0:s1].lower() == word,
          "selection %d..%d is %r, first %r is at %d"
          % (s0, s1, note[s0:s1], word, want))

print()
if FAIL:
    print("FAILED: " + ", ".join(FAIL)); sys.exit(1)
print("ok"); sys.exit(0)
