#!/usr/bin/env python3
"""Byteopoly's host harness (SPEC.md 131.12): every apps/byteopoly/hosttest/
t_*.c compiled with the host compiler under ASan/UBSan and -DBP_HOST, then run.

The same programs `make byteopolyhost` builds, compiled here into a private
temporary directory so the row never writes build/ (tests/suite.py `builds`).
Each test includes byteoply.c, so it exercises the SHIPPED C sources: the
rules engine (t_rules: 21 scripted complete games), the codec and files
(t_save), the banker protocol (t_link: complete cable games under faults on
the real engine), the panels (t_panels) and the tables (t_smoke). A test
passes only when it exits 0 and prints its own `<name>: ... ok` line.
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / 'apps' / 'byteopoly' / 'hosttest'
FLAGS = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
         '-DBP_HOST', '-I', str(ROOT / 'apps' / 'byteopoly'), '-I', str(HOST)]


def main():
    cc = os.environ.get('CC') or shutil.which('cc')
    if not cc:
        print('byteopoly_host: no host C compiler')
        return 1
    tests = sorted(HOST.glob('t_*.c'))
    if not tests:
        print('byteopoly_host: no tests found')
        return 1
    bad = []
    with tempfile.TemporaryDirectory(prefix='bphost-') as td:
        for src in tests:
            exe = Path(td) / src.stem
            r = subprocess.run([cc, *FLAGS, str(src), '-o', str(exe)],
                               cwd=ROOT, capture_output=True, text=True)
            if r.returncode:
                print(f'FAIL {src.stem}: compile\n{r.stderr[-2000:]}')
                bad.append(src.stem)
                continue
            r = subprocess.run([str(exe)], cwd=ROOT, capture_output=True,
                               text=True, timeout=300)
            out = r.stdout + r.stderr
            last = [ln for ln in out.splitlines() if ln.startswith(src.stem + ':')]
            ok = r.returncode == 0 and last and last[-1].rstrip().endswith(
                ('ok', 'ok (0 failures)'))
            print(f'{"ok  " if ok else "FAIL"} {src.stem}: '
                  f'{last[-1] if last else "(no result line)"}')
            if not ok:
                print(out[-3000:])
                bad.append(src.stem)
    print(f'byteopoly_host: {len(tests) - len(bad)} of {len(tests)} passed')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
