; =============================================================================
; os8088 - tests/megahpk/megahpk.asm
;
; MEGAHPK, a TEST instrument (nothing under tests/ ships): A PACKED FILE PAST
; 64KB INTO A HUGE CLAIM (MEGA-HUGE-PLAN F4, KERN-MEGA-PLAN 14.171). It reads
; three host-made 'CZ' files off C:'s RUN folder, each into an OSAPI_MEM_HUGE
; claim of exactly the KB the read says it needs, and walks what arrived by
; the step the claim answered, summing it - the host has the same sums of the
; originals. Every result on port E9, one line each:
;
;   MEGAHPK: HP1.BIN kb KKKK sel SSSS step CCCC cut BBBB
;   MEGAHPK: HP1.BIN read HHHH LLLL sum AAAA BBBB    the bytes, and the sums
;   MEGAHPK: HP1.BIN refused EEEE                    the read's FERR_*
;   MEGAHPK: pool PPPP fill FFFF                     the pool's best run, and
;                                                    the filler that leaves
;                                                    HP_LEAVE KB of it
;   MEGAHPK: HP3.BIN at 4096 refused EEEE | read ... the off-grid read
;   MEGAHPK: done | no NN
;
; HP1.BIN goes into a ONE-RUN claim (BX = HUGE_NOBRIDGE): >128KB, matches
; across its 64KB windows. Then a FILLER takes all but HP_LEAVE KB of the
; pool's best run, so the next two claims BRIDGE: HP2.BIN packs badly, so its
; stream starts before the cut (the seam's case), and HP3.BIN packs well, so
; its stream is all in the conventional piece. Last, HP3.BIN again into the
; bridge 4096 bytes in: a cut off the 64KB grid of X, which is refused
; FERR_PROT. The sum: s1 += byte, s2 += s1, both 16-bit.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGAHPK', hp_entry, 0, OS88_STACK_DEFAULT | OS88_HIGH | OS88_HIGHMEM

HP_LEAVE    equ 192                 ; KB of the pool's best run left free

hp_entry:
    mov si, hp_f1                   ; --- one run ---------------------------
    mov bx, HUGE_NOBRIDGE
    xor di, di
    call hp_leg
    xor ax, ax                      ; --- the filler -------------------------
    mov bx, MEMH_AVAIL
    call OSAPI_MEM_HUGE             ; DX = the pool's best run, KB
    mov cl, 1
    jc .no
    mov si, hp_s_pool
    call hp_say
    mov ax, dx
    call hp_hex4
    sub dx, HP_LEAVE
    jbe .nof
    mov ax, dx
    mov bx, HUGE_NOBRIDGE
    call OSAPI_MEM_HUGE
    jc .nof
    mov [hp_fill], dx
.nof:
    mov si, hp_s_fill
    call hp_say
    mov ax, [hp_fill]
    call hp_hex4
    call hp_nl
    mov si, hp_f2                   ; --- two bridges ------------------------
    xor bx, bx
    xor di, di
    call hp_leg
    mov si, hp_f3
    xor bx, bx
    xor di, di
    call hp_leg
    mov si, hp_f3                   ; --- the cut off X's grid ---------------
    xor bx, bx
    mov di, 4096
    call hp_leg
    mov dx, [hp_fill]
    or dx, dx
    jz .nff
    call OSAPI_MEM_FREE
.nff:
    mov si, hp_s_done
    call hp_say
    call hp_nl
    stc
    ret
.no:
    mov si, hp_s_no
    call hp_say
    mov al, cl
    call hp_hex2
    call hp_nl
    stc
    ret

; hp_leg - SI = the file's name, BX = the claim's ask, DI = the offset in the
; claim the read lands at (0, or the off-grid case). One line or two on E9.
; Clobbers everything but DS
hp_leg:
    mov [hp_name], si
    mov [hp_ask], bx
    mov [hp_off], di
    push ds                         ; the size: a read with no room says it
    pop es
    xor bx, bx
    xor cx, cx
    xor dx, dx
    call OSAPI_FILE_READ
    jnc .bad0
    cmp ax, FERR_BIG
    jne .ref
    add dx, 4                       ; (4KB over, for the off-grid leg)
    mov [hp_kb], dx
    mov ax, dx
    mov bx, [hp_ask]
    call OSAPI_MEM_HUGE
    jnc .got
    mov ax, 0xFFFF
    jmp .ref
.got:
    mov [hp_sel], dx
    mov [hp_step], cx
    mov [hp_cut], bx
    call hp_name_say
    mov si, hp_s_kb
    call hp_say
    mov ax, [hp_kb]
    call hp_hex4
    mov si, hp_s_sel
    call hp_say
    mov ax, [hp_sel]
    call hp_hex4
    mov si, hp_s_step
    call hp_say
    mov ax, [hp_step]
    call hp_hex4
    mov si, hp_s_cut
    call hp_say
    mov ax, [hp_cut]
    call hp_hex4
    mov si, hp_s_lin
    call hp_say
    push bx
    mov dx, [hp_sel]
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX = the first window's base
    pop bx
    xchg ax, dx
    call hp_hex4
    xchg ax, dx
    call hp_hex4
    call hp_nl
    mov ax, [hp_kb]                 ; the capacity: KB x 1024, less the offset
    mov dx, ax
    mov cl, 6
    shr dx, cl
    mov cl, 10
    shl ax, cl
    sub ax, [hp_off]
    sbb dx, 0
    mov cx, ax
    mov es, [hp_sel]
    mov bx, [hp_off]
    mov si, [hp_name]
    call OSAPI_FILE_READ
    push ds
    pop es
    jc .rfree
    mov [hp_nlo], ax
    mov [hp_nhi], dx
    push dx
    push ax
    call hp_sum                     ; (spends the count)
    call hp_name_say
    mov si, hp_s_read
    call hp_say
    pop cx
    pop ax
    call hp_hex4
    mov al, ' '
    out 0xE9, al
    mov ax, cx
    call hp_hex4
    mov si, hp_s_sum
    call hp_say
    mov ax, [hp_s1]
    call hp_hex4
    mov al, ' '
    out 0xE9, al
    mov ax, [hp_s2]
    call hp_hex4
    call hp_nl
    jmp short .free
.rfree:
    push ax
    call hp_name_say
    mov si, hp_s_ref
    call hp_say
    pop ax
    call hp_hex4
    call hp_nl
.free:
    mov dx, [hp_sel]
    call OSAPI_MEM_FREE
    ret
.bad0:
    push ax                         ; a read into no room that succeeded:
    call hp_name_say                ; what it said it read
    mov si, hp_s_r0
    call hp_say
    mov ax, dx
    call hp_hex4
    pop ax
    call hp_hex4
    call hp_nl
    ret
.ref:
    push ax
    call hp_name_say
    mov si, hp_s_ref
    call hp_say
    pop ax
    call hp_hex4
    call hp_nl
    ret

; hp_sum - [hp_nhi]:[hp_nlo] bytes from [hp_sel] + [hp_off], walked 32KB at a
; time by the step -> [hp_s1], [hp_s2]. Clobbers AX BX CX DX SI DI ES
hp_sum:
    xor ax, ax
    mov [hp_s1], ax
    mov [hp_s2], ax
    mov ax, [hp_sel]
    mov [hp_w], ax
    mov di, [hp_off]                ; (0 here: the off-grid leg is refused)
.c:
    mov cx, 0x8000
    cmp word [hp_nhi], 0
    jne .full
    cmp [hp_nlo], cx
    jae .full
    mov cx, [hp_nlo]
    jcxz .done
.full:
    sub [hp_nlo], cx
    sbb word [hp_nhi], 0
    mov es, [hp_w]
    mov si, di
    mov bx, [hp_s1]
    mov dx, [hp_s2]
    xor ah, ah
.b:
    mov al, [es:si]
    inc si
    add bx, ax
    add dx, bx
    loop .b
    mov [hp_s1], bx
    mov [hp_s2], dx
    xor di, 0x8000
    jnz .c                          ; the window's second half
    mov ax, [hp_w]
    add ax, [hp_step]               ; ...or the next window, by the step
    mov [hp_w], ax
    jmp short .c
.done:
    push ds
    pop es
    ret

hp_name_say:
    mov si, hp_s_hdr
    call hp_say
    mov si, [hp_name]
    call hp_say
    cmp word [hp_off], 0
    je .o
    mov si, hp_s_at
    call hp_say
.o:
    ret
hp_say:
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
hp_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
hp_hex4:
    push ax
    mov al, ah
    call hp_hex2
    pop ax
hp_hex2:
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

hp_f1:      db 'HP1.BIN', 0
hp_f2:      db 'HP2.BIN', 0
hp_f3:      db 'HP3.BIN', 0
hp_s_hdr:   db 'MEGAHPK: ', 0
hp_s_at:    db ' at 4096', 0
hp_s_kb:    db ' kb ', 0
hp_s_sel:   db ' sel ', 0
hp_s_step:  db ' step ', 0
hp_s_cut:   db ' cut ', 0
hp_s_lin:   db ' lin ', 0
hp_s_read:  db ' read ', 0
hp_s_sum:   db ' sum ', 0
hp_s_ref:   db ' refused ', 0
hp_s_r0:    db ' read into none ', 0
hp_s_pool:  db 'MEGAHPK: pool ', 0
hp_s_fill:  db ' fill ', 0
hp_s_done:  db 'MEGAHPK: done', 0
hp_s_no:    db 'MEGAHPK: no ', 0

hp_fill:    dw 0
hp_name:    dw 0
hp_ask:     dw 0
hp_off:     dw 0
hp_kb:      dw 0
hp_sel:     dw 0
hp_step:    dw 0
hp_cut:     dw 0
hp_nlo:     dw 0
hp_nhi:     dw 0
hp_s1:      dw 0
hp_s2:      dw 0
hp_w:       dw 0

OS88_IMAGE_END
