#!/usr/bin/env python3
"""THE ICON STORE GROWS (SPEC.md 25.9.6).

The machine-wide icon store was a fixed 60 rows, and a standard install of
the two 1.44MB disks outgrew it: a store that is full answers SPEC.md 25's
generic icon, and the document types - composed last - were the ones that
lost theirs.  `ico_add` now calls `ico_grow`, which takes the claim up a
kilobyte with `mem_regrow`.

Forcing a real overflow needs ~60 distinct packages mounted, so this row
makes the store FULL instead: it writes `[ico_max]` down to the rows already
held, which is exactly the state `ico_add` tests.  The second volume then has
packages the first did not, and each one wants a row.

  1  NEGATIVE CONTROL, in the same boot.  `[ico_kb]` is set to ICO_KBMAX, so
     the grow must refuse: A:'s packages that B: did not have draw the
     generic icon and the store does not move.  This is the leg that proves
     the poke reaches the test `ico_add` makes; without it, "it grew" could
     be a store that was never full.
  2  `[ico_kb]` back to ICO_KB and the listing re-made: the store GROWS -
     `[ico_kb]` + 1, `[ico_max]` re-derived from it, more rows than it held -
     every A:-only package that was on the generic icon in leg 1 takes a
     row, and no reference names a row past `[ico_n]`.
  3  ...AND THE ROWS IT HELD CAME WITH IT.  `mem_regrow` extends in place or
     moves and copies; either way row 0's key and body are byte-identical at
     the store's base afterwards, which is the capture-at-the-head layout
     (ICO_RBASE) working: a row's offset never changes, only the base.

Break it on purpose: take the `call ico_grow` out of `ico_add` (leave the
`jb .room` falling to `.no`) and leg 2 fails with A:'s packages on the
generic icon - the same picture leg 1 shows on purpose.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88ui                                                  # noqa: E402
import os88geom                                                # noqa: E402
import os88marty                                               # noqa: E402
import os88sym                                                 # noqa: E402

SYS = "build/os8088-360.img"
APPS = "build/apps360.img"
ICO_R_FOLDER = os88geom.ICO_R_FOLDER
ICO_R_NONE = os88geom.ICO_R_NONE

_SY = os88sym.syms()
_EQ = os88sym.equates()


def V(name):
    return _SY[name] if name in _SY else _EQ[name]


def fail(msg):
    print("icogrow: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - `make` builds it" % p)
    for e in ("ICO_RBASE", "ICO_KB", "ICO_KBMAX", "ICO_ROW", "ICO_KEY"):
        if e not in _EQ:
            fail("%s is not an equate of this kernel - it has no growing "
                 "store (SPEC.md 25.9.6)" % e)
    rbase, row = V("ICO_RBASE"), V("ICO_ROW")
    span = V("ICO_KEY") + V("DSK_ICO_SIZE")         # key + body

    with os88ui.boot(SYS, apps=APPS, quick=True) as ui:
        m = ui.m
        kseg = V("KERNEL_SEG")
        lin = getattr(m, "lin", lambda s, o=0: (s << 4) + o)

        def kb(name, n=1):
            return int.from_bytes(m.read((kseg << 4) + V(name), n), "little")

        def kput(name, val):
            m.write((kseg << 4) + V(name), bytes([val]))

        def state():
            return kb("ico_seg", 2), kb("ico_n"), kb("ico_max"), kb("ico_kb")

        def refs():
            n = kb("dsk_nmax", 2)
            vp = kb("fm_vp", 2)
            vseg = int.from_bytes(m.read((kseg << 4) + vp + V("FS_VSEG"), 2),
                                  "little") if vp else 0
            if not vseg:
                fail("the acting Disk window holds no FS_VSEG claim")
            st = V("DSK_DE_STRIDE")
            raw = m.read(lin(vseg), n * st)
            ix = m.read(lin(vseg, n * st), n)
            # FS_N, NOT dsk_nmax: the claim is dsk_nmax slots wide and a
            # short listing leaves the previous one's names and references
            # behind it - read past FS_N, A:/APPS "had" INSTALL.O88 on the
            # generic icon, which was the ROOT's stale slot 5 and no fault
            live = int.from_bytes(m.read((kseg << 4) + vp + V("FS_N"), 2),
                                  "little")
            out = []
            for i in range(min(live, n)):
                name = raw[i * st:i * st + 16].split(b"\0")[0]
                if name.upper().endswith(b".O88"):
                    out.append((name.decode("latin-1"), ix[i]))
            return out

        ui.path("B:/APPS")
        os88marty.settle(m)
        seg0, n0, max0, kb0 = state()
        print("icogrow: B:/APPS  store %04X  rows %d of %d  %dKB"
              % (seg0, n0, max0, kb0))
        if not seg0 or not n0:
            fail("B:/APPS stored no rows, so there is nothing to grow")
        if kb0 != V("ICO_KB") or max0 != (kb0 * 1024 - rbase) // row:
            fail("a first claim should read %dKB and %d rows; it reads %dKB "
                 "and %d (ico_need writes the pair as one word)"
                 % (V("ICO_KB"), (V("ICO_KB") * 1024 - rbase) // row,
                    kb0, max0))
        b_names = {nm for nm, r in refs() if r not in (ICO_R_FOLDER, ICO_R_NONE)}
        row0 = bytes(m.read(lin(seg0, rbase), span))

        # --- 1: FULL, AT THE CEILING: the grow must refuse ---------------
        kput("ico_max", n0)
        kput("ico_kb", V("ICO_KBMAX"))
        ui.open_drive("A")
        os88marty.settle(m)
        ui.path("A:/APPS")
        os88marty.settle(m)
        seg1, n1, max1, kb1 = state()
        new1 = [(nm, r) for nm, r in refs() if nm not in b_names]
        print("icogrow: leg 1 (ceiling)  store %04X  rows %d of %d  %dKB  "
              "A:-only %s" % (seg1, n1, max1, kb1,
                              " ".join("%s=%02X" % x for x in new1)))
        if not new1:
            fail("A:/APPS has no package B:/APPS did not, so nothing here "
                 "wants a new row - the disks changed under this row")
        if n1 != n0 or seg1 != seg0 or kb1 != V("ICO_KBMAX"):
            fail("at ICO_KBMAX the store still changed (rows %d -> %d, "
                 "%04X -> %04X, %dKB): the ceiling does not refuse"
                 % (n0, n1, seg0, seg1, kb1))
        if any(r != ICO_R_NONE for nm, r in new1):
            fail("a full store at its ceiling gave A:-only packages a row "
                 "(%s): the poke is not reaching ico_add's test, so leg 2 "
                 "would prove nothing" % new1)

        # --- 2: FULL, ROOM TO GROW --------------------------------------
        kput("ico_kb", V("ICO_KB"))
        ui.path("A:/")
        os88marty.settle(m)
        ui.path("A:/APPS")
        os88marty.settle(m)
        seg2, n2, max2, kb2 = state()
        new2 = [(nm, r) for nm, r in refs() if nm not in b_names]
        print("icogrow: leg 2 (grow)     store %04X  rows %d of %d  %dKB  "
              "A:-only %s" % (seg2, n2, max2, kb2,
                              " ".join("%s=%02X" % x for x in new2)))
        if kb2 != V("ICO_KB") + 1:
            fail("the store did not grow: %dKB, wanted %dKB (SPEC.md 25.9.6)"
                 % (kb2, V("ICO_KB") + 1))
        if max2 != (kb2 * 1024 - rbase) // row:
            fail("[ico_max] reads %d for a %dKB claim, which holds %d"
                 % (max2, kb2, (kb2 * 1024 - rbase) // row))
        if n2 <= n0:
            fail("the store grew and took no row (%d -> %d)" % (n0, n2))
        bad = [x for x in new2 if x[1] == ICO_R_NONE or x[1] >= n2]
        if bad:
            fail("A:-only package(s) %s still on the generic icon or past "
                 "[ico_n] = %d after the grow" % (bad, n2))

        # --- 3: THE OLD ROWS CAME WITH IT -------------------------------
        after = bytes(m.read(lin(seg2, rbase), span))
        if after != row0:
            fail("row 0 changed across the grow (store %04X -> %04X): the "
                 "regrow lost or shifted the rows it held" % (seg0, seg2))
        print("icogrow: row 0 identical across the grow (%s)"
              % ("moved" if seg2 != seg0 else "extended in place"))
    print("icogrow: ok")


if __name__ == "__main__":
    main()
