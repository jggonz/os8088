#!/usr/bin/env python3
"""pdw_encrun.py - PDWRT on xref-stream, object-stream and encrypted sources (20.22.4.6).

    pdw_encrun.py OUTDIR PDW_ENC_BINARY [SAMPLE.PDF]

Builds the fixtures with qpdf (provenance: tests/pdfany/wrt/README.TXT), runs
pdw_enc (the writer over its PDENG stand-in) on each, and holds every output
against qpdf and MuPDF:
  * qpdf --check is clean;
  * qpdf (decrypting with the password) reads back the SAME annotations the
    plain control run wrote - subtype, /Contents, /T, /Subj, /DA, /Rect, /C,
    and the bytes of the appearance stream - on an encrypted source as on a
    plain xref-stream one;
  * mutool draws both saves with no encryption complaint on stderr;
  * the source's bytes are still the file's prefix, the update is a classic
    section whose trailer has /Prev, /Encrypt (when the source had it) and
    the source's whole /ID[0].
"""
import base64, json, os, re, shutil, subprocess, sys

out, binary = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
sample = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else os.path.abspath('apps/pagedesk/samples/MARKUP.PDF')
os.makedirs(out, exist_ok=True)
fails = 0

def run(*a, **k):
    return subprocess.run(a, capture_output=True, **k)

def check(c, msg):
    global fails
    if not c: fails += 1
    print(('ok   ' if c else 'FAIL ') + msg)

# name: (qpdf encryption options, password for the reader, object streams)
FIX = {
    'plain':   ([], '', 'generate'),
    'aes128':  (['--encrypt', '', 'owner', '128', '--use-aes=y', '--'], '', 'generate'),
    'aes128u': (['--encrypt', 'u1', 'owner', '128', '--use-aes=y', '--'], 'u1', 'generate'),
    'aes128c': (['--encrypt', '', 'owner', '128', '--use-aes=y', '--'], '', 'disable'),
    'aes256':  (['--encrypt', '', 'owner', '256', '--'], '', 'generate'),
    'rc4128':  (['--allow-weak-crypto', '--encrypt', '', 'owner', '128', '--use-aes=n', '--'], '', 'generate'),
    'rc440':   (['--allow-weak-crypto', '--encrypt', '', 'owner', '40', '--'], '', 'disable'),
}

def annots(pdf, pw):
    r = run('qpdf', '--json=2', '--json-stream-data=inline', '--decode-level=generalized', '--password=' + pw, pdf)
    d = json.loads(r.stdout)['qpdf'][1]
    res = []
    for k, v in d.items():
        v = v.get('value') if isinstance(v, dict) else None
        if isinstance(v, dict) and v.get('/Type') == '/Annot':
            row = {a: b for a, b in v.items() if a in ('/Subtype', '/Contents', '/T', '/Subj', '/DA', '/Rect', '/C', '/F', '/QuadPoints', '/InkList', '/IRT', '/State', '/CreationDate', '/BS', '/L', '/LE', '/Q')}
            ap = v.get('/AP', {}).get('/N')
            if isinstance(ap, str):
                o = d.get('obj:' + ap.replace(' R', ' R'), {}).get('stream', {})
                row['AP'] = (o.get('data') or '', o.get('dict', {}).get('/BBox'))
            if '/IRT' in row: row['/IRT'] = 'ref'           # object numbers differ between sources
            row['NM'] = bool(re.fullmatch(r'u:PD-[0-9A-F]{8}-[0-9A-F]{4}', v.get('/NM', '')))
            res.append(row)
    res.sort(key=lambda r: json.dumps(r, sort_keys=True))
    return res

def pages(pdf, pw):
    """per page: the decoded bytes of its /Contents (base64'd by qpdf) and how many annotations it has"""
    r = run('qpdf', '--json=2', '--json-stream-data=inline', '--decode-level=generalized', '--password=' + pw, pdf)
    j = json.loads(r.stdout); d = j['qpdf'][1]; res = []
    for p in j['pages']:
        pg = d['obj:' + p['object']]['value']
        cs = pg.get('/Contents'); cs = cs if isinstance(cs, list) else [cs]
        data = b''.join(base64.b64decode(d['obj:' + c]['stream'].get('data') or '') for c in cs if c)
        an = pg.get('/Annots', [])
        if isinstance(an, str): an = d['obj:' + an]['value']
        res.append((data, len(an), pg.get('/Rotate', 0)))
    return res

def trailer_facts(pdf):
    b = open(pdf, 'rb').read()
    t = b[b.rindex(b'trailer'):]
    return t.decode('latin1')

FOREIGN = {'plain': ([], ''), 'encaes': (['--encrypt', 'fp', 'owner', '128', '--use-aes=y', '--'], 'fp')}
PAGES8 = os.path.join(os.path.dirname(sample), 'PAGES.PDF')
HERE = os.path.dirname(os.path.abspath(__file__))
ctrl = {}

def qpdf_build(enc, osm, src, dst):
    cmd = ['qpdf'] + (['--allow-weak-crypto'] if '--allow-weak-crypto' in enc else []) + ['--object-streams=' + osm, '--stream-data=uncompress'] \
        + [x for x in enc if x != '--allow-weak-crypto'] + [src, dst]
    return run(*cmd)

for name, (enc, pw, osm) in FIX.items():
    base = os.path.join(out, name); shutil.rmtree(base, ignore_errors=True); os.makedirs(base)
    r = qpdf_build(enc, osm, sample, os.path.join(base, 'SRC.PDF'))
    if r.returncode not in (0, 3):
        print('FAIL building', name, r.stderr.decode()); fails += 1; continue
    fv = sorted(FOREIGN, key=lambda k: k != 'plain') if name in ('plain', 'aes128', 'aes256') else ['plain']
    for fk in fv:
        d = os.path.join(base, fk); os.makedirs(d)
        shutil.copy(os.path.join(base, 'SRC.PDF'), d)
        fenc, fpw = FOREIGN[fk]
        r = qpdf_build(fenc, 'generate', PAGES8, os.path.join(d, 'FOREIGN.PDF'))
        tag = '%s/%s' % (name, fk)
        check(r.returncode in (0, 3), '%s: the foreign file is built (%s)' % (tag, 'AES-128, user password fp' if fenc else 'plain, xref stream'))
        p = subprocess.run([binary, 'SRC.PDF', pw, 'plain' if not fenc else 'enc-aes', fpw], cwd=d, capture_output=True, text=True,
                           env=dict(os.environ, PDW_PREP=os.path.join(HERE, 'pdw_encprep.py'), PDW_SABOTAGE='1'))
        open(os.path.join(d, 'run.txt'), 'w').write(p.stdout + p.stderr)
        check(p.returncode == 0 and 'PASS' in p.stdout, '%s: pdw_enc (the writer over the stand-in): %s' % (tag, p.stdout.strip().splitlines()[-1] if p.stdout.strip() else p.stderr[-200:]))
        if p.returncode != 0:
            print(p.stdout[-1500:]); continue
        for s_ in ('S1.PDF', 'S2.PDF', 'I1.PDF'):
            f = os.path.join(d, s_)
            r = run('qpdf', '--check', '--password=' + pw, f)
            check(r.returncode == 0 and b'No syntax or stream encoding errors' in r.stdout and b'WARNING' not in r.stderr + r.stdout,
                  '%s %s: qpdf --check clean%s' % (tag, s_, '' if r.returncode == 0 else ' ' + (r.stdout + r.stderr).decode()[-300:]))
            r = run('mutool', 'draw', '-p', pw, '-o', os.path.join(d, s_ + '.png'), '-r', '40', f, '1-2')
            bad = [l for l in r.stderr.decode().splitlines() if re.search(r'invalid string length|encryption|error:|warning:|cannot|decrypt|password', l, re.I)]
            check(r.returncode == 0 and not bad, '%s %s: mutool draw, no complaint%s' % (tag, s_, ' ' + '; '.join(bad[:3]) if bad else ''))
        # the source is the output's prefix; the update is classic with /Prev, /Encrypt and the whole ID[0]
        sb = open(os.path.join(d, 'SRC.PDF'), 'rb').read(); ob = open(os.path.join(d, 'S1.PDF'), 'rb').read()
        check(ob.startswith(sb), '%s: the source bytes are the output\'s prefix' % tag)
        t = trailer_facts(os.path.join(d, 'S1.PDF'))
        check('/Prev ' in t and 'startxref' in t, '%s: the update has /Prev (%s)' % (tag, re.search(r'/Prev \d+', t).group(0) if '/Prev' in t else '-'))
        if enc:
            check(re.search(r'/Encrypt \d+ 0 R', t) is not None, '%s: the trailer repeats /Encrypt' % tag)
            j = json.loads(run('qpdf', '--json=2', '--password=' + pw, '--json-stream-data=none', os.path.join(d, 'SRC.PDF')).stdout)['qpdf'][1]['trailer']['value']['/ID'][0][2:]
            check(j in t.replace(' ', '').replace('<', '').replace('>', '').lower(), '%s: /ID[0] (%d bytes) is written byte for byte' % (tag, len(j) // 2))
        # Extract: a PLAIN file even from an encrypted source
        r = run('qpdf', '--show-encryption', os.path.join(d, 'X1.PDF'))
        check(b'not encrypted' in r.stdout, '%s: Extract writes an unencrypted file' % tag)
        r = run('qpdf', '--check', os.path.join(d, 'X1.PDF'))
        check(r.returncode == 0, '%s X1.PDF: qpdf --check clean' % tag)
        px = pages(os.path.join(d, 'X1.PDF'), ''); pi = pages(os.path.join(d, 'I1.PDF'), pw)
        sa = {s_: annots(os.path.join(d, s_), pw) for s_ in ('S1.PDF', 'S2.PDF')}
        if name == 'plain' and fk == 'plain':
            ctrl.update(px=px, pi=pi, sa=sa)
            print('     control: S1 has %d annotations, S2 %d; Extract %d pages; Insert %d pages' % (len(sa['S1.PDF']), len(sa['S2.PDF']), len(px), len(pi)))
            check(len(sa['S1.PDF']) >= 9 and len(px) == 2 and len(pi) == 4, 'plain: the control has the nine marks, 2 extracted pages and 4 pages after Insert')
        else:
            check(px == ctrl['px'], '%s: the extracted pages\' decoded contents equal the plain control\'s' % tag)
            check(pi == ctrl['pi'], '%s: the 4 pages after Insert (contents, annotation counts, rotations) equal the plain control\'s' % tag)
            for s_ in ('S1.PDF', 'S2.PDF'):
                same = sa[s_] == ctrl['sa'][s_]
                check(same, '%s %s: decrypted annotations and appearance streams equal the plain control\'s (%d of them)' % (tag, s_, len(sa[s_])))
                if not same:
                    for x, y in zip(sa[s_], ctrl['sa'][s_]):
                        if x != y: print('       got ', json.dumps(x)[:300]); print('       want', json.dumps(y)[:300]); break
print('%s: %d failure(s)' % ('FAIL' if fails else 'PASS', fails))
sys.exit(1 if fails else 0)
