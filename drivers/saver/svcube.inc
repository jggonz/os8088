; =============================================================================
; svcube.inc - MODE 1: a wireframe cube that tumbles and bounces (SPEC.md 79.5)
;
; WIREFRAME (SPEC.md 78) with the window taken away and the box turned into a
; bouncing ball. The maths is that package's, unchanged and for its reasons:
; two rotations of signed bytes with each term's two products summed at 16
; bits BEFORE the shift, an ORTHOGRAPHIC projection - a perspective divide
; would swing the near corner outside any bound this can be given, and a cube
; with its corners clipped off looks broken rather than fast - and a 256-entry
; sine table because NASM has no sine and the target has no float.
;
; WHAT IS NEW HERE IS THE BOUNCE, and it is the whole reason this is a screen
; saver rather than a demo (rule 1 in saver.asm's header): a figure that
; tumbles in the middle of the glass burns exactly the patch it covers. The
; centre carries a velocity, the velocity reverses at each edge, and the two
; components are coprime steps so the path does not close into a short cycle
; that visits the same pixels all night.
;
; THE FRAME IS 24 LINE CALLS - twelve erased, twelve drawn - which on the
; 4.77MHz 8088 is about 65ms, or a frame every one to two ticks. That is
; measured for WIREFRAME's identical inner loop (SPEC.md 78) and the figure
; here is smaller, so it is a ceiling rather than an estimate.
;
; IT ERASES AND DRAWS EDGE BY EDGE (SPEC.md 78.5) rather than erasing the
; whole figure and then drawing it. That order costs nothing at all - the same
; twenty-four calls - and it is the difference between a cube that is missing
; one edge of twelve at any instant and one that is entirely absent for half
; of every frame, which was measured at 56% of displayed frames blank.
; =============================================================================

SV_CV       equ 24              ; the vertex magnitude. |x'| reaches
                                ; 24*sqrt(2) = 34 after the Y rotation and
                                ; |y'| 24*sqrt(3) = 42 after the X one, so the
                                ; figure is 84px across at its widest and
                                ; SV_CR is what keeps it on the screen
SV_CR       equ 44              ; ...the radius the centre keeps from an edge
SV_CASTEP   equ 3               ; angle steps a frame, of 256 to the turn:
SV_CBSTEP   equ 2               ; coprime, so the tumble does not repeat every
                                ; few seconds
SV_CNV      equ 8
SV_CNE      equ 12

; --- the COMPOSITE (SPEC.md 79.5.6) -----------------------------------------
; The figure is rasterised into a private 1bpp mask and the whole box is put
; down with ONE OSAPI_GFX_BLIT1. The band arrives in final screen polarity, so
; it carries the new figure AND the absence of the old one: no pixel is read,
; none is written twice, and none is ever dark that should be lit.
;
; The box is 128 x 128 and FIXED, which is what makes the frame's cost constant
; - the figure's attitude changes what is in the mask and not what the frame
; costs. Its x is forced onto the byte grid because BLIT1 refuses one that is
; not, and its origin may be slightly NEGATIVE, which BLIT1 clips.
;
; THE MARGIN IS WHAT KEEPS THE OLD FIGURE INSIDE THE NEW BOX. The box moves
; with the centre, at most 3 px in x and 2 in y a frame; the figure reaches
; SV_CR = 54 of a half-width of 64, so 10 px of margin against 3. Without that
; the trailing edge of the last frame's figure would fall outside the band and
; stand there for ever - which is the one way this scheme can leak.
SV_CBW      equ 128             ; the mask, in pixels: a multiple of 8
SV_CBH      equ 128
SV_CBST     equ SV_CBW / 8      ; 16 bytes a row, so y * stride is a shift
SV_CBSZ     equ SV_CBST * SV_CBH
SV_CBHX     equ SV_CBW / 2
SV_CBHY     equ SV_CBH / 2
%if SV_CR + 4 > SV_CBHY
 %error "svcube.inc: the composite box is too short for SV_CR - see the margin"
%endif

; -----------------------------------------------------------------------------
; sv_cube_init - a fresh tumble, at a fresh place, going a fresh way
; in:  the gfx lock is HELD and the screen has just been cleared
; -----------------------------------------------------------------------------
sv_cube_init:
    call OSAPI_RAND             ; the two angles, so a re-pick is not the same
    mov [sv_ca], ax             ; cube in the same attitude: AL to sv_ca, AH to
                                ; sv_cb, which is the next byte

    mov bx, SV_CR               ; a centre anywhere the whole figure fits
    mov di, sv_ccx
    call sv_rndxy

    mov word [sv_cvx], 3        ; ...and a direction. The two speeds are
    mov word [sv_cvy], 2        ; coprime for the header's reason; only the
    call OSAPI_RAND             ; SIGNS are random, because a slow cube that
    shr ah, 1                   ; happens to start along a diagonal would
    jnc .sx                     ; retrace it for the whole of its turn. AH and
    neg word [sv_cvx]           ; not AL: bit 0 of the kernel's LCG alternates
.sx:                            ; and bit 1 has period 4, so the low byte gave
    shr ah, 1                   ; four attitudes on a loop (sv_rndn's header).
    jnc .sy                     ; Two shifts read AH's bits 0 and 1, in that
    neg word [sv_cvy]           ; order, out of CF
.sy:
    ret

; -----------------------------------------------------------------------------
; sv_cube_step - one frame
; in:  the gfx lock is HELD
;
; ONE ROUTINE, IN FOUR PARTS, which used to be four routines with this one
; caller (driver size pass 1): the move, the projection, the composite and the
; put. ES is OURS from the mask's clear to the BLIT1 at the end - nothing in
; between reads the kernel's segment - so it is set once and put back once.
; -----------------------------------------------------------------------------
sv_cube_step:
    ; --- THE MOVE: advance the tumble and the centre ------------------------
    ; The bounce is a REFLECTION AND A CLAMP, not a reflection alone: a centre
    ; put down by sv_cube_init one pixel outside the band - which cannot
    ; happen, and would be permanent if it did - would reverse on every frame
    ; and sit there vibrating. Clamping first makes the state legal and the
    ; reversal then makes it leave.
    add byte [sv_ca], SV_CASTEP
    add byte [sv_cb], SV_CBSTEP

    mov ax, [sv_ccx]
    mov dx, [sv_cvx]
    add ax, dx
    mov bx, [sv_w]
    sub bx, SV_CR
    mov cx, SV_CR
    call sv_bounce
    mov [sv_ccx], ax
    mov [sv_cvx], dx

    mov ax, [sv_ccy]
    mov dx, [sv_cvy]
    add ax, dx
    mov bx, [sv_h]
    sub bx, SV_CR
    mov cx, SV_CR
    call sv_bounce
    mov [sv_ccy], ax
    mov [sv_cvy], dx

    ; --- THE BOX, forced onto the byte grid (SPEC.md 79.5.6). AND on a
    ; negative x rounds toward minus infinity, which is the direction that
    ; keeps the figure inside the band. Taken BEFORE the projection now, so
    ; the vertices are projected straight into MASK coordinates: x lands DX =
    ; ccx - cbx0 into the box and y SV_CBHY - y' down it, which is exactly
    ; what the screen coordinate less the box's origin was
    sub ax, SV_CBHY
    mov [sv_cby0], ax
    mov ax, [sv_ccx]
    sub ax, SV_CBHX
    mov dx, ax
    and ax, 0FFF8h
    mov [sv_cbx0], ax
    sub dx, ax
    add dx, SV_CBHX             ; DX = ccx - cbx0, for every vertex below

    ; --- THE PROJECTION: every vertex into sv_cpx / sv_cpy -----------------
    ; Four sines fetched ONCE for the frame rather than per vertex, which is
    ; eight table reads saved out of thirty-two. sv_rot3 keeps SI, CX and DX,
    ; so the walk banks DI alone
    mov ax, [sv_ca]             ; AL = sv_ca, AH = sv_cb: the four sines, in
    call sv_rotset              ; saver.asm because the geometry rotates too
    mov si, sv_cube_v
    xor di, di
    mov cx, SV_CNV
.vert:
    push di
    call sv_rot3                ; -> [sv_rx], [sv_ry]
    pop di
    mov al, [sv_rx]
    cbw
    add ax, dx
    mov [sv_cpx + di], ax
    mov al, [sv_ry]
    cbw
    mov bx, SV_CBHY
    sub bx, ax                  ; screen y grows DOWN and the model's up
    mov [sv_cpy + di], bx
    inc di
    inc di
    add si, 3
    loop .vert

    ; --- THE COMPOSITE: the whole figure into the mask (SPEC.md 79.5.6) ----
    push es
    push ds                     ; ...and a clean mask to draw into. ES stays
    pop es                      ; ours until the put below
    mov di, sv_cmask
    mov cx, SV_CBSZ / 2
    xor ax, ax
    cld
    rep stosw

    mov si, sv_cube_e           ; the edge list, two vertex bytes an edge, and
.edge:                          ; its END is the loop's bound
    lodsb
    call sv_cube_mpt
    push ax
    push dx
    lodsb
    call sv_cube_mpt
    xchg ax, cx                 ; CX/BX = the far end
    mov bx, dx
    pop dx                      ; AX/DX = the near one
    pop ax
    push si
    call sv_cml
    pop si
    cmp si, sv_cube_e + SV_CNE * 2
    jb .edge

    ; --- THE PUT: the band onto the glass, one arrival (SPEC.md 5.4.2) -----
    mov si, sv_cmask            ; ES:SI = the band, which is ours
    mov bp, SV_CBST
    mov ax, [sv_cbx0]
    mov bx, [sv_cby0]
    mov cx, SV_CBW
    mov dx, SV_CBH
    call OSAPI_GFX_BLIT1        ; ...and SINCE 79.5.6.1 IT IS THE ONLY PATH.
    pop es                      ; It used to be guarded by [sv_cbok] with the
    ret                         ; edge-at-a-time orders behind it as insurance
                                ; against a BLIT1 refusal - and gfx_blit1_x
                                ; refuses four things, of which this call
                                ; makes none: x is forced onto the byte grid,
                                ; and the width and the rows are SV_CBW and
                                ; SV_CBH, constants of 128. kern_small carries
                                ; the body too since SPEC.md 5.4.2.5.1, so
                                ; there is no machine left where the insurance
                                ; can fire. The copy of this frame's vertices
                                ; that followed it fed THAT path's erase and
                                ; nothing else, so it went with it. `pop`
                                ; writes no flag, so BLIT1's CF is the answer

; -----------------------------------------------------------------------------
; sv_bounce - keep AX inside [CX, BX], reversing the velocity at each end
; in:  AX = the new coordinate, DX = the velocity that produced it,
;      CX = the low bound, BX = the high one
; out: AX = clamped, DX = the velocity to keep; clobbers flags
;
; THE VELOCITY IS AN ARGUMENT AND AN ANSWER, so the caller stores both words
; and this routine owns neither. Written as one routine because there are four
; call sites across two modes and an axis reflected in three of them and not
; the fourth is exactly the bug this shape cannot have.
; -----------------------------------------------------------------------------
sv_bounce:
    cmp ax, cx
    jle .lo
    cmp ax, bx
    jge .hi
    ret
.lo:
    mov ax, cx
    neg dx
    ret
.hi:
    mov ax, bx
    neg dx
    ret

; sv_cube_mpt - vertex AL -> AX/DX = its place in the MASK, which is what
; sv_cpx/sv_cpy hold now; clobbers AX, DX, DI, flags. AL is 0..7, so `cbw`
; is the zero extension
sv_cube_mpt:
    cbw
    add ax, ax
    xchg ax, di
    mov dx, [sv_cpy + di]
    mov ax, [sv_cpx + di]
    ret

; -----------------------------------------------------------------------------
; sv_cml - one edge into the mask, in MASK coordinates
; in:  AX = x1, DX = y1, CX = x2, BX = y2
; out: nothing; clobbers AX, BX, CX, DX, SI, DI, BP
;
; ORDERED LEFT TO RIGHT FIRST, so the x step is always +1 and only the y step
; carries a sign - which halves the octants this has to know about. It may:
; nothing here is incremental, so the pixel set is all that has to be right and
; the direction it is laid in does not matter (SPEC.md 5.6.7's rule is about
; walks that get replayed, and this one never is).
;
; **IT REFUSES AN EDGE THAT WOULD LEAVE THE MASK**, which the geometry says
; cannot happen: the margin argument at the top of this file is what makes it
; so. The test is here because the failure without it is not a wrong line, it
; is a write through DI into whatever of the overlay's own image lies past the
; mask - and that is silent until the rotted byte is something we execute.
; It is ONE test of the four ends OR-ed together: a word is inside 0..127
; exactly when none of bits 7..15 is set, negatives included.
;
; THE WALK IS IN REGISTERS (driver size pass 1): the pixel bit in AL, the
; byte in DI, the count in CX, the error in BP, its two steps in SI and DX and
; the row step in BX - where the error steps and the row step used to be read
; out of memory at every pixel. The bit moves with `ror`, which carries the
; rightmost pixel's bit round to the next byte's leftmost and says so in CF.
; -----------------------------------------------------------------------------
sv_cml:
    mov si, ax                  ; --- inside the box, all four?
    or si, dx
    or si, cx
    or si, bx
    test si, 0FF80h
    jnz .out
    cmp ax, cx                  ; --- left to right
    jle .ord
    xchg ax, cx
    xchg dx, bx
.ord:
    sub cx, ax                  ; CX = dx, never negative
    sub bx, dx                  ; BX = dy, and the row step carries its sign
    mov bp, SV_CBST
    jns .dy
    neg bx
    neg bp
.dy:
    mov si, cx                  ; --- the first byte, and the bit in it
    mov di, dx
    mov cl, 4
    shl di, cl                  ; y * SV_CBST, which is why the stride is 16
    mov cl, al
    and cl, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax
    mov al, 80h
    shr al, cl
    mov cx, si                  ; CX = dx again, BX = dy, BP = the row step

    cmp cx, bx
    jl .majy

    xchg bx, bp                 ; --- x is the major axis: BX = the row step,
    add bp, bp                  ; BP = 2dy
    mov si, bp                  ; SI = 2dy, the error's every step
    sub bp, cx                  ; BP = 2dy - dx, the error
    mov dx, cx
    add dx, dx                  ; DX = 2dx, its step back
    inc cx                      ; dx + 1 pixels
.mx:
    or [sv_cmask + di], al
    ror al, 1
    jnc .mx1
    inc di
.mx1:
    add bp, si
    jle .mx2
    sub bp, dx
    add di, bx
.mx2:
    loop .mx
.out:
    ret

.majy:                          ; --- ...or y is
    mov si, cx
    add si, si                  ; SI = 2dx, the error's every step
    mov dx, bx
    add dx, dx                  ; DX = 2dy, its step back
    mov cx, bx
    inc cx                      ; dy + 1 pixels
    xchg bx, bp                 ; BX = the row step, BP = dy
    neg bp
    add bp, si                  ; BP = 2dx - dy, the error
.my:
    or [sv_cmask + di], al
    add di, bx
    add bp, si
    jle .my2
    sub bp, dx
    ror al, 1
    jnc .my2
    inc di
.my2:
    loop .my
    ret

; --- the solid, and it is the only one: a screen saver has no View menu ------
sv_cube_v:
    db -SV_CV, -SV_CV, -SV_CV
    db  SV_CV, -SV_CV, -SV_CV
    db  SV_CV,  SV_CV, -SV_CV
    db -SV_CV,  SV_CV, -SV_CV
    db -SV_CV, -SV_CV,  SV_CV
    db  SV_CV, -SV_CV,  SV_CV
    db  SV_CV,  SV_CV,  SV_CV
    db -SV_CV,  SV_CV,  SV_CV

sv_cube_e:
    db 0,1,  1,2,  2,3,  3,0    ; the back face
    db 4,5,  5,6,  6,7,  7,4    ; the front
    db 0,4,  1,5,  2,6,  3,7    ; ...and the four that join them

; THE MODE'S STATE IS A MACRO, emitted once at the end of saver.asm into the
; mode UNION: only one mode runs at a time and every byte below is written by
; sv_cube_init or by the frame before it is read, so the four modes share one
; block of bss rather than carrying one each (saver.asm, "The mode union").
%macro SV_CUBE_STATE 0
sv_ca:      resb 1              ; the two angles, 256 to the turn
sv_cb:      resb 1
sv_ccx:     resw 1              ; the centre, and where it is going
sv_ccy:     resw 1
sv_cvx:     resw 1
sv_cvy:     resw 1
sv_cpx:     resw SV_CNV         ; this frame's coordinates, in the MASK
sv_cpy:     resw SV_CNV
sv_cbx0:    resw 1              ; the band's origin on the glass
sv_cby0:    resw 1
sv_cmask:   resb SV_CBSZ        ; the band itself
%endmacro
