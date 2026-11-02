; =============================================================================
; os8088 - tests/megarmb/megarmbe.asm
;
; MEGARMBE, a TEST instrument (nothing under tests/ ships): EMS.DRV through a
; REAL-MODE BRACKET (SPEC.md 53.2.3 and 107, docs/plans/MEGA-VIDEO-REALMODE-
; PLAN.md W3). The door copies a DRVC_EMS driver the list names below 1MB at
; the freeze, serves OSAPI_DRV_CALL inside through the twin (rms_drvx), and
; copies the image AND ITS STATE back up at the exit - so what a package
; allocates and maps on one side of the bracket must be what the driver knows
; on the other, and what it writes through the frame must be in the board.
; OS88_HIGH, so on kern_mega its region is above 1MB and the bracket runs a
; copy of it as well (W1).
;
; Its window's timer runs it once, in three parts, every answer a word of the
; result block (tests/vidrmbems.py reads it off the package's memory - 86Box
; has no port E9):
;
;   BEFORE, protected mode: IDENT, CAPS, ALLOC 4 pages (H1), FRAME all four
;     quarters (a SELECTOR), H1's pages 0 and 1 into quarters 0 and 1, each
;     filled with its own pattern and checked
;   R, a bracket with FSXL_DRV DRVC_EMS: real mode inside; IDENT and CAPS
;     through the twin (the frame now a PARAGRAPH, no quarter free); BASE H1
;     - the protected-mode handle is the caller's still; H1 page 0 read
;     through quarter 0 as protected mode left it; ALLOC 2 more (H2); H2's
;     pages into quarters 2 and 3, filled and checked; H1 page 0 mapped into
;     quarter 1 and seen there (one page, two windows), and page 1 back
;   N, a bracket with an EMPTY list: DRVC_EMS not carried, so DRV_CALL
;     answers no driver (CF, AX = 0)
;   AFTER, protected mode: CAPS counts H2 (made INSIDE) and no quarter free;
;     BASE H2 its run; FRAME again (a selector); all four quarters as real
;     mode left them, unmapped; H2 page 1 MAPPED into quarter 0 and seen - the
;     driver's state works after the copy back up; FREE both, UNFRAME, and
;     CAPS back to where it started
;
; me_err keeps the FIRST verb that answered CF = 1, as leg << 8 | verb (leg
; 1 before, 2 R, 3 after); every answer itself is judged by the harness.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGARMBE', me_entry, 0, OS88_STACK_192 | OS88_HIGH

ME_WAIT     equ 36                  ; the timer: two seconds after launch
ME_SPLEN    equ 8192                ; a pattern: the 16 KB page, in words

me_entry:
    mov si, me_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [me_win], bx
    mov ax, me_tick
    call OSAPI_WM_ONTIMER
    mov ax, ME_WAIT
    call OSAPI_WM_TIMER
    clc
.out:
    ret

me_paint:
    ret

; me_call - BL = an EMSV_ verb, AX/CX/DX its arguments: OSAPI_DRV_CALL to
; DRVC_EMS. out CF, AX, CX, DX, SI as the driver answers; BX kept. A CF = 1
; is recorded in [me_err] (the first only) as [me_leg] << 8 | verb
me_call:
    push bx
    mov bh, DRVC_EMS
    call OSAPI_DRV_CALL
    pop bx
    jnc .o
    pushf
    push ax
    cmp word [me_err], 0
    jne .k
    mov al, bl
    mov ah, [me_leg]
    mov [me_err], ax
.k:
    pop ax
    popf
.o:
    ret

; me_map - AL = a quarter, DX = a handle, CX = its page: MAP
me_map:
    mov bl, EMSV_MAP
    jmp me_call

; me_qoff - AL = a quarter -> DI = its offset in the frame
me_qoff:
    mov ah, al
    xor al, al
    shl ah, 1
    shl ah, 1
    shl ah, 1
    shl ah, 1
    shl ah, 1
    shl ah, 1                       ; AX = quarter x 4000h
    xchg ax, di
    ret

; me_fill - ES = the frame, AL = a quarter, AH = a salt: the quarter's 8192
; words, word k = k XOR (salt x 0101h). me_chk - the same compared: CX = the
; words wrong. Both keep everything else
me_fill:
    push ax
    push cx
    push dx
    push di
    mov dl, ah
    mov dh, ah
    call me_qoff
    xor ax, ax
    mov cx, ME_SPLEN
    cld
.l:
    push ax
    xor ax, dx
    stosw
    pop ax
    inc ax
    loop .l
    pop di
    pop dx
    pop cx
    pop ax
    ret
me_chk:
    push ax
    push dx
    push di
    push bp
    mov dl, ah
    mov dh, ah
    call me_qoff
    xor ax, ax
    mov cx, ME_SPLEN
    xor bp, bp
.l:
    push ax
    xor ax, dx
    cmp [es:di], ax
    je .ok
    inc bp
.ok:
    pop ax
    inc di
    inc di
    inc ax
    loop .l
    mov cx, bp
    pop bp
    pop di
    pop dx
    pop ax
    ret

; the timer: a window callback under the gfx lock - the bracket's context
me_tick:
    cmp byte [me_ran], 0
    je .go
    ret
.go:
    mov byte [me_ran], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    ; --- BEFORE: protected mode --------------------------------------------
    mov byte [me_leg], 1
    call OSAPI_CPU_INFO
    mov [me_bfeat], ah
    mov bl, EMSV_IDENT
    call me_call
    mov [me_bident], ax
    jc .done
    mov bl, EMSV_CAPS
    call me_call
    mov [me_bfree], ax
    mov [me_bnpg], cx
    mov [me_bq], si
    mov ax, 4
    mov bl, EMSV_ALLOC
    call me_call
    jc .done
    mov [me_h1], ax
    mov al, 0x0F
    mov bl, EMSV_FRAME
    call me_call
    jc .done
    mov [me_bfr], dx
    mov es, dx
    xor ax, ax                      ; H1 page 0 -> quarter 0
    mov dx, [me_h1]
    xor cx, cx
    call me_map
    jc .done
    mov al, 1                       ; H1 page 1 -> quarter 1
    mov dx, [me_h1]
    mov cx, 1
    call me_map
    jc .done
    mov ax, 0xA100                  ; quarter 0: salt A1
    call me_fill
    mov ax, 0xA201                  ; quarter 1: salt A2
    call me_fill
    mov ax, 0xA100
    call me_chk
    mov [me_bbad], cx
    mov ax, 0xA201
    call me_chk
    add [me_bbad], cx
    ; --- R: a bracket with DRVC_EMS carried ------------------------------
    mov byte [me_leg], 2
    mov bp, me_lr
    mov ax, me_rmain
    call me_run
    mov [me_rcf], dl
    mov [me_rwhy], al
    ; --- N: a bracket with nothing carried --------------------------------
    mov byte [me_leg], 4
    mov bp, me_ln
    mov ax, me_nmain
    call me_run
    mov [me_ncf], dl
    mov [me_nwhy], al
    ; --- AFTER: protected mode --------------------------------------------
    mov byte [me_leg], 3
    call OSAPI_CPU_INFO
    mov [me_afeat], ah
    mov bl, EMSV_CAPS
    call me_call
    mov [me_afree], ax
    mov [me_aq], si
    mov ax, [me_h2]
    mov bl, EMSV_BASE
    call me_call
    mov [me_ab2], ax
    mov [me_al2], cx
    mov ax, [me_h1]
    mov bl, EMSV_BASE
    call me_call
    mov [me_ab1], ax
    mov [me_al1], cx
    mov al, 0x0F                    ; ours already: the frame again
    mov bl, EMSV_FRAME
    call me_call
    jc .done
    mov [me_afr], dx
    mov es, dx
    mov ax, 0xA100                  ; every quarter as real mode left it
    call me_chk
    mov [me_abad], cx
    mov ax, 0xA201
    call me_chk
    add [me_abad], cx
    mov ax, 0xB102
    call me_chk
    add [me_abad], cx
    mov ax, 0xB203
    call me_chk
    add [me_abad], cx
    xor ax, ax                      ; H2 page 1 -> quarter 0, seen there
    mov dx, [me_h2]
    mov cx, 1
    call me_map
    jc .done
    mov ax, 0xB200
    call me_chk
    mov [me_amap], cx
    mov ax, [me_h2]
    mov bl, EMSV_FREE
    call me_call
    mov ax, [me_h1]
    mov bl, EMSV_FREE
    call me_call
    mov al, 0x0F
    mov bl, EMSV_UNFRAME
    call me_call
    mov bl, EMSV_CAPS
    call me_call
    mov [me_afree2], ax
    mov [me_aq2], si
.done:
    mov byte [me_fin], 1
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; me_run - BP = the FSXL_ list, AX = the entry: OSAPI_FSX_RUN, real mode
; asked. out DL = FFh refused, AL = the SI that came back
me_run:
    mov bx, [me_win]
    mov cx, FSXF_REAL
    xor si, si                      ; FSXR_REAL in
    call OSAPI_FSX_RUN
    sbb dl, dl
    mov ax, si
    ret

; R's ENTRY, inside the bracket
me_rmain:
    mov byte [me_rran], 1
    call OSAPI_CPU_INFO
    mov [me_rfeat], ah
    mov bl, EMSV_IDENT
    call me_call
    mov [me_rident], ax
    jc .out
    mov bl, EMSV_CAPS
    call me_call
    mov [me_rfree], ax
    mov [me_rnpg], cx
    mov [me_rcfr], dx
    mov [me_rq], si
    mov ax, [me_h1]                 ; the handle protected mode made
    mov bl, EMSV_BASE
    call me_call
    mov [me_rb1], ax
    mov [me_rl1], cx
    mov al, 0x0F                    ; ours already: the frame, a paragraph
    mov bl, EMSV_FRAME
    call me_call
    jc .out
    mov [me_rfr], dx
    mov es, dx
    mov ax, 0xA100                  ; quarter 0 as protected mode left it
    call me_chk
    mov [me_rbad0], cx
    mov ax, 2                       ; H2, made in here
    mov bl, EMSV_ALLOC
    call me_call
    jc .out
    mov [me_h2], ax
    mov al, 2                       ; H2 page 0 -> quarter 2, H2 page 1 ->
    mov dx, [me_h2]                 ; quarter 3, each filled
    xor cx, cx
    call me_map
    jc .out
    mov al, 3
    mov dx, [me_h2]
    mov cx, 1
    call me_map
    jc .out
    mov ax, 0xB102
    call me_fill
    mov ax, 0xB203
    call me_fill
    mov ax, 0xB102
    call me_chk
    mov [me_rbad1], cx
    mov ax, 0xB203
    call me_chk
    add [me_rbad1], cx
    mov al, 1                       ; H1 page 0 into quarter 1 as well: one
    mov dx, [me_h1]                 ; page, two windows
    xor cx, cx
    call me_map
    jc .out
    mov ax, 0xA101
    call me_chk
    mov [me_rbad2], cx
    mov al, 1                       ; ...and page 1 back where it was
    mov dx, [me_h1]
    mov cx, 1
    call me_map
    jc .out
    mov ax, 0xA201
    call me_chk
    add [me_rbad2], cx
.out:
    ret

; N's ENTRY: nothing carried, so no driver - CF and AX
me_nmain:
    mov byte [me_nran], 1
    call OSAPI_CPU_INFO
    mov [me_nfeat], ah
    mov ax, 0x5555
    mov bx, DRVC_EMS << 8 | EMSV_IDENT
    call OSAPI_DRV_CALL
    sbb dx, dx
    mov [me_nident], ax
    mov [me_nicf], dx
    ret

; THE LISTS (SPEC.md 53.2.3)
me_lr:      db FSXL_DRV, DRVC_EMS, FSXL_END
me_ln:      db FSXL_END
me_tpl:
    dw 8, 300, 200, 40
    dw me_ttl, me_paint, 0, 0
me_ttl:     db 'MegaRMB EMS', 0

; THE RESULT BLOCK, in the bss (tests/vidrmbems.py reads it: its offset is
; os88_image_end, the .bin header's image size). Words unless said
    OS88_BSS ME_RESN
    OS88_IMAGE_END

me_res      equ os88_image_end
me_fin      equ me_res + 0          ; byte: 1 = it ran to the end
me_ran      equ me_res + 1
me_win      equ me_res + 2
me_err      equ me_res + 4          ; the first failure (leg << 8 | verb)
me_leg      equ me_res + 6          ; byte: 1 before, 2 R, 3 after, 4 N
me_h1       equ me_res + 8
me_h2       equ me_res + 10
me_bfeat    equ me_res + 12         ; byte
me_rfeat    equ me_res + 13         ; byte
me_afeat    equ me_res + 14         ; byte
me_nfeat    equ me_res + 15         ; byte
me_bident   equ me_res + 16
me_bfree    equ me_res + 18
me_bnpg     equ me_res + 20
me_bq       equ me_res + 22
me_bfr      equ me_res + 24
me_bbad     equ me_res + 26
me_rcf      equ me_res + 28         ; byte: FFh = FSX_RUN refused
me_rwhy     equ me_res + 29         ; byte: SI's answer
me_rran     equ me_res + 30         ; byte
me_nran     equ me_res + 31         ; byte
me_rident   equ me_res + 32
me_rfree    equ me_res + 34
me_rnpg     equ me_res + 36
me_rcfr     equ me_res + 38         ; CAPS's frame inside
me_rq       equ me_res + 40
me_rb1      equ me_res + 42
me_rl1      equ me_res + 44
me_rfr      equ me_res + 46         ; FRAME's inside
me_rbad0    equ me_res + 48
me_rbad1    equ me_res + 50
me_rbad2    equ me_res + 52
me_ncf      equ me_res + 54         ; byte
me_nwhy     equ me_res + 55         ; byte
me_nident   equ me_res + 56
me_nicf     equ me_res + 58
me_afree    equ me_res + 60
me_aq       equ me_res + 62
me_ab2      equ me_res + 64
me_al2      equ me_res + 66
me_ab1      equ me_res + 68
me_al1      equ me_res + 70
me_afr      equ me_res + 72
me_abad     equ me_res + 74
me_amap     equ me_res + 76
me_afree2   equ me_res + 78
me_aq2      equ me_res + 80
ME_RESN     equ 82
