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
    mov ax, [sv_w]              ; the vanishing point starts in the middle and
    shr ax, 1                   ; drifts from there
    mov [sv_sox], ax
    mov ax, [sv_h]
    shr ax, 1
    mov [sv_soy], ax
    mov ax, 1
    mov [sv_sovx], ax
    mov [sv_sovy], ax
    mov byte [sv_sodn], SV_ODRIFT

    xor di, di
.one:
    mov word [sv_spx + di], SV_NODRAW   ; a SECOND session must not erase at
                                        ; the first one's coordinates: the
                                        ; screen is black under them now
    call sv_star_born           ; (which keeps DI)
    mov cx, SV_ZMAX - SV_ZMIN
    call sv_rndn
    add ax, SV_ZMIN
    mov [sv_sz + di], ax        ; ...over the WHOLE range, not at the back
    inc di
    inc di
    cmp di, SV_NSTAR * 2
    jb .one
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
;
; |x| is `cwd / xor / sub`: DX is the sign, and the two undo it. sv_rndn keeps
; CX, so the bound is loaded once for both coordinates.
; -----------------------------------------------------------------------------
sv_star_born:
    mov cx, SV_SXY * 2
    call sv_rndn
    sub ax, SV_SXY
    mov [sv_sx + di], ax
    cwd
    xor ax, dx
    sub ax, dx
    xchg ax, bx                 ; BX = |x|
    call sv_rndn
    sub ax, SV_SXY
    mov [sv_sy + di], ax
    cwd
    xor ax, dx
    sub ax, dx                  ; AX = |y|
    cmp ax, bx                  ; ...and is either coordinate far enough out?
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

; -----------------------------------------------------------------------------
; sv_star_step - one frame
; in:  the gfx lock is HELD
;
; It is ONE ROUTINE with the drift, the projection and the erase written into
; it, which were each a routine with this one caller (driver size pass 1); the
; two bands' draws are one routine called twice. Every star's index is DI.
; -----------------------------------------------------------------------------
sv_star_step:
    ; --- THE DRIFT: the vanishing point takes a step every SV_ODRIFT frames.
    ; Bounded to the middle half of the screen. Further out and the field
    ; becomes one-sided - every star leaving through the same two edges -
    ; which is both uglier and worse at the job. The two axes are one walk:
    ; sv_sox/sv_soy, sv_sovx/sv_sovy and sv_w/sv_h are each a pair
    dec byte [sv_sodn]
    jnz .still
    mov byte [sv_sodn], SV_ODRIFT
    xor si, si
.axis:
    mov ax, [sv_sox + si]
    mov dx, [sv_sovx + si]
    add ax, dx
    mov bx, [sv_w + si]
    mov cx, bx
    shr cx, 1                   ; the low bound is w/4 - TWO shifts, because an
    shr cx, 1                   ; 8086 shifts by 1 or by CL and nothing else
    sub bx, cx                  ; ...and the high one 3w/4
    call sv_bounce
    mov [sv_sox + si], ax
    mov [sv_sovx + si], dx
    inc si
    inc si
    cmp si, 4
    jb .axis
.still:

    ; --- BANK WHERE EVERY STAR IS - the erase pass below runs after every
    ; draw and needs the whole field's old blots, where the per-star order
    ; needed only one at a time. sv_spx/spy/sw and sv_opx/opy/ow are each
    ; three arrays end to end, so it is one block move
    push es
    push ds
    pop es
    mov si, sv_spx
    mov di, sv_opx
    mov cx, SV_NSTAR * 3
    cld
    rep movsw
    pop es

    ; --- PASS 1: fly every star and project it. NOTHING IS DRAWN HERE -------
    xor di, di
.one:
    mov ax, [sv_sz + di]        ; fly it a step nearer
    sub ax, SV_ZSTEP
    cmp ax, SV_ZMIN
    jg .live
    call sv_star_born
    jmp short .proj
.live:
    mov [sv_sz + di], ax
.proj:
    ; --- ...and project it and RECORD where it lands. A star that projects
    ; off the screen is BORN AGAIN rather than clipped: the primitives would
    ; clip it for free, but it would then spend the rest of its life invisible
    ; and the field would thin out to nothing over a minute. The divide cannot
    ; fault - see the header for why. One UNSIGNED compare a coordinate is
    ; both of its bounds, because a negative one is above any width
    mov ax, [sv_sx + di]
    mov cx, SV_SK
    imul cx                     ; DX:AX = the signed product
    idiv word [sv_sz + di]
    add ax, [sv_sox]
    xchg ax, bx                 ; BX = x
    mov ax, [sv_sy + di]
    imul cx
    idiv word [sv_sz + di]
    add ax, [sv_soy]
    xchg ax, cx                 ; CX = y
    cmp bx, [sv_w]
    jae .gone
    cmp cx, [sv_h]
    jae .gone

    cmp word [sv_sz + di], SV_ZNEAR + 1 ; ...and how BIG, by depth: 1 for a
    sbb dx, dx                  ; 2x2 at z <= SV_ZNEAR, else 0. How BRIGHT is
    neg dx                      ; not decided here: the pen is per band and
    mov [sv_sw + di], dx        ; sv_star_emit reads the band off [sv_sz].
    mov [sv_spx + di], bx       ; The erase has to know what the draw drew,
    mov [sv_spy + di], cx       ; and it is a WORD array indexed the way every
    jmp short .next             ; other one here is - a byte array on a stride
                                ; of two writes over its neighbour, which in a
                                ; file of parallel arrays is the mistake that
                                ; is hardest to see
.gone:
    mov word [sv_spx + di], SV_NODRAW   ; NOTHING IS ON THE GLASS FOR IT NOW,
                                        ; and saying so is what lets the erase
                                        ; tell "the draw already owns this
                                        ; pixel" from "the star left the
                                        ; screen and its last pixel is still
                                        ; lit". Without it a star that ran off
                                        ; the edge left one behind, for the
                                        ; rest of the session
    call sv_star_born           ; ...and it will be projected again next frame
.next:
    inc di
    inc di
    cmp di, SV_NSTAR * 2
    jb .one

    ; --- PASS 2: THE DRAWS, one arrival a band, AND STILL BEFORE THE ERASE --
    ; The order the header insists on is untouched: every star that is going
    ; to be on the glass this frame is on it before one old pixel comes off.
    mov al, SV_MAIN
    mov bp, -1                  ; ...the NEAR band: SV_HOT and SV_MAIN are one
    call sv_star_emit           ; ink since 79.5.11, so it is one arrival
    mov al, SV_DIM
    xor bp, bp
    call sv_star_emit

    ; --- PASS 3: ...and only now the erases, the whole field's PREVIOUS blots
    ; in one arrival. **IT STILL REFUSES TO ERASE THE PIXEL THE DRAW HAS JUST
    ; PUT DOWN**: a far star can project to the same pixel two frames running,
    ; the draws went first, and without the test the field would lose exactly
    ; the stars that move least - which is the ones a starfield has most of.
    ; It compares the ORIGIN only, as it always did, which is safe because a
    ; blot never shrinks at a fixed origin: z only falls, so a size goes 1 ->
    ; 2x2 and back only at a rebirth, which moves the star
    mov al, SV_GROUND
    call sv_pen
    call gfxe_pnew
    xor di, di
.erase:
    mov ax, [sv_opx + di]
    cmp ax, SV_NODRAW
    je .enext                   ; it was not on the glass at all
    mov dx, [sv_opy + di]
    cmp ax, [sv_spx + di]
    jne .ego
    cmp dx, [sv_spy + di]
    je .enext                   ; the same pixel: the draw already owns it
.ego:
    mov cx, [sv_ow + di]
    call sv_star_pts
.enext:
    inc di
    inc di
    cmp di, SV_NSTAR * 2
    jb .erase
    jmp gfxe_pput

; -----------------------------------------------------------------------------
; sv_star_pts - one blot onto the point list (module internal)
; in:  AX = x, DX = y, CX = 0 for a pixel or 1 for a 2x2
; out: nothing; clobbers DX, BX
;
; **THE 2x2 IS FOUR POINTS AND NOT A gfx_fill**, which is the conversion's one
; judgement call and it goes the other way from sv_star_blot's. That routine
; was right that a fill and a pixel are the same ONE arrival - so brightness by
; depth was free - and the whole point here is that there is no arrival left to
; be equal to: the list is going up in a single OSAPI_GFX_POINTS either way, so
; the near star costs four stores against a second ~756us call of its own.
;
; gfxe_padd keeps AX, CX and DX and spends only BX, so the four corners are
; stepped to in AX and DX themselves.
; -----------------------------------------------------------------------------
sv_star_pts:
    call gfxe_padd              ; (x,   y)
    jcxz .out
    inc ax
    call gfxe_padd              ; (x+1, y)
    inc dx
    dec ax
    call gfxe_padd              ; (x,   y+1)
    inc ax
    call gfxe_padd              ; (x+1, y+1)
.out:
    ret

; -----------------------------------------------------------------------------
; sv_star_emit - every star of one DEPTH BAND onto the glass, in ONE arrival
; in:  AL = the pen (sv_pen's index), BP = 0FFFFh for the near band
;      (z <= SV_ZFAR) or 0 for the far one
; out: nothing; clobbers AX, BX, CX, DX, DI
;
; THE BAND IS READ OFF [sv_sz] rather than banked per star, because the pen
; and the size are both functions of depth and depth is already an array. That
; is what lets pass 1 store nothing but the blot. A star's band is the borrow
; of one compare, spread over CX by `sbb`, and BP is the band asked for.
; -----------------------------------------------------------------------------
sv_star_emit:
    call sv_pen                 ; AL - and the LIST is per pen, which is why
    call gfxe_pnew              ; there is one pass a band and not one a star
    xor di, di
.one:
    mov ax, [sv_spx + di]
    cmp ax, SV_NODRAW
    je .next                    ; this one is not on the glass at all
    cmp word [sv_sz + di], SV_ZFAR + 1
    sbb cx, cx                  ; 0FFFFh near, 0 far
    cmp cx, bp
    jne .next
    mov dx, [sv_spy + di]
    mov cx, [sv_sw + di]
    call sv_star_pts
.next:
    inc di
    inc di
    cmp di, SV_NSTAR * 2
    jb .one
    jmp gfxe_pput               ; ...and the whole band goes up at once

; -----------------------------------------------------------------------------
; sv_star_blot / sv_star_bl ARE GONE (SPEC.md 79.5.11). They put ONE star on
; the glass - a gfx_pixel for a far one, a 2x2 gfx_fill for a near one - and
; the frame called them 56 times. sv_star_pts appends to a list and
; sv_star_emit commits it; what was 56 arrivals is 3.
; -----------------------------------------------------------------------------

; The mode's state is a macro emitted into saver.asm's mode union (see
; SV_CUBE_STATE): sv_star_init writes sv_sox..sv_sodn and every star's
; sv_spx/sv_sx/sv_sy/sv_sz, and each frame writes sv_opx/opy/ow from those
; before the erase reads them, so nothing here needs a load-time value.
%macro SV_STAR_STATE 0
sv_sox:     resw 1              ; the vanishing point, and its drift
sv_soy:     resw 1
sv_sovx:    resw 1
sv_sovy:    resw 1
sv_sodn:    resb 1

; SPEC.md 79.5.11: WHERE EACH STAR WAS, for the erase pass. The per-star order
; needed one banked blot at a time (sv_sex/sey/sew, which is what these
; replace); a batched frame draws the whole field before it erases any of it,
; so the whole field's previous blot has to survive pass 1.
sv_opx:     resw SV_NSTAR
sv_opy:     resw SV_NSTAR
sv_ow:      resw SV_NSTAR

sv_sx:      resw SV_NSTAR       ; the model
sv_sy:      resw SV_NSTAR
sv_sz:      resw SV_NSTAR
sv_spx:     resw SV_NSTAR       ; ...and what is on the glass
sv_spy:     resw SV_NSTAR
sv_sw:      resw SV_NSTAR       ; 0 = a pixel, 1 = a 2x2 block
%endmacro
