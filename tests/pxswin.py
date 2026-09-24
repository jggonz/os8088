#!/usr/bin/env python3
"""PIXELSTEIN 3D's two WINDOWS (SPEC.md 97.14; wave 5): WIN1, the 1bpp band
every desktop can show, and WIN4, the 16-colour one a VGA desktop shows on a
286 and up.

    python3 tests/pxswin.py                 # MartyPC os8088_xt_vga_herc
    python3 tests/pxswin.py --qemu          # QEMU's 386, VGA: WIN4 on the glass
    python3 tests/pxswin.py --price         # the 8086's price, os8088_xt_vga

THREE LEGS ON THREE MACHINES, because no one emulator has all three facts:

  --qemu (QEMU's 386 on a VGA; its `pc` CPU answers CPU_386): WIN4 IS THE
      DEFAULT there - px_back = WIN4 with no poke, the Colour item live and
      saying "On"; (q1) the band lands 8-ALIGNED (px_bx is a multiple of 8
      and is the content box's own left, OSAPI_WM_SNAP); (q2) the glass
      under the band is the shadow's bytes through the 32 -> 16 table, pixel
      for pixel, over the whole 512 x 80 - every shadow byte's colour index
      maps to exactly ONE colour on the glass and no two indices share one
      (the palette is the kernel's, so the map is read off the dump rather
      than restated), and at least six colours are in the picture; (q3) a
      turn on the unobscured window is PLANAR: one to four OSAPI_GFX_BLITPs
      a frame (px_nbp against px_frames: the 80-row range is 25 + 25 + 25 +
      5), no BLIT4, and the glass the table's pixel for pixel; (q3b) moved
      under the GAMES window and GAMES raised over it, the window is
      OBSCURED and a forced frame takes the BLIT4 fallback through the clip
      (px_nb4, no BLITP), the uncovered glass still the table's; (q4) A WINDOW MOVE
      COSTS ZERO REPAINTS: a title-bar drag of a still player composes no
      frame (px_frames) and takes no W_PAINT (px_npaint - the kernel's drag
      cache replays the content, SPEC.md 11.96.12) and the glass at the new
      place is still the shadow's; (q5) the eye poked to scene C (the
      brick room's corner: the gold key, the barrel, the guard) and they are
      sprites in the colour window (px_nsc) - the done-when's picture of
      16-colour walls AND sprites. QMP cannot write guest memory, so that
      one leg pokes through the gdb stub HMP's `gdbserver` opens (qpoke);
      every other leg only READS (QMP's pmemsave) and drives the keys and
      the mouse. The screendumps are
      (build/pxs-shots/win4-qemu-*.png).

  default, MartyPC os8088_xt_vga_herc (a 4.77 MHz XT, a VGA and a Hercules,
      extended RIGHT): (m1) on an 8086 the window is WIN1 and the Colour
      item is GREYED WITH ITS PRICE (px_s_colxt, SPEC.md 47) - the Detail
      menu photographed open; (m2) the band lands 8-aligned and the PEN
      path's glass (light grey on black on the VGA) is the shadow's bits;
      (m3) a move on the VGA costs zero repaints; (m4) dragged WHOLLY onto
      the Hercules the 1bpp path's glass is the same shadow's bits - so the
      two paths give IDENTICAL BITS - and the shadow did not move; (m5) THE
      SEAM TAKES THE RIGHT BYTE SET: with the CPU tier poked to a 286
      (px_tier - a machine this cycle-exact that is a 286 does not exist
      here) and Textured pinned, dragged back onto the VGA the window is
      WIN4 - Mode X's inks (px_inkf 0x0808) and byte set (px_btback = the
      Mode X class) and colour on the glass - and dragged onto the Hercules
      again it is WIN1 with the Hercules set, px_s_colmono greying the item;
      wholly on the VGA it went out PLANAR (px_nbp), and (m5b) dragged to
      STRADDLE the seam it is the restrictive card's (SPEC.md 39.16.4.2) -
      WIN1, no BLIT4 and no BLITP on a forced frame.
      (m1) also counts EVERY string that is or can become a menu item (read
      off the source) against MENU_MAXCH; (m4) holds the UNPOKED 8086 on the
      Hercules to px_s_colmono - the display's fact comes before the price
      (SPEC.md 47; review, wave 5); (m6) picks Detail > Colour through the
      menu at the poked 286 on the VGA: WIN1, "Colour: Off", the pen path's
      glass the shadow's bits and PXSTEIN.CFG's colour byte 0 off the
      floppy - and again: WIN4, "On", the table's colours and the byte 1.

  --price, MartyPC os8088_xt_vga: THE 8086'S PRICE IS MEASURED, not
      guessed (SPEC.md 47: grey a fact). The window at EVERY rung it can be
      put on (Flat Full, Textured Low res, Textured Full; Size 64), scene A
      turning, eight frames each way, WIN1 and WIN4 (the tier poked, as
      above), the frame from px_frame_begin to px_frame_begin by MartyPC's
      cycle counter and the present, the expand and the blits bracketed
      apart (present_times); and the number the greyed caption quotes must
      be the DEAREST WIN4 frame's within 15% - a fact about the item, not
      about its cheapest rung (review, wave 5: the first cut priced Flat
      Full alone), and every WIN4 strip of the unobscured window must have
      gone out PLANAR (OSAPI_GFX_BLITP - the second review: BLIT4 sent a
      Textured Full frame to 0.95 s, the planar present to 0.44).
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))     # LAST, so it wins (pxslib)
import dispcp                                                   # noqa: E402
import os88marty                                                # noqa: E402
import os88mouse                                                # noqa: E402
import os88sym                                                  # noqa: E402
import pxslib                                                   # noqa: E402

FAIL = []
S = os88sym.linear
WIN1, WIN4 = 5, 6
ROWS, STRIDE, BAND = 80, 80, 64
W4TAB = 9920 + 25 * 64 * 4              # pxgame.asm's PX_W4TAB
INK_MODEX, INK_HERC = 4, 3              # px_inkcls: Mode X's class, Hercules'
SHOTS = os.path.join(ROOT, "build", "pxs-shots")
MENU_MAXCH = 24                         # os88api.inc: a pull-down's glyphs


def check(ok, what):
    print("   %-72s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def cstr(b):
    return b.split(b"\0", 1)[0]


# --- the picture: the shadow's band, as the package composed it ----------------
def menu_captions():
    """Every px_s_* string that is or can become a MENU ITEM: the ones the
    px_i_* tables and px_modestr name, and the ones code moves into a px_i_*
    slot (px_adapter, px_tex_caption, px_toggle_captions) - read off the
    source, so a caption added later is counted without editing this."""
    import re                                               # noqa: E402
    src = ""
    for f in ("pxgame.asm", "pxrast.inc"):
        src += open(os.path.join(ROOT, "apps", "pixelstein", f)).read()
    names = set()
    for line in src.splitlines():
        code = line.split(";", 1)[0]
        if re.match(r"\s*(px_i_\w+|px_modestr):|\s*dw\s+px_s_", code) or \
                "[px_i_" in code or re.search(r"mov\s+ax,\s*px_s_(col|dtex|gofull|mnone)",
                                               code):
            names.update(re.findall(r"\bpx_s_\w+", code))
    return sorted(names)


def band_bytes(m, g, seg=None):
    """The shadow's band: Size bytes of each of the 80 view rows at x0."""
    sh = g.word("px_shseg") if seg is None else seg
    size, x0 = g.byte("px_size"), g.byte("px_x0")
    raw = m.read(sh << 4, ROWS * STRIDE)
    return [raw[r * STRIDE + x0:r * STRIDE + x0 + size] for r in range(ROWS)], size


def glass_mask(w, px, x0, y0, width, rows):
    """Lit (non-black) pixels of a rect of an RGB24 frame, as rows of bools."""
    out = []
    for r in range(rows):
        base = ((y0 + r) * w + x0) * 3
        row = px[base:base + width * 3]
        out.append([row[i] | row[i + 1] | row[i + 2] != 0 for i in range(0, width * 3, 3)])
    return out


def bits_of(band):
    return [[bool(b & (0x80 >> k)) for b in row for k in range(8)] for row in band]


def colour_check(w, px, x0, y0, band, table, what, col0=0):
    """Every pixel of the 512 x 80 rect is table[shadow byte]'s colour: the map
    index -> RGB is read off the glass and must be one-to-one. `col0` starts
    the check at that byte of each row (a window with another over its left
    part)."""
    idx2rgb, rgb2idx, bad, where = {}, {}, 0, []
    for r, row in enumerate(band):
        base = ((y0 + r) * w + x0) * 3
        for c, b in enumerate(row):
            if c < col0:
                continue
            ci = table[(r & 1) * 32 + (b & 31)] & 15   # the row's table:
                                    # a dark face is its lit colour on the
                                    # even rows, black on the odd (wave 6)
            for k in range(8):
                o = base + (c * 8 + k) * 3
                rgb = px[o:o + 3]
                if idx2rgb.setdefault(ci, rgb) != rgb or rgb2idx.setdefault(rgb, ci) != ci:
                    bad += 1
                    where.append((x0 + c * 8 + k, y0 + r, ci, tuple(rgb)))
    print("   %s: %d colour(s) in the band, %d pixel(s) off the table's map%s"
          % (what, len(idx2rgb), bad, (" - first at %s" % (where[:4],)) if where else ""))
    return len(idx2rgb), bad


def qpoke(q, writes, port=None):
    """Guest memory WRITTEN on QEMU, which QMP cannot do: HMP's `gdbserver`
    opens the gdb stub on a port, and one connection speaks the remote
    protocol's `M` packet (and `D`, which detaches and lets the guest run
    on) - the VM stands still between the attach and the detach, so a set
    of writes lands between two instructions as MartyPC's paused pokes do.
    `writes` is a list of (linear address, bytes)."""
    import socket                                           # noqa: E402
    if port is None:                        # a FREE port: several worktrees
        with socket.socket() as probe:      # run QEMU rows at once (review,
            probe.bind(("127.0.0.1", 0))    # wave 5 - it was fixed at 12357)
            port = probe.getsockname()[1]
    q.hmp("gdbserver tcp:127.0.0.1:%d" % port)
    sk = socket.create_connection(("127.0.0.1", port), timeout=10)

    def pkt(body):
        cs = sum(body.encode()) & 0xFF
        sk.sendall(("$%s#%02x" % (body, cs)).encode())
        buf = b""
        while not (b"#" in buf and len(buf) >= buf.index(b"#") + 3 and b"$" in buf):
            got = sk.recv(4096)
            if not got:
                break
            buf += got
        sk.sendall(b"+")
        return buf
    try:
        pkt("?")                            # the stop reply: attached, halted
        for addr, data in writes:
            r = pkt("M%x,%x:%s" % (addr, len(data), bytes(data).hex()))
            if b"OK" not in r:
                raise RuntimeError("pxswin: gdb stub refused a write at %05x: %r"
                                   % (addr, r))
        sk.sendall(b"$D#44")
        time.sleep(0.2)
    finally:
        sk.close()
    q.hmp("gdbserver none")


# =============================================================================
# QEMU: WIN4 by default, read only
# =============================================================================
def qemu_leg(a):
    import os88fixture, os88qemu                            # noqa: E401,E402
    from ethernet import Qemu, SOCK, settle, Mouse          # noqa: E402
    import shot as shotlib                                  # noqa: E402

    def mouse(*args):
        subprocess.run(["python3", "tools/mouse.py", SOCK] + [str(x) for x in args],
                       check=True, capture_output=True)

    def keys(*cmds):
        subprocess.run(["python3", "tools/qmp.py", SOCK] + list(cmds), check=True,
                       capture_output=True)

    def dump(q):
        p = os.path.join(q.tmp, "d.ppm")
        q.hmp('screendump "%s"' % p)
        return shotlib.read_ppm(p)

    def png(name, crop=None, zoom=None):
        os.makedirs(SHOTS, exist_ok=True)
        args = ["python3", "tools/shot.py", SOCK, os.path.join(SHOTS, name)]
        if crop:
            args += ["--crop", ",".join(str(v) for v in crop)]
        if zoom:
            args += ["--zoom", str(zoom)]
        subprocess.run(args, check=True, capture_output=True)
        print("   shot", os.path.join("build/pxs-shots", name))

    os88qemu.kill()
    os88qemu.own()
    r = os88fixture.make("test")
    if r.returncode:
        sys.exit("pxswin: make test failed:\n" + r.stdout + r.stderr)
    q = Qemu()

    mo = Mouse()
    settle(q)
    time.sleep(6)
    dispcp.open_drive(q, mo, S, settle, "B")
    wx, wy = dispcp.win_rect(q, S, dispcp.win_list(q, S)[-1])[:2]
    dispcp.open_named(q, mo, S, settle, wx, wy, "GAMES")
    wx, wy = dispcp.win_rect(q, S, dispcp.win_list(q, S)[-1])[:2]
    dispcp.open_named(q, mo, S, settle, wx, wy, pxslib.FILE)
    got = pxslib.find(q, S, limit=60.0)
    if got is None:
        sys.exit("pxswin: no Pixelstein window on QEMU")
    win, seg = got
    s = pxslib.syms()

    def rd(name, n):
        return q.read((seg << 4) + s[name], n)

    def b(name):
        return rd(name, 1)[0]

    def w(name):
        return pxslib.u16(rd(name, 2))

    t0 = time.time()
    while w("px_frames") < 1 and time.time() - t0 < 30:
        time.sleep(0.3)
    print("   QEMU: tier %d, the window's display %d bpp, backend %s, Colour %d"
          % (b("px_tier"), b("px_bpp"), pxslib.PXB.get(b("px_back")), b("px_colour")))
    check(b("px_tier") != 0 and b("px_bpp") == 4,
          "(q0) the machine is past the 8086 and the window's display is 16-colour")
    check(b("px_back") == WIN4, "(q0) ...so the window is WIN4 BY DEFAULT (px_back %d)"
          % b("px_back"))
    cap = pxslib.u16(rd("px_i_det", 14), 12)
    check(cap == s["px_s_colon"], "(q0) ...and Detail > Colour is live and says On")
    png("win4-qemu-attract.png")
    keys("sendkey spc")                     # the attract page's Space: PLAY
    t0 = time.time()
    while b("px_state") != pxslib.PXST["play"] and time.time() - t0 < 30:
        time.sleep(0.2)
    # THE POSE IS POKED, not held (review, wave 6 r2): the first cut held
    # `sendkey left 120` "so the two pickups at (6,1) and (9,1) are in the
    # view" - a hold QEMU's host times, onto a floor whose pickups have
    # since moved: tools/pxssim.py through pxsart.win4_tables() counts 7
    # WIN4 colours at scene A's own heading, 5 from -12 to -40 degrees and 4
    # further round, so the hold passed when it turned little and failed
    # (5 colours, twice in two soaks) when it turned more. Scene A's eye and
    # heading, poked with a whole frame owed so the pose is drawn
    import pxssim                                           # noqa: E402
    ax_, ay_, _h = pxslib.scene_at("a")
    hd = _h
    base = seg << 4
    writes = [(base + s["px_px"], ax_.to_bytes(2, "little")),
              (base + s["px_py"], ay_.to_bytes(2, "little")),
              (base + s["px_head"], hd.to_bytes(2, "little")),
              (base + s["px_hcos"], (pxssim.cos_q14(hd) & 0xFFFF).to_bytes(2, "little")),
              (base + s["px_hsin"], (pxssim.sin_q14(hd) & 0xFFFF).to_bytes(2, "little")),
              (base + s["px_pcell"], (((ay_ >> 8) << 6) | (ax_ >> 8)).to_bytes(2, "little")),
              (base + s["px_force"], b"\x01")]
    for name, fill in pxslib.FORCE_ALL:
        writes.append((base + s[name], bytes([fill]) * (2 * pxslib.COLMAX)))
    qpoke(q, writes)
    mouse("to", 630, 30)                    # the arrow off the band: it is the
    time.sleep(2.0)                         # desktop's, drawn over the glass
    f0 = w("px_frames")
    time.sleep(1.5)
    check(w("px_frames") == f0, "(q) the player stands: no frame in a second and a half")
    bx, by, cx = w("px_bx"), w("px_by"), w("px_cx")
    print("   the band at (%d,%d), the content box's left %d" % (bx, by, cx))
    check(bx % 8 == 0 and bx == cx, "(q1) the band lands 8-ALIGNED, at the content's own left")
    table = q.read((w("px_shseg") << 4) + W4TAB, 64)
    import pxsart                                           # noqa: E402
    even, odd = pxsart.win4_tables()
    want = bytes(even + odd)
    check(table == want, "(q2) the two 32 -> 16 tables are tools/pxsart.py's win4_tables(): "
          "0..15 themselves, a dark face its LIT colour on the even rows and black on the "
          "odd (wave 6's line dither - the C160 twin it replaced turned brown to red)")

    class G:                                # band_bytes' reader, over QEMU
        def word(self, n):
            return w(n)

        def byte(self, n):
            return b(n)
    band, size = band_bytes(q, G())
    check(max(max(r) for r in band) < 32,
          "(q2) every WIN4 shadow byte is 0..31 (max %d): px_blit_w4's xlat has no mask"
          % max(max(r) for r in band))
    ww, hh, px = dump(q)
    n, bad = colour_check(ww, px, bx, by, band, table, "(q2) the glass")
    check(size == BAND and n >= 6 and bad == 0,
          "(q2) the glass is the shadow through the table, pixel for pixel (%d colours)" % n)
    png("win4-qemu-play.png")
    png("win4-qemu-play-crop.png", crop=(bx - 8, by - 20, 528, 150), zoom=2)
    # --- (q3) a turn: PLANAR, one BLITP a strip, four a frame at most ------
    n0, p0, f0 = w("px_nb4"), w("px_nbp"), w("px_frames")
    keys("sendkey right 600")
    time.sleep(2.0)
    n1, p1, f1 = w("px_nb4"), w("px_nbp"), w("px_frames")
    print("   a turn: %d frame(s), %d OSAPI_GFX_BLITP and %d OSAPI_GFX_BLIT4 call(s)"
          % (f1 - f0, p1 - p0, n1 - n0))
    check(f1 > f0 and f1 - f0 <= p1 - p0 <= 4 * (f1 - f0) and n1 == n0,
          "(q3) an UNOBSCURED window presents PLANAR: between one and FOUR BLITPs "
          "a frame and no BLIT4 (97.14)")
    band, size = band_bytes(q, G())
    ww, hh, px = dump(q)
    n, bad = colour_check(ww, px, bx, by, band, table, "(q3) the planar glass")
    check(bad == 0 and n >= 4, "(q3) ...and its glass is the shadow through the table, "
          "pixel for pixel")
    png("win4-qemu-turn-crop.png", crop=(bx - 8, by - 20, 528, 150), zoom=2)
    time.sleep(1.0)
    # --- (q3b) the FALLBACK: a window with another over it is BLIT4's -------
    # Up 40 px, under the GAMES window's bottom edge, and GAMES raised by a
    # click on its title: OSAPI_WM_OBSCURED says covered, so the present
    # keeps the clip and takes BLIT4 (a window cannot be dragged off the
    # screen's side - the kernel holds it on - so the probe's own refusal
    # is not reachable from a single display; the two-display straddle is)
    GAMES_RIGHT = 425                       # the GAMES window's right edge (the
    x, y, ww_, wh_ = dispcp.win_rect(q, S, win)   # disk window open_named
    mouse("down", x + 200, y + 9)                 # left, photographed)
    mouse("to", x + 200, y + 9 - 40)
    mouse("up")
    mouse("click", 200, 88)                 # GAMES' title: raised over us
    mouse("to", 20, 30)
    time.sleep(2.5)
    x1, y1 = dispcp.win_rect(q, S, win)[:2]
    n0, p0, f0 = w("px_nb4"), w("px_nbp"), w("px_frames")
    writes = [((seg << 4) + s["px_force"], b"\x01"),   # a WHOLE frame: the
              ((seg << 4) + s["px_dirty"], b"\x01")]   # column memory wiped
    for name, fill in pxslib.FORCE_ALL:                 # (q5's), or the
        writes.append(((seg << 4) + s[name], bytes([fill]) * (2 * pxslib.COLMAX)))
    qpoke(q, writes)                                    # Δ-fill writes nothing
    time.sleep(2.0)
    n1, p1, f1 = w("px_nb4"), w("px_nbp"), w("px_frames")
    print("   covered at (%d,%d): %d frame(s), %d BLITP, %d BLIT4"
          % (x1, y1, f1 - f0, p1 - p0, n1 - n0))
    check(y1 < y and f1 > f0 and p1 == p0 and f1 - f0 <= n1 - n0 <= 4 * (f1 - f0),
          "(q3b) a COVERED window takes the BLIT4 fallback through the clip "
          "(no BLITP drawn)")
    band, size = band_bytes(q, G())
    ww, hh, px = dump(q)
    nbx = w("px_bx")
    c0 = (GAMES_RIGHT + 8 - nbx + 7) // 8   # the columns right of GAMES
    n, bad = colour_check(ww, px, nbx, w("px_by"), band, table,
                          "(q3b) the fallback's glass right of the cover", col0=c0)
    check(bad == 0 and n >= 2, "(q3b) ...and its uncovered glass is the table's too")
    png("win4-qemu-covered.png")
    tx = GAMES_RIGHT + 40                   # our title, right of GAMES:
    mouse("click", tx, y1 + 9)              # ours on top again...
    mouse("down", tx, y1 + 9)               # ...and back where it was
    mouse("to", tx, y1 + 9 + (y - y1))
    mouse("up")
    mouse("to", 630, 30)
    if b("px_pause"):                       # the sticky pause (px_focus_ck)
        keys("sendkey p")                   # lifted as a player lifts it
    time.sleep(2.5)
    if dispcp.win_rect(q, S, win)[:2] != (x, y):
        sys.exit("pxswin: the window did not come back to (%d,%d)" % (x, y))
    # --- (q4) a MOVE costs zero repaints ---------------------------------------
    f0, p0 = w("px_frames"), w("px_npaint")
    x, y, ww_, wh_ = dispcp.win_rect(q, S, win)
    mouse("down", x + 200, y + 9)           # the title bar, pressed...
    mouse("to", x + 200 - 40, y + 9 + 24)   # ...dragged...
    mouse("up")                             # ...and let go
    mouse("to", 630, 30)
    time.sleep(2.5)
    x1, y1 = dispcp.win_rect(q, S, win)[:2]
    f1, p1 = w("px_frames"), w("px_npaint")
    print("   moved (%d,%d) -> (%d,%d): %d frame(s) composed, %d W_PAINT(s)"
          % (x, y, x1, y1, f1 - f0, p1 - p0))
    check((x1, y1) != (x, y), "(q4) the window moved")
    check(f1 == f0 and p1 == p0, "(q4) A MOVE COSTS ZERO REPAINTS: no frame, no W_PAINT")
    band, size = band_bytes(q, G())
    ww, hh, px = dump(q)
    nbx, nby = bx + (x1 - x), by + (y1 - y)
    n, bad = colour_check(ww, px, nbx, nby, band, table, "(q4) the glass after the move")
    check(bad == 0 and n >= 4, "(q4) ...and the glass at the new place is still the shadow's")
    png("win4-qemu-moved.png")
    # --- (q5) the sprites: down the hall, the door opened, the guard beyond --
    # --- (q5) the sprites: scene C, poked (the gdb stub, qpoke) ---------------
    px_, py_, hd = pxslib.scene_at("c")     # the brick room's corner: the key,
    base = seg << 4                         # the barrel, the guard (97.10)
    cell = ((py_ >> 8) << 6) | (px_ >> 8)
    import pxssim                                           # noqa: E402
    writes = [(base + s["px_px"], px_.to_bytes(2, "little")),
              (base + s["px_py"], py_.to_bytes(2, "little")),
              (base + s["px_head"], hd.to_bytes(2, "little")),
              (base + s["px_hcos"], (pxssim.cos_q14(hd) & 0xFFFF).to_bytes(2, "little")),
              (base + s["px_hsin"], (pxssim.sin_q14(hd) & 0xFFFF).to_bytes(2, "little")),
              (base + s["px_pcell"], cell.to_bytes(2, "little")),
              (base + s["px_simoff"], b"\x01"),   # the guard holds its pose
              (base + s["px_force"], b"\x01")]
    for name, fill in pxslib.FORCE_ALL:
        writes.append((base + s[name], bytes([fill]) * (2 * pxslib.COLMAX)))
    qpoke(q, writes)
    time.sleep(2.0)
    nsc = b("px_nsc")
    x, y = dispcp.win_rect(q, S, win)[:2]
    print("   scene C: the eye at tile (%d,%d), %d sprite(s) drawn"
          % (w("px_px") >> 8, w("px_py") >> 8, nsc))
    check(nsc >= 2, "(q5) sprites in the colour window (scene C's key, barrel and guard)")
    band, size = band_bytes(q, G())
    hi = max(max(r) for r in band)
    check(hi < 32, "(q5) ...and with sprites and the weapon, every shadow byte is 0..31 "
          "(max %d)" % hi)
    png("win4-qemu-sprites-crop.png", crop=(x, y, 528, 150), zoom=2)
    q.quit()


# =============================================================================
# MartyPC: WIN1 on both adapters, the seam, the byte set
# =============================================================================
def disp_origin(m, n):
    """Display n's origin in the virtual desktop, off its vid_ctx record."""
    from os88geom import VID_CTX_SZ, VID_CTX_VX, VID_CTX_VY
    ctx = m.read(S("vid_ctx"), (n + 1) * VID_CTX_SZ)
    return (pxslib.u16(ctx, n * VID_CTX_SZ + VID_CTX_VX),
            pxslib.u16(ctx, n * VID_CTX_SZ + VID_CTX_VY))


def detail_menu_shot(m, mo, name):
    """Press on the bar's Detail title, hold it open, photograph, and let go
    over the bar (no item)."""
    x, y = 262, 9
    mo.to(x, y)
    mo._edge(True)
    os88marty.settle(m)
    m.pause()
    w, h, px = m.fbuf(0)
    os88marty.write_png_rgb(os.path.join(SHOTS, name), w, h, px)
    m.run()
    mo.to(x + 200, y, l=True)
    mo._edge(False)
    os88marty.settle(m)
    print("   shot", os.path.join("build/pxs-shots", name))


def drag_to(m, mo, g, x_new, y_new):
    """The title bar to put the window's frame at (x_new, y_new)."""
    x, y, ww, wh = dispcp.win_rect(m, S, g.win)
    gx, gy = x + ww // 2, y + 9
    mo.drag(gx, gy, gx + (x_new - x), gy + (y_new - y))
    mo.to(630, 30)                          # the arrow off the band: it is the
                                            # desktop's, drawn over the glass


def marty_leg(a):
    os.makedirs(SHOTS, exist_ok=True)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine, boot=False) as m:
        cards = m.cards()
        if len(cards) != 2:
            sys.exit("pxswin: %s is not a two-card machine" % a.machine)
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        if m.read(S("vid_ndisp"), 1)[0] != 2:
            sys.exit("pxswin: the Control Panel did not turn Extend on")
        g = pxslib.open_game(m)
        g.sim(False)
        g.god(True)
        g.pin(rung="flat", lowres=False, size=64)
        g.wait_frames(1)
        g.scene("a")
        g.wait_frames(1)
        os88marty.settle(m)
        # --- (m1) the 8086's window, and its greyed price ------------------------
        cap = pxslib.u16(g.bytes_("px_i_det", 14), 12)
        text = cstr(m.read(g.base + cap, 48))
        print("   tier %d, backend %s, Detail > Colour: %r"
              % (g.byte("px_tier"), pxslib.PXB.get(g.byte("px_back")), text))
        check(g.byte("px_back") == WIN1, "(m1) an 8086's window is WIN1")
        check(cap == g.s["px_s_colxt"] and text[:1] == b"\x01" and b"s a frame" in text,
              "(m1) ...and Colour is GREYED WITH ITS PRICE (SPEC.md 47)")
        names = menu_captions()
        long_ = []
        for name in names:
            t = cstr(m.read(g.base + g.s[name], 64)).lstrip(b"\x01")
            if len(t) > MENU_MAXCH:
                long_.append((name, t.decode("latin-1"), len(t)))
        print("   %d menu caption(s) counted%s" % (len(names),
              (": over %d: %s" % (MENU_MAXCH, long_)) if long_ else ""))
        check(len(names) >= 30 and not long_,
              "(m1) EVERY caption of the four menus fits a pull-down (%d glyphs)"
              % MENU_MAXCH)
        detail_menu_shot(m, mo, "wave5-xtvga-colour-greyed.png")
        # --- (m2) 8-aligned, and the pen path's bits ------------------------------
        def pen_leg(card, what):
            m.pause()
            if card == 0:                   # the VGA: what the card rasterised
                w, h, px = m.fbuf(card)
            else:                           # the Hercules: its framebuffer's
                w, h, rows = m.vram("herc") # BITS (the rasterisation of a
            band, size = band_bytes(m, g)   # second card is MartyPC's and not
            m.run()                         # faithful, tests/dispfsxcga.py)
            bx, by = g.word("px_bx"), g.word("px_by")
            if card == 0:
                mask = glass_mask(w, px, bx, by, size * 8, ROWS)
            else:                           # the second display's ORIGIN is its
                vx, vy = disp_origin(m, 1)  # record's, (640, 20) here - not 0
                mask = [[bool(v) for v in rows[by - vy + r][bx - vx:bx - vx + size * 8]]
                        for r in range(ROWS)]
            bits = bits_of(band)
            diff = sum(1 for r in range(ROWS) for c in range(size * 8)
                       if mask[r][c] != bits[r][c])
            lit = sum(sum(r) for r in bits)
            print("   %s: band at (%d,%d), %d lit bits, %d pixel(s) off them"
                  % (what, bx, by, lit, diff))
            return bx, band, diff, lit
        bx, band_vga, diff, lit = pen_leg(0, "the VGA (the pen)")
        check(bx % 8 == 0 and bx == g.word("px_cx"), "(m2) the band lands 8-aligned")
        check(diff == 0 and lit > 500, "(m2) the PEN path's glass is the shadow's bits")
        # --- (m3) a move on the VGA -------------------------------------------------
        f0, p0 = g.word("px_frames"), g.word("px_npaint")
        x, y = dispcp.win_rect(m, S, g.win)[:2]
        drag_to(m, mo, g, x - 40, y + 24)
        os88marty.settle(m)
        f1, p1 = g.word("px_frames"), g.word("px_npaint")
        x1, y1 = dispcp.win_rect(m, S, g.win)[:2]
        print("   moved (%d,%d) -> (%d,%d): %d frame(s), %d W_PAINT(s)"
              % (x, y, x1, y1, f1 - f0, p1 - p0))
        check((x1, y1) != (x, y) and f1 == f0 and p1 == p0,
              "(m3) A MOVE COSTS ZERO REPAINTS: no frame composed, no W_PAINT")
        # --- (m4) wholly onto the Hercules: the 1bpp path ---------------------------
        drag_to(m, mo, g, 640 + 64, 40)
        os88marty.settle(m, card=1)
        g.force()
        g.wait_frames(1)
        os88marty.settle(m, card=1)
        x1, y1 = dispcp.win_rect(m, S, g.win)[:2]
        print("   on the Hercules at (%d,%d), backend %s" % (x1, y1,
                                                           pxslib.PXB.get(g.byte("px_back"))))
        check(x1 >= 640 and g.byte("px_back") == WIN1, "(m4) dragged onto the Hercules: WIN1")
        bx, band_herc, diff, lit = pen_leg(1, "the Hercules (1bpp)")
        check(diff == 0 and lit > 500, "(m4) the 1bpp path's glass is the shadow's bits")
        check(band_herc == band_vga,
              "(m4) ...the SAME bits the pen path drew: the two paths are identical")
        cap = pxslib.u16(g.bytes_("px_i_det", 14), 12)
        print("   on the Hercules at tier %d: Detail > Colour is %r"
              % (g.byte("px_tier"), cstr(m.read(g.base + cap, 48))))
        check(cap == g.s["px_s_colmono"],
              "(m4) an 8086 ON A 1bpp DISPLAY greys Colour with the DISPLAY's fact "
              "('needs 16 colours'), not a price for a mode it cannot show (SPEC.md 47)")
        # --- (m5) the seam takes the right byte set ---------------------------------
        m.pause()
        g.poke_byte("px_tier", 1)          # CPU_286: the colour window's tier
        g.poke_byte("px_colour", 1)        # ...whose default On is decided at
                                           # entry, before the poke (97.14), so
                                           # it is set here as the entry would
        m.run()
        g.pin(rung="tex", lowres=True, size=64)
        g.wait_frames(1, limit=300.0)
        os88marty.settle(m, card=1)
        check(g.byte("px_back") == WIN1 and g.byte("px_btback") == INK_HERC,
              "(m5) a 286 on the Hercules is still WIN1, the Hercules byte set")
        drag_to(m, mo, g, 64, 40)           # back onto the VGA
        os88marty.settle(m)
        g.force()
        g.wait_frames(1, limit=300.0)
        os88marty.settle(m)
        back, ink, bt = g.byte("px_back"), g.word("px_inkf"), g.byte("px_btback")
        cap = pxslib.u16(g.bytes_("px_i_det", 14), 12)
        print("   on the VGA at a 286: backend %s, px_inkf %04x, byte set class %d"
              % (pxslib.PXB.get(back), ink, bt))
        check(back == WIN4 and ink == 0x0808 and bt == INK_MODEX,
              "(m5) dragged onto the VGA: WIN4, Mode X's inks and byte set")
        check(cap == g.s["px_s_colon"], "(m5) ...and Colour is live: On")
        m.pause()
        w, h, px = m.fbuf(0)
        os88marty.write_png_rgb(os.path.join(SHOTS, "wave5-xtvgaherc-win4.png"), w, h, px)
        band, size = band_bytes(m, g)
        table = m.read((g.word("px_shseg") << 4) + W4TAB, 64)
        m.run()
        bx, by = g.word("px_bx"), g.word("px_by")
        n, bad = colour_check(w, px, bx, by, band, table, "(m5) the VGA's glass")
        check(n >= 4 and bad == 0, "(m5) ...the glass is the shadow through the table")
        check(g.word("px_nbp") > 0, "(m5) ...wholly on the VGA and unobscured, it went "
              "out PLANAR (px_nbp %d, OSAPI_GFX_BLITP)" % g.word("px_nbp"))
        # --- (m5b) STRADDLING the seam: the RESTRICTIVE card's, so WIN1 ---------
        # SPEC.md 39.16.4.2: a straddling window's display is the restrictive
        # card's - the Hercules here - so the window is WIN1 and BLITP is never
        # asked. (BLITP's own straddle refusal is therefore not reachable on
        # the one two-card pair this tree hosts: a VGA beside a Hercules.)
        drag_to(m, mo, g, 200, 40)          # the band 200..711: 72 px over
        os88marty.settle(m)
        g.force()
        g.wait_frames(1, limit=300.0)
        os88marty.settle(m)
        n4, np_ = g.word("px_nb4"), g.word("px_nbp")
        g.force()
        g.wait_frames(1, limit=300.0)
        os88marty.settle(m)
        d4, dp = g.word("px_nb4") - n4, g.word("px_nbp") - np_
        bx = g.word("px_bx")
        print("   straddling at band x %d: display %d bpp, backend %s, %d BLIT4, %d BLITP "
              "on a forced frame" % (bx, g.byte("px_bpp"), pxslib.PXB.get(g.byte("px_back")),
                                     d4, dp))
        check(bx < 640 < bx + 512 and g.byte("px_bpp") == 1 and g.byte("px_back") == WIN1
              and d4 == 0 and dp == 0,
              "(m5b) a window STRADDLING the seam is the restrictive card's (SPEC.md "
              "39.16.4.2): WIN1, no BLIT4 and no BLITP")
        drag_to(m, mo, g, 64, 40)           # wholly on the VGA again
        os88marty.settle(m)
        g.force()
        g.wait_frames(1, limit=300.0)
        os88marty.settle(m)
        # --- (m6) Detail > Colour, the one control this wave adds ----------------
        import os88flush, os88ui                            # noqa: E401,E402
        ui = os88ui.UI(m, mouse=mo, verbose=False)

        def cfg_colour():
            try:
                cfg = os88flush.Flush(marty=m).volume(1).read("SYSTEM/APPDATA/PXSTEIN.CFG")
            except os88flush.FlushError as e:
                print("   (%s)" % e)
                return None
            return cfg[11] if len(cfg) == 12 and cfg[:4] == b"PXC\x02" else None
        for want_back, want_cap, want_cfg in ((WIN1, "px_s_coloff", 0),
                                              (WIN4, "px_s_colon", 1)):
            f0 = g.word("px_frames")
            ui.menu_pick("Detail", "Colour")
            mo.to(630, 30)
            g.wait_frames(1, limit=300.0, f0=f0)   # the pick's px_force: the
            os88marty.settle(m)                     # switch's whole frame
            back = g.byte("px_back")
            cap = pxslib.u16(g.bytes_("px_i_det", 14), 12)
            col = cfg_colour()
            print("   Detail > Colour picked: backend %s, caption %r, PXSTEIN.CFG colour %r"
                  % (pxslib.PXB.get(back), cstr(m.read(g.base + cap, 48)), col))
            check(back == want_back and cap == g.s[want_cap],
                  "(m6) Detail > Colour: the window is %s and the item says so"
                  % pxslib.PXB.get(want_back))
            check(col == want_cfg, "(m6) ...and PXSTEIN.CFG's colour byte is %d" % want_cfg)
            if want_back == WIN1:
                bx, band1, diff, lit = pen_leg(0, "the VGA after Colour Off (the pen)")
                check(diff == 0 and lit > 500,
                      "(m6) ...WIN1 on a 16-colour display: the PEN path's glass is "
                      "the shadow's bits")
            else:
                m.pause()
                w, h, px = m.fbuf(0)
                band, size = band_bytes(m, g)
                m.run()
                n, bad = colour_check(w, px, g.word("px_bx"), g.word("px_by"), band,
                                      table, "(m6) the VGA's glass, Colour On again")
                check(n >= 4 and bad == 0,
                      "(m6) ...and back to WIN4: the shadow through the table")
        drag_to(m, mo, g, 640 + 64, 40)     # and onto the Hercules again
        os88marty.settle(m, card=1)
        g.force()
        g.wait_frames(1, limit=300.0)
        back, ink, bt = g.byte("px_back"), g.word("px_inkf"), g.byte("px_btback")
        cap = pxslib.u16(g.bytes_("px_i_det", 14), 12)
        print("   on the Hercules again: backend %s, px_inkf %04x, byte set class %d"
              % (pxslib.PXB.get(back), ink, bt))
        check(back == WIN1 and ink == 0x2288 and bt == INK_HERC,
              "(m5) ...and onto the Hercules again: WIN1, the Hercules inks and set")
        check(cap == g.s["px_s_colmono"], "(m5) ...and Colour greyed: needs 16 colours")
        detail_menu_shot(m, mo, "wave5-xtvgaherc-colmono.png")


# =============================================================================
# --price: the 8086's number, measured
# =============================================================================
PRICE_RUNGS = (("Flat Full", "flat", False), ("Textured Low res", "tex", True),
               ("Textured Full", "tex", False))


def present_times(m, g, n):
    """n consecutive DRAWN turning frames, each split: the FRAME (px_frame_begin
    to px_frame_begin, as pxslib.frame_times), the PRESENT (px_render_win's
    .noblack to .blitted - the whole of px_blit_win) and, on WIN4, its two
    halves summed over the strips: the EXPAND (.strip to .xdone, or to
    .pxdone on the planar path) and the BLITS (.xdone to .bdone, the
    OSAPI_GFX_BLIT4 calls - or .pxdone to .pbdone, the OSAPI_GFX_BLITPs;
    `planar` counts the strips that went that way).
    Cycles, off MartyPC's counter; the first frame is dropped (it spans the
    poke)."""
    a = g.addr
    fb, nb, bl = a("px_frame_begin"), a("px_render_win.noblack"), a("px_render_win.blitted")
    st, xd, bd = a("px_blit_w4.strip"), a("px_blit_w4.xdone"), a("px_blit_w4.bdone")
    pxd, pbd = a("px_blit_w4.pxdone"), a("px_blit_w4.pbdone")
    m.bp_exec(fb, nb, bl, st, xd, bd, pxd, pbd)
    m.run()

    def stop():
        m.wait_stop(60)
        s_ = m.status()
        return ((s_.get("cs", 0) << 4) + s_.get("ip", 0)) & 0xFFFFF, s_["cycles"]
    at, c = stop()
    while at != fb:
        m.run()
        at, c = stop()
    out = []
    head = g.word("px_head")
    for i in range(n + 1):
        head = (head + pxslib.PX_TURN) & 0xFFF
        g.eye_poke(g.word("px_px"), g.word("px_py"), head)
        g.poke_byte("px_dirty", 1)
        c0, rec = c, dict(present=0, expand=0, blits=0, strips=0, planar=0)
        t_nb = t_st = t_xd = c
        while True:
            m.run()
            at, c = stop()
            if at == fb:
                break
            if at == nb:
                t_nb = c
            elif at == bl:
                rec["present"] += c - t_nb
            elif at == st:
                t_st = c
            elif at in (xd, pxd):
                rec["expand"] += c - t_st
                t_xd = c
            elif at in (bd, pbd):
                rec["blits"] += c - t_xd
                rec["strips"] += 1
                rec["planar"] += at == pbd
        rec["frame"] = c - c0
        if i:
            out.append(rec)
    m.bp_exec()
    m.run()
    return out


def price_leg(a):
    """The window's frame on an 8088 at every rung the window can be put on
    (Flat Full, the 8086's start rung - Textured Low res and Textured Full,
    one pick away), WIN1 and WIN4 each, the present bracketed apart from the
    rest. The greyed caption quotes the DEAREST WIN4 frame (SPEC.md 47: a
    fact about the ITEM, not about its cheapest rung - review, wave 5) and
    the row holds it within 15%."""
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        g = pxslib.open_game(m)
        g.sim(False)
        g.god(True)
        out = {}
        for rname, rung, low in PRICE_RUNGS:
            for label, tier in (("WIN1", 0), ("WIN4", 1)):
                m.pause()
                g.poke_byte("px_tier", tier)
                g.poke_byte("px_colour", 1)     # (an 8086 defaults it off)
                m.run()
                g.pin(rung=rung, lowres=low, size=64)
                g.wait_frames(1, limit=300.0)
                g.scene("a")
                g.wait_frames(1, limit=300.0)
                os88marty.settle(m)
                want = WIN1 if tier == 0 else WIN4
                if g.byte("px_back") != want or g.byte("px_rung") != pxslib.PXR[rung]:
                    sys.exit("pxswin: the window did not take %s %s (px_back %d, rung %d)"
                             % (label, rname, g.byte("px_back"), g.byte("px_rung")))
                recs = present_times(m, g, 8)
                med = {k: pxslib.median([pxslib.ms(r[k]) for r in recs])
                       for k in ("frame", "present", "expand", "blits")}
                fr = sorted(pxslib.ms(r["frame"]) for r in recs)
                out[(rname, label)] = med
                if label == "WIN4":
                    check(all(r["strips"] and r["planar"] == r["strips"] for r in recs),
                          "(p) %s: the unobscured window's every strip went out "
                          "PLANAR (OSAPI_GFX_BLITP, 97.14)" % rname)
                print("   %-16s %s: frame %.1f ms (%.1f..%.1f), present %.1f ms"
                      "%s, %d frames"
                      % (rname, label, med["frame"], fr[0], fr[-1], med["present"],
                         (" = expand %.1f + %d blit(s) %.1f (%d planar)"
                          % (med["expand"], recs[0]["strips"], med["blits"],
                             recs[0]["planar"]))
                         if label == "WIN4" else "", len(recs)))
        text = cstr(m.read(g.base + g.s["px_s_colxt"], 48)).decode("latin-1")
        said = float(text.split(": ", 1)[1].split(" s", 1)[0])
        dear = max(out[(r, "WIN4")]["frame"] for r, _, _ in PRICE_RUNGS) / 1000.0
        print("   the greyed caption says %.1f s; the dearest WIN4 frame is %.2f s"
              % (said, dear))
        check(abs(said - dear) <= 0.15 * dear,
              "(p) the 8086's price in the caption is the DEAREST measured WIN4 "
              "frame, within 15%")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qemu", action="store_true")
    ap.add_argument("--price", action="store_true")
    ap.add_argument("--machine", default=None)
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    a = ap.parse_args()
    os.chdir(ROOT)
    if a.qemu:
        qemu_leg(a)
    elif a.price:
        a.machine = a.machine or "os8088_xt_vga"
        price_leg(a)
    else:
        a.machine = a.machine or "os8088_xt_vga_herc"
        marty_leg(a)
    if FAIL:
        print("pxswin: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pxswin: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
