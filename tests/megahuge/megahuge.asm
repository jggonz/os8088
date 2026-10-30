; =============================================================================
; os8088 - tests/megahuge/megahuge.asm
;
; MEGAHUGE, a TEST instrument (nothing under tests/ ships): ONE CLAIM FOR ALL
; OF IT (docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md MC5). It asks OSAPI_MEM_HUGE with
; AX = 0 what one claim can be given, claims exactly that, writes a pattern
; through every 64KB window - the seam between the pool's piece and the
; heap's included - reads it all back, frees it, and asks again. Every result
; on port E9, one line each:
;
;   MEGAHUGE: max NNNN         the KB the query answers
;   MEGAHUGE: claim SSSS step CCCC   the run's first selector and its step
;   MEGAHUGE: first LLLLLLLL last LLLLLLLL   the first and last windows'
;                              linear bases (OSAPI_SEG SEG_LIN)
;   MEGAHUGE: again NNNN       the query's answer after the free
;   MEGAHUGE: ok | bad KKKK OOOO | no NN   the verdict, or where it broke
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGAHUGE', mh_entry, 0, OS88_STACK_DEFAULT | OS88_HIGH | OS88_HIGHMEM

MH_STEPB    equ 64                  ; a pattern word every 64 bytes

mh_entry:
    xor ax, ax                      ; --- the query ---------------------------
    call OSAPI_MEM_HUGE
    mov cl, 1
    jc .no
    mov [mh_kb], ax
    mov si, mh_s_max
    call mh_say
    call mh_hex4
    call mh_nl
    or ax, ax
    mov cl, 2
    jz .no
    xor bx, bx                      ; --- the claim, of exactly that -------
    call OSAPI_MEM_HUGE
    jnc .got                        ; (CX is the answer: no `mov cl` before
    mov cl, 3                       ; it is read)
    jmp .no
.got:
    mov [mh_sel], dx
    mov [mh_step], cx
    mov si, mh_s_claim
    call mh_say
    mov ax, dx
    call mh_hex4
    mov si, mh_s_step
    call mh_say
    mov ax, cx
    call mh_hex4
    call mh_nl
    mov ax, [mh_kb]                 ; windows: KB / 64, rounded up
    add ax, 63
    mov cl, 6
    shr ax, cl
    mov [mh_nwin], ax
    mov si, mh_s_first              ; --- where the first and last are ----
    call mh_say
    mov dx, [mh_sel]
    call mh_lin
    mov si, mh_s_last
    call mh_say
    mov ax, [mh_nwin]
    dec ax
    mul word [mh_step]
    add ax, [mh_sel]
    mov dx, ax
    call mh_lin
    call mh_nl
    mov bp, 0                       ; --- write, window by window ---------
.w:
    call mh_win                     ; ES = window BP, CX = its bytes / 64
    xor di, di
.wl:
    mov ax, bp
    xchg ah, al
    xor ax, di
    mov [es:di], ax
    add di, MH_STEPB
    loop .wl
    inc bp
    cmp bp, [mh_nwin]
    jb .w
    mov bp, 0                       ; --- and read it all back -------------
.r:
    call mh_win
    xor di, di
.rl:
    mov ax, bp
    xchg ah, al
    xor ax, di
    cmp [es:di], ax
    jne .bad
    add di, MH_STEPB
    loop .rl
    inc bp
    cmp bp, [mh_nwin]
    jb .r
    push ds
    pop es
    mov dx, [mh_sel]                ; --- the free, and the question again -
    call OSAPI_MEM_FREE
    mov cl, 4
    jc .no
    xor ax, ax
    call OSAPI_MEM_HUGE
    mov cl, 5
    jc .no
    mov si, mh_s_again
    call mh_say
    call mh_hex4
    call mh_nl
    inc ax                          ; --- ONE KB MORE THAN THE ANSWER IS
    xor bx, bx                      ; REFUSED (KERN-MEGA-PLAN 14.121): the
    call OSAPI_MEM_HUGE             ; answer is the most, so a cache it
    mov si, mh_s_ovn                ; counted is one the claim then sheds
    jc .ovr
    push ds
    pop es
    call OSAPI_MEM_FREE
    mov si, mh_s_ovy
.ovr:
    push ds
    pop es
    call mh_say
    call mh_nl
    mov ax, 64                      ; --- a WHOLE 64KB file into a 64KB pool
    call OSAPI_MEM_CLAIM            ; claim (KERN-MEGA-MEMORY-PLAN 8.6): the
    mov cl, 6                       ; run that ends at the window's end
    jc .no
    mov [mh_c64], dx
    mov si, mh_s_c64
    call mh_say
    call mh_lin
    call mh_nl
    mov es, [mh_c64]                ; (mh_lin spends DX)
    xor bx, bx
    mov dx, 1
    xor cx, cx                      ; DX:CX = 64KB, the file exactly
    mov si, mh_s_f64
    call OSAPI_FILE_READ
    push ds
    pop es
    mov cl, 7
    jc .rno
    mov es, [mh_c64]                ; the bytes: k xor k >> 8, at three places
    mov cl, 8
    cmp byte [es:0x1234], 0x26
    jne .rno
    cmp byte [es:0x8000], 0x80
    jne .rno
    cmp byte [es:0xFFFF], 0x00
    jne .rno
    push ds
    pop es
    mov dx, [mh_c64]
    call OSAPI_MEM_FREE
    mov si, mh_s_r64
    call mh_say
    call mh_nl
    mov si, mh_s_ok
    call mh_say
    call mh_nl
    jmp short .done
.rno:                               ; the 64KB read: refused (AX the error),
    push ds                         ; or the bytes are not the file's
    pop es
    push cx
    mov si, mh_s_r64n
    call mh_say
    call mh_hex4
    call mh_nl
    pop cx
    jmp short .no
.bad:
    push ds
    pop es
    mov si, mh_s_bad
    call mh_say
    mov ax, bp
    call mh_hex4
    mov al, ' '
    out 0xE9, al
    mov ax, di
    call mh_hex4
    call mh_nl
    jmp short .done
.no:
    push ds
    pop es
    mov si, mh_s_no
    call mh_say
    mov al, cl
    call mh_hex2
    call mh_nl
.done:
    stc                             ; nothing to keep open
    ret

; mh_win - BP = a window -> ES = its selector, CX = the pattern words it
; holds (64KB / 64, the last window its claim's end). Clobbers AX, DX
mh_win:
    mov ax, bp
    mul word [mh_step]
    add ax, [mh_sel]
    mov es, ax
    mov cx, 1024                    ; 64KB / 64
    mov ax, bp
    inc ax
    cmp ax, [mh_nwin]
    jb .o
    mov cx, bp                      ; the last: (KB - 64 x BP) x 16 words
    mov ax, 64
    mul cx
    mov cx, [mh_kb]
    sub cx, ax
    mov ax, cx
    mov cl, 4
    shl ax, cl
    mov cx, ax
.o:
    ret

; mh_lin - DX = a selector: its linear base onto E9, eight digits
mh_lin:
    push ax
    push bx
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX
    xchg ax, dx
    call mh_hex4
    xchg ax, dx
    call mh_hex4
    pop bx
    pop ax
    ret

mh_say:
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
mh_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
mh_hex4:
    push ax
    mov al, ah
    call mh_hex2
    pop ax
mh_hex2:
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

mh_s_max:   db 'MEGAHUGE: max ', 0
mh_s_claim: db 'MEGAHUGE: claim ', 0
mh_s_step:  db ' step ', 0
mh_s_first: db 'MEGAHUGE: first ', 0
mh_s_last:  db ' last ', 0
mh_s_again: db 'MEGAHUGE: again ', 0
mh_s_ok:    db 'MEGAHUGE: ok', 0
mh_s_ovn:   db 'MEGAHUGE: over refused', 0
mh_s_ovy:   db 'MEGAHUGE: over GOT', 0
mh_s_bad:   db 'MEGAHUGE: bad ', 0
mh_s_no:    db 'MEGAHUGE: no ', 0
mh_s_c64:   db 'MEGAHUGE: claim64 at ', 0
mh_s_f64:   db 'B64K.BIN', 0
mh_s_r64:   db 'MEGAHUGE: read64 ok', 0
mh_s_r64n:  db 'MEGAHUGE: read64 refused, AX ', 0

mh_kb       dw 0
mh_sel      dw 0
mh_step     dw 0
mh_nwin     dw 0
mh_c64      dw 0

OS88_IMAGE_END
