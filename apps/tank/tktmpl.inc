; =============================================================================
; os8088 - apps/tank/tktmpl.inc
;
; THE HUD TEMPLATE'S SPAN STORE (SPEC.md 85.3.5.1)
;
; SPEC.md 85.3.5 put the static panel in a TEMPLATE so that the per-row clear
; could restore it with the same instruction that used to store zeros. That is
; still the design; what changed is where the template's bytes live.
;
; MEASURED, on both shadow backends and in every state the game can be driven
; into - the crack drawn and the ridge settled included: the template held
; 486-512 non-zero bytes of 16,000. THREE PER CENT. A second 16,000-byte buffer
; was carrying about five hundred bytes of picture, and the 32KB claim it made
; is the whole reason SPEC.md 24.5 kept this package off the small disks: on
; the 128KB floor machine the largest run a claimant can have, once mem_claim
; has shed the purgeable caches (SPEC.md 50.6.2), is 18.5KB.
;
; So the template is stored as SPANS of the bytes it actually lit, and the
; claim is a LADDER - 18KB, 17KB or 16KB, whichever the machine can give
; (tk_r_setup) - rather than a flat 32.
;
;   tk_tmrix[r]   the pool offset of row r's first record. Rows are stored in
;                 ascending order and contiguously, so tk_tmrix[r+1] is where
;                 row r's records END - which is why the array is one longer
;                 than the tallest viewport and why tk_tmrix[vh] is [tk_tmlen].
;   a record      db start, len   then `len` bytes of the row
;
; RUNS CLOSER TOGETHER THAN TKT_GAP ARE ONE SPAN, and the zeros between them
; are stored. That is not slack, it is the trade: a record costs two bytes of
; header and one turn of the restore loop, and the panel's runs are SHORT - 280
; spans for 512 bytes, an average run under two bytes - so merging across a
; short gap is cheaper in both bytes and time than the header it saves. The
; stored zeros are harmless because the restore zeroes the run first and then
; lays the spans over it.
;
; WHY THERE IS NO SEPARATE BUFFER TO DRAW INTO. The template is written in
; exactly two places - tk_tmupdate's items and tk_ridge_tm's settle - and BOTH
; run inside tk_render between tk_r_begin and the first dynamic drawing, which
; is the window SPEC.md 85.3.5's own induction is about: the shadow holds no
; dynamic pixel there. In that window the shadow IS the template. So an item is
; drawn into the SHADOW, where it has to end up anyway, and the store is
; re-encoded from it. The walks need no redirection, tk_tmcopy and tk_tmcpruns
; are gone with the buffer they moved between, and the one rule that has to
; hold is that nothing calls tk_tmenc after a dynamic pixel has landed.
;
; THE POOL CAN FILL, and on the bottom two rungs it is meant to be able to. A
; store that will not fit clears [tk_tmpl], which is the flag Mode X already
; runs the whole game on: the clear goes back to storing zeros, tk_hud draws
; the whole panel and tk_ridge_tm the whole ridge, every frame. Correct,
; slower, one-way for the rest of the bracket, and code every VGA exercises
; anyway. The high water measured under tests/tankperf.py's most hostile
; schedule - a ridge transition and a template update every third frame - is
; 2,132 bytes, so the top rung's 2,432 has 14% over it and the middle rung's
; 1,408 is expected to fall back when the ridge settles.
; =============================================================================

; -----------------------------------------------------------------------------
; tk_tmreset - an empty store. The shadow it describes must be zeroed with it,
;              which is tk_clearall's business and the only caller is there
; -----------------------------------------------------------------------------
tk_tmreset:
    push ds
    pop es
    xor ax, ax
    mov [tk_tmlen], ax
    mov di, tk_tmrix
    mov cx, TK_SHROWS + 1
    cld
    rep stosw
    ret

; -----------------------------------------------------------------------------
; THE SCAN IS `scasb`, AND THAT IS THE WHOLE OF WHY THESE TWO ROUTINES LOOK
; LIKE THIS (PERFORMANCE.md part 2's fetch floor)
;
; The first build walked a row a byte at a time in a twenty-byte loop. On the
; 8088 an instruction costs max(clocks, 4.34 x its bytes), so that loop is
; FETCH-bound at ~87 cycles a byte, and re-encoding the ridge's 72-row band -
; 5,760 bytes of it - took the churn schedule's frame to 493 ms. A hitch that
; size on every turn is worse than the 26 ms a frame SPEC.md 85.3.8's template
; was buying back.
;
; `rep scasb` is 15 cycles a byte and TWO BYTES of code for the whole loop, so
; nothing is fetched per iteration at all. A row is ~95% zeros, so the scan is
; three `rep scasb` runs a span - to the next lit byte, past the lit bytes, and
; over the gap that follows - and no per-byte instruction at all. The same
; frame came back to 324 ms.
;
; THERE IS ONE SCAN. There were two - a length pass to size the hole and a put
; pass to fill it - and the length pass measured 9.97 ms of a 27.62 ms encode,
; 36% of it, for an answer the put pass then recomputes by arriving at it.
; tk_tmenc parks the rows above the range at the top of the pool instead and
; writes the new records straight into their final home. What that costs is a
; SECOND move of the parked rows (533 bytes mean, 2.17 ms) and the per-record
; ceiling test in `.emit` (~1.4 ms), against the scan it deletes: 27.62 ->
; 22.33 ms an encode, and 99 bytes off the package.
; -----------------------------------------------------------------------------

; -----------------------------------------------------------------------------
; tk_tmrowput - one row's records, written
; in:  DS = ES = the shadow, SI = the row's offset in it; [tk_tmpen] = the pool
;      pen, which this advances past the records; [tk_tmceil] = the first pool
;      offset it may NOT write
; out: nothing.  Every register preserved. [tk_tmovf] is set - and NOTHING more
;      is written, here or in any later call - if a record would pass the
;      ceiling; tk_tmenc abandons the template when it sees that
;
; ES IS THE SHADOW HERE AND NOT THE POOL, because the scan is scasb's; the pool
; is ES only across each emit, which is per SPAN rather than per byte.
; -----------------------------------------------------------------------------
tk_tmrowput:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, si
    add bp, TK_LASTB + 1
.seek:
    mov di, si
    mov cx, bp
    sub cx, si
    jcxz .done
    xor al, al
    repe scasb
    je .done
    dec di
    mov dx, di
.grow:
    mov si, di
    mov cx, bp
    sub cx, si
    mov di, si
    xor al, al                      ; scasb's needle, and AL is the gap
    repne scasb
    jne .rowend
    dec di
    mov si, di
    mov cx, bp
    sub cx, si
    jcxz .close
    mov di, si
    xor al, al                      ; scasb's needle, and AL is the gap
    repe scasb
    je .close
    dec di
    mov ax, di
    sub ax, si
    cmp ax, TKT_GAP
    jb .grow
    call .emit                      ; the span so far, then another opens at DI
    mov dx, di
    jmp short .grow
.rowend:
    mov si, bp
.close:
    call .emit
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; the span [DX, SI-1], as a record. DI and the flags are the caller's to lose;
; everything else it is still using comes back
.emit:
    push si
    push di
    mov cx, si
    sub cx, dx                      ; CX = the span's bytes
    cmp byte [cs:tk_tmovf], 0       ; ...and is there room for them? The single
    jne .noroom                     ; pass writes into the room it HAS, because
    push ax                         ; it cannot size the hole in advance
    mov ax, [cs:tk_tmpen]
    add ax, cx
    add ax, 2                       ; the record is a header and its bytes
    cmp ax, [cs:tk_tmceil]
    pop ax
    ja .over
    mov ax, dx
    sub ax, bp
    add ax, TK_LASTB + 1            ; AL = its start within the row
    mov ah, cl                      ; AH = its length
    mov si, dx
    mov di, [cs:tk_tmpen]
    mov es, [cs:tk_tmseg]           ; the POOL, for this emit alone
    cld
    stosw                           ; the record's header...
    rep movsb                       ; ...and the row's bytes after it
    mov [cs:tk_tmpen], di
    mov es, [cs:tk_shseg]           ; ES back to the shadow, for scasb
    pop di
    pop si
    ret
.over:
    mov byte [cs:tk_tmovf], 1
.noroom:
    pop di                          ; nothing written, and nothing will be
    pop si
    ret

; -----------------------------------------------------------------------------
; tk_tmenc - rebuild the store for rows AX..BX out of what the shadow holds
; in:  AX = the first row, BX = the last (inclusive; clamped here)
; out: nothing. [tk_tmpl] is cleared if the pool could not hold the result
; clobbers: everything but the segment registers
;
; THE ROWS ARE REBUILT WHOLE, not the caller's rectangle's columns, because a
; row's records are one list where the rectangle is a sub-range of it. That is
; correct for the same reason the routine may be called at all: in this window
; the shadow row holds template content across its whole width.
;
; ONE PASS, BY PARKING. The rows ABOVE the range are moved to the top of the
; pool first; the new records are then written straight into their final home;
; and the parked rows come back down against wherever the pen finished. The
; size of the hole is learnt by ARRIVING at it, so the length pass that used to
; measure it - 9.97 ms of a 27.6 ms encode, 36%, for an answer the put pass
; recomputed anyway - is gone, and tk_tmrowlen with it.
;
; ENCODING INTO THE FREE TAIL WOULD BE SIMPLER AND DOES NOT FIT: it needs a
; transient of tmlen + the new records, and at the measured high water (2,132
; of the top rung's 2,432) a band encode overflows a pool the result fits in.
; Parking needs no transient at all - the space condition stays tmlen + delta
; <= cap, exactly the one the two-pass had.
;
; BOTH MOVES HAVE A FIXED DIRECTION, so neither needs a test. The park is
; always a RIGHT move, because cap - M >= b reduces to cap >= tmlen, the pool's
; own invariant; the unpark is always a LEFT one, because the pen may not pass
; cap - M and [tk_tmceil] is what holds it there. Grow or shrink, copy up or
; copy down - what the two-pass decided per call is decided here by
; construction.
; -----------------------------------------------------------------------------
tk_tmenc:
    cmp byte [tk_tmpl], 0
    je .out
    or ax, ax                       ; the belt to tk_tmreg's braces, four bytes
    jns .c0
    xor ax, ax
.c0:
    cmp bx, [tk_vh]
    jl .c1
    mov bx, [tk_vh]
    dec bx
.c1:
    cmp bx, ax
    jl .out
    mov [tk_tmr0], ax
    mov [tk_tmr1], bx

    ; --- park the rows above the range at the top of the pool ---------------
    shl bx, 1
    mov si, [tk_tmrix + bx + 2]     ; SI = where they start today
    mov cx, [tk_tmlen]
    sub cx, si                      ; CX = M, their bytes
    mov [tk_tmmid], cx
    mov di, [tk_tmcap]
    mov dx, di
    sub dx, cx                      ; cap - M: where they go, and the first
    mov [tk_tmceil], dx             ; pool byte the encode may NOT write
    shl ax, 1
    mov bx, ax
    mov ax, [tk_tmrix + bx]
    mov [tk_tmpen], ax              ; the pen starts where row r0's records do
    mov byte [tk_tmovf], 0
    jcxz .parked
    add si, cx
    dec si                          ; the LAST byte of each: dst > src, so the
    dec di                          ; move runs backwards
    push ds
    push es
    mov ax, [tk_tmseg]
    mov ds, ax
    mov es, ax
    std
    rep movsb
    cld
    pop es
    pop ds
.parked:

    ; --- the records, written straight into their final home ----------------
    mov si, [tk_tmr0]
    shl si, 1
    mov bx, si
    add si, tk_rowoff               ; SI -> tk_rowoff[r0]...
    add bx, tk_tmrix                ; ...BX -> tk_tmrix[r0]
    mov cx, [tk_tmr1]
    sub cx, [tk_tmr0]
    inc cx
.p:
    mov ax, [tk_tmpen]
    mov [bx], ax                    ; row r's records start where the pen is
    push cx
    push bx
    push si
    mov si, [si]
    push ds
    push es
    mov ds, [tk_shseg]              ; DS AND ES: the scan is scasb's, so the
    mov es, [cs:tk_shseg]           ; shadow has to be both
    call tk_tmrowput
    pop es
    pop ds
    pop si
    pop bx
    inc si
    inc si
    inc bx
    inc bx
    pop cx
    loop .p
    cmp byte [tk_tmovf], 0
    jne .full                       ; the pool filled: nothing was written past
                                    ; the ceiling and nothing more will be
    ; --- the parked rows, back down against what the pen reached ------------
.unpark:
    mov cx, [tk_tmmid]
    mov di, [tk_tmpen]
    mov ax, di
    add ax, cx                      ; AX = the pool's new length...
    mov bx, ax
    sub bx, [tk_tmlen]              ; ...BX = the change, signed
    mov [tk_tmlen], ax
    jcxz .fixup
    mov si, [tk_tmcap]
    sub si, cx
    push ds
    push es
    mov ax, [tk_tmseg]
    mov ds, ax
    mov es, ax
    cld
    rep movsb                       ; a LEFT move, the ceiling having kept the
    pop es                          ; pen below where they were parked
    pop ds
.fixup:
    or bx, bx
    jz .out                         ; the same size: nothing after it moved
    mov si, [tk_tmr1]
    shl si, 1
    add si, tk_tmrix
    add si, 2                       ; SI -> tk_tmrix[r1 + 1]
    mov cx, [tk_vh]
    sub cx, [tk_tmr1]               ; ...and there are vh - r1 of them
    jcxz .out
.fx:
    add [si], bx
    inc si
    inc si
    loop .fx
.out:
    ret
; -----------------------------------------------------------------------------
; .full - the pool would not hold the store, so the template is ABANDONED
;
; DROPPING THE FLAG IS NOT ENOUGH, and that was this routine's whole body. From
; the instant [tk_tmpl] is 0 the clear lays no spans - it zeroes what a run
; covers and nothing else - so every pixel the template was holding OUTSIDE
; this frame's runs is stranded in the shadow for the rest of the bracket: in
; no run, in no store, and so beyond the reach of anything that could erase it.
; The panel heals, because tk_hud's full path draws it whole and marks it; the
; SETTLED RIDGE does not, and neither does anything the live frame happens not
; to cover. Measured on os8088_5150_herc_gla by poking this byte with the ridge
; settled: 410 pixels stranded, the range line and ridge segments among them -
; the "stale ridges in the background" the field reported.
;
; So the shadow goes back to zero and every row is marked, which makes this
; frame one whole-viewport blit and every frame after it the pre-85.3.5 game.
; It costs 16,000 bytes of stores ONCE in a bracket, on a path that is already
; giving up 61-67 ms a frame (85.3.5), and it cannot be reached twice.
; -----------------------------------------------------------------------------
.full:
    push es
    mov byte [tk_tmpl], 0           ; back to a panel that is drawn every frame,
    mov byte [tk_rtm_on], 0         ; which is what Mode X has always done - and
    call tk_tmreset                 ; the template holds no ridge now either
    mov es, [tk_shseg]
    xor di, di
    mov cx, TK_SHSEG / 2
    xor ax, ax
    cld
    rep stosw                       ; ...the shadow with nothing stranded in it
    push ds
    pop es
    mov di, [tk_spcur]
    mov cx, [tk_vh]
    mov ax, TK_LASTB << 8           ; lo = 0, hi = TK_LASTB: every row whole, so
    rep stosw                       ; the blit carries the erase to the glass
    pop es
    ret

; -----------------------------------------------------------------------------
; tk_tmencband - the ridge's own band, re-encoded (SPEC.md 85.3.8)
; -----------------------------------------------------------------------------
tk_tmencband:
    mov ax, [tk_tmr + 48]
    mov bx, [tk_tmr + 50]
    jmp tk_tmenc

; -----------------------------------------------------------------------------
; tk_tmspans - one row's stored spans, laid over a run the clear has zeroed
; in:  ES = the shadow, BX -> tk_rowoff[r], AX = the row's first record and DI
;      one past its last, DL = the run's first byte and DH its last - both
;      inclusive and already rounded out to whole words; DF clear.
;      THE ROW MUST HAVE RECORDS: the caller compares the two indices itself,
;      so an empty row - 83 of the 200 - pays a compare and not a call, and
;      hands on what the compare already read
; out: nothing.  SI, BX and BP preserved; AX, CX, DX and DI are NOT
;
; THE RUN IS ZEROED FIRST AND THIS LAYS THE PANEL OVER IT, which is why a
; record may carry interior zeros and why a span needs clipping to the run and
; to nothing else.
; -----------------------------------------------------------------------------
tk_tmspans:
    push bx                         ; SI, BX and BP are the clear loop's whole
    push si                         ; state, and nothing else is preserved
    push bp
    mov si, ax                      ; the caller's compare already had both
    mov bp, di                      ; ends of the row's record list
    mov bx, [bx]                    ; BX = the row's base in the shadow, held
    push ds                         ; for the whole row rather than re-read per
    mov ds, [tk_tmseg]              ; span. NO cld: tk_clearspans cleared DF
.s:                                 ; before its loop and nothing in it sets DF
    lodsw                           ; AL = the span's start, AH = its length
    mov ch, al
    add ch, ah
    dec ch                          ; CH = its last byte
    cmp al, dl
    jb .clip
    cmp ch, dh
    ja .clip
    ; --- IT LANDS WHOLE, which is the common case: no clipping, and rep
    ;     movsb leaves SI on the next record for nothing ---------------------
    mov cl, ah
    xor ch, ch
    mov di, bx
    xor ah, ah
    add di, ax
    rep movsb
    cmp si, bp
    jb .s
    jmp short .end
.clip:
    cmp ch, dl                      ; wholly before the run...
    jb .past
    cmp al, dh                      ; ...or wholly after it
    ja .past
    push si
    push ax
    push bx
    mov bl, al                      ; BL = from = max(start, the run's first)
    cmp bl, dl
    jae .f
    mov bl, dl
.f:
    mov bh, ch                      ; BH = to = min(last, the run's last)
    cmp bh, dh
    jbe .t
    mov bh, dh
.t:
    mov cl, bl
    sub cl, al
    xor ch, ch
    add si, cx                      ; DS:SI = the first byte that lands
    mov cl, bh
    sub cl, bl
    inc cl
    mov al, bl
    xor ah, ah
    pop bx
    mov di, bx
    add di, ax                      ; ES:DI = where it lands in the shadow
    xor ch, ch
    rep movsb
    pop ax
    pop si
.past:
    mov cl, ah                      ; on to the next record
    xor ch, ch
    add si, cx
    cmp si, bp
    jb .s
.end:
    pop ds
    pop bp
    pop si
    pop bx
    ret
