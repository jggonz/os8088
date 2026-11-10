#!/usr/bin/env python3
"""PDREND's per-document caches against a fresh renderer (verify-perf).

  pdr_cache.py <pdr_test_real> <scratch dir> <resource dir> <samples dir>

perf made the font cache (with its /Widths) and Tf's resource cache live for
the DOCUMENT - verb_init empties them, interp_start no longer does - so a page
is drawn with what earlier pages left behind. This holds that to one rule:
every page drawn in a session (pdr_test's PDR_SEQ: one process, page after
page, INIT only where the primary would INIT) is the page a fresh process
draws, pixel for pixel and status line for status line.

The fixtures are generated here, because the samples cannot catch it: each
page of CACHEFX.PDF says the same words in /F1, and /F1 is a different font
on every page (a form XObject has its own /F1; page 3 inherits the /Pages
node's; page 4 asks for generation 0 of an object whose live generation is 1,
page 5 for generation 1), two of the fonts carry an incompleteness note, and
CACHEFX2.PDF is the same object numbers holding other fonts. CACHEPAD.PDF
spreads the fonts 13 KiB apart so a page's font load READS, and a read is
failed there (the session's :Fn - a disk taken out): the next draw of that
page must be what a fresh process draws, not the gap the failure left.

Negative controls (verify-perf, by hand): keying Tf's cache without the
resource context, or not emptying the font cache at INIT, or keeping a font
whose load failed a read, each fails this script.
"""
import os, subprocess, sys

exe, scratch, resdir, samples = (os.path.abspath(a) for a in sys.argv[1:5])
os.makedirs(scratch, exist_ok=True)
bank = os.path.join(scratch, 'fd'); os.makedirs(bank, exist_ok=True)
data = open(os.path.join(resdir, 'PDRES.BIN'), 'rb').read()
open(os.path.join(bank, 'PDRES.BIN'), 'wb').write(data)
open(os.path.join(bank, 'PAGEDESK.O88'), 'wb').write(b'\x55' * 1536 + data)
if os.path.exists(os.path.join(resdir, 'PDFONTX.RES')):
    open(os.path.join(bank, 'PDFONTX.RES'), 'wb').write(open(os.path.join(resdir, 'PDFONTX.RES'), 'rb').read())


def cachefx(path, alt=False, pad=False):
    def W(w0, step, n=95):
        return '[' + ' '.join(str(w0 + (i * step) % 400) for i in range(n)) + ']'
    fonts = {10: ('Helvetica', 500, 7), 11: ('Times-Roman', 250, 13), 12: ('ABCDEF+Zapfino', 600, 0),
             13: ('Helvetica-Bold', 700, 3), 14: ('Symbol', 300, 11)}
    if alt:
        fonts = {10: fonts[12], 11: fonts[13], 12: fonts[14], 13: fonts[10], 14: fonts[11]}
    objs = {}
    for o, (b, w0, st) in fonts.items():
        objs[(o, 0)] = '<< /Type /Font /Subtype /Type1 /BaseFont /%s /FirstChar 32 /LastChar 126 /Widths %s ' \
                       '/Encoding /WinAnsiEncoding >>' % (b, W(w0, st))
    objs[(15, 1)] = '<< /Type /Font /Subtype /Type1 /BaseFont /Courier-Bold /FirstChar 40 /Widths 16 0 R >>'
    objs[(16, 0)] = W(900, 37, n=80)
    text = b'BT /F1 18 Tf 36 700 Td (The quick brown fox jumps over the lazy dog 0123) Tj 0 -30 Td (AVAWAY iiii mmmm WWWW) Tj ET'
    form = b'BT /F1 14 Tf 36 600 Td (form says hello with its own F1) Tj ET'
    streams = {20: text + b'\nq 1 0 0 1 0 0 cm /Fm1 Do Q', 21: text, 22: text, 23: text, 24: text, 25: form}
    pages = [(30, 20, '<< /Font << /F1 10 0 R >> /XObject << /Fm1 25 0 R >> >>'),
             (31, 21, '<< /Font << /F1 11 0 R >> >>'),
             (32, 22, None),
             (33, 23, '<< /Font << /F1 15 0 R >> >>'),
             (34, 24, '<< /Font << /F1 15 1 R >> >>')]
    for p, c, r in pages:
        objs[(p, 0)] = '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents %d 0 R%s >>' % (
            c, (' /Resources ' + r) if r else '')
    objs[(2, 0)] = '<< /Type /Pages /Kids [%s] /Count %d /Resources << /Font << /F1 12 0 R /F9 13 0 R >> >> >>' % (
        ' '.join('%d 0 R' % p for p, _, _ in pages), len(pages))
    objs[(1, 0)] = '<< /Type /Catalog /Pages 2 0 R >>'
    body = bytearray(b'%PDF-1.4\n%\xe2\xe3\xcf\xd3\n')
    off = {}

    def put(num, gen, d):
        off[num] = (len(body), gen)
        body.extend(b'%d %d obj\n' % (num, gen) + d + b'\nendobj\n')
    for (num, gen) in sorted(objs):
        put(num, gen, objs[(num, gen)].encode())
        if pad and 10 <= num <= 16:
            body.extend((b'%' + b'p' * 78 + b'\n') * 160)
    for s, d in streams.items():
        head = b'<< /Length %d' % len(d)
        if s == 25:
            head += b' /Type /XObject /Subtype /Form /BBox [0 0 612 792] /Resources << /Font << /F1 14 0 R >> >>'
        put(s, 0, head + b' >>\nstream\n' + d + b'\nendstream')
    size = max(off) + 1
    x = b'xref\n0 %d\n0000000000 65535 f \n' % size
    for n in range(1, size):
        x += (b'%010d %05d n \n' % off[n]) if n in off else b'0000000000 00000 f \n'
    start = len(body)
    body += x + b'trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (size, start)
    open(path, 'wb').write(body)


C, C2, CP = (os.path.join(scratch, n) for n in ('CACHEFX.PDF', 'CACHEFX2.PDF', 'CACHEPAD.PDF'))
cachefx(C); cachefx(C2, alt=True); cachefx(CP, pad=True)


def render(seq, out, bpp, dlkb, extra_env=()):
    env = dict(os.environ, PDR_GLYPHKB='24', PDR_RFLAGS='1', **dict(extra_env))
    if seq:
        env['PDR_SEQ'] = seq
    r = subprocess.run([exe, 'render', 'X' if seq else out[0], '0' if seq else out[1], '50', '0', str(bpp), '1/1',
                        '420', '420', '0', '0', out if seq else out[2], bank, str(dlkb)],
                       capture_output=True, text=True, env=env)
    return r.returncode, [l.split(' blits=')[0] for l in r.stdout.splitlines() if l.startswith('status')], r.stderr


fails = []
n = 0
S = os.path.join(samples, 'XTGUIDE.PDF')
seq = ['%s:%d' % (S, p) for p in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 5, 0)]
seq += ['%s:0' % os.path.join(samples, f) for f in ('HELLO.PDF', 'LATIN.PDF', 'MARKUP.PDF', 'ACROFORM.PDF')]
seq += ['%s:%d' % (C, p) for p in (2, 0, 2, 1, 3, 4, 0)]
seq += ['%s:%d' % (C2, p) for p in (0, 1, 2)] + ['%s:2:R' % C2, '%s:0' % C2, '%s:3' % S, '%s:4:R' % S]
for bpp in (4, 1):
    for dlkb in (22, 8):
        base = os.path.join(scratch, 's%d_%d' % (bpp, dlkb))
        rc, st, err = render(' '.join(seq), base, bpp, dlkb)
        if rc or len(st) != len(seq):
            fails.append('session bpp %d dl %d: rc=%d, %d of %d pages (%s)' % (bpp, dlkb, rc, len(st), len(seq), err.strip()[:120]))
            continue
        for k, e in enumerate(seq):
            f, p = e.split(':')[0], e.split(':')[1]
            fr = os.path.join(scratch, 'fresh.ppm')
            rc2, st2, err2 = render(None, (f, p, fr), bpp, dlkb)
            n += 1
            if rc2 or st2 != [st[k]]:
                fails.append('%s bpp %d dl %d #%d: session says [%s], fresh [%s]' % (os.path.basename(e), bpp, dlkb, k, st[k], ' '.join(st2) or err2.strip()[:80]))
            elif open('%s.%d.ppm' % (base, k), 'rb').read() != open(fr, 'rb').read():
                fails.append('%s bpp %d dl %d #%d: the session drew other pixels than a fresh process' % (os.path.basename(e), bpp, dlkb, k))
# a read failing while a font loads: that page again must be what a fresh process draws
fr = os.path.join(scratch, 'fresh.ppm')
_, want, _ = render(None, (CP, '4', fr), 4, 22, (('PDR_CLUSTER', '1'),))
wantpix = open(fr, 'rb').read()
hit = 0
for f in range(1, 9):
    base = os.path.join(scratch, 'io')
    for k in range(3):
        if os.path.exists('%s.%d.ppm' % (base, k)):
            os.unlink('%s.%d.ppm' % (base, k))
    rc, st, err = render('%s:0 %s:4:F%d %s:4' % (CP, CP, f, CP), base, 4, 22, (('PDR_CLUSTER', '1'),))
    n += 1
    if rc or len(st) != 3 or not os.path.exists('%s.2.ppm' % base):
        fails.append('fault %d: rc=%d %s' % (f, rc, err.strip()[:120])); continue
    if not os.path.exists('%s.1.ppm' % base) or open('%s.1.ppm' % base, 'rb').read() != wantpix:
        hit += 1                                    # the fault reached what was drawn (or failed it)
    if st[2] != want[0] or open('%s.2.ppm' % base, 'rb').read() != wantpix:
        fails.append('fault at read %d: the next draw of the page kept the failure (%s)' % (f, st[2]))
if not hit:
    fails.append('no fault reached a font load: the fixture no longer exercises it')
for m in fails[:12]:
    print('FAIL', m)
print('pdr_cache: %d renders against a fresh process, %d fault cases reached the page, %d FAILED' % (n, hit, len(fails)))
sys.exit(1 if fails else 0)
