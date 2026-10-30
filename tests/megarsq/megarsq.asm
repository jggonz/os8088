; =============================================================================
; os8088 - tests/megarsq/megarsq.asm
;
; MEGARSQ, a TEST instrument (nothing under tests/ ships): what STREAMING a
; file costs on kern_mega, the Video Player's way (docs/plans/KERN-MEGA-PLAN.md
; 14.122) - OSAPI_FILE_READ_SEQ in 32 KB calls into ONE 32 KB conventional
; claim, page-safe, as the player's ring-above-1MB bounce ([vp_dbnc]) is
; taken. The file is SQ.DAT beside MEGARSQ, every dword its own offset;
; every chunk is checked against that, and the whole stream is timed with
; the PIT (apps/os88pit.inc - MEGA_BDA is an identity selector, so 40:6C
; reads the same under protected mode). Three passes, each from a SEEK to 0.
; Every result on port E9, one line each:
;
;   MEGARSQ: pass P kb KKKK calls CCCC us UUUUUUUU   per pass (hex)
;   MEGARSQ: bad OOOOOOOO | io EE | no NN             where it broke
;   MEGARSQ: done
;
; ...and the same results in the BIOS data area's intra-application bytes,
; linear 4F0h (MEGA_BDA is an identity selector on kern_mega, 40h on every
; other kernel), for an emulator with no port E9 (86Box): 4F0h 'R' done or
; 'X' broken (0 while it runs), 4F1h the passes, 4F2h a dword of
; microseconds a pass.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGARSQ', rq_entry, 0, OS88_STACK_DEFAULT

RQ_CHUNK    equ 32768
RQ_PASSES   equ 3

rq_entry:
    mov ax, 32
    mov cx, 32                      ; page-safe, all of it: the player's
    call OSAPI_MEM_CLAIM_DMA        ; bounce is taken the same way
    mov cl, 1
    jnc .have
    jmp .no
.have:
    mov [rq_buf], dx
    mov byte [rq_pass], 0
    mov ax, 0x40                    ; the ICA, cleared: nothing yet
    mov es, ax
    mov di, 0xF0
    xor ax, ax
    mov cx, 8
    rep stosw
.pass:
    push ds                         ; --- a seek to 0: a zeroed cursor -----
    pop es
    mov di, rq_cur
    xor ax, ax
    mov cx, FSEQ_SIZE / 2
    rep stosw
    mov [rq_kb], ax
    mov [rq_calls], ax
    mov [rq_off], ax
    mov [rq_off+2], ax
    call pit_now                    ; DX:AX = the start
    mov [rq_t0], ax
    mov [rq_t0+2], dx
.rd:
    mov dx, [rq_buf]
    xor bx, bx
    mov cx, RQ_CHUNK
    push ds
    pop es
    mov di, rq_cur
    mov si, rq_name
    call OSAPI_FILE_READ_SEQ        ; DX:AX = the bytes
    jnc .got
    mov cl, al
    mov si, rq_s_io
    jmp .say
.got:
    or ax, ax
    jz .end
    inc word [rq_calls]
    push ax
    call rq_check                   ; AX bytes at [rq_off]: each dword its
    pop ax                          ; own offset
    jc .bad
    add [rq_off], ax
    adc word [rq_off+2], 0
    mov cl, 10
    shr ax, cl
    add [rq_kb], ax
    jmp short .rd
.end:
    call pit_now
    sub ax, [rq_t0]
    sbb dx, [rq_t0+2]               ; DX:AX = PIT counts (838 ns each)
    mov bx, 838                     ; ...to microseconds: x 838 / 1000
    call rq_mul32                   ; DX:AX = microseconds
    mov [rq_us], ax
    mov [rq_us+2], dx
    push es                         ; ...and into the ICA
    mov bx, 0x40
    mov es, bx
    mov bl, [rq_pass]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov [es:bx+0xF2], ax
    mov [es:bx+0xF4], dx
    mov bl, [rq_pass]
    inc bl
    mov [es:0xF1], bl
    pop es
    mov si, rq_s_pass
    call rq_say
    mov al, [rq_pass]
    call rq_hex2
    mov si, rq_s_kb
    call rq_say
    mov ax, [rq_kb]
    call rq_hex4
    mov si, rq_s_calls
    call rq_say
    mov ax, [rq_calls]
    call rq_hex4
    mov si, rq_s_us
    call rq_say
    mov ax, [rq_us+2]
    call rq_hex4
    mov ax, [rq_us]
    call rq_hex4
    call rq_nl
    inc byte [rq_pass]
    cmp byte [rq_pass], RQ_PASSES
    jae .fin
    jmp .pass
.fin:
    mov dx, [rq_buf]
    call OSAPI_MEM_FREE
    mov al, 'R'
    call rq_ica
    mov si, rq_s_done
    call rq_say
    call rq_nl
    jmp short .done
.bad:
    mov si, rq_s_bad
    call rq_say
    mov ax, [rq_off+2]
    call rq_hex4
    mov ax, [rq_off]
    call rq_hex4
    call rq_nl
    jmp short .done
.no:
    mov si, rq_s_no
.say:
    call rq_say
    mov al, cl
    call rq_hex2
    call rq_nl
.done:
    mov al, 'X'                     ; (a 'R' already there stays)
    call rq_ica
    push ds
    pop es
    stc                             ; nothing to keep open
    ret

; rq_ica - AL = the verdict into 40:F0 unless one is there. Keeps all but
; flags
rq_ica:
    push es
    push bx
    mov bx, 0x40
    mov es, bx
    cmp byte [es:0xF0], 0
    jne .o
    mov [es:0xF0], al
.o:
    pop bx
    pop es
    ret

; rq_mul32 - DX:AX = PIT counts -> DX:AX = microseconds (x 838 / 1000),
; for counts under 2^32 / 838. Clobbers BX, CX
rq_mul32:
    push si
    push di
    mov si, ax                      ; DX:SI = the counts
    mov ax, dx
    mul bx                          ; DX:AX = high x 838
    mov di, ax                      ; (DX is 0 for any run under an hour)
    mov ax, si
    mul bx                          ; DX:AX = low x 838
    add dx, di                      ; DX:AX = counts x 838
    mov cx, 1000
    mov bx, ax
    mov ax, dx
    xor dx, dx
    div cx
    mov si, ax                      ; the high quotient
    mov ax, bx
    div cx                          ; DX = remainder from the high, AX low
    mov dx, si
    pop di
    pop si
    ret

; rq_check - AX = bytes just read into [rq_buf]:0, the file's offset
; [rq_off] -> CF=1 some dword is not its own offset. Clobbers AX, BX, CX, SI
rq_check:
    push es
    push di
    mov es, [rq_buf]
    mov cx, ax
    shr cx, 1
    shr cx, 1                       ; dwords
    xor di, di
    mov ax, [rq_off]
    mov bx, [rq_off+2]
.l:
    cmp [es:di], ax
    jne .bad
    cmp [es:di+2], bx
    jne .bad
    add ax, 4
    adc bx, 0
    add di, 4
    loop .l
    clc
    jmp short .o
.bad:
    stc
.o:
    pop di
    pop es
    ret

%include "os88pit.inc"

rq_say:
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
rq_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
rq_hex4:
    push ax
    mov al, ah
    call rq_hex2
    pop ax
rq_hex2:
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

rq_name:    db 'SQ.DAT', 0
rq_s_pass:  db 'MEGARSQ: pass ', 0
rq_s_kb:    db ' kb ', 0
rq_s_calls: db ' calls ', 0
rq_s_us:    db ' us ', 0
rq_s_bad:   db 'MEGARSQ: bad ', 0
rq_s_io:    db 'MEGARSQ: io ', 0
rq_s_no:    db 'MEGARSQ: no ', 0
rq_s_done:  db 'MEGARSQ: done', 0

rq_buf      dw 0
rq_pass     db 0
rq_kb       dw 0
rq_calls    dw 0
rq_off      dd 0
rq_t0       dd 0
rq_us       dd 0
rq_cur      times FSEQ_SIZE db 0

OS88_IMAGE_END
