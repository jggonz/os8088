; =============================================================================
; apps/isotron/itfs.inc - full screen (SPEC.md 108.12)
;
; THE SAME CANVAS, A DIFFERENT PRESENTER, AND NO WORKER. F / f, Alt+Enter or
; Game > Full Screen calls OSAPI_FSX_RUN from the UI task with the lock held
; (SPEC.md 53.1); the worker is frozen for the bracket (no FSXF_KEEPWORKER:
; "two loops driving one screen is two writers", SPEC.md 93), and THIS loop
; runs the game - the same it_commands, it_flow and governor the worker runs,
; on FSXF_FASTTICK's sub-tick clock (SPEC.md 53.2.1, 108.12.4). Nothing about
; the game is touched by a toggle: the grid, the canvas, the RNG, the
; iteration and the score are the same bytes on both sides of it, and V = now
; on both sides, so no burst follows one.
;
; THE MODE IS THE WINDOW'S DISPLAY'S (OSAPI_FSX_CAPS DL, SPEC.md 53.7.1),
; asked again every time, because a window moves between displays:
;
;   CGA       FSXM_CGA320, palette 1 at LOW intensity through int 10h AH=0Bh
;             (never port 3D9h): the 1982 look, 1:1. C toggles high intensity.
;   VGA       FSXM_VGA0D, 320 x 200 x 16 planar, 1:1, the BIOS's default
;             palette (no DAC write): the isoTron look; C gives Classic.
;   HERCULES  FSXM_HERC, the 1bpp pair patterns at 2x across by 1.5 down,
;             640 x 300 centred at card (40, 24).
;   EGA       NO MODE: a same-mode bracket on the desktop's own 640 x 350 x 16
;             (SPEC.md 53.7 grants the drawing slots to it), presented through
;             the kernel's gfx_blitp by the windowed colour presenter at 2x by
;             1.75 at OSAPI_FSX_SURF's origin, once a tick. FSXM_CGA320 when
;             that rect is smaller than 640 x 350 or the probe refuses.
;
; A STEP IS ONE COLUMN ON THE GLASS TOO (SPEC.md 108.10): itcanvas.inc's
; it_span ends in `jmp [it_spanhk]`, which on CGA, VGA and Hercules is THIS
; file's span hook for the surface - the new wall's top pixel and face rows
; written straight into video memory, one read-modify-write a device row
; through an unrolled run with fixed displacements (no per-pixel loop, no
; branch a row, nothing written twice), and on EGA the windowed dirty box.
; A whole present - on entry, a new arena, a look change - is the canvas
; through per-surface tables minus the overlay text cells, then the text.
;
; Register contract: DS = the package unless a routine says; the bracket's
; game calls run with ES = the work claim, as the worker's do.
; =============================================================================

FSK_NONE    equ 0                   ; it_fsk: the bracket's surface
FSK_CGA     equ 1
FSK_VGA     equ 2
FSK_HERC    equ 3
FSK_EGA     equ 4
KSC_F       equ 0x21                ; F's make code (the entry's release wait)
IT_HX       equ 5                   ; Hercules: the picture's first card byte
IT_HY       equ 24                  ; ...and its first card row (centred)
IT_HROWS    equ 300                 ; ...and its card rows: 200 x 1.5
IT_EGAH     equ 350                 ; EGA: 200 logical rows x 1.75
IT_FCELLS   equ 40                  ; a full-screen row of 8-px text cells

; THE FULL-SCREEN TABLES live in the windowed presenters' STRIP (itwin.inc):
; nothing windowed is presented while a bracket is up - the worker is frozen
; and no W_PAINT can run - so its 5,120 bytes are free, and the claim stays
; 30 KB. (EGA uses the windowed colour tables and the strip as the windowed
; renderer does, and puts nothing here.)
IT_FTA      equ IT_STRIP            ; CGA: a canvas byte -> pixels 0, 1 of a
IT_FTB      equ IT_STRIP + 256      ; device byte (TA) or pixels 2, 3 (TB)
IT_FV       equ IT_STRIP            ; VGA: eight 512-byte c2p word tables
IT_FVCOL    equ IT_STRIP + 4096     ; VGA: a canvas byte's colour (the span's)
IT_FBUF     equ IT_STRIP + 4352     ; rows: VGA 4 x 40, CGA 2 x 80, Herc 3 x 80
IT_HYA      equ IT_STRIP            ; Hercules, a word a logical row y: the
IT_HYB      equ IT_STRIP + 402      ; address of card row c(y) and c(y) + 1,
IT_HC       equ IT_STRIP + 804      ; c(y) itself, and the face run's m = 0
IT_HYP      equ IT_STRIP + 1206     ; point for c(y)'s bank phase (it_fs_herc_tabs)
%if IT_HYP + 402 > IT_FBUF
  %error "the Hercules row tables overrun the row buffer"
%endif
%if IT_FBUF + 240 > IT_STRIP + IT_STRIPSZ
  %error "the full-screen tables and row buffer do not fit the strip"
%endif
%if IT_MLUT & 1023
  %error "the Hercules span reads the mono pages with BL = the byte: 1,024-aligned"
%endif

; the full-screen overlay records (it_ovf), in 8 x 8 logical cells of a
; 40 x 25 grid: the HUD's left line, the title's five lines (wave 4), the
; message, the HUD's right line - in ROW order, which it_fs_area relies on
; (SPEC.md 108.12.5, 108.13.1). The HUD and the title's lines never show at
; once, so the left HUD sharing rows with none of them is all the order needs
OVF_ROW     equ 0
OVF_C0      equ 1
OVF_LEN     equ 2
OVF_STR     equ 4
OVF_SIZE    equ 6
OVF_L       equ 0
OVF_T       equ OVF_SIZE            ; the title's lines, rows 1 .. 5
OVF_M       equ (1 + IT_TLINES) * OVF_SIZE
OVF_R       equ OVF_M + OVF_SIZE
OVF_N       equ 3 + IT_TLINES
OVF_LROW    equ 0
OVF_TROW    equ 1
%if OVF_T != OVF_TROW * OVF_SIZE
  %error "it_ovf_set takes a title line's row from its record's index"
%endif
OVF_MROW    equ 23
OVF_RROW    equ 24

; -----------------------------------------------------------------------------
; it_fs_pick - which full screen the window's display offers (SPEC.md 53.7.1:
; FSX_CAPS DL is THAT display's kind, never OSAPI_VIDEO's primary).
; out: AL = FSK_*, CF = 1 none. Clobbers AX, BX, DX.
; -----------------------------------------------------------------------------
it_fs_pick:
    mov bx, [it_win]
    call OSAPI_FSX_CAPS             ; AX = the settable ids, DL = the kind
    mov bx, ax
    mov al, FSK_VGA
    cmp dl, VID_VGA
    jne .c
    test bx, 1 << FSXM_VGA0D
    jnz .yes
    jmp short .no
.c:
    mov al, FSK_CGA
    cmp dl, VID_CGA
    jne .h
    test bx, 1 << FSXM_CGA320
    jnz .yes
    jmp short .no
.h:
    mov al, FSK_HERC
    cmp dl, VID_HERC
    jne .e
    test bx, 1 << FSXM_HERC
    jnz .yes
    jmp short .no
.e:
    mov al, FSK_EGA                 ; the same-mode bracket needs no caps bit
    cmp dl, VID_EGA                 ; (no mode is set); its fallback does, and
    je .yes                         ; it_fs_setup asks for it there
.no:
    mov al, FSK_NONE
    stc
    ret
.yes:
    clc
    ret

; -----------------------------------------------------------------------------
; it_fs_menu - the Game menu's Full Screen and Colours items, live: greyed with
; their reason (SPEC.md 47) when the window's display offers no full screen,
; or the windowed picture is mono. From every paint (a drag is one), so the
; item answers for the display the window is on. Preserves every register.
; -----------------------------------------------------------------------------
it_fs_menu:
    push ax
    push bx
    push dx
    mov word [it_i_game + 2 * IT_MI_FULL], it_it_full
    call it_fs_pick
    jnc .col
    mov word [it_i_game + 2 * IT_MI_FULL], it_it_fulln
.col:
    mov word [it_i_game + 2 * IT_MI_COL], it_it_col
    cmp byte [it_surf], 0
    jne .out
    mov word [it_i_game + 2 * IT_MI_COL], it_it_coln
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_fs_go - INTO full screen, from W_ONKEY or AM_ONCMD (the UI task, the
; lock held: OSAPI_FSX_RUN's contract). Returns when the bracket has ended
; and the kernel has repainted the desktop - our W_PAINT among it - so the
; window is the canvas again. Preserves every register.
;
; The worker cannot be mid-step here: it changes the game only inside its
; own lock hold, and we hold the lock. It is frozen for the bracket and
; resumes after it with V = now (it_fs_exit).
; -----------------------------------------------------------------------------
it_fs_go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [it_nomem], 0
    jne .out
    cmp byte [it_fsm], 0
    jne .out
    call it_fs_pick
    jc .out                         ; the menu item is greyed for this too
    call it_bld_sync                ; the worker frozen in a build owed or
    jc .out                         ; half done: it is finished first (itrules)
    mov ax, it_fs_main
    mov bx, [it_win]
    mov cx, FSXF_FASTTICK           ; 54.6 sub-ticks a second: the frame clock
    call OSAPI_FSX_RUN
    IT_MARK it_fsback               ; (the way back's end: SPEC.md 108.12.7)
    mov byte [it_fsret], 0          ; (a paint that never came: nothing owed)
    call it_clock_sync              ; V = now HERE, after the kernel's exit mode
    inc word [it_fsout]             ; set and repaint (SPEC.md 108.12.6): the
                                    ; worker's first wake owes nothing for them
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; it_fs_main - THE BRACKET (SPEC.md 53.1, 108.12.4). SI = the window,
; ES = KERNEL_SEG, DS = CS = ours, the lock held for the whole session.
;
;   drain int 16h -> it_fs_key; os88alt_edge
;   ES = the work claim: it_commands, it_flow (the governor; each step's
;     span hook writes the glass)
;   it_fs_present: a whole present when one is owed, else the overlays that
;     changed (EGA: and the dirty boxes, once a tick)
;   FSXW_FRAME, then the sub-tick clock
; =============================================================================
it_fs_main:
    cld
    OS88_ALTENTER_SEED              ; the Alt+Enter that got us here is held
    mov byte [it_fsm], 1
    mov byte [it_fsq], 0
    mov byte [it_fsega], 0
    ; THE KEY THAT GOT US HERE IS STILL DOWN, and a BIOS mode set would mask
    ; its break (Excitebike's xb_fullscreen): wait for Enter and F to be up
.rel:
    mov al, KSC_ENTER
    call OSAPI_KEY_DOWN
    jc .relw
    mov al, KSC_F
    call OSAPI_KEY_DOWN
    jnc .set
.relw:
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jmp short .rel
.set:
    call it_fs_setup
    jc .leave                       ; nothing to show after all: home again
    call it_fs_reset
.frame:
    mov ah, 1                       ; THE KEYS, drained every frame (SPEC.md
    int 0x16                        ; 67.11.2): no events are dispatched here
    jz .nokey
    xor ah, ah
    int 0x16
    call it_fs_key
    jmp short .frame
.nokey:
    call os88alt_edge               ; Alt+Enter, which no XT BIOS enqueues
    jc .leave
    cmp byte [it_fsq], 0
    jne .leave
    IT_MARK it_fsf0                 ; (a frame's work starts: SPEC.md 108.12.7)
    mov es, [it_seg]                ; the game, exactly as the worker runs it
    call it_commands
    call it_flow
    call it_fs_present
    IT_MARK it_fs_frame
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    call it_fs_sub                  ; one more sub-tick has passed
    jmp short .frame
.leave:
    IT_MARK it_fsx0                 ; (the way back's start)
    call it_fs_exit
    ret

; -----------------------------------------------------------------------------
; it_fs_key - one keystroke in the bracket, AX as int 16h gave it. F / f and
; Alt+Enter leave; Esc ENDS THE GAME first and leaves only at the title
; (SPEC.md 11.2.1: Esc keeps meaning cancel first); everything else is the
; window's own key map (itinput.inc it_key). Clobbers AX, BX.
; -----------------------------------------------------------------------------
it_fs_key:
    cmp ax, KEY_ALTENTER            ; (an enhanced ROM's code, if it gave one)
    je .leave
    cmp ah, KSC_ESC
    jne .nesc
    cmp byte [it_state], ST_TITLE
    je .leave
    mov byte [it_qesc], 1           ; a round or its aftermath: the game ends,
    ret                             ; and the title stays full screen
.nesc:
    or al, al
    jz .key                         ; an extended key: tested on AH, never
    mov bl, al                      ; folded (SPEC.md 67.11.1)
    or bl, 0x20
    cmp bl, 'f'
    je .leave
.key:
    jmp it_key
.leave:
    mov byte [it_fsq], 1
    ret

; -----------------------------------------------------------------------------
; it_fs_setup - the mode, the palette, the tables and the row offsets for the
; window's display. out: CF = 1 nothing to show (the bracket simply ends).
; -----------------------------------------------------------------------------
it_fs_setup:
    call it_fs_pick                 ; asked again, now: the window is where it
    jc .no                          ; is, and THAT display is the bracket's
    mov [it_fsk], al
    cmp al, FSK_EGA
    jne .mode
    ; --- EGA: the rect this bracket owns, 640 x 350 at its origin -----------
    call OSAPI_FSX_SURF             ; AX = x, BX = y, CX = w, DX = h
    jc .cga
    cmp cx, 640
    jb .cga
    cmp dx, IT_EGAH
    jb .cga
    push ax
    push bx
    mov cx, 640
    mov dx, IT_EGAH
    mov di, 0xC000                  ; gfx_blitp's probe: will it take this rect?
    call OSAPI_GFX_BLITP
    pop bx
    pop ax
    jc .cga
    call it_fs_ega_geom             ; (its text is font_run's; the glyph table
    jmp .gl                         ; is asked anyway, for the harness's check)
.cga:
    mov bx, [it_win]                ; THE FALLBACK: four colours beat none
    call OSAPI_FSX_CAPS
    test ax, 1 << FSXM_CGA320
    jz .no
    mov byte [it_fsk], FSK_CGA
.mode:
    mov al, FSXM_CGA320
    cmp byte [it_fsk], FSK_CGA
    je .set
    mov al, FSXM_VGA0D
    cmp byte [it_fsk], FSK_VGA
    je .set
    mov al, FSXM_HERC
.set:
    push ds
    pop es
    mov di, it_fsi
    call OSAPI_FSX_MODE             ; the screen is cleared by the mode set
    jc .no
    mov ax, [it_fsi + FSI_SEG]
    mov [it_fseg], ax
    mov es, [it_seg]
    cmp byte [it_fsk], FSK_CGA
    jne .v
    call it_cga_pal
    call it_fs_lut_cga
    mov word [it_fsrect], it_fr_cga
    mov word [it_fsglyph], it_gl_cga
    mov word [it_fshook], it_hk_cga
    jmp short .fonts
.v:
    cmp byte [it_fsk], FSK_VGA
    jne .hg
    call it_fs_lut_vga
    call it_vga_span                ; write mode 0, set/reset on: the spans'
    mov word [it_fsrect], it_fr_vga
    mov word [it_fsglyph], it_gl_vga
    mov word [it_fshook], it_hk_vga
    jmp short .fonts
.hg:
    call it_fs_lut_herc
    mov word [it_fsrect], it_fr_herc
    mov word [it_fsglyph], it_gl_herc
    mov word [it_fshook], it_hk_herc
.fonts:
    call it_fs_rows
    cmp byte [it_fsk], FSK_HERC
    jne .gl
    call it_fs_herc_tabs
.gl:
    call OSAPI_FONT_GLYPHS          ; DX:SI = the 8 x 8 glyphs, AL..AH
    mov [it_gseg], dx
    mov [it_goff], si
    mov [it_gfirst], ax
    clc
    ret
.no:
    stc
    ret

; it_fs_ega_geom - the windowed colour presenter's geometry, for the bracket:
; the origin OSAPI_FSX_SURF gave (AX, BX), 80 bytes across and Hc = 350
; (2x by 1.75), the row maps and the overlays' rows. it_fs_exit has the
; window re-cut its own by the next paint. Clobbers AX..DX, SI.
it_fs_ega_geom:
    mov [it_ox], ax
    mov [it_oy], bx
    mov word [it_pwb], IT_PWBMAX
    mov word [it_hc], IT_EGAH
    call it_rowmaps
    mov byte [it_surf], 1
    mov byte [it_fsega], 1
    mov word [it_fshook], it_span_box ; EGA's steps are boxes, once a tick
    ret

; it_fs_rows - it_fsrow: each surface's device row (Hercules: card row of the
; picture) -> its first byte in video memory. Clobbers AX..DX, SI.
it_fs_rows:
    xor si, si                      ; SI = the row
    mov cx, IT_CROWS
    cmp byte [it_fsk], FSK_HERC
    jne .l
    mov cx, IT_HROWS
.l:
    mov al, [it_fsk]
    cmp al, FSK_CGA
    jne .nc
    mov ax, si                      ; CGA: (y & 1) * 2000h + (y >> 1) * 80
    shr ax, 1
    mov dx, 80
    mul dx
    test si, 1
    jz .st
    add ax, 0x2000
    jmp short .st
.nc:
    cmp al, FSK_VGA
    jne .nh
    mov ax, si                      ; VGA 0Dh: y * 40
    mov dx, 40
    mul dx
    jmp short .st
.nh:
    mov bx, si                      ; Hercules: card row R = 24 + r:
    add bx, IT_HY                   ; (R & 3) * 2000h + (R >> 2) * 90 + 5
    mov ax, bx
    shr ax, 1
    shr ax, 1
    mov dx, 90
    mul dx
    add ax, IT_HX
    and bx, 3
    mov dh, bl
    mov dl, 0
    shl dh, 1
    shl dh, 1
    shl dh, 1
    shl dh, 1
    shl dh, 1                       ; DX = (R & 3) * 2000h
    add ax, dx
.st:
    mov bx, si
    shl bx, 1
    mov [it_fsrow + bx], ax
    inc si
    cmp si, cx
    jb .l
    ret

; it_fs_reset - the game's side of an entry: a whole present owed, the turn
; FIFO and the key counts empty (SPEC.md 108.12.6), the clock synced (V = now)
it_fs_reset:
    mov byte [it_full], 1
    mov byte [it_byield], 0
    mov al, [it_fhead]
    mov [it_ftail], al
    mov byte [it_kboost], 0
    mov byte [it_kf9], 0
    mov word [it_spanhk], it_span_box ; until the first whole present is out
    call it_fs_resub
    call it_clock_sync
    ret

; it_fs_resub - the sub-tick clock from the tick now (phase 0)
it_fs_resub:
    push ax
    call OSAPI_GET_TICKS
    mov [it_fstk], ax
    mov [it_fsptk], ax
    mov byte [it_fsph], 0
    pop ax
    ret

; it_fs_sub - after each FSXW_FRAME: one more sub-tick of the tick, at most
; two (FSX_SUBTICK = 3 a tick); a tick that moved starts phase 0 again. The
; governor's `now` is ticks * 768 + phase * 256 (SPEC.md 108.4.2, 108.12.4).
it_fs_sub:
    push ax
    call OSAPI_GET_TICKS
    cmp ax, [it_fstk]
    je .same
    mov [it_fstk], ax
    mov byte [it_fsph], 0
    pop ax
    ret
.same:
    cmp byte [it_fsph], FSX_SUBTICK - 1
    jae .out
    inc byte [it_fsph]
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_fs_exit - home: the windowed side of the toggle. The kernel's exit mode
; set restores the card and its wm_paint_all is our W_PAINT of the canvas,
; so all that is left is the window's own state: the steps' hook back to the
; dirty box, the overlay records composed for the window (or, after EGA, its
; geometry re-cut by that paint), nothing owed twice, the FIFO and the key
; counts empty. After a mode set the paint OWES the picture rather than
; making it (it_fsret, SPEC.md 108.12.6), and V = now is taken once
; OSAPI_FSX_RUN has returned (it_fs_go), so the way back is not owed.
; -----------------------------------------------------------------------------
it_fs_exit:
    mov word [it_spanhk], it_span_box
    mov byte [it_fsm], 0
    mov byte [it_full], 0
    mov byte [it_fsret], 1          ; the exit paint OWES the window's whole
                                    ; picture, in bands (itwin.inc it_paint_win)
    mov byte [it_byield], 0
    mov byte [it_hudd], 0
    mov byte [it_msgd], 0
    call it_box_clear
    cmp byte [it_fsega], 0
    je .win
    mov byte [it_laid], 0           ; the bracket borrowed the windowed geometry
    mov word [it_fw], 0             ; (the worker's frame cache, asked again)
    jmp short .k
.win:
    cmp byte [it_laid], 0
    je .k
    call it_ovl_compose             ; the paint after this letters what is now
.k:
    mov al, [it_fhead]
    mov [it_ftail], al
    mov byte [it_kboost], 0
    mov byte [it_kf9], 0
    ret                             ; (V = now: it_fs_go, once OSAPI_FSX_RUN is
                                    ; back - the way back is behind it)

; -----------------------------------------------------------------------------
; it_cga_pal - palette 1 through the BIOS (Clear Skies' cs_cga_pal: an EGA or
; VGA in mode 4 has no port 3D9h): BH = 1 BL = 1 the palette, BH = 0 the
; background black with bit 4 the intensity - LOW, the 1982 look, until C
; -----------------------------------------------------------------------------
it_cga_pal:
    push ax
    push bx
    mov ax, 0x0B00
    mov bx, 0x0101
    int 0x10
    mov ax, 0x0B00
    xor bx, bx
    cmp byte [it_cgahi], 0
    je .lo
    mov bl, 0x10
.lo:
    int 0x10
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; THE TABLES, ES = the work claim. Each ~0.02 - 0.05 s, at entry and on C.
; -----------------------------------------------------------------------------

; it_fs_lut_cga - TA[b] = ink(b >> 4, even) << 6 | ink(b & 15, odd) << 4 and
; TB[b] = TA[b] >> 4, from it_cgaink; and each pixel position's TOP bits for
; the span. A canvas byte with one nibble set (a column's) indexes the same
; tables: TA[id << 4] is an even column's bits at position 0, TA[id] an odd
; one's at position 1 (VOID's ink is 0 in every look).
it_fs_lut_cga:
    xor bx, bx
.b:
    mov si, bx
    mov cl, 4
    shr si, cl
    mov al, [it_cgaink + si]
    and al, 0x30                    ; ink(e) << 4 (the even half's, in bits 5-4)
    shl al, 1
    shl al, 1                       ; << 6
    mov si, bx
    and si, 15
    mov ah, [it_cgaink + si]
    and ah, 0x03
    shl ah, cl                      ; ink(o) << 4
    or al, ah
    mov [es:bx + IT_FTA], al
    shr al, cl
    mov [es:bx + IT_FTB], al
    inc bx
    cmp bx, 256
    jb .b
    ; THE TOP's bits a position: even columns index ID_TOP << 4, odd ID_TOP
    mov al, [es:IT_FTA + (ID_TOP << 4)]
    mov [it_hktop], al
    mov al, [es:IT_FTA + ID_TOP]
    mov [it_hktop + 1], al
    mov al, [es:IT_FTB + (ID_TOP << 4)]
    mov [it_hktop + 2], al
    mov al, [es:IT_FTB + ID_TOP]
    mov [it_hktop + 3], al
    ret

; it_fs_lut_herc - the span's TOP bits a position and card-row parity, out
; of the windowed mono pages (itwin.inc: HI and LO a parity, 512 apart)
it_fs_lut_herc:
    xor si, si                      ; SI = the position * 2
.p:
    mov bx, [it_hkhb + si]          ; the position's page, parity 0
    mov al, ID_TOP << 4             ; an even column's form of the byte...
    test si, 2
    jz .ev
    mov al, ID_TOP                  ; ...an odd one's
.ev:
    mov bl, al
    mov al, [es:bx]
    mov ah, [es:bx + 512]
    mov [it_hktop + si], ax         ; AL: an even card row, AH: an odd one
    add si, 2
    cmp si, 8
    jb .p
    ret

; it_fs_herc_tabs - the span's per-row tables (it_fs_rows first): for each
; logical row y = 0..200, c(y) = y + (y + 1) / 2 (its first card row: 1.5
; card rows a row), the addresses of card rows c(y) and c(y) + 1, and the
; face run (its m = 0 point) for c(y)'s bank phase - so a span looks its rows up
; rather than working them out (~300 cycles a step, SPEC.md 108.12.7)
it_fs_herc_tabs:
    xor si, si                      ; SI = y
.y:
    mov ax, si
    inc ax
    shr ax, 1
    add ax, si                      ; AX = c(y)
    mov bx, si
    shl bx, 1
    mov [es:bx + IT_HC], ax
    xor dx, dx                      ; (row 200 has no card row: 0)
    xor cx, cx
    cmp ax, IT_HROWS
    jae .st
    mov di, ax
    shl di, 1
    mov dx, [it_fsrow + di]
    cmp ax, IT_HROWS - 1
    jae .st
    mov cx, [it_fsrow + di + 2]
.st:
    mov [es:bx + IT_HYA], dx
    mov [es:bx + IT_HYB], cx
    and ax, 3                       ; the phase (24 is a multiple of 4)
    mov di, ax
    shl di, 1
    mov ax, [it_hgtp + di]
    mov [es:bx + IT_HYP], ax
    inc si
    cmp si, IT_CROWS
    jbe .y
    ret

; it_fs_lut_vga - the chunky-to-planar tables: for each pair position q
; (0..3: pixels 2q, 2q + 1 of a plane byte), two 256-WORD tables at IT_FV +
; q * 1,024 - T01 (planes 0, 1: the low byte plane 0) and, 512 on, T23
; (planes 2, 3) - entry b canvas byte b's two pixels' bits there; and
; IT_FVCOL[b], a column's byte's colour for the span, with each parity's TOP
; colour. From the look in force ([it_lookp]). tools/isotronsim.py lut_vga_mem
; is this layout, built the same way.
it_fs_lut_vga:
    xor bp, bp                      ; BP = b
    mov si, [it_lookp]
.b:
    mov bx, bp
    mov cl, 4
    shr bx, cl
    mov al, [si + bx]
    shr al, cl                      ; AL = ce (b >> 4 at an even x)
    mov bx, bp
    and bx, 15
    mov ah, [si + bx]
    and ah, 15                      ; AH = co (b & 15 at an odd x)
    mov dl, ah                      ; the span's colour: the nibble it has
    test bp, 0xF0
    jz .o
    mov dl, al
.o:
    mov [es:bp + IT_FVCOL], dl
    mov di, bp
    shl di, 1
    add di, IT_FV                   ; q = 0's T01 entry
    xor bx, bx                      ; BX = the plane's offset: 0, 1, 512, 513
.pl:
    xor dl, dl                      ; v = ce bit p << 1 | co bit p
    shr al, 1
    rcl dl, 1
    shr ah, 1
    rcl dl, 1
    mov dh, dl                      ; q = 3: bits 1-0
    mov [es:di + bx + 3 * 1024], dh
    shl dh, 1
    shl dh, 1
    mov [es:di + bx + 2 * 1024], dh ; q = 2: bits 3-2
    shl dh, 1
    shl dh, 1
    mov [es:di + bx + 1024], dh     ; q = 1: bits 5-4
    shl dh, 1
    shl dh, 1
    mov [es:di + bx], dh            ; q = 0: bits 7-6
    inc bx
    cmp bx, 2
    jne .p2
    mov bx, 512                     ; planes 2, 3: the T23 table
.p2:
    cmp bx, 514
    jb .pl
    inc bp
    cmp bp, 256
    jb .b
    mov al, [es:IT_FVCOL + (ID_TOP << 4)]
    mov [it_fvtop], al
    mov al, [es:IT_FVCOL + ID_TOP]
    mov [it_fvtop + 1], al
    ret

; it_vga_span / it_vga_plain - the Graphics Controller's two states (SPEC.md
; 108.12.8). SPAN: write mode 0, set/reset ENABLED on all four planes, map
; mask 0Fh - a CPU write puts the set/reset colour under the bit mask and the
; latches elsewhere, so a span row is one `xchg` (a read loads the latches).
; PLAIN: set/reset off, bit mask FFh - a whole present writes plane bytes
; under the map mask, and a text byte to all four planes is ink 15 on 0.
; Preserve every register.
it_vga_span:
    push ax
    push dx
    mov dx, 0x3CE
    mov ax, 0x0F01                  ; enable set/reset: all planes
    out dx, ax
    mov dx, 0x3C4
    mov ax, 0x0F02                  ; map mask: all planes
    out dx, ax
    pop dx
    pop ax
    ret

it_vga_plain:
    push ax
    push dx
    mov dx, 0x3CE
    mov ax, 0x0001                  ; enable set/reset: none
    out dx, ax
    mov ax, 0xFF08                  ; bit mask: every bit
    out dx, ax
    mov dx, 0x3C4
    mov ax, 0x0F02
    out dx, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_look_key - C: the next look (SPEC.md 108.12.2). On a CGA in full screen
; the intensity, through the BIOS - no table, no present; on a 16-colour
; surface isoTron <-> Classic, the tables rebuilt and a whole present owed
; (a present, never a redraw of the game); on a mono one nothing. Lock held
; (W_ONKEY) or the bracket. Preserves every register.
; -----------------------------------------------------------------------------
it_look_key:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [it_fsm], 0
    jne .fs
    cmp byte [it_surf], 0           ; WINDOWED: a colour picture only
    je .out
    mov byte [it_full], 1           ; the worker's whole present
    call it_look_flip
    jmp short .out
.fs:
    mov al, [it_fsk]
    cmp al, FSK_HERC
    je .out
    cmp al, FSK_CGA
    jne .c16
    xor byte [it_cgahi], 1
    call it_cga_pal
    jmp short .out
.c16:
    mov byte [it_full], 1           ; owed FIRST: whoever reads it_clook reads
    call it_look_flip               ; a present that has not gone out yet
    cmp byte [it_fsk], FSK_VGA
    jne .out
    mov es, [it_seg]
    call it_fs_lut_vga
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; it_look_flip - isoTron <-> Classic, and the windowed colour tables with it
; (EGA's full screen reads those too). Clobbers AX..DI, BP, ES.
it_look_flip:
    xor byte [it_clook], 1
    mov word [it_lookp], it_col
    cmp byte [it_clook], 0
    je .t
    mov word [it_lookp], it_colc
.t:
    mov es, [it_seg]
    call it_lut_col
    ret

; =============================================================================
; PRESENTING (SPEC.md 108.12.5)
; =============================================================================

; -----------------------------------------------------------------------------
; it_fs_present - a frame's glass: the whole picture when one is owed (entry,
; a new arena, a look), else the overlays that changed - and on EGA the
; steps' dirty boxes, once a tick. Clobbers everything but DS.
; -----------------------------------------------------------------------------
it_fs_present:
    cmp byte [it_full], 0
    je .part
    cmp byte [it_state], ST_SWEEP   ; a whole present owed ends a sweep: it
    jne .w                          ; shows the arena whole (itsweep.inc)
    call it_sw_finish
.w:
    call it_wait_ol                 ; (WAIT's arena: whole, outline and all)
    IT_MARK it_fsw0
    call it_fs_whole
    IT_MARK it_fsw1
    mov byte [it_full], 0
    mov byte [it_hudd], 0
    mov byte [it_msgd], 0
    call it_box_clear               ; (a rebuild's spans: the present had them)
    inc word [it_fsn]
    mov ax, [it_fshook]             ; the steps write the glass from here on
    mov [it_spanhk], ax
    call it_fs_resub                ; and what the present cost is not owed
    call it_clock_sync              ; (SPEC.md 108.4.2: V = now)
    ret
.part:
    cmp byte [it_fsk], FSK_EGA
    je .ega
    mov al, [it_hudd]
    or al, [it_msgd]
    jz .sw
    call it_fs_ovl_update
.sw:
    cmp byte [it_state], ST_SWEEP   ; THE SWEEP: the items due, all of them -
    jne .out                        ; no lock to let go here (itsweep.inc)
    call it_vga_plain_ck
    call it_sw_run
    call it_vga_span_ck
.out:
    ret
.ega:
    cmp byte [it_surf], 0           ; a gfx_blitp that refused after its probe
    jne .e1                         ; said yes (itwin.inc): there is no EGA
    mov byte [it_fsq], 1            ; full screen without it - home
    ret
.e1:
    call it_ovl_update              ; (nothing when nothing changed)
    cmp byte [it_state], ST_SWEEP
    jne .e2
    call it_sw_run                  ; (through gfx_blitp at its geometry)
.e2:
    call OSAPI_GET_TICKS
    cmp ax, [it_fsptk]
    je .out
    mov [it_fsptk], ax
    call it_box_any
    jnc .out
    ; THE PRESENTATION GOVERNOR, the windowed one's rule (SPEC.md 108.11.6,
    ; 108.12.4): the same presenter at the same load needed it windowed
    ; (Lunatic h4 on the XT-VGA). A tick's first frame presents; one whose
    ; boxes run it into the NEXT tick (logic and boxes over ~37 ms of the 55)
    ; presents every second tick from then on - the steps' boxes union in
    ; the meantime - and 32 presents that end in their own tick put it back.
    ; The tick and not pit_now: FASTTICK runs PIT channel 0 at 3x, and
    ; os88pit.inc's clock is one 65,536-count period a tick (SPEC.md 53.2.1)
    cmp byte [it_gskip], 0
    je .eb
    xor byte [it_godd], 1
    jnz .out
.eb:
    call it_present_boxes
    call OSAPI_GET_TICKS
    cmp ax, [it_fsptk]
    je .eq
    mov byte [it_gskip], 1          ; into the next tick: engage
    jmp short .eg
.eq:
    cmp byte [it_gskip], 0
    je .out
    inc byte [it_gquiet]
    cmp byte [it_gquiet], IT_GQUIET
    jb .out
    mov byte [it_gskip], 0
.eg:
    mov byte [it_gquiet], 0
    ret

; -----------------------------------------------------------------------------
; it_fs_whole - the whole picture minus the overlays' text cells, then the
; text: every device byte once. Clobbers everything but DS.
; -----------------------------------------------------------------------------
it_fs_whole:
    cmp byte [it_fsk], FSK_EGA
    jne .direct
    call it_ovl_compose             ; EGA: the windowed whole present, at the
    mov word [it_pr0], 0            ; bracket's geometry, through gfx_blitp,
    mov ax, [it_hc]                 ; and the overlays through font_run - the
    mov [it_pr1], ax                ; drawing slots are this bracket's (53.7)
    mov word [it_pj0], 0
    mov ax, [it_pwb]
    dec ax
    mov [it_pj1], ax
    call it_pbox
    call it_ovl_draw_all
    ret
.direct:
    call it_fs_ovl_compose
    call it_vga_plain_ck
    xor ax, ax                      ; the whole picture minus the text...
    mov [it_fa0], ax
    mov [it_fac0], ax
    mov word [it_fa1], IT_CROWS
    mov word [it_fac1], IT_FCELLS
    call it_fs_area
    xor bx, bx                      ; ...then the text
.tx:
    push bx
    call it_ovf_draw
    pop bx
    add bx, OVF_SIZE
    cmp bx, OVF_N * OVF_SIZE
    jb .tx
    call it_vga_span_ck
    ret

; -----------------------------------------------------------------------------
; it_fs_area - logical rows [it_fa0, it_fa1) (EVEN) by cells [it_fac0,
; it_fac1) of the picture, minus every overlay's text cells: each overlay the
; rect meets cuts it into the rows above it, across; its own rows, left and
; right of its text; and on below - the records in row order (OVF_*). Every
; byte outside the text once. A whole present (it_fs_whole) and a sweep's
; rect (itsweep.inc) alike. Clobbers everything but DS.
; -----------------------------------------------------------------------------
it_fs_area:
    mov ax, [it_fa0]
    mov [it_fab], ax                ; the first row not yet presented
    xor bx, bx
.ov:
    cmp word [it_ovf + bx + OVF_STR], 0
    je .on
    mov al, [it_ovf + bx + OVF_ROW]
    mov ah, 8
    mul ah                          ; AX = its first row
    cmp ax, [it_fa1]
    jae .on                         ; below the rect
    mov dx, ax
    add dx, 8                       ; DX = its end
    cmp dx, [it_fab]
    jbe .on                         ; above what is left of it
    mov cl, [it_ovf + bx + OVF_C0]
    xor ch, ch
    cmp cx, [it_fac1]
    jae .on                         ; right of it
    mov si, cx
    add cl, [it_ovf + bx + OVF_LEN]
    adc ch, 0
    cmp cx, [it_fac0]
    jbe .on                         ; left of it
    push bx
    push cx                         ; the text's cells: [SI, CX)
    push si
    push dx
    cmp ax, [it_fab]
    jbe .own
    mov [it_fy1], ax                ; the rows above it, across
    push ax
    mov ax, [it_fab]
    mov [it_fy0], ax
    call .full
    pop ax
    mov [it_fab], ax
.own:
    pop dx                          ; its own rows, to the rect's end
    cmp dx, [it_fa1]
    jbe .oe
    mov dx, [it_fa1]
.oe:
    mov ax, [it_fab]
    mov [it_fy0], ax
    mov [it_fy1], dx
    mov [it_fab], dx
    pop ax                          ; left of the text
    mov cx, [it_fac0]
    mov [it_fc0], cx
    mov [it_fc1], ax
    call it_fs_rect                 ; (nothing when empty)
    pop ax                          ; right of it
    mov [it_fc0], ax
    mov ax, [it_fac1]
    mov [it_fc1], ax
    call it_fs_rect
    pop bx
.on:
    add bx, OVF_SIZE
    cmp bx, OVF_N * OVF_SIZE
    jb .ov
    mov ax, [it_fab]                ; and on below the last
    mov [it_fy0], ax
    mov ax, [it_fa1]
    mov [it_fy1], ax
.full:
    mov ax, [it_fac0]
    mov [it_fc0], ax
    mov ax, [it_fac1]
    mov [it_fc1], ax
    jmp it_fs_rect

; it_vga_plain_ck / it_vga_span_ck - the GC's state, on VGA alone
it_vga_plain_ck:
    cmp byte [it_fsk], FSK_VGA
    jne .out
    call it_vga_plain
.out:
    ret

it_vga_span_ck:
    cmp byte [it_fsk], FSK_VGA
    jne .out
    call it_vga_span
.out:
    ret

; it_fs_rect - logical rows [it_fy0, it_fy1) by cells [it_fc0, it_fc1) of the
; picture, from the canvas, in the surface's format. Nothing when empty. The
; rows come in PAIRS - every caller's rect is 8-row aligned (it_fs_whole's
; bands and remainders, it_ovf_swap's cells), and the presenters read a
; canvas word, two rows of a column, at a time. Clobbers everything but DS
; and BP: it_fs_whole carries its row cursor in BP across these calls, and
; it_fr_vga counts its cells in BP (SPEC.md 108.12.5 - a VGA present once
; came back with BP = 0 and the next band presented rows [0, 200) again).
it_fs_rect:
    mov ax, [it_fy0]
    cmp ax, [it_fy1]
    jge .none
    mov ax, [it_fc0]
    cmp ax, [it_fc1]
    jge .none
    push bp
    cmp byte [it_pfill], 0          ; VOID (a sweep's fill): zeros, no tables
    jne .fill
    call [it_fsrect]
    pop bp
.none:
    ret
.fill:
    call it_fs_fill
    pop bp
    ret

; it_fs_fill - it_fs_rect's rows and cells as VOID, which is ink 0 on every
; surface and in every look: zero bytes, a `rep stosb` a device row - on
; VGA 0Dh in the PLAIN state (set/reset off, every plane mapped), so a 0 is
; colour 0 in all four planes. Rows: the CGA's and VGA's one a logical row,
; the Hercules' card rows c(fy0) .. c(fy1) (1.5 a row; fy0 is even). A
; cell is 2 bytes on the CGA and the Hercules, 1 on VGA. Clobbers AX..DI.
it_fs_fill:
    mov ax, [it_fy0]
    mov dx, [it_fy1]
    mov cl, 1                       ; CL = bytes a cell
    cmp byte [it_fsk], FSK_VGA
    je .g
    inc cl
    cmp byte [it_fsk], FSK_HERC
    jne .g
    mov bx, ax                      ; card rows: c(y) = y + y / 2, y even
    shr bx, 1
    add ax, bx
    mov bx, dx
    shr bx, 1
    add dx, bx
.g:
    mov si, ax                      ; SI = the first device row, DX its end
    mov ax, [it_fc0]
    mul cl
    mov bx, ax                      ; BX = the first byte in a row
    mov ax, [it_fc1]
    sub ax, [it_fc0]
    mul cl
    mov bp, ax                      ; BP = bytes a row
    push es
    mov es, [it_fseg]
    cld
    xor al, al
.r:
    cmp si, dx
    jae .x
    mov di, si
    shl di, 1
    mov di, [it_fsrow + di]
    add di, bx
    mov cx, bp
    rep stosb
    inc si
    jmp short .r
.x:
    pop es
    ret

; -----------------------------------------------------------------------------
; THE OVERLAYS (SPEC.md 108.12.5): the windowed renderer's three lines, in
; 8 x 8 cells of the logical picture - the HUD's left line at row 0 from
; column 2, the message centred on row 23 (across the slab's front face), the
; HUD's right line on row 24 ending at column 38. During play only the HUD
; shows, and no wall reaches it at any height (108.12.5 has the arithmetic),
; so a span hook never writes under text.
; -----------------------------------------------------------------------------
it_fs_ovl_compose:
    call it_ovl_want                ; AX left, CX message, DX right
    mov bx, OVF_L
    call it_ovf_set
    mov ax, cx
    mov bx, OVF_M
    call it_ovf_set
    mov ax, dx
    mov bx, OVF_R
    call it_ovf_set
    mov bx, OVF_T                   ; the title's lines
.t:
    call it_ovl_tstr
    call it_ovf_set
    add bx, OVF_SIZE
    cmp bx, OVF_M
    jb .t
    ret

; it_ovf_set - record BX gets string AX (0 = none): its length (at most 40)
; and its cells. Preserves all but the record.
it_ovf_set:
    push ax
    push cx
    push si
    mov [it_ovf + bx + OVF_STR], ax
    xor cx, cx
    or ax, ax
    jz .len
    mov si, ax
.l:
    cmp byte [si], 0
    je .len
    inc si
    inc cx
    cmp cx, IT_FCELLS
    jb .l
.len:
    mov [it_ovf + bx + OVF_LEN], cl
    mov al, OVF_LROW
    mov ah, 2                       ; left: from column 2
    cmp bx, OVF_L
    je .set
    mov al, OVF_RROW
    mov ah, IT_FCELLS - 1
    sub ah, cl                      ; right: ending at column 38
    cmp bx, OVF_R
    je .set
    mov al, OVF_MROW
    cmp bx, OVF_M
    je .mid
    mov al, bl                      ; a title line: its row is its record's
    xor ah, ah                      ; index, 1 .. 5 (OVF_T = OVF_SIZE)
    push cx
    mov cl, OVF_SIZE
    div cl
    pop cx
.mid:
    mov ah, IT_FCELLS
    sub ah, cl
    shr ah, 1                       ; the message and the title: centred
.set:
    mov [it_ovf + bx + OVF_ROW], al
    mov [it_ovf + bx + OVF_C0], ah
    pop si
    pop cx
    pop ax
    ret

; it_fs_ovl_update - the overlays that changed (it_hudd, it_msgd): each old
; line's cells from the canvas minus the new line's, then the new text
it_fs_ovl_update:
    call it_vga_plain_ck
    mov al, [it_hudd]
    or al, al
    jz .msg
    mov byte [it_hudd], 0
    mov [it_hmask], al
    call it_ovl_want
    mov [it_hright], dx
    test byte [it_hmask], 1
    jz .r
    mov bx, OVF_L
    call it_ovf_swap
.r:
    test byte [it_hmask], 2
    jz .msg
    mov ax, [it_hright]
    mov bx, OVF_R
    call it_ovf_swap
.msg:
    cmp byte [it_msgd], 0
    je .out
    mov byte [it_msgd], 0
    call it_ovl_want
    mov ax, cx
    mov bx, OVF_M
    call it_ovf_swap
    mov bx, OVF_T                   ; the title's lines go with the message
.t:
    call it_ovl_tstr
    cmp ax, [it_ovf + bx + OVF_STR]
    je .tn                          ; (unchanged: not drawn again)
    push bx
    call it_ovf_swap
    pop bx
.tn:
    add bx, OVF_SIZE
    cmp bx, OVF_M
    jb .t
.out:
    call it_vga_span_ck
    ret

; it_ovf_swap - record BX becomes string AX: the old line's cells that the
; new one does not cover, from the canvas; then the new text
it_ovf_swap:
    mov si, [it_ovf + bx + OVF_STR]
    mov dl, [it_ovf + bx + OVF_C0]  ; DL..DH = the old cells [c0, c1)
    mov dh, dl
    add dh, [it_ovf + bx + OVF_LEN]
    call it_ovf_set
    or si, si
    jz .new
    mov al, [it_ovf + bx + OVF_ROW]
    mov ah, 8
    mul ah
    mov [it_fy0], ax
    add ax, 8
    mov [it_fy1], ax
    mov cl, IT_FCELLS               ; CL..CH = the new cells, or none
    mov ch, cl
    cmp word [it_ovf + bx + OVF_STR], 0
    je .cut
    mov cl, [it_ovf + bx + OVF_C0]
    mov ch, cl
    add ch, [it_ovf + bx + OVF_LEN]
.cut:
    push bx
    push cx
    push dx
    mov al, dl                      ; [oc0, min(oc1, nc0))
    xor ah, ah
    mov [it_fc0], ax
    mov al, dh
    cmp al, cl
    jbe .m1
    mov al, cl
.m1:
    mov [it_fc1], ax
    call it_fs_rect
    pop dx
    pop cx
    push cx
    push dx
    mov al, dl                      ; [max(oc0, nc1), oc1)
    cmp al, ch
    jae .m2
    mov al, ch
.m2:
    xor ah, ah
    mov [it_fc0], ax
    mov al, dh
    mov [it_fc1], ax
    call it_fs_rect
    pop dx
    pop cx
    pop bx
.new:
    call it_ovf_draw
    ret

; it_ovf_draw - record BX's text, if any, opaque
it_ovf_draw:
    mov si, [it_ovf + bx + OVF_STR]
    or si, si
    jz .out
    mov dh, [it_ovf + bx + OVF_ROW]
    mov dl, [it_ovf + bx + OVF_C0]
    mov cl, [it_ovf + bx + OVF_LEN]
    xor ch, ch
    jcxz .out
.c:
    lodsb
    push cx
    push si
    push dx
    call it_fs_glyph                ; AL at cell (DH, DL)
    pop dx
    pop si
    pop cx
    inc dl
    loop .c
.out:
    ret

; it_fs_glyph - character AL at cell row DH, column DL, white on black,
; every byte once: the glyph's 8 bytes off OSAPI_FONT_GLYPHS (a character
; outside the table is a blank cell). Clobbers AX..DI, BP.
it_fs_glyph:
    mov bp, it_zglyph               ; (ES:BP, read by the surface's writer)
    mov bx, ds
    sub al, [it_gfirst]
    jb .z
    mov ah, [it_glast]
    sub ah, [it_gfirst]
    cmp al, ah
    ja .z
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [it_goff]
    mov bp, ax
    mov bx, [it_gseg]
.z:
    mov es, bx
    jmp [it_fsglyph]

it_zglyph:  times 8 db 0

; CGA: a glyph row's 8 bits are 16 pixels' worth of ink 3 or 0 - two bytes,
; nibble by nibble through it_nib2 - on device row 8 * DH + g
it_gl_cga:
    mov al, dh
    mov ah, 8
    mul ah
    mov si, ax                      ; SI = the first row
    mov bl, dl
    xor bh, bh
    shl bx, 1
    mov di, bx                      ; DI = the byte column
    mov cx, 8
    push ds
.r:
    mov bx, si
    shl bx, 1
    mov bx, [cs:it_fsrow + bx]
    add bx, di
    mov al, [es:bp]                 ; the glyph row
    inc bp
    call it_nib2w                   ; AX = its two bytes, left in AL
    mov ds, [cs:it_fseg]
    mov [bx], ax
    push cs
    pop ds
    inc si
    loop .r
    pop ds
    ret

; it_nib2w - AL's 8 bits as 16 two-bit pixels: AL = the high nibble's byte,
; AH = the low one's. Clobbers nothing else.
it_nib2w:
    push bx
    push cx
    mov bl, al
    and bx, 15
    mov ah, [cs:it_nib2 + bx]
    mov bl, al
    mov cl, 4
    shr bl, cl
    mov al, [cs:it_nib2 + bx]
    pop cx
    pop bx
    ret

it_nib2:                            ; 4 bits -> 8, each bit two of them set
    db 0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F
    db 0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF

; Hercules: the same two bytes, on the 1 or 2 card rows of logical row y
; (2 for an even y: 1.5 a row)
it_gl_herc:
    mov al, dh
    mov ah, 8
    mul ah
    mov si, ax                      ; SI = logical row y
    mov bl, dl
    xor bh, bh
    shl bx, 1
    mov di, bx
    mov cx, 8
.r:
    mov al, [es:bp]
    inc bp
    call it_nib2w
    push cx
    mov cx, si                      ; c(y) = y + (y + 1) / 2
    inc cx
    shr cx, 1
    add cx, si
    mov bx, cx
    shl bx, 1
    mov bx, [it_fsrow + bx]
    add bx, di
    push ds
    mov ds, [it_fseg]
    mov [bx], ax
    pop ds
    test si, 1
    jnz .one
    mov bx, cx
    inc bx
    shl bx, 1
    mov bx, [it_fsrow + bx]
    add bx, di
    push ds
    mov ds, [it_fseg]
    mov [bx], ax
    pop ds
.one:
    pop cx
    inc si
    loop .r
    ret

; VGA 0Dh: one byte a glyph row into all four planes (the PLAIN state): ink
; 15 where a bit is set, 0 where not, every plane once
it_gl_vga:
    mov al, dh
    mov ah, 8
    mul ah
    mov si, ax
    mov bl, dl
    xor bh, bh
    mov di, bx
    mov cx, 8
.r:
    mov bx, si
    shl bx, 1
    mov bx, [it_fsrow + bx]
    add bx, di
    mov al, [es:bp]
    inc bp
    push ds
    mov ds, [it_fseg]
    mov [bx], al
    pop ds
    inc si
    loop .r
    ret

; -----------------------------------------------------------------------------
; THE WHOLE-PRESENT RECTS, one per surface: logical rows [it_fy0, it_fy1),
; cells [it_fc0, it_fc1), the rows TWO AT A TIME (it_fs_rect: every rect is
; 8-row aligned). Each converts canvas bytes through the surface's tables into
; IT_FBUF (DS = the work claim, the canvas read through an es: override, as
; itwin.inc's presenters do), then copies each device row to video memory
; with one `rep movsb`: every device byte once.
;
; THE CANVAS IS COLUMN-MAJOR (itcanvas.inc), so a WORD read is rows y and y + 1
; of one column: two output rows share every canvas read, its es: override
; and its disp16. ISOTRON-PLAN 8.3 priced these presents per device byte from
; the table look-ups alone and missed the four overridden disp16 reads under
; each byte - they were most of it (SPEC.md 108.12.7).
; -----------------------------------------------------------------------------

; CGA: a device byte is 4 logical pixels - TA[b0] | TB[b1] - for rows y (AL,
; DL: bank 0) and y + 1 (AH, DH: bank 1) from one word read each of b0, b1
it_fr_cga:
    mov ax, [it_fy0]
    mov [it_fhc], ax                ; the row pair (BP counts the bytes)
.row:
    mov bx, [it_fc0]                ; SI = column 8 * c0, row y, of the canvas
    mov cl, 4
    shl bx, cl                      ; (x * 2: the it_xcol word)
    mov si, [it_xcol + bx]
    add si, [it_fhc]
    mov bp, [it_fc1]
    sub bp, [it_fc0]
    shl bp, 1                       ; BP = device bytes a row
    push ds
    mov es, [it_cseg]
    mov ds, [it_seg]
    mov di, IT_FBUF
    xor bx, bx
.b:
    mov ax, [es:si]                 ; b0 = columns 4j, 4j + 1: rows y, y + 1
    or ax, [es:si + IT_CCOL]
    mov dx, [es:si + 2 * IT_CCOL]   ; b1 = columns 4j + 2, 4j + 3
    or dx, [es:si + 3 * IT_CCOL]
    add si, 4 * IT_CCOL
    mov bl, al
    mov cl, [bx + IT_FTA]
    mov bl, dl
    or cl, [bx + IT_FTB]
    mov [di], cl                    ; row y
    mov bl, ah
    mov cl, [bx + IT_FTA]
    mov bl, dh
    or cl, [bx + IT_FTB]
    mov [di + 80], cl               ; row y + 1
    inc di
    dec bp
    jnz .b
    pop ds
    mov cx, [it_fc1]
    sub cx, [it_fc0]
    shl cx, 1                       ; CX = device bytes a row
    mov ax, [it_fc0]
    shl ax, 1
    mov bx, [it_fhc]
    shl bx, 1
    mov di, [it_fsrow + bx]
    add di, ax
    mov dx, [it_fsrow + bx + 2]
    add dx, ax                      ; DX = row y + 1's first byte
    mov es, [it_fseg]
    push ds
    mov ds, [it_seg]
    mov si, IT_FBUF
    mov bx, cx
    rep movsb
    mov di, dx
    mov si, IT_FBUF + 80
    mov cx, bx
    rep movsb
    pop ds
    add word [it_fhc], 2
    mov ax, [it_fhc]
    cmp ax, [it_fy1]
    jb .row
    ret

; Hercules: the windowed mono pages (HI[b0] | LO[b1] by card-row parity).
; Rows y (even) and y + 1 own card rows c, c + 1 and c + 2 (1.5 a row), so a
; pair is THREE output rows from one word read each of b0, b1: c and c + 2
; have one parity (BH), c + 1 the other - and no row is converted that is not
; shown (an odd row alone owns one card row)
it_fr_herc:
    mov ax, [it_fy0]
    mov [it_fhc], ax                ; the row pair (BP counts the bytes)
.row:
    mov bx, [it_fc0]
    mov cl, 4
    shl bx, cl
    mov si, [it_xcol + bx]
    add si, [it_fhc]
    mov bp, [it_fc1]
    sub bp, [it_fc0]
    shl bp, 1
    mov dx, [it_fhc]                ; c(y) = 1.5 y for an even y
    shr dx, 1
    add dx, [it_fhc]
    push dx
    push ds
    mov es, [it_cseg]
    mov ds, [it_seg]
    mov di, IT_FBUF
    mov bh, IT_MPAGE                ; BH = card row c's parity's pages
    test dl, 1
    jz .b
    mov bh, IT_MPAGE + 2
.b:
    mov ax, [es:si]                 ; b0: rows y (AL), y + 1 (AH)
    or ax, [es:si + IT_CCOL]
    mov dx, [es:si + 2 * IT_CCOL]   ; b1: likewise (DL, DH)
    or dx, [es:si + 3 * IT_CCOL]
    add si, 4 * IT_CCOL
    mov bl, al
    mov cl, [bx]                    ; HI[b0], c's parity
    mov bl, dl
    or cl, [bx + 256]               ; | LO[b1]
    mov [di], cl                    ; card row c: row y
    mov bl, ah
    mov cl, [bx]
    mov bl, dh
    or cl, [bx + 256]
    mov [di + 160], cl              ; card row c + 2: row y + 1
    xor bh, 2                       ; the other parity (1,024-aligned pages)
    mov bl, al
    mov cl, [bx]
    mov bl, dl
    or cl, [bx + 256]
    mov [di + 80], cl               ; card row c + 1: row y again
    xor bh, 2
    inc di
    dec bp
    jnz .b
    pop ds
    pop dx                          ; DX = c
    mov cx, [it_fc1]
    sub cx, [it_fc0]
    shl cx, 1
    mov [it_fhb], cx                ; device bytes a row
    mov ax, [it_fc0]
    shl ax, 1                       ; AX = the byte column
    mov bx, dx
    shl bx, 1                       ; BX = c * 2
    mov es, [it_fseg]
    push ds
    mov ds, [it_seg]
    mov si, IT_FBUF
    mov dx, 3                       ; three card rows, the buffer's in order
.cr:
    mov di, [cs:it_fsrow + bx]
    add di, ax
    mov cx, [cs:it_fhb]
    push si
    rep movsb
    pop si
    add si, 80
    add bx, 2
    dec dx
    jnz .cr
    pop ds
    add word [it_fhc], 2
    mov ax, [it_fhc]
    cmp ax, [it_fy1]
    jb .row
    ret

; VGA 0Dh: a cell is 8 logical pixels, ONE byte of each plane: four canvas
; byte pairs through the c2p tables - a WORD table for planes 0-1 and one for
; planes 2-3 per pair position, indexed by the byte * 2 with AH held 0 - two
; word ORs each, into a row of four plane buffers; then each plane with ONE
; map-mask write and one `rep movsb`. A row at a time (a cell's eight canvas
; reads are already one output byte of each of four planes). Measured on
; MartyPC's XT-VGA: SPEC.md 108.12.7 has the whole present and its cycles a
; cell, most of them the eight overridden canvas reads and the eight table
; words (fetch-bound on the 8088)
it_fr_vga:
    mov ax, [it_fy0]
    mov [it_fhc], ax                ; the row (BP counts the cells)
.row:
    mov bx, [it_fc0]
    mov cl, 4
    shl bx, cl
    mov si, [it_xcol + bx]
    add si, [it_fhc]
    mov bp, [it_fc1]
    sub bp, [it_fc0]
    push ds
    mov es, [it_cseg]
    mov di, [it_fc0]
    mov ds, [it_seg]
    add di, IT_FBUF
    xor ax, ax                      ; AH = 0 for the whole row: AX = the index
.c:
    mov al, [es:si]                 ; b0: columns 8c, 8c + 1
    or al, [es:si + IT_CCOL]
    mov bx, ax
    shl bx, 1
    mov cx, [bx + IT_FV]            ; planes 0, 1
    mov dx, [bx + IT_FV + 512]      ; planes 2, 3
    mov al, [es:si + 2 * IT_CCOL]   ; b1
    or al, [es:si + 3 * IT_CCOL]
    mov bx, ax
    shl bx, 1
    or cx, [bx + IT_FV + 1024]
    or dx, [bx + IT_FV + 1024 + 512]
    mov al, [es:si + 4 * IT_CCOL]   ; b2
    or al, [es:si + 5 * IT_CCOL]
    mov bx, ax
    shl bx, 1
    or cx, [bx + IT_FV + 2048]
    or dx, [bx + IT_FV + 2048 + 512]
    mov al, [es:si + 6 * IT_CCOL]   ; b3
    or al, [es:si + 7 * IT_CCOL]
    mov bx, ax
    shl bx, 1
    or cx, [bx + IT_FV + 3072]
    or dx, [bx + IT_FV + 3072 + 512]
    mov [di], cl
    mov [di + IT_FCELLS], ch
    mov [di + 2 * IT_FCELLS], dl
    mov [di + 3 * IT_FCELLS], dh
    inc di
    add si, 8 * IT_CCOL
    dec bp
    jnz .c
    pop ds
    mov cx, [it_fc1]
    sub cx, [it_fc0]                ; CX = the plane bytes a row
    ; --- the four planes -----------------------------------------------------
    mov bx, [it_fhc]
    shl bx, 1
    mov ax, [it_fsrow + bx]
    add ax, [it_fc0]
    mov [it_fhb], ax                ; the row's first byte
    mov es, [it_fseg]
    mov ax, 0x0102                  ; map mask: plane 0
    xor bx, bx                      ; BX = plane * 40
.pl:
    mov dx, 0x3C4
    out dx, ax
    push ax
    push cx
    mov di, [it_fhb]
    mov si, [it_fc0]
    add si, bx
    push ds
    mov ds, [it_seg]
    add si, IT_FBUF
    rep movsb
    pop ds
    pop cx
    pop ax
    shl ah, 1
    add bx, IT_FCELLS
    cmp bx, 4 * IT_FCELLS
    jb .pl
    mov ax, 0x0F02                  ; all planes again
    out dx, ax
    inc word [it_fhc]
    mov ax, [it_fhc]
    cmp ax, [it_fy1]
    jae .done
    jmp .row
.done:
    ret

; =============================================================================
; THE SPAN HOOKS (SPEC.md 108.12.3) - it_span's tail on a direct surface. In:
; BP = x, DL = y (the TOP pixel's row), DH = k* (rows written, >= 1), AL =
; the face's canvas byte (the column's nibble of its id), BX = the head, ES
; = the work claim. Preserve BX, DS, ES; clobber the rest.
;
; One device row is one read-modify-write - or on VGA one `xchg`, a latch
; load and a set/reset write - at a FIXED displacement in an unrolled run
; entered by count (Duff's shape, a run per bank phase): no per-row branch,
; no per-row address arithmetic, nothing written twice.
; =============================================================================

; --- CGA 320: 2 bits a pixel, rows in two banks ---------------------------------
it_hk_cga:
    push bx
    push ds
    mov si, bp
    and si, 3                       ; the pixel's place in its byte
    mov ah, [it_hkand + si]         ; AH = the AND mask
    mov cl, [it_hktop + si]         ; CL = TOP's bits there
    shl si, 1
    mov bl, al
    xor bh, bh
    add bx, [it_hkft + si]          ; TA (places 0, 1) or TB (2, 3)
    mov bl, [es:bx]                 ; BL = the face's bits
    mov si, bp
    shr si, 1
    shr si, 1                       ; the byte column
    mov ch, dh                      ; CH = rows
    xor dh, dh
    mov di, dx
    shl di, 1
    add si, [it_fsrow + di]
    mov di, si                      ; DI = the TOP pixel's byte
    mov ds, [it_fseg]
    mov al, [di]
    and al, ah
    or al, cl
    mov [di], al
    mov cl, ch
    dec cl                          ; the face rows below it
    jz it_hk_cga_x
    xor ch, ch
    shl cx, 1
    mov si, cx
    test dl, 1
    jnz .odd
    add di, 0x2000                  ; y even: y + 1 is odd, the next bank
    jmp [cs:it_cgt1 + si]
.odd:
    sub di, 0x2000 - 80             ; y odd: y + 1 is even, the next pair
    jmp [cs:it_cgt0 + si]
it_hk_cga_x:
    pop ds
    pop bx
    ret

; IT_CGSEQ p: the face rows from a first row in bank phase p, in REVERSE:
; entry k writes the k-th row below the first; a count of m enters at k =
; m - 1 and falls through to k = 0. ~50 cycles a row (12 bytes, fetch-bound;
; NASM's shortest displacement, so k = 0 - every face's last - and k = 2 are
; 8 and 10: the entries are reached through the jump table, never by size)
%macro IT_CGSEQ 1
  %assign %%k 21
  %rep 22
it_cgs%1e%[%%k]:
    mov al, [di + (((%1 + %%k) & 1) - %1) * 0x2000 + ((%1 + %%k) >> 1) * 80]
    and al, ah
    or al, bl
    mov [di + (((%1 + %%k) & 1) - %1) * 0x2000 + ((%1 + %%k) >> 1) * 80], al
    %assign %%k %%k - 1
  %endrep
    jmp it_hk_cga_x
%endmacro

%macro IT_CGTAB 1
it_cgt%1:
    dw 0
  %assign %%m 0
  %rep 22
    dw it_cgs%1e%[%%m]
    %assign %%m %%m + 1
  %endrep
%endmacro

    IT_CGSEQ 0
    IT_CGSEQ 1
    IT_CGTAB 0
    IT_CGTAB 1

; --- VGA 0Dh: one bit a plane, set/reset ----------------------------------------
it_hk_vga:
    push bx
    push ds
    mov bl, al
    xor bh, bh
    mov ch, [es:bx + IT_FVCOL]      ; CH = the face's colour
    mov bx, bp
    and bx, 7
    mov cl, [it_bit8 + bx]          ; CL = the pixel's bit
    mov bl, dh                      ; BL = rows (BH = 0)
    xor dh, dh
    mov si, dx
    shl si, 1
    mov di, [it_fsrow + si]         ; y * 40
    mov si, bp
    shr si, 1
    shr si, 1
    shr si, 1
    add di, si
    mov si, bp                      ; (x's parity: the TOP's colour)
    and si, 1
    mov bh, [it_fvtop + si]         ; BH = TOP's colour
    mov dx, 0x3CE
    mov al, 8
    mov ah, cl
    out dx, ax                      ; the bit mask: this pixel
    xor al, al
    mov ah, bh
    out dx, ax                      ; set/reset: the TOP
    mov ds, [it_fseg]
    xchg al, [di]                   ; latches in, the TOP's bit out
    dec bl
    jz it_hk_vga_x
    xor al, al
    mov ah, ch
    out dx, ax                      ; set/reset: the face
    xor bh, bh
    shl bx, 1
    jmp [cs:it_vgt + bx]
it_hk_vga_x:
    pop ds
    pop bx
    ret

; STKBALANCE-OK: it_vgs is not a routine - an unrolled run that only
; it_hk_vga's jump table enters, at that hook's depth (two words pushed),
; and that leaves through it_hk_vga_x's two pops (IT_CGSEQ's and IT_HGSEQ's
; runs are the same shape, inside their macros)
it_vgs:
  %assign k 21
  %rep 22
it_vgse%[k]:
    xchg al, [di + (k + 1) * 40]
    %assign k k - 1
  %endrep
    jmp it_hk_vga_x
it_vgt:
    dw 0
  %assign m 0
  %rep 22
    dw it_vgse%[m]
    %assign m m + 1
  %endrep

; --- Hercules: 2 card pixels a logical pixel, 1.5 card rows a row, 4 banks ------
; The TOP is logical row y's 1 or 2 card rows (2 for an even y), each with its
; card row's parity's bits; the face is card rows c(y + 1) .. c(y + n) - 1, an
; unrolled run for the first one's bank phase. Every row number and address
; is looked up (it_fs_herc_tabs), none worked out.
it_hk_herc:
    push bx
    push ds
    mov si, bp
    and si, 3                       ; the pixel's place in its byte
    mov ah, [it_hkand + si]         ; AH = the AND mask
    shl si, 1
    mov cx, [it_hktop + si]         ; CL / CH = TOP's bits, even / odd card row
    mov bx, [it_hkhb + si]          ; the place's page (BL = 0: 256-aligned)
    mov bl, al
    mov al, [es:bx + 512]
    mov bl, [es:bx]
    mov bh, al                      ; BL / BH = the face's bits, even / odd
    shr bp, 1
    shr bp, 1                       ; BP = the card byte (past IT_HX)
    mov si, dx
    and si, 0xFF
    shl si, 1                       ; SI = 2y: the row tables' index
    mov di, [es:si + IT_HYA]
    add di, bp                      ; DI = card row c(y)
    mov ds, [it_fseg]
    test byte [es:si + IT_HC], 1
    jnz .o1
    mov al, [di]                    ; TOP: c(y) even...
    and al, ah
    or al, cl
    mov [di], al
    test dl, 1
    jnz .face
    mov di, [es:si + IT_HYB]        ; ...and for an even y, c(y) + 1 odd
    add di, bp
    mov al, [di]
    and al, ah
    or al, ch
    mov [di], al
    jmp short .face
.o1:
    mov al, [di]                    ; TOP: c(y) odd...
    and al, ah
    or al, ch
    mov [di], al
    test dl, 1
    jnz .face
    mov di, [es:si + IT_HYB]        ; ...and c(y) + 1 even
    add di, bp
    mov al, [di]
    and al, ah
    or al, cl
    mov [di], al
.face:                              ; the face: c(y + 1) .. c(y + n) - 1
    mov cl, dh
    xor ch, ch
    shl cx, 1
    add cx, si
    mov di, cx
    mov cx, [es:di + IT_HC]
    sub cx, [es:si + IT_HC + 2]     ; CX = its card rows
    jz it_hk_herc_x
    mov di, [es:si + IT_HYA + 2]
    add di, bp                      ; DI = its first card row
    mov si, [es:si + IT_HYP + 2]    ; ...and that row's phase's run, at its
    shl cx, 1                       ; m = 0 point: entry k = m - 1 is m * 12
    shl cx, 1                       ; bytes before it (IT_HGSEQ: k >= 1 are
    sub si, cx                      ; 12 bytes each, so no jump table)
    shl cx, 1
    sub si, cx
    ; STKBALANCE-OK: a COMPUTED entry into an IT_HGSEQ run, at this hook's
    ; depth (two words pushed) - no jump table for the walker to follow, by
    ; design (12 bytes an entry); every run leaves through it_hk_herc_x's two
    ; pops, as it_vgs and the IT_CGSEQ runs do
    jmp si
it_hk_herc_x:
    pop ds
    pop bx
    ret

; IT_HGSEQ p: as IT_CGSEQ, four banks and a 90-byte card row; the parity of
; each card row picks BL (even) or BH (odd). Every entry k >= 1 is 12 bytes -
; its displacements forced to 16 bits - so the hook works an entry's address
; out (it_hgs<p>m, the m = 0 point, less 12 a row) and the four phases need no
; 34-word jump table each; entry k = 0, the one every face executes, has
; displacement 0 and sits LAST, so it is the short form and moves nothing
%macro IT_HGSEQ 1
  %assign %%k 32
  %rep 32
it_hgs%1e%[%%k]:
    mov al, [word di + (((%1 + %%k) & 3) - %1) * 0x2000 + ((%1 + %%k) >> 2) * 90]
    and al, ah
  %if (%1 + %%k) & 1
    or al, bh
  %else
    or al, bl
  %endif
    mov [word di + (((%1 + %%k) & 3) - %1) * 0x2000 + ((%1 + %%k) >> 2) * 90], al
    %assign %%k %%k - 1
  %endrep
it_hgs%1e0:
    mov al, [di]                    ; k = 0: card row c(y + 1) itself
    and al, ah
  %if %1 & 1
    or al, bh
  %else
    or al, bl
  %endif
    mov [di], al
    jmp it_hk_herc_x
it_hgs%1m equ it_hgs%1e0 + 12       ; m = 0: entry k = m - 1 is m * 12 before
    times -((it_hgs%1e0 - it_hgs%1e32) != 32 * 12) db 0 ; (12 bytes an entry)
%endmacro

    IT_HGSEQ 0
    IT_HGSEQ 1
    IT_HGSEQ 2
    IT_HGSEQ 3
it_hgtp:    dw it_hgs0m, it_hgs1m, it_hgs2m, it_hgs3m

; the hooks' constant tables
it_hkand:   db 0x3F, 0xCF, 0xF3, 0xFC   ; a 2-bit pixel's AND mask, by place
it_hkft:    dw IT_FTA, IT_FTA, IT_FTB, IT_FTB
it_hkhb:    dw IT_MLUT, IT_MLUT, IT_MLUT + 256, IT_MLUT + 256
it_bit8:    db 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01

; THE CGA LOOK (SPEC.md 108.12.2): an id's ink at an even x << 4 | at an odd
; x, palette 1: 0 black, 1 cyan, 2 magenta, 3 grey (white at high intensity)
; - the original's own four (ISOTRON-PLAN 6.2 marks which entries are
; CONFIRMED off the footage: PSIDE, CFRONT, CSIDE). PFRONT's cyan/white
; stripes are INFERRED from the composite periwinkle - no true-palette frame
; shows a player E/W face (every player wall in r1_*/r2_* runs N/S, solid
; cyan). tools/isotronsim.py's CGA_INK; its selfcheck reads it.
it_cgaink:
    db 0x00, 0x00, 0x11, 0x22       ; VOID, FLOOR, FLOORDOT cyan, RIM magenta
    db 0x33, 0x03, 0x03, 0x13       ; EDGE, SLABF stripes, SLABR, PFRONT
    db 0x11, 0x33, 0x02, 0x20       ; PSIDE cyan, TOP, CFRONT, CSIDE stripes
    db 0x00, 0x11, 0x00, 0x33       ; (free), LOGOF, (free), FLASH
; CLASSIC (C on VGA and EGA): the CGA look in the 16-colour palette, ink 1
; -> 3 cyan, 2 -> 5 magenta, 3 -> 7 grey. The model's CLASSIC.
it_colc:
    db 0x00, 0x00, 0x33, 0x55
    db 0x77, 0x07, 0x07, 0x37
    db 0x33, 0x77, 0x05, 0x50
    db 0x00, 0x33, 0x00, 0x77
