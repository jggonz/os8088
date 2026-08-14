#!/usr/bin/env python3
"""WHO DRAWS INTO .cold? (docs/DUAL-DISPLAY-VGA.md 8(11))

    make && python3 tests/dispcold.py

The field's freeze-or-reboot on an extended desktop is not a logic bug in
anything it looked like: `.cold` is being WRITTEN TO. It is byte-identical to
the built image at the desktop, and one click on a Disk window with a
straddling window over it leaves 34 differing runs in it - at 0x4611, 0x4613,
0x4661, 0x4663, 0x46B1, 0x46B3, 0x4701, 0x4703 ... **stride 0x50**, which is
80 bytes, which is a 640-pixel 1bpp scan line. Something is drawing through a
segment that is COLD_SEG, and what it lands on is `fm_choose`, `fm_find_view`,
`fm_front_win`, `fm_bar_gate_x` and `fm_layout` - the file manager's own code.
Everything downstream (a `call far` turned into `cbw`, DS going to 0, garbage
written over ui_rebootq, the reboot, the freeze) is that code being executed.

.cold is the right place to catch it because SPEC.md 2.6 rule 1 forbids a data
directive there and tools/os88ovlchk.py enforces it: every byte of it is code,
so ANY write is the bug. This carpets a span of it with memory breakpoints -
one per byte, which is what a bus-access breakpoint costs - and reports the
first write with the live video block beside it, because the question is which
segment the renderer thought it was drawing into.

A code FETCH does not trip these: MEM_BPA_BIT is checked in biu_bus_begin,
which asserts the cycle is not a CodeFetch. So executing the carpeted code is
free and only a data write (or read) stops it.
"""
import sys, time
sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88marty, os88mouse, os88sym, dispcp
S = os88sym.linear
SY = os88sym.syms()
SEC = os88sym.sections()
COLD_SEG = 0x0E00
TITLE_H = 18
LO, HI = 0x4000, 0x5000         # the span every observed hit fell inside


def u16(b, i=0): return b[i] | (b[i + 1] << 8)


def hexd(b): return " ".join("%02X" % c for c in b)


def coldbase():
    k = open("build/kernel.bin", "rb").read()
    off = SY["fm_layout"]
    for base in range(0xC000, len(k) - 0x8900, 2):
        if k[base + off:base + off + 4] == b"\x50\x51\x52\x57":
            return base, k
    raise RuntimeError("cannot locate .cold inside kernel.bin")


def coldname(off):
    near = sorted((v, n) for n, v in SY.items()
                  if SEC.get(n) == ".cold" and v <= off)
    return "%s+%d" % (near[-1][1], off - near[-1][0]) if near else "?"


def video(m):
    """The live video block - the answer to 'which segment did it think it
    was drawing into', which is the whole question here."""
    out = []
    for n in ("vid_seg", "vid_rseg", "vid_stride", "vid_w", "vid_h",
              "vid_pw", "vid_ph"):
        out.append("%s=%04X" % (n[4:], u16(m.read(S(n), 2))))
    for n in ("vid_cur", "vid_kind", "vid_mono", "vid_planes"):
        out.append("%s=%02X" % (n[4:], m.read(S(n), 1)[0]))
    return " ".join(out)


def pump(m, mo, px, py, secs=30.0):
    """Position, click, and run until either a write into .cold stops the
    machine or the click has plainly finished. Returns the regs dict on a
    catch, None otherwise."""
    mo.to(px, py)
    os88marty.settle(m, card=0)
    m.mouse(0, 0, l=True)
    t0 = time.time()
    up = False
    while time.time() - t0 < secs:
        st = m.status()
        if st["state"] != "breakpoint":
            time.sleep(0.01)
            if not up and time.time() - t0 > 0.6:
                m.mouse(0, 0, l=False)
                up = True
            continue
        return m.cmd(cmd="regs")
    if not up:
        m.mouse(0, 0, l=False)
    return None


def main():
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine="os8088_xt_vga_herc", boot=False) as m:
        cards = {c["idx"]: c for c in m.cards()}
        pri = [i for i, c in cards.items() if c["type"] == "vga"][0]
        sec = [i for i, c in cards.items() if c["type"] == "mda"][0]
        m.run(); os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        ctx = m.read(S("vid_ctx"), 84); seam = u16(ctx, 42 + 36)
        print("seam at x=%d" % seam)
        print("video (desktop): %s" % video(m))

        dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri)
        w = dispcp.win_list(m, S); disk = w[-1]
        bx, by, bw, bh = dispcp.win_rect(m, S, disk)
        dispcp.open_row(m, mo, S, os88marty.settle, bx, by, 0, card=pri)
        bx, by, bw, bh = dispcp.win_rect(m, S, disk)
        dispcp.open_row(m, mo, S, os88marty.settle, bx, by, 5, card=pri)
        time.sleep(2)
        wins = dispcp.win_list(m, S)
        if len(wins) < 2:
            sys.exit("notepad did not launch")
        np = [s for s in wins if s != disk][-1]
        nx, ny, nw, nh = dispcp.win_rect(m, S, np)
        mo.drag(nx + nw // 2, ny + TITLE_H // 2,
                ((seam - nw // 2) & ~7) + nw // 2, ny + TITLE_H // 2)
        os88marty.settle(m, card=sec)
        nx, ny, nw, nh = dispcp.win_rect(m, S, np)
        print("notepad x %d..%d (seam %d) %s"
              % (nx, nx + nw - 1, seam,
                 "STRADDLES" if nx < seam <= nx + nw - 1 else "does NOT"))
        bx, by, bw, bh = dispcp.win_rect(m, S, disk)
        print("disk window x %d..%d y %d..%d" % (bx, bx + bw - 1, by, by + bh - 1))

        base, k = coldbase()
        img = k[base:base + 0x8900]
        if m.read(COLD_SEG << 4, 0x8900) != img:
            sys.exit(".cold is already corrupt before the click")
        print(".cold is byte-identical to the built image; carpeting "
              "COLD_SEG:%04X..%04X with %d memory breakpoints"
              % (LO, HI - 1, HI - LO))

        m.breakpoints([{"type": "mem", "addr": (COLD_SEG << 4) + a}
                       for a in range(LO, HI)])
        clicks = [(bx + bw // 2, by + TITLE_H + 8 + r * 16) for r in
                  (1, 3, 5, 2, 4, 1)]
        for n, (px, py) in enumerate(clicks, 1):
            print("click %d at (%d,%d)" % (n, px, py))
            r = pump(m, mo, px, py)
            if r is None:
                now = m.read(COLD_SEG << 4, 0x8900)
                bad = [i for i in range(len(img)) if img[i] != now[i]]
                print("   no write caught; .cold differs in %d byte(s)"
                      % len(bad))
                if bad:
                    m.breakpoints([])
                    return 1
                continue
            print("\n*** WRITE INTO .cold")
            print("cs=%04X ip=%04X  ds=%04X es=%04X ss=%04X sp=%04X bp=%04X"
                  % (r["cs"], r["ip"], r["ds"], r["es"], r["ss"], r["sp"],
                     r["bp"]))
            print("ax=%04X bx=%04X cx=%04X dx=%04X si=%04X di=%04X"
                  % (r["ax"], r["bx"], r["cx"], r["dx"], r["si"], r["di"]))
            if r["cs"] == COLD_SEG:
                print("in .cold at %s" % coldname(r["ip"]))
            elif r["cs"] == 0x0060:
                near = sorted((v, n) for n, v in SY.items()
                              if SEC.get(n) == ".text" and v <= r["ip"])
                print("in .text at %s+%d" % (near[-1][1], r["ip"] - near[-1][0]))
            print("code @cs:ip-16 : %s"
                  % hexd(m.readseg(r["cs"], max(0, r["ip"] - 16), 32)))
            print("stack words    : %s"
                  % " ".join("%04X" % u16(m.readseg(r["ss"], r["sp"], 32), i)
                             for i in range(0, 32, 2)))
            print("video          : %s" % video(m))
            try:
                print("callstack      : %s" % m.cmd(cmd="callstack")
                      .get("callstack"))
            except Exception as e:
                print("callstack unavailable: %s" % str(e)[:60])
            m.breakpoints([])
            return 1
        m.breakpoints([])
        m.mouse(0, 0, l=False)
        print("no write into COLD_SEG:%04X..%04X caught" % (LO, HI - 1))
        now = m.read(COLD_SEG << 4, 0x8900)
        bad = [i for i in range(len(img)) if img[i] != now[i]]
        print(".cold now differs in %d byte(s)%s" % (len(bad),
              (": " + ", ".join("%04X (%s)" % (b, coldname(b))
                                for b in bad[:12])) if bad else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
