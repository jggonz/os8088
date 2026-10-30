#!/usr/bin/env python3
"""An emulator's RAM and its parts carve MOVE, and the emulated CPU runs on.

    make build/c64move360.img && python3 tests/emumove.py --pkg c64
    make build/a2move360.img  && python3 tests/emumove.py --pkg apple2

C64-SPEC 3.1.1, APPLE2-SPEC 3.1.1 and SPEC.md 73.14.2.1's CC_ONMOVE_PARTS
(docs/plans/NO-PINNING-PLAN.md C3 and C2). tests/rcmove.py's recipe for a
bigger layout. What is new here is the CARVE - the ROM part (and on the C64
the eager overlay beside it) in one claim - which stayed pinned because the
program keeps os88_part_seg(0)'s answer in its machine record's romseg, and is
movable now that the package's os88_onmove re-reads it. The two arms take the
two procs crt0 can give a carve: the C64's is cc_ovcarve (re-stamp the module,
then tell the C), the Apple II's is cc_partsmv proper (its overlay is lazy).

THE LAYOUT. Note Pad first, so its region takes the CEILING and its document
claim the floor; then the emulator, whose region and carve are claimed
top-down right under Note Pad's and whose RAM bottom-up above the document;
then heapfrag, LAST, so its pinned blocks land below the carve and wall
nothing. Note Pad is closed - a hole at the ceiling over the carve and one on
the floor under the RAM - and a key in heapfrag posts its region compaction
(heapfrag.asm hf_key, SPEC.md 66.4.3), which runs both passes: the RAM slides
DOWN, the region and the carve slide UP. Measured on the C64: RAM 1940 ->
1900, region 9040 -> 9540, carve 8a80 -> 8f80.

It does NOT wait for the emulated machine's prompt: the C64's KERNAL takes
~280 s of guest time to reach READY. on a 4.77 MHz 8088 (C64-SPEC 9.7). The
move is a property of the claims; what is asserted of the machine is that it
runs THROUGH the move, mid-boot, fetching out of the carve and writing its RAM.

SIX ASSERTIONS:

  1. BOTH DECLARED - MC_RLOC non-zero on the RAM claim and on the carve, read
     from mem_tab (a refused declaration looks like success from inside).
  2. THE RAM MOVED - to a new base, and the record's ramseg names that claim.
  3. THE CARVE MOVED, and romseg followed it - the same distance above
     op_base as before the pass, which is os88_part_seg's answer for where
     the carve is now.
  4. THE ROM IS AT romseg - its first 16 bytes and the last 16 the carve
     keeps equal the ROM file: a romseg left behind reads whatever took the
     carve's place.
  5. (C64) THE MODULE AND THE CHARGEN FOLLOWED - cc_ovseg sits the same
     distance above op_base, and c64_chr_seg names one of the two claims.
  6. THE MACHINE STILL RUNS - not jammed, and the 6502/6510's PC is seen at
     more than one address over 60 frames after the pass.

VERIFIED RED, C64: os88_onmove keeping `rom` and not storing it into
c64_m.romseg fails 3 and 4 (romseg left at 8aa0 with the carve at 8f80).
VERIFIED RED, Apple II: os88_onmove not re-reading a2_m.romseg fails 3, 4 and
6 - romseg left at 8c00 with the carve at 9140, and the 6502, fetching the
ROM through a stale bias, stuck at one PC.
C64MOVE_DUMP=1 prints mem_tab before and after.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88fixture                                      # noqa: E402
import os88marty                                        # noqa: E402
import os88ui                                           # noqa: E402
from cmemmove import pkg_syms, u16, claims, pkg_seg, heapfrag_ran  # noqa: E402

M_RAMSEG, M_ROMSEG, M_PC = 0, 2, 4          # both cores' machine records
PKGS = {
    "c64": dict(disk="build/c64move360.img", shim="apps/c64/c64.asm",
                name="C64.O88", title="VICE (C64)", rec="_c64_m",
                state="_c64_state", jam=1, rom="build/c64-rom/C64.ROM",
                keep=20480, ovl=True, chr="_c64_chr_seg"),
    # the Apple II's carve is shrunk to the main ROM once the chargen is
    # decoded (APPLE2-SPEC 1.5), so the last 16 bytes it KEEPS end at 0x3000
    "apple2": dict(disk="build/a2move360.img",
                   shim="apps/apple2/apple2.asm", name="APPLE2.O88",
                   title="Apple II", rec="_a2_m", state="_a2_state", jam=2,
                   rom="build/apple2-rom/APPLE2.ROM", keep=0x3000, ovl=False,
                   chr=None),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pkg", choices=sorted(PKGS), default="c64")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    P = PKGS[a.pkg]
    os88fixture.need(P["disk"])
    C = pkg_syms(P["shim"])
    rom = open(P["rom"], "rb").read()[:P["keep"]]
    dump = os.environ.get("C64MOVE_DUMP")

    with os88ui.boot("build/os8088-360.img", apps=P["disk"],
                     machine=a.machine, quick=True) as ui:
        m, S = ui.m, ui.sym

        def eseg():
            return pkg_seg(m, S, P["title"])[0]

        mega = getattr(m, "bochs", False)

        def lin(sg, off=0):
            return m.lin(sg, off) if mega else sg * 16 + off

        def para(sg):
            """A segment word as mem_tab keys its claim: on kern_mega
            (MC7.4) a SELECTOR that a move rebases and never changes, so
            the claim is the paragraph under it - and "it moved", "romseg
            followed" are asked of paragraphs. The word itself elsewhere"""
            return (m.lin(sg) >> 4) if mega and sg else sg

        def w(seg, name, off=0):
            return u16(m.read(lin(seg, C[name] + off), 2))

        def state(seg):
            st = dict(ram=para(w(seg, P["rec"], M_RAMSEG)),
                      rom=para(w(seg, P["rec"], M_ROMSEG)),
                      pc=w(seg, P["rec"], M_PC),
                      opb=para(w(seg, "op_base")), st=w(seg, P["state"]))
            if P["ovl"]:
                st["ovs"] = para(w(seg, "cc_ovseg"))
            if P["chr"]:
                st["chr"] = para(w(seg, P["chr"]))
            return st

        def rloc(base):
            h = [c for c in claims(m, S) if c[0] == base]
            return h[0] if h else None

        def show():
            if dump:
                for c in sorted(claims(m, S)):
                    print("    claim %04x +%04x own %04x rloc %04x" % c)

        def hf_rn():
            hseg, _ = pkg_seg(m, S, "Heap")
            img = u16(m.read(lin(hseg, 8), 2))
            return u16(m.read(lin(hseg, img + 152), 2))   # heapfrag's hf_rn

        ui.open_drive("B")
        np = ui.open("NOTEPAD.O88")     # the ceiling, and a floor document
        ui.open(P["name"])              # region and carve right under it
        os88marty.until(m, lambda _: state(eseg())["pc"] != 0,
                        "the emulated CPU's first slice", poll=0.5, guest=120)
        hf = ui.open("HEAPFRAG.O88")    # LAST: its pinned blocks land below
        os88marty.until(m, lambda _: heapfrag_ran(m, S),   # the carve
                        "heapfrag's suite and its posted pass", poll=0.5,
                        guest=240)
        seg = eseg()
        s0 = state(seg)
        print("%s at %04x: ram %04x, carve %04x (rom %04x%s)"
              % (a.pkg, seg, s0["ram"], s0["opb"], s0["rom"],
                 ", module %04x" % s0["ovs"] if P["ovl"] else ""))
        show()
        bad = 0
        r_ram, r_cv = rloc(s0["ram"]), rloc(s0["opb"])
        ok1 = bool(r_ram and r_ram[3] and r_cv and r_cv[3])
        print("  1 both declared          %s"
              % ("RAM MC_RLOC=%04x, carve MC_RLOC=%04x" % (r_ram[3], r_cv[3])
                 if ok1 else "NO: RAM %r, carve %r" % (r_ram, r_cv)))
        bad += not ok1

        ui.close(np)
        ui.raise_window(hf)
        m.key("KeyA")
        os88marty.until(m, lambda _: hf_rn() >= 4,
                        "heapfrag's posted region pass and its wake",
                        poll=0.5, guest=120)
        os88marty.quiesce(m, lambda: claims(m, S), guest=1.0,
                          what="the claims after the pass")
        show()

        seg = eseg()
        s1 = state(seg)
        print("after the pass: %s at %04x, ram %04x, carve %04x (rom %04x%s)"
              % (a.pkg, seg, s1["ram"], s1["opb"], s1["rom"],
                 ", module %04x" % s1["ovs"] if P["ovl"] else ""))
        h1 = rloc(s1["ram"])
        ok2 = (bool(r_ram) and s1["ram"] != s0["ram"] and bool(h1)
               and h1[1] == r_ram[1])
        print("  2 the RAM moved          %s"
              % ("%04x -> %04x, and ramseg names it" % (s0["ram"], s1["ram"])
                 if ok2 else "NO: ramseg %04x, claim %r" % (s1["ram"], h1)))
        bad += not ok2
        ok3 = (s1["opb"] != s0["opb"] and rloc(s1["opb"]) is not None
               and (s1["rom"] - s1["opb"]) == (s0["rom"] - s0["opb"]))
        print("  3 carve moved, romseg too %s"
              % ("%04x -> %04x, rom %04x -> %04x"
                 % (s0["opb"], s1["opb"], s0["rom"], s1["rom"]) if ok3 else
                 "NO: carve %04x -> %04x, rom %04x -> %04x"
                 % (s0["opb"], s1["opb"], s0["rom"], s1["rom"])))
        bad += not ok3
        head = m.read(s1["rom"] * 16, 16)
        tail = m.read(s1["rom"] * 16 + len(rom) - 16, 16)
        ok4 = head == rom[:16] and tail == rom[-16:]
        print("  4 the ROM is at romseg   %s" % ("OK" if ok4 else "NO"))
        bad += not ok4
        if P["ovl"]:
            ok5 = ((s1["ovs"] - s1["opb"]) == (s0["ovs"] - s0["opb"])
                   and s1["chr"] in (s1["ram"], s1["rom"]))
            print("  5 module and chr follow  %s"
                  % ("OK" if ok5 else "WRONG: module %04x, chr %04x"
                     % (s1["ovs"], s1["chr"])))
            bad += not ok5
        pcs = set()
        for _ in range(6):
            m.advance(frames=10)
            pcs.add(state(seg)["pc"])
        s2 = state(seg)
        ok6 = s2["st"] != P["jam"] and len(pcs) > 1
        print("  6 the machine runs       %s (state %d, %d PCs seen)"
              % ("OK" if ok6 else "NO", s2["st"], len(pcs)))
        bad += not ok6
        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
