#!/usr/bin/env python3
"""EVERY CONVERTED BUTTON, DRIVEN (SPEC.md 13.7, 13.8, 20.5.1.3).

One package per row: open it, find a live button from its own record, press
and HOLD, slide OFF, and read the record back at each edge.  Four assertions
apiece and none of them is a screenshot, because a press-fired button and a
release-fired one photograph identically.

IT EXISTS BECAUSE READING THE CODE WAS NOT ENOUGH.  Four packages shipped
broken in a row and every one was found by a person looking at a screen:

  * DOS      - record aimed in the CLICK path, so a paint ran with N = 0 and
               the buttons were absent;
  * BROWSER  - the record declared ON TOP of the package's own state words
               (`br_btrec equ br_r3 + 8` beside `br_spen equ br_r3 + 8`), so
               a press filled a garbage rectangle over half the screen;
  * ARTFUL   - N written AFTER the draws, so every paint used the previous
               count and the first used zero;
  * AUDIO    - the press tested in content-relative coordinates against rects
               held in SCREEN ones, so nothing ever armed.

Every one of those is visible in BT_DOWN and BT_N at a known moment, which is
what this reads.  t_btnrules.py is the static half and catches a new caller;
this catches a caller that is wired up wrong.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, ROOT + "/tools")
import os88ui                                              # noqa: E402
import os88marty                                           # noqa: E402
import os88geom                                            # noqa: E402


def u16(b):
    return b[0] | (b[1] << 8)


def pkg_syms(src, inc, defines=()):
    """A package's symbols, by re-assembling it with a map.  THE DEFINES MUST
    MATCH THE SHIPPED BUILD - the DOS box on the system disk is dosp.bin, and
    a map taken without -DDOSKPART resolves to plausible, wrong offsets."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "t.asm"), os.path.join(d, "t.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        cmd = ["nasm", "-f", "bin", "-w+error"]
        for i in inc:
            cmd += ["-I", ROOT + i]
        cmd += list(defines) + ["-o", os.path.join(d, "t.bin"), cp]
        subprocess.run(cmd, check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


# path, record symbol, source, include dirs, defines, drive-the-gesture?
# The press lands on the CENTRE OF BUTTON 0'S OWN RECT, read out of the
# record - the record is the single description of where the control is, so a
# test that aimed at a remembered coordinate would be a second one, which is
# the very drift this control exists to end (docs/WRITING-TESTS.md).
# Telnet is False only because tests/btngesture.py already drives it in
# depth; every other row is driven here.
ROWS = [
    ("A:/APPS/DOS.O88", "dos_btrec", "/apps/dos/dos.asm",
     ["/apps/", "/apps/dos/", "/drivers/net/", "/kerndos/"],
     ("-DDOSKPART", "-DDOS_EXTCORE"), (556, 26)),
    ("A:/APPS/TELNET.O88", "te_btrec", "/apps/telnet/telnet.asm",
     ["/apps/", "/apps/telnet/", "/drivers/net/"], (), False),
    ("A:/APPS/BROWSER.O88", "br_btrec", "/apps/browser/browser.asm",
     ["/apps/", "/apps/browser/", "/drivers/net/"], (), True),
    ("B:/APPS/AUDIO.O88", "apu_btrec", "/apps/audio/audio.asm",
     ["/apps/", "/apps/audio/"], (), True),
    # ARTFUL's SPLASH - New and Open. It is here because converting the modal
    # and leaving the splash on its own at_btnhit ladder is exactly the miss
    # that shipped: buttons that DREW through the record and still fired on
    # the press, which no static check can tell from a correct one.
    ("B:/APPS/ARTFUL.O88", "at_btrec", "/apps/artful/artful.asm",
     ["/apps/", "/apps/artful/"], (), True),
]


def run():
    fails = []
    with os88ui.boot(ROOT + "/build/os8088-360.img",
                     apps=ROOT + "/build/apps360.img") as ui:
        m, mo = ui.m, ui.mo
        for path, rec, src, inc, defs, at in ROWS:
            name = path.rsplit("/", 1)[-1]
            syms = pkg_syms(ROOT + src, inc, defs)
            w = ui.path(path)
            ui.settle()
            seg = u16(m.read(os88geom.winptr(m, w.i, ui.sym) + os88geom.W_SEG, 2))
            base = syms[rec]
            rd = lambda off: u16(m.readseg(seg, base + off, 2))

            # 1. the record is AIMED after a plain paint - DOS's bug, and
            #    Artful's: a rect pointer of 0 or a live count of 0 means
            #    os88ui_btn draws nothing at all.
            n, rects = rd(6), rd(0)
            if n == 0 or rects == 0:
                fails.append("%s: after a PAINT the record is not aimed "
                             "(BT_N=%d BT_RECTS=%d) - the buttons cannot be "
                             "drawn at all" % (name, n, rects))
                try:
                    ui.menu_pick(w.title, "Close")
                except Exception:
                    m.key("Escape"); os88marty.settle(m)
                    ui.menu_pick(w.title, "Close")
                ui.settle()
                continue

            # 2. the rects are inside the window - Browser's bug, where the
            #    record aliased other state and named a garbage rectangle.
            wr = os88geom.win_rect(m, w.i, ui.sym)
            r0 = [u16(m.readseg(seg, rects + i * 2, 2)) for i in range(4)]
            if not (wr[0] - 4 <= r0[0] <= wr[2] and wr[1] - 4 <= r0[1] <= wr[3]
                    and r0[2] >= r0[0] and r0[3] >= r0[1]
                    and r0[2] - r0[0] < 400 and r0[3] - r0[1] < 200):
                fails.append("%s: button 0's rect %s is not a sane rectangle "
                             "inside the window %s - a record that overlaps "
                             "other state reads like this" % (name, r0, list(wr)))

            def dark():
                """dark pixels inside button 0 - THE GLASS, not the record.

                BT_DOWN is what the library thinks; this is what the user
                sees. They came apart once already: every button on the
                Control Panel drew through os88ui_btn_f, whose contract is a
                RECT, and the record-based entry read a live count out of a
                rectangle's coordinates - the state was perfect and the page
                was white."""
                fw, fh, d = m.fbuf(0)
                n = 0
                for y in range(max(0, r0[1] + 2), min(fh, r0[3] - 1)):
                    for x in range(max(0, r0[0] + 2), min(fw, r0[2] - 1)):
                        i = (y * fw + x) * 3
                        if d[i:i + 3] == b"\x00\x00\x00":
                            n += 1
                return n

            if at is not False:
                cx, cy = (r0[0] + r0[2]) // 2, (r0[1] + r0[3]) // 2
                up0 = dark()
                mo.to(cx, cy); os88marty.settle(m)
                mo._edge(True); m.advance(frames=10); m.run()
                held = rd(10)                              # BT_DOWN
                dn = dark()
                if dn <= up0:
                    fails.append("%s: held, the button is not INVERTED on the "
                                 "glass (%d dark pixels against %d upright) - "
                                 "the record may say it is down and the "
                                 "screen is what the user sees"
                                 % (name, dn, up0))
                # 3. the press ARMS - Audio's bug, where the hit test ran in
                #    the wrong coordinate space and nothing ever armed.
                if held == 0:
                    fails.append("%s: pressing button 0 at its OWN centre "
                                 "%s armed NOTHING (BT_DOWN=0) - the press is "
                                 "being hit-tested against rects it does not "
                                 "match" % (name, (cx, cy)))
                # 4. sliding off UN-arms it (SPEC.md 13.8.1's cancel)
                # ABOVE the button, not below it: a clamp to the window's
                # bottom edge can land back INSIDE a control near the foot of
                # the content, which reads exactly like a gesture that cannot
                # be cancelled. Artful's splash row failed this way and the
                # product was correct.
                off_y = max(wr[1] + 2, r0[1] - 10)
                mo.to(cx, off_y, l=True)
                m.advance(frames=30); m.run()
                off = rd(10)
                offdark = dark()
                if held and abs(offdark - up0) > max(8, up0 // 4):
                    fails.append("%s: the pointer slid OFF and the button is "
                                 "still inverted on the GLASS (%d dark pixels, "
                                 "upright was %d) - SPEC.md 13.8.1's cancel is "
                                 "what tells the user the gesture is off"
                                 % (name, offdark, up0))
                if held and off != 0:
                    fails.append("%s: the pointer slid OFF the button and it "
                                 "is still down (BT_DOWN=%d) - the gesture "
                                 "cannot be cancelled" % (name, off))
                mo._edge(False); os88marty.settle(m)
            print("  %-14s N=%d rect0=%s%s"
                  % (name, n, r0,
                     "" if at is False
                     else " up=%d held=%d off=%d ok" % (up0, dn, offdark)))
            # Esc first: a package whose button DID fire may have taken the
            # whole screen (Artful's New does), and a menu pick cannot reach a
            # window that is not there any more.
            try:
                ui.menu_pick(w.title, "Close")
            except Exception:
                m.key("Escape")
                os88marty.settle(m)
                ui.menu_pick(w.title, "Close")
            ui.settle()

    for f in fails:
        print("FAIL " + f)
    print("btnall: %d row(s), %d failure(s)" % (len(ROWS), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(run())
