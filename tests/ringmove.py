#!/usr/bin/env python3
"""The audio packages' long-held claims MOVE between plays and are PINNED for
one (NO-PINNING A6's remainder: SPEC.md 86.5.3, 45.25.6, 105.12.1, 114.11.1).

    make && python3 tests/ringmove.py --app audio      # Sound Blaster
    python3 tests/ringmove.py --app audiospk           # no card: the speaker
    python3 tests/ringmove.py --app tracker            # no card: the speaker
    python3 tests/ringmove.py --app midirack           # SB wavetable
    python3 tests/ringmove.py --app wirefleet          # SB samples

THE OWNER'S RULE, as it reads for a ring: a claim may stay pinned only while a
device is ARMED on it (D) or the program is using it RIGHT NOW (C). Between
plays a ring is neither, so each of these is declared movable while idle and
pinned from the moment a play starts until it stops - never per buffer, never
in the play path (the relocation procs run only inside a compaction). This row
asserts BOTH halves on the machine, through the package's own words:

  1. IDLE, after a play has made and stopped: every named claim is declared
     movable (MC_RLOC read from OUTSIDE, mem_tab - a refused declaration looks
     identical from inside, SPEC.md 66.5.6.2);
  2. a compaction posted from outside at the TRIVIAL rank ([mem_cpq_lvl] =
     0FCh + [mem_cpq], the door OSAPI_MEM_COMPACT posts through) MOVES at
     least one of them - or the run proved nothing, and says so;
  3. every word names a live claim afterwards (the relocation proc ran);
  4. every moved claim's bytes are the same, hashed at both addresses;
  5. it PLAYS afterwards - the package's own playing state, and a counter the
     device or the producer advances;
  6. WHILE it plays, each ring the device or the ISR reads is PINNED
     (MC_RLOC = 0), so a compaction mid-play cannot move a DMA target or the
     speaker ISR's ring;
  7. stopped again, declared again.

TWO RED RUNS each registration names: a relocation proc made a bare `ret`
(the word names the old base: 3 fails), and the play-time pin dropped (the
ring is movable mid-play: 6 fails).

WIREFLEET has no way to play a clip without a link partner, so the clip is
INJECTED: the row stops the guest at WireFleet's own timer callback (the UI
task, DS its segment, the gfx lock held - where the game's own launch cue is
played from) and calls wf_fxsound with AL = WF_SND_LAUNCH, the routine the
game calls, then lets the machine run on.
"""
import argparse
import hashlib
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import dispapps                                          # noqa: E402
import megafix                                           # noqa: E402
import os88geom as geom                                  # noqa: E402
import os88marty as M                                    # noqa: E402
import os88ui                                            # noqa: E402

MC_SIZE, MEM_MAX = geom.MC_SIZE, geom.MEM_MAX
REGS = ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'sp', 'ss', 'ds', 'es',
        'flags')

# the package's own constants, mirrored for reading its state from outside
AP_ST_STOP, AP_ST_PLAY = 0, 1
APS_RL = 16384                          # apps/audio/apspk.inc
TSP_RL = 16384                          # apps/tracker/trkspk.inc
MR_OUT_WT, MRO_WT, MR_ST_PLAY = 6, 6, 1  # apps/midirack
WF_SND_LAUNCH = 0                       # apps/wirefleet/wfsound.inc

APPS = {
    "audio": dict(
        machine="os8088_5150_herc_sb_720_gla", sys="os8088-720.img",
        disk="covoxlisten720.img", open="B:/MEDIA/FURELISE.WAV",
        src="audio", words=["ap_la_seg"], pinned=["ap_la_seg"]),
    "audiospk": dict(
        machine="os8088_5150_herc_720_gla", sys="os8088-720.img",
        disk="covoxlisten720.img", open="B:/MEDIA/FURELISE.WAV",
        src="audio", words=["ap_la_seg", "aps_rseg"],
        pinned=["ap_la_seg", "aps_rseg"]),
    "tracker": dict(
        machine="os8088_5150_herc_720_gla", sys="os8088-720.img",
        disk="covoxlisten720.img", open="B:/MEDIA/BEVERLY.MOD",
        src="tracker", words=["tsp_rseg"], pinned=["tsp_rseg"]),
    "midirack": dict(
        machine="os8088_5150_herc_sb_720_gla", sys="os8088-720.img",
        disk="mrwt720.img", open="B:/APPS/MIDIRACK.O88",
        src="midirack", words=["mr_fseg", "mwt_bseg", "mwt_xseg"],
        pinned=["mwt_bseg", "mwt_xseg"], free=["mr_fseg"]),
    "wirefleet": dict(
        machine="os8088_5150_herc_sb_gla", sys="os8088-360.img",
        disk="games360.img", open="B:/WIREFLET.O88",
        src="wirefleet", words=["wf_pcmseg"], pinned=["wf_pcmseg"],
        # the ring is a TOP-DOWN claim (OSAPI_MEM_CLAIM_DMA_HI), made under
        # the region, so only the descending pass can move it, and only into
        # a hole above: Mines' region is that hole once Mines closes, and the
        # link search (whose serial ring pins the region, 114.9) is turned
        # off so the region above the ring may move up first
        first="B:/MINES.O88", menu=("Link", "Disconnect")),
}


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


class Run(object):
    def __init__(self, ui, cfg, win):
        self.ui, self.m, self.cfg, self.win = ui, ui.m, cfg, win
        self.S = ui.m.sym
        self.P = dispapps._map(cfg["src"])
        self.pinsnap = None             # a play too short to read from
                                        # outside leaves its own reading here
        self.mega = getattr(self.m, "bochs", False)

    def para(self, v):
        """A segment word as mem_tab keys its claim: on kern_mega (KERN-MEGA-
        MEMORY-PLAN MC7.4) an LDT selector, rebased by a move and never
        changed, so "it moved" is asked of the paragraph under it"""
        if self.mega and v and 0x100 <= v < 0xA000 and v & 4:
            b = self.m.selector(v)
            return b[0] >> 4 if b else v
        return v

    def lin(self, seg, off=0):
        """SEG:OFF linear: a selector's base on kern_mega"""
        return self.m.lin(seg, off) if self.mega else seg * 16 + off

    # --- the package, read through ITS segment, re-read each time: the region
    # is movable too (OS88_REGION_MOVABLE), and a stale segment decodes a live
    # machine as a corrupt one ------------------------------------------------
    def seg(self):
        p = geom.winptr(self.m, self.win, self.S)
        return u16(self.m.read(p + geom.W_SEG, 2))

    def addr(self, name):
        return self.lin(self.seg(), self.P[name])

    def b(self, name):
        return self.m.read(self.addr(name), 1)[0]

    def w(self, name):
        return u16(self.m.read(self.addr(name), 2))

    def put(self, name, raw):
        self.m.write(self.addr(name), bytes(raw))

    def claims(self):
        raw = self.m.read(self.S("mem_tab"), MEM_MAX * MC_SIZE)
        return {u16(raw, i * MC_SIZE): (u16(raw, i * MC_SIZE + 2),
                                        u16(raw, i * MC_SIZE + 8))
                for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)}

    def words(self):
        return {n: self.para(self.w(n)) for n in self.cfg["words"]}

    def rloc(self, name):
        c = self.claims().get(self.para(self.w(name)))
        return None if c is None else c[1]

    def heapmap(self):
        raw = self.m.read(self.S("mem_tab"), MEM_MAX * MC_SIZE)
        for i in sorted(range(MEM_MAX), key=lambda i: u16(raw, i * MC_SIZE)):
            b = u16(raw, i * MC_SIZE)
            if b:
                print("     %04x %3dKB own %04x %s"
                      % (b, u16(raw, i * MC_SIZE + 2) // 64,
                         u16(raw, i * MC_SIZE + 4),
                         "MOVABLE" if u16(raw, i * MC_SIZE + 8) else ""))

    def quiet(self):
        M.quiesce(self.m, lambda: sorted(self.claims().items()), guest=2.0,
                  budget=120.0, what="the claims")

    def key(self, k):
        self.m.key(k)

    def until(self, cond, what, limit=60.0):
        M.until(self.m, lambda _: cond(), what, poll=0.2, limit=limit)

    # --- the injected call (tests/n1942.py's Game.call, with the package's
    # own CS): stop at `at` (flat), push the return there, run `routine` -----
    def inject(self, at, routine, then=None, **regs):
        m = self.m
        m.bp_exec(at)
        if m.wait_stop(60) != "breakpoint":
            raise RuntimeError("the guest never reached %05x" % at)
        saved = m.regs()
        sp = (saved["sp"] - 2) & 0xFFFF
        lin = (saved["ss"] << 4) + sp
        r = m.batch([dict(cmd="read", addr=lin, len=2),
                     dict(cmd="write", addr=lin,
                          data=struct.pack("<H", saved["ip"]).hex()),
                     dict(cmd="park", cs=saved["cs"], ip=self.P[routine])]
                    + [dict(cmd="setreg", reg=k, value=saved[k])
                       for k in REGS]
                    + [dict(cmd="setreg", reg="sp", value=sp)]
                    + [dict(cmd="setreg", reg=k, value=v)
                       for k, v in regs.items()]
                    + [dict(cmd="bp", list=[{"type": "exec", "addr":
                            (saved["cs"] << 4) + saved["ip"]}]),
                       dict(cmd="run")])
        old = bytes.fromhex(r[0]["data"])
        if m.wait_stop(30) != "breakpoint":
            raise RuntimeError("%s did not return" % routine)
        m.batch([dict(cmd="write", addr=lin, data=old.hex())]
                + [dict(cmd="setreg", reg=k, value=saved[k]) for k in REGS])
        if then:                        # still stopped: what the call did,
            then()                      # read before a tick can undo it
        m.bp_exec()
        m.go()


# =============================================================================
# per package: make the rings exist and stop; play; stop
# =============================================================================
def audio_ready(r):
    r.until(lambda: r.b("ap_state") == AP_ST_PLAY and r.w("ap_la_seg"),
            "Audio to start playing")


def audio_stop(r):
    r.key("KeyS")
    r.until(lambda: r.b("ap_state") == AP_ST_STOP, "Audio to stop")


def audio_play(r):
    r.key("Space")
    r.until(lambda: r.b("ap_state") == AP_ST_PLAY, "Audio to play again")


def audio_playing(r):
    p0 = r.w("ap_played")
    M.guest_sleep(r.m, 1.5)
    p1 = r.w("ap_played")
    return p1 != p0, "ap_played %d -> %d" % (p0, p1)


def spk_cons(r, segname, rl):
    b = r.m.read(r.lin(r.w(segname), rl + 2), 2)
    return u16(b)


def audiospk_ready(r):
    r.until(lambda: r.b("aps_on") and r.w("aps_rseg"),
            "the speaker's play to start", limit=120)


def door_open(r, segname, rl):
    # the ring is FILLED before the door opens (two seconds of cushion), so
    # CONS is 0 until the ISR has played a sample: wait for it, then time it
    try:
        r.until(lambda: spk_cons(r, segname, rl), "the speaker's door to "
                "open (CONS moving)", limit=60)
    except Exception:
        pass                            # ...and the check below says NO


def audiospk_playing(r):
    door_open(r, "aps_rseg", APS_RL)
    c0 = spk_cons(r, "aps_rseg", APS_RL)
    M.guest_sleep(r.m, 1.0)
    c1 = spk_cons(r, "aps_rseg", APS_RL)
    return r.b("aps_on") and c1 != c0, "aps_on %d, CONS %d -> %d" % (
        r.b("aps_on"), c0, c1)


def audiospk_play(r):
    r.key("Space")
    audiospk_ready(r)


def tracker_ready(r):
    r.until(lambda: r.b("tsp_run") and r.w("tsp_rseg"),
            "the speaker's play to start (the first one calibrates)",
            limit=180)


def tracker_stop(r):
    r.key("Escape")
    r.until(lambda: not r.b("tsp_run"), "Tracker's play to stop")


def tracker_play(r):
    r.key("Enter")
    tracker_ready(r)


def tracker_playing(r):
    door_open(r, "tsp_rseg", TSP_RL)
    c0 = spk_cons(r, "tsp_rseg", TSP_RL)
    M.guest_sleep(r.m, 1.0)
    c1 = spk_cons(r, "tsp_rseg", TSP_RL)
    return r.b("tsp_run") and c1 != c0, "tsp_run %d, CONS %d -> %d" % (
        r.b("tsp_run"), c0, c1)


def midirack_ready(r):
    r.until(lambda: r.b("mr_loaded") == 1, "MIDIRack's autoload", limit=120)
    r.put("mr_want", bytes([MR_OUT_WT]))
    midirack_play(r)


def midirack_play(r):
    r.ui.menu_pick("Play", "Play")
    r.until(lambda: r.b("mr_out") == MRO_WT and r.b("mr_state") == MR_ST_PLAY
            and r.w("mwt_bseg") and r.w("mwt_xseg"),
            "the wavetable to play", limit=60)


def midirack_stop(r):
    r.ui.menu_pick("Play", "Stop")
    r.until(lambda: r.b("mr_out") == 0, "MIDIRack to stop")


def mr_cons(r):
    if r.b("mrb_grant"):
        return r.w("mrb_gcons")
    return u16(r.m.read(r.lin(r.w("mrb_seg"), 16384 + 2), 2))


def midirack_playing(r):
    c0 = mr_cons(r)
    M.guest_sleep(r.m, 1.5)
    c1 = mr_cons(r)
    return r.b("mr_state") == MR_ST_PLAY and c1 != c0, \
        "mr_state %d, the card's CONS %d -> %d" % (r.b("mr_state"), c0, c1)


def wf_ready(r):
    r.until(lambda: r.w("wf_pcmseg"), "WireFleet's samples claim (needs "
            "the Sound Blaster tier)", limit=60)


def wf_play(r):
    # a clip is half a second: read it the instant wf_fxsound returns, with
    # the guest still stopped, and let it play out after
    def snap():
        r.wf_active = r.b("wf_pcmactive")
        r.pinsnap = {n: r.rloc(n) for n in r.cfg["pinned"]}
    r.inject(r.addr("wf_tick"), "wf_fxsound", then=snap, ax=WF_SND_LAUNCH)


def wf_playing(r):
    wf_stop(r)                          # played out: CONS is how far the
    cons = u16(r.m.read(r.lin(r.w("wf_pcmseg"), 4096 + 2), 2))  # card got
    return r.wf_active and cons >= 4096, "wf_pcmactive %d at the call, the " \
        "card's CONS %d at the clip's end" % (r.wf_active, cons)


def wf_stop(r):
    r.until(lambda: not r.b("wf_pcmactive"), "the clip to end (wf_pcm_tick)",
            limit=20)


FLOWS = {
    "audio": (audio_ready, audio_stop, audio_play, audio_playing),
    "audiospk": (audiospk_ready, audio_stop, audiospk_play, audiospk_playing),
    "tracker": (tracker_ready, tracker_stop, tracker_play, tracker_playing),
    "midirack": (midirack_ready, midirack_stop, midirack_play,
                 midirack_playing),
    "wirefleet": (wf_ready, wf_stop, wf_play, wf_playing),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True, choices=sorted(APPS))
    a = ap.parse_args()
    cfg = APPS[a.app]
    ready, stop, play, playing = FLOWS[a.app]
    fails, done = [], []

    def check(n, ok, what, detail=""):
        done.append(n)
        print("  %s %-34s %s%s" % (n, what, "yes" if ok else "NO",
                                   ("  (" + detail + ")") if detail else ""),
              flush=True)
        if not ok:
            fails.append("%s %s%s" % (n, what,
                                      (": " + detail) if detail else ""))

    # bare names, build/'s: launch() resolves them against the run's tree
    with os88ui.boot(os.path.join("build", cfg["sys"]),
                     apps=megafix.arm_disk(os.path.join("build",
                                                        cfg["disk"])),
                     machine=cfg["machine"], quick=True) as ui:
        first = ui.path(cfg["first"]) if cfg.get("first") else None
        w = ui.path(cfg["open"])
        r = Run(ui, cfg, w.i)
        if first is not None:
            ui.close(first)
        if cfg.get("menu"):
            ui.menu_pick(*cfg["menu"])

        # --- a play made the rings, and pinned the ones a device reads ------
        ready(r)
        if a.app == "wirefleet":            # the claim is made at the window's
            r.quiet()                       # open; one clip plays and ends
            play(r)
        pin0 = r.pinsnap or {n: r.rloc(n) for n in cfg["pinned"]}
        r.pinsnap = None
        check("0", all(v == 0 for v in pin0.values()),
              "pinned while the first play runs",
              ", ".join("%s rloc %s" % kv for kv in pin0.items()))
        stop(r)
        r.quiet()
        M.ui_done(r.m)

        # --- 1: idle, declared ----------------------------------------------
        w0 = r.words()
        held = r.claims()
        print("%s at %04x: %s" % (a.app, r.seg(), "  ".join(
            "%s=%04x" % kv for kv in w0.items())), flush=True)
        hashes = {}
        for n, c in w0.items():
            if c in held:
                hashes[n] = hashlib.md5(r.m.read(c * 16,
                                                 held[c][0] * 16)).digest()
        decl = [n for n, c in w0.items() if c in held and held[c][1]]
        check("1", len(decl) == len(w0), "declared movable while idle",
              "%d of %d" % (len(decl), len(w0)))

        # --- 2..4: a compaction posted from outside -------------------------
        wptr = geom.winptr(r.m, r.win, r.S)
        r.m.write(r.S("mem_cpq_lvl"), bytes([0xFC]))
        r.m.write(r.S("mem_cpq"), bytes([wptr & 0xFF, wptr >> 8]))
        r.until(lambda: u16(r.m.read(r.S("mem_cpq"), 2)) == 0,
                "the posted compaction", limit=30)
        r.quiet()
        w1 = r.words()
        held = r.claims()
        print("%s at %04x: %s" % (a.app, r.seg(), "  ".join(
            "%s=%04x" % kv for kv in w1.items())), flush=True)
        moved = [n for n in w1 if w1[n] != w0[n]]
        stale = [n for n, c in w1.items() if c not in held]
        check("2", bool(moved or stale), "moved",
              ", ".join("%s %04x -> %04x" % (n, w0[n], w1[n]) for n in moved)
              or ("the claim under %s moved and the word did NOT follow"
                  % stale if stale else "NOTHING - the run proves nothing"))
        if not moved and not stale:
            r.heapmap()
        check("3", not stale, "every word names a live claim",
              "stale: %s" % stale if stale else "")
        bad = [n for n in moved if n in hashes and w1[n] in held and
               hashlib.md5(r.m.read(w1[n] * 16, held[w1[n]][0] * 16))
               .digest() != hashes[n]]
        check("4", not bad, "contents survived the move",
              "changed: %s" % bad if bad else "")

        # --- 5..7: it plays, pinned; stopped, declared ----------------------
        if not stale:
            play(r)
            pin = r.pinsnap or {n: r.rloc(n) for n in cfg["pinned"]}
            ok, why = playing(r)
            check("5", ok, "plays after the move", why)
            check("6", all(v == 0 for v in pin.values()),
                  "pinned while it plays",
                  ", ".join("%s rloc %s" % kv for kv in pin.items()))
            if cfg.get("free"):         # ...and what nothing holds a copy
                free = {n: r.rloc(n) for n in cfg["free"]}   # of: movable
                check("6b", all(free.values()), "still movable while it plays",
                      ", ".join("%s rloc %s" % kv for kv in free.items()))
            stop(r)
            r.quiet()
            back = {n: r.rloc(n) for n in cfg["words"]}
            check("7", all(back.values()), "declared again once stopped",
                  ", ".join("%s rloc %s" % kv for kv in back.items()))

    print("ringmove %s: %s" % (a.app, "FAIL: " + "; ".join(fails) if fails
                               else "%d checks passed" % len(done)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
