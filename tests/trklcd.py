#!/usr/bin/env python3
"""trklcd - the XT visualiser button and spectrum, the 286 face's markers, and
the LCD drawn by its inputs (SPEC.md 45.23.1, 45.24.1, 45.21.8)

    make trkrate && python3 tests/trklcd.py

On a 5150 with XT mode pre-armed (tier 0, SPEC.md 45.9), playing BEVERLY.MOD
at 5.5 kHz:

  THE BUTTON (45.23.1) - the forced XT meter no longer greys it:
    1. live at 5.5 kHz, and clicks go VU Meter -> Spectrum -> Off -> VU
    2. R to 11 kHz forces none and GREYS it, and a click there does nothing
    3. R back makes it live again

  THE XT SPECTRUM (45.24.1) - bars only, TW_XTNB of them, decayed by the
  clock and held at the top, and it KEEPS UP: 17 frames a second or better
  (it reads 17.8-17.9) with the ring never under half full (TRK_DEEP, where
  the worker turns to mixing first - at the 2-tick hold it touches 4,096 and
  stops there, in every run). The 286's 16-band spectrum with markers, forced
  onto the XT, reads 15.0 and a ring down to 2,048, so either threshold alone
  catches that regression.

  THE ABOUT CARD, HELD (45.21.9) - 13 guest seconds with every frame dropped,
  past both 16-bit windows at 5.5 kHz: the clock must move by the guest time,
  within a second. The build before this reads +1.3 s against +23.8.

  THE 286 FACE'S MARKERS (45.24.1) - reached on the 8088 by [trk_cpu0] = 0 and
  XT mode off: a bar HELD low must let its peak marker fall to it. The first
  build skipped a held band's whole step, marker included, and leaves the
  staged marker at 60 where this wants 16.

  THE LCD (45.21.8) - a line is composed only when its KEY moves, and then
  only the cells that differ from its shadow are lettered:
    4. compositions run at about one a second (the clock), not one per line
       per frame - the old face composed ~60 a second
    5. THE GLASS IS RIGHT. The emulator is stopped mid-song, the LCD and the
       status line are read, a full repaint is forced, and the same pixels
       are read again once the frame that took it has FINISHED drawing: the
       diff-drawn glass must equal the fresh one, pixel for pixel. A build
       that letters the changed span one cell short fails this with 182
       pixels wrong; a check that grabs before the repaint finishes passes
       it, which is why the wait is on [tw_inframe] and not on [tw_dirty]
       (tw_update clears that BEFORE it draws, and a full LCD is a large part
       of a second on an 8088).

Stopping the guest makes the comparison exact, and a line whose key moved
between the two reads (a second ticked) makes the attempt retry rather than
count - the keys are read beside the pixels.

It wants a Sound Blaster, which in a container means os8088_5150_herc_sb_gla
(Hercules, as the owner's 5150 is).
"""
import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.chdir(ROOT)
import os88marty, os88mouse, os88sym, dispcp, os88rate           # noqa: E402
from os88fixture import need                                      # noqa: E402
from os88rate import symbols, scan                                # noqa: E402

DISK = "build/trkship360.img"          # the SHIPPED player
MACHINE = "os8088_5150_herc_sb_gla"
HZ = 4772728.0
VIZ = 8 + 4                            # TW_NTB + the TWO_VIZ option slot
XTNB = int(re.search(r"^TW_XTNB\s+equ\s+(\d+)",          # read, not restated
                     open(os.path.join(ROOT, "apps/tracker/trkwin.inc")).read(),
                     re.M).group(1))
fails = []


def check(name, got, want):
    ok = got == want
    print("  %-52s %-10s %s" % (name, got, "ok" if ok else "FAIL, want %s" % (want,)))
    if not ok:
        fails.append(name)


def immediates(lst):
    """The TRKBUF names only ever used as IMMEDIATES (`mov di, tw_rects_b`),
    which os88rate.symbols() cannot see - it scrapes [name] operands."""
    out = {}
    pats = [(r"BF\[([0-9A-F]{2})([0-9A-F]{2})\]\s+(?:<\d+>)?\s*mov di, (tw_rects_b|tw_flags_b)\s*$", 3),
            (r"81C7\[([0-9A-F]{2})([0-9A-F]{2})\]\s+(?:<\d+>)?\s*add di, (tw_keys)\b", 3),
            (r"BF\[([0-9A-F]{2})([0-9A-F]{2})\]\s+(?:<\d+>)?\s*mov di, (tw_band)\s", 3)]
    for L in open(lst):
        for pat, g in pats:
            mo = re.search(pat, L)
            if mo:
                out.setdefault(mo.group(g), int(mo.group(2) + mo.group(1), 16))
    return out


def main():
    # A PRIVATE listing: os88rate's default is one fixed /tmp path, and two
    # instances started together read each other's - every bss read then
    # lands on the wrong word and reads as "XT mode is not armed".
    fd, os88rate.LST = tempfile.mkstemp(suffix=".lst", prefix="trklcd")
    os.close(fd)
    try:
        P, _ = symbols(())
        imm = immediates(os88rate.LST)
    finally:
        os.unlink(os88rate.LST)
    for n in ("tw_rects_b", "tw_flags_b", "tw_keys", "tw_band"):
        if n not in imm:
            print("FAIL: no address for %s in the listing" % n)
            return 1
    S = os88sym.linear
    need(DISK)                     # `all` builds nothing under tests/
    with os88marty.launch("build/os8088-360.img", apps=DISK,
                          machine=MACHINE, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        slot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, slot)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "BEVERLY.MOD")
        try:                           # the launch is the GUEST's work
            os88marty.until(m, lambda mm: scan(mm)[0],
                            "Tracker's instance", poll=0.5, limit=120.0)
        except os88marty.MartyError:
            pass
        seg, _drv = scan(m)
        if not seg:
            print("FAIL: Tracker never loaded")
            return 1
        base = seg * 16
        b = lambda n: m.read(base + P["@" + n], 1)[0]
        wv = lambda a: int.from_bytes(m.read(base + a, 2), "little")

        def guest(secs):               # GUEST time: a loaded host cannot
            m.advance(cycles=int(secs * HZ))   # shorten it
            m.run()

        for _ in range(60):            # 116KB of module off a 360KB floppy
            if b("mp_loaded"):
                break
            guest(1.0)
        flags = lambda: wv(imm["tw_flags_b"] + VIZ * 2)

        def click_btn(i):
            a = imm["tw_rects_b"] + i * 8
            x1, y1, x2, y2 = [wv(a + k) for k in (0, 2, 4, 6)]
            mo.click((x1 + x2) // 2, (y1 + y2) // 2)
            guest(1.0)

        def click():
            a = imm["tw_rects_b"] + VIZ * 8
            x1, y1, x2, y2 = [wv(a + i) for i in (0, 2, 4, 6)]
            mo.click((x1 + x2) // 2, (y1 + y2) // 2)
            guest(1.0)

        def rate(off, secs=6.0):
            m.bp_exec(base + off)
            n, c0 = 0, m.status()["cycles"]
            while True:
                m.run()
                if m.wait_stop(10.0) is None:
                    break
                if m.status()["cycles"] - c0 > secs * HZ:
                    break
                n += 1
            span = (m.status()["cycles"] - c0) / HZ
            m.bp_exec()
            m.run()
            return n / span

        print("the button (SPEC.md 45.23.1)")
        check("XT mode armed (mp_xt)", b("mp_xt"), 1)
        check("5.5 kHz (trk_xhi)", b("trk_xhi"), 0)
        m.key("Enter")
        guest(8.0)
        check("drawn: the thin XT meter (tw_vizm)", b("tw_vizm"), 4)
        check("the button is live", flags() & 1, 0)
        click()
        check("click: Spectrum picked and drawn", (b("tw_viz"), b("tw_vizm")), (1, 1))
        check("...bars only (tw_mk), TW_XTNB of them",
              (b("tw_mk"), b("tw_nb")), (0, XTNB))
        ups = rate(P["tw_update"])
        # THE RING, not a byte count: [trk_consumed] moves in whole BLOCKS,
        # so a short window reads 93% or 102% of a perfect stream. A lead
        # that never reaches zero is the music reaching the card whole.
        c0 = m.status()["cycles"]
        heard, lead = 0, []
        last = wv(P["@trk_consumed"])
        for _ in range(64):
            guest(0.25)
            now = wv(P["@trk_consumed"])
            heard += (now - last) & 0xFFFF
            last = now
            lead.append((wv(P["@trk_total"]) - now) & 0xFFFF)
        secs = (m.status()["cycles"] - c0) / HZ
        audible = 100.0 * heard / secs / wv(P["@mp_mixrate"])
        print("  XT spectrum: %.1f frames/s, %.1f%% heard, ring lead min %d"
              % (ups, audible, min(lead)))
        check("...at 17 frames a second or better", ups >= 17.0, True)
        check("...the ring stays half full (min lead >= 4096)", min(lead) >= 4096, True)
        check("...and the byte count agrees (>= 95%)", audible >= 95.0, True)
        click()
        check("click: Off picked and drawn", (b("tw_viz"), b("tw_vizm")), (3, 3))
        check("...and the button still live", flags() & 1, 0)
        click()
        check("click: VU Meter again", (b("tw_viz"), b("tw_vizm")), (0, 4))
        m.key("KeyR")
        guest(2.0)
        check("R: 11 kHz, and nothing drawn", (b("trk_xhi"), b("tw_vizm")), (1, 3))
        check("...and the button GREYED", flags() & 1, 1)
        click()
        check("a click on it changes nothing", b("tw_viz"), 0)
        m.key("KeyR")
        guest(2.0)
        check("R back: 5.5 kHz, the meter, live",
              (b("trk_xhi"), b("tw_vizm"), flags() & 1), (0, 4, 0))
        if not b("mp_playing"):
            m.key("Enter")             # R stops the stream to reopen it
            guest(6.0)

        print("the LCD (SPEC.md 45.21.8)")
        ups = rate(P["tw_update"])
        comps = rate(P["tw_lcd_line"])
        print("  frames %.1f/s, LCD compositions %.2f/s" % (ups, comps))
        check("compositions at most ~2 a second", comps <= 2.5, True)
        check("...while frames run at over 10 a second", ups > 10.0, True)
        guest(20.0)                    # the clock and the position move

        ox, oy = wv(P["@tw_ox"]), wv(P["@tw_oy"])
        rows = list(range(oy + 4, oy + 50)) + list(range(oy + 172, oy + 180))

        def grab():
            _w, _h, px = m.vram("herc")
            return [bytes(px[y][ox + 4:ox + 412]) for y in rows]

        keys = lambda: m.read(base + imm["tw_keys"], 80)
        for attempt in range(8):
            m.pause()
            a, k1 = grab(), keys()
            m.write(base + P["@tw_dirty"], bytes([b("tw_dirty") | 1]))
            for _ in range(100):
                m.advance(cycles=int(0.02 * HZ))
                if b("tw_dirty") & 1 == 0:
                    break
            for _ in range(200):       # the frame that took it has FINISHED
                if b("tw_inframe") == 0:
                    break
                m.advance(cycles=int(0.02 * HZ))
            took = b("tw_dirty") & 1 == 0 and b("tw_inframe") == 0
            c, k2 = grab(), keys()
            m.run()
            if not took:
                check("the forced full repaint ran to the end", took, True)
                break
            if k1 != k2:
                print("  (a line's inputs moved on attempt %d - again)" % attempt)
                guest(0.7)
                continue
            lit = sum(sum(r) for r in a)
            ndiff = sum(sum(1 for p, q in zip(ra, rc) if p != q)
                        for ra, rc in zip(a, c))
            check("the LCD has text on it (%d px lit)" % lit, lit > 1000, True)
            check("diff-drawn LCD + status == a full repaint (px)", ndiff, 0)
            break
        else:
            check("found a moment with no line's inputs moving", False, True)

        # A FROZEN FACE KEEPS ITS BOOKS (45.21.9). The About card drops every
        # frame, and a frame was the only thing that asked the card where it
        # was or moved the clock - both through a 16-bit difference. Held past
        # 64 KB of music (11.9 s at 5.5 kHz) the clock lost exactly that for
        # the rest of the song, and between 32 and 64 KB the position was
        # refused as "going backwards" and the face stayed on the old row
        # after the card came down. Held 13 guest seconds here, which is past
        # both, and the clock must have moved by the guest time that passed.
        print("the About card, held (SPEC.md 45.21.9)")
        rate_hz = wv(P["@mp_mixrate"])
        el = lambda: (int.from_bytes(m.read(base + P["@tw_el"], 4), "little"))
        m.pause()
        el0, c0 = el(), m.status()["cycles"]
        m.run()
        click_btn(15)                  # About
        check("the card is up", b("trk_abon"), 1)
        guest(13.0)
        m.key("KeyQ")                  # any key takes it down
        guest(2.0)
        m.pause()
        el1, c1 = el(), m.status()["cycles"]
        m.run()
        heard = (el1 - el0) / float(rate_hz)
        wall = (c1 - c0) / HZ
        print("  across the card: clock +%.1f s, guest +%.1f s" % (heard, wall))
        check("the card is down", b("trk_abon"), 0)
        check("...and the clock kept the music's time (within 1 s)",
              abs(heard - wall) <= 1.0, True)

        # THE 286 FACE'S MARKERS (45.24.1), on the 8088: [trk_cpu0] = 0 and XT
        # mode off is the face a 286 gets. A bar HELD low under a high marker
        # - a neighbour band kicked at half level every row - must let the
        # marker fall to the bar. The hold used to skip the band's whole step,
        # marker included, so it stood frozen at the old peak for as long as
        # the neighbour kept playing. Staged with playback stopped, so no kick
        # moves anything but the decay under test.
        print("the 286 face's markers (SPEC.md 45.24.1)")
        m.write(base + P["@trk_cpu0"], b"\0")
        m.key("KeyX")                  # stops, and takes XT mode off
        guest(3.0)
        check("XT mode off, stopped", (b("mp_xt"), b("mp_playing")), (0, 0))
        for _ in range(4):
            if b("tw_vizm") == 1:
                break
            click()
        check("the 286 spectrum: 16 bands with markers",
              (b("tw_vizm"), b("tw_nb"), b("tw_mk")), (1, 16, 1))
        band = imm["tw_band"]          # tw_peak, tw_phold, tw_bhold follow it
        BAND, PEAK, PHOLD, BHOLD = (band + 16 * i + 5 for i in range(4))
        m.pause()
        for off, v in ((BAND, 16), (BHOLD, 250), (PEAK, 60), (PHOLD, 0)):
            m.write(base + off, bytes([v]))
        m.advance(cycles=int(2.5 * HZ))
        rd = lambda off: m.read(base + off, 1)[0]
        got = (rd(BAND), rd(BHOLD) < 250, rd(PEAK))
        m.run()
        check("bar held at 16 while the ticks ran", got[:2], (16, True))
        check("...and its marker fell to it (was 60)", got[2], 16)

    print("trklcd: %s" % ("pass" if not fails else "%d FAILED" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
