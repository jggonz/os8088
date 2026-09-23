; =============================================================================
; os8088 - apps/dos/doscall.inc
;
; **THE CORE SEAM, EXPANDED TWICE** (SPEC.md 96.44.5): the same list in
; `apps/dos/doscents.inc` becomes a TABLE when the core is being assembled and
; a set of `%define`s when a host is. Nothing in `dos.asm`, `dosh.inc` or the
; window files changes spelling either way - `call dos_load` is `call
; dos_load` in both - which is the property that makes the split checkable
; rather than a rewrite.
;
; **THE TABLE IS AT `CORE_ORG` AND THAT IS THE WHOLE ABI.** A host cannot know
; the core's internal addresses: they are assembled separately, from a file
; the host does not include. What it can know is where the core BEGINS, and
; the first bytes of the core are this table.
;
; `jmp near` IS SPELLED OUT, and it has to be. nasm shrinks a resolved short
; jump, which would make the entries two bytes instead of three and move every
; slot after the first backward branch - in a build where the two halves are
; assembled separately and the host would simply call the wrong routine.
; `kerndos/kdos.asm`'s fixed header carries the same instruction for the same
; reason.
; =============================================================================

; INCLUDED TWICE ON PURPOSE and guarded here rather than at each site: the
; core root includes it for `CORE_ORG` before it includes `dos.asm`, which
; includes it again for `DOS_CBASE`. A host includes it once.
%ifndef DOSCALL_INC
%define DOSCALL_INC 1

; --- WHERE THE CORE LIVES, and both hosts and the core read it from here ----
; **THE CORE IS LOW** (docs/plans/KERN-DOS-PLAN.md 4.1.3.1.1). The plan put it
; ABOVE both hosts, on a measurement that they were within 600 bytes of each
; other - and W8 then cut 14,622 bytes out of one of them. They differ by
; 4,628 now and always will: `kern_dos` carries the disk layer and no window,
; the box carries the window and no disk layer. Core-above turns that
; difference into a HOLE in `kern_dos`'s address space, which is 4.7 KB off
; `KD_IMG_KB` and so 4.7 KB off the DOS program - the one quantity the whole
; plan is a budget for. Core-LOW has no hole at all: each host's own code
; simply starts above a fixed-size core.
;
; `CORE_ORG` clears the two things that cannot move, and since SPEC.md 96.44.6
; there is only one of them worth clearing. It was **0x0600**, set by
; `kern_dos`'s refusal wall - 179 `stc`/`retf` cells covering every published
; `OSAPI_*` offset to 0x05A8, so that a surviving far call refused instead of
; jumping into the disk layer. The wall is gone because the CALLS are: the core
; now makes none (the two in `dos_keeph` were a window routine mis-marked core,
; and `dosh.inc`'s four became `DHK_CLAIM`/`DHK_FREE`), and
; `tests/unit/t_kdapi.py` scans both images for the opcode rather than
; reserving address space against it.
;
; What is left to clear is the BOX's own low bytes - the package header at
; 0x00, the 16x16 icon at 0x20, the association block at 0x60, which
; `OS88_ASSOC16_END` pads to 112, and the document glyph at 0x70, which
; `OS88_DOCGLYPH8_END` pads to **128** (SPEC.md 54.3.2) - and `kern_dos`'s
; eight-byte fixed header. 0x0080 clears both EXACTLY, with nothing over, and
; each host asserts its own side. **1,408 bytes out of BOTH hosts**, which on `kern_dos` is a
; kilobyte of `KD_IMG_KB` and so a kilobyte of the DOS program.
CORE_ORG    equ 0x0080
; `CORE_MAX` is a BUDGET WITH ONE CLAIMANT, which is the other half of why low
; beats high: the plan's `CORE_ORG` is a ledger two hosts push on from
; opposite sides, so growing either one costs the other. This is a rung the
; CORE alone spends, both hosts read, and whose slack is one number in one
; place. Measured at the wave, with the assertion in `apps/dos/doscore.asm`.
CORE_MAX    equ 15872            ; +1024 for SPEC.md 96.11.7's character
                                ; devices, on the owner's call: *"raise the
                                ; image by a kb for now, and we will optimize
                                ; afterwards. Working at all is most
                                ; important."* It was 30 bytes of slack against
                                ; a 14,818-byte core, and the device path does
                                ; not fit in 30. Task #27 is where it comes
                                ; back: a kilobyte here is a kilobyte off
                                ; KD_IMG_KB, so it is a kilobyte off the DOS
                                ; program on the shut-down arm
; ...and the core's bss sits directly above its code, at the same offset in
; every host, which is what SPEC.md 96.44.2 requires of every `DBSS` cell.
; --- THE CORE'S BLOCK IS A BUDGET, NOT A SUM (SPEC.md 96.44.2) --------------
; `CORE_BSS_SIZE` is a CONSTANT because the core is assembled once and joined
; to either host: its cells must be at the same offsets in both, so the host's
; block cannot start wherever this host's core happens to end. It is checked
; rather than derived, the way `KERN_BUDGET` is, and the slack is what a later
; core cell is spent out of.
%ifndef CORE_BSS_SIZE               ; `-DCORE_BSS_SIZE=n` is how the budget is
CORE_BSS_SIZE equ 3328              ; probed: bisect on it and the assertion
%endif                              ; below names the exact figure
                                    ;
                                    ;   3,251 the core's cells at W9b
                                    ;      +8 96.44.3's four host hooks
                                    ;      +9 the padding rows, which belong to
                                    ;         the block they pad and which
                                    ;         nothing NAMES, so the classifier
                                    ;         could not see them
                                    ;      60 slack
                                    ;   ----- -------------------------------
                                    ;   3,328

; ...and the core's bss sits directly above its code, at the same address in
; every host, which is what SPEC.md 96.44.2 requires of every `DBSS` cell.
CORE_BSS_AT equ CORE_ORG + CORE_MAX

; --- A `DBSS` ROW'S SIZE IS PART OF THE LAYOUT (SPEC.md 96.44.2.1) ----------
; `DOS_B_DVCWD` is a word per drive and was written `2 * DVOL_MAX` - which is
; the box's per-drive REACH, and that one is `%ifndef KD_BACKEND` so a host
; bringing its own disk layer keeps that layer's number (96.38).  The window
; said 6, the core said 8, and every core cell after the table was then FOUR
; BYTES APART in the two halves - the exact failure 96.44.2's rule is about,
; arrived at through a row's size rather than through the row.
;
; So the WIDTH is an ABI constant and lives here, where both halves read the
; same line, and `DVOL_MAX` goes back to being only a bound.  It is the widest
; any host can ask for (`kernel/disk.inc`: 4 on kern_small, 8 on kern_big), and
; `apps/dos/dos.asm` asserts that whatever this build's `DVOL_MAX` turns out to
; be still fits it.
DVOL_CAP    equ 8

DOSC_STRIDE equ 3               ; a `jmp near` - see the header

%ifdef DOS_EXTCORE
; --- A HOST: every name becomes its slot ------------------------------------
; A CODE name becomes the ADDRESS of its `jmp`, so `call dos_load` is a near
; call into the table and the table jumps on. A DATA name becomes a memory
; REFERENCE - the brackets are inside the %define - so `mov si, dsh_s_bad`
; reads the word the core put there. That is one byte more per site and it is
; why 96.44.5 spent the effort to get the data population down to two.
; **`%xdefine` AND NOT `%define`, AND IT IS THE WHOLE THING.** A `%define`
; body is expanded where it is USED, so `%define %1 (DOSC_TAB + DOSC_N * 3)`
; captures nothing: every one of the thirty-three names expands to
; `DOSC_N * 3` with DOSC_N at whatever it is at the CALL SITE - which is 33,
; the value it finished the list on. All thirty-three then point at the same
; address, three bytes past the end of the table, and the first call into the
; core jumps into the middle of its own data and never comes back.
; `%xdefine` expands the body HERE, so each name carries its own ordinal.
%assign DOSC_N 0
%macro DOSC_E 1
    %xdefine %1 (DOSC_TAB + DOSC_N * DOSC_STRIDE)
    %assign DOSC_N DOSC_N + 1
%endmacro
%assign DOSC_DN 0
%macro DOSC_D 1
    %xdefine %1 [DOSC_DAT + DOSC_DN * 2]
    %assign DOSC_DN DOSC_DN + 1
%endmacro
%include "doscents.inc"
%unmacro DOSC_E 1
%unmacro DOSC_D 1
DOSC_TAB equ CORE_ORG
DOSC_DAT equ CORE_ORG + DOSC_N * DOSC_STRIDE
DOSC_NENT equ DOSC_N
DOSC_NDAT equ DOSC_DN

%else
; --- THE CORE: the table itself ---------------------------------------------
; Emitted by `apps/dos/doscore.asm` at `org CORE_ORG`, BEFORE anything else,
; so `DOSC_TAB` is the core's first byte. The forward references are resolved
; by nasm's later passes; every one of them is a label further down this same
; assembly.
%macro DOSC_EMIT 0
DOSC_TAB:
    %assign DOSC_N 0
    %macro DOSC_E 1
        jmp near %1
        %assign DOSC_N DOSC_N + 1
    %endmacro
    %macro DOSC_D 1
    %endmacro
    %include "doscents.inc"
    %unmacro DOSC_E 1
    %unmacro DOSC_D 1
DOSC_DAT:
    %assign DOSC_DN 0
    %macro DOSC_E 1
    %endmacro
    %macro DOSC_D 1
        dw %1
        %assign DOSC_DN DOSC_DN + 1
    %endmacro
    %include "doscents.inc"
    %unmacro DOSC_E 1
    %unmacro DOSC_D 1
DOSC_NENT equ DOSC_N
DOSC_NDAT equ DOSC_DN
%endmacro
%endif
%endif                              ; DOSCALL_INC
