; =============================================================================
; os8088 - apps/browser/brcolor.inc
;
; Browser 1.8 step 2: a page's colours (SPEC.md 71.34). <body bgcolor text
; link>, <font color>, and table, row and cell bgcolor, drawn on VGA and EGA.
; A 1bpp adapter never reaches any of it: the parser writes no record there
; and br_build never colours a cell, so Hercules and CGA draw what 1.7 drew.
;
; THE RECORD is three bytes, D_COL and two base-6 digits (D_IMGDIG + 0..5),
; the value v = high * 6 + low:
;
;     0          the foreground back to the page's text colour
;     1..16      the foreground is colour v - 1
;     17         the ground back to the page's
;     18..33     the ground is colour v - 18 (a table cell's, composed rows
;                only)
;
; Every walker that does not draw already skips a byte below 0x20 and the
; digits 26..31, so the record costs them nothing; the layout, br_build and
; the composer read it. The layout keeps a CHECKPOINT of every foreground
; change it walks (br_cprec), so a line that starts inside a <font> knows
; its colour without a backward scan, and the composer re-opens a cell's
; colours at the head of each fragment and closes them at its tail - the
; link's own discipline (br_tlnk) - so a composed row is complete in itself.
;
; Section discipline: .text in, .text out.
; =============================================================================

COL_FGDEF   equ 0
COL_BGDEF   equ 17

; --- br_colenc - AL = a value -> AL = the high digit byte, AH = the low ------
br_colenc:
    push bx
    xor ah, ah
    mov bl, 6
    div bl                          ; AL = v / 6, AH = v % 6
    add ax, (D_IMGDIG << 8) | D_IMGDIG
    pop bx
    ret

; --- br_coldec - ES:SI -> the two digit bytes -> AL = the value, CF=1 bad -----
; AX clobbered, everything else kept
br_coldec:
    push bx
    mov bl, [es:si+1]
    sub bl, D_IMGDIG
    cmp bl, 6
    jae .bad
    mov al, [es:si]
    sub al, D_IMGDIG
    cmp al, 6
    jae .bad
    mov ah, 6
    mul ah
    add al, bl
    cmp al, COL_BGDEF + 17
    jae .bad
    pop bx
    clc
    ret
.bad:
    pop bx
    stc
    ret

; --- br_colput - the parser's record: AL = the value. Nothing on a 1bpp
; machine. A colour needs part 7 to be matched (br_colattr) and reaches here
; only then; a size (34, 35) and a "back to the page's" (0, 17) need nothing,
; so they are written while part 7 is not in yet (1.8 cleanup 5)
br_colput:
    test word [br_opts], OPT_COLOR  ; Options > Colors off (71.36)
    jz .out
    cmp byte [br_bpp], 1
    je .out
    push ax
    push ax
    mov al, D_COL
    call br_put
    pop ax
    call br_colenc
    call br_put
    mov al, ah
    call br_put
    pop ax
.out:
    ret

; --- br_colattr - attribute DI's colour -> CF=0, AL = 0..15 ------------------
; The value is matched in part 7 (brcolor_body.inc): a name of the sixteen
; HTML 3.2 colours and a few the period used, or six hex digits with or
; without '#', to the nearest of the sixteen (apps/os88rgb.inc). No part, no
; colour. ES = the source, as br_getattr wants it. AX clobbered
br_colattr:
    test word [br_opts], OPT_COLOR
    jz .none
    cmp word [br_cpseg], 0
    je .want
    push cx
    push si
    call br_getattr                 ; SI/CX = the value's span
    jc .out
    cmp si, cx
    jae .no
    call far [br_cpvec]
    jmp short .out
.no:
    stc
.out:
    pop si
    pop cx
    ret
.none:
    stc
    ret

; --- br_colattr's absent part: attribute DI present -> part 7 is wanted ----
br_colattr.want:
    cmp byte [br_cwant], 0
    jne .no
    push cx
    push si
    call br_getattr
    pop si
    pop cx
    jc .no
    mov byte [br_cwant], 1          ; the UI task fetches it after the page
.no:
    stc
    ret

; --- br_cpbind - part 7 in and its colour match bound -> CF=1 refused --------
br_cpbind:
    push ax
    push cx
    push dx
    push es
    call br_p7get                   ; ES = part 7's data, DX its code
    jc .out
    mov ax, es
    mov cx, [es:BRCL_MATCH]
    mov [br_cpvec], cx
    mov [br_cpvec+2], dx
    mov [br_cpseg], ax
.out:
    pop es
    pop dx
    pop cx
    pop ax
    ret

; --- br_reparse - the finished page parsed again from its kept source, on the
; UI task, now that its colours can be read. Not a frameset (its document is
; its frames') nor plain text. All kept
br_reparse:
    cmp byte [br_framecount], 0
    jne .ret
    cmp byte [br_nonhtml], 0
    jne .ret
    push ax
    mov al, [br_trunc]              ; the source's own cut stands
    push ax
    mov byte [br_parse_started], 0
    mov byte [br_parse_final], 1
    call br_parse
    pop ax
    or [br_trunc], al               ; (br_cols stands: br_layout lays it out
    pop ax                          ; at the width it has)
.ret:
    ret

; --- br_cpload - part 7, on the UI task at the window's start: a colour
; display only, for the THEME's link colour (a read of SYSTEM.CFG). The part
; is then dropped: a page that names a colour asks for it again (br_colattr,
; br_cwant) and is parsed again once it is in (br_fnwake). Part 7 is ~12 KB,
; and a page with no colour, face or table holds none of it (1.8 cleanup 5).
; Refused, the page is black on white and nothing else changes
br_cpload:
    mov word [br_cpseg], 0
    mov byte [br_cwant], 2          ; no part to ask for...
    mov byte [br_tlink], 0FFh       ; no part: links in the text's colour
    cmp byte [br_bpp], 1
    je .ret
    push ax
    push cx
    push dx
    push si
    push di
    push es
    call br_p7get                   ; ES = part 7's data, DX its code
    jc .no
    mov ax, es
    mov cx, [es:BRCL_MATCH]
    mov [br_cpvec], cx
    mov [br_cpvec+2], dx
    mov [br_cpseg], ax
    push bp                         ; THE THEME'S LINK COLOUR (71.34): the
    push dx                         ; focused title bar's ground
    push word [es:BRCL_THEME]
    mov bp, sp
    call far [ss:bp]
    add sp, 4
    pop bp
    mov [br_tlink], al
    mov word [br_cpseg], 0          ; nothing holds a vector into it yet
    mov byte [br_cwant], 0          ; ...until a page names a colour
    mov al, 7
    call op_drop
.no:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
.ret:
    ret

; --- br_ccontr - AL = a ground -> AL = a legible ink on it ---------------------
br_ccontr:
    cmp al, CLGRAY
    je .light
    cmp al, CLBLUE
    jae .light
    mov al, CWHITE
    ret
.light:
    mov al, CBLACK
    ret

; --- br_cdefault - the page's colours back to black on white ----------------
br_cdefault:
    mov word [br_ppen], (CWHITE << 8) | CBLACK
    mov byte [br_pattr], (CWHITE << 4) | CBLACK
    push ax                         ; links in the theme's accent until the
    mov al, [br_tlink]              ; page says (IE3's blue, the system's own)
    mov [br_plink], al
    push cx                         ; ...and no link asked "visited?" yet
    push di
    push es
    push ds
    pop es
    mov di, br_lvc
    mov cx, BR_LNKMAX / 4
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    mov byte [br_fink], 0
    mov byte [br_fdep], 0
    mov word [br_zcur], 0           ; ...and no <big>/<font size> (71.35)
    mov word [br_tbn], 0            ; br_tbn and br_tbdep: no table yet
    mov byte [br_flt], 0            ; ...nor a float
    mov word [br_tbits], 0          ; ...nor its border
    mov word [br_tbits + 2], 0
    mov word [br_tbits + 4], 0
    mov word [br_tbits + 6], 0
    mov byte [br_tabd], 0
    mov byte [br_trbg], 0
    ret

; --- br_body - <body bgcolor text link background> ---------------------------
; A page with a BACKGROUND picture and no bgcolor keeps white and drops its
; text and link colours: they were chosen against a picture this browser
; does not tile, and white text on white is the page gone.
br_body:
    cmp byte [br_close], 0
    jne .ret
    cmp byte [br_bpp], 1
    je .ret
    cmp byte [br_frameappend], 0    ; A FRAME'S <body> (71.34.5): its colours
    je .page                        ; are its cell's, as IE3 painted a frame,
    jmp br_fbody                    ; never the whole page's
.page:
    push ax
    push bx
    push cx
    push si
    push di
    mov bx, [br_ppen]               ; BL = ink, BH = ground
    mov di, br_a_bgcolor
    call br_colattr
    jc .nobg
    mov bh, al
    jmp short .txt
.nobg:
    mov si, br_a_background
    call br_attrflag
    jnc .set                        ; a picture and no colour: black on white
.txt:
    mov di, br_a_text
    call br_colattr
    jc .lnk
    mov bl, al
.lnk:
    mov di, br_a_link
    call br_colattr
    jc .set
    mov [br_plink], al
.set:
    cmp bl, bh
    jne .c1
    mov al, bh
    call br_ccontr
    mov bl, al
.c1:
    cmp [br_plink], bh
    jne .c2
    mov byte [br_plink], 0FFh
.c2:
    mov [br_ppen], bx
    mov al, bh
    mov cl, 4
    shl al, cl
    or al, bl
    mov [br_pattr], al
    pop di
    pop si
    pop cx
    pop bx
    pop ax
.ret:
    ret

; --- br_font - <font color> and </font>: a stack of 8 foregrounds -----------
br_font:
    cmp byte [br_bpp], 1
    je .ret
    push ax
    push bx
    push di
    xor bh, bh
    cmp byte [br_close], 0
    jne .close
    mov bl, [br_fdep]
    cmp bl, 0FFh
    je .out
    cmp bl, 8
    jae .deep
    mov al, [br_fink]
    mov [br_fstk+bx], al
.deep:
    inc byte [br_fdep]
    mov di, br_a_color
    call br_colattr
    jc .out
    inc al
    jmp short .emit
.close:
    mov bl, [br_fdep]
    or bl, bl
    jz .out
    dec bl
    mov [br_fdep], bl
    cmp bl, 8
    jae .out
    mov al, [br_fstk+bx]
.emit:
    cmp al, [br_fink]
    je .out
    mov [br_fink], al
    call br_colput
.out:
    pop di
    pop bx
    pop ax
.ret:
    ret

; --- br_tcolhook - <table> and <tr> bgcolor, and the font state a table
; leaves as it found. AL = the tag's action, kept; everything kept
br_tcolhook:
    cmp byte [br_bpp], 1
    je .ret
    cmp al, TA_TAB
    je .tab
    cmp al, TA_ROW
    jne .ret
    cmp byte [br_close], 0
    jne .ret
    push ax
    push di
    mov di, br_a_bgcolor
    call br_colattr
    mov ah, 0
    jc .r1
    inc al
    mov ah, al
.r1:
    mov [br_trbg], ah
    pop di
    pop ax
.ret:
    ret
.tab:
    push ax
    push bx
    push di
    mov byte [br_trbg], 0
    mov bl, [br_tabd]
    xor bh, bh
    cmp byte [br_close], 0
    jne .tclose
    or bl, bl
    jnz .tn
    mov al, [br_fdep]               ; the outermost table: the font state
    mov [br_tfsave], al             ; as it opened
    mov al, [br_fink]
    mov [br_tfcur], al
.tn:
    cmp bl, 4
    jae .tdeep
    mov di, br_a_bgcolor
    call br_colattr
    mov ah, 0
    jnc .t0
    or bl, bl                       ; none: a nested table shows its outer
    jz .t1                          ; table's ground
    mov ah, [br_tabbgs+bx-1]
    jmp short .t1
.t0:
    inc al
    mov ah, al
.t1:
    mov [br_tabbgs+bx], ah
.tdeep:
    cmp bl, 0FFh
    je .tout
    inc byte [br_tabd]
    jmp short .tout
.tclose:
    push ax                         ; the ground back at a table's end, so a
    mov al, COL_BGDEF               ; nested (inlined) cell's never runs on
    call br_colput                  ; into its container's text (71.34.5)
    pop ax
    or bl, bl
    jz .tout
    dec bl
    mov [br_tabd], bl
    jnz .tout
    mov al, [br_tfsave]             ; a <font> left open in a cell ends with
    mov [br_fdep], al               ; the table, and the text after it is in
    mov al, [br_tfcur]              ; the colour it was before - said in the
    cmp al, [br_fink]               ; stream too, for a table the layout
    je .tout                        ; walks inline (BROWSER-PLAN 3.2.1)
    mov [br_fink], al
    call br_colput
.tout:
    pop di
    pop bx
    pop ax
    ret

; --- br_cellbg - a <td>/<th>'s ground: its own, its row's or its table's ----
br_cellbg:
    cmp byte [br_bpp], 1
    je .ret
    push ax
    push bx
    push di
    mov di, br_a_bgcolor
    call br_colattr
    jc .inh
    inc al
    jmp short .emit
.inh:
    mov al, [br_trbg]
    or al, al
    jnz .emit
    mov bl, [br_tabd]
    or bl, bl
    jz .out
    dec bl
    cmp bl, 4
    jae .out
    xor bh, bh
    mov al, [br_tabbgs+bx]
    or al, al
    jnz .emit
    or bl, bl                       ; A NESTED table's plain cell (71.34.5):
    jnz .said                       ; the default ground, said - it is inlined
    cmp byte [br_frameappend], 0    ; (a frame document's tables are all
    je .out                         ; inside its frame's cell)
.said:
    xor al, al                      ; in its container's cell, where a sibling's
.emit:                              ; would run on into it
    add al, COL_BGDEF               ; c + 1 + 17 = 18 + c; 0 the default
    call br_colput
.out:
    pop di
    pop bx
    pop ax
.ret:
    ret

; --- br_cpreset - a layout starts: no checkpoint yet --------------------------
br_cpreset:
    mov byte [br_cplast], 0
    cmp word [br_cpseg], 0
    je .ret
    push es
    mov es, [br_cpseg]
    mov word [es:BRCL_CPN], 0       ; BRCL_CPN and BRCL_CPOVER
    pop es
.ret:
    ret

; --- br_cprec - the layout met a record: DX:AX its offset, CL its value -----
; A foreground change is kept as a checkpoint in part 7; past BR_CPMAX the
; rest of the page starts its lines in the page's colour (br_cplook)
br_cprec:
    cmp cl, COL_BGDEF
    jae .ret
    cmp cl, [br_cplast]
    je .ret
    mov [br_cplast], cl
    cmp word [br_cpseg], 0
    je .ret
    push bx
    push es
    mov es, [br_cpseg]
    cmp byte [es:BRCL_CPOVER], 0
    jne .out
    mov bl, [es:BRCL_CPN]
    cmp bl, BR_CPMAX
    jb .room
    mov byte [es:BRCL_CPOVER], 1
    mov [es:BRCL_CPOFF], ax
    mov [es:BRCL_CPOFF+2], dx
    jmp short .out
.room:
    inc byte [es:BRCL_CPN]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov [es:BRCL_CPTAB+bx], ax
    mov [es:BRCL_CPTAB+bx+2], dl
    mov [es:BRCL_CPTAB+bx+3], cl
.out:
    pop es
    pop bx
.ret:
    ret

; --- br_cplook - DX:AX = a line's start -> AL = the foreground there --------
br_cplook:
    cmp word [br_cpseg], 0
    jne .go
    xor al, al
    ret
.go:
    push bx
    push cx
    push si
    push es
    mov es, [br_cpseg]
    cmp byte [es:BRCL_CPOVER], 0
    je .scan
    cmp dx, [es:BRCL_CPOFF+2]
    ja .none
    jb .scan
    cmp ax, [es:BRCL_CPOFF]
    jae .none
.scan:
    mov cl, [es:BRCL_CPN]
    xor ch, ch
    mov si, cx
    shl si, 1
    shl si, 1
    add si, BRCL_CPTAB
.l:
    jcxz .none
    sub si, 4
    dec cx
    mov bl, [es:si+2]
    xor bh, bh
    cmp bx, dx
    ja .l
    jb .hit
    cmp [es:si], ax
    jae .l
.hit:
    mov al, [es:si+3]
    jmp short .out
.none:
    xor al, al
.out:
    pop es
    pop si
    pop cx
    pop bx
    ret

; --- br_bcstart - br_build's colour state at a line's start ------------------
br_bcstart:
    cmp byte [br_bpp], 1
    je .ret
    push ax
    push cx
    push dx
    push di
    push es
    mov byte [br_cuse], 1
    mov word [br_bfg], 0            ; br_bfg and br_bbg
    push ds
    pop es
    mov di, br_lattr
    mov cx, BR_MAXCOL
    mov al, [br_pattr]
    cld
    rep stosb
    mov ax, [br_lbase]
    mov dx, [br_lbase_hi]
    mov byte [br_bcomp], 1          ; past the document: a composed row,
    cmp dx, [br_doclen_hi]          ; complete in itself
    ja .done
    jb .main
    cmp ax, [br_doclen]
    ja .done
.main:
    mov byte [br_bcomp], 0
    call br_cplook
    mov [br_bfg], al
.done:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
.ret:
    ret

; --- br_bcrec - br_build met a record: ES:SI its digits, CX the line's end ---
br_bcrec:
    push ax
    mov ax, si
    add ax, 2
    cmp ax, cx
    ja .end
    call br_coldec
    jc .skip
    cmp byte [br_cuse], 0
    je .skip
    cmp al, COL_BGDEF
    jae .bg
    mov [br_bfg], al
    jmp short .skip
.bg:
    cmp byte [br_bcomp], 0          ; a ground is a cell's: composed rows only
    je .skip
    sub al, COL_BGDEF
    mov [br_bbg], al
.skip:
    add si, 2
    pop ax
    ret
.end:
    mov si, cx
    pop ax
    ret

; --- br_battr - the cell at DI takes its colours; AL = its link mark --------
br_battr:
    cmp byte [br_cuse], 0
    je .ret
    push ax
    push bx
    mov bl, al
    mov ah, [br_bbg]
    or ah, ah
    jz .pbg
    dec ah
    jmp short .fg
.pbg:
    mov ah, [br_ppen+1]
.fg:
    mov al, [br_bfg]
    or al, al
    jz .pfg
    dec al
    jmp short .chk
.pfg:
    mov al, [br_ppen]
    test bl, LM_LINK
    jz .chk
    cmp byte [br_bvis], 0
    jne .chk                        ; followed: the text's colour (IE3's way)
    cmp byte [br_plink], 0FFh
    je .chk
    mov al, [br_plink]
.chk:
    cmp al, ah
    jne .mk
    mov al, ah
    call br_ccontr
.mk:
    shl ah, 1
    shl ah, 1
    shl ah, 1
    shl ah, 1
    or al, ah
    cmp al, [br_pattr]
    je .st
    mov byte [br_lcolr], 1
.st:
    mov [di + (br_lattr - br_lbuf)], al
    pop bx
    pop ax
.ret:
    ret

; --- br_stpen - the pen of a styled row: its first lettered cell's ----------
br_stpen:
    mov ax, [br_ppen]
    cmp byte [br_lcolr], 0
    je .ret
    push bx
    xor bx, bx
.l:
    cmp bx, [br_cols]
    jae .d
    cmp byte [br_lbuf+bx], ' '
    jne .f
    inc bx
    jmp .l
.f:
    mov al, [br_lattr+bx]
    call br_apen
.d:
    pop bx
.ret:
    ret

; --- br_apen - AL = an attribute -> AX = its pen (AL ink, AH ground) ---------
br_apen:
    mov ah, al
    and al, 0Fh
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    ret

; --- br_ulpen - AL = the underline's colour ----------------------------------
br_ulpen:
    mov al, [br_plink]
    cmp al, 0FFh
    jne .ret
    mov al, [br_ppen]
.ret:
    ret

; --- br_margin - the band's right margin and its last part-row, in the
; page's ground. br_paint sets [br_mcol] to 0FFh (the window is OWNBG, so
; nothing is under them); a band-only repaint whose last fill was this
; colour pays nothing
br_margin:
    push ax
    push bx
    push cx
    push dx
    mov al, [br_ppen+1]
    cmp al, CWHITE
    jne .go
    cmp [br_mcol], al
    je .out
.go:
    mov [br_mcol], al
    call OSAPI_SET_COLOR
    mov ax, [br_cols]
    mov cl, 3
    shl ax, cl
    add ax, [br_cx]                 ; AX = x1, just past the band
    mov cx, [br_sbx]
    dec cx                          ; CX = x2, just before the scroll bar
    mov bx, [br_cy]
    mov dx, bx
    add dx, [br_ch]
    dec dx
    cmp cx, ax
    jl .bottom
    call OSAPI_GFX_FILL
.bottom:
    mov bx, [br_rows]
    mov cl, 3
    shl bx, cl
    add bx, [br_cy]                 ; BX = y1, under the last whole row
    cmp bx, dx
    jg .out
    mov ax, [br_cols]
    shl ax, cl
    add ax, [br_cx]
    mov cx, ax
    dec cx
    mov ax, [br_cx]
    call OSAPI_GFX_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_a_bgcolor:    db 'bgcolor', 0
br_a_background: db 'background', 0
br_a_text:       db 'text', 0
br_a_link:       db 'link', 0
br_a_color:      db 'color', 0

; --- the composer's half ------------------------------------------------------

; --- br_bvisit - br_build met a D_LNK1, ES:SI at its three nibbles ----------
br_bvisit:
    mov byte [br_bvis], 0
    cmp word [br_cpseg], 0
    je .ret                         ; no colours: no question
    push ax
    push bx
    push cx
    mov al, [es:si + 2]
    sub al, 16
    mov ah, al                      ; the high nibble
    mov al, [es:si + 1]
    sub al, 16
    mov cl, 4
    shl al, cl
    mov bl, [es:si]
    sub bl, 16
    or al, bl                       ; AX = the link's index
    call br_bvidx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; --- br_bvline - br_build's line BEGINS inside a link: which, and followed?
br_bvline:
    mov byte [br_bvis], 0
    cmp word [br_cpseg], 0
    je .ret
    push ax
    push dx
    mov ax, [br_lrecbuf + LN_OFS]
    mov dx, [br_lrecbuf + LN_OFS + 2]
    call br_linkat                  ; AX = the link the line starts in
    jc .out
    push bx
    push cx
    call br_bvidx
    pop cx
    pop bx
.out:
    pop dx
    pop ax
.ret:
    ret

; --- br_bvidx - AX = a link: [br_bvis] = 1 when it was followed. Clobbers
; AX, BX, CX
br_bvidx:
    cmp ax, BR_LNKMAX
    jae .ret
    mov bx, ax
    mov cl, 2
    shr bx, cl                      ; BX = its byte in the cache
    mov cl, al
    and cl, 3
    shl cl, 1                       ; CL = its two bits' place
    mov ch, [br_lvc + bx]
    shr ch, cl
    and ch, 3
    jnz .have
    call br_lvcomp                  ; CF = visited
    mov ch, 1
    adc ch, 0                       ; 1 no, 2 yes
    push cx
    shl ch, cl
    or [br_lvc + bx], ch
    pop cx
.have:
    cmp ch, 2
    jne .ret
    mov byte [br_bvis], 1
.ret:
    ret

; --- br_lvcomp - AX = a link -> CF=1 the History has it. All kept -----------
br_lvcomp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    mov bx, ax
    shl bx, 1
    mov si, [br_lnkoff + bx]
    mov bp, [br_histn]              ; BP = the URLs to try
    mov dx, [br_urlseg]
    mov es, [br_lnkseg]             ; ES:SI = the href
    xor cx, cx                      ; CX = its length, to a '#' or the NUL
.len:
    mov bx, si
    add bx, cx
    mov al, [es:bx]
    or al, al
    jz .lend
    cmp al, '#'
    je .lend
    inc cx
    jmp .len
.lend:
    jcxz .no
    mov ds, dx                      ; DS = the URL store (no br_* past here)
    xor bx, bx                      ; BX = the URL's base in the store
.url:
    or bp, bp
    jz .no
    dec bp
    xor di, di                      ; DI = its length
.ul:
    cmp byte [bx + di], 0
    je .uend
    inc di
    cmp di, BR_UBUF
    jb .ul
.uend:
    cmp di, cx
    jb .nexturl                     ; shorter than the href: not it
    cmp byte [es:si + 4], ':'       ; "http:" - an absolute href is equal
    jne .tail
    cmp di, cx
    jne .nexturl
.tail:
    push si
    push di
    sub di, cx
    add di, bx                      ; DI = where the href would start in it
    push cx
.cmp:
    mov al, [es:si]
    cmp al, [di]
    jne .ne
    inc si
    inc di
    loop .cmp
    cmp al, al                      ; every byte matched (the incs moved ZF)
.ne:
    pop cx
    pop di
    pop si
    je .yes                         ; (ZF from the last compare or the loop)
.nexturl:
    add bx, BR_UBUF
    jmp .url
.yes:
    stc
    jmp short .out
.no:
    clc
.out:
    pop ds
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; TABLE LINES, IE3'S WAY (1.8): `border` decides whether a table has a grid at
; all - most of the period's tables are border=0, for layout - and the grid it
; has is solid lines, not the cell font's dotted '-', '|' and '+'.
; =============================================================================

; --- br_tbord - the parser at a tag: AL = its action. All kept --------------
; An OUTER table's border, one bit each in the order they open ([br_tbits],
; 64 of them; past that, none)
br_tbord:
    cmp al, TA_TAB
    jne .ret
    push ax
    push bx
    push cx
    push si
    push di
    cmp byte [br_close], 0
    jne .close
    cmp byte [br_tbdep], 0
    jne .deeper
    mov di, br_a_border
    call br_getattr                 ; SI..CX = its value, CF=1 none
    jc .next
    cmp si, cx
    jae .on                         ; <table border>: one
    cmp byte [es:si], '0'
    je .next                        ; border=0: none
.on:
    mov al, [br_tbn]
    cmp al, 64
    jae .next
    mov bl, al
    mov cl, 3
    shr bl, cl
    xor bh, bh
    and al, 7
    mov cl, al
    mov al, 1
    shl al, cl
    or [br_tbits + bx], al
.next:
    mov bl, [br_tbn]                ; ...and its width (71.34.4)
    cmp bl, 64
    jae .nw
    xor bh, bh
    call br_wattr
    jnc .w
    xor al, al
.w:
    mov [br_twid + bx], al
.nw:
    inc byte [br_tbn]
.deeper:
    inc byte [br_tbdep]
    jmp short .out
.close:
    cmp byte [br_tbdep], 0
    je .out
    dec byte [br_tbdep]
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
.ret:
    ret

; --- br_tlook - the layout met an outer table: [br_tbrd] = its border -------
br_tlook:
    push ax
    push bx
    push cx
    mov byte [br_tbrd], 0
    mov byte [br_twd], 0
    cmp byte [br_tind], 0           ; inside a table laid out inline: a nested
    jne .out                        ; one, which br_tbord gave no slot
    mov al, [br_tlc]
    inc byte [br_tlc]
    cmp al, 64
    jae .out
    mov bl, al
    xor bh, bh
    mov ah, [br_twid + bx]
    mov [br_twd], ah
    mov bl, al
    mov cl, 3
    shr bl, cl
    xor bh, bh
    and al, 7
    mov cl, al
    mov al, [br_tbits + bx]
    shr al, cl
    and al, 1
    mov [br_tbrd], al
.out:
    pop cx
    pop bx
    pop ax
    ret

; --- br_bline - br_build's cell AL: a line code goes the bitmap way. All kept
br_bline:
    cmp al, BR_BH
    jb .ret
    cmp al, BR_BX
    ja .ret
    mov byte [br_drawbitmap], 1
.ret:
    ret

; --- br_bglyph - AL = a cell -> CF=1 and AL = a line's [br_bmrow] row ------
; IE3's BEVEL in one colour: every grid line is a PAIR - a light one (a 50%
; dither, which reads as the lighter grey) on the left or top, a solid dark
; one on the right or bottom. One shape serves every place a line is: a
; cell's light bottom/right beside the next cell's dark top/left, and the
; table's outer edge the same way round, raised.
br_bglyph:
    cmp al, BR_BH
    jb .no
    cmp al, BR_BX
    ja .no
    push bx
    mov bl, al
    mov bh, [br_bmrow]
    xor al, al
    cmp bl, BR_BV
    je .vert
    cmp bh, 2                       ; the horizontals: light on row 2...
    jne .h5
    mov al, 0AAh
    test bh, 1
    jz .vert0
.h5:
    cmp bh, 5                       ; ...dark on row 5
    jne .vert0
    mov al, 0FFh
.vert0:
    cmp bl, BR_BH
    je .out
.vert:
    or al, 02h                      ; the verticals: dark in column 6...
    test bh, 1
    jnz .out
    or al, 40h                      ; ...light in column 1, every other row
.out:
    pop bx
    stc
    ret
.no:
    clc
    ret

; --- br_strun - br_paintstyled's raster row: AX = x, BX = y, CX = width,
; DX = 1, ES:SI = the row, BP = its stride. A run of equal attribute a BLIT1
; and a pen when the row has colours (and the adapter shows them); else the
; row at once. CF = BLIT1's
br_strun:
    cmp byte [br_lcolr], 0
    je .one
    cmp byte [br_bpp], 1
    jne .runs
.one:
    cmp byte [br_bpp], 1            ; (a 1bpp adapter reads no pen)
    je .blit
    push ax                         ; THE PEN AGAIN, EVERY ROW: it holds for
    call br_stpen                   ; one lock hold (SPEC.md 5.4.2.2), and on
    call OSAPI_GFX_BLIT1_PEN        ; an XT the eight rows of a heading outlast
    pop ax                          ; one - the rest came out white on black
.blit:
    call OSAPI_GFX_BLIT1
    ret
.runs:
    push ax
    push cx
    push si
    push di
    mov word [br_sc0], 0
.r:
    mov si, [br_sc0]
    cmp si, [br_cols]
    jae .done
    mov al, [br_lattr + si]
    mov di, si
.e:
    inc di
    cmp di, [br_cols]
    jae .emit
    cmp al, [br_lattr + di]
    je .e
.emit:
    call br_apen
    call OSAPI_GFX_BLIT1_PEN
    mov cx, di
    sub cx, si
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; its pixels
    mov ax, si
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [br_bmpx]
    add si, br_stylebuf
    call OSAPI_GFX_BLIT1
    jc .done
    mov [br_sc0], di
    jmp .r
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

; --- br_bgrey - br_build's cell at DI: a table's line takes dark grey ink on
; a colour display, the way IE3 drew its grid. All kept
br_bgrey:
    cmp byte [br_bpp], 1
    je .ret
    cmp byte [di], BR_BH
    jb .ret
    cmp byte [di], BR_BX
    ja .ret
    push ax
    mov al, [di + (br_lattr - br_lbuf)]
    and al, 0F0h
    or al, CDGRAY
    mov [di + (br_lattr - br_lbuf)], al
    mov byte [br_lcolr], 1
    pop ax
.ret:
    ret

; =============================================================================
; WIDTHS, IE3's way (71.34.4): a <table>'s and a <td>'s `width`, PIXELS or a
; PERCENT. One byte says either: 0 none, 80h + pixels / 16 rounded up (at
; most 63, 80h-BFh: C0h-FEh are a picture's slices, 0FFh BR_SNAP), 20h +
; percent / 2 (2% steps).
; =============================================================================

; --- br_wattr - the tag's `width` -> CF=0 AL = its byte; CF=1 none. ES = the
; source. AX clobbered, all else kept
br_wattr:
    push bx
    push cx
    push dx
    push si
    push di
    mov di, br_a_width
    call br_getattr                 ; SI..CX = its value
    jc .out
    xor ax, ax
    xor bh, bh
.d:
    cmp si, cx
    jae .end
    mov bl, [es:si]
    sub bl, '0'
    cmp bl, 9
    ja .end
    mov dx, 10
    mul dx
    add ax, bx
    inc si
    jmp .d
.end:
    or ax, ax
    jz .none
    cmp si, cx
    jae .px
    cmp byte [es:si], '%'
    jne .px
    cmp ax, 100
    jbe .pc
    mov ax, 100
.pc:
    shr ax, 1
    add al, 20h
    clc
    jmp short .out
.px:
    add ax, 15                      ; two cells a unit, 63 at most: 80h-BFh,
    mov cl, 4                       ; so C0h-FEh are a picture's slices
    shr ax, cl
    cmp ax, 63
    jbe .p2
    mov ax, 63
.p2:
    or al, 80h
    clc
    jmp short .out
.none:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; --- br_tcwput - <td width>: D_CMETA and its byte after the span's. All kept
br_tcwput:
    push ax
    call br_wattr
    jc .out
    push ax
    mov al, D_CMETA
    call br_put
    pop ax
    call br_put
.out:
    pop ax
    ret

; =============================================================================
; FRAMES, IE3's way (71.34.5): a frameset's tables are written straight into
; the stream, not by a <table> tag, so they are counted here or every table
; after them takes the wrong border and width. A frame table is bordered and
; the band's full width - IE3's bevelled dividers - and a `cols` list gives
; each frame its own width.
; =============================================================================

; --- br_tbfake - a D_TAB1 written without a <table>: count it, bordered and
; 100% wide. All kept
br_tbfake:
    push ax
    push bx
    push cx
    mov al, [br_tbn]
    cmp al, 64
    jae .n
    mov bl, al
    xor bh, bh
    mov byte [br_twid + bx], 20h + 50   ; 100%
    mov cl, 3
    shr bl, cl
    and al, 7
    mov cl, al
    mov al, 1
    shl al, cl
    or [br_tbits + bx], al
.n:
    inc byte [br_tbn]
    pop cx
    pop bx
    pop ax
    ret

; --- br_fcols - <frameset cols="20%,*,200">: each frame's width byte in
; [br_fcolw] (0 for '*'), the next frame the first. ES = the source. All kept
br_fcols:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [br_fcolw], 0
    mov word [br_fcolw + 2], 0
    mov word [br_fcolw + 4], 0
    mov word [br_fcolw + 6], 0
    mov byte [br_fcolk], 0
    mov di, br_a_cols
    call br_getattr                 ; SI..CX = the list
    jc .out
    xor di, di                      ; DI = the frame
.item:
    cmp si, cx
    jae .out
    cmp di, 8
    jae .out
    xor ax, ax
    xor bh, bh
.d:
    cmp si, cx
    jae .end
    mov bl, [es:si]
    sub bl, '0'
    cmp bl, 9
    ja .end
    mov dx, 10
    mul dx
    add ax, bx
    inc si
    jmp .d
.end:
    xor dl, dl                      ; DL = its byte, 0 a '*' or nothing
    or ax, ax
    jz .skip
    cmp si, cx
    jae .px
    cmp byte [es:si], '%'
    jne .px
    cmp ax, 100
    jbe .pc
    mov ax, 100
.pc:
    shr ax, 1
    add al, 20h
    mov dl, al
    jmp short .skip
.px:
    add ax, 15
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    cmp ax, 63
    jbe .p2
    mov ax, 63
.p2:
    or al, 80h
    mov dl, al
.skip:
    cmp si, cx                      ; to the comma, past it
    jae .put
    cmp byte [es:si], ','
    je .comma
    inc si
    jmp .skip
.comma:
    inc si
.put:
    mov [br_fcolw + di], dl
    inc di
    jmp .item
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_fwnext - a <frame> in a cols frameset: AL = its width byte. BX kept
br_fwnext:
    push bx
    mov bl, [br_fcolk]
    xor al, al
    cmp bl, 8
    jae .out
    inc byte [br_fcolk]
    xor bh, bh
    mov al, [br_fcolw + bx]
.out:
    pop bx
    ret

; --- br_fcellw - the frame cell being written: its span hint, and its width
; when its frameset gave one. BX = the frame. All kept
br_fcellw:
    push ax
    mov al, D_CMETA
    call br_put
    xor al, al                      ; one column, left
    call br_put
    mov al, [br_framew + bx]
    or al, al
    jz .out
    push ax
    mov al, D_CMETA
    call br_put
    pop ax
    call br_put
.out:
    pop ax
    ret

; --- br_fbody - a frame document's <body bgcolor text>: records at its cell's
; head, which the composer takes as the cell's state (br_tcpeek). All kept
br_fbody:
    push ax
    push di
    mov di, br_a_bgcolor
    call br_colattr
    jc .t
    add al, COL_BGDEF + 1           ; ground c
    call br_colput
.t:
    mov di, br_a_text
    call br_colattr
    jc .o
    inc al                          ; ink c
    call br_colput
.o:
    pop di
    pop ax
    ret

; =============================================================================
; TEXT BESIDE A PICTURE (1.8 step 4): <img align=left> outside a table is
; written as an invisible table the band's width - one row, the picture's
; cell its own width (a <td width>, so the row's slack is the text's) and the
; text's cell after it, to the next block. A row of cells is what this
; renderer has; a picture floating in a line of text is not. The composer
; (part 9) already sets a picture in a cell beside text, a row a cell.
; =============================================================================

; --- br_fltopen - the picture tag, before its record. All kept --------------
br_fltopen:
    cmp byte [br_ialign], BR_IA_LEFT
    jne .ret
    cmp byte [br_flt], 0
    jne .ret                        ; one float at a time
    cmp byte [br_tbdep], 0
    jne .ret                        ; in a table it is the cell's picture
    cmp byte [br_pre], 0
    jne .ret
    push ax
    call br_break                   ; the paragraph so far ends above it
    mov al, D_TAB1
    call br_put
    call br_tbflt                   ; counted: no border, 100%
    mov al, D_ROW
    call br_put
    mov al, D_CELL
    call br_put
    mov al, D_CMETA
    call br_put
    xor al, al                      ; one column, left
    call br_put
    mov al, D_CMETA
    call br_put
    mov al, [br_icols]
    add al, 2                       ; the picture and its inset, fixed, in
    shr al, 1                       ; units of two cells
    or al, 80h
    call br_put
    mov byte [br_ialign], 2         ; in its cell, align=top: the text
                                    ; beside it starts at its top, IE3's
    mov byte [br_flt], 2
    pop ax
.ret:
    ret

; --- br_fltcell - the picture is written: the text's cell opens. All kept --
br_fltcell:
    cmp byte [br_flt], 2
    jne .ret
    push ax
    mov al, D_CELL
    call br_put
    mov al, D_CMETA
    call br_put
    xor al, al
    call br_put
    mov byte [br_flt], 1
    pop ax
.ret:
    ret

; --- br_fltend - a block, <br clear> or the end: the float's table closes ---
br_fltend:
    cmp byte [br_flt], 1
    jne .ret
    mov byte [br_flt], 0
    push ax
    mov al, D_TAB0
    call br_put
    pop ax
.ret:
    ret

; --- br_brclear - a <br> with a `clear`. All kept ---------------------------
br_brclear:
    cmp byte [br_flt], 1
    jne .ret
    push ax
    push cx
    push si
    push di
    mov di, br_a_clear
    call br_getattr
    pop di
    pop si
    pop cx
    pop ax
    jc .ret
    call br_fltend
.ret:
    ret

; --- br_tbflt - the float's D_TAB1, counted as a table: no border, 100% ----
br_tbflt:
    push ax
    push bx
    mov al, [br_tbn]
    cmp al, 64
    jae .n
    mov bl, al
    xor bh, bh
    mov byte [br_twid + bx], 20h + 50
.n:
    inc byte [br_tbn]
    pop bx
    pop ax
    ret

br_a_clear: db 'clear', 0

; --- br_bslice - br_build at a D_CMETA, ES:SI at its payload: C0h-FEh is a
; picture's slice (71.34.5), for the D_IMG copy that follows. All kept
br_bslice:
    push ax
    mov al, [es:si]
    cmp al, 0C0h
    jb .out
    cmp al, BR_SNAP
    je .out
    sub al, 0C0h
    mov [br_bslc], al
.out:
    pop ax
    ret
