; =============================================================================
; os8088 - apps/xtmp3/x3pnext.inc - XTMP3's frame loop, in the FRONT part
;
; x3sim Stream.frames(), decision for decision: the header at the ring's read
; position, the resync scan (resumable across X3P_MORE), the truncated frame,
; the frame copied out of the engine's ring, its side information, its
; verdict and its main data into the reservoir. The FRONT part's NEXT verb
; (xf_next); x3pars.inc's x3p_next is the near entry that calls it. It moved
; here out of the primary, whose segment is the package's tightest.
;
; Entered with DS = this part's data segment and ES:SI = the caller's X3R
; descriptor; the variables are this part's (x3p_ names: it is the parser's
; code). Answers exactly as x3p_next does (x3gran.inc section 4).
; =============================================================================

; -----------------------------------------------------------------------------
; xf_next (NEXT) - ES:SI -> the X3R ring; x3p_next's answer, far
; -----------------------------------------------------------------------------
xf_next:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cld
    mov [x3p_rdesc], si
    mov ax, [es:si+X3R_SEG]            ; the ring, as it stands now
    mov [x3p_rseg], ax
    mov ax, [es:si+X3R_MASK]
    mov [x3p_rmask], ax
    mov ax, [es:si+X3R_RD]
    mov [x3p_rrd], ax
    mov cx, [es:si+X3R_WR]
    sub cx, ax
    mov [x3p_ravail], cx
    mov al, [es:si+X3R_EOF]
    mov [x3p_reof], al
    call x3p_fold                   ; the last frame's main data joins the
                                    ; reservoir's tail
    cmp byte [X3P_S_SCAN], 0
    jne .rescan
    cmp byte [X3P_S_SEEK], 0
    je .top
    xor bp, bp                      ; a seek: scan from the first byte
    mov byte [X3P_S_SCAN], 1
    jmp .scan
.rescan:
    xor bp, bp                      ; a scan in progress: from the first byte
    jmp .scan
.top:
    cmp word [x3p_ravail], 4        ; o + 4 <= end
    jae .h
.endmore:
    cmp byte [x3p_reof], 0
    jne .end
    jmp .more
.h:
    xor bx, bx
    call x3p_rget4
    call x3p_hdr
    jc .lost
    call x3p_lockok
    jnc .have
.lost:
    add word [X3P_CNT+X3PC_RESYNC], 1    ; a lost sync: scan from o + 1
    adc word [X3P_CNT+X3PC_RESYNC+2], 0
    mov byte [X3P_S_SCAN], 1
    mov bp, 1
.scan:
    ; --- find_sync(b, o, end, lock), resumable: BP = the candidate --------
    mov ax, bp                      ; p + 4 > avail: no more candidates
    add ax, 4
    jc .exh
    cmp ax, [x3p_ravail]
    jbe .cand
.exh:
    cmp byte [x3p_reof], 0
    jne .gone
    mov bx, bp                      ; consume what was scanned, ask for more
    call x3p_skip
    jmp .more
.gone:
    mov bx, [x3p_ravail]            ; the rest is garbage: the stream ends
    call x3p_skip
    jmp .end
.cand:
    mov bx, bp
    call x3p_rget4
    cmp dh, 0xFF
    jne .adv
    call x3p_hdr
    jc .adv
    call x3p_lockok
    jc .adv
    mov [x3p_hreg], ax              ; keep it: it may be the one
    mov [x3p_hreg+2], bx
    mov [x3p_hreg+4], cx
    mov [x3p_hreg+6], dx
    mov bx, bp                      ; n = p + size
    add bx, dx
    jc .adv
    mov ax, bx
    add ax, 4
    jc .near
    cmp ax, [x3p_ravail]
    ja .near
    call x3p_rget4                  ; n + 4 <= avail: the next must agree
    call x3p_hdr
    jc .adv
    cmp al, [x3p_hreg]
    jne .adv
    cmp ah, [x3p_hreg+1]
    jne .adv
    cmp bl, [x3p_hreg+2]
    jne .adv
    jmp short .found
.near:
    cmp byte [x3p_reof], 0          ; past what is held: at the end, a frame
    je .wait                        ; that ends inside it is believed
    cmp bx, [x3p_ravail]
    jbe .found
    jmp short .adv
.wait:
    mov bx, bp                      ; not yet known: consume up to p
    call x3p_skip
    jmp .more
.adv:
    inc bp
    jmp .scan
.found:
    mov bx, bp
    call x3p_skip
    mov byte [X3P_S_SCAN], 0
    mov byte [X3P_S_SEEK], 0
    mov ax, [x3p_hreg]
    mov bx, [x3p_hreg+2]
    mov cx, [x3p_hreg+4]
    mov dx, [x3p_hreg+6]
.have:
    ; --- the frame at o: AL AH BL BH CL CH DX as x3p_hdr answered ---------
    cmp dx, [x3p_ravail]
    jbe .whole
    cmp byte [x3p_reof], 0
    je .more
    add word [X3P_CNT+X3PC_TRUNC], 1     ; the file ends inside it
    adc word [X3P_CNT+X3PC_TRUNC+2], 0
    jmp .end
.whole:
    call x3p_stage
    mov bx, [X3P_S_SIZE]
    call x3p_consume
    mov al, [X3P_S_NGR]
    mov ah, [X3P_S_VERD]
    clc
    jmp short .out
.more:
    mov ax, X3P_MORE
    stc
    jmp short .out
.end:
    mov ax, X3P_END
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; x3p_lockok - CF = 0 if x3p_hdr's AL AH BL CH is a Layer III header of the
; locked stream, not free format. keeps everything
x3p_lockok:
    cmp ah, 3
    jne .no
    test ch, X3PH_FREE
    jnz .no
    cmp al, [X3P_S_VER]
    jne .no
    cmp bl, [X3P_S_SRI]
    jne .no
    clc
    ret
.no:
    stc
    ret

; x3p_skip - consume BX bytes as skipped (garbage); a seek's scan does not
; count them. clobbers BX
x3p_skip:
    cmp byte [X3P_S_SEEK], 0
    jne x3p_consume
    add [X3P_CNT+X3PC_SKIP], bx
    adc word [X3P_CNT+X3PC_SKIP+2], 0
; x3p_consume - BX bytes off the ring: RD and POS, here and in the
; descriptor. clobbers BX
x3p_consume:
    push si
    add [x3p_rrd], bx
    sub [x3p_ravail], bx
    mov si, [x3p_rdesc]
    add [es:si+X3R_RD], bx
    add [es:si+X3R_POS], bx
    adc word [es:si+X3R_POS+2], 0
    pop si
    ret

; x3p_rget4 - DX:AX = the four bytes BX into the ring (file order, DH first)
; keeps the rest
x3p_rget4:
    push cx
    push si
    push ds
    mov cx, [x3p_rmask]
    mov si, [x3p_rrd]
    add si, bx
    mov ds, [x3p_rseg]
    and si, cx
    mov dh, [si]
    inc si
    and si, cx
    mov dl, [si]
    inc si
    and si, cx
    mov ah, [si]
    inc si
    and si, cx
    mov al, [si]
    pop ds
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; x3p_fold - the staged frame's main data joins the reservoir's tail: the
;            last min(511, tail + main) bytes of X3P_MD[512 - tail, 512 + main)
;            move to end at 512. Deferred to the NEXT x3p_next because the
;            granules of that frame read the md where it lies.
; -----------------------------------------------------------------------------
x3p_fold:
    push cx
    push si
    push di
    push es
    mov cx, [X3P_S_TAIL]
    add cx, [X3P_S_MAIN]
    cmp cx, 511
    jbe .n
    mov cx, 511
.n:
    mov si, X3P_MD + X3P_MD_TAIL    ; source: 512 + main - newtail
    add si, [X3P_S_MAIN]
    sub si, cx
    mov di, X3P_MD + X3P_MD_TAIL    ; dest: 512 - newtail
    sub di, cx
    mov [X3P_S_TAIL], cx
    mov word [X3P_S_MAIN], 0
    push ds                         ; (within this part: ES = DS)
    pop es
    rep movsb                       ; (source above dest: ascending is safe)
    pop es
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; x3p_stage - the frame at the ring's read position (x3p_hdr's registers, DX
; = its size, all of it held): copy it, read its side information, decide its
; verdict, stage its main data. clobbers: AX, BX, CX, DX, SI, DI, BP
; -----------------------------------------------------------------------------
x3p_stage:
    mov [X3P_S_SIZE], dx
    mov [X3P_S_NCH], cl
    mov ah, ch
    and ah, 3
    mov [X3P_S_MODE], ah
    mov ah, ch
    shr ah, 1
    shr ah, 1
    and ah, 3
    mov [X3P_S_MEXT], ah
    xor ah, ah
    test ch, X3PH_CRC
    jz .c
    inc ah
.c:
    mov [X3P_S_CRC], ah
    mov byte [X3P_S_NGR], 1
    cmp al, 1
    jne .g
    mov byte [X3P_S_NGR], 2
.g:
    push ax                         ; AL = version, CL = channels
    push cx
    ; --- copy the frame out of the ring (DS the ring, ES this part) ---
    push es
    push ds
    pop es
    push ds
    mov cx, dx
    mov di, X3P_FRM
    mov si, [x3p_rrd]
    mov bx, [x3p_rmask]
    mov ds, [x3p_rseg]
.cp:
    and si, bx
    movsb
    loop .cp
    pop ds
    pop es
    pop cx
    pop ax
    ; --- counted: a frame; a change of channels ---
    add word [X3P_CNT+X3PC_FRAMES], 1
    adc word [X3P_CNT+X3PC_FRAMES+2], 0
    add word [X3P_S_FRAME], 1
    adc word [X3P_S_FRAME+2], 0
    cmp cl, [X3P_S_NCH0]
    je .same
    add word [X3P_CNT+X3PC_CHG], 1
    adc word [X3P_CNT+X3PC_CHG+2], 0
.same:
    ; --- side information: from 4 (+2 with a CRC) ---
    call x3p_sideb                  ; AX = its bytes
    mov bx, 4
    cmp byte [X3P_S_CRC], 0
    je .nc
    add bx, 2
.nc:
    mov [x3p_sbase], bx
    add bx, ax                      ; the main data: [p + side, size)
    mov ax, [X3P_S_SIZE]
    mov byte [x3p_verd], X3V_BAD
    mov word [x3p_mlen], 0
    cmp bx, ax
    ja .verd                        ; no room for the side information
    sub ax, bx
    mov [x3p_mlen], ax
    mov [x3p_mofs], bx
    call x3p_side                   ; CF = 1: a reserved field
    jc .verd
    ; total part2_3_length > (mdb + main) x 8: badside
    xor bx, bx
    xor ax, ax
    mov cl, [X3P_S_NGR]
    shl cl, 1
    xor ch, ch
    mov si, X3P_SIDE
.sum:
    cmp byte [X3P_S_NCH], 2      ; (a mono frame has a row of one)
    je .s2
    test bx, 1
    jnz .sk
.s2:
    add ax, [si+X3P_SI_P23]
.sk:
    add si, X3P_SI_SZ
    inc bx
    loop .sum
    mov cx, [X3P_S_MDB]
    add cx, [x3p_mlen]
    shl cx, 1                       ; (<= 511 + 1441, x 8 fits a word)
    shl cx, 1
    shl cx, 1
    cmp ax, cx
    ja .verd
    mov ax, [X3P_S_MDB]          ; main_data_begin past what is held
    cmp ax, [X3P_S_HELD]
    jbe .ok
    mov byte [x3p_verd], X3V_RES
    jmp short .verd
.ok:
    mov byte [x3p_verd], X3V_OK
    cmp ax, [X3P_CNT+X3PC_REACH]
    jbe .r
    mov [X3P_CNT+X3PC_REACH], ax
.r:
    mov bx, X3P_MD + X3P_MD_TAIL    ; the md: the reservoir's last mdb bytes
    sub bx, ax                      ; then this frame's main data
    mov [X3P_S_MD0], bx
    add ax, [x3p_mlen]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov [X3P_S_BITN], ax
    mov word [X3P_S_BITP], 0
.verd:
    mov al, [x3p_verd]
    mov [X3P_S_VERD], al
    cmp al, X3V_BAD
    jne .v1
    add word [X3P_CNT+X3PC_BAD], 1
    adc word [X3P_CNT+X3PC_BAD+2], 0
.v1:
    cmp al, X3V_RES
    jne .v2
    add word [X3P_CNT+X3PC_SHORT], 1
    adc word [X3P_CNT+X3PC_SHORT+2], 0
.v2:
    ; --- this frame's main data joins the reservoir, whatever the verdict ---
    mov cx, [x3p_mlen]
    mov si, [x3p_mofs]
    add si, X3P_FRM
    mov di, X3P_MD + X3P_MD_TAIL
    push es                         ; (within this part: ES = DS)
    push ds
    pop es
    rep movsb
    xor ax, ax                      ; four zero bytes past its end
    stosw
    stosw
    pop es
    mov ax, [x3p_mlen]
    mov [X3P_S_MAIN], ax
    add ax, [X3P_S_HELD]         ; held = min(4096, held + main)
    cmp ax, X3_RESERVOIR
    jbe .hd
    mov ax, X3_RESERVOIR
.hd:
    mov [X3P_S_HELD], ax
    mov byte [X3P_S_GRNXT], 0
    ret

; -----------------------------------------------------------------------------
; x3p_side - the side information (x3sim.parse_side) of the frame staged at
; X3P_FRM + [x3p_sbase], into X3P_SIDE, X3P_S_MDB and X3P_S_SCFSI
; out: CF = 1 a reserved field. clobbers: AX, BX, CX, DX, SI, BP
; -----------------------------------------------------------------------------
x3p_side:
    mov word [x3p_sbp], 0
    mov byte [x3p_sres], 0
    cmp byte [X3P_S_VER], 1
    jne .m2
    mov cl, 9                       ; MPEG-1: main_data_begin 9, private 5/3
    call x3p_sget
    mov [X3P_S_MDB], ax
    mov cl, 5
    cmp byte [X3P_S_NCH], 1
    je .p1
    mov cl, 3
.p1:
    call x3p_sget
    mov cl, 4                       ; scfsi, a channel: four bits, group 0
    call x3p_sget                   ; the most significant
    mov [X3P_S_SCFSI], al
    mov byte [X3P_S_SCFSI+1], 0
    cmp byte [X3P_S_NCH], 1
    je .gr
    mov cl, 4
    call x3p_sget
    mov [X3P_S_SCFSI+1], al
    jmp short .gr
.m2:
    mov cl, 8                       ; MPEG-2: main_data_begin 8, private 1/2
    call x3p_sget
    mov [X3P_S_MDB], ax
    mov cl, 1
    cmp byte [X3P_S_NCH], 1
    je .p2
    mov cl, 2
.p2:
    call x3p_sget
    mov word [X3P_S_SCFSI], 0
.gr:
    xor bp, bp                      ; granule x 2 + channel
.gc:
    mov ax, bp
    mov ah, X3P_SI_SZ
    mul ah
    add ax, X3P_SIDE
    mov si, ax
    mov cl, 12
    call x3p_sget
    mov [si+X3P_SI_P23], ax
    mov cl, 9
    call x3p_sget
    mov [si+X3P_SI_BV], ax
    cmp ax, 288
    jbe .bv
    mov byte [x3p_sres], 1
.bv:
    mov cl, 8
    call x3p_sget
    mov [si+X3P_SI_GG], al
    mov cl, 4
    cmp byte [X3P_S_VER], 1
    je .sf
    mov cl, 9
.sf:
    call x3p_sget
    mov [si+X3P_SI_SFC], ax
    mov cl, 1
    call x3p_sget
    mov [si+X3P_SI_WS], al
    or al, al
    jz .nows
    mov cl, 2
    call x3p_sget
    mov [si+X3P_SI_BT], al
    or al, al
    jnz .bt
    mov byte [x3p_sres], 1          ; window switching with block type 0
.bt:
    mov cl, 1
    call x3p_sget
    mov [si+X3P_SI_MIX], al
    mov cl, 5
    call x3p_sget
    mov [si+X3P_SI_TAB], al
    mov cl, 5
    call x3p_sget
    mov [si+X3P_SI_TAB+1], al
    mov byte [si+X3P_SI_TAB+2], 0
    mov cl, 3
    call x3p_sget
    mov [si+X3P_SI_SBG], al
    mov cl, 3
    call x3p_sget
    mov [si+X3P_SI_SBG+1], al
    mov cl, 3
    call x3p_sget
    mov [si+X3P_SI_SBG+2], al
    mov al, 7                       ; region0: 8 for a pure short block
    cmp byte [si+X3P_SI_BT], 2
    jne .r7
    cmp byte [si+X3P_SI_MIX], 0
    jne .r7
    inc al
.r7:
    mov [si+X3P_SI_R0], al
    mov byte [si+X3P_SI_R1], 36
    jmp short .pre
.nows:
    mov word [si+X3P_SI_BT], 0   ; BT, MIX
    mov cl, 5
    call x3p_sget
    mov [si+X3P_SI_TAB], al
    mov cl, 5
    call x3p_sget
    mov [si+X3P_SI_TAB+1], al
    mov cl, 5
    call x3p_sget
    mov [si+X3P_SI_TAB+2], al
    mov word [si+X3P_SI_SBG], 0
    mov byte [si+X3P_SI_SBG+2], 0
    mov cl, 4
    call x3p_sget
    mov [si+X3P_SI_R0], al
    mov cl, 3
    call x3p_sget
    mov [si+X3P_SI_R1], al
.pre:
    xor al, al
    cmp byte [X3P_S_VER], 1
    jne .np
    mov cl, 1
    call x3p_sget
.np:
    mov [si+X3P_SI_PRE], al
    mov cl, 1
    call x3p_sget
    mov [si+X3P_SI_SCL], al
    mov cl, 1
    call x3p_sget
    mov [si+X3P_SI_C1], al
    mov cx, 3                       ; Huffman table 4 or 14: reserved
    xor bx, bx
.tt:
    mov al, [si+X3P_SI_TAB+bx]
    cmp al, 4
    je .rs
    cmp al, 14
    jne .tn
.rs:
    mov byte [x3p_sres], 1
.tn:
    inc bx
    loop .tt
    ; the next: channels, then granules (a mono frame skips odd rows)
    inc bp
    cmp byte [X3P_S_NCH], 2
    je .nx
    inc bp
.nx:
    mov al, [X3P_S_NGR]
    shl al, 1
    xor ah, ah
    cmp bp, ax
    jae .done
    jmp .gc
.done:
    cmp byte [x3p_sres], 1
    cmc                             ; CF = 1 when a field was reserved
    ret

; x3p_sget - CL = n bits (1..12) of the side information -> AX. clobbers: CX,
; DX
x3p_sget:
    push bx
    push si
    mov bx, [x3p_sbp]
    mov si, bx
    shr si, 1
    shr si, 1
    shr si, 1
    add si, [x3p_sbase]
    add si, X3P_FRM
    mov ah, [si]                 ; 24 bits from the byte holding the
    mov al, [si+1]               ; position
    mov dl, [si+2]
    xor ch, ch
    add [x3p_sbp], cx
    mov ch, cl                      ; CH = n
    mov cl, bl
    and cl, 7
    jz .al
    shl ax, cl                      ; the window's top bits at bit 15
    mov dh, 8
    sub dh, cl
    mov cl, dh
    shr dl, cl
    or al, dl
.al:
    mov cl, 16
    sub cl, ch
    shr ax, cl
    pop si
    pop bx
    ret
