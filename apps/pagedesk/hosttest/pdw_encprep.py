#!/usr/bin/env python3
"""pdw_encprep.py - the host stand-in for PDENG's PDRC_PLAIN reads (PAGEDESK-SPEC 20.22.4.6).

    pdw_encprep.py FILE.PDF DBDIR [PASSWORD]

Until PDENG answers xref streams, object streams and encryption, PDWRT's host
harness (pdw_enc.c) reads its source through THIS: qpdf (the independent
reader) decodes the file, and every object is written to DBDIR/o<N>.txt as the
text PDRC_PLAIN is specified to return (20.22.4.2): "N G obj", the dictionary
or value with strings decrypted, a stream's data decrypted but not defiltered
behind a DIRECT /Length, "endobj". DBDIR/trailer.txt, pages.txt and enc.txt
carry what PDV_ENG_TRAILER, PAGEINFO and CRYPTINFO answer.
"""
import json, os, re, subprocess, sys

def run(*a, **k):
    return subprocess.run(a, capture_output=True, **k)

def name_out(n):
    out = '/'
    for ch in n[1:]:
        c = ord(ch)
        if c <= 32 or c >= 127 or ch in '#/%()<>[]{}':
            out += '#%02X' % c
        else:
            out += ch
    return out

def str_out(s):
    if s.startswith('b:'):
        return '<' + s[2:] + '>'
    t = s[2:]
    if all(32 <= ord(c) < 127 for c in t):
        return '(' + t.replace('\\', '\\\\').replace('(', '\\(').replace(')', '\\)') + ')'
    return '<FEFF' + t.encode('utf-16-be').hex().upper() + '>'

def val_out(v):
    if v is None: return 'null'
    if v is True: return 'true'
    if v is False: return 'false'
    if isinstance(v, int): return str(v)
    if isinstance(v, float):
        s = ('%.6f' % v).rstrip('0').rstrip('.')
        return s if s else '0'
    if isinstance(v, str):
        if re.fullmatch(r'\d+ \d+ R', v): return v
        if v.startswith('/'): return name_out(v)
        if v[:2] in ('u:', 'b:'): return str_out(v)
        raise ValueError('string ' + v)
    if isinstance(v, list): return '[' + ' '.join(val_out(x) for x in v) + ']'
    if isinstance(v, dict):
        return '<< ' + ' '.join(name_out(k) + ' ' + val_out(x) for k, x in v.items()) + ' >>'
    raise ValueError(repr(v))

def main():
    src, db = sys.argv[1], sys.argv[2]
    pw = sys.argv[3] if len(sys.argv) > 3 else ''
    os.makedirs(db, exist_ok=True)
    for f in os.listdir(db): os.remove(os.path.join(db, f))
    js = os.path.join(db, 'q.json')
    r = run('qpdf', '--json=2', '--json-stream-data=file', '--json-stream-prefix=' + os.path.join(db, 's'),
            '--decode-level=none', '--password=' + pw, src, js)
    if r.returncode not in (0, 3):
        sys.stderr.write(r.stderr.decode()); sys.exit(2)
    d = json.load(open(js))
    objs = d['qpdf'][1]
    data = open(src, 'rb').read()
    tr = objs['trailer']['value']
    enc = d['encrypt']
    # objects
    plain = {}; cipher = []
    encnum = 0
    if isinstance(tr.get('/Encrypt'), str):
        m3 = re.fullmatch(r'(\d+) (\d+) R', tr['/Encrypt']); encnum = int(m3.group(1)) if m3 else 0
    for k, v in objs.items():
        m = re.fullmatch(r'obj:(\d+) (\d+) R', k)
        if not m: continue
        n, g = int(m.group(1)), int(m.group(2))
        if 'stream' in v:
            dic = dict(v['stream']['dict'])
            body = open(v['stream']['datafile'], 'rb').read()
            dic['/Length'] = len(body)
            t = ('%d %d obj\n%s\nstream\n' % (n, g, val_out(dic))).encode() + body + b'\nendstream\nendobj\n'
        else:
            t = ('%d %d obj\n%s\nendobj\n' % (n, g, val_out(v['value']))).encode()
        # the strings-as-ciphertext contract (PAGEDESK-SPEC 20.22.4.2): an object that sits in the file ITSELF
        # (not in an object stream) is served with the file's own dictionary text - strings still enciphered -
        # and the stream decrypted with /Length fixed; the answer carries the flag
        if enc.get('encrypted') and os.environ.get('PDW_STRCIPHER', '1') == '1' and n != encnum:
            m2 = re.search(rb'(?<![0-9])%d %d obj\s*(.*?)\s*(stream\r?\n|endobj)' % (n, g), data, re.S)
            if m2 and m2.group(1).startswith(b'<<'):
                dtxt = m2.group(1)
                if 'stream' in v:
                    body = open(v['stream']['datafile'], 'rb').read()
                    dtxt, k2 = re.subn(rb'/Length\s+\d+(\s+\d+\s+R)?', b'/Length %d' % len(body), dtxt, count=1)
                    t = b'%d %d obj\n' % (n, g) + dtxt + b'\nstream\n' + body + b'\nendstream\nendobj\n'
                else:
                    t = b'%d %d obj\n' % (n, g) + dtxt + b'\nendobj\n'
                cipher.append(n)
        plain[n] = (g, v)
        open(os.path.join(db, 'o%d.txt' % n), 'wb').write(t)
    open(os.path.join(db, 'cipher.txt'), 'w').write(' '.join(map(str, cipher)) + '\n')
    # the trailer
    sx = int(re.findall(rb'startxref\s+(\d+)', data)[-1])
    isstm = 1 if re.match(rb'\d+ \d+ obj', data[sx:sx + 24]) else 0
    ver = 10 + int(data[7:8]) if data.startswith(b'%PDF-1.') else 13
    ids = tr.get('/ID', [])
    id0 = ids[0][2:] if ids and isinstance(ids[0], str) else ''
    en = 0; eg = 0; direct = 0
    if '/Encrypt' in tr:
        e = tr['/Encrypt']
        if isinstance(e, str):
            m = re.fullmatch(r'(\d+) (\d+) R', e); en, eg = int(m.group(1)), int(m.group(2))
            if os.environ.get('PDW_DIRECT'):    # make it a direct dictionary: the writer must copy its text (20.22.4.1)
                txt = plain[en][1]['value'] if False else None
                direct = 1
                m2 = re.search(rb'obj\s*(<<.*>>)\s*endobj', open(os.path.join(db, 'o%d.txt' % en), 'rb').read(), re.S)
                open(os.path.join(db, 'o65534.txt'), 'wb').write(m2.group(1))
                en = eg = 0
        else:
            direct = 1
            open(os.path.join(db, 'o65534.txt'), 'wb').write(val_out(e).encode() if isinstance(val_out(e), str) else val_out(e))
    def ref(x):
        m = re.fullmatch(r'(\d+) (\d+) R', x or ''); return (int(m.group(1)), int(m.group(2))) if m else (0, 0)
    rn, rg = ref(tr['/Root']); inn, ig = ref(tr.get('/Info'))
    with open(os.path.join(db, 'trailer.txt'), 'w') as f:
        f.write('size %d\nroot %d %d\ninfo %d %d\nstartxref %d\nversion %d\nid0 %s\nencrypt %d %d\ntflags %d\nxrefstm %d\n'
                % (tr['/Size'], rn, rg, inn, ig, sx, ver, id0, en, eg, 2 if direct else 0, isstm))
    # the pages, with the inherited boxes
    def dictof(ref_):
        n = int(ref_.split()[0]); v = plain[n][1]
        return v.get('value', v.get('stream', {}).get('dict', {}))
    with open(os.path.join(db, 'pages.txt'), 'w') as f:
        for p in d['pages']:
            o = p['object']; dd = dictof(o); cur = dd; box = rot = crop = None
            while True:
                if box is None and '/MediaBox' in cur: box = cur['/MediaBox']
                if crop is None and '/CropBox' in cur: crop = cur['/CropBox']
                if rot is None and '/Rotate' in cur: rot = cur['/Rotate']
                if '/Parent' not in cur: break
                cur = dictof(cur['/Parent'])
            def rsv(x):
                while isinstance(x, str) and re.fullmatch(r'\d+ \d+ R', x): x = plain[int(x.split()[0])][1]['value']
                return [rsv(y) for y in x] if isinstance(x, list) else x
            box = rsv(box) if box else [0, 0, 612, 792]; crop = rsv(crop) if crop else box
            rot = rsv(rot)
            n, g = [int(x) for x in o.split()[:2]]
            f.write('%d %d %s %s %d\n' % (n, g, ' '.join(str(int(x)) for x in box), ' '.join(str(int(x)) for x in crop), rot or 0))
    # encryption
    with open(os.path.join(db, 'enc.txt'), 'w') as f:
        if enc.get('encrypted'):
            r = run('qpdf', '--show-encryption', '--show-encryption-key', '--password=' + pw, src)
            key = re.search(rb'Encryption key = ([0-9a-f]+)', r.stdout).group(1).decode()
            pr = enc['parameters']
            mm = {'AESv2': 2, 'AESv3': 3, 'RC4': 1, 'none': 4}
            f.write('encrypted 1\nkey %s\nstm %d\nstr %d\nv %d\nr %d\n' % (key, mm[pr['streammethod']], mm[pr['stringmethod']], pr['V'], pr['R']))
        else:
            f.write('encrypted 0\n')

main()
