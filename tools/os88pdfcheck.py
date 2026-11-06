#!/usr/bin/env python3
"""PageDesk's INDEPENDENT checker (docs/PAGEDESK-SPEC.md 18.4, 20.1).

A thin wrapper over three readers that share no code with PageDesk or with
tools/os88pdfgen.py: `qpdf` (structure), `mutool` (MuPDF) and `pdftoppm`
(Poppler). A tool that is not installed SKIPs with a message and exit code 3 -
it never passes, and it never looks like a pass.

    os88pdfcheck.py validate FILE...          qpdf --check; PASS only when qpdf is
                                              clean (warnings fail unless --warn-ok)
    os88pdfcheck.py render FILE PAGE OUT      OUT.png/.pgm/.pbm via mutool, else pdftoppm
              [--dpi N] [--gray]
    os88pdfcheck.py annots FILE PAGE          the annotations of PAGE as JSON: subtype,
                                              Rect, QuadPoints, Contents, NM, IRT, State,
                                              StateModel, Flags, T, AP (bool), Parent ref
    os88pdfcheck.py compare A.pgm B.pgm       pixelwise: exit 0 when at most --frac of
              [--tol N] [--frac F]            pixels differ by more than --tol (0..255)
    os88pdfcheck.py text FILE [PAGE]          mutool's text of a page (find a string)

Exit codes: 0 pass, 1 fail, 2 usage, 3 SKIP (tool missing).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

SKIP = 3


def tool(name):
    p = shutil.which(name)
    if not p:
        for d in ('/opt/homebrew/bin', '/usr/local/bin'):
            if os.path.exists(os.path.join(d, name)):
                return os.path.join(d, name)
    return p


def run(cmd, timeout=60):
    try:
        r = subprocess.run(cmd, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 124, b'', b'timeout'
    return r.returncode, r.stdout, r.stderr


def skip(what):
    print('SKIP: %s is not installed; this check did NOT run and does NOT pass' % what, file=sys.stderr)
    return SKIP


def cmd_validate(a):
    q = tool('qpdf')
    if not q:
        return skip('qpdf')
    worst = 0
    for f in a.files:
        rc, out, err = run([q, '--check', f])
        text = (out + err).decode('latin-1')
        warn = 'WARNING' in text or rc == 3
        ok = rc == 0 or (rc == 3 and a.warn_ok)
        print('%s %s' % ('PASS' if ok else 'FAIL', f))
        if not ok:
            for ln in text.splitlines()[:6]:
                print('    ' + ln)
            worst = 1
        elif warn and not a.warn_ok:
            pass
    return worst


def render(f, page, out, dpi=72, gray=False):
    """Returns (rc, tool-name). The output type follows OUT's extension."""
    m = tool('mutool')
    ext = os.path.splitext(out)[1].lower()
    if m:
        cmd = [m, 'draw', '-r', str(dpi)]
        if gray or ext in ('.pgm', '.pbm'):
            cmd += ['-c', 'gray']
        rc, o, e = run(cmd + ['-o', out, f, str(page)])
        if rc == 0 and os.path.exists(out):
            return 0, 'mutool'
    p = tool('pdftoppm')
    if p and ext in ('.png', '.pgm', '.ppm'):
        base = out[:-len(ext)]
        flag = {'.png': '-png', '.pgm': '-gray', '.ppm': ''}[ext]
        cmd = [p, '-r', str(dpi), '-f', str(page), '-l', str(page), '-singlefile']
        if flag:
            cmd.append(flag)
        rc, o, e = run(cmd + [f, base])
        if rc == 0:
            return 0, 'pdftoppm'
    if not m and not p:
        return SKIP, None
    return 1, None


def cmd_render(a):
    rc, t = render(a.file, a.page, a.out, a.dpi, a.gray)
    if rc == SKIP:
        return skip('mutool and pdftoppm')
    print('%s %s page %d -> %s (%s)' % ('PASS' if rc == 0 else 'FAIL', a.file, a.page, a.out, t))
    return rc


def _resolve(objs, v):
    if isinstance(v, str) and v.endswith(' R') and v[0].isdigit():
        return objs.get('obj:' + v, {}).get('value', v)
    return v


def _plain(v):
    """qpdf's json strings: u:text (unicode) or b:hex (bytes)."""
    if isinstance(v, str):
        if v.startswith('u:'):
            return v[2:]
        if v.startswith('b:'):
            return bytes.fromhex(v[2:]).decode('latin-1')
    return v


def annots(f, page):
    q = tool('qpdf')
    if not q:
        return None
    rc, out, err = run([q, '--json=2', '--json-stream-data=none', f])
    if rc not in (0, 3):
        raise RuntimeError('qpdf: ' + err.decode('latin-1')[:200])
    d = json.loads(out)
    objs = d['qpdf'][1]
    pages = d['pages']
    if not 1 <= page <= len(pages):
        raise RuntimeError('page %d of %d' % (page, len(pages)))
    pobj = objs['obj:' + pages[page - 1]['object']]['value']
    arr = _resolve(objs, pobj.get('/Annots', []))
    res = []
    for ref in arr or []:
        o = _resolve(objs, ref)
        if not isinstance(o, dict):
            res.append({'ref': ref, 'error': 'not a dictionary'})
            continue
        ap = _resolve(objs, o.get('/AP'))
        rec = {'ref': ref if isinstance(ref, str) else None,
               'subtype': (o.get('/Subtype') or '').lstrip('/'),
               'rect': o.get('/Rect'), 'quadpoints': o.get('/QuadPoints'),
               'contents': _plain(o.get('/Contents')), 'nm': _plain(o.get('/NM')),
               'irt': o.get('/IRT'), 'state': _plain(o.get('/State')),
               'statemodel': _plain(o.get('/StateModel')), 'flags': o.get('/F'),
               'author': _plain(o.get('/T')), 'has_ap': isinstance(ap, dict) and '/N' in ap,
               'inklist': o.get('/InkList'), 'line': o.get('/L'), 'popup_parent': o.get('/Parent')}
        res.append({k: v for k, v in rec.items() if v is not None})
    return res


def cmd_annots(a):
    if not tool('qpdf'):
        return skip('qpdf')
    try:
        r = annots(a.file, a.page)
    except RuntimeError as e:
        print('FAIL: %s' % e, file=sys.stderr)
        return 1
    json.dump(r, sys.stdout, indent=1, sort_keys=True)
    print()
    return 0


def read_pnm(path):
    with open(path, 'rb') as f:
        data = f.read()
    kind = data[:2]
    toks, i = [], 2
    need = 2 if kind == b'P4' else 3
    while len(toks) < need:
        while data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b'#':
            while data[i:i + 1] != b'\n':
                i += 1
            continue
        j = i
        while not data[j:j + 1].isspace():
            j += 1
        toks.append(int(data[i:j]))
        i = j
    i += 1
    w, h = toks[0], toks[1]
    if kind == b'P5':
        return w, h, data[i:i + w * h]
    if kind == b'P4':
        rb = (w + 7) // 8
        px = bytearray()
        for y in range(h):
            row = data[i + y * rb:i + (y + 1) * rb]
            for x in range(w):
                px.append(0 if row[x >> 3] & (0x80 >> (x & 7)) else 255)
        return w, h, bytes(px)
    raise ValueError('only P4/P5 are read: ' + path)


def compare(pa, pb, tol=16, frac=0.01):
    wa, ha, da = read_pnm(pa)
    wb, hb, db = read_pnm(pb)
    if (wa, ha) != (wb, hb):
        return False, 'size %dx%d against %dx%d' % (wa, ha, wb, hb), 1.0
    bad = sum(1 for x, y in zip(da, db) if abs(x - y) > tol)
    f = bad / float(len(da))
    return f <= frac, '%d of %d pixels differ by more than %d (%.3f%%)' % (bad, len(da), tol, f * 100), f


def cmd_compare(a):
    try:
        ok, msg, _ = compare(a.a, a.b, a.tol, a.frac)
    except (OSError, ValueError) as e:
        print('FAIL: %s' % e, file=sys.stderr)
        return 1
    print('%s %s' % ('PASS' if ok else 'FAIL', msg))
    return 0 if ok else 1


def cmd_text(a):
    m = tool('mutool')
    if not m:
        return skip('mutool')
    cmd = [m, 'draw', '-F', 'txt', '-o', '-', a.file] + ([str(a.page)] if a.page else [])
    rc, out, err = run(cmd)
    sys.stdout.write(out.decode('utf-8', 'replace'))
    return 0 if rc == 0 else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest='cmd', required=True)
    s = sp.add_parser('validate')
    s.add_argument('files', nargs='+')
    s.add_argument('--warn-ok', action='store_true')
    s.set_defaults(fn=cmd_validate)
    s = sp.add_parser('render')
    s.add_argument('file')
    s.add_argument('page', type=int)
    s.add_argument('out')
    s.add_argument('--dpi', type=int, default=72)
    s.add_argument('--gray', action='store_true')
    s.set_defaults(fn=cmd_render)
    s = sp.add_parser('annots')
    s.add_argument('file')
    s.add_argument('page', type=int)
    s.set_defaults(fn=cmd_annots)
    s = sp.add_parser('compare')
    s.add_argument('a')
    s.add_argument('b')
    s.add_argument('--tol', type=int, default=16)
    s.add_argument('--frac', type=float, default=0.01)
    s.set_defaults(fn=cmd_compare)
    s = sp.add_parser('text')
    s.add_argument('file')
    s.add_argument('page', type=int, nargs='?')
    s.set_defaults(fn=cmd_text)
    a = ap.parse_args(argv)
    return a.fn(a)


if __name__ == '__main__':
    sys.exit(main())
