#!/usr/bin/env python3
# =============================================================================
# os8088 - tools/getarcrom.py
#
# Fetch an arcade ROM set ARCADE is built against, check every member, and
# write the board's ROM image in the layout the board maps it at
# (docs/ARCADE-SPEC.md section 1.3).
#
#     python3 tools/getarcrom.py invaders -o build/arcade-rom/INVADERS.ROM
#     python3 tools/getarcrom.py invaders --from <dir-or-zip>  (no network)
#     python3 tools/getarcrom.py invaders --check               (cache only)
#
# NOTHING THIS SCRIPT DOWNLOADS IS COMMITTED (CONTRIBUTING.md 6), exactly as
# tools/getapple2rom.py's Apple II ROMs are not. The zip is fetched from ONE
# pinned archive.org item, checked by SHA-1 as a file, and each member is
# checked again by SHA-256 on the way out: a ROM that is not the one the
# translation was written against is a machine that runs something else, so
# a mismatch is a hard failure and never a warning. The download is cached in
# build/arcade-rom/ (which `make clean` spares) and reused.
#
# --from takes a directory holding the members, or a MAME-style zip holding
# them at its root: an owner who has the set already never touches the
# network. The member hashes are checked either way.
# =============================================================================
import argparse
import hashlib
import io
import os
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CACHE = os.path.join(ROOT, 'build', 'arcade-rom')

# Each set: where it comes from, and its members in load order with the
# address each one maps at. The image is the members concatenated, so the
# order IS the address map.
SETS = {
    'invaders': {
        'url': 'https://archive.org/download/arcade_invaders/invaders.zip',
        'zipsha1': '8d2d41725398c33df15afa28d5a2e5350b7290c6',
        'members': [
            # name          at      SHA-256 (CRC32 734f5ad8 ... as MAME's)
            ('invaders.h', 0x0000,
             '1dfb19b1eeeac5b91ff4861ab331e183500785a3c958a6fa30aac0f37a7bbc65'),
            ('invaders.g', 0x0800,
             'ac7031b046274d4cd963cebf02f6e0ea50104e2a29681c387b1cb27e1e3e343d'),
            ('invaders.f', 0x1000,
             '5c8e573ebde9a673d4e7809d3f36aaa20d9062f3616d025d85c3f04fcfe11d72'),
            ('invaders.e', 0x1800,
             '0a97ed62e2063c13e373a9a61ea3d8fbb701e414af05953e16f823fd879d5f49'),
        ],
        'size': 0x2000,
    },
}


def die(msg):
    sys.stderr.write('getarcrom: %s\n' % msg)
    sys.exit(1)


def fetch_zip(name, s, offline):
    path = os.path.join(CACHE, name + '.zip')
    if os.path.exists(path):
        data = open(path, 'rb').read()
        if hashlib.sha1(data).hexdigest() == s['zipsha1']:
            return data
        if offline:
            die('%s: the cached zip is not the pinned one' % path)
    if offline:
        die('%s: not cached, and --check fetches nothing' % path)
    sys.stderr.write('getarcrom: fetching %s\n' % s['url'])
    req = urllib.request.Request(s['url'], headers={'User-Agent': 'os8088-getarcrom'})
    with urllib.request.urlopen(req, timeout=120) as r:
        data = r.read()
    got = hashlib.sha1(data).hexdigest()
    if got != s['zipsha1']:
        die('%s: SHA-1 %s, pinned %s' % (s['url'], got, s['zipsha1']))
    os.makedirs(CACHE, exist_ok=True)
    tmp = path + '.tmp'
    open(tmp, 'wb').write(data)
    os.replace(tmp, path)
    return data


def members_from(src, s):
    want = [m[0] for m in s['members']]
    if os.path.isdir(src):
        out = {}
        for n in want:
            p = os.path.join(src, n)
            if not os.path.exists(p):
                die('%s: missing %s' % (src, n))
            out[n] = open(p, 'rb').read()
        return out
    return members_in_zip(open(src, 'rb').read(), want, src)


def members_in_zip(data, want, what):
    z = zipfile.ZipFile(io.BytesIO(data))
    names = {i.filename: i for i in z.infolist()}
    out = {}
    for n in want:
        if n not in names:
            die('%s: no member %s at the root' % (what, n))
        out[n] = z.read(names[n])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('set', choices=sorted(SETS))
    ap.add_argument('-o', '--output')
    ap.add_argument('--from', dest='src')
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    s = SETS[a.set]
    if a.src:
        got = members_from(a.src, s)
    else:
        data = fetch_zip(a.set, s, a.check)
        got = members_in_zip(data, [m[0] for m in s['members']], a.set + '.zip')
    img = bytearray(s['size'])
    for n, at, sha in s['members']:
        d = got[n]
        h = hashlib.sha256(d).hexdigest()
        if h != sha:
            die('%s: SHA-256 %s, pinned %s' % (n, h, sha))
        img[at:at + len(d)] = d
    if a.check:
        print('getarcrom: %s is cached and checks' % a.set)
        return
    out = a.output or os.path.join(CACHE, a.set.upper() + '.ROM')
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    tmp = out + '.tmp'
    open(tmp, 'wb').write(img)
    os.replace(tmp, out)
    print('getarcrom: %s -> %s (%d bytes)' % (a.set, out, len(img)))


if __name__ == '__main__':
    main()
