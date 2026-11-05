#!/usr/bin/env python3
"""MOUSECHK's gate (SPEC.md 126.10) - python3 tests/mousechk.py [-k leg] [--keep]

Mouse Check (apps/mousechk/, SPEC.md 126) shows every auto-detected value for
every mouse the OS supports. This row boots kern_big under QEMU with the
package's own AUTOSTRT link on the system floppy, so the window is open and
front before the first paint, and reads what the window SAYS out of the
package's own row list in guest memory (mc_nrows, mc_roff, mc_buf - SPEC.md
126.2) rather than out of pixels. The map of those names comes from nasm: the
package is assembled a second time with `[map symbols]` (tests/paintheld.py's
way) and the result is held to the shipped build/mousechk.bin's length.

THE LEGS (each its own QEMU; `-k` picks one, several `-k` pick several):

  serial   the default machine: a serial mouse on COM1. The window opens, the
           Summary names the serial mouse and COM1, and the rows REDRAW when
           the mouse moves (S2: a live value changes under the pointer)
  com2     MOUSEPORT=com2's wiring (a silent UART at 3F8, the mouse at 2F8):
           the Summary names COM2
  ps2      MOUSEPORT=ps2's wiring (no UART at all): the Summary names PS/2, and
           Probe PS/2 is REFUSED - greyed, [mc_pgrey] = FF - and pressing P
           leaves no trace
  nops2    a PRIVATE kernel tree built with NOPS2=1 (SPEC.md 9.9's auxiliary-
           port probe left out), booted with a PS/2 mouse: Probe PS/2 runs
           and its trace shows the device answering; afterwards the keyboard
           still works (S opens the Save dialog) and the kernel's PS/2 state,
           where the build has one, reads as it did before the probe
  save     Save Report onto a writable scratch B: or A: (the dialog's Enter
           accepts the pre-filled MOUSECHK.TXT), the file read back on the
           host, its head and a section line asserted
  mono     a PRIVATE kernel tree built with VIDEO=cga: [mc_bpp] = 1, the window
           opens and the rows are there. The lamp SHAPES (SPEC.md 118.5) are
           held to by a screendump the row keeps (--keep) for a human; see
           126.10 for why it is not asserted pixel by pixel

WHAT THIS CANNOT SHOW: an 8088's speed (QEMU is exact about work and useless
about time), a real STPC / W83977 controller, MartyPC's XT (no 8042 at all),
and "nothing else redrew" - only that the rows changed.

Every string the row expects is in ONE table, EXPECT, just below.
"""
import argparse
import atexit
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

# --- THE EXPECTED STRINGS - one table, the lead's to update at integration ----
# Each is a regex, matched case-insensitively against the window's rows with
# runs of blanks squeezed to one ("Mouse type    serial" -> "Mouse type serial")
# and the rows joined by newlines. `None` = not wired up yet (the leg then
# only asserts that the window and its rows exist, and says so).
EXPECT = {
    # the foundation's stub, which proves the plumbing (removed once `other`'s
    # Summary lands; the row accepts either, see stub_or)
    "stub": r"sections not built yet",
    "window_title": "Mouse Check",
    # SPEC.md 126.5, summary.inc (owner `other`): the verdict names the mouse
    "sum_serial": r"serial.*COM1|COM1.*serial",
    "sum_com2": r"serial.*COM2|COM2.*serial",
    "sum_ps2": r"PS/2",
    # SPEC.md 126.8, ps2.inc/probe.inc (owner `ps2`): the live probe's trace -
    # the device answered (ACK FA / its id), after which the verdict row says
    # so. 126.7's boot record has its own heading, which a no-probe run shows
    "ps2_head": r"PS/2",
    "ps2_trace": r"(answer|ACK|\bFA\b)",
    # SPEC.md 126.6, serial.inc (owner `serial`)
    "ser_head": r"serial",
    # the report file (mousechk.asm mc_r_title / mc_r_kid)
    "rep_head": r"^Mouse Check report, os8088 package build \d+",
    "rep_date": r"^Date \d{4}-\d\d-\d\d \d\d:\d\d",
    "rep_kid": r"^Kernel id [0-9A-F]{4} [0-9A-F]{4} [0-9A-F]{4}",
    "rep_section": r"^\[(INFO|OK|WARN|PROBLEM|N/A)\] Summary",
    # Probe's toast when refused is p2_canprobe's reason (<= 24 characters);
    # only its existence is asserted
}

# kern_big's 'MO' block names (SPEC.md 9.4.2, 9.9): read when the build has them
P2_SYMS = ("mou_p2st", "mou_p2", "mou_p2id")

# what a private tree builds: the 1.44MB system disk (the 360KB one does not
# carry MOUSECHK, SPEC.md 24.3.1.7) and the package, whose 'ID' fingerprint
# is made against THAT tree's kernel
TREE_GOALS = ("os8088.img", "mousechk.o88")
BOOT_SECS = 120.0
SEEN = []                        # files and notes to report on exit


def say(msg):
    print("mousechk: " + msg, flush=True)


# --- the package's own symbols ------------------------------------------------
def pkgmap():
    """{name: offset in the package image} out of nasm's own map, for the
    package as it ships (tests/paintheld.py's recipe, with build/ on the
    include path for mousechk_kid.inc)."""
    import os88build
    d = tempfile.mkdtemp(prefix="mchk")
    try:
        w = os.path.join(d, "w.asm")
        mp = os.path.join(d, "m.map")
        src = os.path.join(ROOT, "apps", "mousechk", "mousechk.asm")
        with open(w, "w") as f:
            f.write('[map symbols %s]\n%%include "%s"\n' % (mp, src))
        bdir = os.path.dirname(os.path.join(ROOT, os88build.at("build/x")))
        r = subprocess.run(["nasm", "-f", "bin", "-w+error", "-I",
                            os.path.join(ROOT, "apps") + os.sep, "-I",
                            bdir + os.sep, "-o", os.path.join(d, "o.bin"), w],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit("mousechk: cannot assemble the package for its map:\n"
                     + r.stderr[-800:])
        out = {}
        for line in open(mp):
            m = re.match(r"\s*([0-9A-F]+)\s+([0-9A-F]+)\s+(\S+)$", line)
            if m:
                out[m.group(3)] = int(m.group(2), 16)
            m = re.match(r"([0-9A-F]{8})\s+(\S+)$", line)
            if m:
                out[m.group(2)] = int(m.group(1), 16)
        for need in ("mc_nrows", "mc_roff", "mc_buf", "mc_bp", "mc_pgrey",
                     "mc_bpp", "mc_kok"):
            if need not in out:
                sys.exit("mousechk: the package map has no %s" % need)
        return out
    finally:
        shutil.rmtree(d, ignore_errors=True)


# --- fixtures -----------------------------------------------------------------
def b(name, tree=None):
    import os88build
    if tree is not None:
        return tree.img(name)
    return os.path.join(ROOT, os88build.at(os.path.join("build", name)))


def system_disk(work, tree=None):
    """A COPY of the system floppy with \\AUTOSTRT\\MOUSECHK.LNK in it, so the
    package opens at boot (SPEC.md 26.10). Needs mtools, like syscheck."""
    import os88lnk
    a = os.path.join(work, "a.img")
    shutil.copyfile(b("os8088.img", tree), a)
    lnk = os.path.join(work, "MOUSECHK.LNK")
    with open(lnk, "wb") as f:
        f.write(bytes(os88lnk.make("SYSTEM/MOUSECHK.O88")))
    subprocess.run(["mcopy", "-i", a, lnk, "::/AUTOSTRT/MOUSECHK.LNK"],
                   check=True, capture_output=True)
    return a


def scratch_b(work):
    """A blank, WRITABLE 1.44MB B: (the shipped apps disk is nearly full)."""
    bimg = os.path.join(work, "b.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", bimg, "--size", "1440"], check=True,
                   capture_output=True)
    return bimg


MICE = {
    "com1": ["-chardev", "msmouse,id=m0", "-serial", "chardev:m0"],
    # MOUSEPORT=com2's wiring (Makefile): a live, SILENT UART at 3F8
    "com2": ["-serial", "none", "-chardev", "null,id=mq",
             "-device", "isa-serial,chardev=mq,iobase=0x3f8,irq=4",
             "-chardev", "msmouse,id=m0",
             "-device", "isa-serial,chardev=m0,iobase=0x2f8,irq=3"],
    # no UART at all: the machine's only pointing device is the PS/2 mouse
    "ps2": ["-serial", "none"],
}


class Box(object):
    """A private QEMU (tests/os88qemu.py's Private, with a choice of mouse)."""

    def __init__(self, work, a, bimg, mouse):
        import os88qemu
        self.sock = os.path.join(work, "q.sock")
        pidf = os.path.join(work, "q.pid")
        cmd = (["qemu-system-i386", "-machine", "pc,vmport=off",
                "-drive", "file=%s,format=raw,if=floppy" % a, "-boot", "a"]
               + MICE[mouse] +
               ["-drive", "file=%s,format=raw,if=floppy,index=1" % bimg,
                "-display", "none", "-qmp", "unix:%s,server,nowait"
                % self.sock, "-daemonize", "-pidfile", pidf])
        os88qemu.own(pidf, self.sock)
        os88qemu.start(cmd, cwd=ROOT, check=True)
        self.pid = int(open(pidf).read().strip())
        atexit.register(self.close)
        for _ in range(150):
            if os.path.exists(self.sock):
                break
            time.sleep(0.2)             # the socket appearing is a HOST event
        import xmcheck
        self.m = xmcheck.Mem(self.sock)
        self.m.readseg = lambda seg, off, n: self.m.read((seg << 4) + off, n)
        self.hmp = lambda *c: xmcheck.qmp(self.sock, *c)

    def close(self):
        if self.pid:
            try:
                os.kill(self.pid, signal.SIGTERM)
                import os88qemu
                os88qemu.gone(self.pid)
            except OSError:
                pass
            self.pid = None


class Pkg(object):
    """The open Mouse Check window and its rows, read out of guest memory."""

    def __init__(self, box, sym, syms):
        import os88geom
        import os88ui
        self.box, self.sym, self.s = box, sym, syms
        self.g = os88geom
        self.ui = os88ui.UI(box.m, mouse=object(), sym=sym, verbose=False)

    def win(self):
        try:
            for w in self.ui.windows():
                if w.visible and EXPECT["window_title"] in w.title:
                    return w
        except Exception:               # noqa: BLE001 - not booted / torn
            pass
        return None

    def seg(self, w):
        at = self.sym("wm_wins") + w.i * self.g.WIN_SIZE + self.g.W_SEG
        a = self.box.m.read(at, 2)
        return a[0] | (a[1] << 8)

    def read(self, off, n):
        """n bytes at package offset off - the segment read twice around it so
        a heap compaction (SPEC.md 66) cannot hand back a torn pair."""
        for _ in range(5):
            w = self.win()
            if w is None:
                return None
            s1 = self.seg(w)
            data = self.box.m.read((s1 << 4) + off, n)
            if self.seg(w) == s1:
                return data
        return None

    def byte(self, name):
        d = self.read(self.s[name], 1)
        return None if d is None else d[0]

    def rows(self):
        """[(kind, lamp, text)] of the list now, or None. A rebuild runs about
        twice a second and rewrites the records in place, so a torn read is
        possible and callers poll for what they expect."""
        d = self.read(self.s["mc_nrows"], 2)
        bp = self.read(self.s["mc_bp"], 2)
        if d is None or bp is None:
            return None
        n = d[0] | (d[1] << 8)
        end = (bp[0] | (bp[1] << 8)) + 70
        buf0 = self.s["mc_buf"]
        if not 0 < n <= 128 or not buf0 < end <= buf0 + 5200:
            return None
        offs = self.read(self.s["mc_roff"], 2 * n)
        blob = self.read(buf0, end - buf0)
        if offs is None or blob is None:
            return None
        out = []
        for i in range(n):
            o = (offs[2 * i] | (offs[2 * i + 1] << 8)) - buf0
            if not 0 <= o < len(blob) - 3:
                return None
            txt = blob[o + 2:].split(b"\0", 1)[0].decode("latin-1")
            out.append((blob[o], blob[o + 1], txt))
        return out

    def text(self):
        r = self.rows()
        if r is None:
            return None
        return "\n".join(re.sub(r" +", " ", t.strip()) for _, _, t in r)


def has(pkg, key, flags=re.I | re.M):
    """A predicate for `acted`: the rows match EXPECT[key]."""
    pat = EXPECT[key]
    return lambda: bool(pat is not None and (pkg.text() is not None)
                        and re.search(pat, pkg.text(), flags))


# --- the machine --------------------------------------------------------------
def boot(work, mouse, tree=None, scratch=True):
    """Boot the system floppy with the AUTOSTRT link; (Box, Pkg) once Mouse
    Check is open and its first rows are built."""
    import os88qemu
    import xmcheck
    a = system_disk(work, tree)
    bimg = scratch_b(work) if scratch else b("apps.img", tree)
    box = Box(work, a, bimg, mouse)
    sym = xmcheck.sym
    syms = pkgmap()
    pkg = Pkg(box, sym, syms)
    if not os88qemu.kernel_up(box.m, sym, secs=BOOT_SECS):
        raise RuntimeError("the kernel never reached the desktop")
    if not os88qemu.acted(box.m, lambda: pkg.win() is not None, secs=60,
                          what="the Mouse Check window", poll=0.2):
        raise RuntimeError("AUTOSTRT never opened a Mouse Check window "
                           "(open: %s)" % [w.title for w in pkg.ui.windows()])
    os88qemu.acted(box.m, lambda: (pkg.rows() or []) != [], secs=20,
                   what="the first rows", poll=0.2)
    os88qemu.ui_done(box.m, sym, cap=3.0)
    return box, pkg


def wait_text(pkg, pat, secs=20, what=None):
    import os88qemu
    return os88qemu.acted(
        pkg.box.m, lambda: bool((pkg.text() or "") and
                                re.search(pat, pkg.text(), re.I | re.M)),
        secs=secs, what=what or pat, poll=0.25)


def dump(pkg):
    t = pkg.text()
    return "\n    ".join((t or "(no rows)").splitlines())


def summary_says(pkg, key, fails, leg):
    """Assert the Summary verdict EXPECT[key] - or, while `other`'s section is
    not in the tree, that the foundation's stub rows are what the window
    shows, which still proves the plumbing end to end."""
    t = pkg.text() or ""
    if re.search(EXPECT["stub"], t, re.I):
        say("%s: the Summary is still the foundation's stub; the window and "
            "its rows are verified, the verdict is NOT" % leg)
        return
    if not wait_text(pkg, EXPECT[key], secs=15, what="the Summary: " + key):
        fails.append("%s: the rows never matched /%s/:\n    %s"
                     % (leg, EXPECT[key], dump(pkg)))


# --- the legs -----------------------------------------------------------------
def leg_serial(work, fails):
    import os88qemu
    box, pkg = boot(work, "com1")
    sym = pkg.sym
    summary_says(pkg, "sum_serial", fails, "serial")
    # S2: a row that follows the mouse changes when it moves. The pointer
    # goes to two far corners of the desktop, away from the window's own
    # buttons, and some row's text must differ from what it was
    before = pkg.text()
    for x, y in ((600, 440), (20, 460), (300, 300)):
        os88qemu.mouse_to(box.m, sym, box.hmp, x, y)
        os88qemu.ui_done(box.m, sym, cap=1.0)
    changed = os88qemu.acted(box.m, lambda: pkg.text() not in (None, before),
                             secs=6, what="a row to change with the mouse",
                             poll=0.25)
    if not changed:
        fails.append("serial: no row changed after the mouse moved across the "
                     "desktop (S2: the rows must redraw live):\n    %s"
                     % dump(pkg))
    pkg.box.close()


def leg_com2(work, fails):
    box, pkg = boot(work, "com2")
    summary_says(pkg, "sum_com2", fails, "com2")
    box.close()


def leg_ps2(work, fails):
    import os88qemu
    box, pkg = boot(work, "ps2")
    summary_says(pkg, "sum_ps2", fails, "ps2")
    grey = pkg.byte("mc_pgrey")
    if grey != 0xFF:
        fails.append("ps2: Probe PS/2 is not greyed on a machine whose kernel "
                     "already owns the PS/2 mouse ([mc_pgrey] = %s, want FF; "
                     "p2_canprobe refuses it, SPEC.md 126.8)" % grey)
    before = pkg.text()
    os88qemu.sendkey(box.sock, "p")
    os88qemu.ui_done(box.m, pkg.sym, cap=2.0)
    after = pkg.text()
    if before is not None and after is not None and \
            re.search(EXPECT["ps2_trace"], after, re.I) and \
            not re.search(EXPECT["ps2_trace"], before, re.I):
        fails.append("ps2: P ran a probe although Probe is greyed:\n    %s"
                     % dump(pkg))
    box.close()


def leg_nops2(work, fails):
    import os88build
    import os88qemu
    tree = os88build.tree("NOPS2=1", targets=TREE_GOALS)
    tree.apply()                        # builds (or reuses) it, points os88sym
    box, pkg = boot(work, "ps2", tree=tree)
    sym = pkg.sym
    kst = {}
    for s in P2_SYMS:                   # state before the probe, where it exists
        try:
            kst[s] = box.m.read(sym(s), 1)[0]
        except (Exception, SystemExit):     # noqa: BLE001 - compiled out
            pass
    if pkg.byte("mc_pgrey") == 0xFF:
        fails.append("nops2: Probe PS/2 is greyed on a NOPS2 kernel; the kernel "
                     "never took the PS/2 port, so Probe must be offered "
                     "(SPEC.md 126.8)")
    os88qemu.sendkey(box.sock, "p")
    if not wait_text(pkg, EXPECT["ps2_trace"], secs=30, what="the probe trace"):
        fails.append("nops2: no probe trace appeared after P (/%s/):\n    %s"
                     % (EXPECT["ps2_trace"], dump(pkg)))
    os88qemu.ui_done(box.m, sym, cap=3.0)
    for s, was in kst.items():
        now = box.m.read(sym(s), 1)[0]
        if now != was:
            fails.append("nops2: the probe changed the kernel's %s: %02X -> %02X"
                         % (s, was, now))
    # the keyboard still types: S opens the Save dialog, Esc closes it again
    n0 = len(pkg.ui.windows())
    os88qemu.sendkey(box.sock, "s")
    if not os88qemu.acted(box.m, lambda: len(pkg.ui.windows()) > n0, secs=10,
                          what="the Save dialog", poll=0.2):
        fails.append("nops2: after the probe the keyboard no longer works: S "
                     "opened no Save dialog (the 8042 was left unusable)")
    else:
        os88qemu.sendkey(box.sock, "esc")
        os88qemu.ui_done(box.m, sym, cap=2.0)
    box.close()


def leg_save(work, fails):
    import os88qemu
    box, pkg = boot(work, "com1")
    sym = pkg.sym
    n0 = len(pkg.ui.windows())
    os88qemu.sendkey(box.sock, "s")
    if not os88qemu.acted(box.m, lambda: len(pkg.ui.windows()) > n0, secs=10,
                          what="the Save dialog", poll=0.2):
        fails.append("save: S opened no dialog")
        box.close()
        return
    os88qemu.ui_done(box.m, sym, cap=2.0)
    os88qemu.sendkey(box.sock, "ret")           # the pre-filled MOUSECHK.TXT
    got = {}

    def landed():
        for img in ("b.img", "a.img"):
            for path in ("::/MOUSECHK.TXT", "::/SYSTEM/MOUSECHK.TXT",
                         "::/MEDIA/MOUSECHK.TXT"):
                r = subprocess.run(["mtype", "-i", os.path.join(work, img),
                                    path], capture_output=True)
                if r.returncode == 0 and r.stdout:
                    got["text"] = r.stdout.decode("latin-1")
                    got["where"] = img + path
                    return True
        return False
    if not os88qemu.acted(box.m, landed, secs=20, what="MOUSECHK.TXT",
                          poll=0.5):
        fails.append("save: no MOUSECHK.TXT on the scratch B: or A: after "
                     "Enter in the Save dialog")
        box.close()
        return
    say("save: wrote %s" % got["where"])
    lines = got["text"].replace("\r", "").split("\n")
    for key in ("rep_head", "rep_date", "rep_kid", "rep_section"):
        if not any(re.search(EXPECT[key], ln) for ln in lines):
            fails.append("save: the report has no line matching /%s/; it "
                         "begins:\n    %s" % (EXPECT[key],
                                              "\n    ".join(lines[:8])))
    box.close()


def leg_mono(work, fails):
    import os88build
    import os88qemu
    tree = os88build.tree("VIDEO=cga", targets=TREE_GOALS)
    tree.apply()
    box, pkg = boot(work, "com1", tree=tree)
    bpp = pkg.byte("mc_bpp")
    if bpp != 1:
        fails.append("mono: [mc_bpp] = %s on a CGA kernel, want 1" % bpp)
    r = pkg.rows()
    if not r or r[0][0] != 0:
        fails.append("mono: no heading row on a 1bpp adapter: %r" % (r,))
    # keep a picture of the glass for a human (lamps are shapes, 118.5)
    shot = os.path.join(work, "mousechk-cga.png")
    try:
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "shot.py"),
                        box.sock, shot], check=True, capture_output=True,
                       timeout=60)
        keep = os.environ.get("MOUSECHK_SHOTS")
        if keep and os.path.exists(shot):
            os.makedirs(keep, exist_ok=True)
            shutil.copy(shot, keep)
    except Exception as ex:             # noqa: BLE001
        say("mono: no screenshot (%s)" % ex)
    box.close()


LEGS = [("serial", leg_serial), ("com2", leg_com2), ("ps2", leg_ps2),
        ("save", leg_save), ("nops2", leg_nops2), ("mono", leg_mono)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="append", metavar="LEG",
                    help="run only this leg: " + ", ".join(n for n, _ in LEGS))
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    import os88qemu
    legs = [(n, f) for n, f in LEGS if not a.k or n in a.k]
    fails = []
    for name, fn in legs:
        work = os88qemu.workdir("mc")
        n0 = len(fails)
        say("leg %s" % name)
        try:
            fn(work, fails)
        except Exception as ex:         # noqa: BLE001
            import traceback
            traceback.print_exc()
            fails.append("%s: %s: %s" % (name, type(ex).__name__, ex))
        say("leg %s: %s" % (name, "ok" if len(fails) == n0 else "FAIL"))
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    if fails:
        for f in fails:
            print("mousechk: FAIL " + f)
        return 1
    print("mousechk: ok - %s" % ", ".join(n for n, _ in legs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
