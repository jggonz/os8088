#!/usr/bin/env python3
"""Video Battle on KERN_MEGA, in a REAL-MODE bracket (SPEC.md 125.4.5,
docs/plans/VBATTLE-MEGA-PLAN.md W1).

    make && make mega && python3 tests/vbtmega.py [--arm play|refuse]

The SHIPPED engine on kern_mega, in Bochs (a 386, VGA, 16 MB), the system
disk kern_mega's and B: a floppy with VBATTLE.O88 and the CGA4 demo, its
enemy at 1 HP. Before this, the first Play was CLOSED by a #GP: vb_load
added paragraphs to the resident claim, an LDT selector there. Now the
claims are made at the door and everything that sums paragraphs runs
inside an FSXF_REAL bracket:

  play    1. the play's bracket is a REAL-MODE one - the kernel's own
             [fsx_rmode] seen 1 while it runs;
          2. the title, then Enter: the menu; Enter: Attack, victory and
             its end loop - the whole battle inside that one bracket;
          3. Enter: the play again, read inside it too (vb_lread) - and the
             title is then the host's decode in B8000, byte for byte;
          4. Esc: back on the desktop, the window's word 0 and the rings
             given back; and Enter again plays a SECOND time to the title,
             the resident claim's parts derived afresh in that bracket;
          5. no kern_mega FAULT and no CLOSED, anywhere.
  card    W2: the same play with Bochs's SB16 (FSXL_DRV DRVC_SOUND in the
          list, SOUND.DRV's low copy): the card's ring claimed at the door,
          the music never dry ([vb_munder]) and the card never found halted
          ([vb_nunder]), the card consuming seconds of the stream ([vb_cons32])
          - and Bochs's capture (OUT/sb.wav) is the battle's MUSIC: its best
          correlation with the bundle's music is at the bundle's own rate
          and stands clear of every other resampling. BROKEN ON PURPOSE
          (vb_mklist leaving DRVC_SOUND out) the play is SILENT - the card
          consumes nothing and the capture is empty - and this goes red.
  refuse  the kernel made to fall back: at OSAPI_FSX_RUN's call the list
          gains FSXL_DRV DRVC_NET, a class the real-mode bracket does not
          carry (FSXR_DRV) - so the bracket runs in PROTECTED mode: vb_main
          does nothing, [fsx_rmode] never 1, no fault, and the window says
          "Needs real mode: a driver".

BROKEN ON PURPOSE (verified when the row was written): vb_door's `and ah,
CPU_F_PM` made `and ah, 0`, the door blind to kern_mega - the first Play is
CLOSED by the #GP again and "the play" never happens. And before the fix it
names, each ring's END summed from the selector at the door (vb_rclaim's
.set) left 3 red: the first title exact, the play again - whose reads wrap
the ring - 192 bytes of garbage in B8000.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import shutil
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
os.environ["OS88_MEGA"] = "1"
import os88build    # noqa: E402
import os88geom as geom    # noqa: E402
import os88marty    # noqa: E402
import os88ui       # noqa: E402
import os88vbt as B         # noqa: E402
import vbtproj as P         # noqa: E402
from cycweb import pkg_syms     # noqa: E402
from vbtloops import screens_of     # noqa: E402

GS_MENU = 1
DRVC_NET = 4
FSXL_DRV = 2
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def run(arm, tmp, syms):
    vbp = os.path.join(tmp, "m.vbp")
    B.demo_project(vbp, bars=2)
    p = P.Project.load(vbp)
    p.set(("enemy", "hp"), 1)
    p.save()
    data = B.build_project(p)
    bd = B.Bundle(data)
    vbt = os.path.join(tmp, "BATTLE.VBT")
    open(vbt, "wb").write(data)
    disk = os.path.join(tmp, "vbt.img")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                    "--size", "1440", os88build.at("build/vbattle.o88"),
                    vbt], check=True, capture_output=True)
    machine = "os8088_xt_vga_sb" if arm == "card" else "os8088_xt_vga"
    with os88ui.boot("build/os8088.img", apps=disk, machine=machine) as ui:
        m = ui.m
        S = ui._S
        w = ui.path("B:/VBATTLE.O88")
        W[:] = [w]

        def sel():
            return u16(m.read(geom.winptr(m, w.i, S) + geom.W_SEG, 2))

        def rb(n):
            return m.readseg(sel(), syms[n], 1)[0]

        def rw(n):
            return u16(m.readseg(sel(), syms[n], 2))

        rmode = [0]

        def until(cond, what, guest=120.0):
            def c(_mm):
                if m.read(S("fsx_rmode"), 1)[0]:
                    rmode[0] = 1
                if m.box.fault():
                    raise SystemExit("vbtmega: %s while waiting for %s"
                                     % (m.box.fault(), what))
                return cond()
            os88marty.until(m, c, what, poll=0.2, limit=900.0, guest=guest)

        def key(k):
            m.key(k)
            until(lambda: True, "a key", guest=0.3)

        if arm == "refuse":
            # AT THE CALL: the list's end made FSXL_DRV DRVC_NET, a class
            # the real-mode bracket does not carry - the kernel's FSXR_DRV
            lin = m.lin(sel(), syms["vb_play.run"])
            m.bp_exec(lin)
            m.key("Enter")
            if not m.wait_stop(limit=120):
                raise SystemExit("vbtmega: never reached vb_play.run")
            fsxl = m.readseg(sel(), syms["vb_fsxl"], 24)
            end = fsxl.index(b"\0", 1) + 1          # past the file's NUL
            if fsxl[end] == FSXL_DRV:
                end += 2
            m.write(m.lin(sel(), syms["vb_fsxl"] + end),
                    bytes([FSXL_DRV, DRVC_NET, 0]))
            m.bp_exec()
            m.run()
            until(lambda: rb("vb_ready") == 0 and rw("vb_status") != 0
                  and rw("vb_status") == rw("vb_err"), "the window's word",
                  guest=60.0)
            os88marty.ui_done(m)
            txt = m.readseg(sel(), rw("vb_status"), 40).split(b"\0")[0]
            ok(rmode[0] == 0 and not m.box.faults(),
               "refuse: the bracket ran in PROTECTED mode ([fsx_rmode] "
               "never 1), nothing faulted")
            ok(txt.strip() == b"Needs real mode: a driver",
               "refuse: the window says %r" % txt.decode("latin-1"))
            return
        key("Enter")
        until(lambda: rb("vb_ready") == 1, "the play")
        if arm == "card":
            return card(m, S, rb, rw, until, key, bd, tmp)
        until(lambda: rb("vb_tplay") == 1, "the title")
        ok(rmode[0] == 1, "1. the play's bracket is REAL MODE "
           "([fsx_rmode] seen 1)")
        title = screens_of(bd, bd.title)

        def looks(n=12):
            hits, best = 0, 1 << 20
            for _ in range(n):
                b8 = bytes(m.read(0xB8000, 16384))
                hits += b8 in title
                best = min([best] + [sum(x != y for x, y in zip(t, b8))
                                     for t in title])
                until(lambda: True, "a look", guest=0.15)
            return hits, best
        h, d = looks()
        ok(h >= 6, "1. the first title is the host's decode in B8000 "
           "(%d of 12 looks exact, %d bytes off at best)" % (h, d))
        key("Enter")
        until(lambda: rb("vb_state") == GS_MENU, "the menu", 240)
        if os.environ.get("VBTMEGA_SHOT"):
            b8 = bytes(m.read(0xB8000, 16384))
            nz = sum(1 for i in range(0, 5) if b8[i] == 0xFF)
            print("   at the menu: B8000[0:5] %s" % b8[:5].hex(), flush=True)
        key("Enter")                    # Attack, at the 1-HP enemy
        until(lambda: rb("vb_eplay") == 1, "victory's end loop", 240)
        if os.environ.get("VBTMEGA_SHOT"):
            allf = [f for ci in range(len(bd.clips)) for f in
                    screens_of(bd, ci)]
            for k in range(6):
                b8 = bytes(m.read(0xB8000, 16384))
                print("   end loop look %d: %d bytes off the nearest frame"
                      % (k, min(sum(x != y for x, y in zip(f, b8))
                                for f in allf)), flush=True)
                until(lambda: True, "a look", guest=0.3)
        ok(rb("vb_over") == 1, "2. the battle to victory and its end "
           "loop, in the bracket")
        key("Enter")                    # play again: the title
        until(lambda: rb("vb_tplay") == 1 and rb("vb_over") == 0,
              "the play again, at the title", 240)
        if os.environ.get("VBTMEGA_SHOT"):
            from PIL import Image
            fw, fh, rgb = m.fbuf()
            Image.frombytes("RGB", (fw, fh), rgb).save(
                os.environ["VBTMEGA_SHOT"] + "-again.png")
            open(os.environ["VBTMEGA_SHOT"] + "-b8.bin", "wb").write(
                bytes(m.read(0xB8000, 16384)))
        title = screens_of(bd, bd.title)
        hits = 0
        for _ in range(12):
            hits += bytes(m.read(0xB8000, 16384)) in title
            until(lambda: True, "a look", guest=0.15)
        ok(hits >= 6, "3. the play again is read in the bracket, and the "
           "title is the host's decode in B8000 (%d of 12 looks exact)"
           % hits)
        key("Escape")
        until(lambda: rb("vb_ready") == 0, "Esc to the desktop", 60)
        os88marty.ui_done(m)
        ok(rw("vb_err") == 0 and rw("vb_rclm") == 0 and rw("vb_ring") == 0,
           "4. back on the desktop: no error, the rings and the card's "
           "ring given back")
        rmode[0] = 0
        key("Enter")
        until(lambda: rb("vb_ready") == 1 and rb("vb_tplay") == 1,
              "the second play's title")
        ok(rmode[0] == 1 and rw("vb_err") == 0,
           "4. a SECOND play reaches its title, real mode again")
        key("Escape")
        until(lambda: rb("vb_ready") == 0, "Esc again", 60)
        ok(not m.box.faults(), "5. no kern_mega FAULT or CLOSED")


def card(m, S, rb, rw, until, key, bd, tmp):
    """W2: the play through Bochs's SB16 - see the module note"""
    ring = rw("vb_ring")
    until(lambda: rb("vb_tplay") == 1, "the title")
    key("Enter")
    until(lambda: rb("vb_state") == GS_MENU, "the menu", 240)
    until(lambda: True, "the music", guest=8.0)
    key("Enter")                    # Attack: its own sound
    until(lambda: rb("vb_eplay") == 1, "victory's end loop", 240)
    def rd(n):
        return struct.unpack("<I", m.readseg(
            u16(m.read(geom.winptr(m, W[0].i, S) + geom.W_SEG, 2)),
            SYMS[n], 4))[0]
    st = dict(cons=rd("vb_cons32"), hper=rd("vb_hper"),
              ring=ring, under=rw("vb_munder"), nunder=rw("vb_nunder"),
              err=rw("vb_err"), reopen=rw("vb_sreopen") if "vb_sreopen"
              in SYMS else None)
    key("Escape")
    until(lambda: rb("vb_ready") == 0, "Esc to the desktop", 60)
    wav = os.path.join(m.box.out, "sb.wav")
    cap = open(wav, "rb").read() if os.path.exists(wav) else b""
    keep = os.environ.get("VBTMEGA_WAV")
    if keep:
        open(keep, "wb").write(cap)
    print("   card: %r, capture %d bytes" % (st, len(cap)), flush=True)
    ok(st["ring"] and st["err"] == 0 and st["under"] == 0
       and st["nunder"] == 0,
       "card: the card's ring claimed (%04X), the music never dry, the card "
       "never found halted" % st["ring"])
    ok(st["cons"] > 5 * bd.rate, "card: the card consumed %.1f s of the "
       "stream in the bracket" % (st["cons"] / float(bd.rate)))
    byrate = music_heard(cap, bd)
    best = max(byrate, key=byrate.get)
    ok(best == 4 and byrate[4] >= 0.55 and
       all(v < byrate[4] - 0.15 for k, v in byrate.items() if k != 4),
       "card: the capture is the battle's music - its best match %.2f at "
       "the bundle's own rate (44,100 / %d), %s elsewhere"
       % (byrate[4], bd.rate, ", ".join("%.2f" % v for k, v in
                                       sorted(byrate.items()) if k != 4)))


def music_heard(cap, bd):
    """{step: the best normalised correlation of a 0.36 s window of the
    bundle's music against Bochs's capture (44,100 Hz, 16-bit stereo; its
    first channel) resampled by STEP}. Bochs's own resampler softens a
    square-ish synth, so a clean copy scores ~0.6-0.7 here; the TEST is that
    the bundle's rate (step 4) stands clear of every other."""
    import numpy as np
    if b"data" not in cap or len(cap) < 200000:
        return {k: 0.0 for k in (2, 3, 3.5, 4, 4.5, 5, 6)}
    d = cap[cap.index(b"data") + 8:]
    a = np.frombuffer(d[:len(d) // 4 * 4], dtype="<i2").reshape(-1, 2)
    a = a[:, 0].astype(float)
    mus = np.frombuffer(bd.mus, dtype=np.uint8).astype(float) - 128
    win = mus[bd.rate:bd.rate + 4000]
    win = win - win.mean()
    n = len(win)
    out = {}
    for step in (2.0, 3.0, 3.5, 4.0, 4.5, 5.0, 6.0):
        c = np.interp(np.arange(0, len(a) - 1, step), np.arange(len(a)), a)
        c = c - c.mean()
        cc = np.correlate(c, win, "valid")
        cs = np.sqrt(np.convolve(c * c, np.ones(n), "valid"))
        out[int(step) if step == int(step) else step] = float(
            (cc / (cs * np.linalg.norm(win) + 1e-9)).max())
    return out


CARD = {}
W = []
SYMS = {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("play", "refuse", "card", "both"),
                    default="both")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    SYMS.update(syms)
    for arm in (("play", "refuse", "card") if a.arm == "both"
                else (a.arm,)):
        tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
        try:
            run(arm, tmp, syms)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print("vbtmega: %d FAILED" % len(fails))
        return 1
    print("vbtmega: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
