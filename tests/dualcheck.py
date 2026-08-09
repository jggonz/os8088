#!/usr/bin/env python3
"""Can this MartyPC drive TWO video cards at once? (docs/DUAL-DISPLAY-PLAN.md 9)

    python3 tests/dualcheck.py                       # os8088_5150_both_gla
    python3 tests/dualcheck.py --machine os8088_5150_both

Step 0 of the dual-display work, and a gate rather than a demo: an extended
desktop across a Hercules and a CGA cannot be verified on an emulator that
holds one card, and it cannot be verified on one that holds two and quietly
maps them onto the same memory either. This answers both, and it answers them
about the EMULATOR - it boots no operating system at all.

That is deliberate. os8088 running on one of the two cards keeps redrawing it,
so every measurement here would be racing a guest; and the question is not
"does os8088 work" but "is this instrument capable of telling me whether it
does". So the CPU is parked in a two-byte `jmp $`, both cards are programmed
from the debugger, and the only thing moving is the rasters.

THE TEST THAT MATTERS IS THE THIRD ONE, and the first two are there because
they look like it and are not:

  1. `cards` reports two.                      Necessary. Says nothing about
                                               whether they are two MEMORIES.
  2. B0000 and B8000 read back what was
     written to each.                          CAN PASS ON AN ALIASED MACHINE,
                                               so it decides nothing on its
                                               own. Measured passing on one
                                               during development: with the
                                               `[[machine.video]]` order
                                               reversed and the Hercules' page
                                               bit never written, its mask is
                                               the full 64KB and the two
                                               addresses are 32KB apart inside
                                               ONE card - so of course they
                                               differ. It happens to fail on
                                               this gate's own machine, because
                                               `program_herc` clears the page
                                               bit first and the card then
                                               masks B8000 down onto page 0.
                                               Neither outcome is evidence.
  3. A write to one card's memory changes
     THAT CARD'S RASTER and not the other's.   The real question. It is the
                                               only one of the three that
                                               fails when the two cards share
                                               memory.

Why a machine can be aliased at all: upstream MartyPC maps a Hercules-subtype
MDA at B0000 *and* B8000 unconditionally (the mapping is built in the
constructor, before any guest has written 3BFh), a CGA maps B8000, and
`Bus::register_map` resolves the overlap by LAST WRITER WINS - it stamps
`mmio_map_fast` and never reads the `priority` field the descriptor carries.
So one of the two cards vanished into the other, and which one depended only
on the order of the `[[machine.video]]` blocks. tools/martypc/patches/02
narrows the Hercules to page 0, which is what a real card with 3BFh bit 1
clear decodes and what os8088's `vid_setmode` deliberately leaves it at
(SPEC.md 39.6). This gate is what says that patch is still applied.

Exit 0 if every check passes, 1 otherwise, with the failing check named.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import os88marty  # noqa: E402

# SPEC.md 39.6's Hercules bring-up, which no BIOS will do for us: graphics
# allowed and page 0 only, video off while the 6845 is half-written, R0..R11
# for 720x348, video on.
HGC_6845 = [0x35, 0x2D, 0x2E, 0x07, 0x5B, 0x02, 0x57, 0x57, 0x02, 0x03, 0x00, 0x00]

# ...and the CGA's, which normally comes from int 10h AX=0006h. 640x200 mono
# graphics: mode register bit 1 (graphics), bit 4 (hi-res), bit 3 (video on).
CGA_6845 = [0x38, 0x28, 0x2D, 0x0A, 0x7F, 0x06, 0x64, 0x70, 0x02, 0x01, 0x06, 0x07]

HERC_BASE, HERC_BYTES = 0xB0000, 4 * 0x2000
CGA_BASE, CGA_BYTES = 0xB8000, 2 * 0x2000

# A RAM address no card and no BIOS data area owns, for the park loop.
PARK = 0x00500


class Fail(Exception):
    pass


def park_cpu(m):
    """Stop the guest contributing anything: a NOP sled into `jmp $`, forever.

    `park` flushes the prefetch queue (it goes through the CPU's reset vector),
    so a bare `jmp $` would do - the sled is belt and braces, and it costs 30
    bytes of a page nothing else wants. It also means the check below is a
    check on `park` rather than on luck.

    Parking matters more than it looks. The alternative is to let the machine
    boot and measure around whatever it does - and what it does is program both
    cards and clear both framebuffers, which is precisely the state this is
    trying to control.
    """
    m.write(PARK, bytes([0x90]) * 30 + bytes([0xEB, 0xFE]))
    m.cmd(cmd="park", cs=0x0000, ip=PARK)
    # ...and prove it took, rather than trusting it: a parked CPU that is not
    # parked reads as a flaky raster later on, a long way from the cause.
    m.cmd(cmd="advance", cycles=200_000)
    ip = m.status()["ip"]
    if not PARK <= ip <= PARK + 31:
        raise Fail("could not park the CPU: ip = 0x%04X, wanted 0x%04X..0x%04X"
                   % (ip, PARK, PARK + 31))


def program_herc(m):
    m.cmd(cmd="outb", port=0x3BF, value=0x01)      # graphics allowed, page 0
    m.cmd(cmd="outb", port=0x3B8, value=0x02)      # graphics, video OFF
    for i, v in enumerate(HGC_6845):
        m.cmd(cmd="outb", port=0x3B4, value=i)
        m.cmd(cmd="outb", port=0x3B5, value=v)
    m.cmd(cmd="outb", port=0x3B8, value=0x0A)      # graphics, video ON


def program_cga(m):
    for i, v in enumerate(CGA_6845):
        m.cmd(cmd="outb", port=0x3D4, value=i)
        m.cmd(cmd="outb", port=0x3D5, value=v)
    m.cmd(cmd="outb", port=0x3D8, value=0x1A)      # hi-res graphics, video on
    m.cmd(cmd="outb", port=0x3D9, value=0x0F)


def lit(m, card):
    """Non-black pixels in a card's RENDERED output."""
    w, h, d = m.fbuf(card=card)
    return sum(1 for i in range(0, len(d), 3) if d[i:i + 3] != b"\x00\x00\x00")


def find(cards, vtype):
    hits = [c for c in cards if c["type"] == vtype]
    if len(hits) != 1:
        raise Fail("expected exactly one %s, got %d (%s)"
                   % (vtype, len(hits), [c["type"] for c in cards]))
    return hits[0]["idx"]


def run(machine, image, verbose):
    def say(*a):
        if verbose:
            print(*a)

    with os88marty.launch(image, machine=machine, boot=False) as m:
        park_cpu(m)

        # --- 1. two cards ---------------------------------------------------
        cards = m.cards()
        say("cards:", [(c["idx"], c["type"]) for c in cards])
        if len(cards) != 2:
            raise Fail("expected 2 video cards, got %d - does this MartyPC "
                       "build keep both [[machine.video]] entries?" % len(cards))
        herc, cga = find(cards, "mda"), find(cards, "cga")
        say("  hercules = card %d, cga = card %d" % (herc, cga))

        program_herc(m)
        program_cga(m)
        m.cmd(cmd="advance", frames=4, card=cga)

        # --- 2. the WEAK test, run so its weakness is on the record ---------
        m.write(HERC_BASE, bytes([0x11, 0x22, 0x33, 0x44]))
        m.write(CGA_BASE, bytes([0xAA, 0xBB, 0xCC, 0xDD]))
        a = m.read(HERC_BASE, 4)
        b = m.read(CGA_BASE, 4)
        say("weak test: B0000=%s B8000=%s" % (a.hex(), b.hex()))
        if a != bytes([0x11, 0x22, 0x33, 0x44]) or b != bytes([0xAA, 0xBB, 0xCC, 0xDD]):
            raise Fail("the two apertures do not even hold their own bytes")
        say("  (passes on an ALIASED machine too - proves nothing on its own)")

        # --- 3. THE TEST: one card's memory drives one card's raster --------
        # Clear both framebuffers and both rasters, then light each card in
        # turn and require the OTHER one not to move.
        m.write(HERC_BASE, bytes(HERC_BYTES))
        m.write(CGA_BASE, bytes(CGA_BYTES))
        m.cmd(cmd="advance", frames=4, card=cga)
        m.cmd(cmd="advance", frames=4, card=herc)
        h0, c0 = lit(m, herc), lit(m, cga)
        say("cleared:  herc lit %d, cga lit %d" % (h0, c0))

        m.write(HERC_BASE, bytes([0xFF]) * HERC_BYTES)
        m.cmd(cmd="advance", frames=4, card=herc)
        m.cmd(cmd="advance", frames=4, card=cga)
        h1, c1 = lit(m, herc), lit(m, cga)
        say("herc lit:  herc %d (+%d), cga %d (+%d)" % (h1, h1 - h0, c1, c1 - c0))
        if h1 <= h0:
            raise Fail("writing the Hercules' memory did not change the "
                       "Hercules' raster - is it programmed, or is it not "
                       "rasterising graphics mode?")
        if c1 != c0:
            raise Fail("writing the HERCULES' memory changed the CGA's raster "
                       "by %d pixels - the two cards share memory (patch 02 "
                       "missing, or the [[machine.video]] order changed)"
                       % (c1 - c0))

        m.write(HERC_BASE, bytes(HERC_BYTES))
        m.cmd(cmd="advance", frames=4, card=herc)
        m.cmd(cmd="advance", frames=4, card=cga)
        h2, c2 = lit(m, herc), lit(m, cga)

        m.write(CGA_BASE, bytes([0xFF]) * CGA_BYTES)
        m.cmd(cmd="advance", frames=4, card=cga)
        m.cmd(cmd="advance", frames=4, card=herc)
        h3, c3 = lit(m, herc), lit(m, cga)
        say("cga lit:   herc %d (+%d), cga %d (+%d)" % (h3, h3 - h2, c3, c3 - c2))
        if c3 <= c2:
            raise Fail("writing the CGA's memory did not change the CGA's raster")
        if h3 != h2:
            raise Fail("writing the CGA's memory changed the HERCULES' raster "
                       "by %d pixels - the two cards share memory" % (h3 - h2))

        # --- 4. the two rasters are the two GEOMETRIES ----------------------
        # A machine that answered both of the above out of one card would also
        # answer them at one geometry.
        hw, hh, _ = m.fbuf(card=herc)
        cw, ch, _ = m.fbuf(card=cga)
        say("geometry: herc %dx%d, cga %dx%d" % (hw, hh, cw, ch))
        if (hw, cw) == (cw, hw) and hh == ch:
            raise Fail("both cards report one geometry (%dx%d)" % (hw, hh))
        if hw < 700 or cw != 640:
            raise Fail("unexpected apertures: herc %dx%d, cga %dx%d - a "
                       "Hercules is 720-wide and a CGA 640" % (hw, hh, cw, ch))

        # --- 5. both cards keep their OWN clock -----------------------------
        # 50Hz Hercules against 60Hz CGA. A capture paced on the wrong card's
        # counter is what the `card=` argument exists to prevent.
        f0 = {c["idx"]: c["frames"] for c in m.cards()}
        m.cmd(cmd="advance", frames=30, card=cga)
        f1 = {c["idx"]: c["frames"] for c in m.cards()}
        dh, dc = f1[herc] - f0[herc], f1[cga] - f0[cga]
        say("30 CGA frames = %d Hercules frames" % dh)
        if dc < 30:
            raise Fail("advance(frames=30, card=cga) moved the CGA %d frames" % dc)
        if dh == 0:
            raise Fail("the Hercules' frame counter did not move at all")
        if dh == dc:
            raise Fail("both cards report the same frame count over the same "
                       "interval - one counter is being read for both")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--machine", default="os8088_5150_both_gla",
                    help="MartyPC machine (default: the GLaBIOS two-card 5150)")
    ap.add_argument("--image", default="build/os8088-360.img",
                    help="a floppy to mount; its contents are never read")
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args()
    try:
        run(a.machine, a.image, not a.quiet)
    except Fail as e:
        print("dualcheck: FAIL - %s" % e, file=sys.stderr)
        return 1
    except Exception as e:                       # noqa: BLE001 - report, don't trace
        print("dualcheck: ERROR - %s" % e, file=sys.stderr)
        return 1
    print("dualcheck: %s drives two independent displays - PASS" % a.machine)
    return 0


if __name__ == "__main__":
    sys.exit(main())
