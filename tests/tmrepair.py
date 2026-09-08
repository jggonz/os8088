#!/usr/bin/env python3
"""The Task Manager repairs itself at the restore (SPEC.md 28.11)

    make && python3 tests/tmrepair.py [--machine os8088_xt_vga]

SPEC.md 28.8 measured why this window cannot hold an ORDINARY raise cache:
the cache is a purgeable heap claim, its two quiet pages are the tree's only
reporters of purgeable claims, so taking one changes what the page must show
and showing it frees the cache. Banked at the raise and gone 0.7 s later,
every time.

28.11 removes the step that closed that loop. The withdrawal was forced by
11.96.1's promise - "my content does not change while I am not drawing" - and
a window that repairs itself at the restore is not making it. The doorway is
a band covering the WHOLE content: 11.96.11's degenerate case, everything
banked and nothing owed, which wm_draw_win already routes through W_PAINT
where a plain restore skips it. wm_damage then answers the EMPTY rect and the
app spends its own debt.

  PROMISE  on the heap page it carries WF_SAVEU, and a whole-content band
           with it.
  KEPT     wholly covered, wm_su_drop is not called for it across many worker
           polls - 28.8's loop is gone. Asserted through a breakpoint and not
           the cache word, for 11.96.18.1's reason.
  REPAIR   cover it wholly, open AND CLOSE an app so the claim table and the
           instance block both move, then close the cover - and the glass
           must equal a forced full repaint. That is the whole feature: the
           restore put back a picture from before any of that happened, and
           tm_update replayed the difference.
  LIVE     cycle to the performance view and the promise goes, band and all.

REPAIR IS THE LEG THAT BITES, and it is written so it cannot pass vacuously.
Without a live cache wm_damage answers WHOLE, tm_paint draws everything, and
the pixels match a full repaint for the wrong reason - so the leg counts
wm_su_occl with DI on our window record. Reaching it IS the restore going
ahead, and it is the one instrument 11.96.18.1 leaves standing: the cache
WORD gave three different answers to three ways of looking at it.

**AND IT DOES NOT SIT PAST ONE REFUSAL, IT SITS PAST FOUR.** This banner said
"past every refusal in wm_su_ck", and kernel/wm.inc says otherwise:
`wm_su_ck`, `wm_su_vset`, `wm_su_ck` again and `wm_su_scrset` each answer CF
and each `jc wm_su_tno`. So a zero here has four causes wanting four
different fixes - a claim that is gone, a rect whose display moved
(SPEC.md 39.14.8.1), an edge scratch that would not fit - and all four leave
the GLASS CORRECT, because the fallback is W_PAINT and a full repaint. That
is the exact signature of every failure this row has reported: `0 wm_su_occl
call(s)` beside `0 subpixels differ`, twice now put down to the observation
window because the report could not say anything else.

So the five are armed together and the trace is printed either way. A failing
run names the gate that refused; a healthy one reads `wm_su_ck -> wm_su_vset
-> wm_su_ck -> wm_su_scrset -> wm_su_occl`, which is the source in order.

Servicing a breakpoint and driving the mouse are the same loop here, which is
why `edge` exists rather than mo.click: os88mouse waits on the guest's own
published mouse_btn, and a guest parked at a breakpoint never publishes it.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88geom, os88sym, dispcp, dispcorner, dispapps

MACHINE = "os8088_xt_vga"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
KERNEL_SEG = 0x60
TITLE_H = 18
POLLS = 8                               # TM_INT is 9 ticks, ~0.5 s each
fails = []


def tick(mm, card=None):
    mm.advance(frames=110)
    mm.run()


def zord(m):
    n = m.read(S("wm_zn"), 1)[0]
    return list(m.read(S("wm_zord"), max(n, 1)))[:n]


def view(m, seg):
    return m.read((seg << 4) + dispapps.img_size("taskmgr")
                  + dispapps.bss_off("taskmgr", "tm_view"), 1)[0]


def win(m, slot):
    got = [x for x in os88geom.windows(m) if x.i == slot]
    return got[0] if got else None


def wholly(over, w):
    return (over.x <= w.x and over.y <= w.y
            and over.x + over.w >= w.x + w.w
            and over.y + over.h >= w.y + w.h)


def rect(fb, cw, w):
    """The window's own pixels out of a framebuffer, as a flat list."""
    out = []
    for y in range(w.y, w.y + w.h):
        o = (y * cw + w.x) * 3
        out.append(fb[o:o + w.w * 3])
    return b"".join(out)


def raise_win(m, mo, slot, x, y, what):
    """wm_front banks the OUTGOING FRONT and nothing else (SPEC.md 11.96.18.1),
    so a cover whose subject was not in front banks nothing at all."""
    for _ in range(3):
        mo.click(x, y)
        mo.to(*dispcorner.PARK)
        tick(m)
        if zord(m)[-1] == slot:
            return True
    fails.append("SETUP: %s never reached the front" % what)
    return False


# --- counting a kernel call while the mouse is being driven -----------------
#
# `advance` leaves the guest running unless a breakpoint stopped it, so the
# shape below is the same one tests/clipkeep.py uses: advance a little, and if
# the machine is sitting still look at where.

# THE FOUR REFUSALS BETWEEN wm_su_try AND wm_su_occl, in the order they
# stand. This row's own banner said the call "sits in wm_su_try past every
# refusal in wm_su_ck", and that is not what the source says: `wm_su_ck`,
# `wm_su_vset`, `wm_su_ck` again and `wm_su_scrset` each answer CF and each
# `jc wm_su_tno`. So there are FOUR ways for the count to be zero, they want
# different fixes, and every one of them leaves the glass CORRECT - the
# fallback is W_PAINT and a full repaint, which is exactly the signature the
# failing runs show: `0 wm_su_occl call(s)` beside `0 subpixels differ`.
#
# Naming the one that fired is the difference between a report that can be
# acted on and "this leg proved nothing". They are all reached with DI still
# holding the window record, so the same filter that counts the hit sorts the
# path.
SU_PATH = ("wm_su_ck", "wm_su_vset", "wm_su_scrset", "wm_su_occl", "wm_su_tno")


def serve(m, at, di, hits, names=None, trace=None):
    """One turn of the pump. True if the guest was found stopped.

    `names` maps a flat address to what is there; when it is given, every stop
    whose DI is ours is appended to `trace`. That is the only way a zero here
    says anything: the count alone cannot tell a cache that was never claimed
    from a rect whose display moved from a scratch that would not fit.
    """
    if not m.stopped():
        return False
    r = m.regs()
    ip = ((r["cs"] & 0xFFFF) << 4) + (r["ip"] & 0xFFFF)
    if (r["di"] & 0xFFFF) == di:
        if ip == at:
            hits[0] += 1
        if names is not None and ip in names and trace is not None:
            trace.append(names[ip])
    m.run()
    return True


def pump(m, at, di, hits, rounds=24, frames=8, names=None, trace=None):
    """Give the guest `rounds * frames` frames, servicing stops as they come.

    **A SERVICED STOP USED TO COST A ROUND**, so the guest got LESS time
    exactly when more was happening - `for _ in range(rounds): if not serve():
    advance()`. That is not a guest-anchored budget at all: it is
    `rounds` minus however many breakpoints fired, which is a property of the
    box's load and of what else is on screen.

    It cost this row its REPAIR leg. In a three-wide emulator lane it read
    `0 wm_su_occl call(s)` with `0 subpixels differ` - the repair itself
    perfect, the call that performs it never observed - and
    tests/dispcells.py's `serve` already warns that stops go missing here and
    that "a zero from a single pass" means NOT SEEN. Counting only the rounds
    that advanced makes the window a fixed amount of the machine's own time.
    """
    left = rounds
    while left > 0:
        if not serve(m, at, di, hits, names, trace):
            m.advance(frames=frames)
            left -= 1


def edge(m, mo, down, at, di, hits, tries=240):
    """One button edge with `at` armed, proven against the guest's mouse_btn.

    os88mouse._edge's contract, with the breakpoint serviced between polls -
    and the packet re-sent on the same schedule, because a level packet is
    idempotent and one clocked into the UART while the machine is parked at a
    breakpoint is simply gone."""
    want = 1 if down else 0
    for i in range(tries):
        if i % 30 == 0:
            m.mouse(0, 0, l=down)
        if serve(m, at, di, hits):
            continue
        if (mo.where()[2] & 1) == want:
            return True
        m.advance(frames=4)
    return False


def watch_click(m, mo, x, y, sym, di, rounds=90, path=None):
    """Click (x, y) with `sym` armed; answer how many hits named DI.

    `path` is a list the refusal trace is appended to when SU_PATH is armed
    alongside `sym` - see `serve`. Arming the extra four costs the pump
    nothing it was not already paying: a stop that is not ours is serviced
    and does not spend a round.
    """
    mo.to(x, y)
    if mo.where()[2] & 1:
        mo.click(*dispcorner.PARK)
        mo.to(x, y)
    hits = [0]
    at = m.sym(sym)
    names, arm = None, [sym]
    if path is not None:
        arm = list(SU_PATH)
        names = {m.sym(n): n for n in arm}
    m.bp_exec(*arm)
    m.run()
    ok = edge(m, mo, True, at, di, hits)
    ok = edge(m, mo, False, at, di, hits) and ok
    pump(m, at, di, hits, rounds=rounds, names=names, trace=path)
    m.breakpoints([])
    m.run()
    if not ok:
        fails.append("SETUP: the press/release at (%d,%d) was never decoded "
                     "with %s armed" % (x, y, sym))
    return hits[0]


def drops(m, ours, rounds=POLLS, frames=110):
    """wm_su_drop calls for OUR window across `rounds` worker intervals."""
    at = m.sym("wm_su_drop")
    m.bp_exec("wm_su_drop")
    mine = 0
    for _ in range(rounds):
        m.run()
        for _ in range(40):
            if not m.stopped():
                m.advance(frames=frames // 8)
            if not m.stopped():
                continue
            r = m.regs()
            if ((r["cs"] & 0xFFFF) << 4) + (r["ip"] & 0xFFFF) != at:
                break
            if (r["bx"] & 0xFFFF) == ours:
                mine += 1
            m.run()
    m.breakpoints([])
    m.run()
    return mine


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    os88marty.no_saver(m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    dx, dy, _, _ = dispcp.win_rect(m, S, disk)
    dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "SYSTEM")
    dispcp.open_named(m, mo, S, tick, dx, dy, "TASKMGR.O88")
    tick(m)
    got, i = None, 0
    while dispapps.pkg_seg(m, i) is not None:
        got, i = dispapps.pkg_seg(m, i), i + 1
    if got is None:
        sys.exit("the Task Manager did not open - %r" % os88geom.windows(m))
    slot, seg = got
    w = win(m, slot)
    if "Task" not in w.title:
        sys.exit("the newest package window is %r" % w.title)
    ours = S("wm_wins") - (KERNEL_SEG << 4) + slot * os88geom.WIN_SIZE

    # the HEAP page: the densest, and the one that reports claims
    for _ in range(4):
        if view(m, seg) == 2:
            break
        mo.click(w.x + 20, w.y + TITLE_H + 40)      # left of the scroll bar
        mo.to(*dispcorner.PARK)
        tick(m)
    if view(m, seg) != 2:
        sys.exit("could not reach the heap page (view %d)" % view(m, seg))

    # --- PROMISE -----------------------------------------------------------
    w = win(m, slot)
    band = m.read(S("wm_su_ext") + slot * 4, 4)
    print("PROMISE : %s (%d,%d) %dx%d view=%d saveu=%s band=%r"
          % (w.title, w.x, w.y, w.w, w.h, view(m, seg), w.promises, list(band)))
    if not w.promises:
        fails.append("PROMISE: the heap page does not carry WF_SAVEU")
    if band[0] != w.w - 2:
        fails.append("PROMISE: the left band is %d and the content is %d wide "
                     "- a band short of the whole content leaves the app owing "
                     "a strip, not the EMPTY rect 28.11 turns on"
                     % (band[0], w.w - 2))

    # Navigate the cover to APPS while it is still small and reachable, so
    # the only thing left to do over the covered window is open a package.
    raise_win(m, mo, disk, dx + 30, dy + 9, "the drive window")
    dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "..")
    dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "APPS", paint=True)

    # --- KEPT --------------------------------------------------------------
    raise_win(m, mo, slot, w.x + w.w - 24, w.y + 9, "the Task Manager")
    cw, _, before = m.fbuf()
    was = rect(before, cw, w)
    mo.dblclick(dx + 40, dy + 9)                    # zoom the cover over it
    mo.to(*dispcorner.PARK)
    tick(m)
    big = win(m, disk)
    covered = wholly(big, w)
    mine = drops(m, ours)
    print("KEPT    : wholly=%s - %d wm_su_drop calls for us across %d polls"
          % (covered, mine, POLLS))
    if not covered:
        fails.append("KEPT: the zoomed cover (%d,%d) %dx%d does not wholly "
                     "cover the Task Manager" % (big.x, big.y, big.w, big.h))
    if mine:
        fails.append("KEPT: %d wm_su_drop calls - 28.8's loop is still "
                     "closing, or wm_clip_set is dropping it" % mine)

    # --- REPAIR ------------------------------------------------------------
    # Move the ground under it: a package opened and closed again is an
    # instance in and out of the table and several claims in and out of the
    # heap, which is exactly what these two pages draw from.
    dispcp.open_named(m, mo, S, os88marty.settle, big.x, big.y, "HELLO.O88")
    hello = [x for x in os88geom.windows(m) if "ello" in x.title]
    if not hello:
        fails.append("REPAIR: Hello did not open, so nothing moved")
    else:
        mo.click(hello[0].x + 8, hello[0].y + 9)    # its close box
        mo.to(*dispcorner.PARK)
        os88marty.settle(m)
        if [x for x in os88geom.windows(m) if "ello" in x.title]:
            fails.append("REPAIR: Hello did not close")

    # **WHAT THE CACHE LOOKED LIKE THE INSTANT BEFORE THE UNCOVER**, which is
    # the one reading that tells a REPAIR failure's two causes apart and the
    # row did not have. `0 wm_su_occl` means either the cache was GONE by
    # here - shed, dropped, withdrawn - or it was live and the call was
    # MISSED, and those want opposite fixes. Two guest reads; it costs a
    # failing run nothing and a passing one nothing worth measuring.
    ww = win(m, slot)
    bb = m.read(S("wm_su_ext") + slot * 4, 4)
    print("BEFORE  : saveu=%s band=%r view=%d"
          % (ww.promises if ww else None, list(bb), view(m, seg)))
    path = []
    hits = watch_click(m, mo, big.x + 8, big.y + 9, "wm_su_occl", ours,
                       path=path)
    mo.to(*dispcorner.PARK)
    tick(m)
    if win(m, disk) and win(m, disk).visible:
        fails.append("REPAIR: the cover did not close, so nothing was "
                     "uncovered")
    cw, _, restored = m.fbuf()
    now = rect(restored, cw, w)

    saved = []
    for x in dispcp.win_list(m, S):
        a = S("wm_wins") + x * os88geom.WIN_SIZE + os88geom.W_FLAGS
        f = m.read(a, 2)
        saved.append((a, f))
        m.write(a, bytes([f[0] & ~0x20 & 0xFF, f[1]]))
    m.write(S("cp_dirty"), b"\x01")
    tick(m)
    os88marty.settle(m)
    for a, f in saved:
        m.write(a, f)
    _, _, honest = m.fbuf()
    diff = sum(1 for a, b in zip(now, rect(honest, cw, w)) if a != b)
    moved = sum(1 for a, b in zip(was, now) if a != b)
    print("REPAIR  : %d wm_su_occl call(s) for us at the uncover - %d "
          "subpixels differ against a forced full repaint (%d moved while it "
          "was covered)" % (hits, diff, moved))
    print("REPAIR  : the path for us was %s"
          % (" -> ".join(path) if path else "(nothing - wm_su_try was never "
                                            "entered for this window)"))
    if not hits:
        # WHICH REFUSAL, because there are four and they want different
        # fixes. The trace's last entry before wm_su_tno is the one that
        # answered CF: wm_su_ck is the CLAIM (shed, or a header that does not
        # match), wm_su_vset the rect against the displays (SPEC.md 39.14.8.1),
        # wm_su_scrset the edge scratch. Nothing at all means wm_su_try was
        # not entered, which is a different question again - the window was
        # not the outgoing front, or nothing uncovered it.
        why = path[-2] if len(path) > 1 and path[-1] == "wm_su_tno" \
            else (path[-1] if path else None)
        fails.append("REPAIR: wm_su_occl was never reached for this window, "
                     "so there was no cache to restore and W_PAINT was told "
                     "WHOLE - this leg proved nothing (SPEC.md 11.96.18.1 on "
                     "why the cache word is not the instrument). The path was "
                     "%s, so the refusal is %s"
                     % (" -> ".join(path) or "EMPTY",
                        why or "before wm_su_try was entered at all"))
    if diff:
        fails.append("REPAIR: %d subpixels differ - tm_update did not replay "
                     "everything that moved while it was covered" % diff)
    if not moved:
        print("        (nothing on the page moved, so the repair had nothing "
              "to replay - the leg is weaker than it reads)")

    # --- LIVE --------------------------------------------------------------
    w = win(m, slot)
    for _ in range(4):
        if view(m, seg) == 0:
            break
        mo.click(w.x + 20, w.y + TITLE_H + 40)
        mo.to(*dispcorner.PARK)
        tick(m)
    band = m.read(S("wm_su_ext") + slot * 4, 4)
    print("LIVE    : view=%d saveu=%s band=%r"
          % (view(m, seg), win(m, slot).promises, list(band)))
    if view(m, seg) != 0:
        fails.append("LIVE: could not get back to the performance view")
    elif win(m, slot).promises:
        fails.append("LIVE: the load meter's page still promises")
    elif band[0]:
        fails.append("LIVE: the band was left named on the live page")

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  it repairs itself at the restore")
