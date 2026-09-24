; =============================================================================
; os8088 - apps/pixelstein/pxcast.inc
;
; THE CAST (SPEC.md 97.2): one ray a column from the eye to its wall, through
; the two pointer walkers of 97.2.2, and the hit of 97.2.5 into the column
; arrays. The four quadrant bodies are here as a template, PX_BODY, stamped
; four times; pxgen.inc copies them into the scratch part when there is one
; and the frame far-calls whichever segment they live in (97.2.1).
;
; Register discipline inside px_cast is the walker's: SI/DX the V walker,
; DI/BX the H walker, CX/BP the two keys, AL the generation, ES the body
; segment - every one of them live across the far call, so the column index
; and the quadrant are words in memory and the per-quadrant constants are
; tables indexed by q*2. DS stays ours for the whole cast (the maps and the
; column arrays are in this segment); the compose that follows is the one
; that switches DS to the destination (pxcomp.inc).
; =============================================================================

; PX_MUL14 - AX = (AX x BX) >> 14, signed: imul, then the 32-bit product
;            shifted left two and its high word taken (apps/tank/tk3d.inc:31)
%macro PX_MUL14 0
    imul bx
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov ax, dx
%endmacro

; -----------------------------------------------------------------------------
; PX_BODY dxs, dys - one quadrant's walker (97.2.2), instruction for
;                    instruction the body tests/pxsbench/pxsbench.asm measured
;                    at 128.2 clk a crossing, with the signs of the four steps
;                    and the direction of the two compares the quadrant's.
;
; A V pass steps x by one row of mapT (64 bytes, the sign of dxs) and y by
; the tangent's integer part plus the fraction's carry - one adc (dys > 0) or
; one sbb (dys < 0) whose immediate is PATCHED once a column: int + 64*dxs
; for the adc, int - 64*dxs for the sbb (97.2.1). The keys follow both
; walkers. It ends in retf with CF = the side (V clear, H set): the one
; answer that costs no memory operand, and what lets a copy of these bytes
; run from another segment untouched.
;
; EVERY OFFSET THE CAST PATCHES OR ENTERS IS ASSERTED WHERE IT IS EMITTED
; (pxgen.inc's ladder guard, the same hazard): the four immediates are
; written into the middle of an instruction once a column and the two pass
; tails are far-called into the middle of the body, so a total-length check
; alone would pass a pair of encodings that grew and shrank together and put
; every offset above them one byte off - a word stored into the opcode
; stream of a routine the frame calls 64 times. Zero bytes.
; -----------------------------------------------------------------------------
; the four patch sites and the two pass tails, as offsets into a body - the
; same in all four because the macro emits the same encodings (an add and a
; sub with a word immediate are both 4 bytes, an inc and a dec both 1)
PXB_VFRAC   equ 13
PXB_VINT    equ 17
PXB_HFRAC   equ 38
PXB_HINT    equ 42
PXB_VPASS   equ 7
PXB_HPASS   equ 32

%macro PX_BODY 2
%%body:
    jmp short %%vcheck
%%ventry:
    test byte [si], PXC_SOLID | PXC_DOOR
    jnz %%vhit
%%vpass:                            ; +7: the pass tail, 97.2.4's re-entry
%if (%%vpass - %%body) != PXB_VPASS
%error "PX_BODY's V pass tail moved: PXB_VPASS is stale"
%endif
    mov [si + PX_SPOTD], al         ; spotvisT = the generation
%if %2 > 0
    add dx, strict word 0           ; PATCHED: the y fraction step
%%vfrac:
    adc si, strict word 0           ; PATCHED: y's integer step + 64*dxs
%%vint:
%else
    sub dx, strict word 0
%%vfrac:
    sbb si, strict word 0           ; PATCHED: y's integer step - 64*dxs
%%vint:
%endif
%if (%%vfrac - 2 - %%body) != PXB_VFRAC
%error "PX_BODY's y fraction immediate moved: PXB_VFRAC is stale"
%endif
%if (%%vint - 2 - %%body) != PXB_VINT
%error "PX_BODY's y integer immediate moved: PXB_VINT is stale"
%endif
%if %1 > 0
    add cx, 64
    inc bp
%else
    sub cx, 64
    dec bp
%endif
%%vcheck:
    cmp si, cx
%if %2 > 0
    jb %%ventry                     ; V first while its y is short of yt
%else
    ja %%ventry
%endif
%%hentry:
    test byte [di], PXC_SOLID | PXC_DOOR
    jnz %%hhit
%%hpass:                            ; +32: the H pass tail
%if (%%hpass - %%body) != PXB_HPASS
%error "PX_BODY's H pass tail moved: PXB_HPASS is stale"
%endif
    mov [di + PX_SPOTD], al
%if %1 > 0
    add bx, strict word 0           ; PATCHED: the x fraction step
%%hfrac:
    adc di, strict word 0           ; PATCHED: x's integer step + 64*dys
%%hint:
%else
    sub bx, strict word 0
%%hfrac:
    sbb di, strict word 0
%%hint:
%endif
%if (%%hfrac - 2 - %%body) != PXB_HFRAC
%error "PX_BODY's x fraction immediate moved: PXB_HFRAC is stale"
%endif
%if (%%hint - 2 - %%body) != PXB_HINT
%error "PX_BODY's x integer immediate moved: PXB_HINT is stale"
%endif
%if %2 > 0
    add bp, 64
    inc cx
%else
    sub bp, 64
    dec cx
%endif
%%hcheck:
    cmp di, bp
%if %1 > 0
    jb %%hentry
%else
    ja %%hentry
%endif
    jmp short %%ventry
%%vhit:
    clc
    retf
%%hhit:
    stc
    retf
%endmacro

px_bodies:                          ; q0 (+,+), q1 (-,+), q2 (-,-), q3 (+,-)
px_body0:
    PX_BODY 1, 1
PXB_SIZE    equ $ - px_body0
    PX_BODY -1, 1
    PX_BODY -1, -1
    PX_BODY 1, -1
PXB_ALL     equ $ - px_bodies

%if PXB_SIZE != 58
%error "PX_BODY is not the 58 bytes its patch offsets were derived for"
%endif
%if PXB_ALL != 4 * PXB_SIZE
%error "the four bodies do not share one length"
%endif

; --- the per-quadrant constants, indexed by q*2 ------------------------------
px_vadj:    dw 64, -64, 64, -64     ; what the V patch adds to the integer step
px_hadj:    dw 64, -64, 64, -64     ; ...and the H patch
px_vpark:   dw 0xFF00, 0xFF00, 0, 0 ; a parked V walker's pointer (97.2.3):
                                    ; past every key for jb, under every key
                                    ; for ja - what the body's FIRST compare
                                    ; sees, so that H is entered instead
px_hpark:   dw 0xFF00, 0, 0, 0xFF00 ; ...and H's, which no compare ever
                                    ; reaches (the far keys below); kept so
                                    ; the register is a value and not a leftover
; THE LIVE WALKER NEVER YIELDS TO A PARKED ONE (97.2.3): its key is put where
; its compare cannot fail for the 63 crossings the solid border bounds it to.
; The keys move 64 a pass, the wrong way for the compare in two quadrants of
; four, so the far key sits 64 * 63 clear of the wrap and of every pointer
; value; pxgame.asm asserts the arrays lie inside that band. A parked pointer
; used to be tested by the other walker's compare and, one carry later,
; ENTERED: the pass tail then stored the generation at park + PX_SPOTD, which
; wraps into this image at 0x0F00 / 0x1000 - over code - and walked on
; storing until it met a byte with bit 0 or 1 set. Reachable: H parks for
; the five near-axial tangent units 1..5, whose tan is 1..2 and not 0, and
; a y fraction above ~0.8 carries within the 24 tiles a corridor allows.
; tools/pxssim.py's v_first()/h_first() already say "the live one, always"
PX_FARJB    equ 0xF000              ; a jb compare: the key is above the pointer
PX_FARJA    equ 0x0800              ; a ja compare: below it
px_cfar:    dw PX_FARJB, PX_FARJB, PX_FARJA, PX_FARJA    ; V's compare: by dys
px_bfar:    dw PX_FARJB, PX_FARJA, PX_FARJA, PX_FARJB    ; H's compare: by dxs
px_dxneg:   dw 0, 1, 1, 0           ; dxs < 0 in q1 and q2
px_dyneg:   dw 0, 0, 1, 1           ; dys < 0 in q2 and q3

; -----------------------------------------------------------------------------
; px_frame_tabs - the per-frame constants the column setup indexes by q
; in:  [px_px], [px_py] the eye.  clobbers AX, BX, CX, DX, SI
;
; xpartial and ypartial (the distance to the first grid line, 97.2.1), the
; two pointer bases, the two keys - each four words, one per quadrant, so a
; column's setup is a table read where a branch would be.
; -----------------------------------------------------------------------------
px_frame_tabs:
    mov ax, [px_px]
    mov bl, al                      ; BL = xf
    mov bh, ah                      ; BH = the x tile
    mov ax, [px_py]
    mov cl, al                      ; CL = yf
    mov ch, ah                      ; CH = the y tile
    ; xpartial: 256 - xf when dxs > 0 (q0, q3), xf otherwise
    xor ah, ah
    mov al, bl
    mov [px_xpq + 2], ax            ; q1
    mov [px_xpq + 4], ax            ; q2
    neg ax
    add ax, 256
    mov [px_xpq + 0], ax            ; q0
    mov [px_xpq + 6], ax            ; q3
    mov al, cl
    xor ah, ah
    mov [px_ypq + 4], ax            ; q2 (dys < 0)
    mov [px_ypq + 6], ax            ; q3
    neg ax
    add ax, 256
    mov [px_ypq + 0], ax            ; q0
    mov [px_ypq + 2], ax            ; q1
    ; the corner tile per quadrant: xt = xtile + dxs, yt = ytile + dys
    xor si, si
.q:
    mov al, bh
    xor ah, ah
    cmp word [px_dxneg + si], 0
    je .xp
    dec ax
    jmp short .xd
.xp:
    inc ax
.xd:
    mov dx, ax                      ; DX = xt
    mov al, ch
    xor ah, ah
    cmp word [px_dyneg + si], 0
    je .yp
    dec ax
    jmp short .yd
.yp:
    inc ax
.yd:                                ; AX = yt
    push ax
    ; vbase = mapT + xt*64; ckey = vbase + yt
    push cx
    mov cl, 6
    shl dx, cl                      ; xt*64
    shl ax, cl                      ; yt*64
    pop cx
    push ax
    mov ax, dx
    add ax, px_mapT
    mov [px_vbase + si], ax
    pop ax                          ; yt*64
    push ax
    add ax, px_map
    mov [px_hbase + si], ax         ; hbase = map + yt*64
    pop ax
    pop dx                          ; DX = yt, AX = yt*64
    push dx
    mov dx, [px_vbase + si]
    pop ax                          ; AX = yt
    add dx, ax
    mov [px_ckey + si], dx          ; ckey = mapT + xt*64 + yt
    ; bkey = map + yt*64 + xt: xt = (vbase - mapT) >> 6
    mov dx, [px_vbase + si]
    sub dx, px_mapT
    push cx
    mov cl, 6
    shr dx, cl
    pop cx
    add dx, [px_hbase + si]
    mov [px_bkey + si], dx
    inc si
    inc si
    cmp si, 8
    jb .q
    ret

; -----------------------------------------------------------------------------
; px_spot_clear - both spotvis arrays to "never crossed" (97.1) - a new
;                 floor, so the Tab map's memory too (px_seen, wave 6),
;                 and px_gen back to 0;
;                 px_spot_wrap - the generation wrapped: the marks FOLDED
;                 into the map's memory first (px_seen_fold), then cleared
; clobbers AX, CX, DI, ES (px_spot_wrap: and BX, DX, SI)
; -----------------------------------------------------------------------------
px_spot_clear:
    push ds
    pop es
    mov di, px_seen
    mov cx, 256
    xor ax, ax
    cld
    rep stosw
    mov byte [px_gen], 0            ; ...and the generation restarts: the
    jmp short px_spot_zero          ; wrap falls 255 frames into a floor, so
                                    ; the timedemo's 81 never hold it and
                                    ; every owner times the same frames
                                    ; (review, wave 6 r2)
px_spot_wrap:
    call px_seen_fold
px_spot_zero:
    push ds
    pop es
    mov di, px_spotT
    mov cx, 2048
    xor ax, ax
    cld
    rep stosw
    mov di, px_spot
    mov cx, 2048
    rep stosw
    ret

; px_cellT2P - AX = a mapT index (x*64 + y) -> AX = the plain cell (y*64 + x).
;              Preserves every other register
px_cellT2P:
    push cx
    mov cl, 6
    mov ch, al                      ; CH = (x & 3) << 6 | y
    and ch, 63                      ; CH = y
    shr ax, cl                      ; AX = x
    xchg al, ch                     ; AL = y, CH = x
    xor ah, ah
    shl ax, cl                      ; AX = y * 64
    or al, ch                       ; ...| x
    pop cx
    ret

; -----------------------------------------------------------------------------
; px_cast - every column's ray, into the column arrays
; in:  the eye and heading, [px_hcos]/[px_hsin], [px_fanp], [px_cols]
; out: px_top/px_bot/px_wallh/px_mat/px_side/px_u per column
; clobbers EVERYTHING but DS (SPEC.md 85.3.4's discipline: a frame's inner
; loop preserves nothing)
; -----------------------------------------------------------------------------
px_cast:
    ; --- the generation --------------------------------------------------
    mov al, [px_gen]
    inc al
    jnz .gen
    call px_spot_wrap               ; 255 frames on: both arrays back to 0,
                                    ; folded into the Tab map's bits first
    mov al, 1
.gen:
    mov [px_gen], al
    call px_frame_tabs
    mov es, [px_bseg]               ; ES = the bodies, for the frame
    mov word [px_col], 0
.col:
    ; --- the angle, its quadrant, the two steps (97.2.1) -----------------
    mov bx, [px_col]
    shl bx, 1
    add bx, [px_fanp]
    mov ax, [px_head]
    add ax, [bx]                    ; a = heading + fan[c]
    and ax, 0FFFh
    mov bx, ax
    and bx, 1023
    shl bx, 1
    mov cx, [px_tan + bx]           ; tan[i]
    neg bx
    mov dx, [px_tan + 2048 + bx]    ; tan[1024 - i]
    mov bl, ah
    shr bl, 1
    shr bl, 1
    and bx, 3                       ; the quadrant: the angle's bits 11:10
    test bl, 1
    jz .even
    xchg cx, dx                     ; an odd quadrant swaps the two
.even:
    mov [px_q], bl
    mov [px_tanv], cx               ; |tan|: V's step, y per x
    mov [px_tanh], dx               ; |cot|: H's step, x per y
    shl bx, 1
    mov si, bx                      ; SI = q*2 for the tables
    mov [px_qs2], si                ; ...and for the hit, after the walk
    mov ax, [px_qoff + si]
    mov [px_qcur], ax               ; this column's body
    mov bx, ax                      ; ES:BX = the body, for the patches
    ; --- the four patches --------------------------------------------------
    mov ax, cx                      ; AH = int, AL = frac of |tan|
    mov dh, al
    xor dl, dl
    mov [es:bx + PXB_VFRAC], dx     ; the fraction, Q0.16
    mov al, ah
    xor ah, ah
    add ax, [px_vadj + si]
    mov [es:bx + PXB_VINT], ax      ; the integer step +/- 64
    mov ax, [px_tanh]
    mov dh, al
    xor dl, dl
    mov [es:bx + PXB_HFRAC], dx
    mov al, ah
    xor ah, ah
    add ax, [px_hadj + si]
    mov [es:bx + PXB_HINT], ax
    ; --- V's first intercept: y where the ray crosses x = xt --------------
    mov ax, [px_tanv]
    cmp ax, PX_TANCLAMP
    je .vpark
    mul word [px_xpq + si]          ; DX:AX = |tan| * xpartial, Q8.8 * int
    mov al, ah
    mov ah, dl                      ; AX = yoff in Q8.8: the middle bytes
    cmp word [px_dyneg + si], 0
    je .vdn
    neg ax
.vdn:
    add ax, [px_py]                 ; yint
    cmp ah, 64
    jae .vpark                      ; off the map (a negative reads >= 0x80)
    mov dh, al
    xor dl, dl                      ; DX = the fraction << 8
    mov al, ah
    xor ah, ah
    add ax, [px_vbase + si]         ; SI-to-be = mapT + xt*64 + vrow
    mov bp, [px_bkey + si]          ; H's key: the walk's
    jmp short .vok
.vpark:
    mov ax, [px_vpark + si]
    xor dx, dx
    mov bp, [px_bfar + si]          ; ...or the far one: H never yields to
                                    ; a parked V (97.2.3)
.vok:
    push ax
    push dx
    ; --- H's: x where the ray crosses y = yt ---------------------------------
    mov ax, [px_tanh]
    cmp ax, PX_TANCLAMP
    je .hpark
    mul word [px_ypq + si]
    mov al, ah
    mov ah, dl                      ; xoff
    cmp word [px_dxneg + si], 0
    je .hdn
    neg ax
.hdn:
    add ax, [px_px]                 ; xint
    cmp ah, 64
    jae .hpark
    mov bh, al
    xor bl, bl                      ; BX = the fraction << 8
    mov al, ah
    xor ah, ah
    add ax, [px_hbase + si]
    mov di, ax                      ; DI = map + yt*64 + hcol
    mov cx, [px_ckey + si]          ; V's key: the walk's
    jmp short .hok
.hpark:
    mov di, [px_hpark + si]
    xor bx, bx
    mov cx, [px_cfar + si]          ; ...or the far one: V never yields to
                                    ; a parked H. Both parked cannot happen
                                    ; with the eye in tiles 1..62 (97.2.3)
.hok:
    ; --- the generation, the walk -------------------------------------------
    mov al, [px_gen]
    pop dx
    pop si
    call far [px_qcur]              ; ...and back with CF = the side
.side:
    jc .hhit
    ; =====================================================================
    ; the V walker hit: SI = mapT + xt*64 + vrow, DX = the fraction
    ; =====================================================================
    mov ah, [si]
    test ah, PXC_DOOR
    jz .vsolid
    test ah, PXC_DOOREW
    jnz .vother
    ; a door across this walker's line (97.2.4): the slab at x = xt + 0.5.
    ; Registers stay the walker's until the slab is known to be struck.
    push ax
    push cx
    mov ax, si
    sub ax, px_mapT
    and ax, 63
    mov ah, al                      ; AH = vrow
    mov al, dh                      ; AX = y88 = vrow.frac
    mov cx, [px_tanv]
    shr cx, 1
    push si
    mov si, [px_qs2]
    cmp word [px_dyneg + si], 0
    pop si
    je .vdp
    neg cx
.vdp:
    add cx, ax                      ; CX = where the ray is at the slab
    cmp ch, ah                      ; still inside the cell?
    je .vslab
.vthru:
    pop cx                          ; no: it left before the slab, into the
    pop ax                          ; jamb the H walker finds - walk on
    mov ax, [px_qcur]
    add ax, PXB_VPASS
    mov [px_qcurv], ax
    mov al, [px_gen]
    call far [px_qcurv]
    jmp short .side
.vslab:
    ; THE SLAB HAS SLID (97.2.4, wave 3): the door's position is how far
    ; its leading edge has moved along the slab, so a ray whose u is under
    ; it passes through the gap and one that strikes the door reads the
    ; texture from that edge - u less the position, BEFORE the mirror
    ; (tools/pxssim.py's doorpos, the same arithmetic). The walker's
    ; registers are still live here: only AX (pushed) and the stack are
    ; spent on the lookup
    push bx
    mov ax, si
    sub ax, px_mapT
    call px_cellT2P                 ; AX = the plain cell of this mapT index
    call px_door_of                 ; AX = its door record (CF: none - a
    jc .vshut                       ; DOOR cell the load did not list reads
    mov bx, ax                      ; as shut)
    mov ax, [bx + PXD_POS]
    jmp short .vpos
.vshut:
    xor ax, ax
.vpos:
    pop bx
    push cx
    xor ch, ch                      ; CX = u
    cmp ax, cx
    pop cx
    ja .vthru                       ; the edge is past this ray: the gap
    ; struck: the walk is over, registers free
    mov [px_hy], cx                 ; hy = the slab's y - BEFORE the edge
                                    ; comes off u: CL is that coordinate's
                                    ; fraction, and the first cut stored CX
                                    ; after the sub, so a half-open door's
                                    ; hit slid along the slab by its
                                    ; position, and the distance, wallh and
                                    ; the sprite z-test with it (review,
                                    ; wave 3; tests/pxssim.py's door sweep)
    sub cl, al                      ; the slab's own texel, from its edge
    mov [px_hu], cl                 ; u, raw
    mov ax, si
    sub ax, px_mapT
    mov cl, 6
    shr ax, cl                      ; xt
    mov ah, al
    mov al, 128
    mov [px_hx], ax                 ; hx = xt + 0.5
    mov al, [si]
    mov cl, 4
    shr al, cl
    mov [px_hmat], al
    add sp, 4                       ; the two pushes above
    jmp .vproj
.vother:                            ; a door of the other orientation
    test ah, PXC_SOLID
    jz .vthrough
.vsolid:
    ; the hit point off the pointer: x = xt (from the west) or xt + 1 (from
    ; the east), y = vrow + the fraction; the jamb from the cell the ray
    ; came through, one row of mapT back (97.2.4)
    mov ax, si
    sub ax, px_mapT
    mov cx, ax
    and cx, 63                      ; vrow
    mov ch, cl
    mov cl, dh                      ; CX = hy = vrow.frac
    mov [px_hy], cx
    mov [px_hu], dh                 ; u raw = the fraction
    mov cl, 6
    shr ax, cl                      ; xt
    mov ah, al
    xor al, al                      ; AX = xt << 8
    mov bx, [px_qs2]
    cmp word [px_dxneg + bx], 0
    je .vwest
    add ax, 256                     ; seen from the east: the far edge
    mov cl, [si + 64]               ; ...and the cell it came through is a
    jmp short .vjamb                ; row of mapT ahead
.vwest:
    mov cl, [si - 64]
.vjamb:
    mov [px_hx], ax
    mov al, [si]
    push cx
    mov cl, 4
    shr al, cl
    pop cx
    test cl, PXC_DOOR
    jz .vmat
    mov al, PXM_JAMB
.vmat:
    mov [px_hmat], al
    jmp short .vproj
.vthrough:                          ; DOOR of the other axis, not solid: pass
    mov ax, [px_qcur]
    add ax, PXB_VPASS
    mov [px_qcurv], ax
    mov al, [px_gen]
    call far [px_qcurv]
    jmp .side
.vproj:
    mov byte [px_hside], 0
    jmp .project
.hhit:
    ; =====================================================================
    ; the H walker hit: DI = map + yt*64 + hcol, BX = the fraction
    ; =====================================================================
    mov ah, [di]
    test ah, PXC_DOOR
    jz .hsolid
    test ah, PXC_DOOREW
    jz .hother
    push ax
    push cx
    mov ax, di
    sub ax, px_map
    and ax, 63
    mov ah, al                      ; AH = hcol
    mov al, bh                      ; AX = x88 = hcol.frac
    mov cx, [px_tanh]
    shr cx, 1
    push si
    mov si, [px_qs2]
    cmp word [px_dxneg + si], 0
    pop si
    je .hdp
    neg cx
.hdp:
    add cx, ax
    cmp ch, ah
    je .hslab
.hthru:
    pop cx
    pop ax
    mov ax, [px_qcur]
    add ax, PXB_HPASS
    mov [px_qcurh], ax
    mov al, [px_gen]
    call far [px_qcurh]
    jmp .side
.hslab:
    push bx                         ; the slide, the V arm's way: the plain
    mov ax, di                      ; map index IS the cell
    sub ax, px_map
    call px_door_of
    jc .hshut
    mov bx, ax
    mov ax, [bx + PXD_POS]
    jmp short .hpos
.hshut:
    xor ax, ax
.hpos:
    pop bx
    push cx
    xor ch, ch
    cmp ax, cx
    pop cx
    ja .hthru
    mov [px_hx], cx                 ; hx = the slab's x, before the edge
    sub cl, al                      ; comes off u (as the V arm)
    mov [px_hu], cl
    mov ax, di
    sub ax, px_map
    mov cl, 6
    shr ax, cl                      ; yt
    mov ah, al
    mov al, 128
    mov [px_hy], ax                 ; hy = yt + 0.5
    mov al, [di]
    mov cl, 4
    shr al, cl
    mov [px_hmat], al
    add sp, 4
    jmp short .hproj
.hother:
    test ah, PXC_SOLID
    jz .hthrough
.hsolid:
    mov ax, di
    sub ax, px_map
    mov cx, ax
    and cx, 63                      ; hcol
    mov ch, cl
    mov cl, bh                      ; CX = hx = hcol.frac
    mov [px_hx], cx
    mov [px_hu], bh
    mov cl, 6
    shr ax, cl                      ; yt
    mov ah, al
    xor al, al                      ; AX = yt << 8
    mov si, [px_qs2]
    cmp word [px_dyneg + si], 0
    je .hnorth
    add ax, 256                     ; seen from the south: the far edge
    mov cl, [di + 64]
    jmp short .hjamb
.hnorth:
    mov cl, [di - 64]
.hjamb:
    mov [px_hy], ax
    mov al, [di]
    push cx
    mov cl, 4
    shr al, cl
    pop cx
    test cl, PXC_DOOR
    jz .hmat
    mov al, PXM_JAMB
.hmat:
    mov [px_hmat], al
    jmp short .hproj
.hthrough:
    mov ax, [px_qcur]
    add ax, PXB_HPASS
    mov [px_qcurh], ax
    mov al, [px_gen]
    call far [px_qcurh]
    jmp .side
.hproj:
    mov byte [px_hside], 1
    ; =====================================================================
    ; the hit (97.2.5): the mirror of u, nx, h, top and bot, the arrays
    ; =====================================================================
.project:
    mov di, [px_col]                ; the column arrays' index
    ; u reads left to right from wherever it is seen: a V hit from the east
    ; and an H hit from the north are mirrored
    mov bx, [px_qs2]
    mov al, [px_hu]
    cmp byte [px_hside], 0
    jne .umh
    cmp word [px_dxneg + bx], 0
    je .ustore
    jmp short .umir
.umh:
    cmp word [px_dyneg + bx], 0
    jne .ustore
.umir:
    not al                          ; 255 - u
.ustore:
    mov [px_u + di], al
    mov al, [px_hmat]
    mov [px_mat + di], al
    mov al, [px_hside]
    mov [px_side + di], al
    ; nx = dx cos + dy sin, MUL14 twice (apps/tank/tk3d.inc's), clamped
    mov ax, [px_hx]
    sub ax, [px_px]
    mov bx, [px_hcos]
    PX_MUL14
    mov cx, ax
    mov ax, [px_hy]
    sub ax, [px_py]
    mov bx, [px_hsin]
    PX_MUL14
    add ax, cx                      ; nx, Q8.8
    cmp ax, PX_MINDIST
    jge .far
    mov ax, PX_MINDIST
.far:
    mov bx, ax
    mov ax, PX_HEIGHTK
    xor dx, dx
    div bx                          ; AX = h, the column's one div
    mov bx, di
    shl bx, 1
    mov [px_wallh + bx], ax         ; the sprite z-buffer, unclipped
    cmp byte [px_rung], PXR_TEX
    jne .noq
    ; TEXTURED: h is QUANTISED to the scaler set's heights (97.3) - over
    ; 120 clamps to the tallest scaler, and every h aliases DOWN through
    ; px_hq to the generated height the compose will call - so top/bot and
    ; the rows the scaler stores are one arithmetic (tools/pxssim.py's
    ; project() does the same under the Textured rung)
    cmp ax, PXG_HMAX
    jbe .hq
    mov ax, PXG_HMAX
.hq:
    mov bx, ax
    mov al, [px_hq + bx]            ; AH is 0: h <= 120
    mov [px_h + di], al
.noq:
    cmp ax, PX_ROWS
    jb .part
    mov byte [px_top + di], 0       ; taller than the view: all of it
    mov byte [px_bot + di], PX_ROWS - 1
    jmp short .next
.part:
    mov bx, PX_ROWS
    sub bx, ax
    shr bx, 1                       ; top = (80 - h) >> 1
    mov [px_top + di], bl
    add bx, ax
    dec bx                          ; bot = top + h - 1
    mov [px_bot + di], bl
.next:
    inc di
    mov [px_col], di
    mov ax, di
    cmp al, [px_cols]
    jae .done
    jmp .col
.done:
    ret
