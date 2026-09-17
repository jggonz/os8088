; =============================================================================
; os8088 - drivers/sound/rad1.inc  (RADPLAY.DRV)
;
; The version 1.0 engine: SPEC.md 96.4.5, tools/radsim.py's EngineV1 - RAD
; V1.1a's PLAYER.ASM re-written, with the three deviations that keep it inside
; the tune. A different and smaller engine than the 2.1 one: nine 2-op
; channels, frequencies handled as one absolute number, no riffs.
;
; Conventions as rad2.inc's: DS = the claim, CL = the channel with CH = 0 where
; a routine takes one; CX, SI, DI, BP preserved, AX, BX, DX not.
; =============================================================================

R1_FSTART   equ 0x156
R1_FRANGE   equ 0x158

; -----------------------------------------------------------------------------
; r1_parse - the instrument pointers, the order list and the pattern table
; -----------------------------------------------------------------------------
r1_parse:
    push ax
    push si
    push di
    cld
    mov si, RAD_TUNE+0x11
    lodsb
    test al, 0x80
    jz .inst
.desc:
    lodsb
    or al, al
    jnz .desc
.inst:
    lodsb
    or al, al
    jz .order
    xor ah, ah
    dec ax
    shl ax, 1
    mov di, ax
    mov [r1_inst+di], si
    add si, 11
    jmp .inst
.order:
    lodsb
    mov [rd_ordlen], al
    mov [rd_ordlist], si
    xor ah, ah
    add si, ax
    mov [r1_patlist], si
    pop di
    pop si
    pop ax
    ret

; r1_patptr - AL = a pattern number -> BX = its lines' claim offset, 0 = empty
r1_patptr:
    xor ah, ah
    shl ax, 1
    mov bx, [r1_patlist]
    add bx, ax
    mov bx, [bx]
    or bx, bx
    jz .out
    add bx, RAD_TUNE
.out:
    ret

; -----------------------------------------------------------------------------
; r1_endplayer - 20h..F5h <- 00h
; -----------------------------------------------------------------------------
r1_endplayer:
    push ax
    push bx
    mov bx, 0x20
    xor al, al
.reg:
    call rd_set
    inc bx
    cmp bx, 0xF6
    jb .reg
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; r1_start - the 1.0 start (PLAYER.ASM's InitPlayer) over zeroed state
; -----------------------------------------------------------------------------
r1_start:
    push ax
    push bx
    push cx
    mov bx, r1_tsspd            ; the tone-slide speed starts at 1
    mov cx, 9
.ts:
    mov byte [bx], 1
    inc bx
    loop .ts
    call r1_endplayer
    mov bx, 0x01
    mov al, 0x20
    call rd_set
    mov bl, 0x08
    xor al, al
    call rd_set
    mov bl, 0xBD
    call rd_set
    mov bx, [rd_ordlist]
    mov al, [bx]                ; the first entry: never a jump (validated)
    call r1_patptr
    mov [r1_ppos], bx
    mov byte [rd_order], 0
    mov byte [rd_speedcnt], 0
    mov byte [rd_line], 0
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; r1_frame - one frame (EngineV1.frame)
; =============================================================================
r1_frame:
    push cx
    push si
    push di
    cmp byte [rd_speedcnt], 0
    je .line
    dec byte [rd_speedcnt]
    jmp .update
.line:
    push ds                     ; portamento, volume slide and tone slide off
    pop es                      ; on every channel, and the status block's
    cld                         ; "effect on this line"
    xor al, al
    mov di, r1_port
    mov cx, 9
    rep stosb
    mov di, r1_vols
    mov cl, 9
    rep stosb
    mov di, r1_tson
    mov cl, 9
    rep stosb
    mov di, rd_st + RST_CHAN + RSC_FX
    mov cl, 9
.fx0:
    mov [di], al
    add di, RSC_SIZE
    loop .fx0
    mov si, [r1_ppos]
    or si, si
    jz .adv
    mov al, [si]
    and al, 0x7F
    cmp al, [rd_line]
    jne .adv
    mov dl, [si]
    and dl, 0x80                ; the last line?
    jz .notlast
    mov word [r1_ppos], 0
.notlast:
    push dx
    inc si
.ent:
    mov cl, [si]
    and cl, 0x7F
    xor ch, ch
    mov al, [si+1]
    mov ah, [si+2]
    mov bh, [si]                ; the channel byte, for its last-entry bit
    add si, 3
    xor dl, dl
    test ah, 0x0F
    jz .noparam
    mov dl, [si]
    inc si
.noparam:
    push si
    push bx
    call r1_playnote
    pop bx
    pop si
    jc .jump
    test bh, 0x80
    jz .ent
    pop dx
    or dl, dl                   ; deviation 2: the last line ends the pattern
    jz .keep
    xor si, si
.keep:
    mov [r1_ppos], si
.adv:
    mov al, [rd_speed]
    dec al
    mov [rd_speedcnt], al
    mov al, [rd_line]
    inc al
    mov [rd_line], al
    cmp al, 64
    jb .update
    mov byte [rd_line], 0
    call r1_nextpat
    jmp short .update
.jump:
    pop dx
    call r1_jumpto              ; AL = the line
.update:
    call r1_updnotes
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; r1_jumpto - PLAYER.ASM's jump-to-line, deviation 3 (the seek reads the tune)
; in:  AL = the line
; -----------------------------------------------------------------------------
r1_jumpto:
    push si
    mov bl, [rd_speed]
    mov [rd_speedcnt], bl
    mov [rd_line], al
    push ax
    call r1_nextpat             ; BX = the pattern
    pop ax
    mov si, bx
    or si, si
    jz .out
.line:
    mov ah, [si]
    and ah, 0x7F
    cmp ah, al
    jae .set
    test byte [si], 0x80
    jnz .none
    inc si
.ent:
    mov ah, [si]
    add si, 3
    test byte [si-1], 0x0F
    jz .np
    inc si
.np:
    test ah, 0x80
    jz .ent
    jmp .line
.none:
    xor si, si
.set:
    mov [r1_ppos], si
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; r1_nextpat - the next order entry's pattern (NextPattern)
; out: BX = [r1_ppos] = the pattern, 0 = empty
; -----------------------------------------------------------------------------
r1_nextpat:
    push ax
    mov al, [rd_order]
    inc al
    cmp al, [rd_ordlen]
    jb .look
    xor al, al
    or byte [rd_st+RST_FLAGS], RSTF_LOOPED
.look:
    mov [rd_order], al
    xor ah, ah
    mov bx, [rd_ordlist]
    add bx, ax
    mov al, [bx]
    test al, 0x80
    jz .pat
    and al, 0x7F
    or byte [rd_st+RST_FLAGS], RSTF_LOOPED
    jmp .look
.pat:
    call r1_patptr
    mov [r1_ppos], bx
    pop ax
    ret

; =============================================================================
; r1_playnote - one entry (PLAYER.ASM's PlayNote)
; in:  CL = channel, AL = note byte, AH = instrument/effect byte, DL = parameter
; out: CF = 1 and AL = the line: a jump-to-line abandons the rest of the line
; =============================================================================
r1_playnote:
    mov bl, ah
    and bl, 0x0F                ; BL = the effect
    test al, 0x0F
    jnz .note                   ; deviation 1: a note nibble of 0 is no note
    jmp .effects
.note:
    push ax
    mov bh, al
    and bh, 0x0F
    push bx
    mov bl, al
    and bl, 0x70
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    mov byte [r1_noct], bl
    pop bx
    cmp bl, CM_TONESLIDE
    jne .play
    pop ax
    mov al, bh                  ; note 1..15
    dec al
    cmp al, 12
    jae .nothing                ; a key-off cannot be slid to
    push dx
    xor ah, ah
    shl ax, 1
    push bx
    mov bx, ax
    mov ax, [rd_notefreq+bx]    ; octave x 158h + F-Number - 156h
    pop bx
    push ax                     ; the F-Number
    mov al, [r1_noct]
    xor ah, ah
    mul word [r1_frange]        ; DX:AX = octave x 158h (DX is banked)
    pop dx
    add ax, dx
    sub ax, R1_FSTART           ; 16 bits, as PLAYER.ASM's
    pop dx                      ; DL = the parameter again
    mov bx, cx
    shl bx, 1
    mov [r1_tsfrq+bx], ax
    mov bx, cx
    mov byte [r1_tson+bx], 1
    or dl, dl
    jz .nothing
    mov [r1_tsspd+bx], dl
.nothing:
    clc
    ret
.play:
    pop ax
    push ax
    mov bx, cx
    mov bl, [r1_oldb0+bx]
    and bl, 0xDF
    push bx
    mov bx, cx
    pop ax                      ; AL = the B0h image with key-on clear
    mov [r1_oldb0+bx], al
    mov bl, cl
    add bl, 0xB0
    xor bh, bh
    call rd_set
    pop ax
    push ax
    mov bl, al
    rol bl, 1                   ; bit 7 of the note byte -> bit 0
    and bl, 1
    shl bl, 1
    shl bl, 1
    shl bl, 1
    shl bl, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    or bl, ah                   ; the instrument, 0..31
    jz .noinst
    mov al, bl
    call r1_loadinst
.noinst:
    pop ax
    push ax
    and al, 0x0F
    cmp al, 15
    je .keyoff
    dec al
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov ax, [rd_notefreq+bx]    ; AX = the F-Number
    mov bh, [r1_noct]
    shl bh, 1
    shl bh, 1
    or bh, 0x20
    or bh, ah                   ; octave << 2 | key-on | F-Number >> 8
    push bx
    mov bx, cx
    mov [r1_olda0+bx], al
    pop bx
    push bx
    mov bl, cl
    add bl, 0xA0
    mov bh, 0
    call rd_set                 ; A0h + channel
    pop bx
    mov al, bh
    mov bx, cx
    mov [r1_oldb0+bx], al
    mov bl, cl
    add bl, 0xB0
    xor bh, bh
    call rd_set                 ; B0h + channel: the note starts
    mov al, RSC_SIZE            ; the status block: this note, and a key-on
    mul cl
    mov bx, ax
    inc byte [rd_st+RST_CHAN+RSC_KEYS+bx]
    pop ax
    push ax
    and al, 0x7F
    mov [rd_st+RST_CHAN+RSC_NOTE+bx], al
.keyoff:
    pop ax
    mov bl, ah
    and bl, 0x0F
.effects:
    push bx
    mov al, RSC_SIZE
    mul cl
    mov bx, ax
    pop ax                      ; AL = the effect again (from BL)
    mov [rd_st+RST_CHAN+RSC_FX+bx], al
    mov bx, cx
    cmp al, CM_PORTUP
    jne .e1
    mov [r1_port+bx], dl
    jmp .done
.e1:
    cmp al, CM_PORTDN
    jne .e2
    neg dl
    mov [r1_port+bx], dl
    jmp .done
.e2:
    cmp al, CM_TONESLIDE
    jne .e3
    or dl, dl
    jz .ts1
    mov [r1_tsspd+bx], dl
.ts1:
    mov byte [r1_tson+bx], 1
    jmp .done
.e3:
    cmp al, CM_TONEVOL
    je .vol
    cmp al, CM_VOLSLIDE
    jne .e4
.vol:
    cmp dl, 50
    jb .vs
    sub dl, 50
    neg dl
.vs:
    mov [r1_vols+bx], dl
    cmp al, CM_TONEVOL
    jne .done
    mov byte [r1_tson+bx], 1
    jmp .done
.e4:
    cmp al, CM_SETVOL
    jne .e5
    mov al, dl
    call r1_setvol
    jmp .done
.e5:
    cmp al, CM_JUMP
    jne .e6
    cmp dl, 64
    jae .done
    mov al, dl
    stc
    ret
.e6:
    cmp al, CM_SPEED
    jne .done
    mov [rd_speed], dl
.done:
    clc
    ret

; -----------------------------------------------------------------------------
; r1_loadinst - AL = instrument 1..31 onto channel CL (an undefined one loads
;               nothing)
; -----------------------------------------------------------------------------
r1_loadinst:
    push si
    push dx
    xor ah, ah
    dec ax
    shl ax, 1
    mov bx, ax
    mov si, [r1_inst+bx]
    or si, si
    jz .out
    shr bx, 1                   ; the status block's instrument number
    inc bx
    push bx
    mov al, RSC_SIZE
    mul cl
    mov bx, ax
    pop ax
    mov [rd_st+RST_CHAN+RSC_INST+bx], al
    mov bx, cx
    mov al, [si+2]
    mov [r1_old43+bx], al
    mov dl, [r1_choff+bx]       ; DL = the channel's operator base (20h..32h)
    xor dh, dh
    xor ah, ah                  ; AH = k
.k:
    mov bx, dx
    mov al, ah
    xor ah, ah
    shl ax, 1
    push ax
    push si
    add si, ax
    mov al, [si+1]
    call rd_set                 ; reg <- b[off + 2k + 1]
    add bx, 3
    mov al, [si]
    call rd_set                 ; reg + 3 <- b[off + 2k]
    pop si
    pop ax
    shr ax, 1
    mov ah, al
    add dl, 0x20
    inc ah
    cmp ah, 4
    jb .k
    add dl, 0x40
    mov bx, dx
    mov al, [si+10]
    call rd_set
    add bx, 3
    mov al, [si+9]
    call rd_set
    mov bl, cl
    add bl, 0xC0
    xor bh, bh
    mov al, [si+8]
    call rd_set
.out:
    pop dx
    pop si
    ret

; -----------------------------------------------------------------------------
; r1_setvol - AL = volume onto channel CL's carrier (clamped at 63)
; -----------------------------------------------------------------------------
r1_setvol:
    cmp al, 64
    jb .k
    mov al, 63
.k:
    xor al, 0x3F
    mov bx, cx
    mov ah, [r1_old43+bx]
    and ah, 0xC0
    or al, ah
    mov [r1_old43+bx], al
    mov bl, [r1_choff+bx]
    xor bh, bh
    add bx, 0x23
    call rd_set
    ret

; -----------------------------------------------------------------------------
; r1_getfreq - channel CL's frequency as one number -> AX; clobbers BX, DX
; r1_setfreq - AX -> B0h then A0h
; -----------------------------------------------------------------------------
r1_getfreq:
    mov bx, cx
    mov al, [r1_oldb0+bx]
    mov ah, al
    and ah, 3
    mov dl, [r1_olda0+bx]
    push ax
    mov al, dl
    sub ax, R1_FSTART           ; AX = (B0h & 3) << 8 | A0h, minus 156h
    mov dx, ax
    pop ax
    shr al, 1
    shr al, 1
    and al, 7
    xor ah, ah
    push dx
    mul word [r1_frange]        ; octave x 158h
    pop dx
    add ax, dx
    ret

r1_setfreq:
    push cx
    xor dx, dx
    div word [r1_frange]        ; AX = the octave, DX = the remainder
    add dx, R1_FSTART
    shl al, 1
    shl al, 1
    mov bx, cx
    mov ah, [r1_oldb0+bx]
    and ah, 0xE0
    or al, ah
    or al, dh
    mov [r1_oldb0+bx], al
    push dx
    mov bl, cl
    add bl, 0xB0
    xor bh, bh
    call rd_set
    pop dx
    mov bx, cx
    mov [r1_olda0+bx], dl
    mov al, dl
    mov bl, cl
    add bl, 0xA0
    xor bh, bh
    call rd_set
    pop cx
    ret

; -----------------------------------------------------------------------------
; r1_updnotes - every frame, per channel: portamento, volume slide, tone slide
; -----------------------------------------------------------------------------
r1_updnotes:
    push cx
    push si
    xor cx, cx
.ch:
    mov si, cx
    mov al, [r1_port+si]
    or al, al
    jz .vol
    push ax
    call r1_getfreq
    pop dx
    xchg ax, dx
    cbw
    add ax, dx
    call r1_setfreq
.vol:
    mov ah, [r1_vols+si]
    mov al, [r1_old43+si]
    and al, 0x3F
    xor al, 0x3F                ; the carrier level as a volume
    or ah, ah
    jz .tone
    sub al, ah
    test ah, 0x80
    jz .down
    cmp al, 64
    jb .setv
    mov al, 63
    jmp short .setv
.down:
    test al, 0x80
    jz .setv
    xor al, al
.setv:
    call r1_setvol
.tone:
    cmp byte [r1_tson+si], 0
    je .next
    call r1_getfreq             ; AX = where it is
    mov bx, si
    shl bx, 1
    mov dx, [r1_tsfrq+bx]       ; DX = where it is going
    mov bl, [r1_tsspd+si]
    cmp ax, dx
    jne .move
    mov byte [r1_tson+si], 0
    jmp short .setf
.move:
    ja .downf
    push bx
    xor bh, bh
    add ax, bx
    pop bx
    cmp ax, dx
    jb .setf
    mov ax, dx
    mov byte [r1_tson+si], 0
    jmp short .setf
.downf:
    push bx
    xor bh, bh
    sub ax, bx
    pop bx
    cmp ax, dx
    ja .setf
    mov ax, dx
    mov byte [r1_tson+si], 0
.setf:
    call r1_setfreq
.next:
    inc cl
    cmp cl, 9
    jb .ch
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; r1_status - the status block's channel half, built at verb 6 (rd_stat)
; -----------------------------------------------------------------------------
r1_status:
    push ax
    push bx
    push cx
    push di
    call rd_status_pos
    xor bx, bx
    mov di, rd_st + RST_CHAN
    mov cx, 9
.ch:
    mov al, [r1_old43+bx]
    and al, 0x3F
    xor al, 0x3F
    mov [di+RSC_VOL], al
    xor al, al
    test byte [r1_oldb0+bx], 0x20
    jz .f
    mov al, RSCF_KEYED
.f:
    mov [di+RSC_FLAGS], al
    inc bx
    add di, RSC_SIZE
    loop .ch
    pop di
    pop cx
    pop bx
    pop ax
    ret

r1_choff:   db 0x20, 0x21, 0x22, 0x28, 0x29, 0x2A, 0x30, 0x31, 0x32
r1_frange:  dw R1_FRANGE
