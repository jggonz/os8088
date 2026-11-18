; =============================================================================
; brtable.inc - the table composer, PART 9 (SPEC.md 71.34.2): moved out of the
; primary whole, with the table-only colour helpers that were brcolor.inc's.
; It runs with DS = the primary (every br_* datum is the primary's, imported
; by tools/brdlsyms.py) and CS = the part, and reaches primary code only far,
; through br_tb_rtab (brtabres.inc).
; =============================================================================
; -----------------------------------------------------------------------------
; br_table - lay out the table whose body starts at SI
; in:  SI = just past D_TAB1, ES = docseg
; out: CF=0 the table was emitted and SI is past its D_TAB0
;      CF=1 INLINE IT - 3.2.1 says this is a box, not a table; SI unchanged
;           and the caller lays the body out as ordinary content
; -----------------------------------------------------------------------------
br_table:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    call br_tmeas                   ; widths, ncols, cells, any-text
    pop si
    push si
    cmp byte [br_tany], 0
    je .decor                       ; no text in any cell: decoration
    cmp word [br_tcells], 1
    jbe .box                        ; 1x1: a box, not a table
    cmp byte [br_tn], 2
    jb .box
    call br_tfit                    ; squeeze the columns into the band
    call br_temit                   ; ...and put the rows out
    pop si
    call br_tskip                   ; SI past the matching D_TAB0
    clc
    jmp .out
.decor:
    pop si
    call br_tskip
    clc
    jmp .out
.box:
    pop si
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_tmeas - one measuring pass: how wide does each column want to be?
; in:  SI = table body start
; out: [br_tn] = columns (capped), [br_tw] = wanted widths, [br_tflr] = the
;      longest WORD in each (a column squeezed below it splits words - the
;      model found that one, BROWSER-PLAN 2.5), [br_tcells], [br_tany]
; -----------------------------------------------------------------------------
br_tmeas:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    mov cx, BR_TCOLS
    mov di, br_tw
    push es
    push ds
    pop es
    xor al, al
    cld
    rep stosb
    mov cx, BR_TCOLS
    mov di, br_tflr
    rep stosb
    pop es
    mov byte [br_tfix], 0           ; no column with a width of its own
    mov byte [br_tn], 0
    mov byte [br_tany], 0
    mov word [br_tcells], 0
    mov byte [br_tcol], 0FFh        ; no cell open yet
    mov byte [br_tspan], 1
    xor bx, bx                      ; BX = this cell's running length
    xor dx, dx                      ; DX = this cell's running WORD length
    xor cx, cx                      ; CX = nested-table depth
.loop:
    cmp si, [br_doclen]
    jae .done
    mov al, [es:si]
    inc si
    cmp al, 0x20
    jae .text
    cmp al, D_LNK1                  ; a link's three nibbles (16..31) are
    jne .tim                        ; not markers: one of them can read as
    add si, 3                       ; D_IMG
    jmp .loop
.tim:
    cmp al, D_IMG                   ; A PICTURE IN A CELL (1.8): COLS cells
    jne .tab                        ; of the column, one unbreakable unit
    call br_tmimg
    jmp .loop
.tab:
    cmp al, D_TAB1
    jne .t0
    inc cx                          ; a nested table FLATTENS into this cell
    jmp .loop
.t0:
    cmp al, D_TAB0
    jne .row
    or cx, cx
    jnz .t0end
    jmp .done
.t0end:
    dec cx
    jmp .loop
.row:
    or cx, cx
    jnz .loop                       ; inside a nested table: not our structure
    cmp al, D_ROW
    jne .cell
    call br_tcellend
    mov byte [br_tcol], 0FFh
    xor bx, bx
    xor dx, dx
    jmp .loop
.cell:
    cmp al, D_CELL
    jne .loop
    call br_tcellend
    cmp byte [br_tcol], 0FFh
    jne .nextcol
    mov byte [br_tcol], 0
    jmp .meta
.nextcol:
    mov al, [br_tspan]
    add [br_tcol], al
.meta:
    mov byte [br_tspan], 1
    mov byte [br_talign], 0
    cmp byte [es:si], D_CMETA
    jne .clipspan
    inc si
    mov al, [es:si]
    mov bl, al
    and al, 7
    inc al
    mov [br_tspan], al
    shr bl, 1
    shr bl, 1
    shr bl, 1
    and bl, 3
    mov [br_talign], bl
    inc si
    cmp byte [es:si], D_CMETA       ; ...and its WIDTH (71.34.4)
    jne .clipspan
    mov al, [es:si + 1]
    cmp al, 20h
    jb .clipspan
    cmp al, 0C0h
    jae .clipspan
    call br_tcwidth
    add si, 2
.clipspan:
    mov al, BR_TCOLS
    sub al, [br_tcol]
    cmp [br_tspan], al
    jbe .count
    mov [br_tspan], al
.count:
    inc word [br_tcells]
    xor bx, bx
    xor dx, dx
    jmp .loop
.text:
    or cx, cx
    jnz .txt2
.txt2:
    cmp byte [br_tcol], 0FFh
    je .loop                        ; text outside any cell: not ours
    inc bx
    cmp al, ' '
    je .wsp
    inc dx
    mov byte [br_tany], 1
    jmp .loop
.wsp:
    call br_twordend
    xor dx, dx
    jmp .loop
.done:
    call br_tcellend
    mov al, [br_tn]
    cmp al, BR_TCOLS
    jbe .cap
    mov byte [br_tn], BR_TCOLS
.cap:
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_tmimg - a picture's record in a cell (SI just past its D_IMG): BX +=
; its COLS, a word of its own; the table has content. SI past the record and
; its alt text. AX clobbered; a damaged record is skipped as nothing
br_tmimg:
    call far [br_tb_r_imgdec]                  ; [br_icols], [br_ialen]; SI at the alt
    jc .ret
    mov al, [br_ialen]
    xor ah, ah
    add si, ax
    cmp byte [br_tcol], 0FFh
    je .ret                         ; outside any cell: not ours
    call br_twordend                ; the word before it ends here
    xor dx, dx
    mov al, [br_icols]
    xor ah, ah
    add bx, ax
    push di                         ; ...and the column's FLOOR is the whole
    mov ah, [br_tcol]               ; picture, not BR_TFLRMAX: br_tfit never
    cmp ah, BR_TCOLS                ; squeezes a column through it
    jae .nf
    mov di, ax
    xor al, al
    xchg al, ah
    xchg ax, di                     ; DI = the column, AL = COLS
    cmp al, [di+br_tflr]
    jbe .nf
    mov [di+br_tflr], al
.nf:
    pop di
    mov byte [br_tany], 1           ; a table of pictures is not decoration
.ret:
    ret

; --- br_tcellend - fold BX (length) and DX (word) into this column -----------
br_tcellend:
    push ax
    push di
    call br_twordend
    mov al, [br_tcol]
    cmp al, 0FFh
    je .out
    cmp al, BR_TCOLS
    jae .out
    xor ah, ah
    mov di, ax                      ; starting logical column
    mov al, [br_tspan]
    dec al
    shl al, 1
    xor ah, ah
    cmp bx, ax
    ja .adjust
    mov bx, 1
    jmp .width
.adjust:
    sub bx, ax                      ; separators absorbed by the spanning cell
.width:
    mov al, [br_tcol]
    mov ah, [br_tspan]
    add al, ah                      ; the COUNT through this cell - [br_tn] is
    cmp al, BR_TCOLS                ; a column count everywhere it is read,
    jbe .ncap                       ; not the last logical index
    mov al, BR_TCOLS
.ncap:
    cmp al, [br_tn]
    jbe .w
    mov [br_tn], al                 ; the widest ROW decides the count
.w:
    cmp bx, 255
    jbe .w2
    mov bx, 255
.w2:
    cmp bl, [di+br_tw]
    jbe .out
    mov [di+br_tw], bl
.out:
    pop di
    pop ax
    ret

; --- br_twordend - fold DX (a word's length) into this column's floor --------
br_twordend:
    push ax
    push di
    or dx, dx
    jz .out
    mov al, [br_tcol]
    cmp al, 0FFh
    je .out
    cmp al, BR_TCOLS
    jae .out
    xor ah, ah
    mov di, ax
    cmp dx, BR_TFLRMAX
    jbe .f
    mov dx, BR_TFLRMAX              ; a very long word splits; it does not get
.f:                                 ; to reserve the whole band for itself
    cmp dl, [di+br_tflr]
    jbe .out
    mov [di+br_tflr], dl
.out:
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_tfit - squeeze the wanted widths into the band
; The drawn width is sum(w) + 2*ncols + 1 - a '|' and a space either side of
; every column, and a '+' at each edge - so that is what has to fit, not the
; sum alone.
; -----------------------------------------------------------------------------
br_tfit:
    push ax
    push bx
    push cx
    push dx
    push di
    mov cl, [br_tn]
    xor ch, ch
    or cx, cx                       ; jcxz is short-only on an 8086 and .out
    jnz .go                         ; is past its reach
    jmp .out
.go:
    mov ax, [br_cols]               ; avail = cols - 2n - 1
    mov bx, cx
    shl bx, 1
    inc bx
    sub ax, bx
    jns .a
    mov ax, 1
.a:
    push ax
    call br_ttarget                 ; AX = the table's own, 0 none (71.34.4)
    mov [br_ttin], ax
    pop ax
    cmp word [br_ttin], 0
    je .a2
    sub word [br_ttin], bx          ; its columns: less the separators
    ja .a1
    mov word [br_ttin], 0
    jmp short .a2
.a1:
    cmp ax, [br_ttin]
    jbe .a2
    mov ax, [br_ttin]               ; narrower than the band: that is the fit
.a2:
    mov [br_tavail], ax
    xor bx, bx                      ; BX = sum of wanted
    xor di, di
.sum:
    mov al, [di+br_tw]
    xor ah, ah
    add bx, ax
    inc di
    cmp di, cx
    jb .sum
    cmp bx, [br_tavail]
    jbe .out                        ; it already fits: nothing to squeeze
    or bx, bx
    jz .out
                                    ; proportional, then lifted back to each
                                    ; column's longest WORD where there is room
    xor di, di
.sq:
    mov al, [di+br_tw]
    xor ah, ah
    mul word [br_tavail]
    div bx                          ; w * avail / sum
    or ax, ax
    jnz .s1
    mov ax, 1
.s1:
    cmp al, [di+br_tflr]
    jae .s2
    mov al, [di+br_tflr]            ; never squeeze THROUGH a word
.s2:
    mov [di+br_tw], al
    inc di
    cmp di, cx
    jb .sq
                                    ; give any excess back from the widest
.trim:
    xor bx, bx
    xor di, di
    mov dx, 0                       ; DX = index of the widest
.t1:
    mov al, [di+br_tw]
    xor ah, ah
    add bx, ax
    inc di
    cmp di, cx
    jb .t1
    cmp bx, [br_tavail]
    jbe .out
    xor di, di
    xor dx, dx
    mov ah, 0
.t2:
    mov al, [di+br_tw]
    cmp al, [di+br_tflr]            ; never trim a column through its floor
    jbe .t3                         ; (a word, or a picture: 1.8); none left
    cmp al, ah                      ; and the table overflows the band
    jbe .t3
    mov ah, al
    mov dx, di
.t3:
    inc di
    cmp di, cx
    jb .t2
    cmp ah, 2
    jbe .out                        ; cannot shrink further: let it overflow
    mov di, dx
    dec byte [di+br_tw]
    jmp .trim
.out:
    call br_tgrow                   ; ...and out to the table's width
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_temit - compose and emit the table's lines
; in:  SI = table body start
; -----------------------------------------------------------------------------
br_temit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call br_trule                   ; the top edge
.rows:
    call br_tfindrow                ; SI -> just past the next D_ROW, CF=1 none
    jc .out
    call br_tcells_of               ; fill br_cs/br_ce from this row
    call br_trowout                 ; ...and put its wrapped lines out
    call br_trule
    jmp .rows
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_tfindrow - advance SI to just past the next D_ROW at depth 0 ---------
br_tfindrow:
    push ax
    push cx
    xor cx, cx
.loop:
    cmp si, [br_doclen]
    jae .none
    mov al, [es:si]
    inc si
    cmp al, D_TAB1
    jne .t0
    inc cx
    jmp .loop
.t0:
    cmp al, D_TAB0
    jne .r
    jcxz .none
    dec cx
    jmp .loop
.r:
    cmp al, D_ROW
    je .row
    cmp al, D_CELL                 ; classic HTML often omits the TR wrapper.
    je .implicit                   ; Treat the first cell as a one-row table;
    jmp .loop                      ; explicit rows still delimit subsequent
.row:                              ; rows in br_tcells_of.
    or cx, cx
    jnz .loop
    clc
    jmp .out
.implicit:
    or cx, cx                      ; never promote a cell inside a nested table
    jnz .loop
    dec si                         ; br_tcells_of starts at the D_CELL marker
    clc
    jmp .out
.none:
    stc
.out:
    pop cx
    pop ax
    ret

; --- br_tcells_of - record this row's cell spans into br_cs / br_ce ----------
; in:  SI just past a D_ROW; out: SI at the row's end, [br_tnc] = cells found
br_tcells_of:
    push ax
    push bx
    push cx
    push di
    mov byte [br_tnc], 0
    mov di, 0FFFFh                  ; DI = the open cell's index, FFFF = none
    xor cx, cx
.loop:
    cmp si, [br_doclen]
    jae .done
    mov al, [es:si]
    cmp al, D_CMETA                 ; a cell hint's payload (0..23) is not a
    jne .t1                         ; marker: a 9th cell's, or a nested
    add si, 2                       ; table's, reaches here and would count
    jmp .loop                       ; as a D_TAB1/D_TAB0
.t1:
    cmp al, D_TAB1
    jne .t0
    inc cx
    inc si
    jmp .loop
.t0:
    cmp al, D_TAB0
    jne .row
    jcxz .done
    dec cx
    inc si
    jmp .loop
.row:
    or cx, cx
    jnz .adv
    cmp al, D_ROW
    je .done
    cmp al, D_CELL
    jne .adv
    call br_tclose
    mov al, [br_tnc]
    cmp al, BR_TCOLS
    jae .adv2
    xor ah, ah
    mov bx, ax
    mov di, ax
    shl di, 1
    inc si
    mov byte [br_tspanarr+bx], 1
    mov byte [br_talignarr+bx], 0
    cmp byte [es:si], D_CMETA
    jne .cellstart
    inc si
    mov al, [es:si]
    mov bl, al
    and al, 7
    inc al
    mov bx, di
    shr bx, 1
    mov [br_tspanarr+bx], al
    mov al, [es:si]
    shr al, 1
    shr al, 1
    shr al, 1
    and al, 3                       ; the alignment, built in AL - BX is the
    mov bx, di                      ; index and was overwriting it
    shr bx, 1
    mov [br_talignarr+bx], al
    inc si
    cmp byte [es:si], D_CMETA       ; ...past its width too (71.34.4), so the
    jne .cellstart                  ; cell starts at its own first byte
    cmp byte [es:si + 1], 20h
    jb .cellstart
    add si, 2
.cellstart:
    mov [di+br_cs], si
    mov [di+br_ce], si
    inc byte [br_tnc]
    jmp .loop
.adv2:
    mov di, 0FFFFh
.adv:
    inc si
    jmp .loop
.done:
    call br_tclose
    pop di
    pop cx
    pop bx
    pop ax
    ret

; --- br_tclose - the open cell (if any) ends at SI --------------------------
br_tclose:
    push ax
    push bx
    cmp di, 0FFFFh
    je .out
    mov bx, di
    mov [bx+br_ce], si
.out:
    pop bx
    pop ax
    ret

; --- br_trowout - wrap every cell and emit the row's display lines -----------
br_trowout:
    push ax
    push bx
    push cx
    push dx
    push di
    mov cl, [br_tnc]
    xor ch, ch
    or cx, cx
    jnz .go
    jmp .out
.go:
    xor di, di                      ; copy the spans into the wrap cursors
.init:
    mov bx, di
    shl bx, 1
    mov ax, [bx+br_cs]
    mov [bx+br_tcur], ax
    mov word [bx+br_tlnk], 0FFFFh   ; no anchor is open in a fresh row
    mov al, [br_cplast]             ; ...and the cells' ink is the table's,
    xor ah, ah                      ; their ground the page's (1.8)
    mov [bx+br_tclr], ax
    mov byte [bx+br_tsz], 0         ; ...in the cell until it asks (71.35)
    mov word [bx+br_tpk], 0         ; ...and no picture's slices pending
    call br_tcpeek                  ; ...unless the cell opens with its own
    inc di
    cmp di, cx
    jb .init
.line:
    mov byte [br_tlogical], 0
    call br_tcompose                ; CF=1 when every cell is exhausted
    jc .out
    jmp .line
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_tcompose - one display line of the current row -----------------------
; out: CF=1 if there was nothing left to draw
br_tcompose:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    mov byte [br_tadvanced], 0
    call br_cstart                  ; DI = the composed line's start
    call br_tbar                    ; AL = the column's edge (1.8)
    call br_cput
    xor cx, cx                      ; CX = source cell index
    xor dx, dx                      ; DX = did any cell contribute?
.col:
    cmp cl, [br_tnc]
    jae .missing
    mov bx, cx
    mov al, [bx+br_tspanarr]
    add al, [br_tlogical]
    cmp al, [br_tn]
    jbe .spanok
    mov al, [br_tn]
    sub al, [br_tlogical]
    mov [bx+br_tspanarr], al
.spanok:
    call br_tcbgon                  ; the inset takes the cell's ground (1.8)
    mov al, ' '
    call br_cput
    mov al, D_CMETA                 ; THE CELL STARTS HERE (71.35): a tall
    call br_cput                    ; row's letters snap to its column
    mov al, BR_SNAP
    call br_cput
    call br_tcell1                  ; one cell's next fragment, padded
    or dx, ax
    call br_tbar                    ; AL = the column's edge (1.8)
    call br_cput
    mov bx, cx
    mov al, [bx+br_tspanarr]
    add [br_tlogical], al
    inc cx
    jmp .col
.missing:
    mov al, [br_tlogical]
    cmp al, [br_tn]
    jae .end
    mov bl, al
    xor bh, bh
    mov cl, [bx+br_tw]
    xor ch, ch
.blank:
    jcxz .blankdone
    mov al, ' '
    call br_cput
    dec cx
    jmp .blank
.blankdone:
    call br_tbar                    ; AL = the column's edge (1.8)
    call br_cput
    inc byte [br_tlogical]
    mov cx, [br_tnc]
    jmp .missing
.end:
    cmp byte [br_tadvanced], 0      ; a live table cursor must advance on
    je .none                        ; every emitted display line
    or dx, dx
    jz .none
    call br_cend                    ; emit the line entry
    clc
    jmp .out
.none:
    call br_cdrop                   ; nothing on it: give the bytes back
    stc
.out:
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_tcell1 - the next fragment of column CX, padded to its width --------
; out: AX = 1 if this cell contributed any character
br_tcell1:
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, cx
    mov al, [bx+br_tspanarr]
    or al, al
    jnz .span
    ; A source cell past the eight-column edge has a measured span of zero.
    ; It has no display column; consume it rather than letting DEC wrap the
    ; width loop through 256 bytes of adjacent state.
    shl bx, 1
    mov si, [bx+br_ce]
    mov [bx+br_tcur], si
    xor ax, ax
    jmp .out
.span:
    mov [br_tspan], al
    mov al, [bx+br_talignarr]
    mov [br_talign], al
    mov bl, [br_tlogical]
    xor bh, bh
    xor di, di
.width:
    mov al, [bx+br_tw]
    xor ah, ah
    add di, ax
    inc bx
    dec byte [br_tspan]
    jnz .width
    mov bx, cx
    mov al, [bx+br_tspanarr]
    dec al
    shl al, 1
    xor ah, ah
    add di, ax                      ; absorb each spanned separator and inset
    xor dx, dx                      ; DX = characters placed
    shl bx, 1
    mov [br_tccol], bx              ; (br_twimg's column, 71.34.5)
    mov si, [bx+br_tcur]
    mov ax, [bx+br_ce]
.skip:
    cmp si, ax                      ; leading spaces never start a fragment
    jae .exh
    push ax
    mov al, [es:si]
    cmp al, D_COL                   ; a colour joins the cell's state, and is
    jne .skc                        ; re-opened below (1.8)
    pop ax
    call br_tcabs
    jmp .skip
.exh:
    call br_tcolon                  ; an emptied cell's ground still pads
    call br_tpslice                 ; ...or a picture's next slice fills it
    jmp .store
.skc:
    cmp al, D_LNK1                  ; ...but a LINK MARKER IS NOT A SPACE, and
    jae .keep                       ; eating it here is what left every anchor
    cmp al, ' '                     ; in a table cell with no bracket at all
    pop ax                          ; (issue #137): the marker is the cell's
    ja .go                          ; first byte in the common case
    inc si                          ; <td><a href=...>, so this loop consumed
    jmp .skip                       ; it before br_twrap ever saw one
.keep:
    pop ax
.go:
    call br_tcolon                  ; the cell's colours open the fragment
    push bx
    push ax                         ; the cell's end - br_twrap's own AX
    mov ax, [bx+br_tlnk]            ; A LINK THE LAST FRAGMENT LEFT OPEN IS
    mov [br_wlnk], ax               ; RE-OPENED AT THE HEAD OF THIS ONE and
    cmp ax, 0FFFFh                  ; closed again at its tail, so each
    je .g2                          ; composed row is balanced on its own. The
    call br_creopen                 ; row is one arena line with every column
.g2:                                ; on it, and br_linkat answers a click by
    pop ax                          ; scanning BACKWARD - so a link left open
    call br_twrap                   ; at the end of column 0 would swallow
    mov dx, cx                      ; columns 1..n and send their clicks to the
    mov ax, [br_wlnk]               ; wrong page. Re-opening costs four bytes
    mov [bx+br_tlnk], ax            ; of arena per wrapped fragment and needs
    cmp ax, 0FFFFh                  ; no second walk
    je .g3
    mov al, D_LNK0
    call br_cput
.g3:
    pop bx
.store:
    cmp si, [bx+br_tcur]
    jbe .stored
    mov byte [br_tadvanced], 1
.stored:
    mov [bx+br_tcur], si
.pad:
    mov cx, dx                      ; pad from the REAL count up to the width,
.padloop:                           ; counting separately - DX must stay the
    cmp cx, di                      ; number of characters this cell actually
    jae .done                       ; contributed, or an exhausted row of empty
    mov al, ' '                     ; cells reports itself as non-empty and
    call br_cput                    ; br_trowout composes blank rows for ever
    inc cx
    jmp .padloop
.done:
    call br_tcoloff                 ; ...and close at its tail (1.8)
    mov ax, 0
    or dx, dx
    jz .out
    mov ax, 1
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; --- br_twrap - take up to DI characters of ES:SI, breaking on a space -------
; in:  SI = cursor, AX = end, DI = width; out: SI advanced, CX = placed
;
; TWO PASSES on purpose. The first decides how many characters this fragment
; gets and emits nothing; only then are they put out. A single pass cannot
; back up: by the time it knows the word will not fit it has already composed
; half of it, and there is no un-emitting from an append-only arena.
br_twrap:
    push ax
    push bx
    push dx
                                    ; SI IS AN OUTPUT - the advanced cursor -
                                    ; so it is deliberately not saved. Pushing
                                    ; it here rewinds the cell on every call,
                                    ; so the cell never empties, always reports
                                    ; that it contributed, and the row composes
                                    ; until the arena is full
    xor cx, cx                      ; CX = characters scanned
    xor bx, bx                      ; BX = count at the last space, 0 = none
    mov dx, si
.scan:
    cmp si, ax
    jae .take
    cmp cx, di
    jae .full
    push ax
    mov al, [es:si]
    cmp al, D_LNK1                  ; a link's nibbles: not markers (one can
    jne .scimg                      ; read as D_IMG)
    add si, 3
    jmp .adv
.scimg:
    cmp al, D_IMG                   ; A PICTURE (1.8): COLS cells, whole
    jne .scmeta
    push si
    inc si
    call far [br_tb_r_imgdec]
    jc .scbad
    mov al, [br_ialen]
    xor ah, ah
    add si, ax                      ; SI = past the record
    mov al, [br_icols]
    add ax, cx                      ; (AH is 0: COLS <= 96)
    cmp ax, di
    jbe .scfit
    or cx, cx
    jz .scfit                       ; alone and too wide: it goes anyway
    pop si                          ; the fragment ends BEFORE it
    pop ax
    xor bx, bx
    jmp .take
.scfit:
    mov cx, ax
    mov bx, ax
    inc bx                          ; a break is allowed after it (.full
    add sp, 2                       ; takes BX - 1)
    pop ax
    jmp .scan
.scbad:
    pop si                          ; a damaged record: an ordinary marker
    jmp .adv
.scmeta:
    cmp al, D_CMETA
    jne .sc1
    inc si                          ; a cell hint's payload is not a marker
.sc1:
    cmp al, D_BR                    ; A BLOCK BREAK IN A CELL (71.34.5): the
    je .brk                         ; fragment ends at it, and the text after
    cmp al, D_PARA                  ; it starts the cell's next line - <br>,
    je .brk                         ; <p>, a heading, an item, a rule, IE3's
    cmp al, D_HEAD                  ; way where they were dropped
    je .brk
    cmp al, D_LI
    je .brk
    cmp al, D_HR
    je .brk
    cmp al, D_TAB1                  ; ...and a nested table's own structure:
    je .brk                         ; it is inlined, a line a cell, and its
    cmp al, D_ROW                   ; records then open the line they colour
    je .brk                         ; rather than the end of the one before
    cmp al, D_CELL
    jne .nbrk
.brk:
    or cx, cx
    jz .adv                         ; at the fragment's start: just past it
    pop ax
    xor bx, bx
    jmp .take
.nbrk:
    cmp al, 0x20
    jb .adv                         ; a marker inside a cell is not a cell
    inc cx
    cmp al, ' '
    jne .adv
    mov bx, cx                      ; ...a place we could break
.adv:
    pop ax
    inc si
    jmp .scan
.full:
    or bx, bx
    jz .take                        ; one long word: hard split at the width
    mov cx, bx
    dec cx                          ; ...excluding the space itself
.take:
    mov byte [br_tpad], 0
    jcxz .emitstart
    push ax                          ; alignment output must not replace the
                                     ; source cell's end offset in AX
    push dx                          ; preserve the source cursor across padding
    mov bx, di
    sub bx, cx                       ; spare cells inside this cell
    jnc .spare
    xor bx, bx                       ; none: a picture wider than its column
.spare:                              ; (1.8) - and never 65,000 spaces
    xor dx, dx
    cmp byte [br_talign], 2
    je .rightpad
    cmp byte [br_talign], 1
    jne .pad
    shr bx, 1
.rightpad:
    mov dx, bx
.pad:
    or dx, dx
    jz .padend
    mov al, ' '
    call br_cput
    dec dx
    inc byte [br_tpad]
    jmp .pad
.padend:
    pop dx
    pop ax
.emitstart:
    mov si, dx                      ; rewind and emit exactly CX characters
    xor dx, dx
.emit:
    cmp si, ax                      ; **THE CHARACTER BUDGET IS TESTED AT THE
    jae .skiptail                   ; TEXT BYTE, not at the top of the loop**:
    push ax                         ; a link's closing marker sits just past
    mov al, [es:si]                 ; the last character it covers, and exiting
    cmp al, 0x20                    ; on the count alone left it behind - the
    jae .etxt                       ; anchor stayed open across the cell wall
    cmp dx, cx                      ; ...but past the last character, a BREAK
    jb .mk                          ; ends it: what follows it opens the next
    call br_isbrk                   ; line (71.34.5), its colours included
    jc .edone
.mk:
    cmp al, D_LNK1                  ; and the rest of the row read as part of
    jb .e2                          ; it. D_LNK1 goes over with its three ID
    jne .em0                        ; nibbles, as a unit
    mov [br_wlnk], si               ; copy the marker and ID without width
    call br_cput                    ; the marker, so dropping them with the
    mov al, [es:si+1]               ; other markers below 0x20 made cells
    call br_cput                    ; of the link's own text the index. A LINK
    mov al, [es:si+2]
    call br_cput
    mov al, [es:si+3]
    call br_cput
    add si, 3                       ; br_tmeas measured
    jmp .e2
.em0:
    cmp al, D_LNK0
    jne .emx
    mov word [br_wlnk], 0FFFFh
    call br_cput
    jmp .e2
.emx:
    cmp al, D_IMG                   ; A PICTURE (1.8): copied whole, its COLS
    jne .emm                        ; counted, laid out and its reach kept
    call br_twimg
    jc .edone                       ; it does not fit what is left
    jmp .e2
.emm:
    cmp al, D_COL                   ; a colour goes over, and the fragment's
    jne .emm2                       ; state follows it (1.8)
    call br_twcol
    jmp .e2
.emm2:
    cmp al, D_CMETA                 ; every other marker carries no cell width
    jne .e2                         ; and is dropped - a cell hint with the
    inc si                          ; payload byte after it
    jmp .e2
.etxt:
    cmp dx, cx
    jae .edone
    call br_cput
    inc dx
.e2:
    pop ax
    inc si
    jmp .emit
.edone:
    pop ax
.skiptail:
    cmp si, ax                      ; step over the space we broke at
    jae .out
    push ax
    mov al, [es:si]
    cmp al, ' '
    pop ax
    jne .out
    inc si
.out:
    mov cx, dx
    xor ax, ax
    mov al, [br_tpad]
    add cx, ax
    pop dx
    pop bx
    pop ax
    ret

; --- br_twimg - br_twrap's emit at a picture: SI at its D_IMG, DX placed, CX
; the budget. CF=1 it does not fit (SI kept); else the record and its alt
; text copied (br_cput), DX += COLS, the picture laid out (br_imglaid: 4.2
; fetches it) and the rows it reaches folded into [br_creach]. SI at the
; record's last byte (the caller's `inc si` steps past). AX clobbered
br_twimg:
    push bx
    push cx
    mov bx, si
    inc si
    call far [br_tb_r_imgdec]
    jc .no
    mov al, [br_icols]
    xor ah, ah
    add ax, dx
    cmp ax, cx
    jbe .fits
    or dx, dx                       ; alone in the fragment, it goes anyway
    jnz .no                         ; (br_twrap's scan placed it)
.fits:
    mov dx, ax
    mov al, [br_ialen]
    xor ah, ah
    add si, ax                      ; SI = past the record
    mov cx, si
    sub cx, bx                      ; CX = its bytes
    mov [br_tpst], bx               ; (its start and bytes, for br_tpset)
    mov [br_tpln], cx
    xchg si, bx                     ; SI = its D_IMG, BX = past it
.cp:
    mov al, [es:si]
    call br_cput
    inc si
    loop .cp
    dec si                          ; (the caller's inc)
    call far [br_tb_r_imglaid]
    call far [br_tb_r_imgreach]                ; AL above, AH below
    call br_tpset                   ; TOP-ALIGNED: its rows are the cell's own
    jc .be                          ; next lines, text beside them (71.34.5)
    cmp al, [br_creach]
    jbe .ab
    mov [br_creach], al
.ab:
    cmp ah, [br_creach + 1]
    jbe .be
    mov [br_creach + 1], ah
.be:
    clc
    jmp short .out
.no:
    mov si, bx
    stc
.out:
    pop cx
    pop bx
    ret

; --- br_trule - the +---+---+ edge ------------------------------------------
br_trule:
    cmp byte [br_tbrd], 0
    jne .drawn
    ret                             ; border=0, IE3's way: no rule at all
.drawn:
    push ax
    push bx
    push cx
    push dx
    call br_cstart
    mov al, BR_BX
    call br_cput
    xor cx, cx
.col:
    cmp cl, [br_tn]
    jae .end
    mov bx, cx
    mov dl, [bx+br_tw]
    inc dl                          ; the leading space belongs to the cell
    xor dh, dh
.bar:
    or dx, dx
    jz .plus
    mov al, BR_BH
    call br_cput
    dec dx
    jmp .bar
.plus:
    mov al, BR_BX
    call br_cput
    inc cx
    jmp .col
.end:
    call br_cend
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; the composed-text arena - bytes appended past D_END in the document claim
; -----------------------------------------------------------------------------
br_cstart:
    push ax
    push dx
    mov word [br_creach], 0         ; no picture on it yet (1.8)
    mov byte [br_cface], 0          ; ...nor the face (71.35)
    mov ax, [br_comp]
    mov [br_cline], ax
    mov dx, [br_comp_hi]
    mov [br_cline_hi], dx
    pop dx
    pop ax
    ret

br_cput:
    push bx
    push cx
    push dx                         ; table rules use DX as their run counter
    push di
    push ax                         ; caller's byte survives address arithmetic
    mov di, [br_comp]
    mov ax, di
    mov dx, [br_comp_hi]
    add ax, 1
    adc dx, 0
    call far [br_tb_r_docensure]
    jc .limited
    mov ax, [br_comp]
    mov dx, [br_comp_hi]
    call far [br_tb_r_docmap]
    pop ax
    mov [es:di], al
    inc word [br_comp]
    adc word [br_comp_hi], 0
    mov es, [br_docseg]             ; REGROW may move the source document too;
                                    ; table cursors are logical offsets in it
    jmp short .out
.limited:
    pop ax
    mov byte [br_trunc], 1
.out:
    pop di
    pop dx
    pop cx
    pop bx
    ret

; --- br_cend - turn the composed bytes into a line entry ---------------------
; Writes the record itself rather than going through br_emitline: a composed
; line owes nothing to [br_lstart], [br_lcen] or [br_lind], and borrowing that
; routine would mean saving and restoring three pieces of the prose walk's
; state around every table row.
br_cend:
    push ax
    push bx
    push di
    push es
    mov ax, [br_cline]
    mov dx, [br_cline_hi]
    mov bx, [br_comp]
    mov cx, [br_comp_hi]
    cmp cx, dx
    ja .nonempty
    jb .out
    cmp bx, ax
    jbe .out
.nonempty:
    push cx                         ; THE ROWS ITS PICTURES REACH ABOVE (1.8):
    mov [br_cofs], ax               ; LN_X = +d, the span its start, as
    mov [br_cofs + 2], dx           ; br_emitline writes them
    mov cl, [br_creach]
.above:
    or cl, cl
    jz .text
    mov al, cl
    mov di, br_cofs
    call far [br_tb_r_emitirow]
    jc .abort
    dec cl
    jmp .above
.abort:
    pop cx
    jmp .out
.text:
    pop cx
    mov ax, [br_cofs]
    mov dx, [br_cofs + 2]
    call far [br_tb_r_lslot]                   ; reserve the slot before writing its record
    jc .out
    mov [br_lrecbuf+LN_OFS], ax
    mov [br_lrecbuf+LN_OFS+2], dx
    mov [br_lrecbuf+LN_END], bx
    mov [br_lrecbuf+LN_END+2], cx
    xor al, al
    cmp byte [br_cface], 0          ; THE FACE ON IT (71.35): tall, its
    je .flat                        ; base the cell, and a row below - but a
    cmp word [br_creach], 0         ; row a picture reaches stays in the cell
    jne .flat
    mov al, 80h
    mov byte [br_creach + 1], 1
.flat:
    mov [br_lrecbuf+LN_X], al
    mov byte [br_lrecbuf+LN_FL], 0
    mov ax, [br_nlines]
    call far [br_tb_r_lwrite]
    jc .out
    inc word [br_nlines]
    mov [br_cofs], bx               ; ...AND BELOW: LN_X = -d, the span its
    mov [br_cofs + 2], cx           ; end
    xor cl, cl
.below:
    cmp cl, [br_creach + 1]
    jae .out
    inc cl
    mov al, cl
    neg al
    mov di, br_cofs
    call far [br_tb_r_emitirow]
    jnc .below
.out:
    pop es
    pop di
    pop bx
    pop ax
    ret

br_cdrop:
    push ax
    push dx
    mov ax, [br_cline]
    mov [br_comp], ax
    mov dx, [br_cline_hi]
    mov [br_comp_hi], dx
    pop dx
    pop ax
    ret

; --- br_creopen - re-open the link whose D_LNK1 sits at ES:AX ----------------
; The four bytes are COPIED from the document rather than re-encoded: the
; index is already in the payload's 16..31 form there, so this needs neither
; the shift nor the knowledge of that encoding, and it cannot disagree with
; what br_anchor wrote.
br_creopen:
    push ax
    push bx
    push cx
    mov bx, ax
    mov cx, 4                       ; the marker and its three ID nibbles
.l:
    mov al, [es:bx]
    call br_cput
    inc bx
    loop .l
    pop cx
    pop bx
    pop ax
    ret

; --- br_tskip - SI past this table's matching D_TAB0 ------------------------
br_tskip:
    push ax
    push cx
    xor cx, cx
.loop:
    cmp si, [br_doclen]
    jae .out
    mov al, [es:si]
    inc si
    cmp al, D_CMETA
    jne .t1
    inc si                          ; a cell hint's payload is not a marker
    jmp .loop
.t1:
    cmp al, D_TAB1
    jne .t0
    inc cx
    jmp .loop
.t0:
    cmp al, D_TAB0
    jne .loop
    jcxz .out
    dec cx
    jmp .loop
.out:
    pop cx
    pop ax
    ret



; --- br_ccol - the composer's record, into the arena: AL = the value --------
br_ccol:
    push ax
    cmp al, 35
    jne .nf
    mov byte [br_cface], 1          ; the face on this line: it is tall
.nf:
    push ax
    mov al, D_COL
    call br_cput
    pop ax
    call far [br_tb_r_colenc]
    call br_cput
    mov al, ah
    call br_cput
    pop ax
    ret


; br_tcabs - a record at a cell fragment's head: into the cell's state.
; BX = the cell * 2, ES:SI at its D_COL, AX = the cell's end; SI past it
br_tcabs:
    push ax
    push si
    add si, 3
    cmp si, ax
    pop si
    ja .cut
    inc si
    cmp byte [es:si], D_IMGDIG + 5  ; a size (71.35): the cell's
    jne .colour
    mov al, [es:si + 1]
    sub al, D_IMGDIG + 4
    cmp al, 2
    jae .colour
    mov [bx+br_tsz], al
    jmp short .bad
.colour:
    call far [br_tb_r_coldec]
    jc .bad
    cmp al, COL_BGDEF
    jae .bg
    mov [bx+br_tclr], al
    jmp short .bad
.bg:
    sub al, COL_BGDEF
    mov [bx+br_tclr+1], al
.bad:
    add si, 2
    pop ax
    ret
.cut:
    mov si, ax
    pop ax
    ret

; br_tcpeek - a row starts: the records a cell opens with are its state
; before its first inset is written. BX = the cell * 2
br_tcpeek:
    push ax
    push si
    mov si, [bx+br_tcur]
    mov ax, [bx+br_ce]
.l:
    cmp si, ax
    jae .o
    cmp byte [es:si], D_COL
    jne .o
    call br_tcabs
    jmp .l
.o:
    mov [bx+br_tcur], si
    pop si
    pop ax
    ret

; br_tcbgon - CX = a cell: its ground before the inset that leads it
br_tcbgon:
    push ax
    push bx
    mov bx, cx
    shl bx, 1
    mov al, [bx+br_tclr+1]
    or al, al
    jz .o
    add al, COL_BGDEF
    call br_ccol
.o:
    pop bx
    pop ax
    ret

; br_tcolon - a fragment opens: the cell's ground and ink. BX = cell * 2
br_tcolon:
    push ax
    mov al, [bx+br_tsz]             ; the column's size (71.35) first
    mov [br_wsz], al
    or al, al
    jz .sz
    mov al, 35
    call br_ccol
.sz:
    mov ax, [bx+br_tclr]
    mov [br_wfg], al
    mov [br_wbg], ah
    or ah, ah
    jz .f
    mov al, ah
    add al, COL_BGDEF
    call br_ccol
    mov al, [br_wfg]
.f:
    or al, al
    jz .o
    call br_ccol
.o:
    pop ax
    ret

; br_tcoloff - the fragment closes: the state kept, the row put back
br_tcoloff:
    push ax
    mov al, [br_wsz]                ; the size kept, the row back in the cell
    mov [bx+br_tsz], al
    or al, al
    jz .sz
    mov al, 34
    call br_ccol
.sz:
    mov al, [br_wfg]
    mov ah, [br_wbg]
    mov [bx+br_tclr], ax
    or ah, ah
    jz .f
    push ax
    mov al, COL_BGDEF
    call br_ccol
    pop ax
.f:
    or al, al
    jz .o
    mov al, COL_FGDEF
    call br_ccol
.o:
    pop ax
    ret

; br_twcol - br_twrap's emit at a record: ES:SI at its D_COL. Copied, and
; the fragment's state follows it; SI at its last byte
br_twcol:
    push ax
    inc si
    cmp byte [es:si], D_IMGDIG + 5  ; A SIZE (71.35): 34 the cell, 35 the
    jne .colour                     ; face - copied, and the fragment's
    mov al, [es:si + 1]             ; size follows it
    sub al, D_IMGDIG + 4
    cmp al, 2
    jae .colour
    mov [br_wsz], al
    add al, 34
    call br_ccol
    jmp short .bad
.colour:
    call far [br_tb_r_coldec]
    jc .bad
    cmp al, COL_BGDEF
    jae .bg
    mov [br_wfg], al
    jmp short .put
.bg:
    push ax
    sub al, COL_BGDEF
    mov [br_wbg], al
    pop ax
.put:
    call br_ccol
.bad:
    inc si
    pop ax
    ret

; --- br_tbar - AL = a column's edge: a line, or a space with no border ------
br_tbar:
    mov al, BR_BV
    cmp byte [br_tbrd], 0
    jne .ret
    mov al, ' '
.ret:
    ret

; =============================================================================
; WIDTHS (71.34.4)
; =============================================================================

; br_ttarget - AX = the table's width in cells, 0 none: pixels / 8, or the
; percent of the band. All else kept
br_ttarget:
    push dx
    xor ax, ax
    mov al, [br_twd]
    or al, al
    jz .out
    test al, 80h
    jz .pc
    and al, 3Fh
    shl al, 1                       ; units of two cells
    jmp short .cap
.pc:
    sub al, 20h
    shl ax, 1                       ; the percent
    mul word [br_cols]              ; DX:AX = percent * the band
    push bx
    mov bx, 100
    div bx
    pop bx
.cap:
    cmp ax, [br_cols]
    jbe .out
    mov ax, [br_cols]
.out:
    pop dx
    ret

; br_tcwidth - a cell's width byte AL: its column wants at least that many
; cells (a percent is of the table's width, else of the band). All kept
br_tcwidth:
    push ax
    push bx
    push dx
    cmp byte [br_tspan], 1
    jne .out                        ; a spanning cell's is the spans' business
    mov bl, [br_tcol]
    cmp bl, BR_TCOLS
    jae .out
    xor bh, bh
    test al, 80h
    jz .pc
    and al, 3Fh
    shl al, 1                       ; units of two cells
    jmp short .set
.pc:
    sub al, 20h
    xor ah, ah
    shl ax, 1
    push ax
    call br_ttarget
    or ax, ax
    jnz .base
    mov ax, [br_cols]
.base:
    pop dx
    mul dx
    mov dx, 100
    push bx
    mov bx, dx
    xor dx, dx
    div bx
    pop bx
    cmp ax, 255
    jbe .set
    mov al, 255
.set:
    push cx
    mov cl, bl                      ; this column has a width of its own:
    mov ah, 1                       ; br_tgrow gives it none of the rest
    shl ah, cl
    or [br_tfix], ah
    pop cx
    cmp al, [bx + br_tw]
    jbe .out
    mov [bx + br_tw], al
.out:
    pop dx
    pop bx
    pop ax
    ret

; br_tgrow - br_tfit's last step: a table narrower than its own width grows
; to it, a cell at a time round the columns WITHOUT a width of their own -
; IE3's way: a width="30%" column keeps its 30 - or round all of them when
; every column has one. All kept
br_tgrow:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, [br_ttin]
    or ax, ax
    jz .out
    mov cl, [br_tn]
    xor ch, ch
    jcxz .out
    xor bx, bx                      ; BX = the columns' sum
    xor di, di
.sum:
    mov al, [di + br_tw]
    xor ah, ah
    add bx, ax
    inc di
    cmp di, cx
    jb .sum
    mov dl, 1                       ; DL = a column's bit
    shl dl, cl
    dec dl                          ; ...every column's
    mov dh, [br_tfix]
    not dh
    and dh, dl                      ; DH = the columns that may grow
    jnz .go
    mov dh, dl                      ; all have widths: all of them
.go:
    xor di, di
.g:
    cmp bx, [br_ttin]
    jae .out
    push cx
    mov cx, di
    mov al, 1
    shl al, cl
    pop cx
    test al, dh
    jz .n
    cmp byte [di + br_tw], 255
    je .n
    inc byte [di + br_tw]
    inc bx
.n:
    inc di
    cmp di, cx
    jb .g
    xor di, di
    jmp .g
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; br_isbrk - AL = a marker: CF=1 one a cell's text breaks its line at
br_isbrk:
    cmp al, D_BR
    je .y
    cmp al, D_PARA
    je .y
    cmp al, D_HEAD
    je .y
    cmp al, D_LI
    je .y
    cmp al, D_HR
    je .y
    cmp al, D_TAB1
    je .y
    cmp al, D_ROW
    je .y
    cmp al, D_CELL
    je .y
    clc
    ret
.y:
    stc
    ret

; =============================================================================
; A PICTURE BESIDE TEXT (71.34.5): a TOP-aligned picture in a cell - a left
; float's is one - keeps no rows of its own below the line. Its column puts
; the next slice on each of the row's following lines instead (a copy of its
; record behind D_CMETA, C0h + the slice), so the text in the next cell runs
; down beside the whole picture rather than below it.
; =============================================================================

; br_tpset - br_twimg placed the record at [br_tpst], [br_tpln] bytes, AL/AH
; its reach: CF=1 it is a top-aligned picture whose slices this column owes
br_tpset:
    or al, al
    jnz .no                         ; rows above: not top-aligned
    or ah, ah
    jz .no                          ; one row: nothing owed
    push bx
    push dx
    mov bx, [br_tccol]
    mov dx, [br_tpst]               ; the record
    mov [bx + br_tpic], dx
    mov dx, [br_tpln]
    mov [bx + br_tplen], dx
    cmp ah, 0FEh - 0C0h
    jbe .r
    mov ah, 0FEh - 0C0h
.r:
    mov al, 1
    mov [bx + br_tpk], ax           ; next slice 1, AH of them
    mov al, [br_icols]
    xor ah, ah
    mov [bx + br_tpcols], ax
    pop dx
    pop bx
    stc
    ret
.no:
    clc
    ret

; br_tpslice - the cell at BX (column * 2) is spent: its picture's next slice,
; if one is owed. DX += its cells
br_tpslice:
    push ax
    push cx
    push si
    mov ax, [bx + br_tpk]
    or ah, ah
    jz .out
    dec ah
    push ax
    mov al, D_CMETA
    call br_cput
    pop ax
    push ax
    add al, 0C0h
    call br_cput
    pop ax
    inc al
    mov [bx + br_tpk], ax
    mov si, [bx + br_tpic]
    mov cx, [bx + br_tplen]
    jcxz .out                       ; (never a 64K copy)
.cp:
    mov al, [es:si]
    call br_cput
    inc si
    loop .cp
    add dx, [bx + br_tpcols]
    mov byte [br_tadvanced], 1      ; a line of slices is a line
.out:
    pop si
    pop cx
    pop ax
    ret
