#!/usr/bin/env python3
"""What WireFleet's window COSTS TO DRAW, a gesture at a time (SPEC.md 114.10).

    python3 tests/wfdraw.py [--herc] [--json OUT]

tests/mrdraw.py's instrument on WireFleet: every far call the package makes
into a DRAWING cell of the API table, armed on the cell itself so the
kernel's own painting is never counted, named by the package routine that
made it. One PC, no cable: the automatic link (114.9) leases the free COM2
and searches, which is exactly the state a captain deploys in.

The default machine is MartyPC's 4.77 MHz XT with a VGA - the colour face;
--herc is the 5150 with Hercules - 114.2's mono face, which this delivery
did not mean to change.

THREE ASSERTIONS:

  idle      five seconds of searching draws NOTHING: the search moves bytes,
            never pixels (114.2: no animation needs an idle repaint)
  ceilings  each gesture's calls under a number a full repaint blows through
  identity  after a run of gestures the content is captured, a FULL repaint
            forced (About up and down) and captured again: identical, or the
            caches drew a picture a paint from nothing would not
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.chdir(ROOT)
import os88marty as M          # noqa: E402
import os88ui                  # noqa: E402
import mrprobe                 # noqa: E402
import dispapps                # noqa: E402

VGA = ("build/os8088-360.img", "build/wireflet360.img", "os8088_xt_vga")
HERC = ("build/os8088-360.img", "build/wireflet360.img",
        M.machine("os8088_5150_herc"))
CEIL = {"idle, 5 s searching": 0, "Right: cursor": 12, "2: select": 16,
        "R: rotate": 12, "Enter: place": 30, "A: auto-place": 60,
        "Y: ready": 400, "Right: enemy cursor": 12}
CELLS = {"pixel": 0x20, "hline": 0x28, "vline": 0x30, "fill": 0x38,
         "frame": 0x40, "gray": 0x48, "xor": 0x58, "blit4": 0x182,
         "text": 0x1E5, "icon": 0x39D, "blitp": 0x3A4, "blit1": 0x320}
SYMS = []


def caller(m, rec):
    r = rec.get("regs") or {}
    ss, sp = r.get("ss", 0), r.get("sp", 0)
    return int.from_bytes(bytes(m.read(ss * 16 + sp, 2)), "little")


def nearest(ip):
    best = None
    for name, off in SYMS:
        if off <= ip:
            best = name
        else:
            break
    return best or "?"


def measure(ui, what, act, settle):
    addrs = {M.KERNEL_SEG * 16 + off: n for n, off in CELLS.items()}
    with M.bp_trace(ui.m, *addrs.keys(), cap=50000, regs=True,
                    on_hit=caller) as tr:
        act()
        M.guest_sleep(ui.m, settle)
    by, fn = {}, {}
    for h in tr.hits:
        n = addrs.get(h["addr"], "?")
        by[n] = by.get(n, 0) + 1
        f = nearest(h.get("hit") or 0)
        fn[f] = fn.get(f, 0) + 1
    print("%-22s %4d  %s" % (what, tr.n, " ".join(
        "%s %d" % kv for kv in sorted(by.items()))), flush=True)
    if fn:
        print("      " + ", ".join("%s %d" % kv for kv in sorted(
            fn.items(), key=lambda kv: -kv[1])[:8]), flush=True)
    return {"gesture": what, "calls": tr.n, "by": by, "callers": fn}


def content(ui, p):
    w, h, px = ui.m.fbuf()
    x0, y0, cw, ch = p.w("wf_ox"), p.w("wf_oy"), p.w("wf_w"), p.w("wf_h")
    out = bytearray()
    for y in range(y0, y0 + ch):
        out += px[(y * w + x0) * 3:(y * w + x0 + cw) * 3]
    return cw, ch, bytes(out)


def identity(ui, p, key):
    M.guest_sleep(ui.m, 1.0)
    cw, ch, a = content(ui, p)
    measure(ui, "H: About up", lambda: key("KeyH"), 1.0)
    measure(ui, "Esc: About down", lambda: key("Escape"), 2.0)
    b = content(ui, p)[2]
    bad = [(i // 3 % cw, i // 3 // cw) for i in range(0, len(a), 3)
           if a[i:i + 3] != b[i:i + 3]]
    if bad:
        try:
            from PIL import Image
            for tag, px in (("a", a), ("b", b)):
                Image.frombytes("RGB", (cw, ch), px).save(
                    "build/wfdraw-identity-%s.png" % tag)
        except ImportError:
            pass
        xs, ys = [x for x, _ in bad], [y for _, y in bad]
        print("FAIL identity: %d pixels differ, in (%d,%d)-(%d,%d) of the "
              "content" % (len(bad), min(xs), min(ys), max(xs), max(ys)),
              flush=True)
        return False
    print("PASS identity: %dx%d content, the incremental picture IS the "
          "repaint's" % (cw, ch), flush=True)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    ap.add_argument("--herc", action="store_true")
    a = ap.parse_args()
    img, apps, machine = HERC if a.herc else VGA
    rows, ok = [], True
    with os88ui.boot(img, apps=apps, machine=machine) as ui:
        ui.path("B:/WIREFLET.O88")
        ui.settle()
        # the window's segment is current: the lease pins the region
        p = mrprobe.Probe(ui.m, mrprobe.seg_of(ui.m, "WireFleet"),
                          dispapps._map("wirefleet"))
        want = 0 if a.herc else 1
        if p.b("wf_colour") != want:
            print("FAIL face: wf_colour %d, wanted %d" % (p.b("wf_colour"),
                                                        want), flush=True)
            ok = False
        M.until(ui.m, lambda _: p.b("wf_linkopen") == 1, "the search's lease",
                poll=.2, limit=20)
        p.seg = mrprobe.seg_of(ui.m, "WireFleet")   # pinned from here on
        global SYMS
        SYMS = sorted(((k, v) for k, v in p.s.items()
                       if "." not in k and v < 0xA000), key=lambda kv: kv[1])
        key = ui.m.key
        M.guest_sleep(ui.m, 1.0)
        rows.append(measure(ui, "idle, 5 s searching", lambda: None, 5.0))
        for what, k, t in (("Right: cursor", "ArrowRight", 0.8),
                           ("2: select", "Digit2", 0.8),
                           ("R: rotate", "KeyR", 0.8),
                           ("Enter: place", "Enter", 1.0),
                           ("A: auto-place", "KeyA", 1.5),
                           ("Y: ready", "KeyY", 2.0),
                           ("Right: enemy cursor", "ArrowRight", 0.8)):
            rows.append(measure(ui, what, lambda k=k: key(k), t))
        for r in rows:
            c = CEIL.get(r["gesture"])
            if c is not None and r["calls"] > c:
                print("FAIL %s: %d drawing calls, ceiling %d"
                      % (r["gesture"], r["calls"], c), flush=True)
                ok = False
        for k in ("ArrowDown", "ArrowLeft", "ArrowDown"):
            key(k)
            M.guest_sleep(ui.m, 0.5)
        ok = identity(ui, p, key) and ok
    if a.json:
        json.dump(rows, open(a.json, "w"), indent=1)
    print("%s wfdraw%s" % ("PASS" if ok else "FAIL", " --herc" if a.herc else ""))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
