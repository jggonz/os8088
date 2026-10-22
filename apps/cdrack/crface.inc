; =============================================================================
; os8088 - apps/cdrack/crface.inc - CDRack's faces (SPEC.md 119.6.2-119.6.4)
;
; THREE LAYOUTS, one table each (L_* below), chosen at every full paint from
; the content box the window was given (cr_setup):
;
;   FULL     608 x 352  the deck, the transport, the track list, a status bar
;   MID      608 x 232  the same with no list: EGA's and Hercules' 350 lines
;   COMPACT  440 x 84   the mini player, for the CGA's 200 lines - and 440 x
;            (168)      168 with its rows doubled where the pixels are square
;                       enough to want them (C2), which is what Options >
;                       Compact Player gives a VGA
;
; TWO THEMES, one ROLE table each (crdraw.inc): colour, which needs a display
; of more than one bit, and black and white, which is every display's.
;
; THE STATIC PARTS - panels, frames, labels, the logo - are drawn by the
; layout's L_DECO at a full paint and never again. EVERYTHING THAT CHANGES is
; an ELEMENT (L_ELEMS): a proc that works out a KEY - the whole of what it
; would draw, in sixteen bits - and draws only when the key differs from the
; one its slot in cr_keys remembers (PERFORMANCE.md rule 1). The paint
; forgets every key and runs the same elements, so the incremental draw and
; the full paint are one code path and cannot disagree (the identity the
; vga-face method asks for). A field the clip cut is forgotten too, and drawn
; again by the next pass (cr_ekdone).
; =============================================================================

; --- the layout record ---------------------------------------------------------
L_W         equ 0                   ; the content it was cut for
L_H         equ 2
L_DECO      equ 4                   ; the static parts' proc
L_ELEMS     equ 6                   ; the element list: (proc, param) pairs, 0
L_BTNS      equ 8                   ; CR_NB rects, content-relative, inclusive
L_ICONS     equ 10                  ; CR_NB picture pointers
L_CAPS      equ 12                  ; CR_NB caption pointers (0 = none)
L_CHK       equ 14                  ; the check and the empty box at its size
L_UNCHK     equ 16
L_DSET      equ 18                  ; the LCD's digit set (CRD_*)
L_TRKX      equ 20                  ; the track number
L_TRKY      equ 22
L_TIMX      equ 24                  ; the time
L_TIMY      equ 26
L_STX       equ 28                  ; the play state's field
L_STY       equ 30
L_PB        equ 32                  ; the progress bar's INSIDE: x1, y1, x2, y2
L_PBFG      equ 40                  ; ...its roles: done, to go
L_PBBG      equ 42
L_SL        equ 44                  ; the volume slider's inside: x1, y1, x2, y2
L_SBY       equ 52                  ; FULL/MID: the status bar's top
L_LCD       equ 54                  ; COMPACT: the LCD's rect
L_TLBL      equ 62                  ; COMPACT: 'TRACK' x, y
L_VSEP      equ 66                  ; COMPACT: the LCD's divider x, y1, y2
L_TOT       equ 72                  ; COMPACT: '/ 04:18' x, y, and its set
L_TOTSET    equ 76
L_OF        equ 78                  ; COMPACT: '03 OF 12' x, y
L_SPK       equ 82                  ; COMPACT: the speaker x, y, picture
L_BROW      equ 88                  ; COMPACT: the bottom row's y1, y2
L_CMP       equ 92                  ; 1 = a compact layout

; --- a digit set ----------------------------------------------------------------
CRD_TAB     equ 0                   ; twelve pictures: '0'-'9', ' ', '-'
CRD_COL     equ 2                   ; the colon
CRD_GAP     equ 4                   ; pixels between characters
CRD_W       equ 6                   ; a digit's width and height
CRD_H       equ 8
CRD_CW      equ 10                  ; the colon's width

%macro CRDSET 3                     ; name, NAME, gap
cr_ds_%1:   dw cdart_%1_tab, cdart_%1_col, %3
            dw CDART_%2_W, CDART_%2_H, CDART_%2_CW
%endmacro

; --- FULL's (and MID's) fixed geometry ----------------------------------------
CF_W        equ 608
CF_H        equ 352
CM_H        equ 232
CF_LCDX1    equ 216                 ; the LCD
CF_LCDY1    equ 52
CF_LCDX2    equ 595
CF_LCDY2    equ 143
CF_GX       equ 432                 ; the track grid: 6 x 2 cells of 26 x 20
CF_GY       equ 56
CF_GCW      equ 26
CF_GCH      equ 20
CF_LX1      equ 16                  ; the track list's rows: 71 characters
CF_LY0      equ 247                 ; from x 16, 13 rows a track, six of them
CF_LROW     equ 13
CF_LN       equ 6
CF_LCH      equ 71
CF_SBX      equ 584                 ; the list's scroll bar
CF_SBT1     equ 259                 ; its trough
CF_SBT2     equ 312
%if CF_GX + 6 * CF_GCW > CF_LCDX2 - 4
%error "CDRack's track grid runs past its LCD"
%endif
%if CF_LX1 + CF_LCH * 8 != CF_SBX
%error "CDRack's list rows must end where the scroll bar begins"
%endif
%if CF_LY0 + CF_LN * CF_LROW > 327
%error "CDRack's list rows run past their box"
%endif

C1_H        equ 84                  ; COMPACT, a row a pixel (the CGA)
C2_H        equ 168                 ; COMPACT, rows doubled
CC_W        equ 440

; =============================================================================
; SETUP
; =============================================================================

; -----------------------------------------------------------------------------
; cr_setup - at a full paint: the display, the theme, the layout, the origin
; in:  BX = the window.  clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cr_setup:
    push bx
    call OSAPI_WM_DISPLAY           ; DH = bits a pixel (BX = the height)
    pop bx
    mov byte [cr_bpp1], 0
    mov ax, cr_tcol
    cmp dh, 1
    ja .colour
    mov byte [cr_bpp1], 1           ; one bit: black and white whatever was
    jmp short .bw                   ; chosen (the colour item is grey)
.colour:
    cmp byte [cr_theme], 0
    je .th
.bw:
    mov ax, cr_tbw
.th:
    mov [cr_th], ax
    call OSAPI_WM_GEOM              ; CX = content width, DX = height
    mov [cr_cw], cx
    mov [cr_ch], dx
    mov si, cr_lfull                ; the largest that fits, unless the user
    cmp byte [cr_want], 0           ; asked for the compact one
    jne .cmp
    cmp cx, CF_W
    jb .cmp
    cmp dx, CF_H
    jae .lay
    mov si, cr_lmid
    cmp dx, CM_H
    jae .lay
.cmp:
    mov si, cr_lc2
    cmp dx, C2_H
    jae .lay
    mov si, cr_lc1
.lay:
    mov [cr_lp], si
    jmp cr_origin

; =============================================================================
; THE ELEMENT CACHE
; =============================================================================

; cr_ek - AX = the key this element would draw. CF = 1: the window shows it
; already. CF = 0: draw, then cr_ekdone. Keeps every register
cr_ek:
    push si
    mov si, [cr_slot]
    cmp [si], ax
    pop si
    je .same
    mov [cr_kpend], ax
    mov byte [cr_cutf], 0
    clc
    ret
.same:
    stc
    ret

; cr_ekdone - the window shows the pending key now - unless the clip cut it,
; when it is forgotten and the next pass draws it again. Keeps every register
cr_ekdone:
    push ax
    push si
    mov ax, [cr_kpend]
    cmp byte [cr_cutf], 0
    je .keep
    mov ax, 0xFFFF
.keep:
    mov si, [cr_slot]
    mov [si], ax
    pop si
    pop ax
    ret

; cr_forget - every key and every button's flags owed: the next pass draws all
cr_forget:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov ax, 0xFFFF
    mov di, cr_keys
    mov cx, CR_NKEYS
    cld
    rep stosw
    mov di, cr_bflags
    mov cx, CR_NB
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cr_update - walk the layout's elements, then the buttons
; in:  BX = the window; the gfx lock held, the clip armed, cr_origin current;
;      DL = 1 draw every button (the paint), 0 only the changed
; clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cr_update:
    push bx
    push dx
    call cr_menulive                ; the menus' greying and marks (no draw)
    call cr_lfollow                 ; the list follows the selection
    mov si, [cr_lp]
    mov si, [si+L_ELEMS]
    mov di, cr_keys
.next:
    mov ax, [si]
    or ax, ax
    jz .btns
    mov cx, [si+2]
    mov [cr_slot], di
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
    jmp cr_regrey

; =============================================================================
; THE STATIC PARTS
; =============================================================================

; cr_ground - the whole content box in the ground colour (OSAPI_WM_OWNBG:
; nothing else will). clobbers AX, BX, CX, DX, SI
cr_ground:
    xor ax, ax
    xor bx, bx
    mov cx, [cr_cw]
    dec cx
    mov dx, [cr_ch]
    dec dx
    mov si, R_BG
    jmp cr_rect

; cr_panel - a filled panel AX,BX - CX,DX in role SI, ruled round in R_EDGE
cr_panel:
    call cr_rect
    push si
    mov si, R_EDGE
    call cr_frame
    pop si
    ret

; cr_pbframe / cr_slframe - the rule round the progress bar and the slider,
; from the layout's inside rects. clobbers AX, BX, CX, DX, SI
cr_pbframe:
    mov si, [cr_lp]
    add si, L_PB
    mov di, R_LCDDIM
    mov bx, [cr_lp]
    cmp byte [bx+L_CMP], 0
    je cr_rframe
    mov di, R_EDGE
    jmp short cr_rframe
cr_slframe:
    mov si, [cr_lp]
    add si, L_SL
    mov di, R_EDGE
cr_rframe:                          ; SI = an inside rect, DI = the role
    mov ax, [si]
    mov bx, [si+2]
    mov cx, [si+4]
    mov dx, [si+6]
    dec ax
    dec bx
    inc cx
    inc dx
    mov si, di
    jmp cr_frame

; -----------------------------------------------------------------------------
; cr_d_full / cr_d_mid - FULL's and MID's static parts. clobbers all but BX
; -----------------------------------------------------------------------------
cr_d_full:
    push bx
    call cr_d_deck
    mov ax, 4                       ; the list's panel...
    mov bx, 216
    mov cx, 603
    mov dx, 332
    mov si, R_PANEL
    call cr_panel
    mov cx, 16
    mov dx, 221
    mov si, cr_s_ltitle
    mov ax, (R_PANEL << 8) | R_TEXT
    call cr_text
    mov ax, 15                      ; ...its box...
    mov bx, 232
    mov cx, 596
    mov dx, 327
    mov si, R_LIST
    call cr_panel
    mov dx, (R_LIST << 8) | R_LABEL ; ...the column heads...
    mov ax, 48
    mov bx, 236
    mov si, cr_s_htrack
    call cr_capsat
    mov ax, 112
    mov si, cr_s_htitle
    call cr_capsat
    mov ax, 520
    mov si, cr_s_htime
    call cr_capsat
    mov ax, 16
    mov bx, 245
    mov cx, 595
    mov si, R_EDGE
    call cr_hl
    mov bx, CF_LY0                  ; ...and the scroll bar's two arrows
    mov si, cr_s_up
    call cr_d_arrow
    mov bx, CF_LY0 + CF_LN * CF_LROW - 12
    mov si, cr_s_down
    call cr_d_arrow
    mov bx, 336
    jmp short cr_d_sbar

cr_d_mid:
    push bx
    call cr_d_deck
    mov bx, 216
cr_d_sbar:                          ; the status bar, at y = BX
    mov [cr_sby], bx
    mov ax, 4
    mov cx, 603
    mov dx, bx
    add dx, 15
    mov si, R_STAT
    call cr_panel
    mov cx, 552
    mov dx, bx
    add dx, 4
    mov si, cr_s_os
    mov ax, (R_STAT << 8) | R_STATTXT
    call cr_text
    pop bx
    ret

; cr_d_arrow - a scroll bar arrow box at CF_SBX, y = BX; SI = its glyph
cr_d_arrow:
    push si
    mov ax, CF_SBX
    mov cx, CF_SBX + 11
    mov dx, bx
    add dx, 11
    mov si, R_BTN
    call cr_panel
    pop si
    mov cx, CF_SBX + 2
    mov dx, bx
    add dx, 2
    mov ax, (R_BTN << 8) | R_BTNTXT
    jmp cr_text

; cr_d_deck - what FULL and MID share: the header, the deck, the transport
cr_d_deck:
    call cr_ground
    mov ax, 4                       ; the header: the logo and its line
    mov bx, 4
    mov cx, 603
    mov dx, 39
    mov si, R_HDR
    call cr_panel
    mov ax, 14
    mov bx, 12
    mov si, cdart_logo
    mov dx, (R_HDR << 8) | R_LOGO
    call cr_picat
    mov byte [cr_cgap], 5
    mov ax, 152
    mov bx, 20
    mov si, cr_s_tag
    call cr_capsat
    mov byte [cr_cgap], 0
    mov cx, 28                      ; the grille: a band of 1000 columns
    mov dx, 24
    call cr_bnew
    push es
    push ds
    pop es
    mov di, cr_band
    mov ax, [cr_bst]
    mul word [cr_bh]
    mov cx, ax
    mov al, 0x88
    cld
    rep stosb
    pop es
    mov ax, 568
    mov bx, 10
    mov dx, (R_HDR << 8) | R_LABEL
    call cr_bout
    mov ax, 4                       ; the deck
    mov bx, 44
    mov cx, 603
    mov dx, 151
    mov si, R_PANEL
    call cr_panel
    mov ax, 12                      ; the tray...
    mov bx, 52
    mov cx, 207
    mov dx, 143
    mov si, R_EDGE
    call cr_frame
    mov ax, 20                      ; ...its slot...
    mov bx, 60
    mov cx, 199
    mov dx, 91
    mov si, R_SLOT
    call cr_panel
    cmp word [cr_th], cr_tbw
    jne .nolbl
    mov ax, 73                      ; ...whose label is a white card in black
    mov bx, 69                      ; and white
    mov cx, 146
    mov dx, 81
    mov si, R_EDGE
    call cr_frame
.nolbl:
    mov ax, 22                      ; ...and the disc mark
    mov bx, 104
    mov si, cdart_discmark
    mov dx, (R_PANEL << 8) | R_LABEL
    call cr_picat
    mov ax, CF_LCDX1                ; the LCD
    mov bx, CF_LCDY1
    mov cx, CF_LCDX2
    mov dx, CF_LCDY2
    mov si, R_LCD
    call cr_rect
    mov si, R_LCDEDGE
    call cr_frame
    mov ax, 236
    mov bx, 58
    mov si, cr_s_track
    mov dx, (R_LCD << 8) | R_LCDLBL
    call cr_capsat
    mov ax, 284                     ; its divider
    mov bx, 58
    mov dx, 98
    mov si, R_LCDDIM
    call cr_vl
    mov ax, CF_GX                   ; the grid's rules: seven columns...
    mov bx, CF_GY
    mov dx, CF_GY + 2 * CF_GCH
.gv:
    call cr_vl
    add ax, CF_GCW
    cmp ax, CF_GX + 6 * CF_GCW
    jbe .gv
    mov ax, CF_GX                   ; ...and three rows
    mov cx, CF_GX + 6 * CF_GCW
.gh:
    call cr_hl
    add bx, CF_GCH
    cmp bx, CF_GY + 2 * CF_GCH
    jbe .gh
    mov ax, 228                     ; the rule over the progress row
    mov bx, 117
    mov cx, 587
    call cr_hl
    call cr_pbframe
    mov ax, 4                       ; the transport's panel
    mov bx, 156
    mov cx, 603
    mov dx, 211
    mov si, R_PANEL
    call cr_panel
    mov ax, 332
    mov bx, 162
    mov dx, 205
    mov si, R_EDGE
    call cr_vl
    mov ax, 344
    mov bx, 163
    mov si, cdart_speaker
    mov dx, (R_PANEL << 8) | R_LABEL
    call cr_picat
    jmp cr_slframe

; -----------------------------------------------------------------------------
; cr_d_cmp - COMPACT's static parts, from its layout record (C1 or C2)
; -----------------------------------------------------------------------------
cr_d_cmp:
    push bx
    call cr_ground
    mov di, [cr_lp]
    mov ax, [di+L_LCD]
    mov bx, [di+L_LCD+2]
    mov cx, [di+L_LCD+4]
    mov dx, [di+L_LCD+6]
    mov si, R_LCD
    call cr_rect
    mov si, R_LCDEDGE
    call cr_frame
    mov ax, [di+L_TLBL]
    mov bx, [di+L_TLBL+2]
    mov si, cr_s_track
    mov dx, (R_LCD << 8) | R_LCDLBL
    call cr_capsat
    mov ax, [di+L_VSEP]
    mov bx, [di+L_VSEP+2]
    mov dx, [di+L_VSEP+4]
    mov si, R_LCDDIM
    call cr_vl
    call cr_pbframe
    call cr_slframe
    mov di, [cr_lp]
    mov ax, [di+L_SPK]
    mov bx, [di+L_SPK+2]
    mov si, [di+L_SPK+4]
    mov dx, (R_BG << 8) | R_LABEL
    call cr_picat
    mov bx, [di+L_BROW]             ; the bottom row's two dividers
    mov dx, [di+L_BROW+2]
    mov si, R_EDGE
    mov ax, 198
    call cr_vl
    mov ax, 306
    call cr_vl
    pop bx
    ret

; =============================================================================
; THE ELEMENTS - each: in CX = its parameter, [cr_slot] = its key; clobbers
; AX, BX, CX, DX, SI, DI
; =============================================================================

; cr_isaudio - ZF = 1 when the drive holds a disc with audio tracks - which
; it may not be able to PLAY (CPS_NOAUD, QEMU's drive): the face still shows
; the disc, and the buttons, which ask cr_bok, stay grey. clobbers nothing
cr_isaudio:
    cmp byte [cr_state], CPS_AUDIO
    je .out
    cmp byte [cr_state], CPS_NOAUD
.out:
    ret

; --- the track number -----------------------------------------------------------
cr_e_trk:
    mov ax, 0x7F00                  ; '--': no audio disc
    call cr_isaudio
    jne .k
    mov al, [cr_sel]
    xor ah, ah
.k:
    call cr_ek
    jc .out
    mov di, cr_tbuf
    mov word [di], '--'
    cmp ah, 0x7F
    je .s
    call cr_2dig
.s:
    mov byte [cr_tbuf+2], 0
    mov si, [cr_lp]
    mov di, [si+L_DSET]
    mov cx, [di+CRD_W]              ; the field: two digits
    shl cx, 1
    add cx, [di+CRD_GAP]
    mov dx, [di+CRD_H]
    call cr_bnew
    xor ax, ax
    xor bx, bx
    mov si, cr_tbuf
    call cr_bdigs
    mov si, [cr_lp]
    mov ax, [si+L_TRKX]
    mov bx, [si+L_TRKY]
    mov dx, (R_LCD << 8) | R_LCDFG
    call cr_bout
    call cr_ekdone
.out:
    ret

; --- the time: elapsed, or remaining when TIME is on (FULL and MID) -------------
cr_e_time:
    call cr_tval                    ; AX = the value, DL = 1 remaining
    mov cx, ax
    mov ax, 0x7FFF
    call cr_isaudio
    jne .k
    mov ax, cx
    or dl, dl
    jz .k
    or ah, 0x20
.k:
    call cr_ek
    jc .out
    mov di, cr_tbuf
    cmp ax, 0x7FFF
    jne .t
    mov word [di], '--'
    mov word [di+2], ':-'
    mov byte [di+4], '-'
    add di, 5
    jmp short .z
.t:
    mov ax, cx
    call cr_mmss                    ; 'mm:ss '
    dec di
.z:
    mov byte [di], 0
    mov si, [cr_lp]
    mov di, [si+L_DSET]
    mov cx, [di+CRD_W]              ; the field: four digits and the colon
    shl cx, 1
    shl cx, 1
    add cx, [di+CRD_CW]
    mov ax, [di+CRD_GAP]
    shl ax, 1
    shl ax, 1
    add cx, ax
    mov dx, [di+CRD_H]
    call cr_bnew
    xor ax, ax
    xor bx, bx
    mov si, cr_tbuf
    call cr_bdigs
    mov si, [cr_lp]
    mov ax, [si+L_TIMX]
    mov bx, [si+L_TIMY]
    mov dx, (R_LCD << 8) | R_LCDFG
    call cr_bout
    call cr_ekdone
.out:
    ret

; cr_tval - AX = the seconds the big time shows; DL = 1 when that is the
; time REMAINING. The compact face always counts up. clobbers DH
cr_tval:
    xor dl, dl
    xor ax, ax
    call cr_going
    jne .out
    mov ax, [cr_secs]
    mov si, [cr_lp]
    cmp byte [si+L_CMP], 0
    jne .out
    cmp byte [cr_time], 0
    je .out
    neg ax
    add ax, [cr_tlen]
    jns .rem
    xor ax, ax
.rem:
    inc dl
.out:
    ret

; --- 'ELAPSED' / 'REMAIN' over it ----------------------------------------------
cr_e_tlbl:
    mov al, [cr_time]
    xor ah, ah
    call cr_ek
    jc .out
    mov si, cr_s_elapsed
    or al, al
    jz .s
    mov si, cr_s_remain
.s:
    mov ax, 296
    mov bx, 58
    mov cx, 42
    mov dx, (R_LCD << 8) | R_LCDLBL
    call cr_fcaps
    call cr_ekdone
.out:
    ret

; --- one cell of the track grid (CX = 0..11) -------------------------------------
cr_e_cell:
    call cr_cellt                   ; AL = its track, 0 = none
    xor ah, ah
    or al, al
    jz .k
    mov ah, 4                       ; bit 10: a track
    cmp al, [cr_sel]
    jne .ns
    or ah, 1                        ; bit 8: the selection
.ns:
    mov si, cr_prog
    call cr_bit
    jnc .np
    or ah, 2                        ; bit 9: programmed
.np:
    mov si, cr_isaud
    call cr_bit
    jc .k
    or ah, 8                        ; bit 11: a data track
.k:
    call cr_ek
    jc .out
    push ax
    push cx
    mov cx, CF_GCW - 1
    mov dx, CF_GCH - 1
    call cr_bnew
    pop cx
    pop ax
    or al, al
    jz .put
    mov di, cr_tbuf                 ; its number, centred: one digit or two
    cmp al, 10
    jae .two
    add al, '0'
    mov [di], al
    mov byte [di+1], 0
    jmp short .num
.two:
    push ax
    call cr_2dig
    mov byte [di], 0
    pop ax
.num:
    push ax
    push cx
    mov si, cr_tbuf
    call cr_capsw
    mov ax, CF_GCW - 1
    sub ax, cx
    shr ax, 1
    mov bx, 6
    call cr_bcaps
    pop cx
    pop ax
.put:
    mov dx, (R_LCD << 8) | R_LCDFG  ; the roles by its state
    test ah, 1
    jz .nsel
    mov dx, (R_LCDSEL << 8) | R_LCD
    jmp short .go
.nsel:
    test ah, 2
    jz .nprog
    mov dl, R_LCDPRG
.nprog:
    test ah, 8
    jz .go
    mov dl, R_LCDDIM
.go:
    mov ax, cx                      ; the cell's place
    xor bx, bx
    cmp ax, 6
    jb .row
    sub ax, 6
    mov bx, CF_GCH
.row:
    push dx
    mov dx, CF_GCW
    mul dx
    pop dx
    add ax, CF_GX + 1
    add bx, CF_GY + 1
    call cr_bout
    call cr_ekdone
.out:
    ret

; cr_cellt - CX = a grid cell -> AL = the track in it on the selection's page
; of twelve, 0 = none. clobbers AH
cr_cellt:
    call cr_isaudio
    jne .none
    mov al, [cr_sel]
    dec al
    xor ah, ah
    push cx
    mov cl, 12
    div cl                          ; AL = the page
    mul cl                          ; AX = its first cell's track - 1
    pop cx
    add al, cl
    inc al
    cmp al, [cr_first]
    jb .none
    cmp al, [cr_last]
    jbe .out
.none:
    xor al, al
.out:
    ret

; --- the play state, with the play mark -----------------------------------------
cr_e_stat:
    call cr_statmsg                 ; SI = the words, AL = 1 with the mark
    mov ax, si                      ; the key: the string itself, and the mark
    add ax, ax                      ; (strings are under 32K into the image)
    or al, [cr_mark]
    call cr_ek
    jc .out
    mov si, [cr_lp]
    mov cx, 90
    mov dx, 7
    call cr_bnew
    cmp byte [cr_mark], 0
    je .nomark
    push si
    xor ax, ax
    xor bx, bx
    mov si, cdart_playmark
    call cr_bpic
    pop si
.nomark:
    call cr_statmsg
    mov ax, 11
    xor bx, bx
    call cr_bcaps
    mov si, [cr_lp]
    mov ax, [si+L_STX]
    mov bx, [si+L_STY]
    mov dx, (R_LCD << 8) | R_LCDFG
    call cr_bout
    call cr_ekdone
.out:
    ret

; cr_statmsg - SI = the LCD's words for the state; [cr_mark] = 1 when the
; play mark goes with them. clobbers AX
cr_statmsg:
    mov byte [cr_mark], 0
    mov al, [cr_state]
    cmp al, CPS_AUDIO
    jne .disc
    mov al, [cr_play]
    cmp al, CDS_PLAYING
    jne .notp
    mov byte [cr_mark], 1
.notp:
    xor ah, ah
    mov si, ax
    shl si, 1
    mov si, [cr_lpmsg+si]
    ret
.disc:
    xor ah, ah
    mov si, ax
    shl si, 1
    mov si, [cr_lsmsg+si]
    ret

; --- 'STEREO  44.1 KHZ' while there is audio --------------------------------------
cr_e_stereo:
    xor ax, ax
    call cr_isaudio
    jne .k
    inc ax
.k:
    call cr_ek
    jc .out
    push ax
    mov cx, 98
    mov dx, 7
    call cr_bnew
    pop ax
    or ax, ax
    jz .put
    xor ax, ax
    xor bx, bx
    mov si, cr_s_stereo
    call cr_bcaps
    mov ax, 50
    mov si, cr_s_khz
    call cr_bcaps
.put:
    mov ax, 330
    mov bx, 106
    mov dx, (R_LCD << 8) | R_LCDLBL
    call cr_bout
    call cr_ekdone
.out:
    ret

; --- the progress row's two times (FULL and MID): CX = 0 elapsed, 1 the length --
cr_e_ptxt:
    mov ax, 0x7FFF
    call cr_isaudio
    jne .k
    mov ax, [cr_tlen]
    jcxz .el
    jmp short .k
.el:
    xor ax, ax
    call cr_going
    jne .k
    mov ax, [cr_secs]
.k:
    call cr_ek
    jc .out
    mov di, cr_tbuf
    cmp ax, 0x7FFF
    jne .t
    mov word [di], '--'
    mov word [di+2], ':-'
    mov byte [di+4], '-'
    add di, 5
    jmp short .z
.t:
    call cr_mmss
    dec di
.z:
    mov byte [di], 0
    mov dx, 125
    push cx
    mov cx, 224
    pop ax
    or ax, ax
    jz .x
    mov cx, 544
.x:
    mov si, cr_tbuf
    mov ax, (R_LCD << 8) | R_LCDFG
    call cr_text
    call cr_ekdone
.out:
    ret

; --- the progress bar: only the strip between the old length and the new -------
cr_e_pbar:
    mov si, [cr_lp]
    mov cx, [si+L_PB+4]
    sub cx, [si+L_PB]
    inc cx                          ; CX = its inside width
    xor ax, ax
    call cr_going
    jne .k
    mov bx, [cr_tlen]
    or bx, bx
    jz .k
    mov ax, [cr_secs]
    cmp ax, bx
    jb .in
    mov ax, bx
.in:
    mul cx
    div bx                          ; AX = the done part's pixels
.k:
    call cr_ek
    jc .out
    mov si, [cr_lp]
    mov di, [cr_slot]
    mov dx, [di]                    ; DX = what the window shows, FFFF none
    mov bx, [si+L_PB+2]
    push dx
    mov dx, [si+L_PB+6]
    mov cx, [si+L_PB]
    pop di                          ; DI = the old length
    cmp di, 0xFFFF
    je .whole
    cmp ax, di
    je .done
    ja .grow
    ; shrank: the strip [new, old) goes back to the ground
    add cx, di
    dec cx
    add ax, [si+L_PB]
    push si
    mov si, [si+L_PBBG]
    call cr_rect
    pop si
    jmp short .done
.grow:                              ; grew: [old, new) fills
    xchg ax, di
    add ax, cx
    add cx, di
    dec cx
    push si
    mov si, [si+L_PBFG]
    call cr_rect
    pop si
    jmp short .done
.whole:                             ; both parts, from nothing
    push ax
    mov di, ax
    mov ax, cx
    add cx, di
    dec cx
    push si
    mov si, [si+L_PBFG]
    call cr_rect                    ; [x1, x1 + done)
    pop si
    mov ax, cx
    inc ax
    mov cx, [si+L_PB+4]
    push si
    mov si, [si+L_PBBG]
    call cr_rect                    ; [x1 + done, x2]
    pop si
    pop ax
.done:
    call cr_ekdone
.out:
    ret

; --- 'VOL 75%' (FULL and MID) --------------------------------------------------
cr_e_vol:
    mov ax, 0x7F00
    cmp byte [cr_volok], 0
    je .k
    mov al, [cr_vol]
    xor ah, ah
.k:
    call cr_ek
    jc .out
    mov di, cr_tbuf
    mov word [di], 'VO'
    mov word [di+2], 'L '
    add di, 4
    cmp ah, 0x7F
    jne .n
    mov word [di], '--'
    add di, 2
    jmp short .z
.n:
    cmp al, 100
    jb .two
    mov word [di], '10'
    mov byte [di+2], '0'
    add di, 3
    jmp short .pc
.two:
    call cr_num
.pc:
    mov byte [di], '%'
    inc di
.z:
    mov byte [di], 0
    mov si, cr_tbuf
    mov ax, 366
    mov bx, 168
    mov cx, 48
    mov dx, (R_PANEL << 8) | R_LABEL
    call cr_fcaps
    call cr_ekdone
.out:
    ret

; --- the volume slider ----------------------------------------------------------
CR_THUMB    equ 6
cr_e_slider:
    mov ax, 0x7F00
    cmp byte [cr_volok], 0
    je .k
    mov al, [cr_vol]
    xor ah, ah
    cmp byte [cr_drag], 0
    je .k
    or ah, 0x40                     ; dragged: a different picture's key
.k:
    call cr_ek
    jc .out
    mov si, [cr_lp]
    mov bx, [si+L_SL+2]
    mov dx, [si+L_SL+6]
    cmp ah, 0x7F
    jne .live
    mov ax, [si+L_SL]               ; no volume to set: all ground
    mov cx, [si+L_SL+4]
    mov si, R_SLREST
    call cr_rect
    jmp short .done
.live:
    call cr_thumbx                  ; AX = the thumb's left
    mov di, ax
    mov ax, [si+L_SL]
    mov cx, di
    dec cx
    push si
    mov si, R_SLFILL
    call cr_rect                    ; the part turned up
    pop si
    mov ax, di
    add ax, CR_THUMB
    mov cx, [si+L_SL+4]
    push si
    mov si, R_SLREST
    call cr_rect                    ; the part to come
    mov ax, di
    mov cx, di
    add cx, CR_THUMB - 1
    call cr_thumb                   ; and the thumb
    pop si
.done:
    call cr_ekdone
.out:
    ret

; cr_thumb - a thumb at AX,BX - CX,DX: R_THUMB, and in black and white an
; R_EDGE outline round a smaller fill - each pixel once. keeps everything
cr_thumb:
    push si
    mov si, R_THUMB
    cmp word [cr_th], cr_tbw
    jne .fill
    push si
    mov si, R_EDGE
    call cr_frame
    pop si
    inc ax
    inc bx
    dec cx
    dec dx
    call cr_rect
    dec ax
    dec bx
    inc cx
    inc dx
    pop si
    ret
.fill:
    call cr_rect
    pop si
    ret

; cr_thumbx - AX = the thumb's left x for [cr_vol]; SI = the layout. keeps
; everything else
cr_thumbx:
    push cx
    push dx
    mov cx, [si+L_SL+4]
    sub cx, [si+L_SL]
    sub cx, CR_THUMB - 1            ; the travel
    mov al, [cr_vol]
    xor ah, ah
    mul cx
    mov cx, 100
    div cx
    add ax, [si+L_SL]
    pop dx
    pop cx
    ret

; --- the tray's label (FULL and MID) ---------------------------------------------
cr_e_tray:
    mov al, [cr_state]
    xor ah, ah
    call cr_ek
    jc .out
    mov si, ax
    shl si, 1
    mov si, [cr_ltray+si]
    push si
    mov cx, 72
    mov dx, 11
    call cr_bnew
    pop si
    mov ax, 72
    call cr_capsw
    sub ax, cx
    shr ax, 1
    mov bx, 2
    call cr_bcaps
    mov ax, 74
    mov bx, 70
    mov dx, (R_SLOTLBL << 8) | R_SLOTTXT
    call cr_bout
    call cr_ekdone
.out:
    ret

; --- '12 tracks  48:26' over the list (FULL) ---------------------------------------
cr_e_linfo:
    mov al, [cr_gen]
    mov ah, [cr_state]
    call cr_ek
    jc .out
    mov di, cr_tbuf
    mov cx, 18
    mov al, ' '
    call cr_fillb
    mov byte [di+18], 0
    call cr_isaudio
    jne .put
    mov al, [cr_last]
    sub al, [cr_first]
    inc al
    call cr_num
    mov si, cr_s_tracks
    call cr_put
    mov ax, [cr_dlen]
    call cr_mmss
.put:
    mov cx, 448
    mov dx, 221
    mov si, cr_tbuf
    mov ax, (R_PANEL << 8) | R_LABEL
    call cr_text
    call cr_ekdone
.out:
    ret

; --- one row of the track list (CX = 0..5) -----------------------------------------
cr_e_row:
    mov al, [cr_ltop]
    add al, cl
    xor ah, ah
    call cr_isaudio
    jne .none
    cmp al, [cr_last]
    jbe .is
.none:
    xor al, al
    jmp short .k
.is:
    mov ah, 1                       ; bit 8: a track
    cmp al, [cr_sel]
    jne .ns
    or ah, 2                        ; bit 9: the selection
    call cr_going
    jne .ns
    or ah, 4                        ; bit 10: playing it
.ns:
    mov si, cr_prog
    call cr_bit
    jnc .np
    or ah, 8                        ; bit 11: programmed
.np:
    mov si, cr_isaud
    call cr_bit
    jc .k
    or ah, 16                       ; bit 12: a data track
.k:
    call cr_ek
    jc .out
    push ax
    push cx
    mov di, cr_lbuf                 ; the row, CF_LCH characters
    mov cx, CF_LCH
    mov al, ' '
    call cr_fillb
    mov byte [di+CF_LCH], 0
    pop cx
    pop ax
    or ah, ah
    jz .draw
    test ah, 4
    jz .nm
    mov byte [di+1], 0x10           ; the play mark: CP437's right triangle
.nm:
    test ah, 8
    jz .nprg
    mov byte [di+9], '*'
.nprg:
    push ax
    add di, 4
    call cr_2dig
    pop ax
    mov di, cr_lbuf + 12
    mov si, cr_s_ltrk
    test ah, 16
    jz .tt
    mov si, cr_s_ldata
.tt:
    call cr_put
    push ax
    call cr_2dig
    pop ax
    mov di, cr_lbuf + 63
    push ax
    xor ah, ah
    mov si, ax
    shl si, 1
    mov ax, [cr_tlens+si]
    call cr_mmss
    pop ax
.draw:
    mov dx, (R_LIST << 8) | R_LISTTXT
    test ah, 2
    jz .col
    mov dx, (R_SEL << 8) | R_SELTXT
.col:
    mov ax, cx                      ; its y
    mov bx, CF_LROW
    push dx
    mul bx
    pop dx
    add ax, CF_LY0
    mov bx, ax
    push dx
    mov dl, dh                      ; the ground: two rows above the text,
    xor dh, dh                      ; three below
    mov si, dx
    mov ax, CF_LX1
    mov cx, CF_SBX - 1
    mov dx, bx
    inc dx
    call cr_rect
    add bx, 10
    mov dx, bx
    add dx, 2
    call cr_rect
    sub bx, 8
    pop ax                          ; AL = the ink, AH = the ground
    mov cx, CF_LX1
    mov dx, bx
    mov si, cr_lbuf
    call cr_text
    call cr_ekdone
.out:
    ret

; --- the list's scroll bar ------------------------------------------------------------
cr_e_sbar:
    xor ax, ax
    call cr_isaudio
    jne .k
    mov al, [cr_ltop]
    sub al, [cr_first]
    mov ah, [cr_last]
    sub ah, [cr_first]
    inc ah                          ; AH = the tracks
.k:
    call cr_ek
    jc .out
    mov cl, ah                      ; the thumb: its height and its top
    xor ch, ch
    mov si, CF_SBT2 - CF_SBT1 + 1   ; SI = the trough's height
    jcxz .empty
    cmp cx, CF_LN
    ja .some
    xor di, di                      ; it all fits: the thumb is the trough
    mov dx, si
    jmp short .draw
.some:
    push ax
    mov ax, si
    mov dx, CF_LN
    mul dx
    div cx                          ; AX = trough * shown / tracks
    cmp ax, 8
    jae .h
    mov ax, 8
.h:
    mov dx, ax                      ; DX = the thumb's height
    pop ax
    push dx
    mov bl, al                      ; ...and its top: (trough - thumb) *
    xor bh, bh                      ; top / (tracks - shown)
    sub si, dx
    mov ax, si
    mul bx
    sub cx, CF_LN
    div cx
    mov di, ax
    pop dx
    jmp short .draw
.empty:                             ; no disc: the trough alone
    mov ax, CF_SBX
    mov cx, CF_SBX + 11
    mov bx, CF_SBT1
    mov dx, CF_SBT2
    mov si, R_TROUGH
    call cr_rect
    jmp .done
.draw:
    mov ax, CF_SBX                  ; trough, then thumb: the trough's two
    mov cx, CF_SBX + 11             ; strips round the thumb
    mov bx, CF_SBT1
    push dx
    mov dx, CF_SBT1 - 1
    add dx, di
    mov si, R_TROUGH
    call cr_rect                    ; above
    pop dx
    add bx, di
    push bx
    add bx, dx
    push dx
    mov dx, CF_SBT2
    call cr_rect                    ; below
    pop dx
    pop bx
    push bx
    add dx, bx
    dec dx
    call cr_thumb                   ; the thumb
    pop bx
.done:
    call cr_ekdone
.out:
    ret

; --- the status bar's words (FULL and MID) ---------------------------------------
cr_e_sbtxt:
    mov al, [cr_state]
    mov cl, 4
    shl al, cl
    or al, [cr_play]
    mov ah, [cr_letter]
    call cr_ek
    jc .out
    mov di, cr_lbuf
    mov cx, 48
    mov al, ' '
    call cr_fillb
    mov byte [di+48], 0
    mov si, cr_s_drive              ; 'Drive D:' - or 'CD-ROM:' with no
    cmp byte [cr_letter], 0         ; volume to name
    je .nod
    call cr_put
    mov al, [cr_letter]
    mov [di], al
    mov byte [di+1], ':'
    jmp short .msg
.nod:
    mov si, cr_s_cdrom
    call cr_put
.msg:
    mov di, cr_lbuf + 12
    mov al, [cr_state]
    xor ah, ah
    mov si, ax
    shl si, 1
    mov si, [cr_bsmsg+si]
    cmp byte [cr_state], CPS_AUDIO
    jne .w
    mov al, [cr_play]
    mov si, ax
    shl si, 1
    mov si, [cr_bpmsg+si]
.w:
    call cr_put
    cmp byte [cr_state], CPS_AUDIO
    jne .put
    call cr_going
    jne .put
    mov al, [cr_sel]                ; '...track 03'
    call cr_2dig
.put:
    mov cx, 16
    mov dx, [cr_sby]
    add dx, 4
    mov si, cr_lbuf
    mov ax, (R_STAT << 8) | R_STATTXT
    call cr_text
    call cr_ekdone
.out:
    ret

; --- COMPACT: '/ 04:18' ---------------------------------------------------------------
cr_e_ctot:
    mov ax, 0x7FFF
    call cr_isaudio
    jne .k
    mov ax, [cr_tlen]
.k:
    call cr_ek
    jc .out
    mov di, cr_tbuf
    cmp ax, 0x7FFF
    jne .t
    mov word [di], '--'
    mov word [di+2], ':-'
    mov byte [di+4], '-'
    add di, 5
    jmp short .z
.t:
    call cr_mmss
    dec di
.z:
    mov byte [di], 0
    mov si, [cr_lp]
    mov di, [si+L_TOTSET]
    mov cx, [di+CRD_W]
    shl cx, 1
    shl cx, 1
    add cx, [di+CRD_CW]
    mov ax, [di+CRD_GAP]
    shl ax, 1
    shl ax, 1
    add cx, ax
    add cx, 10                      ; the '/' and its space
    mov dx, [di+CRD_H]
    call cr_bnew
    mov ax, 10
    xor bx, bx
    mov si, cr_tbuf
    call cr_bdigs
    xor ax, ax                      ; the '/', on the digits' baseline
    mov bx, [di+CRD_H]
    sub bx, 7
    mov si, cr_s_slash
    call cr_bcaps
    mov si, [cr_lp]
    mov ax, [si+L_TOT]
    mov bx, [si+L_TOT+2]
    mov dx, (R_LCD << 8) | R_LCDFG
    call cr_bout
    call cr_ekdone
.out:
    ret

; --- COMPACT: '03 OF 12' -------------------------------------------------------------------
cr_e_cof:
    mov ax, 0x7FFF
    call cr_isaudio
    jne .k
    mov al, [cr_sel]
    mov ah, [cr_last]
.k:
    call cr_ek
    jc .out
    mov di, cr_tbuf
    mov byte [di], 0
    cmp ax, 0x7FFF
    je .put
    push ax
    call cr_2dig
    mov si, cr_s_of
    call cr_put
    pop ax
    mov al, ah
    call cr_2dig
    mov byte [di], 0
.put:
    mov si, [cr_lp]
    mov ax, [si+L_OF]
    mov bx, [si+L_OF+2]
    mov si, cr_tbuf
    mov cx, 48
    mov dx, (R_LCD << 8) | R_LCDLBL
    call cr_fcaps
    call cr_ekdone
.out:
    ret

; =============================================================================
; THE BUTTONS (apps/os88ui.inc's record, CDRack's painter: SPEC.md 13.8.10)
; =============================================================================

; -----------------------------------------------------------------------------
; cr_bpaint - the OWN painter: AX = the index, BX = the rect (screen,
;             inclusive), DI = the flags, DOWN and DIS resolved. Keeps every
;             register
;
; A frame, and the whole inside as ONE band - the picture and the caption
; composed in it, the face its paper - so a button is two calls, pressed,
; latched, greyed or not, and no pixel is drawn twice.
; -----------------------------------------------------------------------------
cr_bpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp word [bx], CR_PARK          ; not on this layout
    je .out
    mov bp, ax                      ; BP = the index
    mov ax, [bx]                    ; the rect, content-relative
    sub ax, [cr_ox]
    mov cx, [bx+4]
    sub cx, [cr_ox]
    mov dx, [bx+6]
    sub dx, [cr_oy]
    mov bx, [bx+2]
    sub bx, [cr_oy]
    mov si, R_BTNEDGE               ; the roles: up, or down (pressed or
    mov word [cr_bink], (R_BTN << 8) | R_BTNTXT ; latched)
    test di, OS88UI_DIS
    jnz .up
    test di, OS88UI_DOWN
    jz .up
    mov si, R_BTNONE
    mov word [cr_bink], (R_BTNON << 8) | R_BTNONT
.up:
    call cr_frame
    inc ax                          ; the inside
    inc bx
    dec cx
    dec dx
    push ax
    push bx
    sub cx, ax
    inc cx
    sub dx, bx
    inc dx
    call cr_bnew                    ; CX x DX
    mov [cr_bbw], cx
    mov [cr_bbh], dx
    mov si, bp                      ; the picture...
    shl si, 1
    mov bx, [cr_lp]
    mov bx, [bx+L_ICONS]
    mov ax, [bx+si]
    mov [cr_bpic_], ax
    mov bx, [cr_lp]                 ; ...and the caption
    mov bx, [bx+L_CAPS]
    mov ax, [bx+si]
    mov [cr_bcap], ax
    mov si, [cr_lp]
    cmp bp, B_REPEAT
    jb .stack
    cmp bp, B_TIME
    ja .side
    mov ax, [si+L_UNCHK]            ; a toggle: its box says it
    test di, CRF_ON
    jz .box
    mov ax, [si+L_CHK]
.box:
    mov [cr_bpic_], ax
.side:                              ; picture, then caption, side by side
    mov si, [cr_bpic_]
    call cr_picw
    mov ax, [cr_bbh]
    sub ax, dx
    sar ax, 1
    mov bx, ax
    mov ax, 3
    call cr_bpic
    add ax, 3
    mov bx, [cr_bbh]
    sub bx, 7
    sar bx, 1
    mov si, [cr_bcap]
    or si, si
    jz .compose
    call cr_bcaps
    jmp short .compose
.stack:                             ; picture over caption, centred
    mov si, [cr_bpic_]
    call cr_picw                    ; CX = w, DX = h
    mov bx, dx
    cmp word [cr_bcap], 0
    je .alone
    add bx, 4 + 7
.alone:
    mov ax, [cr_bbh]
    sub ax, bx
    sar ax, 1
    mov bx, ax                      ; BX = the picture's top
    mov ax, [cr_bbw]
    sub ax, cx
    sar ax, 1
    call cr_bpic
    mov si, [cr_bcap]
    or si, si
    jz .compose
    add bx, dx
    add bx, 4
    call cr_capsw
    mov ax, [cr_bbw]
    sub ax, cx
    sar ax, 1
    call cr_bcaps
.compose:
    test di, OS88UI_DIS             ; greyed: the dim ink on the colour face,
    jz .live                        ; half the ink in black and white
    cmp word [cr_th], cr_tbw
    je .dith
    mov byte [cr_bink], R_BTNDIS
    jmp short .live
.dith:
    mov byte [cr_bdim], 1
.live:
    pop bx
    pop ax
    mov dx, [cr_bink]
    call cr_bout
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; HIT-TESTS - a press that was not a button's. In: CX, DX content-relative
; =============================================================================

; cr_in - CF = 0 when CX, DX is inside the rect at SI (x1, y1, x2, y2)
cr_in:
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
