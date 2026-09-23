#!/usr/bin/env python3
"""TWO VOLUMES, ONE BODY (SPEC.md 25.9).

The Disk window's icons used to be a 64-byte slot PER ENTRY, per listing AND
mirrored per open window - so the copy of a package on the system disk and the
copy on the apps disk were two bodies in RAM, and a folder full of documents
was sixty-four zero bytes apiece.  SPEC.md 25.9 keeps ONE body per distinct
icon for the whole machine and gives a listing one REFERENCE BYTE per entry.

WHAT THIS ASSERTS, and none of it is visible on the glass:

  1  a listing's references are DISTINCT - fourteen entries, fourteen bodies.
     The defect this catches is real and shipped for one commit: the folder's
     SHARED allocator was reached by the per-entry paths too, so `dsk_icoix`
     read `00 00 00 ... 00` and every row drew the same picture.
  2  a FOLDER takes no row at all (ICO_R_FOLDER, the built-in body).
  3  ...THE SECOND VOLUME REUSES THE FIRST'S ROWS.  SPEC.md 24.3 ships the
     core packages on the system disk AND the apps disk, so this is the
     ordinary case rather than a contrived one.
  4  and a SHED is survivable: the store refills from the next mount.  The
     claim is purgeable (MEM_P_ICO), so a DOS program taking the arena frees
     it under a desktop that is still running.

**WHAT ASSERTION 4 DOES NOT COVER, said rather than implied.**  `ico_body`
refuses a row past `[ico_n]`, which is what stops a listing staged BEFORE a
shed from resolving row 12 of a store that no longer has twelve rows - 64
bytes of whatever the heap handed out next, drawn as an icon.  This row does
NOT test that guard: navigating after the shed RE-MOUNTS, so every reference
it then reads is fresh.  Removing the guard leaves this row green, which was
checked rather than assumed.

Covering it needs a repaint of a window whose references are stale WITHOUT a
mount - raise or drag the same window after the shed - and then a pixel test,
because the failure is a wrong picture and not a wrong number: with the guard
every stale entry draws SPEC.md 25's generic icon, so the icon cells are
identical to each other; without it each reads a different 64 bytes of freed
heap and they differ.  That is the shape of the test somebody should write.

THE KEY IS `(name, size)` AND THAT IS THE WHOLE DESIGN (SPEC.md 25.9): it is
the only identity available without a SECTOR READ, which is what SPEC.md 54.7's
cache exists to avoid.  Break it by keying on the name alone and two packages
of the same name and different sizes merge; break it by keying on the size
alone and everything of one size merges.  Either shows up here as a row count
that is too low.
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
ICO_R_FOLDER = os88geom.ICO_R_FOLDER    # ...from the kernel source, not
ICO_R_NONE = os88geom.ICO_R_NONE        # retyped (t_mirror, SPEC.md 25.9)

_SY = os88sym.syms()
_EQ = os88sym.equates()


def V(name):
    return _SY[name] if name in _SY else _EQ[name]


def fail(msg):
    print("icostore: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - `make` builds it" % p)

    with os88ui.boot(SYS, apps=APPS) as ui:
        m = ui.m
        kseg, low = V("KERNEL_SEG"), V("LOW_SEG")

        def rows():
            return m.read((kseg << 4) + V("ico_n"), 1)[0]

        def refs():
            # **OUT OF THE ACTING WINDOW'S OWN CLAIM**
            # (docs/plans/LISTING-HOME-PLAN.md 13). A Disk window's mount
            # writes the entries AND their reference index straight into
            # FS_VSEG now, and leaves the floor listing honestly empty - so
            # reading `LOW_SEG:dsk_icoix`, which is what this did, answers
            # about whatever was listed there LAST. It read sixteen zeroes
            # and reported them as the shared-body defect, which is the same
            # picture a real one makes.
            #
            # The index sits immediately past the entries, which is
            # `fmv_iofs`'s arithmetic and `dsk_dest_x`'s, and `[dsk_nmax]` is
            # the width both of them use.
            n = int.from_bytes(m.read((kseg << 4) + V("dsk_nmax"), 2), "little")
            vp = int.from_bytes(m.read((kseg << 4) + V("fm_vp"), 2), "little")
            vseg = 0
            if vp:
                vseg = int.from_bytes(
                    m.read((kseg << 4) + vp + V("FS_VSEG"), 2), "little")
            if vseg:
                return list(m.read((vseg << 4) + n * V("DSK_DE_STRIDE"), n))
            # **AND THERE IS NO FLOOR TO FALL BACK TO** (SPEC.md 22.6.3).
            # This arm read `LOW_SEG:dsk_icoix`, and that symbol no longer
            # exists - so the line it was meant to make readable raises
            # KeyError instead, at exactly the moment the row has something
            # to report. A window with no store gets a QUIET mount and
            # harvests nothing, which is a fact worth naming rather than a
            # case worth decoding.
            fail("the acting Disk window holds no FS_VSEG claim, so there is "
                 "no listing anywhere to read a reference index out of - a "
                 "window with no store gets a QUIET mount and harvests no "
                 "icons at all (SPEC.md 22.6)")

        if rows() != 0:
            fail("the store already holds %d row(s) before any listing - it is "
                 "claimed LAZILY (SPEC.md 25.9), so a fresh desktop must have "
                 "none" % rows())

        ui.path("B:/APPS")
        os88marty.settle(m)
        b_rows, b_refs = rows(), refs()
        used = [r for r in b_refs if r not in (ICO_R_FOLDER, ICO_R_NONE)]
        folders = [r for r in b_refs if r == ICO_R_FOLDER]
        print("icostore: B:/APPS  rows=%d  refs %s"
              % (b_rows, " ".join("%02X" % r for r in b_refs[:16])))

        if not used:
            fail("B:/APPS iconned NOTHING - every reference is a folder or the "
                 "generic icon, so the harvest stored no body at all")
        if len(set(used)) != len(used):
            dup = sorted({r for r in used if used.count(r) > 1})
            fail("two entries share body %s, and only a FOLDER may share one "
                 "(SPEC.md 25.9). This is the shape of the defect the split "
                 "between dsk_put_icon_fld_x and dsk_put_icon_k_x fixed: the "
                 "per-entry paths reached the folder's SHARED allocator, so "
                 "every entry drew one picture" % dup)
        if not folders:
            fail("B:/APPS has no ICO_R_FOLDER reference, and its `..` is a "
                 "folder - so a folder took a ROW instead of the built-in "
                 "body it should reference for free")
        # THE COUNTS ARE NO LONGER ONE TO ONE, and this used to assert that
        # they were.  It was a proxy for "every package's icon is its own",
        # which assertion 1 tests directly; what made it hold was that the
        # harvest was the store's only source.  SPEC.md 54.7.4 gave it a
        # second: `asc_absorb` takes the whole VOLUME's bodies out of
        # ASSOC.DAT at the mount, so a folder listing thirteen packages sits
        # in a store of twenty-three and every one of those thirteen resolves
        # WITHOUT A SECTOR READ, which is the point. What is still binding is
        # that the store holds at least what the listing uses and that every
        # reference names a row that exists - a reference past [ico_n] is
        # SPEC.md 25.9's stale-reference case and draws 64 bytes of whatever
        # the heap handed out next.
        if b_rows < len(used):
            fail("%d row(s) stored for %d entry body/ies - a listing cannot "
                 "use more bodies than the store holds" % (b_rows, len(used)))
        bad = [r for r in used if r >= b_rows]
        if bad:
            fail("entry/ies reference row(s) %s of a %d-row store (SPEC.md "
                 "25.9): a row past [ico_n] is 64 bytes of freed heap drawn "
                 "as an icon" % (sorted(set(bad)), b_rows))
        print("icostore: ...%d of those rows came from somewhere other than "
              "this listing - the volume's ASSOC.DAT, absorbed at the mount "
              "(SPEC.md 54.7.4)" % (b_rows - len(used)))

        # --- 3: the SAME packages off the SYSTEM disk (SPEC.md 24.3) --------
        ui.open_drive("A")
        os88marty.settle(m)
        ui.path("A:/APPS")
        os88marty.settle(m)
        a_rows, a_refs = rows(), refs()
        a_used = [r for r in a_refs if r not in (ICO_R_FOLDER, ICO_R_NONE)]
        shared = [r for r in a_used if r < b_rows]
        print("icostore: A:/APPS  rows=%d  refs %s"
              % (a_rows, " ".join("%02X" % r for r in a_refs[:16])))
        print("icostore: the second volume added %d row(s) for %d body/ies, "
              "so %d resolved to rows the first volume stored"
              % (a_rows - b_rows, len(a_used), len(shared)))

        if not a_used:
            fail("A:/APPS iconned nothing")
        if not shared:
            fail("A:/APPS reused NONE of B:/APPS's %d rows, and SPEC.md 24.3 "
                 "ships the core packages on both disks. The (name, size) key "
                 "is what collapses them, so this is that key failing to "
                 "match across volumes" % b_rows)
        if a_rows - b_rows >= len(a_used):
            fail("the second volume stored %d new row(s) for %d body/ies - it "
                 "reused nothing, so every listing is paying for its own copy "
                 "again" % (a_rows - b_rows, len(a_used)))

        # --- 4: A SHED, and what a listing staged before it then draws ----
        # SPEC.md 50.6's shed zeroes the holder's word and frees the block;
        # the holder's existing refusal path is the whole notification
        # protocol.  So the danger is not the shed, it is the REFERENCES a
        # listing is still holding: row 12 of a store that no longer has
        # twelve rows is 64 bytes of whatever the heap handed out next.
        m.write((kseg << 4) + V("ico_seg"), b"\x00\x00")
        m.write((kseg << 4) + V("ico_n"), b"\x00")
        os88marty.settle(m)
        ui.path("A:/")
        os88marty.settle(m)
        after = rows()
        print("icostore: after a shed, a fresh listing refilled %d row(s)"
              % after)
        if after == 0:
            fail("the store stayed empty after a shed and a fresh mount - it "
                 "is claimed LAZILY, so the next listing that wants a body "
                 "must re-make it (SPEC.md 25.9)")
        ui.path("A:/APPS")
        os88marty.settle(m)
        refilled = [r for r in refs() if r not in (ICO_R_FOLDER, ICO_R_NONE)]
        if not refilled:
            fail("A:/APPS iconned nothing after the shed, so the refill does "
                 "not work and every icon is lost for the session")
        print("icostore: ...and A:/APPS iconned %d entry/ies again"
              % len(refilled))

    print("icostore: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
