#!/usr/bin/env python3
"""SOUND.DRV's image MOVES under a loaded RAD tune, and the tune follows it
(SPEC.md 34.12.8, 96.8).

    make && make radgate && python3 tests/radmove.py            # all arms
    python3 tests/radmove.py rtc rtc-nostamp                    # QEMU only
    python3 tests/radmove.py tick tick-nocallseg tick-nostamp   # MartyPC only

WHY IT EXISTS. §66.6.3 lets the compactor move a driver image whenever no
frame stands in it and no driver worker exists - and a playing RAD tune has
neither, because it is interrupt-driven. The kernel patches every word IT
holds (drv_tab, the int 70h vector, the claim's MC_OWN) and tells the driver
nothing, so wave 3's claim, which copied the resident's segment once at verb 4
(RO_RSEG) and again into its far pointers at arm, would leave IRQ8 exiting
through freed memory up to 1,024 times a second. The fix re-stamps RO_RSEG
with the resident's CS at every entry into the claim - rad_callseg (a verb),
snd_tickp (the tick class) and rad_i70 (the RTC class) - and the claim loads
its far pointers' segment from it at each jump. Each stamp carries a local
label `.stamp`, so this row can take any of them out again at run time.

THE ARENA IS BUILT, and it has to be: loaded at boot the image sits at the
ceiling with nothing above it to slide into. So, tests/sndmove.py's shape
with RADGATE's own claims as the placeholders (RADGATE `c`/`f`/`k`):

  1. RADGATE opens (its region, its 49KB buffer), then the Control Panel.
     RADGATE's verb 6 poll is held off (`rt_nopoll`) across the unmount - see
     the note at the end.
  2. SOUND.DRV unmounted. Every free run above the largest is claimed whole
     (F), so the remount cannot go back up there; then X, the tune claim's
     size, then P1, the sound driver's claims plus 2KB.
  3. SOUND.DRV mounted again: it lands directly UNDER P1.
  4. X freed, the tune loaded into that hole - ABOVE P1, so the pinned tune
     claim walls nothing - and P1 freed:

        [ tune ][ P1 hole ][ SOUND.DRV image (+ ring) ][ the big run ]

  5. The forcing ask, RADGATE `k`: OSAPI_MEM_CLAIM_HI of exactly the P1 hole
     plus the big run, in KB. Larger than any free run, so only §66.4.1's
     descending pass can answer it - by sliding the image (and the ring) up
     by P1. P1 is larger than the image, so where the image WAS lies wholly
     inside the block the ask then gets, and RADGATE fills that block with
     FAh F4h (cli, hlt): a stale far jump into the old image stops the
     machine rather than running a copy that happens to still be intact.

THE ARMS. Each is one boot.

  rtc            QEMU ADLIB=1 (RTC class), RV1.RAD PLAYING across the move.
                 The image moved and the int 70h vector followed; the claim's
                 RO_RSEG names the new segment; frames still advance 50 a
                 second and IRQ8s are still counted; stop gives register B
                 and the vector back (RADS_UNHOOK, a far call to the NEW
                 segment); START hooks again (RADS_HOOK) and plays; all-off
                 frees the claim.
  rtc-nostamp    the same boot with rad_i70's stamp NOPed in the image before
                 the move (four bytes, through QEMU's gdb stub). The verb 6
                 poll still stamps through rad_callseg every two ticks - and
                 the tune must STALL anyway, because the first IRQ8 after the
                 move exits through the old segment. The negative control:
                 without it the `rtc` arm could pass on the poll's stamp.
  tick           MartyPC os8088_5150_sb_gla + patch 05's OPL3 (tick class).
                 On the tick class the claim reaches the resident's segment
                 only through a service call - RADS_HALTED, when a frame
                 HALTs - so FAN.RAD is LOADED before the move (RADGATE `n`)
                 and STARTED after it: it HALTs at interrupt time, and the
                 HALT's far call must reach the new segment (the chip and
                 channels 0..7 given back, RSTF_HALTED); RV1.RAD then plays
                 at 2.75 frames a tick from the moved image.
  tick-nocallseg rad_callseg's stamp NOPed (the start and every poll stamp
                 nothing): snd_tickp's stamp ALONE must still carry the HALT.
  tick-nostamp   both NOPed: the HALT far-calls the old segment and must NOT
                 give the chip back - the negative control for the pair.

What no arm reaches is named in SPEC.md 34.12.8: a move taken while an RTC
BIOS chain's frame is suspended inside the ROM's own `sti`.

THE POLL HOLD-OFF IS A WORKAROUND FOR A KERNEL DEFECT this row found, and the
defect is PRE-EXISTING on origin/main: drv_svc_call_x is FAR-called from
drv_svc_call, but its nothing-published exit jumps to drv_svc_none, whose
`ret` is near (drv_fs_call shares it). An OSAPI_SND_FM verb with no sound
driver loaded therefore returns into COLD_SEG at the stub's offset and runs
off into LOW_SEG - measured on QEMU: RADGATE's two-tick verb 6 poll hung the
UI task with the graphics lock held the moment the Control Panel unmounted
SOUND.DRV. Decision D8: it is FIXED IN A PULL REQUEST OF ITS OWN against main,
not in RADBOX's work, which changes no kernel file - so this row holds the
poll off (RADGATE `rt_nopoll`) and goes on being about a moving image.
SPEC.md 96.8 records the defect; SPEC.md 96.2 puts the guard on the package
(OSAPI_SND_CAPS before every verb 6, which reads 0 once the driver is gone),
which is what makes RADBOX correct on a kernel either side of the fix.
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.chdir(ROOT)
import os88build                                            # noqa: E402
import os88geom                                             # noqa: E402
import radlib                                               # noqa: E402

S = radlib.S
u16 = radlib.u16
MC_SIZE = os88geom.MC_SIZE
MEM_MAX = 32
CP_I0Y, CP_IROWH, CP_RX = 6, 14, 96     # tests/heaphi.py's Control Panel
CP_DBY1, CP_DROWH, CP_IDRV = 20, 26, 2
RAD_WORK = 13312 + 1024
NOP4, NOP5 = b"\x90" * 4, b"\x90" * 5
STAMP_I70 = b"\x8c\x0e\x24\x00"         # mov [RO_RSEG], cs
STAMP_ES = b"\x26\x8c\x0e\x24\x00"      # mov [es:RO_RSEG], cs

fails = []


def say(*a):
    print(*a)
    sys.stdout.flush()


def check(name, cond, note=""):
    say("  %-4s %s%s" % ("ok" if cond else "FAIL", name,
                         ("  " + note) if note else ""))
    if not cond:
        fails.append(name)


def wait_for(cond, limit, step=0.5):
    t0 = time.time()
    while time.time() - t0 < limit:
        if cond():
            return True
        time.sleep(step)
    return cond()


# --- the heap --------------------------------------------------------------
def claims(m):
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    return sorted((u16(raw, i * MC_SIZE), u16(raw, i * MC_SIZE + 2),
                   u16(raw, i * MC_SIZE + 4))
                  for i in range(MEM_MAX) if u16(raw, i * MC_SIZE))


def runs(m, shed=False):
    """[(base, paras)] of every free run between mem_base and mem_top - with
    `shed`, as they would be once every purgeable cache (an owner whose high
    byte is MEM_PG_TRIV..MEM_PG_HIGH, FBh..FEh) was shed: mem_claim sheds
    before it compacts a second time (SPEC.md 66.4), so an ask that a shed
    alone can answer never moves anything."""
    base, top = u16(m.read(S("mem_base"), 2)), u16(m.read(S("mem_top"), 2))
    out, at = [], base
    for b, p, o in claims(m):
        if shed and 0xFB <= (o >> 8) <= 0xFE:
            continue
        if b > at:
            out.append((at, b - at))
        at = max(at, b + p)
    if top > at:
        out.append((at, top - at))
    return out


def dump(m, tag):
    say("   -- %s" % tag)
    free = dict(runs(m))
    rows = [(b, p, "own %04x" % o) for b, p, o in claims(m)]
    rows += [(b, p, "HOLE") for b, p in free.items()]
    for b, p, what in sorted(rows):
        say("      %04x %6.1fK %s" % (b, p / 64.0, what))


# --- the two machines ------------------------------------------------------
class QemuBox:
    """QEMU ADLIB=1 through tests/radrtc.py's launch: an AT, the RTC class."""

    def __init__(self):
        import radrtc
        self.rt = radrtc
        self.m = radrtc.launch(os88build.at("build/os8088.img"))
        self.box = radlib.Box(self.m)

    def poke(self, addr, data):
        from sndtick import Gdb
        g = Gdb(self.rt.GDBPORT)
        try:
            g.write(addr, data)
        finally:
            g.detach()

    def key(self, ch):
        self.m.hmp("sendkey " + ch)
        time.sleep(0.5)

    def mouse(self, *a):
        subprocess.run(["python3", "tools/mouse.py", self.rt.SOCK]
                       + [str(x) for x in a], check=True, capture_output=True)

    def at(self, x, y):
        # PROVEN, because a `to` sent while the Control Panel is still
        # writing its configuration was measured landing nowhere
        for _ in range(6):
            self.mouse("to", x, y)
            time.sleep(0.5)
            b = self.m.read(S("mouse_x"), 4)
            if (u16(b, 0), u16(b, 2)) == (x, y):
                return
            time.sleep(2)
        raise SystemExit("radmove: the pointer never reached %d,%d" % (x, y))

    def click(self, x, y):
        self.at(x, y)
        self.mouse("down")
        time.sleep(0.2)
        self.mouse("up")
        time.sleep(1.5)

    def menu(self, x0, y0, x1, y1):
        self.at(x0, y0)
        self.mouse("down")
        self.mouse("to", x1, y1)
        time.sleep(0.5)
        self.mouse("up")
        time.sleep(4)

    def open_gate(self):
        _mo, win = self.rt.open_gate(self.m, self.box)
        return win

    def settle(self, s=3):
        time.sleep(s)

    def ticks_per_sec(self):
        return 18.2065

    def close(self):
        self.m.quit()
        time.sleep(1.5)


class MartyBox:
    """MartyPC os8088_5150_sb_gla + patch 05's OPL3: an 8088, the tick class."""

    def __init__(self, M, m):
        from os88mouse import Mouse
        self.M, self.m = M, m
        self.mo = Mouse(marty=m)
        self.box = radlib.Box(m)

    def poke(self, addr, data):
        self.m.write(addr, data)

    def key(self, ch):
        self.m.run()
        self.m.key("Key" + ch.upper() if ch.isalpha() else "Digit" + ch)
        time.sleep(0.8)

    def click(self, x, y):
        self.m.run()
        self.mo.click(x, y)

    def menu(self, x0, y0, x1, y1):
        self.m.run()
        self.mo.menu(x0, y0, x1, y1)
        self.settle(4)

    def open_gate(self):
        # the double-click is the one MartyPC step a loaded host fails with
        # "two FIRST clicks" (docs/TESTING.md); a retry is a fresh one
        import radopl3
        for tries in range(3):
            try:
                _mo, win = radopl3.gate_open(self.m, self.box, self.M)
                return win
            except self.M.MartyError as e:
                say("  (open RADGATE: %s - again)" % str(e)[:60])
                time.sleep(15)
        raise SystemExit("radmove: RADGATE never opened")

    def settle(self, s=3):
        self.m.run()
        try:
            self.M.settle(self.m, limit=max(15.0, s * 4))
        except self.M.MartyError:
            pass
        time.sleep(s / 3.0)

    def close(self):
        pass


# --- the arena -------------------------------------------------------------
def gate_mem(mb, win, op, slot, kb=0, limit=60):
    """RADGATE `c` / `f` / `k`: (CF, DX), or None when it never answered."""
    box = mb.box
    focus(mb, win)
    n0 = box.gread("rt_mres", 4)[3]
    g = (box.gseg << 4)
    mb.poke(g + box.G["rt_mkb"], bytes([kb & 0xFF, kb >> 8]))
    mb.poke(g + box.G["rt_mslot"], bytes([slot]))
    mb.key(op)
    if not wait_for(lambda: box.gread("rt_mres", 4)[3] != n0, limit, 0.3):
        return None
    r = box.gread("rt_mres", 4)
    return r[0], u16(r, 1)


def focus(mb, win):
    x, y, w, _h = os88geom.win_rect(mb.m, win, S)
    mb.click(x + w // 2, y + 5)


def build_arena(mb, label, tune_key, tune_len):
    """Steps 1-4 of the docstring. Returns (win, ask KB, image before)."""
    m, box = mb.m, mb.box
    win = mb.open_gate()
    snd0 = box.dseg()
    mine = [c for c in claims(m) if c[0] == snd0 or c[2] == snd0]
    sndkb = (sum(p for _b, p, _o in mine) + 63) // 64
    say("  %s: SOUND.DRV at %04x, %d claim(s) %dKB" % (label, snd0, len(mine),
                                                      sndkb))
    before = set(os88geom.win_list(m, S))
    mb.menu(8, 8, 8, 40)                        # chip menu -> Control Panel
    new = [w for w in os88geom.win_list(m, S) if w not in before]
    if not new:
        raise SystemExit("radmove: no Control Panel")
    cp = new[-1]
    x, y, _w, _h = os88geom.win_rect(m, cp, S)
    x0, y0 = x + 1, y + 18
    mb.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)
    mb.settle(3)

    def drvrow():
        focus(mb, cp)
        mb.click(x0 + CP_RX + 40, y0 + CP_DBY1 + CP_DROWH // 2)
        mb.settle(8)

    g = box.gseg << 4
    mb.poke(g + box.G["rt_nopoll"], b"\x01")
    drvrow()
    if box.dseg():
        raise SystemExit("radmove: SOUND.DRV did not unmount")
    free = runs(m)
    big = max(free, key=lambda r: r[1])
    slot = 0
    for b, p in sorted(free, reverse=True):     # every run above the big one
        if b > big[0] and p >= 64 and slot < 4:
            r = gate_mem(mb, win, "c", slot, p // 64)
            if not r or r[0]:
                raise SystemExit("radmove: the ceiling fill was refused")
            slot += 1
    tkb = (tune_len + RAD_WORK + 1023) // 1024
    if gate_mem(mb, win, "c", 4, tkb)[0] or \
            gate_mem(mb, win, "c", 5, sndkb + 2)[0]:
        raise SystemExit("radmove: X or P1 refused")
    drvrow()
    snd1 = box.dseg()
    mb.poke(g + box.G["rt_nopoll"], b"\x00")
    if not snd1:
        raise SystemExit("radmove: SOUND.DRV did not mount again")
    focus(mb, cp)
    mb.click(*os88geom.close_xy(x, y))
    mb.settle(3)
    gate_mem(mb, win, "f", 4)
    focus(mb, win)
    mb.key(tune_key)
    if not wait_for(lambda: box.claim() != 0, 40):
        raise SystemExit("radmove: the tune never loaded")
    mb.settle(3)
    gate_mem(mb, win, "f", 5)
    dump(m, "%s: the arena" % label)
    tune = box.claim()                          # owned by the driver too
    mine = sorted(c for c in claims(m) if (c[0] == snd1 or c[2] == snd1)
                  and c[0] != tune)
    lo = mine[0][0]
    hi = max(b + p for b, p, _o in mine)
    free = dict(runs(m, shed=True))
    above = free.get(hi, 0)
    below = [p for b, p in free.items() if b + p == lo]
    below = below[0] if below else 0
    ask = (above + below) // 64
    others = max([p for b, p in free.items() if b not in (hi, lo - below)]
                 + [0]) // 64
    check("%s: the arena - a hole above SOUND.DRV (%.1fK) and a run below it "
          "(%.1fK), the tune claim above both, and no other run of %dKB"
          % (label, above / 64.0, below / 64.0, ask),
          above >= (hi - lo) and below and ask > others and ask > below // 64
          and tune > hi,
          "sound %04x..%04x tune %04x, largest other run %dKB"
          % (lo, hi, tune, others))
    return win, ask, snd1, (lo, hi)


def force(mb, win, label, ask, snd1, span, control=False):
    """Step 5: the ask. Returns the image's segment after it. A negative
    control may halt the machine inside the poison fill, so it is not asked
    for RADGATE's answer - only for the move."""
    box = mb.box
    r = gate_mem(mb, win, "k", 7, ask, limit=30 if control else 90)
    snd2 = box.dseg()
    lo, hi = span
    answered = r is not None and r[0] == 0 and r[1] <= lo and \
        r[1] + ask * 64 >= hi
    check("%s: the forcing ask %s, SOUND.DRV's image MOVED%s"
          % (label, "was made" if control else "was answered",
             "" if control else ", and the whole of where it was is inside "
             "the poisoned block"),
          snd2 and snd2 != snd1 and (control or answered),
          "answer %r, image %04x -> %04x, old span %04x..%04x"
          % (r, snd1, snd2, lo, hi))
    return snd2


# --- the RTC arms ----------------------------------------------------------
def rtc_arm(nostamp):
    label = "rtc-nostamp" if nostamp else "rtc"
    say("%s: QEMU ADLIB=1, the RTC class, RV1.RAD playing across the move"
        % label)
    mb = QemuBox()
    m, box, rt = mb.m, mb.box, mb.rt
    try:
        check("%s: the pacer class is RTC" % label, box.dbyte("rad_class") == 1)
        b0, v0 = rt.cmos(m, 0x0B), rt.vec70(m)
        tune = open("tests/fixtures/rad/RV1.RAD", "rb").read()
        win, ask, snd1, span = build_arena(mb, label, "1", len(tune))
        playing = box.claim() and box.obyte("ro_state") == 2
        check("%s: RV1.RAD playing on int 70h before the move" % label,
              playing and rt.vec70(m) == (snd1, box.D["rad_i70"]),
              "vector %04x:%04x" % rt.vec70(m))
        if nostamp:
            at = (snd1 << 4) + box.D["rad_i70.stamp"]
            check("%s: rad_i70's stamp is where the map says" % label,
                  m.read(at, 4) == STAMP_I70, m.read(at, 4).hex())
            mb.poke(at, NOP4)
        snd2 = force(mb, win, label, ask, snd1, span, control=nostamp)
        claim = box.claim()
        t0, f0 = box.tick(), box.frames()
        n0 = radlib.u32(m.read((claim << 4) + box.O["rp_nirq"], 4))
        time.sleep(6)
        t1, f1 = box.tick(), box.frames()
        n1 = radlib.u32(m.read((claim << 4) + box.O["rp_nirq"], 4))
        dt = (t1 - t0) & 0xFFFF
        fps = (f1 - f0) / (dt / 18.2065) if dt else 0.0
        if nostamp:
            check("rtc-nostamp: WITHOUT rad_i70's stamp the tune stalls after "
                  "the move (the poll's own stamp does not save it)",
                  fps < 5.0, "%d frames in %d ticks" % (f1 - f0, dt))
            return
        check("rtc: the int 70h vector names the MOVED rad_i70",
              rt.vec70(m) == (snd2, box.D["rad_i70"]),
              "vector %04x:%04x" % rt.vec70(m))
        check("rtc: the claim's RO_RSEG names the moved image",
              u16(m.read((claim << 4) + 36, 2)) == snd2,
              "%04x" % u16(m.read((claim << 4) + 36, 2)))
        check("rtc: frames still 50 a second and IRQ8s still counted after the "
              "move", abs(fps - 50.0) <= 1.5 and dt and
              (n1 - n0) / (dt / 18.2065) > 200,
              "%.2f frames a second, %d IRQ8s in %d ticks"
              % (fps, n1 - n0, dt))
        focus(mb, win)
        mb.key("s")
        time.sleep(2)
        check("rtc: stop after the move gives register B and the vector back "
              "(RADS_UNHOOK reached the new segment)",
              box.obyte("ro_state") == 1 and rt.cmos(m, 0x0B) == b0 and
              rt.vec70(m) == v0 and box.dbyte("rad_iarm") == 0,
              "B %02x/%02x vector %04x:%04x" % ((b0, rt.cmos(m, 0x0B))
                                                + rt.vec70(m)))
        mb.key("g")
        time.sleep(3)
        f = box.frames()
        time.sleep(2)
        check("rtc: START after the move hooks int 70h again (RADS_HOOK) and "
              "plays", box.obyte("ro_state") == 2 and box.frames() > f and
              rt.vec70(m) == (snd2, box.D["rad_i70"]),
              "state %d vector %04x:%04x" % ((box.obyte("ro_state"),)
                                             + rt.vec70(m)))
        mb.key("s")
        time.sleep(2)
        mb.key("x")
        time.sleep(2)
        check("rtc: all-off frees the claim, B and the vector as they were",
              box.claim() == 0 and not box.held(claim) and
              rt.cmos(m, 0x0B) == b0 and rt.vec70(m) == v0,
              "claim %04x" % box.claim())
    finally:
        mb.close()


# --- the tick arms ---------------------------------------------------------
def tick_arm(which):
    import os88marty as M
    label = which
    say("%s: MartyPC os8088_5150_sb_gla + patch 05's OPL3, the tick class, "
        "FAN.RAD loaded across the move" % label)
    os.environ.pop("MARTYPC_OPL2", None)
    os.environ.pop("MARTYPC_NO38A", None)
    with M.launch(os88build.at("build/os8088-360.img"),
                  apps=os88build.at("build/radgate360.img"),
                  machine="os8088_5150_sb_gla", boot=False) as m:
        m.run()
        M.settle(m, gate=M.desktop_up)
        M.no_saver(m)
        m.run()
        mb = MartyBox(M, m)
        box = mb.box
        check("%s: the pacer class is tick" % label, box.dbyte("rad_class") == 0)
        tune = open(os.path.join(ROOT, "build", "radgate", "FAN.RAD"),
                    "rb").read()
        win, ask, snd1, span = build_arena(mb, label, "n", len(tune))
        check("%s: FAN.RAD loaded, stopped" % label,
              box.claim() and box.obyte("ro_state") == 1)
        if which in ("tick-nocallseg", "tick-nostamp"):
            at = (snd1 << 4) + box.D["rad_callseg.stamp"]
            check("%s: rad_callseg's stamp is where the map says" % label,
                  m.read(at, 4) == STAMP_I70, m.read(at, 4).hex())
            mb.poke(at, NOP4)
        if which == "tick-nostamp":
            at = (snd1 << 4) + box.D["snd_tickp.stamp"]
            check("%s: snd_tickp's stamp is where the map says" % label,
                  m.read(at, 5) == STAMP_ES, m.read(at, 5).hex())
            mb.poke(at, NOP5)
        snd2 = force(mb, win, label, ask, snd1, span,
                     control=which == "tick-nostamp")
        claim = box.claim()
        focus(mb, win)
        n0 = box.res()[4]
        mb.key("g")
        halted = wait_for(lambda: box.obyte("ro_state") == 1 and
                          box.ostat()[3] & 0x10, 20)
        own = m.read((snd2 << 4) + box.D["opl_own"], 8)
        gave = halted and box.dbyte("rad_chip") == 0 and own == b"\xff" * 8
        if which == "tick-nostamp":
            check("tick-nostamp: WITHOUT either stamp the HALT's service call "
                  "goes to the old image and the chip is NOT given back",
                  not gave, "halted %s chip %d own %s"
                  % (halted, box.dbyte("rad_chip"), own.hex()))
            return 1 if fails else 0
        check("%s: START after the move, FAN.RAD HALTs at interrupt time and "
              "RADS_HALTED reaches the MOVED image - the chip and channels "
              "0..7 given back" % label, box.res()[4] != n0 and gave,
              "halted %s chip %d own %s" % (halted, box.dbyte("rad_chip"),
                                            own.hex()))
        check("%s: the claim's RO_RSEG names the moved image" % label,
              u16(m.read((claim << 4) + 36, 2)) == snd2,
              "%04x, image %04x" % (u16(m.read((claim << 4) + 36, 2)), snd2))
        if which == "tick":
            mb.key("1")
            ok = wait_for(lambda: box.claim() and box.obyte("ro_state") == 2, 30)
            m.run()
            t0, f0 = box.tick(), box.frames() or 0
            wait_for(lambda: (box.tick() - t0) & 0xFFFF >= 182, 120, 0.5)
            t1, f1 = box.tick(), box.frames() or 0
            per = (f1 - f0) / max(1, (t1 - t0) & 0xFFFF)
            check("tick: RV1.RAD then plays from the moved image at 2.75 "
                  "frames a tick, the tick proc named",
                  ok and abs(per - 500 / 182.0) < 0.06 and
                  box.kcell() == box.D["snd_tickp"],
                  "%.3f frames a tick, kernel %04x" % (per, box.kcell()))
            mb.key("x")
            time.sleep(2)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("arms", nargs="*",
                    default=["rtc", "rtc-nostamp", "tick", "tick-nocallseg",
                             "tick-nostamp"])
    a = ap.parse_args()
    for arm in a.arms:
        if arm in ("rtc", "rtc-nostamp"):
            rtc_arm(arm == "rtc-nostamp")
        elif arm in ("tick", "tick-nocallseg", "tick-nostamp"):
            tick_arm(arm)
        else:
            raise SystemExit("radmove: no arm %r" % arm)
    say("radmove: %s" % ("FAILED: " + ", ".join(fails) if fails else
                         "the image moved under a loaded tune on both pacer "
                         "classes and the tune followed it; take a stamp out "
                         "and it does not"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
