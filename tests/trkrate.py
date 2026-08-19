#!/usr/bin/env python3
"""trkrate - XT mode's second rate, and the surface it refuses (SPEC.md 45.9.3)

    make trkrate && python3 tests/trkrate.py [--shipped]

Checks the whole control rather than the rate alone, because the interesting
half is the REFUSAL: 11,000 Hz holds windowed and does not hold on 45.13's
text screen, so `trk_fs_enter` says no while it is picked. A test that only
proved the rate changed would pass on a build that let the user into a
fullscreen it cannot feed.

  1. XT mode is pre-armed on this machine and the rate starts at 5,500
  2. fullscreen works at 5,500
  3. R picks 11,000 - and the BENCH sweep moves with it, or the menu label and
     the playing rate part company
  4. fullscreen is REFUSED at 11,000, and trk_fs is still 0 afterwards
  5. ...and the stream really opens at 11,000 when Play is pressed
  6. R back to 5,500 and fullscreen works again

It wants a Sound Blaster, which in a container means os8088_5150_sb_gla.
"""
import sys, os, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.chdir(ROOT)
import os88marty, os88mouse, os88sym, dispcp                      # noqa: E402
from os88rate import symbols, scan                                # noqa: E402

# The SHIPPED build is a different rate path - trk_play picks tlog_xrate under
# TRKLOG and trk_xhi without it - so a gate that only ever runs the bench build
# has not tested the binary anybody gets. `--shipped` runs the same checks on
# TRACKER.O88, minus the two that are about the sweep.
SHIPPED = "--shipped" in sys.argv
DEFINES = () if SHIPPED else ("TRKLOG",)
DISK = ("build/trkship360.img" if SHIPPED else "build/trklog360.img")

fails = []


def check(name, got, want):
    ok = got == want
    print("  %-46s %-12s %s" % (name, got, "ok" if ok else "FAIL, want %s" % (want,)))
    if not ok:
        fails.append(name)


def main():
    P, _ = symbols(DEFINES)
    S = os88sym.linear
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
        seg = None
        for _ in range(60):
            time.sleep(2)
            seg, _drv = scan(m)
            if seg:
                break
        if not seg:
            print("FAIL: Tracker never loaded"); return 1
        time.sleep(25)
        base = seg * 16
        b = lambda n: m.read(base + P["@" + n], 1)[0]
        w = lambda n: int.from_bytes(m.read(base + P["@" + n], 2), "little")
        w2 = lambda off: int.from_bytes(m.read(base + off, 2), "little")

        print("1. the defaults")
        check("mp_xt (pre-armed on a tier-0 machine)", b("mp_xt"), 1)
        check("trk_xhi (bss arrives zeroed = 5,500)", b("trk_xhi"), 0)

        print("2. fullscreen at 5,500")
        m.key("KeyF"); time.sleep(4)
        check("trk_fs after F", b("trk_fs"), 1)
        m.key("Escape"); time.sleep(4)
        check("trk_fs after Esc", b("trk_fs"), 0)

        print("3. R picks the high rate")
        m.key("KeyR"); time.sleep(2)
        check("trk_xhi after R", b("trk_xhi"), 1)
        if not SHIPPED:
            check("the bench sweep followed it", w("tlog_xrate"), 11000)

        print("4. ...and the text screen is refused while it is picked")
        m.key("KeyF"); time.sleep(4)
        check("trk_fs after F at 11 kHz", b("trk_fs"), 0)

        print("5. the stream opens at the rate the control names")
        m.key("Enter"); time.sleep(14)
        check("mp_mixrate while playing", w("mp_mixrate"), 11000)
        check("mp_playing", b("mp_playing"), 1)
        m.key("Space"); time.sleep(4)

        print("6. the Rate MENU is the mode's own rows (SPEC.md 45.9.3)")
        # Walked the way the kernel walks it: the set entry -> AMENU_ITEMS ->
        # the item array -> the first string. Reading the composed buffer by
        # name would need its equ; reading the STRUCTURE tests what the bar
        # will actually draw.
        AMENU_ITEMS, AMENU_NITEM = 2, 4

        def entry_items(lbl):
            arr = w2(P[lbl] + AMENU_ITEMS)
            return [w2(arr + 2 * i) for i in range(w2(P[lbl] + AMENU_NITEM))]

        def item0_byte(lbl):
            return m.read(base + entry_items(lbl)[0], 1)[0]

        check("Rate rows with XT mode ON", w2(P["trk_e_rate"] + AMENU_NITEM), 2)
        check("View > Fullscreen is MENU_DIS at 11 kHz", item0_byte("trk_e_view"), 1)

        print("7. R back, and the surface comes back with it")
        m.key("KeyR"); time.sleep(2)
        check("trk_xhi after R", b("trk_xhi"), 0)
        if not SHIPPED:
            check("the bench sweep followed it", w("tlog_xrate"), 5500)
        check("View > Fullscreen is live again", item0_byte("trk_e_view"), ord("F"))
        m.key("KeyF"); time.sleep(4)
        check("trk_fs after F at 5.5 kHz", b("trk_fs"), 1)
        m.key("Escape"); time.sleep(3)

        print("8. ...and XT mode off puts the other mode's three rows back")
        m.key("KeyX"); time.sleep(3)
        check("mp_xt after X", b("mp_xt"), 0)
        check("Rate rows with XT mode OFF", w2(P["trk_e_rate"] + AMENU_NITEM), 3)
        check("View > Fullscreen is live", item0_byte("trk_e_view"), ord("F"))

    print("\n%s" % ("FAILED: " + ", ".join(fails) if fails else "all checks passed"))
    return 1 if fails else 0


sys.exit(main())
