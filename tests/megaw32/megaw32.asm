; =============================================================================
; os8088 - tests/megaw32/megaw32.asm
;
; MEGAW32, a TEST instrument (nothing under tests/ ships): an OSAPI_MEM_HUGE
; claim with a WINDOW EVERY 32KB (BX = 8000h, the Video Player's ring's shape,
; docs/plans/KERN-MEGA-PLAN.md 14.41) MOVED by the pool's compaction WHILE an
; FSXF_RATE hook reads it through its odd windows - KERN-MEGA-PLAN 14.49's
; invariant, tested where it bites. Inside its own bracket (the only place a
; rate hook runs) it:
;
;   1. claims X, 60KB, then W, HG_WKB of windows 32KB apart above it, and
;      fills W through its EVEN windows (a 64KB piece each): every word of
;      piece p is 57h:p
;   2. lets the hook run ~2 s: at every call it reads, in EVERY window k
;      of W, the words at 0 and 7FFEh (piece k / 2) and at 8000h and FFFEh
;      (piece (k + 1) / 2) - an odd window straddles two pieces, which is
;      the whole point
;   3. frees X and claims every free KB: mx_alloc's compaction moves W down
;      by 60KB, a 64KB piece at a time with interrupts on between
;   4. lets the hook run ~2 s more, then reads all of W through every window
;
; A window torn by the move - rebased with its lower piece while its upper
; one is still to copy - reads the OLD piece's tag at 8000h, and the hook
; counts it. KERN-MEGA-PLAN 14.49 holds the hook off from the first piece to
; the last ([sch_rbusy]), so the verdict needs errors 0 AND the hook to have
; run after the move. The longest gap between calls, in periods of 1.72 ms
; (582 Hz), is the hold as the hook saw it.
;
;   MEGAW32: calls CCCC periods PPPP gap GGGG errors EEEE
;   MEGAW32: ok | no NN
;
; NN: 01 not protected mode / the bracket refused; 02 X refused; 03 W
; refused or not 32KB apart; 04 the big claim refused; 05 errors; 06 W's
; bytes after the move; 07 the hook did not run after it.
;
; Run it: OS88_BOCHS_MEGS=4 python3 tools/os88bochs.py sweep build/megaw32.o88
; (it opens its window, enters the bracket at the first paint, and closes
; when done). The deliberate break: build the kernel with mx_cpmove's
; `inc byte [sch_rbusy]` taken out and errors go above 0.
; Prefix mw_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGAW32', mw_entry, 0, OS88_STACK_192 | OS88_HIGH | OS88_HIGHMEM

MW_WKB  equ 512                     ; W's KB: sixteen windows

mw_entry:
    push si
    mov si, mw_tpl
    call OSAPI_WM_CREATE
    mov [mw_win], bx
    pop si
    ret

; mw_paint - W_PAINT, the lock held: the bracket, once
mw_paint:
    cmp byte [mw_done], 0
    jne .o
    mov byte [mw_done], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, si
    mov ax, mw_run
    mov cx, FSXF_RATE
    mov dx, FSX_RATE_MIN            ; 582 Hz
    mov di, mw_hook
    call OSAPI_FSX_RUN
    jnc .ran
    mov byte [mw_code], 0x01
.ran:
    call mw_verdict
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.o:
    ret

; mw_run - the bracket's proc (the UI task): steps 1-4
mw_run:
    mov byte [mw_code], 0x02
    mov ax, 60
    xor bx, bx
    call OSAPI_MEM_HUGE             ; X
    jc .out
    mov [mw_xsel], dx
    mov byte [mw_code], 0x03
    test dl, 4
    jz .out
    mov ax, MW_WKB
    mov bx, 0x8000
    call OSAPI_MEM_HUGE             ; W
    jc .out
    mov [mw_wsel], dx
    or bx, bx
    jnz .out                        ; (not 32KB apart: BX kept)
    call mw_fill
    mov byte [mw_ready], 1          ; the hook reads from here
    call mw_wait
    mov dx, [mw_xsel]
    call OSAPI_MEM_FREE
    mov word [mw_xsel], 0
    mov ax, [mw_calls]
    mov [mw_calls0], ax
    mov byte [mw_code], 0x04
    call OSAPI_XMEM_CAPS            ; every free KB: W moves down by X
    xor bx, bx
    call OSAPI_MEM_HUGE
    jc .stop
    mov [mw_bsel], dx
    mov ax, [mw_calls]
    mov [mw_calls1], ax             ; the hook's calls when the move is done
    call mw_wait
    mov byte [mw_ready], 0
    mov byte [mw_code], 0x05
    cmp word [mw_err], 0
    jne .free
    mov byte [mw_code], 0x07
    mov ax, [mw_calls]
    cmp ax, [mw_calls1]
    jbe .free
    mov byte [mw_code], 0x06
    call mw_chkall
    jc .free
    mov byte [mw_code], 0
.free:
    mov dx, [mw_bsel]
    call OSAPI_MEM_FREE
    mov word [mw_bsel], 0
.stop:
    mov byte [mw_ready], 0
.out:
    mov byte [mw_ready], 0
    mov dx, [mw_wsel]
    or dx, dx
    jz .nw
    call OSAPI_MEM_FREE
.nw:
    mov dx, [mw_xsel]
    or dx, dx
    jz .nx
    call OSAPI_MEM_FREE
.nx:
    ret

; mw_wait - ~2 s of ticks inside the bracket
mw_wait:
    mov cx, 36
.l:
    mov al, FSXW_TICK
    call OSAPI_FSX_WAIT
    loop .l
    ret

; mw_fill - W through its even windows: piece p = 57h:p
mw_fill:
    push es
    mov dx, [mw_wsel]
    xor bx, bx                      ; BL = p
.p:
    mov es, dx
    mov al, bl
    mov ah, 0x57
    xor di, di
    mov cx, 0x8000
    cld
    rep stosw
    add dx, 16
    inc bl
    cmp bl, MW_WKB / 64
    jb .p
    pop es
    ret

; mw_chkall - every window k's first 32KB is piece k / 2: CF = 1 not
mw_chkall:
    push es
    mov dx, [mw_wsel]
    xor bx, bx                      ; BL = k
.w:
    mov es, dx
    mov al, bl
    shr al, 1
    mov ah, 0x57
    xor di, di
    mov cx, 0x4000
    cld
    repe scasw
    jne .bad
    add dx, 8
    inc bl
    cmp bl, MW_WKB / 32
    jb .w
    pop es
    clc
    ret
.bad:
    pop es
    stc
    ret

; mw_hook - FSXF_RATE's hook: AX = the periods since the last call, IF = 0,
; DS = ours, 48 bytes of stack and no kernel call. Every window, each call
mw_hook:
    cmp byte [mw_ready], 0
    je .o
    inc word [mw_calls]
    add [mw_pers], ax
    cmp ax, [mw_gap]
    jbe .g
    mov [mw_gap], ax
.g:
    push bx
    push dx
    push es
    xor bx, bx                      ; BL = the window, every one each call
.w:
    mov dx, bx
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, [mw_wsel]
    mov es, dx
    mov dl, bl
    shr dl, 1
    mov dh, 0x57                    ; DX = piece k / 2's word
    cmp [es:0], dx
    jne .e
    cmp [es:0x7FFE], dx
    jne .e
    cmp bl, MW_WKB / 32 - 1         ; the last window ends at the claim's end
    jae .x
    mov dl, bl
    inc dl
    shr dl, 1                       ; DX = piece (k + 1) / 2's
    cmp [es:0x8000], dx
    jne .e
    cmp [es:0xFFFE], dx
    je .n
.e:
    inc word [mw_err]
.n:
    inc bl
    cmp bl, MW_WKB / 32
    jb .w
.x:
    pop es
    pop dx
    pop bx
.o:
    ret

; mw_verdict - the lines on E9, and the verdict on the glass
mw_verdict:
    mov si, mw_s_calls
    call mw_say
    mov ax, [mw_calls]
    call mw_hex4
    mov si, mw_s_pers
    call mw_say
    mov ax, [mw_pers]
    call mw_hex4
    mov si, mw_s_gap
    call mw_say
    mov ax, [mw_gap]
    call mw_hex4
    mov si, mw_s_err
    call mw_say
    mov ax, [mw_err]
    call mw_hex4
    call mw_nl
    push ds                         ; the toast, for a machine with no
    pop es                          ; port E9 (86Box): 'MEGAW32 ok gGGGG
    mov di, mw_tbuf                 ; eEEEE' or 'MEGAW32 no NN gGGGG ...'
    mov si, mw_t_hd
    call mw_cpy
    cmp byte [mw_code], 0
    jne .no
    mov si, mw_s_ok
    call mw_say
    call mw_nl
    mov ax, 'ok'
    stosw
    jmp short .t
.no:
    mov si, mw_s_no
    call mw_say
    mov al, [mw_code]
    call mw_hex2
    call mw_nl
    mov ax, 'no'
    stosw
    mov al, ' '
    stosb
    mov al, [mw_code]
    call mw_thex2
.t:
    mov si, mw_t_g
    call mw_cpy
    mov ax, [mw_gap]
    call mw_thex4
    mov si, mw_t_e
    call mw_cpy
    mov ax, [mw_err]
    call mw_thex4
    xor al, al
    stosb
    mov si, mw_tbuf
    mov cx, 1820
    call OSAPI_TOAST
    ret

; mw_cpy - the string at SI to ES:DI, without its 0
mw_cpy:
    lodsb
    or al, al
    jz .o
    stosb
    jmp short mw_cpy
.o:
    ret
; mw_thex4 / mw_thex2 - AX / AL in hex to ES:DI
mw_thex4:
    push ax
    mov al, ah
    call mw_thex2
    pop ax
mw_thex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 15
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    stosb
    ret

mw_say:
    push ax
.l:
    lodsb
    or al, al
    jz .o
    out 0xE9, al
    jmp short .l
.o:
    pop ax
    ret
mw_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
mw_hex4:
    push ax
    mov al, ah
    call mw_hex2
    pop ax
mw_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 15
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    out 0xE9, al
    ret

mw_tpl:
    dw 300, 26, 128, 40
    dw mw_ttl, mw_paint, 0, 0
mw_ttl:     db 'MegaW32', 0
mw_s_calls: db 'MEGAW32: calls ', 0
mw_s_pers:  db ' periods ', 0
mw_s_gap:   db ' gap ', 0
mw_s_err:   db ' errors ', 0
mw_s_ok:    db 'MEGAW32: ok', 0
mw_s_no:    db 'MEGAW32: no ', 0
mw_t_hd:    db 'MEGAW32 ', 0
mw_t_g:     db ' g', 0
mw_t_e:     db ' e', 0

MW_BSS      equ 64
    OS88_BSS MW_BSS
    OS88_IMAGE_END

mw_win      equ os88_image_end + 0
mw_xsel     equ os88_image_end + 2
mw_wsel     equ os88_image_end + 4
mw_bsel     equ os88_image_end + 6
mw_calls    equ os88_image_end + 8
mw_pers     equ os88_image_end + 10
mw_gap      equ os88_image_end + 12
mw_err      equ os88_image_end + 14
mw_calls0   equ os88_image_end + 16
mw_calls1   equ os88_image_end + 18
mw_ready    equ os88_image_end + 20
mw_done     equ os88_image_end + 21
mw_code     equ os88_image_end + 22
mw_k        equ os88_image_end + 23
mw_tbuf     equ os88_image_end + 24     ; the toast's line, 40 bytes
