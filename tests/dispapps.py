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
import os88build                                            # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88pkg                                              # noqa: E402
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


def _map(app, defines=()):
    """Every equate in a package, from nasm's own `[map]` on a temp copy.

    NOT a regex over the source: apps/arkanoid and apps/solitaire declare their
    bss through a macro (`%1 equ os88_image_end + ARK_BSS`), so there is no
    `name equ os88_image_end + N` line to read - and a survey that silently
    skipped the two games would be a gate that passes because it tested
    nothing. The map is also what tools/os88sym.py uses, for the same reason.
    """
    key = (app, tuple(defines))
    if key in _MAPS:
        return _MAPS[key]
    src = os.path.join(ROOT, "apps", app, app + ".asm")
    if not os.path.exists(src):
        # ...or a package under tests/, which is where everything that does
        # NOT ship lives (CLAUDE.md's Layout). tests/facetest is the first
        # such caller: it asks apps/os88type.inc what typefaces the machine
        # has, and the answer is four bytes of its bss rather than anything a
        # screendump can be read for.
        src = os.path.join(ROOT, "tests", app, app + ".asm")
    # PER PROCESS, and that is not tidiness. These were "/tmp/os88_<app>.map"
    # flat, so two rows of the suite mapping the same package at once wrote
    # each other's file - and `os88test.py --marty-jobs 3` runs exactly that.
    # What it looked like was not a race: one row said "could not map paint"
    # with an EMPTY nasm stderr, and the others read plausible-looking rubbish
    # out of the guest - [pt_planar] = 60, a 14337x28673 canvas - because the
    # offsets came from a half-written map. Every one of those points at the
    # package under test rather than at the harness.
    tag = app + "".join("_" + d.lstrip("-D") for d in defines)
    tmp = "/tmp/os88_%s_map_%d.asm" % (tag, os.getpid())
    mp = "/tmp/os88_%s_%d.map" % (tag, os.getpid())
    open(tmp, "w").write(open(src).read() + "\n[map all %s]\n" % mp)
    inc = ["-I", os.path.join(ROOT, "apps") + os.sep,
           "-I", os.path.join(ROOT, "apps", app) + os.sep,
           "-I", os.path.join(ROOT, "tests") + os.sep,
           "-I", os.path.join(ROOT, "drivers", "net") + os.sep,
                                        # apps/telnet and apps/ftpd include
                                        # netpkg.inc from the driver that
                                        # publishes the socket surface, which
                                        # is what their Makefile lines pass
                                        # too. Harmless for every other app -
                                        # nasm only reaches a -I when an
                                        # %include misses
           "-I", os.path.join(ROOT, "build") + os.sep]
                                        # A C PACKAGE'S SHIM %includes THE
                                        # COMPILED C out of build/ (SPEC.md
                                        # 73.1: `%include "paccman.gen.asm"`),
                                        # so without this every C package -
                                        # paccman, cword, runcpm, weave, c64 -
                                        # fails to map at all. tests/paccman.py
                                        # is the row that needs it; it calls
                                        # _map('paccman') directly, the way
                                        # tests/alertbtn.py calls _map('paint').
                                        # The generated file is a build
                                        # product, so a tree that has not run
                                        # `make <pkg>` still cannot map it and
                                        # the caller gets nasm's own message
                                        # saying which file is missing.
                                        #
                                        # LAST OF THE FOUR, deliberately: it
                                        # is the only search path that holds
                                        # BUILD PRODUCTS, so a generated file
                                        # that happened to share a name with a
                                        # source include must never be the one
                                        # nasm finds first.
    bn = "/tmp/os88_%s_%d.bin" % (tag, os.getpid())
    r = subprocess.run(["nasm", "-f", "bin", "-w+error"] + inc + list(defines) +
                       ["-o", bn, tmp],
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
    for f in (tmp, mp):                 # unique names, so they must be swept
        try:
            os.unlink(f)
        except OSError:
            pass
    if "os88_image_end" not in out and "cc_image_end" not in out:
        sys.exit("dispapps: %s's map has no os88_image_end" % app)
    if "os88_image_end" not in out:     # a C package: apps/cc/crt0.asm names
        out["os88_image_end"] = out["cc_image_end"]     # the same byte this
    # **THE MAP DESCRIBES THE SOURCE; THE GUEST IS RUNNING THE IMAGE.** If
    # build/ is behind the tree, every offset below is right for a layout the
    # machine does not have - and what comes back is not an error, it is
    # plausible rubbish. It cost two rows: `canvas 0 wide, [pt_planar] = 0`
    # and `Paint reports a 0x0 canvas`, both of which name the PACKAGE and
    # neither of which is about it.
    #
    # tools/os88test.py already makes this check for the KERNEL - os88sym
    # refuses an address unless a re-assembly is byte-identical to
    # build/kernel.bin - and packages had no equivalent. This is it.
    # os88pkg.py validates and stamps without changing a byte, so the
    # comparison is exact rather than a size test.
    # **THE FILE IS NO LONGER THE IMAGE** (SPEC.md 20.13.5,
    # docs/plans/O88-COMPRESSION-PLAN.md): `PKGZ ?= lz4`, so every shipped
    # `.o88` on disk is
    # a compressed CONTAINER and the bytes nasm emitted are inside it.
    # Comparing the file against a fresh assembly therefore compares 11,758
    # bytes with 14,935 and reports "build/ is BEHIND THE TREE" about a
    # perfectly current one - which is a WRONG DIAGNOSIS of a right check, and
    # it took 41 rows down in one soak because every graphical row imports
    # this. `image_unwrap` is the rule that plan states for exactly this
    # shape: a host-side check about a size, the bss arithmetic or an
    # assembly wants the IMAGE, never the file.
    #
    # ...and through `os88build.at`, because under a frozen run the shipped
    # packages are in the run's own tree (docs/plans/SOAK-PARALLEL.md 14.2) and
    # `build/` may hold another build's.
    sub = os.path.join("build", "smallapp") if defines else "build"
    o88 = os88build.at("%s/%s.o88"
                       % (sub, {"solitaire": "solitair"}.get(app, app)))
    if not os.path.isabs(o88):
        o88 = os.path.join(ROOT, o88)
    try:
        built = os88pkg.image_unwrap(open(o88, "rb").read())
        fresh = open(bn, "rb").read()
    except OSError as e:
        sys.exit("dispapps: cannot compare %s against the tree (%s) - run "
                 "`make`%s" % (o88, e, " && make smallapps" if defines else ""))
    finally:
        for f in (tmp, mp, bn):
            try:
                os.unlink(f)
            except OSError:
                pass
    if built != fresh:
        sys.exit("dispapps: %s holds a %d-byte image and %s assembles to %d - "
                 "build/ is BEHIND THE TREE, so every bss offset this returns "
                 "describes a layout the guest does not have. Run `make%s`."
                 % (o88, len(built), os.path.relpath(src, ROOT), len(fresh),
                    " && make smallapps" if defines else ""))
    _MAPS[key] = out
    return out


def colour_gif(src="build/OS8088.GIF", dst="/tmp/OS88COL.GIF"):
    """`src` with a FOUR-entry colour table, so SPEC.md 42.23.6 keeps it 4bpp.

    **Every pixel INDEX is identical, and what the indices mean is not** - the
    two new entries go in FIRST, so the picture's own pair moves to 2 and 3
    and every pixel is drawn in one of the two new colours. That is what makes
    the file colour, and the file has to be colour or `pt_fmtpick` takes a
    1bpp canvas and the planar rows have no subject. What it reads is one bit:
    42.23.6 opens a picture whose colour table has two entries ONE BIT DEEP,
    on any adapter, and `build/OS8088.GIF` turns out to have exactly two.

    **A row whose oracle is "the 1bpp canvas equals the file's own bitmap"
    therefore CANNOT use this** - see the note at the insertion below, which
    is what `blitpair` cost. Those rows want `build/OS8088.GIF` itself.

    That is correct for that file and it left the tree with no COLOUR picture
    at all - so `paintrow` and `paintback`, whose whole subject is the
    four-plane canvas, stopped being able to get one. Deriving the fixture
    rather than committing a second image keeps them pinned to the same
    picture the rest of the paint rows use, and keeps the repo free of a
    binary (CONTRIBUTING.md 6).

    Returns `dst`, whose basename is deliberately already a legal 8.3 name in
    upper case: the row has to find it in a file window by the name os88disk
    put on the disk, and a stem of nine characters would be silently truncated
    to something neither end agrees on.
    """
    import os
    import shutil
    sp = os.path.join(ROOT, src) if not os.path.isabs(src) else src
    if (os.path.exists(dst)
            and os.path.getmtime(dst) >= os.path.getmtime(sp)):
        return dst
    d = bytearray(open(sp, "rb").read())
    if d[:3] != b"GIF":
        sys.exit("dispapps: %s is not a GIF" % sp)
    pk = d[10]
    if not pk & 0x80:
        sys.exit("dispapps: %s has no global colour table to widen" % sp)
    n = 2 << (pk & 7)
    if n != 2:
        shutil.copyfile(sp, dst)            # already colour: nothing to do
        return dst
    d[10] = (pk & 0xF8) | 1                 # 2 << 1 = four entries
    # **PREPENDED, AND THAT IS THE POINT, so the paragraph above is wrong in
    # the one way that matters and is corrected here rather than tidied.** The
    # table starts at offset 13, so this inserts BEFORE the two entries the
    # image uses: indices 0 and 1 - the only ones any pixel carries - become
    # the two new colours, and the picture's own pair moves to 2 and 3. The
    # INDICES are untouched; what they mean is not.
    #
    # That is what makes the file colour, which is the whole purpose: with the
    # original black and white still at 0 and 1 the picture reduces to one bit
    # again, `pt_fmtpick` calls it colourless and Paint takes a 1bpp canvas -
    # measured, `paintrow` and `paintplan` then report *"no gfx_blitp - the
    # canvas is not planar"*, which is those rows losing their subject.
    #
    # **SO IT IS NOT A DROP-IN FOR OS8088.GIF, and a row whose oracle is "the
    # 1bpp canvas equals the file's own bitmap" must not use it**: on a 1bpp
    # adapter SPEC.md 39.4 reduces 0xAA0000 and 0x0000AA to the SAME class, so
    # the canvas is solid where the file alternates. `tests/blitpair.py` was
    # pointed here and read 20,327 differing pixels of 51,260 - which looks
    # exactly like the decoder bug it exists to catch, and was filed as a
    # pre-existing failure (docs/plans/HANDOFF-SOAK-FINDINGS.md F1). It takes
    # build/OS8088.GIF itself now, and says why.
    d[13:13] = bytes([0xAA, 0x00, 0x00,     # ...two of them unused by the
                      0x00, 0x00, 0xAA])    # image, and genuinely colours
    open(dst, "wb").write(bytes(d))
    return dst


def bss_off(app, name, small=False):
    """The offset of a bss word from the start of the package's bss.

    `small=True` maps the `-DAPP_SMALL` build, WHICH IS A DIFFERENT LAYOUT.
    Without it a row reading a small-built package's bss gets the shipped
    build's offsets applied to the wrong image, and what comes back is not an
    error - it is plausible rubbish. That cost a wrong reading once already:
    a 0x258-wide canvas with a stride of 0 and a claim "1.8 KB" out of a Paint
    holding 15.
    """
    m = _map(app, ("-DAPP_SMALL",) if small else ())
    if name not in m:
        sys.exit("dispapps: %s has no symbol %s%s"
                 % (app, name, " in the APP_SMALL build" if small else ""))
    return m[name] - m["os88_image_end"]


def img_size(app, small=False):
    """The package's image size, which is where its bss starts.

    **`image` AT +8 IS THE UNPACKED SIZE ON BOTH CONTAINERS** (SPEC.md
    20.13.5), so `n != len(d)` is not a layout check any more - it is how a
    COMPRESSED package looks, and `PKGZ ?= lz4` makes that every shipped one.
    The check is kept, against the unwrapped image, because it is still worth
    something: a header whose +8 disagrees with the bytes it describes is a
    layout that has moved. It just has to be told what the bytes are first.
    """
    sub = os.path.join("build", "smallapp") if small else "build"
    p = os88build.at("%s/%s.o88" % (sub.replace(os.sep, "/"),
                                    {"solitaire": "solitair"}.get(app, app)))
    if not os.path.isabs(p):
        p = os.path.join(ROOT, p)
    raw = open(p, "rb").read()
    if raw[:2] != b"O8":
        sys.exit("dispapps: %s is not a package image" % p)
    d = os88pkg.image_unwrap(raw)
    n = int.from_bytes(d[8:10], "little")       # +8 = image size (SPEC.md 20.2)
    if n != len(d):
        sys.exit("dispapps: %s says image=%d and unwraps to %d bytes - the "
                 "header layout has moved" % (p, n, len(d)))
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
        dispcp.open_named(m, mo, S, settle, wx, wy, "APPS")
        for _ in range(3):
            wx, wy, ww, wh = dispcp.win_rect(m, S, disk)
            mo.click(wx + 40, wy + TITLE_H // 2)
            settle(m)
            dispcp.open_named(m, mo, S, settle, wx, wy, "PIANO.O88")
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
        dispcp.open_named(m, mo, S, settle, wx, wy, "..")
        wx, wy, ww, wh = dispcp.win_rect(m, S, disk)
        dispcp.open_named(m, mo, S, settle, wx, wy, "GAMES")
        for pkg, app in (("ARKANOID.O88", "arkanoid"),
                         ("SOLITAIR.O88", "solitaire"),
                         ("MISSILE.O88", "missile")):
            got = None
            for _ in range(3):      # a dropped double-click SELECTS the row and
                                    # launches nothing (SPEC.md 9.4.3) - and a
                                    # retry on an already-selected row is just
                                    # another double-click, so it costs nothing
                wx, wy, ww, wh = dispcp.win_rect(m, S, disk)
                mo.click(wx + 40, wy + TITLE_H // 2)   # raise it back over
                settle(m)                                   # whatever opened
                dispcp.open_named(m, mo, S, settle, wx, wy, pkg)
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
