; =============================================================================
; os8088 - mod.inc
;
; On-demand kernel modules (SPEC.md 2.8, docs/ONDEMAND-PLAN.md): kernel code
; that lives in a file rather than in KERNEL.SYS, is read into a heap claim
; when the feature is asked for, and is freed when the feature is done.
; Prefix `mod_`.
;
; **This is `.cold` with the address changed, and nothing else.** A module is
; assembled as part of THIS kernel, in a section of its own at vstart=0, and
; tools/os88mod.py cuts it out of the binary afterwards. So it keeps every one
; of cold code's properties (SPEC.md 2.6) - CS of its own, DS still
; KERNEL_SEG, out through the cw_* shims, near `ret`s throughout - and the
; only thing that changes is that its inbound thunk names a segment out of
; `mod_fp` instead of the constant COLD_SEG.
;
; Being part of the same assembly is the load-bearing property. Every kernel
; address a module names - `cp_sel`, `dsk_secbuf`, a string in .text - is the
; one the kernel itself uses, resolved by the same pass, so a module cannot
; hold a stale or second opinion about where anything is. The build stamp in
; its header (SPEC.md 2.8.2) refuses the one case that would break that: a
; module file left over beside a kernel it was not built with.
;
; WHAT MAY BE A MODULE is not a question about code (docs/ONDEMAND-PLAN.md 1):
; a feature qualifies only if the system disk is already required to use it,
; or can be required without interrupting what the user was doing - because on
; a one-floppy machine every load is a DISK SWAP. The Control Panel qualifies
; because it writes SYSTEM.CFG to that disk when it closes; the floppy
; formatter qualifies because preparing a disk is an operation with a trip to
; the disk box already in it; the disk CLONER (SPEC.md 18.99) qualifies for
; the formatter's reason with one more term in its favour, since a same-drive
; clone has TWO trips in it already. The Standard File dialog and
; Cut/Copy/Paste do not, and no amount of seam width changes that.
;
; NOTHING HERE CAN STOP ANYTHING. Every failure - no system disk, no heap, a
; short read, a stamp mismatch - is CF = 1 out of mod_need, and every caller
; owes a refusal the user can act on (SPEC.md 47 rule 3, and 59's toast is
; where all four of the current ones say it).
; =============================================================================

%ifdef FCP_MOD
MOD_MAX     equ 6               ; rows in mod_tab - SIX on kern_small, which
%else                           ; alone carries Cut/Copy/Paste and the file
MOD_MAX     equ 4               ; dialog as modules
%endif
MOD_CTRL    equ 0               ; the Control Panel (SPEC.md 31)
MOD_FMT     equ 1               ; the floppy formatter (SPEC.md 18.96)
MOD_CLONE   equ 2               ; the disk cloner (SPEC.md 18.99)
MOD_HIBER   equ 3               ; hibernate and resume (SPEC.md 87) - on
                                ; kern_small a one-entry stub, so that every
                                ; kernel cuts the same first four files
%ifdef FCP_MOD
MOD_FCP     equ 4               ; Cut/Copy/Paste (SPEC.md 22.3-22.5),
                                ; kern_small's alone - kern_big keeps the
                                ; bodies resident in `.cold`
MOD_FDLG    equ 5               ; ...and the Standard File dialog (SPEC.md 38,
                                ; 38.0), on the same terms
%endif

MOD_NENT    equ 7               ; far-pointer slots reserved per module. A
                                ; fixed stride, so a thunk's
                                ; `call far [mod_fp + K]` is an assembly-time
                                ; constant with no arithmetic at run time -
                                ; which is what makes dispatch re-entrant and
                                ; register-free (see mod_fp below).
                                ;
                                ; SEVEN, WHICH IS WHAT THE LARGEST MODULE USES
                                ; - SPEC.md 38.0's file dialog, on kern_small.
                                ; It is NOT per-build, and that was tried: the
                                ; obvious `%ifdef FDLG_MOD` saves kern_big the
                                ; twelve `.bss` bytes of a seventh slot it has
                                ; no module for, and tools/os88mod.py SCRAPES
                                ; this line - it cannot evaluate a %ifdef, so
                                ; it read 7 for both builds and refused
                                ; CTRL.DRV's first entry as being inside a
                                ; header two bytes longer than the one the
                                ; kernel had actually emitted. The tool's own
                                ; header says why that is the right way round:
                                ; the drawn thing and the tested thing come
                                ; from ONE description. Twelve bytes is the
                                ; price of keeping it one.
                                ;
                                ; It was SIX, WHICH WAS EXACTLY WHAT THE
                                ; MODULES THEN USED.
                                ; It was four, then eight when SPEC.md 13.8.3
                                ; gave the Control Panel a W_ONMOUSEUP and a
                                ; W_ONDRAG and it needed six. Eight was picked
                                ; because it is a POWER OF TWO and mod_fpr
                                ; could then reach mod_fp with three shifts -
                                ; but the three modules declare 6, 4 and 1
                                ; (CP_NENT, FM_NENT, CLO_NENT), so eleven
                                ; slots of twenty-four were ever armed and 52
                                ; bytes of mod_fp were written by mod_disarm
                                ; on every load and drop and were never a
                                ; far-call target.
                                ;
                                ; The power of two was never the requirement -
                                ; it was one ENCODING of MODFP_STRIDE /
                                ; MODR_SIZE, which is now 6, and x6 is
                                ; (x2 + x1) x 2: the same THIRTEEN BYTES of
                                ; mod_fpr, measured. What it costs is
                                ; headroom: the Control Panel is at the
                                ; ceiling, so a SEVENTH entry raises this
                                ; constant, and at x7 mod_fpr gains two bytes
                                ; on top of the .bss because x7 is x8 - x1.
                                ; It cannot be a silent overrun - every module
                                ; header ends `times MOD_NENT - X dw 0` and a
                                ; negative TIMES is a hard NASM error, in the
                                ; file that grew.

MODFP_STRIDE equ MOD_NENT * 4   ; bytes of mod_fp per module: four far
                                ; pointers. Named because it is spelled TWICE
                                ; and the two spellings drifted - the code
                                ; SHIFTED by it and each module's own base is
                                ; an `equ` that MULTIPLIES by it, and the
                                ; shift said 5 while the multiply said 16.
                                ; They agree for module 0 whatever either says,
                                ; so the Control Panel worked perfectly and the
                                ; formatter's slots were armed sixteen bytes
                                ; past the end of the array - over [mod_fsz],
                                ; which the arm loop's own bound check then
                                ; read - while its thunks far-called a block
                                ; nothing had written. Asserted below, because
                                ; the failure is silent in both directions:
                                ; the write lands on whatever .bss ordering
                                ; puts there, so a change nowhere near this
                                ; file decides what it corrupts.
                                ;
                                ; THE ASSERTION IS NOW AGAINST MODR_SIZE, not
                                ; against a shift of its own. mod_fpr goes
                                ; from a ROW to a slot block with three
                                ; unrolled `shl di,1`, and that 8 is
                                ; MODFP_STRIDE / MODR_SIZE - so what has to
                                ; hold is that the two strides are in that
                                ; ratio, whatever either of them is.

; --- the module image header, at offset 0 of the file ------------------------
; The one datum in a module image, and it is read through ES by the LOADER,
; never through DS by the module - so SPEC.md 2.6's "cold code carries no
; data" survives intact.
MOD_H_MAGIC equ 0               ; word: 0x384F ('O','8')
MOD_H_VER   equ 2               ; byte: 5 = kernel module (a package is 3, a
                                ; driver 4), so ld_check_hdr and drv_check
                                ; both refuse it - two independent gates, the
                                ; discipline SPEC.md 51.1 already uses
MOD_H_ID    equ 3               ; byte: which MOD_* row this image is
MOD_H_BUILD equ 4               ; word: the build number of the kernel this
                                ; was assembled with (SPEC.md 2.8.2)
MOD_H_STAMP equ 6               ; word: this kernel's LAYOUT, which the build
                                ; number cannot answer for - `make small`,
                                ; `make VIDEO=cga` and a FONT= build are all
                                ; the same commit and put the kernel's data at
                                ; different addresses, so a module built for
                                ; one would far-call correct offsets into the
                                ; wrong variables. It is the section sizes
                                ; summed, which no two configurations of one
                                ; commit have ever shared
MOD_H_IMG   equ 8               ; word: image size = the file's whole size
MOD_H_NENT  equ 10              ; word: entries that follow
MOD_H_ENT   equ 12              ; word[]: entry offsets, in this image
MOD_HDR_SZ  equ MOD_H_ENT + MOD_NENT*2
MOD_VER     equ 5
MOD_MAX_KB  equ 16              ; the biggest module image we will claim

; The layout stamp (SPEC.md 2.8.2). Forward references to the section sizes,
; which is legal in a `dw` and is how MODC_SIZE already reaches its own end
; label - and it has to be, because every module is assembled before the sizes
; are known. Summed rather than hashed: the point is that two builds of one
; commit differ, and a section size differing by ONE byte moves every address
; above it.
MOD_STAMP   equ (KTEXT_SIZE + KBSS_SIZE + COLD_SIZE + KLOW_SIZE) & 0xFFFF

; --- the table row, stride MODR_SIZE -----------------------------------------
MODR_SEG    equ 0               ; dw: the segment it is loaded at, 0 = not
MODR_FILE   equ 2               ; dw: -> its NUL 8.3 file name (.text)
MODR_SIZE   equ 4               ; ...and that is the whole row. It was 8: two
                                ; live words, a `dw: KB its claim took` that
                                ; three sites STORED and nothing ever read,
                                ; and two bytes of padding to keep the stride
                                ; a power of two. The dead word went first and
                                ; the padding with it - the live part had been
                                ; the first four bytes for a while, and MOD_MAX
                                ; is THREE, so the pad was wasting six.
MODR_SHIFT  equ 2               ; ...still a power of two, so the id -> row
                                ; conversion is shifts and never a multiply.
                                ; ONE SITE reads it now - mod_row, which
                                ; unrolls it as two `shl bx,1`. The three
                                ; `mov cl, MODR_SHIFT / shr ax, cl` that took
                                ; the id back OUT of a row pointer are gone:
                                ; the only caller that wanted it (mod_check)
                                ; is handed the id in DI instead, and the
                                ; conversion is now one-way.
%if (1 << MODR_SHIFT) != MODR_SIZE
  %error "MODR_SHIFT and MODR_SIZE disagree: mod_row SHIFTS a module id up into a row pointer while every other site SHIFTS one back down, and a stride that is not 1<<MODR_SHIFT makes those two operations different functions - which reads one row and writes another, silently"
%endif
%if MODFP_STRIDE != MODR_SIZE * MOD_NENT
  %error "mod_fpr turns a row pointer into &mod_fp[id] with a chain whose ratio is MODFP_STRIDE / MODR_SIZE. Change either stride without the other and every module but module 0 arms a block that is not its own - silently, over whatever .bss ordering has put there"
%endif
%if MOD_NENT != 7
  %error "mod_fpr's multiply chain is hand-written for THIS ratio (x7 = x8 - x1). A different MOD_NENT needs a different chain, and one that assembles is not the same as one that is right - work out the bytes before changing it"
%endif      ; ...and see MODFP_STRIDE above for this one

section .text
; DATA stays in the kernel segment, reached through DS, exactly as a cold
; module's own data does (SPEC.md 2.6). The CODE below is .cold, and that is
; not a tidiness choice: in .text it would be ~430 bytes against the 438 this
; kernel has left under KERN_CODE_MAX, and that guard cannot be raised by
; anybody. Being cold also means the module thunks that call mod_need are in
; the same segment and reach it with a NEAR call, so the pair of cw_* shims
; the first draft needed does not exist.

mod_f_ctrl: db 'CTRL.DRV', 0    ; .DRV for the attributes and nothing else
mod_f_fmt:  db 'FORMAT.DRV', 0  ; (SPEC.md 19.6): os88disk.py stamps anything
mod_f_clon: db 'CLONE.DRV', 0   ; ...and a third of them (SPEC.md 18.99)
mod_f_hib:  db 'HIBER.DRV', 0   ; ...and a fourth: hibernate (SPEC.md 87),
                                ; which qualifies twice - the user is leaving
                                ; the machine, or has just booted it
%ifdef FCP_MOD
mod_f_fcp:  db 'FILECP.DRV', 0  ; ...and kern_small's fifth (SPEC.md 22.3)
mod_f_fdlg: db 'FDLG.DRV', 0    ; ...and its sixth (SPEC.md 38.0)
%endif
                                ; ending DRV read-only + hidden + system by
                                ; EXTENSION, the installer's `*.DRV` copy rule
                                ; picks it up, and neither of these is in
                                ; drv_tab, so nothing lists them as drivers or
                                ; tries to attach one

mod_tab:
    dw 0, mod_f_ctrl            ; MOD_CTRL
    dw 0, mod_f_fmt             ; MOD_FMT
    dw 0, mod_f_clon            ; MOD_CLONE
    dw 0, mod_f_hib             ; MOD_HIBER
%ifdef FCP_MOD
    dw 0, mod_f_fcp             ; MOD_FCP
mod_r_fdlg:                     ; ...and this one is NAMED, because SPEC.md
    dw 0, mod_f_fdlg            ; 38.0.1's resident guard asks "is the image
                                ; held?" and that question is this word. A
                                ; label rather than mod_tab + MOD_FDLG *
                                ; MODR_SIZE at the call site: ui.inc is
                                ; %included ABOVE this file, so the two `equ`s
                                ; are not defined there and a forward label is
                                ; the only spelling that works in both.
                                ; Under FCP_MOD and not FDLG_MOD because that
                                ; is where the ROW is; the two are defined
                                ; together by KERN_SMALL, and decoupling them
                                ; is an undefined symbol rather than a silent
%endif                          ; read of the wrong word
MOD_TAB_OFF equ mod_tab - $$    ; mod_tab AS A NUMBER. .text has vstart=0
                                ; (kernel.asm), so this IS the address every
                                ; `mov bx, mod_tab` loads - and a number can
                                ; be multiplied where a label cannot: nasm
                                ; 3.02 refuses `mod_tab*7` in mod_fpr below
                                ; as an invalid operand, in every spelling

section .cold                   ; ...and the loader itself

; -----------------------------------------------------------------------------
; mod_gone - what every unloaded entry slot points at
; out: CF = 1
;
; A `retf`, because the slots it fills are far-call targets. Nothing should
; ever reach it - every thunk calls mod_need first - so this is the answer to
; "what if one does" being a refusal rather than a jump into whatever claimed
; that memory next. Zeroing the slot instead would far-call 0000:0000, which
; is the divide-by-zero vector.
; -----------------------------------------------------------------------------
mod_gone:
    stc
    retf

; -----------------------------------------------------------------------------
; mod_row - AL = a module id -> BX = its row
; clobbers: BX, AH, flags
;
; AH, because `cbw` is a byte where `mov bh, 0` is two. It SIGN-extends, so an
; AL past 0x7F would give a negative row offset where `mov bh,0` gave a large
; positive one - both out-of-table garbage, and every caller passes a MOD_*
; constant or the [fm_swapwho]-selected one, so no reachable input differs.
; All three callers bank AX round the call.
; -----------------------------------------------------------------------------
mod_row:
    cbw
    mov bx, ax
    shl bx, 1
    shl bx, 1                   ; MODR_SHIFT, unrolled rather than borrowing CL
    add bx, mod_tab
    ret

; -----------------------------------------------------------------------------
; mod_live - is module AL resident RIGHT NOW? (SPEC.md 2.8, 13.8.3)
; in:  AL = a module id
; out: CF = 0 resident, CF = 1 not; BX = its row either way, nothing else
;      touched - which is still true, and the only thing that changed is
;      WHICH flags carry the answer. `cmp word [bx+MODR_SEG], 1` sets CF iff
;      the word is below 1 unsigned, and for a 16-bit word that is exactly
;      "is zero" - the question this routine asks. ZF/SF/OF now differ on the
;      resident arm; both callers (ctrl.inc's cpf_cp_onup and cpf_cp_ondrag,
;      the complete set) read CF alone, and the contract never named the rest.
;
; mod_need without the loading. The callers are the ones that can only ever
; follow a gesture the module itself took - a W_ONMOUSEUP or a W_ONDRAG after
; a W_ONCLICK that loaded it (SPEC.md 13.8.3) - where "not resident" means the
; arm is stale and there is nothing to fire. Loading the Control Panel off a
; FLOPPY to answer a mouse movement would be absurd, and on the drag path
; mod_need would try it eighteen times a second.
; -----------------------------------------------------------------------------
mod_live:
    call mod_row
    cmp word [bx+MODR_SEG], 1   ; CF = 1 iff the word is 0 - see the banner
    ret

; -----------------------------------------------------------------------------
; mod_fpr - BX = a ROW -> DI = &mod_fp[id], the base of its slot block
; clobbers: DI, flags.  BX SURVIVES, which is what deletes the two round trips
;           its callers used to make through the stack.
;
; Both callers already hold the row and both want the answer in DI, so the id
; is never formed at all: &mod_fp[id] is mod_fp + (BX - mod_tab) / MODR_SIZE *
; MODFP_STRIDE, and that ratio is 6 (the %if above pins it), so the whole
; thing is BX*MOD_NENT with a constant folded in.  BX*6 may wrap 16 bits and the
; result is still exact - it is a `.bss` offset under 64KB, so the arithmetic
; is modular either way.  x6 is (x2 + x1) x 2, which is the same thirteen bytes
; the three shifts were: the ratio being a power of two bought nothing here,
; which is what let MOD_NENT come down to what the modules use - and then back
; up to SEVEN on kern_small alone, where x7 is x8 - x1 for one byte more.
; -----------------------------------------------------------------------------
mod_fpr:
    mov di, bx                  ; x7 = x8 - x1: no multiply and no CL, which is
    shl di, 1                   ; the property that matters, and one byte more
    shl di, 1                   ; than the x6 chain it replaced
    shl di, 1
    sub di, bx
    add di, mod_fp - MOD_TAB_OFF*7
    ret

; -----------------------------------------------------------------------------
; mod_need - make sure a module is loaded (SPEC.md 2.8.1)
; in:  AL = MOD_*
; out: CF = 0 it is loaded and its mod_fp slots are live; CF = 1 it is not,
;      and nothing was left half-done
; clobbers: FLAGS ONLY - every register is preserved, including AX
;
; **Every register, because this sits under a thunk whose caller's arguments
; are already in them.** cp_onclick takes CX/BX, dskw_format takes AL; the
; thunk pushes AX to make room for the id and pops it back, and this routine
; owes it everything else. That is drv_svc_call's lesson (SPEC.md 51.2) in the
; one place where the argument set is not even known here.
;
; The order is drv_load's, minus everything that is about being a driver: no
; table row to publish, no class, no DRVV_ATTACH, no tier. drv_vol_bank /
; drv_vol_back bracket it for SPEC.md 51.5.2's reason - the user may be
; anywhere, and going to the system volume is a file operation and not a
; navigation - and the compare inside them makes the common case free.
;
; **Every call below is NEAR**, because driver.inc, memory.inc and diskw.inc
; are all in the cold set and so is this file. Routing them out through a cw_
; shim and back in through a resident thunk is SPEC.md 2.6's double crossing -
; six segment transitions where a near call does - and the sweep that found
; 139 of those deleted the shims this file was written against.
; -----------------------------------------------------------------------------
mod_need:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    mov di, ax                  ; THE ID, FOR THE WHOLE ROUTINE, and in a
                                ; register rather than on the stack: it used
                                ; to be pushed here and popped by both exit
                                ; arms, and nothing between read it -
                                ; mod_check's own banner said the pushed copy
                                ; was NOT read. DI survives every callee
                                ; below (drv_vol_bank/back and drv_mounted
                                ; preserve everything, drv_find clobbers AX
                                ; alone, mem_claim_hi_x "every other register
                                ; preserved" - including through mem_compact,
                                ; whose mem_reloc_call is "all registers
                                ; preserved" - and dskw_read_x banks BP/DI
                                ; itself), and mod_check reads it instead of
                                ; deriving the id back out of the row pointer
    call mod_row
    cmp word [bx+MODR_SEG], 0
    jne .already

    call drv_vol_bank                      ; this goes to the system volume to find the
                                ; file, and the user may have been anywhere
    call drv_mounted           
    jc .fail

    mov si, [bx+MODR_FILE]
    call drv_find                             ; AX = the file's size, refused over 64KB
    jc .fail
    cmp ax, MOD_HDR_SZ
    jb .fail
    mov bp, ax                  ; THE FILE'S BYTE SIZE, in the register this
                                ; routine already banks and never spends -
                                ; it was a .bss word read five times, four of
                                ; them through a disp16 operand. mod_check
                                ; takes it as an input

    call mem_bytes_kb_x         ; -> whole KB. The size is well under 64KB,
    cmp ax, MOD_MAX_KB          ; so this cannot carry out
    ja .fail
    push bx
    mov bx, MEM_K_MOD
    call mem_claim_hi_x                   ; a module's base IS its CS, so it can never
    pop bx                      ; move: top down, like a package's region
    jc .fail
    mov [bx+MODR_SEG], dx

    push bx
    mov es, dx                  ; ...and NO second `mov si, [bx+MODR_FILE]`:
    mov cx, bp                  ; SI is still the one loaded for drv_find, and
    xor bx, bx                  ; mem_claim_x banks SI/DI around everything
                                ; including the compaction (memory.inc:520)
    xor dx, dx                  ; DX:CX = the capacity, DX:AX the answer
    call dskw_read_x                         ; the whole file, by name, on the system disk
    pop bx
    jc .freefail
    cmp ax, bp                  ; DX is 0 or the read would have refused
    jne .freefail

    call mod_check
    jc .freefail

    call drv_vol_back           ; preserves CF - and the success path FALLS
                                ; INTO the answer below
.already:
.ok:
    clc
    jmp short .out
.freefail:
    call mod_free_row           ; the claim goes back; nothing was armed
.fail:
    call drv_vol_back           ; ...and this one no longer has to preserve
    stc                         ; CF, which is weaker than what the old shape
                                ; leaned on rather than stronger
.out:
    jmp kretc_es

; -----------------------------------------------------------------------------
; mod_check - validate a loaded image and arm its entry slots (module internal)
; in:  BX = the row with MODR_SEG live, BP = the bytes that arrived,
;      DI = the module id mod_need was called with - which is checked against
;      the header's own MOD_H_ID rather than derived back out of the row
;      pointer, because mod_need already has it
; out: CF = 0 and mod_fp armed; CF = 1 malformed - and nothing armed
; clobbers: AX, CX, DX, SI, DI, ES, flags
;
; ld_check_hdr's tests on a module, plus the one a package and a driver do not
; need: **the build stamp**. A module carries no dispatcher, because the
; kernel knows its entry offsets - it built them - and that is cheaper than
; `call bp / retf` and also the hazard, since a module file from another
; build would be far-called at offsets that have since moved. buildnum.py's
; number changes on every commit (SPEC.md 14.2), so agreement here is by
; construction and disagreement is refused rather than executed.
; -----------------------------------------------------------------------------
mod_check:
    push bx
    mov es, [bx+MODR_SEG]
    cmp word [es:MOD_H_MAGIC], 0x384F
    jne .bad
    cmp byte [es:MOD_H_VER], MOD_VER
    jne .bad
    mov ax, di                  ; the id mod_need was CALLED with - nine
    cmp [es:MOD_H_ID], al       ; bytes of arithmetic recovered a number the
    jne .bad                    ; caller was holding. If DI were ever
                                ; clobbered on the way here the failure is a
                                ; REFUSAL and not a wrong dispatch, which is
                                ; the safe direction
    cmp word [es:MOD_H_BUILD], BUILD_NUM
    jne .bad
    cmp word [es:MOD_H_STAMP], MOD_STAMP
    jne .bad
    mov ax, [es:MOD_H_IMG]
    cmp ax, bp                  ; the image IS the file
    jne .bad
    mov cx, [es:MOD_H_NENT]
    jcxz .bad
    cmp cx, MOD_NENT
    ja .bad

    ; --- arm the slots. Every entry offset is checked against the image the
    ; --- way ld_check_hdr checks a package's: inside the bytes that arrived,
    ; --- and past the header, so a malformed table cannot aim a far call at
    ; --- the header's own bytes or off the end of the claim.
    call mod_fpr                ; DI = &mod_fp[id], and BX is untouched - so
                                ; the `pop bx / push bx` that used to put the
                                ; row back goes with the id derivation
    mov si, MOD_H_ENT           ; ...and CX is STILL MOD_H_NENT from the bound
    cld                         ; test above: mod_fpr writes DI and the flags
.arm:                           ; and nothing else, and says so
    es lodsw                    ; the entry table, one word a pass
    cmp ax, MOD_HDR_SZ
    jb .badarm
    cmp ax, bp
    jae .badarm
    mov [di], ax                ; offset...
    mov [di+2], es              ; ...and segment, which ES IS: it was loaded
                                ; from [bx+MODR_SEG] at the top of this
                                ; routine and no segment register is written
                                ; in between
    add di, 4
    loop .arm
    pop bx
    clc
    ret
.badarm:
    call mod_disarm             ; a half-armed block is the one state nothing
    pop bx                      ; downstream could survive
    stc
    ret
.bad:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; mod_disarm - point one module's slots back at mod_gone (module internal)
; in:  BX = the row
; clobbers: CX, DI, flags       <- NOT AX any more: mod_fpr answers from the
;           row, so the id is never formed. mod_init_x's loop counter depends
;           on that (see there).
; -----------------------------------------------------------------------------
mod_disarm:
    call mod_fpr                ; DI = &mod_fp[id]; BX survives, so the bank
    mov cx, MOD_NENT            ; round this routine goes too
.slot:
    mov word [di], mod_gone
    mov word [di+2], COLD_SEG
    add di, 4
    loop .slot
    ret

; -----------------------------------------------------------------------------
; mod_free_row - give a module's claim back (module internal)
; in:  BX = the row
; clobbers: AX, BX, CX, DX, DI, flags   <- BX now: it becomes the MEM_K_MOD
;           owner word for mem_free and is not put back. Both callers are in
;           this file, both named below, and neither reads it after the call.
;           A THIRD CALLER WILL NOT BE TOLD - the same hazard clo_drop
;           accepted, and the same mitigation: it is written here.
;
; DISARM FIRST. The claim is freed here, and a slot still naming it is a far
; call into whatever claims that memory next - which is the failure this whole
; mechanism has to be unable to produce, because it would not fault.
; -----------------------------------------------------------------------------
mod_free_row:
    cmp word [bx+MODR_SEG], 0   ; ...and no outer bank: nothing between here
    je .out                     ; and `.out` writes BX except the inner
    call mod_disarm             ; mod_disarm preserves BX since it stopped
    xor dx, dx                  ; forming the id, and READ-AND-CLEAR is one
    xchg dx, [bx+MODR_SEG]      ; instruction on the 8086 - DX comes out the
    mov bx, MEM_K_MOD           ; old segment and the word is left 0. NO bank
    call mem_free_x             ; round BX either: neither caller reads it
.out:                           ; after the call - mod_need's .freefail falls
    ret                         ; into .fail and out through kretc_es, and
                                ; mod_drop goes straight to its own pop run

; -----------------------------------------------------------------------------
; mod_drop - the feature is finished with its module (SPEC.md 2.8.3)
; in:  AL = MOD_*
; clobbers: FLAGS ONLY - every register preserved, for mod_need's reason
;
; **The caller decides when, and there is exactly one right moment per
; feature**, because nothing here can tell whether the module is on the stack:
; the Control Panel's is `cp_flush_close` returning (the panel's own teardown,
; SPEC.md 31.8), and the formatter's is the confirmation ending, either way.
; Dropping a module that is still executing frees the code that is running -
; which does not fault and is why this is not, and must not become, something
; the heap does on its own initiative. docs/ONDEMAND-PLAN.md 7.1/7.2 is the
; purgeable-claim design that would need a pin; it is deliberately not built.
; -----------------------------------------------------------------------------
mod_drop:
    push ax
    push bx
    push cx
    push dx
    push si                     ; SI is pushed so the run is a SUFFIX of the
    push di                     ; shared ladder's es/bp/di/si/dx/cx/bx/ax -
    call mod_row                ; which mod_drop already preserved in fact
    call mod_free_row           ; (mod_free_row clobbers AX, BX, CX, DX, DI),
    jmp kretc_di                ; so no contract weakens. .cold's copy of the
                                ; ladder, and mod_drop is .cold

; -----------------------------------------------------------------------------
; mod_init - point every slot at mod_gone before anything can call one
; clobbers: CX, DI, flags
;
; WHY IT EXISTS - AND NOT FOR THE REASON THIS COMMENT USED TO GIVE. It said
; ".bss arrives as whatever was in memory - `-f bin` zeroes nothing". That is
; false here and clip.inc's declaration block says so from the other end:
; `.bss` is `nobits vfollows=.text` and `.cold`'s file position is padded past
; it, so NASM emits at least KBSS_SIZE zero bytes and stage 2's one contiguous
; read lands them. `.bss` IS zero at boot, which is precisely why clip.inc
; needs no init entry point.
;
; What this routine is for is the OTHER thing: a row's resting value is not
; zero. Every MODR_SEG must read mod_gone before the first thunk runs, or a
; thunk that reaches its far call ahead of its own mod_need jumps to segment
; 0. mod_need cannot be trusted to be the only door, because a thunk with the
; test written the wrong way round still assembles.
; -----------------------------------------------------------------------------
mod_init_x:
    mov di, mod_fp              ; ONE PASS over the whole array. The row walk
    mov cx, MOD_MAX * MOD_NENT  ; existed only to reach mod_fp - mod_disarm
    call mod_disarm.slot        ; turned each row straight back into
    retf                        ; &mod_fp[id] through mod_fpr - and the three
                                ; blocks are contiguous by construction.
                                ;
                                ; ...and NO `mov word [bx+MODR_SEG], 0`: that
                                ; wrote zero over zero. mod_tab is in .text
                                ; (see its declaration) as a real `dw 0`
                                ; initialiser, re-read out of KERNEL.SYS on
                                ; every boot, and this routine's ONE caller is
                                ; kmain - Chip > Restart being `int 19h`, the
                                ; BIOS bootstrap, which re-loads the image.

section .cold                   ; the far entry, for kmain

section .bss

; **A far pointer per entry, and that is the whole dispatch mechanism.**
; A thunk does `call far [mod_fp + K]` with K an assembly-time constant, which
; takes no register, needs no scratch word two callers could race for, and is
; re-entrant across modules by construction. It is drv_fptr/drv_fseg's idiom
; (SPEC.md 51.2.1) with a slot per entry rather than a slot per class - and
; the reason it is not one shared `mod_target` word set by mod_need is that a
; word set here and consumed one instruction later is a race with anything
; that can run in between, which on a pre-emptive kernel is everything.
mod_fp:     resb MOD_MAX * MODFP_STRIDE
                                ; ...and NO mod_fsz. The file's byte size
                                ; lives in BP for the length of mod_need,
                                ; which banks BP at entry and never spends
                                ; it. It used to be the word after mod_fp,
                                ; which is how the stride bug above announced
                                ; itself at all - the arm loop overwrote it
                                ; and then compared against it

section .text
