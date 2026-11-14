#!/usr/bin/env python3
"""A CLIP WITH ITS OWN SOUND, its sound WITH its picture (SPEC.md 126.3),
on MartyPC's XT with a CGA and an SB - IN LOCKSTEP (tests/vbtstep.py), so
every run is the same run.

    make vbattle && python3 tests/vbtownsnd.py

The owner: "The sound in the opening, and ending (post-victory) video is
~0.3-0.5s late. It plays fine, its just playing late ... the sound mixed
into clips is now right ... So the only clips late are ones set to 'their
own sound'." Two causes, both held here:

  * Such a clip's sound was booked at the frame's own time, which the sound
    ring - written the card's lead (0.37 s) ahead - had already passed: it
    started at once and was heard a lead after its picture (vb_abook).
  * When its sound began in the card ring's LAST 512 bytes, the chunk that
    started it was cut at the ring's wrap and then had its length
    clobbered by vb_asound (vb_crec's CL = 4): 516 bytes went in, four of
    them over the ring's control words - SND_EXT_TOTAL and the card's own
    count read 0x9090 - and the clock jumped a dozen frames on, the picture
    with it, then held while the sound caught up: ~5 frames ahead at the
    clip's start. One title-leaving keypress in twenty did that, and as
    this row then pressed it at a HOST-timed instant it went red one run in
    five, the same tree every time.

tests/vbtending.py's battle (--opening fade): the title faded into a first
opening with its own sound, and victory joining an end clip with its own
sound. While each plays, the record on the glass (its records less
[vb_cleft]) against the record HEARD (those its sound has fed, less what the
card has queued and not played - [vb_wpos] - [vb_cons32]): within two
frames (a whole frame on the glass against a sound mid-frame).

THE SWEEP: one boot to the title, a snapshot, and the Enter that fades it
pressed at a run of guest instants GAP apart, each from the snapshot - the
first opening's start looked at three times after each, until at least
LEGS_MIN have run AND one has started its sound in the ring's last block
(the case above; LEGS_MAX of them finding none is a failure of the row's
premise, said so). That leg goes on to the menu, Attack at the 1-HP enemy,
and the end clip, looked at eight times.

Broken on purpose: vb_chunk's CX not kept across vb_asound - the leg whose
sound starts at the ring's last block ~+5 frames, red; vb_abook booking at
[vb_due] as before - every leg ~+5, red.
"""
import os
import socket
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty            # noqa: E402
import os88vbt as B         # noqa: E402
import vbtending as E       # noqa: E402
import vbtstep as V         # noqa: E402
from cycweb import pkg_syms     # noqa: E402

VB_RL = 16384                   # the card's ring (vbattle.asm's VB_RL)
GAP = 1.0 / 30                  # between two legs' keypresses, guest s
LEGS_MIN = 6
LEGS_MAX = 48
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def looks(S, b, clip, tag, n):
    """The picture's record against the sound's, N looks 0.25 guest s apart
    from its third frame on"""
    S.until(lambda: S.rb("vb_cc") == clip and S.rw("vb_cfr") >= 3 and
            S.rw("vb_aring") != 0, tag + " under way", step=V.STEP // 10)
    out = []
    spf = b.spf
    nrec = b.clips[clip]["nrec"]
    for _ in range(n):
        if S.rb("vb_cc") != clip or S.rw("vb_aring") == 0:
            break
        # THE RECORD ON THE GLASS against THE RECORD HEARD: the sound's
        # written place (records fed, and into the one it is in) less what
        # the card has not played yet
        pic = nrec - S.rw("vb_cleft") - 1
        wrote = nrec - S.rw("vb_aleft") + S.rw("vb_afpos") / float(spf)
        q = (S.rd("vb_wpos") - S.rd("vb_cons32")) & 0xFFFFFFFF
        if q & 0x80000000:          # (signed: a card a moment past what
            q -= 1 << 32            # was written - an underrun)
        out.append(pic - (wrote - q / float(spf)))
        S.rest(0.25)
    return out


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        p = E.project(tmp, "joinfade", opening="fade")
        data = B.build_project(p)
        b = B.Bundle(data)
        names = [c["name"] for c in b.clips]
        intro = names.index("INTRO")
        outro = [i for i, n in enumerate(names) if n.endswith("OUTRO")][0]
        vbt = os.path.join(tmp, "BATTLE.VBT")
        open(vbt, "wb").write(data)
        vhd = V.disk(tmp, vbt)
        m = os88marty.launch(None, machine=V.MACHINE,
                             extra=["--mount", "hd:0:" + vhd], boot=False)
        legs, edge = [], None
        try:
            S = V.Step(m, syms)
            S.opened()
            S.to_title()
            S.rest(1.0)
            print("     the title, a second on: guest cycle %d" % S.cyc(),
                  flush=True)
            snap = m.snapshot()["id"]
            for i in range(LEGS_MAX):
                r = m.restore(snap, free_port())
                R = V.Step(r, syms)
                R.wrec = S.wrec
                keep = False
                try:
                    R.rest(i * GAP)
                    R.key("Enter")          # the title FADES to the opening
                    d = looks(R, b, intro, "the first opening", 3)
                    at = (R.rd("vb_apat") - R.rw("vb_rbase")) & (VB_RL - 1)
                    last = VB_RL - at <= 512
                    legs.append((i, at, d, R.rw("vb_nended")))
                    print("     Enter %.3f s on: its sound from ring byte "
                          "%5d%s, the picture against it %s frames"
                          % (i * GAP, at, " (the LAST block)" if last else "",
                             ["%+.1f" % x for x in d]), flush=True)
                    if last and edge is None:
                        edge, keep = (R, i), True
                finally:
                    if not keep:
                        r.quit()
                if edge is not None and len(legs) >= LEGS_MIN:
                    break
            bad = [(i, d) for i, _, d, _ in legs
                   if len(d) < 3 or max(abs(x) for x in d) > 2.0]
            ok(not bad,
               "the first opening (its own sound, after the title's fade), "
               "the Enter at %d guest instants %.3f s apart: the picture "
               "within two frames of its sound at its start every time%s"
               % (len(legs), GAP, "" if not bad else " - NOT at %s" % ", ".join(
                   "%.3f s %s" % (i * GAP, ["%+.1f" % x for x in d])
                   for i, d in bad)))
            ok(edge is not None,
               "a leg whose opening's sound began in the card ring's last "
               "block (where a chunk's length lost its cut at the wrap): %s"
               % ("Enter %.3f s on" % (edge[1] * GAP) if edge else
                  "NONE in %d legs - the sweep no longer reaches the case "
                  "it is for: widen LEGS_MAX or GAP" % len(legs)))
            if edge is not None:
                R = edge[0]
                R.until(lambda: R.rb("vb_state") == 1 and
                        R.rb("vb_dirty") == 0, "the menu", guest=240.0)
                R.key("Enter")              # Attack, at the 1-HP enemy
                d2 = looks(R, b, outro, "the end clip", 8)
                ok(d2 and max(abs(x) for x in d2) <= 2.0,
                   "the end clip (its own sound, victory joined to it): the "
                   "picture against its sound %s frames"
                   % ["%+.1f" % x for x in d2])
                R.key("Escape")
        finally:
            if edge is not None:
                edge[0].m.quit()
            m.quit()
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
