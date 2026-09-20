#!/usr/bin/env python3
"""Symbol offsets for the DOS box and for a probe running INSIDE it.

    import dosmap
    dm = dosmap.package()                   # DOS.O88's own near offsets
    dm = dosmap.package("DOSNET_CARD")      # ...plus a knob, if the disk has one
    pm = dosmap.probe()                     # DOSPKT.COM's, org 100h

WHY NOT `dispapps._map`. That one takes `defines` and means exactly ONE thing
by them - `-DAPP_SMALL` - so it compares the result against
`build/smallapp/<app>.o88` and exits naming a file a knob build never writes.
And its source path is `apps/<app>/<app>.asm` or `tests/<app>/<app>.asm`,
which `tests/dostrap/dospkt.asm` is neither. Two small differences, both of
which fail as a message about the wrong subject.

**THE PROBE'S OFFSETS ARE FROM THE PSP**, which needs saying because there are
two plausible bases and one of them reads plausible rubbish. A `.COM` is
loaded at PSP:0100 with CS = DS = the PSP, and the file is assembled `org
0x100` - so a map value already carries the 0x100 and the base is the PSP
itself, not PSP+0x10. With the segment biased by a paragraph-of-0x100 the
first reading of this was `lastflags=0000` for a program that had recorded a
SYN|ACK, which points at the box and is a bug in the reader.
"""
import io
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_CACHE = {}


def _map(src, defines, incs):
    key = (src, tuple(defines))
    if key in _CACHE:
        return _CACHE[key]
    d = tempfile.mkdtemp(prefix="os88dosmap")
    a, mp = os.path.join(d, "x.asm"), os.path.join(d, "x.map")
    open(a, "w").write(open(src).read() + "\n[map all %s]\n" % mp)
    args = ["nasm", "-f", "bin", "-w+error", "-o", os.path.join(d, "x.bin")]
    for i in incs:
        args += ["-I", os.path.join(ROOT, i) + os.sep]
    args += ["-D" + x for x in defines] + [a]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        sys.exit("dosmap: could not map %s:\n%s" % (src, r.stderr[:400]))
    out = {}
    # **TWO SHAPES, AND THE SECOND ONE IS THE CLAIM.** Inside a section nasm
    # writes "<real> <virtual> <name>"; under its "---- No Section ----"
    # heading it writes "<value> <name>" for every ABSOLUTE equate - which is
    # where PKB_STATE, DNB_LSN, dn_lsn and DOS_ENVBUF live, because an offset
    # into a heap CLAIM is a plain number and not an address in this image.
    # A reader that took only the three-field lines could see dos_pkt_bseg
    # (`equ os88_image_end + N`, relocatable) and not the offset to apply to
    # what it holds, which left a test hardcoding the layout - the one thing
    # DOS_TRACE_SZ's own comment says not to do.
    nosect = False
    for line in open(mp):
        if line.startswith("---- "):
            nosect = line.startswith("---- No Section")
            continue
        p = line.split()                # "<vaddr> <raddr> <name>", HEX
        if len(p) == 3:
            try:
                out[p[2]] = int(p[0], 16)
            except ValueError:
                pass
        elif nosect and len(p) == 2:
            try:
                out.setdefault(p[1], int(p[0], 16))
            except ValueError:
                pass
    for f in (a, mp):
        try:
            os.unlink(f)
        except OSError:
            pass
    if "os88_image_end" not in out and "start" not in out:
        sys.exit("dosmap: %s's map looks empty" % src)
    _CACHE[key] = out
    return out


# **THE SHIPPED BOX IS TWO DEFINES AND NOT NONE** (SPEC.md 96.44.5, 96.40.3).
# `$(SYSROOT)` is the PARTED package now, so the DOS.O88 in APPS/ of every
# system disk is `apps/dos/dos.asm` assembled `-DDOSKPART -DDOS_EXTCORE` - and
# the second define moves EVERY offset in the map: the core comes out of the
# image and the reservation goes in, so `os88_image_end` and every bss cell
# hanging off it shift by about 1,800 bytes.
#
# **SO IT IS THE DEFAULT AND NOT AN OPT-IN.** It was a constant three rows
# passed by hand while `package()` with no arguments meant the plain box, and
# the moment the shipped package changed that default became a map of
# something no disk carries - which does not fail, it reads a rect of rubble
# and CLICKS AT A COORDINATE THAT DOES NOT EXIST, hanging in `os88mouse`
# waiting for a cursor that can never arrive, or (`dosmedia`, which is how
# this was caught) reports `the box is on volume 16`. A caller with a knob of
# its own passes it ON TOP: `package("DOSNET_CARD")` is the shipped box plus
# that, which is what `make DOSNETCARD=1` builds.
SHIPPED = ("DOSKPART", "DOS_EXTCORE")


def package(*defines, shipped=True):
    """Every label and equate in apps/dos/dos.asm, as a near offset.

    Packages are assembled at org 0 and never relocated (SPEC.md 20), so the
    map value IS the offset inside the instance's segment - bss included,
    since `dos_pkt_xl` and friends are `equ os88_image_end + N`.

    `SHIPPED` is in the define set unless `shipped=False`: see the block
    above. **Pass `shipped=False` only to ask about the box as ONE image** -
    `tools/os88doscost.py` does, because its whole subject is splitting the
    core from the window and `-DDOS_EXTCORE` has already taken the core's
    image and bss out. Nothing on a disk is built that way, so a row that
    reads a live machine never wants it.
    """
    base = list(SHIPPED) if shipped else []
    want = base + [d for d in defines if d not in base]
    return _map(os.path.join(ROOT, "apps", "dos", "dos.asm"), want,
                ("apps", os.path.join("apps", "dos"),
                 os.path.join("drivers", "net"),
                 "kerndos"))          # -DDOSKPART's launch block (96.40)


def rect(m, pseg, dm, name):
    """The four words at `name` in a live DOS instance, as x1, y1, x2, y2.

    **A CONTROL IS RESOLVED OUT OF THE GUEST, never recomputed here.** Every
    rect this box draws - the bar's two buttons, the setup area's four, the
    memory check box - is four words in the package's bss, and an os88line
    block starts with LN_X1..LN_Y2, which is the same four. So one reader
    covers both and a test never carries a copy of the layout.

    docs/WRITING-TESTS.md names the alternative as one of the four failures
    that keep coming back, and SPEC.md 96.32 is what made it bite: the
    arguments box and Save Shortcut moved to a page of their own, and the two
    rows that clicked them at `content + DOS_BTNY + 7` went on clicking an
    empty part of the window.
    """
    at = (pseg << 4) + dm[name]
    return [int.from_bytes(m.read(at + i * 2, 2), "little") for i in range(4)]


def instance(m, slot=0):
    """The SEGMENT of a live DOS instance, which is what rect() wants.

    A package is loaded at a paragraph boundary and never relocated (SPEC.md
    20), so the map value IS the offset inside this segment - no image size to
    add and nothing to subtract.

    **ASK AT THE POINT OF USE, AND NEVER BANK IT ACROSS A DELAY** (SPEC.md
    66.6.1.2). "Never relocated" is about the package's own near offsets and
    not about where its segment IS: the DOS box's region MOVES under the
    compactor, at every arena claim, since the re-homed carve was unpinned.
    A base taken earlier names the bytes the package used to occupy - and a
    heap block the compactor copied DOWN is not scrubbed, so a stale base
    decodes as plausible rubbish rather than as an error.

    Four harnesses banked it in `__init__` and read across whole sessions;
    `tests/doslnk.py` banked it across a `dosmap.package()` call, which shells
    out to nasm for seconds of host time with the guest free-running, and read
    nine bytes of machine code out of `[dos_path]`. All five resolve per
    access now. It is one window-record lookup - cheap beside the debug round
    trip every read already costs.
    """
    import dispapps
    g = dispapps.pkg_seg(m, slot)
    if not g:
        raise RuntimeError("dosmap.instance: no package window in slot %d" % slot)
    return g[1]


def centre(m, pseg, dm, name):
    """...and the middle of it, which is what a click wants."""
    x1, y1, x2, y2 = rect(m, pseg, dm, name)
    return (x1 + x2) // 2, (y1 + y2) // 2


def sdk_const(name, default=None):
    """One `equ` out of the SDK (`apps/os88api.inc`), by name.

    `kd_const`'s twin and for its reason - SCRAPED rather than mirrored, so a
    row asserting a published constant asserts the number the SDK actually
    carries. The two files are different trees, which is the whole point: the
    kernel's `DRVM_CEIL_DISK` and the SDK's are a MIRRORED PAIR that
    `t_mirror` keeps level, and a row that wants to know what a package would
    see must read the package's copy.
    """
    pat = re.compile(r"^\s*%s\s+equ\s+([0-9]+)\s*(?:;.*)?$" % re.escape(name),
                     re.M)
    m = pat.search(io.open(os.path.join(ROOT, "apps", "os88api.inc"),
                           encoding="utf-8", errors="replace").read())
    if m:
        return int(m.group(1))
    if default is not None:
        return default
    raise RuntimeError("dosmap.sdk_const: apps/os88api.inc has no plain "
                       "`%s equ <number>` - an %%ifdef or a computed value is "
                       "out of this reader's reach, and a wrong number is "
                       "worse than a refusal" % name)


def kd_const(name, default=None):
    """One `equ` out of `kerndos/`, by name.

    **SCRAPED RATHER THAN MIRRORED.** A row that needs `KD_RAH_KEEP` needs the
    number the kernel was BUILT with, and a copy of it in Python is a second
    definition that `tests/unit/t_mirror.py` would have to police and that
    goes stale silently in between - which is exactly how `kdarena` and
    `kdbigexe` came to assert a ladder that shed to nothing after SPEC.md
    96.44.11.4 made it stop at a width.

    It is a regex over the source and it says so: an `equ` behind an `%ifdef`,
    or one computed from another, is out of its reach. Those are worth a
    refusal rather than a wrong number, so an unparsable value raises unless
    the caller passed a `default`.
    """
    pat = re.compile(r"^\s*%s\s+equ\s+([0-9]+)\s*(?:;.*)?$" % re.escape(name),
                     re.M)
    for fn in sorted(os.listdir(os.path.join(ROOT, "kerndos"))):
        if not fn.endswith((".inc", ".asm")):
            continue
        m = pat.search(io.open(os.path.join(ROOT, "kerndos", fn),
                               encoding="utf-8", errors="replace").read())
        if m:
            return int(m.group(1))
    if default is not None:
        return default
    raise RuntimeError("no plain `%s equ <number>` under kerndos/ - it has "
                       "moved, been made conditional, or been computed from "
                       "something else, and a row that guessed would assert "
                       "the wrong ladder" % name)


def probe(name="dospkt"):
    """Every label in one of tests/dostrap/'s `.COM` probes, from the PSP."""
    return _map(os.path.join(ROOT, "tests", "dostrap", name + ".asm"), (),
                (os.path.join("tests", "dostrap"),))
