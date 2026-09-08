#!/usr/bin/env python3
"""os88pkg: validate a .o88 v3 package and write it.

    python3 tools/os88pkg.py IN.bin -o OUT.o88 [--part P.bin ...]

IN.bin is the package assembled at org 0 (the only assembly there is).

**PARTS (SPEC.md 20.12).** `--part` appends a separately assembled binary
after the image, 512-aligned, and fills in the row the package reserved for
it in its OWN PART TABLE - which lives in the image, at a label the package
named, found here by an eight-byte magic. The kernel never sees that table:
all it learns is flags bit 2, which says the file is longer than the image on
purpose. Everything else is `apps/os88parts.inc`'s and the package's.

**v3 has no relocation table.** Since SPEC.md 20.1 a package owns a SEGMENT:
it links at zero, the loader puts it on a paragraph boundary and hands it its
own CS/DS, so nothing in the image depends on where it landed. That retires
the whole v2 machinery - the dual assembly at 0xB000/0xB800, the diff scan,
the byte-exact reconstruction check and the author rule about whole-word
package addresses - and with it the class of bug where an address folded into
a constant assembled cleanly and relocated wrong.

What is left is validation, which matters more than it used to: the header
now carries the three-byte DISPATCHER the kernel far-calls to reach any
callback (SPEC.md 20.2), and a package that does not carry it would send the
kernel into its data on the first paint. Any failure exits 1 with a message
on stderr; OUT.o88 is not written on failure.
"""
import argparse
import os
import struct
import sys

HEADER_SIZE = 32
MAGIC = 0x384F            # 'O','8' little-endian
VERSION = 3
ENTRY_MIN = 0x20          # first byte after the header
ENTRY_MIN_ICON = 0x60     # first byte after the embedded icon (flags bit 0)
ICON_END = 96             # header (32) + icon block (64)
ASSOC_SIZE = 16           # the association block (SPEC.md 54.6), flags bit 1
ASSOC_MAXN = 5            # ...holding a count byte and up to five extensions
APP_MAX_SIZE = 0xF000     # image + bss budget: 60KB (one segment's worth -
                          # the region is a heap claim, so the real limit is
                          # also whatever the heap has contiguous)
DISPATCH = bytes((0xFF, 0xD5, 0xCB))         # call bp / retf, at +12..+14
STK_CLASS_MAX = 4                            # ...and +15 is the STACK CLASS
                                             # (SPEC.md 8.7), an index into the
                                             # kernel's sch_clsbytes. It was the
                                             # dispatcher's PAD and this check
                                             # required it to be 0, which is
                                             # exactly why the field could be
                                             # given a meaning without moving
                                             # the header version - and exactly
                                             # why this line had to move with it
PARTS_MAGIC = b"O88PARTS"  # the part table's head, INSIDE the image (20.12.3)
PARTS_HDR = 10             # magic(8) + count(1) + reserved(1)
PART_ROW = 8               # kind(1) flags(1) off(2, 512-byte units)
                           # len(2) zkb(2)
PART_ALIGN = 512           # every part starts on a 512-byte boundary in the
                           # file - see the layout comment in main()
PK_SEG, PK_ASSET = 0, 1
OPF_XMS, OPF_ZERO, OPF_OPT, OPF_LAZY, OPF_COMP = 1, 2, 4, 8, 16
OPF_ALL = OPF_XMS | OPF_ZERO | OPF_OPT | OPF_LAZY | OPF_COMP
PART_FMT_AT = PARTS_HDR - 1     # the table's reserved byte, which is the
                                # FORMAT the OP_COMP rows use (SPEC.md
                                # 20.12.7). It is the package's own scope, so
                                # a compressed PART needs nothing of the
                                # header's flags and nothing of the kernel's


def fail(msg: str) -> None:
    print(f"os88pkg: error: {msg}", file=sys.stderr)
    sys.exit(1)


def parse_name(raw: bytes) -> str:
    """16 name bytes: printable ASCII, NUL padding, at least one char."""
    for b in raw:
        if b != 0 and not (0x20 <= b <= 0x7E):
            fail(f"name contains non-printable byte 0x{b:02X}")
    name = raw.split(b"\0", 1)[0]
    if not name:
        fail("name field is empty")
    if len(name) > 15:
        fail("name is 16 bytes with no NUL; the field is NUL-padded, max 15 chars")
    if any(b for b in raw[len(name):]):
        fail("name field has non-NUL bytes after the terminator")
    return name.decode("ascii")


def read_file(path: str) -> bytes:
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError as e:
        fail(f"cannot read {path}: {e}")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Validate an os8088 v3 package and write the .o88 file.")
    ap.add_argument("input", metavar="IN.bin",
                    help="flat binary assembled at org 0")
    ap.add_argument("-o", "--output", metavar="OUT.o88", required=True,
                    help="package file to write on success")
    ap.add_argument("--compress", metavar="FMT", nargs="?", const="lz4",
                    choices=("lz4", "lzb", "none"), default=None,
                    help="compress the image, unpacked by the loader "
                         "(docs/plans/O88-COMPRESSION-PLAN.md 7). The header, icon "
                         "and association block stay in the CLEAR - the mount "
                         "reads them from the first sector and must not have "
                         "to decompress to harvest an icon. `image` keeps "
                         "meaning the UNPACKED size, which is what the loader "
                         "sizes the region from; the file becomes shorter "
                         "than it, which flags bit 3 is what permits")
    ap.add_argument("--compress-if", metavar="FMT", dest="compress_if",
                    choices=("lz4", "lzb", "none"), default=None,
                    help="...the same thing, SOFTLY: compress when it pays "
                         "and is safe, and write the package PLAIN with a "
                         "line saying why when it does not. This is what a "
                         "FLEET-WIDE flag needs - `make PKGZ=lz4` compresses "
                         "two dozen packages and a few of them are legitimately "
                         "better off plain - where --compress above stays "
                         "strict, because there the refusal is the answer to a "
                         "question somebody asked")
    ap.add_argument("--part-compress", metavar="FMT", dest="part_compress",
                    choices=("lz4", "lzb"), default="lz4",
                    help="the format an OP_COMP part uses (SPEC.md 20.12.7), "
                         "default lz4. WHICH parts is the package's own "
                         "decision - the flag is on the row - and this only "
                         "says how. It is written into the part table's own "
                         "format byte, so a compressed PART is independent of "
                         "whether the IMAGE is compressed; the two cannot both "
                         "be, because a part's offset is measured from the "
                         "start of the file and lives inside the image")
    ap.add_argument("--part", metavar="PART.bin", action="append", default=[],
                    help="append a part after the image, 512-aligned, and "
                         "fill its row in the package's own part table. One "
                         "per FILE-BACKED row, in table order; a row with no "
                         "file bytes (OP_ZERO) takes none (SPEC.md 20.12)")
    args = ap.parse_args()

    a = read_file(args.input)
    if len(a) < HEADER_SIZE:
        fail(f"file is {len(a)} bytes; header alone is {HEADER_SIZE}")

    (magic, version, flags, link, entry, image, bss) = \
        struct.unpack_from("<HBBHHHH", a, 0)
    name = parse_name(a[16:32])

    if magic != MAGIC:
        fail(f"bad magic 0x{magic:04X} (want 0x{MAGIC:04X} 'O8')")
    if version != VERSION:
        fail(f"bad version {version} (want {VERSION}; rebuild against the "
             "v3 os88api.inc)")
    if flags & 0xF8:
        fail(f"flags 0x{flags:02X} has reserved bits set (bits 3-7 must be 0)")
    if link != 0:
        fail(f"bad link base 0x{link:04X}: a v3 package links at org 0")
    if a[12:15] != DISPATCH:
        fail("header offset 12 is not the dispatcher `call bp / retf` - "
             "the OS88_HEADER macro emits it, so this image was built "
             "against an older os88api.inc")
    if a[15] > STK_CLASS_MAX:
        fail(f"header offset 15 is stack class {a[15]}, and the kernel "
             f"publishes {STK_CLASS_MAX + 1} of them (0..{STK_CLASS_MAX}, "
             "SPEC.md 8.7). The kernel answers an unknown index with the "
             "largest class rather than refusing the launch, so this is a "
             "BUILD-time complaint about a package that means something by "
             "the byte - not a load-time one")
    if flags & 4:
        if image > len(a):
            fail(f"image size field {image} is past the file's {len(a)} bytes")
    elif image != len(a):
        fail(f"image size field {image} != actual file size {len(a)}")
    if args.part and not flags & 4:
        fail(f"{len(args.part)} --part argument(s) but flags bit 2 is clear - "
             "OS88_PARTS_BEGIN sets it, so this image has no part table "
             "(SPEC.md 20.12)")
    # The association block (SPEC.md 54.6) follows the icon, or the header when
    # there is none - so it moves where the code can start. This is the whole
    # of the format change and it needs NO version bump: everything an older
    # kernel reads is unmoved, and LD_H_ENTRY is absolute, so where the code
    # begins is told rather than derived.
    assoc_base = ICON_END if flags & 1 else HEADER_SIZE
    entry_min = ICON_END if flags & 1 else ENTRY_MIN
    if flags & 2:
        entry_min = assoc_base + ASSOC_SIZE
        if image < entry_min:
            fail(f"flags bit 1 set but the image is {image} bytes; the "
                 f"association block needs at least {entry_min}")
        n = a[assoc_base]
        if n > ASSOC_MAXN:
            fail(f"association block declares {n} extensions, at most "
                 f"{ASSOC_MAXN} fit")
        for i in range(n):
            ext = a[assoc_base + 1 + 3 * i:assoc_base + 4 + 3 * i]
            if any(not 0x20 <= b <= 0x7E for b in ext):
                fail(f"association {i}: extension bytes must be printable")
            if ext.decode("ascii") != ext.decode("ascii").upper():
                fail(f"association {i}: {ext!r} must be uppercase - the mount "
                     f"compares exactly (SPEC.md 19.1)")
            if ext == b"O88":
                fail("association: O88 cannot be declared - a package is "
                     "never opened through an association")
    if not (entry_min <= entry < image):
        fail(f"entry +0x{entry:04X} outside [0x{entry_min:04X}, "
             f"0x{image:04X})")
    if image + bss > APP_MAX_SIZE:
        fail(f"image {image} + bss {bss} = {image + bss} exceeds "
             f"budget 0x{APP_MAX_SIZE:04X} ({APP_MAX_SIZE})")
    if flags & 1 and image < ICON_END:
        fail(f"flags bit 0 set but image is {image} bytes; the embedded "
             f"icon needs at least {ICON_END}")

    out = bytearray(a)
    if args.compress and args.compress_if:
        fail("--compress and --compress-if are the same decision taken two "
             "ways; give one")
    if args.compress and args.compress != "none":
        out = compress_image(out, image, bss, flags, args.compress)
    elif args.compress_if and args.compress_if != "none":
        soft = compress_image(out, image, bss, flags, args.compress_if,
                              soft=True)
        if soft is not None:
            out = soft
    table, rows = find_parts(a, image, flags)
    if table is None:
        if args.part:
            fail("--part given but the image carries no 'O88PARTS' table - "
                 "bracket one with OS88_PARTS_BEGIN (SPEC.md 20.12.3)")
    else:
        lay_out_parts(out, table, rows, args.part, args.part_compress)

    try:
        with open(args.output, "wb") as f:
            f.write(out)
    except OSError as e:
        fail(f"cannot write {args.output}: {e}")

    flags = out[3]                          # compress_image may have set bits
    icon = "yes" if flags & 1 else "no"
    assoc = a[assoc_base] if flags & 2 else 0
    print(f"os88pkg: {name!r} entry=+0x{entry:04X} image={image} bss={bss} "
          f"icon={icon} assoc={assoc} -> {args.output}")
    if table is not None:
        report_parts(out, table, rows, args.part)
    return 0


# --- compression (docs/plans/O88-COMPRESSION-PLAN.md 7, 12.5) ----------------------
# The bytes the MOUNT reads stay in the clear. SPEC.md 18.3 step 4 harvests an
# icon out of a file's first sector and SPEC.md 54.6's association block sits
# beside it, so a compressed package still answers both without a decoder -
# which is what makes this change invisible to disk_mount, dsk_synth and the
# Disk window all at once.
PKG_COMP_BIT   = 0x08           # flags bit 3: the image is compressed
PKG_COMP_FMT   = 0x10           # flags bit 4: 0 = LZ4, 1 = LZB
                                # ...and NO MARGIN CONSTANT any more: the
                                # loader reads the file at roundup512(image -
                                # file) and the stream's own raw tail is what
                                # makes that enough (SPEC.md 20.13.7).
                                # os88lz.in_place_margin asserts it below


def roundup(v: int, n: int) -> int:
    return (v + n - 1) // n * n


def clear_prefix(flags: int) -> int:
    """bytes at the front that must NOT be compressed - everything the mount
    reads out of the first sector without opening the file."""
    n = ICON_END if flags & 1 else HEADER_SIZE
    if flags & 2:
        n += ASSOC_SIZE
    return n


def compress_image(out: bytearray, image: int, bss: int, flags: int,
                   fmt: str, soft: bool = False, packer=None):
    """Compress a package's image in place, or answer None.

    `soft` is what a FLEET-WIDE flag needs and `--compress` deliberately does
    not have: `make PKGZ=lz4` compresses two dozen packages and three of them
    are legitimately better off plain (parts, a file that grows, a layout that
    would make the loader claim more), so a refusal there has to be a line of
    output and not a stopped build. A per-package `--compress` stays STRICT,
    because there the refusal is the answer to a question somebody asked.

    `packer` swaps the ENCODER and nothing else - the refusals, the clear
    prefix, the flag bits and the in-place arithmetic all stay this function's.
    tests/lzcomp.py passes `os88lz.lzb_compress_machine` so that what the 8088
    is expected to write is derived from ONE statement of the rule rather than
    from a second copy of it in a test (SPEC.md 22.22.1).
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import os88lz

    def refuse(msg):
        if soft:
            print(f"os88pkg: NOT compressed: {msg}")
            return None
        fail(msg)

    if flags & 4:
        return refuse(
            "--compress with parts is not supported yet: a part's offset is "
            "measured from the start of the FILE and lives in a table INSIDE "
            "the image, so compressing the image and laying out its parts are "
            "circular. docs/plans/O88-COMPRESSION-PLAN.md wave 4 (OP_COMP) is where "
            "that is resolved; nothing in the tree needs both today")
    if flags & (PKG_COMP_BIT | PKG_COMP_FMT):
        return refuse(f"flags 0x{flags:02X} already has a compression bit set")
    if len(out) != image:
        fail(f"internal: file is {len(out)} bytes and image is {image}")

    fid = os88lz.LZ4 if fmt == "lz4" else os88lz.LZB
    pre = clear_prefix(flags)
    body = bytes(out[pre:])
    z = packer(body) if packer else os88lz.compress(body, fid)
    if z is None:
        return refuse("the packer made nothing smaller of this image")
    if os88lz.decompress(z, fid, len(body)) != body:
        fail(f"{fmt} did not round-trip this image - tools/os88lz.py is the "
             "reference the kernel copies, so this is a bug there, not here")

    packed = pre + len(z)
    if packed >= image:
        return refuse(
            f"{fmt} makes this image LARGER ({packed} vs {image} bytes). "
            "Ship it uncompressed: the loader would spend cycles to read "
            "more sectors, which is the trade upside down")
    margin = os88lz.in_place_margin(body, fid, packed=z)
    if margin:
        fail(f"{fmt} needs {margin} bytes of in-place margin - a stream with "
             "a tail needs none (SPEC.md 20.13.7), so os88lz.py's cut is "
             "wrong and this is a bug there")

    # THE IN-PLACE LAYOUT HAS TO FIT THE REGION THE LOADER WAS GOING TO CLAIM
    # ANYWAY, and this side is what guarantees it - which is the whole reason
    # SPEC.md 21 step 4 needs no change at all. The loader reads the file at
    # R = roundup512(image - file) so it can expand downwards; if that does
    # not fit, the answer is to ship this package UNCOMPRESSED rather than to
    # make every launch on the machine claim a bigger region.
    r = roundup(image - packed, 512)
    if r + packed > roundup(image + bss, 512):
        return refuse(
            f"{fmt} saves {image - packed} bytes but its in-place layout "
            f"needs {r + packed} of the {roundup(image + bss, 512)}-byte "
            "region, so the loader would have to claim more. Ship it "
            "uncompressed - a package this marginal was not going to save a "
            "sector anyway")

    new = bytearray(out[:pre]) + z
    new[3] = flags | PKG_COMP_BIT | (PKG_COMP_FMT if fid == os88lz.LZB else 0)
    print(f"os88pkg: compressed {fmt}: image {image} -> file {packed} bytes "
          f"({packed / image:.1%}), {pre} clear")
    return new


def image_unwrap(blob: bytes) -> bytes:
    """A shipped `.o88` back to the IMAGE the loader will run.

    THE FILE STOPPED BEING THE IMAGE when compression shipped (SPEC.md 20.13),
    and every host-side gate that reads `build/*.o88` has to say which of the
    two it means. A gate comparing what is on a floppy against what the build
    produced wants the FILE; one checking a size field, a layout or an
    assembly against its source wants the IMAGE, and this is how it gets it.
    Returns `blob` unchanged when the package is not compressed, so a caller
    never has to ask first.
    """
    if len(blob) < HEADER_SIZE:
        return blob
    flags = blob[3]
    if not flags & PKG_COMP_BIT:
        return blob
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import os88lz
    image = struct.unpack_from("<H", blob, 8)[0]
    pre = clear_prefix(flags & ~(PKG_COMP_BIT | PKG_COMP_FMT))
    fid = os88lz.LZB if flags & PKG_COMP_FMT else os88lz.LZ4
    out = bytearray(blob[:pre])
    out[3] = flags & ~(PKG_COMP_BIT | PKG_COMP_FMT)
    return bytes(out) + os88lz.decompress(blob[pre:], fid, image - pre)


# --- parts (SPEC.md 20.12) ---------------------------------------------------
def find_parts(a: bytes, image: int, flags: int):
    """Locate the package's own part table INSIDE the image.

    By a magic and not by a derived offset, so a package puts its table
    wherever it likes and there is no interaction with the icon or the
    association block. Exactly one occurrence, or this is not a table.
    """
    if not flags & 4:
        return None, 0
    hits = []
    at = a.find(PARTS_MAGIC, 0, image)
    while at != -1:
        hits.append(at)
        at = a.find(PARTS_MAGIC, at + 1, image)
    if not hits:
        fail("flags bit 2 is set but the image carries no 'O88PARTS' table. "
             "The flag and the table are one thing: the flag tells the KERNEL "
             "the file is longer than the image, and the table tells this "
             "tool where to write what it appends (SPEC.md 20.12)")
    if len(hits) > 1:
        fail(f"the image carries {len(hits)} 'O88PARTS' magics, at "
             f"{', '.join(hex(h) for h in hits)}. One package, one table - "
             "a second is either a copy or a literal that collided, and "
             "neither is safe to fill in")
    at = hits[0]
    n = a[at + PARTS_HDR - 2]
    if a[at + PART_FMT_AT] != 0:
        fail("the part table's format byte is not 0 - this tool writes it "
             "(SPEC.md 20.12.7) and a package lays it out as zero")
    if not 1 <= n <= 64:
        fail(f"the part table declares {n} parts; 1..64")
    end = at + PARTS_HDR + PART_ROW * n
    if end > image:
        fail(f"the part table runs to {end}, past the image's {image} bytes")
    return at, n


def compress_part(i: int, body: bytes, fmt: str):
    """(the stream, its length) for an OP_COMP row - or a hard refusal.

    STRICT, like --compress and unlike --compress-if: the flag is on a row the
    package's author wrote, so a part that does not pay is an error rather than
    a line of output. The two refusals are the image's (SPEC.md 20.13.4)
    measured against a PART: it has to get smaller, and it has to round-trip
    through the reference decoder. It expands in place with no margin, as
    every stream does (SPEC.md 20.13.7), and that is asserted rather than
    measured.
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import os88lz
    fid = os88lz.LZ4 if fmt == "lz4" else os88lz.LZB
    z = os88lz.compress(body, fid)
    if os88lz.decompress(z, fid, len(body)) != body:
        fail(f"part {i}: {fmt} did not round-trip - tools/os88lz.py is the "
             "reference the kernel copies, so this is a bug there, not here")
    if len(z) >= len(body):
        fail(f"part {i}: {fmt} makes it LARGER ({len(z)} vs {len(body)} "
             "bytes). Take OP_COMP off the row: the launch would spend cycles "
             "to read more sectors, which is the trade upside down")
    if os88lz.in_place_margin(body, fid, packed=z):
        fail(f"part {i}: the stream needs an in-place margin, which a stream "
             "with a tail cannot (SPEC.md 20.13.7) - a bug in os88lz.py")
    print(f"os88pkg: part {i} compressed {fmt}: {len(body)} -> {len(z)} bytes "
          f"({len(z) / len(body):.1%})")
    return z, len(z)


def lay_out_parts(out: bytearray, table: int, rows: int, parts,
                  part_fmt: str = "lz4") -> None:
    """Append each part 512-aligned and fill its row.

    THE ROWS AND THE PAYLOADS ARE NOT ONE LIST: a row with no file bytes
    (OP_ZERO) takes no --part, so the i'th --part is the i'th FILE-BACKED
    row and not the i'th row.
    """
    filed = []
    seen_xms = seen_lazy = seen_comp = False
    for i in range(rows):
        off = table + PARTS_HDR + PART_ROW * i
        kind, pflags, poff, plen, pzkb = struct.unpack_from("<BBHHH", out, off)
        if kind not in (PK_SEG, PK_ASSET):
            fail(f"part {i}: kind {kind}; 0 = SEGMENT, 1 = ASSET")
        if pflags & ~OPF_ALL:
            fail(f"part {i}: flags 0x{pflags:02X}; this toolchain writes "
                 "OP_XMS, OP_ZERO, OP_OPT and OP_LAZY (SPEC.md 20.12.4)")
        if pflags & OPF_XMS and kind != PK_ASSET:
            fail(f"part {i}: OP_XMS on a SEGMENT. Code has to be addressable "
                 "and extended memory is not (SPEC.md 41)")
        if (poff, plen) != (0, 0):
            fail(f"part {i}: off/len must be laid out as zeros - this tool "
                 "fills them in, because the assembler cannot know a "
                 "separately assembled module's length (SPEC.md 20.12.3)")
        if pflags & OPF_ZERO:
            if not pzkb:
                fail(f"part {i}: OP_ZERO with 0 KB is not a part")
        else:
            if pzkb:
                fail(f"part {i}: {pzkb} KB on a FILE-BACKED part. The carve is "
                     "contiguous - each part's bytes are followed by the next "
                     "part's - so a part that wants scratch beside it declares "
                     "a second, OP_ZERO part (SPEC.md 20.12.4)")
            if pflags & OPF_OPT:
                fail(f"part {i}: OP_OPT on a file-backed part. It sits inside "
                     "the carved run, and dropping it would split the one read "
                     "the carve exists to be (SPEC.md 20.12.4)")
        if pflags & OPF_LAZY:
            if pflags & OPF_ZERO:
                fail(f"part {i}: OP_LAZY on a scratch part. A scratch part is "
                     "worth declaring because it is SIZED before a sector is "
                     "read and refused before one is spent; a lazy part is "
                     "outside that sizing by definition, so all a lazy "
                     "scratch part would be is OSAPI_MEM_CLAIM with extra "
                     "steps - and its zkb word is where a fetched part banks "
                     "its segment (SPEC.md 20.12.4)")
            if pflags & OPF_OPT:
                fail(f"part {i}: OP_LAZY with OP_OPT. Lazy already means "
                     "`you may not get it` - op_fetch refuses and op_lazyok "
                     "answers in advance - so OP_OPT adds a second way to say "
                     "the same thing (SPEC.md 20.12.4)")
            if pflags & OPF_XMS:
                fail(f"part {i}: OP_LAZY with OP_XMS. The parts that go above "
                     "1MB are ONE block claimed and filled at load, so a lazy "
                     "member of that span would either force the block to be "
                     "claimed anyway - which is not lazy - or need a block of "
                     "its own, which is a different mechanism (SPEC.md "
                     "20.12.4)")
            seen_lazy = True
        elif not pflags & OPF_ZERO and seen_lazy:
            fail(f"part {i} is file-backed and follows an OP_LAZY part. A "
                 "lazy part is one op_size steps over, so every lazy row "
                 "comes after every part of the carve - otherwise stepping "
                 "over one leaves a hole in the MIDDLE of the run, and the "
                 "one read the carve exists to be reads it anyway (SPEC.md "
                 "20.12.4)")

        if pflags & OPF_COMP:
            if pflags & OPF_ZERO:
                fail(f"part {i}: OP_COMP on a scratch part. There are no file "
                     "bytes to compress, and its zkb word is the KB it asks "
                     "for (SPEC.md 20.12.7)")
            if pflags & OPF_LAZY:
                fail(f"part {i}: OP_COMP with OP_LAZY. A lazy row's zkb word "
                     "banks the segment it was fetched into and a compressed "
                     "row's carries its packed length - one word, and a "
                     "fetched part would overwrite the length it needs to be "
                     "fetched again (SPEC.md 20.12.7)")
            if pflags & OPF_XMS:
                fail(f"part {i}: OP_COMP with OP_XMS. The span above 1MB is "
                     "staged through a transient conventional buffer a chunk "
                     "at a time, and a stream cannot be expanded a chunk at a "
                     "time - the parts up there are stored plain (SPEC.md "
                     "20.12.7)")
            seen_comp = True

        if pflags & OPF_XMS:
            seen_xms = True
        elif not pflags & OPF_ZERO and not pflags & OPF_LAZY and seen_xms:
            fail(f"part {i} is file-backed and follows an OP_XMS part. The "
                 "parts that go above 1MB are read as ONE contiguous span "
                 "into ONE block, so they come after every conventional filed "
                 "part - and when there is no store they simply extend the "
                 "carve, which is what makes OP_XMS a hint rather than a mode "
                 "(SPEC.md 20.12.4)")
        filed.append(not pflags & OPF_ZERO)

    want = sum(filed)
    if want != len(parts):
        fail(f"the table declares {want} file-backed part(s) of {rows} and "
             f"{len(parts)} --part argument(s) were given; each file-backed "
             "row takes the next --part on the command line, and a row with "
             "no file bytes takes none (SPEC.md 20.12)")

    it = iter(parts)
    laid = []                   # (row, flags, first sector, sector count)
    for i, has_file in enumerate(filed):
        if not has_file:
            continue
        pflags = out[table + PARTS_HDR + PART_ROW * i + 1]
        body = read_file(next(it))
        if not body:
            fail(f"part {i}: the payload is empty")
        if len(body) > 0xFFFF:
            fail(f"part {i}: {len(body)} bytes. A part is reached through a "
                 "SEGMENT, so it is addressed by a 16-bit offset and 65,535 "
                 "is the ceiling (SPEC.md 20.12)")
        # ...and OP_COMP is where the disk bytes stop being the memory bytes.
        # STRICT, like --compress and unlike --compress-if: a row carrying the
        # flag is a decision the package's author wrote down, so a part that
        # does not pay is an error and not a line of output (SPEC.md 20.12.7).
        unpacked, packed = len(body), len(body)
        if pflags & OPF_COMP:
            body, packed = compress_part(i, body, part_fmt)
        # PADDING IS POISON, NOT ZERO. These are bytes no part declares and
        # nothing is meant to read, so a stub that strays into one - a length
        # rounded up to a sector, a run started a cluster early - has to
        # produce something a test can SEE. Zero padding makes every one of
        # those look exactly like a byte that was correctly zeroed.
        pad = (-len(out)) % PART_ALIGN
        out.extend(b"\xE5" * pad)
        off = table + PARTS_HDR + PART_ROW * i
        # `len` STAYS THE UNPACKED FIGURE. It is what op_size cuts the claim
        # from, so a machine that cannot fit the part is still told before a
        # sector is read - which is the third thing the whole parts design
        # exists for. `zkb` takes the packed one.
        struct.pack_into("<HH", out, off + 2, len(out) // PART_ALIGN, unpacked)
        if pflags & OPF_COMP:
            struct.pack_into("<H", out, off + 6, packed)
        laid.append((i, pflags, len(out) // PART_ALIGN,
                     (len(body) + PART_ALIGN - 1) // PART_ALIGN))
        out.extend(body)

    # THE RUN AND THE SPAN ARE EACH BOUNDED AT 128 SECTORS, and the bound is
    # the standard's own arithmetic rather than any machine's memory: op_size
    # refuses a carve of 64KB or more ("Parts do not fit", everywhere) because
    # op_cap is a word with the head slack added to it, and op_xload climbs
    # the OP_XMS span through `shl ax, 9` in a word, so a span of 128 sectors
    # reads as 0 bytes and 180 as 26,624 (apps/os88parts.inc). A package past
    # either bound builds, ships, and fails at launch with a toast that blames
    # the machine - so refuse it HERE, where the author is. The OP_XMS rows
    # are counted in the carve too: where there is no store they fall back
    # into it (SPEC.md 20.12.4), which is every 8088.
    def extent(rows_):
        return rows_[-1][2] + rows_[-1][3] - rows_[0][2] if rows_ else 0
    carve = [r for r in laid if not r[1] & OPF_LAZY]
    span = [r for r in laid if r[1] & OPF_XMS]
    if extent(carve) >= 128:
        fail(f"the carved run is {extent(carve)} sectors (parts "
             f"{carve[0][0]}..{carve[-1][0]}, OP_XMS rows included - they "
             "fall back into the carve on a machine with no store). op_size "
             "refuses 128 or more: the carve plus a cluster has to fit one "
             "WORD (SPEC.md 20.12.4)")
    if extent(span) >= 128:
        fail(f"the OP_XMS span is {extent(span)} sectors (parts "
             f"{span[0][0]}..{span[-1][0]}). op_xload walks it in a WORD of "
             "bytes, so 128 sectors is its ceiling too (SPEC.md 20.12.4)")

    # ...AND THE UNPACKED CARVE IS BOUNDED THE SAME WAY, on the other side.
    # op_size cuts the CLAIM from the unpacked lengths and refuses 128 sectors
    # there too - so a package whose parts compress from 70KB to 40KB passes
    # the disk bound above and fails at launch without this (SPEC.md 20.12.7).
    if seen_comp:
        usecs = 0
        for i in range(rows):
            off = table + PARTS_HDR + PART_ROW * i
            _, pf, po, pl, _ = struct.unpack_from("<BBHHH", out, off)
            if po and not pf & OPF_LAZY:
                usecs += (pl + PART_ALIGN - 1) // PART_ALIGN
        if usecs >= 128:
            fail(f"the carve UNPACKS to {usecs} sectors. op_size refuses 128 "
                 "or more on that side too - the claim is cut from the "
                 "unpacked lengths, and compressing a part makes the file "
                 "smaller without making the memory smaller (SPEC.md 20.12.7)")
        out[table + PART_FMT_AT] = (0 if part_fmt == "lz4" else 1)


def report_parts(out: bytes, table: int, rows: int, parts) -> None:
    fi = 0
    for i in range(rows):
        off = table + PARTS_HDR + PART_ROW * i
        kind, pflags, poff, plen, pzkb = struct.unpack_from("<BBHHH", out, off)
        what = "SEGMENT" if kind == PK_SEG else "ASSET  "
        tag = "".join(c for c, b in (("X", OPF_XMS), ("Z", OPF_ZERO),
                                     ("O", OPF_OPT), ("L", OPF_LAZY),
                                     ("C", OPF_COMP))
                      if pflags & b) or "-"
        if pflags & OPF_ZERO:
            print(f"os88pkg:   part {i} {what} {tag:4} scratch {pzkb}K, "
                  f"no file bytes")
        elif pflags & OPF_COMP:
            print(f"os88pkg:   part {i} {what} {tag:5} sector {poff} "
                  f"len {plen} from {pzkb} packed  <- {parts[fi]}")
            fi += 1
        else:
            print(f"os88pkg:   part {i} {what} {tag:4} sector {poff} "
                  f"len {plen}  <- {parts[fi]}")
            fi += 1


if __name__ == "__main__":
    sys.exit(main())
