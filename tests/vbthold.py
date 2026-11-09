#!/usr/bin/env python3
"""The Video Battle's bundle HELD IN XMS (VBATTLE-MEMORY-PLAN 4 and 5,
SPEC.md 126.4.1), on QEMU.

    make && python3 tests/vbthold.py [--arm whole|copy|part|order|nox]

WHY QEMU: docs/TESTING.md's closed list, entry 1 - XMS is a 286-and-up
store and every MartyPC machine is an 8088. Nothing here is timed: every
wait is on the guest's own state.

THE INSTRUMENT IS A DISK SWAP, as tests/vidxms.py's is. The demo battle
(its enemy at 1 HP) on B:, the engine on B:, QEMU's own 128 MB:

  whole: THE CACHE over the whole file (SPEC.md 126.4.6, `[vb_cfix]`):
         in place from the open, the idle loop in the block, and every clip
         filled into its own place while the title goes round - each slot
         byte for byte its clip; then B: is changed to a BLANK floppy.
         Enter: the fade, the openings, the menu, an Attack, victory and
         its end loop, Enter and the title again - nothing read off the
         disk, which has nothing to find. The rings are read IN PLACE
         through a 386's loaded base, the decode yielding (the step poked
         to 256 bytes), and the title again is the host's decode in B8000.
  copy:  the decode in place gated off ([vb_noinp]): the copying hold of
         126.4.1, the whole bundle one range, M2's, still plays - every
         read after the swap to a blank floppy the hold's.
  part:  the pool held to the idle loop and 100 KB (`[vb_hcap]`, and the
         slots' least poked down, `[vb_cmin]`): the cache in SLOTS - the
         idle loop pinned, clips taken FIFO and EVICTED to make room for
         the next, a clip longer than the room (the Limit) never given
         one - every slot whole at the end byte for byte its clip, and
         the play to victory and again.
  order: part, gated off in place: the copying hold takes the bundle's own
         HOLD ORDER, fills to its last, and the play runs with the disk
         serving the rest - copies AND disk reads both counted.
  nox:   `-m 1`, no pool: no hold, and the play the disk's as before.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build, os88geom as geom, os88qemu, os88ui    # noqa: E402
import os88vbt as B         # noqa: E402
import vbtproj as P         # noqa: E402
import xmcheck              # noqa: E402
from cycweb import pkg_syms     # noqa: E402
from vidxms import Q        # noqa: E402
from pxswin import qpoke       # noqa: E402
from vbtloops import screens_of     # noqa: E402

GS_MENU = 1
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("whole", "copy", "part", "order",
                                      "nox"),
                    default="whole")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    pkg = os88build.at("build/vbattle.o88")
    sysimg0 = os88build.at("build/os8088.img")
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        vbp = os.path.join(tmp, "h.vbp")
        B.demo_project(vbp, bars=2)
        p = P.Project.load(vbp)
        p.set(("enemy", "hp"), 1)
        p.save()
        data = B.build_project(p)
        vbt = os.path.join(tmp, "BATTLE.VBT")
        open(vbt, "wb").write(data)
        bd = B.Bundle(data)
        disk = os.path.join(tmp, "vbt.img")
        blank = os.path.join(tmp, "blank.img")
        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(sysimg0, sysimg)
        for out, files in ((disk, [pkg, vbt]), (blank, [])):
            subprocess.run([sys.executable, "tools/os88disk.py", "-o", out,
                            "--size", "1440"] + files, check=True,
                           capture_output=True)
        q = Q(tmp, sysimg, disk, 1 if a.arm == "nox" else 128)
        xmcheck.wait_desktop(q.sock, "vbthold")
        S = xmcheck.sym
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
        mo = os88qemu.Mouse(q, S, q.hmp, after=0)

        def wait(cond, what, secs=60):
            if not os88qemu.acted(q, cond, secs=secs, what=what, poll=0.05):
                raise SystemExit("vbthold: timed out waiting for %s" % what)
        x, y = geom.drive_pt(q, "B", S)
        mo.dblclick(x, y)
        box = {}

        def diskwin():
            for w in ui.windows():
                if w.visible and ui.fs_of(w) == (1, 0, 1):
                    box["w"] = w
                    return True
            return False
        wait(diskwin, "drive B's window", 30)
        os88qemu.ui_done(q, S, cap=1.0)
        w = box["w"]
        i, _ = ui.entry("VBATTLE.O88", w)
        before = set(o.i for o in ui.windows())
        mo.dblclick(*ui.row_xy(w, i - ui.scroll(w)))

        def engine():
            for o in ui.windows():
                if o.i not in before and o.visible:
                    box["p"] = o
                    return True
            return False
        wait(engine, "the battle's window", 30)
        os88qemu.ui_done(q, S, cap=1.0)
        rec = q.read(S("wm_wins") + box["p"].i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = u16(rec, geom.W_SEG) << 4

        def rb(n, off=0):
            return q.read(base + syms[n] + off, 1)[0]

        def rw(n, off=0):
            return u16(q.read(base + syms[n] + off, 2))
        whole = a.arm in ("whole", "copy")
        cache = a.arm in ("whole", "part")
        if cache:               # the yield step down to 256 record bytes:
            qpoke(q, [(base + syms["vb_xyb"], struct.pack("<H", 256))])
        if a.arm in ("copy", "order"):  # the A/B gate: the copying hold
            qpoke(q, [(base + syms["vb_noinp"], b"\x01")])
        if a.arm == "order":    # the pool held under the bundle: the
            cap = len(data) // 4096 * 6 // 10   # gate's cap, in 4 KBs
            qpoke(q, [(base + syms["vb_hcap"], struct.pack("<H", cap))])
        if a.arm == "part":     # the idle loop and 100 KB of slots
            cap = (len(bd.idle_r) + 4095) // 4096 + 25
            qpoke(q, [(base + syms["vb_hcap"], struct.pack("<H", cap)),
                      (base + syms["vb_cmin"], struct.pack("<H", 8))])
        q.hmp("sendkey ret")
        wait(lambda: rb("vb_ready") == 1, "the play to start", 60)
        hon = rb("vb_hon")
        if os.environ.get("VBTH_DBG"):
            print("   hon %d hrn %d hkb4 %d hpool %d hcap %d hold %s tab %s" % (
                hon, rb("vb_hrn"), rw("vb_hkb4"), rw("vb_hpool"), rw("vb_hcap"),
                bd.hold[:4], q.read(base + syms["vb_htab"], 24).hex()))
        if a.arm == "nox":
            ok(hon == 0, "nox: no pool, no hold")
        else:
            ok(hon == 1 and rb("vb_hrn") >= 1,
               "%s: the hold is taken (%d range(s), %d KB)"
               % (a.arm, rb("vb_hrn"), rw("vb_hkb4") * 4))
            ok(rb("vb_cmode") == int(cache),
               "%s: the clip CACHE %s" % (a.arm, "on, in place from the "
                                          "open" if cache else "off"))
            if cache:
                ok(rb("vb_hinp") == 1 and rb("vb_iinp") == 1,
                   "%s: the hold in place, the idle loop in its block"
                   % a.arm)
                ok(rb("vb_cfix") == int(a.arm == "whole"),
                   "%s: %s" % (a.arm, "the whole file: every slot its own "
                               "place" if a.arm == "whole" else
                               "SLOTS, the pool under the bundle"))
            elif whole:
                ok(rb("vb_hrn") == 1 and rw("vb_hkb4") * 4096 >= len(data),
                   "copy: ONE range, the whole bundle (%d KB of %d)"
                   % (rw("vb_hkb4") * 4, len(data) // 1024))
            wait(lambda: rb("vb_tplay") == 1, "the title", 60)
            wait(lambda: rb("vb_hfull") == 1, "the hold's ranges to fill, "
                 "while the title goes round", 240)
            ok(True, "%s: the hold's ranges filled while the title went "
               "round" % a.arm)
            tok = struct.unpack_from("<I", q.read(base + syms["vb_htok"], 4))[0]
            nclips = len(bd.clips)

            def slots():
                def tab(n):
                    return struct.unpack_from("<%dH" % nclips, q.read(
                        base + syms[n], 2 * nclips))
                return tab("vb_cx"), tab("vb_cl4"), tab("vb_chave")

            def slot_bytes(what):
                cx, cl, ch = slots()
                good = bad = 0
                for k, c in enumerate(bd.clips):
                    if cx[k] == 0xFFFF or ch[k] < cl[k]:
                        continue
                    got = q.read(tok + cx[k] * 4096, c["len"])
                    if got == data[c["off"]:c["off"] + c["len"]]:
                        good += 1
                    else:
                        bad += 1
                ok(good > 0 and bad == 0, "%s: %s - %d slots whole, each "
                   "byte for byte its clip (%d not)" % (a.arm, what, good, bad))
            if a.arm == "whole":
                def allin():
                    cx, cl, ch = slots()
                    return all(cx[k] != 0xFFFF and ch[k] >= cl[k]
                               for k in range(nclips))
                wait(allin, "every clip into the cache", 240)
                ok(True, "whole: every clip filled into its place while "
                   "the title went round")
                slot_bytes("the cache")
            if cache:
                ix = struct.unpack_from("<I", q.read(base + syms["vb_ixb"], 4))[0]
                ht = struct.unpack_from("<HHHH", q.read(base + syms["vb_htab"], 8))
                ok(ht[1] > 0 and ht[3] >= ht[1] and q.read(tok + ix, ht[1] * 4096)
                   == data[ht[0] * 4096:(ht[0] + ht[1]) * 4096],
                   "%s: the idle loop in its block, byte for byte (%d KB)"
                   % (a.arm, ht[1] * 4))
            if a.arm == "copy":
                got = q.read(tok, len(data))
                ok(got == data, "copy: the hold is the bundle, byte for byte")
            c0 = rw("vb_hcopies")
            r0 = rw("vb_rcalls")
            inp = rb("vb_hinp")
            if a.arm in ("copy", "order"):
                ok(inp == 0, "%s: not in place (the gate)" % a.arm)
            i0, y0 = rw("vb_icalls"), rw("vb_iyc")
            if whole:
                q.hmp("change floppy1 %s raw" % blank)
                print("   B: is blank now")
            if a.arm == "order":
                ok(rb("vb_hrn") >= 2 and rw("vb_hkb4") * 4096 < len(data),
                   "order: the pool under the bundle - the HOLD ORDER's %d "
                   "ranges, %d KB of %d" % (rb("vb_hrn"), rw("vb_hkb4") * 4,
                                            len(data) // 1024))
                first = struct.unpack_from("<HH", q.read(base + syms["vb_htab"], 4))
                ok(first == tuple(bd.hold[0]),
                   "order: ...its first range the order's first")
        q.hmp("sendkey ret")                 # the fade, then the openings
        wait(lambda: rb("vb_state") == GS_MENU, "the menu", 240)
        if cache:
            # THE IDLE LOOP in the cache's block, decoded where it lies
            # (126.4.6): the canvas above the band is one of its frames as
            # the host decodes them
            rows = bd.band[1]

            def canvas(buf):
                return b"".join(bytes(buf[(y >> 1) * 80 + (y & 1) * 8192:]
                                      [:80]) for y in range(rows))
            B.verify(data, quiet=True)      # (its idle loop's screens: from
            frames = set(canvas(x) for x in B.LAST_IDLE.values())  # the
                                            # openings' end, as the engine)
            hits, seen = 0, set()
            for _ in range(16):
                q.hmp("stop")
                cv = canvas(q.read(0xB8000, 16384))
                q.hmp("cont")
                if cv in frames:
                    hits += 1
                    seen.add(cv)
                time.sleep(0.15)
            if os.environ.get("VBTH_DBG"):
                q.hmp("stop")
                open(os.path.join(os.environ.get("VBTH_DIR", "."),
                                  "glass-%s.bin" % a.arm), "wb").write(
                    q.read(0xB8000, 16384))
                open(os.path.join(os.environ.get("VBTH_DIR", "."),
                                  "frames-%s.bin" % a.arm), "wb").write(
                    b"".join(sorted(frames)))
                q.hmp("cont")
            ok(hits >= 8 and len(seen) >= 3, "%s: the IDLE LOOP read in "
               "place from the block is the host's decode on the glass, and "
               "it goes round (%d of 16 looks exact, %d frames)"
               % (a.arm, hits, len(seen)))
        q.hmp("sendkey ret")                 # Attack, at the 1-HP enemy
        wait(lambda: rb("vb_eplay") == 1, "victory's end loop", 240)
        ok(rb("vb_over") == 1 and rb("vb_quit") == 0,
           "%s: the battle played to victory and its end loop" % a.arm)
        q.hmp("sendkey ret")                 # play again: the title
        wait(lambda: rb("vb_tplay") == 1 and rb("vb_over") == 0,
             "the play again, at the title", 240)
        ok(rb("vb_quit") == 0, "%s: ...and again, from the title" % a.arm)
        if cache:
            # THE BYTES: the title again, its ring given it in place, its
            # every record read through a loaded base and decoded across
            # yields - on the glass as the host decodes it
            title = screens_of(bd, bd.title)
            hits, seen = 0, set()
            for _ in range(16):
                q.hmp("stop")
                g = q.read(0xB8000, 16384)
                q.hmp("cont")
                if g in title:
                    hits += 1
                    seen.add(g)
                time.sleep(0.15)
            ok(hits >= 8 and len(seen) >= 3, a.arm + ": the title read IN "
               "PLACE is the host's decode on the glass, going round (%d of "
               "16 looks exact, %d frames)" % (hits, len(seen)))
        if a.arm != "nox":
            n = rw("vb_hcopies") - c0
            if os.environ.get("VBTH_DBG"):
                t = syms["vd_ytab"]
                sites = [u16(q.read(base + t + 2 * k, 2)) for k in range(4)]
                print("   xif %d xact %d tier %d ylim %04x kind %d sites %s" % (
                    rb("vb_xif"), rb("vb_xact"), rb("vb_tier"),
                    rw("vd_ylim"), rb("vb_kind"),
                    [q.read(base + x, 2).hex() for x in sites]))
            ni = (rw("vb_icalls") - i0) & 0xFFFF
            ny = (rw("vb_iyc") - y0) & 0xFFFF
            nr = (rw("vb_rcalls") - r0) & 0xFFFF
            if cache:
                ok(ni > 0 and ny > 0 and rw("vb_irings") >= 3,
                   "%s: records read IN PLACE: %d, the decode yielding %d "
                   "times, %d rings in place, %d copies, %d disk reads"
                   % (a.arm, ni, ny, rw("vb_irings"), n, nr))
                if a.arm == "whole":
                    ok(nr == 0, "whole: nothing read off the (blank) disk "
                       "from the swap on")
                else:
                    slot_bytes("after the play")
                    ok(rw("vb_cev") > 0, "part: slots EVICTED to make room "
                       "(%d)" % rw("vb_cev"))
            else:
                ok(n > 0 and ni == 0,
                   "%s: reads the hold answered after it filled: %d copies"
                   % (a.arm, n))
        q.hmp("sendkey esc")
        wait(lambda: rb("vb_ready") == 0, "Esc to leave", 60)
        q.close()
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
