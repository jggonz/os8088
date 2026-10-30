#!/usr/bin/env python3
"""QEMU's QMP socket, answered by a kern_mega machine - Bochs, or 86Box with
$OS88_MEGA_EMU=86box (tools/os88megabochs.py, tools/os88mega86box.py) - or
by a kern_big or kern_small machine on 86Box with $OS88_EMU=86box
(tools/os88box86api.py; docs/86BOX.md "kern_big rows on 86Box").

Not a row: the layer the NETWORK rows' kern_mega arms stand on
(docs/BOCHS.md, "Rows ported from MartyPC"; tests/megaport.txt's
`# THE NETWORK (wave 2)`). Those rows were written against QEMU, because it
was the only emulator here with a network card, and they drive it through a
QMP UNIX SOCKET from every direction at once: their own `Qemu` classes,
tests/xmcheck.py's in-process client, tools/qmp.py, tools/mouse.py and
tools/shot.py as subprocesses. Under $OS88_MEGA, tests/os88qemu.py's
launchers start one of these instead of QEMU - a Bochs with an NE2000 on
its slirp (os88megabochs.launch's `_ne2k`, the host at 10.0.2.2 as QEMU's
`-netdev user` has it) behind a server on the SAME socket path - so every
one of those clients runs as it stands.

    srv = os88qmpmega.Server(sock, pidfile, a_img, b_img)

UNDER $OS88_EMU=86box WITHOUT $OS88_MEGA it is the row's OWN kernel and
disks on 86Box (os88box86api.launch): nothing is twinned or swapped, and the
machine is an AT (the mr286, `at286`) with a VGA, because what these rows
were written against is QEMU's - a fast AT - and the NE2000 they want is a
16-bit card. `ps2=True` (make_test's MOUSEPORT=ps2) is vm/386-ps2's
Packard Bell and its PS/2 mouse instead.

UNDER $OS88_MEGA_EMU=86box the machine is an 86Box instead
(tools/os88mega86box.py, docs/86BOX.md "kern_mega rows on 86Box") - a VGA,
the NE2000 on 86Box's slirp, run PACED rather than fast-forwarded because
these rows time their input on the host's clock - and everything below is
the same. That is how telansi and telzm, which Bochs's slirp stops
(KERN-MEGA-PLAN 14.89), are ported.

It is a SERVER rather than a wrapper, os88megabochs's own trick one layer
up: the clients speak QMP and this answers it out of a MegaBochs, so
nothing above it learns that the machine changed. What it answers:

  * `human-monitor-command`: `xp /N{x,d,u}{b,h,w,g} ADDR` printed as QEMU
    prints it, `pmemsave ADDR N "FILE"`, `sendkey a-b-c`, `mouse_move DX DY`,
    `mouse_button MASK`, `screendump FILE` (a P6 PPM, as QEMU writes one),
    `info status`, `stop`/`cont`, `system_reset` (a POWER CYCLE, the shim's
    `reset`), `quit`. Anything else is an error naming itself.
  * `input-send-event`: `key` (qcodes), `btn` and `rel` events.
  * `query-status`, `screendump`, `quit`, `qmp_capabilities`.

THE POINTER IS ABSOLUTE UNDERNEATH. A QEMU row moves the serial mouse by
relative `mouse_move`s - tools/mouse.py pins against the kernel's edge clamp
and walks back, os88qemu.mouse_to sends a delta and reads `mouse_x` back -
and Bochs's PS/2 mouse is fed X pointer motion, whose scale is not one to
one. So a `mouse_move` only moves an INTENDED position (clamped to the
screen, as the kernel clamps it), and the next command that is not a
`mouse_move` drives the guest there CLOSED LOOP first (os88mouse.Mouse.to,
which reads the published cursor back). A pin-and-walk lands exactly where
it meant to, and a closed loop above it converges in one step.

BUTTONS ARE NOT VERIFIED one edge at a time. A double-click is four
`mouse_button`s ~0.1 s apart down one connection (xmcheck.dblclick,
ethernet.Mouse.dblclick) and the kernel's window is nine ticks; proving each
edge costs a debugger stop a poll and pushed the pair apart. Edges are
QUEUED and go to X as ONE xdotool chain 30 ms apart, as os88megabochs's own
double-click does, at the next command of another kind or after HOLD with
no further edge (Server._button: Bochs's guest clock is the host's, so an
xdotool spawned per edge spaced a loaded box's double-click 1.15 s apart).
The next stop is then held off until Bochs has READ the chain
(os88bochs._Input.taken), because a stop drops the grab.

MEMORY: an address is LINEAR, as QEMU's `xp`/`pmemsave` take it. A row that
computes `seg << 4` from a guest-read segment reads the wrong place under
kern_mega, where every package region, heap claim and driver image is an
LDT selector - such a row calls os88qemu.lin(seg, off), which is
`(seg << 4) + off` on QEMU and the descriptor's base here.

THE DISKS: os88megabochs.launch's mapping (a shipped system floppy becomes
build/mega.img, a shipped apps floppy build/megaapps.img) plus one this
layer adds, `mega_twin`: a SYSTEM-DISK FIXTURE the Makefile builds out of the
shipped disk plus a few files (ether360.img, telnetsys.img, httpmount.img,
thewire360.img) is rebuilt on mega.img by copying exactly the files in which
the fixture differs from the shipped disk of its geometry - so no fixture
needs naming here. When the machine closes, the B: image Bochs wrote is
copied back over the path the row booted, which is where a QEMU row reads
what the guest saved (QEMU writes its image in place; the shim boots a copy).
"""
import atexit
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

MEGA = bool(os.environ.get("OS88_MEGA"))
BOX86 = os.environ.get("OS88_MEGA_EMU") == "86box" or \
    (os.environ.get("OS88_EMU") or "").lower() == "86box"
_LIVE = []                  # every Server this process started, newest last

# QEMU qcode -> X keysym (what HMP's sendkey and input-send-event name)
_QK = {
    "ret": "Return", "kp_enter": "KP_Enter", "spc": "space", "tab": "Tab",
    "esc": "Escape", "backspace": "BackSpace", "delete": "Delete",
    "insert": "Insert", "home": "Home", "end": "End", "pgup": "Prior",
    "pgdn": "Next", "up": "Up", "down": "Down", "left": "Left",
    "right": "Right", "shift": "Shift_L", "shift_r": "Shift_R",
    "ctrl": "Control_L", "ctrl_r": "Control_R", "alt": "Alt_L",
    "alt_r": "Alt_R", "caps_lock": "Caps_Lock", "num_lock": "Num_Lock",
    "scroll_lock": "Scroll_Lock", "minus": "minus", "equal": "equal",
    "bracket_left": "bracketleft", "bracket_right": "bracketright",
    "backslash": "backslash", "semicolon": "semicolon",
    "apostrophe": "apostrophe", "grave_accent": "grave", "comma": "comma",
    "dot": "period", "slash": "slash", "asterisk": "KP_Multiply",
    "kp_add": "KP_Add", "kp_subtract": "KP_Subtract",
    "kp_multiply": "KP_Multiply", "kp_divide": "KP_Divide",
    "kp_decimal": "KP_Decimal", "pause": "Pause", "print": "Print",
    "sysrq": "Sys_Req", "less": "less",
}


def keysym(q):
    q = q.lower()
    if q in _QK:
        return _QK[q]
    if len(q) == 1 and (q.isalnum()):
        return q
    if re.match(r"f\d{1,2}$", q):
        return q.upper()
    if re.match(r"kp_\d$", q):
        return "KP_" + q[3]
    raise ValueError("os88qmpmega: no X keysym for qcode %r" % q)


# QEMU qcode -> MartyKey name, for the 86Box backend (its `key` verb)
_QM = {
    "ret": "Enter", "kp_enter": "NumpadEnter", "spc": "Space", "tab": "Tab",
    "esc": "Escape", "backspace": "Backspace", "delete": "Delete",
    "insert": "Insert", "home": "Home", "end": "End", "pgup": "PageUp",
    "pgdn": "PageDown", "up": "ArrowUp", "down": "ArrowDown",
    "left": "ArrowLeft", "right": "ArrowRight", "shift": "ShiftLeft",
    "shift_r": "ShiftRight", "ctrl": "ControlLeft", "ctrl_r": "ControlRight",
    "alt": "AltLeft", "alt_r": "AltRight", "caps_lock": "CapsLock",
    "num_lock": "NumLock", "scroll_lock": "ScrollLock", "minus": "Minus",
    "equal": "Equal", "bracket_left": "BracketLeft",
    "bracket_right": "BracketRight", "backslash": "Backslash",
    "semicolon": "Semicolon", "apostrophe": "Quote",
    "grave_accent": "Backquote", "comma": "Comma", "dot": "Period",
    "slash": "Slash", "asterisk": "NumpadMultiply", "kp_add": "NumpadAdd",
    "kp_subtract": "NumpadSubtract", "kp_multiply": "NumpadMultiply",
    "kp_divide": "NumpadDivide", "kp_decimal": "NumpadDecimal",
}


def martykey(q):
    """A qcode as the MartyKey name os88mega86box's `key` verb takes."""
    q = q.lower()
    if q in _QM:
        return _QM[q]
    if len(q) == 1 and q.isalpha():
        return "Key" + q.upper()
    if len(q) == 1 and q.isdigit():
        return "Digit" + q
    if re.match(r"f\d{1,2}$", q):
        return q.upper()
    if re.match(r"kp_\d$", q):
        return "Numpad" + q[3]
    raise ValueError("os88qmpmega: no MartyKey for qcode %r" % q)


def live():
    """The newest machine this process started, or None."""
    for s in reversed(_LIVE):
        if not s.closed:
            return s
    return None


def lin(seg, off=0):
    """SEG:OFF as a linear address: `(seg << 4) + off`, or under kern_mega the
    descriptor's base when SEG is a selector (os88megabochs.lin)."""
    s = live() if MEGA else None
    if s is None:
        return ((seg & 0xFFFF) << 4) + off
    return s.m.lin(seg, off)


def claims():
    """kern_mega's heap claims as tests/ethernet.py's `claims` tuples -
    (name, paragraphs, owner, dma head, rloc, hi) - with NAME what the claim's
    holder keeps (a driver's `[sk_seg]`, a package's word): the mem_tab
    record's SELECTOR (`mem_sel`, by index) for a conventional claim, and
    every block of the XMEM POOL above 1MB too (kernel/mega.inc's mx_tab,
    tools/heapmap.py's `pool`), which mem_tab never sees. A pool block has
    no MC_DMA: it reads `hi` 1, because the thing `hi` stands for - a claim
    out of the way of the conventional arena's ascending pass - is what
    being above 1MB is (SPEC.md 66.4.1 cannot reach it either way)."""
    import heapmap
    m = live().m
    raw = m.read(m.sym("mem_tab"), heapmap.MEM_MAX * heapmap.MC_SIZE)
    sel = m.read(m.sym("mem_sel"), heapmap.MEM_MAX * 2)
    out = []
    for i in range(heapmap.MEM_MAX):
        c = heapmap.Claim(raw[i * heapmap.MC_SIZE:(i + 1) * heapmap.MC_SIZE])
        if not c.seg:
            continue
        name = heapmap.u16(sel, 2 * i) or c.seg
        out.append((name, c.para, c.own, c.dma, c.rloc, 1 if c.hi else 0))
    for c in heapmap.pool(m):
        out.append((c.sel, c.para, c.own, 0, c.rloc, 1))
    return sorted(out)


# --- the disks --------------------------------------------------------------

def _tree(img, dst):
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    os.makedirs(dst, exist_ok=True)
    subprocess.run(["mcopy", "-s", "-n", "-m", "-i", img, "::/*", dst],
                   check=True, env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    out = {}
    for r, _ds, fs in os.walk(dst):
        for f in fs:
            p = os.path.join(r, f)
            out[os.path.relpath(p, dst).upper()] = p
    return out


_BASE = {368640: "os8088-360.img", 737280: "os8088-720.img",
         1228800: "os8088-120.img", 1474560: "os8088.img"}
_TWINS = {}


def mega_twin(img, bld=None):
    """`img` itself off kern_mega. Under it: a shipped system floppy is left
    to os88megabochs's own mapping; any other SYSTEM disk (one carrying a
    KERNEL.SYS) is rebuilt on a copy of build/mega.img with every file in
    which it differs from the shipped system disk of its geometry - the
    Makefile's fixture recipes are that disk plus a SYSTEM.CFG and a file or
    two, and this needs to know none of them. A disk with no KERNEL.SYS is a
    data disk and goes in as it is."""
    if not MEGA or not img:
        return img
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88build
    bld = bld or os88build.mega_build()     # the run's tree under a soak
    ap = os.path.abspath(img)
    if re.match(r"(os8088|small)[-\w]*\.img$", os.path.basename(ap)) and \
            os88build.shipped_dir(os.path.dirname(ap)):
        return img
    # KEYED ON THE FILE AS IT STANDS: a row that boots the same fixture
    # again meets its twin as the last machine left it (ethcfg's reboot,
    # close() copying A: back into the twin), and one that REWRITES the
    # fixture between boots (netinst's AUTOSTRT link) gets a fresh twin
    st = os.stat(ap)
    key = (ap, st.st_mtime_ns, st.st_size)
    if key in _TWINS:
        return _TWINS[key]
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88fat
    try:
        names = [os88fat.Fat12.pretty(e[0]) if isinstance(e[0], bytes)
                 else e for e in os88fat.Fat12(ap).entries()]
    except Exception:                                       # noqa: BLE001
        names = []
    flat = " ".join(str(n) for n in names)
    if "KERNEL.SYS" not in flat and "KERNEL  SYS" not in flat:
        return img
    try:                        # ALREADY kern_mega's: a scratch A: a machine
        if os88fat.Fat12(ap).read("KERNEL.SYS") == os88fat.Fat12(  # of this
                os.path.join(bld, "mega.img")).read("KERNEL.SYS"):  # layer
            return img          # left behind (close() copies it home)
    except Exception:                                       # noqa: BLE001
        pass
    base = os.path.join(bld, _BASE.get(os.path.getsize(ap), "os8088.img"))
    tmp = tempfile.mkdtemp(prefix="megatwin-")
    atexit.register(shutil.rmtree, tmp, True)
    mine, theirs = _tree(ap, os.path.join(tmp, "f")), \
        _tree(base, os.path.join(tmp, "b"))
    megas = _tree(os.path.join(bld, "mega.img"), os.path.join(tmp, "m"))
    out = os.path.join(tmp, "twin-" + os.path.basename(ap))
    shutil.copyfile(os.path.join(bld, "mega.img"), out)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")

    def body(f):
        return open(f, "rb").read() if f else None
    for rel, p in sorted(mine.items()):
        q = theirs.get(rel)
        if q is not None and body(q) == body(p):
            continue            # the shipped disk's own: mega.img has its own
        # ...AND NEVER A FILE THE KERNEL BUILDS: a .DRV or .SYS the shipped
        # disk and mega.img both carry, different (KERNEL.SYS, the on-demand
        # modules, BOOTHD.SYS), is kern_big's on the fixture - and a fixture
        # built at another build number differs from the shipped disk in
        # exactly those, so copying them put kern_big's CTRL.DRV on
        # kern_mega's disk (mod_need refused it: no Control Panel). A
        # PACKAGE that differs is the fixture's own (netinst's INSTALL.O88
        # gate), and a driver both disks share is the row's to swap
        # (rdxms's red control's RAMDISK.DRV)
        if rel == "KERNEL.SYS" or (
                rel.endswith((".DRV", ".SYS")) and q is not None
                and rel in megas and body(q) != body(megas[rel])):
            continue
        parts = rel.split(os.sep)
        for i in range(1, len(parts)):
            subprocess.run(["mmd", "-D", "s", "-i", out,
                            "::/" + "/".join(parts[:i])], env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["mcopy", "-o", "-i", out, p, "::/" + "/".join(parts)],
                       check=True, env=env, stdout=subprocess.DEVNULL)
    _TWINS[key] = out
    return out


# --- the server -------------------------------------------------------------

class Server(object):
    """A kern_mega Bochs, and a QMP socket at `sock` that drives it."""

    def __init__(self, sock, pidfile, a, b, hd=None, ne2k=True, cfg=None,
                 label=None, megs=None, sb=False, ne2k_io=None, ps2=False):
        import os88mouse
        self.closed = False
        self.sock, self.pidfile = sock, pidfile
        self.a_row, self.b_row, self.hd_row = a, b, hd
        self.a_img = mega_twin(a)
        self.twinned = self.a_img != a
        machine = ("_ne2k" if ne2k else "") + ("_hdd" if hd else "") + \
            ("_sb" if sb else "")
        if BOX86 and not MEGA:          # kern_big's: a QEMU-shaped AT, or
            machine = ("at386_vga_ps2" if ps2 else  # the PS/2 machine
                       "at286_vga") + machine
            cfg = "keep"                # the row's own disk, as it is
        elif BOX86:                     # QEMU's VGA (86Box's own default
            machine = "xt_vga" + machine    # is MartyPC's, a CGA)
        if cfg is None and self.twinned and _has_cfg(self.a_img):
            cfg = "keep"                # the fixture's SYSTEM.CFG stands
        # THE BACKEND: Bochs, or under $OS88_MEGA_EMU=86box an 86Box
        # (tools/os88mega86box.py), or kern_big's 86Box (os88box86api) -
        # the same launch arguments every way
        self.box86 = BOX86
        if BOX86 and not MEGA:
            import os88box86api as backend
        elif BOX86:
            import os88mega86box as backend
        else:
            import os88megabochs as backend
        # (kern_big's floppies TURBO: the rows were written against QEMU's,
        # which load a package in no time - paced at an AT's real speed a
        # 60KB browser was still coming off the disk when the row looked)
        kw = {"turbo": True} if BOX86 and not MEGA else {}
        self.m = backend.launch(self.a_img, b, machine=machine,
                                boot=True, cfg=cfg, label=label,
                                megs=megs, cimg=hd, ne2k_io=ne2k_io, **kw)
        self.mo = os88mouse.Mouse(marty=self.m)
        if BOX86:
            # PACED, AS QEMU AND BOCHS ARE: a QMP row times its own input on
            # the HOST's clock (a double-click is four `mouse_button`s 0.1 s
            # apart, against a nine-tick window), and 86Box fast-forwarded
            # runs ~10x - so those 0.1 s were a guest second apart
            self.m._a("ff off")
        # THE ROW'S OWN SCRATCH IS BOOTED IN PLACE on QEMU, and a row reads
        # it WHILE the machine runs (brdownload's os88flush.Volume of the B:
        # it saved to). Bochs boots a copy in its run directory, so the row's
        # path is made a link to the live file until close() puts the bytes
        # back as a plain file
        live = self._files()
        for k, dst in (("b", b), ("c", hd), ("a", a if self.twinned else None)):
            src = live.get(k)
            if dst and src and _scratch(dst) and os.path.exists(src):
                os.remove(dst)
                os.symlink(src, dst)
        self._tx = self._ty = None
        self._btn = 0
        self._chain = []                # button edges not yet sent (_button)
        self._chain_t = 0.0
        self._wh = None
        self._lk = threading.RLock()
        self._pid()
        try:
            os.unlink(sock)
        except OSError:
            pass
        self.ls = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.ls.bind(sock)
        self.ls.listen(8)
        threading.Thread(target=self._accept, daemon=True).start()
        if not self.box86:
            threading.Thread(target=self._flusher, daemon=True).start()
        _LIVE.append(self)
        atexit.register(self.close)

    @property
    def box(self):
        return self.m.box               # a NEW Box after a reset

    def _files(self):
        """The LIVE images the guest writes, by drive letter. 86Box mounts
        the row's C: in place (os88mega86box's `cimg`), so it has none."""
        if self.box86:
            return {"a": self.m._live.get(0), "b": self.m._live.get(1)}
        out = self.box.out
        return {k: os.path.join(out, k + ".img") for k in "abc"}

    def _pid(self):
        if self.pidfile:
            with open(self.pidfile, "w") as fh:
                fh.write("%d\n" % self.m.pid)

    # ---- the socket -------------------------------------------------------
    def _accept(self):
        while not self.closed:
            try:
                c, _ = self.ls.accept()
            except OSError:
                return
            threading.Thread(target=self._serve, args=(c,),
                             daemon=True).start()

    def _serve(self, c):
        f = c.makefile("rw")
        try:
            f.write(json.dumps({"QMP": {"version": {"qemu": {
                "major": 8, "minor": 2, "micro": 0}, "package": "bochs"},
                "capabilities": []}}) + "\n")
            f.flush()
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    req = json.loads(line)
                    with self._lk:
                        r = {"return": self._exec(req.get("execute"),
                                                  req.get("arguments") or {})}
                except Exception as e:                      # noqa: BLE001
                    r = {"error": {"class": "GenericError",
                                   "desc": "os88qmpmega: %s" % e}}
                f.write(json.dumps(r) + "\n")
                f.flush()
        except (OSError, ValueError):
            pass
        finally:
            try:
                f.close()
                c.close()
            except OSError:
                pass

    def _exec(self, cmd, args):
        if cmd == "qmp_capabilities":
            return {}
        if cmd == "human-monitor-command":
            return self.hmp(args["command-line"])
        if cmd == "input-send-event":
            for ev in args.get("events", ()):
                self._event(ev)
            return {}
        if cmd not in ("input-send-event", "human-monitor-command"):
            self._flush()               # (both of those decide for themselves)
        if cmd == "query-status":
            st = self.m.status().get("state")
            return {"status": "running" if st == "running" else "paused",
                    "running": st == "running", "singlestep": False}
        if cmd == "screendump":
            self._sync()
            self.screendump(args["filename"])
            return {}
        if cmd == "quit":
            self.close()                # BEFORE the answer: a row that
            return {}                   # boots the same disk next must
                                        # find what this one wrote on it
        if cmd == "system_reset":
            self.hmp("system_reset")
            return {}
        raise ValueError("QMP %r is not answered here" % cmd)

    # ---- HMP --------------------------------------------------------------
    def hmp(self, line):
        line = line.strip()
        w = line.split()
        if not w:
            return ""
        op = w[0]
        if op != "mouse_button":
            self._flush()               # (edges first: a drag's press, then
        if op != "mouse_move":          # the motion it carries)
            if self._tx is not None:
                self._flush()
            self._sync()
        if op == "mouse_move":
            self._move(int(w[1]), int(w[2]))
            return ""
        if op == "mouse_button":
            self._button(int(w[1], 0))
            return ""
        if op == "xp":
            return self._xp(w[1], w[2])
        if op == "pmemsave":
            m = re.match(r"pmemsave\s+(\S+)\s+(\S+)\s+\"?([^\"]+)\"?$", line)
            addr, n = int(m.group(1), 0), int(m.group(2), 0)
            with open(m.group(3), "wb") as fh:
                fh.write(self.m.read(addr, n))
            return ""
        if op == "sendkey":
            names = w[1].split("-")
            for n in names[:-1]:
                self._key(n, True)
            self._key(names[-1], True)
            time.sleep(0.05)
            self._key(names[-1], False)
            for n in reversed(names[:-1]):
                self._key(n, False)
            return ""
        if op == "screendump":
            self.screendump(w[1].strip('"'))
            return ""
        if op == "info" and len(w) > 1 and w[1] == "status":
            st = self.m.status().get("state")
            return "VM status: %s\r\n" % ("running" if st == "running"
                                           else "paused")
        if op == "info" and len(w) > 1 and w[1] == "registers":
            if self.box86:
                return json.dumps(self.m.regs()) + "\r\n"
            return self.m.box and self.m._stopped(["r", "sreg"])
        if op == "stop":
            self.m.pause()
            return ""
        if op in ("cont", "c"):
            self.m.go()
            return ""
        if op == "system_reset":
            self.m.cmd(cmd="reset")     # a power cycle: a new Bochs
            self._wh = None
            self._pid()
            return ""
        if op in ("quit", "q"):
            self.close()
            return ""
        raise ValueError("HMP %r is not answered by the kern_mega Bochs "
                         "(tests/os88qmpmega.py)" % op)

    def _xp(self, fmt, addr):
        # /COUNT then the format and the size letters IN EITHER ORDER, as
        # QEMU takes them (`/2bx` is tests/xmcheck.py's, `/1hx` qmp.py's)
        m = re.match(r"/(\d*)([xducbhwg]*)$", fmt)
        if not m:
            raise ValueError("xp format %r" % fmt)
        n = int(m.group(1) or 1)
        kind = next((c for c in m.group(2) if c in "xduc"), "x")
        size = {"b": 1, "h": 2, "w": 4, "g": 8}[next(
            (c for c in m.group(2) if c in "bhwg"), "w")]
        a = int(addr, 0)
        data = self.m.read(a, n * size)
        per = 8 if size == 1 else 16
        out = []
        for i in range(0, n * size, per):
            row = data[i:i + per]
            vals = []
            for j in range(0, len(row), size):
                v = int.from_bytes(row[j:j + size], "little")
                if kind == "x":
                    vals.append("0x%0*x" % (size * 2, v))
                elif kind == "d":
                    if v >= 1 << (size * 8 - 1):
                        v -= 1 << (size * 8)
                    vals.append("%d" % v)
                elif kind == "u":
                    vals.append("%u" % v)
                else:
                    vals.append("'%s'" % chr(v))
            out.append("%016x: %s" % (a + i, " ".join(vals)))
        return "\r\n".join(out) + "\r\n"

    # ---- input ------------------------------------------------------------
    def _screen(self):
        if self._wh is None:
            import os88sym
            b = self.m.read(os88sym.linear("vid_w"), 2)
            c = self.m.read(os88sym.linear("vid_h"), 2)
            w, h = b[0] | b[1] << 8, c[0] | c[1] << 8
            if not (100 < w <= 2048 and 100 < h <= 2048):
                return (640, 480)       # not up yet: don't keep it
            self._wh = (w, h)
        return self._wh

    def _move(self, dx, dy):
        if self._tx is None:
            self._tx, self._ty, _b = self.mo.where()
        w, h = self._screen()
        self._tx = max(0, min(w - 1, self._tx + dx))
        self._ty = max(0, min(h - 1, self._ty + dy))

    def _sync(self):
        """Drive the guest to the intended position, if one is pending."""
        if self._tx is None:
            return
        x, y = self._tx, self._ty
        self._tx = self._ty = None
        self.mo.to(x, y, l=bool(self._btn & 1), r=bool(self._btn & 2))

    def _button(self, mask):
        was, self._btn = self._btn, mask
        m = self.m
        if self.box86:                  # the serial mouse's own report:
            self._box_edges(was, mask)  # buttons with no motion
            return
        # A BURST OF EDGES GOES TO X AS ONE xdotool CHAIN, not one process
        # an edge. Whatever spaces the edges on the host spaces them in the
        # guest (with sync=realtime it WAS the guest's clock) -
        # and a double-click's four `mouse_button`s (xmcheck.dblclick) each
        # spawning xdotool were 1.15 s press to press on a loaded box, one
        # xdotool taking 0.83 s to start, against FM_DBLCLK's nine ticks:
        # two FIRST clicks, nothing opened (mega-httpinstall, 3 runs in 6
        # with four CPU hogs). So an edge is QUEUED, and the chain goes out
        # at the next command of any other kind - every row's next step is a
        # read or a wait, which needs it sent - or once no edge has followed
        # for HOLD, so a click with only a host sleep after it still lands.
        # In the chain the edges are 30 ms apart, os88mouse's Bochs
        # double-click, so the gesture's span is the chain's and not the
        # host's: one X request stream, read by Bochs in one GUI pass
        for bit, xb in ((1, 1), (2, 3), (4, 2)):
            if (mask ^ was) & bit:
                self._chain.append(("mousedown" if mask & bit else "mouseup",
                                    xb))
        self._chain_t = time.time()

    def _box_edges(self, was, mask):
        """86Box: each changed button an edge the guest has DECODED.

        86Box's automation `mouse` verb SETS the button state and the serial
        mouse reports it at its own poll, so edges sent back to back are
        COALESCED - a double-click's four `mouse_button`s (os88qemu.
        mouse_dblclick sends them in one burst, which is right for QEMU's
        msmouse, a packet queued per edge) reached the guest as no edge at
        all, and box-ethernet's Disk window never opened. So each edge waits
        until the published mouse_btn agrees (os88mouse.Mouse._edge, which
        re-sends a dropped packet), as QEMU's queue would have delivered it.
        A guest that never decodes it is left for the caller's own check to
        name, as QEMU leaves it: this layer does not raise on an edge."""
        from os88marty import MartyError
        for bit in (1, 2):
            if not (mask ^ was) & bit:
                continue
            try:
                self.mo._edge(bool(mask & bit), btn=bit)
            except MartyError:
                pass
            # (_edge's packet carries the OTHER button up: a chord of left
            # and right is not a gesture any row here makes)

    HOLD = 0.3      # s with no further edge before a queued burst goes out:
                    # longer than xmcheck.dblclick's longest gap (0.12 s)

    def _flusher(self):
        while not self.closed:
            time.sleep(0.02)
            if self._chain and time.time() - self._chain_t >= self.HOLD:
                with self._lk:
                    self._flush()

    def _flush(self):
        """Send the queued button edges (see _button) as one chain."""
        with self._lk:
            chain, self._chain = self._chain, []
            if not chain:
                return
            m = self.m
            if not m.box.captured:
                m.box.capture()
            args = []
            # A PS/2 MOUSE TAKES THE BURST QUEUED ON A STOPPED GUEST
            # (os88megabochs.queued_input): one GUI pass, a packet an edge,
            # so its spacing is no host interval at all. A SERIAL one keeps
            # the 30 ms: Bochs's serial mouse latches the LAST button state
            # (iodev/serial.cc mouse_enq), so edges read in one pass merge
            import os88bochs
            queued = len(chain) > 1 and os88bochs.MOUSE != "serial"
            for edge in chain:
                if args and not queued:
                    args += ["sleep", "0.03"]
                args += list(edge)
            with m._lock:
                if queued:
                    m.queued_input(*args)
                else:
                    m._xd(*args)
                for ed, xb in chain:
                    if ed == "mousedown":
                        m._btn.add(xb)
                    else:
                        m._btn.discard(xb)

    def _key(self, q, down):
        self._flush()                   # (in the order they were sent)
        m = self.m
        if self.box86:
            m.cmd(cmd="key", key=martykey(q), down=down, up=not down)
            return
        if not m.box.captured:
            m.box.capture()
        k = keysym(q)
        with m._lock:
            m._xd("keydown" if down else "keyup", k)
            (m._keys.add if down else m._keys.discard)(k)

    def _event(self, ev):
        t, d = ev.get("type"), ev.get("data", {})
        if t == "key":
            k = d["key"]
            if k.get("type") != "qcode":
                raise ValueError("a key event by %s" % k.get("type"))
            self._flush()
            self._sync()
            self._key(k["data"], d.get("down", True))
        elif t == "btn":
            bit = {"left": 1, "right": 2, "middle": 4}[d["button"]]
            if self._tx is not None:
                self._flush()
            self._sync()
            self._button((self._btn | bit) if d.get("down") else
                         (self._btn & ~bit))
        elif t == "rel":
            v = int(d.get("value", 0))
            if d.get("axis") == "x":
                self._move(v, 0)
            elif d.get("axis") == "y":
                self._move(0, v)
        else:
            raise ValueError("input event %r" % t)

    # ---- the glass --------------------------------------------------------
    def screendump(self, path):
        w, h, px = self.m.fbuf()
        with open(path, "wb") as fh:
            fh.write(b"P6\n%d %d\n255\n" % (w, h))
            fh.write(px)

    # ---- the end ----------------------------------------------------------
    def close(self):
        if self.closed:
            return
        self.closed = True
        try:
            self.ls.close()
        except OSError:
            pass
        live = self._files()
        try:
            if self.box86:
                self.m.pause()          # 86Box writes through: held is enough
            else:
                self.box.stop()         # Bochs GONE before its disks are read
        except Exception:                                   # noqa: BLE001
            pass
        # WHAT THE GUEST WROTE goes back where the row booted it from: QEMU
        # writes its image in place, the shim booted a copy. A twin of a
        # system-disk fixture is this process's own (mega_twin), so a row
        # that boots the same fixture again (ethcfg's reboot) meets what the
        # guest saved on it
        for src, dst in ((live.get("b"), self.b_row),
                         (live.get("a"),
                          self.a_img if self.twinned else self.a_row),
                         (live.get("a"),
                          self.a_row if self.twinned else None),
                         (live.get("c"), self.hd_row)):
            if dst and src and os.path.exists(src) and _scratch(dst):
                try:
                    if os.path.islink(dst):
                        os.remove(dst)
                    shutil.copyfile(src, dst)
                except OSError:
                    pass
        try:
            self.m.close()
        except Exception:                                   # noqa: BLE001
            pass
        for f in (self.sock, self.pidfile):
            try:
                os.unlink(f)
            except (OSError, TypeError):
                pass


def _has_cfg(img):
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    return subprocess.run(["mdir", "-b", "-i", img, "::/SYSTEM.CFG"], env=env,
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode == 0


def _scratch(path):
    """A path a row may have its guest write to: never an image in build/
    itself, in the run's tree or in any tree under build/trees/ - those are
    every row's (os88build.shared_dir).

    **IT WAS `build/` ALONE, and under a frozen soak that is no test at all**
    (docs/plans/SOAK-PARALLEL.md 17.13): a row is handed the TREE's
    `vesa.img`, so this answered "scratch", the path was made a symlink into
    Bochs's run directory for the whole boot, and close() copied kern_mega's
    A: back over it. Every kern_big row that booted that fixture afterwards
    booted kern_mega under QEMU - `vid_kind 134 ... DRVR_ERR 139`, garbage
    read through kern_big's map - and passed alone, where nothing is frozen
    and the old test happened to be right."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88build
    p = os.path.realpath(path)
    return not os88build.shared_dir(os.path.dirname(p))
