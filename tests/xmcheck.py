#!/usr/bin/env python3
"""The extended-memory TEARDOWN gate (SPEC.md 41.5, 29.4).

Drives `tests/xmtest` under QEMU and reads `xm_tab` before and after its
window is closed: were the blocks that instance held freed?

THE TABLE IS IN XMEM.DRV NOW (SPEC.md 41.12), not in the kernel, so finding it
takes two steps instead of one: the kernel's `xm_row` says which segment the
overlay was loaded into (DRVR_SEG, a heap claim, different every boot), and
the overlay's OWN nasm map says where `xm_tab` sits inside it. Neither can be
guessed and both are exact.

    python3 tests/xmcheck.py                 # boots its own machine
    python3 tests/xmcheck.py build/qmp.sock  # ...or drives one already up

IT BOOTS ITS OWN MACHINE NOW, which is what the suite row needs and what it
did not have: registered as `py("tests/xmcheck.py")` with no argument, every
run ended in `SystemExit(__doc__)` at the argument check - exit 1, in a
hundredth of a second, printing this docstring as though it were a failure
report. A gate that cannot be run by the runner that registers it is an
ABSENT gate reading as a failing one (tools/os88fixture.py makes the same
point about a missing fixture). The socket argument still works, because the
two-step form is what you want when poking at a machine by hand.

Exit 0 = the release works. Exit 1 = blocks outlived their instance, which is
the state the #51 integration merge shipped for a year after it dropped
xm_release_rec's three call sites and left the comment describing them.

THE ASSERTION IS OUTSIDE THE PACKAGE, on purpose. A package asking the kernel
"did you free my blocks?" is the writer and the reader being the same code,
and both agreeing on the same wrong answer is the failure this area has
already had once (docs/FIELD-NOTES.md 4).

WHY QEMU: the machine must HAVE extended memory and the target machine never
can - an 8088 has no A20 line and nothing above linear 0x0FFFFF (SPEC.md 41.9
rule 1). This is one of QEMU's short list of legitimate uses (docs/TESTING.md).

IT IS VERIFIED TO FAIL, which is the only thing that makes a green run mean
anything: with the three `call xm_release_rec` commented out of instance.inc,
the same run leaves all three blocks live at KB=4 owner=1 after the close.
"""
import os
import subprocess
import sys
import os88qemu                                              # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# os88qemu is tests/'s and needs no path work; os88fixture is tools/'s, so the
# insert comes first. (`_sym` below does its own insert for the same reason -
# this one is here because the import is at module scope.)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88fixture                                           # noqa: E402
import os88build

XM_BLKSZ, XM_MAX_BLKS = 8, 8        # XM_MAX_BLKS: kernel/kernel.asm.
                                    # XM_BLKSZ: drivers/xmem/xmem.asm, NOT
                                    # kernel.asm - it is the image's, and it
                                    # is the stride this file decodes xm_tab
                                    # with, so a wrong citation sends the next
                                    # reader to the wrong file to check it
XB_OFF, XB_KB, XB_OWN = 0, 2, 4
XM_OWN_KERN = 0xFF

SOCK = os.path.join(ROOT, "build", "qmp.sock")
PID = os.path.join(ROOT, "build", "qemu.pid")
XMIMG = os88build.at("build/xmtest.img")          # `all` builds nothing under tests/, and
                                    # `make test` names TESTAPPS as one of its
                                    # own prerequisites, so asking for it here
                                    # builds it (tools/os88fixture.py's rule,
                                    # arrived at by the Makefile rather than
                                    # by a second copy of the ladder)

# Where things are on a 640x480 desktop with build/xmtest.img in drive B:
# the Disk B drive zone, and the package's row in the window it opens.
#
# THE CLOSE BOX IS NOT HERE, and used to be: `CLOSE = (194, 125)` was computed
# for a window assumed to sit at (180,120), the window manager put it at 175,
# and 194 is one column PAST the close box of a window at 175. wm_hit read
# that as the title bar, the click started a drag, the window never closed -
# and this gate then reported three blocks "outliving their instance" for an
# instance that was still running. It is read off the live record now, through
# tools/os88geom.py, which mirrors the offsets and is guarded against the
# kernel moving them.
DISKB = (610, 110)
ROW = (180, 128)


def qmp(sock, *cmds):
    """tools/qmp.py's work, in process (a Python start a call was most of a
    memory read's cost); a failure is the same SystemExit as before. A plain
    `sendkey` is CONFIRMED there (tools/qmp.py `key`, SOAK-PARALLEL.md 17.5).
    """
    import traceback
    t = os.path.join(ROOT, "tools")
    if t not in sys.path:
        sys.path.insert(0, t)
    import qmp as qmpc
    try:
        c = _KEEP.get(sock)
        if c is None and sock in _WANT:
            try:
                c = _KEEP[sock] = qmpc.Client(sock)
            except OSError:
                c = None
        if c is not None:
            try:
                out, err, rc = c.run(list(cmds))
            except (OSError, RuntimeError, ValueError):
                release(sock)               # (a dead connection: once more,
                c = None                    # on a fresh one)
        if c is None:
            out, err, rc = qmpc.run(sock, list(cmds))
    except Exception:                       # noqa: BLE001 - as a crashed child
        out, err, rc = "", traceback.format_exc(), 1
    if rc != 0:
        raise SystemExit("xmcheck: qmp failed:\n" + (err or out)[-600:])
    return out + "\n" if out else ""


def qmp_exec(sock, execute, arguments=None):
    """ONE raw QMP command - for the few that are not HMP's, such as
    `input-send-event`, which can send a key's press and its release apart
    where HMP's `sendkey` sends both. On the kept connection when there is
    one; a refusal is the same SystemExit as `qmp`'s."""
    t = os.path.join(ROOT, "tools")
    if t not in sys.path:
        sys.path.insert(0, t)
    import qmp as qmpc
    obj = {"execute": execute}
    if arguments is not None:
        obj["arguments"] = arguments
    c = _KEEP.get(sock)
    if c is None and sock in _WANT:
        c = _KEEP[sock] = qmpc.Client(sock)
    if c is not None:
        r = c.send(obj)
    else:
        c = qmpc.Client(sock)
        try:
            r = c.send(obj)
        finally:
            c.close()
    if "error" in r:
        raise SystemExit("xmcheck: qmp %s failed: %r" % (execute, r["error"]))
    return r.get("return")


_KEEP = {}
_WANT = set()


def keep(sock, on=True):
    """Hold ONE QMP connection to `sock` for every later `qmp` call in this
    process (tools/qmp.py's Client): a call is then one round trip where a
    fresh connection is three, ~35 ms a memory read on a loaded box. QMP
    serves ONE client, so this file's mouse.py helpers `release` it before
    they spawn (the next `qmp` call opens it again); anything else that
    connects - a tools/shot.py child - must `release` first too, or it waits
    for ever. `keep(sock, False)` ends it (vidxms.Q.close does)."""
    if on:
        _WANT.add(sock)
    else:
        _WANT.discard(sock)
        release(sock)


def release(sock):
    """Close the kept connection, if any; `qmp` reopens it while kept."""
    c = _KEEP.pop(sock, None)
    if c is not None:
        try:
            c.close()
        except OSError:
            pass


def goto(sock, x, y):
    """CLOSED LOOP when the kernel's pointer words can be read
    (os88qemu.mouse_to: send the delta, wait for `mouse_x` to MOVE, repeat
    until it is there), which is what a click needs to land on a row it
    named. tools/mouse.py, the fallback, DEAD-RECKONS - it pins against the
    clamp and walks back on host sleeps - and a guest starved past those
    sleeps took a double-click somewhere else, after which the row timed out
    waiting for a listing that was never asked for (rdxms, httpmountname,
    docs/plans/SOAK-PARALLEL.md 17.5)."""
    try:
        import os88qemu
        os88qemu.mouse_to(os88qemu._Sock(sock), sym,
                          lambda *c: qmp(sock, *c), x, y)
        return
    except BaseException:               # noqa: BLE001 - no map, a kernel
        pass                            # without the words: dead-reckon
    release(sock)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mouse.py"), sock,
                    "to", str(x), str(y)], capture_output=True, cwd=ROOT)


def click(sock, x, y):
    release(sock)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mouse.py"), sock,
                    "click", str(x), str(y)], capture_output=True, cwd=ROOT)


def dblclick(sock, x, y):
    """Two presses over ONE connection: the 9-tick window (SPEC.md 22) is far
    shorter than two mouse.py invocations."""
    goto(sock, x, y)
    qmp(sock, "mouse_button 1", "sleep 0.08", "mouse_button 0",
        "sleep 0.12", "mouse_button 1", "sleep 0.08", "mouse_button 0")


def sym(name):
    """A KERNEL symbol's flat address, out of nasm's own map.

    Through the LIBRARY and not through `os88sym.py --all`, which is what this
    used to shell out to: that re-assembles the whole kernel to answer about
    one symbol, and it prints a dash rather than an address for the on-demand
    modules (SPEC.md 2.8) - which a positional `int(f[3], 16)` would have
    parsed as an error the day one of those names was asked for.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88sym
    try:
        return os88sym.linear(name)
    except KeyError:
        raise SystemExit(
            f"xmcheck: no kernel symbol {name!r}. kern_small has none "
            "of this at all (SPEC.md 41.11) - this gate is kern_big's.")


def ovl_sym(name):
    """A symbol's offset inside XMEM.DRV, out of the overlay's own nasm map.

    os88sym.py's discipline, on the other image: assemble a COPY with
    `[map all]` appended and require the result to be byte-identical to the
    build/xmem.bin that shipped, so a map of a different overlay is an error
    rather than a wrong answer.
    """
    import tempfile
    src = os.path.join(ROOT, "drivers", "xmem", "xmem.asm")
    ship = os.path.join(ROOT, os88build.at("build/xmem.bin"))   # the run's
                                    # tree under a frozen soak, not build/
                                    # (docs/WRITING-TESTS.md 5.4)
    if not os.path.exists(ship):
        raise SystemExit("xmcheck: no build/xmem.bin - `make` first.")
    with tempfile.TemporaryDirectory() as td:
        cp, out = os.path.join(td, "x.asm"), os.path.join(td, "x.bin")
        mp = os.path.join(td, "x.map")
        with open(cp, "w") as f:
            f.write(open(src).read() + "\n[map all %s]\n" % mp)
        r = subprocess.run(["nasm", "-f", "bin", "-w+error", "-I",
                            os.path.join(ROOT, "drivers") + os.sep, "-I",
                            os.path.join(ROOT, "apps") + os.sep, "-o", out, cp],
                           capture_output=True, text=True, cwd=ROOT)
        if r.returncode != 0:
            raise SystemExit("xmcheck: could not assemble the overlay:\n"
                             + r.stderr[-600:])
        if open(out, "rb").read() != open(ship, "rb").read():
            raise SystemExit("xmcheck: the overlay this map describes is not "
                             "the one that shipped - rebuild and re-run.")
        for line in open(mp):
            f = line.split()
            if len(f) >= 3 and f[2] == name:
                return int(f[0], 16)
    raise SystemExit(f"xmcheck: no symbol {name!r} in XMEM.DRV.")


def newest_window(sock):
    """(slot, x, y, w, h) of the frontmost used+visible window."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88geom
    raw = read_bytes(sock, sym("wm_wins"),
                     os88geom.MAX_WIN * os88geom.WIN_SIZE)

    def u16(o):
        return raw[o] | (raw[o + 1] << 8)
    out = None
    for i in range(os88geom.MAX_WIN):
        b = i * os88geom.WIN_SIZE
        if u16(b + os88geom.W_FLAGS) & 3 == 3:
            out = (i, u16(b + os88geom.W_X), u16(b + os88geom.W_Y),
                   u16(b + os88geom.W_W), u16(b + os88geom.W_H))
    return out


def table_base(sock):
    """Where xm_tab actually is this boot: the overlay's segment x 16, plus
    its offset inside the image."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88geom
    row = sym("xm_row")
    seg = read_bytes(sock, row + os88geom.DRVR_SEG, 2)
    seg = seg[0] | (seg[1] << 8)
    if not seg:
        raise SystemExit(
            "xmcheck: XMEM.DRV is not loaded, so there is no table to read.\n"
            "         Either this machine has no memory above 1MB (an 8088\n"
            "         never does - SPEC.md 41.12.1's sniff declines and not a\n"
            "         sector is read), or the overlay refused at attach and\n"
            "         the kernel freed it. Use QEMU on a 386 (SPEC.md 41.7).")
    off = ovl_sym("xm_tab")
    print(f"xmcheck: XMEM.DRV at {seg:#06x}, xm_tab +{off:#05x}")
    return (seg << 4) + off


def read_bytes(sock, addr, n):
    data = bytearray()
    for line in qmp(sock, f"xp /{n}xb 0x{addr:X}").splitlines():
        if ":" in line:
            for tok in line.split(":", 1)[1].split():
                if tok.startswith("0x"):
                    data.append(int(tok, 16) & 0xFF)
    return data


def blocks(sock, base):
    """Every LIVE entry as (index, kb-offset, kb, owner). XB_KB = 0 is free."""
    out, data = [], bytearray()
    for line in qmp(sock, f"xp /{XM_MAX_BLKS * XM_BLKSZ}xb 0x{base:X}").splitlines():
        if ":" in line:
            for tok in line.split(":", 1)[1].split():
                if tok.startswith("0x"):
                    data.append(int(tok, 16) & 0xFF)
    for i in range(XM_MAX_BLKS):
        e = data[i * XM_BLKSZ:(i + 1) * XM_BLKSZ]
        if len(e) < XM_BLKSZ:
            break
        kb = e[XB_KB] | (e[XB_KB + 1] << 8)
        if kb:
            out.append((i, e[XB_OFF] | (e[XB_OFF + 1] << 8), kb, e[XB_OWN]))
    return out


def show(label, live):
    print(f"  {label}: {len(live)} live block(s)")
    for i, off, kb, own in live:
        who = "KERNEL" if own == XM_OWN_KERN else f"instance {own}"
        print(f"      [{i}] +{off}KB {kb}KB owner {own:#04x} ({who})")


def boot():
    """`make test TESTAPPS=build/xmtest.img`'s machine WITHOUT make
    (os88qemu.launch_test): its own socket and copies of both floppies, so
    no `make` runs in the shared build/ (a build/ behind its sources is
    REBUILT by `make test`'s prerequisites) and no stale instance on
    build/qmp.sock can answer for it. Answers the socket at a desktop."""
    import atexit
    import shutil
    import tempfile
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    atexit.register(shutil.rmtree, tmp, True)   # (after the kill: LIFO)
    sock, _ = os88qemu.launch_test(tmp, apps=XMIMG)
    wait_desktop(sock)                      # ...and for the desktop
    return sock


def wait_desktop(sock, who="xmcheck"):
    """Drive B's zone is what the first click aims at, so its existence is
    the condition - in GUEST seconds (tests/os88qemu.py), where this was a
    host `time.sleep(12)` - and then a second of the machine for its paint."""
    import dispcp
    m = Mem(sock)

    def zone():
        # ...AND ui_task ASLEEP WITH NOTHING QUEUED. The zone alone can be
        # read out of RAM the boot has not written yet - drv_boot_x runs, and
        # fills a driver's drv_tab row, before desk_rowcalc lays the desktop
        # out, and a first look in that window has read desk_rows as 33031 -
        # while ui_idle cannot hold before kmain has become ui_task.
        try:
            return (dispcp.drive_ordinal(m, sym, "B") is not None
                    and os88qemu.ui_idle(m, sym))
        except Exception:                   # noqa: BLE001 - not answering yet
            return False
    if not os88qemu.acted(m, zone, secs=90, what="drive B's zone", poll=0.05):
        raise SystemExit("%s: drive B: never got a desktop zone - the guest "
                         "did not reach a desktop" % who)
    os88qemu.ui_done(m, sym, cap=1.0)       # (was pace(1): the paint, capped)


class Mem(object):
    """`read(linear, n)` over this file's `xp` reader - the one shape
    tests/os88qemu.py's guest-clock waits take."""

    def __init__(self, sock):
        self.sock = sock

    def read(self, linear, n):
        return bytes(read_bytes(self.sock, linear, n))


def main():
    mine = len(sys.argv) < 2
    sock = boot() if mine else sys.argv[1]
    try:
        return check(sock)
    finally:
        if mine:
            qmp(sock, "quit")


def check(sock):
    m = Mem(sock)
    base = table_base(sock)

    # Every wait below is on the GUEST's clock (tests/os88qemu.py) and on the
    # thing the next line reads: a window, the package's blocks, the close.
    print("xmcheck: opening Disk B and launching XMTEST.O88")
    # closed-loop on mouse_x/mouse_btn, every edge confirmed (SOAK-SPEED 15),
    # where dblclick walks tools/mouse.py blind
    hmp = lambda *c: qmp(sock, *c)                          # noqa: E731
    os88qemu.mouse_dblclick(m, sym, hmp, *DISKB)
    os88qemu.acted(m, lambda: newest_window(sock) is not None, secs=20,
                   what="the Disk B window", poll=0.05)
    os88qemu.ui_done(m, sym, cap=1.0)       # ...and its listing painted
    os88qemu.mouse_dblclick(m, sym, hmp, *ROW)

    def owned_now():
        return [b for b in blocks(sock, base) if b[3] != XM_OWN_KERN]
    if os88qemu.acted(m, lambda: bool(owned_now()), secs=30,
                      what="an instance-owned block", poll=0.05):
        os88qemu.quiesce(m, lambda: blocks(sock, base), secs=1.0,
                         what="xm_tab")

    before = blocks(sock, base)
    show("window open", before)
    owned = [b for b in before if b[3] != XM_OWN_KERN]
    if not owned:
        raise SystemExit(
            "xmcheck: no instance-owned blocks, so there is nothing to test.\n"
            "         Either xmtest did not launch, or this machine has no\n"
            "         store above 1MB. Use QEMU on a 386 (SPEC.md 41.7).")

    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88geom
    win = newest_window(sock)
    if win is None:
        raise SystemExit("xmcheck: no window is open, so xmtest did not run.")
    slot, wx, wy, ww, wh = win
    cx, cy = os88geom.close_xy(wx, wy)
    print(f"xmcheck: closing window {slot} at ({wx},{wy}) {ww}x{wh} - "
          f"its close box, at ({cx},{cy}), not minimize")
    os88qemu.mouse_click(m, sym, hmp, cx, cy)

    def closed():
        w = newest_window(sock)
        return w is None or w[0] != slot
    if os88qemu.acted(m, closed, secs=15, what="window %d closing" % slot,
                      poll=0.05):
        os88qemu.quiesce(m, lambda: blocks(sock, base), secs=1.0,
                         what="xm_tab")
    # ...and PROVE it closed. Without this the leak report below cannot tell
    # "the blocks were not freed" from "the window is still open", which is
    # the state this gate spent a year in.
    still = newest_window(sock)
    if still is not None and still[0] == slot:
        raise SystemExit(
            f"xmcheck: window {slot} is STILL OPEN after clicking "
            f"({cx},{cy}) - the click missed its close box, so nothing below "
            "would have been a memory finding. Check WM_BOX_* in "
            "tools/os88geom.py against kernel/wm.inc's wm_hit.")
    after = blocks(sock, base)
    show("after close", after)

    keys = {(i, off, kb, own) for i, off, kb, own in owned}
    leaked = [b for b in after if b in keys]
    print()
    if leaked:
        print(f"xmcheck: FAIL - {len(leaked)} block(s) outlived their instance.")
        print("         xm_release_rec is not reached from SPEC.md 29.4's")
        print("         teardown sites in kernel/instance.inc.")
        return 1
    print(f"xmcheck: PASS - all {len(owned)} instance-owned block(s) freed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
