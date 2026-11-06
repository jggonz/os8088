#!/usr/bin/env python3
"""The Video Battle's TITLE loop, its FADE and its ENDINGS on MartyPC's XT
(SPEC.md 126.3.1): os8088_xt_hdd_sb, the bundle on its XT-IDE C:.

    make vbattle && python3 tests/vbtloops.py

Two bundles of the demo battle, its enemy at 1 HP so that one Attack wins:

  A. the title goes ROUND (its frame counter laps, and what is on the glass
     is one of its own frames - the host's decode of the bundle - each time
     it is looked at); Enter FADES it (the screen's lit pixels fall step by
     step, the steps in order) and the openings play; the battle; VICTORY
     fades to its END loop, which goes round in the canvas; Enter fades the
     whole screen and the play starts OVER - the title round again;
  B. the title is left by a CLIP, opening 0 entered at one of the title's
     anchors, and the play reaches the menu through it; VICTORY JOINS its
     end loop with no fade at all;
  C. A's ways, with a title and an end loop far bigger than a ring (the
     Limit, 204 KB): each is READ ROUND (format 7, `[vb_lstream]`), and
     still laps with the glass its own frames;
  D. A's, the battle saying ANY KEY continues (header 348): a letter leaves
     the title and plays the end loop again - where in A a letter leaves
     nothing (Enter or Space).

Esc leaves at the end of each. The waits are on the guest's state, never
on a clock: a fade or a lap that never comes fails by name.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build, os88geom as geom, os88marty, os88ui   # noqa: E402
import os88vbt as B         # noqa: E402
import vbtproj as P         # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_xt_hdd_sb"
KEYS = {"return": "Enter", "esc": "Escape", "a": "KeyA"}
VM_CLIP, VM_FADE = 1, 4
GS_MENU, GS_END = 1, 3
fails = []
LIT = [sum(1 for k in range(4) if (x >> 2 * k) & 3) for x in range(256)]


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def project(tmp, mode):
    leave = mode == "B"
    vbp = os.path.join(tmp, "loops.vbp")
    B.demo_project(vbp, bars=2)
    p = P.Project.load(vbp)
    p.set(("enemy", "hp"), 1)
    if leave:                       # B: no openings of its own, the title
        p.set(("openings",), [])    # left by REVEAL entered at 0, 8, 16
        p.set_title("title", (10, 34), leave="reveal", anchors=(0, 8, 16))
        p.set_ending("victory", "join", "victory", (0, 20))
    elif mode == "C":               # C: loops far bigger than a ring - the
        p.set_title("ari_limit", (0, 110))  # Limit, 240 KB - READ ROUND
        p.set_ending("victory", "fade", "ari_limit", (0, 110))
        p.remove_clip("title")      # (the demo's own title, unused here)
    else:
        p.set_title("title", (10, 34))
        p.set_ending("victory", "fade", "victory", (0, 20))
    if mode == "D":                 # D: A's, ANY key continuing (header 348)
        p.set(("any_key",), True)
    p.set(("fade_steps",), 12)
    p.save()
    data = B.build_project(p)
    return data


def screens_of(b, ci):
    """Loop clip CI's frames as the screen's 16 KB, decoded by the host"""
    rs = b.stream(b.clips[ci])
    s = bytearray(65536)
    out = []
    for r in rs[:-1]:
        B.decode(s, r)
        out.append(bytes(s[:16384]))
    return out


def run(tmp, mode, syms):
    leave = mode == "B"
    data = project(tmp, mode)
    b = B.Bundle(data)
    vbt = os.path.join(tmp, "BATTLE.VBT")
    open(vbt, "wb").write(data)
    vhd = os.path.join(tmp, "c.vhd")
    subprocess.run(
        [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
         "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
         "--vbr", os88build.at("build/boothd.bin"),
         "--mbr", os88build.at("build/mbr.bin"),
         "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
         "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
         "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
         "--file", "VBATTLE.O88=" + os88build.at("build/vbattle.o88"),
         "--file", "BATTLE.VBT=" + vbt], check=True, capture_output=True)
    tag = mode
    title = screens_of(b, b.title)
    m = os88marty.launch(None, machine=MACHINE,
                         extra=["--mount", "hd:0:" + vhd], boot=False)
    try:
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=240)
        os88marty.no_saver(m)
        os88marty.ui_done(m)
        w = ui.path("C:/VBATTLE.O88")
        rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4

        def rb(nm, off=0):
            return m.read(base + syms[nm] + off, 1)[0]

        def rw(nm, off=0):
            return struct.unpack_from("<H", m.read(base + syms[nm] + off, 2))[0]

        def until(cond, what, guest=120.0, poll=0.05):
            os88marty.until(m, lambda mm: cond(), what, poll=poll,
                            limit=900.0, guest=guest)

        def key(k):
            m.key(KEYS[k], up=False)
            os88marty.until(m, lambda mm: True, "a key", poll=0.0,
                            limit=10.0, guest=0.06)
            m.key(KEYS[k], down=False)
            os88marty.until(m, lambda mm: True, "a key", poll=0.0,
                            limit=10.0, guest=0.25)

        def vram():
            return m.read(0xB8000, 16384)
        key("return")
        until(lambda: rb("vb_ready") == 1, "the play to start")
        until(lambda: rb("vb_tplay") == 1 and rb("vb_cc") == b.title,
              "%s: the title to be up" % tag)
        if os.environ.get("VBTLOOPS_KEY"):
            print("   key %04X fon %d fwant %d" % (rw("vb_key"), rb("vb_fon"), rb("vb_fwant")), flush=True)
        # THE TITLE GOES ROUND: its records counter comes back to the lap's
        # start at least twice, and the glass shows its own frames
        n = rw("vb_lnrec")
        seen, matched, looks, last, laps = set(), 0, 0, 0, 0
        while laps < 2 and looks < 1200:
            f = rw("vb_cfr")
            if f < last:
                laps += 1
            last = f
            v = vram()
            looks += 1
            if v in title:
                matched += 1
                seen.add(title.index(v))
            until(lambda: True, "a moment", guest=0.05, poll=0.0)
        if mode == "C":
            ok(rb("vb_lstream") == 1 and b.clips[b.title]["len"] > 147456,
               "C: the title (%d KB) is too big for its ring, and READ ROUND"
               % (b.clips[b.title]["len"] // 1024))
        ok(n == len(title) and laps >= 2,
           "%s: the title goes round (%d frames, %d laps in %d looks)"
           % (tag, n, laps, looks))
        ok(matched * 3 >= looks and len(seen) >= 4,
           "%s: ...and the glass shows its own frames (%d of %d looks, %d "
           "frames seen)" % (tag, matched, looks, len(seen)))
        if mode == "A":                 # (a letter is not Enter: it stays)
            key("a")
            os88marty.until(m, lambda mm: True, "a moment", poll=0.0,
                            limit=10.0, guest=0.5)
            ok(rb("vb_tplay") == 1 and rb("vb_fon") == 0,
               "A: a letter does not leave the title (Enter or Space)")
        key("a" if mode == "D" else "return")
        if mode == "D":
            ok(True, "D: a LETTER left the title (any key, header 348)")
        if not leave:
            until(lambda: rb("vb_fon") == 1, "%s: the fade to begin" % tag)
            lit = []
            while rb("vb_fon") == 1:
                st = rb("vb_fstep")
                v = vram()      # lit PIXELS, two bits each: a byte with one
                lit.append((st, sum(LIT[x] for x in v)))    # lit stays lit
                until(lambda: True, "a moment", guest=0.03, poll=0.0)
            steps = [s for s, _ in lit]
            ok(steps == sorted(steps) and max(steps) >= 10,
               "%s: the fade's steps come in order (%s)"
               % (tag, sorted(set(steps))))
            med = lambda xs: sorted(xs)[len(xs) // 2] if xs else -1
            early = med([c for s, c in lit if s <= 2])
            late = med([c for s, c in lit if s >= 11])
            ok(early > 0 and 0 <= late < early // 3,
               "%s: ...and put the picture out: %d lit pixels in its first "
               "steps, %d in its last" % (tag, early, late))
        until(lambda: rb("vb_tplay") == 0, "%s: the title to be left" % tag)
        if leave:
            ok(rb("vb_cc") == 0 and rw("vb_cfr") >= 1,
               "B: the title is left by its clip, opening 0 (clip %d)"
               % rb("vb_cc"))
        until(lambda: rb("vb_state") == GS_MENU, "%s: the menu" % tag,
              guest=180.0)
        ok(True, "%s: the openings play on into the battle's menu" % tag)
        key("return")                   # ARI's Attack, at the 1-HP enemy
        fon_seen = [False]
        vloop = b.endings["victory"][1]

        def ended():
            fon_seen[0] |= rb("vb_fon") == 1
            return rb("vb_eplay") == 1
        until(ended, "%s: the end loop" % tag, guest=180.0, poll=0.02)
        ok(rb("vb_cc") == vloop and rb("vb_over") == 1,
           "%s: victory, and its end loop plays (clip %d)" % (tag, rb("vb_cc")))
        ok(fon_seen[0] != bool(leave),
           "%s: ...%s" % (tag, "JOINED, with no fade" if leave else
                          "after a fade of the canvas"))
        last, laps, looks = 0, 0, 0
        st0 = rw("vb_stall")
        while laps < 2 and looks < 1200:
            f = rw("vb_cfr")
            laps += f < last
            last = f
            looks += 1
            if os.environ.get("VBTLOOPS_DBG") and looks % 20 == 0:
                print("  look %d cfr %d stall %d fill %s" % (
                    looks, f, rw("vb_stall") - st0, [m.read(base + syms[r] + 4, 4).hex() for r in ("vb_rg0", "vb_rg1")]), flush=True)
            until(lambda: True, "a moment", guest=0.05, poll=0.0)
        ok(laps >= 2, "%s: the end loop goes round (%d laps)" % (tag, laps))
        if mode == "C":
            ok(rb("vb_lstream") == 1, "C: ...read round as well")
        if not leave:
            key("a" if mode == "D" else "return")   # play again
            until(lambda: rb("vb_fon") == 1, "A: the fade before again")
            until(lambda: rb("vb_tplay") == 1 and rb("vb_state") != GS_END,
                  "A: the play to start over at the title", guest=60.0)
            ok(rb("vb_cc") == b.title and rb("vb_over") == 0,
               "%s: %s on the end loop plays again: the title round"
               % (tag, "a letter" if mode == "D" else "Enter"))
        key("esc")
        until(lambda: rb("vb_ready") == 0, "%s: Esc to leave" % tag)
        ok(True, "%s: Esc leaves" % tag)
    finally:
        m.quit()


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    for mode in (os.environ.get("VBTLOOPS_ONLY") or "ABCD"):
        with tempfile.TemporaryDirectory() as tmp:
            run(tmp, mode, syms)
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
