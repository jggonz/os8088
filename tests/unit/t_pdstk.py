#!/usr/bin/env python3
"""PageDesk's stack gate (DECISIONS D21/D23, tools/os88pdstk.py).

    python3 tests/unit/t_pdstk.py

THE REAL TREE. Every PageDesk segment is read from the source it is
assembled from and the C as `make pagedesk` left it (build/*.gen.asm). Three
things must hold. No recursion outside the bounded ratchet. Every module
verb's deepest chain on D23's private stack is <= 1776 from the door. Every
primary callback's chain on the UI task's 512-byte stack is <= 384. That
last one counts the kernel's dispatch, the primary's frames, and the kernel
call underneath: the primary's own, or a module's swapped back at
pdu_mcall's depth. A segment whose generated C is not built is SKIPPED by
name, never passed: PageDesk is not in `all` (a C package needs SmallerC,
SPEC.md 73.1).

THE NEGATIVE CONTROLS run whether or not PageDesk is built. A synthetic
tree with the real segments' shape - a primary whose pdu_mcall far-calls a
door, a door whose dispatcher calls a verb - fails in each of three ways
the gate exists for:
  * a verb that recurses (D21: bez() did, and wrote over the menu bar)
  * a verb deeper than the private stack (DEEP)
  * a callback whose own frames plus a kernel call pass 384 (UI)
and passes when none of them is present. A gate that cannot fail is not a
gate. Each control must fail and name the function that caused it.
~1 s.
"""
import io
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88pdstk as P                                               # noqa: E402

SEGS = [
    ("PRIMARY", "prim.asm", None, None),
    ("PDREND", "rend.asm", "pdr_entry", "_pdr_verb"),
]

PRIM = """
section .text
cc_onwake:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push si
    call _os88_onwake
    add sp, 2
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
_os88_onwake:
    push bp
    mov bp, sp
    sub sp, %(cbframe)d
    call _pdu_mcall
    call _os88_thing
    mov sp, bp
    pop bp
    ret
_os88_thing:
    call OSAPI_FILE_READ_SEQ
    ret
_pdu_mcall:
    push bp
    mov bp, sp
    push si
    push di
    push es
    call far [si]
    pop es
    pop di
    pop si
    pop bp
    ret
"""

REND = """
section .text
pdr_entry:
    push bp
    push si
    push di
    push es
    push ds
    push dx
    push cx
    push bx
    push ax
    call _pdr_verb
    add sp, 8
    pop ds
    pop es
    pop di
    pop si
    pop bp
    retf
_pdr_verb:
    push bp
    mov bp, sp
    call _verb_step
    call _verb_init
    pop bp
    ret
_verb_init:
    ret
_verb_step:
    push bp
    mov bp, sp
    sub sp, %(stepframe)d
    call _leaf
    mov sp, bp
    pop bp
    ret
_leaf:
    push bp
    mov bp, sp
%(leafbody)s
    call OSAPI_GFX_BLIT4
    pop bp
    ret
"""


def run(cbframe=20, stepframe=40, leafbody=""):
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "prim.asm"), "w") as f:
            f.write(PRIM % {"cbframe": cbframe})
        with open(os.path.join(tmp, "rend.asm"), "w") as f:
            f.write(REND % {"stepframe": stepframe, "leafbody": leafbody})
        buf = io.StringIO()
        fails, g = P.analyse(tmp, out=lambda s: buf.write(s + "\n"), segments=SEGS, root=tmp)
        return fails, g, buf.getvalue()


# 20.22.2.4: an OCCUPANT of the slot, entered by PDREND's "slot" far call,
# and calling PDENG alone - here a door, a dispatcher and one verb whose frame
# the control sets, and its far call into a PDENG that is only a door
REND_SLOT = """
section .text
pdr_entry:
    push bp
    call _pdr_verb
    pop bp
    retf
_pdr_verb:
    push bp
    mov bp, sp
    call far [_pdr_slot_off]
    pop bp
    ret
"""

OCC = """
section .text
pdxg_entry:
    push bp
    call _pdxg_verb
    pop bp
    retf
_pdxg_verb:
    push bp
    mov bp, sp
    sub sp, %(occframe)d
    call far [_pdxg_eng]
    mov sp, bp
    pop bp
    ret
"""

ENG = """
section .text
pde_entry:
    push bp
    call _pde_verb
    pop bp
    retf
_pde_verb:
    ret
"""


def run_occ(occframe):
    segs = [("PDENG", "eng.asm", "pde_entry", "_pde_verb"),
            ("PDREND", "rend.asm", "pdr_entry", "_pdr_verb"),
            ("PDXG", "pdxg.asm", "pdxg_entry", "_pdxg_verb")]
    with tempfile.TemporaryDirectory() as tmp:
        for name, body in (("eng.asm", ENG), ("rend.asm", REND_SLOT),
                           ("pdxg.asm", OCC % {"occframe": occframe})):
            with open(os.path.join(tmp, name), "w") as f:
                f.write(body)
        buf = io.StringIO()
        fails, g = P.analyse(tmp, out=lambda s: buf.write(s + "\n"), segments=segs, root=tmp)
        return fails, g


def controls():
    fails, g = run_occ(100)
    c, p = g.cost(("PDREND", "pdr_entry"), "deep")
    j = " ".join(p)
    check(fails == [] and "pdxg_entry" in j and "pde_entry" in j,
          "PDREND's \"slot\" far call is followed into the occupant, and the "
          "occupant's far call into PDENG's door (20.22.2.4)", got=(fails, p))
    fails, g = run_occ(1900)
    check(any(f.startswith("DEEP PDREND pdr_entry") and "_pdxg_verb" in f for f in fails),
          "an occupant deeper than the private stack FAILS under PDREND, naming "
          "the occupant's verb", got=fails,
          why="the occupant runs on PDREND's verb's stack: its frames are "
              "PDREND's to pay")

    fails, g, txt = run()
    check(fails == [], "the clean synthetic tree passes the gate", got=fails, want=[])
    deep, _ = g.cost(("PDREND", "pdr_entry"), "deep")
    # from the door's entry: door 18 at the call + 2, verb 2 + 2, step 42 + 2,
    # leaf 2 + the gate's 6 for its kernel call (deepstk's figures)
    want = 18 + 2 + 2 + 2 + 42 + 2 + 2 + P.GATE
    check(deep == want, "the scan adds the door's pushes, every frame and the "
          "return addresses exactly", got=deep, want=want,
          why="a gate whose arithmetic is off by a frame is a gate that passes "
              "the next bez() by the size of that frame")

    fails, g, txt = run(leafbody="    call _verb_step")
    check(any("RECURSION" in f and "_verb_step" in f for f in fails),
          "a verb that recurses FAILS, named (D21)", got=fails,
          why="bez() recursed seven deep on the UI stack and wrote over the "
              "kernel's menu bar; no recursion is the rule that stops the next one")

    fails, g, txt = run(stepframe=1900)
    check(any(f.startswith("DEEP PDREND pdr_entry") for f in fails),
          "a verb deeper than the private stack's %d FAILS" % P.DEEP_BUDGET,
          got=fails)

    fails, g, txt = run(cbframe=300)
    check(any(f.startswith("UI cc_onwake") and "_os88_onwake" in f and "KAPI" in f for f in fails),
          "a callback whose frames + a kernel call pass %d FAILS, naming the "
          "callback and the kernel call under it" % P.UI_BUDGET, got=fails)

    fails, g, txt = run(cbframe=80)
    check(fails == [], "...and the same callback with an 80-byte frame passes "
          "(the far call to the door is charged at pdu_mcall's depth, not the module's)",
          got=fails, want=[])


def real():
    build = os.path.join(ROOT, "build")
    gens = ["pagedesk.gen.asm", "pdeng.gen.asm", "pdrend.gen.asm", "pdwrt.gen.asm"]
    if not all(os.path.isfile(os.path.join(build, g)) for g in gens):
        print("SKIP the real tree: build/{%s} not all built (make pagedesk)" % ",".join(gens))
        return
    buf = io.StringIO()
    fails, g = P.analyse(build, out=lambda s: buf.write(s + "\n"))
    sys.stdout.write(buf.getvalue())
    check(fails == [], "PageDesk's call graph: no recursion, DEEP <= %d, UI <= %d"
          % (P.DEEP_BUDGET, P.UI_BUDGET), got=fails, want=[],
          why="D21/D23: the UI task's stack is 512 bytes and the kernel's menu bar "
              "is the next thing under it")
    for seg, door in [("PDENG", "pde_entry"), ("PDREND", "pdr_entry"), ("PDWRT", "pdw_entry")] + \
            [(seg, door) for seg, _, door, _ in P.occupant_segments(ROOT)]:
        check((seg, door) in g.funcs, "%s's door %s is found" % (seg, door),
              why="a renamed door would make every chain under it read as zero")
    c, p = g.cost(("PDREND", "pdr_entry"), "deep")
    check("pde_entry" in " ".join(p), "PDREND's worst chain runs through its far "
          "call into PDENG's door (the nested module call is followed)", got=p)


def main():
    controls()
    real()
    done("t_pdstk")


if __name__ == "__main__":
    sys.exit(main())
