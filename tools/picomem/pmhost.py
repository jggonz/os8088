"""pmhost.py - drive libpmhost (a PicoMEM running its own firmware) from Python.

docs/plans/completed/PICOMEM-EMU-PLAN.md wave 0. This is the ctypes face of
tools/picomem/shim/pmhost.h, and the instrument a host-side test uses to talk
to the card with no emulator at all: every port read, port write and memory
cycle goes to the firmware's own code, and card time moves only when this
module says so.

    card = pmhost.Card()                    # build/picomem/v1/libpmhost-v1.so
    card.boot()                             # power-on, run until READY
    card.cmd(0x73, 1)                       # AdLib on, as SOUND.DRV does

ONE CARD PER LIBRARY (pmhost.h): the firmware's state is global. Card()
therefore loads a PRIVATE COPY of the library, so every Card in a process is
its own power-on with its own globals - a test that needs four cards, as
tests/picomem.py's legs do, makes four. A card is never torn down: its
thread stays parked until the process exits.

    python3 tools/picomem/pmhost.py         # the self-check: boot, ramp, ROM,
                                            # the commands os8088 sends

GPL-2.0-or-later, like the library it loads.
"""
import ctypes
import itertools
import os
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.environ.get("OS88_PICOMEM_BUILD", os.path.join(ROOT, "build", "picomem"))

BASE = 0x2A0                       # the card's port block (picomem.cpp PM_IOBASEPORT)
ROM = 0xD0000                      # ROM_BIOS: 16 KB of BIOS, then 8 KB of shared RAM

# PM_Status values (pm_defines.h)
STAT_READY, STAT_CMDINPROGRESS, STAT_CMDERROR, STAT_CMDNOTFOUND, STAT_INIT, STAT_WAITCOM = range(6)

IRQ_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.c_int)
LOG_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_char_p)
NET_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_int)
DMA_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int)


class _Host(ctypes.Structure):
    _fields_ = [("ctx", ctypes.c_void_p), ("irq", IRQ_FN), ("log", LOG_FN),
                ("net_tx", NET_FN), ("dma_req", DMA_FN)]


class _Cfg(ctypes.Structure):
    _fields_ = [("irq", ctypes.c_int), ("sd_image", ctypes.c_char_p),
                ("irq_lines", ctypes.c_int), ("dma", ctypes.c_int)]


_serial = itertools.count()
_tmp = []


def _tmpdir():
    if not _tmp:
        _tmp.append(tempfile.TemporaryDirectory(prefix="pmhost-"))
    return _tmp[0].name


class CardError(Exception):
    pass


class Card:
    """One PicoMEM. irq_jumper is the 1.x's IRQ jumper; sd is a raw image of
    the microSD card or None for no card. On a 2.x (board="v2") irq_lines is
    the jumper mask, bit n = IRQ n (default IRQ 7 alone, as the field 2.x
    has it), and dma its DRQ channel."""

    def __init__(self, board="v1", irq_jumper=5, sd=None, lib=None,
                 irq_lines=None, dma=1):
        path = lib or os.path.join(BUILD, board, "libpmhost-%s.so" % board)
        if not os.path.exists(path):
            raise CardError("%s is not built: run tools/picomem/build.sh" % path)
        # dlopen shares one copy per PATH, so a private path is a private card
        copy = os.path.join(_tmpdir(), "pmhost-%d.so" % next(_serial))
        shutil.copyfile(path, copy)
        self.lib = ctypes.CDLL(copy)
        L = self.lib
        L.pm_create.argtypes = [ctypes.POINTER(_Cfg), ctypes.POINTER(_Host)]
        L.pm_advance.argtypes = [ctypes.c_uint64]
        L.pm_time_ns.restype = ctypes.c_uint64
        L.pm_io_read.argtypes = [ctypes.c_uint16, ctypes.POINTER(ctypes.c_uint8)]
        L.pm_io_write.argtypes = [ctypes.c_uint16, ctypes.c_uint8]
        L.pm_mem_read.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint8)]
        L.pm_mem_write.argtypes = [ctypes.c_uint32, ctypes.c_uint8]
        L.pm_mem_waits.argtypes = [ctypes.c_uint32]
        L.pm_peek_status.restype = ctypes.c_uint8
        L.pm_shared_ram.restype = ctypes.POINTER(ctypes.c_uint8)
        L.pm_net_rx.argtypes = [ctypes.c_char_p, ctypes.c_int]
        L.pm_dma_byte.argtypes = [ctypes.c_uint8, ctypes.c_int]
        self.board = L.pm_board()
        if irq_lines is None:
            irq_lines = 1 << 7 if self.board == 2 else 0
        self.drq = []                           # channels the card raised DRQ on
        self._dma_cb = DMA_FN(lambda ctx, ch: self.drq.append(ch))
        self.irq_lines = {}                     # line -> level, as the card last drove it
        self.irq_log = []                       # (card ns, line, level)
        self.log = []
        self._irq_cb = IRQ_FN(self._on_irq)     # kept alive: the library holds these
        self._log_cb = LOG_FN(self._on_log)
        self.sent = []                          # frames the card's NE2000 sent
        self._net_cb = NET_FN(self._on_net)
        self._host = _Host(None, self._irq_cb, self._log_cb, self._net_cb,
                           self._dma_cb)
        self._cfg = _Cfg(irq_jumper, sd.encode() if sd else None, irq_lines, dma)
        self.irq_jumper = irq_jumper
        if L.pm_create(ctypes.byref(self._cfg), ctypes.byref(self._host)) != 0:
            raise CardError("pm_create refused (one card per process)")

    def _on_irq(self, ctx, line, level):
        self.irq_lines[line] = level
        self.irq_log.append((self.lib.pm_time_ns(), line, level))


    def _on_net(self, ctx, frame, n):
        self.sent.append(bytes(frame[:n]))

    def net_rx(self, frame):
        """A frame off the network, into the card's NE2000."""
        self.lib.pm_net_rx(frame, len(frame))

    def _on_log(self, ctx, text):
        self.log.append(text.decode(errors="replace"))

    # ---- time ---------------------------------------------------------------
    def advance(self, us=1):
        self.lib.pm_advance(int(us * 1000))

    @property
    def now_us(self):
        return self.lib.pm_time_ns() / 1000.0

    # ---- the bus -------------------------------------------------------------
    def inb(self, port):
        """An I/O read. None if the card did not drive the bus."""
        v = ctypes.c_uint8()
        return v.value if self.lib.pm_io_read(port, ctypes.byref(v)) else None

    def outb(self, port, value):
        return bool(self.lib.pm_io_write(port, value & 0xFF))

    def peek(self, addr):
        v = ctypes.c_uint8()
        return v.value if self.lib.pm_mem_read(addr, ctypes.byref(v)) else None

    def poke(self, addr, value):
        return bool(self.lib.pm_mem_write(addr, value & 0xFF))

    def peek_block(self, addr, n):
        return bytes(self.peek(addr + i) or 0 for i in range(n))

    @property
    def status(self):
        return self.lib.pm_peek_status()

    # ---- the card's protocol, as a PC driver speaks it ----------------------
    def boot(self, budget_us=5_000_000, step_us=100):
        """Power-on to READY, as the BIOS waits for it (pmbios.asm spins on
        status 4). Returns the card time it took."""
        t0 = self.now_us
        # ...and until the card answers at its base at all: a 2.x's status
        # byte reads READY before its init has installed the port
        while self.status == STAT_INIT or self.inb(BASE) is None:
            if self.now_us - t0 > budget_us:
                raise CardError("card still INIT after %d us" % budget_us)
            self.advance(step_us)
        if self.status != STAT_READY:
            raise CardError("card came up with status %d" % self.status)
        return self.now_us - t0

    def detect_irq(self, budget_us=100_000, step_us=50):
        """What the PicoMEM BIOS does at POST to learn the card's line: hook
        the candidate IRQs, send CMD_SendIRQ (30h), and from the handler that
        fires store the line in shared RAM byte 11 (BV_IRQ, pm_defines.h).
        Nothing else tells the firmware its jumper, and until BV_IRQ is set
        it refuses the Sound Blaster with CMDERR_NOIRQ (13h). Returns the
        line the card raised, or None."""
        if self.wait_ready() != STAT_READY:
            raise CardError("card not READY for CMD_SendIRQ")
        self.outb(BASE, 0x30)
        line, t0, fired = None, self.now_us, 0
        while self.inb(BASE) == STAT_CMDINPROGRESS:
            for l, lv in self.irq_lines.items():
                if lv and not fired >> l & 1:
                    fired |= 1 << l
                    if line is None or self.board == 2:
                        line = l                 # the "handler": BV_IRQ = n
                        self.poke(ROM + 0x4000 + 11, l)
            if self.now_us - t0 > budget_us:
                raise CardError("CMD_SendIRQ still running after %d us" % budget_us)
            self.advance(step_us)
        if self.board == 2:                      # the 2.x's: every line that
            self.poke(ROM + 0x4000 + 12, fired)  # fired, BV_HWIRQ (isa_irq.cpp)
        return line

    def dma_byte(self, v, tc=False):
        """The 8237's cycle for one DRQ the 2.x raised: True if it was asked."""
        return bool(self.lib.pm_dma_byte(v, 1 if tc else 0))

    def dsp_at(self, bases=(0x220, 0x240, 0x230, 0x250, 0x260)):
        """The base whose Sound Blaster DSP answers a reset with AAh, or None:
        the check SOUND.DRV's sbl_f_probe makes, against the firmware's DSP."""
        for b in bases:
            self.outb(b + 6, 1)
            self.advance(10)
            self.outb(b + 6, 0)
            for _ in range(100):
                self.advance(10)
                st = self.inb(b + 0xE)
                if st is not None and st & 0x80:
                    if self.inb(b + 0xA) == 0xAA:
                        return b
                    break
        return None

    def wait_ready(self, budget_us=1_000_000, step_us=10):
        t0 = self.now_us
        while True:
            s = self.inb(BASE)
            if s != STAT_CMDINPROGRESS:
                return s
            if self.now_us - t0 > budget_us:
                raise CardError("command still in progress after %d us" % budget_us)
            self.advance(step_us)

    def cmd(self, command, arg=0, budget_us=1_000_000):
        """One command the way drivers/sound/picomem.inc sends it: wait READY,
        argument low then high as two byte writes, the command, wait READY,
        the answer low then high. Returns (status, answer)."""
        s = self.wait_ready(budget_us)
        if s != STAT_READY:
            raise CardError("status %d before command %02Xh" % (s, command))
        self.outb(BASE + 1, arg & 0xFF)
        self.outb(BASE + 2, arg >> 8)
        self.outb(BASE, command)
        s = self.wait_ready(budget_us)
        return s, self.inb(BASE + 1) | self.inb(BASE + 2) << 8


def _selfcheck():
    card = Card()
    took = card.boot()
    print("boot: READY after %.1f ms of card time" % (took / 1000))

    ramp = [card.inb(BASE + 3) for _ in range(100)]
    ok = all(((b - a) & 0xFF) == 1 for a, b in zip(ramp, ramp[1:]))
    print("2A3h ramp: %s (%s...)" % ("counts" if ok else "BROKEN", ramp[:4]))
    if not ok:
        return 1

    sig = card.peek_block(ROM, 3)
    print("ROM at D000:0: %s, %d x 512 bytes" % (sig[:2].hex(), sig[2]))
    if sig[:2] != b"\x55\xaa":
        return 1
    shared = card.peek(ROM + 0x4000)
    print("shared RAM magic at D400:0: %02Xh" % shared)
    if shared != 0x12:
        return 1

    line = card.detect_irq()
    print("IRQ detect (CMD 30h): the card raised IRQ %s; BV_IRQ = %d" % (line, card.peek(ROM + 0x400B)))
    if line != card.irq_jumper:
        return 1

    for name, c, a in (("AdLib on", 0x73, 1), ("SB IRQ 7 / DMA 1", 0x77, 7 | 1 << 8),
                       ("SB on at 220h", 0x78, 0x220)):
        s, ans = card.cmd(c, a)
        print("cmd %02Xh %-18s status %d answer %04Xh" % (c, name, s, ans))
    print("AdLib status port 388h reads %s" % card.inb(0x388))
    at = card.dsp_at()
    print("Sound Blaster DSP answers a reset at %s" % ("%03Xh" % at if at else "no port"))
    if at != 0x220:
        return 1
    s, ans = card.cmd(0x77, card.irq_jumper | 1 << 8)
    print("cmd 77h SB on the card's OWN line: answer %04Xh (refused: %s)" % (ans, ans != 0))
    if ans == 0:
        return 1
    two = Card(irq_jumper=7)
    two.boot()
    print("a second card in the same process: READY, IRQ %s" % two.detect_irq())
    return 0


if __name__ == "__main__":
    sys.exit(_selfcheck())
