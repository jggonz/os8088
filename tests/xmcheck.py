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

XM_BLKSZ, XM_MAX_BLKS = 8, 8        # XM_MAX_BLKS: kernel/kernel.asm.
                                    # XM_BLKSZ: drivers/xmem/xmem.asm, NOT
                                    # kernel.asm - it is the image's, and it
                                    # is the stride this file decodes xm_tab
                                    # with, so a wrong citation sends the next
                                    # reader to the wrong file to check it
DRVR_SEG = 2                        # kernel/driver.inc, into xm_row
XB_OFF, XB_KB, XB_OWN = 0, 2, 4
XM_OWN_KERN = 0xFF

SOCK = os.path.join(ROOT, "build", "qmp.sock")
PID = os.path.join(ROOT, "build", "qemu.pid")
XMIMG = "build/xmtest.img"          # `all` builds nothing under tests/, and
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
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "qmp.py"), sock,
                        *cmds], capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0:
        raise SystemExit("xmcheck: qmp failed:\n" + (r.stderr or r.stdout)[-600:])
    return r.stdout


def goto(sock, x, y):
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mouse.py"), sock,
                    "to", str(x), str(y)], capture_output=True, cwd=ROOT)


def click(sock, x, y):
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
    ship = os.path.join(ROOT, "build", "xmem.bin")
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
    row = sym("xm_row")
    seg = read_bytes(sock, row + DRVR_SEG, 2)
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
    """`make test TESTAPPS=build/xmtest.img`, and answer when QMP is up.

    A previous run's QEMU is killed first, for CLAUDE.md's reason: `make test`
    fails with `cannot create PID file` while the STALE instance keeps
    answering on build/qmp.sock, so every read below succeeds and describes a
    machine nobody asked for. tests/minesrc.py opens the same way.
    """
    import time
    if os.path.exists(PID):
        try:
            os.kill(int(open(PID).read().strip()), 15)
            time.sleep(1.0)
        except (OSError, ValueError):
            pass
    for f in (SOCK, PID):
        if os.path.exists(f):
            os.remove(f)
    # `make test` DAEMONISES the emulator, so it outlives this script
    # unless somebody kills it - and the somebody is us (os88qemu).
    os88qemu.own()
    r = subprocess.run(["make", "test", "TESTAPPS=" + XMIMG],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        raise SystemExit("xmcheck: make test failed:\n" + r.stdout + r.stderr)
    for _ in range(150):                    # ...and wait for the socket
        if os.path.exists(SOCK):
            break
        time.sleep(0.2)
    else:
        raise SystemExit("xmcheck: QEMU never opened " + SOCK)
    time.sleep(12)                          # ...and for the desktop
    return SOCK


def main():
    import time
    mine = len(sys.argv) < 2
    sock = boot() if mine else sys.argv[1]
    try:
        return check(sock)
    finally:
        if mine:
            qmp(sock, "quit")


def check(sock):
    import time
    base = table_base(sock)

    print("xmcheck: opening Disk B and launching XMTEST.O88")
    dblclick(sock, *DISKB)
    time.sleep(7)
    dblclick(sock, *ROW)
    time.sleep(9)

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
    click(sock, cx, cy)
    time.sleep(5)
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
