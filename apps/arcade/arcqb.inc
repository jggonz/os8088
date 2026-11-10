; =============================================================================
; os8088 - apps/arcade/arcqb.inc - Gottlieb's Q*bert board, the host's half
; (ARCADE-SPEC 10). Included by arcade.asm.
;
; The board's CPU is an 8088 and its program runs NATIVELY, in G, a 64KB
; claim of ours (apps/arcade/arcn88.asm is the door): the program at A000h
; as the board has it, its RAM, sprite RAM, video RAM and palette at the
; same addresses as on the board. So the host does not see a write as it
; happens; it reads what the game left, once a frame, while the game waits
; for its NMI:
;
;   the inputs   written into 5800h, 5801h and 5804h before every run
;   the outputs  read at 5808-580Fh, where qbert.patch moved the writes
;   the palette  5000-501Fh compared with the last, into the DAC on change
;   the tiles    3800-3BBFh compared with a copy: a tile is redrawn where
;                its byte changed (there is no write list - nothing traps
;                a native store)
;   the sprites  3000-30FFh, 64 of them, each one's box redrawn when it moved
;
; THE PICTURE (ARCADE-SPEC 10.4). VGA only, 640x480x16: the board has exactly
; 16 colours, so a pixel's value is its DAC register and a palette change is
; 16 DAC writes, never a redraw. The 240 x 256 portrait screen is drawn twice
; as wide and 15/8 as tall - 480 x 480, centred - in boxes of up to 4 x 4
; tiles COMPOSED in planes in RAM (the tiles, the sprites over them, and the
; tiles again where the background has priority), then put on the glass a
; plane at a time. No pixel is written twice (PERFORMANCE.md rule 2).
; =============================================================================

QB_TILES    equ 0x6000              ; ROM part (qb_build): a tile's planes
QB_FULL     equ 0x8840              ;   a sprite code's byte: 1 = has pixels
QB_ROMKB    equ 35                  ;   0000-893F
QB_SPRKB    equ 40                  ; the sprite part: 256 x 160 bytes
QB_PROGLEN  equ 0x6000              ;   the program, for A000h
QB_XB       equ 10                  ; the picture's left edge: byte 10 (80 px)
QB_DSW      equ 0x02                ; the DIPs: MAME's defaults (the kicker on)
QB_IN1REST  equ 0x40                ; IN1 at rest: the service switch is off
QB_SP0      equ 0x1FF0 - 20         ; the game's first stack: the frame that
                                    ;   n88_resume pops (7 registers, FLAGS,
                                    ;   IP, CS)
QBS_X       equ 0                   ; a sprite's state: its portrait column,
QBS_Y       equ 2                   ;   row (words, signed), and its code
QBS_CODE    equ 4
QBS_SIZE    equ 6
QB_NSPR     equ 64
QB_SKIPMAX  equ 20                  ; frames the picture may wait, in a row
QB_LATE     equ 240                 ; frames the host may fall behind before
                                    ;   it drops one (BD_LATE): on an 8088 a
                                    ;   box can take two and the whole screen
                                    ;   (an attract mode's change of scene)
                                    ;   about 107 (MEASURED), and the game's
                                    ;   own frames catch up at 8k cycles each
QB_SHPL     equ 256 * 30            ; the shadow of the glass: a byte a plane
                                    ;   for 8 portrait pixels, plane p at
                                    ;   p x 7680 (part 7's rule: write only
                                    ;   what differs, ARCADE-SPEC 10.4)

; -----------------------------------------------------------------------------
; qb_on / qb_off - Q*bert picked, and given back: G, the game's 64KB, and
; the shadow, with its sound board's memory past it when it can be heard
; -----------------------------------------------------------------------------
qb_on:
    mov word [qb_rclaim], 0
    mov word [qb_sclaim], 0
    mov word [qbs_seg], 0
    mov ax, 64
    call OSAPI_MEM_CLAIM
    jc .r
    mov [qb_gseg], dx
    mov [qb_gcs], dx                ; G as CODE: itself, but on kern_mega
%ifdef PKG_MEGA
    call qb_gcode
%endif
    mov dx, [qb_gseg]               ; MOVABLE (SPEC.md 66): qb_gmv
    mov ax, qb_gmv
    call OSAPI_MEM_MOVABLE
    xor ax, ax                      ; ...and the shadow of the glass, with
    call qbs_want                   ;   the sound board's 32KB past it when
    or ax, ax                       ;   it can be heard here: one claim, not
    jz .shadow                      ;   two (ARCADE-SPEC 13.3)
    mov ax, QB_SHPL * 4 / 1024 + QBS_KB
    call OSAPI_MEM_CLAIM
    jc .shadow
    mov [qb_shseg], dx
    mov ax, QB_SHPL * 4 / 16        ; the 6503's memory: a segment of its
    call ar_sub                     ;   own (ARCADE-SPEC 14 rule 1)
    cmp dx, [qb_shseg]              ; (refused - no LDT entry: not heard)
    je .silent
    mov [qbs_seg], dx
    jmp short .shmv
.shadow:
    mov ax, QB_SHPL * 4 / 1024      ; (the shadow alone: the board silent)
    call OSAPI_MEM_CLAIM
    jc .noshadow
    mov [qb_shseg], dx
.silent:
    mov byte [qbs_chips], 0         ; (and no chips kept for it: qb_build)
.shmv:
    mov dx, [qb_shseg]
    mov ax, qb_shmv
    call OSAPI_MEM_MOVABLE
    clc                             ; (refused - a pool claim on kern_mega,
    ret                             ;   which moves by a rebase - it stays)
.noshadow:
    mov dx, [qb_gseg]
    call OSAPI_MEM_FREE
    stc
.r:
    ret
%ifdef PKG_MEGA
; qb_gcode - DX = G: [qb_gcs] = the selector to run the game's code through
; (ARCADE-MEGA-PLAN M3). A conventional claim is a pair already, its code half
; DX - 8 (OSAPI_MEM_CODESEL); one in the pool above 1MB is a lone data
; selector, and SEG_SUBC makes a pair over it that moves and goes with it.
; Refused, G itself stays: each transfer then faults and is completed, slow
; but right. Kept: all but AX, BX, CX, flags
qb_gcode:
    push dx
    call OSAPI_MEM_CODESEL
    jnc .have
    mov bl, SEG_SUBC
    xor ax, ax
    xor cx, cx
    call OSAPI_SEG
    jc .r
    mov ax, dx
    sub ax, 8
.have:
    mov [qb_gcs], ax
.r:
    pop dx
    ret
%endif

; qb_yields - each yield in G (qbpatch.inc, kind 2) pointed at the N88 core
; as it is NOW: [ar_core+2] and its header's AH_YIELD. qb_reset's, and
; ar_partsmv's when the N88 part moves (SPEC.md 20.12.12). No G, nothing.
; Clobbers AX, BX, DX, SI, DI, ES
qb_yields:
    mov ax, [qb_gseg]
    or ax, ax
    jz .r
    mov es, ax
    mov ax, [ar_core+2]             ; the N88 core: its segment, and its
    mov [qb_n88], ax                ;   yield's offset from its header
    push ds
    mov ds, ax
    mov ax, [ARC_HDR+AH_YIELD]
    pop ds
    mov [qb_n88y], ax
    mov dx, [qb_n88]                ; DX:BX = the yield
    mov bx, [qb_n88y]
    mov si, qb_patches
.y:
    lodsb
    cmp al, 0xFF
    je .r
    mov di, [si]
    add si, 3
    cmp al, 2
    jne .y
    mov [es:di+1], bx               ; call far n88:yield
    mov [es:di+3], dx
    jmp .y
.r:
    ret

; qb_gmv - G moved (SPEC.md 66; never on kern_mega, where a move is a
; rebase): BX = was, DX = now. G holds no segment of its own but one: the
; game's stack, between runs, keeps the far return into its yield - CS = G
; (the first run's frame too). That, the view's two words and ours. A play
; claims nothing, so G moves only between runs
qb_gmv:
    mov [qb_gseg], dx
    mov [qb_gcs], dx
    mov es, [ar_vseg]
    mov [es:NV_GSEG], dx
    mov [es:NV_GCS], dx
    mov si, [es:NV_GSP]
    mov es, dx
    cmp [es:si+18], bx              ; (FLAGS and seven registers, then IP:CS)
    jne .r
    mov [es:si+18], dx
.r:
    ret

; qb_rmv / qb_smv / qb_shmv - Q*bert's built ROM part (ar_rseg), sprite part
; (ar_aseg) and shadow of the glass moved: the word and the copy it is banked
; in - and with the shadow, the 6503's memory past it. BX = was, DX = now
qb_rmv:
    mov [qb_rclaim], dx
    cmp [ar_rseg], bx
    jne .r
    mov [ar_rseg], dx
.r:
    ret
qb_smv:
    mov [qb_sclaim], dx
    cmp [ar_aseg], bx
    jne .r
    mov [ar_aseg], dx
.r:
    ret
qb_shmv:
    mov [qb_shseg], dx
    cmp word [qbs_seg], 0           ; (the board heard: qb_on)
    je .r
    mov ax, QB_SHPL * 4 / 16        ; the 6503's memory is m6_seg, read at
    call ar_sub                     ;   each m6_run
    mov [qbs_seg], dx
    mov [m6_seg], dx
.r:
    ret

qb_off:
    call qbs_off                    ; (its sound board's memory, ARCADE-SPEC 13)
    mov dx, [qb_gseg]
    call OSAPI_MEM_FREE
    mov dx, [qb_shseg]
    call OSAPI_MEM_FREE
    mov word [qb_gseg], 0
    mov word [qb_gcs], 0
    mov word [qb_shseg], 0
    mov si, qb_rclaim               ; ...and the parts qb_build made
    call .free
    mov si, qb_sclaim
.free:
    xor dx, dx
    xchg dx, [si]
    or dx, dx
    jz .r
    call OSAPI_MEM_FREE
.r:
    ret

; -----------------------------------------------------------------------------
; qb_build - Q*bert's BD_BUILD (docs/plans/ARCADE-RELEASE-PLAN.md 2.4): the
; user's image (ar_rimg: the program at 0, the tiles at 6000h, the sprite
; planes at 8000h, MSB first) made into what arcqbgfx.py made on the host
; before - the ROM part (ar_rseg: the program with qbert.patch applied, the
; tiles turned to planes and opaque bytes, each sprite code's FULL byte) and
; the sprite part (ar_aseg: the sprites turned, five words a row). The patch
; is qbpatch.inc: arcqbgfx.py checked every line against the pinned ROM, and
; the user's image is that ROM, by its CRC32. BX = the board. CF=1: no
; memory, a toast. Kept: BX
; -----------------------------------------------------------------------------
qb_build:
    push bx
    mov ax, QB_ROMKB
    cmp byte [qbs_chips], 0         ; (+4KB: the sound chips, kept past the
    je .rom                         ;   image when qb_on made the board room
    add ax, QBS_CHIPKB              ;   - ARCADE-SPEC 13.3)
.rom:
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [qb_rclaim], dx
    mov [ar_rseg], dx
    mov ax, QB_SPRKB
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [qb_sclaim], dx
    mov [ar_aseg], dx
    mov es, dx                      ; both made from nothing
    xor di, di
    xor ax, ax
    mov cx, QB_SPRKB * 512
    cld
    rep stosw
    mov es, [ar_rseg]
    mov di, QB_PROGLEN
    mov cx, (QB_ROMKB * 1024 - QB_PROGLEN) / 2
    rep stosw
    push ds                         ; the program, then the patch
    mov ds, [ar_rimg]
    xor si, si
    xor di, di
    mov cx, QB_PROGLEN / 2
    rep movsw
    pop ds
    mov si, qb_patches
.p:
    lodsb
    cmp al, 0xFF
    je .patched
    mov di, [si]
    sub di, 0xA000
    mov cl, [si+2]
    xor ch, ch
    add si, 3
    cmp al, 1
    jb .nops
    je .up
    mov word [es:di], 0x009A        ; call far 0000:0000 / nop - qb_reset
    mov word [es:di+2], 0           ;   writes the pointer
    mov word [es:di+4], 0x9000
    jmp .p
.nops:
    mov al, 0x90
    rep stosb
    jmp .p
.up:
    add byte [es:di], 8             ; 5800-5807h to 5808-580Fh
    jmp .p
.patched:
    push ds
    mov ds, [ar_rimg]               ; DS = the image, ES = the ROM part
    call qbb_tiles
    mov es, [cs:ar_aseg]
    call qbb_sprites
    pop ds
    call qbs_keep                   ; (the sound chips, before the image goes)
    call ar_freeimg
    mov dx, [qb_rclaim]             ; built: both MOVABLE (qb_rmv, qb_smv)
    mov ax, qb_rmv
    call OSAPI_MEM_MOVABLE
    mov dx, [qb_sclaim]
    mov ax, qb_smv
    call OSAPI_MEM_MOVABLE
    pop bx
    clc
    ret
.nomem:
    mov si, ar_s_nomem
    call ar_toast
    pop bx
    stc                             ; (qb_off gives back what was made)
    ret

; qbb_tiles - each tile's portrait (row r, column c) is the board's (x = 7 -
; r, y = c): the nibble at 6000h + tile x 32 + y x 4 + x / 2, the high one at
; an even x. To ES:6000h + tile x 40: plane p's 8 rows at p x 8, then the
; 8 rows' opaque bytes. DS = the image
qbb_tiles:
    mov bp, 0x6000                  ; BP = the tile's 32 bytes
    mov di, QB_TILES                ; DI = its 40
.tile:
    xor dx, dx                      ; DL = r
.row:
    mov bl, 7                       ; x = 7 - r: its byte across, its nibble
    sub bl, dl
    mov cl, bl
    shr bl, 1
    xor bh, bh
    add bx, bp
    mov ch, 0x80                    ; the column's bit, c = 0 first
.col:
    mov al, [bx]
    test cl, 1
    jnz .low
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.low:
    and al, 15
    jz .z
    push bx
    mov bl, dl
    xor bh, bh
    or [es:di+bx+32], ch            ; opaque
    mov ah, al
%assign P 0
%rep 4
    shr ah, 1
    jnc .p%[P]
    or [es:di+bx+P*8], ch
.p%[P]:
%assign P P+1
%endrep
    pop bx
.z:
    add bx, 4                       ; y + 1
    shr ch, 1
    jnz .col
    inc dl
    cmp dl, 8
    jb .row
    add bp, 32
    add di, 40
    cmp di, QB_TILES + 256 * 40
    jb .tile
    ret

; qbb_sprites - each sprite's portrait (row r, column c) is the board's (x =
; 15 - r, y = c): plane q's bit at 8000h + q x 2000h + code x 32 + y x 2 + x
; / 8 (q 0 the value's bit 3). To ES:code x 160 + r x 10: the opaque word,
; then planes 0-3, bit 15 the left pixel; the ROM part's FULL byte for a code
; with any. DS = the image
qbb_sprites:
    xor bp, bp                      ; BP = code x 32
    xor di, di                      ; DI = code x 160
.code:
    xor dx, dx                      ; DH = r
.row:
    mov cl, 15                      ; x = 15 - r: its byte across (BX) and
    sub cl, dh                      ;   its bit (DL)
    mov bl, cl
    and cl, 7
    mov dl, 0x80
    shr dl, cl
    shr bl, 1
    shr bl, 1
    shr bl, 1
    xor bh, bh
    add bx, bp
    add bx, 0x8000                  ; plane 0's byte for y = 0
    ; each plane's 16 columns gathered into a word, column 0 the top bit:
    ; AND the pixel's bit, NEG puts "any" in CF, RCL takes it in - unrolled,
    ; because this is 262,144 pixels a pick and the loop was most of 7.5 s
    ; on the 8088 (MEASURED, MartyPC)
%assign Q 0
%rep 4
%assign C 0
%rep 16
    mov al, [bx+Q*0x2000+C*2]
    and al, dl
    neg al
    rcl si, 1
%assign C C+1
%endrep
    mov [es:di+(4-Q)*2], si         ; plane 3 - Q is the value's bit 3 - Q
%assign Q Q+1
%endrep
    mov ax, [es:di+2]               ; the opaque word: any plane
    or ax, [es:di+4]
    or ax, [es:di+6]
    or ax, [es:di+8]
    mov [es:di], ax
    add di, 10
    inc dh
    cmp dh, 16
    jb .row
    sub di, 160                     ; any opaque: the FULL byte
    xor ax, ax
    mov cx, 16
.f:
    or ax, [es:di]
    add di, 10
    loop .f
    or ax, ax
    jz .empty
    push es
    mov es, [cs:ar_rseg]
    mov si, bp
    shr si, 1
    shr si, 1
    shr si, 1
    shr si, 1
    shr si, 1
    mov byte [es:QB_FULL+si], 1
    pop es
.empty:
    add bp, 32
    cmp bp, 0x2000
    jb .code
    ret

; -----------------------------------------------------------------------------
; qb_reset - the board powered on: G cleared, the program at A000h, each
; yield pointed at the N88 core, the game's first stack, and the inputs at
; rest. Every register kept.
; -----------------------------------------------------------------------------
qb_reset:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov es, [qb_gseg]
    xor di, di
    xor ax, ax
    mov cx, 0x8000
    cld
    rep stosw
    push ds
    mov ds, [ar_rseg]
    xor si, si
    mov di, 0xA000
    mov cx, QB_PROGLEN / 2
    rep movsw
    pop ds
    call qb_yields                  ; each yield: the N88 core's far pointer
    mov es, [qb_gseg]
    mov di, QB_SP0                  ; the first run's frame: no registers,
    xor ax, ax                      ;   FLAGS with IF clear, and A000h - the
    mov cx, 7                       ;   reset's own `jmp B259h`
    rep stosw
    mov ax, 0x0002
    stosw
    mov ax, 0xA000
    stosw
    mov ax, [qb_gcs]                ; (its CS: G's code half on kern_mega)
    stosw
    mov byte [es:0x5800], QB_DSW
    mov byte [es:0x5801], QB_IN1REST
    mov es, [ar_vseg]
    mov ax, [qb_gseg]
    mov [es:NV_GSEG], ax
    mov ax, [qb_gcs]
    mov [es:NV_GCS], ax
    mov word [es:NV_GSP], QB_SP0
    mov byte [es:NV_NMI], 0
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; qb_irq - the frame's NMI (61.42 Hz): the next run starts in the game's own
; handler. An NMI is never masked.
; -----------------------------------------------------------------------------
qb_irq:
    mov es, [ar_vseg]
    mov byte [es:NV_NMI], 1
    clc
    ret

; -----------------------------------------------------------------------------
; qb_setup - in the bracket: VGA's 640x480x16, the row table, the palette to
; be loaded, and the whole picture. SI = the window. CF=1 refused
; -----------------------------------------------------------------------------
qb_setup:
    mov bx, si
    mov ch, 0               ; CH = 0: BX's window's display (R12)
    call OSAPI_FSX_CAPS
    test ax, 1 << FSXM_VGA12
    jnz .vga
    mov word [ar_err], ar_s_qbvga
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
    xor bx, bx                      ; the row table: row r at screen row
    mov di, qb_yoff                 ;   r x 15 / 8, so 256 rows fill 480
.row:
    mov ax, bx
    mov cx, 15
    mul cx
    mov cl, 3
    shr ax, cl
    mov cx, 80
    mul cx
    add ax, QB_XB
    mov [di], ax
    add di, 2
    inc bx
    cmp bx, 257
    jb .row
    mov es, [qb_shseg]              ; the shadow: the mode set left the
    xor di, di                      ;   glass black
    mov cx, QB_SHPL * 2
    xor ax, ax
    cld
    rep stosw
    mov byte [qb_skips], 0
    mov byte [qb_dnext], 0
    mov byte [qb_palok], 0          ; the DAC from the board's palette
    call qb_palette
    mov es, [qb_gseg]               ; the background's priority now
    mov al, [es:0x580B]
    and al, 1
    mov [qb_pri], al
    mov byte [qb_srok], 0
    call qb_sprites
    call qb_whole
    clc
    ret

; -----------------------------------------------------------------------------
; qb_frame - after the NMI was delivered: the keys for the next run, then the
; picture as the last run left it
; -----------------------------------------------------------------------------
qb_frame:
    call qb_keys
    call pit_now                    ; already late for the next frame: the
    sub ax, [ar_due]                ;   picture waits (the GAME keeps every
    sbb dx, [ar_due+2]              ;   frame), QB_SKIPMAX in a row at most
    js .draw                        ;   (ARCADE-SPEC 10.5)
    cmp byte [qb_skips], QB_SKIPMAX
    jae .draw
    inc byte [qb_skips]
    ret
.draw:
    mov byte [qb_skips], 0
    call qb_palette
    mov es, [qb_gseg]               ; the background's priority (5803h bit 0,
    mov al, [es:0x580B]             ;   at 580Bh): a change redraws it all
    and al, 1
    cmp al, [qb_pri]
    je .tiles
    mov [qb_pri], al                ; all of it again AT ONCE: a few times a
    call qb_sprites                 ;   minute, and a box left for later
    jmp qb_whole                    ;   would show the old order exactly where
                                    ;   a sprite crosses a tile (107 frames on
                                    ;   the 8088, inside QB_LATE)
.tiles:
    call qb_sprites                 ; qb_cur from the board
    pushf
    call qb_tiles                   ; the boxes whose tiles changed, marked
    call qb_boxes                   ;   ...and a few of them drawn
    popf
    jc .still
    jmp qb_movers
.still:
    ret

; qb_keys - IN1 (the coin, the starts) and IN4 (the joystick), active high
qb_keys:
    mov bl, QB_IN1REST
    mov al, [ar_pbits]
    test al, AR_COIN
    jz .nocoin
    or bl, 0x04
.nocoin:
    test al, AR_START1
    jz .no1
    or bl, 0x01
.no1:
    test al, AR_START2
    jz .no2
    or bl, 0x02
.no2:
    xor bh, bh
    mov si, qb_keymap
.k:
    lodsw
    or al, al
    jz .done
    call OSAPI_KEY_DOWN
    jnc .k
    or bh, ah
    jmp .k
.done:
    mov es, [qb_gseg]
    mov byte [es:0x5800], QB_DSW
    mov [es:0x5801], bl
    mov [es:0x5804], bh
    ret

qb_keymap:                          ; scancode, IN4 bit. The stick is turned
    db KSC_RIGHT, 0x01              ;   45 degrees: MAME's Right is down and
    db KSC_LEFT, 0x02               ;   right, Left up and left, Up up and
    db KSC_UP, 0x04                 ;   right, Down down and left - and the
    db KSC_DOWN, 0x08               ;   keypad's corners are the four ways
    db 0x51, 0x01                   ;   as they look: 3 (PgDn) down-right,
    db 0x47, 0x02                   ;   7 (Home) up-left,
    db 0x49, 0x04                   ;   9 (PgUp) up-right,
    db 0x4F, 0x08                   ;   1 (End) down-left
    db 0, 0

; qb_palette - 5000-501Fh, when it changed, into the DAC: each colour's even
; byte holds green (bits 4-7) and blue, its odd byte red, four bits each
qb_palette:
    push ds
    pop es
    push ds
    mov ds, [qb_gseg]
    mov si, 0x5000
    mov di, qb_pal
    mov cx, 16
    cmp byte [es:qb_palok], 0
    je .load
    cld
    repe cmpsw
    je .same
.load:
    mov byte [es:qb_palok], 1
    mov si, 0x5000
    mov di, qb_pal
    mov cx, 16
    rep movsw
    pop ds
    xor bx, bx
.dac:
    mov dx, 0x3C8
    mov al, [ar_acmap+bx]
    out dx, al
    inc dx
    shl bx, 1
    mov al, [qb_pal+bx+1]           ; red
    call qb_dac6
    out dx, al
    mov al, [qb_pal+bx]             ; green
    mov cl, 4
    shr al, cl
    call qb_dac6
    out dx, al
    mov al, [qb_pal+bx]             ; blue
    call qb_dac6
    out dx, al
    shr bx, 1
    inc bx
    cmp bx, 16
    jb .dac
    ret
.same:
    pop ds
    ret
; qb_dac6 - AL's low four bits (a resistor weight) as a DAC's six
qb_dac6:
    and al, 15
    mov ah, al
    shl al, 1
    shl al, 1
    shr ah, 1
    shr ah, 1
    or al, ah
    ret

; qb_sprites - the 64 sprites' state, from the board, into qb_cur, and the
; active list; CF=1 when nothing in sprite RAM changed since. MAME:
; sx = [n+1] - 4, sy = [n] - 13, code = [n+2] XOR FFh; turned, column sy and
; row 240 - sx
qb_sprites:
    push ds                         ; the board's sprite RAM as it was last
    pop es                          ;   read: nothing to do (CF=1). Sprites
    push ds                         ;   that stand still are most frames
    mov ds, [qb_gseg]
    mov si, 0x3000
    mov di, qb_srcopy
    mov cx, QB_NSPR * 2
    cld
    cmp byte [es:qb_srok], 0
    je .read
    repe cmpsw
    jne .read
    pop ds
    stc
    ret
.read:
    mov si, 0x3000
    mov di, qb_srcopy
    mov cx, QB_NSPR * 2
    rep movsw
    pop ds
    mov byte [qb_srok], 1
    mov di, qb_cur
    xor si, si
    push ds
    mov ds, [qb_gseg]
.s:
    mov al, [si+0x3000]
    mov ah, 0
    sub ax, 13
    stosw
    mov al, [si+0x3001]
    mov ah, 0
    mov dx, 244
    sub dx, ax
    xchg ax, dx
    stosw
    mov al, [si+0x3002]
    not al
    mov ah, 0
    stosw
    add si, 4
    cmp si, QB_NSPR * 4
    jb .s
    pop ds
    mov es, [ar_rseg]               ; the ACTIVE ones, in order: a code with
    xor bx, bx                      ;   pixels, somewhere on the screen. The
    mov di, qb_act                  ;   board parks the others at (0,0) with
.a:                                 ;   a blank code, and each box would
    mov si, [qb_cur+bx+QBS_CODE]    ;   test all 64 (MEASURED, 8088)
    and si, 0xFF
    cmp byte [es:si+QB_FULL], 0
    je .skip
    mov ax, [qb_cur+bx+QBS_X]
    cmp ax, -15
    jl .skip
    cmp ax, 240
    jge .skip
    mov ax, [qb_cur+bx+QBS_Y]
    cmp ax, -15
    jl .skip
    cmp ax, 248
    jge .skip
    mov [di], bx
    add di, 2
.skip:
    add bx, QBS_SIZE
    cmp bx, QB_NSPR * QBS_SIZE
    jb .a
    mov [qb_actend], di
    clc
    ret

; qb_tiles - every tile whose byte changed since the copy: its 4 x 4 box
; marked, for qb_boxes. The board's screen is rows 0-29 of its 32 x 32
; map, 960 bytes
qb_tiles:
    push ds
    pop es
    xor bx, bx
.scan:
    push ds
    mov ds, [qb_gseg]
    mov si, 0x3800
    add si, bx
    mov di, qb_vcopy
    add di, bx
    mov cx, 960                     ; (BX counts bytes, and is even:
    sub cx, bx                      ;   the words left)
    shr cx, 1
    cld
    repe cmpsw
    pop ds
    je .scanned
    sub si, 2                       ; the word that differs: its two bytes
    mov bx, si
    sub bx, 0x3800
    push ds
    mov ds, [qb_gseg]
    mov ax, [si]
    pop ds
    mov dx, [qb_vcopy+bx]
    mov [qb_vcopy+bx], ax
    cmp al, dl
    je .hi
    call .mark
.hi:
    inc bx
    cmp ah, dh
    je .next
    call .mark
.next:
    inc bx
    cmp bx, 960
    jb .scan
.scanned:
    ret
.mark:                              ; BX = a changed landscape index: its
    push bx                         ;   portrait tile is column index / 32,
    push cx                         ;   row 31 - (index AND 31), and its box
    mov ax, bx                      ;   row / 4 x 8 + column / 4
    mov cl, 7
    shr ax, cl                      ; column / 4
    and bx, 31
    neg bx
    add bx, 31
    shr bx, 1
    shr bx, 1                       ; row / 4
    mov cl, 3
    shl bx, cl
    add bx, ax
    mov byte [qb_dirty+bx], 1
    pop cx
    pop bx
    ret

; qb_boxes - the boxes whose tiles changed, composed, taken in turn from
; where the last frame stopped, UNTIL THE NEXT FRAME IS DUE (one at least).
; A change of scene marks every box: a 286 draws them all in a frame, and an
; 8088 over as many frames as it needs instead of in one draw of 1.7 s that
; the game's own time could not wait for (MEASURED: the board fell behind
; and dropped, ARCADE-SPEC 10.5)
qb_boxes:
    mov cx, 64
.b:
    mov bl, [qb_dnext]
    xor bh, bh
    inc byte [qb_dnext]
    and byte [qb_dnext], 63
    cmp byte [qb_dirty+bx], 0
    je .next
    mov byte [qb_dirty+bx], 0
    push cx
    mov ax, bx                      ; across: (box AND 7) x 4, 4 wide but
    and ax, 7                       ;   the last (two tiles: 30 across)
    shl ax, 1
    shl ax, 1
    mov cx, 30
    sub cx, ax
    cmp cx, 4
    jbe .w
    mov cx, 4
.w:
    shr bx, 1                       ; down: (box / 8) x 4
    and bx, 0x1C
    mov dx, 4
    call qb_compose
    call pit_now                    ; the next frame due: stop there
    sub ax, [ar_due]
    sbb dx, [ar_due+2]
    pop cx
    jns .out
.next:
    loop .b
.out:
    ret

; qb_whole - the whole screen, in 4 x 4 tiles, the copy of the tiles first
qb_whole:
    push ds
    pop es
    mov di, qb_dirty                ; (nothing left to draw after it)
    mov cx, 32
    xor ax, ax
    cld
    rep stosw
    push ds
    mov ds, [qb_gseg]
    mov si, 0x3800
    mov di, qb_vcopy
    mov cx, 480
    cld
    rep movsw
    pop ds
    xor bx, bx
.all:
    xor ax, ax
.across:
    push ax
    push bx
    mov cx, 30
    sub cx, ax
    cmp cx, 4
    jbe .w
    mov cx, 4
.w:
    mov dx, 4
    call qb_compose
    pop bx
    pop ax
    add ax, 4
    cmp ax, 30
    jb .across
    add bx, 4
    cmp bx, 32
    jb .all
    mov si, qb_cur                  ; and every sprite as it is now drawn
    mov di, qb_old
    mov cx, QB_NSPR * QBS_SIZE / 2
    push ds
    pop es
    rep movsw
    ret

; -----------------------------------------------------------------------------
; qb_movers - every sprite that moved or changed: the box it left and the box
; it is in now, then remembered as drawn
; -----------------------------------------------------------------------------
qb_movers:
    xor bx, bx
.spr:
    mov ax, [qb_cur+bx+QBS_X]
    cmp ax, [qb_old+bx+QBS_X]
    jne .moved
    mov ax, [qb_cur+bx+QBS_Y]
    cmp ax, [qb_old+bx+QBS_Y]
    jne .moved
    mov al, [qb_cur+bx+QBS_CODE]
    cmp al, [qb_old+bx+QBS_CODE]
    je .next
.moved:
    push bx
    lea si, [qb_old+bx]
    lea di, [qb_cur+bx]
    call qb_spritebox
    pop bx
    mov ax, [qb_cur+bx+QBS_X]
    mov [qb_old+bx+QBS_X], ax
    mov ax, [qb_cur+bx+QBS_Y]
    mov [qb_old+bx+QBS_Y], ax
    mov ax, [qb_cur+bx+QBS_CODE]
    mov [qb_old+bx+QBS_CODE], ax
.next:
    add bx, QBS_SIZE
    cmp bx, QB_NSPR * QBS_SIZE
    jb .spr
    ret

; qb_sprbox - the tile box a sprite's state covers. SI -> the state. CF=1
; nothing on the screen; else AX = the first tile across, BX = down, CX =
; across, DX = down. Sprites are clipped to rows 0-247 (MAME's x >= 8)
qb_sprbox:
    mov ax, [si+QBS_X]
    mov dx, [si+QBS_Y]
    cmp ax, -15
    jl .none
    cmp ax, 240
    jge .none
    cmp dx, -15
    jl .none
    cmp dx, 248
    jge .none
    mov cx, ax                      ; across: max(x, 0) .. min(x + 15, 239)
    add cx, 15
    cmp cx, 239
    jle .x1
    mov cx, 239
.x1:
    or ax, ax
    jns .x0
    xor ax, ax
.x0:
    mov bx, dx                      ; down: max(y, 0) .. min(y + 15, 247)
    add bx, 15
    cmp bx, 247
    jle .y1
    mov bx, 247
.y1:
    or dx, dx
    jns .y0
    xor dx, dx
.y0:
    push cx                         ; ...in tiles
    mov cl, 3
    shr ax, cl
    shr bx, cl
    shr dx, cl
    pop cx
    shr cx, 1
    shr cx, 1
    shr cx, 1
    sub cx, ax
    inc cx
    sub bx, dx
    inc bx
    xchg bx, dx                     ; BX = the first row, DX = rows
    clc
    ret
.none:
    stc
    ret

; qb_spritebox - SI -> where it was, DI -> where it is. One box for both when
; they fit 4 x 4 tiles, else each its own
qb_spritebox:
    push di
    call qb_sprbox
    jc .newonly
    mov [qb_ox], ax
    mov [qb_oy], bx
    add cx, ax
    mov [qb_ox1], cx
    add dx, bx
    mov [qb_oy1], dx
    pop si
    call qb_sprbox
    jc .oldonly
    add cx, ax
    add dx, bx
    cmp ax, [qb_ox]
    jle .u1
    mov ax, [qb_ox]
.u1:
    cmp bx, [qb_oy]
    jle .u2
    mov bx, [qb_oy]
.u2:
    cmp cx, [qb_ox1]
    jge .u3
    mov cx, [qb_ox1]
.u3:
    cmp dx, [qb_oy1]
    jge .u4
    mov dx, [qb_oy1]
.u4:
    sub cx, ax
    sub dx, bx
    cmp cx, 4
    ja .apart
    cmp dx, 4
    ja .apart
    jmp qb_compose
.apart:
    push si
    call qb_sprbox
    call qb_compose
    pop si
.oldonly:
    mov ax, [qb_ox]
    mov bx, [qb_oy]
    mov cx, [qb_ox1]
    sub cx, ax
    mov dx, [qb_oy1]
    sub dx, bx
    jmp qb_compose
.newonly:
    pop si
    call qb_sprbox
    jc .out
    jmp qb_compose
.out:
    ret

; -----------------------------------------------------------------------------
; qb_compose - AX = the first tile across, BX = down, CX = tiles across (1-4),
; DX = tiles down (1-4): the tiles, every sprite over them and, when the
; background has priority, the tiles' own pixels again over those, composed
; in qb_pl - plane p's row y at p x 128 + y x 4, a byte a tile across - then
; put on the screen. Clobbers every register but DS.
; -----------------------------------------------------------------------------
qb_compose:
    push es                         ; (counted: V_NCELL, for the
    mov es, [ar_vseg]               ;   instruments)
    inc word [es:V_NCELL]
    pop es
    mov [qb_ctx], ax
    mov [qb_cty], bx
    mov [qb_ctw], cx
    mov [qb_cth], dx
    push ds
    pop es
    xor bp, bp                      ; BP = the tile row in the box
.trow:
    xor dx, dx                      ; DX = the tile across in the box
.tcol:
    mov ax, [qb_ctx]                ; the board's index: its row is our
    add ax, dx                      ;   column, its column 31 - our row
    mov cl, 5
    shl ax, cl
    add ax, 31
    sub ax, [qb_cty]
    sub ax, bp
    mov bx, ax
    push ds
    mov ds, [qb_gseg]
    mov al, [bx+0x3800]
    pop ds
    mov ah, 40                      ; its planes: the tile x 40
    mul ah
    add ax, QB_TILES
    mov si, ax
    mov bx, bp                      ; its place: row (tile row x 8) x 4 +
    mov cl, 5                       ;   across, in qb_pl and in qb_top
    shl bx, cl
    add bx, dx
    lea di, [qb_pl+bx]
    push ds
    mov ds, [ar_rseg]
    mov ah, 4
.tpl:
    mov cx, 8
.tr:
    movsb
    add di, 3
    loop .tr
    add di, 128 - 32
    dec ah
    jnz .tpl
    lea di, [qb_top+bx]             ; the opaque bytes after them, for the
    mov cx, 8                       ;   background's priority
.to:
    movsb
    add di, 3
    loop .to
    pop ds
    inc dx
    cmp dx, [qb_ctw]
    jb .tcol
    inc bp
    cmp bp, [qb_cth]
    jb .trow
    cmp byte [qb_pri], 0
    je .over
    mov si, qb_pl                   ; the tiles, kept for the second pass
    mov di, qb_tl
    mov cx, 256
    cld
    rep movsw
.over:
    call qb_overlay
    cmp byte [qb_pri], 0
    je .put
    call qb_bgover
.put:
    jmp qb_put

; qb_bgover - the background over the sprites: every plane's byte cut through
; the tiles' opaque byte and the tiles' own ORed in
qb_bgover:
    mov ax, [qb_cth]
    mov cl, 3
    shl ax, cl
    mov cx, ax                      ; rows
    xor si, si
.row:
    push cx
    mov cx, [qb_ctw]
    push si
.b:
    mov ah, [qb_top+si]
    mov al, ah
    not al
%assign PL 0
%rep 4
    mov dl, [qb_pl+si+PL*128]
    and dl, al
    or dl, [qb_tl+si+PL*128]
    mov [qb_pl+si+PL*128], dl
%assign PL PL+1
%endrep
    inc si
    loop .b
    pop si
    add si, 4
    pop cx
    loop .row
    ret

; qb_overlay - every ACTIVE sprite over the box, 0 first so that 63 is on top: each
; row's opaque mask and four plane words shifted to the box's place, each
; plane cut through the mask and the sprite's ORed in
qb_overlay:
    mov ax, [qb_ctx]
    mov cl, 3
    shl ax, cl
    mov [qb_bx0], ax
    mov dx, [qb_ctw]
    shl dx, cl
    add ax, dx
    mov [qb_bx1], ax
    mov ax, [qb_cty]
    shl ax, cl
    mov [qb_by0], ax
    mov dx, [qb_cth]
    shl dx, cl
    add ax, dx
    cmp ax, 248                     ; no sprite below row 247
    jbe .ycl
    mov ax, 248
.ycl:
    mov [qb_by1], ax
    mov word [qb_s], qb_act
.spr:
    mov bx, [qb_s]
    cmp bx, [qb_actend]
    jae .done
    mov bx, [bx]
    mov ax, [qb_cur+bx+QBS_X]
    cmp ax, [qb_bx1]
    jge .next
    add ax, 16
    cmp ax, [qb_bx0]
    jle .next
    mov dx, [qb_cur+bx+QBS_Y]       ; rows max(y, Y0) .. min(y + 16, Y1)
    mov [qb_sy], dx
    mov cx, dx
    cmp cx, [qb_by0]
    jge .y0
    mov cx, [qb_by0]
.y0:
    add dx, 16
    cmp dx, [qb_by1]
    jle .y1
    mov dx, [qb_by1]
.y1:
    cmp dx, cx
    jle .next
    mov [qb_iy1], dx
    mov ax, [qb_cur+bx+QBS_X]       ; its shift into the box's 32 pixels
    sub ax, [qb_bx0]
    mov [ar_sh], ax
    mov al, [qb_cur+bx+QBS_CODE]    ; its rows: the code x 160
    mov ah, 160
    mul ah
    mov [qb_sbase], ax
    mov bx, cx
.row:
    mov ax, bx                      ; the sprite's row: r x 10
    sub ax, [qb_sy]
    mov si, ax
    shl ax, 1
    shl ax, 1
    add ax, si
    shl ax, 1
    add ax, [qb_sbase]
    mov si, ax
    push bx
    call qb_sprrow
    pop bx
    inc bx
    cmp bx, [qb_iy1]
    jb .row
.next:
    add word [qb_s], 2
    jmp .spr
.done:
    ret

; qb_sprrow - the sprite row at aux:SI (opaque, planes 0-3) onto qb_pl's row
; BX (a screen row); a row with nothing opaque costs its test only
qb_sprrow:
    mov es, [ar_aseg]
    mov ax, [es:si]
    or ax, ax
    jz .none
    sub bx, [qb_by0]                ; the box row's bytes: qb_pl + row x 4
    shl bx, 1
    shl bx, 1
    add bx, qb_pl
    call ar_shift                   ; NOT the opaque mask, in the box
    not dx
    not ax
    mov [qb_o], dx
    mov [qb_o+2], ax
%assign PL 0
%rep 4
    mov ax, [es:si+2+PL*2]
    call ar_shift
    mov cx, [bx+PL*128]
    and cx, [qb_o]
    or cx, dx
    mov [bx+PL*128], cx
    mov cx, [bx+PL*128+2]
    and cx, [qb_o+2]
    or cx, ax
    mov [bx+PL*128+2], cx
%assign PL PL+1
%endrep
.none:
    ret

; qb_put - qb_pl's box onto the screen, ROW BY ROW: a row's place and height
; worked out once, then each plane's bytes - one map-mask write each - doubled
; across, and ONLY those that differ from the shadow of the glass (a sprite
; that moved a pixel changes a few bytes of its box). A row the table makes
; two screen rows is written once and COPIED down through the latches (write
; mode 1: a byte read and a byte written move all four planes), only when
; something in it changed. ES = the shadow here, and REPE CMPSB passes
; over what it already has. On the 8088 the row's own overhead was the cost, four times a
; row when the loop went a plane at a time (MEASURED, MartyPC: 75k cycles for
; a 3 x 3 box, ARCADE-SPEC 10.5).

%macro QB_PUTPL 1                   ; plane %1 of the row: REPE CMPSB runs
    mov dx, 0x3C4                   ;   over what the shadow already has
    mov ax, ((1 << %1) << 8) | 2
    out dx, ax
    mov si, [qb_pp]
    add si, %1 * 128
    mov di, [qb_shr]
    add di, %1 * QB_SHPL
    mov cx, [qb_ctw]
%%cmp:
    repe cmpsb
    je %%done                       ; the rest the same
    mov al, [si-1]                  ; a byte that differs: to the shadow,
    mov [es:di-1], al               ;   and doubled to the glass at the
    mov bx, [qb_ctw]                ;   row's byte (ctw - CX - 1) x 2
    sub bx, cx
    dec bx
    shl bx, 1
    add bx, [qb_di0]
    xor ah, ah
    mov bp, ax
    shl bp, 1
    mov dx, [ds:bp+ar_dbl]
    push es
    mov es, [ar_vidseg]
    mov [es:bx], dx
    pop es
    mov byte [qb_chg], 1
    jcxz %%done
    jmp %%cmp
%%done:
%endmacro

qb_put:
    mov ax, [qb_cth]                ; rows = tiles down x 8
    mov cl, 3
    shl ax, cl
    mov [qb_rleft], ax
    mov ax, [qb_cty]                ; the first row: its table entry, and
    shl ax, cl                      ;   its shadow (row x 30 + ctx)
    mov bx, ax
    mov cx, 30
    mul cx
    add ax, [qb_ctx]
    mov [qb_shr], ax
    shl bx, 1
    add bx, qb_yoff
    mov [qb_yr], bx
    mov ax, [qb_ctx]                ; the box's byte across: 2 a tile
    shl ax, 1
    mov [qb_bx2], ax
    mov word [qb_pp], qb_pl
    mov es, [qb_shseg]
    cld
.row:
    mov bx, [qb_yr]                 ; the row's screen offset, and whether
    mov di, [bx]                    ;   it is one screen row or two
    add di, [qb_bx2]
    mov [qb_di0], di
    mov ax, [bx+2]
    sub ax, [bx]
    mov [qb_two], al                ; (80: one; 160: two)
    mov byte [qb_chg], 0
    QB_PUTPL 0
    QB_PUTPL 1
    QB_PUTPL 2
    QB_PUTPL 3
    cmp byte [qb_chg], 0
    je .next
    cmp byte [qb_two], 80
    jbe .next
    mov dx, 0x3C4                   ; the row below: the latches, all four
    mov ax, 0x0F02                  ;   planes a byte
    out dx, ax
    mov dx, 0x3CE
    mov ax, 0x0105                  ; write mode 1
    out dx, ax
    push es
    mov es, [ar_vidseg]
    mov di, [qb_di0]
    mov cx, [qb_ctw]
    shl cx, 1
.copy:
    mov al, [es:di]
    mov [es:di+80], al
    inc di
    loop .copy
    pop es
    mov ax, 0x0005                  ; write mode 0 again
    out dx, ax
.next:
    add word [qb_pp], 4
    add word [qb_yr], 2
    add word [qb_shr], 30
    dec word [qb_rleft]
    jz .done
    jmp .row
.done:
    mov dx, 0x3C4                   ; all planes again, for whoever is next
    mov ax, 0x0F02
    out dx, ax
    ret
