#!/usr/bin/env python3
"""The HTTP drive's host side (SPEC.md 122): publish a tree, check it, serve it.

    python3 tools/os88httpdrv.py export SRC OUTDIR   # SRC: a folder or a FAT12 .img
    python3 tools/os88httpdrv.py check  OUTDIR       # validate OS88.IDX against the tree
    python3 tools/os88httpdrv.py serve  SRC [--port 8088] [--bind 0.0.0.0]
    python3 tools/os88httpdrv.py --selfcheck         # no emulator, no network

IT RUNS OFF THE TREE: copy this file, and os88fat.py beside it when the
source is a .img, to the PC that will serve - Python 3 and nothing else.

HTTP HAS NO DIRECTORY LISTING, so a server that is to look like a drive carries
one file the drive reads first: `OS88.IDX`, the whole tree's names, sizes and
folders in 18-byte records (SPEC.md 122.3). Everything else is a plain static
tree of 8.3 names, and any web server that can send a file can serve it -
`Range:` is used when the server honours it and is not required.

This file is the REFERENCE for the index format: `index_build` writes it and
`index_parse` reads it with every rule HTTPFS.DRV applies, so a fixture this
tool accepts is one the driver accepts and a refusal here names the rule.

`serve` exists because the throwaway server everybody reaches for -
`python3 -m http.server` - ignores `Range:` and answers HTTP/1.0 with chunked
nothing in particular; this one honours ranges, logs every request on one line
(which is how a round trip is counted rather than guessed), and can serve a
built floppy image directly: `serve build/os8088-720.img` exports it into a
temporary folder first.

FILES ARE SERVED EXACTLY AS STORED, compressed ones included. A `'CZ'` file
(SPEC.md 20.14) needs no directory hint to be read right: the read SNIFFS its
own header when there is none (SPEC.md 20.14.6), and a redirected volume -
this drive's - takes that same flow (20.14.6.1), as the driver loader does
(20.14.6.4). So nothing is expanded on the way out, which is fewer bytes on
the wire, and a release served this way is the release's own bytes - the
sizes and sums System Check holds a system disk to (SPEC.md 118.2.1).
"""
import argparse
import http.server
import os
import re
import shutil
import socketserver
import struct
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

IDX_NAME = "OS88.IDX"
IDX_MAGIC = b"OS88IDX\0"
IDX_HDR = 16
IDX_REC = 18
HF_MAXREC = 896                 # HTTPFS.DRV's: one 16 KB claim (SPEC.md 122.3)
A_DIR = 0x10
A_KEEP = 0x07                   # read-only, hidden, system: what FIND reports
# The characters an EXPORT names a file with: FAT's own short-name set less
# lower case. What HTTPFS.DRV ACCEPTS in an index is wider - any printable
# byte, ' '..'~', the range the kernel's listing sanitizer passes (SPEC.md
# 19.1, 122.3) - and NAME_PRINT is that rule, which index_parse applies.
NAME_OK = set(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$%'-_@~`!(){}^#&")
NAME_PRINT = set(range(0x20, 0x7F))


def fail(msg):
    print("os88httpdrv: " + msg, file=sys.stderr)
    sys.exit(1)


def listing_cap():
    """DSK_NENT for kern_big, read out of the kernel the way os88disk.py reads
    it - and 64 when the kernel's source is not beside this file.

    kern_big's and not kern_small's: the HTTP drive is a driver, and only
    kern_big has a driver layer (SPEC.md 122.1). The fallback matters
    because this tool is meant to be COPIED OFF the tree onto whatever PC
    serves the files, where there is no kernel/ to read - and falling back
    to kern_small's 32 refused the stock 360KB system disk's 35-entry root,
    which the drive lists perfectly well.
    """
    try:
        with open(os.path.join(HERE, "..", "kernel", "dskwin.inc")) as f:
            m = re.findall(r"^DSK_NENT\s+equ\s+(\d+)", f.read(), re.M)
        if m:
            return int(m[0])
    except OSError:
        pass
    return 64


def warn(msg):
    print("os88httpdrv: warning: " + msg, file=sys.stderr)


def raw11(name):
    """'FOO.BAR' -> the 11 FAT bytes, or None if it is not an 8.3 name."""
    up = name.upper()
    base, dot, ext = up.partition(".")
    if not base or len(base) > 8 or len(ext) > 3 or "." in ext:
        return None
    b = (base.ljust(8) + ext.ljust(3)).encode("latin1", "replace")
    if any(c not in NAME_OK and c != 0x20 for c in b):
        return None
    if b[0] == 0x20:
        return None
    return b


def pretty(r11):
    base = r11[:8].decode("latin1").rstrip()
    ext = r11[8:].decode("latin1").rstrip()
    return base + ("." + ext if ext else "")


# --- the index ---------------------------------------------------------------
def index_build(tree, serial=0, attrs=None):
    """tree: {name: bytes | dict} -> the OS88.IDX bytes. attrs: {'APPS/X.O88':
    the FAT attribute's read-only, hidden and system bits}, which the drive
    hands FIND (SPEC.md 62.9.17.1) - the installer copies a hidden + system
    file through the _SYS verbs, and KERNEL.SYS with no bits is refused.

    Breadth first, each folder's children CONTIGUOUS and sorted by name, so a
    folder's listing is a slice (first, count) and every parent sits below its
    children - which is what lets the driver validate in one pass and enumerate
    by ordinal with no walk (SPEC.md 62.9.1's stable ordinal, by construction).
    """
    cap = listing_cap()
    recs = [[b" " * 11, A_DIR, 0, 0, 0]]      # name, attr, parent, a, b
    queue = [(0, tree, "/")]
    while queue:
        idx, node, where = queue.pop(0)
        names = sorted(node)
        limit = cap if idx == 0 else cap - 1  # a subfolder lists `..` too
        if len(names) > limit:
            # A WARNING AND NOT A REFUSAL: the drive's listing stops cleanly
            # at the kernel's cap (OSAPI_FS_ENT answers "full"), so the rest
            # of the tree is still served and every file is still reachable
            # by name - which is the case for a whole release, where the
            # folder that overflows may not be the one anybody browses
            warn("%s holds %d entries and a Disk window lists the first %d"
                 % (where, len(names), limit))
        first = len(recs)
        recs[idx][3], recs[idx][4] = first if names else 0, len(names)
        for n in names:
            r = raw11(n)
            if r is None:
                fail("%s%s is not an 8.3 name the drive can list" % (where, n))
            child = node[n]
            a = (attrs or {}).get((where + n)[1:], 0) & A_KEEP
            if isinstance(child, dict):
                queue.append((len(recs), child, where + n + "/"))
                recs.append([r, A_DIR | a, idx, 0, 0])
            else:
                if len(child) > 0xFFFFFFFF:
                    fail("%s%s is over 4GB" % (where, n))
                recs.append([r, a, idx, len(child) & 0xFFFF, len(child) >> 16])
    if len(recs) > HF_MAXREC:
        fail("%d records, and HTTPFS.DRV holds %d (one 16 KB claim)" % (len(recs), HF_MAXREC))
    out = bytearray(IDX_MAGIC + struct.pack("<HHI", len(recs), 0, serial))
    for r, attr, parent, a, b in recs:
        out += r + bytes([attr]) + struct.pack("<HHH", parent, a, b)
    return bytes(out)


def index_parse(blob):
    """OS88.IDX -> [(name11, attr, parent, size_or_first, count)], or raise
    ValueError naming the rule HTTPFS.DRV would refuse the index on."""
    if len(blob) < IDX_HDR or blob[:8] != IDX_MAGIC:
        raise ValueError("no OS88IDX magic")
    n, rsv, _ = struct.unpack_from("<HHI", blob, 8)
    if rsv != 0:
        raise ValueError("reserved word is not 0 (a later version of the format)")
    if not 1 <= n <= HF_MAXREC:
        raise ValueError("record count %d outside 1..%d" % (n, HF_MAXREC))
    if len(blob) != IDX_HDR + n * IDX_REC:
        raise ValueError("length %d is not 16 + 18 x %d" % (len(blob), n))
    recs = []
    for i in range(n):
        o = IDX_HDR + i * IDX_REC
        name = blob[o:o + 11]
        attr = blob[o + 11]
        parent, a, b = struct.unpack_from("<HHH", blob, o + 12)
        recs.append((name, attr, parent, a, b))
    if not recs[0][1] & A_DIR or recs[0][2] != 0:
        raise ValueError("record 0 is not the root folder")
    for i, (name, attr, parent, a, b) in enumerate(recs):
        if attr & A_DIR:
            first, count = a, b
            if count and (first < 1 or first + count > n):
                raise ValueError("folder %d's children run past the table" % i)
            for c in range(first, first + count) if count else ():
                if recs[c][2] != i:
                    raise ValueError("record %d is in folder %d's run and names %d"
                                     % (c, i, recs[c][2]))
        if i == 0:
            continue
        if parent >= i:
            raise ValueError("record %d's parent %d is not below it" % (i, parent))
        pr = recs[parent]
        if not pr[1] & A_DIR or not pr[3] <= i < pr[3] + pr[4]:
            raise ValueError("record %d is not inside its parent's run" % i)
        if name[0] == 0x20 or any(c not in NAME_PRINT for c in name):
            raise ValueError("record %d's name %r has a character the drive refuses" % (i, name))
    return recs


def index_paths(recs):
    """record index -> its path below the prefix ('APPS/CALC.O88')."""
    out = {0: ""}
    for i in range(1, len(recs)):
        p = out[recs[i][2]]
        out[i] = (p + "/" if p else "") + pretty(recs[i][0])
    return out


# --- sources -------------------------------------------------------------------
def attrs_from_idx(path):
    """A folder that already carries an OS88.IDX (an export, uploaded) keeps
    the attributes it was exported with: a host file system has nowhere else
    to hold FAT's hidden and system bits."""
    try:
        recs = index_parse(open(os.path.join(path, IDX_NAME), "rb").read())
    except (OSError, ValueError):
        return {}
    return {p: recs[i][1] & A_KEEP for i, p in index_paths(recs).items()
            if i and recs[i][1] & A_KEEP}


def tree_from_dir(path):
    t = {}
    for n in sorted(os.listdir(path)):
        if n == IDX_NAME:
            continue
        p = os.path.join(path, n)
        if os.path.isdir(p):
            t[n.upper()] = tree_from_dir(p)
        else:
            t[n.upper()] = open(p, "rb").read()
    return t


def tree_from_img(path, attrs=None):
    """A FAT12 floppy image, whole: every folder walked. attrs, when given,
    collects each entry's read-only, hidden and system bits by path."""
    from os88fat import Fat12
    v = Fat12(path)
    csz = v.spc * v.bps

    def body(first, size):
        if not first:
            return b""
        return b"".join(bytes(v.img[v.cluster_off(c):v.cluster_off(c) + csz])
                        for c in v.chain(first))[:size]

    def walk(raw, where=""):
        t = {}
        for o in range(0, len(raw), 32):
            e = raw[o:o + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] & 0x08 or e[0] == 0x2E:
                continue                        # deleted, label, . and ..
            name = Fat12.pretty(e[:11])
            first = struct.unpack_from("<H", e, 26)[0]
            if attrs is not None and e[11] & A_KEEP:
                attrs[where + name] = e[11] & A_KEEP
            if e[11] & A_DIR:
                d = b"".join(bytes(v.img[v.cluster_off(c):v.cluster_off(c) + csz])
                             for c in v.chain(first)) if first else b""
                t[name] = walk(d, where + name + "/")
            else:
                t[name] = body(first, struct.unpack_from("<I", e, 28)[0])
        return t

    return walk(bytes(v.img[v.root_off:v.root_off + v.nroot * 32]))


def load(src):
    """-> (tree, attrs)"""
    if os.path.isdir(src):
        return tree_from_dir(src), attrs_from_idx(src)
    attrs = {}
    return tree_from_img(src, attrs), attrs


def write_tree(tree, out):
    for n, b in tree.items():
        p = os.path.join(out, n)
        if isinstance(b, dict):
            os.makedirs(p, exist_ok=True)
            write_tree(b, p)
        else:
            with open(p, "wb") as f:
                f.write(b)


def export(src, out):
    tree, attrs = load(src)
    serial = 0
    idx = index_build(tree, serial, attrs)
    os.makedirs(out, exist_ok=True)
    write_tree(tree, out)
    with open(os.path.join(out, IDX_NAME), "wb") as f:
        f.write(idx)
    n = struct.unpack_from("<H", idx, 8)[0]
    print("os88httpdrv: %s -> %s: %d records, %s %d bytes"
          % (src, out, n, IDX_NAME, len(idx)))
    return out


def check(out):
    blob = open(os.path.join(out, IDX_NAME), "rb").read()
    recs = index_parse(blob)
    for i, p in index_paths(recs).items():
        if i == 0:
            continue
        f = os.path.join(out, *p.split("/"))
        name, attr, _, a, b = recs[i]
        if attr & A_DIR:
            if not os.path.isdir(f):
                raise ValueError("%s is a folder in the index and not on disk" % p)
        else:
            if not os.path.isfile(f):
                raise ValueError("%s is in the index and not on disk" % p)
            size = a | (b << 16)
            if os.path.getsize(f) != size:
                raise ValueError("%s is %d bytes and the index says %d"
                                 % (p, os.path.getsize(f), size))
    print("os88httpdrv: %s: %d records, every one present at its size" % (out, len(recs)))


# --- serve ---------------------------------------------------------------------
class Handler(http.server.SimpleHTTPRequestHandler):
    """Static files with Range, one log line a request - and the connection
    KEPT when the client asks (HTTPFS.DRV does, SPEC.md 122.2.1): http.server
    honours `Connection: keep-alive` only at protocol HTTP/1.1, and closes an
    idle one after `timeout` seconds, as a real server does."""
    protocol_version = "HTTP/1.1"
    timeout = 15

    def log_message(self, fmt, *a):
        sys.stderr.write("os88httpdrv: %s:%d %s\n" % (self.client_address[0],
                         self.client_address[1], fmt % a))

    def do_GET(self):
        path = self.translate_path(self.path)
        if not os.path.isfile(path):
            self.send_error(404)
            return
        size = os.path.getsize(path)
        rng = self.headers.get("Range")
        lo, hi, code = 0, size - 1, 200
        if rng and getattr(self.server, "ranges", True):
            m = re.fullmatch(r"\s*bytes=(\d+)-(\d*)\s*", rng)
            if m:
                lo = int(m.group(1))
                hi = int(m.group(2)) if m.group(2) else size - 1
                hi = min(hi, size - 1)
                if lo > hi:
                    self.send_response(416)
                    self.send_header("Content-Range", "bytes */%d" % size)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                code = 206
        self.send_response(code)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(hi - lo + 1))
        if code == 206:
            self.send_header("Content-Range", "bytes %d-%d/%d" % (lo, hi, size))
        self.send_header("Accept-Ranges", "bytes")
        if not self.close_connection:   # a 1.0 client's connection is kept
            # only when the reply SAYS so (RFC 7230 6.3), and HTTPFS.DRV asks
            # in 1.0 and reads nothing else (SPEC.md 122.2.1). http.server
            # keeps it on the request's word and does not answer in kind
            self.send_header("Connection", "keep-alive")
        self.end_headers()
        with open(path, "rb") as f:
            f.seek(lo)
            left = hi - lo + 1
            while left:
                chunk = f.read(min(left, 16384))
                if not chunk:
                    break
                self.wfile.write(chunk)
                left -= len(chunk)

    do_HEAD = None


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def serve(src, port, bind, ranges=True, ready=None):
    root = src
    tmp = None
    if not os.path.isdir(src) or not os.path.isfile(os.path.join(src, IDX_NAME)):
        tmp = tempfile.mkdtemp(prefix="os88httpdrv-")
        root = export(src, tmp)
    check(root)
    handler = lambda *a, **k: Handler(*a, directory=root, **k)   # noqa: E731
    httpd = Server((bind, port), handler)
    httpd.ranges = ranges
    print("os88httpdrv: serving %s on http://%s:%d/  (Ranges %s)"
          % (root, bind, httpd.server_address[1], "on" if ranges else "IGNORED"))
    sys.stdout.flush()
    if ready:
        ready(httpd)
    try:
        httpd.serve_forever()
    finally:
        httpd.server_close()
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)


# --- selfcheck -----------------------------------------------------------------
def selfcheck():
    tree = {"README.TXT": b"hello\n", "APPS": {"A.O88": b"x" * 700, "B.TXT": b""},
            "EMPTY": {}, "DEEP": {"ONE": {"TWO": {"F.BIN": bytes(range(256)) * 9}}}}
    idx = index_build(tree)
    recs = index_parse(idx)
    paths = index_paths(recs)
    assert sorted(p for p in paths.values() if p) == sorted([
        "APPS", "DEEP", "EMPTY", "README.TXT", "APPS/A.O88", "APPS/B.TXT",
        "DEEP/ONE", "DEEP/ONE/TWO", "DEEP/ONE/TWO/F.BIN"]), paths
    # the attributes ride the index, and only the three FIND reports
    ia = index_parse(index_build(tree, 0, {"README.TXT": 0x27, "APPS/B.TXT": 6}))
    got = {p: ia[i][1] for i, p in index_paths(ia).items() if i}
    assert got["README.TXT"] == 0x07 and got["APPS/B.TXT"] == 6, got
    assert got["APPS"] == A_DIR and got["APPS/A.O88"] == 0, got
    # every refusal the driver makes is one this reader makes
    bad = bytearray(idx)
    bad[0] = ord("X")
    for blob, why in ((bytes(bad), "magic"), (idx[:-1], "length"),
                      (idx[:8] + struct.pack("<H", 0) + idx[10:], "count")):
        try:
            index_parse(blob)
        except ValueError:
            continue
        raise AssertionError("a broken %s was accepted" % why)
    # a child naming the wrong parent
    b2 = bytearray(idx)
    struct.pack_into("<H", b2, IDX_HDR + 1 * IDX_REC + 12, 3)
    try:
        index_parse(bytes(b2))
        raise AssertionError("a bad parent was accepted")
    except ValueError:
        pass
    # a name with a byte the font has no glyph for
    b3 = bytearray(idx)
    b3[IDX_HDR + 1 * IDX_REC] = 0x80
    try:
        index_parse(bytes(b3))
        raise AssertionError("a name past '~' was accepted")
    except ValueError:
        pass
    assert raw11("toolongname.txt") is None and raw11("A.B.C") is None
    assert raw11("calc.o88") == b"CALC    O88"
    # serve round trip, with and without ranges
    import urllib.request
    d = tempfile.mkdtemp(prefix="os88httpdrv-sc-")
    try:
        write_tree(tree, d)
        open(os.path.join(d, IDX_NAME), "wb").write(idx)
        check(d)
        open(os.path.join(d, IDX_NAME), "wb").write(
            index_build(tree, 0, {"APPS/B.TXT": 6}))
        assert attrs_from_idx(d) == {"APPS/B.TXT": 6}   # an upload re-exported
        open(os.path.join(d, IDX_NAME), "wb").write(idx)
        for ranges in (True, False):
            box = {}
            ev = threading.Event()
            t = threading.Thread(target=serve, args=(d, 0, "127.0.0.1", ranges,
                                 lambda h: (box.update(h=h), ev.set())), daemon=True)
            t.start()
            ev.wait(5)
            port = box["h"].server_address[1]
            r = urllib.request.Request("http://127.0.0.1:%d/DEEP/ONE/TWO/F.BIN" % port,
                                       headers={"Range": "bytes=10-19"})
            with urllib.request.urlopen(r) as resp:
                body = resp.read()
                if ranges:
                    assert resp.status == 206 and body == bytes(range(10, 20)), body
                else:
                    assert resp.status == 200 and len(body) == 256 * 9
            box["h"].shutdown()
    finally:
        shutil.rmtree(d, ignore_errors=True)
    print("os88httpdrv: selfcheck ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--selfcheck", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    e = sub.add_parser("export", help="write a tree and its OS88.IDX")
    e.add_argument("src")
    e.add_argument("out")
    c = sub.add_parser("check", help="validate OS88.IDX against its tree")
    c.add_argument("out")
    s = sub.add_parser("serve", help="serve a tree (or a .img, exported first)")
    s.add_argument("src")
    s.add_argument("--port", type=int, default=8088)
    s.add_argument("--bind", default="0.0.0.0")
    s.add_argument("--no-range", action="store_true",
                   help="ignore Range: like a server that cannot do it")
    a = ap.parse_args()
    if a.selfcheck:
        selfcheck()
    elif a.cmd == "export":
        export(a.src, a.out)
    elif a.cmd == "check":
        try:
            check(a.out)
        except ValueError as ex:
            fail(str(ex))
    elif a.cmd == "serve":
        serve(a.src, a.port, a.bind, not a.no_range)
    else:
        ap.print_help()


if __name__ == "__main__":
    main()
