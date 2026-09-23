#!/usr/bin/env python3
"""Does the dock stand on every edge, hide, open and close? (SPEC.md 30.5,
30.6, 31.13)

    make && python3 tests/dockpos.py

The strip used to be pinned to the bottom, and twenty sites read
`[vid_dock_y0]` as "where the strip is". SPEC.md 30.5 lets it stand on the
left or the right and 30.6 lets it hide, so every one of those sites now has
an x fence or a rect test instead - and a site that was missed does not fail,
it draws. So this drives the Control Panel's Dock page on a 5150 with a
Hercules card and, for every setting, asserts three things:

  1. the GEOMETRY the kernel published - the band's x fence and its last row,
     read out of the guest, against SPEC.md 30.5's table for 720x348;
  2. the STRIP is where the table says - the rule's column or row is ink from
     end to end, or, while it hides, the outer edge's one line is;
  3. the glass agrees with a forced full repaint (docs/history/WM-ARTIFACTS.md
     0, the dockmark row's method): a change that left the old strip on the
     screen, or dithered over the new one, is a difference here.

...and then the HIDE, which no screenshot of a settled screen can show:

  4. a pointer resting on the right-hand line opens the strip ([dock_up] = 1)
     and the rule is drawn OVER the desktop;
  5. moved off, it is STILL open a moment later (the 0.75 s linger, SPEC.md 30.6)
     and closed after it - and the screen is then pixel-identical to the one
     before it opened, because the damage repaint restored it;
  6. a press outside the open strip is not eaten: it lands on the Dock page's
     Bottom radio, so the setting moves.

...and that the OPEN strip holds nothing up (SPEC.md 30.6.1): the gfx lock
must be seen FREE while it is open, because the strip stays on top through
the clip region rather than by keeping every other task off the screen.

`--cga` adds SPEC.md 30.5.1 on a 5150/CGA: 180 rows hold seven tiles down a
side, so Left is accepted and the strip holds seven.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88ui
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

S = os88sym.linear
TITLE_H, MBAR_H = 18, 20
CP_RX = 96
CPK_R0Y, CPK_ROWH, CPK_AY = 18, 16, 74      # ctrl.inc's Dock page
DOCK_H, DOCK_SW = 24, 32
DOCK_LEAVE_T = 14                           # dock.inc: 0.77 s of ticks
P_BOTTOM, P_LEFT, P_RIGHT, F_AUTO = 0, 1, 2, 4
FAIL = []


def check(ok, what):
    print("   %-62s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def u16(b):
    return b[0] | (b[1] << 8)


def word(m, name):
    return u16(m.read(S(name), 2))


def byte(m, name):
    return m.read(S(name), 1)[0]


def mbase(m):
    """DOCK.DRV's live segment - SPEC.md 30.5 keeps the advanced Dock's own
    geometry (the whole-strip rect, the along axis, the cap, the packing
    table and the hover timer) inside the MODULE IMAGE, so a machine with no
    module mounted carries none of it. os88sym refuses a kernel-segment read
    of one rather than answering with a plausible wrong address, which is why
    these two helpers exist."""
    seg = word(m, "mod_r_dock")
    assert seg, "DOCK.DRV is not mounted - nothing to read"
    return seg * 16


def mbyte(m, name):
    return m.read(mbase(m) + os88sym.syms()[name], 1)[0]


def hidden(m):
    """Is the strip HIDDEN? A machine with no DOCK.DRV mounted cannot hide
    one at all (SPEC.md 30.6), and its [dock_hidden] does not exist - so the
    basic bottom Dock answers 0 without a read."""
    return mbyte(m, "dock_hidden") if word(m, "mod_r_dock") else 0


def mword(m, name):
    return u16(m.read(mbase(m) + os88sym.syms()[name], 2))


def frame(m, kind):
    w, h, rows = m.vram(kind)
    return w, h, bytes(b for r in rows for b in r)


def full_repaint(m):
    """Make the GUEST repaint the whole screen ([cp_dirty] is exactly that)."""
    m.write(S("cp_dirty"), b"\x01")
    for _ in range(400):
        time.sleep(0.05)
        if byte(m, "cp_dirty") == 0:
            break
    else:
        raise RuntimeError("ui_task never drained [cp_dirty]")
    os88marty.settle(m)


def diff(a, b, w, clk):
    """Differing pixels below the menu bar's clock field."""
    return [i for i in range(min(len(a), len(b))) if a[i] != b[i]
            and not (i // w < MBAR_H and i % w >= clk)]


def pane(m):
    wx, wy = dispcp._cp_win(m, S)
    return wx + 1 + CP_RX, wy + TITLE_H + 1


def click_row(m, mo, y):
    px, py = pane(m)
    mo.click(px + 4 + 6, py + y + 6, settle=0)
    os88marty.settle(m)


def expect(pw, ph, cfg):
    """SPEC.md 30.5's table: (band_x0, band_xe, dock_y0, hidden)."""
    side, auto = cfg & 3, bool(cfg & F_AUTO)
    t = 1 if auto else (DOCK_H if side == P_BOTTOM else DOCK_SW)
    if side == P_BOTTOM:
        return 0, pw, ph - t, int(auto)
    if side == P_LEFT:
        return t, pw, ph, int(auto)
    return 0, pw - t, ph, int(auto)


def strip_ok(px, w, h, cfg):
    """Is the rule (or, hidden, the line) ink from end to end? Ink is 0 on a
    1bpp card - the strip's own ground is lit."""
    side, auto = cfg & 3, bool(cfg & F_AUTO)
    if side == P_BOTTOM:
        y = h - 1 if auto else h - DOCK_H
        return all(px[y * w + x] == 0 for x in range(w))
    if side == P_LEFT:
        x = 0 if auto else DOCK_SW - 1
    else:
        x = w - 1 if auto else w - DOCK_SW
    return all(px[y * w + x] == 0 for y in range(MBAR_H, h))


def setting(m, mo, kind, cfg, want_cfg=None):
    """Drive the page to `cfg` and assert 1-3."""
    before_seg = word(m, "mod_r_dock")
    have = byte(m, "dock_cfg")
    if (have & 3) != (cfg & 3):
        click_row(m, mo, CPK_R0Y + (cfg & 3) * CPK_ROWH)
    if (byte(m, "dock_cfg") & F_AUTO) != (cfg & F_AUTO):
        click_row(m, mo, CPK_AY)
    check(bool(word(m, "mod_r_dock")) == bool(cfg),
          "cfg %d: Dock module %s" % (cfg, "loaded" if cfg else "unloaded"))
    seg = word(m, "mod_r_dock")
    eq = os88sym.equates()
    m.pause()
    try:
        tab = m.read(S("mem_tab"), eq["MEM_MAX"] * eq["MC_SIZE"])
    finally:
        m.run()
    claims = {}
    for off in range(0, len(tab), eq["MC_SIZE"]):
        row = tab[off:off + eq["MC_SIZE"]]
        claims[u16(row[eq["MC_SEG"]:])] = (
            u16(row[eq["MC_PARA"]:]), u16(row[eq["MC_OWN"]:]))
    if seg:
        paras = ((eq["MODK_SIZE"] + 1023) // 1024) * 64
        check(claims.get(seg) == (paras, eq["MEM_K_MOD"]),
              "advanced Dock has exactly its rounded module claim")
    elif before_seg:
        check(before_seg not in claims, "disabling the Dock frees its old claim")
    got = byte(m, "dock_cfg")
    want = cfg if want_cfg is None else want_cfg
    check(got == want, "cfg %d: [dock_cfg] = %d" % (cfg, got))
    pw, ph = word(m, "vid_pw"), word(m, "vid_ph")
    e = expect(pw, ph, want)
    g = (word(m, "vid_band_x0"), word(m, "vid_band_xe"),
         word(m, "vid_dock_y0"), hidden(m))
    check(g == e, "cfg %d: band x0/xe, dock_y0, hidden = %s (want %s)"
          % (want, g, e))
    os88marty.settle(m)
    mo.to(pw // 2, ph - 60)
    os88marty.settle(m)
    w, h, px = frame(m, kind)
    check(strip_ok(px, w, h, want), "cfg %d: the rule / line is on its edge"
          % want)
    full_repaint(m)
    _, _, after = frame(m, kind)
    d = diff(px, after, w, word(m, "vid_clk_hx"))
    check(not d, "cfg %d: glass == a forced repaint (%d px%s)"
          % (want, len(d), "" if not d else ", x %d..%d y %d..%d"
             % (min(i % w for i in d), max(i % w for i in d),
                min(i // w for i in d), max(i // w for i in d))))


def wait_for(m, name, value, secs):
    end = time.time() + secs
    while time.time() < end:
        if byte(m, name) == value:
            return True
        time.sleep(0.1)
    return False


def herc(a):
    kind = "herc"
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        os88marty.no_saver(m)
        check(word(m, "mod_r_dock") == 0, "basic Dock boots without a module")
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle, page=None)
        wx, wy = dispcp._cp_win(m, S)
        row = byte(m, "cp_nst") - 1     # Dock is the LAST static record and
                                        # is never hidden (SPEC.md 31.13)
        mo.click(wx + 1 + 6 + 30, wy + TITLE_H + 1 + 6 + row * 14 + 7,
                 settle=0)
        os88marty.settle(m)
        print("   panel at (%d,%d), Dock is row %d" % (wx, wy, row))

        for cfg in (P_LEFT, P_RIGHT, P_BOTTOM, P_LEFT | F_AUTO,
                    P_BOTTOM | F_AUTO, P_RIGHT | F_AUTO):
            setting(m, mo, kind, cfg)

        # --- 4..5: the hover, the open strip, the linger ---------------------
        pw, ph = word(m, "vid_pw"), word(m, "vid_ph")
        # Put a real window under the open strip: removing the clipping
        # fence must fail the forced-repaint check below, not pass vacuously
        # against an uncovered desktop.
        ui = os88ui.UI(m, mouse=mo, sym=S)
        panel = ui.window("Control Panel")
        panel = ui.move_window(panel, pw - panel.w - 1, 80)
        # **IT SAYS THE NUMBERS**, because it failed once in a soak and the
        # bare form left the reader nothing at all to go on: `check` here
        # prints the sentence and no values, so a one-line FAIL was the whole
        # of the evidence.
        check(panel.x + panel.w > pw - DOCK_SW,
              "the panel overlaps the strip (x %d w %d, right edge %d, "
              "wanted past %d of %d)"
              % (panel.x, panel.w, panel.x + panel.w, pw - DOCK_SW, pw))
        mo.to(pw // 2, ph - 60)
        os88marty.settle(m)
        w, h, before = frame(m, kind)
        mo.to(pw - 1, ph // 2)
        opened = wait_for(m, "dock_up", 1, 30.0)
        check(opened, "resting on the right-hand line opens the strip")
        if opened:
            free = 0
            for _ in range(20):
                free += byte(m, "gfx_lock_flag") == 0
                time.sleep(0.05)
            check(free > 0, "the gfx lock is free while the strip is open "
                  "(%d of 20 reads)" % free)
            time.sleep(1.0)
            _, _, open_px = frame(m, kind)
            rule = w - DOCK_SW
            check(all(open_px[y * w + rule] == 0 for y in range(MBAR_H, h)),
                  "the open strip's rule is drawn over the desktop")
            full_repaint(m)
            _, _, repainted = frame(m, kind)
            changed = sum(open_px[y * w + x] != repainted[y * w + x]
                          for y in range(MBAR_H, h)
                          for x in range(rule, w))
            check(not changed, "window repaint leaves the open strip intact "
                  "(%d px)" % changed)
            # ONE PACKET off the strip, not the long way home: the harness
            # spends up to a guest second a packet, so the four it takes to
            # reach mid-screen are ~73 ticks - longer than the linger - and
            # the read below would measure the move rather than the close.
            mo.to(pw - DOCK_SW - 16, ph // 2)
            closed = wait_for(m, "dock_up", 0, 60.0)
            check(closed, "moved off, it closes")
            # THE LINGER IN GUEST TICKS, not host seconds: a pointer move
            # through the harness is itself hundreds of guest milliseconds,
            # so "is it still open right after the move" measured MartyPC.
            # [dock_timer_tick] is the tick the pointer was first seen off the
            # strip, and it survives the close.
            gone = word(m, "ticks") - mword(m, "dock_timer_tick")
            check(DOCK_LEAVE_T <= (gone & 0xFFFF) <= DOCK_LEAVE_T + 40,
                  "...%d ticks after it left (the 0.75 s linger is %d)"
                  % (gone & 0xFFFF, DOCK_LEAVE_T))
            mo.to(pw // 2, ph - 60)
            os88marty.settle(m)
            _, _, after = frame(m, kind)
            d = diff(before, after, w, word(m, "vid_clk_hx"))
            check(not d, "closed, the screen is what it was (%d px)" % len(d))

            # --- 6: a press outside the open strip is not eaten ----------------
            mo.to(pw - 1, ph // 2)
            if wait_for(m, "dock_up", 1, 30.0):
                px, py = pane(m)
                mo.click(px + 10, py + CPK_R0Y + 6, settle=0)
                wait_for(m, "dock_up", 0, 30.0)
                os88marty.settle(m)
                check(byte(m, "dock_cfg") == (P_BOTTOM | F_AUTO),
                      "a press outside the open strip reaches the page")
            else:
                check(False, "the strip opened a second time")

        setting(m, mo, kind, P_BOTTOM)


def cga(a):
    with os88marty.launch(a.image, apps=a.apps, machine=os88marty.machine("os8088_5150_cga"),
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        os88marty.no_saver(m)
        check(word(m, "mod_r_dock") == 0, "basic Dock boots without a module")
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle, page=None)
        wx, wy = dispcp._cp_win(m, S)
        row = byte(m, "cp_nst") - 1
        mo.click(wx + 1 + 6 + 30, wy + TITLE_H + 1 + 6 + row * 14 + 7,
                 settle=0)
        os88marty.settle(m)
        click_row(m, mo, CPK_R0Y + CPK_ROWH)            # Left
        check(byte(m, "dock_cfg") == P_LEFT, "CGA: Left is accepted")
        check(byte(m, "dock_side") == P_LEFT and
              word(m, "vid_band_x0") == DOCK_SW,
              "CGA: the strip stands on the left")
        capacity = min(7, os88sym.equates()["INST_MAX"])
        check(mbyte(m, "dock_cap") == capacity,
              "CGA: capacity %d (%d)" % (capacity, mbyte(m, "dock_cap")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--cga", action="store_true",
                    help="also run SPEC.md 30.5.1's refusal on a 5150/CGA")
    a = ap.parse_args()
    herc(a)
    if a.cga:
        cga(a)
    print("dockpos: %s" % ("FAILED: " + "; ".join(FAIL) if FAIL else "ok"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
