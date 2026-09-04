; =============================================================================
; os8088 - tests/pkgrun/pkgrun.asm
;
; PKGRUN - the capability gate for OSAPI_PKG_RUN (SPEC.md 21.5). A TEST
; package: `make pkgrun` builds it and no shipped floppy carries it, exactly
; like tests/multiseg and tests/wire (SPEC.md 78.9).
;
; THREE CHECKS, on the one image, in order:
;
;   A  the image runs.   HELLO.O88 is read off the disk beside us into a claim
;      of ours and handed to the slot. CF=0 and AX=0, and the KERNEL's own
;      instance table then holds a live record named HELLO - which is what
;      tests/pkgrun.py reads, so the pass is asserted against the kernel and
;      not against this package's opinion of it.
;   B  a corrupt magic is REFUSED.  One byte of the header is spoiled and the
;      same call must answer CF=1 with AL = LD_EBAD (2).
;   C  a PARTS image is REFUSED.  The magic is put back and header flags bit 2
;      is set instead (SPEC.md 20.12): parts are read by a package out of its
;      OWN FILE and there is none here, so this is LD_EBAD as well - and it is
;      a DIFFERENT refusal from B, decided before ld_check_hdr rather than
;      inside it.
;
; THE VERDICT IS A BLOCK AT OFFSET 32, immediately after the 32-byte header
; and before any code, so the host reads it with no map of this package at
; all: find the live instance named PKGRUN, take its I_SPTR, read fourteen
; bytes at offset 32 of that segment. It opens with the tag 'PR', which is
; the debug registry's own habit (SPEC.md 57) - a reader that followed the
; wrong pointer can tell.
;
; THE CHECKS RUN FROM THE WAKE HANDLER AND NOT FROM THE ENTRY PROC, and that
; is a correctness requirement rather than a style: the entry proc runs INSIDE
; ld_start (SPEC.md 21 step 8), so calling the loader from it would re-enter
; [ld_rec] / [ld_base] / [ld_need] and load one package over another's
; bookkeeping. The entry proc creates the window and posts ONE wake to itself
; (OSAPI_WM_WAKE is legal from any context), and ui_task pops it after the
; load has finished - on the UI task, with no lock held, which is the slot's
; documented context.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'PKGRUN', pr_entry

; --- the verdict, at a FIXED offset (see the banner) --------------------------
; It is here, before the entry proc, because a host that has to be told an
; offset has to be told again every time this file changes.
pr_res:
    db 'P', 'R'                     ; +32 the tag
pr_done:    db 0                    ; +34 1 = the wake handler ran to the end
pr_ok:      db 0                    ; +35 bit 0 = A, bit 1 = B, bit 2 = C
pr_cfa:     db 0                    ; +36 the CF each call answered, 0 or 1
pr_cfb:     db 0                    ; +37
pr_cfc:     db 0                    ; +38
pr_ala:     db 0                    ; +39 ...and the AL each one answered
pr_alb:     db 0                    ; +40
pr_alc:     db 0                    ; +41
pr_ferr:    db 0                    ; +42 OSAPI_FILE_READ's FERR_*, 0 = read
pr_len:     dw 0                    ; +43 ...and the bytes it delivered
pr_pad:     db 0                    ; +45
%if ($ - $$) != 46
  %error "the verdict block must start at offset 32 and be 14 bytes: tests/pkgrun.py reads it by ARITHMETIC, not by a map"
%endif

PR_CLAIM_KB equ 4                   ; HELLO.O88 is under a kilobyte; four is
                                    ; room for it to grow without this file
PR_CONT_W  equ 286                  ; content width:  288 outer - 2px borders
PR_CONT_H  equ 57                  ; ...and 76 outer - TITLE_H - 1
PR_ROW_H   equ 12

LD_EBAD    equ 2                    ; SPEC.md 21.4, mirrored - a test package
                                    ; may not include kernel/loader.inc

; -----------------------------------------------------------------------------
; pr_entry - package entry (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, gfx lock NOT held
; out: BX = window ptr, CF clear
; -----------------------------------------------------------------------------
pr_entry:
    push ax
    push si
    mov si, pr_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc .out
    mov [pr_win], bx                ; the wake handler is handed SI = the
                                    ; window, but the repaint below wants it
                                    ; from a path that has spent SI
    mov ax, pr_onwake
    call OSAPI_WM_ONWAKE            ; BX = the window we just created
    call OSAPI_WM_WAKE              ; ...and kick ourselves once. It is legal
                                    ; from any context and the UI task pops it
                                    ; after this load has finished, which is
                                    ; the whole reason the checks are not here
    clc                             ; OSAPI_WM_WAKE answers CF=1 on a full ring
.out:                               ; and that is not this package refusing to
    pop si                          ; start - the loader reads our CF
    pop ax
    ret

; -----------------------------------------------------------------------------
; pr_onwake - the three checks (SPEC.md 21.5)
; in:  SI = our window; UI task, NO gfx lock held
; out: nothing
; -----------------------------------------------------------------------------
pr_onwake:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cmp byte [pr_done], 0
    jne .done                       ; a stale wake after the checks have run

    ; --- the image, off the disk beside us --------------------------------
    mov ax, PR_CLAIM_KB
    call OSAPI_MEM_CLAIM            ; out CF=1 refused, DX = the base segment
    jc .done
    mov [pr_seg], dx
    mov es, dx
    xor bx, bx
    mov cx, PR_CLAIM_KB * 1024
    xor dx, dx                      ; DX:CX = the buffer's capacity
    mov si, pr_s_file
    call OSAPI_FILE_READ            ; out CF=0 and DX:AX = the bytes read
    jnc .read
    mov [pr_ferr], al
    jmp .paint
.read:
    mov [pr_len], ax                ; the low word: PR_CLAIM_KB bounds it, so
                                    ; DX is 0 and this is the whole length

    ; --- A: it runs -------------------------------------------------------
    call pr_run
    mov [pr_cfa], bl
    mov [pr_ala], al
    or bl, bl
    jnz .b
    or al, al
    jnz .b
    or byte [pr_ok], 1

    ; --- B: a corrupt magic is refused ------------------------------------
.b:
    mov es, [pr_seg]
    mov byte [es:0], 0              ; 'O' of the 'O8' magic (SPEC.md 20.2)
    call pr_run
    mov [pr_cfb], bl
    mov [pr_alb], al
    cmp bl, 1
    jne .c
    cmp al, LD_EBAD
    jne .c
    or byte [pr_ok], 2

    ; --- C: a PARTS image is refused --------------------------------------
.c:
    mov es, [pr_seg]
    mov byte [es:0], 'O'            ; the magic back...
    or byte [es:3], 4               ; ...and header flags bit 2 instead
    call pr_run
    mov [pr_cfc], bl
    mov [pr_alc], al
    cmp bl, 1
    jne .free
    cmp al, LD_EBAD
    jne .free
    or byte [pr_ok], 4

.free:
    mov dx, [pr_seg]                ; ours to give back: the image was COPIED
    call OSAPI_MEM_FREE             ; and never adopted (SPEC.md 21.5)
    mov word [pr_seg], 0
.paint:
    mov byte [pr_done], 1
    mov si, [pr_win]                ; the one callback that is NOT under the
    mov bx, si                      ; lock, so it takes it itself for a burst
    call OSAPI_GFX_LOCK             ; it can state (SPEC.md 74.1) - and it may
    call OSAPI_WM_CLIP_SET          ; not draw before it has
    jc .unlock
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov bx, dx
    mov cx, ax
    add cx, PR_CONT_W - 1
    add dx, PR_CONT_H - 1
    call OSAPI_GFX_FILL             ; AX is x1 already
    call pr_paint                   ; SI = the window still
.unlock:
    call OSAPI_GFX_UNLOCK
.done:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pr_run - one call of the slot under test
; in:  [pr_seg] holds the image, [pr_len] its length
; out: BL = 1 the call answered CF=1, else 0; AL = the code it answered
; clobbers: AX, BX, CX, DX, SI, DI, ES
; -----------------------------------------------------------------------------
pr_run:
    mov es, [pr_seg]
    xor si, si                      ; ES:SI = the image
    mov cx, [pr_len]
    xor dx, dx                      ; DX:CX = its length
    mov di, pr_s_file               ; DI = the name, in OUR segment and not in
                                    ; ES - which is the point of the slot's
                                    ; register contract (SPEC.md 21.5)
    call OSAPI_PKG_RUN
    mov bl, 0
    jnc .out
    mov bl, 1
.out:
    ret

; -----------------------------------------------------------------------------
; pr_paint - W_PAINT: the verdict in words, for the eye. The ASSERTIONS are
;            tests/pkgrun.py's reads of the block at offset 32; this is so a
;            person looking at a screenshot can see the same three answers.
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pr_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov bx, ax                      ; BX = the content's left column
    add dx, 6
    mov di, pr_lines                ; DI walks the three names...
    mov cl, 1                       ; ...and CL is the [pr_ok] bit beside each
.row:
    push cx
    mov si, [di]
    mov cx, bx
    add cx, 6
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop cx

    mov si, pr_s_wait
    cmp byte [pr_done], 0
    je .say
    mov si, pr_s_fail
    mov al, [pr_ok]
    test al, cl
    jz .say
    mov si, pr_s_pass
.say:
    push cx
    mov cx, bx
    add cx, 176
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop cx

    add di, 2
    add dx, PR_ROW_H
    shl cl, 1
    cmp di, pr_lines + 6
    jb .row
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) --------------------------
pr_tpl:
    dw 40, 40, 288, 76
    dw pr_ttl, pr_paint, 0, 0

pr_ttl:     db 'PKGRUN', 0
pr_s_file:  db 'HELLO.O88', 0
pr_s_wait:  db 'running...', 0
pr_s_fail:  db 'FAIL', 0
pr_s_pass:  db 'ok', 0
pr_lines:   dw pr_s_a, pr_s_b, pr_s_c
pr_s_a:     db 'A run from memory', 0
pr_s_b:     db 'B bad magic refused', 0
pr_s_c:     db 'C parts refused', 0

    OS88_BSS 4
    OS88_IMAGE_END

pr_seg      equ os88_image_end + 0  ; word: the claim holding the image
pr_win      equ os88_image_end + 2  ; word: our window
