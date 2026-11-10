; =============================================================================
; os8088 - tests/megargw/megargw.asm
;
; MEGARGW, a TEST instrument (nothing under tests/ ships): A HUGE CLAIM
; REGROWS, every shape OSAPI_MEM_HUGE gives (docs/plans/MEGA-HUGE-PLAN.md F8,
; KERN-MEGA-PLAN 14.174, SPEC.md 50.3.5). On kern_mega in protected mode a
; huge claim is one run of LDT selectors stepped by 8 over a pool block, over
; a conventional claim (mega_hugec), or over one piece of each (a BRIDGE).
; Before F8 only the first regrew: a bridge was refused, a pool claim the pool
; could not hold at the new size was refused, and a conventional run regrew
; as a plain claim with its windows as they were - the new 64KB past the run
; named by whatever descriptors followed it. Every leg writes a pattern -
; byte (o xor o >> 8 xor 35h k) at window k, offset o - and reads it back by
; the step walk after each regrow:
;
;   L1  a 128KB claim in the pool (HUGE_NOBRIDGE), the pool then filled by
;       OSAPI_XMEM_ALLOC blocks, the claim regrown to 256KB: claim, copy,
;       free - it is the heap's run now, windows 0 and 1 kept, window 3 new;
;   L2  L1's claim freed and a 256KB claim made with the pool still full -
;       mega_hugec's run in the heap - regrown to 384KB (its windows
;       extended) and to 160KB (cut: window 3's entry free, window 2's
;       limit 32KB - 1);
;   L3  the largest bridge, its conventional piece shrunk by 128KB, grown
;       back by 64KB (in place, the windows refitted), then the claim taken
;       below its cut: the pool's one run again.
;
; Every answer lands in MR_RES ('MRGRES01', for 86Box, which has no port E9)
; and on E9 as one line, 'MEGARGW: ' and the words from +8:
;
;   +8  L1 the claim's CF   +10 1 in the pool, 2 not   +12 the filler's KB
;   +14 L1 the regrow's CF  +16 1 window 0 below 1MB now, 2 not
;   +18 1 windows 0 and 1 the pattern, 2 not   +20 window 3 written and read
;   +22 L2 the 384KB regrow's CF   +24 1 windows 0..5 contiguous, 2 not
;   +26 1 windows 0, 1 and 5 the pattern, 2 not
;   +28 L2 the 160KB regrow's CF   +30 1 windows 0..2 contiguous and window
;       3 no longer readable, 2 not contiguous, 3 window 3 still readable
;   +32 window 2's limit (LSL), FFFFh unreadable
;   +34 L3 the ask's KB     +36 the claim's CF     +38 its BX (P, the cut)
;   +40 the shrink's CF     +42 1 windows P-1 and P the pattern, 2 not
;   +44 the grow's CF       +46 1 windows P-1, P the pattern and P.. contiguous,
;       2 the pattern wrong, 3 not contiguous
;   +48 down below the cut: CF   +50 1 every window above 1MB and window
;       P-2 the pattern, 2 a window below 1MB, 3 the pattern wrong
;   +52 1 = done (written last)
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGARGW', mr_entry, 0, OS88_STACK_DEFAULT | OS88_HIGH | OS88_HIGHMEM

MR_NOBR     equ 2                   ; HUGE_NOBRIDGE
MR_NFILL    equ 6                   ; filler blocks at most

mr_entry:
    push si
    push di
    push bp
    mov si, mr_tpl
    call OSAPI_WM_CREATE
    jc .out0
    mov [mr_wp], bx                 ; THE WINDOW, for the entry's return:
                                    ; SPEC.md 20.2 - the loader binds BX, and
                                    ; the asks below leave MEMH_AVAIL's total
                                    ; there (KERN-MEGA-PLAN 14.176)
    OS88_REGION_MOVABLE
    ; --- L1: 128KB in the pool, the pool filled, regrown to 256KB ---------
    mov ax, 128
    mov bx, MR_NOBR
    call OSAPI_MEM_HUGE
    sbb si, si
    mov [mr_res+8], si
    jnc .l1
    jmp .l3
.l1:
    mov [mr_a], dx
    call mr_lin                     ; DX:AX = window 0's base
    mov word [mr_res+10], 2
    cmp dx, 0x10
    jb .l1c
    mov word [mr_res+10], 1
.l1c:
    mov dx, [mr_a]
    xor ax, ax
    call mr_fill
    inc ax
    call mr_fill
    xor bp, bp                      ; the filler: the pool's best run, again
    mov di, mr_fill_tok             ; and again
.f:
    cmp bp, MR_NFILL
    jae .fd
    xor ax, ax
    mov bx, 1                       ; MEMH_AVAIL: DX = the pool's best run
    call OSAPI_MEM_HUGE
    jc .fd
    cmp dx, 8
    jb .fd
    add [mr_res+12], dx
    mov ax, dx                      ; DX:AX = its bytes
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    call OSAPI_XMEM_ALLOC
    jc .fd
    mov [di], ax
    mov [di+2], dx
    add di, 4
    inc bp
    jmp short .f
.fd:
    mov si, mr_s_e90
    call mr_dump
    mov dx, [mr_a]
    mov ax, 256 * 64
    mov bl, MEMP_REGROW
    call OSAPI_MEM_P
    sbb si, si
    mov [mr_res+14], si
    jc .l1x
    mov [mr_a], dx
    call mr_lin
    mov word [mr_res+16], 2
    cmp dx, 0x10
    jae .l1p
    mov word [mr_res+16], 1
.l1p:
    mov dx, [mr_a]
    xor ax, ax
    call mr_chk2                    ; windows 0 and 1: AX = 1 / 2
    mov [mr_res+18], ax
    mov ax, 3
    call mr_fill
    call mr_chk
    mov [mr_res+20], ax
    mov si, mr_s_e91                ; (a line a leg: a fault names its leg)
    call mr_dump
.l1x:
    mov dx, [mr_a]                  ; L1's claim back: L2 claims its own
    call OSAPI_MEM_FREE
    ; --- L2: a conventional run (the pool still full), to 384KB and 160KB -
    mov ax, 256
    mov bx, MR_NOBR
    call OSAPI_MEM_HUGE
    jc .l1fz
    mov [mr_a], dx
    xor ax, ax
    call mr_fill
    inc ax
    call mr_fill
    mov ax, 384 * 64
    mov bl, MEMP_REGROW
    call OSAPI_MEM_P
    sbb si, si
    mov [mr_res+22], si
    jc .l1f
    mov [mr_a], dx
    mov cx, 6
    call mr_contig
    mov [mr_res+24], ax
    cmp ax, 1                       ; (not a run: window 5 is not to be
    jne .l2b                        ; loaded at all)
    mov ax, 5
    call mr_fill
    xor ax, ax
    call mr_chk2
    cmp ax, 1
    jne .l2b
    mov ax, 5
    call mr_chk
.l2b:
    mov [mr_res+26], ax
    mov ax, 160 * 64
    mov bl, MEMP_REGROW
    call OSAPI_MEM_P
    sbb si, si
    mov [mr_res+28], si
    jc .l1f
    mov [mr_a], dx
    mov cx, 3
    call mr_contig
    cmp ax, 1
    jne .l2c
    mov bx, dx
    add bx, 3 * 8
    db 0x0F, 0x00, 0xE3             ; verr bx: ZF = 1 readable
    jnz .l2c
    mov ax, 3
.l2c:
    mov [mr_res+30], ax
    mov bx, dx
    add bx, 2 * 8
    mov ax, 0xFFFF
    db 0x0F, 0x03, 0xC3             ; lsl ax, bx
    mov [mr_res+32], ax
    mov si, mr_s_e92
    call mr_dump
.l1f:
    mov dx, [mr_a]                  ; L2's claim back...
    call OSAPI_MEM_FREE
.l1fz:
    mov si, mr_fill_tok             ; ...and the filler
.ff:
    cmp si, di
    jae .ffd
    mov ax, [si]
    mov dx, [si+2]
    call OSAPI_XMEM_FREE
    add si, 4
    jmp short .ff
.ffd:
.l3:
    ; --- L3: the largest bridge -----------------------------------------
    xor ax, ax
    xor bx, bx
    call OSAPI_MEM_HUGE
    jc .say
    mov [mr_res+34], ax
    mov [mr_m], ax
    xor bx, bx
    call OSAPI_MEM_HUGE
    sbb si, si
    mov [mr_res+36], si
    jc .say
    mov [mr_res+38], bx
    mov [mr_a], dx
    mov [mr_p], bx
    cmp bx, 2
    jb .l3f                         ; (no bridge: nothing past a cut)
    mov ax, bx
    sub ax, 2
    call mr_fill                    ; windows P-2, P-1, P
    inc ax
    call mr_fill
    inc ax
    call mr_fill
    mov ax, [mr_m]
    sub ax, 128
    mov bl, MEMP_REGROW
    call mr_kb       
    sbb si, si
    mov [mr_res+40], si
    jc .l3f
    mov [mr_a], dx
    mov ax, [mr_p]
    dec ax
    call mr_chk2
    mov [mr_res+42], ax
    mov ax, [mr_m]
    sub ax, 64
    mov bl, MEMP_REGROW
    call mr_kb       
    sbb si, si
    mov [mr_res+44], si
    jc .l3f
    mov [mr_a], dx
    mov ax, [mr_p]
    dec ax
    call mr_chk2
    cmp ax, 1
    jne .l3g
    mov ax, [mr_m]                  ; the conventional piece's windows:
    sub ax, 64                      ; window P on, contiguous
    mov cl, 6
    shr ax, cl
    sub ax, [mr_p]                  ; (its windows, whole)
    mov cx, ax
    mov ax, 1                       ; (none whole: nothing to compare)
    jcxz .l3g
    mov ax, [mr_p]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add dx, ax
    call mr_contig
    mov dx, [mr_a]
    cmp ax, 1
    je .l3g
    mov ax, 3
.l3g:
    mov [mr_res+46], ax
    mov ax, [mr_p]                  ; below the cut: windows 0..P-2, in
    dec ax                          ; paragraphs (64KB a window)
    mov cl, 12
    shl ax, cl
    mov bl, MEMP_REGROW
    call OSAPI_MEM_P
    sbb si, si
    mov [mr_res+48], si
    jc .l3f
    mov [mr_a], dx
    mov cx, [mr_p]
    dec cx
    mov word [mr_res+50], 1
.hi:
    push dx
    call mr_lin
    cmp dx, 0x10
    pop dx
    jae .hn
    mov word [mr_res+50], 2
.hn:
    add dx, 8
    loop .hi
    cmp word [mr_res+50], 1
    jne .l3f
    mov dx, [mr_a]
    mov ax, [mr_p]
    sub ax, 2
    call mr_chk
    cmp ax, 1
    je .l3f
    mov word [mr_res+50], 3
.l3f:
    mov dx, [mr_a]
    call OSAPI_MEM_FREE
.say:
    mov word [mr_res+52], 1
    mov si, mr_s_e9
    call mr_dump
    mov bx, [mr_wp]                 ; BX = the window (SPEC.md 20.2)
    clc                             ; (CF = 1 out of an entry proc is a
.out0:                              ; refusal: the instance would be torn down
                                    ; before a row on 86Box reads MR_RES)
    pop bp
    pop di
    pop si
    ret

mr_paint:
    ret

; mr_lin - DX = a selector -> DX:AX = its linear base (0 refused)
mr_lin:
    push bx
    mov bl, SEG_LIN
    call OSAPI_SEG
    jnc .o
    xor ax, ax
    xor dx, dx
.o:
    pop bx
    ret

; mr_fill - DX = the claim's first selector, AX = a window k: its 64KB the
; pattern. Keeps all
mr_fill:
    push ax
    push bx
    push cx
    push di
    push es
    call mr_win                     ; ES = window k, BL = 35h x k
    xor di, di
    xor cx, cx                      ; (65,536)
    cld
.l:
    mov ax, di
    xor al, ah
    xor al, bl
    stosb
    loop .l
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; mr_win - DX = the first selector, AX = k -> ES = window k, BL = 35h x k
mr_win:
    push ax
    push dx
    mov bl, 0x35
    mul bl
    mov bl, al
    pop dx
    pop ax
    push ax
    push dx
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add dx, ax
    mov es, dx
    pop dx
    pop ax
    ret

; mr_chk - DX, AX = k -> AX = 1 window k is the pattern, 2 not. Keeps the rest
mr_chk:
    push bx
    push cx
    push di
    push es
    call mr_win
    xor di, di
.l:
    mov ax, di
    xor al, ah
    xor al, bl
    cmp [es:di], al
    jne .bad
    inc di
    jnz .l
    mov ax, 1
    jmp short .o
.bad:
    mov ax, 2
.o:
    pop es
    pop di
    pop cx
    pop bx
    ret

; mr_chk2 - DX, AX = k -> AX = 1 windows k and k+1 the pattern, 2 not
mr_chk2:
    push bx
    mov bx, ax
    call mr_chk
    cmp ax, 1
    jne .o
    lea ax, [bx+1]
    call mr_chk
.o:
    pop bx
    ret

; mr_contig - DX = a window, CX = n -> AX = 1 windows DX.. DX + 8(n-1) are
; based 64KB apart, 2 not. Keeps the rest
mr_contig:
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, dx
    call mr_lin
    mov si, ax                      ; DI:SI = window 0's base
    mov di, dx
    mov ax, 1
.l:
    dec cx
    jz .o
    add bx, 8
    inc di
    mov dx, bx
    push ax
    call mr_lin
    cmp ax, si
    pop ax
    jne .bad
    cmp dx, di
    je .l
.bad:
    mov ax, 2
.o:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; mr_dump - SI = a tag: it and MR_RES's words from +8 onto E9, one line
mr_dump:
    lodsb
    or al, al
    jz .w
    out 0xE9, al
    jmp short mr_dump
.w:
    mov si, mr_res+8
    mov cx, 23
.l:
    lodsw
    call mr_hex4
    mov al, ' '
    out 0xE9, al
    loop .l
    mov al, 10
    out 0xE9, al
    ret
mr_hex4:
    push ax
    mov al, ah
    call mr_hex2
    pop ax
mr_hex2:
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

mr_tpl:
    dw 8, 400, 200, 40
    dw mr_ttl, mr_cls
; its CLASS (SPEC.md 11.104): every handler, and the flags it is born with
    OS88_CLASS mr_cls, 0, PAINT, mr_paint
mr_ttl:     db 'MegaRgw', 0
mr_s_e9:    db 'MEGARGW: ', 0
mr_s_e90:   db 'MEGARGW0: ', 0
mr_s_e91:   db 'MEGARGW1: ', 0
mr_s_e92:   db 'MEGARGW2: ', 0

mr_res:     db 'MRGRES01'
            times 48 db 0
mr_a        dw 0
mr_wp       dw 0                ; the window OSAPI_WM_CREATE made
mr_m        dw 0
mr_p        dw 0
mr_fill_tok times MR_NFILL * 4 db 0

; mr_kb - OSAPI_MEM_P's verb BL with AX in KB (and, with MEMP_CONV, CX the
; head in KB): this test plans in KB. AX, CX and SI kept
mr_kb:
    push ax
    push cx
    push si
    mov si, cx
    mov cl, 6
    shl ax, cl
    shl si, cl
    mov cx, si
    call OSAPI_MEM_P
    pop si
    pop cx
    pop ax
    ret

OS88_IMAGE_END
