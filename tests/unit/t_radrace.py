#!/usr/bin/env python3
"""A HALT at interrupt time, landed between every two instructions of verb 5
(SPEC.md 34.12.2's "every state test commits in the IF = 0 window it was made
in").

    python3 tests/unit/t_radrace.py            (needs nasm and Python's unicorn)

The pacer HALTs a tune from inside an interrupt (34.13.3 step 5): the stop
sequence, the tune set stopped, channels 0..7 and the chip handed back, from
which instant another instance may claim a channel. Verb 5 runs at the
caller's IF, so a pause, stop, resume or start that tests the tune's state and
then acts on it can be overtaken between the two. No emulator gate can place
an interrupt on a chosen instruction, so this row does it on the host:
RADPLAY.DRV - the SHIPPED build, no RADLOG - assembled here and run in
Unicorn's 16-bit x86, driven the way SOUND.DRV drives it. For each verb it
runs the verb once to count its instructions, then, for every instruction
boundary k in the stretch where the race lives and at which IF = 1, restores
a snapshot, runs the verb k instructions, fires the tick-class pacer there
(RADV_TICK on a stack of its own, as an IRQ0 would) with FAN.RAD loaded - a
tune whose first frame HALTs (tests/unit/radrows.py) - and runs the verb to
its end. Then, whatever k was:

  - the state and the pacer agree: playing exactly when armed;
  - the status block's state is the overlay's;
  - a tune that HALTed ends stopped, not holding the chip, and the verb wrote
    NO port after the HALT - neither a second stop sequence nor a key-off
    onto a chip that is no longer the tune's;
  - a pause that was not overtaken ends paused, a stop stopped.

The negative control assembles the same overlay with the `cli` taken out of
the pause and stop windows, and the rows above must fail on it - so the row
can see the defect it exists for, and a text drift that stops the mutation
applying is itself a failure.

It also reads SPEC.md 34.12.5's private stack's WATER MARK: the top RAD_STK
bytes of the claim filled with a sentinel, the pacer run over RV2.RAD (the
riff depth-8 guard), HEAVY (the bound tick) and FAN.RAD (a HALT, through the
service call), and the deepest byte touched - plus the RTC class's and the
real resident's extra 16 bytes, counted from the source - must be within half
of RAD_STK.

What it does NOT reach is the RESIDENT's half of the rule - start's step 0
disarm before its step 1 claim re-reads opl_own, and rad_tickset - which run
in SOUND.DRV; SPEC.md 96.8 names that.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                          # noqa: E402
import radrows                                           # noqa: E402

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INSN
    from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BP, UC_X86_REG_CS,
                                   UC_X86_REG_CX, UC_X86_REG_DS, UC_X86_REG_ES,
                                   UC_X86_REG_FLAGS, UC_X86_REG_IP, UC_X86_REG_SP,
                                   UC_X86_REG_SS, UC_X86_INS_IN, UC_X86_INS_OUT)
except ImportError:
    print("t_radrace: SKIP - Python's unicorn module is not installed "
          "(pip install unicorn)")
    sys.exit(0)

CLAIM, RSEG, STK, TSTK, HALTSEG = 0x2000, 0x4000, 0x5000, 0x7000, 0x6000
RAD_TUNE = 13312
RADV_LOAD, RADV_START, RADV_PAUSE, RADV_RESUME, RADV_STOP, RADV_TICK = 1, 2, 3, 4, 5, 8
RST_STATE = 0
HALTS = 0x200                   # RSEG:0200h - the resident's rad_svc counts here
IF = 0x200


def build(tmp, mutate=False, defines=(), patch=None):
    """The overlay assembled into `tmp`; `defines` are nasm -D switches and
    `patch` a callable handed `tmp` AFTER the sources are copied there and
    before nasm runs (t_radfuzz's negative control). Returns (image, symbols)."""
    src_dir = os.path.join(ROOT, "drivers", "sound")
    for f in os.listdir(src_dir):
        if f.startswith("rad") and (f.endswith(".inc") or f.endswith(".asm")):
            shutil.copy(os.path.join(src_dir, f), tmp)
    if patch is not None:
        patch(tmp)
    main = os.path.join(tmp, "radplay.asm")
    if mutate:
        s = open(main).read()
        for head in ("rd_pause:\n    pushf ", "rd_stop:\n    pushf "):
            at = s.find(head)
            k = s.find("\n    cli ", at)
            if at < 0 or k < 0 or k - at > 200:
                return None
            s = s[:k] + "\n    nop " + s[k + len("\n    cli "):]
        open(main, "w").write(s)
    mapf = os.path.join(tmp, "rp.map")
    with open(main, "a") as f:
        f.write("\n[map symbols %s]\n" % mapf)
    binf = os.path.join(tmp, "rp.bin")
    r = subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", tmp + "/",
                        "-I", os.path.join(ROOT, "drivers") + "/",
                        "-I", os.path.join(ROOT, "apps") + "/"]
                       + ["-D" + d for d in defines] + ["-o", binf, main],
                       capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("t_radrace: nasm failed:\n" + r.stderr)
    syms = {}
    for line in open(mapf):
        f = line.split()
        if len(f) == 3 and re.fullmatch(r"[0-9A-F]+", f[0]):
            syms[f[2]] = int(f[0], 16)
    return open(binf, "rb").read(), syms


class Rig(object):
    def __init__(self, img, syms, trace=True):
        self.img, self.S = img, syms
        self.entry = struct.unpack_from("<H", img, 6)[0]
        mu = self.mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        self.outs = 0

        def hout(mu, port, size, value, ud):
            self.outs += 1

        mu.hook_add(UC_HOOK_INSN, hout, None, 1, 0, UC_X86_INS_OUT)
        mu.hook_add(UC_HOOK_INSN, lambda mu, port, size, ud: 0, None, 1, 0, UC_X86_INS_IN)
        self.steps = 0
        self.limit = None

        # Unicorn's own `count` is not an instruction count here (a step of 1
        # runs to the end of a translation block), so the boundary is placed
        # by stopping in the code hook, which fires before each instruction
        def hcode(mu, addr, size, ud):
            if self.limit is not None and self.steps >= self.limit:
                mu.emu_stop()
                return
            self.steps += 1

        if trace:
            mu.hook_add(UC_HOOK_CODE, hcode)
        # the resident: its dispatcher (call bp / retf) and a rad_svc that
        # counts RADS_HALTED calls
        mu.mem_write(RSEG * 16 + 12, b"\xff\xd5\xcb")
        mu.mem_write(RSEG * 16 + 0x100, b"\x2e\xfe\x06" + struct.pack("<H", HALTS) + b"\xc3")

    def w(self, off, v, n):
        self.mu.mem_write(CLAIM * 16 + off, v.to_bytes(n, "little"))

    def byte(self, off):
        return self.mu.mem_read(CLAIM * 16 + off, 1)[0]

    def sym(self, name):
        return self.byte(self.S[name])

    def halts(self):
        return self.mu.mem_read(RSEG * 16 + HALTS, 1)[0]

    def enter(self, verb, stack, ds=CLAIM):
        mu = self.mu
        sp = 0xFFF0 - 4
        mu.mem_write(stack * 16 + sp, struct.pack("<HH", 0, HALTSEG))
        for reg, v in ((UC_X86_REG_SS, stack), (UC_X86_REG_SP, sp), (UC_X86_REG_DS, ds),
                       (UC_X86_REG_ES, 0), (UC_X86_REG_AX, verb), (UC_X86_REG_CX, 0),
                       (UC_X86_REG_BP, self.entry), (UC_X86_REG_FLAGS, 0x0202),
                       (UC_X86_REG_CS, CLAIM), (UC_X86_REG_IP, 12)):
            mu.reg_write(reg, v)

    def here(self):
        return (self.mu.reg_read(UC_X86_REG_CS) << 4) + self.mu.reg_read(UC_X86_REG_IP)

    def done(self):
        return self.here() == HALTSEG * 16

    def run(self, count=None):
        """Run from where the CPU is: `count` instructions, or to the return."""
        if self.done():
            return
        self.steps = 0
        self.limit = count
        try:
            self.mu.emu_start(self.here(), HALTSEG * 16)
        finally:
            self.limit = None

    def call(self, verb, ds=CLAIM, stack=STK):
        self.enter(verb, stack, ds)
        self.run()
        if not self.done():
            raise RuntimeError("verb %d did not return" % verb)
        return self.mu.reg_read(UC_X86_REG_FLAGS) & 1

    def load(self, tune):
        self.mu.mem_write(CLAIM * 16, self.img)
        self.mu.mem_write(CLAIM * 16 + RAD_TUNE, tune)
        self.w(34, 0, 1)                # tick class
        self.w(35, 1, 1)                # an OPL3
        self.w(36, RSEG, 2)
        self.w(38, 0x100, 2)
        self.w(40, 0, 2)                # the stack top: 64KB
        self.w(42, 0, 2)                # no RADLOG buffer
        self.w(44, len(tune), 2)
        return self.call(RADV_LOAD)

    def tick(self):
        """An IRQ0 at this boundary: the CPU state banked, RADV_TICK run on a
        stack of its own, the state put back."""
        ctx = self.mu.context_save()
        self.call(RADV_TICK, ds=RSEG, stack=TSTK)
        self.mu.context_restore(ctx)

    def snapshot(self):
        return bytes(self.mu.mem_read(0, 0x100000)), self.mu.context_save(), self.outs

    def restore(self, snap):
        mem, ctx, outs = snap
        self.mu.mem_write(0, mem)
        self.mu.context_restore(ctx)
        self.outs = outs


SETUPS = {                          # verb -> the verbs that put the tune where
    RADV_PAUSE: (RADV_START,),      # the race lives
    RADV_STOP: (RADV_START,),
    RADV_RESUME: (RADV_START, RADV_PAUSE),
    RADV_START: (),
}
NAMES = {RADV_PAUSE: "pause", RADV_STOP: "stop", RADV_RESUME: "resume",
         RADV_START: "start"}


def race(img, syms, verb):
    """[(k, halted, problems)] for every IF = 1 boundary in the race's stretch."""
    rig = Rig(img, syms)
    if rig.load(radrows.FAN):
        raise SystemExit("t_radrace: FAN.RAD refused by RADV_LOAD")
    for v in SETUPS[verb]:
        rig.call(v)
    rst = syms["rd_st"] + RST_STATE
    rig.enter(verb, STK)
    base = rig.snapshot()
    # the clean run: how many instructions, and where the stretch is. A pause
    # or a stop races before its first port write (after it the pacer is
    # disarmed in either version); a resume or a start after its LAST one
    trace = []

    def note(mu, port, size, value, ud):
        trace.append(rig.steps)

    h = rig.mu.hook_add(UC_HOOK_INSN, note, None, 1, 0, UC_X86_INS_OUT)
    rig.run()
    rig.mu.hook_del(h)
    total = rig.steps
    if verb in (RADV_PAUSE, RADV_STOP):
        lo, hi = 0, (trace[0] if trace else total)
    else:
        lo, hi = (trace[-1] if trace else 0), total
    out = []
    for k in range(lo, hi + 1):
        rig.restore(base)
        if k:
            rig.run(count=k)
        if rig.done():
            break
        if not rig.mu.reg_read(UC_X86_REG_FLAGS) & IF:
            continue
        h0 = rig.halts()
        rig.tick()
        halted = rig.halts() != h0
        outs = rig.outs
        rig.run()
        if not rig.done():
            out.append((k, halted, ["the verb did not return"]))
            continue
        state, armed = rig.sym("ro_state"), rig.sym("rp_armed")
        bad = []
        if (state == 2) != (armed == 1):
            bad.append("state %d with the pacer %s" % (state, "armed" if armed else "disarmed"))
        if rig.byte(rst) != state:
            bad.append("status block says %d, the overlay %d" % (rig.byte(rst), state))
        if halted:
            if state != 1 or rig.sym("ro_chip"):
                bad.append("HALTed, but ends state %d chip %d" % (state, rig.sym("ro_chip")))
            if rig.outs != outs:
                bad.append("%d port writes AFTER the HALT" % (rig.outs - outs))
        elif verb == RADV_PAUSE and state != 3:
            bad.append("a pause not overtaken ends state %d" % state)
        elif verb == RADV_STOP and state != 1:
            bad.append("a stop not overtaken ends state %d" % state)
        out.append((k, halted, bad))
    return out


RAD_STK = 1024
SENTINEL = 0xA5
# what the RTC class's handler and the real resident add over this rig's tick
# path, counted from the source: nine registers pushed rather than seven (+4),
# three words across a frame rather than two (+2), and in a HALT the
# resident's rad_svc (push ds) calling rad_chipoff (a return address, push ax,
# push bx, pushf) where the rig's stub pushes nothing (+10)
RTC_EXTRA = 16


def water(img, syms, tune, ticks):
    """(bytes of the private stack ever used, frames played) for `ticks`
    tick-class pacer interrupts of `tune` from a start."""
    rig = Rig(img, syms, trace=False)
    if rig.load(tune):
        raise SystemExit("t_radrace: the water tune was refused by RADV_LOAD")
    base = CLAIM * 16 + 0x10000 - RAD_STK
    rig.mu.mem_write(base, bytes([SENTINEL]) * RAD_STK)
    rig.call(RADV_START)
    for _ in range(ticks):
        rig.tick()
    stk = bytes(rig.mu.mem_read(base, RAD_STK))
    low = next((i for i, b in enumerate(stk) if b != SENTINEL), RAD_STK)
    frames = struct.unpack_from("<I", bytes(rig.mu.mem_read(
        CLAIM * 16 + syms["rd_st"] + 12, 4)))[0]
    return RAD_STK - low, frames


def main():
    if not shutil.which("nasm"):
        print("t_radrace: SKIP - no nasm")
        sys.exit(0)
    tmp = tempfile.mkdtemp(prefix="radrace-")
    try:
        img, syms = build(os.path.join(tmp))
        for verb in (RADV_PAUSE, RADV_STOP, RADV_RESUME, RADV_START):
            rows = race(img, syms, verb)
            halted = sum(1 for _, h, _ in rows if h)
            bad = [(k, b) for k, _, b in rows if b]
            check(rows and halted, "%s: the race was run - a HALT landed inside the verb"
                  % NAMES[verb], got=(len(rows), halted), want="(>0, >0)")
            check(not bad, "%s: a HALT at any of %d boundaries (%d of them HALTed) leaves "
                  "the tune consistent and the chip untouched" % (NAMES[verb], len(rows), halted),
                  got=bad[:3], want=[])
            print("  %-6s %4d boundaries with IF = 1, %4d HALTed inside the verb, %d bad"
                  % (NAMES[verb], len(rows), halted, len(bad)))
        # SPEC.md 34.12.5's private stack: its water mark on the tunes that
        # reach its deepest paths - RV2.RAD's instruments 12 and 13 recurse
        # into each other's riffs to the depth-8 guard, HEAVY plays the bound
        # tick, FAN.RAD HALTs on its first frame
        fx = os.path.join(ROOT, "tests", "fixtures", "rad")
        worst = 0
        for name, tune, ticks in (("RV2.RAD", open(os.path.join(fx, "RV2.RAD"), "rb").read(), 600),
                                  ("HEAVY", radrows.HEAVY, 64),
                                  ("FAN.RAD", radrows.FAN, 4)):
            used, frames = water(img, syms, tune, ticks)
            worst = max(worst, used)
            print("  stack  %-8s %4d bytes of %d after %d ticks (%d frames)"
                  % (name, used, RAD_STK, ticks, frames))
            check(0 < used and frames, "the private stack's water was read on %s" % name,
                  got=(used, frames), want="(>0, >0)")
        check(worst + RTC_EXTRA <= RAD_STK // 2,
              "the private stack's worst water, with the RTC class's and the real "
              "resident's %d bytes added, is within half of RAD_STK" % RTC_EXTRA,
              got=worst + RTC_EXTRA, want="<= %d" % (RAD_STK // 2))
        mtmp = os.path.join(tmp, "mut")
        os.mkdir(mtmp)
        m = build(mtmp, mutate=True)
        check(m is not None, "negative control: the pause and stop windows' cli found "
              "to take out")
        if m:
            for verb in (RADV_PAUSE, RADV_STOP):
                rows = race(m[0], m[1], verb)
                bad = [(k, b) for k, _, b in rows if b]
                check(bad, "negative control: %s without its cli fails the row"
                      % NAMES[verb], got=len(bad), want=">0")
                print("  %-6s without its window: %d bad boundaries, e.g. %r"
                      % (NAMES[verb], len(bad), bad[:1]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    done("t_radrace")


if __name__ == "__main__":
    main()
