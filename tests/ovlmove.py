#!/usr/bin/env python3
"""A C overlay MOVES out from under its own call chain, and both returns land.

    make covl build/ovlmove360.img && python3 tests/ovlmove.py

WHAT IT IS ABOUT (docs/plans/NO-PINNING-PLAN.md wave 0, SPEC.md 73.14.2.1).
A C module function calling ANOTHER module function used to be
`call far [cc_ovm_*]`: the module's own CS went on the stack for a plain
`retf`, so a claim under that call that compacted moved the module and the
return landed in whatever took its place. LEMMINGS found it and the overlay
carve was PINNED for every C package's whole life. The fix routes that call
through crt0's cc_ovmthunk, which keeps the return offset in the stash and
rebuilds the `retf` from [cc_ovcs] - so the module may move.

THE SCENARIO IS THAT EXACT CHAIN. tests/covl's M key runs
ovl_mv_outer -> ovl_mv_inner (module to module) -> cv_claim (out, resident)
-> os88_mem_claim, with a size this row picks so the claim fits ONLY after a
compaction. COVL's module is its LAZY part, a bottom-up claim; HEAPFRAG goes
first and owns the floor, COVL lands above it, HEAPFRAG closes, and the hole
it leaves is under the module - so the compaction slides the module down
while both module frames are live.

FOUR ASSERTIONS:
  1. the module claim is declared movable (MC_RLOC != 0, read from outside);
  2. M's claim was granted and the call chain answered 1008 (= 1000 + 7 + 1):
     both returns landed in the moved module;
  3. the module MOVED - the claim holding COVL.OVL's bytes has a new base -
     which is what makes 2 mean anything;
  4. the program still works: SPACE answers ovl_mix(1,2,3,4) = 1234.

VERIFIED TO FAIL: with tools/cc8086.py emitting the old direct
`call far [cc_ovm_*]` for a module-to-module call, the move strands the
return and assertion 2 goes red. IT PASSED THAT RED RUN UNTIL THE CLAIM WAS
POISONED: the claim made under the chain is never written, so the module's
OLD bytes were still at the stale return address and the old copy of the code
ran and answered correctly. The row now fills the claim with HLT before it is
freed, so a stale return stops dead.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import cmemmove                                         # noqa: E402
import dispcp                                           # noqa: E402
import os88fixture                                      # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402

DISK = "build/ovlmove360.img"
u16 = cmemmove.u16


def module_claim(m, S, owner, mod):
    """(base, paras, rloc) of the claim holding the module's bytes."""
    head = mod[:64]
    for base, para, own, rloc in cmemmove.claims(m, S):
        if own != owner:
            continue
        if head in m.read(base * 16, min(para * 16, len(mod) + 1024)):
            return base, para, rloc
    return None


def free_runs(m, S):
    """Free runs in paragraphs, between mem_base and mem_top."""
    lo = u16(m.read(S("mem_base"), 2))
    hi = u16(m.read(S("mem_top"), 2))
    runs, at = [], lo
    for base, para, _, _ in sorted(cmemmove.claims(m, S)):
        if base > at:
            runs.append(base - at)
        at = max(at, base + para)
    if hi > at:
        runs.append(hi - at)
    return runs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    S = os88sym.linear
    os88fixture.need(DISK)
    CV = cmemmove.pkg_syms("tests/covl/covl.asm",
                           incs=("apps/", "build/", "tests/covl/"))
    mod = open(os.path.join(HERE, "..", "build", "COVL.OVL"), "rb").read()
    bad = 0

    with os88marty.launch("build/os8088-360.img", apps=DISK,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.kernel_up(m)
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)
        os88marty.settle(m)
        disk = dispcp.win_rect(m, S, dslot)[:2]

        def raise_win(slot):
            w = [x for x in os88geom.windows(m, S) if x.i == slot][0]
            pt = cmemmove.uncovered(m, S, w)
            mo.click(*pt)
            os88marty.settle(m)

        dispcp.open_named(m, mo, S, os88marty.settle, *disk,
                          name="HEAPFRAG.O88")
        os88marty.until(m, lambda _: cmemmove.heapfrag_ran(m, S),
                        "heapfrag's suite", poll=0.5, limit=120)
        os88marty.settle(m)
        _, hf_win = cmemmove.pkg_seg(m, S, "Heap")

        raise_win(dslot)
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name="COVL.O88")
        cmemmove.opened(m, S, "C Overlay")
        cv_seg, cv_win = cmemmove.pkg_seg(m, S, "C Overlay")
        if cv_seg is None:
            print("FAIL: COVL never opened a window")
            return 1

        def cvw(name):
            return u16(m.read(cv_seg * 16 + CV[name], 2))

        m.key("Space")                  # the first call fetches the module
        os88marty.settle(m)
        before = module_claim(m, S, cv_seg, mod)
        if before is None:
            print("FAIL: no claim of COVL's holds COVL.OVL's bytes")
            return 1
        print("module claim at %04x (%d paragraphs), MC_RLOC %04x"
              % before)
        ok = before[2] != 0
        print("  1 declared movable          %s" % ("yes" if ok else "NO"))
        bad += not ok

        # --- close heapfrag: the floor under the module opens ---------------
        pt = cmemmove.uncovered(m, S, hf_win, prefer_title=True)
        mo.click(*pt)
        os88marty.settle(m)
        mo.click(hf_win.x + 8, hf_win.y + 9)
        os88marty.settle(m)

        runs = free_runs(m, S)
        largest, total = max(runs), sum(runs)
        kb = largest // 64 + 1          # fits only once the runs are joined
        print("free runs %s paragraphs: largest %d KB, total %d KB -> M "
              "claims %d KB" % (runs, largest // 64, total // 64, kb))
        if total // 64 < kb + 2:
            print("FAIL: not enough free memory for the scenario")
            return 1

        m.write(cv_seg * 16 + CV["_cv_mvkb"], bytes([kb & 0xFF, kb >> 8]))
        raise_win(cv_win.i)
        # THE CLAIM IS POISONED BEFORE IT IS GIVEN BACK. It is never written by
        # COVL, so without this the module's OLD bytes are still where a stale
        # return would land - and a stale return then runs the old copy of the
        # code and "works", which is exactly how this defect stayed silent.
        # mem_free_x with DX = M's claim is cv_claim giving it back: fill it
        # with HLT, so a return into the old module stops dead instead.
        m.bp_exec("mem_free_x")
        m.key("KeyM")
        poisoned = 0
        for _ in range(40):
            if m.wait_stop(30) is None:
                break
            dx = m.regs()["dx"]
            hit = [c for c in cmemmove.claims(m, S)
                   if c[0] == dx and c[1] == kb * 64]
            if hit:
                for at in range(0, kb * 1024, 0x4000):
                    m.write(dx * 16 + at, b"\xF4" * min(0x4000,
                                                       kb * 1024 - at))
                poisoned = kb * 1024
                break
            m.go()
        m.bp_exec()
        m.go()
        print("poisoned %d bytes of M's claim before it was freed" % poisoned)
        os88marty.settle(m)

        res = cvw("_cv_mvres")
        ok = res == 1008
        print("  2 the chain answered        %d  %s"
              % (res, "(1000 + 7 + granted)" if ok else
                 "<-- want 1008 (1007 = claim refused, anything else = a "
                 "return landed somewhere it should not)"))
        bad += not ok

        after = module_claim(m, S, cv_seg, mod)
        ok = after is not None and after[0] != before[0]
        print("  3 the module moved          %s"
              % ("%04x -> %04x" % (before[0], after[0]) if ok else
                 "NO  <-- the run proves nothing (%r)" % (after,)))
        bad += not ok

        m.write(cv_seg * 16 + CV["_cv_mix"], b"\0\0")
        m.key("Space")
        os88marty.settle(m)
        mix = cvw("_cv_mix")
        ok = mix == 1234
        print("  4 still works               ovl_mix = %d%s"
              % (mix, "" if ok else "  <-- want 1234"))
        bad += not ok

    print("ovlmove: %s" % ("FAIL (%d)" % bad if bad else "4 checks passed"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
