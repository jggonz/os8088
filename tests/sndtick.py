#!/usr/bin/env python3
"""DSV_TICK is switched by the driver, and a lost update heals (SPEC.md 34.13.7).

    make && make build/sbtest.img build/sbpoll.img && python3 tests/sndtick.py

Decision D6 of docs/RADBOX-PLAN.md made the tick a thing SOUND.DRV ASKS FOR:
the kernel takes DX as `[drv_svc+DSV_TICK]` after every successful FM verb,
every stream verb but 3 and `DSV_RELINST`, and the driver answers its one tick
proc only while something needs it. What that buys is every sound-card
machine's idle tick - a Sound Blaster published its watchdog at attach and
paid for it on every slice for the life of the driver.

FIVE BOOTS, and each is one question:

  A  ADLIB=1 (an OPL2 and no DSP): the cell reads 0 on an idle desktop and
     stays 0 - nothing on an FM-only card ever wants the tick yet.
  B  SB16=1, the shipped driver, stock SBTEST: 0 idle; the click opens a
     stream and both the kernel's cell and the driver's own read snd_tickp;
     the refill worker's stack slot is read (tools/stkwater.py) while its
     loop carries verb 9; the second click closes, and the cell is 0 again
     once the worker's exit-path verb 9 has run. Then (F) the stream is
     reopened with the card's IRQ masked at the PIC: the watchdog must END it
     and the dead worker switch the tick off before anybody closes it.
  C  SB16=1, the shipped driver, SBPOLL (an owner that issues only verb 3,
     which is not a site): 0 PLANTED in both cells while the stream is live,
     and both read the proc again within 2 BIOS ticks - the heal.
  D  the same with a -DSNDREADBACK driver (verb 9 re-answers its cell): the
     planted 0 is still there after 36 ticks. The recompute's negative control.
  E  the same with a -DSNDNOHEAL driver (no verb 9 in the workers' loops):
     the kernel's cell is still 0 after 36 ticks. The heal's negative control.

THE POKE IS A GDB `M` PACKET. QMP reads memory (`pmemsave`) and cannot write
it, so each boot also opens QEMU's gdb stub on a free port: attaching halts
the guest, which is what lets the BIOS tick be read at the instant of the poke,
and a detach (`D`) resumes it.

EVERY CONTROL CHECKS ITS OWN PREMISE. A stream the watchdog has ENDED is
legitimately not live - its worker switches the tick off on the way out - so a
planted 0 that stays 0 would prove nothing. Each planted arm therefore reads
`[sbl_str_act]` and `[sbl_str_state]` at the end and refuses to count a stream
that is closed or ENDED.

QEMU by name: SPEC.md 96.8 pins this row to `SB16=1` and `ADLIB=1`, and the
2-tick bound is a QEMU figure. Nothing here is a time.
"""
import os
import re
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.chdir(ROOT)
import dispcp                                               # noqa: E402
from ethernet import Qemu, Mouse                            # noqa: E402
import os88build                                            # noqa: E402
import os88qemu                                             # noqa: E402
import os88sym                                              # noqa: E402

PIDFILE = os.path.join(ROOT, "build", "qemu.pid")
SOCK = os.path.join(ROOT, "build", "qmp.sock")
S = os88sym.linear
DSV_TICK = 6
DRVR_SEG, SND_ROW = 2, 0                # drv_tab row 0 is the sound class
BIOS_TICK = 0x46C
SBL_ST_END = 2
HEAL_TICKS = 2                          # SPEC.md 34.13.7's QEMU figure
CONTROL_TICKS = 36

fails = []


def say(*a):
    print(*a)
    sys.stdout.flush()


def check(name, cond, note=""):
    say("  %-4s %s%s" % ("ok" if cond else "FAIL", name,
                         ("  " + note) if note else ""))
    if not cond:
        fails.append(name)


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def drv_syms(defines=()):
    """SOUND.DRV's labels, image-relative, by re-assembling it with a map."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "s.asm"), os.path.join(d, "s.map")
        src = open("drivers/sound/sound.asm").read()
        open(cp, "w").write(src + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + ["-D" + x for x in defines]
                       + ["-I", "drivers/sound/", "-I", "drivers/",
                          "-I", "apps/", "-o", os.path.join(d, "s.bin"), cp],
                       check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


# --- gdb remote protocol: just enough to read and write memory --------------
class Gdb:
    def __init__(self, port):
        self.s = None
        for _ in range(100):
            try:
                self.s = socket.create_connection(("127.0.0.1", port), 2)
                break
            except OSError:
                time.sleep(0.1)
        if self.s is None:
            raise RuntimeError("no gdb stub on port %d" % port)
        self.buf = b""
        self.cmd("?")                   # attach: the guest is halted now

    def _pkt(self, body):
        cs = sum(body.encode()) & 0xFF
        return ("$%s#%02x" % (body, cs)).encode()

    def cmd(self, body, reply=True):
        self.s.sendall(self._pkt(body))
        if not reply:
            return None
        while True:
            while b"#" not in self.buf or len(self.buf) < self.buf.index(b"#") + 3:
                chunk = self.s.recv(4096)
                if not chunk:
                    raise RuntimeError("gdb stub closed")
                self.buf += chunk
            i = self.buf.find(b"$")
            if i < 0:
                self.buf = b""
                continue
            j = self.buf.index(b"#", i)
            data = self.buf[i + 1:j].decode()
            self.buf = self.buf[j + 3:]
            self.s.sendall(b"+")
            if data.startswith("O") and data != "OK":
                continue                # console output, not our answer
            return data

    def read(self, addr, n):
        return bytes.fromhex(self.cmd("m%x,%x" % (addr, n)))

    def write(self, addr, data):
        r = self.cmd("M%x,%x:%s" % (addr, len(data), data.hex()))
        if r != "OK":
            raise RuntimeError("gdb write refused: %r" % r)

    def detach(self):
        try:
            self.cmd("D")
        finally:
            self.s.close()


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def launch(img, apps, card, wav=None):
    os88qemu.kill(PIDFILE, SOCK)
    port = free_port()
    # THE SOUND BLASTER BOX CARRIES AN OPL TOO, as every real Sound Blaster
    # does: the kernel's boot sniff (drv_probe) wants the sound driver only
    # when an OPL answers at 388h, and QEMU's sb16 has none, so `-device sb16`
    # alone boots with SOUND.DRV unwanted (SNDSNIFF=sb is the knob-kernel way
    # round that, and a knob kernel is a different symbol map).
    dev = {"sb16": "-device sb16,audiodev=snd -device adlib,audiodev=snd",
           "adlib": "-device adlib,audiodev=snd"}[card]
    em = "qemu" + "-system-i386"        # never whole: tests/os88qemu.py
    subprocess.run(
        em + " -machine pc,vmport=off"
        " -drive file=%s,format=raw,if=floppy -boot a"
        " -drive file=%s,format=raw,if=floppy,index=1"
        " -chardev msmouse,id=m0 -serial chardev:m0"
        " -audiodev %s,id=snd %s -gdb tcp:127.0.0.1:%d"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (img, apps, ("wav,path=%s" % wav) if wav else "none", dev, port,
           SOCK, PIDFILE),
        cwd=ROOT, shell=True, check=True)
    os88qemu.own(PIDFILE, SOCK)
    m = Qemu(SOCK)
    t0 = time.time()
    seg = 0
    while time.time() - t0 < 90:
        seg = u16(m.read(S("drv_tab") + SND_ROW * 16 + DRVR_SEG, 2))
        if seg and m.read(S("spl_live"), 1)[0] == 0:
            break
        time.sleep(0.5)
    if not seg:
        raise SystemExit("sndtick: SOUND.DRV never loaded on the %s box" % card)
    time.sleep(5)                       # the desktop's first paint and idle
    return m, port, seg


class Box:
    def __init__(self, m, port, seg, D):
        self.m, self.port, self.seg, self.D = m, port, seg, D

    def kcell(self):
        return u16(self.m.read(S("drv_svc") + DSV_TICK, 2))

    def dcell(self):
        return u16(self.m.read((self.seg << 4) + self.D["snd_services"]
                               + DSV_TICK, 2))

    def byte(self, name):
        return self.m.read((self.seg << 4) + self.D[name], 1)[0]

    def tick(self):
        return u16(self.m.read(BIOS_TICK, 2))

    def proc(self):
        return self.D["snd_tickp"]

    def live(self):
        return self.byte("sbl_str_act") == 1 and \
            self.byte("sbl_str_state") != SBL_ST_END

    def plant(self):
        """Halt, read the tick, zero both cells, resume. Answers the tick."""
        g = Gdb(self.port)
        try:
            t = u16(g.read(BIOS_TICK, 2))
            g.write(S("drv_svc") + DSV_TICK, b"\0\0")
            g.write((self.seg << 4) + self.D["snd_services"] + DSV_TICK,
                    b"\0\0")
            k = u16(g.read(S("drv_svc") + DSV_TICK, 2))
            d = u16(g.read((self.seg << 4) + self.D["snd_services"]
                           + DSV_TICK, 2))
        finally:
            g.detach()
        if k or d:
            raise SystemExit("sndtick: the poke did not land (%04x %04x)"
                             % (k, d))
        return t

    def wait_ticks(self, t0, n):
        while (self.tick() - t0) & 0xFFFF < n:
            time.sleep(0.02)


def open_pkg(m, mo, name):
    dispcp.open_drive(m, mo, S, lambda *a, **k: time.sleep(2.0), "B")
    disk = dispcp.win_list(m, S)[-1]
    wx, wy = dispcp.win_rect(m, S, disk)[:2]
    before = set(dispcp.win_list(m, S))
    dispcp.open_named(m, mo, S, lambda *a, **k: time.sleep(2.0), wx, wy, name)
    time.sleep(3)
    new = [w for w in dispcp.win_list(m, S) if w not in before]
    if not new:
        raise SystemExit("sndtick: %s never opened a window" % name)
    return dispcp.win_rect(m, S, new[-1])


def stkwater(m):
    """{slot: (used, size)} for every spawned task slice (tools/stkwater.py).

    Read here rather than through stkwater's own main, which resolves its
    symbols against a KFZTRACE kernel and so refuses the shipped one."""
    import stkwater as sw
    sizes = sw.slice_sizes(())
    base = S("sch_stacks")
    mem = m.read(base, sum(sizes))
    return {slot: (used, sizes[slot - 1])
            for slot, used, _ in sw.water(mem, len(sizes), sizes)
            if used is not None}


def arm_idle(box, label):
    k, d = box.kcell(), box.dcell()
    check("%s: DSV_TICK is 0 on an idle desktop" % label, k == 0 and d == 0,
          "kernel %04x, driver %04x" % (k, d))
    t0 = box.tick()
    box.wait_ticks(t0, 18)
    k, d = box.kcell(), box.dcell()
    check("%s: ...and still 0 a second later" % label, k == 0 and d == 0,
          "kernel %04x, driver %04x" % (k, d))


def boot_a():
    say("A: ADLIB=1 - an OPL2, no DSP")
    D = drv_syms()
    m, port, seg = launch("build/os8088.img", os88build.at("build/sbtest.img"),
                          "adlib")
    box = Box(m, port, seg, D)
    caps = u16(m.read(S("drv_svc"), 2))
    say("  driver at %04x, kernel DSV_CAPS %04x" % (seg, caps))
    arm_idle(box, "A")
    m.quit()


def boot_b():
    say("B: SB16=1 - the shipped driver, stock SBTEST")
    D = drv_syms()
    wav = os.path.join(ROOT, "build", "sndtick-sb.wav")
    if os.path.exists(wav):
        os.remove(wav)
    m, port, seg = launch("build/os8088.img", os88build.at("build/sbtest.img"),
                          "sb16", wav)
    box = Box(m, port, seg, D)
    mo = Mouse()
    arm_idle(box, "B")
    before = stkwater(m)
    x, y, w, h = open_pkg(m, mo, "SBTEST.O88")
    mo.click(x + w // 2, y + h // 2)            # open the stream
    time.sleep(1.0)
    k, d = box.kcell(), box.dcell()
    act = box.byte("sbl_str_act")
    check("B: a stream opened", act == 1, "sbl_str_act %d" % act)
    check("B: the kernel's cell names snd_tickp while it is open",
          k == box.proc(), "kernel %04x, snd_tickp %04x" % (k, box.proc()))
    check("B: the driver's own cell holds the same", d == k,
          "driver %04x" % d)
    time.sleep(3.0)                             # the 2 s tone underruns
    k = box.kcell()
    st = box.byte("sbl_str_state")
    check("B: still named while the stream is open and not ENDED",
          (k == box.proc()) == box.live(),
          "kernel %04x, state %d, act %d" % (k, st, box.byte("sbl_str_act")))
    during = stkwater(m)
    grew = sorted((s, during[s], before.get(s)) for s in during
                  if during[s] != before.get(s))
    for s, (used, sz), was in grew:
        say("  stkwater slot %d: %d of %d used while streaming (was %s)"
            % (s, used, sz, "%d" % was[0] if was else "never spawned"))
    mo.click(x + w // 2, y + h // 2)            # close it
    t0 = box.tick()
    box.wait_ticks(t0, 6)                       # the worker's exit-path verb 9
    k, d = box.kcell(), box.dcell()
    check("B: 0 again after close, once the worker has exited",
          k == 0 and d == 0 and box.byte("sbl_str_act") == 0,
          "kernel %04x, driver %04x" % (k, d))

    # --- F: THE WATCHDOG STILL STOPS A STREAM WHOSE IRQ NEVER COMES ---------
    # sbl_tick is snd_tickp's first half now and runs only while the tick is
    # named, so this is the check that the switch did not switch the watchdog
    # off with it. The lost IRQ is made rather than waited for: HMP's `o`
    # writes the master PIC's mask with the card's line set, right after the
    # open, so no block IRQ is delivered. The stream must reach SBL_ST_END,
    # and its worker's exit-path verb 9 must then switch the tick off while
    # the stream is still OPEN - nothing has closed it.
    irq = box.byte("sbl_irq")
    mo.click(x + w // 2, y + h // 2)            # reopen
    mask0 = None
    if 2 <= irq <= 7 and box.byte("sbl_str_act") == 1:
        out = m.hmp("i /b 0x21")
        mm = re.search(r"=\s*0x([0-9a-fA-F]+)", out)   # portb[0x0021] = 0x88
        mask0 = int(mm.group(1), 16) if mm else None
    if mask0 is None:
        check("F: a stream reopened on a line this row can mask", False,
              "irq %d, act %d, PIC read %r" % (irq, box.byte("sbl_str_act"),
                                               locals().get("out")))
        m.quit()
        return
    m.hmp("o /b 0x21 0x%02x" % (mask0 | (1 << irq)))
    t0 = box.tick()
    box.wait_ticks(t0, 36)
    st, act = box.byte("sbl_str_state"), box.byte("sbl_str_act")
    k, d = box.kcell(), box.dcell()
    m.hmp("o /b 0x21 0x%02x" % mask0)
    check("F: the watchdog ENDED the stream whose IRQ was masked (IRQ%d)" % irq,
          st == SBL_ST_END and act == 1, "state %d, act %d" % (st, act))
    check("F: ...and its worker switched the tick off on the way out",
          k == 0 and d == 0, "kernel %04x, driver %04x" % (k, d))
    mo.click(x + w // 2, y + h // 2)            # close the dead stream
    time.sleep(1.0)
    check("F: the dead stream closes", box.byte("sbl_str_act") == 0 and
          box.kcell() == 0, "act %d, kernel %04x"
          % (box.byte("sbl_str_act"), box.kcell()))
    m.quit()
    time.sleep(1.5)
    r = subprocess.run(["python3", "tools/sndcheck.py", wav, "1000"],
                       capture_output=True, text=True)
    last = (r.stdout.strip().splitlines() or [r.stderr.strip()])[-1]
    check("B: SBTEST's stream SOUNDED - its 1 kHz square is in the capture",
          r.returncode == 0, last)


def planted(label, knob, defines, heal):
    say("%s: SB16=1 - %s driver, SBPOLL owner (verb 3 only)" % (label, knob))
    if knob == "shipped":
        img = "build/os8088.img"
    else:
        t = os88build.tree("%s=1" % knob, targets=("os8088.img",))
        img = t.img("os8088.img")
    D = drv_syms(defines)
    m, port, seg = launch(img, os88build.at("build/sbpoll.img"), "sb16")
    box = Box(m, port, seg, D)
    mo = Mouse()
    arm_idle(box, label)
    before = stkwater(m)
    x, y, w, h = open_pkg(m, mo, "SBPOLL.O88")
    mo.click(x + w // 2, y + h // 2)
    time.sleep(1.5)
    k = box.kcell()
    check("%s: the stream is open and the tick is named" % label,
          box.live() and k == box.proc(),
          "kernel %04x, act %d, state %d" % (k, box.byte("sbl_str_act"),
                                            box.byte("sbl_str_state")))
    t0 = box.plant()
    if heal:
        got = None
        while (box.tick() - t0) & 0xFFFF <= HEAL_TICKS + 1:
            if box.kcell() == box.proc() and box.dcell() == box.proc():
                got = (box.tick() - t0) & 0xFFFF
                break
            time.sleep(0.01)
        check("%s: both cells read snd_tickp again within %d ticks"
              % (label, HEAL_TICKS), got is not None and got <= HEAL_TICKS,
              "after %s ticks; kernel %04x, driver %04x"
              % (got, box.kcell(), box.dcell()))
    else:
        box.wait_ticks(t0, CONTROL_TICKS)
        k, d = box.kcell(), box.dcell()
        check("%s: the kernel's cell is still 0 after %d ticks"
              % (label, CONTROL_TICKS), k == 0,
              "kernel %04x, driver %04x" % (k, d))
    during = stkwater(m)
    for s_, (used, sz) in sorted(during.items()):
        if during[s_] != before.get(s_):
            say("  stkwater slot %d: %d of %d used (%s)"
                % (s_, used, sz, "verb 9 in the loop" if heal or
                   knob == "SNDREADBACK" else "no verb 9 in the loop"))
    check("%s: the stream was live throughout (the premise)" % label,
          box.live(), "act %d, state %d" % (box.byte("sbl_str_act"),
                                             box.byte("sbl_str_state")))
    m.quit()


def main():
    only = sys.argv[1:] or ["A", "B", "C", "D", "E"]
    try:
        if "A" in only:
            boot_a()
        if "B" in only:
            boot_b()
        if "C" in only:
            planted("C", "shipped", (), True)
        if "D" in only:
            planted("D", "SNDREADBACK", ("SNDREADBACK",), False)
        if "E" in only:
            planted("E", "SNDNOHEAL", ("SNDNOHEAL",), False)
    finally:
        os88qemu.kill(PIDFILE, SOCK)
    say("sndtick: %s" % ("FAILED: " + ", ".join(fails) if fails else
                         "DSV_TICK is 0 idle, named while a stream is open, "
                         "and a planted 0 heals - and neither control does"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
