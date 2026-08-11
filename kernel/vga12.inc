; =============================================================================
; os8088 - vga12.inc
;
; VGA mode 12h (640x480x16, planar) primitives, region save/restore, and the
; global drawing mutex. Everything here programs the Graphics Controller
; (index 0x3CE / data 0x3CF) and Sequencer (0x3C4/0x3C5) directly.
;
; Conventions (SPEC.md 1, 5, 7):
;   - Solid fills use Set/Reset (GC0 = color, GC1 = 0Fh) with the Bit Mask
;     (GC8) for partial edge bytes; a read of the target byte loads the
;     latches before every masked write.
;   - XOR ops use ALU function = XOR (GC3 = 18h) with Set/Reset color 0Fh,
;     and EVERY target byte is read immediately before it is written -
;     including full interior bytes - because the ALU XORs against the latch
;     contents. rep stosb is only used for the interior of replace-mode
;     fills, where the latches are irrelevant.
;   - Every routine clips to 0..639 / 0..479 and leaves the GC/Sequencer in
;     the default state on return: Set/Reset enable = 0, Bit Mask = FF,
;     function = replace, rotate 0, Map Mask = 0Fh, Read Map = 0, mode 0.
;   - Drawing routines run with the gfx lock held (SPEC.md 1.6), so the
;     module-private scratch variables below need no interrupt guard: the
;     mouse ISR never draws while the lock is set.
; =============================================================================

VGA_GC      equ 0x3CE           ; Graphics Controller index port (data +1)
VGA_SEQ     equ 0x3C4           ; Sequencer index port (data +1)

gfx_color       db CBLACK       ; current drawing color for every solid op
gfx_dis         db 0            ; 1 = what is being drawn is a DISABLED
                                ; control (SPEC.md 47). It is a FLAG and not
                                ; a colour because the renderer has to do
                                ; something a colour cannot express: on a
                                ; mono adapter font_ink masks the glyph to a
                                ; checkerboard, which is the only way a
                                ; disabled STRING is distinguishable from a
                                ; live one at 1bpp. Keying that off CDGRAY
                                ; instead would catch every other use of the
                                ; colour - Minesweeper's digit 8 came out
                                ; looking like a menu item you cannot pick -
                                ; and would make "never use this colour for
                                ; anything else" a rule the tree has to keep
                                ; by hand. Set it through gfx_pen_* and it
                                ; cannot disagree with gfx_color.
                                ; Initialized data, NOT .bss (nothing zeroes
                                ; .bss), and gfx_unlock clears it for the
                                ; reason it clears the clip region: it is
                                ; valid for exactly one lock hold.
gfx_pat         dw 0            ; gfx_fill_pat: near ptr to 8 row bytes,
                                ; set by the caller before every call
gfx_lock_flag   db 0            ; drawing mutex (SPEC.md 7); 0 = free.
                                ; Initialized data, NOT .bss: nothing zeroes
                                ; .bss at boot, and this byte must start clear.
gfx_lock_own    db 0xFF         ; ...and WHICH task holds it ([sch_cur] at the
                                ; take, 0xFF = nobody). toast_now's whole test
                                ; (SPEC.md 60.4): the flag alone says somebody
                                ; holds it, never that the CALLER does.

; -----------------------------------------------------------------------------
; GFXCLIP - the clip hook, at a public rect entry (SPEC.md 11.3)
;
; Placed at the PUBLIC entry, above the [bb_on] dispatch, so one hook covers
; the VRAM path, the back-buffer path, VGA and both mono adapters at once -
; on a 1bpp adapter the software renderer IS the direct path (SPEC.md 39.5),
; and a hook below the dispatch would work on VGA and silently do nothing on
; Hercules. Same reasoning that places bb_mono_chk where it is.
;
; Disarmed (the whole repaint path) it costs one compare and a taken branch;
; nothing else changes. Armed, gfx_clip_run re-enters the raw body once per
; visible fragment.
; -----------------------------------------------------------------------------
%macro GFXCLIP 1
    cmp word [wm_clip_n], 0     ; clipped background draw? (SPEC.md 11.3)
    jne %%clipped
    call cur_unlazy             ; UNCLIPPED: this may write any pixel on the
    jmp %1                      ; screen, so a deferred hide is owed (7.1.4)
%%clipped:
    push di
    mov di, %1
    call gfx_clip_run
    pop di
    ret
%endmacro

; -----------------------------------------------------------------------------
; GFXDISP - the display hook, immediately BELOW a rect entry's GFXCLIP
;           (SPEC.md 39.14.1)
;
;   gfx_fill:        GFXCLIP gfx_fill_d       ; clip, in VIRTUAL space
;   gfx_fill_d:      GFXDISP gfx_fill_raw     ; split by display, then translate
;   gfx_fill_raw:    ...
;
; BELOW GFXCLIP AND NOT ABOVE IT, which is the correctness argument rather
; than a preference: wm_clip_tab holds VIRTUAL rects, so the clip has to be
; applied before anything is translated. Above, gfx_clip_run would be handed
; display-local coordinates to compare against virtual fragments - which is
; right on display 0, where the two are the same numbers, and silently wrong
; everywhere else. The dual-display plan said "above"; it was written before
; the clip's coordinate space had been thought about.
;
; ONE DISPLAY COSTS A COMPARE AND A TAKEN BRANCH, then falls straight through
; into the body - the bargain [bb_on] and [wm_clip_n] already make. On
; kern_small it expands to nothing at all.
;
; [gfx_dnest] is what lets this compose with 39.14.2's whole-shape hooks: a
; font_run that has already translated falls back to gfx_fill + font_str, and
; a translated caller reaching a hooked callee would translate twice.
; -----------------------------------------------------------------------------
%macro GFXDISP 1
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe %%one                   ; one display: the rect is already local
    cmp byte [gfx_dnest], 0
    jne %%one                   ; ...or an outer hook already translated it
    push di
    mov di, %1
    call gfx_disp_run
    pop di
    ret
%%one:
%endif
%endmacro

%ifdef KERN_BIG
; .text with real initialisers: -f bin zeroes nothing, and every drawing call
; in the machine reads these - including the first, before anything has run.
gfx_dnest   db 0                ; whole-shape hooks in effect (SPEC.md 39.14.2)
gfx_dr      times 4 dw 0        ; gfx_disp_run's banked virtual rect
gfx_dfn     dw 0                ; ...and the raw body it is re-entering

; -----------------------------------------------------------------------------
; gfx_disp_run - internal: run a rect primitive once per DISPLAY it touches
; in:       AX = x1, BX = y1, CX = x2, DX = y2 in VIRTUAL coordinates,
;           DI = near ptr to the raw body (the entry below that primitive's
;           GFXDISP); more than one display exists and no hook is nested
; out:      -
; clobbers: flags
;
; gfx_clip_run's shape, with a GEOMETRY attached to each rect instead of only
; a rect: intersect with the display, translate the survivor into the
; display's own coordinates, activate it, draw. A fragment landing in no
; display is drawn by nobody and there is no test for it anywhere - which is
; the whole reason the DEAD ZONE (SPEC.md 39.2.1) costs nothing.
;
; The state is module scratch for gfx_clip_run's reason: the raw bodies
; preserve everything, there is nothing left to spare, and this only ever runs
; with the gfx lock held.
;
; IT DOES NOT RESTORE THE DISPLAY (SPEC.md 39.14.3): the last one drawn on
; stays current until gfx_unlock, so a window's repaint pays ONE swap and not
; two per primitive. That is safe because the cursor brackets itself (39.15.2)
; - cur_unlazy runs at the top of GFXCLIP, above all of this, and would
; otherwise erase the arrow through whatever display happened to be live.
; -----------------------------------------------------------------------------
gfx_disp_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [gfx_dr+0], ax
    mov [gfx_dr+2], bx
    mov [gfx_dr+4], cx
    mov [gfx_dr+6], dx
    mov [gfx_dfn], di
    xor si, si                  ; SI = the display being drawn
.disp:
    mov ax, si
    call vid_ctx_ptr            ; DI = its record; AX preserved
    mov ax, [di+VID_CTX_VX]     ; x1 = max(rect's, this display's)
    cmp ax, [gfx_dr+0]
    jge .x1
    mov ax, [gfx_dr+0]
.x1:
    mov cx, [di+VID_CTX_VX]     ; x2 = min(rect's, its last column)
    add cx, [di+VID_CTX_CW]
    dec cx
    cmp cx, [gfx_dr+4]
    jle .x2
    mov cx, [gfx_dr+4]
.x2:
    cmp ax, cx
    jg .next                    ; no columns here - and if no display claims
                                ; the fragment, nobody draws it (39.2.1)
    mov bx, [di+VID_CTX_VY]
    cmp bx, [gfx_dr+2]
    jge .y1
    mov bx, [gfx_dr+2]
.y1:
    mov dx, [di+VID_CTX_VY]
    add dx, [di+VID_CTX_CH]
    dec dx
    cmp dx, [gfx_dr+6]
    jle .y2
    mov dx, [gfx_dr+6]
.y2:
    cmp bx, dx
    jg .next
    push si                     ; ...into the display's own coordinates, while
    mov si, [di+VID_CTX_VX]     ; the record is still in hand: vid_ctx_act is
    sub ax, si                  ; about to move the live words under us
    sub cx, si
    mov si, [di+VID_CTX_VY]
    sub bx, si
    sub dx, si
    pop si
    push ax                     ; AL is the display's, x1 is the rect's
    mov ax, si
    call vid_ctx_act
    pop ax
    call word [gfx_dfn]         ; the body - it preserves every register
.next:
    inc si
    mov al, [vid_ndisp]
    mov ah, 0
    cmp si, ax
    jb .disp                    ; ...and the LAST display drawn on stays
    pop di                      ; current; gfx_unlock is what puts 0 back
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_disp_enter / gfx_disp_leave - the WHOLE-SHAPE hook (SPEC.md 39.14.2)
;
; in:       AX = x, BX = y - the shape's ANCHOR, in virtual coordinates
; out:      that display is current; AX/BX are its own coordinates;
;           [vid_ox]/[vid_oy] is the origin subtracted, for a caller with more
;           coordinates of its own to move
; clobbers: flags (the GFXDLEAVE macro banks them, because a leave sits at the
;           end of routines whose answer is in CF).
;
; For font_char, font_run, ico_core, gfx_scroll, gfx_save/restore and
; gfx_line, none of which can be cut into rect fragments: they clip per whole
; cell, per whole shape or per pixel. So the shape goes ENTIRELY onto the
; display holding its anchor and is cut at that display's edge exactly as it
; is cut at a screen edge today - 39.14.2's decision, and 39.14.3's rule for
; the cursor applied to everything else with the same shape.
;
; THE NESTING COUNT IS WHAT MAKES IT COMPOSE. font_run's fallback path is
; gfx_fill + font_str and gfx_line's axis-aligned cases are gfx_hline/vline:
; a translated caller reaching a hooked callee would translate twice. It is a
; COUNT and not a flag because gfx_disp_leave has to know which call owes the
; restore.
; -----------------------------------------------------------------------------
gfx_disp_enter:
    inc byte [gfx_dnest]
    cmp byte [gfx_dnest], 1
    jne .out                    ; nested: an outer hook owns the translation
    push ax                     ; vid_disp_of ANSWERS in AL, so x is banked
    call vid_disp_of            ; AL = the display holding (AX, BX); it cannot
    call vid_ctx_act            ; fail - the dead zone answers with the primary
    pop ax
    sub ax, [vid_ox]
    sub bx, [vid_oy]
.out:
    ret

gfx_disp_enter_cd:
    push ax                     ; the same, with the anchor in CX/DX - and
    push bx                     ; nested it writes back what it was handed,
    mov ax, cx                  ; because gfx_disp_enter left AX/BX alone
    mov bx, dx
    call gfx_disp_enter
    mov cx, ax
    mov dx, bx
    pop bx
    pop ax
    ret

%endif

; GFXDENTER / GFXDLEAVE - the two above, with their one-display test inlined
; so a single-card machine pays a compare rather than a near call. font_char
; is the reason: SPEC.md 6.1.1 prices a glyph cell at ~1ms on the target and
; a call/ret at ~11us, which is 1.2% of every character in the system for a
; branch that is never taken on any machine but one.
%macro GFXDENTER 0
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe %%one
    call gfx_disp_enter
%%one:
%endif
%endmacro

; ...and the CX/DX spelling, for font_char and font_run_x - where AL is the
; character and AX cannot be touched - and for the icon wrappers.
%macro GFXDENTERCD 0
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe %%one
    call gfx_disp_enter_cd
%%one:
%endif
%endmacro

; GFXDORG - move two more of the caller's coordinates into the display the
; enter above chose. For a routine holding a RECT rather than a point: the
; enter translated the anchor, this translates the far corner.
;
; Gated on the nest count being exactly 1, not on it being non-zero: nested,
; an OUTER hook did the translation and [vid_ox] is that hook's origin, so
; subtracting it again would move the shape twice.
%macro GFXDORG 2
%ifdef KERN_BIG
    cmp byte [gfx_dnest], 1
    jne %%skip
    sub %1, [vid_ox]
    sub %2, [vid_oy]
%%skip:
%endif
%endmacro

; ...and GFXDLEAVE is only the count coming down, because NOTHING RESTORES
; THE DISPLAY any more (SPEC.md 39.14.3): the last one drawn on stays current
; until gfx_unlock, so a burst of primitives in one place pays one swap
; instead of one per call. It banks the FLAGS, because more than one of these
; routines answers in CF and a leave is the last thing before its `ret`, and
; `dec` sets them.
%macro GFXDLEAVE 0
%ifdef KERN_BIG
    pushf
    cmp byte [vid_ndisp], 1
    jbe %%one
    dec byte [gfx_dnest]
%%one:
    popf
%endif
%endmacro

; -----------------------------------------------------------------------------
; gfx_clip_run - internal: run a rect primitive once per visible fragment
; in:       AX = x1, BX = y1, CX = x2, DX = y2, DI = near ptr to the raw body
;           (the entry just below that primitive's GFXCLIP); clip is armed
; out:      -
; clobbers: flags
;
; The loop's state lives in module scratch rather than registers because the
; raw bodies preserve everything and there is nothing left to spare anyway.
; That is safe for the same reason vga12's rect scratch is: this only ever
; runs with the gfx lock held, and no clipped primitive calls another.
; -----------------------------------------------------------------------------
gfx_clip_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
%ifdef KERN_BIG
    cmp byte [gfx_dnest], 0     ; SPEC.md 39.14.4: a nested call from inside a
    je .virt                    ; whole-shape hook arrives in DISPLAY
    add ax, [vid_ox]            ; coordinates, and wm_clip_tab is virtual. The
    add cx, [vid_ox]            ; fragments go back below, one subtraction
    add bx, [vid_oy]            ; each, because the raw body wants them local
    add dx, [vid_oy]
.virt:
%endif
    mov [gfx_cl_x1], ax
    mov [gfx_cl_y1], bx
    mov [gfx_cl_x2], cx
    mov [gfx_cl_y2], dx
    mov [gfx_cl_fn], di
    mov si, wm_clip_tab
    mov di, [wm_clip_n]
.frag:
    mov ax, [si+WCR_X1]         ; AX/CX = the fragment's x span, clipped to
    cmp ax, [gfx_cl_x1]         ; the primitive's
    jge .x1
    mov ax, [gfx_cl_x1]
.x1:
    mov cx, [si+WCR_X2]
    cmp cx, [gfx_cl_x2]
    jle .x2
    mov cx, [gfx_cl_x2]
.x2:
    cmp ax, cx
    jg .next
    mov bx, [si+WCR_Y1]
    cmp bx, [gfx_cl_y1]
    jge .y1
    mov bx, [gfx_cl_y1]
.y1:
    mov dx, [si+WCR_Y2]
    cmp dx, [gfx_cl_y2]
    jle .y2
    mov dx, [gfx_cl_y2]
.y2:
    cmp bx, dx
    jg .next
%ifdef KERN_BIG
    cmp byte [gfx_dnest], 0     ; ...and the fragment back into the display the
    je .lv                      ; body is going to draw on. The loop re-reads
    sub ax, [vid_ox]            ; gfx_cl_* below, which stayed virtual, so
    sub cx, [vid_ox]            ; there is nothing to undo
    sub bx, [vid_oy]
    sub dx, [vid_oy]
.lv:
%endif
    call word [gfx_cl_fn]
.next:
    add si, WCR_SZ
    dec di
    jnz .frag
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_set_color - internal: arm Set/Reset with [gfx_color] on all planes
; in:       -
; out:      GC0 = [gfx_color], GC1 = 0Fh
; clobbers: flags only (all registers preserved)
; -----------------------------------------------------------------------------
vga_set_color:
    push ax
    push dx
    mov dx, VGA_GC
    mov ah, [gfx_color]
    xor al, al                  ; GC0: Set/Reset value
    out dx, ax
    mov ax, 0x0F01              ; GC1: enable Set/Reset on all planes
    out dx, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_set_xor - internal: arm Set/Reset 0Fh with ALU function = XOR
; in:       -
; out:      GC0 = 0Fh, GC1 = 0Fh, GC3 = 18h
; clobbers: flags only
; -----------------------------------------------------------------------------
vga_set_xor:
    push ax
    push dx
    mov dx, VGA_GC
    mov ax, 0x0F00              ; GC0: Set/Reset value = 0Fh
    out dx, ax
    mov ax, 0x0F01              ; GC1: enable on all planes
    out dx, ax
    mov ax, 0x1803              ; GC3: rotate 0, function = XOR
    out dx, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_gc_reset - internal: restore the GC default state (SPEC.md 1.7)
; in:       -
; out:      GC1 = 0, GC3 = 0, GC8 = FF
; clobbers: flags only
; -----------------------------------------------------------------------------
vga_gc_reset:
    push ax
    push dx
    mov dx, VGA_GC
    mov ax, 0x0001              ; GC1: Set/Reset enable off
    out dx, ax
    mov ax, 0x0003              ; GC3: rotate 0, function = replace
    out dx, ax
    mov ax, 0xFF08              ; GC8: Bit Mask = all bits
    out dx, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; The two edge masks, by x & 7. They were computed with `shr dl,cl` / `shl
; dl,cl`, which on an 8088 is 8 clocks plus 4 PER BIT and costs a push/pop of
; CX to free the count register - about a fifth of vga_rect_setup for two
; bytes that a table answers outright (PERFORMANCE.md Part 2: the per-call
; floor is CPU-side setup, and this is setup).
; -----------------------------------------------------------------------------
vga_lmtab:  db 0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01  ; FF >> (x1 & 7)
vga_rmtab:  db 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF  ; FF << 7-(x2&7)

; -----------------------------------------------------------------------------
; vga_rect_setup - internal: clip a rect and precompute its geometry
; in:       AX = x1, BX = y1, CX = x2, DX = y2  (x1<=x2, y1<=y2)
; out:      CF = 1 if the clipped rect is empty (nothing else valid);
;           else CF = 0 and vga_y1/vga_rows/vga_off/vga_span/vga_lmsk/
;           vga_rmsk hold clipped top row, row count, VRAM offset of the
;           top-left byte, right byte - left byte, and the edge bit masks
;           (combined into vga_lmsk when the rect spans a single byte).
; clobbers: AX, BX, CX, DX, flags
; -----------------------------------------------------------------------------
vga_rect_setup:
    or bx, bx                   ; y1 < 0 -> 0
    jns .y1ok
    xor bx, bx
.y1ok:
    cmp dx, [vid_ch]            ; y2 past the last row -> the last row
    jl .y2ok
    mov dx, [vid_chm1]
.y2ok:
    cmp dx, bx
    jl .empty
    or ax, ax                   ; x1 < 0 -> 0
    jns .x1ok
    xor ax, ax
.x1ok:
    cmp cx, [vid_cw]            ; x2 past the last column -> the last column
    jl .x2ok
    mov cx, [vid_cwm1]
.x2ok:
    cmp cx, ax
    jl .empty
    mov [vga_y1], bx            ; y1 is banked, so BX is the free register the
    sub dx, bx                  ; mask lookups below index through - which is
    inc dx                      ; what retires the two variable shifts and the
    mov [vga_rows], dx          ; three push/pops the old sequence needed
    mov bx, ax                  ; left mask = FF >> (x1 & 7)
    and bx, 7                   ; (BH = 0 falls out of the AND)
    mov dl, [bx + vga_lmtab]
    mov [vga_lmsk], dl
    mov bx, cx                  ; right mask = FF << (7 - (x2 & 7))
    and bx, 7
    mov dl, [bx + vga_rmtab]
    mov [vga_rmsk], dl
    mov dx, cx                  ; DX = x2; [vga_rows] already banked it
    mov cl, 3
    shr ax, cl                  ; ax = left byte  (x1 / 8)
    mov [vga_lcol], ax          ; kept for the bb_* dirty union (SPEC.md 32)
    shr dx, cl                  ; dx = right byte (x2 / 8)
    sub dx, ax
    mov [vga_span], dx          ; span = right byte - left byte
    or dx, dx
    jnz .offs
    mov dl, [vga_rmsk]          ; one-byte rect: combine the masks
    and [vga_lmsk], dl
.offs:
    mov ax, [vga_y1]            ; this one call re-bases every rect primitive
    call gfx_rowbase            ; in BOTH renderers - on VGA it is still just
    add ax, [vga_lcol]          ; y1 * 80 (SPEC.md 39.3)
    mov [vga_off], ax
    clc
    ret
.empty:
    stc
    ret

; -----------------------------------------------------------------------------
; gfx_pixel - plot one pixel in [gfx_color]
; in:       CX = x, DX = y
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_pixel:
    push ax
    push bx
    mov ax, cx                  ; a pixel is a 1x1 solid rect (CX/DX already
    mov bx, dx                  ; hold x2/y2); gfx_fill clips and back-
    call gfx_fill               ; buffer-dispatches itself
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_hline - horizontal line in [gfx_color], inclusive endpoints
; in:       AX = x1, BX = x2, DX = y  (x1 <= x2)
; out:      -
; clobbers: flags
;
; A 1-row solid rect: gfx_fill clips and back-buffer-dispatches itself.
; (The former dedicated VRAM loop was retired for the SPEC.md 32 code
; budget - gfx_fill produces identical pixels for one row.)
; -----------------------------------------------------------------------------
gfx_hline:
    push bx
    push cx
    mov cx, bx                  ; rect (x1,y)-(x2,y)
    mov bx, dx
    call gfx_fill
    pop cx
    pop bx
    ret


; =============================================================================
; SPEC.md 5.6.7 - the RESUMABLE walk
;
; gfx_line answers "draw this whole line". These two answer "draw the next N
; pixels of this line", and the difference is what lets a caller ERASE
; exactly what it drew.
;
; The problem they solve: a missile trail is DRAWN a couple of pixels a frame
; and ERASED as one long line, and Bresenham over the whole line does not
; visit the union of the per-frame segments - so 5.6.5 dilated the erase by a
; pixel either side and hoped. That is an approximation in BOTH directions:
; it can still miss (leaving the dashes 48's first note is about) and it
; erases pixels the trail never drew, out of whatever was underneath. It also
; costs three walks, measured at 500us a trail pixel on a 4.77MHz machine
; (PERFORMANCE.md Set 4) - the largest remaining item in that game.
;
; A caller that carries the walk's own state has neither problem. The draw
; advances it; the erase replays it from a saved copy. Same recurrence, same
; pixels, one walk, and the caller may stop and resume anywhere - which is
; also what lets an erase be SPREAD over frames instead of landing whole.
;
; THE WALK RUNS IN THE CALLER'S DIRECTION. gfx_line normalises downward so
; its pixel set is a property of the endpoint PAIR (5.6.2); this cannot,
; because the caller is drawing outward from a fixed point and needs step 1
; to be next to it. Order therefore matters here and gfx_prevrow exists for
; it. Two calls with the same endpoints agree by construction, which is all
; a draw/erase pair needs.
; =============================================================================

; -----------------------------------------------------------------------------
; gfx_linit - set a walk state up for the line (x1,y1)-(x2,y2)
; in:  AX = x1, BX = y1, CX = x2, DX = y2; ES:DI = a GLS_SZ-byte block
; out: the block; preserves every register
; -----------------------------------------------------------------------------
gfx_linit:
    push ax
    push bx
    push cx
    push dx
    mov [es:di + GLS_X], ax
    mov [es:di + GLS_Y], bx
    mov word [es:di + GLS_SX], 1
    sub cx, ax
    jns .dxok
    neg cx
    mov word [es:di + GLS_SX], -1
.dxok:
    mov [es:di + GLS_DX], cx
    mov word [es:di + GLS_SY], 1
    sub dx, bx
    jns .dyok
    neg dx
    mov word [es:di + GLS_SY], -1
.dyok:
    mov [es:di + GLS_DY], dx
    sub cx, dx                  ; err = dx - dy, exactly gfx_line's
    mov [es:di + GLS_ERR], cx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_lstep - draw the next CX pixels of the walk in [gfx_color]
; in:  ES:DI = the block, CX = pixels (0 legal, does nothing)
; out: the block advanced; preserves every register; caller holds the gfx lock
;
; The block is STAGED into kernel scratch and written back, rather than being
; addressed through ES throughout - 20.8 rule 3, and here it is forced as
; well as preferred: the mono walk needs ES for the framebuffer.
;
; Each call plots the CURRENT point first and advances after, so N then M
; pixels are exactly the N+M one call would have drawn. That property is the
; whole contract.
; -----------------------------------------------------------------------------
gfx_lstep:
    call cur_unlazy             ; ...and so does the SCALAR walk. gfx_lstepv had
                                ; this and the twin below it did not, over the
                                ; same gfx_ls_one body: on mono the walk
                                ; read-modify-writes the framebuffer directly,
                                ; so with the region disarmed there is nothing
                                ; else in the path that could spend gfx_lock's
                                ; promise - the line draws over an arrow still
                                ; on screen and gfx_unlock then paints the
                                ; stale save-under back over it (SPEC.md 7.1.4)
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    or cx, cx
    jz .out
    call gfx_ls_ink
    call gfx_ls_one
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

; -----------------------------------------------------------------------------
; gfx_lstepv - step MANY walks in one call (SPEC.md 5.6.8)
; in:  ES:DI = an array of CX descriptors, each `dw block_offset, pixels`,
;      the blocks in ES too; CX = 0 legal, does nothing
; out: every block advanced; preserves every register; caller holds the lock
;
; Identical pixels to CX separate gfx_lstep calls - it IS those calls, with
; the arriving done once. That is the whole point: a walk step draws two or
; three pixels and PERFORMANCE.md Part 2 prices the arriving at ~756us, so a
; caller with eight live lines was paying eight floors a frame to move about
; twenty pixels. A descriptor with a zero count is legal and is skipped, so a
; caller may build the array once and vary what it asks for.
; -----------------------------------------------------------------------------
gfx_lstepv:
    call cur_unlazy             ; ...and so does the resumable walk
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    jcxz .out
    call gfx_ls_ink             ; the pen is the caller's, and one for all
    mov si, di                  ; SI = the descriptor being read
.each:
    mov di, [es:si]             ; the block...
    mov ax, [es:si+2]           ; ...and how far to take it
    or ax, ax
    jz .next
    push cx
    push si
    mov cx, ax
    call gfx_ls_one
    pop si
    pop cx
.next:
    add si, 4
    loop .each
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

; gfx_ls_ink - resolve [gfx_color] to the 1bpp class once (00 / FF / 01)
; clobbers AX
gfx_ls_ink:
    mov al, [gfx_color]
    call gfx_ink
    mov [gfx_ln_ink], al
    ret

; gfx_ls_one - stage the block at ES:DI in, walk CX pixels, stage it back
; in:  ES:DI = the caller's block, CX = pixels (non-zero), [gfx_ln_ink] set
; clobbers AX/BX/DX/SI/DI/BP/ES; CX is spent
gfx_ls_one:
    mov [gfx_ls_cseg], es       ; --- stage the caller's block in. Its SEGMENT
    mov [gfx_ls_coff], di       ; has to be remembered: the walk needs ES for
    push cx                     ; the framebuffer, so it cannot stay there
    push si
    push ds
    pop ax                      ; AX = KERNEL_SEG
    mov si, di
    push es
    pop ds                      ; DS:SI = the caller's block
    mov es, ax
    mov di, gfx_ls_x            ; ES:DI = ours
    mov cx, GLS_SZ / 2
    cld
    rep movsw
    mov ds, ax                  ; DS = KERNEL_SEG again
    pop si
    pop cx

    cmp byte [bb_on], 0         ; 5.6.4's split: mono gets the per-pixel walk,
    je .slow                    ; VGA is the fast machine and gets gfx_pixel
    cmp byte [vid_planes], 1
    jne .slow
    call gfx_lstep_mono
    jmp short .back
.slow:
    call gfx_lstep_slow
.back:
    mov si, gfx_ls_x            ; --- and back out, to where it came from
    mov es, [gfx_ls_cseg]
    mov di, [gfx_ls_coff]
    mov cx, GLS_SZ / 2
    cld
    rep movsw
    ret

; gfx_lstep_slow - one gfx_pixel a step. VGA's planar path, and any adapter
; whose renderer is not the 1bpp one; CX = steps.
gfx_lstep_slow:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, cx                  ; the count moves out of CX: gfx_pixel takes
.px:                            ; its x THERE
    mov cx, [gfx_ls_x]
    mov dx, [gfx_ls_y]
    call gfx_pixel              ; clips to the region and the screen itself
    call gfx_ls_adv
    dec si
    jnz .px
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; gfx_ls_adv - one Bresenham step of the staged state. Clobbers AX only.
gfx_ls_adv:
    push ax
    mov ax, [gfx_ls_err]
    add ax, ax                  ; e2 = 2*err
    mov [gfx_ls_e2], ax
    mov ax, [gfx_ls_dy]
    neg ax
    cmp [gfx_ls_e2], ax         ; e2 > -dy: step x
    jle .noxs
    mov ax, [gfx_ls_err]
    sub ax, [gfx_ls_dy]
    mov [gfx_ls_err], ax
    mov ax, [gfx_ls_x]
    add ax, [gfx_ls_sx]
    mov [gfx_ls_x], ax
.noxs:
    mov ax, [gfx_ls_e2]
    cmp ax, [gfx_ls_dx]         ; e2 < dx: step y
    jge .noys
    mov ax, [gfx_ls_err]
    add ax, [gfx_ls_dx]
    mov [gfx_ls_err], ax
    mov ax, [gfx_ls_y]
    add ax, [gfx_ls_sy]
    mov [gfx_ls_y], ax
.noys:
    pop ax
    ret


; gfx_lstep_mono - the 1bpp per-pixel walk, CX steps. Registers: SI = x,
; DX = y, BP = err, DI = framebuffer offset, BL = the bit mask, ES = the
; framebuffer. dx/dy/sx/sy stay in memory, as they do in gfx_line_mono.
gfx_lstep_mono:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [vid_rseg]          ; [gfx_ln_ink] is the caller's - gfx_ls_ink
    mov si, [gfx_ls_x]
    mov dx, [gfx_ls_y]
    mov bp, [gfx_ls_err]
    call gfx_ls_box             ; the rect this walk starts inside - AFTER
    call gfx_ls_addr            ; SI/DX are loaded, because it reads them
.px:
    cmp si, [gfx_ls_bx1]        ; still in the resolved rect?
    jl .miss
    cmp si, [gfx_ls_bx2]
    jg .miss
    cmp dx, [gfx_ls_by1]
    jl .miss
    cmp dx, [gfx_ls_by2]
    jle .draw
.miss:
    call gfx_ls_box             ; ...no: find the one that holds it, if any
    cmp si, [gfx_ls_bx1]
    jl .step
    cmp si, [gfx_ls_bx2]
    jg .step
    cmp dx, [gfx_ls_by1]
    jl .step
    cmp dx, [gfx_ls_by2]
    jg .step
.draw:
    mov al, [gfx_ln_ink]
    cmp al, 1                   ; the dither class: lit where (x+y) is even
    jne .solid
    mov ax, si
    xor ax, dx
    and al, 1
    dec al
.solid:
    and al, bl
    mov ah, bl
    not ah
    and ah, [es:di]
    or  ah, al
    mov [es:di], ah
.step:
    mov ax, bp
    add ax, ax
    mov [gfx_ls_e2], ax
    mov ax, [gfx_ls_dy]
    neg ax
    cmp [gfx_ls_e2], ax         ; e2 > -dy: step x
    jle .noxs
    sub bp, [gfx_ls_dy]
    cmp word [gfx_ls_sx], 0
    jl .xleft
    inc si
    shr bl, 1
    jnz .noxs
    mov bl, 0x80
    inc di
    jmp short .noxs
.xleft:
    dec si
    shl bl, 1
    jnz .noxs
    mov bl, 0x01
    dec di
.noxs:
    mov ax, [gfx_ls_e2]
    cmp ax, [gfx_ls_dx]         ; e2 < dx: step y, EITHER WAY (5.6.7)
    jge .noys
    add bp, [gfx_ls_dx]
    cmp word [gfx_ls_sy], 0
    jl .yup
    inc dx
    call gfx_nextrow
    jmp short .noys
.yup:
    dec dx
    call gfx_prevrow
.noys:
    dec cx
    jnz .px
    mov [gfx_ls_x], si          ; hand the advanced state back
    mov [gfx_ls_y], dx
    mov [gfx_ls_err], bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; gfx_ls_addr - DI = the framebuffer byte for (SI,DX), BL = its bit
; clobbers AX/CX, and DI/BL are outputs
gfx_ls_addr:
    push cx                     ; CX is the caller's STEP COUNT
    push dx
    mov ax, dx
    call gfx_rowbase            ; clobbers AX/CX/DX, preserves BX
    mov di, ax
    mov ax, si
    mov cl, 3
    shr ax, cl
    add di, ax
    mov cx, si
    and cl, 7
    mov bl, 0x80
    shr bl, cl
    pop dx
    pop cx
    ret

; gfx_ls_box - the clip rect containing (SI,DX), into gfx_ls_bx1..by2. With
; the region disarmed that is the screen; with no rect containing the point
; it is an EMPTY box, so the caller skips the pixel and re-resolves at the
; next one. Preserves every register.
gfx_ls_box:
    push ax
    push bx
    push cx
    push di
    cmp word [wm_clip_n], 0
    jne .armed
    xor ax, ax
    mov [gfx_ls_bx1], ax
    mov [gfx_ls_by1], ax
    mov ax, [vid_cwm1]
    mov [gfx_ls_bx2], ax
    mov ax, [vid_chm1]
    mov [gfx_ls_by2], ax
    jmp short .out
.armed:
    mov bx, wm_clip_tab
    mov cx, [wm_clip_n]
.scan:
    mov ax, [bx + WCR_X1]
    cmp si, ax
    jl .next
    cmp si, [bx + WCR_X2]
    jg .next
    cmp dx, [bx + WCR_Y1]
    jl .next
    cmp dx, [bx + WCR_Y2]
    jg .next
    mov [gfx_ls_bx1], ax
    mov ax, [bx + WCR_Y1]
    mov [gfx_ls_by1], ax
    mov ax, [bx + WCR_X2]
    mov [gfx_ls_bx2], ax
    mov ax, [bx + WCR_Y2]
    mov [gfx_ls_by2], ax
    jmp short .out
.next:
    add bx, WCR_SZ
    loop .scan
    mov word [gfx_ls_bx1], 1    ; nothing holds it: an empty box, so every
    mov word [gfx_ls_bx2], 0    ; later pixel re-resolves rather than
    mov word [gfx_ls_by1], 1    ; inheriting a stale rect
    mov word [gfx_ls_by2], 0
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_line - an arbitrary-angle line in [gfx_color] (SPEC.md 5.6)
;
; in:       AX = x1, BX = y1, CX = x2, DX = y2, inclusive, absolute screen
;           coordinates. SI = 0 thin / 1 dilated (SPEC.md 5.6.5). Pen in
;           [gfx_color]; caller holds the gfx lock.
; out:      -
; clobbers: flags
;
; It does NOT replace the area primitives and defers to them: a fill writes
; eight pixels a store on 1bpp where a walk writes one, so a long axis-
; aligned run is gfx_fill's job whatever the angle of the argument (SPEC.md
; 5.6.1, crossover ~27px). The two compares below are what let a caller hand
; this routine any pair of points, degenerate ones included.
;
; The walk always runs DOWNWARD (SPEC.md 5.6.2). Bresenham is not symmetric
; under endpoint swap, so a line drawn as (A,B) and erased as (B,A) leaves a
; scatter behind; normalising makes the pixel set a property of the pair. It
; is also what lets the mono loop step rows with gfx_nextrow, which only goes
; down, instead of a gfx_rowbase per pixel.
; -----------------------------------------------------------------------------
gfx_line:
    call cur_unlazy             ; walks pixels, not clipped rects: 7.1.4
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp                     ; 5.6.6's three-column walk uses it as a plain
    push es                     ; register - never as an address base
    GFXDENTER                   ; the walk goes onto the display holding the
    GFXDORG cx, dx              ; FIRST endpoint and is cut at that display's
                                ; edge, exactly as it is cut at a screen edge
                                ; (SPEC.md 39.14.2). Above the axis-aligned
                                ; dispatch, so .hline/.vline reach gfx_fill
                                ; with LOCAL coordinates and the nesting count
                                ; stops it translating them again
    cmp bx, dx                  ; an axis-aligned pair belongs in the AREA
    je .hline                   ; primitive, whoever asked for a line - and
    cmp ax, cx                  ; it needs no dilation either, its segments
    je .vline                   ; being on the same lattice as the whole line
    cmp bx, dx                  ; SIGNED: y1 <= y2, or swap both ends
    jle .ord
    xchg ax, cx
    xchg bx, dx
.ord:
    mov [gfx_ln_x1], ax
    mov [gfx_ln_y1], bx
    mov [gfx_ln_x2], cx
    mov [gfx_ln_y2], dx
    sub dx, bx                  ; dy = y2 - y1, and cannot be negative now
    mov [gfx_ln_dy], dx
    mov word [gfx_ln_sx], 1
    sub cx, ax                  ; dx = |x2 - x1|, sx = its sign
    jns .dxok
    neg cx
    mov word [gfx_ln_sx], -1
.dxok:
    mov [gfx_ln_dx], cx
    sub cx, dx                  ; err = dx - dy
    mov [gfx_ln_err], cx

    mov word [gfx_ln_fdx], 0    ; SPEC.md 5.6.5: SI = 1 dilates by a pixel
    mov word [gfx_ln_fdy], 0    ; either side of the MINOR axis - three passes
    mov word [gfx_ln_np], 1     ; at the same dx/dy/err, so only the start
    mov byte [gfx_ln_wide], 0   ; point moves
    or si, si
    jz .passes
    mov word [gfx_ln_np], 3
    mov ax, [gfx_ln_dx]
    cmp ax, [gfx_ln_dy]
    jb .fatx
    mov word [gfx_ln_fdy], 1    ; shallow: the minor axis is y
    dec word [gfx_ln_y1]
    dec word [gfx_ln_y2]
    jmp short .passes
.fatx:
    mov word [gfx_ln_fdx], 1    ; steep: the minor axis is x
    dec word [gfx_ln_x1]
    dec word [gfx_ln_x2]

    ; SPEC.md 5.6.6: on 1bpp, a STEEP dilation is three horizontally adjacent
    ; pixels a step - the same row, usually the same BYTE - and the three
    ; passes share dx, dy and err, so pass k visits exactly (x_i + k-1, y_i).
    ; One walk with a three-bit mask is therefore the identical pixel set at a
    ; third of the Bresenham. Solid ink only: the dither class alternates with
    ; x, so the neighbours never carry the centre's bit.
    cmp byte [bb_on], 0         ; the same two tests gfx_line_raw makes
    je .passes
    cmp byte [vid_planes], 1
    jne .passes
    mov al, [gfx_color]
    call gfx_ink
    cmp al, 1
    je .passes
    mov byte [gfx_ln_wide], 1
    mov word [gfx_ln_np], 1
    inc word [gfx_ln_x1]        ; undo the shift: the mask spans both sides
    inc word [gfx_ln_x2]
.passes:
    cmp word [wm_clip_n], 0     ; SPEC.md 5.6.3: one pass per clip rect, the
    jne .clipped                ; gfx_clip_run idiom. Clipping the ENDPOINTS
    xor ax, ax                  ; would move the lattice and the erase would
    mov [gfx_ln_cx1], ax        ; then miss; disarmed, one pass at the screen
    mov [gfx_ln_cy1], ax        ; edge, which is also where this gets its
    mov ax, [vid_cwm1]          ; screen clipping
    mov [gfx_ln_cx2], ax
    mov ax, [vid_chm1]
    mov [gfx_ln_cy2], ax
    call gfx_line_raw
    jmp short .nextpass
.clipped:
    mov si, wm_clip_tab
    mov di, [wm_clip_n]
.frag:
    mov ax, [si+WCR_X1]         ; the rects are disjoint, so no pixel of the
    mov bx, [si+WCR_Y1]         ; line is ever written twice
    mov cx, [si+WCR_X2]
    mov dx, [si+WCR_Y2]
%ifdef KERN_BIG
    cmp byte [gfx_dnest], 0     ; the fragments are VIRTUAL and the endpoints
    je .fv                      ; went display-local at GFXDORG, so each
    sub ax, [vid_ox]            ; fragment follows them onto the display -
    sub cx, [vid_ox]            ; gfx_clip_run's own idiom, which this loop
    sub bx, [vid_oy]            ; replicated WITHOUT the translation: a local
    sub dx, [vid_oy]            ; line clipped against virtual rects meets
.fv:                            ; none of them on display 1 and vanishes
%endif
    mov [gfx_ln_cx1], ax
    mov [gfx_ln_cy1], bx
    mov [gfx_ln_cx2], cx
    mov [gfx_ln_cy2], dx
    push si
    push di
    call gfx_line_raw
    pop di
    pop si
    add si, WCR_SZ
    dec di
    jnz .frag
.nextpass:
    dec word [gfx_ln_np]
    jz .out
    mov ax, [gfx_ln_fdx]        ; the next parallel pass, one pixel across
    add [gfx_ln_x1], ax
    add [gfx_ln_x2], ax
    mov ax, [gfx_ln_fdy]
    add [gfx_ln_y1], ax
    add [gfx_ln_y2], ax
    jmp .passes
.out:
    GFXDLEAVE
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.hline:
    cmp ax, cx                  ; gfx_hline wants x1 <= x2 in AX/BX, y in DX
    jle .hord
    xchg ax, cx
.hord:
    mov bx, cx
    call gfx_hline
    jmp short .out
.vline:
    cmp bx, dx                  ; gfx_vline wants x in AX, y1/y2 in BX/DX
    jle .vord
    xchg bx, dx
.vord:
    call gfx_vline
    jmp short .out

; gfx_line_raw - internal: one pass over the line, drawing only inside the
;                box in gfx_ln_c*. The Bresenham state is already staged.
; clobbers: everything but the segment registers
gfx_line_raw:
    cmp byte [bb_on], 0         ; SPEC.md 5.6.4: mono IS the 4.77MHz machine
    je gfx_line_runs            ; and gets the per-pixel loop; VGA is the fast
    cmp byte [vid_planes], 1    ; one and gets major-axis runs, still fewer
    jne gfx_line_runs           ; calls than the app-side horizontal-only
                                ; coalescing this replaces
; --- the 1bpp per-pixel walk -------------------------------------------------
gfx_line_mono:
    mov al, [gfx_color]
    call gfx_ink                ; AL = 00 black / FF white / 01 the dither
    mov [gfx_ln_ink], al
    mov es, [vid_rseg]
    mov ax, [gfx_ln_y1]
    call gfx_rowbase            ; clobbers AX/CX/DX, preserves BX
    mov di, ax
    mov ax, [gfx_ln_x1]
    mov cl, 3
    shr ax, cl
    add di, ax
    mov cx, [gfx_ln_x1]         ; BL = the rotating bit mask
    and cl, 7
    mov bl, 0x80
    shr bl, cl
    mov si, [gfx_ln_x1]         ; SI = x, DX = y, CX = err
    mov dx, [gfx_ln_y1]
    mov cx, [gfx_ln_err]
.pixel:
    cmp dx, [gfx_ln_cy2]        ; y only ever grows, so past the box's bottom
    jg .done                    ; there is nothing left to draw
    cmp dx, [gfx_ln_cy1]
    jl .step
    cmp byte [gfx_ln_wide], 0   ; SPEC.md 5.6.6
    jne .wide
    cmp si, [gfx_ln_cx1]
    jl .step
    cmp si, [gfx_ln_cx2]
    jg .step
    mov al, [gfx_ln_ink]
    cmp al, 1                   ; the dither class: lit where (x+y) is even
    jne .solid
    mov ax, si
    xor ax, dx
    and al, 1
    dec al                      ; 0 (even) -> FF lit, 1 -> 00
.solid:
    and al, bl                  ; AL = the bit to set, 0 if this ink clears it
    mov ah, bl
    not ah
    and ah, [es:di]             ; one read and one write, not two RMW
    or  ah, al
    mov [es:di], ah
.step:
    cmp si, [gfx_ln_x2]
    jne .go
    cmp dx, [gfx_ln_y2]
    je .done
.go:
    mov ax, cx                  ; e2 = 2 * err; BOTH tests read the same e2,
    add ax, ax                  ; which is what makes it one Bresenham
    mov [gfx_ln_e2], ax
    mov ax, [gfx_ln_dy]
    neg ax
    cmp [gfx_ln_e2], ax         ; e2 > -dy: step x
    jle .noxs
    sub cx, [gfx_ln_dy]
    cmp word [gfx_ln_sx], 0
    jl .xleft
    inc si
    shr bl, 1
    jnz .noxs
    mov bl, 0x80
    inc di
    jmp short .noxs
.xleft:
    dec si
    shl bl, 1
    jnz .noxs
    mov bl, 0x01
    dec di
.noxs:
    mov ax, [gfx_ln_e2]
    cmp ax, [gfx_ln_dx]         ; e2 < dx: step y
    jge .pixel
    add cx, [gfx_ln_dx]
    inc dx
    call gfx_nextrow            ; DI and flags only - CX/SI/DX/BX survive
    jmp .pixel
.done:
    ret

; --- SPEC.md 5.6.6: the three columns x-1, x, x+1 of one row, in one visit ---
; Each column is clipped on its own, exactly as its own pass would have been.
; At most ONE of them can fall in a neighbouring byte, because the centre bit
; cannot be both 0x80 and 0x01 - so BH carries that bit and BP its direction.
.wide:
    xor ah, ah                  ; AH = the bits of [es:di] this row owes
    mov bh, 0                   ; BH = a bit in the neighbouring byte, 0 = none
    cmp si, [gfx_ln_cx1]
    jl .wleft
    cmp si, [gfx_ln_cx2]
    jg .wleft
    mov ah, bl
.wleft:
    dec si
    cmp si, [gfx_ln_cx1]
    jl .wright
    mov al, bl
    shl al, 1                   ; 0x80 shifts OUT: the column is in [es:di-1]
    jnz .wlin
    mov bh, 0x01
    mov bp, -1
    jmp short .wright
.wlin:
    or ah, al
.wright:
    inc si
    inc si
    cmp si, [gfx_ln_cx2]
    jg .wmid
    mov al, bl
    shr al, 1                   ; 0x01 shifts OUT: the column is in [es:di+1]
    jnz .wrin
    mov bh, 0x80
    mov bp, 1
    jmp short .wmid
.wrin:
    or ah, al
.wmid:
    dec si                      ; SI back to the centre column
    or ah, ah
    jz .wnb
    mov al, [gfx_ln_ink]        ; solid by construction: 00 or FF
    and al, ah
    not ah
    and ah, [es:di]
    or  ah, al
    mov [es:di], ah
.wnb:
    or bh, bh
    jz .wout
    add di, bp
    mov al, [gfx_ln_ink]
    and al, bh
    not bh
    and bh, [es:di]
    or  bh, al
    mov [es:di], bh
    sub di, bp
.wout:
    jmp .step

; --- major-axis runs, one gfx_fill_raw each ----------------------------------
; gfx_fill_raw and not gfx_fill: the region has already been spent above, and
; re-entering GFXCLIP per run would clip the same line twice.
gfx_line_runs:
    mov si, [gfx_ln_x1]
    mov dx, [gfx_ln_y1]
    mov cx, [gfx_ln_err]
    mov ax, [gfx_ln_dx]         ; the major axis: x if dx >= dy, else y
    cmp ax, [gfx_ln_dy]
    jae .majx
    mov byte [gfx_ln_majx], 0
    mov [gfx_ln_rs], dx         ; a vertical run starts at this y
    jmp short .step
.majx:
    mov byte [gfx_ln_majx], 1
    mov [gfx_ln_rs], si         ; ...a horizontal one at this x
.step:
    cmp si, [gfx_ln_x2]
    jne .go
    cmp dx, [gfx_ln_y2]
    je .last
.go:
    mov ax, cx
    add ax, ax
    mov [gfx_ln_e2], ax
    mov ax, [gfx_ln_dy]
    neg ax
    cmp [gfx_ln_e2], ax
    jle .noxs
    sub cx, [gfx_ln_dy]
    cmp byte [gfx_ln_majx], 0   ; a MINOR-axis step ends the open run
    jne .xok
    call gfx_line_flush
.xok:
    add si, [gfx_ln_sx]
    cmp byte [gfx_ln_majx], 0
    jne .noxs
    mov [gfx_ln_rs], dx         ; the next vertical run starts here
.noxs:
    mov ax, [gfx_ln_e2]
    cmp ax, [gfx_ln_dx]
    jge .step
    add cx, [gfx_ln_dx]
    cmp byte [gfx_ln_majx], 0
    je .yok
    call gfx_line_flush
.yok:
    inc dx
    cmp byte [gfx_ln_majx], 0
    je .step
    mov [gfx_ln_rs], si
    jmp short .step
.last:
    call gfx_line_flush
    ret

; gfx_line_flush - internal: the open run, clipped to the box, as one rect.
; in:  SI = x, DX = y (the run's far end), [gfx_ln_rs] its near end
; clobbers: ax, bx, di, flags. SI/CX/DX are the walk's and are preserved.
gfx_line_flush:
    push cx
    push dx
    push si
    cmp byte [gfx_ln_majx], 0
    je .vert
    mov ax, [gfx_ln_rs]         ; horizontal: (rs..x, y..y)
    mov cx, si
    cmp ax, cx
    jle .hord
    xchg ax, cx
.hord:
    mov bx, dx
    jmp short .clip
.vert:
    mov ax, si                  ; vertical: (x..x, rs..y)
    mov cx, si
    mov bx, [gfx_ln_rs]
.clip:
    cmp ax, [gfx_ln_cx1]
    jge .c1
    mov ax, [gfx_ln_cx1]
.c1:
    cmp cx, [gfx_ln_cx2]
    jle .c2
    mov cx, [gfx_ln_cx2]
.c2:
    cmp ax, cx
    jg .out
    cmp bx, [gfx_ln_cy1]
    jge .c3
    mov bx, [gfx_ln_cy1]
.c3:
    cmp dx, [gfx_ln_cy2]
    jle .c4
    mov dx, [gfx_ln_cy2]
.c4:
    cmp bx, dx
    jg .out
    call gfx_fill_raw
.out:
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; gfx_blit4 - draw a block of packed 4bpp pixels (SPEC.md 5.4/42)
;
; in:       ES:SI = source, packed 4 bits per pixel, 2 pixels per byte, the
;                   HIGH nibble leftmost - the layout apps/paint's canvas
;                   already uses, and the one a 16-colour BMP row uses
;           BP    = source stride in bytes (one row to the next)
;           AX    = destination x, BX = destination y (screen coords)
;           CX    = width in pixels (> 0), DX = height in rows (> 0)
; out:      nothing; every register preserved. [gfx_color] is left holding
;           the last colour drawn.
; clobbers: nothing else (flags)
;
; What this is, and what it deliberately is not. It coalesces runs of equal
; pixels and emits one gfx_fill per run - the same thing apps/paint's pt_blit
; did in the package, and therefore the same pixels - but from INSIDE the
; kernel. That is the whole win and it is a large one: since packages own a
; segment (SPEC.md 20.1) every gfx call from one is a FAR call, so a canvas
; repaint that cost N far calls plus N run scans now costs ONE call and a
; near loop. It also means the primitive works on all three adapters, in
; front of and behind the back buffer, and inside a clip region, without one
; line of adapter-specific code - because gfx_fill already does.
;
; **The run scan compares BYTES, not pixels, and that is the whole speed of
; the SCAN.** A run of colour c is a run of bytes equal to c|c<<4, so
; `repe scasb` walks it two pixels at a time at about 15 clocks a byte -
; roughly seven and a half clocks a pixel. The odd ends are handled by hand:
; the first pixel when the run starts on a low nibble, the last when it ends
; on a high one.
;
; The first version of this routine decoded every pixel individually instead
; - read the byte, test the parity, `shr al, cl` by four, compare, branch -
; and that is 75 to 90 clocks a pixel on an 8086, because a 4-bit shift by CL
; alone is 24 of them. It kept the SHAPE of the optimisation (one call per
; run) and threw away the optimisation. Those two cycle counts are still
; WRITTEN DOWN rather than measured, and the "quarter of a second to over
; two" figure this comment used to give for a 448x280 repaint was derived
; from them; the ratio is the point and the absolute numbers are not
; measurements. What HAS since been measured on the target machine is more
; important than either, and it is below.
;
; **What a blit actually costs, measured on a 4.77MHz 8088 (PERFORMANCE.md
; Part 9): `runs x 0.5 ms`.** Each coalesced run is one gfx_hline, and a
; drawing call on this machine has a ~756us FIXED cost - far call, lock, clip
; test, dispatch, gfx_rowbase - before it draws a pixel. Three routes agree:
; a 64-run solid blit is 441us a call, a 1,024-run striped one 548us, and
; GFX_HLINE 8px through the API 765us. So the pixel count barely enters the
; price at all and **how FLAT the source is decides it**: the same 4,096
; pixels cost 28.2 ms solid and 561 ms in 4-pixel stripes, a 20x spread with
; no pixel of difference. A caller that wants a blit to be cheap makes its
; art flat; a caller that must draw detail should expect ~0.5 ms per colour
; change per row, and budget accordingly.
;
; A plane-parallel VGA fast path would attack exactly that, since it costs
; one write per 8 pixels regardless of flatness - see below.
;
; A plane-parallel VGA fast path (build a byte-per-8-pixels mask per plane
; and write it through the Map Mask) would beat this on detailed pictures and
; lose to it on flat ones, and it would need its own back-buffer and 1bpp
; twins. It is a worthwhile future optimisation and it changes no caller;
; docs/PAINT-NOTES.md records why it was not the first one.
;
; Colour reduction on a 1bpp adapter happens in gfx_fill, per run (SPEC.md
; 39.4), so a 16-colour picture reads as black/dither/white there exactly as
; the rest of the UI does.
; -----------------------------------------------------------------------------
gfx_blit4:
    call cur_unlazy             ; off the clip list (SPEC.md 11.3): 7.1.4
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    or cx, cx
    jz .out
    or dx, dx
    jz .out
    mov [gfx_blit_w], cx        ; lock-held scratch, like vga12's rect block:
    mov [gfx_blit_x], ax        ; every drawing entry runs under the gfx lock
    mov [gfx_blit_h], dx        ; (SPEC.md 1 rule 6), so one copy serves
    mov [gfx_blit_sv], si
.row:
    mov si, [gfx_blit_sv]       ; ES:SI = this row's first byte
    xor dx, dx                  ; DX = the pixel the next run starts at
.run:
    ; --- the run's colour, and the byte a matching PAIR would hold ---------
    mov di, dx
    shr di, 1
    add di, si
    mov al, [es:di]
    test dl, 1
    jnz .lo                     ; an even pixel is the HIGH nibble
    mov cl, 4
    shr al, cl
.lo:
    and al, 0Fh
    mov [gfx_blit_c], al
    mov ah, al
    mov cl, 4
    shl ah, cl
    or ah, al
    mov [gfx_blit_pb], ah

    ; --- walk it two pixels at a time -------------------------------------
    mov cx, dx
    test cl, 1
    jz .aligned
    inc cx                      ; an odd start: the pair scan begins at the
.aligned:                       ; next pixel, and this one is already in
    mov [gfx_blit_e], cx
    mov ax, [gfx_blit_w]
    sub ax, cx
    jbe .tail                   ; nothing left to scan in pairs
    shr ax, 1                   ; whole pairs remaining in the row
    jz .tail
    mov di, cx
    shr di, 1
    add di, si                  ; ES:DI = the first pair byte
    mov [gfx_blit_s0], di       ; SI belongs to the row, so the scan's origin
    mov cx, ax                  ; is parked in memory
    mov al, [gfx_blit_pb]
    cld
    repe scasb
    mov ax, di                  ; `mov` leaves scasb's ZF alone, and the
    jne .mism                   ; branch has to read it before anything else
    sub ax, [gfx_blit_s0]       ; does: every byte matched
    jmp short .have
.mism:
    dec ax                      ; the mismatch sits at DI-1, so it is not
    sub ax, [gfx_blit_s0]       ; part of the run
.have:
    add ax, ax                  ; bytes matched -> pixels
    add [gfx_blit_e], ax
.tail:
    mov cx, [gfx_blit_e]        ; the odd final nibble, by hand
    cmp cx, [gfx_blit_w]
    jae .emit
    mov di, cx
    shr di, 1
    add di, si
    mov al, [es:di]
    test cl, 1
    jnz .tlo
    mov cl, 4
    shr al, cl
.tlo:
    and al, 0Fh
    cmp al, [gfx_blit_c]
    jne .emit
    inc word [gfx_blit_e]       ; it matches too
.emit:
    mov di, [gfx_blit_x]
    add di, dx                  ; DI = the run's first x, on screen
    mov ah, [gfx_blit_c]
    mov dx, [gfx_blit_e]        ; DX = one past its last pixel, row-relative
    call gfx_blit_run           ; (which is where the next run starts)
    cmp dx, [gfx_blit_w]
    jb .run

    mov ax, [gfx_blit_sv]       ; next source row
    add ax, bp
    mov [gfx_blit_sv], ax
    inc bx                      ; next destination row
    dec word [gfx_blit_h]
    jnz .row
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_blit_run - emit one coalesced run (module-internal to gfx_blit4)
; in:  DI = run start x, DX = one past the run's last pixel (row-relative),
;      AH = colour, BX = y, [gfx_blit_x] = the block's left edge
; out: nothing (AX/BX/CX/DX/SI/DI/BP preserved)
; -----------------------------------------------------------------------------
gfx_blit_run:
    push ax
    push bx
    push cx
    push dx
    mov al, ah
    mov [gfx_color], al
    mov ax, di                  ; x1 = the run's start
    mov cx, [gfx_blit_x]
    add cx, dx
    dec cx                      ; x2 = the last pixel drawn
    cmp ax, cx
    ja .out                     ; empty run (cannot happen; cheap to guard)
    mov dx, bx                  ; gfx_hline wants y in DX
    mov bx, cx
    call gfx_hline              ; clips, dispatches and reduces for us
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_vline_core - internal: vertical run of masked byte writes
;
; Shared by gfx_pixel / gfx_vline / gfx_xor_rect: the caller has already
; armed GC0/GC1 (and GC3 for XOR). Reads each byte before writing, which
; both loads the latches for the masked write and feeds the XOR ALU.
; in:       AX = x, BX = y1, DX = y2
; out:      GC8 left set to the column mask (caller resets)
; clobbers: flags only
; -----------------------------------------------------------------------------
vga_vline_core:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp ax, SCREEN_W            ; catches x < 0 too (unsigned)
    jae .done
    or bx, bx
    jns .y1ok
    xor bx, bx
.y1ok:
    cmp dx, SCREEN_H
    jl .y2ok
    mov dx, SCREEN_H-1
.y2ok:
    cmp dx, bx
    jl .done
    mov si, dx                  ; si = row count
    sub si, bx
    inc si
    push ax
    mov ax, ROW_BYTES
    mul bx                      ; ax = y1 * 80
    mov di, ax
    pop ax
    mov cx, ax                  ; bh = 80h >> (x & 7)
    and cl, 7
    mov bh, 0x80
    shr bh, cl
    mov cl, 3
    shr ax, cl
    add di, ax                  ; di = VRAM offset of top byte
    mov dx, VGA_GC
    mov ah, bh
    mov al, 8
    out dx, ax                  ; GC8 = column mask
    mov ax, VGA_SEG
    mov es, ax
.row:
    mov al, [es:di]             ; load latches
    mov [es:di], al
    add di, ROW_BYTES
    dec si
    jnz .row
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_vline - vertical line in [gfx_color], inclusive endpoints
; in:       AX = x, BX = y1, DX = y2  (y1 <= y2)
; out:      -
; clobbers: flags
;
; A 1-column solid rect via gfx_fill (which clips and back-buffer-
; dispatches itself); vga_vline_core stays for gfx_xor_rect's verticals.
; -----------------------------------------------------------------------------
gfx_vline:
    push cx
    mov cx, ax                  ; rect (x,y1)-(x,y2)
    call gfx_fill
    pop cx
    ret

; -----------------------------------------------------------------------------
; gfx_fill - solid rectangle in [gfx_color], inclusive corners
; in:       AX = x1, BX = y1, CX = x2, DX = y2
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_fill:
    GFXCLIP gfx_fill_d
gfx_fill_d:
    GFXDISP gfx_fill_raw
gfx_fill_raw:
    call bb_mono_chk            ; colour tracking runs in BOTH modes: double
                                ; buffering can be switched on later, and the
                                ; buffer it seeds from VRAM is only plane-
                                ; identical if nothing but 0/15 was ever
                                ; drawn (SPEC.md 32)
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    jne bb_fill
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call vga_rect_setup
    jc .done
    call vga_set_color
    mov ax, VGA_SEG
    mov es, ax
    mov dx, VGA_GC
    mov ah, [vga_lmsk]          ; left edge column (whole rect if 1 byte wide)
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    mov si, [vga_rows]
.lcol:
    mov al, [es:di]             ; load latches
    mov [es:di], al
    add di, ROW_BYTES
    dec si
    jnz .lcol
    mov bx, [vga_span]
    or bx, bx
    jz .reset
    mov ah, [vga_rmsk]          ; right edge column
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    add di, bx
    mov si, [vga_rows]
.rcol:
    mov al, [es:di]
    mov [es:di], al
    add di, ROW_BYTES
    dec si
    jnz .rcol
    dec bx                      ; interior bytes per row
    jz .reset
    mov ax, 0xFF08              ; full bytes: latches irrelevant
    out dx, ax
    mov di, [vga_off]
    inc di
    mov si, [vga_rows]
.irow:
    mov cx, bx
    push di
    rep stosb
    pop di
    add di, ROW_BYTES
    dec si
    jnz .irow
.reset:
    call vga_gc_reset
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_frame - 1px outline rectangle in [gfx_color]
; in:       AX = x1, BX = y1, CX = x2, DX = y2  (x1<=x2, y1<=y2)
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_frame:
    push bx
    push dx
    push si
    push di
    mov si, bx                  ; si = y1
    mov di, dx                  ; di = y2
    mov bx, cx                  ; hline wants BX = x2
    mov dx, si
    call gfx_hline              ; top
    mov dx, di
    call gfx_hline              ; bottom
    mov bx, si                  ; vline wants BX = y1, DX = y2
    mov dx, di
    call gfx_vline              ; left  (replace mode: corner overlap is fine)
    push ax
    mov ax, cx
    call gfx_vline              ; right
    pop ax
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; gfx_fill_gray - 50% dither: (x+y) even = white, odd = black (desktop)
;
; Ignores [gfx_color]. Bytes start on even x, so the pattern byte depends
; only on row parity: 0AAh on even rows, 55h on odd. With Set/Reset off and
; Map Mask 0Fh the CPU byte lands in all four planes, giving color 15/0
; per bit directly.
; in:       AX = x1, BX = y1, CX = x2, DX = y2
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_fill_gray:
    GFXCLIP gfx_fill_gray_d
gfx_fill_gray_d:
    GFXDISP gfx_fill_gray_raw
gfx_fill_gray_raw:
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    jne bb_fill_gray
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call vga_rect_setup
    jc .done
    mov bl, 0xAA                ; pattern for the top clipped row
    test byte [vga_y1], 1
    jz .parity
    mov bl, 0x55
.parity:
    mov ax, VGA_SEG
    mov es, ax
    mov dx, VGA_GC
    mov ah, [vga_lmsk]          ; left edge column
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    mov si, [vga_rows]
    mov bh, bl
.lcol:
    mov al, [es:di]             ; load latches for the masked write
    mov [es:di], bh
    not bh                      ; AA <-> 55 each row
    add di, ROW_BYTES
    dec si
    jnz .lcol
    mov cx, [vga_span]
    or cx, cx
    jz .reset
    mov ah, [vga_rmsk]          ; right edge column
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    add di, cx
    mov si, [vga_rows]
    mov bh, bl
.rcol:
    mov al, [es:di]
    mov [es:di], bh
    not bh
    add di, ROW_BYTES
    dec si
    jnz .rcol
    dec cx                      ; interior bytes per row
    jz .reset
    mov ax, 0xFF08              ; full bytes: latches irrelevant
    out dx, ax
    mov di, [vga_off]
    inc di
    mov si, [vga_rows]
.irow:
    mov al, bl
    push cx
    push di
    rep stosb
    pop di
    pop cx
    add di, ROW_BYTES
    not bl
    dec si
    jnz .irow
.reset:
    call vga_gc_reset
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_fill_pat - 8x8 dither pattern fill, screen-aligned (SPEC.md 5)
;
; Row y takes the byte [gfx_pat][y & 7]; bit set = white (15), clear =
; black (0), bit 7 the leftmost pixel of each screen byte. Ignores
; [gfx_color] and, like gfx_fill_gray, writes only colors 0/15, so it
; never retires [bb_mono] (SPEC.md 32). Same masked-edge scheme as
; gfx_fill_gray, with the row byte fetched from the table instead of the
; AA/55 toggle. The 8 pattern bytes are staged into vga_patbuf first:
; [bp+bx] is not an 8086 addressing mode, but [vga_patbuf+bx] is (and the
; bb twin shares the staging).
; in:       AX = x1, BX = y1, CX = x2, DX = y2; [gfx_pat] = pattern ptr
; out:      -
; clobbers: flags
;
; Clipped like the other rect fills, and it re-enters per fragment for free
; because the pattern is SCREEN-aligned: the row byte is [vga_y1] & 7 off a
; table staged from [gfx_pat] on every call, so a fragment starting at any y
; picks the same byte the whole rect would have. It was the one rect
; primitive without the hook, and the Task Manager's memory map is drawn
; almost entirely out of it - the claim bands, the buffer texture and each
; package's region pattern - so a map redrawn behind another window painted
; its full width straight across whatever was on top. The kernel's own band
; stopped at the right place, because gfx_fill_gray had the hook, which is
; what made it look like the map was drawing the wrong SHAPE rather than in
; the wrong PLACE.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; osapi_gfx_fill_pat - API slot 0x02B0's X body (SPEC.md 20.9)
; in:  AX/BX/CX/DX = the rect, ES:SI = the caller's 8 pattern bytes
; out: nothing; SI preserved
;
; The eight bytes are staged into the kernel's own segment first, the
; dsk_get_dir idiom (SPEC.md 2.1), because vga_pat_stage reads [gfx_pat]
; through DS and DS is KERNEL_SEG by the time it runs. [gfx_pat] is a NEAR
; pointer and always was; giving it a segment would put one on every kernel
; caller of a primitive that is drawn per fragment.
; -----------------------------------------------------------------------------
osapi_gfx_fill_pat:
    push ax
    push cx
    push si
    push di
    mov di, api_pat
    mov cx, 8
.copy:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .copy
    mov word [gfx_pat], api_pat
    pop di
    pop si
    pop cx
    pop ax
    call gfx_fill_pat
    ret

gfx_fill_pat:
    GFXCLIP gfx_fill_pat_d
gfx_fill_pat_d:
    GFXDISP gfx_fill_pat_raw
gfx_fill_pat_raw:
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    jne bb_fill_pat
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call vga_rect_setup
    jc .done
    call vga_pat_stage          ; vga_patbuf <- the 8 bytes at [gfx_pat]
    mov ax, VGA_SEG
    mov es, ax
    mov dx, VGA_GC
    mov ah, [vga_lmsk]          ; left edge column
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    mov si, [vga_rows]
    mov bx, [vga_y1]
    and bx, 7
.lcol:
    mov ah, [vga_patbuf+bx]     ; row byte first: the masked write must
    mov al, [es:di]             ; follow the latch load immediately
    mov [es:di], ah
    inc bx
    and bx, 7
    add di, ROW_BYTES
    dec si
    jnz .lcol
    mov cx, [vga_span]
    or cx, cx
    jz .reset
    mov ah, [vga_rmsk]          ; right edge column
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    add di, cx
    mov si, [vga_rows]
    mov bx, [vga_y1]
    and bx, 7
.rcol:
    mov ah, [vga_patbuf+bx]
    mov al, [es:di]
    mov [es:di], ah
    inc bx
    and bx, 7
    add di, ROW_BYTES
    dec si
    jnz .rcol
    dec cx                      ; interior bytes per row
    jz .reset
    mov ax, 0xFF08              ; full bytes: latches irrelevant
    out dx, ax
    mov di, [vga_off]
    inc di
    mov si, [vga_rows]
    mov bx, [vga_y1]
    and bx, 7
.irow:
    mov al, [vga_patbuf+bx]
    push cx
    push di
    rep stosb
    pop di
    pop cx
    add di, ROW_BYTES
    inc bx
    and bx, 7
    dec si
    jnz .irow
.reset:
    call vga_gc_reset
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_pat_stage - internal: copy the 8 bytes at [gfx_pat] into vga_patbuf
;                 so the fill loops can index them ([bx+disp] addressing)
; in:       [gfx_pat]
; out:      vga_patbuf filled
; clobbers: SI, DI, ES, flags
; -----------------------------------------------------------------------------
vga_pat_stage:
    push ds                     ; DF=0 per SPEC.md 1
    pop es
    mov si, [gfx_pat]
    mov di, vga_patbuf
    movsw
    movsw
    movsw
    movsw
    ret

; -----------------------------------------------------------------------------
; vga_xor_hline - internal: horizontal XOR line
;
; Caller has armed the XOR state (vga_set_xor). Every byte - edges AND
; interior - is read immediately before the write so the ALU XORs against
; fresh latches. No rep stosb here (SPEC.md 5).
; in:       AX = x1, BX = x2, DX = y  (x1 <= x2)
; out:      GC8 left set (caller resets)
; clobbers: flags only
; -----------------------------------------------------------------------------
vga_xor_hline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp dx, SCREEN_H
    jae .done
    or ax, ax
    jns .x1ok
    xor ax, ax
.x1ok:
    cmp bx, SCREEN_W
    jl .x2ok
    mov bx, SCREEN_W-1
.x2ok:
    cmp bx, ax
    jl .done
    push ax
    mov ax, ROW_BYTES
    mul dx                      ; ax = y * 80
    mov di, ax
    pop ax
    mov si, ax                  ; si = left byte
    mov cl, 3
    shr si, cl
    mov dx, bx                  ; dx = right byte
    shr dx, cl
    sub dx, si
    add di, si
    mov si, dx                  ; si = span
    mov cl, al                  ; ch = left mask
    and cl, 7
    mov ch, 0xFF
    shr ch, cl
    mov cl, bl                  ; bh = right mask
    and cl, 7
    xor cl, 7
    mov bh, 0xFF
    shl bh, cl
    mov ax, VGA_SEG
    mov es, ax
    mov dx, VGA_GC
    or si, si
    jnz .edge
    and ch, bh                  ; one byte: combine the masks
.edge:
    mov ah, ch
    mov al, 8
    out dx, ax
    mov al, [es:di]             ; read-before-write: loads the latches
    stosb
    or si, si
    jz .done
    dec si
    jz .right
    mov ax, 0xFF08              ; interior: full mask, still byte-by-byte
    out dx, ax
    mov cx, si
.interior:
    mov al, [es:di]
    stosb
    loop .interior
.right:
    mov ah, bh
    mov al, 8
    out dx, ax
    mov al, [es:di]
    stosb
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_xor_rect - 1px outline rectangle, XOR 0Fh (drag outline)
;
; Self-inverting: drawing it twice restores the screen. Each edge pixel is
; touched exactly once - the verticals skip the corner rows, and degenerate
; rects (x1=x2 and/or y1=y2) collapse to a single line/pixel rather than
; XORing anything twice.
; in:       AX = x1, BX = y1, CX = x2, DX = y2  (x1<=x2, y1<=y2)
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_xor_rect:
    cmp word [wm_clip_n], 0     ; clipped background draw? (SPEC.md 11.3)
    jne gfx_xor_rect_clip       ; an OUTLINE is not the intersection of its
                                ; bounding rect with anything - clipping it
                                ; that way would draw edges through the
                                ; middle of it, so it decomposes first
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    jne bb_xor_rect
vga_xor_rect_vram:
    call cur_unlazy             ; the drag outline bypasses the buffer AND
                                ; the clip (SPEC.md 32): 7.1.4              ; VRAM body under its own name: transient
                                ; overlays (the drag outline) call it direct
                                ; so they never enter the back buffer
    cmp byte [vid_mono], 0      ; ...but "direct" means the framebuffer, and
    jne bb_xor_rect             ; on a 1bpp adapter the software renderer IS
                                ; the direct path (SPEC.md 39.5)
    push bx
    push dx
    push si
    push di
    call vga_set_xor
    mov si, bx                  ; si = y1
    mov di, dx                  ; di = y2
    mov bx, cx                  ; xor_hline wants BX = x2
    mov dx, si
    call vga_xor_hline          ; top
    cmp di, si
    je .reset                   ; y1 = y2: one line, done
    mov dx, di
    call vga_xor_hline          ; bottom
    mov bx, si                  ; verticals span y1+1 .. y2-1
    inc bx
    mov dx, di
    dec dx
    cmp dx, bx
    jl .reset                   ; only two rows: no vertical part
    call vga_vline_core         ; left (AX = x1; reads before writes)
    cmp cx, ax
    je .reset                   ; x1 = x2: one column, done
    push ax
    mov ax, cx
    call vga_vline_core         ; right
    pop ax
.reset:
    call vga_gc_reset
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; gfx_xor_rect_clip - internal: the outline as four filled strips
; in:       AX = x1, BX = y1, CX = x2, DX = y2; the clip is armed
; out:      -
; clobbers: flags
;
; The same decomposition bb_xor_rect uses, with the same degenerate cases -
; each pixel is touched exactly once, so the result is still self-inverting.
; Every strip goes through the PUBLIC gfx_xor_fill, which is where it meets
; the clip region (SPEC.md 11.3).
; -----------------------------------------------------------------------------
gfx_xor_rect_clip:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, bx                  ; SI = y1, DI = y2
    mov di, dx
    mov dx, si
    call gfx_xor_fill           ; top row
    cmp di, si
    je .out                     ; y1 = y2: one line, done
    mov bx, di
    mov dx, di
    call gfx_xor_fill           ; bottom row
    mov bx, si                  ; verticals span y1+1 .. y2-1
    inc bx
    mov dx, di
    dec dx
    cmp dx, bx
    jl .out                     ; only two rows: no vertical part
    push cx
    mov cx, ax
    call gfx_xor_fill           ; left column
    pop cx
    cmp cx, ax
    je .out                     ; x1 = x2: one column, done
    mov ax, cx
    call gfx_xor_fill           ; right column
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_xor_fill - filled rectangle, XOR 0Fh (menu highlight)
;
; Every byte read-before-write; no rep stosb anywhere (SPEC.md 5).
; in:       AX = x1, BX = y1, CX = x2, DX = y2
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_xor_fill:
    GFXCLIP gfx_xor_fill_d
gfx_xor_fill_d:
    GFXDISP gfx_xor_fill_raw
gfx_xor_fill_raw:
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    jne bb_xor_fill
vga_xor_fill_vram:
    call cur_unlazy             ; ...and the menu highlights with it              ; VRAM body under its own name (see
                                ; vga_xor_rect_vram) - the menu highlights
    cmp byte [vid_mono], 0
    jne bb_xor_fill
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call vga_rect_setup
    jc .done
    call vga_set_xor
    mov ax, VGA_SEG
    mov es, ax
    mov dx, VGA_GC
    mov ah, [vga_lmsk]          ; left edge column
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    mov si, [vga_rows]
.lcol:
    mov al, [es:di]             ; read-before-write
    mov [es:di], al
    add di, ROW_BYTES
    dec si
    jnz .lcol
    mov bx, [vga_span]
    or bx, bx
    jz .reset
    mov ah, [vga_rmsk]          ; right edge column
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    add di, bx
    mov si, [vga_rows]
.rcol:
    mov al, [es:di]
    mov [es:di], al
    add di, ROW_BYTES
    dec si
    jnz .rcol
    dec bx                      ; interior bytes per row
    jz .reset
    mov ax, 0xFF08
    out dx, ax
    mov di, [vga_off]
    inc di
    mov si, [vga_rows]
.irow:
    mov cx, bx
    push di
.ibyte:
    mov al, [es:di]             ; every interior byte too
    stosb
    loop .ibyte
    pop di
    add di, ROW_BYTES
    dec si
    jnz .irow
.reset:
    call vga_gc_reset
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_save - copy a screen region to a buffer, plane by plane
;
; x1 is rounded down and x2 up to byte boundaries internally. Buffer layout:
; all plane-0 rows, then plane 1, plane 2, plane 3 (rows top to bottom,
; ((x2/8)-(x1/8)+1) bytes each). Uses Read Map Select (GC4) per plane.
; in:       AX = x1, BX = y1, CX = x2, DX = y2, ES:DI = buffer
; out:      DI advanced past the data (unchanged if the clipped rect is
;           empty); ES preserved
; clobbers: flags
;
; Dispatches to bb_save under double buffering (SPEC.md 32). vga_save_vram
; is the VRAM body under its own name: the mouse cursor's save-under calls
; it directly - the cursor lives in VRAM, never in the back buffer.
; -----------------------------------------------------------------------------
gfx_save:
    call cur_unlazy             ; the menu save-under READS the screen, so
                                ; the arrow must not be in what it banks
    cmp byte [bb_on], 0
    jne bb_save
vga_save_vram:
    cmp byte [vid_mono], 0      ; the cursor's save-under, from inside IRQ4:
    jne bb_save                 ; on mono the framebuffer IS the renderer's
                                ; target, so one "plane" of bb_xfer is it
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    call vga_rect_setup
    jc .done
    mov byte [vga_plane], 0
.plane:
    mov dx, VGA_GC
    mov ah, [vga_plane]
    mov al, 4                   ; GC4: Read Map Select
    out dx, ax
    mov si, [vga_off]
    mov dx, [vga_rows]
    mov bp, [vga_span]
    inc bp                      ; bp = bytes per row
    push ds
    mov ax, VGA_SEG
    mov ds, ax                  ; movsb source = VRAM
.row:
    mov cx, bp
    rep movsb                   ; buffer DI advances contiguously
    add si, ROW_BYTES
    sub si, bp
    dec dx
    jnz .row
    pop ds
    inc byte [vga_plane]
    cmp byte [vga_plane], 4
    jb .plane
    mov dx, VGA_GC
    mov ax, 0x0004              ; GC4 back to plane 0 (default)
    out dx, ax
.done:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_restore - write a gfx_save buffer back to the screen
;
; Same rounding and buffer layout as gfx_save. Writes through the Sequencer
; Map Mask one plane at a time with Set/Reset off and Bit Mask FF, so the
; CPU bytes land verbatim and the latches are irrelevant.
; in:       AX = x1, BX = y1, CX = x2, DX = y2, ES:SI = buffer
; out:      SI advanced past the data (unchanged if the clipped rect is
;           empty); ES preserved
; clobbers: flags
;
; Dispatches to bb_restore under double buffering (SPEC.md 32);
; vga_restore_vram is the VRAM body for the cursor path (see gfx_save).
; -----------------------------------------------------------------------------
gfx_restore:
    call cur_unlazy             ; ...and writes it back, unclipped
    cmp byte [bb_on], 0
    jne bb_restore
vga_restore_vram:
    cmp byte [vid_mono], 0      ; see vga_save_vram
    jne bb_restore
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    call vga_rect_setup
    jc .done
    mov byte [vga_plane], 1     ; plane mask bit, 1/2/4/8
.plane:
    mov dx, VGA_SEQ
    mov ah, [vga_plane]
    mov al, 2                   ; SEQ2: Map Mask = this plane only
    out dx, ax
    mov di, [vga_off]
    mov dx, [vga_rows]
    mov bp, [vga_span]
    inc bp                      ; bp = bytes per row
    push ds
    push es
    mov ax, es
    mov ds, ax                  ; movsb source = caller's buffer
    mov ax, VGA_SEG
    mov es, ax                  ; movsb dest = VRAM
.row:
    mov cx, bp
    rep movsb
    add di, ROW_BYTES
    sub di, bp
    dec dx
    jnz .row
    pop es
    pop ds
    mov al, [vga_plane]
    add al, al                  ; next plane bit
    mov [vga_plane], al
    cmp al, 0x10
    jb .plane
    mov dx, VGA_SEQ
    mov ax, 0x0F02              ; SEQ2: Map Mask back to all planes
    out dx, ax
.done:
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_pen_live / gfx_pen_dis / gfx_pen_cf - the enabled and disabled pens
;
; in:       gfx_pen_cf only: CF = 0 live, CF = 1 disabled - which is the shape
;           EVERY greying predicate in the tree already answers in, so a call
;           site is `call <ok-test>` then `call gfx_pen_cf` and nothing else
; out:      nothing
; clobbers: nothing (flags)
;
; SPEC.md 47 makes a disabled control two things at once - the CDGRAY pen and
; the [gfx_dis] flag - and these exist so no call site can set one without the
; other. That is not hypothetical tidiness: the bug this whole section came
; out of was a row whose LABEL was greyed and whose GLYPH was not, and "two
; halves of one control disagreeing" is the failure mode the flag would
; otherwise invite a second time.
;
; gfx_pen_live returns the pen to CBLACK, which is what every site in the tree
; wants back. A site that needs some other colour afterwards sets it itself -
; but it must still come through here, or the flag stays armed and the next
; string on this lock hold draws dithered.
; -----------------------------------------------------------------------------
gfx_pen_cf:
    jc gfx_pen_dis
gfx_pen_live:
    mov byte [gfx_color], CBLACK
    mov byte [gfx_dis], 0
    ret
gfx_pen_dis:
    mov byte [gfx_color], CDGRAY
    mov byte [gfx_dis], 1
    ret

; -----------------------------------------------------------------------------
; gfx_lock - acquire the drawing mutex, then hide the cursor (SPEC.md 7)
;
; Spins with task_yield while another task holds it, so background tasks
; keep running. Task-level only - never call from an ISR. Non-reentrant:
; re-acquiring from the holder deadlocks.
; in:       -
; out:      -
; clobbers: flags (IF = 1 on return)
; -----------------------------------------------------------------------------

gfx_lock:
.retry:
    cli
    cmp byte [gfx_lock_flag], 0
    je .take
    sti
    call task_yield
    jmp .retry
.take:
    mov byte [gfx_lock_flag], 1
    push ax
    mov al, [sch_cur]           ; the owner is stamped inside the cli window,
    mov [gfx_lock_own], al      ; so flag and owner can never disagree
    pop ax
    sti
    mov byte [cur_lazy], 1      ; PROMISE the hide (SPEC.md 7.1.4) - the ISR
    ret                         ; will not move the arrow while the flag is
                                ; set, so it is frozen where it is until
                                ; something is about to draw over it

; -----------------------------------------------------------------------------
; gfx_unlock - flush, show the cursor, then release the mutex (SPEC.md 7)
;
; Order is binding: gfx_flush pushes the dirty back-buffer rect to VRAM
; while the cursor is still hidden (a no-op without double buffering);
; cursor_show then redraws at the latest mouse position - its save-under
; captures the freshly flushed pixels - while we still own the screen;
; only then may the ISR draw again.
;
; The clip region dies here too (SPEC.md 11.3 rule 1). Making it die with
; the lock hold is what stops a leaked region from silently truncating the
; next painter - and it is what keeps the drag outline and the menu
; highlights unclipped without any of them knowing the region exists. The
; DISPLAY CONTEXT dies with it, for the same reason (SPEC.md 39.14.3).
; in:       -
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_unlock:
    mov word [wm_clip_n], 0
    mov byte [gfx_dis], 0       ; the disabled pen dies with the lock, for the
                                ; clip region's reason (SPEC.md 11.3/47): a
                                ; site that forgets gfx_pen_live cannot leak
                                ; dithered text into whatever draws next
%ifdef KERN_BIG
    push ax                     ; ...and so does the DISPLAY CONTEXT (SPEC.md
    xor al, al                  ; 39.14.3). Nothing in 39.14 restores it any
    call vid_ctx_act            ; more - the last display drawn on stays
    pop ax                      ; current, so a repaint pays one swap instead
                                ; of two per primitive - and this is what
                                ; bounds that to one lock hold, exactly as the
                                ; two stores above bound theirs. It is a
                                ; compare and a branch when nothing moved
%endif
    call gfx_flush
    cmp byte [cur_lazy], 0      ; was the hide ever spent? (SPEC.md 7.1.4)
    jne .never                  ; no: the arrow never left the screen, so
    call cursor_show            ; showing it would bank itself as background
    jmp short .rel
.never:
    call cur_lazyend            ; ...but the ISR could not move it all hold
.rel:
    mov byte [gfx_lock_own], 0xFF   ; owner first: a task switch between the
                                    ; two stores must not show flag-free with
                                    ; a stale owner still named
    mov byte [gfx_lock_flag], 0
    mov byte [dsk_batch], 0     ; ANY UNLOCKING OF THE USER INTERFACE ENDS A
                                ; BATCH (SPEC.md 18.9.3). One store, and it is
                                ; the whole safety argument: a caller cannot
                                ; leave a bracket open across a moment when the
                                ; user could reach the drive, because every way
                                ; out of a locked run of kernel code - return,
                                ; failure, a question, a swap prompt - comes
                                ; through here. mov leaves the flags alone,
                                ; which this routine's contract requires
    ret

; -----------------------------------------------------------------------------
section .bss

api_pat:     resb 8             ; osapi_gfx_fill_pat's staging slot: a
                                ; package's eight pattern bytes, copied into
                                ; the kernel's segment because vga_pat_stage
                                ; reads them through DS

gfx_blit_x:  resw 1             ; gfx_blit4's row cursor (SPEC.md 5.4):
gfx_blit_w:  resw 1             ; destination left, width, rows left and the
gfx_blit_h:  resw 1             ; current source row's base. Lock-held
gfx_blit_sv: resw 1             ; scratch, like the rect block below.
gfx_blit_e:  resw 1             ; ...and the run scanner's state: one past the
gfx_blit_s0: resw 1             ; run's last pixel, where `repe scasb` began,
gfx_blit_c:  resb 1             ; the run's colour, and the byte a matching
gfx_blit_pb: resb 1             ; PAIR of that colour holds (c | c<<4)

vga_plane       resb 1          ; save/restore plane counter / mask bit
vga_lmsk        resb 1          ; rect: left edge bit mask
vga_rmsk        resb 1          ; rect: right edge bit mask
vga_y1          resw 1          ; rect: clipped top row (dither parity)
vga_rows        resw 1          ; rect: clipped row count
vga_span        resw 1          ; rect: right byte - left byte
vga_off         resw 1          ; rect: VRAM offset of top-left byte
vga_lcol        resw 1          ; rect: left byte column (bb dirty union)
vga_patbuf      resb 8          ; gfx_fill_pat: staged pattern rows
gfx_cl_x1       resw 1          ; gfx_clip_run: the primitive's own rect,
gfx_cl_y1       resw 1          ; held while the raw body runs per fragment
gfx_cl_x2       resw 1
gfx_cl_y2       resw 1
gfx_cl_fn       resw 1          ; ...and the raw body's near pointer

gfx_ln_x1       resw 1          ; gfx_line (SPEC.md 5.6): the normalised
gfx_ln_y1       resw 1          ; endpoints - y1 <= y2 always, so the walk
gfx_ln_x2       resw 1          ; runs downward and the pixel set is a
gfx_ln_y2       resw 1          ; property of the PAIR, not of its order
gfx_ln_dx       resw 1          ; |x2-x1|, and y2-y1 which cannot be negative
gfx_ln_dy       resw 1
gfx_ln_sx       resw 1          ; +1 / -1
gfx_ln_err      resw 1          ; the Bresenham error term at the start
gfx_ln_e2       resw 1          ; ...and 2*err inside the loop
gfx_ln_cx1      resw 1          ; the box this pass draws inside - a clip
gfx_ln_cy1      resw 1          ; rect, or the screen when the region is
gfx_ln_cx2      resw 1          ; disarmed (5.6.3)
gfx_ln_cy2      resw 1
gfx_ln_rs       resw 1          ; runs path: the open run's near end, on the
                                ; major axis
gfx_ln_np       resw 1          ; passes still owed: 1 thin, 3 fat (5.6.5)
gfx_ln_fdx      resw 1          ; ...and the step between them, which is one
gfx_ln_fdy      resw 1          ; pixel across the MINOR axis
gfx_ln_ink      resb 1          ; mono path: gfx_ink's 00 / FF / 01
gfx_ln_wide     resb 1          ; 1 = the 1bpp three-column walk (5.6.6)
gfx_ln_majx     resb 1          ; runs path: 1 = the major axis is x
gfx_ls_x        resw 1          ; 5.6.7's staged walk state - the caller's
gfx_ls_y        resw 1          ; block, copied in and back out (20.8 rule 3,
gfx_ls_err      resw 1          ; and forced here as well as preferred: the
gfx_ls_dx       resw 1          ; mono walk needs ES for the framebuffer)
gfx_ls_dy       resw 1
gfx_ls_sx       resw 1
gfx_ls_sy       resw 1
gfx_ls_e2       resw 1
gfx_ls_bx1      resw 1          ; the clip rect the walk is inside, re-resolved
gfx_ls_by1      resw 1          ; only when a pixel steps out of it - a step is
gfx_ls_bx2      resw 1          ; a handful of pixels, so this is one list walk
gfx_ls_by2      resw 1          ; per CALL in every case that matters
gfx_ls_cseg     resw 1          ; ...and where the caller's block came from,
gfx_ls_coff     resw 1          ; because ES cannot hold it across the walk

section .text
