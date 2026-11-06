#!/usr/bin/env python3
"""os88pdprep.py - prepare a PDF for PageDesk on a modern host (PD-22,
docs/PAGEDESK-SPEC.md 20.14).

    os88pdprep.py IN.pdf OUT.pdf [options]

PageDesk (an 8088 PDF viewer/editor) reads the PDF profile of SPEC 1.3: a
classic cross-reference table, no object streams, streams in no filter (or
ASCIIHex / RunLength; Flate on the Enhanced tier), no encryption, object
numbers up to 65535, at most 1024 pages. This host tool takes a PDF outside
that profile and writes a DERIVATIVE that PageDesk opens:

  1. the file is rewritten by qpdf (pinned, see --version): xref and object
     streams become one classic table of 20-byte entries, every stream in a
     filter PageDesk lacks (Flate unless --flate, LZW, ASCII85) is decoded,
     objects are renumbered 1..N, revisions are flattened;
  2. an encrypted file is refused unless --password / --password-file is
     given (then it is decrypted - the derivative carries no encryption);
  3. every page is CLASSIFIED against what PDENG and PDREND draw: shading,
     patterns, soft masks, transparency, ICC/Indexed/Separation colour,
     JPEG/JPX/CCITT/JBIG2/masked/oversized images, long inline images, Type0/
     Type3/symbol fonts and out-of-range page boxes are "incomplete" (the page
     would be drawn with PageDesk's Incomplete rendering banner);
  4. --raster auto|all|PAGES replaces the pages you choose by horizontal
     strips of a mono (1-bit, ordered dither) or grey (8-bit) image rendered
     by MuPDF (or Poppler), each strip at most --strip-bytes decoded so no
     image nears PDENG's 1 MiB cap, plus an INVISIBLE Courier text layer
     (render mode 3, one word at a time, horizontally scaled onto the word's
     box) where the page really had text - so Find and Copy keep working;
  5. the derivative is VERIFIED by qpdf --check, by tools/os88pdfcheck.py, by
     a profile check of its own and (when built) by PDENG itself, the host
     harness build/pde_test; and a plain-text report says what changed.

The ORIGINAL is never touched: OUT must be a different file and must not exist
(--force replaces an earlier derivative). The derivative's Info dictionary
gets /PageDeskPrep, a text string naming this tool, the original's size and
SHA-256, and the pages that were rasterised, so PageDesk can show
"Prepared copy" and an independent tool can bind the derivative to its source.
Nothing else is a cache: the PDF is the whole product, so nothing can go
stale, and it stays viewable outside PageDesk.

Exit status: 0 prepared and verified; 1 refused (reason on stderr); 2 usage;
3 a needed tool (qpdf; mutool or pdftoppm when rasterising) is missing; 4 the
derivative failed verification (it is left in place, named in the report).

Tools: qpdf, mutool, pdftoppm, found on PATH only. No Python PDF library.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

VERSION = '1.0'

# ---- PDENG / PDREND profile numbers (PAGEDESK-SPEC 1.3, 7.1, 20.2.2) -------
MAX_OBJ = 65535            # D8: object numbers are 16 bits
MAX_PAGES = 1024           # PD_MAXPAGES
MAX_DECODED = 1 << 20      # a decoded stream is cut at 1 MiB
MAX_INLINE = 4096          # inline images beyond 4 KiB are refused
BOX_LIMIT = 2047.0         # page boxes within +/-2047 pt
BASE14 = ('Helvetica', 'Times', 'Courier')
NATIVE_FILTERS = ('/ASCIIHexDecode', '/AHx', '/RunLengthDecode', '/RL')
FLATE_FILTERS = ('/FlateDecode', '/Fl')
GOOD_COLOR = ('/DeviceGray', '/DeviceRGB', '/DeviceCMYK', '/G', '/RGB', '/CMYK')
GOOD_IMG_COLOR = ('/DeviceGray', '/DeviceRGB', '/G', '/RGB')

EXIT_REFUSED, EXIT_USAGE, EXIT_NOTOOL, EXIT_VERIFY = 1, 2, 3, 4


class Refused(Exception):
    pass


class NoTool(Exception):
    pass


# ---------------------------------------------------------------- tools ----
def tool(name):
    return shutil.which(name)           # PATH only, like os88pdfcheck.py


def need(name):
    p = tool(name)
    if not p:
        raise NoTool('%s not found on PATH' % name)
    return p


def run(cmd, ok=(0,), timeout=300, inp=None):
    try:
        r = subprocess.run(cmd, capture_output=True, timeout=timeout, input=inp)
    except subprocess.TimeoutExpired:
        raise Refused('%s timed out' % cmd[0])
    return r.returncode, r.stdout, r.stderr


def qpdf_version():
    rc, out, _ = run([need('qpdf'), '--version'])
    return out.decode('ascii', 'replace').split('\n')[0].strip()


# ---------------------------------------------------------- json helpers ---
REF = re.compile(r'^(\d+) (\d+) R$')


class Doc:
    """A qpdf --json=2 dump (stream data left out) with reference chasing."""

    def __init__(self, js):
        self.js = js
        self.objs = js['qpdf'][1]
        self.trailer = self.objs.get('trailer', {}).get('value', {})
        self.pages = js['pages']

    def get(self, v, depth=0):
        """Resolve a reference to the dict/value (a stream gives its dict)."""
        while isinstance(v, str) and depth < 12:
            m = REF.match(v)
            if not m:
                break
            o = self.objs.get('obj:' + v)
            if o is None:
                return None
            v = o['value'] if 'value' in o else o['stream']['dict']
            depth += 1
        return v

    def is_stream(self, ref):
        o = self.objs.get('obj:' + ref) if isinstance(ref, str) else None
        return bool(o and 'stream' in o)

    def inh(self, page, key):
        """An inheritable page attribute, resolved."""
        node, n = page, 0
        while isinstance(node, dict) and n < 40:
            if key in node:
                return self.get(node[key])
            node = self.get(node.get('/Parent'))
            n += 1
        return None

    def maxobj(self):
        best = 0
        for k in self.objs:
            m = re.match(r'obj:(\d+) ', k)
            if m:
                best = max(best, int(m.group(1)))
        return best


def qpdf_json(path, extra=(), args=()):
    rc, out, err = run([need('qpdf'), '--json=2', '--json-stream-data=none'] + list(args) + list(extra) + [path])
    if rc not in (0, 3):
        raise Refused('qpdf could not read %s: %s' % (path, err.decode('utf-8', 'replace').strip().split('\n')[-1]))
    return json.loads(out)


def names(v):
    """A /Filter value (a name or an array of names, already resolved) as a list."""
    if v is None:
        return []
    if isinstance(v, str):
        return [v]
    return [x for x in v if isinstance(x, str)]


def text_of(s):
    if isinstance(s, str) and s[:2] in ('u:', 'b:'):
        return s[2:]
    return s


# ------------------------------------------------------ content scanning ---
WS = b' \t\r\n\f\x00'
DELIM = b'()<>[]{}/%'


class Tok:
    """A content-stream tokenizer: (operator, operands) pairs and inline images."""

    def __init__(self, data):
        self.d = data
        self.i = 0
        self.n = len(data)

    def skip(self):
        d, n = self.d, self.n
        while self.i < n:
            c = d[self.i]
            if c in WS:
                self.i += 1
            elif c == 0x25:                                   # % comment
                while self.i < n and d[self.i] not in b'\r\n':
                    self.i += 1
            else:
                break

    def value(self):
        """One token: ('op', str) | ('v', python value) | ('eof',) | ('end', ']') ."""
        self.skip()
        d, n = self.d, self.n
        if self.i >= n:
            return ('eof',)
        c = d[self.i]
        if c == 0x2F:                                         # /Name
            j = self.i + 1
            while j < n and d[j] not in WS and d[j] not in DELIM:
                j += 1
            s = d[self.i:j].decode('latin-1')
            self.i = j
            return ('v', s)
        if c == 0x28:                                         # (string)
            depth, j = 1, self.i + 1
            while j < n and depth:
                if d[j] == 0x5C:
                    j += 1
                elif d[j] == 0x28:
                    depth += 1
                elif d[j] == 0x29:
                    depth -= 1
                j += 1
            self.i = j
            return ('v', b'')
        if c == 0x3C:
            if self.i + 1 < n and d[self.i + 1] == 0x3C:      # << dict >>
                self.i += 2
                depth = 1
                while depth and self.i < n:
                    t = self.value()
                    if t[0] == 'eof':
                        break
                    if t == ('op', '<<'):
                        depth += 1
                    elif t == ('op', '>>'):
                        depth -= 1
                return ('v', {})
            j = d.find(b'>', self.i)
            self.i = n if j < 0 else j + 1
            return ('v', b'')
        if c == 0x3E:
            self.i += 2 if d[self.i:self.i + 2] == b'>>' else 1
            return ('op', '>>')
        if c == 0x5B:                                         # [ array ]
            self.i += 1
            arr = []
            while True:
                t = self.value()
                if t[0] == 'eof' or t == ('end', ']'):
                    break
                if t[0] == 'v':
                    arr.append(t[1])
            return ('v', arr)
        if c == 0x5D:
            self.i += 1
            return ('end', ']')
        if c in b'{}()':
            self.i += 1
            return ('op', chr(c))
        j = self.i
        while j < n and d[j] not in WS and d[j] not in DELIM:
            j += 1
        s = d[self.i:j].decode('latin-1')
        self.i = j
        if re.match(r'^[+-]?(\d+\.?\d*|\.\d+)$', s):
            return ('v', float(s))
        if s in ('true', 'false'):
            return ('v', s == 'true')
        if s == 'null':
            return ('v', None)
        return ('op', s)

    def ops(self):
        """Yield (operator, operands, extra). BI yields extra = (dict, datalen)."""
        stack = []
        while True:
            t = self.value()
            if t[0] == 'eof':
                return
            if t[0] == 'v':
                stack.append(t[1])
                if len(stack) > 64:
                    del stack[:32]
                continue
            if t[0] == 'end':
                continue
            op = t[1]
            if op == 'BI':
                yield op, [], self.inline()
                stack = []
                continue
            yield op, stack, None
            stack = []

    def inline(self):
        kv = {}
        while True:
            k = self.value()
            if k[0] == 'eof' or k == ('op', 'ID'):
                break
            if k[0] == 'v' and isinstance(k[1], str) and k[1].startswith('/'):
                v = self.value()
                if v[0] == 'v':
                    kv[k[1]] = v[1]
        if self.i < self.n:
            self.i += 1                                       # the one white byte after ID
        start = self.i
        m = re.compile(rb'[\s]EI(?=[\s]|$)').search(self.d, start)
        end = m.start() if m else self.n
        self.i = m.end() if m else self.n
        return kv, end - start


def b64(s):
    return base64.b64decode(s)


def decoded_size(w, h, nc, bpc):
    return ((w * nc * bpc + 7) // 8) * h


class Scan:
    """Everything one page (and the forms it draws) needs from the renderer."""

    def __init__(self, doc, flate):
        self.doc = doc
        self.flate = flate
        self.data = {}                                        # ref -> content bytes

    # -- page-level -----------------------------------------------------
    def fetch(self, path, refs):
        """Inline stream data of the given objects, in one qpdf call."""
        refs = [r for r in refs if r not in self.data]
        if not refs:
            return
        args = ['--json-stream-data=inline']
        for r in refs:
            args.append('--json-object=' + r.split()[0])
        rc, out, err = run([need('qpdf'), '--json=2'] + args + [path])
        if rc not in (0, 3):
            raise Refused('qpdf could not dump streams: ' + err.decode('utf-8', 'replace').strip()[-200:])
        objs = json.loads(out)['qpdf'][1]
        for r in refs:
            o = objs.get('obj:' + r)
            self.data[r] = b64(o['stream']['data']) if o and 'stream' in o and 'data' in o['stream'] else b''

    def prefetch(self, path):
        doc = self.doc
        want = set()
        for p in doc.pages:
            pd = doc.get(p['object'])
            c = pd.get('/Contents')
            c = doc.get(c) if isinstance(c, str) and not doc.is_stream(c) else c
            for r in ([c] if isinstance(c, str) else c or []):
                if isinstance(r, str) and REF.match(r):
                    want.add(r)
        for k, o in doc.objs.items():
            if 'stream' in o and o['stream']['dict'].get('/Subtype') == '/Form':
                want.add(k[4:])
        self.fetch(path, sorted(want))

    def page_contents(self, pd):
        c = pd.get('/Contents')
        if isinstance(c, str) and not self.doc.is_stream(c):
            c = self.doc.get(c)
        refs = [c] if isinstance(c, str) else (c or [])
        return [r for r in refs if isinstance(r, str) and REF.match(r)]

    # -- the walk ---------------------------------------------------------
    def page(self, pd, pageidx):
        """-> (hard, soft): lists of (code, text)."""
        doc = self.doc
        hard, soft = [], []

        def add(lst, code, text):
            if (code, text) not in lst:
                lst.append((code, text))

        # page box
        mb = doc.inh(pd, '/MediaBox') or [0, 0, 612, 792]
        cb = doc.inh(pd, '/CropBox') or mb
        for nm, bx in (('MediaBox', mb), ('CropBox', cb)) if cb != mb else (('MediaBox', mb),):
            try:
                v = [float(doc.get(x)) for x in bx]
                if len(v) != 4 or max(abs(x) for x in v) > BOX_LIMIT or v[0] == v[2] or v[1] == v[3]:
                    add(hard, 'BOX-RANGE', '%s %s is outside +/-2047 pt or empty (PDE_RANGE)' % (nm, v))
            except (TypeError, ValueError):
                add(hard, 'BOX-RANGE', '%s is not four numbers' % nm)
        rot = doc.inh(pd, '/Rotate')
        if isinstance(rot, (int, float)) and int(rot) % 90:
            add(hard, 'BOX-RANGE', '/Rotate %s is not a multiple of 90 (PDE_RANGE)' % rot)

        res = doc.inh(pd, '/Resources') or {}
        seen = set()
        for r in self.page_contents(pd):
            n = int(r.split()[0])
            d = doc.get(r) or {}
            if isinstance(d.get('/Length'), int) and d['/Length'] > MAX_DECODED:
                add(hard, 'CONTENT-BIG', 'content stream %d is over 1 MiB (PDE_LIMIT)' % n)
            self.stream(self.data.get(r, b''), res, hard, soft, seen, 0)
        return hard, soft

    def stream(self, data, res, hard, soft, seen, depth):
        doc = self.doc

        def add(lst, code, text):
            if (code, text) not in lst:
                lst.append((code, text))

        res = doc.get(res) if isinstance(res, str) else (res or {})
        fonts = doc.get(res.get('/Font')) or {}
        xobjs = doc.get(res.get('/XObject')) or {}
        gstates = doc.get(res.get('/ExtGState')) or {}
        cspaces = doc.get(res.get('/ColorSpace')) or {}
        for op, a, extra in Tok(data).ops():
            if op == 'sh':
                add(hard, 'SHADING', 'shading fill (sh)')
            elif op == 'Tf' and a and isinstance(a[0], str):
                self.font(fonts.get(a[0]), a[0], hard, soft)
            elif op == 'Tr' and a and isinstance(a[-1], float) and 4 <= a[-1] <= 7:
                add(soft, 'TEXT-CLIP', 'text render mode %d (clip) is not drawn' % a[-1])
            elif op == 'gs' and a and isinstance(a[0], str):
                g = doc.get(gstates.get(a[0])) or {}
                sm = g.get('/SMask')
                if sm not in (None, '/None'):
                    add(hard, 'SMASK', 'soft mask in graphics state %s' % a[0])
                bm = doc.get(g.get('/BM'))
                if isinstance(bm, list):
                    bm = bm[0] if bm else None
                if bm not in (None, '/Normal', '/Compatible'):
                    add(hard, 'BLEND', 'blend mode %s' % bm)
                for k in ('/CA', '/ca'):
                    if isinstance(g.get(k), (int, float)) and g[k] < 1:
                        add(hard, 'ALPHA', 'transparency (%s %s)' % (k[1:], g[k]))
            elif op in ('cs', 'CS') and a and isinstance(a[0], str):
                self.cspace(a[0], cspaces, hard)
            elif op in ('scn', 'SCN', 'sc', 'SC') and a and isinstance(a[-1], str):
                add(hard, 'PATTERN', 'pattern fill/stroke %s' % a[-1])
            elif op == 'Do' and a and isinstance(a[0], str):
                ref = xobjs.get(a[0])
                xd = doc.get(ref) or {}
                st = xd.get('/Subtype')
                if st == '/Image':
                    self.image(xd, a[0], hard)
                elif st == '/Form' and isinstance(ref, str) and ref not in seen and depth < 4:
                    seen.add(ref)
                    if isinstance(xd.get('/Length'), int) and xd['/Length'] > MAX_DECODED:
                        add(hard, 'CONTENT-BIG', 'form %s is over 1 MiB (PDE_LIMIT)' % a[0])
                    sub = xd.get('/Resources') or res
                    self.stream(self.data.get(ref, b''), sub, hard, soft, seen, depth + 1)
                    seen.discard(ref)
            elif op == 'BI':
                kv, n = extra
                if n > MAX_INLINE:
                    add(hard, 'INLINE-BIG', 'inline image of %d bytes (limit 4096)' % n)
                f = names(kv.get('/F') or kv.get('/Filter'))
                if any(x not in NATIVE_FILTERS and (x not in FLATE_FILTERS or not self.flate) for x in f):
                    add(hard, 'INLINE-FILTER', 'inline image filter %s' % ','.join(f))
                cs = kv.get('/CS') or kv.get('/ColorSpace')
                if cs is not None and cs not in GOOD_IMG_COLOR and kv.get('/IM') is not True and kv.get('/ImageMask') is not True:
                    add(hard, 'IMG-CS', 'inline image colour space %s' % (cs,))

    def cspace(self, name, cspaces, hard):
        doc = self.doc
        if name in GOOD_COLOR:
            return
        if name == '/Pattern':
            hard.append(('PATTERN', 'pattern colour space')) if ('PATTERN', 'pattern colour space') not in hard else None
            return
        cs = doc.get(cspaces.get(name))
        fam = cs[0] if isinstance(cs, list) and cs else cs
        if fam in GOOD_COLOR:
            return
        item = ('COLORSPACE', 'colour space %s (%s)' % (name, fam))
        if item not in hard:
            hard.append(item)

    def font(self, ref, nm, hard, soft):
        doc = self.doc
        f = doc.get(ref)
        if not isinstance(f, dict):
            return
        sub = f.get('/Subtype')
        base = text_of(f.get('/BaseFont', '')) or ''
        base = re.sub(r'^/?[A-Z]{6}\+', '', base).lstrip('/')
        item = None
        if sub in ('/Type0', '/CIDFontType0', '/CIDFontType2'):
            item = (hard, 'FONT-CID', 'composite (CID) font %s' % (base or nm))
        elif sub == '/Type3':
            item = (hard, 'FONT-TYPE3', 'Type 3 font %s' % nm)
        elif re.match(r'^(Symbol|ZapfDingbats)', base):
            item = (hard, 'FONT-SYMBOL', 'symbol font %s' % base)
        elif sub in ('/Type1', '/MMType1') and base.startswith(BASE14) and '+' not in str(f.get('/BaseFont', '')):
            return
        elif sub == '/TrueType':
            item = (soft, 'FONT-SUBST', 'TrueType font %s is drawn with a substitute face' % (base or nm))
        else:
            item = (soft, 'FONT-SUBST', 'font %s is drawn with a substitute face' % (base or nm))
        if (item[1], item[2]) not in item[0]:
            item[0].append((item[1], item[2]))

    def image(self, xd, nm, hard):
        doc = self.doc

        def add(code, text):
            if (code, text) not in hard:
                hard.append((code, text))

        flt = names(doc.get(xd.get('/Filter')))
        for x in flt:
            if x in NATIVE_FILTERS or (self.flate and x in FLATE_FILTERS):
                continue
            add('IMG-FILTER', 'image %s uses %s' % (nm, x[1:]))
        if '/SMask' in xd or '/Mask' in xd:
            add('IMG-MASK', 'image %s has a soft/colour-key/explicit mask' % nm)
        w, h = doc.get(xd.get('/Width')), doc.get(xd.get('/Height'))
        mask = doc.get(xd.get('/ImageMask')) is True
        bpc = 1 if mask else doc.get(xd.get('/BitsPerComponent'))
        cs = doc.get(xd.get('/ColorSpace'))
        fam = cs[0] if isinstance(cs, list) and cs else cs
        nc = 1
        if not mask:
            if fam not in GOOD_IMG_COLOR:
                add('IMG-CS', 'image %s colour space %s' % (nm, fam))
            nc = 3 if fam in ('/DeviceRGB', '/RGB') else 1
            if bpc not in (1, 8):
                add('IMG-BPC', 'image %s has %s bits per component' % (nm, bpc))
        if isinstance(w, int) and isinstance(h, int) and isinstance(bpc, int):
            if decoded_size(w, h, nc, bpc) > MAX_DECODED:
                add('IMG-BIG', 'image %s decodes to %d bytes (limit 1 MiB, PDE_LIMIT)' % (nm, decoded_size(w, h, nc, bpc)))


# --------------------------------------------------------------- raster ----
def rl_encode(data):
    """PDF RunLengthDecode, ended by 128. Short runs of equal bytes cost nothing."""
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        j = i
        while j < n and j - i < 128 and data[j] == data[i]:
            j += 1
        if j - i >= 2:
            out += bytes((257 - (j - i), data[i]))
            i = j
            continue
        k = i
        while k < n and k - i < 128 and not (k + 1 < n and data[k] == data[k + 1]):
            k += 1
        k = max(k, i + 1)
        out.append(k - i - 1)
        out += data[i:k]
        i = k
    out.append(128)
    return bytes(out)


BAYER = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))


def dither_1bit(gray, w, h):
    """Ordered (Bayer 4x4) dither of 8-bit grey rows to packed 1-bit, 1 = white."""
    rows = []
    stride = (w + 7) // 8
    pad = stride * 8 - w
    thr = []
    for y in range(4):
        t = [(BAYER[y][x & 3] * 16 + 8) for x in range(w)]
        thr.append(t)
    tab = bytes.maketrans(b'\x00\x01', b'01')
    for y in range(h):
        row = gray[y * w:(y + 1) * w]
        bits = bytes(map(int.__gt__, row, thr[y & 3]))        # 1 where the pixel beats the threshold
        s = bits.translate(tab) + b'0' * pad
        rows.append(int(s, 2).to_bytes(stride, 'big'))
    return b''.join(rows), stride


def read_pnm(data):
    """P5 -> (w, h, bytes). P4 is refused (we always ask for grey)."""
    m = re.match(rb'P5\s+(?:#[^\n]*\n\s*)*(\d+)\s+(\d+)\s+(\d+)\s', data)
    if not m:
        raise Refused('the rasteriser did not return a binary PGM')
    w, h = int(m.group(1)), int(m.group(2))
    body = data[m.end():]
    if len(body) < w * h:
        raise Refused('the rasteriser returned a short image')
    return w, h, body[:w * h]


def render_gray(path, pageno, dpi):
    """The page as displayed (CropBox, /Rotate, annotations) as grey pixels."""
    if tool('mutool'):
        with tempfile.TemporaryDirectory() as td:
            out = os.path.join(td, 'p.pgm')
            rc, _, err = run([tool('mutool'), 'draw', '-q', '-r', '%g' % dpi, '-c', 'gray', '-o', out, path, str(pageno)])
            if rc == 0 and os.path.exists(out):
                return read_pnm(open(out, 'rb').read()), 'mutool'
    if tool('pdftoppm'):
        rc, out, err = run([tool('pdftoppm'), '-gray', '-r', '%g' % dpi, '-f', str(pageno), '-l', str(pageno), path])
        if rc == 0:
            return read_pnm(out), 'pdftoppm'
    raise NoTool('neither mutool nor pdftoppm could render page %d' % pageno)


def stext_words(path, pageno):
    """Words of a page in the displayed frame: [(x0, x1, baseline, size, text)] in
    points, y down from the top; plus the displayed (w, h). Needs mutool."""
    if not tool('mutool'):
        return None, None
    rc, out, _ = run([tool('mutool'), 'draw', '-q', '-F', 'stext', '-o', '-', path, str(pageno)])
    if rc != 0:
        return None, None
    import html
    # a regex over MuPDF's regular stext XML: pyexpat is not dependable on every host Python
    txt = out.decode('utf-8', 'replace')
    m = re.search(r'<page\b([^>]*)>', txt)
    if not m:
        return None, None
    at = dict(re.findall(r'(\w+)="([^"]*)"', m.group(1)))
    pw, ph = float(at.get('width', 0)), float(at.get('height', 0))
    words = []
    size, dirok, cur = 10.0, True, []

    def flush():
        if cur:
            x0 = float(cur[0][1])
            x1 = max(cur[-1][3])
            words.append((x0, x1, float(cur[0][2]), size, ''.join(c[0] for c in cur)))
            del cur[:]

    for t in re.finditer(r'<(/?)(line|font|char)\b([^>]*?)/?>', txt):
        close, tag, rest = t.group(1), t.group(2), t.group(3)
        if tag == 'line':
            flush()
            if not close:
                at = dict(re.findall(r'(\w+)="([^"]*)"', rest))
                dirok = at.get('dir') in (None, '1 0')
        elif tag == 'font':
            flush()
            if not close:
                at = dict(re.findall(r'(\w+)="([^"]*)"', rest))
                size = float(at.get('size', '10'))
        elif tag == 'char' and dirok:
            at = dict(re.findall(r'(\w+)="([^"]*)"', rest))
            c = html.unescape(at.get('c', ''))
            if c in (' ', '\u00a0', '\t', ''):
                flush()
            else:
                q = [float(v) for v in at.get('quad', '0 0 0 0 0 0 0 0').split()]
                cur.append((c, at.get('x', '0'), at.get('y', '0'), q[0::2]))
    flush()
    return words, (pw, ph)


def pdf_text_string(s):
    """cp1252 bytes for a Courier string, or None when a char has no code."""
    try:
        b = s.encode('cp1252')
    except UnicodeEncodeError:
        return None
    if any(c < 0x20 for c in b):
        return None
    return b


def esc(b):
    return b.replace(b'\\', b'\\\\').replace(b'(', b'\\(').replace(b')', b'\\)')


class Raster:
    """Build the update-JSON objects that replace one page by image strips."""

    def __init__(self, dpi, bits, strip_bytes, textlayer, flate):
        self.dpi, self.bits, self.strip_bytes = dpi, bits, strip_bytes
        self.textlayer, self.flate = textlayer, flate

    def page(self, path, pageno, first_obj, font_obj):
        """-> (objs, page_dict_overrides, info) ; objs is {key: json}, next free obj number."""
        f = 1.0
        (w, h, gray), used = render_gray(path, pageno, self.dpi)
        if max(w, h) * 72.0 / self.dpi > 2000:               # a poster: shrink it inside +/-2047 pt
            f = 2000.0 / (max(w, h) * 72.0 / self.dpi)
            (w, h, gray), used = render_gray(path, pageno, self.dpi * f)
        wpage, hpage = w * 72.0 / (self.dpi), h * 72.0 / (self.dpi)
        objs = {}
        n = first_obj
        rowbytes = (w + 7) // 8 if self.bits == 1 else w
        per = max(1, self.strip_bytes // rowbytes)
        content = [b'q']
        xobj = {}
        y = 0
        k = 0
        total = 0
        while y < h:
            rows = min(per, h - y)
            chunk = gray[y * w:(y + rows) * w]
            if self.bits == 1:
                raw, _ = dither_1bit(chunk, w, rows)
            else:
                raw = bytes(chunk)
            d = {'/Type': '/XObject', '/Subtype': '/Image', '/Width': w, '/Height': rows,
                 '/ColorSpace': '/DeviceGray', '/BitsPerComponent': self.bits}
            data = raw
            if not self.flate:
                enc = rl_encode(raw)
                if len(enc) < len(raw):
                    d['/Filter'] = '/RunLengthDecode'
                    data = enc
            total += len(data)
            objs['obj:%d 0 R' % n] = {'stream': {'dict': d, 'data': base64.b64encode(data).decode()}}
            top = hpage - y * 72.0 / self.dpi
            hh = rows * 72.0 / self.dpi
            nm = '/S%d' % k
            xobj[nm] = '%d 0 R' % n
            content.append(('%.4f 0 0 %.4f 0 %.4f cm %s Do' % (wpage, hh, top - hh, nm)).encode())
            # cm accumulates in a q/Q pair per strip so each strip stands alone
            content[-1] = b'q ' + content[-1] + b' Q'
            n += 1
            k += 1
            y += rows
        content.append(b'Q')
        words = 0
        skipped = 0
        res = {'/XObject': xobj}
        if self.textlayer:
            ws, sz = stext_words(path, pageno)
            if ws:
                sx = wpage / sz[0] if sz and sz[0] else 1.0
                tl = [b'BT 3 Tr /FT 10 Tf']
                for x0, x1, base, size, txt in ws:
                    b = pdf_text_string(txt)
                    if not b or size <= 0:
                        skipped += 1
                        continue
                    if x1 <= x0:                              # an unembedded font with no widths: guess half an em a letter
                        x1 = x0 + 0.5 * size * len(b)
                    sizep = size * sx
                    tz = (x1 - x0) * sx / (0.6 * sizep * len(b)) * 100.0
                    tz = min(max(tz, 1.0), 2000.0)
                    tl.append(('/FT %.3f Tf %.2f Tz 1 0 0 1 %.3f %.3f Tm (' % (sizep, tz, x0 * sx, hpage - base * sx)).encode() + esc(b) + b') Tj')
                    words += 1
                tl.append(b'ET')
                if words:
                    content += tl
                    res['/Font'] = {'/FT': '%d 0 R' % font_obj}
        cdata = b'\n'.join(content) + b'\n'
        objs['obj:%d 0 R' % n] = {'stream': {'dict': {}, 'data': base64.b64encode(cdata).decode()}}
        cref = '%d 0 R' % n
        n += 1
        info = {'strips': k, 'w': w, 'h': h, 'bytes': total, 'words': words, 'skipped': skipped, 'tool': used,
                'scale': f, 'pt': (wpage, hpage)}
        over = {'/MediaBox': [0, 0, round(wpage, 3), round(hpage, 3)], '/Contents': cref, '/Resources': res}
        return objs, over, info, n


# --------------------------------------------------------------- report ----
class Report:
    def __init__(self):
        self.lines = []

    def add(self, s=''):
        self.lines.append(s)

    def text(self):
        return '\n'.join(self.lines) + '\n'


def parse_pages(spec, npages):
    out = set()
    for part in spec.split(','):
        part = part.strip()
        if not part:
            continue
        m = re.match(r'^(\d+)(?:-(\d+))?$', part)
        if not m:
            raise Refused('bad page list %r (use 2,5-7)' % spec)
        a = int(m.group(1))
        b = int(m.group(2) or a)
        for p in range(a, b + 1):
            if 1 <= p <= npages:
                out.add(p)
    return out


def filters_in(js):
    cnt = {}
    for k, o in js['qpdf'][1].items():
        if 'stream' in o:
            for f in names(o['stream']['dict'].get('/Filter')):
                cnt[f[1:]] = cnt.get(f[1:], 0) + 1
            if '/Filter' not in o['stream']['dict']:
                cnt['(none)'] = cnt.get('(none)', 0) + 1
    return cnt


# ------------------------------------------------------------ the job ------
def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def prepare(a, rep):
    src, dst = os.path.abspath(a.input), os.path.abspath(a.output)
    if not os.path.isfile(src):
        raise Refused('%s is not a file' % a.input)
    if os.path.exists(dst) and (os.path.samefile(src, dst)):
        raise Refused('the output is the input; the original is never overwritten')
    if os.path.exists(dst) and not a.force:
        raise Refused('%s exists (use --force to replace an earlier derivative)' % a.output)
    head = open(src, 'rb').read(1024)
    if b'%PDF-' not in head:
        raise Refused('%s does not look like a PDF (no %%PDF- in the first 1 KiB)' % a.input)
    raw = open(src, 'rb').read()
    qpdf = need('qpdf')
    qv = qpdf_version()
    flate = a.flate

    pwargs = []
    pwtmp = None
    pw = a.password
    if a.password_file:
        pw = open(a.password_file, 'rb').read().rstrip(b'\r\n').decode('utf-8', 'replace')
    rc, _, _ = run([qpdf, '--is-encrypted', src])
    encrypted = rc == 0
    if encrypted:
        if pw is None:
            raise Refused('the file is encrypted (PDE_ENCRYPT); give --password or --password-file to decrypt it')
        fd, pwtmp = tempfile.mkstemp(prefix='pdprep-')
        os.write(fd, pw.encode('utf-8'))
        os.close(fd)
        pwargs = ['--password-file=' + pwtmp, '--decrypt']
    try:
        return _prepare(a, rep, src, dst, raw, qpdf, qv, flate, encrypted, pwargs)
    finally:
        if pwtmp:
            os.unlink(pwtmp)


def _prepare(a, rep, src, dst, raw, qpdf, qv, flate, encrypted, pwargs):
    td = tempfile.mkdtemp(prefix='pdprep-')
    try:
        return _prepare2(a, rep, src, dst, raw, qpdf, qv, flate, encrypted, pwargs, td)
    finally:
        shutil.rmtree(td, ignore_errors=True)


def _prepare2(a, rep, src, dst, raw, qpdf, qv, flate, encrypted, pwargs, td):
    srcsha = hashlib.sha256(raw).hexdigest()
    warnings = []

    # ---- what the input is
    inj = qpdf_json(src, args=pwargs)
    in_filters = filters_in(inj)
    idoc = Doc(inj)
    n_in = len(idoc.pages)
    has_xrefstm = bool(re.search(rb'/Type\s*/XRef', raw))
    has_objstm = bool(re.search(rb'/Type\s*/ObjStm', raw))
    revs = raw.count(b'startxref')
    linear = b'/Linearized' in raw[:2048]
    cat = idoc.get(idoc.trailer.get('/Root')) or {}
    sigs = '/Perms' in cat or bool(re.search(rb'/ByteRange\b', raw))
    notes = []
    if has_xrefstm:
        notes.append('cross-reference stream(s) rewritten as a classic xref table with 20-byte entries')
    if has_objstm:
        notes.append('object streams expanded into ordinary objects')
    if revs > 1:
        notes.append('%d incremental revisions flattened into one' % revs)
    if linear:
        notes.append('linearization removed')
    if encrypted:
        notes.append('decrypted with the supplied password; the derivative carries NO encryption or permissions')
    if sigs:
        warnings.append('the file carries /Perms or a signature: rewriting INVALIDATES any signature')
    if '/AcroForm' in cat and isinstance(idoc.get(cat['/AcroForm']), dict) and '/XFA' in idoc.get(cat['/AcroForm']):
        warnings.append('XFA form data is present; PageDesk ignores it (AcroForm editing is Enhanced, PD-26)')
    if '/Names' in cat or '/OpenAction' in cat or '/AA' in cat:
        notes.append('names/open actions are kept as data; PageDesk never runs actions or JavaScript')

    # ---- step A: normalise (and page subset)
    A = os.path.join(td, 'a.pdf')
    cmd = [qpdf] + pwargs + ['--object-streams=disable', '--newline-before-endstream']
    if flate:
        cmd += ['--decode-level=generalized', '--compress-streams=y']
    else:
        cmd += ['--stream-data=uncompress']
    sel = None
    if a.pages:
        sel = a.pages
        cmd += [src, '--pages', '.', sel, '--', A]
    else:
        cmd += [src, A]
    rc, _, err = run(cmd)
    if rc not in (0, 3):
        raise Refused('qpdf could not rewrite the file: ' + err.decode('utf-8', 'replace').strip()[-300:])
    if rc == 3:
        warnings.append('qpdf reported warnings (the input was damaged and was reconstructed): ' +
                        ' | '.join(l[:100] for l in err.decode('utf-8', 'replace').split('\n') if l.strip())[:300])
    aj = qpdf_json(A)
    doc = Doc(aj)
    npages = len(doc.pages)
    if npages == 0:
        raise Refused('the document has no pages')
    if npages > MAX_PAGES:
        raise Refused('%d pages: PageDesk holds %d (PDE_LIMIT); prepare a range with --pages 1-%d' % (npages, MAX_PAGES, MAX_PAGES))
    if sel:
        notes.append('only pages %s kept (%d of %d); the outline is dropped, its targets may be gone' % (sel, npages, n_in))

    # ---- step B: classify every page
    sc = Scan(doc, flate)
    sc.prefetch(A)
    report = []
    for i, p in enumerate(doc.pages):
        pd = doc.get(p['object'])
        hard, soft = sc.page(pd, i)
        report.append((hard, soft))

    # ---- step C: choose pages to rasterise
    mode = a.raster
    chosen = set()
    if mode == 'all':
        chosen = set(range(1, npages + 1))
    elif mode == 'auto':
        for i, (hard, soft) in enumerate(report):
            if hard or (a.fonts == 'raster' and soft):
                chosen.add(i + 1)
    elif mode != 'none':
        chosen = parse_pages(mode, npages)
    elif a.fonts == 'raster':
        for i, (hard, soft) in enumerate(report):
            if any(c.startswith('FONT') for c, _ in soft):
                chosen.add(i + 1)

    upd_objs = {}
    page_over = {}
    rinfo = {}
    if chosen:
        if not (tool('mutool') or tool('pdftoppm')):
            raise NoTool('rasterising needs mutool or pdftoppm on PATH')
        ras = Raster(a.dpi, a.bits, a.strip_bytes, a.text_layer, flate)
        nextobj = doc.maxobj() + 1
        font_obj = nextobj
        nextobj += 1
        upd_objs['obj:%d 0 R' % font_obj] = {'value': {'/Type': '/Font', '/Subtype': '/Type1', '/BaseFont': '/Courier',
                                                       '/Encoding': '/WinAnsiEncoding'}}
        for pg in sorted(chosen):
            objs, over, info, nextobj = ras.page(A, pg, nextobj, font_obj)
            upd_objs.update(objs)
            page_over[pg] = over
            rinfo[pg] = info
        for pg in sorted(chosen):
            ref = doc.pages[pg - 1]['object']
            pd = dict(doc.get(ref))
            keep = {k: v for k, v in pd.items() if k in ('/Type', '/Parent')}
            keep.update(page_over[pg])
            upd_objs['obj:' + ref] = {'value': keep}

    # ---- the Info stamp
    ptxt = 'os88pdprep %s; qpdf %s; src-size=%d; src-sha256=%s; tier=%s' % (
        VERSION, qv.split()[-1] if qv else '?', len(raw), srcsha, 'enhanced' if flate else 'xt')
    if chosen:
        ptxt += '; raster=%s; dpi=%g; bits=%d' % (','.join(str(p) for p in sorted(chosen)), a.dpi, a.bits)
    ptxt += '; pages=%d' % npages
    infoval = doc.get(doc.trailer.get('/Info'))
    iref = doc.trailer.get('/Info')
    if isinstance(iref, str) and REF.match(iref) and isinstance(infoval, dict):
        iv = dict(infoval)
        iv['/PageDeskPrep'] = 'u:' + ptxt
        upd_objs['obj:' + iref] = {'value': iv}
        trailer_upd = None
    else:
        if chosen:
            nid = max(int(k.split(':')[1].split()[0]) for k in upd_objs) + 1
        else:
            nid = doc.maxobj() + 1
        upd_objs['obj:%d 0 R' % nid] = {'value': {'/Producer': 'u:os88pdprep ' + VERSION, '/PageDeskPrep': 'u:' + ptxt}}
        tv = dict(doc.trailer)
        tv['/Info'] = '%d 0 R' % nid
        trailer_upd = {'value': tv}
    if sel:
        catref = doc.trailer.get('/Root')
        cv = dict(doc.get(catref))
        cv.pop('/Outlines', None)
        upd_objs['obj:' + catref] = {'value': cv}
    uj = {'qpdf': [{'jsonversion': 2, 'pushedinheritedpageresources': False, 'calledgetallpages': False,
                    'parameters': {'decodelevel': 'none'}}, dict(upd_objs)]}
    if trailer_upd:
        uj['qpdf'][1]['trailer'] = trailer_upd
    ufile = os.path.join(td, 'u.json')
    with open(ufile, 'w') as f:
        json.dump(uj, f)

    # ---- step D: the final write, to a name beside the output, then renamed
    tmp = dst + '.tmp'
    cmd = [qpdf, '--object-streams=disable', '--newline-before-endstream', '--deterministic-id',
           '--update-from-json=' + ufile]
    cmd += ['--compress-streams=y'] if flate else ['--stream-data=preserve']
    rc, _, err = run(cmd + [A, tmp])
    if rc not in (0, 3):
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise Refused('qpdf could not write the derivative: ' + err.decode('utf-8', 'replace').strip()[-300:])
    os.replace(tmp, dst)

    # ---- verification
    unfixed = {i + 1 for i, (h, _s) in enumerate(report) if h and (i + 1) not in chosen}
    ver = verify(a, dst, npages, flate, unfixed)
    out_size = os.path.getsize(dst)

    # ---- the report
    rep.add('os88pdprep %s - prepared copy report' % VERSION)
    rep.add('qpdf    : %s' % qv)
    rep.add('input   : %s (%d bytes, PDF %s, sha256 %s)' % (a.input, len(raw), inj.get('version', '?'), srcsha))
    rep.add('output  : %s (%d bytes, %d page%s)' % (a.output, out_size, npages, '' if npages == 1 else 's'))
    rep.add('tier    : %s' % ('Enhanced - Flate streams kept (needs PD-25; an XT refuses them)' if flate else
                              'XT - no Flate, no object streams, no xref stream'))
    rep.add('original: untouched (the derivative is a separate file)')
    rep.add('')
    rep.add('FILE')
    for n in notes or ['already a classic file; rewritten only to renumber objects and unify streams']:
        rep.add('  - ' + n)
    out_filters = filters_in(qpdf_json(dst))
    rep.add('  - stream filters in : %s' % (', '.join('%s x%d' % kv for kv in sorted(in_filters.items())) or 'none'))
    rep.add('  - stream filters out: %s' % (', '.join('%s x%d' % kv for kv in sorted(out_filters.items())) or 'none'))
    nobj = sum(1 for k in aj['qpdf'][1] if k.startswith('obj:'))
    rep.add('  - objects: %d numbered 1..%d (limit %d)' % (nobj, nobj, MAX_OBJ))
    if sel:
        rep.add('  - page selection %s' % sel)
    for w in warnings:
        rep.add('  ! ' + w)
    rep.add('')
    rep.add('PAGES')
    text_pages = 0
    for i, (hard, soft) in enumerate(report):
        pg = i + 1
        if pg in chosen:
            ri = rinfo[pg]
            why = '; '.join(t for _, t in hard) or ('substituted fonts' if soft else 'requested')
            rep.add('  page %-4d RASTERISED (%s) -> %d strip%s of %dx%d px, %d-bit, %g dpi%s, %d bytes of image data' % (
                pg, why, ri['strips'], '' if ri['strips'] == 1 else 's', ri['w'], ri['h'], a.bits, a.dpi,
                ' (page scaled to %.0f%% to fit +/-2047 pt)' % (ri['scale'] * 100) if ri['scale'] < 1 else '', ri['bytes']))
            if ri['words']:
                text_pages += 1
                rep.add('             searchable text: %d invisible word%s (Courier, scaled onto each word box)%s' % (
                    ri['words'], '' if ri['words'] == 1 else 's',
                    '; %d word(s) skipped (characters outside WinAnsi)' % ri['skipped'] if ri['skipped'] else ''))
            else:
                rep.add('             searchable text: none (no text layer was available%s)' % (
                    '' if a.text_layer else '; --no-text-layer' ))
        elif hard:
            rep.add('  page %-4d INCOMPLETE in PageDesk: %s' % (pg, '; '.join(t for _, t in hard)))
            text_pages += 1
        elif soft:
            rep.add('  page %-4d native with substitutions: %s' % (pg, '; '.join(t for _, t in soft)))
            text_pages += 1
        else:
            rep.add('  page %-4d native' % pg)
            text_pages += 1
    inc = [i + 1 for i, (h, s) in enumerate(report) if h and (i + 1) not in chosen]
    if inc:
        rep.add('')
        rep.add('  %d page(s) will show "Incomplete rendering": %s. Re-run with --raster auto to replace them by images.' % (
            len(inc), ','.join(map(str, inc[:20])) + ('...' if len(inc) > 20 else '')))
    if chosen:
        rep.add('')
        rep.add('  Rasterised pages are pictures: their annotations/links are baked in, vector sharpness is gone, and only')
        rep.add('  words listed above can be found or copied. New annotations PageDesk adds are ordinary vector marks.')
    rep.add('')
    rep.add('SIZE   %d -> %d bytes%s' % (len(raw), out_size, '  (over a 360 KB floppy: use a bigger disk, --bits 1, a lower --dpi or --pages)'
                                           if out_size > 340 * 1024 else ''))
    rep.add('')
    rep.add('VERIFY')
    for line in ver['lines']:
        rep.add('  ' + line)
    if ver['ok']:
        rep.add('  RESULT: prepared and verified')
    else:
        rep.add('  RESULT: VERIFICATION FAILED - %s is left in place for inspection' % a.output)
    return 0 if ver['ok'] else EXIT_VERIFY


# --------------------------------------------------------------- verify ----
def profile_check(path, flate, npages):
    """Independent of the preparation: re-read the OUTPUT and test the profile."""
    errs = []
    raw = open(path, 'rb').read()
    if not raw.startswith(b'%PDF-'):
        errs.append('header not at offset 0')
    if re.search(rb'/Type\s*/(ObjStm|XRef)\b', raw):
        errs.append('object/xref stream present')
    if b'/Encrypt' in raw[-4096:]:
        errs.append('/Encrypt in the trailer')
    sx = raw.rfind(b'startxref')
    if sx < 0:
        errs.append('no startxref')
    else:
        off = int(raw[sx + 9:].split()[0])
        if not raw[off:off + 4] == b'xref':
            errs.append('startxref does not point at "xref"')
        else:
            body = raw[off + 4:raw.find(b'trailer', off)]
            lines = body.split(b'\n')
            sub = 0
            ent = 0
            i = 0
            while i < len(lines):
                m = re.match(rb'^\s*(\d+) (\d+)\s*$', lines[i])
                if m:
                    sub += 1
                    cnt = int(m.group(2))
                    first = int(m.group(1))
                    if first + cnt > MAX_OBJ + 1:
                        errs.append('object numbers above %d' % MAX_OBJ)
                    for j in range(cnt):
                        e = lines[i + 1 + j] if i + 1 + j < len(lines) else b''
                        if len(e) + 1 != 20 and len(e) + 2 != 20:
                            errs.append('xref entry %d is not 20 bytes' % (first + j))
                            break
                    ent += cnt
                    i += 1 + cnt
                else:
                    i += 1
            if sub > 256:
                errs.append('%d xref subsections (limit 256)' % sub)
    js = qpdf_json(path)
    allowed = set(NATIVE_FILTERS)
    if flate:
        allowed |= set(FLATE_FILTERS)
    hard_unknown = {}
    for k, o in js['qpdf'][1].items():
        if 'stream' in o:
            for f in names(o['stream']['dict'].get('/Filter')):
                if f not in allowed:
                    hard_unknown[f] = hard_unknown.get(f, 0) + 1
    # image filters PDENG lacks are legitimate leftovers (reported per page); only content filters are errors
    if len(js['pages']) != npages:
        errs.append('page count %d != %d' % (len(js['pages']), npages))
    return errs, hard_unknown


def verify(a, dst, npages, flate, expected=()):
    lines, ok = [], True
    rc, out, err = run([need('qpdf'), '--check', dst])
    msg = (out + err).decode('utf-8', 'replace')
    if rc == 0 and 'No syntax or stream encoding errors found' in msg and 'WARNING' not in msg:
        lines.append('qpdf --check: clean')
    else:
        ok = False
        lines.append('qpdf --check: FAILED (rc %d): %s' % (rc, ' | '.join(l for l in msg.split('\n') if 'WARN' in l or 'ERR' in l)[:300]))
    chk = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'os88pdfcheck.py')
    if os.path.isfile(chk):
        rc, out, err = run([sys.executable, '-I', chk, 'validate', dst])
        if rc == 0:
            lines.append('os88pdfcheck validate: PASS')
        elif rc == 3:
            lines.append('os88pdfcheck validate: SKIPPED (a tool is missing)')
        else:
            ok = False
            lines.append('os88pdfcheck validate: FAIL ' + (out + err).decode('utf-8', 'replace').strip()[:200])
    else:
        lines.append('os88pdfcheck validate: not run (tools/os88pdfcheck.py not found)')
    errs, leftover = profile_check(dst, flate, npages)
    if errs:
        ok = False
        lines.append('profile check: FAILED: ' + '; '.join(errs))
    else:
        lines.append('profile check: classic xref, 20-byte entries, no object/xref streams, no encryption, %d page%s' % (
            npages, '' if npages == 1 else 's'))
    if leftover:
        lines.append('  note: filters PageDesk lacks remain in %s (images; their pages are listed above)' % (
            ', '.join('%s x%d' % kv for kv in sorted(leftover.items()))))
    pde = a.pde_test or os.environ.get('PDE_TEST')
    if not pde:
        cand = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'build', 'pde_test')
        pde = cand if os.path.isfile(cand) else None
    if flate and pde:
        lines.append('PDENG host harness: not run (this tier needs the Flate filter, PD-25)')
    elif pde and os.path.isfile(pde):
        rc, out, err = run([pde, 'dump', dst])
        s = out.decode('utf-8', 'replace')
        m = re.search(r'^OPEN ok pages=(\d+)', s, re.M)
        bad = []                                          # (1-based page, line) of every failing harness line
        for ln in s.split('\n'):
            mm = re.match(r'^(?:CONTENT|TOKEN|PAGE)\s+(\d+)\b.*?\b(?:read)?rc=[1-9]\d*', ln)
            if mm:
                bad.append((int(mm.group(1)) + 1, ln))
            elif re.match(r'^(?:OBJ|ANNOT)\b.*\brc=[1-9]', ln):
                bad.append((0, ln))
        stray = [ln for pg, ln in bad if pg == 0 or pg not in expected]
        if rc != 0 or not m or int(m.group(1)) != npages or stray:
            ok = False
            lines.append('PDENG host harness: FAILED: ' + ' | '.join((stray or s.split('\n'))[:2])[:240])
        elif bad:
            lines.append('PDENG host harness: opens, %s pages; the engine refuses page(s) %s, which are the incomplete ones reported above' % (
                m.group(1), ','.join(str(x) for x in sorted({pg for pg, _ in bad}))))
        else:
            lines.append('PDENG host harness: opens, %s pages, every content stream decodes and tokenizes' % m.group(1))
    else:
        lines.append('PDENG host harness: not run (build/pde_test not built; make build/pde_test)')
    return {'ok': ok, 'lines': lines}


# ------------------------------------------------------------------ main ---
def main(argv=None):
    ap = argparse.ArgumentParser(description='Prepare a PDF for PageDesk (PAGEDESK-SPEC 20.14).')
    ap.add_argument('input')
    ap.add_argument('output')
    ap.add_argument('--flate', action='store_true', help='Enhanced tier: keep/re-encode streams with Flate (default: XT, no Flate)')
    ap.add_argument('--password', help='decrypt with this password (visible to ps; --password-file is safer)')
    ap.add_argument('--password-file', help='file whose first line is the password')
    ap.add_argument('--raster', default='none', metavar='none|auto|all|PAGES',
                    help='rasterise pages: auto = those PageDesk cannot draw fully; or a list like 2,5-7 (default none)')
    ap.add_argument('--fonts', choices=('keep', 'raster'), default='keep',
                    help='raster also pages whose fonts PageDesk would substitute (default keep)')
    ap.add_argument('--dpi', type=float, default=100.0, help='raster resolution (default 100)')
    ap.add_argument('--bits', type=int, choices=(1, 8), default=None, help='raster depth: 1 (dithered mono) or 8 (grey); default 1')
    ap.add_argument('--target', choices=('vga', 'cga', 'herc'), help='default depth for the adapter (vga 8-bit, cga/herc 1-bit)')
    ap.add_argument('--strip-bytes', type=int, default=60000, help='largest decoded raster strip (default 60000)')
    ap.add_argument('--no-text-layer', dest='text_layer', action='store_false', help='no invisible text over raster pages')
    ap.add_argument('--pages', help='keep only these pages (qpdf syntax, e.g. 1-100); needed above 1024 pages')
    ap.add_argument('--force', action='store_true', help='replace an existing OUT (never the input)')
    ap.add_argument('--pde-test', help='path of the PDENG host harness (default build/pde_test)')
    ap.add_argument('--report', help='write the report here (default OUT.report.txt; - for none)')
    ap.add_argument('--version', action='version', version='os88pdprep %s' % VERSION)
    a = ap.parse_args(argv)
    if a.bits is None:
        a.bits = 8 if a.target == 'vga' else 1
    if a.dpi < 20 or a.dpi > 600:
        ap.error('--dpi must be 20..600')
    if a.strip_bytes < 1024:
        ap.error('--strip-bytes must be at least 1024')
    rep = Report()
    try:
        rc = prepare(a, rep)
    except Refused as e:
        print('os88pdprep: refused: %s' % e, file=sys.stderr)
        return EXIT_REFUSED
    except NoTool as e:
        print('os88pdprep: SKIP: %s; nothing was prepared' % e, file=sys.stderr)
        return EXIT_NOTOOL
    sys.stdout.write(rep.text())
    if a.report != '-':
        rp = a.report or (a.output + '.report.txt')
        with open(rp, 'w') as f:
            f.write(rep.text())
    return rc


if __name__ == '__main__':
    sys.exit(main())
