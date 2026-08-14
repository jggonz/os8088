#!/usr/bin/env python3
"""Does going back to text name the CARD? (SPEC.md 39.20)

    make marty && make && python3 tests/disptext.py
    python3 tests/disptext.py --primary vga        # the other direction

`vid_text` asked int 10h for a MODE - mode 7, "mono text" - and on a machine
with one mono card in it that is the same statement as "program the MDA". A
VGA's option ROM owns int 10h and its mode 7 is the VGA's OWN mono text:
aperture at B0000, CRTC at 3B4, both directly on top of the real Hercules. So
the reboot path put two cards on one address space, on its way out of the GUI
with a bootstrap as the next instruction.

THE ASSERTION IS dualcheck's, AND IT HAS TO BE. "Is the mono card in text
mode" cannot see this bug - the mono card is fine either way; what is wrong is
that a SECOND card has moved on top of it. So the question is the one
dualcheck asks about apertures: after the text handoff, does writing B0000
still change the mono card's raster ALONE? If the VGA has taken that aperture
the write reaches both, and that is the failure this exists to catch.

CALLING IT NEEDS A STUB, because the thing under test is three instructions
ahead of an int 19h. CMD_REBOOT reboots, and int 19h is a bootstrap rather
than a POST, so os8088 comes straight back up and reprograms both cards - the
state to be measured is destroyed by the very call that produces it. So the
machine is driven to the arrangement first, then PARKED, and the parked CPU is
pointed at a stub that calls vid_reboot and falls into `jmp $` - the prologue
idiom dualcheck already uses for the VGA's option ROM, one segment along. The
stub is written over menu_bcell, which is a composition buffer for a menu bar
that will never be drawn again.

WHAT IS DELIBERATELY NOT ASSERTED is that the boot appears on the monitor the
user was looking at. On a VGA+MDA machine it cannot: the BIOS owns int 10h and
prints on the VGA, and "which card is primary" is an os8088 idea the next
operating system has never heard of (SPEC.md 39.20.1). What is asserted is
that both monitors are left in a sane state and that the two cards are not
fighting.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dispcp                                               # noqa: E402
import dualcheck                                            # noqa: E402

HERC_BASE = 0xB0000
KERNEL_SEG = 0x0060


class Fail(Exception):
    pass


def lit(m, card):
    w, h, d = m.fbuf(card=card)
    return sum(1 for i in range(0, len(d), 3) if d[i:i + 3] != b"\x00\x00\x00")


def geom(m, card):
    w, h, _ = m.fbuf(card=card)
    return (w, h)


def stub(m, S, entry):
    """`cli / mov ax,cs / mov ds,ax / call <entry> / jmp $`, in KERNEL_SEG.

    A near call, so CS has to be the kernel's - which is why this cannot go at
    dualcheck's PARK (CS = 0) and goes in the kernel segment instead.
    """
    where = S("menu_bcell") - KERNEL_SEG * 16       # offset within the segment
    target = S(entry) - KERNEL_SEG * 16
    rel = (target - (where + 8)) & 0xFFFF           # cli+mov+mov = 5, call = 3
    code = bytes([0xFA,                             # cli
                  0x8C, 0xC8,                       # mov ax, cs
                  0x8E, 0xD8,                       # mov ds, ax
                  0xE8, rel & 0xFF, rel >> 8,       # call entry
                  0xEB, 0xFE])                      # jmp $
    m.write(S("menu_bcell"), code)
    return where, len(code)


def where_is_the_vga(m):
    """Which card the VGA BIOS last set a mode on, out of its own record.

    dualcheck's raster discriminator - write one card's memory, watch one
    card's output - is the right instrument for an aperture question and
    CANNOT BE USED HERE, for two reasons that are both about the emulator and
    neither about os8088. MartyPC does not rasterise MDA TEXT at all, so the
    mono card reads 0 lit whatever is in its memory (which also defeats
    "did the write land"); and a VGA in mode 3 has a BLINKING CURSOR, so its
    raster changes by ~16 pixels between any two samples whether or not
    anything wrote to it. Measured both, on the fixed kernel.

    So the question is asked where the answer is written down instead. 40:49
    and 40:63 are the VGA BIOS's OWN record of the mode it set and the CRTC
    base that mode uses, and they are the difference the bug is made of:
    mode 7 puts the VGA at 3B4 and B0000, on top of the Hercules; mode 3 puts
    it at 3D4 and B8000, where it belongs. This is a stronger reading than a
    raster in one way and a weaker one in another - it is what the ROM
    BELIEVES rather than what the card is doing - and the geometry check
    beside it is the independent second opinion (mode 3 rasters 720x400,
    mode 7 rasters 720x350).
    """
    bda = m.readseg(0x0040, 0x0049, 1)[0]
    crtc = m.readseg(0x0040, 0x0063, 2)
    return bda, crtc[0] | (crtc[1] << 8)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga_herc")
    ap.add_argument("--primary", default="herc", choices=("herc", "vga"))
    ap.add_argument("--entry", default="vid_reboot",
                    help="kernel routine the stub calls (vid_text to see "
                         "the fsx path instead)")
    a = ap.parse_args(argv)

    S = os88sym.linear
    fail = []

    def say(*x):
        print(*x)

    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine=a.machine) as m:
        mo = os88mouse.Mouse(marty=m)

        def settle(mm, card=None):
            os88marty.settle(mm, card=card)

        os88marty.settle(m, card=0)
        cs = {c["idx"]: c for c in m.cards()}
        mono = [i for i, c in cs.items() if c.get("type") == "mda"][0]
        colour = [i for i, c in cs.items() if c.get("type") == "vga"][0]
        say("cards: vga %d, mda %d" % (colour, mono))

        # --- the arrangement: extended, and the primary the caller asked for
        dispcp.open_panel(m, mo, S, settle, card=0)
        dispcp.set_mode(m, mo, S, settle, "right", card=0)
        if a.primary == "herc":
            avail = m.read(S("vid_avail"), 1)[0]
            dispcp.set_primary(m, mo, S, settle,
                               dispcp.adapter_row(avail, 1), card=mono)
        kind = m.read(S("vid_kind"), 1)[0]
        want = 1 if a.primary == "herc" else 0
        say("primary: vid_kind = %d (wanted %d)" % (kind, want))
        if kind != want:
            raise Fail("could not make %s the primary" % a.primary)

        say("before the handoff: mono %dx%d %d lit, colour %dx%d %d lit"
            % (geom(m, mono) + (lit(m, mono),)
               + geom(m, colour) + (lit(m, colour),)))

        # --- park, then run vid_reboot once ---------------------------------
        off, n = stub(m, S, a.entry)
        m.cmd(cmd="park", cs=KERNEL_SEG, ip=off)
        m.cmd(cmd="advance", cycles=8_000_000)
        ip = m.status()["ip"]
        if not off <= ip <= off + n:
            raise Fail("the stub did not run to its jmp $: ip = 0x%04X, "
                       "wanted 0x%04X..0x%04X" % (ip, off, off + n))
        say("%s ran; parked at ip = 0x%04X" % (a.entry, ip))

        say("after the handoff:  mono %dx%d %d lit, colour %dx%d %d lit"
            % (geom(m, mono) + (lit(m, mono),)
               + geom(m, colour) + (lit(m, colour),)))

        # 1. THE BUS. The whole reason this file exists.
        mode, crtc = where_is_the_vga(m)
        say("    the VGA BIOS's own record: 40:49 mode = 0x%02X, "
            "40:63 CRTC = 0x%03X" % (mode, crtc))
        if mode == 0x07 or crtc == 0x3B4:
            fail.append("the VGA is in MONO TEXT (mode 0x%02X at CRTC 0x%03X):"
                        " its aperture is B0000 and its CRTC is 3B4, both on "
                        "top of the real Hercules. That is SPEC.md 39.20's bug"
                        % (mode, crtc))
        else:
            say("  [ok] the VGA is at CRTC 0x%03X, clear of the mono card"
                % crtc)

        # 2. The mono card holds a CLEARED TEXT PAGE and not a frozen 720x348
        #    desktop read as characters. Read out of MEMORY rather than off
        #    the raster, because MartyPC does not rasterise MDA text - the
        #    bytes are the honest question here and the pixels are not.
        if a.primary == "herc":
            page = m.read(HERC_BASE, 4000)
            pairs = [page[i:i + 2] for i in range(0, len(page), 2)]
            nblank = sum(1 for p in pairs if p == b"\x20\x07")
            say("    B0000 holds %d/%d blank cells" % (nblank, len(pairs)))
            if nblank != len(pairs):
                fail.append("the mono card's text page is not cleared (%d of "
                            "%d cells): what is in there is a 720x348 graphics"
                            " desktop read as characters"
                            % (len(pairs) - nblank, len(pairs)))
            else:
                say("  [ok] the mono card holds a cleared 80x25 text page")

        # 3. The card int 10h is going to print on has a text mode, but only
        #    on the reboot path - vid_text alone deliberately leaves it alone
        #    (SPEC.md 39.20.1: fsx has darked it and must keep it dark).
        g = geom(m, colour)
        if a.entry == "vid_reboot":
            if g == (640, 480):
                fail.append("the VGA is still in mode 12h (%dx%d): the next "
                            "boot's messages render as graphics over a stale "
                            "desktop (SPEC.md 39.20.1)" % g)
            else:
                say("  [ok] the VGA left mode 12h for a text raster %dx%d" % g)
        elif a.primary == "herc":
            if g != (640, 480):
                fail.append("vid_text moved the VGA to %dx%d - it must leave "
                            "the other card alone, because inside an fsx "
                            "bracket that card has been DARKED (39.18)" % g)
            else:
                say("  [ok] vid_text left the VGA where it was (%dx%d)" % g)

    if fail:
        print("\ndisptext: FAIL")
        for f in fail:
            print("  - " + f)
        return 1
    print("\ndisptext: PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Fail as e:
        print("disptext: FAIL\n  - %s" % e)
        sys.exit(1)
