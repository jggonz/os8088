#!/usr/bin/env python3
"""MOUSECHK's gate (SPEC.md 126.10) - python3 tests/mousechk.py [-k leg] [--keep] [--list]

Mouse Check (apps/mousechk/, SPEC.md 126) shows every auto-detected value for
every mouse the OS supports. This row boots kern_big under QEMU with the
package's own AUTOSTRT link on the system floppy, so the window is open and
front before the first paint, and reads what the window SAYS out of the
package's own row list in guest memory (mc_nrows, mc_roff, mc_buf - SPEC.md
126.2) rather than out of pixels. The map of those names comes from nasm: the
package is assembled a second time with `[map symbols]` (tests/paintheld.py's
way) and the result is held BYTE-IDENTICAL to the mousechk.bin of the
build the leg boots (the shared one, or a private tree's).

THE LEGS (each its own QEMU; `-k` picks one, several `-k` pick several).
Every verdict is checked beside the KERNEL's own bytes ([mou_seen],
[mou_port]), so a wrong verdict is told apart from a machine wired other than
the leg assumes:

  serial   the default machine: a serial mouse on COM1. After the pointer
           moves, the Summary's Position row reads the kernel's mouse_x/y at
           EACH stop (S2: a row that can only change by following the device),
           the verdict names the COM1 serial mouse, the Serial section's own
           winner and line say COM1 / IRQ4, the kernel id matches, VGA 4 bpp
  com2     MOUSEPORT=com2's wiring (a silent UART at 3F8, the mouse at 2F8):
           the same, naming COM2 / IRQ3. Both straight wirings also assert
           the Serial lamp is OK and there is NO cross-wired line, the
           negative control for:
  com2irq4 MOUSEPORT=com2irq4 (SPEC.md 9.5.2, the Compaq Portable III: the
           mouse on COM2's UART raising IRQ4): after a move the Summary still
           says working on COM2, and the Serial section says "Cross-wired:
           data here, interrupt on the other IRQ." with a WARN lamp and the
           winning line IRQ4
  ps2      MOUSEPORT=ps2's wiring (no UART at all): before any move the
           Summary is the WAIT verdict ("A PS/2 mouse answered"); Probe PS/2 is
           REFUSED - [mc_pgrey] = FF, the reason row names the kernel's live
           mouse - and P starts no trace; after a move the verdict names PS/2
  nops2    a PRIVATE kernel tree built with NOPS2=1 (SPEC.md 9.9's auxiliary-
           port probe left out), PS/2 mouse, no UART: the kernel has NO
           pointer, so the Summary is the no-mouse verdict; Probe is offered
           and runs, its trace says the device answered and the command byte
           was restored; the kernel's PS/2 bytes are unchanged and the
           keyboard still opens and closes the Save dialog
  save     Save Report onto the boot floppy's MEDIA (the dialog's Enter
           accepts the pre-filled MOUSECHK.TXT), the file read back on the
           host: head, date, kernel id, a section line and the verdict row
  mono     a PRIVATE kernel tree built with VIDEO=cga: [mc_bpp] = 1, the list
           opens with a heading and the Display row says CGA 640x200, 1 bpp.
           The lamp SHAPES (SPEC.md 118.5) are a screendump kept for a human
           (MOUSECHK_SHOTS=<dir>); see 126.10 for why they are not asserted

WHAT THIS CANNOT SHOW: an 8088's speed (QEMU is exact about work and useless
about time), a real STPC / W83977 controller, MartyPC's XT (no 8042 at all),
and "nothing else redrew" - only that the rows' text follows the device.

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
# Each is a regex, matched case-insensitively and per line (re.M) against the
# window's rows, each row stripped and its runs of blanks squeezed to one
# ("Position              x 190  y 191" -> "Position x 190 y 191"). A `sum_*`
# key is matched against the SUMMARY SECTION ONLY (its heading to the next), so
# a "PS/2 mouse" heading further down cannot satisfy a Summary assertion.
#
# The sources: summary.inc (owner `other`, ~/Repos/mousechk-logs/
# other-report.md), serial.inc (owner `serial`, serial-report.md), and for the
# PS/2 keys ps2.inc/probe.inc (owner `ps2`). Every entry was checked against
# the merged sources at integration (each text is present in its .inc), and
# the ps2/probe strings against the QEMU trace in SPEC.md 126.8.
EXPECT = {
    "window_title": "Mouse Check",
    # the foundation's stub: its presence after integration is a FAILURE, not a
    # pass on the plumbing alone
    "stub": r"sections not built yet",
    "sum_head": r"Summary",
    # --- summary.inc (126.5) -------------------------------------------------
    "sum_ok_com1": r"^Your mouse is working: COM1 serial mouse$",
    "sum_ok_com2": r"^Your mouse is working: COM2 serial mouse$",
    "sum_ok_ps2": r"^Your mouse is working: PS/2 mouse$",
    "sum_wait_ps2": r"^A PS/2 mouse answered\. Move it to confirm\.$",
    "sum_none": r"^No mouse found\. See the sections below for why\.$",
    "sum_kbd": r"^The keyboard mouse is on: arrows move, keypad 5 clicks\.$",
    "sum_ptr_com1": r"^Pointer in use COM1 serial mouse$",
    "sum_ptr_com2": r"^Pointer in use COM2 serial mouse$",
    "sum_ptr_ps2": r"^Pointer in use PS/2 mouse$",
    # {x} {y} are filled from the kernel's own mouse_x/mouse_y
    "sum_pos": r"^Position x {x} y {y} buttons --$",
    "sum_kid": r"^Kernel id [0-9A-F]{4} [0-9A-F]{4} [0-9A-F]{4} matches "
               r"this package$",
    "sum_vga": r"^Display VGA 640x480, 4 bpp$",
    "sum_cga": r"^Display CGA 640x200, 1 bpp$",
    # --- serial.inc (126.6): its OWN bytes (MOS_PORT and MOS_LINE), so the
    # winner and the line are a second reading beside the Summary's ----------
    "ser_win_com1": r"^Contest winner serial COM1$",
    "ser_win_com2": r"^Contest winner serial COM2$",
    "ser_win_ps2": r"^Contest winner PS/2 aux port$",
    # (serial.inc at dd7f51bcd: the line is the IRQ alone now)
    "ser_head": "Serial mice",
    "ser_line_irq4": r"^Winning serial line IRQ4$",
    "ser_line_irq3": r"^Winning serial line IRQ3$",
    # SPEC.md 9.5.2.1, the Compaq Portable III: data on COM2, interrupt on
    # IRQ4 - the section's WARN verdict (ser_s_vcx) and lamp
    "ser_cx": r"^Cross-wired: data here, interrupt on the other IRQ\.$",
    # --- ps2.inc / probe.inc (126.7, 126.8) ------------------------------
    # Probe refused, and why: kern_big owns a live PS/2 mouse (p2_r_live)
    "p2_refused_live": r"^Probe PS/2 is off: Kernel's PS/2 mouse live$",
    # Probe offered (p2x_s_offer): the NOPS2 kernel never took the aux port
    "p2_offer": r"^Probe PS/2 \(P\) re-runs this handshake",
    # a probe RAN: the trace's own header, which no other state prints
    "p2_ran": r"^Trace: ",
    # ...and QEMU's 8042 and mouse cooperate, so the kernel's road works and
    # the device is a plain mouse, ID 00 (p2x_v_kok)
    "p2_works": r"^Verdict: kernel's road works now, ID 00",
    # ...and the controller was put back as it was found (p2x_s_rest/_nrest)
    "p2_restored": r"^Command byte [0-9A-F]{2}>[0-9A-F]{2}: restored$",
    "p2_notrest": r"NOT RESTORED",
    # the shell's overflow line (rows.inc mc_s_full): trace rows were dropped
    "list_full": r"^\(list full",
    # --- the report file (mousechk.asm mc_r_title / mc_r_kid) ---------------
    "rep_head": r"^Mouse Check report, os8088 package build \d+",
    "rep_date": r"^Date \d{4}-\d\d-\d\d \d\d:\d\d",
    "rep_kid": r"^Kernel id [0-9A-F]{4} [0-9A-F]{4} [0-9A-F]{4}",
    "rep_section": r"^\[(INFO|OK|WARN|PROBLEM|N/A)\] Summary",
    # ...and the report carries the rows, not only a header
    "rep_verdict": r"Your mouse is working: COM1 serial mouse",
}

# what the KERNEL says, read beside what the window says, so a wrong verdict
# is told apart from a machine wired other than the leg believes. [mou_port]
# is a row's byte offset (SPEC.md 9.4.2): 0 COM1, 2 COM2, 4 the PS/2 row
KPORT = {"com1": 0, "com2": 2, "com2irq4": 2, "ps2": 4}

# rows.inc's lamps, as a heading row's +1 byte carries them
LP_INFO, LP_OK, LP_WARN, LP_BAD, LP_NA = range(5)
LP_NAME = {LP_INFO: "INFO", LP_OK: "OK", LP_WARN: "WARN", LP_BAD: "BAD",
           LP_NA: "N/A"}

# kern_big's 'MO' block names (SPEC.md 9.4.2, 9.9): read when the build has them
P2_SYMS = ("mou_p2st", "mou_p2", "mou_p2id")

# what a private tree builds: the 1.44MB system disk (the 360KB one does not
# carry MOUSECHK, SPEC.md 24.3.1.7) and the package, whose 'ID' fingerprint
# is made against THAT tree's kernel
TREE_GOALS = ("os8088.img", "mousechk.o88")
BOOT_SECS = 120.0


def say(msg):
    print("mousechk: " + msg, flush=True)


# --- the package's own symbols ------------------------------------------------
def pkgmap():
    """{name: offset in the package image} out of nasm's own map, for the
    package AS IT SHIPS on the disk this leg boots (tests/paintheld.py's
    recipe, with the build directory on the include path for
    mousechk_kid.inc and buildnum.inc).

    The build directory is `at("build/mousechk.bin")`'s, so after a private
    tree's `apply()` it is THAT tree's - the 'ID' include there is made
    against the knob kernel - and the assembly is HELD BYTE-IDENTICAL to that
    tree's mousechk.bin. A map that describes another build is refused, the
    way tools/os88sym.py refuses a kernel map, rather than trusted."""
    import os88build
    shipped = os.path.join(ROOT, os88build.at("build/mousechk.bin"))
    if not os.path.exists(shipped):
        sys.exit("mousechk: no %s - `make build/mousechk.o88` first" % shipped)
    bdir = os.path.dirname(shipped)
    d = tempfile.mkdtemp(prefix="mchk")
    try:
        w = os.path.join(d, "w.asm")
        mp = os.path.join(d, "m.map")
        ob = os.path.join(d, "o.bin")
        src = os.path.join(ROOT, "apps", "mousechk", "mousechk.asm")
        with open(w, "w") as f:
            f.write('[map symbols %s]\n%%include "%s"\n' % (mp, src))
        r = subprocess.run(["nasm", "-f", "bin", "-w+error", "-I",
                            os.path.join(ROOT, "apps") + os.sep, "-I",
                            bdir + os.sep, "-o", ob, w],
                           capture_output=True, text=True, cwd=ROOT)
        if r.returncode:
            sys.exit("mousechk: cannot assemble the package for its map:\n"
                     + r.stderr[-800:])
        if open(ob, "rb").read() != open(shipped, "rb").read():
            sys.exit("mousechk: the map describes a DIFFERENT build than %s "
                     "(a stale .bin, or a source edited since `make`) - "
                     "rebuild it" % shipped)
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
    # MOUSEPORT=com2irq4's wiring (Makefile; SPEC.md 9.5.2, the Compaq
    # Portable III): the mouse's UART at 2F8 raises IRQ4, COM1's line
    "com2irq4": ["-serial", "none", "-chardev", "null,id=mq",
                 "-device", "isa-serial,chardev=mq,iobase=0x3f8,irq=4",
                 "-chardev", "msmouse,id=m0",
                 "-device", "isa-serial,chardev=m0,iobase=0x2f8,irq=4"],
    # no UART at all: the machine's only pointing device is the PS/2 mouse
    "ps2": ["-serial", "none"],
}

BOXES = []                      # every QEMU this process started, for main()


class Box(object):
    """A private QEMU (tests/os88qemu.py's Private, with a choice of mouse).
    Registered in BOXES the moment it has a pid, so main() ends it after its
    leg on EVERY path - an exception inside boot() never returns the box to
    the leg - and not only at exit, when the next leg's guest is already
    running beside it."""

    def __init__(self, work, a, bimg, mouse):
        import os88qemu
        self.pid = None
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
        BOXES.append(self)
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
                import xmcheck
                xmcheck.release(self.sock)  # (a kept QMP connection, if any)
            except Exception:           # noqa: BLE001
                pass
            try:
                os.kill(self.pid, signal.SIGTERM)
                import os88qemu
                os88qemu.gone(self.pid)
            except OSError:
                pass
            self.pid = None


def kbyte(box, name):
    """One kernel byte by symbol, or None where this build has no such name."""
    import xmcheck
    try:
        return box.m.read(xmcheck.sym(name), 1)[0]
    except (Exception, SystemExit):     # noqa: BLE001 - compiled out
        return None


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
                if w.visible and w.title == EXPECT["window_title"]:
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

    def _rows_once(self):
        d = self.read(self.s["mc_nrows"], 2)
        bp = self.read(self.s["mc_bp"], 2)
        if d is None or bp is None:
            return None
        n = d[0] | (d[1] << 8)
        # [mc_bp] is advanced LAZILY (rows.inc mc_newrow): after a rebuild it
        # names the LAST record's start, so +70 covers that record (2 + 60 +
        # NUL) and a few bytes over
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

    def rows(self):
        """[(kind, lamp, text)] of the list now, or None. A rebuild runs about
        twice a second and rewrites the records IN PLACE while each of these
        reads is a separate monitor call, so one pass can mix two rebuilds:
        the answer is two passes that agree, else None and the caller polls.
        (A torn list that differs from the last one is exactly what a "some
        row changed" check would have taken for a live row.)"""
        for _ in range(4):
            r1 = self._rows_once()
            if r1 is None:
                continue
            if self._rows_once() == r1:
                return r1
        return None

    def text(self, section=None):
        """The rows as squeezed lines - all of them, or one SECTION's: its
        heading row (kind 0, text == `section`) up to the next heading."""
        r = self.rows()
        if r is None:
            return None
        if section is not None:
            got, on = [], False
            for kind, _, t in r:
                if kind == 0:
                    on = t.strip() == section
                if on:
                    got.append((kind, _, t))
            r = got
        return "\n".join(re.sub(r" +", " ", t.strip()) for _, _, t in r)


# --- the machine --------------------------------------------------------------
def boot(work, mouse, tree=None, scratch=True):
    """Boot the system floppy with the AUTOSTRT link; (Box, Pkg) once Mouse
    Check is open and its first rows are built."""
    import os88build
    import os88qemu
    import xmcheck
    if tree is None:                    # a plain leg after a tree leg, or
        os88build.plain().apply()       # alone: either way the SHIPPED map
    a = system_disk(work, tree)
    bimg = scratch_b(work) if scratch else b("apps.img", tree)
    syms = pkgmap()                     # (before the launch: it can refuse)
    box = Box(work, a, bimg, mouse)
    sym = xmcheck.sym
    pkg = Pkg(box, sym, syms)
    if not os88qemu.kernel_up(box.m, sym, secs=BOOT_SECS):
        raise RuntimeError("the kernel never reached the desktop")
    if not os88qemu.acted(box.m, lambda: pkg.win() is not None, secs=60,
                          what="the Mouse Check window", poll=0.2):
        raise RuntimeError("AUTOSTRT never opened a Mouse Check window "
                           "(open: %s)" % [w.title for w in pkg.ui.windows()])
    if not os88qemu.acted(box.m, lambda: bool(pkg.text(EXPECT["sum_head"])),
                          secs=20, what="the first rows", poll=0.2):
        raise RuntimeError("the window opened but its list never held a "
                           "Summary section:\n    %s" % dump(pkg))
    os88qemu.ui_done(box.m, sym, cap=3.0)
    return box, pkg


def wait_text(pkg, pat, secs=20, what=None, section=None):
    import os88qemu
    return os88qemu.acted(
        pkg.box.m, lambda: bool(re.search(pat, pkg.text(section) or "",
                                          re.I | re.M)),
        secs=secs, what=what or pat, poll=0.25)


def dump(pkg, section=None):
    t = pkg.text(section)
    return "\n    ".join((t or "(no rows)").splitlines())


def expect(pkg, key, fails, leg, secs=15, summary=False):
    """Poll until EXPECT[key] matches (the Summary section alone when
    `summary`); a miss is a failure that prints what the window held."""
    sec = EXPECT["sum_head"] if summary else None
    if wait_text(pkg, EXPECT[key], secs=secs, what=key, section=sec):
        return True
    fails.append("%s: %s never matched /%s/ - %s held:\n    %s"
                 % (leg, key, EXPECT[key], "the Summary" if summary
                    else "the window", dump(pkg, sec)))
    return False


def absent(pkg, key, fails, leg, why):
    t = pkg.text() or ""
    if re.search(EXPECT[key], t, re.I | re.M):
        fails.append("%s: /%s/ is in the window - %s:\n    %s"
                     % (leg, EXPECT[key], why, dump(pkg)))


def lamp(pkg, heading):
    """The lamp byte of the section whose heading row reads `heading`, from
    ONE agreeing list (Pkg.rows), or None when there is no such section."""
    for kind, lp, t in pkg.rows() or []:
        if kind == 0 and t.strip() == heading:
            return lp
    return None


def want_lamp(pkg, heading, want, fails, leg, secs=10):
    """Poll until section `heading`'s lamp is `want` (the shell raises it per
    rebuild, so the verdict and the lamp can be one rebuild apart)."""
    import os88qemu
    if os88qemu.acted(pkg.box.m, lambda: lamp(pkg, heading) == want,
                      secs=secs, what="the %s lamp" % heading, poll=0.25):
        return True
    got = lamp(pkg, heading)
    fails.append("%s: the %s section's lamp is %s, want %s:\n    %s"
                 % (leg, heading, LP_NAME.get(got, got), LP_NAME[want],
                    dump(pkg, heading)))
    return False


def not_stub(pkg, fails, leg):
    """The foundation's stub text after integration means the sections did not
    land: a FAILURE. It used to pass the leg on the plumbing alone, so a row
    run against a half-merged tree came back green and verified nothing."""
    if re.search(EXPECT["stub"], pkg.text() or "", re.I):
        fails.append("%s: the window still shows the foundation's stub (/%s/)"
                     " - the sections are not in this build" % (leg,
                                                               EXPECT["stub"]))
        return False
    return True


def kport(box, fails, leg, mouse):
    """The KERNEL's own verdict, beside the package's: [mou_seen] 1 and
    [mou_port] the leg's row. A miss here is the machine, not Mouse Check."""
    seen, port = kbyte(box, "mou_seen"), kbyte(box, "mou_port")
    if seen != 1 or port != KPORT[mouse]:
        fails.append("%s: the kernel itself reads mou_seen %s mou_port %s, "
                     "want 1 and %d - this machine is not wired as the leg "
                     "assumes, so the package's verdict below proves nothing"
                     % (leg, seen, port, KPORT[mouse]))
        return False
    return True


def wiggle(box, pkg, fails, leg, spots=((600, 440), (300, 300))):
    """Move the pointer through `spots` (closed-loop, tests/os88qemu.py's
    mouse_to: the packets go through QEMU's device, the UART or 8042 and the
    kernel's decoder) and require the Summary's Position row to read the
    kernel's own mouse_x/mouse_y after EACH move. That is S2's "rows follow the
    device" with a row that can only change by following it: a torn read or a
    rebuild of identical text cannot match two different coordinates."""
    import os88qemu
    ok = True
    for x, y in spots:
        os88qemu.mouse_to(box.m, pkg.sym, box.hmp, x, y)
        kx, ky, _ = os88qemu.mouse_where(box.m, pkg.sym)
        pat = EXPECT["sum_pos"].replace("{x}", str(kx)).replace("{y}", str(ky))
        if not wait_text(pkg, pat, secs=6, what="Position %d,%d" % (kx, ky),
                         section=EXPECT["sum_head"]):
            fails.append("%s: the pointer is at %d,%d but the Summary's "
                         "Position row never said so (S2: the rows must follow "
                         "the device):\n    %s"
                         % (leg, kx, ky, dump(pkg, EXPECT["sum_head"])))
            ok = False
    return ok


def premove(pkg, leg):
    """What the Summary says BEFORE any packet - recorded, not asserted, for
    the serial legs (the kernel settles a serial port only on clean packets,
    SPEC.md 9.5, so mou_seen is 0 until the first move)."""
    t = pkg.text(EXPECT["sum_head"]) or ""
    say("%s: before the first move the Summary verdict reads: %s"
        % (leg, " / ".join(t.splitlines()[-2:])))


# --- the legs -----------------------------------------------------------------
def leg_serial(work, fails):
    box, pkg = boot(work, "com1")
    if not not_stub(pkg, fails, "serial"):
        return
    premove(pkg, "serial")
    wiggle(box, pkg, fails, "serial", ((600, 440), (20, 460), (300, 300)))
    kport(box, fails, "serial", "com1")
    for key in ("sum_ok_com1", "sum_ptr_com1", "sum_kid", "sum_vga"):
        expect(pkg, key, fails, "serial", summary=True)
    for key in ("ser_win_com1", "ser_line_irq4"):
        expect(pkg, key, fails, "serial")
    # the negative control for com2irq4: a straight wiring is OK, not WARN
    want_lamp(pkg, EXPECT["ser_head"], LP_OK, fails, "serial")
    absent(pkg, "ser_cx", fails, "serial", "a cross-wired verdict on a "
           "straight COM1 / IRQ4 wiring")
    if pkg.byte("mc_kok") != 1:
        fails.append("serial: [mc_kok] = %s on the shipped kernel and package "
                     "(want 1: the 'ID' fingerprint matches)"
                     % pkg.byte("mc_kok"))


def leg_com2(work, fails):
    box, pkg = boot(work, "com2")
    if not not_stub(pkg, fails, "com2"):
        return
    premove(pkg, "com2")
    wiggle(box, pkg, fails, "com2")
    kport(box, fails, "com2", "com2")
    for key in ("sum_ok_com2", "sum_ptr_com2"):
        expect(pkg, key, fails, "com2", summary=True)
    for key in ("ser_win_com2", "ser_line_irq3"):
        expect(pkg, key, fails, "com2")
    want_lamp(pkg, EXPECT["ser_head"], LP_OK, fails, "com2")
    absent(pkg, "ser_cx", fails, "com2", "a cross-wired verdict on a "
           "straight COM2 / IRQ3 wiring")


def leg_com2irq4(work, fails):
    """MOUSEPORT=com2irq4: the mouse on COM2's UART, its interrupt on IRQ4.
    The kernel still finds it (SPEC.md 9.5.2 counts the LINE that fired), so
    the Summary says working on COM2; the Serial section says WHY it should
    not be trusted - the Cross-wired line and a WARN lamp - and its winning
    line is IRQ4, not the 2F8 convention's IRQ3. Before any move nothing is
    settled, so both are asserted only after the wiggle."""
    box, pkg = boot(work, "com2irq4")
    if not not_stub(pkg, fails, "com2irq4"):
        return
    premove(pkg, "com2irq4")
    wiggle(box, pkg, fails, "com2irq4")
    kport(box, fails, "com2irq4", "com2irq4")
    for key in ("sum_ok_com2", "sum_ptr_com2"):
        expect(pkg, key, fails, "com2irq4", summary=True)
    for key in ("ser_win_com2", "ser_line_irq4", "ser_cx"):
        expect(pkg, key, fails, "com2irq4")
    want_lamp(pkg, EXPECT["ser_head"], LP_WARN, fails, "com2irq4")


def leg_ps2(work, fails):
    import os88qemu
    box, pkg = boot(work, "ps2")
    if not not_stub(pkg, fails, "ps2"):
        return
    # kern_big's handshake ran at boot (SPEC.md 9.9), and NOTHING has moved:
    # the one state that is the WAIT verdict and no other
    expect(pkg, "sum_wait_ps2", fails, "ps2", summary=True)
    grey = pkg.byte("mc_pgrey")
    if grey != 0xFF:
        fails.append("ps2: Probe PS/2 is not greyed on a machine whose kernel "
                     "already owns the PS/2 mouse ([mc_pgrey] = %s, want FF; "
                     "p2_canprobe refuses it, SPEC.md 126.8)" % grey)
    expect(pkg, "p2_refused_live", fails, "ps2")
    os88qemu.sendkey(box.sock, "p")
    os88qemu.ui_done(box.m, pkg.sym, cap=2.0)
    # a trace would show at the next rebuild (~0.5 s): give it three guest
    # seconds to appear, which is a ceiling on a state and not a pause
    if wait_text(pkg, EXPECT["p2_ran"], secs=3, what="no trace"):
        fails.append("ps2: P ran a probe although Probe is refused:\n    %s"
                     % dump(pkg))
    wiggle(box, pkg, fails, "ps2")
    kport(box, fails, "ps2", "ps2")
    for key in ("sum_ok_ps2", "sum_ptr_ps2"):
        expect(pkg, key, fails, "ps2", summary=True)
    expect(pkg, "ser_win_ps2", fails, "ps2")


def leg_nops2(work, fails):
    import os88build
    import os88qemu
    tree = os88build.tree("NOPS2=1", targets=TREE_GOALS)
    tree.apply()                        # builds (or reuses) it, points os88sym
    box, pkg = boot(work, "ps2", tree=tree)
    sym = pkg.sym
    if not not_stub(pkg, fails, "nops2"):
        return
    if pkg.byte("mc_kok") != 1:
        fails.append("nops2: [mc_kok] = %s - the disk carries a package made "
                     "against ANOTHER kernel than this tree's"
                     % pkg.byte("mc_kok"))
    # no UART and a kernel with no PS/2 half: the kernel has NO pointer, which
    # is the one machine here that shows the no-mouse verdict (126.5)
    expect(pkg, "sum_none", fails, "nops2", summary=True)
    expect(pkg, "sum_kbd", fails, "nops2", summary=True)
    kst = {s: kbyte(box, s) for s in P2_SYMS}
    if pkg.byte("mc_pgrey") != 0:
        fails.append("nops2: Probe PS/2 is greyed ([mc_pgrey] = %s) on a NOPS2 "
                     "kernel; the kernel never took the PS/2 port, so Probe "
                     "must be offered (SPEC.md 126.8)" % pkg.byte("mc_pgrey"))
    expect(pkg, "p2_offer", fails, "nops2")
    absent(pkg, "p2_ran", fails, "nops2", "a trace before any probe")
    os88qemu.sendkey(box.sock, "p")
    if expect(pkg, "p2_ran", fails, "nops2", secs=30):
        for key in ("p2_works", "p2_restored"):
            expect(pkg, key, fails, "nops2", secs=10)
        absent(pkg, "p2_notrest", fails, "nops2", "the probe left the 8042's "
               "command byte changed")
        absent(pkg, "list_full", fails, "nops2", "the trace overflowed the "
               "row list, so trace rows were dropped")
    os88qemu.ui_done(box.m, sym, cap=3.0)
    for s, was in kst.items():
        now = kbyte(box, s)
        if now != was:
            fails.append("nops2: the probe changed the kernel's %s: %s -> %s"
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
        if not os88qemu.acted(box.m, lambda: len(pkg.ui.windows()) == n0,
                              secs=10, what="the dialog to close", poll=0.2):
            fails.append("nops2: Esc did not close the Save dialog")


def leg_save(work, fails):
    import os88qemu
    box, pkg = boot(work, "com1")
    sym = pkg.sym
    # one move first, so the Summary has a verdict to write (mou_seen)
    wiggle(box, pkg, fails, "save", ((300, 300),))
    expect(pkg, "sum_ok_com1", fails, "save", summary=True)
    n0 = len(pkg.ui.windows())
    os88qemu.sendkey(box.sock, "s")
    if not os88qemu.acted(box.m, lambda: len(pkg.ui.windows()) > n0, secs=10,
                          what="the Save dialog", poll=0.2):
        fails.append("save: S opened no dialog")
        return
    os88qemu.ui_done(box.m, sym, cap=2.0)
    os88qemu.sendkey(box.sock, "ret")           # the pre-filled MOUSECHK.TXT
    keys = ("rep_head", "rep_date", "rep_kid", "rep_section", "rep_verdict")
    got = {}

    def complete(text):
        lines = text.replace("\r", "").split("\n")
        return all(any(re.search(EXPECT[k], ln) for ln in lines) for k in keys)

    def landed():
        # polled until the WHOLE report is there: the image is read while the
        # guest is still writing it, so a first read can be a short file
        for img in ("a.img", "b.img"):
            for path in ("::/MEDIA/MOUSECHK.TXT", "::/MOUSECHK.TXT",
                         "::/SYSTEM/MOUSECHK.TXT"):
                r = subprocess.run(["mtype", "-i", os.path.join(work, img),
                                    path], capture_output=True)
                if r.returncode == 0 and r.stdout:
                    got["text"] = r.stdout.decode("latin-1")
                    got["where"] = img + path
                    if complete(got["text"]):
                        return True
        return False
    if not os88qemu.acted(box.m, landed, secs=20, what="MOUSECHK.TXT",
                          poll=0.5):
        if "text" not in got:
            fails.append("save: no MOUSECHK.TXT on the scratch B: or A: after "
                         "Enter in the Save dialog")
            return
    say("save: wrote %s" % got["where"])
    lines = got["text"].replace("\r", "").split("\n")
    for key in keys:
        if not any(re.search(EXPECT[key], ln) for ln in lines):
            fails.append("save: the report has no line matching /%s/; it "
                         "begins:\n    %s" % (EXPECT[key],
                                              "\n    ".join(lines[:12])))


def leg_mono(work, fails):
    import os88build
    tree = os88build.tree("VIDEO=cga", targets=TREE_GOALS)
    tree.apply()
    box, pkg = boot(work, "com1", tree=tree)
    if not not_stub(pkg, fails, "mono"):
        return
    if pkg.byte("mc_kok") != 1:
        fails.append("mono: [mc_kok] = %s - the disk carries a package made "
                     "against ANOTHER kernel than this tree's"
                     % pkg.byte("mc_kok"))
    bpp = pkg.byte("mc_bpp")
    if bpp != 1:
        fails.append("mono: [mc_bpp] = %s on a CGA kernel, want 1" % bpp)
    r = pkg.rows()
    if not r or r[0][0] != 0:
        fails.append("mono: no heading row on a 1bpp adapter: %r" % (r,))
    expect(pkg, "sum_cga", fails, "mono", summary=True)
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


LEGS = [("serial", leg_serial), ("com2", leg_com2),
        ("com2irq4", leg_com2irq4), ("ps2", leg_ps2),
        ("save", leg_save), ("nops2", leg_nops2), ("mono", leg_mono)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="append", metavar="LEG",
                    help="run only this leg: " + ", ".join(n for n, _ in LEGS))
    ap.add_argument("--keep", action="store_true",
                    help="keep each leg's work directory (disks, screenshot)")
    ap.add_argument("--list", action="store_true",
                    help="print the legs and the EXPECT table; boot nothing")
    a = ap.parse_args()
    names = [n for n, _ in LEGS]
    bad = [k for k in (a.k or []) if k not in names]
    if bad:
        ap.error("no such leg: %s (legs: %s)" % (", ".join(bad),
                                                 ", ".join(names)))
    if a.list:
        print("legs: " + ", ".join(names))
        for k, v in EXPECT.items():
            print("  %-16s %s" % (k, v))
        return 0
    import os88qemu
    legs = [(n, f) for n, f in LEGS if not a.k or n in a.k]
    fails = []
    for name, fn in legs:
        work = os88qemu.workdir("mc")
        n0 = len(fails)
        say("leg %s" % name)
        try:
            fn(work, fails)
        except (Exception, SystemExit) as ex:   # noqa: BLE001 - a dead
            import traceback                # guest's QMP is a SystemExit
            traceback.print_exc()           # (xmcheck.qmp): the LEG fails,
            fails.append("%s: %s: %s"       # and the next leg still runs
                         % (name, type(ex).__name__, ex))
        finally:
            while BOXES:                # every path, before the next leg boots
                BOXES.pop().close()
        say("leg %s: %s" % (name, "ok" if len(fails) == n0 else "FAIL"))
        if a.keep:
            say("leg %s: kept %s" % (name, work))
        else:
            shutil.rmtree(work, ignore_errors=True)
    if fails:
        for f in fails:
            print("mousechk: FAIL " + f)
        return 1
    print("mousechk: ok - %s" % ", ".join(n for n, _ in legs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
