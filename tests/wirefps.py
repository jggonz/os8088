#!/usr/bin/env python3
"""What SPEC.md 5.6.4.1 is worth to a program, measured by the program.

    make wiredisk && python3 tests/wirefps.py [--machine os8088_5150_cga_gla]

WIREFRAME DOES NOT SHIP (SPEC.md 78.9), so `make wiredisk` is what puts
WIRE.O88 on a floppy for this to open. `make` alone still builds the
package - it just no longer lands on the apps disk.

apps/wire (SPEC.md 78) draws a wireframe solid with nothing but
OSAPI_GFX_LINE and keeps its own frame rate in `wr_fps`, in tenths. So the
whole question - what does the fast walk buy something that draws lines? -
is one program, run twice on one machine, reading its own answer out of its
own bss.

THE TWO RUNS ARE THE SAME BOOT AND THE SAME BINARY. `gfx_line_raw` reaches
the fast walk through three bytes; poking `stc` + two `nop`s over them sends
every line down 5.6.4's general walk instead, with nothing else different -
not the kernel, not the package, not the window, not where it sits. An A/B
across two builds would have to hold all of that equal by hand.

It is a MEASUREMENT and not a gate: no threshold is asserted, because a
number that fails a build when a harness gets slower teaches nobody anything.
It prints, and PERFORMANCE.md is where the figure is kept.

**ON THE GLaBIOS TWIN**, `os8088_5150_herc_gla`, because `os8088_5150_herc`
wants the IBM ROM this repo cannot ship. Checked with that ROM dropped in
beside GLaBIOS: the fast walk reads 18.2 fps on both, the general walk 9.5
against 9.1, and the ratio this row exists to report 1.92x against 2.00x.

**IT HAD ALSO BEEN DEAD SINCE SPEC.md 2.9**, like tests/linefast.py and for the
same reason: stage 2 went in front of `.text` inside `kernel.bin`, so
`find_call`'s scan read 6,656 bytes of the wrong thing, found nothing and
exited "no `call gfx_line_fast` in gfx_line_raw" - which reads as a kernel that
moved rather than a scan that did not. See `find_call`.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import os88layout                                           # noqa: E402
import dispapps                                             # noqa: E402
import dispcp                                               # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNEL_SEG = 0x0060
WR_FPS = 14                             # apps/wire's own bss offsets, from its
WR_NE = 38                              # `os88_image_end +` block


def find_call(sym):
    """The `call gfx_line_fast` in gfx_line_raw, by its own encoding."""
    img = open(os.path.join(ROOT, "build", "kernel.bin"), "rb").read()
    # ...MINUS STAGE 2, which SPEC.md 2.9 put in front of .text inside the same
    # file: a symbol offset stopped indexing kernel.bin that day, so this scan
    # was reading 6,656 bytes of the wrong thing and found no call at all.
    # tools/os88layout.py is the one place that knows the size, and after it
    # `a` is a .text offset - which is what the poke below wants. The same
    # subtraction as tests/unit/t_api_abi.py and tests/linefast.py.
    img = img[os88layout.boot2_pad(ROOT):]
    want = sym["gfx_line_fast"]
    for a in range(sym["gfx_line_raw"], sym["gfx_line_raw"] + 64):
        if a + 3 <= len(img) and img[a] == 0xE8:
            rel = int.from_bytes(img[a + 1:a + 3], "little", signed=True)
            if (a + 3 + rel) & 0xFFFF == want:
                return a
    sys.exit("wirefps: no `call gfx_line_fast` in gfx_line_raw")


def sample(m, seg, base, secs=6):
    """Let it run, then read the package's own figure. Tenths -> float."""
    end = m.status()["cycles"] + int(4772727 * secs)
    while m.status()["cycles"] < end:
        m.advance(frames=120)
        m.run()
    m.pause()
    fps = int.from_bytes(m.readseg(seg, base + WR_FPS, 2), "little")
    ne = m.readseg(seg, base + WR_NE, 1)[0]
    return fps / 10.0, ne


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/wire360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    S = os88sym.linear
    sym = os88sym.syms()
    call = find_call(sym)

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        rows = [r[0] for r in dispcp.listing(m, S)]
        # ...and NOT open_named for the package itself: it settles, and a
        # window with a live worker in it never settles again.
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                               rows.index("WIRE.O88"))
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        m.advance(frames=200)
        m.run()
        got = dispapps.pkg_seg(m, 0)
        if got is None:
            sys.exit("wirefps: WIRE.O88 did not open")
        slot, seg = got
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")   # header +8 =
        # OS88_IMAGE_SIZE, which IS os88_image_end: the loader zeroes the bss
        # immediately after the image
        print("  WIRE.O88: window %d, segment %04x, bss at %04x"
              % (slot, seg, base))

        orig = m.read((KERNEL_SEG << 4) + call, 3)
        fast, ne = sample(m, seg, base)
        m.write((KERNEL_SEG << 4) + call, b"\xF9\x90\x90")      # stc, nop, nop
        m.run()
        slow, _ = sample(m, seg, base)
        m.write((KERNEL_SEG << 4) + call, orig)
        m.run()
        again, _ = sample(m, seg, base)

    print()
    print("  %s, %d edges drawn and erased a frame" % (a.machine, ne))
    print("  SPEC.md 5.6.4.1's walk   %5.1f fps" % fast)
    print("  SPEC.md 5.6.4's walk     %5.1f fps" % slow)
    print("  ...and back              %5.1f fps" % again)
    if slow > 0:
        print("  ratio                    %5.2fx" % (fast / slow))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
