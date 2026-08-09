"""os88prof - a sampling profiler for the guest, costing the guest nothing.

    nasm -f bin -w+error -DTRKLOG -I apps/ -I apps/tracker/ -I tests/ \\
         -o /dev/null -l /tmp/tl.lst apps/tracker/tracker.asm
    python3 tools/os88prof.py /tmp/tl.lst 25


MartyPC's status gives flat_ip, so sampling it and bucketing by the nearest
preceding label out of the NASM listing IS a profile - no counters, no
instrumentation, no guest cycles spent.  The sample rate is whatever the debug
link manages; what matters is that it is uncorrelated with anything the guest
does, which a host-driven poll is.
"""
import sys, re, time
sys.path.insert(0, '/home/user/os8088/tools')
from os88marty import Marty

LST = sys.argv[1] if len(sys.argv) > 1 else 'build/tracker.lst'
SECS = float(sys.argv[2]) if len(sys.argv) > 2 else 20


def symbols():
    """(offset, name) for every label in the package image, sorted.

    A label line in a NASM listing carries NO address - the address turns up
    on the next line that emits a byte - so a pending name is held until one
    does.  Getting that wrong buckets everything into the nearest DATA label,
    which is how the first run of this put 41% of the guest in `mp_ntab`.
    """
    out, pending, last = [], [], None
    for L in open(LST):
        m = re.match(r'\s*\d+\s+([0-9A-F]{8})\s', L)
        addr = int(m.group(1), 16) if m else None
        lab = re.search(r'<1>\s*(\.?[A-Za-z_][A-Za-z0-9_.]*):', L)
        if lab:
            n = lab.group(1)
            if n.startswith('.'):
                n = (last or '?') + n
            else:
                last = n
            if addr is not None:
                out.append((addr, n))
            else:
                pending.append(n)
            continue
        if addr is not None and pending:
            for n in pending:
                out.append((addr, n))
            pending = []
    out.sort()
    return out


def run():
    m = Marty('127.0.0.1:9001')
    buf = m.read(0x40000, 0xA0000 - 0x40000)
    seg = None
    for o in range(0, len(buf) - 32, 16):
        if buf[o:o+2] == b'O8' and buf[o+2] == 3 and buf[o+16:o+23] == b'TRACKER':
            seg = (0x40000 + o) >> 4
    base = seg << 4
    syms = symbols()
    print('package at %04X, %d labels' % (seg, len(syms)))
    hits, tot, out = {}, 0, 0
    t0 = time.time()
    while time.time() - t0 < SECS:
        ip = m.cmd(cmd='status')['flat_ip']
        tot += 1
        if not (base <= ip < base + 0x10000):
            out += 1
            continue
        off = ip - base
        lo, hi = 0, len(syms)
        while lo < hi - 1:
            mid = (lo + hi) // 2
            if syms[mid][0] <= off: lo = mid
            else: hi = mid
        hits[syms[lo][1]] = hits.get(syms[lo][1], 0) + 1
    print('%d samples, %d (%.0f%%) outside the package (kernel/BIOS/idle)'
          % (tot, out, 100.0 * out / tot))
    for name, n in sorted(hits.items(), key=lambda x: -x[1])[:18]:
        print('   %5.1f%% of ALL   %5.1f%% of pkg   %s'
              % (100.0*n/tot, 100.0*n/(tot-out), name))


run()
