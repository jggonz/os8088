#!/usr/bin/env python3
"""WORD'S AND SCRIBE'S DOCUMENT CLAIMS MOVE, AND THE DOCUMENT IS STILL THERE
(SPEC.md 68.3.2, 95.14).

    make && make build/heapfrag.o88 && python3 tests/wdheapmove.py --app word
    python3 tests/wdheapmove.py --app scribe

Until NO-PINNING A2 every claim either program made was PINNED by default -
the text, its CHP twin, the PAP dictionary, the italic glyph table, the two
undo arenas and (SCRIBE) each picture - so a long session with a document
open left up to ~70KB of walls in the arena that no compaction could reach.
They declare one relocation proc each now, and this row is what says the
proc fixes EVERY word: a proc that leaves one out is not a crash, it is the
next keystroke written into somebody else's block.

paintmove.py's recipe, and cmemmove.py's: heapfrag owns the floor, the editor
lands above it, heapfrag's death opens the floor, and heapfrag re-run makes
the claim that forces the pass. What the editor is made to hold first:

  * a DOCUMENT (WELCOME.DOC grown past 4KB - main() says why the size
    matters), whose first screen carries italics - so the
    italic table is claimed by the first paint, from inside a walk that holds
    ES = the document. That is the one claim either program makes with a
    segment in a register, and wd_itinit / sc_itinit pin the rest across it
    (SPEC.md 68.3.2);
  * typing, then a DELETION once the typing group has closed - an insert
    records a count and no bytes, so only a deletion claims the undo arenas;
  * (SCRIBE) a PICTURE, by Insert > Picture of a small BMP.

THE ASSERTIONS, after the forced pass:

  1. every claim the editor holds, bar its own region, is DECLARED (MC_RLOC),
     read from the kernel's own table - a pin nobody meant is the defect the
     plan exists for;
  2. every base word names a claim the editor still holds - the one-word
     proc is exact or it is wrong, and a stale word fails HERE rather than as
     a corrupted document twenty keystrokes later;
  3. EVERY ONE MOVED - the layout is built so that each claim has the floor's
     hole beneath it, and a claim that stayed put is a word this run did not
     test - and each has the same bytes at its new base (only the LIVE bytes
     are hashed: the italic table's staging half and an arena's tail are
     redrawn and rewritten by design);
  4. the document still EDITS: three characters typed at the caret land at
     the caret in both claims, read back out of the guest.

Verified to go red: leave wd_cseg out of wd_rltab and 2 fails on [wd_cseg]
(it names a block the editor no longer holds) and 4 fails on the CHP half.
"""
import argparse
import functools
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

print = functools.partial(print, flush=True)
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(_ROOT)
sys.path.insert(0, os.path.join(_ROOT, "tools"))
sys.path.insert(0, os.path.join(_ROOT, "tests"))
import os88fixture             # noqa: E402
import os88marty as M          # noqa: E402
import os88ui                  # noqa: E402
import os88geom                # noqa: E402
import heapmap                 # noqa: E402

u16 = lambda b, i=0: b[i] | (b[i + 1] << 8)      # noqa: E731
FAIL = []

APPS = {
    "word": dict(src="apps/word/word.asm", incs=("apps/", "apps/word/"),
                 pkg="build/word.o88", name="WORD.O88", p="wd",
                 title="Microsoft Word"),
    "scribe": dict(src="apps/scribe/scribe.asm",
                   incs=("apps/", "apps/scribe/"),
                   pkg="build/scribe.o88", name="SCRIBE.O88", p="sc",
                   title="Scribe"),
}
WORDS = ("dseg", "cseg", "pseg", "iseg", "useg", "cuseg")
IT_TABLE = 95 * 32          # the italic claim's glyph table; the rest is staging


def check(name, ok, detail=""):
    print("   %-58s %s%s" % (name, "ok" if ok else "FAIL",
                             "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


def pkg_syms(src, incs):
    """{name: offset}, by re-assembling the package (paintmove.py's trick):
    the bss is `equ os88_image_end + N`, neither greppable nor stable."""
    with tempfile.TemporaryDirectory(dir="build") as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + sum([["-I", i] for i in incs], [])
                       + ["-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out, open(os.path.join(d, "p.bin"), "rb").read(32)


def bmp4(w, h, seed):
    """A 4bpp bottom-up BMP (tests/scsize.py's)."""
    stride = ((w + 1) // 2 + 3) & ~3
    pal = b"".join(struct.pack("<BBBB", (i * 37) & 255, (i * 91) & 255,
                               (i * 17) & 255, 0) for i in range(16))
    rows = b""
    for y in range(h):
        r = bytes((((x * 2 + y + seed) % 16) << 4) | ((x * 2 + 1 + y * 3) % 16)
                  for x in range((w + 1) // 2))
        rows += r + b"\0" * (stride - len(r))
    off = 14 + 40 + 64
    return (b"BM" + struct.pack("<IHHI", off + len(rows), 0, 0, off)
            + struct.pack("<IiiHHIIiiII", 40, w, h, 1, 4, 0, len(rows),
                          2835, 2835, 16, 16) + pal + rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", choices=sorted(APPS), default="word")
    # VGA, as tests/scsize.py: SCRIBE's dialogs need more content than a
    # 200-line window has, and one machine for both arms keeps them alike
    ap.add_argument("--machine", default="os8088_xt_vga")
    a = ap.parse_args()
    cfg = APPS[a.app]
    p = cfg["p"]

    os88fixture.need("build/heapfrag.o88", cfg["pkg"])
    syms, head = pkg_syms(cfg["src"], cfg["incs"])
    tmp = tempfile.mkdtemp(prefix="wdheapmove-", dir="build")
    # THE DOCUMENT IS WELCOME.DOC GROWN PAST 4KB, and the size is the point.
    # A load ends in wd_fitclaim's REGROW, and a regrow that cannot extend in
    # place lands HIGHEST-fit (SPEC.md 50.3.1). WELCOME.DOC's 3KB fits the
    # ceiling gap between the program's own region and the one above it,
    # where the region is a barrier to the ascending pass and nothing ever
    # opens over it - so its text claim is declared, named and never moved,
    # and a proc that forgot [wd_dseg] passed. Past that gap it lands under
    # the region with the floor's hole beneath it, and the pass moves it.
    wtx = os.path.join(tmp, "BIG.WTX")
    with open(wtx, "w") as f:
        f.write(open("apps/word/welcome.wtx").read())
        for i in range(24):
            f.write("Filler paragraph %d, so that the text claim cannot fit "
                    "the ceiling gap and has to move: {i}italic{/i} and "
                    "{b}bold{/b} ride along.\n" % i)
    big = os.path.join(tmp, "BIG.DOC")
    subprocess.run([sys.executable, "tools/os88doc.py", wtx, "-o", big],
                   check=True)
    files = ["build/heapfrag.o88", cfg["pkg"], big]
    if a.app == "scribe":
        open(os.path.join(tmp, "SCP.BMP"), "wb").write(bmp4(64, 48, 1))
        files.append(os.path.join(tmp, "SCP.BMP"))
    disk = M.scratch_disk("build/wdheapmove-%s.img" % a.app, *files, size=360)

    print("== %s's document claims move (SPEC.md 68.3.2 / 95.14), %s =="
          % (a.app.upper(), a.machine))
    with os88ui.boot("build/os8088-360.img", apps=disk, machine=a.machine,
                     quick=True) as ui:
        m = ui.m
        S = ui._S
        mega = getattr(m, "bochs", False)

        def L(sg, off=0):
            """SEG:OFF linear: an LDT selector's base on kern_mega"""
            return m.lin(sg, off) if mega else sg * 16 + off

        def K(v):
            """A segment word as the heap keys its block - itself on a
            real-mode kernel. On kern_mega (KERN-MEGA-MEMORY-PLAN MC7.4) the
            region, every base word and every claim owner are LDT selectors
            a move REBASES and never changes, so "moved", "names a claim it
            holds" and the bytes are asked of the PARAGRAPH under each"""
            if mega and v and 0x100 <= v < 0xA000 and v & 4:
                b = m.selector(v)
                return b[0] >> 4 if b else v
            return v

        def idle(what):
            M.ui_done(m, what, limit=600)

        def claims():
            hm = heapmap.Map(m, {n: S(n) for n in ("mem_base", "mem_top",
                                                   "spl_live", "mem_tab")})
            return hm.claims

        def instance():
            """The editor's segment, found by its header (scsize.py's way):
            its window's W_SEG would do as well, but the instance is up
            before the window is."""
            for i in range(12):
                r = m.read(S("inst_tab") + i * os88geom.I_RECSZ,
                           os88geom.I_RECSZ)
                c = u16(r, os88geom.I_SPTR)
                h = bytes(m.read(L(c), 32)) if c else b""
                if (r[os88geom.I_STATE] == 1 and c and h[:2] == head[:2]
                        and h[12:] == head[12:]):
                    return c
            return None

        def heapfrag():
            for w in ui.windows():
                if w.title.startswith("Heap"):
                    return w
            return None

        def heapfrag_ran():
            """heapfrag's suite has RETURNED and its posted pass landed
            (tests/cmemmove.py's test: the suite runs inside its first
            W_PAINT with the gfx lock held)."""
            w = heapfrag()
            if not w:
                return False
            seg = u16(m.read(os88geom.winptr(m, w.i, S) + os88geom.W_SEG, 2))
            img = u16(m.read(L(seg, 8), 2))
            b = m.read(L(seg, img), 48)
            done, posted, woke = b[28], b[46], b[47]
            return bool(done and not m.read(S("gfx_lock_flag"), 1)[0]
                        and (woke or not posted))

        def run_heapfrag():
            if any(w.title.startswith("Disk") for w in ui.windows()):
                # the drive's desktop zone is under the editor by now, so
                # the Disk window already open is the way in
                # ...named by TITLE: after a chooser [fm_vinst] names the
                # chooser's instance, so disk_window() has no answer
                dw = ui.window("Disk")
                ui.raise_window(dw)
                ui.open("HEAPFRAG.O88", win=dw)
            else:
                ui.path("B:/HEAPFRAG.O88")
            M.until(m, lambda _: heapfrag_ran(),
                    "heapfrag's suite and its posted pass", poll=0.5,
                    limit=180)
            idle("heapfrag")

        seg = [None]

        def W(n):
            return u16(m.read(L(seg[0], syms[n]), 2))

        def doc():
            n = W(p + "_len")
            return (bytes(m.read(K(W(p + "_dseg")) * 16, n)),
                    bytes(m.read(K(W(p + "_cseg")) * 16, n)))

        def live(word, base):
            """The bytes of the claim `word` names that mean something."""
            if word == "dseg" or word == "cseg":
                n = W(p + "_len")
            elif word == "pseg":
                n = 1024
            elif word == "iseg":
                n = IT_TABLE
            else:
                n = W(p + "_utop")
            return hashlib.md5(bytes(m.read(base * 16, n))).hexdigest(), n

        def heapdump(what):
            if not os.environ.get("WDHMDBG"):
                return
            print("   --- the arena, %s" % what)
            for c in claims():
                print("     %04x %4dKB own %04x%s%s"
                      % (c.seg, c.para // 64, c.own,
                         "" if c.pinned else " movable",
                         " top-down" if c.hi else ""))

        def menu(title, item):
            m.alt(title)
            idle("the menu")
            m.key(item)
            idle("the item")

        # --- heapfrag first, so it owns the floor of the arena --------------
        run_heapfrag()
        print("   heapfrag up")

        # --- then the editor, with a document, above it ----------------------
        if a.app == "word":
            ui.path("B:/BIG.DOC")              # .DOC is Word's (SPEC.md 68.4)
            M.until(m, lambda _: instance(), "Word's instance", poll=0.1,
                    limit=120)
            seg[0] = instance()
            idle("Word's open")
        else:
            ui.path("B:/SCRIBE.O88")           # Scribe declares no association
            M.until(m, lambda _: instance(), "Scribe's instance", poll=0.1,
                    limit=120)               # (SPEC.md 95.2): File > Open
            seg[0] = instance()
            idle("Scribe's launch")
            menu("KeyF", "KeyO")
            ui.chooser_open("BIG.DOC")
            idle("the open")
        M.until(m, lambda _: W(p + "_len") > 4096, "the document to load",
                poll=0.1, limit=120)
        M.until(m, lambda _: W(p + "_iseg") != 0,
                "the italic table (the first screen has italics)",
                poll=0.1, limit=60)
        print("   %s at %04x, document %d characters"
              % (a.app, seg[0], W(p + "_len")))

        # --- give it an undo arena: type, let the group close, delete -------
        n0 = W(p + "_len")
        m.type_text("compactor ")
        M.until(m, lambda _: W(p + "_len") == n0 + 10
                and m.read(L(seg[0], syms[p + "_uopen"]), 1)[0] == 0,
                "the typing, and its undo group to close", poll=0.1, limit=60)
        idle("the typing")
        for _ in range(4):
            m.key("Backspace")
        M.until(m, lambda _: W(p + "_len") == n0 + 6 and W(p + "_useg") != 0,
                "the deletion, and the undo arena it claims", poll=0.1,
                limit=60)
        idle("the deletion")

        pics = 0
        if a.app == "scribe":
            menu("KeyI", "KeyP")
            ui.chooser_open("SCP.BMP")
            M.until(m, lambda _: W("sc_npic") == 1, "the picture", poll=0.05,
                    limit=600)
            idle("the picture")
            pics = 1

        def picseg(i):
            return K(u16(m.read(L(seg[0], syms["sc_pictab"] + i * 8 + 6), 2)))

        def picbytes(i, base):
            rec = bytes(m.read(L(seg[0], syms["sc_pictab"] + i * 8), 8))
            n = u16(rec, 4) * u16(rec, 2)
            return hashlib.md5(bytes(m.read(base * 16, n))).hexdigest()

        # --- bank every base word and its live bytes ------------------------
        bank = {}
        for wd in WORDS:
            b = K(W(p + "_" + wd))
            bank[wd] = (b, ) + live(wd, b) if b else (0, None, 0)
            print("   [%s_%s] = %04x" % (p, wd, b))
        pbank = {i: (picseg(i), picbytes(i, picseg(i))) for i in range(pics)}
        for i, (b, _) in pbank.items():
            print("   [sc_pictab+%d] = %04x" % (i * 8 + 6, b))
        check("SETUP: all six claims exist",
              all(bank[wd][0] for wd in WORDS),
              "%r" % {wd: "%04x" % bank[wd][0] for wd in WORDS})
        mine0 = [c for c in claims() if K(c.own) == K(seg[0])
                 and c.seg != K(seg[0])]
        print("   it holds %s" % ["%04x/%dKB%s" % (c.seg, c.para // 64,
                                                  "" if c.pinned else "*")
                                  for c in mine0])

        heapdump("before")
        # --- close heapfrag: the floor under the editor opens up ------------
        ui.close(heapfrag())
        idle("heapfrag's close")
        heapdump("heapfrag closed")

        # --- and run it again, whose big claim forces the compaction --------
        run_heapfrag()

        heapdump("after")
        seg0 = seg[0]
        seg[0] = instance()                    # the REGION may move too
        if seg[0] != seg0:
            print("   %s ITSELF moved %04x -> %04x (its region is movable)"
                  % (a.app, seg0, seg[0]))
        mine = [c for c in claims() if K(c.own) == K(seg[0])
                and c.seg != K(seg[0])]
        held = {c.seg for c in mine}
        print("   it now holds %s" % ["%04x/%dKB%s" % (c.seg, c.para // 64,
                                                      "" if c.pinned else "*")
                                      for c in mine])

        # 1 every claim declared
        pinned = [c for c in mine if c.pinned]
        check("1 every claim it holds is declared movable", not pinned,
              "pinned: %r" % ["%04x/%dKB" % (c.seg, c.para // 64)
                              for c in pinned])

        # 2 / 3 every word names a held claim; moved ones kept their bytes
        moved = 0
        for wd in WORDS:
            b0, h0, n = bank[wd]
            b1 = K(W(p + "_" + wd))
            if b1 != b0:
                moved += 1
            check("2 [%s_%s] %s names a claim it holds"
                  % (p, wd, ("%04x -> %04x" % (b0, b1)) if b1 != b0
                     else "%04x (still)" % b0),
                  b1 in held, "a stale word: the proc missed it")
            if b1 != b0 and b1 in held:
                check("3 [%s_%s] its %d live bytes came with it" % (p, wd, n),
                      live(wd, b1)[0] == h0, "CORRUPT")
        for i, (b0, h0) in pbank.items():
            b1 = picseg(i)
            if b1 != b0:
                moved += 1
            check("2 picture %d %s names a claim it holds"
                  % (i, ("%04x -> %04x" % (b0, b1)) if b1 != b0
                     else "%04x (still)" % b0),
                  b1 in held, "a stale slot: sc_reloc missed it")
            if b1 != b0 and b1 in held:
                check("3 picture %d: its pixels came with it" % i,
                      picbytes(i, b1) == h0, "CORRUPT")
        check("3 every claim moved (else that word went untested)",
              moved == len(WORDS) + pics,
              "%d of %d - one sat where no hole opened beneath it"
              % (moved, len(WORDS) + pics))
        print("   %d of %d base words moved" % (moved, len(WORDS) + pics))

        # 4 it still edits: three characters at the caret, read back
        ui.raise_window(ui.window(cfg["title"]))
        idle("the raise")
        t0, c0 = doc()
        cur = W(p + "_cur")
        m.type_text("XYZ")
        M.until(m, lambda _: W(p + "_len") == len(t0) + 3, "the typing",
                poll=0.1, limit=60)
        idle("the typing")
        t1, c1 = doc()
        check("4 the text took three characters at the caret",
              t1 == t0[:cur] + b"XYZ" + t0[cur:],
              "caret %d: %r -> %r" % (cur, t0[max(0, cur - 8):cur + 8],
                                      t1[max(0, cur - 8):cur + 11]))
        check("4 ...and the CHP claim the same three cells",
              len(c1) == len(c0) + 3 and c1[:cur] == c0[:cur]
              and c1[cur + 3:] == c0[cur:], "the CHP twin did not follow")

    print("VERDICT:", "OK" if not FAIL else "%d PROBLEM(S)" % len(FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
