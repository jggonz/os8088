#!/usr/bin/env python3
"""Clear Skies' worlds, each assembled on its own and packed (SPEC.md 88.10.5).

    python3 tools/csworlds.py --out build

A world is data the renderer walks with DS - `cs_scene` reads `[si +
CSO_MODEL]` and then `[di + CSM_TYPE]`, at 169 ms a frame - so it cannot be
addressed through a segment. It is copied into an OVERLAY in the program's bss
instead, at a fixed org, and every near pointer inside it is already right.

This builds each world through `apps/skies/cswone.asm`, which lays the shared
vocabulary at CS_WLD_ORG and the world at CS_WLD_ORG + CS_VOCAB_MAX - the same
two addresses the overlay uses - then KEEPS ONLY WHAT FOLLOWS the vocabulary
and packs it. The vocabulary is packed once, on its own.

What it also writes is `cswidx.inc`, the RESIDENT index: nine locations, each
with the world it stands in, the offset of its location record inside that
world, and its name. The names have to be resident because the launcher lists
all nine before any world is loaded; they are COPIED OUT of the world blobs
here rather than typed a second time, so there is nothing to keep in step.

CS_DEFPORT is DERIVED, and that is a fix rather than a tidy-up: skies.asm
carried it as a hand-kept 5 under a comment warning that moving Paris down the
list would silently change which runway a fresh instance opens on. It is the
index of PARIS-ISSY, worked out here.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88lz                                                  # noqa: E402
from os88pkg import APP_MAX_SIZE                              # noqa: E402

# THE OVERLAY'S FOUR NUMBERS, and this is the ONLY place they are written.
# skies.asm gets them out of the cswidx.inc emitted below and cswone.asm off
# the command line, so there is no mirror to hold in step - which matters more
# than usual here: a world laid at one address and read at another is a world
# of wild pointers, and nothing would fault.
CS_VOCAB_MAX = 576              # the shared vocabulary's room in the overlay...
CS_WLD_MAX = 2560               # ...and the picked world's

# WHERE THE OVERLAY BEGINS, AND IT IS DERIVED (SPEC.md 88.10.6). It sits at the
# TOP of the segment, so this is APP_MAX_SIZE less what it holds and there is no
# number here for anyone to tune, get right, or leave behind.
#
# IT USED TO BE HAND-SET, and that is what went wrong. `image + bss` is
# CS_VOCAB_AT plus the overlay - a CONSTANT, whatever the program's own size -
# so this address IS the heap claim (SPEC.md 88.4.5.5 says so), and the gap
# below it is the growth headroom for the image and the ZWORD chain TOGETHER.
# Set by hand it tracked nothing: it went 0xB400 -> 0xBE00 when the end tables
# landed and then stayed there while 88.10.2, 88.10.3, 88.10.4 and 88.10.5 took
# the art, the reader, the body and the nine worlds OUT of the image - so the
# program got smaller and its growth room did not get bigger. The gap reached
# 208 bytes, the -DCSPROBE build stopped ASSEMBLING (which nothing in `all`
# could catch), and a `--vocab-at 0xC200` knob had been bolted on for the diag
# trees to buy one rung of it back.
#
# THE PROJECT HAD ALREADY HAD THIS ARGUMENT AND SETTLED IT THE OTHER WAY:
# SPEC.md 88.4.5.5's end tables were refused against the gap, correctly on the
# arithmetic and wrongly on the question, "before anybody asked what the gap was
# actually protecting". It protects nothing. The bss ships inside the part as a
# run of zeros that LZ4 all but deletes, so the floppy does not notice; SKIES is
# in SMALLOMIT_GAMES, so the 128 KB machine never loads it; and the claim this
# makes - 61,440 - is 340 bytes above the 61,100 Clear Skies ALREADY SHIPPED AT
# before 88.10.3. A kern_big desktop has run this program at this size.
#
# AND IT IS DERIVED FROM THE PROGRAM, NOT FROM THE CEILING (88.10.6.1). The
# paragraph above is why the address must not be hand-set; it is not an
# argument for putting it at APP_MAX_SIZE, and for one cycle it was read as
# one. The two are not the same trade. A ceiling address is staleness-proof
# AND maximises the gap by construction - and the gap is not free: the claim
# is CS_VOCAB_AT plus the overlay, so every byte the address is raised by is
# claimed RAM no instruction reads, on every instance, for the life of the
# program. Measured at the ceiling it was 9,614 bytes.
#
# So the ceiling is the PROVISIONAL value now - pass 1's, which always
# assembles because nothing can be above it - and `--vocab-from` reads the
# program's real top off that pass and puts the overlay directly on it. The
# gap becomes the paragraph rounding and nothing else, and the claim tracks
# the program DOWN as well as up.
CS_VOCAB_CEIL = APP_MAX_SIZE - CS_VOCAB_MAX - CS_WLD_MAX
CS_VOCAB_AT = CS_VOCAB_CEIL
CS_WLD_AT = CS_VOCAB_AT + CS_VOCAB_MAX


def vocab_from_probe(path):
    """CS_VOCAB_AT off a -DCS_SIZEPROBE object (SPEC.md 88.10.6.1).

    The probe is the IMAGE followed by one word of CS_BSS, so the program's
    top is `len - 2 + that word`. Rounded UP to a paragraph, because
    skies.asm hands the overlay to cs_wldget as `CS_VOCAB_AT / 16` - a
    paragraph count, so an address that is not a multiple of 16 would read
    the stream in at the wrong place and nothing would fault.
    """
    with open(path, "rb") as fh:
        d = fh.read()
    if len(d) < 3:
        sys.exit("csworlds: %s is not a size probe (%d bytes) - pass 1 did "
                 "not run, or it assembled something else" % (path, len(d)))
    image_end = len(d) - 2
    cs_bss = d[image_end] | (d[image_end + 1] << 8)
    top = (image_end + cs_bss + 15) & ~15
    if top > CS_VOCAB_CEIL:
        sys.exit("csworlds: Clear Skies no longer FITS ONE SEGMENT.\n"
                 "  image %d + bss %d = %d, rounded %d\n"
                 "  the overlay needs %d above it and APP_MAX_SIZE is %d,\n"
                 "  so the ceiling for this address is %d - over by %d.\n"
                 "THE ANSWER IS ANOTHER PART, NOT ANOTHER ADDRESS (SPEC.md "
                 "20.12): there is nowhere left to raise it to."
                 % (image_end, cs_bss, image_end + cs_bss, top,
                    CS_VOCAB_MAX + CS_WLD_MAX, APP_MAX_SIZE,
                    CS_VOCAB_CEIL, top - CS_VOCAB_CEIL))
    return top, image_end, cs_bss

# The nine locations, IN THE DROP-DOWN'S ORDER (sorted by name, which is what
# skies.asm's cs_ports has always been). Paris carries two.
LOCATIONS = [
    ("spx",  "csw_spx"),   ("lcy",  "csw_lcy"),  ("mia", "csw_mia"),
    ("vnlk", "csw_vnlk"),  ("jfk",  "csw_jfk"),  ("issy", "csw_paris"),
    ("lbg",  "csw_paris"), ("sdu",  "csw_sdu"),  ("sfo", "csw_sfo"),
]
WORLDS = []
for _, w in LOCATIONS:
    if w not in WORLDS:
        WORLDS.append(w)


def assemble(world, out):
    """(bytes of the world alone, {symbol: offset within it})."""
    # PER-PROCESS SCRATCH NAMES. `make` runs this once, but world_map() below
    # hands the same routine to the suite, where several rows assemble worlds
    # into build/ at the same time - and two of them sharing _csw.lst is one
    # row reading the other's listing, which is a map that is plausible and
    # wrong rather than an error.
    b = os.path.join(out, "_csw_%d_%s.bin" % (os.getpid(), world))
    l = os.path.join(out, "_csw_%d_%s.lst" % (os.getpid(), world))
    subprocess.run(["nasm", "-f", "bin", "-w+error",
                    "-I", os.path.join(ROOT, "apps", "skies") + os.sep,
                    "-DCS_WLD_ORG=%d" % CS_VOCAB_AT,
                    "-DCS_VOCAB_MAX=%d" % CS_VOCAB_MAX,
                    '-DCSW_FILE="%s.inc"' % world, "-o", b, "-l", l,
                    os.path.join(ROOT, "apps", "skies", "cswone.asm")],
                   check=True)
    raw = open(b, "rb").read()
    # A label's address is the first EMITTING line at or after it: a label on a
    # line of its own puts no bytes down, so the listing gives it no column.
    syms, pend = {}, []
    for ln in open(l, errors="replace"):
        for m in re.finditer(r"(?:^|\s)([a-z_][\w]*):", ln.split(";")[0]):
            pend.append(m.group(1))
        m = re.match(r"\s*\d+\s+([0-9A-F]{8})\s", ln)
        if m and pend:
            at = int(m.group(1), 16)
            for n in pend:
                syms.setdefault(n, at)
            pend = []
    os.unlink(b)
    os.unlink(l)
    # The listing's addresses are FILE offsets, so a world symbol's offset
    # inside its own blob is its listing address less the vocabulary's room.
    # The listing's addresses are FILE offsets. A symbol below CS_VOCAB_MAX is
    # the VOCABULARY's and is exported as an absolute equ; one above it is the
    # world's own and is recorded as an offset inside its blob.
    voc = {k: v + CS_VOCAB_AT for k, v in syms.items() if v < CS_VOCAB_MAX}
    wld = {k: v - CS_VOCAB_MAX for k, v in syms.items() if v >= CS_VOCAB_MAX}
    return raw[CS_VOCAB_MAX:], wld, raw[:CS_VOCAB_MAX], voc


# WHICH WORLD EACH LOCATION STANDS IN, by its tag - the resident half of
# cswidx.inc, in Python. A test that names a world symbol needs it to turn
# "LCY" into "csw_lcy".
WORLD_OF = dict(LOCATIONS)

_WMAP = {}


def world_map(world):
    """{symbol: its address IN THE OVERLAY} for one world.

    Test-facing, and the only honest way to resolve a world symbol now: a
    world is a packed part read into an overlay at run time (SPEC.md
    88.10.5), so the program image carries none of its labels and
    dispapps._map('skies') cannot see them. cswone.asm is `org CS_VOCAB_AT`
    and lays the vocabulary and then the world exactly where cs_wldget will
    put them, so a map of it IS the guest's addresses - nothing to bias.

    `[map all]` and NOT assemble()'s listing parse, which is what the streams
    are cut with. A mesh is declared by a MACRO - `CS_PYR cs_m_lcy_shd, 28,
    306, 28` - and a listing renders the macro's body with `%1:` in it, so
    the label's own name never appears in the file at all and every mesh in
    every world is invisible to a reader of one. nasm's map has them because
    it is the assembler's own symbol table. It also has the pure constants,
    which is why it is not what cswidx.inc's vocabulary equs are cut from:
    there a stray CSM_STACK would become an absolute address.

    Cached per process: a row that names four symbols assembles once.
    """
    adopt_built()
    if world not in _WMAP:
        tag = "%s_%d" % (world, os.getpid())
        tmp = "/tmp/os88_csw_%s.asm" % tag
        mpf = "/tmp/os88_csw_%s.map" % tag
        binf = "/tmp/os88_csw_%s.bin" % tag
        src = os.path.join(ROOT, "apps", "skies", "cswone.asm")
        open(tmp, "w").write(open(src).read() + "\n[map all %s]\n" % mpf)
        r = subprocess.run(
            ["nasm", "-f", "bin", "-w+error",
             "-I", os.path.join(ROOT, "apps", "skies") + os.sep,
             "-DCS_WLD_ORG=%d" % CS_VOCAB_AT,
             "-DCS_VOCAB_MAX=%d" % CS_VOCAB_MAX,
             '-DCSW_FILE="%s.inc"' % world, "-o", binf, tmp],
            capture_output=True, text=True)
        if r.returncode:
            sys.exit("csworlds: could not map %s:\n%s" % (world, r.stderr[:400]))
        out = {}
        for line in open(mpf):
            p = line.split()            # "<vaddr> <raddr> <name>", HEX
            if len(p) == 3:
                try:
                    out[p[2]] = int(p[0], 16)
                except ValueError:
                    pass
        for f in (tmp, mpf, binf):
            try:
                os.unlink(f)
            except OSError:
                pass
        if "csw_body" not in out:
            sys.exit("csworlds: %s's map has no csw_body - the world did not "
                     "assemble the way this expects" % world)
        _WMAP[world] = out
    return _WMAP[world]


_OVL = {}


def overlay(world):
    """`build/skies.bin` with `world` laid into it, as the GUEST has it.

    Test-facing, and the companion to world_map(). A host-side reader that
    walks Clear Skies' object tables used to index straight into
    `build/skies.bin`, because the worlds were in it; they are parts now
    (SPEC.md 88.10.5) and the program image carries only the resident index.
    What it carries instead is the ROOM - `skies.bin` is image + bss and the
    overlay is the top of that bss - so laying the vocabulary and one world
    into their own addresses reconstructs the segment exactly.

    ONE WORLD AT A TIME, which is the machine's own constraint and not a
    convenience: a reader that wants all nine locations walks eight overlays.
    """
    if world not in _OVL:
        adopt_built()
        blob, _, vocab, _ = assemble(world, os.path.join(ROOT, "build"))
        img = bytearray(open(os.path.join(ROOT, "build", "skies.bin"),
                             "rb").read())
        if len(img) < CS_WLD_AT + len(blob):
            sys.exit("csworlds: build/skies.bin is %d bytes and the overlay "
                     "ends at %d - the program's bss no longer holds it, or "
                     "the build is stale"
                     % (len(img), CS_WLD_AT + len(blob)))
        img[CS_VOCAB_AT:CS_VOCAB_AT + len(vocab)] = vocab
        img[CS_WLD_AT:CS_WLD_AT + len(blob)] = blob
        _OVL[world] = bytes(img)
    return _OVL[world]


_ADOPTED = [False]


def adopt_built(out=None):
    """Take CS_VOCAB_AT from the tree that was BUILT, not from the default.

    The address is derived per tree now (SPEC.md 88.10.6.1), so a reader that
    recomputed it would be describing a different build of the program - and
    the failure is silent in the direction that matters: this module lays a
    world into skies.bin at CS_WLD_AT, and a world laid at one address and
    read at another is a world of wild pointers.

    So every LIBRARY path reads build/cswidx.inc, which is the file the image
    was assembled against. main() does not - it is what computes the value.
    """
    if _ADOPTED[0]:
        return
    global CS_VOCAB_AT, CS_WLD_AT
    inc = os.path.join(out or os.path.join(ROOT, "build"), "cswidx.inc")
    try:
        with open(inc) as fh:
            text = fh.read()
    except OSError:
        sys.exit("csworlds: no %s - the tree is not built, and the overlay's "
                 "address is a property of the build now (SPEC.md 88.10.6.1) "
                 "rather than a constant this file can supply" % inc)
    m = re.search(r"^CS_VOCAB_AT\s+equ\s+(0x[0-9A-Fa-f]+|\d+)", text, re.M)
    if not m:
        sys.exit("csworlds: %s declares no CS_VOCAB_AT" % inc)
    CS_VOCAB_AT = int(m.group(1), 0)
    CS_WLD_AT = CS_VOCAB_AT + CS_VOCAB_MAX
    _ADOPTED[0] = True


def cstr(blob, at):
    end = blob.index(b"\0", at)
    return blob[at:end].decode("ascii")


def main(argv):
    # STILL NO `--vocab-at`. There was one, so a diag tree could raise the
    # overlay above a shipped address that had stopped leaving room, and a
    # number anyone can tune is a number that goes stale. `--vocab-from` is
    # not that: it takes an OBJECT and reads the size out of it, so nothing
    # is chosen. With neither, the address is the ceiling - which is pass 1,
    # and is also what a tree that never runs pass 2 gets: correct, and
    # 9,614 bytes fatter.
    #
    # EVERY TREE DERIVES ITS OWN, which is the other half of what this fixes.
    # CSDIAG, CSPROBE and CSHZPROBE each build into a $(BUILD) of their own,
    # so each gets a cswidx.inc measured against ITS image - and -DCSPROBE,
    # which stopped assembling at all against a shipped address, now sizes
    # the overlay against itself.
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "build"))
    ap.add_argument("--vocab-from", metavar="PROBE",
                    help="a -DCS_SIZEPROBE object; the overlay goes directly "
                         "on top of the program it measures")
    a = ap.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)

    global CS_VOCAB_AT, CS_WLD_AT
    if a.vocab_from:
        CS_VOCAB_AT, image_end, cs_bss = vocab_from_probe(a.vocab_from)
        CS_WLD_AT = CS_VOCAB_AT + CS_VOCAB_MAX
        _ADOPTED[0] = True          # main COMPUTES the address; the library
                                    # doors read it back. Latch it so a call
                                    # into one of them from here cannot go and
                                    # read the cswidx.inc we are replacing
        print("csworlds: overlay at 0x%04X - image %d + bss %d = %d, "
              "gap %d, claim %d (ceiling would be %d)"
              % (CS_VOCAB_AT, image_end, cs_bss, image_end + cs_bss,
                 CS_VOCAB_AT - (image_end + cs_bss),
                 CS_VOCAB_AT + CS_VOCAB_MAX + CS_WLD_MAX, APP_MAX_SIZE))

    blobs, syms, vocab, vsyms = {}, {}, None, None
    for w in WORLDS:
        blobs[w], syms[w], v, vs = assemble(w, a.out)
        vocab = v if vocab is None else vocab
        vsyms = vs if vsyms is None else vsyms
        if v != vocab:
            sys.exit("csworlds: %s assembled a DIFFERENT vocabulary - the "
                     "shared tables must be identical in every world's build, "
                     "or a world's pointers into them are wrong" % w)

    # --- the packed streams -------------------------------------------------
    # THE FILES ARE NUMBERED, not named: cswN.z is directory row N, which is
    # what csload.asm hands the program and what the Makefile lists. A name
    # would be a second place the order lives, and the two would drift.
    tot_raw = tot_z = 0
    lens = {}
    for row, (name, blob) in enumerate(
            [("csvocab", vocab)] + [(w, blobs[w]) for w in WORLDS]):
        if len(blob) > (CS_VOCAB_MAX if name == "csvocab" else CS_WLD_MAX):
            sys.exit("csworlds: %s is %d bytes and the overlay holds %d - "
                     "raise CS_WLD_MAX here - it is declared in this file "
                     "alone and skies.asm reads the generated cswidx.inc"
                     % (name, len(blob), CS_VOCAB_MAX if name == "csvocab"
                        else CS_WLD_MAX))
        z = os88lz.compress(blob, os88lz.LZ4)
        assert os88lz.decompress(z, os88lz.LZ4, len(blob)) == blob, \
            "csworlds: %s does not expand to the bytes that made it" % name
        open(os.path.join(a.out, "csw%d.z" % row), "wb").write(z)
        lens[name] = (len(blob), len(z))
        tot_raw += len(blob)
        tot_z += len(z)
        print("csworlds: csw%d.z %-9s %5d -> %5d packed"
              % (row, name, len(blob), len(z)))

    # --- the resident index -------------------------------------------------
    lines = [
        "; GENERATED by tools/csworlds.py - do not edit by hand.",
        ";",
        "; The nine locations Clear Skies offers, and what the program needs to",
        "; know about each of them BEFORE any world is loaded (SPEC.md",
        "; 88.10.5): its name, which world it stands in, and where its location",
        "; record sits inside that world once the overlay holds it.",
        ";",
        "; ...and the SHARED VOCABULARY's own symbols, as absolute addresses in",
        "; the overlay. The program names seven of them - the runway's face and",
        "; edges, four solid shapes and one string - and the worlds name eleven;",
        "; both sides get them from here, which is the only place that knows",
        "; where the overlay put them.",
        "",
        "CS_VOCAB_AT  equ 0x%04X" % CS_VOCAB_AT,
        "CS_VOCAB_MAX equ %d" % CS_VOCAB_MAX,
        "CS_WLD_AT    equ CS_VOCAB_AT + CS_VOCAB_MAX",
        "CS_WLD_MAX   equ %d" % CS_WLD_MAX,
        "",
    ] + ["%-12s equ CS_VOCAB_AT + %d" % (k, v - CS_VOCAB_AT)
         for k, v in sorted(vsyms.items(), key=lambda kv: kv[1])] + [
        "",
        "; THE EXACT UNPACKED LENGTH of each stream, in directory order.",
        "; OSAPI_DECOMP is told what a stream expands to and CHECKS it (SPEC.md",
        "; 20.13.3), so the room the overlay has - CS_VOCAB_MAX, CS_WLD_MAX - is",
        "; the wrong number to hand it: it writes the bytes and then reports the",
        "; mismatch, which reads as a world that loaded and a load that failed.",
        "cs_wstrraw:  dw " + ", ".join(
            str(lens[n][0]) for n in ["csvocab"] + WORLDS),
        ""]
    ports, names, wof, strs = [], [], [], []
    defport = None
    for i, (loc, w) in enumerate(LOCATIONS):
        rec = syms[w].get("cs_a_" + loc)
        nam = syms[w].get("cs_s_" + loc)
        if rec is None or nam is None:
            sys.exit("csworlds: %s.inc defines no cs_a_%s / cs_s_%s"
                     % (w, loc, loc))
        ports.append("CS_WLD_AT + %d" % rec)
        names.append("cs_sn_" + loc)
        wof.append(str(WORLDS.index(w)))
        strs.append("cs_sn_%-6s db '%s', 0" % (loc + ":", cstr(blobs[w], nam)))
        if loc == "issy":
            defport = i
    lines += ["cs_ports:    dw " + ", ".join(ports[:5]),
              "             dw " + ", ".join(ports[5:]),
              "cs_apnames:  dw " + ", ".join(names[:5]),
              "             dw " + ", ".join(names[5:]),
              "cs_apwld:    db " + ", ".join(wof),
              "CS_NPORTS    equ %d" % len(LOCATIONS),
              "CS_DEFPORT   equ %d              ; PARIS-ISSY, where the "
              "simulator shipped" % defport,
              "CS_NWORLDS   equ %d" % len(WORLDS),
              ""] + strs + [""]
    idx = os.path.join(a.out, "cswidx.inc")
    open(idx, "w").write("\n".join(lines))
    print("csworlds: %d worlds, %d locations, %d -> %d packed; %s"
          % (len(WORLDS), len(LOCATIONS), tot_raw, tot_z, idx))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
