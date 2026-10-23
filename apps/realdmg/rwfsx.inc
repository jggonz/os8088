; =============================================================================
; os8088 - apps/realdmg/rwfsx.inc
;
; RealDMG's FULL SCREEN (docs/REALDMG-SPEC.md section 15): the machine run on
; the UI task inside an OSAPI_FSX_RUN bracket, with the screen its own.
; Included by realdmg.asm.
;
; The worker is FROZEN in there, so it must not be frozen INSIDE the machine
; - in the core, or in rdg_render, whose working memory is the view's. It
; holds [rw_incore] from its loop's top to the end of its render and backs
; off at the top when [rw_fsq] is set; rw_fsenter sets [rw_fsq] and yields
; until [rw_incore] is clear. The store order is what makes it exact: the
; worker raises its byte BEFORE it reads ours, so once we have seen it clear
; with ours set, every later entry of its sees ours and backs off.
;
; THE SCREEN is mode 0Dh, 320x200 planar, the picture at 1x in the middle.
; rdg_render's two shade planes go to VGA planes 0 and 1 as they are, through
; a RAM shadow of the last frame: a word is written only when it changed.
; Plane 2 is set everywhere but the picture, so the border is DAC entry 4 and
; the speed line, planes 0 and 1 over it, entry 7. The DAC is ours in here;
; the restore's BIOS mode set puts the desktop's back (SPEC.md 53.6).
;
; THE CLOCK is FSXF_RATE at the Game Boy's own frame rate: a hook on every
; period counts frames owed and nothing else, and the loop runs the machine
; to the count. It draws a frame when it has caught up, or when RW_FSSKIP
; frames have gone by undrawn. kern_small refuses FSXF_RATE (and has no
; VGA): the bracket is retried without it and paces by the tick.
; =============================================================================

RW_FSDIV        equ 19977           ; 1,193,182 / 59.7275: a period a frame
RW_FSSKIP       equ 4               ; frames run undrawn at most, behind
RW_FSX          equ 0               ; the picture's byte column and first row:
RW_FSY          equ 0               ;   the top left of the CRTC's window
RW_FSTXT        equ 146             ; the speed line's row, inside the window
RW_FSVGA        equ 0               ; rw_fskind: the writers
RW_FSHERC       equ 1
RW_FSCGA        equ 2
RW_HX           equ 25              ; Hercules: the picture's byte column
RW_HY           equ 26              ;   (x = 200, 320 wide) and first row
RW_HTXT         equ 322             ;   (288 tall), and the speed line's
RW_CY           equ 28              ; CGA: the picture's first row (320 by
RW_CTXT         equ 182             ;   144), and the speed line's
RW_FSBUD        equ 6000            ; M-cycles a core run in here: no mouse
RW_FSBUD86      equ 1400            ;   to serve, so only IRQ0 bounds it - one
                                    ;   rate period is 16.7 ms, and a 286 at
                                    ;   ~35 clocks a busy M-cycle spends ~7 ms
                                    ;   on 6,000 of them all busy (an 8088 at
                                    ;   ~48, ~14 ms on 1,400)

; rw_mgrey - every menu item made to say what it is now: the choices
; marked, the settings' text, and greyed what cannot be had - Full Screen
; with no game or no mode we have a writer for, the recompiler's items on an
; 8088 ("(286+)"), the states and replays with no game. Any context.
rw_mgrey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; MACHINE: Full Screen greyed without a game or a writer; Pause, Show
    ; Stats and Log marked; Sound and Interlace say what they are
    mov si, rw_it_fs
    cmp byte [rw_state], 1
    jne .fs
    call rw_fspick                  ; CF = 1: no mode we have a writer for
    jc .fs
    inc si
.fs:
    mov [rw_i_mach + 2 * RW_MFS], si
    mov al, [rw_pause]
    mov si, rw_it_pause
    call rw_mark
    mov al, [rw_ovl]
    mov si, rw_it_stats
    call rw_mark
    mov al, [rw_logon]
    mov si, rw_it_log
    call rw_mark
    mov bl, [rw_sndm]
    xor bh, bh
    shl bx, 1
    mov si, [rw_sndit + bx]
    inc si
    mov [rw_i_mach + 2 * RW_MSND], si
    mov bl, [rdg_ilace]
    shl bx, 1
    mov si, [rw_ilit + bx]
    inc si
    mov [rw_i_mach + 2 * RW_MIL], si
    ; CPU: the three modes marked by the one chosen - the recompiler's two
    ; greyed on an 8088, Interpreter its mode - and Profile for Static and
    ; the cache likewise; the speed chosen marked
    mov cl, [rw_jitmode]
    cmp byte [rw_286], 0
    jne .m
    mov cl, 2                       ; (an 8088: the interpreter, and only)
.m:
    mov di, rw_i_cpu
    mov si, rw_it_jon
    mov al, 1
    call rw_mitem
    mov si, rw_it_jst
    mov al, 3
    call rw_mitem
    mov si, rw_it_joff
    mov al, 2
    call rw_mitem
    cmp byte [rw_286], 0
    jne .c286
    mov word [rw_i_cpu + 2 * RW_CJON], rw_it_jon8
    mov word [rw_i_cpu + 2 * RW_CJST], rw_it_jst8
    mov word [rw_i_cpu + 2 * RW_CPRF], rw_it_prf8
    mov word [rw_i_cpu + 2 * RW_CBIG], rw_it_big8
    jmp short .spd
.c286:
    mov al, [rw_prfon]
    mov si, rw_it_prf
    call rw_mark
    inc si
    mov [rw_i_cpu + 2 * RW_CPRF], si
    mov al, [rw_big]
    mov si, rw_it_big
    call rw_mark
    inc si
    mov [rw_i_cpu + 2 * RW_CBIG], si
.spd:
    mov di, rw_i_cpu + 2 * RW_CSPD  ; Speed's four, the one chosen marked
    mov cl, [rw_speed]
    xor bx, bx
.sp:
    mov si, [rw_spdit + bx]
    mov al, bl
    shr al, 1
    call rw_mitem
    add bx, 2
    cmp bx, 2 * RW_SPEEDS
    jb .sp
    ; SAVE STATE: the slot marked (none for 7-9, which only keys reach);
    ; Save and Load with a game, Record and Play with a game and the other
    ; not running, each saying Stop while it runs
    mov di, rw_i_st
    mov cl, [rw_slot]
    inc cl
    xor bx, bx
.sl:
    mov si, [rw_slotit + bx]
    mov al, bl
    shr al, 1
    inc al
    call rw_mitem
    add bx, 2
    cmp bx, 2 * RW_SSLOTS
    jb .sl
    mov si, rw_it_sv
    mov dx, rw_it_ld
    cmp byte [rw_state], 1
    jne .st
    inc si
    inc dx
.st:
    mov [rw_i_st + 2 * RW_SSV], si
    mov [rw_i_st + 2 * RW_SLD], dx
    mov si, rw_it_rec               ; Record and Play Replay: a game, and
    mov dx, rw_it_ply               ;   the other not running
    cmp byte [rw_state], 1
    jne .rp
    cmp byte [rw_rpmode], RPM_PLAY
    je .rpp
    mov si, rw_it_recs + 1
    cmp byte [rw_rpmode], RPM_REC
    je .rp
    mov si, rw_it_rec + 1
    mov dx, rw_it_ply + 1
    jmp short .rp
.rpp:
    mov dx, rw_it_plys + 1
.rp:
    mov [rw_i_st + 2 * RW_SREC], si
    mov [rw_i_st + 2 * RW_SPLY], dx
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_mark - SI = an item (RW_MI's label), AL = 0: its mark a space, else '*'
rw_mark:
    mov byte [si + 1], ' '
    or al, al
    jz .r
    mov byte [si + 1], '*'
.r:
    ret

; rw_mitem - one of a menu's choices: SI = its item, AL = what it chooses,
; CL = what is chosen; marked when they are the same, and its list entry -
; DI's, advanced - the item
rw_mitem:
    push ax
    cmp al, cl
    mov al, 0
    jne .m
    inc al
.m:
    call rw_mark
    lea ax, [si + 1]
    mov [di], ax
    pop ax
    inc di
    inc di
    ret

rw_slotit:      dw rw_it_slot1, rw_it_slot2, rw_it_slot3, rw_it_slot4
                dw rw_it_slot5, rw_it_slot6

; rw_fsenter - Machine > Full Screen, on the UI task with the lock held
rw_fsenter:
    cmp byte [rw_state], 1
    jne .r
    call rw_fspick                  ; AL = the writer
    jc .r                           ; greyed; a shortcut gets nothing either
    mov [rw_fskind], al
    call OSAPI_FONT_GLYPHS          ; DX:SI, AL first, AH last: copied now,
    call rw_fscopy                  ;   before the mode leaves the desktop's
    mov byte [rw_fsq], 1
.wait:
    cmp byte [rw_incore], 0         ; the worker out of the machine first
    je .go
    call rw_bserve                  ; (it may be waiting on us for a bank)
    call OSAPI_TASK_YIELD
    jmp short .wait
.go:
    mov byte [rw_pfmt], 0           ; the window's planes will not match the
                                    ;   frame the bracket leaves: the
                                    ;   restore's repaint paints white, and
                                    ;   the next draw starts from every line
    mov byte [rw_fsrate], 1
    mov ax, rw_fsmain
    mov bx, [rw_win]
    mov cx, FSXF_RATE
    mov dx, RW_FSDIV
    mov di, rw_fshook
    call OSAPI_FSX_RUN
    jnc .out
    mov byte [rw_fsrate], 0         ; kern_small: no rate, the tick paces
    mov ax, rw_fsmain
    mov bx, [rw_win]
    xor cx, cx
    call OSAPI_FSX_RUN
.out:
    call OSAPI_GET_TICKS            ; the window takes up from now
    mov [rw_last], ax
    mov word [rw_owed], 0
    call rw_drawnow                 ; (on the window's clock again)
    mov al, RLE_FSOUT
    call rw_logevt
    mov byte [rw_fsq], 0
    cmp byte [rw_lflq], 0           ; F12 in the bracket: the log written
    je .r                           ;   now that a toast can be read
    mov byte [rw_lflq], 0
    jmp rw_logflush
.r:
    ret

; rw_fspick - the writer for this window's display: VGA mode 0Dh, else the
; Hercules page at 2x2, else CGA's 320x200 in four colours. AL = RW_FS*, or
; CF = 1 for none.
rw_fspick:
    push bx
    mov bx, [rw_win]
    call OSAPI_FSX_CAPS             ; AX = the modes this display can set
    pop bx
    test ax, 1 << FSXM_VGA0D
    jz .h
    mov al, RW_FSVGA
    clc
    ret
.h:
    test ax, 1 << FSXM_HERC
    jz .c
    mov al, RW_FSHERC
    clc
    ret
.c:
    test ax, 1 << FSXM_CGA320
    jz .no
    mov al, RW_FSCGA
    clc
    ret
.no:
    stc
    ret

; rw_fscopy - the kernel's glyph table kept for the bracket's text, which
; reads it where it is (a far pointer, not a 768-byte copy: memory is not a
; drawing slot). DX:SI = the table, AL = its first code. Our lines are
; ASCII, which every table the kernel answers holds.
rw_fscopy:
    mov [rw_fontp], si
    mov [rw_fontp + 2], dx
    mov [rw_font0], al
    ret

; rw_fshook - every rate period: a frame owed. AX = periods since the last.
rw_fshook:
    add [rw_fowed], ax
    add [rw_fsper], ax              ; (rw_clk's, for the log)
    ret

; rw_fsmain - the bracket. DS = CS = ours, ES = KERNEL_SEG, SI = the window.
rw_fsmain:
    OS88_ALTENTER_SEED              ; the Alt+Enter that got us here is held
    push ds
    pop es
    mov bl, [rw_fskind]
    xor bh, bh
    mov al, [rw_fsmodes + bx]
    mov di, rw_fsi
    call OSAPI_FSX_MODE
    jc .r
    cmp byte [rw_fskind], RW_FSVGA
    jne .mono
    call rw_fsdac
    call rw_fscrtc
    call rw_fsframe
    call rw_dmgfull                 ; every line of the first frame
    jmp short .in
.mono:
    call rw_fsclear                 ; Hercules or CGA: the screen black,
    call rw_fstab                   ;   the writer's table, and every line
    call rw_dmgfull                 ;   of the first frame
.in:
    mov byte [rw_infsx], 1
    push word [rw_budget]           ; the window's budget, back at the end
    mov ax, RW_FSBUD
    cmp word [rw_budget], RW_BUDGET86
    jne .bud
    mov ax, RW_FSBUD86
.bud:
    mov [rw_budget], ax
    mov byte [rw_ovlnew], 1
    mov word [rw_fowed], 0
    mov byte [rw_fsfrac], 0
    mov byte [rw_fsrun], 0
    call OSAPI_GET_TICKS
    mov [rw_last], ax
    mov byte [rw_infsx], 1          ; (rw_clk: the bracket's clock now)
    mov al, RLE_FSIN
    call rw_logevt
    call rw_drawnow                 ; the first draws now
.loop:
    call rw_fsdrain                 ; ZF = 0: Esc
    jnz .out
    call rw_fsowed                  ; AX = frames owed now (taken)
    or ax, ax
    jnz .run
    cmp byte [rw_undrawn], 0        ; caught up: what has not been drawn,
    jne .idle                       ;   rather than wait (the window's .idle)
    mov al, FSXW_FRAME
    cmp byte [rw_fsrate], 0
    jne .w
    mov al, FSXW_TICK
.w:
    call OSAPI_FSX_WAIT
    jmp short .loop
.run:
    cmp ax, RW_BEHIND               ; behind by more than that: run on
    jbe .n                          ;   from here rather than race
    mov ax, RW_BEHIND
.n:
    mov [rw_fsleft], ax
    call rw_logowed                 ; (AL = frames owed, for the log)
.f:
    call rw_keys
    call rw_frame
    jc .out                         ; it stopped
    call rw_sound
    inc word [rw_nrun]
    call rw_stat
    inc byte [rw_fsrun]
    xor bx, bx
    dec word [rw_fsleft]
    jz .due                         ; caught up: draw this one, when due
    cmp byte [rw_fsrun], RW_FSSKIP  ; (rw_drawdue, as the window's)...
    jb .f
    mov bx, [rw_fsleft]             ; ...or behind (rw_drawdue), by more
    call rw_behind                  ;   the further behind
.due:
    call rw_drawdue
    jc .draw
    mov byte [rw_fsrun], 0
    cmp word [rw_fsleft], 0
    jne .f
    jmp .loop
.idle:
    mov word [rw_fsleft], 0         ; (none to run after it)
.draw:
    mov byte [rw_fsrun], 0
    mov si, rw_fsdraw
    call rw_timed
    call rw_fsdrain                 ; Esc between two draws too: on an 8088
    jnz .out                        ;   a turn of frames is longer than a press
    cmp word [rw_fsleft], 0
    jne .f
    jmp .loop
.out:
    pop word [rw_budget]
    mov byte [rw_infsx], 0
.r:
    ret

; rw_fsowed - AX = the frames owed since the last call, and none left owed
rw_fsowed:
    cmp byte [rw_fsrate], 0
    je .tick
    pushf
    cli
    mov ax, [rw_fowed]
    mov word [rw_fowed], 0
    popf
    cmp byte [rw_speed], 0          ; Machine > Speed under 100%: periods x
    je .r                           ;   its 256ths, the fraction kept
    push dx
    mul word [rw_spdk]
    add al, [rw_fsfrac]
    adc ah, 0
    adc dl, 0
    mov [rw_fsfrac], al
    mov al, ah
    mov ah, dl
    pop dx
.r:
    ret
.tick:
    push bx
    push dx
    call OSAPI_GET_TICKS
    mov bx, ax
    xchg bx, [rw_last]
    sub ax, bx
    cmp ax, 10                      ; away longer than that: start over
    jbe .t
    mov ax, 10
.t:
    mul word [rw_pertick]           ; (RW_PERTICK at 100%: Machine > Speed)
    add ax, [rw_owed]
    mov dl, al                      ; the fraction stays owed
    xor dh, dh
    mov [rw_owed], dx
    mov al, ah
    xor ah, ah
    pop dx
    pop bx
    ret

; rw_fsdrain - the BIOS key buffer emptied (a full one beeps); ZF = 0 when
; Esc is held
rw_fsdrain:
.k:
    mov ah, 1
    int 16h
    jz .e
    xor ah, ah
    int 16h
    call rw_klat                    ; a press a draw would hide (rw_keys)
    jmp short .k
.e:
    mov al, 1                       ; Esc
    call OSAPI_KEY_DOWN             ; CF = 1: held
    jc .out
    call os88alt_edge               ; ...or a fresh Alt+Enter
    jc .out
    xor al, al                      ; (ZF = 1: stay)
    ret
.out:
    or al, 1                        ; (ZF = 0: leave)
    ret

; rw_fsdac - shades 0-3 as greys at DAC 0-3, the border at 4, the text at 7
rw_fsdac:
    mov dx, 3C8h
    xor al, al
    out dx, al
    inc dx
    mov si, rw_fsgreys
    mov cx, 8 * 3
.d:
    lodsb
    out dx, al
    loop .d
    ret

; rw_fscrtc - mode 0Dh's DISPLAY cut to the picture (REALDMG-SPEC 15): 160
; pixels by 154 rows - the 144 of the picture, a gap and the speed line's 8 -
; from VRAM offset 0, centred by moving the blanking and the retraces, the
; border around it the card's OVERSCAN in the border's grey (index 4). The
; total, the sync widths and so every frequency are mode 0Dh's own: only
; where the sync falls against the picture moves. It is what lets the
; picture move in VRAM under a fixed window (the start address and the pel
; panning, SPEC.md 53.7: the bracket's mode is the app's to program); the
; exit's mode set puts back all of it.
rw_fscrtc:
    mov dx, 3D4h
    mov al, 11h                     ; CR11: unlock CR0-CR7 (bit 7)
    out dx, al
    inc dx
    in al, dx
    and al, 7Fh
    out dx, al
    dec dx
    mov si, rw_fscrv
    mov cx, (rw_fscrve - rw_fscrv) / 2
.c:
    lodsw                           ; AL = the index, AH = the value
    out dx, ax
    loop .c
    mov dx, 3DAh                    ; the attribute controller's flip-flop
    in al, dx
    mov dx, 3C0h
    mov al, 31h                     ; AR11, the overscan (with the display
    out dx, al                      ;   left on, bit 5)
    mov al, 4                       ; ...the border's grey
    out dx, al
    ret
rw_fscrv:   db 01h, 13h             ; horizontal display end: 20 chars
            db 02h, 1Fh             ; start blank, 9 chars earlier
            db 03h, 87h             ; end blank (39: bit 5 in CR05)
            db 04h, 22h             ; start retrace, 9 earlier
            db 05h, 89h             ; end retrace (41), end blank bit 5
            db 12h, 33h             ; vertical display end: 307 (154 x 2)
            db 15h, 68h             ; start blank 360, 46 lines earlier
            db 16h, 8Bh             ; end blank 395
            db 10h, 6Eh             ; start retrace 366
            db 11h, 80h             ; end retrace (368), CR0-CR7 locked again
rw_fscrve:

; rw_fsframe - plane 2 set everywhere but the picture: the border
rw_fsframe:
    mov es, [rw_fsi + FSI_SEG]
    mov dx, 3C4h
    mov ax, 0402h                   ; map mask: plane 2
    out dx, ax
    xor di, di
    mov cx, 40 * 200 / 2
    mov ax, 0FFFFh
    rep stosw
    mov di, RW_FSY * 40 + RW_FSX
    mov bx, 144
    xor ax, ax
.r:
    mov cx, 10
    rep stosw
    add di, 40 - 20
    dec bx
    jnz .r
    ret

; rw_fsdraw - the frame onto VRAM through the shadow, and the speed line
; when F1 has it up and it changed
rw_fsdraw:
    cmp byte [rw_fskind], RW_FSVGA
    je .vga
    jmp rw_fsdrawd
.vga:
    mov ax, [rw_fsi + FSI_SEG]      ; the changed lines, by damage - each to
    mov [rdg_dvga], ax               ;   VRAM as rdg_renderd decides it changed
    call rw_render                  ;   (ppu_dvga), not by a second pass over
    mov word [rdg_dvga], 0           ;   the buffer
    call rw_fsline                  ; ZF = 0: the speed line to draw
    jz .r
    call rw_fstext
.r:
    ret
%if RW_FSY * 40 + RW_FSX != 0
%error "ppu_dvga writes line n at 40 n: the picture's corner must be VRAM 0"
%endif

; rw_fstext - rw_ovltxt on the border under the picture, 16 cells wide
rw_fstext:                          ; (SI = the line, rw_fsline's)
    mov es, [rw_fsi + FSI_SEG]      ; (its own: the writer before it no
    mov dx, 3C4h                    ;   longer leaves ES on the card)
    mov ax, 0302h                   ; planes 0 and 1: the glyph's set bits
    out dx, ax                      ;   read 7, its clear ones 4
    mov di, RW_FSTXT * 40 + RW_FSX
    mov cx, 20
.c:
    lodsb
    or al, al
    jnz .g
    dec si                          ; past the end: blanks
    mov al, ' '
.g:
    push si
    push cx
    sub al, [rw_font0]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov si, [rw_fontp]
    add si, ax
    mov cx, 8
    push di
    push ds
    mov ds, [rw_fontp + 2]          ; (the kernel's table, where it is)
.row:
    movsb
    add di, 40 - 1
    loop .row
    pop ds
    pop di
    inc di
    pop cx
    pop si
    loop .c
    ret

; DAC 0-7, six bits a gun: the shades (0 lightest), the border, -, -, text
rw_fsgreys:
    db 63, 63, 63,  42, 42, 42,  21, 21, 21,  0, 0, 0
    db 4, 4, 8,  0, 0, 0,  0, 0, 0,  48, 48, 52

rw_fsmodes:     db FSXM_VGA0D, FSXM_HERC, FSXM_CGA320

; =============================================================================
; The Hercules and CGA writers (REALDMG-SPEC section 15.1): rdg_renderd's
; changed lines, through a table, straight to the page. A Game Boy byte -
; eight pixels, two shade bits each - is two NIBBLE indices, (bit 0's four
; pixels << 4) | bit 1's, and each index is a table entry: on a Hercules two
; bytes, the 2x2 dither's top and bottom rows of those four pixels doubled;
; on a CGA a word, the four pixels doubled in four colours.
; =============================================================================

; rw_fsclear - the page black. ES:DI from rw_fsi.
rw_fsclear:
    mov es, [rw_fsi + FSI_SEG]
    xor di, di
    mov cx, 8000h / 2               ; Hercules' 32KB (CGA's 16KB, twice)
    cmp byte [rw_fskind], RW_FSHERC
    je .c
    mov cx, 4000h / 2
.c:
    xor ax, ax
    rep stosw
    ret

; rw_fstab - rw_htab for the writer: 256 x (row a, row b) bytes for a
; Hercules, 256 words for a CGA
rw_fstab:
    xor bx, bx                      ; BL = the index
.i:
    xor dx, dx                      ; the entry, a pixel at a time
    mov ch, 3                       ; CH = the pixel's bit in each nibble,
.p:                                 ;   the leftmost (3) first
    mov cl, ch
    mov al, bl
    shr al, cl                      ; bit 1's: the low nibble
    and al, 1
    shl al, 1
    mov ah, bl
    add cl, 4
    shr ah, cl                      ; bit 0's: the high nibble
    and ah, 1
    or al, ah                       ; AL = the shade, 0-3
    push bx
    mov bl, al
    xor bh, bh
    cmp byte [rw_fskind], RW_FSHERC
    jne .cga
    shl dl, 1                       ; Hercules: two pixels on each row
    shl dl, 1
    shl dh, 1
    shl dh, 1
    or dl, [rw_hdita + bx]
    or dh, [rw_hditb + bx]
    jmp short .nx
.cga:
    mov cl, 4                       ; CGA: the colour twice, four bits
    shl dx, cl
    or dl, [rw_cgac + bx]
.nx:
    pop bx
    dec ch
    jns .p
    push bx
    xor bh, bh
    shl bx, 1
    cmp byte [rw_fskind], RW_FSHERC
    jne .w
    mov [rw_htab + bx], dl          ; Hercules: row a, then row b
    mov [rw_htab + bx + 1], dh
    jmp short .wd
.w:
    xchg dl, dh                     ; CGA: the left pixels' byte first
    mov [rw_htab + bx], dx
.wd:
    pop bx
    inc bl
    jnz .i
    ret

rw_hdita:       db 3, 3, 2, 0       ; the 2x2 dither, a shade's two pixels
rw_hditb:       db 3, 2, 1, 0       ;   on the top row and on the bottom
rw_cgac:        db 0Fh, 05h, 0Ah, 00h ; shade -> CGA colour, doubled: white,
                                    ;   cyan, magenta, black

; rw_fsdrawd - a frame by damage onto the Hercules or CGA page, and the speed
; line when it changed
rw_fsdrawd:
    call rw_render                  ; AX = the lines that changed
    mov es, [rw_fsi + FSI_SEG]
    or ax, ax
    jz .txt
    xor dl, dl                      ; DL = the line
.l:
    call rw_dchg
    jz .n
    push dx
    mov al, dl
    mov ah, 40
    mul ah
    push es                         ; the line's two planes into rw_lbuf:
    push ds                         ;   the writers read our table (rw_htab)
    pop es                          ;   beside it
    mov si, ax
    mov di, rw_lbuf
    mov ax, [rw_fseg]
    push ds
    mov ds, ax
    mov cx, 20
    rep movsw
    pop ds
    pop es
    mov si, rw_lbuf                 ; SI = the line's two planes
    xor dh, dh
    cmp byte [rw_fskind], RW_FSHERC
    jne .cga
    call rw_hline
    jmp short .p
.cga:
    call rw_cline
.p:
    pop dx
.n:
    inc dl
    cmp dl, 144
    jb .l
.txt:
    call rw_fsline
    jz .r
    call rw_fstext1
.r:
    ret

; rw_fsline - the speed line's turn: a note (rw_note) that has had its time
; comes down, and ZF = 0 when the line is to be drawn again - changed, and
; F1's or a note's or blank where one just was. SI = what it says
rw_fsline:
    cmp byte [rw_msgt], 0
    je .t
    call OSAPI_GET_TICKS
    sub ax, [rw_msgend]
    js .t
    mov byte [rw_msgt], 0           ; its time is up: blank, or F1's again
    mov byte [rw_msgclr], 1
    mov byte [rw_ovlnew], 1
.t:
    cmp byte [rw_ovlnew], 0
    je .r
    mov si, rw_msg
    cmp byte [rw_msgt], 0
    jne .y
    mov si, rw_ovltxt
    cmp byte [rw_ovl], 0
    jne .y
    mov si, rw_s_none               ; blank - when something is up
    cmp byte [rw_msgclr], 0         ;   (F1 off left its line standing: the
    jne .y                          ;   flag said only "a note came down")
    cmp al, al                      ; (ZF = 1: nothing to say)
.r:
    ret
.y:
    mov byte [rw_ovlnew], 0
    mov byte [rw_msgclr], 0         ; [rw_msgclr]: the line has text on it
    cmp si, rw_s_none
    je .z
    mov byte [rw_msgclr], 1
.z:
    or si, si                       ; (ZF = 0)
    ret
rw_s_none:  db 0

; rw_hrow - AX = a Hercules row: DI = its first byte at the picture's column
rw_hrow:
    push ax
    push cx
    push dx
    mov di, ax
    and di, 3
    mov cl, 13
    shl di, cl                      ; the bank: (row AND 3) x 2000h
    mov cl, 2
    shr ax, cl
    mov cx, 90
    mul cx
    add di, ax
    add di, RW_HX
    pop dx
    pop cx
    pop ax
    ret

; rw_hline - DX = a Game Boy line, SI = its planes: Hercules rows HY + 2 x
; line and the one under it, 40 bytes each
rw_hline:
    mov ax, dx
    shl ax, 1
    add ax, RW_HY
    call rw_hrow
    mov bp, di                      ; BP = row a
    inc ax
    call rw_hrow                    ; DI = row b
    xchg bp, di
    mov bx, rw_htab
    mov ch, 20
    mov cl, 4
.b:
    mov al, [si]                    ; bit 0's eight pixels
    mov ah, [si + 20]               ; bit 1's
    inc si
    mov dl, al
    and dl, 0F0h
    mov dh, ah
    shr dh, cl
    or dl, dh                       ; DL = the left four's index
    shl al, cl
    and ah, 0Fh
    or ah, al                       ; AH = the right four's
    mov al, dl
    xor dh, dh
    mov dl, ah                      ; (DL kept: the right index)
    xor ah, ah
    shl ax, 1
    xchg ax, bx
    mov ax, [rw_htab + bx]          ; AL = row a, AH = row b, left four
    xchg ax, bx
    mov [es:di], bl
    mov [es:bp], bh
    mov bl, dl
    xor bh, bh
    shl bx, 1
    mov ax, [rw_htab + bx]          ; ...the right four
    mov [es:di + 1], al
    mov [es:bp + 1], ah
    add di, 2
    add bp, 2
    dec ch
    jnz .b
    ret

; rw_cline - DX = a Game Boy line, SI = its planes: CGA row CY + line, 80
; bytes (even rows at 0, odd at 2000h)
rw_cline:
    mov ax, dx
    add ax, RW_CY
    mov di, ax
    and di, 1
    mov cl, 13
    shl di, cl
    shr ax, 1
    mov cx, 80
    push dx
    mul cx
    pop dx
    add di, ax
    mov ch, 20
    mov cl, 4
.b:
    mov al, [si]
    mov ah, [si + 20]
    inc si
    mov dl, al
    and dl, 0F0h
    mov dh, ah
    shr dh, cl
    or dl, dh                       ; DL = the left four's index
    shl al, cl
    and ah, 0Fh
    or ah, al                       ; AH = the right four's
    mov bl, dl
    xor bh, bh
    shl bx, 1
    mov dx, [rw_htab + bx]
    mov [es:di], dx
    mov bl, ah
    xor bh, bh
    shl bx, 1
    mov dx, [rw_htab + bx]
    mov [es:di + 2], dx
    add di, 4
    dec ch
    jnz .b
    ret

; rw_fstext1 - rw_ovltxt under the picture on a Hercules or CGA page, the
; glyph's set bits lit (a CGA's in colour 3)
rw_fstext1:                         ; (SI = the line, rw_fsline's)
    xor bx, bx                      ; BX = the cell
.c:
    mov al, [si]
    or al, al
    jnz .g
    mov al, ' '
    dec si
.g:
    inc si
    sub al, [rw_font0]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    push si
    mov si, [rw_fontp]
    add si, ax                      ; SI = its eight rows
    xor cx, cx                      ; CX = the row
.r:
    push ds
    mov ds, [cs:rw_fontp + 2]       ; (the kernel's table, where it is)
    lodsb
    pop ds
    push cx
    push bx
    cmp byte [rw_fskind], RW_FSHERC
    jne .cg
    mov dl, al
    mov ax, RW_HTXT
    add ax, cx
    call rw_hrow
    add di, bx
    mov [es:di], dl
    jmp short .rn
.cg:
    mov dl, al                      ; eight pixels in colour 3: a word
    xor ax, ax
    mov cx, 8
.x:
    shl ax, 1
    shl ax, 1
    shl dl, 1
    jnc .x0
    or al, 3
.x0:
    loop .x
    xchg al, ah                     ; the left pixels' byte first
    pop bx
    pop cx
    push cx
    push bx
    push ax
    mov ax, RW_CTXT
    add ax, cx
    mov di, ax
    and di, 1
    mov cl, 13
    shl di, cl
    shr ax, 1
    mov cx, 80
    mul cx
    add di, ax
    shl bx, 1
    add di, bx
    add di, 10                      ; the line starts 20 pixels in... x 4
    pop ax
    mov [es:di], ax
.rn:
    pop bx
    pop cx
    inc cx
    cmp cx, 8
    jb .r
    pop si
    inc bx
    cmp bx, 16
    jb .c
    ret

