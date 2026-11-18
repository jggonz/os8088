; =============================================================================
; brfont.inc - the Browser's tall headings, resident half (SPEC.md 71.35)
;
; <h1>, <h2> and <h3> are set TWO ROWS TALL by part 7's font body (brfont_body.inc): h1
; in the 8x8 cell doubled, h2 and h3 in the system's Times face. What stays
; here is what the layout and the paint need to ask it, and the decision.
;
; ON DEMAND, TWICE OVER. Nothing is read at launch: the first layout that
; meets one of those headings with the part absent records that it wanted it
; ([br_fwant]) and sets the line in the 8x8 cell as before; the UI task's
; next wake, with the page standing still, fetches the part, reads the face
; and lays the page out again. A page with no heading never reads either, and
; neither does a session on a machine with fonts off ([br_fon], off on an
; 8086 until the Options menu can say otherwise - 1.8 step 7).
;
; A TALL LINE is a text record with LN_X bit 7 set and one continuation row
; below it (LNF_IROW, the picture rows' own record): the paint draws the top
; slice on the text row and the bottom slice on the continuation row, each
; from the whole line composed again, so a band that repaints either row
; alone draws it right. Its kind rides in LNF_UNDER, which a tall line does
; not otherwise use: set = BRT_X2. A line that also holds a picture stays one
; row, in the cell.
;
; Its width is measured in PIXELS ([br_tpx], beside the cell count the wrap
; already keeps), so a tall line breaks where its own letters reach the edge.
; =============================================================================

; --- br_fninit - at entry: no part, fonts on (every CPU, the owner's choice
; of 2026-11-15: an XT's heading as IE3 drew it; Options > Fonts turns it off)
br_fninit:
    push ax
    xor ax, ax
    mov [br_fnseg], ax
    mov word [br_fon], 1            ; br_fon on, br_fwant 0
    mov [br_tbok], ax               ; br_tbok and br_tbw: no tables yet
    mov [br_zcur], ax               ; br_zcur and br_zdep
    mov [br_fface], al
    pop ax
    ret

; --- br_fnload - part 8 and its face, on the UI task -> CF=1 refused ---------
; A machine with no SYSTEM/FONTS (the 360KB system disk, SPEC.md 24.3.1.5)
; still takes the part: h1's doubled cell needs no file
br_fnload:
    push ax
    push dx
    push bp
    push es
    call br_p7get                   ; ES = part 7's data, DX its code
    jc .no
    push es
    mov ax, [es:BRFN_ADV]
    mov [br_fnadv], ax
    mov [br_fnadv+2], dx
    mov ax, [es:BRFN_DRAW]
    mov [br_fndraw], ax
    mov [br_fndraw+2], dx
    push dx
    push word [es:BRFN_OPEN]
    mov bp, sp
    call far [ss:bp]                ; the face (a floppy's walk to FONTS)
    mov [br_fface], al              ; 1 it opened: h2 and h3 may be tall
    add sp, 4
    pop ax                          ; the data segment
    mov [br_fnseg], ax
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop bp
    pop dx
    pop ax
    ret

; --- br_fnwake - the UI task's wake: the part a layout wanted ----------------
br_fnwake:
    cmp byte [br_cwant], 1          ; colours, fonts or tables (part 7)?
    je .wanted
    cmp byte [br_fwant], 1
    je .wanted
    cmp byte [br_tbw], 1
    jne .ret
.wanted:
    cmp byte [br_fin], 0            ; the page stands still: no worker writing
    jne .ret                        ; it, no layout under way
    test byte [br_parse_started], BR_PARSE_LAYOUT
    jnz .ret
    mov al, [br_nstate]             ; THE PAGE IS ALL IN: idle, done or
    cmp al, BN_OPEN                 ; failed. A part read from the floppy and
    jb .allin                       ; a layout with the page still arriving
    cmp al, BN_DONE                 ; hung an XT, the heap written over with
    jb .ret                         ; spaces (1.8, Fonts on)
.allin:
    push ax
    push bx
    xor bl, bl                      ; BL = something came in
    cmp byte [br_cwant], 1
    jne .fonts
    mov byte [br_cwant], 2          ; asked once a session, answered or not
    call br_cpbind
    jc .fonts
    call br_reparse
    inc bl
.fonts:
    cmp byte [br_fwant], 1
    jne .tables
    mov byte [br_fwant], 2          ; asked once a session, answered or not
    call br_fnload
    jc .tables
    inc bl
.tables:
    cmp byte [br_tbw], 1
    jne .any
    mov byte [br_tbw], 2
    call br_tbload
    jc .any
    inc bl
.any:
    or bl, bl
    jz .none
    call OSAPI_GFX_LOCK
    call br_layout                  ; the page again, its headings tall
    xor ax, ax
    call br_advance
    mov bx, [br_win]
    call OSAPI_WM_CLIP_SET
    jc .unlock
    call br_paint_band
    call br_sbar
.unlock:
    call OSAPI_GFX_UNLOCK
.none:
    call br_promise                 ; the part is answered: still again?
    pop bx
    pop ax
.ret:
    ret

; --- br_twant - part 8 is wanted: CF=0 it is in, CF=1 not (yet). All kept ---
br_twant:
    cmp byte [br_fon], 0
    je .no
    cmp word [br_fnseg], 0
    jne .ret                        ; (CF=0 from the compare: not below)
    cmp byte [br_fwant], 0
    jne .no
    mov byte [br_fwant], 1          ; the UI task's next wake fetches it
.no:
    stc
.ret:
    ret

; --- br_tset - the layout's heading rank changed: is its line tall? ---------
br_tset:
    push ax
    mov byte [br_tall], 0
    mov al, [br_lhead]
    or al, al
    jz .out
    call br_twant
    jc .out
    cmp al, 1
    je .x2
    cmp byte [br_fface], 0
    je .out                         ; no Times on this machine: the cell
    mov byte [br_tall], BRT_FACE
    jmp short .out
.x2:
    mov byte [br_tall], BRT_X2
.out:
    pop ax
    ret

; --- br_szrec - the layout met a D_COL at ES:SI: a size record? All kept ---
; 34 sets the text that follows in the cell, 35 in the face (71.35)
br_szrec:
    push ax
    call br_szval                   ; AL = 0 / 1, CF=1 not a size
    jc .out
    mov [br_zl], al
    cmp byte [es:si+2], D_IMGDIG + 5
    jne .out                        ; the record asked for the face (35):
    call br_twant                   ; part 8, if it is not in
.out:
    pop ax
    ret

; br_szval - ES:SI at a D_COL -> CF=0 AL = 1 big, 0 not; CF=1 a colour.
; With Fonts off, or part 8 not in yet, every size is the cell's: nothing
; could draw a big letter (br_szrec has asked for the part already)
br_szval:
    mov al, [es:si+1]
    cmp al, D_IMGDIG + 5            ; 34 and 35 are digits 5,4 and 5,5
    jne .no
    mov al, [es:si+2]
    sub al, D_IMGDIG + 4
    cmp al, 2
    jae .no
    cmp byte [br_fon], 0
    je .cell
    cmp word [br_fnseg], 0          ; ...AND UNTIL PART 8 IS IN: a big letter
    jne .yes                        ; is drawn by its code, and a line laid
.cell:                              ; tall without it far-called nothing
    xor al, al                      ; (CF=0 from xor)
    ret
.yes:
    clc
    ret
.no:
    stc
    ret

; --- br_tzero - the pending line is empty: no pixels, its start's size -----
br_tzero:
    mov word [br_tpx], 0
    push ax
    mov al, [br_zl]
    mov [br_zstart], al
    mov byte [br_lbig], 0
    pop ax
    ret

; --- br_tcount - the wrap counted AL: its pixels too. All kept --------------
br_tcount:
    cmp word [br_fnseg], 0
    je .ret
    push ax
    push ax
    cmp byte [br_tall], 0
    jne .base                       ; a heading's kind is every letter's
    cmp byte [br_zl], 0
    je .base
    cmp byte [br_fface], 0
    je .base
    mov byte [br_lbig], 1           ; a big letter: the line goes tall
    jmp short .face
.base:
    mov al, BRT_X2ADV
    cmp byte [br_tall], BRT_X2
    je .add
    mov al, 7
    cmp byte [br_tall], BRT_FACE
    jne .add
.face:
    pop ax
    push ax
    call far [br_fnadv]             ; AL = its advance in the face
.add:
    xor ah, ah
    add [br_tpx], ax
    pop ax
    cmp al, ' '
    jne .out
    mov ax, [br_tpx]                ; a breakpoint: the width up to here
    mov [br_tbpx], ax
.out:
    pop ax
.ret:
    ret

; br_tis - ZF=0 the pending line is tall-shaped (a tall heading or a big
; letter on it). All kept
br_tis:
    push ax
    mov al, [br_tall]
    or al, [br_lbig]
    pop ax
    ret

; --- br_tover - CF=1 the tall line is wider than the band. All kept ---------
br_tover:
    call br_tis
    jz .fit
    push ax
    mov ax, [br_effcols]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    cmp ax, [br_tpx]                ; CF = band < width
    pop ax
    ret
.fit:
    clc
    ret

; --- br_tswap / br_tafter - around a break at a space: the line emitted is
; the width up to the space, and what follows carries the rest
br_tswap:
    push ax
    mov ax, [br_tpx]
    mov [br_tsave], ax
    mov ax, [br_tbpx]
    mov [br_tpx], ax
    pop ax
    ret
br_tafter:
    push ax
    mov ax, [br_tsave]
    sub ax, [br_tbpx]
    mov [br_tpx], ax
    mov al, [br_zl]                 ; the rest is in the size now open
    mov [br_lbig], al
    mov [br_zstart], al
    pop ax
    ret

; --- br_tlemit - br_emitline's line, AX = its picture rows: tall? -----------
; out: CX = its cells when it is
br_tlemit:
    mov byte [br_ltall], 0
    call br_tis
    jz .ret
    or ax, ax
    jnz .ret                        ; a picture on it: one row, in the cell
    cmp byte [br_lpre], 0
    jne .ret                        ; PRE keeps its columns
    mov byte [br_ltall], 1
    mov byte [br_lreach+1], 1       ; the continuation row below
    mov cx, [br_tpx]
    add cx, 7
    shr cx, 1
    shr cx, 1
    shr cx, 1
    jnz .ret
    inc cx
.ret:
    ret

; --- br_tflag - the record being written: mark it tall ----------------------
; A tall line's LN_FL is ours: bit 32 the face is its base, 128 the doubled
; cell, neither the 8x8 cell; 64 its first letter is big
br_tflag:
    cmp byte [br_ltall], 0
    je .ret
    push ax
    or byte [br_lrecbuf+LN_X], 80h
    mov al, [br_lrecbuf+LN_FL]
    and al, 0FFh - LNF_BOLD - LNF_ITALIC - LNF_UNDER
    cmp byte [br_tall], BRT_FACE
    jne .nf
    or al, LNF_BOLD
.nf:
    cmp byte [br_tall], BRT_X2
    jne .nx
    or al, LNF_UNDER
.nx:
    cmp byte [br_zstart], 0
    je .nb
    or al, LNF_ITALIC
.nb:
    mov [br_lrecbuf+LN_FL], al
    pop ax
.ret:
    ret

; --- br_tread - br_build's LN_X: AL = the column, [br_ltk] = 1 + the base
; kind on a tall line (0 not tall), [br_bz] = its first letter is big
br_tread:
    mov al, [br_lrecbuf+LN_X]
    mov byte [br_ltk], 0
    mov byte [br_bz], 0
    test al, 80h
    jz .ret
    and al, 7Fh
    push ax
    mov ah, [br_lrecbuf+LN_FL]
    mov al, 1 + BRT_CELL
    test ah, LNF_BOLD
    jz .f
    mov al, 1 + BRT_FACE
.f:
    test ah, LNF_UNDER
    jz .x
    mov al, 1 + BRT_X2
.x:
    mov [br_ltk], al
    test ah, LNF_ITALIC
    jz .b
    mov byte [br_bz], 1
.b:
    pop ax
.ret:
    ret

; --- br_bzrec / br_bzmark - br_build's size state, and a cell's mark -------
br_bzrec:                           ; ES:SI just past a D_COL. All kept
    push ax
    dec si
    call br_szval
    inc si
    jc .out
    mov [br_bz], al
.out:
    pop ax
    ret
br_bzmark:                          ; DI = the cell just written. All kept
    call br_bgrey                   ; a table's line in IE3's grey (1.8)
    cmp byte [br_ltk], 0
    je .ret
    cmp byte [br_snap], 0           ; a table cell's first letter (71.35)
    je .nosnap
    mov byte [br_snap], 0
    or byte [di + (br_lmark - br_lbuf)], LM_SNAP
.nosnap:
    cmp byte [br_bz], 0
    je .ret
    or byte [di + (br_lmark - br_lbuf)], LM_BIG
.ret:
    ret

; --- br_tpaint - a tall line's slice: CX/DX = the row's pen -----------------
br_tpaint:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov al, [br_lrowk]
    mov [br_tsl], al
    or al, al
    jz .have
    push cx                         ; the bottom slice: the text row's cells
    push dx
    mov bl, al
    xor bh, bh
    mov ax, [br_tidx]
    sub ax, bx
    call br_build
    pop dx
    pop cx
.have:
    mov [br_fpar + BRFP_X], cx
    mov [br_fpar + BRFP_Y], dx
    mov ax, [br_cols]
    mov cx, ax
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov [br_fpar + BRFP_W], ax
    call br_stpen
    mov [br_fpar + BRFP_PEN], ax
    mov al, [br_lcol]
    xor ah, ah
    sub cx, ax                      ; CX = the cells from the line's column
    mov si, ax
    add si, br_lbuf
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov [br_fpar + BRFP_X0], ax
    mov al, [br_ltk]
    dec al                          ; the base kind
    mov ah, [br_tsl]
    mov [br_fpar + BRFP_KIND], ax   ; KIND and SLICE
    mov word [br_fpar + BRFP_MOFF], br_lmark - br_lbuf
    mov word [br_fpar + BRFP_AOFF], br_lattr - br_lbuf
    mov al, [br_lcolr]
    mov [br_fpar + BRFP_COLR], al
    mov al, [br_ulmask]
    mov [br_fpar + BRFP_ULM], al
.trim:
    jcxz .draw
    mov bx, si
    add bx, cx
    cmp byte [bx-1], ' '
    jne .draw
    dec cx
    jmp .trim
.draw:
    push ds
    pop es
    mov di, br_fpar
    call far [br_fndraw]
    mov ax, (CBLACK << 8) | CWHITE  ; the lock's default pen back
    call OSAPI_GFX_BLIT1_PEN
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- br_thit - br_hitofs's cell on a tall line: AX = the column, BX = the
; line -> the same, rebased to the TEXT row and to the character under the
; click by the letters' own advances. Any other line is left as it was.
br_thit:
    push cx
    push dx
    push si
    push di
    mov di, ax                      ; DI = the column, banked
    mov si, bx                      ; SI = the line, banked
    mov ax, bx
    call br_lread
    test byte [br_lrecbuf+LN_FL], LNF_IROW
    jz .text
    mov al, [br_lrecbuf+LN_X]       ; a continuation row: its text row is
    cbw                             ; LN_X rows away
    add ax, si
    mov bx, ax
    call br_lread
    jc .keep
.text:
    test byte [br_lrecbuf+LN_X], 80h
    jz .keep                        ; not tall: the cell is the cell
    push bx
    mov ax, bx
    call br_build                   ; br_lbuf = its characters, [br_lcol]
    pop bx
    mov al, [br_lcol]
    xor ah, ah
    mov cx, di
    sub cx, ax
    jb .done                        ; left of its text: the column stands
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, 4                       ; CX = the click, pixels into the text
    mov si, ax
    add si, br_lbuf                 ; SI = its first character
    xor dx, dx                      ; DX = pixels so far
.ch:
    test byte [si + (br_lmark - br_lbuf)], LM_SNAP
    jz .nosnap                      ; a table cell's first letter: its column
    mov dx, si
    sub dx, br_lbuf
    sub dl, [br_lcol]
    sbb dh, 0
    shl dx, 1
    shl dx, 1
    shl dx, 1
.nosnap:
    mov al, [si]
    cmp al, BR_BH
    jb .notline
    cmp al, BR_BX
    jbe .base                       ; a grid line is a cell wide
.notline:
    cmp byte [br_ltk], 1 + BRT_CELL
    jne .base                       ; a heading's kind is every letter's
    test byte [si + (br_lmark - br_lbuf)], LM_BIG
    jnz .face
.base:
    mov ah, BRT_X2ADV
    cmp byte [br_ltk], 1 + BRT_X2
    je .adv
    mov ah, 8
    cmp byte [br_ltk], 1 + BRT_FACE
    jne .adv
.face:
    call far [br_fnadv]
    mov ah, al
.adv:
    mov al, ah
    xor ah, ah
    add dx, ax
    cmp dx, cx
    ja .hit                         ; the click is inside this one
    inc si
    mov ax, si
    sub ax, br_lbuf
    cmp ax, [br_cols]
    jb .ch
.hit:
    mov di, si
    sub di, br_lbuf                 ; DI = its column in the built line
.done:
    mov ax, di
    jmp short .out
.keep:
    mov ax, di
    mov bx, si
.out:
    call br_ldocback
    pop di
    pop si
    pop dx
    pop cx
    ret

; =============================================================================
; THE PARSER'S SIZES: <big>, <small> and <font size>. One stack for the
; three, since they nest as tags; the record is D_COL 34 (the cell) or 35
; (the face), which every colour reader refuses (br_coldec's range).
; =============================================================================
br_fsize:                           ; <font size>. All kept
    push ax
    push cx
    push si
    push di
    cmp byte [br_close], 0
    jne .close
    call br_zpush
    mov di, br_a_size
    call br_getattr                 ; SI..CX = its value
    jnc .sized
    mov di, br_a_face               ; no size: a face asked for is the face
    call br_getattr
    jc .out
    mov al, 2
    jmp short .set
.sized:
    cmp si, cx
    jae .out
    mov al, [es:si]
    cmp al, '-'
    je .small                       ; any smaller size: the cell
    cmp al, '+'
    je .normal                      ; any larger: the face
    cmp al, '3'
    jb .small                       ; size 1 and 2: the cell, 3 and up the face
.normal:
    mov al, 2
    jmp short .set
.small:
    mov al, 1
.set:
    call br_zset
    jmp short .out
.close:
    call br_zpop
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

br_fbig:                            ; <big> the face, <small> the cell
    mov ah, 2
    jmp short br_fbs
br_fsmall:
    mov ah, 1
br_fbs:
    cmp byte [br_close], 0
    jne br_zpop
    call br_zpush
    mov al, ah
    jmp short br_zset

br_zpush:                           ; the size open now, kept for the close
    push bx
    mov bl, [br_zdep]
    cmp bl, 8
    jae .deep
    xor bh, bh
    push ax
    mov al, [br_zcur]
    mov [br_zstk + bx], al
    pop ax
.deep:
    inc byte [br_zdep]
    pop bx
    ret

br_zpop:                            ; ...and back to it
    push ax
    push bx
    mov bl, [br_zdep]
    or bl, bl
    jz .out
    dec bl
    mov [br_zdep], bl
    cmp bl, 8
    jae .out
    xor bh, bh
    mov al, [br_zstk + bx]
    call br_zset
.out:
    pop bx
    pop ax
    ret

br_zset:                            ; AL = 0 / 1: the record, if it changed
    cmp al, [br_zcur]
    je .ret
    mov [br_zcur], al
    push ax
    push ax
    mov al, D_COL
    call br_put
    pop ax
    cmp al, 2                       ; 2 -> 35 the face; 0 and 1 -> 34 the cell
    mov al, 35
    je .rec
    dec al
.rec:
    call br_colenc
    call br_put
    mov al, ah
    call br_put
    pop ax
.ret:
    ret

br_a_face:  db 'face', 0

; br_bsnap - br_build at a D_CMETA, ES:SI at its payload: BR_SNAP marks the
; next cell as a table cell's first. All kept
br_bsnap:
    cmp byte [es:si], BR_SNAP
    jne .ret
    mov byte [br_snap], 1
.ret:
    ret
