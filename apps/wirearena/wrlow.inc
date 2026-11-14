; WireArena's OUTLINE path: boxes lower than the arena's tallest
; (docs/plans/WIREARENA-HEIGHTS-PLAN.md 1.3, 7.3.3, 7.3.4). Included by the
; renderer part after wrender.inc, whose vertex cache, span table, column
; state and plotting it shares.
;
; A tall box is a wall: its faces shut their columns (wa_span). A LOW box
; leaves them open above its top, so what stands behind it still shows
; there, and from an eye above it its top shows too. It is drawn as its
; outline, each cell tested against the column's clip - the topmost
; half-row something nearer already covers - and the clip then raised to the
; box's own top, with an entry on the column's depth stack (its span tag,
; the row it covers from) for the figures, the floor grid and the pistol's
; ray (wa_occl):
;  1. the posts at the visible corners, against the clip as it stood;
;  2. the faces the player is outside of, a pass each: the floor edge drawn
;     against the clip as it stood, then the top edge, raising it;
;  3. with the eye above the box, its other faces' top edges - the far side
;     of its top - raising it again;
;  4. a FLIGHT (kind 1) is a RAMP (HEIGHTS-PLAN 7.4.2): its low end's
;     corners on the floor, its sides straight from there to its top, its
;     low face one edge, and no treads. With the T option (wa_treads) it is
;     drawn STEPPED, as wave 2 drew a flight: its low end a face as tall as
;     the first tread, its inner treads lines across it at their heights,
;     nearest first. The walk is the ramp either way (wa_hat).
; A top edge at or above the screen's top shuts its column, wa_cov holding
; its tag as a wall's does. Each corner keeps its own height (wl_ch): a
; ramp's low-end corners stand on the floor. A corner in
; the view is projected once a box (wl_kc..wl_kf); an edge with a corner out
; of it is clipped (wa_clip) and its ends projected on the spot.

;
; LOOK OPTIONS (the detail levels, wa_look - HEIGHTS-PLAN 9.11): FARTOP
; draws the far side of a box's top only with the eye WL_FARTOPV units or
; more above it (a word wa_lookset sets each frame: 0 when off, the same
; arithmetic); RAMP2 (Low and Very Low) draws a ramp without its low end's
; floor line (its high end's top stays: it shuts the slope's columns), one
; byte test a flight. (r5's WL_NOPOSTS
; and WL_NEARPOST, rejected by the owner, are gone.)
WL_FARTOPV  equ 64

; a corner's record in wl_c (WL_REC bytes, x 16 its index): its vertex x 2,
; its top's (product, shift) pair, and - projected, in the view - its column
; x 16, top row, inverse depth and floor row, in wl_e1c's order
WL_CV       equ 0
WL_CN       equ 2
WL_CS       equ 4
WL_KC       equ 6
WL_KT       equ 8
WL_KI       equ 10
WL_KF       equ 12
WL_REC      equ 16

%macro WL_BSS 0
WA_VAR wl_h, 2                  ; its height (a flight's, its top tread's)
WA_VAR wl_kind, 1               ; its kind byte and faces byte (wa_bxh)
WA_VAR wl_lom, 1                ; a ramp's low face's bit (else 0): drawn
                                ; as one edge, its top on the floor
WA_VAR wl_faces, 1
WA_VAR wl_seen, 1               ; the faces the player is outside of: bits
WA_VAR wl_front, 1              ; ...of them, those drawn (posts, floors)
WA_VAR wl_topb, 1               ; the faces' top bits
WA_VAR wl_need, 1               ; the corners projected: bits
WA_VAR wl_fneed, 1              ; ...of them, those with a floor row
WA_VAR wl_m, 1                  ; a mask being walked
WA_VAR wl_pm, 1                 ; ...and the faces given a pass
WA_VAR wl_c, 4*WL_REC           ; its corners (WL_*)
WA_VAR wl_fn, 2                 ; the floor's pair (made in the frame
WA_VAR wl_fs, 1                 ; wl_ffr)
WA_VAR wl_ffr, 1
WA_VAR wl_hn, 2                 ; the box's height's
WA_VAR wl_hs, 1
WA_VAR wl_hh, 2                 ; ...that height (-1: none this frame)
WA_VAR wl_vk, 1                 ; the corners projected in the view: bits
WA_VAR wl_two, 1                ; ...and draws a floor edge with its top
WA_VAR wl_n1, 2                 ; a clipped edge's ends' pairs
WA_VAR wl_s1, 1
WA_VAR wl_n2, 2
WA_VAR wl_s2, 1
WA_VAR wl_e1c, 2                ; a pass's ends: column x 16, top row,
WA_VAR wl_e1r, 2                ; inverse depth, floor row - adjacent, in
WA_VAR wl_e1i, 2                ; this order (wl_face, wl_proj, wl_draw)
WA_VAR wl_e1f, 2
WA_VAR wl_e2c, 2
WA_VAR wl_e2r, 2
WA_VAR wl_e2i, 2
WA_VAR wl_e2f, 2
WA_VAR wl_stw, 2                ; the top's step a column (its value: CX.BP)
WA_VAR wl_stf, 2
WA_VAR wl_bsw, 2                ; the floor edge's step
WA_VAR wl_bsf, 2
WA_VAR wl_col0, 2               ; a pass's first column x 2
WA_VAR wl_end, 2                ; one past the last column, x 2
WA_VAR wl_prev, 2               ; the column before's half-rows (a steep
WA_VAR wl_bprev, 2              ; step)
WA_VAR wl_tag, 1
WA_VAR wl_k, 2                  ; a flight: the treads left, the count, the
WA_VAR wl_nt, 2                 ; rise, the low and high faces, and a
WA_VAR wl_rise, 2               ; tread's step along each side, camera
WA_VAR wl_lo, 1                 ; space
WA_VAR wl_hi, 1
WA_VAR wl_dxa, 2
WA_VAR wl_dza, 2
WA_VAR wl_dxb, 2
WA_VAR wl_dzb, 2
WA_VAR wl_ca, 2                 ; ...its low end's two corners (records)
WA_VAR wl_cb, 2
WA_VAR wl_tx1, 2                ; ...and the tread being drawn: its ends,
WA_VAR wl_tz1, 2                ; its height, the height's step, and 1
WA_VAR wl_tx2, 2                ; walking down from the top
WA_VAR wl_tz2, 2
WA_VAR wl_th, 2
WA_VAR wl_hst, 2
WA_VAR wl_back, 1
WA_VAR wl_fartop, 2             ; FARTOP: 0, or WL_FARTOPV (wa_lookset)
%endmacro

; wl_crn - per set of faces (S 1, E 2, N 4, W 8): the corners they run
; between, face f from corner f to f + 1; wl_bit - a face's bit, and the
; next one round's
wl_crn:
%assign wl_i 0
%rep 16
    db (wl_i | (wl_i << 1) | (wl_i >> 3)) & 15
%assign wl_i wl_i+1
%endrep
wl_bit:     db 1, 2, 4, 8, 1

; WLCORNER %1 = a corner: wa_lowbox's, for one corner (BP = the corner
; indices; clobbers AX, BX, CX, DX, SI, DI)
%macro WLCORNER 1
    test byte [wl_need], 1 << %1
    jz %%n
    xor bx, bx
    mov bl, [ds:bp+%1]
    mov [wl_c+%1*WL_REC+WL_CV], bx
    mov al, [wa_frame]
    cmp [wa_vst+bx], al
    je %%c
    call [wa_vertp]             ; (the level's: wa_vertr at ROUND, where the
%%c:                            ; products' low words are not even made)
    test byte [wa_vok+bx], 1
    jz %%n
    or byte [wl_vk], 1 << %1
    mov ax, [wa_vpc+bx]
    mov [wl_c+%1*WL_REC+WL_KC], ax
    mov bx, [wa_vz+bx]
    cmp bx, WA_ZMAX             ; (wl_iz, inline)
    jae %%div
    shl bx, 1
    mov bx, [wa_rcp+bx-WA_NEAR*2]
%%iz:
    mov [wl_c+%1*WL_REC+WL_KI], bx
    mov ax, [wl_c+%1*WL_REC+WL_CN]
    mov cl, [wl_c+%1*WL_REC+WL_CS]
    imul bx
    sar dx, cl
    mov ax, [wa_hq]
    sub ax, dx
    mov [wl_c+%1*WL_REC+WL_KT], ax
    test byte [wl_fneed], 1 << %1
    jz %%n
    mov ax, [wl_fn]
    mov cl, [wl_fs]
    imul bx
    sar dx, cl
    mov ax, [wa_hq]
    sub ax, dx
    mov [wl_c+%1*WL_REC+WL_KF], ax
    jmp short %%n
%%div:
    call wl_iz.div
    jmp short %%iz
%%n:
%endmacro

; WLPOST, WLPASS2, WLPASS3 %1 = a face: wa_lowbox's three walks over the
; faces, one face each (r5): its posts; its seen pass, with the floor edge
; where [wl_m] has its bit; its far top's pass. [wl_pm] the faces given a
; pass
%macro WLPOST 1
    test byte [wl_front], 1 << %1
    jz %%n
    mov si, %1 * WL_REC         ; its start corner's
    mov cx, 7
    call wl_post
    test byte [wl_seen], 1 << ((%1 + 1) & 3)
    jnz %%n
    mov si, ((%1 + 1) & 3) * WL_REC
    mov cx, 7-16                ; ...and its end's
    call wl_post
%%n:
%endmacro
%macro WLPASS2 1
    test byte [wl_pm], 1 << %1
    jz %%n
    mov al, [wl_m]
    and al, 1 << %1
    mov [wl_two], al
    mov si, %1 * WL_REC
    mov di, ((%1 + 1) & 3) * WL_REC
    mov al, (1 << %1) | (1 << ((%1 + 1) & 3))
    call wl_face
%%n:
%endmacro
%macro WLPASS3 1
    test byte [wl_pm], 1 << %1
    jz %%n
    mov si, %1 * WL_REC
    mov di, ((%1 + 1) & 3) * WL_REC
    mov al, (1 << %1) | (1 << ((%1 + 1) & 3))
    call wl_face
%%n:
%endmacro

; wa_lowbox - from wa_leaf: SI = a low box, DI = its record x 4 in wa_bxh,
; [wa_crow] its corners, [wa_wcol] its colour (ES = DS, as all through the
; frame)
wa_lowbox:
    mov ax, [wa_bxh+di]
    mov [wl_h], ax
    mov ax, [wa_bxh+di+2]
    mov [wl_faces], ah
    mov ah, al                  ; the kind: only a FLIGHT (kind 1) keeps it -
    and ah, 3                   ; every flight test below is "kind & 3 is
    cmp ah, 1                   ; not 0", and a LIFT (kind 2, its index in
    je .kind                    ; bits 4-7) taken for one divided its height
    xor al, al                  ; by that index as a tread count: lift 0's
.kind:                          ; divide fault, every frame it was in view
    mov [wl_kind], al           ; (HEIGHTS-PLAN 9.5). A lift draws as the
                                ; low box it is at its current height
    mov byte [wl_lom], 0
    xor bl, bl                  ; the faces seen, as wa_box chooses them
    mov ax, [wa_py]             ; (S 1, E 2, N 4, W 8)
    cmp ax, [si+2]
    jge .s1
    or bl, 1
    jmp .ew
.s1:
    cmp ax, [si+6]
    jle .ew
    or bl, 4
.ew:
    mov ax, [wa_px]
    cmp ax, [si+4]
    jle .w1
    or bl, 2
    jmp .sd
.w1:
    cmp ax, [si]
    jge .sd
    or bl, 8
.sd:
    mov [wl_seen], bl
    mov al, bl
    and al, [wl_faces]
    mov [wl_front], al          ; the faces drawn: their posts, floor edges
    mov bx, wl_crn
    xlatb
    mov [wl_fneed], al          ; ...and the corners that need a floor row
    mov al, [wl_faces]
    mov cl, 4
    shr al, cl
    mov [wl_topb], al
    mov al, 15                  ; THE CORNERS WANTED: all four with the eye
    mov cx, [wa_eye]            ; above the top (its far side is drawn) or
    sub cx, [wl_fartop]         ; (FARTOP: n units above it)
    cmp cx, [wl_h]              ; for a flight (its treads span it), else
    jg .need                    ; only the seen faces'
    test byte [wl_kind], 3
    jnz .need
    mov al, [wl_seen]
    xlatb
.need:
    mov [wl_need], al
    mov byte [wl_vk], 0
    mov al, [wa_frame]          ; the floor's pair, once a frame (wa_render
    cmp al, [wl_ffr]            ; clears wl_ffr) - and the box's own
    je .hpair                   ; height's, kept for the next box of that
    mov [wl_ffr], al            ; height in the frame
    xor ax, ax
    call wa_hnum
    mov [wl_fn], ax
    mov [wl_fs], bl
    mov word [wl_hh], -1
.hpair:
    mov ax, [wl_h]              ; the box's own height's, every corner's -
    cmp ax, [wl_hh]             ; but a flight's low end's (wl_flight0)
    je .hhave
    mov [wl_hh], ax
    call wa_hnum
    mov [wl_hn], ax
    mov [wl_hs], bl
.hhave:
    mov ax, [wl_hn]
    mov bl, [wl_hs]
    mov [wl_c+0*WL_REC+WL_CN], ax
    mov [wl_c+1*WL_REC+WL_CN], ax
    mov [wl_c+2*WL_REC+WL_CN], ax
    mov [wl_c+3*WL_REC+WL_CN], ax
    mov [wl_c+0*WL_REC+WL_CS], bl
    mov [wl_c+1*WL_REC+WL_CS], bl
    mov [wl_c+2*WL_REC+WL_CS], bl
    mov [wl_c+3*WL_REC+WL_CS], bl
    test byte [wl_kind], 3
    jz .cor
    call wl_flight0
    test byte [wa_look], LK_RAMP2 ; (RAMP2: a ramp, not stepped, without
    jz .cor                     ; its low end's floor line - its high end's
    cmp word [wl_nt], 2         ; top stays: it closes the slope's columns,
    jae .cor                    ; and without it the platform's face behind
    mov bl, [wl_lo]             ; showed through - the owner, 2026-11-14)
    xor bh, bh
    mov al, [wl_bit+bx]
    not al
    and [wl_front], al
    and [wl_topb], al
.cor:
    ; EACH CORNER WANTED (r5: one pass, unrolled): transformed through the
    ; shared vertex cache, then, in the view, its column, inverse depth and
    ; top row, and for a drawn face's its floor row (wa_ptbi's arithmetic,
    ; the pairs scaled by 4 where they would pass 16 bits). BP = the box's
    ; corner indices (wa_vert keeps BP and BX)
    mov bp, [wa_crow]
    WLCORNER 0
    WLCORNER 1
    WLCORNER 2
    WLCORNER 3
.posts:
    ; 1. POSTS, against the clip as it stood: a drawn face's start corner,
    ; and its end where the next face round is not seen (wa_boxf's rule).
    ; (r5: each of the three walks over the faces unrolled, its masks made
    ; once)
    WLPOST 0
    WLPOST 1
    WLPOST 2
    WLPOST 3
.p1d:
    ; 2. THE SEEN FACES, a pass each: the floor edge, then the top raising
    ; the clip - and a seen face hidden against a taller neighbour still
    ; shows its top's crease where its top bit says. A ramp's low face is
    ; its one edge
    mov al, [wl_front]
    mov ah, al
    not ah
    and ah, [wl_topb]           ; AH = the seen faces' top creases...
    mov dl, [wl_lom]
    and dl, al
    or ah, dl                   ; ...and a ramp's low face: one edge each
    not dl
    and al, dl                  ; AL = the faces drawn with a floor edge
    and al, [wl_seen]
    and ah, [wl_seen]
    mov [wl_m], al
    or ah, al
    mov [wl_pm], ah             ; the seen faces given a pass
    WLPASS2 0
    WLPASS2 1
    WLPASS2 2
    WLPASS2 3
    ; (a flight seen from its low end: its inner treads are nearer than the
    ; far side of its top, and the depth stack keeps its entries in order)
    call wl_lowseen
    jz .p3s
    call wl_treads
.p3s:
    ; 3. THE FAR SIDE OF ITS TOP, with the eye above the box
    mov ax, [wa_eye]
    sub ax, [wl_fartop]         ; (FARTOP)
    cmp ax, [wl_h]
    jle .p3d
    mov al, [wl_seen]           ; (the faces not seen whose tops are drawn)
    not al
    and al, [wl_topb]
    mov [wl_pm], al
    mov byte [wl_two], 0
    WLPASS3 0
    WLPASS3 1
    WLPASS3 2
    WLPASS3 3
.p3d:
    ; 4. A FLIGHT'S INNER TREADS, seen from anywhere but its low end
    test byte [wl_kind], 3
    jz .done
    call wl_lowseen
    jnz .done
    call wl_treads
.done:
    ret

; wl_lowseen - ZF = 0 when the box is a flight and the player stands past
; its low end. Clobbers AL, CL
wl_lowseen:
    test byte [wl_kind], 3
    jz .out
    mov al, [wl_seen]
    mov cl, [wl_lo]
    shr al, cl
    test al, 1
.out:
    ret

; wl_ends - BX = a face (0 S, 1 E, 2 N, 3 W): SI, DI = its start and end
; corners' records (offsets into wl_c). Keeps BX; clobbers CL
wl_ends:
    mov si, bx
    mov cl, 4
    shl si, cl
    lea di, [si+WL_REC]
    and di, 3*WL_REC
    ret

; wl_face - SI, DI = a face's start and end corners' records (offsets into
; wl_c), AL their two bits, [wl_two] 1 its floor edge too: its pass, raising
; the clip
wl_face:
    mov ah, al
    and ah, [wl_vk]
    cmp ah, al
    jne .clip
    add si, wl_c+WL_KC          ; both corners in the view: the two
    add di, wl_c+WL_KC          ; records' projections, in place (wl_draw
    jmp wl_draw                 ; culls a pass hidden in all its columns)
.clip:
    mov ax, [wl_c+si+WL_CN]     ; a corner out of the view: the edge clipped,
    mov [wl_n1], ax             ; its ends projected at their corners'
    mov ax, [wl_c+di+WL_CN]     ; heights
    mov [wl_n2], ax
    mov al, [wl_c+si+WL_CS]
    mov [wl_s1], al
    mov al, [wl_c+di+WL_CS]
    mov [wl_s2], al
    mov bx, [wl_c+si+WL_CV]
    mov si, [wl_c+di+WL_CV]
    mov ax, [wa_vx+bx]
    mov [wa_x1], ax
    mov ax, [wa_vz+bx]
    mov [wa_z1], ax
    mov ax, [wa_vx+si]
    mov [wa_x2], ax
    mov ax, [wa_vz+si]
    mov [wa_z2], ax
    jmp wl_xedge

; wl_post - SI = a corner's record (an offset into wl_c), CX = 7 for a
; face's start corner (the first column whose centre is past it) or 7 - 16
; for its end (the last short of it): its post, `│` from the top to the
; floor, against the clip
wl_post:                        ; SI = the corner's record, CX = 7 (or 7 - 16
    mov bx, [wl_c+si+WL_CV]     ; for the end's rule: one column less)
    test byte [wa_vok+bx], 1
    jz .out                     ; (not in the view: off the screen, or behind)
    mov bx, [wl_c+si+WL_KT]     ; top and floor, half-rows, rounded
    add bx, 2
    sar bx, 1
    sar bx, 1
    mov ax, [wl_c+si+WL_KF]
    add ax, 2
    sar ax, 1
    sar ax, 1
    mov dx, ax                  ; (no cell between them, whatever the clip:
    sar dx, 1                   ; the floor's row at most one under the
    mov di, bx                  ; top's - a ramp's low corners, on the
    sar di, 1                   ; floor, and every short post - r5)
    inc di
    cmp dx, di
    jle .out
    mov di, [wl_c+si+WL_KC]     ; its column: as wa_span's corners
    add di, cx
    mov cl, 4
    sar di, cl
    js .out
    cmp di, [wa_w]
    jge .out
    shl di, 1                   ; DI = the column x 2
    cmp word [wa_cov+di], 0
    jne .out                    ; (shut)
    mov si, di                  ; SI = the column x 2
    cmp ax, [wa_cclip+si]
    jle .clp
    mov ax, [wa_cclip+si]
.clp:
    mov di, [wa_lp]
    mov dl, [wa_frame]
    mov cl, 1
    call wa_corner              ; (wa_vedge: every cell tested)
    mov [wa_lp], di
.out:
    ret

; wl_iz - BX = z (>= NEAR) -> BX = the inverse depth (wa_rcp's). Clobbers
; AX, DX
wl_iz:
    cmp bx, WA_ZMAX
    jae .div
    shl bx, 1
    mov bx, [wa_rcp+bx-WA_NEAR*2]
    ret
.div:
    mov ax, 0xFFF0              ; (BX >= WA_ZMAX > 7 = DX: no fault, and the
    mov dx, 0x0007              ; quotient fits)
    div bx
    mov bx, ax
    ret

; wl_row - BX = an inverse depth, AX, CL = a height's (product, shift)
; pair -> AX = its row, quarter half-rows: hq - (product x iz) >> 16 >> CL.
; Clobbers DX
wl_row:
    imul bx
    sar dx, cl
    mov ax, [wa_hq]
    sub ax, dx
    ret

; wl_xedge - [wa_x1],[wa_z1] - [wa_x2],[wa_z2] in camera space, [wl_n1]/
; [wl_s1] and [wl_n2]/[wl_s2] the ends' heights' pairs, [wl_two]:
; clipped (the near plane, the guard band), projected and drawn
wl_xedge:
    call wa_clip
    jc .out
.in:                            ; (both ends inside already: wl_treads)
    mov di, wl_e1c
    mov ax, [wa_x1]
    mov bx, [wa_z1]
    mov cx, [wl_n1]
    mov dl, [wl_s1]
    call wl_proj
    mov ax, [wa_x2]
    mov bx, [wa_z2]
    mov cx, [wl_n2]
    mov dl, [wl_s2]
    call wl_proj
    mov si, wl_e1c
    mov di, wl_e2c
    jmp wl_draw
.out:
    ret

; wl_proj - AX = x, BX = z (z >= NEAR, |x| <= 8z: wa_clip's), CX, DL = a
; height's pair, DI = wl_e1c or wl_e2c: its column x 16, that height's row,
; the inverse depth and, with [wl_two], the floor's row (quarter half-rows),
; DI past them. Clobbers AX, BX, CX, DX, SI
wl_proj:
    mov si, cx                  ; SI = the product, CH = the shift
    mov ch, dl
    imul word [wa_k16]
    idiv bx                     ; (z >= NEAR: no fault; |x| <= 8z: it fits)
    add ax, [wa_c16]
    stosw
    call wl_iz
    mov ax, si
    mov cl, ch
    imul bx
    sar dx, cl
    mov ax, [wa_hq]
    sub ax, dx
    stosw
    mov ax, bx
    stosw
    cmp byte [wl_two], 0
    je .nof                     ; (no floor edge: the word is not read)
    mov ax, [wl_fn]
    mov cl, [wl_fs]
    imul bx
    sar dx, cl
    mov ax, [wa_hq]
    sub ax, dx
.nof:
    stosw
    ret

; wl_draw - a pass over the columns between two ends, SI and DI: each the
; address of a (column x 16, top row, inverse depth, floor row) quad - a
; corner's record at WL_KC, or wl_e1c/wl_e2c (wl_xedge's ends): from
; the first column whose centre is past the left end to the last short of
; the right, in each the top edge plotted where it is above the column's
; clip (and the column not shut), with [wl_two] the floor edge too, a steep
; step between two columns filled; the clip raised to the top
; and a stack entry pushed (a top at or above the screen's shuts the column).
; A pass whose edges lie at or under every one of its columns' clips draws
; nothing and raises nothing, so it is CULLED before its edges are set up:
; neither edge's interpolation leaves the range of its two ends, and each
; is drawn, and raises, only above the clip (the steep fill too: wa_slope
; stops at the clip); a shut column draws nothing either. It registers no
; tag, the one thing it would have left.
wl_draw:
    mov ax, [si]
    mov bp, [di]
    cmp ax, bp
    jle .ord
    xchg si, di                 ; (left to right)
    xchg ax, bp
.ord:                           ; AX = the left end, BP = the right
    mov dx, 4*WA_HR-1           ; a top wholly under the screen (both ends
    cmp [si+2], dx              ; past the last half-row, with the walk's
    jl .walk                    ; rounding to spare: its columns lie
    cmp [di+2], dx              ; between them) draws and raises nothing,
    jl .walk                    ; and nor does a floor edge under it: no
    cmp byte [wl_two], 0        ; pass (hs-cols)
    je .none
    cmp [si+6], dx
    jl .walk
    cmp [di+6], dx
    jge .none
.walk:
    mov cl, 4
    mov bx, ax
    add bx, 7
    sar bx, cl                  ; the first column
    mov dx, bp
    add dx, 7
    sar dx, cl
    dec dx                      ; the last
    sub bp, ax                  ; BP = dc
    or bx, bx
    jns .lok
    xor bx, bx
.lok:
    cmp dx, [wa_w]
    jl .rok
    mov dx, [wa_w]
    dec dx
.rok:
    cmp bx, dx
    jg .none
    shl bx, 1                   ; (x 2 from here)
    shl dx, 1
    mov cx, dx
    sub cx, bx
    shr cx, 1
    inc cx                      ; CX = the columns
    inc dx
    inc dx
    mov [wl_end], dx
    push ax
    mov dx, [si+2]          ; THE CULL: the highest half-row either edge
    cmp dx, [di+2]          ; reaches (an end's, rounded as the loops
    jle .c1                     ; round)...
    mov dx, [di+2]
.c1:
    cmp byte [wl_two], 0
    je .c3
    cmp dx, [si+6]
    jle .c2
    mov dx, [si+6]
.c2:
    cmp dx, [di+6]
    jle .c3
    mov dx, [di+6]
.c3:
    add dx, 2
    sar dx, 1
    sar dx, 1
    push si
    lea si, [wa_cclip+bx]
.scan:
    lodsw                       ; ...against each column's clip
    cmp ax, dx
    jg .hit
.snext:
    loop .scan
    pop si
    pop ax
.none:
    ret                         ; (hidden throughout: nothing to do)
.hit:
    cmp word [si+wa_cov-wa_cclip-2], 0
    jne .snext                  ; (a shut column shows nothing either)
    pop si
    pop cx                      ; CX = the left end
    mov [wa_dc], bp
    mov ax, bx                  ; the first centre past the left end, x 16
    push bx                     ; (the first column, for the loop)
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, 8
    sub ax, cx
    mov [wa_fs], ax
    mov bx, [wa_tag]            ; its tag (wa_span.reg's): its inverse
                                ; depth's line
    add bx, 2
    cmp bx, WA_MAXSP*2
    jae .full
    mov [wa_tag], bx
    mov [wa_spcs+bx], cx
    mov [wa_spsh+bx], bp
    mov ax, [si+4]
    mov [wa_spih+bx], ax
    neg ax
    add ax, [di+4]
    mov [wa_spsl+bx], ax
    mov [wl_tag], bl
    pop word [wl_col0]          ; (the first column x 2)
    push word [si+2]            ; (the top's ends, for after the floor's)
    push word [di+2]
    cmp byte [wl_two], 0        ; THE FLOOR EDGE first, a loop of its own
    je .top                     ; (r5): it is drawn against the clip as it
    mov ax, [si+6]              ; stood, which the top raises only after it
    mov bx, [di+6]              ; in each column - and each column is its
    mov byte [wa_steep], 0      ; own, so all the floor's cells and then all
    call wa_span.edge           ; the top's are the same cells, plotted in
    mov [wl_bsw], dx            ; the same order within a column. Its start
    mov [wl_bsf], ax            ; and step are wa_span's own (exact at the
    mov bp, cx                  ; first column; half-rows with a 16-bit
    mov cx, bx                  ; fraction, the whole part rounded): CX.BP
    mov si, [wl_col0]
    mov di, [wa_lp]
    mov dl, [wa_frame]
    mov bx, wl_f0
    cmp byte [wa_steep], 0
    je .f
    mov bx, wl_f1
.f:
    call bx
    mov [wa_lp], di
.top:
    pop bx                      ; THE TOP, raising the clip
    pop ax
    mov byte [wa_steep], 0
    call wa_span.edge
    mov [wl_stw], dx
    mov [wl_stf], ax
    mov bp, cx                  ; CX.BP
    mov cx, bx
    mov si, [wl_col0]
    mov di, [wa_lp]
    mov dl, [wa_frame]
    mov bx, wl_l00
    cmp byte [wa_steep], 0
    je .t
    mov bx, wl_l01s
.t:
    call bx
    mov [wa_lp], di
    ret
.full:
    pop bx
    mov word [wa_left], 0       ; (more spans than the table holds: drawing
    ret                         ; stops this frame - never seen)

; WLLOOP %1 = 1 the edge may be steep: wl_draw's top edge, its columns (SI
; from the first x 2 to [wl_end]; CX.BP the top; DL the stamp, DI the list). For the columns AH holds the colour and DH counts them down.
; A column's top is ONE signed test against its clip (the clip is 1..WA_HR in
; an open column): at or under it nothing is drawn nor raised; over it the
; cell is plotted when on the screen, the clip raised and an entry pushed - or,
; at or over the screen's top, the column shut (wl_shutc). Where the top is
; shallow the clip is tested first: a column it is hidden in needs no shut
; test. A steep step is filled where the whole half-rows of two
; neighbouring columns are two or more apart - which a shallow edge's never
; are, so the test stands for the flag - unless both ends are at or under the
; clip, where wa_slope fills nothing; a shut column leaves the next column's
; own value as the one before it, so no step is filled across it.
%macro WLFILL 2                 ; %1 = this column's edge (a register), %2 =
    mov bx, %1                  ; the last's (a word): wa_slope down a step
    sub bx, %2
    inc bx                      ; -1..+1 -> 0..2: no step
    cmp bx, 3
    jb %%no
    mov bx, %2
    cmp bx, %1
    jl %%min
    mov bx, %1
%%min:
    cmp bx, [wa_cclip+si]
    jge %%no
    push cx
    mov ax, %1
    mov cx, %2
    call wa_slope
    pop cx
    mov ah, [wa_wcol]
%%no:
%endmacro

%macro WLLOOP 1
    mov ah, [wa_wcol]
    mov bx, [wl_end]
    sub bx, si
    shr bx, 1
    mov dh, bl
%if %1
    mov [wl_prev], cx
    jmp short %%col
%%shut:
    add bp, [wl_stf]            ; (the next column's own values are the ones
    adc cx, [wl_stw]            ; before it: no step filled across this one)
    mov [wl_prev], cx
    jmp %%adv
%%col:
    cmp byte [wa_cov+si], 0
    jne %%shut                  ; (shut)
    WLFILL cx, [wl_prev]        ; the top's step
    mov [wl_prev], cx
%else
%%col:
%endif
    cmp cx, [wa_cclip+si]
    jge %%next                  ; under something nearer, or below the screen
%if %1 == 0
    cmp byte [wa_cov+si], 0
    jne %%next                  ; (shut)
%endif
    or cx, cx
    js %%over                   ; (over the screen's top: shut, nothing drawn)
    mov bx, cx
    mov al, [wa_hgl+bx]
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, si
    WPLOT
    jcxz %%over                 ; (the top row: shut)
    mov [wa_cclip+si], cx
    mov bx, [wa_cstn+si]        ; the depth stack: (tag, row) on top, or,
    cmp bl, WA_CSTK             ; full, merged into the deepest - the
    jae %%full                  ; nearer tag kept, so a figure between the
    inc bx                      ; two can only be hidden a frame too soon
    mov [wa_cstn+si], bx
    shl bx, 1
    mov bx, [wl_k160-2+bx]
    mov al, [wl_tag]
    mov [wa_cst+bx+si], al
    mov [wa_cst+bx+si+1], cl
%%next:
    add bp, [wl_stf]
    adc cx, [wl_stw]
%%adv:
    inc si
    inc si
    dec dh
    jnz %%col
    ret
%%full:
    mov [wa_cst+(WA_CSTK-1)*2*WA_MAXW+si+1], cl
    jmp %%next
%%over:
    call wl_shutc
    jmp %%next
%endmacro
wl_l00:     WLLOOP 0
wl_l01s:    WLLOOP 1

; WLFLOOR %1 = 1 the edge may be steep: a floor edge alone (wl_draw's, r5),
; its columns from SI to [wl_end], CX.BP its value, [wl_bsw].[wl_bsf] its
; step; each cell plotted where it is under the column's clip (ONE unsigned
; test: a row off either side reads as past it) and the column not shut, a
; steep step filled down to the clip; nothing raised
%macro WLFLOOR 1
    mov ah, [wa_wcol]
    mov bx, [wl_end]
    sub bx, si
    shr bx, 1
    mov dh, bl
%if %1
    mov [wl_bprev], cx
    jmp short %%col
%%shut:
    add bp, [wl_bsf]            ; (the next column's own value is the one
    adc cx, [wl_bsw]            ; before it: no step filled across this one)
    mov [wl_bprev], cx
    jmp short %%adv
%endif
%%col:
    cmp byte [wa_cov+si], 0
%if %1
    jne %%shut                  ; (shut)
    push cx                     ; its step
    mov ax, cx
    mov cx, [wl_bprev]
    mov [wl_bprev], ax
    call wl_fillc
    pop cx
    mov ah, [wa_wcol]
%else
    jne %%next                  ; (shut)
%endif
    cmp cx, [wa_cclip+si]
    jae %%next
    mov bx, cx
    mov al, [wa_hgl+bx]
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, si
    WPLOT
%%next:
    add bp, [wl_bsf]
    adc cx, [wl_bsw]
%%adv:
    inc si
    inc si
    dec dh
    jnz %%col
    ret
%endmacro
wl_f0:      WLFLOOR 0
wl_f1:      WLFLOOR 1
wl_k160:    dw 0, 2*WA_MAXW, 4*WA_MAXW, 6*WA_MAXW

; wl_fillc - AX = a column's edge, CX = the last column's (half-rows), SI =
; the column x 2: WLFILL's step, out of line. Clobbers AX, BX, CX
wl_fillc:
    mov bx, ax
    sub bx, cx
    inc bx
    cmp bx, 3
    jb .no
    mov bx, cx
    cmp bx, ax
    jl .min
    mov bx, ax
.min:
    cmp bx, [wa_cclip+si]
    jge .no
    jmp wa_slope
.no:
    ret

; wl_shutc - SI = a column x 2: the column covered from the screen's top -
; shut, wa_cov holding the tag as a wall's does - its clip 0, and its stack
; entry pushed (row 0), or merged into the deepest. Clobbers AL, BX
wl_shutc:
    mov bl, [wl_tag]
    xor bh, bh
    mov [wa_cov+si], bx
    dec word [wa_left]
    xor bx, bx
    mov [wa_cclip+si], bx
    mov bx, [wa_cstn+si]
    cmp bl, WA_CSTK
    jae .full
    inc bx
    mov [wa_cstn+si], bx
    shl bx, 1
    mov bx, [wl_k160-2+bx]
    mov al, [wl_tag]
    mov [wa_cst+bx+si], al
    mov byte [wa_cst+bx+si+1], 0
    ret
.full:
    mov byte [wa_cst+(WA_CSTK-1)*2*WA_MAXW+si+1], 0
    ret

; --- flights --------------------------------------------------------------
; wl_flight0 - a flight's corners: the low end's two take its first tread's
; height's pair, its high end's its top's (wa_lowbox's). [wl_lo], [wl_hi]
; the two ends' faces; [wl_nt], [wl_rise] its treads and their rise
wl_flight0:
    mov al, [wl_kind]
    mov cl, 2
    shr al, cl
    and al, 3                   ; the face it rises TOWARD
    mov [wl_hi], al
    xor al, 2
    mov [wl_lo], al
    xor bh, bh                  ; the low face's two corners
    mov bl, al
    call wl_ends
    cmp byte [wa_treads], 0     ; A RAMP (the default, HEIGHTS-PLAN 7.4.2):
    jne .steps                  ; its low face's corners on the floor, its
    mov word [wl_nt], 0         ; sides straight from there to its top, no
    mov al, [wl_bit+bx]         ; treads, and that face drawn as its one
    mov [wl_lom], al            ; edge (its top IS its floor edge)
    mov ax, [wl_fn]
    mov bl, [wl_fs]
    jmp .set
.steps:                         ; STEPPED: the low face as tall as the first
    mov al, [wl_kind]           ; tread, the treads lines across it
    mov cl, 4
    shr al, cl
    xor ah, ah
    mov [wl_nt], ax
    mov cx, ax                  ; (1..15: arenagen refuses any other count,
    mov ax, [wl_h]              ; and only a flight keeps its kind - so no
    xor dx, dx                  ; divide fault, HEIGHTS-PLAN 9.5)
    div cx
    mov [wl_rise], ax
    call wa_hnum
.set:
    mov [wl_c+si+WL_CN], ax
    mov [wl_c+di+WL_CN], ax
    mov [wl_c+si+WL_CS], bl
    mov [wl_c+di+WL_CS], bl
    ret

; wl_treads - a flight's inner treads, 2..N: each a line across it at its
; height, between points on its two sides - a tread's going along each, in
; camera space, made once - nearest first, from whichever end the player
; stands at. The low face runs corner A to B; A's side goes to the corner
; before A, B's to the corner after B (the high face's two). Each tread's
; ends and height are the last's plus a step, the same bits as k - 1 goings
; and k rises made afresh
wl_treads:
    cmp word [wl_nt], 2
    jb .out
    xor bh, bh
    mov bl, [wl_lo]
    call wl_ends                ; SI = A, DI = B (records)
    mov [wl_ca], si
    mov [wl_cb], di
    mov bx, [wl_c+si+WL_CV]
    sub si, WL_REC
    and si, 3*WL_REC            ; A's side's far corner
    mov si, [wl_c+si+WL_CV]
    call wl_going
    mov [wl_dxa], ax
    mov [wl_dza], dx
    mov si, [wl_cb]
    mov bx, [wl_c+si+WL_CV]
    add si, WL_REC
    and si, 3*WL_REC            ; B's side's
    mov si, [wl_c+si+WL_CV]
    call wl_going
    mov [wl_dxb], ax
    mov [wl_dzb], dx
    mov byte [wl_two], 0
    mov ax, [wl_nt]
    dec ax
    mov [wl_k], ax              ; N - 1 treads
    call wl_lowseen             ; from the low end when the player is past
    mov cx, 1                   ; it - tread 2, one going out - else from
    mov ax, [wl_rise]           ; the top, tread N, stepping back
    mov byte [wl_back], 0
    jnz .first
    mov cx, [wl_k]
    neg ax
    mov byte [wl_back], 1
.first:
    mov [wl_hst], ax
    mov ax, cx                  ; the first's height: k rises
    inc ax
    mul word [wl_rise]
    mov [wl_th], ax
    mov si, [wl_ca]             ; ...its ends: k - 1 goings along each side
    mov si, [wl_c+si+WL_CV]
    mov ax, [wl_dxa]
    imul cx
    add ax, [wa_vx+si]
    mov [wl_tx1], ax
    mov ax, [wl_dza]
    imul cx
    add ax, [wa_vz+si]
    mov [wl_tz1], ax
    mov si, [wl_cb]
    mov si, [wl_c+si+WL_CV]
    mov ax, [wl_dxb]
    imul cx
    add ax, [wa_vx+si]
    mov [wl_tx2], ax
    mov ax, [wl_dzb]
    imul cx
    add ax, [wa_vz+si]
    mov [wl_tz2], ax
    cmp byte [wl_back], 0       ; (stepping back: the goings turned round)
    je .t
    neg word [wl_dxa]
    neg word [wl_dza]
    neg word [wl_dxb]
    neg word [wl_dzb]
.t:
    mov ax, [wl_th]             ; its height's pair, both ends (the top
    cmp ax, [wl_h]              ; tread's the box's)
    jne .th
    mov ax, [wl_hn]
    mov bl, [wl_hs]
    jmp .tp
.th:
    call wa_hnum
.tp:
    mov [wl_n1], ax
    mov [wl_n2], ax
    mov [wl_s1], bl
    mov [wl_s2], bl
    mov si, wl_tx1              ; its ends, for wa_clip to cut (wa_x1, z1,
    mov di, wa_x1               ; x2, z2 adjacent, as ours)
    movsw
    movsw
    movsw
    movsw
    mov cl, 3                   ; both ends inside the near plane and the
    mov ax, [wl_tz1]            ; guard band (wa_clip's tests): no cut to
    cmp ax, WA_NEAR             ; make
    jl .clip
    shl ax, cl
    mov dx, [wl_tx1]
    cmp dx, ax
    jg .clip
    neg ax
    cmp dx, ax
    jl .clip
    mov ax, [wl_tz2]
    cmp ax, WA_NEAR
    jl .clip
    shl ax, cl
    mov dx, [wl_tx2]
    cmp dx, ax
    jg .clip
    neg ax
    cmp dx, ax
    jl .clip
    call wl_xedge.in
    jmp .next
.clip:
    call wl_xedge
.next:
    mov ax, [wl_hst]            ; the next
    add [wl_th], ax
    mov ax, [wl_dxa]
    add [wl_tx1], ax
    mov ax, [wl_dza]
    add [wl_tz1], ax
    mov ax, [wl_dxb]
    add [wl_tx2], ax
    mov ax, [wl_dzb]
    add [wl_tz2], ax
    dec word [wl_k]
    jnz .t
.out:
    ret

; wl_going - BX, SI = the vertices x 2 at a side's two ends: AX, DX = a
; tread's going along it, camera x and z ([wl_nt] >= 2: no fault)
wl_going:
    mov ax, [wa_vx+si]
    sub ax, [wa_vx+bx]
    cwd
    idiv word [wl_nt]
    push ax
    mov ax, [wa_vz+si]
    sub ax, [wa_vz+bx]
    cwd
    idiv word [wl_nt]
    mov dx, ax
    pop ax
    ret
