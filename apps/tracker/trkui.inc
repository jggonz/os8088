; =============================================================================
; os8088 - apps/tracker/trkui.inc
;
; The Tracker's whole visual surface (SPEC.md 45): the FastTracker II layout,
; adapter-parameterized. Included by tracker.asm AFTER the TRKB/TRKW/TRKBUF
; bss macros are defined; label prefix tui_ (trk_ belongs to tracker.asm,
; mp_ to trkplay.inc; ui_ is the kernel's and stays out of a package).
;
; Three layouts, chosen by OSAPI_VIDEO height (SPEC.md 39):
;   >= 400  VGA 640x480      the FT2-proportioned screen: top desktop area
;                            y=0..171 (position editor, TRACKER nameplate,
;                            BPM/Spd + Ptn/Ln boxes, status line, 2x2 scope
;                            grid, two button stacks, instrument list), the
;                            pattern editor below with 16 rows above and 18
;                            below the full-width current-row band
;   >= 300  Hercules 720x348 the mid layout: same top blocks, scopes in one
;                            row of four, no menus/instrument list, 14+14
;                            pattern rows
;   else    CGA 640x200      compact: one title/readout line, four inline
;                            volume bars, 10+10 pattern rows
;
; Colours are a table indexed by role, filled per adapter: the VGA table is
; the FT2 "Arctic" palette mapped to 16 colours (pattern bg 0, pattern text
; 9, band 7 with text 15, bevels 15/8, accent 1); the 1bpp table never uses
; colour 9 (it reduces to BLACK - invisible on the pattern's black field,
; SPEC.md 39.4): text is 15 on 0, the band inverts to solid white with black
; text, faces go black with white bevels, and the desktop is the 50% dither.
;
; THE GRANULARITY RULE (SPEC.md 11.3) is enforced the fr_status way: every
; erase+text pair - a pattern row strip, a readout value, a whole desktop
; element - is gated on ONE OSAPI_WM_CLIP_TEST of its rect and skipped whole
; when any of it is covered. Pure fills (backgrounds, volume bars) clip per
; pixel and are never gated. In fullscreen the only thing that ever covers
; this window is the Standard File dialog - which is exactly when the gates
; matter.
; =============================================================================

; --- layout record offsets (words; one record copied into tui_lay) -------------
TL_PATY   equ 0                     ; pattern area top (0 = none above it)
TL_HDRY   equ 2                     ; channel digit strip y
TL_ROWT   equ 4                     ; first upper row y
TL_RAB    equ 6                     ; rows above the band
TL_BANDY  equ 8                     ; band top (= ROWT + RAB*8)
TL_LOWY   equ 10                    ; first lower row y (= BANDY + 11)
TL_RBE    equ 12                    ; rows below the band
TL_RNL    equ 14                    ; left row-number x
TL_RNR    equ 16                    ; right row-number x, 0FFFFh = none
TL_CHX    equ 18                    ; channel block left x
TL_CHW    equ 20                    ; per-channel width, separator included
TL_PAD    equ 22                    ; cell text x offset inside a channel
TL_SCX    equ 24                    ; scope grid origin
TL_SCY    equ 26
TL_SCDX   equ 28                    ; cell pitch
TL_SCDY   equ 30
TL_SCW    equ 32                    ; cell size
TL_SCH    equ 34
TL_SCPR   equ 36                    ; scopes per row (2 or 4)
TL_STX    equ 38                    ; status/message text position
TL_STY    equ 40
TL_STW    equ 42                    ; ...and its erase width
TL_SLX    equ 44                    ; top-band sliver left x, 0FFFFh = none
TL_FLAGS  equ 46                    ; bit0 instrument list, bit1 button
                                    ; stacks, bit2 compact (CGA)
TL_SIZE   equ 48

TLF_INSTR equ 1
TLF_MENUS equ 2
TLF_CMPCT equ 4

; --- colour table roles (bytes, indexed into tui_col) --------------------------
TC_BG     equ 0                     ; pattern background
TC_TEXT   equ 1                     ; pattern text (9 - NEVER on mono)
TC_BAND   equ 2                     ; current-row band fill
TC_BANDT  equ 3                     ; ...and its text
TC_FACE   equ 4                     ; desktop element face
TC_FACET  equ 5                     ; ...and its text
TC_BEVL   equ 6                     ; light bevel
TC_BEVD   equ 7                     ; dark bevel
TC_BTN    equ 8                     ; button face
TC_BTNT   equ 9                     ; ...and its text
TC_VU     equ 10                    ; volume bar
TC_SEP    equ 11                    ; channel separators / rules
TC_ACC    equ 12                    ; nameplate accent
TC_ACCT   equ 13                    ; ...and its text
TC_STATB  equ 14                    ; status line background
TC_STATT  equ 15                    ; ...and its text
TC_WBG    equ 16                    ; windowed splash / About panel bg
TC_WTX    equ 17                    ; ...and its text
TC_N      equ 18

; set the pen from the colour table
%macro TSETC 1
    push ax
    mov al, %1
    call tui_setcol
    pop ax
%endmacro

; -----------------------------------------------------------------------------
; tui_layout_init - pick a layout + colour table off the live screen. Called
;                   once, from the first W_PAINT (OSAPI_VIDEO is any-context).
; preserves all registers
; -----------------------------------------------------------------------------
tui_layout_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_VIDEO                ; AX = w, BX = h, DL = adapter, DH = bpp
    mov [tui_vw], ax
    mov [tui_vh], bx
    mov byte [tui_mono], 0
    cmp dh, 1
    jne .depth
    mov byte [tui_mono], 1
.depth:
    mov si, tui_lay_vga
    cmp bx, 400
    jae .lay
    mov si, tui_lay_hrc
    cmp bx, 300
    jae .lay
    mov si, tui_lay_cga
.lay:
    mov di, tui_lay
    mov cx, TL_SIZE / 2
.lcopy:
    mov ax, [si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .lcopy
    mov si, tui_col_vga
    cmp byte [tui_mono], 0
    je .col
    mov si, tui_col_mono
.col:
    mov di, tui_col
    mov cx, TC_N
.ccopy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .ccopy
    ; derived: right edge of the channel block, of a row strip, VU bar width
    mov ax, [tui_lay + TL_CHW]
    shl ax, 1
    shl ax, 1                       ; * 4 channels
    add ax, [tui_lay + TL_CHX]
    dec ax
    mov [tui_chend], ax
    mov bx, [tui_lay + TL_RNR]
    cmp bx, 0FFFFh
    je .norr
    add bx, 15
    cmp bx, ax                      ; strip reaches whichever is further
    jbe .norr
    mov ax, bx
.norr:
    mov [tui_rend], ax
    mov ax, [tui_lay + TL_SCW]
    sub ax, 24
    cmp ax, 128
    jbe .vok
    mov ax, 128
.vok:
    mov [tui_vuw], ax
    mov byte [tui_inited], 1
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_track - adopt the live content origin and size (top of every draw; the
;             window moves while the worker sleeps, and under WF_FULL the
;             kernel answers (0,0) and the whole screen - SPEC.md 11.2)
; -----------------------------------------------------------------------------
tui_track:
    push ax
    push bx
    push cx
    push dx
    mov bx, [trk_win]
    call OSAPI_WM_CONTENT           ; AX = content left, DX = top
    mov [tui_ox], ax
    mov [tui_oy], dx
    call OSAPI_WM_GEOM              ; CX = content width, DX = height
    mov [tui_cw], cx
    mov [tui_ch], dx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Content-coordinate wrappers (the ark_fillc pattern): everything above
; measures from the content origin; only these add it. All preserve every
; register (tui_clipc returns CF).
; =============================================================================
tui_fillc:
    push ax
    push bx
    push cx
    push dx
    add ax, [tui_ox]
    add cx, [tui_ox]
    add bx, [tui_oy]
    add dx, [tui_oy]
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tui_framec:
    push ax
    push bx
    push cx
    push dx
    add ax, [tui_ox]
    add cx, [tui_ox]
    add bx, [tui_oy]
    add dx, [tui_oy]
    call OSAPI_GFX_FRAME
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tui_grayc:
    push ax
    push bx
    push cx
    push dx
    add ax, [tui_ox]
    add cx, [tui_ox]
    add bx, [tui_oy]
    add dx, [tui_oy]
    call OSAPI_GFX_FILL_GRAY
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tui_hlinec:                         ; AX = x1, BX = x2, DX = y
    push ax
    push bx
    push dx
    add ax, [tui_ox]
    add bx, [tui_ox]
    add dx, [tui_oy]
    call OSAPI_GFX_HLINE
    pop dx
    pop bx
    pop ax
    ret

tui_vlinec:                         ; AX = x, BX = y1, DX = y2
    push ax
    push bx
    push dx
    add ax, [tui_ox]
    add bx, [tui_oy]
    add dx, [tui_oy]
    call OSAPI_GFX_VLINE
    pop dx
    pop bx
    pop ax
    ret

tui_textc:                          ; SI = string, CX = x, DX = y
    push cx
    push dx
    add cx, [tui_ox]
    add dx, [tui_oy]
    call OSAPI_FONT_STR
    pop dx
    pop cx
    ret

tui_charc:                          ; AL = char, CX = x, DX = y
    push cx
    push dx
    add cx, [tui_ox]
    add dx, [tui_oy]
    call OSAPI_FONT_CHAR
    pop dx
    pop cx
    ret

tui_clipc:                          ; AX,BX,CX,DX rect -> CF=1 partly covered
    push ax
    push bx
    push cx
    push dx
    add ax, [tui_ox]
    add cx, [tui_ox]
    add bx, [tui_oy]
    add dx, [tui_oy]
    call OSAPI_WM_CLIP_TEST         ; CF survives the pops below
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_setcol - AL = colour TABLE INDEX -> the global pen (lock held: the pen
;              is one machine-wide byte, SPEC.md 20.6 rule 7)
; -----------------------------------------------------------------------------
tui_setcol:
    push ax
    push bx
    mov bl, al
    xor bh, bh
    mov al, [tui_col + bx]
    call OSAPI_SET_COLOR
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_bevelc - the FT2 3D edge: light top/left, dark bottom/right
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (content); preserves all
; -----------------------------------------------------------------------------
tui_bevelc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, ax                      ; x1
    mov di, bx                      ; y1
    TSETC TC_BEVL
    mov ax, si
    mov bx, cx
    push dx
    mov dx, di
    call tui_hlinec                 ; top
    pop dx
    mov ax, si
    mov bx, di
    call tui_vlinec                 ; left
    TSETC TC_BEVD
    mov ax, si
    mov bx, cx
    call tui_hlinec                 ; bottom (DX = y2)
    mov ax, cx
    mov bx, di
    call tui_vlinec                 ; right
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_facec - a desktop element: face fill + bevel
; tui_deskfill - the desktop background: solid on VGA, 50% dither on mono
; in:  AX,BX,CX,DX rect (content); preserve all
; -----------------------------------------------------------------------------
tui_facec:
    TSETC TC_FACE
    call tui_fillc
    call tui_bevelc
    ret

tui_deskfill:
    cmp byte [tui_mono], 0
    jne .mono
    TSETC TC_FACE
    call tui_fillc
    ret
.mono:
    call tui_grayc
    ret

; =============================================================================
; Small text helpers
; =============================================================================

; tui_scpy - copy the NUL string at SI to DI (NUL not copied), DI advances
tui_scpy:
    push ax
    push si
.l:
    mov al, [si]
    or al, al
    jz .d
    mov [di], al
    inc si
    inc di
    jmp .l
.d:
    pop si
    pop ax
    ret

; tui_hexb - AL as two uppercase hex chars at DI; DI advances by 2
tui_hexb:
    push ax
    push bx
    push cx
    mov ah, al
    mov bl, al
    mov cl, 4
    shr bl, cl
    xor bh, bh
    mov al, [tui_hexd + bx]
    mov [di], al
    inc di
    mov bl, ah
    and bl, 0Fh
    xor bh, bh
    mov al, [tui_hexd + bx]
    mov [di], al
    inc di
    pop cx
    pop bx
    pop ax
    ret

; tui_hex2 - AL -> tui_nbuf as 'XX', NUL-terminated
tui_hex2:
    push di
    mov di, tui_nbuf
    call tui_hexb
    mov byte [di], 0
    pop di
    ret

; tui_dec3 - AL -> tui_nbuf as three decimal digits, NUL-terminated
tui_dec3:
    push ax
    push bx
    xor ah, ah
    mov bl, 100
    div bl                          ; AL = hundreds, AH = rest
    add al, '0'
    mov [tui_nbuf], al
    mov al, ah
    xor ah, ah
    mov bl, 10
    div bl
    add al, '0'
    add ah, '0'
    mov [tui_nbuf + 1], al
    mov [tui_nbuf + 2], ah
    mov byte [tui_nbuf + 3], 0
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_rdout - ONE gated erase+text pair (the fr_status idiom, SPEC.md 11.3):
;             clip-test the erase rect; covered in any part, skip BOTH.
; in:  SI = NUL string, CX = x, DX = y (content), BX = erase width px,
;      AH = erase colour index, AL = text colour index
; preserves all registers
; -----------------------------------------------------------------------------
tui_rdout:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [tui_tcol], al
    mov [tui_ecol], ah
    mov ax, bx
    add ax, cx
    dec ax                          ; x2
    mov bx, dx                      ; y1
    push dx                         ; text y
    push cx                         ; text x
    mov dx, bx
    add dx, 7                       ; y2
    xchg ax, cx                     ; AX = x1, CX = x2
    call tui_clipc
    jc .skip
    push ax
    mov al, [tui_ecol]
    call tui_setcol
    pop ax
    call tui_fillc
    mov al, [tui_tcol]
    call tui_setcol
    pop cx
    pop dx
    call tui_textc
    jmp .out
.skip:
    pop cx
    pop dx
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The status/message line (tui_msg is the export tracker.asm calls)
; =============================================================================
tui_msg:                            ; SI = NUL string; lock held
    push si
    mov [tui_msgp], si
    call tui_msg_draw
    pop si
    ret

tui_msg_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [tui_msgp]
    or si, si
    jnz .have
    mov si, tui_s_hint
.have:
    cmp byte [trk_fs], 0
    je .win
    mov cx, [tui_lay + TL_STX]
    mov dx, [tui_lay + TL_STY]
    mov bx, [tui_lay + TL_STW]
    mov ah, TC_STATB
    mov al, TC_STATT
    call tui_rdout
    jmp .out
.win:
    mov cx, 4
    mov dx, [tui_ch]
    sub dx, 10
    mov bx, [tui_cw]
    sub bx, 8
    mov ah, TC_WBG
    mov al, TC_WTX
    call tui_rdout
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Desktop elements (full layouts). Each is gated whole on its own rect: a
; partly covered element stays STALE, never half-blank, and the every-16th
; top refresh repaints it once it is uncovered.
; =============================================================================

; --- position editor (0,0)-(111,76): 5-line order window with the current
; --- order on a band, plus song length and position readouts ------------------
tui_el_posed:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor ax, ax
    xor bx, bx
    mov cx, 111
    mov dx, 76
    call tui_clipc
    jc .out
    call tui_facec
    mov byte [tui_chn], 0           ; k = order-window line 0..4
.k:
    mov al, [tui_chn]
    xor ah, ah
    mov bl, 9
    mul bl
    add ax, 5
    mov dx, ax                      ; line y: 5, 14, 23, 32, 41
    cmp byte [tui_chn], 2
    jne .plain
    push dx
    mov ax, 3                       ; the band line (FT2's highlighted order)
    mov bx, 23
    mov cx, 108
    mov dx, 31
    TSETC TC_BAND
    call tui_fillc
    pop dx
    add dx, 1
    mov al, TC_BANDT
    jmp .col
.plain:
    mov al, TC_FACET
.col:
    call tui_setcol
    mov al, [mp_songpos]            ; ord = songpos - 2 + k
    xor ah, ah
    sub ax, 2
    mov bl, [tui_chn]
    xor bh, bh
    add ax, bx
    cmp ax, 0
    jl .next
    mov bl, [mp_songlen]
    xor bh, bh
    cmp ax, bx
    jge .next
    push dx
    mov di, tui_nbuf                ; 'oo pp' - order index, pattern number
    call tui_hexb
    mov byte [di], ' '
    inc di
    push di
    call mp_order                   ; AL order in -> AL pattern out (clobbers)
    pop di
    call tui_hexb
    mov byte [di], 0
    pop dx
    mov si, tui_nbuf
    mov cx, 8
    call tui_textc
.next:
    inc byte [tui_chn]
    cmp byte [tui_chn], 5
    jb .k
    TSETC TC_FACET
    mov si, tui_s_len
    mov cx, 6
    mov dx, 56
    call tui_textc
    mov al, [mp_songlen]
    call tui_hex2
    mov si, tui_nbuf
    mov cx, 38
    call tui_textc
    mov si, tui_s_posl2
    mov cx, 6
    mov dx, 66
    call tui_textc
    mov al, [mp_songpos]
    call tui_hex2
    mov si, tui_nbuf
    mov cx, 38
    call tui_textc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the nameplate strip (112,0)-(290,31) --------------------------------------
tui_el_logo:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, 112
    xor bx, bx
    mov cx, 290
    mov dx, 31
    call tui_clipc
    jc .out
    TSETC TC_ACC
    call tui_fillc
    call tui_bevelc
    mov si, tui_s_logo
    cmp byte [tui_mono], 0          ; drop shadow: black on the blue plate;
    jne .plain                      ; on mono the plate IS black - skip it
    TSETC TC_BEVD
    mov cx, 150
    mov dx, 13
    call tui_textc
.plain:
    TSETC TC_ACCT
    mov cx, 149
    mov dx, 12
    call tui_textc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- BPM / Spd box (112,32)-(205,76) -------------------------------------------
tui_el_bpm:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, 112
    mov bx, 32
    mov cx, 205
    mov dx, 76
    call tui_clipc
    jc .out
    call tui_facec
    TSETC TC_FACET
    mov si, tui_s_bpm
    mov cx, 116
    mov dx, 36
    call tui_textc
    mov al, [mp_bpm]
    call tui_dec3
    mov si, tui_nbuf
    mov cx, 168
    call tui_textc
    mov si, tui_s_spd
    mov cx, 116
    mov dx, 50
    call tui_textc
    mov al, [mp_speed]
    call tui_dec3
    mov si, tui_nbuf
    mov cx, 168
    call tui_textc
    mov al, [mp_bpm]
    mov [tui_lbpm], al
    mov al, [mp_speed]
    mov [tui_lspd], al
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- Ptn / Ln box (206,32)-(290,76) --------------------------------------------
tui_el_ptn:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, 206
    mov bx, 32
    mov cx, 290
    mov dx, 76
    call tui_clipc
    jc .out
    call tui_facec
    TSETC TC_FACET
    mov si, tui_s_ptn
    mov cx, 210
    mov dx, 36
    call tui_textc
    mov al, [mp_pattern]
    call tui_hex2
    mov si, tui_nbuf
    mov cx, 258
    call tui_textc
    mov si, tui_s_ln
    mov cx, 210
    mov dx, 50
    call tui_textc
    mov si, tui_s_64
    mov cx, 258
    call tui_textc
    mov si, tui_s_np
    mov cx, 210
    mov dx, 64
    call tui_textc
    mov al, [mp_npat]
    call tui_hex2
    mov si, tui_nbuf
    mov cx, 258
    call tui_textc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- status bar (0,77)-(290,91) ------------------------------------------------
tui_el_status:
    push ax
    push bx
    push cx
    push dx
    push si
    xor ax, ax
    mov bx, 77
    mov cx, 290
    mov dx, 91
    call tui_clipc
    jc .out
    TSETC TC_STATB
    call tui_fillc
    call tui_bevelc
    mov si, [tui_msgp]
    or si, si
    jnz .have
    mov si, tui_s_hint
.have:
    TSETC TC_STATT
    mov cx, [tui_lay + TL_STX]
    mov dx, [tui_lay + TL_STY]
    call tui_textc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_scellxy - scope cell AL (0..3) -> [tui_t1] = x0, [tui_t2] = y0
; preserves all registers
; -----------------------------------------------------------------------------
tui_scellxy:
    push ax
    push bx
    push dx
    xor ah, ah
    div byte [tui_lay + TL_SCPR]    ; AL = row, AH = column
    mov bl, al
    mov al, ah
    xor ah, ah
    mul word [tui_lay + TL_SCDX]
    add ax, [tui_lay + TL_SCX]
    mov [tui_t1], ax
    mov al, bl
    xor ah, ah
    mul word [tui_lay + TL_SCDY]
    add ax, [tui_lay + TL_SCY]
    mov [tui_t2], ax
    pop dx
    pop bx
    pop ax
    ret

; --- the scopes: volume-bar stand-ins for FT2's oscilloscopes ------------------
; Frames, channel numbers and MUTE flags here (redrawn on mute change and by
; the top refresh); the bars themselves are tui_draw_vu's, every frame.
tui_el_scopes:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [tui_chn], 0
.cell:
    mov al, [tui_chn]
    call tui_scellxy
    mov ax, [tui_t1]
    mov bx, [tui_t2]
    mov cx, ax
    add cx, [tui_lay + TL_SCW]
    dec cx
    mov dx, bx
    add dx, [tui_lay + TL_SCH]
    dec dx
    call tui_clipc
    jc .next
    TSETC TC_BG
    call tui_fillc
    call tui_bevelc
    TSETC TC_TEXT
    mov cx, [tui_t1]
    mov dx, [tui_t2]
    cmp word [tui_lay + TL_SCH], 16 ; compact cells centre differently
    jb .csm
    add cx, 4
    add dx, 2
    jmp .dig
.csm:
    inc cx
    inc dx
.dig:
    mov al, [tui_chn]
    add al, '1'
    call tui_charc
    mov bl, [tui_chn]
    xor bh, bh
    cmp byte [mp_mute + bx], 0
    je .next
    cmp word [tui_lay + TL_SCH], 16
    jb .msm
    mov si, tui_s_mute
    add cx, 12
    call tui_textc
    jmp .next
.msm:
    mov al, 'M'
    mov cx, [tui_t1]
    add cx, 146
    call tui_charc
.next:
    inc byte [tui_chn]
    cmp byte [tui_chn], 4
    jb .cell
    ; the Hercules layout has no instrument list: the song title rides under
    ; the scope row instead
    test byte [tui_lay + TL_FLAGS], TLF_INSTR | TLF_CMPCT
    jnz .sync
    mov si, mp_title
    mov cx, 295
    mov dx, 52
    mov bx, 176
    mov ah, TC_STATB
    mov al, TC_STATT
    call tui_rdout
.sync:
    mov ax, [mp_mute]               ; remember what we drew, so tui_draw_dyn
    mov [tui_lmute], ax             ; can see a toggle
    mov ax, [mp_mute + 2]
    mov [tui_lmute + 2], ax
    xor ax, ax                      ; the cells were just repainted: the
    mov [tui_vupx], ax              ; delta-drawn bars restart from empty
    mov [tui_vupx + 2], ax          ; (SPEC.md 45.12)
    mov [tui_vupx + 4], ax
    mov [tui_vupx + 6], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_btn - one FT2-style button: face, bevel, label
; in:  CX = x, DX = y (content), SI = label; preserves all
; -----------------------------------------------------------------------------
tui_btn:
    push ax
    push bx
    push cx
    push dx
    mov ax, cx
    mov bx, dx
    add cx, 59
    add dx, 13
    push cx
    push dx
    mov cx, ax
    add cx, 59
    mov dx, bx
    add dx, 13
    TSETC TC_BTN
    call tui_fillc
    call tui_bevelc
    pop dx
    pop cx
    sub cx, 55                      ; label at x+4, y+3
    sub dx, 10
    TSETC TC_BTNT
    call tui_textc
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the two button stacks (291,0)-(355,172) and (356,0)-(420,172) -------------
; Decorative in v1 (SPEC.md 45: every action is a key), but they are the FT2
; signature and each names the key that does the same thing.
tui_el_menus:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, 291
    xor bx, bx
    mov cx, 355
    mov dx, 172
    call tui_clipc
    jc .right
    call tui_facec
    mov di, tui_mbl
    mov cx, 294
    call tui_stack
.right:
    mov ax, 356
    xor bx, bx
    mov cx, 420
    mov dx, 172
    call tui_clipc
    jc .out
    call tui_facec
    mov di, tui_mbr
    mov cx, 359
    call tui_stack
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tui_stack:                          ; DI = dw label table (0-ended), CX = x
    push cx
    push dx
    push si
    push di
    mov dx, 2
.b:
    mov si, [di]
    or si, si
    jz .done
    call tui_btn
    add di, 2
    add dx, 17
    jmp .b
.done:
    pop di
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; tui_insget - instrument AL's 22-byte name (raw blob bytes, hostile) into
;              tui_insbuf: 17 chars kept, control bytes spaced, NUL added
; out: tui_insbuf; preserves registers (ES restored per the mp_insname rule)
; -----------------------------------------------------------------------------
tui_insget:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mp_insname                 ; -> ES:SI, CF=1 none/invalid
    jc .none
    mov di, tui_insbuf
    mov cx, 17
.c:
    mov al, [es:si]
    cmp al, 32
    jb .sp
    cmp al, 126
    jbe .keep
.sp:
    mov al, ' '
.keep:
    mov [di], al
    inc si
    inc di
    loop .c
    mov byte [di], 0
    jmp .done
.none:
    mov byte [tui_insbuf], 0
.done:
    push ds
    pop es                          ; caller's ES = DS restored
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the instrument list + song name (421,0)-(631,172), VGA only ---------------
tui_el_instr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, 421
    xor bx, bx
    mov cx, 631
    mov dx, 172
    call tui_clipc
    jc .out
    call tui_facec
    mov byte [tui_chn], 0           ; instrument row 0..11
.row:
    mov al, [tui_chn]
    xor ah, ah
    mov bl, 11
    mul bl
    add ax, 4
    mov dx, ax                      ; row y
    TSETC TC_FACET
    mov al, [tui_chn]
    inc al
    call tui_hex2
    mov si, tui_nbuf
    mov cx, 425
    call tui_textc
    mov al, [tui_chn]
    inc al
    call tui_insget
    cmp byte [tui_insbuf], 0
    je .skip
    mov si, tui_insbuf
    mov cx, 447
    call tui_textc
.skip:
    inc byte [tui_chn]
    cmp byte [tui_chn], 12
    jb .row
    mov ax, 424                     ; the song-name textbox
    mov bx, 155
    mov cx, 587
    mov dx, 168
    TSETC TC_BG
    call tui_fillc
    TSETC TC_BEVD
    call tui_framec
    TSETC TC_TEXT
    mov si, mp_title
    mov cx, 428
    mov dx, 158
    call tui_textc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The compact (CGA 640x200) top: one title/readout line
; =============================================================================
tui_top_cga:
    push ax
    push bx
    push cx
    push dx
    push si
    xor ax, ax
    xor bx, bx
    mov cx, [tui_vw]
    dec cx
    mov dx, 8
    call tui_clipc                  ; the whole line is one erase+text unit
    jc .vals
    TSETC TC_BG
    call tui_fillc
    TSETC TC_TEXT
    mov si, tui_s_name
    xor cx, cx
    xor dx, dx
    call tui_textc
    mov si, tui_s_bpmc
    mov cx, 64
    call tui_textc
    mov si, tui_s_spdc
    mov cx, 136
    call tui_textc
    mov si, tui_s_posc
    mov cx, 200
    call tui_textc
    mov si, tui_s_ptnc
    mov cx, 288
    call tui_textc
.vals:
    call tui_val_cga_bs
    call tui_val_cga_pos
    call tui_msg_draw
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tui_val_cga_pos:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, tui_nbuf                ; 'pp/ll'
    mov al, [mp_songpos]
    call tui_hexb
    mov byte [di], '/'
    inc di
    mov al, [mp_songlen]
    call tui_hexb
    mov byte [di], 0
    mov si, tui_nbuf
    mov cx, 232
    xor dx, dx
    mov bx, 40
    mov ah, TC_BG
    mov al, TC_TEXT
    call tui_rdout
    mov al, [mp_pattern]
    call tui_hex2
    mov si, tui_nbuf
    mov cx, 320
    mov bx, 16
    mov ah, TC_BG
    mov al, TC_TEXT
    call tui_rdout
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tui_val_cga_bs:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [mp_bpm]
    call tui_dec3
    mov si, tui_nbuf
    mov cx, 96
    xor dx, dx
    mov bx, 24
    mov ah, TC_BG
    mov al, TC_TEXT
    call tui_rdout
    mov al, [mp_speed]
    call tui_dec3
    mov si, tui_nbuf
    mov cx, 168
    mov bx, 24
    mov ah, TC_BG
    mov al, TC_TEXT
    call tui_rdout
    mov al, [mp_bpm]
    mov [tui_lbpm], al
    mov al, [mp_speed]
    mov [tui_lspd], al
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The pattern editor - the defining FT2 element: rows scrolling through a
; fixed full-width band, hex row numbers on both sides, four channel columns
; of 'C-2 01 A0F' cells.
; =============================================================================

; --- the channel digit strip + rule --------------------------------------------
tui_hdr:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [tui_lay + TL_CHX]
    mov bx, [tui_lay + TL_HDRY]
    mov cx, [tui_chend]
    mov dx, bx
    add dx, 8
    call tui_clipc
    jc .out
    TSETC TC_BG
    call tui_fillc
    TSETC TC_TEXT
    mov byte [tui_chn], 0
.d:
    mov al, [tui_chn]
    xor ah, ah
    mul word [tui_lay + TL_CHW]
    add ax, [tui_lay + TL_CHX]
    mov cx, [tui_lay + TL_CHW]
    shr cx, 1
    add cx, ax
    sub cx, 4
    mov dx, [tui_lay + TL_HDRY]
    mov al, [tui_chn]
    add al, '1'
    call tui_charc
    inc byte [tui_chn]
    cmp byte [tui_chn], 4
    jb .d
    TSETC TC_SEP
    mov ax, [tui_lay + TL_CHX]
    mov bx, [tui_chend]
    mov dx, [tui_lay + TL_HDRY]
    add dx, 8
    call tui_hlinec
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_viewrow - the row the band shows: the playing row, or the scroll row
;               (tracker.asm's Up/Down) while stopped
; out: [tui_cvr]
; -----------------------------------------------------------------------------
tui_viewrow:
    push ax
    mov al, [mp_row]
    cmp byte [mp_playing], 0
    jne .set
    mov al, [tui_vrow]
.set:
    mov [tui_cvr], al
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_row1 - one pattern row strip: gated whole (SPEC.md 11.3), erased, then
;            row numbers + four cells. Rows outside 0..63 erase to blank.
; in:  BX = row (signed word), DX = strip y (content), [tui_band] = 1 on the
;      current-row band; preserves all registers
; -----------------------------------------------------------------------------
tui_row1:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [tui_band], 0
    jne .band
    mov ax, [tui_lay + TL_RNL]
    mov cx, [tui_rend]
    push bx
    mov bx, dx                      ; y1
    add dx, 7
    call tui_clipc
    jc .covered
    TSETC TC_BG
    call tui_fillc
    TSETC TC_SEP                    ; the 1px channel separators the erase
    mov si, [tui_lay + TL_CHW]      ; just took out
    mov ax, [tui_lay + TL_CHX]
    add ax, si
    dec ax
    mov di, 3
.sep:
    call tui_vlinec
    add ax, si
    dec di
    jnz .sep
    mov [tui_t1], bx                ; text y
    pop bx
    jmp .content
.band:
    mov ax, 2
    mov cx, [tui_vw]
    sub cx, 3
    push bx
    mov bx, dx
    add dx, 10                      ; the band is 11 rows: BANDY..LOWY-1
    call tui_clipc
    jc .covered
    TSETC TC_BAND
    call tui_fillc
    mov [tui_t1], bx
    add word [tui_t1], 2            ; band text sits 2 rows in
    pop bx
    jmp .content
.covered:
    pop bx
    jmp .out
.content:
    cmp bx, 0
    jl .out
    cmp bx, 63
    jg .out
    mov al, TC_TEXT
    cmp byte [tui_band], 0
    je .col
    mov al, TC_BANDT
.col:
    call tui_setcol
    mov al, bl
    call tui_hex2                   ; the FT2 hex row number, both sides
    mov si, tui_nbuf
    mov cx, [tui_lay + TL_RNL]
    mov dx, [tui_t1]
    call tui_textc
    mov cx, [tui_lay + TL_RNR]
    cmp cx, 0FFFFh
    je .cells
    call tui_textc
.cells:
    mov byte [tui_chn], 0
.cl:
    mov al, [tui_chn]
    xor ah, ah
    mul word [tui_lay + TL_CHW]
    add ax, [tui_lay + TL_CHX]
    add ax, [tui_lay + TL_PAD]
    mov [tui_t2], ax                ; cell text x
    push bx
    mov al, [mp_pattern]
    xor ah, ah
    mov di, ax
    mov ax, bx
    mov ah, [tui_chn]               ; AL = row, AH = channel, DI = pattern
    call mp_cell2txt                ; -> mp_celltxt 'C-2 01 A0F'; clobbers
    pop bx
    jc .nx
    mov si, mp_celltxt
    mov cx, [tui_t2]
    mov dx, [tui_t1]
    call tui_textc
.nx:
    inc byte [tui_chn]
    cmp byte [tui_chn], 4
    jb .cl
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_draw_pat - the whole row area around the view row, and the change
;                markers tui_draw_dyn compares against
; -----------------------------------------------------------------------------
tui_marks:
    push ax
    call tui_viewrow
    mov al, [mp_row]
    mov [tui_lrow], al
    mov al, [mp_songpos]
    mov [tui_lpos], al
    mov al, [mp_pattern]
    mov [tui_lpat], al
    mov al, [tui_vrow]
    mov [tui_lvrow], al
    mov al, [tui_cvr]
    mov [tui_lcvr], al              ; the scroll path's delta baseline (45.12)
    pop ax
    ret

tui_draw_pat:
    push ax
    push bx
    push cx
    push dx
    call tui_marks
    mov byte [tui_band], 0
    mov cx, [tui_lay + TL_RAB]
    mov al, [tui_cvr]
    cbw
    sub ax, cx
    mov bx, ax                      ; first row = view - rowsAbove
    mov dx, [tui_lay + TL_ROWT]
.up:
    push cx
    call tui_row1
    inc bx
    add dx, 8
    pop cx
    loop .up
    mov byte [tui_band], 1          ; BX = the view row itself
    mov dx, [tui_lay + TL_BANDY]
    call tui_row1
    mov byte [tui_band], 0
    inc bx
    mov cx, [tui_lay + TL_RBE]
    mov dx, [tui_lay + TL_LOWY]
.lo:
    push cx
    call tui_row1
    inc bx
    add dx, 8
    pop cx
    loop .lo
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_scrl - the one-row scroll path (SPEC.md 45.12): when the view moved by
; exactly one row inside one pattern/position, the row area's content is the
; same pixels eight rows away - scroll the upper and lower regions (the band
; between them breaks the 8-row rhythm, so they move separately) and repaint
; three strips: band, the strip that left it, the row entering at the far
; edge. out CF=0 done (markers updated); CF=1 not eligible, or gfx_scroll
; refused (a clip cutting the area - the dialog) - caller runs tui_draw_pat,
; whose per-strip gates are then the correct renderer.
; -----------------------------------------------------------------------------
tui_scrl:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [mp_songpos]
    cmp al, [tui_lpos]
    jne .no
    mov al, [mp_pattern]
    cmp al, [tui_lpat]
    jne .no
    call tui_viewrow                ; [tui_cvr] = the row the band shows now
    mov al, [tui_cvr]
    sub al, [tui_lcvr]
    cmp al, 1
    je .up
    cmp al, -1
    jne .no
    mov si, -8                      ; view moved up: content moves down
    jmp .go
.up:
    mov si, 8                       ; view advanced: content moves up
.go:
    mov ax, [tui_lay + TL_RAB]      ; upper region: ROWT .. ROWT + RAB*8 - 1
    mov cl, 3
    shl ax, cl
    mov bx, [tui_lay + TL_ROWT]
    mov dx, bx
    add dx, ax
    dec dx
    xor ax, ax                      ; full width: x2+1 = vid_w, a multiple
    mov cx, [tui_vw]                ; of 8 on all three adapters
    dec cx
    call OSAPI_GFX_SCROLL
    jc .no
    mov ax, [tui_lay + TL_RBE]      ; lower region: LOWY .. LOWY + RBE*8 - 1
    mov cl, 3
    shl ax, cl
    mov bx, [tui_lay + TL_LOWY]
    mov dx, bx
    add dx, ax
    dec dx
    xor ax, ax
    mov cx, [tui_vw]
    dec cx
    call OSAPI_GFX_SCROLL
    jc .no                          ; half-scrolled: the full repaint the
                                    ; caller falls back to repairs it whole
    mov al, [tui_cvr]               ; the three strips (SPEC.md 45.12)
    cbw
    mov byte [tui_band], 0
    or si, si
    js .dstrips
    mov bx, ax                      ; up: the strip that LEFT the band is
    dec bx                          ; row view-1, the last upper strip...
    mov dx, [tui_lay + TL_RAB]
    dec dx
    mov cl, 3
    shl dx, cl
    add dx, [tui_lay + TL_ROWT]
    call tui_row1
    mov bx, ax                      ; ...and row view+RBE enters at the
    add bx, [tui_lay + TL_RBE]      ; last lower strip
    mov dx, [tui_lay + TL_RBE]
    dec dx
    mov cl, 3
    shl dx, cl
    add dx, [tui_lay + TL_LOWY]
    call tui_row1
    jmp .band
.dstrips:
    mov bx, ax                      ; down: row view-RAB enters at the first
    sub bx, [tui_lay + TL_RAB]      ; upper strip...
    mov dx, [tui_lay + TL_ROWT]
    call tui_row1
    mov bx, ax                      ; ...and the strip that LEFT the band is
    inc bx                          ; row view+1, the first lower strip
    mov dx, [tui_lay + TL_LOWY]
    call tui_row1
.band:
    mov bx, ax                      ; the band itself, on the new view row
    mov byte [tui_band], 1
    mov dx, [tui_lay + TL_BANDY]
    call tui_row1
    mov byte [tui_band], 0
    call tui_marks
    clc
    jmp .out
.no:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_row_lt - XT mode (SPEC.md 45.9): redraw only the band strip for the
; new row and settle the row marker; the rows around it stay until the
; position moves. tui_row1 carries the §11.3 clip gate, so a covering file
; dialog still blanks nothing.
; -----------------------------------------------------------------------------
tui_row_lt:
    push ax
    push bx
    push dx
    call tui_viewrow
    mov al, [mp_row]
    mov [tui_lrow], al
    mov al, [tui_cvr]
    cbw
    mov bx, ax
    mov byte [tui_band], 1
    mov dx, [tui_lay + TL_BANDY]
    call tui_row1
    mov byte [tui_band], 0
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; Volume bars (every worker frame) and the scope hit-test
; =============================================================================
tui_draw_vu:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [tui_chn], 0
.c:
    mov bl, [tui_chn]
    xor bh, bh
    mov al, [mp_chvol + bx]         ; 0..64, 0 while muted/silent
    cmp al, [tui_vu + bx]
    jbe .decay
    mov [tui_vu + bx], al           ; jump up, fall slowly: a VU needle
    jmp .have
.decay:
    mov al, [tui_vu + bx]
    cmp al, 2
    jae .sub
    xor al, al
    jmp .st
.sub:
    sub al, 2
.st:
    mov [tui_vu + bx], al
.have:
    xor ah, ah                      ; SI = bar width = vu * vuw / 64
    mul word [tui_vuw]
    mov cl, 6
    shr ax, cl
    mov si, ax
    mov al, [tui_chn]
    call tui_scellxy                ; [tui_t1]/[tui_t2] = cell origin
    mov dx, [tui_t2]                ; bar top y
    mov di, [tui_t1]
    add di, 4                       ; DI = bar left x (compact: +12, below)
    cmp word [tui_lay + TL_SCH], 16
    jb .cb
    add dx, [tui_lay + TL_SCH]
    sub dx, 13
    jmp .yb
.cb:
    inc dx                          ; compact cell: the bar shares rows
    add di, 8                       ; t2+1..t2+8 with the channel digit at
                                    ; t1+1..t1+8, so it starts at t1+12 -
                                    ; still left of the 'M' flag at t1+146
                                    ; (12 + 128-wide bar ends at t1+139)
.yb:
    mov bl, [tui_chn]               ; delta-draw (SPEC.md 45.12): paint only
    xor bh, bh                      ; the difference against the last drawn
    shl bx, 1                       ; width - a steady bar costs nothing
    mov ax, [tui_vupx + bx]
    cmp ax, si
    je .next
    mov [tui_vupx + bx], si
    jb .grow
    mov cx, di                      ; falling: erase [new .. old-1]
    add cx, ax
    dec cx
    mov ax, di
    add ax, si
    mov bx, dx
    add dx, 7
    TSETC TC_BG
    call tui_fillc
    jmp .next
.grow:
    mov cx, di                      ; rising: light [old .. new-1]
    add cx, si
    dec cx
    add ax, di
    mov bx, dx
    add dx, 7
    TSETC TC_VU
    call tui_fillc
.next:
    inc byte [tui_chn]
    cmp byte [tui_chn], 4
    jb .c
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_scopehit - which scope cell does a click land in? (fullscreen mute)
; in:  CX = x, DX = y (absolute screen); out CF=0 AL = channel 0..3, CF=1 miss
; preserves all other registers
; -----------------------------------------------------------------------------
tui_scopehit:
    push bx
    push cx
    push dx
    push si
    push di
    sub cx, [tui_ox]
    sub dx, [tui_oy]
    mov al, 0
.h:
    call tui_scellxy
    mov bx, [tui_t1]
    cmp cx, bx
    jb .n
    add bx, [tui_lay + TL_SCW]
    cmp cx, bx
    jae .n
    mov bx, [tui_t2]
    cmp dx, bx
    jb .n
    add bx, [tui_lay + TL_SCH]
    cmp dx, bx
    jae .n
    clc
    jmp .done
.n:
    inc al
    cmp al, 4
    jb .h
    stc
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; The top-area redraw (full draw, and the every-16th-frame refresh that also
; erases the menu-bar strip a cancelled file dialog leaves on a fullscreen
; surface - SPEC.md 38.6/45)
; =============================================================================
tui_draw_top:
    push ax
    push bx
    push cx
    push dx
    test byte [tui_lay + TL_FLAGS], TLF_CMPCT
    jnz .cga
    mov ax, [tui_lay + TL_SLX]      ; the sliver right of the top blocks:
    cmp ax, 0FFFFh                  ; nothing else repaints those rows
    je .els
    xor bx, bx
    mov cx, [tui_vw]
    dec cx
    mov dx, MBAR_H - 1
    call tui_deskfill
.els:
    call tui_el_posed
    call tui_el_logo
    call tui_el_bpm
    call tui_el_ptn
    call tui_el_status
    call tui_el_scopes
    test byte [tui_lay + TL_FLAGS], TLF_MENUS
    jz .nm
    call tui_el_menus
.nm:
    test byte [tui_lay + TL_FLAGS], TLF_INSTR
    jz .hdr
    call tui_el_instr
    jmp .hdr
.cga:
    xor ax, ax                      ; compact: erase rows 0..MBAR_H-1 whole
    xor bx, bx                      ; before the elements - the title line
    mov cx, [tui_vw]                ; and scope cells leave 2px gap columns
    dec cx                          ; nothing else repaints, and a cancelled
    mov dx, MBAR_H - 1              ; file dialog's menu-bar strip would
    TSETC TC_BG                     ; persist in them (a pure fill: per-pixel
    call tui_fillc                  ; clip, never gated - SPEC.md 45.6)
    call tui_top_cga
    call tui_el_scopes
.hdr:
    call tui_hdr
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The two exported redraws
; =============================================================================

; -----------------------------------------------------------------------------
; tui_draw_all - full redraw for the current mode; lock held by the caller
; (W_PAINT, a key/menu/completion handler - never the worker)
; -----------------------------------------------------------------------------
tui_draw_all:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [tui_inited], 0
    je .out
    call tui_track
    cmp byte [trk_fs], 0
    je .win
    test byte [tui_lay + TL_FLAGS], TLF_CMPCT
    jnz .cbg
    xor ax, ax                      ; desktop band, then the black pattern
    xor bx, bx                      ; field below it
    mov cx, [tui_vw]
    dec cx
    mov dx, [tui_lay + TL_PATY]
    dec dx
    call tui_deskfill
    xor ax, ax
    mov bx, [tui_lay + TL_PATY]
    mov dx, [tui_vh]
    dec dx
    TSETC TC_BG
    call tui_fillc
    jmp .top
.cbg:
    xor ax, ax
    xor bx, bx
    mov cx, [tui_vw]
    dec cx
    mov dx, [tui_vh]
    dec dx
    TSETC TC_BG
    call tui_fillc
.top:
    call tui_draw_top
    call tui_draw_pat
    jmp .about
.win:
    TSETC TC_WBG                    ; the windowed splash card
    xor ax, ax
    xor bx, bx
    mov cx, [tui_cw]
    dec cx
    mov dx, [tui_ch]
    dec dx
    call tui_fillc
    mov si, tui_s_splash1
    mov dx, 16
    call tui_cline
    mov si, tui_s_splash2
    mov dx, 36
    call tui_cline
    mov si, mp_title
    cmp byte [mp_loaded], 0
    jne .title
    mov si, tui_s_none
.title:
    mov dx, 56
    call tui_cline
    mov si, tui_s_keys1
    mov dx, 76
    call tui_cline
    mov si, tui_s_keys2
    mov dx, 88
    call tui_cline
    mov si, tui_s_anykey
    mov dx, 108
    call tui_cline
    call tui_msg_draw
.about:
    cmp byte [trk_abon], 0
    je .out
    call tui_about_panel
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_cline - a centred splash line: one gated erase+text pair on white
; in:  SI = NUL string, DX = y (content)
; -----------------------------------------------------------------------------
tui_cline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_FONT_WIDTH           ; AX = pixel width
    mov di, [tui_cw]
    sub di, ax
    shr di, 1                       ; DI = centred x
    mov ax, 4
    mov bx, dx
    mov cx, [tui_cw]
    sub cx, 5
    push dx
    add dx, 7
    call tui_clipc
    jc .skip
    TSETC TC_WBG
    call tui_fillc
    TSETC TC_WTX
    pop dx
    mov cx, di
    call tui_textc
    jmp .out
.skip:
    pop dx
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_draw_dyn - the worker's frame (lock held, clip armed by trk_render):
;                pattern + readouts when the view moved, VU bars always,
;                the top band every 16th call
; -----------------------------------------------------------------------------
tui_draw_dyn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [tui_inited], 0
    je .out
    call tui_track
    cmp byte [trk_fs], 0
    je .win
    inc byte [tui_frame]
    mov al, [mp_row]                ; did the view move?
    cmp al, [tui_lrow]
    jne .rows
    mov al, [mp_songpos]
    cmp al, [tui_lpos]
    jne .rows
    mov al, [mp_pattern]
    cmp al, [tui_lpat]
    jne .rows
    mov al, [tui_vrow]
    cmp al, [tui_lvrow]
    je .norows
.rows:
    cmp byte [mp_xt], 0             ; XT (SPEC.md 45.9): a row-only change
    je .rfull                       ; relights the band alone; the full
    mov al, [mp_songpos]            ; repaint waits for a position/pattern
    cmp al, [tui_lpos]              ; (or stopped-scroll) move
    jne .rfull
    mov al, [mp_pattern]
    cmp al, [tui_lpat]
    jne .rfull
    mov al, [tui_vrow]
    cmp al, [tui_lvrow]
    jne .rfull
    call tui_row_lt
    jmp .norows
.rfull:
    call tui_scrl                   ; one-row delta: scroll + three strips
    jnc .norows                     ; (SPEC.md 45.12; no readouts change on
                                    ; a row-only move)
    call tui_draw_pat
    test byte [tui_lay + TL_FLAGS], TLF_CMPCT
    jnz .rowc
    call tui_el_posed
    call tui_el_ptn
    jmp .norows
.rowc:
    call tui_val_cga_pos
.norows:
    mov al, [mp_bpm]                ; did the tempo?
    cmp al, [tui_lbpm]
    jne .bs
    mov al, [mp_speed]
    cmp al, [tui_lspd]
    je .nobs
.bs:
    test byte [tui_lay + TL_FLAGS], TLF_CMPCT
    jnz .bsc
    call tui_el_bpm
    jmp .nobs
.bsc:
    call tui_val_cga_bs
.nobs:
    mov ax, [mp_mute]               ; did a mute toggle?
    cmp ax, [tui_lmute]
    jne .mute
    mov ax, [mp_mute + 2]
    cmp ax, [tui_lmute + 2]
    je .nomute
.mute:
    call tui_el_scopes
.nomute:
    call tui_draw_vu
    mov al, [tui_frame]
    and al, 15
    jnz .out
    call tui_draw_top               ; the fdlg-cancel menu-bar eraser
    jmp .out
.win:
    cmp byte [mp_playing], 0        ; windowed: a live position line on the
    je .out                         ; splash while something plays
    mov al, [mp_row]
    cmp al, [tui_lrow]
    jne .wupd
    mov al, [mp_songpos]
    cmp al, [tui_lpos]
    je .out
.wupd:
    mov al, [mp_row]
    mov [tui_lrow], al
    mov al, [mp_songpos]
    mov [tui_lpos], al
    mov di, tui_pbuf
    mov si, tui_s_posl
    call tui_scpy
    mov al, [mp_songpos]
    call tui_hexb
    mov si, tui_s_rowl
    call tui_scpy
    mov al, [mp_row]
    call tui_hexb
    mov byte [di], 0
    mov si, tui_pbuf
    mov cx, 8
    mov dx, [tui_ch]
    sub dx, 22
    mov bx, 120
    mov ah, TC_WBG
    mov al, TC_WTX
    call tui_rdout
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_about_panel - the credits, panel-in-content (the ark_about pattern):
;                   drawn by the UI task while [trk_abon] holds the worker's
;                   frames off, so nothing paints over it
; -----------------------------------------------------------------------------
tui_about_panel:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [tui_cw]
    sub ax, 240
    shr ax, 1
    mov [tui_t1], ax                ; panel left
    mov ax, [tui_ch]
    sub ax, 84
    shr ax, 1
    mov [tui_t2], ax                ; panel top
    mov ax, [tui_t1]
    mov bx, [tui_t2]
    mov cx, ax
    add cx, 239
    mov dx, bx
    add dx, 83
    TSETC TC_WBG
    call tui_fillc
    TSETC TC_WTX
    call tui_framec
    mov di, tui_ablines
    mov dx, [tui_t2]
    add dx, 10
.line:
    mov si, [di]
    or si, si
    jz .out
    push dx
    push di
    call OSAPI_FONT_WIDTH           ; AX = width
    mov cx, 240
    sub cx, ax
    shr cx, 1
    add cx, [tui_t1]
    pop di
    pop dx
    call tui_textc
    add di, 2
    add dx, 12
    jmp .line
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Data: layout records, colour tables, strings
; =============================================================================

; VGA 640x480 - the FT2-proportioned layout (research: FT2 is 632x400 with
; the pattern from y=173; the extra 80 rows go to the pattern view)
tui_lay_vga:
    dw 172, 175, 185, 16, 313, 324, 18         ; PATY HDRY ROWT RAB BANDY LOWY RBE
    dw 8, 608, 28, 145, 28                     ; RNL RNR CHX CHW PAD
    dw 2, 94, 145, 39, 143, 38, 2              ; SCX SCY SCDX SCDY SCW SCH SCPR
    dw 4, 80, 282, 632                         ; STX STY STW SLX
    dw TLF_INSTR | TLF_MENUS                   ; FLAGS

; Hercules 720x348 - the mid layout: same top blocks, one scope row, no
; instrument list or button stacks, channel block centred in 720
tui_lay_hrc:
    dw 92, 95, 105, 14, 217, 228, 14
    dw 8, 656, 70, 145, 28
    dw 291, 2, 106, 0, 104, 44, 4
    dw 4, 80, 282, 291              ; SLX 291, not 714: the sliver fill must
                                    ; cover ALL of rows 0..MBAR_H-1 right of
                                    ; the logo (rows 0..1 and the scope-gap
                                    ; columns repaint nowhere else), or a
                                    ; cancelled file dialog's menu-bar strip
                                    ; persists there for the session
    dw 0

; CGA 640x200 - compact: title/readout line, inline bars, 10+10 rows
tui_lay_cga:
    dw 0, 19, 28, 10, 108, 119, 10
    dw 4, 0FFFFh, 24, 152, 32
    dw 0, 9, 160, 0, 158, 10, 4
    dw 352, 0, 284, 0FFFFh
    dw TLF_CMPCT

; colour tables (TC_N bytes each; see the role list at the top)
tui_col_vga:                        ; FT2 'Arctic' mapped to 16 colours
    db 0, 9, 7, 15, 7, 0, 15, 8, 7, 0, 10, 7, 1, 15, 7, 0, 15, 0
tui_col_mono:                       ; 1bpp: no colour 9 anywhere (SPEC.md 39.4)
    db 0, 15, 15, 0, 0, 15, 15, 15, 0, 15, 15, 15, 0, 15, 0, 15, 15, 0

tui_hexd:    db '0123456789ABCDEF'

; labels
tui_s_name:  db 'TRACKER', 0
tui_s_logo:  db 'T R A C K E R', 0
tui_s_bpm:   db 'BPM', 0
tui_s_spd:   db 'Spd', 0
tui_s_ptn:   db 'Ptn', 0
tui_s_ln:    db 'Ln', 0
tui_s_64:    db '64', 0
tui_s_np:    db 'Np', 0
tui_s_len:   db 'Len', 0
tui_s_posl2: db 'Pos', 0
tui_s_mute:  db 'MUTE', 0
tui_s_bpmc:  db 'BPM:', 0
tui_s_spdc:  db 'SPD:', 0
tui_s_posc:  db 'POS:', 0
tui_s_ptnc:  db 'PTN:', 0
tui_s_posl:  db 'Pos ', 0
tui_s_rowl:  db '  Row ', 0

; the FT2 button stacks (decorative in v1; each names its key)
tui_mbl:     dw tui_b_about, tui_b_mute, tui_b_scroll, tui_b_pos, tui_b_exit, 0
tui_mbr:     dw tui_b_play, tui_b_ppat, tui_b_stop, tui_b_load, tui_b_full, 0
tui_b_about: db 'About', 0
tui_b_mute:  db 'Mute1-4', 0
tui_b_scroll:db 'Rows'
             db 0
tui_b_pos:   db 'Pos <->', 0
tui_b_exit:  db 'Esc=win', 0
tui_b_play:  db 'Play', 0
tui_b_ppat:  db 'Pattern', 0
tui_b_stop:  db 'Stop', 0
tui_b_load:  db 'Load L', 0
tui_b_full:  db 'Full F', 0

; the windowed splash
tui_s_splash1: db 'T R A C K E R', 0
tui_s_splash2: db 'A FastTracker II style MOD player', 0
tui_s_none:    db 'No module loaded', 0
tui_s_keys1:   db 'ENTER play  SPACE stop  P pattern', 0
tui_s_keys2:   db 'ARROWS position  1-4 mute  L load  X xt', 0
tui_s_anykey:  db 'Press any key for fullscreen', 0
tui_s_hint:    db 'ENTER play  SPACE stop  L load', 0

; the About panel
tui_ablines:
    dw tui_ab1, tui_ab2, tui_ab3, tui_ab4, tui_ab5, 0
tui_ab1:     db 'Tracker for os8088', 0
tui_ab2:     db 'A FastTracker II homage', 0
tui_ab3:     db 'MOD replay in the style of', 0
tui_ab4:     db '8bitbubsy/ft2-clone', 0
tui_ab5:     db 'Any key closes', 0

; =============================================================================
; trkui bss (through tracker.asm's TRKB/TRKW/TRKBUF accumulator)
; =============================================================================
    TRKB tui_inited                 ; layout + colours are live
    TRKB tui_mono                   ; 1bpp adapter (SPEC.md 39.4)
    TRKW tui_vw                     ; the live screen (OSAPI_VIDEO)
    TRKW tui_vh
    TRKW tui_ox                     ; content origin + size, re-read per draw
    TRKW tui_oy
    TRKW tui_cw
    TRKW tui_ch
    TRKBUF tui_lay, TL_SIZE         ; the chosen layout record
    TRKBUF tui_col, TC_N            ; the chosen colour table
    TRKW tui_chend                  ; right edge of the channel block
    TRKW tui_rend                   ; right edge of a row strip
    TRKW tui_vuw                    ; volume bar full width
    TRKB tui_frame                  ; worker frame counter (top band each 16)
    TRKB tui_vrow                   ; the Up/Down scroll row while stopped
    TRKB tui_cvr                    ; the view row this frame
    TRKB tui_band                   ; tui_row1: drawing the band row
    TRKB tui_chn                    ; shared small loop counter
    TRKB tui_lrow                   ; last-drawn markers for tui_draw_dyn
    TRKB tui_lpos
    TRKB tui_lpat
    TRKB tui_lvrow
    TRKB tui_lbpm
    TRKB tui_lspd
    TRKBUF tui_lmute, 4
    TRKBUF tui_vu, 4                ; decaying VU levels
    TRKBUF tui_vupx, 8              ; last-drawn bar widths, px (SPEC.md
                                    ; 45.12 - the delta-draw baseline)
    TRKB tui_lcvr                   ; last-drawn view row (the scroll path's
                                    ; one-row-delta detector, SPEC.md 45.12)
    TRKW tui_msgp                   ; status-line string, 0 = the key hint
    TRKB tui_tcol                   ; tui_rdout scratch
    TRKB tui_ecol
    TRKW tui_t1                     ; shared scratch words (serial: every
    TRKW tui_t2                     ; drawer runs under the one gfx lock)
    TRKBUF tui_nbuf, 8              ; number/text compose buffers
    TRKBUF tui_pbuf, 20
    TRKBUF tui_insbuf, 20
