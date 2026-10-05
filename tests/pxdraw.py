#!/usr/bin/env python3
"""What PiXEL's window COSTS TO DRAW, a gesture at a time (SPEC.md 106.15).

    python3 tests/pxdraw.py [--machine os8088_xt_vga] [--record] [--json OUT]

A redraw is priced by the primitive calls it makes (PERFORMANCE.md's first
sentence), so this counts them: every far call the package makes into a
DRAWING cell of the API table - fill, frame, the lines, grey and XOR fill, a
text run, an icon, the three blits, a scroll, a save and a restore - armed on
the cell itself, so the kernel's own painting (a menu, the title bar) is never
counted. Each call is named by the package routine that made it (the far
return address against this tree's own map), which is how a regression is
found rather than only detected. tests/mrdraw.py is the shape this copies.

The gestures are the ones a user makes with a picture open (CITY.PCX, a
320x240 PCX): a tool by its letter and back, a zoom step in and out, a pan,
Fit, the Histogram's channel through its drop-down, Hide Panels and Show
Panels, Hide Filmstrip and Show Filmstrip, the keyboard card up and down, and
an open (File > Revert, the decode included); and EDITING's (106.24): the
Marquee by its letter, a drag, a nudge, Esc, Invert and its Undo, a card up
and down, the Eyedropper's readout following the pointer, Select All. Keys
and menu picks, so a press's own button flash is not in the count.

TWO ASSERTIONS, on Hercules (the default, the mono face) AND on the VGA (the
colour face, --machine os8088_xt_vga):

  ceilings  each gesture's calls under a number a FULL repaint would blow
            straight through: SPEC.md 106.15 promises a command draws what
            it changed, and a caller that goes back to repainting regions
            fails here by name
  identity  after the run of gestures the content is captured, a FULL
            repaint is forced (the keyboard card up and down) and captured
            again, and the two must be IDENTICAL to the pixel: the caches
            are only sound if drawing through them arrives at the picture a
            paint from nothing does. The run ends with Select All, so the
            marquee's XOR outline is in both pictures (SPEC.md 106.24)
"""
import argparse
import functools
import json
import os
import sys

print = functools.partial(print, flush=True)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, "tests")
sys.path.insert(0, "tools")
import os88marty as M          # noqa: E402
import os88ui                  # noqa: E402
import os88build               # noqa: E402
from pxsyms import pkg_syms, instance, u16      # noqa: E402

CELLS = {"pixel": 0x20, "hline": 0x28, "vline": 0x30, "fill": 0x38,
         "frame": 0x40, "gray": 0x48, "xor": 0x50, "xorfill": 0x58,
         "blit4": 0x182,
         "scroll": 0x19C, "text": 0x1E5, "blit1": 0x320, "icon": 0x39D,
         "blitp": 0x3A4, "save": 0x3DC, "rest": 0x3E4}
KSEG = 0x60
# THE CEILINGS (SPEC.md 106.15): the most each gesture may draw, on either
# face. Each is what it draws through the records with room for the
# composer's bands to move (the canvas is the renderer's), and a fraction of
# a repaint of the regions it touches - a full repaint is ~300 on either
# face, the toolbar alone ~60 and the panel column ~100. The card coming
# DOWN is the one gesture that repaints, so it has none: it is the yardstick
CEIL = {"tool: Zoom (z)": 16, "tool: Hand (h)": 16, "zoom in (=)": 40,
        "pan down (arrow)": 12, "pan across (arrow)": 90,
        "zoom out (-)": 40, "Fit (0)": 40, "Hide Panels": 50,
        "Show Panels": 180, "Hide Filmstrip": 200,
        # (since 106.21 the folder holds seven pictures, so the strip's
        # cards are seven named cards with their thumbnails where wave 4's
        # were one named card and blank ones: ~14 calls a card either way)
        "Show Filmstrip": 280,
        "card up (F1)": 15, "status field (timer)": 2,
        "open (Revert)": 200,
        # THE FOLDER (SPEC.md 106.21): Next and Prev are an open each, the
        # strip's two cards whose highlight moved and nothing else of it;
        # a page is the strip's cards that changed and its two pagers
        "Next (Space)": 230, "Prev (Backspace)": 230,
        "strip page (>)": 130, "strip page (<)": 130,
        # EDITING (SPEC.md 106.24): a tool is two buttons; the marquee is
        # four XOR fills a step of the drag off and four on; a nudge, Esc
        # and Select All are the outline and nothing else; a palette
        # operation and its Undo are the canvas's composer bands and the
        # Navigator's well, never the chrome; a card is itself
        "tool: Marquee (m)": 16, "marquee drag": 140,
        "nudge (arrow)": 12, "deselect (Esc)": 10, "Select All": 10,
        "Invert (palette)": 200, "Undo Invert": 200,
        "card up (Gamma)": 60, "eyedropper (tick)": 12}
# A GIF THAT PLAYS (SPEC.md 106.25): the most one frame may draw - its rect
# through the canvas's composer, a band or two, and nothing of the chrome
CEIL_ANIM = 12
ANIM_N = 6
# the folder beside CITY.PCX (tools/pixcorpus.py's): seven pictures, more
# than a strip shows, so it pages
FOLDER = ("B24.BMP", "C8.PCX", "G8.GIF", "N6.PPM", "P0_8.PNG", "T2_24.TGA")


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


def pkg_syms_at(src):
    """pkg_syms for a copy of apps/pixel/ somewhere else (--ab)."""
    import subprocess
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(os.path.join(src, "pixel.asm")).read()
                            + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", src.rstrip("/") + "/", "-o",
                        os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for L in open(mp):
            f = L.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[1], 16)
        return out, open(os.path.join(d, "p.bin"), "rb").read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--record", action="store_true")
    ap.add_argument("--json")
    ap.add_argument("--ab", nargs=2, metavar=("SRCDIR", "O88"),
                    help="measure ANOTHER build - its apps/pixel/ copy and "
                    "its PIXEL.O88 - for a before-and-after table")
    a = ap.parse_args()
    if a.ab:
        syms, image = pkg_syms_at(a.ab[0])
        o88path = a.ab[1]
    else:
        syms, image = pkg_syms()
        o88path = os88build.at("build/pixel.o88")
    o88 = open(o88path, "rb").read()
    if o88[:syms["op_table"]] != image[:syms["op_table"]]:
        sys.exit("%s is not that tree's pixel.asm - run "
                 "`make build/pixel.o88`" % o88path)
    code = sorted(((k, v) for k, v in syms.items()
                   if "." not in k and v < syms["op_table"]),
                  key=lambda kv: kv[1])

    def nearest(ip):
        best = "?"
        for name, off in code:
            if off <= ip:
                best = name
            else:
                break
        return best

    def caller(m, rec):
        """The far return address on top of the stack at the cell: (IP, CS).
        A CS that is not PiXEL's is the kernel calling its own cell - the
        planar blit's clip walk far-calls the cell once a fragment, from
        .cold (SPEC.md 5.4.3.6) - and is not one of the package's calls."""
        r = rec.get("regs") or {}
        ss, sp = r.get("ss", 0), r.get("sp", 0)
        b = bytes(m.read(ss * 16 + sp, 4))
        return (b[0] | b[1] << 8, b[2] | b[3] << 8)

    disk = "build/pxdraw.img"
    if a.ab:                            # (os88disk names the file by its
        os.makedirs("build/pxdraw-ab", exist_ok=True)       # own name)
        o88path = "build/pxdraw-ab/PIXEL.O88"
        open(o88path, "wb").write(o88)
    sys.path.insert(0, "tools")
    import pixcorpus as C
    cor = {n: d for n, d, v in C.corpus()}
    os.makedirs("build/pxdrawf", exist_ok=True)
    extra = []
    for n in FOLDER:
        open(os.path.join("build/pxdrawf", n), "wb").write(cor[n])
        extra.append(os.path.join("build/pxdrawf", n))
    M.scratch_disk(disk, o88path, "build/PIXEL.GFX",
                   "apps/pixel/samples/CITY.PCX", *extra,
                   "ANIM:apps/pixel/samples/BOUNCE.GIF")
    rows, ok = [], True
    print("== PiXEL: drawing calls a gesture, %s (SPEC.md 106.15) =="
          % a.machine)
    with os88ui.boot("build/os8088-360.img", apps=disk,
                     machine=a.machine) as ui:
        m = ui.m
        S = ui._S
        ui.path("B:/CITY.PCX")
        M.until(m, lambda _: instance(m, S, image), "PiXEL's instance",
                poll=0.3, limit=120)
        base = instance(m, S, image) * 16
        B = lambda n: m.read(base + syms[n], 1)[0]
        W = lambda n: u16(m.read(base + syms[n], 2))

        def idle():
            return B("px_busy") == 0 and B("px_job") == 0

        def still():
            """...and the thumbnails done: no hidden decode, the timer at
            its slow pace (SPEC.md 106.21) - a gesture is measured from
            still, or a card arriving would be counted as its"""
            return idle() and B("px_hmode") == 0 and B("px_tq") == 0
        M.until(m, lambda _: W("px_ndone") and still(), "the decode",
                poll=0.3, limit=900)
        ui.settle()
        ui.mo.to(2, 2)
        ui.settle()
        addrs = {KSEG * 16 + off: n for n, off in CELLS.items()}

        def measure(what, act, wait=None):
            M.until(m, lambda _: still(), "still, before " + what,
                    poll=0.3, limit=900)
            M.ui_done(m, "still")
            with M.bp_trace(m, *addrs.keys(), cap=50000, regs=True,
                            on_hit=caller) as tr:
                act()
                if wait:
                    M.until(m, lambda _: wait(), what, poll=0.3, limit=900)
                M.ui_done(m, what)
                M.guest_sleep(m, 0.3)
            by, fn, n_ = {}, {}, 0
            if tr.overflowed:
                sys.exit("pxdraw: %s overflowed the trace" % what)
            for h in tr.hits:
                ip, cs = h.get("hit") or (0, 0)
                if cs != base >> 4:
                    continue                    # the kernel's own re-entry
                n_ += 1
                n = addrs.get(h["addr"], "?")
                by[n] = by.get(n, 0) + 1
                f = nearest(ip)
                fn[f] = fn.get(f, 0) + 1
            row = {"gesture": what, "calls": n_, "by": by, "callers": fn}
            print("%-26s %4d  %s" % (what, n_, " ".join(
                "%s %d" % kv for kv in sorted(by.items()))))
            if fn:
                print("      " + ", ".join("%s %d" % kv for kv in sorted(
                    fn.items(), key=lambda kv: -kv[1])[:8]))
            rows.append(row)
            return row

        key = m.key
        measure("tool: Zoom (z)", lambda: key("KeyZ"))
        measure("tool: Hand (h)", lambda: key("KeyH"))
        measure("zoom in (=)", lambda: key("Equal"))
        measure("pan down (arrow)", lambda: key("ArrowDown"))
        measure("pan across (arrow)", lambda: key("ArrowRight"))
        measure("zoom out (-)", lambda: key("Minus"))
        measure("Fit (0)", lambda: key("Digit0"))
        measure("Hide Panels", lambda: ui.menu_pick("View", "Hide Panels"))
        measure("Show Panels", lambda: ui.menu_pick("View", "Show Panels"))
        measure("Hide Filmstrip",
                lambda: ui.menu_pick("View", "Hide Filmstrip"))
        measure("Show Filmstrip",
                lambda: ui.menu_pick("View", "Show Filmstrip"))
        measure("card up (F1)", lambda: key("F1"))
        measure("card down (Esc)", lambda: key("Escape"))
        # the free memory "changed" (its value made stale, so the next
        # five-second look marks it) and its RECORD told the glass shows an
        # x in its sixth cell: the look draws that one cell and no other
        mem = syms["px_sv"] + 6 * 16
        m.write(base + mem, b"x\0")
        if "px_slots" in syms:          # (a build from before the records)
            slot = syms["px_slots"] + 6 * syms.get("PX_SLOTSZ", 32) + 8 + 5
            m.write(base + slot, b"x")
        measure("status field (timer)", lambda: None,
                lambda: m.read(base + mem, 1) != b"x")
        n0 = W("px_ndone")

        def revert():
            m.write(base + syms["px_cur"], b"CITY.PCX".ljust(13, b"\0"))
            m.ctrl("KeyR")
        measure("open (Revert)", revert,
                lambda: W("px_ndone") != n0 and idle())
        n0 = W("px_ndone")
        measure("Next (Space)", lambda: key("Space"),
                lambda: W("px_ndone") != n0 and idle())
        n0 = W("px_ndone")
        measure("Prev (Backspace)", lambda: key("Backspace"),
                lambda: W("px_ndone") != n0 and idle())

        def pager(i):
            r = m.read(base + syms["px_brects"] + 8 * i, 8)
            x1, y1, x2, y2 = (u16(r, 0), u16(r, 2), u16(r, 4), u16(r, 6))
            ui.mo.click((x1 + x2) // 2, (y1 + y2) // 2)
        fcs = W("px_fcs")
        measure("strip page (>)", lambda: pager(20),
                lambda: W("px_fcs") != fcs)
        fcs = W("px_fcs")
        measure("strip page (<)", lambda: pager(19),
                lambda: W("px_fcs") != fcs)

        # EDITING (SPEC.md 106.24)
        measure("tool: Marquee (m)", lambda: key("KeyM"))

        def org():
            """The picture's top-left on the glass (the view's ix, iy)."""
            b = bytes(m.read(base + syms["px_vcan"], 4))
            return tuple(v - 65536 if v >= 32768 else v
                         for v in (u16(b, 0), u16(b, 2)))

        def drag():
            ix, iy = org()
            x0, y0 = ix + 30, iy + 20
            ui.mo.to(x0, y0)
            ui.mo._edge(True)
            ui.mo.to(x0 + 60, y0 + 40, l=True)
            M.guest_sleep(m, 0.3)
            ui.mo._edge(False)
        measure("marquee drag", drag, lambda: B("px_sel") == 1)
        measure("nudge (arrow)", lambda: key("ArrowRight"))
        measure("deselect (Esc)", lambda: key("Escape"),
                lambda: B("px_sel") == 0)
        ui.mo.to(2, 2)
        measure("Invert (palette)",         # (the plans: the worker's,
                lambda: ui.menu_pick("Image", "Invert"),    # then drawn)
                lambda: B("px_dirty") == 1 and idle())
        measure("Undo Invert",
                lambda: ui.menu_pick("Edit", "Undo Invert"),
                lambda: B("px_dirty") == 0 and idle())
        measure("card up (Gamma)",
                lambda: ui.menu_pick("Effects", "Gamma..."),
                lambda: B("px_pcon") != 0)
        measure("card down (Esc, a repaint)", lambda: key("Escape"),
                lambda: B("px_pcon") == 0)
        key("KeyE")
        M.ui_done(m, "Eyedropper")
        ix, iy = org()
        ui.mo.to(ix + 40, iy + 30)
        M.guest_sleep(m, 0.5)
        measure("eyedropper (tick)", lambda: ui.mo.to(ix + 48, iy + 34),
                lambda: B("px_eyeon") == 1)
        key("KeyH")
        M.ui_done(m, "Hand")
        ui.mo.to(2, 2)
        # (the MENU, not Ctrl+A: a modifier's release sent while the trace
        # holds the guest at a stop can be lost, and a Ctrl left down turns
        # the identity's F1 into Ctrl+F1, scan 5Eh, which nothing answers)
        measure("Select All", lambda: ui.menu_pick("Edit", "Select All"),
                lambda: B("px_sel") == 1)

        for r in rows:
            c = CEIL.get(r["gesture"])
            if not a.record and c is not None and r["calls"] > c:
                print("FAIL %s: %d drawing calls, ceiling %d"
                      % (r["gesture"], r["calls"], c))
                ok = False

        # THE IDENTITY: a run of gestures, then the picture they left
        # against the picture a repaint from nothing draws
        for k in ("KeyM", "Equal", "ArrowDown", "ArrowRight", "KeyH",
                  "Minus"):
            key(k)
            M.ui_done(m, k)
        ui.menu_pick("View", "Hide Filmstrip")
        ui.menu_pick("View", "Show Filmstrip")
        ui.mo.to(2, 2)
        ui.settle()
        M.until(m, lambda _: still(), "still, before the identity",
                poll=0.3, limit=900)
        # (and the free-memory field's look past: it is a five-second look,
        # PX_MEMT, and the store Hide Filmstrip gave back and Show Filmstrip
        # claimed again moves it 31K - the repaint draws it as it is now)
        M.guest_sleep(m, 6.0)

        def content():
            w, h, px = m.fbuf()
            x0, y0 = W("px_cx0"), W("px_cy0")
            cw, ch = W("px_w"), W("px_h")
            out = bytearray()
            for y in range(y0, y0 + ch):
                out += px[(y * w + x0) * 3:(y * w + x0 + cw) * 3]
            return cw, ch, bytes(out)
        def mq():
            return "sel %d on %d hold %d mqr %r" % (
                B("px_sel"), B("px_mqon"), B("px_mqhold"),
                [W("px_mqr") if k == 0 else u16(m.read(
                    base + syms["px_mqr"] + 2 * k, 2)) for k in range(4)])
        ca = content()
        mqa = mq()
        key("F1")
        try:
            M.until(m, lambda _: B("px_helpon") == 1, "the key card up",
                    poll=0.3, limit=60)     # (an Esc before it would
        except Exception:
            print("the key card did not come up: the toast %r" % (
                ui.toast(),))
            raise
        M.ui_done(m, "the card")            # drop the selection instead)
        M.guest_sleep(m, 1.0)
        key("Escape")
        M.ui_done(m, "the repaint")
        ui.settle()
        M.guest_sleep(m, 1.0)
        cb = content()
        cw, ch, ap_ = ca
        bad = [(i // 3 % cw, i // 3 // cw) for i in range(0, len(ap_), 3)
               if ap_[i:i + 3] != cb[2][i:i + 3]]
        if bad:
            for tag, c in (("a", ap_), ("b", cb[2])):
                M.write_png_rgb("build/pxdraw-%s.png" % tag, cw, ch, c)
            xs = [x for x, _ in bad]
            ys = [y for _, y in bad]
            print("FAIL identity: %d pixels differ between the incremental "
                  "picture and a repaint, in (%d,%d)-(%d,%d) of the content"
                  % (len(bad), min(xs), min(ys), max(xs), max(ys)))
            print("     the marquee: %s, then %s" % (mqa, mq()))
            ok = False
        else:
            print("PASS identity: %dx%d content, the incremental picture IS "
                  "the repaint's" % (cw, ch))

        # A GIF THAT PLAYS (SPEC.md 106.25): the gallery's BOUNCE.GIF, in a
        # folder of its own (the record's folder poked, pxdecode's way), and
        # what its frames draw - the rect each changed, never the window
        m.write(base + syms["px_thoff"], b"\1")    # (no thumbnail arriving
        cl = dir_cluster(disk, "ANIM")              # between the two shots)
        m.write(base + syms["px_cur"] + 14, bytes((cl & 255, cl >> 8)))
        m.write(base + syms["px_cur"], b"BOUNCE.GIF".ljust(13, b"\0"))
        n0 = W("px_ndone")
        m.ctrl("KeyR")
        M.until(m, lambda _: W("px_ndone") != n0 and B("px_anon") == 1
                and W("px_anfr") >= 3, "BOUNCE.GIF playing", poll=0.3,
                limit=900)
        f0 = W("px_anfr")
        with M.bp_trace(m, *addrs.keys(), cap=50000, regs=True,
                        on_hit=caller) as tr:
            M.until(m, lambda _: (W("px_anfr") - f0) % 65536 >= ANIM_N,
                    "%d frames" % ANIM_N, poll=0.1, limit=600)
        n_ = sum(1 for h in tr.hits
                 if (h.get("hit") or (0, 0))[1] == base >> 4)
        fn = {}
        for h in tr.hits:
            ip, cs = h.get("hit") or (0, 0)
            if cs == base >> 4:
                f = nearest(ip)
                fn[f] = fn.get(f, 0) + 1
        per = n_ / float(ANIM_N)
        print("%-26s %4d  over %d frames, %.1f a frame" % (
            "animation (BOUNCE.GIF)", n_, ANIM_N, per))
        print("      " + ", ".join("%s %d" % kv for kv in sorted(
            fn.items(), key=lambda kv: -kv[1])[:8]))
        rows.append({"gesture": "animation frame", "calls": per,
                     "callers": fn})
        if not a.record and per > CEIL_ANIM:
            print("FAIL animation frame: %.1f drawing calls, ceiling %d"
                  % (per, CEIL_ANIM))
            ok = False
        # ...and what they drew is the picture: stopped (A), the glass
        # against a repaint from nothing, as the identity above - a frame
        # whose rect was spent on the pixels before its rows were in shows
        # here as the ball's old places left on the sand
        m.key("KeyA")
        M.until(m, lambda _: B("px_anon") == 0 and B("px_anjob") == 0,
                "the animation stopped", poll=0.2, limit=60)
        M.ui_done(m, "A")
        M.guest_sleep(m, 6.0)           # (the free-memory field's look)
        ga = content()
        key("F1")
        M.until(m, lambda _: B("px_helpon") == 1, "the key card up",
                poll=0.3, limit=60)
        M.ui_done(m, "the card")
        M.guest_sleep(m, 1.0)
        key("Escape")
        M.ui_done(m, "the repaint")
        M.guest_sleep(m, 1.0)
        gb = content()
        bad = sum(1 for i in range(0, len(ga[2]), 3)
                  if ga[2][i:i + 3] != gb[2][i:i + 3])
        if bad:
            M.write_png_rgb("build/pxdraw-anim-a.png", ga[0], ga[1], ga[2])
            M.write_png_rgb("build/pxdraw-anim-b.png", gb[0], gb[1], gb[2])
            print("FAIL animation identity: %d pixels of the stopped frame "
                  "differ from a repaint" % bad)
            ok = False
        else:
            print("PASS animation identity: the frames drawn ARE the "
                  "repaint's")
        # ...and A RELEASE THAT ACTS while it plays (review-w8 A2): the
        # Rotate tool's click is the release path, and a tick restarts the
        # frame job between the press and it - the release stops the job
        # first now, so the turn is made rather than refused (it read
        # "Rotate CW needs 9K; 241K") or the job wedged beside it
        m.key("KeyA")
        M.until(m, lambda _: B("px_anon") == 1 and B("px_anjob") == 1,
                "playing again", poll=0.1, limit=120)
        key("KeyR")
        M.ui_done(m, "the Rotate tool")
        M.until(m, lambda _: B("px_anjob") == 1, "a frame job running",
                poll=0.05, limit=120)
        v = bytes(m.read(base + syms["px_vcan"], 16))
        ix, iy = [x - 65536 if x >= 32768 else x for x in (u16(v, 0),
                                                         u16(v, 2))]
        ui.mo.click(ix + u16(v, 12) // 2, iy + u16(v, 14) // 2)
        try:
            M.until(m, lambda _: B("px_dirty") == 1 and idle(),
                    "the turn made", poll=0.2, limit=300)
            good = B("px_anon") == 0 and B("px_anjob") == 0
        except Exception:
            good = False
        print("%s A2: the Rotate tool's click while a GIF plays turns it "
              "(dirty %d busy %d job %d anon %d anjob %d, the toast %r)" % (
                  "PASS" if good else "FAIL", B("px_dirty"), B("px_busy"),
                  B("px_job"), B("px_anon"), B("px_anjob"), ui.toast()))
        ok = ok and good
        # ...and that edited ANIMATED GIF is not saved over itself as one
        # picture (review-w8 N1): File > Open asks "Save changes?" (Revert
        # discards without asking, by design), and its Save (Enter) goes to
        # Save As's card, the file untouched
        if good:
            m.ctrl("KeyO")
            M.until(m, lambda _: u16(m.read(base + syms["os88ui_awin"], 2)),
                    "Save changes?", poll=0.2, limit=60)
            M.ui_done(m, "asked")
            m.key("Enter")
            M.ui_done(m, "Save")
            n1 = B("px_pcon") == 6 and B("px_dirty") == 1
            print("%s N1: Save over the edited animated GIF is Save As "
                  "(card %d, dirty %d, the toast %r)" % (
                      "PASS" if n1 else "FAIL", B("px_pcon"), B("px_dirty"),
                      ui.toast()))
            ok = ok and n1
            key("Escape")
    print("gestures: %d drawing calls" % sum(r["calls"] for r in rows))
    if a.json:
        json.dump(rows, open(a.json, "w"), indent=1)
    print("%s pxdraw" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
