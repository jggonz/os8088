#!/usr/bin/env python3
"""Can this MartyPC drive TWO video cards at once? (docs/DUAL-DISPLAY-PLAN.md 9)

    python3 tests/dualcheck.py                          # os8088_5150_both_gla
    python3 tests/dualcheck.py --machine os8088_5150_both
    python3 tests/dualcheck.py --machine os8088_xt_vga_herc   # VGA + Hercules

Step 0 of the dual-display work, and a gate rather than a demo: an extended
desktop across two cards cannot be verified on an emulator that holds one, and
it cannot be verified on one that holds two and quietly maps them onto the same
memory either. This answers both, and it answers them about the EMULATOR - it
boots no operating system at all.

That is deliberate. os8088 running on one of the two cards keeps redrawing it,
so every measurement here would be racing a guest; and the question is not
"does os8088 work" but "is this instrument capable of telling me whether it
does". So the CPU is parked in a two-byte `jmp $`, both cards are programmed
from the debugger, and the only thing moving is the rasters.

IT COVERS BOTH TWO-CARD MACHINES OUT OF ONE BODY - Hercules+CGA, which is the
calibration 5150's pair and what SPEC.md 39.12-39.19 ships for, and
VGA+Hercules (docs/DUAL-DISPLAY-VGA.md), which is the pair the period built the
most of. One gate rather than two, for tests/gfxbench's reason: two sources
that measure "the same" thing drift apart, and the drift is silent. Everything
that differs between the two pairs is a row in CARDS - the aperture, the fill
that lights it, and how the card is brought up.

Each card is driven through THE APERTURE os8088 ITSELF USES: A0000 for a VGA in
mode 12h, B0000 for a Hercules on page 0, B8000 for a CGA. A gate that proved
two cards independent at some other address would be answering a question
nobody asked.

THE TEST THAT MATTERS IS THE THIRD ONE, and the first two are there because
they look like it and are not:

  1. `cards` reports two.                      Necessary. Says nothing about
                                               whether they are two MEMORIES.
  2. each card's aperture reads back what
     was written to it.                        CAN PASS ON AN ALIASED MACHINE,
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

# ...and the VGA gets no register table at all, because it is the one card here
# with an option ROM: five bytes of guest code ask int 10h for mode 12h and the
# card's own BIOS does what a hand-written table would have guessed at.
#
# MODE 12h AND NOT THE MODE 3 IT POSTS IN, which is worth stating because mode 3
# is free and was tried first. Two things make it the wrong surface. Its
# aperture is B8000, and os8088 draws mode 12h through A0000 - so a gate built
# on B8000 would prove the two cards independent at an address the kernel never
# uses, while leaving the one it does use untested. And text mode is ODD/EVEN
# planed: characters land in plane 0 and attributes in plane 1, the debug
# server's read path returns plane 0 alone, and a `11 22 33 44` written there
# reads back `11 00 33 00`. The writes are fine - a DB/07 fill lights 288,000
# pixels - so it is the READ that cannot see half of what is there, which would
# have made check 2 fail on a machine with nothing wrong with it.
#
# In 12h the same aperture reads back exactly what was written and a full fill
# lights 307,200 pixels, which is 640x480 and nothing else.
VGA_BASE, VGA_BYTES = 0xA0000, 640 * 480 // 8

# mov ax, 0x0012 / int 0x10 - run as park_cpu's prologue, so the mode is set by
# the card's own ROM and the CPU then falls into the same `jmp $` as ever.
VGA_MODE12 = bytes([0xB8, 0x12, 0x00, 0xCD, 0x10])

# A RAM address no card and no BIOS data area owns, for the park loop.
PARK = 0x00500


class Fail(Exception):
    pass


def frames_of(m, idx):
    return {c["idx"]: c["frames"] for c in m.cards()}[idx]


def warm_up(m, idxs, say):
    """Let POST run until each of these cards is rasterising. Park AFTER this.

    A VGA PROGRAMS ITSELF and the other two do not, which is the whole reason
    this exists. Its option ROM runs during POST and leaves it in mode 3,
    scanning; a Hercules and a CGA are left alone by every BIOS here and are
    programmed by hand below. So parking the CPU first - which is what this
    gate did for as long as it only knew about those two - stops POST before
    the VGA's ROM has run, and the card then sits at 0 frames FOR EVER.

    That failure is silent and expensive. `advance frames=` on a card whose
    counter never moves spins until the debug server's own 300-second timeout,
    so the symptom is a gate that appears to hang, five minutes from the cause,
    on a machine where both cards are present and one of them is fine.

    Rasterising is TWO consecutive batches that each move the counter, not one.
    A single advance can be caught in the middle of the ROM's own mode set,
    which produces a frame or two and then stops.
    """
    for idx in idxs:
        spent, prev, runs = 0, frames_of(m, idx), 0
        while spent < WARM_MAX:
            m.cmd(cmd="advance", cycles=WARM_STEP)
            spent += WARM_STEP
            now = frames_of(m, idx)
            runs = runs + 1 if now > prev else 0
            prev = now
            if runs >= 2:
                say("warm-up:  card %d rasterising after %d cycles (%d frames)"
                    % (idx, spent, now))
                break
        else:
            raise Fail("card %d never started rasterising in %d cycles of POST "
                       "- it programs itself, so this is the card's option ROM "
                       "not running, not a missing `program` here"
                       % (idx, WARM_MAX))


def park_cpu(m, prologue=b""):
    """Stop the guest contributing anything: a NOP sled into `jmp $`, forever.

    `park` flushes the prefetch queue (it goes through the CPU's reset vector),
    so a bare `jmp $` would do - the sled is belt and braces, and it costs 30
    bytes of a page nothing else wants. It also means the check below is a
    check on `park` rather than on luck.

    Parking matters more than it looks. The alternative is to let the machine
    boot and measure around whatever it does - and what it does is program both
    cards and clear both framebuffers, which is precisely the state this is
    trying to control.

    A PROLOGUE is the one thing the guest is allowed to do on its way in, and it
    exists for the VGA: setting a VGA mode means calling its option ROM, which
    means running code, which is exactly what parking stops. So the mode set
    goes AHEAD of the sled, the CPU runs it once, and falls into the same
    `jmp $` it would have reached anyway.
    """
    code = prologue + bytes([0x90]) * 30 + bytes([0xEB, 0xFE])
    m.write(PARK, code)
    m.cmd(cmd="park", cs=0x0000, ip=PARK)
    # ...and prove it took, rather than trusting it: a parked CPU that is not
    # parked reads as a flaky raster later on, a long way from the cause.
    # Generous, because a prologue here is an int 10h mode set and the ROM's
    # own 640x480 clear is most of it.
    m.cmd(cmd="advance", cycles=6_000_000 if prologue else 200_000)
    ip = m.status()["ip"]
    lo, hi = PARK + len(prologue), PARK + len(code)
    if not lo <= ip <= hi:
        raise Fail("could not park the CPU: ip = 0x%04X, wanted 0x%04X..0x%04X"
                   " (prologue of %d bytes ran?)" % (ip, lo, hi, len(prologue)))


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


def program_vga(m):
    """Nothing: the mode was set by the card's own ROM, in park_cpu's prologue.

    It has to happen there rather than here because setting a VGA mode means
    RUNNING GUEST CODE, and by the time the other two cards are being programmed
    the CPU is parked. See VGA_MODE12 and park_cpu.
    """      # cursor off


# What each kind of card is, so that ONE gate covers both two-card machines
# rather than two gates that can drift apart - tests/gfxbench's argument for
# being one package for Hercules and CGA, in a harness instead of a package.
#
# `base`/`bytes` is the aperture whose contents that card's raster is drawn
# from, `lit` the fill that makes it bright, and `program` what has to happen
# before it scans at all (None for the VGA, which arrives programmed).
CARDS = {
    "mda": dict(label="Hercules", base=HERC_BASE, size=HERC_BYTES,
                program=program_herc, selfprog=False,
                lit=lambda n: bytes([0xFF]) * n),
    "cga": dict(label="CGA", base=CGA_BASE, size=CGA_BYTES,
                program=program_cga, selfprog=False,
                lit=lambda n: bytes([0xFF]) * n),
    "vga": dict(label="VGA", base=VGA_BASE, size=VGA_BYTES,
                program=program_vga, selfprog=True, prologue=VGA_MODE12,
                lit=lambda n: bytes([0xFF]) * n),
}

# How long POST is given to bring a self-programming card up, and in what
# steps. 4M cycles is ~0.84s of guest time on a 4.77MHz machine and the VGA
# here is rasterising well inside one batch; the cap is generous because the
# failure it guards is a card that never comes up at all, and that is worth
# saying rather than waiting 300s for the server to say it.
WARM_STEP, WARM_MAX = 2_000_000, 40_000_000


def lit(m, card):
    """Non-black pixels in a card's RENDERED output."""
    w, h, d = m.fbuf(card=card)
    return sum(1 for i in range(0, len(d), 3) if d[i:i + 3] != b"\x00\x00\x00")


def run(machine, image, verbose):
    def say(*a):
        if verbose:
            print(*a)

    with os88marty.launch(image, machine=machine, boot=False) as m:
        # --- 1. two cards ---------------------------------------------------
        cards = m.cards()
        say("cards:", [(c["idx"], c["type"]) for c in cards])
        if len(cards) != 2:
            raise Fail("expected 2 video cards, got %d - does this MartyPC "
                       "build keep both [[machine.video]] entries?" % len(cards))
        for c in cards:
            if c["type"] not in CARDS:
                raise Fail("card %d is a %r, which this gate has no entry for "
                           "in CARDS" % (c["idx"], c["type"]))
        # A and B rather than herc/cga, because the pair is now whatever the
        # machine holds: Hercules+CGA on the 5150, VGA+Hercules on the XT.
        (a_i, a_k), (b_i, b_k) = [(c["idx"], c["type"]) for c in cards]
        A, B = CARDS[a_k], CARDS[b_k]
        if A["base"] == B["base"]:
            raise Fail("both cards are tested through %05X - this gate cannot "
                       "tell them apart by aperture" % A["base"])
        say("  %s = card %d, %s = card %d" % (A["label"], a_i, B["label"], b_i))

        # POST first, THEN park - a self-programming card needs its own option
        # ROM to have run, and parking is what stops it running (see warm_up).
        warm_up(m, [i for i, k in ((a_i, a_k), (b_i, b_k))
                    if CARDS[k]["selfprog"]], say)
        park_cpu(m, A.get("prologue", b"") + B.get("prologue", b""))

        A["program"](m)
        B["program"](m)
        m.cmd(cmd="advance", frames=4, card=a_i)

        def settle():
            m.cmd(cmd="advance", frames=4, card=a_i)
            m.cmd(cmd="advance", frames=4, card=b_i)
            return lit(m, a_i), lit(m, b_i)

        def clear():
            m.write(A["base"], bytes(A["size"]))
            m.write(B["base"], bytes(B["size"]))
            return settle()

        # --- 2. the WEAK test, run so its weakness is on the record ---------
        m.write(A["base"], bytes([0x11, 0x22, 0x33, 0x44]))
        m.write(B["base"], bytes([0xAA, 0xBB, 0xCC, 0xDD]))
        ra = m.read(A["base"], 4)
        rb = m.read(B["base"], 4)
        say("weak test: %05X=%s %05X=%s"
            % (A["base"], ra.hex(), B["base"], rb.hex()))
        if ra != bytes([0x11, 0x22, 0x33, 0x44]) or rb != bytes([0xAA, 0xBB, 0xCC, 0xDD]):
            raise Fail("the two apertures do not even hold their own bytes")
        say("  (passes on an ALIASED machine too - proves nothing on its own)")

        # --- 3. THE TEST: one card's memory drives one card's raster --------
        # Clear both framebuffers and both rasters, then light each card in
        # turn and require the OTHER one not to move.
        base = clear()
        say("cleared:  %s lit %d, %s lit %d" % (A["label"], base[0], B["label"], base[1]))

        for one, other, oi in ((A, B, 1), (B, A, 0)):
            prev = clear()
            m.write(one["base"], one["lit"](one["size"]))
            now = settle()
            mine, theirs = now[1 - oi], now[oi]
            was_mine, was_theirs = prev[1 - oi], prev[oi]
            say("%s lit: %s %d (+%d), %s %d (+%d)"
                % (one["label"], one["label"], mine, mine - was_mine,
                   other["label"], theirs, theirs - was_theirs))
            if mine <= was_mine:
                raise Fail("writing the %s card's memory did not change the %s "
                           "card's raster - is it programmed, or is it not "
                           "rasterising?" % (one["label"], one["label"]))
            if theirs != was_theirs:
                raise Fail("writing the %s card's memory changed the %s card's "
                           "raster by %d pixels - the two cards share memory "
                           "(patch 02 missing, or the [[machine.video]] order "
                           "changed)"
                           % (one["label"], other["label"], theirs - was_theirs))

        # --- 4. the two rasters are the two GEOMETRIES ----------------------
        # A machine that answered both of the above out of one card would also
        # answer them at one geometry.
        aw, ah, _ = m.fbuf(card=a_i)
        bw, bh, _ = m.fbuf(card=b_i)
        say("geometry: %s %dx%d, %s %dx%d" % (A["label"], aw, ah, B["label"], bw, bh))
        if (aw, ah) == (bw, bh):
            raise Fail("both cards report one geometry (%dx%d)" % (aw, ah))

        # --- 5. both cards keep their OWN clock -----------------------------
        # 50Hz Hercules against 60Hz CGA, and the VGA's own rate against the
        # Hercules. A capture paced on the wrong card's counter is what the
        # `card=` argument exists to prevent.
        f0 = {c["idx"]: c["frames"] for c in m.cards()}
        m.cmd(cmd="advance", frames=30, card=a_i)
        f1 = {c["idx"]: c["frames"] for c in m.cards()}
        da, db = f1[a_i] - f0[a_i], f1[b_i] - f0[b_i]
        say("30 %s frames = %d %s frames" % (A["label"], db, B["label"]))
        if da < 30:
            raise Fail("advance(frames=30) moved the %s %d frames"
                       % (A["label"], da))
        if db == 0:
            raise Fail("the %s card's frame counter did not move at all"
                       % B["label"])
        if da == db:
            raise Fail("both cards report the same frame count over the same "
                       "interval - one counter is being read for both")

        # There is deliberately no sixth check asking whether the two apertures
        # are two MEMORIES. It was written, and it is redundant: each card is
        # driven through the aperture os8088 itself uses - A0000 in mode 12h,
        # B0000 on the Hercules, B8000 on the CGA - so checks 2 and 3 already
        # ask that pair, and check 3 asks it of the rasters, which is the
        # stronger question.
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
