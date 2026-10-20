; =============================================================================
; os8088 - apps/pixelstein/pxrast.inc
;
; THE BACKENDS (SPEC.md 97.3, 97.4): which foreign mode this adapter gives
; us, the geometry of each, the per-row device table, the palettes, the ink
; table the compose reads, the presents, and the bracket's own main loop.
; NOT ONE kernel drawing slot past OSAPI_FSX_MODE (SPEC.md 53.7); the
; windowed present is pxwin.inc's and is the one place OSAPI_GFX_BLIT1 is
; called.
;
; ONE STRIDE FOR ALL OF THEM: the shadow is 80 bytes a row, a Mode X plane
; row is 80 bytes and the C160 attribute row is 80 bytes at stride 2 - so the
; compose never knows which backend it is on, and only px_present does.
; =============================================================================

; -----------------------------------------------------------------------------
; px_adapter - the raster this machine would give us, and the Mode row
; in:  [px_win].  out: [px_vkind], [px_caps], px_mode0/px_mode1 (the two Mode
;      items' backends, PXB_NONE = "no second mode"), [px_mode] the one in
;      force, [px_fsxm] its FSXM_*, and the two items' captions
; preserves every register (px_onresize is a callback)
;
; Asked of OUR WINDOW so that a two-card machine answers about the display
; the window is on (SPEC.md 39.18.2), and re-asked on W_ONRESIZE. EVERY
; BRANCH WRITES BOTH WAYS (apps/tank's rule): a drag from a VGA to a CGA
; must take Mode X off the row, not merely fail to add it.
; -----------------------------------------------------------------------------
px_adapter:
    push ax
    push bx
    push dx
    mov bx, [px_win]
    call OSAPI_FSX_CAPS             ; AX = the settable modes, DL = the kind
    mov [px_caps], ax
    mov [px_vkind], dl
    mov byte [px_mode0], PXB_NONE
    mov byte [px_mode1], PXB_NONE
    test ax, 1 << FSXM_MODEX
    jz .nomx
    mov byte [px_mode0], PXB_MODEX  ; VGA: Mode X first, its CGA mode second
    test ax, 1 << FSXM_CGA320
    jz .say
    mov byte [px_mode1], PXB_CGA4
    jmp short .say
.nomx:
    test ax, 1 << FSXM_CGA320
    jz .noc4
    mov byte [px_mode0], PXB_CGA4   ; CGA and EGA: 320x200x4 first...
    cmp dl, VID_CGA
    jne .say
    mov byte [px_mode1], PXB_C160   ; ...and the 160x100x16 retime on a
    jmp short .say                  ; genuine CGA only (SPEC.md 88.15.7)
.noc4:
    test ax, 1 << FSXM_HERC
    jz .say
    mov byte [px_mode0], PXB_HERC
.say:
    ; the mode in force: keep the player's pick if this adapter still has
    ; it, else the first item
    mov al, [px_mode]
    or al, al
    jz .pick                        ; NONE is never "kept": on a Hercules or
    cmp al, [px_mode0]              ; an EGA the second item is NONE too, and
    je .keep                        ; a compare of the two kept nothing for
    cmp al, [px_mode1]              ; a whole run of the gate
    je .keep
.pick:
    mov al, [px_mode0]
    mov [px_mode], al
.keep:
    or al, al
    jz .none
    mov bl, al
    xor bh, bh
    mov al, [px_fsxtab + bx]
    mov [px_fsxm], al
.none:
    ; the captions: the item names what it is, or why it is not
    mov bl, [px_mode0]
    xor bh, bh
    shl bx, 1
    mov ax, [px_modestr + bx]
    or bx, bx
    jnz .m0
    mov ax, px_s_mnone0             ; NONE in the FIRST slot is not "no
                                    ; second mode": a display with none of
                                    ; the three modes (SPEC.md 39.18.2 asks
                                    ; about THIS window's display) says so
.m0:
    mov [px_i_mode + 0], ax
    mov bl, [px_mode1]
    xor bh, bh
    shl bx, 1
    mov ax, [px_modestr + bx]
    mov [px_i_mode + 2], ax
    mov ax, px_s_gofull
    cmp byte [px_mode0], PXB_NONE
    jne .go
    mov ax, px_s_gofulln
.go:
    mov [px_i_game + 0], ax
    ; THE COLOUR ITEM (97.14): the depth of THIS window's display, never
    ; OSAPI_VIDEO's (the primary's, SPEC.md 39.16.4) - and the worker asks
    ; px_w4want of it at its next frame, so a drag across a seam that fired
    ; this notice takes the other byte set there
    push cx
    push si
    mov bx, [px_win]
    call OSAPI_WM_DISPLAY           ; DH = bits a pixel
    mov [px_bpp], dh
    pop si
    pop cx
    mov ax, px_s_colmono            ; a 1bpp display FIRST: greyed with the
    cmp byte [px_bpp], 1            ; display's own fact - an 8086 on a CGA
    jbe .col                        ; or a Hercules (the calibration machines)
    mov ax, px_s_colxt              ; cannot show WIN4 at any speed, so a price
    cmp byte [px_tier], CPU_8086    ; there would be a guess (SPEC.md 47;
    je .col                         ; review, wave 5). Then the 8086: greyed
                                    ; with its MEASURED price
    mov ax, px_s_colon
    cmp byte [px_colour], 0
    jne .col
    mov ax, px_s_coloff
.col:
    mov [px_i_det + PXD_COLOUR * 2], ax
    pop dx
    pop bx
    pop ax
    ret

; px_w4want - AL = the window's backend (97.14): PXB_WIN4 when the player
;             wants colour, the CPU is past the 8086 and THIS window's
;             display is a 16-colour one ([px_bpp], asked of the window);
;             PXB_WIN1 otherwise. clobbers nothing else
px_w4want:
    mov al, PXB_WIN1
    cmp byte [px_colour], 0
    je .out
    cmp byte [px_tier], CPU_8086
    je .out
    cmp byte [px_bpp], 1
    jbe .out
    mov al, PXB_WIN4
.out:
    ret

px_fsxtab:  db 0, FSXM_CGA320, FSXM_TEXT80, FSXM_HERC, FSXM_MODEX
px_modestr: dw px_s_mnone, px_s_mcga4, px_s_mc160, px_s_mherc, px_s_mmodex
px_s_mnone:  db MENU_DIS, 'No second mode', 0  ; greyed: nothing to pick
px_s_mnone0: db MENU_DIS, 'No full-screen mode here', 0  ; ...in slot 0
px_s_mcga4:  db 'CGA 320x200', 0
px_s_mc160:  db 'CGA 160x100x16', 0
px_s_mherc:  db 'Hercules', 0
px_s_mmodex: db 'Mode X', 0

; -----------------------------------------------------------------------------
; px_go_fsx - take the machine (SPEC.md 53.1), from a key or the menu: the
;             UI task with the lock held, never the worker
; -----------------------------------------------------------------------------
px_go_fsx:
    push ax
    push bx
    push cx
    cmp byte [px_inbr], 0
    jne .out
    cmp byte [px_mode], PXB_NONE
    je .nomode                      ; the predicate the Game item's caption
                                    ; was rewritten off ("Full Screen (no
                                    ; mode)", px_adapter): the item is not
                                    ; greyed, on apps/tank's precedent, and
                                    ; the refusal lives here
    cmp word [px_shseg], 0
    je .nobr                        ; no shadow: the window says so already
    call px_abdismiss               ; a card may not cross into the bracket
    mov byte [px_fsxq], 0
    mov byte [px_inbr], 1           ; BEFORE the call: fsx_restore's
    mov byte [px_fsx], 1            ; wm_paint_all runs INSIDE fsx_run and
                                    ; re-enters our own W_PAINT
    ; ...AND THE WORKER MUST BE OUT OF ITS COMPOSE (SPEC.md 97.8): it
    ; composes lock-free, so OSAPI_FSX_RUN's freeze would otherwise land at
    ; any instruction of px_cast or px_compose, and px_r_setup then rewrites
    ; the globals and the patched immediates under it - one corrupt frame
    ; on entering and one on leaving. It clears px_composing BEFORE it asks
    ; for the lock this task holds, so the spin cannot deadlock; with
    ; px_inbr already set it goes to sleep at its loop's top
.spin:
    cmp byte [px_composing], 0
    je .quiet
    call OSAPI_TASK_YIELD
    jmp short .spin
.quiet:
    mov ax, px_fsx_main
    mov bx, [px_win]
    xor cx, cx                      ; no worker kept: this loop IS the game
    call OSAPI_FSX_RUN
    inc word [px_brn]               ; brackets left: the gates wait on this
                                    ; word, because px_inbr clears at the
                                    ; TOP of the exit path and 0.9 s of
                                    ; regeneration and a composed frame
                                    ; follow it (pxslib.leave_fsx)
    mov byte [px_fsx], 0            ; whether it ran or was refused (a
    mov byte [px_inbr], 0           ; refused run never reached px_fsx_main,
    mov byte [px_fsxq], 0           ; so the shadow is still the window's)
    mov byte [px_demoreq], 0        ; ...and a T's request is spent either
    mov byte [px_lined], 1          ; way (a refused FSX_MODE left it set,
    cmp byte [px_cfgdirty], 0       ; and the next F ran the timedemo)
    je .out                         ; a V key inside the bracket: the file
    call px_set_save                ; is written now, on the desktop
    jmp short .out
.nomode:
    mov byte [px_lrefuse], 1        ; SAY SO (SPEC.md 47): F or T on a
    mov byte [px_lined], 1          ; machine with no mode names the reason
.nobr:                              ; on the window's line
    mov byte [px_demoreq], 0        ; no bracket ran: no timedemo is owed
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; px_fsx_main - the exclusive main (SPEC.md 53.1)
; in:  SI = our window, ES = KERNEL_SEG, DS = CS = ours, the gfx lock held for
;      the whole session, every other task frozen
; out: near ret; the kernel restores the desktop
; -----------------------------------------------------------------------------
px_fsx_main:
    push ds
    pop es
    mov di, px_fsi
    mov al, [px_fsxm]
    call OSAPI_FSX_MODE
    jc .out                         ; refused: the desktop is untouched
    call OSAPI_FSX_SURF             ; THE MOUSE'S MIDDLE IS THIS BRACKET'S
    jc .nosurf                      ; (SPEC.md 53.7.1, wave 6): OSAPI_VIDEO
    shr cx, 1                       ; answers about the PRIMARY, and on a
    add ax, cx                      ; two-card desktop a bracket on the other
    jmp short .surf                 ; card is that card's width - 720 on a
.nosurf:                            ; Hercules beside a 640 VGA, and the
    mov ax, [px_scrw]               ; mouse turned from 320 there, 40 dots
    shr ax, 1                       ; left of the middle. Asked once, after
.surf:                              ; the mode set's collapse (a bracket-only
    mov [px_fmid], ax               ; slot, px_mouse_turn reads the word)
    mov al, [px_mode]
    mov [px_back], al
    call px_r_setup
    mov byte [px_quit], 0
    call px_auto_start              ; the rung for a bracket on this tier
    call px_force_all
    cmp byte [px_demoreq], 0        ; A FINISHED GAME STARTS A NEW ONE at the
    jne .kept                       ; bracket's entry (97.13, apps/tank's rule):
    mov al, [px_state]              ; the attract page or GAME OVER is where
    cmp al, PXST_ATTRACT            ; F from a window leaves off, and the
    je .newg                        ; machine is taken to play on it - unless
    cmp al, PXST_OVER               ; it was taken for the timedemo
    jne .kept
.newg:
    xor al, al
    call px_new_game
.kept:
    call OSAPI_GET_TICKS
    mov [px_last], ax
.loop:
    call px_input                   ; int 16h to empty, through px_key_common
    cmp byte [px_quit], 0
    jne .out2
    call px_steps                   ; CX = the ticks owed, or the wait
    jcxz .draw
    cmp byte [px_state], PXST_PLAY  ; the held keys are PLAY's (97.13): a
    jne .nokeys                     ; card reads none, and the timedemo's
    call px_keys_read               ; script owns the step's turn and walk
.nokeys:                            ; (the held keys, once at the first owed
    cmp byte [px_pause], 0          ; step, 97.8)
    jne .draw
.sim:
    push cx
    call px_step
    pop cx
    loop .sim
.draw:
    call px_frame_begin
    jnc .loop
    call px_present
    call px_frame_end
    jmp .loop
.out2:
    mov byte [px_fsx], 0            ; cleared INSIDE the bracket, before
    mov byte [px_inbr], 0           ; fsx_restore's wm_paint_all runs - or
    mov byte [px_fsxq], 0           ; the thawed worker draws the bracket's
                                    ; geometry over the desktop
    mov byte [px_demoreq], 0        ; a timedemo left half run is over: the
    cmp byte [px_state], PXST_DEMO  ; attract page, and no numbers (97.13)
    jne .nodemo
    mov byte [px_demodone], 0
    call px_demo_restore            ; ...and the Detail and Size it pinned
    call px_to_attract
.nodemo:
    mov byte [px_hmsgw], PXM_NONE   ; the bracket's messages end with it -
    cmp byte [px_hmsg], PXM_PAUSED  ; one owed too - and the window's line
    je .msgok                       ; says the rest (the Tab map's line too:
    cmp byte [px_hmsg], PXM_MAP     ; the map stays up across the exit, so
    je .msgok                       ; its line does - review, wave 6)
    mov byte [px_hmsg], PXM_NONE
    mov byte [px_hmsgt], 0
.msgok:
    call px_r_setup_win             ; ...and the WINDOW'S frame composed here
    call px_auto_start              ; (Auto re-seated at the window's start:
                                    ; a window is one rung under the bracket
                                    ; on an 8086, 97.8; a pinned Detail is
                                    ; untouched by it)
    call px_frame_begin             ; too, before that repaint blits the
                                    ; shadow: it held the bracket's bytes in
                                    ; the bracket's inks, and W_PAINT would
                                    ; have shown them for a frame
.out:
    ret

; -----------------------------------------------------------------------------
; px_r_setup - adopt the bracket's backend for the length of one bracket
; in:  [px_back] = a PXB_*, [px_fsi] filled by OSAPI_FSX_MODE
; clobbers everything
; -----------------------------------------------------------------------------
px_r_setup:
    mov byte [px_lastx0], 0xFF      ; a new backend: px_apply clears and
                                    ; blanks for whatever band it applies
    call px_spr_class               ; the sprite set follows the ink class
    call px_ink_build
    call px_hud_setup               ; the bar's writer, cells and inks (97.13)
    call px_res_apply               ; ...which builds the device-row table
                                    ; too, biased by the band's first byte
                                    ; (px_devrows). THE SCALER SET AND THE
                                    ; BYTE SET ARE NOT BUILT HERE: px_apply
                                    ; builds them when the rung in force is
                                    ; Textured (pxgen.inc's px_tex_setup) -
                                    ; behind the black in a bracket, on the
                                    ; worker's first Textured frame in a
                                    ; window, and never for a rung that has
                                    ; no use for them
    mov word [px_pgofs], 0
    mov byte [px_page], 0
    mov al, [px_back]
    cmp al, PXB_MODEX
    je .modex
    mov ax, [px_shseg]              ; a shadow backend composes in RAM and
    mov [px_dseg], ax               ; copies rows
    mov word [px_dbase], 0
    call px_shadow_clear
    mov al, [px_back]
    cmp al, PXB_C160
    jne .nc160
    call px_c160_mode               ; the retime, on top of the plain 80x25
    ret
.nc160:
    cmp al, PXB_CGA4
    jne .out
    call px_cga_pal
.out:
    ret
.modex:
    mov ax, [px_fsi + FSI_SEG]      ; Mode X composes straight into the
    mov [px_dseg], ax               ; hidden page
    mov word [px_dbase], PX_MXROW0 * PX_STRIDE
    mov dx, 0x3C4
    mov ax, 0x0F02                  ; every plane: a byte store is four pixels
    out dx, ax
    mov es, [px_fsi + FSI_SEG]      ; both pages black: the mode set cleared
    xor di, di                      ; what the card is SHOWING and no more
    mov cx, 2 * 19200 / 2
    xor ax, ax
    cld
    rep stosw
    call px_modex_pal
    ret

; px_spr_class - the sprite set stands only for the ink class it was built
;                for (97.6): a backend of another class clears px_sprok, and
;                the next pass that wants it rebuilds (px_sprites,
;                px_tex_setup). preserves every register
px_spr_class:
    push ax
    push bx
    mov bl, [px_back]
    xor bh, bh
    mov al, [px_inkcls + bx]
    cmp al, [px_sprback]
    je .same
    mov byte [px_sprok], 0
    mov byte [px_sprback], 0xFF
.same:
    pop bx
    pop ax
    ret

; px_r_setup_win - the window's own backend - WIN1, or WIN4 where px_w4want
;                  says (97.14): the shadow, the inks, the bar; on WIN4 the
;                  32 -> 16 table put into the shadow claim beside its strip
px_r_setup_win:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call px_w4want
    mov [px_back], al
    mov byte [px_lastx0], 0xFF      ; (px_r_setup's reason)
    call px_spr_class
    call px_ink_build
    call px_hud_setup
    mov byte [px_hudw], 1           ; ...and all of it blitted (97.13)
    call px_card_owe                ; a card standing is the window's now
    call px_res_apply               ; (the sets, if the window's rung wants
                                    ; them, are px_apply's - above)
    mov word [px_pgofs], 0
    mov byte [px_page], 0
    mov ax, [px_shseg]
    mov [px_dseg], ax
    mov word [px_dbase], 0
    call px_shadow_clear
    call px_force_all
    cmp byte [px_back], PXB_WIN4
    jne .n4
    call px_w4tab_put
.n4:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_shadow_clear - THE PICTURE's bytes of the shadow to zero (black on every
;                   backend): PX_ROWS x PX_STRIDE = 6,400 of the 16 KB
;                   claim, which is every byte a compose writes or a present
;                   reads today (the rest is reserved for Rows 100 and wave
;                   5's WIN4 strips and holds nothing, 97.9); the first cut
;                   cleared the claim, 8,192 words for 3,200 of picture, ~26
;                   ms of it dead inside a keypress (review, wave 2). Nothing
;                   on Mode X, which composes into its pages and has
;                   px_band_blank for the margins
px_shadow_clear:
    cmp word [px_shseg], 0
    je .out                         ; NO SHADOW: a refused claim leaves the
    cmp byte [px_back], PXB_MODEX   ; word 0, and 3,200 words through ES = 0
    je .out                         ; is the IVT, the BIOS data area and the
    mov es, [px_shseg]              ; kernel's low bytes (caught in review)
    xor di, di
    mov cx, PX_ROWS * PX_STRIDE / 2
    xor ax, ax
    cld
    rep stosw
.out:
    ret

; px_w4tab_put - WIN4's two 32 -> 16 tables (97.14) into the shadow claim at
;                PX_W4TAB, beside the strip they expand into, so the present's
;                inner loop has its source, its table and its destination in
;                ONE segment: entry i is the colour of Mode X's byte i with
;                both nibbles set, for the EVEN rows and then the ODD -
;                0..15 the sixteen themselves on both, 16..31 the dark face:
;                its LIT colour on the even rows and black on the odd, a
;                line dither that keeps the material's hue (wave 6; the first
;                cut's C160 twin made brown red and red blue). The tables are
;                tools/pxsart.py's win4_tables(), px_it_win4 in pxart.inc,
;                and px_blit_w4 swaps them a row (`xor bl, 0x20`).
;                clobbers CX, SI, DI, ES
px_w4tab_put:
    mov es, [px_shseg]
    mov di, PX_W4TAB
    mov si, px_it_win4
    mov cx, 32
    cld
    rep movsw
    ret

; -----------------------------------------------------------------------------
; px_res_apply - the Size and the Resolution to the frame's words (97.1,
;                97.3): the band's bytes and its first byte, the compose's
;                column bias, the columns, the bytes a column, u's shift to
;                a texture column, the fan, the ladder, the scaler
;                directory - and in a bracket the device-row table, which
;                carries the band's first byte in every entry
; in:  [px_sizeix] the Size row's pick, [px_lowres], [px_back], [px_inbr]
; clobbers AX, BX, CX, DX, SI, DI
;
; WINDOWED THE BAND IS 64 BYTES AT MOST: the window's frame is fixed at
; PX_WINW (97.3), so Size 72 and 80 are the bracket's and a window shows 64
; and says so on its text line. The picture is centred in the 80-byte row
; on every byte backend, so x0 = (80 - Size) / 2 - ONE word for the writer
; and the readers alike: px_cbias is what px_compose adds to every column's
; base (x0 less the ladders' 256), px_x0 what the windowed blit reads, and
; px_devoff carries x0 (2 x0 on C160) in every device row so the bracket's
; present loop reads nothing of it (the first cut's present re-read x0, Size
; and the advance from memory on every row: +5,800 clk a frame over wave 1's)
; -----------------------------------------------------------------------------
px_res_apply:
    mov bl, [px_sizeix]
    xor bh, bh
    mov al, [px_sizes + bx]
    cmp byte [px_back], PXB_WIN1
    jb .sz                          ; (WIN1 and WIN4, the two windowed ones)
    cmp al, PX_BAND
    jbe .sz
    mov al, PX_BAND
.sz:
    mov [px_size], al
    mov ah, PX_STRIDE
    sub ah, al
    mov [px_sadv], ah               ; the present's row advance past the band
    shr ah, 1
    mov [px_x0], ah
    mov bl, ah
    xor bh, bh
    sub bx, 256
    mov [px_cbias], bx              ; x0 - 256, the compose's bias (pxcomp.inc)
    mov byte [px_ush], 3            ; a texture column is u >> 3...
    cmp byte [px_back], PXB_C160
    jne .ush
    mov byte [px_ush], 4            ; ...u >> 4 on C160: a column is a pair
.ush:
    mov byte [px_bpc], 1
    mov word [px_ladp], px_ladb
    mov word [px_gsc], px_sctab
    mov word [px_gc2t], px_c2t
    cmp byte [px_lowres], 0
    je .cols
    shr al, 1                       ; Low res: half the rays, two bytes each
    mov byte [px_bpc], 2
    mov word [px_ladp], px_ladw
    mov word [px_gsc], px_sctab + PXG_DIRSZ
    mov word [px_gc2t], px_c2t + PXG_DIRSZ
.cols:
    mov [px_cols], al
    ; the fan of that many columns, off px_fantab (pxtab.inc): word columns,
    ; word table, PX_FAN_RUNGS rows
    xor ah, ah
    mov si, px_fantab
    mov cx, PX_FAN_RUNGS
.fan:
    cmp [si], ax
    je .found
    add si, 4
    loop .fan
    mov si, px_fantab + 4 * 2       ; (unreachable: every size has a fan;
                                    ; 32 columns if one did not)
.found:
    mov ax, [si + 2]
    mov [px_fanp], ax
    cmp byte [px_inbr], 0           ; in a bracket the device rows carry x0:
    je .out                         ; rebuilt with the Size (px_devrows) on
    cmp byte [px_back], PXB_MODEX   ; the shadow backends - Mode X composes
    je .out                         ; into its pages and has no row table
    call px_devrows
.out:
    ret

px_sizes:   db 48, 56, 64, 72, 80   ; the Size row, PX_NSIZES rungs
PX_NSIZES   equ 5
PX_NSIZEW   equ 3                   ; ...of which a WINDOW cycles the first
                                    ; three (48, 56, 64): 72 and 80 are the
                                    ; bracket's, the items say so (97.3)

; -----------------------------------------------------------------------------
; px_band_blank - in a bracket, the 80 device rows OUTSIDE the band to black:
;                 the mode set's clear was the whole screen's, so a Size
;                 picked narrower inside the bracket would leave the wider
;                 band's edges lit for the rest of the session (the present
;                 copies Size bytes at x0 and nothing else). Once per Size
;                 pick (px_apply), ~80 x 2 rep stores: cold. Both Mode X
;                 pages, since the compose alternates them; the C160 cell
;                 outside the band is put back to the retime's own black
;                 (the half block over attribute 0, px_c160_mode)
; clobbers AX, BX, CX, DX, SI, DI, ES
; -----------------------------------------------------------------------------
px_band_blank:
    cmp byte [px_inbr], 0
    je .out
    mov es, [px_fsi + FSI_SEG]
    cld
    mov dl, [px_x0]                 ; DL = the left margin's bytes
    mov dh, [px_sadv]
    sub dh, dl                      ; DH = the right's (80 - x0 - Size)
    mov al, [px_back]
    cmp al, PXB_MODEX
    je .modex
    cmp al, PXB_C160
    je .c160
    mov bx, px_devoff               ; CGA4 / Hercules: the row table, each
    mov si, PX_ROWS                 ; entry the band's first byte
.row:
    mov di, [bx]
    mov cl, dl
    xor ch, ch
    sub di, cx                      ; the row's byte 0
    xor ax, ax
    rep stosb                       ; [0, x0)
    mov cl, [px_size]
    add di, cx                      ; past the band
    mov cl, dh
    rep stosb                       ; [x0 + Size, 80)
    inc bx
    inc bx
    dec si
    jnz .row
.out:
    ret
.c160:
    mov bx, px_devoff               ; the entry is the band's ATTRIBUTE byte,
    mov si, PX_ROWS                 ; two device bytes a cell
.trow:
    mov di, [bx]
    mov cl, dl
    xor ch, ch
    sub di, cx
    sub di, cx
    dec di                          ; cell 0's character byte
    mov ax, 0x00DE                  ; the half block, black on black
    rep stosw
    mov cl, [px_size]
    add di, cx
    add di, cx
    mov cl, dh
    rep stosw
    inc bx
    inc bx
    dec si
    jnz .trow
    ret
.modex:
    mov di, PX_MXROW0 * PX_STRIDE   ; both pages: the view rows, 80 a row,
    mov bx, 2                       ; map mask 0Fh (px_r_setup)
.page:
    mov si, PX_ROWS
.mrow:
    mov cl, dl
    xor ch, ch
    xor ax, ax
    rep stosb
    mov cl, [px_size]
    add di, cx
    mov cl, dh
    rep stosb                       ; ...and DI is the next row's byte 0
    dec si
    jnz .mrow
    add di, 19200 - PX_ROWS * PX_STRIDE
    dec bx
    jnz .page
    ret

; -----------------------------------------------------------------------------
; px_devrows - the device offset of each of the 80 view rows in the mode the
;              bracket set (apps/skies' cs_devrows, the three banked arms),
;              PLUS THE BAND'S FIRST BYTE - x0, or 2 x0 on C160, whose cell
;              is two device bytes - so the present's row loop reads the
;              table and nothing else (px_res_apply rebuilds it with the
;              Size). clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
px_devrows:
    mov cl, [px_x0]
    xor ch, ch
    mov si, cx                      ; SI = x0, the bias of every entry
    cmp byte [px_back], PXB_C160
    jne .bias
    shl si, 1                       ; two device bytes a cell
.bias:
    mov di, px_devoff
    xor bx, bx                      ; BX = the view row
.row:
    mov ax, bx
    mov cl, [px_back]
    cmp cl, PXB_HERC
    je .herc
    cmp cl, PXB_C160
    je .text
    add ax, PX_CGAROW0              ; CGA: bank = y & 1, 80 bytes a row
    mov dx, ax
    and dx, 1
    shr ax, 1
    mov cx, PX_STRIDE
    push dx                         ; the bank, across the mul that writes DX
    mul cx
    pop dx
    jmp short .bank
.herc:
    add ax, PX_HERCROW0             ; HERC: bank = y & 3, 90 bytes a row, +5
    mov dx, ax                      ; for the box's x = 40
    and dx, 3
    shr ax, 1
    shr ax, 1
    mov cx, 90
    push dx
    mul cx
    pop dx
    add ax, 5
    jmp short .bank
.text:
    mov cx, 160                     ; C160: 80 words a character row, no
    mul cx                          ; bank, and the attribute is the odd byte
    inc ax
    xor dx, dx
.bank:
    mov cl, 13
    shl dx, cl                      ; bank * 0x2000
    add ax, dx
    add ax, si                      ; + the band's first byte
    mov [di], ax
    inc di
    inc di
    inc bx
    cmp bx, PX_ROWS + PX_HUDROWS    ; THE BAR'S ROWS FOLLOW (97.13): the
    jb .row                         ; table runs on past the view into
    mov di, px_hudoff               ; px_hudoff's slot of it...
    mov cx, PX_HUDROWS
.hb:
    sub [di], si                    ; ...which is NOT biased by the band's
    inc di                          ; first byte: the bar is the whole row
    inc di                          ; wide, and px_hud_present adds its own
    loop .hb
    ret

; -----------------------------------------------------------------------------
; px_cga_pal - CGA palette 0 (green, red, brown) over BLACK, through the BIOS:
;              a VGA or EGA running mode 4 has no port 3D9h (apps/skies'
;              cs_cga_pal, SPEC.md 88.3)
; -----------------------------------------------------------------------------
px_cga_pal:
    mov ax, 0x0B00
    mov bx, 0x0100                  ; BH = 1: the palette, BL = 0
    int 0x10
    mov ax, 0x0B00
    mov bx, 0x0000                  ; BH = 0: the background, BL = 0 black
    int 0x10
    ret

; -----------------------------------------------------------------------------
; px_modex_pal - the DAC: 0..15 the sixteen colours as tools/pxssim.py's
;                PALETTE, 16..31 the same at 60% for a dark face (97.4)
; -----------------------------------------------------------------------------
px_modex_pal:
    cld
    mov si, px_dactab
    mov cx, 16
    xor bl, bl
.e:
    mov dx, 0x3C8
    mov al, bl
    out dx, al
    inc dx
    lodsb
    out dx, al
    lodsb
    out dx, al
    lodsb
    out dx, al
    inc bl
    loop .e
    mov si, px_dactab
    mov cx, 16
.d:                                 ; ...and the dark set: v * 6 / 10
    mov dx, 0x3C8
    mov al, bl
    out dx, al
    inc dx
    push cx
    mov cx, 3
.c:
    lodsb
    mov bh, 6
    mul bh                          ; AX = v * 6 (up to 378)
    mov bh, 10                      ; the divisor OUTSIDE AX: `div ah` after
    div bh                          ; `mov ah, 10` divides 0x0Axx and
    out dx, al                      ; overflows into int 0 for ever
    loop .c
    pop cx
    inc bl
    loop .d
    ret

px_dactab:                          ; 6-bit, the 16 colours of os88api.inc
    db  0,  0,  0                   ;  0 black
    db  0,  0, 42                   ;  1 blue
    db  0, 42,  0                   ;  2 green
    db  0, 42, 42                   ;  3 cyan
    db 42,  0,  0                   ;  4 red
    db 42,  0, 42                   ;  5 magenta
    db 42, 21,  0                   ;  6 brown
    db 42, 42, 42                   ;  7 light grey
    db 21, 21, 21                   ;  8 dark grey
    db 21, 21, 63                   ;  9 light blue
    db 21, 63, 21                   ; 10 light green
    db 21, 63, 63                   ; 11 light cyan
    db 63, 21, 21                   ; 12 light red
    db 63, 21, 63                   ; 13 light magenta
    db 63, 63, 21                   ; 14 yellow
    db 63, 63, 63                   ; 15 white

; -----------------------------------------------------------------------------
; px_c160_mode - the 160x100x16 retime (SPEC.md 88.15.2): six 6845 writes with
;                the video off, every cell the right half block, video on
; -----------------------------------------------------------------------------
px_c160_crtc:
    db  4, 127
    db  5,   6
    db  6, 100
    db  7, 112
    db  9,   1
    db 10, 0x20
PX_C160_NREG equ ($ - px_c160_crtc) / 2

px_c160_mode:
    mov dx, 0x3D8
    mov al, 0x01                    ; 80-column text, blink off, VIDEO OFF
    out dx, al
    cld
    mov si, px_c160_crtc
    mov cx, PX_C160_NREG
    mov dx, 0x3D4
.r:
    lodsw
    out dx, al
    inc dx
    xchg al, ah
    out dx, al
    dec dx
    loop .r
    mov dx, 0x3D9
    xor al, al
    out dx, al
    mov es, [px_fsi + FSI_SEG]
    xor di, di
    mov ax, 0x00DE                  ; the right half block, black on black
    mov cx, 160 * 100 / 2
    rep stosw
    mov dx, 0x3D8
    mov al, 0x09                    ; ...and the picture back on
    out dx, al
    ret

; -----------------------------------------------------------------------------
; px_ink_build - the compose's tone table for [px_back] (97.4): px_inkt is a
;                word per (material, side) - the even-row byte low, the
;                odd-row byte high - and px_inkc/px_inkf/px_inke the ceiling,
;                the floor and Wire's edge. THE RULE IS tools/pxssim.py's
;                ink(), written once more here, and tests/pxssim.py holds the
;                two to the same bytes.
; clobbers AX, BX, CX, DX, SI, DI, ES
; -----------------------------------------------------------------------------
px_ink_build:
    push ds
    pop es
    cld                             ; lodsw/stosw over our own image and bss
    mov al, [px_back]
    cmp al, PXB_MODEX
    je .modex
    cmp al, PXB_WIN4                ; WIN4 composes Mode X's bytes (97.14)
    je .modex
    cmp al, PXB_C160
    je .c160
    mov si, px_ink_cga4             ; the two-tone backends: seven words
    cmp al, PXB_CGA4
    je .fixed
    mov si, px_ink_herc             ; Hercules, and WIN1's band is its byte set
.fixed:
    lodsw
    mov [px_inkc], ax
    lodsw
    mov [px_inkf], ax
    lodsw
    mov bx, ax                      ; wall lit
    lodsw
    mov dx, ax                      ; wall dark
    lodsw
    mov [px_inkdl], ax              ; the door slab's own (PXM_DOOR)
    lodsw
    mov [px_inkdd], ax
    lodsw
    mov [px_inke], ax
    lodsw
    mov [px_inks], ax               ; a sprite's silhouette (97.6)
    lodsw
    mov [px_inkw], ax               ; the DIE wash (97.8)
    mov di, px_inkt
    mov cx, 16
.f:
    mov ax, bx
    stosw
    mov ax, dx
    stosw
    loop .f
    mov ax, [px_inkdl]
    mov [px_inkt + PXM_DOOR * 4], ax
    mov ax, [px_inkdd]
    mov [px_inkt + PXM_DOOR * 4 + 2], ax
    ret
.modex:
    mov word [px_inkc], 0
    mov word [px_inkf], 0x0808
    mov word [px_inke], 0x0101      ; material 1, lit
    mov word [px_inks], 0x1818      ; a silhouette: dark grey, dark
    mov word [px_inkw], 0x0404      ; the wash: red
    mov di, px_inkt
    xor cx, cx
.m:
    mov al, cl                      ; lit: the material's own DAC entry
    mov ah, al
    stosw
    add al, 16                      ; dark: sixteen along
    mov ah, al
    stosw
    inc cx
    cmp cx, 16
    jb .m
    ret
.c160:
    mov word [px_inkc], 0
    mov word [px_inks], 0x8888      ; a silhouette: dark grey
    mov word [px_inkw], 0x4444      ; the wash: red
    mov word [px_inkf], 0x2222      ; green, as CGA4's floor is: dark grey
                                    ; (8) is the dark twin of the eleven
                                    ; light-grey materials, and a floor in
                                    ; it lost the corner between a dark
                                    ; face and the ground (97.4)
    mov di, px_inkt
    xor bx, bx
    mov cl, 4
.t:
    mov al, [px_c160_m + bx]        ; the material's colour...
    push bx
    mov bl, al
    xor bh, bh
    mov dl, [px_c160_d + bx]        ; ...and its dark, one shade down
    pop bx
    mov ah, al                      ; both nibbles: texels u and u+1 of one
    shl ah, cl                      ; row (SPEC.md 97.4)
    or al, ah
    mov ah, al
    stosw
    mov al, dl
    mov ah, al
    shl ah, cl
    or al, ah
    mov ah, al
    stosw
    inc bx
    cmp bx, 16
    jb .t
    mov ax, [px_inkt + 1 * 4]       ; Wire's edge: material 1, lit
    mov [px_inke], ax
    ret

; the two-tone backends' nine words: ceiling, floor, a lit face, a dark
; face, the door slab lit and dark, Wire's edge, a sprite's silhouette, the
; DIE wash - each the even-row byte low and the odd-row byte high
; (tools/pxssim.py's ink(), CGA palette 0: 1 green, 2 red, 3 brown; Hercules
; a 2x2 dither by row parity)
px_ink_cga4: dw 0x0000, 0x5555, 0xFFFF, 0xAAAA, 0xDDDD, 0x8888, 0xFFFF, 0x8888, 0xAAAA
px_ink_herc: dw 0x0000, 0x2288, 0xFFFF, 0xAA55, 0xEEBB, 0x4411, 0xFFFF, 0x2288, 0x4411
; C160: a material's attribute colour, and a colour's dark twin
px_c160_m:   db 7, 7, 7, 9, 6, 6, 4, 7, 7, 7, 7, 7, 3, 6, 14, 7
px_c160_d:   db 8, 8, 8, 3, 8, 8, 8, 8, 8, 1, 8, 8, 8, 8, 6, 8

; -----------------------------------------------------------------------------
; px_present - the frame onto the glass, bracket backends (SPEC.md 97.5)
; in:  [px_r0]..[px_r1] the rows the compose wrote
; clobbers everything but DS
;
; Shadow backends copy the band of every dirty row through the device-row
; table, rep movsw a row; C160 lays each shadow byte at every OTHER address
; (apps/skies' cs_blit.expand: the attribute is the odd byte and the
; character bytes were written once by the retime); Mode X shows the page
; it drew and the next frame takes the other.
;
; THE ROW LOOP READS THE TABLE AND NOTHING ELSE: the band's first byte is in
; every px_devoff entry (px_devrows), the words a row are in AX and the
; advance past the band in DX, loaded once - the first cut of the Size row
; re-read x0, Size and the advance from memory on every row, five
; cs:-prefixed instructions that cost 5,800 clk a frame (107,026 against
; wave 1's 101,200 on the same 64 x 80 band, review). MEASURED 99,24x clk =
; 20.8 ms on every rung and scene of the 64-byte band (docs/reports/PXS-
; FRAME-2026-09-21.md): the same 5,120 bytes every frame, and a row whose
; median frame caught IRQ0 inside this stage reads ~6,000 more - the tick's
; own cost, which px_frame_end never subtracts (only px_twait's waits are) -
; not a property of the copy. The first take of this comment called that
; band "the CGA's wait states at the scan phase" (review, wave 2)
; -----------------------------------------------------------------------------
px_present:
    mov al, [px_back]
    cmp al, PXB_MODEX
    je .flip
    mov al, [px_r0]
    cmp al, [px_r1]
    ja .out                         ; nothing of the view was written
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov si, [px_row80 + bx]
    mov cl, [px_x0]
    xor ch, ch
    add si, cx                      ; DS:SI = the first dirty row's band
    add bx, px_devoff               ; CS:BX = its device row (band-biased)
    mov cl, [px_r1]
    sub cl, al
    inc cl
    xor ch, ch
    mov bp, cx                      ; BP = rows (a counter, never [bp])
    mov dl, [px_sadv]
    xor dh, dh                      ; DX = the advance to the next row's band
    mov al, [px_size]
    xor ah, ah
    shr ax, 1                       ; AX = the band's words
    mov es, [px_fsi + FSI_SEG]
    push ds
    mov ds, [cs:px_shseg]
    cld
    cmp byte [cs:px_back], PXB_C160
    je .expand
.row:
    mov di, [cs:bx]
    mov cx, ax
    rep movsw                       ; Size bytes: the band (97.3)
    add si, dx                      ; ...to the next row's band
    inc bx
    inc bx
    dec bp
    jnz .row
    pop ds
    jmp px_hud_present              ; ...and the bar's rectangle (97.13)
.expand:
    mov di, [cs:bx]                 ; (two device bytes a shadow byte, and
    mov cl, [cs:px_size]            ; the table carries 2 x0)
    xor ch, ch
    shr cx, 1
.e:
    lodsw                           ; (AX is the data here, so the count is
    stosb                           ; re-read a row: ~35 clk A ROW, ~2,800 a
    inc di                          ; frame, against the 220,000 the expand
                                    ; itself costs)
    xchg al, ah
    stosb
    inc di
    loop .e
    add si, dx
    inc bx
    inc bx
    dec bp
    jnz .expand
    pop ds
.out:
    cmp byte [px_back], PXB_MODEX   ; (nothing of the view written: the bar
    je .ret                         ; may still be owed on a shadow backend)
    jmp px_hud_present
.ret:
    ret
.flip:
    call px_wait_begin              ; THE RETRACE IS NOT WORK (97.8): the
    mov al, [px_page]               ; kernel waits for the field that latches
    call OSAPI_FSX_PAGE             ; the page (SPEC.md 53.10), and a frame
    call px_wait_end                ; timed through it reads as whole 16.7 ms
    xor byte [px_page], 1           ; fields - the selector then judged the
                                    ; bucket and not the frame
    mov word [px_dbase], PX_MXROW0 * PX_STRIDE
    mov word [px_pgofs], 0
    cmp byte [px_page], 0
    je .out
    mov word [px_dbase], 19200 + PX_MXROW0 * PX_STRIDE
    mov word [px_pgofs], PX_COLMAX
    ret
