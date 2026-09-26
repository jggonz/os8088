#!/usr/bin/env python3
"""deskfdd - is a floppy drawn as the diskette its DRIVE takes? (SPEC.md 26.4.1)

A 5.25" drive gets the 5.25" picture (ico_f525_32 / ico_f525_14) and a 3.5"
one keeps ico_disk32 / ico_disk14. Three machines, each asking a different
half of the design:

  cga5150   GLaBIOS 5150, CGA, two 360KB drives. The ROM refuses int 13h
            AH=08h, so both drives are GUESSED 5.25" - and the picture is the
            14-row one.
  herc5150  the same on Hercules: the 32-row 5.25" picture.
  herc144   GLaBIOS with two 1.44MB drives. It refuses AH=08h too (measured:
            CF=1, AH=01h), so both are guessed 5.25" at desk_init - and then
            drv_boot's mount of A: reads an 18-sector BPB and desk_learn_x
            corrects A: to 3.5" BEFORE THE FIRST PAINT. B: stays the guess
            until a disk is read in it; the row opens B: and requires the zone
            to be REPAINTED as a 3.5" (desk_zmark_x), then closes the window
            so the zone is on the glass again.

For every zone it asserts the row's DV_FLAGS bits (guest state, exact) AND the
pixels under the icon, against an INDEPENDENT decode of the record the flags
select - SPEC.md 25.7's run format and 25.7.3's five-bit index, read out of the
guest's own ico_pool and the record bytes, in Python. That is the
half tests/icoclip.py cannot see: it proves a clipped icon equals the
unclipped one, which is exactly as true when the decoder reads the WRONG ROW.
Break the index arithmetic in icon_draw_ix and this row goes red; break
desk_learn_x and herc144 goes red on A:.

Only pixels INSIDE the icon's mask are compared: outside it is desktop dither.
A selected zone is XOR-highlighted (desk_zone_hilite), so [desk_sel] is read
and the expectation inverted for that zone.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88geom as geom                                     # noqa: E402
import os88sym                                              # noqa: E402
import os88ui                                               # noqa: E402

DV_SIZE, DV_KIND, DV_FLAGS = 16, 0, 2
DVF_ZONE, DVF_525, DVF_GUESS = 1, 2, 4
DESK_ZY0, DESK_COLW = 32, 44


def say(*a):
    print(*a, flush=True)


def word(m, name):
    b = m.read(os88sym.linear(name), 2)
    return b[0] | b[1] << 8


def decode(m, recname):
    """SPEC.md 25.7/25.7.3, independently: (mask rows, data rows) as ints.

    A record is its height and then one byte a run, `pool row << 3 | count
    - 1`, over ico_pool's four-byte rows."""
    h = m.read(os88sym.linear(recname), 1)[0]
    if not 1 <= h <= 32:
        raise SystemExit("deskfdd: %s reads as %d rows - not an indexed record"
                         % (recname, h))
    pools = m.read(os88sym.linear("ico_pool"), 32 * 4)
    runs = m.read(os88sym.linear(recname) + 1, 2 * h)   # never more than that
    rows, i = [], 0
    while len(rows) < 2 * h:
        b = runs[i]
        i += 1
        off = (b >> 3) * 4
        w0 = pools[off] | pools[off + 1] << 8
        w1 = pools[off + 2] | pools[off + 3] << 8
        rows += [(w0 << 16) | w1] * ((b & 7) + 1)
    if len(rows) != 2 * h:
        raise SystemExit("deskfdd: %s's runs overshoot its height" % recname)
    return rows[:h], rows[h:]


def check_zone(ui, letter, want_525, want_guess, fails):
    m = ui.m
    v = ord(letter) - ord("A")
    row = m.read(os88sym.linear("dsk_vtab") + v * DV_SIZE, DV_SIZE)
    fl = row[DV_FLAGS]
    got_525, got_guess = bool(fl & DVF_525), bool(fl & DVF_GUESS)
    tag = "%s: flags %02X" % (letter, fl)
    if (got_525, got_guess) != (want_525, want_guess):
        fails.append("%s - DVF_525 %d DVF_GUESS %d, wanted %d %d"
                     % (tag, got_525, got_guess, want_525, want_guess))
    # ...and the picture, off what the FLAGS say, so a row whose flags are
    # right and whose drawing ignores them still fails here
    tall = word(m, "desk_zh1") > 30
    rec = ("ico_f525_" if got_525 else "ico_disk") + ("32" if tall else "14")
    mask, data = decode(m, rec)
    ordinal = geom.drive_ordinal(m, letter)
    rows_per = word(m, "desk_rows")
    col, r = divmod(ordinal, rows_per)
    x0 = word(m, "vid_desk_zx") - col * DESK_COLW
    y0 = DESK_ZY0 + r * word(m, "desk_zstep")
    sel = m.read(os88sym.linear("desk_sel"), 1)[0] == v
    w, h, fb = m.vram()
    bad = []
    for y in range(len(mask)):
        for x in range(32):
            bit = 1 << (31 - x)
            if not mask[y] & bit and not data[y] & bit:
                continue                        # desktop shows through
            want = 0 if data[y] & bit else 1    # data is black, mask white
            if sel:
                want ^= 1
            got = fb[y0 + y][x0 + x]
            if got != want:
                bad.append((x, y))
    if bad:
        fails.append("%s - %d of the icon's pixels are not %s's (first at "
                     "icon x=%d y=%d, screen %d,%d%s)"
                     % (tag, len(bad), rec, bad[0][0], bad[0][1],
                        x0 + bad[0][0], y0 + bad[0][1],
                        ", zone SELECTED" if sel else ""))
    say("  %-8s %s -> %-12s %s" % (letter + ":", tag, rec,
                                   "ok" if not bad else "PIXELS WRONG"))


def machine(name, img, apps, want, fails, learn_b=False):
    say("%s (%s, %s)" % (name, img, apps))
    with os88ui.boot(img, apps=apps, machine=name, verbose=False) as ui:
        for letter, (w525, wguess) in want.items():
            check_zone(ui, letter, w525, wguess, fails)
        if learn_b:
            # B:'s first mount is what settles it. open_drive CONFIRMS the
            # listing came from a good mount (FS_MOK), which is after
            # dsk_bpb_check and therefore after desk_learn_x
            win = ui.open_drive("B")
            ui.close(win)
            ui.settle()
            say("  ...after B:'s first mount and the window closed:")
            check_zone(ui, "B", False, True, fails)


def main():
    fails = []
    machine("os8088_5150_cga", "build/os8088-360.img", "build/apps360.img",
            {"A": (True, True), "B": (True, True)}, fails)
    machine("os8088_5150_herc", "build/os8088-360.img", "build/apps360.img",
            {"A": (True, True), "B": (True, True)}, fails)
    machine("os8088_5150_herc_gla_144", "build/os8088.img", "build/apps.img",
            {"A": (False, True), "B": (True, True)}, fails, learn_b=True)
    for f in fails:
        say("  FAIL: " + f)
    say("deskfdd: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
