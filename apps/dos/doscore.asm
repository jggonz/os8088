; =============================================================================
; os8088 - apps/dos/doscore.asm
;
; **THE DOS CORE, ON ITS OWN** (SPEC.md 96.44, 96.44.5; the design record is
; docs/plans/KERN-DOS-PLAN.md 4.1.3): INT 21h and everything under it - the
; PSP, the handle layer, the FCBs, the MCB chain, `AH=4Bh` and the built-in
; commands - assembled ONCE, with no host around it, and shipped as a part
; both hosts join.
;
; **IT IS THE SAME FILE, BEHIND TWO DEFINES, AND NOT A COPY.** `apps/dos/dos.asm`
; carries three populations and each is marked where it stands:
;
;   %ifndef KD_BACKEND   the WINDOW half (96.43.2) - what `kern_dos` leaves out
;   %ifndef DOS_EXTCORE  the CORE (96.44) - what a HOST leaves out
;   everything else      the container: the constants, the DBSS table and the
;                        bss equates, which every build wants and which emit
;                        no bytes at all
;
; so this root is `KD_BACKEND` with no host, and a host is `DOS_EXTCORE` with
; no core.  Nothing was moved to make that true, which is the whole reason the
; split is checkable.
;
; WHAT IT NAMES OUTSIDE ITSELF IS THREE THINGS, and that is the measurement
; docs/plans/KERN-DOS-PLAN.md 4.1.3 wanted (*"core -> box is ZERO"*), which
; came out true on the tree: the only symbol a core span names that a host
; defines is `DVOL_MAX`, a constant the container already `%ifndef`s.
;
;   os88_image_end   where the DBSS table is based - `CORE_BSS_AT`, a CONSTANT
;                    both hosts place the core's bss at (96.44.2)
;   DVOL_MAX         how many volumes the machine can have (96.38)
;   dos_bevec        the twenty-two back-end doors AS ADDRESSES (96.44.1),
;                    filled by whichever host is running
;
; **THE FIRST BYTES ARE THE TABLE** (`apps/dos/doscall.inc`), because a host
; cannot know the core's internal addresses - they are assembled separately -
; and what it CAN know is where the core begins.
; =============================================================================
cpu 8086
bits 16

%define KD_BACKEND                  ; ...so the window half is not in this one
%define DOS_CORE_ROOT               ; ...and the container knows it has no host

%include "doscall.inc"              ; CORE_ORG, CORE_MAX and the table's shape

    org CORE_ORG

    DOSC_EMIT                       ; the jump table, then the data table

; --- what a host would otherwise have said ----------------------------------
%ifndef DVOL_MAX
DVOL_MAX equ DVOL_CAP               ; the kernel's own WIDEST arm, which is
%endif                              ; `kernel/disk.inc`'s 8 (4 on kern_small)
                                    ; and which `doscall.inc` above already
                                    ; calls `DVOL_CAP` - the one number both
                                    ; halves read.  **IT WAS A LITERAL 8 AND
                                    ; dos.asm's WAS A LITERAL 6**, which is
                                    ; SPEC.md 96.44.2.1: the core's own bss
                                    ; table was sized from it, so the two
                                    ; halves laid every cell after that table
                                    ; four bytes apart
os88_image_end equ CORE_BSS_AT      ; THE CORE NEVER NAMES AN HBSS CELL - it
                                    ; is `tests/unit/t_dosbss.py`'s rule 2 -
                                    ; so this exists only so the equate block
                                    ; assembles. The core's own cells are at
                                    ; `DOS_CBASE` (96.44.5)

%include "dos.asm"

; --- and the budget, which is the one number this file owns -----------------
CORE_SIZE equ $ - $$
%if CORE_SIZE > CORE_MAX
  %error "the DOS core outgrew CORE_MAX - raise it in apps/dos/doscall.inc, \
and note that BOTH hosts reserve it, so a byte here is a byte off each of them"
%endif
