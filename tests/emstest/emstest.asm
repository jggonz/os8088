; =============================================================================
; os8088 - tests/emstest/emstest.asm
;
; EMSTEST: SPEC.md 107's gate package - EMS.DRV through OSAPI_DRV_CALL, as a
; package sees it. NOT SHIPPED (tests/ ships nothing); `make emstest` puts it
; on a system disk that wants the driver, and tests/ems.py drives it.
;
; Every launch runs ONE sequence in its entry proc and leaves the answers in
; et_res, a table of words the harness reads out of this instance's bss by
; symbol. Which sequence is decided by what the board says: a board with every
; page free is the FIRST instance's, anything else a LATER one's.
;
; FIRST (et_phase = 1):
;   IDENT, CAPS; ALLOC 3 pages; ALLOC 0 (must be EMSE_BAD) and ALLOC of more
;   than the board (must be EMSE_ROOM); FRAME all four quarters; MAP pages
;   0, 1, 2 of the handle into quarters 0, 1, 2 and write each a signature
;   through the frame; MAP page 0 into quarter 3 and read quarter 0's
;   signature there - the page is the same memory through two windows; BASE;
;   then THE RECIPE (SPEC.md 107.4): page 1 into quarter 3 by the package's
;   own OUT, and quarter 3 reads page 1's signature; CAPS again. It keeps the
;   handle and the quarters on purpose: it is CLOSED holding them, and only
;   EMSV_GONE can give them back.
; LATER (et_phase = 2):
;   CAPS; FRAME quarter 0 (another instance holds it: EMSE_BUSY); FREE
;   handle 1 (not this instance's: EMSE_BAD); BX after a call (BH must be
;   DRVC_EMS again, whatever the kernel handed the driver).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'EMSTEST', et_entry

ET_PAGES    equ 3               ; pages the first instance allocates

; et_res's words, by index - tests/ems.py names them the same
R_ICF       equ 0               ; IDENT: CF, then AX
R_IAX       equ 1
R_FREE0     equ 2               ; CAPS: free, total, frame, free quarters
R_TOTAL     equ 3
R_FRAME     equ 4
R_QFREE0    equ 5
R_HND       equ 6               ; ALLOC 3: CF, then the handle (or error)
R_HCF       equ 7
R_A0        equ 8               ; ALLOC 0: CF << 8 | AX
R_ABIG      equ 9               ; ALLOC of more than the board: CF << 8 | AX
R_FRCF      equ 10              ; FRAME 0Fh: CF, CX port, SI step, AL mask
R_PORT      equ 11
R_STEP      equ 12
R_OR        equ 13
R_ALIAS     equ 14              ; page 0 seen through quarter 3: 1 = right
R_BASE      equ 15              ; BASE: the first page, the length
R_BLEN      equ 16
R_RECIPE    equ 17              ; page 1 by the package's own OUT: 1 = right
R_FREE1     equ 18              ; CAPS after: free, free quarters
R_QFREE1    equ 19
R_BUSY      equ 20              ; LATER: FRAME 1 -> CF << 8 | AX
R_FREEX     equ 21              ; LATER: FREE 1 -> CF << 8 | AX
R_BX        equ 22              ; LATER: BX after a call
R_N         equ 23

et_entry:
    push ax
    push cx
    push dx
    push si
    push di
    mov si, et_tpl
    call OSAPI_WM_CREATE        ; BX = the window, the output
    jc .out
    push bx
    call et_run
    pop bx
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; et_call - BL = a verb: OSAPI_DRV_CALL to the EMS class. Preserves BX
et_call:
    mov bh, DRVC_EMS
    call OSAPI_DRV_CALL
    ret

; et_cf - AX = CF (0/1) << 8 | AL
et_cf:
    mov ah, 0
    adc ah, 0
    ret

et_run:
    mov bl, EMSV_IDENT
    call et_call
    sbb cx, cx
    neg cx
    mov [et_res + R_ICF * 2], cx
    mov [et_res + R_IAX * 2], ax
    mov bl, EMSV_CAPS
    call et_call
    jc .done
    mov [et_res + R_FREE0 * 2], ax
    mov [et_res + R_TOTAL * 2], cx
    mov [et_res + R_FRAME * 2], dx
    mov [et_res + R_QFREE0 * 2], si
    mov [et_frame], dx
    cmp ax, cx
    je .first
    jmp .later
.done:
    mov byte [et_phase], 0xFF   ; no driver answered
    ret

.first:
    mov byte [et_phase], 1
    mov ax, ET_PAGES
    mov bl, EMSV_ALLOC
    call et_call
    sbb cx, cx
    neg cx
    mov [et_res + R_HCF * 2], cx
    mov [et_res + R_HND * 2], ax
    mov [et_hnd], ax
    xor ax, ax
    mov bl, EMSV_ALLOC
    call et_call
    call et_cf
    mov [et_res + R_A0 * 2], ax
    mov ax, [et_res + R_TOTAL * 2]
    inc ax
    mov bl, EMSV_ALLOC
    call et_call
    call et_cf
    mov [et_res + R_ABIG * 2], ax
    mov al, 0x0F
    mov bl, EMSV_FRAME
    call et_call
    sbb di, di
    neg di
    mov [et_res + R_FRCF * 2], di
    mov [et_res + R_PORT * 2], cx
    mov [et_res + R_STEP * 2], si
    mov ah, 0
    mov [et_res + R_OR * 2], ax
    ; --- pages 0..2 into quarters 0..2, a signature in each
    push es
    mov es, [et_frame]
    xor cx, cx                  ; CX = the page = the quarter
.sig:
    mov al, cl
    mov dx, [et_hnd]
    mov bl, EMSV_MAP
    call et_call
    jc .nosig
    mov di, cx
    mov ax, cx
    mov ah, 0xE5                ; E5xx: page xx of this handle
    push cx
    mov cl, 14
    shl di, cl                  ; DI = the quarter's offset in the frame
    pop cx
    mov [es:di], ax
.nosig:
    inc cx
    cmp cx, 3
    jb .sig
    ; --- page 0 into quarter 3 as well: the same memory through it
    mov al, 3
    mov dx, [et_hnd]
    xor cx, cx
    mov bl, EMSV_MAP
    call et_call
    xor ax, ax
    cmp word [es:0xC000], 0xE500
    jne .a
    inc ax
.a:
    mov [et_res + R_ALIAS * 2], ax
    ; --- BASE, and THE RECIPE: page 1 into quarter 3 by our own OUT
    mov ax, [et_hnd]
    mov bl, EMSV_BASE
    call et_call
    mov [et_res + R_BASE * 2], ax
    mov [et_res + R_BLEN * 2], cx
    inc ax                      ; page 1 of the handle
    or al, [et_res + R_OR * 2]
    mov dx, [et_res + R_STEP * 2]
    add dx, dx                  ; x 3 quarters: step + step + step
    add dx, [et_res + R_STEP * 2]
    add dx, [et_res + R_PORT * 2]
    out dx, al
    xor ax, ax
    cmp word [es:0xC000], 0xE501
    jne .r
    inc ax
.r:
    mov [et_res + R_RECIPE * 2], ax
    pop es
    mov bl, EMSV_CAPS
    call et_call
    mov [et_res + R_FREE1 * 2], ax
    mov [et_res + R_QFREE1 * 2], si
    ret

.later:
    mov byte [et_phase], 2
    mov al, 1
    mov bl, EMSV_FRAME
    call et_call
    call et_cf
    mov [et_res + R_BUSY * 2], ax
    mov ax, 1
    mov bl, EMSV_FREE
    call et_call
    call et_cf
    mov [et_res + R_FREEX * 2], ax
    mov bl, EMSV_IDENT          ; BX must come back as it went: the driver
    mov bh, DRVC_EMS            ; was handed this instance's SLOT in BH and
    call OSAPI_DRV_CALL         ; puts the class back (SPEC.md 107.1)
    mov [et_res + R_BX * 2], bx
    ret

et_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    mov cx, ax
    add cx, 8
    add dx, 8
    mov si, et_s_first
    cmp byte [et_phase], 1
    je .say
    mov si, et_s_later
    cmp byte [et_phase], 2
    je .say
    mov si, et_s_none
.say:
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- window template (SPEC.md 11) ---------------------------------------------
et_tpl:
    dw 160, 100, 240, 60
    dw et_ttl, et_paint, 0, 0

et_ttl:      db 'EmsTest', 0
et_s_first:  db 'Holding 3 pages, 4 quarters', 0
et_s_later:  db 'A later instance', 0
et_s_none:   db 'No EMS driver answered', 0

    OS88_BSS R_N * 2 + 8
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -----------------------------------
et_res      equ os88_image_end + 0          ; R_N words
et_phase    equ os88_image_end + R_N * 2    ; byte: 1 first, 2 later, FFh none
et_hnd      equ et_phase + 2                ; word: the first's handle
et_frame    equ et_phase + 4                ; word: the frame's segment
