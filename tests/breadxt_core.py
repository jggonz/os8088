#!/usr/bin/env python3
"""Shipping Breadboard XT core vs independent host fixtures/oracle.

No guest claim: native-double adapters exercise portable C only. The BBNUM
8088 package is the separate arithmetic/ABI instrument. This row builds its
host harness in a temporary directory, never changes the target build tree.
"""
import argparse
import json
import math
from pathlib import Path
import random
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent


def reference(n, branches):
    """Independent double Cholesky solve of an SPD conductance network.

    Nodes 0/1 are prescribed 5/0 V; all others are unknown. Source graph is
    logical, independently assigned to actual board holes by the C harness.
    """
    matrix = [[0.0] * n for _ in range(n)]
    rhs = [0.0] * n
    for a, b, resistance in branches:
        g = 1.0 / resistance
        for u, v in ((a, b), (b, a)):
            if u >= 2:
                matrix[u-2][u-2] += g
                if v >= 2:
                    matrix[u-2][v-2] -= g
                elif v == 0:
                    rhs[u-2] += 5.0*g
    lower = [[0.0]*n for _ in range(n)]
    for i in range(n):
        for j in range(i+1):
            value = matrix[i][j] - sum(lower[i][k]*lower[j][k] for k in range(j))
            lower[i][j] = math.sqrt(value) if i == j else value/lower[j][j]
    mid = [0.0]*n
    for i in range(n):
        mid[i] = (rhs[i]-sum(lower[i][j]*mid[j] for j in range(i)))/lower[i][i]
    volts = [0.0]*n
    for i in range(n-1, -1, -1):
        volts[i] = (mid[i]-sum(lower[j][i]*volts[j] for j in range(i+1,n)))/lower[i][i]
    volts = [5.0,0.0] + volts
    current = [(volts[a]-volts[b])/r for a,b,r in branches]
    supply = sum(i if a == 0 else -i if b == 0 else 0.0
                 for (a,b,_),i in zip(branches,current))
    # The independent oracle itself must satisfy KCL before it is used.
    for node in range(2,n+2):
        residual = sum(i if a == node else -i if b == node else 0.0
                       for (a,b,_),i in zip(branches,current))
        assert abs(residual) < 1e-12, (node,residual)
    return volts,current,supply


def compare_network(n, branches, actual, expected, case):
    expect_v, expect_i, supply = expected
    assert len(actual['mv']) == n+2 and len(actual['ua']) == len(branches)
    checks = 0
    for node, (want, mv) in enumerate(zip(expect_v, actual['mv'])):
        assert abs(mv-1000*want) <= 0.51, (case, node, mv, want)
        checks += 1
    for part, (want, ua) in enumerate(zip(expect_i, actual['ua'])):
        assert abs(ua-1e6*want) <= 0.51, (case, part, ua, want)
        checks += 1
    assert abs(actual['source_ua']+1e6*supply) <= 0.51, (case, actual, -supply)
    checks += 1
    # Check the returned branch currents too, allowing independent rounding of
    # each incident branch to an integer microamp. This is separate from the
    # exact-double KCL check on the reference solution.
    for node in range(2, n+2):
        incident = [(a, b, ua) for (a, b, _), ua in zip(branches, actual['ua'])
                    if a == node or b == node]
        residual = sum(ua if a == node else -ua for a, b, ua in incident)
        assert abs(residual) <= 0.51*len(incident), (case, node, residual)
        checks += 1
    return checks


def randomized(binary, count):
    rng = random.Random(0x8088555)
    checks = 0
    for case in range(count):
        n = 1 + case % 8
        branches = []
        # Every unknown has two shunts plus adjacent ladder links: <=4 leads
        # per terminal strip, with at most9 leads on each15-hole power rail.
        for node in range(2,n+2):
            branches += [(0,node,10*rng.randint(300,60000)),
                         (node,1,10*rng.randint(300,60000))]
        branches += [(i,i+1,10*rng.randint(100,60000)) for i in range(2,n+1)]
        expected = reference(n, branches)
        fixture = f"{n} {len(branches)}\n" + "".join(f"{a} {b} {r}\n" for a,b,r in branches)
        run = subprocess.run([str(binary),'--network'],input=fixture,text=True,
                             capture_output=True,check=True)
        actual = json.loads(run.stdout)
        checks += compare_network(n, branches, actual, expected, case)
        if case == 0:
            # Deliberately corrupt each measured kind and prove that the
            # independent comparison rejects it, including source direction.
            for kind in ('mv', 'ua', 'source_ua'):
                broken = json.loads(json.dumps(actual))
                if kind == 'source_ua':
                    broken[kind] = -broken[kind]
                else:
                    broken[kind][2 if kind == 'mv' else 0] += 5
                try:
                    compare_network(n, branches, broken, expected, case)
                except AssertionError:
                    pass
                else:
                    raise AssertionError(f'random oracle accepted corrupted {kind}')
            print('breadxt random: altered voltage/current/source direction controls rejected')
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--random',type=int,default=160)
    parser.add_argument('--no-sanitize',action='store_true')
    parser.add_argument('--random-only',action='store_true')
    args = parser.parse_args()
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix='breadxt-core-') as directory:
        binary = Path(directory)/'coretest'
        command = ['cc','-std=c99','-Wall','-Wextra','-Werror','-O1','-g']
        if not args.no_sanitize:
            command += ['-fsanitize=address,undefined']
        command += [str(ROOT/'apps/breadxt/hosttest/coretest.c'),'-lm','-o',str(binary)]
        subprocess.run(command,check=True,cwd=ROOT)
        if not args.random_only:
            subprocess.run([str(binary)],check=True,cwd=ROOT)
            for family in ('topology','numeric','digital','transient','scope','codec'):
                negative = subprocess.run([str(binary),'--negative',family],text=True,
                                          capture_output=True,cwd=ROOT)
                assert negative.returncode == 1 and '1 failures' in negative.stdout, (family,negative.stdout,negative.stderr)
            print('breadxt core: six independent expected-answer negative controls fail as intended')
        checks = randomized(binary,args.random)
        print(f'breadxt random: {args.random} resistor networks, {checks} voltage/current/KCL checks')
    print(f'breadxt host elapsed: {time.monotonic()-started:.2f}s')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
