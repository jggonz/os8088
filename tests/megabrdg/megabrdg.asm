; =============================================================================
; os8088 - tests/megabrdg/megabrdg.asm
;
; MEGABRDG, a TEST instrument (nothing under tests/ ships): EVERY WAY INTO A
; BRIDGED CLAIM AGREES WITH ITS STEP WALK (docs/plans/MEGA-HUGE-PLAN.md F1,
; F2, F9; SPEC.md 50.3.5). On kern_mega in protected mode an OSAPI_MEM_HUGE
; claim that neither the pool nor the heap holds alone BRIDGES them: its first
; P windows over a pool block above 1MB, the rest over a conventional claim,
; ONE run of selectors stepped by 8. Window k (DX + 8k) is right by
; construction; everything that computes a place from a BASE plus an offset
; has to follow the pieces too, or it reaches past the pool piece into
; whatever pool memory follows it. This claims the largest bridge (AX = 0's
; answer), finds the cut by the walk itself (the first window whose SEG_LIN
; is below 1MB), writes a pattern through windows P-1, P and P+1 - byte
; (o xor o >> 8 xor 35h k) at window k, offset o - and reads it back through:
;
;   SEG_SUB, SEG_AT, SEG_HND from window P-1 at 256 bytes before the cut, at
;   1230h past it and at 10040h past it; SEG_TILE across the cut (which no
;   tile can express: refused) and at it; SEG_LIN by the rule (window k's own
;   base plus the offset in it) and naively (the first base plus the whole
;   offset, wrong by construction on a bridge); OSAPI_FILE_WRITE of 96KB from
;   32KB before the cut and OSAPI_FILE_READ of it back to 16KB before it,
;   compared byte for byte, and again at ODD offsets and an odd length, so a
;   sector straddles the cut both ways.
;
; ...and the F2 half: the ask and the claim with BX = HUGE_NOBRIDGE (2) never
; bridge, and the claim answers BX = the pool piece's window count; and F9:
; OSAPI_MEM_MOVABLE of the bridge's DX takes its conventional piece too. The
; claim is kept and the window stays open, so a row reads the result and the
; heap with the claim standing. Every answer lands in MB_RES, an 'MBRRES01'
; block in its own image (86Box has no port E9), and on port E9 (Bochs):
;
;   +0  'MBRRES01'     +8  the ask's KB        +10 the claim's CF (0 ok)
;   +12 windows        +14 P, the walk's cut (FFFFh: no bridge)
;   +16 the claim's BX out (the pool piece's windows since F2; 0 before)
;   +18 +20 +22  SEG_SUB at cut - 100h, + 1230h, + 10040h
;   +24 +26 +28  SEG_AT, the same three     +30 SEG_HND at + 1230h
;   each 1 the pattern, 2 a wrong word, 3 refused (CF = 1, or DX = 0), 0 not
;   asked (no window P+1)
;   +32 SEG_TILE across the cut: 1 refused, 2 given and its second tile wrong,
;       3 given and right                   +34 SEG_TILE at the cut (1/2/3)
;   +36 SEG_LIN by the rule (1/2/3)         +38 SEG_LIN naively (1/2/3)
;   +40 the file, aligned: 1 byte-exact, 2 a byte differs (+42 where, as the
;       offset into the read), 3 the write refused, 4 the read refused
;   +44 the file at odd offsets, +46 where
;   +48 the ask with BX = HUGE_NOBRIDGE (KB) +50 a claim of +8 with it:
;       1 refused, 2 granted                +52 a claim of +48 with it:
;       1 granted, one piece; 2 granted, bridged; 3 refused
;   +54 OSAPI_MEM_MOVABLE of the bridge's DX: its CF
;   +56 window P's linear base (dword): the conventional piece, for the row
;   +60 1 = done (written last)
;   +62 the linear base (dword) of an 80KB conventional claim made BEFORE the
;       bridge and freed after it: the hole a placement at the fill point
;       would leave UNDER the conventional piece (F9)
;   +66 1 = all but the file legs done, and a four-second pause to read the
;       heap in; 2 = the file legs done too
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGABRDG', mb_entry, 0, OS88_STACK_DEFAULT | OS88_HIGH | OS88_HIGHMEM

MB_NOBR     equ 2                   ; HUGE_NOBRIDGE's value (SPEC.md 50.3.5),
                                    ; spelt here so the probe assembles on
                                    ; the tree before it, where it is ignored

mb_entry:
    push si
    push di
    push bp
    mov si, mb_tpl                  ; the window FIRST, so a row on 86Box sees
    call OSAPI_WM_CREATE            ; the program up and reads MB_RES as it
    jc .out0                        ; fills, done or not
    mov [mb_wptr], bx               ; (BX is the window at the return)
    OS88_REGION_MOVABLE
    mov ax, 0xFFFF
    mov [mb_res+14], ax
    xor ax, ax                      ; --- F2: the ask and claims, no bridge -
    mov bx, MB_NOBR
    call OSAPI_MEM_HUGE
    jc .nb0
    mov [mb_res+48], ax
.nb0:
    xor ax, ax                      ; --- the ask, a bridge allowed --------
    xor bx, bx
    call OSAPI_MEM_HUGE
    jnc .ask
    jmp .win
.ask:
    mov [mb_res+8], ax
    mov bx, MB_NOBR                 ; all of it with no bridge: refused
    call OSAPI_MEM_HUGE
    mov word [mb_res+50], 1
    jc .nb1
    mov word [mb_res+50], 2
    call mb_free
.nb1:
    mov ax, [mb_res+48]             ; what the no-bridge ask said: granted,
    or ax, ax                       ; and on one side of 1MB only
    jz .nb3
    mov bx, MB_NOBR
    call OSAPI_MEM_HUGE
    mov word [mb_res+52], 3
    jc .nb3
    push ax
    mov [mb_sel], dx
    mov [mb_step], cx
    call mb_nw                      ; [mb_nwin] for AX KB
    call mb_cut                     ; AX = the first window below 1MB, or
    pop cx                          ; FFFFh
    mov word [mb_res+52], 1
    cmp ax, 0xFFFF
    je .nb2
    or ax, ax
    jz .nb2                         ; (all of it conventional: one piece)
    mov word [mb_res+52], 2
.nb2:
    call mb_free
.nb3:
    mov ax, 80                      ; --- a conventional claim under it ---
    MEMKB_CLAIM                     ; (over 64KB: never the pool's)
    jc .nob1
    mov [mb_b1], dx
    call mb_lin
    mov [mb_res+62], ax
    mov [mb_res+64], dx
    xor ax, ax                      ; the ask again, with it standing
    xor bx, bx
    call OSAPI_MEM_HUGE
    mov [mb_res+8], ax
.nob1:
    mov ax, [mb_res+8]              ; --- the bridge ----------------------
    xor bx, bx
    call OSAPI_MEM_HUGE
    sbb si, si
    mov [mb_res+10], si
    pushf
    push dx
    mov dx, [mb_b1]                 ; ...and the claim under it freed
    or dx, dx
    jz .nob2
    call mb_free
.nob2:
    pop dx
    popf
    jnc .got
    jmp .win
.got:
    mov [mb_res+16], bx
    mov [mb_sel], dx
    mov [mb_step], cx
    call mb_nw
    mov ax, [mb_nwin]
    mov [mb_res+12], ax
    call mb_cut                     ; AX = P
    mov [mb_res+14], ax
    cmp ax, 0xFFFF
    jne .bridged
    jmp .say                        ; one piece: nothing past a cut to ask
.bridged:
    or ax, ax
    jnz .p1
    jmp .say
.p1:
    mov [mb_p], ax
    mov dx, ax                      ; window P's base: the conventional piece
    call mb_wsel
    call mb_lin
    mov [mb_res+56], ax
    mov [mb_res+58], dx
    mov ax, [mb_p]                  ; --- the pattern, windows P-1..P+1 ----
    dec ax
    call mb_fill
    inc ax
    call mb_fill
    inc ax
    call mb_fill
    mov dx, [mb_p]                  ; W = window P-1, the base asked from
    dec dx
    call mb_wsel
    mov [mb_w], dx
    ; --- SEG_SUB / SEG_AT / SEG_HND at three places ---------------------
    mov si, mb_legs
    mov di, mb_res+18
.leg:
    lodsw                           ; AX = paragraphs from W
    mov bp, ax
    lodsw                           ; BX:CX = the window k and offset o it
    mov bx, ax                      ; names: k relative to P
    lodsw
    mov cx, ax
    add bx, [mb_p]
    cmp bx, [mb_nwin]
    jb .lgo
    add di, 2                       ; (no such window: 0, not asked)
    jmp short .lnext
.lgo:
    push si
    mov [mb_k], bx
    mov [mb_o], cx
    mov ax, bp                      ; SEG_SUB
    mov dx, [mb_w]
    xor cx, cx
    mov bl, SEG_SUB
    call OSAPI_SEG
    call mb_rd                      ; (CF in) -> AX = the code
    mov [di], ax
    mov ax, bp                      ; SEG_AT
    mov dx, [mb_w]
    xor cx, cx
    mov bl, SEG_AT
    call OSAPI_SEG
    jc .atr
    or dx, dx
    jnz .atr
    stc                             ; DX = 0: no window, refused
.atr:
    pushf
    call mb_rd
    mov [di+6], ax
    popf
    jc .atn
    mov bl, SEG_FREE
    call OSAPI_SEG
.atn:
    pop si
.lnext:
    add di, 2
    cmp si, mb_legs_end
    jb .leg
    mov word [mb_k], 0              ; SEG_HND at + 1230h
    mov ax, [mb_p]
    mov [mb_k], ax
    mov word [mb_o], 0x1230
    mov si, [mb_w]
    mov ax, 0x1123
    xor cx, cx
    xor dx, dx
    mov bl, SEG_HND
    call OSAPI_SEG
    pushf
    call mb_rd
    mov [mb_res+30], ax
    popf
    jc .hn
    xor si, si                      ; ...and freed
    mov bl, SEG_HND
    call OSAPI_SEG
.hn:
    mov dx, [mb_w]                  ; --- SEG_TILE across the cut: tile 0
    mov ax, 0x0800                  ; would be 32KB of each piece
    mov cx, 2
    mov bl, SEG_TILE
    call OSAPI_SEG
    mov word [mb_res+32], 1
    jc .t1
    add dx, 8                       ; given: is its second tile cut + 8000h?
    mov ax, [mb_p]
    mov [mb_k], ax
    mov word [mb_o], 0x8000
    clc
    call mb_rd
    inc ax                          ; 1 right -> 2... 2 wrong -> 3: swapped
    xor ax, 1                       ; below (2 = wrong, 3 = right)
    mov [mb_res+32], ax
.t1:
    mov dx, [mb_w]                  ; SEG_TILE at the cut: the conventional
    mov ax, 0x1000                  ; piece's first 64KB
    mov cx, 1
    mov bl, SEG_TILE
    call OSAPI_SEG
    mov bx, [mb_p]
    mov [mb_k], bx
    mov word [mb_o], 0
    call mb_rd
    mov [mb_res+34], ax
    mov dx, [mb_p]                  ; --- SEG_LIN by the rule: window P's own
    call mb_wsel                    ; base + 1230h
    call mb_lin
    jc .lr
    add ax, 0x1230
    adc dx, 0
.lr:
    call mb_winl                    ; -> AX = the code
    mov [mb_res+36], ax
    mov dx, [mb_w]                  ; ...and naively: W's base + 11230h
    call mb_lin
    jc .ln
    add ax, 0x1230
    adc dx, 1
.ln:
    call mb_winl
    mov [mb_res+38], ax
    call mb_mov                     ; F9, before the file legs
    mov si, mb_s_e90                ; (a line before the file legs, in case
    call mb_dump                    ; one of them takes the machine down)
    mov word [mb_res+66], 1         ; ...and a pause for a row on 86Box to
    call OSAPI_GET_TICKS            ; read MB_RES and the heap, the same
    mov bx, ax                      ; moment (four seconds)
.pause:
    call OSAPI_GET_TICKS
    sub ax, bx
    cmp ax, 73
    jb .pause
    ; --- the file across the cut, aligned, then odd ---------------------
    mov word [mb_fs], 0x8000        ; from 32KB before the cut...
    mov word [mb_fd], 0xC000        ; ...back to 16KB before it: 96KB,
    mov word [mb_fl], 0x8000        ; so the read is OVER 64KB too, and
    mov word [mb_flh], 1            ; takes the selector arm's own walk
    mov word [mb_fn], mb_s_f1
    call mb_file
    mov [mb_res+40], ax
    mov [mb_res+42], dx
    mov ax, [mb_p]                  ; the pattern again where the read landed
    dec ax
    call mb_fill
    inc ax
    call mb_fill
    inc ax
    call mb_fill
    mov word [mb_fs], 0x7FFD
    mov word [mb_fd], 0xD003
    mov word [mb_fl], 0xF007
    mov word [mb_flh], 0
    mov word [mb_fn], mb_s_f2
    call mb_file
    mov [mb_res+44], ax
    mov [mb_res+46], dx
    mov word [mb_res+66], 2
    jmp short .win
.say:
    call mb_mov
.win:
    push ds
    pop es
    mov word [mb_res+60], 1         ; the gate, last
    mov si, mb_s_e9                 ; ...and the line for Bochs
    call mb_dump
    mov bx, [mb_wptr]               ; the window keeps it open: BX = it,
    clc                             ; as the entry proc's contract wants
.out:
    pop bp
    pop di
    pop si
    ret

.out0:
    pop bp
    pop di
    pop si
    stc
    ret

; mb_mov - F9: the bridge declared movable through its DX. Clobbers AX, DX
mb_mov:
    push es
    push ds
    pop es
    mov dx, [mb_sel]
    mov ax, mb_reloc
    call OSAPI_MEM_MOVABLE
    sbb ax, ax
    mov [mb_res+54], ax
    pop es
    ret

mb_reloc:                           ; the bridge's relocation proc: it holds
    ret                             ; selectors, which follow a move
mb_paint:
    ret

; mb_free - DX = a claim of ours: freed (ES = ours). Keeps all
mb_free:
    push es
    push ds
    pop es
    call OSAPI_MEM_FREE
    pop es
    ret

; mb_nw - AX = KB -> [mb_nwin] = its 64KB windows. Clobbers AX
mb_nw:
    push cx
    add ax, 63
    mov cl, 6
    shr ax, cl
    mov [mb_nwin], ax
    pop cx
    ret

; mb_wsel - DX = a window -> DX = its selector, by the step. Keeps the rest
mb_wsel:
    push ax
    mov ax, dx
    push dx
    mul word [mb_step]
    pop dx
    add ax, [mb_sel]
    mov dx, ax
    pop ax
    ret

; mb_lin - DX = a selector -> DX:AX its linear base (CF = 1 refused)
mb_lin:
    push bx
    mov bl, SEG_LIN
    call OSAPI_SEG
    pop bx
    ret

; mb_cut - [mb_sel]/[mb_step]/[mb_nwin] -> AX = the first window whose base
; is below 1MB, FFFFh none. Keeps the rest
mb_cut:
    push cx
    push dx
    xor cx, cx
.l:
    mov dx, cx
    call mb_wsel
    call mb_lin
    jc .n
    cmp dx, 0x10
    jb .y
.n:
    inc cx
    cmp cx, [mb_nwin]
    jb .l
    mov cx, 0xFFFF
.y:
    mov ax, cx
    pop dx
    pop cx
    ret

; mb_wlen - AX = a window -> CX = its bytes (0 = 65,536). Keeps the rest
mb_wlen:
    push ax
    push dx
    xor cx, cx
    inc ax
    cmp ax, [mb_nwin]
    jb .o
    dec ax                          ; the last: (KB - 64 x k) KB
    mov dx, 64
    mul dx
    mov cx, [mb_res+8]
    sub cx, ax
    cmp cx, 64
    jae .z
    push cx
    mov cl, 10
    pop ax
    shl ax, cl
    mov cx, ax
    jmp short .o
.z:
    xor cx, cx
.o:
    pop dx
    pop ax
    ret

; mb_fill - AX = a window (none past the claim): the pattern through it.
; Keeps every register
mb_fill:
    cmp ax, [mb_nwin]
    jae .r
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov dx, ax
    call mb_wsel
    mov es, dx
    call mb_wlen
    mov bl, al
    mov al, 0x35
    mul bl
    mov bl, al                      ; BL = 35h x k
    xor di, di
    cld
.l:
    mov ax, di
    xor al, ah
    xor al, bl
    stosb
    loop .l
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; mb_pat - [mb_k], [mb_o] -> AX = the pattern's word there. Keeps the rest
mb_pat:
    push bx
    push cx
    mov al, [mb_k]
    mov bl, 0x35
    mul bl
    mov bl, al
    mov cx, [mb_o]
    mov al, cl
    xor al, ch
    xor al, bl
    inc cx
    mov ah, cl
    xor ah, ch
    xor ah, bl
    pop cx
    pop bx
    ret

; mb_rd - CF = 1 refused, else DX = a selector: AX = 1 its word at 0 is the
; pattern at [mb_k]:[mb_o], 2 it is not, 3 refused. Keeps the rest
mb_rd:
    jc .no
    push es
    push bx
    mov es, dx
    call mb_pat
    cmp [es:0], ax
    mov ax, 1
    je .o
    inc ax
.o:
    pop bx
    pop es
    ret
.no:
    mov ax, 3
    ret

; mb_winl - CF = 1 refused, else DX:AX = a linear address: AX = mb_rd's code
; for the word there against [mb_k] = P, [mb_o] = 1230h (a SEG_WINL window,
; freed after). Keeps the rest
mb_winl:
    jc .no
    push bx
    push cx
    push dx
    push si
    mov cx, 16
    xor si, si
    mov bl, SEG_WINL
    call OSAPI_SEG
    jc .r
    push word [mb_p]
    pop word [mb_k]
    mov word [mb_o], 0x1230
    clc
    call mb_rd
    push ax
    mov bl, SEG_FREE
    call OSAPI_SEG
    pop ax
    jmp short .o
.r:
    mov ax, 3
.o:
    pop si
    pop dx
    pop cx
    pop bx
    ret
.no:
    mov ax, 3
    ret

; mb_file - [mb_fs] / [mb_fd] = source and destination offsets in window P-1,
; [mb_flh]:[mb_fl] = bytes, [mb_fn] = the name: written from the source,
; the destination zeroed, read back into it, compared against the pattern
; at the source -> AX = 1 byte-exact, 2 a byte differs (DX = where in the
; read), 3 the write refused, 4 the read refused (DX = its AX)
mb_file:
    push bx
    push cx
    push si
    push di
    push bp
    push es
    mov es, [mb_w]
    mov bx, [mb_fs]
    call mb_flen                    ; DX:CX = the length
    mov si, [mb_fn]
    call OSAPI_FILE_WRITE
    jnc .wok
    mov dx, ax
    mov ax, 3
    jmp .out
.wok:
    mov dx, [mb_p]                  ; the destination zeroed, walked by the
    dec dx                          ; step: DX = window, DI = offset
    mov di, [mb_fd]
    call mb_cset
.z:
    push dx
    call mb_wsel
    mov es, dx
    pop dx
    mov byte [es:di], 0
    inc di
    jnz .zn
    inc dx
.zn:
    call mb_cdec
    jnz .z
    mov es, [mb_w]
    mov bx, [mb_fd]
    call mb_flen
    mov si, [mb_fn]
    call OSAPI_FILE_READ
    jnc .rok
    mov dx, ax
    mov ax, 4
    jmp short .out
.rok:
    mov bx, [mb_p]                  ; the compare: BX:SI the source's window
    dec bx                          ; and offset (for the pattern), DX:DI the
    mov si, [mb_fs]                 ; destination's, BP the bytes done
    mov dx, bx
    mov di, [mb_fd]
    xor bp, bp
    call mb_cset
.c:
    push dx
    call mb_wsel
    mov es, dx
    pop dx
    mov [mb_k], bx
    mov [mb_o], si
    call mb_pat                     ; AL = the source's byte
    cmp [es:di], al
    jne .bad
    inc bp
    call mb_cdec
    jz .ok
    inc si
    jnz .cs
    inc bx
.cs:
    inc di
    jnz .c
    inc dx
    jmp short .c
.ok:
    mov ax, 1
    xor dx, dx
    jmp short .out
.bad:
    mov ax, 2
    mov dx, bp
.out:
    pop es
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; mb_flen - DX:CX = the length. Keeps the rest
mb_flen:
    mov cx, [mb_fl]
    mov dx, [mb_flh]
    ret

; mb_cset / mb_cdec - the bytes left of a walk, 32 bits: set to the length;
; one off, ZF = 1 at none. Keep the rest (but the flags)
mb_cset:
    push word [mb_fl]
    pop word [mb_cnt]
    push word [mb_flh]
    pop word [mb_cnth]
    ret
mb_cdec:
    sub word [mb_cnt], 1
    sbb word [mb_cnth], 0
    push ax
    mov ax, [mb_cnt]
    or ax, [mb_cnth]
    pop ax
    ret

; mb_dump - SI = a tag: it and MB_RES's words from +8 onto E9, one line.
; Clobbers AX, CX, SI
mb_dump:
    call mb_say
    mov si, mb_res+8
    mov cx, 30
.l:
    lodsw
    call mb_hex4
    mov al, ' '
    out 0xE9, al
    loop .l
    mov al, 10
    out 0xE9, al
    ret

mb_say:
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
mb_hex4:
    push ax
    mov al, ah
    call mb_hex2
    pop ax
mb_hex2:
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

; the three places, from W = window P-1: paragraphs from W, then the window
; (relative to P) and the offset in it the pattern there is read by
mb_legs:
    dw 0x0FF0, -1, 0xFF00           ; 256 bytes before the cut
    dw 0x1123,  0, 0x1230           ; 1230h past it
    dw 0x2004,  1, 0x0040           ; 10040h past it
mb_legs_end:

mb_tpl:
    dw 8, 400, 200, 40
    dw mb_ttl, mb_cls
; its CLASS (SPEC.md 11.104): every handler, and the flags it is born with
    OS88_CLASS mb_cls, 0, PAINT, mb_paint
mb_ttl:     db 'MegaBrdg', 0
mb_s_e9:    db 'MEGABRDG: ', 0
mb_s_e90:   db 'MEGABRDG0: ', 0
mb_s_f1:    db 'BRDG1.DAT', 0
mb_s_f2:    db 'BRDG2.DAT', 0

mb_res:     db 'MBRRES01'
            times 60 db 0
mb_b1       dw 0
mb_wptr     dw 0
mb_sel      dw 0
mb_step     dw 0
mb_nwin     dw 0
mb_p        dw 0
mb_w        dw 0
mb_k        dw 0
mb_o        dw 0
mb_fs       dw 0
mb_fd       dw 0
mb_fl       dw 0
mb_flh      dw 0
mb_cnt      dw 0
mb_cnth     dw 0
mb_fn       dw 0

OS88_IMAGE_END
