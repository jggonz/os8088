; =============================================================================
; os8088 - apps/modplug/mppui.inc
;
; ModPlug Player's face (SPEC.md 56.3/56.4/56.6). Prefix mppu_. Everything
; ModPlug Player V2 draws in its 417x226 main window, drawn here in 16
; colours that survive reduction to one bit:
;
;   the bevelled body      the green LCD panel     the LED transport row
;   the time scrubber      the volume slider       the option-button grid
;   the spectrum analyser / oscilloscope / VU meter, and the status line
;
; TWO LAYOUTS, ONE CODE PATH (SPEC.md 56.4). ModPlug Player's own window is
; 417x226 and CGA's whole desktop band is 156 rows, so the full layout cannot
; exist there. Every rectangle on the face is therefore a WORD IN BSS, filled
; by mppu_layout from one of two tables, and no drawing routine below that
; point contains a screen-derived constant. The compact layout keeps the LCD,
; the transport, the scrubber, the visualiser and eight of the twelve option
; buttons; what it gives up is rows, not features.
;
; THE 1BPP PALETTE, and it is not decoration. On Hercules and CGA (SPEC.md
; 39.4) colours 0..6 go black, 7..11 and 13 dither, and 12/14/15 go white -
; and a GLYPH rounds to black rather than dithering, because half a stroke is
; not a letter. Two consequences run through this file:
;
;   - Light grey and dark grey are the SAME COLOUR at 1bpp, so a bevel drawn
;     in CLGRAY over CDGRAY would be invisible. Every bevel here is CWHITE
;     against CBLACK, and the FACE it sits on is [mppu_body] - CLGRAY at
;     4bpp, CWHITE at 1bpp. That second half was learned the hard way: a grey
;     chassis reduces to a 50% checkerboard, and every black button glyph,
;     option label and status line on it was competing with a screen. White
;     body, black outlines is the 1bpp Macintosh look this OS is modelled on,
;     and the raised bevel survives it as a black L on two sides - which is
;     how a Mac drew a raised edge anyway.
;   - The LCD is green on black, and CLGREEN is in the DITHER class, so its
;     text would come out BLACK ON BLACK on the two mono adapters. That is
;     Missile Command's trap exactly (SPEC.md 48). [mppu_ink] is latched from
;     OSAPI_VIDEO's DH at layout time - CLGREEN at 4bpp, CWHITE at 1bpp -
;     one byte spent to keep the panel readable on every machine this OS
;     runs on. An unreadable panel is not a more faithful port.
;
; TWO CONVENTIONS worth knowing before reading any routine here:
;
;   - MPPU_PEN is a MACRO, not a call, and it brackets the colour change with
;     push ax / pop ax. The API preserves every register, so a bare
;     `mov al, CLGRAY` before a fill would quietly destroy the low byte of
;     the rect's own x1 - which is the shape of bug that draws a control at
;     the wrong place only when its x crosses a multiple of 256.
;   - The coordinate helpers take CONTENT-RELATIVE coordinates and add the
;     origin themselves, so no layout number ever has a window position
;     folded into it, and mppu_hit can read the very same words.
;
; Every proc here runs with the gfx lock HELD (from a window callback, or
; from the worker's one lock hold) and preserves all registers.
; =============================================================================

%macro MPPU_PEN 1
    push ax
    mov al, %1
    call OSAPI_SET_COLOR
    pop ax
%endmacro

; --- geometry both layouts share -----------------------------------------------
MPPU_BW      equ 30                 ; a transport button...
MPPU_BPITCH  equ 32                 ; ...and the next one's left edge
MPPU_BX0     equ 6                  ; the row starts here; nine of them
MPPU_NBTN    equ 9
MPPU_LEDW    equ 10                 ; the LED strip under each glyph
MPPU_LEDH    equ 3
MPPU_THUMBW  equ 9                  ; the scrubber's and the volume's handle
MPPU_MAXBAND equ 16                 ; spectrum bands - the full layout's count
MPPU_SCOPEN  equ 208                ; oscilloscope columns banked per chunk
MPPU_VUCH    equ 4                  ; VU rows: one per replayer channel
MPPU_LROW    equ 11                 ; LCD text rows are this far apart
MPPU_LMAX    equ 4                  ; ...and the most any layout has

; --- the invalidation mask (SPEC.md 56.12) -------------------------------------
; One bit per independently drawable element of the face. mppu_inval sets them,
; mppu_poll sets the ones whose state is a cheap pure function, and mppu_paint
; spends them. MPPI_BODY is the whole face: the body fill wipes everything on
; top of it, so it can only mean "draw the lot".
MPPI_BODY    equ 0001h              ; body, bevel, LCD well, button faces and
                                    ; glyphs, option faces and labels => ALL
MPPI_LCD     equ 0002h              ; the LCD's text lines
MPPI_LEDS    equ 0004h              ; the transport LED strips
MPPI_SCRUB   equ 0008h              ; the scrubber groove and its handle
MPPI_VOL     equ 0010h              ; the volume groove and its handle
MPPI_OPTLED  equ 0020h              ; the option grid's indicators
MPPI_STATUS  equ 0040h              ; the status line
MPPI_VIZ     equ 0080h              ; the visualiser pane
MPPI_ALL     equ 00FFh

; --- the two layout tables (SPEC.md 56.4) --------------------------------------
; MPPU_LAYW words each, copied wholesale into the bss block at the end of this
; file - which is laid out in exactly this order and asserted to match.
mppu_lay_full:
    dw 5, 4, 410, 55                ; LCD panel: x1 y1 x2 y2
    dw 4                            ; ...text lines it has room for
    dw 60, 26                       ; transport row: top y, height
    dw 302, 66, 408, 79             ; volume slider box
    dw 5, 90, 410, 103              ; scrubber box
    dw 5, 108, 248, 181             ; visualiser pane
    dw 252, 108                     ; option grid origin
    dw 78, 11                       ; ...button width, height
    dw 79, 12                       ; ...and the two pitches
    dw 2, 6, MPPO_COUNT             ; ...columns, rows, buttons shown
    dw 185                          ; the status line's text row
    dw 16                           ; spectrum bands
MPPU_LAYW    equ 30

mppu_lay_compact:
    dw 5, 3, 410, 38
    dw 3
    dw 42, 24
    dw 302, 47, 408, 60
    dw 5, 69, 410, 80
    dw 5, 84, 225, 119
    dw 230, 84
    dw 78, 11
    dw 79, 12
    dw 2, 3, MPPO_COMPACT
    dw 122
    dw 10
mppu_lay_end:

    %if (mppu_lay_compact - mppu_lay_full) != MPPU_LAYW * 2
        %error "mppu_lay_full must be MPPU_LAYW words"
    %endif
    %if (mppu_lay_end - mppu_lay_compact) != MPPU_LAYW * 2
        %error "mppu_lay_compact must be MPPU_LAYW words"
    %endif

; =============================================================================
; mppu_layout - latch the content origin, the adapter's depth and the layout
; in:  gfx lock held; preserves all registers
;
; Run at the top of every W_PAINT rather than once, because the content ORIGIN
; moves with the window and a paint is exactly when it may have moved. The
; layout CHOICE cannot change (this frame is not resizable), but copying 24
; words costs less than the branch that would avoid copying them.
; =============================================================================
mppu_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [mpp_win]
    call OSAPI_WM_CONTENT           ; out AX = content left, DX = content top
    cmp byte [mppu_ok], 0           ; a face that has never been laid out has
    je .moved                       ; no pixels to keep, and an origin of 0,0
    cmp ax, [mppu_cx0]              ; is a legal window position
    jne .moved
    cmp dx, [mppu_cy0]
    je .keep
.moved:
    push ax                         ; the content moved under us, so every
    mov ax, MPPI_BODY               ; element's recorded state describes pixels
    call mppu_inval                 ; at the old origin. Draw the lot.
    pop ax
.keep:
    mov [mppu_cx0], ax
    mov [mppu_cy0], dx
    mov bx, [mpp_win]
    call OSAPI_WM_GEOM              ; out CX = content w, DX = content h
    mov [mppu_cw], cx
    mov [mppu_ch], dx
    call OSAPI_VIDEO                ; DH = bits per pixel, 4 or 1
    mov al, CLGREEN                 ; ModPlug Player's LCD green - except
    cmp dh, 1                       ; where green is a dither class and a
    jne .ink                        ; glyph rounds to black, which would put
    mov al, CWHITE                  ; black text on a black panel
.ink:
    mov [mppu_ink], al
    mov al, CLGRAY                  ; ...and the BODY is the same question
    cmp dh, 1                       ; asked of the other half of the face.
    jne .body                       ; CLGRAY is in the dither class, so at
    mov al, CWHITE                  ; 1bpp the player's whole chassis becomes
.body:                              ; a checkerboard and every black glyph and
    mov [mppu_body], al             ; every black button glyph on it is
                                    ; competing with a 50% screen. White body,
                                    ; black outlines: the 1bpp Macintosh look
                                    ; this OS is modelled on, and the one
                                    ; pairing where a stroke keeps its
                                    ; contrast. The raised bevel's highlight
                                    ; is white either way - invisible on a
                                    ; white body, which leaves the black
                                    ; shadow on two sides, and a black L IS
                                    ; how a Mac drew a raised edge.
    mov si, mppu_lay_full
    cmp byte [mpp_compact], 0
    je .cp
    mov si, mppu_lay_compact
.cp:
    mov di, mppu_lx1                ; the bss block IS the table's shape
    mov cx, MPPU_LAYW
    push ds
    pop es
    cld
    rep movsw
    mov byte [mppu_ok], 1
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Coordinate helpers - content-relative in, absolute at the API
;
; They add [mppu_dx0]/[mppu_dy0], the CURRENT drawing origin, rather than the
; player window's own - which is what lets the Setup window and the PlayList
; editor draw themselves with these same routines instead of carrying two more
; copies of a bevel. Every top-level paint sets the origin first: the player's
; through mppu_org_player, the other two from their own content origin. The
; worker only ever draws the player, and mppu_draw_all / mppu_paint set the
; origin at their top, so a worker frame landing between a Setup paint and the
; next player paint cannot inherit the dialog's origin.
; =============================================================================

; mppu_org - AX = origin x, DX = origin y
mppu_org:
    mov [mppu_dx0], ax
    mov [mppu_dy0], dx
    ret

; mppu_org_player - point the helpers back at the player's content
mppu_org_player:
    push ax
    mov ax, [mppu_cx0]
    mov [mppu_dx0], ax
    mov ax, [mppu_cy0]
    mov [mppu_dy0], ax
    pop ax
    ret

; mppu_fillr / mppu_framer - AX=x1 BX=y1 CX=x2 DX=y2, content-relative
mppu_fillr:
    call mppu_toabs
    call OSAPI_GFX_FILL
    jmp mppu_torel
mppu_framer:
    call mppu_toabs
    call OSAPI_GFX_FRAME
    jmp mppu_torel

mppu_toabs:
    add ax, [mppu_dx0]
    add cx, [mppu_dx0]
    add bx, [mppu_dy0]
    add dx, [mppu_dy0]
    ret
mppu_torel:
    sub ax, [mppu_dx0]
    sub cx, [mppu_dx0]
    sub bx, [mppu_dy0]
    sub dx, [mppu_dy0]
    ret

; mppu_hlr - AX=x1 BX=x2 DX=y, content-relative
mppu_hlr:
    add ax, [mppu_dx0]
    add bx, [mppu_dx0]
    add dx, [mppu_dy0]
    call OSAPI_GFX_HLINE
    sub ax, [mppu_dx0]
    sub bx, [mppu_dx0]
    sub dx, [mppu_dy0]
    ret

; mppu_vlr - AX=x BX=y1 DX=y2, content-relative
mppu_vlr:
    add ax, [mppu_dx0]
    add bx, [mppu_dy0]
    add dx, [mppu_dy0]
    call OSAPI_GFX_VLINE
    sub ax, [mppu_dx0]
    sub bx, [mppu_dy0]
    sub dx, [mppu_dy0]
    ret

; -----------------------------------------------------------------------------
; mppu_run - CX=x DX=y content-relative, SI = NUL string, AL = ink, AH = ground
;
; OSAPI_FONT_RUN with the drawing origin added, and the reason every window in
; this package letters through it rather than through mppu_strr: a run cannot
; produce SPEC.md 11.3's granularity failure, so any of the three windows may
; be redrawn under an armed clip region with no per-cell gate. The transparent
; mppu_strr below stays for text drawn onto a surface that was just filled in
; the same unclipped pass.
; -----------------------------------------------------------------------------
mppu_run:
    add cx, [mppu_dx0]
    add dx, [mppu_dy0]
    call OSAPI_FONT_RUN
    sub cx, [mppu_dx0]
    sub dx, [mppu_dy0]
    ret

; -----------------------------------------------------------------------------
; mppu_dpen / mppu_dink / mppu_dpen_off - the DISABLED pen, SPEC.md 47 rule 1
;
; in (dpen, dink):  CF = 0 live / CF = 1 disabled
; out (mppu_dpen):  [gfx_color] and [gfx_dis] set together. EVERY register
;                   preserved, CF included - for a caller drawing a SHAPE,
;                   which reads the pen and takes no colour argument.
; out (mppu_dink):  the above, plus AL = the ink to letter in - for a caller
;                   drawing an opaque run, which takes its own ink and ground.
; out (dpen_off):   the pen back to live. Every register preserved.
;
; This package was written against a kernel with no OSAPI_GFX_PEN, so it greyed
; by putting CDGRAY in AL and nothing else - and that is rule 1's own failure
; rather than a shortcut. A disabled control is TWO pieces of state, the colour
; and the [gfx_dis] flag, and on Hercules and CGA the flag is the entire
; difference: font_ink masks a flagged glyph to a checkerboard, while CDGRAY on
; its own rounds to solid black. So every greyed caption in the three windows
; was pixel-identical to a live one at 1bpp - the Setup pane's dead rows, the
; PlayList's Save List and the option grid's unavailable labels all stopped
; saying so on two adapters out of three, while their FRAMES dithered around
; them (gfx_ink screens a shape with no help), which is rule 2's failure on
; top: the two halves of one control disagreeing.
;
; TWO ENTRY POINTS BECAUSE AX IS SOMETIMES LIVE, and that is not a style
; choice - it is the bug the first version of this had. mpps_draw_box holds the
; control's x in AX across the branch that greys it, so a pen routine that
; returns an ink in AL corrupts the rect it is about to draw. The shape callers
; take mppu_dpen and keep their registers; only the three run callers, which
; are all building AX rather than holding it, take mppu_dink.
;
; The pen is one lock hold long - gfx_unlock clears it like it clears the clip
; region - but that is not licence to leave it set: the next thing this package
; draws in the same hold would come out disabled. mppu_dpen_off is the `clc`
; half, and every caller below spends it before it returns rather than at the
; branch, so no path can leak the flag.
; -----------------------------------------------------------------------------
mppu_dpen:
    push ax
    call OSAPI_GFX_PEN              ; CF IS the argument, and the cell touches
    pop ax                          ; no flag - nor do the push/pop - so it
    ret                             ; crosses to mppu_dink unchanged

mppu_dink:
    call mppu_dpen
    mov al, CBLACK
    jnc .out
    mov al, CDGRAY
.out:
    ret

mppu_dpen_off:
    push ax
    clc
    call OSAPI_GFX_PEN
    pop ax
    ret

; -----------------------------------------------------------------------------
; mppu_clip_arm / mppu_clip_done - a secondary window repainting itself from a
; click rather than from its own W_PAINT
; in:  BX = the window; gfx lock held. out (arm): CF=1 = draw nothing at all.
;
; Same argument as mppu_refresh: the kernel orders a W_PAINT, a click handler
; does not, and by the time the Setup window has opened a Standard File dialog
; there is something on top of it. Cleared explicitly because the lock is
; already held and gfx_unlock - which is what normally clears a region - is
; somebody else's call.
; -----------------------------------------------------------------------------
mppu_clip_arm:
    call OSAPI_WM_GEOM              ; CF=1: hidden
    jc .no
    call OSAPI_WM_CLIP_SET          ; CF=1: wholly covered
    jc .no
    clc
    ret
.no:
    stc
    ret

mppu_clip_done:
    call OSAPI_WM_CLIP_CLEAR
    ret

; mppu_strr - CX=x DX=y content-relative, SI = NUL string
mppu_strr:
    add cx, [mppu_dx0]
    add dx, [mppu_dy0]
    call OSAPI_FONT_STR
    sub cx, [mppu_dx0]
    sub dx, [mppu_dy0]
    ret

; -----------------------------------------------------------------------------
; mppu_bevel - the whole ModPlug Player body is made of these
; in:  AX=x1 BX=y1 CX=x2 DX=y2 content-relative, DI = 0 raised / 1 sunken
; preserves all registers except flags
;
; The rect is banked in bss rather than juggled: the four edges want three
; different register conventions between them, and mppu_e1..e4 is one place
; instead of four stack dances. Nothing nests a bevel inside a bevel.
; -----------------------------------------------------------------------------
mppu_bevel:
    push ax
    push bx
    push cx
    push dx
    mov [mppu_e1], ax
    mov [mppu_e2], bx
    mov [mppu_e3], cx
    mov [mppu_e4], dx
    or di, di
    jz .raised
    MPPU_PEN CBLACK                 ; sunken: dark above and left
    jmp .hi
.raised:
    MPPU_PEN CWHITE
.hi:
    mov ax, [mppu_e1]               ; the lit pair: top edge...
    mov bx, [mppu_e3]
    mov dx, [mppu_e2]
    call mppu_hlr
    mov ax, [mppu_e1]               ; ...and left edge
    mov bx, [mppu_e2]
    mov dx, [mppu_e4]
    call mppu_vlr
    or di, di
    jz .rlo
    MPPU_PEN CWHITE
    jmp .lo
.rlo:
    MPPU_PEN CBLACK
.lo:
    mov ax, [mppu_e1]               ; the shadowed pair: bottom edge...
    mov bx, [mppu_e3]
    mov dx, [mppu_e4]
    call mppu_hlr
    mov ax, [mppu_e3]               ; ...and right edge
    mov bx, [mppu_e2]
    mov dx, [mppu_e4]
    call mppu_vlr
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mppu_well - a sunken panel: bevel it in, then black out the inside. The LCD,
;             the visualiser pane and both slider grooves are all this.
; in:  AX=x1 BX=y1 CX=x2 DX=y2 content-relative; preserves all registers
; -----------------------------------------------------------------------------
mppu_well:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, 1
    call mppu_bevel
    MPPU_PEN CBLACK
    inc ax
    inc bx
    dec cx
    dec dx
    call mppu_fillr
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mppu_face - a raised control face: fill it grey, then bevel it out
; in:  AX=x1 BX=y1 CX=x2 DX=y2 content-relative; preserves all registers
; -----------------------------------------------------------------------------
mppu_face:
    push ax
    push bx
    push cx
    push dx
    push di
    MPPU_PEN [mppu_body]
    call mppu_fillr
    xor di, di
    call mppu_bevel
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mppu_tri - a filled triangle, which is most of the transport glyphs
; in:  AX = the flat side's x, BX = centre y, CL = half-height,
;      CH = 0 points right / 1 points left. Content-relative.
; preserves all registers
; -----------------------------------------------------------------------------
mppu_tri:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [mppu_tx], ax
    mov [mppu_ty], bx
    mov [mppu_tdir], ch
    mov bl, cl
    xor bh, bh
    mov si, bx                      ; SI = the half-height
    xor di, di                      ; DI = the column, 0 .. half-height
.col:
    mov ax, [mppu_tx]
    cmp byte [mppu_tdir], 0
    je .r
    sub ax, di                      ; pointing left: the columns walk backwards
    jmp .x
.r:
    add ax, di
.x:
    mov cx, si                      ; this column's half-extent, shrinking to
    sub cx, di                      ; nothing at the apex
    mov bx, [mppu_ty]
    sub bx, cx
    mov dx, [mppu_ty]
    add dx, cx
    call mppu_vlr
    inc di
    cmp di, si
    jbe .col
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mppu_tri_up - the eject glyph's triangle: apex up, flat side down
; in:  AX = the flat side's left x, BX = its y, CL = the half-width
; preserves all registers
; -----------------------------------------------------------------------------
mppu_tri_up:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [mppu_tx], ax
    mov [mppu_ty], bx
    mov bl, cl
    xor bh, bh
    mov si, bx                      ; SI = the half-width
    xor di, di                      ; DI = the row, 0 .. half-width
.row:
    mov ax, [mppu_tx]
    add ax, di                      ; each row up loses one column at each end
    mov cx, [mppu_tx]
    add cx, si
    add cx, si
    sub cx, di
    mov bx, [mppu_ty]
    sub bx, di
    mov dx, bx
    call mppu_fillr
    inc di
    cmp di, si
    jbe .row
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Text assembly - every LCD line is built into one buffer and drawn ONCE.
; A panel drawn in fragments flickers on the machines this app is aimed at,
; and a readout assembled in place shifts under a name a character longer
; than the last one.
; =============================================================================

; mppu_putc - AL = a character, DI = the cursor; DI advances
mppu_putc:
    mov [di], al
    inc di
    ret

; mppu_puts - SI = a NUL string, DI = the cursor; the NUL is not copied
mppu_puts:
    push ax
.c:
    mov al, [si]
    or al, al
    jz .z
    mov [di], al
    inc si
    inc di
    jmp .c
.z:
    pop ax
    ret

; mppu_putn - SI = a NUL string, CL = EXACTLY this many characters, space
;             padded and truncated. What keeps a readout from shifting.
;
; The padding is an EXPLICIT loop and not a rep stosb, which is not a style
; choice: stos writes through ES, and ES is KERNEL_SEG on entry to every
; callback (SPEC.md 20.1) - so the obvious version pads the kernel's segment
; instead of this buffer. It leaves the line half-built, which is how it was
; noticed, and scribbles on the kernel, which is why it matters.
mppu_putn:
    push ax
    push cx
    xor ch, ch
    jcxz .out
.c:
    mov al, [si]
    or al, al
    jz .pad
    mov [di], al
    inc si
    inc di
    loop .c
    jmp .out
.pad:
    mov al, ' '
.p:
    mov [di], al
    inc di
    loop .p
.out:
    pop cx
    pop ax
    ret

; mppu_putnes - the same, from ES:SI, and filtering. The module's instrument
; names are RAW BYTES a stranger wrote (SPEC.md 18's rule): 22 of them, not
; NUL-guaranteed, and a MOD is a traditional place to hide artwork made of
; control codes. Anything outside printable ASCII becomes a space here rather
; than being handed to the glyph renderer.
mppu_putnes:
    push ax
    push cx
    xor ch, ch
    jcxz .out
.c:
    mov al, [es:si]
    cmp al, ' '
    jb .sp
    cmp al, 126
    jbe .st
.sp:
    mov al, ' '
.st:
    mov [di], al
    inc si
    inc di
    loop .c
.out:
    pop cx
    pop ax
    ret

; mppu_put2 - AL = 0..99 as two zero-padded digits at DI
mppu_put2:
    push ax
    push dx
    xor ah, ah
    mov dl, 100
    div dl                          ; a value over 99 parks at 99 rather than
    mov al, ah                      ; printing its remainder as if it were the
    xor ah, ah                      ; number
    mov dl, 10
    div dl                          ; AL = tens, AH = units
    add al, '0'
    mov [di], al
    inc di
    mov al, ah
    add al, '0'
    mov [di], al
    inc di
    pop dx
    pop ax
    ret

; mppu_put3 - AL = 0..255 as three zero-padded digits at DI
mppu_put3:
    push ax
    push dx
    xor ah, ah
    mov dl, 100
    div dl
    add al, '0'
    mov [di], al
    inc di
    mov al, ah
    call mppu_put2
    pop dx
    pop ax
    ret

; mppu_puth2 - AL as two hex digits at DI
mppu_puth2:
    push ax
    push bx
    push cx
    mov bl, al
    mov cl, 4
    shr al, cl
    call .nib
    mov al, bl
    and al, 0x0F
    call .nib
    pop cx
    pop bx
    pop ax
    ret
.nib:
    add al, '0'
    cmp al, '9'
    jbe .st
    add al, 7
.st:
    mov [di], al
    inc di
    ret

; =============================================================================
; The LCD panel (SPEC.md 56.3) - ModPlug Player's own readout, line for line
; =============================================================================
; in: DI = 0 draw the well too (a full repaint), 1 the text alone (a frame)
mppu_draw_lcd:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    or di, di
    jnz .text                       ; a frame draws only opaque runs, so it
    mov ax, [mppu_lx1]              ; needs no fill and cannot blank a cut cell
    mov bx, [mppu_ly1]
    mov cx, [mppu_lx2]
    mov dx, [mppu_ly2]
    call mppu_well
.text:
    mov word [mppu_lrow], 0
    call mppu_lcd_title
    call mppu_lcd_time
    cmp word [mppu_llines], 4        ; the full layout has room for the format
    jb .three                        ; line AND the transport readout; the
    call mppu_lcd_fmt                ; compact one picks between them, which
    call mppu_lcd_pos                ; is what its Info button is for there
    jmp .out
.three:
    cmp byte [mpp_info], 0
    jne .fmt
    call mppu_lcd_pos
    jmp .out
.fmt:
    call mppu_lcd_fmt
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mppu_lcd_emit - draw [mppu_line] on the next LCD row, then advance the row
;
; ONE OPAQUE RUN (OSAPI_FONT_RUN, SPEC.md 6.1), and that is what lets the
; worker redraw this panel under an armed clip region with no gate at all.
; The obvious version - fill the row black, then letter it - is SPEC.md 11.3's
; granularity trap: a fill clips per pixel and a glyph clips per whole 8x8
; cell, so a Setup window whose edge crosses the LCD would have the panel
; erased in the cells it cuts and the letters refused there, and the readout
; would go BLANK rather than stale, on every frame. font_run decides the
; background and the glyph together, per cell, so a cell it cannot draw it
; also does not erase.
;
; Every line is space-padded to its full width by its builder for the same
; reason the Note Pad's rows are (SPEC.md 27.2): a space paints background on
; this path, so the padding IS the erase and a line that got shorter cleans up
; after itself without a fill anywhere.
; -----------------------------------------------------------------------------
;
; AND IT IS CHECKED BEFORE IT IS DRAWN (SPEC.md 56.12). Composing a line is
; arithmetic; drawing one is twenty-four glyph cells, which on the machine
; this package exists for is twenty-four milliseconds. The title changes when
; a module loads, the format line never changes while one plays, and the
; elapsed time changes on one worker wake in eighteen - so the hash below
; turns four runs a frame into an average of well under one, and it does it
; for all four builders at once because they all emit through here.
;
; A line the clip region CUTS is recorded anyway, and that is safe rather than
; sloppy: font_run draws per cell under an armed region (SPEC.md 6.1.2), so
; the cells it refused are exactly the cells something else is covering - and
; uncovering is a wm_paint_dmg repaint, which reaches us as a W_PAINT, which
; is MPPI_BODY, which is mppu_ckclr. The stale hash cannot outlive the window
; that caused it.
mppu_lcd_emit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [mppu_lrow]             ; -> &mppu_cklcd[row], while the row index
    shl bx, 1                       ; is still cheap to have
    add bx, mppu_cklcd
    mov si, mppu_line
    call mppu_sum                   ; AX = this line's hash
    call mppu_chk                   ; CF=1: these very pixels are on the screen
    jc .done
    mov ax, [mppu_lrow]
    mov cx, MPPU_LROW
    mul cx
    add ax, [mppu_ly1]
    add ax, 3
    mov dx, ax
    add dx, [mppu_dy0]
    mov cx, [mppu_lx1]
    add cx, 4
    add cx, [mppu_dx0]
    mov si, mppu_line
    mov al, [mppu_ink]
    mov ah, CBLACK
    call OSAPI_FONT_RUN
.done:
    inc word [mppu_lrow]
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; line 0 - the module's own title, or the app's name when nothing is loaded
mppu_lcd_title:
    push ax
    push cx
    push si
    push di
    mov di, mppu_line
    mov si, mpp_s_appname
    cmp byte [mpm_loaded], 0
    je .n
    mov si, mpm_title
    cmp byte [si], 0                ; a MOD may carry a blank title, and a
    jne .n                          ; blank line reads as a broken panel
    mov si, mpp_s_untitled
.n:
    mov cl, 24
    call mppu_putn
    mov byte [di], 0
    call mppu_lcd_emit
    pop di
    pop si
    pop cx
    pop ax
    ret

; line 1 - the elapsed time and the file the module came from
mppu_lcd_time:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, mppu_line
    mov ax, [mppu_secs]
    xor dx, dx
    mov cx, 60
    div cx                          ; AX = minutes, DX = seconds
    cmp ax, 99                      ; the field is two digits, so a module
    jbe .mm                         ; that runs past 99 minutes parks there
    mov ax, 99                      ; rather than wrapping round to 00
.mm:
    push dx
    call mppu_put2
    mov al, ':'
    call mppu_putc
    pop ax
    call mppu_put2
    mov al, ' '
    call mppu_putc
    call mppu_putc
    mov si, mpp_lcdname
    cmp byte [si], 0
    jne .f
    mov si, mpp_s_nofile
.f:
    mov cl, 13
    call mppu_putn
    mov byte [di], 0
    call mppu_lcd_emit
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; the format line - ModPlugPlayer's own "44100 Hz 16 Bit Stereo" readout,
; telling the truth about what this machine is doing instead, and then the
; DSP flags that are actually on
mppu_lcd_fmt:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, mppu_line
    mov ax, [mpm_mixrate]           ; ONE DECIMAL, because the four rates this
    xor dx, dx                      ; player has are 5.5, 11, 22.05 and 44.1 -
    mov cx, 1000                    ; and a whole-number readout turns XT
    div cx                          ; mode's 5,500 Hz into '05 kHz', which
    push dx                         ; reads as a bug rather than as a mode
    call mppu_put2
    cmp byte [di-2], '0'            ; ...and a LEADING zero reads as one too:
    jne .kd                         ; '05.5' is not how anyone writes it
    mov byte [di-2], ' '
.kd:
    mov al, '.'
    call mppu_putc
    pop ax
    xor dx, dx
    mov cx, 100
    div cx
    add al, '0'
    call mppu_putc
    mov si, mpp_s_khz
    call mppu_puts
    cmp byte [mpm_xt], 0
    je .lin
    mov si, mpp_s_fxt
    call mppu_puts
.lin:
    cmp byte [mpm_interp], 0
    je .dith
    mov si, mpp_s_flin
    call mppu_puts
.dith:
    cmp byte [mpm_dither], 0
    je .ami
    mov si, mpp_s_fdith
    call mppu_puts
.ami:
    mov bl, [mpm_amiga]
    or bl, bl
    jz .crush
    mov si, mpp_s_fa500
    cmp bl, 1
    je .amis
    mov si, mpp_s_fa1200
.amis:
    call mppu_puts
.crush:
    cmp byte [mpm_crush], 0
    je .done
    mov si, mpp_s_fcrush
    call mppu_puts
.done:
    mov byte [di], 0
    call mppu_lcd_emit
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; the transport readout, or (Info, on the full layout) what each channel is
; playing - the closest thing a four-voice MOD has to ModPlugPlayer's Details
; pane, and free: the replayer already tracks each channel's instrument
mppu_lcd_pos:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [mppu_llines], 4
    jb .transport                   ; the compact layout has no Info line to
    cmp byte [mpp_info], 0          ; spare, so Info means the format line
    jne .info                       ; there and this one is always transport
.transport:
    mov di, mppu_line
    mov si, mpp_s_pos
    call mppu_puts
    mov al, [mpm_songpos]
    call mppu_put2
    mov al, '/'
    call mppu_putc
    mov al, [mpm_songlen]
    call mppu_put2
    mov si, mpp_s_pat
    call mppu_puts
    mov al, [mpm_pattern]
    call mppu_put2
    mov si, mpp_s_row
    call mppu_puts
    mov al, [mpm_row]
    call mppu_puth2
    mov si, mpp_s_spd
    call mppu_puts
    mov al, [mpm_speed]
    call mppu_put2
    mov si, mpp_s_bpm
    call mppu_puts
    mov al, [mpm_bpm]
    call mppu_put3
    jmp .emit
.info:
    mov di, mppu_line
    xor bx, bx                      ; BX = the channel, 0..3
.ch:
    mov al, bl
    add al, '1'
    call mppu_putc
    mov al, ':'
    call mppu_putc
    push bx
    mov al, MPM_CHSZ                ; the channel record, without an imm mul
    mul bl
    mov si, mpm_chans
    add si, ax
    mov al, [si+MPM_SMP]
    or al, al
    jz .none
    call mpm_insname                ; out ES:SI -> 22 RAW bytes in the blob,
    jc .none                        ; and ES is the blob's segment, not ours
    mov cl, 5
    call mppu_putnes                ; ...so it is filtered and truncated here
    push ds
    pop es                          ; ES = DS again: the callback default, and
    jmp .sp                         ; mpm_insname's documented caller duty
.none:
    mov si, mpp_s_dashes
    mov cl, 5
    call mppu_putn
.sp:
    mov al, ' '
    call mppu_putc
    pop bx
    inc bx
    cmp bx, 4
    jb .ch
.emit:
    mov byte [di], 0
    call mppu_lcd_emit
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The transport row (SPEC.md 56.3): nine bevelled buttons, each with a vector
; glyph and an LED strip beneath it. ModPlugPlayer's buttons ARE LEDs - its
; play triangle lights when it plays - and the strip is how that survives to
; one bit, where "lit green" and "unlit grey" are the same pixel.
; =============================================================================
mppu_draw_buttons:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si                      ; SI = the button index
.b:
    call mppu_btn_rect              ; -> AX BX CX DX = its face
    call mppu_face
    call mppu_btn_glyph
    call mppu_btn_led
    inc si
    cmp si, MPPU_NBTN
    jb .b
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppu_draw_leds - the dynamic half: the LED strips alone, on every frame
mppu_draw_leds:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.b:
    call mppu_btn_rect
    call mppu_btn_led
    inc si
    cmp si, MPPU_NBTN
    jb .b
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppu_btn_rect - SI = index -> AX BX CX DX = its face, content-relative
mppu_btn_rect:
    push si
    mov ax, MPPU_BPITCH
    mul si                          ; DX:AX, and 9 * 32 cannot reach DX
    add ax, MPPU_BX0
    mov cx, ax
    add cx, MPPU_BW - 1
    mov bx, [mppu_by]
    mov dx, bx
    add dx, [mppu_bh]
    dec dx
    pop si
    ret

; mppu_btn_led - SI = index, AX BX CX DX = its face; the strip under the glyph
mppu_btn_led:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, ax                      ; the strip, centred on the face's bottom
    add di, (MPPU_BW - MPPU_LEDW) / 2
    mov bx, dx
    sub bx, MPPU_LEDH + 1
    mov ax, di
    mov cx, di
    add cx, MPPU_LEDW - 1
    mov dx, bx
    add dx, MPPU_LEDH - 1
    push ax
    mov ax, si
    call mppu_btn_lit               ; AL = 0 dark / 1 lit
    mov di, ax
    pop ax
    or di, di
    jz .dark
    MPPU_PEN [mppu_ink]             ; lit: ModPlug's green, white at 1bpp
    jmp .fill
.dark:
    MPPU_PEN CBLACK                 ; unlit: a black well, so lit against
.fill:                              ; unlit is white against black - a
    call mppu_fillr                 ; distinction no colour reduction eats
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppu_btn_lit - AX = index -> AL = 1 if that button's function is active now
mppu_btn_lit:
    push bx
    xor bx, bx
    cmp ax, 3
    je .play
    cmp ax, 4
    je .pause
    cmp ax, 5
    je .stop
    cmp ax, 8
    je .setup
    jmp .out                        ; Open, Prev, Rew and FFwd are momentary:
.play:                              ; they have no state to show
    cmp byte [mpm_playing], 0
    je .out
    inc bl
    jmp .out
.pause:
    mov bl, [mpp_paused]
    jmp .out
.stop:
    cmp byte [mpp_sopen], 0         ; "stopped" is the absence of a STREAM,
    jne .out                        ; not of playback: a paused player is not
    inc bl                          ; a stopped one, and the two lamps say so
    jmp .out
.setup:
    call mpps_visible               ; AL = 1 while the Setup window is up
    mov bl, al
.out:
    mov ax, bx
    pop bx
    ret

; -----------------------------------------------------------------------------
; mppu_btn_glyph - SI = index, AX BX CX DX = its face; draw its symbol
; The glyphs are ModPlug Player's: eject, |<<, <<, >, ||, [], >>, >>|, and a
; three-slider "setup" mark. All are black on the grey face, which at 1bpp is
; the one pairing that keeps every stroke it started with.
; -----------------------------------------------------------------------------
mppu_btn_glyph:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, ax
    add di, MPPU_BW / 2
    mov [mppu_gx], di               ; the glyph's centre x...
    mov ax, dx
    sub ax, MPPU_LEDH + 2
    add ax, bx
    shr ax, 1                       ; ...and its centre y, biased up off the
    mov [mppu_gy], ax               ; LED strip
    MPPU_PEN CBLACK
    mov ax, si
    shl ax, 1
    mov si, ax
    jmp [mppu_glyphtab + si]

.eject:                             ; a triangle over a bar
    mov ax, [mppu_gx]
    sub ax, 4
    mov bx, [mppu_gy]
    add bx, 1
    mov cx, 4
    call mppu_tri_up
    mov ax, [mppu_gx]
    sub ax, 4
    mov cx, ax
    add cx, 8
    mov bx, [mppu_gy]
    add bx, 3
    mov dx, bx
    inc dx
    call mppu_fillr
    jmp .done
.prev:                              ; a bar, then two left-pointing triangles
    mov ax, [mppu_gx]
    sub ax, 6
    call mppu_vbar
    mov ax, [mppu_gx]
    add ax, 1
    call mppu_ltri
    mov ax, [mppu_gx]
    add ax, 6
    call mppu_ltri
    jmp .done
.rew:
    mov ax, [mppu_gx]
    call mppu_ltri
    mov ax, [mppu_gx]
    add ax, 5
    call mppu_ltri
    jmp .done
.playg:
    mov ax, [mppu_gx]
    sub ax, 3
    mov bx, [mppu_gy]
    mov cx, 5                       ; CH = 0: points right
    call mppu_tri
    jmp .done
.pauseg:
    mov ax, [mppu_gx]
    sub ax, 4
    call mppu_vbar
    mov ax, [mppu_gx]
    add ax, 2
    call mppu_vbar
    jmp .done
.stopg:
    mov ax, [mppu_gx]
    sub ax, 4
    mov cx, ax
    add cx, 8
    mov bx, [mppu_gy]
    sub bx, 4
    mov dx, bx
    add dx, 8
    call mppu_fillr
    jmp .done
.ffwd:
    mov ax, [mppu_gx]
    sub ax, 5
    call mppu_rtri
    mov ax, [mppu_gx]
    call mppu_rtri
    jmp .done
.next:
    mov ax, [mppu_gx]
    sub ax, 7
    call mppu_rtri
    mov ax, [mppu_gx]
    sub ax, 2
    call mppu_rtri
    mov ax, [mppu_gx]
    add ax, 4
    call mppu_vbar
    jmp .done
.setupg:                            ; three rails, each with its knob at a
    mov di, 0                       ; different stop - the mixer-strip mark
.sl:                                ; ModPlug uses for Setup
    mov ax, [mppu_gx]
    sub ax, 5
    mov bx, ax
    add bx, 10
    mov dx, [mppu_gy]
    sub dx, 4
    push ax
    mov ax, di
    shl ax, 1
    shl ax, 1
    add dx, ax
    pop ax
    call mppu_hlr
    mov ax, [mppu_gx]               ; the knob, stepped along its own rail
    sub ax, 5
    add ax, di
    add ax, di
    add ax, di
    mov cx, ax
    inc cx
    push ax
    mov ax, di
    shl ax, 1
    shl ax, 1
    mov bx, [mppu_gy]
    sub bx, 5
    add bx, ax
    mov dx, bx
    add dx, 2
    pop ax
    call mppu_fillr
    inc di
    cmp di, 3
    jb .sl
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mppu_glyphtab:
    dw mppu_btn_glyph.eject, mppu_btn_glyph.prev, mppu_btn_glyph.rew
    dw mppu_btn_glyph.playg, mppu_btn_glyph.pauseg, mppu_btn_glyph.stopg
    dw mppu_btn_glyph.ffwd, mppu_btn_glyph.next, mppu_btn_glyph.setupg

; the three shapes the glyph table builds everything out of; each takes AX = x
; and reads [mppu_gy], and all preserve their registers
mppu_rtri:
    push bx
    push cx
    mov bx, [mppu_gy]
    mov cx, 0x0004                  ; CH = 0: points right
    call mppu_tri
    pop cx
    pop bx
    ret
mppu_ltri:
    push bx
    push cx
    mov bx, [mppu_gy]
    mov cx, 0x0104                  ; CH = 1: points left
    call mppu_tri
    pop cx
    pop bx
    ret
mppu_vbar:                          ; a 2x9 upright bar at AX
    push ax
    push bx
    push cx
    push dx
    mov cx, ax
    inc cx
    mov bx, [mppu_gy]
    sub bx, 4
    mov dx, bx
    add dx, 8
    call mppu_fillr
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The time scrubber and the volume slider (SPEC.md 56.3)
; =============================================================================
mppu_draw_scrub:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [mppu_sx1]
    mov bx, [mppu_sy1]
    mov cx, [mppu_sx2]
    mov dx, [mppu_sy2]
    call mppu_well
    call mppu_scrub_thumb           ; AX = the handle's left x
    mov cx, ax
    add cx, MPPU_THUMBW - 1
    mov bx, [mppu_sy1]
    inc bx
    mov dx, [mppu_sy2]
    dec dx
    call mppu_face
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppu_scrub_thumb - AX = the handle's left x, from the song position
mppu_scrub_thumb:
    push bx
    push cx
    push dx
    mov cx, [mppu_sx2]              ; CX = the travel, in pixels
    sub cx, [mppu_sx1]
    sub cx, MPPU_THUMBW + 1
    mov al, [mpm_songlen]
    xor ah, ah
    dec ax                          ; the positions are 0 .. songlen-1
    jle .zero
    mov bx, ax
    mov al, [mpm_songpos]
    xor ah, ah
    mul cx                          ; DX:AX = pos * travel; 128 * 400 fits a
    div bx                          ; word, so the divide cannot #DE
    jmp .place
.zero:
    xor ax, ax                      ; a one-position song parks the handle at
.place:                             ; the start rather than dividing by zero
    add ax, [mppu_sx1]
    inc ax
    pop dx
    pop cx
    pop bx
    ret

mppu_draw_vol:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [mppu_vx1]
    mov bx, [mppu_vy1]
    mov cx, [mppu_vx2]
    mov dx, [mppu_vy2]
    call mppu_well
    call mppu_vol_thumb             ; AX = the handle's left x
    mov cx, ax
    add cx, MPPU_THUMBW - 1
    mov bx, [mppu_vy1]
    inc bx
    mov dx, [mppu_vy2]
    dec dx
    call mppu_face
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppu_vol_thumb - AX = the handle's left x, from [mpm_master] (0..64)
mppu_vol_thumb:
    push bx
    push cx
    push dx
    mov cx, [mppu_vx2]
    sub cx, [mppu_vx1]
    sub cx, MPPU_THUMBW + 1
    mov al, [mpm_master]
    xor ah, ah
    mul cx
    mov bx, 64
    div bx
    add ax, [mppu_vx1]
    inc ax
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; The visualiser (SPEC.md 56.6) - spectrum analyser, oscilloscope, VU meter
; =============================================================================

; mppu_viz_reset - clear every bar, cap and trace
mppu_viz_reset:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    cld
    mov di, mppu_band
    mov cx, MPPU_MAXBAND * 2        ; the bars and their peak caps
    xor al, al
    rep stosb
    mov di, mppu_vu
    mov cx, MPPU_VUCH * 2
    rep stosb
    mov di, mppu_scope
    mov cx, MPPU_SCOPEN
    mov al, 128                     ; silence is the CENTRE line, not zero
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mppu_viz_feed - bank the oscilloscope's trace out of the chunk just mixed
; in:  mpm_outbuf holds MPP_HALF fresh bytes; preserves all registers
;
; Called from mpp_mix_stage, which is the ONLY moment that buffer is fresh -
; the next call overwrites it. It runs on the worker while a module plays and
; on the UI task during the pre-roll, and the drawing side reads it with no
; handshake: a torn trace is one frame of a waveform drawn from two chunks,
; which is invisible at 18 frames a second and not worth a lock.
; -----------------------------------------------------------------------------
mppu_viz_feed:
    push ax
    push cx
    push si
    push di
    mov si, mpm_outbuf
    mov di, mppu_scope
    mov cx, MPPU_SCOPEN
.c:
    mov al, [si]
    mov [di], al
    add si, MPP_HALF / MPPU_SCOPEN  ; stride the WHOLE chunk, so the trace
    inc di                          ; spans the buffer instead of magnifying
    loop .c                         ; its first few milliseconds
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mppu_viz_step - advance the meters one frame: decay what is falling, raise
;                 what the replayer is playing now, drag the peak caps behind
; preserves all registers
;
; THE SPECTRUM ANALYSER IS NOT AN FFT, and SPEC.md 56.6 says so in the
; contract rather than leaving it to be discovered. ModPlugPlayer runs KissFFT
; over its output; this machine has no cycles for that, and a four-voice
; source makes it unnecessary. Each channel's PERIOD is matched against the
; replayer's own ProTracker period table - 36 entries, three octaves, one
; semitone apart - and the note index that comes back is projected onto the
; bands. A semitone index IS a logarithmic frequency axis, which is the axis a
; spectrum analyser is drawn on, so the bars land where the notes are. What it
; cannot show is harmonic content: a square-wave lead puts up one bar, not the
; odd-harmonic comb a real transform would draw.
; -----------------------------------------------------------------------------
mppu_viz_step:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; --- decay every bar and cap first --------------------------------------
    mov cx, MPPU_MAXBAND
    xor si, si
.dec:
    mov al, [mppu_band + si]
    sub al, 20
    jnc .db
    xor al, al
.db:
    mov [mppu_band + si], al
    mov al, [mppu_peak + si]
    sub al, 6                       ; the cap falls slower than the bar, and
    jnc .dp                         ; that lag IS the peak-hold readout
    xor al, al
.dp:
    mov [mppu_peak + si], al
    inc si
    loop .dec
    mov cx, MPPU_VUCH
    xor si, si
.dvu:
    mov al, [mppu_vu + si]
    sub al, 24
    jnc .dv
    xor al, al
.dv:
    mov [mppu_vu + si], al
    mov al, [mppu_vupk + si]
    sub al, 6
    jnc .dvp
    xor al, al
.dvp:
    mov [mppu_vupk + si], al
    inc si
    loop .dvu
    cmp byte [mpm_playing], 0
    je .out
    ; --- then raise from the four channels ----------------------------------
    xor si, si                      ; SI = the channel, 0..3
.ch:
    cmp byte [mpm_mute + si], 0
    jne .next                       ; a muted channel contributes nothing, so
                                    ; the meters agree with what is audible
    mov bx, si
    shl bx, 1
    mov ax, [mpm_chper + bx]        ; the period the channel is sounding
    or ax, ax
    jz .next
    mov cl, [mpm_chvol + si]        ; its volume, 0..64
    or cl, cl
    jz .next
    mov al, cl                      ; the level: 0..64 scaled up to 0..255,
    shl al, 1                       ; saturating rather than wrapping
    shl al, 1
    jnc .lvok
    mov al, 255
.lvok:
    mov [mppu_lvl], al              ; banked in bss, not in a register: the
                                    ; band arithmetic below needs AX for mul,
                                    ; DX for div's high half and CX for the
                                    ; divisor, which is every register a byte
                                    ; could have hidden in
    ; the VU meter first - the same level with no band projection at all
    cmp al, [mppu_vu + si]
    jbe .band
    mov [mppu_vu + si], al
    cmp al, [mppu_vupk + si]
    jbe .band
    mov [mppu_vupk + si], al
.band:
    call mppu_note_of               ; AX = period -> AL = 0..35, CF=1 outside
    jc .next                        ; the table's three octaves
    xor ah, ah
    mul word [mppu_bands]           ; band = note * bands / 36, and 35 * 16
    mov cx, 36                      ; leaves mul's DX at zero, which is what
    div cx                          ; div's dividend needs it to be
    mov di, ax
    cmp di, [mppu_bands]
    jb .lvl
    mov di, [mppu_bands]
    dec di
.lvl:
    mov al, [mppu_lvl]
    call mppu_band_raise            ; DI = band, AL = level
    mov al, [mppu_lvl]              ; ...and half of it into each neighbour,
    shr al, 1                       ; which is what gives a four-voice source
    dec di                          ; the shape of a spectrum instead of four
    js .rn                          ; isolated spikes
    call mppu_band_raise
.rn:
    inc di
    inc di
    mov al, [mppu_lvl]
    shr al, 1
    call mppu_band_raise            ; (mppu_band_raise range-checks DI itself)
.next:
    inc si
    cmp si, MPPU_VUCH
    jb .ch
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppu_band_raise - DI = a band, AL = a level. A bar only ever RISES here and
; its cap follows it up; falling is the decay pass's job alone. DI is
; range-checked here so the callers may hand it a neighbour off either end.
; preserves all registers.
mppu_band_raise:
    push ax
    cmp di, MPPU_MAXBAND
    jae .out
    cmp di, [mppu_bands]
    jae .out
    cmp al, [mppu_band + di]
    jbe .out
    mov [mppu_band + di], al
    cmp al, [mppu_peak + di]
    jbe .out
    mov [mppu_peak + di], al
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; mppu_note_of - AX = a ProTracker period -> AL = the semitone index 0..35
; out: CF=1 if it is outside the table's three octaves
; preserves BX CX DX SI DI
;
; The replayer already carries the exact finetune-0 period table, so this is a
; walk down 36 words for the first entry the period reaches. Periods FALL as
; pitch rises, so the table is descending and the first match is the note.
; 36 compares per channel per frame is nothing beside the mixing.
; -----------------------------------------------------------------------------
mppu_note_of:
    push bx
    push cx
    push si
    mov si, mpm_pertab
    xor bx, bx
    mov cx, 36
.n:
    cmp ax, [si]
    jae .hit
    add si, 2
    inc bx
    loop .n
    stc                             ; higher than the top of the table
    jmp .out
.hit:
    mov ax, bx
    clc
.out:
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; mppu_draw_viz - whichever of the three is selected
; -----------------------------------------------------------------------------
mppu_draw_viz:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [mppu_zx1]
    mov bx, [mppu_zy1]
    mov cx, [mppu_zx2]
    mov dx, [mppu_zy2]
    call mppu_well
    cmp byte [mpm_loaded], 0
    je .out                         ; an empty well: there is no level to draw
    mov al, [mpp_viz]
    cmp al, MPPV_SCOPE
    je .scope
    cmp al, MPPV_VU
    je .vu
    call mppu_viz_spectrum
    jmp .out
.scope:
    call mppu_viz_scope
    jmp .out
.vu:
    call mppu_viz_vu
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the spectrum analyser ------------------------------------------------------
mppu_viz_spectrum:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [mppu_zx2]              ; the pane's inside, and so the bar pitch
    sub ax, [mppu_zx1]
    sub ax, 3
    xor dx, dx
    div word [mppu_bands]
    or ax, ax
    jnz .p
    inc ax                          ; a pane too narrow for one column a band
.p:                                 ; still draws something
    mov [mppu_bpitch], ax
    xor si, si                      ; SI = the band index
.b:
    mov ax, [mppu_bpitch]           ; the bar's span
    mul si
    add ax, [mppu_zx1]
    add ax, 2
    mov [mppu_q1], ax
    add ax, [mppu_bpitch]
    dec ax
    mov [mppu_q3], ax               ; one pixel of gap between bars
    mov al, [mppu_band + si]
    or al, al
    jz .cap
    call mppu_viz_h                 ; AL = level -> AX = its height in pixels
    or ax, ax
    jz .cap
    mov dx, [mppu_zy2]
    dec dx
    mov bx, dx
    sub bx, ax
    inc bx
    cmp bx, [mppu_zy1]
    ja .h
    mov bx, [mppu_zy1]
    inc bx
.h:
    mov al, [mppu_band + si]
    call mppu_viz_ink               ; the classic green/yellow/red ramp - one
    mov ax, [mppu_q1]               ; colour for the whole bar, because a
    mov cx, [mppu_q3]               ; gradient costs a fill per row
    call mppu_fillr
.cap:
    mov al, [mppu_peak + si]
    or al, al
    jz .next
    call mppu_viz_h
    mov dx, [mppu_zy2]
    dec dx
    sub dx, ax
    cmp dx, [mppu_zy1]              ; the cap rides at the top of the pane
    ja .capok                       ; rather than climbing out of it
    mov dx, [mppu_zy1]
    inc dx
.capok:
    MPPU_PEN CWHITE
    mov ax, [mppu_q1]
    mov bx, [mppu_q3]
    call mppu_hlr
.next:
    inc si
    cmp si, [mppu_bands]
    jb .b
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppu_viz_h - AL = a level 0..255 -> AX = its height in the pane, in pixels
mppu_viz_h:
    push bx
    push cx
    push dx
    xor ah, ah
    mov cx, [mppu_zy2]
    sub cx, [mppu_zy1]
    sub cx, 2                       ; the pane's inside
    mul cx
    mov bx, 255
    div bx
    pop dx
    pop cx
    pop bx
    ret

; mppu_viz_ink - AL = a level; the green/yellow/red ramp ModPlug Player draws
; its bars with - and white at 1bpp, where all three are the same class and
; the HEIGHT is the whole reading. Preserves all registers.
mppu_viz_ink:
    push ax
    cmp byte [mppu_ink], CWHITE
    je .mono
    cmp al, 170
    jb .green
    cmp al, 220
    jb .yellow
    MPPU_PEN CLRED
    jmp .out
.yellow:
    MPPU_PEN CYELLOW
    jmp .out
.green:
    MPPU_PEN CLGREEN
    jmp .out
.mono:
    MPPU_PEN CWHITE
.out:
    pop ax
    ret

; --- the oscilloscope -----------------------------------------------------------
; One column per banked sample, drawn as a run from the centre line, which is
; what makes a quiet passage a line rather than nothing at all.
mppu_viz_scope:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    MPPU_PEN [mppu_ink]
    mov ax, [mppu_zy1]              ; the centre line...
    add ax, [mppu_zy2]
    shr ax, 1
    mov [mppu_zmid], ax
    mov ax, [mppu_zy2]              ; ...and the half-height it may swing over
    sub ax, [mppu_zy1]
    sub ax, 4
    shr ax, 1
    mov [mppu_zamp], ax
    mov si, [mppu_zx1]
    inc si                          ; SI = the column being drawn
    xor di, di                      ; DI = the sample index
.c:
    mov al, [mppu_scope + di]
    sub al, 128                     ; the buffer is unsigned around a 0x80
    cbw                             ; bias; the trace is signed around zero
    imul word [mppu_zamp]
    mov bx, 128
    idiv bx                         ; AX = the excursion, in pixels
    mov bx, [mppu_zmid]
    mov dx, bx
    add dx, ax
    cmp dx, bx                      ; draw from the centre to the excursion,
    jae .ord                        ; in whichever order puts y1 first
    xchg bx, dx
.ord:
    mov ax, si
    call mppu_vlr
    inc si
    inc di
    cmp di, MPPU_SCOPEN
    jae .done
    cmp si, [mppu_zx2]
    jb .c
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the VU meter ---------------------------------------------------------------
; Four horizontal bars, one per replayer channel, each with its peak cap and
; its channel number. A click in a row mutes that channel (see mppu_hit) -
; the same gesture the number keys are.
mppu_viz_vu:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mppu_vu_geom               ; -> [mppu_vuh], one row's height
    xor si, si
.r:
    call mppu_vu_row                ; SI = channel -> [mppu_q2] / [mppu_q4]
    mov ax, [mppu_zx1]
    add ax, 12                      ; room for the channel number at the left
    mov [mppu_q1], ax
    mov al, [mppu_vu + si]
    or al, al
    jz .cap
    call mppu_vu_w                  ; AL = level -> AX = its width in pixels
    or ax, ax
    jz .cap
    add ax, [mppu_q1]
    mov [mppu_q3], ax
    mov al, [mppu_vu + si]
    call mppu_viz_ink
    mov ax, [mppu_q1]
    mov cx, [mppu_q3]
    mov bx, [mppu_q2]
    mov dx, [mppu_q4]
    call mppu_fillr
.cap:
    mov al, [mppu_vupk + si]
    or al, al
    jz .num
    call mppu_vu_w
    add ax, [mppu_q1]
    MPPU_PEN CWHITE
    mov bx, [mppu_q2]
    mov dx, [mppu_q4]
    call mppu_vlr
.num:
    MPPU_PEN [mppu_ink]
    mov ax, si                      ; the channel number, or a dash where the
    add al, '1'                     ; channel is muted - the state carried in
    cmp byte [mpm_mute + si], 0     ; shape, not in a colour a mono adapter
    je .live                        ; would flatten
    mov al, '-'
.live:
    mov [mppu_line], al
    mov byte [mppu_line + 1], 0
    push si
    mov si, mppu_line
    mov cx, [mppu_zx1]
    add cx, 3
    mov dx, [mppu_q2]
    call mppu_strr
    pop si
    inc si
    cmp si, MPPU_VUCH
    jb .r
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppu_vu_w - AL = a level 0..255 -> AX = its width in the pane, in pixels
mppu_vu_w:
    push bx
    push cx
    push dx
    xor ah, ah
    mov cx, [mppu_zx2]
    sub cx, [mppu_zx1]
    sub cx, 14
    mul cx
    mov bx, 255
    div bx
    pop dx
    pop cx
    pop bx
    ret

; mppu_vu_geom - [mppu_vuh] = the height of one VU row
mppu_vu_geom:
    push ax
    push cx
    push dx
    mov ax, [mppu_zy2]
    sub ax, [mppu_zy1]
    dec ax
    xor dx, dx
    mov cx, MPPU_VUCH
    div cx
    or ax, ax
    jnz .ok
    inc ax
.ok:
    mov [mppu_vuh], ax
    pop dx
    pop cx
    pop ax
    ret

; mppu_vu_row - SI = channel -> [mppu_q2] / [mppu_q4], its band's top and
; bottom. Preserves all registers.
mppu_vu_row:
    push ax
    push dx
    mov ax, [mppu_vuh]
    mul si
    add ax, [mppu_zy1]
    inc ax
    mov [mppu_q2], ax
    add ax, [mppu_vuh]
    sub ax, 2
    mov [mppu_q4], ax
    pop dx
    pop ax
    ret

; =============================================================================
; The option-button grid (SPEC.md 56.3) - ModPlug Player's labelled toggles,
; each with an LED at its left edge saying whether it is on
; =============================================================================
mppu_draw_opts:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.o:
    call mppu_opt_rect              ; SI = index -> AX BX CX DX
    call mppu_face
    call mppu_opt_led
    call mppu_opt_label
    inc si
    cmp si, [mppu_on]
    jb .o
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppu_draw_optleds - the option grid's indicators alone, for a worker frame
mppu_draw_optleds:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.o:
    call mppu_opt_rect
    call mppu_opt_led
    inc si
    cmp si, [mppu_on]
    jb .o
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppu_opt_rect - SI = index -> AX BX CX DX = its face
mppu_opt_rect:
    push si
    push di
    mov ax, si
    xor dx, dx
    div word [mppu_ocols]           ; AX = the row, DX = the column
    mov di, dx                      ; ...bank the column, the muls want AX
    mov cx, [mppu_opitchy]
    mul cx
    add ax, [mppu_oy1]
    mov bx, ax                      ; BX = y1
    mov ax, [mppu_opitchx]
    mul di
    add ax, [mppu_ox1]              ; AX = x1
    mov cx, ax
    add cx, [mppu_ow]
    dec cx
    mov dx, bx
    add dx, [mppu_oh]
    dec dx
    pop di
    pop si
    ret

; mppu_opt_led - the indicator at the button's left edge. It sits in a black
; well, so "on" is white against black on every adapter.
mppu_opt_led:
    push ax
    push bx
    push cx
    push dx
    push di
    mov [mppu_q1], ax               ; bank the FACE before building the LED's
    mov [mppu_q2], bx               ; own rect out of it
    mov ax, si
    call mppu_opt_state             ; AL = on, AH = greyed
    mov di, ax
    mov ax, [mppu_q1]
    add ax, 3
    mov bx, [mppu_q2]
    add bx, 3
    mov cx, ax
    add cx, 3
    mov dx, bx
    add dx, 3
    MPPU_PEN CBLACK                 ; the well, one pixel proud all round
    push ax
    push bx
    push cx
    push dx
    dec ax
    dec bx
    inc cx
    inc dx
    call mppu_fillr
    pop dx
    pop cx
    pop bx
    pop ax
    mov [mppu_q3], di               ; the state, past the pen macro's push
    cmp byte [mppu_q3], 0
    je .dark
    cmp byte [mppu_q3 + 1], 0       ; greyed AND on is still drawn dark: a
    jne .dark                       ; control that cannot act must not look
    MPPU_PEN [mppu_ink]             ; like one that is acting (SPEC.md 47)
    jmp .fill
.dark:
    MPPU_PEN CBLACK
.fill:
    call mppu_fillr
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppu_opt_label - the caption, to the right of the LED
mppu_opt_label:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [mppu_q1], ax
    mov [mppu_q2], bx
    mov ax, si
    call mppu_opt_state             ; AH = 1 when the control is unavailable
    mov [mppu_qdis], ah             ; SPEC.md 47: greyed is a WHOLE control -
                                    ; the LED above went dark with the label
.txt:
    mov ax, si
    shl ax, 1
    mov di, ax
    mov si, [mpp_optlab + di]
    mov cx, [mppu_q1]
    add cx, 11                      ; 11 + 8 glyphs of 'Spectrum' is 75, which
    add cx, [mppu_dx0]              ; clears the right bevel of a 78-pixel
    mov dx, [mppu_q2]               ; button - the reason the LED is 4px
    add dx, 2
    add dx, [mppu_dy0]
    cmp byte [mppu_qdis], 0         ; an opaque run again, so a label may be
    stc                             ; redrawn on its own without a fill under
    jne .pen                        ; it (and so a clip edge cannot blank it)
    clc
.pen:
    call mppu_dink                  ; -> AL, and [gfx_dis] with it: at 1bpp
    mov ah, [mppu_body]             ; the flag is the whole of the greying
    call OSAPI_FONT_RUN
    call mppu_dpen_off
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mppu_opt_state - AX = the option index
; out: AL = 1 if it is ON, AH = 1 if it is UNAVAILABLE right now
; preserves BX CX DX SI DI
;
; ONE predicate, read by the LED, by the label's pen and by mpp_option's
; refusal. That is SPEC.md 47's rule 2: the greying and the refusal must not
; be able to disagree about what is available.
; -----------------------------------------------------------------------------
mppu_opt_state:
    push bx
    push si
    xor bx, bx                      ; BL = on, BH = greyed
    cmp ax, 0
    je .rep
    cmp ax, 1
    je .shuf
    cmp ax, 2
    je .list
    cmp ax, 3
    je .ami
    cmp ax, 4
    je .dith
    cmp ax, 5
    je .xt
    cmp ax, 6
    je .scope
    cmp ax, 7
    je .spec
    cmp ax, 8
    je .vu
    cmp ax, 9
    je .info
    cmp ax, 10
    je .setup
    jmp .about
.rep:
    cmp byte [mpp_repeat], 0
    je .out
    inc bl
    jmp .out
.shuf:
    mov bl, [mpp_shuffle]
    jmp .out
.list:
    call mppl_visible
    mov bl, al
    jmp .out
.ami:
    cmp byte [mpm_amiga], 0
    je .amig
    inc bl
.amig:
    mov bh, [mpm_xt]                ; XT mode owns the DSP tail (SPEC.md 56.7)
    jmp .out
.dith:
    mov bl, [mpm_dither]
    mov bh, [mpm_xt]
    jmp .out
.xt:
    mov bl, [mpm_xt]
    jmp .out
.scope:
    cmp byte [mpp_viz], MPPV_SCOPE
    jne .out
    inc bl
    jmp .out
.spec:
    cmp byte [mpp_viz], MPPV_SPECTRUM
    jne .out
    inc bl
    jmp .out
.vu:
    cmp byte [mpp_viz], MPPV_VU
    jne .out
    inc bl
    jmp .out
.info:
    mov bl, [mpp_info]
    jmp .out
.setup:
    call mpps_visible
    mov bl, al
    jmp .out
.about:
    mov bl, [mpp_abon]
.out:
    mov ax, bx
    pop si
    pop bx
    ret

; =============================================================================
; The status line - erased and lettered as one band
; =============================================================================
mppu_msg:
    mov [mppu_msgp], si
    ret

; One opaque run, padded to the band, for mppu_lcd_emit's reasons exactly
mppu_draw_status:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, [mppu_msgp]
    or si, si
    jnz .s
    mov si, mpp_s_hint
.s:
    mov di, mppu_line
    mov cl, 50                      ; the padding is the erase
    call mppu_putn
    mov byte [di], 0
    mov si, mppu_line
    mov cx, 6
    add cx, [mppu_dx0]
    mov dx, [mppu_sty]
    add dx, [mppu_dy0]
    mov al, CBLACK
    mov ah, [mppu_body]
    call OSAPI_FONT_RUN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The About panel (OSAPI_ABOUT_SET) - drawn into our own content, over
; everything, and taken down by the next key or click. The [ark_abon] pattern.
; =============================================================================
mppu_draw_about:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cl, 4                       ; 8086: a shift is by 1 or by CL, and
    mov ax, [mppu_cw]               ; nothing else (SPEC.md 1)
    shr ax, cl                      ; A centred card, sized off the content so
    mov [mppu_q1], ax               ; the compact layout gets a smaller one -
    mov cx, [mppu_cw]               ; but SIXTEENTHS across and eighths down,
    sub cx, ax                      ; because the widest line here is 42
    mov [mppu_q3], cx               ; glyphs and eighths left it 312 pixels to
    mov cl, 3                       ; put 336 into, which reads as a card with
    mov bx, [mppu_ch]               ; its credits chopped off
    shr bx, cl
    mov [mppu_q2], bx
    mov dx, [mppu_ch]
    sub dx, bx
    mov [mppu_q4], dx
    mov ax, [mppu_q1]               ; RELOAD all four: the `mov cl, 3` above
    mov bx, [mppu_q2]               ; is a write to CX, which had been holding
    mov cx, [mppu_q3]               ; the card's right edge since three
    mov dx, [mppu_q4]               ; instructions earlier - the same class of
    call mppu_face                  ; bug the MPPU_PEN macro exists to stop,
                                    ; here done by a shift count instead of a
                                    ; colour, and it drew a card too narrow
                                    ; for its own credits
    MPPU_PEN CBLACK
    mov si, mpp_about_lines
    mov dx, [mppu_q2]
    add dx, 6
.l:
    mov di, [si]                    ; a NUL string pointer; 0 ends the card
    or di, di
    jz .done
    push si
    mov si, di
    mov cx, [mppu_q1]
    add cx, 10
    call mppu_strr
    pop si
    add si, 2
    add dx, 10
    mov ax, dx
    add ax, 8
    cmp ax, [mppu_q4]
    jb .l
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; mppu_clock - one worker wake of elapsed time
; in:  called from mpp_worker with NO lock held; preserves all registers
;
; The elapsed time cannot come from [mpp_total]: that counter is 16 bits and
; free-running, so it wraps every 65,536 bytes - six seconds at 11 kHz. It
; cannot come from the redraw either, because a covered or hidden window skips
; frames and the clock would stop with the picture. So it is counted here, on
; the worker's own ~18 Hz wake, which runs whatever the screen is doing.
; =============================================================================
mppu_clock:
    cmp byte [mpm_playing], 0
    je .out
    inc word [mppu_frames]
    cmp word [mppu_frames], 18
    jb .out
    sub word [mppu_frames], 18
    inc word [mppu_secs]
.out:
    ret

mppu_clock_reset:
    mov word [mppu_frames], 0
    mov word [mppu_secs], 0
    ret

; =============================================================================
; The invalidation mask (SPEC.md 56.12) - what makes this a player and not a
; slideshow
;
; The worker wakes ~18 times a second and every one of those wakes used to
; redraw the LCD's four lines, the fifty-character status line, all nine
; transport lamps, all twelve option lamps and the whole 405-pixel scrubber
; groove - about 140 glyph cells and 5,300 pixels of fill, essentially none of
; which differed from what was already on the screen. On the 4.77 MHz machine
; this package exists for, a glyph cell costs about a millisecond
; (SPEC.md 6.1.1), so the text alone was 140 ms of work inside a 55 ms frame.
; The player could not keep its own clock.
;
; So drawing is now gated on a WORD OF DIRTY BITS, the way every window system
; of the period gated it on an update region: InvalRect and BeginUpdate on the
; Macintosh, InvalidateRect and the update region BeginPaint hands back on
; Windows, the layer damage list behind Intuition's BeginRefresh on the Amiga.
; Two things set those bits, and the split is the whole design:
;
;   - mppu_poll asks the CHEAP PURE PREDICATES. Nine calls to mppu_btn_lit,
;     twelve to mppu_opt_state, one multiply-and-divide for each slider handle
;     and one pointer compare for the status line - all of it arithmetic on
;     bytes this package already owns, none of it touching the framebuffer. An
;     element whose signature is unchanged is not drawn. This is what makes an
;     event handler that changes one lamp cost one lamp, without the handler
;     having to say so.
;   - mppu_inval is for what polling cannot see: the static face. The body,
;     the bevels, the button glyphs and the option labels change only when the
;     kernel repaints us or the About panel comes and goes, and MPPI_BODY says
;     so.
;
; The LCD is the third case and it is neither: its four lines are composed
; every frame (a compose is arithmetic, ~0.4 ms; a draw is 90 glyph cells) and
; each line's HASH decides whether it reaches the screen. That is the Task
; Manager's tm_chunksum/tm_rowck discipline exactly, and for its reason: the
; content check comes first, so a covered readout costs a hash and not a
; redraw.
;
; THE RULE THAT KEEPS IT HONEST: a frame that is not drawn must not clear the
; bits it did not spend. mppu_refresh returns with the mask intact when the
; window is hidden, wholly covered or behind its own About panel, so the
; change is still owed and the next frame pays it. That is tm_rowok's rule -
; ask before you record - and getting it backwards loses the change forever
; rather than merely delaying it.
; =============================================================================

; mppu_inval - AX = a mask of MPPI_* bits to mark dirty
; preserves every register AND THE FLAGS - the two bytes of pushf/popf are
; there because this is a utility that will be called from the middle of
; anything, and `or` writes flags. wm_snap learned that one the expensive way.
mppu_inval:
    pushf
    or [mppu_dirty], ax
    popf
    ret

; -----------------------------------------------------------------------------
; mppu_sum - one word standing for the NUL-terminated string at SI
; out: AX = the hash; preserves everything else
;
; An explicit loop rather than lodsb for mppu_putn's reason one level along:
; lodsb is fine through DS, but the direction flag is not this package's to
; assume inside a callback, and the loop costs a byte.
; -----------------------------------------------------------------------------
mppu_sum:
    push bx
    push si
    xor bx, bx
    xor ah, ah
.next:
    mov al, [si]
    or al, al
    jz .done
    rol bx, 1
    add bx, ax
    inc si
    jmp .next
.done:
    mov ax, bx
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; mppu_chk - has the element whose check word is at BX changed to key AX?
; in:  AX = key, BX = the check word's address
; out: CF = 1 unchanged (skip the draw), CF = 0 changed (and recorded)
; clobbers: nothing else (flags)
;
; The never-zero rule is tm_elchk's, and it is not hypothetical there or here:
; mppu_ckclr writes 0, so an element whose state legitimately hashes to zero
; would read as "unchanged" on the very first paint and never be drawn at all.
; A blank LCD line hashes to zero, and a player that has not loaded anything
; yet has several.
; -----------------------------------------------------------------------------
mppu_chk:
    or ax, ax
    jnz .cmp
    inc ax
.cmp:
    cmp ax, [bx]
    je .same
    mov [bx], ax
    clc
    ret
.same:
    stc
    ret

; -----------------------------------------------------------------------------
; mppu_ckclr - forget every element's last-drawn state
;
; Called where pixels are destroyed by something other than this module's own
; drawing: a full face repaint, and a layout origin that moved under us. After
; it, every element redraws once and re-records.
; -----------------------------------------------------------------------------
mppu_ckclr:
    push ax
    push cx
    push di
    xor ax, ax
    mov [mppu_ckleds], ax
    mov [mppu_ckopts], ax
    mov [mppu_ckscrub], ax
    mov [mppu_ckvol], ax
    mov [mppu_ckmsg], ax
    mov di, mppu_cklcd              ; an explicit loop: stos writes through ES,
    mov cx, MPPU_LMAX               ; and ES is KERNEL_SEG in a callback
.l:
    mov [di], ax
    inc di
    inc di
    loop .l
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mppu_sig_leds - one word standing for all nine transport lamps
; mppu_sig_opts - one word standing for the option grid's lamps AND their
;                 greying, which mppu_opt_state answers together
; out: AX = the signature; preserves everything else
; -----------------------------------------------------------------------------
mppu_sig_leds:
    push bx
    push cx
    push si
    xor bx, bx
    xor si, si
.l:
    mov ax, si
    call mppu_btn_lit               ; AL = 0/1
    rol bx, 1
    add bx, ax
    inc si
    cmp si, MPPU_NBTN
    jb .l
    mov ax, bx
    pop si
    pop cx
    pop bx
    ret

mppu_sig_opts:
    push bx
    push cx
    push si
    xor bx, bx
    xor si, si
    cmp si, [mppu_on]
    jae .done                       ; a layout showing no options at all
.l:
    mov ax, si
    call mppu_opt_state             ; AL = on, AH = greyed - both are drawn,
    rol bx, 1                       ; so both belong in the signature
    add bx, ax
    inc si
    cmp si, [mppu_on]
    jb .l
.done:
    mov ax, bx
    pop si
    pop cx
    pop bx
    ret

; =============================================================================
; mppu_poll - set the dirty bits for everything whose state is a cheap pure
;             function of bytes we already own
; in:  gfx lock held; preserves all registers
;
; This is the half of the design that lets an event handler stay exactly as it
; was: a click that toggles Shuffle changes mppu_opt_state's answer, the
; signature moves, MPPI_OPTLED goes up, and one lamp is drawn. The handler
; never had to know which lamp it touched.
;
; The LCD is marked unconditionally because its check is per LINE and lives in
; mppu_lcd_emit - composing four lines is arithmetic, and the hash there is
; what stops the arithmetic becoming pixels.
;
; The visualiser is marked only while it is actually animating. SPEC.md 56.7's
; XT rule is the one exception and it is applied by the caller, not here.
; =============================================================================
mppu_poll:
    push ax
    push bx
    push dx
    cmp byte [mppu_ok], 0           ; the slider signatures are computed FROM
    je .out                         ; the layout words, so there is nothing to
                                    ; ask before the first mppu_layout
    mov ax, MPPI_LCD                ; always composed, per-line hash decides
    call mppu_inval

    call mppu_sig_leds
    mov bx, mppu_ckleds
    call mppu_chk
    jc .opts
    mov ax, MPPI_LEDS
    call mppu_inval
.opts:
    call mppu_sig_opts
    mov bx, mppu_ckopts
    call mppu_chk
    jc .scrub
    mov ax, MPPI_OPTLED
    call mppu_inval
.scrub:
    call mppu_scrub_thumb           ; AX = the handle's left x: the ONE number
    mov bx, mppu_ckscrub            ; the groove's picture depends on
    call mppu_chk
    jc .vol
    mov ax, MPPI_SCRUB
    call mppu_inval
.vol:
    call mppu_vol_thumb
    mov bx, mppu_ckvol
    call mppu_chk
    jc .msg
    mov ax, MPPI_VOL
    call mppu_inval
.msg:
    mov ax, [mppu_msgp]             ; a POINTER: the status line's strings are
    mov bx, mppu_ckmsg              ; static, so identity is equality here
    call mppu_chk
    jc .out
    mov ax, MPPI_STATUS
    call mppu_inval
.out:
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; mppu_paint - spend the dirty mask
; in:  gfx lock held, the clip region already arranged by the caller;
;      preserves all registers
;
; MPPI_BODY short-circuits the lot: the body fill wipes every element on top
; of it, so there is no sense in asking about them individually - and
; mppu_draw_all clears the check words on the way past, so each element
; records itself afresh on the next poll.
; =============================================================================
mppu_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [mppu_ok], 0
    je .out
    cmp word [mppu_dirty], 0
    je .out                         ; nothing changed - which is what a click
                                    ; that toggled nothing now costs
    call mppu_org_player            ; the shared helpers may be pointed at the
                                    ; Setup window right now (see mppu_org)
    ; The mask is re-read at every test rather than banked in a register: each
    ; painter below is documented as preserving all of them, and this way a
    ; future one that forgets cannot silently eat the rest of the frame.
    test word [mppu_dirty], MPPI_BODY
    jnz .all
    test word [mppu_dirty], MPPI_LCD
    jz .leds
    mov di, 1                       ; the text alone: no well, so no fill, and
    call mppu_draw_lcd              ; each line's hash decides for itself
.leds:
    test word [mppu_dirty], MPPI_LEDS
    jz .optled
    call mppu_draw_leds
.optled:
    test word [mppu_dirty], MPPI_OPTLED
    jz .scrub
    call mppu_draw_optleds
.scrub:
    test word [mppu_dirty], MPPI_SCRUB
    jz .vol
    call mppu_draw_scrub
.vol:
    test word [mppu_dirty], MPPI_VOL
    jz .viz
    call mppu_draw_vol
.viz:
    test word [mppu_dirty], MPPI_VIZ
    jz .status
    call mppu_draw_viz
.status:
    test word [mppu_dirty], MPPI_STATUS
    jz .spent
    call mppu_draw_status
.spent:
    mov word [mppu_dirty], 0
    jmp .out
.all:
    call mppu_draw_all              ; ...which clears the check words itself
    mov word [mppu_dirty], 0
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; mppu_draw_all - the whole face
; in:  gfx lock held; preserves all registers
; =============================================================================
mppu_draw_all:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [mppu_ok], 0
    je .out
    call mppu_ckclr                 ; FIRST: every pixel below is about to be
                                    ; destroyed, so no element's recorded state
                                    ; describes the screen any more - the LCD's
                                    ; line hashes above all, which the well is
                                    ; about to black out
    call mppu_org_player            ; the shared helpers may be pointed at the
                                    ; Setup window right now (see mppu_org)
    MPPU_PEN [mppu_body]            ; the player body...
    xor ax, ax
    xor bx, bx
    mov cx, [mppu_cw]
    dec cx
    mov dx, [mppu_ch]
    dec dx
    call mppu_fillr
    xor di, di                      ; ...and ModPlug Player's own frame: light
    call mppu_bevel                 ; above and left, dark below and right
    xor di, di                      ; the well as well as the text
    call mppu_draw_lcd
    call mppu_draw_buttons
    call mppu_draw_scrub
    call mppu_draw_vol
    call mppu_draw_viz
    call mppu_draw_opts
    call mppu_draw_status
    cmp byte [mpp_abon], 0
    je .out
    call mppu_draw_about            ; last, and over everything
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; mppu_frame - the worker's frame: advance the meters, then poll and spend
; in:  gfx lock held (the worker's) and the clip region already armed by it;
;      preserves all registers
;
; The visualiser is the one element on this face with no state worth
; comparing - it IS animation, and an animated pane is dirty by definition -
; so it is invalidated here rather than in mppu_poll. Everything else earns
; its pixels through a signature.
; =============================================================================
mppu_frame:
    push ax
    cmp byte [mpp_cpu0], 0          ; SPEC.md 56.7: on the machine XT mode
    je .viz                         ; exists for, the visualiser is not
    cmp byte [mpm_playing], 0       ; animated WHILE PLAYING - those cycles
    jne .poll                       ; are what the audio needs. Stopped, it
.viz:                               ; animates like anywhere else.
    call mppu_viz_step
    mov ax, MPPI_VIZ
    call mppu_inval
.poll:
    call mppu_poll
    call mppu_paint
    pop ax
    ret

; =============================================================================
; mppu_refresh - repaint the player's face from a context that is NOT its own
;                W_PAINT: another of this app's windows may be on top of it
; in:  gfx lock held; preserves all registers
;
; This is the difference between "repaint me" and "repaint me over whatever is
; in front of me". The kernel orders a W_PAINT for us; a menu command, a click
; in the Setup window or an option toggle does not, so the region has to be
; armed here exactly as the worker's own frame arms it (SPEC.md 11.3) - and
; cleared again, because the lock is already held and gfx_unlock (which is
; what normally clears it) is somebody else's call.
;
; Drawing under that region is safe because every text element on this face is
; an OSAPI_FONT_RUN and every other element is a solid fill: fills clip per
; pixel, and a run decides erase-and-letter together per cell, so there is no
; pair left that can disagree.
;
; IT NO LONGER REPAINTS THE FACE (SPEC.md 56.12). It polls and spends the
; dirty mask, exactly as the worker's frame does, so the ~14 handlers that
; call it keep their one unconditional `call mppu_refresh` and get a redraw
; scaled to what they actually changed - a lamp for a lamp, a slider for a
; slider, and nothing at all for a click that toggled nothing. A handler that
; changes something polling CANNOT see (the static face; the About panel) says
; so with mppu_inval first, and there are three of those.
;
; It draws SYNCHRONOUSLY rather than leaving the mask for the worker, which is
; the one place this departs from the Windows deferred-WM_PAINT model it is
; otherwise copying. The worker is HIRED, and mpp_hire's refusal is transient
; and retried; a handler that only marked would draw nothing at all on a
; machine where the hire has not succeeded yet, and there is no second timer
; here to notice.
;
; A hidden or wholly covered window returns with the mask INTACT. The change
; is still owed, and the next frame - or the W_PAINT that uncovering
; generates - pays it.
; =============================================================================
mppu_refresh:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [mpp_win]
    call OSAPI_WM_GEOM              ; CF=1: hidden - there is nothing to draw
    jc .out
    mov bx, [mpp_win]
    call OSAPI_WM_CLIP_SET          ; CF=1: wholly covered - skip the frame
    jc .out
    call mppu_layout                ; the origin may have moved since the last
                                    ; paint - and mppu_layout invalidates the
                                    ; whole face when it has
    call mppu_poll
    call mppu_paint
    call OSAPI_WM_CLIP_CLEAR
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; mppu_hit - which control is under a click
; in:  CX = x, DX = y (ABSOLUTE screen), gfx lock held
; out: CF=1 nothing; else AL = a control id, and BX = its value for the
;      controls that have one (the two sliders and the VU rows)
; preserves CX DX SI DI
;
; Geometry lives in ONE place: this routine and the drawing above read the
; same layout words, so a control cannot be drawn somewhere it cannot be
; clicked. That is the fm_hit rule, and it is why the layout is data.
;
; The click is banked into [mppu_hx]/[mppu_hy] on entry and read from there
; throughout - the tests below need x and y in different registers at
; different points, and a stack dance around that is how a hit test starts
; disagreeing with the picture.
; =============================================================================
mppu_hit:
    push cx
    push dx
    push si
    push di
    cmp byte [mppu_ok], 0
    je .miss
    sub cx, [mppu_cx0]              ; content-relative from here down
    js .miss
    sub dx, [mppu_cy0]
    js .miss
    mov [mppu_hx], cx
    mov [mppu_hy], dx

    ; --- the transport row --------------------------------------------------
    mov ax, [mppu_hy]
    sub ax, [mppu_by]
    js .scrub
    cmp ax, [mppu_bh]
    jae .scrub
    mov ax, [mppu_hx]
    sub ax, MPPU_BX0
    js .vol
    xor dx, dx
    mov si, MPPU_BPITCH
    div si                          ; AX = the button, DX = the offset into it
    cmp dx, MPPU_BW
    jae .vol                        ; landed in the gap between two buttons
    cmp ax, MPPU_NBTN
    jae .vol
    jmp .out

    ; --- the volume slider, which shares that band --------------------------
.vol:
    mov ax, [mppu_hx]
    cmp ax, [mppu_vx1]
    jb .miss
    cmp ax, [mppu_vx2]
    ja .miss
    mov ax, [mppu_hy]
    cmp ax, [mppu_vy1]
    jb .miss
    cmp ax, [mppu_vy2]
    ja .miss
    mov ax, [mppu_hx]
    sub ax, [mppu_vx1]
    dec ax
    jle .vzero
    mov bx, 64
    mul bx
    mov bx, [mppu_vx2]
    sub bx, [mppu_vx1]
    sub bx, MPPU_THUMBW + 1
    jle .vzero
    div bx
    cmp ax, 64
    jbe .vok
    mov ax, 64
    jmp .vok
.vzero:
    xor ax, ax
.vok:
    mov bx, ax
    mov ax, MPPC_VOL
    jmp .out

    ; --- the time scrubber --------------------------------------------------
.scrub:
    mov ax, [mppu_hy]
    cmp ax, [mppu_sy1]
    jb .opts
    cmp ax, [mppu_sy2]
    ja .opts
    mov ax, [mppu_hx]
    cmp ax, [mppu_sx1]
    jb .miss
    cmp ax, [mppu_sx2]
    ja .miss
    mov bl, [mpm_songlen]
    xor bh, bh
    or bx, bx
    jz .miss                        ; nothing loaded: the bar is decoration
    mov ax, [mppu_hx]
    sub ax, [mppu_sx1]
    dec ax
    jle .szero
    mul bx                          ; pos = x * songlen / travel
    push bx
    mov bx, [mppu_sx2]
    sub bx, [mppu_sx1]
    sub bx, MPPU_THUMBW + 1
    jg .sdiv
    mov bx, 1
.sdiv:
    div bx
    pop bx
    cmp ax, bx
    jb .sok
    mov ax, bx
    dec ax                          ; the far end of the bar is the LAST
    jmp .sok                        ; position, not one past it
.szero:
    xor ax, ax
.sok:
    mov bx, ax
    mov ax, MPPC_SCRUB
    jmp .out

    ; --- the option grid ----------------------------------------------------
.opts:
    mov ax, [mppu_hx]
    sub ax, [mppu_ox1]
    js .viz
    xor dx, dx
    div word [mppu_opitchx]
    cmp dx, [mppu_ow]
    jae .viz                        ; the gap between two columns
    cmp ax, [mppu_ocols]
    jae .viz
    mov si, ax                      ; SI = the column
    mov ax, [mppu_hy]
    sub ax, [mppu_oy1]
    js .viz
    xor dx, dx
    div word [mppu_opitchy]
    cmp dx, [mppu_oh]
    jae .viz
    cmp ax, [mppu_orows]
    jae .viz
    mul word [mppu_ocols]
    add ax, si
    cmp ax, [mppu_on]
    jae .viz                        ; a grid cell past the last button shown
    add ax, MPPC_OPT0
    jmp .out

    ; --- a VU row -----------------------------------------------------------
.viz:
    cmp byte [mpp_viz], MPPV_VU
    jne .miss
    mov ax, [mppu_hx]
    cmp ax, [mppu_zx1]
    jb .miss
    cmp ax, [mppu_zx2]
    ja .miss
    mov ax, [mppu_hy]
    cmp ax, [mppu_zy1]
    jb .miss
    cmp ax, [mppu_zy2]
    ja .miss
    call mppu_vu_geom
    mov ax, [mppu_hy]
    sub ax, [mppu_zy1]
    dec ax
    js .miss
    xor dx, dx
    div word [mppu_vuh]
    cmp ax, MPPU_VUCH
    jae .miss
    mov bx, ax
    mov ax, MPPC_SCOPE
.out:
    clc
    pop di
    pop si
    pop dx
    pop cx
    ret
.miss:
    stc
    pop di
    pop si
    pop dx
    pop cx
    ret

; =============================================================================
; mppui bss (through modplug.asm's MPPB/MPPW/MPPBUF accumulator)
; =============================================================================

; --- the live layout: THIS BLOCK IS THE LAYOUT TABLES' SHAPE ------------------
; mppu_layout rep-movsw's one of the two tables above straight into here, so
; the order of these 30 words is a pinned interface with mppu_lay_full and
; mppu_lay_compact. Adding a rectangle means adding a word to all three, and
; the %if assertions at the tables catch the half of that edit that forgets.
    MPPW mppu_lx1                   ; the LCD panel
    MPPW mppu_ly1
    MPPW mppu_lx2
    MPPW mppu_ly2
    MPPW mppu_llines                ; ...text lines it has room for
    MPPW mppu_by                    ; the transport row: top, height
    MPPW mppu_bh
    MPPW mppu_vx1                   ; the volume slider
    MPPW mppu_vy1
    MPPW mppu_vx2
    MPPW mppu_vy2
    MPPW mppu_sx1                   ; the time scrubber
    MPPW mppu_sy1
    MPPW mppu_sx2
    MPPW mppu_sy2
    MPPW mppu_zx1                   ; the visualiser pane
    MPPW mppu_zy1
    MPPW mppu_zx2
    MPPW mppu_zy2
    MPPW mppu_ox1                   ; the option grid's origin...
    MPPW mppu_oy1
    MPPW mppu_ow                    ; ...one button...
    MPPW mppu_oh
    MPPW mppu_opitchx               ; ...and the step to the next one
    MPPW mppu_opitchy
    MPPW mppu_ocols
    MPPW mppu_orows
    MPPW mppu_on                    ; buttons this layout shows
    MPPW mppu_sty                   ; the status line's text row
    MPPW mppu_bands                 ; spectrum bands

; --- the window, and the adapter ----------------------------------------------
    MPPW mppu_dx0                   ; the CURRENT drawing origin: whichever of
    MPPW mppu_dy0                   ; the three windows is being painted
    MPPW mppu_cx0                   ; the player's content origin, absolute
    MPPW mppu_cy0
    MPPW mppu_cw                    ; ...and its size
    MPPW mppu_ch
    MPPB mppu_ok                    ; a layout has been computed at least once
    MPPB mppu_ink                   ; the LCD's ink: CLGREEN, or CWHITE where
                                    ; green would reduce to black on black
    MPPB mppu_body                  ; ...and the chassis: CLGRAY, or CWHITE
                                    ; where grey would reduce to a screen

; --- drawing scratch -----------------------------------------------------------
    MPPW mppu_e1                    ; mppu_bevel's own banked rect - private,
    MPPW mppu_e2                    ; because mppu_draw_about banks a rect too
    MPPW mppu_e3                    ; and calls mppu_face, which bevels
    MPPW mppu_e4
    MPPW mppu_q1                    ; the general banked rect
    MPPW mppu_q2
    MPPW mppu_q3
    MPPW mppu_q4
    MPPW mppu_tx                    ; mppu_tri / mppu_tri_up
    MPPW mppu_ty
    MPPB mppu_tdir
    MPPW mppu_gx                    ; the glyph centre a transport button's
    MPPW mppu_gy                    ; symbol is built around
    MPPW mppu_lrow                  ; the LCD row being emitted
    MPPBUF mppu_line, 64            ; ...and the line being built for it
    MPPW mppu_msgp                  ; the status line's string, 0 = the hint
    MPPW mppu_bpitch                ; one spectrum bar's pitch
    MPPW mppu_zmid                  ; the oscilloscope's centre line...
    MPPW mppu_zamp                  ; ...and the half-height it swings over
    MPPW mppu_vuh                   ; one VU row's height
    MPPW mppu_hx                    ; mppu_hit's banked click
    MPPW mppu_hy
    MPPB mppu_lvl                   ; mppu_viz_step's banked channel level
    MPPB mppu_qdis                  ; the option label's greyed answer, banked
                                    ; between mppu_opt_state and the run

; --- the invalidation mask and the check words (SPEC.md 56.12) ----------------
; mppu_dirty is the update region; the rest are one element's last-drawn state
; each, all of them zeroed together by mppu_ckclr, and all of them read and
; written only through mppu_chk - which is where the never-zero rule lives.
    MPPW mppu_dirty                 ; MPPI_* bits owed a draw
    MPPW mppu_ckleds                ; the nine transport lamps, as one word
    MPPW mppu_ckopts                ; the option grid's lamps AND their greying
    MPPW mppu_ckscrub               ; the scrubber handle's x
    MPPW mppu_ckvol                 ; the volume handle's x
    MPPW mppu_ckmsg                 ; the status line's string POINTER
    MPPBUF mppu_cklcd, MPPU_LMAX * 2    ; one hash per LCD text line

; --- the elapsed-time clock ----------------------------------------------------
    MPPW mppu_frames                ; worker wakes since the last whole second
    MPPW mppu_secs

; --- the meters ----------------------------------------------------------------
; mppu_peak MUST follow mppu_band and mppu_vupk MUST follow mppu_vu: mppu_viz_reset
; clears each pair in one rep stosb, which is the only place that adjacency is
; load-bearing and the only place it would break.
    MPPBUF mppu_band, MPPU_MAXBAND
    MPPBUF mppu_peak, MPPU_MAXBAND
    MPPBUF mppu_vu, MPPU_VUCH
    MPPBUF mppu_vupk, MPPU_VUCH
    MPPBUF mppu_scope, MPPU_SCOPEN
