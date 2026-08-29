#!/usr/bin/env python3
"""SPEC.md 5.6.4.1's fast walk draws the SAME pixels as 5.6.4's.

    make && python3 tests/linefast.py [--machine os8088_5150_cga]

The claim the fast walk rests on is not "it is quicker" - it is that it lays
the identical pixel set, because 5.6.2 makes the erase contract depend on it:
a trail drawn by one walk and erased by the other has to cancel, and half a
pixel of disagreement is a dashed ghost nobody attributes to the rasteriser.

SO IT IS CHECKED AGAINST ITSELF, ON ONE KERNEL. `gfx_line_raw` reaches the
fast walk through three bytes - `call gfx_line_fast` - and the two bytes after
it are `jc gfx_line_mono`. Poking `stc` + two `nop`s over the call therefore
sends EVERY line down 5.6.4's general walk with nothing else changed: same
build, same clip state, same framebuffer, same everything. Draw a fan, hash
the screen, poke, draw it again, compare. An A/B across two kernels would have
to keep everything else identical by hand; this cannot fail to.

WHAT THE FAN COVERS, and each of these has been a bug in something:

  - all eight octants, so both x directions and both major axes;
  - BLACK and WHITE ink, which are different loops (5.6.4.2) - and the
    background is painted the other colour first, so a plot that quietly
    did nothing would read as a pass;
  - a CLIP REGION armed, so the interval clip is exercised rather than the
    screen-edge case: lines that start above the box (the skip loop runs),
    end below it, cross it entirely, and miss it altogether;
  - lines whose MINOR extent leaves the box, which is the refusal path
    (5.6.4.2) and must draw exactly what the general walk draws;
  - the DITHER ink, which the fast walk refuses outright - here to prove the
    refusal reaches the old loop at all;
  - a DILATED STEEP line BOTH WAYS. 5.6.6.1 made 5.6.6's wide walk a runtime
    CHOICE against three fast passes, so the third configuration below forces
    gfx_lf_wide3 to refuse and takes the wide walk instead. All three must
    agree, which is 5.6.6's own claim - that its walk IS those three passes -
    checked rather than believed.

The whole framebuffer is compared, not a crop: a walk that runs away writes
outside the box and that is the failure worth catching.
"""
import argparse
import hashlib
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNEL_SEG = 0x0060

# x1, y1, x2, y2, fat, colour, what it is for
CL = (60, 30, 300, 200)                 # the clip rect the armed cases use
FAN = []
for _a, _b in ((240, 60), (60, 240), (200, 200), (30, 250), (250, 30)):
    for _sx in (1, -1):
        for _sy in (1, -1):
            FAN.append((180 + _sx * _a // 2, 115 + _sy * _b // 2,
                        180 - _sx * _a // 2, 115 - _sy * _b // 2))
EDGE = [                                # the cases the interval clip is for
    (180, 10, 200, 240),                # starts above the box, ends below
    (180, 10, 260, 60),                 # starts above, ends inside
    (100, 150, 340, 190),               # runs out of the box to the right
    (20, 100, 340, 130),                # crosses it left to right
    (10, 10, 40, 20),                   # misses it entirely
    (200, 250, 210, 300),               # below it entirely
    (61, 31, 299, 199),                 # exactly the box's diagonal
    (59, 29, 301, 201),                 # one pixel outside it, both ends
]


def draw_all(rig, sym, colours):
    """Every case, in order, through the kernel's own gfx_line."""
    for col in colours:
        rig.m.write(os88sym.linear("gfx_color"), bytes([col]))
        for (x1, y1, x2, y2) in FAN + EDGE:
            rig.gfx_line(x1, y1, x2, y2, 0)
            rig.gfx_line(x1, y1, x2, y2, 1)     # 5.6.5's dilation too


def run(rig, sym, armed):
    rig.m.write(os88sym.linear("wm_clip_n"),
                (1 if armed else 0).to_bytes(2, "little"))
    if armed:
        tab = os88sym.linear("wm_clip_tab")
        for i, v in enumerate(CL):
            rig.m.write(tab + 2 * i, v.to_bytes(2, "little"))
    draw_all(rig, sym, (0, 15, 8))          # black, white, and the dither
    return hashlib.sha256(rig.fb()).hexdigest()


def _find_call(sym):
    """The `call gfx_line_fast` inside gfx_line_raw, by its own encoding."""
    img = open(os.path.join(ROOT, "build", "kernel.bin"), "rb").read()
    want = sym["gfx_line_fast"]
    for a in range(sym["gfx_line_raw"], sym["gfx_line_raw"] + 64):
        if a + 3 <= len(img) and img[a] == 0xE8:
            rel = int.from_bytes(img[a + 1:a + 3], "little", signed=True)
            if (a + 3 + rel) & 0xFFFF == want:
                return a
    sys.exit("linefast: no `call gfx_line_fast` in gfx_line_raw - the "
             "dispatch moved, and this test would have poked three bytes of "
             "something else")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc")
    ap.add_argument("--image", default="build/os8088-360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from os88linecost import Rig                             # noqa: E402

    sym = os88sym.syms()
    # `call gfx_line_fast`, wherever gfx_line_raw keeps it. FOUND BY ITS OWN
    # ENCODING rather than by an offset from a neighbour: `E8 rel16` is only
    # this call when rel16 resolves to gfx_line_fast, so an inserted routine
    # cannot silently move it and leave the test poking three bytes of
    # something else - which reads as a pass, both runs having taken the same
    # path.
    call = _find_call(sym)
    wide3 = sym.get("gfx_lf_wide3")
    fails = []
    with os88marty.launch(a.image, machine=a.machine) as m:
        os88marty.settle(m)
        m.pause()
        rig = Rig(m, sym, sym["snd_xlat"])      # the stub's 256 idle .bss
                                                # bytes (tools/os88linecost.py)
        got = m.read((KERNEL_SEG << 4) + call, 3)
        if got[0] != 0xE8:
            sys.exit("linefast: %04x is not `call gfx_line_fast` (%s) - the "
                     "dispatch moved" % (call, got.hex()))
        print("  dispatch at %04x: %s" % (call, got.hex()))

        w3 = m.read((KERNEL_SEG << 4) + wide3, 2) if wide3 else None
        for armed in (False, True):
            what = "clip armed" if armed else "clip disarmed"
            clean = rig.fb()
            fast = run(rig, sym, armed)
            rig.restore(clean)
            m.write((KERNEL_SEG << 4) + call, b"\xF9\x90\x90")  # stc, nop, nop
            slow = run(rig, sym, armed)
            m.write((KERNEL_SEG << 4) + call, got)
            rig.restore(clean)
            wide = fast
            if wide3:                       # ...and 5.6.6's wide walk, forced
                m.write((KERNEL_SEG << 4) + wide3, b"\xF9\xC3")   # stc, ret
                wide = run(rig, sym, armed)
                m.write((KERNEL_SEG << 4) + wide3, w3)
                rig.restore(clean)
            ok = fast == slow == wide
            print("  [%s] %-14s fast %s  general %s  wide %s"
                  % ("PASS" if ok else "FAIL", what,
                     fast[:12], slow[:12], wide[:12]))
            if not ok:
                fails.append(what)
    print("linefast: %s" % ("PASS" if not fails else "FAIL " + ", ".join(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
