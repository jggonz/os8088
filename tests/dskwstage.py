#!/usr/bin/env python3
"""Does the DMA staging arm work - and does it move the RIGHT bytes?
(SPEC.md 18.4.2.1, and 18.4.1/18.4.2 for the run arithmetic around it)

    make && python3 tests/dskwstage.py

`dskw_runadd` gives THREE answers. `CF=1` is an I/O error, `CF=0` with `CX=0`
means every sector went into the pending run, and `CF=0` with `CX != 0` means
`dskw_runmax` answered 0 - not one sector fits the 64KB DMA page the source
currently sits in, so the caller stages that one sector through `dsk_secbuf`
and calls again.

**Both callers dropped that third answer** from `2e8e292` until SPEC.md
18.4.2.1: a shared `jmp .ioerr` trampoline sat exactly where it fell through,
so `dskw_wdata.stg` and `dskw_rdata.stg` had ZERO incoming jumps and had never
executed, on any machine, in any test, since they were written. What the user
saw was `FERR_IO` on a transfer whose buffer happened to start in the last 512
bytes of a physical 64KB page - a "Disk error" that moves whenever anything
else in the tree changes size. Fixing it did not restore a path; it TURNED ON
a staging routine nobody had ever watched run, which is why this file asserts
the bytes and not the return code.

NOTHING ON A BOOTED DESKTOP REACHES IT, and that is by design: §18.4.1 keeps
every base of the kernel's own making 512-aligned, and 64KB is a whole number
of sectors, so a sector that starts 512-aligned always ends inside its own
page. The condition therefore has to be ARRANGED, and this file arranges it
the way a package would meet it by accident - `mem_claim` a block big enough
to span a 64KB physical boundary (200KB spans three), then hand `dskw_write_x`
a buffer that starts 0xF0 bytes short of one. `dskw_runmax` answers 0 there
and nowhere else.

FIVE CASES, and the two CONTROLS are what stop this file passing on a harness
that is not arranging anything:

  W1  a straddling WRITE            .stg must fire exactly once
  W2  a page-safe write, same bytes .stg must NOT fire            (control)
  R1  a page-safe read of W1's file .stg must not fire; bytes must match
  R2  a straddling read of it       .stg must fire once; bytes must match
  R3  a page-safe read of W2's file .stg must not fire; bytes must match (control)

R1 is the one that prices the write half: the destination is page-safe, so it
comes back on the ordinary run path, and the bytes it delivers are what the
STAGED write actually put on the disk. R2 is the read half's own.

AND THEN THE HOST READS THE DISK. The floppy is flushed and walked by
`tests/unit/t_image.Vol` - an independent FAT12 reader that shares no code
with the kernel - so the last assertion does not go through os8088 at all:
both files are 2,000 bytes, both chains are sane, both contents equal the
pattern, and **the two files are byte-identical to each other**. The same
2,000 bytes written the staged way and the ordinary way must land the same, or
one of the two paths is wrong and the guest cannot be the one to say which.
`os88disk.py --verify` then fsck's the volume, because a staging path that
wrote a good file over somebody else's clusters would satisfy everything
above.

THE PATTERN IS NOT ZEROES and the destination is POISONED first. A staged
sector that never copied, or copied the wrong 512 bytes, has to show up as a
difference rather than as a plausible run of nulls - and the 48 bytes past EOF
in the destination must still be poison afterwards, which is the overrun test:
the file is 2,000 bytes, the last sector is read whole into `dsk_secbuf`, and
only 464 of its 512 bytes may reach the caller.

`--bug` INVERTS W1: the straddling write must be REFUSED with FERR_IO, which
is what every kernel from `2e8e292` to SPEC.md 18.4.2.1 does. That is the A/B
this row exists to make repeatable - point it at a pre-fix image
(`--img`/`--apps`) and it must pass; point it at this one and it must fail.
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402
import t_image                                              # noqa: E402

KERNEL_SEG = 0x60
KB = KERNEL_SEG << 4
COLD_SEG = os88sym.equates()["COLD_SEG"]
CB = COLD_SEG << 4

CLAIM_KB = 200                  # > 128KB, so it spans at least two 64KB
                                # physical boundaries whatever segment it lands
                                # on - one for the source, one for the
                                # destination, and neither reused
NEAR_END = 0xF0                 # bytes left in the page at the buffer's start.
                                # Anything in 1..511 makes dskw_runmax answer
                                # 0; 0xF0 is a paragraph multiple, so the
                                # buffer is ES:0 and dskw_norm has nothing to
                                # fold - the straddle is the ONLY thing under
                                # test
FSIZE = 2000                    # 3 whole sectors + a 464-byte tail, over two
                                # clusters at spc=2: the staged sector, a run
                                # after it, a cluster hop, and a partial final
                                # sector, in one file
POISON = 0x5A
FERR_IO = 2


def pattern(n):
    """Distinct-looking bytes: never 0, never POISON, and never periodic with
    512, so a sector delivered from the wrong offset differs."""
    return bytes((((i * 7 + (i >> 8) * 31) & 0xFF) or 0x11) for i in range(n))


class Caller(object):
    """Call a near kernel routine on a PAUSED machine (tests/icoclip.py's).

    THE ENTRY IS `park`, NOT `setreg("ip")`: `pc` is the fetch pointer, so a
    bare write leaves the bytes the 8088 already prefetched from the old
    address in front of the new ones. `park` goes through the reset vector,
    which flushes the queue and clears every register - so all of them are
    written after it.

    CS IS `COLD_SEG` HERE, not KERNEL_SEG. `kernel/diskw.inc`'s transfer
    pipelines live in `.cold` (SPEC.md 2.6), which runs at its own segment,
    and so does the return trap - a near `ret` cannot cross one.

    SS IS LEFT ALONE, and that is a requirement rather than an economy:
    `dskw_stage` reaches `dsk_secbuf` as `push ss / pop es`, so the staging
    this file is about only works with SS = LOW_SEG.
    """

    def __init__(self, m, trap="dskw_rmtree"):
        self.m = m
        self.trap = os88sym.linear(trap)
        self.lock = os88sym.linear("sch_lock")
        # **ON TASK 0's STACK, NOT ON THE INTERRUPTED TASK'S**, and this is
        # what the row was failing on - `dskw_write_x never returned`, 2 runs
        # in 3, at both ends of the pass (docs/plans/HANDOFF-SOAK-FINDINGS.md E2,
        # which got as far as ruling out the ROM and the box's load).
        #
        # It used to take the paused machine's own SS:SP and drop 96 bytes,
        # on the reasoning that the call *"goes as deep as the BIOS's own
        # int 13h handler, on this task's stack"*. It does - and the stack it
        # was landing on is 128 bytes:
        #
        #   sch_chstack  .lowbss 0x1556  128 bytes, the ROM int 08h chain's
        #   sch_stacks   .lowbss 0x15D6  the twelve task slices, first = 128
        #
        # SPEC.md 8.1.2 is why it is always that one: an idle desktop is
        # **96.9% halted**, so a pause lands on the idle task, whose slice is
        # the smallest class in SCH_PARTITION. Measured, the paused SP was
        # 0x1654 - two bytes into a slice topping out at 0x1656 - and the
        # traced call took SP down to **0x1562**, which is 116 bytes BELOW
        # `sch_stacks`, through its canary word and into `sch_chstack`. The
        # damage then shows up whenever something reads those bytes, which is
        # why the row failed sometimes rather than always.
        #
        # `STK0_TOP` is the answer and not merely a bigger number.  Task 0 is
        # the UI task, it owns no slice of `sch_stacks` (kernel/sched.inc's
        # header), its stack runs from `kernel_low_end` 0x23DE to 0x25DC with
        # nothing below it, and **it is the stack the real caller uses**: a
        # file operation on this machine IS the UI task calling this routine.
        # So the row now reproduces the shipping caller's stack rather than an
        # idle task's slice, which is a better experiment as well as a
        # survivable one.
        #
        # SS is still LOW_SEG, which is the requirement: `dskw_stage` reaches
        # `dsk_secbuf` as `push ss / pop es`.  Clobbering task 0's parked frame
        # costs nothing here - `park` clears IF, so from the first call on,
        # nothing but this file's own calls ever executes.
        self.ss = os88sym.equates()["LOW_SEG"]
        self.sp = os88sym.equates()["STK0_TOP"]

    def call(self, name, watch=(), limit=180.0, **regs):
        """Returns (registers at the trap, {watched symbol: times hit}).

        The watched symbols are exec breakpoints armed alongside the trap, so
        "did this block execute" is answered by the CPU rather than inferred
        from what came back. `.stg` had never fired in the history of this
        kernel; a test that could not count that would be asserting the fix
        from its own return code.
        """
        m = self.m
        wmap = dict((os88sym.linear(w), w) for w in watch)
        m.bp_exec(self.trap, *wmap.keys())
        m.cmd(cmd="park", cs=COLD_SEG, ip=os88sym.linear(name) - CB)
        sp = (self.sp - 2) & 0xFFFF
        off = self.trap - CB
        m.write((self.ss << 4) + sp, bytes((off & 0xFF, off >> 8)))
        m.setreg("ss", self.ss)
        m.setreg("sp", sp)
        m.setreg("ds", KERNEL_SEG)
        m.setreg("es", KERNEL_SEG)
        # **INTERRUPTS ON, AND THE SCHEDULER LOCKED**, in that order of
        # importance. `park` goes through the reset vector, so it clears FLAGS
        # with every other register and this ran with IF = 0 for its whole
        # life. That is not what the shipping caller does and it is why the
        # row hung.
        #
        # The BIOS's floppy handler starts the MOTOR and then waits for it to
        # come up to speed, and it times that wait on the BIOS tick at
        # 0040:006C - a byte only IRQ0 advances. With interrupts off the tick
        # never moves and the wait never ends: sampled at the timeout, the
        # guest was at F000:FF23 with IF=0 and a PIC read in front of it,
        # parked in the ROM for 540 guest seconds.
        #
        # THE PROOF IS THE MOTOR. `launch` settles on the first desktop, a few
        # hundred milliseconds after the last boot read, with the motor still
        # turning - and a handler that finds it already up skips the wait. So
        # the row passed exactly when it won that race, which is what "2 runs
        # in 3" was. Adding five idle guest seconds before taking over - long
        # enough for the ROM's own motor-off timer - took it to **0 of 6**,
        # deterministically, which is the cleanest evidence in this file.
        #
        # Interrupts alone are not enough, because this Caller's context is
        # not a task the scheduler knows about: the first IRQ0 to reach
        # `sch_switch` would park our synthetic SP into whatever `sch_cur`
        # names. [sch_lock] is the byte `sch_isr` tests before it picks
        # ("locked: count ticks but do not switch"), and it is exactly what
        # `dsk_xfer` raises around every int 13h in this system. The tick
        # still runs and still advances 0040:006C, `mou_isr` still runs on its
        # own stack (SPEC.md 9.10), and nothing switches.
        #
        # It is restored to what it was after the trap, which is right
        # whatever the routine's own inc/dec did in between: with the machine
        # parked in our call, nothing else can have touched it.
        was_lock = m.read(self.lock, 1)[0]
        m.write(self.lock, bytes((was_lock + 1,)))
        m.setreg("flags", 0x0202)
        for r, v in regs.items():
            m.setreg(r, v & 0xFFFF)
        # **AND THE SETUP IS READ BACK.** Every line above is one debug-server
        # round trip, and a synthetic call is exactly as good as the registers
        # it starts with: a `setreg` that does not take leaves the value
        # `park`'s CPU reset left - 0 for SP - and the machine then runs off
        # with a stack pointer nobody chose. That is the residual failure this
        # row had after the stack fix, and its signature was SS:SP = 1860:2A08
        # with the "stack" pointing at kernel CODE. Confirming costs one round
        # trip and turns a silent bad start into a sentence naming it, which
        # is what the rest of this harness does for every other action.
        want = {"ss": self.ss, "sp": sp, "cs": COLD_SEG,
                "ip": os88sym.linear(name) - CB}
        want.update((k, v & 0xFFFF) for k, v in regs.items())
        got = m.regs()
        wrong = [(k, want[k], got[k]) for k in want if got.get(k) != want[k]]
        if wrong:
            raise SystemExit(
                "dskwstage: the call setup for %s did not take - %s. Every "
                "register above is a debug-server round trip and one of them "
                "was not applied; running anyway would report on a machine "
                "nobody configured"
                % (name, ", ".join("%s wanted %04X, reads %04X" % w
                                   for w in wrong)))

        hits = dict((w, 0) for w in watch)
        while True:
            m.run()
            if m.wait_stop(limit) is None:
                raise SystemExit("dskwstage: %s never returned - %s"
                                 % (name, _where(m, limit)))
            r = m.regs()
            flat = (r["cs"] << 4) + r["ip"]
            if flat in wmap:
                hits[wmap[flat]] += 1
                continue
            if flat != self.trap:
                raise SystemExit("dskwstage: %s stopped at %#07x, which is "
                                 "neither the return trap %#07x nor a watched "
                                 "symbol" % (name, flat, self.trap))
            m.write(self.lock, bytes((was_lock,)))
            return r, hits


def _where(m, limit):
    """WHERE the guest is, for a call that never came back.

    `wait_stop`'s budget is GUEST seconds now (os88marty, and
    docs/plans/HANDOFF-SOAK-FINDINGS.md E2 is the entry that made it so), so a
    timeout here means the machine really did run that long inside the call -
    it is not the box being loaded. What it does NOT say is where, and "the
    machine is still running" was the whole of the old message: E2 spent four
    runs and two ROM sets on it and got no further than "the hang is still
    open".

    So sample the program counter. A hang has a shape: a tight loop reads as
    two or three addresses in one routine, a wait on a device reads as one,
    and a machine walking off into nothing reads as addresses with no symbol
    near them at all. Six samples a fifth of a guest second apart cost
    nothing and turn "it hung" into "it is spinning in <symbol>+0x<n>".

    The samples are taken PAUSED and the machine is left running, because the
    caller raises straight after and the `with launch(...)` block still has to
    tear the emulator down.
    """
    # **SYMBOL VALUES ARE SECTION-RELATIVE, and IP is what to compare them
    # against.** `os88sym.syms()` answers offsets within a section and
    # `os88sym.linear()` answers a flat address; a walker that sorts the first
    # and searches with the second gets a name for every address and every
    # name is wrong. It cost this diagnosis a whole pass: adjacent addresses
    # came back `menu_drop.poll`, `fpg_begin.shr`, `wm_db_b`, `fm_saycl` -
    # four unrelated subsystems in twenty bytes, which is the tell.
    #
    # This kernel is near-model (CLAUDE.md), so within a segment the offset IS
    # the section offset: KERNEL_SEG holds `.text`/`.bss` and COLD_SEG holds
    # `.cold` (SPEC.md 2.6). So pick the table by CS and search it with IP.
    _sec = os88sym.sections()
    _syms = os88sym.syms()
    _by_seg = {
        os88sym.KERNEL_SEG: sorted((v, k) for k, v in _syms.items()
                                   if _sec.get(k) in (".text", ".bss")),
        COLD_SEG: sorted((v, k) for k, v in _syms.items()
                         if _sec.get(k) == ".cold"),
    }

    def near(cs, ip):
        tab = _by_seg.get(cs)
        if tab is None:
            return "ROM" if cs >= 0xF000 else "not a kernel segment"
        best, lo = None, 0
        for v, k in tab:
            if v <= ip:
                best, lo = k, v
            else:
                break
        return ("%s+0x%x" % (best, ip - lo)) if best is not None else "?"

    seen, extra = [], []
    for i in range(6):
        m.pause()
        r = m.regs()
        flat = (r["cs"] << 4) + r["ip"]
        seen.append("%04X:%04X (%s)" % (r["cs"], r["ip"],
                                        near(r["cs"], r["ip"])))
        if i == 0:
            # IF is the first thing to know when the PC is in the ROM: the
            # BIOS's int 13h hands the transfer to DMA and SPINS on IRQ6
            # (SPEC.md 15.3.8), so a handler entered with interrupts off can
            # never be completed by the controller it is waiting for.
            extra.append("IF=%d" % (1 if r["flags"] & 0x200 else 0))
            extra.append("code at CS:IP %s"
                         % bytes(m.read(flat, 8)).hex())
            # ...and who called in. The eight words above SP are enough to
            # spot our own return trap and the routine under it.
            sp = (r["ss"] << 4) + r["sp"]
            extra.append("stack %s"
                         % " ".join("%04X" % int.from_bytes(
                             bytes(m.read(sp + k * 2, 2)), "little")
                             for k in range(10)))
            extra.append("SS:SP %04X:%04X" % (r["ss"], r["sp"]))
            extra.append("regs " + " ".join(
                "%s=%04X" % (k.upper(), r[k]) for k in
                ("ax", "bx", "cx", "dx", "si", "di", "bp", "ds", "es")))
            # **A CPU EXCEPTION LANDS HERE AND LOOKS LIKE A HANG**, so name
            # it rather than leaving a segment nobody recognises. The one
            # this machine actually takes is int 0: kernel/disk.inc warns
            # about "zeroing disk_read's CHS divisor and divide-faulting with
            # sch_lock held" in as many words, and a `div` by a zero geometry
            # word is a jump through vector 0 to wherever that points.
            ivt = bytes(m.read(0, 5 * 4))
            for v in range(5):
                o = int.from_bytes(ivt[v * 4:v * 4 + 2], "little")
                g = int.from_bytes(ivt[v * 4 + 2:v * 4 + 4], "little")
                if (g, o) == (r["cs"], r["ip"]):
                    extra.append("**this IS int %02X's vector** - the machine "
                                 "took a CPU exception, it did not hang" % v)
        m.run()
        os88marty.guest_sleep(m, 0.2)
    m.pause()
    uniq = []
    for x in seen:
        if x not in uniq:
            uniq.append(x)
    return ("the machine ran %g GUEST seconds inside it and is at %s; %s"
            % (limit * os88marty.GUEST_BUDGET_RATIO, ", ".join(uniq),
               "; ".join(extra)))


def blast(m, addr, data, chunk=2048):
    for i in range(0, len(data), chunk):
        m.write(addr + i, data[i:i + chunk])


def name_at(m, s):
    """Put a NUL-terminated 8.3 name where DS:SI can see it.

    `dskw_rt_name` is `dskw_rmtree`'s 12-byte scratch and nothing else's
    (kernel/diskw.inc has three references, all inside that routine), so it is
    idle for the whole of a read or a write - and rmtree is also this file's
    return trap, so it can never run and reclaim it.
    """
    b = s.encode("ascii") + b"\0"
    assert len(b) <= 12
    m.write(os88sym.linear("dskw_rt_name"), b)
    return os88sym.linear("dskw_rt_name") - KB


def run(img, apps, machine, want_bug, verbose):
    bad = []
    src_pat = pattern(FSIZE)
    with os88marty.launch(img, apps=apps, machine=machine,
                          label="dskwstage") as m:
        # **TAKE THE MACHINE OVER AT A DEFINED POINT, not wherever `pause`
        # lands.** Everything below runs kernel routines on a synthetic
        # context, so the state they inherit is whatever the guest was in the
        # middle of - and `pause` is a host-timed act, so that is different
        # every run. It is the row's only remaining source of variance and it
        # was worth a failure in three even after the stack was fixed: a pause
        # inside a disk operation leaves `dsk_secbuf`, the FAT window and
        # [sch_lock] mid-flight, and the staging path below shares all three.
        #
        # `sch_idle_body.loop` is the quiet point (SPEC.md 8.1.2): the idle
        # task's own footprint is at most 4 bytes, nothing is in a primitive,
        # and on a settled desktop the machine is 96.9% halted so it arrives
        # at once. Stopping there costs one breakpoint and makes every run
        # start from the same machine.
        m.bp_exec(os88sym.linear("sch_idle_body.loop"))
        m.run()
        if m.wait_stop(30.0) is None:
            raise SystemExit("dskwstage: the guest never reached the idle "
                             "task's loop - it is not a settled desktop, and "
                             "every call below would inherit whatever it is "
                             "doing instead")
        m.bp_exec()                 # ...and disarm it: the Caller arms its own
        # **AND LET THE DRIVE GO QUIET FIRST.** `launch` settles on the first
        # desktop, which is a few hundred milliseconds after the last boot
        # read: the motor is still turning, the BIOS's own motor-off timer has
        # not run, and IRQ6 may still be pending. Everything below then runs
        # `int 13h` with IF clear (see the Caller), so a completion that
        # arrives from the PREVIOUS operation is one this call will never
        # account for. Five guest seconds is past every motor timeout in the
        # ROMs here, and the machine spends them halted (SPEC.md 8.1.2), so it
        # is five seconds of nothing rather than five seconds of work.
        m.run()
        os88marty.guest_sleep(m, 5.0)
        m.bp_exec(os88sym.linear("sch_idle_body.loop"))
        m.run()
        if m.wait_stop(30.0) is None:
            raise SystemExit("dskwstage: the guest left the idle loop and did "
                             "not come back - something is running that was "
                             "not there five seconds ago")
        m.bp_exec()
        if not m.read(os88sym.linear("dsk_mntok"), 1)[0]:
            raise SystemExit("dskwstage: the boot volume is not mounted - "
                             "there is nothing to write to")
        spc = m.read(os88sym.linear("dsk_spc"), 1)[0]
        c = Caller(m)

        r, _ = c.call("mem_claim_x", ax=CLAIM_KB, bx=0xFF09)   # MEM_K_CLONE
        if r["flags"] & 1:
            raise SystemExit("dskwstage: mem_claim refused %dKB - the heap on "
                             "this machine cannot host the straddle"
                             % CLAIM_KB)
        base, end = r["dx"] << 4, (r["dx"] << 4) + CLAIM_KB * 1024

        # The 64KB physical boundaries inside the claim. Two are needed and a
        # 200KB claim has three wherever it lands, so this is a fact about the
        # size rather than a hope about the allocator.
        bounds = [b for b in range(((base + 0xFFFF) & ~0xFFFF), end, 0x10000)
                  if b - NEAR_END >= base and b + 0x1000 <= end]
        if len(bounds) < 2:
            raise SystemExit("dskwstage: the %dKB claim at %#07x spans %d 64KB "
                             "boundaries and this row needs 2"
                             % (CLAIM_KB, base, len(bounds)))
        straddle_src = bounds[0] - NEAR_END
        straddle_dst = bounds[1] - NEAR_END
        # ...and two page-SAFE buffers, a full 4KB clear of any boundary in
        # both directions, for the controls.
        safe_src = bounds[0] + 0x2000
        safe_dst = bounds[0] + 0x4000
        for a in (safe_src, safe_dst):
            if min((a & 0xFFFF), 0x10000 - (a & 0xFFFF)) < 0x1000:
                raise SystemExit("dskwstage: the 'safe' buffer at %#07x is "
                                 "within 4KB of a page edge - the control "
                                 "would be testing the same thing as the case"
                                 % a)
        # DISJOINT, asserted rather than eyeballed. Every buffer is written
        # whole (4KB for a destination, so the poison covers the read), and
        # two that overlap make one case quietly rewrite another's source -
        # which shows up as a byte comparison passing for the wrong reason.
        spans = {"straddle_src": (straddle_src, 4096),
                 "straddle_dst": (straddle_dst, 4096),
                 "safe_src": (safe_src, 4096), "safe_dst": (safe_dst, 4096)}
        for i, (n1, (a1, l1)) in enumerate(sorted(spans.items())):
            if a1 < base or a1 + l1 > end:
                raise SystemExit("dskwstage: %s at %#07x runs outside the "
                                 "claim %#07x..%#07x" % (n1, a1, base, end))
            for n2, (a2, l2) in sorted(spans.items())[i + 1:]:
                if a1 < a2 + l2 and a2 < a1 + l1:
                    raise SystemExit("dskwstage: %s (%#07x) and %s (%#07x) "
                                     "overlap" % (n1, a1, n2, a2))
        if verbose:
            print("  claim %#07x..%#07x  spc=%d" % (base, end, spc))
            print("  straddling src %#07x (page off %#06x), dst %#07x"
                  % (straddle_src, straddle_src & 0xFFFF, straddle_dst))
            print("  page-safe  src %#07x, dst %#07x" % (safe_src, safe_dst))

        blast(m, straddle_src, src_pat)
        blast(m, safe_src, src_pat)

        # --- W1: the straddling write --------------------------------------
        r, hits = c.call("dskw_write_x", watch=("dskw_wdata.stg",),
                         si=name_at(m, "STGW.TST"),
                         es=straddle_src >> 4, bx=0, cx=FSIZE, dx=0)
        cf, ax, n = r["flags"] & 1, r["ax"], hits["dskw_wdata.stg"]
        if want_bug:
            if not (cf and ax == FERR_IO):
                bad.append("W1 --bug: the straddling write returned CF=%d "
                           "AX=%d, and a pre-18.4.2.1 kernel refuses it with "
                           "CF=1 FERR_IO(%d). This image is FIXED."
                           % (cf, ax, FERR_IO))
            if n:
                bad.append("W1 --bug: dskw_wdata.stg executed %d times, and "
                           "in a pre-18.4.2.1 kernel nothing can reach it at "
                           "all" % n)
            return bad
        if cf or ax:
            bad.append("W1: the straddling write was REFUSED, CF=%d AX=%d "
                       "(FERR_IO is %d). dskw_runmax answered 0 and the third "
                       "case went to the error arm - which is exactly the "
                       "defect SPEC.md 18.4.2.1 fixed" % (cf, ax, FERR_IO))
        if n != 1:
            bad.append("W1: dskw_wdata.stg executed %d times, want 1. The "
                       "source starts %d bytes short of a 64KB page, so "
                       "exactly one sector stages and the rest go in runs"
                       % (n, NEAR_END))

        # --- W2: the same bytes, page-safe (control) -----------------------
        r, hits = c.call("dskw_write_x", watch=("dskw_wdata.stg",),
                         si=name_at(m, "SAFEW.TST"),
                         es=safe_src >> 4, bx=0, cx=FSIZE, dx=0)
        if r["flags"] & 1 or r["ax"]:
            bad.append("W2 (control): an ORDINARY write failed, CF=%d AX=%d - "
                       "this row's machinery is broken, not the staging"
                       % (r["flags"] & 1, r["ax"]))
        if hits["dskw_wdata.stg"]:
            bad.append("W2 (control): dskw_wdata.stg fired %d times on a "
                       "PAGE-SAFE buffer, so the .stg counter above is not "
                       "measuring the straddle" % hits["dskw_wdata.stg"])

        # --- R1/R2/R3: read it back ----------------------------------------
        def readback(tag, fname, dst, want_stg):
            m.write(dst, bytes([POISON]) * 4096)
            r, hits = c.call("dskw_read_x", watch=("dskw_rdata.stg",),
                             si=name_at(m, fname),
                             es=dst >> 4, bx=0, cx=4096, dx=0)
            got = m.read(dst, 4096)
            if r["flags"] & 1:
                bad.append("%s: reading %s failed, AX=%d"
                           % (tag, fname, r["ax"]))
                return None
            size = r["ax"] | (r["dx"] << 16)
            if size != FSIZE:
                bad.append("%s: %s came back %d bytes, want %d"
                           % (tag, fname, size, FSIZE))
            n = hits["dskw_rdata.stg"]
            if n != want_stg:
                bad.append("%s: dskw_rdata.stg executed %d times, want %d"
                           % (tag, n, want_stg))
            if got[:FSIZE] != src_pat:
                first = next(i for i in range(FSIZE)
                             if got[i] != src_pat[i])
                bad.append("%s: %s reads back WRONG at byte %d (sector %d, "
                           "offset %d): got %#04x, want %#04x. The staging "
                           "path moved bytes and moved the wrong ones, which "
                           "is worse than the error it replaced"
                           % (tag, fname, first, first // 512, first % 512,
                              got[first], src_pat[first]))
            tail = got[FSIZE:512 * ((FSIZE + 511) // 512)]
            if tail != bytes([POISON]) * len(tail):
                bad.append("%s: %d bytes PAST EOF were overwritten in the "
                           "caller's buffer - the last sector is read whole "
                           "into dsk_secbuf and only %d of its bytes may "
                           "reach the caller"
                           % (tag, sum(1 for b in tail if b != POISON),
                              FSIZE % 512))
            return got[:FSIZE]

        r1 = readback("R1", "STGW.TST", safe_dst, 0)
        r2 = readback("R2", "STGW.TST", straddle_dst, 1)
        readback("R3", "SAFEW.TST", safe_dst, 0)
        if r1 is not None and r2 is not None and r1 != r2:
            bad.append("R1/R2: the same file read into a page-safe buffer and "
                       "into a straddling one differ - the read staging is "
                       "delivering different bytes from the run path")

        # --- and the host reads the disk, with none of the kernel's code ---
        with tempfile.NamedTemporaryFile(suffix=".img", delete=False) as t:
            out = t.name
        try:
            m.flush(0, path=out)
            bad += host_check(out, src_pat, verbose)
        finally:
            os.unlink(out)
    return bad


def host_check(path, src_pat, verbose):
    """Walk the flushed floppy with tests/unit/t_image's own FAT12 reader.

    Independent of os8088 by construction: it shares no code with the kernel,
    so a writer and a reader that agree on the same wrong thing cannot pass
    it. This is where "the bytes are right" is actually settled.
    """
    bad = []
    v = t_image.Vol(t_image.read(path), os.path.basename(path))
    found = {}
    for _path, name11, attr, clus, size in v.walk():
        nm = name11.decode("ascii", "replace")
        if nm in ("STGW    TST", "SAFEW   TST"):
            found[nm.replace(" ", "")] = (clus, size)
    for want in ("STGWTST", "SAFEWTST"):
        if want not in found:
            bad.append("host: %s is not in the flushed volume's root at all"
                       % want)
    if len(found) != 2:
        return bad

    blobs = {}
    for nm, (clus, size) in sorted(found.items()):
        if size != FSIZE:
            bad.append("host: %s's directory entry says %d bytes, want %d"
                       % (nm, size, FSIZE))
        chain, eoc = v.chain(clus)
        if not (0xFF8 <= eoc <= 0xFFF):
            bad.append("host: %s's chain ends at %#05x, which is not an EOC - "
                       "the FAT link the staged sector's cluster needed never "
                       "landed" % (nm, eoc))
        want_clus = -(-FSIZE // (512 * v.spc))
        if len(chain) != want_clus:
            bad.append("host: %s owns %d clusters, want %d"
                       % (nm, len(chain), want_clus))
        blob = b""
        for cl in chain:
            lba = v.cluster_lba(cl)
            blob += v.blob[lba * v.byts: (lba + v.spc) * v.byts]
        blobs[nm] = blob[:FSIZE]
        if blobs[nm] != src_pat:
            first = next((i for i in range(min(len(blobs[nm]), FSIZE))
                          if blobs[nm][i] != src_pat[i]), FSIZE)
            bad.append("host: %s's bytes ON THE DISK are wrong from byte %d "
                       "(sector %d + %d) - read by a FAT12 walker that shares "
                       "no code with the kernel that wrote them"
                       % (nm, first, first // 512, first % 512))
        if verbose:
            print("  host: %-9s %d bytes, clusters %s"
                  % (nm, size, chain))
    if len(blobs) == 2 and blobs["STGWTST"] != blobs["SAFEWTST"]:
        bad.append("host: the staged write and the ordinary write of the SAME "
                   "2,000 bytes produced different files")

    rc = subprocess.run([sys.executable,
                         os.path.join(ROOT, "tools", "os88disk.py"),
                         "--verify", path],
                        capture_output=True, text=True)
    if rc.returncode:
        bad.append("host: os88disk.py --verify refuses the volume after the "
                   "staged write:\n%s" % (rc.stdout + rc.stderr).strip())
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--img", default=os.path.join(ROOT, "build",
                                                  "os8088-360.img"))
    ap.add_argument("--apps", default=os.path.join(ROOT, "build",
                                                   "apps360.img"))
    # The twin: docs/plans/HANDOFF-SOAK-FINDINGS.md E2 ran this row on the IBM ROM
    # and on GLaBIOS and got the identical hang, so the ROM is measured NOT
    # to be the variable here.
    ap.add_argument("--machine",
                    default=os88marty.machine("os8088_5150_cga"))
    ap.add_argument("--bug", action="store_true",
                    help="assert the PRE-18.4.2.1 behaviour instead: the "
                         "straddling write must be refused FERR_IO and .stg "
                         "must never execute")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    bad = run(a.img, a.apps, a.machine, a.bug, a.verbose)
    for b in bad:
        print("FAIL: %s" % b)
    if bad:
        print("dskwstage: %d failure(s)" % len(bad))
        return 1
    print("dskwstage: %s" % (
        "--bug: PRE-18.4.2.1 behaviour confirmed - the straddling write is "
        "refused FERR_IO and .stg never executes" if a.bug else
        "5 cases, 2 controls, host-side FAT12 read-back - the DMA staging arm "
        "runs and moves the right bytes"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
