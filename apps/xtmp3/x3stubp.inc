; =============================================================================
; os8088 - apps/xtmp3/x3stubp.inc - THE FRONT END'S STAND-IN (engine track)
;
; x3gran.inc section 4's procs - x3p_inew, x3p_open, x3p_probe, x3p_start,
; x3p_next, x3p_gran, x3p_reset, x3p_seekpos, x3p_counts, x3p_reftext - over
; the frames of a real MP3, until the STANDARD track's x3pars.inc arrives and
; replaces this file at its %include in xtmp3.asm. It is REAL ABOUT THE
; STREAM'S SHAPE and about nothing else: it skips ID3v2 by its length, syncs
; on a header (and the next, where it is in the buffer), reads Xing/Info's
; frame count and a LAME tag's delay and padding at x3sim's offsets
; (tools/x3sim.py xing_info), refuses Layer I/II and free format by name, and
; then hands out one frame at a time - its bytes consumed from the engine's
; ring exactly as the real parser will consume them. The record it hands a
; backend is the HEADER alone (X3G_HDR bytes): what the stub backend
; (x3stub.asm) reads to make its tone. Nothing here decodes a bit of audio.
;
; So the engine's whole life - the ring, its refill, the worker, the feed,
; seeking, calibration, the conversion, the cache - runs on real files now,
; and phase 3 swaps this file for the real one and the stub part for the real
; backends without the engine changing (docs/xtmp3/ENGINE.md).
;
; Labels: x3p_ (the API's own) and x3sp_ (this stand-in's).

%include "x3stub.inc"
; =============================================================================

; -----------------------------------------------------------------------------
; x3p_inew - DI -> an X3I record, DX:AX = the file's bytes: zeroed, sized
; -----------------------------------------------------------------------------
x3p_inew:
    push ax
    push cx
    push di
    push ax
    mov cx, X3I_SIZE
    xor al, al
    call x3_fillb
    pop ax
    mov [di+X3I_FSIZE], ax
    mov [di+X3I_FSIZE+2], dx
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3p_open - the machine (x3gran.inc section 4): DI -> the record, ES:BX ->
; CX bytes at file offset DX:AX. out: AX = X3P_DONE / X3P_NEED, CF = 0
; -----------------------------------------------------------------------------
x3p_open:
    push bx
    push cx
    push dx
    push si
    mov [x3sp_bo], ax               ; the buffer's file offset
    mov [x3sp_bo+2], dx
    mov [x3sp_bn], cx
    mov al, [di+X3I_STAGE]
    or al, al
    jnz .s1
    ; --- STAGE 0: the head. ID3v2 skipped by its synchsafe length
    xor ax, ax
    xor dx, dx
    cmp cx, 10
    jb .start
    cmp word [es:bx], 'ID'
    jne .start
    cmp byte [es:bx+2], '3'
    jne .start
    push si
    mov si, 6                       ; four bytes of seven bits
.syn:
    mov cx, 7
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh
    mov cl, [es:bx+si]
    and cl, 0x7F
    or al, cl
    inc si
    cmp si, 10
    jb .syn
    pop si
    add ax, 10                      ; the tag's own header
    adc dx, 0
    test byte [es:bx+5], 0x10       ; ...and its footer
    jz .start
    add ax, 10
    adc dx, 0
.start:
    mov [di+X3I_START], ax
    mov [di+X3I_START+2], dx
    mov [di+X3I_SKIP], ax
    mov [di+X3I_SKIP+2], dx
    mov byte [di+X3I_STAGE], 1
    mov [di+X3I_WANT], ax
    mov [di+X3I_WANT+2], dx
    mov word [di+X3I_WANTN], 2048
    call x3sp_covers                ; is it in this buffer already?
    jnc .s1go
    jmp .need
.s1:
    cmp al, 1
    jne .s2
    call x3sp_covers
    jc .need
.s1go:
    call x3sp_sync                  ; FIRST, the header's fields
    jc .nosync
    call x3sp_xing                  ; Xing/Info/LAME, AUDIO
    mov ax, [di+X3I_FSIZE]          ; ID3v1: the last 128 bytes, asked for
    mov dx, [di+X3I_FSIZE+2]
    mov [di+X3I_AEND], ax
    mov [di+X3I_AEND+2], dx
    sub ax, 128
    sbb dx, 0
    jb .dur
    mov [di+X3I_WANT], ax
    mov [di+X3I_WANT+2], dx
    mov word [di+X3I_WANTN], 128
    mov byte [di+X3I_STAGE], 2
    call x3sp_covers
    jc .need
.s2:
    call x3sp_covers
    jc .need
    call x3sp_at                    ; SI -> WANT in the buffer
    cmp word [es:si], 'TA'
    jne .dur
    cmp byte [es:si+2], 'G'
    jne .dur
    mov ax, [di+X3I_WANT]
    mov dx, [di+X3I_WANT+2]
    mov [di+X3I_AEND], ax
    mov [di+X3I_AEND+2], dx
    or byte [di+X3I_FLAGS], X3IF_V1
.dur:
    call x3sp_dur
    mov byte [di+X3I_STAGE], 3
    mov ax, X3P_DONE
    jmp short .out
.nosync:
    mov [di+X3I_REF], al
    mov byte [di+X3I_STAGE], 3
    mov ax, X3P_DONE
    jmp short .out
.need:
    mov ax, X3P_NEED
.out:
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

; x3sp_covers - does the buffer hold [WANT, + min(WANTN, file's rest))?
; CF = 1 not
x3sp_covers:
    push ax
    push dx
    push cx
    mov ax, [di+X3I_WANT]
    mov dx, [di+X3I_WANT+2]
    sub ax, [x3sp_bo]
    sbb dx, [x3sp_bo+2]
    jb .no                          ; before the buffer
    or dx, dx
    jnz .no
    mov cx, [x3sp_bn]
    sub cx, ax
    jbe .no                         ; past it
    cmp cx, [di+X3I_WANTN]
    jae .yes
    mov ax, [x3sp_bo]               ; short: fine only at the file's end
    mov dx, [x3sp_bo+2]
    add ax, [x3sp_bn]
    adc dx, 0
    cmp ax, [di+X3I_FSIZE]
    jne .no
    cmp dx, [di+X3I_FSIZE+2]
    jne .no
.yes:
    pop cx
    pop dx
    pop ax
    clc
    ret
.no:
    pop cx
    pop dx
    pop ax
    stc
    ret

; x3sp_at - SI = BX + (WANT - the buffer's offset). keeps the rest
x3sp_at:
    push ax
    mov si, [di+X3I_WANT]
    sub si, [x3sp_bo]
    add si, bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3sp_sync - from WANT: the first header a Layer III stream believes (the
; next one agreeing too, where the buffer holds it). Fills FIRST, HDR, VER,
; SRI, NCH, RATE, KBPS. out: CF = 1 AL = X3REF_*
; -----------------------------------------------------------------------------
x3sp_sync:
    call x3sp_at                    ; SI -> the scan's start
    mov cx, [x3sp_bn]
    add cx, bx
    sub cx, si                      ; CX = bytes from it
    mov byte [x3sp_ref], X3REF_NOSYN
.s:
    cmp cx, 4
    jb .no
    call x3sp_hdr                   ; at ES:SI: CF = 0 a Layer III header
    jnc .one
    cmp al, 0                       ; a header, but one we refuse by name
    je .n
    mov [x3sp_ref], al
.n:
    inc si
    dec cx
    jmp short .s
.one:
    mov ax, [x3sp_fsz]              ; the next one, where the buffer has it
    cmp ax, cx
    jae .take
    push si
    push cx
    add si, ax
    sub cx, ax
    cmp cx, 4
    jb .t1
    mov al, [x3sp_ver]
    mov ah, [x3sp_sri]
    push ax
    call x3sp_hdr
    pop ax
    jc .t0
    cmp al, [x3sp_ver]
    jne .t0
    cmp ah, [x3sp_sri]
    jne .t0
.t1:
    pop cx
    pop si
    call x3sp_hdr                   ; (the first's fields back)
    jmp short .take
.t0:
    pop cx
    pop si
    jmp short .n
.take:
    mov ax, si                      ; FIRST = the buffer's offset + (SI - BX)
    sub ax, bx
    xor dx, dx
    add ax, [x3sp_bo]
    adc dx, [x3sp_bo+2]
    mov [di+X3I_FIRST], ax
    mov [di+X3I_FIRST+2], dx
    mov [di+X3I_AUDIO], ax
    mov [di+X3I_AUDIO+2], dx
    mov ax, [es:si]
    mov [di+X3I_HDR], ax
    mov ax, [es:si+2]
    mov [di+X3I_HDR+2], ax
    mov al, [x3sp_ver]
    mov [di+X3I_VER], al
    mov al, [x3sp_sri]
    mov [di+X3I_SRI], al
    mov al, [x3sp_nch]
    mov [di+X3I_NCH], al
    mov ax, [x3sp_rate]
    mov [di+X3I_RATE], ax
    mov ax, [x3sp_kbps]
    mov [di+X3I_KBPS], ax
    clc
    ret
.no:
    mov al, [x3sp_ref]
    stc
    ret

; -----------------------------------------------------------------------------
; x3sp_hdr - ES:SI -> four bytes: a Layer III header? CF = 0 and x3sp_ver,
; _sri, _nch, _mode, _rate, _kbps, _fsz, _side set. CF = 1 AL = 0 not a
; header at all, or X3REF_* a header refused by name. keeps all but AL, flags
; -----------------------------------------------------------------------------
x3sp_hdr:
    push bx
    push cx
    push dx
    mov al, [es:si]
    cmp al, 0xFF
    jne .no
    mov al, [es:si+1]
    mov ah, al
    and ah, 0xE0
    cmp ah, 0xE0
    jne .no
    mov ah, al                      ; version: bits 4-3
    shr ah, 1
    shr ah, 1
    shr ah, 1
    and ah, 3
    cmp ah, 1
    je .no                          ; (reserved)
    mov bl, 1
    cmp ah, 3
    je .v
    mov bl, 2
    cmp ah, 2
    je .v
    mov bl, 25
.v:
    mov [x3sp_ver], bl
    mov bh, al                      ; layer: bits 2-1
    shr bh, 1
    and bh, 3
    jz .no
    cmp bh, 1
    je .l3
    mov al, X3REF_L2
    cmp bh, 2
    je .ref
    mov al, X3REF_L1
.ref:
    stc
    jmp .out
.l3:
    mov al, [es:si+2]
    mov bh, al                      ; the bitrate index
    shr bh, 1
    shr bh, 1
    shr bh, 1
    shr bh, 1
    cmp bh, 15
    je .no
    mov ah, al                      ; the rate index
    shr ah, 1
    shr ah, 1
    and ah, 3
    cmp ah, 3
    je .no
    mov [x3sp_sri], ah
    or bh, bh
    jnz .br
    mov al, X3REF_FREE
    jmp short .ref
.br:
    push ax
    mov bl, bh                      ; kbps
    xor bh, bh
    shl bx, 1
    mov ax, [x3sp_kb1+bx]
    cmp byte [x3sp_ver], 1
    je .k
    mov ax, [x3sp_kb2+bx]
.k:
    mov [x3sp_kbps], ax
    pop ax
    mov bl, ah                      ; Hz
    xor bh, bh
    shl bx, 1
    mov dx, [x3sp_hz1+bx]
    cmp byte [x3sp_ver], 1
    je .h
    mov dx, [x3sp_hz2+bx]
    cmp byte [x3sp_ver], 2
    je .h
    mov dx, [x3sp_hz25+bx]
.h:
    mov [x3sp_rate], dx
    and al, 2                       ; the padding bit
    shr al, 1
    mov [x3sp_pad], al
    mov al, [es:si+3]               ; the mode
    mov ah, al
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    mov [x3sp_mode], ah
    mov byte [x3sp_nch], 2
    cmp ah, 3
    jne .c
    mov byte [x3sp_nch], 1
.c:
    mov ax, [x3sp_kbps]             ; the frame: 144000 (72000 on MPEG-2/2.5)
    mov cx, 144                     ; x kbps / rate + padding
    cmp byte [x3sp_ver], 1
    je .m
    mov cx, 72
.m:
    mul cx                          ; DX:AX = kbps x 144
    mov cx, 1000
    mul cx                          ; (fits: 320 x 144 x 1000 < 2^32)
    div word [x3sp_rate]
    add al, [x3sp_pad]
    adc ah, 0
    mov [x3sp_fsz], ax
    cmp ax, 24
    jb .no
    xor al, al
    clc
    jmp short .out
.no:
    xor al, al
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

; x3sp_xing - the first frame at ES:SI (FIRST): a Xing/Info tag's frame
; count and a LAME tag's delay and padding (x3sim xing_info's offsets);
; AUDIO past the tag frame when it is one
x3sp_xing:
    push bx
    push cx
    mov bx, si
    add bx, 4                       ; past the header (and a CRC)
    test byte [es:si+1], 1
    jnz .nc
    add bx, 2
.nc:
    mov ax, 32                      ; the side information's bytes
    cmp byte [x3sp_ver], 1
    je .s1
    mov ax, 17
    cmp byte [x3sp_nch], 1
    jne .s
    mov ax, 9
    jmp short .s
.s1:
    cmp byte [x3sp_nch], 1
    jne .s
    mov ax, 17
.s:
    add bx, ax                      ; ES:BX -> where the tag would be
    cmp word [es:bx], 'Xi'
    jne .info
    cmp word [es:bx+2], 'ng'
    je .tag
.info:
    cmp word [es:bx], 'In'
    jne .out
    cmp word [es:bx+2], 'fo'
    jne .out
.tag:
    or byte [di+X3I_FLAGS], X3IF_TAGF
    mov cl, [es:bx+7]               ; the flags' low byte (big-endian)
    lea si, [bx+8]
    test cl, 1
    jz .nf
    mov ah, [es:si]                 ; frames, big-endian
    mov al, [es:si+1]
    mov [di+X3I_FRAMES+2], ax
    mov ah, [es:si+2]
    mov al, [es:si+3]
    mov [di+X3I_FRAMES], ax
    or byte [di+X3I_FLAGS], X3IF_EXACT
    add si, 4
.nf:
    test cl, 2
    jz .nb
    add si, 4
.nb:
    test cl, 4
    jz .nt
    add si, 100
.nt:
    test cl, 8
    jz .nq
    add si, 4
.nq:
    cmp word [es:si], 'LA'          ; 'LAME' (or Lavf/Lavc: 'La')
    je .lame
    cmp word [es:si], 'La'
    jne .audio
.lame:
    mov al, [es:si+21]              ; delay: 12 bits, padding: 12 bits
    mov ah, [es:si+22]
    mov cl, 4
    xor dh, dh
    mov dl, al
    shl dx, cl
    mov ch, ah
    shr ch, cl
    or dl, ch
    mov [di+X3I_DELAY], dx
    mov dl, ah
    and dl, 15
    mov dh, dl
    mov dl, [es:si+23]
    mov [di+X3I_PAD], dx
    or byte [di+X3I_FLAGS], X3IF_LAME
.audio:
    mov ax, [x3sp_fsz]              ; AUDIO: past the tag frame
    add [di+X3I_AUDIO], ax
    adc word [di+X3I_AUDIO+2], 0
.out:
    pop cx
    pop bx
    ret

; x3sp_dur - DURMS and DUR: frames x samples a frame x 1000 / rate when the
; count is known, else the audio's bytes x 8 / kbps (x3sim's two formulas)
x3sp_dur:
    push bx
    push cx
    push dx
    test byte [di+X3I_FLAGS], X3IF_EXACT
    jz .est
    mov ax, 1152
    cmp byte [di+X3I_VER], 1
    je .s
    mov ax, 576
.s:
    mov cx, ax
    mov ax, [di+X3I_FRAMES]         ; frames x spf = samples...
    mov dx, [di+X3I_FRAMES+2]
    mov bx, 1
    call x3_mdiv
    mov cx, 1000                    ; ...x 1000 / rate = ms
    mov bx, [di+X3I_RATE]
    call x3_mdiv
    jmp short .set
.est:
    mov ax, [di+X3I_AEND]           ; bytes x 8 / kbps = ms
    mov dx, [di+X3I_AEND+2]
    sub ax, [di+X3I_FIRST]
    sbb dx, [di+X3I_FIRST+2]
    mov cx, 8
    mov bx, [di+X3I_KBPS]
    call x3_mdiv
    jmp short .set
.set:
    mov [di+X3I_DURMS], ax
    mov [di+X3I_DURMS+2], dx
    add ax, 500                     ; seconds, rounded half up
    adc dx, 0
    mov cx, 1000
    cmp dx, cx
    jae .big
    div cx
    jmp short .d
.big:
    mov ax, 0xFFFE
.d:
    mov [di+X3I_DUR], ax
    pop dx
    pop cx
    pop bx
    ret

x3sp_kb1:   dw 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320
x3sp_kb2:   dw 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160
x3sp_hz1:   dw 44100, 48000, 32000
x3sp_hz2:   dw 22050, 24000, 16000
x3sp_hz25:  dw 11025, 12000, 8000

; -----------------------------------------------------------------------------
; x3p_probe - title and duration for the queue (UI task): the file's head and
; tail through a transient claim, the instance put back. SI = the name, BL =
; its volume, DX = its folder, DI -> the record. out: CF = 0 complete
; -----------------------------------------------------------------------------
x3p_probe:
    push bx
    push cx
    push dx
    push si
    push es
    mov [x3sp_pn], si
    mov [x3sp_pdi], di
    push bx
    push dx
    call OSAPI_FILE_HERE            ; where we stand, for after
    mov [x3sp_hd], dx
    mov [x3sp_hv], bl
    pop dx
    pop bx
    call OSAPI_FILE_GOTO_QM
    jc .no
    mov ax, 8                       ; 8 KB transient: two 4 KB clusters
    call OSAPI_MEM_CLAIM
    jc .home
    mov [x3sp_pseg], dx
    mov si, [x3sp_pn]
    call x3_fsize                   ; (x3_fsize reads SI's name)
    jc .free
    mov di, [x3sp_pdi]
    call x3p_inew
    xor ax, ax
    xor dx, dx
.rd:
    call x3sp_prd                   ; ES:BX, CX = bytes at DX:AX
    jc .free
    mov di, [x3sp_pdi]
    call x3p_open
    cmp ax, X3P_NEED
    jne .done
    mov ax, [di+X3I_WANT]
    mov dx, [di+X3I_WANT+2]
    jmp short .rd
.done:
    clc
    pushf
    jmp short .fr
.free:
    stc
    pushf
.fr:
    mov dx, [x3sp_pseg]
    call OSAPI_MEM_FREE
    popf
.home:
    pushf
    mov dx, [x3sp_hd]
    mov bl, [x3sp_hv]
    call OSAPI_FILE_GOTO_QM
    popf
    jmp short .out
.no:
    stc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; x3sp_prd - DX:AX = an offset: 8 KB from its 4 KB boundary into the probe's
; claim. out: ES:BX -> the offset's byte, CX = bytes from it; CF = 1 none
x3sp_prd:
    push si
    push di
    mov di, ax
    and di, 0x0FFF
    and ax, 0xF000
    mov es, [x3sp_pseg]
    xor bx, bx
    mov cx, 8192
    mov si, [x3sp_pn]
    push ax
    push dx
    call OSAPI_FILE_READ_AT
    jc .no2
    or dx, dx
    jnz .f
    sub ax, di
    jbe .no2
    mov cx, ax
    jmp short .ok
.f:
    mov cx, 8192
    sub cx, di
.ok:
    mov bx, di
    pop dx
    pop ax
    add ax, di
    clc
    jmp short .out
.no2:
    pop dx
    pop ax
    stc
.out:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; x3p_start - DI -> the record, SI -> the ring (at X3I_AUDIO): the stream's
; frames from the start, the counters cleared
; -----------------------------------------------------------------------------
x3p_start:
    push ax
    mov al, [di+X3I_VER]
    mov [x3sp_sver], al
    mov al, [di+X3I_SRI]
    mov [x3sp_ssri], al
    mov word [x3sp_frame], 0
    mov word [x3sp_frame+2], 0
    pop ax
    clc
    ret

; x3p_reset - after a seek: the next frame is found by a scan from the ring's
; read position
x3p_reset:
    clc
    ret

; -----------------------------------------------------------------------------
; x3p_next - SI -> the ring: the next frame, consumed. out: CF = 0 AL = its
; granules, AH = X3V_OK; CF = 1 AX = X3P_MORE / X3P_END. Garbage before a
; header is skipped (and counted by the real parser)
; -----------------------------------------------------------------------------
x3p_next:
    push bx
    push cx
    push dx
    push di
    push es
    mov es, [si+X3R_SEG]
.scan:
    mov cx, [si+X3R_WR]
    sub cx, [si+X3R_RD]             ; CX = bytes held
    cmp cx, 4
    jb .short
    mov di, [si+X3R_RD]             ; the four header bytes, wrapped into
    mov bx, [si+X3R_MASK]           ; x3sp_hb
    push si
    mov si, di
    and si, bx
    mov al, [es:si]
    mov [x3sp_hb], al
    inc di
    mov si, di
    and si, bx
    mov al, [es:si]
    mov [x3sp_hb+1], al
    inc di
    mov si, di
    and si, bx
    mov al, [es:si]
    mov [x3sp_hb+2], al
    inc di
    mov si, di
    and si, bx
    mov al, [es:si]
    mov [x3sp_hb+3], al
    pop si
    push es
    push si
    push ds
    pop es
    mov si, x3sp_hb
    call x3sp_hdr
    pop si
    pop es
    jc .skip
    mov al, [x3sp_ver]              ; the stream's own version and rate only
    cmp al, [x3sp_sver]
    jne .skip
    mov al, [x3sp_sri]
    cmp al, [x3sp_ssri]
    jne .skip
    mov ax, [x3sp_fsz]              ; the whole frame held?
    cmp ax, cx
    ja .short
    add [si+X3R_RD], ax             ; CONSUMED
    add [si+X3R_POS], ax
    adc word [si+X3R_POS+2], 0
    add word [x3sp_frame], 1
    adc word [x3sp_frame+2], 0
    mov al, [x3sp_nch]
    mov [x3sp_rnch], al
    mov al, [x3sp_mode]
    mov [x3sp_rmode], al
    mov al, 1
    cmp byte [x3sp_ver], 1
    jne .g
    inc al
.g:
    mov [x3sp_ngr], al
    xor ah, ah
    clc
    jmp short .out
.skip:
    inc word [si+X3R_RD]            ; garbage: a byte at a time
    add word [si+X3R_POS], 1
    adc word [si+X3R_POS+2], 0
    jmp .scan
.short:
    mov ax, X3P_MORE
    cmp byte [si+X3R_EOF], 0
    je .no
    mov ax, X3P_END                 ; at the end: a truncated last frame
.no:
    stc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; x3p_gran - AL = the granule: DX:AX -> its record. The stand-in's record is
; the HEADER alone (X3G_HDR bytes, in the primary): the stub backend reads it
; -----------------------------------------------------------------------------
x3p_gran:
    mov [x3sp_rec+X3G_GR], al
    mov al, [x3sp_sver]
    mov [x3sp_rec+X3G_VER], al
    mov al, [x3sp_ssri]
    mov [x3sp_rec+X3G_SRI], al
    mov al, [x3sp_rnch]
    mov [x3sp_rec+X3G_NCH], al
    mov al, [x3sp_rmode]
    mov [x3sp_rec+X3G_MODE], al
    mov al, [x3sp_ngr]
    mov [x3sp_rec+X3G_NGR], al
    mov ax, [x3_srate]
    mov [x3sp_rec+X3G_RATE], ax
    mov ax, [x3sp_frame]
    mov [x3sp_rec+X3G_FRAME], ax
    mov ax, [x3sp_frame+2]
    mov [x3sp_rec+X3G_FRAME+2], ax
    mov dx, ds
    mov ax, x3sp_rec
    clc
    ret

; -----------------------------------------------------------------------------
; x3p_seekpos - DI -> the record, DX:AX = ms: DX:AX = the offset, CX = the
; frame there - by the frame count's average when it is known, by the first
; frame's bitrate when it is not
; -----------------------------------------------------------------------------
x3p_seekpos:
    push bx
    push si
    mov cx, 1000                    ; frame = ms x rate / 1000 / spf
    cmp dx, cx
    jae .end
    div cx                          ; AX = seconds, DX = ms
    push dx
    mul word [di+X3I_RATE]          ; samples
    pop cx
    push dx
    push ax
    mov ax, cx
    mul word [di+X3I_RATE]
    mov cx, 1000
    div cx
    pop bx
    pop dx
    add ax, bx
    adc dx, 0                       ; DX:AX = samples
    mov cx, 1152
    cmp byte [di+X3I_VER], 1
    je .s
    mov cx, 576
.s:
    cmp dx, cx
    jae .end
    div cx                          ; AX = the frame
    mov si, ax
    ; bytes a frame: (AEND - AUDIO) / frames, or the first frame's size
    mov ax, [x3sp_fsz]
    test byte [di+X3I_FLAGS], X3IF_EXACT
    jz .b
    mov bx, [di+X3I_FRAMES]
    or bx, bx
    jz .b
    cmp word [di+X3I_FRAMES+2], 0
    jne .b
    mov ax, [di+X3I_AEND]
    mov dx, [di+X3I_AEND+2]
    sub ax, [di+X3I_AUDIO]
    sbb dx, [di+X3I_AUDIO+2]
    cmp dx, bx
    jae .b
    div bx
.b:
    mul si
    add ax, [di+X3I_AUDIO]
    adc dx, [di+X3I_AUDIO+2]
    cmp dx, [di+X3I_AEND+2]
    jb .ok
    ja .end
    cmp ax, [di+X3I_AEND]
    jb .ok
.end:
    mov ax, [di+X3I_AEND]
    mov dx, [di+X3I_AEND+2]
    mov si, 0xFFFF
.ok:
    mov cx, si
    pop si
    pop bx
    ret

; x3p_counts - ES:DI -> X3PC_SIZE bytes: the stand-in counts only frames
x3p_counts:
    push ax
    push cx
    push di
    mov cx, X3PC_SIZE
    xor al, al
    cld
    rep stosb
    pop di
    mov ax, [x3sp_frame]
    mov [es:di+X3PC_FRAMES], ax
    mov ax, [x3sp_frame+2]
    mov [es:di+X3PC_FRAMES+2], ax
    pop cx
    pop ax
    ret

; x3p_reftext - AL = X3REF_* -> SI = its NUL name. CF = 1 for 0
x3p_reftext:
    or al, al
    jz .no
    push bx
    mov bl, al
    xor bh, bh
    dec bx
    shl bx, 1
    mov si, [x3sp_refs+bx]
    pop bx
    clc
    ret
.no:
    stc
    ret

x3sp_refs:  dw x3sp_r1, x3sp_r2, x3sp_r3, x3sp_r4
x3sp_r1:    db 'FREE FORMAT', 0
x3sp_r2:    db 'LAYER I', 0
x3sp_r3:    db 'LAYER II', 0
x3sp_r4:    db 'NO MPEG AUDIO', 0

%ifdef X3STUB
; -----------------------------------------------------------------------------
; THE TEST HOOK (X3STUB builds only): X3STUB.CFG, read from the folder the
; package was launched from at x3_e_init, is copied into the stub part's data
; each time it is fetched (x3_stubpoke): a speed per rung (x100, 0 = none: as
; fast as the machine), and a mid-track slowdown - from granule N on, rung R
; runs at speed S (docs/xtmp3/ENGINE.md, tests/xtmp3eng.py)
; -----------------------------------------------------------------------------
x3_stubcfg:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    push ds
    pop es
    mov si, x3sp_cfgn
    mov bx, x3sp_cfg
    mov cx, X3SB_CFGSZ
    xor dx, dx
    call OSAPI_FILE_READ
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3_stubpoke - the cfg into the part (ES = its segment), and the rung the
; engine is opening it for (one stub part serves both backends)
x3_stubpoke:
    push cx
    push si
    push di
    mov si, x3sp_cfg
    mov di, X3SB_CFG
    mov cx, X3SB_CFGSZ
    cld
    rep movsb
    mov al, [x3_rung]
    mov [es:X3SB_RUNG], al
    pop di
    pop si
    pop cx
    ret

x3sp_cfgn:  db 'X3STUB.CFG', 0
%endif

; --- the stand-in's bss (X3_STUBPBSS, in xtmp3.asm's X3B list) ---------------------
%macro X3_STUBPBSS 0
    X3B x3sp_bo, 4                  ; x3p_open's buffer: its file offset...
    X3B x3sp_bn, 2                  ; ...and bytes
    X3B x3sp_ref, 1                 ; a refusal seen while syncing
    X3B x3sp_ver, 1                 ; x3sp_hdr's answer: version, rate index,
    X3B x3sp_sri, 1                 ; channels, mode, padding, Hz, kbps, the
    X3B x3sp_nch, 1                 ; frame's bytes
    X3B x3sp_mode, 1
    X3B x3sp_pad, 1
    X3B x3sp_rate, 2
    X3B x3sp_kbps, 2
    X3B x3sp_fsz, 2
    X3B x3sp_sver, 1                ; the stream: its version and rate index
    X3B x3sp_ssri, 1
    X3B x3sp_rnch, 1                ; the staged frame: channels, mode,
    X3B x3sp_rmode, 1               ; granules
    X3B x3sp_ngr, 1
    X3B x3sp_frame, 4               ; frames staged
    X3B x3sp_hb, 4                  ; a header, out of the ring
    X3B x3sp_rec, X3G_HDR           ; the record (its header)
    X3B x3sp_pn, 2                  ; x3p_probe's: the name, the record, the
    X3B x3sp_pdi, 2                 ; claim, home
    X3B x3sp_pseg, 2
    X3B x3sp_hd, 2
    X3B x3sp_hv, 1
 %ifdef X3STUB
    X3B x3sp_cfg, X3SB_CFGSZ        ; X3STUB.CFG
 %endif
%endmacro
