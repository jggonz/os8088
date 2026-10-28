#!/usr/bin/env python3
"""SHEET's code module MOVES out from under its own call chain (SPEC.md 82.16.11).

    make && make build/sheetovl360.img && python3 tests/sheetovlmove.py

WHAT IT IS ABOUT (docs/plans/NO-PINNING-PLAN.md item P1b). SHEET loads two
code modules into heap claims - the chart module (52KB) and the macro module
(28KB), CHART.OVL and MACRO.OVL until they became parts of SHEET.O88
(SPEC.md 82.16.12) - and
both were PINNED for the program's whole life - up to 80KB of wall - because
a module reaches resident code with `call far`, which leaves the MODULE'S OWN
CS on the stack for the return, and a resident routine (or a kernel slot the
module called directly) can claim, so a compaction under it would move the
module and the retf would land in whatever took its place. apps/cc/crt0.asm's
cc_ovthunk is the C SDK's answer to the same thing (SPEC.md 73.14.2.1);
apps/os88chartovl.inc's ch_xthunk is SHEET's, and every module->resident
crossing and every claim-capable kernel slot a module calls (SHK) goes
through it now, so both claims are declared movable.

THE SCENARIO IS A REAL ONE, not a test hook: Formula > Goto (F5) is
`ch_ovcall` -> CHART.OVL's SHM_IDOPEN -> `SHK wm_show` -> ch_xthunk ->
OSAPI_WM_SHOW -> wm_raise -> wm_su_take's `mem_claim` - a window being shown
banks the one it lands on. The row stops on that one `mem_claim` and asks for
one KB more than the largest free run, so it can be granted ONLY by a
compaction - which slides both modules down while CHART.OVL's frame is live.

TWO ARRANGEMENTS, each found by a run that proved nothing without it:
  - SHEET's window is given WF_SAVEU (SPEC.md 11.96.1), the promise PAINT and
    HELLO make for themselves: without it wm_su_take refuses at wm_dc_ok and
    the Goto open makes no claim at all.
  - The hole has to be BETWEEN SHEET's data claims and its modules, because
    the staging buffer among the data WAS PINNED when this row was written:
    a hole under SHEET only ever moved three claims below that wall. (It
    moves since NO-PINNING 1, SPEC.md 81.2.1; the placed hole still sits
    above all six data claims, so the scenario is unchanged.) So the row
    writes one claim record into mem_tab, OWNED BY NOTE PAD (the package that
    owns the floor here - HEAPFRAG's comb holds half the arena and leaves no
    room for SHEET's 200KB), just past the room SHEET's six data claims take.
    First fit puts the data under it and both modules over it; closing Note
    Pad frees it with Note Pad's own.

THE RETURN IS WATCHED, NOT ONLY POISONED. ovlmove.py's lesson is that a stale
return into memory still holding the module's old bytes runs the old copy and
"works". The row reads the module's frame off the stack at the forced claim
- its IP, and above it the module's CS (the old shape) or ch_xthunk's tag -
and when the module moves it breaks on that IP at BOTH homes, told apart by
CS: the old home is the defect, the new one the fix. Every paragraph of the
old home no live claim holds is poisoned with HLT as well.

SIX ASSERTIONS:
  1. both module claims are declared movable (MC_RLOC != 0, read from outside);
  2. CHART.OVL MOVED under the live chain, and the module's return from
     WM_SHOW came back to its NEW home, never to the old one;
  3. the Goto dialog opened - the module ran on after WM_SHOW returned;
  4. [ch_ovseg], [ch_ovfar+2] and [sh_m2far+2] name the modules' NEW claims;
  5. Esc closes it - the module's key and close paths, at its new address;
  6. F5 opens it again and Esc closes it again.

VERIFIED TO FAIL: with CHX put back to the old shim shape (`call routine /
retf`, the module's CS left on the stack) the return is caught at the OLD
home (5c00:2559, the module moved 5c00 -> 44c0), the UI task halts in the
poison, and 2, 5 and 6 go red. SOVL_DEBUG=1 prints every stop and the arena.
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
from sheetmove import pkg_syms                          # noqa: E402

DISK = "build/sheetovl360.img"
u16 = cmemmove.u16
MEM_MAX, MC_SIZE = os88geom.MEM_MAX, os88geom.MC_SIZE


def module_claim(m, S, owner, mod):
    """(base, paras, rloc) of the claim of `owner`'s holding the module's bytes."""
    head = mod[:64]
    for base, para, own, rloc in cmemmove.claims(m, S):
        if own == owner and m.read(base * 16, len(head)) == head:
            return base, para, rloc
    return None


def free_runs(m, S):
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


def module_frame(words, old, tag, back):
    """The module's innermost return frame on the stack: (index, IP, shape).

    Old shape: [ret into the shim][IP][the module's CS]. ch_xthunk's shape:
    [ch_xthunk.back][IP][the tag, the ADDRESS of ch_ovseg]. The first from
    the top of the stack is the frame the WM_SHOW chain will return through.
    """
    for i in range(1, len(words) - 1):
        if words[i + 1] == old:
            return i, words[i], "the module's CS (the old shape)"
        if words[i + 1] == tag and words[i - 1] == back:
            return i, words[i], "ch_xthunk's tag"
    return None


def poison(m, S, old, new, para):
    """HLT over every paragraph of the old home no live claim holds now."""
    held = [(new, new + para)]
    held += [(b, b + p) for b, p, _, _ in cmemmove.claims(m, S) if b != old]
    n, q = 0, old
    while q < old + para:
        if any(lo <= q < hi for lo, hi in held):
            q += 1
            continue
        e = q
        while e < old + para and e - q < 0x400 and \
                not any(lo <= e < hi for lo, hi in held):
            e += 1
        m.write(q * 16, b"\xF4" * ((e - q) * 16))
        n += (e - q) * 16
        q = e
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    S = os88sym.linear
    os88fixture.need(DISK)
    SH = pkg_syms("apps/sheet/sheet.asm")
    build = os.path.join(HERE, "..", "build")
    # the raw cuts os88pkg.py packed as SHEET.O88's parts 0 and 1 (82.16.12):
    # op_fetch expands each onto its claim's base, so these are its bytes
    chart = open(os.path.join(build, "sheet.chart.bin"), "rb").read()
    macro = open(os.path.join(build, "sheet.macro.bin"), "rb").read()
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

        def win(slot):
            return [x for x in os88geom.windows(m, S) if x.i == slot][0]

        def raise_win(slot, title=False):
            pt = cmemmove.uncovered(m, S, win(slot), prefer_title=title)
            if pt is None:
                raise RuntimeError("window %d is wholly covered" % slot)
            mo.click(*pt)
            os88marty.settle(m)

        # --- Note Pad owns the floor ---------------------------------------
        dispcp.open_named(m, mo, S, os88marty.settle, *disk,
                          name="NOTEPAD.O88")
        np_slot = dispcp.win_list(m, S)[-1]
        os88marty.quiesce(m, lambda: cmemmove.claims(m, S), guest=1.0,
                          what="Note Pad's claims")
        npw = win(np_slot)
        mo.drag(npw.x + 60, npw.y + 9, 60, npw.y + 9)   # sheetmove's reason:
        os88marty.settle(m)                               # keep a corner clear

        # --- ...and ONE MORE CLAIM of Note Pad's, where the hole is wanted --
        # SHEET claims in one go at its entry - cell, text, STAGING, borders,
        # chart, undo, then CHART.OVL and MACRO.OVL - and the staging buffer
        # WAS PINNED (it is every file call's ES:BX; movable since NO-PINNING
        # 1, pinned only across each file call, and the arrangement stands
        # because the hole below is placed above all six data claims). So a hole
        # left by a package closed under SHEET lands below that wall, and the
        # modules above it can never slide into it: the first runs here moved
        # three claims and no module. The hole has to be BETWEEN SHEET's data
        # and its modules, so the row writes one claim record into the
        # kernel's table, OWNED BY NOTE PAD, just past the room SHEET's six
        # data claims take: first fit puts all six under it and both modules
        # over it, and closing Note Pad frees it with the rest of Note Pad's.
        # The record is the kernel's own shape (MC_RLOC 0, pinned, like every
        # claim a package does not declare) and nothing else is touched.
        np_seg = u16(m.read(os88geom.winptr(m, np_slot, S) + os88geom.W_SEG,
                            2))
        cl = sorted(cmemmove.claims(m, S))
        lo = u16(m.read(S("mem_base"), 2))
        gaps, at = [], lo
        for base, para, _, _ in cl:
            if base > at:
                gaps.append((base - at, at))
            at = max(at, base + para)
        top = u16(m.read(S("mem_top"), 2))
        if top > at:
            gaps.append((top - at, at))
        run, run_at = max(gaps)
        DATA, WALL = 7488 + 64, 5184     # SHEET's six data claims, and the
        f_at = run_at + DATA             # module pair's 5,120 + a KB
        raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
        slot = next(i for i in range(MEM_MAX)
                    if u16(raw, i * MC_SIZE) == 0)
        m.write(S("mem_tab") + slot * MC_SIZE,
                bytes([f_at & 255, f_at >> 8, WALL & 255, WALL >> 8,
                       np_seg & 255, np_seg >> 8, 0, 0, 0, 0]))
        print("Note Pad (%04x) holds a placed claim %04x/%d paras, %d into a "
              "%d-paragraph run" % (np_seg, f_at, WALL, DATA, run))

        # --- SHEET above it: its entry proc claims both modules ------------
        raise_win(dslot)
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name="SHEET.O88")
        cmemmove.opened(m, S, "Sheet")
        sh_seg, sh_win = cmemmove.pkg_seg(m, S, "Sheet")
        if sh_seg is None:
            print("FAIL: SHEET never opened a window")
            return 1
        # NOT dragged down, as sheetmove.py does: a window over the open
        # dock is one wm_su_take refuses to bank (dock_hole_win), and the
        # bank's claim is the whole scenario
        c0 = module_claim(m, S, sh_seg, chart)
        m0 = module_claim(m, S, sh_seg, macro)
        if c0 is None or m0 is None:
            print("FAIL: SHEET holds no claim with CHART.OVL's (%r) or "
                  "MACRO.OVL's (%r) bytes - the modules did not load" % (c0, m0))
            print("      SHEET at %04x; [ch_ovseg] %04x, [ch_ovfail] %d, "
                  "[sh_m2far+2] %04x, [sh_m2fail] %d; claims %s"
                  % (sh_seg, u16(m.read(sh_seg * 16 + SH["ch_ovseg"], 2)),
                     m.read(sh_seg * 16 + SH["ch_ovfail"], 1)[0],
                     u16(m.read(sh_seg * 16 + SH["sh_m2far"] + 2, 2)),
                     m.read(sh_seg * 16 + SH["sh_m2fail"], 1)[0],
                     ["%04x/%d/%04x/%04x" % c for c in cmemmove.claims(m, S)]))
            return 1
        print("CHART.OVL at %04x (%d paras, MC_RLOC %04x), MACRO.OVL at %04x "
              "(%d paras, MC_RLOC %04x)" % (c0 + m0))
        ok = c0[2] != 0 and m0[2] != 0
        print("  1 both declared movable      %s" % ("yes" if ok else "NO"))
        bad += not ok

        # --- the floor owner closes: the hole is under both modules --------
        raise_win(np_slot, title=True)
        npw = win(np_slot)
        mo.click(npw.x + 8, npw.y + 9)                  # its close box
        os88marty.settle(m)
        if any(w.i == np_slot and w.visible for w in os88geom.windows(m, S)):
            print("FAIL: Note Pad did not close, so no hole opened")
            return 1
        raise_win(sh_win.i, title=True)          # F5 goes to the front window
        # THE FIRST ARRANGEMENT (see the header): SHEET's window is given
        # WF_SAVEU (SPEC.md 11.96.1), the promise PAINT and HELLO make for
        # themselves. Without
        # it wm_su_take refuses at wm_dc_ok - no promise, no raise cache - and
        # the Goto open claims nothing at all; the second run here stopped in
        # wm_su_take and never reached a claim. With it, wm_raise banks the
        # window the dialog lands on, from inside the module's WM_SHOW. It
        # changes what is drawn back on a raise and nothing else.
        wp = os88geom.winptr(m, sh_win.i, S) + os88geom.W_FLAGS
        m.write(wp, bytes([m.read(wp, 1)[0] | 32]))

        runs = free_runs(m, S)
        if os.environ.get("SOVL_DEBUG"):
            print("    claims (base/paras/owner/rloc): %s" % ["%04x/%d/%04x/%04x"
                  % c for c in sorted(cmemmove.claims(m, S))])
            print("    mem_base %04x mem_top %04x" % (u16(m.read(S("mem_base"), 2)),
                  u16(m.read(S("mem_top"), 2))))
        largest, total = max(runs), sum(runs)
        kb = largest // 64 + 1
        print("free runs %s paragraphs: largest %d KB, total %d KB -> the "
              "raise cache asks %d KB" % (runs, largest // 64, total // 64, kb))
        if total < kb * 64:
            print("FAIL: not enough free memory for the scenario")
            return 1

        # --- F5: stop on the bank's claim, inflate it; watch the return ----
        take = m.sym("wm_su_take")
        tag, back = SH["ch_ovseg"], SH["ch_xthunk.back"]
        homes = {c0[0]: ("CHART.OVL", c0[1]), m0[0]: ("MACRO.OVL", m0[1])}
        bps = ["mem_claim", "mem_reloc_call"]
        m.bp_exec(*bps)
        m.key("F5")
        forced = poisoned = 0
        frame = new_c = landed = None
        watch = {}
        for _ in range(80):
            if m.wait_stop(30 if not forced else 10) is None:
                break
            r = m.regs()
            at = r["cs"] * 16 + r["ip"]
            if os.environ.get("SOVL_DEBUG"):
                print("    stop %04x:%04x ax %04x bx %04x dx %04x ret %04x"
                      % (r["cs"], r["ip"], r["ax"], r["bx"], r["dx"],
                         u16(m.read(r["ss"] * 16 + r["sp"], 2))))
            if at in watch and landed is None:
                landed = (r["cs"], r["ip"])
                print("  the module's return landed at %04x:%04x - its %s home"
                      % (landed[0], landed[1],
                         "NEW" if landed[0] == new_c else
                         "OLD" if landed[0] == c0[0] else "??"))
                m.bp_exec(*bps)
            elif at == m.sym("mem_claim") and not forced:
                ret = r["cs"] * 16 + u16(m.read(r["ss"] * 16 + r["sp"], 2))
                if take < ret < take + 0x200:
                    stack = m.read(r["ss"] * 16 + r["sp"], 1024)
                    frame = module_frame([u16(stack, i)
                                          for i in range(0, 1024, 2)],
                                         c0[0], tag, back)
                    m.setreg("ax", kb)
                    forced = 1
                    print("  forced: the raise cache's claim %d KB -> %d KB; "
                          "the module's frame: %s"
                          % (r["ax"], kb, "IP %04x under %s" % frame[1:]
                             if frame else "NOT FOUND"))
            elif at == m.sym("mem_reloc_call") and r["bx"] in homes:
                name, para = homes[r["bx"]]
                n = poison(m, S, r["bx"], r["dx"], para)
                poisoned += n
                print("  %s moved %04x -> %04x: %d bytes of its old home "
                      "poisoned" % (name, r["bx"], r["dx"], n))
                if r["bx"] == c0[0] and frame:
                    new_c = r["dx"]
                    ip = frame[1]
                    watch = {c0[0] * 16 + ip: "old", new_c * 16 + ip: "new"}
                    m.bp_exec(*(bps + list(watch)))
            m.go()
        m.bp_exec()
        m.go()
        os88marty.settle(m)

        sh_seg, _ = cmemmove.pkg_seg(m, S, "Sheet")
        c1 = module_claim(m, S, sh_seg, chart) if sh_seg else None
        m1 = module_claim(m, S, sh_seg, macro) if sh_seg else None
        moved = forced and c1 is not None and c1[0] != c0[0]
        ok = moved and landed is not None and landed[0] == c1[0]
        print("  2 moved, returned to the new %s"
              % ("OK  %04x -> %04x, back at %04x:%04x" % (c0[0], c1[0],
                                                          *landed)
                 if ok else
                 "NO  <-- %s" % ("the run proves nothing (forced %d, %r)"
                                 % (forced, c1) if not moved else
                                 "the return was never seen" if landed is None
                                 else "it came back to the OLD home %04x:%04x"
                                 % landed)))
        bad += not ok

        def word(name):
            return u16(m.read(sh_seg * 16 + SH[name], 2))

        def top():                      # the FRONT window, by z-order: a
            zn = m.read(S("wm_zn"), 1)[0]   # slot index is not an age - the
            z = list(m.read(S("wm_zord"), zn))  # dialog reuses Note Pad's
            return z[-1] if z else None

        dlg = word("sh_idlg_win")
        ok = dlg != 0 and top() not in (None, sh_win.i)
        print("  3 Goto opened                %s"
              % ("yes" if ok else "NO (sh_idlg_win %04x, top %r)"
                 % (dlg, top())))
        bad += not ok

        want = (c1[0] if c1 else None, c1[0] if c1 else None,
                m1[0] if m1 else None)
        got = (word("ch_ovseg"), u16(m.read(sh_seg * 16 + SH["ch_ovfar"] + 2,
                                            2)),
               u16(m.read(sh_seg * 16 + SH["sh_m2far"] + 2, 2)))
        ok = got == want
        print("  4 the words follow           %s"
              % ("OK %s" % ["%04x" % g for g in got] if ok else
                 "STALE: ch_ovseg/ch_ovfar+2/sh_m2far+2 = %s, claims %s"
                 % (["%04x" % g for g in got], want)))
        bad += not ok

        def esc_closes(n):
            m.key("Escape")
            os88marty.settle(m)
            w = word("sh_idlg_win")
            ok = w == 0 and top() == sh_win.i
            print("  %s Esc closes it             %s"
                  % (n, "yes" if ok else "NO (sh_idlg_win %04x, top %r) "
                     "<-- the module never came back" % (w, top())))
            return ok

        shut = esc_closes("5")
        bad += not shut
        if shut:                        # a dialog still up from 3 would read
            m.key("F5")                  # as one opened again
            os88marty.settle(m)
            again = word("sh_idlg_win") != 0 and top() != sh_win.i
            print("  6 F5 opens it again          %s"
                  % ("yes" if again else "NO"))
            bad += not again
            if again:
                bad += not esc_closes("6")
        else:
            print("  6 F5 opens it again          NOT TRIED: 5 left it open")
            bad += 1

    print("sheetovlmove: %s" % ("FAIL (%d)" % bad if bad else
                                "all checks passed"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
