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
; `jmp strict near` IS SPELLED OUT, and it has to be. nasm shrinks a resolved
; short jump (nasm 3.01 shrinks one written `jmp near` too; only `strict`
; stops it), which would make the entries two bytes instead of three and move
; every slot after the first short one - in a build where the two halves are
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
; DOS.O88 carries the window and no disk layer. Core-above turns that
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
; What is left to clear is DOS.O88's own low bytes - the package header at
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
%ifndef DOS_MEGA
CORE_MAX    equ 16576            ; +512 for SPEC.md 96.7.2's stack swap and
                                ; 96.12.5's name truncation, which are 214
                                ; bytes between them against 89 of slack. The
                                ; two are the Playroom's whole report: a gate
                                ; frame on the program's stack was scribbling
                                ; on the free block's MCB, and a name that is
                                ; not 8.3 was refused where DOS truncates.
                                ; 387 left.
                                ; +1024 for SPEC.md 96.11.7's character
                                ; devices, on the owner's call: *"raise the
                                ; image by a kb for now, and we will optimize
                                ; afterwards. Working at all is most
                                ; important."* It was 30 bytes of slack against
                                ; a 14,818-byte core, and the device path does
                                ; not fit in 30. Task #27 is where it comes
                                ; back: a kilobyte here is a kilobyte off
                                ; KD_IMG_KB, so it is a kilobyte off the DOS
                                ; program on the shut-down arm.
                                ; **16,384 -> 15,312 IN APPS SIZE PASS 1**
                                ; (SPEC.md 96.7.3, 96.44.15): the core fell
                                ; to 15,311, so the kilobyte came back and
                                ; one more with it - KD_IMG_KB 35 -> 33
                                ; +32 for SPEC.md 96.30.9: `CD \A\B` walks
                                ; the folder part (dsh_cdto, +30) and a CD
                                ; that fails goes back to where it stood
                                ; (+8), against 8 of slack. 15,342 used,
                                ; kept a multiple of 16
                                ; +48 for CP/M's CALL 5 (dos_cpm, and
                                ; dos_psp_make aiming PSP:5 at it), which the
                                ; HMA run had and the PSP:0050 gate never
                                ; served: 15,391 used
                                ; +16 for SPEC.md 96.55's AH=62h/51h, the
                                ; call XCOPY.EXE takes its PSP from: 12 bytes
                                ; against 10 of slack, 15,410 used
                                ; +48 for SPEC.md 96.8.2: an .EXE is read as
                                ; far as its LOAD MODULE and no further, which
                                ; DOS does and the whole-file read did not
                                ; (Works 1.05: a 319KB file, a 2.8KB image).
                                ; +99 against 52 of slack, 15,471 used - and
                                ; +48 is exactly what kern_dos's 33KB rung
                                ; had left (49), so its DOS program pays
                                ; nothing. dos_ldrd sits after dos_cpm's
                                ; `align 16` for that reason: in front of it
                                ; the same code cost 112
                                ; +16 for SPEC.md 96.54.4's dos_m33_evt, the
                                ; tick's event dispatch made a proc a window's
                                ; worker calls as it wakes: 7 bytes (+4 the
                                ; split, +3 its DOSC_E slot) and 13 of
                                ; dos_cpm's paragraph alignment, which the
                                ; slot pushed over a boundary: 15,430 used
                                ; ...and the two met at the merge at 15,475,
                                ; three over - and over by 15,472's three
                                ; was a 34KB rung for kern_dos. dos_m33_evt
                                ; testing BL where it copied it to SI first
                                ; took four back: CORE_MAX stays 15,472
                                ; -32 for SPEC.md 96.54.8's SIZE PASS, which
                                ; paid for the mouse hook's reset and kept
                                ; 32: twenty adds and subs of 1 and 2 as
                                ; inc/dec (19, the carry read by none of
                                ; them), and ten shared epilogues into two
                                ; OS88_POPLADDERs (25), less the reset's 5
                                ; that dos_cpm's padding absorbed - 15,430
                                ; used. kern_mega's core falls with it,
                                ; 16,080 used of 16,096
                                ; -48 for the windowed-DOS SIZE PASS's slice
                                ; in dos.asm (SPEC.md 96.54.8.1): 27 bytes in
                                ; front of dos_cpm's `align 16`, which falls
                                ; two paragraphs (32) - dos_load's ES is
                                ; dos_ldrd's first chunk's, its tail a popb
                                ; rung and its error a `mov` under the cmp's
                                ; flags; dos_psp_make reads the PSP out of ES
                                ; and writes the JFT's 1, 0 and the FFFF:FFFF
                                ; as words; the hook table's end stores the
                                ; AL = 0 it ended on; two more popa rungs -
                                ; and behind it dos_ldrd's segment a `lea`
                                ; (1), dos_m33_evt's tail the popa ladder
                                ; (5), AH=36h's default drive a `sub` and its
                                ; dropped word a `pop` (4): 15,430 -> 15,388
                                ; used. kern_mega's core falls with it,
                                ; 16,080 -> 16,038 used of 16,048
                                ; +48 for SPEC.md 12.8.10's one bar, the
                                ; DBE_FSOP door and COPY's and TYPE's use of
                                ; it (50 bytes against 10 of slack): 15,448 on its own tree; with this branch's
                                ; size pass beside it, 15,436
                                ; +528 for SPEC.md 96.56, WordPerfect 4.1's
                                ; calls: AH=33h and AH=37h (24 bytes) and the
                                ; FCB file calls 0Fh 10h 11h 13h 16h 27h 28h
                                ; (503) against 4 of slack, 15,963 used - and
                                ; past kern_dos's rung: KD_IMG_KB 33 -> 34
                                ; +16 for SPEC.md 96.8.2.1, the load
                                ; module's reads as ONE bar (16 bytes
                                ; against 5 of slack): 15,979 used
                                ; +144 for SPEC.md 96.56.2, Galaxy Trek's
                                ; 14h, 21h and 24h on 27h's body (140 bytes
                                ; against 5 of slack): 16,119 used
                                ; +368 for SPEC.md 96.56.3, the rest of the
                                ; FCB family (15h 17h 22h 23h, a real 12h,
                                ; the extended FCB's attribute, the PSP's
                                ; FCBs parsed and copied): 371 bytes against
                                ; 9 of slack, 16,490 used
                                ; +16 at the merge with main's WIN16 phase A,
                                ; whose write-back in place (.fwback, SPEC.md
                                ; 96.11.6.4) met this arm's 16,490: 3 bytes
                                ; over, 16,499 used
                                ; +16 for SPEC.md 96.56.1/96.56.3's two FCB
                                ; repairs - a closed FCB's handle spoiled,
                                ; and 27h/28h bounded by the DTA's segment
                                ; (AL = 2): 28 bytes against 13 of slack,
                                ; 16,527 used. +32 for SPEC.md 96.11.11's
                                ; per-volume cluster (dos_fh_cb), which met
                                ; main's 1 of slack at #327's merge of #328
                                ; at 16,568 and was cut back to 16,558 (36h's
                                ; one tail, two shared jumps); kern_dos paid
                                ; its kilobyte's worth back in kd_resume.
                                ; +16 for COPY's buffer sized on the
                                ; DESTINATION's cluster (SPEC.md 96.30.7.3,
                                ; a goto and the KB asked for: 14 bytes
                                ; against 2 of slack) and the drive-only
                                ; path's colon (96.33.21, 1): 16,572 used
%else                           ; **kern_mega's core is 67 bytes longer**:
CORE_MAX    equ 17232            ; SPEC.md 96.15.4's extended-to-extended
%endif                          ; move (dos_xms_move's `.bothx`), 15,409 used.
                                ; +80, kept a multiple of 16, and paid ONLY by
                                ; kern_mega's DOS.O88 - both of its hosts
                                ; reserve the hole, so it is 80 bytes of each.
                                ; +128 for that move's overlap, which XMS
                                ; guarantees and one forward pass broke
                                ; (KERN-MEGA-DOS-PLAN 10.4 item 1): 15,550 used
                                ; +64 for CP/M's CALL 5 on the HMA run,
                                ; dos_cpm and its PSP arm (KERN-MEGA-DOS-PLAN
                                ; 10.5 defect 3), the handler paragraph-aligned
                                ; +64 for dos_hvec and its five callers, every
                                ; far address the core publishes routed
                                ; through a low slot (10.5 defect 4): 15,645
                                ; +384 on the other track for the rest of the
                                ; XMS surface - 0Ch-0Fh, 08h's largest block,
                                ; 0Bh's handle bounds (SPEC.md 96.15.4): the
                                ; two met at the merge, 16,027 used
                                ; +64 for SPEC.md 96.8.2's load-module read,
                                ; +91 here against 37 of slack: 16,118 used
                                ; +32 for the DBE_FSOP door and its two
                                ; users (SPEC.md 12.8.10): 16,095
                                ; ...and with the windowed-DOS size pass
                                ; beside it at the merge, 16,088 used
                                ; +528 for SPEC.md 96.56's AH=33h/37h and
                                ; FCB file calls, 527 bytes against 8 of
                                ; slack: 16,613 used
                                ; +16 for SPEC.md 96.8.2.1's one bar for
                                ; the load module: 16,629 used
                                ; +144 for SPEC.md 96.56.2's 14h, 21h and
                                ; 24h, as on kern_big's arm
                                ; +368 for SPEC.md 96.56.3, as on kern_big's
                                ; arm: 17,140 used
                                ; +32 for the two FCB repairs, as on
                                ; kern_big's arm (28 bytes against 3 of
                                ; slack): 17,177 used
                                ; +32 for SPEC.md 96.11.11's per-volume
                                ; cluster and WIN16's forward (dosw16.inc,
                                ; 124.31.1) on this arm, met at #327's
                                ; merge of #328: 17,208 used
                                ; +16 for COPY's destination-sized buffer
                                ; and the drive-only path, as on kern_big's
                                ; arm (14 bytes against 8): 17,222 used
; ...and the core's bss sits directly above its code, at the same offset in
; every host, which is what SPEC.md 96.44.2 requires of every `DBSS` cell.
; --- THE CORE'S BLOCK IS A BUDGET, NOT A SUM (SPEC.md 96.44.2) --------------
; `CORE_BSS_SIZE` is a CONSTANT because the core is assembled once and joined
; to either host: its cells must be at the same offsets in both, so the host's
; block cannot start wherever this host's core happens to end. It is checked
; rather than derived, the way `KERN_BUDGET` is, and the slack is what a later
; core cell is spent out of.
%ifndef CORE_BSS_SIZE               ; `-DCORE_BSS_SIZE=n` is how the budget is
 %ifndef DOS_MEGA                   ; probed: bisect on it and the assertion
CORE_BSS_SIZE equ 3517              ; below names the exact figure. +6 for
                                    ; dos_vcb, a byte a volume, less the
                                    ; launch-only word it replaced (SPEC.md
                                    ; 96.11.11)
 %else                              ; kern_mega's +2 is `dos_hslot`, the HMA
CORE_BSS_SIZE equ 3521              ; host's slot maker (KERN-MEGA-DOS-PLAN
                                    ; 10.5 defect 4), and +2 again for
                                    ; DHK_XUNP, the child's EXEPACK hook
                                    ; (SPEC.md 96.40.9.1) - DHK_NENT 7 -> 8
                                    ; grows dos_hkv on kern_mega alone
 %endif
%endif
                                    ;
                                    ;   3,251 the core's cells at W9b
                                    ;      +8 96.44.3's four host hooks
                                    ;      +9 the padding rows, which belong to
                                    ;         the block they pad and which
                                    ;         nothing NAMES, so the classifier
                                    ;         could not see them
                                    ;     +60 later cells, which spent the
                                    ;         slack to the byte
                                    ;     +13 SPEC.md 96.52: FH_DIR, a word a
                                    ;         handle, less the three bytes
                                    ;         dos_fh_at stopped banking
                                    ;    +166 SPEC.md 96.53: a READ_SEQ
                                    ;         cursor and a WRITE_SEQ token a
                                    ;         handle (+144), the shell COPY's
                                    ;         pair (+18), and 4 of the padding
                                    ;         the rows around them want
                                    ;       0 slack
                                    ;      +2 DBE_FSOP, dos_bevec's 25th
                                    ;         door (SPEC.md 12.8.10)
                                    ;   ----- -------------------------------
                                    ;   3,509

; ...and the core's bss sits directly above its code, at the same address in
; every host, which is what SPEC.md 96.44.2 requires of every `DBSS` cell.
CORE_BSS_AT equ CORE_ORG + CORE_MAX

; --- A `DBSS` ROW'S SIZE IS PART OF THE LAYOUT (SPEC.md 96.44.2.1) ----------
; `DOS_B_DVCWD` is a word per drive and was written `2 * DVOL_MAX` - which is
; DOS.O88's per-drive REACH, and that one is `%ifndef KD_BACKEND` so a host
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

DOSC_STRIDE equ 3               ; a `jmp strict near` - see the header

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
        jmp strict near %1
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
