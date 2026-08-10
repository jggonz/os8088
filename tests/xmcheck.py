#!/usr/bin/env python3
"""The extended-memory TEARDOWN gate (SPEC.md 41.5, 29.4).

Drives `tests/xmtest` under QEMU and reads the kernel's own `xm_tab` before
and after its window is closed: were the blocks that instance held freed?

    make test TESTAPPS=build/xmtest.img
    python3 tests/xmcheck.py build/qmp.sock

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

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

XM_BLKSZ, XM_MAX_BLKS = 8, 8        # kernel/kernel.asm
XB_OFF, XB_KB, XB_OWN = 0, 2, 4
XM_OWN_KERN = 0xFF

# Where things are on a 640x480 desktop with build/xmtest.img in drive B.
# The Disk B drive zone, the package's row in the window it opens, and the
# close box of the window the package creates at (180,120) 240x90.
DISKB = (610, 110)
ROW = (180, 128)
CLOSE = (194, 125)


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
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88sym.py"),
                        "--all"], capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0:
        raise SystemExit("xmcheck: os88sym.py failed:\n" + r.stderr[-600:])
    for line in r.stdout.splitlines():
        f = line.split()
        if len(f) >= 4 and f[0] == name:
            return int(f[3], 16)
    raise SystemExit(f"xmcheck: no kernel symbol {name!r}. kern_small has no "
                     "xm_tab at all (SPEC.md 41.11) - this gate is kern_big's.")


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


def main():
    import time
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    sock = sys.argv[1]
    base = sym("xm_tab")
    print(f"xmcheck: xm_tab at {base:#07x}")

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

    print("xmcheck: closing its window (the close box, not minimize)")
    click(sock, *CLOSE)
    time.sleep(5)
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
