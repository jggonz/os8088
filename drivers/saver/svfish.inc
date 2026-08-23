; =============================================================================
; svfish.inc - MODE 4: fish and sea life (SPEC.md 79.5)
;
; Four swimmers - fish and jellyfish - crossing a black sea with bubbles
; rising through them, each of them ANIMATED: a fish's tail pumps through
; three positions and a jellyfish's bell contracts and relaxes.
;
; --- ONE CALL A SWIMMER, AND NOTHING IS EVER ERASED -------------------------
;
; This mode was written twice and the second version is the interesting one.
;
; The first drew each swimmer out of six or seven `gfx_*` primitives - three
; fills for a tapering body, two lines for the tail, a pixel for the eye - and
; blanked the box it had last occupied before drawing the new one. That is
; erase-then-draw, PERFORMANCE.md rule 2's canonical violation, and it was
; taken deliberately because a fish does not FILL its bounding box: Cyclone's
; draw-first-then-erase-the-remainder order (SPEC.md 67) leaves the old TAIL
; standing wherever it fell inside the new box. The cost was supposed to be a
; small rectangle absent for the length of six drawing calls.
;
; **REPORTED FROM A REAL SCREEN: the fish flicker.** Six calls at SPEC.md
; 5.7's ~756us arrival is 4.5ms with the sprite off the glass, and it is off
; the glass EVERY FRAME - so what the eye gets is not a 4% brightness dip, it
; is a sprite that strobes.
;
; The answer is `OSAPI_GFX_BLIT4`. A swimmer is a 4bpp block in this image's
; own RAM, drawn in ONE call that lays down its background as well as its ink
; - so it is opaque, and drawing it over where it used to be IS the erase.
; Better still, the block handed over is the UNION of the old box and the new
; one, with the sprite painted at the new offset and ground everywhere else,
; so a single blit draws the swimmer AND takes up its trail. Nothing is
; absent at any instant, there is no second call to be interrupted between,
; and the frame went from ~40 `gfx_*` arrivals to 12.
;
; That is also what makes the animation free: the frame is chosen out of the
; swimmer's own phase, and a different 16-byte bitmap costs the same one blit.
;
; --- THE THINGS THAT MAKE THAT WORK, EACH OF WHICH CAN BE GOT WRONG ---------
;
;   * **ES IS OURS FOR THE BLIT AND THE KERNEL'S EVERYWHERE ELSE.** The slot's
;     contract says ES:SI, "ES is yours here - no stub is involved". A package
;     callback runs with ES = KERNEL_SEG (SPEC.md 20.1), so it is set and put
;     back around the one call; forget that and the kernel draws 300 bytes of
;     its own `.text` as a picture.
;
;   * **A SPEED IS EVEN.** The sprite is painted into the union buffer at an x
;     offset of |dx|, and 4bpp packs TWO PIXELS PER BYTE - so an odd offset
;     would put every row half a byte out of phase and need a shift per pixel.
;     Speeds are 2 and 4, and SV_FMAXD is what bounds the buffer.
;
;   * **THE BLIT CLIPS AND THE BUFFER MUST NOT.** gfx_blit4 clips in both axes
;     - `vga_blit_span` cuts x1 < 0 and x2 past the last column, the 1bpp row
;     decoder refuses an overhanging block and falls back to the run scanner,
;     and a row off the bottom is skipped - so a swimmer half off the edge
;     draws its visible half and nothing is special-cased here.
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

; The buffer: (32 + 4) x (16 + 4) pixels at two per byte.
SV_SPRW     equ SV_FWU * 2 + SV_FMAXD       ; 36 px
SV_SPRH     equ SV_FHU * 2 + SV_FMAXD       ; 20 rows
SV_SPRST    equ SV_SPRW / 2                 ; 18 bytes a row
SV_SPRSZ    equ SV_SPRST * SV_SPRH          ; 360

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

    xor si, si
.sw:
    mov byte [sv_fon + si], 0   ; nothing on the glass from a past session
    call sv_fish_born
    mov cx, [sv_w]              ; ...and the first lot start ON the screen
    call sv_rndn                ; rather than all queued at one edge
    and al, 0FEh                ; **EVEN**, and this is not tidiness: it is the
                                ; ONE place a swimmer's x is not derived from
                                ; an even birth and an even speed, so it is the
                                ; one place the file's alignment invariant can
                                ; be broken - and a swimmer that starts on an
                                ; odd column KEEPS that parity for its whole
                                ; first crossing, because every speed is even.
                                ; What it then does is in sv_fish_clip's header
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
    and al, 0FEh                ; **EVEN, HERE, WHATEVER THE CALLER STORED.**
                                ; Everything downstream is derived from this
                                ; word - the union offset, the block origin,
                                ; the left cut and the box banked for the next
                                ; frame - so making it even ONCE makes all of
                                ; them even, and sv_fish_clip's halving is
                                ; exact by construction rather than by a rule
                                ; somebody has to keep. It rounds DOWN, which
                                ; is what keeps a negative x rounding the same
                                ; way as a positive one
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
    call sv_spr_build

    mov ax, [sv_fdx]            ; the block's origin: the lower of the two
    sub ax, [sv_fbox]           ; corners in each axis
    mov [sv_fbx0], ax
    mov ax, [sv_fdy]
    sub ax, [sv_fboy]
    mov [sv_fby0], ax

    call sv_fish_clip           ; ...and CUT IT TO THE SCREEN ourselves, which
    jc .gone                    ; is not tidiness - see sv_fish_clip

    push si                     ; SI IS THE SWIMMER EVERYWHERE ELSE IN THIS
    push es                     ; FILE and is the SOURCE for exactly one call
    push ds
    pop es                      ; ES:SI = OUR block (see the file header)
    mov si, [sv_fsp]
    mov ax, [sv_fbx0]
    mov bx, [sv_fby0]
    mov cx, [sv_fbw]
    mov dx, [sv_fbh]
    mov bp, SV_SPRST
    call OSAPI_GFX_BLIT4
    pop es
    pop si
.gone:

    mov di, si                  ; the box now on the glass, for the next frame.
    add di, di                  ; DI is RE-DERIVED: sv_spr_build spends it
    mov ax, [sv_fdx]
    mov [sv_fox + di], ax
    mov ax, [sv_fdy]
    mov [sv_foy + di], ax
    mov al, [sv_fs + si]
    mov [sv_fos + si], al
    mov byte [sv_fon + si], 1
    ret

; -----------------------------------------------------------------------------
; sv_fish_clip - cut the block to the screen in x (module internal)
; in:  [sv_fbx0]/[sv_fbw] = the block; out: those adjusted and [sv_fsp] = the
;      source pointer to hand over; CF = 1 = nothing of it is on screen
; clobbers: AX, CX, flags
;
; **gfx_blit4 CLIPS PERFECTLY WELL AND THAT IS THE PROBLEM.** On the 1bpp
; adapters it has two paths: a row DECODER that lays a whole row down in one
; pass, and a run scanner that emits each coalesced run as its own gfx_hline.
; The decoder cannot clip in x - a left clip advances the source by a pixel
; count that may be odd, which shifts the nibble phase of every pair after it -
; so a block that overhangs either edge falls back to the scanner, and a
; swimmer's row is five or six runs. Measured on a 4.77MHz 8088: a frame with
; three of the four swimmers at an edge is ~130ms against ~30ms with none, and
; the swimming visibly slows to a crawl whenever the sea is busy at the sides.
;
; So the block is cut HERE, where the odd-pixel problem does not exist: every
; x a swimmer can have is EVEN by construction (a speed is 2 or 4, a birth is
; at -width or at the screen width, and the union offset is one of those), so
; the left cut is an even number of pixels and moving the source on by half of
; it is exact. The fast path then takes every frame, and the swimming went
; from ~7.5 frames a second to ~15.5 measured on the same machine.
;
; **THE EVEN CUT IS AN INVARIANT AND IT WAS ONCE FALSE.** A swimmer's x is
; even everywhere it is written - a birth is at -width or at the screen width,
; both even, and a speed is 2 or 4 - with exactly one exception, which was
; `sv_fish_init` seeding the opening four at a RANDOM column. An odd start is
; permanent, because every speed is even, so two swimmers in four spent their
; whole first crossing half a byte out of phase.
;
; It showed only at the LEFT edge, and only there, because the left cut is the
; only one that moves the source pointer: `shr ax, 1` on an odd cut truncates,
; so the block went down a pixel short AND its last column - the trail, the
; part that does the erasing - was never reached. Reported as "their tails
; appear above or below them after they swim off", which is what an unerased
; trailing column looks like on a swimmer that bobs.
;
; Two things now hold it, and the second is the one that matters:
; `sv_fish_init` rounds down, and `sv_fish_where` makes the drawn x even at
; the single point every other number here is derived from. The first is
; tidiness; the second is why this routine may divide by two at all.
;
; **The obvious third defence was tried and does not work**, so it is not
; here: refusing an odd cut and handing the block over overhanging, for
; `gfx_blit4` to clip on its run scanner. Measured by forcing an odd x on 14
; crossings, it still left up to 103 pixels standing per crossing against a
; clean control - so the slow path is not a drop-in for this and the invariant
; is not optional. Whatever is wrong there was not chased down; nothing in
; this file can reach it now.
;
; The y axis needs none of this: the decoder's own row loop tests the
; destination row UNSIGNED against the screen height, so a row above the top
; or below the bottom is skipped without leaving the fast path.
; -----------------------------------------------------------------------------
sv_fish_clip:
    mov ax, sv_spr
    mov [sv_fsp], ax
    mov ax, [sv_fbx0]
    or ax, ax
    jns .right
    neg ax                      ; AX = how far off the left, and EVEN
    cmp ax, [sv_fbw]
    jae .none                   ; entirely off it
    sub [sv_fbw], ax
    mov word [sv_fbx0], 0
    shr ax, 1                   ; ...two pixels a byte
    add [sv_fsp], ax
.right:
    mov ax, [sv_fbx0]
    add ax, [sv_fbw]
    sub ax, [sv_w]
    jle .ok                     ; the last column is on screen
    cmp ax, [sv_fbw]
    jae .none
    sub [sv_fbw], ax
.ok:
    clc
    ret
.none:
    stc
    ret

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
; sv_spr_build - the union block, ground everywhere but the sprite
; in:  SI = the swimmer; [sv_fbw]/[sv_fbh] = the block, [sv_fbox]/[sv_fboy] =
;      where the sprite sits inside it
; out: sv_spr filled; clobbers AX, BX, CX, DX, DI
;
; TWO PIXELS A BYTE, high nibble leftmost, which is the slot's own layout. The
; ground is CBLACK and CBLACK is 0, so the wipe is a `rep stosb` of zero and
; the paint below only ever has to SET bits - which is why there is no
; per-pixel read anywhere in here.
;
; It is written through ES, so ES is ours for the whole routine: `stosb` has
; no other option (SPEC.md 73's C rule has the same root).
;
; Cost, measured in instructions rather than in `gfx_*` arrivals: ~360 stores
; for the wipe and 16 rows x 16 bits for the paint, about 1.1ms on a 4.77MHz
; 8088 - against the 4.5ms of held-lock silence the six-call version paid.
; -----------------------------------------------------------------------------
sv_spr_build:
    push si
    push es
    push ds
    pop es

    mov di, sv_spr              ; --- the whole block to ground
    mov cx, SV_SPRSZ
    xor al, al
    cld
    rep stosb

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

    mov al, [sv_fi + si]        ; the colour, both nibbles, for scale 2
    mov [sv_fpx], al
    mov ah, al
    mov cl, 4
    shl ah, cl
    or ah, al
    mov [sv_fpp], ah

    mov ax, [sv_fboy]           ; DI = the sprite's first byte in the block
    mov bl, SV_SPRST
    mul bl
    mov bx, [sv_fbox]
    shr bx, 1                   ; ...and the x offset is EVEN by construction
    add ax, bx
    add ax, sv_spr
    mov [sv_fdst], ax

    cmp byte [sv_fs + si], 1
    je .one
    call sv_spr_2x
    jmp short .out
.one:
    call sv_spr_1x
.out:
    pop es
    pop si
    ret

; -----------------------------------------------------------------------------
; sv_spr_1x - the bitmap at one pixel per pixel: 8 rows of 8 bytes
; in:  [sv_fsrc], [sv_fdst], [sv_fpx]; ES = DS
; out: nothing; clobbers AX, BX, CX, DX, DI
;
; Two source bits make one destination byte, the first of them the HIGH
; nibble. The ground is already there, so a clear bit writes nothing.
; -----------------------------------------------------------------------------
sv_spr_1x:
    mov si, [sv_fsrc]
    mov di, [sv_fdst]
    mov dh, SV_FHU              ; rows
.row:
    push di
    mov dl, 2                   ; two source bytes a row
.src:
    lodsb
    mov bh, 4                   ; four destination bytes from eight bits
.pair:
    xor bl, bl
    shl al, 1
    jnc .p1
    mov bl, [sv_fpx]
    mov cl, 4
    shl bl, cl
.p1:
    shl al, 1
    jnc .p2
    or bl, [sv_fpx]
.p2:
    or [di], bl                 ; OR, not MOV: the ground is already laid and
    inc di                      ; a clear pixel must leave it alone
    dec bh
    jnz .pair
    dec dl
    jnz .src
    pop di
    add di, SV_SPRST
    dec dh
    jnz .row
    ret

; -----------------------------------------------------------------------------
; sv_spr_2x - ...and at two: 8 rows emitted twice, one byte a source bit
; in:  as sv_spr_1x
;
; A doubled pixel is a whole byte - both nibbles the same colour - which is
; why this loop is shorter than the one above rather than longer.
; -----------------------------------------------------------------------------
sv_spr_2x:
    mov ax, [sv_fsrc]
    mov [sv_frow], ax           ; THE ROW POINTER IS IN MEMORY and not in CX,
    mov di, [sv_fdst]           ; which is the bit counter: one register
    mov dh, SV_FHU              ; holding both is a source that walks eight
.row:                           ; bytes back every row
    mov dl, 2                   ; ...each row emitted twice
.twice:
    mov si, [sv_frow]
    push di
    mov bh, 2                   ; two source bytes
.src:
    lodsb
    mov ch, 8
.bit:
    shl al, 1
    jnc .clear
    mov bl, [sv_fpp]
    or [di], bl
.clear:
    inc di
    dec ch
    jnz .bit
    dec bh
    jnz .src
    pop di
    add di, SV_SPRST
    dec dl
    jnz .twice
    add word [sv_frow], 2
    dec dh
    jnz .row
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
    call OSAPI_GFX_FILL
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
sv_fsp:     dw 0                    ; ...and the source it was cut to
sv_fsrc:    dw 0                    ; sv_spr_build's bitmap and its target
sv_fdst:    dw 0
sv_frow:    dw 0                    ; ...and sv_spr_2x's source row cursor
sv_fpx:     db 0                    ; the colour, one nibble and two
sv_fpp:     db 0
sv_bex:     dw 0                    ; a bubble's PREVIOUS blot, banked across
sv_bey:     dw 0                    ; the move so the erase can follow the draw
sv_beph:    db 0
sv_spr:     times SV_SPRSZ db 0     ; the union block itself
