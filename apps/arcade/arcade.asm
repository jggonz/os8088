; =============================================================================
; os8088 - apps/arcade/arcade.asm - ARCADE, an arcade-board emulator
; (docs/ARCADE-SPEC.md; the plan is docs/plans/ARCADE-EMU-PLAN.md)
;
; The first board is Midway's 8080 B&W board running Space Invaders. Its CPU
; is not interpreted: apps/arcade/arcaot.py translated the whole ROM to 8086
; code before the package was built, and that code is part 0 of this file.
; The package hosts it: it delivers the board's two interrupts on the PC's
; own clock, reads the keys, and draws what the game wrote to VRAM.
;
; THE GAME RUNS FULL SCREEN, inside OSAPI_FSX_RUN (SPEC.md 53): VGA 640x480
; with the cabinet's colour overlay, Hercules 720x348, or CGA 640x200. The
; window is the launcher and says which keys do what. Leaving with Esc keeps
; the machine where it stood, so Enter goes back into the same game.
;
; THE TIME (ARCADE-SPEC 5): RST 1 and RST 2 alternate every half frame by
; pit_now, not by counting 8080 cycles. Space Invaders keeps its game time in
; those two handlers and spins in its main loop between them, so a host too
; slow for the whole loop still runs the GAME at full speed - it just goes
; round the main loop fewer times a frame (MEASURED: 3.85 times at full
; speed, tools/arcsim.py stats).
; =============================================================================

%include "os88api.inc"
%include "os88mega.inc"                 ; the shipped build refuses under
                                        ;   CPU_F_PM: the code part is run
                                        ;   with a far jump into its segment
%include "arcdefs.inc"

    OS88_HEADER 'ARCADE', ar_entry, OS88_F_ICON | OS88_F_PARTS

; --- embedded 16x16 icon: an invader -----------------------------------------
    OS88_ICON16
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0x0000, 0x0000, 0x0820, 0x0440, 0x0FE0, 0x1BB0, 0x3FF8, 0x2FE8
    dw 0x2828, 0x06C0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
    OS88_ICON16_END

%include "os88parts.inc"

    OS88_PARTS_BEGIN 3
      OS88_PART OP_ASSET, OP_COMP                   ; 0 the translated code
      OS88_PART OP_ASSET, OP_ZERO, ARC_VIEW_KB      ; 1 the view
      OS88_PART OP_ASSET, OP_ZERO, ARC_AUX_KB       ; 2 aux: the write list
    OS88_PARTS_END

AR_CONT_W   equ 300
AR_CONT_H   equ 101
AR_HALF     equ 10020               ; PIT counts in half a frame: the board's
                                    ;   59.54 Hz (MAME mw8080bw: 4.992 MHz,
                                    ;   320 x 262), two interrupts a frame
AR_RUNLO    equ 2400                ; a run under 2 ms doubles the budget,
AR_RUNHI    equ 7200                ;   over 6 ms halves it (ARCADE-SPEC 5.2):
                                    ;   each run costs the host a PIT read and
                                    ;   the register save, so fewer is faster
AR_BUDMAX   equ 0x4000            ; (weights: roughly 8080 instructions)
AR_CELLS    equ 896                 ; 28 x 32 cells of 8 rows x 1 byte

; =============================================================================
; ar_entry - in: DS = ours, ES = KERNEL_SEG. out: BX = window, CF = 0
; =============================================================================
ar_entry:
    OS88_MEGA_REFUSE
    push es
    call op_load                    ; the three parts; a refusal has toasted
    pop es
    jc .out
    mov al, 0
    call op_seg
    mov [ar_core+2], ax
    mov al, 1
    call op_seg
    mov [ar_vseg], ax
    mov al, 2
    call op_seg
    mov [ar_aseg], ax
    push es
    mov es, [ar_core+2]
    cmp word [es:ARC_HDR+AH_MAGIC], ARC_MAGIC
    mov ax, [es:ARC_HDR+AH_ENTER]
    pop es
    jne .bad
    mov [ar_core], ax
    call ar_reset
    mov si, ar_tpl
    call OSAPI_WM_CREATE
    jc .out
    OS88_REGION_MOVABLE
    OS88_ALTENTER_ARM               ; arms the key-state map OSAPI_KEY_DOWN reads
    mov [ar_win], bx
    mov si, ar_menus
    call OSAPI_MENU_SET
    mov si, ar_about
    call OSAPI_ABOUT_SET
    clc
.out:
    ret
.bad:
    mov si, ar_s_bad
    call ar_toast
    stc
    ret

; ar_toast - SI = a NUL line in our segment. Every register kept.
ar_toast:
    push ax
    push cx
    push es
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop cx
    pop ax
    ret

; =============================================================================
; ar_reset - power the board on: the ROM into the view, RAM and VRAM and the
; cells cleared, the 8080 at 0000 with interrupts off. Every register kept.
; =============================================================================
ar_reset:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov es, [ar_vseg]
    xor di, di
    mov cx, (ARC_VIEW_KB * 1024) / 2
    xor ax, ax
    cld
    rep stosw
    mov ds, [ar_core+2]
    mov si, [ARC_HDR+AH_ROM]
    mov cx, [ARC_HDR+AH_ROMLEN]
    xor di, di
    rep movsb
    mov word [es:ST_AF], 0x0200     ; F: bit 1 is always 1 on an 8080
    mov word [es:ST_SP], 0x2400     ; (the ROM sets its own at once; this
                                    ;   keeps a poll's CALL inside the view)
    mov byte [es:V_IN1], 0x08       ; IN 1 bit 3 reads 1 on the board
    mov word [es:V_SIG], 'AR'
    mov word [es:V_SIG+2], 'CV'
    mov byte [es:V_IN2], 0x00       ; the DIPs: 3 bases, extra at 1500,
    pop es                          ;   coin info shown
    pop ds
    push es
    mov es, [ar_aseg]
    mov word [es:AUX_LPTR], AUX_LIST
    pop es
    mov byte [ar_halted], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; The window: the launcher
; =============================================================================
ar_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT
    mov bx, ax
    add dx, 6
    mov si, ar_lines
.line:
    lodsw
    or ax, ax
    jz .about
    push si
    mov si, ax
    mov cx, bx
    add cx, 8
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop si
    add dx, 11
    jmp .line
.about:
    pop si
    push si
    cmp byte [ar_abon], 0
    je .out
    mov bx, si
    mov si, ar_ablines
    call os88ui_about_d
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

ar_onkey:                           ; AX = the key
    cmp al, 13
    je ar_play
    cmp al, ' '
    je ar_play
    ret

ar_onclick:
    call ar_abdismiss
    jc .out
    call ar_play
.out:
    ret

ar_oncmd:                           ; Game: Play (0), Reset (1)
    push ax
    call ar_abdismiss
    pop ax
    cmp al, 1
    jne ar_play
    call ar_reset
    ret

ar_about:
    mov byte [ar_abon], 1
    mov bx, si
    mov si, ar_ablines
    jmp os88ui_about

ar_abdismiss:
    mov bx, ar_abon
    mov ax, ar_repaint
    jmp os88ui_abdismiss

ar_repaint:
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, si
    call OSAPI_WM_CONTENT
    mov bx, dx
    mov cx, ax
    add cx, AR_CONT_W-1
    add dx, AR_CONT_H-1
    call OSAPI_GFX_FILL
    call ar_paint
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ar_play - into the full-screen bracket, and the toast it brings back
; =============================================================================
ar_play:
    mov word [ar_err], 0
    mov ax, ar_fsx
    mov bx, [ar_win]
    xor cx, cx
    call OSAPI_FSX_RUN
    jnc .ran
    mov si, ar_s_nofsx
    jmp ar_toast
.ran:
    mov si, [ar_err]
    or si, si
    jz .out
    jmp ar_toast
.out:
    ret

; ar_fsx - the bracket's proc: SI = our window, DS = ours, ES = KERNEL_SEG
ar_fsx:
    mov bx, si
    call OSAPI_FSX_CAPS
    mov dl, FSXM_VGA12
    test ax, 1 << FSXM_VGA12
    jnz .mode
    mov dl, FSXM_HERC
    test ax, 1 << FSXM_HERC
    jnz .mode
    mov dl, FSXM_CGA640
    test ax, 1 << FSXM_CGA640
    jnz .mode
    mov word [ar_err], ar_s_nomode
    ret
.mode:
    mov al, dl
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
    call ar_vinit
    call ar_dirtyall
    call ar_render
    call ar_loop
    ret

; =============================================================================
; ar_loop - run the board until Esc (ARCADE-SPEC 5)
; =============================================================================
ar_loop:
    mov byte [ar_quit], 0
    call pit_now
    add ax, AR_HALF
    adc dx, 0
    mov [ar_due], ax
    mov [ar_due+2], dx
    mov byte [ar_which], 1
    mov word [ar_budget], 128
    call pit_now                    ; a run is timed from the end of the one
    mov [ar_t0], ax                 ;   before it: one PIT read a run
    mov [ar_t0+2], dx
.run:
    cmp byte [ar_halted], 0         ; HLT waits for the interrupt
    jne .clock
    mov es, [ar_vseg]
    mov ax, [ar_budget]
    mov [es:ST_BUD], ax
    push ds
    mov ax, es
    mov dx, [ar_aseg]
    call far [ar_core]
    pop ds
    sti
    mov es, [ar_vseg]
    inc word [es:V_NRUN]
    mov ax, [ar_budget]
    mov [es:V_NBUD], ax
    mov al, [es:ST_WHY]
    cmp al, ARCX_MISS
    je .miss
    cmp al, ARCX_HALT
    jne .budget
    mov byte [ar_halted], 1
.budget:
    mov es, [ar_vseg]               ; a run EI cut short says nothing about
    mov al, 0                       ;   how long a budget takes
    xchg al, [es:ST_EIX]
    or al, al
    jnz .clock
    call pit_now                    ; the budget follows the run's length,
    push ax                         ;   in 32 bits: a run past 55 ms wraps a
    push dx                         ;   word to a small, plausible number
    sub ax, [ar_t0]
    sbb dx, [ar_t0+2]
    jnz .long
    cmp ax, AR_RUNLO
    jae .notshort
    cmp word [ar_budget], AR_BUDMAX
    jae .adapted
    shl word [ar_budget], 1
    jmp short .adapted
.notshort:
    cmp ax, AR_RUNHI
    jbe .adapted
.long:
    cmp word [ar_budget], 2
    jbe .adapted
    shr word [ar_budget], 1
.adapted:
    pop dx
    pop ax
    mov [ar_t0], ax
    mov [ar_t0+2], dx
    jmp short .due
.clock:
    call pit_now
    mov [ar_t0], ax
    mov [ar_t0+2], dx
.due:
                                    ; now - due, signed
    sub ax, [ar_due]
    sbb dx, [ar_due+2]
    js .run
    or dx, dx                       ; two frames behind: catch up by
    jnz .resync                     ;   dropping, never by racing
    cmp ax, AR_HALF * 4
    jb .deliver
.resync:
    mov es, [ar_vseg]
    inc word [es:V_NDROP]
    call pit_now
    mov [ar_due], ax
    mov [ar_due+2], dx
.deliver:
    mov es, [ar_vseg]
    mov byte [es:ST_PEND], 1
    cmp byte [es:ST_INTE], 0        ; masked: it stays pending, and the EI
    je .run                         ;   that unmasks it ends the next run
    mov byte [es:ST_INTE], 0
    mov byte [es:ST_PEND], 0
    mov byte [ar_halted], 0
    mov bx, [es:ST_SP]              ; RST n: the PC onto the 8080's stack
    sub bx, 2
    mov [es:ST_SP], bx
    mov ax, [es:ST_PC]
    mov [es:bx], ax
    mov al, [ar_which]
    mov ah, 0
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov [es:ST_PC], ax
    add word [ar_due], AR_HALF
    adc word [ar_due+2], 0
    xor byte [ar_which], 3
    cmp byte [ar_which], 1          ; RST 2 went in: a frame is done
    jne .run
    inc word [es:V_NFRAME]
    call ar_render
    call ar_sound
    call ar_keys
    cmp byte [ar_quit], 0
    je .run
    jmp ar_quiet
.miss:
    mov ax, [es:ST_PC]
    mov di, ar_s_miss + 19
    call ar_hex4
    mov word [ar_err], ar_s_miss
    jmp ar_quiet

; ar_hex4 - AX as four hex digits at DS:DI
ar_hex4:
    mov cx, 4
.d:
    push cx
    mov cl, 4
    rol ax, cl
    pop cx
    push ax
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe .w
    add al, 7
.w:
    mov [di], al
    inc di
    pop ax
    loop .d
    ret

; =============================================================================
; ar_keys - the controls into IN 1 and IN 2, once a frame
; =============================================================================
ar_keys:
.drain:                             ; nothing typed in here reaches the
    mov ah, 1                       ;   desktop afterwards - and a coin or a
    int 0x16                        ;   start TYPED between two samples of
    jz .levels                      ;   the levels below is still seen: it
    xor ah, ah                      ;   holds its bit for AR_PULSE frames
    int 0x16
    cmp al, 27                      ; Esc, typed: back to the window
    jne .notesc
    mov byte [ar_quit], 1
.notesc:
    or al, 0x20                     ; (letters to lower case)
    mov si, ar_typed
.t:
    mov cx, [si]
    or cx, cx
    jz .drain
    add si, 2
    cmp al, cl
    jne .t
    or [ar_pbits], ch
    mov byte [ar_pleft], AR_PULSE
    jmp .drain
.levels:
    mov bl, 0x08
    cmp byte [ar_pleft], 0
    je .held
    or bl, [ar_pbits]
    dec byte [ar_pleft]
    jnz .held
    mov byte [ar_pbits], 0
.held:
    mov si, ar_keymap
.k:
    lodsw
    or al, al
    jz .done
    call OSAPI_KEY_DOWN
    jnc .k
    or bl, ah
    jmp .k
.done:
    mov es, [ar_vseg]
    mov [es:V_IN1], bl
    mov al, bl                      ; IN 2: one player's controls serve both
    and al, 0x70                    ;   (an upright cabinet's are shared)
    mov ah, [es:V_IN2]
    and ah, 0x8F
    or al, ah
    mov [es:V_IN2], al
    ret

AR_PULSE    equ 5                   ; frames a typed coin or start is held
ar_typed:                           ; a character typed, its IN 1 bit
    db '5', 0x01
    db 'c', 0x01
    db '1', 0x04
    db '2', 0x02
    dw 0

ar_keymap:                          ; scancode, IN 1 bit: the HELD keys,
    db KSC_SPACE, 0x10              ;   asked by level once a frame. Each is
    db KSC_LEFT, 0x20               ;   a far call, so only these: a coin, a
    db KSC_RIGHT, 0x40              ;   start and Esc are TYPED (ar_typed),
    db 0, 0                         ;   and nine calls a frame were 9% of an
                                    ;   8088 in play with the IRQs (MEASURED)

; =============================================================================
; THE PICTURE (ARCADE-SPEC 4)
;
; The 8080 writes VRAM as 224 rows of 32 bytes; the cabinet's monitor is
; turned, so a byte is 8 pixels down one screen COLUMN. A cell is 8 rows of
; one byte: turning it is an 8x8 bit transpose, and gives 8 screen bytes, one
; per screen row, each doubled across to 16 pixels for the aspect. ar_ytab
; says where each of the 256 screen rows goes on this adapter.
; =============================================================================

; ar_vinit - the row table, the doubling table and, on VGA, the overlay
ar_vinit:
    push ds
    pop es
    xor bx, bx                      ; the doubling table: a byte's 8 pixels
.dbl:                               ;   as 16, left byte first
    mov al, bl
    call ar_dbl4hi
    mov dl, ah
    mov al, bl
    mov cl, 4
    shl al, cl
    call ar_dbl4hi
    mov dh, ah
    mov si, bx
    shl si, 1
    mov [ar_dbl+si], dx
    inc bl
    jnz .dbl
    mov ax, [ar_fsi+FSI_SEG]
    mov [ar_vidseg], ax
    xor bx, bx                      ; BX = Y
    mov di, ar_ytab
.row:
    mov al, [ar_fsi+FSI_MODE]
    cmp al, FSXM_VGA12
    je .vga
    cmp al, FSXM_HERC
    je .herc
    mov ax, bx                      ; CGA: 256 rows into 200, merged inside
    mov cl, 3                       ;   a cell so no two cells share a row
    shr ax, cl                      ; AX = q
    mov dx, ax
    mov cl, 6
    mul cl                          ; q x 6
    mov cx, dx
    shr cx, 1
    shr cx, 1
    add ax, cx                      ; + the 7-row cells before q
    mov si, bx
    and si, 7
    and dl, 3
    cmp dl, 3
    jne .six
    add si, 8
.six:
    add al, [ar_cgamap+si]
    adc ah, 0
    mov dx, ax
    shr ax, 1
    mov cl, 80
    mul cl
    test dl, 1
    jz .cgaeven
    add ax, 0x2000
.cgaeven:
    add ax, 12
    stosw
    mov ax, 0xFFFF
    stosw
    jmp .next
.herc:
    mov ax, bx
    add ax, 46
    mov dx, ax
    shr ax, 1
    shr ax, 1
    mov cl, 90
    mul cl
    and dx, 3
    mov cl, 13
    shl dx, cl
    add ax, dx
    add ax, 17
    stosw
    mov ax, 0xFFFF
    stosw
    jmp .next
.vga:
    mov ax, bx                      ; 8 rows into 15: seven doubled, one not
    mov cl, 3
    shr ax, cl
    mov cl, 15
    mul cl
    mov si, bx
    and si, 7
    shl si, 1
    add ax, si
    mov cx, 80
    mul cx
    add ax, 12
    stosw
    cmp si, 14
    je .single
    add ax, 80
    stosw
    jmp .next
.single:
    mov ax, 0xFFFF
    stosw
.next:
    inc bx
    cmp bx, 256
    jb .row
    mov word [ar_put], ar_put_cga
    mov al, [ar_fsi+FSI_MODE]
    cmp al, FSXM_HERC
    jne .notherc
    mov word [ar_put], ar_put_herc
.notherc:
    cmp al, FSXM_VGA12
    jne .out
    mov word [ar_put], ar_put_vga
    call ar_overlay
.out:
    ret

; ar_dbl4hi - AH = AL's top four pixels, each doubled
ar_dbl4hi:
    mov cx, 4
    xor ah, ah
.b:
    shl al, 1
    pushf
    rcl ah, 1
    popf
    rcl ah, 1
    loop .b
    ret

; ar_overlay - the cabinet's cellophane, in planes 1 and 2, once: the game
; writes plane 0 only and the DAC colours each band (ARCADE-SPEC 4.3)
ar_overlay:
    mov dx, 0x3C8
    xor al, al
    out dx, al
    inc dx
    mov si, ar_pal
    mov cx, 6 * 3
.pal:
    lodsb
    out dx, al
    loop .pal
    mov es, [ar_vidseg]
    mov si, ar_bands
.band:
    lodsb                           ; plane mask, 0 = done
    or al, al
    jz .done
    mov ah, al
    mov dx, 0x3C4
    mov al, 2
    out dx, ax
    lodsw
    mov bx, ax                      ; first Y
    lodsw
    mov bp, ax                      ; last Y + 1
    lodsw
    mov [ar_band], ax               ; first byte, count
.y:
    push si
    mov si, bx
    shl si, 1
    shl si, 1
    mov di, [ar_ytab+si]
    call .fill
    mov di, [ar_ytab+si+2]
    cmp di, 0xFFFF
    je .ny
    call .fill
.ny:
    pop si
    inc bx
    cmp bx, bp
    jb .y
    jmp .band
.fill:
    mov al, [ar_band]
    mov ah, 0
    add di, ax
    mov cl, [ar_band+1]
    mov ch, 0
    mov al, 0xFF
    rep stosb
    ret
.done:
    mov dx, 0x3C4                   ; the game's plane from here on
    mov ax, 0x0102
    out dx, ax
    ret

ar_pal:     db 0,0,0, 63,63,63, 0,0,0, 63,16,16, 0,0,0, 16,63,16
ar_bands:   db 2
            dw 32, 64
            db 0, 56                ; red over the saucer's rows
            db 4
            dw 184, 240
            db 0, 56                ; green over the shields and the base
            db 4
            dw 240, 256
            db 4, 30                ; ...and over the spare bases, not the
            db 0                    ;   credits

ar_cgamap:  db 0,1,1,2,3,4,4,5, 0,1,2,3,3,4,5,6

; ar_dirtyall - every cell onto the write list, as if each had just changed:
; a fresh screen after a mode set. Its flags set, so the core adds none twice
ar_dirtyall:
    mov es, [ar_aseg]
    cld
    mov di, AUX_LIST
    mov ax, 0x2400
.band:
    mov cx, 32
.col:
    stosw
    mov bx, ax
    mov byte [es:bx], 1
    inc ax
    loop .col
    add ax, 0x100 - 32
    cmp ah, 0x40
    jb .band
    mov [es:AUX_LPTR], di
    ret

; =============================================================================
; ar_render - draw every cell on the write list, once
;
; The core lists a cell the first time a store CHANGES one of its bytes (the
; translated store compares first: 79% of VRAM stores in play change nothing,
; tools/arcsim.py), and its flag - the byte at the key's own offset in aux -
; keeps it off the list again until it is drawn here. So the list is the
; cells that changed, each once, at most 896 of them.
; =============================================================================
ar_render:
    mov si, AUX_LIST
.cells:
    mov es, [ar_aseg]
    cmp si, [es:AUX_LPTR]
    jae .drawn
    mov bx, [es:si]
    mov byte [es:bx], 0
    push si
    mov es, [ar_vseg]
    inc word [es:V_NCELL]
    call ar_cell
    pop si
    add si, 2
    jmp .cells
.drawn:
    mov word [es:AUX_LPTR], AUX_LIST
    ret

; ar_cell - draw the cell whose key is BX: its first byte's 8080 address,
; 2400h + band x 256 + column. Clobbers all but DS.
ar_cell:
    mov si, bx
    mov al, bh
    sub al, 0x24
    mov ah, 0
    shl ax, 1
    mov bp, ax                      ; two screen bytes per cell across
    mov al, bl                      ; the top screen row: Y = 248 - 8c
    mov ah, 0
    mov cl, 3
    shl ax, cl
    neg ax
    add ax, 248
    shl ax, 1
    shl ax, 1
    add ax, ar_ytab
    push ax
    push ds
    mov ds, [ar_vseg]
    mov al, [si]
    mov ah, [si+32]
    mov bl, [si+64]
    mov bh, [si+96]
    mov cl, [si+128]
    mov ch, [si+160]
    mov dl, [si+192]
    mov dh, [si+224]
    pop ds
    pop si
%rep 8                              ; out[b] = bit b of rows 0..7, row 0 at
    shr al, 1                       ;   the left; pushed b = 0 first, so the
    rcl di, 1                       ;   pops come top row first
    shr ah, 1
    rcl di, 1
    shr bl, 1
    rcl di, 1
    shr bh, 1
    rcl di, 1
    shr cl, 1
    rcl di, 1
    shr ch, 1
    rcl di, 1
    shr dl, 1
    rcl di, 1
    shr dh, 1
    rcl di, 1
    push di
%endrep
    mov es, [ar_vidseg]
    jmp [ar_put]

; ar_put_* - pop the cell's 8 rows, top first, and put each doubled across.
; SI = its first row's ytab entry, BP = the cell's byte offset across.
ar_put_vga:                         ; 8 rows into 15: the last is single
    ; STKBALANCE-OK: jumped to from ar_cell, which pushed the eight rows it pops
%rep 7
    pop bx
    xor bh, bh
    shl bx, 1
    mov ax, [ar_dbl+bx]
    mov di, [si]
    mov [es:di+bp], ax
    mov [es:di+bp+80], ax
    add si, 4
%endrep
    pop bx
    xor bh, bh
    shl bx, 1
    mov ax, [ar_dbl+bx]
    mov di, [si]
    mov [es:di+bp], ax
    ret

ar_put_herc:
    ; STKBALANCE-OK: jumped to from ar_cell, which pushed the eight rows it pops
%rep 8
    pop bx
    xor bh, bh
    shl bx, 1
    mov ax, [ar_dbl+bx]
    mov di, [si]
    mov [es:di+bp], ax
    add si, 4
%endrep
    ret

ar_put_cga:                         ; 8 rows into 6 or 7: a row the next
    ; STKBALANCE-OK: jumped to from ar_cell, which pushed the eight rows this
    ; loop pops, one a turn, the count in CX
    xor dx, dx                      ;   merges into is ORed, not written
    mov cx, 8
.o:
    pop bx
    xor bh, bh
    shl bx, 1
    or dx, [ar_dbl+bx]
    mov di, [si]
    cmp cx, 1
    je .w
    cmp di, [si+4]
    je .n
.w:
    mov [es:di+bp], dx
    xor dx, dx
.n:
    add si, 4
    loop .o
    ret

; =============================================================================
; THE SOUND (ARCADE-SPEC 4.5): the board's discrete circuits as speaker tones
;
; OUT 3 and OUT 5 are latches whose bits start and hold the circuits. Once a
; frame the rising bits pick a cue, by priority, and a held saucer warbles
; under everything. Through OSAPI_SND_TONE, the tone route WireBall uses
; inside its own bracket, called only when the pitch changes. OUT 3 bit 5 is
; the amplifier: the attract mode turns it off, and so does this.
; =============================================================================
ar_sound:
    mov es, [ar_vseg]
    mov al, [es:V_SND3]
    mov ah, [es:V_SND5]
    mov bx, [ar_sold]
    mov [ar_sold], ax
    not bx
    and bx, ax                      ; BX = the bits that rose this frame
    test al, 0x20
    jz ar_quiet                     ; the amplifier is off
    mov si, ar_cues
.cue:
    lodsw                           ; AX = the bit (OUT 3 low, OUT 5 high)
    or ax, ax
    jz .play
    mov dx, ax
    lodsw                           ; AX = the cue's notes
    test bx, dx
    jz .cue
    mov cx, si
    sub cx, ar_cues                 ; CX = its rank: lower is louder
    cmp word [ar_cseq], 0
    je .start
    cmp cx, [ar_crank]
    ja .cue
.start:
    mov [ar_cseq], ax
    mov [ar_crank], cx
    mov byte [ar_cwait], 0
.play:
    mov si, [ar_cseq]
    or si, si
    jz .ufo
    cmp byte [ar_cwait], 0
    je .note
    dec byte [ar_cwait]
    jmp .hold
.note:
    mov ax, [si]
    or ax, ax
    jz .done
    mov cl, [si+2]
    mov [ar_cwait], cl
    add si, 3
    mov [ar_cseq], si
    jmp ar_tone
.done:
    mov word [ar_cseq], 0
.ufo:
    test byte [ar_sold], 0x01       ; the saucer, held: two pitches
    jz ar_quiet
    inc byte [ar_warb]
    mov ax, 700
    test byte [ar_warb], 4
    jz ar_tone
    mov ax, 900
    jmp ar_tone
.hold:
    ret

ar_quiet:
    xor ax, ax
; ar_tone - AX = Hz (0 = off), only when it changes
ar_tone:
    cmp ax, [ar_hz]
    je .out
    mov [ar_hz], ax
    xor cx, cx
    mov dl, 0x40
    call OSAPI_SND_TONE
.out:
    ret

ar_cues:                            ; the bit, the notes: by priority
    dw 0x0004, ar_n_die             ; OUT 3 bit 2: the base is hit
    dw 0x1000, ar_n_ufohit          ; OUT 5 bit 4: the saucer is hit
    dw 0x0010, ar_n_extra           ; OUT 3 bit 4: an extra base
    dw 0x0008, ar_n_invhit          ; OUT 3 bit 3: an invader is hit
    dw 0x0002, ar_n_shot            ; OUT 3 bit 1: a shot
    dw 0x0100, ar_n_f1              ; OUT 5 bits 0-3: the fleet's four steps
    dw 0x0200, ar_n_f2
    dw 0x0400, ar_n_f3
    dw 0x0800, ar_n_f4
    dw 0
ar_n_die:    dw 220
             db 4
             dw 180
             db 4
             dw 150
             db 4
             dw 120
             db 4
             dw 200
             db 4
             dw 160
             db 4
             dw 130
             db 4
             dw 100
             db 4
             dw 170
             db 4
             dw 140
             db 4
             dw 110
             db 4
             dw 90
             db 6
             dw 0
ar_n_ufohit: dw 1200
             db 3
             dw 1000
             db 3
             dw 1200
             db 3
             dw 900
             db 3
             dw 1100
             db 3
             dw 800
             db 3
             dw 0
ar_n_extra:  dw 1000
             db 4
             dw 1300
             db 4
             dw 1000
             db 4
             dw 1300
             db 4
             dw 0
ar_n_invhit: dw 900
             db 2
             dw 700
             db 2
             dw 500
             db 2
             dw 400
             db 2
             dw 0
ar_n_shot:   dw 1600
             db 1
             dw 1300
             db 1
             dw 1000
             db 1
             dw 800
             db 1
             dw 600
             db 1
             dw 0
ar_n_f1:     dw 110
             db 4
             dw 0
ar_n_f2:     dw 98
             db 4
             dw 0
ar_n_f3:     dw 87
             db 4
             dw 0
ar_n_f4:     dw 82
             db 4
             dw 0

%include "os88pit.inc"

; =============================================================================
; data
; =============================================================================
ar_tpl:
    dw 60, 40, AR_CONT_W+2, AR_CONT_H+TITLE_H+1
    dw ar_title, ar_paint, ar_onkey, ar_onclick
    OS88_MENUSET ar_menus, ar_title, ar_oncmd
        OS88_MENU ar_m_game, ar_i_game, 2
    OS88_MENUSET_END ar_menus
ar_i_game:  dw ar_s_play, ar_s_reset
ar_m_game:  db 'Game', 0
ar_s_play:  db 'Play', 0
ar_s_reset: db 'Reset', 0
ar_title:   db 'Arcade', 0

ar_lines:   dw ar_l1, ar_l2, ar_l3, ar_l4, ar_l5, ar_l6, ar_l7, ar_l8, 0
ar_l1:      db 'Space Invaders (Taito/Midway 1978)', 0
ar_l2:      db 0
ar_l3:      db 'Enter or click: play full screen', 0
ar_l4:      db '5 or C: coin    1, 2: players', 0
ar_l5:      db 'Left, Right: move  Space: fire', 0
ar_l6:      db 'Esc: back here, the game held', 0
ar_l7:      db 0
ar_l8:      db 'Game > Reset: power the board off', 0

ar_ablines: dw ar_ab1, ar_ab2, ar_ab3, ar_ab4, 0
ar_ab1:     db 'Arcade for os8088', 0
ar_ab2:     db 'An 8080 board, translated', 0
ar_ab3:     db 'to 8086 ahead of time', 0
ar_ab4:     db 'Space Invaders (C) Taito', 0

ar_s_bad:   db 'Arcade: code part bad', 0
ar_s_nofsx: db 'Arcade: no full screen here', 0
ar_s_nomode: db 'Arcade: no mode for this display', 0
ar_s_miss:  db 'Arcade: no code at ????', 0

%define OS88UI_ABOUT
%define OS88UI_NOBTN
%define OS88UI_ABDISMISS
%include "os88ui.inc"

; =============================================================================
; bss: the parts standard's first, then ours
; =============================================================================
%assign AR_BSS 0
%macro AR_VAR 2
%1 equ os88_image_end + OP_BSS + AR_BSS
%assign AR_BSS AR_BSS + %2
%endmacro
AR_VAR ar_core, 4                   ; dw arc_enter, dw the code part
AR_VAR ar_vseg, 2
AR_VAR ar_aseg, 2
AR_VAR ar_vidseg, 2
AR_VAR ar_win, 2
AR_VAR ar_abon, 1
AR_VAR ar_quit, 1
AR_VAR ar_halted, 1
AR_VAR ar_which, 1
AR_VAR ar_err, 2
AR_VAR ar_due, 4
AR_VAR ar_t0, 4
AR_VAR ar_band, 2
AR_VAR ar_put, 2                    ; ar_put_vga, _herc or _cga
AR_VAR ar_budget, 2
AR_VAR ar_col, 2
AR_VAR ar_xoff, 2
AR_VAR ar_sold, 2                   ; the sound latches last frame
AR_VAR ar_cseq, 2                   ; the cue playing: its next note
AR_VAR ar_crank, 2
AR_VAR ar_cwait, 1
AR_VAR ar_warb, 1
AR_VAR ar_hz, 2                     ; the pitch sounding now
AR_VAR ar_pbits, 1                  ; typed coin/start bits being held
AR_VAR ar_pleft, 1                  ;   ...and for how many more frames
AR_VAR ar_fsi, FSI_SIZE
AR_VAR ar_dbl, 512
AR_VAR ar_ytab, 1024
    OS88_BSS OP_BSS + AR_BSS
    OS88_IMAGE_END
