#!/usr/bin/env python3
"""trktxsurf - the fullscreen SURFACE is a pick, not XT mode's (SPEC.md 45.13.7)

    make trkrate && python3 tests/trktxsurf.py

The claim the section makes is one sentence: a machine can have §45.13's text
screen WITHOUT XT mode's rates. Everything here exists to test that sentence
and the two ways of getting it wrong.

The trap is that a test can pass while proving nothing. On a tier-0 machine -
which MartyPC is - Tracker pre-arms XT mode at entry (§45.9), so `trk_txon`'s
first term is true before anything is pressed and a bracket that reaches the
text screen has NOT demonstrated the decoupling. So X comes off FIRST, and
every interesting assertion below is made with `[mp_xt]` observed to be 0.

  1. XT mode is pre-armed here, and the pick is untouched by that
  2. X takes XT mode off - the rate menu goes back to the 45.10 rows
  3. V sets the pick, and the row is LIVE (not MENU_DIS) with XT mode off
  4. R takes the rate to 22 kHz, which no XT mode can reach
  5. F reaches the TEXT screen with [mp_xt] = 0            <- the whole point
  6. ...the music under it plays at 22,050, and the NAMEPLATE says so - it
     was the constant 'XT 5500 Hz' and this screen is not XT mode's any more
  7. Esc, then V again: the pick clears and F reaches the GRAPHICS screen
  8. the pick SURVIVES an XT mode round trip - the 45.9.3 rule that a setting
     which undoes itself is one nobody can rely on

It wants a Sound Blaster, which in a container means os8088_5150_sb_gla.
"""
import sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.chdir(ROOT)
import os88marty, os88mouse, os88sym, dispcp                      # noqa: E402
from os88fixture import need                                      # noqa: E402
from os88rate import symbols, scan                                # noqa: E402

DISK = "build/trkship360.img"          # the SHIPPED player: no %ifdef paths
fails = []


def check(name, got, want):
    ok = got == want
    print("  %-52s %-10s %s" % (name, got, "ok" if ok else "FAIL, want %s" % (want,)))
    if not ok:
        fails.append(name)


def main():
    P, _ = symbols(())
    S = os88sym.linear
    need(DISK)                     # `all` builds nothing under tests/
    with os88marty.launch("build/os8088-360.img",
                          apps=DISK,
                          machine="os8088_5150_sb_gla", boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        slot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, slot)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "BEVERLY.MOD")
        # THE MODULE LOADED, on guest state: the image found in memory, its
        # [mp_loaded] set, and the open's own tail after it (trk_play, the
        # completion repaint) gone quiet. This was a 2s poll on a host clock
        # and then a blind 25s.
        found = [None]

        def _loaded(_m):
            found[0] = scan(m)[0]
            return bool(found[0]) and m.read(
                found[0] * 16 + P["@mp_loaded"], 1)[0] == 1
        try:
            os88marty.until(m, _loaded, "Tracker to load BEVERLY.MOD",
                            poll=1.0, limit=150)
        except os88marty.MartyError:
            pass
        seg = found[0]
        if not seg:
            print("FAIL: Tracker never loaded"); return 1
        base = seg * 16
        os88marty.quiesce(m, lambda: tuple(
            m.read(base + P["@" + n], 2 if n == "mp_mixrate" else 1)
            for n in ("mp_loaded", "mp_playing", "mp_mixrate", "mp_xt",
                      "trk_fs")),
            guest=1.0, what="Tracker's open to finish")
        b = lambda n: m.read(base + P["@" + n], 1)[0]
        w = lambda n: int.from_bytes(m.read(base + P["@" + n], 2), "little")
        w2 = lambda off: int.from_bytes(m.read(base + off, 2), "little")

        # The View menu walked the way the KERNEL walks it - set entry ->
        # AMENU_ITEMS -> the item array - so this tests what the bar will
        # actually draw rather than a buffer read by name (trkrate's idiom).
        AMENU_ITEMS, AMENU_NITEM = 2, 4

        def view_items():
            arr = w2(P["trk_e_view"] + AMENU_ITEMS)
            n = w2(P["trk_e_view"] + AMENU_NITEM)
            return [w2(arr + 2 * i) for i in range(n)]

        def rate_cell():
            """The nameplate's rate string, as ttx_rate_stamp leaves it."""
            return m.read(base + P["ttx_s_xtrate"], 11)

        def text_row():
            """The composed View > Text Screen row, as bytes up to its NUL."""
            items = view_items()
            if len(items) < 2:
                return b"<no second View item>"
            raw = m.read(base + items[1], 24)
            return raw.split(b"\0")[0]

        # Every key below is waited out on the byte it moves and then on the
        # whole of what the checks read going still - each used to be a blind
        # 3 to 16 seconds (up to 72 GUEST seconds for a Play), which is what
        # this row spent most of its time doing.
        STATE = ("mp_xt", "trk_txw", "trk_rsel", "trk_fs", "trk_tx",
                 "mp_playing", "mp_mixrate")

        def state():
            # A BYTE EACH, bar the rate: a two-byte read of a byte variable
            # carries its neighbour in .bss, and those neighbours are live
            # while the module plays (they cycle 1-2-3 and flip 0/1), so the
            # tuple never went still and the quiesce after Play failed once
            # its readings happened to straddle one
            return (tuple(m.read(base + P["@" + n],
                                 2 if n == "mp_mixrate" else 1)
                          for n in STATE),
                    rate_cell(), text_row(),
                    w2(P["trk_e_rate"] + AMENU_NITEM))

        def press(key, name, want):
            m.key(key)
            try:
                os88marty.until(m, lambda _: b(name) == want,
                                "%s -> [%s] = %d" % (key, name, want),
                                poll=0.2, limit=60)
            except os88marty.MartyError as e:
                print("  (%s)" % e)     # the check below names it
            os88marty.quiesce(m, state, guest=1.0,
                              what="Tracker to finish %s" % key)

        print("1. the defaults on a tier-0 machine")
        check("mp_xt (pre-armed, SPEC.md 45.9)", b("mp_xt"), 1)
        check("trk_txw (bss arrives zeroed)", b("trk_txw"), 0)
        check("View has TWO rows now", w2(P["trk_e_view"] + AMENU_NITEM), 2)
        check("...and the row is MENU_DIS while XT forces it",
              text_row()[:1], b"\x01")

        print("2. X takes XT mode off")
        press("KeyX", "mp_xt", 0)
        check("mp_xt after X", b("mp_xt"), 0)
        # 11/22 kHz, and 33/44 ONLY where the card can play them: the
        # count is the guest's own SND_CAP_PCM_HI, read off the kernel's
        # copy of the driver's caps (drv_svc + DSV_CAPS = 0), so this holds
        # on the DSP 2.01 SB this machine carries (2) and on an SB Pro/16 (4)
        # alike (SPEC.md 45.10.1)
        hirate = int.from_bytes(m.read(S("drv_svc"), 2), "little") & 0x20
        check("Rate rows with XT mode OFF (%s)" % ("SB Pro/16" if hirate
              else "SB 2.0: no 33/44"), w2(P["trk_e_rate"] + AMENU_NITEM),
              4 if hirate else 2)
        check("the Text Screen row is LIVE and unmarked",
              text_row(), b"  Text Screen")

        print("3. V picks the text surface, XT mode still off")
        press("KeyV", "trk_txw", 1)
        check("trk_txw after V", b("trk_txw"), 1)
        check("mp_xt is untouched by V", b("mp_xt"), 0)
        check("the row is starred and still live", text_row(), b"* Text Screen")

        # R moves the rate to 22 kHz BEFORE the bracket, and it has to happen
        # here: trk_rate_set refuses on the text surface (SPEC.md 45.13.3).
        # 22,050 is the point of the whole exercise - it is a rate NO XT mode
        # can reach, so a text screen running at it cannot be XT mode's. 11,000
        # would not do: TRK_RATE_XT2 is 11,000 as well, so the two ladders
        # collide on that value and a pass there proves nothing.
        print("4. R takes the rate somewhere XT mode cannot go")
        press("KeyR", "trk_rsel", 1)
        check("trk_rsel after R (45.10 row 1)", b("trk_rsel"), 1)

        print("5. F reaches the TEXT screen with XT mode OFF")
        press("KeyF", "trk_fs", 1)
        check("trk_fs", b("trk_fs"), 1)
        check("trk_tx  <- the decoupling", b("trk_tx"), 1)
        check("mp_xt still 0 under it", b("mp_xt"), 0)

        print("6. ...at 22 kHz, which is what the section is FOR")
        press("Enter", "mp_playing", 1)
        check("mp_playing", b("mp_playing"), 1)
        check("mp_mixrate  <- no XT mode reaches this", w("mp_mixrate"), 22050)
        # The nameplate was the CONSTANT 'XT 5500 Hz', on the reasoning that
        # this screen only existed while XT mode was on. It does not any more,
        # and a header claiming 5,500 over a 22,050 stream is the app lying
        # about its own state - so the cell is stamped, and delta-drawn from
        # inside the bracket because Play opens the stream after ttx_name ran.
        check("the nameplate rate followed Play", rate_cell(), b"   22050 Hz")
        press("Space", "mp_playing", 0)
        press("Escape", "trk_fs", 0)
        check("trk_fs after Esc", b("trk_fs"), 0)
        check("trk_tx after Esc", b("trk_tx"), 0)

        print("7. V again clears it, and F is the graphics bracket")
        press("KeyV", "trk_txw", 0)
        check("trk_txw after the second V", b("trk_txw"), 0)
        press("KeyF", "trk_fs", 1)
        check("trk_fs", b("trk_fs"), 1)
        check("trk_tx is 0 - the FT2 screen", b("trk_tx"), 0)
        press("Escape", "trk_fs", 0)

        print("8. the pick survives an XT round trip (SPEC.md 45.9.3's rule)")
        press("KeyV", "trk_txw", 1)
        check("trk_txw picked again", b("trk_txw"), 1)
        press("KeyX", "mp_xt", 1)                      # XT on  - forces the surface
        check("mp_xt on", b("mp_xt"), 1)
        check("trk_txw is NOT cleared by the toggle", b("trk_txw"), 1)
        press("KeyF", "trk_fs", 1)                     # ...and the header says XT now
        press("Enter", "mp_playing", 1)
        check("the nameplate says XT again", rate_cell(), b"XT  5500 Hz")
        press("Space", "mp_playing", 0)
        press("Escape", "trk_fs", 0)
        press("KeyX", "mp_xt", 0)                      # XT off - and the pick stands
        check("mp_xt off", b("mp_xt"), 0)
        check("trk_txw still stands", b("trk_txw"), 1)
        check("the row is starred and live again", text_row(), b"* Text Screen")

    print()
    if fails:
        print("FAIL (%d): %s" % (len(fails), ", ".join(fails)))
        return 1
    print("trktxsurf: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
