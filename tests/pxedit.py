#!/usr/bin/env python3
"""PiXEL'S EDITING AGAINST THE REFERENCE, BYTE FOR BYTE (SPEC.md 106.24).

    python3 tests/pxedit.py [--machine os8088_xt_vga_144] [--host-only]

THE HOST LEG first, when Unicorn is installed (tests/pxpartemu.py; without
it the leg SKIPS and says so): the EDIT part itself - build/pxedit.bin, the
bytes PIXEL.O88 carries - run on a faked package, every palette verb over a
grid of its parameters and every pixel verb over seven sizes, three palettes
(its own, the cube, the greys) and both destinations (a second master, and
in place), against tools/pixelsim.py's pal_* and op_* - the palette, the
master, the GREY/cube answer and the new master's histogram counts.

THE GUEST LEG on MartyPC (the VGA XT; --machine for another), CITY.PCX open
(320x240, its own 256 colours):

  PALETTE   Invert, Greyscale, Sepia and Auto Levels from their menus, and
            Brightness/Contrast, Gamma, Posterize and Threshold through
            their cards (the arrows, then Enter): each palette pixelsim's,
            the mode PAL, the picture unsaved; Undo puts the palette, its
            mode and the saved state back
  PIXELS    every Image and Effects operation from its menu - the turns,
            the flips, Resize's card, Crop to a poked selection, the four
            kernels and Pixelate: the master, its size and its mode
            pixelsim's; Undo puts the master and the palette back byte for
            byte and PiXEL's claims as they were; Undo again (Redo) the
            result again
  CHAINED   Blur (the cube), Invert (an edited cube is PAL), Sharpen through
            that palette - pixelsim's chain of the three
  CANCEL    Esc in the middle of Blur: the picture, the claims and undo as
            before
  TOOLS     the Marquee's drag makes the selection under the pointer, the
            arrows nudge it, Esc drops it; the Crop tool's drag and Enter
            crop to it; the Zoom tool's click steps in about the point and
            Shift's out; the Eyedropper's readout and its pin in Image Info;
            the Rotate tool's click turns the picture
"""
import argparse
import functools
import os
import struct
import sys

print = functools.partial(print, flush=True)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, "tests")
sys.path.insert(0, "tools")
import pixelsim as P            # noqa: E402
import pxpartemu                # noqa: E402

FAIL = []
EV = dict(INVERT=0, GREY=1, SEPIA=2, BRICON=3, GAMMA=4, POSTER=5, THRESH=6,
          LEVELS=7, ROTCW=8, ROTCCW=9, ROT180=10, FLIPH=11, FLIPV=12, CROP=13,
          RESIZE=14, BLUR=15, SHARP=16, EDGE=17, EMBOSS=18, PIXEL=19)


def check(name, ok, detail=""):
    print("   %-64s %s%s" % (name, "ok" if ok else "FAIL",
                             "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


def first_diff(a, b):
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            return (i, a[i], b[i])
    return (n, len(a), len(b)) if len(a) != len(b) else None


# =============================================================================
# THE HOST LEG
# =============================================================================
def host_leg():
    if not pxpartemu.available():
        print("== host leg SKIPPED: no Unicorn (pip install unicorn) ==")
        return
    part = "build/pxedit.bin"
    print("== the EDIT part on the host, against tools/pixelsim.py ==")
    import random
    pal = [((i * 37) & 255, (i * 91) & 255, (i * 13) & 255) for i in range(256)]
    w, h = 13, 7
    m = bytes(((x * 7 + y * 31) ^ (x * y)) & 255 for y in range(h)
              for x in range(w))
    counts = P.hist_counts(m)
    cases = [("INVERT", (), P.pal_invert(pal)), ("GREY", (), P.pal_grey(pal)),
             ("SEPIA", (), P.pal_sepia(pal)),
             ("LEVELS", (), P.pal_levels(pal, counts))]
    for b in range(-100, 101, 50):
        for c in range(-90, 91, 30):
            cases.append(("BRICON", (b & 0xFFFF, c & 0xFFFF),
                          P.pal_bricon(pal, b, c)))
    for gi, g in enumerate(P.GAMMAS):
        cases.append(("GAMMA", (gi,), P.pal_gamma(pal, g)))
    for n in (2, 3, 4, 8, 16):
        cases.append(("POSTER", (n,), P.pal_poster(pal, n)))
    for t in (8, 64, 128, 248):
        cases.append(("THRESH", (t,), P.pal_thresh(pal, t)))
    bad = 0
    for name, args, want in cases:
        p = pxpartemu.PxPart(part)
        p.set_pal(pal)
        p.set_master(m, w, h, counts)
        for k, a in enumerate(args):
            p.w16("px_ep", a, 2 * k)
        ax, cf = p.decode(EV[name])
        if cf or p.pal() != want:
            bad += 1
            print("      %s %r: %s" % (name, args, first_diff(p.pal(), want)))
    check("%d palette operations" % len(cases), not bad, "%d differ" % bad)

    def run(name, m, w, h, pl, args=(), inplace=False):
        p = pxpartemu.PxPart(part)
        p.set_pal(pl)
        seg = p.set_master(m, w, h, P.hist_counts(m))
        for k, a in enumerate(args):
            p.w16("px_ep", a, 2 * k)
        ax, cf = p.decode(EV[name] | 0x80)
        if cf:
            raise RuntimeError("EV_WORK refused %s" % name)
        dw, dh = p.r16("px_edw"), p.r16("px_edh")
        p.w16("px_ewseg", p.claim(ax * 1024 + 16) if ax else 0)
        if inplace or name in ("ROT180", "FLIPH", "FLIPV"):
            p.w16("px_edseg", 0)
            ds = seg
        else:
            ds = p.claim(dw * dh + 2048 + 16)
            p.w16("px_edseg", ds)
        ax, cf = p.decode(EV[name])
        if cf:
            raise RuntimeError("%s answered CF, AX %d" % (name, ax))
        out = p.seg_read(ds, dw * dh)
        tail = p.seg_read(pxpartemu.PxPart.tail_of(ds, dw, dh), 1024)
        cnt = [struct.unpack_from("<I", tail, 4 * i)[0] for i in range(256)]
        return out, dw, dh, p.r8("px_egrey"), cnt

    rnd = random.Random(7)
    n = bad = 0
    for (w, h) in [(13, 7), (1, 1), (2, 3), (16, 16), (37, 5), (5, 37),
                   (64, 48)]:
        m = bytes(rnd.randrange(256) for _ in range(w * h))
        for pn, pl in (("its own", pal), ("grey", P.GREY), ("cube", P.CUBE)):
            refs = []
            for name, k in (("ROTCW", 1), ("ROTCCW", 3), ("ROT180", 2)):
                wm, ww, wh = P.op_rotate(m, w, h, k)
                refs.append((name, (), False, (wm, ww, wh), None))
            for name, a in (("FLIPH", 1), ("FLIPV", 0)):
                wm, ww, wh = P.op_flip(m, w, h, a)
                refs.append((name, (), False, (wm, ww, wh), None))
            x1, y1, x2, y2 = w // 4, h // 3, w - 1 - w // 5, h - 1
            for ip in (False, True):
                refs.append(("CROP", (x1, y1, x2, y2), ip,
                             P.op_crop(m, w, h, x1, y1, x2, y2), None))
            for kind, nm in (("blur", "BLUR"), ("sharpen", "SHARP"),
                             ("edge", "EDGE"), ("emboss", "EMBOSS")):
                wm, ww, wh, mode = P.op_conv(m, w, h, pl, kind)
                for ip in (False, True):
                    refs.append((nm, (), ip, (wm, ww, wh), mode))
            wm, ww, wh, mode = P.op_pixelate(m, w, h, pl)
            for ip in (False, True):
                refs.append(("PIXEL", (), ip, (wm, ww, wh), mode))
            for num, den in P.RESIZE_STEPS:
                wm, ww, wh, mode = P.op_resize(m, w, h, pl, num, den)
                refs.append(("RESIZE", (num, den), False, (wm, ww, wh), mode))
            for name, args, ip, want, mode in refs:
                n += 1
                got, dw, dh, grey, cnt = run(name, m, w, h, pl, args, ip)
                ok = (got, dw, dh) == want and cnt == P.hist_counts(want[0])
                if mode is not None:
                    ok = ok and grey == (mode == P.PM_GREY)
                if not ok:
                    bad += 1
                    print("      %s%r %dx%d %s in place %d: %s" % (
                        name, args, w, h, pn, ip, first_diff(got, want[0])))
    check("%d pixel operations (sizes x palettes x destinations)" % n,
          not bad, "%d differ" % bad)
    # review-w7 F7: a WIDE short master whose work layout passes 64 KB in
    # an intermediate sum is refused ("too wide to edit") - it used to wrap,
    # claim a small work area and write up to 45 KB past it
    for name, ww, hh, args in (("RESIZE", 950, 2, (4, 1)),
                               ("RESIZE", 1300, 2, (3, 1)),
                               ("RESIZE", 2000, 2, (2, 1)),
                               ("BLUR", 6000, 3, ())):
        p = pxpartemu.PxPart(part)
        p.set_pal(pal)
        mm = bytes(ww * hh)
        p.set_master(mm, ww, hh, P.hist_counts(mm))
        for k, a in enumerate(args):
            p.w16("px_ep", a, 2 * k)
        ax, cf = p.decode(EV[name] | 0x80)
        check("%s%r of %dx%d: refused, not wrapped" % (name, args, ww, hh),
              cf, "EV_WORK answered %d KB" % ax)


# =============================================================================
# THE GUEST LEG
# =============================================================================
def guest_leg(machine):
    import os88marty as M
    import os88ui
    import heapmap
    from pxsyms import pkg_syms, instance, u16
    syms, image = pkg_syms()
    DISK = "build/pxedit.img"
    M.scratch_disk(DISK, "build/pixel.o88", "build/PIXEL.GFX",
                   "apps/pixel/samples/CITY.PCX")
    city = open("apps/pixel/samples/CITY.PCX", "rb").read()
    q = P.decode(city, "PCX")
    m0, w0, h0, mode0, pal0 = P.emit(q, 0)
    m0 = bytes(m0)
    pal0 = P.pal_full(pal0)
    sysdisk = "build/os8088.img" if machine.endswith("_144") else \
        "build/os8088-360.img"
    print("== PiXEL's editing on %s, CITY.PCX (SPEC.md 106.24) ==" % machine)
    with os88ui.boot(sysdisk, apps=DISK, machine=machine) as ui:
        m = ui.m
        S = ui._S
        ui.path("B:/CITY.PCX")
        M.until(m, lambda _: instance(m, S, image), "PiXEL's instance",
                poll=0.3, limit=120)
        seg = instance(m, S, image)
        base = seg * 16
        B = lambda n, k=0: m.read(base + syms[n] + k, 1)[0]      # noqa
        W = lambda n, k=0: u16(m.read(base + syms[n] + k, 2))    # noqa
        m.write(base + syms["px_thoff"], b"\1")     # (no thumbnails here)

        def idle():
            return B("px_busy") == 0 and B("px_job") == 0
        M.until(m, lambda _: W("px_ndone") and idle(), "the decode",
                poll=0.3, limit=600)
        M.ui_done(m, "decoded")

        def rec():
            b = m.read(base + syms["px_cur"], 50)
            return dict(mw=u16(b, 32), mh=u16(b, 34), scl=b[36],
                        pmode=b[37], mseg=u16(b, 38), npal=u16(b, 44))

        def pal():
            b = m.read(base + syms["px_pal"], 768)
            return [tuple(b[3 * i:3 * i + 3]) for i in range(256)]

        def master():
            r = rec()
            return bytes(m.read(r["mseg"] * 16, r["mw"] * r["mh"]))

        def parts():
            out = []
            for k in range(9):
                r = m.read(base + syms["op_table"] + 10 + 8 * k, 8)
                if r[1] & 32:
                    out.append(u16(r, 6))
            return out

        def claims():
            """PiXEL's claims but its region and its parts (which an
            operation fetches and a save drops: the claims a picture holds
            are the question here)."""
            hm = heapmap.Map(m, {n: S(n) for n in ("mem_base", "mem_top",
                                                   "spl_live", "mem_tab")})
            ps = parts()
            return sorted((c.seg, c.para) for c in hm.claims
                          if c.own == seg and c.seg != seg
                          and not any(c.seg <= p < c.seg + c.para
                                      for p in ps))

        def held(before):
            """PiXEL's claims are `before`'s - and, after an Undo that
            kept the result for Redo, that master's claim too."""
            extra = sorted(set(claims()) - set(before))
            redo = W("px_urec", 38) if B("px_ukind") == 3 else None
            return set(before) <= set(claims()) and (
                not extra or (len(extra) == 1 and extra[0][0] == redo))

        def wait_op(what):
            M.ui_done(m, what)              # (the handler has run, so a
            M.until(m, lambda _: idle() and B("px_pcon") == 0, what,
                    poll=0.3, limit=1200)   # job it started is busy now)
            M.ui_done(m, what)

        def menu(mn, item):
            ui.menu_pick(mn, item)
            M.ui_done(m, item)

        def undo(name, redo=False):
            menu("Edit", ("Redo " if redo else "Undo ") + name)
            wait_op("undo " + name)

        def card(mn, item, keys):
            menu(mn, item)
            check("%s: its card up" % item, B("px_pcon") != 0)
            for k in keys:
                m.key(k)
                M.ui_done(m, k)
            m.key("Enter")
            M.ui_done(m, "OK")
            wait_op(item)

        before = claims()
        # --- PALETTE -------------------------------------------------------
        pal_ops = [
            ("Image", "Invert", None, "Invert", P.pal_invert(pal0)),
            ("Image", "Greyscale", None, "Greyscale", P.pal_grey(pal0)),
            ("Effects", "Sepia", None, "Sepia", P.pal_sepia(pal0)),
            ("Image", "Auto Levels", None, "Auto Levels",
             P.pal_levels(pal0, P.hist_counts(m0))),
            ("Image", "Brightness/Contrast...", ["ArrowRight", "ArrowRight",
                                                 "ArrowUp"],
             "Brightness", P.pal_bricon(pal0, 20, 10)),
            ("Effects", "Gamma...", ["ArrowRight", "ArrowRight",
                                     "ArrowRight"],
             "Gamma", P.pal_gamma(pal0, P.GAMMAS[10])),
            ("Effects", "Posterize...", ["ArrowLeft"], "Posterize",
             P.pal_poster(pal0, 3)),
            ("Effects", "Threshold...", ["ArrowRight"], "Threshold",
             P.pal_thresh(pal0, 136)),
        ]
        for mn, item, keys, uname, want in pal_ops:
            if keys is None:
                menu(mn, item)
                wait_op(item)
            else:
                card(mn, item, keys)
            r = rec()
            check("%s: the palette pixelsim's" % item, pal() == want,
                  "first diff %s" % (first_diff(pal(), want),))
            check("%s: PAL, 256, unsaved" % item,
                  (r["pmode"], r["npal"], B("px_dirty")) == (0, 256, 1),
                  "%r dirty %d" % (r, B("px_dirty")))
            undo(uname)
            r = rec()
            check("%s: undone - the palette, the mode, saved again" % item,
                  pal() == pal0 and r["pmode"] == mode0
                  and B("px_dirty") == 0 and master() == m0,
                  "%r dirty %d" % (r, B("px_dirty")))
        # --- PIXELS ----------------------------------------------------------
        sel = (40, 30, 239, 189)
        pix_ops = [
            ("Image", "Rotate 90 CW", "Rotate CW",
             P.op_rotate(m0, w0, h0, 1) + (mode0,), None),
            ("Image", "Rotate 90 CCW", "Rotate CCW",
             P.op_rotate(m0, w0, h0, 3) + (mode0,), None),
            ("Image", "Rotate 180", "Rotate 180",
             P.op_rotate(m0, w0, h0, 2) + (mode0,), None),
            ("Image", "Flip Horizontal", "Flip Across",
             P.op_flip(m0, w0, h0, 1) + (mode0,), None),
            ("Image", "Flip Vertical", "Flip Down",
             P.op_flip(m0, w0, h0, 0) + (mode0,), None),
            ("Effects", "Blur", "Blur", P.op_conv(m0, w0, h0, pal0, "blur"),
             None),
            ("Effects", "Sharpen", "Sharpen",
             P.op_conv(m0, w0, h0, pal0, "sharpen"), None),
            ("Effects", "Edge Detect", "Edge Detect",
             P.op_conv(m0, w0, h0, pal0, "edge"), None),
            ("Effects", "Emboss", "Emboss",
             P.op_conv(m0, w0, h0, pal0, "emboss"), None),
            ("Effects", "Pixelate", "Pixelate",
             P.op_pixelate(m0, w0, h0, pal0), None),
            ("Image", "Resize...", "Resize",
             P.op_resize(m0, w0, h0, pal0, 1, 2), []),
            ("Edit", "Crop to Selection", "Crop",
             P.op_crop(m0, w0, h0, *sel) + (mode0,), "sel"),
        ]
        def gsec():
            return int(m.status().get("cycles", 0)) / M.GUEST_HZ
        for mn, item, uname, want, how in pix_ops:
            wm, ww, wh, wmode = want
            t0 = gsec()
            if how == "sel":
                menu("Edit", "Select All")      # (the item live), then
                m.write(base + syms["px_selr"],  # the rect this row wants
                        struct.pack("<4H", *sel))
                menu(mn, item)
                wait_op(item)
            elif how is not None:
                card(mn, item, how)
            else:
                menu(mn, item)
                wait_op(item)
            print("      %s: %.1f s of the guest's, the menu included"
                  % (item, gsec() - t0))
            r = rec()
            got = master()
            check("%s: %dx%d mode %d, the master pixelsim's" % (
                item, ww, wh, wmode),
                (r["mw"], r["mh"], r["pmode"]) == (ww, wh, wmode)
                and got == bytes(wm),
                "%r first diff %s" % (r, first_diff(got, bytes(wm))))
            if wmode == P.PM_CUBE:
                check("%s: the cube's palette" % item, pal() == P.CUBE)
            undo(uname)
            r = rec()
            check("%s: undone - master, palette, claims" % item,
                  master() == m0 and pal() == pal0 and r["pmode"] == mode0
                  and held(before) and B("px_dirty") == 0,
                  "%r claims %s -> %s, redo's %04X" % (
                      r, before, claims(), W("px_urec", 38)))
            if item in ("Blur", "Rotate 90 CW"):
                undo(uname, redo=True)          # (the item reads Redo now)
                check("%s: redone" % item,
                      master() == bytes(wm) and B("px_dirty") == 1)
                undo(uname)
                check("%s: undone again" % item, master() == m0)
        # --- CHAINED: the cube, edited, then a kernel through it ---------------
        menu("Effects", "Blur")
        wait_op("Blur")
        menu("Image", "Invert")
        wait_op("Invert")
        menu("Effects", "Sharpen")
        wait_op("Sharpen")
        b1 = P.op_conv(m0, w0, h0, pal0, "blur")
        p1 = P.pal_invert(P.CUBE)
        b2 = P.op_conv(bytes(b1[0]), w0, h0, p1, "sharpen")
        check("Blur, Invert, Sharpen: pixelsim's chain",
              master() == bytes(b2[0]) and rec()["pmode"] == b2[3],
              "first diff %s" % (first_diff(master(), bytes(b2[0])),))
        m.ctrl("KeyR")                          # Revert: the file again
        M.until(m, lambda _: idle() and master() == m0, "the revert",
                poll=0.3, limit=600)
        check("Revert: the file's master, nothing to undo, saved",
              B("px_ukind") == 0 and B("px_dirty") == 0)
        # --- CANCEL ------------------------------------------------------------
        before = claims()
        menu("Effects", "Blur")
        M.until(m, lambda _: W("px_erow") >= 4, "Blur under way",
                poll=0.05, limit=300)
        m.key("Escape")
        wait_op("the cancel")
        check("Esc in Blur: the picture as it was",
              master() == m0 and pal() == pal0 and rec()["pmode"] == mode0)
        check("Esc in Blur: claims as they were, nothing to undo",
              claims() == before and B("px_ukind") == 0,
              "%s -> %s, ukind %d" % (before, claims(), B("px_ukind")))
        # --- TOOLS -------------------------------------------------------------
        def view():
            b = m.read(base + syms["px_vcan"], 16)
            ix, iy = struct.unpack_from("<hh", b, 0)
            hs, vs = struct.unpack_from("<II", b, 4)
            return P.GuestView(ix, iy, hs, vs, u16(b, 12), u16(b, 14))

        def s2m(x, y):
            v = view()
            mx, my = v.master_xy(x, y)
            return min(max(mx, 0), w0 - 1), min(max(my, 0), h0 - 1)
        m.key("KeyM")
        M.ui_done(m, "Marquee")
        v = view()
        x0, y0 = v.ix + v.dw // 5, v.iy + v.dh // 5
        x1, y1 = v.ix + v.dw // 2, v.iy + v.dh // 2
        ui.mo.to(x0, y0)
        ui.mo._edge(True)
        ui.mo.to(x1, y1, l=True)
        M.guest_sleep(m, 0.3)
        ui.mo._edge(False)
        M.ui_done(m, "the drag")
        want = s2m(x0, y0) + s2m(x1, y1)
        got = struct.unpack("<4H", m.read(base + syms["px_selr"], 8))
        check("Marquee: the selection the drag's pixels",
              B("px_sel") == 1 and got == want and B("px_mqon") == 1,
              "selr %r want %r sel %d on %d" % (got, want, B("px_sel"),
                                              B("px_mqon")))
        m.key("ArrowRight")
        M.ui_done(m, "nudge")
        got2 = struct.unpack("<4H", m.read(base + syms["px_selr"], 8))
        check("Marquee: an arrow nudges it a master pixel",
              got2 == (got[0] + 1, got[1], got[2] + 1, got[3]), "%r" % (got2,))
        m.key("Escape")
        M.ui_done(m, "deselect")
        check("Marquee: Esc drops it", B("px_sel") == 0 and B("px_mqon") == 0)
        m.key("KeyC")                           # the Crop tool
        M.ui_done(m, "Crop")
        ui.mo.to(x0, y0)
        ui.mo._edge(True)
        ui.mo.to(x1, y1, l=True)
        M.guest_sleep(m, 0.3)
        ui.mo._edge(False)
        M.ui_done(m, "the crop's drag")
        cs = struct.unpack("<4H", m.read(base + syms["px_selr"], 8))
        m.key("Enter")
        M.ui_done(m, "Enter")
        wait_op("the crop")
        wm = P.op_crop(m0, w0, h0, *cs)
        check("Crop tool: drag and Enter crop to it",
              master() == bytes(wm[0]) and rec()["mw"] == wm[1],
              "rec %r want %dx%d" % (rec(), wm[1], wm[2]))
        undo("Crop")
        m.key("KeyZ")                           # the Zoom tool
        M.ui_done(m, "Zoom")
        z0 = struct.unpack("<I", m.read(base + syms["px_z"], 4))[0]
        fit0 = B("px_zfit")
        v = view()
        ui.mo.click(v.ix + v.dw // 3, v.iy + v.dh // 3)
        M.ui_done(m, "zoom click")
        z1 = struct.unpack("<I", m.read(base + syms["px_z"], 4))[0]
        check("Zoom tool: a click steps in", B("px_zfit") == 0
              and (fit0 or z1 > z0), "z %d -> %d fit %d" % (z0, z1, fit0))
        m.key("KeyE")                           # the Eyedropper
        M.ui_done(m, "Eyedropper")
        v = view()
        ex, ey = v.ix + 50, v.iy + 40
        ui.mo.click(ex, ey)
        M.ui_done(m, "pin")
        M.guest_sleep(m, 0.5)
        mx, my = s2m(ex, ey)
        idx = m0[my * w0 + mx]
        want = "%d,%d #%02X%02X%02X %d" % ((mx, my) + pal0[idx] + (idx,))
        iv = syms["px_ival"] + 8 * 24
        got = m.read(base + iv, 24).split(b"\0")[0].decode()
        check("Eyedropper: the click pins x,y #RRGGBB index in Image Info",
              got == want, "got %r want %r" % (got, want))
        sv = lambda k: m.read(base + syms["px_sv"] + 16 * k, 16).split(  # noqa
            b"\0")[0].decode()
        fields = (sv(1), sv(3), sv(2))      # DIMS, ZOOM, FMT: the readout's
        M.ui_done(m, "a repaint or two")
        check("Eyedropper: the status bar reads x,y / #RRGGBB / index",
              fields == tuple(want.split(" ")) and
              (sv(1), sv(3), sv(2)) == fields,
              "%r, then %r, want %r" % (fields, (sv(1), sv(3), sv(2)),
                                        want.split(" ")))
        m.key("KeyR")                           # the Rotate tool
        M.ui_done(m, "Rotate")
        m.key("Digit0")
        M.ui_done(m, "Fit")
        v = view()
        ui.mo.click(v.ix + v.dw // 2, v.iy + v.dh // 2)
        M.ui_done(m, "rotate click")
        wait_op("the turn")
        check("Rotate tool: a click turns it clockwise",
              master() == bytes(P.op_rotate(m0, w0, h0, 1)[0]))
        undo("Rotate CW")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga_144")
    ap.add_argument("--host-only", action="store_true")
    a = ap.parse_args()
    host_leg()
    if not a.host_only:
        guest_leg(a.machine)
    print("pxedit: %s" % ("ok" if not FAIL else "%d FAILED" % len(FAIL)))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
