#!/usr/bin/env python3
"""RECORDER, end to end on a Sound Blaster - SPEC.md 35.

    make && python3 tests/recorder.py [--o88 F --src F] [--break-sweep]

build/recorder.o88 goes onto a scratch 720KB floppy of its own (it ships on
no floppy, SPEC.md 35.1) and is opened from B: on MartyPC's Hercules 5150
with a Sound Blaster, where SOUND.DRV attaches at boot. Every step is
confirmed off the package's own variables, read through its window's W_SEG:

  1. THE BUTTONS FIRE. A click on DEMO, pressed and released over it, stages
     the sweep: [rc_len] = 8,000 and the status line says so. The press only
     ARMS (SPEC.md 13.7); the release is where the action is, and the release
     hit-tests the pointer against the button rects - so a release handler
     that lost the pointer's y to OSAPI_WM_CONTENT's DX never fires anything.
  2. THE SWEEP IS THE SWEEP, byte for byte. The demo is read back out of the
     grant by the speaker path (step 4), and all 8,000 samples must equal the
     model below: a 256-entry sine at 128 + round(88 sin), stepped by a phase
     accumulator whose increment is Bresenham-swept 400 -> 800 -> 400 Hz.
  3. PLAY ON THE CARD: Sound > Play opens the PCM_BG stream (PLAYING (SB)),
     and a repaint after it has run out retires it (PLAY DONE (SB)).
  4. PLAY ON THE SPEAKER: with PCM_BG taken out of the caps word the app
     queried at launch, Play takes the PCM_EXCL path - each 0.5 s chunk read
     back through verb 5 and handed to os88pcm_play. Every call's ES:SI/CX is
     read at its entry and must be the model's next 4,000 samples; the run
     ends PLAY DONE (SPEAKER); and the heap holds no claim of the package's
     but its region afterwards (the play buffer is a claim for the length of
     one play, SPEC.md 35.2).
  5. REC / STOP: Record opens the input stream (RECORDING...), Stop closes it.

Broken on purpose - `--break-sweep` flips one sample of the model, and step
2 FAILS; the base package (b13cfe60) FAILS step 1, its release handler
hit-testing (x, content top) after OSAPI_WM_CONTENT clobbered DX.
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M, os88ui, os88geom, heapmap     # noqa: E402
from cycweb import pkg_syms, Pkg, u16               # noqa: E402

MACHINE = "os8088_5150_herc_sb_720_gla"
IMG = "build/os8088-720.img"
SND_CAP_PCM_BG = 0x04
RC_IDLE, RC_REC, RC_PLAY = 0, 1, 2
RC_D_LEN = 8000


def sweep():
    """The demo take, as SPEC.md 35 describes it and not as the code does."""
    import math
    sine = [128 + int(math.floor(88 * math.sin(2 * math.pi * i / 256) + .5))
            for i in range(256)]
    out, ph, inc, err, step = [], 0, 3277, 0, 1
    for n in range(RC_D_LEN):
        if n == RC_D_LEN // 2:
            step = -step
        out.append(sine[ph >> 8])
        ph = (ph + inc) & 0xFFFF
        err += 6554 - 3277
        if err >= RC_D_LEN // 2:
            err -= RC_D_LEN // 2
            inc += step
    return bytes(out)


def fail(msg):
    print("FAIL " + msg, flush=True)
    sys.exit(1)


def build(tmp, o88):
    img = os.path.join(tmp, "rec720.img")
    dst = os.path.join(tmp, "RECORDER.O88")
    with open(o88, "rb") as f, open(dst, "wb") as g:
        g.write(f.read())
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", img, "--size",
                    "720", dst], check=True, capture_output=True)
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--o88", default="build/recorder.o88")
    ap.add_argument("--src", default="apps/recorder/recorder.asm")
    ap.add_argument("--break-sweep", action="store_true")
    ap.add_argument("--demo-by-menu", action="store_true",
                    help="stage the demo through Sound > Demo instead of the "
                         "button: how the old package (whose buttons never "
                         "fired) is driven through steps 2-5 for an A/B")
    a = ap.parse_args()
    want = bytearray(sweep())
    if a.break_sweep:
        want[4321] ^= 1
    syms, _ = pkg_syms(a.src)
    tmp = tempfile.mkdtemp(prefix="rectest-", dir=os.path.join(ROOT, "build"))
    img = build(tmp, a.o88)
    with os88ui.boot(IMG, apps=img, machine=MACHINE) as ui:
        m = ui.m
        ui.path("B:/RECORDER.O88")
        seg = None
        for w in os88geom.windows(m, ui.sym):
            if w.title == "Recorder":
                seg = u16(m.read(os88geom.winptr(m, w.i, ui.sym)
                                 + os88geom.W_SEG, 2))
                win = w
        if not seg:
            fail("no Recorder window")
        p = Pkg(m, seg, syms)

        def msg():
            return bytes(m.readseg(seg, p.rw("rc_msg"), 32)).split(b"\0")[0] \
                .decode("latin-1")

        def wait_msg(text, what, limit=20):
            M.until(m, lambda _: msg() == text, what, poll=.1, limit=limit)

        caps = p.rw("rc_caps")
        print("caps %04X, status %r" % (caps, msg()), flush=True)
        if not caps & SND_CAP_PCM_BG:
            fail("no PCM_BG at launch - SOUND.DRV did not attach the card")

        # --- 1. the buttons fire on the release -------------------------------
        # the window is up before its first W_PAINT has run, and the paint is
        # what stashes the origin and lays the button rects out
        M.until(m, lambda _: p.rw("rc_oy") and p.rw("rc_rects"),
                "the first paint", poll=.1, limit=20)
        ox, oy = p.rw("rc_ox"), p.rw("rc_oy")
        bx, by = ox + 166 + 26, oy + 100 + 8          # DEMO's centre
        if a.demo_by_menu:
            ui.menu_pick("Sound", "Demo")
        else:
            ui.mo.click(bx, by)
        try:
            M.until(m, lambda _: p.rw("rc_len") == RC_D_LEN, "DEMO staged",
                    poll=.1, limit=20)
        except M.MartyError:
            fail("1: a click on DEMO staged nothing (status %r, len %d)"
                 % (msg(), p.rw("rc_len")))
        wait_msg("DEMO STAGED - 400-800 HZ", "the demo's status")
        print("PASS 1: DEMO %s, %d bytes staged"
              % ("picked off the menu (no button tested)" if a.demo_by_menu
                 else "button fired on its release", p.rw("rc_len")),
              flush=True)

        # --- 3. play on the card ----------------------------------------------
        ui.menu_pick("Sound", "Play")
        if p.rb("rc_st") != RC_PLAY or msg() != "PLAYING (SB)":
            fail("3: Play did not open the stream (st %d, %r)"
                 % (p.rb("rc_st"), msg()))
        M.guest_sleep(m, 1.6)
        # the poll runs on a paint, a fired button or a menu command - and a
        # move is put back by the raise cache WITHOUT a paint (SPEC.md
        # 11.96). Taking the About card down is the one way to ask for a
        # poll that does nothing else: rc_abdismiss polls and redraws
        ui.menu_pick("Recorder", "About Recorder")
        M.until(m, lambda _: p.rb("rc_abon") == 1, "the About card",
                poll=.1, limit=10)
        ui.mo.click(ox + 100, oy + 30)
        M.until(m, lambda _: p.rb("rc_abon") == 0, "the card dismissed",
                poll=.1, limit=10)
        wait_msg("PLAY DONE (SB)", "the stream retired", limit=5)
        print("PASS 3: PCM_BG played and retired (About card up and down)",
              flush=True)

        # --- 4. play on the speaker, and 2. the sweep -------------------------
        p.ww("rc_caps", caps & ~SND_CAP_PCM_BG)
        pc = seg * 16 + syms["os88pcm_play"]
        got = bytearray()
        heap = {n: ui._S(n) for n in ("mem_base", "mem_top", "spl_live",
                                       "mem_tab")}
        mine = sorted((c.seg, c.para) for c in heapmap.Map(m, heap).claims
                      if c.own == seg)
        m.bp_exec(pc)
        # the pick itself returns into a callback that BLOCKS, so drive it by
        # hand: Sound > Play, and the stops arrive while the menu tracks
        try:
            ui.menu_pick("Sound", "Play", limit=4)
        except Exception:
            pass
        for k in range(2):
            m.wait_stop(limit=30)
            r = m.regs()
            if (r["cs"] << 4) + r["ip"] != pc:
                fail("4: stopped at %04X:%04X, not os88pcm_play"
                     % (r["cs"], r["ip"]))
            es, si, cx = r["es"], r["si"], r["cx"]
            held = [c for c in heapmap.Map(m, heap).claims
                    if c.own == seg and c.seg == es]
            if es == seg:
                print("note: chunk %d plays from the package's own segment"
                      % k, flush=True)
            elif not held or held[0].para * 16 < si + cx:
                fail("4: chunk %d plays from %04X:%04X, which is neither "
                     "the package nor a claim of its own that holds %d bytes"
                     % (k, es, si, cx))
            else:
                print("chunk %d: %d bytes at %04X:%04X, a %.1f KB claim of "
                      "the package's own" % (k, cx, es, si,
                                             held[0].para / 64.0), flush=True)
            got += bytes(m.readseg(es, si, cx))
            if k == 0:
                m.bp_exec(pc)
            else:
                m.breakpoints([])
            m.run()
        if len(got) != RC_D_LEN:
            fail("4: the speaker path played %d bytes, want %d"
                 % (len(got), RC_D_LEN))
        bad = [i for i in range(RC_D_LEN) if got[i] != want[i]]
        if bad:
            fail("2: %d of %d samples differ from the model, first at %d "
                 "(%d, want %d)" % (len(bad), RC_D_LEN, bad[0],
                                    got[bad[0]], want[bad[0]]))
        print("PASS 2: all %d samples are the 400-800-400 Hz sweep"
              % RC_D_LEN, flush=True)
        wait_msg("PLAY DONE (SPEAKER)", "the speaker play's end", limit=30)
        after = sorted((c.seg, c.para) for c in heapmap.Map(m, heap).claims
                       if c.own == seg)
        if after != mine:
            fail("4: the package's claims were %s before Play and %s after"
                 % (mine, after))
        print("PASS 4: PCM_EXCL played both chunks, claims %s unchanged"
              % mine, flush=True)
        p.ww("rc_caps", caps)

        # --- 5. rec / stop ----------------------------------------------------
        ui.menu_pick("Sound", "Record")
        if p.rb("rc_st") != RC_REC or msg() != "RECORDING...":
            fail("5: Record did not open the input (st %d, %r)"
                 % (p.rb("rc_st"), msg()))
        M.guest_sleep(m, .5)
        ui.menu_pick("Sound", "Stop")
        if p.rb("rc_st") != RC_IDLE:
            fail("5: Stop left state %d (%r)" % (p.rb("rc_st"), msg()))
        print("PASS 5: Record opened the input, Stop closed it (%r, %d "
              "bytes)" % (msg(), p.rw("rc_len")), flush=True)
    print("recorder: ok", flush=True)


if __name__ == "__main__":
    main()
