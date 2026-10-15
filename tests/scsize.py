#!/usr/bin/env python3
"""SCRIBE'S CLAIMS AND ITS SHARED SCRATCH, ON THE MACHINE (SPEC.md 95.13).

    make scribedisk && python3 tests/scsize.py [--machine NAME] [--src DIR]

The first apps size pass changed four things in SCRIBE that no row covered,
because no row drove SCRIBE at all:

  OVL       SCRIBE.OVL is loaded at entry into a claim of exactly SC_OVKB KB
  ROUND     .DOC and .RTF Save As and re-open give back the SAME document,
            text and CHP bytes both. The .DOC writer's FKP page and banks
            and the RTF reader's group stack are arms of one union with
            Insert > Picture's row buffer and Sort's arrays (95.13), so a
            wrong arm boundary or an arm that relied on zeroed bss shows up
            here as a document that does not come back
  PICTURE   Insert > Picture of a small BMP: the picture's claim is EXACTLY
            its own KB (the decoder's 40KB scratch shrunk in place), no
            scratch or staging claim is left behind, and the picture
            survives an .RTF round trip byte for byte
  TOOBIG    a 63,618-byte picture file - between the 62KB staging ceiling
            and 64KB - is REFUSED with "That file is too big". The old read
            had a 0xFFFF capacity into a 62KB claim and wrote up to 2KB
            past the claim's end (130 bytes, this file); the heap's own
            records are checked after
  SORT      Utilities > Sort over the whole document leaves its paragraphs
            a permutation of what they were and in order, and a second Sort
            changes nothing
  EDIT      two Undos give the unsorted document back - one of the cold
            commands whose tails are sc_ld_N's ladder now (95.13). Copy and
            Paste are NOT driven: Shift+ArrowRight on the XT keyboard is
            keypad 6 and types a '6' rather than selecting

Every leg prints a digest of the document bytes it ended on, so the same run
against a build of another tree (--src, --pkg, --ovl) can be compared line
for line: the base and this tree must print the same digests.
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
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty as M          # noqa: E402
import os88ui                  # noqa: E402
import os88geom                # noqa: E402
import heapmap                 # noqa: E402
import os88pkg                 # noqa: E402
import os88lz                  # noqa: E402

u16 = lambda b, i=0: b[i] | (b[i + 1] << 8)      # noqa: E731
FAIL = []


def check(name, ok, detail=""):
    print("   %-60s %s%s" % (name, "ok" if ok else "FAIL",
                             "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


def pkg_syms(src):
    """{name: offset} of SCRIBE as `src` assembles, and its trimmed image's
    first bytes (the header the instance is found by)."""
    with tempfile.TemporaryDirectory(dir="build") as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(os.path.join(src, "scribe.asm")).read()
                            + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", src + "/", "-o", os.path.join(d, "p.bin"), cp],
                       check=True)
        out = {}
        for L in open(mp):
            f = L.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out.setdefault(f[2], int(f[1], 16))
        return out, open(os.path.join(d, "p.bin"), "rb").read()[:32]


def bmp4(w, h, seed):
    """A 4bpp bottom-up BMP, w x h, a deterministic pattern."""
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
    ap.add_argument("--machine", default="os8088_xt_vga")  # a dialog needs
    # more content than a 200-line CGA window has (sc_dgopen refuses with a toast)
    ap.add_argument("--src", default="apps/scribe")
    ap.add_argument("--pkg", default="build/scribe.o88")
    ap.add_argument("--ovl", default="build/ovlz/SCRIBE.OVL")
    a = ap.parse_args()

    # THE PAIR MUST BE ONE BUILD. SCRIBE.OVL addresses the package's bss and
    # vector table by offset, so another build's loaded and read a document of
    # nothing; it cost this row's first run on the ladder. The package refuses
    # one now (SPEC.md 95.8.7) - which here would read as an overlay that
    # never loads - so the same comparison is made first and named: the
    # module's word 0 against the immediate sc_ovload compares it with.
    syms, head = pkg_syms(a.src)
    img = os88pkg.image_unwrap(open(a.pkg, "rb").read())
    mod = os88lz.cz_unwrap(open(a.ovl, "rb").read())
    at = syms.get("sc_ovload.stimm")    # absent on a pre-stamp --src tree
    want, have = (u16(img, at - 2) if at else None), u16(mod)
    if at and want != have:
        print("scsize: %s carries build stamp %04X and %s %04X - not one "
              "build (`make scribe` brings both)" % (a.pkg, want, a.ovl, have))
        return 1
    tmp = tempfile.mkdtemp(prefix="scsize-", dir="build")
    small = bmp4(64, 48, 1)
    big = bmp4(1000, 127, 2)
    assert 62 * 1024 < len(big) < 65536, len(big)
    open(os.path.join(tmp, "SCP.BMP"), "wb").write(small)
    open(os.path.join(tmp, "SCBIG.BMP"), "wb").write(big)
    disk = M.scratch_disk("build/scsize.img", a.pkg, a.ovl,
                          "build/WELCOME.DOC", os.path.join(tmp, "SCP.BMP"),
                          os.path.join(tmp, "SCBIG.BMP"), size=360)
    ovkb = int([L.split()[2] for L in open(os.path.join(a.src, "scribe.asm"))
                if L.startswith("SC_OVKB")][0])
    print("== SCRIBE's claims and shared scratch (SPEC.md 95.13), %s, %s =="
          % (a.machine, a.src))
    with os88ui.boot("build/os8088-360.img", apps=disk, machine=a.machine) as ui:
        m = ui.m
        S = ui._S

        def instance():
            for i in range(12):
                r = m.read(S("inst_tab") + i * os88geom.I_RECSZ,
                           os88geom.I_RECSZ)
                c = u16(r, os88geom.I_SPTR)
                h = bytes(m.read(c * 16, 32)) if c else b""
                # the flags (+3: packed) and the image size (+8: the
                # trim) are the packager's; the entry and name are ours
                if (r[os88geom.I_STATE] == 1 and c and h[:2] == head[:2]
                        and h[12:] == head[12:]):
                    return c
            return None

        ui.path("B:/SCRIBE.O88")
        M.until(m, lambda _: instance(), "SCRIBE's instance", poll=0.3,
                limit=120)
        base = instance() * 16
        W = lambda n, k=0: u16(m.read(base + syms[n] + k, 2))   # noqa: E731

        def claims():
            hm = heapmap.Map(m, {n: S(n) for n in ("mem_base", "mem_top",
                                                   "spl_live", "mem_tab")})
            return hm.claims

        def mine():
            seg = base >> 4
            return sorted((c.seg, c.para) for c in claims()
                          if c.own == seg and c.seg != seg)

        def doc():
            n = W("sc_len")
            t = bytes(m.read(W("sc_dseg") * 16, n))
            c = bytes(m.read(W("sc_cseg") * 16, n))
            return t, c

        def digest(what):
            t, c = doc()
            h = hashlib.sha1(t + b"|" + c).hexdigest()[:12]
            print("      digest %-24s len %5d  %s" % (what, len(t), h))
            if os.environ.get("SCDBG"):
                print("        head %r" % t[:60])
                print("        tail %r" % t[-60:])
            return t, c

        def idle(what):
            M.ui_done(m, what, limit=600)

        def menu(title, item):
            m.alt(title)
            idle("the menu")
            m.key(item)
            idle("the item")

        def open_doc(name):
            menu("KeyF", "KeyO")
            ui.chooser_open(name)
            idle("open " + name)

        def save_as(name):
            menu("KeyF", "KeyA")
            ui.chooser_save(name)
            M.until(m, lambda _: W("sc_dirty") == 0 and W("sc_stgseg") == 0,
                    "the save of " + name, poll=0.3, limit=600)
            idle("save " + name)

        M.until(m, lambda _: W("sc_ovseg"), "SCRIBE.OVL at entry", poll=0.3,
                limit=120)
        idle("launch")
        ov = [c for c in claims() if c.seg == W("sc_ovseg")]
        check("OVL: loaded at entry into a claim of SC_OVKB = %d KB" % ovkb,
              len(ov) == 1 and ov[0].para == ovkb * 64,
              "%r" % [(c.seg, c.para) for c in ov])

        # --- ROUND ---------------------------------------------------------
        open_doc("WELCOME.DOC")
        t0, c0 = digest("WELCOME.DOC")
        check("ROUND: WELCOME.DOC loaded", len(t0) > 100, "%d" % len(t0))
        check("ROUND: the staging claim went back", W("sc_stgseg") == 0)
        save_as("T1.DOC")
        open_doc("T1.DOC")
        t1, c1 = digest("T1.DOC")
        check("ROUND: .DOC Save As and re-open: same text", t1 == t0)
        check("ROUND: ...and the same CHP bytes", c1 == c0)
        save_as("T1.RTF")
        open_doc("T1.RTF")
        t2, c2 = digest("T1.RTF")
        # An RTF file closes its last paragraph with \par, so the document
        # that comes back ends in a mark the .DOC one did not have - on the
        # base tree too, and SPEC.md 68.8's reader, not this pass
        check("ROUND: .RTF Save As and re-open: same text, + a final mark",
              t2 == t0 + b"\r", "%d -> %d bytes" % (len(t0), len(t2)))
        # (its CHP bytes are NOT the .DOC's: the RTF reader dresses runs
        # its own way, and the digest - equal to the base tree's - is the
        # check on them)

        # --- PICTURE -------------------------------------------------------
        before = mine()
        menu("KeyI", "KeyP")
        ui.chooser_open("SCP.BMP")
        M.until(m, lambda _: W("sc_npic") == 1, "the picture", poll=0.3,
                limit=600)
        idle("insert")
        rec = bytes(m.read(base + syms["sc_pictab"], 8))
        pw, ph, ps, pseg = (u16(rec, k) for k in (0, 2, 4, 6))
        check("PICTURE: 64 x 48 in the table", (pw, ph) == (64, 48),
              "%d x %d" % (pw, ph))
        kb = (ps * ph + 1023) // 1024
        pc = [c for c in claims() if c.seg == pseg]
        check("PICTURE: its claim is exactly its own %d KB" % kb,
              len(pc) == 1 and pc[0].para == kb * 64,
              "%r" % [(c.seg, c.para) for c in pc])
        check("PICTURE: no scratch or staging claim left",
              W("sc_picseg") == 0 and W("sc_stgseg") == 0
              and mine() == sorted(before + [(pseg, kb * 64)]),
              "%r -> %r" % (before, mine()))
        pix = bytes(m.read(pseg * 16, ps * ph))
        save_as("T2.RTF")
        open_doc("T2.RTF")
        digest("T2.RTF")
        rec2 = bytes(m.read(base + syms["sc_pictab"], 8))
        pseg2 = u16(rec2, 6)
        check("PICTURE: survives an .RTF round trip, byte for byte",
              W("sc_npic") == 1 and rec2[:6] == rec[:6]
              and bytes(m.read(pseg2 * 16, ps * ph)) == pix)

        # --- TOOBIG --------------------------------------------------------
        held = mine()
        menu("KeyI", "KeyP")
        ui.chooser_open("SCBIG.BMP")
        said = ui.wait_toast(says="too big", limit=120)
        idle("refused")
        check("TOOBIG: a 63,618-byte FILE is refused before it is read",
              said == "That file is too big", "%r - the old read put it "
              "~130 bytes past a 62KB claim and the decoder refused it" % said)
        check("TOOBIG: nothing claimed, nothing inserted, heap whole",
              mine() == held and W("sc_npic") == 1, "%r -> %r"
              % (held, mine()))

        # --- SORT ----------------------------------------------------------
        open_doc("WELCOME.DOC")
        paras0 = sorted(doc()[0].split(b"\r"))
        menu("KeyU", "KeyO")
        m.key("Enter")
        idle("sorted")
        t3, c3 = digest("sorted")
        check("SORT: a permutation of the paragraphs",
              sorted(t3.split(b"\r")) == paras0)
        menu("KeyU", "KeyO")
        m.key("Enter")
        idle("sorted again")
        t4, c4 = digest("sorted again")
        check("SORT: a second Sort changes nothing", (t4, c4) == (t3, c3))

        # --- EDIT: the cold commands sc_ld_N's ladder ends (95.13) --------
        tw, cw = digest("before undo")
        menu("KeyE", "KeyU")                   # Undo the second Sort...
        menu("KeyE", "KeyU")                   # ...and the first
        t5, c5 = digest("undone twice")
        t6 = t0                                # (re-opening would ask
        check("EDIT: two Undos give back the unsorted document",   # to save)
              t5 == t0 and t5 != tw, "%d vs %d bytes" % (len(t5), len(t0)))
    for f in ("SCP.BMP", "SCBIG.BMP"):
        os.unlink(os.path.join(tmp, f))
    os.rmdir(tmp)
    print("scsize: %s" % ("FAIL: " + ", ".join(FAIL) if FAIL else "ok"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
