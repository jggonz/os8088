#!/usr/bin/env python3
"""os8088's file compressor and splitter - the host side of SPEC.md 20.14 and 20.17.

Two containers, one tool:

  'CZ'  ONE compressed file (SPEC.md 20.14). The kernel expands it on the way
        in, so every program on the machine reads it as the original - but it
        is expanded WHOLE, into a claim of its unpacked size, so it is for
        files the machine can hold. `pack` / `unpack`.

  'CS'  a SPLIT SET (SPEC.md 20.17): NAME.001, NAME.002 ... each a part that
        fits a floppy, made of independent blocks of at most 32KB, each LZ4,
        LZB or stored. It exists because the field machine is an IBM 5150
        whose other way in is a parallel cable, and a 4MB video does not fit
        on a 720KB disk. The file manager's Uncompress on any part joins the
        set on the machine, streaming (SPEC.md 22.23.5), so the size of a set
        is bounded by the disk and not the heap. `split` / `join`.

    python3 tools/os88cz.py split  VIDEO.V88 --size 720k      -> VIDEO.001 ...
    python3 tools/os88cz.py join   VIDEO.001                  -> VIDEO.V88
    python3 tools/os88cz.py pack   README.TXT -o README.CZ    -> a 'CZ' file
    python3 tools/os88cz.py unpack README.CZ -o README.TXT    (a part: joins)
    python3 tools/os88cz.py info   VIDEO.003
    python3 tools/os88cz.py --selfcheck

**THIS FILE IS THE REFERENCE for the 'CS' container** and `tools/os88lz.py`
stays the reference for the streams inside it: a block's payload is exactly a
SPEC.md 20.13.7 stream, T word and raw tail included, so the kernel's decoder
expands it unchanged. tools/os88czgui.py is this tool's window and
dostools/os88cz.asm (OS88CZ.COM) its DOS twin; tests/unit/t_cz.py keeps the
three agreeing.

NO THIRD-PARTY IMPORTS, for os88lz.py's reason.
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88lz  # noqa: E402

# --- the 'CS' part header (SPEC.md 20.17.1) ----------------------------------
CS_MAGIC = b"CS"
CS_VER = 1
CS_HDR = 32
CS_MAXPARTS = 999               # three digits of extension
BLOCK = 32768                   # every block but the set's last expands to this
REC_HDR = 10                    # a block's own header (SPEC.md 20.17.2)
REC_MAX = REC_HDR + BLOCK       # the largest record, stored
M_STORE, M_LZ4, M_LZB = 0, 1, 2  # M = the LZ_* id plus one
METHODS = {"store": M_STORE, "lz4": M_LZ4, "lzb": M_LZB}
MNAMES = {v: k for k, v in METHODS.items()}

# A part fits a FRESH disk of its geometry: the whole data area, in clusters,
# of a volume with one file on it (the n/354, n/713 ... os88disk.py prints).
SIZES = {
    "360k": 354 * 1024,
    "720k": 713 * 1024,
    "1.2m": 2371 * 512,
    "1.44m": 2847 * 512,
}

_OK83 = set(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!#$%&'()-@^_`{}~")


class CZError(ValueError):
    """a refusal, said in a sentence - the GUI shows it as it is"""


# =============================================================================
# names
# =============================================================================
def name83(name):
    """`name` as an upper-case 8.3 name, or CZError saying why it is not one."""
    n = os.path.basename(name).upper()
    base, dot, ext = n.partition(".")
    if not base or len(base) > 8 or len(ext) > 3 or "." in ext:
        raise CZError(f"{name!r} is not an 8.3 name - give one with --name")
    for c in (base + ext).encode("latin-1", "replace"):
        if c not in _OK83:
            raise CZError(f"{name!r} has a character DOS will not take - "
                          "give an 8.3 name with --name")
    return base + (dot + ext if ext else "")


def part_name(name, k):
    """the k-th part's name: the original's base, a dot, three digits"""
    return f"{name83(name).partition('.')[0]}.{k:03d}"


def is_part_name(name):
    ext = os.path.basename(name).rpartition(".")[2]
    return "." in os.path.basename(name) and len(ext) == 3 and ext.isdigit()


# =============================================================================
# the block (SPEC.md 20.17.2)
# =============================================================================
def check(data):
    """(A, B) over the bytes as little-endian words, an odd byte zero-extended"""
    a = b = 0
    buf = bytes(data) + (b"\0" if len(data) & 1 else b"")
    for (w,) in struct.iter_unpack("<H", buf):
        a = (a + w) & 0xFFFF
        b = (b + a) & 0xFFFF
    return a, b


def encode_block(data, method=M_LZ4, depth=128):
    """one record: the 10-byte header and the payload. A block that does not
    get smaller is STORED - ten bytes of cost and never more."""
    n = len(data)
    if not 1 <= n <= BLOCK:
        raise CZError(f"a block of {n} bytes")
    pay, m = data, M_STORE
    if method != M_STORE:
        try:
            # LZB through the MACHINE's parse (cmz_pack's model): the host's
            # shortest-path one is ~5 points smaller and takes forty seconds
            # on a 32KB block of anything repetitive, which is a tool nobody
            # would wait for
            z = (os88lz.lz4_compress(data, depth) if method == M_LZ4
                 else os88lz.lzb_compress_machine(data))
        except ValueError:
            z = None
        if z is not None and len(z) < n:
            if os88lz.decompress(z, method - 1, n) != bytes(data):
                raise CZError("round trip failed - os88lz is the reference")
            pay, m = z, method
    a, b = check(data)
    return (len(pay).to_bytes(2, "little") + n.to_bytes(2, "little")
            + bytes([m, 0]) + a.to_bytes(2, "little") + b.to_bytes(2, "little")
            + bytes(pay))


def _enc(args):
    return encode_block(*args)


def decode_record(buf, p):
    """(bytes, next p) for the record at p, or CZError"""
    if p + REC_HDR > len(buf):
        raise CZError("a block header runs past the end of the part")
    s = int.from_bytes(buf[p:p + 2], "little")
    n = int.from_bytes(buf[p + 2:p + 4], "little")
    m, r = buf[p + 4], buf[p + 5]
    a0 = int.from_bytes(buf[p + 6:p + 8], "little")
    b0 = int.from_bytes(buf[p + 8:p + 10], "little")
    if not (1 <= s <= BLOCK and 1 <= n <= BLOCK) or m > M_LZB or r:
        raise CZError(f"a bad block header at {p}")
    if m == M_STORE and s != n:
        raise CZError(f"a stored block of {n} carrying {s}")
    q = p + REC_HDR
    if q + s > len(buf):
        raise CZError("a block runs past the end of the part")
    pay = bytes(buf[q:q + s])
    try:
        out = pay if m == M_STORE else os88lz.decompress(pay, m - 1, n)
    except (ValueError, IndexError) as e:
        raise CZError(f"a block at {p} will not expand: {e}")
    if check(out) != (a0, b0):
        raise CZError(f"a block at {p} fails its check - a damaged copy")
    return out, q + s


# =============================================================================
# the set (SPEC.md 20.17.1)
# =============================================================================
def parse_part(blob):
    """the part header as a dict, or CZError"""
    if len(blob) < CS_HDR or blob[:2] != CS_MAGIC:
        raise CZError("not a part of a split set")
    if blob[2] != CS_VER or blob[3]:
        raise CZError(f"a part of version {blob[2]}, which this tool does "
                      "not know")
    u16 = lambda o: int.from_bytes(blob[o:o + 2], "little")     # noqa: E731
    u32 = lambda o: int.from_bytes(blob[o:o + 4], "little")     # noqa: E731
    h = dict(index=u16(4), count=u16(6), total=u32(8), offset=u32(12),
             setid=u32(16), name=blob[20:32].rstrip(b"\0").decode("latin-1"))
    if not (1 <= h["index"] <= h["count"] <= CS_MAXPARTS) or not h["total"]:
        raise CZError("a part header that does not describe a set")
    if h["offset"] % BLOCK or h["offset"] >= h["total"]:
        raise CZError("a part whose offset is not a block boundary")
    name83(h["name"])
    return h


def _header(k, count, total, offset, setid, name):
    return (CS_MAGIC + bytes([CS_VER, 0]) + k.to_bytes(2, "little")
            + count.to_bytes(2, "little") + total.to_bytes(4, "little")
            + offset.to_bytes(4, "little") + setid.to_bytes(4, "little")
            + name83(name).encode().ljust(12, b"\0"))


def part_size(size):
    """'720k' and friends, or a byte count"""
    s = str(size).lower()
    if s in SIZES:
        return SIZES[s]
    try:
        v = int(s, 0)
    except ValueError:
        raise CZError(f"a part size of {size!r} - give 360k, 720k, 1.2m, "
                      "1.44m or bytes")
    if v < CS_HDR + REC_MAX:
        raise CZError(f"a part of {v} bytes cannot hold a block - the "
                      f"smallest is {CS_HDR + REC_MAX}")
    return v


def split(data, name, size="720k", method=M_LZ4, depth=128, progress=None,
          jobs=None):
    """the parts of a set, as a list of bytes. `progress(done, total)` is
    called as blocks are encoded."""
    data = bytes(data)
    name = name83(name)
    if is_part_name(name):
        raise CZError(f"{name} would be one of its own parts' names")
    if not data:
        raise CZError("an empty file has nothing to carry")
    cap = part_size(size) - CS_HDR
    blocks = [data[i:i + BLOCK] for i in range(0, len(data), BLOCK)]
    work = [(b, method, depth) for b in blocks]
    recs = []
    if method == M_STORE or len(work) < 4 or jobs == 1:
        for w in work:
            recs.append(_enc(w))
            if progress:
                progress(len(recs), len(work))
    else:
        import multiprocessing
        with multiprocessing.Pool(jobs) as pool:
            for r in pool.imap(_enc, work, chunksize=2):
                recs.append(r)
                if progress:
                    progress(len(recs), len(work))
    # whole records into each part, in order
    groups, cur, used = [], [], 0
    for r in recs:
        if cur and used + len(r) > cap:
            groups.append(cur)
            cur, used = [], 0
        cur.append(r)
        used += len(r)
    groups.append(cur)
    if len(groups) > CS_MAXPARTS:
        raise CZError(f"{len(groups)} parts - a set is at most "
                      f"{CS_MAXPARTS}; use a bigger part size")
    setid = zlib.crc32(data) & 0xFFFFFFFF
    parts, off = [], 0
    for k, g in enumerate(groups, 1):
        parts.append(_header(k, len(groups), len(data), off, setid, name)
                     + b"".join(g))
        off += BLOCK * len(g)
    return parts


def join(parts, progress=None):
    """(name, bytes) from a set's parts, in order - checked as the machine
    checks them (SPEC.md 22.23.5), and a refusal names the part"""
    if not parts:
        raise CZError("no parts")
    first = parse_part(parts[0])
    if first["index"] != 1:
        raise CZError("the first part given is not part 1")
    if len(parts) != first["count"]:
        raise CZError(f"a set of {first['count']} parts, and "
                      f"{len(parts)} given")
    out = bytearray()
    for k, blob in enumerate(parts, 1):
        pn = part_name(first["name"], k)
        try:
            h = parse_part(blob)
        except CZError as e:
            raise CZError(f"{pn}: {e}")
        if (h["index"] != k or h["count"] != first["count"]
                or h["total"] != first["total"]
                or h["setid"] != first["setid"] or h["offset"] != len(out)):
            raise CZError(f"{pn} is not part {k} of this set")
        p = CS_HDR
        if p >= len(blob):
            raise CZError(f"{pn} has no blocks")
        while p < len(blob):
            if len(out) % BLOCK:
                raise CZError(f"{pn}: a short block before the last")
            try:
                b, p = decode_record(blob, p)
            except CZError as e:
                raise CZError(f"{pn}: {e}")
            out += b
            if len(out) > first["total"]:
                raise CZError(f"{pn}: the set is longer than its header says")
            if progress:
                progress(len(out), first["total"])
    if len(out) != first["total"]:
        raise CZError(f"the set is {len(out)} bytes and its header says "
                      f"{first['total']}")
    return first["name"], bytes(out)


# =============================================================================
# 'CZ' (SPEC.md 20.14) - os88lz carries the container, this is the refusals
# =============================================================================
# The most a 'CZ' file can be and still be opened ANYWHERE: the machine
# expands one whole, in place, into a claim of its unpacked size (SPEC.md
# 20.14.2, 22.23.4), and the heap a 640KB machine gives a program is 449KB
# (docs/plans/KERN-DOS-PLAN.md's measured arena). A bigger file packs
# perfectly well and is `Not enough memory` on every machine there is - which
# is what a split set is for (20.17)
CZ_OPENMAX = 449 * 1024


def pack(data, method=M_LZ4):
    """a 'CZ' file, or CZError saying why the machine could not use one"""
    if method == M_STORE:
        raise CZError("a 'CZ' file is compressed by definition")
    if len(data) > CZ_OPENMAX:
        raise CZError(
            f"{len(data):,} bytes is too big for a 'CZ' file: the machine "
            f"expands one whole, in memory, and no machine has more than "
            f"{CZ_OPENMAX // 1024}KB to give it. Split it instead - a split "
            f"set of any size joins in 82KB (SPEC.md 20.17)")
    data = bytes(data)
    z = None
    if method == M_LZB:
        # the MACHINE's parse, as a split's blocks use: os88lz's own is
        # exact and ran for longer than anyone waited on a 720KB disk image
        # (a zero-filled region is the worst case for it). The machine's
        # Compress writes 'CZ' files with this very parse (cmz_pack)
        try:
            z = os88lz.lzb_compress_machine(data)
        except ValueError:
            z = None
        if z is None:
            raise CZError("it would not get smaller")
    out, did = os88lz.cz_wrap(data, method - 1, packed=z)
    if not did:
        raise CZError("it would not get smaller")
    return out


def unpack(blob):
    p = os88lz.cz_parse(blob)
    if not p:
        raise CZError("not a 'CZ' file")
    try:
        return os88lz.cz_unwrap(blob)
    except (ValueError, IndexError) as e:
        raise CZError(f"it will not expand: {e}")


def kind(blob):
    if len(blob) >= 2 and blob[:2] == CS_MAGIC:
        return "part"
    if os88lz.cz_parse(blob):
        return "cz"
    return "plain"


# =============================================================================
# files
# =============================================================================
def _find(folder, name):
    """a file in `folder` by name, case-insensitively - a FAT volume mounted
    on a host hands back whichever case its driver likes"""
    want = name.upper()
    try:
        for f in os.listdir(folder):
            if f.upper() == want:
                return os.path.join(folder, f)
    except FileNotFoundError:
        pass
    return None


def split_file(path, outdir=None, size="720k", method=M_LZ4, name=None,
               progress=None, jobs=None):
    data = open(path, "rb").read()
    nm = name83(name or os.path.basename(path))
    parts = split(data, nm, size, method, progress=progress, jobs=jobs)
    outdir = outdir or os.path.dirname(os.path.abspath(path))
    os.makedirs(outdir, exist_ok=True)
    out = []
    for k, blob in enumerate(parts, 1):
        p = os.path.join(outdir, part_name(nm, k))
        with open(p, "wb") as f:
            f.write(blob)
        out.append(p)
    return nm, len(data), out


GEOM_KB = {"360k": 360, "720k": 720, "1.2m": 1200, "1.44m": 1440}


def make_images(paths, size, outdir=None):
    """one FAT12 floppy image per part, `BIG-001.IMG` for `BIG.001`, of the
    geometry the parts were cut for - so a set crosses to the machine as N
    images written to N disks, with nothing to copy by hand. Built by
    tools/os88disk.py, which is what builds every shipped floppy."""
    import subprocess
    kb = GEOM_KB.get(str(size).lower())
    if not kb:
        raise CZError("disk images want a named size: " + ", ".join(GEOM_KB))
    tool = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "os88disk.py")
    out = []
    for p in paths:
        base = os.path.basename(p).replace(".", "-") + ".IMG"
        img = os.path.join(outdir or os.path.dirname(os.path.abspath(p)), base)
        r = subprocess.run([sys.executable, tool, "-o", img, "--size",
                            str(kb), p], capture_output=True, text=True)
        if r.returncode:
            raise CZError(f"{base}: {(r.stderr or r.stdout).strip()}")
        out.append(img)
    return out


def join_file(path, outdir=None, progress=None):
    """join the set `path` is a part of: every part from .001 on, found beside
    it. Answers (the output's path, its size)."""
    folder = os.path.dirname(os.path.abspath(path))
    h = parse_part(open(path, "rb").read(CS_HDR))
    parts = []
    for k in range(1, h["count"] + 1):
        pn = part_name(h["name"], k)
        p = _find(folder, pn)
        if not p:
            raise CZError(f"{pn} is missing - every part must be in "
                          f"{folder}")
        parts.append(open(p, "rb").read())
    name, data = join(parts, progress)
    outdir = outdir or folder
    os.makedirs(outdir, exist_ok=True)
    out = os.path.join(outdir, name)
    with open(out, "wb") as f:
        f.write(data)
    return out, len(data)


def describe(blob):
    """one paragraph about a file, for `info` and the GUI"""
    k = kind(blob)
    if k == "cz":
        fmt, n = os88lz.cz_parse(blob)
        return (f"a 'CZ' file, {os88lz.NAMES[fmt].upper()}: {len(blob):,} "
                f"bytes that expand to {n:,} ({len(blob) / max(n, 1):.1%})")
    if k == "part":
        h = parse_part(blob)
        p, n, ms = CS_HDR, 0, {}
        while p < len(blob):
            s = int.from_bytes(blob[p:p + 2], "little")
            n += int.from_bytes(blob[p + 2:p + 4], "little")
            m = MNAMES.get(blob[p + 4], "?")
            ms[m] = ms.get(m, 0) + 1
            p += REC_HDR + s
        return (f"part {h['index']} of {h['count']} of {h['name']} "
                f"({h['total']:,} bytes, set {h['setid']:08X}): "
                f"{len(blob):,} bytes carrying {n:,} from offset "
                f"{h['offset']:,}; blocks "
                + ", ".join(f"{v} {k}" for k, v in sorted(ms.items())))
    return f"a plain file of {len(blob):,} bytes"


# =============================================================================
def _selfcheck(paths):
    import random
    rnd = random.Random(8088)
    bad = 0

    def expect_refusal(what, fn):
        nonlocal bad
        try:
            fn()
        except CZError:
            return
        print(f"os88cz: NOT REFUSED - {what}")
        bad += 1

    cases = [b"x", b"hello, 5150" * 3, bytes(BLOCK), bytes(BLOCK + 1),
             (b"text " * 7000)[:BLOCK + 99],
             bytes(rnd.getrandbits(8) for _ in range(3 * BLOCK + 17))]
    for f in paths or []:
        cases.append(open(f, "rb").read())
    for i, d in enumerate(cases):
        for m in (M_STORE, M_LZ4, M_LZB):
            for size in ("360k", CS_HDR + REC_MAX):
                if size != "360k" and len(d) > 8 * BLOCK:
                    continue
                parts = split(d, "TEST.BIN", size, m, jobs=1)
                nm, out = join(parts)
                if out != d or nm != "TEST.BIN":
                    print(f"os88cz: ROUND TRIP FAILED case {i} "
                          f"{MNAMES[m]} {size}")
                    bad += 1
                if max(len(p) for p in parts) > part_size(size):
                    print(f"os88cz: A PART OVER ITS SIZE case {i}")
                    bad += 1
    # the refusals the machine makes, made here first
    d = bytes(rnd.getrandbits(8) for _ in range(5 * BLOCK))
    parts = split(d, "SET.DAT", CS_HDR + REC_MAX, M_STORE, jobs=1)
    other = split(d[::-1], "SET.DAT", CS_HDR + REC_MAX, M_STORE, jobs=1)
    expect_refusal("a missing part", lambda: join(parts[:-1]))
    expect_refusal("a part from another set",
                   lambda: join(parts[:2] + other[2:3] + parts[3:]))
    expect_refusal("two parts swapped",
                   lambda: join([parts[0], parts[2], parts[1]] + parts[3:]))
    dmg = bytearray(parts[1])
    dmg[CS_HDR + REC_HDR + 100] ^= 0x40
    expect_refusal("a damaged byte",
                   lambda: join([parts[0], bytes(dmg)] + parts[2:]))
    expect_refusal("a truncated last part",
                   lambda: join(parts[:-1] + [parts[-1][:-1]]))
    expect_refusal("a part named like a part",
                   lambda: split(b"abc", "FOO.001"))
    expect_refusal("a long name", lambda: split(b"abc", "LONGNAME9.TXT"))
    expect_refusal("an empty file", lambda: split(b"", "E.TXT"))
    # the check catches a word swap, which one sum would not
    a = bytes(range(8))
    if check(a) == check(a[2:4] + a[0:2] + a[4:]):
        print("os88cz: the check missed a word swap")
        bad += 1
    # pack/unpack
    txt = b"The quick brown fox. " * 200
    if unpack(pack(txt)) != txt or unpack(pack(txt, M_LZB)) != txt:
        print("os88cz: 'CZ' ROUND TRIP FAILED")
        bad += 1
    print(f"os88cz: {len(cases)} cases, three methods, "
          + ("ALL ROUND TRIPS AND REFUSALS OK" if not bad
             else f"{bad} FAILURE(S)"))
    return 1 if bad else 0


def _bar(label):
    def f(done, total):
        w = 40
        k = w * done // max(total, 1)
        sys.stderr.write(f"\r{label} [{'#' * k}{'.' * (w - k)}] "
                         f"{done}/{total}")
        if done >= total:
            sys.stderr.write("\n")
        sys.stderr.flush()
    return f if sys.stderr.isatty() else None


def main():
    import argparse
    argv = sys.argv[1:]
    if "--selfcheck" in argv:           # before the subcommands can claim
        return _selfcheck([f for f in argv if f != "--selfcheck"])
    ap = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        epilog="SPEC.md 20.17 is the split set's contract, 22.23.5 the join "
               "on the machine.")
    ap.add_argument("--selfcheck", action="store_true",
                    help="round-trip every method and refusal, plus FILES")
    sub = ap.add_subparsers(dest="cmd")

    def meth(p, default="lz4"):
        g = p.add_mutually_exclusive_group()
        g.add_argument("--lz4", dest="method", action="store_const",
                       const=M_LZ4, help="fast to expand on an 8088 "
                       "(the default)")
        g.add_argument("--lzb", dest="method", action="store_const",
                       const=M_LZB, help="~10 points smaller, ~4x the decode")
        if default == "lz4":
            g.add_argument("--store", dest="method", action="store_const",
                           const=M_STORE, help="split only, compress nothing")
        p.set_defaults(method=METHODS[default])

    p = sub.add_parser("split", help="cut a file into NAME.001 ... parts")
    p.add_argument("file")
    p.add_argument("--size", default="720k",
                   help="360k, 720k (default), 1.2m, 1.44m, or bytes")
    p.add_argument("--name", help="the 8.3 name it rejoins as (default: "
                   "the file's own)")
    p.add_argument("-o", "--outdir")
    p.add_argument("-j", "--jobs", type=int, help="encoder processes")
    p.add_argument("--images", action="store_true",
                   help="also write each part to a floppy image of its size "
                        "(NAME-001.IMG ...), ready to write to a disk")
    meth(p)
    p = sub.add_parser("join", help="rejoin a set from any of its parts")
    p.add_argument("part")
    p.add_argument("-o", "--outdir")
    p = sub.add_parser("pack", help="compress ONE file into a 'CZ' file")
    p.add_argument("file")
    p.add_argument("-o", "--out")
    meth(p)
    p = sub.add_parser("unpack", help="expand a 'CZ' file, or join a set")
    p.add_argument("file")
    p.add_argument("-o", "--out", help="the output file ('CZ') or folder "
                   "(a set)")
    p = sub.add_parser("info", help="say what a file is")
    p.add_argument("files", nargs="+")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "split":
            nm, n, out = split_file(a.file, a.outdir, a.size, a.method,
                                    a.name, _bar("encoding"), a.jobs)
            tot = sum(os.path.getsize(p) for p in out)
            print(f"os88cz: {nm}, {n:,} bytes -> {len(out)} part(s), "
                  f"{tot:,} bytes ({tot / n:.1%})")
            for p in out:
                print(f"  {p}  {os.path.getsize(p):,}")
            if a.images:
                for p in make_images(out, a.size, a.outdir):
                    print(f"  {p}")
        elif a.cmd == "join" or (a.cmd == "unpack" and
                                 kind(open(a.file, "rb").read(2)) == "part"):
            src = a.part if a.cmd == "join" else a.file
            od = a.outdir if a.cmd == "join" else a.out
            out, n = join_file(src, od, _bar("joining"))
            print(f"os88cz: {out}, {n:,} bytes")
        elif a.cmd == "pack":
            d = open(a.file, "rb").read()
            z = pack(d, a.method)
            out = a.out or a.file + ".CZ"
            open(out, "wb").write(z)
            print(f"os88cz: {out}, {len(d):,} -> {len(z):,} bytes "
                  f"({len(z) / len(d):.1%})")
        elif a.cmd == "unpack":
            d = unpack(open(a.file, "rb").read())
            out = a.out or (a.file[:-3] if a.file.upper().endswith(".CZ")
                            else a.file + ".OUT")
            open(out, "wb").write(d)
            print(f"os88cz: {out}, {len(d):,} bytes")
        elif a.cmd == "info":
            for f in a.files:
                print(f"{f}: {describe(open(f, 'rb').read())}")
        else:
            ap.print_help()
            return 2
    except (CZError, OSError) as e:
        print(f"os88cz: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
