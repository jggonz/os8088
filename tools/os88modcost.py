#!/usr/bin/env python3
"""What an ON-DEMAND MODULE costs the kernel when it is NOT loaded.

    python3 tools/os88modcost.py              # kern_big
    python3 tools/os88modcost.py --small      # kern_small
    python3 tools/os88modcost.py --ovl        # the boot overlay, same question
    python3 tools/os88modcost.py --detail     # every label, not just the totals

SPEC.md 2.8.1 states the rule this measures: *"A module's data does not move.
It stays in `.text`, reached through DS exactly as cold code's data is."*  So
a module's IMAGE leaves the kernel and its DATA does not, and the bytes that
serve a feature nobody has opened are resident for the life of the machine.
SPEC.md 2.8.6 opened one door out of that for STRINGS - read them through CS
out of the image - and this says how much never walked through it.

WHAT IT ANSWERS.  A label defined in `.text`, `.bss` or `.cold` whose every
reference in the tree is inside a module image section is resident memory that
exists only while a module is loaded.  Those are the bytes a self-contained
module would take with it.  Anything a resident caller also names is NOT
reported: it is shared, and moving it is a design question rather than an
accounting one.

HOW.  Two passes, and they are deliberately different instruments.

  Pass A reads the SOURCE, from kernel.asm down every %include, tracking the
  current `section` - so both the definition and every reference are filed
  under the section they are in.  It does not evaluate %ifdef, which is
  tools/os88ovlchk.py's rule and is right for the same reason: `filecp.inc`
  and `fdlg.inc` carry exactly one conditional `section` each, so filing their
  bodies as `.modp`/`.modd` describes the kern_small build, which is the one
  the question is about.  `--small` and the default differ only in WHICH
  sections count as a module (`.modh` is kern_big's; `.modp`/`.modd` are
  kern_small's).

  Pass B reads a nasm LISTING of the same source for the bytes each label
  emitted, attributed per section so that a `section` switch cannot pile a
  module's whole image onto the last resident label before it.

WHAT THE LISTING WILL DO TO YOU IF YOU LET IT.  Three traps, all sprung here:

  * it lists DEAD %if BRANCHES with no marker, so a `section` directive in the
    branch nasm skipped will silently redirect every size after it.  Pass B
    therefore never decides a LABEL'S section from the listing - only which
    bucket to accumulate into - and pass A's source scan is what classifies;
  * the source text starts at column 40 on EVERY line.  The `<depth>` field at
    36 is blank at depth 0, so slicing at 36 indents every kernel.asm label out
    of a `^label:` regex's reach and the bytes pile onto the previous label -
    which read as a 6-byte trampoline costing 229;
  * a bare `label:` line carries NO address.  Detect the label before the
    address, or that label is skipped and its bytes are charged to the one
    above it.

So the totals are CHECKED rather than trusted: pass B's per-section sums are
compared against `tools/kernsize.py --json`, which re-assembles the kernel and
measures it independently, and a shortfall over TOL fails the run.  Every one
of the three bugs above moved a section total by more than that.

WHAT IT CANNOT SEE, and both are conservative - they UNDER-report:

  * an indirect reference.  A label reached only through a table of pointers
    is named where the table is, not where it is dispatched from.  That is
    os88ovlchk.py's blind spot too and it stays a review rule.
  * a reference inside a %macro BODY is filed where the body is written, not
    where it expands.

AND THAT SECOND ONE IS NOT CONSERVATIVE, which this file claimed until a wave
was built on the claim.  It cuts BOTH ways: a macro body written INSIDE a
module section makes its calls look module-only when they expand somewhere
else entirely.  `sched_mode_set` read as `.modc`-only and is called by the
BOOT OVERLAY - `driver.inc:3475` is line 73 of `%macro CFG_BOOT`, expanded at
`driver.inc:3825` into `.ovl` - so moving it into the image would have put a
routine the boot ladder needs out of the boot ladder's reach.
`tools/os88ovlchk.py` refused the build, which is the only reason that was ten
wasted minutes rather than a kernel that does not boot.  A row whose module
references ALL come from inside a macro body is marked `?macro` and must be
checked by hand before anything is moved.

  * A THIRD constraint this cannot see at all, and it is the one that costs
    the most: **a buffer or string a KERNEL routine reads must stay
    DS-addressable.** `font_run` takes `DS:SI`; `wm_create` takes a template
    at `ES:SI` with `ES` = the caller's DS; `OSAPI_WM_TITLE` stores a pointer
    the window manager dereferences on any repaint. So "named only from the
    image" does NOT imply "can move into the image", and a good share of the
    MOVABLE column below is composed-then-drawn scratch that cannot:
    `cp_dmbuf` is built per row and handed to `cp_run` -> `cw_font_run`,
    `clk_fbuf` and `fdlg_num` are the same shape, `fdlg_tpl` is a window
    template, and `cp_sbuf` is the staging buffer that exists BECAUSE
    SPEC.md 2.8.6.1's strings already moved.  Moving such a string is still
    possible - that is what `cp_stage` is - but it costs a resident buffer
    back, so it is a NET figure per image and never the gross one here.
    MOVABLE is an upper bound. Cost a wave by hand before believing it.

WHAT IT DELIBERATELY EXCLUDES: an `apic_*` label (SPEC.md 2.8, and the comment
over `osapi_table`).  It names a cell of the PUBLISHED table so that a module
can far-call the door a package already uses, and that cell is resident for the
package ABI whether any module names it or not - so its marginal cost here is
zero, and counting it would make the whole table look like a module's.
"""
import argparse
import collections
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Module image sections, per build.  `.modp`/`.modd` are FILECP.DRV and
# FDLG.DRV and exist on kern_small alone (FCP_MOD/FDLG_MOD); on kern_big those
# bodies assemble into `.cold` and are resident by decision (SPEC.md 2.8).
# `.modh` is HIBER.DRV, which is kern_big's - the 128KB machine has no hard
# disk to hibernate to (mod.inc) - and so are `.modk` DOCK.DRV and `.modx`
# EXTD.DRV (SPEC.md 30.5, 39.19.6), which this list missed until the second.
MODS = {False: ('.modc', '.modf', '.modl', '.modh', '.modk', '.modx'),
        True:  ('.modc', '.modf', '.modl', '.modp', '.modd')}
OVLS = ('.ovl', '.ovlw')
RESIDENT = ('.text', '.bss', '.cold')

# The far shims a module leaves the kernel through (SPEC.md 2.6's cw_* and
# each module's own `*f_` block).  They are named only by the image BY
# CONSTRUCTION - that is what they are for - and they cannot move into it,
# being the thing it calls to get out.  Reported, and never counted as
# movable.
SHIM = re.compile(r'^(?:dskf_|dkf_|drvf_|fmf_|mmf_|memf_|cw_|hbk_)|_f$')

# `apic_*` NAMES AN OSAPI CELL and costs a module nothing: the cell is in the
# published table whether or not any module calls it, and the label emits no
# byte at all.  It is excluded rather than counted, and the exclusion is not a
# nicety - a bare label in the middle of the table ABSORBS every unlabelled
# cell after it, so the first one added here read 440 bytes and would have sent
# the next reader chasing a table that is resident for the package ABI.
NOTMOD = re.compile(r'^apic_')

SECT  = re.compile(r'^\s*section\s+(\.\w+)')
INCL  = re.compile(r'^\s*%include\s+"([^"]+)"')
LABEL = re.compile(r'^([A-Za-z_][\w.]*)\s*:')
EQU   = re.compile(r'^\s*([A-Za-z_][\w.]*)\s+equ\b', re.I)
IDENT = re.compile(r'[A-Za-z_][A-Za-z_0-9]*')
STRIP = re.compile(r"""('[^']*'|"[^"]*"|;.*$)""")
DATA  = re.compile(r'^\s*(times\s+[^\s;]+\s+)?(d[bwdqt]|res[bwdqt])\b', re.I)

TOL = 0.02          # pass B may fall this far short of kernsize before it is
                    # a parser bug rather than an unlabelled run of bytes


def scan_source():
    """label -> (section, file, line), and label -> [(file, line, section)]."""
    defs, refsite, kind = {}, collections.defaultdict(list), {}
    sec = ['.text']
    macro = [0]

    def walk(path):
        try:
            src = open(path, encoding='utf-8', errors='replace').read().splitlines()
        except FileNotFoundError:
            return
        rel = os.path.relpath(path, ROOT)
        for n, raw in enumerate(src, 1):
            m = INCL.match(raw)
            if m:
                for c in (os.path.join(ROOT, 'kernel', m.group(1)),
                          os.path.join(ROOT, m.group(1)),
                          os.path.join(ROOT, 'apps', m.group(1)),
                          os.path.join(os.path.dirname(path), m.group(1))):
                    if os.path.isfile(c):
                        walk(c)
                        break
                continue
            if re.match(r'\s*%macro\b', raw, re.I):
                macro[0] += 1
                continue
            if re.match(r'\s*%endmacro\b', raw, re.I):
                macro[0] = max(0, macro[0] - 1)
                continue
            m = SECT.match(raw)
            if m:
                sec[0] = m.group(1)
                continue
            line = STRIP.sub(' ', raw)
            m = LABEL.match(line)
            if m and not m.group(1).startswith('.'):
                lab, rest = m.group(1), line[m.end():]
                if lab not in defs:
                    defs[lab] = (sec[0], rel, n)
                    kind[lab] = 'data' if DATA.match(rest) else 'code'
                line = rest
            m = EQU.match(line)
            if m and not m.group(1).startswith('.'):
                lab = m.group(1)
                if lab not in defs:
                    defs[lab] = ('equ', rel, n)
                    kind[lab] = 'equ'
                line = line[m.end():]
            for tok in IDENT.findall(line):
                refsite[tok].append((rel, n, sec[0], macro[0] > 0))

    walk(os.path.join(ROOT, 'kernel', 'kernel.asm'))
    return defs, refsite, kind


def nbytes(field):
    """How many bytes this listing line emitted."""
    f = field.strip()
    if not f:
        return 0
    m = re.fullmatch(r'<res ([0-9A-F]+)h>', f)          # resb/resw in .bss
    if m:
        return int(m.group(1), 16)
    m = re.fullmatch(r'[0-9A-F?]{2}<rep ([0-9A-F]+)h>', f)   # times N db v
    if m:
        return int(m.group(1), 16)
    # hex pairs, `??` for a nobits byte, `[....]` a relocation of its own
    # nibble count, and a trailing `-` meaning the next line continues it
    f = f.rstrip('-').replace('[', '').replace(']', '')
    return len(re.findall(r'[0-9A-F?]', f)) // 2


def listing(defines):
    """Assemble the kernel with -l and return (label -> bytes, section -> bytes)."""
    with tempfile.TemporaryDirectory() as td:
        lst = os.path.join(td, 'k.lst')
        cmd = ['nasm', '-f', 'bin', '-w+error',
               '-I', os.path.join(ROOT, 'kernel') + os.sep,
               '-I', os.path.join(ROOT, 'apps') + os.sep,
               '-I', os.path.join(ROOT, 'build') + os.sep,
               '-l', lst, '-o', os.path.join(td, 'k.bin')]
        cmd += defines + [os.path.join(ROOT, 'kernel', 'kernel.asm')]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            sys.exit('nasm: ' + (r.stderr.strip().splitlines() or ['failed'])[-1])
        sizes = collections.Counter()
        persec = collections.Counter()
        cur, sec = {}, '.text'
        for raw in open(lst, encoding='utf-8', errors='replace'):
            # column-based: line no, address at 7, bytes at 16, a 4-wide
            # <depth> field at 36 that is BLANK at depth 0, source at 40
            body = raw[40:].rstrip('\n') if len(raw) > 40 else ''
            m = SECT.match(body)
            if m:
                sec = m.group(1)
                continue
            m = LABEL.match(STRIP.sub(' ', body))
            if m and not m.group(1).startswith('.'):
                cur[sec] = m.group(1)                   # BEFORE the address
            if len(raw) < 16 or not re.fullmatch(r'[0-9A-F]{8}', raw[7:15]):
                continue                                # a bare label has none
            n = nbytes(raw[16:36])
            persec[sec] += n
            if cur.get(sec):
                sizes[cur[sec]] += n
        return sizes, persec


def crosscheck(persec, defines):
    """Pass B against an independent re-assembly (tools/kernsize.py)."""
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'kernsize.py'),
                        '--json'] + defines, capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        return ['kernsize.py would not run - cross-check SKIPPED']
    k = json.loads(r.stdout)
    bad = []
    # `.cold` absorbs `.modp`/`.modd` on kern_big and `.modh` on kern_small:
    # the source carries those `section` directives unconditionally for
    # os88ovlchk's sake, so compare the pair rather than the part.
    got_cold = persec['.cold'] + persec['.modp'] + persec['.modd'] + persec['.modh']
    for name, mine in (('text', persec['.text']), ('bss', persec['.bss']),
                       ('cold', got_cold), ('ovl', persec['.ovl']),
                       ('ovlw', persec['.ovlw']), ('lowbss', persec['.lowbss'])):
        want = k.get(name, 0)
        if want and mine < want * (1 - TOL):
            bad.append('%-7s attributed %d of kernsize\'s %d (%.1f%% short)'
                       % (name, mine, want, 100.0 * (want - mine) / want))
    return bad


# --- the far shims, and whether the PUBLIC table already reaches them --------
# A module leaves its image through a four-byte far shim (`call <near body>` +
# `retf`) because its CS is its heap claim: a near call into KERNEL_SEG or
# COLD_SEG would assemble and emit a displacement between two address spaces,
# which is the bug tools/os88ovlchk.py exists to refuse.  An `OSAPI_*` cell is
# the same door built for packages - and `OSAPI_SLOT` is `push ds / push cs /
# pop ds / call / pop ds / retf`, so calling one with DS already KERNEL_SEG is
# the shim's semantics exactly, plus four wasted instructions.
#
# So a shim whose body the table ALREADY reaches is a duplicate the module can
# stop using.  One that it does not is NOT worth publishing for this: a cell is
# **8 bytes** and a shim is **4**, and the table is contiguous (167 cells in 168
# positions), so a new slot costs 8 to save 4 - and commits the SDK for ever.
CELL = re.compile(r'\s*OSAPI_(SLOT|CSLOT|JSLOT|XCELL|CXCELL|NCELL|FARCELL|RSLOT|RXCELL|RCXCELL|RNCELL|RCSLOT|JCELL|FCELL)\s+(\w+)\s*;\s*(0x[0-9A-Fa-f]+)')
SHIMDEF = re.compile(r'^((?:dskf_|dkf_|drvf_|fmf_|mmf_|memf_|cw_|hbk_)\w*|\w*_f):'
                     r'\s*(?:call\s+(?:\w+:)?(\w+))?')


def _sources():
    d = os.path.join(ROOT, 'kernel')
    return {f: open(os.path.join(d, f), errors='replace').read().splitlines()
            for f in sorted(os.listdir(d)) if f.endswith(('.inc', '.asm'))}


def api_check(rows):
    """Which module shims wrap a body an OSAPI_* cell already lands on."""
    src = _sources()
    # every one-hop thunk: a label whose whole body is one call/jmp and a ret
    thunk = {}
    for L in src.values():
        for i, line in enumerate(L):
            m = re.match(r'^(\w+):\s*(.*?)\s*(?:;.*)?$', line)
            if not m:
                continue
            lab, step = m.groups()
            j = i
            while not step and j + 1 < len(L):
                j += 1
                step = re.sub(r';.*', '', L[j]).strip()
            m2 = re.match(r'(?:call|jmp)\s+(?:strict\s+near\s+)?(?:\w+:)?(\w+)\s*$', step)
            if not m2:
                continue
            tail = ''
            for k in range(j + 1, min(j + 4, len(L))):
                t = re.sub(r';.*', '', L[k]).strip()
                if t:
                    tail = t
                    break
            if tail in ('ret', 'retf') or step.startswith('jmp'):
                thunk[lab] = m2.group(1)

    def resolve(lab):
        seen = set()
        while lab in thunk and lab not in seen:
            seen.add(lab)
            lab = thunk[lab]
        return lab

    shim = {}
    for L in src.values():
        for i, line in enumerate(L):
            m = SHIMDEF.match(line)
            if not m:
                continue
            lab, tgt = m.groups()
            if tgt is None and i + 1 < len(L):
                m2 = re.match(r'\s*call\s+(?:\w+:)?(\w+)', L[i + 1])
                tgt = m2.group(1) if m2 else None
            if tgt:
                shim[lab] = tgt

    cells = {}
    for line in src['kernel.asm']:
        m = CELL.match(line)
        if m:
            cells.setdefault(resolve(m.group(2)), (m.group(3), m.group(2), m.group(1)))

    out = []
    for r in rows:
        if r['kind'] == 'data' or not SHIM.match(r['lab']):
            continue
        body = resolve(shim[r['lab']]) if r['lab'] in shim else None
        out.append((r['sz'], r['lab'], body, cells.get(body)))
    return sorted(out, key=lambda x: (x[3] is None, -x[0], x[1]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--small', action='store_true', help='measure kern_small')
    ap.add_argument('--ovl', action='store_true',
                    help='the boot overlay instead of the modules')
    ap.add_argument('--detail', action='store_true', help='every label')
    ap.add_argument('--api', action='store_true',
                    help='which far shims the public OSAPI table already reaches')
    ap.add_argument('--json', action='store_true')
    a = ap.parse_args()

    defines = ['-DKERN_SMALL' if a.small else '-DKERN_BIG']
    target = set(OVLS if a.ovl else MODS[a.small])

    defs, refsite, kind = scan_source()
    sizes, persec = listing(defines + ['-DKZIP', '-DKZ_SECS=0', '-DKZ_RPARA=0',
                                       '-DKZ_NBLK=1', '-DKZ_HEADSEC=9'])
    bad = crosscheck(persec, defines)
    if bad:
        sys.exit('os88modcost: the listing pass does not add up, so the numbers\n'
                 'below would be wrong rather than approximate:\n  '
                 + '\n  '.join(bad))

    rows = []
    for lab, (dsec, f, ln) in defs.items():
        if dsec not in RESIDENT or NOTMOD.match(lab):
            continue
        sites = [x for x in refsite[lab] if (x[0], x[1]) != (f, ln)]
        naming = {x[2] for x in sites}
        # ANY macro-body reference, not all of them: `sched_mode_set` has a
        # real `.modc` caller AND a `%macro CFG_BOOT` one that expands into
        # `.ovl`, and it is the second that decides whether it can move.
        inmac = any(x[3] for x in sites)
        if naming and naming <= target:
            rows.append(dict(sz=sizes.get(lab, 0), lab=lab, sec=dsec, macro=inmac,
                             kind=kind.get(lab, '?'), f=f, ln=ln,
                             by=sorted(naming),
                             cls=('DATA  .text' if dsec == '.text' and kind.get(lab) == 'data'
                                  else 'STATE .bss' if dsec == '.bss'
                                  else 'far shim' if SHIM.match(lab)
                                  else 'CODE')))
    rows.sort(key=lambda r: -r['sz'])
    if a.json:
        json.dump(rows, sys.stdout, indent=1)
        return

    what = 'the boot overlay' if a.ovl else 'an on-demand module'
    print('=== kern_%s: resident bytes named ONLY from %s ==='
          % ('small' if a.small else 'big', what))
    print('    (%s)' % ' '.join(sorted(target)))
    print()
    cls = collections.Counter()
    for r in rows:
        cls[r['cls']] += r['sz']
    for c in ('DATA  .text', 'STATE .bss', 'CODE', 'far shim'):
        if cls[c]:
            print('  %-12s %6d%s' % (c, cls[c],
                  '   - the ABI out, cannot move' if c == 'far shim' else ''))
    print('  %-12s %6d' % ('TOTAL', sum(cls.values())))
    print('  %-12s %6d' % ('MOVABLE', sum(v for c, v in cls.items() if c != 'far shim')))
    print()
    per = collections.Counter()
    for r in rows:
        for b in r['by']:
            per[b] += r['sz'] / len(r['by'])
    for b in sorted(per):
        print('  %-8s %6.0f bytes' % (b, per[b]))
    if a.api:
        res = api_check(rows)
        print()
        print('  far shims against the public table  (a cell is 8 bytes, a shim 4)')
        print('  %5s  %-22s %-22s %s' % ('bytes', 'shim', '-> body', 'public cell'))
        dup = 0
        for sz, lab, body, cell in res:
            c = '%s %s (%s)' % (cell[0], cell[2], cell[1]) if cell else '-'
            print('  %5d  %-22s %-22s %s' % (sz, lab, body or '?', c))
            dup += sz if cell else 0
        print('  REDUNDANT: %d bytes of shim wrap a body the table already reaches'
              % dup)
    if a.detail:
        print()
        print('  %6s  %-22s %-6s %-12s %s' % ('bytes', 'label', 'sec', 'named from', 'where'))
        for r in rows:
            print('  %6d  %-22s %-6s %-12s %s:%d%s'
                  % (r['sz'], r['lab'], r['sec'], ','.join(r['by']), r['f'],
                     r['ln'], '   ?macro' if r.get('macro') else ''))


if __name__ == '__main__':
    main()
