; WireArena's OUTLINE path: boxes lower than the arena's tallest
; (docs/plans/completed/WIREARENA-HEIGHTS-PLAN.md 1.3, 7.3.3, 7.3.4). Included by the
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
;  1. the posts at the visible corners, against the clip as it stood (each
;     with its face's pass: it stands in a column no other face's reaches);
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
; its tag as a wall's does. Each corner keeps its own height (WL_CN, WL_CS):
; a ramp's low-end corners stand on the floor. A corner in
; the view is projected once a box (WL_KC..WL_KF); an edge with a corner out
; of it is clipped (wa_clip) and its ends projected on the spot - unless it
; lies under the screen whole (wl_face).
; An edge is walked across its columns in RUNS of one half-row (wl_run,
; wl_rtop, wl_rfloor): set up without a divide where its left end is on the
; screen; a steep one, a half-row a column or more, keeps a column loop
; (wl_l01s, wl_f1).

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
%assign WL_SV 74                ; a stack's save slot (wl_child): wl_seen ..
                                ; wl_c's end, then wl_nvk

%macro WL_BSS 0
WA_VAR wl_pvc, 2                ; the view's count last frame
WA_VAR wl_vp, 2                 ; this frame's corner maker: wa_vert, wl_vert
WA_VAR wl_h, 2                  ; its height (a flight's, its top tread's)
WA_VAR wl_kind, 1               ; its kind byte (wa_bxh), a flight's alone
WA_VAR wl_lom, 1                ; a ramp's low face's bit (else 0): drawn
                                ; as one edge, its top on the floor
WA_VAR wl_nt, 2                 ; a flight's treads (0: a ramp, or no flight:
                                ; cleared with wl_lom, its low byte)
WA_VAR wl_seen, 1               ; the faces the player is outside of: bits
WA_VAR wl_front, 1              ; ...of them, those drawn (posts, floors)
WA_VAR wl_topb, 1               ; the faces' top bits
WA_VAR wl_need, 1               ; the corners projected: bits
WA_VAR wl_abv, 1                ; ...and 1: the eye above the box (wl_need's
                                ; high byte)
WA_VAR wl_fneed, 1              ; ...of them, those with a floor row
WA_VAR wl_m, 1                  ; the seen faces drawn with a floor edge
WA_VAR wl_pm, 1                 ; ...and the faces given a pass
WA_VAR wl_c, 4*WL_REC           ; its corners (WL_*)
WA_VAR wl_fn, 2                 ; the floor's pair: the ground's, or a
WA_VAR wl_fs, 1                 ; stacked box's, its floor's (wl_child)
WA_VAR wl_gfn, 2                ; ...the ground's, made each frame
WA_VAR wl_gfs, 1                ; (wa_render)
WA_VAR wl_lvl, 1                ; a stack's level being drawn (wl_child)
WA_VAR wl_save, WL_SV*(WA_MSTK-1) ; ...and its saves
WA_VAR wl_hn, 2                 ; the box's height's
WA_VAR wl_hs, 1
WA_VAR wl_hh, 2                 ; ...that height (-1: none this frame)
WA_VAR wl_nvk, 1                ; the corners NOT projected in the view: bits
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
WA_VAR wl_stw, 2                ; a steep edge's step a column (its value:
WA_VAR wl_stf, 2                ; CX.BP)
WA_VAR wl_hr, 2                 ; a shallow edge (wl_run): its half-row,
WA_VAR wl_ks, 2                 ; its fraction's step, what a carry takes
WA_VAR wl_dcm, 2                ; off the fraction (the pass's: wl_draw),
WA_VAR wl_dir, 2                ; and the half-row's step
WA_VAR wl_col0, 2               ; a pass's first column x 2
WA_VAR wl_sc, 2                 ; the loops' DX: the stamp, the pass's columns
WA_VAR wl_prev, 2               ; the column before's half-row (a steep step)
WA_VAR wl_tag, 1
WA_VAR wl_k, 2                  ; a flight: the treads left, the
WA_VAR wl_rise, 2               ; rise, the low face, and a tread's step
WA_VAR wl_lo, 1                 ; along each side, camera space
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
%if wl_c + 4*WL_REC - wl_seen != WL_SV - 2
 %error "wl_child saves wl_seen .. wl_c's end as one block"
%endif
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
; indices; clobbers AX, BX, CX, DX, SI, DI). Its inverse depth is wa_rcp's:
; a box's corner is inside the room, never as deep as WA_ZMAX (arenagen)
%macro WLCORNER 1
    test byte [wl_need], 1 << %1
    jz %%n
    xor bx, bx
    mov bl, [ds:bp+%1]
    mov [wl_c+%1*WL_REC+WL_CV], bx
    mov al, [wa_frame]
    cmp [wa_vst+bx], al
    je %%c
    call [wl_vp]                ; (wa_vert, which takes the level's transform -
%%c:                            ; at ROUND the products' low words are not made)
    test byte [wa_vok+bx], 1
    jz %%n
    and byte [wl_nvk], ~(1 << %1)
    mov ax, [wa_vpc+bx]
    mov [wl_c+%1*WL_REC+WL_KC], ax
    mov bx, [wa_vz+bx]
    shl bx, 1
    mov bx, [wa_rcp+bx-WA_NEAR*2]
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
%%n:
%endmacro

; WLSEEN, WLFAR %1 = a face: wa_lowbox's two walks over the faces, one face
; each (r5), where [wl_pm] has its bit. WLSEEN: a seen face - its posts
; where it is drawn ([wl_front]: its start corner's, and its end's where
; the next face round is not seen - wa_boxf's rule), then its pass, with the
; floor edge where [wl_m] has its bit. The posts stand in the pass's own
; first and last columns, which no other seen face's pass reaches, so each
; is still drawn against the clip as it stood before the box. WLFAR: its
; top alone, the far side of the box's top.
%macro WLSEEN 1
    test byte [wl_pm], 1 << %1
    jz %%n
    test byte [wl_front], 1 << %1
    jz %%p
    mov si, %1 * WL_REC         ; its start corner's
    call wl_post
    test byte [wl_seen], 1 << ((%1 + 1) & 3)
    jnz %%p
    mov si, ((%1 + 1) & 3) * WL_REC
    call wl_poste               ; ...and its end's
%%p:
    mov al, [wl_m]
    and al, 1 << %1
    mov [wl_two], al
    WLFAR %1, 0
%%n:
%endmacro
%macro WLFAR 1-2 1
%if %2
    test byte [wl_pm], 1 << %1
    jz %%n
%endif
    mov si, wl_c + %1 * WL_REC + WL_KC
    mov di, wl_c + ((%1 + 1) & 3) * WL_REC + WL_KC
    mov al, (1 << %1) | (1 << ((%1 + 1) & 3))
    call wl_face
%%n:
%endmacro

; wl_vert - wa_vert on a frame whose view is where it was last frame
; ([wl_vp], wa_render's): a vertex stamped THAT frame stands - its camera
; place and its column are functions of the view alone - and is stamped
; again with nothing made. Two corners of a low box in three were a
; transform and a divide each, every still frame (ARENAS-PLAN 5). Keeps
; what wa_vert keeps
wl_vert:
    mov ah, al
    dec ah
    cmp ah, [wa_vst+bx]
    jne wa_vert
    mov [wa_vst+bx], al
    ret

; wa_lowbox - from wa_leaf: SI = a low box, DI = its record x 4 in wa_bxh,
; [wa_crow] its corners, [wa_wcol] its colour (ES = DS, as all through the
; frame)
wa_lowbox:
    mov ax, [wa_bxh+di]
    mov [wl_h], ax
    mov ax, [wa_bxh+di+2]
    mov dl, ah                  ; DL = its faces byte
    test al, 2                  ; the kind: only a FLIGHT (kind 1) keeps it -
    jz .kind                    ; every flight test below is "kind & 3 is
    xor al, al                  ; not 0", and a LIFT (kind 2, its index in
.kind:                          ; bits 4-7) taken for one divided its height
    mov [wl_kind], al           ; by that index as a tread count: lift 0's
                                ; divide fault, every frame it was in view
                                ; (HEIGHTS-PLAN 9.5). A lift draws as the
                                ; low box it is at its current height
    mov word [wl_lom], 0        ; (and no treads: wl_nt's low byte)
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
    and al, dl
    mov [wl_front], al          ; the faces drawn: their posts, floor edges
    mov bx, wl_crn
    xlatb
    mov [wl_fneed], al          ; ...and the corners that need a floor row
    mov al, dl
    mov cl, 4
    shr al, cl
    mov [wl_topb], al
    mov ax, 0x010F              ; THE CORNERS WANTED: all four with the eye
    mov cx, [wa_eye]            ; above the top (its far side is drawn: AH,
    sub cx, [wl_fartop]         ; wl_abv - FARTOP: n units above it) or
    cmp cx, [wl_h]              ; for a flight (its treads span it), else
    jg .need                    ; only the seen faces'
    test byte [wl_kind], 3
    jz .nfl
    mov cx, [wa_eye]            ; A FLIGHT RISING OVER THE EYE: its far side
    cmp cx, [wl_h]              ; - its slopes and its high end - is its
    jle .need                   ; outline from in front (THREE-ARENAS-PLAN,
    dec ah                      ; the ziggurat's ramps)
    jmp short .need
.nfl:
    dec ah
    mov al, [wl_seen]
    xlatb
.need:
    mov [wl_need], ax
    mov byte [wl_nvk], 15
    mov al, [wa_frame]
    mov [wl_sc], al             ; (the loops' stamp)
    mov ax, [wl_h]              ; the box's own height's pair, every corner's
    cmp ax, [wl_hh]             ; - kept for the next box of that height in
    je .hhave                   ; the frame (the floor's: wa_render's, once a
    mov [wl_hh], ax             ; frame)
    call wa_hnum                ; (but a flight's low end's: wl_flight0)
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
    cmp byte [wl_nt], 2         ; top stays: it closes the slope's columns,
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
    ; 1, 2. THE SEEN FACES, each its posts and then its pass: the floor
    ; edge, then the top raising the clip - and a seen face hidden against
    ; a taller neighbour still shows its top's crease where its top bit
    ; says. A ramp's low face is its one edge
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
    WLSEEN 0
    WLSEEN 1
    WLSEEN 2
    WLSEEN 3
    ; (a flight seen from its low end: its inner treads are nearer than the
    ; far side of its top, and the depth stack keeps its entries in order)
    cmp byte [wl_nt], 2
    jb .p3s
    call wl_lowseen
    jz .p3s
    call wl_treads
.p3s:
    test byte [wl_kind], 3      ; (a flight's bits 2-3 are the face it rises
    jnz .p3go                   ; toward, not a stack's: an east- or west-
                                ; rising ramp drew the next box on its top)
    test byte [wl_kind], 4      ; A STACK (HEDGES-PLAN 8): the box standing
    jz .p3go                    ; on this one, between this one's near
    call wl_child               ; faces and the far side of its top
.p3go:
    ; 3. THE FAR SIDE OF ITS TOP, with the eye above the box
    cmp byte [wl_abv], 0
    je .p3d
    mov al, [wl_seen]           ; (the faces not seen whose tops are drawn)
    not al
    and al, [wl_topb]
    mov [wl_pm], al
    mov byte [wl_two], 0
    WLFAR 0
    WLFAR 1
    WLFAR 2
    WLFAR 3
.p3d:
    ; 4. A FLIGHT'S INNER TREADS, seen from anywhere but its low end
    cmp byte [wl_nt], 2
    jb .done
    call wl_lowseen
    jnz .done
    jmp wl_treads
.done:
    ret

; wl_child - a STACK (HEDGES-PLAN 8): the box standing on this one, the next
; in wa_boxes, drawn whole - with any standing on it - as a low box whose
; floor is this one's top: its posts and its floor edges there. Drawn after
; this one's near faces, which are nearer than anything of it, and before
; this one's far side, which it may hide: every ray from the eye crosses
; this one's near faces first and its far edge last. That far side is drawn
; on this one's corners, so with the eye above (wl_abv) they and the faces'
; bits are saved round the call, a slot a level (WA_MSTK - 1 of them).
; The floor's pair is the ground's again after. Clobbers everything.
wl_child:
    cmp word [wa_left], 0       ; (this box's near faces shut every column:
    je .gone                    ; the walk's own stop, as it would have)
    mov bx, [wa_crow]
    sub bx, wa_corn             ; BX = its record x 4
                                ; ITS VIEW TEST, the walk's hints' (it has no
                                ; silhouette of its own): wholly past the
    mov si, bx                  ; right edge's line or behind - its least
    add bx, [wa_kr]             ; x - z - or the left's, its greatest x + z,
    call .corner                ; on the products' high words. Its own
    sub ax, [wa_xc+di+2]        ; and what stands on it are drawn or not
    sub ax, [wa_yc+bx+2]        ; together: they are inside it
    sub ax, [wa_ys+bx+2]
    cmp ax, 3
    jge .gone
    lea bx, [si+0]
    add bx, [wa_kl]
    call .corner
    add ax, [wa_xc+di+2]
    add ax, [wa_ys+bx+2]
    sub ax, [wa_yc+bx+2]
    cmp ax, -3
    jg .seen
.gone:
    ret
.corner:                        ; BX = its corner's place in wa_corn: DI =
    mov bl, [bx]                ; its x index x 4, BX its y's, AX = its
    xor bh, bh                  ; x's [wa_xs] high word
    mov bx, [wa_vtx+bx]
    mov al, bl
    mov ah, 0
    mov di, ax
    mov bl, bh
    xor bh, bh
    mov ax, [wa_xs+di+2]
    ret
.seen:
    push word [wa_crow]         ; (this box's corners)
    push word [wa_wcol]         ; (its colour: the far side's)
    mov al, [wl_abv]
    push ax
    or al, al
    jz .go
    call .slot
    mov si, wl_seen             ; this box's far side's state, saved
    mov cx, (WL_SV - 2) / 2
    rep movsw
    mov al, [wl_nvk]
    stosb
.go:
    inc byte [wl_lvl]
    mov ax, [wl_hn]             ; ITS FLOOR: this box's top
    mov [wl_fn], ax
    mov al, [wl_hs]
    mov [wl_fs], al
    mov ax, [wa_crow]
    add ax, 4
    mov [wa_crow], ax
    sub ax, wa_corn+4
    mov di, ax                  ; DI = its record x 4 (wa_bxh)
    mov si, ax
    shl si, 1
    add si, wa_boxes            ; SI = the box
    mov bx, ax
    shr bx, 1
    shr bx, 1
    mov bl, [wa_bcol+bx+1]
    xor bh, bh
    mov al, [wa_at+bx]
    mov [wa_wcol], al
    call wa_lowbox
    dec byte [wl_lvl]
    mov ax, [wl_gfn]            ; (the ground's pair again)
    mov [wl_fn], ax
    mov al, [wl_gfs]
    mov [wl_fs], al
    pop ax
    or al, al
    jz .out
    call .slot
    mov si, di
    mov di, wl_seen
    mov cx, (WL_SV - 2) / 2
    rep movsw
    lodsb
    mov [wl_nvk], al
.out:
    pop ax
    mov [wa_wcol], al
    pop word [wa_crow]
    ret
.slot:                          ; DI = this level's slot (wl_lvl)
    mov al, [wl_lvl]
    mov ah, WL_SV
    mul ah
    add ax, wl_save
    mov di, ax
    ret

; wl_lowseen - a flight: ZF = 0 when the player stands past its low end.
; Clobbers AL, CL
wl_lowseen:
    mov al, [wl_seen]
    mov cl, [wl_lo]
    shr al, cl
    test al, 1
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

; wl_face - SI, DI = a face's start and end corners' quads (their records
; at WL_KC), AL their two bits, [wl_two] 1 its floor edge too: its pass,
; raising the clip
wl_face:
    test [wl_nvk], al
    jnz .clip
    jmp wl_draw                 ; both corners in the view: the two
                                ; records' projections, in place (wl_draw
                                ; culls a pass hidden in all its columns)
.clip:
    ; A corner out of the view: the edge clipped, its ends projected at
    ; their corners' heights. But first: a LEVEL edge under the eye is
    ; lower on the screen the nearer it comes, so with its FARTHER end's
    ; top (made: in the view) under the screen's last half-row all of it
    ; is - the edges of the box the player stands on, passing under the
    ; view - and it needs no clip, no projection, no pass
    mov bx, [si+WL_CV-WL_KC]
    mov bp, [di+WL_CV-WL_KC]
    mov ax, [si+WL_CN-WL_KC]
    cmp ax, [di+WL_CN-WL_KC]
    jne .cut                    ; (a ramp's side: its ends at two heights)
    or ax, ax
    jns .cut                    ; (the eye at or under it)
    mov ax, [wa_vz+bx]
    sub ax, [ds:wa_vz+bp]
    mov cx, [si+WL_KT-WL_KC]
    jge .far
    xchg bx, bp
    mov cx, [di+WL_KT-WL_KC]
.far:
    test byte [wa_vok+bx], 1
    jz .cut
    cmp cx, 4*WA_HR-1
    jge .under
.cut:
    mov ax, [si+WL_CN-WL_KC]
    mov [wl_n1], ax
    mov ax, [di+WL_CN-WL_KC]
    mov [wl_n2], ax
    mov al, [si+WL_CS-WL_KC]
    mov [wl_s1], al
    mov al, [di+WL_CS-WL_KC]
    mov [wl_s2], al
    mov bx, [si+WL_CV-WL_KC]
    mov si, [di+WL_CV-WL_KC]
    mov ax, [wa_vx+bx]
    mov [wa_x1], ax
    mov ax, [wa_vz+bx]
    mov [wa_z1], ax
    mov ax, [wa_vx+si]
    mov [wa_x2], ax
    mov ax, [wa_vz+si]
    mov [wa_z2], ax
    jmp wl_xedge
.under:
    ret

; wl_post - SI = a corner's record (an offset into wl_c), a face's start
; corner (its column the first whose centre is past it: CX = 7), or
; wl_poste - its end (the last short of it: 7 - 16): its post, `│` in the
; rows between its top's and its floor's, against the clip - every cell
; tested (first one wins). Clobbers AX, BX, CX, DX, SI, DI
wl_poste:
    mov cx, 7-16
    jmp short wl_post.go
wl_post:
    mov cx, 7
.go:
    mov bx, [wl_c+si+WL_CV]
    test byte [wa_vok+bx], 1
    jz wl_face.under            ; (not in the view: off the screen, or behind)
    mov ax, [wl_c+si+WL_KF]
    mov bx, [wl_c+si+WL_KT]
    mov dx, ax                  ; (a row between its top's and its floor's
    sub dx, bx                  ; takes three half-rows: nine quarters at
    cmp dx, 9                   ; least - a ramp's low corners, on the
    jl wl_face.under            ; floor, and every short post)
    add ax, 2
    sar ax, 1
    sar ax, 1                   ; AX = the floor, half-rows, rounded
    add bx, 2
    sar bx, 1
    sar bx, 1
    sar bx, 1
    inc bx                      ; BX = the row under the top's
    mov di, [wl_c+si+WL_KC]     ; its column: as wa_span's corners
    add di, cx
    mov cl, 4
    sar di, cl
    js .out
    cmp di, [wa_w]
    jge .out
    shl di, 1                   ; DI = the column x 2
    cmp byte [wa_cov+di], 0
    jne .out                    ; (shut)
    cmp ax, [wa_cclip+di]
    jle .clp
    mov ax, [wa_cclip+di]
.clp:
    sar ax, 1                   ; the row to stop before (the clip is WA_HR
    or bx, bx                   ; at most: never past the last)
    jns .tok
    xor bx, bx
.tok:
    sub ax, bx
    jle .out
    mov cx, ax                  ; CX = the rows
    shl bx, 1
    mov bx, [wa_rowo2+bx]
    add bx, di
    mov di, [wa_lp]
    mov dl, [wa_frame]
    mov al, 0xB3
    mov ah, [wa_wcol]
    mov si, [wa_rowo2+2]        ; (a row's offset: row 1's)
.row:
    WPLOT
    add bx, si
    loop .row
    mov [wa_lp], di
.out:
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
    shl bx, 1                   ; (its inverse depth: wa_rcp's, the point
    mov bx, [wa_rcp+bx-WA_NEAR*2] ; on a box's edge, inside the room)
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
; and a stack entry pushed (a top at or above the screen's top shuts the column).
; A pass whose edges lie at or under every one of its columns' clips draws
; nothing and raises nothing, so it is CULLED before its edges are set up:
; neither edge's interpolation leaves the range of its two ends, and each
; is drawn, and raises, only above the clip (the steep fill too: wa_slope
; stops at the clip); a shut column draws nothing either. It registers no
; tag, the one thing it would have left. A floor edge is never above its
; top (a corner's floor row is its top's or under it), so the top's two
; ends stand for all four in both tests.
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
    jge .none                   ; and nor does the floor edge under it
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
    sub dx, bx
    jl .none
    inc dx                      ; DX = the columns
    mov [wl_sc+1], dl
    mov cx, dx
    shl bx, 1                   ; (the first x 2 from here)
    mov dx, [si+2]              ; THE CULL: the highest half-row the top
    cmp dx, [di+2]              ; reaches (an end's, rounded as the loops
    jle .c1                     ; round)...
    mov dx, [di+2]
.c1:
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
.none:
    ret                         ; (hidden throughout: nothing to do)
.hit:
    cmp word [si+wa_cov-wa_cclip-2], 0
    jne .snext                  ; (a shut column shows nothing either)
    pop si
    mov cx, [si]                ; CX = the left end
    mov [wa_dc], bp
    mov [wl_col0], bx           ; (the first column x 2)
    mov ax, bx                  ; the first centre past the left end, x 16
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, 8
    sub ax, cx
    mov [wa_fs], ax
    cmp ax, 17                  ; (wl_run: dc with the left end on the
    sbb ax, ax                  ; screen, else 0)
    and ax, bp
    mov [wl_dcm], ax
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
    push word [si+2]            ; (the top's ends, for after the floor's)
    push word [di+2]
    cmp byte [wl_two], 0        ; THE FLOOR EDGE first, a loop of its own
    je .top                     ; (r5): it is drawn against the clip as it
    mov ax, [si+6]              ; stood, which the top raises only after it
    mov bx, [di+6]              ; in each column - and each column is its
    mov dx, ax                  ; own, so all the floor's cells and then all
    cmp dx, bx                  ; the top's are the same cells, plotted in
    jle .f1                     ; the same order within a column.
    mov dx, bx                  ; ITS CULL, the top's: at or under every
.f1:                            ; open column's clip it draws nothing (and
    add dx, 2                   ; raises nothing, ever) - the floor of a
    sar dx, 1                   ; box behind a nearer one's top
    sar dx, 1
    push si
    mov si, [wl_col0]
    mov cl, [wl_sc+1]
    xor ch, ch
    add si, wa_cclip
.fscan:
    lodsw
    cmp ax, dx
    jg .fhit
.fnext:
    loop .fscan
    pop si
    jmp short .top
.fhit:
    cmp word [si+wa_cov-wa_cclip-2], 0
    jne .fnext
    pop si
    mov ax, [si+6]
    mov bx, [di+6]
    mov cx, wl_lfloor
    call wl_run
.top:
    pop bx                      ; THE TOP, raising the clip
    pop ax
    mov cx, wl_ltop
    jmp wl_run
.full:
    mov word [wa_left], 0       ; (more spans than the table holds: drawing
    ret                         ; stops this frame - never seen)

; wl_run - AX = t1, BX = t2 (an edge's ends, left and right, quarter
; half-rows), CX = its loops (wl_ltop or wl_lfloor: the steep one, the
; shallow), the pass's [wa_dc], [wa_fs], [wl_dcm], [wl_col0], [wl_sc] and
; [wl_tag]: the edge's start and step, and on into its loop over the pass's
; columns (which stores the list's pointer and returns).
; A SHALLOW edge (under a half-row a column - most): [wl_hr] its half-row
; at the first column (rounded: a half added), BP the fraction there and
; [wl_ks] its step, in the run loops' form - BP + k carries when the
; half-row moves on by [wl_dir] (+1 down the screen, -1 up), and the loop
; then takes [wl_dcm] off BP. With the left end on the screen (the first
; centre within a column of it: [wl_dcm] is dc) that is the edge itself and
; NO DIVIDE: the fraction counts in dc-ths of a half-row - the step is
; 4 |t2 - t1| of them, the start (t1 + 2) / 4 half-rows and |t2 - t1| x fs / 4
; dc-ths (one multiply) - kept less dc so that passing dc is the carry; an
; edge going up keeps the complement (as FLATPASS's does). With the left
; end off the screen (clipped, or a box the screen's edge cuts: fs can be
; hundreds of columns; [wl_dcm] is 0, which sends every edge this way) it
; is wa_span.edge's start and step, exact to a 65536th: the same form with
; nothing to take off.
; A STEEP one: CX.BP its value at the first column and [wl_stw].[wl_stf]
; its step, 16.16 (wa_span.edge's).
wl_run:
    push cx
    mov di, ax                  ; DI = t1
    sub bx, ax
    mov ax, bx
    cwd                         ; DX = the change's sign
    xor ax, dx
    sub ax, dx                  ; AX = |t2 - t1|
    mov cx, [wl_dcm]
    mov si, ax
    shl si, 1
    jc .exact
    shl si, 1
    jc .exact
    cmp si, cx
    jae .exact                  ; (a half-row a column or more)
    mov [wl_ks], si
    mov bp, dx                  ; BP = the sign...
    mul word [wa_fs]            ; (under 65536: fs is 16 at most and the
    and bp, 3                   ; change under dc / 4, dc under 9,600 with
    add ax, bp                  ; its left end on the screen - the guard
    shr ax, 1                   ; band) / 4, rounded AWAY from the start for
    shr ax, 1                   ; an edge going up: both ways the fraction
                                ; errs toward the screen's top, by under
                                ; 2 / dc of a half-row
    lea bx, [di+2]              ; t1 and a half...
    mov di, cx                  ; DI = dc
    xor si, si
    test bl, 1
    jz .q1
    mov si, cx
.q1:
    test bl, 2
    jz .q2
    add si, cx
    add si, cx
.q2:
    shr si, 1                   ; ...its quarters in dc-ths (dc is under
    shr si, 1                   ; 17,800: three fit)
    sar bx, 1
    sar bx, 1                   ; BX = the half-row
    or bp, bp
    jnz .up
    add si, ax
    sub si, di                  ; (less dc: the carry's form)
    jc .dn
    sub si, di                  ; (a half-row passed already)
    inc bx
.dn:
    mov ax, 1
    jmp short .set
.up:
    sub si, ax
    jnc .u1
    add si, di
    dec bx
.u1:
    not si                      ; (dc - 1 - a, less dc)
    mov ax, -1
.set:
    mov bp, si
.set2:
    mov [wl_dir], ax
    mov [wl_hr], bx
    pop bx
    mov cl, [wl_tag]            ; (shallow: CL the tag, BP its fraction)
    inc bx
    inc bx
.go:
    mov si, [wl_col0]
    mov di, [wa_lp]
    mov dx, [wl_sc]             ; DL = the stamp, DH = the columns
    mov ah, [wa_wcol]
    jmp [bx]
.exact:
    mov ax, di
    add bx, di
    mov byte [wa_steep], 0
    call wa_span.edge           ; BX.CX the start, DX.AX the step
    cmp byte [wa_steep], 0
    jne .steep
    mov bp, cx
    mov cx, 1
    or dx, dx
    jns .ex1
    neg ax                      ; (up the screen: the complement)
    not bp
    neg cx
.ex1:
    mov [wl_ks], ax
    xchg ax, cx
    jmp .set2
.steep:
    mov [wl_stw], dx
    mov [wl_stf], ax
    mov bp, cx
    mov cx, bx
    pop bx
    jmp .go
wl_ltop:    dw wl_l01s, wl_rtop
wl_lfloor:  dw wl_f1, wl_rfloor

; wl_rtop - a pass's TOP edge, shallow (wl_run's): its columns from SI (x 2),
; DH of them, at [wl_hr] and BP stepping by [wl_ks]; CL the pass's tag, AH the
; colour, DL the stamp, DI the list. The edge keeps its half-row for a RUN
; of columns (FLATPASS's way): the glyph, the cell and the stack's entry -
; CX, the tag and the row - are made once a run, the cell stepped, and the
; half-row moves on at a carry, in CH. In a column the top is above the
; clip (one byte test: the clip is 0..WA_HR) and the column not shut: the
; cell plotted, the clip raised and an entry pushed - the column's count
; tested in place, no index made - or, full, merged into the deepest (the
; nearer tag kept, so a figure between the two can only be hidden a frame
; too soon). A half-row off the screen, or its top one, goes a column at a
; time (wl_rodd, [wl_hr] the half-row there): under the screen nothing; at
; or over its top the column shut (wl_shutc), the top half-row's cell drawn
; first.
wl_rodd:
    inc bx                      ; BX = the half-row
    mov [wl_hr], bx
    cmp bx, [wa_cclip+si]
    jge .next                   ; (under the clip, as all under the screen)
    cmp byte [wa_cov+si], 0
    jne .next
    or bx, bx
    jnz .shut                   ; (over the screen's top: nothing drawn)
    mov al, [wa_hgl]
    mov bx, [wa_hroff]
    add bx, si
    WPLOT
.shut:
    call wl_shutc
.next:
    inc si
    inc si
    dec dh
    jz wl_rtop.done
    add bp, [wl_ks]
    jnc wl_rtop
    sub bp, [wl_dcm]
    mov bx, [wl_dir]
    add bx, [wl_hr]
    jmp short wl_rtop.run2
wl_rtop:
    mov bx, [wl_hr]
.run2:
    dec bx
    cmp bx, WA_HR-1
    jae wl_rodd
    inc bx
    mov ch, bl
    mov al, [wa_hgl+bx]
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, si
.col:
    cmp ch, [wa_cclip+si]
    jae .next                   ; under something nearer
    cmp byte [wa_cov+si], 0
    jne .next                   ; (shut)
    WPLOT
    mov [wa_cclip+si], ch
    cmp byte [wa_cstn+si], 1
    jae .p1
    mov [wa_cst+si], cx
.inc:
    inc byte [wa_cstn+si]
.next:
    inc bx
    inc bx
    inc si
    inc si
    dec dh
    jz .done
    add bp, [wl_ks]
    jnc .col
    sub bp, [wl_dcm]            ; the next half-row (CH: 1..47 here, so the
    add ch, [wl_dir]            ; next is 0..48 - a byte)
    mov bl, ch
    xor bh, bh
    jmp .run2
.done:
    mov [wa_lp], di
    ret
.p1:
    ja .p2
    mov [wa_cst+2*WA_MAXW+si], cx
    jmp .inc
.p2:
    cmp byte [wa_cstn+si], 3
    ja .full
    je .p3
    mov [wa_cst+4*WA_MAXW+si], cx
    jmp .inc
.p3:
    mov [wa_cst+6*WA_MAXW+si], cx
    jmp .inc
.full:
    mov [wa_cst+(WA_CSTK-1)*2*WA_MAXW+si+1], ch
    jmp .next

; wl_rfloor - a pass's FLOOR edge alone, shallow (wl_draw's, r5): wl_rtop's
; walk, each cell plotted where it is above the column's clip and the
; column not shut; nothing raised. A run off the screen is stepped through
; (wl_rfoff, [wl_hr] the half-row there).
wl_rfoff:
    mov [wl_hr], bx
.col:
    inc si
    inc si
    dec dh
    jz wl_rfloor.done
    add bp, [wl_ks]
    jnc .col
    sub bp, [wl_dcm]
    mov bx, [wl_dir]
    add bx, [wl_hr]
    jmp short wl_rfloor.run
wl_rfloor:
    mov bx, [wl_hr]
.run:
    cmp bx, WA_HR
    jae wl_rfoff
    mov ch, bl
    mov al, [wa_hgl+bx]
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, si
.col:
    cmp ch, [wa_cclip+si]       ; (the clip first: it hides more of a floor
    jae .next                   ; edge than shut columns do)
    cmp byte [wa_cov+si], 0
    jne .next                   ; (shut)
    WPLOT
.next:
    inc bx
    inc bx
    inc si
    inc si
    dec dh
    jz .done
    add bp, [wl_ks]
    jnc .col
    sub bp, [wl_dcm]            ; the next half-row (CH: 0..47 here)
    add ch, [wl_dir]
    mov bl, ch
    mov bh, 0
    jns .run
    dec bh                      ; (-1: over the screen's top)
    jmp .run
.done:
    mov [wa_lp], di
    ret

; wl_l01s - a pass's TOP edge, STEEP (a half-row a column or more): its
; columns from SI, DH of them, CX.BP the top and [wl_stw].[wl_stf] its step;
; DL the stamp, DI the list, AH the colour.
; A column's top is ONE signed test against its clip (the clip is 1..WA_HR in
; an open column): at or under it nothing is drawn nor raised; over it the
; cell is plotted when on the screen, the clip raised and an entry pushed - or,
; at or over the screen's top, the column shut (wl_shutc). A step is filled
; where the whole half-rows of two neighbouring columns are two or more
; apart, unless both ends are at or under the clip, where wa_slope fills
; nothing; a shut column leaves the next column's own value as the one
; before it, so no step is filled across it.
wl_l01s:
    mov [wl_prev], cx
    jmp short .col
.shut:
    add bp, [wl_stf]            ; (the next column's own values are the ones
    adc cx, [wl_stw]            ; before it: no step filled across this one)
    mov [wl_prev], cx
    jmp .adv
.col:
    cmp byte [wa_cov+si], 0
    jne .shut                   ; (shut)
    mov bx, cx                  ; a step of two half-rows or more from
    sub bx, [wl_prev]           ; the column before's is filled
    mov [wl_prev], cx
    inc bx                      ; (-1..+1 -> 0..2: none)
    cmp bx, 3
    jb .nf
    call wl_fillc
.nf:
    cmp cx, [wa_cclip+si]
    jge .next                   ; under something nearer, or below the screen
    or cx, cx
    js .over                    ; (over the screen's top: shut, nothing drawn)
    mov bx, cx
    mov al, [wa_hgl+bx]
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, si
    WPLOT
    jcxz .over                  ; (the top row: shut)
    mov [wa_cclip+si], cx
    mov bh, cl                  ; the depth stack: (tag, row) on top, or,
    mov bl, [wl_tag]            ; full, merged into the deepest (wl_rtop's
    cmp byte [wa_cstn+si], 1    ; ladder)
    jae .p1
    mov [wa_cst+si], bx
.inc:
    inc byte [wa_cstn+si]
.next:
    add bp, [wl_stf]
    adc cx, [wl_stw]
.adv:
    inc si
    inc si
    dec dh
    jnz .col
    mov [wa_lp], di
    ret
.p1:
    ja .p2
    mov [wa_cst+2*WA_MAXW+si], bx
    jmp .inc
.p2:
    cmp byte [wa_cstn+si], 3
    ja .full
    je .p3
    mov [wa_cst+4*WA_MAXW+si], bx
    jmp .inc
.p3:
    mov [wa_cst+6*WA_MAXW+si], bx
    jmp .inc
.full:
    mov [wa_cst+(WA_CSTK-1)*2*WA_MAXW+si+1], cl
    jmp .next
.over:
    call wl_shutc
    jmp .next

; wl_f1 - a pass's FLOOR edge alone, STEEP: its columns from SI, DH of
; them, CX.BP its value, [wl_stw].[wl_stf] its step; each cell plotted where
; it is under the column's clip (ONE unsigned test: a row off either side
; reads as past it) and the column not shut, a steep step filled down to
; the clip; nothing raised
wl_f1:
    mov [wl_prev], cx
    jmp short .col
.shut:
    add bp, [wl_stf]            ; (the next column's own value is the one
    adc cx, [wl_stw]            ; before it: no step filled across this one)
    mov [wl_prev], cx
    jmp short .adv
.col:
    cmp byte [wa_cov+si], 0
    jne .shut                   ; (shut)
    mov bx, cx                  ; a step of two half-rows or more from
    sub bx, [wl_prev]           ; the column before's is filled
    mov [wl_prev], cx
    inc bx                      ; (-1..+1 -> 0..2: none)
    cmp bx, 3
    jb .nf
    call wl_fillc
.nf:
    cmp cx, [wa_cclip+si]
    jae .next
    mov bx, cx
    mov al, [wa_hgl+bx]
    shl bx, 1
    mov bx, [wa_hroff+bx]
    add bx, si
    WPLOT
.next:
    add bp, [wl_stf]
    adc cx, [wl_stw]
.adv:
    inc si
    inc si
    dec dh
    jnz .col
    mov [wa_lp], di
    ret
wl_k160:    dw 0, 2*WA_MAXW, 4*WA_MAXW, 6*WA_MAXW

; wl_fillc - CX = a column's edge (half-rows), BX = its step from the column
; before's + 1 (not 0..2), SI = the column x 2: the step filled (wa_slope),
; unless both ends are at or under the clip. Keeps CX; AH the colour again.
; Clobbers AL, BX
wl_fillc:
    push cx
    mov ax, cx
    sub cx, bx
    inc cx                      ; CX = the column before's
    mov bx, cx
    cmp bx, ax
    jl .min
    mov bx, ax
.min:
    cmp bx, [wa_cclip+si]
    jge .no
    call wa_slope
.no:
    pop cx
    mov ah, [wa_wcol]
    ret

; wl_shutc - SI = a column x 2: the column covered from the screen's top -
; shut, wa_cov holding the tag as a wall's does - its clip 0, and its stack
; entry pushed (row 0), or merged into the deepest. Clobbers AL, BX.
; (The column is open, so wa_cov's high byte is 0 already - its spans'
; one-past-the-end mark is theirs alone - and a clip's always is: a byte
; store each.)
wl_shutc:
    mov al, [wl_tag]
    mov [wa_cov+si], al
    dec word [wa_left]
    mov byte [wa_cclip+si], 0
    mov bx, [wa_cstn+si]
    cmp bl, WA_CSTK
    jae .full
    inc byte [wa_cstn+si]
    shl bx, 1
    mov bx, [wl_k160+bx]
    mov [wa_cst+bx+si], al
    mov byte [wa_cst+bx+si+1], 0
    ret
.full:
    mov byte [wa_cst+(WA_CSTK-1)*2*WA_MAXW+si+1], 0
    ret

; --- flights --------------------------------------------------------------
; wl_flight0 - a flight's corners: the low end's two take its first tread's
; height's pair, its high end's its top's (wa_lowbox's). [wl_lo] its low
; end's face; [wl_nt], [wl_rise] its treads and their rise
wl_flight0:
    mov al, [wl_kind]
    mov cl, 2
    shr al, cl
    and al, 3                   ; the face it rises TOWARD
    xor al, 2
    mov [wl_lo], al
    xor bh, bh                  ; the low face's two corners
    mov bl, al
    call wl_ends
    cmp byte [wa_treads], 0     ; A RAMP (the default, HEIGHTS-PLAN 7.4.2):
    jne .steps                  ; its low face's corners on the floor, its
                                ; sides straight from there to its top, no
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
