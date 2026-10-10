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
    mov [sv_ca], al             ; cube in the same attitude
    mov [sv_cb], ah

    mov cx, [sv_w]              ; a centre anywhere the whole figure fits
    sub cx, SV_CR * 2
    call sv_rndn
    add ax, SV_CR
    mov [sv_ccx], ax
    mov cx, [sv_h]
    sub cx, SV_CR * 2
    call sv_rndn
    add ax, SV_CR
    mov [sv_ccy], ax

    mov word [sv_cvx], 3        ; ...and a direction. The two speeds are
    mov word [sv_cvy], 2        ; coprime for the header's reason; only the
    call OSAPI_RAND             ; SIGNS are random, because a slow cube that
    test ah, 1                  ; happens to start along a diagonal would
    jz .sx                      ; retrace it for the whole of its turn. AH and
    neg word [sv_cvx]           ; not AL: bit 0 of the kernel's LCG alternates
.sx:                            ; and bit 1 has period 4, so the low byte gave
    test ah, 2                  ; four attitudes on a loop (sv_rndn's header)
    jz .sy
    neg word [sv_cvy]
.sy:
    ret

; -----------------------------------------------------------------------------
; sv_cube_step - one frame
; in:  the gfx lock is HELD
; -----------------------------------------------------------------------------
sv_cube_step:
    call sv_cube_move
    call sv_cube_proj

    call sv_cube_compose        ; --- the composite (SPEC.md 79.5.6), and
    call sv_cube_put            ; SINCE 79.5.6.1 IT IS THE ONLY PATH. It used
                                ; to be guarded by [sv_cbok] with the
                                ; edge-at-a-time orders behind it as insurance
                                ; against a BLIT1 refusal - and gfx_blit1_x
                                ; refuses four things, of which this call makes
                                ; none: x is forced onto the byte grid, and the
                                ; width and the rows are SV_CBW and SV_CBH,
                                ; constants of 128. kern_small carries the body
                                ; too since SPEC.md 5.4.2.5.1, so there is no
                                ; machine left where the insurance can fire.
                                ; The copy of this frame's vertices that
                                ; followed it fed THAT path's erase and
                                ; nothing else, so it went with it
    ret

; -----------------------------------------------------------------------------
; sv_cube_move - advance the tumble and the centre (module internal)
;
; The bounce is a REFLECTION AND A CLAMP, not a reflection alone: a centre put
; down by sv_cube_init one pixel outside the band - which cannot happen, and
; would be permanent if it did - would reverse on every frame and sit there
; vibrating. Clamping first makes the state legal and the reversal then makes
; it leave.
; -----------------------------------------------------------------------------
sv_cube_move:
    mov al, [sv_ca]
    add al, SV_CASTEP
    mov [sv_ca], al
    mov al, [sv_cb]
    add al, SV_CBSTEP
    mov [sv_cb], al

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
    ret

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

; -----------------------------------------------------------------------------
; sv_cube_proj - every vertex into sv_cpx / sv_cpy (module internal)
;
; Four sines fetched ONCE for the frame rather than per vertex, which is eight
; table reads saved out of thirty-two.
; -----------------------------------------------------------------------------
sv_cube_proj:
    mov al, [sv_ca]             ; the four sines, in saver.asm because the
    mov ah, [sv_cb]             ; geometry mode rotates through them too
    call sv_rotset

    mov si, sv_cube_v
    mov di, 0
    mov cx, SV_CNV
.vert:
    push cx
    push si
    push di
    call sv_rot3                ; -> [sv_rx], [sv_ry]
    pop di
    mov al, [sv_rx]
    cbw
    add ax, [sv_ccx]
    mov [sv_cpx + di], ax
    mov al, [sv_ry]
    cbw
    mov bx, [sv_ccy]
    sub bx, ax                  ; screen y grows DOWN and the model's up
    mov [sv_cpy + di], bx
    add di, 2
    pop si
    add si, 3
    pop cx
    dec cx
    jnz .vert
    ret

; -----------------------------------------------------------------------------
; sv_cube_compose - the whole figure into the mask (SPEC.md 79.5.6)
; in:  [sv_cpx]/[sv_cpy] projected; out: [sv_cmask], [sv_cbx0]/[sv_cby0]
; clobbers: AX, BX, CX, DX, SI, DI, BP
; -----------------------------------------------------------------------------
sv_cube_compose:
    push es
    mov ax, [sv_ccx]            ; the box, forced onto the byte grid. AND on a
    sub ax, SV_CBHX             ; negative x rounds toward minus infinity,
    and ax, 0FFF8h              ; which is the direction that keeps the figure
    mov [sv_cbx0], ax           ; inside the band
    mov ax, [sv_ccy]
    sub ax, SV_CBHY
    mov [sv_cby0], ax

    push ds                     ; ...and a clean mask to draw into
    pop es
    mov di, sv_cmask
    mov cx, SV_CBSZ / 2
    xor ax, ax
    cld
    rep stosw
    pop es

    mov si, sv_cube_e
    mov cx, SV_CNE
.edge:
    push cx
    push si
    mov al, [si]
    call sv_cube_mpt
    mov [sv_cmx1], ax
    mov [sv_cmy1], dx
    pop si
    push si
    mov al, [si+1]
    call sv_cube_mpt
    mov [sv_cmx2], ax
    mov [sv_cmy2], dx
    call sv_cml
    pop si
    add si, 2
    pop cx
    dec cx
    jnz .edge
    ret

; sv_cube_mpt - vertex AL -> AX/DX = its place in the MASK
; clobbers AX, DX, DI, flags
sv_cube_mpt:
    xor ah, ah
    add ax, ax
    mov di, ax
    mov dx, [sv_cpy + di]
    sub dx, [sv_cby0]
    mov ax, [sv_cpx + di]
    sub ax, [sv_cbx0]
    ret

; -----------------------------------------------------------------------------
; sv_cube_put - the band onto the glass, one arrival (SPEC.md 5.4.2)
; out: CF = 1 = REFUSED, nothing drawn
; -----------------------------------------------------------------------------
sv_cube_put:
    push es
    push ds
    pop es                      ; ES:SI = the band, which is ours
    mov si, sv_cmask
    mov bp, SV_CBST
    mov ax, [sv_cbx0]
    mov bx, [sv_cby0]
    mov cx, SV_CBW
    mov dx, SV_CBH
    call OSAPI_GFX_BLIT1
    pop es                      ; `pop` writes no flag, so BLIT1's CF is still
    ret                         ; the caller's answer


; -----------------------------------------------------------------------------
; sv_cml - one edge into the mask, in MASK coordinates
; in:  [sv_cmx1]/[sv_cmy1] -> [sv_cmx2]/[sv_cmy2]
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
; -----------------------------------------------------------------------------
sv_cml:
    mov ax, [sv_cmx1]           ; --- inside the box, both ends?
    or ax, [sv_cmx2]
    js .out
    mov ax, [sv_cmy1]
    or ax, [sv_cmy2]
    js .out
    cmp word [sv_cmx1], SV_CBW
    jae .out
    cmp word [sv_cmx2], SV_CBW
    jae .out
    cmp word [sv_cmy1], SV_CBH
    jae .out
    cmp word [sv_cmy2], SV_CBH
    jb .go
.out:
    ret
.go:
    mov ax, [sv_cmx1]           ; --- left to right
    mov cx, [sv_cmx2]
    cmp ax, cx
    jle .ord
    mov [sv_cmx1], cx
    mov [sv_cmx2], ax
    mov ax, [sv_cmy1]
    mov cx, [sv_cmy2]
    mov [sv_cmy1], cx
    mov [sv_cmy2], ax
.ord:
    mov ax, [sv_cmx2]
    sub ax, [sv_cmx1]
    mov [sv_cmdx], ax
    mov ax, [sv_cmy2]
    sub ax, [sv_cmy1]
    mov word [sv_cmsy], SV_CBST
    jns .dy
    neg ax
    mov word [sv_cmsy], -SV_CBST
.dy:
    mov [sv_cmdy], ax

    mov ax, [sv_cmy1]           ; --- the first byte, and the bit in it
    mov cl, 4
    shl ax, cl                  ; y * SV_CBST, which is why the stride is 16
    mov di, ax
    mov ax, [sv_cmx1]
    mov bx, ax
    mov cl, 3
    shr bx, cl
    add di, bx
    and al, 7
    mov cl, al
    mov bl, 80h
    shr bl, cl

    mov ax, [sv_cmdx]
    cmp ax, [sv_cmdy]
    jl .majy

    mov si, ax                  ; --- x is the major axis
    inc si
    mov bp, [sv_cmdy]
    add bp, bp
    mov [sv_cme1], bp
    sub bp, ax
    add ax, ax
    mov [sv_cme2], ax
.mx:
    or [sv_cmask + di], bl
    shr bl, 1
    jnz .mx1
    mov bl, 80h
    inc di
.mx1:
    add bp, [sv_cme1]
    jle .mx2
    sub bp, [sv_cme2]
    add di, [sv_cmsy]
.mx2:
    dec si
    jnz .mx
    ret

.majy:                          ; --- ...or y is
    mov si, [sv_cmdy]
    inc si
    mov bp, [sv_cmdx]
    add bp, bp
    mov [sv_cme1], bp
    sub bp, [sv_cmdy]
    mov ax, [sv_cmdy]
    add ax, ax
    mov [sv_cme2], ax
.my:
    or [sv_cmask + di], bl
    add di, [sv_cmsy]
    add bp, [sv_cme1]
    jle .my2
    sub bp, [sv_cme2]
    shr bl, 1
    jnz .my2
    mov bl, 80h
    inc di
.my2:
    dec si
    jnz .my
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
sv_cpx:     resw SV_CNV         ; this frame's screen coordinates
sv_cpy:     resw SV_CNV
sv_cbx0:    resw 1              ; the band's origin on the glass
sv_cby0:    resw 1
sv_cmx1:    resw 1              ; sv_cml's endpoints, in the mask
sv_cmy1:    resw 1
sv_cmx2:    resw 1
sv_cmy2:    resw 1
sv_cmdx:    resw 1
sv_cmdy:    resw 1
sv_cmsy:    resw 1              ; the row step, which carries the sign
sv_cme1:    resw 1
sv_cme2:    resw 1
sv_cmask:   resb SV_CBSZ        ; the band itself
%endmacro
