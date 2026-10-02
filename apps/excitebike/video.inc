; Fullscreen bracket, loading screen, adapter art and the Wave 1 placeholder
; scene (SPEC.md 102.1, 102.2).  VGA runs FSXM_VGA0D (320x200x16 planar,
; 8 pixels a byte a plane); CGA runs FSXM_CGA320.  One 40x25 tile layout on
; both.  Nothing here writes a pixel twice in a frame loop - the placeholder is
; drawn once, each tile a run of byte moves, each plane once (the game's scroll
; engine replaces this in Wave 2).
;
; Cycle notes (8088, 4.77 MHz): a VGA tile is 4 planes x 8 `movsb` + `add` +
; `loop` = ~1,250 clk, the whole 960-tile grid ~1.2M clk (0.25 s, once); a CGA
; tile is 4 x (`movsw`,`lodsw`,store,`add`) = ~330 clk.  The 16-bit checksum is
; ~45 clk a byte: 12 KB is 0.1 s at load, never in a frame.

; -----------------------------------------------------------------------------
; xb_launch - the desktop side of Enter / Alt+Enter / a click on START
; -----------------------------------------------------------------------------
xb_launch:
    cmp byte [xb_frontplay], 0
    je .out
    mov byte [xb_error], XB_ERR_NONE
    mov byte [xb_nomem], 0
    mov bx, [xb_win]
    xor ax, ax
    call OSAPI_WM_TIMER
    mov bx, [xb_win]
    mov ax, xb_fullscreen
    mov cx, FSXF_FASTTICK
    call OSAPI_FSX_RUN
    ; Back on the desktop the kernel repaints every window whole.  The splash
    ; claim never left, so that repaint is memory-to-glass and the heap is
    ; exactly where it was before the visit.
    mov bx, [xb_win]
    mov ax, 1
    call OSAPI_WM_TIMER
.out: ret

; -----------------------------------------------------------------------------
; xb_fullscreen - the bracket proc.  DS = CS = our segment, ES = KERNEL_SEG.
; -----------------------------------------------------------------------------
xb_fullscreen:
    cld
    OS88_ALTENTER_SEED
    ; Let the chord that got us here release before a BIOS video mode call can
    ; mask its break.  The wait is the bracket's, never the scheduler's.
.release:
    mov al, KSC_ENTER
    call OSAPI_KEY_DOWN
    jnc .mode
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jmp .release
.mode:
    mov bx, [xb_win]
    call OSAPI_FSX_CAPS
    mov byte [xb_cga], 0
    mov byte [xb_herc], 0
    mov word [xb_wcols], XB_COLS
    mov word [xb_xlo], XB_EDGE      ; a bike shows from 22 pixels off the left edge to 22 off the right:
    mov word [xb_xspan], XB_COLS * 8 + XB_EDGE - 1   ; the clipping blitters draw the part that is on (SPEC.md 102.7.4)
    mov word [xa_ahead], XA_AHEAD
    mov word [xb_xoff], 0
    mov al, FSXM_VGA0D
    cmp dl, VID_VGA
    je .set
    inc byte [xb_cga]
    mov al, FSXM_CGA320
    cmp dl, VID_HERC
    jne .set
    inc byte [xb_herc]                      ; Hercules: the CGA back end's shape at two card pixels a
    mov al, FSXM_HERC                       ; game pixel (SPEC.md 102.7); 45 columns of 16 pixels
    mov word [xb_wcols], XH_COLS
    mov word [xb_xspan], XH_COLS * 8 + XB_EDGE - 1
    mov word [xa_ahead], XA_AHEAD + (XH_COLS - XB_COLS) * 8
    mov word [xb_xoff], 4                   ; the 40-cell menus sit two cells in from the left
.set:
    push ds
    pop es
    mov di, xb_fsi
    call OSAPI_FSX_MODE
    jc xb_fsmodefail
    mov byte [xb_fs], 1
    mov byte [xb_gshift], 3
    mov word [xb_cell], 1
    cmp byte [xb_cga], 0
    je .shifted
    mov byte [xb_gshift], 4
    mov word [xb_cell], 2
.shifted:
    cmp byte [xb_herc], 0
    je .cleared
    call xh_crtc                    ; show 200 lines, not 348 (herc.inc)
.cleared:
    call xb_screen_clear
    call xb_loading                ; resident bitmap: up before the first disk read
    call xb_gfx_load
    jc xb_fsassetfail
    call xb_font_copy
    call xb_race_open              ; back end: dictionaries, sprites (CF: refused)
    jc xb_fsassetfail
    call xu_start                  ; the sound: FM claim or the speaker (audio.inc)
    cmp byte [xb_noflow], 0
    jne .raw
    call xb_game                   ; the menus and the races (flow.inc)
    call xb_race_close
    jmp xb_fsdone
.raw:
    mov al, [xb_track]             ; the harness's raw loop: the course, then the race
    call xb_track_load             ; the course: sets the theme
    jc .rawfail
    call xb_palette
    call xb_race_begin
    call xu_race_begin
    call xb_race                   ; the scroll test track, until Esc / Alt+Enter
    call xb_race_close
    jmp xb_fsdone
.rawfail:
    call xb_race_close
    jmp xb_fsassetfail
xb_fsmodefail:
    mov byte [xb_error], XB_ERR_MODE
    ret
xb_fsassetfail:
    mov byte [xb_error], XB_ERR_FILES
    cmp byte [xb_nomem], 0          ; a refused CLAIM is a machine with too little memory, not a missing
    je xb_fsdone                    ; file: the two get different sentences (SPEC.md 102.8.3)
    mov byte [xb_error], XB_ERR_MEM
xb_fsdone:
    call xu_close
    call xb_spr_free
    call xb_gfx_free
    mov byte [xb_fs], 0
    ret

; -----------------------------------------------------------------------------
; the screen, cleared once: 4 planes of 8,000 bytes (VGA) or both CGA banks
; -----------------------------------------------------------------------------
xb_screen_clear:
    SAVE
    mov es, [xb_fsi+FSI_SEG]
    xor di, di
    xor ax, ax
    mov cx, 8192                    ; CGA: 16 KB
    cmp byte [xb_herc], 0
    je .notherc
    mov cx, 16384                   ; Hercules: 32 KB, four banks
    jmp .clear
.notherc:
    cmp byte [xb_cga], 0
    jne .clear
    mov dx, 3c4h
    mov ax, 0f02h                   ; map mask: all four planes
    out dx, ax
    xor ax, ax
    mov cx, 4000                    ; 8,000 bytes a plane
.clear:
    rep stosw
    RESTORE
    ret

; -----------------------------------------------------------------------------
; the loading screen: two centred lines from glyph rows compiled into the
; package (exbtables.inc), so it needs no file and no claim
; -----------------------------------------------------------------------------
xb_loading:
    mov si, exb_ld_0
    mov al, 10
    call xb_ld_line
    mov si, exb_ld_1
    mov al, 12
    call xb_ld_line
    ret

; SI = a string of glyph indices ending 255 (254 = a blank cell), AL = tile row
xb_ld_line:
    SAVE
    mov es, [xb_fsi+FSI_SEG]
    xor ah, ah
    mov cx, 320
    cmp byte [xb_herc], 0
    je .rowb
    mov cx, 180                     ; a tile row is two 90-byte rows of four scan lines
.rowb:
    mul cx
    mov di, ax                      ; row * a tile row's bytes
    xor cx, cx
    mov bx, si
.len:
    cmp byte [bx], 255
    je .measured
    inc bx
    inc cx
    jmp .len
.measured:
    mov ax, 40
    sub ax, cx
    shr ax, 1                       ; first column, centred
    cmp byte [xb_cga], 0
    je .col
    shl ax, 1
.col:
    add di, ax
    add di, [xb_xoff]
.char:
    lodsb
    cmp al, 255
    je .out
    cmp al, 254
    je .blank
    xor ah, ah
    push si
    mov cl, [xb_gshift]
    shl ax, cl
    mov si, exb_ld_v
    cmp byte [xb_cga], 0
    je .src
    mov si, exb_ld_c
.src:
    add si, ax
    call xb_glyph
    pop si
.blank:
    add di, [xb_cell]
    jmp .char
.out:
    RESTORE
    ret

; one 8x8 glyph, DS:SI -> ES:DI (8 bytes VGA planar-all / 16 bytes CGA 2bpp);
; DI is left where it was
xb_glyph:
    cmp byte [xb_cga], 0
    jne .cga
%rep 8
    movsb
    add di, 39
%endrep
    sub di, 320
    ret
.cga:
    cmp byte [xb_herc], 0           ; (the VGA's path above pays nothing for the Hercules)
    jne .herc
%rep 4
    movsw                           ; the even line
    lodsw
    mov [es:di+1ffeh], ax           ; the odd line, in the second bank
    add di, 78
%endrep
    sub di, 320
    ret
.herc:
%rep 2
    movsw                           ; scan line 0 of the bank row
    lodsw
    mov [es:di+1ffeh], ax           ; 1, 2 and 3: the other three banks
    lodsw
    mov [es:di+3ffeh], ax
    lodsw
    mov [es:di+5ffeh], ax
    add di, 88
%endrep
    sub di, 180
    ret

; DS:SI = an ASCII string, AL = tile row, AH = tile column.  Lower case folds
; to upper; the font is the compiled 64 glyphs ASCII 32..95 (xb_font).
xb_puts:
    SAVE
    push ax
    mov es, [xb_fsi+FSI_SEG]
    pop ax
    mov bl, ah
    xor bh, bh
    xor ah, ah
    mov cx, 320
    cmp byte [xb_herc], 0
    je .rowb
    mov cx, 180
.rowb:
    mul cx
    mov di, ax
    cmp byte [xb_cga], 0
    je .col
    shl bx, 1
.col:
    add di, bx
    add di, [xb_xoff]
.char:
    lodsb
    or al, al
    jz .out
    cmp al, 'a'
    jb .fold
    cmp al, 'z'
    ja .fold
    sub al, 32
.fold:
    sub al, 32
    cmp al, 64
    jb .known
    xor al, al
.known:
    xor ah, ah
    push si
    mov cl, [xb_gshift]
    shl ax, cl
    add ax, xb_font
    mov si, ax
    call xb_glyph
    pop si
    add di, [xb_cell]
    jmp .char
.out:
    RESTORE
    ret

; -----------------------------------------------------------------------------
; the adapter's art file: claim, read, and prove it before following an offset
; -----------------------------------------------------------------------------
xb_gfx_load:
    SAVE
    mov bx, xb_gfxfiles
    cmp byte [xb_cga], 0
    je .pick
    add bx, 8
    cmp byte [xb_herc], 0
    je .pick
    add bx, 8
.pick:
    mov si, [bx]                    ; file name
    mov cx, [bx+2]                  ; its size
    mov bp, [bx+4]                  ; its compiled checksum
    mov di, [bx+6]                  ; the magic's second word
    mov [xb_gfxsize], cx
    mov ax, cx
    add ax, 1023
    mov cl, 10
    shr ax, cl                      ; KB
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [xb_gfxseg], dx
    mov es, dx
    push di
    push bp
    xor bx, bx
    xor dx, dx
    mov cx, [xb_gfxsize]
    call OSAPI_FILE_READ
    pop bp
    pop di
    jc .bad
    or dx, dx
    jnz .bad
    cmp ax, [xb_gfxsize]
    jne .bad
    cmp word [es:0], 'EX'
    jne .bad
    cmp word [es:2], di
    jne .bad
    cmp ax, [es:4]                  ; the length word
    jne .bad
    mov ax, [es:6]                  ; the stored checksum
    cmp ax, bp                      ; ...must be the one compiled into us
    jne .bad
    ; 16-bit byte sum of the whole file with the checksum word taken as zero
    push ds
    mov ds, [xb_gfxseg]
    mov cx, [cs:xb_gfxsize]
    xor si, si
    xor dx, dx
.sum:
    lodsb
    add dl, al
    adc dh, 0
    loop .sum
    pop ds
    mov ax, [es:6]
    xor bx, bx
    mov bl, al
    sub dx, bx
    mov bl, ah
    sub dx, bx
    cmp dx, ax
    jne .bad
    ; the record directory: 10 offsets
    mov cx, 10
    mov si, 12
    mov di, xb_recs
    push ds
    pop es                          ; ES = package for the stores
    push ds
    mov ds, [xb_gfxseg]
.rec:
    lodsw                           ; offset
    add si, 2                       ; skip the length word
    stosw
    loop .rec
    pop ds
    RESTORE
    clc
    ret
.nomem:
    mov byte [xb_nomem], 1
.bad:
    RESTORE
    stc
    ret

xb_gfx_free:
    push dx
    mov dx, [xb_gfxseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [xb_gfxseg], 0
.out:
    pop dx
    ret

; name, size, compiled checksum, magic's second word - VGA then CGA
xb_gfxfiles:
    dw xb_gfxname_v, EXB_GFX_VGA_SIZE, EXB_GFX_VGA_SUM, 'BV'
    dw xb_gfxname_c, EXB_GFX_CGA_SIZE, EXB_GFX_CGA_SUM, 'BC'
    dw xb_gfxname_h, EXB_GFX_HRC_SIZE, EXB_GFX_HRC_SUM, 'BH'
xb_gfxname_v: db '8BBV.GFX',0
xb_gfxname_c: db '8BBC.GFX',0
xb_gfxname_h: db '8BBH.GFX',0

; the font record -> xb_font: 64 glyphs, one 8-byte 1-bit row set (VGA) or a
; 16-byte 2-bit set (CGA), so text is a run of moves with DS = our segment
xb_font_copy:
    SAVE
    push ds
    pop es
    mov di, xb_font
    mov si, [xb_recs+EXBR_FONT*2]
    mov cx, 512
    cmp byte [xb_cga], 0
    je .copy
    mov cx, 1024
.copy:
    push ds
    mov ds, [xb_gfxseg]
    rep movsb
    pop ds
    RESTORE
    ret

; -----------------------------------------------------------------------------
; palette: VGA - an identity attribute map (pixel n = DAC n) and the current
; theme's 16 colours; CGA - the intensity/palette register of the profile
; -----------------------------------------------------------------------------
xb_palette:
    SAVE
    cmp byte [xb_cga], 0
    jne .cga
    mov dx, 3dah
    in al, dx                       ; reset the attribute flip-flop
    mov dx, 3c0h
    mov cx, 16
    xor al, al
.atc:
    out dx, al                      ; index n ...
    out dx, al                      ; ... = n
    inc al
    loop .atc
    mov al, 20h                     ; palette address source back on the CPU
    out dx, al
    mov al, [xb_theme]
    mov ah, 48
    mul ah
    add ax, [xb_recs+EXBR_PAL*2]
    mov si, ax
    push ds
    mov ds, [xb_gfxseg]
    mov dx, 3c8h
    xor al, al
    out dx, al
    inc dx
    mov cx, 48
.dac:
    lodsb
    out dx, al
    loop .dac
    pop ds
    mov byte [xb_bfl], 0            ; the DAC is the theme's own again
    jmp .out
.cga:
    cmp byte [xb_herc], 0
    jne .out                        ; a Hercules has no palette register (and 3D9h may be a colour card's)
    mov bl, [xb_profile]
    xor bh, bh
    add bx, [xb_recs+EXBR_PAL*2]
    push ds
    mov ds, [xb_gfxseg]
    mov al, [bx+32]                 ; the 3D9h value of the profile
    pop ds
    mov dx, 3d9h
    out dx, al
.out:
    RESTORE
    ret

; -----------------------------------------------------------------------------
; xb_banner - the BANNER FLASH (plan section 4.4: a DAC rewrite, not new art).  While the lap flash or
; GO! runs, DAC 13 - the blue of the banner plates and pennants (and the arrows) - alternates with the
; theme's orange (slot 12) every 8 steps, and is the theme's own again when it ends.  VGA only: the
; CGA and the Hercules have no palette to rewrite (a 3D9h flip would repaint the whole screen).
; Priced (counted from the code): the quiet frame is `cmp` + `mov` + `or` + `jz` + `cmp` with three memory
; operands = ~80-100 clk; a change adds six push/pop pairs, a `mul`, three `lodsb` and four `out` = a few
; hundred clk, plus a wait bounded at 400 reads of 3DAh for a blanking interval (normally under one
; scan line, ~32 us), so that the DAC write is not made mid-scan-out (clone VGAs sparkle on that).
; That is ~19 times in a 2.5 s flash.  The colour change itself still lands from the beam's line down,
; so for one 18 ms frame the plates above it may show the old colour - a strobe, and the point of the effect.
; -----------------------------------------------------------------------------
xb_banner:
    cmp byte [xb_cga], 0
    jne .out
    xor bl, bl
    mov ax, [xb_flash]
    or ax, ax
    jz .have
    test al, 8
    jz .have
    inc bl
.have:
    cmp bl, [xb_bfl]
    je .out
    mov [xb_bfl], bl
    push ax
    push bx
    push cx
    push dx
    push si
    push ds
    mov al, [xb_theme]
    mov cl, 48
    mul cl                          ; AX = theme * 48
    add ax, [xb_recs+EXBR_PAL*2]
    add ax, 13 * 3                  ; normal: the theme's slot 13 ...
    or bl, bl
    jz .rd
    sub ax, 3                       ; ... flashed: its slot 12
.rd:
    mov si, ax
    mov ds, [xb_gfxseg]
    lodsb
    mov bl, al
    lodsb
    mov bh, al
    lodsb
    mov cl, al                      ; the three components, so the writes below are back to back
    mov dx, 3dah
    push cx
    mov cx, 400                     ; bounded: wait for a blanking interval (bit 0) - some clone VGAs
.bl:                                ; sparkle on a DAC write made while they are scanning out
    in al, dx
    test al, 1
    loopz .bl
    pop cx
    mov dx, 3c8h
    mov al, 13
    out dx, al
    inc dx
    mov al, bl
    out dx, al
    mov al, bh
    out dx, al
    mov al, cl
    out dx, al
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; C: the next of the five course themes (VGA) or the other CGA profile
xb_palette_next:
    cmp byte [xb_cga], 0
    jne .cga
    inc byte [xb_theme]
    cmp byte [xb_theme], 5
    jb .apply
    mov byte [xb_theme], 0
    jmp .apply
.cga:
    xor byte [xb_profile], 1
.apply:
    jmp xb_palette

