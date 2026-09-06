#!/usr/bin/env python3
"""FTPD: the FTP server, driven by a real FTP client (SPEC.md 77).

    make && make ftpdtest && python3 tests/ftpd.py

**THIS ONE IS QEMU'S, FOR THE SAME REASON tests/ethernet.py IS.** CLAUDE.md's
rule is MartyPC first with a short list of exceptions, and the network card is
on it: MartyPC has no NIC of any kind, so the emulator this tree develops on
cannot host ETHER.DRV at all. QEMU's `ne2k_isa` on `-netdev user` is the only
harness there is.

What QEMU costs is what it always costs: the machine is not an 8088 and no
timing here means anything. Every assertion below is about BEHAVIOUR and none
is about speed, so there is no number in this file for PERFORMANCE.md to want
off the 5150.

**THE CLIENT IS `ftplib` AND THAT IS THE POINT.** A hand-rolled client in this
file would test the server against this file's idea of RFC 959; the standard
library's has talked to real servers for thirty years, so what it accepts is
evidence about interoperability rather than about the harness. It is what
finds a missing `227` bracket, a reply code the wrong side of a class boundary
and a `LIST` no parser can read.

`make test ETHFWD=1` is what makes it reachable at all: slirp gives a guest no
inbound route, so the control port and the whole passive range are forwarded
(Makefile). ftplib since Python 3.11 IGNORES the address in a `227` reply and
dials the one it is already connected to, so its data connection comes back to
127.0.0.1 and the forward catches it.

**AND THAT DEFAULT HID A REAL BUG FOR A WHOLE RELEASE.** This paragraph used
to end "a security default doing us a favour", and the favour it was doing was
concealing that the server advertised its own unroutable 10.0.2.15 in every
`227`. WinSCP trusts that address, dials it, and times out - so the server was
unusable behind NAT while this gate reported six green assertions. A harness
whose client is MORE FORGIVING than a real one is not a harness for that
behaviour (tests/lptlink/partner.py's `NC_BYE` is the same lesson). Assertion
7 now runs a client that trusts the 227, and assertion 8 runs ACTIVE mode,
which nothing covered at all.

TWELVE ASSERTIONS, and they climb the same way stage E's did.

1. THE SERVER ANSWERS. A `220` greeting, and USER/PASS reach `230`. That is
   the port-21 listener, NETV_ACCEPT, and the control connection.

2. IT LISTS. `LIST` returns rows for the files that are on the disk, in the
   `ls -l` shape `SYST` promises - so the data connection opened, PASV's
   address was one the client could reach, and OSAPI_FILE_FIND's walk ran on
   the UI task while the worker held the socket.

3. RETR IS BYTE-EXACT, for TEXT AND FOR BINARY. FTPBIN.DAT is every byte value
   eight times over, which is what catches a path that is clean for ASCII and
   eats 0x00 or 0x1A.

4. STOR ROUND-TRIPS, AND THE SERVER IS NOT ASKED WHETHER IT WORKED. The file
   goes up, and it is read back with RETR - and then the IMAGE is read on the
   HOST by an independent FAT12 reader (tools/os88disk.py --verify plus a
   direct extract). Asking os8088 whether os8088 saved it correctly is the
   failure docs/FIELD-NOTES.md 4 is about: the writer and the reader are the
   same FAT12 code, so the two agreeing on the same wrong thing is exactly
   what cannot be seen from inside.

5. IT NAVIGATES. `CWD DEEP`, a `LIST` there, `PWD` says `/DEEP`, `CDUP` comes
   back. That is OSAPI_FILE_GOTO walking and fd_pathpush's string agreeing
   with it.

6. READ ONLY REFUSES. With the box ticked, STOR is answered `550` and the file
   does NOT appear - so the gate is the server's and not the client's.

7. A CLIENT THAT TRUSTS THE 227 CAN STILL TRANSFER. `trust_server_pasv_ipv4_
   address = True` is WinSCP's behaviour, and it is the one this gate was
   blind to. It needs the PASV override set, which is what SPEC.md 77.12's
   Setup page and FTPD.CFG exist for - so this drives the setting through the
   window and then proves a trusting client works.

8. ACTIVE MODE WORKS. `PORT`, where the SERVER dials the client. It needs no
   address from the server at all, which is why it is the answer for a machine
   behind NAT with nothing configured - and it had no coverage whatsoever.

9. A USER AND A PASSWORD GATE IT (SPEC.md 77.15). Configured through the
   Setup page, the old credentials are refused, the NAME is folded and the
   PASSWORD is not - which is two assertions in one, because getting the
   comparison the same way round for both is the easy mistake.

10. THE ROOT IS SELECTABLE (SPEC.md 77.16.1). Rooted at DEEP, `/` holds
    DEEP's one file and CDUP at the root stays put - the session never sees
    above what it was given.

11. WHOLE-MACHINE MODE SERVES THE VOLUMES (SPEC.md 77.16). The root lists one
    directory row per mounted drive, `CWD B` lands in B:'s root, `CDUP` comes
    back up above every volume, a BARE name there is refused, and an absolute
    `/B/FTPHELLO.TXT` still reaches the file - which is the payoff of putting
    the branch in `fd_enter` rather than in each command.

12. THE SETUP BUTTON, READ ONLY AS A SETTING, AND A BAD ROOT (SPEC.md 77.17).
    The menu item still toggles the page; Read Only ticked on the SETUP page
    refuses a STOR, which is what says the page's box and the toolbar's are
    one setting; `B:\DEEP` reaches the same folder `B:/DEEP` does; and a Root
    that does not resolve REFUSES to start with nothing left listening,
    rather than quietly serving somewhere else - which is what the field
    reported and the one failure a user cannot see.

A STOR bigger than the staging buffer is deliberately included in 4: the whole
design is a stage-and-commit loop (SPEC.md 77.1) and a file that fits in one
chunk never exercises OSAPI_FILE_APPEND's cluster precondition at all.
"""
import argparse
import ftplib
import io
import os
import socket
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import dispcp                                          # noqa: E402
import ethernet as eth                                 # noqa: E402
import os88sym                                         # noqa: E402
import os88layout                                           # noqa: E402
import os88qemu                                              # noqa: E402

S = os88sym.linear

SOCK = "build/qmp.sock"
SYSIMG = "build/ether360.img"
APPIMG = "build/ftpapps.img"
CTRL = 2121
HOST = "127.0.0.1"

HELLO = b"hello from os8088\r\n"
BINDAT = bytes(range(256)) * 8


def say(*a):
    print(*a)
    sys.stdout.flush()


# --- QMP's names for the keys dispcp's scroller presses ----------------------
# dispcp.scroll_to calls `m.key("ArrowDown")`, which is MartyPC's spelling -
# every other caller of it is a MartyPC gate (tests/brtest.py and friends) and
# os88marty.Marty has the method. This is the same contract over QMP, which is
# the whole of what a QEMU-hosted gate is missing to reuse that scroller.
QKEYS = {"ArrowDown": "down", "ArrowUp": "up", "Home": "home", "End": "end",
         "PageDown": "pgdn", "PageUp": "pgup", "Tab": "tab", "Enter": "ret"}


class Qemu(eth.Qemu):
    def key(self, name):
        if name not in QKEYS:
            raise KeyError("no QMP sendkey name for %r" % name)
        self.hmp("sendkey " + QKEYS[name])
        time.sleep(0.05)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true",
                    help="leave QEMU running for a look afterwards")
    ap.add_argument("--kfz", action="store_true",
                    help="build the images with KFZ=1 and report every task "
                         "stack's high water at the end (tools/stkwater.py). "
                         "MEASUREMENT, never a gate: the number is what "
                         "SCH_STACK has to be sized from, and QEMU understates "
                         "a real BIOS by ~20 bytes")
    ap.add_argument("--shot", metavar="PNG",
                    help="write a screenshot of the desktop just before the "
                         "machine is torn down. For looking at what the "
                         "window DREW, which no assertion here can reach")
    a = ap.parse_args()
    knob = ["KFZ=1"] if a.kfz else []
    if a.kfz:
        os88sym.default_defines("KFZTRACE")     # every S() below resolves
                                                # against the kernel actually
                                                # built, or os88sym refuses
    fails = []

    # THE IMAGES ARE REBUILT, NOT CHECKED. QEMU mounts both WRITABLE and the
    # guest writes to them - this gate's own STOR lands on ftpapps.img - so a
    # second run would find the uploaded file already there and assertion 4
    # would pass without a byte crossing the wire. Staleness is not the hazard
    # here, a dirty image is, and `make` cannot see the difference because the
    # guest's write leaves the image NEWER than everything it was built from.
    for f in (SYSIMG, APPIMG):
        if os.path.exists(f):
            os.remove(f)
    r = subprocess.run(["make", "ftpdtest"] + knob,
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("ftpd: make ftpdtest failed:\n" + r.stdout + r.stderr)

    # A SURVIVOR KEEPS THE SOCKET, so the new machine cannot bind and every
    # read below would come from the OLD one - which reads as a change that
    # did nothing. Kill it by PID out of the pidfile, never with `pkill -f
    # qemu`, whose pattern matches the calling shell.
    if os.path.exists("build/qemu.pid"):
        try:
            os.kill(int(open("build/qemu.pid").read().strip()), 15)
            time.sleep(1.0)
        except (OSError, ValueError):
            pass
    for f in ("build/qmp.sock", "build/qemu.pid"):
        if os.path.exists(f):
            os.remove(f)

    # `make test` DAEMONISES the emulator, so it outlives this script
    # unless somebody kills it - and the somebody is us (os88qemu).
    os88qemu.own()
    r = subprocess.run(["make", "test", "ETHER=1", "ETHFWD=1",
                        "TESTIMG=" + SYSIMG, "TESTAPPS=" + APPIMG] + knob,
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("ftpd: make test failed:\n" + r.stdout + r.stderr)

    # ethernet.py's Qemu and Mouse are reused rather than copied: one QMP
    # client at a time, `pmemsave` for memory, and one tools/mouse.py process
    # per action because msmouse is 1200 baud and the pacing inside that tool
    # is what makes a click land (its own header says so).
    m = Qemu(SOCK)
    mo = eth.Mouse()

    try:
        run_gate(m, mo, fails)
        if a.kfz:
            stack_water(m)
        if a.shot:
            # **BEFORE THE QUIT, AND ONLY HERE.** The window's own log is the
            # one thing in this package that no assertion can see - it is
            # pixels - and a screenshot at the end of a session that has just
            # done a STOR, a RETR and a LIST is how a DRAWING defect gets
            # looked at without a field run (SPEC.md 77.42).
            r = subprocess.run(["python3", "tools/shot.py", SOCK, a.shot],
                               capture_output=True, text=True)
            say("")
            say("screenshot -> %s%s" % (a.shot, "" if not r.returncode
                                        else " FAILED: " + r.stderr.strip()))
    finally:
        if not a.keep:
            try:
                m.quit()
            except Exception:
                pass

    say("")
    if fails:
        for f in fails:
            say("FAIL " + f)
        sys.exit(1)
    say("ftpd: all assertions passed")


def stack_water(m):
    """Every task slice's deepest byte, after a whole FTP session.

    ONLY under --kfz, because only a `KFZ=1` `task_spawn` fills the slices with
    0xCC. It prints and never fails: the classes are numbers to decide with
    whoever owns the memory budget (docs/KERNEL-MEMORY.md), and a gate here
    would turn that decision into a build break on somebody else's machine.

    PER-SLOT SIZES since SPEC.md 8.7 - `slice_sizes()` decodes them off the
    kernel. One number here read every small slice off the end of itself and
    into the next task's fill, which is the failure this whole family of
    readers keeps having in a new place each time.
    """
    import stkwater                                 # tools/ is already on the
    sizes = stkwater.slice_sizes(stkwater.DEF)      # path (the header above)
    ns = stkwater.slots()
    base = os88sym.linear("sch_stacks", stkwater.DEF)
    mem = m.read(base, sum(sizes))
    say("")
    worst = stkwater.report(mem, ns, sizes,
                            "(after the whole ftpd gate, under QEMU)", base)
    if not worst:
        return
    # ...and WHERE those bytes went. The deepest slice's dead words still carry
    # the return addresses of the chain that made it deep, so the number gets
    # attributed instead of argued about (docs/KERNEL-MEMORY.md, "Task stacks").
    rows = [r for r in stkwater.water(mem, ns, sizes)
            if r[1] == worst]
    slot = rows[0][0]
    row = m.read(S("drv_tab") + eth.ETH_ROW * eth.DRVR_SZ + eth.DRVR_SEG, 2)
    say("")
    stkwater.deepest_seen(m.read, os88sym.linear("khb_deep", stkwater.DEF),
                          drv=eth.ether_syms(),
                          dimg=open("build/ether.bin", "rb").read(),
                          pseg=eth.u16(row))
    say("")
    at = sum(sizes[:slot - 1])                     # the slice's OWN offset and
    sz = sizes[slot - 1]                           # size: they are not a stride
    stkwater.annotate(mem[at:at + sz], worst, sz,
                      kern=os88sym.syms(stkwater.DEF), drv=eth.ether_syms(),
                      seg=eth.u16(row),
                      # ...as .TEXT OFFSETS index it, not the raw file: the
                      # words being annotated are near return addresses off a
                      # guest stack (SPEC.md 2.9, tools/os88layout.py)
                      kimg=os88layout.kernel_text(),
                      dimg=open("build/ether.bin", "rb").read())


def wait_dhcp(m):
    """The card up and an address bound, read out of the driver's own image.

    The gate reads state rather than clicking, exactly as tests/ethernet.py
    does: SYSTEM.CFG already asked for ETHER.DRV, so this is a wait and not a
    Control Panel drive.
    """
    syms = eth.ether_syms()
    for _ in range(200):
        time.sleep(0.4)
        row = m.read(S("drv_tab") + eth.ETH_ROW * eth.DRVR_SZ + eth.DRVR_SEG, 2)
        seg = eth.u16(row)
        if not seg:
            continue
        ip = m.readseg(seg, syms["eth_ip"], 4)
        if any(ip):
            return eth.dotted(ip)
    return None


def launch(m, mo):
    """Open B:, launch FTPD.O88, press Start.

    **THE DOUBLE-CLICK IS RETRIED AND THE WAIT IS ON THE WINDOW.** The first
    version did one open_named and then slept two seconds, and it failed about
    half the time with no instance in inst_tab at all - the launch had simply
    not happened. That is the harness being flaky and not the package: a
    double-click is two presses inside a 9-tick window through a 1200-baud
    mouse (CLAUDE.md), and scroll_to has just been pressing keys at the same
    window. So this waits for the STATE IT WANTS rather than for a duration,
    which is what dispcp's own scroller does, and asks again if it does not
    arrive.
    """
    def settle(mm, card=None):
        time.sleep(2.0)

    dispcp.open_drive(m, mo, S, settle, "B")
    # **open_drive ANSWERS THE DRIVE ICON's (x, y), NOT THE WINDOW's**, and
    # taking it for the window is what the first version of this did: row_xy
    # then computes a row inside the desktop off to the right of the Disk
    # window, every double-click lands on bare desktop, and the symptom is a
    # package that "will not launch" with nothing selected and no error
    # anywhere. The window's own rect is the only thing that answers this.
    wins = dispcp.win_list(m, S)
    if not wins:
        raise RuntimeError("opening drive B: left no window")
    wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
    for _ in range(4):
        dispcp.open_named(m, mo, S, settle, wx, wy, "FTPD.O88")
        fx, fy = wait_win(m, 12.0)
        if fx is not None:
            break
        say("   (the launch did not take - asking again)")
    else:
        raise RuntimeError("FTPD.O88 never opened a window in 4 attempts - "
                           "the package failed to load, rather than the click "
                           "failing to land")
    # The Start button is derived from the WINDOW RECORD rather than from the
    # template, because wm_fit clamps a template that does not fit the live
    # screen (SPEC.md 39.7) and a CGA desktop is 200 rows.
    bx, by = start_btn(fx, fy)
    mo.click(bx, by)
    time.sleep(1.5)
    return fx, fy


def wait_win(m, secs):
    """Poll for the FTP window rather than sleeping a guess."""
    end = time.time() + secs
    while time.time() < end:
        try:
            return ftp_win(m)
        except RuntimeError:
            time.sleep(0.5)
    return None, None


# --- the FTP window, and its two controls ------------------------------------
# FD_W is the identifier rather than "the newest slot": win_list answers in
# SLOT order and a slot is reused, so the highest index is not reliably the
# window just opened. A width is a fact about which window this is.
FD_W, FD_H = 400, 176
FD_PAD, FD_BTNW, FD_BTNH = 4, 72, 14
TITLE_H = 18


def ftp_win(m):
    slots = dispcp.win_list(m, S)
    if not slots:
        raise RuntimeError("no windows at all after launching FTPD.O88")
    for i in reversed(slots):
        x, y, w, h = dispcp.win_rect(m, S, i)
        if w == FD_W:
            return x, y
    x, y, w, h = dispcp.win_rect(m, S, slots[-1])
    raise RuntimeError("no %dpx-wide window after launching FTPD.O88 - the "
                       "newest is %dx%d at (%d,%d), which is the Disk window "
                       "still, so the package never opened one"
                       % (FD_W, w, h, x, y))


def start_btn(fx, fy):
    return (fx + 1 + FD_PAD + FD_BTNW // 2,
            fy + TITLE_H + FD_PAD + FD_BTNH // 2)


def ro_box(fx, fy):
    return (fx + 1 + FD_PAD + FD_BTNW + 8 + 6,
            fy + TITLE_H + FD_PAD + 2 + 6)


# --- the Setup page's controls, derived the way fd_setup_rects derives them --
# FD_SETX/FD_SETY/FD_SROW/FD_FLDX/FD_FLDW and the check box's offset, mirrored
# from ftpd.asm. A label and its field SHARE a row (the content box on CGA is
# ~117 rows once wm_fit has clamped the window), so the row pitch is one
# FD_SROW and not two.
FD_SETX, FD_SETY, FD_SROW = 8, 22, 16
FD_FLDX, FD_FLDW, FD_FN = 112, 160, 4
F_PASV, F_ROOT, F_USER, F_PASS = 0, 1, 2, 3


def setup_btn(fx, fy):
    """The way IN, right-aligned in the toolbar (and Done, in Start's corner).

    Right-aligned rather than at a constant, so this is derived the same way
    fd_layout derives it: the content's right edge, less the pad, less the
    button. FD_W - 2 is the content width.
    """
    x2 = fx + 1 + (FD_W - 2) - 1 - FD_PAD
    return (x2 - FD_BTNW // 2,
            fy + TITLE_H + FD_PAD + FD_BTNH // 2)


def field_pt(fx, fy, idx):
    return (fx + 1 + FD_FLDX + 20,
            fy + TITLE_H + FD_SETY + idx * FD_SROW + 6)


def _box_pt(fx, fy, row):
    return (fx + 1 + FD_SETX + 6,
            fy + TITLE_H + FD_SETY + FD_FN * FD_SROW + 4 + row * FD_SROW + 6)


def sro_box(fx, fy):
    """Read Only, on the SETUP page - the same [fd_ro] as the toolbar's."""
    return _box_pt(fx, fy, 0)


def mach_box(fx, fy):
    return _box_pt(fx, fy, 1)


def restart(m, mo):
    """Stop, then Start - the root is walked at START and nowhere else."""
    fx, fy = ftp_win(m)
    mo.click(*start_btn(fx, fy))
    time.sleep(1.5)
    mo.click(*start_btn(fx, fy))
    time.sleep(2.5)


# [fd_st], read out of the package's own bss. The o88 header carries the image
# length at +8, and the bss follows it - dispcp gives the window's segment.
FD_ST_OFF = 48          # fd_st equ os88_image_end + 48
STATES = {0: "off", 1: "listening", 2: "serving", 3: "err"}


FD_PAGE_OFF = 68        # fd_page equ os88_image_end + 68
PAGES = {0: "log", 1: "setup", 2: "about"}


def read_page(m):
    """Which face the window has up, out of the package's own bss."""
    for i in reversed(dispcp.win_list(m, S)):
        x, y, w, h = dispcp.win_rect(m, S, i)
        if w != FD_W:
            continue
        r = m.read(S("wm_wins") + i * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        seg = dispcp._u16(r, 22)
        o88 = open("build/ftpd.o88", "rb").read()
        img = o88[8] | (o88[9] << 8)
        return PAGES.get(m.readseg(seg, img + FD_PAGE_OFF, 1)[0], "?")
    raise RuntimeError("the FTP window is gone")


def press_slide_release(mo, x0, y0, x1, y1):
    """Press at one point, slide to another, release there.

    SPEC.md 77.18's gesture driven as its three separate edges - which is
    what `click` is NOT: that presses and releases in one place, so it can
    never tell firing on the press from firing on the release.
    """
    mo.run("to", str(x0), str(y0))
    mo.run("down")
    time.sleep(0.3)
    mo.run("to", str(x1), str(y1))
    time.sleep(0.3)
    mo.run("up")
    time.sleep(0.8)


def read_state(m):
    for i in reversed(dispcp.win_list(m, S)):
        x, y, w, h = dispcp.win_rect(m, S, i)
        if w != FD_W:
            continue
        r = m.read(S("wm_wins") + i * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        seg = dispcp._u16(r, 22)
        o88 = open("build/ftpd.o88", "rb").read()
        img = o88[8] | (o88[9] << 8)
        v = m.readseg(seg, img + FD_ST_OFF, 1)[0]
        return STATES.get(v, "?%d" % v)
    raise RuntimeError("the FTP window is gone")


def connect():
    f = ftplib.FTP()
    f.encoding = "latin-1"
    f.connect(HOST, CTRL, timeout=30)
    return f


def stor_paced(f, name, blob, tries=4):
    """One STOR, retried on the TRANSIENT 425.

    **BACK-TO-BACK TRANSFERS CAN LEGITIMATELY BE REFUSED.** The stack has
    NET_SOCKS = 4 handles for the whole machine (netpkg.inc), the listener
    holds one and the control connection another, so a data socket that is
    still closing when the next PORT arrives leaves nothing to hand out -
    and the server says 425, which is the TRANSIENT code, deliberately (it
    used to say 501, which tells a client never to try again). This paces
    and retries rather than asserting the server has an infinite supply,
    because the refusal is correct behaviour and only the timing is the
    harness's problem.
    """
    last = None
    for _ in range(tries):
        try:
            f.storbinary("STOR " + name, io.BytesIO(blob))
            time.sleep(1.2)
            return
        except ftplib.error_temp as e:
            last = e
            time.sleep(2.5)
    raise last


def retr(f, name):
    buf = io.BytesIO()
    f.retrbinary("RETR " + name, buf.write)
    return buf.getvalue()


def run_gate(m, mo, fails):
    ip = wait_dhcp(m)
    if not ip:
        fails.append("the card never bound an address - ETHER.DRV did not "
                     "attach, or DHCP never completed")
        return
    say("card up, address %s" % ip)

    launch(m, mo)
    say("FTPD launched and started")

    # --- 1: it answers ------------------------------------------------------
    f = None
    for _ in range(30):
        try:
            f = connect()
            break
        except (OSError, ftplib.all_errors):
            time.sleep(1.0)
    if f is None:
        fails.append("nothing answered on port 21 - the server did not start, "
                     "or Start was never clicked")
        return
    greet = f.getwelcome()
    say("greeting: %s" % greet)
    if not greet.startswith("220"):
        fails.append("the greeting is %r, not a 220" % greet)
    r = f.login("os8088", "os8088")
    say("login: %s" % r)
    if not r.startswith("230"):
        fails.append("login answered %r, not a 230" % r)

    # --- 2: it lists --------------------------------------------------------
    rows = []
    f.retrlines("LIST", rows.append)
    say("LIST returned %d rows" % len(rows))
    for row in rows:
        say("   " + row)
    names = set()
    for row in rows:
        parts = row.split()
        if parts:
            names.add(parts[-1])
    for want in ("FTPHELLO.TXT", "FTPBIN.DAT", "FTPD.O88", "DEEP"):
        if want not in names:
            fails.append("LIST does not mention %s (saw %s)"
                         % (want, sorted(names)))
    for row in rows:
        if row.endswith("DEEP") and not row.startswith("d"):
            fails.append("DEEP is a folder and its LIST row does not say so: "
                         "%r - no client will let the user into it" % row)

    # --- 3: RETR is byte-exact, text and binary -----------------------------
    got = retr(f, "FTPHELLO.TXT")
    if got != HELLO:
        fails.append("RETR FTPHELLO.TXT gave %r, wanted %r" % (got, HELLO))
    else:
        say("RETR FTPHELLO.TXT: %d bytes, exact" % len(got))

    got = retr(f, "FTPBIN.DAT")
    if got != BINDAT:
        n = sum(1 for i in range(min(len(got), len(BINDAT)))
                if got[i] != BINDAT[i])
        fails.append("RETR FTPBIN.DAT is wrong: %d bytes against %d, %d "
                     "differing in the overlap" % (len(got), len(BINDAT), n))
    else:
        say("RETR FTPBIN.DAT: %d bytes, every byte value, exact" % len(got))

    # --- 4: STOR round-trips, and the HOST reads the image ------------------
    # BIGGER THAN THE STAGE ON PURPOSE: the whole design is a stage-and-commit
    # loop (SPEC.md 77.1), and a file that fits one chunk never exercises
    # OSAPI_FILE_APPEND's cluster precondition at all.
    up = bytes((i * 7 + 13) & 0xFF for i in range(20000))
    r = f.storbinary("STOR UP.DAT", io.BytesIO(up))
    say("STOR UP.DAT: %s" % r)
    if not r.startswith("226"):
        fails.append("STOR answered %r, not a 226" % r)
    back = retr(f, "UP.DAT")
    if back != up:
        fails.append("STOR/RETR round trip is wrong: %d bytes back against "
                     "%d sent" % (len(back), len(up)))
    else:
        say("STOR/RETR round trip: %d bytes, exact" % len(up))

    size = f.size("UP.DAT")
    if size != len(up):
        fails.append("SIZE says %r, the file is %d" % (size, len(up)))

    # --- 5: it navigates ----------------------------------------------------
    f.cwd("DEEP")
    pwd = f.pwd()
    say("after CWD DEEP, PWD = %s" % pwd)
    if pwd != "/DEEP":
        fails.append("PWD says %r after CWD DEEP, wanted '/DEEP'" % pwd)
    deep = retr(f, "FTPHELLO.TXT")
    if deep != HELLO:
        fails.append("the copy in DEEP came back %r" % deep)
    f.cwd("..")
    pwd = f.pwd()
    if pwd != "/":
        fails.append("PWD says %r after CDUP, wanted '/'" % pwd)

    # --- 5b: a listing BIGGER than one stage loses nothing ------------------
    # BIG/ carries 150 files and a LIST row is ~61 bytes, so the listing
    # overflows the 8KB stage and the resumable walk (SPEC.md 77.5) runs at
    # least once. The defect class this pins: the resume ordinal was
    # committed BEFORE the row-fits test, so the entry that did not fit was
    # skipped on resume - exactly one file lost per stage boundary, invisibly,
    # in both LIST and NLST.
    names = f.nlst("BIG")
    say("NLST BIG: %d names" % len(names))
    if len(names) != 150:
        say("   the names that DID come back: %s" % sorted(names))
        fails.append("NLST BIG returned %d names, the disk holds 150 - the "
                     "stage-boundary resume is dropping entries" % len(names))
    else:
        want = {"F%03d.TXT" % i for i in range(150)}
        got = {n.rsplit("/", 1)[-1].upper() for n in names}
        if got != want:
            fails.append("NLST BIG names differ: missing %s, extra %s"
                         % (sorted(want - got)[:3], sorted(got - want)[:3]))
        else:
            say("all 150 names present - the boundary rows survived the resume")
    rows = []
    f.retrlines("LIST BIG", rows.append)
    if len(rows) != 150:
        fails.append("LIST BIG returned %d rows, the disk holds 150" % len(rows))

    try:
        f.cwd("NOSUCH")
        fails.append("CWD NOSUCH was accepted")
    except ftplib.error_perm:
        pass
    pwd = f.pwd()
    if pwd != "/":
        fails.append("a FAILED CWD moved the session to %r - every later bare "
                     "name now resolves in the wrong folder, silently" % pwd)

    # --- 5b: MKD and RMD, and RMD's refusal to empty a folder ---------------
    # **THE REFUSAL IS THE ASSERTION HERE, not the removal.** RFC 959's RMD is
    # specified to fail on a non-empty directory, and the recursive form of
    # OSAPI_FILE_RMDIR (SPEC.md 18.90.2) is one register away - so a server
    # that reached for it would pass a "does RMD work" test while silently
    # destroying a tree the client believed it was protecting.
    r = f.mkd("NEWDIR")
    say("MKD NEWDIR -> parsed path %r" % r)
    # ftplib's mkd() runs parse257, which pulls the path out of the QUOTED
    # field RFC 959 gives a 257. It answers '' rather than raising when the
    # field is missing - so an empty answer here is the reply being malformed
    # in a way the client tolerates, which is how it shipped once already.
    if r != "NEWDIR":
        fails.append("MKD's 257 parsed as %r, not 'NEWDIR' - the reply is not "
                     "carrying the quoted path RFC 959 asks for" % r)
    rows = []
    f.retrlines("LIST", rows.append)
    if not any(r.split()[-1] == "NEWDIR" and r.startswith("d") for r in rows):
        fails.append("MKD NEWDIR did not produce a folder row in LIST")
    try:
        f.rmd("DEEP")
        fails.append("RMD emptied DEEP, which has a file in it - the server "
                     "reached for the RECURSIVE form and RFC 959 says it must "
                     "not")
    except ftplib.error_perm as e:
        say("RMD on a non-empty folder refused: %s" % e)
        if not str(e).startswith("550"):
            fails.append("RMD on a non-empty folder answered %r, not a 550" % e)
    deep = retr(f, "DEEP/FTPHELLO.TXT")
    if deep != HELLO:
        fails.append("the refused RMD damaged DEEP's contents")
    f.rmd("NEWDIR")
    rows = []
    f.retrlines("LIST", rows.append)
    if any(r.split()[-1] == "NEWDIR" for r in rows):
        fails.append("RMD NEWDIR left the folder in the listing")
    else:
        say("RMD NEWDIR: gone")

    f.quit()

    # --- 6: Read Only refuses ----------------------------------------------
    fx, fy = ftp_win(m)
    mo.click(*ro_box(fx, fy))
    time.sleep(1.0)
    f = connect()
    f.login("os8088", "os8088")
    refused = False
    try:
        f.storbinary("STOR NO.DAT", io.BytesIO(b"x" * 64))
    except ftplib.error_perm as e:
        refused = str(e).startswith("550")
        say("Read Only refused STOR: %s" % e)
    if not refused:
        fails.append("STOR was accepted with Read Only ticked")
    rows = []
    f.retrlines("LIST", rows.append)
    if any("NO.DAT" in r for r in rows):
        fails.append("Read Only refused the STOR and the file appeared anyway")
    try:
        f.mkd("NOPE")
        fails.append("MKD was accepted with Read Only ticked")
    except ftplib.error_perm:
        pass
    try:
        f.rmd("DEEP")
        fails.append("RMD was accepted with Read Only ticked")
    except ftplib.error_perm:
        say("Read Only refused MKD and RMD too")
    f.quit()

    # --- 8: ACTIVE mode, which needs no address from the server -------------
    # It had NO coverage, and it is the answer for a machine behind NAT with
    # nothing configured - so it is the first thing to check after a report
    # that passive times out.
    # A BEAT BETWEEN SESSIONS, and it is not padding. Four handles
    # (netpkg.inc) and the session just ended still holds one while TCP
    # finishes its close, so a data connection opened immediately can find
    # nothing free. The server answers 425 for that now rather than 501, so a
    # real client retries - but this gate asserts the FIRST attempt, which
    # means it has to give the previous one time to drain.
    time.sleep(4.0)
    f = connect()
    f.login("os8088", "os8088")
    f.set_pasv(False)
    rows = []
    try:
        f.retrlines("LIST", rows.append)
        say("ACTIVE LIST: %d rows" % len(rows))
        if not rows:
            fails.append("active-mode LIST returned nothing")
    except Exception as e:
        fails.append("active mode (PORT) failed: %s: %s" % (type(e).__name__, e))
    try:
        got = retr(f, "FTPHELLO.TXT")
        if got != HELLO:
            fails.append("active-mode RETR gave %r" % got)
        else:
            say("ACTIVE RETR: exact")
    except Exception as e:
        fails.append("active-mode RETR failed: %s: %s" % (type(e).__name__, e))
    f.quit()

    # --- 7: a client that TRUSTS the 227, which is WinSCP's behaviour -------
    # Untick Read Only first (the box is still set from assertion 6), then set
    # the PASV override through the Setup page and prove a trusting client can
    # transfer. Without the override the server advertises its own 10.0.2.15
    # and this times out - which is exactly what the field reported.
    fx, fy = ftp_win(m)
    mo.click(*ro_box(fx, fy))
    time.sleep(1.0)
    set_pasv_override(m, mo, "127.0.0.1")

    f = ftplib.FTP()
    f.encoding = "latin-1"
    f.trust_server_pasv_ipv4_address = True     # WinSCP, not ftplib's default
    f.connect(HOST, CTRL, timeout=30)
    f.login("os8088", "os8088")
    raw = f.sendcmd("PASV")
    say("227 as a trusting client sees it: %s" % raw)
    if "127,0,0,1" not in raw:
        fails.append("the PASV override did not reach the 227: %r" % raw)
    rows = []
    try:
        f.retrlines("LIST", rows.append)
        say("TRUSTING LIST: %d rows" % len(rows))
        if not rows:
            fails.append("a client trusting the 227 got an empty listing")
    except Exception as e:
        fails.append("a client trusting the 227 could not transfer: %s: %s "
                     "- the PASV override is not working, which is the whole "
                     "reason it exists" % (type(e).__name__, e))
    f.quit()

    say("--- 8b. a long name is COLLAPSED, not refused (SPEC.md 77.20) ---")

    # THE FIELD'S OTHER HALF. `banana split.mod` failed instantly with
    # `552 disk full or protected` on a volume with 20MB free: the name was
    # the problem and the reply named the disk, which sent the investigation
    # the wrong way entirely. Now it is collapsed the way every DOS-era
    # system displayed a long name.
    f = connect()
    f.login("os8088", "os8088")
    f.set_pasv(False)
    blob = b"M.K." + bytes(range(256)) * 4
    want = [("banana split.mod", "BANANA~1.MOD"),   # a space AND too long
            ("elysium longname.mod", "ELYSIU~1.MOD"),
            ("no_ext_at_all_here", "NO_EXT~1"),     # no extension at all
            ("....", "FILE~1")]                     # nothing legal survives
    for sent, _ in want:
        try:
            stor_paced(f, sent, blob)
        except Exception as e:
            fails.append("STOR %r was refused (%s) - SPEC.md 77.20 says it is "
                         "collapsed, not refused" % (sent, str(e).strip()))
    rows = []
    f.retrlines("LIST", rows.append)
    have = {r.split()[-1] for r in rows if r.split()}
    for sent, expect in want:
        if expect in have:
            say("%-22r -> %s" % (sent, expect))
        else:
            fails.append("STOR %r should have landed as %s; the directory "
                         "holds %s" % (sent, expect, sorted(have)))

    # A NAME THAT IS ALREADY LEGAL IS LEFT ALONE - the property that makes
    # the mapping round-trip, because LIST prints the stored name and the
    # client asks for it back.
    stor_paced(f, "PLAIN.TXT", blob)
    rows = []
    f.retrlines("LIST", rows.append)
    have = {r.split()[-1] for r in rows if r.split()}
    if "PLAIN.TXT" not in have:
        fails.append("PLAIN.TXT is already a legal 8.3 name and must pass "
                     "through untouched; the directory holds %s" % sorted(have))
    elif "PLAIN~1.TXT" in have:
        fails.append("PLAIN.TXT was mangled to PLAIN~1.TXT - mangling a legal "
                     "name breaks every name LIST just printed")
    else:
        say("PLAIN.TXT untouched, so what LIST prints is what RETR takes")

    # ...and the collapsed name really is fetchable by the name LIST gave.
    buf = io.BytesIO()
    for _ in range(4):
        try:
            buf = io.BytesIO()
            f.retrbinary("RETR BANANA~1.MOD", buf.write)
            break
        except ftplib.error_temp:
            time.sleep(2.5)
    if buf.getvalue() != blob:
        fails.append("RETR BANANA~1.MOD did not return what STOR "
                     "'banana split.mod' sent - the mapping does not "
                     "round-trip")
    else:
        say("RETR BANANA~1.MOD is byte-exact, so the collapse round-trips")
    f.quit()

    say("--- 8d. a transfer STRAIGHT after another does not stall "
        "(SPEC.md 72.14) ---")

    # THE 6-SECOND HOLE THE FIELD MEASURED AT THE START OF AN UPLOAD, before
    # one byte of disk work and with the window at its ceiling. Four slots and
    # one FTP session accounts for all four, so the data socket a LIST just
    # closed sits in TIME-WAIT holding the fourth - and the next transfer's
    # inbound SYN is dropped, which from this side looks like nothing at all
    # happening. sk_alloc reaps a TIME-WAIT slot rather than refusing.
    #
    # **NO SLEEP BETWEEN THE TWO, ON PURPOSE.** TCP_TWTMO is two seconds and
    # every real client starts its next transfer well inside them; a gate that
    # waits politely is a gate that tests the case that already worked.
    # Measured across the fix on this exact sequence: 6.15s -> 0.39s.
    f = connect()
    f.login("os8088", "os8088")
    f.retrlines("LIST", lambda _l: None)
    t0 = time.time()
    f.storbinary("STOR TWTEST.DAT", io.BytesIO(b"z" * 8192))
    dt = time.time() - t0
    f.quit()
    if dt > 3.0:
        fails.append("a STOR issued straight after a LIST took %.1fs - the "
                     "socket pool is starved by the LIST's TIME-WAIT and the "
                     "client is sitting through its own SYN backoff "
                     "(SPEC.md 72.14)" % dt)
    else:
        say("STOR immediately after LIST: %.2fs, so the pool did not starve"
            % dt)

    say("--- 8c. an aborted transfer gives its socket back (SPEC.md 77.25) ---")

    # THE FIELD'S LAST FAILURE. NET_SOCKS is four: the port-21 listener, the
    # control connection and a passive listener are three, and a data socket
    # left finishing a FIN handshake with a client that has gone is the
    # fourth - so the NEXT session's NETV_ACCEPT has no slot and its LIST
    # hangs at the 150 with nothing logged. It cleared only when the
    # retransmit timer expired, about a minute.
    #
    # Driven the way it actually happens: start a STOR, walk away from it
    # WITHOUT closing politely, then immediately ask a fresh session to list.
    # Before NETV_ABORT that second session hung.
    import socket as _s
    # **BREATHING ROOM, and it is the assertion above that needs it.** 8b runs
    # seven transfers back to back; a RETR and a LIST must close their data
    # socket gracefully (the close IS the end-of-file), and each close holds
    # its slot for a FIN round trip. With NET_SOCKS = 4 that is real pressure
    # - SPEC.md 77.28 - and this assertion is about recovery after an ABORT,
    # not about that. So it waits for the pool rather than racing it.
    time.sleep(6.0)
    try:
        f = connect()
        f.login("os8088", "os8088")
        f.set_pasv(False)
        sock = f.transfercmd("STOR ABORTME.DAT")
        sock.sendall(b"x" * 4096)
    except Exception as e:
        say("   (first try: %s - waiting for the socket pool)"
            % str(e).strip()[:50])
        time.sleep(20.0)
        try:
            f = connect()
            f.login("os8088", "os8088")
            f.set_pasv(False)
            sock = f.transfercmd("STOR ABORTME.DAT")
            sock.sendall(b"x" * 4096)
            return_early = False
        except Exception as e2:
            fails.append("could not START the transfer this assertion aborts, "
                         "even after 20s (%s) - SPEC.md 77.28's socket "
                         "pressure is worse than it is documented to be"
                         % str(e2).strip()[:70])
            return_early = True
    else:
        return_early = False
    if return_early:
        sock = None
    if sock is not None:
        sock.setsockopt(_s.SOL_SOCKET, _s.SO_LINGER, struct.pack("ii", 1, 0))
        sock.close()                    # RST, and the control socket too
        f.sock.setsockopt(_s.SOL_SOCKET, _s.SO_LINGER, struct.pack("ii", 1, 0))
        try:
            f.sock.close()
        except Exception:
            pass
    # **HOW LONG IT TAKES IS PART OF THE ASSERTION.** The first version waited
    # four seconds and failed, and the failure was the harness's: the server
    # does recover - fd_st goes back to FD_LISTEN with both handles at 0 - but
    # not instantly, because the worker has to poll the dead control socket
    # before fd_bye can run. So this WAITS FOR THE STATE and reports the time,
    # which is the number a user retrying after a failed upload feels.
    t0 = time.time()
    while time.time() - t0 < 90:
        if read_state(m) == "listening":
            break
        time.sleep(2.0)
    say("back to listening %.0fs after the client vanished" % (time.time() - t0))

    ok = False
    for attempt in range(3):
        try:
            g = connect()
            g.login("os8088", "os8088")
            g.set_pasv(False)
            rows = []
            g.retrlines("LIST", rows.append)
            say("after an aborted transfer, LIST works (%d rows, attempt %d)"
                % (len(rows), attempt))
            g.quit()
            ok = True
            break
        except Exception as e:
            say("   attempt %d: %s" % (attempt, str(e).strip()[:60]))
            time.sleep(10.0)
    if not ok:
        fails.append("after an aborted transfer the next session could not "
                     "LIST - the data socket's slot was not released, which "
                     "is what SPEC.md 77.25's NETV_ABORT exists to do")

    # === 9. AUTHENTICATION (SPEC.md 77.15) ==================================
    say("")
    say("--- 9. a configured user and password ---")
    setup(m, mo, fields=((F_USER, "bob"), (F_PASS, "s3cret")))

    f = ftplib.FTP()
    f.encoding = "latin-1"
    f.connect(HOST, CTRL, timeout=30)
    try:
        f.login("os8088", "os8088")
        fails.append("the server accepted os8088/os8088 with bob/s3cret "
                     "configured - the User setting is not a gate at all")
    except ftplib.error_perm as e:
        say("wrong credentials refused: %s" % str(e).strip())
    f.close()

    # THE NAME IS FOLDED AND THE PASSWORD IS NOT, so `BOB` must work and a
    # differently-cased password must not. One connection each: a 530 leaves
    # [fd_auth] clear but the control connection open, and a client that
    # retries on the same one is testing something this server does not
    # promise.
    f = ftplib.FTP()
    f.encoding = "latin-1"
    f.connect(HOST, CTRL, timeout=30)
    try:
        f.login("bob", "S3CRET")
        fails.append("the server accepted S3CRET for s3cret - the PASSWORD is "
                     "being folded, which throws bits away (SPEC.md 77.15)")
    except ftplib.error_perm:
        say("a differently-cased PASSWORD refused, as it must be")
    f.close()

    f = ftplib.FTP()
    f.encoding = "latin-1"
    f.trust_server_pasv_ipv4_address = True
    f.connect(HOST, CTRL, timeout=30)
    try:
        f.login("BOB", "s3cret")
        rows = []
        f.retrlines("LIST", rows.append)
        say("BOB/s3cret logged in and listed %d rows - the NAME is folded"
            % len(rows))
        if not rows:
            fails.append("an authenticated client got an empty listing")
    except ftplib.error_perm as e:
        fails.append("BOB/s3cret was refused: %s - the user name is being "
                     "compared case-SENSITIVELY (SPEC.md 77.15)" % e)
    f.quit()

    # === 10. A SELECTABLE ROOT (SPEC.md 77.16.1) ============================
    # `Root` is a PATH in FTPD.CFG and a (drive, cluster) in the session,
    # walked once. DEEP holds exactly one file and no folder, so a session
    # rooted there is unmistakable from one rooted at B:'s own root.
    say("")
    say("--- 10. a selectable root ---")
    setup(m, mo, fields=((F_ROOT, "DEEP"),))

    f = ftplib.FTP()
    f.encoding = "latin-1"
    f.trust_server_pasv_ipv4_address = True
    f.connect(HOST, CTRL, timeout=30)
    f.login("BOB", "s3cret")
    rows = []
    f.retrlines("LIST", rows.append)
    names = sorted(r.split()[-1] for r in rows if r.split())
    say("rooted at DEEP, / holds %r" % names)
    if names != ["FTPHELLO.TXT"]:
        fails.append("a session rooted at DEEP lists %r - it must be DEEP\'s "
                     "own contents and nothing above them" % names)
    if f.pwd() != "/":
        fails.append("PWD in the served root is %r, not '/'" % f.pwd())

    # ...AND THE SESSION NEVER SEES ABOVE IT. CDUP at the root succeeds and
    # stays put, which is what every FTP server does - a 550 to a client's
    # "go to the top" loop makes it fail.
    f.cwd("..")
    if f.pwd() != "/":
        fails.append("CDUP at the served root moved to %r" % f.pwd())
    rows = []
    f.retrlines("LIST", rows.append)
    names = sorted(r.split()[-1] for r in rows if r.split())
    if names != ["FTPHELLO.TXT"]:
        fails.append("CDUP at the served root escaped it: %r" % names)
    else:
        say("CDUP at the served root stays put, as it must")
    f.quit()

    # === 11. WHOLE-MACHINE MODE (SPEC.md 77.16) =============================
    # The root becomes the LEVEL ABOVE every volume: one row per mounted
    # drive, and a CWD into one lands in that volume's root. It is the only
    # place in this server where the thing being listed is not a directory.
    say("")
    say("--- 11. whole-machine mode ---")
    setup(m, mo, machine=True)

    f = ftplib.FTP()
    f.encoding = "latin-1"
    f.trust_server_pasv_ipv4_address = True
    f.connect(HOST, CTRL, timeout=30)
    f.login("BOB", "s3cret")
    rows = []
    f.retrlines("LIST", rows.append)
    say("machine root: %r" % rows)
    names = [r.split()[-1] for r in rows if r.split()]
    if "A" not in names or "B" not in names:
        fails.append("the machine root listed %r - it must carry one row per "
                     "MOUNTED volume, and this machine has A: and B:" % names)
    for r in rows:
        if not r.startswith("d"):
            fails.append("a volume row is not a directory: %r" % r)
    if f.pwd() != "/":
        fails.append("PWD at the machine root is %r, not '/'" % f.pwd())

    # ...and stepping into one is a real volume, with the apps disk's own
    # folders in it. B: is TESTAPPS, whose root this gate has been serving.
    f.cwd("B")
    if f.pwd() != "/B":
        fails.append("PWD inside a volume is %r, not '/B'" % f.pwd())
    vrows = []
    f.retrlines("LIST", vrows.append)
    vnames = [r.split()[-1] for r in vrows if r.split()]
    say("B: holds %r" % vnames)
    if "DEEP" not in vnames:
        fails.append("B: does not list DEEP - stepping into a volume did not "
                     "land in its root (%r)" % vnames)

    # A BARE NAME AT THE MACHINE ROOT IS REFUSED, which is the guard in
    # fd_split's `.bare`: there is no directory to resolve it in, and
    # resolving it in whichever volume was current last serves a folder the
    # client was never shown.
    f.cwd("..")
    if f.pwd() != "/":
        fails.append("CDUP from a volume root is %r, not the machine level"
                     % f.pwd())
    try:
        retr(f, "FTPHELLO.TXT")
        fails.append("a bare name at the MACHINE root was served - it "
                     "resolved in whatever volume happened to be current "
                     "(SPEC.md 77.16.2)")
    except ftplib.error_perm:
        say("a bare name at the machine root refused, as it must be")

    # ...but an ABSOLUTE one through a volume works, which is the whole
    # payoff of putting the branch in fd_enter rather than in each command.
    got = retr(f, "/B/FTPHELLO.TXT")
    if got != HELLO:
        fails.append("RETR /B/FTPHELLO.TXT gave %r - an absolute path "
                     "through a volume must reach the file" % got[:40])
    else:
        say("RETR /B/FTPHELLO.TXT is byte-exact through the machine root")
    f.quit()

    # === 12. THE SETUP BUTTON, PERSISTED READ ONLY, AND A BAD ROOT ==========
    # Assertions 7-11 already drove the page through the BUTTON, so what is
    # left here is the three things they could not: that the menu item still
    # works, that Read Only is a persisted setting now and not just a live
    # switch, and that a root which will not resolve REFUSES rather than
    # quietly serving somewhere else.
    say("")
    say("--- 12. the Setup button, Read Only as a setting, and a bad root ---")

    # (a) THE MENU ITEM STILL WORKS. A pull-down nobody drives is one that
    #     breaks quietly, and the button is now the way in for everything
    #     above. Read Only is ticked and untoggled in two visits, so the
    #     machine ends as it started.
    setup(m, mo, readonly=True, use_menu=True)
    setup(m, mo, readonly=True, use_menu=True)
    say("the menu item still toggles the page")

    # (b) READ ONLY SET ON THE SETUP PAGE GATES THE SERVER, which also proves
    #     the page's box and the toolbar's are ONE setting.
    setup(m, mo, readonly=True)
    f = ftplib.FTP()
    f.encoding = "latin-1"
    f.trust_server_pasv_ipv4_address = True
    f.connect(HOST, CTRL, timeout=30)
    f.login("BOB", "s3cret")
    try:
        f.storbinary("STOR RO.DAT", io.BytesIO(b"x" * 16))
        fails.append("Read Only ticked on the SETUP page did not refuse a "
                     "STOR - the two boxes are not the same setting")
    except ftplib.error_perm as e:
        say("Read Only set on the Setup page refused STOR: %s"
            % str(e).strip())
    f.quit()
    setup(m, mo, readonly=True)         # ...and off again

    # (c) A DOS-SHAPED ROOT WORKS, because that is what a person standing at
    #     this machine types. `B:\DEEP` must be the same folder `DEEP` was in
    #     assertion 10 - the ROOT field takes either separator, while an FTP
    #     path stays '/' alone.
    setup(m, mo, fields=((F_ROOT, "B:\\DEEP"),), machine=True)
    restart(m, mo)                      # the root is walked at START
    f = ftplib.FTP()
    f.encoding = "latin-1"
    f.trust_server_pasv_ipv4_address = True
    f.connect(HOST, CTRL, timeout=30)
    f.login("BOB", "s3cret")
    rows = []
    f.retrlines("LIST", rows.append)
    names = sorted(r.split()[-1] for r in rows if r.split())
    say("rooted at B:\\DEEP, / holds %r" % names)
    if names != ["FTPHELLO.TXT"]:
        fails.append("a backslash root served %r - the Root field must take "
                     "`B:\\DEEP` as well as `B:/DEEP`, because a DOS box is "
                     "what the person typing it is looking at" % names)
    f.quit()

    # (d) A ROOT THAT WILL NOT RESOLVE REFUSES TO START. This is the one the
    #     field reported: a typed root served a DIFFERENT folder and nothing
    #     on screen said the setting had not taken. Serving somewhere else is
    #     a failure the user cannot see, so the server stops and says so.
    setup(m, mo, fields=((F_ROOT, "/NOSUCH"),))
    restart(m, mo)
    st = read_state(m)
    say("with a bad Root, the server state is %r" % st)
    if st != "err":
        fails.append("a Root that does not resolve left the server in state "
                     "%r - it must REFUSE to start (SPEC.md 77.16.1)" % st)
        try:
            g = ftplib.FTP()
            g.encoding = "latin-1"
            g.connect(HOST, CTRL, timeout=10)
            g.login("BOB", "s3cret")
            rows = []
            g.retrlines("LIST", rows.append)
            fails.append("...and it served %r instead"
                         % [r.split()[-1] for r in rows if r.split()])
            g.quit()
        except Exception:
            pass
    else:
        # ...and NOTHING is listening, which is the other half: a refused
        # Start must not leave the port open on a server that will not serve.
        try:
            g = ftplib.FTP()
            g.encoding = "latin-1"
            g.connect(HOST, CTRL, timeout=8)
            g.close()
            fails.append("the server refused to start and something is still "
                         "answering on port 21 - fd_start must close the "
                         "listener it had already opened")
        except (OSError, EOFError, ftplib.Error):
            say("...and nothing is listening, so the port went back")

    # The root is left as /NOSUCH ON PURPOSE: verify_cfg reads it back off the
    # image, which is what says a refused Start still SAVED the setting the
    # user typed rather than reverting it behind their back.

    say("--- 8e. the log does not paint over the window ON TOP of it "
        "(SPEC.md 77.33) ---")

    # THE FIELD SAW THE FTP LOG PRINTED ACROSS THE FRONT OF A DISK WINDOW.
    # fd_flush_glass and the UI wake both take the gfx lock THEMSELVES - they
    # are not W_PAINT callbacks, so nothing has armed a clip for them - and
    # the gfx_* primitives take ABSOLUTE screen coordinates. Without
    # OSAPI_WM_CLIP_SET a background painter draws straight over whatever is
    # in front of it.
    #
    # Driven the way it happens: raise the Disk window over the FTP window,
    # photograph the overlap, then make the server log a line and photograph
    # it again. Those pixels belong to the Disk window and must not move.
    import subprocess as _sp
    fw = ftp_win(m)
    disk = None
    for w in dispcp.win_list(m, S):
        r = dispcp.win_rect(m, S, w)
        if (r[2], r[3]) != (FD_W, FD_H):
            disk = r
    if disk is None:
        fails.append("no Disk window to put in front of the FTP window - "
                     "this assertion needs one to overlap with")
    else:
        dx, dy, dw, dh = disk
        # the overlap: the Disk window's own area, which the FTP log sits under
        crop = "%d,%d,%d,%d" % (dx + 2, dy + 20, dw - 8, dh - 40)
        _sp.run(["python3", "tools/shot.py", SOCK, "/tmp/ftpclip_0.png",
                 "--crop", crop], capture_output=True)
        # **NOT ITS TITLE BAR** - the Disk window is at (103,80,320,200) and
        # the FTP window at (40,40,400,176), so that title is UNDERNEATH the
        # window this is trying to put it in front of and the click lands on
        # the FTP server. Click the part that shows, below the FTP window's
        # bottom edge; anywhere in a window raises it.
        mo.click(dx + dw // 2, dy + dh - 30)
        time.sleep(1.5)
        _sp.run(["python3", "tools/shot.py", SOCK, "/tmp/ftpclip_a.png",
                 "--crop", crop], capture_output=True)
        # **THE SETUP CHECKS ITSELF.** The first version of this clicked four
        # pixels too high, never raised anything, and cropped the FTP log -
        # which changes on every line by design, so it reported the bug it was
        # written to detect on a build that had just been fixed. A gate whose
        # premise is unverified is a gate that lies in the expensive direction.
        if open("/tmp/ftpclip_0.png", "rb").read() == \
                open("/tmp/ftpclip_a.png", "rb").read():
            fails.append("clicking the Disk window's title bar changed "
                         "nothing - it never came to the front, so this "
                         "assertion has no window on top to protect")
        # A CONTROL CONNECTION ONLY - no LIST. 8b above runs seven transfers
        # back to back and each close holds its slot for TCP_TWTMO, so a data
        # connection here races the pool for no reason: `Client connected`,
        # `USER`, `PASS` are four log lines and that is all this needs.
        time.sleep(3.0)
        for _try in range(3):
            try:
                f = connect()
                f.login("os8088", "os8088")
                f.quit()
                break
            except Exception as e:
                say("   (connect %d: %s)" % (_try, str(e).strip()[:40]))
                time.sleep(4.0)
        time.sleep(2.5)
        _sp.run(["python3", "tools/shot.py", SOCK, "/tmp/ftpclip_b.png",
                 "--crop", crop], capture_output=True)
        a = open("/tmp/ftpclip_a.png", "rb").read()
        b = open("/tmp/ftpclip_b.png", "rb").read()
        if a != b:
            fails.append("the Disk window's pixels CHANGED while the FTP "
                         "server logged underneath it - the log is painting "
                         "over the window on top (SPEC.md 77.33). See "
                         "/tmp/ftpclip_a.png and _b.png")
        else:
            say("the window on top is untouched while the log scrolls under it")

    say("--- 13. the controls are STANDARD: press, slide off, release ---")

    # THE WHOLE POINT OF SPEC.md 77.18. Every control used to fire on the
    # PRESS, so a mis-aimed press acted and there was no way to change your
    # mind. `click` cannot see the difference - it presses and releases in one
    # place - so this drives the three edges apart.
    #
    # It is asserted on the PAGE and not on the server, because assertion 12
    # deliberately leaves a Root that will not resolve: the state machine is
    # in `err` here and Start is the wrong control to prove a gesture with.
    # The page is also the one control that is unambiguous in a screenshot-
    # free assertion - it is a byte in the package's own bss.
    fx, fy = ftp_win(m)
    sx, sy = setup_btn(fx, fy)
    was = read_page(m)
    press_slide_release(mo, sx, sy, fx + 20, fy + TITLE_H + 120)
    now = read_page(m)
    if now != was:
        fails.append("a press on Setup that was SLID OFF before the release "
                     "still fired: the page went %s -> %s. SPEC.md 77.18's "
                     "release must re-find the control and refuse"
                     % (was, now))
    else:
        say("pressed Setup, slid off, released: still on '%s' - cancelled"
            % now)

    # ...and the same gesture WITHOUT the slide still fires, so the refusal
    # above is a cancel and not a control that has stopped working.
    mo.click(sx, sy)
    time.sleep(1.2)
    now = read_page(m)
    if now != "setup":
        fails.append("a press and release ON Setup did not open the page "
                     "(still %r) - the control is dead, not cancel-safe" % now)
    else:
        say("pressed and released ON Setup: log -> setup")
        # Done is in Start's corner, and it is the SETUP page's main control -
        # so this also proves the second rect table is wired to the right one.
        mo.click(*start_btn(fx, fy))
        time.sleep(1.2)
        back = read_page(m)
        if back != "log":
            fails.append("Done did not come back to the log page (%r) - the "
                         "SETUP rect table's main control is not Done" % back)
        else:
            say("...and Done, in Start's corner, came back to the log")

    # --- and the HOST reads the image, with no os8088 code in the way -------
    verify_host(fails, up)
    verify_cfg(fails)


def setup(m, mo, fields=(), machine=None, readonly=None, use_menu=False):
    """Drive the Setup page: menu, click each field, type, tick, menu again.

    THROUGH THE UI AND NOT BY POKING THE BSS, because what is under test is
    the whole path - the menu item, the line editor, the parse, the save to
    FTPD.CFG and the reader taking it back out. A poke would prove the last
    step and none of the others.

    `fields` is (index, text) pairs; the text is TYPED, so a field is only
    ever appended to - every caller here sets a field that was empty.
    """
    fx, fy = ftp_win(m)
    enter(m, mo, fx, fy, use_menu)
    time.sleep(1.0)
    for idx, text in fields:
        mo.click(*field_pt(fx, fy, idx))
        time.sleep(0.6)
        # CLEARED FIRST. os88line has no select-all, so a field is only ever
        # appended to - and the Root field is set three times in this gate,
        # which would otherwise give `/NOSUCHB:\DEEP`. End, then enough
        # Backspaces for anything this file types.
        subprocess.run(["python3", "tools/qmp.py", SOCK, "sendkey end"]
                       + ["sendkey backspace", "sleep 0.05"] * 24,
                       check=True, capture_output=True)
        time.sleep(0.3)
        type_text(text)
        time.sleep(0.4)
    if readonly is not None:
        mo.click(*sro_box(fx, fy))
        time.sleep(0.8)
    if machine is not None:
        mo.click(*mach_box(fx, fy))
        time.sleep(0.8)
    leave(m, mo, fx, fy, use_menu)  # leaving is what commits and saves
    time.sleep(2.0)
    say("Setup%s: %s%s%s"
        % (" (by menu)" if use_menu else "",
           ", ".join("field %d = %r" % f for f in fields) or "nothing typed",
           "" if readonly is None else ", Read Only toggled",
           "" if machine is None else ", whole-machine toggled"))


def enter(m, mo, fx, fy, use_menu=False):
    """INTO Setup - the toolbar's right-hand button, or the menu item.

    The button is the way in now (SPEC.md 77.17); the menu item still works
    and assertion 12 is what keeps it working, because a pull-down nobody
    drives is a pull-down that breaks quietly.
    """
    if use_menu:
        menu_setup(m, mo, fx, fy)
    else:
        mo.click(*setup_btn(fx, fy))
        time.sleep(0.8)


def leave(m, mo, fx, fy, use_menu=False):
    """OUT of Setup - and it is a DIFFERENT button, in a different corner.

    Done sits where Start does, top-left: that corner is the primary action of
    whichever page you are looking at. Clicking the way-in button to come back
    out lands on empty space, and the page never leaves - which is silent,
    because leaving is what COMMITS, so the setting is typed, visible, and
    never saved. That is exactly how this harness first read a working build
    as a broken PASV override.
    """
    if use_menu:
        menu_setup(m, mo, fx, fy)
    else:
        mo.click(*start_btn(fx, fy))
        time.sleep(0.8)


def set_pasv_override(m, mo, addr):
    setup(m, mo, fields=((F_PASV, addr),))


# The app's menu bar cell and its third item, MEASURED on a running machine
# rather than derived: `Server` spans x 97..143 under the chip menu and the
# `Ftpd` name cell, and MENU_ITEM_H puts `Setup...` at y 56.
MENU_X, MENU_Y, SETUP_Y = 120, 8, 56


def menu_setup(m, mo, fx, fy):
    """Server > Setup..., through the real menu bar.

    **A MENU IS PRESS, DRAG, RELEASE - never a click** (CLAUDE.md): menu_track
    draws the pull-down and then polls a level, so a press-and-release in place
    opens it and closes it in the same breath. tools/mouse.py has no `menu`
    verb - that is os88mouse.py, the MartyPC one - so it is down / to / up.
    """
    mo.run("down", str(MENU_X), str(MENU_Y))
    time.sleep(0.5)
    mo.run("to", str(MENU_X), str(SETUP_Y))
    time.sleep(0.4)
    mo.run("up")
    time.sleep(0.8)


# QMP's `sendkey` takes KEY names, not characters, so a shifted character is
# `shift-<key>` and the punctuation has names of its own. Only what this gate
# actually types is here - an unmapped character exits rather than sending a
# plausible wrong key, because a field that quietly received something else is
# a failure that reads as the setting not working.
QCHR = {".": "dot", "/": "slash", ":": "shift-semicolon", "-": "minus",
        "_": "shift-minus", "\\": "backslash"}


def type_text(text):
    cmds = []
    for ch in text:
        if ch.isdigit():
            cmds += ["sendkey " + ch]
        elif "a" <= ch <= "z":
            cmds += ["sendkey " + ch]
        elif "A" <= ch <= "Z":
            cmds += ["sendkey shift-" + ch.lower()]
        elif ch in QCHR:
            cmds += ["sendkey " + QCHR[ch]]
        else:
            sys.exit("ftpd: no sendkey mapping for %r" % ch)
        cmds += ["sleep 0.08"]
    subprocess.run(["python3", "tools/qmp.py", SOCK] + cmds,
                   check=True, capture_output=True)


def verify_host(fails, up):
    """**ASKING os8088 WHETHER os8088 SAVED IT IS NOT A TEST.**

    The writer and the reader are the same FAT12 code, so the failure that
    matters most - both halves agreeing on the same wrong thing - is the one
    that cannot be seen from inside (docs/FIELD-NOTES.md 4). This walks the
    image with tools/os88disk.py's own independent reader instead.

    The guest wrote to the MOUNTED image, so QEMU has already flushed it by
    the time it quit.
    """
    r = subprocess.run(["python3", "tools/os88disk.py", "--verify", APPIMG],
                       capture_output=True, text=True)
    if r.returncode:
        fails.append("the image does not fsck after the upload:\n"
                     + r.stdout + r.stderr)
        return
    say("host fsck of %s: clean" % APPIMG)
    data = extract(APPIMG, "UP      DAT")
    if data is None:
        fails.append("UP.DAT is not in the image's root when read on the HOST")
    elif data[:len(up)] != up:
        fails.append("UP.DAT on the image differs from what was sent - the "
                     "server and its own reader agree on the wrong bytes")
    else:
        say("UP.DAT read off the image by an independent reader: %d bytes, "
            "exact" % len(up))


def verify_cfg(fails):
    """FTPD.CFG is ON THE DISK, parsed by a reader that shares no code with it.

    **THE IN-SESSION EFFECT IS NOT THE PERSISTENCE.** Assertion 7 proves the
    override reaches the 227, which it does whether or not anything was ever
    written - and on the first run of this gate nothing was: the test disk had
    no SYSTEM/APPDATA, so fd_data_enter refused and the save said nothing,
    exactly as SPEC.md 19.9 asks it to. The setting worked all session and was
    gone on the next launch, and every assertion still passed.
    """
    data = extract(APPIMG, "FTPD    CFG", ("SYSTEM     ", "APPDATA    "))
    if data is None:
        fails.append("FTPD.CFG is not on the image - the setting was never "
                     "persisted, and SPEC.md 77.12 is half a feature")
        return
    if data[:8] != b"O88FTPD\0":
        fails.append("FTPD.CFG's magic is %r" % data[:8])
        return
    ver = data[8] | (data[9] << 8)
    keys = {}
    i = 10
    while i + 1 < len(data) and data[i] != 0:
        k, n = data[i], data[i + 1]
        keys[chr(k)] = list(data[i + 2:i + 2 + n])
        i += 2 + n
    say("FTPD.CFG: %d bytes, version %d, keys %s"
        % (len(data), ver, sorted(keys)))
    if keys.get("A") != [127, 0, 0, 1]:
        fails.append("FTPD.CFG's 'A' record is %r, wanted [127,0,0,1]"
                     % keys.get("A"))
    # ...and everything assertions 9 and 10 set. A record carries NO
    # terminator - its length byte is the bound - so an empty setting is an
    # ABSENT key rather than a one-byte one, which is why `R` is not here:
    # nothing typed a root.
    # WHAT THE SESSION ENDED ON, not what it passed through: assertion 12
    # leaves the root at /NOSUCH deliberately (a refused Start must still
    # save what the user typed rather than reverting it behind their back)
    # and turns whole-machine and Read Only back off.
    want = {"U": b"bob", "P": b"s3cret", "R": b"/NOSUCH"}
    for k, v in want.items():
        got = bytes(keys.get(k, []))
        if got != v:
            fails.append("FTPD.CFG's %r record is %r, wanted %r - the setting "
                         "worked all session and is gone on the next launch"
                         % (k, got, v))
    # ...and a FLAG that is off writes no record at all, which is the same
    # rule an empty string follows: the absent key IS the default.
    for k, what in (("M", "whole-machine"), ("O", "Read Only")):
        if k in keys:
            fails.append("FTPD.CFG carries a %r record (%r) with %s turned "
                         "OFF - an off flag must write NO record"
                         % (k, keys[k], what))
    say("FTPD.CFG persisted the root, the user and the password, and wrote "
        "no record for the two flags that ended off")


def extract(img, name11, path=()):
    """A file's bytes, by hand off the BPB - optionally down a folder PATH.

    Deliberately NOT os88disk.py's own extractor: this is the second opinion,
    so it reads the volume the way any FAT12 driver would and shares no code
    with the thing under test.

    **THE PATH ARGUMENT IS NOT A CONVENIENCE.** Without it this walked the
    ROOT only, and FTPD.CFG lives in SYSTEM/APPDATA (SPEC.md 19.9) - so the
    persistence check reported the file missing when it was there, which is a
    false failure that reads exactly like the real one it was written to
    catch.
    """
    b = open(img, "rb").read()
    bps = b[11] | (b[12] << 8)
    spc = b[13]
    rsvd = b[14] | (b[15] << 8)
    nfat = b[16]
    nroot = b[17] | (b[18] << 8)
    spf = b[22] | (b[23] << 8)
    root = (rsvd + nfat * spf) * bps
    data = root + nroot * 32
    fat = b[rsvd * bps: (rsvd + spf) * bps]

    def chain(clus, limit=None):
        out = b""
        while 2 <= clus < 0xFF0 and (limit is None or len(out) < limit):
            off = data + (clus - 2) * spc * bps
            out += b[off: off + spc * bps]
            j = clus + (clus >> 1)              # FAT12: 12 bits an entry
            v = fat[j] | (fat[j + 1] << 8)
            clus = (v >> 4) if (clus & 1) else (v & 0xFFF)
        return out

    # Walk down to the directory that holds the file. The root is a flat
    # region; a SUBdirectory is an ordinary cluster chain of the same records.
    ents = b[root: root + nroot * 32]
    for comp in path:
        found = None
        for i in range(0, len(ents), 32):
            e = ents[i:i + 32]
            if not e or e[0] == 0x00:
                break
            if e[0] == 0xE5 or (e[11] & 0x0F) == 0x0F:
                continue
            if e[:11].decode("latin-1") == comp and (e[11] & 0x10):
                found = e[26] | (e[27] << 8)
                break
        if found is None:
            return None
        ents = chain(found)
    for i in range(0, len(ents), 32):
        e = ents[i:i + 32]
        if len(e) < 32 or e[0] == 0x00:
            break
        if e[0] == 0xE5 or (e[11] & 0x0F) == 0x0F:
            continue
        if e[:11].decode("latin-1") != name11:
            continue
        size = int.from_bytes(e[28:32], "little")
        return chain(e[26] | (e[27] << 8), size)[:size]
    return None


if __name__ == "__main__":
    main()
