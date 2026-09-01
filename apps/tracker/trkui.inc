; =============================================================================
; os8088 - apps/tracker/trkui.inc
;
; The Tracker's whole visual surface (SPEC.md 45): the FastTracker II layout,
; adapter-parameterized. Included by tracker.asm AFTER the TRKB/TRKW/TRKBUF
; bss macros are defined; label prefix tui_ (trk_ belongs to tracker.asm,
; mp_ to trkplay.inc; ui_ is the kernel's and stays out of a package).
;
; Three layouts, chosen by OSAPI_VIDEO height (SPEC.md 39):
;   >= 400  VGA 640x480      the FT2-proportioned screen: top desktop area
;                            y=0..171 (position editor, TRACKER nameplate,
;                            BPM/Spd + Ptn/Ln boxes, status line, 2x2 scope
;                            grid, two button stacks, instrument list), the
;                            pattern editor below with 16 rows above and 18
;                            below the full-width current-row band
;   >= 300  Hercules 720x348 the mid layout: same top blocks, scopes in one
;                            row of four, no menus/instrument list, 14+14
;                            pattern rows
;   else    CGA 640x200      compact: one title/readout line, four inline
;                            volume bars, 10+10 pattern rows
;
; Colours are a table indexed by role, filled per adapter: the VGA table is
; the FT2 "Arctic" palette mapped to 16 colours (pattern bg 0, pattern text
; 9, band 7 with text 15, bevels 15/8, accent 1); the 1bpp table never uses
; colour 9 (it reduces to BLACK - invisible on the pattern's black field,
; SPEC.md 39.4): text is 15 on 0, the band inverts to solid white with black
; text, faces go black with white bevels, and the desktop is the 50% dither.
;
; THE GRANULARITY RULE (SPEC.md 11.3) is enforced the fr_status way: every
; erase+text pair - a pattern row strip, a readout value, a whole desktop
; element - is gated on ONE OSAPI_WM_CLIP_TEST of its rect and skipped whole
; when any of it is covered. Pure fills (backgrounds, volume bars) clip per
; pixel and are never gated. In fullscreen the only thing that ever covers
; this window is the Standard File dialog - which is exactly when the gates
; matter.
; =============================================================================

; --- layout record offsets (words; one record copied into tui_lay) -------------
TL_PATY   equ 0                     ; pattern area top (0 = none above it)
TL_HDRY   equ 2                     ; channel digit strip y
TL_ROWT   equ 4                     ; first upper row y
TL_RAB    equ 6                     ; rows above the band
TL_BANDY  equ 8                     ; band top (= ROWT + RAB*8)
TL_LOWY   equ 10                    ; first lower row y (= BANDY + 11)
TL_RBE    equ 12                    ; rows below the band
TL_RNL    equ 14                    ; left row-number x
TL_RNR    equ 16                    ; right row-number x, 0FFFFh = none
TL_CHX    equ 18                    ; channel block left x
TL_CHW    equ 20                    ; per-channel width, separator included
TL_PAD    equ 22                    ; cell text x offset inside a channel
TL_SCX    equ 24                    ; scope grid origin
TL_SCY    equ 26
TL_SCDX   equ 28                    ; cell pitch
TL_SCDY   equ 30
TUI_VUH   equ 3                     ; volume-bar height, px. It was 8 - a
                                    ; full glyph row - and a bar is the one
                                    ; thing here that is redrawn on EVERY
                                    ; frame, so its height is a direct
                                    ; multiplier on the per-frame fill cost.
                                    ; Three rows still reads as a level meter
                                    ; and costs three eighths of the pixels
TL_SCW    equ 32                    ; cell size
TL_SCH    equ 34
TL_SCPR   equ 36                    ; scopes per row (2 or 4)
TL_STX    equ 38                    ; status/message text position
TL_STY    equ 40
TL_STW    equ 42                    ; ...and its erase width
TL_SLX    equ 44                    ; top-band sliver left x, 0FFFFh = none
TL_FLAGS  equ 46                    ; bit0 instrument list, bit1 button
                                    ; stacks, bit2 compact (CGA)
TL_SIZE   equ 48

TLF_INSTR equ 1
TLF_MENUS equ 2
TLF_CMPCT equ 4

; --- colour table roles (bytes, indexed into tui_col) --------------------------
TC_BG     equ 0                     ; pattern background
TC_TEXT   equ 1                     ; pattern text (9 - NEVER on mono)
TC_BAND   equ 2                     ; current-row band fill
TC_BANDT  equ 3                     ; ...and its text
TC_FACE   equ 4                     ; desktop element face
TC_FACET  equ 5                     ; ...and its text
TC_BEVL   equ 6                     ; light bevel
TC_BEVD   equ 7                     ; dark bevel
TC_BTN    equ 8                     ; button face
TC_BTNT   equ 9                     ; ...and its text
TC_VU     equ 10                    ; volume bar
TC_SEP    equ 11                    ; channel separators / rules
TC_ACC    equ 12                    ; nameplate accent
TC_ACCT   equ 13                    ; ...and its text
TC_STATB  equ 14                    ; status line background
TC_STATT  equ 15                    ; ...and its text
TC_WBG    equ 16                    ; windowed splash / About panel bg
TC_WTX    equ 17                    ; ...and its text
TC_N      equ 18

; --- the scroll path's reach (SPEC.md 45.12.2) --------------------------------
; How many rows of view movement tui_scrl will still answer with a blit. The
; cost is 2n+1 strips against tui_draw_pat's RAB+RBE+1 - 35 on the VGA layout,
; 29 on the Hercules one - so the arithmetic alone allows far more than this;
; what caps it is that past a few rows the move is a SEEK rather than a late
; frame, and a seek has nothing worth preserving in the region. Swept on the
; 286 in SPEC.md 45.12.2.
TUI_SCRL_MAX equ 4

; set the pen from the colour table
%macro TSETC 1
    push ax
    mov al, %1
    call tui_setcol
    pop ax
%endmacro

; -----------------------------------------------------------------------------
; tui_layout_init - pick a layout + colour table off the live screen. Called
;                   once, from the first W_PAINT (OSAPI_VIDEO is any-context).
; preserves all registers
; -----------------------------------------------------------------------------
tui_layout_init:
    push ax
    push bx
    push dx
    call OSAPI_VIDEO                ; AX = w, BX = h, DL = adapter, DH = bpp
    mov [tui_scrh], bx              ; ...banked, so tui_track can tell "the
    call tui_layout_for             ; tables are the screen's" from "the tables
    pop dx                          ; are some bracket surface's" with a
    pop bx                          ; compare rather than a far call per draw
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_layout_for - ...and the same pick for a SURFACE that is not the screen
; in:       AX = width, BX = height, DH = bits per pixel (OSAPI_VIDEO's own
;           three outputs, so the caller above just forwards them)
; preserves all registers
;
; IT IS A PARAMETER NOW BECAUSE OF THE SECOND DISPLAY. The layout is chosen by
; HEIGHT - it is really "which class of machine is this" - and a bracket on a
; two-display desktop owns one display which need not be the primary and need
; not be the same shape (SPEC.md 53.7.1): a 480-row VGA layout drawn onto a
; 350-row Hercules loses its bottom third. tui_track re-picks per draw, which
; costs two compares in the steady state because [tui_layh] records what the
; live tables were picked FOR.
; -----------------------------------------------------------------------------
tui_layout_for:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [tui_vw], ax
    mov [tui_vh], bx
    mov [tui_layh], bx
    mov byte [tui_mono], 0
    cmp dh, 1
    jne .depth
    mov byte [tui_mono], 1
.depth:
    mov si, tui_lay_vga
    cmp bx, 400
    jae .lay
    mov si, tui_lay_hrc
    cmp bx, 300
    jae .lay
    mov si, tui_lay_cga
.lay:
    mov di, tui_lay
    mov cx, TL_SIZE / 2
.lcopy:
    mov ax, [si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .lcopy
    mov si, tui_col_vga
    cmp byte [tui_mono], 0
    je .col
    mov si, tui_col_mono
.col:
    mov di, tui_col
    mov cx, TC_N
.ccopy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .ccopy
    ; derived: right edge of the channel block, of a row strip, VU bar width
    mov ax, [tui_lay + TL_CHW]
    shl ax, 1
    shl ax, 1                       ; * 4 channels
    add ax, [tui_lay + TL_CHX]
    dec ax
    mov [tui_chend], ax
    mov bx, [tui_lay + TL_RNR]
    cmp bx, 0FFFFh
    je .norr
    add bx, 15
    cmp bx, ax                      ; strip reaches whichever is further
    jbe .norr
    mov ax, bx
.norr:
    mov [tui_rend], ax
    mov ax, [tui_lay + TL_SCW]
    sub ax, 24
    cmp ax, 128
    jbe .vok
    mov ax, 128
.vok:
    mov [tui_vuw], ax
    mov byte [tui_inited], 1
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_track - adopt the live content origin and size (top of every draw; the
;             window moves while the worker sleeps, and under WF_FULL the
;             kernel answers (0,0) and the whole screen - SPEC.md 11.2)
; -----------------------------------------------------------------------------
tui_track:
    push ax
    push bx
    push cx
    push dx
    cmp byte [trk_fs], 0            ; on the fsx surface (SPEC.md 53) the
    je .win                         ; SURFACE is the content, and it is asked
                                    ; for rather than assumed (SPEC.md 53.7.1).
                                    ; fsx does not resize the window record the
                                    ; way WF_FULL did, so the kernel would
                                    ; answer the WINDOWED box - and the old
                                    ; answer here, (0,0) plus the live adapter
                                    ; size, is only right while the desktop is
                                    ; ONE display: a same-mode bracket does not
                                    ; collapse a two-display desktop (39.18.3),
                                    ; so (0,0) is the PRIMARY and Tracker
                                    ; fullscreened on the second monitor drew
                                    ; the FT2 screen over the OTHER one
    call OSAPI_FSX_SURF             ; AX = x, BX = y, CX = w, DX = h
    jc .fsold                       ; refused: not in a bracket after all -
                                    ; keep the historical answer rather than
                                    ; draw at a garbage origin
    mov [tui_ox], ax
    mov [tui_oy], bx
    mov [tui_cw], cx
    mov [tui_ch], dx
    mov [tui_vw], cx                ; ...and the fullscreen EXTENT, which most
    mov [tui_vh], dx                ; of this file's fills reach for by name.
                                    ; Written whatever the layout does below,
                                    ; so a surface that shares the screen's
                                    ; height but not its width is still right
    cmp dx, [tui_layh]              ; ...and the layout is picked by HEIGHT, so
    je .out                         ; a surface of a different shape from the
    mov bx, [trk_win]               ; screen needs its own (see tui_layout_for)
    call OSAPI_FSX_CAPS             ; DL = THIS display's VID_* kind - the only
    mov dh, 1                       ; place the DEPTH can come from, since
    cmp dl, VID_VGA                 ; OSAPI_VIDEO answers about the primary and
    je .fscol                       ; the two cards of a mixed machine disagree
    cmp dl, VID_EGA                 ; ...and EGA is that same planar 4bpp path
    jne .fsdep                      ; at 640x350 (SPEC.md 39.24)
.fscol:
    mov dh, 4
.fsdep:
    mov ax, [tui_cw]
    mov bx, [tui_ch]
    call tui_layout_for
    jmp short .out
.fsold:
    xor ax, ax
    mov [tui_ox], ax
    mov [tui_oy], ax
    mov ax, [tui_vw]
    mov [tui_cw], ax
    mov ax, [tui_vh]
    mov [tui_ch], ax
    jmp short .out
.win:
    mov ax, [tui_layh]              ; ...and the way back: a bracket that took
    cmp ax, [tui_scrh]              ; a differently-shaped surface left the
    je .winok                       ; tables AND [tui_vw]/[tui_vh] picked for
    call tui_layout_init            ; IT, and the windowed face is drawn
.winok:                             ; against the SCREEN's class. Both come
                                    ; back in the one call
    mov bx, [trk_win]
    call OSAPI_WM_CONTENT           ; AX = content left, DX = top
    mov [tui_ox], ax
    mov [tui_oy], dx
    call OSAPI_WM_GEOM              ; CX = content width, DX = height
    mov [tui_cw], cx
    mov [tui_ch], dx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Content-coordinate wrappers (the ark_fillc pattern): everything above
; measures from the content origin; only these add it. All preserve every
; register (tui_clipc returns CF).
; =============================================================================
tui_fillc:
    push ax
    push bx
    push cx
    push dx
    add ax, [tui_ox]
    add cx, [tui_ox]
    add bx, [tui_oy]
    add dx, [tui_oy]
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tui_framec:
    push ax
    push bx
    push cx
    push dx
    add ax, [tui_ox]
    add cx, [tui_ox]
    add bx, [tui_oy]
    add dx, [tui_oy]
    call OSAPI_GFX_FRAME
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tui_grayc:
    push ax
    push bx
    push cx
    push dx
    add ax, [tui_ox]
    add cx, [tui_ox]
    add bx, [tui_oy]
    add dx, [tui_oy]
    call OSAPI_GFX_FILL_GRAY
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tui_hlinec:                         ; AX = x1, BX = x2, DX = y
    push ax
    push bx
    push dx
    add ax, [tui_ox]
    add bx, [tui_ox]
    add dx, [tui_oy]
    call OSAPI_GFX_HLINE
    pop dx
    pop bx
    pop ax
    ret

tui_vlinec:                         ; AX = x, BX = y1, DX = y2
    push ax
    push bx
    push dx
    add ax, [tui_ox]
    add bx, [tui_oy]
    add dx, [tui_oy]
    call OSAPI_GFX_VLINE
    pop dx
    pop bx
    pop ax
    ret

tui_textc:                          ; SI = string, CX = x, DX = y
    push cx
    push dx
    add cx, [tui_ox]
    add dx, [tui_oy]
    call OSAPI_FONT_STR_XPARENT
    pop dx
    pop cx
    ret

; tui_runc - one OPAQUE text run: SI = string, CX = x, DX = y,
;            AL = ink role, AH = background role (TC_*)
;
; On a mono adapter this is OSAPI_FONT_RUN (SPEC.md 6.1), which paints the
; cells' background and their glyphs in one pass and - because the pattern
; columns are laid out on 8-pixel boundaries - writes each cell row as a
; single store instead of a shift and a read-modify-write. That is the whole
; of the pattern view's text cost on the machines that cannot afford it, and
; it is measured rather than hoped: for a run this length it is 1.26x the
; instructions and 2.85x the framebuffer traffic (SPEC.md 6.1.1), the same on
; Hercules and on CGA. The harness that says so is apps/fontbench, which lives
; on the testing branch - the numbers are recorded in 6.1.1 either way.
;
; On a colour adapter [vid_mono] is 0, so the kernel would fall back to its
; own fill plus FONT_STR - a second fill of
; ground the caller has already covered. There it stays FONT_STR: the row
; band was filled once, which is cheaper than filling it again per run.
tui_runc:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [tui_mono], 0
    jne .run
    call tui_setcol                 ; colour: the caller already filled
    call tui_textc
    jmp short .out
.run:
    push ax
    mov bl, ah                      ; resolve both roles through the table
    xor bh, bh
    mov ah, [tui_col + bx]
    mov [tui_rbg], ah
    pop ax
    mov bl, al
    xor bh, bh
    mov al, [tui_col + bx]
    mov ah, [tui_rbg]
    add cx, [tui_ox]
    add dx, [tui_oy]
    call OSAPI_FONT_RUN
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tui_runo - the same run, OPAQUE ON EVERY ADAPTER (SPEC.md 45.20)
;
; tui_runc's gate is right for the PATTERN VIEW, whose caller fills the row band
; once and would pay for it twice on a colour adapter - and wrong for a field
; that owns its own background, which is every tui_rdout readout. This entry is
; that second case: it skips the test and takes the run, so on colour
; font_run's OWN fallback (a gfx_fill of the run's rect, then font_str over it)
; does the fill rather than tui_rdout doing it by hand. Same pixels, one path,
; and the pen is aligned on both adapters instead of only on mono.
;
; An ENTRY POINT rather than a flag argument because the five pushes and the
; epilogue are shared exactly, and a flag would want a register nothing here has
; spare: AL and AH are the two colour ROLES and BX is the lookup's scratch.
tui_runo:
    push ax
    push bx
    push cx
    push dx
    push si
    jmp tui_runc.run

; tui_charc - AL = char, CX = x, DX = y: ONE CELL, opaque
;
; A one-character STRING rather than a font_char, because there is no opaque
; font_char and a run of one cell is the same call at the same cost - fractal's
; zoom exponent took the same shape (SPEC.md 40.2.2). Its one caller,
; tui_el_scopes, fills the cell with TC_BG and bevels it two instructions
; earlier, so the pair is the caller's own ground and its own TC_TEXT.
tui_charc:
    push ax
    push cx
    push dx
    push si
    mov [tui_cbuf], al
    mov byte [tui_cbuf + 1], 0
    mov si, tui_cbuf
    mov ax, (TC_BG << 8) | TC_TEXT  ; AL = ink role, AH = ground role
    call tui_runo
    pop si
    pop dx
    pop cx
    pop ax
    ret

tui_clipc:                          ; AX,BX,CX,DX rect -> CF=1 partly covered
    push ax
    push bx
    push cx
    push dx
    add ax, [tui_ox]
    add cx, [tui_ox]
    add bx, [tui_oy]
    add dx, [tui_oy]
    call OSAPI_WM_CLIP_TEST         ; CF survives the pops below
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_setcol - AL = colour TABLE INDEX -> the global pen (lock held: the pen
;              is one machine-wide byte, SPEC.md 20.6 rule 7)
; -----------------------------------------------------------------------------
tui_setcol:
    push ax
    push bx
    mov bl, al
    xor bh, bh
    mov al, [tui_col + bx]
    call OSAPI_SET_COLOR
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_bevelc - the FT2 3D edge: light top/left, dark bottom/right
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (content); preserves all
; -----------------------------------------------------------------------------
tui_bevelc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, ax                      ; x1
    mov di, bx                      ; y1
    TSETC TC_BEVL
    mov ax, si
    mov bx, cx
    push dx
    mov dx, di
    call tui_hlinec                 ; top
    pop dx
    mov ax, si
    mov bx, di
    call tui_vlinec                 ; left
    TSETC TC_BEVD
    mov ax, si
    mov bx, cx
    call tui_hlinec                 ; bottom (DX = y2)
    mov ax, cx
    mov bx, di
    call tui_vlinec                 ; right
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_facec - a desktop element: face fill + bevel
; tui_deskfill - the desktop background: solid on VGA, 50% dither on mono
; in:  AX,BX,CX,DX rect (content); preserve all
; -----------------------------------------------------------------------------
tui_facec:
    TSETC TC_FACE
    call tui_fillc
    call tui_bevelc
    ret

tui_deskfill:
    cmp byte [tui_mono], 0
    jne .mono
    TSETC TC_FACE
    call tui_fillc
    ret
.mono:
    call tui_grayc
    ret

; =============================================================================
; Small text helpers
; =============================================================================

; tui_scpy - copy the NUL string at SI to DI (NUL not copied), DI advances
tui_scpy:
    push ax
    push si
.l:
    mov al, [si]
    or al, al
    jz .d
    mov [di], al
    inc si
    inc di
    jmp .l
.d:
    pop si
    pop ax
    ret

; tui_hexb - AL as two uppercase hex chars at DI; DI advances by 2
tui_hexb:
    push ax
    push bx
    push cx
    mov ah, al
    mov bl, al
    mov cl, 4
    shr bl, cl
    xor bh, bh
    mov al, [tui_hexd + bx]
    mov [di], al
    inc di
    mov bl, ah
    and bl, 0Fh
    xor bh, bh
    mov al, [tui_hexd + bx]
    mov [di], al
    inc di
    pop cx
    pop bx
    pop ax
    ret

; tui_hex2 - AL -> tui_nbuf as 'XX', NUL-terminated
tui_hex2:
    push di
    mov di, tui_nbuf
    call tui_hexb
    mov byte [di], 0
    pop di
    ret

; tui_hexnp - the pattern count as two hex digits, clamped.
;
; [mp_npat] is a WORD (SPEC.md 45.5.1) and this readout is FT2's two-digit
; one, so the single value that does not fit - P = 256, reachable now that a
; module may be larger than 128KB - is shown as FF rather than as AL, which
; would print it as 00 and say the module has no patterns at all.
tui_hexnp:
    push ax
    mov ax, [mp_npat]
    cmp ax, 255
    jbe .fits
    mov al, 255
.fits:
    call tui_hex2
    pop ax
    ret

; tui_dec3 - AL -> tui_nbuf as three decimal digits, NUL-terminated
tui_dec3:
    push ax
    push bx
    xor ah, ah
    mov bl, 100
    div bl                          ; AL = hundreds, AH = rest
    add al, '0'
    mov [tui_nbuf], al
    mov al, ah
    xor ah, ah
    mov bl, 10
    div bl
    add al, '0'
    add ah, '0'
    mov [tui_nbuf + 1], al
    mov [tui_nbuf + 2], ah
    mov byte [tui_nbuf + 3], 0
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_rdout - ONE readout field: the value, opaque, in its own rect.
; in:  SI = NUL string, CX = x, DX = y (content), BX = field width px,
;      AH = background colour index, AL = text colour index
; preserves all registers
;
; IT WAS AN ERASE-AND-LETTER PAIR AND THAT IS PERFORMANCE.md PART 1's
; DOUBLE-DRAW FLASH. Every value readout in the windowed view goes through
; here - Pos, Row, BPM, Spd, Ptn, Np, the song title, the status line - and
; the pair left the field BLANK between the fill and the last glyph. On an
; 8088 that gap is tens of milliseconds, and the Pos/Row line redraws about
; seven times a second, so the field visibly strobes. Reported from the field
; as "windowed mode is drawing flashing characters".
;
; It is now ONE OSAPI_FONT_RUN on EVERY adapter (SPEC.md 6.1, 45.20): the
; string is space-padded to the field's width and a space paints background, so
; THE PADDING IS THE ERASE and every cell goes from its old contents to its
; final contents in one pass. There is no moment at which the field is empty.
;
; On mono that pass is font_run's single-store cell path. On a colour adapter
; font_run's own FALLBACK is a gfx_fill of the run's rect plus font_str over
; it - which is exactly the erase-and-letter pair this routine used to write by
; hand there, so the pixels are the same and the pen is now aligned. tui_runo
; rather than tui_runc is what asks for that: tui_runc's mono gate is right for
; the PATTERN VIEW, whose caller fills the row band once and would pay for it
; twice, and wrong for a field that owns its own background.
;
; Two things earn the fast path and both are needed. The window is WF_SNAP
; (SPEC.md 11.94), so [tui_ox] is a multiple of 8; and the field's x must be
; too, which is what the round-down below is for. Rounding DOWN rather than
; moving the callers is safe because the up-to-7 extra pixels are inside the
; field's own background - a value sits in a box face with its label a cell or
; more to the left - and it makes every caller aligned, including ones not yet
; written. The one site where the pixels to the left are NOT this field's
; background is the song title, whose x is 296 rather than 295 for exactly
; this reason. Measured: of every windowed readout only the STATUS LINE was
; unaligned on VGA, and its pen moves from content x = 4 to x = 0 - where the
; two mono adapters have always drawn it, the widening having been theirs.
;
; A string longer than the field is TRUNCATED - on every adapter now - where it
; used to overflow the erase rect and draw outside it. That is the better
; behaviour and no shipped string reaches it: the widest field is the status
; line, 51 cells at [tui_cw] = 418, and the longest string sent to it is
; 'ENTER play  SPACE stop  HOME top  P pattern' at 43.
; -----------------------------------------------------------------------------
; The windowed position readout's row, measured UP from the bottom of the
; content. It was 22, which put it on rows [ch-22 .. ch-15] - and on the two
; adapters where wm_fit squeezes the 180-row template (CGA gives [tui_ch] =
; 136) that is 114..121, straight through the splash's last line at 108..115.
; The gap between that line and the status line at ch-10 is where it belongs:
; 38 clears the splash at every geometry this window has (ch = 136 puts it at
; 98..105, against a keys line ending at 95) and still clears the status line.
TUI_WPOSY   equ 38
TUI_WPOSW   equ 9                   ; 'Pos xx/yy' - SPEC.md 45.16.6: the ROW is
                                    ; NOT here, because windowed it cannot be
                                    ; right and the position always is

TUI_RDCELLS equ 56                  ; the widest field is [tui_cw]/8 and the
                                    ; frame is TRK_WINW = 420, so 52 cells;
                                    ; the clamp is a bound, not a budget
tui_rdout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [tui_tcol], al
    mov [tui_ecol], ah
    mov ax, cx                      ; widen the field LEFT to the byte boundary
    and ax, 7                       ; on EVERY adapter (SPEC.md 45.20). It was
    sub cx, ax                      ; mono-only, on the premise that what an
    add bx, ax                      ; aligned pen buys is font_run's
                                    ; single-store cell path - which is 1bpp's
                                    ; alone. Mode 12h is also eight pixels to a
                                    ; byte, four planes of one bit, so an
                                    ; unaligned cell spills into a second
                                    ; framebuffer byte there too and font_char
                                    ; pays a second GC8 Bit Mask `out` plus a
                                    ; second latched read-modify-write for it,
                                    ; once per row per cell (11.94). VGA gains
                                    ; 9.4% against mono's 3.4%
    mov [tui_rdw], bx
    mov [tui_rdx], cx
    mov [tui_rdy], dx
    mov ax, cx                      ; the clip gate runs on the rect actually
    mov bx, dx                      ; drawn, widened or not
    mov cx, [tui_rdw]
    add cx, ax
    dec cx                          ; x2
    add dx, 7                       ; y2
    call tui_clipc
    jc .out
    push ds                         ; stosb writes through ES, and ES is
    pop es                          ; KERNEL_SEG on entry to a callback
    cld
    mov ax, [tui_rdw]
    mov cl, 3
    shr ax, cl                      ; AX = cells the field spans
    cmp ax, TUI_RDCELLS
    jbe .cok
    mov ax, TUI_RDCELLS
.cok:
    mov cx, ax
    mov di, tui_rbuf
.cp:
    jcxz .cdone                     ; the value, then spaces to the field's
    lodsb                           ; end - the pad is what erases what was
    or al, al                       ; there before
    jz .pad
    stosb
    dec cx
    jmp short .cp
.pad:
    mov al, ' '
    rep stosb
.cdone:
    mov byte [di], 0
    mov si, tui_rbuf
    mov cx, [tui_rdx]
    mov dx, [tui_rdy]
    mov al, [tui_tcol]
    mov ah, [tui_ecol]
    call tui_runo               ; opaque on BOTH adapters now (SPEC.md 45.20):
.out:                           ; on colour font_run's own fallback is the fill
                                ; and the font_str this used to write by hand
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The status/message line (tui_msg is the export tracker.asm calls)
; =============================================================================
tui_msg:                            ; SI = NUL string; lock held
    push si
    mov [tui_msgp], si
    call tui_msg_draw
    pop si
    ret

tui_msg_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [trk_tx], 0            ; the XT text screen (SPEC.md 45.13) owns
    jne .out                        ; the adapter: every kernel drawing slot
                                    ; renders DESKTOP geometry after an
                                    ; OSAPI_FSX_MODE call (SPEC.md 53.1). The
                                    ; message is already in [tui_msgp] and
                                    ; ttx_draw_dyn's status line spends it, so
                                    ; recording it IS delivering it
    mov si, [tui_msgp]
    or si, si
    jnz .have
    call tui_hintp
.have:
    cmp byte [trk_fs], 0
    je .win
    mov cx, [tui_lay + TL_STX]
    mov dx, [tui_lay + TL_STY]
    mov bx, [tui_lay + TL_STW]
    mov ah, TC_STATB
    mov al, TC_STATT
    call tui_rdout
    jmp .out
.win:
    mov cx, 4
    mov dx, [tui_ch]
    sub dx, 10
    mov bx, [tui_cw]
    sub bx, 8
    mov ah, TC_WBG
    mov al, TC_WTX
    call tui_rdout
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Desktop elements (full layouts). Each is gated whole on its own rect: a
; partly covered element stays STALE, never half-blank, and the every-16th
; top refresh repaints it once it is uncovered.
; =============================================================================

; --- position editor (0,0)-(111,76): 5-line order window with the current
; --- order on a band, plus song length and position readouts ------------------
tui_el_posed:
    mov byte [tui_vonly], 0         ; full: face, bevel, labels, values
    jmp short tui_posed_body
; --- ...and the same box with only the parts a position move can change ------
; Its five order lines DO all move - the window scrolls under a fixed
; highlight - so they are redrawn, but each erases only its own 8-row band
; instead of the box being refilled and bevelled underneath them. 'Len' and
; 'Pos' are labels and stay; the song length cannot change while a module is
; loaded, so only the Pos value is relettered.
tui_val_posed:
    mov byte [tui_vonly], 1
tui_posed_body:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor ax, ax
    xor bx, bx
    mov cx, 111
    mov dx, 76
    call tui_clipc
    jc .out
    cmp byte [tui_vonly], 0
    jne .lines
    call tui_facec
.lines:
    mov byte [tui_chn], 0           ; k = order-window line 0..4
.k:
    mov al, [tui_chn]
    xor ah, ah
    mov bl, 9
    mul bl
    add ax, 5
    mov dx, ax                      ; line y: 5, 14, 23, 32, 41
    cmp byte [tui_vonly], 0         ; no box fill under us: erase this line's
    je .band                        ; own band, which is all that moved
    cmp byte [tui_chn], 2
    je .band                        ; (line 2's band fill below erases it)
    push dx
    mov ax, 3
    mov bx, dx
    mov cx, 108
    add dx, 7
    TSETC TC_FACE
    call tui_fillc
    pop dx
.band:
    cmp byte [tui_chn], 2
    jne .plain
    push dx
    mov ax, 3                       ; the band line (FT2's highlighted order)
    mov bx, 23
    mov cx, 108
    mov dx, 31
    TSETC TC_BAND
    call tui_fillc
    pop dx
    add dx, 1
    mov al, TC_BANDT
    jmp .col
.plain:
    mov al, TC_FACET
.col:
    call tui_setcol
    mov al, [tui_apos]              ; ord = songpos - 2 + k
    xor ah, ah
    sub ax, 2
    mov bl, [tui_chn]
    xor bh, bh
    add ax, bx
    cmp ax, 0
    jl .next
    mov bl, [mp_songlen]
    xor bh, bh
    cmp ax, bx
    jge .next
    push dx
    mov di, tui_nbuf                ; 'oo pp' - order index, pattern number
    call tui_hexb
    mov byte [di], ' '
    inc di
    push di
    call mp_order                   ; AL order in -> AL pattern out (clobbers)
    pop di
    call tui_hexb
    mov byte [di], 0
    pop dx
    mov si, tui_nbuf
    mov cx, 8
    call tui_textc
.next:
    inc byte [tui_chn]
    cmp byte [tui_chn], 5
    jb .k
    cmp byte [tui_vonly], 0
    jne .pos                        ; 'Len' and 'Pos' are labels, and the song
    TSETC TC_FACET                  ; length cannot change while a module is
    mov si, tui_s_len               ; loaded: only the Pos value moves
    mov cx, 6
    mov dx, 56
    call tui_textc
    mov al, [mp_songlen]
    call tui_hex2
    mov si, tui_nbuf
    mov cx, 38
    call tui_textc
    mov si, tui_s_posl2
    mov cx, 6
    mov dx, 66
    call tui_textc
    TSETC TC_FACET
    mov al, [tui_apos]
    call tui_hex2
    mov si, tui_nbuf
    mov cx, 38
    mov dx, 66
    call tui_textc
    jmp short .out
.pos:
    mov al, [tui_apos]
    call tui_hex2
    mov si, tui_nbuf
    mov cx, 38
    mov dx, 66
    mov bx, 16
    mov ah, TC_FACE
    mov al, TC_FACET
    call tui_rdout
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Value-only redraws (SPEC.md 45.12)
;
; The tui_el_* routines above are FULL element draws: box face, bevel, static
; labels, values. That is what a repaint wants and it is the wrong thing to
; run once a beat. tui_el_bpm refilled a 93x44 box, redrew its bevel and
; relettered 'BPM' and 'Spd' to change one digit of tempo; tui_el_ptn did the
; same for 'Ptn', 'Ln', '64' and 'Np', of which only two are values at all
; and one of those cannot move while a module plays.
;
; These draw the VALUES and nothing else, each as one tui_rdout - a fill of
; exactly the field's rect and the digits back into it - which is the same
; erase-and-letter pair the compact layout has always used (tui_val_cga_*).
; The face, the bevel and every label are drawn once by tui_draw_top and then
; left alone, because nothing about them changes while the module plays.
; =============================================================================

; --- BPM / Spd values, inside the box tui_el_bpm draws ------------------------
tui_val_bpm:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [tui_abpm]
    call tui_dec3
    mov si, tui_nbuf
    mov cx, 168
    mov dx, 36
    mov bx, 24
    mov ah, TC_FACE
    mov al, TC_FACET
    call tui_rdout
    mov al, [tui_aspd]
    call tui_dec3
    mov si, tui_nbuf
    mov cx, 168
    mov dx, 50
    mov bx, 24
    mov ah, TC_FACE
    mov al, TC_FACET
    call tui_rdout
    mov al, [tui_abpm]
    mov [tui_lbpm], al
    mov al, [tui_aspd]
    mov [tui_lspd], al
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- Ptn / Np values ('Ln 64' is a constant and is never redrawn) -------------
tui_val_ptn:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [tui_apat]
    call tui_hex2
    mov si, tui_nbuf
    mov cx, 258
    mov dx, 36
    mov bx, 16
    mov ah, TC_FACE
    mov al, TC_FACET
    call tui_rdout
    call tui_hexnp
    mov si, tui_nbuf
    mov cx, 258
    mov dx, 64
    mov bx, 16
    mov ah, TC_FACE
    mov al, TC_FACET
    call tui_rdout
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the nameplate strip (112,0)-(290,31) --------------------------------------
tui_el_logo:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, 112
    xor bx, bx
    mov cx, 290
    mov dx, 31
    call tui_clipc
    jc .out
    TSETC TC_ACC
    call tui_fillc
    call tui_bevelc
    mov si, tui_s_logo
    cmp byte [tui_mono], 0          ; drop shadow: black on the blue plate;
    jne .plain                      ; on mono the plate IS black - skip it
    TSETC TC_BEVD
    mov cx, 150
    mov dx, 13
    call tui_textc
.plain:
    TSETC TC_ACCT
    mov cx, 149
    mov dx, 12
    call tui_textc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- BPM / Spd box (112,32)-(205,76) -------------------------------------------
tui_el_bpm:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, 112
    mov bx, 32
    mov cx, 205
    mov dx, 76
    call tui_clipc
    jc .out
    call tui_facec
    TSETC TC_FACET
    mov si, tui_s_bpm
    mov cx, 116
    mov dx, 36
    call tui_textc
    mov al, [tui_abpm]
    call tui_dec3
    mov si, tui_nbuf
    mov cx, 168
    call tui_textc
    mov si, tui_s_spd
    mov cx, 116
    mov dx, 50
    call tui_textc
    mov al, [tui_aspd]
    call tui_dec3
    mov si, tui_nbuf
    mov cx, 168
    call tui_textc
    mov al, [tui_abpm]
    mov [tui_lbpm], al
    mov al, [tui_aspd]
    mov [tui_lspd], al
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- Ptn / Ln box (206,32)-(290,76) --------------------------------------------
tui_el_ptn:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, 206
    mov bx, 32
    mov cx, 290
    mov dx, 76
    call tui_clipc
    jc .out
    call tui_facec
    TSETC TC_FACET
    mov si, tui_s_ptn
    mov cx, 210
    mov dx, 36
    call tui_textc
    mov al, [tui_apat]
    call tui_hex2
    mov si, tui_nbuf
    mov cx, 258
    call tui_textc
    mov si, tui_s_ln
    mov cx, 210
    mov dx, 50
    call tui_textc
    mov si, tui_s_64
    mov cx, 258
    call tui_textc
    mov si, tui_s_np
    mov cx, 210
    mov dx, 64
    call tui_textc
    call tui_hexnp
    mov si, tui_nbuf
    mov cx, 258
    call tui_textc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- status bar (0,77)-(290,91) ------------------------------------------------
tui_el_status:
    push ax
    push bx
    push cx
    push dx
    push si
    xor ax, ax
    mov bx, 77
    mov cx, 290
    mov dx, 91
    call tui_clipc
    jc .out
    TSETC TC_STATB
    call tui_fillc
    call tui_bevelc
    mov si, [tui_msgp]
    or si, si
    jnz .have
    call tui_hintp
.have:
    TSETC TC_STATT
    mov cx, [tui_lay + TL_STX]
    mov dx, [tui_lay + TL_STY]
    call tui_textc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_scellxy - scope cell AL (0..3) -> [tui_t1] = x0, [tui_t2] = y0
; preserves all registers
; -----------------------------------------------------------------------------
tui_scellxy:
    push ax
    push bx
    push dx
    xor ah, ah
    div byte [tui_lay + TL_SCPR]    ; AL = row, AH = column
    mov bl, al
    mov al, ah
    xor ah, ah
    mul word [tui_lay + TL_SCDX]
    add ax, [tui_lay + TL_SCX]
    mov [tui_t1], ax
    mov al, bl
    xor ah, ah
    mul word [tui_lay + TL_SCDY]
    add ax, [tui_lay + TL_SCY]
    mov [tui_t2], ax
    pop dx
    pop bx
    pop ax
    ret

; --- the scopes: volume-bar stand-ins for FT2's oscilloscopes ------------------
; Frames, channel numbers and MUTE flags here (redrawn on mute change and by
; the top refresh); the bars themselves are tui_draw_vu's, every frame.
tui_el_scopes:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [tui_chn], 0
.cell:
    mov al, [tui_chn]
    call tui_scellxy
    mov ax, [tui_t1]
    mov bx, [tui_t2]
    mov cx, ax
    add cx, [tui_lay + TL_SCW]
    dec cx
    mov dx, bx
    add dx, [tui_lay + TL_SCH]
    dec dx
    call tui_clipc
    jc .next
    TSETC TC_BG
    call tui_fillc
    call tui_bevelc
    TSETC TC_TEXT
    mov cx, [tui_t1]
    mov dx, [tui_t2]
    cmp word [tui_lay + TL_SCH], 16 ; compact cells centre differently
    jb .csm
    add cx, 4
    add dx, 2
    jmp .dig
.csm:
    inc cx
    inc dx
.dig:
    mov al, [tui_chn]
    add al, '1'
    call tui_charc
    mov bl, [tui_chn]
    xor bh, bh
    cmp byte [mp_mute + bx], 0
    je .next
    cmp word [tui_lay + TL_SCH], 16
    jb .msm
    mov si, tui_s_mute
    add cx, 12
    call tui_textc
    jmp .next
.msm:
    mov al, 'M'
    mov cx, [tui_t1]
    add cx, 146
    call tui_charc
.next:
    inc byte [tui_chn]
    cmp byte [tui_chn], 4
    jb .cell
    ; the Hercules layout has no instrument list: the song title rides under
    ; the scope row instead
    test byte [tui_lay + TL_FLAGS], TLF_INSTR | TLF_CMPCT
    jnz .sync
    mov si, mp_title
    mov cx, 296                     ; byte-aligned: tui_rdout rounds DOWN and
                                    ; the 7 pixels left of 295 are the scope
                                    ; row, not this field's background
    mov dx, 52
    mov bx, 176
    mov ah, TC_STATB
    mov al, TC_STATT
    call tui_rdout
.sync:
    mov ax, [mp_mute]               ; remember what we drew, so tui_draw_dyn
    mov [tui_lmute], ax             ; can see a toggle
    mov ax, [mp_mute + 2]
    mov [tui_lmute + 2], ax
    xor ax, ax                      ; the cells were just repainted: the
    mov [tui_vupx], ax              ; delta-drawn bars restart from empty
    mov [tui_vupx + 2], ax          ; (SPEC.md 45.12)
    mov [tui_vupx + 4], ax
    mov [tui_vupx + 6], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_btn - one FT2-style button: face, bevel, label
; in:  CX = x, DX = y (content), SI = label; preserves all
; -----------------------------------------------------------------------------
tui_btn:
    push ax
    push bx
    push cx
    push dx
    mov ax, cx
    mov bx, dx
    add cx, 59
    add dx, 13
    push cx
    push dx
    mov cx, ax
    add cx, 59
    mov dx, bx
    add dx, 13
    TSETC TC_BTN
    call tui_fillc
    call tui_bevelc
    pop dx
    pop cx
    sub cx, 55                      ; label at x+4, y+3
    sub dx, 10
    TSETC TC_BTNT
    call tui_textc
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the two button stacks (291,0)-(355,172) and (356,0)-(420,172) -------------
; Decorative in v1 (SPEC.md 45: every action is a key), but they are the FT2
; signature and each names the key that does the same thing.
tui_el_menus:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, 291
    xor bx, bx
    mov cx, 355
    mov dx, 172
    call tui_clipc
    jc .right
    call tui_facec
    mov di, tui_mbl
    mov cx, 294
    call tui_stack
.right:
    mov ax, 356
    xor bx, bx
    mov cx, 420
    mov dx, 172
    call tui_clipc
    jc .out
    call tui_facec
    mov di, tui_mbr
    mov cx, 359
    call tui_stack
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tui_stack:                          ; DI = dw label table (0-ended), CX = x
    push cx
    push dx
    push si
    push di
    mov dx, 2
.b:
    mov si, [di]
    or si, si
    jz .done
    call tui_btn
    add di, 2
    add dx, 17
    jmp .b
.done:
    pop di
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; tui_insget - instrument AL's 22-byte name (raw blob bytes, hostile) into
;              tui_insbuf: 17 chars kept, control bytes spaced, NUL added
; out: tui_insbuf; preserves registers (ES restored per the mp_insname rule)
; -----------------------------------------------------------------------------
tui_insget:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mp_insname                 ; -> ES:SI, CF=1 none/invalid
    jc .none
    mov di, tui_insbuf
    mov cx, 17
.c:
    mov al, [es:si]
    cmp al, 32
    jb .sp
    cmp al, 126
    jbe .keep
.sp:
    mov al, ' '
.keep:
    mov [di], al
    inc si
    inc di
    loop .c
    mov byte [di], 0
    jmp .done
.none:
    mov byte [tui_insbuf], 0
.done:
    push ds
    pop es                          ; caller's ES = DS restored
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the instrument list + song name (421,0)-(631,172), VGA only ---------------
tui_el_instr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, 421
    xor bx, bx
    mov cx, 631
    mov dx, 172
    call tui_clipc
    jc .out
    call tui_facec
    mov byte [tui_chn], 0           ; instrument row 0..11
.row:
    mov al, [tui_chn]
    xor ah, ah
    mov bl, 11
    mul bl
    add ax, 4
    mov dx, ax                      ; row y
    TSETC TC_FACET
    mov al, [tui_chn]
    inc al
    call tui_hex2
    mov si, tui_nbuf
    mov cx, 425
    call tui_textc
    mov al, [tui_chn]
    inc al
    call tui_insget
    cmp byte [tui_insbuf], 0
    je .skip
    mov si, tui_insbuf
    mov cx, 447
    call tui_textc
.skip:
    inc byte [tui_chn]
    cmp byte [tui_chn], 12
    jb .row
    mov ax, 424                     ; the song-name textbox
    mov bx, 155
    mov cx, 587
    mov dx, 168
    TSETC TC_BG
    call tui_fillc
    TSETC TC_BEVD
    call tui_framec
    TSETC TC_TEXT
    mov si, mp_title
    mov cx, 428
    mov dx, 158
    call tui_textc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The compact (CGA 640x200) top: one title/readout line
; =============================================================================
tui_top_cga:
    push ax
    push bx
    push cx
    push dx
    push si
    xor ax, ax
    xor bx, bx
    mov cx, [tui_vw]
    dec cx
    mov dx, 8
    call tui_clipc                  ; the whole line is one erase+text unit
    jc .vals
    TSETC TC_BG
    call tui_fillc
    TSETC TC_TEXT
    mov si, tui_s_name
    xor cx, cx
    xor dx, dx
    call tui_textc
    mov si, tui_s_bpmc
    mov cx, 64
    call tui_textc
    mov si, tui_s_spdc
    mov cx, 136
    call tui_textc
    mov si, tui_s_posc
    mov cx, 200
    call tui_textc
    mov si, tui_s_ptnc
    mov cx, 288
    call tui_textc
.vals:
    call tui_val_cga_bs
    call tui_val_cga_pos
    call tui_msg_draw
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tui_val_cga_pos:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, tui_nbuf                ; 'pp/ll'
    mov al, [tui_apos]
    call tui_hexb
    mov byte [di], '/'
    inc di
    mov al, [mp_songlen]
    call tui_hexb
    mov byte [di], 0
    mov si, tui_nbuf
    mov cx, 232
    xor dx, dx
    mov bx, 40
    mov ah, TC_BG
    mov al, TC_TEXT
    call tui_rdout
    mov al, [tui_apat]
    call tui_hex2
    mov si, tui_nbuf
    mov cx, 320
    mov bx, 16
    mov ah, TC_BG
    mov al, TC_TEXT
    call tui_rdout
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tui_val_cga_bs:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [tui_abpm]
    call tui_dec3
    mov si, tui_nbuf
    mov cx, 96
    xor dx, dx
    mov bx, 24
    mov ah, TC_BG
    mov al, TC_TEXT
    call tui_rdout
    mov al, [tui_aspd]
    call tui_dec3
    mov si, tui_nbuf
    mov cx, 168
    mov bx, 24
    mov ah, TC_BG
    mov al, TC_TEXT
    call tui_rdout
    mov al, [tui_abpm]
    mov [tui_lbpm], al
    mov al, [tui_aspd]
    mov [tui_lspd], al
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The pattern editor - the defining FT2 element: rows scrolling through a
; fixed full-width band, hex row numbers on both sides, four channel columns
; of 'C-2 01 A0F' cells.
; =============================================================================

; --- the channel digit strip + rule --------------------------------------------
tui_hdr:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [tui_lay + TL_CHX]
    mov bx, [tui_lay + TL_HDRY]
    mov cx, [tui_chend]
    mov dx, bx
    add dx, 8
    call tui_clipc
    jc .out
    TSETC TC_BG
    call tui_fillc
    TSETC TC_TEXT
    mov byte [tui_chn], 0
.d:
    mov al, [tui_chn]
    xor ah, ah
    mul word [tui_lay + TL_CHW]
    add ax, [tui_lay + TL_CHX]
    mov cx, [tui_lay + TL_CHW]
    shr cx, 1
    add cx, ax
    sub cx, 4
    mov dx, [tui_lay + TL_HDRY]
    mov al, [tui_chn]
    add al, '1'
    call tui_charc
    inc byte [tui_chn]
    cmp byte [tui_chn], 4
    jb .d
    TSETC TC_SEP
    mov ax, [tui_lay + TL_CHX]
    mov bx, [tui_chend]
    mov dx, [tui_lay + TL_HDRY]
    add dx, 8
    call tui_hlinec
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_sync - publish the position the LISTENER is at (SPEC.md 45.15). Every
;            drawer calls this once, first thing, and then quotes tui_a*
;            instead of mp_*.
;
; mp_row and its neighbours are the row being MIXED, and trk_feed keeps the
; ring topped up to TRK_RING - TRK_HALF: at XT mode's 5,500 Hz that is 2.2-3.0
; SECONDS of music the card has not played yet. Quoting them put the whole
; screen that far ahead of the sound - measured, not modelled (press Enter,
; screendump 0.4 s later, and the grid is on row 21).
;
; So the display asks the stamps (SPEC.md 45.15) which row the card is inside.
; [trk_consumed] is the worker's own status poll, at most a tick old, which is
; a fifth of a row at the default tempo and can never move backwards.
;
; With no stream the two positions ARE the same thing, and that is also what
; keeps the stopped view live: Up/Down and the position keys move mp_*, and
; the display has to follow them with no music to sync to.
; -----------------------------------------------------------------------------
tui_sync:
    push ax
    push bx
    push dx
    push si
    cmp byte [trk_sopen], 0
    je .live
    cmp byte [mp_playing], 0
    je .live
%ifdef TRKLOG
    cmp byte [tlog_nosync], 0       ; Y: back on the mixer, to A/B this
    jne .live                       ; against the card (tests/trklog.inc)
%endif
    call tui_playpos                ; AX = where the card is, INTERPOLATED
    call mp_at_pos                  ; SI = the stamp being heard
    mov al, [si + MP_SP_SPOS]
    mov [tui_apos], al
    mov al, [si + MP_SP_PAT]
    mov [tui_apat], al
    mov al, [si + MP_SP_ROW]
    mov [tui_arow], al
    mov al, [si + MP_SP_BPM]
    mov [tui_abpm], al
    mov al, [si + MP_SP_SPD]
    mov [tui_aspd], al
    mov ax, [si + MP_SP_VOL]
    mov [tui_avol], ax
    mov ax, [si + MP_SP_VOL + 2]
    mov [tui_avol + 2], ax
    mov ax, [si + MP_SP_POS]        ; a row BECOMING AUDIBLE kicks the needles
    cmp ax, [tui_vlpos]             ; of the channels that struck in it
    je .out                         ; (SPEC.md 45.12.1). The gate is the
    mov [tui_vlpos], ax             ; stamp's stream position, which is unique
    xor bx, bx                      ; per row and monotonic - so this is
.kick:                              ; idempotent across the several drawers
    cmp byte [si + MP_SP_HIT + bx], 0   ; that call tui_sync in one frame, and
    je .kn                          ; it fires once per row however many
    mov al, [tui_avol + bx]         ; frames that row is on screen for
    mov [tui_vu + bx], al
.kn:
    inc bx
    cmp bx, 4
    jb .kick
    jmp short .out
.live:
    mov al, [mp_songpos]
    mov [tui_apos], al
    mov al, [mp_pattern]
    mov [tui_apat], al
    mov al, [mp_row]
    mov [tui_arow], al
    mov al, [mp_bpm]
    mov [tui_abpm], al
    mov al, [mp_speed]
    mov [tui_aspd], al
    mov ax, [mp_chvol]
    mov [tui_avol], ax
    mov ax, [mp_chvol + 2]
    mov [tui_avol + 2], ax
.out:
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_playpos - out AX = the stream byte the card is playing NOW (SPEC.md
;               45.15.1), interpolated between block interrupts
;
; [trk_consumed] is the truth and it is COARSE: the driver advances it one
; whole DMA half per block IRQ, which at XT mode's 5,500 Hz is 2,048 bytes -
; 372 ms, ~6.8 system ticks, about three rows. Following it raw makes the grid
; stand still for a third of a second and then jump three rows, measured
; exactly so in a SPEC.md 45.14 capture: the longest run of ticks with the
; screen row unchanged was 7 whatever the frame rate.
;
; So between blocks the position is ESTIMATED from elapsed ticks at the
; stream's own byte rate, and two properties make that safe rather than
; merely smoother:
;
;   BOUNDED BY WHAT WAS STAGED. The card cannot have played bytes the mixer
;   has not written, so [trk_total] is the cap - a physical bound rather than
;   an arbitrary one. It used to be "one block past the last REPORT", and the
;   field showed why that is not the same thing: [trk_consumed] is not sampled
;   per tick, it is whatever the WORKER last saw, and a capture has feed
;   passes 10 to 20 ticks apart. Through a gap like that the estimate hit the
;   clamp after one block and stopped, so the scroll ran three or four rows
;   and then froze for about a fifth of a second - reported from the field in
;   exactly those words. The card was playing normally throughout; only our
;   view of it had stalled.
;
;   AN ACCUMULATOR, NOT A STOPWATCH. [tui_lcons] is the model's own running
;   position and [tui_ct0] the tick it was last advanced on, so the elapsed
;   term is only ever 0, 1 or 2 ticks. Holding an anchor and multiplying a
;   growing elapsed against it overflows a word at 217 ticks - twelve seconds
;   - and the overflow branch then slammed the estimate to [trk_total], which
;   is the MIXER's position, nearly three seconds ahead of the music. The
;   field caught it and QEMU could not: the model only free-runs that long
;   when the report does not overtake it for twelve seconds, which needs a
;   machine whose byte rate matches the model as closely as the real one does.
;
;   ANCHORED ONLY FORWARD. A report is the driver's block counter, so it is
;   quantized DOWN: it names the last 2,048-byte boundary the card crossed and
;   is therefore up to a whole block BEHIND where the card actually is. The
;   model interpolates past that on purpose - so restarting the model at every
;   report threw the interpolation away and, with the monotone guard holding
;   the old answer, froze the display until the model climbed back. Up to a
;   block of it, which is 6.8 ticks; the field measured 26% of row-to-row gaps
;   at 4 ticks or more with exactly that shape. So the report re-anchors the
;   model only when it has OVERTAKEN it, which means the model ran slow and
;   the truth is the better answer. Drift the other way is bounded by the
;   staged cap above.
;
;   MONOTONE, and this is the load-bearing one. [tui_play] is the last answer
;   given and the estimate can never go below it. Without that, any
;   disagreement between the estimate and the next real block - a rate that
;   is a hair fast, a block that arrives early - moves the answer BACKWARDS,
;   and a scroll that jumps back a row reads far worse than one that steps.
;
;   SUB-TICK, and it is the same argument one level down (SPEC.md 45.15.2).
;   Everything above interpolates between the card's BLOCK reports, 2,048
;   bytes apart; what it leaves is a staircase whose step is the SYSTEM TICK,
;   because [tui_lcons] only moves when [ticks] does. At the XT rate a row is
;   2.55 ticks, so row changes land on tick edges in a 3,2,3,3,2 pattern -
;   165 ms, 110, 165, 165, 110 - and a 1.5x swing in the spacing of a scroll
;   is exactly the "microstutter" the field reported after the music itself
;   was smooth. The field capture is the proof rather than the theory: every
;   single-tick PLAY step in it is 0, 302 or 604 and nothing between.
;
;   The screen redraws far more often than the tick - on the SPEC.md 53.5.1
;   frame clock, exactly FSX_SUBTICK times - so the frames THEMSELVES are the
;   finer clock, and no other one is available: SPEC.md 34.1 reserves the ch0
;   latch and a package has no slot for it. So the frames of the PREVIOUS
;   tick are counted and this tick is divided into that many equal steps. It
;   cannot drift, because the sub-term is an offset ON TOP of the exact
;   accumulator and is thrown away at every tick edge rather than added into
;   it; it cannot go backwards, because the offset is capped one byte below a
;   whole tick, so the edge that zeroes it also adds [tui_bpt]; and a machine
;   that manages only one frame a tick divides by one and gets exactly the
;   old behaviour back.
;
;   THE DIVISOR IS [tui_fcnt] PLUS ONE, and the missing +1 was a stutter of
;   its own (SPEC.md 45.15.2). [tui_fcnt] is reset AT the tick edge by the
;   frame standing on it, so it ends the tick holding the number of frames
;   INSIDE the tick - two of the three the frame clock delivers. Divided by
;   two, a tick's three frames offered 0, bpt/2 and (capped) bpt/2 again:
;   the estimate froze for the last third of every tick and then jumped half
;   a tick, which is one row in six at the XT rate. Measured as [tui_fpt] = 2
;   on a machine measured at three frames a tick - the two numbers
;   disagreeing is the whole bug, and 0, bpt/3, 2*bpt/3 is what it should
;   always have been.
; -----------------------------------------------------------------------------
tui_playpos:
    push bx
    push cx
    push dx
    call OSAPI_GET_TICKS
    mov cx, ax                      ; CX = now, asked once
    sub cx, [tui_ct0]               ; CX = ticks since the model last moved -
    jz .subtick                     ; ALWAYS 0 or 1 or 2, because the model is
    add [tui_ct0], cx               ; an accumulator: the anchor advances with
    mov ax, cx                      ; it every call, so the multiply below can
    mul word [tui_bpt]              ; never overflow and the clock never has a
    add [tui_lcons], ax             ; long elapsed to multiply (see the header)

    mov al, [tui_fcnt]              ; a new tick: last tick's frame count is
    inc al                          ; this tick's divisor - PLUS ONE, because
    jnz .div                        ; [tui_fcnt] counts the frames INSIDE the
    dec al                          ; tick and the frame standing on the edge
.div:                               ; is a frame too (saturate, never wrap to
    mov [tui_fpt], al               ; 0: a 0 divisor is a #DE below)
    mov byte [tui_fcnt], 0          ; the edge itself carries no offset: the
    mov word [tui_sub], 0           ; whole tick just landed in [tui_lcons]
    xor ah, ah
    mov cx, ax
    xor dx, dx
    mov ax, [tui_bpt]
    div cx                          ; bpt / frames, so fcnt*bpf <= bpt always
    mov [tui_bpf], ax
    jmp short .have
.subtick:
    mov al, [tui_fcnt]              ; the k'th frame inside this tick. COUNTING
    inc al                          ; and CAPPING are separate on purpose: cap
    jnz .fok                        ; the counter and it can never exceed its
    dec al                          ; own previous value, so the divider
.fok:                               ; ratchets down to 1 and stays there
    mov [tui_fcnt], al
    cmp al, [tui_fpt]               ; past what the last tick held: leave the
    jae .have                       ; offset where it is rather than let it
    xor ah, ah                      ; reach a whole tick
    mul word [tui_bpf]              ; DX = 0 by the cap above
    mov cx, [tui_bpt]
    dec cx                          ; one byte short of the next tick edge:
    cmp ax, cx                      ; monotone across it by construction
    jbe .subok
    mov ax, cx
.subok:
    mov [tui_sub], ax
.have:
    mov ax, [tui_lcons]             ; AX = what the model says, plus however
    add ax, [tui_sub]               ; far into this tick the frames say we are

    mov bx, [trk_consumed]          ; --- the PHASE loop (SPEC.md 45.15.3) ---
    cmp bx, [tui_pcon]              ; a REPORT EDGE is the only place the
    je .phdone                      ; model's phase can be measured at all
    mov cx, bx
    sub cx, [tui_pcon]              ; exactly one block, or the poll was late
    mov [tui_pcon], bx              ; (a jump of two is not a measurement of
    cmp cx, TRK_HALF                ; phase - the model was right to run on)
    jne .phdone
    push ax
    sub ax, bx                      ; AX = the model's offset above the report
    mov cx, [tui_bpt]               ; ...against where it SHOULD be: the report
    shr cx, 1                       ; is seen about half a tick after the IRQ
    sub ax, cx                      ; that raised it, so that is the target
    sar ax, 1                       ; ...and a QUARTER of the error, so the
    sar ax, 1                       ; loop converges in a few blocks and never
    mov cx, ax                      ; steps the model back far enough to be
    pop ax                          ; seen (SPEC.md 45.15.3)
    sub ax, cx
    sub [tui_lcons], cx
.phdone:

    mov bx, [trk_consumed]          ; has the REPORT overtaken the model?
    sub bx, ax                      ; (wrap-safe: the sign decides)
    js .bound                       ; no - and that is the normal case: a
                                    ; report is quantized BEHIND us, and
                                    ; re-anchoring there throws the
                                    ; interpolation away
    mov ax, [trk_consumed]          ; yes: the model ran slow, so take the
    mov [tui_lcons], ax             ; truth - and the anchor is now HERE, so
    mov word [tui_sub], 0           ; the sub-tick offset that got us here is
                                    ; spent, not owed again
.bound:
    mov bx, [trk_total]             ; ...bounded by what the mixer has WRITTEN,
    sub bx, ax                      ; because the card cannot play what is not
    jns .mono                       ; there yet (wrap-safe: the sign decides)
    mov ax, [trk_total]
    mov [tui_lcons], ax             ; a capped model stays capped rather than
    mov word [tui_sub], 0           ; sprinting into a ceiling it cannot pass
.mono:
    mov bx, ax                      ; ...and it may never go backwards
    sub bx, [tui_play]
    jns .keep
    mov ax, [tui_play]
    jmp short .out
.keep:
    mov [tui_play], ax
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tui_viewrow - the row the band shows: the playing row, or the scroll row
;               (tracker.asm's Up/Down) while stopped
; out: [tui_cvr]
; -----------------------------------------------------------------------------
tui_viewrow:
    push ax
    mov al, [tui_arow]
    cmp byte [mp_playing], 0
    jne .set
    mov al, [tui_vrow]
.set:
    mov [tui_cvr], al
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_row1 - one pattern row strip: gated whole (SPEC.md 11.3), erased, then
;            row numbers + four cells. Rows outside 0..63 erase to blank.
; in:  BX = row (signed word), DX = strip y (content), [tui_band] = 1 on the
;      current-row band; preserves all registers
; -----------------------------------------------------------------------------
tui_row1:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [tui_band], 0
    jne .band
    mov ax, [tui_lay + TL_RNL]
    mov cx, [tui_rend]
    push bx
    mov bx, dx                      ; y1
    add dx, 7
    call tui_clipc
    jc .covered
    TSETC TC_BG
    call tui_fillc
    TSETC TC_SEP                    ; the 1px channel separators the erase
    mov si, [tui_lay + TL_CHW]      ; just took out
    mov ax, [tui_lay + TL_CHX]
    add ax, si
    dec ax
    mov di, 3
.sep:
    call tui_vlinec
    add ax, si
    dec di
    jnz .sep
    mov [tui_t1], bx                ; text y
    pop bx
    jmp .content
.band:
    mov ax, 2
    mov cx, [tui_vw]
    sub cx, 3
    push bx
    mov bx, dx
    add dx, 10                      ; the band is 11 rows: BANDY..LOWY-1
    call tui_clipc
    jc .covered
    TSETC TC_BAND
    call tui_fillc
    mov [tui_t1], bx
    add word [tui_t1], 2            ; band text sits 2 rows in
    pop bx
    jmp .content
.covered:
    pop bx
    jmp .out
.content:
    cmp bx, 0
    jl .out
    cmp bx, 63
    jg .out
    mov ax, (TC_BG << 8) | TC_TEXT  ; AL = ink role, AH = background role
    cmp byte [tui_band], 0
    je .col
    mov ax, (TC_BAND << 8) | TC_BANDT
.col:
    mov [tui_rrole], ax
    mov al, bl
    call tui_hex2                   ; the FT2 hex row number, both sides
    mov si, tui_nbuf
    mov cx, [tui_lay + TL_RNL]
    mov dx, [tui_t1]
    mov ax, [tui_rrole]
    call tui_runc
    mov cx, [tui_lay + TL_RNR]
    cmp cx, 0FFFFh
    je .cells
    mov ax, [tui_rrole]
    call tui_runc
.cells:
    mov byte [tui_chn], 0
.cl:
    mov al, [tui_chn]
    xor ah, ah
    mul word [tui_lay + TL_CHW]
    add ax, [tui_lay + TL_CHX]
    add ax, [tui_lay + TL_PAD]
    mov [tui_t2], ax                ; cell text x
    push bx
    mov al, [tui_apat]
    xor ah, ah
    mov di, ax
    mov ax, bx
    mov ah, [tui_chn]               ; AL = row, AH = channel, DI = pattern
    call mp_cell2txt                ; -> mp_celltxt 'C-2 01 A0F'; clobbers
    pop bx
    jc .nx
    mov si, mp_celltxt
    mov cx, [tui_t2]
    mov dx, [tui_t1]
    mov ax, [tui_rrole]
    call tui_runc
.nx:
    inc byte [tui_chn]
    cmp byte [tui_chn], 4
    jb .cl
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_draw_pat - the whole row area around the view row, and the change
;                markers tui_draw_dyn compares against
; -----------------------------------------------------------------------------
tui_marks:
    push ax
    call tui_viewrow
    mov al, [tui_arow]
    mov [tui_lrow], al
    mov al, [tui_apos]
    mov [tui_lpos], al
    mov al, [tui_apat]
    mov [tui_lpat], al
    mov al, [tui_vrow]
    mov [tui_lvrow], al
    mov al, [tui_cvr]
    mov [tui_lcvr], al              ; the scroll path's delta baseline (45.12)
    pop ax
    ret

; =============================================================================
; The 8088 single-line view (SPEC.md 45.9.1)
;
; XT mode already stopped the pattern view scrolling per ROW and made it
; repaint per POSITION (45.9). On a real 4.77 MHz 8088 that is still far more
; than the machine has: the view does not scroll, it does not even move -
; one line updates now and then and the rest stands still, which reads as a
; hung display rather than a slow one. The audio, meanwhile, is fine, and
; keeping it that way is the whole point.
;
; So on a tier-0 MACHINE, and only while something is playing, the pattern
; grid is not animated at all. The row area is cleared once when playback
; starts and a SINGLE banded line - the row being played, drawn by the same
; tui_row1 the grid uses - is updated in place. One row of four cells and two
; hex numbers per row change, against thirty-odd rows of the same.
;
; It is gated on the machine, not on [mp_xt]: XT mode is a playback setting
; anyone can switch on, and a 386 running it can animate the grid perfectly
; well. And it is gated on playback, so a stopped 8088 still gets the whole
; pattern to look at and Up/Down still scroll it.
;
; The line is centred in the cleared area rather than left at TL_BANDY. With
; the grid gone there is nothing for the band's usual one-third-down position
; to relate to, and a lone strip near the top of a large empty field looks
; like a rendering fault.
; =============================================================================

; tui_xt_y - where the single line goes; out AX = its top y
tui_xt_y:
    mov ax, [tui_vh]
    sub ax, [tui_lay + TL_ROWT]
    sub ax, 11                      ; the band is 11 rows (tui_row1)
    shr ax, 1
    add ax, [tui_lay + TL_ROWT]
    ret

; tui_xt_clear - blank the whole row area, so no stale grid is left behind,
;                and rule the channel separators down it
;
; The separators are what the grid drew per row (tui_row1's non-band path).
; Drawn once, full height, they cost nothing per frame and they are the
; difference between a line floating in a black field and a line sitting in
; the four-channel view it belongs to.
tui_xt_clear:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    xor ax, ax
    mov bx, [tui_lay + TL_ROWT]
    mov cx, [tui_vw]
    dec cx
    mov dx, [tui_vh]
    dec dx
    TSETC TC_BG
    call tui_fillc
    TSETC TC_SEP
    mov si, [tui_lay + TL_CHW]
    mov ax, [tui_lay + TL_CHX]
    add ax, si
    dec ax
    mov bx, [tui_lay + TL_ROWT]
    mov dx, [tui_vh]
    dec dx
    mov di, 3
.sep:
    call tui_vlinec
    add ax, si
    dec di
    jnz .sep
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tui_xt_line - the one line, plus the change markers the caller compares on
tui_xt_line:
    push ax
    push bx
    push cx
    push dx
    call tui_marks                  ; [tui_cvr] = the row to show
    mov byte [tui_band], 1
    mov al, [tui_cvr]
    cbw
    mov bx, ax
    call tui_xt_y
    mov dx, ax
    call tui_row1
    mov byte [tui_band], 0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tui_xt_on - is the single-line view active right now?
; out: ZF = 1 (jz) no, ZF = 0 (jnz) yes; AL clobbered
tui_xt_on:
    mov al, [trk_cpu0]
    and al, [mp_playing]
    ret

tui_draw_pat:
%ifdef TRKDBG
    inc word [tds_pat]              ; tests/trkscrl.inc
%endif
    push ax
    push bx
    push cx
    push dx
    call tui_xt_on                  ; 8088 + playing: one line, no grid
    jz .grid
    call tui_xt_clear
    call tui_xt_line
    jmp .out
.grid:
    call tui_marks
    mov byte [tui_band], 0
    mov cx, [tui_lay + TL_RAB]
    mov al, [tui_cvr]
    cbw
    sub ax, cx
    mov bx, ax                      ; first row = view - rowsAbove
    mov dx, [tui_lay + TL_ROWT]
.up:
    push cx
    call tui_row1
    inc bx
    add dx, 8
    pop cx
    loop .up
    mov byte [tui_band], 1          ; BX = the view row itself
    mov dx, [tui_lay + TL_BANDY]
    call tui_row1
    mov byte [tui_band], 0
    inc bx
    mov cx, [tui_lay + TL_RBE]
    mov dx, [tui_lay + TL_LOWY]
.lo:
    push cx
    call tui_row1
    inc bx
    add dx, 8
    pop cx
    loop .lo
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_scrl - the scroll path (SPEC.md 45.12): when the view moved by a few rows
; inside one pattern/position, the row area's content is the same pixels 8n
; rows away - scroll the upper and lower regions (the band between them breaks
; the 8-row rhythm, so they move separately) and relight 2n+1 strips: the n
; that entered each region at its far edge, and the band. out CF=0 done
; (markers updated); CF=1 not eligible, or gfx_scroll refused (a clip cutting
; the area - the dialog) - caller runs tui_draw_pat, whose per-strip gates are
; then the correct renderer.
;
; N IS NOT 1, AND THAT IS THE WHOLE OF SPEC.md 45.12.2. A frame that arrives
; late sees the view two rows on, and a path that only knew ONE row handed
; that frame to tui_draw_pat - 35 strips on the VGA layout against the three
; a scroll costs. The full repaint then takes longer than the frame it was
; drawn in, the next frame is later still, and the machine settles into
; repainting the whole grid at every row change: the failure is a RATCHET, not
; a slowdown, and it reads on the screen as a pattern view that has stopped
; scrolling and started re-lettering itself. Reproduced under QEMU at
; `-icount shift=9`, which is about the 12.5 MHz 286 the field reported it
; from, with that report's module (125 BPM, speed 6 - 8.3 rows a second
; against the tick's 18.2 frames): the counters in this file read 6.8 scrolls
; and 0.72 full repaints a second, where the music only turns the pattern
; over once every 7.7 s. It is 8.2 and 0.13 now - every row change scrolled,
; and every repaint a pattern. THE SECOND FIGURE IS AN INSTRUCTION COUNT
; STANDING IN FOR A CLOCK: QEMU charges nothing for the VGA's bus, so the real
; machine spends more of its frame drawing than this and the ratchet shuts
; harder there, not softer.
;
; The generalisation is exact rather than approximate: n rows of view is 8n
; pixels of content, one gfx_scroll each way still, and the strips that need
; relighting are the n at the region's trailing edge instead of the one. So
; the cost is 2n+1 strips and the ceiling is where that stops beating
; tui_draw_pat's RAB+RBE+1.
;
; TUI_SCRL_MAX is that ceiling and it is a MEASUREMENT, not a guess: see
; SPEC.md 45.12.2 for the sweep. Past it the jump is a seek rather than a late
; frame - a pattern loop, a HOME, an Up/Down held down - and the region has
; too little left in it to be worth preserving.
; -----------------------------------------------------------------------------
tui_scrl:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [tui_apos]
    cmp al, [tui_lpos]
    jne .no
    mov al, [tui_apat]
    cmp al, [tui_lpat]
    jne .no
    call tui_viewrow                ; [tui_cvr] = the row the band shows now
    mov al, [tui_cvr]
    sub al, [tui_lcvr]              ; both are 0..63, so the byte difference is
    cbw                             ; a signed row delta and cbw is exact
    or ax, ax
    jz .no                          ; nothing moved: the caller's own gate has
    mov si, ax                      ; already decided otherwise, so this is a
    jns .npos                       ; belt rather than a path
    neg ax
.npos:
    cmp ax, TUI_SCRL_MAX
    ja .no                          ; too far to be a late frame (see above)
    mov [tui_srn], ax               ; AX = n from here to the strip runs
    mov cl, 3
    shl si, cl                      ; SI = 8n, SIGNED: positive moves the
                                    ; content UP, which is the view advancing
    mov ax, [tui_lay + TL_RAB]      ; upper region: ROWT .. ROWT + RAB*8 - 1
    mov cl, 3
    shl ax, cl
    mov bx, [tui_lay + TL_ROWT]     ; THE TWO RAW SITES IN THIS FILE - every
    add bx, [tui_oy]                ; other drawing call goes through the
    mov dx, bx                      ; tui_*c helpers, which add the origin. On
    add dx, ax                      ; a bracket that origin is the DISPLAY's
    dec dx                          ; now and not (0,0) (SPEC.md 53.7.1), so a
    mov ax, [tui_ox]                ; scroll left here would move the primary's
    mov cx, ax                      ; pixels while the rest of the screen drew
    add cx, [tui_cw]                ; on the second monitor
    dec cx
    call OSAPI_GFX_SCROLL
    jc .no
    mov ax, [tui_lay + TL_RBE]      ; lower region: LOWY .. LOWY + RBE*8 - 1
    mov cl, 3
    shl ax, cl
    mov bx, [tui_lay + TL_LOWY]
    add bx, [tui_oy]
    mov dx, bx
    add dx, ax
    dec dx
    mov ax, [tui_ox]
    mov cx, ax
    add cx, [tui_cw]
    dec cx
    call OSAPI_GFX_SCROLL
    jc .no                          ; half-scrolled: the full repaint the
                                    ; caller falls back to repairs it whole
    mov al, [tui_cvr]               ; the 2n+1 strips (SPEC.md 45.12)
    cbw
    mov byte [tui_band], 0
    or si, si
    js .dstrips

    ; --- the view ADVANCED n rows: the last n strips of each region ----------
    mov bx, ax                      ; upper: rows view-n .. view-1, ending at
    sub bx, [tui_srn]               ; the strip immediately above the band
    mov [tui_sru], bx
    mov bx, [tui_lay + TL_RAB]
    sub bx, [tui_srn]
    mov cl, 3
    shl bx, cl
    add bx, [tui_lay + TL_ROWT]
    mov [tui_sruy], bx
    mov bx, ax                      ; lower: rows view+RBE-n+1 .. view+RBE,
    add bx, [tui_lay + TL_RBE]      ; ending at the region's bottom strip
    sub bx, [tui_srn]
    inc bx
    mov [tui_srl], bx
    mov bx, [tui_lay + TL_RBE]
    sub bx, [tui_srn]
    mov cl, 3
    shl bx, cl
    add bx, [tui_lay + TL_LOWY]
    mov [tui_srly], bx
    jmp short .strips

    ; --- ...or moved BACK n: the first n strips of each region ---------------
.dstrips:
    mov bx, ax
    sub bx, [tui_lay + TL_RAB]      ; upper: rows view-RAB .. view-RAB+n-1
    mov [tui_sru], bx
    mov bx, [tui_lay + TL_ROWT]
    mov [tui_sruy], bx
    mov bx, ax
    inc bx                          ; lower: rows view+1 .. view+n
    mov [tui_srl], bx
    mov bx, [tui_lay + TL_LOWY]
    mov [tui_srly], bx

.strips:
    push ax                         ; AX = the view row, wanted by the band
    mov cx, [tui_srn]
    mov bx, [tui_sru]
    mov dx, [tui_sruy]
.ustrip:
    push cx
    call tui_row1
    pop cx
    inc bx
    add dx, 8
    loop .ustrip
    mov cx, [tui_srn]
    mov bx, [tui_srl]
    mov dx, [tui_srly]
.lstrip:
    push cx
    call tui_row1
    pop cx
    inc bx
    add dx, 8
    loop .lstrip
    pop ax
    mov bx, ax                      ; the band itself, on the new view row
    mov byte [tui_band], 1
    mov dx, [tui_lay + TL_BANDY]
    call tui_row1
    mov byte [tui_band], 0
    call tui_marks
%ifdef TRKDBG
    inc word [tds_scrl]             ; tests/trkscrl.inc
%endif
    clc
    jmp .out
.no:
%ifdef TRKDBG
    inc word [tds_no]               ; tests/trkscrl.inc
%endif
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_row_lt - XT mode (SPEC.md 45.9): redraw only the band strip for the
; new row and settle the row marker; the rows around it stay until the
; position moves. tui_row1 carries the §11.3 clip gate, so a covering file
; dialog still blanks nothing.
; -----------------------------------------------------------------------------
tui_row_lt:
    push ax
    push bx
    push dx
    call tui_viewrow
    mov al, [tui_arow]
    mov [tui_lrow], al
    mov al, [tui_cvr]
    cbw
    mov bx, ax
    mov byte [tui_band], 1
    mov dx, [tui_lay + TL_BANDY]
    call tui_row1
    mov byte [tui_band], 0
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; Volume bars (every worker frame) and the scope hit-test
; =============================================================================
; tui_vu_step - one channel's needle: DECAY. in BX = channel 0..3; out AL =
;               the level 0..64, also stored. Every other register preserved.
;
; Factored out because the SPEC.md 45.13 text bars need the same needle: the
; rule about how a level moves belongs in one place, or the two surfaces
; disagree about what the music is doing.
;
; IT USED TO RISE INSTANTLY TO [tui_avol] AND THAT WAS THE BUG (SPEC.md
; 45.12.1). A MOD's volume column changes only when a volume command does, so
; on a channel holding one value the rise and the fall cancelled EXACTLY: the
; needle oscillated between the volume and two units under it, and both
; quantized to the same cell. Measured on the field module, channel 2:
; mp_chvol a constant 30, tui_vu alternating 30 and 28, 30*10/64 and 28*10/64
; both flooring to 4 - a bar stuck at four tenths for the whole song while
; notes played through it. The other three sat pinned at 10/10, which is the
; same defect wearing a plausible face.
;
; So the needle is driven by the NOTE now and not by the volume: tui_sync
; kicks it to the struck volume when a row that struck one becomes audible,
; and this decays it the rest of the time. What that gives up is a sustained
; note, which fades to nothing while it is still sounding - unavoidable,
; because the true amplitude is only knowable by measuring the mixer's
; output, and mp_mixch_xt is already half of a 4.77MHz machine (SPEC.md
; 53.5.1). Note energy is what this can honestly show, and it is what a
; tracker's bars have always shown.
TUI_VUFALL equ 2                    ; units per call, ~18 calls a second: full
                                    ; to empty in 1.8 s, and about one cell of
                                    ; ten per row at the XT default
tui_vu_step:
    mov al, [tui_vu + bx]
    cmp al, TUI_VUFALL
    jae .sub
    xor al, al
    jmp short .st
.sub:
    sub al, TUI_VUFALL
.st:
    mov [tui_vu + bx], al
    ret

tui_draw_vu:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [tui_chn], 0
.c:
    mov bl, [tui_chn]
    xor bh, bh
    call tui_vu_step
    xor ah, ah                      ; SI = bar width = vu * vuw / 64
    mul word [tui_vuw]
    mov cl, 6
    shr ax, cl
    mov si, ax
    mov al, [tui_chn]
    call tui_scellxy                ; [tui_t1]/[tui_t2] = cell origin
    mov dx, [tui_t2]                ; bar top y
    mov di, [tui_t1]
    add di, 4                       ; DI = bar left x (compact: +12, below)
    cmp word [tui_lay + TL_SCH], 16
    jb .cb
    add dx, [tui_lay + TL_SCH]
    sub dx, 11                      ; TUI_VUH rows, sitting where the middle
    jmp .yb                         ; of the old eight-row bar sat
.cb:
    add dx, 3                       ; compact cell: the bar shares rows
    add di, 8                       ; t2+1..t2+8 with the channel digit at
                                    ; t1+1..t1+8, so it starts at t1+12 -
                                    ; still left of the 'M' flag at t1+146
                                    ; (12 + 128-wide bar ends at t1+139)
.yb:
    mov bl, [tui_chn]               ; delta-draw (SPEC.md 45.12): paint only
    xor bh, bh                      ; the difference against the last drawn
    shl bx, 1                       ; width - a steady bar costs nothing
    mov ax, [tui_vupx + bx]
    cmp ax, si
    je .next
    mov [tui_vupx + bx], si
    jb .grow
    mov cx, di                      ; falling: erase [new .. old-1]
    add cx, ax
    dec cx
    mov ax, di
    add ax, si
    mov bx, dx
    add dx, TUI_VUH - 1
    TSETC TC_BG
    call tui_fillc
    jmp .next
.grow:
    mov cx, di                      ; rising: light [old .. new-1]
    add cx, si
    dec cx
    add ax, di
    mov bx, dx
    add dx, TUI_VUH - 1
    TSETC TC_VU
    call tui_fillc
.next:
    inc byte [tui_chn]
    cmp byte [tui_chn], 4
    jb .c
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_scopehit - which scope cell does a click land in? (fullscreen mute)
; in:  CX = x, DX = y (absolute screen); out CF=0 AL = channel 0..3, CF=1 miss
; preserves all other registers
; -----------------------------------------------------------------------------
tui_scopehit:
    push bx
    push cx
    push dx
    push si
    push di
    sub cx, [tui_ox]
    sub dx, [tui_oy]
    mov al, 0
.h:
    call tui_scellxy
    mov bx, [tui_t1]
    cmp cx, bx
    jb .n
    add bx, [tui_lay + TL_SCW]
    cmp cx, bx
    jae .n
    mov bx, [tui_t2]
    cmp dx, bx
    jb .n
    add bx, [tui_lay + TL_SCH]
    cmp dx, bx
    jae .n
    clc
    jmp .done
.n:
    inc al
    cmp al, 4
    jb .h
    stc
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; The top-area redraw (full draw, and the every-16th-frame refresh that also
; erases the menu-bar strip a cancelled file dialog leaves on a fullscreen
; surface - SPEC.md 38.6/45)
; =============================================================================
tui_draw_top:
    push ax
    push bx
    push cx
    push dx
    test byte [tui_lay + TL_FLAGS], TLF_CMPCT
    jnz .cga
    mov ax, [tui_lay + TL_SLX]      ; the sliver right of the top blocks:
    cmp ax, 0FFFFh                  ; nothing else repaints those rows
    je .els
    xor bx, bx
    mov cx, [tui_vw]
    dec cx
    mov dx, MBAR_H - 1
    call tui_deskfill
.els:
    call tui_el_posed
    call tui_el_logo
    call tui_el_bpm
    call tui_el_ptn
    call tui_el_status
    call tui_el_scopes
    test byte [tui_lay + TL_FLAGS], TLF_MENUS
    jz .nm
    call tui_el_menus
.nm:
    test byte [tui_lay + TL_FLAGS], TLF_INSTR
    jz .hdr
    call tui_el_instr
    jmp .hdr
.cga:
    xor ax, ax                      ; compact: erase rows 0..MBAR_H-1 whole
    xor bx, bx                      ; before the elements - the title line
    mov cx, [tui_vw]                ; and scope cells leave 2px gap columns
    dec cx                          ; nothing else repaints, and a cancelled
    mov dx, MBAR_H - 1              ; file dialog's menu-bar strip would
    TSETC TC_BG                     ; persist in them (a pure fill: per-pixel
    call tui_fillc                  ; clip, never gated - SPEC.md 45.6)
    call tui_top_cga
    call tui_el_scopes
.hdr:
    call tui_hdr
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The two exported redraws
; =============================================================================

; -----------------------------------------------------------------------------
; tui_draw_all - full redraw for the current mode; lock held by the caller
; (W_PAINT, a key/menu/completion handler - never the worker)
; -----------------------------------------------------------------------------
tui_draw_all:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [tui_inited], 0
    je .out
    call tui_sync                   ; the position being HEARD (SPEC.md 45.15)
    call tui_track
    cmp byte [trk_fs], 0
    je .win
    test byte [tui_lay + TL_FLAGS], TLF_CMPCT
    jnz .cbg
    xor ax, ax                      ; desktop band, then the black pattern
    xor bx, bx                      ; field below it
    mov cx, [tui_vw]
    dec cx
    mov dx, [tui_lay + TL_PATY]
    dec dx
    call tui_deskfill
    xor ax, ax
    mov bx, [tui_lay + TL_PATY]
    mov dx, [tui_vh]
    dec dx
    TSETC TC_BG
    call tui_fillc
    jmp .top
.cbg:
    xor ax, ax
    xor bx, bx
    mov cx, [tui_vw]
    dec cx
    mov dx, [tui_vh]
    dec dx
    TSETC TC_BG
    call tui_fillc
.top:
    call tui_draw_top
    call tui_draw_pat
    jmp .about
.win:
    TSETC TC_WBG                    ; the windowed splash card
    mov byte [tui_wdrawn], 0        ; ...which covers the readout, and the
                                    ; position may not move again for nine
                                    ; seconds, so the marker alone would leave
                                    ; the line blank until it did
    xor ax, ax
    xor bx, bx
    mov cx, [tui_cw]
    dec cx
    mov dx, [tui_ch]
    dec dx
    call tui_fillc
    mov si, tui_s_splash1
    mov dx, 16
    call tui_cline
    mov si, tui_s_splash2
    mov dx, 36
    call tui_cline
    mov si, mp_title
    cmp byte [mp_loaded], 0
    jne .title
    mov si, tui_s_none
.title:
    mov dx, 56
    call tui_cline
    mov si, tui_s_keys1
    mov dx, 76
    call tui_cline
    mov si, tui_s_keys2
    mov dx, 88
    call tui_cline
    mov si, tui_s_anykey            ; ...and NOT while 11 kHz is picked, where
    call trk_fs_ok                  ; F and the click are both refused
    jnc .hint                       ; (SPEC.md 45.9.3). View > Fullscreen is
    mov si, tui_s_nofs              ; greyed on the same predicate; a legend
.hint:                              ; is the one place with no greying to do
    mov dx, 108                     ; the explaining, so it says the other
    call tui_cline                  ; thing instead. The tree has been here
                                    ; before: trk_s_stopdf's own comment is
                                    ; about a legend that went on advertising
                                    ; a key it was written to stop advertising
    call tui_msg_draw
.about:
    cmp byte [trk_abon], 0
    je .out
    call tui_about_panel
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_win_lines - redraw ONLY the two windowed-splash lines a load changes
; in:  gfx lock held; preserves all registers
;
; The splash card is static except for the module title and the status line,
; so a load owes the screen those and nothing else. tui_cline erases its own
; band before lettering it, which is what makes this safe without the full
; content fill - and skipping that fill is the point: on a 4.77MHz machine
; the fill plus five untouched lines was a couple of hundred milliseconds of
; redrawing text that had not changed (PERFORMANCE.md).
;
; The About panel is checked exactly as tui_draw_all checks it: while it is
; up it owns the content, and these two lines must not be painted through it.
; -----------------------------------------------------------------------------
tui_win_lines:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [tui_inited], 0
    je .out
    cmp byte [trk_abon], 0
    jne .out
    call tui_track
    mov si, mp_title
    cmp byte [mp_loaded], 0
    jne .title
    mov si, tui_s_none
.title:
    mov dx, 56                      ; the same y tui_draw_all letters it at
    call tui_cline
    call tui_msg_draw
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_cline - a centred splash line: one gated erase+text pair on white
; in:  SI = NUL string, DX = y (content)
; -----------------------------------------------------------------------------
tui_cline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_FONT_WIDTH           ; AX = pixel width
    mov di, [tui_cw]
    sub di, ax
    shr di, 1                       ; DI = centred x
    mov ax, 4
    mov bx, dx
    mov cx, [tui_cw]
    sub cx, 5
    push dx
    add dx, 7
    call tui_clipc
    jc .skip
    TSETC TC_WBG
    call tui_fillc
    TSETC TC_WTX
    pop dx
    mov cx, di
    call tui_textc
    jmp .out
.skip:
    pop dx
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_draw_dyn - the worker's frame (lock held, clip armed by trk_render):
;                pattern + readouts when the view moved, VU bars always,
;                the top band every 16th call
; -----------------------------------------------------------------------------
tui_draw_dyn:
%ifdef TRKDBG
    inc word [tds_frame]            ; tests/trkscrl.inc
%endif
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [tui_inited], 0
    je .out
    call tui_sync                   ; the position being HEARD (SPEC.md 45.15)
    call tui_track
    mov al, [mp_playing]            ; did playback START or STOP? the test is
    cmp al, [tui_lplay]             ; above the fullscreen split because the
    je .noxn                        ; TRANSPORT changed either way, and the
    mov [tui_lplay], al             ; legend has to follow it in both. This
    call trk_transport              ; is also the only site that sees a stop
    cmp byte [trk_fs], 0            ; nobody asked for - song end, F00, the
    je .win                         ; watchdog, a failed load - so it is the
    cmp byte [trk_cpu0], 0          ; one that must park the view row.
    je .played                      ; On a tier-0 machine the transition also
    call tui_draw_pat               ; changes the SHAPE of the pattern view
    jmp .norows                     ; (SPEC.md 45.9.1), redrawn once here
.noxn:                              ; rather than at each of the five places
    cmp byte [trk_fs], 0            ; that can start or stop a module
    je .win
.played:
    mov al, [tui_arow]              ; did the view move?
    cmp al, [tui_lrow]
    jne .rows
    mov al, [tui_apos]
    cmp al, [tui_lpos]
    jne .rows
    mov al, [tui_apat]
    cmp al, [tui_lpat]
    jne .rows
    mov al, [tui_vrow]
    cmp al, [tui_lvrow]
    je .norows
.rows:
    call tui_xt_on                  ; 8088 + playing (SPEC.md 45.9.1): the
    jz .xtno                        ; grid is not animated at all - one
    call tui_xt_line                ; banded line moves in place, and the
    jmp .norows                     ; readouts below still follow
.xtno:
    cmp byte [mp_xt], 0             ; XT (SPEC.md 45.9): a row-only change
    je .rfull                       ; relights the band alone; the full
    mov al, [tui_apos]              ; repaint waits for a position/pattern
    cmp al, [tui_lpos]              ; (or stopped-scroll) move
    jne .rfull
    mov al, [tui_apat]
    cmp al, [tui_lpat]
    jne .rfull
    mov al, [tui_vrow]
    cmp al, [tui_lvrow]
    jne .rfull
    call tui_row_lt
    jmp .norows
.rfull:
    call tui_scrl                   ; one-row delta: scroll + three strips
    jnc .norows                     ; (SPEC.md 45.12; no readouts change on
                                    ; a row-only move)
    call tui_draw_pat
    test byte [tui_lay + TL_FLAGS], TLF_CMPCT
    jnz .rowc
    call tui_val_posed              ; VALUES only - the box faces, the bevels
    call tui_val_ptn                ; and every label are already on screen
    jmp .norows
.rowc:
    call tui_val_cga_pos
.norows:
    mov al, [tui_abpm]              ; did the tempo?
    cmp al, [tui_lbpm]
    jne .bs
    mov al, [tui_aspd]
    cmp al, [tui_lspd]
    je .nobs
.bs:
    test byte [tui_lay + TL_FLAGS], TLF_CMPCT
    jnz .bsc
    call tui_val_bpm
    jmp .nobs
.bsc:
    call tui_val_cga_bs
.nobs:
    mov ax, [mp_mute]               ; did a mute toggle?
    cmp ax, [tui_lmute]
    jne .mute
    mov ax, [mp_mute + 2]
    cmp ax, [tui_lmute + 2]
    je .nomute
.mute:
    call tui_el_scopes
.nomute:
    call tui_draw_vu
    jmp .out

    ; The top band used to be redrawn WHOLE every 16 frames from here - the
    ; logo, both readout blocks, the status line, four scope frames, the
    ; button column and the pattern header - as an eraser for the menu bar,
    ; which the kernel drew over our fullscreen content whenever the Standard
    ; File dialog was cancelled. That was a second-or-so heartbeat of static
    ; repainting on a machine with nothing to spare, to undo something that
    ; should never have been drawn: a visible fullscreen window covers the
    ; chrome (SPEC.md 11.2), and the kernel now declines to paint the bar or
    ; the dock underneath one instead of leaving the application to scrub it
    ; off afterwards. Closing the dialog damages our rect and we get a
    ; W_PAINT for it like anything else, which is one repaint on an event
    ; rather than a repaint per second forever.
.win:
    cmp byte [mp_playing], 0        ; windowed: a live position line on the
    je .out                         ; splash while something plays
    ; ONLY when it changes - and what changes is the POSITION alone, once every
    ; 64 rows. It used to compare the row too, so this drew a 17-cell run seven
    ; times a second on Beverly, a millisecond a cell (SPEC.md 6.1.1), for a
    ; number that windowed cannot be right anyway (SPEC.md 45.16.5/45.16.6).
    ; [tui_wdrawn] is the "is it on screen" half, because a repaint fills the
    ; card and the position may not move again for nine seconds - the same trap
    ; a zeroed bss marker sets, one step further along: here the marker is
    ; correct and the PIXELS are gone.
    cmp byte [tui_wdrawn], 0
    je .wupd
    mov al, [tui_apos]
    cmp al, [tui_lpos]
    je .out
.wupd:
    mov byte [tui_wdrawn], 1
    mov al, [tui_apos]
    mov [tui_lpos], al
    mov di, tui_pbuf
    mov bx, TUI_WPOSW * 8
    call tui_wpos
    mov byte [di], 0
    mov cx, 8
    call tui_wfld
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_wpos / tui_wfld - the windowed readout's two halves, factored out
;
; tui_wpos appends 'Pos xx  Row yy' at [DI] and leaves DI past it. tui_wfld
; draws tui_pbuf as one opaque run (SPEC.md 6.1) at x = CX, width BX, on the
; readout's row. They are separate because the bench build has TWO fields on
; that row and they redraw on different things - the text on a row change, the
; hold's animation on a frame - so neither may redraw for the other.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; tui_hintp - SI = the status legend this SURFACE should carry
; -----------------------------------------------------------------------------
tui_hintp:
    mov si, tui_s_hint
    cmp byte [trk_fs], 0
    je .out
    mov si, tui_s_hintf
.out:
    ret

tui_wpos:
    push ax
    push si
    mov si, tui_s_posl
    call tui_scpy
    mov al, [tui_apos]
    call tui_hexb
    mov byte [di], '/'              ; ...OF the song's length. A bare 'Pos 09'
    inc di                          ; steps once every 64 rows, which at
    mov al, [mp_songlen]            ; Beverly's tempo is 8.9 SECONDS, and with
    call tui_hexb                   ; no row counter beside it the denominator
    pop si                          ; is the only thing saying the field is
    pop ax                          ; alive rather than stuck
    ret

tui_wfld:
    push ax
    push dx
    push si
    mov si, tui_pbuf
    mov dx, [tui_ch]
    sub dx, TUI_WPOSY
    mov ah, TC_WBG
    mov al, TC_WTX
    call tui_rdout
    pop si
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tui_about_panel - the credits, panel-in-content (the ark_about pattern):
;                   drawn by the UI task while [trk_abon] holds the worker's
;                   frames off, so nothing paints over it
; -----------------------------------------------------------------------------
tui_about_panel:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [tui_cw]
    sub ax, 240
    shr ax, 1
    mov [tui_t1], ax                ; panel left
    mov ax, [tui_ch]
    sub ax, 84
    shr ax, 1
    mov [tui_t2], ax                ; panel top
    mov ax, [tui_t1]
    mov bx, [tui_t2]
    mov cx, ax
    add cx, 239
    mov dx, bx
    add dx, 83
    TSETC TC_WBG
    call tui_fillc
    TSETC TC_WTX
    call tui_framec
    mov di, tui_ablines
    mov dx, [tui_t2]
    add dx, 10
.line:
    mov si, [di]
    or si, si
    jz .out
    push dx
    push di
    call OSAPI_FONT_WIDTH           ; AX = width
    mov cx, 240
    sub cx, ax
    shr cx, 1
    add cx, [tui_t1]
    pop di
    pop dx
    call tui_textc
    add di, 2
    add dx, 12
    jmp .line
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Data: layout records, colour tables, strings
; =============================================================================

; VGA 640x480 - the FT2-proportioned layout (research: FT2 is 632x400 with
; the pattern from y=173; the extra 80 rows go to the pattern view)
tui_lay_vga:
    dw 172, 175, 185, 16, 313, 324, 18         ; PATY HDRY ROWT RAB BANDY LOWY RBE
    dw 8, 608, 32, 144, 24                     ; RNL RNR CHX CHW PAD
    dw 2, 94, 145, 39, 143, 38, 2              ; SCX SCY SCDX SCDY SCW SCH SCPR
    dw 4, 80, 282, 632                         ; STX STY STW SLX
    dw TLF_INSTR | TLF_MENUS                   ; FLAGS

; Hercules 720x348 - the mid layout: same top blocks, one scope row, no
; instrument list or button stacks, channel block centred in 720
tui_lay_hrc:
    dw 92, 95, 105, 14, 217, 228, 14
    dw 8, 656, 72, 144, 24
    dw 291, 2, 106, 0, 104, 44, 4
    dw 4, 80, 282, 291              ; SLX 291, not 714: the sliver fill must
                                    ; cover ALL of rows 0..MBAR_H-1 right of
                                    ; the logo (rows 0..1 and the scope-gap
                                    ; columns repaint nowhere else), or a
                                    ; cancelled file dialog's menu-bar strip
                                    ; persists there for the session
    dw 0

; CGA 640x200 - compact: title/readout line, inline bars, 10+10 rows
tui_lay_cga:
    dw 0, 19, 28, 10, 108, 119, 10
    dw 4, 0FFFFh, 24, 152, 32
    dw 0, 9, 160, 0, 158, 10, 4
    dw 352, 0, 284, 0FFFFh
    dw TLF_CMPCT

; colour tables (TC_N bytes each; see the role list at the top)
tui_col_vga:                        ; FT2 'Arctic' mapped to 16 colours
    db 0, 9, 7, 15, 7, 0, 15, 8, 7, 0, 10, 7, 1, 15, 7, 0, 15, 0
tui_col_mono:                       ; 1bpp: no colour 9 anywhere (SPEC.md 39.4)
    db 0, 15, 15, 0, 0, 15, 15, 15, 0, 15, 15, 15, 0, 15, 0, 15, 15, 0

tui_hexd:    db '0123456789ABCDEF'

; labels
tui_s_name:  db 'TRACKER', 0
tui_s_logo:  db 'T R A C K E R', 0
tui_s_bpm:   db 'BPM', 0
tui_s_spd:   db 'Spd', 0
tui_s_ptn:   db 'Ptn', 0
tui_s_ln:    db 'Ln', 0
tui_s_64:    db '64', 0
tui_s_np:    db 'Np', 0
tui_s_len:   db 'Len', 0
tui_s_posl2: db 'Pos', 0
tui_s_mute:  db 'MUTE', 0
tui_s_bpmc:  db 'BPM:', 0
tui_s_spdc:  db 'SPD:', 0
tui_s_posc:  db 'POS:', 0
tui_s_ptnc:  db 'PTN:', 0
tui_s_posl:  db 'Pos ', 0

; "is the windowed readout on the glass" - .text, not bss, because it wants a
; real initialiser and every launch reloads the image (SPEC.md 20.1), so a
; fresh instance starts owing the line rather than believing it drew one.
tui_wdrawn:  db 0

; the FT2 button stacks (decorative in v1; each names its key)
tui_mbl:     dw tui_b_about, tui_b_mute, tui_b_scroll, tui_b_pos, tui_b_exit, 0
tui_mbr:     dw tui_b_play, tui_b_ppat, tui_b_stop, tui_b_load, tui_b_full, 0
tui_b_about: db 'About', 0
tui_b_mute:  db 'Mute1-4', 0
tui_b_scroll:db 'Rows'
             db 0
tui_b_pos:   db 'Pos <->', 0
tui_b_exit:  db 'Esc=win', 0
tui_b_play:  db 'Play', 0
tui_b_ppat:  db 'Pattern', 0
tui_b_stop:  db 'Stop', 0
tui_b_load:  db 'Load L', 0
tui_b_full:  db 'Full F', 0

; the windowed splash
tui_s_splash1: db 'T R A C K E R', 0
tui_s_splash2: db 'A FastTracker II style MOD player', 0
tui_s_none:    db 'No module loaded', 0
tui_s_keys1:   db 'ENTER play  SPACE stop  HOME top  P pattern', 0
tui_s_keys2:   db 'ARROWS position  1-4 mute  L load  X xt  V screen', 0
                                    ; 'a key nothing names is a key nobody
                                    ; finds' (just below) applies to V as much
                                    ; as it did to the bench keys, and the
                                    ; SURFACE is the one setting whose effect
                                    ; is invisible until F is pressed
                                    ; (SPEC.md 45.13.7). 49 cells against
                                    ; tui_cline's centred [tui_cw] = 418 = 52,
                                    ; and tui_cw does not move with the
                                    ; adapter - only tui_ch does - so this
                                    ; fits on all three of SPEC.md 39's
%ifdef TRKLOG                       ; the bench keys are on screen, because a
                                    ; key nothing names is a key nobody finds:
                                    ; this line said 'P pattern' one row above
                                    ; it and P was reached for as the pace
                                    ; cycler for a whole round of testing
tui_s_anykey:  db 'F full  K rate  D log  W write  M mark', 0
%else
tui_s_anykey:  db 'Press F or click for fullscreen', 0
%endif
tui_s_nofs:    db 'Full screen needs Rate 5.5 kHz', 0
; Two status legends, because L IS REFUSED on the exclusive surface
; (SPEC.md 53.7 - the file dialog's answer comes back through an event ladder a
; bracket is not running) and offering a key that answers "not here" is worse
; than not offering it. The refusal stays: a key the user presses anyway still
; says WHY, which is SPEC.md 47 rule 3's shape - the greyed thing explains
; itself and the unlisted thing has to.
tui_s_hint:    db 'ENTER play  SPACE stop  HOME top  L load', 0
tui_s_hintf:   db 'ENTER play  SPACE stop  F/ESC', 0


; the About panel
tui_ablines:
    dw tui_ab1, tui_ab2, tui_ab3, tui_ab4, tui_ab5, 0
tui_ab1:     db 'Tracker for os8088', 0
tui_ab2:     db 'A FastTracker II homage', 0
tui_ab3:     db 'MOD replay in the style of', 0
tui_ab4:     db '8bitbubsy/ft2-clone', 0
tui_ab5:     db 'Any key closes', 0

; =============================================================================
; trkui bss (through tracker.asm's TRKB/TRKW/TRKBUF accumulator)
; =============================================================================
    TRKB tui_inited                 ; layout + colours are live
    TRKB tui_mono                   ; 1bpp adapter (SPEC.md 39.4)
    TRKW tui_vw                     ; the surface the LAYOUT was picked for -
    TRKW tui_vh                     ; the screen windowed, the bracket's own
                                    ; display inside one (SPEC.md 53.7.1)
    TRKW tui_layh                   ; ...its height, so a re-pick is a compare
    TRKW tui_scrh                   ; ...and the SCREEN's, so the way back out
                                    ; of a bracket is one too
    TRKW tui_ox                     ; content origin + size, re-read per draw
    TRKW tui_oy
    TRKW tui_cw
    TRKW tui_ch
    TRKBUF tui_lay, TL_SIZE         ; the chosen layout record
    TRKBUF tui_col, TC_N            ; the chosen colour table
    TRKW tui_chend                  ; right edge of the channel block
    TRKW tui_rend                   ; right edge of a row strip
    TRKW tui_vuw                    ; volume bar full width
    TRKW tui_srn                    ; the scroll's row delta n (SPEC.md 45.12.2)
    TRKW tui_sru                    ; first row to relight in the upper region
    TRKW tui_sruy                   ; ...and that strip's y
    TRKW tui_srl                    ; the same pair for the lower region
    TRKW tui_srly
    TRKB tui_vrow                   ; the Up/Down scroll row while stopped
    TRKB tui_cvr                    ; the view row this frame

; --- what the LISTENER is hearing (SPEC.md 45.15): tui_sync publishes these
; --- once a frame and every drawer quotes them instead of mp_* -----------------
    TRKB tui_apos                   ; order position
    TRKB tui_apat                   ; pattern
    TRKB tui_arow                   ; row
    TRKB tui_abpm                   ; tempo...
    TRKB tui_aspd                   ; ...and ticks per row, as they were heard
    TRKBUF tui_avol, 4              ; the channel volumes the needles show

; --- tui_playpos's interpolation between block IRQs (SPEC.md 45.15.1) ---------
    TRKW tui_bpt                    ; stream bytes per system tick
    TRKW tui_lcons                  ; the reported position it is anchored to
    TRKW tui_ct0                    ; ...and the tick that anchor landed on
    TRKW tui_pcon                   ; the report the phase loop last measured on
    TRKW tui_play                   ; the last answer given: never goes back
    TRKW tui_sub                    ; the SUB-TICK offset (SPEC.md 45.15.2):
                                    ; how far the frames say we are into the
                                    ; current tick. An offset on top of the
                                    ; accumulator, never added into it
    TRKW tui_bpf                    ; ...[tui_bpt] / the frames a tick holds
    TRKB tui_fpt                    ; frames the LAST tick held: this one's
    TRKB tui_fcnt                   ; divisor, and the count so far in this
    TRKB tui_band                   ; tui_row1: drawing the band row
    TRKB tui_chn                    ; shared small loop counter
    TRKB tui_lplay                  ; last seen [mp_playing], for the 8088
                                    ; view's start/stop transition (45.9.1)
    TRKW tui_rrole                  ; tui_row1's ink/background role pair
    TRKB tui_rbg                    ; tui_runc's resolved background colour
    TRKBUF tui_cbuf, 2              ; tui_charc's one character and its NUL
    TRKB tui_vonly                  ; 1 = draw an element's VALUES only, the
                                    ; face/bevel/labels being on screen already
    TRKB tui_lrow                   ; last-drawn markers for tui_draw_dyn
    TRKB tui_lpos
    TRKB tui_lpat
    TRKB tui_lvrow
    TRKB tui_lbpm
    TRKB tui_lspd
    TRKBUF tui_lmute, 4
    TRKBUF tui_vu, 4                ; decaying VU levels
    TRKW tui_vlpos                  ; the stamp position the needles were last
                                    ; kicked on (SPEC.md 45.12.1) - the row
                                    ; gate, not a display value
    TRKBUF tui_vupx, 8              ; last-drawn bar widths, px (SPEC.md
                                    ; 45.12 - the delta-draw baseline)
    TRKB tui_lcvr                   ; last-drawn view row (the scroll path's
                                    ; one-row-delta detector, SPEC.md 45.12)
    TRKW tui_msgp                   ; status-line string, 0 = the key hint
    TRKB tui_tcol                   ; tui_rdout scratch
    TRKB tui_ecol
    TRKW tui_rdw                    ; ...and the field it is drawing, banked
    TRKW tui_rdx                    ; because the clip gate spends CX and DX
    TRKW tui_rdy
    TRKBUF tui_rbuf, TUI_RDCELLS + 1    ; the space-padded run (SPEC.md 6.1):
                                        ; the padding is what erases the old
                                        ; value, so there is no blank moment
    TRKW tui_t1                     ; shared scratch words (serial: every
    TRKW tui_t2                     ; drawer runs under the one gfx lock)
    TRKBUF tui_nbuf, 8              ; number/text compose buffers
    TRKBUF tui_pbuf, 48             ; 'Pos xx  Row yy' plus, in the TRKLOG
                                    ; build only, SPEC.md 45.16.4's sweep
    TRKBUF tui_insbuf, 20
