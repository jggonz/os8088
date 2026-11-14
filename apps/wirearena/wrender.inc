; WireArena's renderer (docs/plans/MULTIPLAYER-FPS-PLAN.md sections 4, 16).
;
; 1. The arena's walls are the faces of boxes in a host-built BSP
;    (tools/arenagen.py), walked FRONT TO BACK (wa_order: the list stands
;    while the player stays in a cell of the tree's lines). A box is its
;    SILHOUETTE - the two or three vertices of the faces seen from where the
;    player stands (wa_sil) - tested against the view on those vertices, with
;    a one-corner hint while it stays out (wa_render's walk). A vertex is two
;    sums of per-coordinate products made when the view moves (wa_products -
;    the walls are axis-aligned) and a column; a face with an end outside the
;    view takes an end at the screen's edge in its place (wa_vend), and is
;    then STEPPED across its columns. While the view stands, the products,
;    the vertices and the columns all do.
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
WA_VAR wa_drz, 4                ; the floor grid (wa_dots): a line's first
WA_VAR wa_drx, 4                ;   dot's sums, Z and XK, 32 bits
WA_VAR wa_dxz, 4                ;   the step a line on in x adds to each...
WA_VAR wa_dxx, 4
WA_VAR wa_dyz, 2                ;   ...and a dot on in y (Z x 32, XK x 2)
WA_VAR wa_dyx, 2
WA_VAR wa_dms, 2                ;   sin k16 >> 10, cos k16 >> 10
WA_VAR wa_dmc, 2
WA_VAR wa_dwh, 2                ;   W / 2
WA_VAR wa_dnx, 1                ;   lines left in x, dots in a line,
WA_VAR wa_dny, 1                ;   dots left in this one
WA_VAR wa_dcnt, 1
WA_VAR wa_dsh, 1                ;   the row's shift
WA_VAR wa_dlim, 1               ;   the first hidden half-row in the column
WA_VAR wa_vt0, 2
WA_VAR wa_stl, 2
WA_VAR wa_sbl, 2
WA_VAR wa_vbl, 2
WA_VAR wa_vb0, 2
WA_VAR wa_afix, 1               ; the span's shade is one band
WA_VAR wa_fclp, 2               ; wa_fclpc: 4 the clip-tested passes, else 0
WA_VAR wa_p2i, 2                ; ...and pass 2's (4 too for a thin wall's)
WA_VAR wa_fst, 2                ; a clip-tested pass's step (its fraction)
WA_VAR wa_cflat, 1              ; wa_cclip holds a flat arena's (wa_collook)
WA_VAR wa_tup, 1                ; SPANLOOP: the top moves up the screen or
WA_VAR wa_bdn, 1                ; not at all, the bottom down (wa_span.gen1:
                                ; ONE word's store - keep the two together)
WA_VAR wa_lvb, 2                ; wa_flat's LEVEL test: 7 while an edge that
WA_VAR wa_lvt, 2                ; moves under a half-row is drawn level
                                ; (LK_COARSE), else 0 - the bottom's and the
                                ; top's (0 too where the top is not drawn:
                                ; the sparse level, ROOMF) - wa_collook
WA_VAR wa_roomf, 1              ; ROOMF: non-zero while the room's faces are
                                ; drawn at a level with it (wa_render)
WA_VAR wa_vtp, 2                ; wa_vte or wa_vtr: the level's (wa_lookset)
WA_VAR wa_klook, 1              ; ...the wa_look it was chosen for
WA_VAR wa_kspa, 1               ; ...and the wa_sparse (wa_collook's)
WA_VAR wa_fspp, 2               ; wa_fspan, or heights' wa_fspanh (wa_face)
WA_VAR wa_mkp, 2                ; wa_face.mk and .miz, or .mk1 and .miz1
WA_VAR wa_mizp, 2               ; (wa_render)
WA_VAR wa_numh, 2               ; .mk1's: wa_numt + wa_numb
WA_VAR wa_cend, 2               ; the first column x 16 off the right (wa_render)
WA_POSTMIN equ 8                ; NOPOST: the shortest wall with posts
WA_VAR wa_left, 2               ; columns still open
WA_VAR wa_lp, 2                 ; the list's write pointer across spans
WA_VAR wa_xc, WA_MXV*4          ; per frame: (x - px) cos and sin, 32 bits,
WA_VAR wa_xs, WA_MXV*4          ; one per distinct x of the arena...
WA_VAR wa_yc, WA_MYV*4          ; ...and per distinct y: a vertex is two
WA_VAR wa_ys, WA_MYV*4          ; 32-bit adds, not four multiplies
WA_VAR wa_vst, WA_MVTX*2        ; a vertex's cache stamp (the frame number)
WA_VAR wa_vok, WA_MVTX*2+6      ; 1: inside the view, projected below (the
                                ; scratch ends' too: 3, always - wa_geominit)
WA_VAR wa_vx, WA_MVTX*2         ; camera space
WA_VAR wa_vz, WA_MVTX*2
WA_VS1 equ WA_MVTX*2            ; ...and three more of these four: a face's
WA_VS2 equ WA_MVTX*2+2          ; ends at the screen's edges (wa_vend), and
WA_VS3 equ WA_MVTX*2+4          ; a clipped end (wa_face.end)
WA_VAR wa_vpc, WA_MVTX*2+6      ; projected: column x16, top, bottom,
WA_VAR wa_vpt, WA_MVTX*2+6      ; inverse depth
WA_VAR wa_vpb, WA_MVTX*2+6
WA_VAR wa_vpi, WA_MVTX*2+6
WA_VAR wa_vu, 2                 ; 1 - c16 / k16, x 65536, for the projection
WA_VAR wa_vuk, 4                ; (k16, c16) in wa_vuk (wa_render)
WA_VAR wa_fv, 2
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
wa_cos  equ wa_fcs              ; the camera's cos and sin: the frame
wa_sinv equ wa_fsn              ; block's (the game's wa_sincos)
WA_VAR wa_cang, 2
WA_VAR wa_hq, 2                 ; horizon, quarter half-rows
WA_VAR wa_hh, 2                 ; horizon, half-rows
WA_VAR wa_numt, 2
WA_VAR wa_numb, 2
WA_VAR wa_x1, 2
WA_VAR wa_z1, 2
WA_VAR wa_x2, 2
WA_VAR wa_z2, 2
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
WA_VAR wa_hroff, WA_HR*2         ; a half-row's cell row offset
WA_VAR wa_hrfb, WA_HR*2          ; ...and its row's place in wa_fbuf
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
WA_VAR wa_thw, 2                ; wa_thing's shape: half width, from and
WA_VAR wa_th0, 2                ;   to above its z, units; attribute
WA_VAR wa_th1, 2
WA_VAR wa_tha, 1
%if wa_th0 != wa_thw + 2 || wa_th1 != wa_thw + 4 || wa_tha != wa_thw + 6
    %error "wa_thshape copies a shape row over wa_thw, wa_th0, wa_th1, wa_tha"
%endif
WA_VAR wa_thhb, 1               ;   a column's quadrant bits
WA_VAR wa_thz, 2                ;   its z
WA_VAR wa_thdx, 2               ;   its offset from the eye, x (wa_view)
WA_VAR wa_ovoldn, 2             ; the overlay's cells as it last came in:
WA_VAR wa_ovold, WA_MOV*2       ;   how many, and each's stamp's place
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
WA_VAR wa_ldx, 2
WA_VAR wa_ldy, 2
WA_VAR wa_lsx, 2
WA_VAR wa_lsy, 2
WA_VAR wa_fbw, 2                ; wa_figure's box: width in cells,
WA_VAR wa_fbc, 2                ;   left column x 2, last row x 2
WA_VAR wa_fbr, 2
WA_VAR wa_frow, 2
WA_VAR wa_fnr, 2                ;   rows left to sweep
WA_VAR wa_fvis, WA_MAXW*2       ;   per column x 2: 0, or the first half-row
                                ;   hidden there + 1 (all 0 between figures)
WA_VAR wa_fnh, 1                ;   its columns with something hidden
WA_VAR wa_fsk, 2                ;   wa_femit: a row's bytes outside the box
WA_VAR wa_flfn, 2               ;   wa_fline or wa_flinef
WA_VAR wa_foff, 1               ;   0: every joint on the screen (bit 0 one
                                ;   may be off the side, bit 1 above or below)
WA_VAR wa_fkh, 2                ;   Khi: the feet's line, sub-columns
WA_VAR wa_fk, 2                 ; wa_fjl's narrow sums: Klo (fcx fs8 + 256 W:
                                ;   a byte, its high byte left 0),
WA_VAR wa_fe, 2                 ;   eye fv8 + 256 hh + 255
WA_VAR wa_fbuf, WA_CELLS*2      ; a figure's sub-pixels by cell offset (0
                                ; between figures: wa_figure clears its own)
WA_VAR wa_nwr, 2                ; video writes this frame
WA_VAR wa_rcp, (WA_ZMAX-WA_NEAR)*2 ; 0x7FFF0 / z, z = NEAR.. (wa_ptbi)
WA_VAR wa_xk, WA_MXV*2+2        ; per coordinate: the step from the one
WA_VAR wa_yk, WA_MYV*2+2        ; before, as its products' place in wa_mk
WA_VAR wa_nk, 2                 ; the last slot used, x 8
WA_VAR wa_lord, WA_MBOX*4+1    ; the boxes front to back, a record each:
WA_VAR wa_lrec, 2               ; box, a, b, c (wa_order, wa_sil) - the walk's
WA_VAR wa_kr, 2                 ; place in them, and this frame's corners
WA_VAR wa_kl, 2                 ; for the hints (wa_render)
WA_VAR wa_bh, WA_MBOX*4         ; per box x 4: its hint, and at +2 the view
WA_VAR wa_vcnt, 2               ; its three vertices were made at: [wa_vcnt]
WA_VAR wa_rview, 2              ; counts the view's moves (wa_moved); the
                                ; room's four
WA_VAR wa_pang, 2               ; the view the products were made for
WA_VAR wa_ppx, 2                ; (wa_products)
WA_VAR wa_ppy, 2
WA_VAR wa_mx0, 2                ; the silhouettes' cell, [mx0, mx1) x
WA_VAR wa_mx1, 2                ; [my0, my1) (wa_sil) - the four adjacent,
WA_VAR wa_my0, 2                ; as the order's below...
WA_VAR wa_my1, 2
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
    mov byte [wa_nsht], 3
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
    mov word [wa_mkp], wa_face.mk ; a vertex's top and bottom by one multiply
    mov word [wa_mizp], wa_face.miz ; (wa_face.mk1) where the bottom's
    cmp bl, 3                   ; numerator less the top's is a 16th of
    jne .paired                 ; their sum - the standing eye
    mov dx, [wa_numt]
    or dx, dx
    jle .paired
    mov bx, ax
    sub bx, dx
    add ax, dx
    jc .paired
    test al, 15
    jnz .paired
    mov dx, ax
    mov cl, 4
    shr dx, cl
    cmp dx, bx
    jne .paired
    mov [wa_numh], ax
    mov word [wa_mkp], wa_face.mk1
    mov word [wa_mizp], wa_face.miz1
.paired:
    mov al, [wa_sparse]         ; the detail level's choices, when it
    cmp al, [wa_kspa]           ; changes (wa_geominit forgets it) - or L,
    je .spa                     ; the sparse key, which wa_collook reads too
    mov [wa_kspa], al
    mov byte [wa_klook], 0xFF
.spa:
    mov al, [wa_look]
    cmp al, [wa_klook]
    je .looked
    call wa_lookset
.looked:
    xor al, al
    mov word [wa_fspp], wa_fspan
    cmp byte [wa_hflat], 0
    jne .gen
    mov word [wa_fspp], wa_fspanh
    mov word [wa_mkp], wa_face.mk ; (heights: a top per box, two multiplies)
    mov word [wa_mizp], wa_face.miz
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
    mov ax, [wa_px]             ; THE BOXES' ORDER, and each one's silhouette:
    cmp ax, [wa_mx0]            ; the same from anywhere in a cell of lines the
    jl .mout                    ; player stands in, so they are made again
    cmp ax, [wa_mx1]            ; only when one is crossed. The silhouettes'
    jge .mout                   ; cell (wa_sil's: the boxes' own lines) is
    mov ax, [wa_py]             ; inside the order's (wa_order's: the tree's
    cmp ax, [wa_my0]            ; split lines)
    jl .mout
    cmp ax, [wa_my1]
    jl .ordered
.mout:
    mov ax, [wa_px]
    cmp ax, [wa_rx0]
    jl .order
    cmp ax, [wa_rx1]
    jge .order
    mov ax, [wa_py]
    cmp ax, [wa_ry0]
    jl .order
    cmp ax, [wa_ry1]
    jl .stale
.order:
    call wa_order
.stale:
    call wa_stale
.ordered:
    mov ax, [wa_k16]            ; THE VIEW'S EDGES, made again only when the
    mov bx, [wa_c16]            ; projection changes
    cmp ax, [wa_vuk]
    jne .u
    cmp bx, [wa_vuk+2]
    je .uok
.u:
    mov [wa_vuk], ax
    mov [wa_vuk+2], bx
    mov word [wa_vpc+WA_VS1], 0 ; a face's ends there (wa_vend): their
    mov dx, bx                  ; columns, x 16...
    shl dx, 1
    mov [wa_vpc+WA_VS2], dx
    sub dx, 7                   ; ...the first column past the right, less
    mov [wa_cend], dx           ; a centre's rounding (the walk)...
    mov dx, ax
    xor ax, ax                  ; ...and u = 1 - c16 / k16, x 65536: the
    sub dx, bx                  ; edge is x = +-(1 - u) z. 0 at 90 degrees
    jbe .us                     ; (or wider: never); k16 - c16 is under
    cmp dx, [wa_k16]            ; k16 - no overflow
    jae .us
    div word [wa_k16]
.us:
    mov [wa_vu], ax
    call wa_moved               ; (every vertex's column is to make again)
.uok:
; THE WALK over wa_order's list, a box a turn. A record is the box (x 4) and
; the vertices of its SILHOUETTE from where the player stands - a, the left
; end of the faces seen, b between them where two are seen, c, the right end
; (wa_sil: made when the box is first tested in a cell of the boxes' lines).
; THE VIEW TEST is on those vertices, not a cone over the box's bounds: we
; see a convex box's faces left to right from a to c through less than a half
; turn, so nothing of it is in the view when a is right of the view's right
; edge (x > |z|), or c left of its left edge, or both are past the right
; edge's line (x - z > 0: the box is behind) - at 90 degrees, with a margin
; for the products' rounding, off the two transforms a kept box needs anyway;
; and then exactly, at any field of view, by a's and c's columns. A kept box
; pays nothing but the compares.
; THE HINT (wa_bh, a box's: it outlives the list and the silhouette): a box
; dropped that way is nearly always still wholly past the right edge's line
; (1), or the left's (2) - and that is ONE corner's test, the box's least in
; x - z (wa_kr) or greatest in x + z (wa_kl: wa_products), on the products'
; high words (their rounding, or at an exact level their truncation, is
; inside the margin): four of them, where the full test is two transforms.
; One dropped by a column - between the 90 degrees of that test and a
; narrower view's edge - is still off that edge when c is left of the left
; one (3) or a right of the right one (4): one transform and a multiply. A
; hint that fails (0x80 with it) is not set again until the box has been in
; the view; another may be.
; ...AND A STILL VIEW: a box drawn tall keeps the view's count (wa_vcnt) at
; +2, and while the view stands goes straight to its faces (.lsame).
    mov si, wa_lord             ; front to back, while any column is open
    jmp short .lgo
.leaves:                        ; (after a box that may have drawn)
    cmp word [wa_left], 0
    je .lend
.lnext:
    mov si, [wa_lrec]
.lgo:
    lodsw                       ; AL = the box x 4, AH = a
    cmp al, 0xFF
    jne .lbox
.lend:
    jmp .walked
.lbox:
    inc si
    inc si
    xor bx, bx
    mov bl, al
    mov dl, [wa_bh+bx]
    dec dl
    js .lfull                   ; no hint
    jz .lhr
    dec dl
    jz .lhl
    dec dl
    jz .lh3
    dec dl
    jnz .lfull                  ; (one that failed)
    mov [wa_lrec], si           ; 4: a right of the screen's own edge,
    mov bl, [si-3]              ; x - t z > 0 - t the view's half width in
    call [wa_vtp]               ; x/z, as 1 - u (wa_vu): x - z + u z
    mov bx, ax
    sub bx, cx
    jmp short .lh34
.lh3:                           ; 3: c left of the screen's edge, x + t z < 0:
    mov [wa_lrec], si           ; -(x + z) + u z
    mov bl, [si-1]
    call [wa_vtp]
    mov bx, ax
    add bx, cx
    neg bx
.lh34:
    mov si, [wa_lrec]
    cmp cx, WA_NEAR             ; (and ahead of the eye)
    jl .lmiss
    mov ax, cx
    mul word [wa_vu]
    add bx, dx
    cmp bx, 3
    jge .lgo
    jmp short .lmiss
.lhl:
    add bx, [wa_kl]
    mov bl, [bx]                ; the corner's vertex...
    xor bh, bh
    mov bx, [wa_vtx+bx]         ; ...its x and y indices (x 4)
    mov al, bl
    cbw
    mov di, ax
    mov bl, bh
    xor bh, bh
    mov ax, [wa_xs+di+2]        ; x + z
    add ax, [wa_xc+di+2]
    add ax, [wa_ys+bx+2]
    sub ax, [wa_yc+bx+2]
    cmp ax, -3
    jle .lgo                    ; still left of the view
    jmp short .lmiss
.lhr:
    add bx, [wa_kr]
    mov bl, [bx]
    xor bh, bh
    mov bx, [wa_vtx+bx]
    mov al, bl
    cbw
    mov di, ax
    mov bl, bh
    xor bh, bh
    mov ax, [wa_xs+di+2]        ; x - z
    sub ax, [wa_xc+di+2]
    sub ax, [wa_yc+bx+2]
    sub ax, [wa_ys+bx+2]
    cmp ax, 3
    jge .lgo                    ; still right of the view, or behind
.lmiss:
    xor bx, bx
    mov bl, [si-4]
    or byte [wa_bh+bx], 0x80
.lfull:                         ; BX = the box x 4
    mov [wa_lrec], si
    mov ax, [wa_vcnt]           ; THE VIEW WHERE IT WAS when this box was last
    cmp ax, [wa_bh+bx+2]        ; drawn tall: its three vertices stand, and
    je .lsame                   ; it is in view still
    mov bl, [si-3]              ; a
    cmp bl, 0xFE
    jb .lsil
    jne .lover                  ; (no face seen: the player over the box)
    call wa_sil                 ; (its silhouette, in this cell)
    xor bx, bx
    mov bl, [si-3]
    cmp bl, 0xFF
    jne .lsil
.lover:                         ; (no face seen: a low box's top, on heights)
    cmp byte [wa_hflat], 0
    jne .lnx
    mov bl, [si-4]
    mov ax, [wa_bxh+bx]
    cmp ax, [wa_hmax]
    jb .llowb
    cmp ax, [wa_eye]
    jle .llowb
.lnx:
    jmp .lnext
.lsame:
    mov cl, 0xFD                ; (their tops and bottoms are the frame's:
    mov bl, [si-3]              ; the pitch and the eye may have moved)
    and [wa_vok+bx], cl
    mov bl, [si-1]
    and [wa_vok+bx], cl
    mov bl, [si-2]
    cmp bl, 0xFF
    je .ls1
    and [wa_vok+bx], cl
.ls1:
    mov bl, [si-4]
    jmp .lcok2                  ; (CX not 0)
.lsil:
    call [wa_vtp]               ; a: AX = x, CX = z, DI = the vertex
    mov dx, ax
    sub dx, cx
    mov bp, dx                  ; BP = a's x - z
    cmp dx, 3
    jl .la
    mov dx, ax
    add dx, cx
    cmp dx, 3
    jl .la
    mov dl, 1                   ; a right of the view: all of it is
    jmp short .lhset
.la:
    call wa_vcam                ; a's column: off the screen's right?
    cmp ax, [wa_cend]
    jl .lain
    mov dl, 4
    jmp short .lhset
.lain:
    mov si, [wa_lrec]
    mov bl, [si-1]              ; (BH = 0 still)
    call [wa_vtp]               ; c
    mov dx, ax
    sub dx, cx
    cmp dx, -3
    jg .lc
    mov bx, ax
    add bx, cx
    cmp bx, -3
    jg .lkeep
    mov dl, 2                   ; c left of the view: all of it is
.lhset:                         ; DL = the hint for the next frame - unless
    mov si, [wa_lrec]           ; it is the one that failed
    xor bx, bx
    mov bl, [si-4]
    mov al, [wa_bh+bx]
    and al, 0x7F
    cmp al, dl
    je .lnx
    mov [wa_bh+bx], dl
    jmp .lnext
.lc:
    cmp dx, 3
    jl .lkeep
    mov dl, 1
    cmp bp, 3
    jge .lhset                  ; both past the right edge's line
.lkeep:
    call wa_vcam                ; c's column: its last one under 0 is off the
    cmp ax, 9                   ; left (a vertex behind is no column:
    jge .lcok                   ; wa_face's)
    cmp ax, 0x8000
    je .lcok
    mov dl, 3
    jmp .lhset
.lcok:
    mov si, [wa_lrec]
    xor bx, bx
    mov bl, [si-4]              ; the box x 4
    mov byte [wa_bh+bx], 0      ; (in the view: a hint may be set again)
    xor cx, cx
.lcok2:                         ; (CX not 0: its vertices made, at this view)
    cmp byte [wa_hflat], 0
    jne .ltall
    mov ax, [wa_bxh+bx]         ; HEIGHTS (HEIGHTS-PLAN 7.3.1): a box at the
    cmp ax, [wa_hmax]           ; arena's tallest, the eye below it, is TALL -
    jb .llow                    ; today's spans at its own height; any other
    cmp ax, [wa_eye]            ; is drawn in OUTLINE (wrlow.inc)
    jle .llow
    call wa_hnt
.ltall:
    shr bx, 1
    shr bx, 1
    mov bl, [wa_bcol+bx+1]      ; its colour (the room's is first)
    mov al, [wa_at+bx]
    mov [wa_wcol], al
    or cx, cx
    jnz .lfaces
    mov bl, [si-4]
    mov ax, [wa_vcnt]           ; (its vertices stand while the view does)
    mov [wa_bh+bx+2], ax
    mov bl, [si-2]              ; b
    cmp bl, 0xFF
    je .lone
    call [wa_vtp]
    call wa_vcam
    mov si, [wa_lrec]
    jmp short .ltwo
.lfaces:
    cmp byte [si-2], 0xFF
    je .lone
.ltwo:
    mov byte [wa_endok], 0      ; (the corner two faces share is the second
                                ; one's START: drawing both put `││` there)
    mov ax, [si-3]              ; a -> b
    call wa_face
    mov si, [wa_lrec]
    mov ax, [si-2]              ; b -> c
    jmp short .llast
.lone:
    mov al, [si-3]              ; a -> c
    mov ah, [si-1]
.llast:
    mov byte [wa_endok], 1
    call wa_face
    jmp .leaves
.llow:                          ; a and c are made: stamped, for wa_lowbox
    mov al, [wa_frame]
    mov bl, [si-3]
    mov [wa_vst+bx], al
    mov bl, [si-1]
    mov [wa_vst+bx], al
    mov bl, [si-4]
.llowb:                         ; BX = the box x 4
    mov di, bx                  ; DI = its record in wa_bxh
    lea ax, [bx+wa_corn+4]      ; (the room's row is first)
    mov [wa_crow], ax
    shl bx, 1
    lea si, [bx+wa_boxes]
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov bl, [wa_bcol+bx+1]
    mov al, [wa_at+bx]
    mov [wa_wcol], al
    call wa_lowbox
    jmp .leaves
.walked:
    cmp word [wa_left], 0       ; the room, last: it encloses everything
    je .roomless
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
.rflat:
    mov ax, [wa_vcnt]           ; its four corners (vertices 0..3) - unless
    cmp ax, [wa_rview]          ; the view is where it was when they were
    jne .rmk                    ; last made: then only their tops and
    mov al, 0xFD                ; bottoms are to make again
    and [wa_vok], al
    and [wa_vok+2], al
    and [wa_vok+4], al
    and [wa_vok+6], al
    jmp short .rfaces
.rmk:
    mov [wa_rview], ax
    mov bx, 6
.rv:
    call [wa_vtp]
    call wa_vcam
    lea bx, [di-2]
    or bx, bx
    jns .rv
.rfaces:
    mov ax, (6 << 8) | 0        ; ...and its faces, which run the other way:
    call wa_face                ; c0->c3, c3->c2, c2->c1, c1->c0 (vertex x 2)
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
    mov byte [wa_vok+WA_VS1], 3 ; (an end at the screen's edge is in the
    mov byte [wa_vok+WA_VS2], 3 ; view, and made: wa_face, wa_flat.steep)
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
    mov byte [wa_klook], 0xFF   ; ...and the level's choices (and with them
                                ; the products: wa_lookset)
    mov word [wa_mx0], 1        ; ...and each box's silhouette, with no hint
    mov word [wa_mx1], 0        ; (wa_bh)
    mov di, wa_bh
    mov cx, WA_MBOX*2
    xor ax, ax
    rep stosw
    mov [wa_rview], ax
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
    dec cx                      ; its step's products' place in wa_mk (slot
.s:                             ; x 8, from 8), then 0
    inc di
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
    add bx, wa_mk
    mov [di], bx
    inc si
    inc si
    loop .s
    mov word [di+2], 0
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
    jmp %%put
%%next:
    add ax, [bx]                ; ...and each after it, the step's added
    adc dx, [bx+2]
    add bp, [bx+4]
    adc cx, [bx+6]
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
    inc si
    mov bx, [si]
    or bx, bx
    jnz %%next
%endmacro

; wa_products - per distinct coordinate, (v - p) x 4 cos and (v - p) x 4 sin:
; x 4 so a vertex's camera z and x are the HIGH words of two 32-bit sums,
; with no shifts (wa_vte, wa_vtr). All of them, whenever the view has moved:
; doing it lazily per coordinate was measured SLOWER (19.5 -> 19.0 fps at 40
; columns) - nearly every coordinate is used, so the stamp tests were pure
; cost. But not by a multiply each: the first value of an axis is
; multiplied, and each after it is the one before plus the products of the
; STEP between them - an arena has few distinct steps (wa_geominit, at most
; WA_MAXKS), multiplied here when the angle has moved. 12 multiplies in the
; full arena, where there were 52; the sums are exact, so every product is
; the same bits as before. Also the view's count (wa_vcnt) and, with the
; angle, the hints' corners (wa_kr, wa_kl)
wa_products:
    mov ax, [wa_cang]           ; THE VIEW WHERE IT WAS - the angle is one of
    mov bx, [wa_px]             ; 1024, so a slow aim holds it for frames at
    mov cx, [wa_py]             ; a time: every product stands. The angle
    cmp ax, [wa_pang]           ; alone where it was (walking a straight
    jne .ang                    ; line): the steps' products stand
    cmp bx, [wa_ppx]
    jne .pos
    cmp cx, [wa_ppy]
    jne .pos
    ret
.pos:
    mov [wa_ppx], bx
    mov [wa_ppy], cx
    jmp short .kd
.ang:
    mov [wa_pang], ax
    mov [wa_ppx], bx
    mov [wa_ppy], cx
    mov ax, [wa_sinv]           ; the corners a box wholly outside the view
    mov bx, ax                  ; is tested by (wa_render's hints): of its
    sub ax, [wa_cos]            ; four, the least in x - z and the greatest
    add bx, [wa_cos]            ; in x + z - by the signs of sin - cos and
    xor si, si                  ; sin + cos, the same for every box
    or ax, ax
    jle .ka
    inc si
.ka:
    or bx, bx
    jle .kb
    inc si
    inc si
.kb:
    shl si, 1
    mov ax, [wa_krt+si]
    mov [wa_kr], ax
    mov ax, [wa_klt+si]
    mov [wa_kl], ax
    mov si, [wa_nk]             ; the steps: slot x 8, from 8
    mov bx, [wa_cos]
    mov cx, [wa_sinv]
    or si, si
    jz .kd
.k:
    mov ax, [wa_kv+si]          ; the step x 4
    imul bx
    mov [wa_mk+si], ax
    mov [wa_mk+si+2], dx
    mov ax, [wa_kv+si]
    imul cx
    mov [wa_mk+si+4], ax
    mov [wa_mk+si+6], dx
    sub si, 8
    jnz .k
.kd:
    inc word [wa_vcnt]          ; (the view has moved: wa_moved)
    jnz .kv
    call wa_moved.lap
.kv:
    test byte [wa_look], LK_ROUND
    jnz .round
    WA_AXIS wa_xv, wa_px, wa_xk, wa_xc, wa_xs, 0
    WA_AXIS wa_yv, wa_py, wa_yk, wa_yc, wa_ys, 0
    ret
.round:
    WA_AXIS wa_xv, wa_px, wa_xk, wa_xc, wa_xs, 1
    WA_AXIS wa_yv, wa_py, wa_yk, wa_yc, wa_ys, 1
    ret

; wa_moved - the view has moved (wa_products), or the projection changed:
; [wa_vcnt] counts them, and a vertex made at an earlier count is to be made
; again (wa_bh + 2, a box's; wa_rview, the room's). Never 0 - no box's - and
; when it laps, every box's is forgotten. Clobbers AX, CX, DI.
wa_moved:
    inc word [wa_vcnt]
    jnz .out
.lap:
    inc word [wa_vcnt]
    xor ax, ax
    mov [wa_rview], ax
    mov di, wa_bh+2
    mov cx, WA_MBOX
.z:
    mov [di], ax
    add di, 4
    loop .z
.out:
    ret

; wa_order - the box tree walked front to back from the player's position
; into wa_lord (a record a box: the box x 4 and three bytes for its
; silhouette; 0xFF at the end), and the cell that position is in:
; [rx0, rx1) x [ry0, ry1). The order is a function of the position alone -
; which side of each split line it is on - so a frame whose position is
; still in the cell (turning, or moving inside it, which is most frames of
; play) reuses the list, and a frame walks no inner node.
; THE PVS (docs/plans/WIREARENA-HEIGHTS-PLAN.md 9.8): an arena may carry, per
; cell of a square grid, the boxes that can draw anything from somewhere in
; it (tools/arenagen.py, pvs_masks); the rest are left off the list, so a
; frame never visits them. The order's cell is then inside the grid's cell.
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
    mov al, 0xFF
    stosb
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
    mov al, [bx+1]              ; the box x 8
    shr al, 1
    stosb                       ; (its record's: x 4)
    add di, 3                   ; (its silhouette: wa_stale, wa_sil)
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

; wa_stale - the player has left the silhouettes' cell (or the order is
; new): every box's silhouette is to be made again (a = 0xFE: wa_sil, when
; the box is next tested), in a cell that starts as the order's; and a hint
; that is a silhouette vertex's test (3, 4) goes with it. (ES = DS.)
wa_stale:
    mov si, wa_rx0
    mov di, wa_mx0
    movsw
    movsw
    movsw
    movsw
    mov si, wa_lord
.s:
    lodsw                       ; AL = the box x 4
    cmp al, 0xFF
    je .out
    mov byte [si-1], 0xFE
    inc si
    inc si
    xor bx, bx
    mov bl, al
    mov al, [wa_bh+bx]
    and al, 0x7F
    cmp al, 3
    jb .s
    mov byte [wa_bh+bx], 0
    jmp .s
.out:
    ret

; wa_sil - SI = past a box's record: its SILHOUETTE from where the player
; stands, into the record - the vertices (x 2) a, b and c of the faces seen,
; left to right: one face a -> c (b 0xFF), or two a -> b -> c; none (the
; player over the box) a 0xFF. Which faces are seen is the position's alone -
; which side of the box's four lines it is on - so the silhouettes' cell
; [mx0, mx1) x [my0, my1) is kept inside those lines, and a frame in it
; chooses no faces and looks up no corners. Keeps SI; clobbers the rest.
; (ES = DS.)
wa_sil:
    xor bx, bx
    mov bl, [si-4]
    shl bx, 1
    mov di, bx                  ; DI = the box x 8
    mov ax, [wa_py]             ; the faces seen: S 1, E 2, N 4, W 8 (a face
    lea bx, [di+wa_boxes+2]     ; f runs from corner f to f + 1)
    mov bp, wa_my0
    call .side
    mov bx, .ybit
    xlatb
    mov cl, al
    mov ax, [wa_px]
    lea bx, [di+wa_boxes]
    mov bp, wa_mx0
    call .side
    mov bx, .xbit
    xlatb
    or al, cl
    mov bx, .sil
    xlatb                       ; the silhouette's corners, two bits each:
    mov ah, al                  ; a, b, c - or 0xFF, no face
    shr di, 1
    lea bx, [di+wa_corn+4]      ; the box's corners' vertices (the room's
    lea di, [si-3]              ; row is first)
    mov cx, 3
    cmp al, 0xFF
    je .none
.v:
    mov al, ah
    and al, 3
    xlatb
    stosb
    shr ah, 1
    shr ah, 1
    loop .v
    test ah, 1                  ; (one face: no b)
    jz .out
    mov byte [si-2], 0xFF
.out:
    ret
.none:
    rep stosb
    ret
.ybit:      db 1, 0, 4
.xbit:      db 8, 0, 2
; by the faces seen: a | b << 2 | c << 4, corner numbers - and 64 for one
; face (no b); 0xFF none (or two the player cannot be outside of at once)
.sil:       db 0xFF, 0x50, 0x61, 0x24, 0x72, 0xFF, 0x39, 0xFF
            db 0x43, 0x13, 0xFF, 0xFF, 0x0E, 0xFF, 0xFF, 0xFF
.side:                          ; AX = the player's coordinate, BX -> the
    mov dx, [bx]                ; box's low one (its high at +4), BP -> the
    cmp ax, dx                  ; cell's pair: AL = 0 below the box, 1
    jge .s1                     ; beside it, 2 above - and the cell kept on
    xor al, al                  ; that side of the box's two lines
.shi:
    cmp dx, [ds:bp+2]
    jge .sr
    mov [ds:bp+2], dx
.sr:
    ret
.s1:
    call .slo
    mov dx, [bx+4]
    inc dx
    cmp ax, dx
    mov al, 1
    jl .shi
    inc ax
.slo:
    cmp dx, [ds:bp]
    jle .sr
    mov [ds:bp], dx
    ret
; the corner of a box least in x - z, by (sin - cos > 0) + 2 (sin + cos > 0),
; and the one greatest in x + z (wa_render's hints): its place in a box's
; row of corners' vertices (the room's row is first)
wa_krt:     dw wa_corn+4+1, wa_corn+4+0, wa_corn+4+2, wa_corn+4+3
wa_klt:     dw wa_corn+4+0, wa_corn+4+3, wa_corn+4+1, wa_corn+4+2

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

; wa_face - AL = from vertex x 2, AH = to vertex x 2, both made this frame
; (wa_vcam: a box's, the room's - wa_render): a face we see the front of.
; Unless every column it could reach is already closed, it is projected (an
; end outside the view made at the screen's edge: wa_vend) and stepped. A
; vertex's column is made with its transform; its top, bottom and inverse
; depth only here, past the occlusion test, once a frame - a face wholly
; behind nearer walls never pays for them.
; One routine for a flat arena and for heights' tall faces, at the height
; wa_numt and wa_nsht hold (wa_hnt; a flat arena's from wa_render): the span's
; way on is [wa_fspp].
wa_face:
    mov [wa_fv], ax
    xor bx, bx
    mov bl, ah
    mov dx, [wa_vpc+bx]         ; THE PRE-CHECK: the columns it could reach,
    mov bl, al                  ; x 16 (a vertex in view is its own, one
    mov si, bx                  ; beside the view is beyond that edge -
    mov ax, [wa_vpc+bx]         ; wa_vcam's sentinels - and one BEHIND is
    cmp ax, dx                  ; beyond the left edge as a face's first end
    jg .rev                     ; and beyond the right as its second: we see
.lohi:                          ; a face's front, so it runs left to right)
    mov cl, 4                   ; the first column whose centre is at or past
    add ax, 7                   ; the near end, (c + 7) >> 4, and the last
    sar ax, cl                  ; short of the far end, ((c + 7) >> 4) - 1:
    add dx, 7                   ; EXACTLY wa_span's columns, so a face seen
    sar dx, cl                  ; edge-on, between two centres, ends here -
    dec dx                      ; not after its tops, bottoms and inverse
    cmp ax, dx                  ; depths (two a frame on the play bench)
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
.rev:                           ; c1 > c2: the ends the other way about - a
    cmp dx, 0x8000              ; face seen edge-on, no columns at all -
    jne .shut                   ; unless the second end is BEHIND
    mov dx, WA_VOFF
    jmp .lohi
.open:                          ; SI = v1 x 2
    mov [wa_cs0], bx
    mov [wa_ce0], dx
    lea ax, [di-2-wa_cov]       ; the first open column (wa_fspan's)
    mov [wa_cs], ax
    mov bl, [wa_fv+1]
    xor bh, bh
    mov di, bx                  ; DI = v2 x 2
    mov al, [wa_vok+si]
    mov [wa_e1], al
    mov ah, [wa_vok+di]
    and ah, [wa_endok]          ; (the end corner, only on a silhouette)
    mov [wa_e2], ah
    or al, al
    jnz .in1
    xor cx, cx                  ; THE FIRST END OUTSIDE THE VIEW: an end at
    call wa_vend                ; the screen's left edge in its place
    jc .clip
    mov si, WA_VS1
    call [wa_mizp]
    jmp short .e2
.in1:
    test al, 2                  ; (in the view: its top, bottom and inverse
    jnz .e2                     ; depth made the first time a face of it
    call [wa_mkp]               ; gets this far in a frame)
.e2:
    mov al, [wa_vok+di]
    or al, al
    jnz .in2
    push si
    mov si, di
    mov cx, 2                   ; ...and the second, at the right edge
    call wa_vend
    jc .clip1
    mov si, WA_VS2
    call [wa_mizp]
    mov di, si
    pop si
    jmp short .go
.in2:
    test al, 2
    jnz .go
    xchg si, di
    call [wa_mkp]
    xchg si, di
.go:
    mov ax, si                  ; (wa_fv: the two ends the span HAS, an end
    mov bx, di                  ; at the screen's edge among them - its steep
    mov ah, bl                  ; way reads their tops and bottoms back off
    mov [wa_fv], ax             ; them: wa_flat.steep)
    jmp [wa_fspp]               ; (wa_fspan, or heights' wa_fspanh)
.clip1:
    pop si
.clip:                          ; (an end the edge cannot stand for - the
    mov bx, [wa_fv]             ; plane inside the near plane there: the
    mov si, bx                  ; clip's own way)
    and si, 0xFF
    mov bl, bh
    xor bh, bh
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
    test byte [wa_vok+si], 1    ; DI = wa_c1 or wa_c2: projected - as a
    jnz .put                    ; vertex of its own (WA_VS3)
    mov ax, [bx]
    mov cx, [bx+2]              ; (z >= NEAR, |x| <= 8z)
    imul word [wa_k16]          ; the column, x16: NOT clamped - the span
    idiv cx                     ; clamps to the screen
    add ax, [wa_c16]
    mov si, WA_VS3
    mov [wa_vpc+si], ax
    mov bx, cx
    shl bx, 1
    mov bx, [wa_rcp+bx-WA_NEAR*2]
    call [wa_mizp]
    jmp short .have
.put:                           ; SI = a vertex in view, DI = wa_c1 or wa_c2:
    test byte [wa_vok+si], 2    ; its column, top, bottom, inverse depth -
    jnz .have                   ; the last three made the first time a face
    call [wa_mkp]               ; of it gets this far in a frame (.mk)
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
    mov bx, [wa_vz+si]          ; AX, BX, CX, DX. (A vertex's z is under
    shl bx, 1                   ; WA_ZMAX, the room's diagonal: the table's)
    mov bx, [wa_rcp+bx-WA_NEAR*2]
.miz:                           ; (BX = an inverse depth: an edge end's)
    mov [wa_vpi+si], bx
    mov ax, [wa_numt]
    imul bx
    mov cl, [wa_nsht]
    sar dx, cl
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
; .mk1, .miz1 - .mk and .miz by ONE multiply, where the top and the bottom
; are 15/32 and 17/32 of the wall's whole height - a flat arena's 3 m wall
; from the standing eye, 1.6 m: most frames (wa_render chooses: [wa_mkp],
; [wa_mizp]). [wa_numh] is the two numerators' sum; a quarter half-row's
; rounding apart, the same values.
.mk1:
    or byte [wa_vok+si], 2
    mov bx, [wa_vz+si]
    shl bx, 1
    mov bx, [wa_rcp+bx-WA_NEAR*2]
.miz1:
    mov [wa_vpi+si], bx
    mov ax, [wa_numh]
    mul bx                      ; DX = the whole height, x 16
    mov cl, 4
    mov ax, dx
    shr ax, cl
    mov bx, dx
    add dx, ax                  ; 17/16 of it, / 16: below the horizon
    shr dx, cl
    add dx, [wa_hq]
    mov [wa_vpb+si], dx
    sub bx, ax                  ; 15/16 of it, / 16: above
    shr bx, cl
    mov ax, [wa_hq]
    sub ax, bx
    mov [wa_vpt+si], ax
    ret

; wa_vend - a face's end OUTSIDE the view, made where the face meets the
; screen's EDGE instead of by a clip. Every wall is axis-aligned, so along a
; face on the plane x = X the inverse depth depends on the screen column
; alone: 1/z = (cos + s sin) / (X - px), s the column's x/z - and on y = Y,
; (sin - s cos) / (Y - py). At the screen's edges s is -t and +t, t the
; view's half width in x/z (c16 / k16: 1 at 90 degrees), so the end is one
; multiply for the numerator and one of wa_rcp's 1 / |d| - no lerp, no
; divide, and nothing off the screen for the span to step over (wa_clip's
; end is at the guard band, ~150 columns out). t sin is sin - u sin, u =
; 1 - t as a 16-bit fraction (wa_vu: wa_render).
; In: SI = the end's vertex x 2, [wa_fv] the face, CX = 0 its first end (the
; left edge) or 2 its second (the right). Out: CF = 0, BX = the inverse
; depth there; CF = 1: wa_clip's way - the plane inside the near plane on
; that ray (the player against the wall, |d| under ~23), or the vertex
; behind the near plane but AHEAD of the eye, where the face can end in the
; view. Keeps SI, DI; clobbers AX, CX, DX.
wa_vend:
    cmp word [wa_vpc+si], 0x8000
    jne .side
    cmp word [wa_vz+si], 0
    jg .no
.side:
    xor bh, bh
    mov bl, [wa_fv]
    mov ax, [wa_vtx+bx]         ; its first vertex's x and y indices (x 4)
    mov bl, [wa_fv+1]
    cmp al, [wa_vtx+bx]         ; both ends on one x: the plane x = X
    jne .yp
    mov bl, al
    shr bx, 1
    mov ax, [wa_xv+bx]
    sub ax, [wa_px]             ; d
    mov bx, [wa_sinv]           ; cos -+ t sin
    mov dx, [wa_cos]
    jmp short .d
.yp:
    mov bl, ah
    shr bx, 1
    mov ax, [wa_yv+bx]
    sub ax, [wa_py]
    mov bx, [wa_cos]            ; sin +- t cos
    neg bx
    mov dx, [wa_sinv]
.d:
    or cx, cx
    jnz .r
    neg bx                      ; (the left edge: s = -t)
.r:
    push ax
    push dx
    mov ax, bx
    imul word [wa_vu]
    sub bx, dx                  ; t x the other function
    pop cx
    add cx, bx                  ; CX = the ray's numerator, Q14
    pop ax                      ; AX = d
    mov dx, ax
    xor dx, cx
    js .no                      ; (the ray never meets the plane ahead)
    or ax, ax
    jns .pos
    neg ax
    neg cx
.pos:
    cmp ax, WA_NEAR
    jb .no
    cmp ax, WA_ZMAX
    jae .no
    mov bx, ax
    shl bx, 1
    mov ax, [wa_rcp+bx-WA_NEAR*2]
    mul cx                      ; 0x7FFF0 / |d| x |numerator| >> 14
    cmp dx, 0x2000
    jae .no                     ; (nearer than the near plane)
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov bx, dx
    cmp bx, 1                   ; (never 0: a column's "no wall")
    adc bx, 0                   ; (CF = 0)
    ret
.no:
    stc
    ret

; wa_vert - BX = a vertex x 2 (BH = 0) not yet made this frame, AL = the
; frame: stamped, into camera space (the level's way: wa_vtp) and, inside
; the view (the guard band), its column - wa_vcam. BX and BP are kept;
; clobbers AX, CX, DX, SI, DI. (wrlow.inc's corners: the tall boxes' and the
; room's are made unstamped, by wa_render.)
wa_vert:
    mov [wa_vst+bx], al
    call [wa_vtp]
    mov bx, di
; wa_vcam - DI = a vertex x 2, AX = its camera x, CX = its z: kept, and
; inside the view (the guard band) its column. [wa_vok]: 0 outside, 1 the
; column made, 3 the rest too (wa_face). Outside, [wa_vpc] is the pre-check's
; sentinel instead: 0x8000 behind, -WA_VOFF beside the view on the left,
; WA_VOFF on the right - past any column the guard band holds (under 9,600),
; and small enough to take a + 7. AX = [wa_vpc]; keeps BX, SI, DI, BP;
; clobbers CX, DX.
WA_VOFF equ 16000
wa_vcam:
    mov [wa_vz+di], cx
    mov [wa_vx+di], ax
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
    mov byte [wa_vok+di], 1
    imul word [wa_k16]          ; the column, x16 (wa_proj)
    idiv cx
    add ax, [wa_c16]
    mov [wa_vpc+di], ax
    ret
.behind:
    mov ax, 0x8000
    jmp short .out
.left:
    mov ax, -WA_VOFF
    jmp short .out
.right:
    mov ax, WA_VOFF
.out:
    mov byte [wa_vok+di], 0
    mov [wa_vpc+di], ax
    ret

; wa_vte, wa_vtr - BX = a vertex x 2 (BH = 0): AX = its camera x, CX = its
; z, DI = the vertex. wa_vte from the products' 32-bit sums, exact; wa_vtr
; at a level with ROUND, their rounded high words alone, a sum each
; (wa_products). [wa_vtp] is the level's (wa_lookset). BH stays 0; clobbers
; BX, SI.
wa_vte:
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
    ret
wa_vtr:
    mov di, bx
    mov ax, [wa_vtx+bx]
    mov bl, ah
    xor ah, ah
    mov si, ax
    mov cx, [wa_xc+si+2]
    add cx, [wa_ys+bx+2]
    mov ax, [wa_xs+si+2]
    sub ax, [wa_yc+bx+2]
    ret

; wa_lookset - AL = [wa_look], changed: the detail level's choices that are
; made once rather than tested where they act - which transform, a low
; box's far-top allowance (wrlow.inc)
wa_lookset:
    mov [wa_klook], al
    mov byte [wa_pang+1], 0xFF  ; (the products are the level's: made again)
    mov ax, wa_vte
    test byte [wa_look], LK_ROUND
    jz .r
    mov ax, wa_vtr
.r:
    mov [wa_vtp], ax
    call wa_collook             ; (the columns': wa_flat's level tests)
    xor ax, ax
    test byte [wa_look], LK_FARTOP
    jz .f
    mov al, WL_FARTOPV
.f:
    mov [wl_fartop], ax
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
    call .reg                   ; (AX = c1, DX = c2 - c1)
    jc .none
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

; .gen1 - wa_flat's steep way out: SPANLOOP's. The span is registered,
; wa_dc and wa_fs made (.fgx), wa_t1, wa_t2, wa_b1 and wa_b2 its edges' ends.
.gen1:                          ; the last open column too
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
    mov dh, [wa_tag]            ; DH = the tag (registered by the flat way)
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
    mov bx, [wa_vb]             ; THE HEAD: columns the wall leaves EMPTY -
    or cx, cx                   ; its top above the screen and its bottom
    jns .hgo                    ; under it (a near wall running off the
    cmp bx, WA_HR               ; view): opened and stepped, no more - the
    jl .hgo                     ; bottom BX.AX while they last
    mov ax, [wa_vbl]
.hd:
    cmp word [wa_cov+si], 0
    jne .hsh
    mov [wa_cov+si], dh
.hst:
    add bp, [wa_stl]
    adc cx, [wa_st]
    add ax, [wa_sbl]
    adc bx, [wa_sb]
    inc si
    inc si
    cmp si, [wa_end]
    jae .hall
    or cx, cx
    jns .hend
    cmp bx, WA_HR
    jge .hd
.hend:
    mov [wa_vbl], ax
    mov [wa_vb], bx
    jmp .hgo
.hall:                          ; (all of it)
    mov [wa_vbl], ax
    mov [wa_vb], bx
    jmp .vd
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
    cmp ax, 0x4000
    jae .eb
    shl ax, 1
    shl ax, 1                   ; |change| x 4
    cmp ax, cx
    jae .ew
    xchg ax, dx                 ; under a half-row a column: the fraction
    div cx                      ; alone
    mov bx, ax                  ; BX = |step|
    mul word [wa_fs]            ; x off: DX:AX
    shr dx, 1                   ; / 16
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    jmp .e4
.e0:
    xor bx, bx                  ; (no step: the start is t1's)
    xor ax, ax
    cwd
    jmp .e4
.eb:
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1                   ; DX:AX = |change| x 4
.ew:                            ; a half-row a column or more: the step's
    div cx                      ; whole part too
    mov si, ax
    mov byte [wa_steep], 1
    xor ax, ax
    div cx
    mov bx, ax                  ; SI.BX = |step|
    mul word [wa_fs]            ; x off: DX:AX, and above
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
    add di, 2                   ; + t1 + a half: (t1 + 2) / 4
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
; steep edge's way out, wa_flat.steep, reads them back). On heights (wa_fspanh) the passes
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
; edge at a time (wa_fedge), two passes by runs (wa_span.ftail), SPANLOOP's when
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
    jae .l2e
    cmp word [wa_cov+si+2], 0   ; a shut column: the next one open? Else on
    je .l2a                     ; to the next open one with one scan (a
    push ax                     ; nearer box hides a run of them)
    push cx
    push di
    lea di, [si+wa_cov+4]
    mov cx, [wa_end]
    sub cx, si
    shr cx, 1
    dec cx
    dec cx
    jle .l2n
    xor ax, ax
    repne scasw
    jne .l2n
    lea ax, [di-2-wa_cov]
    sub ax, si
    add si, ax
    add bx, ax
    add bp, ax
    pop di
    pop cx
    pop ax
    jmp .l2
.l2n:
    pop di
    pop cx
    pop ax
    mov si, [wa_end]
.l2e:
    mov byte [wa_cov+si+1], 0   ; (SI = the end)
    mov ax, di                  ; opened: two cells a column on the list
    sub ax, [wa_lp]
    shr ax, 1
    shr ax, 1
    jmp .lcnt
.l1x:
    mov bx, [wa_vt0]
    jmp .l1
.l1b:
    mov bx, cx
.l1:                            ; one edge, BX (or none: off the screen):
    mov [wa_fh], bx             ; the opening pass, no step
    xor cx, cx
    mov [wa_ncl], cx
    mov ah, [wa_wcol]
    call wa_fo
    mov byte [wa_cov+si+1], 0   ; (SI = the end)
    xchg ax, si                 ; opened: the range less the shut ones
    sub ax, [wa_cs]
    shr ax, 1
    sub ax, [wa_ncl]
.lcnt:
    sub [wa_left], ax
    test byte [wa_look], LK_NOPOST ; (NOPOST: a wall shorter than
    jz .np                      ; WA_POSTMIN half-rows - a far one - has no
    mov ax, [wa_vb0]            ; corner posts)
    sub ax, [wa_vt0]
    cmp ax, WA_POSTMIN
    jl .pdone
.np:
    mov al, [wa_e1]             ; THE POSTS, at the segment's own ends where
    or al, [wa_e2]              ; this span opened them (wa_e1, wa_e2): a `│`
    jz .pdone                   ; each, between the same two half-rows - the
    mov ax, [wa_vb0]            ; rows made once (wa_corner's: stop before
    sar ax, 1                   ; the bottom's row, start under the top's).
    cmp ax, WA_ROWS             ; Nothing else is in either column: no stamp
    jle .pb                     ; tests
    mov ax, WA_ROWS
.pb:
    mov bx, [wa_vt0]
    sar bx, 1
    inc bx
    jns .pt
    xor bx, bx
.pt:
    sub ax, bx
    jle .pdone
    xchg ax, bp                 ; BP = the rows
    shl bx, 1
    mov bx, [wa_rowo2+bx]       ; BX = the first's offset
    mov al, 0xB3
    mov ah, [wa_wcol]
    cmp byte [wa_e1], 0
    je .c2
    mov si, [wa_cs]
    cmp si, [wa_cs0]
    jne .c2
    call .post
.c2:
    cmp byte [wa_e2], 0
    je .pdone
    mov si, [wa_ce0]
    cmp si, [wa_end]            ; (on the screen)
    jge .pdone
    cmp [wa_cov+si], dh         ; (and opened here)
    jne .pdone
    call .post
.pdone:
    mov [wa_lp], di
    ret
.post:                          ; SI = the column x 2
    push bx
    add bx, si
    mov cx, bp
    mov si, [wa_rowo2+2]        ; (a row's offset: row 1's)
.pr:
    mov [wa_stamp+bx], dl
    mov [wa_cur+bx], ax
    xchg ax, bx
    stosw
    xchg ax, bx
    add bx, si
    loop .pr
    pop bx
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
.steep:                         ; A STEEP EDGE: SPANLOOP's (wa_span.gen1),
    xor bh, bh                  ; on the edges' ends in wa_t1.. - a clipped
    mov bl, [wa_fv]             ; span's are there (wa_span: one of the
    mov si, bx                  ; face's vertices, wa_face's wa_fv, outside
    mov bl, [wa_fv+1]           ; the view); else the two vertices' (a
    mov di, bx                  ; segment inside one column has no step: it
    mov al, [wa_vok+si]         ; never comes here)
    and al, [wa_vok+di]
    jz .sgo
    mov ax, [wa_vpt+si]
    mov [wa_t1], ax
    mov ax, [wa_vpb+si]
    mov [wa_b1], ax
    mov ax, [wa_vpt+di]
    mov [wa_t2], ax
    mov ax, [wa_vpb+di]
    mov [wa_b2], ax
.sgo:
    jmp wa_span.gen1

; wa_collook - when the detail level or the sparse key changes, and after an
; arena is loaded (wa_lookset): wa_flat's level tests, from the detail level
wa_collook:
    cmp byte [wa_hflat], 0      ; A FLAT ARENA'S CLIP: the screen's end in
    je .hts                     ; every column, made once (heights clears
    cmp byte [wa_cflat], 0      ; its own each frame: wa_render) - so the
    jne .lv                     ; steep loop, wa_slope and wa_corner test the
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
; there as here). SI = the column the loop stood in. Clobbers BX, SI.
wa_stail:
    add bp, [wa_stl]
    adc cx, [wa_st]
    inc si
    inc si
; wa_openrest - the columns from SI to [wa_end] OPENED where they are open
; (wa_cov: DH, the span's tag), the shut ones counted (wa_ncl), nothing
; drawn: a span's columns once its edges have left the screen. Out: SI =
; the end. Keeps AX, CX, DX, DI and BP; clobbers BX.
wa_openrest:
    push ax
    push cx
    mov bx, di                  ; (the list)
    lea di, [si+wa_cov]
    mov cx, [wa_end]
    sub cx, si
    jbe .done
    shr cx, 1
    xor ax, ax
.l:
    scasw
    jne .s
    mov [di-2], dh
.n:
    loop .l
.done:
    mov si, [wa_end]
    mov di, bx
    pop cx
    pop ax
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
; pass, the fraction and its step stay in registers where SPANLOOP keeps the
; bottom in memory: pass 1 opens the columns (wa_cov 0 -> the tag) and draws
; the top (its column's first cell: no stamp test), pass 2 draws the bottom
; in the columns pass 1 opened - untested too where the wall is three
; half-rows tall or more (wa_flat's .thin: the edges share no cell); a thin
; wall's pass 2 is the clip's, which tests. The list's order is free: nothing
; reads it. A shut column costs a step, not a call (wa_gap). An edge that
; steps OFF the screen is off it for the rest of the span (it moves one way):
; pass 1 opens the rest (wa_openrest), pass 2 returns - and one off it at the
; first column and moving further off does so at once.
; FLATPASS %1 = 1 the opening pass, 0 the second; %2 = the edge's direction
; (+1 down the screen, -1 up); %3 = 1 the clip's (heights: a cell under a
; nearer low box's top not drawn, and every cell's stamp tested).
; In: SI = the first column x 2, BP = the edge's fraction (a pass's form) and
; [wa_fh] its half-row there, CX = the step's fraction, AH = the attribute,
; DL = the stamp, DH = the tag, DI = the list, [wa_end] = one past the last
; column x 2 - which pass 1 must find SHUT (wa_span's sentinel). Out: DI;
; BP and [wa_fh] one column past the end (an edge gone off the screen: off
; it, and BP = 0xFFFF); pass 1: SI = the end, and the columns it found shut
; counted into wa_ncl. Clobbers AL, BX, SI (the clip's: CX too).
%macro FLATPASS 2-3 0
%if %3
    mov [wa_fst], cx            ; (the clip's: the step in memory, the edge's
%endif                          ; half-row in CX - a column's clip test)
    mov bx, [wa_fh]             ; (wa_fcell, inline)
    cmp bx, WA_HR
    jae %%off0
%if %3
    mov cx, bx
%endif
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
    cmp cx, [wa_cclip+si]       ; (heights: under a nearer low box's top?)
    jae %%adv
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
%if %3
    add bp, [wa_fst]
%else
    add bp, cx
%endif
    jnc %%col
    mov bx, [%2]                ; the next half-row (wa_fcell, inline)
    add bx, [wa_fh]
    mov [wa_fh], bx
    cmp bx, WA_HR
    jae %%gone
%if %3
    mov cx, bx
%endif
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
%%off0:                         ; off the screen at the first column: and
    jcxz %%gone                 ; level, or moving further off (above it
    xor bx, [%2]                ; and going up, or under it and down)?
    jns %%gone
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
%if %3
    add bp, [wa_fst]
%else
    add bp, cx
%endif
    jnc %%off
    mov bx, [%2]
    add [wa_fh], bx
    call wa_fcell
    jc %%off
%if %3
    mov cx, [wa_fh]
%endif
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
%%gone:                         ; the edge has LEFT the screen: for good (it
    mov bp, 0xFFFF              ; moves one way) - no borrow for the far
%if %1                          ; corner's step back, and the rest of the
    jmp wa_openrest             ; columns opened, no more
%else
    ret
%endif
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
    add si, 2                   ; + e1 + a half: SI.BP = (e1 + 2) / 4
    mov bp, si
    ror bp, 1
    ror bp, 1
    and bp, 0xC000
    sar si, 1
    sar si, 1
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
    pop ax
.out:
    ret

; wa_occlow - BX = column x 2, AX = an inverse depth, CL = a half-row: CL
; lowered to the row the column's depth stack hides from at that depth -
; its entries front to back while they are nearer (wa_cst). Keeps AX, BX
; and BP; clobbers CH, DX, SI, DI (its askers have those saved, or free).
wa_occlow:
    mov ch, [wa_cstn+bx]        ; CH = the entries (none: nothing to lower
    or ch, ch                   ; it - most columns)
    jz .none
    push bp
    mov bp, ax                  ; BP = the depth asked about
    mov di, bx                  ; DI = entry k's place, less wa_cst
.e:
    mov ax, [wa_cst+di]         ; AL = its span's tag, AH = the row it
    mov dh, ah                  ; hides from
    xor ah, ah
    mov si, ax
    mov ax, [wa_spih+si]        ; its inverse depth here lies between its
    cmp ax, bp                  ; ends' (the entry is in one of its
    jbe .lo                     ; columns), so the ends often settle it:
    add ax, [wa_spsl+si]        ; both nearer...
    cmp ax, bp
    ja .near
.exact:
    mov ax, bx                  ; else as wa_wallz makes it
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, 8
    sub ax, [wa_spcs+si]
    imul word [wa_spsl+si]
    idiv word [wa_spsh+si]
    add ax, [wa_spih+si]
    cmp ax, bp
    jbe .done                   ; not nearer: nor is any after it
    mov dh, [wa_cst+di+1]       ; (the row again: the divide took DX)
.near:
    cmp dh, cl
    jae .next
    mov cl, dh
.next:
    add di, 2*WA_MAXW
    dec ch
    jnz .e
.done:
    mov ax, bp
    pop bp
.none:
    ret
.lo:
    add ax, [wa_spsl+si]        ; ...or neither
    cmp ax, bp
    ja .exact
    jmp .done

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
; depth cue that does not depend on a wall's height. Behind walls and under
; figures (drawn after). On by default from the High level up.
;
; The dots are a regular grid, so in camera space each one is the one before
; plus a constant: two running sums, a line's first dot in 32 bits and its
; dots in 16, holding its depth z (units) and its XK = x k16 / 64 - the
; column's scale folded into the sum,
; so the column is ONE multiply, W/2 + (XK x 1/z) >> 17, with 1/z out of
; wa_rcp (no divide). A dot outside the view leaves on two compares; one in
; it asks the column's wall for its depth only when the span's two ends do
; not settle it (wa_occl), and pays its second multiply (the row) only when
; it is not hidden. The first version made four products a grid line and a
; divide and three multiplies a dot, ~99,000 clk a frame on the play bench
; (docs/plans/MULTIPLAYER-FPS-PLAN.md 16.9).
WA_DOTSP    equ 128
wa_dots:
    cmp byte [wa_grid], 0
    jne .on
    ret
.on:
    mov ax, [wa_cos]            ; MS, MC = sin k16 >> 10, cos k16 >> 10: the
    call .mk16                  ; XK sum's multipliers (|x| k16 / 64 < 2^15
    mov [wa_dmc], ax            ; for a dot within 6 m at any width and view)
    mov ax, [wa_sinv]
    call .mk16
    mov [wa_dms], ax
    mov ax, [wa_cos]            ; the lines within three of the player's -
    mov bx, [wa_sinv]           ; less, on the axis the view runs along, the
    call .dir                   ; ones behind the player (.dir)
    mov ax, [wa_px]
    call .lines
    or cx, cx
    jnz .hx
    ret                         ; (no line ahead: against a wall)
.hx:
    mov [wa_dnx], cl
    push ax
    mov ax, [wa_sinv]
    mov bx, [wa_cos]
    call .dir
    mov ax, [wa_py]
    call .lines
    pop bx                      ; BX = X0 - px
    or cx, cx
    jnz .hy
    ret
.hy:
    mov [wa_dny], cl
    mov cx, ax                  ; CX = Y0 - py
    ; the first dot's sums: Z = (X0-px) 4 cos + (Y0-py) 4 sin (the high word
    ; is z), XK = ((X0-px) 64) MS - ((Y0-py) 64) MC
    mov ax, bx
    shl ax, 1
    shl ax, 1
    imul word [wa_cos]
    mov si, ax
    mov di, dx
    mov ax, cx
    shl ax, 1
    shl ax, 1
    imul word [wa_sinv]
    add si, ax
    adc di, dx
    mov [wa_drz], si
    mov [wa_drz+2], di
    push cx
    mov cl, 6
    shl bx, cl
    pop ax
    shl ax, cl
    imul word [wa_dmc]
    mov si, ax
    mov di, dx
    mov ax, bx
    imul word [wa_dms]
    sub ax, si
    sbb dx, di
    mov [wa_drx], ax
    mov [wa_drx+2], dx
    ; the steps: a line on in x adds 512 cos to Z and 8192 MS to XK; a line
    ; on in y adds 512 sin to Z and takes 8192 MC from XK (kept negated)
    mov ax, 512
    imul word [wa_cos]
    mov [wa_dxz], ax
    mov [wa_dxz+2], dx
    mov ax, 8192
    imul word [wa_dms]
    mov [wa_dxx], ax
    mov [wa_dxx+2], dx
    mov ax, [wa_sinv]           ; ...which a line's own dots take in 16 bits,
    sar ax, 1                   ; Z x 32 and XK x 2 (the steps sin / 4 and
    sar ax, 1                   ; -MC / 4: at most six a line, so under a
    mov [wa_dyz], ax            ; fifth of a unit of z adrift at its far end)
    mov ax, [wa_dmc]
    neg ax
    sar ax, 1
    sar ax, 1
    mov [wa_dyx], ax
    mov al, [wa_nshb]           ; the floor's half-row: hh + (numb x iz) >>
    add al, 2                   ; 21 (or >> 19 of numb / 4)
    mov [wa_dsh], al
    mov ax, [wa_w]
    shr ax, 1
    mov [wa_dwh], ax
    push bp
    mov di, [wa_lp]
.ix:
    mov si, [wa_drz+2]          ; a line of dots: SI = z x 32, BP = XK x 2
    mov ax, [wa_drz]
    mov cl, 5
    shl si, cl
    mov cl, 11
    shr ax, cl
    or si, ax
    mov ax, [wa_drx]
    shl ax, 1
    mov bp, [wa_drx+2]
    rcl bp, 1
    mov al, [wa_dny]
    mov [wa_dcnt], al
.dot:
    cmp si, 48*32
    jl .next                    ; behind, or at our feet
    mov bx, si
    mov cl, 5
    shr bx, cl                  ; BX = z
    mov ax, bx                  ; in the view: |x| k16 / z <= 8W, which is
    shl ax, 1                   ; |XK| <= z W / 8 - 5 z at 40 columns, 10 z
    shl ax, 1                   ; at 80 (and BP is XK x 2)
    add ax, bx
    shl ax, 1
    cmp byte [wa_w], 40
    je .w40
    shl ax, 1
.w40:
    mov dx, bp
    or dx, dx
    jns .xa
    neg dx
.xa:
    cmp dx, ax
    ja .next
    shl bx, 1
    mov cx, [wa_rcp+bx-WA_NEAR*2] ; CX = 1/z (z < 700: inside the table)
    mov ax, bp
    imul cx
    sar dx, 1                   ; (XK x iz) >> 17: columns from the centre
    sar dx, 1
    add dx, [wa_dwh]
    cmp dx, [wa_w]
    jae .next                   ; (negative is huge, unsigned)
    mov bx, dx
    shl bx, 1                   ; BX = the column x 2
    mov ax, cx                  ; AL = the first half-row hidden there (the
    call wa_occl                ; span's ends before the wall's own depth)
    or al, al
    jz .next                    ; a wall is nearer
    mov [wa_dlim], al
    mov ax, [wa_numb]
    imul cx
    mov cl, [wa_dsh]
    sar dx, cl
    add dx, [wa_hh]
    cmp dx, WA_HR
    jae .next
    cmp dl, [wa_dlim]
    jae .next                   ; under a nearer platform's band
    mov al, 0xFA                ; upper half: the middle dot...
    test dl, 1
    jz .gl
    mov al, '.'                 ; ...lower: the full stop
.gl:
    xchg bx, dx
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, dx                  ; BX = the cell
    mov ah, [wa_at+A_DOT]
    mov dl, [wa_frame]
    WPLOT
.next:
    add si, [wa_dyz]            ; the next dot of the line
    add bp, [wa_dyx]
    dec byte [wa_dcnt]
    jnz .dot
    mov ax, [wa_dxz]            ; the next line
    add [wa_drz], ax
    mov ax, [wa_dxz+2]
    adc [wa_drz+2], ax
    mov ax, [wa_dxx]
    add [wa_drx], ax
    mov ax, [wa_dxx+2]
    adc [wa_drx+2], ax
    dec byte [wa_dnx]
    jnz .ix
    mov [wa_lp], di
    pop bp
    ret
; .dir - AX = the view direction's component along an axis, BX = its other
; (Q14): DI = +1 when nothing in the view lies behind the player on that
; axis, -1 when nothing lies ahead, 0 when it has both. A point in the
; widest view (|x| <= z) lies z (a - |b|) to z (a + |b|) along the axis, so
; a >= |b| means none is behind: the half of the lines there are not walked
; at all - 21 of the 49 dots, whichever way the player faces. (A margin of
; 1/64 keeps a dot at the view's very edge.)
.dir:
    xor di, di
    or bx, bx
    jns .d0
    neg bx
.d0:
    add bx, 256
    cmp ax, bx
    jl .d1
    inc di
    ret
.d1:
    neg ax
    cmp ax, bx
    jl .d2
    dec di
.d2:
    ret
; .lines - AX = the player's coordinate, DI = .dir's answer for the axis: AX
; = the first grid line's coordinate less the player's, CX = how many lines
; (0: none). Clobbers BX, DX, SI
.lines:
    mov dx, ax                  ; DX = p
    mov cl, 7
    mov bx, ax
    shr bx, cl                  ; the line nearest below, roughly
    sub bx, 3
    jns .l0
    xor bx, bx
.l0:                            ; BX = the first line
    mov si, bx
    add si, 7
    cmp si, 16                  ; (the room is 16 lines across)
    jbe .l1
    mov si, 16
.l1:                            ; SI = one past the last
    or di, di
    jz .l4
    js .l2
    mov ax, dx                  ; ahead only: from the first line at or past
    add ax, 63                  ; the player, 64 + 128 k >= p
    shr ax, cl
    cmp ax, bx
    jbe .l4
    mov bx, ax
    jmp .l4
.l2:
    mov ax, dx                  ; behind only: to the last line at or before
    sub ax, 64
    jb .l5
    shr ax, cl
    inc ax
    cmp ax, si
    jae .l4
    mov si, ax
.l4:
    sub si, bx
    jg .l6
.l5:
    xor si, si
.l6:
    mov ax, bx
    shl ax, cl                  ; the line's coordinate: 64 + 128 k
    add ax, 64
    sub ax, dx
    mov cx, si
    ret
; .mk16 - AX = a Q14 sin or cos: AX = it x k16 >> 10
.mk16:
    imul word [wa_k16]
    mov cx, 6
.m1:
    shl ax, 1
    rcl dx, 1
    loop .m1
    mov ax, dx
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
    mov cx, 32                  ; (half a metre: too close to draw - and
    call wa_view                ; behind the eye: half the multiplies)
    pop di
    pop bx
    jc .nxt
    mov cx, dx
    add cx, 64
    mov si, ax
    or ax, ax
    jns .ax
    neg ax
.ax:
    cmp ax, cx
    jg .nxt                     ; outside the view, with a body's margin
    mov [wa_fxs+bx], si
    mov [wa_fzs+bx], dx
    mov [wa_frec+bx], di
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
; A near figure is drawn in four steps, each priced for the 8088:
;  1. ITS BOX, before a joint is made: the keyframe's bound (wa_ktop and
;     wa_krch, tools/arenagen.py: how high any joint is, and how far any
;     reaches to either side of the feet's line at this facing, over the
;     two keyframes of a blend) projected - three multiplies, where finding
;     the eleven joints' own least and most was forty-four compares - and
;     run by run of its columns whether the figure is in front of the wall
;     there: a run of columns one span drew (`repe scasw` finds its end)
;     takes one verdict when the span's ends bound it, and when they do not
;     the run's END column where the wall is nearest is asked exactly
;     (wa_wallz), which settles every column of it when the figure is in
;     front there, the wall's depth being monotonic along a span. Only a hidden column is written down (wa_fvis). Off the screen,
;     or behind the walls in every column: nothing more is done, and the
;     figure counts as not drawn;
;  2. its eleven joints, blended and projected - the blend specialised per
;     quarter (wa_fjl), the scale in a register;
;  3. its nine limbs and head rasterised into wa_fbuf, a cell-mask scratch
;     laid out like the screen, by a Bresenham whose state is its POSITION
;     IN THE CODE (wa_fline: one block per sub-cell quadrant, so a sub-pixel
;     is one `or byte [bx], imm` and a pointer step) - ~70 clk a sub-pixel
;     against ~700 for the per-sub-pixel clip, depth test and plot it was;
;     with the whole box on the screen, through wa_flinef and no clip tests;
;  4. the box swept with `repe scasw` (wa_femit): each cell the figure
;     touched is cleared out of wa_fbuf and plotted once - inline - however
;     many of its four sub-pixels the limbs set.
; wa_fbuf is all zeros between figures: step 4 clears exactly what step 3
; set, because every sub-pixel step 3 plots is on the screen and inside the
; box (a line stays inside its endpoints' box, each joint is inside the
; bound; the head's 2x2 reaches one up and one right, which the box allows
; for).
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
    mov ax, [di+F_ANG]          ; facing, relative to the view, of 16 (x 2:
    sub ax, [wa_cang]           ; >> 5, as << 3 and the high byte)
    add ax, 32
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov al, ah
    and ax, 30
    xchg ax, bx
    mov si, [wa_fcl+bx]         ; SI = the facing's row of reaches (wa_krch)
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
    jmp short .nx
.r16:                           ; (a figure at arm's length: 16 bits)
    xor ah, ah
    mul bx
    mov al, ah
    mov ah, dl
    jmp short .rd8
.nx:
    mov dl, [di+F_ANIM]
    add ax, dx
    add cx, dx
    add si, ax
    mov dl, [si]                ; DL = the reach to either side...
    mov si, cx
    shl si, 1
    mov si, [wa_kfo+si]
    add si, bx
    mov [wa_pb], si             ; keyframe B at this facing
    mov si, ax
    mov dh, [wa_ktop+si]        ; ...DH = the top
    mov cx, dx
    shl si, 1
    mov si, [wa_kfo+si]
    add si, bx
    mov [wa_pa], si             ; keyframe A
    mov ax, [wa_fs8]            ; K = fcx fs8 + 256 W: its feet's line,
    imul word [wa_fcx]          ; sub-columns x 256 (wa_fjl's too)
    add ah, [wa_w]
    adc dx, 0
    mov [wa_fk], al             ; Klo (its high byte stays 0)...
    mov al, ah
    mov ah, dl
    mov [wa_fkh], ax            ; ...and Khi
    mov bp, ax
    mov al, cl                  ; THE BOX. Its sub-columns: Khi - r to
    mov bx, [wa_fs8]            ; Khi + r + 1 (the head's 2x2 reaches one
    or bh, bh                   ; further), r = reach x fs8 / 256 + 1 (a
    jnz .r16                    ; byte by a byte nearly always)
    mul bl
    mov al, ah
    xor ah, ah
.rd8:
    inc ax
    mov bx, bp
    sub bx, ax                  ; BX = the left one
    mov dx, bp
    add dx, ax
    inc dx                      ; DX = the right one
    xor al, al                  ; AL = wa_foff: bit 0 a joint may be off the
    mov si, [wa_w2]             ; side, bit 1 above or below - limbs clip
    or bx, bx
    js .xoff
    cmp dx, si
    jl .xon
.xoff:
    inc ax
    or bx, bx
    jns .xon
    xor bx, bx
.xon:
    dec si
    cmp dx, si
    jle .cr
    mov dx, si
.cr:
    sar bx, 1                   ; columns
    sar dx, 1
    sub dx, bx
    jl .none                    ; all off the screen
    inc dx
    mov [wa_fbw], dx            ; the box's width, cells
    shl bx, 1
    mov [wa_fbc], bx            ; ...and its left column x 2
    mov bl, al
    mov ax, [wa_fv8]            ; its half-rows. The floor's: hh + (eye fv8
    imul word [wa_feye]         ; + 255 >> 8), a joint's very number at
    add ax, 255                 ; height 0 (wa_fjl)...
    adc dx, 0
    mov si, ax
    mov al, ah
    mov ah, dl
    add ax, [wa_hh]
    xchg ax, si                 ; SI = the floor's
    add ah, [wa_hh]             ; (E = eye fv8 + 256 hh + 255, mod 2^16:
    mov [wa_fe], ax             ; wa_fjl's narrow sum)
    mov al, ch                  ; ...and the top's: no higher than the
    mov cx, [wa_fv8]            ; floor's less (top fv8 >> 8) + 1, and one
    or ch, ch                   ; more for the head's 2x2
    jz .t8
    xor ah, ah
    mul cx
    jmp short .td8
.t8:
    mul cl
.td8:
    mov al, ah
    xor ah, ah
    neg ax
    add ax, si
    dec ax
    dec ax
    xchg ax, cx                 ; CX = the top
    mov ax, si
    or cx, cx
    js .yoff
    cmp ax, WA_HR
    jl .yon
.yoff:
    or bl, 2
    or cx, cx
    jns .ct
    xor cx, cx
.ct:
    cmp ax, WA_HR-1
    jle .yon
    mov ax, WA_HR-1
.yon:
    mov [wa_foff], bl
    and cl, 0xFE                ; half-rows -> row x 2 (wa_rowo2's index)
    and al, 0xFE
    cmp cx, ax
    jg .none
    mov [wa_fbr], ax            ; the last row x 2
    mov [wa_frow], cx           ; ...and the first
    push di                     ; (wa_fjdone pops it)
    mov bp, [wa_fiz]            ; the box's columns: is the figure in front
    mov di, [wa_fbc]            ; of the wall there? - and if not in one of
    add di, wa_cov              ; them, nothing of it shows: no joints, no
    mov cx, [wa_fbw]            ; limbs, no sweep. DI = a column's span tag
    xor dx, dx                  ; in wa_cov, CX = columns left, DL = the
.run:                           ; columns showing, DH = those hidden
    mov si, [di]                ; this run's span...
    mov ax, si
    mov bx, cx
    repe scasw                  ; ...to another's column, or the box's end
    je .rl
    inc cx                      ; (that column is the next run's)
    dec di
    dec di
.rl:
    sub bx, cx                  ; BX = the run's columns
    or si, si
    jz .vis                     ; no wall at all
    mov ax, [wa_spih+si]        ; A BOUND FIRST: a span opens only columns
    push dx                     ; whose centres lie inside its ends (wa_span),
    mov dx, ax                  ; so wa_wallz's value there lies between its
    add dx, [wa_spsl+si]        ; ends' inverse depths - and a figure in
    cmp ax, dx                  ; front of both, or behind both, needs no
    jbe .lo                     ; divide to say so
    xchg ax, dx
.lo:
    cmp dx, bp
    pop dx
    jbe .vis                    ; nearer than the whole span
    cmp ax, bp
    ja .hid                     ; farther than the whole span
    push cx                     ; BETWEEN: the run's END column where the
    mov cx, bx                  ; wall is nearest, exactly - not nearer than
    lea bx, [di-wa_cov-2]       ; the figure there, it is nowhere in the run
    mov ax, cx                  ; (its depth is monotonic along a span)
    dec ax
    shl ax, 1                   ; AX = from the run's first column to its
    cmp word [wa_spsl+si], 0    ; last, BX = the last
    jge .nr
    sub bx, ax                  ; (the wall comes nearer leftward: the first)
    neg ax
.nr:
    call .wz
    jae .bv
    sub bx, ax                  ; nearer there: the other end - nearer there
    call .wz                    ; too, the run is hidden
    jb .bh
    or ax, ax                   ; one of each: every column, from the first
    jns .mix
    add bx, ax
    jmp short .mix
.wz:                            ; wa_wallz's line for BX's column, the span
    push ax                     ; in SI: into wa_colz, and CF = 1: nearer
    push dx                     ; than the figure (BP). Keeps every register
    mov ax, bx
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, 8
    sub ax, [wa_spcs+si]
    imul word [wa_spsl+si]
    idiv word [wa_spsh+si]
    add ax, [wa_spih+si]
    mov [wa_colz+bx], ax
    cmp bp, ax
    pop dx
    pop ax
    ret
.bh:
    mov bx, cx
    pop cx
    jmp short .hid
.bv:
    mov bx, cx
    pop cx
.vis:
    add dl, bl                  ; (wa_fvis stays 0: nothing hidden)
    or cx, cx
    jnz .run
    jmp short .rd
.mix:                           ; (the wall passes the figure's depth inside
    call .wz                    ; the run: each column exactly)
    jae .mv
    mov byte [wa_fvis+bx], 1
    inc dh
    dec dl
.mv:
    inc dl
    inc bx
    inc bx
    loop .mix
    pop cx
    jmp short .rn
.hid:
    add dh, bl
    push di
    push cx
    mov cx, bx
    shl bx, 1
    sub di, bx
    add di, wa_fvis - wa_cov    ; the run's columns in wa_fvis: hidden from
    mov ax, 1                   ; half-row 0 down
    rep stosw
    pop cx
    pop di
.rn:
    or cx, cx
    jnz .run
.rd:
    cmp byte [wa_hflat], 0      ; heights: from which row a nearer platform
    jne .fl                     ; hides it, per column (wa_fvis)
    mov si, bp
    call wa_fthrs
.fl:
    mov [wa_fnh], dh
    or dl, dl
    jz .gone
    jmp wa_fjl                  ; the joints (and on to wa_fjdone)
.gone:
    call wa_fvclr               ; (nothing shows: wa_fvis clean again)
    pop di
.none:
    mov word [di+F_PZ], 0       ; not drawn: the trigger has nothing to hit
    ret
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
wa_fcl:                         ; ...and its class's row of wa_krch: the
%assign wa_i 0                  ; facing folded into a quarter turn
%rep WA_NFACE
  %assign wa_j wa_i % (WA_NFACE/2)
  %if wa_j > WA_NFACE/4
    %assign wa_j WA_NFACE/2 - wa_j
  %endif
    dw wa_krch + wa_j*WA_NFRAMES
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
    mov ax, [wa_fs8]            ; (20 units: fs8 x 20 / 256)
    mov cx, ax
    shl ax, 1
    shl ax, 1
    add ax, cx
    shl ax, 1
    shl ax, 1
    mov al, ah
    xor ah, ah
    or al, al
    jnz .w
    inc ax
.w:
    mov [di+F_PWH], ax
    push di
    mov ax, wa_fline
    cmp byte [wa_foff], 0
    jne .limbs
    mov ax, wa_flinef
.limbs:
    mov [wa_flfn], ax
    cmp byte [di+F_ANIM], WA_A_GIB
    jae .gib                    ; GIB: its joints alone, the bits
    cmp byte [di+F_SH], 0
    je .limbs0
    call .body                  ; A SHIELD (WEAPONS-PLAN 2.3): the figure
    jmp wa_fchest               ; swept as ever, then its chest recoloured
.body:                          ; (wa_femit pops this and returns)
    push di
.limbs0:
    cmp byte [wa_fsub], 4       ; (J: a small figure's ends alone)
    je .few
    WA_LIMB 2, 4                ; step 3: the limbs into wa_fbuf (joint x 2
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
    cmp word [wa_fv8], 40       ; big enough for a 2x2 head
    jae .head2
    call wa_fpix
    jmp wa_femit
.head2:
    cmp byte [wa_foff], 0
    jne .headc
    call wa_fpair               ; (all of it on the screen: two cell
    dec di                      ; lookups, not four clipped sub-pixels)
    call wa_fpair
    jmp wa_femit
.headc:
    call wa_fpix
    inc si
    call wa_fpix
    dec di
    call wa_fpix
    dec si
    call wa_fpix
    jmp wa_femit
.few:                           ; J: neck-pelvis, neck to each hand, pelvis
    WA_LIMB 2, 4                ; to each foot, straight
    WA_LIMB 2, 6
    WA_LIMB 2, 8
    WA_LIMB 4, 10
    WA_LIMB 4, 12
    jmp wa_femit                ; (no head: under a sub-pixel)
.gib:                           ; GIB (WEAPONS-PLAN 2.3): each joint alone,
    mov cx, WA_NJ               ; a 2x2 block when near (the head's) - the
    cmp byte [wa_fsub], 4       ; bits; the box allows for the reach, as for
    jne .g0                     ; the head (J: the seven joints it made)
    mov cl, 7
.g0:
    mov si, wa_jsx
.gj:
    push si
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
    loop .gj
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
; cell the limbs touched is taken out of wa_fbuf and plotted - the old
; wa_plotf inline, every array addressed off DI (the cell's place in
; wa_fbuf, + 2) by a constant displacement: the stamp to match in DL and the
; cell's sub-pixels in DH (one word store: wa_fmask is the stamp's odd
; byte), the glyph by XLAT off BX, the attribute in BP's high byte (its low
; one the box's width), the list's end in SI. The sweep asks nothing about depth: what a nearer wall
; or platform hides was taken out of wa_fbuf first (wa_fhide), and only
; when some column of the box had a verdict at all. Ends wa_figure: pops
; its DI.
WA_FSTO equ wa_stamp - wa_fbuf - 2
WA_FMSO equ wa_fmask - wa_fbuf - 2
WA_FCUO equ wa_cur - wa_fbuf - 2
wa_femit:
    cmp byte [wa_fnh], 0
    je .sweep
    call wa_fhide
.sweep:
    mov dl, [wa_frame]
    or dl, 0x80                 ; DL = a figure cell's stamp this frame
    mov si, [wa_newp]
    add si, [wa_newn]           ; SI = the cell list's end
    mov bx, [wa_frow]
    mov di, [wa_rowo2+bx]
    add di, [wa_fbc]
    add di, wa_fbuf             ; DI = the box's first cell in wa_fbuf
    mov ax, [wa_fbr]
    sub ax, bx
    shr ax, 1
    inc ax
    mov [wa_fnr], ax            ; rows
    mov ax, [wa_w]
    sub ax, [wa_fbw]
    shl ax, 1
    mov [wa_fsk], ax            ; from the box's end to its start a row on
    mov ah, [wa_fatt]
    mov al, [wa_fbw]
    mov bp, ax                  ; BP = the attribute, and the box's width
    mov bx, wa_fglyph
    xor ax, ax
    jmp short .first
.row:
    add di, [wa_fsk]
.first:
    mov cx, bp
    xor ch, ch
    repe scasw                  ; (only a cell's even byte is ever set)
    jne .found
.nrow:
    dec word [wa_fnr]
    jnz .row
    sub si, [wa_newp]
    mov [wa_newn], si
    pop di
    ret
.found:                         ; DI = the cell's place in wa_fbuf, + 2
    mov dh, [di-2]              ; DH = its sub-pixels, taken out of the
    mov [di-2], ah              ; scratch (AX is 0)
    mov al, [di+WA_FSTO]
    xor al, dl                  ; (its stamp against DL: 0 a figure's cell
    shl al, 1                   ; this frame, 0x80 a wall's - one test for
    jz .drawn                   ; the two, and the usual cell is neither)
    lea ax, [di-2-wa_fbuf]      ; the cell, onto this frame's list
    mov [si], ax
    inc si
    inc si
.listed:
    mov [di+WA_FSTO], dx        ; its stamp and its mask
    mov ax, bp
    mov al, dh
    xlat
    mov [di+WA_FCUO], ax
    xor ax, ax
    jcxz .nrow
    repe scasw
    jne .found
    jmp .nrow
.drawn:
    jc .listed                  ; a wall cell this frame: already listed
    or dh, [di+WA_FMSO]         ; (a cell two figures share: add the bits)
    jmp .listed

; wa_fhide - a figure's limbs are in wa_fbuf: out of it again the cells a
; nearer wall or platform hides. wa_fvis has, per column x 2 of the box, 0
; (nothing hidden: nearly every column) or the first hidden half-row + 1 -
; a wall hides from 0, a platform from its lip (wa_fthrs); a cell row is
; hidden when its top half-row is. Leaves wa_fvis all 0 again. Clobbers AX,
; BX, CX, DX, SI.
wa_fhide:
    mov bx, [wa_fbc]
    mov cx, [wa_fbw]
.c:
    mov al, [wa_fvis+bx]
    or al, al
    jz .n
    mov byte [wa_fvis+bx], 0
    and ax, 0x00FE              ; the first hidden row x 2
    cmp ax, [wa_frow]
    jae .r
    mov ax, [wa_frow]
.r:
    mov dx, [wa_fbr]
    sub dx, ax
    jb .n                       ; (hidden below the box alone)
    shr dx, 1
    inc dx                      ; DX = rows
    mov si, ax
    mov si, [wa_rowo2+si]
    lea si, [si+bx+wa_fbuf]     ; SI = the column's first hidden cell
    mov ax, [wa_w2]
.z:
    mov byte [si], 0
    add si, ax
    dec dx
    jnz .z
.n:
    inc bx
    inc bx
    loop .c
    ret

; wa_fvclr - wa_fvis all 0 again over the box (a figure wholly hidden).
; Clobbers AX, CX, DI.
wa_fvclr:
    mov di, [wa_fbc]
    add di, wa_fvis
    mov cx, [wa_fbw]
    xor ax, ax
    rep stosw
    ret

; wa_fthrs - heights, once a figure: per column of its box with a platform
; in it at all (wa_cstn: `repe scasw` passes the rest, most) and not behind
; the column's tall face, the first half-row a nearer platform hides
; (wa_occlow) + 1 into wa_fvis - 1 where nothing of it shows, which takes
; the column out (DL the columns showing less one); DH the columns with a
; verdict more one. SI = the figure's inverse depth. Clobbers AX, BX, CX, DI.
wa_fthrs:
    mov di, [wa_fbc]
    add di, wa_cstn
    mov cx, [wa_fbw]
.s:
    xor ax, ax
    repe scasw
    je .out
    lea bx, [di-2-wa_cstn]      ; BX = the column x 2
    cmp byte [wa_fvis+bx], 0
    jne .n                      ; (its tall face hid it)
    push cx
    push dx
    push si
    push di
    mov cl, 0xFF
    mov ax, si
    call wa_occlow
    mov al, cl
    pop di
    pop si
    pop dx
    pop cx
    inc al
    jz .n                       ; (0xFF: nothing hidden)
    mov [wa_fvis+bx], al
    inc dh
    dec al
    jnz .n
    dec dl                      ; (from the top: gone)
.n:
    or cx, cx
    jnz .s
.out:
    ret

; wa_fjl - wa_figure's joints: SI, DI = keyframes A and B at this facing.
; Each is A + (B - A) x q / 4 (q = [wa_fsub], rounded down as wa_blend's
; was; q = 3 is worked as B + (A - B) / 4, the same number), projected:
; x = ((lateral + fcx) fs8 >> 8) + W, y = hh - ((height - eye) fv8 >> 8).
; Eight loops, one per quarter and per multiply:
; - NARROW (fs8 under 256 and fv8 under 128: past ~1.4 m at 40 columns and
;   ~1.6 m at 80, most of the time): 8-bit multiplies on sums regrouped so
;   they stay in 16 bits - the lateral taken + 128, unsigned, and K = (fcx -
;   128) fs8 + 256 W split as 256 Khi + Klo, so x = (((lateral + 128) fs8 +
;   Klo) >> 8) + Khi; and E = eye fv8 + 256 hh + 255, so y = (E - height
;   fv8) >> 8. The same numbers, so the same pixels;
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
%macro WA_FJ 2                  ; wide, %1 = q (%2 = 0, the label's): CX =
.j%2%1:                         ; fs8, BP = fv8
    WA_FBLEND %1, 0             ; lateral
    add ax, [wa_fcx]
    imul cx
    mov al, ah
    mov ah, dl
    add ax, [wa_w]
    mov [wa_jsx+bx], ax
    WA_FBLEND %1, 1             ; height
    sub ax, [wa_feye]
    imul bp
    mov al, ah
    mov ah, dl
    neg ax
    add ax, [wa_hh]
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
    add al, 0x80                ; (the lateral + 128, unsigned: K is less
    mul cl                      ; 128 fs8 to match, and fs8 may be to 255)
    add ax, [wa_fk]
    mov al, ah
    xor ah, ah
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
    jmp wa_fjdone
%endmacro
wa_fjl:
    ; STKBALANCE-OK: never called - wa_figure's continuation, JMPed to with
    ; wa_figure's DI on the stack; its table dispatch (`jmp ax`) and every
    ; joint block leave by `jmp wa_fjdone`, which pops it
    xor bx, bx
    mov bl, [wa_fsub]
    shl bx, 1
    mov si, [wa_pa]
    mov ax, [wa_fs8]
    mov dx, [wa_fv8]
    or ah, ah
    jnz .wide
    cmp dx, [wa_fvn]            ; (128, or less where the narrow sum would
    jae .wide                   ; leave 16 bits: wa_render - the bound is
    cmp word [wa_feye], 0       ; the camera's eye's, so a figure's own, at
    jl .wide                    ; most that, is under it unless below 0)
    mov cl, al
    mov ch, dl
    mov bp, [wa_fkh]            ; (K and E: wa_figure made them.) K less
    shr ax, 1                   ; 128 fs8, for the laterals + 128: Khi less
    mov dh, 0                   ; fs8 >> 1, Klo less 128 when fs8 is odd,
    rcr dh, 1                   ; and the borrow
    sub [wa_fk], dh
    sbb bp, ax
    mov ax, [.tab+8+bx]
    mov bx, [wa_pb]
    sub bx, si                  ; BX = B - A
    mov di, wa_jsx
    jmp ax
.wide:
    mov cx, ax                  ; (the scales in registers)
    mov bp, dx
    and bx, 6                   ; (J's q 4 is q 0 here: all eleven joints)
    mov ax, [.tab+bx]
    mov di, [wa_pb]
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
    WA_FJN 0, r, 7              ; J: the first seven alone

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

; wa_fpair - SI = sub-column, DI = half-row: that sub-pixel and the one to
; its right into wa_fbuf, both on the screen (the head's 2x2, a row of it).
; Keeps SI and DI; clobbers AX, BX.
wa_fpair:
    mov bx, di
    shl bx, 1
    mov bx, [wa_hrfb+bx]
    mov ax, si
    and al, 0xFE
    add bx, ax                  ; BX = the cell
    mov ax, di
    shr ax, 1
    mov al, 3                   ; an upper half-row: the cell's upper
    jnc .u                      ; quadrants... or its lower
    mov al, 12
.u:
    test si, 1
    jz .put
    and al, 0x0A                ; the cell's right half, and the next
    or [bx], al                 ; cell's left
    shr al, 1
    inc bx
    inc bx
.put:
    or [bx], al
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
; The blocks lie in the order the pen USUALLY takes them - a steep line goes
; down its column's upper quadrant into its lower, a shallow one along its
; row's - so the usual step falls through and only the minor axis's step,
; out of line, jumps.
%macro WA_FYP 2                 ; steep: %1 R/L, %2 c - the column's two
wa_fy%{1}0%{2}:                     ; quadrants, upper then lower
    or byte [bx], 1 << %2
    dec cx
    jz wa_fy%{1}ret
    sub bp, si
    js %%s0                     ; (a sideways step too)
wa_fy%{1}1%{2}:
    or byte [bx], 4 << %2
    dec cx
    jz wa_fy%{1}ret
    sub bp, si
    js %%s1
    add bx, ax                  ; down: the next row's upper quadrant
    jmp wa_fy%{1}0%{2}
%%s0:
    add bp, di
    WA_FSIDE %1, %2
  %if %2
    jmp wa_fy%{1}10
  %else
    jmp wa_fy%{1}11
  %endif
%%s1:
    add bp, di
    WA_FSIDE %1, %2
    add bx, ax
  %if %2
    jmp wa_fy%{1}00
  %else
    jmp wa_fy%{1}01
  %endif
%endmacro
%macro WA_FSIDE 2               ; a sub-column on, %1 R/L, from column half %2:
  %ifidn %1, R                  ; into the next cell from its outer half
    %if %2 == 1
    inc bx
    inc bx
    %endif
  %else
    %if %2 == 0
    dec bx
    dec bx
    %endif
  %endif
%endmacro
%macro WA_FXP 4                 ; shallow: %1 R/L, %2 r, %3 the half the pen
wa_fx%{1}%{2}%{3}:                    ; enters a cell by, %4 the other - the
    or byte [bx], 1 << (%3 + 2*%2) ; row's two quadrants in its order
    dec cx
    jz wa_fx%{1}ret
    sub bp, si
    js %%d0                     ; (a step down too)
wa_fx%{1}%{2}%{4}:
    or byte [bx], 1 << (%4 + 2*%2)
    dec cx
    jz wa_fx%{1}ret
    WA_FSIDE %1, %4             ; ...on into the next cell
    sub bp, si
    js %%d1
    jmp wa_fx%{1}%{2}%{3}
%%d0:
    add bp, di
  %if %2
    add bx, ax
    jmp wa_fx%{1}0%{4}
  %else
    jmp wa_fx%{1}1%{4}
  %endif
%%d1:
    add bp, di
  %if %2
    add bx, ax
    jmp wa_fx%{1}0%{3}
  %else
    jmp wa_fx%{1}1%{3}
  %endif
%endmacro
wa_fyRret:
    ret
    WA_FYP R, 0
    WA_FYP R, 1
wa_fyLret:
    ret
    WA_FYP L, 0
    WA_FYP L, 1
wa_fxRret:
    ret
    WA_FXP R, 0, 0, 1
    WA_FXP R, 1, 0, 1
wa_fxLret:
    ret
    WA_FXP L, 0, 1, 0
    WA_FXP L, 1, 1, 0
wa_fl_ytab:                     ; by (r x 2 + c) x 2, leftward + 8
    dw wa_fyR00, wa_fyR01, wa_fyR10, wa_fyR11
    dw wa_fyL00, wa_fyL01, wa_fyL10, wa_fyL11
wa_fl_xtab:
    dw wa_fxR00, wa_fxR01, wa_fxR10, wa_fxR11
    dw wa_fxL00, wa_fxL01, wa_fxL10, wa_fxL11

; wa_marker - DI = a figure too far for limbs: a one-sub-column bar from
; head to feet, its attribute already chosen. The trigger's extents too -
; unless its column is off the screen or the wall there is nearer, when it
; stops at that, not drawn (F_PZ 0), before its height is worked out.
wa_marker:
    mov ax, [wa_fcx]
    imul word [wa_fs8]
    mov al, ah
    mov ah, dl
    add ax, [wa_w]
    mov [di+F_PCX], ax
    mov si, ax                  ; SI = the sub-column
    cmp si, [wa_w2]
    jae .none                   ; (unsigned: off either side)
    mov bx, si
    and bx, 0xFFFE              ; BX = its column x 2: ONE verdict for
    mov ax, [wa_fiz]            ; the whole bar (the span's ends first:
    call wa_thocc               ; wa_occl's very answer)
    or al, al
    jz .none                    ; the wall there is nearer
    mov ch, al                  ; CH = the first half-row a platform hides
    mov ax, [wa_feye]           ; the feet, the floor: hh + (eye fv8 >> 8)
    imul word [wa_fv8]
    mov cl, al                  ; (CL = that product's low byte)
    mov al, ah
    mov ah, dl
    add ax, [wa_hh]
    mov [di+F_PBOT], ax
    mov [wa_mbot], ax
    mov al, 112                 ; the head, 1.75 m: hh - ((112 - eye) fv8
    mul byte [wa_fv8]           ; >> 8), which is the feet's less ((112 fv8
    mov dl, cl                  ; - that low byte) >> 8) - a byte multiply
    xor dh, dh                  ; (fv8 is under 9), the same number
    sub ax, dx
    mov al, ah
    cbw
    neg ax
    add ax, [wa_mbot]
    mov [di+F_PTOP], ax
    mov word [di+F_PWH], 1
    mov dx, ax                  ; DX = top half-row
    mov al, ch
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
    mov cx, dx
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
    ret
.none:
    mov word [di+F_PZ], 0       ; not drawn: the trigger has nothing to hit
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
    mov si, wa_pks
.p:                             ; AL = the pickups still to draw, a bit each
    shr al, 1
    jnc .next
    push ax
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
    pop ax
.next:
    add si, WA_PKSZ
    or al, al
    jnz .p
.out:
    ret

; wa_thshape - BX = a shape row (wa_pkshape's): into wa_thw..wa_tha (which
; lie in its order), the attribute the adapter's. Keeps all but AX, BX, DI
wa_thshape:
    xchg si, bx
    mov di, wa_thw
    movsw
    movsw
    movsw
    lodsw                       ; (the colour's attribute, the mono one)
    cmp byte [wa_mono], 0
    je .c
    mov al, ah
.c:
    stosb
    mov si, bx
    ret

; wa_rockets - after the figures: each rocket a bright dot, each explosion a
; block growing over its ticks, yellow and red by turns
wa_rockets:
    mov al, [wa_rks+WA_RKST]    ; (none in flight, none exploding: nearly
%assign wa_i 1                  ; every frame)
%rep WA_NRK-1
    or al, [wa_rks+wa_i*WA_RKSZ+WA_RKST]
%assign wa_i wa_i+1
%endrep
    jz .ret
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
.ret:
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
    mov cx, 24
    call wa_view                ; AX = camera x, DX = z
    jc .out                     ; (at the eye, or behind it)
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

; wa_view - AX, BX = a world point, CX = the least z worth having: its place
; in view space, z FIRST - one behind the eye (half of them, turning round)
; costs two multiplies, not four. CF = 1: its z is under CX; else AX =
; camera x (right), DX = z (ahead), the walls' own arithmetic. Clobbers BX,
; CX, SI, DI.
wa_view:
    sub ax, [wa_px]
    sub bx, [wa_py]
    mov [wa_thdx], ax
    mov di, cx
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
    cmp dx, di
    jl .out
    mov di, dx
    mov ax, [wa_thdx]           ; x = (dx sin - dy cos) >> 14
    imul word [wa_sinv]
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
    clc
    ret
.out:
    stc
    ret

; wa_thocc - BX = a column x 2, AX = an inverse depth: wa_occl's answer (AL
; = the first half-row something nearer hides there, 0 all of it, 0xFF
; none), with the figures' BOUND first (wa_figure): the column's tall face
; lies between its span's ends' inverse depths, so a thing nearer than both,
; or farther than both, needs no divide to say so. Keeps all but AL
wa_occl:
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
    mov al, 0xFF                ; nothing tall: what a platform hides
    cmp byte [wa_hflat], 0
    jne .out
    push cx
    mov cl, al
    call wa_occlow
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
    cmp byte [wa_ovfresh], 0
    jne .fresh
    mov cx, [wa_ovoldn]         ; (the overlay's cells, as it came in)
    jcxz .out
    mov si, wa_ovold            ; ...each's STAMP's place
.c:
    lodsw
    xchg ax, bx
    mov al, [bx]
    and al, 0x7F
    cmp al, dl
    je .put                     ; the view drew here: listed, ours again
    mov [bx], dl                ; (kept off present's old list's blanks)
    loop .c
    ret
.put:
    mov ax, [si+wa_ovg-wa_ovold-2]
    mov [bx+wa_cur-wa_stamp], ax
    loop .c
.out:
    ret
.fresh:
    mov di, [wa_newp]
    add di, [wa_newn]           ; DI = this frame's list's end
    mov cx, [wa_ovoldn]         ; the last overlay's cells, back to the view:
    jcxz .new                   ; listed and blank, unless the view drew them
    mov si, wa_ovold
.o:
    lodsw
    xchg ax, bx                 ; BX = the cell's stamp's place
    mov al, [bx]
    and al, 0x7F
    cmp al, dl
    je .on
    mov [bx], dl
    lea ax, [bx-wa_stamp]
    stosw
    mov word [bx+wa_cur-wa_stamp], WA_BLANK
.on:
    loop .o
.new:
    mov cx, [wa_ovn]            ; ...then this one's, every cell listed
    mov [wa_ovoldn], cx
    jcxz .done
    mov si, wa_ov
.n:
    lodsw
    mov bx, ax                  ; BX = the cell
    add ax, wa_stamp            ; (its stamp's place: the frames to come)
    mov [si+wa_ovold-wa_ov-2], ax
    mov al, [wa_stamp+bx]
    and al, 0x7F
    cmp al, dl
    je .nput
    mov [wa_stamp+bx], dl
    mov [di], bx
    inc di
    inc di
.nput:
    mov ax, [si+WA_OVGD]
    mov [wa_cur+bx], ax
    loop .n
.done:
    sub di, [wa_newp]
    mov [wa_newn], di
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
; Write this frame's cells; blank last frame's cells this frame did not draw.
; Nothing else. Each list is walked four cells an iteration, entered part-way
; for the remainder (a jump table, once), so the loop's own cost is a quarter
; of a `loop` a cell.
; - A new cell is WRITTEN, whatever the glass holds. It used to be compared
;   with the glass first (a read of text VRAM) and written only where it
;   differed - but in play two cells in three have changed (the view moves),
;   so the compare was a VRAM read and a branch for every cell to save a
;   write for one in three: 77 clk a changed cell and 69 an unchanged one,
;   against 53 for the write alone. The card sees FEWER bus cycles this way
;   (a write a cell, where there was a read a cell and a write for most),
;   which on a real IBM CGA at 80 columns is less snow, not more: there a
;   read snows as a write does. Writing a cell the value it holds shows
;   nothing. (Before that the glass had a RAM copy, wa_vis: 3,840 bytes and
;   a second write a cell.)
; - An old cell's usual case is DRAWN AGAIN (85 of 121 on the P bench), and
;   by a wall, so that is one compare against the wall's stamp falling
;   through; a figure's stamp and the blank are out of line. It is written without
;   asking whether the glass is blank already: an old-list cell was drawn last
;   frame, so it is not, and if it were, the write is a no-op.
; wa_nwr, the meter's N, counts the writes made: every listed cell and every
; blank (it used to count the cells that had changed).
%macro PNEW 0                   ; this frame's cell: written
    lodsw
    xchg bx, ax
    mov ax, [bx+di]             ; (DI = wa_cur)
    mov [es:bx], ax
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
    mov si, [wa_newp]
    mov di, wa_cur
    mov cx, [wa_newn]           ; bytes: 2 a cell
    mov bp, cx
    shr bp, 1                   ; BP = writes, for the meter
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
