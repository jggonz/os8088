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
import sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.chdir(ROOT)
import os88marty, os88mouse, os88sym, dispcp                      # noqa: E402
from os88fixture import need                                      # noqa: E402
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
    need(DISK)                     # `all` builds nothing under tests/
    with os88marty.launch("build/os8088-360.img",
                          apps=DISK,
                          machine="os8088_5150_sb_gla", boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        # THE SAVER, OFF, before anything waits. This row polls for the
        # player to become resident for up to two guest MINUTES with no
        # input, which is well inside no_saver's own rule, and the saver
        # then animates - so `settle` can never return and the run dies
        # naming the saver rather than the tracker. Measured, in the pass-2
        # soak: "the screen was still changing after 120s because SPEC.md
        # 79's SCREEN SAVER IS RUNNING".
        os88marty.no_saver(m)
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
            m.read(base + P["@" + n], 2) for n in
            ("mp_loaded", "mp_playing", "mp_mixrate", "mp_xt", "trk_fs")),
            guest=1.0, what="Tracker's open to finish")
        b = lambda n: m.read(base + P["@" + n], 1)[0]
        w = lambda n: int.from_bytes(m.read(base + P["@" + n], 2), "little")
        w2 = lambda off: int.from_bytes(m.read(base + off, 2), "little")

        # Each key waits for the UI to be FINISHED with it rather than for a
        # fixed pause: out of the BIOS ring (its head moved), and then the
        # step's own end state - the UI idle (no event queued, the gfx lock
        # free), or the bracket up, which holds the lock for its whole life.
        # Twice, a twentieth of a guest second apart, because ui_task pops an
        # event a few instructions before it takes the lock for it. A step
        # that never gets there is reported and the check after it says why.
        KBUF = 0x41A                    # 0040:001A - the BIOS ring's head
        kbhead = lambda: m.read(KBUF, 2)
        cyc = lambda: int(m.status()["cycles"])

        def ui_idle():
            kb = m.read(KBUF, 4)
            return (kb[0:2] == kb[2:4] and m.read(S("evq_count"), 1)[0] == 0
                    and m.read(S("gfx_lock_flag"), 1)[0] == 0)

        def key(k, fin=ui_idle):
            h = kbhead(); m.key(k)
            what = "the %s key to be handled" % k
            try:
                os88marty.until(m, lambda _: kbhead() != h and fin(), what,
                                poll=0.05, limit=30)
                c0 = cyc()
                os88marty.until(m, lambda _: cyc() - c0 >= os88marty.GUEST_HZ / 20
                                and fin(), what, poll=0.05, limit=30)
            except os88marty.MartyError as e:
                print("  (%s)" % str(e).split(". ")[0])

        fs_on = lambda: b("trk_fs") == 1 or ui_idle()
        fs_off = lambda: b("trk_fs") == 0 and ui_idle()

        print("1. the defaults")
        check("mp_xt (pre-armed on a tier-0 machine)", b("mp_xt"), 1)
        check("trk_xhi (bss arrives zeroed = 5,500)", b("trk_xhi"), 0)

        print("2. fullscreen at 5,500")
        key("KeyF", fs_on)
        check("trk_fs after F", b("trk_fs"), 1)
        key("Escape", fs_off)
        check("trk_fs after Esc", b("trk_fs"), 0)

        print("3. R picks the high rate")
        key("KeyR")
        check("trk_xhi after R", b("trk_xhi"), 1)
        if not SHIPPED:
            check("the bench sweep followed it", w("tlog_xrate"), 11000)

        print("4. ...and the text screen is refused while it is picked")
        key("KeyF", fs_on)
        check("trk_fs after F at 11 kHz", b("trk_fs"), 0)

        print("5. the stream opens at the rate the control names")
        key("Enter", lambda: b("mp_playing") == 1 and ui_idle())
        os88marty.quiesce(m, lambda: (w("mp_mixrate"), b("mp_playing")),
                          guest=1.0, what="the stream to open")
        check("mp_mixrate while playing", w("mp_mixrate"), 11000)
        check("mp_playing", b("mp_playing"), 1)
        key("Space", lambda: b("mp_playing") == 0 and ui_idle())

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
        key("KeyR")
        check("trk_xhi after R", b("trk_xhi"), 0)
        if not SHIPPED:
            check("the bench sweep followed it", w("tlog_xrate"), 5500)
        check("View > Fullscreen is live again", item0_byte("trk_e_view"), ord("F"))
        key("KeyF", fs_on)
        check("trk_fs after F at 5.5 kHz", b("trk_fs"), 1)
        key("Escape", fs_off)

        print("8. ...and XT mode off puts the other mode's rows back")
        key("KeyX")
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
        check("View > Fullscreen is live", item0_byte("trk_e_view"), ord("F"))

    print("\n%s" % ("FAILED: " + ", ".join(fails) if fails else "all checks passed"))
    return 1 if fails else 0


sys.exit(main())
