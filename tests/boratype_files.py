#!/usr/bin/env python3
"""Production Borat APPDATA transport under filesystem failures (SPEC.md 134).

Run after boratype is built. This host row neither rebuilds nor boots the OS.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    cc = os.environ.get('CC') or shutil.which('cc')
    if not cc:
        raise SystemExit('boratype_files: host C compiler missing')
    with tempfile.TemporaryDirectory(prefix='bt-files-') as td:
        exe = Path(td) / 't_files'
        subprocess.run([cc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-O1',
                        '-g', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all',
                        str(ROOT/'tests/boratypefiles/t_files.c'), '-o', str(exe)],
                       cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
