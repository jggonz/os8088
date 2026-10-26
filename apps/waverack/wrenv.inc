; =============================================================================
; os8088 - apps/waverack/wrenv.inc - the waveform (SPEC.md 122.5)
;
; A COARSE ENVELOPE PER SOURCE: the block B is the smallest power of two
; >= 16 frames for which ceil(frames / B) <= 2,048, and each block stores,
; per channel, its minimum and maximum as signed 8-bit (an 8-bit sample -
; 80h; a 16-bit sample's high byte). It is built by a SCAN after an open, a
; gulp of WE_GULP = 16 KB a pass from the window's one-shot timer, so the
; desktop stays live; the status bar says `Scanning 45%`, and Stop, Escape or
; another Open abandons it.
;
; THE VIEW is a first frame and a zoom: fpp = 2^z frames a column, from 1 up
; to the smallest that fits the document (Fit). The first frame is always a
; multiple of fpp, so a column at fpp >= B covers whole blocks.
;
; THE VIEW CACHE (a 4 KB claim, WE_VKB) holds per column and channel a
; minimum and a maximum and an EXACT byte. fpp >= B: made from the blocks,
; exact. fpp < B: first the block's range (a superset, never a fabrication),
; then REFINED from the samples in reads of WE_REFINE = 8 KB a pass, from the
; same timer, abandoned when the view moves; `Refining` until every column
; is exact. A refined column's span includes the previous column's last
; sample, so the trace at small fpp is a line, not dots.
;
; THE WAVEFORM BAND (the claim made at entry, WRL_BANDKB): the ruler's rows,
; then the lanes', then the overview's, each composed as 1bpp and put down
; with OSAPI_GFX_BLIT1 - the ruler in one call; the lanes in one call in
; black and white (the grid is dots in the band) and, in colour, one call a
; run between the grid's columns plus one a grid column (its paper is the
; grid's dark grey, so the trace crossing it stays green); the overview in
; one, or five in colour (the view's bracket in the handle's colour). The
; lanes stay in the band: the cursor's old column is put back from it with
; one 1-pixel blit, and the line drawn again (the playhead's way, wave 3).
; =============================================================================

WE_VKB      equ 4                   ; the view cache's claim
WE_CMAX     equ 640                 ; columns it holds (COMPACT's 615 fit)
WE_EXACT    equ WE_CMAX * 4         ; its exact bytes
WE_YTAB     equ WE_EXACT + WE_CMAX  ; the 256-entry value -> row table
WE_GULP     equ 16384               ; a scan's read
WE_REFINE   equ 8192                ; a refinement's read (SPEC.md 122.5)
WE_NGX      equ 16                  ; the ruler's majors a view can hold
WE_LBLGAP   equ 56                  ; labels at least this far apart
WE_EMPTY    equ 0x807F              ; a min/max pair with nothing in it

; =============================================================================
; THE GEOMETRY - from the layout, at every full paint (wf_setup)
; =============================================================================

; -----------------------------------------------------------------------------
; we_geom - the waveform's and the overview's boxes for this layout; a new
;           width refits the view and rebuilds the cache. clobbers AX, CX,
;           DX, SI, DI
; -----------------------------------------------------------------------------
we_geom:
    mov si, [wd_lp]
    mov ax, [si+WL_WAVE]
    mov [we_bx], ax
    mov cx, [si+WL_WAVE+4]
    sub cx, ax
    inc cx
    mov [we_ww], cx
    mov ax, [si+WL_WAVE+2]
    mov [we_by], ax
    mov ax, [si+WL_RULH]
    mov [we_rulh], ax
    mov ax, [si+WL_WAVE+6]
    sub ax, [si+WL_WAVE+2]
    inc ax
    mov [we_wrows], ax
    sub ax, [we_rulh]
    mov [we_lh], ax
    mov ax, [si+WL_OVW+2]
    mov [we_oy], ax
    mov ax, [si+WL_OVW+6]
    sub ax, [si+WL_OVW+2]
    inc ax
    mov [we_oh], ax
    cmp cx, [wv_ww]                 ; the cache was built for another width
    je .out
    mov [wv_ww], cx
    cmp byte [wo_doc], 0
    je .out
    call we_zfit                    ; Fit for this width; the zoom no wider
    mov al, [wv_zfit]
    cmp [wv_z], al
    jbe .z
    mov [wv_z], al
.z:
    call we_clamp
    call wv_build
.out:
    ret

; we_zfit - [wv_zfit] = the smallest z with ww << z >= the frames. keeps all
; but flags
we_zfit:
    push ax
    push cx
    push dx
    push bx
    xor cl, cl
.try:
    mov ax, [we_ww]                 ; DX:AX = ww << z
    xor dx, dx
    push cx
    xor ch, ch
    jcxz .sh0
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh
.sh0:
    pop cx
    cmp dx, [wo_frames+2]
    ja .fit
    jb .more
    cmp ax, [wo_frames]
    jae .fit
.more:
    inc cl
    cmp cl, 26
    jb .try
.fit:
    mov [wv_zfit], cl
    pop bx
    pop dx
    pop cx
    pop ax
    ret

; we_fpp - DX:AX = 1 << z (frames a column). keeps the rest
we_fpp:
    push cx
    mov ax, 1
    xor dx, dx
    mov cl, [wv_z]
    xor ch, ch
    jcxz .out
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh
.out:
    pop cx
    ret

; we_span - DX:AX = ww << z (the frames the view shows). keeps the rest
we_span:
    push cx
    mov ax, [we_ww]
    xor dx, dx
    mov cl, [wv_z]
    xor ch, ch
    jcxz .out
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; we_clamp - the first frame inside the document - at most frames - span, at
;            least 0 - and on a multiple of fpp. keeps every register
; -----------------------------------------------------------------------------
we_clamp:
    push ax
    push bx
    push cx
    push dx
    call we_span                    ; CX:BX = frames - span, or 0
    mov bx, [wo_frames]
    mov cx, [wo_frames+2]
    sub bx, ax
    sbb cx, dx
    jnc .max
    xor bx, bx
    xor cx, cx
.max:
    mov ax, [wv_first]
    mov dx, [wv_first+2]
    test dh, 0x80                   ; a scroll left of nought
    jz .pos
    xor ax, ax
    xor dx, dx
.pos:
    cmp dx, cx
    jb .in
    ja .cap
    cmp ax, bx
    jbe .in
.cap:
    mov ax, bx
    mov dx, cx
.in:
    push ax                         ; ...on a multiple of fpp: rounded DOWN,
    push dx                         ; so the end stays in view
    call we_fpp
    sub ax, 1
    sbb dx, 0
    not ax
    not dx
    mov bx, ax
    mov cx, dx
    pop dx
    pop ax
    and ax, bx
    and dx, cx
    mov [wv_first], ax
    mov [wv_first+2], dx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE OPEN AND THE SCAN
; =============================================================================

; -----------------------------------------------------------------------------
; we_envsize - the envelope for [wo_frames] (SPEC.md 122.5)
; out: CL = the block shift, BX = the blocks, AX = the claim in KB (0: an
;      empty sound, no claim). clobbers CH, DX
; -----------------------------------------------------------------------------
we_envsize:
    mov cl, 4
    xor bx, bx
    mov ax, [wo_frames]
    mov dx, [wo_frames+2]
    mov ch, al
    or ch, ah
    or ch, dl
    or ch, dh
    jz .empty
    sub ax, 1                       ; (frames - 1) >> shift: the blocks - 1
    sbb dx, 0
.try:
    push ax
    push dx
    push cx
    xor ch, ch
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    pop cx
    or dx, dx
    jnz .more
    cmp ax, 2047
    ja .more
    inc ax
    mov bx, ax                      ; BX = the blocks
    pop dx
    pop ax
    mov ax, bx                      ; the bytes: blocks x channels x 2
    shl ax, 1
    cmp byte [wo_ch], 2
    jne .kb
    shl ax, 1
.kb:
    add ax, 1023
    mov al, ah                      ; >> 10
    shr al, 1
    shr al, 1
    xor ah, ah
    ret
.more:
    pop dx
    pop ax
    inc cl
    jmp short .try
.empty:
    xor ax, ax
    ret

; -----------------------------------------------------------------------------
; we_open - a document was laid: Fit, the cursor at nought, and the scan
;           begun (or, for an empty sound, done). clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
we_open:
    mov al, [wo_src + WS_BS]
    mov [we_bs], al
    call we_zfit
    mov al, [wv_zfit]
    mov [wv_z], al
    xor ax, ax
    mov [wv_first], ax
    mov [wv_first+2], ax
    mov [we_sf], ax
    mov [we_sf+2], ax
    mov [we_boff], ax
    mov byte [we_pct], 0FFh
    call we_bstart
    mov ax, [wo_frames]
    or ax, [wo_frames+2]
    jz we_scandone
    mov byte [we_busy], 1
    call we_scansay
    jmp wr_arm

; we_bstart - a new block: its frames left = B, its range empty. keeps all
we_bstart:
    push ax
    push cx
    push dx
    mov ax, 1
    xor dx, dx
    mov cl, [we_bs]
    xor ch, ch
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh
    mov [we_bleft], ax
    mov [we_bleft+2], dx
    mov word [we_acc], WE_EMPTY
    mov word [we_acc+2], WE_EMPTY
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; we_scanstep - one gulp of the scan: read, fold every frame into its block,
;               store each block as it ends. The timer's. clobbers AX, BX, CX,
;               DX, SI, DI, BP, ES
; -----------------------------------------------------------------------------
we_scanstep:
    mov si, wo_src
    mov ax, [we_sf]
    mov dx, [we_sf+2]
    mov cx, 0xFFFF
    mov bp, WE_GULP
    call wo_srcread                 ; ES:DI = the frames, CX = how many
    jnc .read
    mov byte [ww_err], E_IO         ; the file could not be read: the document
    call wo_new                     ; goes, and the status bar says why
    jmp ww_say
.read:
    jcxz we_scandone
.fold:
    mov ax, cx                      ; n = min(what is here, the block's rest)
    cmp word [we_bleft+2], 0
    jne .n
    cmp ax, [we_bleft]
    jbe .n
    mov ax, [we_bleft]
.n:
    push cx
    push ax
    mov cx, ax
    mov dx, [we_acc]
    mov bx, [we_acc+2]
    call we_mm                      ; ES:DI on past them
    mov [we_acc], dx
    mov [we_acc+2], bx
    pop ax
    pop cx
    sub cx, ax
    add [we_sf], ax
    adc word [we_sf+2], 0
    sub [we_bleft], ax
    sbb word [we_bleft+2], 0
    mov ax, [we_bleft]
    or ax, [we_bleft+2]
    jnz .more
    call we_bstore                  ; a whole block
    call we_bstart
.more:
    or cx, cx
    jnz .fold
    mov ax, [we_sf+2]               ; the whole source?
    cmp ax, [wo_src + WS_FRM+2]
    jb .say
    mov ax, [we_sf]
    cmp ax, [wo_src + WS_FRM]
    jae we_scandone
.say:
    call we_scansay
    jmp wr_arm                      ; the next gulp, a tick from now

; we_scandone - the last part block stored, the cache built, and the status
; bar's note (or Ready). clobbers AX, CX, DX, SI, DI
we_scandone:
    mov ax, [we_acc]
    cmp ax, WE_EMPTY
    je .none
    call we_bstore
.none:
    mov byte [we_busy], 0
    mov byte [ww_err], 0
    call ww_say                     ; the open's note, if it had one...
    jnc .built
    mov si, wr_s_ready              ; ...else Ready
    call wr_say
.built:
    call wv_build
    jmp we_moved

; we_bstore - the block's range into the envelope, and on to the next.
; clobbers AX
we_bstore:
    push di
    push es
    mov es, [wo_src + WS_ENV]
    mov di, [we_boff]
    mov ax, [we_acc]
    stosw
    cmp byte [wo_ch], 2
    jne .m
    mov ax, [we_acc+2]
    stosw
.m:
    mov [we_boff], di
    pop es
    pop di
    ret

; we_scansay - 'Scanning 45%' when the percentage moved. clobbers AX, BX, CX,
; DX, DI
we_scansay:
    mov ax, [we_sf]                 ; sf * 100 / frames, both scaled until the
    mov dx, [we_sf+2]               ; frames fit a word
    mov bx, [wo_frames]
    mov cx, [wo_frames+2]
.sc:
    jcxz .div
    shr cx, 1
    rcr bx, 1
    shr dx, 1
    rcr ax, 1
    jmp short .sc
.div:
    or bx, bx
    jz .out
    mov cx, bx
    mov bx, 100
    call wo_muldiv
    cmp al, [we_pct]
    je .out
    mov [we_pct], al
    mov di, wr_tbuf2
    mov si, we_s_scan
    call wr_put
    xor ah, ah
    call dec_u16
    mov word [di], '%'
    mov si, wr_tbuf2
    jmp wr_say
.out:
    ret

; -----------------------------------------------------------------------------
; we_mm - fold CX (> 0) frames at ES:DI into the ranges DL..DH (the left or
;         only channel) and BL..BH (the right), signed 8-bit: an 8-bit
;         sample - 80h, a 16-bit one's high byte. out DI past them.
;         clobbers AX, CX, SI
; -----------------------------------------------------------------------------
we_mm:
    mov al, [wo_ch]                 ; (read before DS is the buffer's)
    mov ah, [wo_bits]
    push ds
    push es
    pop ds
    mov si, di
    cld
    cmp al, 2
    je .st
    cmp ah, 16
    je .m16
.m8:
    lodsb
    xor al, 0x80
    cmp al, dl
    jge .m8a
    mov dl, al
.m8a:
    cmp al, dh
    jle .m8b
    mov dh, al
.m8b:
    loop .m8
    jmp short .done
.m16:
    inc si
    lodsb
    cmp al, dl
    jge .m16a
    mov dl, al
.m16a:
    cmp al, dh
    jle .m16b
    mov dh, al
.m16b:
    loop .m16
    jmp short .done
.st:
    cmp ah, 16
    je .s16
.s8:
    lodsb
    xor al, 0x80
    cmp al, dl
    jge .s8a
    mov dl, al
.s8a:
    cmp al, dh
    jle .s8b
    mov dh, al
.s8b:
    lodsb
    xor al, 0x80
    cmp al, bl
    jge .s8c
    mov bl, al
.s8c:
    cmp al, bh
    jle .s8d
    mov bh, al
.s8d:
    loop .s8
    jmp short .done
.s16:
    inc si
    lodsb
    cmp al, dl
    jge .s16a
    mov dl, al
.s16a:
    cmp al, dh
    jle .s16b
    mov dh, al
.s16b:
    inc si
    lodsb
    cmp al, bl
    jge .s16c
    mov bl, al
.s16c:
    cmp al, bh
    jle .s16d
    mov bh, al
.s16d:
    loop .s16
.done:
    mov di, si
    pop ds
    ret

; =============================================================================
; THE VIEW CACHE
; =============================================================================

; -----------------------------------------------------------------------------
; we_range - the envelope's range over document frames [DX:AX, [we_f1]):
;            every block those frames touch, through the pieces, both
;            channels. out [we_racc] = minL, maxL, minR, maxR.
;            clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
we_range:
    mov word [we_racc], WE_EMPTY
    mov word [we_racc+2], WE_EMPTY
.piece:
    cmp dx, [we_f1+2]               ; done when f0 >= f1
    jb .go
    ja .out
    cmp ax, [we_f1]
    jae .out
.go:
    push ax
    push dx
    call wo_locate                  ; SI = the piece, DX:AX = the offset,
    jc .out2                        ; CX:BX = what is left of it
    mov di, [we_f1]                 ; n = min(left, f1 - f0) -> CX:BX
    push ax
    push dx
    mov ax, di
    mov dx, [we_f1+2]
    mov di, sp                      ; (f0 is under the offset: lo at +6)
    sub ax, [ss:di+6]
    sbb dx, [ss:di+4]
    cmp dx, cx
    ja .left
    jb .need
    cmp ax, bx
    jae .left
.need:
    mov bx, ax
    mov cx, dx
.left:
    pop dx
    pop ax
    mov [we_n], bx
    mov [we_n+2], cx
    add ax, [si+WP_FIRST]           ; the source's frame
    adc dx, [si+WP_FIRST+2]
    push ax
    mov ax, [si+WP_SRC]
    mov di, WS_SIZE
    push dx
    mul di
    pop dx
    add ax, wo_src
    mov si, ax                      ; SI = the source
    pop ax
    mov cl, [si+WS_BS]
    xor ch, ch
    push ax                         ; b0 = s >> bs
    push dx
    call we_shr
    mov di, ax                      ; DI = b0
    pop dx
    pop ax
    add ax, [we_n]                  ; b1 = (s + n - 1) >> bs
    adc dx, [we_n+2]
    sub ax, 1
    sbb dx, 0
    call we_shr                     ; AX = b1
    cmp ax, [si+WS_NBLK]
    jb .b1
    mov ax, [si+WS_NBLK]
    dec ax
.b1:
    sub ax, di
    jb .next
    inc ax
    mov cx, ax                      ; CX = the blocks
    mov al, [si+WS_CH]
    push es
    mov es, [si+WS_ENV]
    shl di, 1
    cmp al, 2
    jne .mono
    shl di, 1
.mono:
    mov dx, [we_racc]
    mov bx, [we_racc+2]
.blk:
    mov ah, [es:di]
    cmp ah, dl
    jge .b2
    mov dl, ah
.b2:
    mov ah, [es:di+1]
    cmp ah, dh
    jle .b3
    mov dh, ah
.b3:
    add di, 2
    cmp al, 2
    jne .b6
    mov ah, [es:di]
    cmp ah, bl
    jge .b4
    mov bl, ah
.b4:
    mov ah, [es:di+1]
    cmp ah, bh
    jle .b5
    mov bh, ah
.b5:
    add di, 2
.b6:
    loop .blk
    mov [we_racc], dx
    mov [we_racc+2], bx
    pop es
.next:
    pop dx                          ; f0 += n
    pop ax
    add ax, [we_n]
    adc dx, [we_n+2]
    jmp .piece
.out2:
    pop dx
    pop ax
.out:
    ret

; we_shr - DX:AX >> CL (CH = 0); out AX = the low word (a block number).
; clobbers DX
we_shr:
    push cx
    jcxz .out
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; wv_build - THE VIEW CACHE for the view as it stands: every column from the
;            blocks; exact at fpp >= B, else the block's range for now and
;            the refinement begun. Nothing while the scan runs.
;            clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wv_build:
    mov byte [wv_ok], 0
    cmp byte [wo_doc], 0
    je .out
    cmp byte [we_busy], 1
    je .out
    cmp word [we_vseg], 0
    je .out
    mov byte [we_busy], 0
    mov byte [we_rcs], 0
    mov word [we_rcol], 0
    mov byte [we_inexact], 0
    call we_fpp
    mov [we_fppd], ax
    mov [we_fppd+2], dx
    mov ax, [wv_first]
    mov dx, [wv_first+2]
    mov [we_f0], ax
    mov [we_f0+2], dx
    xor cx, cx                      ; CX = the column
.col:
    cmp cx, [we_ww]
    jae .done
    push cx
    mov ax, [we_f0]
    mov dx, [we_f0+2]
    cmp dx, [wo_frames+2]           ; past the end: nothing there, exact
    jb .in
    ja .past
    cmp ax, [wo_frames]
    jb .in
.past:
    mov word [we_racc], WE_EMPTY
    mov word [we_racc+2], WE_EMPTY
    mov bl, 1
    jmp short .put
.in:
    mov bl, 1                       ; exact when a column is whole blocks...
    mov cl, [we_bs]
    cmp [wv_z], cl
    jae .whole
    xor bl, bl                      ; ...else its block's range, for now
    mov byte [we_inexact], 1
    mov cx, 1
    jmp short .f1
.whole:
    mov cx, [we_fppd]
.f1:
    push bx
    mov bx, ax                      ; f1 = f0 + (fpp or 1)
    mov si, dx
    add bx, cx
    adc si, 0
    cmp cx, 1
    je .one
    add si, [we_fppd+2]
.one:
    mov [we_f1], bx
    mov [we_f1+2], si
    call we_range
    pop bx
.put:
    pop cx
    push cx
    push es
    mov es, [we_vseg]
    mov di, cx
    shl di, 1
    shl di, 1
    mov ax, [we_racc]
    stosw
    mov ax, [we_racc+2]
    stosw
    mov di, cx
    mov [es:di + WE_EXACT], bl
    pop es
    pop cx
    mov ax, [we_fppd]               ; the next column's first frame
    add [we_f0], ax
    mov ax, [we_fppd+2]
    adc [we_f0+2], ax
    inc cx
    jmp .col
.done:
    mov byte [wv_ok], 1
    cmp byte [we_inexact], 0
    je .out
    mov byte [we_busy], 2           ; the samples, from the timer
    mov byte [we_rsaid], 0
    call wr_arm
.out:
    ret

; -----------------------------------------------------------------------------
; we_refstep - ONE REFINEMENT READ (WE_REFINE bytes): the column being made
;              and as many after it as the read holds, from the samples.
;              The timer's. clobbers AX, BX, CX, DX, SI, DI, BP, ES
; -----------------------------------------------------------------------------
we_refstep:
    cmp byte [we_rcs], 0            ; a column under way?
    jne .read
    call we_rnext                   ; CF = 1: every column is exact
    jnc .read
    jmp we_rdone
.read:
    mov ax, [we_rf]
    mov dx, [we_rf+2]
    mov cx, 0xFFFF
    mov bp, WE_REFINE
    call wo_read                    ; ES:DI, CX = the frames from [we_rf]
    jnc .got
    mov byte [we_busy], 0           ; a read error: the block ranges stand
    mov si, ww_e12
    jmp wr_say
.got:
    or cx, cx
    jnz .fold
    jmp we_rdone0                   ; the read met the end: stop here
.fold:
    mov ax, [we_rend]               ; m = min(CX, rend - rf)
    mov dx, [we_rend+2]
    sub ax, [we_rf]
    sbb dx, [we_rf+2]
    or dx, dx
    jnz .m
    cmp ax, cx
    jbe .m0
.m:
    mov ax, cx
.m0:
    push cx
    push ax
    mov cx, ax
    mov dx, [we_racc]
    mov bx, [we_racc+2]
    call we_mm
    mov [we_racc], dx
    mov [we_racc+2], bx
    pop ax
    pop cx
    sub cx, ax
    add [we_rf], ax
    adc word [we_rf+2], 0
    mov ax, [we_rf]                 ; the column whole?
    cmp ax, [we_rend]
    jne .more
    mov ax, [we_rf+2]
    cmp ax, [we_rend+2]
    jne .more
    call we_rput                    ; stored, exact
    push word [we_rf]               ; the next: where does it start?
    push word [we_rf+2]
    call we_rnext
    pop dx
    pop ax
    jc we_rdone0
    sub ax, 1                       ; it starts on the frame before (the
    sbb dx, 0                       ; last one folded): step back one frame
    cmp ax, [we_rf]
    jne .gap
    cmp dx, [we_rf+2]
    jne .gap
    push cx
    mov cl, [wo_sh]
    mov ax, 1
    shl ax, cl
    sub di, ax
    pop cx
    inc cx
    jmp short .more
.gap:
    add ax, 1                       ; ...or on the frame after
    adc dx, 0
    cmp ax, [we_rf]
    jne .pass
    cmp dx, [we_rf+2]
    jne .pass
.more:
    or cx, cx
    jnz .fold
.pass:
    call we_moved                   ; draw what was refined
    cmp byte [we_rsaid], 0
    jne .armed
    mov byte [we_rsaid], 1
    mov si, we_s_refine
    call wr_say
.armed:
    jmp wr_arm

; we_rdone0 / we_rdone - refinement over (drawn, and Ready if it said so)
we_rdone0:
    call we_moved
we_rdone:
    mov byte [we_busy], 0
    mov byte [we_rcs], 0
    cmp byte [we_rsaid], 0
    je .out
    mov byte [we_rsaid], 0
    mov si, wr_s_ready
    call wr_say
.out:
    ret

; we_rnext - the next column not exact, from [we_rcol]: its frames into
; [we_rf] (from the frame before, when it is not the first) and [we_rend],
; its range emptied, [we_rcs] = 1. CF = 1 none left. clobbers AX, BX, DX
we_rnext:
    push cx
    push es
    mov es, [we_vseg]
    mov bx, [we_rcol]
.find:
    cmp bx, [we_ww]
    jae .none
    cmp byte [es:bx + WE_EXACT], 0
    je .got
    inc bx
    jmp short .find
.got:
    mov [we_rcol], bx
    mov ax, bx                      ; f0 = first + col << z
    xor dx, dx
    mov cl, [wv_z]
    xor ch, ch
    jcxz .z0
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh
.z0:
    add ax, [wv_first]
    adc dx, [wv_first+2]
    push ax
    push dx
    call we_fpp                     ; rend = min(f0 + fpp, frames)
    mov bx, sp
    add ax, [ss:bx+2]
    adc dx, [ss:bx]
    cmp dx, [wo_frames+2]
    jb .end
    ja .clip
    cmp ax, [wo_frames]
    jbe .end
.clip:
    mov ax, [wo_frames]
    mov dx, [wo_frames+2]
.end:
    mov [we_rend], ax
    mov [we_rend+2], dx
    pop dx
    pop ax
    cmp word [we_rcol], 0           ; from the frame before: continuity
    je .first
    mov bx, ax
    or bx, dx
    jz .first
    sub ax, 1
    sbb dx, 0
.first:
    mov [we_rf], ax
    mov [we_rf+2], dx
    mov word [we_racc], WE_EMPTY
    mov word [we_racc+2], WE_EMPTY
    mov byte [we_rcs], 1
    pop es
    pop cx
    clc
    ret
.none:
    mov byte [we_rcs], 0
    pop es
    pop cx
    stc
    ret

; we_rput - the column made: into the cache, exact; the next column next.
; keeps every register
we_rput:
    push ax
    push di
    push es
    mov es, [we_vseg]
    mov di, [we_rcol]
    mov byte [es:di + WE_EXACT], 1
    shl di, 1
    shl di, 1
    mov ax, [we_racc]
    mov [es:di], ax
    mov ax, [we_racc+2]
    mov [es:di+2], ax
    inc word [we_rcol]
    mov byte [we_rcs], 0
    pop es
    pop di
    pop ax
    ret

; we_moved - the waveform and the overview are owed a draw. keeps all
we_moved:
    push ax
    mov ax, [we_wgen]
    call wo_inc
    mov [we_wgen], ax
    mov ax, [we_ogen]
    call wo_inc
    mov [we_ogen], ax
    pop ax
    ret

; =============================================================================
; THE VIEW'S COMMANDS
; =============================================================================

; we_ready - CF = 0 when the view can move: a document, scanned. keeps all
we_ready:
    cmp byte [wo_doc], 0
    je .no
    cmp byte [we_busy], 1
    je .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; we_zoom - AL = -1 in (fpp halved), +1 out (doubled), 0 Fit. The cursor's
;           frame stays at its column when it is in view, else the view's
;           middle does. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
we_zoom:
    or al, al
    jnz .step
    mov al, [wv_zfit]               ; Fit: the whole document from nought
    mov [wv_z], al
    xor ax, ax
    mov [wv_first], ax
    mov [wv_first+2], ax
    jmp short .built
.step:
    push ax
    call we_anchor                  ; DX:AX = the frame, CX = its column
    pop bx
    add bl, [wv_z]
    mov [wv_z], bl
    ; first = frame - (col << z)
    push dx
    push ax
    mov ax, cx
    xor dx, dx
    mov cl, bl
    xor ch, ch
    jcxz .s0
.s:
    shl ax, 1
    rcl dx, 1
    loop .s
.s0:
    mov bx, ax
    mov cx, dx
    pop ax
    pop dx
    sub ax, bx
    sbb dx, cx
    jnc .f
    xor ax, ax
    xor dx, dx
.f:
    mov [wv_first], ax
    mov [wv_first+2], dx
.built:
    call we_clamp
    call wv_build
    jmp we_moved

; we_anchor - DX:AX = the cursor's frame and CX = its column when it is in
; view, else the view's middle frame and column. clobbers BX
we_anchor:
    call we_curcol                  ; AX = the column, CF = 1 out of view
    jc .mid
    mov cx, ax
    mov ax, [wo_cur]
    mov dx, [wo_cur+2]
    ret
.mid:
    mov cx, [we_ww]
    shr cx, 1
    mov ax, cx
    xor dx, dx
    push cx
    mov cl, [wv_z]
    xor ch, ch
    jcxz .m0
.m:
    shl ax, 1
    rcl dx, 1
    loop .m
.m0:
    pop cx
    add ax, [wv_first]
    adc dx, [wv_first+2]
    ret

; -----------------------------------------------------------------------------
; we_scroll - the view by AX (signed) COLUMNS. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
we_scroll:
    cwd                             ; DX:AX = the columns, signed
    mov cl, [wv_z]
    xor ch, ch
    jcxz .s0
.s:
    shl ax, 1
    rcl dx, 1
    loop .s
.s0:
    add ax, [wv_first]
    adc dx, [wv_first+2]
    ; (falls into we_seefirst)

; we_seefirst - DX:AX (signed) as the first frame, clamped; rebuilt only when
; it moved. clobbers AX, BX, CX, DX, SI, DI
we_seefirst:
    push word [wv_first]
    push word [wv_first+2]
    mov [wv_first], ax
    mov [wv_first+2], dx
    call we_clamp
    pop dx
    pop ax
    cmp ax, [wv_first]
    jne .moved
    cmp dx, [wv_first+2]
    je .out
.moved:
    call wv_build
    call we_moved
.out:
    ret

; we_centre - the view's middle at document frame DX:AX. clobbers AX, BX,
; CX, DX, SI, DI
we_centre:
    push ax
    push dx
    mov ax, [we_ww]
    shr ax, 1
    xor dx, dx
    mov cl, [wv_z]
    xor ch, ch
    jcxz .s0
.s:
    shl ax, 1
    rcl dx, 1
    loop .s
.s0:
    mov bx, ax
    mov cx, dx
    pop dx
    pop ax
    sub ax, bx
    sbb dx, cx
    jmp we_seefirst                 ; (a negative first is clamped to 0)

; -----------------------------------------------------------------------------
; we_setcur - the cursor at frame DX:AX (signed; clamped to 0..frames), and
;             the view scrolled to show it. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
we_setcur:
    test dh, 0x80
    jz .pos
    xor ax, ax
    xor dx, dx
.pos:
    cmp dx, [wo_frames+2]
    jb .in
    ja .end
    cmp ax, [wo_frames]
    jbe .in
.end:
    mov ax, [wo_frames]
    mov dx, [wo_frames+2]
.in:
    cmp ax, [wo_cur]
    jne .moved
    cmp dx, [wo_cur+2]
    je .out
.moved:
    mov [wo_cur], ax
    mov [wo_cur+2], dx
    call we_cmoved
    call we_ready                   ; scanned: keep it in view
    jc .out
    call we_curcol
    jnc .out
    mov ax, [wo_cur]                ; left of the view: it becomes the first
    mov dx, [wo_cur+2]
    cmp dx, [wv_first+2]
    jb we_seefirst
    ja .right
    cmp ax, [wv_first]
    jb we_seefirst
.right:
    push ax                         ; right of it: the last column
    push dx
    mov ax, [we_ww]
    dec ax
    xor dx, dx
    mov cl, [wv_z]
    xor ch, ch
    jcxz .r0
.r:
    shl ax, 1
    rcl dx, 1
    loop .r
.r0:
    mov bx, ax
    mov cx, dx
    pop dx
    pop ax
    sub ax, bx
    sbb dx, cx
    jmp we_seefirst
.out:
    ret

; we_curcol - AX = the cursor's column, CF = 1 when it is not in view (or
; there is nothing to show it on). keeps the rest
we_curcol:
    cmp byte [wo_doc], 0
    je .no
    push cx
    push dx
    mov ax, [wo_cur]
    mov dx, [wo_cur+2]
    sub ax, [wv_first]
    sbb dx, [wv_first+2]
    jc .no2
    mov cl, [wv_z]
    xor ch, ch
    call we_shr                     ; (DX:AX >> z; DX left as the high part)
    or dx, dx
    jnz .no2
    cmp ax, [we_ww]
    jb .in
    jne .no2                        ; one past the last column: the END of
    mov dx, [wo_cur]                ; the document stands there when the
    cmp dx, [wo_frames]             ; view ends on it (we_clamp cannot
    jne .no2                        ; scroll further) - its line is drawn
    mov dx, [wo_cur+2]              ; on the last column
    cmp dx, [wo_frames+2]
    jne .no2
    dec ax
.in:
    pop dx
    pop cx
    clc
    ret
.no2:
    pop dx
    pop cx
.no:
    stc
    ret

; we_colframe - CX = a column -> DX:AX = its first frame. keeps the rest
we_colframe:
    push cx
    mov ax, cx
    xor dx, dx
    mov cl, [wv_z]
    xor ch, ch
    jcxz .s0
.s:
    shl ax, 1
    rcl dx, 1
    loop .s
.s0:
    add ax, [wv_first]
    adc dx, [wv_first+2]
    pop cx
    ret

; =============================================================================
; THE BAND: composing and putting down
; =============================================================================

; we_rowoff - BX = a band row -> DI = its offset. keeps the rest
we_rowoff:
    push ax
    push dx
    mov ax, bx
    mul word [wf_wst]
    mov di, ax
    pop dx
    pop ax
    ret

; we_clr - band rows BX.. (DX of them) cleared. keeps every register
we_clr:
    push ax
    push cx
    push di
    push es
    call we_rowoff
    mov es, [wr_bseg]
    mov ax, [wf_wst]
    push dx
    mul dx
    pop dx
    mov cx, ax
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    ret

; we_row - band row BX set across its whole stride (a centre line: the blit
; draws only the columns it is asked for). keeps every register
we_row:
    push ax
    push cx
    push di
    push es
    call we_rowoff
    mov es, [wr_bseg]
    mov cx, [wf_wst]
    mov al, 0xFF
    cld
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    ret

; we_pix - column CX (0..ww-1) on band row BX. keeps every register
we_pix:
    push ax
    push cx
    push di
    push es
    call we_rowoff
    mov es, [wr_bseg]
    add cx, [wf_wk]
    mov ax, cx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax
    and cl, 7
    mov al, 0x80
    shr al, cl
    or [es:di], al
    pop es
    pop di
    pop cx
    pop ax
    ret

; we_vspan - column CX, band rows BX..DX (BX <= DX) set: a trace's span.
; keeps every register
we_vspan:
    push ax
    push cx
    push dx
    push di
    push es
    call we_rowoff
    mov es, [wr_bseg]
    add cx, [wf_wk]
    mov ax, cx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax
    and cl, 7
    mov al, 0x80
    shr al, cl
    sub dx, bx
    inc dx
    mov cx, dx
.r:
    or [es:di], al
    add di, [wf_wst]
    loop .r
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; we_dots - every DX-th column of band row BX, from column AX. keeps all
we_dots:
    push cx
    mov cx, ax
.d:
    cmp cx, [we_ww]
    jae .out
    call we_pix
    add cx, dx
    jmp short .d
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; we_blit - put band columns CX..DI (inclusive) of DX rows from band row SI
;           onto the glass at content row BX; AL = the ink role, AH = the
;           paper role. ONE OSAPI_GFX_BLIT1 (none when the run is empty).
;           keeps every register
; -----------------------------------------------------------------------------
we_blit:
    or dx, dx
    jz .none
    cmp cx, di
    jg .none
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push si
    push bx
    mov si, ax                      ; the pen, through the theme
    and si, 0xFF
    call wd_rv
    mov bx, si                      ; BL = the ink
    mov al, ah
    xor ah, ah
    mov si, ax
    call wd_rv
    mov ax, si
    mov ah, al                      ; AH = the paper
    mov al, bl
    and ax, 0x0F0F
    call OSAPI_GFX_BLIT1_PEN
    pop bx
    pop si
    mov bp, di                      ; BP = the last column
    push bx
    mov bx, si
    call we_rowoff                  ; DI = the row's offset
    pop bx
    mov ax, cx                      ; ...and the byte holding column c0
    add ax, [wf_wk]
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax
    mov si, di
    mov ax, bp
    sub ax, cx
    inc ax
    push ax                         ; the width
    mov ax, [wd_ox]                 ; the screen: x, y
    add ax, [we_bx]
    add ax, cx
    add bx, [wd_oy]
    pop cx
    push ax
    push bx
    push cx
    push dx
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call wd_cut
    pop dx
    pop cx
    pop bx
    pop ax
    mov bp, [wf_wst]
    mov es, [wr_bseg]
    call OSAPI_GFX_BLIT1
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.none:
    ret

; -----------------------------------------------------------------------------
; we_putcols - the lanes' columns CX..DI from the band onto the glass: one
;              call in black and white; in colour one a run between the
;              grid's columns and one a grid column, whose paper is the
;              grid's colour (so a trace crossing it keeps its own).
;              keeps every register
; -----------------------------------------------------------------------------
we_putcols:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [we_pc1], di
    mov si, [we_rulh]
    mov bx, [we_by]
    add bx, si
    mov dx, [we_lh]
    cmp word [wd_th], wrart_tbw
    je .last
    mov word [we_pgi], 0
.g:
    mov di, [we_pgi]
    cmp di, [we_ng]
    jae .last
    inc word [we_pgi]
    shl di, 1
    mov ax, [we_gx + di]
    cmp ax, cx
    jb .g                           ; left of the run: passed
    cmp ax, [we_pc1]
    ja .last                        ; right of it: the run is done
    mov di, ax
    dec di
    push ax
    mov ax, (R_LCD << 8) | R_WAVE   ; the run up to it...
    call we_blit
    pop cx
    mov di, cx
    mov ax, (R_GRID << 8) | R_WAVE  ; ...and the grid column itself
    call we_blit
    inc cx
    jmp short .g
.last:
    mov di, [we_pc1]
    mov ax, (R_LCD << 8) | R_WAVE
    call we_blit
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; we_bank - the band's phase and stride for the waveform's x (wf_wbank).
; keeps every register
we_bank:
    push ax
    push di
    mov ax, [we_ww]
    mov [wf_ww], ax
    mov di, [wd_lp]
    call wf_wbank
    pop di
    pop ax
    ret

; we_target - wd_bor (the small caps, the pictures) composes into the
;             waveform band, not wrdraw.inc's. wd_bnew points it back. keeps
;             every register
we_target:
    push ax
    mov ax, [wr_bseg]
    mov [wd_tseg], ax
    mov word [wd_tbase], 0
    mov ax, [wf_wst]
    mov [wd_tst], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; we_draw - THE WAVEFORM: the ruler composed and put down, then the lanes -
;           grid, centre lines, the trace from the view cache - composed and
;           put down. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
we_draw:
    call we_bank
    call we_target
    xor bx, bx                      ; --- the ruler -------------------------
    mov dx, [we_rulh]
    call we_clr
    call we_ruler
    xor cx, cx
    mov di, [we_ww]
    dec di
    xor si, si
    mov dx, [we_rulh]
    mov bx, [we_by]
    mov ax, (R_LCD << 8) | R_RULER
    call we_blit
    mov bx, [we_rulh]               ; --- the lanes -------------------------
    mov dx, [we_lh]
    call we_clr
    mov ax, [we_lh]                 ; their geometry: one lane, or two and a
    mov bx, [we_rulh]               ; row between
    mov [we_lt0], bx
    cmp byte [wo_ch], 2
    jne .mono
    dec ax
    shr ax, 1
    mov [we_lnh], ax
    add bx, ax
    mov [we_lsep], bx
    inc bx
    mov [we_lt1], bx
    jmp short .ytab
.mono:
    mov [we_lnh], ax
.ytab:
    call we_ytab                    ; ES = the cache: the value -> row table
    cmp word [wd_th], wrart_tbw     ; black and white: the grid is dots in
    jne .lines                      ; the band
    mov si, [we_ng]
    shl si, 1
.gc:
    sub si, 2
    jb .grows
    mov cx, [we_gx + si]
    mov bx, [we_rulh]
    mov dx, [we_lh]
    add dx, bx
.gcr:
    call we_pix
    add bx, 4
    cmp bx, dx
    jb .gcr
    jmp short .gc
.grows:
    cmp byte [wo_ch], 2             ; ...and one lane's quarter rows
    je .lines
    mov bl, [es:WE_YTAB + 192]
    xor bh, bh
    add bx, [we_lt0]
    xor ax, ax
    mov dx, 4
    call we_dots
    mov bl, [es:WE_YTAB + 64]
    xor bh, bh
    add bx, [we_lt0]
    call we_dots
.lines:
    mov bl, [es:WE_YTAB + 128]      ; the centre line of each lane
    xor bh, bh
    add bx, [we_lt0]
    call we_row
    cmp byte [wo_ch], 2
    jne .trace
    mov bl, [es:WE_YTAB + 128]
    xor bh, bh
    add bx, [we_lt1]
    call we_row
    mov bx, [we_lsep]               ; the row between: dotted
    xor ax, ax
    mov dx, 2
    call we_dots
.trace:
    cmp byte [wv_ok], 0
    je .put
    xor cx, cx                      ; every column, every channel: a span
.col:
    cmp cx, [we_ww]
    jae .put
    mov si, cx
    shl si, 1
    shl si, 1
    mov ax, [es:si]                 ; AL = min, AH = max (signed)
    mov bx, [we_lt0]
    call we_span1
    cmp byte [wo_ch], 2
    jne .nc
    mov ax, [es:si+2]
    mov bx, [we_lt1]
    call we_span1
.nc:
    inc cx
    jmp short .col
.put:
    cmp byte [wo_ch], 2             ; L and R, where a lane holds a letter:
    jne .putx                       ; on a cleared box, so a loud lane does
    cmp word [we_lnh], 9            ; not swallow its own name
    jb .putx
    mov bx, [we_lt0]
    mov si, we_s_l
    call we_lane1
    mov bx, [we_lt1]
    mov si, we_s_r
    call we_lane1
.putx:
    xor cx, cx
    mov di, [we_ww]
    dec di
    jmp we_putcols

; we_lane1 - lane name SI at the top left of the lane whose top is band row
; BX: columns 1..7, rows 0..8 cleared, the letter at column 2, row 1.
; clobbers AX, CX, DX, DI
we_lane1:
    push bx
    push es
    mov es, [wr_bseg]
    mov dx, 9
.row:
    call we_rowoff
    mov ax, [wf_wk]                 ; columns 1..7: the bits from k + 1
    inc ax
    mov cx, ax
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax
    and cl, 7
    mov ax, 0xFE00                  ; seven bits, shifted to the column
    shr ax, cl
    not ax
    and [es:di], ah
    and [es:di+1], al
    inc bx
    dec dx
    jnz .row
    pop es
    pop bx
    push bx
    mov ax, [wf_wk]
    add ax, 2
    inc bx
    call wd_bcaps
    pop bx
    ret

; we_span1 - column CX's range AL (min) .. AH (max) in the lane whose top is
; band row BX, through the table at ES:WE_YTAB. Nothing when the range is
; empty. keeps every register
we_span1:
    cmp al, ah
    jg .out
    push ax
    push bx
    push dx
    push si
    mov si, bx
    xor bh, bh
    mov bl, ah                      ; the max: the top row
    xor bl, 0x80
    mov dl, [es:WE_YTAB + bx]
    mov bl, al                      ; the min: the bottom
    xor bl, 0x80
    mov dh, [es:WE_YTAB + bx]
    mov bl, dl
    xor bh, bh
    add bx, si
    mov dl, dh
    xor dh, dh
    add dx, si
    call we_vspan
    pop si
    pop dx
    pop bx
    pop ax
.out:
    ret

; we_ytab - ES = the cache; the table at WE_YTAB for a lane [we_lnh] rows
; tall, built when the height changed: row = ((255 - u) * (h - 1) + 127) /
; 255 for u = the sample + 80h - no multiply per pixel. clobbers AX, BX, CX,
; DX, DI
we_ytab:
    mov es, [we_vseg]
    mov cx, [we_lnh]
    cmp cx, [we_yh]
    je .out
    mov [we_yh], cx
    dec cx
    mov di, WE_YTAB
    xor bx, bx
.u:
    mov ax, 255
    sub ax, bx
    mul cx
    add ax, 127
    adc dx, 0
    push bx
    mov bx, 255
    div bx
    pop bx
    mov [es:di+bx], al
    inc bx
    cmp bx, 256
    jb .u
.out:
    ret

; -----------------------------------------------------------------------------
; we_ruler - THE RULER into band rows 0..RULH-1: majors on a 1/2/5 series in
;            milliseconds, the first that puts WE_LBLGAP pixels between
;            them, aligned to the view's frames; four minors between; a
;            label right of each major where it fits and clears the last.
;            The majors' columns go to we_gx (the grid stands on them).
;            clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
we_ruler:
    mov word [we_ng], 0
    mov word [we_lend], 0xFFF0      ; (signed: before column 0)
    cmp word [wo_rate], 0
    jne .rate
    ret
.rate:
    mov ax, WE_LBLGAP               ; the target: 56 << z frames a step
    xor dx, dx
    mov cl, [wv_z]
    xor ch, ch
    jcxz .t0
.t:
    shl ax, 1
    rcl dx, 1
    loop .t
.t0:
    mov [we_tgt], ax
    mov [we_tgt+2], dx
    mov word [we_base], 1
    mov word [we_base+2], 0
.dec:
    xor si, si                      ; x1, x2, x5
.m:
    mov ax, [we_base]
    mov dx, [we_base+2]
    cmp si, 1
    jb .x1
    je .x2
    mov bx, ax
    mov cx, dx
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    add ax, bx
    adc dx, cx
    jmp short .x1
.x2:
    shl ax, 1
    rcl dx, 1
.x1:
    mov [we_s], ax
    mov [we_s+2], dx
    mov bx, [wo_rate]               ; its frames: s * rate / 1000
    mov cx, 1000
    call wo_muldiv
    cmp dx, [we_tgt+2]
    ja .found
    jb .next
    cmp ax, [we_tgt]
    jae .found
.next:
    inc si
    cmp si, 3
    jb .m
    mov ax, [we_base]               ; the next decade
    mov dx, [we_base+2]
    shl ax, 1
    rcl dx, 1
    mov bx, ax
    mov cx, dx
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    add ax, bx
    adc dx, cx
    cmp dx, 0x3B9A                  ; 10^9: the series ends there
    jae .found
    mov [we_base], ax
    mov [we_base+2], dx
    jmp short .dec
.found:
    mov cl, [wv_z]                  ; the spacing in pixels
    xor ch, ch
    call we_shr
    or dx, dx
    jz .dp
    mov ax, 0x7FFF
.dp:
    mov [we_dpx], ax
    mov ax, [we_s]                  ; the labels' decimals, from the step
    mov dx, [we_s+2]
    mov cx, 1000
    mov bx, ax
    mov ax, dx
    xor dx, dx
    div cx
    mov ax, bx
    div cx                          ; DX = s mod 1000
    xor bl, bl
    or dx, dx
    jz .dd
    inc bl
    mov ax, dx
    xor dx, dx
    mov cx, 100
    div cx
    or dx, dx
    jz .dd
    inc bl
    mov ax, dx
    xor dx, dx
    mov cx, 10
    div cx
    or dx, dx
    jz .dd
    inc bl
.dd:
    mov [we_dec], bl
    ; --- the first major: the step's first multiple at or after the view ----
    mov ax, [wv_first]              ; t0 = first * 1000 / rate
    mov dx, [wv_first+2]
    mov bx, 1000
    mov cx, [wo_rate]
    call wo_muldiv
    add ax, [we_s]                  ; t = ceil(t0 / s) * s
    adc dx, [we_s+2]
    sub ax, 1
    sbb dx, 0
    push ax
    push dx
    mov bx, [we_s]
    mov cx, [we_s+2]
    call wo_div32                   ; CX:BX = the remainder
    pop dx
    pop ax
    sub ax, bx
    sbb dx, cx
    mov [we_t], ax
    mov [we_t+2], dx
.tick:
    cmp word [we_ng], WE_NGX
    jae .ticks
    mov ax, [we_t]                  ; its frame, and its column
    mov dx, [we_t+2]
    mov bx, [wo_rate]
    mov cx, 1000
    call wo_muldiv
    sub ax, [wv_first]
    sbb dx, [wv_first+2]
    jc .adv
    mov cl, [wv_z]
    xor ch, ch
    call we_shr
    or dx, dx
    jnz .ticks
    cmp ax, [we_ww]
    jae .ticks
    mov si, [we_ng]
    shl si, 1
    mov [we_gx + si], ax
    inc word [we_ng]
    call we_label
.adv:
    mov ax, [we_s]
    add [we_t], ax
    mov ax, [we_s+2]
    adc [we_t+2], ax
    jnc .tick
    ; --- the ticks: majors, and four minors a step ---------------------------
.ticks:
    mov ax, [we_rulh]               ; the label's row, the major's first, the
    sub ax, 13                      ; minor's first
    jns .ly
    xor ax, ax
.ly:
    add ax, 7
    mov [we_mst], ax
    mov ax, [we_rulh]
    sub ax, [we_mst]
    inc ax
    shr ax, 1
    mov bx, [we_rulh]
    sub bx, ax
    mov [we_nst], bx
    mov dx, [we_rulh]
    dec dx
    xor si, si
.maj:
    cmp si, [we_ng]
    jae .lead
    mov di, si
    shl di, 1
    mov cx, [we_gx + di]
    mov bx, [we_mst]
    call we_vspan
    call we_minors
    inc si
    jmp short .maj
.lead:
    cmp word [we_ng], 0             ; the minors left of the first major
    je .out
    mov cx, [we_gx]
    sub cx, [we_dpx]
    call we_minors
.out:
    ret

; we_minors - the four minors after column CX (signed), d = [we_dpx] apart in
; fifths; DX = the ruler's last row. keeps every register
we_minors:
    push ax
    push bx
    push cx
    push si
    push dx
    mov si, cx
    mov bx, 1
.j:
    mov ax, [we_dpx]
    mul bx
    push bx
    mov bx, 5
    div bx
    pop bx
    mov cx, si
    add cx, ax
    js .nx
    cmp cx, [we_ww]
    jae .nx
    push bx
    mov bx, [we_nst]
    mov dx, [we_rulh]
    dec dx
    call we_vspan
    pop bx
.nx:
    inc bx
    cmp bx, 5
    jb .j
    pop dx
    pop si
    pop cx
    pop bx
    pop ax
    ret

; we_label - the label for the major at column AX, time [we_t] ms with
; [we_dec] decimals: into the band right of the tick, where it fits and
; clears the last one. keeps AX, SI
we_label:
    push ax
    push si
    push ax
    mov di, wr_tbuf
    mov ax, [we_t]                  ; seconds and thousandths
    mov dx, [we_t+2]
    mov cx, 1000
    mov bx, ax
    mov ax, dx
    xor dx, dx
    div cx
    xchg ax, bx
    div cx                          ; BX:AX = the seconds, DX = the rest
    push dx
    or bx, bx
    jnz .min
    cmp ax, 60
    jb .sec
.min:
    mov cx, 60                      ; 'M:SS'
    push ax
    mov ax, bx
    xor dx, dx
    div cx
    mov bx, ax
    pop ax
    div cx                          ; BX:AX = the minutes, DX = the seconds
    push dx
    mov dx, bx
    call dec_u32
    mov byte [di], ':'
    inc di
    pop ax
    call wo_two
    jmp short .frac
.sec:
    call dec_u16
.frac:
    pop ax                          ; the thousandths, as many as the step
    mov cl, [we_dec]                ; needs
    or cl, cl
    jz .txt
    mov byte [di], '.'
    inc di
    mov bx, 100
.fd:
    xor dx, dx
    div bx
    add al, '0'
    mov [di], al
    inc di
    push dx
    mov ax, bx
    xor dx, dx
    mov bx, 10
    div bx
    mov bx, ax
    pop ax
    dec cl
    jnz .fd
.txt:
    mov byte [di], 0
    sub di, wr_tbuf                 ; its width: 6 a letter, less the last gap
    mov ax, di
    shl ax, 1
    add ax, di
    shl ax, 1
    dec ax
    pop cx                          ; CX = the column
    add cx, 2
    mov bx, cx
    add bx, ax
    cmp bx, [we_ww]
    ja .skip
    mov ax, [we_lend]
    add ax, 4
    cmp cx, ax
    jl .skip
    mov [we_lend], bx
    mov ax, cx
    add ax, [wf_wk]
    mov bx, [we_rulh]
    sub bx, 13
    jns .y
    xor bx, bx
.y:
    mov si, wr_tbuf
    call wd_bcaps
.skip:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; we_ovdraw - THE OVERVIEW: the whole document at the overview's own
;             resolution (any frames a column - not a power of two), both
;             channels' envelope in one (their union: neither is dropped),
;             and the view's extent bracketed. clobbers AX, BX, CX, DX, SI,
;             DI
; -----------------------------------------------------------------------------
we_ovdraw:
    call we_bank
    call we_target
    mov bx, [we_wrows]
    mov dx, [we_oh]
    call we_clr
    mov ax, [we_oh]                 ; the centre: the row a 0 sample is on
    dec ax
    mov [we_oh1], ax
    mov ax, 127
    call we_orow
    add bx, [we_wrows]
    call we_row
    cmp byte [we_busy], 1           ; scanned: the envelope
    je .bracket
    mov ax, [wo_frames]
    or ax, [wo_frames+2]
    jz .bracket
    xor cx, cx
.col:
    cmp cx, [we_ww]
    jae .bracket
    push cx
    mov ax, [wo_frames]             ; f1 = (c + 1) * frames / ow...
    mov dx, [wo_frames+2]
    mov bx, cx
    inc bx
    mov cx, [we_ww]
    call wo_muldiv
    mov [we_f1], ax
    mov [we_f1+2], dx
    pop cx
    push cx
    mov ax, [wo_frames]             ; ...f0 = c * frames / ow
    mov dx, [wo_frames+2]
    mov bx, cx
    mov cx, [we_ww]
    call wo_muldiv
    cmp dx, [we_f1+2]               ; at least one frame
    jb .rng
    ja .one
    cmp ax, [we_f1]
    jb .rng
.one:
    mov bx, ax
    mov cx, dx
    add bx, 1
    adc cx, 0
    mov [we_f1], bx
    mov [we_f1+2], cx
.rng:
    call we_range
    pop cx
    mov dx, [we_racc]               ; the union of the channels
    cmp byte [wo_ch], 2
    jne .u
    mov bx, [we_racc+2]
    cmp bl, dl
    jge .u1
    mov dl, bl
.u1:
    cmp bh, dh
    jle .u
    mov dh, bh
.u:
    cmp dl, dh
    jg .nx
    mov al, dh                      ; the max: the top row
    cbw
    mov bx, 127
    sub bx, ax
    mov ax, bx
    call we_orow
    push bx
    mov al, dl                      ; the min: the bottom
    cbw
    mov bx, 127
    sub bx, ax
    mov ax, bx
    call we_orow
    mov dx, bx
    pop bx
    add bx, [we_wrows]
    add dx, [we_wrows]
    call we_vspan
.nx:
    inc cx
    jmp .col
.bracket:
    call we_ovext                   ; AX = x0, CX = x1 of the view
    mov [we_ox0], ax
    mov [we_ox1], cx
    mov bx, [we_wrows]              ; the two edges, the overview's height
    mov dx, bx
    add dx, [we_oh1]
    push cx
    mov cx, ax
    call we_vspan
    pop cx
    call we_vspan
    mov cx, [we_ox0]                ; and dotted rows along its top and
.dot:                               ; bottom between them
    add cx, 2
    cmp cx, [we_ox1]
    jae .put
    mov bx, [we_wrows]
    call we_pix
    add bx, [we_oh1]
    call we_pix
    jmp short .dot
.put:
    mov si, [we_wrows]
    mov dx, [we_oh]
    mov bx, [we_oy]
    xor cx, cx
    cmp word [wd_th], wrart_tbw
    je .one1
    mov di, [we_ox0]                ; colour: the edges in the handle's ink
    dec di
    mov ax, (R_LCD << 8) | R_WAVE
    call we_blit
    mov cx, [we_ox0]
    mov di, cx
    mov ax, (R_LCD << 8) | R_HANDLE
    call we_blit
    inc cx
    mov di, [we_ox1]
    dec di
    mov ax, (R_LCD << 8) | R_WAVE
    call we_blit
    mov cx, [we_ox1]
    mov di, cx
    mov ax, (R_LCD << 8) | R_HANDLE
    call we_blit
    inc cx
.one1:
    mov di, [we_ww]
    dec di
    mov ax, (R_LCD << 8) | R_WAVE
    jmp we_blit

; we_orow - AX = 127 - a sample (0..255) -> BX = its overview row,
; (AX * (oh - 1) + 127) / 255. keeps AX, CX
we_orow:
    push ax
    push dx
    mul word [we_oh1]
    add ax, 127
    adc dx, 0
    mov bx, 255
    div bx
    mov bx, ax
    pop dx
    pop ax
    ret

; we_ovext - the view's extent in overview columns: AX = x0, CX = x1 (x0 <=
; x1 < ow). clobbers BX, DX
we_ovext:
    mov ax, [wv_first]
    mov dx, [wv_first+2]
    call we_ofx
    push ax
    call we_span
    add ax, [wv_first]
    adc dx, [wv_first+2]
    call we_ofx
    mov cx, ax
    pop ax
    or cx, cx
    jz .c
    dec cx
.c:
    mov bx, [we_ww]
    dec bx
    cmp cx, bx
    jbe .c1
    mov cx, bx
.c1:
    cmp ax, cx
    jbe .out
    mov ax, cx
.out:
    ret

; we_ofx - document frame DX:AX -> AX = its overview column, frame * ow /
; frames (both scaled until the frames fit a word). clobbers BX, DX
we_ofx:
    push cx
    mov bx, [wo_frames]
    mov cx, [wo_frames+2]
.sc:
    jcxz .d
    shr cx, 1
    rcr bx, 1
    shr dx, 1
    rcr ax, 1
    jmp short .sc
.d:
    or bx, bx
    jz .zero
    mov cx, bx
    mov bx, [we_ww]
    call wo_muldiv
    or dx, dx
    jz .out
    mov ax, 0x7FFF
    jmp short .out
.zero:
    xor ax, ax
.out:
    pop cx
    ret

we_s_scan:   db 'Scanning ', 0
we_s_refine: db 'Refining', 0
we_s_l:      db 'L', 0
we_s_r:      db 'R', 0
