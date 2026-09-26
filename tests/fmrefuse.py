#!/usr/bin/env python3
"""A DRIVER SERVICE THAT NOTHING PUBLISHES MUST REFUSE, NOT HANG (SPEC.md 2.6.1.1).

    make && make build/fmtest.img && python3 tests/fmrefuse.py

`drv_svc_call_x` is FAR-entered - `drv_svc_call` in `.text` far-calls it into
`COLD_SEG` (SPEC.md 2.6) - so its own path ends in `retf`. Its two REFUSAL
paths (`or bp, bp / jz` = nothing publishes this verb, `cmp word [drv_fseg], 0
/ je` = no driver of this class) used to jump out of the extent to
`drv_svc_none`, which ends in a NEAR `ret` because `drv_fs_call` and
`drv_blk_call_x` share it and both are near-called. A refusal therefore popped
two bytes of a four-byte far frame and resumed at the caller's offset WITH CS
STILL COLD_SEG: a wild jump into cold code, taken with the graphics lock held.

WHAT MAKES THIS REACHABLE ON A STOCK MACHINE is that `osapi_snd_fm` is the one
sound slot with no zero test in front of it - `osapi_snd_stream` has one for a
different reason (SPEC.md 34.5) and the tone tier asks `snd_rt_card` first -
so ANY `OSAPI_SND_FM` call on a machine with no sound driver went through it.
A package that asks `OSAPI_SND_CAPS` first never does, which is why it
survived; the field route was Control Panel -> Drivers -> unmount Sound with a
sound application still polling, which is the same call arriving one tick
after the table was cleared.

THE MACHINE IS THE POINT. This cannot be a size check or a disassembly: what
the user sees is not a crash but a desktop that stops answering, because a
W_ONCLICK handler runs with the graphics lock held (SPEC.md 11) and nothing
can take it back. So the row drives the click and then asks the desktop a
question.

NO SOUND CARD, DELIBERATELY - `os8088_5150_cga` has none, so `drv_svc` is the
all-zero table the refusal path exists for. Assertion 1 states that rather
than assuming it: on a machine WITH a card every verb below succeeds and the
row would be green while proving nothing. (`make test-snd ADLIB=1
TESTAPPS=build/fmtest.img` is the other arm of the same disk, and it is the
one that tests the driver.)

FIVE ASSERTIONS:

  1. no driver publishes anything - `drv_svc` is all zero and `drv_fseg` is 0,
     so both of the refusal exits are the ones under test;
  2. FMTEST's click LANDED - its own `ft_stage` byte goes 0 -> 1. Without this
     the row passes a click that never reached the package, and assertions 3
     to 5 are then all true of a machine that did nothing;
  3. the FM verbs REFUSED - the package writes 'P' into its status line when
     verb 2 came back CF=1, 'N' for verb 0 and 'K' when both succeeded. 'P' is
     the contract on a machine with no driver;
  4. the graphics lock is BACK - `gfx_lock_flag` is 0 after the click. This is
     the assertion that goes red on the broken kernel;
  5. ...and the desktop still answers: the Apple menu drops and an item picks.

MEASURED, ON THE BROKEN KERNEL (origin/main at dfd5796b, the same disk):
`gfx_lock_flag` 0 -> 1 and stuck, and `menu_pick('Apple', ...)` raising "the
Apple menu to drop did not happen in 8 GUEST seconds". With the four-byte
`retf` in `drv_svc_call_x` the lock reads 0 and the menu drops.
"""
import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88build                                        # noqa: E402
import os88fixture                                      # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88ui                                           # noqa: E402

DISK = "build/fmtest.img"
PKG = "build/fmtest.bin"        # the package image, to locate its state bytes


def layout():
    """Where FMTEST keeps its stage byte and its result char, derived.

    `ft_ttl` / `ft_stage` / `ft_s_line1` are consecutive in the source and the
    title is the only literal that cannot move, so the two offsets are read
    out of the built image rather than typed here - a package rebuilt with one
    more string still answers.
    """
    d = open(os88build.at(PKG), "rb").read()
    o = d.find(b"FM Test\x00")
    if o < 0:
        raise SystemExit("fmrefuse: no 'FM Test' title in %s - the package's "
                         "layout moved and this row cannot find its state" % PKG)
    stage, line1 = o + 8, o + 9
    if d[stage] != 0 or d[line1:line1 + 10] != b"FM patch: ":
        raise SystemExit("fmrefuse: %s does not have ft_stage / ft_s_line1 "
                         "where this row expects them (%r)"
                         % (PKG, d[stage:stage + 21]))
    return stage, line1 + 10    # [+10] is the result char (K / P / N)


def main():
    ap = argparse.ArgumentParser()
    # NO SOUND CARD is the requirement; the 5150 CGA machine is only the
    # cheapest one that has none. Through os88marty.machine() so the IBM romset
    # resolves to its GLaBIOS twin on a box without a private copy.
    ap.add_argument("--machine", default=os88marty.machine("os8088_5150_cga"))
    a = ap.parse_args()
    os88fixture.need(DISK)
    off_stage, off_res = layout()
    bad = []

    with os88ui.boot("build/os8088-360.img", apps=DISK,
                     machine=a.machine) as ui:
        m, S = ui.m, ui.m.sym

        # --- 1. nothing is published, so the refusal path is the one run ----
        svc = m.read(S("drv_svc"), 16)
        fseg = struct.unpack("<H", m.read(S("drv_fseg"), 2))[0]
        print("drv_svc  = %s" % " ".join("%02x" % b for b in svc))
        print("drv_fseg = %04x" % fseg)
        if any(svc) or fseg:
            bad.append("1: something IS published (drv_svc %r, drv_fseg %04x)"
                       " - this machine has a sound driver, so the refusal "
                       "path is never taken and the row proves nothing"
                       % (list(svc), fseg))

        # ONE RETRY, AND IT IS NOT FLAKE-PAPERING. The first double-click of a
        # session opens a drive that has never been MOUNTED, so click one pays
        # a mount - several int 13h at ~400ms of guest time each - and click
        # two can land outside the kernel's 9-tick window on a loaded box
        # ("the guest saw two FIRST clicks"). The mount is done by then, so the
        # second attempt is the ordinary case and not a second roll of the same
        # dice. Nothing under test is touched: the defect is in the CLICK
        # BELOW, on a window that is open either way.
        for attempt in range(2):
            try:
                w = ui.path("B:/FMTEST.O88")
                break
            except Exception as e:
                if attempt:
                    raise
                print("  (navigation retried once: %s)"
                      % str(e).split(".")[0])
                ui.settle()
        raw = m.read(S("wm_wins"), os88geom.MAX_WIN * os88geom.WIN_SIZE)
        seg = struct.unpack_from("<H", raw,
                                 w.i * os88geom.WIN_SIZE + os88geom.W_SEG)[0]
        print("FMTEST %s in segment %04x" % (w, seg))

        def pkg(off, n=1):
            return m.readseg(seg, off, n)

        if pkg(off_stage)[0] != 0:
            bad.append("2: ft_stage is not 0 before the click")

        # --- the click: one FM patch-load + note-on, both refused -----------
        cx0, cy0, cx1, cy1 = w.content
        clicked = True
        try:
            ui.mo.click((cx0 + cx1) // 2, (cy0 + cy1) // 2)
        except Exception as e:
            clicked = False
            # A hung UI task is not itself a reason the mouse packet cannot be
            # decoded - the ISR is untouched - but the press/release handshake
            # has been seen to fail against the broken kernel. Record it and
            # go on: assertion 2 is what says whether the click landed, and
            # assertions 4 and 5 are the substantive ones either way.
            print("  (the click raised %s: %s)" % (type(e).__name__,
                                                   str(e).split(".")[0]))
        time.sleep(2)

        # --- 2. it LANDED -------------------------------------------------
        stage = pkg(off_stage)[0]
        print("ft_stage = %d (1 = the click reached ft_onclick)" % stage)
        if stage != 1 and clicked:
            # The click PROVED itself - os88mouse raises unless the guest's own
            # mouse_btn decoded both edges - so the packet arrived and the
            # handler is what did not finish. That is the defect, said in the
            # package's own state.
            bad.append("2: the click was decoded by the guest and ft_stage is "
                       "still %d - ft_onclick never returned from its first "
                       "OSAPI_SND_FM (SPEC.md 2.6.1.1)" % stage)
        elif stage != 1:
            bad.append("2: ft_stage is %d and the click did not land either - "
                       "nothing below is about the FM slot" % stage)

        # --- 3. ...and the verbs REFUSED ----------------------------------
        res = chr(pkg(off_res)[0])
        print("FM patch: %s   ('P' = verb 2 refused, the contract with no "
              "driver)" % res)
        if res != "P":
            bad.append("3: the status char is %r, not 'P' - with no driver "
                       "loaded OSAPI_SND_FM verb 2 must answer CF=1" % res)

        # --- 4. the graphics lock is back ---------------------------------
        lock = m.read(S("gfx_lock_flag"), 1)[0]
        print("gfx_lock_flag = %d" % lock)
        if lock:
            bad.append("4: gfx_lock_flag is %d - the W_ONCLICK handler never "
                       "returned, so no task can draw again (SPEC.md 2.6.1.1)"
                       % lock)

        # --- 5. ...and the desktop still answers --------------------------
        # A RAISE RATHER THAN A MENU, because the menu bar belongs to whichever
        # window is front and a hung machine's bar is not a fact about
        # liveness. `raise_window` clicks a title bar that is provably on the
        # glass and then reads `wm_zord` back, so the answer is the kernel's
        # own, and on the broken kernel it cannot come: the title-bar click
        # wants the graphics lock the dead handler is still holding.
        try:
            ui.raise_window("Disk")
            f = ui.front()
            print("the desktop still answers - front is now %r"
                  % (f.title if f else None))
            if not f or f.title != "Disk":
                bad.append("5: the raise was accepted but the front window is "
                           "%r" % (f.title if f else None))
        except Exception as e:
            bad.append("5: the desktop does not answer - %s"
                       % str(e).split(".")[0])

    for b in bad:
        print("FAIL " + b)
    if bad:
        return 1
    print("PASS: OSAPI_SND_FM with no sound driver refuses and the desktop "
          "lives (SPEC.md 2.6.1.1)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
