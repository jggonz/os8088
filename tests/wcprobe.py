#!/usr/bin/env python3
"""WireChat's guest probe (SPEC.md 113.12): the running package's own
variables, read by NAME out of a re-assembly of the source - mrprobe.py's
shape, for tests/wcdraw.py.

    import wcprobe
    p = wcprobe.attach(ui)            # after ui.path('B:/WIRECHAT.O88')
    p.b('wc_link'), p.w('wc_top'), p.put('wc_wake', b'\\x01')

MartyPC has no network card, so a row that wants a WireChat with something
in it INJECTS a reply: `inject()` writes a body SPEC.md 113.2.3 would send
into `wc_resp`, makes it the answer to a poll for the current view, raises
the worker's one byte, and then something must wake the UI task - a menu
pick that saves the settings does (`wc_wantsave`).
"""
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'tests'))
import mrprobe                                                   # noqa: E402

SRC = os.path.join(ROOT, 'apps', 'wirechat', 'wirechat.asm')
# build/ for wcface.bin, the face wctxt.inc incbins (SPEC.md 113.5.3) - the
# Makefile's -I $(BUILD)/, and `make` has written it before any row runs
INCS = ('apps/', 'apps/wirechat/', 'drivers/net/', 'build/')

RQ_POLL = 2
WW_DONE = 1


def syms():
    with tempfile.TemporaryDirectory() as d:
        cp = os.path.join(d, 'p.asm')
        mp = os.path.join(d, 'p.map')
        open(cp, 'w').write(open(SRC).read() + '\n[map symbols %s]\n' % mp)
        subprocess.run(['nasm', '-f', 'bin', '-w+error']
                       + sum([['-I', os.path.join(ROOT, i)] for i in INCS], [])
                       + ['-o', os.path.join(d, 'p.bin'), cp], check=True,
                       cwd=ROOT)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in '0123456789ABCDEF' for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


def attach(ui):
    return mrprobe.Probe(ui.m, mrprobe.seg_of(ui.m, 'WireChat'), syms())


def inject(p, body, hist=False):
    """Make BODY (str, LF-separated) the reply to a poll for the view - a
    history (h=1, which REPLACES the view's messages) when `hist`."""
    raw = body.encode('ascii') + b'\0'
    p.put('wc_resp', raw)
    p.put('wc_rqvgen', bytes([p.b('wc_vgen')]))
    p.put('wc_rqh', b'\x01' if hist else b'\x00')
    p.put('wc_rq', bytes([RQ_POLL]))
    p.put('wc_wake', bytes([WW_DONE]))
