#!/usr/bin/env python3
"""os8088 BOOTS from the hard disk it was installed to (SPEC.md 2.9.9, 52.10).

    make && python3 tests/hdboot.py

**Nothing in this tree ever booted a hard disk, and that is how SPEC.md 2.9's
blob shipped with `boot/boothd.asm` still loading the image the old way.**
`tests/instdeep.py` drives a real install and reads the partition back on the
HOST - it proves the bytes ARRIVE and never that they RUN. Every other boot row
boots a floppy, and the floppy sector is a different 512 bytes with a different
loader in it.

What that cost: the volume boot record had two addresses pinned into it, and
2.9.4 moved both. It loaded the file from sector 0 to KERNEL_SEG and jumped to
offset 0 - which is stage 2, not `.text` - and ticked the bar through
KERNEL_SEG:0008, which is now the middle of an instruction. The machine printed
`Disk error`, from boot/boot2.asm, on a boot that never touched a floppy.

WHAT IT ASSERTS, and the second is the one that would have caught the
half-fixed version:

 1. the machine reaches a DESKTOP from drive C: - menu bar, and drive icons
    down the right-hand edge;
 2. the LOADING SCREEN came up on the way - `[spl_live]` was raised at some
    point during the boot. It is the one thing a shot of the finished desktop
    cannot show, and SPEC.md 2.9.9.1 is why it needs asserting: the volume boot
    record can be right, the blob aboard and `[spl_fseg]` published, and the
    screen still never appear, because `spl_step` used to be a no-op until
    somebody else had started it and stage 2 was the only somebody there was;
 3. ...and the CLOCK IS DRAWN IN FULL. That is the boot overlay's own signature:
    `clk_init` is an OVLGATE, so a kernel that did not get the blob skips it
    along with cpu_detect, font_init, desk_init and the settings parse -
    and comes up looking almost right, with two glyphs where the date goes and
    no drivers configured. A boot that merely REACHES the desktop proves
    nothing here; the date string is what says the overlay ran.

It installs first, because a gate that depends on some other row having run is
not a gate. That costs a full install per run, which is why it is a soak row.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty as M                                       # noqa: E402
import os88sym                                              # noqa: E402
import instdeep                                             # noqa: E402

MACHINE = "os8088_xt_hdd"
BLANK = os.path.join(ROOT, "build", "hdboot-blank.img")
MIN_ICONS = 40              # rows of ink in the right-hand strip: the
                            # three drive zones are ~100, and a desktop
                            # that skipped desk_init has 0
CLOCK_X = 460               # the date starts well right of the last menu title
MIN_CLOCK_GROUPS = 8        # 'Jul 04 2026 00:00' is far more; two is the
                            # failure this row exists to name


def ink_groups(px, w, rows):
    """Runs of dark pixels across `rows`, as (x0, x1) - one per glyph or so."""
    lit = [x for x in range(w)
           if any(px[(r * w + x) * 3] < 128 for r in rows)]
    out = []
    for x in lit:
        if out and x - out[-1][1] <= 2:
            out[-1][1] = x
        else:
            out.append([x, x])
    return [tuple(v) for v in out]


def drive_rows(px, w, h):
    """How many rows of the desktop's right-hand strip carry ink.

    A COUNT rather than a count of icons, deliberately. The zones stack with a
    couple of rows between them, so on a 200-line screen they merge into one
    band and on a 480-line one they do not - and this row runs on whatever
    adapter the hard-disk machine has (SPEC.md 39 has three geometries). What
    it has to tell apart is a desktop with drive icons from a desktop with
    NONE, which is what desk_init being skipped looks like, and any threshold
    above zero does that on every adapter.
    """
    x0 = w - 64
    return sum(1 for y in range(16, h)
               if any(px[(y * w + x) * 3] < 128 for x in range(x0, w)))


def main():
    # ONE RUN TREE FOR BOTH HALVES, and it is the whole point of this row.
    #
    # The install and the boot are two MACHINES - the install needs the system
    # floppy in fd:0 and the boot needs a blank one, so that GLaBIOS offers its
    # menu and C is the hard disk - but they have to be two machines over ONE
    # VHD. This used to work by accident: every instance mounted the shared
    # master disk. Per-instance isolation (docs/MARTYPC-DEBUG.md) then gave
    # each its own clone and `close()` deletes a private one, so the install
    # landed in a directory that went away and the boot opened a fresh clone of
    # the pristine master. It found an empty disk and reported "no desktop from
    # drive C:" - which reads as a broken volume boot record, and cost a bisect
    # to place on a host-side commit that touched no kernel code at all
    # (docs/HANDOFF-SOAK-FINDINGS.md B1).
    #
    # `M.stage_run_dir` is a tree the CALLER owns: `launch(run_dir=...)` leaves
    # its media alone, so the VHD survives the first instance closing, and
    # `launch` re-clones the FLOPPY on every call, so the second machine gets
    # its blank one in the same directory.
    run_dir = M.stage_run_dir("hdboot")
    print("  installing (tests/instdeep.py's own installer, on this tree)...")
    with M.launch("build/os8088-360.img", apps="build/apps360.img",
                  machine=instdeep.MACHINE, run_dir=run_dir) as m:
        M.settle(m)
        instdeep.install(m)
        v = instdeep.partition(instdeep.vhd(m))
    tree = v.tree()
    missing = [p for p in instdeep.WANT_FILES if p not in tree]
    if missing:
        raise SystemExit("hdboot: the install itself failed (%s missing), so "
                         "nothing below would mean what it says"
                         % ", ".join(missing))
    print("  installed: FAT%d, %d paths" % (v.bits, len(tree)))

    # A floppy with no boot signature, so GLaBIOS offers its menu and C is the
    # hard disk. `launch` wants an image in fd:0 either way.
    if not os.path.exists(BLANK):
        with open(BLANK, "wb") as f:
            f.write(bytes(368640))

    lin_live = os88sym.linear("spl_live")
    splashed = False
    with M.launch(BLANK, machine=MACHINE, boot=0, run_dir=run_dir) as m:
        m.advance(frames=300)
        m.key("KeyC")                       # GLaBIOS: boot the hard disk
        for _ in range(160):
            m.advance(frames=30)            # finely enough to CATCH the splash:
            if m.read(lin_live, 1)[0]:      # it is up for under a second here
                splashed = True
            w, h, px = m.fbuf()
            bar = ink_groups(px, w, (4, 8))
            if len(bar) > 20:               # a drawn menu bar, not POST text
                break
        else:
            raise SystemExit(
                "hdboot: FAIL - no desktop from drive C:. SPEC.md 2.9.9 is the "
                "last time, and it printed `Disk error` from boot/boot2.asm "
                "because the volume boot record jumped into stage 2 instead "
                "of into `.text`.")

    if not splashed:
        raise SystemExit(
            "hdboot: FAIL - the desktop came up and [spl_live] was NEVER "
            "raised: this machine booted with no loading screen and no "
            "SPEC.md 15.6 status line. spl_step starts the splash itself when "
            "no loader has (2.9.9.1) - a hard-disk sector does not tick one - "
            "and that is what this catches.")

    clock = [g for g in bar if g[0] >= CLOCK_X]
    icons = drive_rows(px, w, h)
    print("  menu bar: %d ink groups, %d of them right of x=%d"
          % (len(bar), len(clock), CLOCK_X))

    if len(clock) < MIN_CLOCK_GROUPS:
        raise SystemExit(
            "hdboot: FAIL - the desktop is up and the CLOCK is %d ink groups "
            "wide. The boot overlay did not run: clk_init, font_init, "
            "desk_init and the settings parse are all OVLGATEs, so a kernel that "
            "did not get stage 2's blob skips every one of them and boots to a "
            "desktop that looks almost right with no clock and no configured "
            "drivers (SPEC.md 2.9.5.3)." % len(clock))
    if icons < MIN_ICONS:
        raise SystemExit(
            "hdboot: FAIL - %d rows of ink in the right-hand strip, expected "
            "at least %d: this desktop has NO DRIVE ICONS. desk_init is "
            "reached through the overlay too." % (icons, MIN_ICONS))
    print("  ok  loading screen seen, desktop from drive C:, clock drawn in "
          "full, %d rows of drive icons" % icons)


if __name__ == "__main__":
    main()
