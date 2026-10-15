; =============================================================================
; THE GROUND'S TEXTURE (SPEC.md 88.13.1.5) - Detail Level Ultra
;
; A flat ground gives a pilot nothing to read height or speed off until a
; building comes by. Two things fix that, in the form Flight Simulator 3.0
; had them:
;
;   - the GROUND ITSELF is not one flat ink - the Texture box, F7, on every
;     rung (88.13.1.5.2): cs_grsync swaps its four row patterns for a
;     two-tone set, so the green carries dots on CGA and the text hack, and
;     on Mode X dots written through the card's latches (cs_mxlon), the way
;     Hercules' ground always has;
;   - and at Ultra FIELDS lie on it, SPARSE, LARGE and IRREGULAR: one convex patch of
;     CSI_FIELD or CSI_FIELD2 in five tiles of every eight, each up to two
;     kilometres across. The first cut was six squares in every kilometre and
;     read as a chessboard; the field put it beside FS3's screenshots.
;
; THE FIELDS ARE OBJECTS, and that is why this file is short. A tile is a row
; in cs_gtobj with one of five shared FLAT models, re-placed each frame and
; handed to cs_consider like any other, so the cull, the range at the Draw
; Distance, the near plane under the wheels, the projection, the fill and the
; span marks are all the world's own. What is new is the model TYPE:
; CSM_TEXT is a flat model (its CSM_FLAT bit set, which is the bit cs_drawobj
; tests) that cs_drawpass draws in a pass of its OWN, FIRST - so the runway,
; the water and the roads lie on the fields and not under them. A world never
; names it, so the map and the water test, which walk the world's own table,
; never see it.
;
; WHICH model a tile carries is a hash of the tile's own coordinates, so a
; field is fixed to the ground and the aeroplane flies over it, which is the
; whole of what a texture is for: (tx + 3 tz) mod 8 picks a row of cs_gtmod,
; and three rows of the eight are empty. Neighbours step the index by 1 east
; and 3 north, so no two adjacent tiles carry the same model - and the three
; empties are rows 0, 2 and 4, no two of them 1 or 3 apart, so no two empty
; tiles are adjacent either. The first table had them at 1, 4 and 6, which
; is three in a row going north: a whole view of bare ground.
;
; THE TILES ARE FILED LAST, after every object of the world: cs_vkey drops the
; object that does not fit (CS_NVIS), and the one dropped must be a field and
; never the Eiffel Tower.
; =============================================================================

CS_GTT   equ 2048                   ; a tile, metres: a POWER OF TWO, so the
CS_GTSH  equ 11                     ; snap to the grid is an AND, and its log
CS_GTN   equ 5                      ; tiles a side, round the aeroplane's own
CS_GTR   equ 4400                   ; ...and drawn within this (Manhattan), at
                                    ; Moderate's Draw Distance
CS_GTF   equ 16                     ; ...and at most this many of the 25 carry a
                                    ; field: cs_gtmod's three empties, walked
                                    ; over every offset of the 5x5 against
                                    ; (tx + 3 tz) mod 8, leave 15 or 16. So
                                    ; cs_gtobj is 16 rows, not 25 - 180 bytes,
                                    ; which is what put the CSDIAG build back
                                    ; inside one segment - and cs_gtex stops at
                                    ; the end of it if the table ever changes

; --- the five fields: one convex patch each, placed and shaped by hand ------
cs_m_gta: db CSM_TEXT, 7, 1, 0, CSI_LINE, 0
    dw 861, .v, .f, 0
.v: dw -700, -500,  -100, -850,  550, -650,  850, -50,  600, 550,  -150, 700,  -750, 250
.f: db 7, CSI_FIELD, CSF_NOCULL, 0, 1, 2, 3, 4, 5, 6
cs_m_gtb: db CSM_TEXT, 5, 1, 0, CSI_LINE, 0
    dw 922, .v, .f, 0
.v: dw -900, -200,  -300, -600,  700, -450,  820, 100,  -450, 430
.f: db 5, CSI_FIELD2, CSF_NOCULL, 0, 1, 2, 3, 4
cs_m_gtc: db CSM_TEXT, 6, 1, 0, CSI_LINE, 0
    dw 744, .v, .f, 0
.v: dw -100, -300,  450, -350,  700, 100,  500, 550,  0, 600,  -300, 200
.f: db 6, CSI_FIELD, CSF_NOCULL, 0, 1, 2, 3, 4, 5
cs_m_gtd: db CSM_TEXT, 5, 1, 0, CSI_LINE, 0
    dw 993, .v, .f, 0
.v: dw -650, -750,  350, -820,  880, -300,  250, 620,  -720, 120
.f: db 5, CSI_FIELD2, CSF_NOCULL, 0, 1, 2, 3, 4
cs_m_gte: db CSM_TEXT, 6, 1, 0, CSI_LINE, 0
    dw 821, .v, .f, 0
.v: dw -800, -100,  -500, -650,  150, -700,  450, -200,  200, 350,  -550, 400
.f: db 6, CSI_FIELD, CSF_NOCULL, 0, 1, 2, 3, 4, 5

cs_gtmod: dw 0, cs_m_gta, 0, cs_m_gtb, 0, cs_m_gtc, cs_m_gtd, cs_m_gte

; --- the ground's own patterns, plain and textured, per backend (CSB_* - 1):
;     Hercules' ground was already dots and keeps them ------------------------
cs_grpat:
    db 1, 1, 1, 1,              CS_MXLAT + 4, CS_MXLAT + 5, CS_MXLAT + 6, CS_MXLAT + 7
                                    ; Mode X: LATCHED (cs_mxlon), the tile
                                    ; below
    db 0x55, 0x55, 0x55, 0x55,  0x55, 0xD5, 0x55, 0x5D              ; CGA
    db 0x88, 0x00, 0x22, 0x00,  0x88, 0x00, 0x22, 0x00              ; Hercules
    db 0x22, 0x22, 0x22, 0x22,  0x22, 0x62, 0x22, 0x22              ; text hack

; --- Mode X's ground TILE (cs_mxlon): a CHECKER of two greens, four rows of
;     four pixels in DAC indices - and a second one for the haze's ground
;     band, the same checker a shade toward the sky, so the band carries the
;     ground's texture up to the horizon instead of standing out flat. Rows 0
;     to 3 are the band's (latch bytes CS_MXPAT + 0..3), 4 to 7 the ground's.
;     The field chose it of four (Dots, Dither, Wide, Lines - 88.13.1.5.2):
;     "Dither wins, by a huge amount" ------------------------------------
CS_MXG equ CSI_GRALT
CS_MXH equ CSI_HZGALT
cs_mxtilet:
    db CSI_HZG1, CS_MXH, CSI_HZG1, CS_MXH
    db CS_MXH, CSI_HZG1, CS_MXH, CSI_HZG1
    db CSI_HZG1, CS_MXH, CSI_HZG1, CS_MXH
    db CS_MXH, CSI_HZG1, CS_MXH, CSI_HZG1
    db 1, CS_MXG, 1, CS_MXG
    db CS_MXG, 1, CS_MXG, 1
    db 1, CS_MXG, 1, CS_MXG
    db CS_MXG, 1, CS_MXG, 1

; ...and the haze band's own row in the Mode X ink table, plain and latched
cs_mxhzp: db CSI_HZG1, CSI_HZG1, CSI_HZG1, CSI_HZG1
          db CS_MXLAT, CS_MXLAT + 1, CS_MXLAT + 2, CS_MXLAT + 3

; cs_mxtile - the tiles into the eight latch bytes at CS_MXPAT, one plane at a
;             time (a byte's plane k is the pixel x = 4n + k). Clobbers AX,
;             CX, DX, SI, DI, ES
cs_mxtile:
    mov es, [cs_tseg]
    mov si, cs_mxtilet
    mov dx, 0x3C4
    mov ax, 0x0102                  ; plane 0 first
.k:
    out dx, ax
    mov di, CS_MXPAT
    mov cx, 8
.p:
    mov al, [si]
    mov [es:di], al
    add si, 4                       ; the next row, same column
    inc di
    loop .p
    sub si, 31                      ; ...back to row 0, the next column
    mov al, 2
    shl ah, 1
    cmp ah, 0x10
    jb .k
    mov ax, 0x0F02
    out dx, ax
    ret

; -----------------------------------------------------------------------------
; cs_grsync - the live ink table's GROUND row to the Texture box's, once a
;             frame. A change refills every view row, because a row of the
;             ground's KIND is otherwise "as it was" and is never laid again
;             (88.3.1). Asked every frame rather than on a change, so a poke,
;             the 286 boost and every path into the setting are all one path.
;             Clobbers AX, CX, DX, SI, DI, ES
; -----------------------------------------------------------------------------
cs_grsync:
    mov al, [cs_back]
    dec al
    cmp al, CSB_C160 - 1
    ja .out                         ; no bracket: nothing to draw into
    or al, al                       ; (AL 0 is Mode X: CSB_MODEX - 1)
    jnz .gc
    ; --- MODE X: the Graphics Mode register's other bits, EVERY frame and
    ;     whatever the Texture: cs_mxarm writes the mode whole rather than
    ;     reading it per run, and every band is laid in write mode 1 now
    ;     (88.13.1.5.2.1), so a frame that never set it would put the card
    ;     out of its 256-colour shift
    mov dx, 0x3CE
    mov al, 5
    out dx, al
    inc dx
    in al, dx
    and al, 0xFC
    mov [cs_mxgc5], al
    mov byte [cs_mxon], 0
    xor al, al
.gc:
    mov cl, 3
    shl al, cl
    xor ah, ah
    mov si, ax
    add si, cs_grpat
    test byte [cs_setfill], CSFL_GTEX
    jz .have                        ; the Texture box (88.13.1.5.2)
    add si, 4
    cmp byte [cs_back], CSB_MODEX   ; ...and Mode X's latch bytes, EVERY
    jne .have                       ; frame: they are VRAM, and a new bracket
    push si                         ; is a new mode set (32 stores)
    call cs_mxtile
    pop si
.have:
    mov di, [cs_inktab]
    add di, CSI_GROUND * 4
    mov ax, [si]
    cmp ax, [di]
    jne .set
    mov ax, [si + 2]
    cmp ax, [di + 2]
    je .out
.set:
    push ds
    pop es
    cld
    movsw
    movsw
    cmp byte [cs_back], CSB_MODEX   ; Mode X: the haze's ground band takes
    jne .inv                        ; its own latched checker with the ground
    mov si, cs_mxhzp                ; (cs_mxtilet's first four rows)
    test byte [cs_setfill], CSFL_GTEX
    jz .hz
    add si, 4
.hz:
    mov di, [cs_inktab]
    add di, CSI_HZG1 * 4
    movsw
    movsw
.inv:
    mov byte [cs_ink], 0xFF         ; cs_setink's cache named the old one
    mov di, cs_rowkind              ; ...and every row is unknown
    mov cx, CS_MAXROW
    mov al, 3
    rep stosb
.out:
    ret

; -----------------------------------------------------------------------------
; cs_gtex - place the tiles round the eye and file them. Clobbers everything
; -----------------------------------------------------------------------------
cs_gtex:
    mov si, [cs_airport]
    mov ax, [si + CSA_ELEV]
    mov [cs_gtel], ax
    mov ax, [cs_ex]                 ; the first column's centre: the eye's
    and ax, ~(CS_GTT - 1)           ; tile, less two, plus half a tile
    sub ax, (CS_GTN / 2) * CS_GTT - CS_GTT / 2
    mov [cs_gtx0], ax
    mov dx, [cs_ez]
    and dx, ~(CS_GTT - 1)
    sub dx, (CS_GTN / 2) * CS_GTT - CS_GTT / 2
    mov si, cs_gtobj
    mov ch, CS_GTN
.z:
    mov bx, [cs_gtx0]
    mov cl, CS_GTN
.x:
    push cx                         ; --- the tile's model, off its own
    mov ax, dx                      ;     coordinates: (tx + 3 tz) mod 8 ---
    mov cl, CS_GTSH
    sar ax, cl
    mov di, ax
    shl ax, 1
    add di, ax                      ; 3 tz
    mov ax, bx
    sar ax, cl
    add di, ax                      ; + tx
    and di, 7
    shl di, 1
    mov di, [cs_gtmod + di]
    pop cx
    or di, di
    jz .next                        ; an empty tile: the plain ground
    cmp si, cs_gtobj + CS_GTF * CSO_SIZE
    jae .next                       ; (the rows are full: see CS_GTF)
    mov [si + CSO_MODEL], di
    mov word [si + CSO_FAR], 0
    mov [si + CSO_X], bx
    mov [si + CSO_Z], dx
    mov ax, [cs_gtel]
    mov [si + CSO_Y], ax
    mov word [si + CSO_RANGE], CS_GTR
    mov word [si + CSO_LOD], 0
    mov word [si + CSO_NAME], 0
    mov word [si + CSO_FLAGS], CSO_TERRAIN
    mov word [si + CSO_SKIP], 0     ; a tile is somewhere else every time it
    push bx                         ; moves, so no skip it learnt still holds
    push cx
    push dx
    push si
    call cs_consider
    pop si
    pop dx
    pop cx
    pop bx
    add si, CSO_SIZE
.next:
    add bx, CS_GTT
    dec cl
    jnz .x
    add dx, CS_GTT
    dec ch
    jnz .z
    ret
