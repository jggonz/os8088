#!/usr/bin/env python3
"""kern_mega under Bochs, behind MartyPC's `Marty` API - so a row written for
the 8088 runs on the protected-mode kernel unchanged (docs/BOCHS.md, "Rows
ported from MartyPC").

    OS88_MEGA=1 python3 tests/<row>.py

`os88marty.launch()` answers a `MegaBochs` instead of a MartyPC connection
when $OS88_MEGA is set, and everything above it - os88ui, os88mouse, every
`os88marty.until`/`ui_done`/`settle` - runs on top of it as it stands,
because the shim is a SERVER rather than a wrapper: `Marty` is built on one
method, `cmd(**kw)`, the debug server's request/reply, and this class
answers those requests itself out of a Bochs (tools/os88bochs.py's Box) and
its debugger. So `type_text`, `ctrl`, `readseg`, `bp_exec`, `go`,
`wait_stop` and `advance` are MartyPC's own code over this transport.

WHAT IT MAPS, AND WHAT IT CANNOT
  * the disks: any system floppy (os8088*.img, small*.img) becomes
    build/mega.img and any shipped apps floppy (apps*.img) build/megaapps.img
    - kern_mega has one system disk and one apps disk, at 1.44MB. Any other
    image (a category disk, a scratch disk a row built) goes in B: as it is.
  * the machine: always Bochs's 386, a VGA, a PS/2 mouse, 16MB. A machine
    name with `_sb` in it gets the SB16, one with `_hdd` a fixed disk
    (os88bochs.Box's C:, an ATA disk with RUN/ on it), one with `_ne2k` (or
    $OS88_BOCHS_NE2K) an NE2000 on slirp, and $OS88_BOCHS_MOUSE=serial
    swaps the PS/2 mouse for a serial one on COM1 (what kern_dos's INT 33h
    serves). An adapter, a ROM or a clock card in
    the name is ignored - a row whose subject is one of those is not a row to
    port.
  * the clock: Bochs runs `clock: sync=slowdown` (os88bochs.SYNC), so its
    time is its instruction count and the host only caps it at real time.
    `status()["cycles"]` is that GUEST time at 4.77MHz, read off `ptime`
    (_cycles), so every guest budget in os88marty means the same amount of
    the machine's own time here as on MartyPC, on any box. Nothing measured
    in cycles is a measurement of an 8088. A stop costs the guest nothing
    (os88bochs.bxclock, the time() preload), and os88marty.pace and
    `advance(cycles=)` end on Bochs's time breakpoint (_run_exact), so a
    wait is the same amount of guest on a loaded box as on an idle one.
  * memory: `read`/`write` are LINEAR (paging is off). `readseg(seg, off)`
    takes a REAL segment or a SELECTOR - under kern_mega every package
    region and heap claim is an LDT selector, and the GDT is an identity
    map except the kernel's data (0010) and COLD_SEG (`.cold` above 1MB),
    so its base is looked up in the descriptor. A row that does `seg << 4`
    itself has to call `m.lin(seg, off)` instead.
  * the glass: `fbuf()` is a screenshot of the Bochs window's guest area,
    which lags the framebuffer by one VGA update (`vga: update_freq`, raised
    to 20 here). `screen()` reads a text mode's cells out of B8000.
  * breakpoints: `exec`/`execseg` are Bochs `lb` (linear); `int` is an `lb`
    on the vector's handler (the IDT's gate in protected mode, so a CPU
    exception such as #DE). `mem` and `io` are refused - Bochs has
    watchpoints but not MartyPC's semantics.
  * `park` sets CS:IP but CANNOT clear IF (Bochs's `set` has no EFLAGS):
    code parked on starts with `cli` if it needs interrupts off. And a
    debugger write to code that has ALREADY RUN is not seen - Bochs's trace
    cache keeps the old bytes - so a row patches code before it executes.
  * the mouse: PS/2; `launch(..., wheel=True)` makes it an IntelliMouse
    and `m.wheel()` works; `launch(..., noserial=True)` removes COM1.
    X key autorepeat is off on the display (a held key is one make).
  * `disks()` names the image AS MOUNTED (copied before the first `c`), so
    os88flush's diff has a reference; `flush` copies the live file.
  * a row's OWN fixed disk - `--mount hd:0:PATH` in `extra`, or a staged
    `run_dir`'s VHD on an `_hdd` machine - is mounted in place and written
    through (row_disk); `inb`/`outb`/`planes` run as guest stubs (_stub).
  * refused outright, by name: `flicker`, `pace`,
    `snapshot`/`restore`. `reset` is a POWER CYCLE (_c_reset). A row
    built on one of those is not portable and says so in its first second.
"""
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import os88bochs as ob         # noqa: E402
import os88marty as om         # noqa: E402
# what does not depend on Bochs - the disk mapping, a row's own fixed disk,
# selectors, `.cold`/`.mega` symbols and the boot toast - is shared with
# the 86Box backend (tools/os88mega86box.py) and lives there
from os88megacommon import (MegaMixin, mega_images,  # noqa: E402,F401
                            hd_geometry, row_disk, _blank)

GUEST_HZ = om.GUEST_HZ
# $OS88_BOCHS_EXACT=0: waits end on the polled clock again, not on Bochs's
# time breakpoint (MegaBochs._run_exact) - the A/B
EXACT = os.environ.get("OS88_BOCHS_EXACT", "1") != "0"
_WIDEN = bytes(v | v >> 6 for v in range(256))

# W3C KeyboardEvent.code (MartyKey's names) -> X keysym
_KEYS = {
    "Enter": "Return", "Escape": "Escape", "Backspace": "BackSpace",
    "Tab": "Tab", "Space": "space", "ArrowUp": "Up", "ArrowDown": "Down",
    "ArrowLeft": "Left", "ArrowRight": "Right", "Home": "Home", "End": "End",
    "PageUp": "Prior", "PageDown": "Next", "Insert": "Insert",
    "Delete": "Delete", "ShiftLeft": "Shift_L", "ShiftRight": "Shift_R",
    "ControlLeft": "Control_L", "ControlRight": "Control_R",
    "AltLeft": "Alt_L", "AltRight": "Alt_R", "CapsLock": "Caps_Lock",
    "NumLock": "Num_Lock", "ScrollLock": "Scroll_Lock",
    "Minus": "minus", "Equal": "equal", "BracketLeft": "bracketleft",
    "BracketRight": "bracketright", "Backslash": "backslash",
    "Semicolon": "semicolon", "Quote": "apostrophe", "Backquote": "grave",
    "Comma": "comma", "Period": "period", "Slash": "slash",
    "NumpadEnter": "KP_Enter", "NumpadAdd": "KP_Add",
    "NumpadSubtract": "KP_Subtract", "NumpadMultiply": "KP_Multiply",
    "NumpadDivide": "KP_Divide", "NumpadDecimal": "KP_Decimal",
    "PrintScreen": "Print", "Pause": "Pause",
}


def keysym(name):
    if name in _KEYS:
        return _KEYS[name]
    if name.startswith("Key") and len(name) == 4:
        return name[3].lower()
    if name.startswith("Digit") and len(name) == 6:
        return name[5]
    if name.startswith("Numpad") and name[6:].isdigit():
        return "KP_" + name[6:]
    if re.match(r"F\d{1,2}$", name):
        return name
    raise om.MartyError("os88megabochs: no X keysym for MartyKey %r" % name)


# --- the machine ------------------------------------------------------------

class MegaBochs(MegaMixin, om.Marty):
    """`Marty` with the debug server replaced by Bochs: see the module note.
    `m.box` is the os88bochs.Box underneath, for anything only Bochs has."""

    bochs = True

    def __init__(self, box, run_dir, label=None):
        # NOT Marty.__init__: there is no socket. Everything Marty's methods
        # touch is set here.
        self.box = box
        self.run_dir = run_dir
        self.addr = "bochs%s" % box.display
        self.port = None
        self.pid = box.p.pid
        self._proc = box.p
        self._rec = None
        self._lock = threading.RLock()
        self._log = []
        self._go = None
        self._pumping = 0
        self._planes_verb = True    # `planes` is _c_planes
        self._pipeline = False
        self.label = label
        # the machine's state, kept HERE: Bochs answers nothing between stops
        self._state = "running"
        self._stops = 0
        self._cyc = 0.0             # guest seconds run before _since
        self._since = time.time()   # running since (None while stopped)
        self._gt = 0                # Bochs's `ptime` at the last reading,
        self._gt0 = 0               # ...at cycle 0 (launch(boot=False)),
        self._gt_at = 0.0           # ...taken at this host time, and
        self._gt_dirty = True       # ...the guest has run since it
        self._gt_cost = 0.0         # host seconds the last clock stop took
        self._bps = {}              # bochs bp number -> linear
        self._bpmark = os.path.getsize(box.e9)   # e9 offset of the last resume
        self._at = (0, 0, 0)        # cs, ip, flat ip of the last stop
        self._btn = set()
        self._keys = set()          # keysyms a row holds down (_c_key)
        self._ldt = None
        self._tmp = tempfile.mkdtemp(prefix="bxm", dir=run_dir)
        self._closed = False
        self._wav = os.environ.get("MARTYPC_WAV")    # see close()
        # THE MOUNTED-FROM IMAGES, as MartyPC's `disks` names them: Bochs
        # writes a.img/b.img through as the guest writes, so the live file
        # is no reference for "what did the guest change" (os88flush.diff,
        # tests/cfgtrip.py read 0 sectors changed after a SYSTEM.CFG write).
        # Copied before the first `c`, when Bochs has written nothing
        self._src = {}
        for d, n in ((0, "a"), (1, "b")):
            live = os.path.join(box.out, n + ".img")
            if os.path.exists(live):
                self._src[d] = os.path.join(run_dir, n + "-mounted.img")
                shutil.copy(live, self._src[d])

    # ---- the clock --------------------------------------------------------
    # THE GUEST'S OWN CLOCK, NOT THE HOST'S (docs/BOCHS.md, *The clock*).
    # Under `clock: sync=slowdown` (os88bochs.SYNC, the default) Bochs's time
    # IS its instruction count - `ptime` over `cpu: ips=` is guest seconds to
    # the instruction, HLT included - and the host only CAPS it at real time.
    # So the clock here is read OFF the guest: a stop this module makes for a
    # read carries a `ptime` with it for nothing (`_stopped`), and a status
    # poll on a running guest whose last reading is older than GT_STALE host
    # seconds takes one. Between readings it does NOT extrapolate: a reading
    # can only lag the guest, never lead it, so no budget spent against it
    # can run out while the guest made no progress - which is the failure
    # the host-time clock had under load (a descheduled Bochs was charged
    # every second it did not run). OS88_BOCHS_SYNC=realtime puts the old
    # clock back for an A/B: host seconds the guest ran, nothing else.
    GT_STALE = float(os.environ.get("OS88_BOCHS_GTSTALE", "0.05"))
    GT_MAXAGE = 0.5

    def _cycles(self, fresh=False, stale=None):
        if ob.SYNC == "realtime":
            run = self._cyc + ((time.time() - self._since)
                               if self._since is not None else 0.0)
            return int(run * GUEST_HZ)
        if fresh and not self._closed and self._proc.poll() is None:
            try:
                if self._state == "running":
                    # ...AND NO OFTENER THAN THE STOPS LEAVE IT RUNNING ~80%
                    # of the time: a stop is ~20 ms idle and ten times that
                    # on a loaded box, and a poll loop reading the clock
                    # every GT_STALE then kept the guest at its prompt -
                    # mega-trkcompact's pause ran at a tenth of real time
                    age = time.time() - self._gt_at
                    # - but at least every GT_MAXAGE, or os88marty's stall
                    # arm (GUEST_STALL, 2 host s) reads a still clock as a
                    # stopped machine
                    if age > min(max(self.GT_STALE if stale is None
                                     else stale, 4 * self._gt_cost),
                                 self.GT_MAXAGE):
                        t0 = time.time()
                        self._stopped([])       # halts, reads ptime, resumes
                        self._gt_cost = time.time() - t0
                elif self._gt_dirty:
                    self._gt_take(self._do(["ptime"]))
            except om.MartyError:
                pass                # a wedged machine: the last reading
        return int(max(0, self._gt - self._gt0) * GUEST_HZ / ob.IPS_N)

    def _clock(self):
        """MegaMixin's guest seconds: the clock above"""
        if ob.SYNC == "realtime":
            return time.time()
        return self._cycles(fresh=True) / GUEST_HZ

    def _gt_take(self, text):
        """A `ptime` answer in TEXT, if there is one, onto the clock."""
        got = re.findall(r"ptime: (\d+)", text or "")
        if got:
            self._gt = int(got[-1])
            self._gt_at = time.time()
            self._gt_dirty = self._state == "running"

    # ---- the debugger channel -------------------------------------------
    def _e9(self, start):
        return ob._tail(self.box.e9, start)

    def _bp_hit(self):
        """A stop Bochs made by ITSELF since the last resume: a breakpoint.
        Read off the transcript, so asking costs no stop."""
        if self._state != "running" or not self._bps:
            return False
        t = self._e9(self._bpmark)
        if not ob._PROMPT.search(t):
            return False
        self._stopped_now("breakpoint", t)
        return True

    def _stopped_now(self, why, text=""):
        if self._since is not None:
            self._cyc += time.time() - self._since
            self._since = None
        self._state = why
        self._stops += 1
        m = None
        for m in re.finditer(r"\[0x([0-9a-f]+)\] ([0-9a-f]{4}):([0-9a-f]+)",
                             text):
            pass
        if m:
            self._at = (int(m.group(2), 16), int(m.group(3), 16) & 0xFFFF,
                        int(m.group(1), 16))
        if self.box.captured:
            self.box.inp.regrab = True

    def _halt(self):
        """Stop the guest at the debugger's prompt (no-op if stopped). The
        transcript is read from the last RESUME, not from the signal: a
        breakpoint that fires between the check and the SIGINT is a prompt
        already printed, and the signal is then lost at it - so the stop is
        whichever prompt comes first, and a `Breakpoint` line says which."""
        if self._state != "running" or self._bp_hit():
            return
        # NOT WHILE BOCHS HAS INPUT UNREAD: the stop releases the grab, and
        # an event read after it is dropped (os88bochs._Input.taken). A
        # breakpoint reached meanwhile is a stop already, and is this one
        self.box.inp.taken(stopped=self._bp_hit)
        if self._state != "running" or self._bp_hit():
            return                      # (_bp_hit records the stop itself)
        # A SIGINT CAN BE LOST, and the kick is what finds it: Bochs echoes
        # the `c` as readline reads it, and bx_dbg_continue_command then
        # CLEARS bx_guard.interrupt_requested before it runs (dbg_main.cc),
        # so a signal landing between the two is erased. A stop made just
        # after a resume - every exact pace (_run_exact) makes one - lands
        # in that window on a busy box, and the kick at 1 s it had was a
        # host second of RUNNING guest: a whole guest second spent in one
        # look of mega-vidfskeysvga4, 1.032 s in the trace, so 7 looks where
        # 20 fit. A SIGINT at the prompt is harmless (that same `c` clears
        # it), so the kick comes every KICK host seconds
        end, kick = time.time() + 20, time.time() + self.KICK
        kicks = 1
        os.kill(self.box.p.pid, signal.SIGINT)
        nap = 0.002
        while True:
            t = self._e9(self._bpmark)
            if ob._PROMPT.search(t) and t.rstrip(" ").endswith(">"):
                break
            if time.time() > kick:
                os.kill(self.box.p.pid, signal.SIGINT)
                kicks += 1
                kick = time.time() + self.KICK
            if time.time() > end:
                raise om.MartyError("os88megabochs: no debugger prompt in 20s"
                                    " - Bochs on %s is gone or wedged"
                                    % self.box.display)
            time.sleep(nap)
            nap = min(nap * 2, 0.02)
        if kicks > 1:
            # ...and a kick that reached the PROMPT ends its read as an
            # empty line, which prints a prompt more: let it land before
            # anything counts prompts (_do measures from after it)
            time.sleep(0.03)
            t = self._e9(self._bpmark)
        self._stopped_now("breakpoint" if re.search(r"Breakpoint \d+,", t)
                          else "paused", t)

    def _do(self, cmds, timeout=30.0):
        """Run debugger commands on a STOPPED guest; their transcript."""
        if not cmds:
            return ""
        e9 = self.box.e9
        n = os.path.getsize(e9)
        with open(os.path.join(self.box.out, "dbg.fifo"), "w") as fh:
            fh.write("\n".join(cmds) + "\n")
        end, nap = time.time() + timeout, 0.002
        while True:
            t = self._e9(n)
            if (len(ob._PROMPT.findall(t)) >= len(cmds)
                    and t.rstrip(" ").endswith(">")):
                return t
            if time.time() > end:
                raise om.MartyError("os88megabochs: %r had no answer in %.0fs"
                                    % (cmds[0], timeout))
            time.sleep(nap)
            nap = min(nap * 2, 0.02)

    def _resume(self):
        if self._state == "running":
            return
        e9 = self.box.e9
        n = os.path.getsize(e9)
        with open(os.path.join(self.box.out, "dbg.fifo"), "w") as fh:
            fh.write("c\n")
        end = time.time() + 10
        mark = None
        while time.time() < end:        # until Bochs has READ it (its echo):
            i = self._e9(max(0, n - 4)).find("c\n")   # a SIGINT before that
            if i >= 0:                                 # is lost at the prompt
                # ...and the mark is the END OF THE ECHO, not the file's size
                # when this loop got round to looking: a breakpoint a few
                # instructions on has its prompt printed by then, and a mark
                # past it is a stop nobody ever sees (wait_stop runs out)
                mark = max(0, n - 4) + i + 2
                break
            time.sleep(0.002)
        # FROM BEFORE THE `c`, not after its echo: a breakpoint at the next
        # few instructions (a loop back onto the one just stepped off) has
        # printed its stop and its prompt by the time the echo is seen, and a
        # mark taken then reads an empty transcript - `_bp_hit` says no, and
        # `_halt`'s SIGINTs are lost at the prompt for 20s (bptrace)
        self._bpmark = n
        self._state = "running"
        self._since = time.time()
        self._gt_dirty = True
        q = self.__dict__.pop("_kq", None)
        for a in q or ():               # _c_key's keys typed while stopped,
            self._xd(*a)                # one GUI pass apart
            time.sleep(ob.INPUT_GAP)

    def _stopped(self, cmds, timeout=30.0):
        """CMDS with the guest stopped for them, left as it was found."""
        with self._lock:
            if self._state == "running" and not self._bp_hit():
                self._halt()
                if self._state == "paused":
                    try:
                        # ...and THE CLOCK RIDES ALONG: one `ptime` more in
                        # the batch reads the guest's time for nothing
                        t = self._do(list(cmds) + ["ptime"], timeout)
                        self._gt_take(t)
                        return t
                    finally:
                        self._resume()
                        # a stop made only for a read is not a stop a
                        # caller waits on: take it back out of the count
                        self._stops -= 1
            return self._do(cmds, timeout)

    # ---- memory -----------------------------------------------------------
    def _read(self, addr, n):
        if n <= 64:
            got = ob.xp_bytes(self._stopped(["xp /%dbx 0x%x" % (n, addr)]))
            b = got.get(addr, b"")
        else:
            f = os.path.join(self._tmp, "r.bin")
            if os.path.exists(f):
                os.remove(f)
            self._stopped(['writemem "%s" 0x%x %d' % (f, addr, n)])
            with open(f, "rb") as fh:
                b = fh.read()
        if len(b) < n:
            raise om.MartyError("os88megabochs: read 0x%x came back %d of %d"
                                " bytes" % (addr, len(b), n))
        return b[:n]

    def _write(self, addr, data):
        cmds, i = [], 0
        while i < len(data):
            k = 4 if len(data) - i >= 4 else 1
            v = int.from_bytes(data[i:i + k], "little")
            cmds.append("setpmem 0x%x %d 0x%x" % (addr + i, k, v))
            i += k
        for j in range(0, len(cmds), 64):
            self._stopped(cmds[j:j + 64])
        return len(data)

    def ldt_base(self):
        if self._ldt is None:
            t = self._stopped(["sreg"])
            m = re.search(r"ldtr:0x[0-9a-f]+, dh=(0x[0-9a-f]+), "
                          r"dl=(0x[0-9a-f]+)", t)
            if not m:
                raise om.MartyError("os88megabochs: no LDTR in `sreg`")
            dh, dl = int(m.group(1), 16), int(m.group(2), 16)
            self._ldt = ((dl >> 16) | ((dh & 0xFF) << 16) | (dh & 0xFF000000),
                         (dl & 0xFFFF) | (dh & 0xF0000))
        return self._ldt

    def gdt_base(self):
        if getattr(self, "_gdt", None) is None:
            t = self._stopped(["sreg"])
            m = re.search(r"gdtr:base=0x([0-9a-f]+), limit=0x([0-9a-f]+)", t)
            if not m:
                raise om.MartyError("os88megabochs: no GDTR in `sreg`")
            g = (int(m.group(1), 16), int(m.group(2), 16))
            if g[1] < 0x100:            # not in protected mode yet (the
                return g                # boot): not worth keeping
            self._gdt = g
        return self._gdt

    def gdt(self, sel):
        """(base, limit) of GDT selector SEL, out of the live GDTR."""
        g = getattr(self, "_gdt", None)
        if g is None or not g[0]:
            t = self._stopped(["sreg"])
            m_ = re.search(r"gdtr:base=0x([0-9a-f]+), limit=0x([0-9a-f]+)", t)
            if not m_:
                raise om.MartyError("os88megabochs: no GDTR in `sreg`")
            self._gdt = g = (int(m_.group(1), 16), int(m_.group(2), 16))
        if (sel & ~7) + 7 > g[1]:
            raise om.MartyError("os88megabochs: GDT selector %#x is past "
                                "the table's limit %#x" % (sel, g[1]))
        d = self._read(g[0] + (sel & ~7), 8)
        return (d[2] | d[3] << 8 | d[4] << 16 | d[7] << 24,
                d[0] | d[1] << 8 | (d[6] & 0x0F) << 16)

    def protected(self):
        """Is the CPU in protected mode NOW (CR0.PE)? kern_mega enters it
        late in kmain, so a row stopped earlier is still in real mode."""
        t = self._stopped(["creg"])
        m_ = re.search(r"CR0=0x([0-9a-f]+)", t)
        return bool(m_ and int(m_.group(1), 16) & 1)

    def _vector(self, v):
        """The linear address vector V's handler starts at: an `int`
        breakpoint, as an `lb` there. In protected mode it is the IDT's
        gate - so under kern_mega a CPU EXCEPTION (0-0Fh: #DE is 00h)
        stops at mega_xs's stub for it, with the interrupt frame on top
        of the stack as MartyPC's `int` stop has it (tests/int0sweep.py);
        a hardware IRQ's vector is the IDT's, not the real-mode one. In
        real mode, the IVT's."""
        t = self._stopped(["sreg"])
        m = re.search(r"idtr:base=0x([0-9a-f]+), limit=0x([0-9a-f]+)", t)
        base, limit = (int(m.group(1), 16), int(m.group(2), 16)) if m \
            else (0, 0x3FF)
        if limit >= 0x400 or base:              # an IDT: 8-byte gates
            g = self._read(base + v * 8, 8)
            off = g[0] | g[1] << 8 | (g[6] | g[7] << 8) << 16 \
                if g[5] & 0x08 else g[0] | g[1] << 8    # a 386 gate's
            return self.lin(g[2] | g[3] << 8, off)
        g = self._read(v * 4, 4)
        return ((g[2] | g[3] << 8) << 4) + (g[0] | g[1] << 8)

    # ---- the glass --------------------------------------------------------
    def _window(self):
        w = getattr(self, "_win", None)
        if w is None:
            r = subprocess.run(["xdotool", "search", "--name", "Bochs"],
                               env=self.box.env, capture_output=True,
                               text=True).stdout.split()
            if not r:
                raise om.MartyError("os88megabochs: no Bochs window")
            self._win = w = r[0]
        return w

    def _fbuf(self):
        """(w, h, rgb24) of the guest's display, out of the Bochs window."""
        # GLASS only for a RUNNING guest, whose next updates are what it
        # waits for. A STOPPED one paints nothing until it resumes, so the
        # sleep bought no pixel - and under sync=slowdown every host second
        # stopped is paid back as a guest SPRINT on the resume (the clock
        # catches up), so a row looking at a paused machine N times handed
        # the guest N x GLASS of extra time: mega-vidfskeysvga4 saw a toast
        # ~25 frames long 4 times, ~7 frames a look, and failed `looks < 8`
        if self._state == "running":
            time.sleep(ob.GLASS)
        raw = subprocess.run(["xwd", "-silent", "-id", self._window()],
                             env=self.box.env, capture_output=True,
                             check=True).stdout
        hsz = int.from_bytes(raw[0:4], "big")
        W, H = (int.from_bytes(raw[16:20], "big"),
                int.from_bytes(raw[20:24], "big"))
        bpl = int.from_bytes(raw[48:52], "big")
        ncol = int.from_bytes(raw[76:80], "big")
        px = raw[hsz + ncol * 12:]
        # THE GUEST AREA: `xwd -id` takes the window with its 4-pixel X
        # border, and Bochs's x GUI draws a 32-pixel headerbar above the
        # guest's display and an 18-pixel status bar under it (measured on
        # this build: a 640x480 desktop is a 648x538 dump, white from row 36
        # to row 515). The display is exactly as wide as the window.
        bd, top, bot = 4, 32, 18
        w, h = W - 2 * bd, H - 2 * bd - top - bot
        out = bytearray(w * h * 3)
        for y in range(h):
            o0 = (bd + top + y) * bpl + bd * 4
            row = px[o0:o0 + w * 4]
            o = y * w * 3
            out[o:o + w * 3:3] = row[2::4]
            out[o + 1:o + w * 3:3] = row[1::4]
            out[o + 2:o + w * 3:3] = row[0::4]
        # THE DAC'S SIX BITS, widened the way a VGA's are read: Bochs shifts
        # them left two (white is FC), MartyPC and every palette in tests/
        # fill the low bits (FF, AA, 55) - so v | v >> 6
        return w, h, bytes(out).translate(_WIDEN)

    # ---- the server -------------------------------------------------------
    def cmd(self, **kw):
        c = kw.pop("cmd")
        f = getattr(self, "_c_" + c, None)
        if f is None:
            raise om.MartyError(
                "`%s` is a MartyPC instrument the kern_mega Bochs shim does not"
                " have (tools/os88megabochs.py) - this row is not portable "
                "as it stands" % c)
        with self._lock:
            r = f(**kw)
        r = dict(r or {})
        r["ok"] = True
        return r

    def _status(self):
        self._bp_hit()
        cs, ip, flat = self._at
        return {"state": self._state, "cycles": self._cycles(fresh=True),
                "stops": self._stops, "cs": cs, "ip": ip, "flat_ip": flat}

    def _c_ping(self, **_):
        return {"pipeline": False}

    def _c_status(self, **_):
        return self._status()

    def _c_read(self, addr, len, **_):          # noqa: A002
        return {"data": self._read(addr, len).hex()}

    def _c_write(self, addr, data, **_):
        return {"written": self._write(addr, bytes.fromhex(data))}

    def _c_run(self, **_):
        self._bp_hit()
        frm = self._stops
        self._resume()
        st = self._status()
        st["resumed_from"] = frm
        return st

    def _c_pause(self, **_):
        self._halt()
        return self._status()

    def _c_step(self, n=1, over=False, **_):
        self._halt()
        t = self._do(["%s %d" % ("n" if over else "s", n)])
        self._stopped_now("paused", t)
        return self._status()

    KICK = 0.05                # s: _halt's SIGINT again, until a prompt
    TB_SENTINEL = 10 ** 15     # ticks: eight years of guest at 4M

    def _run_exact(self, secs, count=False):
        """Run the guest EXACTLY `secs` guest seconds and leave it STOPPED:
        Bochs's own time breakpoint (`sb N`, N instructions on - a
        pc_system timer, so it counts through HLT), armed on a stopped
        guest, then `c` and the prompt it stops at. The guest seconds it
        ran (1 instruction's worth at the least, so truthy); None
        when it cannot be used, and the caller polls the clock instead:
        under `realtime`, inside a bp_trace pump, or with a breakpoint of
        the row's own armed - a time breakpoint cannot be deleted, so one
        a row's breakpoint got ahead of would stop the guest later, at a
        moment nobody asked for.

        WHY: the clock otherwise reads the guest only every 50-500 host
        ms (_cycles), and a guest capped at real time runs that whole lag
        - so a short wait handed out what the HOST took to look, a tenth
        of a guest second idle and half of one on a loaded box, where
        mega-vidfskeysvga4's pace(0.01) per look fitted 4 looks into a
        toast that fits 15. With the stop costing the guest nothing
        (os88bochs.bxclock), a wait ended this way is the same on any
        box."""
        if not EXACT or ob.SYNC == "realtime" or self._pumping or \
                self._bps or \
                self._closed or self._proc.poll() is not None:
            return None
        n = int(round(secs * ob.IPS_N))
        if n < 1:
            return 1.0 / ob.IPS_N
        with self._lock:
            if self._bps:
                return None
            if self._state == "running":
                self._halt()
                if self._state != "paused":
                    return None         # a breakpoint got there first
                self._stops -= 1
            # BOCHS 2.7'S TIME BREAKPOINT IS ONE-SHOT ONCE: the second `sb`
            # takes dbg_main.cc's re-arm path, which activates the timer
            # CONTINUOUS - it then fires every N for ever, stopping a guest
            # this module thinks is running (mega-stikmenu ran 524s instead
            # of ~100) - while pc_system.cc's handler re-arms ONE-SHOT for
            # the next one queued. So the queue always holds a SENTINEL
            # years away, armed first: every `sb` here is then the head,
            # and its handler leaves the timer one-shot on the sentinel
            if getattr(self, "_tb_pid", None) != self._proc.pid:
                self._do(["sb %d" % self.TB_SENTINEL])
                self._tb_pid = self._proc.pid
            self._gt_take(self._do(["ptime", "sb %d" % n]))
            g0 = self._gt
            self._resume()
            mark = self._bpmark
            t0 = time.time()
            end = t0 + max(5.0, secs * om.HOST_BACKSTOP)
            nap = 0.002
            while True:
                t = self._e9(mark)
                if ob._PROMPT.search(t) and t.rstrip(" ").endswith(">"):
                    break
                if time.time() > end:
                    # ...and SAY HOW FAR it got: stop it and read the clock,
                    # which tells a breakpoint that never fired from a guest
                    # that never ran
                    got = "?"
                    try:
                        self._halt()
                        self._gt_take(self._do(["ptime"]))
                        got = "%d" % (self._gt - g0)
                    except om.MartyError:
                        pass
                    raise om.MartyError(
                        "os88megabochs: %.3f guest seconds (sb %d) had not "
                        "run after %.0f host seconds - %s instructions had "
                        "- Bochs on %s; the transcript since: %r"
                        % (secs, n, time.time() - t0, got, self.box.display,
                           t[-400:]))
                time.sleep(nap)
                nap = min(nap * 2, 0.01)
            self._stopped_now("paused", t)
            if not count:               # advance's stop is a stop, pace's
                self._stops -= 1        # is not
            self._gt_take(self._do(["ptime"]))
        return max(1, self._gt - g0) / ob.IPS_N

    def pace_exact(self, secs):
        """os88marty.pace's GUEST seconds, exactly (_run_exact), the guest
        left RUNNING as a pause leaves it; None when it cannot be done
        exactly here, and pace polls the clock as it does on MartyPC."""
        if self._state != "running":
            return None                 # a pause ends at a stop: pace's own
        g = self._run_exact(secs)
        if not g:
            return None
        self._resume()
        return g

    def _c_advance(self, frames=None, cycles=None, card=None, **_):
        secs = (cycles / GUEST_HZ if cycles is not None else
                (frames or 1) / 60.0)
        self._bp_hit()
        if self._run_exact(secs, count=True):
            return self._status()
        if ob.SYNC == "realtime":
            self._resume()
            end = time.time() + secs
            while time.time() < end:
                if self._bp_hit():
                    return self._status()
                time.sleep(min(0.01, max(0.0, end - time.time())))
            self._halt()
            return self._status()
        # GUEST seconds, read off the guest (_cycles): a host that stalls
        # makes this take longer, never hand the guest less than it asked
        want = self._cycles(fresh=True) + int(secs * GUEST_HZ)
        self._resume()
        t0 = time.time()
        while True:
            if self._bp_hit():
                return self._status()
            rem = (want - self._cycles()) / GUEST_HZ
            if rem <= 0:
                break
            # ...looking only once the host has had the time to run it: the
            # guest runs at real time at most, so an earlier look is a stop
            # spent on an answer known in advance
            if time.time() - t0 >= secs * 0.9:
                if self._cycles(fresh=True, stale=min(0.02, rem)) >= want:
                    break
            time.sleep(min(0.005, max(0.001, rem / 4)))
        self._halt()
        return self._status()

    def _c_bp(self, list, **_):                 # noqa: A002
        was = self._state
        cmds = ["d %d" % n for n in self._bps]
        want = []
        for b in list:
            t = b.get("type")
            if t == "exec":
                want.append(int(b["addr"]))
            elif t == "execseg":
                want.append(self.lin(int(b["seg"]), int(b["off"])))
            elif t == "int":
                want.append(self._vector(int(b["addr"])))
            else:
                raise om.MartyError("os88megabochs: a %r breakpoint is not "
                                    "available on Bochs" % t)
        with self._lock:
            if self._state == "running":
                self._halt()
                if self._state == "paused":
                    self._stops -= 1
                else:
                    was = self._state       # a breakpoint got there first
            self._do(cmds) if cmds else None
            self._bps = {}
            if want:
                # BOCHS HAS A FIXED NUMBER OF LINEAR BREAKPOINT SLOTS ("no
                # more linear breakpoint slots left"), and a trace on every
                # drawing cell (deskcalperf's 21 and a tone) is past it - the
                # rest silently never fired. Paging is off, so a PHYSICAL
                # breakpoint is the same address from a pool of its own:
                # what `lb` refused goes to `pb`, and what neither took is an
                # error rather than a trace that quietly sees nothing
                pat = (r"^\s*(\d+) [lp]breakpoint\s+keep y\s+"
                       r"0x([0-9a-f]+)")
                t = self._do(["lb 0x%x" % a for a in want] + ["blist"])
                got = {int(m.group(2), 16)
                       for m in re.finditer(pat, t, re.M)}
                rest = [a for a in want if a not in got]
                if rest:
                    t = self._do(["pb 0x%x" % a for a in rest] + ["blist"])
                for m in re.finditer(pat, t, re.M):
                    self._bps[int(m.group(1))] = int(m.group(2), 16)
                miss = set(want) - set(self._bps.values())
                if miss:
                    raise om.MartyError(
                        "os88megabochs: Bochs has no breakpoint slot left "
                        "for %d of %d addresses (%s)" % (
                            len(miss), len(want),
                            " ".join("%x" % a for a in sorted(miss)[:8])))
            if was == "running":
                self._resume()
        return {"count": len(want)}

    def _c_regs(self, **_):
        t = self._stopped(["r", "sreg"])
        r = {}
        for name, val in re.findall(r"^r([a-z0-9]{1,2})\s*: ([0-9a-f_]+)$",
                                    t, re.M):
            v = int(val.replace("_", ""), 16)
            if name in ("ax", "bx", "cx", "dx", "sp", "bp", "si", "di",
                        "ip"):
                r[name] = v & 0xFFFF
        m = re.search(r"eflags 0x([0-9a-f]+)", t)
        if m:
            r["flags"] = int(m.group(1), 16) & 0xFFFF
        for s, v in re.findall(r"^(es|cs|ss|ds|fs|gs):0x([0-9a-f]+)", t,
                               re.M):
            r[s] = int(v, 16)
        return r

    def _c_setreg(self, reg, value, **_):
        """FLAGS go through `set $eflags`, the only spelling Bochs 2.7's
        debugger takes (`set eflags` is a syntax error, and this used to
        send it and answer ok - a row's injected CF never reached the
        guest: tests/czto.py's CRC error read as a good sector). Measured,
        that command writes bits 0-7 as given, puts bit 9 of its value in
        DF and bit 10 in OF, and leaves IF and TF as they were - so DF and
        OF are passed one bit down and IF/TF cannot be written at all."""
        if reg == "flags":
            v = (value & 0xFF) | ((value & 0xC00) >> 1)
            self._stopped(["set $eflags = 0x%x" % v])
            return {}
        reg = {"ip": "eip"}.get(reg, reg)
        self._stopped(["set %s = 0x%x" % (reg, value)])
        return {}

    def _c_park(self, cs, ip, **_):
        """MartyPC's `park`: the CPU to CS:IP on a stopped machine (a row
        calling a kernel routine on the paused guest, tests/desklayer.py's
        top_i). Bochs's `set` takes a selector into CS as it stands"""
        self._halt()
        cmds = ["set eip = 0x%x" % ip]
        if cs != self._at[0]:
            cmds.insert(0, "set cs = 0x%x" % cs)
        t = self._do(cmds)
        self._at = (cs, ip, self._at[2])
        return {"if_cleared": False}   # IF cannot be cleared from here: a
                                       # parked stub starts with `cli`

    def _c_key(self, key, down=True, up=True, **_):
        k = keysym(key)
        if not self.box.captured:   # a key before any mouse event: until
            self.box.capture()      # the window has the focus it is lost
        if down and up:
            a = ("key", "--delay", 40, k)
        elif down:
            a = ("keydown", k)
            self._keys.add(k)
        else:
            a = ("keyup", k)
            self._keys.discard(k)
        if self._state == "running":
            self._xd(*a)
        else:
            # A KEY TYPED ON A STOPPED MACHINE WAITS FOR THE RESUME, PACED.
            # Sent to X now, every event of a string waits in the X queue and
            # Bochs's GUI hands the lot to its 8042 in ONE pass at the resume
            # - whose buffer is 16 scancodes, so os88marty.text_taken (which
            # types with the machine paused) kept four shifted characters of
            # `DOSMOUSE.COM` and dropped eight (tests/kdmouse.py --nomouse)
            self.__dict__.setdefault("_kq", []).append(a)
        self._log.append({"cycles": self._cycles(), "kind": "key",
                          "args": {"key": key, "down": down, "up": up}})
        return {"cycles": self._cycles()}

    def _xd(self, *a):
        """xdotool on the machine's display, keeping a HELD Ctrl held: the
        re-capture after a debugger stop (os88bochs.GRAB) is Ctrl + middle
        click and lets Ctrl go, so a row holding ControlLeft across a read
        (msel's Ctrl+click, any Ctrl+key) lost it at its next event

        UNDER THE MACHINE'S LOCK, which every stop is taken under
        (`_stopped`, `_c_*`): a stop RELEASES the capture, so one landing
        while an xdotool chain is still going out drops the rest of it. A
        row that samples guest memory from a THREAD while it drives
        (tests/curbusy.py's Watch) stopped the guest mid-double-click on
        every read - the four edges reached the guest as at most one click
        and B: never opened (mega-curbusy). Under the lock a chain and a
        stop cannot overlap, and `regrab` - set inside the stop - is always
        seen by the input that follows it"""
        with self._lock:
            if self.box.inp.regrab:
                held = [k for k in ("Control_L", "Control_R")
                        if k in self._keys]
                if held:
                    a = sum((("keydown", k) for k in held), ()) + a
            self.box.xd(*a)


    def queued_input(self, *a, service=None):
        """Send xdotool input A to a STOPPED guest, so all of it reaches the
        machine in ONE pass of Bochs's GUI - an atomic gesture in GUEST
        time, whatever the host does in between (os88mouse.dblclick).

        gui/x.cc's handle_events drains the whole X queue each time it runs
        (once a guest ms), and each button change is a PS/2 packet of its
        own (iodev/keyboard.cc mouse_motion's force_enq), so the four edges
        of a double-click reach the 8042 a few guest ms apart. The stop
        drops Bochs's capture, so `regrab` puts os88bochs.GRAB ahead of A in
        the same call; X delivers it first, and with the pointer parked at
        the capture's warp home a re-capture moves nothing.

        WHAT IT MUST NOT DO is stop again before that pass: the prompt drops
        the capture and the queue would be read uncaptured - lost. So it
        returns once Bochs has READ its X connection dry (os88bochs._Input.
        taken, which every stop waits on as well).

        A BREAKPOINT met before that read (a bp_trace's, inside a held
        pump) is handed to SERVICE(status) and resumed: the queue starts
        with GRAB, so it is read captured all the same - but that stop set
        `regrab` for a capture the queue then took back, and the GRAB the
        next input would carry would toggle it OFF. So after such a stop the
        capture is dropped once more by a stop of our own, and `regrab` is
        true again because it is."""
        def stop_seen():
            if service is not None and self._state == "breakpoint":
                service(self._status())
        with self._lock:
            self._halt()
            stop_seen()
            self._xd(*a)
            self._resume()
            between = False
            end = time.time() + 20.0
            while True:
                self.box.inp.taken(stopped=self._bp_hit)
                if self._state == "running" or time.time() > end:
                    break
                between = True
                stop_seen()
                self._resume()
            if between:
                self._halt()
                stop_seen()
                self._resume()
            self._log.append({"cycles": self._cycles(), "kind": "queued",
                              "args": list(a)})

    def paced_input(self, chains, gap):
        """Send each xdotool chain of CHAINS to a STOPPED guest and run it
        EXACTLY `gap` GUEST seconds before the next (_run_exact) - input
        whose spacing matters, on a device that cannot take a queue.

        Bochs's SERIAL mouse is that device: it LATCHES the button state
        (iodev/serial.cc mouse_enq) and builds a packet only when its idle
        UART polls, every 100 guest ms, so two edges inside one poll are one
        packet carrying the second's level - a press and its release merge
        into NO press. os88mouse.dblclick used to space them with host
        sleeps on a running guest, which the clock preload makes a guest
        interval of anything from ~0 (a Bochs descheduled for over 50 ms is
        a STOP to bxclock, and its time is taken out whole) to the 0.2 s it
        meant - so on a loaded box the edges merged and the double-click
        was one click (mega-irqgrab, mega-dosmouevt, mega-kdnomouse: the
        drive selected, nothing opened).

        Each chain is read in the first GUI pass after the resume (a guest
        ms), long before the time breakpoint, so the capture the stop drops
        is back (`regrab`, os88bochs) when Bochs reads it. A chain still
        unread at the stop - it cannot be read stopped - is waited for on a
        running guest, as queued_input does, and said.

        Leaves the guest RUNNING. Answers the GUEST seconds run after each
        chain, read off the time breakpoint (so they are the spacing the
        guest saw, exactly, where ticks read around the call also count the
        host's gaps either side of it); [] when an exact run cannot be had
        here (a breakpoint of the row's own armed, a bp_trace pump,
        `realtime`), having sent nothing: the caller spaces it another way.
        """
        if ob.SYNC == "realtime" or self._pumping or self._bps:
            return []
        ran = []
        with self._lock:
            self._halt()
            if self._state != "paused":
                return []               # a breakpoint got there first
            for a in chains:
                self._xd(*a)
                g = self._run_exact(gap)
                ran.append(g)
                if not g:
                    self._resume()
                    raise om.MartyError("os88megabochs: paced input could "
                                        "not run the guest %.3f s exactly "
                                        "after %r" % (gap, a))
                inp = self.box.inp
                inp._scan()
                if inp.sent and inp.log and inp.seen < inp.marks:
                    sys.stderr.write("os88megabochs: %r unread after %.3f "
                                     "guest s - waited for running\n"
                                     % (a, gap))
                    self._resume()
                    inp.taken(stopped=self._bp_hit)
                    self._halt()
            self._resume()
            self._log.append({"cycles": self._cycles(), "kind": "paced",
                              "args": [list(a) for a in chains]})
        return ran

    def _c_mouse(self, dx=0, dy=0, l=False, r=False, wz=0, **_):
        if wz and not getattr(self, "imps2", False):
            raise om.MartyError("os88megabochs: no wheel on Bochs's PS/2 "
                                "mouse - launch(..., wheel=True) makes it an "
                                "IntelliMouse")
        if not self.box.captured:
            self.box.capture()
        if wz:
            # WZ NOTCHES, positive TOWARD THE USER (os88marty.wheel): X's
            # button 5 is Bochs's -Z, which its packet sends negated. THEN A
            # ZERO-SUM MOTION, because Bochs 2.7 never clears `delayed_dz`
            # after the packet it built (iodev/keyboard.cc create_mouse_
            # packet) and the next capture change - every debugger stop -
            # sends one more notch the same way (tests/wheelps2.py read +5
            # for four); a motion packet assigns it 0. One guest pixel
            # there and back, at the pointer's current place, each its own
            # GUI pass (Bochs drops a motion that nets to nothing)
            self.box.xd("click", "--repeat", abs(wz), "--delay", 30,
                        5 if wz > 0 else 4)
            self.box.xd("sleep", "0.05", "mousemove_relative", "--", 2, 0,
                        "sleep", "0.05", "mousemove_relative", "--", -2, 0)
        if dx or dy:
            # TWO X PIXELS A GUEST PIXEL, measured (os88bochs.Box.goto) -
            # a serial packet's dx is in mickeys at the same ratio
            self._xd("mousemove_relative", "--", 2 * dx, 2 * dy)
        # A PURE BUTTON PACKET (no motion) IS SENT WHATEVER WE THINK THE
        # LEVEL IS. os88mouse's `_edge` re-sends one when the guest has not
        # published the edge - a Microsoft packet carries the LEVEL, so a
        # re-send is safe - and this used to drop it as already sent. So a
        # release lost in the X queue at a debugger stop (a stop drops the
        # capture, and with it whatever input Bochs had not yet taken) was
        # lost for good: `mouse_btn` stuck at 1 and the drag never ended
        # (deskclip and thm95, with three Bochs rows sharing the box). A
        # repeated X press or release is the same level to Bochs's mouse
        resend = not dx and not dy
        ever = self.__dict__.setdefault("_ever", set())
        for want, btn in ((l, 1), (r, 3)):
            if want and (btn not in self._btn or resend):
                self._xd("mousedown", btn)
                self._btn.add(btn)
                ever.add(btn)
            elif not want and (btn in self._btn or (resend and btn in ever)):
                self._xd("mouseup", btn)
                self._btn.discard(btn)
        return {"cycles": self._cycles()}

    def _video(self):
        """What mode the card is in, from the BIOS data area's mode byte and
        the window's own size - enough for Marty's callers."""
        try:
            mode = self._read(0x449, 1)[0]
        except om.MartyError:
            mode = 0x12
        text = mode in (0, 1, 2, 3, 7)
        return {"type": "vga", "primary": True, "mode": "Mode%02X" % mode,
                "graphics": not text, "field_w": 800, "field_h": 524,
                "frames": 1}

    def _c_video(self, card=None, **_):
        return self._video()

    def _c_cards(self, **_):
        return {"cards": [self._video()]}

    def _c_screen(self, card=None, **_):
        v = self._video()
        if v["graphics"]:
            return {"rows": []}
        return {"rows": self._vga_text(v)}

    def _c_fbuf(self, aperture=0, card=None, **_):
        w, h, data = self._fbuf()
        return {"w": w, "h": h, "data": data.hex()}

    def _c_disk(self, reset=False, **_):
        """MartyPC's FDC counters, out of Bochs's own floppy log (`debug:
        floppy=report`, which Box(flog=True) turns on): a READ or WRITE
        command the controller completed is one `<<READ DONE>>` or
        `<<WRITE DONE>>` line. Read incrementally, so asking is cheap."""
        log = os.path.join(self.box.out, "bochs.log")
        pos, cnt = getattr(self, "_flog", (0, [0, 0]))
        try:
            with open(log, "rb") as f:
                f.seek(pos)
                t = f.read()
        except OSError:
            t = b""
        cut = t.rfind(b"\n") + 1
        cnt[0] += t[:cut].count(b"<<READ DONE>>")
        cnt[1] += t[:cut].count(b"<<WRITE DONE>>")
        self._flog = (pos + cut, cnt)
        r = {"reads": cnt[0], "writes": cnt[1]}
        if reset:
            cnt[0] = cnt[1] = 0
        return r

    def _c_mount(self, drive, path, **_):
        """MartyPC's runtime floppy SWAP (Marty.mount), the guest running:
        the new disk's bytes are written OVER the drive's image in place.
        Bochs reads a floppy image per sector with no cache, so the next
        transfer reads the other disk - and the guest is told nothing, as
        on MartyPC: os8088 notices a swap once the motor has stopped
        (SPEC.md 18.9.1). The geometry is the one Bochs booted with, so the
        two images must be the same size. What the guest writes after it
        lands in the run's image, which `flush` copies out, and `disks`
        names `path` as mounted from then on (tests/czto.py,
        tests/imgswap.py)."""
        live = os.path.join(self.box.out, "a.img" if drive == 0 else "b.img")
        with open(path, "rb") as f:
            data = f.read()
        if len(data) != os.path.getsize(live):
            raise om.MartyError(
                "os88megabochs: mount: %s is %d bytes and drive %d holds "
                "%d - Bochs cannot change a drive's geometry while it runs"
                % (path, len(data), drive, os.path.getsize(live)))
        with open(live, "r+b") as f:
            f.write(data)
        self._src[drive] = os.path.join(
            self.run_dir, "%s-mounted.img" % "ab"[drive])
        shutil.copy(path, self._src[drive])
        return {}

    def _c_reset(self, **_):
        """MartyPC's machine reset, as a POWER CYCLE: Bochs's debugger has
        no reset, so this one is stopped and a new one booted on the SAME
        display from the disks as the guest left them (Bochs writes a
        floppy straight through). The breakpoints are re-armed in the new
        one; the guest is left RUNNING, which `reset(); run()` expects.
        SYSTEM.CFG is not re-injected - what the guest wrote is the point
        of a reboot row."""
        kw = dict(self._boxkw, cfg=None)
        keep = tempfile.mkdtemp(prefix="reset", dir=self.run_dir)
        for n in ("a.img", "b.img"):
            shutil.copy(os.path.join(self.box.out, n), os.path.join(keep, n))
        bps = list(self._bps.values())
        try:
            for b in sorted(self._btn):
                self.box.xd("mouseup", b)
        except Exception:                                   # noqa: BLE001
            pass
        self.box.stop()
        kw.update(aimg=os.path.join(keep, "a.img"),
                  bimg=os.path.join(keep, "b.img"))
        self.box = ob.Box(self.run_dir, [], **kw)
        self.pid = self.box.p.pid
        self._proc = self.box.p
        self._btn, self._keys, self._bps = set(), set(), {}
        self._ldt, self._win, self._flog = None, None, (0, [0, 0])
        self._state, self._since = "running", time.time()
        # ...and the clock carries on from where it was: the new Bochs's
        # `ptime` starts again at 0
        self._gt0, self._gt, self._gt_dirty = self._gt0 - self._gt, 0, True
        _first_c(self.box)
        self._bpmark = os.path.getsize(self.box.e9)
        if bps:
            self._c_bp([{"type": "exec", "addr": a} for a in bps])
        shutil.rmtree(keep, ignore_errors=True)
        return self._status()

    def xfers(self):
        """Every SECTOR the floppy controller has moved since the last call:
        [(drive, byte offset in the image, 'r' or 'w')], out of Bochs's own
        floppy log (`floppy_xfer:` lines, one per 512 bytes, which
        `debug: floppy=report` prints). It is the instrument a DISKCNT kernel
        is on MartyPC ([dsk_dbg_sec]) for a kern_mega row, which has no knob
        build - and it names the sectors, not only their count."""
        log = os.path.join(self.box.out, "bochs.log")
        pos = getattr(self, "_fxfer", 0)
        try:
            with open(log, "rb") as f:
                f.seek(pos)
                t = f.read()
        except OSError:
            t = b""
        cut = t.rfind(b"\n") + 1
        self._fxfer = pos + cut
        return [(int(d), int(o), "r" if w == b"from" else "w")
                for d, o, w in re.findall(
                    rb"floppy_xfer: drive=(\d+), offset=(\d+), bytes=512, "
                    rb"direction=(from|to) floppy", t[:cut])]

    # ---- ports, through the guest -----------------------------------------
    # BOCHS'S DEBUGGER CANNOT TOUCH AN I/O PORT - it reads and writes memory
    # and registers only - so `inb`, `outb` and `planes` are a few bytes of
    # code the GUEST runs: written at 0000:0500 (the DOS communication area,
    # which nothing at the desktop uses, and whose bytes are put back
    # anyway), entered with CS:EIP parked on them and their inputs in
    # registers, stopped by a breakpoint, and then every register they touch,
    # CS:EIP and the bytes are put back. Each stub sits at its own FIXED
    # offset and its bytes never change, because Bochs caches decoded
    # instructions per page and a debugger write does not invalidate them
    # (docs/BOCHS.md) - so inputs go in registers and never in immediates.
    # Every stub is pushf/cli ... popf: what it does to the card is ATOMIC
    # against the guest's own interrupts, and IF comes back as it was, which
    # is the one register Bochs's `set` cannot write.
    _STUB_AT = 0x500
    _STUBS = bytes.fromhex(
        # 00 planes: pushf; cli; mov dx,3CEh; keep the GC index (CL), Mode
        # (CH) and Read Map Select (BL); read mode 0; 1A: Read Map = AH;
        # 1D: STOP - the plane is read through the debugger; then all three
        # back and popf; 2E: STOP
        "9cfabace03ec88c1b005ee42ec88c524f7ee4ab004ee42ec88c3"
        "88e0ee90"
        "88d8ee4ab005ee4288e8ee4a88c8ee9d90"
        "0000000000000000000000000000000000"        # ...to 40
        "9cfaec9d90000000"                          # 40: in al, dx; 44 STOP
        "9cfaee9d90000000")                         # 48: out dx, al; 4C STOP
    _BLIST = r"^\s*(\d+) [lp]breakpoint\s+keep y\s+0x0*%x\b"

    def _stub(self, entry, regs, stops, at_stop=lambda i, r: None):
        """Run the stub at _STUB_AT + ENTRY with REGS ({'eax': v, ...}) set,
        to each offset in STOPS in turn. AT_STOP(i, regs) runs with the
        guest stopped there and may return {'eip': stub offset, reg: v} to
        set before it goes on, with 'again': True to run to the SAME stop
        once more. The row's breakpoints are lifted for the run and every
        register, CS:EIP, the stub's bytes and the machine's run state are
        put back after."""
        with self._lock:
            at0, stops0 = self._at, self._stops
            was = self._state
            if was == "running" and not self._bp_hit():
                self._halt()
            else:
                was = self._state
            t = self._do(["r", "sreg", "creg"])
            orig = {n: int(v.replace("_", ""), 16) & 0xFFFFFFFF
                    for n, v in re.findall(r"^r([a-z]{2})\s*: ([0-9a-f_]+)$",
                                           t, re.M)}
            cs0 = int(re.search(r"^cs:0x([0-9a-f]+)", t, re.M).group(1), 16)
            cs, ip0, gdesc = self._STUB_AT >> 4, 0, None
            if int(re.search(r"CR0=0x([0-9a-f]+)", t).group(1), 16) & 1:
                # PROTECTED MODE: selector 0050h is GDT entry 10, the
                # identity map's DATA descriptor based at 0500h (kernel/
                # mega.inc's map), so for the run its access byte says CODE
                # and 0050h:0 is the stub - and it is DATA again after. No
                # kernel or package loads 0050h: it is the DOS area's segment
                gb, _gl = self.gdt_base()
                d = self._read(gb + 0x50, 8)
                if (d[2] | d[3] << 8 | d[4] << 16 | d[7] << 24) != 0x500 \
                        or (d[5] & 0x98) != 0x90:
                    raise om.MartyError(
                        "os88megabochs: GDT entry 0050h is not data at "
                        "0500h (%s) - no CS for the port stub" % d.hex())
                gdesc = (gb + 0x50 + 5, d[5:6])
                self._write(gdesc[0], bytes([0x9B]))
            keep = self._read(self._STUB_AT, len(self._STUBS))
            self._write(self._STUB_AT, self._STUBS)
            rows = dict(self._bps)
            try:
                if rows:
                    self._do(["d %d" % n for n in rows])
                self._bps = {}
                self._do(["set cs = 0x%x" % cs,
                          "set eip = 0x%x" % (ip0 + entry)] +
                         ["set %s = 0x%x" % kv for kv in regs.items()])
                i = 0
                while i < len(stops):
                    lin = self._STUB_AT + stops[i]
                    t = self._do(["lb 0x%x" % lin, "c"], timeout=30)
                    n = re.findall(self._BLIST % lin, self._do(["blist"]),
                                   re.M)
                    if n:
                        self._do(["d %s" % n[-1]])
                    if not re.search(r"\[0x0*%x\]" % lin, t):
                        raise om.MartyError("os88megabochs: the port stub "
                                            "did not stop at %x" % lin)
                    now = {k: int(v.replace("_", ""), 16) & 0xFFFFFFFF
                           for k, v in re.findall(
                               r"^r([a-z]{2})\s*: ([0-9a-f_]+)$",
                               self._do(["r"]), re.M)}
                    nxt = dict(at_stop(i, now) or {})
                    again = nxt.pop("again", False)
                    cmds = ["set %s = 0x%x" % (k, ip0 + v if k == "eip"
                                               else v)
                            for k, v in nxt.items()]
                    if cmds:
                        self._do(cmds)
                    if not again:
                        i += 1
            finally:
                self._do(["set cs = 0x%x" % cs0] +
                         ["set e%s = 0x%x" % (n, orig[n])
                          for n in ("ax", "bx", "cx", "dx", "ip")])
                self._write(self._STUB_AT, keep)
                if gdesc:
                    self._write(*gdesc)
                if rows:
                    t = self._do(["lb 0x%x" % a for a in rows.values()] +
                                 ["blist"])
                    for a in rows.values():
                        n = re.findall(self._BLIST % a, t, re.M)
                        if n:
                            self._bps[int(n[-1])] = a
                self._at, self._stops = at0, stops0
                if was == "running":
                    self._resume()
                else:
                    self._state = was

    def _c_inb(self, port, **_):
        got = {}
        self._stub(0x40, {"edx": port & 0xFFFF}, [0x44],
                   lambda i, r: got.setdefault("v", r["ax"] & 0xFF) and None)
        return {"value": got["v"]}

    def _c_outb(self, port, value, **_):
        self._stub(0x48, {"edx": port & 0xFFFF, "eax": value & 0xFF}, [0x4C])
        return {}

    def _c_planes(self, offset=0, planes=(0, 1, 2, 3), card=None, **kw):
        """MartyPC's `planes`: each VGA plane's `len` bytes from `offset`.
        The stub selects read mode 0 and the plane's Read Map with
        interrupts off and STOPS there, the plane is read through the
        debugger - whose A0000 reads go through Bochs's VGA as a CPU's do -
        and the next plane is one more pass round the stub's last two
        instructions; then the card is put back as the guest had it."""
        planes = list(planes)
        out = []

        def at(i, r):
            if i:
                return None
            out.append(self._read(0xA0000 + offset, n))
            if len(out) < len(planes):
                return {"eax": planes[len(out)] << 8, "eip": 0x1A,
                        "again": True}
            return None
        n = kw.get("len", 38400)
        if planes:
            self._stub(0x00, {"eax": planes[0] << 8}, [0x1D, 0x2E], at)
        return {"data": [b.hex() for b in out]}

    def _c_quit(self, **_):
        self.close()
        return {}

    def ticks(self):
        """Bochs's own instruction clock (`ptime`): what MartyPC's
        `status()["instructions"]` answers, for a row asking whether the guest
        is RETIRING instructions at all (a cli/hlt freeze). Costs a stop, so
        it is a method and not a status field - status is polled."""
        t = self._stopped(["ptime"])
        m_ = re.findall(r"ptime: (\d+)", t)
        if not m_:
            raise om.MartyError("os88megabochs: no answer to `ptime`")
        return int(m_[-1])

    def cmos(self):
        """The MC146818's 128 bytes, out of Bochs's `info device cmos` - the
        chip as the HARDWARE holds it, which no guest read can tell from the
        kernel's own copy. Bochs-only: MartyPC models no clock chip."""
        t = self._stopped(["info device cmos"])
        b = bytearray(128)
        for m_ in re.finditer(r"^00([0-7])0  ((?:[0-9a-f]{2} ?){16})", t,
                              re.M):
            r = int(m_.group(1), 16) * 16
            b[r:r + 16] = bytes.fromhex(m_.group(2).replace(" ", ""))
        return bytes(b)

    def vram(self, kind=None):
        """Marty.vram's (width, height, rows-of-bits), out of the GLASS: a
        pixel is lit when it is not black. There is no 1bpp framebuffer here
        - kern_mega's desktop is mode 12h on Bochs's VGA - so a row that
        reads ink off `vram()` (black on white, white on a black bar) reads
        the same picture through `fbuf` instead. It is the rendered frame,
        so it trails the kernel's writes by one window refresh, as `fbuf`
        does; a colour that is not black reads as lit."""
        w, h, px = self._fbuf()
        rows = []
        for y in range(h):
            row = px[y * w * 3:(y + 1) * w * 3]
            rows.append([1 if (row[i] | row[i + 1] | row[i + 2]) else 0
                         for i in range(0, w * 3, 3)])
        return w, h, rows

    # Marty.screen falls back to _vga_text via `video`, which reads B8000
    def video(self, card=None):
        return self._video()

    def disks(self):
        """MartyPC's answer: each drive and the image it was MOUNTED from -
        the pristine copy taken at launch, which `flush(path=None)` writes
        back over as MartyPC's Save Floppy does its own private copy."""
        return [{"drive": d, "path": self._src.get(
                    d, os.path.join(self.box.out, "ab"[d] + ".img"))}
                for d in (0, 1)]

    def flush(self, drive=0, path=None, fmt="raw"):
        """Bochs writes a floppy straight through to its image file, so the
        image IS the disk: copy it out."""
        src = os.path.join(self.box.out, "a.img" if drive == 0 else "b.img")
        dst = path or self._src.get(drive)
        if dst:
            shutil.copy(src, dst)
        return {"path": dst or src}

    def wait_for(self, text, secs):
        return self.box.wait_for(text, secs)

    def close(self):
        if self._closed:
            return
        self._closed = True
        try:
            for b in sorted(self._btn):
                self.box.xd("mouseup", b)
        except Exception:
            pass
        try:
            self.box.stop()
            # THE CARD'S CAPTURE, where a MartyPC row looks for it: with
            # $MARTYPC_WAV set at launch, MartyPC writes <it>.sound_blaster.wav
            # and the SB16 here writes OUT/sb.wav (os88bochs.Box(sb=True)) -
            # complete only once Bochs has stopped, so it is copied here
            wav = os.path.join(self.box.out, "sb.wav")
            if self._wav and os.path.exists(wav):
                shutil.copy(wav, self._wav + ".sound_blaster.wav")
        finally:
            if not os.environ.get("OS88_MEGA_KEEP"):
                shutil.rmtree(self.run_dir, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


_ENV_DISP_USED = False


def _first_c(box):
    """UNTIL BOCHS HAS TAKEN ITS FIRST `c`: it starts at the debugger's
    prompt, and a SIGINT that lands before that `c` is read is lost"""
    end = time.time() + 60
    while not re.search(r"<bochs:1> c", ob._tail(box.e9, 0)):
        if time.time() > end or box.p.poll() is not None:
            raise om.MartyError("os88megabochs: Bochs never started (%s)"
                                % os.path.join(box.out, "bochs.log"))
        time.sleep(0.05)



def launch(image, apps=None, machine="", boot=True, label=None, extra=(),
           sb=None, cfg=None, megs=None, noserial=False, wheel=False,
           run_dir=None, cimg=None, ne2k_io=None, **_):
    """os88marty.launch's shape, on Bochs and kern_mega (see the module
    note). `boot` True waits for the desktop (os88bochs.Box.up's state gate,
    then a settle of the glass); a number runs blind for that many seconds;
    False hands the machine back PAUSED, as MartyPC's does. A row's own
    fixed disk (`--mount hd:0:`, or a staged `run_dir`) is `row_disk`'s."""
    own = cimg                  # a C: handed as `cimg=` (copied, below)
    cimg, cgeom = row_disk(machine, extra, run_dir)
    noa = bool(cimg) and (not image or _blank(image))
    a, b = mega_images(image, apps)
    import os88build
    bld = os88build.mega_build()        # the run's tree under a soak
    base = os.path.join(ROOT, "build", "bochs", "inst")
    os.makedirs(base, exist_ok=True)
    tag = re.sub(r"\W+", "", label or os.path.splitext(
        os.path.basename(sys.argv[0] or "row"))[0])[:24]
    run_dir = tempfile.mkdtemp(prefix="%s-" % tag, dir=base)
    if own:
        shutil.copyfile(own, os.path.join(run_dir, "c.img"))
    # THE RUNNER'S DISPLAY for this row's first machine (tools/os88test.py
    # holds one per Bochs row, $OS88_BOCHS_DISPLAY); any further machine in
    # the same process takes one of its own
    global _ENV_DISP_USED
    disp = os.environ.get("OS88_BOCHS_DISPLAY")
    if not disp or _ENV_DISP_USED or ob.running(disp):
        disp = ob.take_display()
    else:
        _ENV_DISP_USED = True
    if sb is None:
        sb = "_sb" in (machine or "")
    # AN NE2000 on Bochs's slirp (the host is 10.0.2.2, $OS88_BOCHS_SLIRP its
    # host-forwards): a machine name with `_ne2k` in it, or $OS88_BOCHS_NE2K;
    # at 300h, or `ne2k_io` (tests/netcoexist.py's card the sweep misses).
    # The row still has to want ETHER.DRV - its own SYSTEM.CFG, or `cfg`
    ne2k = "_ne2k" in (machine or "") or bool(os.environ.get(
        "OS88_BOCHS_NE2K"))
    # `cfg="keep"`: A:'s own SYSTEM.CFG stands - a system-disk FIXTURE that
    # wants ETHER.DRV and more (tests/os88qmpmega.py's mega_twin)
    keep = cfg == "keep"
    if keep:
        cfg = None
    if ne2k and cfg is None and not keep:
        cfg = 0x10              # ...and A:'s SYSTEM.CFG wants ETHER.DRV
                                # (kernel/driver.inc drv_cfgbit), ethertest's
                                # disk's one setting: a card no driver loads
                                # for is no card to any row
    # A FIXED DISK when the machine name has `_hdd` in it (os8088_xt_hdd and
    # its twins): os88bochs.Box's default C:, an ATA disk with RUN/ on it.
    # Everything else has none, as before
    noc = "_hdd" not in (machine or "") and not cimg
    # ...or a fixed disk the row hands as `cimg=` (tests/os88qmpmega.py's
    # TESTHD): COPIED into run_dir as c.img (Box keeps a c.img already in its
    # directory) and read back by the row through that copy - os88qmpmega
    # copies it home. row_disk's `--mount hd:0:` is the other door, mounted
    # IN PLACE; a row uses one or the other
    if own:
        noc = False
        # ITS GEOMETRY BY ITS SIZE: os88disk.py --hdd's one fixed shape
        # (65 x 16 x 63) or Box's own (os88bochs.GEOM)
        sz = os.path.getsize(own)
        cgeom = next((g for g in ((65, 16, 63), tuple(ob.GEOM))
                      if g[0] * g[1] * g[2] * 512 == sz), None)
        if cgeom is None:       # ...or any blank whose size 16 heads of 63
            for h, spt in ((16, 63), (16, 32), (4, 17)):    # or 32 sectors,
                if sz % (h * spt * 512) == 0:               # or Box's 4 of
                    cgeom = (sz // (h * spt * 512), h, spt)  # 17, divide
                    break
        if cgeom is None:
            raise om.MartyError("os88megabochs: a C: of %d bytes is neither "
                                "os88disk.py --hdd's 65/16/63 nor %r" %
                                (sz, ob.GEOM))
    # ...and FILES ON IT: `extra` takes MartyPC's emulator arguments, which
    # mean nothing here, so a row that needs a file on C: (stikhiber's
    # HIBERNAT.PTR) passes ("--put", SPEC) pairs instead - os88disk.py's
    # `FOLDER:path` or a bare path for the root, Box's own `put`
    put, it = [], iter(extra or ())
    for x in it:
        if x == "--put":
            put.append(next(it))
    if not b:
        b = os.path.join(bld, "megaapps.img")
    btype = {368640: "360k", 737280: "720k", 1228800: "1_2",
             1474560: "1_44"}[os.path.getsize(b)]
    box_btype = "1_2" if btype in ("360k", "1_2") else "1_44"
    box = ob.Box(run_dir, [], sb=sb, cfg=cfg, bimg=b, aimg=a,
                 btype=box_btype, cmode="keep" if (cimg or own) else "run",
                 build=bld, display=disp, megs=megs, noc=noc, vgahz=20, flog=True,
                 ne2k=ne2k, noserial=noserial, imps2=wheel, put=put,
                 cimg=cimg, cgeom=cgeom, noa=noa, ne2k_io=ne2k_io)
    m = MegaBochs(box, run_dir, label=label)
    m.cimg = cimg or (own and os.path.join(run_dir, "c.img"))  # its own C:
    m.imps2 = wheel or "type=imps2" in ob.BOCHSRC
    m._boxkw = dict(sb=sb, cfg=cfg, btype=box_btype, build=bld,
                    display=disp, megs=megs, noc=noc, vgahz=20, flog=True,
                    ne2k=ne2k, noserial=noserial, imps2=wheel,
                    cmode="keep",    # a reset keeps the C: it had
                    cimg=cimg, cgeom=cgeom, noa=noa, ne2k_io=ne2k_io)
    try:
        _first_c(box)
        m._bpmark = os.path.getsize(box.e9)
        ob.autorepeat_off(box.display)  # Bochs is connected: it holds
        if boot is False or boot == 0:
            m._c_pause()
            # ...at cycle 0, as MartyPC's is: rows tell launch(boot=False)
            # by `status()["cycles"] == 0` (tests/tank.py's open_game), and
            # the moments Bochs ran before this pause are its BIOS's POST
            m._cyc = 0.0
            if ob.SYNC != "realtime":
                m._gt_take(m._do(["ptime"]))
                m._gt0 = m._gt
            return m
        if boot is True:
            box.up()
            box.capture()
            m.boot_toast()
            return m
        time.sleep(float(boot))
        return m
    except BaseException:
        m.close()
        raise
