#!/usr/bin/env python3
"""os88pdfx_jpx - deterministic JPEG 2000 fixtures for PDXJ (PAGEDESK-SPEC 20.22.13).

    python3 tools/os88pdfx_jpx.py [--out DIR] [--ref]

Writes tests/pdfany/jpx/*.j2k|*.jp2 with OpenJPEG's opj_compress from synthetic
images made here (numpy, a fixed LCG: the same bytes every run), and a
MANIFEST.json describing each. --ref also decodes every fixture at every
reduction with opj_decompress into DIR/ref/ (PGM/PPM) for the host harness; the
reference is never committed (it is regenerated, and is a few MB).

Tools: opj_compress / opj_decompress from OpenJPEG (2.5.4 when the fixtures
were made; `brew install openjpeg`, `apt install libopenjp2-tools`).
"""
import json, os, subprocess, sys, shutil
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEF_OUT = os.path.join(HERE, '..', 'tests', 'pdfany', 'jpx')


def lcg(n, seed):
    out = np.empty(n, dtype=np.uint32)
    x = seed & 0xFFFFFFFF
    for i in range(n):
        x = (x * 1664525 + 1013904223) & 0xFFFFFFFF
        out[i] = x >> 16
    return out


def synth(w, h, ncomp, depth=8, seed=1):
    """A picture with smooth ramps, edges, a disc, fine texture and noise."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    mx = (1 << depth) - 1
    chans = []
    noise = (lcg(w * h, seed).reshape(h, w) % 16).astype(np.float64)
    for c in range(ncomp):
        base = (xx / w) * (0.5 + 0.15 * c) + (yy / h) * (0.3 + 0.1 * c)
        disc = ((xx - w * (0.35 + 0.1 * c)) ** 2 + (yy - h * 0.5) ** 2) < (min(w, h) * 0.28) ** 2
        v = base * mx * 0.8
        v = np.where(disc, mx - v * 0.6, v)
        v += 0.04 * mx * np.sin(xx * (0.9 + 0.2 * c)) * np.sin(yy * 0.7)
        v += noise * (mx / 255.0)
        v[(xx.astype(int) // 16 + yy.astype(int) // 16) % 7 == 0] *= 0.7
        chans.append(np.clip(v, 0, mx).astype(np.uint16))
    return chans


def write_pnm(path, chans, depth):
    h, w = chans[0].shape
    mx = (1 << depth) - 1
    with open(path, 'wb') as f:
        f.write(('P%d\n%d %d\n%d\n' % (5 if len(chans) == 1 else 6, w, h, mx)).encode())
        a = np.stack(chans, axis=-1) if len(chans) > 1 else chans[0]
        if depth > 8:
            f.write(a.astype('>u2').tobytes())
        else:
            f.write(a.astype(np.uint8).tobytes())


def read_pnm(path):
    d = open(path, 'rb').read()
    toks, i = [], 0
    while len(toks) < 4:
        while d[i:i + 1].isspace(): i += 1
        if d[i:i + 1] == b'#':                       # a comment (opj_decompress writes one)
            while d[i:i + 1] != b'\n': i += 1
            continue
        j = i
        while not d[j:j + 1].isspace(): j += 1
        toks.append(d[i:j]); i = j
    i += 1
    kind, w, h, mx = toks[0], int(toks[1]), int(toks[2]), int(toks[3])
    nc = 1 if kind == b'P5' else 3
    dt = '>u2' if mx > 255 else np.uint8
    a = np.frombuffer(d[i:], dtype=dt).reshape(h, w, nc) if nc > 1 else np.frombuffer(d[i:], dtype=dt).reshape(h, w)
    return a, mx


# name, (w, h, ncomp, depth), opj_compress args, ext
FIX = [
    ('g_ll',       (160, 120, 1, 8),  ['-n', '5'], 'j2k'),
    ('g_ly',       (160, 120, 1, 8),  ['-I', '-r', '12', '-n', '5'], 'j2k'),
    ('g_ly_jp2',   (160, 120, 1, 8),  ['-I', '-r', '8', '-n', '4'], 'jp2'),
    ('c_ll',       (160, 120, 3, 8),  ['-n', '5'], 'j2k'),
    ('c_ly',       (160, 120, 3, 8),  ['-I', '-r', '14', '-n', '5'], 'j2k'),
    ('c_ly_jp2',   (160, 120, 3, 8),  ['-I', '-r', '10', '-n', '4'], 'jp2'),
    ('c_nomct',    (96, 64, 3, 8),    ['-n', '3', '-mct', '0'], 'j2k'),
    ('c_nomct_ly', (96, 64, 3, 8),    ['-I', '-r', '10', '-n', '3', '-mct', '0'], 'j2k'),
    ('odd_ll',     (157, 113, 1, 8),  ['-n', '4'], 'j2k'),
    ('odd_ly',     (157, 113, 1, 8),  ['-I', '-r', '9', '-n', '4'], 'j2k'),
    ('tiny_ll',    (7, 5, 1, 8),      ['-n', '2'], 'j2k'),
    ('n1_ll',      (64, 48, 1, 8),    ['-n', '1'], 'j2k'),
    ('n2_ly',      (64, 48, 1, 8),    ['-I', '-r', '6', '-n', '2'], 'j2k'),
    ('n6_ly',      (200, 150, 1, 8),  ['-I', '-r', '16', '-n', '6'], 'j2k'),
    ('n0_ll',      (48, 32, 1, 8),    ['-n', '1', '-b', '16,16'], 'j2k'),
    ('cb16',       (160, 120, 1, 8),  ['-n', '4', '-b', '16,16'], 'j2k'),
    ('cb32x8',     (160, 120, 1, 8),  ['-n', '4', '-b', '32,8'], 'j2k'),
    ('cb64',       (160, 120, 1, 8),  ['-I', '-r', '10', '-n', '4', '-b', '64,64'], 'j2k'),
    ('t64_ll',     (160, 120, 1, 8),  ['-n', '5', '-t', '64,64'], 'j2k'),
    ('t64_ly',     (160, 120, 1, 8),  ['-I', '-r', '12', '-n', '5', '-t', '64,64'], 'j2k'),
    ('t64_c',      (160, 120, 3, 8),  ['-I', '-r', '12', '-n', '4', '-t', '64,64'], 'j2k'),
    ('t32_nomct',  (96, 64, 3, 8),    ['-n', '3', '-t', '32,32', '-mct', '0'], 'j2k'),
    ('t_bad',      (160, 120, 1, 8),  ['-n', '4', '-t', '100,100'], 'j2k'),
    ('p_lrcp',     (160, 120, 1, 8),  ['-I', '-r', '30,10,3', '-n', '4', '-p', 'LRCP'], 'j2k'),
    ('p_rlcp',     (160, 120, 1, 8),  ['-I', '-r', '30,10,3', '-n', '4', '-p', 'RLCP'], 'j2k'),
    ('p_rpcl',     (160, 120, 1, 8),  ['-I', '-r', '30,10,3', '-n', '4', '-p', 'RPCL'], 'j2k'),
    ('p_pcrl',     (160, 120, 1, 8),  ['-I', '-r', '30,10,3', '-n', '4', '-p', 'PCRL'], 'j2k'),
    ('p_cprl',     (160, 120, 3, 8),  ['-I', '-r', '30,10,3', '-n', '4', '-p', 'CPRL'], 'j2k'),
    ('p_rpcl_pr',  (160, 120, 1, 8),  ['-n', '3', '-p', 'RPCL', '-c', '[32,32],[32,32],[64,64],[64,64]'], 'j2k'),
    ('p_pcrl_pr',  (160, 120, 3, 8),  ['-n', '3', '-p', 'PCRL', '-c', '[32,32],[32,32],[64,64],[64,64]'], 'j2k'),
    ('p_cprl_pr',  (160, 120, 3, 8),  ['-I', '-r', '20,6', '-n', '3', '-p', 'CPRL', '-c', '[64,64],[64,64],[64,64],[64,64]'], 'j2k'),
    ('p_lrcp_pr',  (160, 120, 1, 8),  ['-I', '-r', '20,6', '-n', '3', '-p', 'LRCP', '-c', '[32,32],[32,32],[64,64],[64,64]', '-b', '16,16'], 'j2k'),
    ('sop_eph',    (160, 120, 1, 8),  ['-n', '4', '-SOP', '-EPH'], 'j2k'),
    ('sop_eph_ly', (160, 120, 1, 8),  ['-I', '-r', '20,5', '-n', '4', '-SOP', '-EPH'], 'j2k'),
    ('d12_ll',     (96, 64, 1, 12),   ['-n', '3'], 'j2k'),
    ('d12_ly',     (96, 64, 1, 12),   ['-I', '-r', '8', '-n', '3'], 'j2k'),
    ('style_reset',(96, 64, 1, 8),    ['-n', '3', '-M', '2'], 'j2k'),
    ('style_vsc',  (96, 64, 1, 8),    ['-n', '3', '-M', '8'], 'j2k'),
    ('style_segs', (96, 64, 1, 8),    ['-n', '3', '-M', '32'], 'j2k'),
    ('style_all3', (96, 64, 1, 8),    ['-I', '-r', '8', '-n', '3', '-M', '42'], 'j2k'),
    ('refuse_bypass', (96, 64, 1, 8), ['-n', '3', '-M', '1'], 'j2k'),
    ('refuse_termall', (96, 64, 1, 8), ['-n', '3', '-M', '4'], 'j2k'),
    ('refuse_roi',  (96, 64, 1, 8),   ['-n', '3', '-ROI', 'c=0,U=5'], 'j2k'),
]


def main():
    out = DEF_OUT
    ref = False
    a = sys.argv[1:]
    while a:
        x = a.pop(0)
        if x == '--out': out = a.pop(0)
        elif x == '--ref': ref = True
        else: sys.exit(__doc__)
    out = os.path.abspath(out)
    os.makedirs(out, exist_ok=True)
    tmp = os.path.join(out, '_tmp'); os.makedirs(tmp, exist_ok=True)
    man = []
    for name, (w, h, nc, depth), args, ext in FIX:
        pnm = os.path.join(tmp, name + ('.pgm' if nc == 1 else '.ppm'))
        write_pnm(pnm, synth(w, h, nc, depth, seed=hash_name(name)), depth)
        dst = os.path.join(out, name + '.' + ext)
        r = subprocess.run(['opj_compress', '-i', pnm, '-o', dst] + args, capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(dst):
            print('SKIP', name, r.stderr.strip()[:100]); continue
        e = {'name': name, 'file': name + '.' + ext, 'w': w, 'h': h, 'ncomp': nc, 'depth': depth, 'args': args}
        man.append(e)
        if ref:
            os.makedirs(os.path.join(out, 'ref'), exist_ok=True)
            for rr in range(0, 7):
                o = os.path.join(out, 'ref', '%s.r%d.%s' % (name, rr, 'pgm' if nc == 1 else 'ppm'))
                p = subprocess.run(['opj_decompress', '-i', dst, '-o', o, '-r', str(rr)], capture_output=True)
                if p.returncode != 0: break
    json.dump({'tool': 'opj_compress (OpenJPEG 2.5.4)', 'fixtures': man}, open(os.path.join(out, 'MANIFEST.json'), 'w'), indent=1)
    shutil.rmtree(tmp)
    print(len(man), 'fixtures in', out)


def hash_name(s):
    h = 2166136261
    for c in s.encode(): h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


if __name__ == '__main__':
    main()
