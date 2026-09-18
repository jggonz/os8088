#!/usr/bin/env python3
"""kern_dos is a PART, and its bytes are reachable as absolute SECTORS.

    make && make kdostest && python3 tests/kdpart.py

docs/plans/KERN-DOS-PLAN.md §4.1.1 is the claim this row makes good, and it is
the one the whole handoff rests on: *the part is a byte range of `DOS.O88`,
which is a file on a volume, so the same walk that turns a hibernation image
into extents turns the part into extents too.* The stub then reads `kern_dos`
straight into low memory with `int 13h`, off a machine whose heap has already
been given away.

**IT IS ARITHMETIC AND IT IS DONE HERE BEFORE ANY ASSEMBLY DEPENDS ON IT.**
Every step below is one the kernel's walk and the stub will repeat on the
guest, and each has a way to be quietly wrong:

  1  the part table says where the part starts IN THE FILE - `OP_R_OFF` in
     512-byte units, which is not bytes and not clusters;
  2  the FAT chain says which CLUSTERS the file occupies, which are not
     contiguous and whose runs are what an extent list is;
  3  a cluster is `spc` sectors, so file sector N is the (N mod spc)'th sector
     of the (N div spc)'th cluster - the one place an off-by-one lands in the
     middle of the image rather than at its edge;
  4  and the bytes at those sectors, LZ4-expanded, have to BE `kern_dos` -
     which is the only step that cannot be fooled by a consistent mistake in
     the first three.

VERIFIED TO FAIL: starting the walk one sector LATE takes step 4 red with
`LZ4: bad offset 3956 at output 5` - a stream that starts mid-token - and
taking the part's UNPACKED length as the number of sectors to read takes step
3 red with `wants file sectors 63..153 and the file has 138`.
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88fat                                                  # noqa: E402
import os88lz                                                   # noqa: E402

IMG = os.path.join(ROOT, "build", "os8088-360.img")
KD = os.path.join(ROOT, "build", "kerndos.bin")
CORE = os.path.join(ROOT, "build", "doscore.bin")
DOSCALL = os.path.join(ROOT, "apps", "dos", "doscall.inc")
PARTS_MAGIC = b"O88PARTS"
PARTS_HDR = 10                  # magic(8) + count(1) + reserved(1)
PART_ROW = 8
DOS_PART_CORE = 1               # loader image, 0 = the box, 1 = the INT 21h
DOS_PART_KD = 2                 # CORE (SPEC.md 96.44.5), 2 = kern_dos


def coredef(name):
    """One `equ` out of apps/dos/doscall.inc. Read rather than mirrored: a
    fourth copy of CORE_ORG is a fourth thing to keep in step, and this row is
    about whether the ASSEMBLY agrees with itself."""
    for line in open(DOSCALL):
        f = line.split()
        if len(f) >= 3 and f[0] == name and f[1] == "equ":
            return int(f[2], 0)
    fail("%s is not an equ in %s" % (name, DOSCALL))


def fail(msg):
    print("kdpart: FAIL: %s" % msg)
    sys.exit(1)


def read_chain(v, first, size):
    """A file's bytes, out of its cluster chain. `Fat12.read` is ROOT ONLY by
    design (its own docstring says why), and DOS.O88 is in APPS/."""
    out = bytearray()
    for c in v.chain(first):
        off = v.cluster_off(c)
        out += v.img[off:off + v.spc * v.bps]
    return bytes(out[:size])


def dir_entry(v, folder, raw11):
    """The 32-byte directory record for `raw11` inside root/`folder`."""
    fc = None
    for _i, _o, e in v.entries():
        if e[:11] == os88fat.Fat12.raw11(folder) and e[11] & 0x10:
            fc = struct.unpack_from("<H", e, 26)[0]
    if fc is None:
        fail("no %s folder in the root of %s" % (folder, os.path.basename(IMG)))
    for c in v.chain(fc):
        off = v.cluster_off(c)
        for i in range(0, v.spc * v.bps, 32):
            rec = bytes(v.img[off + i:off + i + 32])
            if rec[0] in (0x00, 0xE5):
                continue
            if rec[:11] == raw11:
                return rec
    return None


def main():
    for p in (IMG, KD, CORE):
        if not os.path.exists(p):
            fail("%s is missing - `make` builds the system disk and "
                 "`make kdostest` the DOS pieces" % p)

    v = os88fat.Fat12(IMG)
    ent = dir_entry(v, "APPS", b"DOS     O88")
    if ent is None:
        fail("no DOS.O88 in APPS/ of %s - $(SYSROOTARG) is what puts it "
             "there" % os.path.basename(IMG))
    fclus = struct.unpack_from("<H", ent, 26)[0]
    fsize = struct.unpack_from("<I", ent, 28)[0]
    blob = read_chain(v, fclus, fsize)
    print("kdpart: APPS/DOS.O88 is %d bytes, %d sector(s)"
          % (len(blob), -(-len(blob) // 512)))

    # --- 1: the part table, inside the image ---------------------------------
    image = struct.unpack_from("<H", blob, 8)[0]
    flags = blob[3]
    if not flags & 4:
        fail("DOS.O88's flags are 0x%02X and bit 2 (OS88_F_PARTS) is clear - "
             "this is the PLAIN package, so the SHIPPED system disk has lost "
             "kern_dos and the Memory page's third arm is greyed on every "
             "machine. $(SYSROOT) in the Makefile is the one variable that "
             "decides it (SPEC.md 96.40.3)" % flags)
    at = blob.find(PARTS_MAGIC, 0, image)
    if at < 0:
        fail("flags bit 2 is set and there is no 'O88PARTS' table in the image")
    # **kern_dos IS PART 1 AND THE IMAGE IS A LOADER** (SPEC.md 96.44.4). It
    # was part 0 of a package whose image was the box itself until W9c, which
    # made the image `apps/dos/dosload.asm` and the box part 0 - because
    # os88pkg.py refuses --compress beside parts, so whatever is the IMAGE
    # ships raw and 96.40.3 measured that at +932 ms a launch.
    n = blob[at + PARTS_HDR - 2]
    if n != 3:
        fail("the table declares %d part(s); the four-piece DOS.O88 is a "
             "loader image with THREE - the box, the INT 21h core and "
             "kern_dos (SPEC.md 96.44.5)" % n)
    def row(idx):
        _k, pflags, poff, plen, pzkb = struct.unpack_from(
            "<BBHHH", blob, at + PARTS_HDR + PART_ROW * idx)
        if not pflags & 16:
            fail("part %d's flags are 0x%02X and OP_COMP (16) is clear: an "
                 "uncompressed part costs the 360KB system disk eight more "
                 "clusters than it has to. The pairing with OP_LAZY was "
                 "refused until SPEC.md 20.12.7.4" % (idx, pflags))
        if not pflags & 8:
            fail("part %d's flags are 0x%02X and OP_LAZY (8) is clear. It "
                 "MUST be lazy: op_load reads every eager part into one carve "
                 "and op_size refuses a carve of 64KB or more, and the box "
                 "plus kern_dos unpack to ~74KB (SPEC.md 96.44.4.1)"
                 % (idx, pflags))
        return poff, plen, pzkb

    coff, clen, czkb = row(DOS_PART_CORE)
    poff, plen, pzkb = row(DOS_PART_KD)
    print("kdpart: 1/7 part %d is ASSET+COMP+LAZY at file sector %d, %d bytes "
          "unpacked, %d packed" % (DOS_PART_KD, poff, plen, pzkb))
    print("kdpart: 2/7 part %d (the CORE) at file sector %d, %d bytes "
          "unpacked, %d packed" % (DOS_PART_CORE, coff, clen, czkb))

    # --- 2: the chain, as absolute LBA runs ----------------------------------
    # Exactly the shape kernel/hiber.inc's hbm_extents builds: {lba, count},
    # contiguous clusters coalesced. The volume is the boot floppy, so an
    # absolute LBA is the volume-relative one - there is no partition base.
    runs = []
    for c in v.chain(fclus):
        lba = v.cluster_lba(c)
        if runs and runs[-1][0] + runs[-1][1] == lba:
            runs[-1][1] += v.spc
        else:
            runs.append([lba, v.spc])
    print("kdpart: 3/7 the chain is %d cluster(s) in %d coalesced run(s)"
          % (len(list(v.chain(fclus))), len(runs)))

    # --- 3: file sector -> absolute sector ------------------------------------
    flat = []
    for lba, cnt in runs:
        flat.extend(range(lba, lba + cnt))

    def walk(off, zkb, what):
        need = -(-zkb // 512)
        if off + need > len(flat):
            fail("%s wants file sectors %d..%d and the file has %d"
                 % (what, off, off + need - 1, len(flat)))
        want = flat[off:off + need]
        ext, i = [], 0
        while i < len(want):
            j = i
            while j + 1 < len(want) and want[j + 1] == want[j] + 1:
                j += 1
            ext.append((want[i], j - i + 1))
            i = j + 1
        return want, ext

    want, ext = walk(poff, pzkb, "the part")
    print("kdpart: 4/7 the part is %d sector(s) in %d extent(s): %s"
          % (len(want), len(ext),
             ", ".join("%d+%d" % e for e in ext[:6])
             + (" ..." if len(ext) > 6 else "")))

    # --- 4: and the bytes ARE kern_dos ---------------------------------------
    got = b"".join(bytes(v.img[l * v.bps:(l + 1) * v.bps]) for l, _c in
                   [(s, 1) for s in want])
    got = got[:pzkb]
    try:
        out = os88lz.decompress(got, os88lz.LZ4, plen)
    except Exception as e:                                      # noqa: BLE001
        fail("the sectors the walk names do not decompress: %s. That is step "
             "3's arithmetic wrong, not the packer's - a part read one sector "
             "early or late is a stream that starts mid-token" % e)
    if len(out) != plen:
        fail("the part expands to %d bytes and its row says %d" % (len(out), plen))
    raw = open(KD, "rb").read()
    if out != raw:
        n = next((i for i, (a, b) in enumerate(zip(out, raw)) if a != b), None)
        fail("the part's bytes are not build/kerndos.bin - they differ at "
             "offset %s of %d" % (n, len(raw)))
    print("kdpart: 5/7 those sectors expand to build/kerndos.bin EXACTLY "
          "(%d bytes)" % len(raw))

    # --- 6: and so do the CORE's, out of ONE run with them --------------------
    # **`hbm_dosrun` READS BOTH PARTS AS ONE RANGE** (SPEC.md 96.44.5.4):
    # KDH_COFF to the end of KDH_PLEN, because the stub has two streams to
    # expand and one disk walk to do it with. So the arithmetic checked above
    # is checked again over the pair, and the CORE's own bytes have to come out
    # of the front of it.
    cwant, cext = walk(coff, czkb, "the core part")
    craw = open(CORE, "rb").read()
    try:
        cout = os88lz.decompress(
            b"".join(bytes(v.img[l * v.bps:(l + 1) * v.bps]) for l in cwant)[:czkb],
            os88lz.LZ4, clen)
    except Exception as e:                                      # noqa: BLE001
        fail("the core part's sectors do not decompress: %s" % e)
    if cout != craw:
        n = next((i for i, (a, b) in enumerate(zip(cout, craw)) if a != b), None)
        fail("the core part's bytes are not build/doscore.bin - they differ "
             "at offset %s of %d" % (n, len(craw)))
    span = (poff - coff) + len(want)
    bwant, bext = walk(coff, span * 512, "the combined run")
    if bwant[:len(cwant)] != cwant or bwant[-len(want):] != want:
        fail("the combined range %d..%d does not contain both parts' sectors "
             "in order - hbm_dosrun's one walk would hand the stub the wrong "
             "bytes" % (coff, coff + span - 1))
    # KDS_CDELTA is paragraphs from the run's base to kern_dos's stream, and
    # 512-aligned parts are what make it exact.
    delta = (poff - coff) * 32
    if delta * 16 + pzkb > span * 512:
        fail("KDS_CDELTA would be %d paragraphs and the run is only %d bytes"
             % (delta, span * 512))
    print("kdpart: 6/7 the pair is ONE range of %d sector(s) in %d extent(s), "
          "kern_dos's stream %d paragraph(s) in" % (span, len(bext), delta))

    # --- ...and kern_dos really does reserve the hole the core lands in -------
    # The stub expands kern_dos FIRST and the core over it, so what has to be
    # true of the image on the disk is that CORE_ORG..CORE_ORG+CORE_MAX is
    # `times ... db 0` and nothing else. If that span ever carried code, the
    # core would land on top of it.
    corg, cmax, cbss = (coredef("CORE_ORG"), coredef("CORE_MAX"),
                        coredef("CORE_BSS_SIZE"))
    if len(raw) < corg + cmax + cbss:
        fail("kern_dos is %d bytes and the core's reservation needs %d - the "
             "`times CORE_MAX + CORE_BSS_SIZE db 0` in kerndos/kdos.asm is "
             "not there" % (len(raw), corg + cmax + cbss))
    hole = raw[corg:corg + cmax + cbss]
    if hole.count(0) != len(hole):
        n = next(i for i, b in enumerate(hole) if b)
        fail("kern_dos's reservation at 0x%04X is not zeros - byte %d of it "
             "is 0x%02X, so the core would land on top of something"
             % (corg, n, hole[n]))
    if len(craw) > cmax:
        fail("the core is %d bytes and CORE_MAX is %d" % (len(craw), cmax))

    # --- 5: the extent list is what HS_XMAX can hold --------------------------
    # The staging area the stub reads from is fixed at assembly time
    # (SPEC.md 87.5 step 2), so a part fragmented past it cannot be handed over
    # at all - and a floppy that has been written to for a year is where that
    # would first show up.
    HS_XMAX = 1280
    if len(ext) > HS_XMAX:
        fail("the part is in %d extents and the staging area holds %d"
             % (len(ext), HS_XMAX))
    if len(bext) > HS_XMAX:
        fail("the pair is in %d extents and the staging area holds %d"
             % (len(bext), HS_XMAX))
    print("kdpart: 7/7 %d extent(s) for the pair against the %d the staging "
          "area holds; the reservation at 0x%04X is %d zero byte(s)"
          % (len(bext), HS_XMAX, corg, len(hole)))
    print("kdpart: ok - both parts are reachable as absolute sectors")
    return 0


if __name__ == "__main__":
    sys.exit(main())
