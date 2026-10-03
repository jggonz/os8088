#!/usr/bin/env python3
"""PIXEL'S PART BOUNDARY: fetch, far call, drop, fetch again (SPEC.md 106.5).

    python3 tests/pxparts.py

PiXEL is the first assembly package to FAR-CALL a lazy code part (SPEC.md
68.10 called it "the next step"), and every decoder it will have stands on
that boundary. Wave 1 proves it with the one part it ships, the keyboard
card (apps/pixel/pxhelp.asm), before anything depends on it. A wrong far
pointer, a part read to the wrong segment, a claim not given back or a
second fetch that cannot happen all fail SILENTLY in a viewer - the card is
just not there, or the heap is a little smaller every time - so every leg
here reads the guest rather than the glass:

  leg A  the launch: PIXEL.O88 from a scratch floppy, its instance found by
         the image it carries, and NO part fetched yet - the row is clear and
         PiXEL holds no claim but its region
  leg B  F1: op_fetch ran ONCE, both vectors were far-called ([px_pcalls] 2),
         INIT answered PXP_PROBE - a number only the part computes - and the
         card holds the part's lines byte for byte (apps/pixel/pxhelp.asm's)
  leg C  ...and the part was DROPPED: the row is clear again and PiXEL holds
         exactly the claims it held before
  leg D  a key takes the card down; F1 again FETCHES AGAIN (op_fetch twice
         now), calls twice more, and drops again - claims unchanged. The part
         is OP_COMP|OP_LAZY, so this is the leg the SHADOW is for (SPEC.md
         20.12.7.4.1): without it a dropped compressed row is spent and this
         second fetch refuses
  leg G  (run right after A, before any fetch) A FETCH THAT FAILS, THEN ONE
         THAT DOES NOT (the speed review's F1): PiXEL's home folder poked to a
         drive that is not there, F1 refused - and its unwinding drop must
         leave the compressed row a never-fetched one (op_drop asks
         OP_FETCHED, SPEC.md 20.12.7.4.1), not SPENT, which is what it became
         while the shadow word was still 0. Home put back, F1 fetches and the
         card comes up
  leg E  THE NEGATIVE CONTROL: the row's file offset pointed at sector 0, so
         the fetch reads the package's own header where a part should be -
         as a compressed stream, which either will not expand (op_fetch
         refuses) or expands to bytes without the part's signature (px_pcall
         refuses). Either way no card, the call count unmoved, and the claim
         given back - a check that never refuses is not a check
  leg F  THE GO-HOME BRACKET (SPEC.md 106.5, wave-1 review MIN-10): a PNG
         in a folder of its own, P/, opened through File > Revert with the
         record pointed there. The PNG part is fetched out of PIXEL.O88 in
         the root - so the fetch must go HOME and come BACK - and the
         picture then decodes from P/, which it could not if the bracket had
         left the instance standing at home ([px_pmoved] says the bracket
         moved; the picture decoded says it came back). The part is KEPT
         after a picture of its kind (106.18), and a PCX after it drops it
         for the SIMPLE part, which reads the PCX (106.20)
"""
import os, re, sys, subprocess, tempfile, argparse, functools
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tests"); sys.path.insert(0, "tools")
import os88marty as M
import os88ui, os88geom, heapmap, os88build

u16 = lambda b, i=0: b[i] | (b[i + 1] << 8)
FAIL = []
PROBE = 0x5850 ^ 0x1D06             # apps/pixel/pxpart.inc's PXP_PROBE
OP_T_ROWS, OP_FETCHED = 10, 32      # apps/os88parts.inc
OP_LAZY, OP_COMP = 8, 16


def check(name, ok, detail=""):
    print("   %-62s %s%s" % (name, "ok" if ok else "FAIL", "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


def pkg_syms():
    """PiXEL's symbols and image, assembled from THIS tree with a map."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open("apps/pixel/pixel.asm").read()
                            + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/pixel/", "-o", os.path.join(d, "p.bin"), cp],
                       check=True)
        out = {}
        for L in open(mp):
            f = L.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[1], 16)
        return out, open(os.path.join(d, "p.bin"), "rb").read()


def help_lines():
    """The card's lines, out of the part's own source."""
    src = open("apps/pixel/pxhelp.asm").read()
    body = src[src.index("ph_text:"):src.index("ph_end:")]
    out = []
    for ln in body.split("\n"):
        m = re.match(r"\s*db\s+(.*)$", ln)
        if not m:
            continue
        arg = m.group(1).split(";")[0].strip()
        if arg == "0":
            out.append("")
        else:
            out.append(re.match(r"'(.*)',\s*0$", arg).group(1))
    return out


ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_5150_cga_gla")
a = ap.parse_args()
syms, image = pkg_syms()
o88 = open(os88build.at("build/pixel.o88"), "rb").read()
if o88[:syms["op_table"]] != image[:syms["op_table"]]:   # the table's rows
                                                          # are os88pkg.py's
    sys.exit("build/pixel.o88's image is not this tree's pixel.asm - run "
             "`make build/pixel.o88`")
LINES = help_lines()
print("== PiXEL: the part boundary (SPEC.md 106.5) ==")
print("   op_table at +%04X, the card %d lines" % (syms["op_table"], len(LINES)))
DISK = "build/pxpartsgate.img"
sys.path.insert(0, "tools")
import pixcorpus
_fx = dict((n, d) for n, d, v in pixcorpus.corpus())
os.makedirs("build/pxparts", exist_ok=True)
for _n in ("P2_8.PNG", "C8.PCX"):
    open("build/pxparts/" + _n, "wb").write(_fx[_n])
M.scratch_disk(DISK, "build/pixel.o88", "P:build/pxparts/P2_8.PNG",
               "P:build/pxparts/C8.PCX")


def dir_cluster(img, name):
    """A root folder's first cluster, read off the FAT12 image itself."""
    d = open(img, "rb").read()
    bps, res, nfat, nroot = (u16(d, 11), u16(d, 14), d[16], u16(d, 17))
    spf = u16(d, 22)
    root = (res + nfat * spf) * bps
    for i in range(nroot):
        e = d[root + 32 * i:root + 32 * i + 32]
        if e[:11] == name.ljust(11).encode() and e[11] & 0x10:
            return u16(e, 26)
    raise SystemExit("no folder %s on %s" % (name, img))

with os88ui.boot("build/os8088-360.img", apps=DISK, machine=a.machine) as ui:
    m = ui.m
    S = ui._S
    ui.open_drive("B")
    ui.open("PIXEL.O88")

    def inst():
        """PiXEL's segment: the instance whose image carries this header."""
        for i in range(12):
            r = m.read(S("inst_tab") + i * os88geom.I_RECSZ, os88geom.I_RECSZ)
            c = u16(r, os88geom.I_SPTR)
            if c and m.read(c * 16, 32) == image[:32]:
                return c
        return None
    M.until(m, lambda _: inst(), "PiXEL's instance", poll=0.3, limit=60)
    seg = inst()
    base = seg * 16
    B = lambda n: m.read(base + syms[n], 1)[0]
    W = lambda n: u16(m.read(base + syms[n], 2))
    row = lambda: m.read(base + syms["op_table"] + OP_T_ROWS, 8)

    def claims():
        hm = heapmap.Map(m, {n: S(n) for n in ("mem_base", "mem_top",
                                              "spl_live", "mem_tab")})
        return sorted((c.seg, c.para) for c in hm.claims
                      if c.own == seg and c.seg != seg)

    def card():
        """The card's lines, as PiXEL's own line table points at them."""
        out = []
        t = base + syms["px_helptab"]
        for k in range(16):
            p = u16(m.read(t + 2 * k, 2))
            if not p:
                break
            s = m.read(base + p, 64)
            out.append(s[:s.index(0)].decode("ascii", "replace"))
        return out

    # --- A ------------------------------------------------------------------
    print("   PiXEL at %04X" % seg)
    r0 = row()
    c0 = claims()
    check("A: launched with no part fetched", not r0[1] & OP_FETCHED,
          "row flags %02X" % r0[1])
    packed = u16(r0, 6)
    check("A: the part is COMPRESSED and lazy, its zkb the packed length",
          r0[1] & (OP_COMP | OP_LAZY) == OP_COMP | OP_LAZY
          and 0 < packed < u16(r0, 4),
          "flags %02X zkb %d len %d" % (r0[1], packed, u16(r0, 4)))
    shadow = base + syms["op_zshadow"]
    check("A: ...and no call made", W("px_pcalls") == 0, "%d" % W("px_pcalls"))
    check("A: PiXEL holds no claim but its region", c0 == [],
          "claims %s" % c0)

    fetch = base + syms["op_fetch"]
    drop = base + syms["op_drop"]
    held = []                       # PiXEL's claims AT the drop: the part's

    def f1(what):
        del held[:]
        with M.bp_trace(m, fetch, drop, cap=8,
                        on_hit=lambda mm, r: held.append(claims())) as tr:
            m.key("F1")
            M.until(m, lambda _: B("px_helpon") or B("px_pcalls") != calls0,
                    what, poll=0.2, limit=60)
            M.ui_done(m, what)
        return tr.count(tr._by_addr[fetch & 0xFFFFF])

    # --- G: a failed fetch spends nothing - BEFORE any fetch, while the
    # shadow word is still 0: the case op_drop used to turn into OP_SPENT ----
    home = B("px_homevol")
    m.write(base + syms["px_homevol"], bytes((25,)))     # Z: nobody's
    calls0 = W("px_pcalls")
    m.key("F1")
    M.ui_done(m, "the refused fetch")
    M.pace(m, 1.0)
    check("G: home unreachable: F1 refused, nothing called",
          B("px_helpon") == 0 and W("px_pcalls") == calls0,
          "helpon %d calls %d -> %d" % (B("px_helpon"), calls0, W("px_pcalls")))
    check("G: ...and the row is a NEVER-FETCHED one still, not spent",
          not row()[1] & OP_FETCHED and u16(row(), 6) == packed
          and claims() == c0,
          "flags %02X zkb %04X claims %s" % (row()[1], u16(row(), 6), claims()))
    m.write(base + syms["px_homevol"], bytes((home,)))
    m.key("Escape")                 # (a toast, if one stands)
    M.ui_done(m, "the toast")
    calls0 = W("px_pcalls")
    n = f1("the card after the failure")
    check("G: home back: F1 fetches and the card is up", n == 1
          and B("px_helpon") == 1 and W("px_pcalls") == calls0 + 2,
          "fetched %d helpon %d calls %d" % (n, B("px_helpon"), W("px_pcalls")))
    m.key("Escape")
    M.until(m, lambda _: B("px_helpon") == 0, "the card to come down",
            poll=0.2, limit=30)
    M.ui_done(m, "the repaint")
    m.write(base + syms["px_pcalls"], b"\0\0")    # B counts from nothing


    # --- B and C -------------------------------------------------------------
    calls0 = W("px_pcalls")
    n = f1("the keyboard card")
    check("B: F1 fetched the part once", n == 1, "op_fetch ran %d times" % n)
    check("B: ...far-called both vectors", W("px_pcalls") == 2,
          "[px_pcalls] = %d" % W("px_pcalls"))
    check("B: ...INIT answered the part's probe", W("px_pres") == PROBE,
          "[px_pres] = %04X, want %04X" % (W("px_pres"), PROBE))
    check("B: ...and the card is up", B("px_helpon") == 1, "")
    got = card()
    check("B: the card's lines are the part's, byte for byte", got == LINES,
          "got %r" % got)
    check("B: the part held ONE claim of PiXEL's own while it was here",
          len(held) >= 2 and len(held[-1]) == len(c0) + 1,
          "claims at the fetch / drop: %s" % held)
    r1 = row()
    check("C: the part was dropped (its row is clear)", not r1[1] & OP_FETCHED,
          "flags %02X" % r1[1])
    check("C: ...and its zkb is the PACKED LENGTH again, from the shadow",
          u16(r1, 6) == packed and u16(m.read(shadow, 2)) == packed,
          "zkb %04X shadow %04X, want %04X" % (u16(r1, 6),
                                               u16(m.read(shadow, 2)), packed))
    check("C: ...and PiXEL holds what it held before", claims() == c0,
          "claims %s, were %s" % (claims(), c0))

    # --- D -------------------------------------------------------------------
    m.key("Escape")
    M.until(m, lambda _: B("px_helpon") == 0, "the card to come down",
            poll=0.2, limit=30)
    M.ui_done(m, "the repaint")
    calls0 = W("px_pcalls")
    n = f1("the card again")
    check("D: F1 again FETCHED AGAIN", n == 1, "op_fetch ran %d times" % n)
    check("D: ...and called twice more", W("px_pcalls") == 4,
          "[px_pcalls] = %d" % W("px_pcalls"))
    check("D: ...the card is the same card", card() == LINES, "")
    check("D: ...dropped again, the heap as it was", claims() == c0
          and not row()[1] & OP_FETCHED and u16(row(), 6) == packed,
          "claims %s, zkb %04X" % (claims(), u16(row(), 6)))

    # --- E: the negative control --------------------------------------------
    m.key("Escape")
    M.until(m, lambda _: B("px_helpon") == 0, "the card to come down",
            poll=0.2, limit=30)
    M.ui_done(m, "the repaint")
    off = base + syms["op_table"] + OP_T_ROWS + 2
    keep = m.read(off, 2)
    m.write(off, b"\x00\x00")       # the part "is" sector 0: our own header
    calls0 = W("px_pcalls")
    m.key("F1")
    M.ui_done(m, "the refused card")
    M.pace(m, 1.0)
    check("E: a part that is not a part is REFUSED", B("px_helpon") == 0
          and W("px_pcalls") == calls0,
          "helpon %d, calls %d -> %d" % (B("px_helpon"), calls0, W("px_pcalls")))
    check("E: ...and its claim given back all the same", claims() == c0
          and not row()[1] & OP_FETCHED, "claims %s" % claims())
    m.write(off, keep)

    # --- F: the go-home bracket ---------------------------------------------
    PNGROW = base + syms["op_table"] + OP_T_ROWS + 8 * 2
    prow = lambda: m.read(PNGROW, 8)

    def revert(name):
        cl = dir_cluster(DISK, "P")
        m.write(base + syms["px_cur"], name.encode().ljust(13, b"\0"))
        m.write(base + syms["px_cur"] + 14, bytes((cl & 255, cl >> 8)))
        m.write(base + syms["px_cur"] + 13, bytes((B("px_homevol"),)))
        m.write(base + syms["px_cur"] + 27, b"\1")     # PXR_FHAVE: Revert's
        m.write(base + syms["px_lastref"], b"\0")      # one condition
        n0 = W("px_ndone")
        m.ctrl("KeyR")
        M.until(m, lambda _: W("px_ndone") != n0 and B("px_busy") == 0
                and B("px_job") == 0, "the open of " + name, poll=0.3,
                limit=600)
        M.ui_done(m, name)
        r = m.read(base + syms["px_cur"], 48)
        return r[:13].split(b"\0")[0].decode(), r[43]
    m.write(base + syms["px_pmoved"], b"\0")
    calls0 = W("px_pcalls")
    name, have = revert("P2_8.PNG")
    check("F: a PNG in P/ decoded (the stream read from P/)",
          name == "P2_8.PNG" and have == 1 and B("px_lastref") == 0,
          "record %s have %d refusal %d" % (name, have, B("px_lastref")))
    check("F: ...its part fetched from PIXEL.O88's folder: the bracket moved",
          B("px_pmoved") == 1, "[px_pmoved] = %d" % B("px_pmoved"))
    check("F: ...HEAD far-called once, DECODE on the worker",
          W("px_pcalls") == calls0 + 1, "%d -> %d" % (calls0, W("px_pcalls")))
    check("F: ...and the part KEPT after a picture of its kind",
          prow()[1] & OP_FETCHED and B("px_kheld") == 2,
          "flags %02X held %d" % (prow()[1], B("px_kheld")))
    name, have = revert("C8.PCX")
    SIMPROW = base + syms["op_table"] + OP_T_ROWS + 8 * 4
    check("F: a PCX after it opens, and the PNG part is DROPPED - for the "
          "SIMPLE part, which reads it (SPEC.md 106.20)",
          name == "C8.PCX" and have == 1 and not prow()[1] & OP_FETCHED
          and B("px_kheld") == 4 and m.read(SIMPROW, 8)[1] & OP_FETCHED,
          "record %s have %d flags %02X held %d" % (name, have, prow()[1],
                                                    B("px_kheld")))

print()
if FAIL:
    print("FAILED: " + ", ".join(FAIL)); sys.exit(1)
print("ok"); sys.exit(0)
