; =============================================================================
; os8088 - apps/audio/apdec.inc
;
; The Audio Player's decoder (SPEC.md 86). Two wire formats reach it and one
; leaves it:
;
;   in    WAVE_FORMAT_PCM       (0x0001) - unsigned 8-bit mono PCM
;         WAVE_FORMAT_DVI_ADPCM (0x0011) - IMA/DVI 4-bit ADPCM, mono
;   out   unsigned 8-bit mono PCM, straight into the stage buffer the worker
;         hands OSAPI_SND_STREAM verb 6
;
; The IMA decoder is written from the public IMA/DVI ADPCM specification - the
; step table and the index table below are the ones every BSD/public-domain
; implementation carries; nothing is vendored (SPEC.md 86.17). The predictor
; is a signed 16-bit value clamped to [-32768, 32767] each step; the sample
; handed on is
;
;         pcm8 = (predictor >> 8) + 128          ; arithmetic >>, exact
;
; which needs no clamp of its own: predictor>>8 is [-128, 127], +128 is
; [0, 255]. There is no intermediate 16-bit PCM buffer (SPEC.md 86.4).
;
; THE DECODER IS RESUMABLE. apd_pull(want) fills up to `want` PCM8 bytes into
; apd_out and returns how many it actually produced. It pulls its input from
; the look-ahead ring through ap_la_get (audio.asm) - one ADPCM block header,
; one data byte, or a run of PCM bytes at a time - and keeps predictor /
; index / block cursor / nibble phase across calls, so a 2048-byte stage
; half never has to line up with an ADPCM block boundary.
;
; State lives in the package bss (the APB/APW accumulator in audio.asm); this
; file only defines routines and the two constant tables. Prefix apd_.
; =============================================================================

; --- IMA step-size table: 89 entries, from the IMA/DVI ADPCM specification ----
apd_step:
    dw 7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66
    dw 73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371
    dw 408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707
    dw 1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484
    dw 7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350
    dw 22385,24623,27086,29794,32767

; --- IMA index-adjustment table: 16 entries, indexed by the whole nibble -----
apd_idxtab:
    db -1,-1,-1,-1, 2, 4, 6, 8
    db -1,-1,-1,-1, 2, 4, 6, 8

; =============================================================================
; apd_start - begin decoding a new track
; in:  AL = codec (AP_FMT_PCM8 / AP_FMT_ADPCM), CX = block align (ADPCM only)
; out: nothing.  preserves every register.
; =============================================================================
apd_start:
    push ax
    push cx
    mov [apd_codec], al
    mov [apd_balign], cx
    mov word [apd_bleft], 0         ; no ADPCM block in progress
    mov byte [apd_hasbyte], 0       ; no data byte in hand
    mov byte [apd_hinib], 0
    mov word [apd_pred], 0
    mov word [apd_index], 0
    mov word [apd_inlen], 0         ; input bounce empty
    mov word [apd_inpos], 0
    pop cx
    pop ax
    ret

; =============================================================================
; apd_getb - next ADPCM input byte from the look-ahead ring, via a 512-byte
;            bounce so the per-byte cost is a compare and an index, not an
;            ap_la_get call (SPEC.md 86.4).
; out: AL = the byte, CF = 0; CF = 1 = the ring is dry (AL undefined)
; preserves every register except AX and flags.
; =============================================================================
apd_getb:
    push bx
    mov bx, [apd_inpos]
    cmp bx, [apd_inlen]
    jb .have
    push cx
    push di
    push es
    push ds
    pop es
    mov cx, APD_INBUF
    mov di, apd_inbuf
    call ap_la_get                  ; AX = bytes copied into apd_inbuf
    pop es
    pop di
    pop cx
    mov [apd_inlen], ax
    xor bx, bx
    or ax, ax
    jz .dry
.have:
    mov al, [apd_inbuf + bx]
    inc bx
    mov [apd_inpos], bx
    pop bx
    clc
    ret
.dry:
    mov [apd_inpos], bx             ; = 0, and inlen = 0
    pop bx
    stc
    ret

; =============================================================================
; apd_pull - produce up to CX PCM8 bytes into apd_out
; in:  CX = bytes wanted (1..APD_OUTSZ)
; out: AX = bytes produced (0..CX). Fewer than CX means the look-ahead ring
;      ran dry - the caller decides whether that is a transient underrun or
;      real end of stream (ap_src_eof).
; preserves every register except AX.
; =============================================================================
apd_pull:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; ES = DS: apd_out and the scratch are ours
    APCNT apc_declc
    mov [apd_want], cx
    xor bx, bx                      ; BX = bytes produced so far

    cmp byte [apd_codec], AP_FMT_ADPCM
    je .adpcm

; --- PCM8: a straight run out of the look-ahead ring --------------------------
    mov cx, [apd_want]
    mov di, apd_out
    call ap_la_get                  ; in CX = max, DI = dst; out AX = got
    jmp .ret                        ; AX already holds the produced count

; --- IMA/DVI ADPCM -----------------------------------------------------------
.adpcm:
.aloop:
    mov cx, [apd_want]
    cmp bx, cx
    jae .adone
    cmp word [apd_bleft], 0
    jne .adata                      ; still inside the current block's data

    ; --- start of a block: pull its 4-byte header (predictor, index, pad) --
    call apd_getb
    jc .adone
    mov ah, al
    call apd_getb
    jc .adone
    xchg al, ah                     ; AX = LE predictor
    mov [apd_pred], ax
    call apd_getb                   ; step index byte
    jc .adone
    xor ah, ah
    cmp al, 88
    jbe .idxok
    mov ax, 88
.idxok:
    mov [apd_index], ax
    call apd_getb                   ; the 4th header byte (reserved)
    jc .adone
    mov cx, [apd_balign]
    sub cx, 4
    mov [apd_bleft], cx             ; data bytes still to consume this block
    mov byte [apd_hasbyte], 0
    mov byte [apd_hinib], 0
    mov ax, [apd_pred]              ; emit the header predictor as sample 0
    call apd_emit
    jmp .aloop

.adata:
    mov cx, [apd_want]
    cmp bx, cx
    jae .adone
    cmp byte [apd_hasbyte], 0
    jne .havebyte
    cmp word [apd_bleft], 0
    je .aloop                       ; block's data exhausted -> next header
    call apd_getb
    jc .adone                       ; end of stream mid-block
    mov [apd_curbyte], al
    mov byte [apd_hasbyte], 1
    mov byte [apd_hinib], 0
    dec word [apd_bleft]
.havebyte:
    mov al, [apd_curbyte]
    cmp byte [apd_hinib], 0
    jne .hi
    and al, 0x0F                    ; low nibble first
    mov byte [apd_hinib], 1
    jmp .donib
.hi:
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1                       ; high nibble (single shifts: CL stays free)
    mov byte [apd_hasbyte], 0       ; the pair is spent
.donib:
    call apd_step_nib              ; AL = nibble -> AX = new predictor
    call apd_emit
    jmp .adata

.adone:
    mov ax, bx                      ; ADPCM path returns the produced count
.ret:
%ifdef APROF
    push ax
    push dx
    xor dx, dx
    add word [apc_decby], ax
    adc word [apc_decby+2], dx
    pop dx
    pop ax
%endif
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; apd_emit - store signed-16 sample AX as PCM8 into apd_out, advance BX
; in:  AX = sample, BX = count so far (also the write index into apd_out)
; out: BX incremented.  clobbers: AX, CX, DI.
;
; NO push/pop: this runs once per decoded sample (~8000/s), where the ~55
; cycles of stack traffic dominated on a 4.77 MHz XT (SPEC.md 86.4). Every
; caller reloads CX from [apd_want] at the top of the ADPCM loop and needs no
; DI across the call.
; -----------------------------------------------------------------------------
apd_emit:
    mov cl, 8
    sar ax, cl                      ; arithmetic shift: -> [-128..127]
    add al, 128                     ; -> [0..255]
    mov di, apd_out
    add di, bx
    mov [di], al
    inc bx
    ret

; -----------------------------------------------------------------------------
; apd_step_nib - advance the IMA predictor by one 4-bit code
; in:  AL = nibble (0..15)
; out: AX = new predictor (signed 16); [apd_pred] / [apd_index] updated
; clobbers: CX, DX, SI, DI.  Preserves BX (the ADPCM loop's output index).
;
; NO push/pop for the same reason as apd_emit - ~135 cycles of stack traffic
; per sample. The one push/pop kept is the predictor across the index update.
; The algorithm is unchanged and still byte-identical to ffmpeg's
; adpcm_ima_wav; the >>2 / >>3 use single shifts so CL is never a divisor.
; -----------------------------------------------------------------------------
apd_step_nib:
    xor ah, ah
    mov si, ax                      ; SI = nibble (0..15)

    mov di, [apd_index]
    add di, di
    mov di, [apd_step + di]         ; DI = step (1..32767), kept across

    mov ax, di                      ; diff = step >> 3
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov cx, ax                      ; CX = diff accumulator (unsigned)

    test si, 4
    jz .n4
    add cx, di                      ; + step
.n4:
    test si, 2
    jz .n2
    mov ax, di
    shr ax, 1
    add cx, ax                      ; + step>>1
.n2:
    test si, 1
    jz .n1
    mov ax, di
    shr ax, 1
    shr ax, 1
    add cx, ax                      ; + step>>2
.n1:
    mov ax, [apd_pred]
    cwd                             ; DX:AX = sign-extended predictor
    test si, 8
    jnz .sub
    add ax, cx
    adc dx, 0
    jmp .clamp
.sub:
    sub ax, cx
    sbb dx, 0
.clamp:
    or dx, dx
    jz .dz
    js .dneg
    mov ax, 0x7FFF                  ; dx > 0 -> above range
    jmp .stp
.dneg:
    cmp dx, -1
    jl .lo                          ; dx <= -2 -> below range
    test ax, 0x8000
    jnz .stp                        ; dx == -1, ax top bit -> in [-32768,-1]
.lo:
    mov ax, 0x8000
    jmp .stp
.dz:
    test ax, 0x8000
    jz .stp                         ; 0..32767 -> fine
    mov ax, 0x7FFF                  ; 32768..65535 -> clamp
.stp:
    mov [apd_pred], ax

    push ax                         ; keep the sample across the index update
    mov al, [apd_idxtab + si]       ; SI still = nibble
    cbw                             ; AX = signed adjustment
    add ax, [apd_index]
    jns .ipos
    xor ax, ax
    jmp .isave
.ipos:
    cmp ax, 88
    jle .isave
    mov ax, 88
.isave:
    mov [apd_index], ax
    pop ax                          ; AX = the clamped predictor (the sample)
    ret
