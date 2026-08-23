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
import re, sys, glob

CALL = re.compile(r'\b(?:call|jmp|j[a-z]{1,3}|loop[a-z]{0,2})\s+'
                  r'(?:(?:near|short)\s+)?(?:(\w+):)?([A-Za-z_]\w*)\b')
# an API cell macro whose body near-calls its LAST argument
CELL = re.compile(r'^\s*OSAPI_(?:SLOT|NSTUB|XSTUB)\s+(?:\w+\s*,\s*)?'
                  r'([A-Za-z_]\w*)\s*$')
MODS = ('.modc', '.modf', '.modl')   # on-demand module images (SPEC.md 2.8).
# **A section added here and nowhere else is a section NOTHING below checks**,
# which is how `.modl` shipped once with the near-call check blind to it - the
# clone module's every `call COLD_SEG:` was correct, and would not have been
# reported had one been near.
FAR = ('.ovl', '.cold') + MODS   # sections with a vstart of their own


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
EXTRA = {'apps/os88ui.inc': '.cold'}


def sections(path):
    """yield (section, line-number, source-line) with comments stripped"""
    cur = EXTRA.get(path, '.text')
    for n, raw in enumerate(open(path), 1):
        line = raw.split(';')[0]
        m = re.match(r'^\s*section\s+(\.\w+)', line)
        if m:
            cur = m.group(1)
            continue
        yield cur, n, line


def main():
    kfiles = sorted(glob.glob('kernel/*.inc')) + ['kernel/kernel.asm']
    files = kfiles + sorted(EXTRA)
    where = {}                       # label -> section it is defined in
    for f in files:
        for sect, n, line in sections(f):
            m = re.match(r'^([A-Za-z_]\w*):', line)
            if m:
                where[m.group(1)] = sect
    bad = []
    for f in files:
        for sect, n, line in sections(f):
            hits = [(m.group(1), m.group(2)) for m in CALL.finditer(line)]
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
    CS = re.compile(r'\b(?:push\s+cs|mov\s+\w+\s*,\s*cs|cs\s*:'
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
    DATA = re.compile(r'^\s*(?:[A-Za-z_]\w*:?\s+)?(?:d[bwdq]|times|resb|resw)\b',
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


if __name__ == '__main__':
    main()
