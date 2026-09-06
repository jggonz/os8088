#!/usr/bin/env python3
"""Prove no call crosses a segment boundary inside the kernel as a NEAR call.

`.ovl` and `.cold` each have their own `vstart`, so a near call between one
of them and `.text` assembles
without complaint and emits a displacement computed between two different
address spaces.  Nothing catches that: not NASM, not the linker (there isn't
one), and not a boot on the one machine whose rung QEMU can emulate.  This
walks every `section` block in kernel/ and refuses any near control transfer
whose target label lives in a different address space: call, jmp (near and
short spellings included - `short` was once swallowed by the regex as if it
were the label, which silently exempted every `jmp short`), and the
conditional branches and loops.  Local labels (.foo) bind to their parent
and cannot cross, so they fall out of the label map untested, which is
correct.

It also knows the `OSAPI_*` cell macros, whose argument IS a call site: the
`call` lives in the macro body as `call %1`, so a plain scan of the source
sees `OSAPI_SLOT dskw_dfree` as no call at all.  Six of those pointed into
the file modules the day they went cold and not one would have been
reported.  A new cell macro that near-calls its argument belongs in CELL
below.

What it CANNOT see, by construction: an indirect transfer (`call bx`,
`jmp [table]`) and a code pointer stored in data.  Those stay a review rule:
a table of `.cold` pointers may live in `.text` only if cold code alone
dispatches through it.  There are four - ctrl.inc's page table, and
files.inc's `fm_jmp` plus the two `fm_ctx_*` descriptor sets, all three
reached only from `fm_docmd` / `fm_rclick`, which are themselves cold.  The
mirror of that rule is what a build cannot catch either: a table in `.text`
that `.text` DOES dispatch through must name the resident thunk and not the
`_x` body, which is how `fm_tpl` and `fm_menus` are written.

Run it from `make`; it is worth more than any amount of reading.
"""
import os, re, sys, glob

CALL = re.compile(r'\b(?:call|jmp|j[a-z]{1,3}|loop[a-z]{0,2})\s+'
                  r'(?:(?:near|short)\s+)?(?:(\w+):)?([A-Za-z_]\w*)\b')
# an API cell macro whose body near-calls its LAST argument
CELL = re.compile(r'^\s*OSAPI_(?:SLOT|JSLOT|NSTUB|XSTUB)\s+(?:\w+\s*,\s*)?'
                  r'([A-Za-z_]\w*)\s*(?:,\s*\d+\s*)?$')
# ...and the two-or-three-argument cells DEFINE their first argument, as `%1:`
# inside the macro body.  A `name:` scan cannot see that, so the 45 OSAPI_JSLOT
# targets were not merely untested above - they were not in the label map at
# all, which is how adding JSLOT alone would have bought nothing.
CELLDEF = re.compile(r'^\s*OSAPI_(?:NSTUB|XSTUB)\s+([A-Za-z_]\w*)\s*,')
MODS = ('.modc', '.modf', '.modl', '.modh', '.modp', '.modd')  # module images (2.8).
# `.modp` is Cut/Copy/Paste and kern_small's ALONE (SPEC.md 22.3,
# docs/KERN-SMALL-MODULE-SPLIT.md 9.2): filecp.inc emits its bodies there on
# that build and into `.cold` on kern_big, which is the first conditional
# `section` in the tree. This scanner reads SOURCE and cannot evaluate the
# %ifdef, so it files those bodies as `.modp` on both - which is why filecp.inc
# now carries exactly ONE such switch and every call that leaves the image goes
# through its FCPX/FCPXJ macros. A near call inside the body is then
# `.modp -> .modp` and true on either build. `.modd` is fdlg.inc on the same
# terms (SPEC.md 38.0) and obeys the same three rules.
# `.modh` is hiber.inc's HIBER.DRV (SPEC.md 87) - a stub on kern_small - and
# was missing from this list when it shipped, so every label in it filed as
# `.text` and a near call from the module into the kernel passed in silence.
# **A section added here and nowhere else is a section NOTHING below checks**,
# which is how `.modl` shipped once with the near-call check blind to it - the
# clone module's every `call COLD_SEG:` was correct, and would not have been
# reported had one been near.
FAR = ('.boot2', '.ovl', '.ovlw', '.cold') + MODS  # sections with a vstart
# `.ovlw` is the boot overlay's OTHER half (SPEC.md 2.5.3): the bodies that are
# dead at the first mount rather than at spl_finish, landing on FAT_SEG off the
# kernel's own read.  It has a vstart of its own for `.ovl`'s reason and every
# rule below that names one names both.


# A file the kernel %includes from OUTSIDE kernel/, and the section its
# contents therefore land in.  apps/os88ui.inc is the shared button and glyph
# (SPEC.md 20.5.1): one source for two worlds, %included by fdlg.inc from
# inside a `.cold` block, so every label in it is cold - and a NEAR call to
# one from another address space is the exact bug this file exists to refuse.
#
# It was not scanned at all until an on-demand module (SPEC.md 2.8) near-called
# os88ui_glyph from `.modc`.  The label was not in the map, so the call was
# untested rather than reported, and the Control Panel painted itself and then
# ran off the end of its own image into whatever was above it.  A file that
# emits code into the kernel belongs here whatever directory it is in.
EXTRA = {'apps/os88ui.inc': '.cold',
         # ...and SPEC.md 2.9's stage 2, which is %included into `.boot2` from
         # kernel.asm and lives in boot/. It carries no `section` of its own -
         # one would displace it from file offset 0 - so without this line
         # every label in it would be filed as `.text` and a near call out of
         # it would go unreported, which is the exact bug this file exists for.
         'boot/boot2.asm': '.boot2',
         # ...and the loading screen, which joined it (SPEC.md 2.9.4). Like
         # clockw.inc below it carries no `section` of its own - kernel.asm
         # wraps the %include - so without this line every label in it files
         # as `.text` and stage 2's own near calls into it are reported as
         # crossings that are not, while a real crossing OUT of it would not
         # be reported at all.
         'kernel/splash.inc': '.boot2',
         # ...and one that IS under kernel/, so the glob below finds it too:
         # clockw.inc is SPEC.md 37.94's RTC write half, %included from
         # ctrl.inc's `.modc`. It carries no `section` of its own (one would
         # push modc_hdr off offset 0), so without this line every label in it
         # would be filed as `.text` and every far call out of it reported as
         # a crossing that is not one - and, worse, a NEAR call out of it
         # would not be reported at all.
         'kernel/clockw.inc': '.modc'}


# THE SECTION WALK, DONE ONCE PER FILE AND KEPT.  Every check below wants the
# same (section, line) sequence, and there are eighteen loops over it: walking
# the file per check is the same parse eighteen times, which measured as 1.86M
# line-parses over 110,920 lines of kernel and 78% of this tool's run time
# (2.16s -> 1.00s, and every `make` pays it once).
# The cache is keyed on the path and never invalidated, which is correct for a
# process that reads each file and exits; a caller that edits kernel/ and asks
# again inside one process would get the old answer, and there is none.
#
# `section` as a SUBSTRING before the regex is the second half of it: a line
# matching SECT must contain the word, and almost none of them do, so the
# test that costs a byte scan replaces one that costs a regex.
SECT = re.compile(r'^\s*section\s+(\.\w+)')
_WALK = {}

# ...and the rest of the per-line patterns, compiled once for the same reason.
# `re.match(r'...')` is not free: it is a cache lookup and two function calls
# per line per check, and there are 564k of them left after the walk above.
# The ones built with `%` interpolation further down are deliberately NOT
# here - re's own cache is what serves a pattern whose text is not a constant.
MACRO_D = re.compile(r'^\s*%macro\s+(\w+)')
MACRO_B = re.compile(r'^\s*%macro\b')
ENDMACRO = re.compile(r'^\s*%endmacro')
ENDMACRO_B = re.compile(r'^\s*%endmacro\b')
LABEL = re.compile(r'^([A-Za-z_]\w*):')
LABEL_DOT = re.compile(r'^[A-Za-z_.]\w*:')
DRVBOOT = re.compile(r'^\s*OVL(?:GATE1?|CALL)\s+drv_boot_x\b')
OVWCALL = re.compile(r'\bOVWCALL\s+(\w+)')
CSMEM = re.compile(r'\[[^]]*\bcs\s*:')
RESERVE = re.compile(r'^\s*([A-Za-z_]\w*)\s*:?\s*(?:res[bwdqt])\b')
WORD = re.compile(r'\b\w+\b')
# A label a MACRO PASTES TOGETHER - `%1 %+ _pack:` in driver.inc's CFG_DATA,
# CFG_SHARED, CFG_BOOT and CFG_SAVE - is `ovc_pack` or `cpc_pack` once
# expanded, and a `name:` scan never sees it: it entered no map (where, half,
# mdata, rets), so a near call into `ovc_load` from `.text` was reported by
# NOTHING. driver.inc's own comment above ovl_cfg_load says as much and routes
# the one crossing through a plain label to give the gate something to look
# at. So a macro body is read as a TEMPLATE - which suffixes it defines,
# whether each is data, what each returns - and every expansion line
# (`CFG_SHARED ovc`) adds the pasted names to the maps, in the section the
# EXPANSION is in, which is the only section that means anything for them.
TLABEL = re.compile(r'^\s*%1\s*%\+\s*(\w+):(.*)$')
TDATA = re.compile(r'^\s*(?:times\s+\S+\s+)?(?:d[bwdq]|res[bwdqt])\b', re.I)
TRET = re.compile(r'^\s*(ret|retf|retn|iret)\b', re.I)
TLADJ = re.compile(r'^\s*jmp\s+(kretf?c?_[a-z]{2})\s*$', re.I)
TEXP = re.compile(r'^\s*(\w+)\s+([A-Za-z_]\w*)\s*$')


def _walk(path):
    """[(section, line-number, stripped-line, raw-line)] for the whole file."""
    got = _WALK.get(path)
    if got is None:
        got = []
        cur = EXTRA.get(path, '.text')
        for n, raw in enumerate(open(path), 1):
            line = raw.split(';')[0]
            if 'section' in line:
                m = SECT.match(line)
                if m:
                    cur = m.group(1)
                    continue
            got.append((cur, n, line, raw))
        _WALK[path] = got
    return got


def sections(path):
    """yield (section, line-number, source-line) with comments stripped"""
    for cur, n, line, _raw in _walk(path):
        yield cur, n, line


def sections_raw(path):
    """...and the same walk with the RAW line beside the stripped one.

    The `; ovlchk: DS = ...` markers below live in COMMENTS, which sections()
    throws away before anything can see them - so the one check that needs
    both halves takes the raw line as a fourth field rather than being given a
    flag.  It is the SAME walk underneath: _walk keeps both, and which fields
    a caller unpacks is the whole of the difference between these two.
    """
    return iter(_walk(path))


def main():
    # glob yields the pattern's own separator, which is a backslash on
    # Windows - and then the forward-slash keys in EXTRA never match, so
    # clockw.inc / splash.inc file as .text and 42 far calls out of them read
    # as near crossings that are not. Normalise to '/'; open() takes it fine.
    kfiles = sorted(p.replace('\\', '/')
                    for p in glob.glob('kernel/*.inc')) + ['kernel/kernel.asm']
    # dedup: an EXTRA under kernel/ is already in the glob, and scanning it
    # twice reports every finding in it twice.
    files = kfiles + [f for f in sorted(EXTRA) if f not in kfiles]
    where = {}                       # label -> section it is defined in
    mbody = {}                       # %macro -> the labels its body near-calls
    tmpl = {}                        # %macro -> [(suffix, is_data, ret kinds)]
    for f in files:
        macro = None
        tcur = None                  # the `%1 %+ _x:` template label open now
        for sect, n, line in sections(f):
            m = MACRO_D.match(line)
            if m:
                macro = m.group(1)
                mbody.setdefault(macro, set())
                tmpl.setdefault(macro, [])
                tcur = None
                continue
            if ENDMACRO.match(line):
                macro = None
                tcur = None
            elif macro:
                for mm in CALL.finditer(line):
                    if mm.group(1) is None:
                        mbody[macro].add(mm.group(2))
                t = TLABEL.match(line)
                if t:
                    # [suffix, is_data, ret kinds, undecided]: a label with
                    # its data on the SAME line (`%1 %+ _sig: db ...`) is
                    # decided here; a bare one (`%1 %+ _keys:` then `db`) by
                    # the first line under it.
                    rest = t.group(2).strip()
                    tcur = [t.group(1), bool(TDATA.match(rest)), set(),
                            not rest]
                    tmpl[macro].append(tcur)
                elif tcur is not None:
                    if tcur[3] and line.strip():
                        tcur[1], tcur[3] = bool(TDATA.match(line)), False
                    r = TRET.match(line)
                    if r:
                        tcur[2].add(r.group(1).lower())
                    l = TLADJ.match(line)
                    if l:
                        tcur[2].add('retf' if l.group(1).lower()
                                    .startswith('kretfc_') else 'ret')
            m = LABEL.match(line)
            if m:
                where[m.group(1)] = sect
            m = CELLDEF.match(line)
            if m:
                where[m.group(1)] = sect
    # ...and a %macro whose BODY holds a near transfer to a fixed label makes
    # every expansion site a call site that no textual scan can see.  `MARK 42`
    # is not a call; `call mark_here` is, three thousand lines away in the
    # macro.  Eight MARK sites sit inside mouse_init, so a section move that
    # takes mouse_init cold takes eight invisible near calls with it - and
    # `bootmark` being in the build matrix does NOT cover it, because a near
    # call between two vstart=0 sections assembles perfectly.  Collected from
    # the source above rather than hand-listed, so a new macro is covered the
    # day it is written; %%local labels start with `%` and fall out by
    # themselves, and anything that is not a known label is dropped here.
    mbody = {k: (v & set(where)) for k, v in mbody.items()}
    mbody = {k: v for k, v in mbody.items() if v}
    MEXP = re.compile(r'^\s*(\w+)\b')

    # ...and the PASTED labels (TLABEL above): every expansion of a macro that
    # defines `%1 %+ _x:` puts `<arg>_x` in the expansion's section. `where`
    # takes them now; `half`, `mdata` and `rets` take theirs where each is
    # built. A definition seen twice (CFG_DATA ovc, CFG_DATA cpc) is two
    # different names, which is the whole point of the paste.
    tmpl = {k: v for k, v in tmpl.items() if v}
    pasted = {}                      # 'ovc_pack' -> (section, is_data, kinds)
    for f in files:
        inmacro = False
        for sect, n, line in sections(f):
            if MACRO_B.match(line):
                inmacro = True
                continue
            if ENDMACRO_B.match(line):
                inmacro = False
                continue
            if inmacro:
                continue
            m = TEXP.match(line)
            if m and m.group(1) in tmpl:
                for suffix, is_data, kinds, _u in tmpl[m.group(1)]:
                    # the suffix carries its own underscore: `%1 %+ _load`
                    pasted[m.group(2) + suffix] = (sect, is_data, kinds)
    for lab, (sect, _d, _k) in pasted.items():
        where[lab] = sect

    bad = []
    for f in files:
        inmacro = False
        for sect, n, line in sections(f):
            # A %macro BODY is not a call site; its EXPANSIONS are, and mbody
            # above is what makes each of those visible. Scanning the body
            # itself reports the DEFINITION's section, which is wherever the
            # macro happens to be written - so a macro defined beside a file's
            # constants and expanded only inside a module image was reported as
            # a `.text -> .modc` crossing that does not exist, and the only way
            # to silence it was to write the definition inside a `section` it
            # emits nothing into. That is a false positive with a workaround
            # attached, which is the worst kind.
            if MACRO_B.match(line):
                inmacro = True
                continue
            if ENDMACRO_B.match(line):
                inmacro = False
                continue
            if inmacro:
                continue
            hits = [(m.group(1), m.group(2)) for m in CALL.finditer(line)]
            m = MEXP.match(line)
            if m and m.group(1) in mbody:
                hits += [(None, t) for t in mbody[m.group(1)]]
            m = CELL.match(line)
            if m:
                hits.append((None, m.group(1)))
            for seg, tgt in hits:
                tsect = where.get(tgt)
                if tsect is None:
                    continue
                a = sect if sect in FAR else '.text'
                b = tsect if tsect in FAR else '.text'
                if a != b and seg is None:
                    bad.append((f, n, '%s -> %s, near' % (a, b), tgt))
    for f, n, why, tgt in bad:
        print("%s:%d: %s: %s" % (f, n, why, tgt), file=sys.stderr)
    if bad:
        sys.exit("os88ovlchk: %d call(s) cross a segment boundary near" % len(bad))
    print("os88ovlchk: no near call crosses a segment boundary")

    # --- and the OTHER half of SPEC.md 2.6: nothing may assume CS ------------
    # Rule 1 puts cold code's DATA in .text, so cold code has no data of its
    # own to reach - which makes "CS is mentioned in .cold" a clean invariant
    # rather than a heuristic, and the whole kernel satisfies it.
    #
    # It is worth a refusal because the failure is silent in a way the check
    # above is not: `mov ax, cs` meaning "the kernel segment" assembles, and
    # the loop that breaks it is a segment LOAD rather than a control
    # transfer, so nothing here saw it. Cold, CS is COLD_SEG - dsk_copy_in
    # staged a boot sector into the middle of the cold segment and every BPB
    # field stayed 0, which surfaced as "No os8088 disk (A:)" on a good
    # floppy. Name the segment (`mov ax, KERNEL_SEG`) and this stays quiet.
    #
    # .ovl is deliberately NOT checked: the overlay's data rides WITH it, so
    # reaching it through CS is the correct idiom there and two places use it
    # (font.inc's glyph copy does `push cs / pop ds`, drv_snd_sniff uses
    # `cs lodsw`).
    #
    # **AND NEITHER ARE THE MODULE SECTIONS, SINCE SPEC.md 2.8.6.** They used
    # to be, on rule 1's premise that a module carries no data - and when that
    # premise was true the check was exact. A module may now carry its own
    # STRINGS and read them through CS, which is what took the cloner's and
    # the formatter's prompts out of the kernel entirely, so `[cs:si]` in a
    # `.mod*` section is the correct idiom exactly as it is in `.ovl`. What is
    # lost with it is the guard against `mov ax, cs` meaning KERNEL_SEG inside
    # a module; that is now a review rule, and the cheap half of it is the
    # `lods` refusal further down.
    CS = re.compile(r'\b(?:push\s+cs'
                    r'|mov\s+(?:\w+|(?:(?:byte|word|dword)\s+)?\[[^\]]*\])'
                    r'\s*,\s*cs'
                    r'|cs\s*:'
                    r'|cs\s+(?:lods|movs|stos|scas))', re.I)
    # SCOPED TO kernel/, and EXTRA's files are deliberately left out. A
    # one-source-two-worlds include (apps/os88ui.inc, SPEC.md 20.5.1) is half
    # package and half kernel behind `%ifdef OS88UI_KERNEL`, and this scanner
    # does not evaluate the preprocessor - so the PACKAGE half's `os88ui_armw:
    # dw 0`, which never reaches the kernel at all, reads here as data in
    # .cold. The near-call check above still needs the file, and needs it
    # badly: it is where eleven real crossings hid (SPEC.md 2.8). What these
    # two checks ask - where does THIS file's code land - is the question a
    # dual-world include does not have one answer to.
    cs_bad = []
    for f in kfiles:
        for sect, n, line in sections(f):
            if sect == '.cold' and CS.search(line):
                cs_bad.append((f, n, line.strip()[:60]))
    for f, n, src in cs_bad:
        print("%s:%d: .cold assumes CS: %s" % (f, n, src), file=sys.stderr)
    if cs_bad:
        sys.exit("os88ovlchk: %d CS assumption(s) in .cold - SPEC.md 2.6 "
                 "rule 2 (name the segment)" % len(cs_bad))
    print("os88ovlchk: no .cold code assumes CS")

    # --- rule 2b: `.ovl` code may not STORE CS ------------------------------
    # `.ovl` rides inside the boot blob and is GIVEN BACK TO THE HEAP at
    # spl_finish (kernel.asm, "It costs no RAM after that at all"). That is the
    # whole reason boot-only code is worth putting there - it stops costing
    # anything the moment the desktop is up - and it is also the trap.
    #
    # READING through CS in `.ovl` is correct and stays allowed, for the reason
    # the block above gives: the overlay's data rides with it. What can never be
    # right is WRITING CS somewhere - an interrupt vector, a far pointer, a saved
    # segment word. The value stored is the blob's segment, and the blob is
    # about to be handed to the heap and written over by the first claim. The
    # store succeeds, the boot finishes, and the machine dies later at a vector
    # that now points into somebody's buffer.
    #
    # This is not hypothetical. mouse_init installs the mouse and int 09h
    # vectors with `mov [es:si+2], cs`, and moving its boot half into `.ovl` is
    # exactly what SPEC.md 9.4's overlay move does. Three such sites are inside
    # the moved range. Before this rule they were guarded by NOTHING: the CS
    # check above is scoped to `.cold`, `.ovl` was deliberately exempt from it,
    # and nasm is happy - so the reviewer's own note that "the gate refuses it"
    # was false, and was demonstrated false by reverting the fix and watching
    # both the stock and the patched gate pass in silence.
    #
    # WHAT THIS CANNOT SEE, stated so nobody trusts it further than it goes: a
    # store laundered through a register (`mov ax, cs` ... `mov [foo], ax`) is
    # invisible here, and so is a `push cs` whose value is popped into a far
    # frame. Those stay a review rule. What is caught is the direct form, which
    # is the one that is written by hand and the one that has actually shipped.
    OVLCS = re.compile(r'\bmov\s+(?:(?:byte|word|dword)\s+)?\[[^\]]*\]'
                       r'\s*,\s*cs\b', re.I)
    ovlcs_bad = []
    for f in kfiles:
        for sect, n, line in sections(f):
            if sect in ('.ovl', '.ovlw') and OVLCS.search(line):
                ovlcs_bad.append((f, n, sect, line.strip()[:60]))
    for f, n, sect, src_ in ovlcs_bad:
        print("%s:%d: %s stores CS: %s" % (f, n, sect, src_), file=sys.stderr)
    if ovlcs_bad:
        sys.exit("os88ovlchk: %d CS store(s) in the boot overlay - both halves "
                 "are given back (.ovl at spl_finish, .ovlw at the first "
                 "mount), so the stored segment becomes somebody else's memory "
                 "(name the segment instead)" % len(ovlcs_bad))
    print("os88ovlchk: no boot-overlay code stores CS")

    # --- rule 2c: every reference INTO `.ovl` is registered ------------------
    # The rule above stops `.ovl` publishing its own segment. This one stops the
    # opposite mistake, which is quieter and which nothing in this tree caught
    # before: somebody adds a call to a body that lives in the overlay, from
    # code that runs AFTER the overlay is gone.
    #
    # `.ovl` is released at spl_finish. A body there is correct exactly as long
    # as every path that reaches it runs during boot. That is not a property the
    # assembler can check, it is not a property a screenshot shows, and the
    # failure is silent: the call lands in whatever the heap handed out, so the
    # routine "works" until the machine is busy enough for the claim underneath
    # it to be something else.
    #
    # `dispcold` byte-compares `.cold` and has no `.ovl` counterpart. This is it,
    # in the only form that is cheap and exact: a REGISTRY, the way SPEC.md
    # 6.6's transparent-text sites are a registry. Every reference into `.ovl`
    # from outside it is listed in tests/ovlrefs.txt with the reason it is
    # boot-only. A new one fails the build until somebody writes that reason
    # down, and the list can be read in one sitting to audit the whole surface.
    #
    # The list may shrink freely. It grows only by a deliberate edit, and the
    # question that edit has to answer is the only question that matters here:
    # WHAT GUARANTEES THIS RUNS BEFORE spl_finish?
    #
    # ...OR BEFORE THE FIRST MOUNT, which is the other half of it since SPEC.md
    # 2.5.3 split the overlay by lifetime. `.ovl` rides in the blob and lives to
    # spl_finish; `.ovlw` lands on FAT_SEG and is forfeit the moment drv_boot
    # mounts a volume, which is EARLIER. So a symbol's deadline depends on which
    # half it is in, and the error below says which - "it looks like boot code"
    # was never an answer and is now not even a well-formed one.
    #
    # THE DIRECTION MATTERS, and it is the new way to get this wrong.
    #
    #   .ovlw -> .ovl    always safe: the callee outlives the caller.
    #   .ovl  -> .ovlw   NOT safe by construction. The blob half is still there
    #                    after the mount and the window half is not, so a call
    #                    the other way is exactly the silent failure this rule
    #                    exists for - and it is INSIDE the overlay, which is
    #                    where the old rule stopped looking.
    #
    # So `.ovlw` is exempt as a SOURCE only when the target is `.ovl`, and every
    # other crossing is registered.
    OVLREG = os.path.join(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))), 'tests', 'ovlrefs.txt')
    registered = set()
    if os.path.exists(OVLREG):
        for line in open(OVLREG):
            line = line.split('#', 1)[0].strip()
            if line:
                registered.add(line.split()[0])

    half = {}                       # label -> '.ovl' | '.ovlw'
    for f in kfiles:
        for sect, n, line in sections(f):
            if sect in ('.ovl', '.ovlw'):
                m = LABEL.match(line)
                if m:
                    half[m.group(1)] = sect
    for lab, (sect, _d, _k) in pasted.items():
        if sect in ('.ovl', '.ovlw'):
            half[lab] = sect         # the ovc_ family, pasted into .ovl

    DEADLINE = {'.ovl': 'spl_finish', '.ovlw': "drv_boot's FIRST MOUNT"}
    ovl_refs, unregistered = set(), []
    if half:
        pat = re.compile(r'\b(' + '|'.join(sorted(map(re.escape, half))) + r')\b')
        for f in kfiles:
            for sect, n, line in sections(f):
                code = line.split(';', 1)[0]
                for m in pat.finditer(code):
                    sym = m.group(1)
                    tgt = half[sym]
                    if sect == tgt:
                        continue            # within one half: near, and fine
                    if sect == '.ovlw' and tgt == '.ovl':
                        continue            # the callee outlives the caller
                    ovl_refs.add(sym)
                    if sym not in registered:
                        unregistered.append((f, n, sect, sym, tgt,
                                             code.strip()[:52]))
    for f, n, sect, sym, tgt, src_ in unregistered:
        print("%s:%d: unregistered reference from %s into %s: %s   %s"
              % (f, n, sect or '(resident)', tgt, sym, src_), file=sys.stderr)
    if unregistered:
        deadlines = sorted({DEADLINE[t] for _, _, _, _, t, _ in unregistered})
        sys.exit("os88ovlchk: %d reference(s) into the boot overlay are not in "
                 "tests/ovlrefs.txt - each one must say what guarantees it runs "
                 "before %s" % (len(unregistered), ' / '.join(deadlines)))
    stale = sorted(registered - ovl_refs)
    if stale:
        sys.exit("os88ovlchk: tests/ovlrefs.txt lists %d reference(s) that no "
                 "longer exist (%s) - the registry may only shrink by deleting "
                 "the row with the code" % (len(stale), ', '.join(stale[:4])))
    nw = sum(1 for v in half.values() if v == '.ovlw')
    print("os88ovlchk: every reference into the boot overlay is registered "
          "(%d of %d labels; %d in .ovl to spl_finish, %d in .ovlw to the "
          "first mount)" % (len(ovl_refs), len(half), len(half) - nw, nw))

    # --- rule 2d: the CALL MACRO has to match the HALF ----------------------
    # Rule 2c asks whether a reference is registered. It does not ask whether
    # the call will ARRIVE, and after SPEC.md 2.5.3 those are different
    # questions: the blob half is reached through [spl_fseg], the window half
    # by `call FAT_SEG:`, and a body registered perfectly well can still be
    # called through the wrong one.
    #
    # What that does is not a refusal, it is a far call to the BLOB's segment
    # carrying a WINDOW offset - so the machine lands wherever that offset
    # falls inside the loading screen, executes it, and returns through a
    # stack it has already ruined. Measured, the first time it happened: a
    # spin at HEAP_SEG:36D2 with SP 53,482 bytes past task 0's stack, a screen
    # of garbage, and no message from anything.
    #
    # It is a one-line mistake to make - `dsk_flop_add: OVLGATE1 dsk_flop_add_x`
    # survived the sweep that converted the other twenty-three sites because
    # the macro shared its line with a label - so it is checked rather than
    # reviewed.
    MACHALF = {'SPLCALL': '.ovl', 'OVLCALL': '.ovl', 'OVLCALLC': '.ovl',
               'OVLGATE': '.ovl', 'OVLGATE1': '.ovl', 'SPLSTUB': '.ovl',
               'SPLGATE': '.ovl', 'SPLGATE1': '.ovl', 'OVWCALL': '.ovlw'}
    MACPAT = re.compile(r'\b(' + '|'.join(MACHALF) + r')\s+(\w+)')
    REACH = {'.ovl': 'the blob, through [spl_fseg]',
             '.ovlw': 'the FAT window, by `call FAT_SEG:`'}
    macbad = []
    for f in kfiles:
        for sect, n, line in sections(f):
            m = MACPAT.search(line.split(';', 1)[0])
            if not m:
                continue
            macro, tgt = m.group(1), m.group(2)
            want, got = MACHALF[macro], half.get(tgt)
            if got and got != want:
                macbad.append((f, n, macro, tgt, want, got))
    for f, n, macro, tgt, want, got in macbad:
        print("%s:%d: %s reaches %s, but %s is in %s"
              % (f, n, macro, REACH[want], tgt, got), file=sys.stderr)
    if macbad:
        sys.exit("os88ovlchk: %d call(s) into the boot overlay use the wrong "
                 "half's entry - the segment and the offset would come from "
                 "different sections (SPEC.md 2.5.3)" % len(macbad))
    print("os88ovlchk: every overlay call matches its target's half")

    # --- rule 2e: nothing in the WINDOW half is called after the mount ------
    # Rule 2d asks whether a call arrives. This asks whether it arrives IN
    # TIME, for the one ordering the tree actually writes down: `kmain` calls
    # `drv_boot_x`, which mounts a volume, and the mount takes the FAT window
    # and the buffers above it - so every `.ovlw` byte is gone from that line
    # onward. Any OVWCALL below it is a call into a FAT table.
    #
    # THIS IS THE ONE THAT COST A BOOT. `xm_boot_x` is boot-only and was
    # registered as such, correctly, for the deadline the registry used to
    # have: it runs before spl_finish. It runs AFTER drv_boot, though - kmain
    # calls it on the next line - so the split put it in the half that is
    # already overwritten, and the machine mounted A:, far-called into the FAT
    # table, executed it, and unwound its own stack until sch_switch's canary
    # caught the wreckage several thousand instructions later. Nothing named
    # the overlay.
    #
    # Only kmain is scanned, and that is deliberate rather than a shortcut:
    # kmain is where the boot's ORDER is written, one call per line, and a
    # reachability answer for anything else is what tests/ovlrefs.txt's reason
    # column is for. A body reached from a runtime path is rule 2c's business.
    kfile = [f for f in kfiles if f.endswith('kernel.asm')]
    late = []
    for f in kfile:
        lines = open(f, errors='replace').read().split('\n')
        try:
            kstart = next(i for i, l in enumerate(lines) if l.startswith('kmain:'))
            mount = next(i for i, l in enumerate(lines)
                         if i > kstart and DRVBOOT.search(l))
        except StopIteration:
            continue
        # ...and STOP at kmain's own end, which is the next label in column 0.
        # Scanning to the end of the file instead reads the resident
        # trampolines below it - `dsk_flop_add: OVWCALL dsk_flop_add_x` is one,
        # and it is called from desk_init at MARK 20, long before the mount.
        # A rule about ORDER has to stop where the ordered code does.
        for i in range(mount + 1, len(lines)):
            if LABEL_DOT.match(lines[i]):
                break
            m = OVWCALL.search(lines[i].split(';', 1)[0])
            if m:
                late.append((f, i + 1, m.group(1)))
    for f, n, sym in late:
        print("%s:%d: OVWCALL %s is AFTER drv_boot_x - the first mount has "
              "already taken the FAT window those bytes are in"
              % (f, n, sym), file=sys.stderr)
    if late:
        sys.exit("os88ovlchk: %d call(s) into .ovlw run after the mount - move "
                 "the body to .ovl, which lives until spl_finish (SPEC.md "
                 "2.5.3)" % len(late))
    print("os88ovlchk: no .ovlw body is called after the first mount")

    # --- rule 1: cold code's DATA lives in .text ----------------------------
    # Same argument as the CS check and the same kind of invariant: DS still
    # names KERNEL_SEG in cold code, so a `db`/`dw` inside .cold is addressed
    # at the wrong segment by every reader of it.  The whole kernel satisfies
    # this (0 across seven cold modules), which is what makes it a refusal.
    #
    # It is worth checking mechanically because the tell is easy to miss BY
    # EYE: NASM does not require a colon on a label, so `desk_pdisk dw
    # ico_disk32` does not look like a label at a glance and a scan keyed on
    # `name:` walks straight past it.  Moving desk.inc cold took seven such
    # lines with it; DS then read the icon pointers out of .text at the cold
    # offsets, and the machine jumped into the weeds on the first click on a
    # drive zone - with the gfx lock held, so it froze rather than faulting.
    # The comment sitting above those very lines had predicted it: "a zero
    # [desk_pdisk] draws the interrupt vector table as an icon".
    #
    # .ovl is again exempt, and for the same reason: the overlay's data rides
    # WITH it, so fdd_mbit, drvp_sbbase and ovl_font_bits all belong there.
    DATA = re.compile(r'^\s*(?:[A-Za-z_]\w*:?\s+)?(?:d[bwdq]|times|res[bwdqt])\b',
                      re.I)
    d_bad = []
    for f in kfiles:                     # kernel/ only - see the note above
        for sect, n, line in sections(f):
            if sect == '.cold' and DATA.match(line):
                # ...and a MODULE's data is its header and nothing else
                # (SPEC.md 2.8): that block is read through ES by the loader,
                # never through DS by the module, so it is the one legitimate
                # `dw` on the far side of a boundary. mod_hdr_ok below is what
                # proves it stops there.
                d_bad.append((f, n, line.strip()[:60]))
    for f, n, src in d_bad:
        print("%s:%d: data in .cold: %s" % (f, n, src), file=sys.stderr)
    if d_bad:
        sys.exit("os88ovlchk: %d data directive(s) in .cold - SPEC.md 2.6 "
                 "rule 1 (data stays in .text)" % len(d_bad))
    print("os88ovlchk: no data in .cold")

    # --- and a module's data is its HEADER, at its head, and nothing else ---
    # A module (SPEC.md 2.8) runs with CS = a heap claim and DS = KERNEL_SEG,
    # so rule 1 binds it exactly as it binds .cold - with ONE exception, which
    # is the 12-byte header plus its entry table: those bytes are read through
    # ES by mod_need, never through DS by the module, and they have to be at
    # offset 0 because that is where the loader looks.
    #
    # So the check is positional rather than absolute: data before the first
    # instruction is the header, and data after it is the bug rule 1 describes.
    # Without this the module sections would be the one place in the kernel
    # where a stray `dw` is not refused by anything.
    # **THIS CHECK IS GONE, and SPEC.md 2.8.6 is why.** A module may now carry
    # its own strings, so `db` past the header is the new normal rather than
    # the bug it was - and the paragraph above describes what that costs. What
    # replaces it is narrower and still catches the thing that actually goes
    # wrong: **`lodsb` in a module image**.
    #
    # The failure a module's data can produce is one-sided. Nothing can read
    # module data too EARLY (the image is loaded before anything far-calls
    # into it) and nothing can read it from the wrong OFFSET (one assembly
    # fixes both ends). What is left is reading it through the wrong SEGMENT -
    # DS, which is KERNEL_SEG - and the one instruction that does that without
    # naming a segment is `lods`. It is also exactly the instruction somebody
    # reaches for when writing a string copier, which is what a module's data
    # is for. `mov al, [cs:si]` is the spelling that works.
    #
    # `movs`, `stos`, `cmps` and `scas` are NOT refused: all four take an
    # explicit pointer setup that a module already has to get right for other
    # reasons, and every use of them in the tree's modules is over a heap
    # claim or LOW_SEG rather than over the image (clo_keepboot, clo_fin,
    # clo_issrc, clo_zerohdr). Refusing them would refuse correct code.
    LODS = re.compile(r'^\s*(?:[A-Za-z_]\w*:\s*)?(?:rep\w*\s+)?lods[bwd]?\b',
                      re.I)
    m_bad = []
    for f in kfiles:
        for sect, n, line in sections(f):
            if sect in MODS and LODS.match(line):
                m_bad.append((f, n, sect, line.strip()[:50]))
    for f, n, sect, src in m_bad:
        print("%s:%d: lods in %s reads DS:SI, which is the KERNEL: %s"
              % (f, n, sect, src), file=sys.stderr)
    if m_bad:
        sys.exit("os88ovlchk: %d lods in a module image - SPEC.md 2.8.6 (a "
                 "module's own data is CS-relative: mov al, [cs:si])"
                 % len(m_bad))
    print("os88ovlchk: no module image reads its data through DS")

    # --- ...and the OTHER way to read module data through DS -----------------
    # The `lods` check above is the whole of what this file used to say about
    # SPEC.md 2.8.6, and it is blind to the two failures that actually happen
    # when a body of strings moves into an image:
    #
    #   * a MISSING `cs:` on a table read - `mov si, [si + cp_vidnam]` reads
    #     the KERNEL image at that offset and letters whatever is there;
    #   * a MISSING staging call - `mov si, cp_s_vcap` hands `font_run` a
    #     pointer that is correct in the IMAGE and nonsense in KERNEL_SEG.
    #
    # Neither moves a byte, both assemble, and both draw rubbish on a page a
    # user opens. Proven rather than asserted: deleting one `call cp_stage`
    # and one `cs:` from ctrl.inc left `kernsize` byte-for-byte identical and
    # every check in this file passing. That is the safety net this batch
    # exists to put under a 26-site hand review, and both halves are here.
    #
    # **Half 1 is universal.** A memory operand naming module data must carry
    # a `cs:`. There is no correct way to write that operand without one, in
    # any image, so this needs no registration and has no false positives.
    #
    # **Half 2 is per image, and needs the image to be written for it.** A
    # pointer LOAD (`mov si, <label>`) is indistinguishable from correct code
    # until you know what happens to SI, which is dataflow this cannot do. So
    # the rule is turned round: an image registers the macro that DEFINES a
    # string and the macros that READ one, and then a string label may appear
    # only at its own definition, inside another data directive or `equ` in
    # the same image, or as an argument of one of those macros. A bare
    # `mov si, cp_s_*` fails the build. It is a construction rule rather than
    # an analysis, which is why it is exact.
    #
    # **`.modl` (CLONE.DRV) and `.modf` (FORMAT.DRV) are NOT registered**, and
    # so get half 1 only. They predate this and read their strings with a bare
    # `mov si, clo_s_x` / `call clo_cat` in 26 and 10 places, in shapes a
    # next-line rule cannot express (`jmp .tail` reaches the composer four
    # lines later; a conditional pair stages at the join). Registering them
    # means converting those sites to a macro of their own - worth doing, and
    # not on the back of a change to a different image.
    MODDEF  = re.compile(r'^\s*([A-Za-z_]\w*):\s*(?:times\s+\S+\s+)?'
                         r'(?:d[bwdq]|res[bwdqt])\b', re.I)
    ISDATA  = re.compile(r'^\s*(?:[A-Za-z_]\w*:)?\s*(?:times\s+\S+\s+)?'
                         r'(?:d[bwdq]|res[bwdqt])\b', re.I)
    ISEQU   = re.compile(r'^\s*[A-Za-z_]\w*\s+equ\b', re.I)
    WORD    = re.compile(r'\b([A-Za-z_]\w*)\b')
    # per image: the macro that DEFINES one of its strings, and the macros
    # that READ one.  An image absent from here gets half 1 only.
    MODSTAGE = {'.modc': {'def': ('CPS',), 'use': ('CPSTAGE', 'CPSTAGEX')}}

    mdata = {}                       # module string/table label -> its section
    for f in files:
        for sect, n, line in sections(f):
            if sect not in MODS:
                continue
            m = MODDEF.match(line)
            if m:
                mdata[m.group(1)] = sect
                continue
            reg = MODSTAGE.get(sect)
            if reg:
                for d in reg['def']:
                    m = re.match(r'^\s*%s\s+([A-Za-z_]\w*)\s*,' % d, line)
                    if m:
                        mdata[m.group(1)] = sect
    for lab, (sect, is_data, _k) in pasted.items():
        if sect in MODS and is_data:
            mdata[lab] = sect        # cpc_sig, cpc_buf, ... pasted into .modc
    # the image HEADER is the one exception SPEC.md 2.8 carves out: mod_need
    # reads it through ES, at offset 0, before the image is ever entered.
    for k in [k for k in mdata if k.endswith('_hdr')]:
        del mdata[k]

    d_bad = []
    for f in files:
        for sect, n, line in sections(f):
            names = [w for w in WORD.findall(line) if w in mdata]
            if not names:
                continue
            if MODDEF.match(line) and MODDEF.match(line).group(1) in mdata:
                continue                       # the definition itself
            if sect in MODS and (ISDATA.match(line) or ISEQU.match(line)):
                continue                       # a table or an equ in the image
            reg = MODSTAGE.get(sect)
            if reg:
                mm = re.match(r'^\s*(%s)\s+([A-Za-z_]\w*)'
                              % '|'.join(reg['def'] + reg['use']), line)
                if mm and mm.group(2) in mdata:
                    continue                   # a sanctioned macro
            # half 1: a memory operand over module data must name CS
            if CSMEM.search(line):
                continue
            for w in names:
                if reg is None and not re.search(r'\[[^]]*\b%s\b' % w, line):
                    continue          # unregistered image: half 1 only, and
                                      # this is a pointer LOAD rather than a
                                      # memory operand over the image
                d_bad.append((f, n, sect, w, line.strip()[:56]))
    for f, n, sect, w, src in d_bad:
        print("%s:%d: %s is %s data read through DS: %s" % (f, n, w, sect, src),
              file=sys.stderr)
    if d_bad:
        sys.exit("os88ovlchk: %d module-data reference(s) do not name CS or a "
                 "registered stager - SPEC.md 2.8.6 (an image's own data is "
                 "CS-relative, and a pointer to it must be staged before "
                 "anything reads it through DS)" % len(d_bad))
    print("os88ovlchk: every module-data reference names CS or a registered "
          "stager (%d symbols)" % len(mdata))

    # --- and no TAIL CALL to a cw_ shim -------------------------------------
    # A cw_ shim is `call <target>` / `retf`: it exists to turn a far CALL
    # from cold code into a near call plus a far return.  Reaching it with a
    # `jmp` instead leaves no far frame, so the shim's `retf` pops the
    # JUMPING routine's near return address as CS:IP - a wild jump into
    # whatever segment that word happens to name.
    #
    # It is easy to write by accident, because a near tail call
    # (`jmp gfx_xor_fill`) is an ordinary idiom in this kernel and the
    # conversion to a shim looks mechanical.  desk_zone_hilite ended that way
    # and froze the machine on the first click on a drive zone, with the gfx
    # lock held; and drv_task's `.die` did the same and got away with it only
    # because task_exit never comes back.  Neither is visible to the near-call
    # check above: both ARE far transfers, which is exactly what it wants.
    JSHIM = re.compile(r'\bjmp\s+(?:far\s+)?\w+\s*:\s*cw_(\w+)')
    j_bad = []
    for f in files:
        for sect, n, line in sections(f):
            m = JSHIM.search(line)
            if m:
                j_bad.append((f, n, m.group(1)))
    for f, n, tgt in j_bad:
        print("%s:%d: jmp to cw_%s - a shim ends in retf, so this pops a near "
              "frame as CS:IP" % (f, n, tgt), file=sys.stderr)
    if j_bad:
        sys.exit("os88ovlchk: %d tail call(s) to a cw_ shim - use call + ret"
                 % len(j_bad))
    print("os88ovlchk: no tail call reaches a cw_ shim")

    # --- and .lowbss / .vgabuf are reached through SS or ES, never DS -------
    # SPEC.md 2.1: those two sections are in LOW_SEG and VGABUF_SEG and DS is
    # KERNEL_SEG for all kernel code, so a BARE reference to a symbol declared
    # in either reads the kernel's own image at that offset.  It assembles
    # cleanly and runs wrong, which is the whole family this file exists for.
    #
    # `.vgabuf` is here for SPEC.md 39.22's reason and is not a special case:
    # it is a rung of its own above `.lowbss` and is just as unreachable
    # through DS, so the same rule binds it.
    #
    # THE EXEMPTION IS A BANK, AND IT NAMES ITS SEGMENT - which is the whole
    # of why it names one.  A routine may point DS at one of these segments
    # for a hot loop (vga_blit_prow does, so its table costs no override byte
    # a pixel), and inside such a bank the bare reference is the correct one
    # FOR THAT SEGMENT ONLY.  `; ovlchk: DS = VGABUF_SEG` opens one and
    # `; ovlchk: DS restored` closes it.
    #
    # A bank that exempted EVERYTHING would be blind in precisely the place
    # the hazard is: SPEC.md 39.22 moved two buffers out of `.lowbss` into
    # `.vgabuf` and left three `.lowbss` words being read inside the decoder's
    # bank, each of which needed an `ss:` it had never needed before.  So the
    # symbol carries the segment its section lives in and the bank is checked
    # against it - inside a VGABUF_SEG bank a bare `.vgabuf` word is right and
    # a bare `.lowbss` word is the bug.
    SECT_SEG = {'.lowbss': 'LOW_SEG', '.vgabuf': 'VGABUF_SEG'}
    lb = {}
    for f in kfiles:
        for sect, n, line in sections(f):
            if sect not in SECT_SEG:
                continue
            m = RESERVE.match(line)
            if m:
                lb[m.group(1)] = SECT_SEG[sect]
    # `\b` on both sides so `vid_rowtab` does not match `vid_rowtab2` and
    # `font_zero` does not match inside `xfont_zero`.
    MEMREF = re.compile(r'\[\s*(?:(\w\w)\s*:)?([^\]]*)\]')
    OPEN = re.compile(r';\s*ovlchk:\s*DS\s*=\s*(\w+_SEG)\b', re.I)
    SHUT = re.compile(r';\s*ovlchk:\s*DS\s+restored\b', re.I)
    l_bad = []
    for f in files:
        low, opened_at = False, 0
        for n, raw in enumerate(open(f), 1):
            if OPEN.search(raw):
                if low:
                    l_bad.append((f, n, '(nested)', 'a DS bank inside a DS bank'))
                low, opened_at = True, n
            elif SHUT.search(raw):
                if not low:
                    l_bad.append((f, n, '(unopened)', 'DS restored, never banked'))
                low = False
        if low:
            l_bad.append((f, opened_at, '(unclosed)',
                          'a DS bank with no "DS restored"'))
    for f in files:
        bank = None                  # the segment DS is banked to, if any
        for sect, n, line, raw in sections_raw(f):
            m = OPEN.search(raw)
            if m:
                bank = m.group(1).upper()
            elif SHUT.search(raw):
                bank = None
            if sect in SECT_SEG:
                continue             # the declarations themselves
            for m in MEMREF.finditer(line):
                seg = (m.group(1) or '').lower()
                if seg in ('ss', 'es', 'cs'):
                    continue
                for w in WORD.findall(m.group(2)):
                    if w not in lb or lb[w] == bank:
                        continue
                    l_bad.append((f, n, w, 'reached without ss: or es:'
                                  if bank is None else
                                  'is %s, but DS is banked to %s here'
                                  % (lb[w], bank)))
    for f, n, sym, why in l_bad:
        print("%s:%d: %s - %s (SPEC.md 2.1/39.22)" % (f, n, sym, why),
              file=sys.stderr)
    if l_bad:
        sys.exit("os88ovlchk: %d .lowbss/.vgabuf finding(s) - SPEC.md 2.1 "
                 "(LOW_SEG and VGABUF_SEG are reached through SS or ES)"
                 % len(l_bad))
    print("os88ovlchk: every .lowbss/.vgabuf reference names SS or ES "
          "(%d symbols)" % len(lb))

    # --- and a routine's RETURN KIND matches how it is called ---------------
    # `ret` pops two bytes and `retf` pops four.  Get it the wrong way round
    # and the machine does not fault: it resumes at whatever the next word on
    # the stack happens to name, which on a task stack is live data.  Nothing
    # else here can see it - the near-call check above is about the CALL's
    # displacement and says nothing about the RETURN.
    #
    # It became checkable, and necessary, when SPEC.md 2.6.1 deleted 84 of the
    # `Xf_: call Y_x / retf` thunks by giving the body a `retf` of its own.
    # That is a 340-byte saving and one keystroke away from a wild jump: a
    # future near `call Y_x` from inside the same segment assembles perfectly
    # and returns into nowhere.  This is the rule that refuses it, and it
    # caught two real ones the first time it ran (two bodies had a SECOND
    # thunk nobody had noticed).
    #
    # A proc is classified by the return instructions inside its extent - from
    # its label to the next top-level one.  Anything mixed, or holding an
    # `iret`, is not classified and not judged: an interrupt handler and a
    # dual-entry routine are both legitimate and neither is this rule's
    # business.  An INDIRECT call (`call bx`, a `dw` table) is invisible here
    # exactly as it is to the near-call check, and stays a review rule.
    #
    # A ROUTINE THAT ENDS ON THE SHARED EPILOGUE LADDER STILL RETURNS.  The
    # ladder (kernel.asm, `kret_*` in .text, `kretc_*`/`kretfc_*` in .cold) is
    # reached by a tail `jmp`, so its rungs' `ret`/`retf` sit outside the
    # routine's extent and RETI saw nothing at all: 140 routines in kernel/
    # reach a rung and 135 of them were UNCLASSIFIED, which is neither arm of
    # the rule firing rather than the rule passing.  LADJ reads the rung's own
    # name - `kretfc_*` is the far ladder and returns `retf`, `kret_*` and
    # `kretc_*` are the near ones - so a converted routine keeps exactly the
    # coverage its `ret` had.  Nothing else about the ladder is this rule's
    # business: the DEPTH is `tools/stkbalance.py` and the pop ORDER is
    # `t_asmrules.crossed_pops`, and neither of those can see a return KIND.
    LADJ = re.compile(r'^\s*jmp\s+(kretf?c?_[a-z]{2})\s*(?:;.*)?$', re.I)
    RETI = re.compile(r'^\s*(?:[A-Za-z_.]\w*:\s*)?(ret|retf|retn|iret)\b', re.I)
    TOPL = re.compile(r'^([A-Za-z_]\w*):')
    FARC = re.compile(r'\bcall\s+(?:far\s+)?\w+\s*:\s*([A-Za-z_]\w*)')
    NRC  = re.compile(r'\bcall\s+(?:near\s+)?([A-Za-z_]\w*)\s*$')
    #
    # A LABEL IS COLLECTED AS A LIST OF EXTENTS, NOT AS ONE.  `%ifdef
    # KERN_BIG` / `%else` is the ordinary shape for a routine whose small-
    # kernel answer is a refusing stub, so `X_x` is TWO extents in one file -
    # and this used to keep one entry per label, `rets[cur] = seen`, so
    # whichever arm came last in the file classified the label and the other
    # arm was never looked at.  osapi_drv_dlg_x is exactly that: `ret` in the
    # KERN_BIG body, `retf` in the stub, far-called from the resident
    # trampoline - and the stub's retf waved the shipped kernel's near return
    # through.  Merging the arms into one set is the WRONG repair and was
    # tried: kindof() already declines to judge a mixed extent, so merging
    # turns a reported defect into an unreported one.  Each definition is
    # classified on its own and a call is refused if ANY of them disagrees.
    rets = {}
    for f in files:
        cur, seen = None, set()
        for sect, n, line in sections(f):
            m = TOPL.match(line)
            if m:
                if cur:
                    rets.setdefault(cur, []).append(seen)
                cur, seen = m.group(1), set()
            r = RETI.match(line)
            if r and cur:
                seen.add(r.group(1).lower())
            l = LADJ.match(line)
            if l and cur:
                seen.add('retf' if l.group(1).lower().startswith('kretfc_')
                         else 'ret')
        if cur:
            rets.setdefault(cur, []).append(seen)
    for lab, (_s, _d, kinds) in pasted.items():
        if kinds:
            rets.setdefault(lab, []).append(set(kinds))

    def kind1(r):
        if not r or 'iret' in r:
            return None
        if r == {'retf'}:
            return 'far'
        if r <= {'ret', 'retn'}:
            return 'near'
        return None                  # mixed: not this rule's business

    def kinds(lab):
        # every definition's classification, the unjudgeable ones dropped
        return set(k for k in map(kind1, rets.get(lab, ())) if k)

    r_bad = []
    for f in files:
        for sect, n, line in sections(f):
            for lab in FARC.findall(line):
                if 'near' in kinds(lab):
                    r_bad.append((f, n, lab, 'far-called, ends in a NEAR ret'))
            m = NRC.search(line)
            if m and 'far' in kinds(m.group(1)):
                r_bad.append((f, n, m.group(1), 'near-called, ends in RETF'))
    for f, n, lab, why in r_bad:
        print("%s:%d: %s is %s" % (f, n, lab, why), file=sys.stderr)
    if r_bad:
        sys.exit("os88ovlchk: %d return-kind mismatch(es) - SPEC.md 2.6.1 (a "
                 "far entry ends in retf and is never near-called)" % len(r_bad))
    print("os88ovlchk: every return kind matches how the routine is called")

    # --- and no far TAIL JUMP into a far entry either -----------------------
    # The cw_ rule above covers ONE direction of two.  A cw_ shim is
    # `call X / retf` in `.text`, reached from `.cold`; the `.text` -> `.cold`
    # far entries (cpf_, fmf_, dwf_, dkf_, ldf_, drvf_, uif_, ...) are the
    # SAME two instructions pointing the other way, and reaching one with
    # `jmp SEG:entry` has the identical failure: the jump pushes nothing, so
    # the entry's `retf` pops the JUMPING routine's near return address as IP
    # and whatever sits above it on the stack as CS.  A wild jump, and the
    # JSHIM regex above never named these labels because it matches `cw_` only.
    #
    # A far tail jump IS correct from a routine that was itself far-entered:
    # then the far frame the `retf` consumes is the one its own caller pushed.
    # That is the whole exemption, and it is decided by the same `kinds()` map
    # the return-kind rule already builds - so a body must PROVE it is far
    # (`retf` and nothing else) to be allowed one.
    JFAR = re.compile(r'\bjmp\s+(?:far\s+)?\w+\s*:\s*([A-Za-z_]\w*)')
    t_bad = []
    for f in files:
        cur = None
        for sect, n, line in sections(f):
            m = TOPL.match(line)
            if m:
                cur = m.group(1)
            m = JFAR.search(line)
            if m and 'far' in kinds(m.group(1)) and kinds(cur) != {'far'}:
                t_bad.append((f, n, m.group(1), cur or '(file head)'))
    for f, n, tgt, cur in t_bad:
        print("%s:%d: jmp to %s - it ends in retf, and %s does not, so the "
              "retf pops a near frame as CS:IP" % (f, n, tgt, cur),
              file=sys.stderr)
    if t_bad:
        sys.exit("os88ovlchk: %d far tail call(s) into a retf entry from a "
                 "near-returning body - use call + ret" % len(t_bad))
    print("os88ovlchk: no far tail jump enters a retf body without a far frame")

    # --- and a BLOB entry ends in retf, whichever macro names it ------------
    # The rule above judges a call it can SEE. The blob's entries are reached
    # by an indirect far call - `mov word [spl_fp], X` / `call far [spl_fp]`,
    # inside SPLCALL / OVLCALL / OVLCALLC / SPLGATE1 / OVLGATE1 / SPLSTUB - and
    # the only textual trace of X at the site is an operand of a `mov`. So
    # FARC never matched one, and an entry that kept its near `ret` popped IP,
    # left CS on the stack, and returned into whatever the word above it named.
    #
    # Every entry in the tree happened to be a `call body / retf` trampoline,
    # so the hole was invisible until SPEC.md 2.5.3 started moving BODIES into
    # `.ovl` and naming them directly - which is the right shape (SPEC.md
    # 2.6.1: the body owns the far return and the thunk in the middle is
    # deleted) and is one keystroke from a wild return. It was written wrong
    # here first, twice, and neither nasm nor any other check in this file said
    # anything.
    #
    # The macro name is the whole signal and that is deliberate: a site says
    # `OVLGATE1 sched_init` and nothing else in the line is a call, so this is
    # the one place the target can be read at all.
    # OVWCALL is in here for the same reason as the rest: it is a FAR call, so
    # its target owes a `retf` exactly as a blob entry does. Leaving it out was
    # not hypothetical - the sweep that created `.ovlw` moved 78 bodies and
    # this check stopped looking at every one of them.
    BLOBCALL = re.compile(r'^\s*(?:(?:SPL|OVL)(?:CALL|CALLC|GATE|GATE1|STUB)'
                          r'|OVWCALL)\s+([A-Za-z_]\w*)\s*$')
    b_bad = []
    for f in files:
        for sect, n, line in sections(f):
            m = BLOBCALL.match(line)
            if not m:
                continue
            if 'near' in kinds(m.group(1)):
                b_bad.append((f, n, m.group(1)))
    for f, n, lab in b_bad:
        print("%s:%d: %s is reached through `call far [spl_fp]` and ends in a "
              "NEAR ret" % (f, n, lab), file=sys.stderr)
    if b_bad:
        sys.exit("os88ovlchk: %d blob entry(ies) end in a near ret - every "
                 "SPLCALL/OVLCALL/OVLGATE target is entered with a FAR call, "
                 "so it ends in retf (SPEC.md 2.5.3, 2.6.1)" % len(b_bad))
    print("os88ovlchk: every blob entry ends in retf")



# =============================================================================
# THE APPLICATION PACKAGES, which the walk above does not reach (SPEC.md 68.10)
#
# A package that carries an overlay has the same hazard the kernel does and had
# no guard at all: `section .modc` code gets a CS of its own, so a near call
# from it to a resident label assembles cleanly - NASM emits a relative
# displacement, which is legal - and lands at that offset in the WRONG segment
# at run time. There is no crash to read and no message; the app simply stops.
#
# It is checked differently from the kernel half, and better: instead of a map
# saying which section each %included file lands in, this FOLLOWS the includes
# and carries the current section across them. That is what NASM actually does,
# so a file moved from `.text` into `.modc` by editing one %include line is
# reclassified with no second edit here - which matters, because the whole
# point of the overlay is that moving a subsystem out is a matter of moving its
# text.
#
# Each package is its own address space, so each gets its own label map: two
# packages may legitimately define the same wd_* label and neither can call the
# other's.
# A package listed here that is not in the tree is SKIPPED, not an error: a
# fork may carry one this branch does not, and the walk should still check the
# ones that are here. The count reported at the end is what was actually
# walked, so a skipped package cannot be mistaken for a checked one.
PKGS = ['apps/word/word.asm']


def expand(path, seen):
    """The package's source with %include inlined, as NASM assembles it."""
    if path in seen:
        return
    seen = seen | {path}
    here = os.path.dirname(path)
    for n, raw in enumerate(open(path, errors='replace'), 1):
        m = re.match(r'\s*%include\s+"([^"]+)"', raw)
        if m:
            for cand in (os.path.join(here, m.group(1)),
                         os.path.join('apps', m.group(1))):
                if os.path.exists(cand):
                    for row in expand(cand, seen):
                        yield row
                    break
            continue
        yield path, n, raw


def check_pkgs():
    bad = []
    walked = 0
    for pkg in PKGS:
        if not os.path.exists(pkg):
            continue
        walked += 1
        stream = list(expand(pkg, frozenset()))
        # one linear pass for the section each line lands in: a `section`
        # directive inside an include stays in force after it, exactly as it
        # does for NASM
        rows, cur = [], '.text'
        for f, n, raw in stream:
            line = raw.split(';')[0]
            m = re.match(r'\s*section\s+(\.\w+)', line)
            if m:
                cur = m.group(1)
                continue
            rows.append((cur, f, n, line))

        # ONLY `.modc` IS ANOTHER ADDRESS SPACE. A package is one flat binary
        # (SPEC.md 20.2) and tools/os88ovl.py cuts exactly one section off the
        # end of it, so `.text`, `.cold` and `.bss` are all the same CS and a
        # near call between them is correct. apps/os88ui.inc carries `.cold`
        # directives for the KERNEL's benefit (it is %included into a cold
        # block there) and those must not be read as a boundary here - without
        # this fold, 70 correct calls were reported.
        fold = lambda x: x if x == '.modc' else '.text'

        # A FILE %included TWICE was deduped here by keeping the LAST copy, on
        # the reasoning that the code-bearing include comes last. It does not
        # survive being tested: a file included into `.modc` FIRST and `.text`
        # second had its `.modc` rows dropped, and a real `.modc -> .text` near
        # call inside such a file was reported clean - a false negative, which
        # is the exact failure this whole walk exists to prevent. Nothing in
        # the tree needed it either: no file in a package's expansion is
        # included twice at all. So there is no dedup, and both copies of a
        # doubly-included file are judged in the section they actually land in.
        # If one is ever wanted again it must keep the `.modc` copy, not the
        # last one.

        where = {}
        for sect, f, n, line in rows:
            m = re.match(r'^([A-Za-z_]\w*):', line)
            if m:
                where[m.group(1)] = fold(sect)
        for sect, f, n, line in rows:
            for m in CALL.finditer(line):
                seg, tgt = m.group(1), m.group(2)
                if seg is not None:
                    continue            # `call far [vec]` is the way across
                t = where.get(tgt)
                if t is not None and t != fold(sect):
                    bad.append((f, n, '%s -> %s, near' % (fold(sect), t),
                                tgt, pkg))
    for f, n, why, tgt, pkg in bad:
        print("%s:%d: %s: %s  (%s)" % (f, n, why, tgt, pkg), file=sys.stderr)
    if bad:
        sys.exit("os88ovlchk: %d package call(s) cross a section boundary near "
                 "- SPEC.md 68.10 rule 1" % len(bad))
    if PKGS and not walked:
        sys.exit("os88ovlchk: none of the %d package(s) in PKGS is in the tree "
                 "- the package half of this gate checked nothing" % len(PKGS))
    print("os88ovlchk: %d package(s) keep every overlay call far" % walked)


if __name__ == '__main__':
    main()
    check_pkgs()
