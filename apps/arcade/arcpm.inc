; =============================================================================
; os8088 - apps/arcade/arcpm.inc - the Namco Pac-Man board, the host's half
; (ARCADE-SPEC 7). Included by arcade.asm when it is built with the ROM.
;
; The Z80 is apps/arcade/arcz80t.py's translation; this is everything around
; it: the board's cells at power-on, its one interrupt a frame (IM 2, the line
; HELD until taken or until the game clears 5000 bit 0), its joystick and
; buttons, the Namco WSG as speaker tones, and the picture.
;
; THE PICTURE (ARCADE-SPEC 7.7). VGA only, 640x480x16: the board has exactly
; 16 colours, which go into the DAC once. The 224 x 288 portrait screen is
; drawn twice as wide and one and a half times as tall - 448 x 432, centred -
; and only where something changed: a tile the Z80 wrote (the write list, as
; Space Invaders' cells) and the box each sprite left and entered. Every such
; box is COMPOSED in RAM - the tiles, then the sprites over them - and written
; to the planes once, so no pixel is drawn twice and a sprite never flickers
; over its own erase (PERFORMANCE.md rule 2).
;
; It is composed IN PLANES, a byte a plane for eight pixels, never a byte a
; pixel: a tile row's plane byte is (m0 AND S0) OR (m1 AND S1) OR (m2 AND S2)
; OR (m3 AND S3), mv being the row's pixels of value v and Sv FFh when the
; palette's colour for v has that plane's bit; a sprite row is the same in
; words, shifted into place, cut into the planes through its opaque mask. A
; byte a pixel and a conversion to planes after it cost a 3 x 3-tile box
; about 55k clocks - a 286's frame is 266k - and this is a fraction of it.
;
; The ROM part (apps/arcade/arcpmgfx.py) carries the tiles and sprites turned
; to the portrait screen, the colours, and the two tile maps.
; =============================================================================

PM_TMASK    equ 0x6400              ; ROM part (arcpmgfx.py): a tile's rows as
                                    ;   value masks, 3 bytes a row
PM_SMASK    equ 0x7C00              ;   a sprite's, 3 words a row, then flipped
PM_PSEL     equ 0xAC00              ;   a palette's plane selectors, 16 bytes
PM_POPQ     equ 0xAE00              ;   its opacity, 4 bytes
PM_DAC      equ 0xAE80              ;   16 DAC triples
PM_S2T      equ 0xAF00              ; (ty, tx) -> video RAM index
PM_T2S      equ 0xB700              ; video RAM index -> (ty << 8) | tx
PM_ROMLEN   equ 0x4000
PM_XB       equ 12                  ; the picture's left edge: byte 12 (96 px)
PM_Y0       equ 24                  ;   and its top row

; -----------------------------------------------------------------------------
; pm_reset - Pac-Man's cells at power-on. Every register kept.
; -----------------------------------------------------------------------------
pm_reset:
    push ax
    push cx
    push di
    push es
    mov es, [ar_vseg]
    mov di, 0x4800                  ; 4800-4BFF is not there: reads give BF
    mov cx, 0x400
    mov al, 0xBF
    cld
    rep stosb
    mov byte [es:PM_IN0], 0xFF      ; nothing pressed (the inputs are low
    mov byte [es:PM_IN1], 0xFF      ;   when pressed; IN1 bit 7 = upright)
    mov byte [es:PM_DSW1], 0xC9     ; 1 coin 1 credit, 3 lives, bonus at
    mov byte [es:PM_DSW2], 0xFF     ;   10000, normal, the ghosts' names
    mov word [es:ST_IX], 0xFFFF
    mov word [es:ST_IY], 0xFFFF
    mov word [es:ST_SP], 0xFFFF
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pm_irq - the VBlank interrupt is due. Dropped (CF=0) when the game has it
; disabled; masked (CF=1, ST_PEND set) while IFF1 is clear; else IM 2: the PC
; onto the stack and the vector's word, I x 256 + the latched byte.
; -----------------------------------------------------------------------------
pm_irq:
    mov es, [ar_vseg]
    cmp byte [es:ST_IRQEN], 0
    je .dropped
    mov byte [es:ST_PEND], 1
    cmp byte [es:ST_INTE], 0
    je .masked
    mov byte [es:ST_INTE], 0
    mov byte [es:ST_PEND], 0
    mov bx, [es:ST_SP]
    sub bx, 2
    mov [es:ST_SP], bx
    mov ax, [es:ST_PC]
    mov [es:bx], ax
    mov bh, [es:ST_I]
    mov bl, [es:ST_VEC]
    and bl, 0xFE
    mov ax, [es:bx]
    mov [es:ST_PC], ax
.dropped:
    clc
    ret
.masked:
    stc
    ret

; -----------------------------------------------------------------------------
; pm_frame - after the frame's interrupt: the picture, the sound, the keys
; -----------------------------------------------------------------------------
pm_frame:
    call pm_render
    call pm_sound
    ; fall through

; pm_keys - IN0 (the joystick, the coin) and IN1 (the starts), active low
pm_keys:
    mov bl, 0xFF
    mov bh, 0xFF
    mov al, [ar_pbits]
    test al, AR_COIN
    jz .nocoin
    and bl, ~0x20
.nocoin:
    test al, AR_START1
    jz .no1
    and bh, ~0x20
.no1:
    test al, AR_START2
    jz .no2
    and bh, ~0x40
.no2:
    mov si, pm_keymap
.k:
    lodsw
    or al, al
    jz .done
    call OSAPI_KEY_DOWN
    jnc .k
    not ah
    and bl, ah
    jmp .k
.done:
    mov es, [ar_vseg]
    mov [es:PM_IN0], bl
    mov [es:PM_IN1], bh
    ret

pm_keymap:                          ; scancode, IN0 bit
    db KSC_UP, 0x01
    db KSC_LEFT, 0x02
    db KSC_RIGHT, 0x04
    db KSC_DOWN, 0x08
    db 0, 0

; -----------------------------------------------------------------------------
; pm_sound - the Namco WSG's three voices as one speaker tone: the loudest
; voice that has a frequency, at f x 96000 / 2^20 Hz (a 20-bit accumulator
; stepping at 96 kHz through a 32-sample wave). Silent when 5001 bit 0 says
; the sound is off.
; -----------------------------------------------------------------------------
pm_sound:
    mov es, [ar_vseg]
    test byte [es:PM_LATCH+1], 1
    jz .quiet
    xor dx, dx                      ; DL = the loudest volume, DI = its f
    xor di, di
    mov si, pm_voices
.v:
    lodsw                           ; AL = the volume's nibble, AH = the
    or al, al                       ;   frequency's first (lowest kept)
    jz .best
    mov bl, al
    mov bh, 0
    mov cl, [es:bx+PM_SOUND-0x40]   ; the volume
    mov bl, ah
    mov ax, [es:bx+PM_SOUND-0x40]   ; four nibbles, low first, into a word
    mov ch, [es:bx+PM_SOUND-0x40+2]
    and ax, 0x0F0F
    and ch, 0x0F
    push cx
    mov cl, 4
    shl ah, cl
    or al, ah                       ; AL = n1 | n2 << 4 ... (low byte)
    mov ah, ch
    mov ch, [es:bx+PM_SOUND-0x40+3]
    and ch, 0x0F
    shl ch, cl
    or ah, ch                       ; AH = n3 | n4 << 4
    pop cx
    or ax, ax
    jz .v
    cmp cl, dl
    jbe .v
    mov dl, cl
    mov di, ax
    jmp .v
.best:
    or dl, dl
    jz .quiet
    mov ax, di                      ; Hz = f16 x 375 / 256
    mov cx, 375
    mul cx
    or dh, dh
    jnz .quiet
    mov al, ah
    mov ah, dl
    cmp ax, 20
    jb .quiet
    jmp ar_tone
.quiet:
    jmp ar_quiet

pm_voices:                          ; the volume's register, the frequency's
    db 0x55, 0x51                   ;   (voice 0's lowest nibble is dropped)
    db 0x5A, 0x56
    db 0x5F, 0x5B
    dw 0

; -----------------------------------------------------------------------------
; pm_setup - in the bracket: VGA's 640x480x16, the colours, the row table,
; and the whole screen drawn once. SI = the window. CF=1 refused.
; -----------------------------------------------------------------------------
pm_setup:
    mov bx, si
    call OSAPI_FSX_CAPS
    test ax, 1 << FSXM_VGA12
    jnz .vga
    mov word [ar_err], ar_s_vgaonly
    stc
    ret
.vga:
    mov al, FSXM_VGA12
    push es
    push ds
    pop es
    mov di, ar_fsi
    call OSAPI_FSX_MODE
    pop es
    jnc .set
    mov word [ar_err], ar_s_nomode
    ret
.set:
    mov ax, [ar_fsi+FSI_SEG]
    mov [ar_vidseg], ax
    call ar_dblinit
    mov es, [ar_rseg]               ; the 16 colours, each into the DAC
    mov si, PM_DAC                  ;   register the attribute controller's
    xor bx, bx                      ;   default palette points it at
.dac:
    mov dx, 0x3C8
    mov al, [pm_acmap+bx]
    out dx, al
    inc dx
    mov cx, 3
.rgb:
    mov al, [es:si]
    inc si
    out dx, al
    loop .rgb
    inc bx
    cmp bx, 16
    jb .dac
    xor bx, bx                      ; the row table: 288 rows into 432, the
    mov di, pm_yoff                 ;   even rows doubled
.row:
    mov ax, bx
    shr ax, 1
    mov cx, ax
    shl ax, 1
    add ax, cx                      ; (y / 2) x 3
    test bl, 1
    jz .even
    add ax, 2
.even:
    add ax, PM_Y0
    mov cx, 80
    mul cx
    add ax, PM_XB
    mov [di], ax
    add di, 2
    inc bx
    cmp bx, 288
    jb .row
    mov es, [pm_shseg]              ; the shadow: the mode set left the
    xor di, di                      ;   glass black
    mov cx, PM_SHPL * 2
    xor ax, ax
    cld
    rep stosw
    mov cx, 8                       ; no sprite has been drawn yet
    mov di, pm_old
    mov ax, 0x8000
.none:
    mov [di], ax
    add di, PMS_SIZE
    loop .none
    call pm_whole
    clc
    ret

pm_acmap:   db 0, 1, 2, 3, 4, 5, 0x14, 7, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F

; pm_whole - the whole screen, in 4 x 4 tiles, and the write list emptied
; with every tile's flag
pm_whole:
    push ds                         ; every tile recomposed: the cache too
    pop es
    mov di, pm_tvalid
    mov cx, 28 * 36
    xor al, al
    cld
    rep stosb
    mov word [pm_ty], 0
.all:
    mov word [pm_tx], 0
.across:
    mov ax, [pm_tx]
    mov bx, [pm_ty]
    mov cx, 28
    sub cx, ax
    cmp cx, 4
    jbe .w
    mov cx, 4
.w:
    mov dx, 36
    sub dx, bx
    cmp dx, 4
    jbe .h
    mov dx, 4
.h:
    call pm_compose
    add word [pm_tx], 4
    cmp word [pm_tx], 28
    jb .across
    add word [pm_ty], 4
    cmp word [pm_ty], 36
    jb .all
    mov es, [ar_aseg]
    mov word [es:AUX_LPTR], AUX_LIST
    xor di, di
.clr:
    mov byte [es:di+PM_TFLAG], 0
    inc di
    cmp di, 0x400
    jb .clr
    ret

; -----------------------------------------------------------------------------
; pm_render - the tiles the write list names, then every sprite that moved or
; changed: the box it left and the box it is in now
; -----------------------------------------------------------------------------
PMS_X       equ 0                   ; a sprite's state: its portrait x (word,
PMS_Y       equ 2                   ;   0x8000 = not shown), y, code byte, the
PMS_ATTR    equ 4                   ;   palette byte
PMS_PAL     equ 5
PMS_SIZE    equ 6

PM_MANY     equ 96                  ; more tiles than this changed: the whole
                                    ;   screen in 4 x 4 blocks is cheaper
                                    ;   than each alone (a clear lists 1,008)
pm_render:
    mov es, [ar_aseg]
    mov ax, [es:AUX_LPTR]
    sub ax, AUX_LIST
    cmp ax, PM_MANY * 2
    jbe .few
    call pm_whole
    jmp .tilesdone
.few:
    mov si, AUX_LIST
.tiles:
    mov es, [ar_aseg]
    cmp si, [es:AUX_LPTR]
    jae .tilesdone
    mov bx, [es:si]
    add si, 2
    mov byte [es:bx+PM_TFLAG], 0
    shl bx, 1
    mov es, [ar_rseg]
    mov ax, [es:bx+PM_T2S]
    cmp ax, 0xFFFF
    je .tiles
    push si
    push ax                         ; its cache is stale: ty x 28 + tx
    mov bl, ah
    mov al, 28
    mul bl
    pop bx
    push bx
    mov bh, 0
    add bx, ax
    mov byte [pm_tvalid+bx], 0
    pop ax
    mov bl, ah
    mov bh, 0
    mov ah, 0
    mov cx, 1
    mov dx, 1
    call pm_compose
    pop si
    jmp .tiles
.tilesdone:
    mov word [es:AUX_LPTR], AUX_LIST
    call pm_sprites                 ; pm_cur from the board
    xor bx, bx                      ; BX = the sprite's offset in pm_cur
.spr:
    mov ax, [pm_cur+bx+PMS_X]
    cmp ax, [pm_old+bx+PMS_X]
    jne .moved
    mov ax, [pm_cur+bx+PMS_Y]
    cmp ax, [pm_old+bx+PMS_Y]
    jne .moved
    mov ax, [pm_cur+bx+PMS_ATTR]
    cmp ax, [pm_old+bx+PMS_ATTR]
    je .next
.moved:
    push bx
    lea si, [pm_old+bx]             ; where it was and where it is
    lea di, [pm_cur+bx]
    call pm_spritebox
    pop bx
    mov ax, [pm_cur+bx+PMS_X]
    mov [pm_old+bx+PMS_X], ax
    mov ax, [pm_cur+bx+PMS_Y]
    mov [pm_old+bx+PMS_Y], ax
    mov ax, [pm_cur+bx+PMS_ATTR]
    mov [pm_old+bx+PMS_ATTR], ax
.next:
    add bx, PMS_SIZE
    cmp bx, 8 * PMS_SIZE
    jb .spr
    ret

; pm_sprites - the eight sprites' state, from the board, into pm_cur
pm_sprites:
    push ds
    pop es
    mov di, pm_cur
    mov si, 0
.s:
    push ds
    mov ds, [ar_vseg]
    mov al, [si+0x4FF0]             ; code << 2 | y flip << 1 | x flip
    test al, 3                      ;   (MAME's fx is bit 0); held here
    jpe .same                       ;   swapped, x flip in bit 1, the one
    xor al, 3                       ;   that reverses a turned sprite's rows
.same:
    mov ah, [si+0x4FF1]             ; the palette
    mov cl, [si+PM_SPRXY]           ; MAME: sx = 272 - [5061], sy = [5060]
    mov ch, [si+PM_SPRXY+1]         ;   - 31; turned, portrait x = 239 -
    pop ds                          ;   [5060] and y = 272 - [5061]
    and ah, 0x1F
    mov [di+PMS_ATTR], ax
    mov ax, 239
    mov dl, cl
    mov dh, 0
    sub ax, dx
    cmp si, 6                       ; MAME's xoffsethack: sprites 0-2 sit
    jae .xo                         ;   one pixel over
    dec ax
.xo:
    mov [di+PMS_X], ax
    mov ax, 272
    mov dl, ch
    sub ax, dx
    mov [di+PMS_Y], ax
    add di, PMS_SIZE
    add si, 2
    cmp si, 16
    jb .s
    ret

; pm_sprbox - the tile box a sprite's state covers. SI -> the state. CF=1
; nothing on the screen; else AX = the first tile across, BX = down, CX =
; across, DX = down. Clobbers nothing else.
pm_sprbox:
    mov ax, [si+PMS_X]
    cmp ax, 0x8000
    je .none
    mov dx, [si+PMS_Y]
    cmp ax, -15                     ; wholly off the screen: nothing
    jl .none
    cmp ax, 224
    jge .none
    cmp dx, 1
    jl .none
    cmp dx, 272
    jge .none
    mov cx, ax                      ; tiles: x / 8 .. (x + 15) / 8, clipped
    add cx, 15
    sar cx, 1
    sar cx, 1
    sar cx, 1
    sar ax, 1
    sar ax, 1
    sar ax, 1
    jns .xin
    xor ax, ax
.xin:
    cmp cx, 27
    jle .xok
    mov cx, 27
.xok:
    sub cx, ax
    inc cx                          ; CX = tiles across
    mov bx, dx
    add bx, 15
    sar bx, 1
    sar bx, 1
    sar bx, 1
    sar dx, 1
    sar dx, 1
    sar dx, 1
    cmp bx, 33
    jle .yok
    mov bx, 33
.yok:
    sub bx, dx
    inc bx
    xchg bx, dx                     ; BX = the first tile row, DX = rows
    clc
    ret
.none:
    stc
    ret

; pm_spritebox - a sprite moved or changed: SI -> where it was, DI -> where
; it is. One box for both when they fit 4 x 4 tiles, else each its own.
pm_spritebox:
    push di
    call pm_sprbox
    jc .newonly
    mov [pm_ox], ax
    mov [pm_oy], bx
    add cx, ax
    mov [pm_ox1], cx
    add dx, bx
    mov [pm_oy1], dx
    pop si
    call pm_sprbox
    jc .oldonly
    add cx, ax                      ; the union
    add dx, bx
    cmp ax, [pm_ox]
    jle .u1
    mov ax, [pm_ox]
.u1:
    cmp bx, [pm_oy]
    jle .u2
    mov bx, [pm_oy]
.u2:
    cmp cx, [pm_ox1]
    jge .u3
    mov cx, [pm_ox1]
.u3:
    cmp dx, [pm_oy1]
    jge .u4
    mov dx, [pm_oy1]
.u4:
    sub cx, ax
    sub dx, bx
    cmp cx, 4
    ja .apart
    cmp dx, 4
    ja .apart
    jmp pm_compose
.apart:
    push si
    call pm_sprbox
    call pm_compose
    pop si
.oldonly:
    mov ax, [pm_ox]
    mov bx, [pm_oy]
    mov cx, [pm_ox1]
    sub cx, ax
    mov dx, [pm_oy1]
    sub dx, bx
    jmp pm_compose
.newonly:
    pop si
    call pm_sprbox
    jc .out
    jmp pm_compose
.out:
    ret

; -----------------------------------------------------------------------------
; pm_compose - AX = the first tile across, BX = down, CX = tiles across (1-4),
; DX = tiles down (1-4): compose them and every sprite over them in pm_pl,
; then put them on the screen. Clobbers every register but DS.
; -----------------------------------------------------------------------------
pm_compose:
    mov [pm_ctx], ax
    mov [pm_cty], bx
    mov [pm_ctw], cx
    mov [pm_cth], dx
    mov es, [ar_rseg]
    xor bp, bp                      ; BP = the tile row in the box
.trow:
    xor dx, dx                      ; DX = the tile across in the box
.tcol:
    mov ax, [pm_cty]                ; its video RAM index: the map
    add ax, bp
    mov cl, 28
    mul cl
    add ax, [pm_ctx]
    add ax, dx
    mov [pm_t], ax                  ; the screen tile's number, for its cache
    mov bx, ax
    mov di, bp                      ; its place in pm_pl: row (tile row x 8)
    mov cl, 5                       ;   x 4, + the tile across
    shl di, cl
    add di, dx
    add di, pm_pl
    cmp byte [pm_tvalid+bx], 0      ; composed before, and not written since:
    je .compute                     ;   its 32 plane bytes from the cache
    push dx
    push ds
    mov si, ax
    mov cl, 5
    shl si, cl
    add si, PM_CACHE
    push ds
    pop es
    mov ds, [pm_shseg]
    mov dx, 4
.cpl:
    mov cx, 8
.cr:
    movsb
    add di, 3
    loop .cr
    add di, 128 - 32
    dec dx
    jnz .cpl
    pop ds
    pop dx
    mov es, [ar_rseg]
    jmp .tdone
.compute:
    shl ax, 1
    mov bx, ax
    mov bx, [es:bx+PM_S2T]
    push ds
    mov ds, [ar_vseg]
    mov al, [bx+0x4000]             ; the tile
    mov ah, [bx+0x4400]             ; its palette
    pop ds
    push ax
    mov al, ah                      ; the palette's 16 selectors, to pm_sel
    and al, 0x1F
    mov ah, 0
    mov cl, 4
    shl ax, cl
    add ax, PM_PSEL
    mov si, ax
    mov cx, 8
    mov di, pm_sel
.sel:
    mov ax, [es:si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .sel
    pop ax                          ; its masks: the tile x 24
    mov ah, 0
    mov si, ax
    shl ax, 1
    add ax, si
    mov cl, 3
    shl ax, cl
    add ax, PM_TMASK
    mov si, ax
    mov di, bp                      ; pm_pl again: DI was the selectors'
    mov cl, 5
    shl di, cl
    add di, dx
    add di, pm_pl
    push dx
    push di
    mov ch, 8
.row:
    mov bh, [es:si]                 ; BH, DL, DH = the values 1, 2, 3
    mov dl, [es:si+1]
    mov dh, [es:si+2]
    add si, 3
    mov bl, bh                      ; BL = value 0: none of them
    or bl, dl
    or bl, dh
    not bl
%assign PL 0
%rep 4
    mov al, bl
    and al, [pm_sel+PL]
    mov ah, bh
    and ah, [pm_sel+4+PL]
    or al, ah
    mov ah, dl
    and ah, [pm_sel+8+PL]
    or al, ah
    mov ah, dh
    and ah, [pm_sel+12+PL]
    or al, ah
    mov [di+PL*128], al
%assign PL PL+1
%endrep
    add di, 4
    dec ch
    jnz .row
    pop si                          ; ...and into the cache, valid from now
    push si
    push ds
    pop es
    mov di, [pm_t]
    mov byte [pm_tvalid+di], 1
    mov cl, 5
    shl di, cl
    add di, PM_CACHE
    push es
    mov es, [pm_shseg]
    mov dx, 4
.spl:
    mov cx, 8
.sr:
    movsb
    add si, 3
    loop .sr
    add si, 128 - 32
    dec dx
    jnz .spl
    pop es
    pop di
    pop dx
    mov es, [ar_rseg]
.tdone:
    inc dx
    cmp dx, [pm_ctw]
    jb .tcol
    inc bp
    cmp bp, [pm_cth]
    jb .trow
    call pm_overlay
    jmp pm_put

; pm_overlay - every sprite over the box, 7 first so that 0 is on top: each
; row's masks (words, bit 15 the left pixel) shifted to the box's place, the
; planes cut through the row's opaque mask and its colours ORed in
pm_overlay:
    mov ax, [pm_ctx]                ; the box in pixels: X0, Y0..Y1
    mov cl, 3
    shl ax, cl
    mov [pm_bx0], ax
    mov ax, [pm_cty]
    shl ax, cl
    mov [pm_by0], ax
    mov dx, [pm_cth]
    shl dx, cl
    add ax, dx
    cmp ax, 272                     ; sprites are clipped to the playfield's
    jbe .ycl                        ;   rows 16-271 (MAME's spriteclip)
    mov ax, 272
.ycl:
    mov [pm_by1], ax
    mov ax, [pm_ctw]
    shl ax, cl
    add ax, [pm_bx0]
    mov [pm_bx1], ax
    mov word [pm_s], 7 * PMS_SIZE
.spr:
    mov bx, [pm_s]
    mov ax, [pm_cur+bx+PMS_X]
    cmp ax, 0x8000
    je .next
    cmp ax, [pm_bx1]                ; across: does it touch the box at all?
    jge .next
    add ax, 16
    cmp ax, [pm_bx0]
    jle .next
    mov dx, [pm_cur+bx+PMS_Y]       ; down: rows max(y, Y0, 16) ..
    mov [pm_sy], dx                 ;   min(y + 16, Y1)
    mov cx, dx
    cmp cx, [pm_by0]
    jge .y0
    mov cx, [pm_by0]
.y0:
    cmp cx, 16
    jge .y0b
    mov cx, 16
.y0b:
    mov [pm_iy0], cx
    add dx, 16
    cmp dx, [pm_by1]
    jle .y1
    mov dx, [pm_by1]
.y1:
    cmp dx, cx
    jle .next
    mov [pm_iy1], dx
    mov ax, [pm_cur+bx+PMS_X]       ; its shift into the box's 32 bits
    sub ax, [pm_bx0]
    mov [pm_sh], ax
    mov al, [pm_cur+bx+PMS_PAL]     ; its selectors and opacity, widened
    mov ah, 0                       ;   to words: 16 selectors then 4
    mov cl, 4
    shl ax, cl
    add ax, PM_PSEL
    mov si, ax
    mov cx, 16
    mov di, pm_selw
.sel:
    mov al, [es:si]
    mov ah, al
    mov [di], ax
    inc si
    add di, 2
    loop .sel
    mov al, [pm_cur+bx+PMS_PAL]
    mov ah, 0
    shl ax, 1
    shl ax, 1
    add ax, PM_POPQ
    mov si, ax
    mov cx, 4
    mov di, pm_opqw
.opq:
    mov al, [es:si]
    mov ah, al
    mov [di], ax
    inc si
    add di, 2
    loop .opq
    mov al, [pm_cur+bx+PMS_ATTR]    ; its masks: (code x 2 + y flip) x 96
    mov [pm_flip], al
    mov ah, 0
    shr al, 1
    rcl ah, 1                       ; AH = the y flip (bit 0)
    shr al, 1                       ; AL = the code
    shl al, 1
    or al, ah                       ; code x 2 + flip, < 128
    mov ah, 96
    mul ah
    add ax, PM_SMASK
    mov [pm_sbase], ax
    mov bx, [pm_iy0]
.row:
    mov ax, bx                      ; r = y - sy, the x flip reversing rows
    sub ax, [pm_sy]
    test byte [pm_flip], 2
    jz .rf
    neg ax
    add ax, 15
.rf:
    mov si, ax
    shl ax, 1
    add ax, si
    shl ax, 1
    add ax, [pm_sbase]
    mov si, ax
    push bx
    mov bx, [es:si]                 ; BX, CX, DX: values 1, 2, 3
    mov cx, [es:si+2]
    mov dx, [es:si+4]
    mov ax, bx                      ; value 0: none of them
    or ax, cx
    or ax, dx
    not ax
    mov [pm_m], ax
    mov [pm_m+2], bx
    mov [pm_m+4], cx
    mov [pm_m+6], dx
    pop bx                          ; the screen row again: BX held masks
    push bx
    call pm_sprrow
    pop bx
    inc bx
    cmp bx, [pm_iy1]
    jb .row
.next:
    sub word [pm_s], PMS_SIZE
    jns .spr
    ret

; pm_sprrow - pm_m's four value masks for one row onto pm_pl's row BX (a
; screen row). Each mask is shifted ONCE into the box's 32 pixels (pm_sm);
; the opaque mask is the OR of those whose colour is not 0, the planes the
; OR of those whose colour has the plane's bit, all in words (pm_opqw,
; pm_selw: FFFFh or 0), and each plane is cut through the opaque mask
pm_sprrow:
    sub bx, [pm_by0]                ; the box row's bytes: pm_pl + row x 4
    shl bx, 1
    shl bx, 1
    add bx, pm_pl
%assign V 0
%rep 4
    mov ax, [pm_m+V*2]
    call pm_shift
    mov [pm_sm+V*4], dx
    mov [pm_sm+V*4+2], ax
%assign V V+1
%endrep
    mov dx, [pm_sm]                 ; NOT O, the box's two words
    and dx, [pm_opqw]
    mov ax, [pm_sm+2]
    and ax, [pm_opqw]
%assign V 1
%rep 3
    mov cx, [pm_sm+V*4]
    and cx, [pm_opqw+V*2]
    or dx, cx
    mov cx, [pm_sm+V*4+2]
    and cx, [pm_opqw+V*2]
    or ax, cx
%assign V V+1
%endrep
    not dx
    not ax
    mov [pm_o], dx
    mov [pm_o+2], ax
%assign PL 0
%rep 4
    mov dx, [pm_sm]                 ; plane PL: the masks whose colour has it
    and dx, [pm_selw+PL*2]
    mov ax, [pm_sm+2]
    and ax, [pm_selw+PL*2]
%assign V 1
%rep 3
    mov cx, [pm_sm+V*4]
    and cx, [pm_selw+(V*4+PL)*2]
    or dx, cx
    mov cx, [pm_sm+V*4+2]
    and cx, [pm_selw+(V*4+PL)*2]
    or ax, cx
%assign V V+1
%endrep
    mov cx, [bx+PL*128]
    and cx, [pm_o]
    or cx, dx
    mov [bx+PL*128], cx
    mov cx, [bx+PL*128+2]
    and cx, [pm_o+2]
    or cx, ax
    mov [bx+PL*128+2], cx
%assign PL PL+1
%endrep
    ret

; pm_shift - AX = a row's 16 pixels (bit 15 the left one): DX, AX = the
; box's left and right words with them at pixel pm_sh, in MEMORY order
; (the left pixel's byte first). Two shifts by CL, no loop: a count of 16
; empties a word on every 86 (the 286 and up mask the count to 31)
pm_shift:
    mov cx, [pm_sh]
    or cx, cx
    js .left
    cmp cx, 16
    jae .right
    mov dx, ax                      ; left = M >> n, right = M << (16 - n)
    shr dx, cl
    neg cl
    add cl, 16
    shl ax, cl
    jmp short .done
.right:
    sub cx, 16
    shr ax, cl
    xor dx, dx
    jmp short .done
.left:
    neg cx
    shl ax, cl
    mov dx, ax
    xor ax, ax
.done:
    xchg dh, dl                     ; to memory order
    xchg ah, al
    ret

; pm_put - pm_pl's box onto the screen, a plane at a time - one map-mask
; write each - every byte doubled across, every even row doubled down, and
; ONLY the bytes that differ from the shadow of the glass (part 7, a byte a
; plane for 8 portrait pixels): a sprite that moved a pixel changes a few
; bytes of its box, and VGA memory, across the ISA bus, was a quarter of a
; 286's time in play when the box was written whole (MEASURED, 86Box).
; DS = the shadow here; our own data is reached through CS.
PM_SHPL     equ 288 * 28            ; the shadow: plane p at p x 8064
PM_CACHE    equ 0x8000              ; ...and above it, each screen tile's 32
                                    ;   plane bytes as last composed (part 7)
pm_put:
    mov ax, [pm_cth]                ; rows = tiles down x 8
    mov cl, 3
    shl ax, cl
    mov [pm_rows], ax
    mov es, [ar_vidseg]
    mov word [pm_pp], pm_pl
    mov word [pm_shp], 0
    push ds
    mov ds, [cs:pm_shseg]
    mov cl, 1                       ; CL = the plane's map-mask bit
.plane:
    mov dx, 0x3C4
    mov al, 2
    mov ah, cl
    out dx, ax
    push cx
    mov si, [cs:pm_pp]
    mov ax, [cs:pm_cty]             ; the shadow's first row: (cty x 8) x
    mov cl, 3                       ;   28 + ctx, in this plane - a 16-bit
    shl ax, cl                      ;   multiply: rows reach 287, and an
    mov bx, ax                      ;   8-bit one wrapped the bottom rows
    mov cx, 28                      ;   onto the top
    mul cx
    add ax, [cs:pm_ctx]
    add ax, [cs:pm_shp]
    mov [cs:pm_shr], ax
    shl bx, 1                       ; the first screen row's table entry
    add bx, pm_yoff
    mov bp, [cs:pm_ctx]             ; the box's byte across: 2 a tile
    shl bp, 1
    xor dx, dx
.prow:
    mov di, [cs:bx]
    add di, bp
    mov cx, [cs:pm_ctw]
    push si
    push bx
    mov bx, [cs:pm_shr]
.pb:
    mov al, [cs:si]
    inc si
    cmp al, [bx]
    je .same
    mov [bx], al
    push bx
    mov bl, al
    mov bh, 0
    shl bx, 1
    mov ax, [cs:ar_dbl+bx]
    pop bx
    mov [es:di], ax
    test dl, 1                      ; an even row is two screen rows
    jnz .same
    mov [es:di+80], ax
.same:
    inc bx
    add di, 2
    loop .pb
    pop bx
    pop si
    add si, 4
    add bx, 2
    add word [cs:pm_shr], 28
    inc dx
    cmp dx, [cs:pm_rows]
    jb .prow
    pop cx
    add word [cs:pm_pp], 128
    add word [cs:pm_shp], PM_SHPL
    shl cl, 1
    cmp cl, 16
    jb .plane
    pop ds
    mov dx, 0x3C4                   ; all planes again, for whoever is next
    mov ax, 0x0F02
    out dx, ax
    ret
