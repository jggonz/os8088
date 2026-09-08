#!/usr/bin/env python3
"""The far end of the LapLink cable, played by the HOST (SPEC.md 62.10.3).

docs/plans/completed/NET-PLAN.md 9 assumed nothing here could be driven without two machines,
and that is still true of the WIRE's verdict. It is not true of the os8088
half. MartyPC's ParallelController stores what is written to the status
register (`status_register_write`) and `status_register_read` has no side
effects, so the debug server's `outb` drives exactly the lines the guest
polls; and `data_register_read` returns what the GUEST last wrote, so `inb`
reads what it is driving. Between them that is the whole cable.

THE WIRE, as drivers/net/lplink.inc drives it:

  os8088 -> here   the DATA register (base+0): D0..D3 = the nibble,
                   D4 = strobe when sending / ack when receiving
  here -> os8088   the STATUS register (base+1): bit 7 = the handshake and
                   bits 6..3 = our nibble

  Bit 7 is BUSY and BUSY IS INVERTED IN HARDWARE - lp_snib reads it through
  `xor al, 0x80`, so ASSERTED IS A RAW ZERO and idle is a raw one. Getting
  that backwards is a link that hangs on the first nibble, which is why it is
  written here rather than left in the shifts.

  A byte is LOW NIBBLE FIRST (lp_sbyte).

WHY IT MAY BE STEPPED AT ALL: every deadline in the transport is in TICKS and
not in polls - docs/plans/completed/NET-PLAN.md 9.1's third defect, found by linksim.py and
fixed before any of this shipped. So a partner that pauses the machine
between nibbles is inside the contract rather than getting away with
something: the guest's clock does not advance while it is paused, so LP_TMO
cannot expire underneath us and lp_turn's whole-tick reversal guard costs
nothing.

It is EXACT rather than fast. Every nibble is a handful of debug-server round
trips with the emulator stepped in between, so this is the instrument for
"does the protocol do the right thing", never for "how quick is it" - that
number comes off two period boxes and PERFORMANCE.md Set 39 already has it.
"""

import errno
import select
import socket

MAGIC_Q = b'O88?'                # master -> slave
MAGIC_R = b'O88!'                # slave -> master, then a version byte

# ...and the same question as the HUNT WINDOW SEES IT. A byte goes low nibble
# first (lp_sbyte), so 'O88?' = 4F 38 38 3F arrives as F,4,8,3,8,3,F,3 and the
# eight-nibble window holds 0xF48383F3 - NOT 0x4F38383F. linksim.py calls this
# MAGQ and the real slave compares against exactly it; deriving it from the
# byte order instead is a window that can never match, on a wire that is
# working perfectly.
MAGQ = 0xF48383F3

# How many emulator steps to let the guest run between our pokes. One nibble
# is a few `in`/`out`s, so this only has to be enough to get from one poll to
# the next; too large just wastes wall clock.
STEP = 400

# Steps to let the guest OBSERVE something we just did. Releasing the ack is
# not enough on its own: lp_snib does not finish until its second LP_WAIT sees
# the ack fall, and nothing in this file runs the guest by itself - so a
# partner that released and immediately re-asserted left the guest waiting for
# a level it had never been given a cycle to read. Measured: the whole magic
# arrived and the reply stalled, which is the reversal race lp_turn exists for
# seen from this end.
SEE = 8
TURN = 40

# NO BLIND STEPPING PAST THE EDGE THAT ACTS, and that is the whole of what
# click_paused had to unlearn. Stepping a mouse packet's worth (60,000
# instructions) to "let the UART clear" runs the guest straight through the
# dispatch and into net_connect, which sends NC_BYE and the magic into a wire
# nobody is watching - so the partner starts looking after the conversation
# has already begun and waits out its budget on nibbles that came and went.
# The partner's own 400-step loop clocks the UART perfectly well; it just has
# to be the thing doing the stepping.
#
# **WHICH EDGE THAT IS HAS CHANGED, AND THE RULE HAS NOT.** This said "after
# the PRESS" for as long as ui_task dispatched a panel control on MDOWN.
# SPEC.md 13.8.3 moved every one of them to the RELEASE, and a PACKAGE's own
# W_ONCLICK still fires on MDOWN - so the two cases are two methods and the
# choice is which EDGE acts, never which window is in front.
# click_paused_release steps freely BETWEEN the two edges (it has to, the
# mouse being a 1200-baud UART that DROPS a packet sent while its predecessor
# is in flight, and a press now puts nothing on the wire) and stops dead at
# the release; click_paused sends the press and stops dead there. Read this
# before "simplifying" either: stepping past whichever edge acts puts the
# failure back exactly as it reads above, and it reads as a broken CABLE.


# --- the socket half (SPEC.md 62.11, drivers/net/netpkg.inc) -----------------
# Mirrored from netpkg.inc and netsock.inc. They are the contract and this is
# the second copy - there is no way for a Python far end to include an asm
# header, which is exactly why every one of these carries its name from there.
NET_VER_SOCK = 2                 # the version byte that says "I speak sockets"
NET_VER_ADDR = 3                 # ...and this one adds NW_ADDR (nwire.inc). A
                                 # version is a FLOOR, not a set: NET.DRV
                                 # compares with `jb`, so 3 implies 2
NET_HOSTMAX = 64                 # the fixed host field on the wire
NET_SOCKS = 4                    # simultaneous handles
NET_SKMAX = 1024                 # the most one SEND or RECV moves

NSK_FREE, NSK_CONNECT, NSK_UP = 0, 1, 2
NSK_CLOSING, NSK_CLOSED, NSK_FAILED, NSK_LISTEN = 3, 4, 5, 6

NSTF_PORT, NSTF_LINK, NSTF_SOCK = 0x01, 0x02, 0x04

NETE_OK, NETE_NOLINK, NETE_BUSY, NETE_HANDLE = 0, 1, 2, 3
NETE_FULL, NETE_IO, NETE_REFUSED, NETE_VERB = 4, 5, 6, 7


class LinkTimeout(Exception):
    pass


class Partner(object):
    """One end of the cable, over a shared Marty connection.

    Shares the caller's `Marty` because the debug server takes ONE client and
    a second connection HANGS rather than failing (CLAUDE.md) - the same rule
    Mouse and Flush already follow.
    """

    def __init__(self, marty, base=0x378, budget=4000000):
        self.m = marty
        self.base = base
        self.budget = budget            # emulator steps before we give up
        self.spent = 0
        self.nib = 0                    # the nibble we are presenting
        self.trace = []                 # every nibble received, for diagnosis
        self.lastop = None              # 'r'/'s', for the reversal guard
        self.log = []                   # ...what was asked, and answered. A
                                        # letter per command says the shape of
                                        # a session and nothing about WHICH
                                        # file, which is the question as soon
                                        # as anything goes wrong
        self.stall = 0                  # TICKS to spend THINKING before the
                                        # first byte of a reply - the far
                                        # machine's floppy motor, which is the
                                        # one thing this harness is otherwise
                                        # incapable of being slow about. See
                                        # _spend_stall
        self.stall_body = 0             # ...and the same, MID-frame: see send_body
        self.stall_ack = 0              # ...and BEFORE AN ACK, which is the
                                        # THIRD place the far machine goes to
                                        # its disk and the one no harness could
                                        # reach: srv_write reads the length,
                                        # CREATES the file and only then reads
                                        # a body byte, so the pause lands
                                        # between os8088's length word and its
                                        # first data byte. Armed by arm_ack(),
                                        # spent by recv_nib
        self._owed = False
        self._ackowed = False
        self._status(idle=True)

    def allow(self, steps):
        """Give the guest `steps` more emulator steps FROM NOW.

        **`budget` IS A LIFETIME TOTAL AND `spent` NEVER RESETS**, which is the
        one thing about this class that reads backwards. `_step` raises when
        `spent > budget`, and `serve` raises the ceiling only for the wait on
        a COMMAND BYTE (`idle`) and puts it straight back afterwards - so once
        a session has spent more than the constructor's budget, the very first
        wait INSIDE a command fails, at a point that looks exactly like the
        guest hanging mid-frame.

        It cost a whole run of tests/socktest.py: the connect handshake spent
        9.4M steps, the default budget is 4M, and the fetch's first
        `recv_word` blew up on its first step with `the guest never moved the
        line we are waiting on` - about a guest that was driving the line
        perfectly well.

        So a long session RE-ANCHORS between phases. This is that, named, so
        the next test finds it instead of the trap.
        """
        self.budget = self.spent + steps

    def arm_ack(self):
        """The NEXT nibble os8088 sends us waits `stall_ack` ticks for its ack.

        Armed rather than continuous because a per-nibble stall would make a
        1KB write take hours of wall clock, and because the real pause has one
        place: srv_write's int 21h 3Ch, between the length and the body.
        """
        self._ackowed = True

    def _spend_stall(self):
        """Run the guest for `self.stall` of ITS OWN ticks, answering nothing.

        The field bug this exists for: the master's wait for a partner to
        BEGIN answering was TURN_RX, 8 ticks, and entering a subdirectory on
        the far machine spins a floppy motor - so the reply was certain to
        arrive after the deadline and the driver called the link lost. A
        harness that answers instantly can never show that, which is why the
        deadline shipped wrong.

        It is counted in the GUEST's ticks at 0040:006C, not in emulator
        steps and not in host seconds: the deadline being tested is in ticks,
        so anything else is a conversion that can be wrong in the direction
        that makes the test pass.
        """
        if not self.stall:
            return
        t0 = int.from_bytes(self.m.read(0x46C, 2), 'little')
        while True:
            t = int.from_bytes(self.m.read(0x46C, 2), 'little')
            if ((t - t0) & 0xFFFF) >= self.stall:
                return
            self.m.step(STEP * 8)

    # --- the two wires -------------------------------------------------------
    def _status(self, idle, nib=None):
        """Drive bit 7 (idle = raw 1) and bits 6..3 (our nibble)."""
        if nib is not None:
            self.nib = nib & 0x0F
        v = (self.nib << 3) & 0x78
        if idle:
            v |= 0x80
        self.m.outb(self.base + 1, v)

    def _data(self):
        return self.m.inb(self.base)

    def _step(self):
        self.m.step(STEP)
        self.spent += STEP
        if self.spent > self.budget:
            raise LinkTimeout('the guest never moved the line we are waiting '
                              'on (%d steps)' % self.spent)

    def _await_strobe(self, want):
        """Spin the guest until its D4 (strobe/ack) reaches `want`."""
        while True:
            if bool(self._data() & 0x10) == want:
                return self._data()
            self._step()

    # --- one nibble each way -------------------------------------------------
    def recv_nib(self):
        """os8088 is sending: lp_snib's counterpart."""
        if self._ackowed:               # ...THE FAR SIDE IS NOT LOOKING YET.
            self._ackowed = False       # See stall_ack: this is the gap that
            saved, self.stall = self.stall, self.stall_ack   # let 62.10.4.8 ship
            self._spend_stall()
            self.stall = saved
        d = self._await_strobe(True)    # nibble is stable before the strobe
        n = d & 0x0F
        self._status(idle=False)        # ack UP (asserted = raw bit 7 zero)
        self._await_strobe(False)       # ...until it drops the strobe
        self._status(idle=True)         # ack down...
        for _ in range(SEE):            # ...AND LET IT BE SEEN. lp_snib does
            self._step()                # not finish until its second LP_WAIT
        self.lastop = 'r'               # observes the ack FALL, and nothing
        return n                        # here runs the guest by itself

    def send_nib(self, n):
        """os8088 is receiving: lp_rnib's counterpart."""
        if self.lastop == 'r':              # A DIRECTION REVERSAL, and it owes
            for _ in range(TURN):           # the guest room to get out of its
                self._step()                # send loop and into lp_rnib. This
            self.lastop = 's'               # is lp_turn's guard from the other
                                            # end of the cable - the far side
                                            # spends a whole tick here for the
                                            # same reason (NET-PLAN 9.1's
                                            # second defect)
        self._status(idle=False, nib=n)     # nibble and strobe together: the
                                            # guest samples from the very read
                                            # that sees the strobe
        self._await_strobe(True)            # it acks with its own D4
        self._status(idle=True, nib=n)      # strobe down, nibble held
        self._await_strobe(False)           # ...and it drops the ack
        return n

    # --- bytes, low nibble first (lp_sbyte) ----------------------------------
    def recv_byte(self):
        lo = self.recv_nib()
        hi = self.recv_nib()
        return (hi << 4) | lo

    def send_byte(self, b):
        if self._owed:                  # the FIRST byte of a reply, and the
            self._owed = False          # only place the far end's own work
            self._spend_stall()         # would delay the wire
        self.send_nib(b & 0x0F)
        self.send_nib((b >> 4) & 0x0F)

    def recv(self, n):
        return bytes(self.recv_byte() for _ in range(n))

    def send(self, data):
        for b in bytes(data):
            self.send_byte(b)

    def send_body(self, data):
        """A file's bytes, with `stall_body` ticks of disk every 512.

        os88net.asm's own send_body reads the file through a 512-byte buffer
        and goes back to int 21h between chunks - so the far side pauses
        MID-FRAME, which is a different wait from the one before a reply and
        was the last one still bounded at LP_TMO rather than [lp_turnw]. A
        harness that streams a file straight out of a Python bytes object
        cannot exercise it.
        """
        for i, b in enumerate(bytes(data)):
            if self.stall_body and i and not (i % 512):
                saved, self.stall = self.stall, self.stall_body
                self._owed = True
                self.send_byte(b)
                self.stall = saved
            else:
                self.send_byte(b)

    def recv_word(self):
        b = self.recv(2)
        return b[0] | (b[1] << 8)

    def send_word(self, v):
        self.send(bytes([v & 0xFF, (v >> 8) & 0xFF]))

    def send_dword(self, v):
        self.send(bytes([v & 0xFF, (v >> 8) & 0xFF,
                         (v >> 16) & 0xFF, (v >> 24) & 0xFF]))

    def recv_dword(self):
        b = self.recv(4)
        return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)

    # --- driving the guest to the point of talking ---------------------------
    def click_paused(self, marty=None):
        """Press the left button on a PAUSED guest, deterministically.

        The caller positions the cursor while the machine runs - mo.to()
        proves where it is by reading guest memory, which needs cycles - then
        pauses, then calls this, then starts receiving.

        THE PRESS ALONE, and no release. **This is the right call for a
        control that acts on the PRESS, which since SPEC.md 13.8.3 means a
        PACKAGE's own W_ONCLICK and no longer means a Control Panel control** -
        see click_paused_release below, and pick by which edge acts rather
        than by which window happens to be in front. Anything sent after the
        press is one more thing that can consume the cycles the handshake
        needs.
        """
        m = marty or self.m
        m.mouse(0, 0, l=True)

    def click_paused_release(self, marty=None, settle=4_000_000):
        """...and the same for a control that acts on the RELEASE.

        SPEC.md 13.8.3 moved every Control Panel control off the button DOWN:
        the press now ARMS and draws the pressed look, and the RELEASE is what
        runs net_connect. A press-only click therefore leaves the Drivers row
        armed for ever, the driver is never loaded, and the far end waits on a
        wire nobody is holding - which is what it looks like from here, so the
        failure reads as a broken LINK rather than a harness that is one edge
        short. Nothing about the cable changed.

        The two edges cannot both go into a paused guest back to back: the
        mouse is a real 1200-baud UART and a packet sent while its predecessor
        is still in flight is DROPPED (SPEC.md 9.4.3), so the guest would
        decode one press and no release. So the press goes in, the machine
        runs a BOUNDED, deterministic number of cycles - long enough for the
        packet to arrive and ui_task to arm the row, and it puts NOTHING on
        the wire, because arming is all a press does now - and only then is
        the release sent into a paused machine, still the last thing before
        the partner starts receiving. `advance` and not sleep, for the reason
        its own docstring gives.
        """
        m = marty or self.m
        m.mouse(0, 0, l=True)
        m.advance(cycles=settle)
        m.pause()
        m.mouse(0, 0, l=False)

    # --- the handshake -------------------------------------------------------
    def hunt(self, limit=64):
        """slv_hunt: slide an EIGHT-NIBBLE window until the magic is in it.

        Nibble granularity and not byte, for the reason lplslv.inc gives: a
        byte-granular window cannot recover from being half a byte out of
        step, so a slave that joined in the middle of a transfer could never
        resynchronise. SPEC.md 9.5's mouse resync, one level down.

        It is also what a partner needs even when it did NOT join late. The
        first attempt here read four bytes flat and got `XO88` - the master
        sends NC_BYE ('X') to put the far end back to listening before it
        says hello, so the magic is never the first thing on the wire and
        assuming alignment is wrong from the very first exchange.
        """
        want = MAGQ
        win = 0
        for i in range(limit * 2):
            try:
                win = ((win << 4) | self.recv_nib()) & 0xFFFFFFFF
            except LinkTimeout as e:
                raise LinkTimeout('stalled after %d nibble(s), window %08X: %s'
                                  % (i, win, e))
            self.trace.append(win & 0x0F)
            if win == want:
                return True
        raise LinkTimeout('no magic in %d nibbles (window %08X, wanted %08X)'
                          % (limit * 2, win, want))

    def hello(self, version=NET_VER_ADDR):
        """mst_hello's other half: hunt for 'O88?', answer 'O88!' + version.

        THE VERSION BYTE IS THE SOCKET PROBE (SPEC.md 62.11): a far side that
        answers 2 speaks the lowercase socket verbs and one that answers 1 does
        not, so `hello(version=1)` is how a test presents an OLD PARTNER and
        checks that NET.DRV refuses NETV_* with NETE_REFUSED rather than
        sending a letter into a command loop that has never heard of it.
        """
        self.hunt()
        self.send(MAGIC_R)
        self.send_byte(version)
        return MAGIC_Q

    # --- getting the wire to a known state -----------------------------------
    def sync(self, limit=200):
        """Drive the line idle and let the guest come back to rest.

        NEEDED WHEN THE GUEST HAS BEEN LISTENING, and it is a property of this
        harness rather than of the transport. A real cable's BUSY line is
        pulled to its idle level by the receiver; MartyPC's status register
        reads **0** until something writes one, and LP_WAIT reads bit 7
        INVERTED - so an untouched port looks to a dwelling slave like a strobe
        that is permanently asserted. It samples a garbage nibble, raises its
        ack, and parks in the second LP_WAIT waiting for a fall that cannot
        come.

        So the port genuinely reads D4 HIGH before this end has sent anything,
        and `_await_strobe(True)` is satisfied by a cycle that never happened -
        which puts the very first nibble half a handshake out of step, on a
        link where every byte afterwards looks like a timeout.

        Releasing the phantom strobe is one write; what it also needs is CYCLES,
        because nothing else here runs the guest.
        """
        self._status(idle=True)
        for _ in range(limit):
            if not (self._data() & 0x10):
                return
            self._step()
        raise LinkTimeout('the guest never dropped its ack: the wire is stuck '
                          'with D4 high after %d steps' % (limit * STEP))

    # --- ...and the OTHER role: the host as the MASTER ------------------------
    # The nibble layer is symmetric - `lp_snib` and `lp_rnib` are the guest's
    # whichever end it is playing - so nothing below this line needed a second
    # transport. What differs is only who speaks first.
    #
    # This is what tests OS88NET.COM, which until it existed had run on exactly
    # one machine: the field one. tests/dosstub boots it on a cycle-accurate
    # 8088 with a real port at 0x378 and no DOS under it; this end asks the
    # questions NET.DRV would.
    def mst_hello(self, tries=4, patience=3000000):
        """NET.DRV's own: send 'O88?', read 'O88!' and a version byte.

        NO HUNT on this side. The slave hunts because the master sweeps ports
        and it may join mid-transfer; the master has just spoken, so the reply
        is the next thing on the wire by construction.

        IT RETRIES, because the slave's DWELL CAN EXPIRE UNDER IT. `slv_hunt`
        waits SLAVE_DWELL ticks and then returns to `listen_once`, which
        re-enters it - and the guest's clock runs while this end steps it, so
        an exchange begun near the end of a dwell can have the magic accepted
        and the reply never sent. Measured: the same script failed once and
        passed once, decided by where 25 seconds of boot happened to leave the
        guest.

        That is not a fault to fix in the slave - it is what the dwell is FOR,
        and a real master sweeps ports and re-sends for exactly this reason
        (docs/plans/completed/NET-PLAN.md 1.4.5: the slave dwells and the master sweeps). So
        this end sweeps too. `patience` is per attempt and deliberately short:
        a reply that is coming at all is a few hundred thousand steps away.
        """
        last = None
        for _ in range(tries):
            saved = self.budget
            self.budget = self.spent + patience
            try:
                self.send(MAGIC_Q)
                got = self.recv(4)
                if got == MAGIC_R:
                    ver = self.recv_byte()
                    self.budget = saved
                    return ver
                last = 'slave answered %r, wanted %r' % (got, MAGIC_R)
            except LinkTimeout as e:
                last = str(e)
            finally:
                self.budget = saved
            self.lastop = None          # a fresh attempt owes no turnaround
        raise LinkTimeout('no hello in %d attempts: %s' % (tries, last))

    def mst_cmd(self, letter, arg=None):
        """One command, and its argument word when it takes one."""
        self.send_byte(ord(letter))
        if arg is not None:
            self.send_word(arg)

    def mst_bye(self):
        self.send_byte(ord('X'))

    def mst_list(self, handle):
        """NF_LIST -> (status, [32-byte entry]). The count is on the wire
        before the entries are, so a refusal still completes the frame."""
        self.mst_cmd('L', handle)
        st = self.recv_byte()
        n = self.recv_word()
        ents = [self.recv(DE_SIZE) for _ in range(n)]
        return st, ents             # ...and NO bye: that ends the session

    def mst_chdir(self, handle):
        """NF_CHDIR -> (status, parent handle). The parent comes back because
        the kernel has no directory sector to read one out of."""
        self.mst_cmd('C', handle)
        st = self.recv_byte()
        if st:
            return st, None
        par = self.recv_word()
        return st, par

    def mst_stat(self, folder, name):
        """NF_STAT -> (status, handle, size, attr). The name is a fixed 13
        bytes, NUL-padded, and the FOLDER goes with it (SPEC.md 62.10.1)."""
        self.mst_cmd('S', folder)
        self.send(name.encode('ascii').ljust(13, b'\0')[:13])
        st = self.recv_byte()
        if st:
            return st, None, None, None
        h = self.recv_word()
        sz = self.recv_dword()
        at = self.recv_byte()
        return st, h, sz, at

    def mst_read(self, handle, cap):
        """NF_READ -> (status, bytes). The cap goes OUT, so an oversized file
        is cut at the source instead of crossing and being discarded."""
        self.mst_cmd('G', handle)
        self.send_dword(cap)
        st = self.recv_byte()
        if st:
            return st, b''
        n = self.recv_dword()
        return st, self.recv(n)

    def mst_readat(self, handle, off, cap):
        """NF_READAT -> (status, bytes). Past the end is a length of ZERO and
        not a refusal, which is what lets a caller walk to the end without
        knowing where it is."""
        self.mst_cmd('A', handle)
        self.send_dword(off)
        self.send_word(cap)
        st = self.recv_byte()
        if st:
            return st, b''
        n = self.recv_word()
        return st, self.recv(n)

    # --- the write verbs, master side (SPEC.md 62.10.4.5) --------------------
    # Every one of them is a folder, a name, whatever it carries, and ONE
    # status byte back - a FERR_*, passed through from the far side untouched.
    def _name13(self, s):
        return s.encode('ascii').ljust(13, b'\0')[:13]

    def mst_write(self, folder, name, data):
        self.mst_cmd('U', folder)
        self.send(self._name13(name))
        self.send_dword(len(data))
        self.send(data)
        return self.recv_byte()

    def mst_append(self, folder, name, data):
        self.mst_cmd('P', folder)
        self.send(self._name13(name))
        self.send_word(len(data))       # ONE word: the chunked half of the
        self.send(data)                 # pair, so a copy streams through it
        return self.recv_byte()

    def mst_delete(self, folder, name):
        self.mst_cmd('D', folder)
        self.send(self._name13(name))
        return self.recv_byte()

    def mst_rename(self, folder, old, new):
        self.mst_cmd('N', folder)
        self.send(self._name13(old))
        self.send(self._name13(new))    # ...no length between them: the far
        return self.recv_byte()         # side knows the first ends at 13

    def mst_mkdir(self, folder, name):
        self.mst_cmd('M', folder)
        self.send(self._name13(name))
        return self.recv_byte()

    def mst_rmdir(self, folder, name):
        self.mst_cmd('K', folder)
        self.send(self._name13(name))
        return self.recv_byte()

    def mst_enum(self, folder, ordinal):
        """NF_ENUM -> (status, 32-byte entry).

        THE ENTRY IS ALWAYS READ, whatever the status said (SPEC.md 62.10.6):
        the frame is fixed, so a master that stopped at a refusal would leave
        the far side driving nibbles at an end that had gone quiet, and that
        ends the SESSION rather than the command. Status 4 is the end of the
        folder and status 2 is a folder that could not be walked - the caller
        has to keep those apart, which is the verb's whole point.
        """
        self.mst_cmd('E', folder)
        self.send_word(ordinal)
        st = self.recv_byte()
        return st, self.recv(DE_SIZE)

    def mst_rmtree(self, folder, name):
        """NF_RMTREE -> status. A folder AND EVERYTHING IN IT (SPEC.md
        62.10.7). RMDIR's frame exactly, and a different letter."""
        self.mst_cmd('T', folder)
        self.send(self._name13(name))
        return self.recv_byte()

    def mst_copy(self, src, dst, name):
        """NF_COPY -> status. Two handles and one name; no body crosses."""
        self.mst_cmd('Y', src)
        self.send_word(dst)
        self.send(self._name13(name))
        return self.recv_byte()

    def mst_dfree(self):
        """NF_DFREE -> (status, free bytes, granule)."""
        self.mst_cmd('F')
        st = self.recv_byte()
        if st:
            return st, None, None
        free = self.recv_dword()
        gran = self.recv_word()
        return st, free, gran

    # --- serving, once the link is up ----------------------------------------
    def serve(self, tree, limit=64, idle=2000000, sox=None):
        """Answer commands until the master says NC_BYE, or `limit` of them.

        `sox` is a SocketBox and is what makes this a NETWORK far end as well
        as a file server (SPEC.md 62.11). Pass None and the seven lowercase
        socket commands are refused - which is a state worth being able to
        produce, but NOT the one an old partner is in: an old partner answers
        version 1 at the handshake and NET.DRV never sends a socket letter at
        all. Both refusals exist and they are different things.


        THIS IS THE FILE SERVER, and it is here rather than in a file of its
        own for the reason lplink.inc is shared between the driver and
        tests/lptlink: the wire and what rides on it drift apart the moment
        they live in two places. `tree` is what os88net.asm's findfirst walks,
        as a dict - see FileTree below.

        A command is answered and then LEFT: the master sends NC_BYE itself
        after each reply (net_bye), so the loop returns to waiting rather than
        assuming a session shape. Returning on the count is what keeps a test
        from hanging when the driver asks something this does not implement.

        RUNNING OUT OF COMMANDS IS THE ORDINARY ENDING and not a failure - the
        gap between them is the user's thinking time and has no upper bound
        (lplslv.inc's own note on why lp_rbyte_w waits forever). So the wait
        for a command BYTE is allowed to expire and returns; a wait inside a
        half-answered command is not, and still raises.

        `idle` IS HOW LONG THE MASTER MAY BE SILENT, and it has to cover the
        slowest thing the guest can do BETWEEN two commands - which is not
        bounded by the wire at all. Opening a document is the case that found
        this: the association looks for the app on the redirected volume, does
        not find it, and then MOUNTS A FLOPPY AND READS THE PACKAGE OFF IT
        before coming back to read the document. That is seconds of guest time
        on a cycle-accurate drive, and at the default this end had already
        given up and stopped driving the wire - so the guest's next command
        went into a cable nobody was holding, timed out, and Note Pad came up
        empty. It read exactly like the read path being broken.

        So: pass a generous `idle` for any phase that can include a load, and
        remember the cost is paid ONCE per call, at the end.
        """
        seen = []
        for _ in range(limit):
            saved = self.budget
            # A CEILING, NOT AN INCREMENT, and the difference is the whole
            # runtime. `budget += idle` leaves the wait bounded by the TOTAL
            # budget, so "the master has nothing more to say" cost 60 million
            # steps - 150,000 debug round trips, about two and a half minutes,
            # at the end of every serve() call. Two of those is a test that
            # times out before it prints its first line, which is exactly what
            # it did.
            self.budget = self.spent + idle
            try:
                c = self.recv_byte()
            except LinkTimeout:
                return seen              # nothing more to say: we are done
            finally:
                self.budget = saved
            seen.append(chr(c))
            self._owed = True               # ...armed here and spent by the
                                            # first send_byte, which is AFTER
                                            # the arguments have been read:
                                            # stalling any earlier stalls the
                                            # master mid-transmission, which
                                            # is a different bug wearing this
                                            # one's clothes
            if c == ord('X'):               # NC_BYE ENDS THE SESSION, exactly
                return seen                 # as `serve` does on the real far
                                            # end - it leaves its command loop
                                            # and goes back to hunting for the
                                            # magic. This used to `continue`,
                                            # and that ONE LINE of forgiveness
                                            # let the driver send a bye after
                                            # every verb through a whole
                                            # scripted session: the harness
                                            # answered, the real slave would
                                            # have stopped listening. A stand-in
                                            # that is kinder than the thing it
                                            # stands in for hides precisely the
                                            # bugs it exists to find
            if c == ord('I'):               # NC_INFO
                self.send_byte(0)           # status
                self.send_word(0)           # sectors: NONE. A redirected
                                            # volume has none at all
                self.send_byte(0)           # flags: bit 0 = read-only
            elif c == ord('L'):             # NF_LIST: handle -> count, entries
                h = self.recv_word()
                ents = tree.list(h)
                self.log.append('LIST folder=%d -> %d entries' % (h, len(ents)))
                self.send_byte(0)
                self.send_word(len(ents))
                for e in ents:
                    self.send(e)
            elif c == ord('E'):             # NF_ENUM: handle + ordinal -> ONE
                h = self.recv_word()        # entry, for a folder COPY
                n = self.recv_word()
                e = tree.enum(h, n)
                if e is False:              # THREE ANSWERS, NOT TWO: see
                    st, e = 0x02, bytes(DE_SIZE)    # FileTree.enum. FERR_IO
                    what = 'NO SUCH HANDLE'         # for a folder that cannot
                elif e is None:                     # be walked, FERR_NOENT
                    st, e = 0x04, bytes(DE_SIZE)    # for the end of one
                    what = 'end'
                else:
                    st, what = 0, repr(e[:e.index(b'\0')].decode('ascii'))
                self.log.append('ENUM folder=%d ord=%d -> %s' % (h, n, what))
                self.send_byte(st)
                self.send(e)                # ...ALWAYS 32 bytes: the frame is
                                            # fixed whatever the status said
            elif c == ord('T'):             # NF_RMTREE: a folder AND its
                fold = self.recv_word()     # contents, walked by the far side
                name = self.recv(13).split(b'\0')[0].decode('ascii', 'replace')
                st = tree.rmtree(fold, name)
                self.log.append('RMTREE folder=%d %r -> %s'
                                % (fold, name,
                                   'ok' if st == 0 else 'FERR %d' % st))
                self.send_byte(st)
            elif c == ord('Y'):             # NF_COPY: both ends are the far
                src = self.recv_word()      # side's, so no body crosses
                dst = self.recv_word()
                name = self.recv(13).split(b'\0')[0].decode('ascii', 'replace')
                st = tree.copy(src, dst, name)
                self.log.append('COPY %d -> %d %r : %s'
                                % (src, dst, name,
                                   'ok' if st == 0 else 'FERR %d' % st))
                self.send_byte(st)
            elif c == ord('C'):             # NF_CHDIR: handle -> parent
                h = self.recv_word()
                par = tree.parent(h)
                self.log.append('CHDIR handle=%d -> parent %s' % (h, par))
                if par is None:
                    self.send_byte(0x02)    # no such folder
                    continue
                self.send_byte(0)
                self.send_word(par)
            elif c == ord('S'):             # NF_STAT: folder + name -> handle
                fold = self.recv_word()
                name = self.recv(13).split(b'\0')[0].decode('ascii', 'replace')
                h = tree.find(fold, name)
                self.log.append('STAT folder=%d %r -> %s'
                                % (fold, name,
                                   'handle %d' % h if h else 'NOT FOUND'))
                if h is None:
                    self.send_byte(0x02)
                    continue
                self.send_byte(0)
                self.send_word(h)
                self.send_dword(len(tree.data(h)))
                self.send_byte(0x10 if tree.meta[h][1] == FileTree.T_DIR else 0)
            elif c == ord('G'):             # NF_READ: handle + cap -> bytes
                h = self.recv_word()
                cap = self.recv_dword()
                if h not in tree.meta:
                    self.log.append('READ handle=%d -> NO SUCH HANDLE' % h)
                    self.send_byte(0x02)
                    continue
                d = tree.data(h)[:cap]      # THE CAP IS HONOURED HERE, so an
                self.log.append('READ handle=%d cap=%d -> %d bytes'
                                % (h, cap, len(d)))
                self.send_byte(0)           # oversized file is short at the
                self.send_dword(len(d))     # source rather than sent and
                self.send_body(d)           # thrown away
            elif c == ord('A'):             # NF_READAT: a window
                h = self.recv_word()
                off = self.recv_dword()
                cap = self.recv_word()
                if h not in tree.meta:
                    self.log.append('READAT handle=%d -> NO SUCH HANDLE' % h)
                    self.send_byte(0x02)
                    continue
                d = tree.data(h)[off:off + cap]
                self.log.append('READAT handle=%d off=%d cap=%d -> %d bytes'
                                % (h, off, cap, len(d)))
                self.send_byte(0)
                self.send_word(len(d))      # ...ZERO past the end, which is
                self.send_body(d)           # the contract and not an error
            elif c in (ord('U'), ord('P')):     # NF_WRITE / NF_APPEND
                fold = self.recv_word()
                name = self.recv(13).split(b'\0')[0].decode('ascii', 'replace')
                n = (self.recv_dword() if c == ord('U') else self.recv_word())
                self.arm_ack()                  # ...AND HERE IS WHERE THE FAR
                                                # MACHINE CREATES THE FILE. The
                                                # length is off the wire and
                                                # int 21h 3Ch has not run yet,
                                                # so os8088's first body nibble
                                                # waits on a DOS floppy write
                                                # (SPEC.md 62.10.4.8)
                d = self.recv(n)                # THE RUN IS TAKEN WHATEVER we
                                                # do with it: the length is on
                                                # the wire ahead of the bytes
                st = tree.put(fold, name, d, append=(c == ord('P')))
                self.log.append('%s folder=%d %r %d bytes -> %s'
                                % ('WRITE' if c == ord('U') else 'APPEND',
                                   fold, name, n,
                                   'ok' if st == 0 else 'FERR %d' % st))
                self.send_byte(st)
            elif c == ord('D'):             # NF_DELETE
                fold = self.recv_word()
                name = self.recv(13).split(b'\0')[0].decode('ascii', 'replace')
                st = tree.remove(fold, name, want_dir=False)
                self.log.append('DELETE folder=%d %r -> %s'
                                % (fold, name,
                                   'ok' if st == 0 else 'FERR %d' % st))
                self.send_byte(st)
            elif c == ord('N'):             # NF_RENAME
                fold = self.recv_word()
                old = self.recv(13).split(b'\0')[0].decode('ascii', 'replace')
                new = self.recv(13).split(b'\0')[0].decode('ascii', 'replace')
                st = tree.rename(fold, old, new)
                self.log.append('RENAME folder=%d %r -> %r : %s'
                                % (fold, old, new,
                                   'ok' if st == 0 else 'FERR %d' % st))
                self.send_byte(st)
            elif c == ord('M'):             # NF_MKDIR
                fold = self.recv_word()
                name = self.recv(13).split(b'\0')[0].decode('ascii', 'replace')
                st = tree.mkdir(fold, name)
                self.log.append('MKDIR folder=%d %r -> %s'
                                % (fold, name,
                                   'ok' if st == 0 else 'FERR %d' % st))
                self.send_byte(st)
            elif c == ord('K'):             # NF_RMDIR
                fold = self.recv_word()
                name = self.recv(13).split(b'\0')[0].decode('ascii', 'replace')
                st = tree.remove(fold, name, want_dir=True)
                self.log.append('RMDIR folder=%d %r -> %s'
                                % (fold, name,
                                   'ok' if st == 0 else 'FERR %d' % st))
                self.send_byte(st)
            elif c == ord('F'):             # NF_DFREE: free bytes, granule
                self.send_byte(0)
                self.send_word(tree.free & 0xFFFF)
                self.send_word((tree.free >> 16) & 0xFFFF)
                self.send_word(tree.granule)
            # --- the SOCKET verbs (SPEC.md 62.11) ------------------------
            # LOWERCASE, and that is the whole reason they fit: the one-byte
            # command space had six capitals left and the socket layer needs
            # seven. `sox` is a SocketBox and may be None, which is what a
            # partner built before sockets looks like - it never gets here,
            # because such a partner answers version 1 and NET.DRV refuses
            # NETV_* without sending a letter at all (netsock.inc).
            elif c == ord('o'):             # NW_OPEN: port, 64-byte host
                port = self.recv_word()
                host = self.recv(NET_HOSTMAX).split(b'\0')[0]
                st, h = ((NETE_REFUSED, 0) if sox is None else
                         sox.open(host.decode('ascii', 'replace'), port))
                self.send_byte(st)
                self.send_byte(h)           # ...ALWAYS: the frame is fixed
            elif c == ord('l'):             # NW_LISTEN: port
                port = self.recv_word()
                st, h = ((NETE_REFUSED, 0) if sox is None else
                         sox.listen(port))
                self.send_byte(st)
                self.send_byte(h)
            elif c == ord('a'):             # NW_ACCEPT: a listening handle
                h = self.recv_byte()
                st, n = ((NETE_REFUSED, 0) if sox is None else sox.accept(h))
                self.send_byte(st)
                self.send_byte(n)           # 0 = nobody yet, which is ordinary
            elif c == ord('s'):             # NW_STAT: handle
                h = self.recv_byte()
                if sox is None:
                    st, state, rdy = NETE_REFUSED, NSK_FREE, 0
                else:
                    st, state, rdy = sox.status(h)
                self.send_byte(st)
                self.send_byte(state)
                self.send_word(rdy)
            elif c == ord('w'):             # NW_SEND: handle, len, bytes
                h = self.recv_byte()
                n = self.recv_word()
                body = self.recv(n)
                st, took = ((NETE_REFUSED, 0) if sox is None else
                            sox.send(h, body))
                self.send_byte(st)
                self.send_word(took)
            elif c == ord('r'):             # NW_RECV: handle, cap
                h = self.recv_byte()
                cap = self.recv_word()
                st, body = ((NETE_REFUSED, b'') if sox is None else
                            sox.recv(h, cap))
                self.send_byte(st)
                self.send_word(len(body))
                self.send(body)
            elif c == ord('c'):             # NW_CLOSE: handle
                h = self.recv_byte()
                st = NETE_REFUSED if sox is None else sox.close(h)
                self.send_byte(st)
            elif c == ord('i'):             # NW_ADDR: nothing in
                # FOUR BYTES WHATEVER THE STATUS SAYS - the fixed frame, and
                # the refusal path is the one nothing exercises. A real slave
                # answers with the DOS box's own address, because that is the
                # machine an inbound connection reaches (netpkg.inc).
                ip = None if sox is None else sox.addr()
                if ip is None:
                    self.send_byte(NETE_NOLINK)
                    self.send(b'\0\0\0\0')
                else:
                    self.send_byte(0)
                    self.send(bytes(ip))
            else:
                raise LinkTimeout('unknown command %r after %r'
                                  % (chr(c), ''.join(seen)))
        return seen


# --- what the far side is serving --------------------------------------------
# The entries go across as SPEC.md 19.1 staged entries, UNRESHAPED - the far
# side's findfirst builds the row the Disk window draws, and nothing between
# here and font_str touches it. So this file is where the layout has to be
# right, and it is OSAPI_FS_ENT's, byte for byte:
#
#   +0  16  the DISPLAY name, NUL-terminated 8.3 (`MINES.O88`), not the
#           space-padded on-disk field - the kernel reshapes nothing
#   +16  2  type: 0 file, 1 package, 2 folder. NEVER 3: the parent link is
#           the kernel's to synthesize (SPEC.md 19.5)
#   +18  2  the driver's OPAQUE HANDLE, which the kernel only ever hands back
#   +20  4  size in bytes, which fm_measure sums
#   +24  8  zero
#
# The first draft of this laid it out as the FAT-ish 11/1/4/2 the on-disk
# entry uses, which is a different structure that happens to be the same
# length - so every field was in the wrong place and nothing could say so.
DE_SIZE = 32


class SocketBox(object):
    """The far side's TCP, played with REAL host sockets (SPEC.md 62.11).

    This is the half of the cable that os8088 will never have: the 8088 does
    no TCP at all over the link, it asks the far side for one and polls. So
    the harness's job here is not to simulate a stack - it is to BE one, and
    Python's non-blocking sockets are a better far end than any model, because
    a page fetched through this really did cross a socket.

    THREE THINGS ARE FAITHFUL AND ONE IS NOT.

    Faithful: every method answers OUT OF STATE IT ALREADY HAS and never waits
    for the network, which is the property the whole non-blocking API rests
    on; readable bytes are drained into a buffer by `pump` so `recv` is a copy
    and never a syscall that could block; and a peer that closes leaves
    NSK_CLOSING with whatever it sent still readable, so a caller that reads
    a zero-length recv as an end-of-stream is caught here rather than in the
    field.

    NOT faithful: **the name lookup blocks.** A real far side resolves inside
    NSK_CONNECT with a UDP round trip and a timeout; `getaddrinfo` here is
    synchronous, so a hostname that takes a second to resolve stops this end
    dead for a second while the guest is paused anyway. The CONTRACT it is
    standing in for - poll NETV_STATUS until it leaves NSK_CONNECT - is
    exercised either way, because a connect to a live host still passes
    through NSK_CONNECT for at least one poll.
    """

    def __init__(self, log=None):
        self.sk = {}                 # handle -> dict
        self.log = log if log is not None else []
        self.empty_up = 0            # recvs that delivered NOTHING while the
                                     # socket was still NSK_UP - the condition
                                     # a caller must not read as an end, and
                                     # the one a test has to be able to WAIT
                                     # FOR rather than hope for
        self.ip = (127, 0, 0, 1)     # what NW_ADDR answers. A test that wants
                                     # the no-address case sets it to None

    # --- housekeeping -----------------------------------------------------
    def _free(self):
        for h in range(1, NET_SOCKS + 1):
            if h not in self.sk:
                return h
        return 0

    def close_all(self):
        """Every socket dies with the SESSION, which is what NET.DRV's
        nsk_drop assumes at both edges: NC_BYE ends the session and a fresh
        link starts with no handles."""
        for e in list(self.sk.values()):
            try:
                e['s'].close()
            except OSError:
                pass
        self.sk.clear()

    def pump(self):
        """Advance every connect and drain everything readable. Called before
        any verb answers, so an answer is always about NOW."""
        for h, e in list(self.sk.items()):
            s = e['s']
            if e['st'] == NSK_CONNECT:
                r, w, x = select.select([], [s], [s], 0)
                if x:
                    e['st'] = NSK_FAILED
                elif w:
                    err = s.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
                    e['st'] = NSK_UP if err == 0 else NSK_FAILED
            if e['st'] not in (NSK_UP, NSK_CLOSING):
                continue
            while len(e['rx']) < NET_SKMAX * 4:
                r, w, x = select.select([s], [], [], 0)
                if not r:
                    break
                try:
                    b = s.recv(4096)
                except OSError as ex:
                    if ex.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
                        break
                    e['st'] = NSK_FAILED
                    break
                if not b:
                    # THE PEER CLOSED. The bytes already in `rx` stay
                    # readable - that is what NSK_CLOSING means, and it is
                    # why a zero-length recv is not an end (netpkg.inc).
                    e['st'] = NSK_CLOSING
                    break
                e['rx'] += b

    # --- the verbs --------------------------------------------------------
    def open(self, host, port):
        h = self._free()
        if not h:
            return NETE_FULL, 0
        try:
            info = socket.getaddrinfo(host, port, socket.AF_INET,
                                      socket.SOCK_STREAM)
        except OSError:
            self.log.append('OPEN %s:%d -> no such host' % (host, port))
            return NETE_REFUSED, 0
        af, kind, proto, _, addr = info[0]
        s = socket.socket(af, kind, proto)
        s.setblocking(False)
        e = s.connect_ex(addr)
        if e not in (0, errno.EINPROGRESS, errno.EWOULDBLOCK):
            s.close()
            self.log.append('OPEN %s:%d -> refused (%d)' % (host, port, e))
            return NETE_REFUSED, 0
        self.sk[h] = {'s': s, 'st': NSK_CONNECT, 'rx': bytearray()}
        self.log.append('OPEN %s:%d -> handle %d' % (host, port, h))
        return NETE_OK, h

    def listen(self, port):
        h = self._free()
        if not h:
            return NETE_FULL, 0
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind(('127.0.0.1', port))
            s.listen(4)
        except OSError:
            s.close()
            return NETE_REFUSED, 0
        s.setblocking(False)
        self.sk[h] = {'s': s, 'st': NSK_LISTEN, 'rx': bytearray()}
        self.log.append('LISTEN %d -> handle %d' % (port, h))
        return NETE_OK, h

    def accept(self, h):
        e = self.sk.get(h)
        if e is None or e['st'] != NSK_LISTEN:
            return NETE_HANDLE, 0
        r, w, x = select.select([e['s']], [], [], 0)
        if not r:
            return NETE_OK, 0            # nobody yet: the ORDINARY answer
        n = self._free()
        if not n:
            return NETE_FULL, 0
        c, _ = e['s'].accept()
        c.setblocking(False)
        self.sk[n] = {'s': c, 'st': NSK_UP, 'rx': bytearray()}
        self.log.append('ACCEPT %d -> handle %d' % (h, n))
        return NETE_OK, n

    def status(self, h):
        self.pump()
        e = self.sk.get(h)
        if e is None:
            return NETE_HANDLE, NSK_FREE, 0
        return NETE_OK, e['st'], min(len(e['rx']), 0xFFFF)

    def send(self, h, data):
        self.pump()
        e = self.sk.get(h)
        if e is None or e['st'] == NSK_LISTEN:
            return NETE_HANDLE, 0
        if e['st'] != NSK_UP:
            return NETE_OK, 0            # not connected yet, or closing: took
        try:                             # nothing, which the caller retries
            n = e['s'].send(data)
        except OSError as ex:
            if ex.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
                return NETE_OK, 0
            e['st'] = NSK_FAILED
            return NETE_OK, 0
        self.log.append('SEND %d %d byte(s) -> %d taken' % (h, len(data), n))
        return NETE_OK, n

    def recv(self, h, cap):
        self.pump()
        e = self.sk.get(h)
        if e is None or e['st'] == NSK_LISTEN:
            return NETE_HANDLE, b''
        n = min(cap, len(e['rx']))
        out = bytes(e['rx'][:n])
        del e['rx'][:n]
        # LOGGED EVEN WHEN IT IS ZERO, which is the whole point: a
        # zero-length recv on a socket that is still up is what a caller
        # must not read as an end (netpkg.inc), and a log that only recorded
        # the interesting ones could not prove one ever happened.
        if n == 0 and e['st'] == NSK_UP:
            self.empty_up += 1
        self.log.append('RECV %d cap %d -> %d (state %d)'
                        % (h, cap, n, e['st']))
        return NETE_OK, out

    def close(self, h):
        e = self.sk.pop(h, None)
        if e is None:
            return NETE_HANDLE
        try:
            e['s'].close()
        except OSError:
            pass
        self.log.append('CLOSE %d' % h)
        return NETE_OK

    def addr(self):
        """NW_ADDR: this machine's own IPv4, as four ints, or None.

        The real slave answers with the DOS box's `eth_ip`, because that is
        the machine an inbound connection reaches and forwards down the cable
        (docs/plans/completed/NET-STACK-PLAN.md 1.5). Here it is the loopback the tests run
        against, which is the same claim: the address a client should dial to
        reach the sockets this object is serving.
        """
        self.log.append('ADDR')
        return self.ip


class FileTree(object):
    """A folder tree with handles, standing in for os88net.asm's findfirst.

    Handle 0 is the root, which is the ABI's own convention (FSV_CHDIR takes
    0 for it) and not this file's - so the root cannot be a node like any
    other and the tree is keyed from 1.
    """

    def __init__(self, free=1474560, granule=512):
        self.free = free
        self.granule = granule
        self.nodes = {0: (None, [])}    # handle -> (parent, [children])
        self.meta = {}                  # handle -> (name, type, size)
        self.blobs = {}                 # handle -> real bytes, when it matters
        self._next = 1

    T_FILE, T_PKG, T_DIR = 0, 1, 2      # OSAPI_FS_ENT's, and 3 is not ours

    def add(self, parent, name, size=0, typ=T_FILE, content=None):
        h = self._next
        self._next += 1
        self.nodes[parent][1].append(h)
        self.nodes[h] = (parent, [])
        self.meta[h] = (name, typ, len(content) if content is not None
                        else size)
        if content is not None:
            self.blobs[h] = bytes(content)
        return h

    def data(self, h):
        """A file's contents.

        GENERATED rather than stored unless `content=` said otherwise: byte i
        of handle h is (i + 7h) & 0xFF. The same trick the dosstub uses at the
        other end and for the same reason - the side receiving can predict
        every byte, so `it arrived` and `it arrived CORRECT` stop being one
        claim. `content=` is for the cases where the BYTES have to be real: a
        document Note Pad will render, a package the loader will validate."""
        name, typ, size = self.meta[h]
        if typ == self.T_DIR:
            return b''
        if h in self.blobs:
            return self.blobs[h]
        return bytes((i + 7 * h) & 0xFF for i in range(size))

    def find(self, folder, name):
        """A name in a folder -> its handle, or None."""
        if folder not in self.nodes:
            return None
        for c in self.nodes[folder][1]:
            if self.meta[c][0].upper() == name.upper():
                return c
        return None

    # --- the write verbs (SPEC.md 62.10.4.5) --------------------------------
    # Each answers a FERR_*, which is what crosses the wire: 0 ok, 3 name,
    # 4 no such thing, 5 exists, 8 protected. They are apps/os88api.inc's
    # numbers and NOT the block protocol's int 13h codes, which are a
    # different numbering that happens to use small integers too.
    F_OK, F_NAME, F_NOENT, F_EXIST, F_PROT = 0, 3, 4, 5, 8

    def put(self, folder, name, data, append=False):
        if folder not in self.nodes:
            return self.F_NOENT
        h = self.find(folder, name)
        if append:
            if h is None:
                return self.F_NOENT     # APPEND is to an EXISTING file, which
            if self.meta[h][1] == self.T_DIR:   # is 18.4.4's contract
                return self.F_PROT
            self.blobs[h] = self.data(h) + data
        else:
            if h is not None and self.meta[h][1] == self.T_DIR:
                return self.F_PROT
            if h is None:
                # THE TYPE COMES FROM THE NAME, because on the far side it
                # always did: os88net.asm has no stored type at all, and
                # `ent_ispkg` decides a listed entry's from the extension
                # every time it walks. A file created here and typed T_FILE
                # would list as a plain file where the real far end lists it
                # as a package - so a package copied ONTO the Link volume
                # would lose its icon and its double-click, and only in the
                # harness.
                typ = (self.T_PKG if name.upper().endswith('.O88')
                       else self.T_FILE)
                h = self.add(folder, name, typ=typ, content=data)
                return self.F_OK
            self.blobs[h] = bytes(data)
        n, t, _ = self.meta[h]
        self.meta[h] = (n, t, len(self.blobs[h]))
        return self.F_OK

    def remove(self, folder, name, want_dir):
        h = self.find(folder, name)
        if h is None:
            return self.F_NOENT
        isdir = self.meta[h][1] == self.T_DIR
        if isdir != want_dir:
            return self.F_PROT          # DELETE is not RMDIR and the far side
                                        # must not let one do the other's work
        if isdir and self.nodes[h][1]:
            return self.F_PROT          # ...and only the side holding the
                                        # directory can answer "is it empty"
        self.nodes[folder][1].remove(h)
        del self.nodes[h]
        del self.meta[h]
        self.blobs.pop(h, None)
        return self.F_OK

    def rmtree(self, folder, name):
        """NF_RMTREE: a folder and everything under it (SPEC.md 62.10.7).

        A FILE IS REFUSED, and that is not fussiness: `dskw_delete` is the
        verb for one, and a remover that quietly did another verb's job would
        turn a mis-typed name into a deletion nobody asked for. os88net.asm
        refuses one the same way, off the DTA's attribute byte.
        """
        h = self.find(folder, name)
        if h is None:
            return self.F_NOENT
        if self.meta[h][1] != self.T_DIR:
            return self.F_PROT
        doomed = [h]
        i = 0
        while i < len(doomed):                  # breadth first, no recursion:
            doomed.extend(self.nodes[doomed[i]][1])     # a stand-in that can
            i += 1                                      # blow its own stack is
        for d in reversed(doomed):                      # not a stand-in
            self.nodes[self.nodes[d][0]][1].remove(d)
            del self.nodes[d]
            del self.meta[d]
            self.blobs.pop(d, None)
        return self.F_OK

    def rename(self, folder, old, new):
        h = self.find(folder, old)
        if h is None:
            return self.F_NOENT
        if self.find(folder, new) is not None:
            return self.F_EXIST
        n, t, s = self.meta[h]
        self.meta[h] = (new, t, s)
        return self.F_OK

    def mkdir(self, folder, name):
        if folder not in self.nodes:
            return self.F_NOENT
        if self.find(folder, name) is not None:
            return self.F_EXIST
        self.add(folder, name, 0, self.T_DIR)
        return self.F_OK

    def parent(self, h):
        if h not in self.nodes:
            return None
        p = self.nodes[h][0]
        return 0 if p is None else p

    def entry(self, h):
        name, typ, size = self.meta[h]
        b = bytearray(DE_SIZE)
        b[0:len(name)] = name.encode('ascii')    # ...and the rest stays NUL
        b[16] = typ
        # @18 IS A HANDLE FOR A FOLDER AND A PACKAGE, AND ZERO FOR A PLAIN
        # FILE, which is os88net.asm's rule and not a convenience of this
        # file's. It handed one out for EVERYTHING, and that is precisely why
        # the field found `Disk error` on a package the harness had launched
        # a dozen times: the real far side listed packages with handle 0, the
        # loader reads a package BY handle, and the stand-in was the only
        # thing supplying a working one. A harness kinder than the machine it
        # stands in for hides exactly the bugs it exists to find.
        hand = h if typ in (self.T_DIR, self.T_PKG) else 0
        b[18] = hand & 0xFF
        b[19] = (hand >> 8) & 0xFF
        b[20] = size & 0xFF
        b[21] = (size >> 8) & 0xFF
        b[22] = (size >> 16) & 0xFF
        b[23] = (size >> 24) & 0xFF
        return bytes(b)

    def enum(self, h, n):
        """One child BY ORDINAL, for NF_ENUM - what a folder COPY walks with.

        THREE ANSWERS AND NOT TWO, which is the verb's whole contract
        (SPEC.md 62.10.6): the entry, `None` for past the last one, and
        `False` for a folder that cannot be walked at all. The master's
        end-of-folder answer is `CF=1, AX=0` - letter for letter what an
        absent verb gives - so a stand-in that reported an unknown handle as
        the end would let a folder copy report DONE over a subtree it never
        read, and the harness would be the only thing in the world that could
        not see it.

        The ordinal counts the entries a LISTING shows, so this walks the same
        children `list` does. os88net.asm gets that for free by sharing
        `srv_keep` between the two walks.
        """
        if h not in self.nodes:
            return False
        kids = self.nodes[h][1]
        if n >= len(kids):
            return None
        return self.entry(kids[n])

    def copy(self, src, dst, name):
        """NF_COPY: one file between two folders that are both ours.

        A FOLDER IS REFUSED, and that is not a limitation: the copy ENGINE
        walks a tree (it needs the replace question and the free-space check
        per file), and this verb is the leaf optimisation underneath it
        (SPEC.md 62.9.8). os88net.asm's srv_copy opens the source with
        int 21h 3Dh, which fails on a directory for the same reason.
        """
        if src not in self.nodes or dst not in self.nodes:
            return self.F_NOENT
        h = self.find(src, name)
        if h is None:
            return self.F_NOENT
        if self.meta[h][1] == self.T_DIR:
            return self.F_PROT
        return self.put(dst, name, self.data(h))

    def list(self, h):
        """This folder's children. NO '..' AND NO SORT - the kernel does both
        (SPEC.md 19.4/19.5), and a driver that helped would be a second
        opinion about a listing's order, which is how a display index stops
        meaning what the hit-test resolves."""
        if h not in self.nodes:
            return []
        return [self.entry(c) for c in self.nodes[h][1]]
