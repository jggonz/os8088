#!/usr/bin/env python3
"""Fetch and verify the optional NES oracle into build/, including its MIT licence."""
import argparse
import hashlib
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
COMMIT = '0e4220b084c467e39c04805d955e78c463feadd0'
URL = 'https://raw.githubusercontent.com/kgabis/agnes/' + COMMIT + '/'
PARTS = {
    'agnes.c': (84826, '2a8ff8770cc4fd1dacaa17b4841e7344fc1e458aba66351ee46e8423e7af618f'),
    'agnes.h': (2408, 'a595b12240134ab46861562303a62f19e0e2796ae2ff0b71b5169d98425d45a1'),
    'LICENSE': (1072, '6f27bbb94b336f7940bfe1373adc71c0629960553a2abac082bc8caf95a950df'),
}
DEFAULT = ROOT / 'build' / 'exbnes' / 'agnes'


def valid(name, data):
    size, digest = PARTS[name]
    if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
        raise ValueError('agnes pin mismatch: ' + name)


def acquire(destination=DEFAULT, source=None, check=False):
    destination = Path(destination)
    pending = {}
    for name in PARTS:
        target = destination / name
        if target.exists():
            valid(name, target.read_bytes())
        elif check:
            raise ValueError('missing cached agnes file: ' + str(target))
        else:
            if source:
                data = (Path(source) / name).read_bytes()
            else:
                req = urllib.request.Request(URL + name, headers={'User-Agent': 'os8088-exbnes/1'})
                with urllib.request.urlopen(req, timeout=60) as response:
                    data = response.read()
            valid(name, data)
            pending[name] = data
    if pending:
        destination.mkdir(parents=True, exist_ok=True)
        for name, data in pending.items():
            (destination / name).write_bytes(data)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('-o', '--out', type=Path, default=DEFAULT)
    parser.add_argument('--from', dest='source', type=Path, help='verified local checkout instead of network')
    parser.add_argument('--check', action='store_true', help='check cache; no download or writes')
    args = parser.parse_args()
    try:
        print(acquire(args.out, args.source, args.check))
    except (OSError, ValueError) as error:
        parser.exit(1, 'getagnes: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
