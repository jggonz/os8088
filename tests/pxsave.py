#!/usr/bin/env python3
"""PiXEL'S SAVE AS, BYTE FOR BYTE AGAINST THE REFERENCE (SPEC.md 106.24).

    python3 tests/pxsave.py [--machine os8088_xt_vga_144] [--host-only]

THE HOST LEG first, when Unicorn is installed (tests/pxpartemu.py; without
it the leg SKIPS and says so): the WRITE part - build/pxwrite.bin, the bytes
PIXEL.O88 carries - run on a faked package, every format over seven sizes,
three pictures and the three palette modes, then over five ring shapes (a
slot of 512 bytes to one of 32K, one slot or two) and a RECT (Copy's one
slot): each file tools/pixelsim.py's write_as to the byte, and PIX's
one-segment refusal where pixelsim refuses.

THE GUEST LEG on MartyPC (the VGA XT; --machine for another), CITY.PCX and
CAT.GIF on a scratch floppy:

  FORMATS   Invert, then Save As in each of PNG, GIF, BMP, PCX and PIX - the
            card, the Standard File dialog, a name typed with the format's
            extension: each file read back off the floppy is pixelsim's to
            the byte, decodes to the master and palette shown, the window
            names it and nothing is unsaved; Blur (the cube) and BMP 24-bit,
            chosen on the card's drop-down
  REPLACE   Save As over a name that is taken: "Replace OUT.PCX?" No leaves
            the file as it was, Yes writes it
  UNSAVED   an edit, then Space (Next): "Save changes?" Cancel stays and
            keeps the edit; Discard goes on; an edit, Space and Save writes
            the picture over its own file first; an edit and the close box:
            Cancel keeps the window
  ESC       Esc in a PNG's save: no file, no PXSAVE.TMP, claims as before
  COPY      the whole of CITY.PCX refused with its size; a selection's BMP
            on the clipboard, pixelsim's to the byte
  FULL      a save that does not fit says "Disk full", leaves the file it
            would have replaced as it was and no PXSAVE.TMP
"""
import argparse
import functools
import os
import random
import struct
import sys

print = functools.partial(print, flush=True)
ROOT = os.path.dirname(os.path.abspath(__file__)) + "/.."
os.chdir(ROOT)
sys.path.insert(0, "tests")
sys.path.insert(0, "tools")
import pixelsim as P            # noqa: E402
import pxpartemu                # noqa: E402

FAIL = []
WF = dict(PNG=0, GIF=1, BMP=2, PCX=3, PIX=4, BMP24=5)


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
def save(fmt, m, w, h, pal, mode, chunk=8192, nslot=2, rect=None):
    """The WRITE part's file, drained slot by slot as the UI task would."""
    p = pxpartemu.PxPart("build/pxwrite.bin")
    p.set_pal(pal)
    p.set_master(m, w, h)
    p.w8("px_cur", mode, 37)
    if rect:
        x1, y1, x2, y2 = rect
        p.wbytes("px_wsel", struct.pack("<4H", x1 | 0x8000, y1, x2, y2))
        chunk, nslot = 0xFFFF, 1
        n = 1078 + ((x2 - x1 + 4) & ~3) * (y2 - y1 + 1)
        segs = [p.claim(n + 16)]
    else:
        segs = [p.claim(chunk) for _ in range(nslot)]
    for k, s in enumerate(segs):
        p.w16("px_sseg", s, 2 * k)
    p.w16("px_chunk", chunk)
    p.w8("px_nslot", nslot)
    out = bytearray()
    st = {"k": 0}

    def drain(pp):
        while pp.r8("px_sfull", st["k"]):
            n = pp.r16("px_slen", 2 * st["k"])
            out.extend(pp.seg_read(segs[st["k"]], n))
            pp.w8("px_sfull", 0, st["k"])
            st["k"] = (st["k"] + 1) % nslot
    p.on_alive = drain
    ax, cf = p.decode(WF[fmt] | 0x80)
    if cf:
        return None
    ax, cf = p.decode(WF[fmt])
    if rect:
        out.extend(p.seg_read(segs[0], p.r16("px_slen")))
    else:
        drain(p)
    return bytes(out)


def host_leg():
    if not pxpartemu.available():
        print("== host leg SKIPPED: no Unicorn (pip install unicorn) ==")
        return
    print("== the WRITE part on the host, against tools/pixelsim.py ==")
    pal = [((i * 37) & 255, (i * 91) & 255, (i * 13) & 255) for i in range(256)]
    rnd = random.Random(3)
    n = bad = 0
    for (w, h) in [(13, 7), (1, 1), (2, 3), (64, 33), (300, 2), (200, 150),
                   (37, 41)]:
        pats = [bytes(rnd.randrange(256) for _ in range(w * h)),
                bytes(((x * 7 + y * 31) ^ (x * y)) & 255 for y in range(h)
                      for x in range(w)),
                bytes((x // 5) & 255 for y in range(h) for x in range(w))]
        for m in pats:
            for pl, mode in ((pal, P.PM_PAL), (P.CUBE, P.PM_CUBE),
                             (P.GREY, P.PM_GREY)):
                for fmt in WF:
                    if fmt == "BMP24" and mode != P.PM_CUBE:
                        continue
                    n += 1
                    want = P.write_as(fmt, m, w, h, pl, mode)
                    got = save(fmt, m, w, h, pl, mode)
                    if got != want:
                        bad += 1
                        print("      %s %dx%d mode %d: %s" % (
                            fmt, w, h, mode,
                            "refused" if got is None else
                            first_diff(got, want or b"")))
    check("%d files (formats x sizes x pictures x modes)" % n, not bad,
          "%d differ" % bad)
    w, h = 120, 90
    m = bytes(rnd.randrange(256) if (i // 300) & 1 else (i % 37)
              for i in range(w * h))
    n = bad = 0
    for chunk, ns in ((512, 2), (1024, 1), (32768, 1), (16384, 2), (4096, 2)):
        for fmt in ("BMP", "PCX", "GIF", "PNG", "PIX"):
            n += 1
            if save(fmt, m, w, h, pal, 0, chunk, ns) != \
                    P.write_as(fmt, m, w, h, pal, 0):
                bad += 1
                print("      %s through %d x %d" % (fmt, ns, chunk))
    check("%d files through five ring shapes" % n, not bad, "%d differ" % bad)
    n = bad = 0
    for rect in ((0, 0, w - 1, h - 1), (10, 20, 109, 79), (5, 5, 5, 5),
                 (3, 0, 61, 89)):
        n += 1
        x1, y1, x2, y2 = rect
        cm, cw, ch = P.op_crop(m, w, h, *rect)
        if save("BMP", m, w, h, pal, 0, rect=rect) != \
                P.write_bmp8(cm, cw, ch, pal):
            bad += 1
            print("      Copy's BMP of %r" % (rect,))
    check("%d copies of a rect (one slot)" % n, not bad, "%d differ" % bad)


# =============================================================================
# THE GUEST LEG
# =============================================================================
def guest_leg(machine):
    import os88marty as M
    import os88ui
    import os88flush
    import heapmap
    from pxsyms import pkg_syms, instance, u16
    syms, image = pkg_syms()
    big = machine.endswith("_144")
    size = 1440 if big else 360
    sysdisk = "build/os8088.img" if big else "build/os8088-360.img"
    DISK = M.scratch_disk("build/pxsave.img", "build/pixel.o88",
                          "build/PIXEL.GFX", "apps/pixel/samples/CITY.PCX",
                          "apps/pixel/samples/CAT.GIF", size=size)
    city = open("apps/pixel/samples/CITY.PCX", "rb").read()
    q = P.decode(city, "PCX")
    m0, w0, h0, mode0, pal0 = P.emit(q, 0)
    m0, pal0 = bytes(m0), P.pal_full(pal0)
    print("== PiXEL's Save As on %s, CITY.PCX (SPEC.md 106.24) ==" % machine)
    with os88ui.boot(sysdisk, apps=DISK, machine=machine) as ui:
        m = ui.m
        S = ui._S
        fl = os88flush.Flush(marty=m)
        ui.path("B:/CITY.PCX")
        M.until(m, lambda _: instance(m, S, image), "PiXEL's instance",
                poll=0.3, limit=120)
        seg = instance(m, S, image)
        base = seg * 16
        B = lambda n, k=0: m.read(base + syms[n] + k, 1)[0]      # noqa
        W = lambda n, k=0: u16(m.read(base + syms[n] + k, 2))    # noqa
        m.write(base + syms["px_thoff"], b"\1")

        def idle():
            return B("px_busy") == 0 and B("px_job") == 0
        M.until(m, lambda _: W("px_ndone") and idle(), "the decode",
                poll=0.3, limit=600)
        M.ui_done(m, "decoded")

        def rec():
            b = m.read(base + syms["px_cur"], 50)
            return dict(name=bytes(b[:13]).split(b"\0")[0].decode(),
                        mw=u16(b, 32), mh=u16(b, 34), pmode=b[37],
                        mseg=u16(b, 38))

        def pal():
            b = m.read(base + syms["px_pal"], 768)
            return [tuple(b[3 * i:3 * i + 3]) for i in range(256)]

        def master():
            r = rec()
            return bytes(m.read(r["mseg"] * 16, r["mw"] * r["mh"]))

        def claims():
            hm = heapmap.Map(m, {n: S(n) for n in ("mem_base", "mem_top",
                                                   "spl_live", "mem_tab")})
            ps = []
            for k in range(9):
                r = m.read(base + syms["op_table"] + 10 + 8 * k, 8)
                if r[1] & 32:
                    ps.append(u16(r, 6))
            return sorted((c.seg, c.para) for c in hm.claims
                          if c.own == seg and c.seg != seg
                          and not any(c.seg <= p < c.seg + c.para
                                      for p in ps))

        def files():
            return set(fl.volume(1).names())

        def disk(name):
            return bytes(fl.volume(1).read(name))

        def wait_idle(what, limit=1800):
            M.ui_done(m, what)
            M.until(m, lambda _: idle() and B("px_pcon") == 0
                    and B("px_lvpend") == 0, what, poll=0.5, limit=limit)
            M.ui_done(m, what)

        def menu(mn, item):
            ui.menu_pick(mn, item)
            M.ui_done(m, item)

        def alert_up():
            return W("os88ui_awin")

        def alert_btn(k, n=3):
            """Click button k of the alert (os88ui_arect's arithmetic)."""
            w = ui.windows()[-1]
            row = n * 84 - 12
            x = w.x + (w.w - row) // 2 + 84 * k + 36
            y = w.y + 18 + 46 + 6
            ui.mo.click(x, y)
            M.ui_done(m, "alert button %d" % k)

        def save_as(name, downs=0, replace=None):
            """The card (DOWN pressed `downs` times), the dialog, `name`
            typed, and - when the name is taken - `replace` answered."""
            menu("File", "Save As...")
            check("Save As %s: the card" % name, B("px_pcon") == 6)
            for _ in range(downs):
                m.key("ArrowDown")
                M.ui_done(m, "down")
            m.key("Enter")
            M.ui_done(m, "the card's OK")
            ui.wait_window("Save", limit=60)
            m.type_text("\b" * 12 + name)
            M.ui_done(m, "typed")
            m.key("Enter")
            M.ui_done(m, "the dialog's OK")
            if replace is not None:
                M.until(m, lambda _: W("os88ui_awin"), "Replace?",
                        poll=0.3, limit=60)
                M.ui_done(m, "asked")
                m.key("Enter" if replace else "Escape")
                M.ui_done(m, "replace answered")
            wait_idle("the save of %s" % name)

        # --- FORMATS -----------------------------------------------------------
        menu("Image", "Invert")
        wait_idle("Invert")
        mm, pl, r = master(), pal(), rec()
        for fmt, ext in (("PNG", "PNG"), ("GIF", "GIF"), ("BMP", "BMP"),
                         ("PCX", "PCX"), ("PIX", "PIX")):
            nm = "OUT." + ext
            t0 = int(m.status().get("cycles", 0))
            save_as(nm)
            print("      %s: %.1f s of the guest's, card and dialog included"
                  % (nm, (int(m.status().get("cycles", 0)) - t0)
                     / M.GUEST_HZ))
            have = files()
            check("%s: written, no PXSAVE.TMP" % nm,
                  nm in have and "PXSAVE.TMP" not in have, "%s" % sorted(have))
            if nm not in have:
                continue
            data = disk(nm)
            want = P.write_as(fmt, mm, r["mw"], r["mh"], pl, r["pmode"])
            check("%s: pixelsim's %d bytes" % (nm, len(want)), data == want,
                  "%d bytes, first diff %s" % (len(data),
                                               first_diff(data, want)))
            d = P.decode(data, ext)
            dm, dw, dh, dmode, dpal = P.emit(d, 0)
            if fmt == "PIX":                    # (sixteen colours, by
                check("%s: decodes, %dx%d" % (nm, dw, dh),  # nearest)
                      (dw, dh) == (r["mw"], r["mh"]))
            else:
                check("%s: decodes to the master and palette shown" % nm,
                      bytes(dm) == mm and (dw, dh) == (r["mw"], r["mh"])
                      and P.pal_full(dpal)[:256] == pl)
            check("%s: the window's, nothing unsaved" % nm,
                  rec()["name"] == nm and B("px_dirty") == 0
                  and ui.window("PiXEL - " + nm) is not None,
                  "%r dirty %d" % (rec(), B("px_dirty")))
            check("%s: the toast" % nm, ui.toast()[0] == "Saved " + nm,
                  "%r" % (ui.toast(),))
        menu("Effects", "Blur")
        wait_idle("Blur")
        mm, pl, r = master(), pal(), rec()
        check("Blur: the cube", r["pmode"] == P.PM_CUBE)
        sel = W("px_fdrop", 12)
        save_as("OUT24.BMP", downs=(5 - P_WF_of(rec()["name"])) % 6)
        data = disk("OUT24.BMP") if "OUT24.BMP" in files() else b""
        want = P.write_as("BMP24", mm, r["mw"], r["mh"], pl, r["pmode"])
        check("OUT24.BMP: BMP 24-bit, pixelsim's", data == want,
              "%d bytes, first diff %s (sel was %d)" % (
                  len(data), first_diff(data, want), sel))
        # --- REPLACE -----------------------------------------------------------
        before = disk("OUT.PCX")
        menu("Image", "Invert")
        wait_idle("Invert")
        mm, pl, r = master(), pal(), rec()
        save_as("OUT.PCX", replace=False)
        check("Replace OUT.PCX? No: the file as it was",
              disk("OUT.PCX") == before and B("px_dirty") == 1)
        save_as("OUT.PCX", replace=True)
        want = P.write_as("PCX", mm, r["mw"], r["mh"], pl, r["pmode"])
        check("Replace OUT.PCX? Yes: written",
              disk("OUT.PCX") == want and B("px_dirty") == 0
              and "PXSAVE.TMP" not in files())
        # --- ESC -----------------------------------------------------------------
        menu("Image", "Invert")
        wait_idle("Invert")
        have, c0 = files(), claims()
        menu("File", "Save As...")
        for _ in range((0 - W("px_fdrop", 12)) % 5):
            m.key("ArrowDown")
            M.ui_done(m, "down")
        m.key("Enter")
        ui.wait_window("Save", limit=60)
        m.type_text("\b" * 12 + "ESC.PNG")
        m.key("Enter")
        M.until(m, lambda _: B("px_busy") == 4 and W("px_erow") >= 8,
                "the PNG under way", poll=0.05, limit=600)
        m.key("Escape")
        wait_idle("the cancel")
        check("Esc in a save: no file, no PXSAVE.TMP",
              files() == have, "%s -> %s" % (sorted(have), sorted(files())))
        check("Esc in a save: claims as before, still unsaved",
              claims() == c0 and B("px_dirty") == 1,
              "%s -> %s" % (c0, claims()))
        check("Esc in a save: says so", "Not saved" in ui.toast()[0],
              "%r" % (ui.toast(),))
        # --- UNSAVED -------------------------------------------------------------
        name0 = rec()["name"]

        def opened(n0):
            """A picture opened since px_ndone read n0 - its decode done,
            not only its record named (the decode starts after)."""
            return W("px_ndone") != n0 and idle()
        m.key("Space")
        M.until(m, lambda _: alert_up(), "Save changes?", poll=0.3,
                limit=60)
        m.key("Escape")
        M.ui_done(m, "Cancel")
        check("Next with an edit, Cancel: here, the edit kept",
              rec()["name"] == name0 and B("px_dirty") == 1)
        m.key("Space")
        M.until(m, lambda _: alert_up(), "Save changes?", poll=0.3,
                limit=60)
        n0 = W("px_ndone")
        alert_btn(1)                                    # Discard
        M.until(m, lambda _: opened(n0) and rec()["name"] != name0,
                "the next picture", poll=0.5, limit=600)
        check("Next with an edit, Discard: on to the next picture, clean",
              B("px_dirty") == 0)
        name1 = rec()["name"]
        own = disk(name1)
        menu("Image", "Invert")
        wait_idle("Invert")
        mm, pl, r = master(), pal(), rec()
        ext = name1.rsplit(".", 1)[1]
        m.key("Space")
        M.until(m, lambda _: alert_up(), "Save changes?", poll=0.3,
                limit=60)
        n0 = W("px_ndone")
        m.key("Enter")                                  # Save
        M.until(m, lambda _: opened(n0) and rec()["name"] != name1,
                "the save, then the next picture", poll=0.5, limit=1800)
        M.ui_done(m, "the next picture")
        fmt = {"PNG": "PNG", "GIF": "GIF", "BMP": "BMP", "PCX": "PCX",
               "PIX": "PIX"}.get(ext)
        if fmt and r["mw"] * r["mh"]:
            want = P.write_as(fmt, mm, r["mw"], r["mh"], pl, r["pmode"])
            check("Next with an edit, Save: %s written over itself, then on"
                  % name1, disk(name1) == want and disk(name1) != own)
        menu("Image", "Invert")
        wait_idle("Invert")
        w = ui.window("PiXEL")
        ui.mo.click(w.x + 8, w.y + 8)                   # the close box
        M.until(m, lambda _: alert_up(), "Save changes?", poll=0.3,
                limit=60)
        m.key("Escape")
        M.ui_done(m, "Cancel")
        check("Close with an edit, Cancel: the window stays",
              instance(m, S, image) == seg and B("px_dirty") == 1)
        menu("Edit", "Undo Invert")
        wait_idle("undo")
        # --- COPY ----------------------------------------------------------------
        r = rec()
        menu("Edit", "Copy")
        M.ui_done(m, "Copy")
        n = 1078 + ((r["mw"] + 3) & ~3) * r["mh"]
        txt = ui.toast()[0]
        if n > 32 * 1024:
            check("Copy of the whole picture: refused with its size",
                  txt == "Copy: %dK, over 32K" % ((n + 1023) // 1024),
                  "%r" % txt)
        sel = (10, 20, 109, 99)
        menu("Edit", "Select All")
        m.write(base + syms["px_selr"], struct.pack("<4H", *sel))
        menu("Edit", "Copy")
        wait_idle("Copy")
        cm, cw, ch = P.op_crop(master(), r["mw"], r["mh"], *sel)
        want = P.write_bmp8(cm, cw, ch, pal())
        cseg, cn = ui._word("clip_seg"), ui._word("clip_bytes")
        got = bytes(m.read(cseg * 16, cn)) if cseg else b""
        check("Copy of a selection: pixelsim's BMP on the clipboard",
              got == want, "%d bytes, first diff %s" % (
                  len(got), first_diff(got, want)))
        check("Copy: says so", ui.toast()[0] == "Copied to the clipboard")


def P_WF_of(name):
    """The card's drop-down opens on the shown picture's own format."""
    return WF.get(name.rsplit(".", 1)[-1].upper(), 0) if "." in name else 0


def full_leg(machine):
    """A save that does not fit: a 360KB floppy filled to 20K free."""
    import os88marty as M
    import os88ui
    import os88flush
    from pxsyms import pkg_syms, instance, u16
    syms, image = pkg_syms()
    big = machine.endswith("_144")
    cap = 1440 * 1024 - 33 * 512 if big else 354 * 1024
    ins = ["build/pixel.o88", "build/PIXEL.GFX", "apps/pixel/samples/CITY.PCX"]
    used = sum((os.path.getsize(f) + 1023) // 1024 for f in ins) * 1024
    fill = "build/pxfill.bin"
    n = max(cap - used - 20 * 1024 - (512 if big else 1024) * 4, 0)
    if not os.path.exists(fill) or os.path.getsize(fill) != n:
        open(fill, "wb").write(bytes(n))
    DISK = M.scratch_disk("build/pxfull.img", *(ins + [fill]),
                          size=1440 if big else 360)
    sysdisk = "build/os8088.img" if big else "build/os8088-360.img"
    print("== a save that does not fit (SPEC.md 106.24) ==")
    with os88ui.boot(sysdisk, apps=DISK, machine=machine) as ui:
        m = ui.m
        S = ui._S
        fl = os88flush.Flush(marty=m)
        ui.path("B:/CITY.PCX")
        M.until(m, lambda _: instance(m, S, image), "PiXEL's instance",
                poll=0.3, limit=120)
        base = instance(m, S, image) * 16
        B = lambda n, k=0: m.read(base + syms[n] + k, 1)[0]      # noqa
        W = lambda n, k=0: u16(m.read(base + syms[n] + k, 2))    # noqa
        m.write(base + syms["px_thoff"], b"\1")
        idle = lambda: B("px_busy") == 0 and B("px_job") == 0   # noqa
        M.until(m, lambda _: W("px_ndone") and idle(), "the decode",
                poll=0.3, limit=600)
        M.ui_done(m, "decoded")
        old = bytes(fl.volume(1).read("CITY.PCX"))
        ui.menu_pick("Image", "Invert")
        M.until(m, lambda _: B("px_dirty") == 1 and idle(), "Invert",
                poll=0.3, limit=300)            # (its plans: the worker's)
        M.ui_done(m, "Invert")
        ui.menu_pick("File", "Save As...")
        M.ui_done(m, "card")
        for _ in range((2 - W("px_fdrop", 12)) % 5):    # BMP: 77K
            m.key("ArrowDown")
            M.ui_done(m, "down")
        m.key("Enter")
        ui.wait_window("Save", limit=60)
        m.type_text("\b" * 12 + "CITY.PCX")
        m.key("Enter")
        M.until(m, lambda _: W("os88ui_awin"), "Replace CITY.PCX?",
                poll=0.3, limit=60)
        M.ui_done(m, "asked")
        m.key("Enter")                                  # Replace? Yes
        said = ui.wait_toast(says="not saved", limit=600)
        M.until(m, lambda _: idle(), "the save", poll=0.5, limit=1800)
        M.ui_done(m, "said")
        names = set(fl.volume(1).names())
        check("Disk full: said", said == "Disk full: not saved",
              "%r" % (said,))
        check("Disk full: CITY.PCX as it was, no PXSAVE.TMP",
              bytes(fl.volume(1).read("CITY.PCX")) == old
              and "PXSAVE.TMP" not in names, "%s" % sorted(names))
        check("Disk full: still unsaved", B("px_dirty") == 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga_144")
    ap.add_argument("--host-only", action="store_true")
    a = ap.parse_args()
    host_leg()
    if not a.host_only:
        guest_leg(a.machine)
        full_leg(a.machine)
    print("pxsave: %s" % ("ok" if not FAIL else "%d FAILED" % len(FAIL)))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
