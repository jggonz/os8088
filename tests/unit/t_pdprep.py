#!/usr/bin/env python3
"""tools/os88pdprep.py, PageDesk's host preparation tool (PD-22, PAGEDESK-SPEC 20.14).

    python3 tests/unit/t_pdprep.py

Generated inputs only (nothing committed, nothing fetched): a six-page PDF the
XT profile cannot draw in full is written classic, then rewritten by qpdf with
object streams and Flate, then prepared, and the DERIVATIVE is read back by
tools that share no code with the tool - qpdf, mutool, and tools/os88pdfcheck.py -
and by PageDesk's own PDENG host harness when build/pde_test exists.

What is asserted, and what each defends:
  * the pure helpers (RunLength encoder, ordered dither, content tokenizer,
    page lists) - a wrong dither or an `EI` found inside image data corrupts
    pages that look fine in the report;
  * the pipeline: xref/object streams and Flate gone, a classic 20-byte
    cross-reference, the right pages classified and the right ones rasterised
    into strips under the cap with an invisible text layer that mutool can
    extract, the Info stamp binding the derivative to its source, and the
    output byte-identical on a second run (the build must be reproducible);
  * the refusals: the input as its own output, an existing output, an
    encrypted file without a password, a wrong password;
  * a negative control - the independent profile check must FAIL on a file
    that still has object streams, or a "clean" verdict means nothing.
Without qpdf the pipeline half SKIPs and says so; it never passes silently.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done, eq                       # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import os88pdprep as prep                                 # noqa: E402


def rl_decode(b):
    out, i = bytearray(), 0
    while i < len(b) and b[i] != 128:
        n = b[i]
        if n < 128:
            out += b[i + 1:i + 2 + n]
            i += 2 + n
        else:
            out += b[i + 1:i + 2] * (257 - n)
            i += 2
    return bytes(out)


# ---------------------------------------------------------------- helpers --
for data in (b'', b'a', b'aa', b'abcabc', b'\x00' * 300, bytes(range(256)) * 3, b'aab' * 50 + b'z' * 129):
    eq(rl_decode(prep.rl_encode(data)), data, 'RunLength round-trips %d bytes' % len(data),
       'the strips are stored RunLength; a wrong run length is a corrupt page, not an error')
check(len(prep.rl_encode(b'\xff' * 6000)) < 100, 'a white strip RunLength-encodes to a handful of bytes')

bits, stride = prep.dither_1bit(bytes([255] * 20), 10, 2)
eq((bits, stride), (b'\xff\xc0' * 2, 2), 'dither: white is 1 bits, padded 0 in the last byte')
bits, _ = prep.dither_1bit(bytes([0] * 16), 8, 2)
eq(bits, b'\x00\x00', 'dither: black is 0 bits')
bits, _ = prep.dither_1bit(bytes([128] * 64), 8, 8)
ones = sum(bin(x).count('1') for x in bits)
check(28 <= ones <= 36, 'dither: mid grey is about half white (%d of 64)' % ones)

ops = list(prep.Tok(b'q /F1 12 Tf (a)(b\\)c(d)) Tj [(x) -3 (y)] TJ <48 65> Tj /GS1 gs BT ET % (comment\nQ').ops())
eq([o[0] for o in ops], ['q', 'Tf', 'Tj', 'TJ', 'Tj', 'gs', 'BT', 'ET', 'Q'], 'tokenizer: operators through strings, arrays, hex and comments')
eq(ops[1][1], ['/F1', 12.0], 'tokenizer: Tf operands')
img = b'BI /W 2 /H 2 /BPC 8 /CS /G ID \x01\x02\nEI\x04 Q EI Q /Im0 Do'
ops = list(prep.Tok(img).ops())
eq([o[0] for o in ops], ['BI', 'Q', 'Do'], 'tokenizer: an inline image ends at the EI that is a token, not at one inside its data')
eq(ops[0][2][0].get('/W'), 2.0, 'tokenizer: inline dictionary')
eq(sorted(prep.parse_pages('2,5-7,40', 10)), [2, 5, 6, 7], 'page list: ranges, commas and out-of-range numbers')
try:
    prep.parse_pages('x', 3)
    check(False, 'a bad page list is refused')
except prep.Refused:
    check(True, 'a bad page list is refused')

if not shutil.which('qpdf'):
    print('t_pdprep: SKIP qpdf not installed; the pipeline half did NOT run')
    done('t_pdprep (helpers only)')


# ---------------------------------------------------------------- inputs ---
def build(jpeg):
    objs = {}

    def add(n, body):
        objs[n] = body if isinstance(body, bytes) else body.encode()

    def stm(d, data):
        return ('<< %s /Length %d >>\nstream\n' % (d, len(data))).encode() + data + b'\nendstream'

    add(1, '<< /Type /Catalog /Pages 2 0 R >>')
    add(2, '<< /Type /Pages /Kids [10 0 R 11 0 R 12 0 R 13 0 R 14 0 R 15 0 R] /Count 6 /MediaBox [0 0 200 200] '
           '/Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> >>')
    add(3, '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>')
    add(4, '<< /Type /Font /Subtype /TrueType /BaseFont /Arial /FirstChar 32 /LastChar 32 /Widths [278] >>')
    add(10, '<< /Type /Page /Parent 2 0 R /Contents 20 0 R >>')
    add(20, stm('', b'BT /F1 14 Tf 20 150 Td (Hello native page) Tj ET 20 100 100 30 re S'))
    add(11, '<< /Type /Page /Parent 2 0 R /Contents 21 0 R /Resources << /Shading << /Sh1 30 0 R >> /Font << /F1 3 0 R >> >> >>')
    add(21, stm('', b'q 10 10 180 100 re W n /Sh1 sh Q BT /F1 12 Tf 20 150 Td (Shaded page) Tj ET'))
    add(30, '<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [10 0 190 0] /Function << /FunctionType 2 /Domain [0 1] '
            '/C0 [1 0 0] /C1 [0 0 1] /N 1 >> >>')
    add(12, '<< /Type /Page /Parent 2 0 R /Contents 22 0 R /Resources << /ExtGState << /G1 << /ca 0.5 >> >> /Font << /F2 4 0 R >> >> >>')
    add(22, stm('', b'/G1 gs BT /F2 14 Tf 20 150 Td (Arial words) Tj ET 0.5 g 20 20 100 50 re f'))
    add(13, '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 2500 120] /Contents 24 0 R >>')
    add(24, stm('', b'BT /F1 60 Tf 20 30 Td (WIDE) Tj ET'))
    add(14, '<< /Type /Page /Parent 2 0 R /Contents 23 0 R /Resources << /XObject << /Im1 31 0 R >> >> >>')
    add(23, stm('', b'q 150 0 0 100 20 50 cm /Im1 Do Q'))
    add(31, stm('/Type /XObject /Subtype /Image /Width 12 /Height 12 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode',
                jpeg))
    add(15, '<< /Type /Page /Parent 2 0 R /Contents 25 0 R >>')
    add(25, stm('', b'q 100 0 0 100 20 20 cm BI /W 100 /H 100 /BPC 8 /CS /G ID ' + bytes(range(256)) * 39 + b'\nEI Q'))
    out = bytearray(b'%PDF-1.4\n')
    offs = {}
    for n in sorted(objs):
        offs[n] = len(out)
        out += ('%d 0 obj\n' % n).encode() + objs[n] + b'\nendobj\n'
    top = max(objs) + 1
    xr = len(out)
    out += ('xref\n0 %d\n' % top).encode()
    for n in range(top):
        out += b'0000000000 65535 f \n' if n == 0 else (
            ('%010d 00000 n \n' % offs[n]).encode() if n in offs else b'0000000000 00000 f \n')
    out += ('trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (top, xr)).encode()
    return bytes(out)


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, **kw)


with tempfile.TemporaryDirectory() as tmp:
    classic = os.path.join(tmp, 'classic.pdf')
    cx = os.path.join(tmp, 'complex.pdf')
    enc = os.path.join(tmp, 'enc.pdf')
    jpeg = None
    if shutil.which('pdftoppm'):                  # a REAL JPEG: qpdf --check decodes DCT and a fake one fails it
        tiny = os.path.join(tmp, 'tiny.pdf')
        # the builder's own file with a placeholder image, drawn at 9 dpi: page 1 is enough for a JPEG
        open(tiny, 'wb').write(build(b'\xff\xd8\xff\xd9'))
        sh(['pdftoppm', '-jpeg', '-r', '9', '-f', '1', '-l', '1', '-singlefile', tiny, os.path.join(tmp, 'j')])
        if os.path.exists(os.path.join(tmp, 'j.jpg')):
            jpeg = open(os.path.join(tmp, 'j.jpg'), 'rb').read()
    if jpeg is None:
        print('t_pdprep: note: no JPEG could be made (pdftoppm); the DCT page uses a placeholder')
        jpeg = b'\xff\xd8\xff\xd9'
    open(classic, 'wb').write(build(jpeg))
    r = sh(['qpdf', '--object-streams=generate', '--compress-streams=y', classic, cx])
    check(r.returncode == 0 and os.path.getsize(cx) > 0, 'the complex input (object streams, Flate) is written by qpdf')
    raw = open(cx, 'rb').read()
    check(b'/ObjStm' in raw and b'/XRef' in raw, 'the input really has object and xref streams')

    # a negative control first: the independent profile check must see what is wrong
    errs, _ = prep.profile_check(cx, False, 6)
    check(any('stream' in e for e in errs) or any('startxref' in e for e in errs),
          'negative control: profile_check REFUSES the object-stream input', 'a check that cannot fail proves nothing', got=errs)

    out = os.path.join(tmp, 'out.pdf')
    argv = [cx, out, '--raster', '2,3,4', '--pde-test', os.path.join(ROOT, 'build', 'pde_test'), '--report', '-']
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = prep.main(argv)
    rep = buf.getvalue()
    check(rc == 0, 'prepare exits 0 and verifies', got=rep[-700:], want='rc 0')
    check('RESULT: prepared and verified' in rep, 'the report says prepared and verified')
    for s in ('page 2    RASTERISED', 'page 3    RASTERISED', 'page 4    RASTERISED',
              'page 5    INCOMPLETE', 'page 6    INCOMPLETE', 'shading fill (sh)', 'transparency (ca 0.5)',
              'outside +/-2047 pt', 'DCTDecode', 'inline image of'):
        check(s in rep, 'report: %r' % s, got=rep)
    check(rep.count('page 1    native') == 1, 'report: page 1 is native')
    check('page scaled to 80%' in rep, 'report: the 2500 pt page was scaled to fit +/-2047 pt', got=rep)

    data = open(out, 'rb').read()
    check(not __import__('re').search(rb'/Type\s*/(ObjStm|XRef)', data), 'the derivative has no object or xref stream')
    errs, left = prep.profile_check(out, False, 6)
    eq(errs, [], 'the independent profile check passes the derivative')
    eq(sorted(left), ['/DCTDecode'], 'only the unsupported image keeps a filter PageDesk lacks (its page is reported)')
    check(b'/FlateDecode' not in data, 'no Flate on the XT tier')
    check(sh(['qpdf', '--check', out]).returncode == 0, 'qpdf --check is clean')

    js = json.loads(sh(['qpdf', '--json=2', '--json-stream-data=none', out]).stdout)
    objs = js['qpdf'][1]
    eq(len(js['pages']), 6, 'six pages out')
    info = [o['value'] for k, o in objs.items() if 'value' in o and '/PageDeskPrep' in o['value']]
    check(len(info) == 1, 'exactly one Info dictionary carries /PageDeskPrep')
    stamp = info[0]['/PageDeskPrep'] if info else ''
    import hashlib
    check(hashlib.sha256(raw).hexdigest() in stamp, 'the stamp names the original by SHA-256')
    check('raster=2,3,4' in stamp and 'tier=xt' in stamp, 'the stamp names the rasterised pages and the tier', got=stamp)
    strips = [o['stream']['dict'] for o in objs.values() if 'stream' in o and o['stream']['dict'].get('/Subtype') == '/Image'
              and o['stream']['dict'].get('/ColorSpace') == '/DeviceGray']
    check(len(strips) >= 3, 'rasterised pages are image strips (%d found)' % len(strips))
    for d in strips:
        w, h = d['/Width'], d['/Height']
        check(((w + 7) // 8) * h <= 60000, 'a strip stays under the 60000-byte cap (%dx%d)' % (w, h))
        check(d.get('/BitsPerComponent') == 1 and d.get('/Filter') in (None, '/RunLengthDecode'),
              'a strip is 1-bit, unfiltered or RunLength')
    if shutil.which('mutool'):
        t = sh(['mutool', 'draw', '-q', '-F', 'text', '-o', '-', out, '2']).stdout.decode('utf-8', 'replace')
        check('Shaded page' in t, 'the invisible text layer of a rasterised page is extractable by MuPDF', got=t)
        t = sh(['mutool', 'draw', '-q', '-F', 'text', '-o', '-', out, '4']).stdout.decode('utf-8', 'replace')
        check('WIDE' in t, 'the scaled page keeps its word', got=t)
        t = sh(['mutool', 'draw', '-q', '-F', 'text', '-o', '-', out, '1']).stdout.decode('utf-8', 'replace')
        check('Hello native page' in t, 'a native page keeps its vector text', got=t)
    else:
        print('t_pdprep: note: mutool missing, the text-layer checks did NOT run')

    # refusals
    def refuses(args, why):
        e = io.StringIO()
        with contextlib.redirect_stderr(e), contextlib.redirect_stdout(io.StringIO()):
            r = prep.main(args)
        return r, e.getvalue()

    r, e = refuses([cx, cx, '--force'], 'in-place')
    check(r == 1 and 'never overwritten' in e, 'the input is never its own output (even with --force)', got=(r, e))
    r, e = refuses([cx, out, '--report', '-'], 'exists')
    check(r == 1 and 'exists' in e, 'an existing output is refused without --force', got=(r, e))
    before = open(cx, 'rb').read()
    eq(before, raw, 'the original is untouched after all of it')
    r = sh(['qpdf', '--encrypt', 'upw', 'opw', '256', '--', classic, enc])
    if r.returncode == 0:
        o3 = os.path.join(tmp, 'dec.pdf')
        r, e = refuses([enc, o3], 'enc')
        check(r == 1 and 'encrypted' in e and not os.path.exists(o3), 'an encrypted file is refused without a password', got=(r, e))
        r, e = refuses([enc, o3, '--password', 'nope'], 'wrongpw')
        check(r == 1 and not os.path.exists(o3), 'a wrong password is refused', got=(r, e))
        pwf = os.path.join(tmp, 'pw.txt')
        open(pwf, 'w').write('upw\nnot the password\n')
        with contextlib.redirect_stdout(io.StringIO()) as _o:
            r = prep.main([enc, o3, '--password-file', pwf, '--report', '-', '--pde-test', os.path.join(ROOT, 'build', 'pde_test')])
        check(r == 0 and b'/Encrypt' not in open(o3, 'rb').read(),
              'with the password (the FIRST line of --password-file) the derivative is decrypted and verified', got=r)
    else:
        print('t_pdprep: note: qpdf cannot encrypt here, the password checks did NOT run')

    # ---- what verify-host found (each was a real defect in the first version)
    rpt = os.path.join(tmp, 'v.pdf.report.txt')
    shutil.copy(cx, rpt)
    r, e = refuses([rpt, os.path.join(tmp, 'v.pdf')], 'report-over-input')
    check(r == 1 and 'never overwritten' in e and open(rpt, 'rb').read() == raw,
          'the default report name (OUT.report.txt) is never the input', got=(r, e))

    # a /Pages node that crops and turns its pages, a wide shaded page, an
    # unsorted /Dests name tree and no /ID - prepared from an input NAMED
    # OUT.tmp, which the first version wrote over and renamed away
    d2 = {1: '<< /Type /Catalog /Pages 2 0 R /Names << /Dests 6 0 R >> >>',
          2: '<< /Type /Pages /Kids [3 0 R] /Count 1 /Rotate 90 /MediaBox [0 0 612 792] /CropBox [0 0 306 792] '
             '/Resources << /Font << /F1 5 0 R >> /Shading << /S 7 0 R >> >> >>',
          3: '<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>',
          5: '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
          6: '<< /Names [(zeta) [3 0 R /Fit] (alpha) [3 0 R /Fit]] >>',
          7: '<< /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 612 0] /Function << /FunctionType 2 '
             '/Domain [0 1] /C0 [0] /C1 [1] /N 1 >> >>'}
    body = b'q 0 0 612 792 re W n /S sh Q BT /F1 30 Tf 72 500 Td (two words) Tj ET'
    out2 = bytearray(b'%PDF-1.4\n')
    offs = {}
    for n in sorted(d2):
        offs[n] = len(out2)
        out2 += ('%d 0 obj\n%s\nendobj\n' % (n, d2[n])).encode()
    offs[4] = len(out2)
    out2 += b'4 0 obj\n<< /Length %d >>\nstream\n' % len(body) + body + b'\nendstream\nendobj\n'
    xr = len(out2)
    out2 += b'xref\n0 8\n0000000000 65535 f \n' + b''.join(b'%010d 00000 n \n' % offs[n] for n in range(1, 8))
    out2 += b'trailer\n<< /Size 8 /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % xr
    vin = os.path.join(tmp, 'w.pdf.tmp')
    open(vin, 'wb').write(bytes(out2))
    w1, w2 = os.path.join(tmp, 'w.pdf'), os.path.join(tmp, 'w2.pdf')
    pa = ['--raster', 'auto', '--bits', '8', '--dpi', '150', '--report', '-', '--pde-test', os.path.join(ROOT, 'build', 'pde_test')]
    with contextlib.redirect_stdout(io.StringIO()) as o:
        r = prep.main([vin, w1] + pa)
    rep2 = o.getvalue()
    check(r == 0 and open(vin, 'rb').read() == bytes(out2), 'an input named OUT.tmp is untouched and the run verifies',
          'the first version wrote qpdf\'s output to OUT + ".tmp" and renamed it: the original was destroyed', got=rep2[-600:])
    check(not [f for f in os.listdir(tmp) if f.startswith('.pdprep-')], 'no temporary file is left beside the output')
    if os.path.exists(w1):                        # every check below runs even when the run itself failed
        with contextlib.redirect_stdout(io.StringIO()):
            prep.main([vin, w2] + pa)
        eq(open(w2, 'rb').read() if os.path.exists(w2) else None, open(w1, 'rb').read(), 'an input with no /ID still prepares byte-identically',
           'without --deterministic-id on the first qpdf pass the /ID was random')
        js = json.loads(sh(['qpdf', '--json=2', '--json-stream-data=none', w1]).stdout)
        objs = js['qpdf'][1]

        def val(r):
            o = objs.get('obj:' + r, {})
            return o.get('value') or o.get('stream', {}).get('dict')
        pg = val(js['pages'][0]['object'])
        mb = pg.get('/MediaBox')
        check(pg.get('/Rotate') == 0 and pg.get('/CropBox') == mb,
              'a rasterised page sets /Rotate 0 and /CropBox itself',
              'they are inheritable: the /Pages node\'s turned and cropped the picture a second time', got=pg)
        check(isinstance(mb, list) and abs(mb[2] - 792) < 1 and abs(mb[3] - 306) < 1,
              'the picture is the page as displayed (cropped to 306 pt, turned to 792 x 306)', got=mb)
        rows = [((o['stream']['dict']['/Width'] * o['stream']['dict']['/BitsPerComponent'] + 7) // 8)
                for o in objs.values() if 'stream' in o and o['stream']['dict'].get('/Subtype') == '/Image']
        check(rows and max(rows) <= 1024 and len(rows) > 1,
              'an 8-bit raster 1,650 px wide is cut into columns whose rows are at most 1,024 bytes',
              'PDREND refuses a longer image row (PAGEDESK-SPEC 20.3.2)', got=rows)
        check('in 2 columns' in rep2, 'the report says the page was cut into columns', got=rep2)
        r = sh(['qpdf', '--check', w1])
        check(r.returncode == 0 and b'WARNING' not in r.stdout + r.stderr, 'an unsorted /Dests tree is rebuilt: qpdf --check is clean',
              got=(r.stdout + r.stderr)[-300:])
        cat = val(js['qpdf'][1]['trailer']['value']['/Root'])
        nd = cat['/Names'] if isinstance(cat['/Names'], dict) else val(cat['/Names'])
        root = val(nd['/Dests'])
        keys = []
        for kref in root.get('/Kids', []):
            keys += val(kref)['/Names'][0::2]
        eq(keys, ['u:alpha', 'u:zeta'], 'the rebuilt /Dests tree is sorted')

    # the invisible layer of the six-page file's shaded page carries the space
    # between its two words, so a reader that joins glyphs gets "Shaded page"
    j1 = json.loads(sh(['qpdf', '--json=2', '--json-stream-data=none', out]).stdout)
    c2 = j1['qpdf'][1]['obj:' + j1['pages'][1]['object']]['value']['/Contents']
    j2 = json.loads(sh(['qpdf', '--json=2', '--json-stream-data=inline', '--json-object=' + c2.split()[0], out]).stdout)
    cdata = prep.b64(j2['qpdf'][1]['obj:' + c2]['stream']['data'])
    check(b'(Shaded ) Tj' in cdata and b'(page) Tj' in cdata, 'the invisible words carry the space between them',
          got=cdata[-200:])

done('t_pdprep')
