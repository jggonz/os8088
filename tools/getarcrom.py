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
#     python3 tools/getarcrom.py pacman --from <dir-or-zip>     (the owner's;
#                                                                never fetched)
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
    # Taito's colour boards (MAME 8080bw.cpp): Space Invaders' board with
    # ROM at 4000h as well, and a colour map PROM. The image is 0000-4FFFh
    # as the CPU sees it (RAM's 2000-3FFFh zero) and the two colour maps
    # at 5000h and 5400h (ARCADE-SPEC 9).
    'invadpt2': {
        'url': 'https://archive.org/download/arcade_invadpt2/invadpt2.zip',
        'zipsha1': '228cadc2b54fcedc131a8a46cae6818bae255b1a',
        'members': [
            ('pv01', 0x0000,                                        # 7288a511
             'c12ed67ae4e664994dfef0f23d6bb9c041c611c66b704630ee413410d2601524'),
            ('pv02', 0x0800,                                        # 097dd8d5
             'f20ca15d8c2b447ad4601f4ce071177d43483e93ca83300c2363209d3107873e'),
            ('pv03', 0x1000,                                        # 1766337e
             '00eb1e9be39b98a86e8a5f2213881b516c3ce0d0e7310b11861e9d9ed4edd8b4'),
            ('pv04', 0x1800,                                        # 8f0e62e0
             '662f6cdb7cd16ae1402863b1b0c5c2683a2276c8b20781a9aa826291a31b2e94'),
            ('pv05', 0x4000,                                        # 19b505e9
             '584d276508e91316d42f11afb8de7a5321395205d03983f224215f703b7855d9'),
            ('pv06.1', 0x5000,                                      # a732810b
             '7d7c5481d716d3bbc98036a04c14e7de8be0b78e7ca1ab81f3687649116d6c22'),
            ('pv07.2', 0x5400,                                      # 2c5b91cb
             '3f8419a18354c41bb3ca3f7e419f743e90d3aca65c8fdd6fe4eab3165039c2ef'),
        ],
        'size': 0x5800,
    },
    'lrescue': {
        'url': 'https://archive.org/download/arcade_lrescue/lrescue.zip',
        'zipsha1': '3da8bff250a9665bb5f7ce2b7700dcaa579cbf84',
        'members': [
            ('lrescue.1', 0x0000,                                   # 2bbc4778
             '50218ec69cb1d2d0bdd3bb1096510c970643741646c641a1f5c72e92918320d6'),
            ('lrescue.2', 0x0800,                                   # 49e79706
             '0a6ee07b3aeaf4d336479b408c4af4b9e7ac7a0ffcf5b682f936ed56aaab739d'),
            ('lrescue.3', 0x1000,                                   # 1ac969be
             '8716fc612631a8f1af07fb4a7397d5f51a6482f95ebf581f6a54f22706d75f11'),
            ('lrescue.4', 0x1800,                                   # 782fee3c
             '214eba8596011d0910f2d6f3d797603293d5da91f47a87175ab896f2ba5c5246'),
            ('lrescue.5', 0x4000,                                   # 58fde8bc
             '272dc05049f3d420573aa51b8870831b47f7226a9d3d26fa0ef145782cf7e618'),
            ('lrescue.6', 0x4800,                                   # bfb0f65d
             '2547a97cda1f1f3a3b9a65bea52d1f4fbb6e8ed9d27c25aa28aa8ec22c1edd4f'),
            ('7643-1.cpu', 0x5000,                                  # 8b2e38de: ONE
             'e6f5d2e53c318e97e88d93df9054da488ad2f39d91196a7047dd6d8df089ce8d'),
            ('7643-1.cpu', 0x5400,                                  # map, twice
             'e6f5d2e53c318e97e88d93df9054da488ad2f39d91196a7047dd6d8df089ce8d'),
        ],
        'size': 0x5800,
    },
    # MAME's `pacman` (Midway). NOT fetched: no public archive.org item
    # carries it (arcade_pacman, arcade_puckman and arcade_mspacman are
    # dark), so the owner supplies it with --from and the hashes check it.
    # The image is the board's ROMs in the layout ARCADE-SPEC 7.1 pins.
    'pacman': {
        'url': None,
        'members': [
            ('pacman.6e', 0x0000,                                   # c1e6ab10
             'fe1c3234df345855d30728637f361f79472cabfe2a892a7567c63eaf31a4217b'),
            ('pacman.6f', 0x1000,                                   # 1a6fb2d4
             '09a723c9f84790e9019633e37761cfa4e9d7ab6db14f6fdb12738f51fec11065'),
            ('pacman.6h', 0x2000,                                   # bcdd1beb
             '69347409739b64ed9d9b19713de0bc66627bd137687de649796b9d2ef88ed8e6'),
            ('pacman.6j', 0x3000,                                   # 817d94e3
             '03ee523c210e87fb8dd1d925b092ad269fdd753b5b7a20b3757b0ceee5f18679'),
            ('pacman.5e', 0x4000,                                   # tiles 0c944964
             '8d9a86c97fe94b1fd010b139672c330e3b257ba59b0d8df7a821592e30a77b4b'),
            ('pacman.5f', 0x5000,                                   # sprites 958fedf9
             '49c8f656cb8ea1ae02fb64a2c09df98e7f06a034b43c6c8240032df417c6d36f'),
            ('82s123.7f', 0x6000,                                   # colours 2fc650bd
             '48fe0b01d68e3d702019ca715f7266c8e3261c769509b281720f53ca0a1cc8fb'),
            ('82s126.4a', 0x6020,                                   # palettes 3eb3a8e4
             'ef8f7a3b0c10f787d9cc1cbc5cc266fcc1afadb24c3b4d610fe252b9c3df1d76'),
            ('82s126.1m', 0x6120,                                   # waveforms a9cc86bf
             '8e723ad91e46ef1a186b2ed3c99a8bf1c571786bc7ceae2b367cbfc80857a394'),
        ],
        'size': 0x6220,
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
    elif s['url'] is None:
        die('%s is not fetched: give the set you have with --from <dir-or-zip>'
            % a.set)
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
