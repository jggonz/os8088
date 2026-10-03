#!/usr/bin/env python3
"""What MIDIRack's window COSTS TO DRAW, a gesture at a time (SPEC.md 105.9.4).

    python3 tests/mrdraw.py [--vga] [--json OUT]

A redraw is priced by the primitive calls it makes (PERFORMANCE.md's first
sentence), so this counts them: every far call the package makes into a
DRAWING cell of the API table (fill, frame, line, pixel, text run, icon,
blit, the planar blit the colour face's pictures take, grey and XOR fill) - armed on the cell itself, so the kernel's own
painting (a menu, the title bar) is never counted - for each of the gestures
a user makes while a song plays. Each call is told apart as the UI task's or
the worker's by the stack it was made on, and named by the package routine
that made it (the far return address), which is how a regression is found
rather than only detected.

The default machine is MartyPC's 4.77 MHz 5150 with Hercules and an AdLib
(the FM output, the 720KB disks); --vga is the XT with a VGA and no card, on
the 360KB system and media disks, playing the background melody - the colour
face (SPEC.md 105.9.5) - and asserts the same ceilings.

TWO ASSERTIONS:

  ceilings  each gesture's UI-task calls under a number that a FULL repaint
            or a whole-group button pass would blow straight through: what
            105.9.4 promises is that a command draws what it changed, and a
            caller that goes back to mru_repaint fails here by name
  identity  after a run of gestures - play, pause, the selection moved, a
            skip, Loop on and off - the window is captured, a FULL repaint is
            forced (a card up and down) and captured again, and the two must
            be IDENTICAL: the caches are only sound if drawing through them
            arrives at the picture a paint from nothing does
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

HERC = ("build/os8088-720.img", "build/apps720.img",
        "os8088_5150_herc_adlib_720_gla", "B:/APPS/MIDIRACK.O88")
VGA = ("build/os8088-360.img", "build/media360.img", "os8088_xt_vga",
       "B:/MIDIRACK.O88")
# THE CEILINGS: UI-task drawing calls a gesture may make. Each is a few times
# what the gesture draws through its caches and a fraction of a repaint
# (~150 on either face) or of one pass over all fourteen buttons (~84). A
# Stop and a Next are the dearest honestly: the rack goes back to a song's
# opening instruments, so up to sixteen bars and sixteen programs move
CEIL = {"Play (space)": 40, "Pause (space)": 20, "Resume (space)": 20,
        "Loop toggle (l)": 30, "Loop toggle back (l)": 30,
        "Down: select row 2": 12, "Next song (n)": 70, "Stop (s)": 70}
CELLS = {"pixel": 0x20, "hline": 0x28, "vline": 0x30, "fill": 0x38,
         "frame": 0x40, "gray": 0x48, "xor": 0x58, "blit4": 0x182,
         "text": 0x1E5, "icon": 0x39D, "blitp": 0x3A4}


SYMS = []


def cell_addrs():
    return {M.KERNEL_SEG * 16 + off: name for name, off in CELLS.items()}


def caller(m, rec):
    """(SS, the package routine that made the call): a far call's return
    address is on top of the stack when the cell is reached."""
    r = rec.get("regs") or {}
    ss, sp = r.get("ss", 0), r.get("sp", 0)
    ip = int.from_bytes(bytes(m.read(ss * 16 + sp, 2)), "little")
    return ss, sp, ip


def nearest(ip):
    best = None
    for name, off in SYMS:
        if off <= ip:
            best = name
        else:
            break
    return best or "?"


def measure(ui, what, act, settle, worker_sp=None):
    addrs = cell_addrs()
    with M.bp_trace(ui.m, *addrs.keys(), cap=50000, regs=True,
                    on_hit=caller) as tr:
        act()
        M.guest_sleep(ui.m, settle)
    by, fn, ui_n, wk_n, sps = {}, {}, 0, 0, []
    for h in tr.hits:
        ss, sp, ip = h.get("hit") or (0, 0, 0)
        sps.append(sp)
        if worker_sp and worker_sp[0] - 64 <= sp <= worker_sp[1] + 64:
            wk_n += 1
            continue
        ui_n += 1
        n = addrs.get(h["addr"], "?")
        by[n] = by.get(n, 0) + 1
        f = nearest(ip)
        fn[f] = fn.get(f, 0) + 1
    row = {"gesture": what, "calls": tr.n, "ui": ui_n, "worker": wk_n,
           "by": by, "callers": fn, "sp": [min(sps or [0]), max(sps or [0])]}
    print("%-24s ui %4d  worker %4d  %s" % (what, ui_n, wk_n, " ".join(
        "%s %d" % kv for kv in sorted(by.items()))), flush=True)
    if fn:
        print("      " + ", ".join("%s %d" % kv for kv in sorted(
            fn.items(), key=lambda kv: -kv[1])[:10]), flush=True)
    return row


def content(ui, p):
    """The window's content as the card rasterised it, row-major RGB."""
    w, h, px = ui.m.fbuf()
    x0, y0 = p.w("mru_ox"), p.w("mru_oy")
    cw, ch = p.w("mru_cw"), p.w("mru_ch")
    out = bytearray()
    for y in range(y0, y0 + ch):
        out += px[(y * w + x0) * 3:(y * w + x0 + cw) * 3]
    return cw, ch, bytes(out)


def identity(ui, p, key):
    """The picture the caches drew against the picture a repaint draws."""
    M.guest_sleep(ui.m, 3.0)                    # the bars fall, then still
    a = content(ui, p)
    key("KeyI")                                 # MIDI Info up...
    M.guest_sleep(ui.m, 1.5)
    key("Escape")                               # ...and down: a full repaint
    M.guest_sleep(ui.m, 2.0)
    b = content(ui, p)
    cw, ch, ap = a
    bp = b[2]
    bad = [(i // 3 % cw, i // 3 // cw) for i in range(0, len(ap), 3)
           if ap[i:i + 3] != bp[i:i + 3]]
    if bad:
        xs = [x for x, _ in bad]
        ys = [y for _, y in bad]
        print("FAIL identity: %d pixels differ between the incremental "
              "picture and a repaint, in (%d,%d)-(%d,%d) of the content"
              % (len(bad), min(xs), min(ys), max(xs), max(ys)), flush=True)
        return False
    print("PASS identity: %dx%d content, the incremental picture IS the "
          "repaint's" % (cw, ch), flush=True)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    ap.add_argument("--vga", action="store_true")
    a = ap.parse_args()
    img, apps, machine, path = VGA if a.vga else HERC
    rows = []
    ok = True
    with os88ui.boot(img, apps=apps, machine=machine) as ui:
        ui.path(path)
        p = mrprobe.attach(ui)
        M.until(ui.m, lambda _: p.b("mr_loaded") == 1, "the autoload",
                poll=.2, limit=120)
        if a.vga:
            p.put("mr_want", b"\x04")              # the speaker, and...
            p.put("mr_bg", b"\x01")                # ...in the background
        else:
            p.put("mr_want", b"\x02")              # OPL2
        M.guest_sleep(ui.m, 1.0)
        global SYMS
        SYMS = sorted(((k, v) for k, v in p.s.items()
                       if "." not in k and v < 0x8000), key=lambda kv: kv[1])
        key = ui.m.key
        key("Space")
        M.until(ui.m, lambda _: p.b("mr_state") == 1, "playing", poll=.1,
                limit=10)
        M.guest_sleep(ui.m, 1.0)
        idle = measure(ui, "playing, 5 s idle (worker)", lambda: None, 5.0)
        wsp = idle["sp"]
        idle["worker"], idle["ui"] = idle["calls"], 0
        rows.append(idle)
        key("KeyS")
        M.until(ui.m, lambda _: p.b("mr_state") == 0, "stopped", poll=.1,
                limit=10)
        M.guest_sleep(ui.m, 1.0)
        rows.append(measure(ui, "Play (space)", lambda: key("Space"), 1.0,
                            wsp))
        for what, k, t in (("Pause (space)", "Space", 1.0),
                           ("Resume (space)", "Space", 1.0),
                           ("Loop toggle (l)", "KeyL", 1.0),
                           ("Loop toggle back (l)", "KeyL", 1.0),
                           ("Down: select row 2", "ArrowDown", 1.0),
                           ("Next song (n)", "KeyN", 1.5),
                           ("Stop (s)", "KeyS", 1.0)):
            rows.append(measure(ui, what, lambda k=k: key(k), t, wsp))
        for r in rows:
            c = CEIL.get(r["gesture"])
            if c is not None and r["ui"] > c:
                print("FAIL %s: %d UI-task drawing calls, ceiling %d"
                      % (r["gesture"], r["ui"], c), flush=True)
                ok = False
        # the identity: a run of gestures, ending paused (a still picture)
        for k in ("ArrowUp", "Space", "KeyL", "ArrowDown", "KeyN", "KeyL",
                  "ArrowUp"):
            key(k)
            M.guest_sleep(ui.m, 0.8)
        for _ in range(3):                          # ...to PAUSED, from
            if p.b("mr_state") == 2:                # whichever state the
                break                               # run left it in
            key("Space")
            M.guest_sleep(ui.m, 1.5)
        M.until(ui.m, lambda _: p.b("mr_state") == 2, "paused", poll=.1,
                limit=10)
        ok = identity(ui, p, key) and ok
    print("gestures: %d calls from the UI task" %
          sum(r["ui"] for r in rows))
    if a.json:
        json.dump(rows, open(a.json, "w"), indent=1)
    print("%s mrdraw" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
