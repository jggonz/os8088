; =============================================================================
; os8088 - apps/arcade/arcland.inc - Midway's LANDSCAPE 8080 boards: Gun
; Fight and Sea Wolf (docs/ARCADE-SPEC.md 4.5, 5.3, 8.3)
;
; The same board as Space Invaders with the monitor the right way up: a VRAM
; byte is 8 pixels ACROSS one line, bit 0 leftmost, 224 lines of 32 bytes.
; A cell is still 8 lines of one byte column (the write list's key is the
; same), so only the drawing differs: no transpose, each line its byte
; doubled across. And their own controls: Gun Fight's two gunmen, each a
; stick, an aim lever and a trigger; Sea Wolf's periscope and its lamps.
; =============================================================================

GF_AIMSTEP  equ 6                   ; frames an aim key moves the lever a notch
SW_STEP     equ 2                   ; ...and the periscope a position

; ar_vinitl - a landscape board's row table (4 bytes a VRAM line: where on
; the screen, and where doubled or FFFFh) and its cell drawer, by adapter.
; 256 x 224: VGA doubles it to 512 x 448, centred; Hercules 512 x 224 from
; row 62; CGA 512 x 200, a cell's lines 6 and 7 MERGED (OR) in all but four
; cells of every 28, as Space Invaders' rows are (ARCADE-SPEC 4.3)
ar_vinitl:
    xor bx, bx                      ; BX = the VRAM line
    mov di, ar_ytab
.row:
    mov al, [ar_fsi+FSI_MODE]
    cmp al, FSXM_VGA12
    je .vga
    cmp al, FSXM_HERC
    je .herc
    mov ax, bx                      ; CGA: the band, and the 8-line bands
    mov cl, 3                       ;   before it (3, 10, 17, 24 keep 8)
    shr ax, cl
    mov dx, ax
    mov cl, 7
    mul cl                          ; band x 7
    xchg ax, dx
    add ax, 3
    mov cl, 7
    div cl                          ; AL = (band + 3) / 7
    mov ah, 0
    add dx, ax                      ; DX = the band's first screen row
    mov ax, bx
    and ax, 7                       ; the line in the band
    push ax
    mov ax, bx
    mov cl, 3
    shr ax, cl
    mov cl, 7
    div cl                          ; AH = band mod 7
    cmp ah, 3
    pop ax
    je .eight
    cmp ax, 6
    jbe .eight
    mov ax, 6                       ; line 7 onto line 6's row
.eight:
    add ax, dx                      ; AX = the screen row
    mov dx, ax
    shr ax, 1
    mov cl, 80
    mul cl
    test dl, 1
    jz .ce
    add ax, 0x2000
.ce:
    add ax, 8
    stosw
    mov ax, 0xFFFF
    stosw
    jmp short .next
.herc:
    mov ax, bx
    add ax, 62
    mov dx, ax
    shr ax, 1
    shr ax, 1
    mov cl, 90
    mul cl
    and dx, 3
    mov cl, 13
    shl dx, cl
    add ax, dx
    add ax, 13
    stosw
    mov ax, 0xFFFF
    stosw
    jmp short .next
.vga:
    mov ax, bx
    shl ax, 1
    add ax, 16
    mov cx, 80
    mul cx
    add ax, 8
    stosw
    add ax, 80
    stosw
.next:
    inc bx
    cmp bx, 224
    jb .row
    mov byte [sw_lamp], 0xFF        ; the lamps drawn afresh
    mov word [ar_celf], ar_cellh_cga
    mov al, [ar_fsi+FSI_MODE]
    cmp al, FSXM_HERC
    jne .nh
    mov word [ar_celf], ar_cellh_herc
.nh:
    cmp al, FSXM_VGA12
    jne .out
    mov word [ar_celf], ar_cellh_vga
    mov dx, 0x3C8                   ; VGA: the game's one plane, white, as
    mov al, 1                       ;   Space Invaders' plane 0 without the
    out dx, al                      ;   cellophane
    inc dx
    mov al, 63
    out dx, al
    out dx, al
    out dx, al
    mov dx, 0x3C4
    mov ax, 0x0102
    out dx, ax
.out:
    ret

; ar_cellh_* - the landscape cell whose key is BX: its 8 lines, each its byte
; doubled across (ar_dblr: bit 0 leftmost), onto the screen. Clobbers all
; but DS
%macro AR_CELLH_LOAD 0              ; the 8 bytes pushed, line 7 first: SI =
    mov al, bh                      ;   line 0's ytab entry, BP = the cell's
    sub al, 0x24                    ;   two screen bytes across
    mov ah, 0
    mov cl, 5                       ; band x 8 lines x 4 bytes an entry
    shl ax, cl
    add ax, ar_ytab
    mov si, ax
    mov al, bl
    mov ah, 0
    shl ax, 1
    mov bp, ax
    mov es, [ar_vseg]
%assign k 7
%rep 8
    mov al, [es:bx+k*32]            ; line 0 pushed last, so popped first
    push ax
%assign k k-1
%endrep
    mov es, [ar_vidseg]
%endmacro
%macro AR_CELLH_ROW 0               ; the next line, doubled, into AX
    pop bx
    xor bh, bh
    shl bx, 1
    mov ax, [ar_dblr+bx]
%endmacro

ar_cellh_vga:
    ; STKBALANCE-OK: pushes its 8 lines and pops them all
    AR_CELLH_LOAD
%rep 8
    AR_CELLH_ROW
    mov di, [si]
    mov [es:di+bp], ax
    mov di, [si+2]
    mov [es:di+bp], ax
    add si, 4
%endrep
    ret

ar_cellh_herc:
    ; STKBALANCE-OK: pushes its 8 lines and pops them all
    AR_CELLH_LOAD
%rep 8
    AR_CELLH_ROW
    mov di, [si]
    mov [es:di+bp], ax
    add si, 4
%endrep
    ret

ar_cellh_cga:
    ; STKBALANCE-OK: pushes its 8 lines and pops them all
    AR_CELLH_LOAD
    mov dx, 0xFFFF                  ; DX, CX: the row last written, its word
    mov byte [ar_ccnt], 8
.l:
    AR_CELLH_ROW
    mov di, [si]
    cmp di, dx
    jne .w
    or ax, cx                       ; the same row as the line before: merged
.w:
    mov [es:di+bp], ax
    mov cx, ax
    mov dx, di
    add si, 4
    dec byte [ar_ccnt]
    jnz .l
    ret

; =============================================================================
; Gun Fight
; =============================================================================
; gf_keys - the two gunmen into IN 0 and IN 1, the coin and the start into
; IN 2 (MAME mw8080bw.cpp `gunfight`): each a stick (bits 0-3: up, down,
; left, right), an aim lever's notch (bits 4-6, through the encoder table,
; inverted) and a trigger (bit 7)
gf_keys:
    xor byte [gf_odd], 1            ; the sticks and levers every other frame:
    jz .read                        ;   fourteen keys are fourteen far calls,
    ret                             ;   ~1% of an 8088 each (MEASURED, Space
.read:                              ;   Invaders' nine were 9%)
    mov si, gf_keymap
    call ar_keytab                  ; AX = IN 0 and IN 1's stick and trigger
    push ax
    mov si, gf_aimkeys              ; each lever a notch every GF_AIMSTEP
    mov di, gf_aim                  ;   frames while its key is held
    mov cx, 2
.lever:
    push cx
    call ar_lever
    pop cx
    add di, 2
    loop .lever
    pop ax
    mov bl, [gf_aim]
    call gf_field
    or al, bl
    mov bl, [gf_aim+2]
    call gf_field
    or ah, bl
    mov es, [ar_vseg]
    mov [es:V_IN0], ax
    mov si, [ar_bx]
    mov al, [si+X_IN2]              ; IN 2: the DIPs, and what was typed
    test byte [ar_pbits], AR_COIN
    jz .nc
    or al, 0x40
.nc:
    test byte [ar_pbits], AR_START1 | AR_START2
    jz .ns
    or al, 0x80
.ns:
    mov [es:V_IN2], al
    ret

; gf_field - BL = a lever's notch 0-6: BL = its IN bits 4-6
gf_field:
    push ax
    mov bh, 0
    mov al, [gf_enc+bx]
    not al
    and al, 7
    mov cl, 4
    shl al, cl
    mov bl, al
    pop ax
    ret

gf_enc:     db 0x06, 0x02, 0x00, 0x04, 0x05, 0x01, 0x03

gf_keymap:                          ; scancode, port (0, 1), bit
    db 0x11, 0, 0x01                ; the left gunman: W A S D, G to fire
    db 0x1F, 0, 0x02
    db 0x1E, 0, 0x04
    db 0x20, 0, 0x08
    db 0x22, 0, 0x80
    db KSC_UP, 1, 0x01              ; the right gunman: the arrows, Space
    db KSC_DOWN, 1, 0x02
    db KSC_LEFT, 1, 0x04
    db KSC_RIGHT, 1, 0x08
    db KSC_SPACE, 1, 0x80
    db 0
gf_aimkeys:                         ; up, down: R F; Page Up, Page Down
    db 0x13, 0x21, 0, 6
    db 0x49, 0x51, 0, 6

; gf_sdec - Gun Fight's sound latch: its top four bits, inverted, a code -
; 1 a left shot, 2 a right shot, 3 a left hit, 4 a right hit (MAME
; mw8080bw_a.cpp) - as bits 0-3, the amplifier always on
gf_sdec:
    mov es, [ar_vseg]
    mov al, [es:V_SND3]
    not al
    mov cl, 4
    shr al, cl                      ; AL = the code
    mov ah, 0
    dec al
    cmp al, 3
    ja .none
    mov cl, al
    mov al, 1
    shl al, cl
    or al, 0x20
    ret
.none:
    mov al, 0x20
    ret

gf_cues:                            ; the speaker's
    dw 0x0004, ar_n_die, 0x0008, ar_n_die
    dw 0x0001, ar_n_shot, 0x0002, ar_n_shot, 0
gf_cbits:                           ; the stream's (arcsfx.py's clips)
    dw 0x0001
    db 15
    dw 0x0002
    db 15
    dw 0x0004
    db 16
    dw 0x0008
    db 16
    dw 0
gf_fnoise:                          ; FM: the bit, Hz, the level, frames a step
    dw 0x0004, 300
    db 15, 3
    dw 0x0008, 300
    db 15, 3
    dw 0x0001, 1400
    db 13, 1
    dw 0x0002, 1400
    db 13, 1
    dw 0

; =============================================================================
; Sea Wolf
; =============================================================================
; sw_keys - the periscope (IN 0 bits 0-4: a 30-position encoder, through its
; table, inverted), the trigger (bit 5) and the game time DIPs; the coin and
; the start into IN 1 (MAME `seawolf`)
sw_keys:
    mov si, sw_keymap
    call ar_keytab                  ; AL = the trigger
    push ax
    mov si, sw_scopekeys
    mov di, sw_scope
    call ar_lever
    pop ax
    mov si, [ar_bx]
    or al, [si+X_IN0]
    mov bl, [sw_scope]
    mov bh, 0
    mov ah, [sw_enc+bx]
    not ah
    and ah, 0x1F
    or al, ah
    mov ah, [si+X_IN1]
    test byte [ar_pbits], AR_COIN
    jz .nc
    or ah, 0x01
.nc:
    test byte [ar_pbits], AR_START1 | AR_START2
    jz .ns
    or ah, 0x02
.ns:
    mov es, [ar_vseg]
    mov [es:V_IN0], ax
    ret

sw_enc:     db 0x1e, 0x1c, 0x1d, 0x19, 0x18, 0x1a, 0x1b, 0x13, 0x12, 0x10
            db 0x11, 0x15, 0x14, 0x16, 0x17, 0x07, 0x06, 0x04, 0x05, 0x01
            db 0x00, 0x02, 0x03, 0x0b, 0x0a, 0x08, 0x09, 0x0d, 0x0c, 0x0e
sw_keymap:
    db KSC_SPACE, 0, 0x20
    db 0
sw_scopekeys:                       ; the periscope: Left, Right; 30 positions
    db KSC_LEFT, KSC_RIGHT, 0, 29

; sw_sdec - Sea Wolf's OUT 5: bits 0-4 the ship hit, the torpedo, the dive,
; the sonar, the mine hit, each started on its rise; the amplifier on
sw_sdec:
    mov es, [ar_vseg]
    mov al, [es:V_SND5]
    and al, 0x1F
    or al, 0x20
    mov ah, 0
    ret

sw_cues:
    dw 0x0001, ar_n_die, 0x0010, ar_n_invhit, 0x0002, ar_n_shot
    dw 0x0008, sw_n_sonar, 0x0004, lr_n_thrust, 0
sw_n_sonar: dw 1200
            db 3
            dw 0
sw_cbits:
    dw 0x0001
    db 2                            ; the ship hit
    dw 0x0002
    db 17                           ; a torpedo
    dw 0x0004
    db 18                           ; the dive
    dw 0x0008
    db 19                           ; the sonar
    dw 0x0010
    db 3                            ; a mine hit
    dw 0
sw_fsteps:                          ; FM: struck notes
    dw 0x0008, 1200, 0x0004, 220, 0
sw_fnoise:
    dw 0x0001, 300
    db 15, 5
    dw 0x0010, 900
    db 15, 2
    dw 0x0002, 600
    db 11, 2
    dw 0

; sw_lamps - the cabinet's torpedo lamps (OUT 2 bits 3..0, torpedo 1 at bit
; 3), READY (bit 4) and RELOAD (bit 5), as boxes under the picture on VGA
; and Hercules - a CGA picture has no room under it. Drawn when they change
sw_lamps:
    mov es, [ar_vseg]
    mov al, [es:V_LAMP2]
    cmp al, [sw_lamp]
    je .r
    mov [sw_lamp], al
    cmp byte [ar_fsi+FSI_MODE], FSXM_CGA640
    je .r
    mov es, [ar_vidseg]
    mov si, sw_boxes
.box:
    mov al, [si]
    or al, al
    jz .r
    mov dl, 0                       ; lit: filled; dark: an outline
    test [sw_lamp], al
    jz .dk
    mov dl, 0xFF
.dk:
    mov bl, [si+1]                  ; BX = the first byte across
    mov bh, 0
    mov dh, 0                       ; DH = the row
.y:
    mov al, dh
    mov ah, 0
    call sw_lamprow                 ; DI = the row's first byte
    add di, bx
    mov cl, [si+2]
    mov ch, 0                       ; CX = the width
    cmp dh, 0
    je .full
    cmp dh, 7
    je .full
    mov al, dl
    or al, 0x80
    stosb
    sub cx, 2
    mov al, dl
    rep stosb
    or al, 0x01
    stosb
    jmp short .nx
.full:
    mov al, 0xFF
    rep stosb
.nx:
    inc dh
    cmp dh, 8
    jb .y
    add si, 3
    jmp .box
.r:
    ret

; sw_lamprow - AL = a lamp row 0-7: DI = its first byte on the screen
sw_lamprow:
    push ax
    push cx
    push dx
    cmp byte [ar_fsi+FSI_MODE], FSXM_VGA12
    jne .h
    add ax, 468                     ; VGA: under the 448 rows from 16
    mov cx, 80
    mul cx
    mov di, ax
    jmp short .r
.h:
    add ax, 300                     ; Hercules: under the 224 rows from 62
    mov dx, ax
    shr ax, 1
    shr ax, 1
    mov cl, 90
    mul cl
    and dx, 3
    mov cl, 13
    shl dx, cl
    add ax, dx
    mov di, ax
.r:
    pop dx
    pop cx
    pop ax
    ret

sw_boxes:                           ; the bit, the first byte across, the width
    db 0x08, 10, 4                  ; torpedo 1..4
    db 0x04, 15, 4
    db 0x02, 20, 4
    db 0x01, 25, 4
    db 0x10, 33, 10                 ; READY
    db 0x20, 45, 10                 ; RELOAD
    db 0

; =============================================================================
; shared: keys and levers
; =============================================================================
; ar_keytab - SI = (scancode, port 0/1, bit) triples, 0-ended: AX = the bits
; of the keys held, port 0's in AL and port 1's in AH
ar_keytab:
    xor dx, dx
.k:
    lodsb
    or al, al
    jz .done
    mov cl, al
    lodsw                           ; AL = the port, AH = the bit
    xchg al, cl
    push ax
    push cx
    call OSAPI_KEY_DOWN
    pop cx
    pop ax
    jnc .k
    or cl, cl
    jnz .p1
    or dl, ah
    jmp .k
.p1:
    or dh, ah
    jmp .k
.done:
    mov ax, dx
    ret

; ar_lever - SI = (key down, key up, low, high), DI = a lever's word (the
; position, then frames to its next move): while a key is held it moves a
; notch every GF_AIMSTEP frames (SW_STEP for a 30-position one). SI advanced
ar_lever:
    mov al, [si]
    push ax
    call OSAPI_KEY_DOWN
    pop ax
    mov cl, 0xFF
    jc .held
    mov al, [si+1]
    push ax
    call OSAPI_KEY_DOWN
    pop ax
    mov cl, 1
    jc .held
    mov byte [di+1], 0              ; let go: the next press moves at once
    jmp short .r
.held:
    cmp byte [di+1], 0
    je .move
    dec byte [di+1]
    jmp short .r
.move:
    mov al, GF_AIMSTEP
    cmp byte [si+3], 7
    jb .step
    mov al, SW_STEP
.step:
    mov [di+1], al
    mov al, [di]
    add al, cl
    cmp al, [si+2]
    jl .r
    cmp al, [si+3]
    jg .r
    mov [di], al
.r:
    add si, 4
    ret
