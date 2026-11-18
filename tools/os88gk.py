#!/usr/bin/env python3
"""os88gk.py - the game kit's SCENE compiler (SPEC.md 132.6, 132.7).

A scene is a screen's static decoration stored as DATA in an art record and
drawn by the kit's one resident interpreter, gk_scene (apps/cc/gk.inc). This
tool is the only writer of the record format.

    compile_scenes(paths, symbols) -> {id: bytes}   the build interface (a
        package's art tool calls it; `symbols` maps every #define NAME <int>
        of its art headers, so sprites, fonts and scene ids are names)
    os88gk.py --selfcheck                 compile a sample, decode, check
    os88gk.py dump FILE [--sym HEADER..]  print each scene's ops

Refusals raise GkError naming the file and line (132.7).
"""
import argparse, os, re, struct, sys

MAGIC = b"S"
MAXREC = 4096
ALLROWS = (-16384, 16383)       # an op whose rows come from a slot
DEFFONTH = 24                   # a font with no `fonth`
SPRH = 255                      # a sprite's rows are not known here


class GkError(Exception):
    pass


# op name -> (number, argument kinds). Kinds: x y (origin added), w h, s (a
# string), n (any number). Order is gk.h's.
OPS = {
    "box": (0, "xywhnn"),           # style fill
    "hline": (1, "xywn"),           # raised
    "vline": (2, "xyhn"),
    "shadow": (3, "xywhn"),         # d
    "text": (4, "xywsnnn"),         # s colour font flags
    "textbox": (5, "xywhsnnn"),
    "panel": (6, "xywhnsnn"),       # bar title font style
    "titlebar": (7, "xywhsnn"),     # s font flags
    "button": (8, "xywhsnn"),       # label font state
    "keycap": (9, "xywhsn"),
    "tab": (10, "xywhsnn"),         # label font active
    "check": (11, "xywnn"),         # size on tick
    "radio": (12, "xywn"),          # size on
    "drop": (13, "xywhsn"),
    "banner": (14, "xywhsnn"),      # s font fill
    "arrow": (15, "xywhnn"),        # dir colour
    "sprshadow": (16, "xynn"),      # id d
    "spr": (17, "xyn"),             # id
    "fill": (18, "xywhn"),          # colour            (x y w h -> corners)
    "dither": (19, "xywhnnn"),      # c1 c2 pat
    "dots": (20, "xywhnn"),         # step colour
}
OPNAMES = {v[0]: k for k, v in OPS.items()}
CORNERS = {"fill", "dither", "dots"}
# where each op's font is, for the row range (argument index)
FONTARG = {"text": 5, "textbox": 6, "panel": 6, "titlebar": 5, "button": 5,
           "keycap": 5, "tab": 5, "drop": 5, "banner": 5}
SHADOWED = {"button"}           # always casts GK_DROPW

ROLES = ["OUTLINE", "FACE", "LIGHT", "SHADE", "FIELD", "TEXT", "TITLE",
         "TITLELT", "TITLETX", "FOCUS", "FOCUSTX", "HILITE", "HILITETX",
         "DISABLED", "DROP", "KEY"]
COLOURS = ["black", "blue", "green", "cyan", "red", "magenta", "brown",
           "lightgrey", "darkgrey", "lightblue", "lightgreen", "lightcyan",
           "lightred", "lightmagenta", "yellow", "white"]
KIT = {
    "GK_FLAT": 0, "GK_RAISED": 1, "GK_RAISED2": 2, "GK_SUNKEN": 3,
    "GK_SUNKEN2": 4, "GK_ETCHED": 5, "GK_ROUND": 0x10, "GK_SHADOW": 0x20,
    "GK_DROPW": 3, "GK_LEFT": 0, "GK_CENTRE": 1, "GK_RIGHT": 2,
    "GK_EMBOSS": 0x10, "GK_FIT": 0x20, "GK_B_NORMAL": 0, "GK_B_FOCUS": 1,
    "GK_B_DEFAULT": 2, "GK_B_DISABLED": 3, "GK_B_PRESSED": 4, "GK_B_HILITE": 5,
}
KIT.update({f"GK_R_{r}": i for i, r in enumerate(ROLES)})
ALIAS = {
    "flat": 0, "raised": 1, "raised2": 2, "sunken": 3, "sunken2": 4,
    "etched": 5, "round": 0x10, "shadow": 0x20, "left": 0, "centre": 1,
    "center": 1, "right": 2, "emboss": 0x10, "fit": 0x20, "normal": 0,
    "focus": 1, "default": 2, "disabled": 3, "pressed": 4, "hilite": 5,
}
ALIAS.update({c: i for i, c in enumerate(COLOURS)})
ALIAS["lightgray"], ALIAS["darkgray"] = 7, 8

TOKEN = re.compile(r'"(?:[^"\\]|\\.)*"|\S+')


class Val:
    """One compiled argument: its bytes, and its value when constant."""
    def __init__(self, data, const=None, string=False):
        self.data, self.const, self.string = data, const, string


def enc_num(n, where):
    if not -32768 <= n <= 65535:
        raise GkError(f"{where}: {n} does not fit 16 bits")
    if 0 <= n <= 127:
        return bytes([n])
    if -32 <= n <= -1:
        return bytes([n & 0xFF])
    return b"\xC0" + struct.pack("<H", n & 0xFFFF)


def parse_int(tok):
    try:
        return int(tok, 0)
    except ValueError:
        return None


class Compiler:
    def __init__(self, symbols):
        self.sym = dict(symbols)
        self.fonth = {}

    def name(self, tok, where):
        n = parse_int(tok)
        if n is not None:
            return n
        if tok in self.sym:
            return self.sym[tok]
        if tok in KIT:
            return KIT[tok]
        if tok in ALIAS:
            return ALIAS[tok]
        raise GkError(f"{where}: unknown name {tok!r}")

    def arg(self, tok, kind, where):
        if tok.startswith('"'):
            if kind != "s":
                raise GkError(f"{where}: a string {tok} where a number goes")
            return Val(self.literal(tok[1:-1], where), string=True)
        m = re.fullmatch(r"([%$])(\d+)", tok)
        if m:
            n = int(m.group(2))
            if n > 31:
                raise GkError(f"{where}: slot {tok}: 31 is the last")
            if (m.group(1) == "$") != (kind == "s"):
                raise GkError(f"{where}: {tok} where a {'string' if kind == 's' else 'number'} goes")
            return Val(bytes([(0xA0 if m.group(1) == "$" else 0x80) + n]), string=kind == "s")
        if kind == "s":
            raise GkError(f"{where}: {tok} where a string goes (\"...\" or $n)")
        if tok.startswith("@"):
            if tok[1:] not in ROLES:
                raise GkError(f"{where}: unknown role {tok}")
            return Val(bytes([0xC2, ROLES.index(tok[1:])]))
        v = 0
        for part in tok.split("|"):
            v |= self.name(part, where)
        if "|" in tok and v < 0:
            raise GkError(f"{where}: {tok}: a negative in a set")
        return Val(enc_num(v, where), const=v)

    def literal(self, text, where):
        out, i, n = bytearray(b"\xC1"), 0, 0
        while i < len(text):
            c = text[i]
            if c == "\\" and i + 1 < len(text):
                out.append(ord(text[i + 1]))
                i += 2
            elif c == "$" and text[i + 1:i + 2] == "$":
                out.append(ord("$"))
                i += 2
            elif c == "$":
                m = re.match(r"\$(\d+)", text[i:])
                if not m or int(m.group(1)) > 31:
                    raise GkError(f"{where}: bad slot in \"{text}\" ($0..$31, $$ for $)")
                out.append(int(m.group(1)) + 1)
                i += len(m.group(0))
            else:
                if not 32 <= ord(c) < 127:
                    raise GkError(f"{where}: {c!r}: printable ASCII only")
                out.append(ord(c))
                i += 1
            n += 1
        if n > 63:
            raise GkError(f"{where}: a literal is 63 characters at most")
        return bytes(out + b"\0")

    def rows(self, op, a, where):
        """The rows the op can touch, relative, inclusive."""
        c = lambda i: a[i].const if i < len(a) else None     # noqa: E731
        y = c(1)
        kinds = OPS[op][1]
        hgt = c(kinds.index("h")) if "h" in kinds else None
        if op in ("check", "radio"):
            hgt = c(2)
        if op == "hline":
            hgt = 2
        if y is None or (("h" in kinds or op in ("check", "radio")) and hgt is None):
            return ALLROWS
        if op in ("spr", "sprshadow"):
            d = c(3) if op == "sprshadow" else 0
            return (y, y + SPRH + (d or 0)) if d is not None else ALLROWS
        top, bot = y, y + hgt - 1 if op != "text" else y
        if op == "box" or op == "panel":
            st = c(4) if op == "box" else c(7)
            if st is None or st & 0x20:
                bot += 3
        if op in SHADOWED:
            bot += 3
        if op == "shadow":
            d = c(4)
            if d is None:
                return ALLROWS
            bot += max(d, 0)
        if op == "tab":
            top -= 2
        if op == "check":
            t = c(4)
            if t is None or t > 0:
                top, bot = top - 128, bot + 128
        if op in FONTARG:
            f = c(FONTARG[op])
            fh = 8 if f is not None and f < 0 else self.fonth.get(f, DEFFONTH)
            ty = y if op == "text" else y + ((hgt - fh) >> 1)
            top, bot = min(top, ty), max(bot, ty + fh)
        return (top, bot)

    def rect(self, op, a, where):
        c = [v.const for v in a]
        if op in ("text", "spr", "sprshadow"):
            raise GkError(f"{where}: `{op}` has no rect a tag can name")
        if op in ("check", "radio"):
            r = (c[0], c[1], c[2], c[2])
        elif op == "hline":
            r = (c[0], c[1], c[2], 2)
        elif op == "vline":
            r = (c[0], c[1], 2, c[2])
        else:
            r = tuple(c[:4])
        if any(v is None for v in r):
            raise GkError(f"{where}: a tagged op's rect must be constant")
        return r

    def op(self, toks, cond, where):
        name = toks[0]
        if name not in OPS:
            raise GkError(f"{where}: unknown op {name!r}")
        num, kinds = OPS[name]
        tag = None
        if toks[-1].startswith("#") and len(toks[-1]) > 1:
            tag = self.name(toks[-1][1:], where)
            if not 0 <= tag <= 255:
                raise GkError(f"{where}: tag {tag}: 0..255")
            toks = toks[:-1]
        if len(toks) - 1 != len(kinds):
            raise GkError(f"{where}: `{name}` takes {len(kinds)} arguments "
                          f"({' '.join(kinds)}), not {len(toks) - 1}")
        a = [self.arg(t, k, where) for t, k in zip(toks[1:], kinds)]
        top, bot = self.rows(name, a, where)
        rect = self.rect(name, a, where) if tag is not None else None
        if name in CORNERS:
            x, y, w, h = (v.const for v in a[:4])
            if None in (x, y, w, h):
                raise GkError(f"{where}: `{name}`'s x y w h must be constants")
            a[2] = Val(enc_num(x + w - 1, where), x + w - 1)
            a[3] = Val(enc_num(y + h - 1, where), y + h - 1)
        if len(a) > 10:
            raise GkError(f"{where}: ten arguments at most")
        body = bytearray()
        body.append(num | (0x80 if cond else 0))
        body += struct.pack("<hh", top, bot)
        if cond:
            body += bytes(cond)
        for v in a:
            body += v.data
        if len(body) + 1 > 255:
            raise GkError(f"{where}: an op is 255 bytes at most")
        return bytes([len(body) + 1]) + bytes(body), tag, rect

    def cond(self, toks, where):
        """`if %n` / `if !%n` / `if %n=k` / `if %n!=k` -> (c, k)"""
        if len(toks) < 2:
            raise GkError(f"{where}: `if` wants a slot")
        m = re.fullmatch(r"(!?)%(\d+)(?:(!?=)(-?\w+))?", toks[1])
        if not m:
            raise GkError(f"{where}: `if {toks[1]}`: if %n, if !%n, if %n=k, if %n!=k")
        n = int(m.group(2))
        if n > 31:
            raise GkError(f"{where}: slot %{n}: 31 is the last")
        neg, rel = m.group(1) == "!", m.group(3)
        if neg and rel:
            raise GkError(f"{where}: `if !%n=k` is not a form")
        k = self.name(m.group(4), where) if rel else 0
        if not -128 <= k <= 127:
            raise GkError(f"{where}: if ... ={k}: -128..127")
        eq = neg or rel == "="
        return [n | (0x20 if eq else 0), k & 0xFF], toks[2:]

    def compile_file(self, path, out):
        lines = open(path).read().split("\n")
        cur, block = None, None
        for i, line in enumerate(lines, 1):
            where = f"{path}:{i}"
            s = line.strip()
            if not s or s.startswith("#") and (len(s) == 1 or s[1] in " #!"):
                continue
            s = re.sub(r"\s#\s.*$", "", s)
            toks = TOKEN.findall(s)
            if toks[0] == "fonth":
                if len(toks) != 3:
                    raise GkError(f"{where}: fonth FONT ROWS")
                self.fonth[self.name(toks[1], where)] = self.name(toks[2], where)
                continue
            if toks[0] == "scene":
                if cur:
                    raise GkError(f"{where}: `scene` inside scene {cur['name']} (no `end`)")
                if len(toks) != 4:
                    raise GkError(f"{where}: scene ID W H")
                sid = self.name(toks[1], where)
                if sid in out:
                    raise GkError(f"{where}: scene {toks[1]} ({sid}) twice")
                cur = {"name": toks[1], "id": sid, "w": self.name(toks[2], where),
                       "h": self.name(toks[3], where), "ops": b"", "tags": {}}
                continue
            if cur is None:
                raise GkError(f"{where}: {toks[0]!r} outside a scene")
            if toks == ["end"]:
                if block is not None:
                    raise GkError(f"{where}: `end` inside an `if` block")
                out[cur["id"]] = self.record(cur, where)
                cur = None
                continue
            if toks == ["}"]:
                if block is None:
                    raise GkError(f"{where}: `}}` with no block")
                block = None
                continue
            cond = block
            if toks[0] == "if":
                if block is not None:
                    raise GkError(f"{where}: blocks do not nest")
                cond, toks = self.cond(toks, where)
                if toks == ["{"]:
                    block = cond
                    continue
                if not toks:
                    raise GkError(f"{where}: `if` with no op")
            data, tag, rect = self.op(toks, cond, where)
            if tag is not None:
                if tag in cur["tags"]:
                    raise GkError(f"{where}: tag {tag} twice in scene {cur['name']}")
                cur["tags"][tag] = rect
            cur["ops"] += data
        if cur:
            raise GkError(f"{path}: scene {cur['name']} has no `end`")

    def record(self, cur, where):
        rec = bytearray(MAGIC + struct.pack("<HH", cur["w"] & 0xFFFF, cur["h"] & 0xFFFF))
        rec.append(len(cur["tags"]))
        for t, r in sorted(cur["tags"].items()):
            rec += bytes([t]) + struct.pack("<hhhh", *r)
        rec += cur["ops"] + b"\0"
        if len(rec) > MAXREC:
            raise GkError(f"{where}: scene {cur['name']} is {len(rec)} bytes, over {MAXREC}")
        return bytes(rec)


def compile_scenes(paths, symbols):
    """Every `scene` in the files -> {id: record bytes} (SPEC.md 132.7)."""
    c, out = Compiler(symbols), {}
    for p in paths:
        c.compile_file(p, out)
    return out


def read_symbols(paths):
    sym = {}
    for p in paths:
        for m in re.finditer(r"^#define\s+(\w+)\s+(-?(?:0x[0-9A-Fa-f]+|\d+))\s*(?:/\*.*)?$",
                             open(p).read(), re.M):
            sym[m.group(1)] = int(m.group(2), 0)
    return sym


# ---- the reader (dump, selfcheck) ------------------------------------------
def decode(rec):
    """record -> (w, h, {tag: rect}, [(op, top, bot, cond, [args])]); an arg is
    ('n', v) | ('%', n) | ('$', n) | ('lit', text) | ('@', role)."""
    if rec[:1] != MAGIC:
        raise GkError("not a scene record")
    w, h, nt = struct.unpack_from("<HHB", rec, 1)
    tags, p = {}, 6
    for _ in range(nt):
        t = rec[p]
        tags[t] = struct.unpack_from("<hhhh", rec, p + 1)
        p += 9
    ops = []
    while rec[p]:
        end = p + rec[p]
        op = rec[p + 1]
        top, bot = struct.unpack_from("<hh", rec, p + 2)
        q, cond = p + 6, None
        if op & 0x80:
            cond = (rec[q], struct.unpack_from("<b", rec, q + 1)[0])
            q += 2
        args = []
        while q < end:
            b = rec[q]
            q += 1
            if b < 0x80:
                args.append(("n", b))
            elif b >= 0xE0:
                args.append(("n", b - 256))
            elif b < 0xA0:
                args.append(("%", b - 0x80))
            elif b < 0xC0:
                args.append(("$", b - 0xA0))
            elif b == 0xC0:
                args.append(("n", struct.unpack_from("<h", rec, q)[0]))
                q += 2
            elif b == 0xC1:
                z = rec.index(0, q)
                s = "".join(f"${c - 1}" if c < 32 else chr(c) for c in rec[q:z])
                args.append(("lit", s))
                q = z + 1
            elif b == 0xC2:
                args.append(("@", rec[q]))
                q += 1
            else:
                raise GkError(f"bad argument code {b:02X}h")
        if q != end:
            raise GkError("an op's arguments overrun its length")
        ops.append((op & 0x3F, top, bot, cond, args))
        p = end
    return w, h, tags, ops


def fmt_arg(a):
    k, v = a
    if k == "@":
        return f"@{ROLES[v]}" if v < 16 else f"@{v}"
    if k == "lit":
        return f'"{v}"'
    return str(v) if k == "n" else f"{k}{v}"


def dump(recs, names=None):
    for sid, rec in sorted(recs.items()):
        w, h, tags, ops = decode(rec)
        print(f"scene {names.get(sid, sid) if names else sid} {w} {h}   "
              f"# {len(rec)} bytes, {len(ops)} ops")
        for t, r in sorted(tags.items()):
            print(f"  # tag {t}: x {r[0]} y {r[1]} w {r[2]} h {r[3]}")
        for op, top, bot, cond, args in ops:
            c = ""
            if cond:
                c = f"if %{cond[0] & 31}{'=' if cond[0] & 0x20 else '!='}{cond[1]} "
            print(f"  {c}{OPNAMES.get(op, op)} {' '.join(map(fmt_arg, args))}"
                  f"   # rows {top}..{bot}")
        print("end")


SAMPLE = r'''
# selfcheck sample
fonth FONT13 13
scene SC_TEST 320 200
  panel 0 0 320 200 16 "TITLE $0" FONT13 raised|shadow
  box 8 24 100 40 sunken2 @FIELD
  textbox 8 24 100 40 $1 black FONT13 centre|fit
  button 8 170 96 20 "[B] Buy" FONT13 %2 #1
  if %3 banner 120 170 120 20 "100$$" FONT13 -1
  if %4=2 {
    arrow 300 40 16 16 1 red
    fill 0 100 320 2 @SHADE
  }
  if !%5 check 200 30 12 1 0 #7
  spr 600 -5 SPRITE
end
'''


def selfcheck():
    import tempfile
    sym = {"SC_TEST": 300, "FONT13": 1, "SPRITE": 40}
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write(SAMPLE)
    try:
        recs = compile_scenes([f.name], sym)
        assert list(recs) == [300], recs
        w, h, tags, ops = decode(recs[300])
        assert (w, h) == (320, 200)
        assert tags == {1: (8, 170, 96, 20), 7: (200, 30, 12, 12)}, tags
        names = [OPNAMES[o[0]] for o in ops]
        assert names == ["panel", "box", "textbox", "button", "banner", "arrow",
                         "fill", "check", "spr"], names
        assert ops[0][1:3] == (0, 202) and ops[0][4][5] == ("lit", "TITLE $0")
        assert ops[0][4][7] == ("n", 0x21)
        assert ops[1][4][5] == ("@", 4) and ops[2][4][4] == ("$", 1)
        assert ops[3][1:3] == (170, 192) and ops[3][4][6] == ("%", 2)
        assert ops[4][3] == (3, 0) and ops[4][4][4] == ("lit", "100$")
        assert ops[5][3] == (4 | 0x20, 2) and ops[6][3] == (4 | 0x20, 2)
        assert ops[6][4][:4] == [("n", 0), ("n", 100), ("n", 319), ("n", 101)]
        assert ops[7][3] == (5 | 0x20, 0) and ops[7][1:3] == (30, 41)
        assert ops[8][4][:2] == [("n", 600), ("n", -5)] and ops[8][1:3] == (-5, 250)
        bad = {
            'button 0 0 10 10 "x" 1': "takes 7",
            'text 0 0 10 "x" 0 1 0 #3': "no rect",
            'box %1 0 10 10 0 0 #3': "constant",
            'box 0 0 10 10 0 "x"': "string",
            'text 0 0 10 %1 0 1 0': "where a string",
            'box 0 0 10 10 0 $40': "31",
            'blob 0 0': "unknown op",
            'box 0 0 10 10 nope 0': "unknown name",
            'fill %1 0 10 10 0': "constants",
        }
        for line, want in bad.items():
            with open(f.name, "w") as g:
                g.write(f"scene 1 10 10\n  {line}\nend\n")
            try:
                compile_scenes([f.name], sym)
            except GkError as e:
                assert want in str(e), (line, str(e))
            else:
                raise AssertionError(f"accepted: {line}")
    finally:
        os.unlink(f.name)
    print("os88gk: selfcheck ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--selfcheck", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    d = sub.add_parser("dump", help="print each compiled scene's ops")
    d.add_argument("files", nargs="+")
    d.add_argument("--sym", action="append", default=[], help="a header of #defines")
    a = ap.parse_args()
    try:
        if a.selfcheck:
            selfcheck()
        elif a.cmd == "dump":
            sym = read_symbols(a.sym)
            dump(compile_scenes(a.files, sym), {v: k for k, v in sym.items()})
        else:
            ap.print_help()
    except GkError as e:
        sys.exit(f"os88gk: {e}")


if __name__ == "__main__":
    main()
