; =============================================================================
; svfish.inc - MODE 4: fish and sea life (SPEC.md 79.5)
;
; Four swimmers - fish and jellyfish - crossing a black sea with bubbles
; rising through them, each of them ANIMATED: a fish's tail pumps through
; three positions and a jellyfish's bell contracts and relaxes.
;
; --- ONE CALL A SWIMMER, AND NOTHING IS EVER ERASED -------------------------
;
; This mode has been written three times and each rewrite is a different
; lesson. The first is why nothing is erased; the third is why it keeps up.
;
; **DRAFT 1 - six primitives a swimmer, and it FLICKERED.** Three fills for a
; tapering body, two lines for the tail, a pixel for the eye, and the box it
; had last occupied blanked before any of them. That is erase-then-draw,
; PERFORMANCE.md rule 2's canonical violation, taken deliberately because a
; fish does not FILL its bounding box: Cyclone's draw-first-then-erase-the-
; remainder order (SPEC.md 67) leaves the old TAIL standing wherever it fell
; inside the new box. The cost was supposed to be a small rectangle absent for
; the length of six drawing calls. Reported from a real screen: six calls at
; SPEC.md 5.7's ~756us arrival is 4.5ms with the sprite off the glass, and it
; is off the glass EVERY FRAME - so what the eye gets is not a 4% brightness
; dip, it is a sprite that strobes.
;
; **DRAFT 2 - one opaque blit a swimmer, and the flicker went.** A swimmer
; became a block in this image's own RAM, drawn in ONE call that lays down its
; background as well as its ink - so it is opaque, and drawing it over where
; it used to be IS the erase. Better still, the block handed over is the UNION
; of the old box and the new one, with the sprite painted at the new offset
; and ground everywhere else, so a single blit draws the swimmer AND takes up
; its trail. Nothing is absent at any instant, there is no second call to be
; interrupted between, and the frame went from ~40 `gfx_*` arrivals to 12.
; That is also what makes the animation free: the frame is chosen out of the
; swimmer's own phase, and a different 16-byte bitmap costs the same one blit.
;
; **DRAFT 3 - the same union, one bit a pixel instead of four, and THAT is
; the frame rate.** Draft 2's block was 4bpp and went down through
; `OSAPI_GFX_BLIT4`, which on a 1bpp adapter decodes it a pixel-pair at a time
; through `sw_blit_row`. Measured on MartyPC, a cycle-accurate 4.77MHz 8088
; with a CGA: **2,863 cycles a ROW** for a 36-pixel row, and the width barely
; entered it - most of a row is the pair walk, a `gfx_rowbase` multiply and
; nine pushes. A scale-2 swimmer's eighteen rows were **10.2 ms**, four of
; them were 41 ms of a **73 ms** frame, and a frame that does not fit the
; 54.9 ms tick costs the whole of the next one (SPEC.md 79.5.7): the mode ran
; at 12 fps swinging 9.2-17.8 while the other three sat on 18.2.
;
; The swimmers were 1bpp art all along - twelve bitmaps, one bit a pixel - and
; `OSAPI_GFX_BLIT1` (SPEC.md 5.4.2) takes exactly that and emits it with a
; `rep movsw` a row. So the union is composed as a BAND now: five or six bytes
; a row instead of eighteen, no per-pixel decode at either end, and the pen
; carries the colour that used to be in the pixels. The arrival goes **10.20
; -> 1.26 ms** and the frame **73.4 -> 32.8**, so the PASS is **54.93 ms on
; all three adapters - one tick, flat to 54.90-54.95 over 199 consecutive
; frames - against 76.2 ms and 13.1 fps before**. The mode holds 18.2 fps for
; every sea the generator can roll, with no cap on what it may roll and not
; one pixel of the picture changed. SPEC.md 79.5.8 is the account and the
; table, and tests/fishfit.py is the gate.
;
; --- THE THINGS THAT MAKE THAT WORK, EACH OF WHICH CAN BE GOT WRONG ---------
;
;   * **ES IS OURS FOR THE BLIT AND THE KERNEL'S EVERYWHERE ELSE.** The slot's
;     contract says ES:SI, "ES is yours here - no stub is involved". A package
;     callback runs with ES = KERNEL_SEG (SPEC.md 20.1), so it is set and put
;     back around the one call; forget that and the kernel draws 300 bytes of
;     its own `.text` as a picture.
;
;   * **A BAND STARTS ON THE BYTE GRID AND A SWIMMER DOES NOT.** gfx_blit1
;     refuses an x or a width off it. So the band is the union block rounded
;     DOWN to the grid and widened out to it, and the sprite is shifted into
;     it by whatever is left over - which is `sv_bnd_shift`, a byte walk with
;     one carry, uniform over every shift from 0 to 7.
;
;   * **THE BLIT CLIPS AND THIS FILE NO LONGER DOES.** Draft 2 had to cut the
;     block to the screen itself, because gfx_blit4's fast path cannot clip in
;     x - a left cut advances a 4bpp source by a pixel count that may be odd,
;     which shifts the nibble phase of every pair after it. gfx_blit1 clips in
;     both axes and does it EXACTLY: every extent it meets is a multiple of 8
;     and so is the band, so whole byte columns simply drop. `sv_fish_clip`
;     and its even-cut invariant went with draft 2.
;
;   * **A JELLYFISH IS DITHERED AND A BAND HAS NO GREY.** On a 1bpp adapter
;     the pen is NOT READ (SPEC.md 5.4.2.2) - a set bit is lit, and nothing a
;     caller does changes that - so the mono column's CLGRAY cannot arrive
;     through it. `sv_bnd_build` composes the dither into the band instead,
;     and it has to be the kernel's own: `sw_parity` lays 0xAA on an even
;     screen row and 0x55 on an odd one, bit 7 leftmost, so a pixel is lit
;     when (x + y) is even (SPEC.md 39.4).
;
;   * **A RESPAWN IS NOT A MOVE.** A reborn swimmer has a new kind, a new size
;     and a new lane, so old and new share no pixels and there is no union to
;     take: the old box is erased with one fill and the new one blitted. Two
;     calls, once per crossing.
; =============================================================================

SV_NFISH    equ 4
SV_NBUB     equ 4
SV_FIVAL    equ 1               ; ticks a frame - ONE now, where the six-call
                                ; version needed two. The whole saving went
                                ; into the frame rate
SV_FWU      equ 16              ; the bitmaps are 16 x 8, and a scale of 1 or
SV_FHU      equ 8               ; 2 makes them 16x8 or 32x16 on the glass
SV_FMAXD    equ 4               ; the most a swimmer moves in either axis in
                                ; one frame, which is what the union buffer
                                ; has to have room for
SV_FK_FISH  equ 0
SV_FK_JELLY equ 1
SV_FBOB     equ 4               ; the bob's amplitude, in pixels

; The BAND (SPEC.md 5.4.2): (32 + 4) x (16 + 4) pixels at EIGHT per byte, out
; to the byte grid at both ends - a block whose left edge is seven pixels into
; its first byte needs one more byte than its width alone asks for.
SV_BNDW     equ SV_FWU * 2 + SV_FMAXD       ; 36 px of block...
SV_BNDH     equ SV_FHU * 2 + SV_FMAXD       ; ...20 rows of it
SV_BNST     equ (SV_BNDW + 7 + 7) / 8       ; 6 bytes a row
SV_BNSZ     equ SV_BNST * SV_BNDH           ; 120 - a THIRD of the 4bpp block

; THE RESERVED STRIP AT THE RIGHT EDGE, HERCULES ONLY (SPEC.md 79.5.10). One
; BYTE column, so the band's cut is a width and never a bit mask: the band's
; x and width are multiples of 8 and so is this, which is the same property
; gfx_blit1 is built on. Eight is sized from the artifact's own brightness and
; not from the strip that hides it - see 79.5.10 - and 16 is the next step up
; if a trace survives, at a cost nobody can see either way.
SV_HEDGE    equ 8

; -----------------------------------------------------------------------------
; sv_fish_init - a fresh sea
; in:  the gfx lock is HELD and the screen has just been cleared
; -----------------------------------------------------------------------------
sv_fish_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov byte [sv_ival], SV_FIVAL

    mov word [sv_hlim], 0       ; SPEC.md 79.5.10: no reserve unless this is a
%ifndef NOHEDGE                 ; HERCULES, which is 1bpp AND 720 across -
    cmp byte [sv_monoff], 0     ; nothing else in SPEC.md 39 is that wide, and
    je .nohedge                 ; the width is the quantity the strip is cut
    mov ax, [sv_w]              ; from anyway. NOHEDGE=1 leaves the word 0 and
    cmp ax, 720                 ; every test of it below then costs one compare
    jne .nohedge                ; and goes the way every other adapter goes
    sub ax, SV_HEDGE
    mov [sv_hlim], ax
.nohedge:
%endif

    xor si, si
.sw:
    mov byte [sv_fon + si], 0   ; nothing on the glass from a past session
    call sv_fish_born
    mov cx, [sv_w]              ; ...and the first lot start ON the screen
    call sv_rndn                ; rather than all queued at one edge
    and al, 0FEh                ; EVEN, which the band no longer needs and the
                                ; opening screen still wants: the first four
                                ; are the one place a swimmer's x is not
                                ; derived from an even birth and an even
                                ; speed, and an odd start would be permanent
                                ; because every speed is even. It costs one
                                ; instruction and it keeps every frame of a
                                ; session on the same column grid
    mov di, si
    add di, di
    mov [sv_fx + di], ax
    inc si
    cmp si, SV_NFISH
    jb .sw

    xor si, si
.bu:
    mov byte [sv_bon + si], 0
    call sv_bub_born
    mov cx, [sv_h]
    call sv_rndn
    mov di, si
    add di, di
    mov [sv_by + di], ax
    inc si
    cmp si, SV_NBUB
    jb .bu

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sv_fish_born - swimmer SI gets a new kind, size, lane and heading
; in:  SI = the swimmer; out: nothing kept; clobbers AX, BX, CX, DX, DI
;
; IT IS ROLLED WHOLE, not nudged: kind, scale, ink, speed and direction all
; change together, so the sea after ten minutes is not the sea it started as
; with everything in the same place. The x it is given is off the edge it is
; coming from - sv_fish_init overwrites that for the opening screen, which is
; the one time a swimmer should already be in view.
;
; THE SPEED IS 2 OR 4 AND NEVER 3, which is the nibble-alignment rule in the
; header: an odd x offset into a 4bpp buffer is half a byte.
; -----------------------------------------------------------------------------
sv_fish_born:
    push si
    mov cx, 2                   ; a fish or a jellyfish
    call sv_rndn
    mov di, si
    mov [sv_fk + di], al
    mov bl, al

    mov cx, 2                   ; ...at one of two sizes
    call sv_rndn
    inc al
    mov di, si
    mov [sv_fs + di], al
    mov bh, al                  ; BH = the scale, for the lane below

    mov al, SV_MAIN             ; a fish is the warm ink and a jellyfish the
    cmp bl, SV_FK_JELLY         ; cool one, which on a 1bpp adapter is solid
    jne .ink                    ; against dithered - the one distinction
    mov al, SV_DIM              ; SPEC.md 39.4 leaves us
.ink:
    call sv_ink                 ; ...and the COLOUR, banked: the sprite builder
    mov di, si                  ; paints it into the block and never sets a pen
    mov [sv_fi + di], al

    mov al, bh                  ; the lane: anywhere a whole swimmer fits
    mov ah, 0
    mov bl, SV_FHU
    mul bl                      ; AX = the drawn height
    add ax, SV_FBOB * 2
    mov bx, ax
    mov cx, [sv_h]
    sub cx, bx
    call sv_rndn
    add ax, SV_FBOB
    mov di, si
    add di, di
    mov [sv_fy + di], ax

    mov cx, 2                   ; ...and how fast: 2 or 4, never odd
    call sv_rndn
    add ax, ax
    add ax, 2
    mov dx, ax
    call OSAPI_RAND             ; ...in one of two directions. THE SIGN BIT and
    or ax, ax                   ; not bit 0: bit 0 of the kernel's LCG is a
    jns .heading                ; counter, and reading it here put thirty-odd
    neg dx                      ; consecutive swimmers on the same heading -
.heading:                       ; which is what "they only come from one side"
                                ; was. Measured in sv_rndn's header
    mov di, si
    add di, di
    mov [sv_fvx + di], dx
    call sv_fish_edge           ; ...starting off the edge it swims from

    call OSAPI_RAND
    mov [sv_fph + si], ah        ; AH: the tail phase is the top three bits of
    pop si                       ; this byte, and AL's are the low half of an
    ret                          ; LCG word (sv_rndn's header)

; sv_fish_edge - put swimmer SI just off the edge its heading comes from
; in:  SI; clobbers AX, BX, DI, flags
sv_fish_edge:
    mov di, si
    add di, di
    mov al, [sv_fs + si]
    mov ah, 0
    mov bl, SV_FWU
    mul bl                      ; AX = the drawn width
    mov bx, ax
    cmp word [sv_fvx + di], 0
    jl .fromright
    neg bx                      ; heading right: born off the LEFT edge
    mov [sv_fx + di], bx
    ret
.fromright:
    mov ax, [sv_w]
    mov [sv_fx + di], ax
    ret

; -----------------------------------------------------------------------------
; sv_fish_step - one frame
; in:  the gfx lock is HELD
; -----------------------------------------------------------------------------
sv_fish_step:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    xor si, si
.sw:
    call sv_fish_one
    inc si
    cmp si, SV_NFISH
    jb .sw

    xor si, si
.bu:
    call sv_bub_one
    inc si
    cmp si, SV_NBUB
    jb .bu

    mov byte [sv_shown], 1
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sv_fish_one - move and redraw ONE swimmer (module internal)
; in:  SI = the swimmer; the gfx lock is HELD
; out: nothing; clobbers AX, BX, CX, DX, DI
; -----------------------------------------------------------------------------
sv_fish_one:
    mov di, si
    add di, di
    mov ax, [sv_fx + di]        ; swim...
    add ax, [sv_fvx + di]
    mov [sv_fx + di], ax
    mov al, [sv_fph + si]       ; ...and bob. The step is coprime with 256, so
    add al, 11                  ; the bob does not lock to the frame rate
    mov [sv_fph + si], al

    call sv_fish_gone
    jnc .place
    call sv_fish_off            ; a crossing ends: the old box goes, because a
    call sv_fish_born           ; reborn swimmer shares no pixels with it
.place:
    call sv_fish_where          ; [sv_fdx]/[sv_fdy] = where it is NOW
    call sv_fish_blit
    ret

; sv_fish_gone - has swimmer SI left the screen entirely?
; in: SI; out: CF = 1 yes; clobbers AX, BX, DI, flags
sv_fish_gone:
    mov di, si
    add di, di
    mov al, [sv_fs + si]
    mov ah, 0
    mov bl, SV_FWU
    mul bl
    mov bx, ax                  ; BX = the drawn width
    mov ax, [sv_fx + di]
    cmp word [sv_fvx + di], 0
    jl .left
    cmp ax, [sv_w]              ; heading right: gone when its LEFT edge is
    jl .here                    ; past the last column
    stc
    ret
.left:
    add ax, bx                  ; heading left: gone when its RIGHT edge is
    or ax, ax                   ; past column 0
    jg .here
    stc
    ret
.here:
    clc
    ret

; -----------------------------------------------------------------------------
; sv_fish_where - swimmer SI's drawn top-left, into the scratch
; in:  SI; out: [sv_fdx], [sv_fdy]; clobbers AX, BX, DI, flags
; -----------------------------------------------------------------------------
sv_fish_where:
    mov di, si
    add di, di
    mov ax, [sv_fx + di]
    and al, 0FEh                ; EVEN, HERE, WHATEVER THE CALLER STORED - and
                                ; since SPEC.md 79.5.8 this is a CONVENTION
                                ; rather than a correctness rule. The 4bpp
                                ; block packed two pixels to a byte, so a left
                                ; cut of an odd number of them was half a byte
                                ; and lost the trailing column that does the
                                ; erasing; a band is one bit a pixel and
                                ; sv_bnd_shift takes any offset at all. It is
                                ; kept because the union offset, the block
                                ; origin and the box banked for the next frame
                                ; are all derived from this one word, and a
                                ; sea on one column grid is the sea that was
                                ; measured. It rounds DOWN, which is what keeps
                                ; a negative x rounding the same way as a
                                ; positive one
    mov [sv_fdx], ax
    mov al, [sv_fph + si]       ; the bob: +-SV_FBOB about its lane
    call sv_sin
    mov bl, al
    mov al, SV_FBOB
    call sv_mulsh
    cbw
    add ax, [sv_fy + di]
    mov [sv_fdy], ax
    ret

; -----------------------------------------------------------------------------
; sv_fish_off - the box swimmer SI last occupied, to the ground ink
; in:  SI; out: nothing; clobbers AX, BX, CX, DX, DI
;
; ONLY A RESPAWN REACHES THIS. An ordinary frame's erase is the union blit,
; which is the whole point of this mode's second draft.
; -----------------------------------------------------------------------------
sv_fish_off:
    cmp byte [sv_fon + si], 0
    je .out
    mov al, SV_GROUND
    call sv_pen
    mov di, si
    add di, di
    mov ax, [sv_fox + di]
    mov bx, [sv_foy + di]
    push ax
    push bx
    mov al, [sv_fos + si]
    mov ah, 0
    mov bl, SV_FWU
    mul bl
    mov cx, ax
    mov al, [sv_fos + si]
    mov ah, 0
    mov bl, SV_FHU
    mul bl
    mov dx, ax
    pop bx
    pop ax
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call OSAPI_GFX_FILL
    mov byte [sv_fon + si], 0
.out:
    ret

; -----------------------------------------------------------------------------
; sv_fish_blit - build the union block and put it down in ONE call
; in:  SI = the swimmer, [sv_fdx]/[sv_fdy] = where it is now; lock held
; out: nothing; clobbers AX, BX, CX, DX, DI
;
; The block's origin is the corner nearest the origin of the two boxes, its
; size is the sprite plus however far it moved, and the sprite is painted into
; it at the offset that puts it at the NEW position. Everything else in the
; block is ground - which is what erases the trail.
;
; The union is abandoned and the old box erased separately when the two do not
; overlap or the scale changed (see sv_fish_off): that is a respawn, and this
; buffer is sized for a MOVE.
; -----------------------------------------------------------------------------
sv_fish_blit:
    call sv_fish_size           ; [sv_fbw]/[sv_fbh] = the sprite on the glass
    mov word [sv_fbox], 0       ; ...and assume no trail: the block IS it
    mov word [sv_fboy], 0
    cmp byte [sv_fon + si], 0
    je .build
    mov di, si
    add di, di
    mov al, [sv_fos + si]
    cmp al, [sv_fs + si]
    jne .fresh                  ; the scale changed: not a move

    mov ax, [sv_fdx]            ; --- the union, one axis at a time
    mov bx, [sv_fox + di]
    call sv_fun
    jc .fresh
    mov [sv_fbox], dx
    add [sv_fbw], cx
    mov ax, [sv_fdy]
    mov bx, [sv_foy + di]
    call sv_fun
    jc .fresh
    mov [sv_fboy], dx
    add [sv_fbh], cx
    jmp short .build
.fresh:                         ; not a move at all: the old box goes on its
    call sv_fish_off            ; own and the block is the sprite alone
    call sv_fish_size
    mov word [sv_fbox], 0
    mov word [sv_fboy], 0
.build:
    mov ax, [sv_fdx]            ; the block's origin: the lower of the two
    sub ax, [sv_fbox]           ; corners in each axis. BEFORE the band is
    mov [sv_fbx0], ax           ; built and not after, because the band is cut
    mov ax, [sv_fdy]            ; to the BYTE GRID under this x and the sprite
    sub ax, [sv_fboy]           ; is shifted into it by the remainder
    mov [sv_fby0], ax

    call sv_bnd_build

    mov ax, [sv_hlim]           ; SPEC.md 79.5.10's strip, and NOTHING on a
    or ax, ax                   ; screen that has none: CGA and VGA hand the
    jz .edgeok                  ; whole band over and the kernel's own right
    mov bx, [sv_bnx0]           ; clip is still the only one that cuts it
    cmp bx, ax
    jge .gone                   ; wholly inside the strip - and nothing was
                                ; ever drawn there to erase, because this cut
                                ; has applied to every frame before it
    mov cx, bx
    add cx, [sv_bnw]
    cmp cx, ax
    jle .edgeok
    sub ax, bx                  ; both are multiples of 8, so the width the
    mov [sv_bnw], ax            ; blit meets still is (SPEC.md 5.4.2)
.edgeok:

    mov al, [sv_fi + si]        ; the swimmer's colour on ink, black on paper.
    mov ah, CBLACK              ; CBLACK is 0 and 0 is a subset of every
    call OSAPI_GFX_BLIT1_PEN    ; colour, so the pair is never the one 5.4.2.2
                                ; refuses. NOT READ on a 1bpp adapter, where
                                ; sv_bnd_build's dither carries the fish /
                                ; jellyfish distinction instead

    push si                     ; SI IS THE SWIMMER EVERYWHERE ELSE IN THIS
    push es                     ; FILE and is the SOURCE for exactly one call
    push ds
    pop es                      ; ES:SI = OUR band (see the file header)
    mov si, sv_bnd              ; ...and the whole of it: the kernel's own
                                ; right clip is what cuts it (SPEC.md 79.5.9)
    mov ax, [sv_bnx0]
    mov bx, [sv_fby0]
    mov cx, [sv_bnw]
    mov dx, [sv_fbh]
    mov bp, SV_BNST
    call OSAPI_GFX_BLIT1        ; ...which CLIPS, in both axes and exactly:
    pop es                      ; every extent it meets is a multiple of 8 and
    pop si                      ; so is the band. `pop` touches no flag, so the
    jc .norun                   ; CF it answers in survives both of them -
                                ; SPEC.md 20.3's API cell is built on the same
                                ; property, and a clipped-away band is CF = 0
                                ; ("drawn, and nothing drawn"), so this tests
                                ; for a kernel and not for a screen edge
.gone:

    mov di, si                  ; the box now on the glass, for the next frame.
    add di, di                  ; DI is RE-DERIVED: sv_bnd_build spends it
    mov ax, [sv_fdx]
    mov [sv_fox + di], ax
    mov ax, [sv_fdy]
    mov [sv_foy + di], ax
    mov al, [sv_fs + si]
    mov [sv_fos + si], al
    mov byte [sv_fon + si], 1
    ret
.norun:                         ; A KERNEL WITH NO gfx_blit1 - kern_small, whose
    call sv_fish_off            ; slot is stc/retf (SPEC.md 5.4.2). It cannot be
    ret                         ; this one: the Makefile's SMALLDRIVERS filters
                                ; saver.drv out of both small floppies, exactly
                                ; as it filters xmem.drv, so no kernel that
                                ; refuses this call has the file to load. The
                                ; guard is here because "unreachable" and
                                ; "leaves the last frame standing for the rest
                                ; of the session" are a bad pair to be wrong
                                ; about: the old box goes and the sea is empty
                                ; rather than littered

; -----------------------------------------------------------------------------
; sv_bnd_build - the union BAND: ground everywhere but the swimmer
; in:  SI = the swimmer; [sv_fbw]/[sv_fbh] = the block, [sv_fbox]/[sv_fboy] =
;      where the sprite sits inside it, [sv_fbx0]/[sv_fby0] = the block on the
;      glass
; out: sv_bnd filled, [sv_bnx0] = the band's x (a multiple of 8), [sv_bnw] =
;      its width in pixels (ditto); clobbers AX, BX, CX, DX, DI
;
; ONE BIT A PIXEL, high bit leftmost, 1 = the swimmer - which is the
; framebuffer's own way up (SPEC.md 5.4.2), so the DEFAULT pen already draws
; it right and nothing here is complemented. The ground is 0 and is laid by a
; `rep stosb` of SV_BNSZ, which is 120 bytes against the 4bpp block's 360.
;
; WHY THE BAND, AND WHY IT IS THE WHOLE OF THIS MODE'S FRAME RATE. Measured on
; MartyPC, a cycle-accurate 4.77MHz 8088 with a CGA: `gfx_blit4` charged
; **2,863 cycles a ROW** for a 36-pixel row and the width barely entered it -
; a row of it is `sw_blit_row`'s pair walk plus a `gfx_rowbase` multiply and
; nine pushes - so a scale-2 swimmer's 18 rows were **10.2 ms** and four of
; them were 41 of a 73 ms frame. `gfx_blit1` is a `rep movsw` a row over a
; source that is already 1bpp: five bytes, not eighteen, and no per-pixel
; decode at either end. SPEC.md 79.5.8 has the table.
;
; THE THREE THINGS THAT MAKE IT WORK -----------------------------------------
;
;   * **A BAND STARTS ON THE BYTE GRID AND THE SWIMMER DOES NOT.** gfx_blit1
;     refuses an x or a width off it, and a swimmer moves 2 or 4 pixels a
;     frame. So the band is the block rounded DOWN to the grid and widened to
;     it, and the sprite is SHIFTED into it by the remainder - which is why
;     SV_BNST carries `+ 7 + 7` and not `+ 0`.
;
;   * **THE SHIFT IS A BYTE PAIR, NOT A 32-BIT ROTATE.** `sv_bnd_shift` walks
;     the row a byte at a time carrying the bits that fell off the last one,
;     so a shift of 0 costs the same as a shift of 7 and neither needs a
;     special case: an 8086 `shr r8, cl` with CL = 0 does nothing and `shl
;     r8, cl` with CL = 8 clears it, both of which are the answers this wants.
;
;   * **A JELLYFISH IS DITHERED AND A BAND HAS NO GREY.** SPEC.md 5.4.2.2: on
;     a 1bpp adapter the pen is NOT READ - a set bit is lit and nothing a
;     caller does changes that - so the mono column's CLGRAY cannot arrive
;     through it. The dither is composed here instead, and it has to be the
;     kernel's own: `sw_parity` lays 0xAA on an even screen row and 0x55 on an
;     odd one, bit 7 being the leftmost pixel, so a pixel is lit when (x + y)
;     is even (SPEC.md 39.4). A band starts on the byte grid, so its rows take
;     that mask unshifted.
; -----------------------------------------------------------------------------
sv_bnd_build:
    push si
    push es
    push ds
    pop es                      ; `stosb` and `movsb` have no other option, and
                                ; every write below is into our own image

    ; --- WHICH BITMAP: kind, heading, then the animation frame -------------
    ; Twelve of them, in that order: fish right, fish left, jelly right, jelly
    ; left, three frames each. A bitmap cannot be mirrored for free the way
    ; the first draft's unit coordinates could, so the heading is an INDEX
    ; here rather than a subtraction at every use.
    mov al, [sv_fk + si]
    mov ah, 0
    mov bl, SV_FNFR * 2         ; six a kind: two headings of three
    mul bl
    mov di, ax
    mov bx, si
    add bx, bx
    cmp word [sv_fvx + bx], 0
    jge .right
    add di, SV_FNFR
.right:
    mov bl, [sv_fph + si]       ; the phase's top three bits...
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    and bl, 7
    mov bh, 0
    mov bl, [bx + sv_fseq]      ; ...through a sequence, so the wag goes out
    mov bh, 0                   ; and BACK rather than snapping round
    add di, bx
    mov ax, di
    mov cl, 4                   ; sixteen bytes a bitmap
    shl ax, cl
    add ax, sv_fart
    mov [sv_fsrc], ax

    ; --- the band: the block, out to the byte grid at BOTH ends ------------
    mov ax, [sv_fbx0]
    mov bx, ax
    and bx, 7                   ; BX = how far into its first byte the block
    and ax, 0FFF8h              ; starts. The AND rounds DOWN, and it rounds a
    mov [sv_bnx0], ax           ; NEGATIVE x down too, which is the case a
                                ; swimmer entering from the left is in
    mov ax, bx
    add ax, [sv_fbw]
    add ax, 7
    and ax, 0FFF8h              ; ...and the width out to the grid at the far
    mov [sv_bnw], ax            ; end: at most 7 + 36 + 7, which is SV_BNST

    add bx, [sv_fbox]           ; BX = the SPRITE's offset into the band, in
    mov ax, bx                  ; bits - the block's own shift plus where the
    mov cl, 3                   ; sprite sits inside the block
    shr ax, cl
    mov [sv_bnbo], ax           ; ...as whole bytes...
    and bl, 7
    mov [sv_bnsh], bl           ; ...and the shift left over

    mov al, SV_BNST             ; --- and the whole of it to ground: 120 bytes
    mov ah, 0                   ; where the 4bpp block was 360
    mov bl, [sv_fbh]            ; (a block is never taller than SV_BNDH = 20)
    mul bl
    mov cx, ax
    mov di, sv_bnd
    xor al, al
    cld
    rep stosb

    ; --- the dither, which is the MONO jellyfish's and nothing else's ------
    mov byte [sv_bndx], 0       ; a solid swimmer: the mask never moves...
    mov byte [sv_bndm], 0FFh    ; ...and passes every bit
    cmp byte [sv_monoff], 0
    je .msk                     ; a colour adapter: the pen carries it
    cmp byte [sv_fk + si], SV_FK_JELLY
    jne .msk
    mov byte [sv_bndx], 0FFh    ; ...and 0xAA and 0x55 are each other's
    mov al, 0AAh                ; complement, so one XOR steps the rows
    mov bx, [sv_fby0]
    add bx, [sv_fboy]           ; sw_parity's byte for the sprite's FIRST
    test bl, 1                  ; screen row (SPEC.md 39.4)
    jz .mset
    mov al, 55h
.mset:
    mov [sv_bndm], al
.msk:

    mov al, 2                   ; the bytes a bitmap row becomes BEFORE the
    mov ah, [sv_fs + si]        ; shift: two, or four once doubled
    cmp ah, 2
    jne .n1
    mov al, 4
.n1:
    mov [sv_bnn], al
    mov [sv_bnsc], ah           ; ...and the band rows one bitmap row fills

    mov ax, [sv_fboy]           ; DI = the first band row's sprite byte
    mov bl, SV_BNST
    mul bl
    add ax, [sv_bnbo]
    add ax, sv_bnd
    mov di, ax
    mov si, [sv_fsrc]
    mov byte [sv_bnrc], SV_FHU  ; eight bitmap rows, whatever the scale
.row:
    push di
    cmp byte [sv_bnsc], 2
    je .two
    mov di, sv_bnexp            ; scale 1: the row IS its two bitmap bytes
    movsb
    movsb
    jmp short .shift
.two:
    mov di, sv_bnexp            ; scale 2: each bitmap byte doubles into two
    mov cx, 2
.dbl:
    lodsb
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov ax, [bx + sv_dbl]
    mov [di], ah                ; the leftmost eight pixels first, which is the
    mov [di + 1], al            ; order a band reads in
    add di, 2
    loop .dbl
.shift:
    push si                     ; the bitmap cursor, over the shift's own walk
    mov si, sv_bnexp            ; ...and the row is shifted ONCE, however many
    mov di, sv_bnsft            ; band rows it is about to be stored into
    mov cl, [sv_bnsh]
    mov ch, 8
    sub ch, cl
    mov bh, [sv_bnn]
    call sv_bnd_shift
    pop si
    pop di

    mov dh, [sv_bnsc]           ; ...into one band row a scale, each under the
.emit:                          ; mask its own screen parity asks for
    push di
    mov bx, sv_bnsft
    mov cl, [sv_bnn]
    inc cl                      ; the shift makes one byte more than it took
    mov dl, [sv_bndm]
.byte:
    mov al, [bx]
    and al, dl
    mov [di], al
    inc bx
    inc di
    dec cl
    jnz .byte
    pop di
    add di, SV_BNST
    mov al, [sv_bndx]           ; the row below is the other parity - or, for
    xor [sv_bndm], al           ; a solid swimmer, the same 0xFF
    dec dh
    jnz .emit

    dec byte [sv_bnrc]
    jnz .row

    pop es
    pop si
    ret


; -----------------------------------------------------------------------------
; sv_bnd_shift - BH source bytes at DS:SI, shifted right CL bits, into BH + 1
;                bytes at ES:DI (module internal)
; in:  CL = the shift 0..7, CH = 8 - CL, BH = source bytes; ES = DS
; out: nothing; clobbers AX, DL, BH, SI, DI, flags
;
; The bits that fall off a byte are the top bits of the next one, so the whole
; shift is a byte walk with one carry - no 32-bit rotate, no case for 0 and
; none for 7. An 8086 uses CL whole (the 5-bit mask arrived with the 186), so
; `shl ah, cl` with CL = 8 really is zero, which is what the CL = 0 case needs.
; -----------------------------------------------------------------------------
sv_bnd_shift:
    xor dl, dl                  ; what fell off the byte before this one
.b:
    lodsb
    mov ah, al
    shr al, cl
    or al, dl
    stosb
    xchg cl, ch
    shl ah, cl
    xchg cl, ch
    mov dl, ah
    dec bh
    jnz .b
    mov [di], dl                ; ...and the tail, which is why the band is one
    ret                         ; byte wider than the block


; sv_fish_size - swimmer SI's drawn size into [sv_fbw]/[sv_fbh]
; in:  SI; clobbers AX, BX, flags
sv_fish_size:
    mov al, [sv_fs + si]
    mov ah, 0
    mov bl, SV_FWU
    mul bl
    mov [sv_fbw], ax
    mov al, [sv_fs + si]
    mov ah, 0
    mov bl, SV_FHU
    mul bl
    mov [sv_fbh], ax
    ret

; -----------------------------------------------------------------------------
; sv_fun - one axis of the union block (module internal)
; in:  AX = where the sprite is NOW, BX = where it was
; out: CX = |delta|, DX = the sprite's offset inside the block (0 or |delta|),
;      CF = 1 = further than the buffer can hold, so there is no union
; clobbers: AX, CX, DX, flags
;
; **THE SIGN IS SPENT BEFORE THE BOUND IS TESTED**, and that ordering is the
; whole routine: written as `call abs / cmp / ja / jnc`, the `cmp` writes the
; carry the `jnc` was reading, so a swimmer heading left put its sprite at the
; wrong end of its own block - visible as the fish jumping backwards by its
; own speed every frame while the trail was erased in front of it.
; -----------------------------------------------------------------------------
sv_fun:
    sub ax, bx
    jns .fwd
    neg ax
    mov cx, ax
    xor dx, dx                  ; moved back: the sprite is at the block's own
    jmp short .ck               ; origin and the trail is to its right
.fwd:
    mov cx, ax
    mov dx, ax                  ; moved on: it sits |delta| into the block
.ck:
    cmp cx, SV_FMAXD
    ja .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; sv_bub_born / sv_bub_one - the bubbles (module internal)
; in:  SI = the bubble
;
; TWO CALLS EACH, and the NEW one is drawn FIRST. A bubble rises at least two
; pixels a frame and is two tall, so the old block and the new one never share
; a row - which is what makes draw-then-erase safe here where it is not safe
; for a swimmer, and it is why the bubbles never blinked even in the first
; draft's worst frame.
; -----------------------------------------------------------------------------
sv_bub_born:
    push si
    mov cx, [sv_w]
    call sv_rndn
    mov di, si
    add di, di
    mov [sv_bx + di], ax        ; DI is the WORD index and SI the byte one -
                                ; every array here is one or the other and
                                ; never both
    mov ax, [sv_h]
    dec ax
    mov [sv_by + di], ax
    mov cx, 3
    call sv_rndn
    add ax, 2
    mov di, si
    add di, di
    mov [sv_bv + di], ax
    call OSAPI_RAND
    mov [sv_bph + si], ah       ; AH, for sv_fish_born's reason
    pop si
    ret

sv_bub_one:
    mov di, si
    add di, di
    mov ax, [sv_bx + di]        ; bank where it IS, so the erase below can go
    mov [sv_bex], ax            ; there after the new one is on the glass
    mov ax, [sv_by + di]
    mov [sv_bey], ax
    mov al, [sv_bph + si]
    mov [sv_beph], al

    mov ax, [sv_by + di]
    sub ax, [sv_bv + di]
    mov [sv_by + di], ax
    or ax, ax
    jg .wob
    call sv_bub_born
    jmp short .draw
.wob:
    mov al, [sv_bph + si]       ; the wobble is a re-derived OFFSET and never
    add al, 9                   ; an accumulated one: a bubble whose x drifted
    mov [sv_bph + si], al       ; would eventually leave the screen sideways
.draw:
    mov al, SV_HOT
    call sv_pen
    mov di, si
    add di, di
    mov ax, [sv_bx + di]
    mov bx, [sv_by + di]
    mov cl, [sv_bph + si]
    call sv_bub_blot
    cmp byte [sv_bon + si], 0
    je .up
    mov al, SV_GROUND           ; ...and only NOW does the old one go
    call sv_pen
    mov ax, [sv_bex]
    mov bx, [sv_bey]
    mov cl, [sv_beph]
    call sv_bub_blot
.up:
    mov byte [sv_bon + si], 1
    ret

; sv_bub_blot - the 2x2 at (AX, BX) swayed by phase CL, in the current pen
; in:  AX = x, BX = y, CL = the phase; clobbers AX, BX, CX, DX
sv_bub_blot:
    push si
    push bx
    push ax
    mov al, cl
    call sv_sin
    mov bl, al
    mov al, 3
    call sv_mulsh               ; +-3 pixels of sway
    cbw
    mov si, ax
    pop ax
    add ax, si
    pop bx
    mov cx, ax
    inc cx
    mov dx, bx
    inc dx
    cmp word [sv_hlim], 0       ; SPEC.md 79.5.10 again, and a bubble needs no
    je .fill                    ; band arithmetic: a fill takes any x, so the
    push ax                     ; blot is simply cut at the strip
    mov ax, [sv_hlim]
    dec ax                      ; AX = the last column a bubble may light
    cmp cx, ax
    jbe .xok
    mov cx, ax
.xok:
    pop ax
    cmp ax, cx
    ja .none                    ; ...and the whole blot is inside the strip.
                                ; The ERASE takes this same road with the same
                                ; numbers, so a bubble that is not drawn is not
                                ; left behind either
.fill:
    call OSAPI_GFX_FILL
.none:
    pop si
    ret

; =============================================================================
; The bitmaps. 16 x 8, one bit a pixel, bit 7 of the first byte leftmost -
; which is the order a byte reads in on the page, so the picture beside each
; row IS the row.
;
; A FISH FACES RIGHT AND IS MIRRORED BY... NOTHING. The first draft mirrored
; its unit coordinates for a swimmer heading left; a bitmap cannot be mirrored
; for free, so there are two sets - and 16 bytes a frame is what that costs,
; on the floppy, where this image lives.
; =============================================================================
SV_FNFR     equ 3               ; bitmaps per kind

; sv_dbl - a bitmap byte -> the same eight pixels DOUBLED, sixteen bits, high
; bit leftmost. 512 bytes of image and no run-time build, which is what makes
; a scale-2 band row two table reads where the 4bpp block spent a shift, a
; mask and a store per PIXEL.
sv_dbl:
%assign SVDI 0
%rep 256
  %assign SVDV 0
  %assign SVDB 0
  %rep 8
    %if (SVDI & (1 << SVDB))
      %assign SVDV SVDV | (3 << (SVDB * 2))
    %endif
    %assign SVDB SVDB + 1
  %endrep
    dw SVDV
  %assign SVDI SVDI + 1
%endrep

sv_fseq:    db 0, 1, 2, 1, 0, 1, 2, 1  ; the phase's top bits -> a frame, out
                                       ; and BACK: a tail that snapped from 2
                                       ; to 0 read as a twitch

sv_fart:
; --- fish, tail FLARED -------------------------------------------------------
    db 10000000b, 00000000b     ; #...............
    db 11000001b, 11111100b     ; ##.....#######..
    db 01110011b, 11111010b     ; .###..#######.#.   <- the eye is the gap
    db 00111111b, 11111111b     ; ..##############
    db 00111111b, 11111111b     ; ..##############
    db 01110011b, 11111110b     ; .###..##########
    db 11000001b, 11111100b     ; ##.....#######..
    db 10000000b, 00000000b     ; #...............
; --- fish, tail HALF ---------------------------------------------------------
    db 00000000b, 00000000b     ; ................
    db 01000001b, 11111100b     ; .#.....#######..
    db 01110011b, 11111010b     ; .###..#######.#.
    db 00111111b, 11111111b     ; ..##############
    db 00111111b, 11111111b     ; ..##############
    db 01110011b, 11111110b     ; .###..##########
    db 01000001b, 11111100b     ; .#.....#######..
    db 00000000b, 00000000b     ; ................
; --- fish, tail CLOSED -------------------------------------------------------
    db 00000000b, 00000000b     ; ................
    db 00000001b, 11111100b     ; .......#######..
    db 00110011b, 11111010b     ; ..##..#######.#.
    db 00111111b, 11111111b     ; ..##############
    db 00111111b, 11111111b     ; ..##############
    db 00110011b, 11111110b     ; ..##..##########
    db 00000001b, 11111100b     ; .......#######..
    db 00000000b, 00000000b     ; ................
; --- fish facing LEFT, the same three, mirrored ------------------------------
    db 00000000b, 00000001b     ; ...............#
    db 00111111b, 10000011b     ; ..#######......#
    db 01011111b, 11001110b     ; .#.#######..###.
    db 11111111b, 11111100b     ; ##############..
    db 11111111b, 11111100b     ; ##############..
    db 01111111b, 11001110b     ; .##########.###.
    db 00111111b, 10000011b     ; ..#######......#
    db 00000000b, 00000001b     ; ...............#
    db 00000000b, 00000000b
    db 00111111b, 10000010b     ; ..#######......#
    db 01011111b, 11001110b
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 01111111b, 11001110b
    db 00111111b, 10000010b
    db 00000000b, 00000000b
    db 00000000b, 00000000b
    db 00111111b, 10000000b     ; ..#######.......
    db 01011111b, 11001100b     ; .#.#######..##..
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 01111111b, 11001100b
    db 00111111b, 10000000b
    db 00000000b, 00000000b
; --- jellyfish, bell RELAXED -------------------------------------------------
    db 00000111b, 11100000b     ; .....######.....
    db 00011111b, 11111000b     ; ...##########...
    db 00011111b, 11111000b     ; ...##########...
    db 00001111b, 11110000b     ; ....########....
    db 00001001b, 00100000b     ; ....#..#..#.....
    db 00001001b, 00100000b     ; ....#..#..#.....
    db 00010001b, 00010000b     ; ...#...#...#....
    db 00010000b, 10010000b     ; ...#....#..#....
; --- jellyfish, bell HALF ----------------------------------------------------
    db 00000011b, 11000000b     ; ......####......
    db 00001111b, 11110000b     ; ....########....
    db 00011111b, 11111000b     ; ...##########...
    db 00011111b, 11111000b     ; ...##########...
    db 00001001b, 00100000b     ; ....#..#..#.....
    db 00000101b, 01000000b     ; .....#.#.#......
    db 00001001b, 00100000b     ; ....#..#..#.....
    db 00000101b, 01000000b     ; .....#.#.#......
; --- jellyfish, bell CONTRACTED ----------------------------------------------
    db 00000011b, 11000000b     ; ......####......
    db 00000111b, 11100000b     ; .....######.....
    db 00011111b, 11111000b     ; ...##########...
    db 00011111b, 11111000b     ; ...##########...
    db 00010001b, 00010000b     ; ...#...#...#....
    db 00001001b, 00100000b     ; ....#..#..#.....
    db 00010001b, 00010000b     ; ...#...#...#....
    db 00100000b, 10001000b     ; ..#.....#...#...
; --- jellyfish facing left is the same jellyfish -----------------------------
; It has no facing at all, so the LEFT set is the RIGHT set again rather than
; a branch at every use: sv_fk is 0 or 1 and the heading picks the half.
    db 00000111b, 11100000b
    db 00011111b, 11111000b
    db 00011111b, 11111000b
    db 00001111b, 11110000b
    db 00001001b, 00100000b
    db 00001001b, 00100000b
    db 00010001b, 00010000b
    db 00010000b, 10010000b
    db 00000011b, 11000000b
    db 00001111b, 11110000b
    db 00011111b, 11111000b
    db 00011111b, 11111000b
    db 00001001b, 00100000b
    db 00000101b, 01000000b
    db 00001001b, 00100000b
    db 00000101b, 01000000b
    db 00000011b, 11000000b
    db 00000111b, 11100000b
    db 00011111b, 11111000b
    db 00011111b, 11111000b
    db 00010001b, 00010000b
    db 00001001b, 00100000b
    db 00010001b, 00010000b
    db 00100000b, 10001000b

; --- the sea ----------------------------------------------------------------
sv_fx:      times SV_NFISH dw 0     ; where it is, and where it is going
sv_fy:      times SV_NFISH dw 0
sv_fvx:     times SV_NFISH dw 0
sv_fk:      times SV_NFISH db 0     ; fish or jellyfish
sv_fs:      times SV_NFISH db 1     ; the scale, 1 or 2
sv_fph:     times SV_NFISH db 0
sv_fi:      times SV_NFISH db 0     ; its COLOUR, not its logical ink
sv_fox:     times SV_NFISH dw 0     ; ...and the box it last occupied
sv_foy:     times SV_NFISH dw 0
sv_fos:     times SV_NFISH db 1
sv_fon:     times SV_NFISH db 0

sv_bx:      times SV_NBUB dw 0
sv_by:      times SV_NBUB dw 0
sv_bv:      times SV_NBUB dw 0
sv_bph:     times SV_NBUB db 0
sv_bon:     times SV_NBUB db 0

sv_fdx:     dw 0                    ; the drawing scratch: ONE swimmer at a
sv_fdy:     dw 0                    ; time, sv_shape_load's argument one mode
sv_fbw:     dw 0                    ; over. The block, and where the sprite
sv_fbh:     dw 0                    ; sits inside it
sv_fbox:    dw 0
sv_fboy:    dw 0
sv_fbx0:    dw 0                    ; the block's origin on the glass
sv_fby0:    dw 0
sv_fsrc:    dw 0                    ; sv_bnd_build's chosen bitmap
sv_bnx0:    dw 0                    ; the BAND: where it goes and how wide it
sv_bnw:     dw 0                    ; is, both multiples of 8 (SPEC.md 5.4.2)
sv_bnbo:    dw 0                    ; ...and where the sprite sits in it, as
sv_bnsh:    db 0                    ; whole bytes plus a shift of 0..7
sv_bnn:     db 0                    ; bytes a bitmap row becomes before the
sv_bnsc:    db 0                    ; shift, and band rows it then fills
sv_bnrc:    db 0                    ; bitmap rows left
sv_bndm:    db 0                    ; the dither mask this band row takes...
sv_bndx:    db 0                    ; ...and the XOR that steps it to the next
sv_hlim:    dw 0                    ; SPEC.md 79.5.10: one PAST the last column
                                    ; this mode may light, or 0 for "all of it"
sv_bex:     dw 0                    ; a bubble's PREVIOUS blot, banked across
sv_bey:     dw 0                    ; the move so the erase can follow the draw
sv_beph:    db 0
sv_bnexp:   times 4 db 0            ; one bitmap row, doubled but not yet
sv_bnsft:   times 8 db 0            ; shifted - and then shifted
sv_bnd:     times SV_BNSZ db 0      ; the union band itself
