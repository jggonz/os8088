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
; THE FRAME IS 2 CALLS A STAR, an erase and a draw, so SV_NSTAR is the frame
; budget and nothing else: 28 stars is 56 `gfx_*` calls, about 42ms of the
; ~756us fixed cost on the 4.77MHz 8088 - a frame a tick with the divides
; taking it a little past one, which for a starfield is a frame rate and not a
; deadline. Raising it is a straight trade against that and nothing else
; changes. It was 22, which counted 16 lit on a 640x200 screen and read as
; empty rather than as deep space.
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

SV_NSTAR    equ 28
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

    xor di, di
    mov si, SV_NSTAR
.one:
    mov ax, [sv_spx + di]       ; bank where it IS: the erase happens AFTER
    mov [sv_sex], ax            ; the new one is on the glass
    mov ax, [sv_spy + di]
    mov [sv_sey], ax
    mov ax, [sv_sw + di]
    mov [sv_sew], ax

    mov ax, [sv_sz + di]        ; fly it a step nearer
    sub ax, SV_ZSTEP
    cmp ax, SV_ZMIN
    jg .live
    call sv_star_born
    jmp short .proj
.live:
    mov [sv_sz + di], ax
.proj:
    call sv_star_draw
    call sv_star_off            ; ...and only now does the old one go
    add di, 2
    dec si
    jnz .one

    mov byte [sv_shown], 1
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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
; sv_star_off - erase star DI where it USED to be (module internal)
; in:  DI = index*2, and the banked previous blot in [sv_sex]/[sv_sey]/[sv_sew]
; out: nothing; clobbers AX, BX, CX, DX
;
; **IT REFUSES TO ERASE THE PIXEL THE DRAW HAS JUST PUT DOWN.** A far star can
; project to the same pixel two frames running, and the draw goes first now
; (sv_star_step's header) - so without this test the field would lose exactly
; the stars that move least, which is the ones a starfield has most of.
; -----------------------------------------------------------------------------
sv_star_off:
    cmp word [sv_sex], SV_NODRAW
    je .out                     ; it was not on the glass at all
    mov ax, [sv_sex]
    cmp ax, [sv_spx + di]
    jne .go
    mov ax, [sv_sey]
    cmp ax, [sv_spy + di]
    je .out                     ; the same pixel: the draw already owns it
.go:
    push si
    push di
    mov al, SV_GROUND
    call sv_pen
    mov ax, [sv_sex]            ; sv_star_blot reads the LIVE arrays, so the
    mov bx, [sv_sey]            ; banked blot is handed over directly
    mov cx, [sv_sew]
    call sv_star_bl
    pop di
    pop si
.out:
    ret

; -----------------------------------------------------------------------------
; sv_star_draw - project star DI and put it on the glass (module internal)
; in:  DI = index*2
; out: nothing; clobbers AX, BX, CX, DX
;
; A star that projects off the screen is BORN AGAIN rather than clipped: the
; primitives would clip it for free, but it would then spend the rest of its
; life invisible and the field would thin out to nothing over a minute.
; -----------------------------------------------------------------------------
sv_star_draw:
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

    xor dx, dx                  ; ...and how big and how bright, by depth
    mov ax, [sv_sz + di]
    cmp ax, SV_ZNEAR
    ja .mid
    mov al, SV_HOT
    inc dx
    jmp short .pen
.mid:
    cmp ax, SV_ZFAR
    ja .far
    mov al, SV_MAIN
    jmp short .pen
.far:
    mov al, SV_DIM
.pen:
    mov [sv_sw + di], dx        ; the erase has to know what the draw drew, and
                                ; it is a WORD array indexed the way every
                                ; other one here is - a byte array on a stride
                                ; of two writes over its neighbour, which in a
                                ; file of parallel arrays is the mistake that
                                ; is hardest to see
    call sv_pen
    mov [sv_spx + di], bx
    mov [sv_spy + di], cx
    call sv_star_blot
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
; sv_star_blot - one star at [sv_spx/spy + DI], at [sv_sw + DI]'s size
; in:  DI = index*2, the pen already set
; out: nothing; clobbers AX, BX, CX, DX
;
; ONE CALL EITHER WAY, which is what makes the near stars free: a 2x2 fill and
; a single pixel are both one `gfx_*` arrival (PERFORMANCE.md), so brightness
; by depth costs a compare rather than a second primitive.
; -----------------------------------------------------------------------------
sv_star_blot:
    mov ax, [sv_spx + di]
    mov bx, [sv_spy + di]
    mov cx, [sv_sw + di]
sv_star_bl:                     ; ...and the same blot from three loose words,
    or cx, cx                   ; which is what the erase has (see sv_star_off)
    je .dot
    mov cx, ax
    inc cx
    mov dx, bx
    inc dx
    call OSAPI_GFX_FILL
    ret
.dot:
    mov cx, ax
    mov dx, bx
    call OSAPI_GFX_PIXEL
    ret

sv_sox:     dw 0                ; the vanishing point, and its drift
sv_soy:     dw 0
sv_sovx:    dw 0
sv_sovy:    dw 0
sv_sodn:    db SV_ODRIFT
sv_sex:     dw SV_NODRAW        ; the blot to take off AFTER the new one is
sv_sey:     dw 0                ; down (see sv_star_step's header)
sv_sew:     dw 0

sv_sx:      times SV_NSTAR dw 0 ; the model
sv_sy:      times SV_NSTAR dw 0
sv_sz:      times SV_NSTAR dw 0
sv_spx:     times SV_NSTAR dw SV_NODRAW  ; ...and what is on the glass
sv_spy:     times SV_NSTAR dw 0
sv_sw:      times SV_NSTAR dw 0          ; 0 = a pixel, 1 = a 2x2 block
