#!/usr/bin/env python3
"""pdw_cmppages.py A.PDF B.PDF [pw]: exit 0 when every page of A has the same decoded content stream and annotation strings as B's
(the Insert-from-an-encrypted-file check of the real-engine harness: F12, PAGEDESK-SPEC 20.22.4.2)."""
import base64, json, subprocess, sys

def pages(pdf, pw=''):
    j = json.loads(subprocess.run(['qpdf', '--json=2', '--json-stream-data=inline', '--decode-level=generalized', '--password=' + pw, pdf],
                                  capture_output=True).stdout)
    objs = j['qpdf'][1]; out = []
    for p in j['pages']:
        pd = objs['obj:' + p['object']]['value']
        c = pd.get('/Contents'); cs = c if isinstance(c, list) else ([c] if c else [])
        data = b''.join(base64.b64decode(objs['obj:' + x]['stream'].get('data') or '') for x in cs)
        an = []
        for a in pd.get('/Annots', []) or []:
            av = objs['obj:' + a]['value'] if isinstance(a, str) else a
            an.append({k: v for k, v in av.items() if k in ('/Subtype', '/Contents', '/T', '/Subj')})
        out.append((hash(data) if data else None, len(data), json.dumps(an, sort_keys=True)))
    return out

a = pages(sys.argv[1], sys.argv[3] if len(sys.argv) > 3 else ''); b = pages(sys.argv[2])
bad = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
print('pages %d vs %d; differing: %s; pages without /Contents in A: %s' % (len(a), len(b), bad, [i for i, x in enumerate(a) if x[0] is None]))
sys.exit(0 if len(a) == len(b) and not bad else 1)
