#!/usr/bin/env python3
"""Drive 86Box headless through its automation socket (docs/86BOX.md).

    os88box.py build                       tools/86box/build.sh: 86Box at the
                                           pin, with AUTOMATION and GDBSTUB on
    os88box.py launch VM [opts]            boot a machine and leave it running
    os88box.py list                        what is running
    os88box.py NAME status|info|regs       one line of JSON each
    os88box.py NAME step MS                run MS of emulated time
    os88box.py NAME shot OUT.png           the last frame, as the card drew it
    os88box.py NAME watch SECS EVERY OUT   a contact sheet: one frame each
                                           EVERY emulated seconds for SECS
    os88box.py NAME key KEY [KEY...]       tap keys: return, f10, ctrl+alt+del,
                                           kp_8, up, a, ... (names in KEYS)
    os88box.py NAME type TEXT              type a string
    os88box.py NAME mouse DX DY [BUTTONS]  relative motion (1 left, 2 right)
    os88box.py NAME click [N]              N clicks of the left button
    os88box.py NAME to X Y                 put os8088's pointer at (X, Y),
                                           closing the loop on the kernel's
                                           own mouse_x/mouse_y
    os88box.py NAME pointer | sym NAME     where it is; a symbol's address
    os88box.py NAME memr ADDR LEN          physical memory, hex
    os88box.py NAME wait-text MS TEXT      run until TEXT is on a text screen
    os88box.py NAME fdd DRIVE IMG          change a floppy (0 = A:)
    os88box.py NAME reset [hard|cad]       a hard reset, or Ctrl+Alt+Del
    os88box.py NAME faults [--all] [--map KERNEL.map]
                                           the CPU's exceptions and resets
                                           (patches/01-fault-log.patch)
    os88box.py NAME raw 'COMMAND'          any automation request
    os88box.py NAME quit                   power off; saves the CMOS

launch options:
    VM          a vm/ directory name (286, pc5150, 386-ps2, ...), a
                directory holding 86box.cfg, or a .cfg file
    --name N    the instance's name (default: VM's), and its run directory
                build/86box/run/N/ - it persists, so a CMOS set up once in
                BIOS setup is there next time (quit, do not kill)
    --a/--b/--c/--d IMG
                floppy A:, B: and hard disks 1 and 2, in place of the
                config's. Every image is COPIED into the run directory, so a
                guest write never reaches build/; --inplace mounts them as
                they are
    --cd IMG    the first CD-ROM drive's disc (.iso or .cue), in place of the
                config's - resolved in place, never copied
    --mem KB    mem_size
    --set SECTION.KEY=VALUE
                any other config line (repeatable)
    --hold      start held: no emulated time passes until a step
    --window    a real window on $DISPLAY instead of Qt's offscreen platform
    --fresh     delete the run directory first (and so its CMOS)
    --clear-cmos
    --map M     the kernel's nasm map, for faults/to/sym (default: the
                kern_mega map for an A: image named mega*, else build/'s)
    --gdb-wait  hold the CPU at reset until a GDB client attaches and
                continues (the stub always listens, on the port printed)

86Box's automation protocol is upstream's (doc/specifications/automation.md
in the 86Box tree). This tool adds three things to it: preparing a vm/ config
to run off this tree's images, names for keys, and reading the fault log the
os8088 patch writes - which is how a triple fault that 86Box would otherwise
answer with a silent reboot becomes a CS:IP.
"""
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build", "86box")
BIN = os.environ.get("OS88_86BOX", os.path.join(BUILD, "86Box"))
ROMS = os.environ.get("OS88_86BOX_ROMS", os.path.join(BUILD, "roms"))
RUNS = os.path.join(BUILD, "run")


# --- keys ---------------------------------------------------------------------
# Set-1 make codes, as the protocol wants them; 0x100 marks an E0 key.
KEYS = {}
for i, ch in enumerate("1234567890-="):
    KEYS[ch] = 0x02 + i
for row, base in (("qwertyuiop[]", 0x10), ("asdfghjkl;'`", 0x1E), ("zxcvbnm,./", 0x2C)):
    for i, ch in enumerate(row):
        KEYS[ch] = base + i
KEYS.update({
    "\\": 0x2B, "esc": 0x01, "escape": 0x01, "backspace": 0x0E, "tab": 0x0F,
    "return": 0x1C, "enter": 0x1C, "ctrl": 0x1D, "control": 0x1D,
    "shift": 0x2A, "lshift": 0x2A, "rshift": 0x36, "alt": 0x38, "space": 0x39,
    "capslock": 0x3A, "numlock": 0x45, "scrolllock": 0x46, "kp_multiply": 0x37,
    "kp_7": 0x47, "kp_8": 0x48, "kp_9": 0x49, "kp_minus": 0x4A, "kp_4": 0x4B,
    "kp_5": 0x4C, "kp_6": 0x4D, "kp_plus": 0x4E, "kp_1": 0x4F, "kp_2": 0x50,
    "kp_3": 0x51, "kp_0": 0x52, "kp_period": 0x53,
    "kp_home": 0x47, "kp_up": 0x48, "kp_pgup": 0x49, "kp_left": 0x4B,
    "kp_right": 0x4D, "kp_end": 0x4F, "kp_down": 0x50, "kp_pgdn": 0x51,
    "kp_ins": 0x52, "kp_del": 0x53,
    "up": 0x148, "down": 0x150, "left": 0x14B, "right": 0x14D,
    "home": 0x147, "end": 0x14F, "pgup": 0x149, "pageup": 0x149,
    "pgdn": 0x151, "pagedown": 0x151, "insert": 0x152, "ins": 0x152,
    "delete": 0x153, "del": 0x153, "rctrl": 0x11D, "ralt": 0x138,
    "kp_enter": 0x11C,
})
for i in range(10):
    KEYS["f%d" % (i + 1)] = 0x3B + i
KEYS["f11"], KEYS["f12"] = 0x57, 0x58
SHIFTED = dict(zip('!@#$%^&*()_+{}|:"~<>?', "1234567890-=[]\\;'`,./"))


def keycode(name):
    n = name.lower()
    if n in KEYS:
        return KEYS[n]
    if n.startswith("0x"):
        return int(n, 16)
    raise SystemExit("os88box: no key named %r (see KEYS in %s)" % (name, __file__))


# --- the client ---------------------------------------------------------------
class Box(object):
    """One running 86Box. Every request is a line; every answer a JSON line."""

    def __init__(self, name):
        self.name = name
        self.run = os.path.join(RUNS, name)
        self.sock = os.path.join(self.run, "auto.sock")
        self._s = None
        self._f = None

    def _conn(self):
        if self._s is None:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                s.connect(self.sock)
            except OSError as e:
                raise SystemExit("os88box: %s is not running (%s): %s" % (self.name, self.sock, e))
            self._s, self._f = s, s.makefile("r")
        return self._f

    def close(self):
        if self._s is not None:
            self._s.close()
            self._s = self._f = None

    def cmd(self, line):
        f = self._conn()
        self._s.sendall((line + "\n").encode())
        reply = f.readline()
        if not reply:
            self.close()
            raise SystemExit("os88box: %s closed the connection on %r" % (self.name, line))
        r = json.loads(reply)
        if not r.get("ok"):
            raise SystemExit("os88box: %r -> %s" % (line, r.get("error")))
        return r

    # convenience ---------------------------------------------------------------
    def held(self):
        return bool(self.cmd("status").get("held"))

    def pause(self, ms):
        """ms of emulated time: a step when held, a host sleep when running
        (the machine paces itself to real time then)."""
        if self.held():
            self.cmd("step %d" % ms)
        else:
            time.sleep(ms / 1000.0)

    def shot(self, path):
        return self.cmd("screenshot %s" % os.path.abspath(path))

    def tap(self, combo, gap=60):
        codes = [keycode(k) for k in combo.split("+")] if combo != "+" else [0x0D]
        self.cmd("key down " + " ".join("0x%X" % c for c in codes))
        self.cmd("key up " + " ".join("0x%X" % c for c in reversed(codes)))
        self.pause(gap)

    def type(self, text, gap=40):
        for ch in text:
            if ch == " ":
                self.tap("space", gap)
            elif ch == "\n":
                self.tap("return", gap)
            elif ch.isupper():
                self.tap("shift+" + ch.lower(), gap)
            elif ch in SHIFTED:
                self.tap("shift+" + SHIFTED[ch], gap)
            else:
                self.tap(ch, gap)

    def mouse(self, dx, dy, buttons=0):
        return self.cmd("mouse %d %d %d" % (dx, dy, buttons))

    def click(self, n=1, button=1, hold_ms=80, gap_ms=120):
        for i in range(n):
            self.mouse(0, 0, button)
            self.pause(hold_ms)
            self.mouse(0, 0, 0)
            if i + 1 < n:
                self.pause(gap_ms)

    def memr(self, addr, n):
        return bytes.fromhex(self.cmd("memr 0x%X %d" % (addr, n))["data"])

    def kmap(self):
        """The nasm map of the kernel this instance booted (state.json's
        `map`, chosen at launch from the A: image's name, or --map)."""
        st = state(self.name) or {}
        return st.get("map")

    def ksym(self, sym):
        """A .text/.bss kernel symbol's LINEAR address: KERNEL_SEG x 16 + its
        offset. Not .cold or .lowbss - tools/os88sym.py is the full answer."""
        m = self.kmap()
        if not m or not os.path.isfile(m):
            raise SystemExit("os88box: no kernel map for %s (launch --map)" % self.name)
        for sec in (".text", ".bss"):
            for a, n in load_map(m).get(sec, []):
                if n == sym:
                    return 0x600 + a
        raise SystemExit("os88box: %s is not a .text/.bss symbol in %s" % (sym, m))

    def pointer(self):
        b = self.memr(self.ksym("mouse_x"), 4)
        return b[0] | b[1] << 8, b[2] | b[3] << 8

    def to(self, x, y, tries=40, buttons=0):
        """Move os8088's pointer to (x, y) by relative packets, reading its
        position back out of the kernel each round - 86Box's serial mouse is
        relative and the OS may scale it, so the loop closes on the guest.
        BUTTONS are held throughout: 1 drags down a pull-down menu."""
        for _ in range(tries):
            px, py = self.pointer()
            dx, dy = x - px, y - py
            if dx == 0 and dy == 0:
                return px, py
            step = lambda v: max(-100, min(100, v))
            self.mouse(step(dx), step(dy), buttons)
            self.pause(60)
        raise SystemExit("os88box: pointer stuck at %r on the way to %r" % (self.pointer(), (x, y)))

    def watch(self, secs, every, out):
        """Frames every `every` emulated seconds, montaged into `out` when
        ImageMagick is there (frames kept beside it as OUT-NNN.png)."""
        stem = os.path.splitext(os.path.abspath(out))[0]
        frames = []
        for i in range(int(secs / every)):
            self.pause(int(every * 1000))
            p = "%s-%03d.png" % (stem, i)
            self.shot(p)
            frames.append(p)
        if shutil.which("montage"):
            subprocess.call(["montage"] + frames + ["-tile", "5x", "-geometry", "320x240+2+2", out])
        return frames


# --- preparing a machine ------------------------------------------------------
def read_cfg(path):
    secs, order, cur = {}, [], None
    for line in open(path, encoding="utf-8-sig"):
        line = line.rstrip("\r\n")
        m = re.match(r"^\[(.*)\]$", line)
        if m:
            cur = m.group(1)
            if cur not in secs:
                secs[cur] = []
                order.append(cur)
            continue
        if cur is None or "=" not in line:
            continue
        k, v = line.split("=", 1)
        secs[cur].append([k.strip(), v.strip()])
    return order, secs


def cfg_set(order, secs, section, key, value):
    if section not in secs:
        secs[section] = []
        order.append(section)
    for kv in secs[section]:
        if kv[0] == key:
            if value is None:
                secs[section].remove(kv)
            else:
                kv[1] = value
            return
    if value is not None:
        secs[section].append([key, value])


def cfg_get(secs, section, key):
    for k, v in secs.get(section, []):
        if k == key:
            return v
    return None


def write_cfg(path, order, secs):
    with open(path, "w") as f:
        for s in order:
            f.write("[%s]\n" % s)
            for k, v in secs[s]:
                f.write("%s = %s\n" % (k, v))
            f.write("\n")


def find_cfg(vm):
    for cand in (vm, os.path.join(vm, "86box.cfg"),
                 os.path.join(ROOT, "vm", vm, "86box.cfg")):
        if os.path.isfile(cand):
            return os.path.abspath(cand)
    raise SystemExit("os88box: no 86box.cfg for %r (a vm/ name, a directory, or a file)" % vm)


def mount(order, secs, srcdir, run, slot, section, key, given, inplace):
    """Point a drive at an image: `given` if passed, else the config's own,
    resolved against the config's directory. Copied in unless inplace."""
    path = given or cfg_get(secs, section, key)
    if not path:
        return None
    path = path[5:] if path.startswith("wp://") else path
    if not given:
        path = os.path.normpath(os.path.join(srcdir, path))
    if not os.path.isfile(path):
        print("os88box: %s: %s does not exist here - drive left empty" % (key, path), file=sys.stderr)
        cfg_set(order, secs, section, key, None)
        return None
    if not inplace:
        dst = os.path.join(run, slot + os.path.splitext(path)[1])
        shutil.copyfile(path, dst)
        path = dst
    cfg_set(order, secs, section, key, os.path.abspath(path))
    return path


def alive(pid):
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    try:
        with open("/proc/%d/cmdline" % pid, "rb") as f:
            return b"86Box" in f.read()
    except OSError:
        return True


def state(name):
    p = os.path.join(RUNS, name, "state.json")
    try:
        return json.load(open(p))
    except (OSError, ValueError):
        return None


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def launch(vm, name=None, a=None, b=None, c=None, d=None, mem=None, sets=(),
           hold=False, window=False, fresh=False, clear_cmos=False, inplace=False,
           gdb_wait=False, kmap=None, cd=None):
    if not os.path.isfile(BIN):
        raise SystemExit("os88box: no 86Box at %s - run `python3 tools/os88box.py build` "
                         "(or set OS88_86BOX)" % BIN)
    if not os.path.isdir(ROMS):
        raise SystemExit("os88box: no ROM set at %s (OS88_86BOX_ROMS)" % ROMS)
    src = find_cfg(vm)
    name = name or os.path.basename(os.path.dirname(src)) or "box"
    run = os.path.join(RUNS, name)
    st = state(name)
    if st and alive(st["pid"]):
        raise SystemExit("os88box: %s is already running (pid %d) - quit it first" % (name, st["pid"]))
    if fresh and os.path.isdir(run):
        shutil.rmtree(run)
    os.makedirs(run, exist_ok=True)
    order, secs = read_cfg(src)
    srcdir = os.path.dirname(src)
    for sec in list(secs):                       # history is the host's, not ours
        secs[sec] = [kv for kv in secs[sec] if "image_history" not in kv[0]]
    a_name = os.path.basename(a or cfg_get(secs, "Floppy and CD-ROM drives", "fdd_01_fn") or "")
    mount(order, secs, srcdir, run, "fdd1", "Floppy and CD-ROM drives", "fdd_01_fn", a, inplace)
    mount(order, secs, srcdir, run, "fdd2", "Floppy and CD-ROM drives", "fdd_02_fn", b, inplace)
    for n in range(3, 5):
        mount(order, secs, srcdir, run, "fdd%d" % n, "Floppy and CD-ROM drives",
              "fdd_%02d_fn" % n, None, inplace)
    mount(order, secs, srcdir, run, "hdd1", "Hard disks", "hdd_01_fn", c, inplace)
    mount(order, secs, srcdir, run, "hdd2", "Hard disks", "hdd_02_fn", d, inplace)
    # The CD-ROM's image is resolved and NOT copied: a disc is read-only, and a
    # .cue names its .bin relative to itself. Left relative, 86Box resolves it
    # against the RUN directory, finds nothing and drops the drive's image
    # (and its type and speed) without a word - which is how `386-cdrom`'s
    # disc went missing here
    for n in range(1, 5):
        key = "cdrom_%02d_image_path" % n
        path = (cd if n == 1 else None) or cfg_get(secs, "Floppy and CD-ROM drives", key)
        if not path:
            continue
        if not (cd and n == 1):
            path = os.path.normpath(os.path.join(srcdir, path))
        if not os.path.isfile(path):
            print("os88box: %s: %s does not exist here - drive left empty" % (key, path),
                  file=sys.stderr)
            cfg_set(order, secs, "Floppy and CD-ROM drives", key, None)
            continue
        cfg_set(order, secs, "Floppy and CD-ROM drives", key, os.path.abspath(path))
    for k in ("confirm_exit", "confirm_reset", "confirm_save"):
        cfg_set(order, secs, "General", k, "0")
    # No GL in a container, and an offscreen platform has no GL context.
    cfg_set(order, secs, "General", "vid_renderer", "qt_software")
    # Each instance its own GDB port (loopback; patches/02), so two never meet.
    gdb = free_port()
    cfg_set(order, secs, "General", "gdbstub_port", str(gdb))
    if mem:
        cfg_set(order, secs, "Machine", "mem_size", str(mem))
    for s in sets:
        m = re.match(r"^([^.=]+(?: [^.=]+)*)\.([^=]+)=(.*)$", s)
        if not m:
            raise SystemExit("os88box: --set wants SECTION.KEY=VALUE, got %r" % s)
        cfg_set(order, secs, m.group(1), m.group(2), m.group(3) or None)
    cfg = os.path.join(run, "86box.cfg")
    write_cfg(cfg, order, secs)
    gcfg = os.path.join(run, "global.cfg")
    with open(gcfg, "w") as f:
        f.write("[General]\nconfirm_exit = 0\nconfirm_reset = 0\nconfirm_save = 0\n")
    sock = os.path.join(run, "auto.sock")
    if os.path.exists(sock):
        os.unlink(sock)
    env = dict(os.environ, OS88_FAULTLOG="1")
    if gdb_wait:
        env["OS88_GDBWAIT"] = "1"
    if not window:
        env["QT_QPA_PLATFORM"] = "offscreen"
    rt = env.get("XDG_RUNTIME_DIR") or "/tmp/runtime-%d" % os.getuid()
    os.makedirs(rt, mode=0o700, exist_ok=True)
    env["XDG_RUNTIME_DIR"] = rt
    args = [BIN, "-N", "--assume_copied_vm", "-P", run, "-R", ROMS, "-O", gcfg,
            "-L", os.path.join(run, "86box.log"), "--automation", "unix:" + sock]
    if hold:
        args.append("--automation-hold")
    if clear_cmos:
        args += ["-X", "cmos"]
    args.append(cfg)
    err = open(os.path.join(run, "stderr.log"), "w")
    p = subprocess.Popen(args, cwd=run, env=env, stdout=err, stderr=subprocess.STDOUT,
                         stdin=subprocess.DEVNULL, start_new_session=True)
    if not kmap:
        kdir = "megak" if a_name.startswith("mega") else ""
        kmap = os.path.join(ROOT, "build", kdir, "kernel.map")
    json.dump({"pid": p.pid, "vm": src, "started": time.time(), "gdb": gdb,
               "map": os.path.abspath(kmap)},
              open(os.path.join(run, "state.json"), "w"))
    t0 = time.time()
    while not os.path.exists(sock):
        if p.poll() is not None:
            raise SystemExit("os88box: 86Box exited at once (%d); see %s/stderr.log and 86box.log"
                             % (p.returncode, run))
        if time.time() - t0 > 30:
            raise SystemExit("os88box: no automation socket after 30s; see %s/86box.log" % run)
        time.sleep(0.2)
    box = Box(name)
    box.cmd("status")
    return box


# --- the fault log ------------------------------------------------------------
FAULT = re.compile(r"OS88ABRT (\d+) at ([0-9A-F]{4}):([0-9A-F]{4}) err ([0-9A-F]{4}) msw ([0-9A-F]+)"
                   r"(?: ss ([0-9A-F]{4}) sp ([0-9A-F]{4}) ds ([0-9A-F]{4}) es ([0-9A-F]{4}))?")
WHY = re.compile(r"OS88WHY #\w+ err [0-9A-F]{4} (.*)")
RESET = re.compile(r"OS88SOFTRESET at ([0-9A-F]{4}):([0-9A-F]{4}) msw ([0-9A-F]+)")
NAMES = {0: "#DE", 1: "#DB", 3: "#BP", 5: "#BR", 6: "#UD", 7: "#NM", 8: "#DF",
         10: "#TS", 11: "#NP", 12: "#SS", 13: "#GP", 14: "#PF"}


def load_map(path):
    """A nasm map's symbol tables: {section: [(vaddr, name), ...] sorted}."""
    syms, sec = {}, None
    lines = open(path).read().splitlines()
    for i, l in enumerate(lines):
        m = re.match(r"^---- Section (\S+) ", l)
        if m:
            sec = m.group(1) if i + 2 < len(lines) and lines[i + 2].startswith("Real") else None
            continue
        if sec:
            m = re.match(r"\s*[0-9A-F]+\s+([0-9A-F]+)\s+(\S+)$", l)
            if m:
                syms.setdefault(sec, []).append((int(m.group(1), 16), m.group(2)))
    for v in syms.values():
        v.sort()
    return syms


def resolve(syms, cs, ip, segmap):
    sec = segmap.get(cs)
    if sec and sec.endswith(".map"):              # a package's map: its .text
        syms, sec = load_map(sec), ".text"
    if not syms or not sec or sec not in syms:
        return ""
    best = None
    for a, n in syms[sec]:
        if a > ip:
            break
        best = (a, n)
    return "%s+%X" % (best[1], ip - best[0]) if best else ""


def faults(name, show_all=False, mapfile=None, segs=()):
    log = os.path.join(RUNS, name, "stderr.log")
    syms = load_map(mapfile) if mapfile else None
    # The kernel's own code: KERNEL_SEG real-mode, its identity GDT selector,
    # and kern_mega's bootstrap alias MEGA_SEL_BCODE. Others by --seg.
    segmap = {0x0060: ".text", 0x0028: ".text"}
    for s in segs:                                # HEXCS=.section, or
        k, v = s.split("=", 1)                    # HEXCS=a/package.map for
        segmap[int(k, 16)] = v                    # a package's own code
    last, hidden, why = None, 0, ""
    for line in open(log, errors="replace"):
        w = WHY.search(line)
        if w:                                     # 86Box's own reason: it
            why = w.group(1).strip()              # precedes its fault line
            continue
        m = FAULT.search(line)
        if m:
            vec, cs, ip = int(m.group(1)), int(m.group(2), 16), int(m.group(3), 16)
            last = line
            reason, why = why, ""
            if cs >= 0xF000 and not show_all:     # the BIOS's own: an AT ROM
                hidden += 1                       # leaves protected mode (its
                continue                          # int 15h block move) by one
            where = resolve(syms, cs, ip, segmap)
            print("%-4s %04X:%04X err %s %s%s%s%s" % (
                NAMES.get(vec, "#%d" % vec), cs, ip, m.group(4),
                "PM " if int(m.group(5), 16) & 1 else "RM ",
                ("ss:sp %s:%s ds %s es %s " % m.group(6, 7, 8, 9)) if m.group(6) else "",
                where, ("  [%s]" % reason) if reason else ""))
            continue
        m = RESET.search(line)
        if m:
            cs = int(m.group(1), 16)
            if cs >= 0xF000 and not show_all:
                hidden += 1
                continue
            print("RESET at %s:%s%s" % (m.group(1), m.group(2),
                  ("   <- after: " + last.strip()) if last else ""))
    if hidden:
        print("(%d BIOS-side faults and resets at F000:xxxx hidden - --all shows them)" % hidden)


# --- the command line ---------------------------------------------------------
def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    if argv[0] == "build":
        return subprocess.call([os.path.join(ROOT, "tools", "86box", "build.sh")])
    if argv[0] == "list":
        for n in sorted(os.listdir(RUNS)) if os.path.isdir(RUNS) else ():
            st = state(n)
            if st:
                print("%-16s %s  pid %d  %s" % (n, "running" if alive(st["pid"]) else "stopped",
                                                st["pid"], st["vm"]))
        return 0
    if argv[0] == "launch":
        import argparse
        ap = argparse.ArgumentParser(prog="os88box.py launch")
        ap.add_argument("vm")
        for o in ("--name", "--a", "--b", "--c", "--d", "--map", "--cd"):
            ap.add_argument(o)
        ap.add_argument("--mem", type=int)
        ap.add_argument("--set", action="append", default=[])
        for o in ("--hold", "--window", "--fresh", "--clear-cmos", "--inplace", "--gdb-wait"):
            ap.add_argument(o, action="store_true")
        o = ap.parse_args(argv[1:])
        box = launch(o.vm, o.name, o.a, o.b, o.c, o.d, o.mem, o.set, o.hold, o.window,
                     o.fresh, o.clear_cmos, o.inplace, o.gdb_wait, o.map,
                     os.path.abspath(o.cd) if o.cd else None)
        print("os88box: %s running, %s, gdb on 127.0.0.1:%d" % (box.name, box.run, state(box.name)["gdb"]))
        print(json.dumps(box.cmd("info")))
        return 0

    name, verb, rest = argv[0], (argv[1] if len(argv) > 1 else "status"), argv[2:]
    box = Box(name)
    if verb in ("status", "info", "regs", "hold", "release", "text"):
        print(json.dumps(box.cmd(verb)))
    elif verb == "step":
        print(json.dumps(box.cmd("step %d" % int(rest[0]))))
    elif verb == "shot":
        print(json.dumps(box.shot(rest[0])))
    elif verb == "watch":
        for p in box.watch(float(rest[0]), float(rest[1]), rest[2]):
            pass
        print("os88box: %s" % rest[2])
    elif verb == "key":
        for k in rest:
            box.tap(k)
    elif verb == "type":
        box.type(" ".join(rest))
    elif verb == "mouse":
        print(json.dumps(box.mouse(int(rest[0]), int(rest[1]), int(rest[2]) if len(rest) > 2 else 0)))
    elif verb == "to":
        print(box.to(int(rest[0]), int(rest[1])))
    elif verb == "pointer":
        print(box.pointer())
    elif verb == "sym":
        print("0x%05X" % box.ksym(rest[0]))
    elif verb == "click":
        box.click(int(rest[0]) if rest else 1)
    elif verb == "memr":
        print(box.memr(int(rest[0], 0), int(rest[1], 0)).hex())
    elif verb == "wait-text":
        print(json.dumps(box.cmd("wait text %d %s" % (int(rest[0]), " ".join(rest[1:])))))
    elif verb == "fdd":
        print(json.dumps(box.cmd("fdd load %d %s" % (int(rest[0]), os.path.abspath(rest[1])))))
    elif verb == "reset":
        print(json.dumps(box.cmd("reset %s" % (rest[0] if rest else "hard"))))
    elif verb == "faults":
        import argparse
        ap = argparse.ArgumentParser(prog="os88box.py NAME faults")
        ap.add_argument("--all", action="store_true")
        ap.add_argument("--map")
        ap.add_argument("--seg", action="append", default=[],
                        help="HEXCS=.section (a COLD_SEG selector), or HEXCS=PKG.map for a "
                             "package's code alias: build/video.map")
        o = ap.parse_args(rest)
        faults(name, o.all, o.map or box.kmap(), o.seg)
    elif verb == "raw":
        print(json.dumps(box.cmd(" ".join(rest))))
    elif verb == "quit":
        st = state(name)
        try:
            box.cmd("quit")
        except SystemExit:
            pass
        if st:
            for _ in range(50):
                if not alive(st["pid"]):
                    break
                time.sleep(0.1)
            else:
                os.kill(st["pid"], signal.SIGTERM)
    else:
        raise SystemExit("os88box: no verb %r (see --help)" % verb)
    box.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
