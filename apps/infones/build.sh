#!/bin/sh
# =============================================================================
# os8088 - apps/infones/build.sh
#
# INFONES's host checks (SPEC.md 91.14.4), run by `make infones` BEFORE the
# target build, and each one STOPS IT: a stale cell, a double draw or a shadow
# that has stopped describing the glass costs a second on the target machine,
# and none of the three shows in an emulator (PERFORMANCE.md).
#
#     apps/infones/build.sh      the checks only
#     make infones               the checks, then the package
#     make infonesdisk           ...and the four floppies
#
# THE CHECKS, in the order they run
#
#   the os88.h drift check     apps/infones/hosttest/os88.h is a SECOND copy
#                              of the SDK header, placed ahead of apps/cc on
#                              the include path so that the program compiles
#                              against the same shapes on the host and on the
#                              8086. Every prototype in it must exist in
#                              apps/cc/os88.h with the same signature, or the
#                              harness is testing a different API than the
#                              machine has - which is a test that passes and
#                              means nothing (SPEC.md 91.14.4)
#   hosttest/niuitest.c        the whole program over that stub with a MODEL
#                              OF THE GLASS: after every step the glass must
#                              show what the shadow says it shows, field for
#                              field, and the cost table is printed in calls,
#                              cells and milliseconds
#   tools/niref.py --selftest  the independent compositor's own check: it
#                              injects a one-bit defect and REQUIRES the
#                              compare to fail, because a check that cannot
#                              fail is not a check. (--check against a dumped
#                              frame arrives in wave 2, with the composer that
#                              produces one.)
#   hosttest/nimemtest.sh      nimem.inc AND niband.inc on a real x86 with
#                              SS != DS and an ES sentinel, in raw QEMU, with
#                              two negative controls
#
# NOT HERE, BECAUSE IT TAKES MINUTES: hosttest/nicputest.sh runs the 2A03
# against nestest.log's 8,991 lines and blargg's instr_test-v5 singles
# (`make nicputest`) - the c64cputest and rcz80test precedent. It also fetches
# a fixture, and a fresh clone's `make infones` must neither stall on a
# network nor fail without one.
#
# The compiler for the TARGET is not in this tree: tools/setup-cc.sh fetches
# SmallerC at its pinned commit into build/cc/ (gitignored). The checks below
# need only the host's cc, python3 and - for the mover gate - qemu.
# =============================================================================
set -e

cd "$(dirname "$0")/../.."          # the repo root, whatever the caller's cwd

BUILD=build
HOSTCC=${HOSTCC:-cc}
mkdir -p $BUILD

# --- the stub header may not drift from the SDK's ---------------------------
python3 - <<'PY'
import re, sys

def protos(path):
    src = open(path).read()
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)     # comments out
    out = {}
    for m in re.finditer(
            r'(?m)^\s*((?:unsigned|int|void|char|const)[\w\s\*]*?)'
            r'\b(os88_\w+)\s*\(([^;{]*)\)\s*;', src):
        ret, name, args = m.group(1), m.group(2), m.group(3)
        norm = ' '.join((ret + '|' + args).split())
        norm = norm.replace(' *', '*').replace('* ', '*')
        # the argument NAMES are the author's; only the types are the contract
        norm = re.sub(r'\b(?!unsigned|int|void|char|const|struct|os88_\w+)'
                      r'[a-z_]\w*\b(?!\s*\*)', '', norm)
        out[name] = ' '.join(norm.split())
    return out

sdk = protos('apps/cc/os88.h')
stub = protos('apps/infones/hosttest/os88.h')
bad = 0
for name, sig in sorted(stub.items()):
    if name in ('os88_main', 'os88_paint', 'os88_onkey', 'os88_onclick',
                'os88_oncmd', 'os88_about', 'os88_onwake', 'os88_onfile'):
        continue                    # the callbacks the PACKAGE defines
    if name not in sdk:
        print('nios88: %s is in the stub and not in apps/cc/os88.h' % name)
        bad = 1
    elif sdk[name] != sig:
        print('nios88: %s has drifted' % name)
        print('        sdk  %s' % sdk[name])
        print('        stub %s' % sig)
        bad = 1
if bad:
    sys.exit(1)
print('nios88: %d prototype(s) in the stub, every one identical to '
      'apps/cc/os88.h' % len(stub))
PY

# The UI harness includes infones.c itself, with apps/infones/hosttest ahead of
# apps/cc on the include path so that its stub os88.h is the one that resolves.
# -w because the stubs deliberately ignore arguments; the checks are the point.
$HOSTCC -O1 -w -I apps/infones/hosttest -I apps/infones \
    -o $BUILD/niuitest apps/infones/hosttest/niuitest.c
$BUILD/niuitest

# The independent compositor's own gate: it injects a one-bit defect and
# REQUIRES the compare to fail, because a check that cannot fail is not a
# check.
python3 tools/niref.py --selftest

# ...and the C MODEL's frame against it (SPEC.md 91.14.4). niuitest built the
# fixture from the formula tools/niref.py's synth() and nimemtest.asm's
# nifix_build also build - three independent spellings - and wrote both the
# state blob and the 256x240 frame its C compositor produced. The blob is
# diffed against niref.py's own first, because two frames agreeing about a
# state neither of them checked would prove nothing.
python3 tools/niref.py --synth $BUILD/nifix.state
cmp $BUILD/nirefC.state $BUILD/nifix.state || {
    echo "niref: niuitest's fixture is not tools/niref.py's synth() - the two"
    echo "       harnesses are composing different pictures"
    exit 1
}
python3 tools/niref.py --check $BUILD/nirefC.state $BUILD/nirefC.frm

# The movers and the tile-cache decoder, on a real x86 with SS != DS.
apps/infones/hosttest/nimemtest.sh

# THE DISPATCH TABLE'S GATE (SPEC.md 91.4.1). nicpu.inc's dispatch table is
# 512 bytes of hand-written label names and its cycle table 256 hand-written
# numbers, and a typo in either is a WRONG OPCODE rather than a build error:
# nasm resolves `o_lda_zpx` whether or not that is the handler the opcode
# wants. So this expands the macro invocations, collects every literal `o_*:`
# label beside them, and refuses a build in which any of the table's 256
# entries is undefined - or in which the cycle table is not 256 bytes long. It
# is what makes a 256-entry table of names safe to write by hand.
python3 - <<'NITAB'
import re, sys
src = open('apps/infones/nicpu.inc').read()

labels = set(re.findall(r'(?m)^(o_\w+):', src))
for pat, mk in (
        (r'(?m)^\s*NI_RD\s+(\w+)\s*,\s*(\w+)',      lambda m: 'o_%s_%s' % m.groups()),
        (r'(?m)^\s*NI_ST\s+(\w+)\s*,\s*(\w+)\s*,',  lambda m: 'o_%s_%s' % m.groups()),
        (r'(?m)^\s*NI_RMW\s+(\w+)\s*,\s*(\w+)',     lambda m: 'o_%s_%s' % m.groups()),
        (r'(?m)^\s*NI_UNS\s+(\w+)\s*,\s*(\w+)',     lambda m: 'o_%s_%s' % m.groups()),
        (r'(?m)^\s*NI_SAX\s+(\w+)',                 lambda m: 'o_sax_%s' % m.group(1))):
    for m in re.finditer(pat, src):
        labels.add(mk(m))

tab = src[src.index('ni_tab:'):src.index('ni_cyc:')]
entries = re.findall(r'\b(o_\w+)\b', tab)
bad = 0
if len(entries) != 256:
    print('nicputab: the dispatch table has %d entries, want 256' % len(entries))
    bad = 1
for i, e in enumerate(entries):
    if e not in labels:
        print('nicputab: opcode %02X dispatches to %s, which nothing defines'
              % (i, e))
        bad = 1
cyc = src[src.index('ni_cyc:'):]
cyc = cyc[:cyc.index('\n\n')]
n = 0
for line in cyc.splitlines():
    line = line.split(';')[0]
    if ' db ' in line:
        n += len(line.split('db', 1)[1].split(','))
if n != 256:
    print('nicputab: the cycle table has %d entries, want 256' % n)
    bad = 1
if bad:
    sys.exit(1)
print('nicputab: 256 dispatch entries, %d distinct handlers, 256 cycle counts'
      % len(set(entries)))
NITAB

# THE FACT-LINE GATE, TIED TO THE SOURCES (SPEC.md 91.7.2). Every greyed item
# in this package must have a sentence on the panel's fact line, and nothing
# in the language ties the two together: a wave that greys an item and forgets
# its sentence is a defect no build step can catch - which is exactly why this
# one exists.
#
# THE FIRST VERSION OF THIS GATE DID NOT DO WHAT ITS COMMENT SAID. It claimed
# to refuse "a build with more greyed items than facts" and then only checked
# that greyed > 0 and facts >= 2, so sixteen greyed spellings passed against
# two sentences. What it checks now, and each row is a real defect:
#   1. there are greyed items at all (has the D macro moved?);
#   2. ni_facts[] has exactly one entry per NI_FACT_* constant, so a constant
#      added without a sentence shifts every index below it - and every
#      sentence fits NI_FCELLS, because a contract that states a string the
#      field cannot hold is a contract nobody can read;
#   3. EVERY NI_FACT_* other than NONE is STORED somewhere in the package -
#      an `ni_fact = NI_FACT_X` or a rotation-table entry - or is named on a
#      PLANNED line saying which wave arms it. A sentence nothing can ever
#      select is a fact the user cannot read;
#   4. the rotation covers every fact that is not PLANNED, which is what makes
#      "Mute is greyed with the fact" true ON THE GLASS (decision 3) rather
#      than only true of a key nothing on the machine names.
python3 - <<'PY'
import re, sys
menu = open('apps/infones/nimenu.c').read()
panel = open('apps/infones/nipanel.c').read()
pkg = ''.join(open('apps/infones/' + f).read()
              for f in ('infones.c', 'nippu.c', 'nimap.c', 'nirun.c',
                        'nipanel.c', 'nimenu.c', 'nirom.c', 'nicmd.c',
                        'niabout.c'))
bad = 0

menu_nc = re.sub(r'/\*.*?\*/', ' ', menu, flags=re.S)
panel_nc = re.sub(r'/\*.*?\*/', ' ', panel, flags=re.S)
pkg_nc = re.sub(r'/\*.*?\*/', ' ', pkg, flags=re.S)

greyed = len(re.findall(r'\bD\s*"', menu_nc))
if greyed == 0:
    print('nifact: nimenu.c has no greyed items at all - has the D macro '
          'moved?')
    sys.exit(1)

consts = re.findall(r'#define\s+(NI_FACT_[A-Z0-9]+)\s+(\d+)', panel)
names = [c for c, _ in consts if c != 'NI_FACT_N']
if [int(v) for c, v in consts if c != 'NI_FACT_N'] != list(range(len(names))):
    print('nifact: the NI_FACT_* constants are not 0..n-1 in order, and '
          'ni_facts[] is indexed by them')
    bad = 1

tbl = panel[panel.index('ni_facts[NI_FACT_N] = {'):]
tbl = tbl[:tbl.index('};')]
tbl = re.sub(r'/\*.*?\*/', ' ', tbl, flags=re.S)
facts = re.findall(r'"((?:[^"\\]|\\.)*)"', tbl)
if len(facts) != len(names):
    print('nifact: %d NI_FACT_* constant(s) but %d sentence(s) in ni_facts[]'
          % (len(names), len(facts)))
    bad = 1

cells = int(re.search(r'#define\s+NI_FCELLS\s+(\d+)', panel).group(1))
for t in facts:
    if len(t) > cells:
        print('nifact: the fact-line sentence [%s] is %d cells, over '
              'NI_FCELLS = %d' % (t, len(t), cells))
        bad = 1

planned = set()
for line in panel.splitlines():
    if 'PLANNED' in line:
        planned |= set(re.findall(r'NI_FACT_[A-Z0-9]+', line))
for n in names:
    if n == 'NI_FACT_NONE':
        continue
    stored = re.search(r'(ni_fact\s*=\s*%s\b|t\[n\+\+\]\s*=\s*%s\b)' % (n, n),
                       pkg_nc)
    if not stored and n not in planned:
        print('nifact: %s has a sentence but nothing in the package ever '
              'selects it, and no PLANNED line in nipanel.c says which wave '
              'will' % n)
        bad = 1

rot = set(re.findall(r't\[n\+\+\]\s*=\s*(NI_FACT_[A-Z0-9]+)', panel_nc))
for n in names:
    if n == 'NI_FACT_NONE' or n in planned:
        continue
    if n not in rot:
        print('nifact: %s is live but is not in ni_fact_rotate table, so the '
              'one fact row can never show it' % n)
        bad = 1

if bad:
    sys.exit(1)
print('nifact: %d greyed spelling(s) in nimenu.c, %d sentence(s) of <= %d '
      'cells, %d in the rotation, %d planned'
      % (greyed, len(facts), cells, len(rot), len(planned)))
PY

# THE SCRATCH TABLE IS ONE TABLE IN TWO LANGUAGES (SPEC.md 91.3.1). nicpu.inc
# declares the core's scratch as `NI_S_* equ n` and infones.c as
# `#define NI_S_* n`, and both files carry a comment saying the two are ONE
# table - which was not true: the C copy was missing NI_S_T, NI_S_T2,
# NI_S_CSAV, NI_S_PGC and NI_S_T3 while claiming to be whole, so a wave-2
# author reading only the C would have seen 12..0x7F as free and put a word of
# their own on the temporary that holds half an (zp,X) pointer between two
# reads. Nothing in either language can see across the two files, and there is
# no build error for it: the address is legal and the corruption is one wrong
# operand mid-instruction. So the comparison is done here, IN BOTH DIRECTIONS.
python3 - <<'PY'
import re, sys
asm = open('apps/infones/nicpu.inc').read()
c   = open('apps/infones/infones.c').read()

a = {}
for m in re.finditer(r'(?m)^(NI_S_\w+)\s+equ\s+(0x[0-9A-Fa-f]+|\d+)', asm):
    a[m.group(1)] = int(m.group(2), 0)
d = {}
for m in re.finditer(r'(?m)^#define\s+(NI_S_\w+)\s+(0x[0-9A-Fa-f]+|\d+)', c):
    d[m.group(1)] = int(m.group(2), 0)

bad = 0
if not a or not d:
    print('niscr: one of the two tables is EMPTY - has a prefix moved?')
    sys.exit(1)
for k in sorted(set(a) | set(d)):
    if k not in d:
        print('niscr: %s = %d is in nicpu.inc and not in infones.c - the C '
              'copy must be the WHOLE table, reserved entries included'
              % (k, a[k]))
        bad = 1
    elif k not in a:
        print('niscr: %s = %d is in infones.c and not in nicpu.inc' % (k, d[k]))
        bad = 1
    elif a[k] != d[k]:
        print('niscr: %s is %d in nicpu.inc and %d in infones.c'
              % (k, a[k], d[k]))
        bad = 1
if bad:
    sys.exit(1)
print('niscr: %d scratch offset(s), identical in nicpu.inc and infones.c'
      % len(a))
PY

# THE STRUCTS ARE ONE TABLE IN TWO LANGUAGES TOO, AND NOTHING CHECKED THEM
# (SPEC.md 91.3.1, 91.5). The `niscr` row above exists because "nothing in
# either language can see across the two files, and there is no build error for
# it" - and that sentence is true VERBATIM of four more pairs that shipped with
# only a comment holding each together:
#
#   struct ni_comp / NIC_*      thirteen words written field by field by C on
#                               every one of 240 scanlines and read as byte
#                               offsets by niband.inc. Insert or reorder a
#                               field and it is not a build error in either
#                               language: it is a wrong operand on every line
#                               of every frame.
#   struct ni_mach / NIM_*      the same shape for the core's register file,
#                               and this hole PREDATES wave 2.
#   ni_bgpal / ni_sppal         16 bytes each in C against `resb 16` in the
#                               composer's .bss.
#   FSXM_VGA13                  a mode id the SDK spells by NAME and the menu
#                               greys on by BIT INDEX.
#
# So this reads both spellings of each and refuses a disagreement in either
# direction: the ordered field NAMES, their WORD offsets, and the reservation's
# own length.
python3 - <<'NISTRUCT'
import re, sys

band = open('apps/infones/niband.inc').read()
cpu  = open('apps/infones/nicpu.inc').read()
c    = open('apps/infones/infones.c').read()
menu = open('apps/infones/nimenu.c').read()
api  = open('apps/os88api.inc').read()
bad = 0

def cfields(name):
    """the ordered field names of `struct <name>` in infones.c"""
    m = re.search(r'struct\s+%s\s*\{(.*?)\n\};' % name, c, re.S)
    if not m:
        print('nistruct: infones.c has no `struct %s`' % name)
        sys.exit(1)
    body = re.sub(r'/\*.*?\*/', ' ', m.group(1), flags=re.S)
    out = []
    for decl in body.split(';'):
        decl = decl.strip()
        if not decl:
            continue
        parts = decl.split()
        if len(parts) < 2:
            print('nistruct: cannot read the declaration [%s]' % decl)
            sys.exit(1)
        if parts[0] in ('unsigned', 'int', 'char', 'signed', 'const'):
            names = ' '.join(parts[1:])
        else:
            print('nistruct: struct %s declares [%s] - EVERY FIELD IS A WORD '
                  'here (SPEC.md 91.5)' % (name, decl))
            sys.exit(1)
        for n in names.split(','):
            n = n.strip()
            if not re.match(r'^[A-Za-z_]\w*$', n):
                print('nistruct: struct %s declares [%s], which is not a '
                      'plain word field' % (name, n))
                sys.exit(1)
            out.append(n)
    return out

def asmequs(src, prefix):
    return [(m.group(1)[len(prefix):].lower(), int(m.group(2), 0))
            for m in re.finditer(r'(?m)^(%s\w+)\s+equ\s+(0x[0-9A-Fa-f]+|\d+)'
                                 % prefix, src)]

def check(cname, src, prefix, where):
    global bad
    cf = cfields(cname)
    af = asmequs(src, prefix)
    if [n for n, _ in af] != cf:
        print('nistruct: struct %s is %s' % (cname, ', '.join(cf)))
        print('          %s* in %s is %s'
              % (prefix, where, ', '.join(n for n, _ in af)))
        print('          the two are ONE table, and the composer or the core '
              'reads the WRONG WORD when they differ')
        bad = 1
        return None
    for i, (n, off) in enumerate(af):
        if off != i * 2:
            print('nistruct: %s%s is %d and field %d of struct %s is at %d '
                  '(every field is a word)'
                  % (prefix, n.upper(), off, i, cname, i * 2))
            bad = 1
    return cf

comp = check('ni_comp', band, 'NIC_', 'niband.inc')
mach = check('ni_mach', cpu,  'NIM_', 'nicpu.inc')

# ...and the RESERVATIONS, because a struct that agrees with a table of offsets
# still overruns a .bss that is short of it.
m = re.search(r'(?m)^_ni_cs:\s+resw\s+(\d+)', band)
if not m:
    print('nistruct: niband.inc has no `_ni_cs: resw N`')
    bad = 1
elif comp is not None and int(m.group(1)) != len(comp):
    print('nistruct: _ni_cs reserves %s word(s) and struct ni_comp has %d '
          'field(s)' % (m.group(1), len(comp)))
    bad = 1

seg = cpu[cpu.index('_ni_m:'):cpu.index('NIM_MACHSEG equ')]
n = len(re.findall(r'(?m)^\s*resw 1\s+;\s*NIM_\w+', seg))
if mach is not None and n != len(mach):
    print('nistruct: _ni_m reserves %d word(s) and struct ni_mach has %d '
          'field(s)' % (n, len(mach)))
    bad = 1

# the two palette LUTs, 16 bytes each in both languages
for cn, an in (('ni_bgpal', '_ni_bgpal'), ('ni_sppal', '_ni_sppal')):
    mc = re.search(r'unsigned char\s+%s\s*\[\s*(\d+)\s*\]' % cn, c)
    ma = re.search(r'(?m)^%s:\s+resb\s+(\d+)' % an, band)
    if not mc or not ma:
        print('nistruct: %s is declared in only one of the two languages' % cn)
        bad = 1
    elif int(mc.group(1)) != int(ma.group(1)):
        print('nistruct: %s is %s bytes in infones.c and %s in niband.inc'
              % (cn, mc.group(1), ma.group(1)))
        bad = 1

# the mode id: a NAME in the SDK, a BIT INDEX in the menu
ma = re.search(r'(?m)^FSXM_VGA13\s+equ\s+(\d+)', api)
mc = re.search(r'(?m)^#define\s+NI_FSXM_VGA13\s+(\d+)', menu)
if not ma or not mc:
    print('nistruct: FSXM_VGA13 is missing from apps/os88api.inc or nimenu.c')
    sys.exit(1)
if ma.group(1) != mc.group(1):
    print('nistruct: FSXM_VGA13 is %s in apps/os88api.inc and NI_FSXM_VGA13 is '
          '%s in nimenu.c - the greying would test the wrong bit'
          % (ma.group(1), mc.group(1)))
    bad = 1
if re.search(r'ni_fsx_caps\s*\([^)]*\)\s*&\s*\(\s*1\s*<<\s*\d', menu):
    print('nistruct: nimenu.c shifts by a LITERAL bit index - use '
          'NI_FSXM_VGA13, which this row checks against the SDK')
    bad = 1

if bad:
    sys.exit(1)
print('nistruct: struct ni_comp = %d NIC_* word(s), struct ni_mach = %d NIM_* '
      'word(s), two 16-byte palette LUTs, FSXM_VGA13 = %s in both languages'
      % (len(comp), len(mach), ma.group(1)))
NISTRUCT

# THE TOAST STRIP IS 24 CHARACTERS AND TRUNCATES IN SILENCE (kernel/toast.inc's
# TOAST_MAX = 24; toast_stage copies CX = TOAST_MAX with no ellipsis and no
# error). This package shipped a 37-character launch refusal that reached the
# glass as `INFONES wanted 13 KB, la` - the whole FACT cut off, on the one
# refusal with no state line behind it, because os88_main has no window yet.
# Every ni_say() therefore carries two strings, and this refuses a literal over
# the bound. The COMPOSED toasts (the heap refusal, the two `Header claims`
# sentences and the mapper's) are not literals and cannot be read here:
# hosttest/niuitest.c's os88_toast stub bounds those at run time, on the paths
# its steps exercise.
python3 - <<'PY'
import re, sys
TOAST_MAX = 24
bad = 0
n = 0
for f in ('infones.c', 'nippu.c', 'nimap.c', 'nirun.c', 'nipanel.c',
          'nimenu.c', 'nirom.c', 'nicmd.c', 'niabout.c'):
    src = open('apps/infones/' + f).read()
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    for m in re.finditer(r'ni_say\s*\(\s*"((?:[^"\\]|\\.)*)"\s*,'
                         r'\s*"((?:[^"\\]|\\.)*)"\s*\)', src):
        n += 1
        if len(m.group(2)) > TOAST_MAX:
            print('nitoast: %s: the toast [%s] is %d characters, over '
                  'TOAST_MAX = %d' % (f, m.group(2), len(m.group(2)),
                                      TOAST_MAX))
            bad = 1
    for m in re.finditer(r'os88_toast\s*\(\s*"((?:[^"\\]|\\.)*)"', src):
        n += 1
        if len(m.group(1)) > TOAST_MAX:
            print('nitoast: %s: the toast [%s] is %d characters, over '
                  'TOAST_MAX = %d' % (f, m.group(1), len(m.group(1)),
                                      TOAST_MAX))
            bad = 1
    if re.search(r'ni_say\s*\(\s*"(?:[^"\\]|\\.)*"\s*\)', src):
        print('nitoast: %s calls ni_say with ONE string: the state line is 40 '
              'cells and the toast is 24, and they are not the same sentence'
              % f)
        bad = 1
if bad:
    sys.exit(1)
print('nitoast: %d literal toast(s), every one <= %d characters'
      % (n, TOAST_MAX))
PY

# SPEC.md 91.10 SAYS ITS SENTENCES ARE THE PACKAGE'S OWN STRINGS, AND NOTHING
# CHECKED THAT HALF OF IT. The section's opening paragraph claims "the
# sentences below are the exact strings apps/infones/nipanel.c's ni_facts[]
# carries - the two are one table, checked by build.sh's nifact row", but the
# nifact row above reads nimenu.c and nipanel.c and never opens SPEC.md; and
# the table's last two rows are refusals rather than fact-line sentences, so
# nothing looked at them at all. That is how three spellings of one refusal
# shipped at once - the SPEC's, os88_main's and nirom.c's. This row reads the
# section itself: every ITALIC sentence in its table must exist in the sources,
# with `%u` treated as a wildcard, so a SPEC that quotes a sentence the package
# does not say fails the build.
python3 - <<'PY'
import re, sys
spec = open('SPEC.md').read()
i = spec.index('### 91.10 What is present and greyed')
sec = spec[i:spec.index('### 91.11', i)]

# The haystack is every string literal in the package AND every run of up to
# four CONSECUTIVE literals joined, because that is how these sentences are
# built: `os88_strcpy(ni_msg, "Header claims ")` / a number / `ni_app(ni_msg,
# "KB")` / `ni_app(ni_msg, ", file is ")` renders one sentence out of four
# literals, and a fragment of it straddles two of them.
lits = []
for f in ('infones.c', 'nippu.c', 'nimap.c', 'nirun.c', 'nipanel.c',
          'nimenu.c', 'nirom.c', 'nicmd.c', 'niabout.c'):
    src = open('apps/infones/' + f).read()
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    one = re.findall(r'"((?:[^"\\]|\\.)*)"', src)
    lits += one
    for i in range(len(one)):
        for k in (2, 3, 4):
            if i + k <= len(one):
                lits.append(''.join(one[i:i + k]))

quoted = []
for line in sec.splitlines():
    if not line.startswith('|'):
        continue
    for m in re.finditer(r'(?<![\w*])\*([^*|]{12,})\*(?![\w*])', line):
        quoted.append(m.group(1).strip())
if not quoted:
    print('nispec: 91.10 quotes no sentence at all - has the table moved?')
    sys.exit(1)

bad = 0
for q in quoted:
    for frag in [p for p in re.split(r'%[uds]', q) if p.strip()]:
        if not any(frag in s for s in lits):
            print('nispec: 91.10 quotes [%s], and no string in the package '
                  'contains [%s]' % (q, frag))
            bad = 1
if bad:
    sys.exit(1)
print('nispec: %d sentence(s) quoted by SPEC.md 91.10, every fragment of each '
      'found in the package\'s own literals' % len(quoted))
PY

# THE ROW-DROP TABLE'S ARITHMETIC MUST FIT SIXTEEN BITS (SPEC.md 91.6.2).
#
# `int` is sixteen bits on the target and thirty-two in hosttest/niuitest.c,
# so an expression that overflows on the 8086 is EXACTLY RIGHT on the host and
# no C harness can see it: `(i * 200) / 224` passes 32,767 at i = 164, the
# quotient went negative, the last sixty entries of the table were garbage,
# and thirty-six destination rows were never presented at all - a picture
# missing its bottom fifth with the mode's own border showing through. It was
# found on the glass. nirom.c's length check was the same defect one file
# along, and its comment says the answer in the same words: arithmetic that
# cannot overflow rather than a test that cannot see.
#
# So this reads ni_rowtab's body out of nirun.c, takes the multiply and the
# divide out of it, and checks BOTH that the largest product fits AND that the
# fraction is still 200/224. It refuses the expression rather than the value,
# which is what makes it survive a rewrite.
python3 - <<'PY'
import re, sys
src = open('apps/infones/nirun.c').read()
i = src.index('static void ni_rowtab(void)')
body = src[i:src.index('\n}', i)]
bad = 0

m = re.search(r'for\s*\(\s*i\s*=\s*0\s*;\s*i\s*<\s*(\d+)\s*;', body[body.index('0xFF;'):])
if not m:
    print('nirow: ni_rowtab has no second bound loop - has it been rewritten?')
    sys.exit(1)
n = int(m.group(1))

m = re.search(r'd\s*=\s*\(\s*i\s*\*\s*(\d+)\s*\)\s*/\s*(\d+)\s*;', body)
if not m:
    print('nirow: ni_rowtab no longer computes `d = (i * N) / M;` - the '
          '16-bit check cannot read it, and an unreadable check is not one')
    sys.exit(1)
num, den = int(m.group(1)), int(m.group(2))

top = (n - 1) * num
if top > 32767:
    print('nirow: (i * %d) reaches %d at i = %d, and `int` is SIXTEEN BITS on '
          'the target (SPEC.md 73.7): the quotient goes negative and the '
          'table\'s tail is garbage' % (num, top, n - 1))
    bad = 1
if num * 224 != 200 * den:
    print('nirow: the fraction is %d/%d and SPEC.md 91.6.2 says 200/224'
          % (num, den))
    bad = 1

# ...and the table itself, built here the way the target builds it
tab = [0xFF] * 240
last = -1
for i in range(n):
    d = (i * num) // den
    if d != last:
        tab[8 + i] = d
        last = d
kept = [d for d in tab if d != 0xFF]
if kept != list(range(200)):
    print('nirow: the table keeps %d row(s) and they are not 0..199 strictly '
          'increasing' % len(kept))
    bad = 1
for d in kept:
    if d * 320 + 32 + 256 > 64000:
        print('nirow: destination row %d would store past the 64,000-byte '
              'screen' % d)
        bad = 1
if bad:
    sys.exit(1)
print('nirow: (i * %d) / %d, largest product %d of 32,767; 200 rows, 0..199, '
      'every store inside 64,000 bytes' % (num, den, top))
PY
