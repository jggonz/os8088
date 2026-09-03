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

MOD_MAX     equ 4               ; rows in mod_tab
MOD_CTRL    equ 0               ; the Control Panel (SPEC.md 31)
MOD_FMT     equ 1               ; the floppy formatter (SPEC.md 18.96)
MOD_CLONE   equ 2               ; the disk cloner (SPEC.md 18.99)
MOD_HIBER   equ 3               ; hibernate and resume (SPEC.md 87)

MOD_NENT    equ 8               ; far-pointer slots reserved per module. A
                                ; fixed stride, so a thunk's
                                ; `call far [mod_fp + K]` is an assembly-time
                                ; constant with no arithmetic at run time -
                                ; which is what makes dispatch re-entrant and
                                ; register-free (see mod_fp below).
                                ;
                                ; EIGHT. It was four - "what both modules use,
                                ; and not a round eight" - until SPEC.md
                                ; 13.8.3 gave the Control Panel a
                                ; W_ONMOUSEUP and a W_ONDRAG and it needed
                                ; six. The costs the old comment named are
                                ; real and are what they are: +32 bytes of
                                ; .bss (MOD_MAX x 16) and 8 bytes of each
                                ; module FILE, and every module is rebuilt -
                                ; which a kernel change already forces,
                                ; MOD_H_BUILD being the commit count.
                                ;
                                ; IT HAS TO BE A POWER OF TWO, because
                                ; mod_fpi SHIFTS by MODFP_SHIFT while each
                                ; module's base equ MULTIPLIES by
                                ; MODFP_STRIDE. The assertion below is what
                                ; caught those two disagreeing once already

MODFP_STRIDE equ MOD_NENT * 4   ; bytes of mod_fp per module: four far
                                ; pointers. Named because it is spelled TWICE
                                ; and the two spellings drifted - `mod_fpi`
                                ; SHIFTS by it and each module's own base is
                                ; an `equ` that MULTIPLIES by it, and the
                                ; shift said 5 while the multiply said 16.
MODFP_SHIFT equ 5               ; They agree for module 0 whatever either says,
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
%if (1 << MODFP_SHIFT) != MODFP_STRIDE
  %error "MODFP_SHIFT does not match MOD_NENT: mod_fpi and the *FP equs would disagree"
%endif

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
MODR_KB     equ 4               ; dw: KB its claim took
MODR_PAD    equ 6
MODR_SIZE   equ 8               ; a power of two so mod_row is three shifts;
                                ; two rows, so the byte it wastes is two

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
                                ; ending DRV read-only + hidden + system by
                                ; EXTENSION, the installer's `*.DRV` copy rule
                                ; picks it up, and neither of these is in
                                ; drv_tab, so nothing lists them as drivers or
                                ; tries to attach one

mod_tab:
    dw 0, mod_f_ctrl, 0, 0      ; MOD_CTRL
    dw 0, mod_f_fmt,  0, 0      ; MOD_FMT
    dw 0, mod_f_clon, 0, 0      ; MOD_CLONE
    dw 0, mod_f_hib,  0, 0      ; MOD_HIBER

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
; clobbers: BX, flags
; -----------------------------------------------------------------------------
mod_row:
    mov bl, al
    mov bh, 0
    shl bx, 1
    shl bx, 1
    shl bx, 1                   ; MODR_SIZE = 8
    add bx, mod_tab
    ret

; -----------------------------------------------------------------------------
; mod_live - is module AL resident RIGHT NOW? (SPEC.md 2.8, 13.8.3)
; in:  AL = a module id
; out: CF = 0 resident, CF = 1 not; BX = its row either way, nothing else
;      touched
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
    cmp word [bx+MODR_SEG], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; mod_fpi - AL = a module id -> BX = &mod_fp[id], the base of its slot block
; clobbers: BX, CX, flags
; -----------------------------------------------------------------------------
mod_fpi:
    mov bl, al
    mov bh, 0
    mov cl, MODFP_SHIFT         ; MODFP_STRIDE, the way a register can take it
    shl bx, cl
    add bx, mod_fp
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

    push ax                     ; the id, for the whole routine
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
    mov [mod_fsz], ax

    add ax, 1023                ; -> whole KB. The size is well under 64KB,
    mov cl, 10                  ; so this cannot carry out
    shr ax, cl
    cmp ax, MOD_MAX_KB
    ja .fail
    push bx
    mov bx, MEM_K_MOD
    call mem_claim_hi_x                   ; a module's base IS its CS, so it can never
    pop bx                      ; move: top down, like a package's region
    jc .fail
    mov [bx+MODR_SEG], dx
    mov [bx+MODR_KB], ax

    push bx
    mov es, dx
    mov si, [bx+MODR_FILE]
    mov cx, [mod_fsz]
    xor bx, bx
    xor dx, dx                  ; DX:CX = the capacity, DX:AX the answer
    call dskw_read_x                         ; the whole file, by name, on the system disk
    pop bx
    jc .freefail
    cmp ax, [mod_fsz]           ; DX is 0 or the read would have refused
    jne .freefail

    call mod_check
    jc .freefail

    call drv_vol_back                     ; preserves CF
    jmp short .ok

.freefail:
    call mod_free_row           ; the claim goes back; nothing was armed
.fail:
    call drv_vol_back          
    pop ax
    jmp short .out_c
.already:
.ok:
    pop ax
    clc
    jmp short .out
.out_c:
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mod_check - validate a loaded image and arm its entry slots (module internal)
; in:  BX = the row with MODR_SEG live, [mod_fsz] = the bytes that arrived,
;      the id still on the stack under the return address is NOT read - the
;      header's own MOD_H_ID is checked against the row instead
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
    mov ax, bx
    sub ax, mod_tab
    mov cl, 3
    shr ax, cl                  ; the row index back out of the row pointer
    cmp [es:MOD_H_ID], al
    jne .bad
    cmp word [es:MOD_H_BUILD], BUILD_NUM
    jne .bad
    cmp word [es:MOD_H_STAMP], MOD_STAMP
    jne .bad
    mov ax, [es:MOD_H_IMG]
    cmp ax, [mod_fsz]           ; the image IS the file
    jne .bad
    mov cx, [es:MOD_H_NENT]
    or cx, cx
    jz .bad
    cmp cx, MOD_NENT
    ja .bad

    ; --- arm the slots. Every entry offset is checked against the image the
    ; --- way ld_check_hdr checks a package's: inside the bytes that arrived,
    ; --- and past the header, so a malformed table cannot aim a far call at
    ; --- the header's own bytes or off the end of the claim.
    mov ax, bx
    sub ax, mod_tab
    mov cl, 3
    shr ax, cl
    call mod_fpi                ; BX = &mod_fp[id]
    mov di, bx
    pop bx
    push bx
    mov si, MOD_H_ENT
    mov cx, [es:MOD_H_NENT]
.arm:
    mov ax, [es:si]
    cmp ax, MOD_HDR_SZ
    jb .badarm
    cmp ax, [mod_fsz]
    jae .badarm
    mov [di], ax                ; offset...
    mov ax, [bx+MODR_SEG]
    mov [di+2], ax              ; ...and segment
    add si, 2
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
; clobbers: AX, CX, DI, flags
; -----------------------------------------------------------------------------
mod_disarm:
    push bx
    mov ax, bx
    sub ax, mod_tab
    mov cl, 3
    shr ax, cl
    call mod_fpi
    mov di, bx
    mov cx, MOD_NENT
.slot:
    mov word [di], mod_gone
    mov word [di+2], COLD_SEG
    add di, 4
    loop .slot
    pop bx
    ret

; -----------------------------------------------------------------------------
; mod_free_row - give a module's claim back (module internal)
; in:  BX = the row
; clobbers: AX, CX, DX, DI, flags
;
; DISARM FIRST. The claim is freed here, and a slot still naming it is a far
; call into whatever claims that memory next - which is the failure this whole
; mechanism has to be unable to produce, because it would not fault.
; -----------------------------------------------------------------------------
mod_free_row:
    push bx
    cmp word [bx+MODR_SEG], 0
    je .out
    call mod_disarm
    mov dx, [bx+MODR_SEG]
    mov word [bx+MODR_SEG], 0
    mov word [bx+MODR_KB], 0
    push bx
    mov bx, MEM_K_MOD
    call mem_free_x            
    pop bx
.out:
    pop bx
    ret

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
    push di
    call mod_row
    call mod_free_row
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mod_init - point every slot at mod_gone before anything can call one
; clobbers: AX, BX, CX, DI, flags
;
; .bss arrives as whatever was in memory - `-f bin` zeroes nothing - so this
; is not tidiness: without it the first thunk to run before its own mod_need
; would far-call a garbage address. mod_need cannot be trusted to be the only
; door, because a thunk with the test written the wrong way round still
; assembles.
; -----------------------------------------------------------------------------
mod_init_x:
    mov al, MOD_MAX - 1
.each:
    push ax
    call mod_row
    call mod_disarm
    mov word [bx+MODR_SEG], 0
    mov word [bx+MODR_KB], 0
    pop ax
    dec al
    jns .each
    retf

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
mod_fsz:    resw 1              ; the file's byte size, across the read. It
                                ; follows mod_fp, which is how the stride bug
                                ; above announced itself at all - the arm loop
                                ; overwrote it and then compared against it

section .text
