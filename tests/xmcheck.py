#!/usr/bin/env python3
"""The extended-memory TEARDOWN gate (SPEC.md 41.5, 29.4).

Drives `tests/xmtest` under QEMU and reads `xm_tab` before and after its
window is closed: were the blocks that instance held freed?

THE TABLE IS IN XMEM.DRV NOW (SPEC.md 41.12), not in the kernel, so finding it
takes two steps instead of one: the kernel's `xm_row` says which segment the
overlay was loaded into (DRVR_SEG, a heap claim, different every boot), and
the overlay's OWN nasm map says where `xm_tab` sits inside it. Neither can be
guessed and both are exact.

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
DRVR_SEG = 2                        # kernel/driver.inc, into xm_row
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
    """A KERNEL symbol's flat address, out of nasm's own map."""
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88sym.py"),
                        "--all"], capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0:
        raise SystemExit("xmcheck: os88sym.py failed:\n" + r.stderr[-600:])
    for line in r.stdout.splitlines():
        f = line.split()
        if len(f) >= 4 and f[0] == name:
            return int(f[3], 16)
    raise SystemExit(f"xmcheck: no kernel symbol {name!r}. kern_small has none "
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


def main():
    import time
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    sock = sys.argv[1]
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
