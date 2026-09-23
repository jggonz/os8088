; =============================================================================
; svstars.inc - MODE 2: flying through a starfield (SPEC.md 79.5)
;
; The oldest screen saver there is, and it earns its place here for a reason
; the cube cannot: it fills the WHOLE screen by construction. A star is born
; at a random point of a 2048-unit cube in front of the viewer and walks
; outward along its own radius as z falls, so the pixels a session visits are
; the whole plane rather than a path across it - which is rule 1 of
; saver.asm's header satisfied by the geometry instead of by a bouncing box.
;
; ...ALMOST. A fixed vanishing point is a fixed DARK spot: the middle of the
; screen is where stars are dimmest and rarest, and after eight hours it is
; the one patch that is measurably less worn than the rest. So the origin
; DRIFTS - a pixel every four frames, reflected inside the middle half of the
; screen - which costs two adds and takes the hole for a walk.
;
; THE FRAME WAS 2 CALLS A STAR, an erase and a draw, so SV_NSTAR was the frame
; budget and nothing else: 28 stars was 56 `gfx_*` calls, about 42ms of the
; ~756us fixed cost on the 4.77MHz 8088. **IT IS THREE CALLS A FRAME NOW,
; WHATEVER SV_NSTAR IS** (SPEC.md 79.5.11) - a band a pen plus the erase - so
; a star costs its own arithmetic and nothing else, and the count is bounded
; by the projection's two divides rather than by an arrival.
;
; SO THE FIELD IS 40 AND NOT 28, which is what that bought (79.5.11.4).
; MEASURED with tests/saverate.py at one frame a tick on both 1bpp adapters:
; 28 left 39.8% of a Hercules idle and 40 leaves 18.7%, against 4.4% at 48 and
; nothing at all at 56. It was 22 before 28, which counted 16 lit on a 640x200
; screen and read as empty rather than as deep space.
;
; **THE NEW STAR IS DRAWN BEFORE THE OLD ONE IS TAKEN OFF**, and that order is
; the whole of what stops the field twinkling. Erase-then-draw leaves every
; star absent for the ~756us between its two calls, every frame, and the eye
; reads a strobe rather than a 1% brightness dip - which is exactly what the
; sea mode was reported for (svfish.inc's header). Reversed, a star is never
; absent at any instant.
;
; It is safe HERE and it is not safe for a swimmer, and the difference is
; worth being exact about: a star's two positions are the projection of the
; same model point at two DEPTHS, so they are apart by at least a pixel except
; for the far ones, whose projection can round to the same pixel two frames
; running. That one case would erase the star that was just drawn, so it is
; TESTED - and skipping both calls when nothing moved is a saving as well as a
; correctness fix, since a distant star is most of the field most of the time.
;
; Erasing all of them and then drawing all of them is the third order, and the
; worst: it is one pen change instead of fifty-six and the whole field is
; absent for the length of the erase pass - 21ms of a 55ms frame.
;
; **AND ON A 1bpp ADAPTER NO STAR IS GREY**, which is not a taste decision -
; it is the difference between a star and a star that blinks. `gfx_inktab`
; (SPEC.md 39.4) puts CLGRAY in the 50% dither class, and a 50% dither applied
; to a ONE-PIXEL plot lights the pixel only when its (x + y) parity matches
; the pattern. A far star is therefore invisible about half the time, and
; since it moves diagonally it changes parity most frames: it flashes on and
; off all the way out.
;
; MEASURED on the CGA, counting every lit pixel over 40 frames: grey gave 1287
; of them, 60.1% on one (x + y) parity against 39.9% on the other; white gives
; 1510, split 50.5/49.5. So the dither was costing about 15% of the field
; outright and blinking a third of it - the difference between the two numbers
; is exactly the far stars that were on the wrong square.
;
; This is what "the individual stars still flicker" was, and it survived the
; draw-order fix above BECAUSE IT WAS NEVER THE DRAW ORDER. The two look
; identical from a chair and have nothing to do with each other; the tell is
; that reversing the order fixed the field going dark and left the twinkle
; exactly where it was. What survives on 1bpp as a depth cue is the thing that
; was always doing the work: near stars are a 2x2 blot and the rest are a
; pixel. The colour column is untouched, so VGA still fades them.
;
; THE DIVIDE IS THE ONE THING THAT CAN FAULT HERE and the bounds are chosen so
; it cannot. `idiv` raises interrupt 0 when the quotient will not fit AX, and
; interrupt 0 on this machine is not a signal - it is a jump through a vector
; nothing owns. |x| <= 1023 and the scale is 96, so the dividend is at most
; 98,208; SV_ZMIN is 24, so the quotient is at most 4,092. That is the whole
; argument and it is why SV_ZMIN is a constant rather than "whatever looks
; fast".
; =============================================================================

SV_NSTAR    equ 40
SV_SK       equ 96              ; the projection's scale
SV_SXY      equ 700             ; ...and the cube a star is born in. It was
                                ; 1023 and that is measurably too wide: a star
                                ; is on screen only while |x*k/z| is inside
                                ; the frame, so the outermost ones spent most
                                ; of their run past the edge and were born
                                ; again unseen - counted on a 640x200 screen,
                                ; 16 of 22 lit where 700 puts 20 there
SV_ZMIN     equ 24              ; see the header: this bounds the quotient
SV_ZMAX     equ 1023
SV_ZSTEP    equ 24              ; z a frame, so a star crosses in ~42 frames
SV_ZNEAR    equ 340             ; ...below this it is drawn 2x2 and hot
SV_ZFAR     equ 680             ; ...and above it, dim. Both moved out with
                                ; SV_SXY: the depths are a third of the run
                                ; each, and a star that leaves the frame at
                                ; z = 210 never reached a SV_ZNEAR of 250
SV_ODRIFT   equ 4               ; frames between origin steps
SV_NODRAW   equ 08000h          ; sv_spx: this star is not on the glass

; -----------------------------------------------------------------------------
; sv_star_init - a whole field, at random depths
; in:  the gfx lock is HELD and the screen has just been cleared
;
; THE DEPTHS ARE SPREAD AND NOT ALL AT SV_ZMAX, which is the difference
; between a starfield and a pulse: born together, every star reaches the edge
; together and the screen empties and refills once every two seconds.
; -----------------------------------------------------------------------------
sv_star_init:
    push ax
    push bx
    push cx
    push dx
    push di

    mov ax, [sv_w]              ; the vanishing point starts in the middle and
    shr ax, 1                   ; drifts from there
    mov [sv_sox], ax
    mov ax, [sv_h]
    shr ax, 1
    mov [sv_soy], ax
    mov word [sv_sovx], 1
    mov word [sv_sovy], 1
    mov byte [sv_sodn], SV_ODRIFT

    xor di, di
    mov cx, SV_NSTAR
.one:
    push cx
    mov word [sv_spx + di], SV_NODRAW   ; a SECOND session must not erase at
                                        ; the first one's coordinates: the
                                        ; screen is black under them now
    call sv_star_born
    mov cx, SV_ZMAX - SV_ZMIN
    call sv_rndn
    add ax, SV_ZMIN
    mov [sv_sz + di], ax        ; ...over the WHOLE range, not at the back
    add di, 2
    pop cx
    dec cx
    jnz .one

    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sv_star_born - star DI (a word index) gets a new place at the back
; in:  DI = index*2
; out: nothing kept; clobbers AX, BX, CX, DX
;
; **IT IS PUSHED OFF THE AXIS.** A star born near x = y = 0 spends its whole
; life within a few pixels of the vanishing point and is a dot that brightens
; and vanishes - the field looks sparse at the edges and busy in the middle,
; which is the opposite of what the origin drift above is for. Forcing one
; coordinate out to at least a quarter of the cube costs a compare.
; -----------------------------------------------------------------------------
sv_star_born:
    mov cx, SV_SXY * 2
    call sv_rndn
    sub ax, SV_SXY
    mov [sv_sx + di], ax
    mov cx, SV_SXY * 2
    call sv_rndn
    sub ax, SV_SXY
    mov [sv_sy + di], ax

    mov ax, [sv_sx + di]        ; ...and is either coordinate far enough out?
    call sv_abs
    mov bx, ax
    mov ax, [sv_sy + di]
    call sv_abs
    cmp ax, bx
    ja .big
    mov ax, bx
.big:
    cmp ax, SV_SXY / 4
    jae .ok
    mov word [sv_sx + di], SV_SXY / 2   ; a fixed shove rather than a re-roll,
    mov word [sv_sy + di], -(SV_SXY / 3) ; because a re-roll is a loop with no
.ok:                                    ; bound and the tumble of z makes one
                                        ; repeated birth place invisible
    mov word [sv_sz + di], SV_ZMAX
    ret

; sv_abs - AX = |AX|, signed. clobbers flags
sv_abs:
    or ax, ax
    jns .out
    neg ax
.out:
    ret

; -----------------------------------------------------------------------------
; sv_star_step - one frame
; in:  the gfx lock is HELD
; -----------------------------------------------------------------------------
sv_star_step:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call sv_star_drift

    ; --- PASS 1: fly every star and project it. NOTHING IS DRAWN HERE -------
    xor di, di
    mov si, SV_NSTAR
.one:
    mov ax, [sv_spx + di]       ; bank where it IS - the erase pass below runs
    mov [sv_opx + di], ax       ; after every draw and needs the whole field's
    mov ax, [sv_spy + di]       ; old blots, where the per-star order needed
    mov [sv_opy + di], ax       ; only one at a time
    mov ax, [sv_sw + di]
    mov [sv_ow + di], ax

    mov ax, [sv_sz + di]        ; fly it a step nearer
    sub ax, SV_ZSTEP
    cmp ax, SV_ZMIN
    jg .live
    call sv_star_born
    jmp short .proj
.live:
    mov [sv_sz + di], ax
.proj:
    call sv_star_plot
    add di, 2
    dec si
    jnz .one

    ; --- PASS 2: THE DRAWS, one arrival a band, AND STILL BEFORE THE ERASE --
    ; The order the header insists on is untouched: every star that is going
    ; to be on the glass this frame is on it before one old pixel comes off.
    mov al, SV_MAIN
    xor ah, ah                  ; ...the NEAR band: SV_HOT and SV_MAIN are one
    call sv_star_emit           ; ink since 79.5.11, so it is one arrival
    mov al, SV_DIM
    mov ah, 1
    call sv_star_emit

    ; --- PASS 3: ...and only now the erases ---------------------------------
    call sv_star_erase

    mov byte [sv_shown], 1
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sv_star_pts - one blot onto the point list (module internal)
; in:  AX = x, DX = y, CX = 0 for a pixel or 1 for a 2x2; DI, SI live
; out: nothing; clobbers AX, BX
;
; **THE 2x2 IS FOUR POINTS AND NOT A gfx_fill**, which is the conversion's one
; judgement call and it goes the other way from sv_star_blot's. That routine
; was right that a fill and a pixel are the same ONE arrival - so brightness by
; depth was free - and the whole point here is that there is no arrival left to
; be equal to: the list is going up in a single OSAPI_GFX_POINTS either way, so
; the near star costs four stores against a second ~756us call of its own.
; -----------------------------------------------------------------------------
sv_star_pts:
    push si
    push cx
    mov si, ax                  ; SI = x, banked across the three offsets -
    call gfxe_padd              ; gfxe_padd preserves it, AX, CX and DX and
    or cx, cx                   ; spends only BX
    jz .out
    mov ax, si
    inc ax
    call gfxe_padd              ; (x+1, y)
    inc dx
    mov ax, si
    call gfxe_padd              ; (x,   y+1)
    mov ax, si
    inc ax
    call gfxe_padd              ; (x+1, y+1)
.out:
    pop cx
    pop si
    ret

; -----------------------------------------------------------------------------
; sv_star_emit - every star of one DEPTH BAND onto the glass, in ONE arrival
; in:  AL = the pen (sv_pen's index), AH = 0 for the near band (z <= SV_ZFAR)
;      or 1 for the far one
; out: nothing; clobbers AX, BX, CX, DX
;
; THE BAND IS READ OFF [sv_sz] rather than banked per star, because the pen
; and the size are both functions of depth and depth is already an array. That
; is what lets pass 1 store nothing but the blot.
; -----------------------------------------------------------------------------
sv_star_emit:
    push si
    push di
    push ax
    call sv_pen                 ; AL - and the LIST is per pen, which is why
    call gfxe_pnew              ; there is one pass a band and not one a star
    pop ax
    xor di, di
    mov si, SV_NSTAR
.one:
    cmp word [sv_spx + di], SV_NODRAW
    je .next                    ; this one is not on the glass at all
    mov cx, [sv_sz + di]
    cmp cx, SV_ZFAR
    ja .far
    or ah, ah                   ; near band
    jnz .next
    jmp short .put
.far:
    or ah, ah
    jz .next
.put:
    push ax
    mov ax, [sv_spx + di]
    mov dx, [sv_spy + di]
    mov cx, [sv_sw + di]
    call sv_star_pts
    pop ax
.next:
    add di, 2
    dec si
    jnz .one
    call gfxe_pput              ; ...and the whole band goes up at once
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; sv_star_erase - the whole field's PREVIOUS blots, in one arrival
; out: nothing; clobbers AX, BX, CX, DX
;
; **IT STILL REFUSES TO ERASE THE PIXEL THE DRAW HAS JUST PUT DOWN**, and that
; test is sv_star_off's unchanged: a far star can project to the same pixel two
; frames running, the draws went first, and without it the field would lose
; exactly the stars that move least - which is the ones a starfield has most
; of. It compares the ORIGIN only, as it always did, which is safe because a
; blot never shrinks at a fixed origin: z only falls, so a size goes 1 -> 2x2
; and back only at a rebirth, which moves the star.
; -----------------------------------------------------------------------------
sv_star_erase:
    push si
    push di
    mov al, SV_GROUND
    call sv_pen
    call gfxe_pnew
    xor di, di
    mov si, SV_NSTAR
.one:
    mov ax, [sv_opx + di]
    cmp ax, SV_NODRAW
    je .next                    ; it was not on the glass at all
    cmp ax, [sv_spx + di]
    jne .go
    mov ax, [sv_opy + di]
    cmp ax, [sv_spy + di]
    je .next                    ; the same pixel: the draw already owns it
.go:
    mov ax, [sv_opx + di]
    mov dx, [sv_opy + di]
    mov cx, [sv_ow + di]
    call sv_star_pts
.next:
    add di, 2
    dec si
    jnz .one
    call gfxe_pput
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; sv_star_drift - the vanishing point takes a step every SV_ODRIFT frames
; (module internal); clobbers AX, BX, CX, DX, flags
;
; Bounded to the middle half of the screen. Further out and the field becomes
; one-sided - every star leaving through the same two edges - which is both
; uglier and worse at the job.
; -----------------------------------------------------------------------------
sv_star_drift:
    dec byte [sv_sodn]
    jnz .out
    mov byte [sv_sodn], SV_ODRIFT

    mov ax, [sv_sox]
    mov dx, [sv_sovx]
    add ax, dx
    mov bx, [sv_w]
    mov cx, bx
    shr cx, 1                   ; the low bound is w/4 - TWO shifts, because an
    shr cx, 1                   ; 8086 shifts by 1 or by CL and nothing else
    sub bx, cx                  ; ...and the high one 3w/4
    call sv_bounce
    mov [sv_sox], ax
    mov [sv_sovx], dx

    mov ax, [sv_soy]
    mov dx, [sv_sovy]
    add ax, dx
    mov bx, [sv_h]
    mov cx, bx
    shr cx, 1
    shr cx, 1
    sub bx, cx
    call sv_bounce
    mov [sv_soy], ax
    mov [sv_sovy], dx
.out:
    ret

; -----------------------------------------------------------------------------
; sv_star_plot - project star DI and RECORD where it lands (module internal)
; in:  DI = index*2
; out: nothing; clobbers AX, BX, CX, DX
;
; It used to be sv_star_draw and it used to end in a pen change and a blot;
; both moved out to sv_star_emit, which does them once a BAND instead of once a
; star (SPEC.md 79.5.11). What is left is arithmetic, so a frame's projection
; pass touches the screen not at all.
;
; A star that projects off the screen is BORN AGAIN rather than clipped: the
; primitives would clip it for free, but it would then spend the rest of its
; life invisible and the field would thin out to nothing over a minute.
; -----------------------------------------------------------------------------
sv_star_plot:
    push si
    mov ax, [sv_sx + di]
    call sv_star_proj           ; AX = the offset in x
    add ax, [sv_sox]
    mov bx, ax
    mov ax, [sv_sy + di]
    call sv_star_proj
    add ax, [sv_soy]
    mov cx, ax                  ; BX = x, CX = y

    or bx, bx
    jl .gone
    cmp bx, [sv_w]
    jge .gone
    or cx, cx
    jl .gone
    cmp cx, [sv_h]
    jge .gone

    xor dx, dx                  ; ...and how BIG, by depth. How BRIGHT is not
    mov ax, [sv_sz + di]        ; decided here any more: the pen is per band
    cmp ax, SV_ZNEAR            ; and sv_star_emit reads the band off [sv_sz],
    ja .size                    ; which this array already is
    inc dx
.size:
    mov [sv_sw + di], dx        ; the erase has to know what the draw drew, and
                                ; it is a WORD array indexed the way every
                                ; other one here is - a byte array on a stride
                                ; of two writes over its neighbour, which in a
                                ; file of parallel arrays is the mistake that
                                ; is hardest to see
    mov [sv_spx + di], bx
    mov [sv_spy + di], cx
    pop si
    ret
.gone:
    mov word [sv_spx + di], SV_NODRAW   ; NOTHING IS ON THE GLASS FOR IT NOW,
                                        ; and saying so is what lets
                                        ; sv_star_off tell "the draw already
                                        ; owns this pixel" from "the star left
                                        ; the screen and its last pixel is
                                        ; still lit". Without it a star that
                                        ; ran off the edge left one behind, for
                                        ; the rest of the session
    call sv_star_born           ; ...and it will be projected again next frame
    pop si
    ret

; -----------------------------------------------------------------------------
; sv_star_proj - AX = a model coordinate -> AX = its screen offset
; in:  AX signed, |AX| <= SV_SXY; DI = index*2 (for the depth)
; out: AX; clobbers DX, flags. See the header for why this cannot fault.
; -----------------------------------------------------------------------------
sv_star_proj:
    push bx
    push cx
    mov bx, SV_SK
    imul bx                     ; DX:AX = the signed product
    mov cx, [sv_sz + di]
    idiv cx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sv_star_blot / sv_star_bl ARE GONE (SPEC.md 79.5.11). They put ONE star on
; the glass - a gfx_pixel for a far one, a 2x2 gfx_fill for a near one - and
; the frame called them 56 times. sv_star_pts appends to a list and
; sv_star_emit commits it; what was 56 arrivals is 3.
; -----------------------------------------------------------------------------

sv_sox:     dw 0                ; the vanishing point, and its drift
sv_soy:     dw 0
sv_sovx:    dw 0
sv_sovy:    dw 0
sv_sodn:    db SV_ODRIFT

; SPEC.md 79.5.11: WHERE EACH STAR WAS, for the erase pass. The per-star order
; needed one banked blot at a time (sv_sex/sey/sew, which is what these
; replace); a batched frame draws the whole field before it erases any of it,
; so the whole field's previous blot has to survive pass 1. 168 bytes of an
; OVERLAY - disk and a heap claim while the saver runs, never resident.
sv_opx:     times SV_NSTAR dw SV_NODRAW
sv_opy:     times SV_NSTAR dw 0
sv_ow:      times SV_NSTAR dw 0

sv_sx:      times SV_NSTAR dw 0 ; the model
sv_sy:      times SV_NSTAR dw 0
sv_sz:      times SV_NSTAR dw 0
sv_spx:     times SV_NSTAR dw SV_NODRAW  ; ...and what is on the glass
sv_spy:     times SV_NSTAR dw 0
sv_sw:      times SV_NSTAR dw 0          ; 0 = a pixel, 1 = a 2x2 block
