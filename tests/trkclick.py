#!/usr/bin/env python3
"""trkclick - a CLICKED button's action may repaint the face (SPEC.md 45.21.10)

    make build/trkship360.img && python3 tests/trkclick.py

The field report was a hard freeze on a 286: play BEVERLY.MOD, click XT Mode,
click the rate button twice (5.5 -> 11 -> 5.5 kHz), click Play - and the
machine stopped dead. Pressing X, R, R and Enter did NOT freeze, and that is
the whole defect: only a CLICK goes through tw_prefire/tw_synced.

tw_prefire parked the armed button in [tw_t2], the action ran, and tw_synced
read it back to mark the button as drawn. But tw_t2 is the face's SHARED
SCRATCH (serial, under the lock), and an XT-mode rate change repaints the face
(trk_rate_set -> tui_draw_all). At 11 kHz that reaches tw_voff, which parks
the 'No meters at 11 kHz' string POINTER in tw_t2 - so tw_synced used a
pointer as a button index and wrote two words ~34KB past tw_lastf/tw_lastl,
which is the package's own CODE (tw_rects, tw_bstate). The next thing to run
those bytes - Play, in the report - took an odd SP and a far return into the
BIOS ROM with the gfx lock held for ever. [tw_fired] is its own word now.

It follows the report's own path, because the path is what arms it: the
module PLAYING, XT mode switched on WITHOUT stopping first - which is what
leaves the meter pane owing the whole-pane redraw that reaches tw_voff at
11 kHz. Stop first, or turn the rate with the R key before clicking, and the
stray index never happens; the first version of this row did both and was
green with the defect in.

Three checks. The FIRST is the one that fails on the defect every time:

  1. the package's image bytes after the round trip 5.5 -> 11 -> 5.5 are the
     bytes before it. Playback is stopped by then, so the mixer is not
     patching its own immediates, and the captions and menu rows a rate
     change rewrites are back where they started - so ANY difference is a
     stray write, and no list of legitimate ones is needed. With the defect
     in it names four bytes, two words 0x20 apart (tw_lastf's and tw_lastl's
     stride)
  2. tw_synced marked the RATE button as drawn: tw_lastl[i] is its caption.
     The positive half of the fix; the next refresh re-syncs these words, so
     it passes with the defect too and is here to say the bookkeeping works
  3. Play opens a stream, the gfx lock comes free and the card keeps
     consuming - the report's SYMPTOM, which only fails when the stray words
     land on an instruction that runs before the check does. It did on the
     shipped build (an odd SP and a far return into the BIOS ROM, the gfx
     lock held for ever); a build whose bss layout moves by two bytes smashes
     a different instruction and plays on. That is why check 1 exists

WHY QEMU (docs/TESTING.md's list, entry 1: a 286 or better). An XT does not
get here: MartyPC's VGA XT runs the same clicks and every check passes with
the defect IN (its face is pre-armed into XT mode and never makes the
transition). `SB16=1 ADLIB=1` because the boot's card sniff is the OPL at
0x388, which QEMU's sb16 device does not carry - SB16 alone boots with no
SOUND.DRV and the module never plays.
"""
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.chdir(ROOT)
import dispcp                                                     # noqa: E402
import heapmap                                                    # noqa: E402
import os88fixture                                                # noqa: E402
import os88qemu                                                   # noqa: E402
import os88sym                                                    # noqa: E402
from os88rate import symbols, scan                                # noqa: E402

DISK = "build/trkship360.img"
SOCK = "build/qmp.sock"
# the B: zone and BEVERLY.MOD's row on a 640x480 desktop - tests/trkscrl.py's
# pair; BEVERLY.MOD sorts first on this disk as it does on that one. The
# BUTTON is not remembered: it is read out of the guest's own record
DISKB = (600, 112)
MODROW = (175, 128)
TW_NBTN = 16                        # trkwin.inc: 8 transport + 8 options

fails = []


def check(name, got, want):
    ok = got == want
    print("  %-52s %-12s %s" % (name, got, "ok" if ok else "FAIL, want %s" % (want,)))
    sys.stdout.flush()
    if not ok:
        fails.append(name)


def bufs(P):
    """The TRKBUF arrays' offsets. They are EQUs rather than labels (tracker.asm
    %macro TRKBUF), so the symbol map does not carry them: walk trkwin.inc's
    own declaration run from the TRKW/TRKB word just above the first of them,
    which the map does carry, adding each size as the macro does."""
    src = open(os.path.join(ROOT, "apps", "tracker", "trkwin.inc")).read()
    at, out, size = None, {}, {"TRKW": 2, "TRKB": 1}
    for line in src.splitlines():
        f = line.split(";")[0].split()
        if len(f) >= 2 and f[0] in size:
            name = f[1].rstrip(",")
            if "@" + name in P:
                at = P["@" + name] + size[f[0]]
            elif at is not None:        # declared, never referenced: the
                at += size[f[0]]        # map omits it, the layout does not
        elif len(f) >= 3 and f[0] == "TRKBUF" and at is not None:
            name = f[1].rstrip(",")
            out[name] = at
            try:
                at += eval(" ".join(f[2:]), {"TW_NBTN": TW_NBTN})
            except NameError:           # a size this walk cannot evaluate:
                at = None               # the next TRKW/TRKB re-anchors it
            if name == "tw_lastl":
                return out
        elif f:
            at = None
    raise SystemExit("trkclick: tw_lastl's declaration run not found")


def mouse(*args):
    subprocess.run([sys.executable, "tools/mouse.py", SOCK] + [str(a) for a in args],
                   check=True, stdout=subprocess.DEVNULL)


def boot():
    os88qemu.kill()
    # `make test` DAEMONISES the emulator; os88qemu owns the teardown
    os88qemu.own()
    r = os88fixture.make("test", "TESTAPPS=" + DISK, "SB16=1", "ADLIB=1")
    if r.returncode:
        raise SystemExit("trkclick: make test failed:\n" + r.stdout + r.stderr)


def main():
    os88fixture.need(DISK)
    P, _ = symbols(())
    B = bufs(P)
    S = os88sym.linear
    boot()
    q = heapmap.Qmp(SOCK)
    for _ in range(200):
        try:
            q.hmp("info status")
            break
        except OSError:
            time.sleep(0.1)

    def zone():
        try:
            return dispcp.drive_ordinal(q, S, "B") is not None
        except Exception:                                   # noqa: BLE001
            return False
    if not os88qemu.acted(q, zone, secs=90, what="drive B's zone", poll=0.4):
        raise SystemExit("trkclick: no desktop")
    os88qemu.pace(q, 1)
    mouse("to", *DISKB)
    # inside the double-click window either way: host spacing, trkscrl's rule
    q.hmp("mouse_button 1"); time.sleep(0.1); q.hmp("mouse_button 0")
    time.sleep(0.2)
    q.hmp("mouse_button 1"); time.sleep(0.1); q.hmp("mouse_button 0")
    if os88qemu.acted(q, lambda: bool(dispcp.win_list(q, S, check=False)),
                      secs=20, what="the Disk B window", poll=0.3):
        os88qemu.pace(q, 1)
    mouse("click", *MODROW)             # select, then Enter: trkscrl's reason
    os88qemu.pace(q, 1)
    q.hmp("sendkey ret")

    seg = [None]

    def resident():
        seg[0] = scan(q)[0]
        return bool(seg[0])
    os88qemu.acted(q, resident, secs=52, what="TRACKER.O88 resident", poll=1.0)
    if not seg[0]:
        raise SystemExit("trkclick: the player never became resident")
    base = seg[0] * 16
    b = lambda n: q.read(base + P["@" + n], 1)[0]
    wl = lambda off: int.from_bytes(q.read(base + off, 2), "little")
    os88qemu.acted(q, lambda: b("mp_loaded") == 1, secs=30,
                   what="[mp_loaded]", poll=0.25)
    os88qemu.quiesce(q, lambda: tuple(
        q.read(base + P["@" + n], 2) for n in
        ("mp_loaded", "mp_playing", "mp_mixrate", "mp_xt", "trk_fs")),
        secs=0.5, limit=4.0, what="Tracker's open to finish")

    def ui_idle():
        return (q.read(S("evq_count"), 1)[0] == 0
                and q.read(S("gfx_lock_flag"), 1)[0] == 0)

    def done(cond, what):
        """cond, then the UI idle across a tenth of a guest second"""
        ok = os88qemu.acted(q, lambda: cond() and ui_idle(), secs=6,
                            what=what, poll=0.05)
        os88qemu.pace(q, 0.1)
        return bool(ok and cond() and ui_idle())

    def key(k, cond, what):
        q.hmp("sendkey " + k)
        return done(cond, what)

    # the IMAGE, not the bss: the header's own `image` word (+8), which is
    # the unpacked size on either container (SPEC.md 20.13.5)
    img_len = wl(8)
    img = lambda: q.read(base, img_len)

    w = lambda n: wl(P["@" + n])

    print("1. playing, then XT mode on WITHOUT stopping (the report's path)")
    check("trk_cpu0 (not an XT: the face that fails)", b("trk_cpu0"), 0)
    check("mp_xt (a 286 opens with XT mode off)", b("mp_xt"), 0)
    playing = os88qemu.acted(q, lambda: b("trk_sopen") == 1 and b("mp_playing") == 1,
                             secs=10, what="the module playing", poll=0.2)
    check("the module is playing", bool(playing), True)
    os88qemu.pace(q, 1)                 # ...long enough for the meters to run
    key("x", lambda: b("mp_xt") == 1 and b("mp_playing") == 0, "X while playing")
    check("mp_xt after X", b("mp_xt"), 1)
    check("X stopped the music", b("mp_playing"), 0)
    check("trk_xhi (5.5 kHz)", b("trk_xhi"), 0)

    print("2. the rate BUTTON, clicked, to 11 kHz and back")
    xrn = (wl(P["trk_xrname"]), wl(P["trk_xrname"] + 2))
    idx = next((i for i in range(TW_NBTN)
                if wl(P["tw_labels"] + 2 * i) in xrn), None)
    if idx is None:
        raise SystemExit("trkclick: no button carries an XT rate caption - "
                         "the face this row needs is not on the screen")
    r = q.read(base + B["tw_rects_b"] + 8 * idx, 8)
    x1, y1, x2, y2 = (int.from_bytes(r[j:j + 2], "little") for j in (0, 2, 4, 6))
    pos = ((x1 + x2) // 2, (y1 + y2) // 2)
    print("    rate button %d at %s" % (idx, pos))
    img0 = img()
    for want in (1, 0):
        mouse("click", *pos)
        done(lambda: b("trk_xhi") == want, "the rate click")
        check("trk_xhi after a click", b("trk_xhi"), want)
        check("tw_lastl[rate] is its caption (tw_synced)",
              wl(B["tw_lastl"] + 2 * idx), wl(P["tw_labels"] + 2 * idx))
    img1 = img()
    bad = [i for i in range(img_len) if img0[i] != img1[i]]
    if bad:
        print("    image bytes the round trip left changed:",
              ", ".join("+%04x" % i for i in bad[:12]))
    check("image bytes changed by the round trip", len(bad), 0)

    print("3. ...and Play opens a stream that keeps playing")
    q.hmp("sendkey ret")
    opened = os88qemu.acted(q, lambda: b("trk_sopen") == 1, secs=6,
                            what="the stream open", poll=0.1)
    check("trk_sopen", bool(opened), True)
    a = w("trk_consumed")
    os88qemu.pace(q, 2)
    check("the card consumed in two guest seconds",
          (w("trk_consumed") - a) & 0xFFFF > 0, True)
    # the WORKER takes the lock for every frame it draws, so one sample can
    # land inside a frame: ask whether it comes free inside a guest second,
    # which a machine whose UI task never let go cannot answer
    freed = os88qemu.acted(q, lambda: q.read(S("gfx_lock_flag"), 1)[0] == 0,
                           secs=1, what="the gfx lock free", poll=0.02)
    check("the gfx lock comes free within a guest second", bool(freed), True)

    q.hmp("quit")
    print("\n%s" % ("FAILED: " + ", ".join(fails) if fails else "all checks passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
