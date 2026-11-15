#!/usr/bin/env python3
"""WireArena on a Hercules: the match is drawn in MONOCHROME attributes
(apps/wirearena/wrtab.inc's wa_attrm), never the colour table's.

An MDA-class text page reads an attribute's low bits as UNDERLINE and
intensity, not as a colour: the colour table's walls (01h) come out
underlined, its dots (08h) as nothing at all. So wa_setmode decides mono
once a mode is set, and everything downstream - the renderer part's
attribute table, the sprites, the HUD - follows [wa_mono].

It decided by FSI_SEG == B000h. On kern_mega that word is a SELECTOR (fsx.inc
writes `VSEG ax, 0xB000`, KERN-MEGA-MEMORY-PLAN MC7.4), never B000h, so a
286 with a Hercules played in the colour table. The test is now the
display's kind (OSAPI_FSX_CAPS's DL = VID_HERC), the fact fsx_mode's own
B000h fixup keys on.

  - [wa_mono] is 1 once the match is up;
  - THE PAGE: in 80x25 at B000h, no glyph carries an attribute only the
    colour table has (01, 02, 03, 04, 08, 0B, 0C, 0E), and no cell at all
    is underlined (01h's low bits, which an MDA draws under a blank too);
  - and the frame is really there: a few hundred cells not blank.

Registered as wamono in soak (MartyPC's 5150 with a Hercules, kern_big), and
as mega-wamono in tests/megaport.txt on 86Box's Hercules (kern_mega), which
is the leg that went red: [wa_mono] 0 and the walls in 01h, on the build
before the fix. make wirearenadisk first.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'tests')]
import os88marty                                                # noqa: E402
import os88ui                                                   # noqa: E402
from os88fixture import need                                    # noqa: E402
from walift import amap, start, SYS, APPS                       # noqa: E402

# the colour table's attributes the mono one has not got (wrtab.inc)
COLOUR = {0x01, 0x02, 0x03, 0x04, 0x08, 0x0B, 0x0C, 0x0E}
PAGE = 0xB0000                  # the Hercules's text page 0 (80x25 words)


def main():
    need(APPS)
    sym = amap()
    fails = []
    with os88ui.boot(SYS, apps=APPS, machine="os8088_5150_herc",
                     quick=True) as ui:
        m = ui.m
        w = ui.path("B:/WIRERENA.O88")
        p = start(ui, w, sym, w40=False)   # (40 is refused on a Hercules)
        f0 = p.byte(sym["wa_frame"])
        os88marty.until(m, lambda _: p.byte(sym["wa_frame"]) != f0,
                        "the match's first frames", guest=20)
        frame = p.byte(sym["wa_frame"])
        os88marty.until(m, lambda _: p.byte(sym["wa_frame"]) != frame,
                        "a frame after the first", guest=10)
        m.pause()
        mono = p.byte(sym["wa_mono"])
        page = m.read(PAGE, 80 * 25 * 2)
        attrs = {}
        bad = {}
        lit = 0
        for i in range(0, len(page), 2):
            c, a = page[i], page[i + 1]
            attrs[a] = attrs.get(a, 0) + 1
            blank = c in (0x00, 0x20, 0xFF)
            if not blank and a & 0x77:
                lit += 1
            # what SHOWS wrongly: a glyph in a colour-only attribute, or an
            # underline (attribute 01h's low bits), which an MDA draws under
            # a blank as well. A blank in 08h shows nothing either way (the
            # HUD's last five cells)
            if a in COLOUR and (not blank or a & 0x77 == 0x01):
                bad["%02X" % a] = bad.get("%02X" % a, 0) + 1
        seen = ", ".join("%02X x%d" % (a, n) for a, n in sorted(attrs.items()))
        print("  [wa_mono] %d; attributes %s; %d cells lit" % (mono, seen, lit))
        if mono != 1:
            fails.append("[wa_mono] is %d on a Hercules - the colour table "
                         "(wa_setmode's mono test)" % mono)
        if bad:
            fails.append("the page carries colour-only attributes %s: the "
                         "walls underlined, the dots dark" % bad)
        if lit < 200:
            fails.append("only %d cells lit at B000h - no frame there to "
                         "judge" % lit)
    if fails:
        for f in fails:
            print("  FAIL " + f)
        sys.exit("wamono: %d failure(s)" % len(fails))
    print("wamono: ok - the match in monochrome attributes on a Hercules")


if __name__ == "__main__":
    main()
