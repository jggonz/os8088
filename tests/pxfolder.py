#!/usr/bin/env python3
"""The folder: Prev, Next and the windowed slideshow (SPEC.md 106.21).

    python3 tests/pxfolder.py [--machine os8088_xt_vga_720b]

A folder of seven pictures in B:/PICS - a JPEG of the gallery, a JPEG, two
GIFs, two BMPs and a PCX of tools/pixcorpus.py's. PiXEL is opened on LAKE.JPG
by its ASSOCIATION, from a subfolder of B: (SPEC.md 106.6 / 54.10: the folder
list is the document's own folder), and then:

  BEHIND another window in front while the open finishes, and 20 s of
         guest time after it: no hidden decode begun, and no callback of
         PiXEL's over CEIL_BEHIND (SPEC.md 106.25, review-w5 F13: every
         callback runs on the ONE UI task, so one that holds it holds the
         desktop) - timed in guest cycles, entry to return
  FOLDER PiXEL in front: the folder list is the document's folder, sorted,
         LAKE.JPG open at its place in it
  SLIDES File > Slideshow: three slides go by, the button latched; a key
         stops it, and the picture shown is the last slide; no decode's
         claim is left behind
  BROWSE twenty Prev and Next (Space, Backspace) and then still: PiXEL's
         claims are as they were, the largest free run of the heap too

It is what survived of tests/pxthumb.py when the filmstrip, its thumbnails
and PIXEL.THC were withdrawn (2026-10-06): the legs about the folder, the
slideshow and the heap, which the strip never owned.
"""
import argparse
import functools
import os
import sys
import time

print = functools.partial(print, flush=True)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, "tests")
sys.path.insert(0, "tools")
import os88marty as M           # noqa: E402
import os88ui                   # noqa: E402
import heapmap                  # noqa: E402
import os88build                # noqa: E402
import pixcorpus as C           # noqa: E402
from pxsyms import pkg_syms, instance, u16      # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_xt_vga_720b")
a = ap.parse_args()
syms, image = pkg_syms()
o88 = open(os88build.at("build/pixel.o88"), "rb").read()
if o88[:syms["op_table"]] != image[:syms["op_table"]]:
    sys.exit("build/pixel.o88 is not this tree's pixel.asm - run make")

NREC = syms.get("PX_NREC", 18)  # a folder list record (pixel.asm)
FAIL = []
HZ = 4772727                    # the 5150's clock: guest seconds from cycles
BEHIND_S = 20                   # how long PiXEL waits behind another window
CEIL_BEHIND = 0.25              # its longest callback meanwhile (the
                                # free-memory field's look, ~0.13 s)


def check(what, ok, detail=""):
    print("%s %s%s" % ("PASS" if ok else "FAIL", what,
                       "" if ok or not detail else "  -- " + detail))
    if not ok:
        FAIL.append(what)


# --- the disk ------------------------------------------------------------------
tmp = "build/pxfolder"
os.makedirs(tmp, exist_ok=True)
cor = {n: d for n, d, v in C.corpus()}
PICS = {}
for n in ("BIG24.BMP", "GBIG.GIF", "C8.PCX", "BIG8.BMP", "G8.GIF"):
    PICS[n] = cor[n]
PICS["LAKE.JPG"] = open("apps/pixel/samples/LAKE.JPG", "rb").read()
PICS["JBIG.JPG"] = open("tests/pixel/JBIG.JPG", "rb").read()
files = []
for n, d in sorted(PICS.items()):
    open(os.path.join(tmp, n), "wb").write(d)
    files.append("PICS:" + os.path.join(tmp, n))
DISK = M.scratch_disk("build/pxfolder.img", "build/pixel.o88",
                      "build/PIXEL.GFX", *files, "--folder", "SYSTEM/APPDATA",
                      size=720)
NAMES = sorted(PICS)

print("== PiXEL: the folder, Prev, Next and the slideshow, %s (SPEC.md "
      "106.21) ==" % a.machine)
with os88ui.boot("build/os8088-360.img", apps=DISK, machine=a.machine) as ui:
    m = ui.m
    S = ui._S
    hsym = {n: S(n) for n in ("mem_base", "mem_top", "spl_live", "mem_tab")}

    def launch():
        ui.path("B:/PICS/LAKE.JPG")
        M.until(m, lambda _: instance(m, S, image), "PiXEL's instance",
                poll=0.3, limit=120)
        return instance(m, S, image) * 16

    base = launch()
    B = lambda n: m.read(base + syms[n], 1)[0]                 # noqa: E731
    W = lambda n: u16(m.read(base + syms[n], 2))              # noqa: E731

    def still():
        """Nothing in flight: no open, no hidden decode."""
        return (W("px_ndone") and B("px_busy") == 0 and B("px_job") == 0
                and B("px_hmode") == 0)

    def wait_still(what, limit=900):
        M.until(m, lambda _: still(), what, poll=0.5, limit=limit)
        M.ui_done(m, what)

    def names():
        nn = W("px_nnames")
        raw = m.read(base + syms["px_names"], nn * NREC)
        return [raw[i * NREC:i * NREC + 13].split(b"\0")[0].decode()
                for i in range(nn)]

    def parts_here():
        """The segments of the parts fetched now: a DECODER part is held
        while the picture shown is of its kind and dropped by an open of
        another (SPEC.md 106.18) - pxdecode's exclusion, and for its reason"""
        out = []
        for k in (1, 2, 3, 4, 8):
            r = m.read(base + syms["op_table"] + 10 + 8 * k, 8)
            if r[1] & 32:
                out.append(u16(r, 6))
        return out

    def claims():
        hm = heapmap.Map(m, hsym)
        seg = base >> 4
        here = parts_here()
        mine = sorted(c.para for c in hm.claims
                      if c.own == seg and c.seg != seg
                      and not any(c.seg <= p < c.seg + c.para for p in here))
        # the largest free run with the decoder parts taken as free, as the
        # claims are taken without them: a part is fetched into whatever
        # hole the picture it came after left, so where it lands is that
        # picture's size and not a leak (SPEC.md 106.24: a 1,216-paragraph
        # JPEG part landed 224 paragraphs higher after the browse, behind
        # the other neighbour's master, with every claim as it was)
        hm.claims = [c for c in hm.claims
                     if not any(c.seg <= p < c.seg + c.para for p in here)]
        return mine, max(p for _, p in hm.runs())

    def record():
        b = m.read(base + syms["px_cur"], 48)
        return (b[:13].split(b"\0")[0].decode(), u16(b, 32), u16(b, 34), b[36],
                u16(b, 38), b[43])

    def profile(stop, limit, what):
        """Every W_ONTIMER / W_ONWAKE / W_ONCLOSE of PiXEL's until stop()
        holds or `limit` guest seconds pass: [(name, seconds)], timed in the
        guest's own cycles from
        a breakpoint at the callback's entry to one at the address it returns
        to (review-w5 F13's measurement: every callback runs on the ONE UI
        task, the timer's under the gfx lock, so while one runs nothing else
        on the desktop is answered)"""
        ents = {(base + syms[r]) & 0xFFFFF: r
                for r in ("px_ontimer", "px_onwake", "px_onclose")}
        rets, opn, calls = {}, [], []

        def arm():
            m.breakpoints([{"type": "exec", "addr": x}
                           for x in list(ents) + list(rets)])
        m.pause()
        arm()
        c0 = int(m.status()["cycles"])
        m.run()
        seen = None
        while True:
            st = m.status()
            cyc = int(st["cycles"])
            if st.get("state") == "breakpoint":
                if st.get("stops") != seen:
                    seen = st.get("stops")
                    flat = ((st["cs"] << 4) + st["ip"]) & 0xFFFFF
                    if flat in ents:
                        r = m.regs()
                        ret = (r["cs"] * 16 + u16(m.read(r["ss"] * 16
                                                         + r["sp"], 2)))
                        ret &= 0xFFFFF
                        opn.append((cyc, ents[flat], ret))
                        if ret not in rets:
                            rets[ret] = 1
                            arm()
                    elif flat in rets:
                        for i in range(len(opn) - 1, -1, -1):
                            if opn[i][2] == flat:
                                t, nme, _ = opn.pop(i)
                                calls.append((nme, (cyc - t) / HZ))
                                break
                m.run()
                continue
            if not opn and (stop() or (cyc - c0) / HZ > limit):
                break
            time.sleep(0.02)
        m.breakpoints([])
        m.run()
        long = max(calls, key=lambda c: c[1]) if calls else ("-", 0)
        print("   %s: %d callbacks, %.2f s of the UI task in %.1f s; the "
              "longest %s, %.3f s" % (what, len(calls),
                                       sum(c[1] for c in calls),
                                       (cyc - c0) / HZ, long[0], long[1]))
        for nme, d in calls:
            if d > 0.25:
                print("     %-10s %.3f s" % (nme, d))
        return calls, long[1]

    # -------------------------------------------------------------- BEHIND --
    # another window in front while the open is still decoding: no callback
    # of PiXEL's holds the desktop
    other = [w for w in ui.windows() if not w.title.startswith("PiXEL")]
    ui.raise_window(other[-1])
    M.until(m, lambda _: W("px_ndone") and B("px_busy") == 0
            and B("px_job") == 0, "the open, behind", poll=0.5, limit=900)
    calls, longb = profile(lambda: False, BEHIND_S, "BEHIND")
    check("BEHIND: %.0f s with another window in front, no hidden decode "
          "begun" % BEHIND_S, B("px_hmode") == 0)
    check("BEHIND: the longest callback %.3f s, under %.2f s"
          % (longb, CEIL_BEHIND), longb < CEIL_BEHIND)
    ui.raise_window(ui.window("PiXEL"))

    # -------------------------------------------------------------- FOLDER --
    wait_still("the open, in front")
    nm = names()
    shown = record()
    check("the folder list is the document's folder: %d pictures" % len(nm),
          nm == NAMES, "%r" % nm)
    check("LAKE.JPG open, n of N says %d of %d" % (W("px_fidx"), len(nm)),
          shown[0] == "LAKE.JPG" and shown[5] == 1
          and W("px_fidx") == NAMES.index("LAKE.JPG") + 1)
    c1 = claims()

    # -------------------------------------------------------------- SLIDES --
    n0 = W("px_ndone")
    ui.menu_pick("File", "Slideshow")
    M.until(m, lambda _: B("px_slon") == 1, "the slideshow", poll=0.3,
            limit=30)
    flags = u16(m.read(base + syms["px_bflags"] + 2 * 9, 2))
    check("SLIDES: the button latched", flags & 32 != 0, "flags %04X" % flags)
    M.until(m, lambda _: W("px_ndone") >= n0 + 3, "three slides", poll=0.5,
            limit=300)
    # the picture SHOWN: px_prev's while the next one decodes hidden
    rec = "px_prev" if B("px_hmode") else "px_cur"
    was = m.read(base + syms[rec], 13).split(b"\0")[0].decode()
    m.key("Escape")
    M.until(m, lambda _: B("px_slon") == 0, "the slideshow to stop",
            poll=0.3, limit=60)
    wait_still("still, after the slideshow")
    r2 = record()
    check("SLIDES: three slides, then Esc stopped it on %s, still shown"
          % r2[0], r2[0] != "LAKE.JPG" and r2[5] == 1 and r2[0] == was,
          "%s then %r" % (was, r2))
    c2 = claims()
    check("SLIDES: no claim of a decode left behind (%d, as before)"
          % len(c2[0]), len(c2[0]) == len(c1[0]),
          "%r -> %r" % (c1[0], c2[0]))

    # -------------------------------------------------------------- BROWSE --
    while record()[0] != "LAKE.JPG":
        n0 = W("px_ndone")
        m.key("Space")
        M.until(m, lambda _: W("px_ndone") != n0 and B("px_busy") == 0,
                "Next", poll=0.3, limit=300)
    wait_still("still, on LAKE.JPG again")
    base0 = claims()
    for k in ["Space"] * 10 + ["Backspace"] * 10:
        n0 = W("px_ndone")
        m.key(k)
        M.until(m, lambda _: W("px_ndone") != n0 and B("px_busy") == 0,
                k, poll=0.3, limit=300)
    wait_still("still, after twenty")
    after = claims()
    check("BROWSE: back on LAKE.JPG after ten Next and ten Prev",
          record()[0] == "LAKE.JPG", "%r" % (record(),))
    check("BROWSE: PiXEL's claims as they were (%d)" % len(after[0]),
          after[0] == base0[0], "%r -> %r" % (base0[0], after[0]))
    check("BROWSE: the largest free run as it was (%d paragraphs)"
          % after[1], after[1] >= base0[1], "%d -> %d"
          % (base0[1], after[1]))

print("%s pxfolder" % ("FAIL" if FAIL else "PASS"))
sys.exit(1 if FAIL else 0)
