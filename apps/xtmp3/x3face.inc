; =============================================================================
; os8088 - apps/xtmp3/x3face.inc - XTMP3's face (SPEC.md 121.8, 121.9)
;
; THE ENGINE BOUNDARY FIRST, because it is the contract the later waves are
; written to: EVERYTHING THE FACE SHOWS IS IN ONE RECORD, x3_st (X3S_* below).
; The engine (apps/xtmp3/x3eng.inc, S8/S9 - and until then the demo driver,
; x3demo.inc) WRITES it and calls x3f_update, which redraws exactly what
; changed. The face READS it, and writes only the two fields that are the
; user's to set: X3S_QWANT (the quality override) and X3S_VOL (the knob),
; telling the engine through x3_e_qual and x3_e_vol when it does.
;
; FOUR LAYOUTS, one record each (L_*), chosen at every full paint from the
; content box the window was given (x3f_setup):
;
;   FULL      512 x 352  VGA's 480 lines: the whole mockup, the queue 3 rows
;   MID       512 x 248  EGA's 350 and Hercules' 348: the same, its art cut
;                        for the taller pixels, the queue 2 rows
;   COMPACT   440 x 88   the CGA's 200 lines: the LCD, the progress bar, the
;   (C1)                 transport and OPEN, the status line, at half rows
;   COMPACT   440 x 172  the same with FULL's art: what Options > Compact
;   (C2)                 Player gives a VGA, an EGA or a Hercules
;
; TWO THEMES, one ROLE table each (x3draw.inc): colour, which needs a display
; of more than one bit, and black and white, which is every display's.
;
; THE STATIC PARTS are a DECO LIST per layout - rects, frames, pictures and
; labels as data (x3f_deco) - drawn at a full paint and never again.
; EVERYTHING THAT CHANGES is an ELEMENT (L_ELEMS): a proc that works out a
; KEY - the whole of what it would draw, in sixteen bits - and draws only when
; the key differs from the one its slot remembers. The paint forgets every key
; and runs the same elements, so the incremental draw and the full paint are
; one code path. A field the clip cut is forgotten too (x3f_ekdone).
; =============================================================================

; =============================================================================
; THE STATE RECORD (x3_st) - the engine writes, the face reads
; =============================================================================
X3S_STATE   equ 0                   ; byte: X3ST_*
X3S_QUAL    equ 1                   ; byte: X3Q_*, the rung RUNNING (AUTO = none yet)
X3S_QWANT   equ 2                   ; byte: X3Q_*, the user's override (AUTO =
                                    ;       none). THE FACE WRITES IT
X3S_FLAGS   equ 3                   ; byte: X3SF_*
X3S_ELAP    equ 4                   ; word: seconds played (or decoded)
X3S_TOTAL   equ 6                   ; word: the track's seconds
X3S_RATE    equ 8                   ; word: the OUTPUT rate, Hz
X3S_CHANS   equ 10                  ; byte: output channels, 1 or 2; 0 = none yet
X3S_BITS    equ 11                  ; byte: output bits, 8 or 16
X3S_LEVEL   equ 12                  ; byte: the BUFFER gauge, 0..255 - the PCM
                                    ;       ring's fill live, conversion decoding
X3S_METL    equ 13                  ; byte: the left (or the mono) meter, 0..255
X3S_METR    equ 14                  ; byte: the right meter, 0..255
X3S_PCT     equ 15                  ; byte: the conversion's percentage, 0..100
X3S_SPEED   equ 16                  ; word: the measured speed x 100 (31 = 0.31x)
X3S_UNDER   equ 18                  ; word: underruns counted
X3S_PEAK    equ 20                  ; word: peak memory, KB
X3S_ERR     equ 22                  ; word: the error's NUL text in OUR segment
X3S_VOL     equ 24                  ; byte: 0..X3_VMAX, the knob. THE FACE WRITES IT
X3S_CUR     equ 25                  ; byte: the queue entry shown, FFh none
X3S_TITLE   equ 26                  ; 41 bytes: its title, NUL-ended (40 characters)
X3S_SIZE    equ 68

X3ST_IDLE   equ 0                   ; the states (SPEC.md 121.8)
X3ST_CHECK  equ 1                   ; Checking performance
X3ST_CHOOSE equ 2                   ; Choosing output drive
X3ST_DECODE equ 3                   ; Decoding to disk NN%
X3ST_READY  equ 4                   ; Ready to play
X3ST_LIVE   equ 5                   ; Playing live
X3ST_FILE   equ 6                   ; Playing decoded file
X3ST_PAUSED equ 7                   ; Paused
X3ST_BUFFER equ 8                   ; Buffering
X3ST_DONE   equ 9                   ; Finished
X3ST_ERROR  equ 10                  ; Error: X3S_ERR says what
X3ST_N      equ 11

X3Q_AUTO    equ 0                   ; the quality ladder (SPEC.md 121.5)
X3Q_ORIG    equ 1
X3Q_HALF    equ 2
X3Q_QUART   equ 3
X3Q_XTSTD   equ 4
X3Q_XTFAST  equ 5
X3Q_XTTURBO equ 6
X3Q_XTSPCH  equ 7
X3Q_N       equ 8

X3SF_EST    equ 0x01                ; the total is an ESTIMATE: shown with a '~'
X3SF_NOTOT  equ 0x02                ; no total known: '--:--'
X3SF_DEVLIM equ 0x04                ; the format is the DEVICE's limit, not a
                                    ;   step down: the LCD says DEVICE
X3SF_VOLOK  equ 0x08                ; the knob does something (a mixer, or the
                                    ;   decoder's gain); clear = greyed

X3_VMAX     equ X3ART_KSTEPS - 1    ; the knob's detents: 0..10

; =============================================================================
; THE LAYOUT RECORD
; =============================================================================
L_W         equ 0                   ; the content it was cut for
L_H         equ 2
L_DECO      equ 4                   ; the static parts: a deco list (x3f_deco)
L_ELEMS     equ 6                   ; the elements: (proc, param) pairs, 0 ends
L_BTNS      equ 8                   ; X3_NB rects, content-relative, inclusive
L_ICONS     equ 10                  ; X3_NB picture pointers (0 = none)
L_CAPS      equ 12                  ; X3_NB caption pointers (0 = none)
L_TTLX      equ 14                  ; the LCD's title line: x, y, its face (0 =
L_TTLY      equ 16                  ; the system font), its characters
L_TTLF      equ 18
L_TTLN      equ 20
L_TIMX      equ 22                  ; the elapsed time: x, y, its digit set
L_TIMY      equ 24
L_TIMS      equ 26
L_TOTX      equ 28                  ; '/ 04:38': x, y, its (thin) digit set
L_TOTY      equ 30
L_TOTS      equ 32
L_PB        equ 34                  ; the progress bar's INSIDE: x1, y1, x2, y2
L_FMTX      equ 42                  ; 'MONO': x, y; the rate's x
L_FMTY      equ 44
L_FMTX2     equ 46
L_DIAL      equ 48                  ; the dials' geometry (DG_*), 0 = none
L_KNOB      equ 50                  ; the knob's (KG_*), 0 = none
L_Q         equ 52                  ; the queue's (QG_*), 0 = none
L_SBY       equ 54                  ; the status line: its text's y...
L_SBQX      equ 56                  ; ...the quality's x and characters...
L_SBQN      equ 58
L_SBSX      equ 60                  ; ...the state's x and characters
L_SBSN      equ 62
L_CMP       equ 64                  ; 1 = a compact layout
L_SIZE      equ 66

DG_X0       equ 0                   ; the dials: the left face's x, the right's
DG_X1       equ 2
DG_Y        equ 4                   ; their y and rows
DG_H        equ 6
DG_K        equ 8                   ; the black mask, the red, the black and
DG_R        equ 10                  ; white face (x3art_dial*)
DG_BW       equ 12
DG_ND       equ 14                  ; the needle's ends, X3ART_NPOS (x, y) bytes
DG_PX       equ 16                  ; the pivot
DG_PY       equ 18
DG_SVU      equ 20                  ; the printed scale: VU, and BUFFER
DG_SBUF     equ 22
DG_LY       equ 24                  ; the labels under them: y, the two fields'
DG_LX0      equ 26                  ; x, their width
DG_LX1      equ 28
DG_LW       equ 30

KG_X        equ 0                   ; the knob's canvas: where, how big
KG_Y        equ 2
KG_W        equ 4
KG_H        equ 6
KG_KX       equ 8                   ; the knob in it (KX even: the 4bpp canvas
KG_KY       equ 10                  ; is two pixels a byte)
KG_P0       equ 12                  ; its two planes, its black and white face
KG_P1       equ 14
KG_BW       equ 16
KG_ROWS     equ 18
KG_IND      equ 20                  ; the indicator's lines, the tick ring's
KG_RING     equ 22

QG_X        equ 0                   ; the queue: its text's x...
QG_Y0       equ 2                   ; ...the first row's top, the pitch, rows
QG_PITCH    equ 4
QG_N        equ 6
QG_ABOVE    equ 8                   ; a row: ground above the text, below it
QG_BELOW    equ 10                  ; (the selection bar: above + 8 + below)
QG_SBX      equ 12                  ; the scroll bar: x, the trough's top and
QG_SBT1     equ 14                  ; bottom
QG_SBT2     equ 16

X3_QCH      equ 57                  ; a queue row's characters
X3F_RATECH  equ 22                  ; the rate's field: '44.1 kHz 16-BIT DEVICE'
X3F_DIALT   equ 5                   ; ticks between needle draws: 3.6 a second,
                                    ; under SPEC.md 121.8's four

X3_FW       equ 512                 ; the content boxes (the four layouts)
X3_FH       equ 352
X3_MH       equ 248
X3_CW       equ 440
X3_C1H      equ 88
X3_C2H      equ 172

; =============================================================================
; THE ART PART - every picture, font and line table the face draws from
; =============================================================================
; tools/os88x3art.py lays them out as ONE part of XTMP3.O88 (OP_ASSET, SPEC.md
; 20.12), so they cost the primary segment nothing but this: x3art.inc, whose
; x3art_ labels are OFFSETS in the part, and [x3_artseg]. Every composer that
; reads a picture (x3d_bor, x3d_bpic, x3d_picw, x3d_bstr, x3d_bdigs,
; x3d_line4, and here the dial faces, the needle, the scale and the knob's
; planes) loads ES from [x3_artseg] ONCE A CALL - a glyph, a picture, a field
; - and never once a byte. Eager, because every adapter draws from it at the
; first paint, and LZ4 on the disk (OP_COMP).
X3_PART_ART equ 0                   ; its row in xtmp3.asm's part table
%define X3_ARTFLAGS OP_COMP

; x3f_artseg - [x3_artseg] = the art part's segment: once, from the entry
; proc after op_load. The part's claim is the carve's, and a claim is born
; pinned (SPEC.md 66), so the segment never moves. clobbers AX
x3f_artseg:
    mov al, X3_PART_ART
    call op_seg
    mov [x3_artseg], ax
    ret

; =============================================================================
; SETUP
; =============================================================================

; -----------------------------------------------------------------------------
; x3f_setup - at a full paint: the display, the theme, the layout, the origin
; in:  BX = the window.  clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
x3f_setup:
    push bx
    call OSAPI_WM_DISPLAY           ; DH = bits a pixel
    pop bx
    mov byte [x3d_bpp1], 0
    mov ax, x3_tcol
    cmp dh, 1
    ja .colour
    mov byte [x3d_bpp1], 1          ; one bit: black and white whatever was
    jmp short .bw                   ; chosen (the colour item is grey)
.colour:
    cmp byte [x3_theme], 0
    je .th
.bw:
    mov ax, x3_tbw
.th:
    mov [x3d_th], ax
    call OSAPI_WM_GEOM              ; CX = content width, DX = height
    mov [x3f_cw], cx
    mov [x3f_ch], dx
    mov si, x3_lfull                ; the largest that fits, unless the user
    cmp byte [x3_want], 0           ; asked for the compact one
    jne .cmp
    cmp cx, X3_FW
    jb .cmp
    cmp dx, X3_FH
    jae .lay
    mov si, x3_lmid
    cmp dx, X3_MH
    jae .lay
.cmp:
    mov si, x3_lc2
    cmp dx, X3_C2H
    jae .lay
    mov si, x3_lc1
.lay:
    mov [x3_lp], si
    jmp x3d_origin

; =============================================================================
; THE ELEMENT CACHE
; =============================================================================

; x3f_ek - AX = the key this element would draw. CF = 1: the window shows it
; already. CF = 0: draw, then x3f_ekdone. Keeps every register
x3f_ek:
    push si
    mov si, [x3f_slot]
    cmp [si], ax
    pop si
    je .same
    mov [x3f_kpend], ax
    mov byte [x3d_cutf], 0
    clc
    ret
.same:
    stc
    ret

; x3f_ekdone - the window shows the pending key now - unless the clip cut it,
; when it is forgotten and the next pass draws it again. Keeps every register
x3f_ekdone:
    push ax
    push si
    mov ax, [x3f_kpend]
    cmp byte [x3d_cutf], 0
    je .keep
    mov ax, 0xFFFF
.keep:
    mov si, [x3f_slot]
    mov [si], ax
    pop si
    pop ax
    ret

; x3f_forget - every key and every button's flags owed: the next pass draws all
x3f_forget:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov ax, 0xFFFF
    mov di, x3f_keys
    mov cx, X3F_NKEYS
    cld
    rep stosw
    mov di, x3_bflags
    mov cx, X3_NB
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3f_update - THE ENGINE'S ONE CALL: redraw what x3_st changed, where the
;              window can be seen. Nothing while the About card is up (it is
;              opaque over the face, and its dismissal repaints everything)
; in:  BX = the window; the gfx lock HELD - every window callback holds it. A
;      handler that does not (OSAPI_WM_ONWAKE's) calls x3f_poke instead.
; out: every register preserved
; -----------------------------------------------------------------------------
x3f_update:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [x3_btrec+OS88UI_BT_N], 0
    je .out
    call OSAPI_WM_CLIP_SET          ; CF = 1: not one visible pixel
    jc .out
    call x3d_origin
    mov byte [x3f_full], 0
    xor dl, dl
    call x3f_walk
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3f_poke - x3f_update for a caller WITHOUT the gfx lock: the lock taken and
; given back round it. Every register preserved
x3f_poke:
    push bx
    call OSAPI_GFX_LOCK
    mov bx, [x3_win]
    call x3f_update
    call OSAPI_GFX_UNLOCK
    pop bx
    ret

; -----------------------------------------------------------------------------
; x3f_walk - the menus' live state, the queue's follow, every element, then
;            the buttons. DL = 1 draw every button (the paint), 0 the changed
; in:  BX = the window; the lock held, the clip armed, x3d_origin current
; clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
x3f_walk:
    push bx
    push dx
    call x3_menulive
    call x3_qfollow
    mov si, [x3_lp]
    mov si, [si+L_ELEMS]
    mov di, x3f_keys
.next:
    mov ax, [si]
    or ax, ax
    jz .btns
    mov cx, [si+2]
    mov [x3f_slot], di
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
    jmp x3_regrey

; -----------------------------------------------------------------------------
; x3f_paint - W_PAINT: the layout's static parts, every element and every
;             button; the About card last
; in:  SI = the window (gfx lock held; NOT white-filled - OSAPI_WM_OWNBG).
;      Keeps SI
; -----------------------------------------------------------------------------
x3f_paint:
    push si
    mov bx, si
    call x3f_setup
    push bx
    call OSAPI_WM_DISPLAY           ; the band, for x3_canbig
    sub cx, si
    mov [x3_dband], cx
    mov [x3_dwid], ax
    pop bx
    mov si, [x3_lp]
    mov si, [si+L_DECO]
    call x3f_deco
    call x3f_forget
    mov byte [x3f_full], 1
    mov dl, 1
    call x3f_walk
    pop si
    cmp byte [x3_btrec+OS88UI_BT_N], 0 ; ...and the About card LAST
    jne .out
    mov bx, si
    mov si, x3_ablines
    call os88ui_about_d             ; the paint's region is armed; it keeps
    mov si, bx                      ; every register
.out:
    ret

; =============================================================================
; THE STATIC PARTS - a deco list per layout: an op word, then its arguments
; =============================================================================
D_END       equ 0
D_RECT      equ 1                   ; role, x1, y1, x2, y2
D_FRAME     equ 2                   ; role, x1, y1, x2, y2
D_PIC       equ 3                   ; picture, x, y, roles (ink | paper << 8)
D_STR       equ 4                   ; face, string, x, y, roles
D_CSTR      equ 5                   ; face, string, the CENTRE's x, y, roles
D_KNOBTX    equ 6                   ; (no arguments) the knob's printed parts

; x3f_deco - SI = a deco list: drawn. in BX = the window. clobbers AX, CX, DX,
; SI, DI
x3f_deco:
.next:
    lodsw
    or ax, ax
    jz .out
    mov di, ax                      ; the op: its arguments into x3f_dargs
    mov cl, [x3f_dnarg+di]
    xor ch, ch
    push di
    mov di, x3f_dargs
    push es
    push ds
    pop es
    cld
    rep movsw
    pop es
    pop di
    push si
    push bx
    shl di, 1
    call [x3f_dops+di]
    pop bx
    pop si
    jmp short .next
.out:
    ret

x3f_dnarg:  db 0, 5, 5, 4, 5, 5, 0
x3f_dops:   dw 0, x3f_do_rect, x3f_do_frame, x3f_do_pic, x3f_do_str
            dw x3f_do_cstr, x3f_do_knobtx

x3f_do_rect:
    call x3f_dget
    jmp x3d_rect
x3f_do_frame:
    call x3f_dget
    jmp x3d_frame
; x3f_dget - the five arguments: SI = the role, AX, BX, CX, DX the rect
x3f_dget:
    mov si, [x3f_dargs]
    mov ax, [x3f_dargs+2]
    mov bx, [x3f_dargs+4]
    mov cx, [x3f_dargs+6]
    mov dx, [x3f_dargs+8]
    ret
x3f_do_pic:
    mov si, [x3f_dargs]
    mov ax, [x3f_dargs+2]
    mov bx, [x3f_dargs+4]
    mov dx, [x3f_dargs+6]
    jmp x3d_picat
x3f_do_str:
    call x3f_dsget
    jmp x3d_strat
x3f_do_cstr:
    call x3f_dsget
    call x3d_strw                   ; CX = its width: centred on AX
    shr cx, 1
    sub ax, cx
    jmp x3d_strat
; x3f_dsget - DI = the face, SI = the string, AX, BX = x, y, DX = the roles
x3f_dsget:
    mov di, [x3f_dargs]
    mov si, [x3f_dargs+2]
    mov ax, [x3f_dargs+4]
    mov bx, [x3f_dargs+6]
    mov dx, [x3f_dargs+8]
    ret

; x3f_do_knobtx - VOLUME over the knob and MIN, MAX at its ends, from the
; knob's own geometry so the words cannot drift from the ring
x3f_do_knobtx:
    mov si, [x3_lp]
    mov si, [si+L_KNOB]
    mov ax, [si+KG_W]               ; VOLUME, centred over the canvas
    shr ax, 1
    add ax, [si+KG_X]
    mov bx, [si+KG_Y]
    sub bx, 9
    push si
    mov di, x3_fcapsb
    mov si, x3_s_volume
    call x3d_strw
    shr cx, 1
    sub ax, cx
    mov dx, (R_GND << 8) | R_LABEL
    call x3d_strat
    pop si
    mov ax, [si+KG_X]               ; MIN under the ring's first tick...
    add ax, 4
    mov bx, [si+KG_Y]
    add bx, [si+KG_H]
    inc bx
    push si
    mov si, x3_s_min
    call x3d_strat
    pop si
    mov ax, [si+KG_X]               ; ...MAX under its last
    add ax, [si+KG_W]
    sub ax, 24
    mov si, x3_s_max
    jmp x3d_strat

; =============================================================================
; THE ELEMENTS - each: in CX = its parameter, [x3f_slot] = its key; clobbers
; AX, BX, CX, DX, SI, DI
; =============================================================================

; --- the LCD's title line: 'nn  TITLE', or the empty queue's prompt -------------
x3f_e_title:
    mov di, x3f_tbuf                ; the whole line, then its key
    mov si, [x3_lp]
    mov cx, [si+L_TTLN]
    mov al, ' '
    call x3_fillb
    mov bx, cx
    mov byte [di+bx], 0
    mov al, [x3_st+X3S_CUR]
    cmp al, 0xFF
    jne .track
    mov si, x3_s_notrack
    call x3_putn
    jmp short .key
.track:
    inc al
    call x3_2dig                    ; DI past the number
    inc di                          ; two spaces, then the title
    inc di
    sub cx, 4
    mov si, x3_st+X3S_TITLE
    call x3_putn
.key:
    mov si, x3f_tbuf
    call x3_hash                    ; AX = the key
    call x3f_ek
    jc .out
    mov si, [x3_lp]
    mov di, [si+L_TTLF]
    or di, di
    jz .sys
    mov al, [di+X3FD_ADV]           ; the field: every character's advance
    xor ah, ah
    mul word [si+L_TTLN]
    mov cx, ax
    mov ax, [si+L_TTLX]
    mov bx, [si+L_TTLY]
    mov si, x3f_tbuf
    mov dx, (R_LCD << 8) | R_LCDTTL
    call x3d_fstr
    jmp short .done
.sys:
    mov cx, [si+L_TTLX]             ; the CGA's: the system font, opaque
    mov dx, [si+L_TTLY]
    mov si, x3f_tbuf
    mov ax, (R_LCD << 8) | R_LCDTTL
    call x3d_text
.done:
    call x3f_ekdone
.out:
    ret

; --- the elapsed time, seven segments ------------------------------------------
x3f_e_time:
    mov ax, 0x7FFF                  ; '--:--': no track
    cmp byte [x3_st+X3S_CUR], 0xFF
    je .k
    mov ax, [x3_st+X3S_ELAP]
.k:
    call x3f_ek
    jc .out
    mov di, x3f_tbuf
    call x3f_mmss5                  ; 'mm:ss' or '--:--'
    mov si, [x3_lp]
    mov di, [si+L_TIMS]
    call x3f_dfield                 ; CX x DX: four digits and the colon
    call x3d_bnew
    xor ax, ax
    xor bx, bx
    mov si, x3f_tbuf
    call x3d_bdigs
    mov si, [x3_lp]
    mov ax, [si+L_TIMX]
    mov bx, [si+L_TIMY]
    mov dx, (R_LCD << 8) | R_LCDTIM
    call x3d_bout
    call x3f_ekdone
.out:
    ret

; x3f_mmss5 - AX seconds (7FFFh: none) -> 'mm:ss' / '--:--' at DI, NUL-ended.
; clobbers AX, DI
x3f_mmss5:
    cmp ax, 0x7FFF
    jne .t
    mov word [di], '--'
    mov word [di+2], ':-'
    mov byte [di+4], '-'
    mov byte [di+5], 0
    ret
.t:
    call x3_mmss
    mov byte [di-1], 0
    ret

; x3f_dfield - DI = a digit set -> CX = the width of 'mm:ss' in it, DX = its
; height. clobbers AX
x3f_dfield:
    mov cx, [di+X3DS_W]
    shl cx, 1
    shl cx, 1
    add cx, [di+X3DS_CW]
    mov ax, [di+X3DS_GAP]
    shl ax, 1
    shl ax, 1
    add cx, ax
    mov dx, [di+X3DS_H]
    ret

; --- '/ 04:38', thin - '/~04:38' when the total is an estimate ----------------
x3f_e_total:
    mov ax, 0x7FFF
    cmp byte [x3_st+X3S_CUR], 0xFF
    je .k
    test byte [x3_st+X3S_FLAGS], X3SF_NOTOT
    jnz .k
    mov ax, [x3_st+X3S_TOTAL]
    and ah, 0x3F                    ; (seconds to 4h 33m - more than 99:59)
    test byte [x3_st+X3S_FLAGS], X3SF_EST
    jz .k
    or ah, 0x40
.k:
    call x3f_ek
    jc .out
    mov di, x3f_tbuf
    mov word [di], '/ '
    cmp ax, 0x7FFF                  ; (none: its bit 14 is not the estimate's)
    je .dash
    test ah, 0x40
    jz .sl
    mov byte [di+1], '~'
.sl:
    and ah, 0x3F
.dash:
    add di, 2
    call x3f_mmss5
    mov si, [x3_lp]
    mov di, [si+L_TOTS]
    call x3f_dfield                 ; the digits, then '/' and a digit's room
    add cx, [di+X3DS_SW]
    add cx, [di+X3DS_W]
    mov ax, [di+X3DS_GAP]
    shl ax, 1
    add cx, ax
    call x3d_bnew
    xor ax, ax
    xor bx, bx
    mov si, x3f_tbuf
    call x3d_bdigs
    mov si, [x3_lp]
    mov ax, [si+L_TOTX]
    mov bx, [si+L_TOTY]
    mov dx, (R_LCD << 8) | R_LCDTOT
    call x3d_bout
    call x3f_ekdone
.out:
    ret

; --- the progress bar: only the strip between the old length and the new -------
; It is the TRACK while playing, and the CONVERSION while decoding to disk
x3f_e_pbar:
    mov si, [x3_lp]
    mov cx, [si+L_PB+4]
    sub cx, [si+L_PB]
    inc cx                          ; CX = its inside width
    xor ax, ax
    cmp byte [x3_st+X3S_CUR], 0xFF
    je .k
    cmp byte [x3_st+X3S_STATE], X3ST_DECODE
    jne .play
    mov al, [x3_st+X3S_PCT]
    mul cx
    mov bx, 100
    div bx
    jmp short .clamp
.play:
    mov bx, [x3_st+X3S_TOTAL]
    or bx, bx
    jz .k
    mov ax, [x3_st+X3S_ELAP]
    cmp ax, bx
    jb .in
    mov ax, bx
.in:
    mul cx
    div bx                          ; AX = the done part's pixels
.clamp:
    cmp ax, cx
    jbe .k
    mov ax, cx
.k:
    call x3f_ek
    jc .out
    mov si, [x3_lp]
    mov di, [x3f_slot]
    mov di, [di]                    ; DI = what the window shows, FFFF none
    mov bx, [si+L_PB+2]
    mov dx, [si+L_PB+6]
    mov cx, [si+L_PB]
    cmp di, 0xFFFF
    je .whole
    cmp ax, di
    je .done
    ja .grow
    ; shrank: the strip [new, old) goes back to the glass
    add cx, di
    dec cx
    add ax, [si+L_PB]
    mov si, R_PBBG
    call x3d_rect
    jmp short .done
.grow:                              ; grew: [old, new) fills
    xchg ax, di
    add ax, cx
    add cx, di
    dec cx
    mov si, R_PBFG
    call x3d_rect
    jmp short .done
.whole:                             ; both parts, from nothing
    push ax
    mov di, ax
    mov ax, cx
    add cx, di
    dec cx
    push si
    mov si, R_PBFG
    call x3d_rect                   ; [x1, x1 + done)
    pop si
    mov ax, cx
    inc ax
    mov cx, [si+L_PB+4]
    mov si, R_PBBG
    call x3d_rect                   ; [x1 + done, x2]
    pop ax
.done:
    call x3f_ekdone
.out:
    ret

; --- the output format: 'MONO  11 kHz  8-BIT' ------------------------------------
x3f_e_fmt:
    xor ax, ax                      ; the key: rate / 64, channels, bits, device
    mov cl, [x3_st+X3S_CHANS]
    or cl, cl
    jz .k
    cmp byte [x3_st+X3S_CUR], 0xFF
    je .k
    mov ax, [x3_st+X3S_RATE]
    mov cl, 6
    shr ax, cl                      ; 0..1023
    mov cl, [x3_st+X3S_CHANS]
    and cl, 3
    ror cl, 1                       ; bits 7..6 of CL... into AH's top
    ror cl, 1
    or ah, cl
    cmp byte [x3_st+X3S_BITS], 16
    jne .b8
    or ah, 0x20
.b8:
    test byte [x3_st+X3S_FLAGS], X3SF_DEVLIM
    jz .k
    or ah, 0x10
.k:
    call x3f_ek
    jc .out
    push ax
    mov si, [x3_lp]                 ; the channels' word, its own field
    mov ax, [si+L_FMTX2]
    sub ax, [si+L_FMTX]
    mov cx, ax
    mov dx, 7
    call x3d_bnew
    pop ax
    push ax
    or ax, ax
    jz .chput
    mov si, x3_s_mono
    cmp byte [x3_st+X3S_CHANS], 1
    je .ch
    mov si, x3_s_stereo
.ch:
    xor ax, ax
    xor bx, bx
    mov di, x3_fcaps
    call x3d_bstr
.chput:
    mov si, [x3_lp]
    mov ax, [si+L_FMTX]
    mov bx, [si+L_FMTY]
    mov dx, (R_LCD << 8) | R_LCDFMT
    call x3d_bout
    pop ax
    mov di, x3f_tbuf                ; the rate and the bits, the other colour
    mov cx, X3F_RATECH
    push ax
    mov al, ' '
    call x3_fillb
    pop ax
    mov byte [di+X3F_RATECH], 0
    or ax, ax
    jz .rput
    call x3f_rate                   ; DI past '11 kHz'
    mov si, x3_s_8bit
    cmp byte [x3_st+X3S_BITS], 16
    jne .bits
    mov si, x3_s_16bit
.bits:
    call x3_put
    test byte [x3_st+X3S_FLAGS], X3SF_DEVLIM
    jz .rput
    mov si, x3_s_device
    call x3_put
.rput:
    mov si, [x3_lp]
    mov ax, [si+L_FMTX2]
    mov bx, [si+L_FMTY]
    mov cx, X3F_RATECH * 6
    mov si, x3f_tbuf
    mov di, x3_fcaps
    mov dx, (R_LCD << 8) | R_LCDRATE
    call x3d_fstr
    call x3f_ekdone
.out:
    ret

; x3f_rate - the output rate as 'nn kHz' at DI ('44.1 kHz', '5.5 kHz' - the
; two a decimal names; every other rate the player makes is whole kHz),
; and two spaces. DI past them. clobbers AX, DX, SI
x3f_rate:
    mov ax, [x3_st+X3S_RATE]
    mov si, x3_s_k441
    cmp ax, 44100
    je .named
    mov si, x3_s_k55
    cmp ax, 5512
    je .named
    cmp ax, 5513
    je .named
    xor dx, dx
    push cx
    mov cx, 1000
    div cx
    pop cx
    cmp ax, 100
    jb .n
    mov ax, 99
.n:
    cmp al, 10
    jae .two
    add al, '0'
    mov [di], al
    inc di
    jmp short .khz
.two:
    call x3_2dig
.khz:
    mov si, x3_s_khz
.named:
    jmp x3_put

; --- a VU dial (CX = 0 left, 1 right) ----------------------------------------------
; MONO output: the left dial is the mono level and the RIGHT is the BUFFER
; gauge (SPEC.md 121.8 - the meters never claim stereo the output lacks).
; The needle redraws the box its old and new positions share, at most
; X3F_DIALT ticks apart; a full paint draws the dial whole.
x3f_e_dial:
    call x3f_dmode                  ; AL = 0 VU / 1 BUFFER, AH = its level
    mov bl, al
    xor bh, bh                      ; BX = the mode
    xor al, al                      ; the position: 0..NPOS-1
    cmp byte [x3_meters], 0
    je .rest
    mov al, ah
    xor ah, ah
    mov dx, X3ART_NPOS - 1
    mul dx
    add ax, 127
    mov dl, 255
    div dl                          ; AL = the position
.rest:
    mov ah, bl                      ; the key: position | mode << 8
    call x3f_ek
    jc .out
    cmp byte [x3f_full], 0          ; at most X3F_DIALT ticks between needle
    jne .draw                       ; draws: a later pass will catch up
    push ax
    push bx
    call OSAPI_GET_TICKS
    mov bx, cx
    shl bx, 1
    sub ax, [x3f_dlast+bx]
    cmp ax, X3F_DIALT
    pop bx
    pop ax
    jae .draw
.out:
    ret
.draw:
    push ax
    call OSAPI_GET_TICKS
    mov bx, cx
    shl bx, 1
    mov [x3f_dlast+bx], ax
    pop ax
    mov [x3f_dpos], al
    mov [x3f_dmd], ah
    mov si, [x3_lp]
    mov si, [si+L_DIAL]
    mov di, [si+DG_X0]              ; the face's place
    jcxz .left
    mov di, [si+DG_X1]
.left:
    mov [x3f_dfx], di
    ; the box to redraw: the whole face, or the needles' union
    mov word [x3f_dx0], 0
    mov word [x3f_dx1], X3ART_DIALF_W - 1
    mov word [x3f_dy0], 0
    mov ax, [si+DG_H]
    dec ax
    mov [x3f_dy1], ax
    mov di, [x3f_slot]
    mov ax, [di]
    cmp ax, 0xFFFF
    je .box
    cmp ah, [x3f_dmd]
    jne .box                        ; a new mode: a new face
    mov bl, al                      ; the old end...
    call x3f_nend                   ; CX, DX = (x, y)
    mov [x3f_dx0], cx
    mov [x3f_dx1], cx
    mov [x3f_dy0], dx
    mov bl, [x3f_dpos]              ; ...the new...
    call x3f_nend
    call x3f_dgrow
    mov cx, [si+DG_PX]              ; ...and the pivot
    mov dx, [si+DG_PY]
    call x3f_dgrow
    dec word [x3f_dx0]              ; the needle's two pixels, a pixel's slack
    inc word [x3f_dx1]
    inc word [x3f_dx1]
    dec word [x3f_dy0]
    and word [x3f_dx0], 0xFFFE      ; the canvas is two pixels a byte
    or word [x3f_dx1], 1
    cmp word [x3f_dx0], 0
    jge .x1
    mov word [x3f_dx0], 0
.x1:
    cmp word [x3f_dx1], X3ART_DIALF_W - 1
    jle .y0
    mov word [x3f_dx1], X3ART_DIALF_W - 1
.y0:
    cmp word [x3f_dy0], 0
    jge .box
    mov word [x3f_dy0], 0
.box:
    cmp word [x3d_th], x3_tbw
    je .bw
    call x3f_dial4                  ; colour: the canvas, one BLIT4
    jmp short .done
.bw:
    call x3f_dial1                  ; black and white: a band, one BLIT1
.done:
    call x3f_ekdone
    ret

; x3f_dgrow - the redraw box [x3f_dx0..dx1] x [x3f_dy0..] grown to hold CX, DX
x3f_dgrow:
    cmp cx, [x3f_dx0]
    jge .a
    mov [x3f_dx0], cx
.a:
    cmp cx, [x3f_dx1]
    jle .b
    mov [x3f_dx1], cx
.b:
    cmp dx, [x3f_dy0]
    jge .c
    mov [x3f_dy0], dx
.c:
    ret

; x3f_nend - BL = a needle position, SI = the dials -> CX, DX = its end.
; clobbers BX
x3f_nend:
    push es
    mov es, [x3_artseg]             ; the table is the art part's
    xor bh, bh
    shl bx, 1
    add bx, [si+DG_ND]
    mov cl, [es:bx]
    xor ch, ch
    mov dl, [es:bx+1]
    xor dh, dh
    pop es
    ret

; x3f_dmode - CX = a dial -> AL = 0 a VU meter / 1 the BUFFER gauge, AH = the
; level it shows. keeps everything else
x3f_dmode:
    mov ah, [x3_st+X3S_METL]
    xor al, al
    cmp byte [x3_st+X3S_CUR], 0xFF  ; nothing loaded: two meters at rest
    je .zero
    cmp byte [x3_st+X3S_CHANS], 0
    je .zero
    jcxz .out                       ; the left: always the (first) channel
    mov ah, [x3_st+X3S_METR]
    cmp byte [x3_st+X3S_CHANS], 2
    je .out
    mov ah, [x3_st+X3S_LEVEL]       ; mono output: the right is the BUFFER
    inc al
.out:
    ret
.zero:
    xor ah, ah
    or cx, cx                       ; ...the right still says BUFFER when the
    jz .out                         ; output is known to be mono
    cmp byte [x3_st+X3S_CHANS], 1
    jne .out
    inc al
    ret

; x3f_dial4 - the colour dial: rows dy0..dy1, columns dx0..dx1 of the mode's
; FACE (cream, zone, mask and scale, composed once - x3f_dface) copied into
; the canvas, the needle drawn on it, that box put down in one BLIT4.
; in SI = the dials. clobbers AX..DI
x3f_dial4:
    call x3f_dface                  ; DI = the mode's face, built if it is not
    mov cx, X3ART_DIALF_W
    mov dx, [x3f_dy1]
    sub dx, [x3f_dy0]
    inc dx
    xor al, al
    call x3d_c4new
    mov ax, [x3f_dy0]
    mov [x3d_cvy0], ax
    mul word [x3d_cvst]             ; the face's stride is the canvas's
    add di, ax                      ; DI = the face's row dy0
    mov ax, [x3f_dx0]
    shr ax, 1
    add di, ax                      ; ...its column dx0
    mov bx, x3_work
    add bx, ax                      ; BX = the canvas's
    mov cx, [x3f_dx1]
    sub cx, [x3f_dx0]
    inc cx
    shr cx, 1                       ; CX = the bytes a row
    mov dx, [x3d_cvh]
    push si
    push es
    push ds
    pop es
    cld
.row:
    push cx
    mov si, di
    push di
    mov di, bx
    rep movsb
    pop di
    pop cx
    add di, [x3d_cvst]
    add bx, [x3d_cvst]
    dec dx
    jnz .row
    pop es
    pop si
    mov byte [x3d_pmode], X3P_C4
    mov byte [x3d_pcol], CBLACK
    call x3f_needle
    mov ax, [x3f_dfx]
    mov bx, [si+DG_Y]
    mov cx, [x3f_dx0]
    mov dx, [x3f_dx1]
    jmp x3d_c4out

; x3f_dface - DI = the colour face for mode [x3f_dmd], composed into its own
; buffer the first time it is wanted for these dials (FULL's and MID's are
; different pictures): the cream checker, the zone (red on a VU, green on the
; BUFFER gauge), the black mask, the printed scale - a fraction of a second
; of an XT's time per mode, once, so that a needle is a copy and a line.
; in SI = the dials. keeps SI
x3f_dface:
    cmp [x3f_fdg], si               ; another layout's dials: both faces owed
    je .same
    mov [x3f_fdg], si
    mov byte [x3f_fok], 0
.same:
    mov di, x3f_fvu
    mov al, 1
    cmp byte [x3f_dmd], 0
    je .m
    mov di, x3f_fbuf
    mov al, 2
.m:
    test [x3f_fok], al
    jz .build
    ret
.build:
    or [x3f_fok], al
    push di
    push es
    mov es, [x3_artseg]             ; the masks are the art part's
    mov cx, X3ART_DIALF_W           ; the whole face in the canvas...
    mov dx, [si+DG_H]
    xor al, al
    call x3d_c4new
    mov al, CRED
    cmp byte [x3f_dmd], 0
    je .zc
    mov al, CGREEN
.zc:
    mov [x3f_dzone], al
    mov bx, [si+DG_K]               ; (one stride for both masks)
    mov al, [es:bx+3]
    xor ah, ah
    mov [x3f_mst], ax
    xor bx, bx                      ; BX = the row
    mov di, x3_work
.row:
    push di
    mov ax, bx
    mul word [x3f_mst]
    add ax, 4
    mov dx, ax
    add ax, [si+DG_K]
    mov [x3f_kp], ax
    add dx, [si+DG_R]
    mov [x3f_rp], dx
    xor bp, bp                      ; BP = x (a plain register)
.px:
    mov ax, bp                      ; the master's phase: yellow where x + y
    add ax, bx                      ; is even
    mov dl, CYELLOW
    test al, 1
    jz .cream
    mov dl, CWHITE
.cream:
    push bx
    push si
    mov bx, bp
    mov cl, 3
    shr bx, cl
    mov si, bp
    and si, 7
    mov ch, [x3f_bitm+si]
    mov si, [x3f_rp]
    test [es:si+bx], ch
    jz .nz
    mov dl, [x3f_dzone]
.nz:
    mov si, [x3f_kp]
    test [es:si+bx], ch
    jz .nk
    xor dl, dl                      ; CBLACK
.nk:
    pop si
    pop bx
    mov ax, bp                      ; into the canvas
    shr ax, 1
    push di
    add di, ax
    test bp, 1
    jnz .lo
    mov cl, 4
    shl dl, cl
    and byte [di], 0x0F
    or [di], dl
    jmp short .put
.lo:
    and byte [di], 0xF0
    or [di], dl
.put:
    pop di
    inc bp
    cmp bp, X3ART_DIALF_W
    jb .px
    pop di
    add di, [x3d_cvst]
    inc bx
    cmp bx, [si+DG_H]
    jb .row
    pop es
    mov byte [x3d_pmode], X3P_C4    ; ...the printed scale...
    mov byte [x3d_pcol], CBLACK
    call x3f_dscale
    pop di                          ; ...and the canvas kept as the face
    push si
    push di
    push es
    push ds
    pop es
    mov si, x3_work
    mov ax, [x3d_cvst]
    mul word [x3d_cvh]
    mov cx, ax
    cld
    rep movsb
    pop es
    pop di
    pop si
    ret

x3f_bitm:   db 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01

; x3f_dial1 - the black and white dial: rows dy0..dy1 of its face in a band,
; the scale and the needle on them, one BLIT1. in SI = the dials. clobbers
; AX..DI
x3f_dial1:
    mov cx, X3ART_DIALF_W
    mov dx, [x3f_dy1]
    sub dx, [x3f_dy0]
    inc dx
    call x3d_bnew
    push si
    mov si, [si+DG_BW]              ; the face's rows dy0.., ORed in
    push es
    mov es, [x3_artseg]
    mov cl, [es:si+3]
    pop es
    xor ch, ch
    mov ax, [x3f_dy0]
    mul cx
    add si, ax
    add si, 4
    mov dx, [x3d_bh]
    xor ax, ax
    xor bx, bx
    call x3d_bor
    pop si
    mov ax, [x3f_dy0]
    mov [x3d_cvy0], ax
    mov byte [x3d_pmode], X3P_SET
    call x3f_dscale
    call x3f_needle
    mov ax, [x3f_dfx]
    mov bx, [si+DG_Y]
    add bx, [x3f_dy0]
    mov dx, (R_HI << 8) | R_LABEL   ; black on white, whatever the panel
    jmp x3d_bout

; x3f_dscale - the printed scale for the mode, through x3d_plot into whichever
; target is set (a partial band clips it). in SI = the dials. keeps SI
x3f_dscale:
    push si
    push es
    mov es, [x3_artseg]             ; the marks and their glyphs: the art part
    mov di, [si+DG_SVU]
    cmp byte [x3f_dmd], 0
    je .sc
    mov di, [si+DG_SBUF]
.sc:
    cmp byte [es:di], 0xFF
    je .out
    mov al, [es:di]                 ; a mark: x, y, its text
    xor ah, ah
    mov bl, [es:di+1]
    xor bh, bh
    add di, 2
.ch:
    mov cl, [es:di]
    inc di
    or cl, cl
    jz .sc
    call x3f_ddig                   ; AX advanced
    jmp short .ch
.out:
    pop es
    pop si
    ret

; x3f_needle - the needle at [x3f_dpos], two pixels wide, from the pivot.
; in SI = the dials. keeps SI
x3f_needle:
    push si
    mov bl, [x3f_dpos]
    call x3f_nend                   ; CX, DX = the end
    mov ax, [si+DG_PX]
    mov bx, [si+DG_PY]
    call x3d_line
    inc ax
    inc cx
    call x3d_line
    pop si
    ret

; x3f_ddig - CL = a scale character, plotted at AX, BX through x3d_plot (so a
; partial canvas clips it); AX past it (4). ES = [x3_artseg] (x3f_dscale's).
; clobbers CX, DX
x3f_ddig:
    push si
    push di
    mov dl, cl                      ; the glyph in the dial-digit face
    xor dh, dh
    mov si, dx
    mov dl, [es:x3art_dd_ix + si - X3ART_FONT_FIRST]
    or dl, dl
    jz .adv
    dec dl
    mov si, dx
    shl si, 1
    shl si, 1
    add si, dx                      ; five bytes a glyph
    add si, x3art_dd
    push bx
    mov dh, 5
.row:
    mov dl, [es:si]
    inc si
    push ax
    mov cx, 3
.bit:
    test dl, 0x80
    jz .nb
    call x3d_plot
.nb:
    shl dl, 1
    inc ax
    loop .bit
    pop ax
    inc bx
    dec dh
    jnz .row
    pop bx
.adv:
    add ax, 4
    pop di
    pop si
    ret

; --- the label under a dial: L / R, or MONO / BUFFER -------------------------------
x3f_e_dlab:
    call x3f_dmode                  ; AL = the mode
    mov si, x3_s_l
    jcxz .l
    mov si, x3_s_r
    or al, al
    jz .k
    mov si, x3_s_buffer
    jmp short .k
.l:
    cmp byte [x3_st+X3S_CHANS], 1
    jne .k
    mov si, x3_s_mono
.k:
    mov ax, si                      ; the key: the string itself
    call x3f_ek
    jc .out
    mov di, [x3_lp]
    mov di, [di+L_DIAL]
    mov ax, [di+DG_LX0]
    jcxz .x
    mov ax, [di+DG_LX1]
.x:
    mov bx, [di+DG_LY]
    mov cx, [di+DG_LW]
    mov di, x3_fcapsb
    mov dx, (R_GND << 8) | R_LABEL
    mov byte [x3d_ctr], 1
    call x3d_fstr
    mov byte [x3d_ctr], 0
    call x3f_ekdone
.out:
    ret

; --- the knob: its detent, and whether it does anything ----------------------------
x3f_e_knob:
    mov al, [x3_st+X3S_VOL]
    xor ah, ah
    test byte [x3_st+X3S_FLAGS], X3SF_VOLOK
    jnz .k
    inc ah                          ; greyed: a dotted indicator
.k:
    call x3f_ek
    jc .out
    mov [x3d_pdim], ah
    mov si, [x3_lp]
    mov si, [si+L_KNOB]
    cmp word [x3d_th], x3_tbw
    je .bw
    mov cx, [si+KG_W]               ; COLOUR: the panel's colour...
    mov dx, [si+KG_H]
    push si
    mov si, R_GND
    call x3d_rv
    mov ax, si
    pop si
    call x3d_c4new
    call x3f_knob4                  ; ...the knob's four greys...
    mov byte [x3d_pmode], X3P_C4
    mov byte [x3d_pcol], CBLACK
    call x3f_klines                 ; ...the ring and the indicator
    mov ax, [si+KG_X]
    mov bx, [si+KG_Y]
    xor cx, cx
    mov dx, [si+KG_W]
    dec dx
    call x3d_c4out
    jmp short .done
.bw:
    mov cx, [si+KG_W]               ; BLACK AND WHITE: the dithered face
    mov dx, [si+KG_H]
    call x3d_bnew
    push si
    mov ax, [si+KG_KX]
    mov bx, [si+KG_KY]
    mov si, [si+KG_BW]
    call x3d_bpic
    pop si
    mov byte [x3d_pmode], X3P_SET
    call x3f_klines
    mov ax, [si+KG_X]
    mov bx, [si+KG_Y]
    mov dx, (R_GND << 8) | R_LABEL
    call x3d_bout
.done:
    mov byte [x3d_pdim], 0
    call x3f_ekdone
.out:
    ret

; x3f_klines - the tick ring, then the indicator at [x3_st+X3S_VOL], three
; passes for two pixels at any angle. in SI = the knob. keeps SI
x3f_klines:
    push si
    mov al, [x3d_pdim]
    push ax
    mov byte [x3d_pdim], 0          ; the ring is printed: never dim
    push si
    mov si, [si+KG_RING]
    xor ax, ax
    xor bx, bx
    mov cx, X3ART_KSTEPS
    call x3d_lines
    pop si
    pop ax
    mov [x3d_pdim], al
    mov al, [x3_st+X3S_VOL]
    cmp al, X3_VMAX
    jbe .v
    mov al, X3_VMAX
.v:
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov si, [si+KG_IND]
    add si, ax
    xor ax, ax
    xor bx, bx
    call x3d_line4
    inc ax
    call x3d_line4
    dec ax
    inc bx
    call x3d_line4
    pop si
    ret

; x3f_knob4 - the knob's two planes into the canvas as its four greys, at
; (KG_KX, KG_KY). in SI = the knob. keeps SI
x3f_knob4:
    push si
    push bp
    push es
    mov es, [x3_artseg]             ; the planes are the art part's
    mov di, x3_work                 ; DI = the canvas row
    mov ax, [si+KG_KY]
    mul word [x3d_cvst]
    add di, ax
    mov ax, [si+KG_KX]
    shr ax, 1
    add di, ax
    mov cx, [si+KG_ROWS]
    mov bx, [si+KG_P1]
    mov bp, [si+KG_P0]
    mov dl, [es:bp+3]               ; (ES: BP alone would address SS)
    xor dh, dh
    mov [x3f_kst], dx
    add bx, 4
    add bp, 4
.row:
    push cx
    push di
    push bx
    push bp
    mov cx, X3ART_KNOBF_W / 8       ; eight pixels a step: four canvas bytes
.oct:
    mov al, [es:bp]                 ; plane 0 (ES: BP would address SS)
    mov ah, [es:bx]                 ; plane 1
    push cx
    mov cx, 4
.pair:
    call x3f_kgrey                  ; DL = the next pixel's colour
    mov dh, dl
    push cx
    mov cl, 4
    shl dh, cl
    pop cx
    call x3f_kgrey
    or dl, dh
    mov [di], dl
    inc di
    loop .pair
    pop cx
    inc bx
    inc bp
    loop .oct
    pop bp
    pop bx
    pop di
    pop cx
    add bx, [x3f_kst]
    add bp, [x3f_kst]
    add di, [x3d_cvst]
    loop .row
    pop es
    pop bp
    pop si
    ret

; x3f_kgrey - AL, AH = plane 0's and plane 1's byte, their top bits the next
; pixel -> DL = its colour (black, dark grey, light grey, white); both bytes
; shifted on
x3f_kgrey:
    xor dl, dl
    shl al, 1
    rcl dl, 1
    shl ah, 1
    jnc .p
    or dl, 2
.p:
    push bx
    mov bl, dl
    xor bh, bh
    mov dl, [x3f_grey+bx]
    pop bx
    ret

x3f_grey:   db CBLACK, CDGRAY, CLGRAY, CWHITE

; --- one row of the queue (CX = 0..) --------------------------------------------
x3f_e_qrow:
    mov al, [x3_qtop]
    add al, cl
    xor ah, ah                      ; AL = the entry, AH = its flags
    cmp al, [x3_qn]
    jb .is
    mov ax, 0x00FF                  ; past the end: an empty row
    jmp short .k
.is:
    cmp al, [x3_qsel]
    jne .ns
    or ah, 1                        ; the selection
.ns:
    cmp al, [x3_st+X3S_CUR]
    jne .k
    or ah, 2                        ; the one playing (or loaded)
.k:
    mov dl, [x3_qgen]               ; ...and the queue's generation: a
    and dl, 0x3F                    ; removed or renamed entry is news
    shl dl, 1
    shl dl, 1
    or ah, dl
    call x3f_ek
    jc .out
    push ax
    push cx
    mov di, x3f_tbuf                ; the row, X3_QCH characters
    mov cx, X3_QCH
    mov al, ' '
    call x3_fillb
    mov byte [di+X3_QCH], 0
    pop cx
    pop ax
    cmp al, 0xFF
    je .draw
    push ax
    inc di                          ; ' 01  TITLE ... 04:38 '
    inc al
    call x3_2dig
    pop ax
    push ax
    call x3_qent                    ; SI = the entry
    mov di, x3f_tbuf + 5
    push cx
    mov cx, X3_QCH - 13
    push si
    lea si, [si+QE_TITLE]
    call x3_putn
    pop si
    pop cx
    mov di, x3f_tbuf + X3_QCH - 6
    mov ax, [si+QE_DUR]
    cmp ax, 0xFFFF
    jne .t
    mov ax, 0x7FFF
.t:
    call x3f_mmss5
    mov byte [x3f_tbuf + X3_QCH - 1], ' '
    pop ax
.draw:
    mov si, [x3_lp]
    mov si, [si+L_Q]
    mov di, si                      ; DI = the queue's geometry
    push ax
    mov ax, cx                      ; the row's top
    mul word [di+QG_PITCH]          ; (DX spent)
    add ax, [di+QG_Y0]
    mov [x3f_qy], ax
    pop ax
    mov dx, (R_QBOX << 8) | R_QTXT
    test ah, 1
    jz .col
    mov dx, (R_QSEL << 8) | R_QSELT
.col:
    push dx                         ; the roles, for the text
    mov al, dh                      ; the ground: the bar, or the box
    xor ah, ah
    mov si, ax
    mov ax, [di+QG_X]
    mov cx, ax
    add cx, X3_QCH * 8 - 1
    mov bx, [x3f_qy]                ; above the text
    mov dx, bx
    add dx, [di+QG_ABOVE]
    dec dx
    call x3d_rect
    mov bx, dx                      ; below it
    add bx, 9
    mov dx, bx
    add dx, [di+QG_BELOW]
    dec dx
    call x3d_rect
    mov bx, dx                      ; and the gap to the next, in the box's
    inc bx                          ; own colour whatever the row is
    mov dx, [x3f_qy]
    add dx, [di+QG_PITCH]
    dec dx
    mov si, R_QBOX
    call x3d_rect
    mov dx, [x3f_qy]                ; the text
    add dx, [di+QG_ABOVE]
    mov cx, [di+QG_X]
    pop ax                          ; AL = the ink, AH = the ground
    mov si, x3f_tbuf
    call x3d_text
    call x3f_ekdone
.out:
    ret

; --- the queue's scroll bar ------------------------------------------------------------
x3f_e_qsb:
    mov al, [x3_qtop]
    mov ah, [x3_qn]
    call x3f_ek
    jc .out
    mov si, [x3_lp]
    mov si, [si+L_Q]
    mov di, [si+QG_SBT2]
    sub di, [si+QG_SBT1]
    inc di                          ; DI = the trough's height
    mov cl, ah
    xor ch, ch
    mov bx, [si+QG_N]
    xor dx, dx                      ; DX = the thumb's top in it
    jcxz .none                      ; an empty queue: the trough alone
    cmp cx, bx
    ja .some
    mov bx, di                      ; it all fits: the thumb is the trough
    jmp short .draw
.none:
    xor bx, bx
    jmp short .draw
.some:
    push ax
    mov ax, di
    mul word [si+QG_N]
    div cx                          ; AX = trough * shown / entries
    cmp ax, 6
    jae .h
    mov ax, 6
.h:
    mov bx, ax                      ; BX = the thumb's height
    pop ax
    push bx
    mov dl, al                      ; its top: (trough - thumb) * top /
    xor dh, dh                      ; (entries - shown)
    sub di, bx
    mov ax, di
    mul dx
    sub cx, [si+QG_N]
    div cx
    mov dx, ax
    pop bx
.draw:
    push si
    mov ax, [si+QG_SBX]             ; the trough above, the thumb, below
    mov cx, ax
    add cx, 12
    mov di, bx                      ; DI = the thumb's height
    mov bx, [si+QG_SBT1]
    push dx
    add dx, bx
    push dx
    dec dx
    push si
    mov si, R_TROUGH
    call x3d_rect                   ; above
    pop si
    pop bx                          ; BX = the thumb's top
    push bx
    mov dx, bx
    add dx, di
    dec dx
    push si
    mov si, R_THUMB
    call x3d_rect                   ; the thumb
    pop si
    mov bx, dx
    inc bx
    mov dx, [si+QG_SBT2]
    mov si, R_TROUGH
    call x3d_rect                   ; below
    pop bx
    pop dx
    pop si
    call x3f_ekdone
.out:
    ret

; --- the status line's two fields ---------------------------------------------------------
x3f_e_sbq:                          ; 'Quality: XT FAST (set)'
    mov di, x3f_tbuf
    mov si, [x3_lp]
    mov cx, [si+L_SBQN]
    mov al, ' '
    call x3_fillb
    mov bx, cx
    mov byte [di+bx], 0
    mov si, x3_s_quality
    call x3_putn
    mov al, [x3_st+X3S_QWANT]
    cmp al, X3Q_AUTO
    jne .set
    mov al, [x3_st+X3S_QUAL]        ; what runs (AUTO: nothing chosen yet)
    call x3f_qname
    call x3_putn
    jmp short .key
.set:
    call x3f_qname                  ; the user's choice, said to be one
    call x3_putn
    mov si, x3_s_set
    call x3_putn
.key:
    mov si, [x3_lp]
    mov cx, [si+L_SBQX]
    mov si, x3f_tbuf
    call x3_hash
    call x3f_ek
    jc .out
    mov si, [x3_lp]
    mov dx, [si+L_SBY]
    mov si, x3f_tbuf
    mov ax, (R_STAT << 8) | R_STATTXT
    call x3d_text
    call x3f_ekdone
.out:
    ret

; x3f_qname - AL = a quality -> SI = its name. keeps the rest
x3f_qname:
    push bx
    mov bl, al
    and bx, 7
    shl bx, 1
    mov si, [x3_qnames+bx]
    pop bx
    ret

x3f_e_sbst:                         ; the state, or the advanced figures
    mov di, x3f_tbuf
    mov si, [x3_lp]
    mov cx, [si+L_SBSN]
    mov al, ' '
    call x3_fillb
    mov bx, cx
    mov byte [di+bx], 0
    cmp byte [x3_adv], 0
    jne .adv
    mov bl, [x3_st+X3S_STATE]
    cmp bl, X3ST_N
    jb .st
    mov bl, X3ST_ERROR
.st:
    xor bh, bh
    shl bx, 1
    mov si, [x3_stnames+bx]
    call x3_putn
    cmp byte [x3_st+X3S_STATE], X3ST_DECODE
    jne .err
    mov al, [x3_st+X3S_PCT]         ; 'Decoding to disk 42%'
    call x3f_pct
    jmp short .key
.err:
    cmp byte [x3_st+X3S_STATE], X3ST_ERROR
    jne .key
    mov si, [x3_st+X3S_ERR]
    or si, si
    jz .key
    call x3_putn
    jmp short .key
.adv:                               ; 'Peak 371K  0 underruns  0.31x'
    mov si, x3_s_peak
    call x3_putn
    mov ax, [x3_st+X3S_PEAK]
    call x3f_num
    mov si, x3_s_kunder
    call x3_putn
    mov ax, [x3_st+X3S_UNDER]
    call x3f_num
    mov si, x3_s_under
    call x3_putn
    mov ax, [x3_st+X3S_SPEED]       ; n.nnx
    push cx
    xor dx, dx
    mov cx, 100
    div cx
    pop cx
    push dx
    call x3f_num
    mov si, x3_s_dot
    call x3_putn
    pop ax
    cmp cx, 3
    jb .key
    call x3_2dig
    sub cx, 2
    mov si, x3_s_x
    call x3_putn
.key:
    mov si, x3f_tbuf
    call x3_hash
    call x3f_ek
    jc .out
    mov si, [x3_lp]
    mov cx, [si+L_SBSX]
    mov dx, [si+L_SBY]
    mov si, x3f_tbuf
    mov ax, (R_STAT << 8) | R_STATTXT
    call x3d_text
    call x3f_ekdone
.out:
    ret

; x3f_pct - AL (0..100) as ' nn%' at DI, CX the room left. clobbers AX, SI
x3f_pct:
    xor ah, ah
    call x3f_num
    mov si, x3_s_pct
    jmp x3_putn

; x3f_num - AX as decimal at DI, at most CX characters (CX counts down).
; clobbers AX, SI
x3f_num:
    push dx
    push bx
    mov si, x3f_nbuf + 6
    mov byte [si], 0
    mov bx, 10
.d:
    xor dx, dx
    div bx
    add dl, '0'
    dec si
    mov [si], dl
    or ax, ax
    jnz .d
    pop bx
    pop dx
    jmp x3_putn

; =============================================================================
; THE BUTTONS (apps/os88ui.inc's record, XTMP3's painter: SPEC.md 13.8.10)
; =============================================================================

; -----------------------------------------------------------------------------
; x3f_bpaint - the OWN painter: AX = the index, BX = the rect (screen,
;              inclusive), DI = the flags, DOWN and DIS resolved. Keeps every
;              register
;
; THE WHOLE KEY IS ONE BAND: the rounded outline, the shadow dither along two
; sides, the picture and the caption are its ink, the face its paper - so a
; key is ONE blit pressed, latched, greyed or not, and no pixel is drawn
; twice. The shadow sits bottom-right on a key up and top-left on one held
; down; a LATCHED key (Play while playing, Pause while paused) is the
; mockup's lit one - its face the LCD's black, its ink the LCD's green.
; -----------------------------------------------------------------------------
x3f_bpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp word [bx], X3_PARK          ; not on this layout
    je .out
    mov bp, ax                      ; BP = the index (a plain register)
    mov ax, [bx]                    ; the rect, content-relative
    sub ax, [x3d_ox]
    mov cx, [bx+4]
    sub cx, [x3d_ox]
    mov dx, [bx+6]
    sub dx, [x3d_oy]
    mov bx, [bx+2]
    sub bx, [x3d_oy]
    mov [x3f_bx], ax
    mov [x3f_by], bx
    sub cx, ax
    inc cx
    sub dx, bx
    inc dx
    call x3d_bnew                   ; CX x DX: the whole key
    ; the state: 0 up, 1 pressed, 2 latched
    xor al, al
    test di, OS88UI_DIS
    jnz .st
    test di, OS88UI_DOWN
    jz .st
    inc al
    test di, OS88UI_LATCH
    jz .st
    inc al
.st:
    mov [x3f_bst], al
    ; the picture and the caption, centred - side by side when both
    mov si, bp
    shl si, 1
    mov bx, [x3_lp]
    mov bx, [bx+L_ICONS]
    mov ax, [bx+si]
    mov [x3f_bpic], ax
    mov bx, [x3_lp]
    mov bx, [bx+L_CAPS]
    mov ax, [bx+si]
    mov [x3f_bcap], ax
    xor ax, ax                      ; the content's width: the picture...
    mov si, [x3f_bpic]
    or si, si
    jz .nopic
    call x3d_picw
    mov ax, cx
.nopic:
    mov si, [x3f_bcap]              ; ...a gap and the caption
    or si, si
    jz .nocap
    or ax, ax
    jz .cw
    add ax, 4
.cw:
    push di
    mov di, x3_fcapsb
    call x3d_strw
    pop di
    add ax, cx
.nocap:
    mov cx, [x3d_bw]                ; its left: centred, the shadow allowed
    sub cx, ax
    sar cx, 1
    mov ax, cx
    cmp byte [x3f_bst], 1
    jne .xa
    inc ax                          ; held down: a pixel right and down
.xa:
    mov [x3f_bcx], ax
    mov si, [x3f_bpic]
    or si, si
    jz .cap
    call x3d_picw                   ; DX = its height
    mov bx, [x3d_bh]
    sub bx, dx
    sar bx, 1
    cmp byte [x3f_bst], 1
    jne .ya
    inc bx
.ya:
    call x3d_bpic                   ; AX past it
    add ax, 4
.cap:
    mov si, [x3f_bcap]
    or si, si
    jz .grey
    mov bx, [x3d_bh]
    sub bx, 7
    sar bx, 1
    cmp byte [x3f_bst], 1
    jne .yc
    inc bx
.yc:
    push di
    mov di, x3_fcapsb
    call x3d_bstr
    pop di
.grey:
    test di, OS88UI_DIS             ; greyed in black and white: half the
    jz .edge                        ; picture's ink, the outline kept whole
    cmp word [x3d_th], x3_tbw
    jne .edge
    call x3f_bdim
.edge:
    call x3f_bedge                  ; the outline and the shadow
    mov dx, (R_KEY << 8) | R_KEYINK ; the roles by the state
    cmp byte [x3f_bst], 2
    jne .notlat
    mov dx, (R_KEYON << 8) | R_KEYONI
.notlat:
    test di, OS88UI_DIS
    jz .put
    cmp word [x3d_th], x3_tbw
    je .put
    mov dl, R_KEYDIS
.put:
    mov ax, [x3f_bx]
    mov bx, [x3f_by]
    call x3d_bout
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3f_bdim - the band's ink ANDed with a checkerboard. keeps everything
x3f_bdim:
    push ax
    push cx
    push dx
    push si
    mov si, x3_work
    mov dx, [x3d_bh]
    mov al, 0xAA
.row:
    mov cx, [x3d_bst]
.b:
    and [si], al
    inc si
    loop .b
    not al
    dec dx
    jnz .row
    pop si
    pop dx
    pop cx
    pop ax
    ret

; x3f_bedge - a key's outline (its corners cut: rounded at this size) and its
; shadow, two pixels of checker along the bottom and the right - or the top
; and the left, held down; a latched key has none. keeps everything
x3f_bedge:
    push ax
    push bx
    push cx
    push dx
    mov byte [x3d_pmode], X3P_SET
    mov byte [x3d_pdim], 0
    mov cx, [x3d_bw]
    dec cx                          ; CX = the last column
    mov dx, [x3d_bh]
    dec dx                          ; DX = the last row
    mov ax, 1                       ; the top and the bottom
.hz:
    xor bx, bx
    call x3d_plot
    mov bx, dx
    call x3d_plot
    inc ax
    cmp ax, cx
    jb .hz
    mov bx, 1                       ; the sides
.vt:
    xor ax, ax
    call x3d_plot
    mov ax, cx
    call x3d_plot
    inc bx
    cmp bx, dx
    jb .vt
    cmp byte [x3f_bst], 2
    je .out
    ; the shadow: rows, then columns, every other pixel
    mov byte [x3d_pdim], 1
    cmp byte [x3f_bst], 1
    je .down
    mov bx, dx                      ; up: the bottom two rows inside...
    dec bx
    call x3f_brow
    dec bx
    call x3f_brow
    mov ax, cx                      ; ...and the right two columns
    dec ax
    call x3f_bcol
    dec ax
    call x3f_bcol
    jmp short .out
.down:
    mov bx, 1
    call x3f_brow
    inc bx
    call x3f_brow
    mov ax, 1
    call x3f_bcol
    inc ax
    call x3f_bcol
.out:
    mov byte [x3d_pdim], 0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3f_brow - row BX, columns 1..CX-1, plotted. x3f_bcol - column AX, rows
; 1..DX-1. keep everything
x3f_brow:
    push ax
    mov ax, 1
.p:
    call x3d_plot
    inc ax
    cmp ax, cx
    jb .p
    pop ax
    ret
x3f_bcol:
    push bx
    mov bx, 1
.p:
    call x3d_plot
    inc bx
    cmp bx, dx
    jb .p
    pop bx
    ret

; =============================================================================
; HIT-TESTS - CX, DX content-relative
; =============================================================================

; x3f_in - CF = 0 when CX, DX is inside the rect at SI (x1, y1, x2, y2)
x3f_in:
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

; =============================================================================
; THE LAYOUTS
; =============================================================================
; (the deco lists, the elements, the button rects, the pictures and captions)

%macro DRECT 5
    dw D_RECT, %1, %2, %3, %4, %5
%endmacro
%macro DFRAME 5
    dw D_FRAME, %1, %2, %3, %4, %5
%endmacro
%macro DPIC 4
    dw D_PIC, %1, %2, %3, %4
%endmacro
%macro DSTR 5
    dw D_STR, %1, %2, %3, %4, %5
%endmacro
%macro DCSTR 5
    dw D_CSTR, %1, %2, %3, %4, %5
%endmacro
; a raised bevel round x1, y1 - x2, y2: a black rule, a white one inside it
; along the top and the left, a dark one along the bottom and the right
%macro DBEVEL 4
    DFRAME R_EDGE, %1, %2, %3, %4
    DRECT R_HI, %1 + 1, %2 + 1, %3 - 1, %2 + 1
    DRECT R_HI, %1 + 1, %2 + 2, %1 + 1, %4 - 1
    DRECT R_SH, %1 + 2, %4 - 1, %3 - 1, %4 - 1
    DRECT R_SH, %3 - 1, %2 + 2, %3 - 1, %4 - 2
%endmacro
; ...and a recessed one: the shadow above and left, the light below and right
%macro DSUNK 4
    DFRAME R_EDGE, %1, %2, %3, %4
    DRECT R_SH, %1 + 1, %2 + 1, %3 - 1, %2 + 1
    DRECT R_SH, %1 + 1, %2 + 2, %1 + 1, %4 - 1
    DRECT R_HI, %1 + 2, %4 - 1, %3 - 1, %4 - 1
    DRECT R_HI, %3 - 1, %2 + 2, %3 - 1, %4 - 2
%endmacro
X3_SCREW    equ (R_GND << 8) | R_LABEL

; --- FULL: 512 x 352 ---------------------------------------------------------------
x3_dfull:
    DRECT R_GND, 0, 0, X3_FW - 1, X3_FH - 1
    ; the header: screws, the wordmark, its rule, the legend
    DPIC x3art_screw, 3, 10, X3_SCREW
    DPIC x3art_screw, X3_FW - 12, 10, X3_SCREW
    DPIC x3art_logof, 24, 7, X3_SCREW
    DRECT R_EDGE, 128, 5, 128, 24
    DRECT R_HI, 129, 5, 129, 24
    DSTR x3_fcapst, x3_s_tag, 142, 11, X3_SCREW
    ; the deck's panel
    DBEVEL 14, 31, X3_FW - 15, 227
    ; the LCD, recessed, and its glass
    DSUNK 24, 39, 297, 176
    DFRAME R_LCDRIM, 26, 41, 295, 174
    DRECT R_LCD, 27, 42, 294, 173
    DFRAME R_PBFRM, 42, 120, 281, 134
    ; the transport's divider
    DRECT R_EDGE, 236, 188, 236, 213
    DRECT R_HI, 237, 188, 237, 213
    ; the dials' bezels
    DFRAME R_EDGE, 307, 38, 396, 99
    DFRAME R_BEZEL, 308, 39, 395, 98
    DFRAME R_BEZEL, 309, 40, 394, 97
    DFRAME R_EDGE, 402, 38, 491, 99
    DFRAME R_BEZEL, 403, 39, 490, 98
    DFRAME R_BEZEL, 404, 40, 489, 97
    ; OUTPUT, on its rule
    DRECT R_EDGE, 312, 117, 374, 117
    DRECT R_EDGE, 425, 117, 488, 117
    DCSTR x3_fcapsb, x3_s_output, 400, 114, X3_SCREW
    ; the grilles, and the knob's printed parts
    DPIC x3art_grillf, 312, 154, X3_SCREW
    DPIC x3art_grillf, 446, 154, X3_SCREW
    dw D_KNOBTX
    ; the screws between the deck and the queue
    DPIC x3art_screw, 3, 229, X3_SCREW
    DPIC x3art_screw, X3_FW - 12, 229, X3_SCREW
    ; the queue's box, its margins, the scroll bar's arrows
    DFRAME R_EDGE, 17, 243, 474, 319
    DRECT R_QBOX, 18, 244, 473, 248
    DRECT R_QBOX, 18, 309, 473, 318
    DFRAME R_EDGE, 476, 243, 490, 319
    DPIC x3art_sbup, 477, 244, (R_STAT << 8) | R_LABEL
    DPIC x3art_sbdn, 477, 307, (R_STAT << 8) | R_LABEL
    DRECT R_EDGE, 477, 256, 489, 256
    DRECT R_EDGE, 477, 306, 489, 306
    ; the status line
    DRECT R_EDGE, 0, 325, X3_FW - 1, 325
    DRECT R_STAT, 0, 326, X3_FW - 1, X3_FH - 1
    DPIC x3art_screw, 3, 334, (R_STAT << 8) | R_LABEL
    DPIC x3art_screw, X3_FW - 12, 334, (R_STAT << 8) | R_LABEL
    DRECT R_EDGE, 220, 330, 220, 347
    DRECT R_HI, 221, 330, 221, 347
    dw D_END

x3_efull:
    dw x3f_e_title, 0, x3f_e_time, 0, x3f_e_total, 0, x3f_e_pbar, 0
    dw x3f_e_fmt, 0, x3f_e_dial, 0, x3f_e_dial, 1, x3f_e_dlab, 0
    dw x3f_e_dlab, 1, x3f_e_knob, 0
    dw x3f_e_qrow, 0, x3f_e_qrow, 1, x3f_e_qrow, 2, x3f_e_qsb, 0
    dw x3f_e_sbq, 0, x3f_e_sbst, 0
x3_efull_end:
    dw 0

; the keys: rewind, play, pause, stop, fast forward, OPEN
%macro X3BTN 4
    dw %1, %2, %1 + %3 - 1, %2 + %4 - 1
%endmacro
%macro X3PARKB 0
    dw X3_PARK, X3_PARK, X3_PARK, X3_PARK
%endmacro
x3_bfull:
%assign i 0
%rep 5
    X3BTN 26 + i * 41, 184, 37, 34
%assign i i+1
%endrep
    X3BTN 246, 184, 52, 34

x3_dgfull:                          ; the dials (DG_*)
    dw 310, 405, 41, X3ART_DIALF_H
    dw x3art_dialf_k, x3art_dialf_r, x3art_dialf_bw, x3art_ndf
    dw X3ART_NF_PX, X3ART_NF_PY, x3art_dsf_vu, x3art_dsf_buf
    dw 102, 322, 417, 56
x3_kgfull:                          ; the knob (KG_*)
    dw 360, 140, X3ART_KCF_W, X3ART_KCF_H, X3ART_KCF_KX, X3ART_KCF_KY
    dw x3art_knobf_p0, x3art_knobf_p1, x3art_knobf_bw, X3ART_KNOBF_H
    dw x3art_kif, x3art_krf
x3_qgfull:                          ; the queue (QG_*)
    dw 18, 249, 20, 3, 4, 5, 477, 257, 305

; --- MID: 512 x 248, EGA and Hercules ----------------------------------------------------
x3_dmid:
    DRECT R_GND, 0, 0, X3_FW - 1, X3_MH - 1
    DPIC x3art_screwm, 3, 7, X3_SCREW
    DPIC x3art_screwm, X3_FW - 12, 7, X3_SCREW
    DPIC x3art_logom, 24, 5, X3_SCREW
    DRECT R_EDGE, 128, 3, 128, 18
    DRECT R_HI, 129, 3, 129, 18
    DSTR x3_fcapst, x3_s_tag, 142, 7, X3_SCREW
    DBEVEL 14, 22, X3_FW - 15, 163
    DSUNK 24, 27, 297, 129
    DFRAME R_LCDRIM, 26, 29, 295, 127
    DRECT R_LCD, 27, 30, 294, 126
    DFRAME R_PBFRM, 42, 89, 281, 100
    DRECT R_EDGE, 236, 136, 236, 157
    DRECT R_HI, 237, 136, 237, 157
    DFRAME R_EDGE, 307, 24, 396, 69
    DFRAME R_BEZEL, 308, 25, 395, 68
    DFRAME R_BEZEL, 309, 26, 394, 67
    DFRAME R_EDGE, 402, 24, 491, 69
    DFRAME R_BEZEL, 403, 25, 490, 68
    DFRAME R_BEZEL, 404, 26, 489, 67
    DRECT R_EDGE, 312, 83, 374, 83
    DRECT R_EDGE, 425, 83, 488, 83
    DCSTR x3_fcapsb, x3_s_output, 400, 80, X3_SCREW
    DPIC x3art_grillm, 312, 112, X3_SCREW
    DPIC x3art_grillm, 446, 112, X3_SCREW
    dw D_KNOBTX
    DPIC x3art_screwm, 3, 166, X3_SCREW
    DPIC x3art_screwm, X3_FW - 12, 166, X3_SCREW
    DFRAME R_EDGE, 17, 175, 474, 215
    DRECT R_QBOX, 18, 176, 473, 178
    DRECT R_QBOX, 18, 211, 473, 214
    DFRAME R_EDGE, 476, 175, 490, 215
    DPIC x3art_sbupm, 477, 176, (R_STAT << 8) | R_LABEL
    DPIC x3art_sbdnm, 477, 207, (R_STAT << 8) | R_LABEL
    DRECT R_EDGE, 477, 184, 489, 184
    DRECT R_EDGE, 477, 206, 489, 206
    DRECT R_EDGE, 0, 222, X3_FW - 1, 222
    DRECT R_STAT, 0, 223, X3_FW - 1, X3_MH - 1
    DPIC x3art_screwm, 3, 232, (R_STAT << 8) | R_LABEL
    DPIC x3art_screwm, X3_FW - 12, 232, (R_STAT << 8) | R_LABEL
    DRECT R_EDGE, 220, 226, 220, 243
    DRECT R_HI, 221, 226, 221, 243
    dw D_END

x3_emid:
    dw x3f_e_title, 0, x3f_e_time, 0, x3f_e_total, 0, x3f_e_pbar, 0
    dw x3f_e_fmt, 0, x3f_e_dial, 0, x3f_e_dial, 1, x3f_e_dlab, 0
    dw x3f_e_dlab, 1, x3f_e_knob, 0
    dw x3f_e_qrow, 0, x3f_e_qrow, 1, x3f_e_qsb, 0
    dw x3f_e_sbq, 0, x3f_e_sbst, 0
    dw 0

x3_bmid:
%assign i 0
%rep 5
    X3BTN 26 + i * 41, 135, 37, 26
%assign i i+1
%endrep
    X3BTN 246, 135, 52, 26

x3_dgmid:
    dw 310, 405, 27, X3ART_DIALM_H
    dw x3art_dialm_k, x3art_dialm_r, x3art_dialm_bw, x3art_ndm
    dw X3ART_NM_PX, X3ART_NM_PY, x3art_dsm_vu, x3art_dsm_buf
    dw 71, 322, 417, 56
x3_kgmid:
    dw 360, 100, X3ART_KCM_W, X3ART_KCM_H, X3ART_KCM_KX, X3ART_KCM_KY
    dw x3art_knobm_p0, x3art_knobm_p1, x3art_knobm_bw, X3ART_KNOBM_H
    dw x3art_kim, x3art_krm
x3_qgmid:
    dw 18, 179, 16, 2, 2, 3, 477, 185, 205

; --- COMPACT: the CGA's (C1) and a VGA's (C2) ---------------------------------------------
x3_dc1:
    DRECT R_GND, 0, 0, X3_CW - 1, X3_C1H - 1
    DFRAME R_EDGE, 2, 1, X3_CW - 3, 47
    DFRAME R_LCDRIM, 3, 2, X3_CW - 4, 46
    DRECT R_LCD, 4, 3, X3_CW - 5, 45
    DFRAME R_PBFRM, 12, 38, 427, 43
    DRECT R_EDGE, 230, 53, 230, 66
    DRECT R_EDGE, 0, 71, X3_CW - 1, 71
    DRECT R_STAT, 0, 72, X3_CW - 1, X3_C1H - 1
    DRECT R_EDGE, 212, 74, 212, 85
    dw D_END

x3_dc2:
    DRECT R_GND, 0, 0, X3_CW - 1, X3_C2H - 1
    DSUNK 2, 2, X3_CW - 3, 93
    DFRAME R_LCDRIM, 4, 4, X3_CW - 5, 91
    DRECT R_LCD, 5, 5, X3_CW - 6, 90
    DFRAME R_PBFRM, 14, 74, 425, 86
    DRECT R_EDGE, 230, 104, 230, 129
    DRECT R_HI, 231, 104, 231, 129
    DRECT R_EDGE, 0, 141, X3_CW - 1, 141
    DRECT R_STAT, 0, 142, X3_CW - 1, X3_C2H - 1
    DRECT R_EDGE, 212, 146, 212, 166
    DRECT R_HI, 213, 146, 213, 166
    dw D_END

x3_ecmp:
    dw x3f_e_title, 0, x3f_e_time, 0, x3f_e_total, 0, x3f_e_pbar, 0
    dw x3f_e_fmt, 0, x3f_e_sbq, 0, x3f_e_sbst, 0
    dw 0

x3_bc1:
%assign i 0
%rep 5
    X3BTN 6 + i * 44, 51, 40, 18
%assign i i+1
%endrep
    X3BTN 238, 51, 64, 18
x3_bc2:
%assign i 0
%rep 5
    X3BTN 6 + i * 44, 100, 40, 34
%assign i i+1
%endrep
    X3BTN 238, 100, 64, 34

; the pictures and captions, button order
x3_ibig:
    dw x3art_i_rew, x3art_i_play, x3art_i_pause, x3art_i_stop, x3art_i_ff, 0
x3_ic1:
    dw x3art_h_rew, x3art_h_play, x3art_h_pause, x3art_h_stop, x3art_h_ff
    dw x3art_h_eject
x3_cbtn:
    dw 0, 0, 0, 0, 0, x3_s_open

; the four layout records (L_*)
x3_lfull:
    dw X3_FW, X3_FH, x3_dfull, x3_efull, x3_bfull, x3_ibig, x3_cbtn
    dw 42, 54, x3_flcdf, 26
    dw 42, 70, x3_dstf
    dw 186, 88, x3_dsnf
    dw 43, 121, 280, 133
    dw 42, 151, 86
    dw x3_dgfull, x3_kgfull, x3_qgfull
    dw 335, 24, 24, 232, 32
    dw 0
x3_lmid:
    dw X3_FW, X3_MH, x3_dmid, x3_emid, x3_bmid, x3_ibig, x3_cbtn
    dw 42, 37, x3_flcdm, 26
    dw 42, 50, x3_dstm
    dw 186, 63, x3_dsnm
    dw 43, 90, 280, 99
    dw 42, 111, 86
    dw x3_dgmid, x3_kgmid, x3_qgmid
    dw 231, 24, 24, 232, 32
    dw 0
x3_lc1:
    dw X3_CW, X3_C1H, x3_dc1, x3_ecmp, x3_bc1, x3_ic1, x3_cbtn
    dw 12, 6, 0, 30
    dw 12, 17, x3_dstc
    dw 148, 26, x3_dsnc
    dw 13, 39, 426, 42
    dw 262, 7, 298
    dw 0, 0, 0
    dw 76, 8, 25, 220, 27
    dw 1
x3_lc2:
    dw X3_CW, X3_C2H, x3_dc2, x3_ecmp, x3_bc2, x3_ibig, x3_cbtn
    dw 14, 13, x3_flcdf, 24
    dw 14, 30, x3_dstf
    dw 160, 48, x3_dsnf
    dw 15, 75, 424, 85
    dw 262, 15, 298
    dw 0, 0, 0
    dw 152, 8, 25, 220, 27
    dw 1
%if (x3_lmid - x3_lfull) != L_SIZE || (x3_lc1 - x3_lmid) != L_SIZE
%error "an XTMP3 layout record is L_SIZE bytes"
%endif
%if (x3_lc2 - x3_lc1) != L_SIZE
%error "an XTMP3 layout record is L_SIZE bytes"
%endif
%if (x3_efull_end - x3_efull) / 4 > X3F_NKEYS
%error "X3F_NKEYS is short of the full player's elements"
%endif
