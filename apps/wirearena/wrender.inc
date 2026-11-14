; WireArena's renderer (docs/plans/MULTIPLAYER-FPS-PLAN.md sections 4, 16).
;
; 1. The arena's walls are one-sided segments in a host-built BSP
;    (tools/arenagen.py), walked FRONT TO BACK: the near side of a node, its
;    own segments, the far side. A segment facing away is dropped by one
;    compare; one facing us has its two vertices transformed (two 32-bit adds
;    each, off per-coordinate products made once a frame - the walls are
;    axis-aligned) and projected (cached per vertex), is clipped only when a
;    vertex is outside the view, and is then STEPPED across its columns.
; 2. A column a nearer wall already drew is CLOSED (wa_cov): a wall is full
;    height, so nothing behind it can show there. An open column is drawn on
;    the spot - its inverse depth into wa_colz for the figures, the top and
;    bottom edge glyphs at half-row precision (`─` upper half, `_` lower),
;    `/` or `\` where an edge steps two half-rows or more, `│` at a segment's
;    real corners - and closed. The walk stops when no column is open.
; 3. Figures, far to near, as limb lines at 2x2 SUB-CELL precision, each
;    sub-pixel depth-tested against its column's wall; a cell's four bits
;    pick a solid block glyph. A figure cell replaces a wall cell.
; 4. The crosshair.
; Every cell drawn this frame is stamped and listed; wa_present writes the
; ones that differ from what the glass holds and blanks last frame's cells
; nobody drew this time. Nothing else is touched, so a still view costs no
; video writes at all.
;
; Wave 0's first renderer rasterised every face into a per-column depth
; buffer and drew edges from it in a second pass: 13.0 fps at 80 columns on
; the bench. This one does both in one pass, over the visible walls only.

WA_MAXSP equ 127                ; spans a frame the table holds (a tag x 2
                                ; is a byte: 127 at most) - heights' low
                                ; boxes register an edge each top
%macro WR_BSS 0
WA_VAR wa_colz, WA_MAXW*2       ; nearest face's inverse depth (0 = none)
WA_VAR wa_colt, WA_MAXW*2       ; its top, quarter half-rows
WA_VAR wa_colb, WA_MAXW*2       ; its bottom
WA_VAR wa_covpad, 2             ; (0: what column -1 reads in a steep test)
WA_VAR wa_cov, WA_MAXW*2+2      ; the tag of the span that drew a column
                                ; (+1: the flat passes' end sentinel)
WA_VAR wa_fh, 2                 ; a flat pass's edge: half-row (wa_fedge)
WA_VAR wa_qt, 2                 ; the flat span's top: fraction, the step's
WA_VAR wa_ft, 2                 ;   fraction, direction (0 down the screen,
WA_VAR wa_dt, 2                 ;   -1 up)
WA_VAR wa_qb, 2                 ; ...and its bottom
WA_VAR wa_fb, 2
WA_VAR wa_db, 2
WA_VAR wa_tag, 2                ; this frame's last span tag (x 2)
WA_VAR wa_spcs, WA_MAXSP*2      ; per span: c1 (x16), inverse depth there,
WA_VAR wa_spih, WA_MAXSP*2      ; c2 - c1 and i2 - i1: wa_wallz's line
WA_VAR wa_spsh, WA_MAXSP*2
WA_VAR wa_spsl, WA_MAXSP*2
WA_VAR wa_wcol, 1               ; the colour of the walls being drawn
WA_VAR wa_dxc, 9*4              ; the floor grid's products (wa_dots)
WA_VAR wa_dxs, 9*4
WA_VAR wa_dyc, 9*4
WA_VAR wa_dys, 9*4
WA_VAR wa_dnx, 2
WA_VAR wa_diz, 2
WA_VAR wa_dny, 2
WA_VAR wa_dix, 2
WA_VAR wa_diy, 2
WA_VAR wa_vt0, 2
WA_VAR wa_stl, 2
WA_VAR wa_sbl, 2
WA_VAR wa_vbl, 2
WA_VAR wa_vb0, 2
WA_VAR wa_afix, 1               ; the span's shade is one band
WA_VAR wa_fclp, 2               ; wa_fclpc: 4 the clip-tested passes, else 0
WA_VAR wa_p2i, 2                ; ...and pass 2's (4 too for a thin wall's)
WA_VAR wa_cflat, 1              ; wa_cclip holds a flat arena's (wa_collook)
WA_VAR wa_tup, 1                ; SPANLOOP: the top moves up the screen or
WA_VAR wa_bdn, 1                ; not at all, the bottom down (wa_span.gen1)
WA_VAR wa_lvb, 2                ; wa_flat's LEVEL test: 7 while an edge that
WA_VAR wa_lvt, 2                ; moves under a half-row is drawn level
                                ; (LK_COARSE), else 0 - the bottom's and the
                                ; top's (0 too where the top is not drawn:
                                ; the sparse level, ROOMF) - wa_collook
WA_VAR wa_ftailp, 2             ; wa_fspan's way on: FLATBODY's tail
WA_VAR wa_roomf, 1              ; ROOMF: non-zero while the room's faces are
                                ; drawn at a level with it (wa_render)
WA_VAR wa_vertp, 2              ; wa_vert or wa_vertr: the level's (wa_lookset)
WA_VAR wa_lnear, 2              ; the cone test's near margin: the level's
WA_POSTMIN equ 8                ; NOPOST: the shortest wall with posts
WA_VAR wa_left, 2               ; columns still open
WA_VAR wa_lp, 2                 ; the list's write pointer across spans
WA_VAR wa_xc, WA_MXV*4          ; per frame: (x - px) cos and sin, 32 bits,
WA_VAR wa_xs, WA_MXV*4          ; one per distinct x of the arena...
WA_VAR wa_yc, WA_MYV*4          ; ...and per distinct y: a vertex is two
WA_VAR wa_ys, WA_MYV*4          ; 32-bit adds, not four multiplies
WA_VAR wa_vst, WA_MVTX*2        ; a vertex's cache stamp (the frame number)
WA_VAR wa_vok, WA_MVTX*2        ; 1: inside the view, projected below
WA_VAR wa_vx, WA_MVTX*2         ; camera space
WA_VAR wa_vz, WA_MVTX*2
WA_VAR wa_vpc, WA_MVTX*2        ; projected: column x16, top, bottom,
WA_VAR wa_vpt, WA_MVTX*2        ; inverse depth
WA_VAR wa_vpb, WA_MVTX*2
WA_VAR wa_vpi, WA_MVTX*2
WA_VAR wa_fv, 2
WA_VAR wa_bvis, 1
WA_VAR wa_endok, 1
WA_VAR wa_mbot, 2
WA_VAR wa_e1, 1                 ; this span's ends are REAL corners (not
WA_VAR wa_e2, 1                 ; cut by the frustum): a vertical edge each
WA_VAR wa_pdrawn, 1             ; the last column of this span was drawn
WA_VAR wa_cs0, 2
WA_VAR wa_steep, 1              ; the span's edges can step >= 2 half-rows
WA_VAR wa_crow, 2               ; this box's corner row (wa_corn)
WA_VAR wa_ce0, 2
WA_VAR wa_at, 8                 ; this frame's attributes (A_*)
WA_VAR wa_cos, 2
WA_VAR wa_sinv, 2
WA_VAR wa_cang, 2
WA_VAR wa_hq, 2                 ; horizon, quarter half-rows
WA_VAR wa_hh, 2                 ; horizon, half-rows
WA_VAR wa_numt, 2
WA_VAR wa_numb, 2
WA_VAR wa_x1, 2
WA_VAR wa_z1, 2
WA_VAR wa_x2, 2
WA_VAR wa_z2, 2
WA_VAR wa_d1, 2
WA_VAR wa_d2, 2
WA_VAR wa_c1, 2
WA_VAR wa_t1, 2
WA_VAR wa_b1, 2
WA_VAR wa_i1, 2
WA_VAR wa_c2, 2
WA_VAR wa_t2, 2
WA_VAR wa_b2, 2
WA_VAR wa_i2, 2
WA_VAR wa_pht, 2                ; SPANLOOP: the column before's edges, half-
WA_VAR wa_phb, 2                ; rows (a step's fill)
WA_VAR wa_ht, 2
WA_VAR wa_hroff, WA_HR*2         ; a half-row's cell row offset
WA_VAR wa_hrfb, WA_HR*2          ; ...and its row's place in wa_fbuf
WA_VAR wa_hb, 2
WA_VAR wa_cs, 2
WA_VAR wa_end, 2                ; the span loop's end, column x 2
WA_VAR wa_ncl, 2                ; ...and the columns it found shut
WA_VAR wa_fs, 2
WA_VAR wa_dc, 2
WA_VAR wa_st, 2
WA_VAR wa_sb, 2
WA_VAR wa_slo, 2
WA_VAR wa_shi, 2
WA_VAR wa_vb, 2
WA_VAR wa_vil, 2
WA_VAR wa_vih, 2
WA_VAR wa_fgib, 1               ; the figure being drawn: its bits alone
WA_VAR wa_fshl, 1               ;   (GIB), its chest yellow (a shield)
WA_VAR wa_thw, 2                ; wa_thing's shape: half width, from and
WA_VAR wa_th0, 2                ;   to above its z, units; attribute
WA_VAR wa_th1, 2
WA_VAR wa_tha, 1
WA_VAR wa_thhb, 1               ;   a column's quadrant bits
WA_VAR wa_pkm, 1                ;   the pickups still to draw
WA_VAR wa_thz, 2                ;   its z
WA_VAR wa_thdx, 2               ;   its offset from the eye, x and y
WA_VAR wa_thdy, 2
WA_VAR wa_ovoldn, 2             ; the overlay's cells as it last came in:
WA_VAR wa_ovold, WA_MOV*2       ;   how many, and each's offset
WA_VAR wa_thiz, 2               ;   its inverse depth
WA_VAR wa_thfs, 2               ;   sub-columns a unit, 8.8
WA_VAR wa_thl0, 2               ;   its sub-columns, first and last
WA_VAR wa_thr0, 2
WA_VAR wa_tht, 2                ;   its half-rows, first and last
WA_VAR wa_thb, 2
WA_VAR wa_thl, 2                ;   this column's last
WA_VAR wa_fiz, 2                ; the figure being drawn: inverse depth
WA_VAR wa_fatt, 1               ;   its attribute
WA_VAR wa_fsub, 1               ;   keyframe blend, quarters
WA_VAR wa_fs8, 2                ;   sub-columns a unit, 8.8
WA_VAR wa_fv8, 2                ;   half-rows a unit, 8.8
WA_VAR wa_fcx, 2                ;   its centre, camera x
WA_VAR wa_pa, 2                 ;   keyframe pointers
WA_VAR wa_pb, 2
WA_VAR wa_jsx, WA_NJ*2
WA_VAR wa_jsy, WA_NJ*2
WA_VAR wa_fxs, WA_NFIG*2        ; camera x and z per figure this frame
WA_VAR wa_fzs, WA_NFIG*2
WA_VAR wa_ford, WA_NFIG*2       ; those in view, far to near (x 2)
WA_VAR wa_frec, WA_NFIG*2       ; ...and each one's record
WA_VAR wa_lx1, 2
WA_VAR wa_ly1, 2
WA_VAR wa_ldx, 2
WA_VAR wa_ldy, 2
WA_VAR wa_lsx, 2
WA_VAR wa_lsy, 2
WA_VAR wa_fbw, 2                ; wa_figure's box: width in cells,
WA_VAR wa_fbc, 2                ;   left column x 2, last row x 2
WA_VAR wa_fbr, 2
WA_VAR wa_frow, 2
WA_VAR wa_fnr, 2                ;   rows left to sweep
WA_VAR wa_fvis, WA_MAXW*2       ;   per column x 2: 1 = in front of its wall
WA_VAR wa_flfn, 2               ;   wa_fline or wa_flinef
WA_VAR wa_fon, 1                ;   1: every joint on the screen
WA_VAR wa_fk, 2                 ; wa_fjl's narrow sums: Klo (fcx fs8 + 256 W),
WA_VAR wa_fe, 2                 ;   eye fv8 + 256 hh + 255
WA_VAR wa_fbuf, WA_CELLS*2      ; a figure's sub-pixels by cell offset (0
                                ; between figures: wa_figure clears its own)
WA_VAR wa_nwr, 2                ; video writes this frame
WA_VAR wa_rcp, (WA_ZMAX-WA_NEAR)*2 ; 0x7FFF0 / z, z = NEAR.. (wa_ptbi)
WA_VAR wa_xk, WA_MXV+1          ; per coordinate: the step from the one
WA_VAR wa_yk, WA_MYV+1          ; before, as a slot x 8 (wa_products)
WA_VAR wa_nk, 2                 ; the last slot used, x 8
WA_VAR wa_sel, 10               ; the cone test's bounds bytes and shifts
                                ; (wa_products)
WA_VAR wa_lord, (WA_MBOX+1)*2  ; the leaves front to back (wa_order)...
WA_VAR wa_rx0, 2                ; ...for a position in [rx0, rx1) x
WA_VAR wa_rx1, 2                ; [ry0, ry1) - the pairs adjacent: wa_order
WA_VAR wa_ry0, 2                ; reaches the high end at +2
WA_VAR wa_ry1, 2
WA_VAR wa_kv, (WA_MAXKS+1)*8    ; per slot: the step x 4...
WA_VAR wa_mk, (WA_MAXKS+1)*8    ; ...and its products, cos and sin, 32 bits
WA_VAR wa_nsht, 1               ; ...and wa_numt's, for the height in
WA_VAR wa_ntht, 2               ; wa_ntht (wa_hnt)
WA_VAR wa_gen, 1                ; heights: every span general
WA_VAR wa_roomh, 2              ; the room's height
WA_VAR wa_nshb, 1               ; the shift wa_numb's product takes (3, or 1
                                ; when it is / 4: wa_render)
WA_VAR wa_feye, 2               ; a figure's eye: wa_eye less its F_Z
WA_VAR wa_fvn, 2                ; wa_fjl's narrow path below this fv8
WA_VAR wa_fhr, 1                ; wa_femit: the row's top half-row
WA_VAR wa_fthlo, 1              ; ...and the lowest wa_fthr between 0 and
                                ; 0xFF (wa_fthrs)
WA_VAR wa_fthr, WA_MAXW*2       ; heights: per column x 2, the first half-row
                                ; of the figure a platform hides (wa_fthrs)
WA_VAR wa_hflat, 1              ; 1: no box below the tallest (HEIGHTS-PLAN
WA_VAR wa_hmax, 2               ; 7.2.5) - and the tallest, the room's too
WA_VAR wa_keye, 2               ; the eye and kyq wa_numt and wa_numb were
WA_VAR wa_kkyq, 2               ; made for (wa_render)
; THE COLUMN STATE OF HEIGHTS (docs/plans/WIREARENA-HEIGHTS-PLAN.md 1.2,
; 7.2.5), per column x 2: the topmost half-row covered so far, and a stack
; of the faces that raised it, front to back - entry k at wa_cst + k x
; 2 x WA_MAXW, a word: its span tag (low byte), the row it covers from
; (high). A column's TALL face is its wa_cov tag, as on a flat arena, which
; has no entries at all. Cleared each frame on an arena that is not flat.
WA_CSTK equ 4
WA_VAR wa_cclip, WA_MAXW*2
WA_VAR wa_cstn, WA_MAXW*2       ; entries in the column
WA_VAR wa_cst, WA_CSTK*WA_MAXW*2
%endmacro

; --- the frame --------------------------------------------------------------
wa_render:
    push ds
    pop es
    inc byte [wa_frame]         ; 1..127: bit 7 of a stamp marks a FIGURE
    jns .stamped                ; cell, so a wall cell needs no mask store
    mov di, wa_stamp            ; the stamp lapped: start them again
    xor ax, ax
    mov cx, WA_CELLS
    rep stosw
    mov di, wa_vst              ; ...and the vertex cache's with them
    mov cx, WA_MVTX
    rep stosw
    mov byte [wa_frame], 1
.stamped:
    mov word [wa_newn], 0
    mov ax, [wa_newp]
    mov [wa_lp], ax
    mov si, wa_attrc
    cmp byte [wa_mono], 0
    je .attr
    mov si, wa_attrm
.attr:
    mov di, wa_at
    movsw
    movsw
    movsw
    movsw
    mov ax, [wa_ang]
    mov cl, 6
    shr ax, cl
    mov [wa_cang], ax
    mov ax, [wa_fsn]            ; its sin and cos, from the frame block (the
    mov [wa_sinv], ax           ; game's wa_sincos: the part has no table)
    mov ax, [wa_fcs]
    mov [wa_cos], ax
    mov ax, [wa_pitch]
    add ax, WA_HR*2
    mov [wa_hq], ax
    add ax, 2
    sar ax, 1
    sar ax, 1
    mov [wa_hh], ax
    mov cx, [wa_kyq]            ; (the field of view's vertical scale)
    mov ax, [wa_eye]            ; THE EYE'S PRODUCTS, made again only when
    cmp ax, [wa_keye]           ; the eye or the field of view moved
    jne .pair                   ; (wa_geominit forgets them with the arena:
    cmp cx, [wa_kkyq]           ; a heights arena's wa_hnt takes wa_numt)
    je .paired
.pair:
    mov [wa_keye], ax
    mov [wa_kkyq], cx
    mov ax, WA_HTOP             ; a flat arena's top: every box and the room
    sub ax, [wa_eye]            ; (3 m less the eye: |90| x 230 at most,
    imul cx                     ; never past 16 bits)
    mov [wa_numt], ax
    mov ax, [wa_eye]
    imul cx
    mov bx, dx                  ; THE BOTTOM, eye x kyq, as a (product,
    cwd                         ; shift) pair (HEIGHTS-PLAN 7.2.3): the
    cmp dx, bx                  ; product itself and >> 19 in all wherever
    je .nfit                    ; it fits - the same bits as ever - or the
    sar bx, 1                   ; product / 4 and >> 17 where it does not,
    rcr ax, 1                   ; as from an eye raised by a jump at 60
    sar bx, 1                   ; degrees (kyq 230 x eye 166 is 38,180),
    rcr ax, 1                   ; which wrapped, and the floor rose over
    mov bl, 1                   ; the walls' tops (the "squash")
    jmp .nst
.nfit:
    mov bl, 3
.nst:
    mov [wa_numb], ax
    mov [wa_nshb], bl
.paired:
    mov word [wa_ftailp], wa_span.ftail ; (wa_fspan's: wa_fspanh sets its own)
    call wa_lookset             ; the detail level's choices, once a frame
    xor al, al
    cmp byte [wa_hflat], 0
    jne .gen
    mov word [wa_ntht], -1      ; (heights: the top per box, wa_hnt, and
    mov di, wa_cclip            ; every span general; the column state,
    mov ax, WA_HR               ; HEIGHTS-PLAN 7.2.5)
    mov cx, [wa_w]
    rep stosw
    mov di, wa_cstn
    xor ax, ax
    mov cx, [wa_w]
    rep stosw
    mov [wl_ffr], al            ; (the outline path's floor pair: anew)
    inc ax
.gen:
    mov [wa_gen], al
    mov ax, 128                 ; (eye at most 128 and hh 61: 128 x 127 +
    cmp word [wa_eye], 128      ; 61 x 256 + 255 is under 32512, so 128 -
    ja .fvd                     ; every frame of today's arenas, no divide)
    cmp word [wa_hh], 61
    jle .fvn
.fvd:
    mov ax, [wa_hh]             ; wa_fjl's narrow sum, eye fv8 + 256 hh +
    mov ah, al                  ; 255 less a joint's height x fv8, must
    xor al, al                  ; stay in 16 bits: for joints 0..111 above
    neg ax                      ; the floor, fv8 at most (32512 - 256 hh) /
    add ax, 32512               ; eye. 128 (every frame of today's arenas)
    jg .fvp                     ; whenever that is more
    xor ax, ax
    jmp .fvn
.fvp:
    xor dx, dx
    div word [wa_eye]
    cmp ax, 128
    jbe .fvn
    mov ax, 128
.fvn:
    mov [wa_fvn], ax
    mov di, wa_colz             ; wall depth: not yet asked (wa_wallz)
    mov ax, 0xFFFF
    mov cx, [wa_w]
    rep stosw
    mov di, wa_cov
    xor ax, ax
    mov [wa_tag], ax
    mov cx, [wa_w]
    mov [wa_left], cx
    rep stosw
    call wa_products
    mov ax, [wa_px]             ; the boxes' order: the same from anywhere in
    cmp ax, [wa_rx0]            ; the cell of the tree's split lines the
    jl .order                   ; player stands in, so it is walked again
    cmp ax, [wa_rx1]            ; only when one is crossed (wa_order)
    jge .order
    mov ax, [wa_py]
    cmp ax, [wa_ry0]
    jl .order
    cmp ax, [wa_ry1]
    jl .ordered
.order:
    call wa_order
.ordered:
    mov si, wa_lord             ; front to back, while any column is open
    cmp byte [wa_hflat], 0
    je .hleaves
.leaves:
    cmp word [wa_left], 0
    je .walked
    lodsw
    or ax, ax
    jz .walked
    push si
    xchg ax, bx
    call wa_leaf
    pop si
    jmp .leaves
.hleaves:                       ; (heights: tall, or wrlow.inc's outline)
    cmp word [wa_left], 0
    je .walked
    lodsw
    or ax, ax
    jz .walked
    push si
    xchg ax, bx
    call wa_leafh
    pop si
    jmp .hleaves
.walked:
    cmp word [wa_left], 0       ; the room, last: it encloses everything
    je .roomless
    mov word [wa_crow], wa_corn
    mov al, [wa_bcol]           ; the room's colour
    xor ah, ah
    mov si, ax
    mov al, [wa_at+si]
    mov [wa_wcol], al
    mov byte [wa_endok], 0      ; (each room corner is some face's START)
    mov al, [wa_look]           ; (ROOMF: the room's floor lines alone)
    and al, LK_ROOMF
    mov [wa_roomf], al
    cmp byte [wa_hflat], 0
    jne .rflat
    mov ax, [wa_roomh]          ; (heights: the room at its own height)
    call wa_hnt
    mov ax, (6 << 8) | 0
    call wa_faceh
    mov ax, (4 << 8) | 6
    call wa_faceh
    mov ax, (2 << 8) | 4
    call wa_faceh
    mov ax, (0 << 8) | 2
    call wa_faceh
    jmp .roomless
.rflat:
    mov ax, (6 << 8) | 0        ; its faces run the other way: c0->c3,
    call wa_face                ; c3->c2, c2->c1, c1->c0 (vertex x 2)
    mov ax, (4 << 8) | 6
    call wa_face
    mov ax, (2 << 8) | 4
    call wa_face
    mov ax, (0 << 8) | 2
    call wa_face
.roomless:
    mov byte [wa_roomf], 0
    call wa_dots
    mov ax, [wa_lp]
    sub ax, [wa_newp]
    mov [wa_newn], ax
.c_pk:                          ; (the labels: tools/watrace.py's stops)
    call wa_pickups             ; (WEAPONS-PLAN 2.4, 2.5: under the figures,
.c_fig:                         ; the rockets over them, the overlay over
    call wa_figures             ; everything but the crosshair)
.c_rk:
    call wa_rockets
.c_ov:
    call wa_overlay
.c_cr:
    call wa_cross
    ret

; wa_geominit - once an arena is loaded (wa_main): the reciprocal table
; wa_ptbi reads in place of a divide, and each axis's coordinate steps for
; wa_products. ~2,900 divides, about a tenth of a second on the XT.
wa_geominit:
    push ds
    pop es
    mov di, wa_rcp              ; 0x7FFF0 / z for every z a point can have:
    mov bx, WA_NEAR             ; exactly what the divide gave
.r:
    mov ax, 0xFFF0
    mov dx, 0x0007
    div bx
    stosw
    inc bx
    cmp bx, WA_ZMAX
    jb .r
    mov word [wa_rx0], 1        ; an empty cell: the first frame orders the
    mov word [wa_rx1], 0        ; boxes (wa_order)
    mov word [wa_keye], 0x8000  ; ...and makes the eye's products
    mov word [wa_nk], 0
    mov si, wa_xv
    mov di, wa_xk
    mov cx, [wa_nxv2]
    call .steps
    mov si, wa_yv
    mov di, wa_yk
    mov cx, [wa_nyv2]
.steps:                         ; SI = values, CX = their count x 2, DI = the
    shr cx, 1                   ; axis's steps: per value after the first,
    dec cx                      ; its step's slot x 8 (from 8), then 0
.s:
    inc di
    mov ax, [si+2]
    sub ax, [si]
    shl ax, 1
    shl ax, 1                   ; the step x 4
    mov bx, 8
.f:
    cmp bx, [wa_nk]
    ja .new
    cmp ax, [wa_kv+bx]
    je .have
    add bx, 8
    jmp .f
.new:
    mov [wa_kv+bx], ax          ; (arenagen refuses more than WA_MAXKS)
    mov [wa_nk], bx
.have:
    mov [di], bl
    inc si
    inc si
    loop .s
    mov byte [di+1], 0
    ret

; WA_AXIS values, the player's coordinate, steps, cos products, sin products,
; %6 = 1: ROUND's (the high words alone, rounded)
%macro WA_AXIS 6
    mov ax, [%1]
    sub ax, [%2]
    shl ax, 1
    shl ax, 1
    mov bx, ax
    imul word [wa_sinv]
    mov bp, ax
    mov cx, dx
    mov ax, bx
    imul word [wa_cos]          ; DX:AX, CX:BP = the first value's products
%if %6
    add ax, 0x8000              ; (ROUND: biased by a half, so every high
    adc dx, 0                   ; word after is its product ROUNDED - the
    add bp, 0x8000              ; steps added are exact - and only the high
    adc cx, 0                   ; words are kept)
%endif
    mov di, %4
    mov si, %3
    xor bx, bx
    jmp %%put
%%next:
    add ax, [wa_mk+bx]          ; ...and each after it, the step's added
    adc dx, [wa_mk+bx+2]
    add bp, [wa_mk+bx+4]
    adc cx, [wa_mk+bx+6]
%%put:
%if %6 == 0
    mov [di], ax
%endif
    mov [di+2], dx
%if %6 == 0
    mov [di+%5-%4], bp
%endif
    mov [di+%5-%4+2], cx
    add di, 4
    inc si
    mov bl, [si]
    or bl, bl
    jnz %%next
%endmacro

; wa_products - per distinct coordinate, (v - p) x 4 cos and (v - p) x 4 sin:
; x 4 so a vertex's camera z and x are the HIGH words of two 32-bit sums,
; with no shifts (wa_vert). All of them, every frame: doing it lazily per
; coordinate was measured SLOWER (19.5 -> 19.0 fps at 40 columns) - nearly
; every coordinate is used, so the stamp tests were pure cost. But not by a
; multiply each: the first value of an axis is multiplied, and each after it
; is the one before plus the products of the STEP between them - an arena
; has few distinct steps (wa_geominit, at most WA_MAXKS), multiplied here
; once a frame. 12 multiplies in the full arena, where there were 52; the
; sums are exact, so every product is the same bits as before
wa_products:
    mov si, 8                   ; the steps: slot x 8, from 8
.k:
    cmp si, [wa_nk]
    ja .kd
    mov ax, [wa_kv+si]          ; the step x 4
    imul word [wa_cos]
    mov [wa_mk+si], ax
    mov [wa_mk+si+2], dx
    mov ax, [wa_kv+si]
    imul word [wa_sinv]
    mov [wa_mk+si+4], ax
    mov [wa_mk+si+6], dx
    add si, 8
    jmp .k
.kd:
    test byte [wa_look], LK_ROUND
    jnz .round
    WA_AXIS wa_xv, wa_px, wa_xk, wa_xc, wa_xs, 0
    WA_AXIS wa_yv, wa_py, wa_yk, wa_yc, wa_ys, 0
    jmp .cone
.round:
    WA_AXIS wa_xv, wa_px, wa_xk, wa_xc, wa_xs, 1
    WA_AXIS wa_yv, wa_py, wa_yk, wa_yc, wa_ys, 1
.cone:
    mov cx, [wa_sinv]           ; the cone test's corners (wa_leaf): which
    mov dx, [wa_cos]            ; end of a box's bounds is the extreme of
    mov ax, (9 << 8) | 10       ; each sum - the sums are linear in the
    cmp cx, dx                  ; coordinate
    jl .p
    mov ax, (11 << 8) | 8
.p:
    mov [wa_sel+2], al          ; right: min x - z over x...
    mov [wa_sel+5], ah          ; left: max x + z over y
    mov ax, (8 << 8) | 9
    mov bx, cx
    add bx, dx
    js .q
    mov ax, (10 << 8) | 11
.q:
    mov [wa_sel+3], al          ; right: ...and max over y
    mov [wa_sel+4], ah          ; left: max over x
    mov ax, (10 << 8) | 8
    or dx, dx
    js .c
    xchg al, ah
.c:
    mov [wa_sel+6], al          ; behind: max z over x...
    mov [wa_sel], ah            ; (and the nearest: min z)
    mov ax, (11 << 8) | 9
    or cx, cx
    js .s
    xchg al, ah
.s:
    mov [wa_sel+7], al          ; ...and over y
    mov [wa_sel+1], ah
    mov bl, [wa_fov]            ; the narrower views' shifts (wa_leaf)
    xor bh, bh
    shl bx, 1
    mov ax, [wa_fush+bx]
    mov [wa_sel+8], ax
    ret
wa_fush:    db 0, 0, 3, 1, 2, 2 ; 90 (unused), 75: 7/32, 60: 13/32

; wa_order - the box tree walked front to back from the player's position
; into wa_lord (leaf nodes, 0 at the end), and the cell of the split lines
; that position is in: [rx0, rx1) x [ry0, ry1). The order is a function of
; the position alone - which side of each split line it is on - so a frame
; whose position is still in the cell (turning, or moving inside it, which
; is most frames of play) reuses the list, and a frame walks no inner node.
; THE PVS (docs/plans/WIREARENA-HEIGHTS-PLAN.md 9.8): an arena may carry, per
; cell of a square grid, the boxes that can draw anything from somewhere in
; it (tools/arenagen.py, pvs_masks); the rest are left off the list, so a
; frame never visits them. The order's cell is then the grid's cell too.
wa_order:
    mov ax, 32767
    mov [wa_rx1], ax
    mov [wa_ry1], ax
    inc ax
    mov [wa_rx0], ax
    mov [wa_ry0], ax
    mov cl, [wa_pvsh]
    or cl, cl
    jz .walk
    mov ax, [wa_px]             ; the grid's cell: [x0, x0 + g) x [y0, y0 + g)
    mov si, wa_rx0
    call .cell
    mov bx, ax
    mov ax, [wa_py]
    mov si, wa_ry0
    call .cell                  ; BX = its column, AX = its row
    xchg ax, bx
    mul byte [wa_pvsn]
    add ax, bx
    cmp byte [wa_pkvh], 0       ; (the cell's pickups' byte, when carried:
    je .nopk                    ; WEAPONS-PLAN 2.4)
    mov dx, ax
    add dx, wa_pkv
    mov [wa_pkvp], dx
.nopk:
%if WA_PVSB > 1
    mov cx, WA_PVSB
    mul cx
%endif
    add ax, wa_pvs
    mov [wa_pvp], ax
.walk:
    mov di, wa_lord
    mov bx, [wa_root]
    call .n
    xor ax, ax
    stosw
    ret
.cell:                          ; AX = a coordinate, SI = its bounds, CL =
    or ax, ax                   ; the shift -> AX = its cell, clamped, and
    jns .c0                     ; the bounds the cell's
    xor ax, ax
.c0:
    sar ax, cl
    mov dl, [wa_pvsn]
    xor dh, dh
    cmp ax, dx
    jb .c1
    mov ax, dx
    dec ax
.c1:
    mov dx, ax
    shl dx, cl
    mov [si], dx
    mov di, 1
    shl di, cl
    add dx, di
    mov [si+2], dx
    ret
.n:
    mov al, [bx]
    cmp al, 2
    jne .split
    cmp byte [wa_pvsh], 0       ; a leaf: onto the list - with a PVS, if
    je .take                    ; its box is in the cell's (box x 8: its
    mov al, [bx+1]              ; byte x 64, its bit x 8)
    mov cl, 6
    shr al, cl
    cbw
    mov si, ax
    add si, [wa_pvp]
    mov cl, [bx+1]
    shr cl, 1
    shr cl, 1
    shr cl, 1
    and cl, 7
    mov al, 1
    shl al, cl
    test [si], al
    jz .out
.take:
    mov ax, bx
    stosw
.out:
    ret
.split:
    mov si, [bx+4]              ; near = low, far = high...
    mov dx, [bx+6]
    mov cx, [bx+2]
    mov bp, wa_rx0
    or al, al
    mov ax, [wa_px]
    jz .axis
    mov bp, wa_ry0
    mov ax, [wa_py]
.axis:
    cmp ax, cx
    jl .low
    xchg si, dx                 ; ...or the other way about
    cmp cx, [ds:bp]             ; (the cell: from max(lo, v)...
    jle .go
    mov [ds:bp], cx
    jmp .go
.low:
    cmp cx, [ds:bp+2]           ; ...to min(hi, v))
    jge .go
    mov [ds:bp+2], cx
.go:
    push dx
    mov bx, si
    call .n
    pop bx
    jmp .n

; wa_leaf - BX = a leaf of the box tree: its box, unless the view cone
; drops it.
wa_lret:    ret                 ; (the cone tests' short way out)
%macro LEAFBODY 1               ; %1 = 1: heights (tall or low)
    ; THE VIEW CONE: a box wholly right of x = z, left of x = -z or behind
    ; the near plane cannot reach the screen (90 degrees, the widest view).
    ; Each is a sum of the products' high words (wa_products) at the box's
    ; extreme corner (wa_sel); the margins cover the four truncated words,
    ; so a box is dropped only when the exact walk would have drawn none of
    ; it. Only BOXES are tested, not the tree's inner nodes: their bounds
    ; are most of the room, and over P's poses (and nearly all of B's) not
    ; one inner test in the full arena dropped anything.
    ; (Storing the per-coordinate sums with the products measured slower:
    ; most coordinates' are never read)
    mov si, wa_sel+2
    xor ah, ah
    lodsb
    xlatb
    mov di, ax
    mov cx, [wa_xs+di+2]        ; x - z = (Xs - Xc) - (Yc + Ys) > 1
    sub cx, [wa_xc+di+2]
    lodsb
    xlatb
    mov di, ax
    sub cx, [wa_yc+di+2]
    sub cx, [wa_ys+di+2]
    cmp cx, 4
    jge wa_lret
    lodsb
    xlatb
    mov di, ax
    mov bp, [wa_xs+di+2]        ; x + z = (Xs + Xc) + (Ys - Yc) < -1
    add bp, [wa_xc+di+2]
    lodsb
    xlatb
    mov di, ax
    add bp, [wa_ys+di+2]
    sub bp, [wa_yc+di+2]
    cmp bp, -5
    jle wa_lret
    lodsb
    xlatb
    mov di, ax
    mov dx, [wa_xc+di+2]        ; z = Xc + Ys < NEAR
    lodsb
    xlatb
    mov di, ax
    add dx, [wa_ys+di+2]
    cmp dx, [wa_lnear]          ; (NEAR - 2; ROUND's NEAR - 3, its vertex
                                ; z rounded too: wa_lookset)
    jle wa_lret
    ; THE NARROWER VIEWS: the screen's edge is x = t z with t = 0.767 at
    ; 75 degrees and 0.578 at 60, so a box is also off the screen when
    ; x - (1 - u) z > 0 everywhere in it, for any u <= 1 - t (or the mirror
    ; on the left). Over the box that is at least the 90-degree sum's least
    ; value plus u times the least z - one more corner, and u by shifts:
    ; 7/32 at 75 degrees, 13/32 at 60 (the shifts round down: smaller
    ; still). Only for a box the 90-degree cone kept. Over P's poses at 75
    ; degrees ~0.9 of 6.4 boxes a frame fewer reach wa_box.
    cmp byte [wa_fov], 0
    je %%in
    mov si, wa_sel
    lodsb
    xlatb
    mov di, ax
    mov dx, [wa_xc+di+2]        ; z = Xc + Ys, least
    lodsb
    xlatb
    mov di, ax
    add dx, [wa_ys+di+2]
    mov al, cl
    mov cl, [wa_sel+8]
    sar dx, cl                  ; z / 8 (z / 4 at 60)...
    mov di, dx
    sar di, 1
    add dx, di                  ; ...+ z / 16 (z / 8)...
    mov cl, [wa_sel+9]
    sar di, cl
    add dx, di                  ; ...+ z / 32
    mov cl, al
    add cx, dx                  ; x - (1 - u) z
    cmp cx, 4
    jge %%out
    sub bp, dx                  ; x + (1 - u) z
    cmp bp, -5
    jle %%out
%%in:
    xor ah, ah
    mov al, [bx+1]              ; box x 8
    mov si, ax
    mov cl, 3
    shr si, cl
    mov al, [wa_bcol+si+1]      ; its colour (the room's is first)
    mov si, ax
    mov al, [wa_at+si]
    mov [wa_wcol], al
    mov al, [bx+1]
    mov si, ax
    add si, wa_boxes
    shr ax, 1
%if %1
    mov di, ax                  ; (DI = box x 4: its record in wa_bxh)
%endif
    add ax, wa_corn+4           ; (the room's row is first)
    mov [wa_crow], ax
%if %1 == 0
    jmp wa_box
%else
    mov ax, [wa_bxh+di]         ; HEIGHTS (HEIGHTS-PLAN 7.3.1): a box at the
    cmp ax, [wa_hmax]           ; arena's tallest, the eye below it, is TALL -
    jb %%low                     ; today's spans at its own height; any other
    cmp ax, [wa_eye]            ; is drawn in OUTLINE (wrlow.inc)
    jle %%low
    call wa_hnt
    jmp wa_boxh
%%low:
    jmp wa_lowbox
%endif
%%out:
    ret

%endmacro
wa_leaf:
    LEAFBODY 0
wa_leafh:
    LEAFBODY 1

; wa_hnt - AX = a height: wa_numt and wa_nsht its (product, shift) pair,
; (h - eye) x kyq - unless they hold it already. Keeps every register but
; AX.
wa_hnt:
    cmp ax, [wa_ntht]
    je .out
    mov [wa_ntht], ax
    push bx
    push dx
    call wa_hnum
    mov [wa_numt], ax
    mov [wa_nsht], bl
    pop dx
    pop bx
.out:
    ret

; wa_hnum - AX = a height: AX = (h - eye) x kyq as a (product, shift)
; pair with BL, the shift after the product's high word (HEIGHTS-PLAN
; 7.2.3): the product and 3 wherever it fits in 16 bits, else / 4 and 1.
; Clobbers DX.
wa_hnum:
    sub ax, [wa_eye]
    imul word [wa_kyq]
    mov bx, dx
    cwd
    cmp dx, bx
    je .fit
    sar bx, 1
    rcr ax, 1
    sar bx, 1
    rcr ax, 1
    mov bl, 1
    ret
.fit:
    mov bl, 3
    ret

; wa_box - SI = a box (x0, y0, x1, y1), [wa_crow] its corners: the faces
; the player is outside of - at most two, out of wa_boxf
%macro BOXBODY 1                ; %1 = the face routine
    xor bx, bx                  ; BX = the faces we see, x 8: S 8, E 16,
    mov ax, [wa_py]             ; N 32, W 64
    cmp ax, [si+2]
    jge .s1
    mov bl, 8
    jmp .ew
.s1:
    cmp ax, [si+6]
    jle .ew
    mov bl, 32
.ew:
    mov ax, [wa_px]
    cmp ax, [si+4]
    jle .w1
    or bl, 16
    jmp .go
.w1:
    cmp ax, [si]
    jge .go
    or bl, 64
.go:
    add bx, wa_boxf
    push bx
    call .face
    pop bx
    add bx, 4
.face:                          ; BX = a slot: from, to (corners), endok
    mov al, [bx+2]              ; (0xFF: no face)
    or al, al
    js .out
    mov [wa_endok], al
    mov ax, [bx]
    mov bx, [wa_crow]           ; corner numbers -> vertex x 2
    xlatb
    xchg al, ah
    xlatb
    xchg al, ah
    jmp %1
.out:
    ret

%endmacro
wa_box:
    BOXBODY wa_face
wa_boxh:
    BOXBODY wa_faceh

; wa_boxf - per set of faces seen (S 1, E 2, N 4, W 8), two slots, in the
; order S E N W: the face's corners and whether its END corner is drawn -
; only when the next face round is NOT seen, since a corner two faces share
; is the second one's START, and drawing both put `││` there
wa_boxf:
%assign wbf_m 0
%rep 16
 %assign wbf_n 0
 %assign wbf_f 0
 %rep 4
  %if ((wbf_m >> wbf_f) & 1) && wbf_n < 2
   %assign wbf_g (wbf_f + 1) & 3
    db wbf_f, wbf_g, ((wbf_m >> wbf_g) & 1) ^ 1, 0
   %assign wbf_n wbf_n + 1
  %endif
  %assign wbf_f wbf_f + 1
 %endrep
 %rep 2 - wbf_n
    db 0, 0, 0xFF, 0
 %endrep
 %assign wbf_m wbf_m + 1
%endrep

; wa_face - AL = from vertex x 2, AH = to vertex x 2 (a face we see the front
; of): transform, and unless every column it could reach is already closed,
; project (clipping only when a vertex is outside the view) and step it.
; A vertex's column is made with its transform (wa_vert); its top, bottom
; and inverse depth only here, past the occlusion test, once a frame - a
; face wholly behind nearer walls never pays for them.
%macro FACEBODY 1               ; %1 = 1: heights' tall faces, their own height
    mov [wa_fv], ax
    xor bh, bh
    mov bl, al
    mov al, [wa_frame]          ; (the cache's stamp test here, not behind
    cmp [wa_vst+bx], al         ; a call: a third of the vertices a face
    je .v1                      ; names were made by an earlier face)
    call [wa_vertp]
.v1:
    mov bl, [wa_fv+1]           ; (BH = 0: wa_vert keeps it)
    mov al, [wa_frame]
    cmp [wa_vst+bx], al
    je .v2
    call [wa_vertp]
.v2:
    mov dx, [wa_vpc+bx]         ; THE PRE-CHECK: the columns it could reach,
    mov bl, [wa_fv]             ; x 16 (a vertex in view is its own, one
    mov si, bx                  ; beside the view is beyond that edge, one
    mov ax, [wa_vpc+bx]         ; behind is anywhere - wa_vert's sentinels)
    xor bp, bp                  ; (BP = 1: the ends the other way about -
    cmp ax, dx                  ; with both in the view, no columns at all)
    jle .lohi
    xchg ax, dx
    inc bp
.lohi:
    cmp ax, 0x8000              ; (behind: anywhere)
    je .open
    mov cl, 4                   ; the first column whose centre is at or past
    mov ch, al                  ; the near end, (c + 7) >> 4, and the last
    sar ax, cl                  ; short of the far end, ((c + 7) >> 4) - 1:
    and ch, 15                  ; EXACTLY wa_span's columns, so a face seen
    cmp ch, 9                   ; edge-on, between two centres, ends here -
    sbb ax, -1                  ; not after its tops, bottoms and inverse
    mov ch, dl                  ; depths (two a frame on the play bench).
    sar dx, cl                  ; (+7 by the remainder's carry rather than
    and ch, 15                  ; an add: a sentinel, +-32767, would
    cmp ch, 9                   ; overflow)
    sbb dx, 0                   ; (the far end: one less)
    cmp ax, dx
    jg .shut
    or dx, dx                   ; wholly left of the screen, or right: off
    js .shut                    ; it, whatever is open
    cmp ax, [wa_w]
    jge .shut
    shl ax, 1                   ; x 2 from here: BX, DX = the face's own
    shl dx, 1                   ; ends, before the screen clamps them (its
    mov bx, ax                  ; corners'; wa_cs0 and wa_ce0 when both
    or ax, ax                   ; ends are in the view)
    jns .lo
    xor ax, ax
.lo:
    mov cx, [wa_w2]
    dec cx
    dec cx
    cmp dx, cx
    jg .hi
    mov cx, dx
.hi:
    mov di, cx                  ; (the passes' end, one past the last: as
    inc di                      ; wa_span makes it)
    inc di
    mov [wa_end], di
    sub cx, ax
    shr cx, 1
    inc cx
    mov di, ax
    add di, wa_cov
    xor ax, ax
    repne scasw
    je .open
.shut:
    ret                         ; every one closed: nothing more
.open:                          ; SI = v1 x 2
    mov [wa_cs0], bx
    mov [wa_ce0], dx
    mov bl, [wa_fv+1]           ; BX = v2 x 2
    xor bh, bh
    mov al, [wa_vok+si]
    mov [wa_e1], al
    mov ah, [wa_vok+bx]
    and ah, [wa_endok]          ; (the end corner, only on a silhouette)
    mov [wa_e2], ah
    and al, [wa_vok+bx]
    jz .clip
    or bp, bp                   ; BOTH INSIDE THE VIEW: the cache's own
    jnz .shut                   ; values, and the pre-check's columns
    lea ax, [di-2-wa_cov]       ; (wa_fspan) - c1 > c2 has none
    mov [wa_cs], ax
    mov di, bx                  ; DI = v2 x 2
    test byte [wa_vok+si], 2    ; their tops, bottoms and inverse depths
    jnz .h1                     ; made the first time a face of them gets
    call .mk                    ; this far in a frame
.h1:
    test byte [wa_vok+di], 2
    jnz .h2
    xchg si, di
    call .mk
    xchg si, di
.h2:
%if %1
    jmp wa_fspanh
%else
    jmp wa_fspan
%endif
.clip:
    mov ax, [wa_vx+si]
    mov [wa_x1], ax
    mov ax, [wa_vz+si]
    mov [wa_z1], ax
    mov ax, [wa_vx+bx]
    mov [wa_x2], ax
    mov ax, [wa_vz+bx]
    mov [wa_z2], ax
    push bx
    push si
    call wa_clip                ; (it never moves an end inside the view)
    pop si
    jc .gone
    mov di, wa_c1
    mov bx, wa_x1
    call .end
    pop si
    mov bx, wa_x2
    call .end
    jmp wa_span
.gone:
    pop bx
    ret
.end:                           ; SI = a vertex, BX = its clipped (x, z),
    test byte [wa_vok+si], 1    ; DI = wa_c1 or wa_c2: projected
    jnz .put
    mov ax, [bx]
    mov bx, [bx+2]
%if %1
    call wa_projh
%else
    call wa_proj
%endif
    stosw
    xchg ax, bx
    stosw
    xchg ax, cx
    stosw
    xchg ax, dx
    stosw
    ret
.put:                           ; SI = a vertex in view, DI = wa_c1 or wa_c2:
    test byte [wa_vok+si], 2    ; its column, top, bottom, inverse depth -
    jnz .have                   ; the last three made the first time a face
    call .mk                    ; of it gets this far in a frame (.mk)
.have:
    mov ax, [wa_vpc+si]
    stosw
    mov ax, [wa_vpt+si]
    stosw
    mov ax, [wa_vpb+si]
    stosw
    mov ax, [wa_vpi+si]
    stosw
    ret
.mk:                            ; SI = a vertex in view: its top, bottom and
    or byte [wa_vok+si], 2      ; inverse depth, as .put makes them. Clobbers
    mov bx, [wa_vz+si]          ; AX, BX, CX, DX
    cmp bx, WA_ZMAX
    jae .mzdiv
    shl bx, 1
    mov bx, [wa_rcp+bx-WA_NEAR*2]
.miz:
    mov [wa_vpi+si], bx
    mov ax, [wa_numt]
    imul bx
%if %1
    mov cl, [wa_nsht]
    sar dx, cl
%else
    sar dx, 1
    sar dx, 1
    sar dx, 1
%endif
    mov ax, [wa_hq]
    sub ax, dx
    mov [wa_vpt+si], ax
    mov ax, [wa_numb]
    imul bx
    mov cl, [wa_nshb]
    sar dx, cl
    add dx, [wa_hq]
    mov [wa_vpb+si], dx
    ret
.mzdiv:
    mov ax, 0xFFF0
    mov dx, 0x0007
    div bx
    mov bx, ax
    jmp .miz

%endmacro
wa_face:
    FACEBODY 0
wa_faceh:                       ; (heights: a tall face at [wa_ntht])
    FACEBODY 1

; wa_vert - BX = a vertex x 2 (BH = 0) not yet made this frame, AL = the
; frame: into camera space and, inside the view (the guard band), its
; column. [wa_vok]: 0 outside, 1 the column made, 3 the rest too (wa_face).
; Outside, [wa_vpc] is the pre-check's sentinel instead: 0x8000 behind,
; -32767 beside the view on the left, 32767 on the right. BX is kept;
; clobbers AX, CX, DX, SI, DI.
wa_vert:
    mov [wa_vst+bx], al
    mov di, bx                  ; DI = vertex x 2
    mov ax, [wa_vtx+bx]         ; AL = x index x 4, AH = y index x 4
    mov bl, ah
    xor ah, ah
    mov si, ax
    mov ax, [wa_xc+si]          ; z = ((x-px) 4 cos + (y-py) 4 sin) >> 16
    add ax, [wa_ys+bx]
    mov cx, [wa_xc+si+2]
    adc cx, [wa_ys+bx+2]
    mov ax, [wa_xs+si]          ; x = ((x-px) 4 sin - (y-py) 4 cos) >> 16
    sub ax, [wa_yc+bx]
    mov ax, [wa_xs+si+2]
    sbb ax, [wa_yc+bx+2]
.cam:
    mov bx, di
    mov [wa_vz+bx], cx
    mov [wa_vx+bx], ax
    cmp cx, WA_NEAR             ; projectable: z >= NEAR, |x| <= 8z (the
    jl .behind                  ; guard band - the screen is |x| <= z)
    mov dx, cx
    shl dx, 1
    shl dx, 1
    shl dx, 1
    cmp ax, dx
    jg .right
    neg dx
    cmp ax, dx
    jl .left
    mov byte [wa_vok+bx], 1
    imul word [wa_k16]          ; the column, x16 (wa_proj)
    idiv cx
    add ax, [wa_c16]
    mov [wa_vpc+bx], ax
    ret
.behind:
    mov ax, 0x8000
    jmp .out
.left:
    mov ax, -32767
    jmp .out
.right:
    mov ax, 32767
.out:
    mov byte [wa_vok+bx], 0
    mov [wa_vpc+bx], ax
    ret

; wa_vertr - wa_vert at a level with ROUND: the products' rounded high
; words, a sum each (wa_products), then wa_vert's own way
wa_vertr:
    mov [wa_vst+bx], al
    mov di, bx
    mov ax, [wa_vtx+bx]
    mov bl, ah
    xor ah, ah
    mov si, ax
    mov cx, [wa_xc+si+2]
    add cx, [wa_ys+bx+2]
    mov ax, [wa_xs+si+2]
    sub ax, [wa_yc+bx+2]
    jmp wa_vert.cam

; wa_lookset - once a frame: the detail level's choices (wa_look) that are
; made once rather than tested where they act - which wa_vert, the cone
; test's near margin, a low box's far-top allowance (wrlow.inc)
wa_lookset:
    mov ax, wa_vert
    mov cx, WA_NEAR-2
    test byte [wa_look], LK_ROUND
    jz .r
    mov ax, wa_vertr
    dec cx
.r:
    mov [wa_vertp], ax
    mov [wa_lnear], cx
    call wa_collook             ; (the columns': wa_flat's level tests)
    xor ax, ax
    test byte [wa_look], LK_FARTOP
    jz .f
    mov al, WL_FARTOPV
.f:
    mov [wl_fartop], ax
    ret

; wa_xform - AX, BX = a world point -> AX = camera x (right), DX = z
; (ahead). Clobbers BX, CX, SI, DI.
wa_xform:
    sub ax, [wa_px]
    sub bx, [wa_py]
    push ax
    push bx
    imul word [wa_cos]          ; z = (dx cos + dy sin) >> 14
    mov cx, ax
    mov si, dx
    mov ax, bx
    imul word [wa_sinv]
    add ax, cx
    adc dx, si
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov di, dx
    pop bx
    pop ax
    imul word [wa_sinv]         ; x = (dx sin - dy cos) >> 14
    mov cx, ax
    mov si, dx
    mov ax, bx
    imul word [wa_cos]
    sub cx, ax
    sbb si, dx
    shl cx, 1
    rcl si, 1
    shl cx, 1
    rcl si, 1
    mov ax, si
    mov dx, di
    ret

; wa_clip - the segment (x1,z1)-(x2,z2) against z >= NEAR and the guard
; band x >= -8z, x <= 8z. CF = 1: nothing left. An end inside all three is
; never moved. A cut at the near plane sets z to NEAR (the interpolation
; gave exactly that) and interpolates x alone. Clobbers AX, BX, CX, DX, SI,
; DI, BP.
wa_clip:
    mov ax, [wa_z1]
    sub ax, WA_NEAR
    mov dx, [wa_z2]
    sub dx, WA_NEAR
    call .order
    jc .rej
    jz .left
    xor bx, bx
    call .lerp
    add [si], ax
    mov word [si+2], WA_NEAR
.left:                          ; the GUARD BAND, |x| <= 8z, not the view
    mov cl, 3                   ; (|x| <= z): a vertex beside the view projects
    mov ax, [wa_z1]             ; to a column off the screen and the span
    shl ax, cl                  ; clamps it, so only a vertex far outside -
    add ax, [wa_x1]             ; whose column would overflow - is clipped
    mov dx, [wa_z2]
    shl dx, cl
    add dx, [wa_x2]
    call .order
    jc .rej
    jz .right
    call .side
.right:
    mov cl, 3
    mov ax, [wa_z1]
    shl ax, cl
    sub ax, [wa_x1]
    mov dx, [wa_z2]
    shl dx, cl
    sub dx, [wa_x2]
    call .order
    jc .rej
    jz .ok
    call .side
.ok:
    clc
.rej:
    ret
.order:                         ; AX = d1, DX = d2 -> CF: both outside; else
    or ax, ax                   ; ZF: neither; else SI = the end outside,
    js .o1                      ; DI = the one inside, BP = -d(out),
    or dx, dx                   ; CX = d(in) - d(out)
    jns .none
    mov si, wa_x2
    mov di, wa_x1
    neg dx
    mov bp, dx
    add ax, dx
    mov cx, ax
    or si, si                   ; (NZ, NC)
    ret
.o1:
    or dx, dx
    js .both
    mov si, wa_x1
    mov di, wa_x2
    neg ax
    mov bp, ax
    add dx, ax
    mov cx, dx
    or si, si
    ret
.both:
    stc
    ret
.none:
    xor si, si                  ; (ZF, NC)
    ret
.side:                          ; a cut at a side plane: x and z
    xor bx, bx
    call .lerp
    push ax
    mov bx, 2
    call .lerp
    add [si+2], ax
    pop ax
    add [si], ax
    ret
.lerp:                          ; BX = 0 (x) or 2 (z) -> AX = the change:
    mov ax, [di+bx]             ; (in - out) d(out) / (d(out) - d(in))
    sub ax, [si+bx]
    imul bp
    idiv cx
    ret

; wa_proj - AX = x, BX = z (>= NEAR, |x| <= 8z) -> AX = column x16,
; BX = top and CX = bottom (quarter half-rows), DX = inverse depth
wa_projh:                       ; (heights: wa_ptbih's top)
    imul word [wa_k16]
    idiv bx
    add ax, [wa_c16]
    push ax
    call wa_ptbih
    pop ax
    ret
wa_proj:
    imul word [wa_k16]          ; the column, x16: NOT clamped - a vertex in
    idiv bx                     ; the guard band lands off the screen and
    add ax, [wa_c16]            ; the span clamps to it. (The CENTRE, 8W -
    push ax                     ; not the scale, 8Wf: at 90 degrees they are
    call wa_ptbi                ; one number, and using the scale for both put
    pop ax                      ; every wall six columns right at 75)
    ret

; wa_ptbi - BX = z (>= NEAR) -> BX = top, CX = bottom (quarter half-rows),
; DX = inverse depth, 0x7FFF0 / z (32767 at the near plane) - out of
; wa_rcp, the same bits the divide gave. Clobbers AX
wa_ptbi:
    cmp bx, WA_ZMAX
    jae .div
    shl bx, 1
    mov bx, [wa_rcp+bx-WA_NEAR*2]
.iz:
    mov ax, [wa_numt]           ; top = hq - numt / z = hq - (numt x iz) >> 19:
    imul bx                     ; a multiply where a divide was
    sar dx, 1
    sar dx, 1
    sar dx, 1
    jmp wa_ptbib
.div:                           ; (deeper than the room: never, but exact)
    mov ax, 0xFFF0
    mov dx, 0x0007
    div bx
    mov bx, ax
    jmp .iz
wa_ptbih:                       ; (heights: the top at [wa_ntht]'s shift -
    cmp bx, WA_ZMAX             ; and past the room's diagonal, never, the
    jae wa_ptbi                 ; flat arena's)
    shl bx, 1
    mov bx, [wa_rcp+bx-WA_NEAR*2]
    mov ax, [wa_numt]
    imul bx
    mov cl, [wa_nsht]
    sar dx, cl
wa_ptbib:
    neg dx
    add dx, [wa_hq]
    xchg bx, dx                 ; BX = top, DX = iz
    mov ax, [wa_numb]
    xchg cx, dx                 ; CX = iz
    imul cx
    xchg cx, dx                 ; DX = iz, CX = the product's high word
    xchg ax, cx
    mov cl, [wa_nshb]           ; (3, or 1 for a product / 4)
    sar ax, cl
    add ax, [wa_hq]
    xchg cx, ax                 ; CX = bottom
    ret

; --- spans --------------------------------------------------------------
; The hot loop of a frame. Plotting is INLINE (WPLOT): DL = the frame's
; stamp and DI = the list's write pointer stay in registers for a whole
; span.
%macro WPLOT 0                  ; BX = cell, AX = char|attr; first one wins
    cmp [wa_stamp+bx], dl
    je %%skip
    mov [wa_stamp+bx], dl
    mov [wa_cur+bx], ax
    xchg ax, bx                 ; onto the list (ES = DS through wa_render)
    stosw
    xchg ax, bx
%%skip:
%endmacro

; wa_span - the projected segment (c1,t1,b1,i1)-(c2,t2,b2,i2): stepped
; across its columns, drawn into the open ones, which it then closes.
wa_span:
    mov bx, [wa_c1]             ; first column whose centre (16c+8) >= c1
    mov cl, 4
    add bx, 7
    sar bx, cl
    mov si, [wa_c2]             ; last whose centre < c2
    add si, 7
    sar si, cl
    dec si
    shl bx, 1                   ; (x 2 from here)
    shl si, 1
    mov [wa_cs0], bx            ; (the segment's own ends, for its corners,
    mov [wa_ce0], si            ; before the screen clamps them)
    or bx, bx                   ; ...and the screen
    jns .lok
    xor bx, bx
.lok:
    cmp si, [wa_w2]
    jl .rok
    mov si, [wa_w2]
    dec si
    dec si
.rok:
    mov cx, si                  ; ANY OPEN COLUMN? One scan of wa_cov, before
    sub cx, bx                  ; a single divide: a wall wholly behind
    jl .none                    ; nearer ones costs this and nothing more
    lea ax, [si+2]              ; (and c1 >= c2 has no columns at all)
    mov [wa_end], ax            ; (the flat passes' end: one past the last)
    shr cx, 1
    inc cx
    lea di, [bx+wa_cov]         ; (ES = DS through wa_render)
    xor ax, ax
    repne scasw
    je .open1
.none:
    ret
.open1:
    lea dx, [di-2-wa_cov]       ; DX = the first open column x 2
    mov [wa_cs], dx
    cmp byte [wa_gen], 0        ; (heights: the clip's passes where a low
    je .flat                    ; box has raised one in the range)
    call wa_fclpc
.flat:
    call .reg                   ; (a steep edge's way out takes the tag
    jc .none                    ; back; AX = c1, DX = c2 - c1)
    cmp dx, 16                  ; a segment inside one column: no step (the
    ja .dcok                    ; edges are their first ends' - see .edge)
    mov bx, [wa_t1]
    mov [wa_t2], bx
    mov bx, [wa_b1]
    mov [wa_b2], bx
.dcok:
    mov cx, [wa_b1]
    mov bp, [wa_b2]
    mov si, wa_t1 - wa_vpt      ; (the top's ends, as wa_flat reads them)
    mov di, wa_t2 - wa_vpt
    jmp wa_flat
.steepm:                        ; wa_flat's steep way out, a clipped span
    sub word [wa_tag], 2
    jmp .gen1

; .ftail - a flat span's passes and corners, its edges made (wa_flat's
; general way): the plain passes, or on heights the clip-tested ones where a
; low box has raised a column's clip in the range (wa_fclp, wa_ptab). BX =
; the tag; SI.BP, CX, DI the top's (wa_fedge's).
.ftlev:                         ; the top level: on the screen?
    cmp word [wa_fh], WA_HR
    jb .p1
    push word [wa_dt]           ; OFF it (a near wall's, or the sparse
    mov bp, [wa_db]             ; level's): ONE pass, the bottom's, opens
    mov [wa_dt], bp             ; the columns
    mov bp, [wa_vb0]
    mov [wa_fh], bp
    mov bp, [wa_qb]
    mov cx, [wa_fb]
    call [wa_ptab+bx]
    pop word [wa_dt]
    mov byte [wa_cov+si+1], 0
    jmp .bp
.ftail:
    mov [wa_vt0], si            ; (for the corners)
    mov [wa_qt], bp
    mov [wa_ft], cx
    mov [wa_dt], di
    mov [wa_fh], si
    cmp byte [wa_sparse], 0     ; the sparse level draws no top
    je .full1
    mov word [wa_fh], -1
    xor cx, cx
.full1:
    mov dh, bl                  ; DH = the tag
    mov dl, [wa_frame]
    mov di, [wa_lp]
    mov ah, [wa_wcol]           ; the wall's colour: its ITEM's, not its depth
    mov word [wa_ncl], 0
    mov si, [wa_end]            ; the column past the end reads shut to pass
    mov byte [wa_cov+si+1], 1   ; 1 (a high byte: always 0 otherwise)
    mov si, [wa_cs]
    mov bx, [wa_fclp]           ; (which passes: 0, or 4 the clip's)
    jcxz .ftlev
.p1:
    call [wa_ptab+bx]           ; pass 1: open, and the top (SI = the end)
    mov byte [wa_cov+si+1], 0
    mov bx, [wa_vb0]            ; pass 2: the bottom - unless it is off the
    mov [wa_fh], bx             ; screen and stays there
    mov bp, [wa_qb]
    mov cx, [wa_fb]
    jcxz .bz
.b2:
    mov si, [wa_cs]
    mov bx, [wa_p2i]            ; (the plain pass, or the testing one)
    call [wa_ptab+bx+2]
    jmp .bp
.bz:
    cmp bx, WA_HR
    jb .b2
.bp:
    mov ax, [wa_end]            ; opened: the range less the shut ones
    sub ax, [wa_cs]
    shr ax, 1
    sub ax, [wa_ncl]
    sub [wa_left], ax
    test byte [wa_look], LK_NOPOST ; (NOPOST: a wall shorter than
    jz .fnp                     ; WA_POSTMIN half-rows at its first column -
    mov ax, [wa_vb0]            ; a far one - has no corner posts)
    sub ax, [wa_vt0]
    cmp ax, WA_POSTMIN
    jl .fdone
.fnp:
    mov si, [wa_cs]             ; the corners, at the segment's own ends,
    cmp si, [wa_cs0]            ; when this span opened them: a `│` each
    jne .fc2                    ; (plain: nothing else is in either column,
    cmp byte [wa_e1], 0         ; no stamp tests; the clip's: a low box's
    je .fc2                     ; cells can be, and the post ends at the
    mov bx, [wa_vt0]            ; clip - wa_vedge)
    mov ax, [wa_vb0]
    mov cl, [wa_fclp]
    call wa_corner
.fc2:
    cmp byte [wa_e2], 0
    je .fdone
    mov si, [wa_ce0]
    cmp si, [wa_end]            ; (on the screen)
    jge .fdone
    cmp [wa_cov+si], dh         ; (and opened here)
    jne .fdone
    sub si, [wa_cs]             ; the last column's edges: the top's from
    shr si, 1                   ; its start (a carry a half-row; in a pass's
    xchg ax, si                 ; form, either way), the bottom's a step
    mul word [wa_ft]            ; back from one column past the end (a
    add ax, [wa_qt]             ; borrow)
    adc dx, 0
    mov bx, [wa_vt0]
    cmp word [wa_dt], 0
    jg .fft
    neg dx
.fft:
    add bx, dx
    mov ax, [wa_fh]
    cmp bp, [wa_fb]
    jnc .ffb
    sub ax, [wa_db]
.ffb:
    mov si, [wa_ce0]
    mov dl, [wa_frame]          ; (the multiply took DX)
    mov cl, [wa_fclp]
    call wa_corner
.fdone:
    mov [wa_lp], di
    ret

; .reg - register the span: wa_cov will hold its TAG (x 2, from 2: a byte,
; the word's high byte staying 0) in every column it opens, and the table
; its ends - inverse depth is linear in the column, and wa_wallz
; interpolates it for the few columns asked about. BX = the tag, AX = c1,
; DX = c2 - c1; CF = 1: more spans than the table holds, drawing stops this
; frame (never seen). Clobbers CX.
.reg:
    mov bx, [wa_tag]
    add bx, 2
    cmp bx, WA_MAXSP*2
    jae .full
    mov [wa_tag], bx
    mov ax, [wa_c1]
    mov [wa_spcs+bx], ax
    mov dx, [wa_c2]
    sub dx, ax
    mov [wa_spsh+bx], dx
    mov cx, [wa_i1]
    mov [wa_spih+bx], cx
    neg cx
    add cx, [wa_i2]
    mov [wa_spsl+bx], cx
    clc
    ret
.full:
    mov word [wa_left], 0
    stc
    ret

.gen1:                          ; SPANLOOP's: the last open column too
    mov si, [wa_end]
    dec si
    dec si
    mov dx, [wa_cs]
    mov cx, si
    sub cx, dx
    shr cx, 1
    inc cx
    lea di, [si+wa_cov]
    xor ax, ax
    std
    repne scasw                 ; ...from the right, the last open column
    cld
    lea si, [di+4-wa_cov]       ; one past it, x 2
    mov [wa_end], si
    ; THE EDGES' START AND STEP, EXACT (docs/plans/MULTIPLAYER-FPS-PLAN.md
    ; 16.5). The start used to be STEPPED from the segment's own first
    ; column - for a wall clipped at the guard band, ~150 columns off the
    ; screen - with a step truncated to a whole 13.3 unit: up to 0.99 of a
    ; unit lost a column, two and more half-rows by the first visible one.
    ; That was the far wall the owner saw drawn in front of a nearer pillar.
    ; Now the first open column's value is interpolated exactly, and the
    ; step is in HALF-ROWS with a 16-bit fraction (the loop keeps the edge as
    ; whole.fraction, the whole part being the ROUNDED half-row itself: no
    ; column shifts or rounds). See .edge.
    mov byte [wa_steep], 0
    mov ax, [wa_t1]
    mov bx, [wa_t2]
    call .edge
    mov [wa_st], dx
    mov [wa_stl], ax
    mov [wa_vt0], bx            ; (the first column's, for its corner)
    push cx
    mov ax, [wa_b1]
    mov bx, [wa_b2]
    call .edge
    mov [wa_sb], dx
    mov [wa_sbl], ax
    mov [wa_vb], bx             ; the bottom: half-row and fraction
    mov [wa_vbl], cx
    mov [wa_vb0], bx
    pop bp                      ; the top: CX.BP in the loop
    push cx
    call .reg
    pop cx
    jc .none
    mov dh, bl                  ; DH = the tag
    xor ax, ax                  ; AN EDGE MOVING OFF THE SCREEN (or level),
    cmp [wa_sparse], al         ; once off it, is off it for the rest of
    jne .fl                     ; the span - the loops drop it there (wa_tup:
    mov bx, [wa_st]             ; the top up; wa_bdn: the bottom down) - but
    or bx, bx                   ; for the sparse level, whose posts want the
    js .tu                      ; true top
    or bx, [wa_stl]
    jnz .tn
.tu:
    inc ax
.tn:
    cmp word [wa_sb], 0
    jl .fl
    inc ah
.fl:
    mov [wa_tup], ax
    mov cx, [wa_vt0]
    mov word [wa_ncl], 0
    mov si, [wa_cs]
    mov di, [wa_lp]
    mov dl, [wa_frame]
.hd:                            ; THE HEAD: columns the wall leaves EMPTY -
    or cx, cx                   ; its top above the screen and its bottom
    jns .hgo                    ; under it (a near wall running off the
    cmp word [wa_vb], WA_HR     ; view): opened and stepped, no more
    jl .hgo
    cmp word [wa_cov+si], 0
    jne .hsh
    mov [wa_cov+si], dh
.hst:
    add bp, [wa_stl]
    adc cx, [wa_st]
    mov ax, [wa_sbl]
    add [wa_vbl], ax
    mov ax, [wa_sb]
    adc [wa_vb], ax
    inc si
    inc si
    cmp si, [wa_end]
    jb .hd
    jmp .vd                     ; (all of it)
.hsh:
    inc word [wa_ncl]
    jmp .hst
.hgo:
    mov word [wa_pht], -1       ; (the column before's, when it was one of
    mov word [wa_phb], WA_HR    ; those: off the screen)
    mov ah, [wa_wcol]           ; the wall's colour: its ITEM's, not its depth
    mov bx, wa_ls11             ; which loop: the top drawn, or not -
    cmp byte [wa_roomf], 0      ; (ROOMF: the room's steep span sparse, and
    je .vr                      ; no posts)
    mov byte [wa_e1], 0
    mov byte [wa_e2], 0
    jmp .vs
.vr:
    cmp byte [wa_sparse], 0
    jne .vs
    cmp word [wa_t1], -3        ; (a top above the screen at both ends is
    jg .v1                      ; above it in every column: the sparse
    cmp word [wa_t2], -3        ; loop, which draws no top and no top
    jg .v1                      ; fills, draws the same cells - as
.vs:                            ; wa_fedge's test)
    mov bx, wa_ls01
.v1:
    call bx
.vd:
    mov [wa_lp], di
    mov ax, [wa_end]            ; opened: the range less the shut ones
    sub ax, [wa_cs]
    shr ax, 1
    sub ax, [wa_ncl]
    sub [wa_left], ax
    test byte [wa_look], LK_NOPOST
    jz .np
    mov ax, [wa_vb0]
    sub ax, [wa_vt0]
    cmp ax, WA_POSTMIN
    jl .cdone
.np:
    mov si, [wa_cs]             ; the corners, at the segment's own ends,
    cmp si, [wa_cs0]            ; when this span opened them: a `│` each
    jne .c2
    cmp byte [wa_e1], 0
    je .c2
    mov bx, [wa_vt0]
    mov ax, [wa_vb0]
    push cx
    mov cl, [wa_gen]            ; (nothing else is in this column yet - but
    call wa_corner              ; a low box can be)
    pop cx
.c2:
    mov si, [wa_end]
    dec si
    dec si
    cmp si, [wa_ce0]
    jne .cdone
    cmp byte [wa_e2], 0
    je .cdone
    sub bp, [wa_stl]            ; the last column's edges: the accumulators
    sbb cx, [wa_st]             ; are one column past it
    mov bx, cx
    mov ax, [wa_vbl]
    sub ax, [wa_sbl]
    mov ax, [wa_vb]
    sbb ax, [wa_sb]
    mov cl, [wa_gen]            ; (a steep step's fill can be in this column
    or cl, [wa_steep]           ; too, and wins)
    call wa_corner
.cdone:
    mov [wa_lp], di
    ret

; .edge - AX = t1, BX = t2 (an edge's ends, quarter half-rows) -> DX:AX =
; its step a column, BX.CX = its value at the first open column: half-rows,
; 16.16, the whole part ROUNDED (half a row added). Marks wa_steep when the
; step is a half-row or more. Clobbers SI, DI, BP.
; The step is change x 4 x 65536 / dc: ONE divide when it is under a
; half-row (the usual far wall), two otherwise. The start is t1 + the step x
; off / 16 - the step's 16-bit fraction makes that exact to a few
; thousandths of a half-row even ~300 columns from the segment's start (a
; guard-band clip), and it is one MULTIPLY (two on a steep edge) where an
; IMUL and an IDIV were.
.edge:
    mov di, ax                  ; DI = t1
    sub bx, ax
    xchg ax, bx
    cwd
    mov bp, dx                  ; BP = the change's sign (0 or -1)
    xor ax, dx
    sub ax, dx                  ; |change|
    xor si, si                  ; SI = the step's whole part
    mov cx, [wa_dc]
    cmp cx, 16                  ; whenever the SEGMENT spans more than a
    jbe .e0                     ; column (not the trimmed count)
    xor dx, dx
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1                   ; DX:AX = |change| x 4
    or dx, dx
    jnz .ew
    cmp ax, cx
    jb .ef
.ew:
    div cx
    mov si, ax
    mov byte [wa_steep], 1
    xor ax, ax
    jmp .ed
.ef:
    xchg ax, dx                 ; (under a half-row: the fraction alone)
.ed:
    div cx
    mov bx, ax                  ; SI.BX = |step|
    mul word [wa_fs]            ; x off: DX:AX (a steep step: and above)
    or si, si
    jz .e3
    push di
    mov cx, ax
    mov di, dx
    mov ax, si
    mul word [wa_fs]
    add di, ax
    adc dx, 0
    shr dx, 1                   ; / 16
    rcr di, 1
    rcr cx, 1
    shr dx, 1
    rcr di, 1
    rcr cx, 1
    shr dx, 1
    rcr di, 1
    rcr cx, 1
    shr dx, 1
    rcr di, 1
    rcr cx, 1
    xchg ax, cx
    mov dx, di
    pop di
    jmp .e4
.e0:
    xor bx, bx                  ; (no step: the start is t1's)
    xor ax, ax
    cwd
    jmp .e4
.e3:
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
.e4:
    or bp, bp                   ; the sign onto both
    jz .e5
    neg dx
    neg ax
    sbb dx, 0
    neg si
    neg bx
    sbb si, 0
.e5:
    add ax, 0x8000              ; + a half + t1 (x 16384)
    adc dx, 0
    mov cx, di
    sar cx, 1
    sar cx, 1
    ror di, 1
    ror di, 1
    and di, 0xC000
    add ax, di
    adc dx, cx
    mov cx, ax                  ; BX.CX = the start
    mov ax, bx                  ; DX.AX = the step
    mov bx, dx
    mov dx, si
    ret

; wa_fspan - wa_face's way for a face with BOTH ends in the view: a flat
; span straight off the vertex cache, the pre-check's columns standing for
; wa_span's own scan - no copy into wa_c1.., no second scan. SI = v1 x 2,
; DI = v2 x 2 (made: wa_face.mk); [wa_cs], [wa_end], [wa_cs0], [wa_ce0],
; wa_e1 and wa_e2 set by wa_face, and its [wa_fv] still the two vertices (a
; steep edge's way out reads them back). On heights (wa_fspanh) the passes
; test the column's clip only where a low box has raised one in the range:
; with none raised, no low box has a cell there, and the plain passes draw
; the same cells.
wa_fspanh:                      ; (heights' tall faces)
    push di
    call wa_fclpc
    pop di
wa_fspan:
    mov bx, [wa_tag]            ; the span's tag and its line first (.reg's;
    inc bx                      ; a steep edge's way out takes it back): the
    inc bx                      ; table full stops drawing, as .reg does
    cmp bx, WA_MAXSP*2
    jae wa_span.full
    mov [wa_tag], bx
    mov ax, [wa_vpc+si]         ; c1
    mov [wa_spcs+bx], ax
    mov dx, [wa_vpc+di]
    sub dx, ax                  ; dc = c2 - c1
    mov [wa_spsh+bx], dx
    mov cx, [wa_vpi+si]
    mov [wa_spih+bx], cx
    neg cx
    add cx, [wa_vpi+di]
    mov [wa_spsl+bx], cx
    cmp dx, 16                  ; the edges' far ends: v2's - or, a segment
    ja .st                      ; inside one column, v1's (no step:
    mov di, si                  ; wa_span's rule)
.st:
    mov cx, [wa_vpb+si]         ; the bottom's ends
    mov bp, [wa_vpb+di]
; wa_flat - THE FLAT SPAN, nine in ten: CX, BP = the bottom edge's ends,
; [wa_vpt+si] and [wa_vpt+di] the top's (quarter half-rows); the span
; registered, [wa_tag] its tag, AX = its c1 and DX = c2 - c1; [wa_cs],
; [wa_end] and the corners' as wa_fspan has them.
; BOTH EDGES LEVEL - each moves under a half-row along the whole wall, so
; it is drawn level at its middle's half-row (LK_COARSE, the 8088's levels:
; six flat spans in ten on the play bench) - is ONE loop: no divide, no
; fraction, a column opened and both its cells in one turn (.l2), the posts
; between the same two half-rows at both ends. Anything else is .fgx's: an
; edge at a time (wa_fedge), two passes by runs (FLATTAIL), SPANLOOP's when
; one is steep.
wa_flat:
    mov bx, bp                  ; the bottom's change in -3..3?
    sub bx, cx
    add bx, 3
    cmp bx, [wa_lvb]
    jae .fgx
    mov bx, [wa_vpt+di]         ; ...and the top's
    sub bx, [wa_vpt+si]
    add bx, 3
    cmp bx, [wa_lvt]
    jae .fgx
    cmp byte [wa_fclp], 0       ; (heights, a column's clip raised in the
    jne .fgx                    ; range: the clip-tested passes)
    add cx, bp                  ; e1 + change / 2, rounded to a half-row:
    add cx, 4                   ; (e1 + e2 + 4) >> 3
    sar cx, 1
    sar cx, 1
    sar cx, 1
    mov bx, [wa_vpt+si]
    add bx, [wa_vpt+di]
    add bx, 4
    sar bx, 1
    sar bx, 1
    sar bx, 1
    mov [wa_vb0], cx            ; (for the posts)
    mov [wa_vt0], bx
    mov dl, [wa_frame]
    mov dh, [wa_tag]            ; DH = the tag
    mov di, [wa_lp]
    mov word [wa_ncl], 0
    mov si, [wa_end]            ; the column past the end reads shut (a high
    mov byte [wa_cov+si+1], 1   ; byte: always 0 otherwise)
    mov si, [wa_cs]
    cmp cx, WA_HR
    jae .l1                     ; the bottom off the screen: the top alone
    cmp bx, WA_HR
    jae .l1b                    ; the top off it: the bottom alone
    mov bp, cx
    mov ah, [wa_wcol]           ; the wall's colour: its ITEM's, not its depth
    mov ch, ah
    mov al, [wa_hgl+bx]
    mov cl, [ds:bp+wa_hgl]
    shl bx, 1
    mov bx, [wa_hroff+bx]
    shl bp, 1
    mov bp, [ds:bp+wa_hroff]
    cmp bx, bp
    je .l1x                     ; one row (a far wall): the top's cell alone
    add bx, si                  ; BX, BP = the two cells; AX, CX = their
    add bp, si                  ; glyphs
.l2:
    cmp word [wa_cov+si], 0
    jne .l2s
    mov [wa_cov+si], dh
    mov [wa_stamp+bx], dl
    mov [wa_cur+bx], ax
    mov [ds:bp+wa_stamp], dl
    mov [ds:bp+wa_cur], cx
    xchg ax, bx                 ; both onto the list
    stosw
    xchg ax, bx
    xchg ax, bp
    stosw
    xchg ax, bp
.l2a:
    inc bx
    inc bx
    inc bp
    inc bp
    inc si
    inc si
    jmp .l2
.l2s:
    cmp si, [wa_end]
    jae .ldone
    inc word [wa_ncl]
    jmp .l2a
.l1x:
    mov bx, [wa_vt0]
    jmp .l1
.l1b:
    mov bx, cx
.l1:                            ; one edge, BX (or none: off the screen):
    mov [wa_fh], bx             ; the opening pass, no step
    xor cx, cx
    mov ah, [wa_wcol]
    call wa_fo
.ldone:                         ; (SI = the end)
    mov byte [wa_cov+si+1], 0
    xchg ax, si                 ; opened: the range less the shut ones
    sub ax, [wa_cs]
    shr ax, 1
    sub ax, [wa_ncl]
    sub [wa_left], ax
    test byte [wa_look], LK_NOPOST ; (NOPOST: a wall shorter than
    jz .np                      ; WA_POSTMIN half-rows - a far one - has no
    mov ax, [wa_vb0]            ; corner posts)
    sub ax, [wa_vt0]
    cmp ax, WA_POSTMIN
    jl .pdone
.np:
    cmp byte [wa_e1], 0         ; the corners, at the segment's own ends,
    je .c2                      ; when this span opened them: a `│` each
    mov si, [wa_cs]             ; (nothing else is in either column: no
    cmp si, [wa_cs0]            ; stamp tests)
    jne .c2
    mov bx, [wa_vt0]
    mov ax, [wa_vb0]
    xor cl, cl
    call wa_corner
.c2:
    cmp byte [wa_e2], 0
    je .pdone
    mov si, [wa_ce0]
    cmp si, [wa_end]            ; (on the screen)
    jge .pdone
    cmp [wa_cov+si], dh         ; (and opened here)
    jne .pdone
    mov bx, [wa_vt0]
    mov ax, [wa_vb0]
    xor cl, cl
    call wa_corner
.pdone:
    mov [wa_lp], di
    ret

.fgx:                           ; an edge that is not level, or the clip's passes
    mov [wa_dc], dx             ; (wa_fedge's, and a steep edge's)
    mov bx, [wa_cs]             ; the first open centre past c1, x16
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, 8
    sub bx, ax
    mov [wa_fs], bx
    mov ax, 4                   ; PASS 2 tests its cell for the top's (the
    mov bx, [wa_vpt+si]         ; clip's pass does) unless the wall is three
    push bx                     ; half-rows tall or more at both ends - so
    neg bx                      ; at every column, and its edges share no
    add bx, cx                  ; cell: the plain pass, no test
    cmp bx, 12
    mov bx, [wa_vpt+di]         ; (the top's ends, for after the bottom's)
    push bx
    jl .thin
    neg bx
    add bx, bp
    cmp bx, 12
    jl .thin
    mov ax, [wa_fclp]
.thin:
    mov [wa_p2i], ax
    xchg ax, cx
    mov bx, bp
    call wa_fedge
    jc .steep2
    mov [wa_vb0], si            ; its half-row at the first open column,
    mov [wa_qb], bp             ; fraction, step and direction
    mov [wa_fb], cx
    mov [wa_db], di
    pop bx                      ; the top
    pop ax
.ftop:
    cmp byte [wa_roomf], 0      ; (ROOMF)
    je .tf
    call wa_rnotop
    jmp .td
.tf:
    call wa_fedge
    jc .steep
.td:
    mov bx, [wa_tag]
    jmp wa_span.ftail           ; the passes and the corners
.steep2:
    add sp, 4
.steep:
    xor bh, bh                  ; the face's vertices again (wa_face's):
    mov bl, [wa_fv]             ; one outside the view is a CLIPPED span's,
    mov si, bx                  ; wa_c1.. holding it (wa_span) - else the
    mov bl, [wa_fv+1]           ; vertices into wa_c1..
    mov di, bx
    mov al, [wa_vok+si]
    and al, [wa_vok+di]
    jz wa_span.steepm
    jmp wa_fsteep

; wa_collook - once a frame (wa_lookset): wa_flat's level tests, from the
; detail level
wa_collook:
    cmp byte [wa_hflat], 0      ; A FLAT ARENA'S CLIP: the screen's end in
    je .hts                     ; every column, made once (heights clears
    cmp byte [wa_cflat], 0      ; its own each frame: wa_render) - so the
    jne .lv                     ; steep loop, wa_slope and wa_cclp test the
    mov di, wa_cclip            ; clip on any arena, and the screen with it
    mov ax, WA_HR
    mov cx, WA_MAXW
    rep stosw
.hts:
    mov al, [wa_hflat]
    mov [wa_cflat], al
.lv:
    xor ax, ax
    mov [wa_fclp], ax           ; (the plain passes: heights' spans say
                                ; otherwise - wa_fclpc)
    test byte [wa_look], LK_COARSE
    jz .b
    mov al, 7
.b:
    mov [wa_lvb], ax
    test byte [wa_look], LK_ROOMF ; (no top drawn: .fgl's way throughout)
    jnz .z
    cmp [wa_sparse], ah
    je .t
.z:
    xor ax, ax
.t:
    mov [wa_lvt], ax
    ret

; wa_rnotop - ROOMF: the room's top edge not drawn - wa_fedge's answer for an
; edge above the screen - and no corner posts
wa_rnotop:
    mov si, -1
    xor bp, bp
    xor cx, cx
    mov di, 1
    mov byte [wa_e1], 0
    mov byte [wa_e2], 0
    ret

; wa_fclpc - heights: [wa_fclp] = 4 when a column of [wa_cs, wa_end) is
; under a low box's top (its clip raised: the clip-tested passes, wa_ptab),
; else 0. Clobbers AX, CX, DI
wa_fclpc:
    mov cx, [wa_end]
    mov di, [wa_cs]
    sub cx, di
    shr cx, 1
    add di, wa_cclip
    mov ax, WA_HR
    repe scasw
    mov ax, 0
    je .no
    mov al, 4
.no:
    mov [wa_fclp], ax
    ret

; wa_fsteep - a steep edge in wa_fspan's span: the vertices into wa_c1..
; (with wa_span's one-column rule) and on to SPANLOOP's way, the tag taken
; back for .reg to take again
wa_fsteep:
    call wa_tomem
    sub word [wa_tag], 2
    cmp word [wa_dc], 16
    ja .go
    mov ax, [wa_t1]
    mov [wa_t2], ax
    mov ax, [wa_b1]
    mov [wa_b2], ax
.go:
    jmp wa_span.gen1

; wa_tomem - SI = v1 x 2, DI = v2 x 2 (in the view, made): their column,
; top, bottom and inverse depth into wa_c1..wa_i2. Clobbers AX, BX, DI
wa_tomem:
    mov bx, di
    mov di, wa_c1
    mov ax, [wa_vpc+si]
    stosw
    mov ax, [wa_vpt+si]
    stosw
    mov ax, [wa_vpb+si]
    stosw
    mov ax, [wa_vpi+si]
    stosw
    mov ax, [wa_vpc+bx]
    stosw
    mov ax, [wa_vpt+bx]
    stosw
    mov ax, [wa_vpb+bx]
    stosw
    mov ax, [wa_vpi+bx]
    stosw
    ret

; --- the column loops ---------------------------------------------------------
; The steep spans' loop (a flat span is wa_flat's), assembled three ways so
; no column tests a mode: %1 = 1 draws the TOP edge (0: the sparse one,
; floor lines and corner posts - and a top above the screen), %2 = 1 the
; BOTTOM (0: under the screen for the rest of the span).
; In: SI = first column x 2, [wa_end] = one past the last x 2 (the first
; and last are open), CX.BP = the top (whole half-row, rounded, and a
; 16-bit fraction), wa_vb.wa_vbl the bottom alike, DL = the stamp, DH = the
; span's tag, AH = the wall's attribute, DI = the list. Out: DI, the two
; accumulators one column past the end (an edge dropped: off the screen as
; it is there), wa_ncl = the columns found shut.
; An edge is ONE test against the column's clip (wa_cclip: the screen's
; last half-row + 1 where no low box stands, and on a flat arena throughout
; - wa_collook). An edge two half-rows or more from the column before's
; (wa_pht, wa_phb; a column that was not ours has none) is a STEP: its cell
; and the fill between come from wa_efill, out of line - in a steep span
; most columns. An edge OFF the screen and moving away from it (wa_tup,
; wa_bdn) is DROPPED: the loop goes on in the one without it - a near wall's
; bottom is under the screen for most of a steep span - and with both gone
; the rest of the span is opened and no more (wa_stail).
%macro SPANLOOP 2
wa_ls%1%2:
    cmp word [wa_cov+si], 0
    jne %%shut
    mov [wa_cov+si], dh
    cmp [wa_cov+si-2], dh       ; the column before ours? Else no step from
    je %%ours                   ; it (wa_covpad stands before column 0)
%if %1
    mov [wa_pht], cx
%endif
%if %2
    mov bx, [wa_vb]
    mov [wa_phb], bx
%endif
%%ours:
%if %1
    mov bx, cx
    cmp bx, [wa_cclip+si]       ; off the screen, or under a nearer low box's
    jae %%to                    ; top
    xchg bx, [wa_pht]           ; (this column's, for the next)
    sub bx, cx
    inc bx                      ; -1..+1 -> 0..2: no step
    cmp bx, 3
    jae %%tf
    mov bx, cx
    mov al, [wa_hgl+bx]
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, si
    WPLOT                       ; (a low box's edge may share the cell)
%%t:
%endif
%if %2
wa_ls%1%2_b:
    mov bx, [wa_vb]
    cmp bx, [wa_cclip+si]
    jae %%bo
    xchg bx, [wa_phb]
    sub bx, [wa_vb]
    inc bx
    cmp bx, 3
    jae %%bf
    mov bx, [wa_vb]
    mov al, [wa_hgl+bx]
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, si
    WPLOT                       ; (the top's cell, a wall under a row tall)
%%b:
%endif
wa_ls%1%2_s:
    add bp, [wa_stl]            ; 16.16: the fraction carries
    adc cx, [wa_st]
%if %2
    mov bx, [wa_sbl]
    add [wa_vbl], bx
    mov bx, [wa_sb]
    adc [wa_vb], bx
%endif
    inc si
    inc si
    cmp si, [wa_end]
    jb wa_ls%1%2
    ret
%%shut:                         ; (a nearer wall: wa_gap jumps the run)
    call wa_gap
    jmp wa_ls%1%2
%if %1
%%tf:                           ; the top STEPPED: its cell and the fill
    dec bx                      ; (BX = the last column's top)
    add bx, cx
    push cx
    xchg bx, cx
    call wa_efill
    pop cx
    jmp %%t
%%to:                           ; the top OFF the screen (or clipped): no
    mov bx, [wa_pht]            ; cell - and no fill either unless the
    mov [wa_pht], cx            ; last column's was on it, or off it the
    cmp bx, [wa_cclip+si]       ; other way
    jb %%tx
    xor bx, cx
    jns %%td
    xor bx, cx
%%tx:
    push cx
    xchg bx, cx
    call wa_eoff
    pop cx
%%td:
    or cx, cx                   ; above the screen, and moving up: for good
    jns %%t
    cmp byte [wa_tup], 0
    je %%t
  %if %2
    jmp wa_ls01_b               ; (the bottom alone from here)
  %else
    jmp wa_stail
  %endif
%endif
%if %2
%%bf:
    dec bx
    add bx, [wa_vb]
    push cx
    mov cx, bx
    mov bx, [wa_vb]
    call wa_efill
    pop cx
    jmp %%b
%%bo:
    xchg bx, [wa_phb]           ; (BX = the last column's bottom)
    cmp bx, [wa_cclip+si]
    jb %%bx
    xor bx, [wa_vb]
    jns %%bd
    xor bx, [wa_vb]
%%bx:
    push cx
    mov cx, bx
    mov bx, [wa_vb]
    call wa_eoff
    pop cx
%%bd:
    cmp word [wa_vb], WA_HR     ; under the screen, and moving down: for good
    jl %%b
    cmp byte [wa_bdn], 0
    je %%b
    mov word [wa_vb], 2048      ; (under it wherever the far corner looks)
  %if %1
    jmp wa_ls10_s               ; (the top alone from here)
  %else
    jmp wa_stail
  %endif
%endif
%endmacro
    SPANLOOP 1, 1               ; full
    SPANLOOP 0, 1               ; sparse, or the top above the screen
    SPANLOOP 1, 0               ; the bottom under it

; wa_stail - SPANLOOP's end, both edges off the screen for good: the rest
; of the span's columns opened and counted, nothing drawn; the top one
; column on (as its accumulator is where the loop ends: off the screen
; there as here). SI = the column the loop stood in. Clobbers AX, BX, SI.
wa_stail:
    add bp, [wa_stl]
    adc cx, [wa_st]
    push cx
    mov bx, di                  ; (the list)
    lea di, [si+wa_cov+2]
    mov cx, [wa_end]
    sub cx, si
    shr cx, 1
    dec cx
    jz .done
    xor ax, ax
.l:
    scasw
    jne .s
    mov [di-2], dh
.n:
    loop .l
.done:
    mov di, bx
    pop cx
    ret
.s:
    inc word [wa_ncl]
    jmp .n

; wa_efill - SPANLOOP's STEP: BX = an edge's half-row in the column SI, CX =
; its half-row the column before, two or more apart: the edge's cell, and
; the step between filled down this column - `/` under an edge that rose,
; `\` over one that fell - a cell a row, stepped from the edge's own (no row
; looked up; wa_slope's rows less the edge's own cell, which the edge took
; first). BX is on the screen and over the clip (off it: wa_eoff). Keeps
; SI, DX, AH and BP, moves DI on; clobbers AL, BX, CX.
wa_efill:
    push bp
    mov bp, bx                  ; BP = the edge
    mov al, [wa_hgl+bx]
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, si
    WPLOT
    cmp cx, bp
    jl .fall
    cmp cx, [wa_cclip+si]       ; it rose: the rows under its own to the last
    jle .r1                     ; column's - no lower than the clip (the
    mov cx, [wa_cclip+si]       ; screen's end)
.r1:
    dec cx
    shr cx, 1
    shr bp, 1
    sub cx, bp
    jle .out
    mov al, '/'
    mov bp, [wa_rowo2+2]        ; (a row's offset: row 1's)
    jmp .rows
.fall:
    inc cx                      ; it fell: the rows over its own from the last
    sar cx, 1                   ; column's - no higher than the screen's top
    jns .f1
    xor cx, cx
.f1:
    shr bp, 1
    sub bp, cx
    jle .out
    mov cx, bp
    mov al, 0x5C
    mov bp, [wa_rowo2+2]
    neg bp
.rows:
    add bx, bp
    WPLOT
    loop .rows
.out:
    pop bp
    ret
; wa_eoff - SPANLOOP's edge BX OFF the screen (or under the clip) in the
; column SI, CX = its half-row the column before, on the screen or off it
; the other way: a step of two half-rows or more between them is wa_slope's.
; Keeps SI, DX, AH and BP, moves DI on; clobbers AL, BX, CX.
wa_eoff:
    mov ax, bx
    sub bx, cx
    inc bx
    cmp bx, 3
    jb .no
    call wa_stepfill
.no:
    mov ah, [wa_wcol]
    ret

; --- the flat span's passes ---------------------------------------------------
; A FLAT span (neither edge moves a half-row a column) at full detail - nine
; spans in ten. Such an edge keeps its half-row for RUNS of
; columns, so its whole part is not stepped: the step's fraction is added and
; the carry is the change, after which the cell is simply the next column's.
; An edge moving UP the screen subtracts it instead, a borrow being the
; change; a pass keeps that fraction COMPLEMENTED (~(x - f) = ~x + f, and the
; borrow is the carry), so one loop serves both directions. With ONE edge a
; pass, the fraction and its step stay in registers where SPANLOOP kept the
; bottom in memory: pass 1 opens the columns (wa_cov 0 -> the tag) and draws
; the top (its column's first cell: no stamp test), pass 2 draws the bottom
; in the columns pass 1 opened, stamp-tested (a wall under a row tall). The
; arithmetic and the cells are exactly SPANLOOP's; the list's order is not,
; and nothing reads it. A shut column costs a step, not a call (wa_gap).
; FLATPASS %1 = 1 the opening pass, 0 the second; %2 = the edge's direction
; (+1 down the screen, -1 up). In: SI = the first column x 2, BP = the
; edge's fraction (a pass's form) and [wa_fh] its half-row there, CX = the
; step's fraction, AH = the attribute, DL = the stamp, DH = the tag, DI = the
; list, [wa_end] = one past the last column x 2 - which pass 1 must find SHUT
; (wa_span's sentinel). Out: DI; BP and [wa_fh] one column past the end;
; pass 1 counts the columns it found shut into wa_ncl. Clobbers AL, BX, SI.
%macro FLATPASS 2-3 0
    mov bx, [wa_fh]             ; (wa_fcell, inline)
    cmp bx, WA_HR
    jae %%off
    mov al, [wa_hgl+bx]
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, si
%%col:
%if %1
    cmp word [wa_cov+si], 0
    jne %%shut
    mov [wa_cov+si], dh
%else
    cmp [wa_cov+si], dh
    jne %%shut
%endif
%if %3
    push ax                     ; (heights: under a nearer low box's top?)
    mov ax, [wa_fh]
    cmp ax, [wa_cclip+si]
    pop ax
    jae %%adv
%endif
%if %3
    cmp [wa_stamp+bx], dl       ; (a low box's edge may share the cell, or
    je %%adv                    ; the top's: a wall under a row tall)
%endif
    mov [wa_stamp+bx], dl
    mov [wa_cur+bx], ax
    xchg ax, bx
    stosw
    xchg ax, bx
%%adv:
    inc bx
    inc bx
    inc si
    inc si
    add bp, cx
    jnc %%col
    mov bx, [%2]                ; the next half-row (wa_fcell, inline)
    add bx, [wa_fh]
    mov [wa_fh], bx
    cmp bx, WA_HR
    jae %%off
    mov al, [wa_hgl+bx]
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, si
    jmp %%col
%%shut:
    cmp si, [wa_end]
    jae %%done
%if %1
    inc word [wa_ncl]
%endif
    jmp %%adv
%%off:                          ; (the edge is off the screen here)
%if %1
    cmp word [wa_cov+si], 0
    jne %%oshut
    mov [wa_cov+si], dh
%else
    cmp si, [wa_end]
    jae %%done
%endif
%%oadv:
    inc si
    inc si
    add bp, cx
    jnc %%off
    mov bx, [%2]
    add [wa_fh], bx
    call wa_fcell
    jc %%off
    jmp %%col
%if %1
%%oshut:
    cmp si, [wa_end]
    jae %%done
    inc word [wa_ncl]
    jmp %%oadv
%endif
%%done:
    ret
%endmacro
wa_fo:      FLATPASS 1, wa_dt
wa_fp:      FLATPASS 0, wa_db
wa_foc:     FLATPASS 1, wa_dt, 1    ; (heights: against the column's clip)
wa_fpc:     FLATPASS 0, wa_db, 1
wa_ptab:    dw wa_fo, wa_fp, wa_foc, wa_fpc ; (by wa_fclp)

; wa_fcell - [wa_fh] in the column SI: AL = its glyph, BX = its cell, CF = 0;
; CF = 1 off the screen
wa_fcell:
    mov bx, [wa_fh]
    cmp bx, WA_HR
    jae .off
    mov al, [wa_hgl+bx]
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, si                  ; (CF = 0)
    ret
.off:
    stc
    ret

; wa_fedge - AX = e1, BX = e2 (an edge's ends, quarter half-rows): a FLAT
; edge's start and step, exactly as .edge makes them (wa_span's own, which
; does the steep ones too). CF = 1 steep (a half-row a column or more:
; SPANLOOP's). Else SI.BP = its value at the first open column (half-rows,
; 16.16, the whole part rounded; BP in a pass's form, FLATPASS), CX = the
; step's fraction - its whole part is 0 - and DI = +1 down the screen, -1 up.
; An edge OFF THE SCREEN at both ends is off it at every column between (its
; values lie between theirs), so it is never drawn, steep or not: it is
; half-row -1 or 48, no step, and no divide. Clobbers AX, DX.
wa_fedge:
    cmp ax, -3                  ; above the screen, both ends (half-rows
    jg .na                      ; under 0, rounded)?
    cmp bx, -3
    jle .above
.na:
    cmp ax, 190                 ; ...below it (48 and over)?
    jl .on
    cmp bx, 190
    jge .below
.on:
    sub bx, ax                  ; the change
    mov si, ax
    xchg ax, bx
    cwd
    mov di, dx                  ; its sign
    xor ax, dx
    sub ax, dx                  ; |change|
    shl ax, 1
    jc .steep
    cmp ax, [wa_lvb]            ; (COARSE: an edge that moves less than a
    jb .small                   ; half-row along the whole wall - |change|
    shl ax, 1                   ; x 2 under 7 - is drawn level, at its
                                ; middle's half-row)
    jc .steep
    cmp ax, [wa_dc]             ; |change| x 4 under a column's 16: under a
    jae .steep                  ; half-row a column
    xchg ax, dx
    xor ax, ax
    div word [wa_dc]            ; the step: x 65536 / dc
    mov cx, ax
    mul word [wa_fs]            ; x off / 16
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    mov bp, si                  ; + e1 (x 16384) + a half
    ror bp, 1
    ror bp, 1
    and bp, 0xC000
    sar si, 1
    sar si, 1
    add bp, 0x8000
    adc si, 0
    or di, di
    jnz .up
    add bp, ax
    adc si, dx
    inc di
    clc
    ret
.up:
    sub bp, ax
    sbb si, dx
    not bp                      ; (a pass's form)
    clc
    ret
.small:
    xor ax, di                  ; e1 + change / 2, rounded to a half-row
    sub ax, di                  ; (AX = |change| x 2)
    sar ax, 1
    sar ax, 1
    add si, ax
    add si, 2
    sar si, 1
    sar si, 1
    jmp .off
.above:
    mov si, -1
    jmp .off
.below:
    mov si, WA_HR
.off:
    xor cx, cx
    mov di, 1
    xor bp, bp                  ; (CF = 0)
    ret
.steep:
    stc
    ret

; wa_gap - SI = a shut column x 2 inside a span's loop: on to the next open
; one (the span's last column is open, so there is one), the accumulators
; jumped across the run by MULTIPLYING - a gap behind a nearer box averages
; eight columns, each of which used to be stepped through the loop - and
; wa_ncl counted. Keeps everything else the loop holds.
wa_gap:
    push ax
    push dx
    push di
    push cx
    lea di, [si+wa_cov+2]
    mov cx, [wa_end]
    sub cx, si
    shr cx, 1
    dec cx                      ; the columns after this one (one is open)
    xor ax, ax
    repne scasw
    lea ax, [di-2-wa_cov]
    mov cx, ax
    sub cx, si
    mov si, ax                  ; SI = the next open column
    shr cx, 1                   ; CX = the shut ones, k
    add [wa_ncl], cx
    mov ax, [wa_stl]            ; the top: + k x the step, whole.fraction
    mul cx
    add bp, ax
    adc dx, 0
    mov bx, dx
    mov ax, [wa_st]
    imul cx                     ; (k x the whole part: its low word)
    add bx, ax
    pop ax
    add ax, bx
    push ax
    mov ax, [wa_sbl]            ; the bottom alike
    mul cx
    add [wa_vbl], ax
    adc dx, 0
    mov bx, dx
    mov ax, [wa_sb]
    imul cx
    add ax, bx
    add [wa_vb], ax
    pop cx
    pop di
    pop dx
    pop ax
    ret

; wa_stepfill - AX = this column's edge, CX = the last one's (half-rows), two
; half-rows or more apart: a step to fill (wa_slope) - unless both are at or
; above the screen's top half-row (a near wall's top), or both on or below
; its last: wa_slope's rows between them are none
wa_stepfill:
    cmp ax, 0
    jg .t1
    cmp cx, 0
    jle .no
.t1:
    cmp ax, WA_HR-1
    jl .go
    cmp cx, WA_HR-1
    jge .no
.go:
    jmp wa_slope
.no:
    ret

; wa_wallz - BX = column x 2: the inverse depth of the wall drawn there
; (0 none) into wa_colz[BX], from the span that opened it - once a frame,
; and only for the columns that are asked about. Keeps every register.
wa_wallz:
    cmp word [wa_colz+bx], 0xFFFF
    jne .out
    push ax
    push cx
    push dx
    push si
    mov si, [wa_cov+bx]
    xor ax, ax
    or si, si
    jz .put
    mov ax, bx                  ; iz = i1 + (i2 - i1)(16c + 8 - c1) / (c2 - c1)
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, 8
    sub ax, [wa_spcs+si]
    imul word [wa_spsl+si]
    idiv word [wa_spsh+si]
    add ax, [wa_spih+si]
.put:
    mov [wa_colz+bx], ax
    pop si
    pop dx
    pop cx
    pop ax
.out:
    ret

; wa_occl - BX = column x 2, AX = an inverse depth: AL = the first half-row
; something nearer hides in that column - 0 the whole column (its tall face
; is nearer: wa_wallz), 0xFF none - the one query the figures, the pistol's
; ray and the floor grid ask (docs/plans/WIREARENA-HEIGHTS-PLAN.md 7.2.5).
; On a flat arena that is wa_wallz's test and nothing more. Keeps every
; register but AL.
wa_occl:
    push cx
    call wa_wallz
    xor cl, cl
    cmp [wa_colz+bx], ax
    ja .out                     ; the column's tall face is nearer
    mov cl, 0xFF
    cmp byte [wa_hflat], 0
    jne .out
    call wa_occlow
.out:
    mov al, cl
    pop cx
    ret

; wa_occlow - BX = column x 2, AX = an inverse depth, CL = a half-row: CL
; lowered to the row the column's depth stack hides from at that depth -
; its entries front to back while they are nearer (wa_cst). Keeps every
; register but CL.
wa_occlow:
    cmp byte [wa_cstn+bx], 0    ; (no entries: nothing to lower it - most
    je .none                    ; columns)
    push ax
    push dx
    push si
    push di
    push bp
    mov bp, ax                  ; BP = the depth asked about
    mov dl, [wa_cstn+bx]        ; DL = the entries
    mov di, bx                  ; DI = entry k's place, less wa_cst
.e:
    or dl, dl
    jz .done
    mov si, [wa_cst+di]
    mov dh, [wa_cst+di+1]       ; DH = the row it hides from
    and si, 0xFF                ; SI = its span's tag
    mov ax, [wa_spih+si]        ; its inverse depth here lies between its
    cmp ax, bp                  ; ends' (the entry is in one of its
    jbe .lo                     ; columns), so the ends often settle it:
    add ax, [wa_spsl+si]        ; both nearer...
    cmp ax, bp
    ja .near
    jmp .exact
.lo:
    add ax, [wa_spsl+si]        ; ...or neither
    cmp ax, bp
    jbe .done
.exact:
    mov ax, bx                  ; else as wa_wallz makes it
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, 8
    sub ax, [wa_spcs+si]
    push dx
    imul word [wa_spsl+si]
    idiv word [wa_spsh+si]
    pop dx
    add ax, [wa_spih+si]
    cmp ax, bp
    jbe .done                   ; not nearer: nor is any after it
.near:
    cmp dh, cl
    jae .next
    mov cl, dh
.next:
    add di, 2*WA_MAXW
    dec dl
    jmp .e
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop ax
.none:
    ret

; wa_slope - AX = this column's edge, CX = the last column's (half-rows),
; SI = column x 2: a step of two half-rows or more is filled down it,
; `\` falling to the right and `/` rising - one cell a row, the cell
; stepped by a row rather than looked up. Keeps SI, DI's list pointer and
; DL; clobbers AX, BX, CX.
wa_slope:
    push bp
    mov bp, '\'
    cmp ax, cx
    jg .fall
    mov bp, '/'
    xchg ax, cx                 ; AX = the lower edge, CX = the higher
.fall:
    cmp ax, [wa_cclip+si]       ; (heights: no lower than the column's clip)
    jle .cl
    mov ax, [wa_cclip+si]
.cl:
    inc cx                      ; half-rows CX+1 .. AX-1, as rows
    sar cx, 1
    jns .c0
    xor cx, cx
.c0:
    dec ax
    sar ax, 1
    cmp ax, WA_ROWS-1
    jle .c1
    mov ax, WA_ROWS-1
.c1:
    sub ax, cx
    jl .out
    inc ax
    mov bx, cx
    shl bx, 1
    mov bx, [wa_rowo2+bx]
    add bx, si
    mov cx, ax
    mov ax, bp
    mov ah, [wa_wcol]
.row:
    WPLOT
    add bx, [wa_rowo2+2]        ; (a row's offset: row 1's)
    loop .row
.out:
    pop bp
    ret

; wa_corner - BX = top, AX = bottom (half-rows), SI = column x 2: `│` in the
; rows between them, the cell stepped by a row. CL = 0 when no cell there
; can have been drawn this frame: no stamp tests. Else (wa_vedge) every
; cell is tested, first one wins, and the post ends at the column's clip
; (heights: a nearer low box hides the rest). Keeps DI's list and DX;
; clobbers AX, BX, CX, SI.
wa_vedge:
wa_corner:
    or cl, cl
    jz .rows
    cmp ax, [wa_cclip+si]
    jle .rows
    mov ax, [wa_cclip+si]
.rows:
    sar ax, 1                   ; the bottom's row: stop before it
    cmp ax, WA_ROWS
    jle .bok
    mov ax, WA_ROWS
.bok:
    sar bx, 1
    inc bx                      ; the row under the top's
    jns .tok
    xor bx, bx
.tok:
    sub ax, bx
    jle .out
    shl bx, 1
    mov bx, [wa_rowo2+bx]
    add bx, si
    mov si, [wa_rowo2+2]        ; (a row's offset: row 1's)
    or cl, cl
    xchg ax, cx                 ; CX = the rows
    mov al, 0xB3
    mov ah, [wa_wcol]
    jnz .tst
.row:
    mov [wa_stamp+bx], dl
    mov [wa_cur+bx], ax
    xchg ax, bx
    stosw
    xchg ax, bx
    add bx, si
    loop .row
.out:
    ret
.tst:
    WPLOT
    add bx, si
    loop .tst
    ret

wa_list:                        ; BX = cell, onto this frame's list
    push di
    mov di, [wa_newp]
    add di, [wa_newn]
    mov [di], bx
    add word [wa_newn], 2
    pop di
    ret

; wa_plotf - BX = cell offset, DL = sub-cell bit(s), AH = attribute: a figure's
; sub-pixels, OR'd into whatever of the figure layer the cell has, over any
; wall glyph. Clobbers AX, DX.
wa_plotf:
    mov al, [wa_frame]
    or al, 0x80
    cmp [wa_stamp+bx], al
    je .have                    ; a figure cell already: add the bit
    and al, 0x7F
    cmp [wa_stamp+bx], al
    je .listed                  ; a wall cell this frame: already listed
    call wa_list
.listed:
    or al, 0x80
    mov [wa_stamp+bx], al
    jmp .put
.have:
    or dl, [wa_fmask+bx]
.put:
    mov [wa_fmask+bx], dl
    push bx
    mov bl, dl
    and bx, 15
    mov al, [wa_fglyph+bx]
    pop bx
    mov [wa_cur+bx], ax
    ret

; --- the floor grid (G) -------------------------------------------------------
; A dim dot every 2 m, (64 + 128i, 64 + 128j), within ~6 m on each axis: a
; depth cue that does not depend on a wall's height. Behind walls (wa_wallz)
; and under figures (drawn after). Off by default: the owner's question is
; whether it costs the black ground's readability.
WA_DOTSP    equ 128
wa_dots:
    cmp byte [wa_grid], 0
    jne .on
    ret
.on:
    mov ax, [wa_px]             ; the lines within 4 of the player's
    mov si, wa_dxc
    mov di, wa_dxs
    call .lines
    mov [wa_dnx], cx
    mov ax, [wa_py]
    mov si, wa_dyc
    mov di, wa_dys
    call .lines
    mov [wa_dny], cx
    mov word [wa_dix], 0
.ix:
    mov word [wa_diy], 0
.iy:
    mov si, [wa_dix]
    mov bp, [wa_diy]
    mov ax, [wa_dxc+si]         ; z = (Xc + Ys) >> 14
    mov dx, [wa_dxc+si+2]
    add ax, [ds:wa_dys+bp]
    adc dx, [ds:wa_dys+bp+2]
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    cmp dx, 48
    jl .next                    ; behind, or at our feet
    mov cx, dx                  ; CX = z
    mov ax, [wa_dxs+si]         ; x = (Xs - Yc) >> 14
    mov dx, [wa_dxs+si+2]
    sub ax, [ds:wa_dyc+bp]
    sbb dx, [ds:wa_dyc+bp+2]
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov ax, dx                  ; AX = x
    mov bx, ax
    or bx, bx
    jns .xa
    neg bx
.xa:
    cmp bx, cx
    jg .next                    ; outside even the widest view
    push ax                     ; ONE divide a dot: 1/z, then multiplies
    mov dx, 0x0007
    mov ax, 0xFFF0
    div cx
    mov [wa_diz], ax            ; (2^19 / z)
    pop ax
    imul word [wa_diz]          ; x / z x 2^19 - |x| <= z, so 20 bits: >> 3
    sar dx, 1                   ; is x/z x 65536 in 16
    rcr ax, 1
    sar dx, 1
    rcr ax, 1
    sar dx, 1
    rcr ax, 1
    imul word [wa_k16]          ; x 8Wf / 65536: the column's offset, x16
    mov bx, dx
    add bx, [wa_c16]            ; the centre, not the scale
    push cx
    mov cl, 4
    sar bx, cl
    pop cx
    cmp bx, [wa_w]
    jae .next                   ; (negative is huge, unsigned)
    shl bx, 1
    mov ax, [wa_diz]
    call wa_occl                ; AL = the first half-row hidden there
    or al, al
    jz .next                    ; a wall is nearer
    push ax
    mov ax, [wa_numb]           ; the floor's half-row: hh + numb/z/4 =
    imul word [wa_diz]          ; hh + (numb x 2^19/z) >> 21
    push cx
    mov cl, [wa_nshb]           ; (>> 21, or >> 19 of numb / 4)
    add cl, 2
    sar dx, cl
    pop cx
    pop ax
    xchg ax, dx
    add ax, [wa_hh]
    cmp ax, WA_HR
    jae .next
    cmp al, dl
    jae .next                   ; under a nearer platform's band
    mov si, ax
    shl si, 1
    add bx, [wa_hroff+si]       ; BX = the cell
    mov ah, [wa_at+A_DOT]
    mov al, 0xFA                ; upper half: the middle dot...
    test si, 2
    jz .gl
    mov al, '.'                 ; ...lower: the full stop
.gl:
    mov dl, [wa_frame]
    mov di, [wa_lp]
    WPLOT
    mov [wa_lp], di
.next:
    add word [wa_diy], 4
    mov ax, [wa_diy]
    cmp ax, [wa_dny]
    jb .iy
    add word [wa_dix], 4
    mov ax, [wa_dix]
    cmp ax, [wa_dnx]
    jb .ix
    ret
; .lines - AX = the player's coordinate, SI/DI = cos/sin product tables: the
; grid lines within three of it, (v - p) cos and (v - p) sin each. CX = their
; count x 4.
.lines:
    mov bx, ax
    mov cl, 7
    shr bx, cl                  ; the line nearest below, roughly
    sub bx, 3
    jns .l0
    xor bx, bx
.l0:
    mov dx, bx
    add dx, 7
    cmp dx, 16
    jbe .l1
    mov dx, 16
.l1:
    xor cx, cx
.l2:
    push ax
    push dx
    mov dx, bx                  ; the line's coordinate: 64 + 128 k
    push cx
    mov cl, 7
    shl dx, cl
    pop cx
    add dx, 64
    sub dx, ax
    push dx
    mov ax, dx
    imul word [wa_cos]
    push bx
    mov bx, cx
    mov [si+bx], ax
    mov [si+bx+2], dx
    pop bx
    pop ax
    imul word [wa_sinv]
    push bx
    mov bx, cx
    mov [di+bx], ax
    mov [di+bx+2], dx
    pop bx
    pop dx
    pop ax
    add cx, 4
    inc bx
    cmp bx, dx
    jb .l2
    ret

; --- figures ----------------------------------------------------------------
wa_figures:
    push bp
    mov di, wa_figs
    xor bx, bx                  ; BX = the figure x 2
    xor bp, bp                  ; BP = how many are in view, x 2
.proj:
    mov word [di+F_PZ], 0
    cmp byte [di+F_NFR], 0      ; (no keyframes: blown to bits and gone,
    je .nxt                     ; WEAPONS-PLAN 2.3)
    push bx
    push di
    mov ax, [di+F_X]
    mov bx, [di+F_Y]
    call wa_xform
    pop di
    pop bx
    mov [wa_fxs+bx], ax
    mov [wa_fzs+bx], dx
    mov [wa_frec+bx], di
    cmp dx, 32                  ; half a metre: too close to draw
    jl .nxt
    mov cx, dx
    add cx, 64
    or ax, ax
    jns .ax
    neg ax
.ax:
    cmp ax, cx
    jg .nxt                     ; outside the view, with a body's margin
    mov si, bp                  ; in view: into wa_ford, far to near - an
.ins:                           ; insertion, so the first of equal depths
    or si, si                   ; stays first, as the old pick had it
    jz .put
    mov ax, [wa_ford-2+si]
    xchg ax, bx
    cmp dx, [wa_fzs+bx]
    xchg ax, bx
    jle .put
    mov [wa_ford+si], ax
    dec si
    dec si
    jmp .ins
.put:
    mov [wa_ford+si], bx
    inc bp
    inc bp
.nxt:
    add di, F_SIZE
    inc bx
    inc bx
    cmp bx, WA_NFIG*2
    jb .proj
    xor si, si
.draw:                          ; far to near
    cmp si, bp
    jae .done
    mov bx, [wa_ford+si]
    push si
    push bp
    mov di, [wa_frec+bx]
    mov ax, [wa_fxs+bx]
    mov dx, [wa_fzs+bx]
    call wa_figure
    pop bp
    pop si
    inc si
    inc si
    jmp .draw
.done:
    pop bp
    ret
; WA_MINMAX t - BX, DX = the least and the most of the eleven words at t
; (unrolled for WA_NJ = 11, which tools/arenagen.py asserts): the first, then
; five pairs, each ordered by one compare and its smaller tried against the
; least, its larger against the most - three compares a pair, not four.
; Clobbers AX, CX, SI.
%macro WA_MINMAX 1
    mov si, %1
    lodsw
    mov bx, ax
    mov dx, ax
  %rep 5
    lodsw
    mov cx, ax
    lodsw
    cmp ax, cx
    jle $+3                     ; (over the one-byte exchange)
    xchg ax, cx                 ; AX = the smaller, CX = the larger
    cmp ax, bx
    jge $+4                     ; (over the two-byte move)
    mov bx, ax
    cmp cx, dx
    jle $+4
    mov dx, cx
  %endrep
%endmacro
; WA_LIMB a, b - a limb, joint a x 2 to joint b x 2, into wa_fbuf
%macro WA_LIMB 2
    mov cx, [wa_jsx+%1]
    mov dx, [wa_jsy+%1]
    mov ax, [wa_jsx+%2]
    mov bx, [wa_jsy+%2]
    call word [wa_flfn]         ; wa_fline, or wa_flinef: all on the screen
%endmacro

; wa_figure - DI = a figure, AX = its camera x, DX = its z. Clobbers every
; register but DI.
;
; A near figure is drawn in three passes, each priced for the 8088:
;  1. its eleven joints, blended and projected - the blend specialised per
;     quarter (wa_fjl), the scale in a register;
;  2. the box the joints span, and per column of it whether the figure is
;     in front of the wall there (wa_fvis: the span's ends bound it, and
;     wa_wallz is asked only when they do not) - none: nothing is drawn;
;     its nine limbs and head rasterised into wa_fbuf, a cell-mask scratch
;     laid out like the screen, by a Bresenham whose state is its POSITION
;     IN THE CODE (wa_fline: one block per sub-cell quadrant, so a sub-pixel
;     is one `or byte [bx], imm` and a pointer step) - ~70 clk a sub-pixel
;     against ~700 for the per-sub-pixel clip, depth test and plot it was;
;     with every joint on the screen, through wa_flinef and no clip tests;
;  3. the box swept with `repe scasw` (wa_femit): each cell the figure
;     touched is cleared out of wa_fbuf and, where wa_fvis says so, plotted
;     once - inline - however many of its four sub-pixels the limbs set.
; wa_fbuf is all zeros between figures: pass 3 clears exactly what pass 2
; set, because every sub-pixel pass 2 plots is on the screen and inside the
; joints' box (a line stays inside its endpoints' box; the head's 2x2 reaches
; one up and one right, which the box allows for).
wa_figure:
    mov [wa_fcx], ax
    mov [di+F_PZ], dx
    mov ax, [wa_eye]            ; ITS eye: the camera's less the floor it
    sub ax, [di+F_Z]            ; stands on (HEIGHTS-PLAN 3.3) - every joint
    mov [wa_feye], ax           ; is as high again over the camera's floor
    mov si, dx                  ; the inverse depth, 0x7FFF0 / z: out of
    cmp si, WA_ZMAX             ; wa_rcp (z >= 32 > NEAR), the same bits
    jae .izd
    mov bx, si
    shl bx, 1
    mov ax, [wa_rcp+bx-WA_NEAR*2]
.izok:
    mov [wa_fiz], ax
    mov [di+F_PIZ], ax
    mov ax, [wa_wf]             ; sub-columns a unit: W x f / z (f the field
    xor dx, dx                  ; of view's scale, 8.8)
    div si
    mov [wa_fs8], ax
    mov ax, [wa_kyh]
    xor dx, dx
    div si
    mov [wa_fv8], ax
    mov bx, A_FIG               ; its attribute - the far marker's too, which
    cmp byte [di+F_FLASH], 0    ; used to draw in the last near figure's
    je .att                     ; (a hit far one did not flash, and a near
    mov bl, A_FLASH             ; one's flash coloured the far ones)
    test byte [di+F_FLASH], 0x80 ; (a bot firing: yellow, AI-PLAN 4.3)
    jz .att
    mov bl, A_FIRE
.att:
    mov bl, [wa_at+bx]
    mov [wa_fatt], bl
    cmp ax, 9                   ; FAR: under ~4 half-rows tall. Nine limbs of
    jae .near                   ; a sub-pixel or two each read as a blob
    jmp wa_marker               ; anyway - draw that blob directly
.izd:                           ; (past the room's diagonal: never, but
    mov dx, 0x0007              ; exact)
    mov ax, 0xFFF0
    div si
    jmp .izok
.near:
    mov al, [di+F_SH]           ; a shield: the chest yellow (WEAPONS-PLAN
    mov [wa_fshl], al           ; 2.3)...
    xor al, al
    cmp byte [di+F_ANIM], WA_A_GIB
    jb .ngib
    inc ax                      ; ...GIB: its joints alone, the bits
.ngib:
    mov [wa_fgib], al
    mov ax, [di+F_ANG]          ; facing, relative to the view, of 16
    sub ax, [wa_cang]
    add ax, 32
    mov cl, 6
    shr ax, cl
    and ax, 15
    shl ax, 1
    xchg ax, bx
    mov bx, [wa_fco+bx]         ; BX = facing's offset in a frame
    mov ax, [di+F_PH]
    mov cl, al
    and cl, 3
    test byte [wa_look], LK_FEW
    jnz .lod                    ; (the level's J: off, one test)
.lodd:
    mov [wa_fsub], cl
    shr ax, 1
    shr ax, 1                   ; AX = keyframe
    mov cx, ax
    inc cx                      ; CX = the next one
    mov dl, [di+F_NFR]
    xor dh, dh
    cmp cx, dx
    jb .nx
    xor cx, cx
    cmp byte [di+F_LOOP], 0
    jne .nx
    mov cx, dx
    dec cx
.nx:
    mov dl, [di+F_ANIM]
    add ax, dx
    add cx, dx
    push di
    mov si, ax
    shl si, 1
    mov si, [wa_kfo+si]
    add si, bx                  ; SI = keyframe A at this facing
    mov di, cx
    shl di, 1
    mov di, [wa_kfo+di]
    add di, bx                  ; DI = keyframe B
    jmp wa_fjl                  ; the joints (and back to wa_fjdone)
.lod:                           ; a SMALL figure (fv8 under WA_FLODV): J,
    cmp word [wa_fv8], WA_FLODV ; its keyframe as it stands and only its
    jae .lodd                   ; first seven joints, the ends, joined by
    mov cl, 4                   ; five straight limbs (q 4: wa_fjl,
    jmp .lodd                   ; wa_fjdone)
; a keyframe's place, and a facing's offset in one (no multiply a figure)
wa_kfo:
%assign wa_i 0
%rep WA_NFRAMES
    dw wa_pose + wa_i*WA_FRAMESZ
%assign wa_i wa_i+1
%endrep
wa_fco:
%assign wa_i 0
%rep WA_NFACE
    dw wa_i*WA_NJ*2
%assign wa_i wa_i+1
%endrep
wa_fjdone:                      ; (wa_fjl comes back here)
    pop di
    mov ax, [wa_jsx+4]          ; for the trigger: centre, extent, width
    mov [di+F_PCX], ax
    mov ax, [wa_jsy]
    dec ax
    mov [di+F_PTOP], ax
    mov ax, [wa_jsy+10]         ; (the feet: joints 5 and 6)
    cmp ax, [wa_jsy+12]
    jge .b
    mov ax, [wa_jsy+12]
.b:
    mov [di+F_PBOT], ax
    mov ax, [wa_fs8]
    mov cx, 20
    mul cx
    mov al, ah
    mov ah, dl
    or ax, ax
    jnz .w
    inc ax
.w:
    mov [di+F_PWH], ax
    push di
    mov byte [wa_fon], 1        ; the joints' box FIRST: whether the limbs
    WA_MINMAX wa_jsx            ; may skip their clip tests, and whether any
    mov ax, [wa_w2]             ; of it is in front of the walls at all.
    or bx, bx                   ; BX = min, DX = max sub-column
    js .xoff
    cmp dx, ax
    jl .xon
.xoff:
    mov byte [wa_fon], 0        ; a joint off the side: limbs clip
.xon:
    inc dx                      ; (the 2x2 head reaches one further)
    or bx, bx
    jns .cl
    xor bx, bx
.cl:
    dec ax
    cmp dx, ax
    jle .cr
    mov dx, ax
.cr:
    sar bx, 1                   ; columns
    sar dx, 1
    sub dx, bx
    jl .out                     ; all off the screen
    inc dx
    mov [wa_fbw], dx            ; the box's width, cells
    shl bx, 1
    mov [wa_fbc], bx            ; ...and its left column x 2
    WA_MINMAX wa_jsy
    or bx, bx
    js .yoff
    cmp dx, WA_HR
    jl .yon
.yoff:
    mov byte [wa_fon], 0        ; above or below: limbs clip
.yon:
    dec bx                      ; (and one higher)
    jns .ct
    xor bx, bx
.ct:
    cmp dx, WA_HR-1
    jle .cb
    mov dx, WA_HR-1
.cb:
    and bl, 0xFE                ; half-rows -> row x 2 (wa_rowo2's index)
    and dl, 0xFE
    cmp bx, dx
    jg .out
    mov [wa_fbr], dx            ; the last row x 2
    mov [wa_frow], bx           ; ...and the first
    mov si, [wa_fiz]            ; the box's columns: is the figure in front
    mov bx, [wa_fbc]            ; of the wall there? (wa_fvis) - and if not
    mov cx, [wa_fbw]            ; in one of them, nothing of it shows: no
    xor dl, dl                  ; limbs, no sweep
.rc:
    mov ax, [wa_colz+bx]
    cmp ax, 0xFFFF
    jne .rz                     ; asked already this frame
    mov di, [wa_cov+bx]
    or di, di
    jz .vis                     ; no wall at all
    mov ax, [wa_spih+di]        ; A BOUND FIRST: a span opens only columns
    mov bp, ax                  ; whose centres lie inside its ends (wa_span),
    add bp, [wa_spsl+di]        ; so wa_wallz's value there lies between its
    cmp ax, bp                  ; ends' inverse depths - and a figure in
    jle .lo                     ; front of both, or behind both, needs no
    xchg ax, bp                 ; divide to say so
.lo:
    cmp bp, si
    jbe .vis                    ; nearer than the whole span
    cmp ax, si
    ja .hid                     ; farther than the whole span
    call wa_wallz               ; (between: the exact depth)
    mov ax, [wa_colz+bx]
.rz:
    cmp ax, si
    ja .hid                     ; the wall there is nearer
.vis:
    mov byte [wa_fvis+bx], 1
    inc dx
    jmp .rn
.hid:
    mov byte [wa_fvis+bx], 0
.rn:
    inc bx
    inc bx
    loop .rc
    cmp byte [wa_hflat], 0      ; heights: from which row a nearer platform
    jne .fl                     ; hides it, per column (wa_fthr)
    call wa_fthrs
.fl:
    or dl, dl
    jz .out
    mov word [wa_flfn], wa_fline
    cmp byte [wa_fon], 0
    je .limbs
    mov word [wa_flfn], wa_flinef
.limbs:
    cmp byte [wa_fgib], 0
    jne .gib
    cmp byte [wa_fshl], 0
    je .limbs0
    call .body                  ; A SHIELD (WEAPONS-PLAN 2.3): the figure
    jmp wa_fchest               ; swept as ever, then its chest recoloured
.body:                          ; (wa_femit pops this and returns)
    push di
.limbs0:
    cmp byte [wa_fsub], 4       ; (J: a small figure's ends alone)
    je .few
    WA_LIMB 2, 4                ; pass 2: the limbs into wa_fbuf (joint x 2
    WA_LIMB 2, 14               ; at each end, tools/arenagen.py's JORDER:
    WA_LIMB 14, 6               ; neck-pelvis, the arms, the legs)
    WA_LIMB 2, 16
    WA_LIMB 16, 8
    WA_LIMB 4, 18
    WA_LIMB 18, 10
    WA_LIMB 4, 20
    WA_LIMB 20, 12
    mov si, [wa_jsx]            ; the head
    mov di, [wa_jsy]
    call wa_fpix
.head:
    cmp word [wa_fv8], 40       ; big enough for a 2x2 head
    jb wa_femit
    inc si
    call wa_fpix
    dec di
    call wa_fpix
    dec si
    call wa_fpix
    jmp wa_femit
.out:
    pop di
    ret
.few:                           ; J: neck-pelvis, neck to each hand, pelvis
    WA_LIMB 2, 4                ; to each foot, straight
    WA_LIMB 2, 6
    WA_LIMB 2, 8
    WA_LIMB 4, 10
    WA_LIMB 4, 12
    jmp .head
.gib:                           ; GIB (WEAPONS-PLAN 2.3): each joint alone,
    mov si, wa_jsx              ; a 2x2 block when near (the head's) - the
.gj:                            ; bits; the joints' box allows for the
    push si                     ; reach, as for the head
    mov di, [si+WA_NJ*2]
    mov si, [si]
    call wa_fpix
    cmp word [wa_fv8], 40
    jb .gn
    inc si
    call wa_fpix
    dec di
    call wa_fpix
    dec si
    call wa_fpix
.gn:
    pop si
    inc si
    inc si
    cmp si, wa_jsx + WA_NJ*2
    jb .gj
    jmp wa_femit
; wa_fchest - a shielded figure's chest (WEAPONS-PLAN 2.3), after its sweep:
; down its neck-pelvis limb a cell row at a time, the cell under the limb
; takes the shield's attribute if the figure drew it this frame (its stamp:
; so whatever hid the figure hides its chest too). No second sweep of the
; box - a few compares a row. Ends wa_figure: pops its DI.
wa_fchest:
    mov si, [wa_jsx+2]          ; (SI, BX) the neck, (CX, DX) the pelvis -
    mov bx, [wa_jsy+2]          ; the upper one first
    mov cx, [wa_jsx+4]
    mov dx, [wa_jsy+4]
    cmp bx, dx
    jle .ord
    xchg si, cx
    xchg bx, dx
.ord:
    sub cx, si                  ; CX = its run, sub-columns
    mov ax, dx
    sub ax, bx                  ; AX = its fall, half-rows (>= 0)
    mov word [wa_thw], 0        ; (the step a cell row, 8.8: 0 for a limb
    cmp ax, 2                   ; under a row)
    jb .go
    push dx
    mov bp, ax
    mov ax, cx
    or ax, ax
    jns .ab
    neg ax
.ab:
    mov dx, bp                  ; (|run| under 64 x fall, or the limb is
    mov cl, 6                   ; all but level - none is: then not
    shl dx, cl                  ; recoloured. So the quotient below is
    cmp ax, dx                  ; under 32768 and the divide cannot fault)
    jae .flat
    mov ax, [wa_jsx+4]
    cmp bx, [wa_jsy+2]
    je .sx
    mov ax, [wa_jsx+2]
.sx:
    sub ax, si                  ; (the run again, signed)
    mov cx, 512
    imul cx                     ; DX:AX = run x 2 half-rows x 256
    idiv bp
    mov [wa_thw], ax
    pop dx
.go:
    mov al, [wa_thw+1]          ; the step's whole part, signed
    cbw
    mov [wa_thb], ax
    mov al, [wa_frame]
    or al, 0x80
    mov [wa_tht], al            ; a figure cell's stamp this frame
    xor cl, cl                  ; CL = x's fraction, SI its whole
    mov ch, [wa_at+A_SHLD]
.row:
    cmp bx, dx
    jg .done
    cmp bx, WA_HR
    jae .next                   ; (unsigned: above or below the view)
    cmp si, [wa_w2]
    jae .next
    mov di, bx
    shl di, 1
    mov di, [wa_hroff+di]
    mov ax, si
    and al, 0xFE                ; its column x 2
    add di, ax                  ; DI = the cell
    mov al, [wa_tht]
    cmp [wa_stamp+di], al
    jne .next                   ; (not the figure's this frame)
    mov [wa_cur+di+1], ch
.next:
    add bx, 2
    add cl, [wa_thw]
    adc si, [wa_thb]
    jmp .row
.flat:
    pop dx
.done:
    pop di
    ret

; wa_femit - pass 3: the box swept with `repe scasw`, a row at a time. Each
; cell the limbs touched is cleared out of wa_fbuf, depth-tested once
; against its column's wall (resolved above) and plotted - the old
; wa_plotf inline, every array addressed off DI (the cell's place in
; wa_fbuf, + 2) by a constant displacement, the stamp to match held in DH,
; the column's verdict in wa_fvis, and the list's end in SI.
WA_FSTO equ wa_stamp - wa_fbuf - 2
WA_FMSO equ wa_fmask - wa_fbuf - 2
WA_FCUO equ wa_cur - wa_fbuf - 2
; FEMIT %1: the sweep, %1 = 0 on a flat arena; 1 with heights, where a
; column's verdict depends on the row - each row first sets wa_fvis from the
; first half-row a nearer platform hides (wa_fthr, wa_fthrs).
%macro FEMIT 1
    mov dh, [wa_frame]
    or dh, 0x80                 ; DH = a figure cell's stamp this frame
    mov si, [wa_newp]
    add si, [wa_newn]           ; SI = the cell list's end
    mov bx, [wa_frow]
%if %1
    mov [wa_fhr], bl            ; (the row's top half-row)
%endif
    mov bp, [wa_rowo2+bx]
    add bp, wa_fbuf             ; BP = the row's first cell in wa_fbuf
    mov ax, [wa_fbr]
    sub ax, bx
    shr ax, 1
    inc ax
    mov [wa_fnr], ax            ; rows
%%row:
%if %1
    mov al, [wa_fhr]            ; (above every partial verdict wa_fvis is
    cmp al, [wa_fthlo]          ; wa_fthrs's own)
    jb %%rv
    call wa_frowv
%%rv:
%endif
    mov di, bp
    add di, [wa_fbc]
    mov cx, [wa_fbw]
    xor ax, ax
    repe scasw                  ; (only a cell's even byte is ever set)
    jne %%found
%%nrow:
    add bp, [wa_w2]             ; (2W bytes: a row)
%if %1
    add byte [wa_fhr], 2
%endif
    dec word [wa_fnr]
    jnz %%row
    sub si, [wa_newp]
    mov [wa_newn], si
    pop di
    ret
%%found:                        ; DI = the cell's offset in wa_fbuf, + 2
    mov dl, 0                   ; DL = its sub-pixels, taken out of the
    xchg dl, [di-2]             ; scratch
    mov bx, di
    sub bx, bp                  ; BX = its column x 2, + 2
    cmp byte [wa_fvis-2+bx], 0
    je %%hid                    ; the wall there is nearer
    mov al, [di+WA_FSTO]
    cmp al, dh
    je %%have                   ; a figure cell already: add the bits
    xor al, dh
    cmp al, 0x80
    je %%listed                 ; a wall cell this frame: already listed
    lea ax, [di-2-wa_fbuf]      ; the cell, onto this frame's list
    mov [si], ax
    inc si
    inc si
%%listed:
    mov [di+WA_FSTO], dh
%%put:
    mov [di+WA_FMSO], dl
    mov bl, dl                  ; (BH is 0: BX was a column)
    mov al, [wa_fglyph+bx]
    mov ah, [wa_fatt]
    mov [di+WA_FCUO], ax
%%hid:
    xor ax, ax
    jcxz %%nrow
    repe scasw
    jne %%found
    jmp %%nrow
%%have:                         ; (a cell two figures share: rarer)
    or dl, [di+WA_FMSO]
    jmp %%put
%endmacro
wa_femit:
    cmp byte [wa_hflat], 0
    je wa_femith
    FEMIT 0
wa_femith:                      ; (heights: docs/plans/WIREARENA-HEIGHTS-PLAN.md
    FEMIT 1                     ; 1.5)

; wa_fthrs - heights, once a figure: per column of its box that is in front
; of the column's tall face (wa_fvis 1), the first half-row a nearer
; platform hides (wa_occlow) into wa_fthr - 0 where nothing of it shows,
; which takes the column out (wa_fvis 0, DL the columns showing less one);
; 0 too where the tall face hid it. SI = the figure's inverse depth.
; Keeps every register but DL.
wa_fthrs:
    push ax
    push bx
    push cx
    mov byte [wa_fthlo], 0xFF   ; (the lowest partial verdict: wa_femith
    mov bx, [wa_fbc]            ; asks wa_frowv nothing above it)
    mov cx, [wa_fbw]
.c:
    xor al, al
    cmp byte [wa_fvis+bx], 0
    je .put
    push cx
    mov cl, 0xFF
    mov ax, si
    call wa_occlow
    mov al, cl
    pop cx
    or al, al
    jz .gone
    cmp al, [wa_fthlo]          ; (0xFF, nothing hidden, never lowers it)
    jae .put
    mov [wa_fthlo], al
    jmp .put
.gone:
    mov byte [wa_fvis+bx], 0
    dec dl
.put:
    mov [wa_fthr+bx], al
    inc bx
    inc bx
    loop .c
    pop cx
    pop bx
    pop ax
    ret

; wa_frowv - heights, each row of a figure's sweep: wa_fvis for the row
; whose top half-row is [wa_fhr] - 1 where the column's platform verdict
; (wa_fthr) hides nothing from there. Keeps every register.
wa_frowv:
    push ax
    push bx
    push cx
    mov al, [wa_fhr]
    mov bx, [wa_fbc]
    mov cx, [wa_fbw]
.c:
    mov ah, 0
    cmp [wa_fthr+bx], al
    jbe .put
    inc ah
.put:
    mov [wa_fvis+bx], ah
    inc bx
    inc bx
    loop .c
    pop cx
    pop bx
    pop ax
    ret

; wa_fjl - wa_figure's joints: SI, DI = keyframes A and B at this facing.
; Each is A + (B - A) x q / 4 (q = [wa_fsub], rounded down as wa_blend's
; was; q = 3 is worked as B + (A - B) / 4, the same number), projected:
; x = ((lateral + fcx) fs8 >> 8) + W, y = hh - ((height - eye) fv8 >> 8).
; Eight loops, one per quarter and per multiply:
; - NARROW (fs8 and fv8 under 128: past ~1.6 m at 40 columns, most of the
;   time): 8-bit multiplies on sums regrouped so they stay in 16 bits -
;   K = fcx fs8 + 256 W split as 256 Khi + Klo, so x = ((lateral fs8 + Klo)
;   >> 8) + Khi; and E = eye fv8 + 256 hh + 255, so y = (E - height fv8)
;   >> 8. The same numbers, so the same pixels;
; - WIDE: the 16-bit multiplies, as before.
; The narrow loops walk keyframe A with LODSW (AL = lateral, AH = height),
; read B a fixed distance on (BX = B - A), blend in BYTES - B - A is a
; signed byte for every pair, which tools/arenagen.py asserts - and STOSW
; the joint's x (DI on wa_jsx, its y 22 bytes on): the same numbers in ~25
; fewer bytes of code a joint. ES = DS (wa_femit's scan needs it too).
; Returns to wa_fjdone. Clobbers AX, BX, CX, DX, SI, DI, BP.
%macro WA_FBLEND 2              ; %1 = q, %2 = 0 lateral, 1 height
  %if %1 == 3
    mov al, [di+bx+%2]          ; (B + (A - B) / 4)
  %else
    mov al, [si+bx+%2]
  %endif
    cbw
  %if %1
    mov dx, ax
    %if %1 == 3
    mov al, [si+bx+%2]
    %else
    mov al, [di+bx+%2]
    %endif
    cbw
    sub ax, dx
    %if %1 != 2
    sar ax, 1
    %endif
    sar ax, 1
    add ax, dx
  %endif
%endmacro
%macro WA_FJ 2                  ; %1 = q, %2 = 1 narrow
.j%2%1:
    WA_FBLEND %1, 0             ; lateral
  %if %2
    imul cl
    add ax, [wa_fk]
    mov al, ah
    cbw
    add ax, bp
  %else
    add ax, [wa_fcx]
    imul word [wa_fs8]
    mov al, ah
    mov ah, dl
    add ax, [wa_w]
  %endif
    mov [wa_jsx+bx], ax
    WA_FBLEND %1, 1             ; height
  %if %2
    mul ch
    neg ax
    add ax, [wa_fe]
    mov al, ah
    cbw
  %else
    sub ax, [wa_feye]
    imul word [wa_fv8]
    mov al, ah
    mov ah, dl
    neg ax
    add ax, [wa_hh]
  %endif
    mov [wa_jsy+bx], ax
    inc bx
    inc bx
    cmp bx, WA_NJ*2
    jb .j%2%1
    jmp wa_fjdone
%endmacro
%macro WA_FJN 3                 ; narrow, %1 = q: SI = A, BX = B - A, DI =
.j1%2:                          ; the joint's place in wa_jsx (%2 the label,
                                ; %3 the joints)
    lodsw                       ; AL, AH = A's lateral and height
  %if %1 == 0
    mov dh, ah
  %else
    mov dx, [si+bx-2]           ; DL, DH = B's
    %if %1 == 3
    sub al, dl                  ; B + (A - B) / 4
    sar al, 1
    sar al, 1
    add al, dl
    sub ah, dh
    sar ah, 1
    sar ah, 1
    add dh, ah                  ; DH = the height
    %else
    sub dl, al                  ; A + (B - A) q / 4
      %if %1 == 1
    sar dl, 1
      %endif
    sar dl, 1
    add al, dl
    sub dh, ah
      %if %1 == 1
    sar dh, 1
      %endif
    sar dh, 1
    add dh, ah                  ; DH = the height
    %endif
  %endif
    imul cl
    add ax, [wa_fk]
    mov al, ah
    cbw
    add ax, bp
    stosw                       ; x
    mov al, dh
    mul ch
    neg ax
    add ax, [wa_fe]
    mov al, ah
    cbw
    mov [di+WA_NJ*2-2], ax      ; y (wa_jsy follows wa_jsx)
    cmp di, wa_jsx+%3*2
    jb .j1%2
  %if %3 < WA_NJ
    mov ax, [wa_jsx]
    mov cx, WA_NJ - %3
    rep stosw
    mov ax, [wa_jsy]
    add di, WA_NJ*2 - (WA_NJ - %3)*2
    mov cx, WA_NJ - %3
    rep stosw
  %endif
    jmp wa_fjdone
%endmacro
wa_fjl:
    ; STKBALANCE-OK: never called - wa_figure's continuation, JMPed to with
    ; wa_figure's DI on the stack; its table dispatch (`jmp ax`) and every
    ; joint block leave by `jmp wa_fjdone`, which pops it
    xor bx, bx
    mov bl, [wa_fsub]
    shl bx, 1
    mov ax, [wa_fs8]
    cmp ax, 128
    jae .wide
    mov dx, [wa_fv8]
    cmp dx, [wa_fvn]            ; (128, or less where the narrow sum would
    jae .wide                   ; leave 16 bits: wa_render - the bound is
    cmp word [wa_feye], 0       ; the camera's eye's, so a figure's own, at
    jl .wide                    ; most that, is under it unless below 0)
    mov cl, al
    mov ch, dl
    imul word [wa_fcx]          ; K = fcx fs8 + 256 W...
    mov bp, [wa_w]
    xchg bp, ax
    mov ah, al
    mov al, 0
    xchg bp, ax
    add ax, bp
    adc dx, 0
    mov [wa_fk], al             ; ...Klo
    mov byte [wa_fk+1], 0
    mov al, ah
    mov ah, dl
    mov bp, ax                  ; ...and Khi
    mov al, ch                  ; E = eye fv8 + 256 hh + 255 (mod 2^16: the
    xor ah, ah                  ; difference it ends in fits)
    mul word [wa_feye]
    mov dh, [wa_hh]
    mov dl, 255
    add ax, dx
    mov [wa_fe], ax
    mov ax, [.tab+8+bx]
    mov bx, di
    sub bx, si                  ; BX = B - A
    mov di, wa_jsx
    jmp ax
.wide:
    and bx, 6                   ; (J's q 4 is q 0 here: all eleven joints)
    mov ax, [.tab+bx]
    xor bx, bx                  ; (BX: the joint x 2)
    jmp ax
.tab:
    dw .j00, .j01, .j02, .j03, .j10, .j11, .j12, .j13, .j1r
    WA_FJ 0, 0
    WA_FJ 1, 0
    WA_FJ 2, 0
    WA_FJ 3, 0
    WA_FJN 0, 0, WA_NJ
    WA_FJN 1, 1, WA_NJ
    WA_FJN 2, 2, WA_NJ
    WA_FJN 3, 3, WA_NJ
    WA_FJN 0, r, 7              ; J: the first seven alone - and the other
                                ; four made the head's, for the box

; WA_FPIXI - SI = sub-column, DI = half-row: one sub-pixel into wa_fbuf,
; clipped (wa_fpix inline, for the clipped walk). Clobbers AX, BX, DX.
%macro WA_FPIXI 0
    cmp si, [wa_w2]
    jae %%out
    cmp di, WA_HR
    jae %%out
    mov bx, si
    shr bx, 1                   ; CF = the column's half
    mov bx, di
    rcl bx, 1
    and bx, 3                   ; BX = the quadrant
    mov dl, [wa_qbit+bx]
    mov bx, di
    shl bx, 1
    mov bx, [wa_hroff+bx]
    mov ax, si
    and al, 0xFE
    add bx, ax
    or [wa_fbuf+bx], dl
%%out:
%endmacro
wa_qbit:    db 1, 2, 4, 8

; wa_fpix - SI = sub-column, DI = half-row: one sub-pixel into wa_fbuf,
; clipped to the view (the head). Keeps SI and DI; clobbers AX, BX, DX.
wa_fpix:
    WA_FPIXI
    ret

; wa_fline - (CX,DX) to (AX,BX), sub-pixels: a limb into wa_fbuf. Clobbers
; AX, BX, CX, DX, SI, DI, BP.
;
; Drawn downward. On the FAST path - both ends' columns on the screen, the
; top end inside it, and the bottom end too unless the line is steep - the
; walk is a state machine: a block per quadrant of the cell the pen is in
; (row parity r, column parity c), each plotting its own bit, so no sub-pixel
; computes an address, a bit or a clip. A steep line running off the bottom
; is cut by its count. Anything else (a limb off the side or the top, or
; shallow and off the bottom: a figure at your shoulder) takes the slow walk,
; which clips each sub-pixel (wa_fpix).
wa_fline:
    cmp bx, dx
    jge .down
    xchg ax, cx
    xchg bx, dx
.down:                          ; (CX,DX) the top end, (AX,BX) the bottom
    mov si, [wa_w2]
    cmp cx, si                  ; (unsigned: a negative one is huge)
    jae .slow
    cmp ax, si
    jae .slow
    cmp dx, WA_HR
    jae .slow
    cmp bx, WA_HR
    jb .fast
    mov si, ax                  ; the bottom end is off the screen: fast
    sub si, cx                  ; only when steep, where the count cuts it
    jns .ab
    neg si
.ab:
    mov bp, bx
    sub bp, dx
    cmp si, bp
    jbe .fast
.slow:                          ; the same walk, each sub-pixel clipped
    mov si, cx                  ; SI, DI = the pen
    mov di, dx
    sub ax, cx                  ; AX = |dx|, [wa_lsx] its sign
    mov word [wa_lsx], 1
    jns .sxp
    neg ax
    mov word [wa_lsx], -1
.sxp:
    sub bx, dx                  ; BX = dy, >= 0
    cmp ax, bx
    jg .sx
    mov [wa_ldx], ax            ; steep: [wa_ldx] the minor, [wa_ldy] the
    mov [wa_ldy], bx            ; major
    mov cx, bx
    inc cx
    mov bp, bx
    shr bp, 1
.sy:
    WA_FPIXI
    dec cx
    jz .ret
    inc di
    cmp di, WA_HR
    jge .ret                    ; (below the view: nothing more to see)
    sub bp, [wa_ldx]
    jns .sy
    add bp, [wa_ldy]
    add si, [wa_lsx]
    jmp .sy
.sx:
    mov [wa_ldx], ax            ; shallow: [wa_ldx] the major, [wa_ldy] the
    mov [wa_ldy], bx            ; minor
    mov cx, ax
    inc cx
    mov bp, ax
    shr bp, 1
.sxl:
    WA_FPIXI
    dec cx
    jz .ret
    add si, [wa_lsx]
    sub bp, [wa_ldy]
    jns .sxl
    add bp, [wa_ldx]
    inc di
    cmp di, WA_HR
    jl .sxl
.ret:
    ret

.fast:
    mov bp, WA_HR-1
    sub bp, dx                  ; BP = the most half-rows a steep one steps
    jmp wa_flgo

; wa_flinef - wa_fline for a figure whose joints are ALL on the screen
; (wa_figure decides, once): every limb would take the fast walk, so its
; clip tests are skipped, and a steep one cannot run off the bottom - the
; cut is set out of reach. The same sub-pixels.
wa_flinef:
    cmp bx, dx
    jge .down
    xchg ax, cx
    xchg bx, dx
.down:
    mov bp, 0xFFFF              ; (no cut)
wa_flgo:
    sub ax, cx                  ; AX = x1 - x0
    sub bx, dx                  ; BX = y1 - y0, >= 0
    mov di, dx
    shl di, 1
    mov di, [wa_hrfb+di]
    and dx, 1                   ; DX = (r x 2 + c) x 2, the state's entry
    shr cx, 1
    rcl dx, 1
    shl dx, 1
    shl cx, 1
    add di, cx                  ; DI = the start's cell in wa_fbuf
    or ax, ax
    jns .r
    neg ax                      ; leftward: the second half of each table
    add dx, 8
.r:
    mov si, dx
    cmp ax, bx
    jg .shallow
    mov dx, [wa_fl_ytab+si]     ; steep: a sub-pixel every half-row
    mov cx, bx
    cmp cx, bp
    ja .ycut                    ; (off the bottom: out of line)
.yc:
    inc cx                      ; CX = sub-pixels
    mov si, ax                  ; SI = the minor step...
    mov bp, bx
    shr bp, 1                   ; BP = the error, half the major...
    xchg bx, di                 ; BX = the cell, DI = the major
    mov ax, [wa_w2]             ; AX = a row's bytes (2W)
.walky:
    jmp dx
.ycut:
    mov cx, bp                  ; stop at the edge
    jmp .yc
.shallow:
    mov dx, [wa_fl_xtab+si]     ; shallow: a sub-pixel every sub-column
    mov cx, ax
    inc cx
    mov si, bx                  ; SI = the minor step (dy)
    mov bp, ax
    shr bp, 1
    xchg ax, di                 ; DI = the major (dx), AX = the cell
    xchg ax, bx                 ; BX = the cell
    mov ax, [wa_w2]
.walkx:
    jmp dx
; The fast walk's states. Registers: BX = the cell in wa_fbuf, CX =
; sub-pixels left, BP = the error, SI = the minor step, DI = the major, AX =
; a row's bytes. A block plots its quadrant's bit and moves the pen: a
; half-row down from a bottom quadrant is the next row's top (BX += AX), a
; sub-column right from a right quadrant is the next cell's left (BX += 2).
%macro WA_FYS 5                 ; steep: %1 R/L, %2 r, %3 c, %4 the state
wa_fy%1%2%3:                    ; after a sideways step too, %5 without
    or byte [bx], 1 << (%3 + 2*%2)
    dec cx
    jz wa_fy%1ret
    sub bp, si
  %if %2 == 0
    jns %5
  %else
    jns %%down
  %endif
    add bp, di
  %ifidn %1, R
    %if %3 == 1
    inc bx
    inc bx
    %endif
  %else
    %if %3 == 0
    dec bx
    dec bx
    %endif
  %endif
  %if %2 == 1
    add bx, ax
  %endif
    jmp %4
  %if %2 == 1
%%down:
    add bx, ax
    jmp %5
  %endif
%endmacro
%macro WA_FXS 5                 ; shallow: %4 the state after a step down
wa_fx%1%2%3:                    ; too, %5 without
    or byte [bx], 1 << (%3 + 2*%2)
    dec cx
    jz wa_fx%1ret
  %ifidn %1, R
    %if %3 == 1
    inc bx
    inc bx
    %endif
  %else
    %if %3 == 0
    dec bx
    dec bx
    %endif
  %endif
    sub bp, si
    jns %5
    add bp, di
  %if %2 == 1
    add bx, ax
  %endif
    jmp %4
%endmacro
wa_fyRret:
    ret
    WA_FYS R, 0, 0, wa_fyR11, wa_fyR10
    WA_FYS R, 0, 1, wa_fyR10, wa_fyR11
    WA_FYS R, 1, 0, wa_fyR01, wa_fyR00
    WA_FYS R, 1, 1, wa_fyR00, wa_fyR01
wa_fyLret:
    ret
    WA_FYS L, 0, 0, wa_fyL11, wa_fyL10
    WA_FYS L, 0, 1, wa_fyL10, wa_fyL11
    WA_FYS L, 1, 0, wa_fyL01, wa_fyL00
    WA_FYS L, 1, 1, wa_fyL00, wa_fyL01
wa_fxRret:
    ret
    WA_FXS R, 0, 0, wa_fxR11, wa_fxR01
    WA_FXS R, 0, 1, wa_fxR10, wa_fxR00
    WA_FXS R, 1, 0, wa_fxR01, wa_fxR11
    WA_FXS R, 1, 1, wa_fxR00, wa_fxR10
wa_fxLret:
    ret
    WA_FXS L, 0, 0, wa_fxL11, wa_fxL01
    WA_FXS L, 0, 1, wa_fxL10, wa_fxL00
    WA_FXS L, 1, 0, wa_fxL01, wa_fxL11
    WA_FXS L, 1, 1, wa_fxL00, wa_fxL10
wa_fl_ytab:                     ; by (r x 2 + c) x 2, leftward + 8
    dw wa_fyR00, wa_fyR01, wa_fyR10, wa_fyR11
    dw wa_fyL00, wa_fyL01, wa_fyL10, wa_fyL11
wa_fl_xtab:
    dw wa_fxR00, wa_fxR01, wa_fxR10, wa_fxR11
    dw wa_fxL00, wa_fxL01, wa_fxL10, wa_fxL11

; wa_marker - DI = a figure too far for limbs: a one-sub-column bar from
; head to feet, its attribute already chosen. The trigger's extents too.
wa_marker:
    mov ax, [wa_fcx]
    imul word [wa_fs8]
    mov al, ah
    mov ah, dl
    add ax, [wa_w]
    mov [di+F_PCX], ax
    mov si, ax                  ; SI = the sub-column
    mov ax, 112                 ; the head, 1.75 m
    sub ax, [wa_feye]
    imul word [wa_fv8]
    mov al, ah
    mov ah, dl
    neg ax
    add ax, [wa_hh]
    mov [di+F_PTOP], ax
    mov cx, ax                  ; CX = top half-row
    mov ax, [wa_feye]           ; the feet, the floor
    imul word [wa_fv8]
    mov al, ah
    mov ah, dl
    add ax, [wa_hh]
    mov [di+F_PBOT], ax
    mov [wa_mbot], ax
    mov word [di+F_PWH], 1
    cmp si, [wa_w2]
    jae .out                    ; (unsigned: off either side)
    mov bx, si
    and bx, 0xFFFE              ; BX = its column x 2: ONE verdict for
    mov ax, [wa_fiz]            ; the whole bar
    call wa_occl
    or al, al
    jz .out                     ; the wall there is nearer
    xor ah, ah                  ; (a platform: the bar ends above its band)
    dec ax
    cmp ax, WA_HR-1
    jle .ok
    mov ax, WA_HR-1
.ok:
    cmp ax, [wa_mbot]
    jle .bot
    mov ax, [wa_mbot]
.bot:
    mov [wa_mbot], ax           ; the last half-row, clipped
    or cx, cx
    jns .top
    xor cx, cx
.top:
    and si, 1                   ; SI = the sub-column's half of the cell
    push di
    mov di, bx                  ; DI = the column x 2
.cell:                          ; a cell at a time, both of its half-rows
    cmp cx, [wa_mbot]           ; at once if both are in the bar
    jg .done
    mov bx, cx
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, di                  ; BX = the cell
    mov dl, 4                   ; a lower half-row: its lower quadrant
    inc cx
    test cl, 1
    jz .half                    ; (it was odd)
    mov dl, 1                   ; an upper one: and the lower too, if the
    cmp cx, [wa_mbot]           ; bar goes on
    jg .half
    mov dl, 5
    inc cx
.half:
    or si, si
    jz .put
    shl dl, 1                   ; the right-hand quadrants
.put:
    mov ah, [wa_fatt]
    call wa_plotf
    jmp .cell
.done:
    pop di
.out:
    ret

; --- pickups, rockets, explosions (WEAPONS-PLAN 2.4, 2.5) -----------------
; Each a BILLBOARD through the figures' sub-pixel layer (wa_plotf): a box
; facing the eye, so its shape is a half width and a band of heights. A
; pickup costs nothing unless it is present and the player's PVS cell can
; see it (wa_pkvp, wa_order's); a drawn one is one transform, two divides,
; and per column it covers one wa_occl - the far marker's verdict - and a
; plot a cell.
wa_pkall:   db 0xFF             ; (no PVS carried: every pickup tested)
; per pickup kind: half width, from, to (units above its floor), colour and
; mono attributes. Lines for the weapons, blocks for the packs; on mono the
; proportions tell them apart
wa_pkshape:
    dw 24, 4, 10                ; shotgun: a thin line, brown
    db 0x06, 0x0F
    dw 28, 4, 14                ; chaingun: a little longer and thicker, grey
    db 0x07, 0x0F
    dw 20, 4, 16                ; grenade launcher: short and thick, magenta
    db 0x0D, 0x0F
    dw 32, 4, 20                ; rocket launcher: longer and thicker, green
    db 0x0A, 0x0F
    dw 40, 4, 8                 ; railgun: the longest and thinnest, cyan
    db 0x0B, 0x0F
    dw 16, 0, 28                ; health: a square block, light red
    db 0x0C, 0x0F
    dw 12, 0, 44                ; shield: a tall block, yellow
    db 0x0E, 0x0F
WA_PKSH equ 8

wa_pickups:
    cmp word [wa_npk], 0
    je .out
    mov si, [wa_pkvp]
    mov al, [si]                ; the ones this cell may see...
    and al, [wa_pkon]           ; ...that are there
    jz .out
    mov [wa_pkm], al
    mov si, wa_pks
.p:
    shr byte [wa_pkm], 1
    jnc .next
    push si
    mov bl, [si+6]              ; its kind's shape
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, wa_pkshape
    call wa_thshape
    mov ax, [si]
    mov cx, [si+4]
    mov bx, [si+2]
    call wa_thing
    pop si
.next:
    add si, WA_PKSZ
    cmp byte [wa_pkm], 0
    jne .p
.out:
    ret

; wa_thshape - BX = a shape row (wa_pkshape's): into wa_thw..wa_tha, the
; attribute the adapter's. Keeps all but BX
wa_thshape:
    push ax
    mov ax, [bx]
    mov [wa_thw], ax
    mov ax, [bx+2]
    mov [wa_th0], ax
    mov ax, [bx+4]
    mov [wa_th1], ax
    mov al, [bx+6]
    cmp byte [wa_mono], 0
    je .c
    mov al, [bx+7]
.c:
    mov [wa_tha], al
    pop ax
    ret

; wa_rockets - after the figures: each rocket a bright dot, each explosion a
; block growing over its ticks, yellow and red by turns
wa_rockets:
    mov si, wa_rks
.r:
    mov al, [si+WA_RKST]
    or al, al
    jz .n
    push si
    mov bx, wa_rkshape
    cmp al, WA_RKFLY
    je .shape
    mov ah, WA_RKBOOM           ; an explosion: e = its ticks so far
    sub ah, al
    mov bl, ah
    shl bl, 1
    add bl, ah
    shl bl, 1                   ; (6e)
    xor bh, bh
    add bx, 12                  ; half width 12 + 6e, as tall either way
    mov [wa_thw], bx
    mov [wa_th1], bx
    neg bx
    mov [wa_th0], bx
    mov bx, (0x0C << 8) | 0x07  ; (red, or dim on mono)
    test al, 1
    jz .att
    mov bx, (0x0E << 8) | 0x0F  ; (yellow, or bright)
.att:
    mov al, bh
    cmp byte [wa_mono], 0
    je .a2
    mov al, bl
.a2:
    mov [wa_tha], al
    jmp .draw
.shape:
    cmp byte [si+WA_RKTYP], 0
    je .rk
    mov bx, wa_gnshape
.rk:
    call wa_thshape
.draw:
    mov ax, [si+WA_RKX]
    mov bx, [si+WA_RKY]
    mov cx, [si+WA_RKZ]
    call wa_thing
    pop si
.n:
    add si, WA_RKSZ
    cmp si, wa_rks + WA_NRK*WA_RKSZ
    jb .r
    ret
wa_rkshape:
    dw 6, -5, 5                 ; a rocket in flight: a bright dot
    db 0x0F, 0x0F
wa_gnshape:
    dw 5, 0, 8                  ; a grenade: a magenta dot on what it rolls on
    db 0x0D, 0x0F

; wa_thing - AX = x, BX = y, CX = z (its floor, or its middle), its shape in
; wa_thw..wa_tha: drawn, behind what is nearer per column (wa_occl).
; Clobbers every register.
wa_thing:
    mov [wa_thz], cx
    sub ax, [wa_px]             ; wa_xform's sums, z first: one behind the
    sub bx, [wa_py]             ; eye (half of them, turning round) costs
    mov [wa_thdx], ax           ; two multiplies, not four
    mov [wa_thdy], bx
    imul word [wa_cos]          ; z = (dx cos + dy sin) >> 14
    mov cx, ax
    mov si, dx
    mov ax, bx
    imul word [wa_sinv]
    add ax, cx
    adc dx, si
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    cmp dx, 24
    jl .out                     ; (at the eye, or behind it)
    mov di, dx
    mov ax, [wa_thdx]           ; x = (dx sin - dy cos) >> 14
    imul word [wa_sinv]
    mov cx, ax
    mov si, dx
    mov ax, [wa_thdy]
    imul word [wa_cos]
    sub cx, ax
    sbb si, dx
    shl cx, 1
    rcl si, 1
    shl cx, 1
    rcl si, 1
    mov ax, si
    mov dx, di                  ; AX = camera x, DX = z
    mov bx, ax
    or bx, bx
    jns .ax
    neg bx
.ax:
    mov cx, dx
    add cx, [wa_thw]
    cmp bx, cx
    jg .out                     ; outside 90 degrees, with its width
    cmp dx, WA_ZMAX
    jae .out
    mov bx, dx
    shl bx, 1
    mov bx, [wa_rcp+bx-WA_NEAR*2]
    mov [wa_thiz], bx           ; its inverse depth, 0x7FFF0 / z: so the
    mov [wa_thdx], ax           ; scales at its depth are multiplies, not
    mov ax, [wa_wf]             ; divides - wf / z = wf iz >> 19, to the
    mul bx                      ; unit's 1/65536 (wa_thdx: camera x now)
    mov cl, 3
    shr dx, cl
    mov [wa_thfs], dx           ; sub-columns a unit, 8.8
    mov ax, [wa_kyh]
    mul bx
    shr dx, cl
    mov di, dx                  ; DI = half-rows a unit, 8.8
    mov ax, [wa_thdx]
    imul word [wa_thfs]
    mov al, ah
    mov ah, dl
    add ax, [wa_w]
    mov si, ax                  ; SI = its middle's sub-column
    mov ax, [wa_thw]
    mul word [wa_thfs]
    mov al, ah
    mov ah, dl                  ; AX = its half width, sub-columns
    mov bx, si
    mov dx, si
    or ax, ax
    jz .w
    sub bx, ax
    add dx, ax
    dec dx
.w:
    or bx, bx
    jns .l
    xor bx, bx
.l:
    mov ax, [wa_w2]
    dec ax
    cmp dx, ax
    jle .r
    mov dx, ax
.r:
    cmp bx, dx
    jg .out
    mov [wa_thl0], bx
    mov [wa_thr0], dx
    mov ax, [wa_thz]            ; the top: hh + (eye - (z + to)) fv8
    add ax, [wa_th1]
    call .hrow
    mov [wa_tht], ax
    mov ax, [wa_thz]            ; the bottom: the half-row above (z + from)
    add ax, [wa_th0]
    call .hrow
    dec ax
    cmp ax, [wa_tht]
    jge .b
    mov ax, [wa_tht]            ; (at least one half-row)
.b:
    cmp ax, WA_HR-1
    jle .b1
    mov ax, WA_HR-1
.b1:
    mov [wa_thb], ax
    cmp word [wa_tht], 0
    jge .t
    mov word [wa_tht], 0
.t:
    mov ax, [wa_tht]
    cmp ax, [wa_thb]
    jg .out
.cols:
    mov bp, [wa_thl0]
    and bp, 0xFFFE              ; BP = the column x 2
.col:
    cmp bp, [wa_thr0]
    jg .out
    mov bx, bp
    mov ax, [wa_thiz]
    call wa_thocc               ; AL = the first half-row hidden there
.occd:
    xor ah, ah
    dec ax
    cmp ax, [wa_thb]
    jle .lim
    mov ax, [wa_thb]
.lim:
    mov [wa_thl], ax
    xor dl, dl                  ; the quadrants of this column it covers:
    cmp bp, [wa_thl0]           ; the left half's (bits 0 and 2), the
    jl .hr                      ; right's (1 and 3)
    or dl, 5
.hr:
    mov ax, bp
    inc ax
    cmp ax, [wa_thr0]
    jg .hd
    or dl, 10
.hd:
    mov [wa_thhb], dl
    mov cx, [wa_tht]
.cell:
    cmp cx, [wa_thl]
    jg .ncol
    mov bx, cx
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, bp                  ; BX = the cell
    test cl, 1
    jnz .lo
    mov dl, 3                   ; its upper half-row...
    inc cx
    cmp cx, [wa_thl]
    jg .put
    mov dl, 15                  ; ...and its lower
    inc cx
    jmp .put
.lo:
    mov dl, 12
    inc cx
.put:
    and dl, [wa_thhb]
    mov ah, [wa_tha]
    call wa_plotf
    jmp .cell
.ncol:
    inc bp
    inc bp
    jmp .col
.out:
    ret
.hrow:                          ; AX = a height -> the half-row it projects
    neg ax                      ; to at fv8 = DI: hh + (eye - h) fv8 >> 8
    add ax, [wa_eye]
    imul di
    mov al, ah
    mov ah, dl
    add ax, [wa_hh]
    ret

; wa_thocc - BX = a column x 2, AX = an inverse depth: wa_occl's answer (AL
; = the first half-row something nearer hides there, 0 all of it, 0xFF
; none), with the figures' BOUND first (wa_figure): the column's tall face
; lies between its span's ends' inverse depths, so a thing nearer than both,
; or farther than both, needs no divide to say so. Keeps all but AL
wa_thocc:
    push dx
    push si
    push di
    mov dx, [wa_colz+bx]
    cmp dx, 0xFFFF
    jne .have                   ; (asked already this frame)
    mov di, [wa_cov+bx]
    or di, di
    jz .open                    ; no tall face there at all
    mov dx, [wa_spih+di]
    mov si, dx
    add si, [wa_spsl+di]
    cmp dx, si
    jle .lo
    xchg dx, si                 ; DX = the ends' least, SI their most
.lo:
    cmp si, ax
    jbe .open                   ; nearer than the whole span
    cmp dx, ax
    ja .hid                     ; farther than the whole span
    call wa_wallz               ; (between: the exact depth)
    mov dx, [wa_colz+bx]
.have:
    cmp dx, ax
    ja .hid                     ; the tall face there is nearer
.open:
    push cx                     ; nothing tall: what a platform hides
    mov cl, 0xFF
    cmp byte [wa_hflat], 0
    jne .flat
    call wa_occlow
.flat:
    mov al, cl
    pop cx
    jmp .out
.hid:
    xor al, al
.out:
    pop di
    pop si
    pop dx
    ret

; --- the overlay (WEAPONS-PLAN 2.5) ------------------------------------------
; The game's cells - the weapon in front of the player, the health, the
; ammunition - over the view. They change only when the game says so
; (wa_ovfresh), so they reach the glass ONCE: a fresh overlay lists every
; cell (and puts back the last one's cells it no longer has, blank where the
; view drew nothing), and on any other frame a cell is only stamped - so
; present's old list never blanks it - and given its glyph again where the
; view drew under it this frame (its stamp already says so, and the cell is
; listed). Present compares none of the rest: the glass holds them.
WA_OVGD equ wa_ovg - wa_ov - 2  ; (a cell's glyph, off SI past its offset)
wa_overlay:
    mov dl, [wa_frame]
    mov dh, dl
    or dh, 0x80                 ; DL, DH: this frame's stamps, wall, figure
    cmp byte [wa_ovfresh], 0
    jne .fresh
    mov cx, [wa_ovn]
    jcxz .out
    mov si, wa_ov
.c:
    lodsw
    xchg ax, bx
    mov al, [wa_stamp+bx]
    and al, 0x7F
    cmp al, dl
    je .put                     ; the view drew here: listed, ours again
    mov [wa_stamp+bx], dl       ; (kept off present's old list's blanks)
    loop .c
    ret
.put:
    mov ax, [si+WA_OVGD]
    mov [wa_cur+bx], ax
    loop .c
.out:
    ret
.fresh:
    mov cx, [wa_ovoldn]         ; the last overlay's cells, back to the view
    mov si, wa_ovold
.o:
    jcxz .new
    lodsw
    xchg ax, bx
    cmp [wa_stamp+bx], dl
    je .on
    cmp [wa_stamp+bx], dh
    je .on
    mov [wa_stamp+bx], dl
    call wa_list
    mov word [wa_cur+bx], WA_BLANK
.on:
    dec cx
    jmp .o
.new:
    mov cx, [wa_ovn]            ; ...then this one's, every cell listed
    mov [wa_ovoldn], cx
    mov si, wa_ov
    mov di, wa_ovold
.n:
    jcxz .done
    lodsw
    mov [di], ax
    inc di
    inc di
    xchg ax, bx
    mov ax, [si+WA_OVGD]
    cmp [wa_stamp+bx], dl
    je .nput
    cmp [wa_stamp+bx], dh
    je .nput
    mov [wa_stamp+bx], dl
    call wa_list
.nput:
    mov [wa_cur+bx], ax
    dec cx
    jmp .n
.done:
    mov byte [wa_ovfresh], 0
    ret

; --- the crosshair ----------------------------------------------------------
wa_cross:
    mov bx, [wa_w]              ; column W/2, as an offset: W
    add bx, [wa_rowo2+WA_ROWS]  ; row 12 (WA_ROWS/2 rows x 2 bytes in)
    mov al, '+'
    mov ah, [wa_at+A_CROSS]
    cmp byte [wa_mflash], 0
    je .put
    mov al, '*'
    mov ah, [wa_at+A_FLASH]
.put:
    mov dl, [wa_stamp+bx]
    and dl, 0x7F
    cmp dl, [wa_frame]
    je .over
    mov dl, [wa_frame]
    mov [wa_stamp+bx], dl
    call wa_list
.over:
    mov [wa_cur+bx], ax
    ret

; --- present ---------------------------------------------------------------
; Write this frame's cells that differ from the glass; blank last frame's
; cells this frame did not draw. Nothing else. The glass is compared against
; itself, in text VRAM: a RAM copy of it (wa_vis, 3,840 bytes of BSS) cost a
; second write for every cell written and bought nothing - a VRAM read is a
; RAM read and a few wait states on MartyPC. (On a real IBM CGA at 80
; columns a read snows like a write does: this reads every listed cell once.
; 40 columns, Hercules and EGA/VGA text do not snow.) Each list is walked four cells
; an iteration, entered part-way for the remainder (a jump table, once), so
; the loop's own cost is a quarter of a `loop` a cell.
; - A new cell's usual case in play is CHANGED (the view moves: two in three
;   on the P bench), so the write falls through and the unchanged one jumps.
; - An old cell's usual case is DRAWN AGAIN (85 of 121 on the P bench), and
;   by a wall, so that is one compare against the wall's stamp falling
;   through; a figure's stamp and the blank are out of line. It is written without
;   asking whether the glass is blank already: an old-list cell was drawn last
;   frame, so it is not, and if it were, the write is a no-op.
%macro PNEW 0                   ; this frame's cell: written if it differs
    lodsw
    xchg bx, ax
    mov ax, [bx+di]             ; (DI = wa_cur)
    cmp ax, [es:bx]
    je %%same
    mov [es:bx], ax
    inc bp
%%same:
%endmacro
%macro POLD 1                   ; last frame's: blanked unless drawn again
    lodsw                       ; (DL = this frame, DH = a figure cell's
    xchg bx, ax                 ; stamp this frame, DI = wa_stamp)
    cmp [bx+di], dl             ; (a wall's cell drawn again: the usual
    jne .ob%1                   ; case, one compare)
.or%1:
%endmacro
%macro POLDB 1                  ; ...a figure's, or its blank, out of line
.ob%1:
    cmp [bx+di], dh
    je .or%1
    mov word [es:bx], WA_BLANK
    inc bp
    jmp .or%1
%endmacro
wa_present:
    push es
    mov es, [wa_vseg]
    xor bp, bp                  ; writes, for the meter
    mov si, [wa_newp]
    mov di, wa_cur
    mov cx, [wa_newn]           ; bytes: 2 a cell
    mov bx, cx
    and bx, 6                   ; (cells mod 4) x 2
    add cx, 6
    shr cx, 1
    shr cx, 1
    shr cx, 1                   ; groups of four, the first one partial
    jcxz .old
    jmp [.ntab+bx]
.n0:
    PNEW
.n3:
    PNEW
.n2:
    PNEW
.n1:
    PNEW
    loop .n0
.old:
    mov si, [wa_oldp]
    mov cx, [wa_oldn]
    mov dl, [wa_frame]
    mov dh, dl
    or dh, 0x80
    mov di, wa_stamp
    mov bx, cx
    and bx, 6
    add cx, 6
    shr cx, 1
    shr cx, 1
    shr cx, 1
    jcxz .swap
    jmp [.otab+bx]
.o0:
    POLD 0
.o3:
    POLD 3
.o2:
    POLD 2
.o1:
    POLD 1
    loop .o0
.swap:
    mov [wa_nwr], bp
    mov ax, [wa_newp]
    xchg ax, [wa_oldp]
    mov [wa_newp], ax
    mov ax, [wa_newn]
    mov [wa_oldn], ax
    pop es
    ret
    POLDB 0
    POLDB 3
    POLDB 2
    POLDB 1
.ntab:  dw .n0, .n1, .n2, .n3
.otab:  dw .o0, .o1, .o2, .o3
