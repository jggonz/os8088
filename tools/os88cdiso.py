#!/usr/bin/env python3
"""os88cdiso: CD images for CDROM.DRV's gates, and its reference (SPEC.md 119).

    python3 tools/os88cdiso.py iso -o OUT.iso [--volid ID] --file PATH=HOST ...
    python3 tools/os88cdiso.py audio -o OUT.cue [--tone SECONDS:HZ ...]
                                      [--data DATA.iso]
    python3 tools/os88cdiso.py list IMAGE.iso [FOLDER/SUBFOLDER]
    python3 tools/os88cdiso.py --selfcheck

THREE JOBS, one file, for the reason tools/htmsim.py and tools/weavesim.py
are one file each: what the driver does is written down ONCE in a language
that can be read, and a gate compares the machine against it.

  iso    an ISO 9660 volume WITH FOLDERS - which tools/os88iso.py (the live
         CD's wrapper, SPEC.md 80.2) deliberately has none of. `--file
         DOCS/DEEP/BOTTOM.TXT=path` makes the folders on the way. Names are
         written AS GIVEN plus `;1`, lower case and long ones included: the
         fixture exists to make the driver's 8.3 names (119.3.2) do work.
  audio  a CUE/BIN audio disc for 86Box (vm/386-cdrom, SPEC.md 119.7): each
         `--tone S:HZ` a Red Book track of S seconds of a sine at HZ, and
         `--data` an ISO as track 1 ahead of them (CD-Extra's order is the
         other way round; this is the "mixed mode" one).
  list   what CDROM.DRV lists for a folder of an ISO: the 8.3 name, the type
         (0 file, 1 package, 2 folder), the size and THE HANDLE (119.3.3) -
         `mangle` and `handle` below are the reference the driver's
         cd_mangle and cd_hof are held to, to the byte.

Everything is deterministic - fixed dates, fixed order - so the same inputs
give the same bytes, as every image in this tree does.
"""
import argparse
import math
import os
import struct
import sys

LBS = 2048
YMDHMS = (2026, 1, 1, 0, 0, 0)             # os88disk.py's FIXED_DATE
# The FAT short-name characters past A-Z and 0-9: kernel/diskw.inc's dskw_syms,
# which drivers/cdrom/cdiso.inc's cd_syms copies.
SYMS = set(b"$%'-_@~`!(){}^#&")
CD_SPAN = 1022                             # cdiso.inc: sectors that encode
CD_HCDA = 0xFF80


def fail(msg):
    sys.stderr.write("os88cdiso: error: %s\n" % msg)
    sys.exit(1)


# =============================================================================
# THE REFERENCE (SPEC.md 119.3.2, 119.3.3)
# =============================================================================
def legal(c):
    return 65 <= c <= 90 or 48 <= c <= 57 or c in SYMS


def mangle(raw):
    """An ISO 9660 identifier (bytes, `;1` and all) -> its 8.3 name, as
    cd_mangle makes it. The ;version and trailing dots go, a-z is upper-cased,
    and a legal 8.3 name is used as it is; anything else is six legal
    characters of the base, `~`, the stripped name's byte sum mod 36 as one
    character, and three legal characters of the last extension."""
    n = raw.find(b";")
    s = raw if n < 0 else raw[:n]
    s = s.rstrip(b".")
    up = bytes(c - 32 if 97 <= c <= 122 else c for c in s)
    h = sum(up) % 36
    hc = bytes([48 + h if h < 10 else 55 + h])
    dot = up.rfind(b".")
    base, ext = (up, b"") if dot < 0 else (up[:dot], up[dot + 1:])
    if (1 <= len(base) <= 8 and len(ext) <= 3
            and all(legal(c) for c in base) and all(legal(c) for c in ext)):
        out = base + (b"." + ext if ext else b"")
    else:
        lb = bytes(c for c in base if legal(c))[:6] or b"_"
        le = bytes(c for c in ext if legal(c))[:3]
        out = lb + b"~" + hc + (b"." + le if le else b"")
    return out.decode("latin-1")


def pattern(n):
    """A generated file's bytes: never periodic at a sector, so a sector read
    from the wrong place cannot match by accident."""
    return bytes(((i * 7) ^ (i >> 8) ^ (i >> 16)) & 0xFF for i in range(n))


def handle(lba, off, dbase):
    """A directory record's handle: (LBA - base) x 64 + offset / 32, or None
    when its place does not encode."""
    rel = lba - dbase
    if rel < 0 or rel >= CD_SPAN:
        return None
    return (rel << 6) | (off >> 5)


# =============================================================================
# WRITING AN ISO
# =============================================================================
def both16(n):
    return struct.pack("<H", n) + struct.pack(">H", n)


def both32(n):
    return struct.pack("<I", n) + struct.pack(">I", n)


def dir_date():
    y, mo, d, h, mi, s = YMDHMS
    return bytes([y - 1900, mo, d, h, mi, s, 0])


def vol_date():
    y, mo, d, h, mi, s = YMDHMS
    return ("%04d%02d%02d%02d%02d%02d00" % (y, mo, d, h, mi, s)).encode() + b"\0"


def dirent(ident, extent, size, is_dir):
    body = (bytes([0]) + both32(extent) + both32(size) + dir_date()
            + bytes([2 if is_dir else 0, 0, 0]) + both16(1)
            + bytes([len(ident)]) + ident)
    if (len(body) + 1) & 1:
        body += b"\0"                      # padded to even, and the length
    return bytes([len(body) + 1]) + body   # byte counts the pad (9.1.12)


class Node(object):
    def __init__(self, name, parent=None, data=None):
        self.name = name                   # bytes, as written (no ;1)
        self.parent = parent
        self.data = data                   # None = a folder
        self.kids = []
        self.lba = 0
        self.size = 0


def build_tree(files):
    root = Node(b"\0")
    for path, host in files:
        parts = [p.encode("latin-1") for p in path.replace("\\", "/").split("/")
                 if p]
        if not parts:
            fail("an empty path in --file")
        here = root
        for p in parts[:-1]:
            nxt = [k for k in here.kids if k.data is None and k.name == p]
            if nxt:
                here = nxt[0]
            else:
                n = Node(p, here)
                here.kids.append(n)
                here = n
        with open(host, "rb") as f:
            here.kids.append(Node(parts[-1], here, f.read()))
    return root


def ident_of(n):
    return n.name if n.data is None else n.name + b";1"


def dir_records(d):
    """A folder's records, `.` and `..` first, the rest sorted (ISO 9.3)."""
    out = [(b"\0", d), (b"\1", d.parent or d)]
    for k in sorted(d.kids, key=lambda k: ident_of(k)):
        out.append((ident_of(k), k))
    return out


def lay_dir(d):
    """The folder's bytes, in whole sectors: a record never crosses one."""
    secs, cur = [], b""
    for ident, n in dir_records(d):
        r = dirent(ident, n.lba, n.size, n.data is None)
        if len(cur) + len(r) > LBS:
            secs.append(cur.ljust(LBS, b"\0"))
            cur = b""
        cur += r
    secs.append(cur.ljust(LBS, b"\0"))
    return secs


def folders_bfs(root):
    out, q = [], [root]
    while q:
        d = q.pop(0)
        out.append(d)
        q.extend(sorted((k for k in d.kids if k.data is None),
                        key=lambda k: k.name))
    return out


def path_table(dirs, big):
    num = {id(d): i + 1 for i, d in enumerate(dirs)}
    out = b""
    for d in dirs:
        name = b"\0" if d.parent is None else d.name
        par = 1 if d.parent is None else num[id(d.parent)]
        rec = bytes([len(name), 0])
        rec += struct.pack(">I" if big else "<I", d.lba)
        rec += struct.pack(">H" if big else "<H", par)
        rec += name + (b"\0" if len(name) & 1 else b"")
        out += rec
    return out


def make_iso(files, volid=b"CDFIX"):
    root = build_tree(files)
    dirs = folders_bfs(root)
    # sizes are fixed by the sector COUNT, so lay twice: once for the counts
    for d in dirs:
        d.size = len(lay_dir(d)) * LBS
    pt_len = len(path_table(dirs, False))
    pt_secs = max(1, math.ceil(pt_len / LBS))
    lba = 18
    l_pt, lba = lba, lba + pt_secs
    m_pt, lba = lba, lba + pt_secs
    for d in dirs:
        d.lba = lba
        lba += d.size // LBS
    files_in = []

    def walk(d):
        for k in sorted(d.kids, key=lambda k: ident_of(k)):
            if k.data is None:
                walk(k)
            else:
                files_in.append(k)
    walk(root)
    for f in files_in:
        f.size = len(f.data)
        f.lba = lba
        lba += max(1, math.ceil(f.size / LBS))
    total = lba
    img = bytearray(total * LBS)

    def put(at, blob):
        img[at * LBS:at * LBS + len(blob)] = blob

    for d in dirs:
        put(d.lba, b"".join(lay_dir(d)))
    for f in files_in:
        put(f.lba, f.data)
    put(l_pt, path_table(dirs, False))
    put(m_pt, path_table(dirs, True))
    rootrec = dirent(b"\0", root.lba, root.size, True)
    pvd = bytearray(LBS)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b"OS8088".ljust(32)
    pvd[40:72] = volid.ljust(32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(LBS)
    pvd[132:140] = both32(pt_len)
    pvd[140:144] = struct.pack("<I", l_pt)
    pvd[148:152] = struct.pack(">I", m_pt)
    pvd[156:156 + len(rootrec)] = rootrec
    for i in range(190, 813):
        pvd[i] = 0x20
    for off in (813, 830, 847, 864):
        pvd[off:off + 17] = vol_date() if off < 847 else b"0" * 16 + b"\0"
    pvd[881] = 1
    put(16, bytes(pvd))
    term = bytearray(LBS)
    term[0] = 255
    term[1:6] = b"CD001"
    term[6] = 1
    put(17, bytes(term))
    return bytes(img)


# =============================================================================
# READING ONE BACK - the driver's view, written out
# =============================================================================
class Iso(object):
    """An ISO 9660 image read the way cddisc.inc reads it: the PVD at 16, the
    root, and the type-L path table's lowest extent as the handle base."""

    def __init__(self, data, start=0):
        self.d = data
        self.start = start
        vd = start + 16
        while True:
            s = self.sec(vd)
            if s[1:6] != b"CD001":
                fail("no volume descriptor")
            if s[0] == 1:
                break
            if s[0] == 255:
                fail("no primary volume descriptor")
            vd += 1
        self.rlba = struct.unpack_from("<I", s, 158)[0]
        self.rlen = struct.unpack_from("<I", s, 166)[0]
        ptlen = struct.unpack_from("<I", s, 132)[0]
        ptlba = struct.unpack_from("<I", s, 140)[0]
        self.dbase = self.rlba
        if ptlen < 65536:
            pt = b"".join(self.sec(ptlba + i)
                          for i in range(math.ceil(ptlen / LBS)))[:ptlen]
            p = 0
            while p < ptlen and pt[p]:
                ext = struct.unpack_from("<I", pt, p + 2)[0]
                self.dbase = min(self.dbase, ext)
                p += 8 + pt[p] + (pt[p] & 1)

    def sec(self, lba):
        return self.d[lba * LBS:(lba + 1) * LBS]

    def records(self, lba, length):
        """(record bytes, its LBA, its offset), the walk cd_dnext makes."""
        for i in range(math.ceil(length / LBS)):
            s, off = self.sec(lba + i), 0
            while off <= LBS - 34 and s[off] >= 34 and off + s[off] <= LBS:
                yield s[off:off + s[off]], lba + i, off
                off += s[off]

    def listed(self, lba, length):
        """What cd_vnext hands over: not `.`/`..`, hidden or associated, and
        only where the handle encodes."""
        for r, slba, off in self.records(lba, length):
            if r[25] & 5:
                continue
            nl = r[32]
            if nl == 1 and r[33] <= 1:
                continue
            h = handle(slba, off, self.dbase)
            if h is None:
                continue
            name = mangle(r[33:33 + nl])
            isdir = bool(r[25] & 2)
            kind = 2 if isdir else (1 if name.endswith(".O88") else 0)
            yield (name, kind, struct.unpack_from("<I", r, 10)[0], h,
                   struct.unpack_from("<I", r, 2)[0])

    def folder(self, path):
        lba, length = self.rlba, self.rlen
        for part in [p for p in path.replace("\\", "/").split("/") if p]:
            for name, kind, size, h, ext in self.listed(lba, length):
                if name == part.upper() and kind == 2:
                    lba, length = ext, size
                    break
            else:
                fail("no folder %s" % part)
        return lba, length


# =============================================================================
# AN AUDIO DISC - CUE/BIN for 86Box
# =============================================================================
def tone(seconds, hz):
    """Red Book audio: 44.1 kHz, 16-bit stereo, little-endian, whole frames."""
    frames = int(seconds * 75)
    n = frames * 588
    out = bytearray(n * 4)
    for i in range(n):
        v = int(9000 * math.sin(2 * math.pi * hz * i / 44100))
        struct.pack_into("<hh", out, i * 4, v, v)
    return bytes(out)


def make_audio(cue, tones, data=None):
    base = os.path.splitext(cue)[0]
    lines, nt = [], 1
    if data:
        lines += ['FILE "%s" BINARY' % os.path.basename(data),
                  "  TRACK 01 MODE1/2048", "    INDEX 01 00:00:00"]
        nt = 2
    binf = base + ".bin"
    lines.append('FILE "%s" BINARY' % os.path.basename(binf))
    pos = 0
    with open(binf, "wb") as f:
        for i, (s, hz) in enumerate(tones):
            t = nt + i
            lines.append("  TRACK %02d AUDIO" % t)
            if data and i == 0:
                lines.append("    PREGAP 00:02:00")
            m, r = divmod(pos, 75 * 60)
            lines.append("    INDEX 01 %02d:%02d:%02d" % (m, r // 75, r % 75))
            b = tone(s, hz)
            f.write(b)
            pos += len(b) // 2352
    with open(cue, "w") as f:
        f.write("\n".join(lines) + "\n")


# =============================================================================
# SELFCHECK
# =============================================================================
def selfcheck():
    cases = {
        b"README.TXT;1": "README.TXT",
        b"readme.txt;1": "README.TXT",
        b"DOCS": "DOCS",
        b"NOEXT.;1": "NOEXT",
        b"A.B.C;1": None,                  # two dots: mangled
        b"LONG_FILE_NAME.TEXT;1": None,
        b"Long File Name.text;1": None,
        b";1": None,
    }
    bad = 0
    for raw, want in cases.items():
        got = mangle(raw)
        if want is not None and got != want:
            print("mangle(%r) = %r, want %r" % (raw, got, want))
            bad += 1
        if len(got) > 12 or any(not legal(c) for c in got.replace(".", "")
                                .encode()):
            print("mangle(%r) = %r is not a legal 8.3 name" % (raw, got))
            bad += 1
    # the hash character and the shape, by hand: 'A.B.C' sums to 4*... no -
    # spelled out so a change to the rule shows here first
    s = sum(b"A.B.C")
    want = "AB~" + (chr(48 + s % 36) if s % 36 < 10 else chr(55 + s % 36)) + ".C"
    if mangle(b"A.B.C;1") != want:
        print("mangle(A.B.C) = %r, want %r" % (mangle(b"A.B.C;1"), want))
        bad += 1
    import tempfile
    tmp = tempfile.mkdtemp()
    p = os.path.join(tmp, "x")
    with open(p, "wb") as f:
        f.write(b"x" * 5000)
    iso = Iso(make_iso([("DOCS/DEEP/B.TXT", p), ("a.txt", p),
                        ("Long Name.Text", p)]))
    names = sorted(n for n, *_ in iso.listed(iso.rlba, iso.rlen))
    if names != sorted(["A.TXT", "DOCS", mangle(b"Long Name.Text;1")]):
        print("root lists %r" % names)
        bad += 1
    lba, ln = iso.folder("DOCS/DEEP")
    if [n for n, *_ in iso.listed(lba, ln)] != ["B.TXT"]:
        print("DOCS/DEEP does not list B.TXT")
        bad += 1
    hs = [h for *_, h, _e in iso.listed(iso.rlba, iso.rlen)]
    if len(set(hs)) != len(hs) or 0 in hs or max(hs) >= CD_HCDA:
        print("handles %r are not distinct, non-zero and under FF80h" % hs)
        bad += 1
    print("os88cdiso: selfcheck %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--selfcheck":
        sys.exit(selfcheck())
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("iso")
    a.add_argument("-o", "--output", required=True)
    a.add_argument("--volid", default="CDFIX")
    a.add_argument("--file", action="append", default=[], metavar="PATH=HOST")
    a.add_argument("--gen", action="append", default=[], metavar="PATH:SIZE",
                   help="a file of SIZE bytes of pattern(): no host file, and "
                        "the gate computes the same bytes to compare against")
    b = sub.add_parser("audio")
    b.add_argument("-o", "--output", required=True, help="the .cue")
    b.add_argument("--tone", action="append", default=[], metavar="S:HZ")
    b.add_argument("--data", help="an ISO to put first, as track 1")
    c = sub.add_parser("list")
    c.add_argument("image")
    c.add_argument("folder", nargs="?", default="")
    args = ap.parse_args()
    if args.cmd == "iso":
        files = []
        for spec in args.file:
            if "=" not in spec:
                fail("--file wants PATH=HOST, not %r" % spec)
            path, host = spec.split("=", 1)
            files.append((path, host))
        import tempfile
        tmp = tempfile.mkdtemp()
        for spec in args.gen:
            path, size = spec.rsplit(":", 1)
            host = os.path.join(tmp, "gen%d" % len(files))
            with open(host, "wb") as f:
                f.write(pattern(int(size)))
            files.append((path, host))
        img = make_iso(files, args.volid.encode())
        with open(args.output, "wb") as f:
            f.write(img)
        print("os88cdiso: %s - %d files, %d sectors"
              % (args.output, len(files), len(img) // LBS))
    elif args.cmd == "audio":
        tones = []
        for t in args.tone or ["20:440", "15:660", "10:880"]:
            s, hz = t.split(":")
            tones.append((float(s), float(hz)))
        make_audio(args.output, tones, args.data)
        print("os88cdiso: %s - %d audio tracks%s"
              % (args.output, len(tones), " after a data track"
                 if args.data else ""))
    else:
        with open(args.image, "rb") as f:
            iso = Iso(f.read())
        lba, ln = iso.folder(args.folder)
        for name, kind, size, h, _e in iso.listed(lba, ln):
            print("%-12s %d %10d %04X" % (name, kind, size, h))


if __name__ == "__main__":
    main()
