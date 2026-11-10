#!/usr/bin/env python3
"""The call spelling and the surviving-far-call gate (SPEC.md 20.3.3).

    python3 tests/unit/t_cellgate.py

`apps/os88api.inc` overloads `call` once, so the SDK - not ~8,400 call sites -
decides what a call to a cell assembles to, and T5 flips every RARE cell onto
the software-interrupt door by editing that one place. Two things have to be
true for that to be safe, and neither is visible in a `cmp` of the images
(which proves only that today's arm is the far call it replaced):

  1. THE MACRO ROUTES. A rare cell reaches OS88_RARE_CALL and nothing else
     does - so when the arm changes, exactly the rare calls change. Checked
     by assembling a probe twice: once as shipped (every form must come out
     byte for byte as the bare instruction would), and once with the rare arm
     replaced by a marker, which must appear at the rare calls (plain and
     macro-wrapped) and at no other call. A rare name anywhere but a `call`
     must not assemble at all.
  2. THE GATE REFUSES. tools/os88pkg.py and tools/os88drv.py scan what they
     pack for `9A lo hi <KERNEL_SEG>`: a far call into the middle of a cell
     is refused always, and one to a rare cell once the door is switched on.
     Checked on synthetic images and then end to end: a real built package
     packs with the gate unarmed and is REFUSED with it armed early
     ($OS88_RARE_GATE=1), because every package far-calls rare cells today.
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
    check(len(rare) > 50, "the CELL KINDS blocks mark the rare cells",
          "a gate over an empty rare set routes nothing and refuses nothing",
          got=len(rare), want="> 50")
    hot = sorted(n for n, _, rel in cells
                 if n not in rare and rel == "apps/os88api.inc")
    r1 = sorted(n for n in rare if addr.get(n) is not None
                and n != "OSAPI_DRV_TASK")
    if not (hot and r1):
        done("t_cellgate")
        return
    h, r, seg = hot[0], r1[0], os88cells.sdk_switch()[0]

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
    # the same probe with no SDK: every name spelled as the far address it
    # is today and WRAP's call left to the bare instruction - what the
    # macro has to be indistinguishable from
    twin = probe.replace('%include "os88drv.inc"\n', "")
    for n in (r, h, "OSAPI_DRV_TASK"):
        twin = twin.replace(n, "0x%04X:0x%04X" % (seg, addr[n]))
    plain, err = nasm(twin)
    check(plain is not None, "the bare-instruction twin assembles",
          "it is the control the macro is compared against", got=err)
    got, err = nasm(probe)
    check(got is not None, "the probe assembles against the SDK under -w+error",
          "overloading `call` must not warn: every package is -w+error",
          got=err.strip()[-300:], want="no diagnostics")
    if got is not None and plain is not None:
        # `drivers/os88drv.inc` emits no bytes, so the probe is exactly its
        # calls
        check(got == plain,
              "as shipped, every call form assembles as the bare instruction",
              "the rare arm is today's far call: T6 changes no shipped byte, "
              "and a form the macro mangled would be a silent wrong call",
              got=got, want=plain)

    # ...now swap the rare arm for a marker: exactly the rare calls change.
    marked = probe.replace("%include \"os88drv.inc\"",
                           "%include \"os88drv.inc\"\n%unimacro OS88_RARE_CALL 1\n"
                           "%imacro OS88_RARE_CALL 1\n db 0xCC, 0x5A, 0xA5\n"
                           "%endmacro")
    got, err = nasm(marked)
    if check(got is not None, "the probe assembles with a marker rare arm",
             "T5 will replace this arm; the routing must not depend on what "
             "it emits", got=err.strip()[-300:]):
        n = got.count(RARE_MARK)
        check(n == 4, "the marker arm fires at the four rare calls and no other",
              "the plain, the macro-wrapped, the upper-case and the driver "
              "SDK's rare call - a rare call that missed the arm stays a far "
              "call after T5, and a hot call that took it goes to the door",
              got=n, want=4)
        check(got.count(far(addr[h])) == 2 and far(addr[r]) not in got,
              "the hot calls stay far calls and no rare far call survives",
              "the arm is the ONLY way a rare cell is reached",
              got=got.hex())

    # ...and a rare name that is not a call does not assemble at all.
    bad, err = nasm("cpu 8086\nbits 16\n%%include \"os88api.inc\"\n"
                    "    jmp %s\n" % r)
    check(bad is None, "a rare cell named by `jmp` does not assemble",
          "after T5 a rare name is no far address; a spelling that bypasses "
          "the macro has to fail at the assembler, not at the far end",
          got="assembled" if bad is not None else "refused", want="refused")

    # --- 2. the gate refuses -----------------------------------------------
    mid = addr[h] + 1
    blob = b"\x90" * 4 + far(addr[h]) + far(addr[r])
    p0 = os88cells.farcall_problems({"probe": blob}, armed=False)
    check(not p0, "far calls to published cells pass the unarmed gate",
          "the positive control: a gate that refuses everything passes the "
          "red arms below", got=p0)
    p1 = os88cells.farcall_problems({"probe": blob + far(mid)}, armed=False)
    check(len(p1) == 1 and "0x%04X" % mid in p1[0],
          "a far call into the middle of a cell is refused, armed or not",
          "it lands mid-cell on every kernel", got=p1)
    past = max(addr.values()) + 0x1000
    p3 = os88cells.farcall_problems({"probe": blob + far(past)}, armed=True)
    check(len(p3) == 1 and r in p3[0],
          "a far call PAST the table is not judged",
          "kern_dos, a part of DOS.O88, far-calls the kernel's disk layer at "
          "private offsets by design (t_kdfar polices those)", got=p3)
    p2 =os88cells.farcall_problems({"probe": blob}, armed=True)
    check(len(p2) == 1 and r in p2[0],
          "a far call to a rare cell is refused once the door is switched on",
          "after T5 a rare cell is never a far call; one that is was built by "
          "hand and lands on whatever the renumbered table put there", got=p2)

    # ...end to end, through the packer a package build actually runs.
    pkg = os.path.join(ROOT, "build", "mines.bin")
    if check(os.path.isfile(pkg), "build/mines.bin exists to pack",
             "the end-to-end arm packs a real package", got=pkg):
        with tempfile.TemporaryDirectory(prefix="cellgate") as tmp:
            out = os.path.join(tmp, "m.o88")
            env = dict(os.environ)
            env.pop("OS88_RARE_GATE", None)
            ok = subprocess.run([sys.executable, "tools/os88pkg.py", pkg, "-o",
                                 out], cwd=ROOT, env=env, capture_output=True,
                                text=True)
            check(ok.returncode == 0, "os88pkg packs mines with the gate unarmed",
                  "the shipped spelling far-calls rare cells until T5",
                  got=(ok.stdout + ok.stderr).strip()[-300:])
            env["OS88_RARE_GATE"] = "1"
            no = subprocess.run([sys.executable, "tools/os88pkg.py", pkg, "-o",
                                 out], cwd=ROOT, env=env, capture_output=True,
                                text=True)
            check(no.returncode != 0 and "RARE cell" in no.stdout + no.stderr,
                  "os88pkg REFUSES mines with the gate armed early",
                  "every package far-calls rare cells today, so an armed gate "
                  "that passes one is a gate that scans nothing",
                  got=(no.stdout + no.stderr).strip()[-300:])
    done("t_cellgate")


if __name__ == "__main__":
    main()
