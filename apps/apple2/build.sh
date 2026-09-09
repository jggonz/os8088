#!/bin/sh
# =============================================================================
# os8088 - apps/apple2/build.sh
#
# APPLE2's host checks (docs/APPLE2-SPEC.md section 16.6), run by `make
# apple2` BEFORE the target build, and each one stops it: a stale line, a
# double draw or a shadow that has stopped describing the glass costs a
# second on the target, and none of the three shows in an emulator
# (PERFORMANCE.md).
#
#     apps/apple2/build.sh       the checks only
#     make apple2                the checks, then the package
#     make apple2disk            ...and the four floppies
#
# **EVERY STEP FAILS RATHER THAN PASSING WHEN ITS SUBJECT IS ABSENT**
# (docs/APPLE2-PORT-PLAN.md Decision 12). That is why `a2ref.py --check` takes
# an EXPLICIT MODE LIST - wave 1 asks for `text` alone, and the wave that
# writes the lo-res and hi-res composers adds them - and why the a2_say walk
# below carries an expected MINIMUM: a corpus of zero literals would otherwise
# print green from wave 1 to the wave that adds the first message.
#
# THE CHECKS
#   tools/getapple2rom.py --check  the three fetched Apple II+ ROM images
#                                  against their SHA-256s, before anything
#                                  reads them (section 1.4). The CHECK only:
#                                  build/apple2-rom/APPLE2.ROM itself belongs
#                                  to the Makefile rule that names it
#   hosttest/a2uitest.c            the whole program over a stub os88.h with a
#                                  PIXEL model of the glass: after every step
#                                  the glass must show what the 7,680-byte
#                                  shadow says it shows, and the cost table is
#                                  printed in MILLISECONDS (section 7.9)
#   tools/a2ref.py --check         ...and the composed frames against an
#                                  INDEPENDENT compositor, bit for bit - FIVE
#                                  of them: text on each FLASH PHASE, which is
#                                  the same memory and different pixels, then
#                                  lo-res, hi-res and a MIXED screen, which is
#                                  both graphics composers and the text one in
#                                  one frame. Then
#                                  --selftest, which injects a one-bit defect
#                                  and requires the compare to FAIL: a check
#                                  that cannot fail is not a check. And
#                                  --romshape, which asserts the PINNED ROM's
#                                  measured 128 distinct bitmaps, so the
#                                  64-glyph decision is checkable rather than
#                                  remembered
#   hosttest/a2memtest.sh          a2mem.inc AND a2band.inc's string loops on
#                                  a real x86 with SS != DS and an ES
#                                  sentinel, in raw QEMU, with FOUR negative
#                                  controls (section 3.4)
#
# NOT here, because it takes minutes: hosttest/a2cputest.sh runs the 6502 core
# against Klaus Dormann's functional test and the eleven other rows of section
# 4.4 (`make a2cputest`) - the rcz80test precedent. It arrives with the core
# it gates, in wave 2.
#
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

# THE ROMS, BEFORE ANYTHING READS THEM. A ROM that is not the one the port was
# written against is a machine that boots to something else, and "it booted"
# is not the same claim as "it booted the machine this document describes".
python3 tools/getapple2rom.py --check
# ...and NOT `-o build/apple2-rom/APPLE2.ROM`. That file has a make rule of
# its own and is a prerequisite of build/apple2.o88 through the package's
# parts list, so writing it from the .apple2-hostchecks stamp recipe would
# re-make a downstream prerequisite behind make's back. The artefact has ONE
# owner: the Makefile.
python3 tools/a2ref.py --romshape $BUILD/apple2-rom/APPLE2.ROM

# The UI harness includes apple2.c itself, with apps/apple2/hosttest ahead of
# apps/cc on the include path so that its stub os88.h is the one that
# resolves. -w because the stubs deliberately ignore arguments; the checks are
# the point. -DA2_HOST is what keeps the cost counters OUT of the shipping
# image: nothing in apps/apple2/*.c reads one, the Makefile's smlrcc line
# never defines it, and this harness is their only reader.
$HOSTCC -O1 -w -DA2_HOST -I apps/apple2/hosttest -I apps/apple2 \
    -o $BUILD/a2uitest apps/apple2/hosttest/a2uitest.c
$BUILD/a2uitest

# ...and the frames it composed, against the independent compositor. TWO of
# them, one per FLASH PHASE: the same memory, different pixels, which is the
# one thing on the glass the damage model cannot see (section 7.6).
python3 tools/a2ref.py --check text $BUILD/a2ref-state.bin $BUILD/a2ref-frame.bin
python3 tools/a2ref.py --check text $BUILD/a2ref2-state.bin $BUILD/a2ref2-frame.bin
# ...AND THE OTHER TWO COMPOSERS, AND MIXED, WHICH IS BOTH OF THEM IN ONE
# FRAME. The mode list is EXPLICIT for the reason above: `--check lores` on a
# build whose lo-res composer does not exist is a failure and not a skip.
python3 tools/a2ref.py --check lores $BUILD/a2lores-state.bin $BUILD/a2lores-frame.bin
python3 tools/a2ref.py --check hires $BUILD/a2hires-state.bin $BUILD/a2hires-frame.bin
python3 tools/a2ref.py --check hires $BUILD/a2mixed-state.bin $BUILD/a2mixed-frame.bin
python3 tools/a2ref.py --selftest text $BUILD/a2ref-state.bin $BUILD/a2ref-frame.bin
python3 tools/a2ref.py --selftest hires $BUILD/a2hires-state.bin $BUILD/a2hires-frame.bin
# ...and the lo-res LUMINANCE LADDER over all 256 ordered pairs, against
# luminances a2ref.py derives from MII's `palettes[0]` "Color NTSC"
# (mii_emu src/mii_video.c:94-113) taken through MII's own lo-res mapping
# (src/mii_video.c:173-177) - a table the reference actually DISPLAYS, which
# AppleWin's `PaletteRGB_NTSC` lores block is not (its own first line says so
# and VideoInitializeOriginal overwrites it). apple2emu's `Lores_colors`
# (src/video.cpp:100-115) is the cross-check and agrees on every lit/dark
# decision. Its subject exists now, which is why this line does: a green pass
# over a table that is not there is exactly what these harnesses are built not
# to print.
python3 tools/a2ref.py --lumcheck $BUILD/a2lum.bin

# The movers and the composer's string loops, on a real x86 with SS != DS.
apps/apple2/hosttest/a2memtest.sh

# THE MESSAGE-LENGTH GATE'S LIST, TIED TO THE SOURCES (section 9). a2uitest.c
# walks a hand-typed msgs[] array and asserts every entry fits the status
# row's cells; nothing ties that array to the code, so an a2_say() added in a
# later wave and not copied in would simply NOT BE CHECKED - a gate that
# silently narrows instead of failing. This extracts every a2_say literal out
# of apps/apple2/*.c and requires the array to contain each one, AND requires
# the corpus to reach an explicit MINIMUM, so that "no literals at all" is a
# failure rather than a pass.
#
# ...AND IT WALKS THE COMPOSED MESSAGES TOO. ovl_a2_named("Loaded ", name)
# builds "Loaded <name>" into a buffer and hands a2_say a VARIABLE, so neither
# verb ever reached the corpus - a second route around a gate, added in the
# same wave that designed around the first one call along (a2cmd.c's "TWO
# CALLS AND NOT A TERNARY"). Every call site of BOTH must have a literal first
# argument, and a composed message is checked as verb + the 12 cells a FAT12
# name can be.
python3 - <<'PY'
import re, sys, glob
A2_SAY_MIN = 20
A2_ROW_CELLS = 26                           # the status row, a2uitest's bound
A2_NAME_MAX = 12                            # 8.3 with the dot
said = set()
bad = []
for f in glob.glob('apps/apple2/*.c'):
    src = open(f).read()
    for m in re.finditer(r'a2_say\(\s*"((?:[^"\\]|\\.)*)"', src):
        said.add(m.group(1))
    # EVERY call site of either, literal or not - a gate that only sees the
    # literals is a gate that stops looking the moment somebody composes one.
    # Comments are stripped first (they discuss both names), prototypes and
    # definitions are `(const char`, and a2_say(a2_progmsg) is THE ONE
    # composed call: it is ovl_a2_named's own tail, and what the gate checks
    # for it is the VERB at every call site of ovl_a2_named below.
    code = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    for m in re.finditer(r'(?<![A-Za-z0-9_])((?:ovl_)?a2_(?:say|named))'
                         r'\(\s*([A-Za-z0-9_"]*)', code):
        arg = m.group(2)
        if arg.startswith('"') or arg == 'const' or arg == 'a2_progmsg':
            continue
        bad.append('%s: %s(%s...) - the first argument is not a literal, so '
                   'the length gate cannot see it' % (f, m.group(1), arg))
    for m in re.finditer(r'ovl_a2_named\(\s*"((?:[^"\\]|\\.)*)"', code):
        verb = m.group(1)
        if len(verb) + A2_NAME_MAX > A2_ROW_CELLS:
            bad.append('%s: ovl_a2_named("%s", ...) is %d cells with a %d-char '
                       'name and the row is %d'
                       % (f, verb, len(verb) + A2_NAME_MAX, A2_NAME_MAX,
                          A2_ROW_CELLS))
# ...AND THE TOASTS, WHICH ARE A DIFFERENT ROW WITH A DIFFERENT BOUND.
# TOAST_MAX is 24 characters (kernel/toast.inc:85) and a longer one is
# TRUNCATED rather than refused, so `APPLE2: no APPLE2.OVL beside the program
# - the menu commands will refuse` reached the glass as
# `APPLE2: no APPLE2.OVL be` and the consequence was in the half that was cut.
# Nothing was checking, which is why it shipped.
TOAST_MAX = 24
# ...AND THE COMPOSED ONES, WHICH IS THE ARM THAT WAS MISSING. A literal-only
# walk cannot see os88_toast(line, 0), and `line` is exactly the message this
# wave found truncated at 24 characters - so the arm that mattered most was
# the one nothing checked, while the a2_say arm beside it already failed a
# non-literal outright. A composed toast is allowed only by NAME, with the
# bound its composer proves in its own header; anything else is `bad`.
TOAST_COMPOSED = {
    'a2_jamline': 18,       # `6502: JAM at $` + four hex digits (a2_jam)
    'line': 22,             # `APPLE2: 64K, ` + a 3-digit KB figure + `K free`
}                           #   - a2_refuse_kb's own arithmetic
for f in glob.glob('apps/apple2/*.c'):
    code = re.sub(r'/\*.*?\*/', ' ', open(f).read(), flags=re.S)
    for m in re.finditer(r'os88_toast\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)', code):
        lit = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
        if len(lit) > TOAST_MAX:
            bad.append('%s: os88_toast("%s") is %d characters and TOAST_MAX '
                       'is %d - the rest is silently cut off the glass'
                       % (f, lit, len(lit), TOAST_MAX))
    for m in re.finditer(r'os88_toast\(\s*([A-Za-z_][A-Za-z0-9_]*)', code):
        name = m.group(1)
        if name not in TOAST_COMPOSED:
            bad.append('%s: os88_toast(%s, ...) is composed and is not in '
                       'TOAST_COMPOSED - the length gate cannot see it, and '
                       'TOAST_MAX %d TRUNCATES rather than refusing'
                       % (f, name, TOAST_MAX))
        elif TOAST_COMPOSED[name] > TOAST_MAX:
            bad.append('%s: os88_toast(%s, ...) is bounded at %d and '
                       'TOAST_MAX is %d'
                       % (f, name, TOAST_COMPOSED[name], TOAST_MAX))
if bad:
    for b in bad:
        print('a2msgs: ' + b)
    sys.exit(1)
h = open('apps/apple2/hosttest/a2uitest.c').read()
i = h.index('static const char *msgs[] = {')
listed = set(re.findall(r'"((?:[^"\\]|\\.)*)"', h[i:h.index('};', i)]))
missing = sorted(said - listed)
if missing:
    for s in missing:
        print('a2msgs: a2_say("%s") is not in a2uitest.c\'s msgs[] - the '
              'length gate never sees it' % s)
    sys.exit(1)
if len(said) < A2_SAY_MIN:
    print('a2msgs: only %d a2_say literal(s) in apps/apple2/*.c, and the gate '
          'expects at least %d - a corpus this small is a gate that is not '
          'looking' % (len(said), A2_SAY_MIN))
    sys.exit(1)
print('a2msgs: %d a2_say literal(s), every one in the length gate, every '
      'a2_say/ovl_a2_named call site literal, and every os88_toast - literal '
      'or composed - inside TOAST_MAX' % len(said))
PY

# BOTH REDRAW PATHS CARRY A TIER TERM, WHICH IS THE ROW THAT WOULD HAVE
# CAUGHT WAVE 7's ONE REAL DEFECT. The windowed flush has been paced since
# wave 1 - at most once a host tick, and on the CPU_8086 tier at most once
# every SECOND tick, because a 496.8 ms repaint cannot keep up with a 55 ms
# one (APPLE2-SPEC 7.8). The FULLSCREEN bracket draws the same picture more
# expensively - a colour character row is 140.4 ms against the windowed row's
# 26.4 - and shipped with the "did anything change" gate COPIED and the
# pacing term left behind, so on the one tier that wave had just measured it
# issued a frame between every 256-cycle slice (APPLE2-SPEC 13.2). Nothing in
# the toolchain could see it: both paths compile, both draw the right pixels,
# and the difference is only in how much emulated machine runs between them.
#
# It is a TEXT gate and it says so: the harness cannot reach this loop's
# pacing (a2uitest drives the bracket with the 6502 stopped, so no producer
# marks a line between iterations and neither arm composes a second frame),
# and no emulator in this tree can host the arm at all - it needs a CPU_8086
# machine with a foreign-mode-capable VGA, which section 16.4.1 shows does not
# exist here. What this row refuses is the SHAPE going away.
python3 - <<'PY' || exit 1
import re, sys
bad = []
want = [
    ('apps/apple2/apple2.c', 'os88_onwake', ['a2_tier_slow', 'a2_fltick'],
     'the windowed flush'),
    ('apps/apple2/a2scr.c', 'a2_fsx_main', ['a2_tier_slow', 'a2_fsx_tick'],
     'the fullscreen colour bracket'),
]
for path, fn, terms, what in want:
    code = re.sub(r'/\*.*?\*/', ' ', open(path).read(), flags=re.S)
    # the DEFINITION and not the forward declaration: the last mention that
    # is followed by a brace rather than a semicolon
    i = -1
    for m in re.finditer(r'(?<![A-Za-z0-9_])' + fn + r'\s*\([^;{]*\)\s*\{',
                         code):
        i = m.start()
    if i < 0:
        bad.append('%s: %s has no definition in this file' % (path, fn))
        continue
    body = code[i:i + 8000]
    for t in terms:
        if t not in body:
            bad.append('%s: %s does not mention %s - %s has to be PACED by '
                       'tier and not only gated on "did anything change" '
                       '(APPLE2-SPEC 7.8 and 13.2)' % (path, fn, t, what))
# ...and the stamp, without which the foreign term reads a word nothing writes
code = re.sub(r'/\*.*?\*/', ' ', open('apps/apple2/a2scr.c').read(), flags=re.S)
if not re.search(r'a2_fsx_tick\s*=\s*os88_ticks\(\)', code):
    bad.append('apps/apple2/a2scr.c: nothing stamps a2_fsx_tick, so the '
               'pacing term above compares against a word that never moves')
if bad:
    for b in bad:
        print('a2pace: ' + b)
    sys.exit(1)
print('a2pace: both redraw paths are gated AND paced - a2_flush on '
      'a2_fltick, a2_fsx_main on a2_fsx_tick, each with a2_tier_slow in it')
PY

# THE SHARED SDK'S OWN TOASTS ARE GATED TOO, AND NOT FROM HERE. The three
# overlay refusals apps/cc/crt0.asm raises are assembled from CC_PKG_NAME and
# are therefore a different length in every C package that includes the SDK;
# all three were over TOAST_MAX and were being cut off the glass in every one
# of them - `APPLE2.OVL is not on this disk` reaching the reader as
# `APPLE2.OVL is not on thi` - and nothing in the tree was looking, which is
# how a defect in the SDK outlived seven packages. This wave found it here and
# the check was written here first, WHICH WAS THE WRONG HOME: this script runs
# only for `make apple2`, an on-demand C target outside `all` and outside
# `make test-full`, so lengthening a literal in crt0.asm or adding a C package
# with a long name would not have run the gate that exists to catch exactly
# that. It is a row of tests/unit/t_mirror.py instead - fast tier, every
# `make` - reading the literals out of crt0.asm, TOAST_MAX out of
# kernel/toast.inc and the name cap out of crt0.asm's own `%fatal`. See
# docs/APPLE2-SPEC.md section 17.3.
