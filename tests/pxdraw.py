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
an open (File > Revert, the decode included). Keys and menu picks, so a
press's own button flash is not in the count.

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
            paint from nothing does
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
         "frame": 0x40, "gray": 0x48, "xor": 0x50, "blit4": 0x182,
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
        "Show Panels": 180, "Hide Filmstrip": 200, "Show Filmstrip": 240,
        "card up (F1)": 15, "status field (timer)": 2,
        "open (Revert)": 200}


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
    M.scratch_disk(disk, o88path, "build/PIXEL.GFX",
                   "apps/pixel/samples/CITY.PCX")
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
        M.until(m, lambda _: W("px_ndone") and idle(), "the decode",
                poll=0.3, limit=900)
        ui.settle()
        ui.mo.to(2, 2)
        ui.settle()
        addrs = {KSEG * 16 + off: n for n, off in CELLS.items()}

        def measure(what, act, wait=None):
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
        M.guest_sleep(m, 1.0)

        def content():
            w, h, px = m.fbuf()
            x0, y0 = W("px_cx0"), W("px_cy0")
            cw, ch = W("px_w"), W("px_h")
            out = bytearray()
            for y in range(y0, y0 + ch):
                out += px[(y * w + x0) * 3:(y * w + x0 + cw) * 3]
            return cw, ch, bytes(out)
        ca = content()
        key("F1")
        M.ui_done(m, "the card")
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
            ok = False
        else:
            print("PASS identity: %dx%d content, the incremental picture IS "
                  "the repaint's" % (cw, ch))
    print("gestures: %d drawing calls" % sum(r["calls"] for r in rows))
    if a.json:
        json.dump(rows, open(a.json, "w"), indent=1)
    print("%s pxdraw" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
