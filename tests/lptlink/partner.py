#!/usr/bin/env python3
"""The far end of the LapLink cable, played by the HOST (SPEC.md 62.10.3).

docs/NET-PLAN.md 9 assumed nothing here could be driven without two machines,
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
not in polls - docs/NET-PLAN.md 9.1's third defect, found by linksim.py and
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

# NO BLIND STEPPING AFTER THE PRESS, and that is the whole of what
# click_paused had to unlearn. Stepping a mouse packet's worth (60,000
# instructions) to "let the UART clear" runs the guest straight through the
# click dispatch and into net_connect, which sends NC_BYE and the magic into a
# wire nobody is watching - so the partner starts looking after the
# conversation has already begun and waits out its budget on nibbles that
# came and went. The partner's own 400-step loop clocks the UART perfectly
# well; it just has to be the thing doing the stepping.


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
        self._status(idle=True)

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
        self.send_nib(b & 0x0F)
        self.send_nib((b >> 4) & 0x0F)

    def recv(self, n):
        return bytes(self.recv_byte() for _ in range(n))

    def send(self, data):
        for b in bytes(data):
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
        """Press and release the left button on a PAUSED guest, deterministically.

        The caller positions the cursor while the machine runs - mo.to()
        proves where it is by reading guest memory, which needs cycles - then
        pauses, then calls this, then starts receiving.

        THE PRESS ALONE, and no release: ui_task dispatches a click on MDOWN,
        so the press is what runs net_connect, and anything sent after it is
        one more thing that can consume the cycles the handshake needs.
        """
        m = marty or self.m
        m.mouse(0, 0, l=True)

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

    def hello(self, version=1):
        """mst_hello's other half: hunt for 'O88?', answer 'O88!' + version."""
        self.hunt()
        self.send(MAGIC_R)
        self.send_byte(version)
        return MAGIC_Q

    # --- serving, once the link is up ----------------------------------------
    def serve(self, tree, limit=64, idle=600000):
        """Answer commands until the master says NC_BYE, or `limit` of them.

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
        """
        seen = []
        for _ in range(limit):
            mark = self.spent
            self.budget += idle          # ...only the command byte gets this
            try:
                c = self.recv_byte()
            except LinkTimeout:
                return seen              # nothing more to say: we are done
            finally:
                self.budget -= idle
            if self.spent - mark > idle:
                return seen
            seen.append(chr(c))
            if c == ord('X'):               # NC_BYE - back to listening
                continue
            if c == ord('I'):               # NC_INFO
                self.send_byte(0)           # status
                self.send_word(0)           # sectors: NONE. A redirected
                                            # volume has none at all
                self.send_byte(0)           # flags: bit 0 = read-only
            elif c == ord('L'):             # NF_LIST: handle -> count, entries
                h = self.recv_word()
                ents = tree.list(h)
                self.send_byte(0)
                self.send_word(len(ents))
                for e in ents:
                    self.send(e)
            elif c == ord('C'):             # NF_CHDIR: handle -> parent
                h = self.recv_word()
                par = tree.parent(h)
                if par is None:
                    self.send_byte(0x02)    # no such folder
                    continue
                self.send_byte(0)
                self.send_word(par)
            elif c == ord('F'):             # NF_DFREE: free bytes, granule
                self.send_byte(0)
                self.send_word(tree.free & 0xFFFF)
                self.send_word((tree.free >> 16) & 0xFFFF)
                self.send_word(tree.granule)
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
        self._next = 1

    T_FILE, T_PKG, T_DIR = 0, 1, 2      # OSAPI_FS_ENT's, and 3 is not ours

    def add(self, parent, name, size=0, typ=T_FILE):
        h = self._next
        self._next += 1
        self.nodes[parent][1].append(h)
        self.nodes[h] = (parent, [])
        self.meta[h] = (name, typ, size)
        return h

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
        b[18] = h & 0xFF
        b[19] = (h >> 8) & 0xFF
        b[20] = size & 0xFF
        b[21] = (size >> 8) & 0xFF
        b[22] = (size >> 16) & 0xFF
        b[23] = (size >> 24) & 0xFF
        return bytes(b)

    def list(self, h):
        """This folder's children. NO '..' AND NO SORT - the kernel does both
        (SPEC.md 19.4/19.5), and a driver that helped would be a second
        opinion about a listing's order, which is how a display index stops
        meaning what the hit-test resolves."""
        if h not in self.nodes:
            return []
        return [self.entry(c) for c in self.nodes[h][1]]
