#!/usr/bin/env python3
"""WHAT ONE BOUNCE FRAME COSTS THE MACHINE, in 8088 cycles.

    make && python3 tests/bouncecost.py

Bounce (SPEC.md 14) wakes every 2 ticks and its period is task_sleep's, not
its own, so nothing this measures can change its FRAME RATE. What it measures
is the other thing: how much of a 4.77MHz machine one live Bounce takes away
from the UI task per frame. That is the number a change to the crossings on
its path moves, and SPEC.md 2.6's cadence test is an argument about exactly
this quantity - so it is worth a measurement rather than an estimate.

THE BRACKET IS THE LOOP BODY AND NOT THE PERIOD. app_bounce_task's `.loop`
sleeps first, so `.loop` to `.loop` is 110ms of which ~108 is the sleep and
would drown everything under test. The bracket starts at the instruction the
sleep RETURNS to and ends at the next `.loop`, which is the whole visible
frame - the die check, gfx_lock, the W_FLAGS test, wm_clip_set, app_ball_step,
app_ball_fill, app_ball_wipe and gfx_unlock.

AND ITS FIRST ADDRESS IS DECODED, NOT COMPUTED. That instruction is
`.loop + 3 + len(call)`, and the call is three bytes near or five bytes far
depending on which side of SPEC.md 2.6 app_bounce_task is on. Reading the
opcode says which - 0xE8 near, 0x9A far - so one script measures both kernels
and neither number comes from an assumption about the one it is looking at.

AND FRAMES ARE PAIRED BY BALL STATE, not averaged. The spread between frames
is NOT noise and a mean over it answers the wrong question: app_ball_wipe
draws a column strip and a row strip whose areas follow the step, and a
bounce shortens the step at the wall, so different frames legitimately cost
different amounts - 20,336 to 23,534 cycles over one sample of 24. The ball's
trajectory is deterministic from app_bounce_kinit's (4,4) and (3,2), so the
same (x, y, vx, vy) is the same frame's work in any build. Recording that key
with each sample turns two runs into a PAIRED comparison, and the trajectory
variance - which is most of the spread - cancels instead of being averaged
over. What is left over a matched pair is the crossings and nothing else.

MartyPC's counter is still the MACHINE's, so a PIT tick landing inside a
bracket adds the scheduler and whatever it picks. Those samples are only ever
LONGER, so a key seen more than once keeps its MINIMUM.

    python3 tests/bouncecost.py --json build/bounce-cold.json
    python3 tests/bouncecost.py --json build/bounce-base.json --compare build/bounce-cold.json
"""
import argparse, json, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88marty
from os88mouse import Mouse
import os88sym
from os88geom import MB_ENTSZ      # the bar cell stride, SPEC.md 12.2

S = os88sym.linear
KERNEL_SEG = 0x0060
MBAR_H, MENU_ITEM_H, MB_XL, MB_SEG = 20, 16, 6, 10
CELL_BUILTIN, ITEM_BOUNCE = 2, 1
SAMPLES = 40


def u16(b, o=0):
    return b[o] | (b[o + 1] << 8)


def menu_title(m, cell):
    t = m.read(S("menu_bar") + cell * MB_ENTSZ, MB_ENTSZ)
    p, sg = u16(t, 0), u16(t, MB_SEG)
    if not p:
        return "<logo>"
    return m.read((sg or KERNEL_SEG) * 16 + p,
                  16).split(b"\0")[0].decode("latin-1")


def loop_addrs(m):
    """(the address the sleep returns to, the address of `.loop`), and the
    call width that decided the first - which also names which kernel this is.
    """
    for base in ("app_bounce_task_x", "app_bounce_task"):
        try:
            loop = S(base + ".loop")
        except Exception:
            continue
        op = m.read(loop + 3, 1)[0]
        if op not in (0xE8, 0x9A):
            sys.exit("bouncecost: %s.loop+3 is opcode %02X, which is neither "
                     "a near nor a far call - the loop's shape has changed "
                     "and this bracket no longer describes it" % (base, op))
        return loop + 3 + (3 if op == 0xE8 else 5), loop, op
    sys.exit("bouncecost: app_bounce_task has no `.loop` in the map")


def frames(m, after_sleep, loop, n):
    """{(x, y, vx, vy): cycles} - the ball state the frame STARTED from, and
    what that frame cost. SI carries the state pointer for the task's life
    (app_bounce_task's own comment) and SS is LOW_SEG from kmain onwards, so
    the four words are read where the task itself reads them."""
    out = {}
    for i in range(n):
        m.bp_exec(after_sleep)
        m.run()
        if not m.wait_stop(limit=30.0):
            sys.exit("bouncecost: the Bounce task never woke - is the window "
                     "open and visible?")
        r = m.regs()
        if i == 0:
            # SEED THE TRAJECTORY, because two runs do not otherwise share a
            # single frame. The path is deterministic from app_bounce_kinit's
            # (4,4)+(3,2), but its cycle is ~93 frames in x and 111 in y, so
            # forty CONSECUTIVE frames taken from wherever the first
            # breakpoint happened to land overlap the other run's forty
            # almost never - measured: zero keys in common. Writing the seed
            # here starts both walks at the same place.
            #
            # THIS ADDRESS IS SAFE and the frame stays self-consistent: the
            # bracket opens at the die check, which is BEFORE the task banks
            # CX/DX from BAL_X/BAL_Y, so the step, the fill and the wipe all
            # see the seeded position. app_ball_wipe's [app_bwx]/[app_bwy]
            # are written from CX/DX at its head before anything reads them,
            # so no frame carries state into the next one.
            m.write((r["ss"] << 4) + r["si"],
                    bytes([4, 0, 4, 0, 3, 0, 2, 0]))
        b = m.read((r["ss"] << 4) + r["si"], 8)
        key = (u16(b, 0), u16(b, 2), u16(b, 4), u16(b, 6))
        c0 = m.status()["cycles"]
        m.bp_exec(loop)
        m.run()
        if not m.wait_stop(limit=30.0):
            sys.exit("bouncecost: the frame never reached `.loop`")
        c = m.status()["cycles"] - c0
        if key not in out or c < out[key]:
            out[key] = c
    return out


def compare(mine, other):
    """The paired delta: same ball state, both kernels, one subtraction."""
    them = {tuple(int(x) for x in k.split(",")): v
            for k, v in other["frames"].items()}
    both = sorted(set(mine) & set(them))
    if not both:
        sys.exit("bouncecost: the two runs share no frame - nothing to pair")
    print()
    print("   PAIRED against %s (%s):" % (other["shape"], "the other kernel"))
    print("   %-22s %9s %9s %8s" % ("ball x,y,vx,vy", "this", "other", "delta"))
    d = []
    for k in both:
        d.append(mine[k] - them[k])
        print("   %-22s %9d %9d %+8d"
              % (",".join(map(str, k)), mine[k], them[k], d[-1]))
    print("   %-22s %9s %9s %+8.1f  (%d frames matched)"
          % ("mean", "", "", sum(d) / len(d), len(d)))
    print("   %.1f us a frame at 4.77MHz, %.3f%% of the machine"
          % (sum(d) / len(d) / 4.772727,
             100.0 * (sum(d) / len(d)) / 4772.727 / 109.9))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    ap.add_argument("--compare")
    args = ap.parse_args()
    img = "build/os8088-360.img"
    apps = "build/apps360.img"
    with os88marty.launch(img, apps=apps, machine="os8088_5150_cga_gla") as m:
        mo = Mouse(marty=m)
        os88marty.no_saver(m)   # SPEC.md 79: it draws, so nothing settles
        title = menu_title(m, CELL_BUILTIN)
        if title != "Builtins":
            sys.exit("bouncecost: menu cell %d is %r, not 'Builtins' - the "
                     "bar has been rebuilt and the item index with it"
                     % (CELL_BUILTIN, title))
        t = m.read(S("menu_bar") + CELL_BUILTIN * MB_ENTSZ, MB_ENTSZ)
        x = u16(t, MB_XL) + 6
        # THE GESTURE IS RETRIED, AND THAT IS NOT A SLEEP IN DISGUISE. The
        # boot gate watches the DESKTOP, and SPEC.md 9.4.1's identify window
        # is ~596ms of mouse_init that the desktop does not wait for - so the
        # first press after a boot can land before anything is listening, and
        # whether it does depends on how the kernel's size moved the boot
        # around. That made this look like a difference between two kernels
        # when it is a difference between two boots: one build opened Bounce
        # on the first press and the other never did, twice running, on code
        # neither of them changed. app_bounce_kinit is armed BEFORE the press,
        # so what is waited on is the LAUNCH itself rather than a duration.
        m.bp_exec("app_bounce_kinit")
        for attempt in range(6):
            mo.menu(x, 8, x, MBAR_H + 1 + ITEM_BOUNCE * MENU_ITEM_H + 8)
            if m.wait_stop(limit=8.0):
                break
        else:
            sys.exit("bouncecost: six presses on Builtins/Bounce and "
                     "app_bounce_kinit never ran - the menu is not being "
                     "reached at all")
        if attempt:
            print("   (the menu took %d presses - see the note in the source)"
                  % (attempt + 1))
        # NO settle from here on, and that is not an omission: the thing just
        # opened ANIMATES, so the screen never stops changing and settle's
        # 120s would end in the same failure the screen saver produces. The
        # breakpoint below is the gate instead, and a better one - it proves
        # the task is running AND that it is taking the visible path.
        m.bp_exec("app_ball_wipe")
        m.run()
        if not m.wait_stop(limit=30.0):
            sys.exit("bouncecost: app_ball_wipe never ran - Bounce is open "
                     "but not visible, and a .blind frame is not the frame "
                     "this measures")

        after, loop, op = loop_addrs(m)
        shape = "near (.text)" if op == 0xE8 else "far (.cold)"
        print("   app_bounce_task: task_sleep is a %s call" % shape)
        print("   bracket %05X -> %05X" % (after, loop))

        f = frames(m, after, loop, SAMPLES)
        v = sorted(f.values())
        print("   %d distinct frames: min %d  median %d  max %d cycles"
              % (len(v), v[0], v[len(v) // 2], v[-1]))
        print("   ONE BOUNCE FRAME = %d..%d cycles = %.2f..%.2f ms at 4.77MHz"
              % (v[0], v[-1], v[0] / 4772.727, v[-1] / 4772.727))
        print("   ...of a 2-tick period (109.9 ms): %.2f%%..%.2f%% of the "
              "machine" % (100.0 * v[0] / 4772.727 / 109.9,
                           100.0 * v[-1] / 4772.727 / 109.9))

        if args.json:
            json.dump({"shape": shape,
                       "frames": {",".join(map(str, k)): c
                                  for k, c in f.items()}},
                      open(args.json, "w"), indent=1)
            print("   -> %s" % args.json)
        if args.compare:
            compare(f, json.load(open(args.compare)))


if __name__ == "__main__":
    main()
