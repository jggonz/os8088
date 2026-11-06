#!/usr/bin/env python3
"""The Video Battle's VGA and Hercules RENDITIONS on MartyPC
(VBATTLE-MEMORY-PLAN 9): the demo battle built for each adapter
(tools/os88vbt.py `build --rendition`), its enemy at 1 HP, played on
os8088_xt_vga_hdd_sb (BATTLEV.VBT, mode 0Dh) and os8088_5150_herc_hdd_sb_gla
(BATTLEH.VBT, 720 x 348), each beside the CGA4 bundle.

    make vbattle && python3 tests/vbtadapters.py

For each adapter:
  - the engine took ITS bundle (`[vb_kind]`) and its mode;
  - the TITLE loop on the glass is the host's own decode of that rendition,
    byte for byte - VGA's four planes read off the card (`Marty.planes`),
    Hercules' memory as it is, the canvas at its byte 5;
  - Enter FADES it: the lit pixels fall to under a third;
  - at the menu, the BAND on the glass is the engine's own band buffer
    ([vb_cseg], two bits a pixel) as the host converts it - on VGA each
    colour its pixel value through the band map on all four planes (126.3.2),
    every pixel two by two on Hercules - so
    the present (vbband.inc's vb_bpres) is held exact;
  - the canvas shows the idle loop's own frames;
  - one Attack, and victory fades to its end loop, which plays.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import numpy as np          # noqa: E402
import os88build, os88geom as geom, os88marty, os88ui   # noqa: E402
import os88vbt as B         # noqa: E402
import os88vid as V         # noqa: E402
import vbtana as A          # noqa: E402
import vbtproj as P         # noqa: E402
from cycweb import pkg_syms     # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
KEYS = {"return": "Enter", "esc": "Escape"}
GS_MENU = 1
fails = []
MENU_UP = {}
RUNS = (("vga4", "os8088_xt_vga_hdd_sb", 1),
        ("herc", "os8088_5150_herc_hdd_sb_gla", 2))


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def lit(img):
    return int(sum(bin(x).count("1") for x in img))


def host_vga(surf, rows):
    return [bytes(surf[p * V.PLANE:p * V.PLANE + rows * 40]) for p in range(4)]


def band_vga(cbuf, by, rows, bmap=(0, 1, 2, 3)):
    """The band's buffer (80 bytes a row, 2 bits a pixel) as mode 0Dh's
    planes over rows BY..199, each colour its pixel VALUE through the
    bundle's band map (header 352, SPEC.md 126.3.2)"""
    a = np.frombuffer(cbuf, np.uint8)[:80 * (200 - by)].reshape(200 - by, 80)
    px = np.zeros((200 - by, 320), np.uint8)
    for k in range(4):
        px[:, k::4] = (a >> (6 - 2 * k)) & 3
    px = np.array(bmap, np.uint8)[px]
    out = []
    for p in range(4):
        out.append(np.packbits((px >> p) & 1, axis=1).tobytes())
    return out


def band_herc(cbuf, by, crows):
    """The band's buffer as Hercules rows CROWS.. (two a logical row), each
    pixel two by two, at byte 5"""
    a = np.frombuffer(cbuf, np.uint8)[:80 * (200 - by)].reshape(200 - by, 80)
    px = np.zeros((200 - by, 320), np.uint8)
    for k in range(4):
        px[:, k::4] = (a >> (6 - 2 * k)) & 3
    big = A.herc_of_cga(px, 2 * (200 - by))
    rows = np.packbits(big, axis=1)                 # (2 x band) x 80
    out = {}
    for i, row in enumerate(rows):
        y = crows + i
        out[(y & 3) * 8192 + (y >> 2) * 90 + 5] = row.tobytes()
    return out


def run(tmp, kind, machine, code, syms):
    vbp = os.path.join(tmp, "a.vbp")
    B.demo_project(vbp, bars=2)
    p = P.Project.load(vbp)
    p.set(("enemy", "hp"), 1)
    p.set(("target", "renditions"), ["cga4", kind])
    p.save()
    files = []
    for k in ("cga4", kind):
        data = B.build_project(p, kind=k)
        f = os.path.join(tmp, B.BUNDLE_NAMES[k])
        open(f, "wb").write(data)
        files.append((B.BUNDLE_NAMES[k], f))
    bt = B.battle_of(p, None, kind)     # the host's own pixels
    R = bt.rend
    B.REND = R
    tsurf = [R.surface(B.full_of(f), R.full) for f in bt.title["frames"]]
    isurf = [R.surface(f, bt.rows) for f in bt.idle]
    vhd = os.path.join(tmp, "c.vhd")
    cmd = [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
           "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
           "--vbr", os88build.at("build/boothd.bin"),
           "--mbr", os88build.at("build/mbr.bin"),
           "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
           "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
           "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
           "--file", "VBATTLE.O88=" + os88build.at("build/vbattle.o88")]
    for nm, f in files:
        cmd += ["--file", "%s=%s" % (nm, f)]
    subprocess.run(cmd, check=True, capture_output=True)
    m = os88marty.launch(None, machine=machine,
                         extra=["--mount", "hd:0:" + vhd], boot=False)
    try:
        m.run()
        ui = os88ui.UI(m)
        ui.up(limit=300)
        os88marty.no_saver(m)
        os88marty.ui_done(m)
        w = ui.path("C:/VBATTLE.O88")
        rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4

        def rb(nm, off=0):
            return m.read(base + syms[nm] + off, 1)[0]

        def rw(nm, off=0):
            return struct.unpack_from("<H", m.read(base + syms[nm] + off, 2))[0]

        def until(cond, what, guest=120.0, poll=0.05):
            os88marty.until(m, lambda mm: cond(), what, poll=poll,
                            limit=900.0, guest=guest)

        def key(k):
            m.key(KEYS[k], up=False)
            os88marty.until(m, lambda mm: True, "a key", poll=0.0,
                            limit=10.0, guest=0.06)
            m.key(KEYS[k], down=False)
            os88marty.until(m, lambda mm: True, "a key", poll=0.0,
                            limit=10.0, guest=0.25)

        def glass_full():
            if kind == "vga4":
                return m.planes(offset=0, length=200 * 40)
            return m.read(0xB0000, 32768)

        def host_full(s):
            if kind == "vga4":
                return host_vga(s, 200)
            out = bytearray(32768)
            out[5:] = bytes(s[:32768 - 5])
            return bytes(out)
        key("return")
        until(lambda: rb("vb_ready") == 1, "%s: the play to start" % kind)
        ok(rb("vb_kind") == code, "%s: the engine took BATTLE%s.VBT (kind %d)"
           % (kind, "V" if kind == "vga4" else "H", rb("vb_kind")))
        until(lambda: rb("vb_tplay") == 1, "%s: the title" % kind)
        want = [host_full(s) for s in tsurf]
        hits, looks = 0, 0
        for _ in range(40):
            m.pause()
            g = glass_full()
            m.run()
            looks += 1
            hits += g in want
            until(lambda: True, "a moment", guest=0.07, poll=0.0)
        ok(hits * 2 >= looks, "%s: the title on the glass is the host's own "
           "decode of the rendition (%d of %d looks exact)" % (kind, hits, looks))
        key("return")
        until(lambda: rb("vb_fon") == 1, "%s: the fade" % kind)
        seen = []
        while rb("vb_fon") == 1:
            m.pause()
            g = glass_full()
            m.run()
            seen.append((rb("vb_fstep"), lit(b"".join(g) if kind == "vga4"
                                              else g)))
            until(lambda: True, "a moment", guest=0.03, poll=0.0)
        early = [c for s_, c in seen if s_ <= 3]
        late = [c for s_, c in seen if s_ >= 13]
        # (the first look against the last: a step may jump ahead to the
        # clock's, 126.3.1, so the looks between are not one a step)
        ok(early and late and late[-1] * 3 < early[0],
           "%s: Enter fades it (lit pixels %s early, %s late)"
           % (kind, early[:1], late[-1:]))
        until(lambda: rb("vb_state") == GS_MENU and rb("vb_dirty") == 0,
              "%s: the menu" % kind, guest=180.0)
        # THE BAND: the menu's state comes before its band does - the band is
        # rebuilt off the screen (on MartyPC's XT ~1.2 s of the foreground's
        # time under the hook, as on CGA) and then presented (~0.6 s more
        # here, where CGA copies in ~0.2). So the glass is compared with the
        # engine's buffer every 0.2 guest seconds until they agree, for 30 at
        # most; a present that is wrong never agrees
        t, okb = 0.0, False
        while t < 30.0:
            m.pause()
            cseg = rw("vb_cseg") << 4
            by = rb("vb_meta", 65)      # (the header's band row, VH_BAND+1)
            cbuf = m.read(cseg, 80 * (200 - by))
            quiet = rb("vb_tcomp") == 0 and rb("vb_dirty") == 0
            if kind == "vga4":
                okb = m.planes(offset=by * 40, length=(200 - by) * 40) == \
                    band_vga(cbuf, by, 200 - by, R.bmap)
            else:
                mem = m.read(0xB0000, 32768)
                okb = all(mem[o:o + 80] == r for o, r in
                          band_herc(cbuf, by, bt.rows).items())
            okb = okb and quiet and any(cbuf[49 * 80:])
            if okb:
                break
            m.run()
            until(lambda: True, "a moment", guest=0.2, poll=0.0)
            t += 0.2
        tries = round(t, 1)
        # and the canvas: one of the idle loop's frames - at a look that does
        # not land inside the hook's decode of one (up to ten looks)
        if kind == "vga4":
            ic = [host_vga(s, bt.rows) for s in isurf]
        else:
            ic = [b"".join(bytes(s[(y & 3) * 8192 + (y >> 2) * 90:
                                   (y & 3) * 8192 + (y >> 2) * 90 + 80])
                           for y in range(bt.rows)) for s in isurf]
        canvas_ok = False
        for _look in range(10):
            if kind == "vga4":
                gc = m.planes(offset=0, length=bt.rows * 40)
            else:
                mem = m.read(0xB0000, 32768)
                gc = b"".join(mem[(y & 3) * 8192 + (y >> 2) * 90 + 5:
                                  (y & 3) * 8192 + (y >> 2) * 90 + 85]
                              for y in range(bt.rows))
            if gc in ic:
                canvas_ok = True
                break
            m.run()
            until(lambda: True, "a moment", guest=0.07, poll=0.0)
            m.pause()
        m.run()
        ok(okb and any(c for c in cbuf), "%s: the band on the glass is the "
           "engine's band buffer as the host converts it, exactly (%.1f guest "
           "s after the menu)" % (kind, tries))
        ok(canvas_ok, "%s: the canvas is one of the idle loop's frames"
           % kind)
        key("return")                   # Attack, at the 1-HP enemy
        until(lambda: rb("vb_eplay") == 1, "%s: victory's end loop" % kind,
              guest=180.0)
        ok(rb("vb_over") == 1, "%s: victory, and its end loop plays" % kind)
        key("esc")
        until(lambda: rb("vb_ready") == 0, "%s: Esc to leave" % kind)
    finally:
        m.quit()


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    only = sys.argv[1:] or [k for k, _, _ in RUNS]
    for kind, machine, code in RUNS:
        if kind in only:
            with tempfile.TemporaryDirectory() as tmp:
                run(tmp, kind, machine, code, syms)
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
