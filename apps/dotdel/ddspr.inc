; =============================================================================
; os8088 - apps/dotdel/ddspr.inc
;
; THE SPRITES (SPEC.md 93.5): eleven 16x16 masters in the image, thirty-five
; scaled images in RAM, and one `gfx_blit1` an actor a frame.
;
; --- MASTERS, NOT ART SETS ---------------------------------------------------
;
; The tile is computed from the surface (ddlay.inc), so there is no size the
; art could have been drawn at. It is drawn ONCE at 16x16 - the size where a
; ghost still has eyes - and cut down to the live tile at layout time by
; nearest-neighbour, which for the two widths this game uses is either a copy
; or "take every other bit". A rebuild is ~35 images of at most 32 bytes and
; happens when the window changes size, which is not a thing that happens in
; a frame.
;
; The masters are also FEWER than the images they make. Smiles faces four ways
; and only the RIGHT-facing wedge is drawn: left is its mirror and the two
; vertical ones are its rotations, which is 16x16 of bit-shuffling done once.
; A ghost's sixteen images are two skirt phases crossed with eight eye states,
; and the eye states are one socket master and one pupil master slid by the
; direction it is looking. Drawing all thirty-five by hand would be 1,120
; bytes of image and thirty-five chances to draw one of them slightly wrong.
;
; --- THE BAND, AND WHY IT IS COMPOSED OUT OF THE BOARD (SPEC.md 93.5.1) ------
;
; An actor is put down as a BAND (SPEC.md 5.4.2): the caller decides every
; bit, and the kernel does one `rep movsw` a row. The band is the UNION of
; where the actor was and where it is, so drawing it IS the erase and there is
; no instant at which the sprite is off the glass - SPEC.md 79.5's finding,
; and the reason this game has no erase pass at all.
;
; What the fish did not have to solve is that the ground here is not black. So
; the band is COPIED OUT OF THE BOARD PICTURE first - which already holds the
; dots that are still there and the ones that are not - and the sprite is
; OR'd into it at whatever bit offset the actor's x has landed on. The board
; picture and the band share a byte grid (ddlay.inc pins the board's screen x
; to a multiple of 8), so the copy is a byte copy and only the sprite is ever
; shifted.
; =============================================================================

; --- the image table ----------------------------------------------------------
; Smiles: direction * 3 + phase, phase 0 closed / 1 half / 2 open.
SP_PAC    equ 0
SP_NPAC   equ 12
; Ghosts: skirt * 8 + big * 4 + look direction.
SP_GHOST  equ SP_PAC + SP_NPAC
SP_NGHOST equ 16
; Frightened: skirt phase only - the face does not look anywhere. Then the
; SAME TWO AGAIN, dithered, which is the flash on an adapter with no pen
; (SPEC.md 93.6.4): a colour machine flashes DD_INKFRI to CWHITE and one plane
; has no pen to flash, so there the flash has to be a different IMAGE.
SP_FRIGHT equ SP_GHOST + SP_NGHOST
SP_NFRIB  equ 2                 ; the plain bodies...
SP_FRIDIM equ SP_FRIGHT + SP_NFRIB   ; ...and their dithered twins
SP_NFRI   equ SP_NFRIB * 2
; Eaten: eyes alone, by direction.
SP_EYES   equ SP_FRIGHT + SP_NFRI
SP_NEYES  equ 4
SP_FRUIT  equ SP_EYES + SP_NEYES
; The death (SPEC.md 93.5.16): the three frames past the widest mouth Smiles
; plays with, and the burst. The first three of the seven the animation runs
; are the mouth he already has, facing up, so only these four are art.
SP_DIE    equ SP_FRUIT + 1
SP_NDIE   equ 4
DD_NSPR   equ SP_DIE + SP_NDIE

; A master is 16 rows of two bytes, byte 0 leftmost, bit 7 leftmost.
DD_MSZ    equ 32

; -----------------------------------------------------------------------------
; dd_spr_build - cut every image from the masters to the live tile
; Clobbers everything. Called from dd_claim and from a relayout, never from a
; frame.
; -----------------------------------------------------------------------------
dd_spr_build:
    call dd_rowmap                  ; which master row each output row takes
    call dd_colmap                  ; ...and which column each output column
    ; --- Smiles ------------------------------------------------------------
    ; RIGHT is drawn and the other three are made out of it: LEFT is its
    ; mirror, DOWN and UP its two quarter turns.
    mov si, dd_a_disc               ; phase 0 is the plain disc and faces
    mov di, dd_w0                   ; nowhere, so all four directions share it
    call dd_m_copy
    mov bl, 0
.p0:
    mov al, bl
    mov ah, 0
    mov si, dd_w0
    call dd_pac_store
    inc bl
    cmp bl, 4
    jb .p0

    mov byte [dd_ph], 1
.pacph:
    mov si, dd_a_disc
    mov di, dd_w0
    call dd_m_copy
    mov si, dd_a_wedge1
    cmp byte [dd_ph], 1
    je .w1
    mov si, dd_a_wedge2
.w1:
    mov di, dd_w1
    call dd_m_copy
    mov si, dd_w1
    mov di, dd_w0
    call dd_m_andnot                ; the mouth is a CUT, not a draw
    mov al, DIR_R
    mov ah, [dd_ph]
    mov si, dd_w0
    call dd_pac_store
    mov si, dd_w0
    mov di, dd_w2
    call dd_m_mirror
    mov al, DIR_L
    mov ah, [dd_ph]
    mov si, dd_w2
    call dd_pac_store
    mov si, dd_w0
    mov di, dd_w2
    call dd_m_rotcw                 ; a mouth opening +x, turned clockwise,
    mov al, DIR_D                   ; opens +y
    mov ah, [dd_ph]
    mov si, dd_w2
    call dd_pac_store
    mov si, dd_w0
    mov di, dd_w2
    call dd_m_rotccw
    mov al, DIR_U
    mov ah, [dd_ph]
    mov si, dd_w2
    call dd_pac_store
    inc byte [dd_ph]
    cmp byte [dd_ph], 3
    jb .pacph

    ; --- ghosts: two skirts x (normal, big) x four looks --------------------
    xor ch, ch                      ; CH = skirt phase
.gskirt:
    xor cl, cl                      ; CL = 0 normal sockets, 1 big
.gsize:
    mov bl, 0                       ; BL = look direction
.glook:
    push cx
    push bx
    mov si, dd_a_gh0
    cmp ch, 0
    je .gb0
    mov si, dd_a_gh1
.gb0:
    mov di, dd_w0
    call dd_m_copy                  ; the body
    mov si, dd_a_eye0
    cmp cl, 0
    je .ge0
    mov si, dd_a_eye1
.ge0:
    mov di, dd_w1
    call dd_m_copy
    mov si, dd_w1                   ; ...with the sockets cut out of it
    mov di, dd_w0
    call dd_m_andnot
    mov si, dd_a_pup0
    cmp cl, 0
    je .gp0
    mov si, dd_a_pup1
.gp0:
    mov di, dd_w1
    call dd_m_copy
    mov al, bl                      ; ...and the pupils slid the way it looks
    mov ah, cl
    mov di, dd_w1
    call dd_m_gaze
    mov si, dd_w1
    mov di, dd_w0
    call dd_m_or
    pop bx
    pop cx
    ; index = SP_GHOST + skirt*8 + big*4 + dir
    push bx
    push cx
    mov al, ch
    shl al, 1
    shl al, 1
    shl al, 1
    mov ah, cl
    shl ah, 1
    shl ah, 1
    add al, ah
    add al, bl
    add al, SP_GHOST
    xor ah, ah
    mov si, dd_w0
    call dd_spr_cut
    pop cx
    pop bx
    inc bl
    cmp bl, 4
    jb .glook
    inc cl
    cmp cl, 2
    jb .gsize
    inc ch
    cmp ch, 2
    jb .gskirt

    ; --- frightened: body, square eyes and a zigzag, per skirt phase --------
    xor ch, ch
.fskirt:
    push cx
    mov si, dd_a_gh0
    cmp ch, 0
    je .fb0
    mov si, dd_a_gh1
.fb0:
    mov di, dd_w0
    call dd_m_copy
    mov si, dd_a_face
    mov di, dd_w1
    call dd_m_copy
    mov si, dd_w1
    mov di, dd_w0
    call dd_m_andnot
    pop cx
    push cx
    mov al, ch
    add al, SP_FRIGHT
    xor ah, ah
    mov si, dd_w0
    call dd_spr_cut
    pop cx
    inc ch
    cmp ch, 2
    jb .fskirt

    ; --- ...and the dithered twins (SPEC.md 93.6.4) -------------------------
    ; THE SCALED SPRITE AND NOT THE MASTER. A checkerboard laid on the 16-row
    ; master and then scaled is not a checkerboard afterwards - dd_rmap repeats
    ; and drops rows, so a 16x9 Hercules tile turns it into bands. Dithering
    ; what dd_spr_cut has already produced puts one lit pixel in two at the
    ; SCREEN's resolution, which is the only place it means anything.
    xor ch, ch
.fdith:
    mov al, ch
    add al, SP_FRIGHT
    xor ah, ah
    mov bx, DD_SPRSZ
    mul bx
    mov si, dd_spr
    add si, ax                      ; SI = the plain body
    mov al, ch
    add al, SP_FRIDIM
    xor ah, ah
    mul bx
    mov di, dd_spr
    add di, ax                      ; DI = where its twin goes
    xor dx, dx                      ; DX = the output row
.drow:
    mov al, 0AAh                    ; ...alternating, so the rows interlock
    test dl, 1
    jz .dmask
    mov al, 55h
.dmask:
    mov ah, al
    and al, [si]
    and ah, [si+1]
    mov [di], al
    mov [di+1], ah
    add si, DD_SPRB
    add di, DD_SPRB
    inc dx
    cmp dx, [dd_th]
    jb .drow
    inc ch
    cmp ch, SP_NFRIB
    jb .fdith

    ; --- eaten: the sockets themselves, as ink, with a hole for the pupil ---
    xor bl, bl
.eyes:
    push bx
    mov si, dd_a_eye1               ; the WIDE socket: eyes with no body to
    mov di, dd_w0                   ; sit in have to carry their own size
    call dd_m_copy
    mov si, dd_a_pup1
    mov di, dd_w1
    call dd_m_copy
    mov al, bl
    mov ah, 1
    mov di, dd_w1
    call dd_m_gaze
    mov si, dd_w1
    mov di, dd_w0
    call dd_m_andnot
    pop bx
    push bx
    mov al, bl
    add al, SP_EYES
    xor ah, ah
    mov si, dd_w0
    call dd_spr_cut
    pop bx
    inc bl
    cmp bl, 4
    jb .eyes

    mov si, dd_a_fruit
    mov ax, SP_FRUIT
    call dd_spr_cut

    ; --- the death, in the one direction it faces --------------------------
    mov si, dd_a_die0
    mov ax, SP_DIE + 0
    call dd_spr_cut
    mov si, dd_a_die1
    mov ax, SP_DIE + 1
    call dd_spr_cut
    mov si, dd_a_die2
    mov ax, SP_DIE + 2
    call dd_spr_cut
    mov si, dd_a_burst
    mov ax, SP_DIE + 3
    call dd_spr_cut
    ret

; -----------------------------------------------------------------------------
; dd_pac_store - keep the master at SI as Smiles facing AL in phase AH
; in:  AL = direction, AH = phase, SI = a 16x16 master. Preserves all.
; -----------------------------------------------------------------------------
dd_pac_store:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dl, ah                      ; the phase, before MUL writes AH
    mov bl, 3
    mul bl                          ; AX = direction * 3
    mov ah, 0
    add al, dl
    adc ah, 0
    add ax, SP_PAC
    call dd_spr_cut
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_rowmap - dd_rmap[j] = the master row output row j takes, j < [dd_th]
; -----------------------------------------------------------------------------
dd_rowmap:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, dd_rmap
    xor cx, cx
.each:
    mov ax, cx
    mov bl, 16
    mul bl                          ; AX = j * 16, and an 8-bit MUL leaves DX
    xor dx, dx                      ; ALONE - which a 16-bit DIV then reads
    div word [dd_th]                ; AX = j * 16 / th
    mov [di], al
    inc di
    inc cx
    cmp cx, [dd_th]
    jb .each
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_spr_cut - scale the 16x16 master at SI into image AX of dd_spr
; in:  AX = image index, SI = master. Preserves every register.
;
; The row map is a table; the COLUMN map is one branch, because the only two
; tile widths this game uses are the master's own and exactly half of it.
; -----------------------------------------------------------------------------
dd_spr_cut:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, DD_SPRSZ
    mul bx
    mov di, dd_spr
    add di, ax                      ; DI = this image
    xor cx, cx                      ; CX = the output row
.row:
    mov bx, cx
    mov bl, [dd_rmap + bx]          ; the master row it takes
    xor bh, bh
    shl bx, 1
    mov al, [si + bx]
    mov ah, [si + bx + 1]
    cmp word [dd_tw], 16
    je .wide
    call dd_ccut                    ; ANY width, through dd_cmap - it was 16
.wide:                              ; or exactly half, and the tile is neither
    mov [di], al                    ; now (SPEC.md 93.3.3). A narrow image is
    mov [di+1], ah                  ; a wide one with the spare bits clear, so
    add di, DD_SPRB                 ; every reader strides by DD_SPRB. It used
.next:                              ; to pack narrow rows one byte apart while
                                    ; dd_band_one read them two apart, which is
                                    ; why a CGA's actors were noise
    inc cx
    cmp cx, [dd_th]
    jb .row
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_colmap - dd_cmap[j] = the master column output column j takes, j < [dd_tw]
; dd_rowmap's twin, and it exists for the same reason: the tile is no longer
; the master's own width or exactly half of it (SPEC.md 93.3.3).
; -----------------------------------------------------------------------------
dd_colmap:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, dd_cmap
    xor cx, cx
.each:
    mov ax, cx
    mov bl, 16
    mul bl                          ; AX = j * 16, and an 8-bit MUL leaves DX
    xor dx, dx                      ; ALONE - which a 16-bit DIV then reads
    div word [dd_tw]
    mov [di], al
    inc di
    inc cx
    cmp cx, [dd_tw]
    jb .each
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_ccut - one master row cut to [dd_tw] columns through dd_cmap
; in:  AL = the master's left byte (bit 7 is column 0), AH = its right
; out: AL/AH the same packing, [dd_tw] columns wide, the rest clear
; Preserves BX/CX/DX/SI/DI.
;
; It replaces dd_squash, which was nearest-neighbour at exactly half width and
; nothing else - the only two tile widths that existed. A 16-bit big-endian
; word is the natural shape here: column c is bit 15-c, so taking a column is
; one variable SHL and a sign test.
; -----------------------------------------------------------------------------
dd_ccut:
    push bx
    push cx
    push dx
    push si
    mov bh, al                      ; BX = the row, bit 15 = column 0
    mov bl, ah
    xor dx, dx                      ; DX = the output, built from the left
    xor si, si
    mov cx, [dd_tw]
.col:
    push cx
    mov cl, [dd_cmap + si]
    xor ch, ch
    mov ax, bx
    shl ax, cl                      ; the column it takes, into bit 15
    add dx, dx                      ; ...and the output makes room for it
    or ax, ax
    jns .zero
    inc dx
.zero:
    pop cx
    inc si
    loop .col
    mov cx, 16
    sub cx, [dd_tw]                 ; left-justify: column 0 must be bit 15
    jcxz .just
    shl dx, cl
.just:
    mov al, dh
    mov ah, dl
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; 16x16 MASTER OPERATIONS - all on the three scratch masters dd_w0..dd_w2
; =============================================================================

dd_m_copy:                          ; DI <- SI
    push cx
    push si
    push di
    mov cx, DD_MSZ
.c:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .c
    pop di
    pop si
    pop cx
    ret

dd_m_or:                            ; DI |= SI
    push cx
    push si
    push di
    mov cx, DD_MSZ
.c:
    mov al, [si]
    or [di], al
    inc si
    inc di
    loop .c
    pop di
    pop si
    pop cx
    ret

dd_m_andnot:                        ; DI &= ~SI
    push cx
    push si
    push di
    mov cx, DD_MSZ
.c:
    mov al, [si]
    not al
    and [di], al
    inc si
    inc di
    loop .c
    pop di
    pop si
    pop cx
    ret

; dd_m_mirror - DI = SI flipped left to right
dd_m_mirror:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, 16
.row:
    mov dh, [si]                    ; the left byte
    mov dl, [si+1]                  ; ...and the right
    xor ax, ax
    mov cx, 8
.l:
    shr dl, 1                       ; the right byte's bit 0 is the new left
    rcl ah, 1
    loop .l
    mov cx, 8
.r:
    shr dh, 1
    rcl al, 1
    loop .r
    mov [di], ah
    mov [di+1], al
    add si, 2
    add di, 2
    dec bx
    jnz .row
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dd_m_rotcw  - DI[y][x] = SI[15-x][y]
; dd_m_rotccw - DI[y][x] = SI[x][15-y]
dd_m_rotcw:
    mov byte [dd_rotd], 0
    jmp short dd_m_rot
dd_m_rotccw:
    mov byte [dd_rotd], 1
dd_m_rot:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov cx, DD_MSZ                  ; the destination starts empty
    push di
.z:
    mov byte [di], 0
    inc di
    loop .z
    pop di
    xor bx, bx                      ; BX = destination row y
.row:
    xor cx, cx                      ; CX = destination column x
.col:
    ; source (sy, sx)
    cmp byte [dd_rotd], 0
    jne .ccw
    mov bp, 15
    sub bp, cx                      ; sy = 15 - x
    mov dx, bx                      ; sx = y
    jmp short .fetch
.ccw:
    mov bp, cx                      ; sy = x
    mov dx, 15
    sub dx, bx                      ; sx = 15 - y
.fetch:
    call dd_m_getbit                ; AL = 0/1 from SI at (BP row, DX col)
    or al, al
    jz .nx
    call dd_m_setbit                ; ...into DI at (BX row, CX col)
.nx:
    inc cx
    cmp cx, 16
    jb .col
    inc bx
    cmp bx, 16
    jb .row
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dd_m_getbit - AL = the bit at row BP, column DX of the master at SI
dd_m_getbit:
    push bx
    push cx
    push si
    mov bx, bp
    shl bx, 1
    add si, bx
    mov bx, dx
    mov cl, 3
    shr bx, cl
    mov al, [si + bx]
    mov cx, dx
    and cl, 7
    mov ch, 7
    sub ch, cl
    mov cl, ch
    shr al, cl
    and al, 1
    pop si
    pop cx
    pop bx
    ret

; dd_m_setbit - set the bit at row BX, column CX of the master at DI
dd_m_setbit:
    push ax
    push bx
    push cx
    push di
    push dx
    mov dx, bx
    shl dx, 1
    add di, dx
    mov dx, cx
    mov bx, cx
    push cx
    mov cl, 3
    shr bx, cl
    pop cx
    add di, bx
    and cl, 7
    mov ch, 7
    sub ch, cl
    mov cl, ch
    mov al, 1
    shl al, cl
    or [di], al
    pop dx
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_m_gaze - slide the pupil master at DI the way it is looking
; in:  AL = direction, AH = 0 normal / 1 big (the big pair looks further)
; -----------------------------------------------------------------------------
dd_m_gaze:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor cx, cx
    mov bl, 1
    cmp ah, 0
    je .amt
    mov bl, 2
.amt:
    mov bh, al
    ; vertical first: DOWN adds rows, UP removes them
    cmp bh, DIR_D
    je .down
    cmp bh, DIR_U
    je .up
    cmp bh, DIR_L
    je .left
    ; right
    mov cl, bl
    call dd_m_shr
    jmp short .out
.left:
    mov cl, bl
    call dd_m_shl
    jmp short .out
.down:
    mov cl, bl
    call dd_m_shd
    jmp short .out
.up:
    mov cl, bl
    call dd_m_shu
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dd_m_shr / dd_m_shl - slide the master at DI right/left by CL pixels
dd_m_shr:
    push bx
    push cx
    push di
    mov bx, 16
.row:
    push cx
    mov al, [di]
    mov ah, [di+1]
    xchg al, ah                     ; AX = the row, high byte LEFTMOST
    shr ax, cl
    xchg al, ah
    mov [di], al
    mov [di+1], ah
    pop cx
    add di, 2
    dec bx
    jnz .row
    pop di
    pop cx
    pop bx
    ret

dd_m_shl:
    push bx
    push cx
    push di
    mov bx, 16
.row:
    push cx
    mov al, [di]
    mov ah, [di+1]
    xchg al, ah
    shl ax, cl
    xchg al, ah
    mov [di], al
    mov [di+1], ah
    pop cx
    add di, 2
    dec bx
    jnz .row
    pop di
    pop cx
    pop bx
    ret

; dd_m_shd / dd_m_shu - slide the master at DI down/up by CL rows
dd_m_shd:
    push ax
    push bx
    push cx
    push si
    push di
    xor ch, ch
    xor bx, bx                      ; BX = the output row
.r:
    mov ax, bx
    sub ax, cx                      ; ...takes the row CL above it
    js .zero
    push bx
    shl ax, 1
    mov si, ax
    add si, di                      ; [di+si] is not an 8086 address; [si] is
    mov al, [si]
    mov ah, [si+1]
    pop bx
    jmp short .put
.zero:
    xor ax, ax
.put:
    push bx
    shl bx, 1
    mov [dd_w3 + bx], al
    mov [dd_w3 + bx + 1], ah
    pop bx
    inc bx
    cmp bx, 16
    jb .r
    mov si, dd_w3
    call dd_m_copy
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

dd_m_shu:
    push ax
    push bx
    push cx
    push si
    push di
    xor ch, ch
    xor bx, bx
.r:
    mov ax, bx
    add ax, cx
    cmp ax, 16
    jae .zero
    push bx
    shl ax, 1
    mov si, ax
    add si, di                      ; [di+si] is not an 8086 address; [si] is
    mov al, [si]
    mov ah, [si+1]
    pop bx
    jmp short .put
.zero:
    xor ax, ax
.put:
    push bx
    shl bx, 1
    mov [dd_w3 + bx], al
    mov [dd_w3 + bx + 1], ah
    pop bx
    inc bx
    cmp bx, 16
    jb .r
    mov si, dd_w3
    call dd_m_copy
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

%include "ddart.inc"
