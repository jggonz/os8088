; =============================================================================
; os8088 - apps/waverack/wrface.inc - WaveRack's faces (SPEC.md 123.11)
;
; THREE LAYOUTS, chosen at every full paint from the content box the window
; was given (wf_setup):
;
;   FULL     608 x 400  a VGA's 480 lines: the logo strip, the LCD, LEVEL over
;                       INPUT GAIN, the keys, the waveform, its overview, the
;                       edit row, the format row, VOLUME and the status bar
;   MID      608 x 272  EGA's 350 and Hercules' 348: FULL without the logo
;                       strip and the grille, everything shorter
;   COMPACT  624 x 132  the CGA's 200: the LCD and the meter side by side, one
;                       row of keys, the waveform, and the footer as two rows
;
; THE LAYOUTS ARE DATA, made by tools/os88wrlay.py into build/wrart.inc: a
; record of coordinates (WL_*), an op list of static parts (WO_*), an element
; list, and the keys' rects, pictures and captions. The generator MEASURES
; them (--selfcheck, in the make rule): nothing overlaps, nothing leaves its
; box, every caption fits. Nothing here writes a coordinate of its own.
;
; TWO THEMES, one ROLE table each (wrart_tcol, wrart_tbw): colour, which needs
; a display of more than one bit, and black and white, which is every
; display's.
;
; THE STATIC PARTS are drawn by wf_deco at a full paint and never again.
; EVERYTHING THAT CHANGES is an ELEMENT: a proc that works out a 16-bit KEY
; for the whole of what it would draw, and draws only when that key differs
; from the one its slot in wf_keys remembers. The paint forgets every key and
; runs the same elements, so a full paint and an incremental one are one code
; path (CDRack's identity, SPEC.md 119.6.4). A field the clip cut is
; forgotten too, and drawn again by the next pass (wf_ekdone).
; =============================================================================

; --- a digit set ---------------------------------------------------------------
WDD_TAB     equ 0                   ; twelve pictures: '0'-'9', ' ', '-'
WDD_COL     equ 2                   ; the colon
WDD_DOT     equ 4                   ; the decimal point
WDD_GAP     equ 6                   ; pixels between characters
WDD_W       equ 8                   ; a digit's cell width and height
WDD_H       equ 10
WDD_TIMEW   equ 12                  ; 'MM:SS.cc', its whole width

%macro WDDSET 2                     ; name, NAME
wd_ds_%1:   dw wrart_%1_tab, wrart_%1_col, wrart_%1_dot, WRART_%2_GAP
            dw WRART_%2_W, WRART_%2_H, WRART_%2_TIMEW
%endmacro

; =============================================================================
; SETUP
; =============================================================================

; -----------------------------------------------------------------------------
; wf_setup - at a full paint: the display, the theme, the layout, the origin
; in:  BX = the window.  clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wf_setup:
    push bx
    call OSAPI_WM_DISPLAY           ; DH = bits a pixel
    pop bx
    mov byte [wr_bpp1], 0
    mov ax, wrart_tcol
    cmp dh, 1
    ja .colour
    mov byte [wr_bpp1], 1           ; one bit: black and white whatever was
    jmp short .bw                   ; chosen (View > Theme is grey)
.colour:
    cmp byte [wr_theme], 0
    je .th
.bw:
    mov ax, wrart_tbw
.th:
    mov [wd_th], ax
    call OSAPI_WM_GEOM              ; CX = content width, DX = height
    mov [wr_cw], cx
    mov [wr_ch], dx
    mov si, wrart_l_full            ; the largest that fits, unless the user
    cmp byte [wr_want], 0           ; asked for the compact one
    jne .cmp
    cmp cx, WRL_FULL_W
    jb .cmp
    cmp dx, WRL_FULL_H
    jae .lay
    mov si, wrart_l_mid
    cmp dx, WRL_MID_H
    jae .lay
.cmp:
    mov si, wrart_l_cmp
.lay:
    mov [wd_lp], si
    mov di, wr_drops                ; the drop-downs' items, this layout's
    add si, WL_DRIT                 ; spelling ("Microphone" or "Mic")
    mov cx, WR_NDROP
.dr:
    mov ax, [si]
    mov [di+OS88UI_DR_ITEMS], ax
    add si, 2
    add di, OS88UI_DR_SIZE
    loop .dr
    call wd_origin
    jmp we_geom                     ; the waveform's boxes, and a new width's
                                    ; view (wrenv.inc)

; =============================================================================
; THE ELEMENT CACHE
; =============================================================================

; wf_ek - AX = the key this element would draw. CF = 1: the window shows it
; already. CF = 0: draw, then wf_ekdone. Keeps every register
wf_ek:
    push si
    mov si, [wf_slot]
    cmp [si], ax
    pop si
    je .same
    mov [wf_kpend], ax
    mov byte [wd_cutf], 0
    clc
    ret
.same:
    stc
    ret

; wf_ekdone - the window shows the pending key now - unless the clip cut it,
; when it is forgotten and the next pass draws it again. Keeps every register
wf_ekdone:
    push ax
    push si
    mov ax, [wf_kpend]
    cmp byte [wd_cutf], 0
    je .keep
    mov ax, 0xFFFF
.keep:
    mov si, [wf_slot]
    mov [si], ax
    pop si
    pop ax
    ret

; wf_old - AX = what the slot remembers (FFFF = forgotten). Keeps the rest
wf_old:
    push si
    mov si, [wf_slot]
    mov ax, [si]
    pop si
    ret

; wf_forget - every key and every button's flags owed: the next pass draws all
wf_forget:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov ax, 0xFFFF
    mov di, wf_keys
    mov cx, WF_NKEYS
    cld
    rep stosw
    mov di, wr_bflags
    mov cx, WR_NB
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wf_update - walk the layout's elements, then the buttons
; in:  BX = the window; the gfx lock held, the clip armed, wd_origin current;
;      DL = 1 draw every button (the paint), 0 only the changed
; clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wf_update:
    push bx
    push dx
    call wr_menulive                ; the menus' greying and names (no draw)
    mov si, [wd_lp]
    mov si, [si+WL_ELEMS]
    mov di, wf_keys
.next:
    mov ax, [si]
    or ax, ax
    jz .btns
    mov cx, [si+2]
    mov [wf_slot], di
    push si
    push di
    call ax
    pop di
    pop si
    add si, 4
    inc di
    inc di
    jmp short .next
.btns:
    pop dx
    pop bx
    jmp wr_regrey

; =============================================================================
; THE STATIC PARTS - the layout's op list (tools/os88wrlay.py's OPS)
; =============================================================================

; -----------------------------------------------------------------------------
; wf_deco - every static op of this layout, in order. clobbers all but BX
; Each op is 14 bytes: db op, ra, rb, arg; dw a, b, c, d, p - a..d being AX,
; BX, CX, DX as crdraw's primitives take them, ra/rb roles, p a string or a
; picture. A full paint may write a pixel twice; the ops are only ever run by
; one.
; -----------------------------------------------------------------------------
wf_deco:
    push bx
    mov si, [wd_lp]
    mov si, [si+WL_DECO]
.op:
    mov bl, [si]
    cmp bl, WO_END
    je .out
    push si
    call wf_op1
    pop si
    add si, WO_SIZE
    jmp short .op
.out:
    pop bx
    ret

; wf_op1 - one op at SI. clobbers AX, BX, CX, DX, SI, DI
wf_op1:
    mov di, si
    mov ax, [di+4]
    mov bx, [di+6]
    mov cx, [di+8]
    mov dx, [di+10]
    mov si, [di+1]
    and si, 0xFF                    ; SI = ra; DI = the op record
    cmp byte [di], WO_FILL
    jne .n1
    jmp wd_rect
.n1:
    cmp byte [di], WO_RAISED
    je wf_bevel
    cmp byte [di], WO_SUNKEN
    je wf_bevel
    cmp byte [di], WO_GROOVE
    jne .n4
    mov si, R_SH
    call wd_frame
    inc ax
    inc bx
    inc cx
    inc dx
    mov si, R_GLINT
    jmp wd_frame
.n4:
    cmp byte [di], WO_HL
    jne .n5
    jmp wd_hl
.n5:
    cmp byte [di], WO_VL
    jne .n6
    jmp wd_vl
.n6:
    cmp byte [di], WO_CAPS
    jne .n7
    mov dl, [di+1]
    mov dh, [di+2]
    mov cl, [di+3]
    mov [wd_cgap], cl
    mov si, [di+12]
    call wd_capsat
    mov byte [wd_cgap], 0
    ret
.n7:
    cmp byte [di], WO_TEXT
    jne .n8
    mov cx, ax
    mov dx, bx
    mov al, [di+1]
    mov ah, [di+2]
    mov si, [di+12]
    jmp wd_text
.n8:
    cmp byte [di], WO_PIC
    jne .n9
    mov dl, [di+1]
    mov dh, [di+2]
    mov si, [di+12]
    jmp wd_picat
.n9:
    cmp byte [di], WO_GRILLE
    jne .n10
    jmp wf_grille
.n10:
    cmp byte [di], WO_CELLS
    jne .n11
    xor ax, ax                      ; the meter's 24 cells, all dark
    mov cx, 24
    xor dl, dl
    jmp wf_cells
.n11:
    mov bx, [di+6]                  ; WO_VGROOVE: AX = x, BX..DX the rows
    mov si, R_SH
    call wd_vl
    inc ax
    mov si, R_GLINT
    jmp wd_vl

; wf_bevel - a panel AX,BX - CX,DX filled with role SI; RAISED lights its top
; and left edges and shades the others, SUNKEN the reverse. DI = the op
wf_bevel:
    call wd_rect
    push di
    mov si, R_HI                    ; the top and left
    cmp byte [di], WO_RAISED
    je .tl
    mov si, R_SH
.tl:
    push cx
    mov cx, ax
    call wd_vl                      ; left: AX, BX..DX
    pop cx
    push dx
    mov dx, bx
    call wd_rect                    ; top: AX..CX, BX
    pop dx
    pop di
    mov si, R_SH                    ; the bottom and right
    cmp byte [di], WO_RAISED
    je .br
    mov si, R_HI
.br:
    push bx
    mov bx, dx
    call wd_rect                    ; bottom: AX..CX, DX
    pop bx
    mov ax, cx
    jmp wd_vl                       ; right: CX, BX..DX

; wf_grille - the speaker grille: a band of horizontal rules every [di+3]
; rows, AX,BX - CX,DX; ra the ink, rb the paper. DI = the op
wf_grille:
    push ax
    push bx
    sub cx, ax
    inc cx
    sub dx, bx
    inc dx
    call wd_bnew
    xor bx, bx
.row:
    push cx
    xor ax, ax
    dec cx
    call wd_brun
    pop cx
    mov al, [di+3]
    xor ah, ah
    add bx, ax
    cmp bx, dx
    jb .row
    pop bx
    pop ax
    mov dl, [di+1]
    mov dh, [di+2]
    jmp wd_bout

; =============================================================================
; BAND HELPERS
; =============================================================================

; wd_setrun - OR the pixels AX..CX (inclusive) into the row at ES:DI.
; keeps every register
wd_setrun:
    push ax
    push bx
    push cx
    push dx
    mov dx, cx
.px:
    cmp ax, dx
    jg .done
    mov bx, ax
    mov cl, 3
    shr bx, cl
    mov cl, al
    and cl, 7
    mov ch, 0x80
    shr ch, cl
    or [es:di+bx], ch
    inc ax
    jmp short .px
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wd_brun - the same into the composing band's row BX. keeps every register
wd_brun:
    push di
    push es
    push ax
    push dx
    push ds
    pop es
    mov ax, bx
    mul word [wd_bst]
    mov di, ax
    add di, wd_band
    pop dx
    pop ax
    call wd_setrun
    pop es
    pop di
    ret

; wd_bbox - into the band: a box at AX, BX, CX wide, DX tall - solid when SI
; is non-zero, else its outline. keeps every register
wd_bbox:
    push ax
    push bx
    push cx
    push dx
    add cx, ax
    dec cx                          ; CX = the right column
    add dx, bx                      ; DX = one past the bottom row
    or si, si
    jz .outl
.solid:
    call wd_brun
    inc bx
    cmp bx, dx
    jb .solid
    jmp short .out
.outl:
    call wd_brun                    ; the top...
    push bx
    mov bx, dx
    dec bx
    call wd_brun                    ; ...the bottom...
    pop bx
.side:
    push cx
    mov cx, ax
    call wd_brun                    ; ...and the two sides, a row at a time
    pop cx
    push ax
    mov ax, cx
    call wd_brun
    pop ax
    inc bx
    cmp bx, dx
    jb .side
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wf_pad - copy the NUL string at SI into wr_tbuf, padded with spaces to CX
; characters (cut there when longer), NUL-ended. clobbers AX, SI
wf_pad:
    push cx
    push di
    mov di, wr_tbuf
.c:
    jcxz .z
    lodsb
    or al, al
    jz .fill
    mov [di], al
    inc di
    dec cx
    jmp short .c
.fill:
    mov byte [di], ' '
    inc di
    loop .fill
.z:
    mov byte [di], 0
    pop di
    pop cx
    ret

; wf_padc - the same, the text CENTRED in the CX characters (left and cut
; when it is as long or longer). clobbers AX, SI
wf_padc:
    push bx
    push cx
    push di
    xor bx, bx
.len:
    cmp byte [si+bx], 0
    je .lok
    inc bx
    jmp short .len
.lok:
    cmp bx, cx
    jb .ctr
    pop di
    pop cx
    pop bx
    jmp wf_pad
.ctr:
    mov di, wr_tbuf
    mov ax, cx                      ; half the slack in front
    sub ax, bx
    shr ax, 1
    sub cx, ax                      ; CX = what is left after it
    push cx
    mov cx, ax
    jcxz .nolead
.lead:
    mov byte [di], ' '
    inc di
    loop .lead
.nolead:
    pop cx
.c:
    jcxz .z
    lodsb
    or al, al
    jz .fill
    mov [di], al
    inc di
    dec cx
    jmp short .c
.fill:
    mov byte [di], ' '
    inc di
    loop .fill
.z:
    mov byte [di], 0
    pop di
    pop cx
    pop bx
    ret

; =============================================================================
; THE ELEMENTS - each: in CX = its parameter, [wf_slot] = its key; clobbers
; AX, BX, CX, DX, SI, DI
; =============================================================================

; --- the filename on the LCD -----------------------------------------------------
wf_e_fname:
    mov ax, [wo_gen]                ; the document's name, or NO FILE
    call wf_ek
    jc .out
    mov si, wc_name0                ; a take's name while it runs
    call wc_active
    jnc .none
    mov si, wr_s_nofile
    cmp byte [wo_doc], 0
    je .none
    mov si, wo_name
.none:
    mov di, [wd_lp]
    mov cx, [di+WL_FNCH]
    call wf_pad
    mov cx, [di+WL_FN]
    mov dx, [di+WL_FN+2]
    mov si, wr_tbuf
    mov ax, (R_LCD << 8) | R_LCDTXT
    call wd_text
    call wf_ekdone
.out:
    ret

; --- the big time: the position, MM:SS.cc ----------------------------------------
wf_e_time:
    mov ax, [we_cgen]               ; the cursor's time (00:00.00 with none) -
    call wf_ek                      ; the playhead's while the transport
    jc .out                         ; holds a play (SPEC.md 123.6)
    mov ax, [wo_cur]
    mov dx, [wo_cur+2]
    cmp byte [wt_state], WT_IDLE    ; a play's playhead, or a take's length
    je .t                           ; (wc_meter puts it in wt_ph)
    mov ax, [wt_ph]
    mov dx, [wt_ph+2]
.t:
    mov di, wr_tbuf2
    mov cx, [wc_rate]               ; a take's frames at ITS rate (the
    call wc_active                  ; document is empty while it runs)
    jnc .tc
    call wo_time
    jmp short .td
.tc:
    call wo_timec
.td:
    mov si, wr_tbuf2
    mov di, [wd_lp]
    mov di, [di+WL_DSET]
    mov cx, [di+WDD_TIMEW]
    mov dx, [di+WDD_H]
    call wd_bnew
    xor ax, ax
    xor bx, bx
    call wd_bdigs
    mov si, [wd_lp]
    mov ax, [si+WL_TIM]
    mov bx, [si+WL_TIM+2]
    mov dx, (R_LCD << 8) | R_DIGIT
    call wd_bout
    call wf_ekdone
.out:
    ret

; --- '/ MM:SS.cc', the length -------------------------------------------------------
wf_e_total:
    mov ax, [wo_gen]                ; '/ MM:SS.cc', the document's length
    call wf_ek
    jc .out
    mov word [wr_tbuf2], '/ '
    mov ax, [wo_frames]
    mov dx, [wo_frames+2]
    mov di, wr_tbuf2 + 2
    call wo_time
    mov di, [wd_lp]
    mov cx, [di+WL_TOT]
    mov dx, [di+WL_TOT+2]
    mov si, wr_tbuf2
    mov ax, (R_LCD << 8) | R_LCDTXT
    call wd_text
    call wf_ekdone
.out:
    ret

; --- the format line: the document's, or with none the next take's -----------------
wf_e_fmt:
    mov al, [wr_rate]               ; the next take's, keyed by its rate...
    xor ah, ah
    cmp byte [wo_doc], 0
    je .key
    mov ax, [wo_gen]                ; ...or the document's
    or ax, 0x8000
.key:
    call wf_ek
    jc .out
    cmp byte [wo_doc], 0
    je .take
    call wo_fmttext                 ; '22,050 Hz 16-bit Stereo'
    mov si, wr_tbuf2
    jmp short .s
.take:
    mov si, ax                      ; (AH = 0: the key was the rate alone)
    shl si, 1
    mov si, [wr_ratefmt + si]
.s:
    mov di, [wd_lp]
    mov cx, [di+WL_FMTCH]
    cmp word [di+WL_FMTCAPS], 0
    jne .caps
    call wf_pad
    mov cx, [di+WL_FMT]
    mov dx, [di+WL_FMT+2]
    mov si, wr_tbuf
    mov ax, (R_LCD << 8) | R_LCDTXT
    call wd_text
    jmp short .done
.caps:                              ; COMPACT: the small caps, a field
    mov ax, cx                      ; FMTCH letters wide
    shl ax, 1
    add cx, ax
    shl cx, 1                       ; CX = 6 a letter
    mov ax, [di+WL_FMT]
    mov bx, [di+WL_FMT+2]
    mov dx, (R_LCD << 8) | R_LCDTXT
    call wd_fcaps
.done:
    call wf_ekdone
.out:
    ret

; --- the meter's caption: LEVEL, LEVEL REC, LEVEL PLAY ---------------------------
wf_e_mcap:
    mov al, [wr_mwho]
    xor ah, ah
    call wf_ek
    jc .out
    mov di, [wd_lp]
    mov si, [di+WL_MCAPS]
    shl ax, 1
    add si, ax
    mov si, [si]
    mov cx, [di+WL_MCAPW]
    mov ax, [di+WL_MCAP]
    mov bx, [di+WL_MCAP+2]
    mov dx, (R_BG << 8) | R_LABEL
    call wd_fcaps
    call wf_ekdone
.out:
    ret

; --- the meter's cells: only those whose state changed ----------------------
; Its key is the lit length and the peak-hold cell (SPEC.md 123.8); the cells
; are two 24-bit masks, what the glass shows and what it should, and each run
; of changed cells of one state is one wf_cells - so a bar that grows by two
; and a hold that steps are two or three calls, never the bar.
wf_e_meter:
    mov al, [wr_lit]
    mov ah, [wr_hold]
    call wf_ek
    jc .out
    call wf_mmask                   ; [wf_mn] = the cells lit now
    call wf_old                     ; ...and what the glass shows
    cmp ax, 0xFFFF
    jne .diff
    mov word [wf_mc], 0xFFFF        ; forgotten: every cell
    mov word [wf_mc+2], 0x00FF
    jmp short .walk
.diff:
    push word [wf_mn]
    push word [wf_mn+2]
    call wf_mmask                   ; [wf_mn] = the old cells
    pop dx
    pop bx                          ; DX:BX = the new
    mov ax, [wf_mn]                 ; changed = old ^ new
    xor ax, bx
    mov [wf_mc], ax
    mov ax, [wf_mn+2]
    xor ax, dx
    mov [wf_mc+2], ax
    mov [wf_mn], bx                 ; [wf_mn] = the new again
    mov [wf_mn+2], dx
.walk:
    xor si, si                      ; the runs
.cell:
    cmp si, 24
    jae .done
    mov bx, wf_mc
    call wf_tbit
    jz .nx
    mov bx, wf_mn                   ; changed: its state now
    call wf_tbit
    mov dl, 0
    jz .st
    inc dl
.st:
    mov di, si
.ext:
    inc di                          ; while changed and of the same state
    cmp di, 24
    jae .run
    push si
    mov si, di
    mov bx, wf_mc
    call wf_tbit
    jz .endr
    mov bx, wf_mn
    call wf_tbit
    mov dh, 0
    jz .s2
    inc dh
.s2:
    pop si
    cmp dh, dl
    je .ext
    jmp short .run
.endr:
    pop si
.run:
    push di
    mov ax, si
    mov cx, di
    call wf_cells
    pop si
    jmp short .cell
.nx:
    inc si
    jmp short .cell
.done:
    call wf_ekdone
.out:
    ret

; wf_mmask - AL = the lit cells, AH = the hold (the cell + 1, 0 none) ->
; [wf_mn] = the 24-bit mask of the cells lit: [0, lit), and the hold's when
; it stands above them. keeps everything but AX
wf_mmask:
    push bx
    push cx
    push dx
    xor bx, bx
    xor dx, dx
    mov cl, al
    xor ch, ch
    jcxz .h
.s:
    stc
    rcl bx, 1
    rcl dx, 1
    loop .s
.h:
    cmp ah, al
    jbe .out
    mov cl, ah
    dec cl
    push ax
    mov ax, 1
    cmp cl, 16
    jb .lo
    sub cl, 16
    shl ax, cl
    or dx, ax
    jmp short .hd
.lo:
    shl ax, cl
    or bx, ax
.hd:
    pop ax
.out:
    mov [wf_mn], bx
    mov [wf_mn+2], dx
    pop dx
    pop cx
    pop bx
    ret

; wf_tbit - ZF = 0 when bit SI (0..23) of the dword at BX is set. keeps all
wf_tbit:
    push ax
    push cx
    mov cx, si
    and cl, 15
    mov ax, 1
    shl ax, cl
    cmp si, 16
    jb .lo
    test [bx+2], ax
    jmp short .o
.lo:
    test [bx], ax
.o:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wf_cells - the meter's cells AX..CX-1, lit (DL = 1, by zone: 18 green, 4
; yellow, 2 red) or dark (DL = 0: the off role; an OUTLINE in black and white)
; Each zone's run is one band and one blit. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wf_cells:
    or dl, dl
    jnz .lit
    mov si, R_LEDOFF
    mov dl, 1                       ; solid in colour...
    cmp word [wd_th], wrart_tbw
    jne .run
    xor dl, dl                      ; ...outlined in black and white
.run:
    jmp wf_cellrun
.lit:
    mov di, wf_zones
.zone:
    push ax
    push cx
    cmp ax, [di]                    ; [max(AX, start), min(CX, end))
    jae .a
    mov ax, [di]
.a:
    cmp cx, [di+2]
    jbe .b
    mov cx, [di+2]
.b:
    cmp ax, cx
    jae .skip
    mov si, [di+4]
    mov dl, 1
    push di
    call wf_cellrun
    pop di
.skip:
    pop cx
    pop ax
    add di, 6
    cmp di, wf_zones + 18
    jb .zone
    ret

wf_zones:   dw 0, 18, R_LEDG, 18, 22, R_LEDY, 22, 24, R_LEDR

; wf_cellrun - cells AX..CX-1 as one band in role SI; DL = 1 solid, 0 outline.
; clobbers AX, BX, CX, DX, SI, DI
wf_cellrun:
    cmp ax, cx
    jae .out
    push si
    push dx
    mov di, [wd_lp]
    push ax                         ; the band: (n - 1) pitches and a cell
    sub cx, ax
    push cx
    dec cx
    mov ax, [di+WL_CELLP]
    mul cx
    add ax, [di+WL_CELLW]
    mov cx, ax
    mov dx, [di+WL_CELLH]
    call wd_bnew
    pop cx                          ; CX = how many
    pop ax                          ; AX = the first
    pop dx
    push ax
    xor si, si
    mov si, dx
    and si, 0xFF                    ; SI = solid
    xor ax, ax                      ; the first cell's x in the band
.cell:
    push cx
    mov cx, [di+WL_CELLW]
    mov dx, [di+WL_CELLH]
    xor bx, bx
    call wd_bbox
    add ax, [di+WL_CELLP]
    pop cx
    loop .cell
    pop ax                          ; the first cell, on the glass
    mul word [di+WL_CELLP]
    add ax, [di+WL_CELL]
    mov bx, [di+WL_CELL+2]
    pop si
    mov dx, si
    mov dh, R_LCD
    call wd_bout
.out:
    ret

; --- the PEAK lamp -------------------------------------------------------------------
wf_e_peak:
    mov al, [wr_peak]
    xor ah, ah
    call wf_ek
    jc .out
    mov di, [wd_lp]
    push ax
    mov ax, [di+WL_PEAK]
    mov bx, [di+WL_PEAK+2]
    mov cx, [di+WL_PEAK+4]
    mov dx, [di+WL_PEAK+6]
    pop si
    or si, si
    jz .dark
    mov si, R_LEDR
    call wd_rect
    jmp short .done
.dark:
    mov si, R_LEDOFF                ; dark: the off role - outlined round the
    cmp word [wd_th], wrart_tbw     ; glass in black and white
    jne .fill
    mov si, R_LCD
    call wd_rect
    mov si, R_LEDOFF
    call wd_frame
    jmp short .done
.fill:
    call wd_rect
.done:
    call wf_ekdone
.out:
    ret

; --- INPUT GAIN and VOLUME ---------------------------------------------------------------
wf_e_gain:
    mov al, [wr_gain]
    xor ah, ah
    call wf_ek
    jc .out
    mov si, [wd_lp]
    add si, WL_GSL
    mov cx, WR_GAINMAX
    xor dl, dl
    call wf_slider
    call wf_ekdone
.out:
    ret

wf_e_vol:
    mov al, [wr_vol]
    xor ah, ah
    call wf_ek
    jc .out
    mov si, [wd_lp]
    add si, WL_VSL
    mov cx, 100
    mov dl, 1
    call wf_slider
    call wf_ekdone
.out:
    ret

; -----------------------------------------------------------------------------
; wf_slider - one slider, ONE band and ONE blit: its groove, its ticks and its
; thumb's outline in the slider ink, the faceplate as paper - so the thumb's
; face is the faceplate and nothing under it is written twice.
; in:  SI = its rect (content), AX = the value, CX = the top value,
;      DL = 0 INPUT GAIN's (a thin groove, a tick a step), 1 VOLUME's (a full
;      tube left of the thumb, an empty one right of it)
; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wf_slider:
    push si
    push dx
    push cx
    push ax
    mov cx, [si+4]                  ; the band: the rect
    sub cx, [si]
    inc cx
    mov dx, [si+6]
    sub dx, [si+2]
    inc dx
    call wd_bnew
    pop ax
    pop cx
    call wf_thumbx                  ; AX = the thumb's left, in the band
    mov di, ax                      ; DI = it
    pop dx
    push dx
    mov bx, [wd_bh]
    shr bx, 1                       ; BX = the groove's middle row
    or dl, dl
    jnz .tube
    dec bx                          ; INPUT GAIN: three rows either side of
    call wf_grv                     ; the thumb...
    inc bx
    call wf_grv
    inc bx
    call wf_grv
    add bx, 2                       ; ...and a tick a step, below
    mov ax, bx
    inc ax
    cmp ax, [wd_bh]
    jae .thumb
    push cx
    mov cx, WR_GAINMAX
.tick:
    push cx
    push bx
    mov ax, cx                      ; x = step * travel / steps + half a thumb
    mov bx, [wd_bw]
    sub bx, WF_THUMB
    mul bx
    mov bx, WR_GAINMAX
    div bx
    add ax, WF_THUMB / 2
    pop bx
    cmp ax, di                      ; not under the thumb
    jb .tk
    mov cx, di
    add cx, WF_THUMB - 1
    cmp ax, cx
    jbe .tn
.tk:
    mov cx, ax
    call wd_brun
    inc bx
    call wd_brun
    dec bx
.tn:
    pop cx
    dec cx
    jns .tick
    pop cx
    jmp short .thumb
.tube:                              ; VOLUME: rows mid-2..mid+2 full to the
    sub bx, 2                       ; left, the tube's two walls and its end
    mov si, 5                       ; to the right
.trow:
    xor ax, ax
    mov cx, di
    dec cx
    call wd_brun
    cmp si, 5
    je .wall
    cmp si, 1
    jne .tnext
.wall:
    mov ax, di
    add ax, WF_THUMB
    mov cx, [wd_bw]
    dec cx
    call wd_brun
.tnext:
    mov ax, [wd_bw]                 ; the end cap
    dec ax
    mov cx, ax
    cmp di, ax
    jae .nocap
    call wd_brun
.nocap:
    inc bx
    dec si
    jnz .trow
.thumb:
    mov ax, di                      ; the thumb: an outline the slider's
    xor bx, bx                      ; height, and a grip down its middle
    mov cx, WF_THUMB
    mov dx, [wd_bh]
    xor si, si
    call wd_bbox
    add ax, WF_THUMB / 2
    mov cx, ax
    mov bx, 3
    mov dx, [wd_bh]
    sub dx, 3
.grip:
    cmp bx, dx
    jae .put
    call wd_brun
    inc bx
    jmp short .grip
.put:
    pop dx
    pop si
    mov ax, [si]
    mov bx, [si+2]
    mov dx, (R_BG << 8) | R_SLINK
    jmp wd_bout

; wf_grv - the groove's row BX, either side of the thumb at DI. keeps all
wf_grv:
    push ax
    push cx
    xor ax, ax
    mov cx, di
    dec cx
    call wd_brun
    mov ax, di
    add ax, WF_THUMB
    mov cx, [wd_bw]
    dec cx
    call wd_brun
    pop cx
    pop ax
    ret

; wf_thumbx - AX = a value of CX -> AX = the thumb's left in a band [wd_bw]
; wide. keeps everything else
wf_thumbx:
    push bx
    push dx
    mov bx, [wd_bw]
    sub bx, WF_THUMB                ; the travel
    mul bx
    div cx
    pop dx
    pop bx
    ret

; --- the waveform: its ruler, its grid and its centre line ------------------------
; With no document there is no time to rule: the ticks divide the width in
; fiftieths, the majors in tenths, and the grid stands on the majors. The two
; are composed in the waveform band (a heap claim, SPEC.md 123.5) and put down
; in three calls - the ruler, and the lanes above and below the centre line -
; with the centre line in the waveform's ink between them, so no pixel is
; written twice. Without the claim the lanes are two fills.
wf_e_wave:
    mov ax, [we_wgen]
    call wf_ek
    jc .out
    cmp byte [wo_doc], 0            ; a document: wrenv.inc's band
    je .empty
    call we_draw
    jmp short .done
.empty:
    call wf_wave0
.done:
    call wf_ekdone
    mov word [wf_keys + 2 * WRE_CUR], 0xFFFF  ; the line went with it: its
.out:                               ; element has nothing to put back
    ret

; --- the selection in the lanes and the overview: only where it changed --------
; Its key is the selection's generation. A full draw of the lanes or of the
; overview drew the selection as it stood and recorded it; this redraws the
; columns between the edges the glass shows and the edges it should - a
; drag is a few blits a move, never the waveform (wrenv.inc, we_selpass).
wf_e_wsel:
    mov ax, [wo_sgen]
    call wf_ek
    jc .out
    cmp byte [wo_doc], 0
    je .done
    cmp word [wr_bseg], 0
    je .done
    call we_selpass
.done:
    call wf_ekdone
.out:
    ret

; --- the line: the playhead while the transport holds a play, else the cursor
; (none beside a selection). Moving it puts the column it left back from the
; band - one run, one blit - and draws the line again: two calls a tick. Its
; key is its column + 1, 0 when it is out of view; the waveform's own draw
; forgets it (wf_e_wave), and so does the selection's where it redrew under
; it (we_lput).
wf_e_cur:
    call we_linecol                 ; AX = the column, CF = 1 none
    jnc .in
    mov ax, -1
.in:
    inc ax
    call wf_ek
    jc .out
    push ax
    call wf_old                     ; the line it left
    or ax, ax
    jz .new
    cmp ax, 0xFFFF
    je .new
    dec ax
    mov cx, ax
    mov di, ax
    call we_bank
    call we_putcols
.new:
    pop ax
    or ax, ax
    jz .done
    dec ax
    add ax, [we_bx]
    mov bx, [we_by]
    add bx, [we_rulh]
    mov dx, bx
    add dx, [we_lh]
    dec dx
    mov si, R_PLAYHEAD
    cmp word [wd_th], wrart_tbw     ; black and white: white on the inverted
    jne .line                       ; span is nothing - black there
    push ax
    push cx
    push dx
    sub ax, [we_bx]
    push ax
    call we_lanesel
    pop ax
    cmp byte [we_kon], 0
    je .r
    cmp ax, [we_k0]
    jb .r
    cmp ax, [we_k1]
    ja .r
    mov si, R_LCD
.r:
    pop dx
    pop cx
    pop ax
.line:
    call wd_vl
.done:
    call wf_ekdone
.out:
    ret

wf_wave0:
    mov di, [wd_lp]
    mov ax, [di+WL_WAVE+6]          ; the lanes: their height and centre
    sub ax, [di+WL_WAVE+2]
    inc ax
    sub ax, [di+WL_RULH]
    mov [wf_lh], ax
    shr ax, 1
    mov [wf_lc], ax
    mov ax, [di+WL_WAVE+4]
    sub ax, [di+WL_WAVE]
    inc ax
    mov [wf_ww], ax
    cmp word [wr_bseg], 0
    jne .band
    mov ax, [di+WL_WAVE]            ; no band: the ground in two fills and
    mov bx, [di+WL_WAVE+2]          ; the line between
    mov cx, [di+WL_WAVE+4]
    mov dx, bx
    add dx, [di+WL_RULH]
    add dx, [wf_lc]
    dec dx
    mov si, R_LCD
    call wd_rect
    add dx, 2
    mov bx, dx
    mov dx, [di+WL_WAVE+6]
    call wd_rect
    jmp .line
.band:
    call wf_wbank                   ; [wf_wk] = the x phase, [wf_wst] = stride
    ; --- the ruler -------------------------------------------------------------
    mov dx, [di+WL_RULH]
    call wf_wclear
    xor cx, cx                      ; tick i = 0..50
.rt:
    call wf_tickx                   ; AX = its x
    mov bx, [di+WL_RULH]
    sub bx, 3                       ; a minor: three rows, a major six
    mov si, cx
    push ax
    push dx
    mov ax, si
    xor dx, dx
    mov si, 5
    div si
    or dx, dx
    pop dx
    pop ax
    jnz .rminor
    sub bx, 3
.rminor:
    push cx
    mov cx, ax
.rrow:
    call wf_wpix
    inc bx
    cmp bx, [di+WL_RULH]
    jb .rrow
    pop cx
    inc cx
    cmp cx, 50
    jbe .rt
    xor si, si                      ; out: rows 0..RULH-1
    mov bx, [di+WL_WAVE+2]
    mov dx, [di+WL_RULH]
    mov ax, (R_LCD << 8) | R_RULER
    call wf_wblit
    ; --- the lanes -------------------------------------------------------------
    mov dx, [wf_lh]
    call wf_wclear
    mov cx, 5                       ; the grid's columns: the inner majors,
.gc:                                ; dotted every other row
    call wf_tickx
    xor bx, bx
.gcr:
    cmp bx, [wf_lc]
    je .gcn
    push cx
    mov cx, ax
    call wf_wpix
    pop cx
.gcn:
    add bx, 2                       ; (every fourth in black and white, where
    cmp word [wd_th], wrart_tbw     ; a dot is white and not a dim grey)
    jne .gcs
    add bx, 2
.gcs:
    cmp bx, [wf_lh]
    jb .gcr
    add cx, 5
    cmp cx, 50
    jb .gc
    mov bx, [wf_lc]                 ; ...and its rows, a quarter and three
    shr bx, 1                       ; quarters down, dotted every fourth pixel
    call wf_wdots
    add bx, [wf_lc]
    call wf_wdots
    xor si, si                      ; above the centre line...
    mov bx, [di+WL_WAVE+2]
    add bx, [di+WL_RULH]
    mov dx, [wf_lc]
    mov ax, (R_LCD << 8) | R_GRID
    call wf_wblit
    mov ax, [wf_lc]                 ; ...and below it
    inc ax
    mul word [wf_wst]
    mov si, ax
    mov bx, [di+WL_WAVE+2]
    add bx, [di+WL_RULH]
    add bx, [wf_lc]
    inc bx
    mov dx, [wf_lh]
    sub dx, [wf_lc]
    dec dx
    mov ax, (R_LCD << 8) | R_GRID
    call wf_wblit
.line:
    mov ax, [di+WL_WAVE]            ; the centre line
    mov cx, [di+WL_WAVE+4]
    mov bx, [di+WL_WAVE+2]
    add bx, [di+WL_RULH]
    add bx, [wf_lc]
    mov si, R_WAVE
    jmp wd_hl

; wf_tickx - CX = a tick 0..50 -> AX = its x in the band (the phase added).
; keeps everything else
wf_tickx:
    push bx
    push dx
    mov ax, [wf_ww]
    dec ax
    mul cx
    mov bx, 50
    div bx
    add ax, [wf_wk]
    pop dx
    pop bx
    ret

; wf_wbank - the band's x phase and stride for this pass: a BLIT1 at an x off
; the byte grid starts at the byte column HOLDING x (SPEC.md 5.4.2.8), so
; every pixel is composed (x & 7) further in. keeps every register but flags
wf_wbank:
    push ax
    mov ax, [wd_ox]
    add ax, [di+WL_WAVE]
    and ax, 7
    mov [wf_wk], ax
    add ax, [wf_ww]
    add ax, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [wf_wst], ax
    pop ax
    ret

; wf_wclear - the band's first DX rows cleared. keeps every register
wf_wclear:
    push ax
    push cx
    push di
    push es
    mov es, [wr_bseg]
    mov ax, [wf_wst]
    push dx
    mul dx
    pop dx
    mov cx, ax
    shr cx, 1
    xor ax, ax
    xor di, di
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; wf_wpix - the pixel CX on the band's row BX. keeps every register
wf_wpix:
    push ax
    push di
    push es
    push dx
    mov es, [wr_bseg]
    mov ax, bx
    mul word [wf_wst]
    mov di, ax
    mov ax, cx
    call wd_setrun
    pop dx
    pop es
    pop di
    pop ax
    ret

; wf_wdots - every fourth pixel of the band's row BX. keeps every register
wf_wdots:
    push cx
    mov cx, [wf_wk]
.d:
    call wf_wpix
    add cx, 4
    push ax
    mov ax, [wf_ww]
    add ax, [wf_wk]
    cmp cx, ax
    pop ax
    jb .d
    pop cx
    ret

; -----------------------------------------------------------------------------
; wf_wblit - DX rows of the waveform band from offset SI onto the glass at the
; waveform's x, content row BX; AL = the ink role, AH = the paper role.
; DI = the layout. keeps DI
; -----------------------------------------------------------------------------
wf_wblit:
    or dx, dx
    jz .none
    push di
    push bp
    push es
    push si
    mov si, ax                      ; the pen, through the theme
    and si, 0xFF
    call wd_rv
    mov cx, si
    mov al, ah
    xor ah, ah
    mov si, ax
    call wd_rv
    mov ax, si
    mov ah, al
    mov al, cl
    and ax, 0x0F0F
    call OSAPI_GFX_BLIT1_PEN
    pop si
    mov ax, [di+WL_WAVE]
    add ax, [wd_ox]
    add bx, [wd_oy]
    mov cx, [wf_ww]
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
    WR_NCALL
    pop es
    pop bp
    pop di
.none:
    ret

; --- the overview: the whole document, which is nothing yet -----------------------
wf_e_ovw:
    mov ax, [we_ogen]
    call wf_ek
    jc .out
    cmp byte [wo_doc], 0            ; a document: wrenv.inc's band
    je .empty
    call we_ovdraw
    jmp short .done
.empty:
    mov di, [wd_lp]
    mov ax, [di+WL_OVW]
    mov bx, [di+WL_OVW+2]
    mov cx, [di+WL_OVW+4]
    mov dx, [di+WL_OVW+6]
    add dx, bx
    shr dx, 1                       ; DX = the centre row
    push dx
    dec dx
    mov si, R_LCD
    call wd_rect                    ; above it
    pop bx
    mov si, R_WAVE
    call wd_hl                      ; it
    inc bx
    mov dx, [di+WL_OVW+6]
    mov si, R_LCD
    call wd_rect                    ; below it
.done:
    call wf_ekdone
.out:
    ret

; --- the selection field: 'Selection:' and the range, empty with none -----------
wf_e_sel:
    mov ax, [wo_sgen]               ; 'Selection:  01.00 - 02.50' (SPEC.md
    call wf_ek                      ; 123.9): the prefix and the value
    jc .out
    mov di, [wd_lp]
    mov si, [di+WL_SELPFX]
    mov di, wr_tbuf2
    call wr_put
    mov bx, di                      ; where the value starts
    call we_seltext
    mov byte [di], 0
    mov si, [wd_lp]
    mov cx, [si+WL_SELCH]
    mov ax, di
    sub ax, wr_tbuf2
    cmp ax, cx
    jbe .fit
    call we_squeeze                 ; too long: no spaces round the dash...
    mov byte [di], 0
    mov ax, di
    sub ax, wr_tbuf2
    cmp ax, cx
    jbe .fit
    mov si, bx                      ; ...and then no prefix
    jmp short .pad
.fit:
    mov si, wr_tbuf2
.pad:
    mov di, [wd_lp]
    call wf_pad
    mov cx, [di+WL_SELT]
    mov dx, [di+WL_SELT+2]
    mov si, wr_tbuf
    mov ax, (R_FIELD << 8) | R_FIELDTXT
    call wd_text
    call wf_ekdone
.out:
    ret

; --- the status bar: wr_stbuf, centred; its key is the message's generation -----
wf_e_stat:
    mov ax, [wr_stkey]
    call wf_ek
    jc .out
    mov di, [wd_lp]
    mov si, wr_stbuf
    mov cx, [di+WL_STCH]
    call wf_padc
    mov cx, [di+WL_STT]
    mov dx, [di+WL_STT+2]
    mov si, wr_tbuf
    mov ax, (R_STAT << 8) | R_STATTXT
    call wd_text
    call wf_ekdone
.out:
    ret

; --- a drop-down (CX = 0..3): os88ui's control (SPEC.md 13.14), drawn as it ---
; stands - its box, and its list when that is open
wf_e_drop:
    mov si, cx
    mov ax, OS88UI_DR_SIZE
    mul cx
    mov bx, ax
    add bx, wr_drops                ; BX = the record
    call wr_dropdis                 ; DI = its flags (OS88UI_DIS or 0)
    mov ax, [bx+OS88UI_DR_SEL]
    mov ah, [bx+OS88UI_DR_OPEN]
    shl ah, 1
    or ax, di                       ; the key: the pick, open, greyed
    call wf_ek
    jc .out
    push ax
    mov ax, [bx+OS88UI_DR_RECT]     ; the cut flag, from its screen rect
    push bx
    mov cx, [bx+OS88UI_DR_RECT+4]
    mov dx, [bx+OS88UI_DR_RECT+6]
    mov bx, [bx+OS88UI_DR_RECT+2]
    call wd_cut
    pop bx
    pop ax
    call os88ui_drop
    call wf_ekdone
.out:
    ret

; --- the Loop check box: the box picture and its label --------------------------
wf_e_loop:
    mov al, [wr_loop]
    xor ah, ah
    call wf_ek
    jc .out
    mov di, [wd_lp]
    mov si, [di+WL_UNCHK]
    or al, al
    jz .box
    mov si, [di+WL_CHK]
.box:
    mov ax, [di+WL_LBOX]
    mov bx, [di+WL_LBOX+2]
    mov dx, (R_BG << 8) | R_LABEL
    call wd_picat
    mov cx, [di+WL_LTXT]
    mov dx, [di+WL_LTXT+2]
    mov si, [di+WL_LLBL]
    mov ax, (R_BG << 8) | R_LABEL
    call wd_text
    call wf_ekdone
.out:
    ret

; =============================================================================
; THE BUTTONS (apps/os88ui.inc's record, WaveRack's painter: SPEC.md 13.8.10)
; =============================================================================

; -----------------------------------------------------------------------------
; wf_bpaint - the OWN painter: AX = the index, BX = the rect (screen,
;             inclusive), DI = the flags, DOWN and DIS resolved. Keeps every
;             register
;
; A frame, and the whole inside as ONE band - the picture and the caption
; composed in it, the face its paper - so a key is two calls, pressed,
; greyed or not. Record's dot is red on the colour face: the inside is then
; TWO bands that tile it, the picture's part in the red ink and the caption's
; in the key's, so still no pixel is drawn twice.
; -----------------------------------------------------------------------------
wf_bpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp word [bx], WR_PARK          ; not on this layout
    jne .on
    jmp .out
.on:
    mov bp, ax                      ; BP = the index
    mov ax, [bx]                    ; the rect, content-relative
    sub ax, [wd_ox]
    mov cx, [bx+4]
    sub cx, [wd_ox]
    mov dx, [bx+6]
    sub dx, [wd_oy]
    mov bx, [bx+2]
    sub bx, [wd_oy]
    mov si, R_BTNEDGE               ; the roles: up, or down (pressed or
    mov word [wf_bink], (R_BTN << 8) | R_BTNTXT ; latched)
    mov byte [wf_bdis], 0
    test di, OS88UI_DIS
    jnz .dis
    test di, OS88UI_DOWN
    jz .up
    mov si, R_BTNONE
    mov word [wf_bink], (R_BTNON << 8) | R_BTNONT
    jmp short .up
.dis:
    cmp word [wd_th], wrart_tbw     ; greyed in black and white: the frame is
    jne .up                         ; DOTTED and goes in the key's one band
    mov byte [wf_bdis], 1           ; (SPEC.md 123.11)
    jmp short .in
.up:
    call wd_frame
.in:
    inc ax                          ; the inside
    inc bx
    dec cx
    dec dx
    mov [wf_bx], ax
    mov [wf_by], bx
    sub cx, ax
    inc cx
    sub dx, bx
    inc dx
    mov [wf_bbw], cx
    mov [wf_bbh], dx
    test di, OS88UI_DIS             ; greyed: the dim ink on the colour face,
    jz .live                        ; half the ink in black and white
    cmp word [wd_th], wrart_tbw
    je .live
    mov byte [wf_bink], R_BTNDIS
.live:
    mov bx, [wd_lp]                 ; the picture and the caption
    mov si, [bx+WL_ICONS]
    add si, bp
    add si, bp
    mov ax, [si]
    mov [wf_bpic], ax
    mov si, [bx+WL_CAPS]
    add si, bp
    add si, bp
    mov ax, [si]
    mov [wf_bcap], ax
    call wf_bplace                  ; where each goes: wf_px/py, wf_cx/cy, and
                                    ; [wf_split], the split for a red dot
    mov word [wf_split], 0
    cmp bp, B_REC                   ; Record, live, on the colour face: its
    jne .one                        ; dot red
    test di, OS88UI_DIS | OS88UI_DOWN
    jnz .one
    cmp word [wd_th], wrart_tbw
    je .one
    mov bx, [wd_lp]
    cmp word [bx+WL_SIDE], 0
    jne .sidesplit
    mov ax, [wf_py]                 ; stacked: the rows above the caption
    add ax, 2
    add ax, [wf_pich]
    mov [wf_split], ax
    mov ax, [wf_bbw]                ; part 1: the picture's rows, red
    mov [wf_pw1], ax
    mov ax, [wf_split]
    mov [wf_ph1], ax
    call wf_bpart1
    mov ax, [wf_bbw]                ; part 2: the caption's rows
    mov [wf_pw1], ax
    mov ax, [wf_bbh]
    sub ax, [wf_split]
    mov [wf_ph1], ax
    mov ax, [wf_split]
    sub [wf_cy], ax
    xor ax, ax
    mov [wf_px2], ax
    mov ax, [wf_split]
    mov [wf_py2], ax
    call wf_bpart2
    jmp .out
.sidesplit:
    mov ax, [wf_cx]                 ; side by side: the columns before the
    dec ax                          ; caption
    mov [wf_split], ax
    mov [wf_pw1], ax
    mov ax, [wf_bbh]
    mov [wf_ph1], ax
    call wf_bpart1
    mov ax, [wf_bbw]
    sub ax, [wf_split]
    mov [wf_pw1], ax
    mov ax, [wf_bbh]
    mov [wf_ph1], ax
    mov ax, [wf_split]
    sub [wf_cx], ax
    mov [wf_px2], ax
    mov word [wf_py2], 0
    call wf_bpart2
    jmp short .out
.one:
    cmp byte [wf_bdis], 0
    jne .bwdis
    mov cx, [wf_bbw]                ; the whole inside, one band
    mov dx, [wf_bbh]
    call wd_bnew
    call wf_bpic1
    call wf_bcap1
.put:
    mov ax, [wf_bx]
    mov bx, [wf_by]
    mov dx, [wf_bink]
    call wd_bout
    jmp .out
.bwdis:
    ; GREYED ON ONE BIT (SPEC.md 123.11): the 5 x 7 caps have one-pixel
    ; strokes and a checkerboard takes half of them, so the CAPTION stays
    ; solid; the PICTURE is dithered and the frame is dotted. One band, the
    ; frame included - one call
    mov cx, [wf_bbw]
    add cx, 2
    mov dx, [wf_bbh]
    add dx, 2
    call wd_bnew
    inc word [wf_px]                ; (the places were the inside's)
    inc word [wf_py]
    inc word [wf_cx]
    inc word [wf_cy]
    call wf_bpic1
    call wd_bdith                   ; the picture: half its ink
    call wf_bcap1
    call wf_bdots                   ; the frame: every other pixel
    mov ax, [wf_bx]
    dec ax
    mov bx, [wf_by]
    dec bx
    mov dx, [wf_bink]
    call wd_bout
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wf_bpart1 - the red part: a band [wf_pw1] x [wf_ph1] holding the picture,
; at the inside's origin
wf_bpart1:
    mov cx, [wf_pw1]
    mov dx, [wf_ph1]
    call wd_bnew
    call wf_bpic1
    mov ax, [wf_bx]
    mov bx, [wf_by]
    mov dx, [wf_bink]
    mov dl, R_REC
    jmp wd_bout

; wf_bpart2 - the caption's part: a band [wf_pw1] x [wf_ph1] at the inside
; plus wf_px2, wf_py2
wf_bpart2:
    mov cx, [wf_pw1]
    mov dx, [wf_ph1]
    call wd_bnew
    call wf_bcap1
    mov ax, [wf_bx]
    add ax, [wf_px2]
    mov bx, [wf_by]
    add bx, [wf_py2]
    mov dx, [wf_bink]
    jmp wd_bout

; wf_bpic1 / wf_bcap1 - the picture / caption into the band where wf_bplace
; put it (nothing when there is none). keep every register
wf_bpic1:
    push ax
    push bx
    push si
    mov si, [wf_bpic]
    mov ax, [wf_px]
    mov bx, [wf_py]
    call wd_bpic
    pop si
    pop bx
    pop ax
    ret

wf_bcap1:
    push ax
    push bx
    push si
    mov si, [wf_bcap]
    or si, si
    jz .out
    mov ax, [wf_cx]
    mov bx, [wf_cy]
    call wd_bcaps
.out:
    pop si
    pop bx
    pop ax
    ret

; wf_bdots - a dotted frame round the band: its first and last rows every
; other pixel, its first and last columns every other row. keeps every register
wf_bdots:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, wd_band                 ; the first row
    mov cx, [wd_bst]
.t:
    or byte [si], 0xAA
    inc si
    loop .t
    mov ax, [wd_bh]                 ; the last row
    dec ax
    mul word [wd_bst]
    add ax, wd_band
    mov si, ax
    mov cx, [wd_bst]
.b:
    or byte [si], 0xAA
    inc si
    loop .b
    xor bx, bx                      ; the two columns
.c:
    xor ax, ax
    xor cx, cx
    call wd_brun
    mov ax, [wd_bw]
    dec ax
    mov cx, ax
    call wd_brun
    add bx, 2
    cmp bx, [wd_bh]
    jb .c
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wf_bplace - where the picture and the caption go inside [wf_bbw] x [wf_bbh]:
; stacked and centred (FULL, MID), or side by side and centred as a pair
; (COMPACT, and any key with only one of the two). clobbers AX, BX, CX, DX, SI
wf_bplace:
    xor cx, cx                      ; CX, DX = the picture's size
    xor dx, dx
    mov si, [wf_bpic]
    or si, si
    jz .np
    call wd_picw
.np:
    mov [wf_pich], dx
    xor ax, ax                      ; AX = the caption's width
    mov si, [wf_bcap]
    or si, si
    jz .nc
    push cx
    call wd_capsw
    mov ax, cx
    pop cx
.nc:
    mov bx, [wd_lp]
    cmp word [bx+WL_SIDE], 0
    jne .side
    or cx, cx
    jz .side
    or ax, ax
    jz .side
    ; --- stacked: the picture over the caption, 4 rows apart
    mov si, dx
    add si, 4 + 7                   ; SI = the pair's height
    push ax
    mov ax, [wf_bbh]
    sub ax, si
    sar ax, 1
    mov [wf_py], ax
    add ax, dx
    add ax, 4
    mov [wf_cy], ax
    mov ax, [wf_bbw]
    sub ax, cx
    sar ax, 1
    mov [wf_px], ax
    pop cx
    mov ax, [wf_bbw]
    sub ax, cx
    sar ax, 1
    mov [wf_cx], ax
    ret
.side:                              ; side by side, 3 pixels apart
    mov si, cx
    or cx, cx
    jz .s1
    or ax, ax
    jz .s1
    add si, 3
.s1:
    add si, ax                      ; SI = the pair's width
    push ax
    mov ax, [wf_bbw]
    sub ax, si
    sar ax, 1
    mov [wf_px], ax
    add ax, cx
    or cx, cx
    jz .s2
    add ax, 3
.s2:
    mov [wf_cx], ax
    mov ax, [wf_bbh]
    sub ax, dx
    sar ax, 1
    mov [wf_py], ax
    mov ax, [wf_bbh]
    sub ax, 7
    sar ax, 1
    mov [wf_cy], ax
    pop ax
    ret

; =============================================================================
; HIT-TESTS - a press that was not a button's. In: CX, DX content-relative
; =============================================================================

; wf_in - CF = 0 when CX, DX is inside the rect at SI (x1, y1, x2, y2)
wf_in:
    cmp cx, [si]
    jl .no
    cmp cx, [si+4]
    jg .no
    cmp dx, [si+2]
    jl .no
    cmp dx, [si+6]
    jg .no
    clc
    ret
.no:
    stc
    ret

; wf_slval - CX = a content x over the slider at SI, AX = its top value ->
; AX = the value there, rounded and clamped. clobbers CX, DX
wf_slval:
    push bx
    push ax
    sub cx, [si]
    sub cx, WF_THUMB / 2
    jns .pos
    xor cx, cx
.pos:
    mov bx, [si+4]                  ; the travel
    sub bx, [si]
    inc bx
    sub bx, WF_THUMB
    cmp cx, bx
    jbe .in
    mov cx, bx
.in:
    pop ax
    mul cx                          ; x * top / travel, rounded
    push bx
    shr bx, 1
    add ax, bx
    adc dx, 0
    pop bx
    div bx
    pop bx
    ret
