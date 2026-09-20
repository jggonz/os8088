#!/usr/bin/env python3
"""ASSOC.DAT's buffer is a FILE BUFFER now (SPEC.md 54.7.4 / 25.9.4).

The volume's association cache used to be a 3KB claim held for the whole
session, and 2,560 of those bytes were icon bodies - the same pictures, under
the same `(stem, size)` identity, as the machine-wide store SPEC.md 25.9
built.  Two places holding one picture is what that whole design is against,
and this was the larger of the two.  `asc_use` now ABSORBS every row's body
into the store and FREES the claim before it returns.

WHAT THIS ASSERTS, and the first two are the point:

  gone      after a mount there is NO `MEM_K_ASC` record in `mem_tab` and
            `asc_seg` is 0.  Asserted on the ALLOCATOR and not on the
            variable, because the variable alone would pass if the claim were
            leaked instead of freed - which is the one way this change could
            be worse than what it replaced
  absorbed  ...and the bodies survived it.  Mounting a volume's ROOT stores
            the whole VOLUME's packages, because ASSOC.DAT covers the volume
            and the absorb walks all of it - so the count after a root mount
            is far above what the root's own two or three entries could
            explain
  warm      entering the folder those packages live in adds nothing, which is
            the saving spelled as a number: every one of them resolved out of
            the store instead of reading its first sector
  stamp     and a SHED costs the stamp its meaning.  `asc_vol` says "this
            volume's ASSOC.DAT is in the store" now, and the store is
            PURGEABLE - so `asc_use`'s compare asks `[ico_n]` beside it and an
            emptied store re-reads.  Without that, the next mount skips the
            three-sector re-read and pays a sector per package instead, which
            is a 400 ms `int 13h` apiece on the target machine

**How to make it go red** (WRITING-TESTS 1, all four watched): take the
`call asc_drop` out of `asc_use_x.out` and `gone` fails with the record still
in `mem_tab`; take `call asc_absorb` out and `absorbed` fails with the store
holding only the root's own entries; take the `cmp byte [ico_n], 0 / jne
asc_use_out` out of `asc_use_x` and `stamp` fails, the re-mount trusting a
stamp for bodies that have been purged.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88geom                                                # noqa: E402
import os88marty                                               # noqa: E402
import os88sym                                                 # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
APPS = "build/apps360.img"
ICO_R_FOLDER = os88geom.ICO_R_FOLDER    # ...from the kernel source, not
ICO_R_NONE = os88geom.ICO_R_NONE        # retyped (t_mirror, SPEC.md 25.9)

_SY = os88sym.syms()
_EQ = os88sym.equates()


def V(name):
    return _SY[name] if name in _SY else _EQ[name]


def fail(msg):
    print("ascabsorb: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - `make` builds it" % p)

    with os88ui.boot(SYS, apps=APPS) as ui:
        m = ui.m
        kseg = V("KERNEL_SEG")
        MEM_K_ASC = _EQ["MEM_K_ASC"]

        def u8(sym):
            return m.read((kseg << 4) + V(sym), 1)[0]

        def u16(sym):
            return int.from_bytes(m.read((kseg << 4) + V(sym), 2), "little")

        def asc_record():
            """The allocator's own answer: is MEM_K_ASC holding anything?

            `mem_tab` IS IN `.lowbss` AND SO IN LOW_SEG, not the kernel
            segment - `mem_claim_1` walks it as `[ss:di+MC_SEG]` (SPEC.md
            1: SS is LOW_SEG and SS != DS). Reading it at KERNEL_SEG returns
            something that parses perfectly and holds no records, so the
            `gone` verdict below passed vacuously until the break-on-purpose
            run took asc_drop out and it STILL said "none".
            """
            sz = os88geom.MC_SIZE
            tab = m.read((V("LOW_SEG") << 4) + V("mem_tab"),
                         _EQ["MEM_MAX"] * sz)
            for i in range(_EQ["MEM_MAX"]):
                r = tab[i * sz:(i + 1) * sz]
                seg = int.from_bytes(r[os88geom.MC_SEG:][:2], "little")
                own = int.from_bytes(r[os88geom.MC_OWN:][:2], "little")
                if seg and own == MEM_K_ASC:
                    return seg, int.from_bytes(
                        r[os88geom.MC_PARA:][:2], "little") * 16
            return None

        def rows():
            return u8("ico_n")

        # --- 1. THE CLAIM IS GONE ------------------------------------------
        # The BOOT mount is a mount, so this has already happened once before
        # anything here runs - which is the case kernel.asm's KERN_BUDGET
        # comment was written about, a 3KB claim standing on a bare desktop.
        ui.open_drive("B")
        os88marty.settle(m)
        rec = asc_record()
        seg = u16("asc_seg")
        print("ascabsorb: B: mounted - asc_seg %04x, mem_tab record %s, "
              "store %d row(s), asc_vol %02X"
              % (seg, "none" if rec is None else "%04x %d bytes" % rec,
                 rows(), u8("asc_vol")))
        if rec is not None:
            fail("MEM_K_ASC still holds %d bytes at %04x after the mount - the "
                 "cache is meant to be a FILE BUFFER now (SPEC.md 54.7.4), "
                 "freed by asc_drop on every exit from asc_use" % (rec[1], rec[0]))
        if seg:
            fail("asc_seg is %04x with no record behind it, which is worse "
                 "than the claim: a segment word naming a block that is gone "
                 "is a live wrong answer, not a missing one" % seg)

        # --- 2. ...AND THE BODIES SURVIVED IT ------------------------------
        # B:/ root holds APPS/, MEDIA/ and a document or two, so its own
        # harvest can account for almost nothing.  The store is full of the
        # volume's PACKAGES, which live one folder down (SPEC.md 19.2) and
        # which nothing has listed yet.
        root_rows = rows()
        if root_rows < 8:
            fail("the store holds %d row(s) after a ROOT mount. ASSOC.DAT "
                 "covers the whole VOLUME and asc_absorb walks all of it, so "
                 "this is the absorb not happening - the shipped apps disk "
                 "declares 23 rows of which every one carries a body"
                 % root_rows)
        vol = u8("asc_vol")
        if vol == 0xFF:
            fail("asc_vol is 0xFF after a mount that absorbed %d row(s): the "
                 "stamp is what makes a re-entry free, and nothing would ever "
                 "take it" % root_rows)

        # --- 3. THE FOLDER IS THEN WARM ------------------------------------
        m.disk(reset=True)          # SPEC.md 18.94's counters from OUTSIDE:
                                    # what the CONTROLLER was asked for, with
                                    # nothing in the guest to believe
        ui.path("B:/APPS")
        os88marty.settle(m)
        after_rows = rows()
        n = u16("dsk_nmax")
        # **OUT OF THE ACTING WINDOW'S OWN CLAIM**
        # (docs/plans/LISTING-HOME-PLAN.md 13, SPEC.md 22.6.3). A listing has
        # no home of its own any more: a Disk window's mount writes the
        # entries AND their reference index straight into the FS_VSEG claim
        # the window already holds. This read `LOW_SEG:dsk_icoix`, a fixed
        # `.lowbss` array that no longer exists at all.
        #
        # `[dsk_dseg]` is NOT the way in, and that is worth writing down
        # because it is the obvious-looking one: it is an ARGUMENT to the
        # next loud mount, set immediately before one and put back to zero
        # after it ("a `.bss` word may not name a claim across arbitrary
        # time", kernel/files.inc), so it reads 0 by the time anything has
        # settled. The window is where the listing still is.
        #
        # The index sits immediately past the entries, which is `fmv_iofs`'s
        # arithmetic and `dsk_dest_x`'s, and `[dsk_nmax]` is the width both
        # of them use. tests/icostore.py reads it the same way.
        vp = u16("fm_vp")
        if not vp:
            fail("there is no acting Disk window after entering B:/APPS, so "
                 "there is nowhere for a listing to be (SPEC.md 22.6.3)")
        vseg = int.from_bytes(m.read((kseg << 4) + vp + V("FS_VSEG"), 2),
                              "little")
        if not vseg:
            fail("the acting Disk window holds no FS_VSEG claim after "
                 "entering B:/APPS - a window with no store gets a QUIET "
                 "mount, which harvests no icons at all, and there is no "
                 "floor listing to fall back to any more (SPEC.md 22.6)")
        refs = list(m.read((vseg << 4) + n * V("DSK_DE_STRIDE"), n))
        used = [r for r in refs if r not in (ICO_R_FOLDER, ICO_R_NONE)]
        print("ascabsorb: B:/APPS - %d entry/ies iconned, store %d -> %d"
              % (len(used), root_rows, after_rows))
        if not used:
            fail("B:/APPS iconned nothing at all")
        if after_rows - root_rows > len(used) // 2:
            fail("entering B:/APPS added %d row(s) for %d iconned entry/ies - "
                 "the absorb at the root mount should have stored these "
                 "already, so most of them are being read off the disk again "
                 "(SPEC.md 54.7.4)" % (after_rows - root_rows, len(used)))
        d = m.disk()
        print("ascabsorb: ...and the whole navigation cost %d read(s) of %d "
              "sector(s)" % (d.get("reads", -1), d.get("read_sectors", -1)))

        # --- 4. A SHED COSTS THE STAMP ITS MEANING -------------------------
        # The store is purgeable (MEM_P_ICO), so a program taking the arena
        # frees it under a desktop that is still running.  asc_vol then claims
        # a volume whose bodies are gone.
        m.write((kseg << 4) + V("ico_seg"), b"\x00\x00")
        m.write((kseg << 4) + V("ico_n"), b"\x00")
        os88marty.settle(m)
        ui.path("B:/")
        os88marty.settle(m)
        print("ascabsorb: after a shed and a re-mount - asc_vol %02X, store "
              "%d row(s)" % (u8("asc_vol"), rows()))
        if rows() < 8:
            fail("the store refilled to %d row(s) after a shed. asc_use's "
                 "compare asks [ico_n] beside asc_vol (SPEC.md 54.7.4) "
                 "precisely so an emptied store re-reads ASSOC.DAT; this is "
                 "that test not happening, and every package on the volume is "
                 "back to one sector read apiece" % rows())
        rec = asc_record()
        if rec is not None:
            fail("MEM_K_ASC is holding %d bytes again after the re-read - the "
                 "drop is missing on some path out of asc_use" % rec[1])

    print("ascabsorb: ok - the cache is a file buffer, and the bodies outlive it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
