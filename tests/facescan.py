#!/usr/bin/env python3
"""ty_scan FINDS the machine's typefaces, on the machine (SPEC.md 19.8).

    make && make bench && python3 tests/facescan.py [--machine M]

THIS IS THE ONLY ROW THAT WALKS TO THE FACES. Everything else in the suite
that could was checking something adjacent: `t_fonts` says the files are in
`SYSTEM/FONTS` on every shipped image and that `apps/os88type.inc` spells the
same two components, which is the DISK end and the SOURCE end; `t_pkg` proves
each face's bytes against the artefact it was built from, by name, so the
folder can move and every one of its rows still passes. None of them runs
`ty_gofonts`. Between them there is exactly one thing left unasked and it is
the one that matters: does a package standing on the APPS floppy, told nothing
but `OSAPI_VOL_SYS`, actually arrive in that folder and come back with ten
families?

WHAT THE ANSWER LOOKS LIKE WHEN IT IS NO. `ty_scan` returns CF=1 and
`[ty_nfam]` = 0; the caller falls back to face 0, which is the kernel's own
8x8 cell and a perfectly good face; and the Font menu comes up one item long
with nothing anywhere saying why. A wrong path, a renamed folder, an installer
that dropped a nested directory and a `ty_dive` that matched `..` all produce
that same silence. It is also invisible in a screendump - `families 10` and
`families 1` are the same picture - which is why the assertion is bytes out of
the guest and not pixels.

FACETEST IS THE VEHICLE and it is on `build/bench.img`, not the apps disk:
nothing under tests/ ships (CLAUDE.md's Layout). That is the right shape here
rather than an inconvenience - the package is on B: and the faces are on A:,
so a scan that only ever worked relative to where the caller was standing
fails this row and would pass one driven from the system disk.

FOUR ASSERTIONS, and the last two are why the vehicle is facetest rather than
a listing read on the host:

  SCAN     [ft_nfam] is every family in faces/ - the count the tree built, not
           a number typed here. TY_MAXFAM is 10 and so is the shipped set, so
           a face added without raising it fails here rather than dropping the
           last family off a menu somebody has to notice.
  OPEN     [ft_err] is 0: ty_openfam went back down the same walk, found the
           file the scan had NAMED, and read it. The scan alone proves the
           directory listing; this proves the second descent, which is a
           separate code path with a separate bracket.
  FACE     the handle is not 0. Face 0 is the kernel's cell and is what a
           refusal leaves current, so "a face is open" and "a face off the
           disk is open" are different questions.
  ROWS     the specimen rows drew - the window has ink below its two header
           lines. A face that opened and composed nothing is the one failure
           the three counters above cannot see.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402
import os88geom                                             # noqa: E402
import dispapps                                             # noqa: E402
import os88build

ROOT = os.path.dirname(HERE)

ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_5150_cga_gla")
ap.add_argument("--sys", default=os88build.at("build/os8088-360.img"))
ap.add_argument("--bench", default=os88build.at("build/bench360.img"))
a = ap.parse_args()

for p in (a.sys, a.bench):
    if not os.path.exists(os.path.join(ROOT, p)):
        sys.exit("facescan: no %s - `make && make bench` first" % p)

# The families the tree was going to build, read off faces/ exactly as the
# Makefile's $(FACESRC) reads it - so a new face needs no edit here either.
WANT = sorted(p.stem.upper() for p in
              (__import__("pathlib").Path(ROOT) / "faces").glob("*.t88"))
fails = []


def say(s):
    print("  " + s)


def status(m, seg):
    """[ft_nfam], [ft_err], [ft_face], [ft_cached] out of facetest's own bss.

    The offsets come from nasm's own map of the package (dispapps.bss_off), so
    an edit to facetest.asm cannot leave this row reading the wrong four bytes
    and calling the result a scan failure.
    """
    base = (seg << 4) + dispapps.img_size("facetest")
    out = {}
    for n in ("ft_nfam", "ft_err", "ft_face", "ft_cached"):
        out[n] = m.read(base + dispapps.bss_off("facetest", n), 1)[0]
    return out


TY_FACE_KB = 8                  # apps/os88type.inc; t_mirror does not reach a
                                # package's own equs, so it is named here with
                                # the file it comes from


def mem_claims(m):
    """(base, paragraphs, owner) for every live heap claim."""
    raw = m.read(m.sym("mem_tab"), 32 * os88geom.MC_SIZE)
    out = []
    for i in range(32):
        r = raw[i * os88geom.MC_SIZE:(i + 1) * os88geom.MC_SIZE]
        b = r[0] | r[1] << 8
        if b:
            out.append((b, r[2] | r[3] << 8, r[4] | r[5] << 8))
    return out


with os88ui.boot(a.sys, apps=a.bench, machine=a.machine) as ui:
    m = ui.m
    ui.path("B:/FACETEST.O88")
    os88marty.settle(m)

    w = ui.front()
    say("window %r" % (w,))
    raw = m.read(m.sym("wm_wins"), os88geom.MAX_WIN * os88geom.WIN_SIZE)
    seg = int.from_bytes(
        raw[w.i * os88geom.WIN_SIZE + os88geom.W_SEG:
            w.i * os88geom.WIN_SIZE + os88geom.W_SEG + 2], "little")
    if not seg:
        sys.exit("facescan: the front window is not a package's - FACETEST "
                 "did not launch, and nothing below would mean anything")

    st = status(m, seg)
    say("ft_nfam=%(ft_nfam)d ft_err=%(ft_err)d ft_face=%(ft_face)d "
        "ft_cached=%(ft_cached)d" % st)

    # --- SCAN -----------------------------------------------------------
    if st["ft_nfam"] != len(WANT):
        fails.append(
            "SCAN: ty_scan listed %d families, faces/ holds %d (%s). 0 means "
            "the walk did not arrive: SYSTEM/FONTS is not where ty_gofonts "
            "looks, or the system volume could not be reached at all. A count "
            "SHORT of the tree's is TY_MAXFAM (os88type.inc) being smaller "
            "than the shipped set - the scan stops there and the last "
            "families are simply absent from every Font menu"
            % (st["ft_nfam"], len(WANT), ", ".join(WANT)))

    # --- OPEN -----------------------------------------------------------
    if st["ft_err"] == 0xFF:
        fails.append("OPEN: the scan found nothing, so ty_openfam was never "
                     "asked - see SCAN above")
    elif st["ft_err"] != 0:
        fails.append(
            "OPEN: ty_openfam answered TYE_%d. It is a SECOND descent to the "
            "same folder with its own bracket (os88type.inc), so the scan "
            "passing and this failing means the walk is right and the read is "
            "not: a face whose header ty_hdrchk refuses, or a claim it could "
            "not take" % st["ft_err"])

    # --- FACE -----------------------------------------------------------
    if st["ft_err"] == 0 and st["ft_face"] == 0:
        fails.append(
            "FACE: handle 0 is face 0 - the KERNEL's 8x8 cell (SPEC.md 6.5), "
            "which is what a refusal leaves current. ty_open answered success "
            "and handed back the slot that was never allocated")

    # --- CLAIM ----------------------------------------------------------
    # The face's own 8KB block, and the two things about it that were assumed
    # for a year (docs/plans/HEAP-UNPIN-PLAN.md 3.1): that the base needs
    # rounding to a 512-byte boundary by hand, and that a spare KB has to be
    # claimed to pay for the rounding. Neither is true - kernel.asm's guard 6b
    # asserts every claim base is HEAP_SEG + n*MEM_PARA_KB paragraphs and that
    # MEM_PARA_KB is a multiple of 32 - and a package cannot see that guard,
    # so os88type.inc ASSERTS the alignment now and this reads the assertion's
    # answer from outside. If the guard ever stopped holding, the face would
    # refuse (ft_err = TYE_NOMEM) instead of writing 496 bytes past its claim.
    if st["ft_err"] == 0:
        claims = mem_claims(m)
        cl = [c for c in claims if c[2] == seg]
        say("facetest holds %s"
            % ["%04x/%dKB" % (b, p // 64) for b, p, _ in cl])
        faces = [c for c in cl if c[1] // 64 == TY_FACE_KB]
        if not faces:
            fails.append(
                "CLAIM: no %dKB claim owned by FACETEST. It was TY_FACE_KB+1 "
                "until the hand rounding came out, and the extra KB was never "
                "consumed by it - one kilobyte per open face, up to three per "
                "Word or CWORD instance" % TY_FACE_KB)
        for b, p, _ in faces:
            if b & 0x1F:
                fails.append(
                    "CLAIM: the face claim at %04x is not a multiple of 32 "
                    "paragraphs, so it is not 512-byte aligned - guard 6b no "
                    "longer holds and os88type.inc's alignment test should "
                    "have refused the face rather than opening it" % b)

    # --- ROWS -----------------------------------------------------------
    # facetest draws two header lines and then four specimen rows, the first
    # of them 24px below the content top. Ink below that band is the only
    # evidence that a face which opened also COMPOSED.
    cx, cy = w.content[0], w.content[1]
    pw, ph, rgb = m.fbuf()
    ink = 0
    for y in range(cy + 24, min(cy + 24 + 4 * 18, w.y + w.h - 2)):
        for x in range(cx + 1, min(cx + w.w - 4, pw)):
            i = (y * pw + x) * 3
            if rgb[i] + rgb[i + 1] + rgb[i + 2] < 384:
                ink += 1
    say("specimen band: %d dark pixels" % ink)
    if ink < 100:
        fails.append(
            "ROWS: %d dark pixels in the four specimen rows. A face that "
            "opened and set no type is the one failure the counters cannot "
            "see - ty_use, ty_cache and the compose loops are all downstream "
            "of the walk this row is about" % ink)

if fails:
    print("\nFAILED:\n  " + "\n  ".join(fails))
    sys.exit(1)
print("\nfacescan: ty_scan found %d families in SYSTEM/FONTS and set type in "
      "one, on %s" % (len(WANT), a.machine))
