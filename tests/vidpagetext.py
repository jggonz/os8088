#!/usr/bin/env python3
"""THE FULL SCREEN'S TEXT ON A PAGE-AHEAD PLAY - SPEC.md 98.3.13, 98.3.8.3.

    make && python3 tests/vidpagetext.py

The owner's 486, a page-ahead GasGas: every toast blinked while the video
played. A page-ahead play decodes on the page SHOWN, and the text was put
back off it before every frame and drawn again after (vc_setup took no
flipped play), so it was off the glass for every decode; and its page 1
box went to VP_PAGE - Mode X's 19,200 - where a 640 x 400 screen's page is
32,000. Now the decode goes ROUND the box on the page shown, set up again
at every flip.

vidpage's clip (640 x 400, two flips) on MartyPC's VGA XT, full screen,
held at frame 18, R pressed (its toast kept up by its time poked), then
played on across both flips and Repeat's laps: forty looks at the glass,
each counting the box's pixels in the text's ink. The text must be there in
all but two (Repeat's join clears the canvas) and on BOTH pages. Broken on
purpose - the tree before the fix - it is there in none of the forty.

--pages 4 is the same on PAGE-AHEAD ON N PAGES (98.3.8.4): vidpage --pages
4's clip on sixteen colours' 320 x 200, its changes three frames apart onto
pages 1, 2 and 3 and then 0. The text has two saves and four pages, so a
save MOVES to a page about to be shown (vo_pgsave), its box put back where
it was first; the toast must be on the glass in every look but the joins and
seen on at least three pages. Broken on purpose - vo_pgsave a bare ret, the
save left where it was - the box goes onto the wrong page and the text is
off the glass after the second flip. Every look it misses must be at
Repeat's join, four at most: the join decodes key 0 onto all four pages
rather than two, so its gap is longer than two pages' and that is not a
fault.

--fpage is the same FLIPPED AND BUILT AHEAD (98.3.8.5): vidpage --fpage's
clip, every frame drawn on a page the glass is not showing and the box
drawn onto it before it is shown, both scene changes' pages COPIED - one
of them out of the page on the glass, whose box the copy must not carry as
picture (vo_cfix). The toast must be on the glass in every look but the
joins, on at least three pages. Then a held leg: the toast kept up across
the flip at frame 45 - the one whose record copies off the glass - and
ended, and the box's rows on the glass before frames 50 and 51 (both pages)
must be the clip's picture. With vo_cfix a bare ret the copy leaves the
text on the page drawn next as if it were picture, and it is still there
after the toast has gone.
"""

import os, sys, struct, subprocess, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [ROOT + "/tools", ROOT + "/tests"]
os.chdir(ROOT)
import os88ui, os88marty, os88build, os88vid as vid, os88geom as geom
from cycweb import pkg_syms
import vidpage
u16 = lambda b, i=0: struct.unpack_from("<H", b, i)[0]
syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
NPG = int(sys.argv[sys.argv.index("--pages") + 1]) \
    if "--pages" in sys.argv else 2
SW, SH = (320, 200) if NPG > 2 else (640, 400)
with tempfile.TemporaryDirectory(dir=ROOT + "/build") as tmp:
    v88 = vidpage.fclip(tmp, vid.LAY_LIN80, NPG, vid.SCR_320X200)[0] \
        if "--fpage" in sys.argv else \
        vidpage.mclip(tmp, vid.LAY_LIN80, NPG, vid.SCR_320X200)[0] \
        if NPG > 2 else vidpage.clip(tmp, vid.LAY_LIN80)[0]
    disk = os.path.join(tmp, "t.img")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk, "--size",
                    "360", os88build.at("build/video.o88"), v88], check=True,
                   capture_output=True)
    with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                     machine="os8088_xt_vga") as ui:
        m = ui.m
        w = ui.path("B:/PAGE.V88")
        rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = u16(rec, geom.W_SEG) << 4
        rb = lambda n: m.read(base + syms[n], 1)[0]
        rw = lambda n: u16(m.read(base + syms[n], 2))
        ww = lambda n, v: m.write(base + syms[n], struct.pack("<H", v))
        until = lambda c, w_, g=90.0: os88marty.until(m, c, w_, poll=0.3, limit=600.0, guest=g)
        until(lambda mm: rb("vp_loaded") == 1, "header", 30.0)
        ww("vp_stopat", 18)
        m.write(base + syms["vp_nowin"], b"\1")
        m.type_text("p")
        until(lambda mm: rb("vp_held") == 1 and rw("vp_done") == 18, "hold")
        m.key("KeyR")
        until(lambda mm: rb("vo_kind") == 3, "R's toast", 10)
        os88marty.pace(m, 0.3)
        print("   toast up: decoded round %d, on pages %d" % (rb("vo_clip"), rb("vo_drawn")))
        ww("vp_stopat", 0xFFFF)
        m.write(base + syms["vp_held"], b"\0")
        import numpy as np
        ty0, tx0 = rw("vp_ty0"), rw("vp_tx0")
        yo, xo, n = rw("vo_yoff"), rw("vo_xoff"), rb("vo_n")
        n_box = n + 2
        looks = []
        for k in range(40):
            m.pause()
            m.write(base + syms["vo_ttk"], m.read(ui._S("ticks"), 2))   # (kept up)
            fw, fh, img = m.fbuf(0)
            a = np.frombuffer(bytes(img), np.uint8).reshape(fh, fw, 3)
            sy, sx = fh / SH, fw / SW
            y0, y1 = int((ty0 + yo) * sy), int((ty0 + yo + 10) * sy)
            x0, x1 = int((tx0 + xo) * 8 * sx), int((tx0 + xo + n + 2) * 8 * sx)
            box = a[y0:y1, x0:x1].astype(int)
            pal = vid.Reader(v88).palette
            ii = rb("vo_ink4")
            c = np.array([(pal[3 * ii + j] * 255 + 31) // 63 for j in range(3)])
            ink = int((np.abs(box - c).max(2) <= 6).sum())
            looks.append((rw("vp_done"), ink, rb("vo_kind"), rb("vo_clip"), rb("vo_drawn"), rw("vp_foff")))
            m.run()
            os88marty.pace(m, 0.05)
        top = max(x[1] for x in looks)
        gone = [l[0] for l in looks if l[1] < top * 0.5]
        pages = set(l[5] for l in looks if l[1] >= top * 0.5)
        print("   %d looks: the text on the glass in %d, gone at frames %s; "
              "seen on %d page(s)" % (len(looks), len(looks) - len(gone),
                                      gone, len(pages)))
        bad = []
        # (on N pages the join decodes key 0 onto every page, not two, so
        # its gap is longer: every miss AT the join - the lap's last frame
        # or its first - and four at most)
        join = [g_ for g_ in gone if g_ not in (vidpage.NF, 1)]
        if not top or len(pages) < min(3, NPG) or \
                (len(gone) > 2 if NPG == 2 else (join or len(gone) > 4)):
            bad.append("the toast left the glass in %d of %d looks across "
                       "the flips (or was seen on %d page)" % (len(gone),
                                                              len(looks),
                                                              len(pages)))
        if "--fpage" in sys.argv:
            cvs = vidpage.fclip(tmp, vid.LAY_LIN80, NPG, vid.SCR_320X200,
                                name="REF.V88")[1]
            W = 224

            def keep(mm):
                m.write(base + syms["vo_ttk"], m.read(ui._S("ticks"), 2))

            def hold(n, up):
                ww("vp_stopat", n)
                m.write(base + syms["vp_held"], b"\0")
                until(lambda mm: (keep(mm) if up else None, rb("vp_held") == 1
                                  and rw("vp_done") == n)[1],
                      "the hold before frame %d" % n, 240.0)
            hold(44, False)
            for _ in range(2):          # (a toast again - Repeat off and on,
                m.key("KeyR")           # as it was)
                os88marty.pace(m, 0.3)
            until(lambda mm: rb("vo_kind") == 3, "R's toast", 10)
            keep(m)
            hold(47, True)
            print("   toast up at frame 47: kind %d, on pages %d"
                  % (rb("vo_kind"), rb("vo_drawn")))
            if rb("vo_kind") != 3:
                bad.append("the toast was not up across the flip at 45")
            t = u16(m.read(ui._S("ticks"), 2))
            m.write(base + syms["vo_ttk"], struct.pack("<H", (t - 200) & 0xFFFF))
            for n in (50, 51):
                hold(n, False)
                fw, fh, img = m.fbuf(0)
                a = np.frombuffer(bytes(img), np.uint8).reshape(fh, fw, 3)
                sy, sx = fh / SH, fw / SW
                pal = vid.Reader(v88).palette
                ref = cvs[n - 1]
                wrong = 0
                for y in range(yo, yo + 10):
                    for x in range(xo * 8, (xo + n_box) * 8):
                        if not 0 <= x < W:
                            continue
                        v = ref[y * W + x]
                        c = [(pal[3 * v + j] * 255 + 31) // 63
                             for j in range(3)]
                        px = a[int((ty0 + y + 0.5) * sy),
                               int((tx0 * 8 + x + 0.5) * sx)]
                        wrong += max(abs(int(px[j]) - c[j])
                                     for j in range(3)) > 6
                print("   toast ended (kind %d), hold before frame %d (page "
                      "%d): %d of the box's pixels not the picture"
                      % (rb("vo_kind"), n, rw("vp_foff") // rw("vp_page"),
                         wrong))
                if rb("vo_kind") or wrong:
                    bad.append("the box's rows before frame %d are not the "
                               "picture (%d pixels)" % (n, wrong))
        for b in bad:
            print("   FAIL: %s" % b)
        if not bad:
            print("   ok")
        sys.exit(1 if bad else 0)
