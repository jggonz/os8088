#!/usr/bin/env python3
"""The call spelling and the surviving-far-call gate (SPEC.md 20.3.3, 20.3.4).

    python3 tests/unit/t_cellgate.py

`apps/os88api.inc` overloads `call` once, so the SDK - not ~8,400 call sites -
decides what a call to a cell assembles to, and kernel size pass 19's T5
flipped every RARE cell onto the software-interrupt door by editing that one
place: `call OSAPI_X` for a door cell is `int OS88_RARE / db <index>`. Two
things have to stay true:

  1. THE MACRO ROUTES. A door cell reaches OS88_RARE_CALL and nothing else
     does. Checked by assembling a probe twice: once as shipped (every form
     must come out byte for byte as a hand-written twin - the door sequence
     for a door cell, the bare instruction for everything else), and once
     with the rare arm replaced by a marker, which must appear at the door
     calls (plain and macro-wrapped) and at no other call. A door name
     anywhere but a `call` must not assemble at all.
  2. THE GATE REFUSES. tools/os88pkg.py and tools/os88drv.py scan what they
     pack for `9A lo hi <KERNEL_SEG>`: a far call into the middle of a cell
     is refused - after the renumber, that is what a far call built against
     an older table lands on. Checked on synthetic images and end to end: a
     real built package packs, and the same image with one mid-cell far call
     spliced in is REFUSED.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                           # noqa: E402
import os88cells                                          # noqa: E402

RARE_MARK = b"\xCC\x5A\xA5"      # int3 + a pattern no far call contains


def nasm(src, defines=()):
    """(bytes or None, stderr) - assembled with the SDK on the include path."""
    with tempfile.TemporaryDirectory(prefix="cellgate") as tmp:
        asm, out = os.path.join(tmp, "p.asm"), os.path.join(tmp, "p.bin")
        open(asm, "w").write(src)
        r = subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                            "-I", "drivers/"] + ["-D" + d for d in defines]
                           + ["-o", out, asm], cwd=ROOT, capture_output=True,
                           text=True)
        return (open(out, "rb").read() if r.returncode == 0 else None), r.stderr


def main():
    cells = os88cells.sdk_cells()
    addr = {n: a for n, a, _ in cells}
    rare = {}
    for rel in os88cells.SDK_FILES:
        rare.update(os88cells.block_rare(rel) or {})
    check(len(rare) > 50, "the CELL KINDS blocks mark the door cells",
          "a gate over an empty door set routes nothing", got=len(rare), want="> 50")
    hot = sorted(n for n, _, rel in cells if rel == "apps/os88api.inc")
    r1 = sorted(n for n in rare if n != "OSAPI_DRV_TASK")
    if not (hot and r1):
        done("t_cellgate")
        return
    h, r, seg = ("OSAPI_GFX_FILL" if "OSAPI_GFX_FILL" in hot else hot[0]), r1[0], os88cells.sdk_switch()[0]

    # --- 1. the macro routes ----------------------------------------------
    probe = """cpu 8086
bits 16
%%include "os88drv.inc"
%%macro WRAP 2
    call %%2
%%endmacro
top:
    call %(r)s
    call %(h)s
    call top
    call near top
    call [bx]
    call word [cs:bx+2]
    call far [bx]
    WRAP x, %(r)s
    WRAP x, %(h)s
    CALL %(r)s
    call OSAPI_DRV_TASK
""" % {"r": r, "h": h}
    far = lambda o: b"\x9a" + struct.pack("<HH", o, seg)
    door = lambda n: "db 0xCD, 0x81, 0x%02X" % rare[n][0]
    # the same probe with no SDK: a hot name spelled as its far address, a
    # door name as the door's three bytes, WRAP's call left to the bare
    # instruction - what the macro has to be indistinguishable from
    twin = probe.replace('%include "os88drv.inc"\n', "")
    twin = twin.replace("    call %s\n" % r, "    %s\n" % door(r))
    twin = twin.replace("    CALL %s\n" % r, "    %s\n" % door(r))
    twin = twin.replace("    call OSAPI_DRV_TASK\n", "    %s\n" % door("OSAPI_DRV_TASK"))
    twin = twin.replace("    WRAP x, %s\n" % r, "    %s\n" % door(r))
    twin = twin.replace(h, "0x%04X:0x%04X" % (seg, addr[h]))
    plain, err = nasm(twin)
    check(plain is not None, "the hand-written twin assembles",
          "it is the control the macro is compared against", got=err)
    got, err = nasm(probe)
    check(got is not None, "the probe assembles against the SDK under -w+error",
          "overloading `call` must not warn: every package is -w+error",
          got=err.strip()[-300:], want="no diagnostics")
    if got is not None and plain is not None:
        check(got == plain,
              "as shipped, a door call is `int 81h / db <index>` and every other "
              "call form the bare instruction",
              "a form the macro mangled would be a silent wrong call",
              got=got.hex(), want=plain.hex())

    # ...now swap the rare arm for a marker: exactly the door calls change.
    marked = probe.replace("%include \"os88drv.inc\"",
                           "%include \"os88drv.inc\"\n%unimacro OS88_RARE_CALL 1\n"
                           "%imacro OS88_RARE_CALL 1\n db 0xCC, 0x5A, 0xA5\n"
                           "%endmacro")
    got, err = nasm(marked)
    if check(got is not None, "the probe assembles with a marker rare arm",
             "the routing must not depend on what the arm emits",
             got=err.strip()[-300:]):
        n = got.count(RARE_MARK)
        check(n == 4, "the marker arm fires at the four door calls and no other",
              "the plain, the macro-wrapped, the upper-case and the driver "
              "SDK's door call", got=n, want=4)
        check(got.count(far(addr[h])) == 2 and b"\xcd\x81" not in got,
              "the hot calls stay far calls and no door site survives the marker",
              "the arm is the ONLY way a door cell is reached", got=got.hex())

    # ...and a door name that is not a call does not assemble at all.
    bad, err = nasm("cpu 8086\nbits 16\n%%include \"os88api.inc\"\n"
                    "    jmp %s\n" % r)
    check(bad is None, "a door cell named by `jmp` does not assemble",
          "a door name is no far address; a spelling that bypasses the macro "
          "has to fail at the assembler, not at the far end",
          got="assembled" if bad is not None else "refused", want="refused")

    # --- 2. the gate refuses -----------------------------------------------
    mid = addr[h] + 1
    blob = b"\x90" * 4 + far(addr[h])
    p0 = os88cells.farcall_problems({"probe": blob})
    check(not p0, "far calls to published cells pass the gate",
          "the positive control: a gate that refuses everything passes the "
          "red arms below", got=p0)
    p1 = os88cells.farcall_problems({"probe": blob + far(mid)})
    check(len(p1) == 1 and "0x%04X" % mid in p1[0],
          "a far call into the middle of a cell is refused",
          "it lands mid-cell on every kernel - what a far call built against "
          "the old table would do", got=p1)
    past = max(addr.values()) + 0x1000
    p3 = os88cells.farcall_problems({"probe": blob + far(past)})
    check(not p3, "a far call PAST the table is not judged",
          "kern_dos, a part of DOS.O88, far-calls the kernel's disk layer at "
          "private offsets by design (t_kdfar polices those)", got=p3)

    # ...end to end, through the packer a package build actually runs.
    pkg = os.path.join(ROOT, "build", "mines.bin")
    if check(os.path.isfile(pkg), "build/mines.bin exists to pack",
             "the end-to-end arm packs a real package", got=pkg):
        with tempfile.TemporaryDirectory(prefix="cellgate") as tmp:
            out = os.path.join(tmp, "m.o88")
            ok = subprocess.run([sys.executable, "tools/os88pkg.py", pkg, "-o",
                                 out], cwd=ROOT, capture_output=True, text=True)
            check(ok.returncode == 0, "os88pkg packs mines as built",
                  got=(ok.stdout + ok.stderr).strip()[-300:])
            img = bytearray(open(pkg, "rb").read())
            at = img.find(far(addr[h]))
            if check(at > 0, "mines far-calls %s, to splice over" % h, got=at):
                img[at:at + 5] = far(mid)
                bad = os.path.join(tmp, "bad.bin")
                open(bad, "wb").write(img)
                no = subprocess.run([sys.executable, "tools/os88pkg.py", bad,
                                     "-o", out], cwd=ROOT, capture_output=True,
                                    text=True)
                check(no.returncode != 0 and "middle of a" in no.stdout + no.stderr,
                      "os88pkg REFUSES mines with one mid-cell far call",
                      "a gate that passes it scans nothing",
                      got=(no.stdout + no.stderr).strip()[-300:])
    done("t_cellgate")


if __name__ == "__main__":
    main()
