#!/usr/bin/env python3
"""Do the apps that lay out ONCE re-derive when the adapter changes?
(SPEC.md 11.98)

    make && python3 tests/dispapps.py

Five packages compute a layout from the geometry once and keep the answer:
`taskmgr` (row and column counts), `arkanoid` and `solitaire` and `missile`
(whole metric records picked on screen height - brick widths, card sizes,
rail widths, ball speed, the adapter facts), and `piano` (SPEC.md 11.98.1's
scaled keyboard). Everything else in `apps/` either re-reads its box every
frame or has no second layout to move to - `apps/fractal` is the one worth
naming, because it re-derives per paint and `tests/dispfrac.py` is the gate
for what it caches instead.

IT READS THE PACKAGE'S OWN BSS, which is the only place the answer lives. A
screenshot cannot show this: the Task Manager's list is three rows long on a
quiet machine, so a stale row COUNT draws nothing wrong until enough apps are
open, and a game's metric record is numbers rather than pixels. The offsets
come out of each source's `os88_image_end +` block and are checked against the
package header's image size, so a moved word is an error here rather than a
plausible number nobody attributes.
"""
import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import os88geom                                             # noqa: E402
import dispfit                                              # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = os88sym.linear
VID_VGA, VID_CGA = 0, 2
CHIP, MI_TASKS = (12, 8), (60, 60)
TITLE_H = 18
PN_KB_Y1, PN_KB_Y2_FULL = 88, 155   # apps/piano's own constants


_MAPS = {}


def _map(app):
    """Every equate in a package, from nasm's own `[map]` on a temp copy.

    NOT a regex over the source: apps/arkanoid and apps/solitaire declare their
    bss through a macro (`%1 equ os88_image_end + ARK_BSS`), so there is no
    `name equ os88_image_end + N` line to read - and a survey that silently
    skipped the two games would be a gate that passes because it tested
    nothing. The map is also what tools/os88sym.py uses, for the same reason.
    """
    if app in _MAPS:
        return _MAPS[app]
    src = os.path.join(ROOT, "apps", app, app + ".asm")
    tmp = "/tmp/os88_%s_map.asm" % app
    mp = "/tmp/os88_%s.map" % app
    open(tmp, "w").write(open(src).read() + "\n[map all %s]\n" % mp)
    inc = ["-I", os.path.join(ROOT, "apps") + os.sep,
           "-I", os.path.join(ROOT, "apps", app) + os.sep]
    r = subprocess.run(["nasm", "-f", "bin", "-w+error"] + inc +
                       ["-o", "/dev/null", tmp],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("dispapps: could not map %s:\n%s" % (app, r.stderr[:400]))
    out = {}
    for line in open(mp):
        p = line.split()                    # "<vaddr> <raddr> <name>", HEX
        if len(p) == 3:
            try:
                out[p[2]] = int(p[0], 16)
            except ValueError:
                pass
    if "os88_image_end" not in out:
        sys.exit("dispapps: %s's map has no os88_image_end" % app)
    _MAPS[app] = out
    return out


def bss_off(app, name):
    """The offset of a bss word from the start of the package's bss."""
    m = _map(app)
    if name not in m:
        sys.exit("dispapps: %s has no symbol %s" % (app, name))
    return m[name] - m["os88_image_end"]


def img_size(app):
    """The package's image size, which is where its bss starts."""
    p = os.path.join(ROOT, "build", {"solitaire": "solitair"}.get(app, app)
                     + ".o88")
    d = open(p, "rb").read()
    if d[:2] != b"O8":
        sys.exit("dispapps: %s is not a package image" % p)
    n = int.from_bytes(d[8:10], "little")       # +8 = image size (SPEC.md 20.2)
    if n != len(d):
        sys.exit("dispapps: %s says image=%d but is %d bytes - the header "
                 "layout has moved" % (p, n, len(d)))
    return n


def pkg_seg(m, want):
    """The segment of a visible PACKAGE window, newest last."""
    b = m.read(S("wm_wins"), os88geom.MAX_WIN * os88geom.WIN_SIZE)
    out = []
    for i in range(os88geom.MAX_WIN):
        o = i * os88geom.WIN_SIZE
        fl = int.from_bytes(b[o + os88geom.W_FLAGS:o + os88geom.W_FLAGS + 2], "little")
        seg = int.from_bytes(b[o + os88geom.W_SEG:o + os88geom.W_SEG + 2], "little")
        if fl & 3 == 3 and seg:
            out.append((i, seg))
    return out[want] if len(out) > want else None


# The ones the app declares as a BYTE. Read as a word they pick up whatever
# is declared next, which is not wrong so much as unreadable - mc_mono came
# back as 3840 and 3841 for 0 and 1.
BYTES = set(["mc_mono", "mc_ecoarse", "mc_expfr", "ark_bpp", "sol_bpp",
             "mn_mode", "mn_revealed", "mn_flags"])


def words(m, seg, app, names):
    base = img_size(app)
    out = {}
    for n in names:
        n_b = 1 if n in BYTES else 2
        out[n] = int.from_bytes(
            m.read((seg << 4) + base + bss_off(app, n), n_b), "little")
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)

    fail = []
    say = lambda s: print("  " + s)
    settle = os88marty.settle
    watch = {"taskmgr": ["tm_cols", "tm_colrows", "tm_col2rows", "tm_maxrow"],
             "arkanoid": ["ark_bw", "ark_bh", "ark_rows", "ark_pw0",
                          "ark_bpp"],
             "solitaire": ["sol_cw", "sol_ch", "sol_pitch", "sol_taby",
                           "sol_bpp"],
             "missile": ["mc_mono", "mc_ecoarse", "mc_expfr", "mc_caps"],
             "piano": ["pn_kby2", "pn_bky2", "pn_wlbl", "pn_blbl"]}
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        mo = os88mouse.Mouse(marty=m)
        if dispfit.kind(m) != VID_VGA:
            sys.exit("dispapps: %s did not come up on the VGA" % a.machine)

        mo.menu(CHIP[0], CHIP[1], *MI_TASKS)        # the chip menu's item 3
        m.advance(frames=150)       # NOT settle: the Task Manager repaints
        m.run()                     # twice a second, so the screen never stops
        got = pkg_seg(m, 0)
        if got is None:
            sys.exit("dispapps: the Task Manager did not open")
        slot, seg = got
        say("Task Manager: window %d, segment %04x, rect %r"
            % (slot, seg, dispcp.win_rect(m, S, slot)))
        segs = {"taskmgr": seg}
        slots = {"taskmgr": slot}

        # ...and the two games, out of B:\GAMES
        dispcp.open_drive(m, mo, S, settle, "B")
        disk = dispcp.win_list(m, S)[-1]        # BANKED: once a game opens on
        wx, wy = dispcp.win_rect(m, S, disk)[:2]    # top of it, win_list's
        # ...and apps/piano, out of B:\APPS - the fixed-layout case, whose
        # keyboard is the only part of its content with any slack in it
        wx, wy, ww, wh = dispcp.win_rect(m, S, disk)
        dispcp.open_row(m, mo, S, settle, wx, wy, 0)        # APPS
        for _ in range(3):
            wx, wy, ww, wh = dispcp.win_rect(m, S, disk)
            mo.click(wx + 40, wy + TITLE_H // 2)
            settle(m)
            dispcp.open_row(m, mo, S, settle, wx, wy, 7)    # PIANO.O88
            m.advance(frames=120)
            m.run()
            got = pkg_seg(m, len(segs))
            if got is not None:
                break
        if got is None:
            sys.exit("dispapps: piano did not open")
        segs["piano"] = got[1]
        slots["piano"] = got[0]
        say("piano: segment %04x" % got[1])

        wx, wy, ww, wh = dispcp.win_rect(m, S, disk)        # ...back to B:\ and
        mo.click(wx + 40, wy + TITLE_H // 2)                # on into GAMES
        settle(m)
        dispcp.open_row(m, mo, S, settle, wx, wy, 0)        # ..
        wx, wy, ww, wh = dispcp.win_rect(m, S, disk)
        dispcp.open_row(m, mo, S, settle, wx, wy, 1)        # GAMES
        for row, app in ((1, "arkanoid"), (4, "solitaire"), (3, "missile")):
            got = None
            for _ in range(3):      # a dropped double-click SELECTS the row and
                                    # launches nothing (SPEC.md 9.4.3) - and a
                                    # retry on an already-selected row is just
                                    # another double-click, so it costs nothing
                wx, wy, ww, wh = dispcp.win_rect(m, S, disk)
                mo.click(wx + 40, wy + TITLE_H // 2)   # raise it back over
                settle(m)                                   # whatever opened
                dispcp.open_row(m, mo, S, settle, wx, wy, row)
                m.advance(frames=120)
                m.run()
                got = pkg_seg(m, len(segs))
                if got is not None:
                    break
            if got is None:
                w, h, d = m.fbuf()
                os88marty.write_png_rgb("/tmp/dispapps-stuck.png", w, h, d)
                sys.exit("dispapps: %s did not open - windows %r, see "
                         "/tmp/dispapps-stuck.png"
                         % (app, [(i, dispcp.win_rect(m, S, i))
                                  for i in dispcp.win_list(m, S)]))
            segs[app] = got[1]
            slots[app] = got[0]
            say("%s: segment %04x" % (app, got[1]))

        before = dict((a, words(m, segs[a], a, watch[a])) for a in segs)
        for a in sorted(before):
            say("VGA  %-10s %r" % (a, before[a]))

        dispcp.open_panel(m, mo, S, settle)
        dispfit.switch_to(m, mo, settle, 1, VID_CGA)
        after = dict((a, words(m, segs[a], a, watch[a])) for a in segs)
        for a in sorted(after):
            say("CGA  %-10s %r" % (a, after[a]))
            if after[a] == before[a]:
                fail.append("%s did not re-derive anything across a 480-row "
                            "to 200-row change (SPEC.md 11.98)" % a)
        if after["taskmgr"]["tm_colrows"] >= before["taskmgr"]["tm_colrows"]:
            fail.append("tm_colrows went %d -> %d on a SHORTER screen"
                        % (before["taskmgr"]["tm_colrows"],
                           after["taskmgr"]["tm_colrows"]))
        for a, k in (("arkanoid", "ark_bw"), ("solitaire", "sol_cw")):
            if after[a][k] >= before[a][k]:
                fail.append("%s's %s went %d -> %d on a SHORTER screen - it "
                            "should have taken the small metric record"
                            % (a, k, before[a][k], after[a][k]))
        # apps/missile is the ADAPTER-FACTS case and not a box one: on 1bpp it
        # must be on SPEC.md 48.8's coarse ramp, and off it again afterwards.
        if not after["missile"]["mc_ecoarse"]:
            fail.append("missile is still on the FINE explosion ramp on a 1bpp "
                        "adapter - SPEC.md 48.8's unplayable case")
        if not after["missile"]["mc_mono"]:
            fail.append("missile still thinks it is on a colour adapter")
        # apps/piano's keyboard must END INSIDE the content box on both
        # adapters, and be untouched at full height - the constants it came
        # from are a ceiling now, not a layout.
        for tag, vals in (("VGA", before), ("CGA", after)):
            ch = dispcp.win_rect(m, S, slots["piano"])[3] - TITLE_H - 1
            if vals is before:
                ch = 177 - TITLE_H - 1          # its template, before the switch
            if vals["piano"]["pn_kby2"] > ch - 1:
                fail.append("piano's keyboard ends at row %d of a %d-row "
                            "content box on %s - it is drawing through the "
                            "bottom of its own frame"
                            % (vals["piano"]["pn_kby2"], ch, tag))
            if not (PN_KB_Y1 < vals["piano"]["pn_bky2"]
                    < vals["piano"]["pn_kby2"]):
                fail.append("piano's black keys (%d) are not inside its white "
                            "ones (%d..%d) on %s"
                            % (vals["piano"]["pn_bky2"], PN_KB_Y1,
                               vals["piano"]["pn_kby2"], tag))
        if before["piano"]["pn_kby2"] != PN_KB_Y2_FULL:
            fail.append("piano's keyboard is %d at FULL height and the layout "
                        "it was drawn for is %d - a VGA must be untouched"
                        % (before["piano"]["pn_kby2"], PN_KB_Y2_FULL))

        dispfit.switch_to(m, mo, settle, 0, VID_VGA)
        back = dict((a, words(m, segs[a], a, watch[a])) for a in segs)
        for a in sorted(back):
            say("VGA  %-10s %r" % (a, back[a]))
            if back[a] != before[a]:
                fail.append("%s did not come back: %r against %r - launch and "
                            "resize must agree about an identical frame"
                            % (a, back[a], before[a]))

    print()
    for f in fail:
        print("dispapps: FAIL: %s" % f)
    if fail:
        return 1
    print("dispapps: taskmgr, arkanoid, solitaire, missile and piano all "
          "re-derive on an adapter change and come back - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
