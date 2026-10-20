#!/usr/bin/env python3
"""A DOCUMENT ON A DISK THAT IS NOT ITS PROGRAM'S (SPEC.md 54.3.3, 98.4.7).

    make && python3 tests/assocvol.py

Reported from the field: an installed machine, VIDEO.O88 in C:\\APPS, the
.V88s on F: - a freshly formatted partition with no ASSOC.DAT of its own.
They showed no icon, a double-click started VIDEO.O88 and it said it could
not read the disk, and the reason was behind a closed info card. File>Open
played the same file.

The same shape on two floppies - the 720KB system disk (VIDEO.O88 in
A:\\APPS, its ASSOC.DAT naming it) and a bare B: with OS8088.V88 and a junk
BAD.V88 - and three legs:

  ICON  B: listed BEFORE the machine knows .V88, then A:\\APPS browsed (which
        teaches it), then B: raised: its content must be what a Refresh
        draws, and not what it drew before. A document's glyph is decided at
        its window's mount, so a window listed first kept the bare mark; and
        the repair that re-resolves it left the RAISE CACHE's old picture on
        the glass.
  READ  a double-click on B:\\OS8088.V88 opens a player that is READY - the
        document read. VIDEO.O88 stood in its document's folder with
        OSAPI_FILE_GOTO_Q, which moves the machine and not the instance, so
        its first file call put it back in A:\\APPS and the read missed.
  CARD  a double-click on B:\\BAD.V88 says why with the info card OUT.

Broken on purpose: GOTO_QM back to GOTO_Q, READ fails with vp_s_io; the
wm_su_stale after the repair taken out, ICON fails; the card forced only for
a LOADED file, CARD fails.
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M, os88ui, os88build, os88geom as geom   # noqa: E402
from cycweb import pkg_syms                                    # noqa: E402

MACHINE = "os8088_5150_herc_720_gla"
REFRESH = (282, 24)         # the Disk window's Refresh button, off its frame


def main():
    os.chdir(ROOT)
    bad = []
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    tmp = os.path.join(ROOT, "build", "assocvol-%d" % os.getpid())
    os.makedirs(tmp)
    junk = os.path.join(tmp, "BAD.V88")
    with open(junk, "wb") as f:
        f.write(b"not a video" * 20)
    disk = os.path.join(tmp, "b.img")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                    "--size", "720", "apps/video/os8088.v88", junk],
                   check=True, capture_output=True)
    try:
        with os88ui.boot(os88build.at("build/os8088-720.img"), apps=disk,
                         machine=MACHINE) as ui:
            m = ui.m

            def content(w):
                w = ui._as_win(w.i)
                ui.mo.to(700, 12)
                M.guest_sleep(m, 1.0)
                _, _, rows = m.vram()
                return [bytes(r[w.x + 2:w.x + w.w - 2])
                        for r in rows[w.y + 20:w.y + w.h - 2]]

            # --- ICON
            b = ui.open_drive("B")
            before = content(b)
            ui.path("A:/APPS")
            ui.raise_window(b)
            raised = content(b)
            bw = ui._as_win(b.i)
            ui.mo.click(bw.x + REFRESH[0], bw.y + REFRESH[1])
            M.guest_sleep(m, 2.0)
            fresh = content(b)
            d_fr = sum(1 for p, q in zip(raised, fresh) if p != q)
            d_bf = sum(1 for p, q in zip(before, fresh) if p != q)
            print("   ICON: rows differing, raised vs Refresh %d, before vs "
                  "Refresh %d" % (d_fr, d_bf))
            if d_fr:
                bad.append("ICON: a raise drew %d rows other than Refresh "
                           "does - the documents' glyphs stayed bare" % d_fr)
            if not d_bf:
                bad.append("ICON: the listing before .V88 was known drew "
                           "the same as after - the leg proved nothing")

            def player(name):
                ui.raise_window(b)
                ui.open(name, expect=None)
                M.until(m, lambda mm: len([t for t in ui.titles()
                                           if "Video" in t]) > len(players),
                        "%s to open a player" % name, poll=0.3, limit=300.0,
                        guest=120.0)
                w = [w for w in ui.windows() if "Video" in w.title and
                     w.i not in players][0]
                players.append(w.i)
                rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                             geom.WIN_SIZE)
                base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4
                rb = lambda n: m.read(base + syms[n], 1)[0]
                rw = lambda n: struct.unpack(
                    "<H", bytes(m.read(base + syms[n], 2)))[0]
                M.until(m, lambda mm: rw("vp_msg") != syms["vp_s_none"],
                        "%s's status" % name, poll=0.3, limit=120.0,
                        guest=60.0)
                M.guest_sleep(m, 1.0)
                msg = rw("vp_msg")
                mname = next((k for k, v in syms.items() if v == msg and
                              k.startswith("vp_s_")), hex(msg))
                print("   %s: %s, playable %d, card %d"
                      % (name, mname, rb("vp_ok"), rb("vp_card")))
                return mname, rb("vp_ok"), rb("vp_card")
            players = []
            # --- READ
            msg, ok, card = player("OS8088.V88")
            if (msg, ok) != ("vp_s_ready", 1):
                bad.append("READ: B:\\OS8088.V88 opened as %s" % msg)
            # --- CARD
            msg, ok, card = player("BAD.V88")
            if msg != "vp_s_notv88" or card != 1:
                bad.append("CARD: B:\\BAD.V88 said %s with the card %s"
                           % (msg, "out" if card else "CLOSED"))
    finally:
        for f in os.listdir(tmp):
            os.remove(os.path.join(tmp, f))
        os.rmdir(tmp)
    for x in bad:
        print("   FAIL: %s" % x)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
