#!/usr/bin/env python3
"""Does a capsule the blit REFUSED leave a streak behind it? (SPEC.md 44.10.6.2)

    make && python3 tests/arkpuwipe.py                      # trigger B
    make small && python3 tests/arkpuwipe.py --small \
        --img build/small360.img                           # trigger A

**IT RUNS ON VGA, AND THAT IS NOT A PREFERENCE.** `ark_scale_vel` floors
`ARK_PUFALL` at 1 on CGA (`ark_met_sml`'s velocity scale is 37), so the strip a
capsule vacates there is ONE row - the capsule's own top EDGE, which is
`CBLACK` on a playfield that is also `CBLACK`. The defect is fully present and
draws nothing a lit/unlit comparison can see. On VGA and Hercules the scale is
100, `ARK_PUFALL` is 2, and the second vacated row is BODY - so the streak is a
lit column and the measurement has something to measure. A run on CGA scores the
broken build at zero, which is the trap this paragraph exists to stop.

44.10.6's band carries the erase for the strip a capsule vacated, so
`ark_wipe_pu` must not erase it as well. It used to decide that from
`[ark_spok]` - "the sprites are composed" - which is a statement about the
PACKAGE and not about the kernel, and every path where `gfx_blit1` REFUSES then
had its erase owned by nobody: the two rows `ARK_PUFALL` vacates each frame
stayed on the glass for the rest of the level.

THE MEASUREMENT IS A REPAINT DIFF, cycweb.py's: drop one capsule down a frozen
playfield, capture, then force `ark_draw_all` and capture again. **Every pixel
that differs** is residue - in BOTH directions, which is the half worth writing
down. A leftover row is lit ink over background where the capsule has passed
onto bare playfield, and it is a leftover BLACK row of the previous frame's
letter sitting inside the current body where it has not; the first reads as an
extra pixel and the second as a missing one, so a test that counted only extras
scored the identical defect at zero. Nothing here counts frames or trusts a
colour - a full repaint is what the screen SHOULD look like, by construction.

`puband` in the output is the state that decides it, read off the guest: 1 on
the aligned drop (the blit took the band and owns the erase), 0 on the unaligned
one (it refused, `.slow` drew, and the wipe owns the erase again) and 1 on the
forced column, which is the whole defect in one digit - a refused blit with the
flag still claiming the band erased.

Three columns, all in one run and off one binary, so no part of this rests on
comparing two builds:

  aligned    the capsule's absolute x on the byte grid - the blit TAKES it,
             the band owns the erase, and the wipe must stay out of the way
  unaligned  the same capsule one pixel over: `gfx_blit1_x` refuses any x with
             `test ax, 7` non-zero, which is TRIGGER B - the shipped kernel
             once the window's content origin stops being 8-aligned
  forced     the same unaligned drop with `.slow`'s clear of `[ark_puband]`
             patched to a SET in the running image, so both draw paths raise it
             and nothing lowers it - which is exactly what reading `[ark_spok]`
             amounted to. This is the "before" column, and it is the defect
             reproduced rather than remembered

TRIGGER A - every kern_small machine, where `gfx_blit1` is `stc`/`ret` and so
refuses on EVERY capsule - is the same measurement with `--img build/small.img`:
the `aligned` column there behaves like `unaligned` here, because the refusal is
the kernel's rather than the x's.

The playfield is frozen the way cycweb.py freezes the wave - `ark_do_ball`,
`ark_do_launch` and `ark_do_shots` stubbed to a `ret` in the running image -
because a ball crossing the capsule's column is the one thing that repaints the
streak away for free, and the run would then measure the ball.
"""
import sys, os, time, argparse, tempfile, subprocess

# THIS TREE'S tools, not /home/user/os8088's. Every other test here hard-codes
# that path, which is right in the checkout it was written in and wrong in a
# worktree: os88sym re-assembles ROOT/kernel/kernel.asm and compares it against
# ROOT/build/kernel.bin, so a hard-coded ROOT resolves symbols out of a
# DIFFERENT kernel from the image being booted. The imports are ordered so that
# these win: dispcp and cycweb insert the absolute path when they load, and
# whatever is already in sys.modules by then stays.
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, "tools"))
sys.path.insert(0, os.path.join(_ROOT, "tests"))
import os88marty, os88mouse, os88sym, os88geom
import dispcp
from cycweb import Pkg, u16, diff, bbox, shot

ARK_MAXPU = 3
ARK_PUW = 12
ARK_PUH = 12


def pkg_syms(src="apps/arkanoid/arkanoid.asm", incs=("apps/",)):
    """The package's symbols AND its image, by re-assembling it (cycweb.py)."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        bp = os.path.join(d, "p.bin")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        cmd = ["nasm", "-f", "bin", "-w+error"]
        for i in incs:
            cmd += ["-I", i]
        cmd += ["-o", bp, cp]
        subprocess.run(cmd, check=True)
        syms = {}
        for ln in open(mp):
            f = ln.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                syms[f[2]] = int(f[0], 16)
        return syms, open(bp, "rb").read()


def find_win(m, S, prefix, stride):
    """The window whose title starts with `prefix`, at THIS kernel's stride.

    os88geom hard-codes WIN_SIZE = 34, which is kern_big's. kern_small's record
    is 28 bytes (SPEC.md 13.7/13.9 are not in it), so every slot but 0 decodes
    as garbage there - slot 1 reads flags 0xD0, no WF_USED, and a running app
    is reported as "it did not launch". The first 28 bytes are the same record
    in both, so the stride is the whole of the difference.
    """
    import struct
    raw = m.read(S("wm_wins"), os88geom.MAX_WIN * stride)
    for i in range(os88geom.MAX_WIN):
        b = i * stride
        fl = struct.unpack_from("<H", raw, b + os88geom.W_FLAGS)[0]
        if not (fl & os88geom.WF_USED):
            continue
        seg = struct.unpack_from("<H", raw, b + os88geom.W_SEG)[0]
        tp = struct.unpack_from("<H", raw, b + os88geom.W_TITLE)[0]
        t = bytes(m.readseg(seg or 0x60, tp, 24)).split(b"\0")[0] \
            .decode("latin-1")
        if t.lower().startswith(prefix):
            x, y, w, h = struct.unpack_from("<HHHH", raw, b + os88geom.W_X)
            return t, seg, (x + 1, y + os88geom.TITLE_H,
                            x + w - 2, y + h - 2)
    return None, None, None


def stable_shot(m, card, tries=6):
    """A capture the screen agrees with twice.

    `settle` says two RENDERED frames a second apart matched, which is not
    quite the same as "the guest is between renders": Arkanoid redraws the
    capsule every tick, and a capture that lands mid-draw catches the body laid
    and the letter not yet - about one run in eight, one glyph cell of
    "residue", in whichever column got unlucky. Two identical reads in a row
    is the cheap fix and it costs nothing when the screen is genuinely still.
    """
    last = shot(m, card=card)
    for _ in range(tries):
        os88marty.settle(m)
        cur = shot(m, card=card)
        if cur == last:
            return cur
        last = cur
    return last


def freeze(m, p):
    """Nothing moves but the capsule."""
    # ark_draw_msg goes too, and it is not tidiness. "SPACE TO SERVE" is
    # centred in the content and the capsule falls through its first glyph: the
    # erase takes a bite out of the banner, ark_draw_all puts it back, and the
    # diff scores ~14 px of scenery in whichever column the phase happened to
    # clip it. Stubbed BEFORE the first repaint, the banner is off the glass for
    # the whole run and stays off.
    for name in ("ark_do_ball", "ark_do_launch", "ark_do_shots",
                 "ark_do_paddle", "ark_draw_msg"):
        if name in p.s:
            m.write(p.addr(name), b"\xc3")


def plant(m, p, ox, skew):
    """One capsule, slot 0, at a content x whose ABSOLUTE x is (mis)aligned."""
    for s in range(ARK_MAXPU):                  # every other slot empty
        m.write(p.addr("ark_pukind") + s, b"\x00")
        m.write(p.addr("ark_puwipe") + s, b"\x00")
        if "ark_puband" in p.s:
            m.write(p.addr("ark_puband") + s, b"\x00")
    x = ((p.rw("ark_rail") + 24 + ox + 7) & ~7) - ox + skew
    # BELOW THE WALL, on bare background. Dropped through the bricks instead,
    # a capsule left on the glass is DARK over lit brick - so the residue shows
    # up as "missing" rather than "extra" and the sign of the whole measurement
    # flips with the scenery. On ARK_BG the leftover ink is the only thing there.
    y = p.rw("ark_bricky") + p.rw("ark_rows") * p.rw("ark_bh") + 4
    m.write(p.addr("ark_pukind"), b"\x01")      # PU_EXPAND, kind 1
    p.ww("ark_pux", x)
    p.ww("ark_puy", y)
    p.ww("ark_puold", y)
    return x, y


def old_flag(m, p, image, on):
    """Put the OLD reading back, in the running image.

    Not by writing `[ark_puband]` from the host between frames: the draw clears
    it again on the very next tick, so a poke per step reproduces nothing. The
    old code had ONE flag that `ark_pu_compose` set at level start and nothing
    ever cleared, so the faithful reproduction is to make `.slow`'s clear a SET
    - one immediate byte in `mov byte [ark_puband+si], 0`. Then both draw paths
    set it, which is exactly what reading `[ark_spok]` amounted to.
    """
    import struct
    pat = b"\xC6\x84" + struct.pack("<H", p.s["ark_puband"])
    at = image.find(pat + b"\x00")
    if at < 0 or image.find(pat + b"\x00", at + 1) >= 0:
        raise RuntimeError("cannot find .slow's single clear of ark_puband")
    m.write(p.seg * 16 + at + 4, b"\x01" if on else b"\x00")


def run(m, p, frames, steps, stop):
    """Let the capsule fall, and never as far as the paddle."""
    for _ in range(steps):
        if p.rw("ark_puy") >= stop or p.rb("ark_pukind") == 0:
            break
        m.advance(frames=frames)
    return p.rw("ark_puy")


def repaint(m, p, frames):
    """ark_full = 1: the next ark_render draws the whole content."""
    p.wb("ark_full", 1)
    m.advance(frames=frames * 3)


def column(m, p, ox, skew, force_band, frames, steps, card, fall,
           image, tag):
    old_flag(m, p, image, force_band)
    x, y0 = plant(m, p, ox, skew)
    stop = p.rw("ark_pady") - ARK_PUH - 8
    y1 = run(m, p, frames, steps, stop)
    band = p.rb("ark_puband")
    # STOP THE FALL BEFORE CAPTURING. The two shots are a whole repaint apart,
    # and a capsule that moves between them is a capsule-shaped "streak" plus a
    # capsule-shaped "missing" in every column, whatever the wipe did. Measured:
    # 48/22/22 extra and 137/90/90 missing, all three columns identical - which
    # is the instrument photographing its own motion blur.
    p.ww("ark_pufall", 0)
    m.advance(frames=frames * 2)
    before = stable_shot(m, card)
    repaint(m, p, frames)
    after = stable_shot(m, card)
    # THE CORRIDOR THE CAPSULE HAS LEFT BEHIND, and not the capsule itself.
    # The `.slow` path still writes the body and then the mark on top of it, so
    # a capsule sitting there has a letter-less instant on every tick - that is
    # 44.10.6's ORIGINAL defect, which is exactly what the band exists to remove
    # and exactly what the fallback still has by design. Photographed, it is a
    # heart-shaped 30 px of "residue" in whichever `.slow` column the sampling
    # caught mid-draw, and it says nothing about who owns the erase. What this
    # measures is the strip ABOVE the capsule: everything from where it started
    # to one row above where it stopped.
    oy = p.rw("ark_oy")
    corridor = (x + ox - 1, y0 + oy, x + ox + ARK_PUW, y1 + oy - 1)
    miss, extra = diff(before, after, corridor)
    if miss or extra:
        for nm, sh in (("before", before), ("after", after)):
            w, h, px = sh
            rows = [bytes(255 if px[y * w + x] else 0 for x in range(w))
                    for y in range(h)]
            os88marty.write_png("build/ark_%s_%s.png" % (tag, nm), w, h, rows)
    # clear the slot and repaint, so the next column starts on clean glass
    m.write(p.addr("ark_pukind"), b"\x00")
    m.write(p.addr("ark_puwipe"), b"\x00")
    p.ww("ark_pufall", fall)
    old_flag(m, p, image, False)
    repaint(m, p, frames)
    return miss, extra, y0, y1, band


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga",
                    help="VGA by default - see the header: on CGA the vacated "
                         "strip is one BLACK row on a BLACK playfield and the "
                         "defect is invisible to any pixel diff")
    ap.add_argument("--img", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--src", default="apps/arkanoid/arkanoid.asm")
    ap.add_argument("--frames", type=int, default=8)
    ap.add_argument("--steps", type=int, default=10)
    ap.add_argument("--small", action="store_true",
                    help="the image is a kern_small one (TRIGGER A): resolve "
                         "kernel symbols with -DKERN_SMALL, and do not check "
                         "them against build/kernel.bin, which is the BIG "
                         "kernel and a different binary on purpose")
    a = ap.parse_args()
    if a.small:
        _s = os88sym.syms(("KERN_SMALL",), check=False)

        def S(name):
            return (os88sym.segment_of(name, ("KERN_SMALL",), check=False) * 16
                    + _s[name])
    else:
        S = os88sym.linear

    with os88marty.launch(a.img, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy, _, _ = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "GAMES")
        wx, wy, _, _ = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        entry = dispcp.row_of(m, S, "ARKANOID.O88")
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy, entry)
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        stride = 28 if a.small else os88geom.WIN_SIZE
        title = seg = box = None
        for _ in range(20):             # kern_small reads the package off a
            time.sleep(2)               # 360KB disk and is slower about it
            title, seg, box = find_win(m, S, "ark", stride)
            if title:
                break
        if title is None:
            w, h, px = m.fbuf()
            os88marty.write_png_rgb("build/arkfail.png", w, h, px)
            import struct
            raw = m.read(S("wm_wins"), os88geom.MAX_WIN * os88geom.WIN_SIZE)
            slots = [(i, hex(struct.unpack_from("<H", raw,
                                                i * os88geom.WIN_SIZE)[0]))
                     for i in range(os88geom.MAX_WIN)]
            raise RuntimeError("no Arkanoid window - it did not launch "
                               "(screen in build/arkfail.png); windows: %s; "
                               "wm_wins=%#x slots %s"
                               % (os88geom.windows(m, S), S("wm_wins"), slots))
        mo.to(2, 2)                     # the pointer off the playfield
        time.sleep(1)

        syms, image = pkg_syms(a.src)
        p = Pkg(m, seg, syms)
        lo, n = syms["ark_entry"], 2048
        live = bytes(m.read(seg * 16 + lo, n))
        if live != image[lo:lo + n]:
            raise RuntimeError("the ARKANOID.O88 running here is not %s - "
                               "run make" % a.src)
        card = 0
        ox = p.rw("ark_ox")
        fall = p.rw("ark_pufall")
        freeze(m, p)
        repaint(m, p, a.frames)

        print("window %s content %s  ark_ox=%d ark_spok=%d puband=%s"
              % (title, box, ox, p.rb("ark_spok"),
                 "yes" if "ark_puband" in syms else "NO (pre-44.10.6.2)"))
        rows = []
        for name, skew, forced in (("aligned", 0, False),
                                   ("unaligned", 1, False),
                                   ("forced (the OLD flag)", 1, True)):
            if forced and "ark_puband" not in syms:
                continue
            miss, extra, y0, y1, band = column(m, p, ox, skew, forced,
                                               a.frames, a.steps, card, fall,
                                               image, name.split()[0])
            n = len(extra) + len(miss)
            rows.append((name, n, bbox(extra + miss)))
            print("  %-22s fell %3d px  puband=%d  residue %4d px  %s"
                  % (name, y1 - y0, band, n, bbox(extra + miss)))

        bad = [r for r in rows if not r[0].startswith("forced") and r[1] > 0]
        if bad:
            print("FAIL: %s left residue" % ", ".join(r[0] for r in bad))
            return 1
        forced = [r for r in rows if r[0].startswith("forced")]
        if forced and forced[0][1] == 0:
            print("FAIL: the forced column measured NOTHING - the old reading "
                  "is not being reproduced, so the zeros above prove nothing")
            return 1
        print("PASS")
        return 0


if __name__ == "__main__":
    sys.exit(main())
