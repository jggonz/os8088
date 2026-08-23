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

; --- THEME (SPEC.md 76) -----------------------------------------------------
; [thm_kind] is the SETTING; the eight bytes below are what it MEANS, copied
; whole from thm_tab at the switch so every chrome site is a load and never a
; branch. The content of a window is not themed in any of them - only the
; desktop, the menu bar, the dock and a window's own chrome.
;
; It was TWO bytes while there were two themes, and Color is what made the
; distinctions real: a title bar that is blue when focused and grey when not
; needs a second pair, and a desktop that is neither a dither nor black needs
; a colour rather than a flag. THE LIVE BLOCK AND THE TABLE ROW ARE THE SAME
; EIGHT BYTES IN THE SAME ORDER, so the switch is one `rep movsb` and there is
; no per-field copy to fall out of step with the table.
THM_BRIGHT  equ 0
THM_DARK    equ 1
THM_COLOR   equ 2
%ifdef KERN_BIG
THM_NKIND   equ 3
%else
THM_NKIND   equ 2               ; NO COLOR ON kern_small (SPEC.md 76.12.3):
%endif                          ; the 128KB machine's kernel does not get to
                                ; spend a step on a theme it would have to
                                ; reduce to Bright anyway
THM_DITHER  equ 0xFF            ; [thm_deskc]: lay SPEC.md 26's 50% dither
                                ; rather than a solid colour

%ifdef THEMEDARK                ; make THEMEDARK=1 (Dark) or =2 (Color): the
thm_kind        db THEMEDARK    ; A/B, and the only way to see a theme before
%else                           ; the Control Panel has been clicked
thm_kind        db THM_BRIGHT
%endif
                                ; [thm_kind] IS IN BOTH BUILDS even though
                                ; kern_small has no theme: SYSTEM.CFG's 'TH'
                                ; key is two bytes on both, so a user's choice
                                ; round-trips untouched through a small kernel
                                ; instead of being reset by booting one
                                ; (SPEC.md 76.13)
%ifdef OS88_THEME
; ...and the live block. ADJACENT AND IN THIS ORDER: thm_pair reads the first
; two as one word, and thm_set copies the row over the lot.
thm_bgc         db CWHITE       ; chrome background: the field a caption sits on
thm_inkc        db CBLACK       ; chrome ink: the caption, the rules, the boxes
thm_disc        db CDGRAY       ; ...and DISABLED chrome ink (SPEC.md 76.6)
thm_tbgc        db CWHITE       ; the ACTIVE title bar's ground...
thm_tinkc       db CBLACK       ; ...and its ink. Equal to the pair above in
                                ; both 1bpp themes, which is what keeps them
                                ; byte-identical to the kernel that had one
thm_deskc       db THM_DITHER   ; the desktop's ground, or THM_DITHER
thm_strp        db 1            ; 1 = the active title carries SPEC.md 11's
                                ; pinstripes; 0 = it is a solid bar
thm_pad         db 0            ; the row is 8 so the index is a shift

; bg      ink     dis     tbg     tink    desk        stripes pad
thm_tab:
    db CWHITE, CBLACK, CDGRAY, CWHITE, CBLACK, THM_DITHER, 1, 0   ; Bright
    db CBLACK, CWHITE, CLGRAY, CBLACK, CWHITE, CBLACK,     1, 0   ; Dark
%ifdef KERN_BIG
    db CLGRAY, CBLACK, CDGRAY, CBLUE,  CWHITE, CTEAL,      0, 0   ; Color
%endif

%endif

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
gfx_disink      db CBLACK       ; ...and WHICH DIRECTION a disabled GLYPH
                                ; stipples in on a 1bpp adapter (SPEC.md
                                ; 76.6.1). The flag above buys the
                                ; checkerboard; this says what it is laid on,
                                ; and it is the setting pen's own LIVE ink -
                                ; black for the content pair, [thm_inkc] for
                                ; the chrome's. Without it font_ink's
                                ; reduction sent every disabled pen to black
                                ; (they are all middle greys, and a grey is
                                ; not 0xFF), so a greyed caption and a
                                ; MENU_DIS separator were a black stipple on
                                ; Dark's black chrome - drawn, correct in the
                                ; framebuffer, and invisible.
                                ;
                                ; Initialized data and NOT .bss, for
                                ; [gfx_dis]'s reason one line up: nothing
                                ; zeroes .bss, and CBLACK is the answer for
                                ; every caller that never touches it.
gfx_pat         dw 0            ; gfx_fill_pat: near ptr to 8 row bytes,
                                ; set by the caller before every call
gfx_lock_flag   db 0            ; drawing mutex (SPEC.md 7); 0 = free.
                                ; Initialized data, NOT .bss: nothing zeroes
                                ; .bss at boot, and this byte must start clear.
gfx_lock_own    db 0xFF         ; ...and WHICH task holds it ([sch_cur] at the
                                ; take, 0xFF = nobody). toast_now's whole test
                                ; (SPEC.md 59.4): the flag alone says somebody
                                ; holds it, never that the CALLER does.
gfx_lock_want   db 0            ; SPEC.md 7.3: SOMEBODY IS BLOCKED ON IT. Set by
                                ; a waiter on its way to task_yield, spent by
                                ; the last owner's next acquire. Initialized
                                ; data for gfx_lock_flag's reason.
gfx_lock_last   db 0xFF         ; ...and who took it last, which gfx_lock_own
                                ; cannot say: gfx_unlock stamps 0xFF over it,
                                ; and 7.3's whole question is about a task
                                ; that has ALREADY let go.

; -----------------------------------------------------------------------------
; GFXCLIP - the clip hook, at a public rect entry (SPEC.md 11.3)
;
; Placed at the PUBLIC entry, above the [vid_mono] dispatch, so one hook covers
; the VRAM path, VGA and both mono adapters at once - on a 1bpp adapter the
; software renderer IS the direct path (SPEC.md 39.5), and a hook below the
; dispatch would work on VGA and silently do nothing on Hercules.
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
; into the body - the bargain [vid_mono] and [wm_clip_n] already make. On
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
; SPEC.md 5.4.1's one-shot: the pair tables are derived from gfx_inktab, so
; they are built at run time rather than assembled - and the flag lives HERE,
; in .text with a real initialiser, because -f bin zeroes no .bss and this is
; read by the first blit the machine ever draws.
gfx_pairbuilt db 0

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
    mov [vga_lcol], ax          ; ax = left byte column
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
; A 1-row solid rect: gfx_fill clips and adapter-dispatches itself.
; (The former dedicated VRAM loop was retired for the code budget -
; gfx_fill produces identical pixels for one row.)
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

%ifdef KERN_BIG
    ; --- SPEC.md 39.14.5: the walk goes onto the display holding its CURRENT
    ; point, and the block is handed back in VIRTUAL coordinates.
    ;
    ; It is the staged copy that is translated, never the caller's: SPEC.md
    ; 48.14 has Missile Command computing with the block's x and y itself, and
    ; 5.6.7's replay-to-erase depends on the state meaning the same thing on
    ; every call. A block left display-local would compound the translation on
    ; the next step and put the caller's arithmetic in a space nothing else
    ; uses. This is the whole of what was missing: the walk wrote the primary's
    ; framebuffer at virtual coordinates, so a trail on the second display drew
    ; at the wrong offset on the FIRST one and stopped dead at its edge.
    cmp byte [vid_ndisp], 1
    jbe .nodisp
    push ax
    push bx
    mov ax, [gfx_ls_x]
    mov bx, [gfx_ls_y]
    call gfx_disp_enter         ; ...and it cannot fail: the dead zone answers
    mov [gfx_ls_x], ax          ; with the primary
    mov [gfx_ls_y], bx
    pop bx
    pop ax
.nodisp:
%endif
    cmp byte [vid_mono], 0         ; 5.6.4's split: mono gets the per-pixel walk,
    je .slow                    ; VGA is the fast machine and gets gfx_pixel
    cmp byte [vid_planes], 1
    jne .slow
    call gfx_lstep_mono
    jmp short .back
.slow:
    call gfx_lstep_slow
.back:
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1     ; ...and back into virtual space before the
    jbe .nodisp2                ; caller ever sees it
    push ax
    mov ax, [vid_ox]
    add [gfx_ls_x], ax
    mov ax, [vid_oy]
    add [gfx_ls_y], ax
    pop ax
    dec byte [gfx_dnest]
.nodisp2:
%endif
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
    sar ax, cl                  ; ARITHMETIC: a walk entering the screen from
    add di, ax                  ; the LEFT has x < 0 here, and `shr` puts that
                                ; 8,190 bytes along the row instead of one
                                ; byte before it. The address is not drawn
                                ; through until x reaches the box, and this is
                                ; what makes it right when it does - the bit
                                ; mask below already was, x & 7 being the
                                ; correct bit for any x
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
    jmp .out                    ; NOT `jmp short`: 39.14.9's virtual-space
                                ; scan put the armed branch out of reach
.armed:
%ifdef KERN_BIG
    ; SPEC.md 39.14.9: THE POINT IS DISPLAY-LOCAL HERE AND wm_clip_tab IS
    ; VIRTUAL. gfx_ls_one resolved the walk's display and translated before
    ; the walk ran, so the scan has to be done in virtual space and the rect
    ; it finds has to come back translated - which is 39.14.1's "below
    ; GFXCLIP and not above it" in the one place the hook could not be put
    ; below it. Untranslated, display 0 works (virtual IS local there) and
    ; every other display matches NO rect, takes the empty box below, and
    ; draws not one pixel while the walk advances normally.
    ; Both terms are 0 on display 0, so a one-display machine pays two adds.
    mov ax, si
    add ax, [vid_ox]
    mov [gfx_ls_vx], ax
    mov ax, dx
    add ax, [vid_oy]
    mov [gfx_ls_vy], ax
%else
    mov [gfx_ls_vx], si
    mov [gfx_ls_vy], dx
%endif
    mov bx, wm_clip_tab
    mov cx, [wm_clip_n]
.scan:
    mov ax, [bx + WCR_X1]
    cmp [gfx_ls_vx], ax
    jl .next
    mov ax, [bx + WCR_X2]
    cmp [gfx_ls_vx], ax
    jg .next
    mov ax, [bx + WCR_Y1]
    cmp [gfx_ls_vy], ax
    jl .next
    mov ax, [bx + WCR_Y2]
    cmp [gfx_ls_vy], ax
    jg .next
    ; ...and the survivor comes back in the DISPLAY's coordinates, clamped to
    ; the display: a clip rect may span the seam, and a box reaching past this
    ; card's last column would have gfx_lstep_mono addressing off the end of
    ; its own framebuffer.
    mov ax, [bx + WCR_X1]
    call gfx_ls_lx
    mov [gfx_ls_bx1], ax
    mov ax, [bx + WCR_Y1]
    call gfx_ls_ly
    mov [gfx_ls_by1], ax
    mov ax, [bx + WCR_X2]
    call gfx_ls_lx
    cmp ax, [vid_cwm1]
    jle .x2ok
    mov ax, [vid_cwm1]
.x2ok:
    mov [gfx_ls_bx2], ax
    mov ax, [bx + WCR_Y2]
    call gfx_ls_ly
    cmp ax, [vid_chm1]
    jle .y2ok
    mov ax, [vid_chm1]
.y2ok:
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

; gfx_ls_lx / gfx_ls_ly - one virtual coordinate into the ACTIVE display's,
; floored at 0. AX in, AX out; flags clobbered. Two names rather than one with
; an argument because each has exactly one origin to subtract and the caller
; is a straight run of four.
gfx_ls_lx:
%ifdef KERN_BIG
    sub ax, [vid_ox]
    jns .ok
    xor ax, ax
.ok:
%endif
    ret

gfx_ls_ly:
%ifdef KERN_BIG
    sub ax, [vid_oy]
    jns .ok
    xor ax, ax
.ok:
%endif
    ret


; -----------------------------------------------------------------------------
%ifdef KERN_BIG
; SPEC.md 5.6.4.1's three facts, in BH: which axis is the major one, whether
; the ink sets bits or clears them, and which way x runs. They pick the walk.
LF_STEEP    equ 1
LF_WHITE    equ 2
LF_XPOS     equ 4

; ...and the length below which the walk is not worth arriving at. The setup
; is ~1,700 cycles against the general walk's ~600, and a pixel is 152 against
; 723 (PERFORMANCE.md Set 69), so the two cross at about three pixels. Under
; it the general walk is CHEAPER and keeps the line, which is what stops this
; being a win on average and a regression on the short case.
LF_MINMAJ   equ 4

; GFX_LFWALK - one drawn run of SPEC.md 5.6.4.1's walk, and the whole reason
; there are eight of them: on an 8088 the three things that vary here are an
; OPCODE, a BRANCH CONDITION and an inc/dec, none of which a register can hold.
;
;   %1  1 = y-major (steep), 0 = x-major (shallow)
;   %2  1 = x increasing, 0 = x decreasing
;   %3  1 = white ink, 0 = black
;
; registers, on entry and throughout:
;   AX = d                  BL = the bit mask, INVERTED when the ink is black
;   DX = 2 * major delta    DI = the framebuffer byte
;   SI = 2 * minor delta    BP = vid_rowadd
;   CX = pixels to draw     ES = the framebuffer
;
; THE MASK ROTATES, IT DOES NOT SHIFT, and that is what lets one shape serve
; the inverted mask too: `ror`/`rol` put the leaving bit in CF, so the byte
; boundary is a carry rather than a zero. Going right the plain mask leaves a
; 1 exactly at 0x01 and the inverted one leaves a 0 exactly at 0xFE - the same
; test, read the other way round, which is the only thing %3 changes besides
; `or` against `and`.
; -----------------------------------------------------------------------------
%macro GFX_LFWALK 3
  %if %3
    %define LFOP or
    %define LFNW jnc                ; plain mask: the boundary IS the carry
  %else
    %define LFOP and
    %define LFNW jc                 ; inverted mask: the boundary is its absence
  %endif
  %if %2
    %define LFROT ror
    %define LFADJ inc di
  %else
    %define LFROT rol
    %define LFADJ dec di
  %endif
  %if %1
%%px:
    LFOP [es:di], bl
    or ax, ax                       ; d > 0: the minor axis steps
    jg %%xs
%%noxs:
    add ax, si                      ; d += 2*minor, and one row down - the
    add di, bp                      ; major axis steps every pixel, untested
    test di, [cs:vid_wrapbit]       ; gfx_nextrow, inlined
    jnz %%wrap
%%back:
    loop %%px
    clc
    ret
%%xs:
    sub ax, dx
    LFROT bl, 1
    LFNW %%noxs
    LFADJ
    jmp short %%noxs
%%wrap:
    add di, [cs:vid_wrapfix]
    jmp short %%back
  %else
%%px:
    LFOP [es:di], bl
    or ax, ax
    jg %%ys
%%noys:
    add ax, si
    LFROT bl, 1
    LFNW %%back
    LFADJ
%%back:
    loop %%px
    clc
    ret
%%ys:
    sub ax, dx
    add di, bp
    test di, [cs:vid_wrapbit]
    jnz %%wrap
    jmp short %%noys
%%wrap:
    add di, [cs:vid_wrapfix]
    jmp short %%noys
  %endif
  %undef LFOP
  %undef LFNW
  %undef LFROT
  %undef LFADJ
%endmacro
%endif


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
    cmp byte [vid_mono], 0         ; the same two tests gfx_line_raw makes
    je .passes
    cmp byte [vid_planes], 1
    jne .passes
    mov al, [gfx_color]
    call gfx_ink
    cmp al, 1
    je .passes
%ifdef KERN_BIG
    call gfx_lf_wide3           ; SPEC.md 5.6.6.1: ...and on this kernel the
    jnc .passes                 ; wide walk is the FALLBACK, not the win
%endif
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

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; gfx_lf_wide3 - would SPEC.md 5.6.4.1's walk take all THREE passes of this
;                dilated steep line?
;
; in:       the staged line, with x1/x2 already shifted one column LEFT for
;           pass 0, and 5.6.6's ink and adapter tests already passed
; out:      CF clear = yes, so 5.6.6's wide walk is not wanted
;           CF set   = not sure; take the wide walk
; clobbers: AX, BX, flags
;
; SPEC.md 5.6.6.1. 5.6.6's wide walk was an optimisation against 5.6.4's
; general loop and it still is - but three of 5.6.4.1's passes beat it, and
; not slightly: on a 193-pixel trail erase, the shape SPEC.md 48 actually
; draws, the wide walk is 40.5 ms and three fast passes are 20.3. So the wide
; walk is now what happens when the fast walk would REFUSE a pass, and the
; only question here is which.
;
; IT IS CONSERVATIVE IN THE SAFE DIRECTION. Both answers draw the identical
; pixels - 5.6.6's own argument is that its walk IS those three passes - so a
; wrong guess costs time and never a pixel, and "not sure" is spelled CF set.
; That is why a fragmented region and a second display are declined rather
; than reasoned about: the fast walk's box is a clip rect translated per
; fragment, and reproducing that rule here would be keeping a copy of
; 5.6.4.1's eligibility in a second place, which is the thing that drifts.
; -----------------------------------------------------------------------------
gfx_lf_wide3:
    cmp word [gfx_ln_dy], LF_MINMAJ ; too short to pay for the setup, three
    jl .no                          ; times over
    cmp byte [vid_ndisp], 1         ; 39.14: a fragment on a second display is
    ja .no                          ; translated, and this is not
    mov ax, [gfx_ln_x1]             ; the span all three passes together cover
    mov bx, [gfx_ln_x2]
    cmp ax, bx
    jle .ord
    xchg ax, bx
.ord:
    add bx, 2                       ; ...pass 2 being two columns right of 0
    cmp word [wm_clip_n], 0
    jne .armed
    or ax, ax                       ; disarmed: the box is the screen
    js .no
    cmp bx, [vid_cwm1]
    jg .no
    clc
    ret
.armed:
    cmp word [wm_clip_n], 1         ; one rect: the box is that rect, and the
    jne .no                         ; whole line meets it or none of it does
    cmp ax, [wm_clip_tab + WCR_X1]
    jl .no
    cmp bx, [wm_clip_tab + WCR_X2]
    jg .no
    clc
    ret
.no:
    stc
    ret
%endif

; -----------------------------------------------------------------------------
; gfx_line_raw - internal: one pass over the line, drawing only inside the
;                box in gfx_ln_c*. The Bresenham state is already staged.
; clobbers: everything but the segment registers
gfx_line_raw:
    cmp byte [vid_mono], 0         ; SPEC.md 5.6.4: mono IS the 4.77MHz machine
    je gfx_line_runs            ; and gets the per-pixel loop; VGA is the fast
    cmp byte [vid_planes], 1    ; one and gets major-axis runs, still fewer
    jne gfx_line_runs           ; calls than the app-side horizontal-only
                                ; coalescing this replaces
%ifdef KERN_BIG
    call gfx_line_fast          ; SPEC.md 5.6.4.1 - the same pixels, 4.9x, when
    jc gfx_line_mono            ; the line's shape lets the box be spent up
    ret                         ; front. CF set = it refused; walk it the long
                                ; way, which is what stays below. NOT ON
                                ; kern_small (5.6.4.4): it is 686 bytes of
                                ; .text and that machine is short of the thing
                                ; it would be spent from
%endif
; -----------------------------------------------------------------------------
; gfx_lm_pre - advance the walk to the box's top edge, drawing nothing
;
; in:       SI = x, DX = y, CX = err       out: the same, at y >= [gfx_ln_cy1]
;                                               (or at y2, if the line ends
;                                               above the box)
; clobbers: AX, flags
;
; SPEC.md 5.6.4.3. THE FRAMEBUFFER HAS NO ROW ABOVE ZERO and, on a banked
; adapter, not even the arithmetic for one: `gfx_rowbase(-5)` is not row 0
; minus a stride, it is bank 3 of a 16-bit product that wrapped, and stepping
; five rows down from it does not arrive at row 0 - measured, it arrives 90
; bytes past it, so a line entering the screen from the top drew four rows low
; on Hercules with nothing to say so. The walk skipped those pixels correctly
; and addressed the rest wrongly.
;
; So the fix is not in the addressing, it is here: every pixel above the box
; was going to be skipped anyway, and stepping the state without an address is
; what the walk should have done from the start. It costs the general path one
; call, and a line that starts inside the box - almost all of them - returns
; on the first compare.
; -----------------------------------------------------------------------------
gfx_lm_pre:
    cmp dx, [gfx_ln_cy1]
    jge .out
    cmp dx, [gfx_ln_y2]         ; ...and never past the end of the line
    jge .out
    mov ax, cx                  ; exactly .pixel's step, without the pixel
    add ax, ax
    mov [gfx_ln_e2], ax
    mov ax, [gfx_ln_dy]
    neg ax
    cmp [gfx_ln_e2], ax
    jle .noxs
    sub cx, [gfx_ln_dy]
    add si, [gfx_ln_sx]
.noxs:
    mov ax, [gfx_ln_e2]
    cmp ax, [gfx_ln_dx]
    jge gfx_lm_pre
    add cx, [gfx_ln_dx]
    inc dx
    jmp short gfx_lm_pre
.out:
    ret

; --- the 1bpp per-pixel walk - the GENERAL case (SPEC.md 5.6.4) --------------
gfx_line_mono:
    mov al, [gfx_color]
    call gfx_ink                ; AL = 00 black / FF white / 01 the dither
    mov [gfx_ln_ink], al
    mov es, [vid_rseg]
    mov si, [gfx_ln_x1]         ; SI = x, DX = y, CX = err
    mov dx, [gfx_ln_y1]
    mov cx, [gfx_ln_err]
    call gfx_lm_pre             ; ...to the box's top edge, if it starts above
    call gfx_ls_addr            ; DI = the byte, BL = its bit
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

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; SPEC.md 5.6.4.1 - the 1bpp walk with its state in REGISTERS
;
; PERFORMANCE.md Set 69 priced the general walk above at 723 cycles a pixel and
; said where every one of them goes: 170 in the per-pixel clip test, ~270 in a
; Bresenham whose every term is a word in .bss, ~134 in `call gfx_nextrow`, and
; 38 in the pixel. Forty instructions a pixel at 17.7 cycles each - and 17.7 is
; what a DIRECT MEMORY OPERAND costs on an 8088, not a slow instruction set.
;
; This is the same rasterisation with none of that. Three things buy it and
; they pay for each other:
;
;   SPLIT BY OCTANT. The major axis then steps EVERY pixel, so it needs no test
;   at all, and the minor axis's whole decision is the sign of one register:
;
;       steep:   d = 2dx - dy;  plot; if d>0 {x+=sx; d-=2dy}; d+=2dx; y++
;       shallow: d = 2dy - dx;  plot; if d>0 {y++;   d-=2dx}; d+=2dy; x+=sx
;
;   That is not an approximation of gfx_line_mono, it IS gfx_line_mono with the
;   invariants folded in - tools/os88linecost.py --model checks the two lay the
;   identical pixel set over 9,480 endpoint pairs, and tests/linetest is the
;   gate on the glass.
;
;   SPEND THE BOX AS AN INTERVAL. 5.6.3 refuses to clip because clipping the
;   ENDPOINTS moves the lattice; this moves neither endpoint. The pixels of a
;   monotone line inside an axis-aligned rect are a CONTIGUOUS range of major-
;   axis steps, so the box becomes a first and a count, computed once. The
;   minor axis is handled by refusing outright (below) rather than by testing.
;
;   INLINE THE ROW STEP. `add di,bp` and one test, against a call and a ret.
;
;   x AND y ARE THEN NOT TRACKED AT ALL - DI and BL carry the position - which
;   is what frees the two registers the interval made spare. The three are one
;   change; keeping the per-pixel test would put dx/dy back in .bss and the
;   modelled result is ~2.1x rather than 4.9x.
;
; WHAT IT REFUSES, and every refusal falls through to the walk above rather
; than drawing anything wrong:
;
;   - 5.6.6's dilated STEEP line, which is a three-column mask walk and a
;     different shape entirely;
;   - 39.4's DITHER ink, which is a per-pixel decision by definition;
;   - a line whose MINOR extent leaves the box. The major axis is clipped
;     exactly, the minor one is all-or-nothing, and that is the whole of the
;     compromise: a line crossing a clip rect's left or right edge (steep) or
;     its top or bottom (shallow) is walked the old way and costs what it
;     always did. Nothing is ever slower than before.
;
; in:       the staged Bresenham gfx_line_raw hands gfx_line_mono - x1/y1,
;           x2/y2, dx, dy, sx - and this pass's box in gfx_ln_c*
; out:      CF clear = drawn (which includes "no pixel of it is in this box").
;           CF set   = refused; the caller walks it.
; clobbers: everything but the segment registers
; -----------------------------------------------------------------------------
gfx_line_fast:
    cmp byte [gfx_ln_wide], 0   ; 5.6.6's three-column walk, not this shape
    jne .refuse
    ; ONE BYTE CARRIES THE THREE FACTS THE LOOPS ARE PICKED BY: which axis is
    ; the major one, whether the ink sets bits or clears them, and which way x
    ; runs. It is settled once here, in .bss, because the setup below needs
    ; every register it has - and it is read into BH for the walk, which is
    ; the one register no loop touches.
    xor bx, bx                  ; ...built in BL, where a bit is `or bl,imm` at
    mov al, [gfx_color]         ; four clocks rather than a read-modify-write
    call gfx_ink                ; on .bss at twenty-two. gfx_ink keeps BX
    cmp al, 1
    je .refuse                  ; the dither is a per-pixel decision (39.4)
    or al, al
    jz .blk
    or bl, LF_WHITE
.blk:
    cmp word [gfx_ln_sx], 0
    jl .xneg
    or bl, LF_XPOS
.xneg:
    mov ax, [gfx_ln_dx]
    cmp ax, [gfx_ln_dy]
    jae .shal

    ; --- y-major: the minor extent is the line's whole x span ---------------
    or bl, LF_STEEP
    mov ax, [gfx_ln_x1]
    mov cx, [gfx_ln_x2]         ; CX and not BX: BL is still the flags
    test bl, LF_XPOS
    jnz .xord
    xchg ax, cx                 ; AX = leftmost, CX = rightmost
.xord:
    cmp ax, [gfx_ln_cx1]
    jl .refuse
    cmp cx, [gfx_ln_cx2]
    jg .refuse
    mov [gfx_ln_flg], bl        ; ...and .span wants BX now
    mov ax, [gfx_ln_cy1]        ; the box, as a range of STEPS: step i is at
    sub ax, [gfx_ln_y1]         ; y1 + i, and 5.6.2 says i only ever grows
    mov bx, [gfx_ln_cy2]
    sub bx, [gfx_ln_y1]
    mov cx, [gfx_ln_dx]         ; minor delta
    mov dx, [gfx_ln_dy]         ; major delta
    jmp short .span
.refuse:
    stc
    ret

    ; --- x-major: ...and the minor extent is the whole y span, which 5.6.2
    ; has already ordered for us --------------------------------------------
.shal:
    mov ax, [gfx_ln_y1]
    cmp ax, [gfx_ln_cy1]
    jl .refuse
    mov ax, [gfx_ln_y2]
    cmp ax, [gfx_ln_cy2]
    jg .refuse
    mov [gfx_ln_flg], bl        ; ...and .span wants BX now
    mov ax, [gfx_ln_cx1]        ; step i is at x1 + sx*i, so walking LEFT the
    sub ax, [gfx_ln_x1]         ; box's interval is the same one mirrored
    mov bx, [gfx_ln_cx2]
    sub bx, [gfx_ln_x1]
    test byte [gfx_ln_flg], LF_XPOS
    jnz .sord
    neg ax
    neg bx
    xchg ax, bx
.sord:
    mov cx, [gfx_ln_dy]
    mov dx, [gfx_ln_dx]

    ; --- the step interval, and the staging both octants share --------------
    ; AX = lo, BX = hi, CX = minor delta, DX = major delta
.span:
    cmp dx, LF_MINMAJ           ; a line too short to pay for the setup goes
    jl .refuse                  ; the old way (5.6.4.2)
    or ax, ax                   ; first = max(0, lo)
    jns .lo
    xor ax, ax
.lo:
    cmp bx, dx                  ; last = min(major, hi)
    jle .hi
    mov bx, dx
.hi:
    mov [gfx_ln_first], ax
    sub bx, ax
    jl .empty                   ; no step of this line is inside this box
    inc bx
    mov [gfx_ln_cnt], bx
    mov si, cx                  ; the staged walk, into the registers the skip
    add si, si                  ; below wants it in - SI = 2 * minor...
    mov [gfx_ln_add2], si
    mov cx, ax                  ; ...CX = the steps to skip...
    mov ax, si
    sub ax, dx                  ; ...AX = d = 2*minor - major...
    add dx, dx                  ; ...DX = 2 * major
    mov [gfx_ln_sub2], dx

    ; --- the steps in front of the box, advanced but not drawn --------------
    ; The state and nothing else: no framebuffer, no mask, no address - so one
    ; loop serves both octants and both inks, at ~35 cycles a skipped step
    ; against the 723 the general walk spends testing each one.
    xor di, di                  ; DI = minor steps taken while skipping
    jcxz .at
.skip:
    or ax, ax
    jle .sn
    sub ax, dx
    inc di
.sn:
    add ax, si
    loop .skip
.at:
    mov [gfx_ln_d], ax          ; ...and gfx_ls_addr is about to take AX

    ; --- the entry pixel ----------------------------------------------------
    mov bh, [gfx_ln_flg]        ; BX is free from here, and BH survives
    mov si, [gfx_ln_x1]         ; gfx_ls_addr, which writes only BL
    mov dx, [gfx_ln_y1]
    mov ax, di
    test bh, LF_STEEP
    jz .esh
    add dx, [gfx_ln_first]      ; steep: y by the whole skip, x by the minor
    jmp short .ex               ; steps the skip actually took
.esh:
    add dx, ax                  ; shallow: the other way round
    mov ax, [gfx_ln_first]
.ex:
    test bh, LF_XPOS
    jnz .exp
    neg ax
.exp:
    add si, ax
    call gfx_ls_addr            ; DI = the byte, BL = the bit
    test bh, LF_WHITE
    jnz .ld
    not bl                      ; black CLEARS, so it carries the mask
.ld:                            ; inverted and plots with `and`
    mov es, [vid_rseg]
    mov bp, [vid_rowadd]
    mov ax, [gfx_ln_d]
    mov dx, [gfx_ln_sub2]
    mov si, [gfx_ln_add2]
    mov cx, [gfx_ln_cnt]
    test bh, LF_STEEP
    jz .lshal
    test bh, LF_XPOS
    jz .lsleft
    test bh, LF_WHITE
    jz .lsrb
    GFX_LFWALK 1, 1, 1
.lsrb:
    GFX_LFWALK 1, 1, 0
.lsleft:
    test bh, LF_WHITE
    jz .lslb
    GFX_LFWALK 1, 0, 1
.lslb:
    GFX_LFWALK 1, 0, 0
.lshal:
    test bh, LF_XPOS
    jz .lhleft
    test bh, LF_WHITE
    jz .lhrb
    GFX_LFWALK 0, 1, 1
.lhrb:
    GFX_LFWALK 0, 1, 0
.lhleft:
    test bh, LF_WHITE
    jz .lhlb
    GFX_LFWALK 0, 0, 1
.lhlb:
    GFX_LFWALK 0, 0, 0
.empty:
    clc
    ret
%endif


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
; and inside a clip region, without one line of adapter-specific code -
; because gfx_fill already does.
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
; lose to it on flat ones, and it would need its own 1bpp
; twins. It is a worthwhile future optimisation and it changes no caller;
; docs/PAINT-NOTES.md records why it was not the first one.
;
; Colour reduction on a 1bpp adapter happens in gfx_fill, per run (SPEC.md
; 39.4), so a 16-colour picture reads as black/dither/white there exactly as
; the rest of the UI does.
;
; ...and a DISPLAY SPLIT happens there too, per run, for the same reason
; (SPEC.md 39.14.7). This routine takes no whole-shape hook of its own: it
; keeps its destination in virtual coordinates and lets each run's gfx_fill
; do the splitting, which costs a straddling block nothing but the runs it
; was going to emit anyway. What that costs on a TWO-CARD machine is one
; gfx_disp_run per run instead of GFXDISP's two compares - a few per cent of
; a ~756us call - and on every one-card machine it costs nothing at all,
; because GFXDISP short-circuits on [vid_ndisp].
; -----------------------------------------------------------------------------
gfx_blit4:
    call cur_unlazy             ; off the clip list (SPEC.md 11.3): 7.1.4
                                ; ...but NOT off the display split, and there
                                ; is deliberately no GFXDENTER here: A RUN
                                ; CARRIES NO SOURCE (SPEC.md 39.14.7). The
                                ; destination stays VIRTUAL the whole way
                                ; down, and each coalesced run is one solid
                                ; gfx_hline that splits itself through
                                ; gfx_fill's own GFXDISP - so a straddling
                                ; block needs no source arithmetic at all.
                                ; Nested (gfx_dnest != 0) the runs take
                                ; GFXDISP's local path, which is what an
                                ; outer whole-shape hook wants.
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov byte [gfx_blit_fst], 0  ; BEFORE the two guards below, or a call that
                                ; draws nothing inherits the last one's answer
                                ; and .out resets a VGA that was never armed
    or cx, cx
    jz .out
    or dx, dx
    jz .out

%ifdef KERN_BIG
    ; --- THE WHOLE-SHAPE HOOK, TAKEN WHEN THE BLOCK FITS ONE DISPLAY -------
    ; SPEC.md 39.14.7.1. This is the gate 39.14.7 considered and declined,
    ; and the field priced what declining it cost: a two-display machine sent
    ; EVERY blit down .slow, which is not "per-run gfx_disp_run instead of
    ; two compares" - it is giving up 5.4.1's fast path altogether, so each
    ; coalesced run became a full gfx_fill at its own ~756us arrival instead
    ; of a direct write into the framebuffer.
    ;
    ; Only AX/BX need translating: CX and DX are a WIDTH and a HEIGHT, which
    ; no display origin can move. That is why there is no GFXDORG here and
    ; why this costs one macro rather than the "second path through the
    ; routine" 39.14.7 weighed - the straddling case below is not a second
    ; path, it is the one that was already there.
    mov byte [gfx_blit_hk], 0
    cmp byte [vid_ndisp], 1
    jbe .hkdone                 ; one display: nothing to decide, and this is
                                ; every machine that was already fast
    cmp byte [gfx_dnest], 0
    jne .hkdone                 ; nested: an outer whole-shape hook owns the
                                ; translation and .slow is what it wants
    push cx
    push dx
    add cx, ax                  ; vid_span_one takes an INCLUSIVE rect and
    dec cx                      ; this routine takes a width and a height
    add dx, bx
    dec dx
    call vid_span_one
    pop dx
    pop cx
    jc .hkdone                  ; STRADDLING: leave the destination virtual
                                ; and let each run split itself, which is
                                ; 39.14.7's mechanism and still correct
    mov byte [gfx_blit_hk], 1
    GFXDENTER                   ; ...one display, so the block is local now
.hkdone:
%endif

    mov [gfx_blit_w], cx        ; lock-held scratch, like vga12's rect block:
    mov [gfx_blit_x], ax        ; every drawing entry runs under the gfx lock
    mov [gfx_blit_h], dx        ; (SPEC.md 1 rule 6), so one copy serves
    mov [gfx_blit_sv], si       ; - and AX is the TRANSLATED x by here

    ; --- may a run go straight into the framebuffer? (SPEC.md 5.4.1) -------
    cmp word [wm_clip_n], 0
    jne .slow                   ; a region is armed, and each run has to clip
                                ; itself against it - which only gfx_fill does
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe .dsp1                   ; one display: the fast path as it always was
    cmp byte [gfx_blit_hk], 0   ; two, and we took the hook above: the block
    je .slow                    ; is display-local and one display is current,
                                ; so the fast path is exactly right. Without
                                ; it - straddling, or nested by somebody else
                                ; - a run splits itself through gfx_fill's own
                                ; GFXDISP (SPEC.md 39.14.7)
.dsp1:
%endif
    cmp byte [vid_mono], 0
    je .vga
    or ax, ax                   ; 1 = THE ROW DECODER (SPEC.md 5.4.1), which
    js .slow                    ; does not clip in x: a left clip advances the
    add ax, cx                  ; source by a pixel count that may be ODD, and
    cmp ax, [vid_cw]            ; that shifts the nibble phase of every pair
    ja .slow                    ; after it. Every caller clips its own blit
    mov byte [gfx_blit_fst], 1
    cmp byte [gfx_pairbuilt], 0
    jne .slow
    call sw_pairbuild           ; once a boot, from gfx_inktab
    jmp short .slow
.vga:
    mov byte [gfx_blit_fst], 2  ; 2 = the planar SPAN writer, whose Enable
    call vga_sr_on              ; Set/Reset is armed ONCE here and cleared at
                                ; .out: only the colour and the bit mask
                                ; change per run
.slow:
.row:
    mov si, [gfx_blit_sv]       ; ES:SI = this row's first byte
    cmp byte [gfx_blit_fst], 0
    je .rowd
    cmp bx, [vid_ch]            ; UNSIGNED, so a negative y is caught too: the
    jae .nextrow                ; row is off screen and nothing below it clips
    push ax                     ; any more
    mov ax, bx
    call gfx_rowbase            ; once a ROW, where gfx_fill did it once a RUN.
                                ; BEFORE the `xor dx, dx` below and not after:
                                ; gfx_rowbase ends in a `mul`, so it WRITES DX -
                                ; zero for every geometry this kernel has, which
                                ; is exactly the kind of luck a row cursor
                                ; should not be resting on
    mov [gfx_blit_ro], ax
    mov ax, bx                  ; ...and this row's pair table: the two pixels
    add ax, [gfx_blit_x]        ; of a pair are always opposite parities, so
    mov dx, gfx_pairtab0        ; (x + y) & 1 picks the table and the dither
    test al, 1                  ; then costs the loop nothing (SPEC.md 39.4)
    jz .par
    mov dx, gfx_pairtab1
.par:
    mov [gfx_blit_tb], dx
    pop ax
    cmp byte [gfx_blit_fst], 1
    jne .rowd
    call sw_blit_row            ; THE WHOLE ROW, no run scan at all
    jmp .nextrow                ; (near: the scan below is longer than 127)
.rowd:
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
    cmp byte [gfx_blit_fst], 2  ; (which is where the next run starts) 2 = the
    je .emitv                   ; planar span writer; 0 = gfx_fill as it always
    call gfx_blit_run           ; was. 1 never gets here - the decoder above
    jmp short .emitd            ; draws the whole row and skips the scan
.emitv:
    call vga_blit_span
.emitd:
    cmp dx, [gfx_blit_w]
    jb .run

.nextrow:
    mov ax, [gfx_blit_sv]       ; next source row
    add ax, bp
    mov [gfx_blit_sv], ax
    inc bx                      ; next destination row
    dec word [gfx_blit_h]
    jnz .row
.out:
    cmp byte [gfx_blit_fst], 2  ; the planar writer left Enable Set/Reset on
    jne .done                   ; for the whole call (SPEC.md 5.4.1)
    call vga_gc_reset
.done:
%ifdef KERN_BIG
    cmp byte [gfx_blit_hk], 0   ; ...and the nest comes down only if this call
    je .nohk                    ; put it up: the two guards above can reach
    GFXDLEAVE                   ; .out without ever having taken the hook
.nohk:
%endif
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_sr_on - internal: Enable Set/Reset on all four planes, and nothing else
;
; gfx_blit4's planar fast path arms this ONCE for the whole call and clears it
; with vga_gc_reset at the end (SPEC.md 5.4.1). vga_set_color writes GC0 and
; GC1 together, which is right for a primitive that arrives once and is a
; wasted `out` per RUN here - the colour is the only half that changes.
; -----------------------------------------------------------------------------
vga_sr_on:
    push ax
    push dx
    mov dx, VGA_GC
    mov ax, 0x0F01              ; GC1: enable Set/Reset on all planes
    out dx, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_blit_span - gfx_blit_run's direct twin on VGA (SPEC.md 5.4.1)
; in:  DI = the run's first x, DX = one past its last pixel (row-relative),
;      AH = its colour, [gfx_blit_x] = the block's left edge, [gfx_blit_ro] =
;      the row's base byte, GC1 already armed by vga_sr_on, ES = the CALLER's
;      source segment, DF clear
; out: nothing; every register preserved, ES included
;
; sw_blit_span's planar half, and the same argument: gfx_hline is a gfx_fill and
; a gfx_fill is SPEC.md 5.7's per-call floor before a byte is written, per RUN.
;
; What a run costs here instead is TWO to FOUR `out`s and as many writes. The
; four planes take their data from Set/Reset rather than from the CPU byte, so
; a run is one colour write (GC0) and then the same masked-byte shape the 1bpp
; writer has - which is why the interior can be a `rep stosb` of a byte whose
; VALUE is irrelevant, exactly as gfx_fill's own interior is.
;
; Enable Set/Reset (GC1) is NOT written here: it does not change between runs,
; and hoisting it is the difference between two `out`s a run and three.
; -----------------------------------------------------------------------------
vga_blit_span:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, ah                  ; the documented contract: gfx_blit4 leaves
    mov [gfx_color], al         ; [gfx_color] holding the last colour drawn
    mov ax, di                  ; AX = x1
    mov cx, [gfx_blit_x]
    add cx, dx
    dec cx                      ; CX = x2, the last pixel drawn
    or ax, ax                   ; x1 < 0 -> 0
    jns .x1ok
    xor ax, ax
.x1ok:
    cmp cx, [vid_cw]            ; x2 past the last column -> the last column
    jl .x2ok
    mov cx, [vid_cwm1]
.x2ok:
    cmp cx, ax
    jl .out                     ; entirely off the screen (or an empty run)
    mov si, ax                  ; BH = left edge mask, BL = right, by table
    and si, 7
    mov bh, [si + vga_lmtab]
    mov si, cx
    and si, 7
    mov bl, [si + vga_rmtab]
    shr cx, 1                   ; CX = right byte column
    shr cx, 1
    shr cx, 1
    shr ax, 1                   ; AX = left byte column
    shr ax, 1
    shr ax, 1
    mov di, [gfx_blit_ro]
    add di, ax                  ; DI = the run's first byte
    sub cx, ax
    mov si, cx                  ; SI = byte span; 0 = inside one byte
    mov dx, VGA_GC
    mov ah, [gfx_color]
    xor al, al                  ; GC0: Set/Reset = this run's colour
    out dx, ax
    mov ax, VGA_SEG
    mov es, ax                  ; ...and only NOW is ES not the source
    or si, si
    jnz .multi
    mov ah, bh                  ; one byte: the two edges are one mask
    and ah, bl
    mov al, 8
    out dx, ax
    mov al, [es:di]             ; the read loads the latches; the write is what
    mov [es:di], al             ; the ALU and the bit mask act on
    jmp short .out
.multi:
    mov ah, bh                  ; --- the left edge byte
    mov al, 8
    out dx, ax
    mov al, [es:di]
    mov [es:di], al
    inc di
    dec si
    jz .right
    mov ax, 0xFF08              ; --- whole bytes: latches irrelevant, and the
    out dx, ax                  ; CPU byte with them - Set/Reset supplies all
    mov cx, si                  ; four planes, so AL's value does not matter
    rep stosb
.right:
    mov ah, bl                  ; --- and the right edge byte
    mov al, 8
    out dx, ax
    mov al, [es:di]
    mov [es:di], al
.out:
    pop es
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
; A 1-column solid rect via gfx_fill (which clips and adapter-
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
    cmp byte [vid_mono], 0      ; 1bpp? then the software renderer IS
    jne sw_fill
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
    mov dx, ROW_BYTES           ; DX = the row step LESS what the rep advances
    sub dx, bx                  ; DI by, so one add ends the row (SPEC.md 5.7.1)
.irow:
    mov cx, bx
    rep stosb
    add di, dx
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
    cmp byte [vid_mono], 0      ; 1bpp? then the software renderer IS
    jne sw_fill_gray
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
    mov ah, bl                  ; the row pattern moves to AH so BX can carry
    mov bx, cx                  ; the count the rep destroys, and DX the row
    mov dx, ROW_BYTES           ; step less that count (SPEC.md 5.7.1)
    sub dx, bx
.irow:
    mov al, ah
    mov cx, bx
    rep stosb
    add di, dx
    not ah
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
; is a pattern rather than a solid colour. Same masked-edge scheme as
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
    cmp byte [vid_mono], 0      ; 1bpp? then the software renderer IS
    jne sw_fill_pat
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
    mov dx, cx                  ; DX = the count, which the rep destroys; BX is
    mov bx, [vga_y1]            ; the pattern row and BP is not ours to spend,
    and bx, 7                   ; so the row start is unwound (SPEC.md 5.7.1)
.irow:
    mov al, [vga_patbuf+bx]
    mov cx, dx
    rep stosb
    sub di, dx
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
                                ; (...and each strip meets GFXCLIP, whose
                                ; clipped branch owes no hide: wm_clip_set
                                ; already asked cur_lazyck - 7.1.4)
    call cur_unlazy             ; UNCLIPPED: 7.1.4/7.1.5
    jmp short gfx_xor_rect_d

vga_xor_rect_vram:
    call cur_unlazy             ; the drag outline bypasses the clip
                                ; (SPEC.md 11.3): 7.1.4. VRAM body under its
                                ; own name, which the transient overlays call
                                ; direct - so it owes the hide on its own
                                ; account. Reached from above the flag is
                                ; already spent and this is three instructions
                                ; (cur_unlazy is one-way within a hold)

; SPEC.md 39.14.6: THE DISPLAY HOOK GOES ABOVE THE [vid_mono] DISPATCH, and
; the reason is the same one SPEC.md 11.3 gives for the clip hook - except
; that here getting it the wrong way round does not merely fail to draw.
; `[vid_mono]` is not a property of the MACHINE on a two-card desktop, it is a
; property of the display the renderer is pointed at, and only GFXDENTER
; points it. Tested first, it answers about whichever display the LAST
; primitive happened to leave current (39.14.3 deliberately does not restore
; one), so a rect on the VGA dispatched while the Hercules was still active
; chose the SOFTWARE renderer - and then GFXDENTER switched the live block to
; the VGA underneath it, where [vid_rseg] is 0. sw_col then wrote through
; ES:DI = 0960:8FC1, which is COLD_SEG:45C1: the file manager's own code.
; Measured, one click on a Disk window with a straddling window over it left
; 34 differing runs in .cold at a stride of 80 - a scan line - and what it
; overwrote next ran: a `call far` became `cbw`, DS went to 0, fm_layout's
; stores landed 0x600 low across ui_rebootq, and the machine rebooted or
; froze (docs/DUAL-DISPLAY-VGA.md 8(11), docs/FIELD-NOTES.md).
;
; It is whole-shape rather than split, for gfx_blit4's reason: an outline is
; not the intersection of its bounding rect with anything (see the clipped
; path below), and the four strips it decomposes into - either renderer's -
; are already inside the nest by the time they are drawn. The frame is OUT
; HERE, where gfx_line and gfx_scroll keep theirs: the hook translates AX/BX
; and GFXDORG CX/DX and nothing puts them back (39.14.2), so without it a
; primitive whose contract is `out: -` answers DISPLAY-LOCAL coordinates.
gfx_xor_rect_d:
    push ax
    push bx
    push cx
    push dx
    GFXDENTER
    GFXDORG cx, dx
    call gfx_xor_rect_raw
    GFXDLEAVE
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; gfx_xor_rect_raw - internal: the outline, in the ACTIVE display's own
;                    coordinates and on whichever renderer that display wants
; clobbers: flags
gfx_xor_rect_raw:
    cmp byte [vid_mono], 0      ; 1bpp? then the software renderer IS
    jne sw_xor_rect             ; the direct path (SPEC.md 39.5), and it banks
                                ; its own registers
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
; The same decomposition sw_xor_rect uses, with the same degenerate cases -
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
    cmp byte [vid_mono], 0      ; 1bpp? then the software renderer IS
    jne sw_xor_fill
vga_xor_fill_vram:
    call cur_unlazy             ; ...and the menu highlights with it. VRAM
                                ; body under its own name (see
                                ; vga_xor_rect_vram)
    cmp byte [vid_mono], 0
    jne sw_xor_fill
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
    mov dx, ROW_BYTES           ; the row step less what the byte loop advances
    sub dx, bx                  ; DI by (SPEC.md 5.7.1)
.irow:
    mov cx, bx
.ibyte:
    mov al, [es:di]             ; every interior byte too
    stosb
    loop .ibyte
    add di, dx
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
; Dispatches to sw_save on a 1bpp adapter (SPEC.md 39.5). vga_save_vram is
; the VRAM body under its own name: the mouse cursor's save-under calls it
; directly, without cur_unlazy.
; -----------------------------------------------------------------------------
gfx_save:
    call cur_unlazy             ; the menu save-under READS the screen, so
                                ; the arrow must not be in what it banks
    push ax                     ; the rect is banked around the hook, which
    push bx                     ; translates it and never puts it back - DI is
    push cx                     ; NOT, because DI advancing is this routine's
    push dx                     ; documented answer
    GFXDENTER                   ; ...and it reads it off ONE CARD (SPEC.md
    GFXDORG cx, dx              ; 39.14.8): a save is pixels, and pixels live
    call vga_save_vram          ; on a card rather than in a coordinate space,
    GFXDLEAVE                   ; so this is whole-shape like gfx_scroll and
    pop dx                      ; NOT split like a blit's runs. The caller owes
    pop cx
    pop bx
    pop ax
    ret
                                ; the straddle test - wm_su_take asks
                                ; vid_span_one - because a cut save and a cut
                                ; restore agree with each other and leave the
                                ; rest of the rect stale.
                                ;
                                ; BELOW the hook and reached directly by the
                                ; CURSOR out of IRQ4, which must not take it:
                                ; the arrow brackets itself (39.15.2), and a
                                ; hook here would translate a rect that
                                ; cur_geom has already resolved.
vga_save_vram:
    cmp byte [vid_mono], 0      ; the cursor's save-under, from inside IRQ4:
    jne sw_save                 ; on mono the framebuffer IS the renderer's
                                ; target, so one "plane" of sw_xfer is it
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
; --- the SUB-RECT restore (SPEC.md 5.8) --------------------------------------
;
; Three words that make gfx_restore put back PART of a buffer instead of all of
; it. They are in .text with real initialisers, not .bss: `-f bin` zeroes
; nothing, and the mouse cursor reaches vga_restore_vram on the machine's first
; pointer move, long before anything arms these (the cur_sptr trap, SPEC.md
; 7.1.2).
;
; ALL THREE ARE 0 AT REST, which is what makes the whole feature additive: the
; two restore bodies read them unconditionally and a zero skip is the walk they
; had, so the cursor's own path and the menu save-under are byte-identical.
gfx_sub_st  dw 0                ; buffer offset of the sub-rect's first byte
gfx_sub_rs  dw 0                ; ...bytes to step the BUFFER past each row
gfx_sub_ps  dw 0                ; ...and past each plane

; -----------------------------------------------------------------------------
; gfx_sub_arm - arm the next gfx_restore to put back only part of its buffer
; in:  AX = x1, BX = y1, CX = x2, DX = y2 - the sub-rect wanted; SI -> the four
;      words (x1, y1, x2, y2) the buffer was SAVED over
; out: CF = 0, armed; CF = 1 = refused and nothing armed, so the caller's
;      gfx_restore puts the whole buffer back - which is always correct, only
;      dearer. Preserves every register.
;
; gfx_save's buffer is plane-major, `full_row` bytes per row, x1 rounded down
; and x2 up (SPEC.md 5), so a sub-rect is three numbers: where its first byte
; is, how far past the end of each row its next row starts, and the same past
; each plane. The SCREEN side needs nothing at all - vga_rect_setup recomputes
; the geometry from whatever rect it is handed, so the caller passes the
; sub-rect and it lands in the right place.
;
; IT REFUSES RATHER THAN CLAMPS a sub-rect that is not inside the saved one:
; clamping would silently restore something the caller did not ask for, and
; every refusal has a correct fallback one branch away.
; -----------------------------------------------------------------------------
gfx_sub_arm:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp ax, [si]                ; inside the saved rect on all four sides, or
    jl .no                      ; the arithmetic below indexes outside the
    cmp bx, [si+2]              ; buffer - which is a claim of a few KB, so it
    jl .no                      ; would be somebody else's memory
    cmp cx, [si+4]
    jg .no
    cmp dx, [si+6]
    jg .no
    cmp ax, cx                  ; ...and not degenerate
    jg .no
    cmp bx, dx
    jg .no

    ; The three words double as the scratch this arithmetic needs, so it costs
    ; no .bss: every register is wanted, CL has to be the shift count (an 8086
    ; shifts by 1 or by CL and nothing else), and both `mul`s eat DX.
    mov [gfx_sub_st], ax        ; S.x1
    mov [gfx_sub_rs], cx        ; S.x2
    mov [gfx_sub_ps], dx        ; S.y2
    push bx                     ; S.y1
    mov cl, 3

    mov ax, [si+4]              ; DI = full_row, the buffer's bytes per row;
    shr ax, cl                  ; BX = the saved rect's own left byte column
    mov bx, [si]
    shr bx, cl
    mov di, ax
    sub di, bx
    inc di

    mov ax, [gfx_sub_rs]        ; BP = sub_row, the bytes to move per row
    shr ax, cl
    mov bp, ax
    mov ax, [gfx_sub_st]
    shr ax, cl
    sub bp, ax
    inc bp
    sub ax, bx                  ; AX = the sub-rect's byte column within a
                                ; buffer row

    pop bx                      ; CX = sub_rows, BX = the sub-rect's first row
    mov cx, [gfx_sub_ps]        ; as an offset into the buffer. (CL was the
    sub cx, bx                  ; shift count and every shift is done.)
    inc cx
    sub bx, [si+2]

    push ax                     ; start = that row x full_row + that column.
    mov ax, bx                  ; No overflow to worry about: wm_su_kb has
    mul di                      ; already refused a buffer bigger than one
    pop bx                      ; segment, so every offset in it fits a word
    add ax, bx
    mov [gfx_sub_st], ax

    mov ax, di                  ; row_skip: what the row loop must step past
    sub ax, bp                  ; after each rep movsb
    mov [gfx_sub_rs], ax

    mov ax, [si+6]              ; plane_skip: the saved rows this rect does not
    sub ax, [si+2]              ; cover, one whole buffer row each
    inc ax
    sub ax, cx
    mul di
    mov [gfx_sub_ps], ax
    clc
    jmp short .out
.no:
    stc
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
; gfx_sub_off - back to restoring whole buffers
; Called by both restore bodies on their way out, so a caller CANNOT leave one
; armed: the safe value is the one a forgetful caller inherits, which is
; wm_dmg_dk's rule (SPEC.md 11.91). Preserves every register; clobbers flags,
; which both callers have already finished with.
; -----------------------------------------------------------------------------
gfx_sub_off:
    mov word [gfx_sub_st], 0
    mov word [gfx_sub_rs], 0
    mov word [gfx_sub_ps], 0
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
; Dispatches to sw_restore on a 1bpp adapter (SPEC.md 39.5);
; vga_restore_vram is the VRAM body for the cursor path (see gfx_save).
; -----------------------------------------------------------------------------
gfx_restore:
    call cur_unlazy             ; ...and writes it back, unclipped
    push ax                     ; same frame as gfx_save's, and SI is outside
    push bx                     ; it for the same reason: SI advancing is what
    push cx                     ; this routine answers
    push dx
    GFXDENTER                   ; ...onto the card it came off (SPEC.md
    GFXDORG cx, dx              ; 39.14.8) - gfx_save's twin, and it has to be
    call vga_restore_vram       ; the same hook or a rect banked on one display
    GFXDLEAVE                   ; could be put back on the other
    pop dx
    pop cx
    pop bx
    pop ax
    ret
vga_restore_vram:
    cmp byte [vid_mono], 0      ; see vga_save_vram
    jne sw_restore
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    call vga_rect_setup
    jc .done
    add si, [gfx_sub_st]        ; the SUB-RECT restore (SPEC.md 5.8), and 0 at
                                ; rest: DS is still KERNEL_SEG out here, so
                                ; this one needs no override
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
    add si, [cs:gfx_sub_rs]     ; CS: IS MANDATORY - DS is the BUFFER in here
    add di, ROW_BYTES
    sub di, bp
    dec dx
    jnz .row
    pop es
    pop ds
    add si, [gfx_sub_ps]        ; ...and past the rows this rect does not cover
                                ; (DS is KERNEL_SEG again by here)
    mov al, [vga_plane]
    add al, al                  ; next plane bit
    mov [vga_plane], al
    cmp al, 0x10
    jb .plane
    mov dx, VGA_SEQ
    mov ax, 0x0F02              ; SEQ2: Map Mask back to all planes
    out dx, ax
.done:
    call gfx_sub_off            ; a sub-rect is ONE restore (SPEC.md 5.8)
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
; -----------------------------------------------------------------------------
; thm_bg / thm_ink - set the pen to the chrome's background / ink (SPEC.md 76)
; in:  -   out: [gfx_color]   clobbers: nothing (flags preserved)
;
; A near call is THREE bytes where `mov byte [gfx_color], CWHITE` is five, so
; the substitution pays for these two routines several times over. Flags are
; preserved because several chrome sites set the pen between a CF-setting call
; and the branch that reads it (wm_draw_title's wm_flush, SPEC.md 11.95.2).
; -----------------------------------------------------------------------------
%ifndef OS88_THEME              ; NO THEME HERE (SPEC.md 76.13): the pens keep
thm_bg:                         ; their names and become CONSTANTS, so every
    mov byte [gfx_color], CWHITE; chrome site in the kernel is untouched - and
    ret                         ; a 3-byte call is still shorter than the
thm_ink:                        ; 5-byte immediate store it replaced, so the
    mov byte [gfx_color], CBLACK; small build comes out AHEAD of the tree that
    ret                         ; never had a theme. Flags survive: neither
%else                           ; instruction writes any
thm_bg:
    pushf
    push ax
    mov al, [thm_bgc]
    mov [gfx_color], al
    pop ax
    popf
    ret
thm_ink:
    pushf
    push ax
    mov al, [thm_inkc]
    mov [gfx_color], al
    pop ax
    popf
    ret

; ...and the ACTIVE TITLE BAR's pair (SPEC.md 76.12). Equal to the two above in
; both 1bpp themes, so a build that only ever had one pair draws the same
; pixels; under Color they are white-on-blue, which is the whole of what makes
; focus visible at a glance.
thm_tbg:
    pushf
    push ax
    mov al, [thm_tbgc]
    mov [gfx_color], al
    pop ax
    popf
    ret
thm_tink:
    pushf
    push ax
    mov al, [thm_tinkc]
    mov [gfx_color], al
    pop ax
    popf
    ret
%endif

%ifndef OS88_THEME              ; ...and with no theme the ACTIVE pair IS the
thm_tbg     equ thm_bg          ; chrome pair, aliased rather than %ifdef'd at
thm_tink    equ thm_ink         ; each of wm_draw_title's six sites - which is
%endif                          ; where a divergence would rot (SPEC.md 76.13)

; thm_pair - AL = chrome ink, AH = chrome background, for font_run
; thm_rpair - the INVERSE pair (SPEC.md 59's toast strip)
; The three theme bytes are declared ADJACENT for this: [thm_bgc] read as a
; WORD is already the inverse pair, so thm_rpair is one instruction and
; thm_pair is that plus an xchg.
%ifndef OS88_THEME
thm_pair:
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = background
    ret
thm_rpair:
    mov ax, (CBLACK << 8) | CWHITE
    ret
%else
thm_pair:
    mov ax, [thm_bgc]
    xchg al, ah
    ret
thm_rpair:
    mov ax, [thm_bgc]
    ret
%endif

; thm_desk - lay the desktop's ground over AX,BX..CX,DX (SPEC.md 76.2)
; Bright is SPEC.md 26's 50% dither; Dark is solid black. gfx_fill_gray is a
; CLIPPED primitive and so is gfx_fill, so wm_dmg_gray's region arithmetic is
; unchanged either way.
%ifndef OS88_THEME
thm_desk:
    jmp gfx_fill_gray           ; SPEC.md 26's dither, and the only ground
%else                           ; this build has
thm_desk:
    push ax
    mov al, [thm_deskc]
    cmp al, THM_DITHER
    je .dither
    mov [gfx_color], al
    pop ax
    jmp gfx_fill
.dither:
    pop ax
    jmp gfx_fill_gray
%endif

; thm_set - adopt AL (0/1) as the theme and resolve the two pens
%ifdef OS88_THEME
thm_set:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    cmp al, THM_NKIND           ; a byte from a future version, or from a
    jb .known                   ; settings disk this kernel does not know
    xor al, al
.known:
%ifdef KERN_BIG
    cmp al, THM_COLOR           ; COLOUR IS A FACT ABOUT THE ADAPTER, and it is
    jne .ok                     ; refused HERE rather than at the two callers
    cmp byte [vid_kind], VID_VGA; (SPEC.md 76.12.1) - so a settings disk
    je .ok                      ; carried to a Hercules machine, and SPEC.md
    xor al, al                  ; 39.11's Display page moving this one onto a
.ok:                            ; card with no colour in it, both land on the
%endif                          ; same refusal and both fall back to Bright.
                                ; On kern_small THM_NKIND is 2, so the range
                                ; test above has already clamped it
    mov [thm_kind], al
    mov bl, al                  ; the row: index * 8, the row being padded to
    xor bh, bh                  ; 8 for exactly this
    mov cl, 3
    shl bx, cl
    add bx, thm_tab
    push ds                     ; ONE copy, so the live block cannot fall out
    pop es                      ; of step with the table field by field
    mov si, bx
    mov di, thm_bgc
    mov cx, 8
    cld
    rep movsb
    call menu_force             ; THE TWO DIFFING PAINTERS CANNOT SEE THIS
    call dock_force             ; (SPEC.md 76.7). menu_bcell is a record of
    pop es                      ; CHARACTERS and a dock tile's key is its icon
    pop di                      ; plus its marks - a theme change moves
    pop si                      ; NEITHER, so composing IS the diff finds no
    pop cx                      ; difference and the bar and the strip keep
    pop bx                      ; the colours they had. wm_paint_all does not
    pop ax                      ; cover it: it calls both painters and both
    ret                         ; are gated

%endif

; THESE TWO ARE CONTENT PENS AND THE THEME DOES NOT REACH THEM (SPEC.md 76.6).
; Every caller but one draws inside a WINDOW - the Control Panel's pages, the
; Standard File dialog, os88ui.inc's shared controls, and every package
; through OSAPI_GFX_PEN - and a window's content is white in both themes. The
; one caller that is chrome is the pull-down menu, and it has its own pair
; below rather than bending these: themed here, the Control Panel's radios
; and every package's greyed caption go white on white and vanish.
gfx_pen_cf:
    jc gfx_pen_dis
gfx_pen_live:
    mov byte [gfx_color], CBLACK
    mov byte [gfx_dis], 0
    ret
gfx_pen_dis:
    mov byte [gfx_color], CDGRAY
    mov byte [gfx_dis], 1
    mov byte [gfx_disink], CBLACK   ; content is white in both themes, so a
    ret                             ; greyed caption stipples in black

; ...and the CHROME's pair, for menu_drop's items (SPEC.md 76.6). Same shape,
; same [gfx_dis] contract - only the two colours come from the theme, so on
; Bright they are byte-for-byte what the pair above does - which is why the
; unthemed build simply ALIASES them to it (SPEC.md 76.13).
%ifndef OS88_THEME
thm_pen_live    equ gfx_pen_live
thm_pen_dis     equ gfx_pen_dis
%else
thm_pen_live:
    push ax
    mov al, [thm_inkc]
    mov [gfx_color], al
    pop ax
    mov byte [gfx_dis], 0
    ret
thm_pen_dis:
    push ax
    mov al, [thm_disc]          ; CDGRAY on white is a legible grey and on
    mov [gfx_color], al         ; black is very nearly nothing, so Dark takes
    mov al, [thm_inkc]          ; CLGRAY. That is the VGA answer; on 1bpp the
    mov [gfx_disink], al        ; flag buys a checkerboard and [gfx_disink]
    pop ax                      ; says what it is laid ON - this surface's own
    mov byte [gfx_dis], 1       ; live ink, so black on Bright and white on
    ret                         ; Dark (SPEC.md 76.6.1)
%endif

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
    je .free
    mov byte [gfx_lock_want], 1 ; SPEC.md 7.3: say so BEFORE the sti, inside
                                ; the cli window that read the flag - a store
                                ; after it can land behind the very unlock it
                                ; was meant to be seen by
.block:
    sti
%ifndef NOPARKLK
    call inst_park_lk           ; BLOCKED HERE HOLDS NOTHING, if the package
%endif                          ; said so (SPEC.md 66.5.4) - which is what
    call task_yield             ; lets the compactor reach a DRAWING worker's
%ifndef NOPARKLK                ; claims at all. make HEAPPARKLK=0 is the A/B
    call inst_park_unlk
%endif
    jmp .retry

.free:                          ; the flag is clear - but SPEC.md 7.3 asks one
    push ax                     ; more question first. The save and the
    mov al, [sch_cur]           ; [sch_cur] read are .take's, made one step
    cmp byte [gfx_lock_want], 0 ; early and shared, so the fast path pays a
    je .take                    ; compare and a branch and nothing else
    cmp al, [gfx_lock_last]     ; somebody IS blocked. Am I the task that just
    jne .take                   ; let go? Then taking it again here IS the
    pop ax                      ; starvation: a worker that unlocks and
    mov byte [gfx_lock_want], 0 ; re-locks inside one slice wins every race
    jmp short .block            ; against a waiter that has to be SCHEDULED
                                ; before it can even look. The debt is one
                                ; scheduling window and it is spent HERE, so
                                ; the same task cannot starve a waiter twice
                                ; running
.take:
    mov byte [gfx_lock_flag], 1 ; AX is saved and AL = [sch_cur] already
    mov [gfx_lock_own], al      ; the owner is stamped inside the cli window,
    mov [gfx_lock_last], al     ; so flag and owner can never disagree - and
    pop ax                      ; this second one outlives the hold (SPEC 7.3)
    sti
    mov byte [cur_lazy], 1      ; PROMISE the hide (SPEC.md 7.1.4) - the ISR
    ret                         ; will not move the arrow while the flag is
                                ; set, so it is frozen where it is until
                                ; something is about to draw over it

; -----------------------------------------------------------------------------
; gfx_unlock - show the cursor, then release the mutex (SPEC.md 7)
;
; Order is binding: cursor_show redraws at the latest mouse position while we
; still own the screen; only then may the ISR draw again.
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
    call fpg_finish             ; ANY UNLOCKING OF THE USER INTERFACE TAKES
                                ; SPEC.md 12.8'S PROGRESS WIDGET DOWN
                                ; (SPEC.md 12.8.3), and it is dsk_batch's
                                ; argument below in the other direction: the
                                ; freeze the widget reports IS this hold, so
                                ; the hold is its extent and no caller can
                                ; leave it on the bar - not by forgetting a
                                ; bracket, not by returning early, not by a
                                ; file operation that failed.
                                ;
                                ; HERE, and not below with the batch: it
                                ; DRAWS, so it has to run while we still own
                                ; the screen and the arrow is still down - and
                                ; after the clip region above, or the fills
                                ; that give the bed back would be cut by
                                ; whatever region the hold happened to leave
                                ; armed. It preserves the flags, which this
                                ; routine's contract requires, and is a
                                ; compare and a return on the unlocks that saw
                                ; no file work, which is nearly all of them
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
; -----------------------------------------------------------------------------
; gfx_scroll - vertical scroll blit (SPEC.md 5.5; API slot 0x01E0)
;
; in:       AX = x1, BX = y1, CX = x2, DX = y2 (inclusive, absolute screen
;           coords), SI = dy SIGNED: positive moves the rect's content UP dy
;           rows, negative down. Caller holds the gfx lock - which is what
;           makes the blit cursor-safe: the cursor is hidden under it.
; out:      CF=0 done; the |dy| vacated rows keep UNSPECIFIED content and
;           the caller repaints them. CF=1 refused, nothing moved: x1/(x2+1)
;           not multiples of 8, empty/inverted rect, |dy| = 0 or >= height,
;           rect off the live screen, or an armed clip region that does not
;           contain the whole rect (whole-shape clipping, the font_char/
;           ico_core class - a blit cannot be cut per pixel; the caller
;           falls back to a full repaint, which clips per strip).
; clobbers: nothing (registers preserved; CF is the answer)
;
; Two backends, one contract (SPEC.md 39):
;   VGA direct   - GC write mode 1: each movsb moves 8 pixels across all
;                  four planes through the latches; mode restored after
;                  (SPEC.md 1 rule 7).
;   mono         - per-row rep movsb through gfx_rowbase, immune to the
;                  banked layouts by construction.
; -----------------------------------------------------------------------------
gfx_scroll:
    call cur_unlazy             ; off the clip list (SPEC.md 11.3): 7.1.4
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    GFXDENTER                   ; a blit cannot take a sub-rect (SPEC.md 11.3),
    GFXDORG cx, dx              ; so it goes onto the display holding its top
                                ; left - and the two edge tests below then
                                ; REFUSE a rect that reaches past that display,
                                ; which is 39.14.2's straddle rule for free
    test ax, 7                  ; byte-column granularity on every adapter
    jnz .no
    mov di, cx
    inc di
    test di, 7
    jnz .no
    cmp ax, cx
    ja .no
    cmp bx, dx
    ja .no
    cmp cx, [vid_cw]
    jae .no                     ; also catches a "negative" x1 via x1 <= x2
    cmp dx, [vid_ch]
    jae .no
    or si, si
    jz .no
    mov di, si                  ; DI = |dy|
    jns .pos
    neg di
.pos:
    push dx
    sub dx, bx
    inc dx                      ; DX = height
    cmp di, dx
    pop dx
    jae .no
    call wm_clip_test           ; whole-shape clip (SPEC.md 11.3): CF=1 =
    jc .no                      ; armed and not wholly inside one fragment
    mov [vgas_sdy], si
    push cx
    mov cl, 3
    shr ax, cl
    mov [vgas_col], ax           ; left byte column
    pop ax                      ; AX = x2
    inc ax
    mov cl, 3
    shr ax, cl
    sub ax, [vgas_col]
    mov [vgas_nb], ax            ; bytes per row
    mov ax, dx
    sub ax, bx
    inc ax
    sub ax, di
    mov [vgas_rows], ax          ; rows to copy = height - |dy|
    cmp word [vgas_sdy], 0
    jl .down
    mov [vgas_ydst], bx          ; up: dest rows y1..y2-|dy|, walked top-down
    mov word [vgas_step], 1      ; (source below destination - forward-safe)
    jmp short .disp
.down:
    mov [vgas_ydst], dx          ; down: dest rows y1+|dy|..y2, walked
    mov word [vgas_step], -1     ; bottom-up (source above destination)
.disp:
    cmp byte [vid_planes], 1
    je .mono
    mov dx, 0x03CE              ; VRAM, via the latches
    mov ax, 0x0105              ; GC5: write mode 1 - a movsb read fills the
    out dx, ax                  ; latches, the write stores all four planes
    mov dx, VGA_SEG
    call vgas_lincopy
    mov dx, 0x03CE
    mov ax, 0x0005              ; write mode back to 0 (SPEC.md 1 rule 7)
    out dx, ax
    jmp short .ok
.mono:
    call vgas_bankcopy           ; the framebuffer IS [vid_rseg] (SPEC.md 39)
.ok:
    clc
    jmp short .out
.no:
    stc
.out:
    GFXDLEAVE                   ; ...and it banks CF, which is this routine's
    pop di                      ; whole answer
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vgas_lincopy - scroll one LINEAR segment (the VGA stride): a buffer plane,
; or VRAM itself in write mode 1
; in:       DX = target segment; the vgas_* geometry
; clobbers: AX/BX/CX/DX/SI/DI, flags (gfx_scroll banked the caller's)
; -----------------------------------------------------------------------------
vgas_lincopy:
    push ds
    mov es, dx
%ifdef SCROLL_ROWBASE           ; the reference form: two muls a row (SPEC 5.5.1)
    mov ax, [vgas_rows]
    mov [vgas_left], ax
    mov bx, [vgas_ydst]
.r:
    mov ax, bx
    add ax, [vgas_sdy]
    mul word [vid_stride]       ; AX = src row * stride (DX:AX, DX = 0)
    add ax, [vgas_col]
    mov si, ax
    mov ax, bx
    mul word [vid_stride]
    add ax, [vgas_col]
    mov di, ax
    mov cx, [vgas_nb]
    push es
    pop ds                      ; DS = ES = the target for the movs
    cld
    rep movsb
    mov ax, KERNEL_SEG
    mov ds, ax
    add bx, [vgas_step]
    dec word [vgas_left]
    jnz .r
%else
    ; --- SPEC.md 5.5.1: the row address ADVANCES, so derive it once ----------
    ; A linear framebuffer has one bank, so rowbase(y) is y * stride and the
    ; two ends of the copy are a CONSTANT dy * stride apart - for any dy, with
    ; no alignment precondition anywhere. Both products are taken here instead
    ; of twice a row.
    mov ax, [vgas_rows]
    mov [vgas_left], ax
    mov ax, [vgas_ydst]
    mul word [vid_stride]
    add ax, [vgas_col]
    mov di, ax                  ; DI = the destination cursor
    mov ax, [vgas_sdy]          ; the delta, ONCE. `mul` is unsigned and sdy is
    mul word [vid_stride]       ; signed, and that is fine: only the low word
    mov dx, ax                  ; is used, and a product mod 65536 does not
                                ; care which sign the operand was read as -
                                ; the same reason DI itself may wrap
    mov bx, [vid_stride]        ; BX = the per-row step, in the walk's own
    cmp word [vgas_step], 0     ; direction (down: bottom-up, so backwards)
    jg .fwd
    neg bx
.fwd:
    sub bx, [vgas_nb]           ; ...LESS what the rep movsb will already have
                                ; added to DI. SPEC.md 5.7.1's rule, and the
                                ; A/B caught its absence: a string op advances
                                ; its pointer, and the reference form never had
                                ; to care because it recomputed DI every row.
                                ; Folded into the increment, so it is free
    cld                         ; nothing in the loop touches DF
.r:
    mov si, di
    add si, dx
    mov cx, [vgas_nb]
    push es
    pop ds                      ; DS = ES = the target for the movs
    rep movsb
    mov ax, KERNEL_SEG
    mov ds, ax
    add di, bx
    dec word [vgas_left]
    jnz .r
%endif
    pop ds
    ret

; -----------------------------------------------------------------------------
; vgas_bankcopy - the mono twin: per-row copies through gfx_rowbase, which
; absorbs the Hercules/CGA bank interleave (SPEC.md 39.2)
; in:       the vgas_* geometry; target = [vid_rseg]
; clobbers: AX/BX/CX/DX/SI/DI, flags
; -----------------------------------------------------------------------------
vgas_bankcopy:
    push ds
    mov ax, [vid_rseg]
    mov es, ax
    mov ax, [vgas_rows]
    mov [vgas_left], ax
    mov bx, [vgas_ydst]
%ifndef SCROLL_ROWBASE
    ; --- SPEC.md 5.5.1: a whole number of BANKS makes the delta constant -----
    ; rowbase(y) is banktab[y & bmask] + (y >> bshift) * stride, so the two ends
    ; of the copy are a constant apart exactly when dy leaves the bank field
    ; alone - dy a multiple of 2 on CGA and of 4 on Hercules. Then the quotients
    ; differ by dy >> bshift and nothing else, and the destination can walk with
    ; gfx_nextrow instead of being recomputed. The test costs two instructions
    ; on the path that cannot use it.
    mov ax, [vgas_sdy]
    and ax, [vid_bmask]         ; a negative dy is fine: two's complement keeps
    jnz .gen                    ; the low bits' residue
    mov ax, bx
    call gfx_rowbase            ; the destination base, ONCE
    add ax, [vgas_col]
    mov di, ax
    mov ax, [vgas_sdy]          ; delta = (dy >> bshift) * stride, signed - SAR
    mov cl, [vid_bshift]        ; because dy may be negative and the shift has
    sar ax, cl                  ; to floor; exact, because dy is a multiple of
    mul word [vid_stride]       ; the bank count (mul's low word again)
    mov dx, ax
    cld
    cmp word [vgas_step], 0
    jl .rdn
.rup:                           ; up: destination walks DOWN the screen
    mov si, di
    add si, dx
    mov cx, [vgas_nb]
    push es
    pop ds
    rep movsb
    mov ax, KERNEL_SEG
    mov ds, ax
    sub di, [vgas_nb]           ; back to the row's own byte: rep movsb left DI
                                ; nb past it, and the advance below is
                                ; column-preserving, so it has to start from
                                ; the column it is preserving (SPEC.md 5.7.1)
    call gfx_nextrow            ; DI and flags only, and CS-relative reads, so
    dec word [vgas_left]        ; it is safe whatever DS was left at
    jnz .rup
    jmp short .out
.rdn:                           ; down: bottom-up, so the mirror
    mov si, di
    add si, dx
    mov cx, [vgas_nb]
    push es
    pop ds
    rep movsb
    mov ax, KERNEL_SEG
    mov ds, ax
    sub di, [vgas_nb]           ; back to the row's own byte: rep movsb left DI
                                ; nb past it, and the advance below is
                                ; column-preserving, so it has to start from
                                ; the column it is preserving (SPEC.md 5.7.1)
    call gfx_prevrow
    dec word [vgas_left]
    jnz .rdn
    jmp short .out
%endif
.gen:                           ; the general form: one rowbase per END per row
.r:
    mov ax, bx
    add ax, [vgas_sdy]
    call gfx_rowbase            ; AX = the src row's start (CS-relative
    add ax, [vgas_col]           ; parameter reads - DS-safe, SPEC.md 39.2)
    mov si, ax
    mov ax, bx
    call gfx_rowbase
    add ax, [vgas_col]
    mov di, ax
    mov cx, [vgas_nb]
    push es
    pop ds
    cld
    rep movsb
    mov ax, KERNEL_SEG
    mov ds, ax
    add bx, [vgas_step]
    dec word [vgas_left]
    jnz .r
.out:
    pop ds
    ret

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
gfx_blit_ro: resw 1             ; SPEC.md 5.4.1's direct paths: this row's base
gfx_blit_tb: resw 1             ; byte, its pair table, and which path is armed
gfx_blit_fst:resb 1             ; (0 = gfx_fill, 1 = the decoder, 2 = VGA spans)
%ifdef KERN_BIG
gfx_blit_hk: resb 1             ; did THIS call take the whole-shape display
                                ; hook? (SPEC.md 39.14.7.1) - .out must not
                                ; bring a nest down that it never put up, and
                                ; the two early guards can reach it
%endif
gfx_pairtab0: resb 256          ; ...and the pair tables themselves, built once
gfx_pairtab1: resb 256          ; from gfx_inktab by sw_pairbuild

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
%ifdef KERN_BIG
gfx_ln_d        resw 1          ; SPEC.md 5.6.4.1's register walk, staged
gfx_ln_sub2     resw 1          ; across gfx_ls_addr: 2*major and 2*minor, the
gfx_ln_add2     resw 1          ; step interval's start, and how many pixels of
gfx_ln_first    resw 1          ; it are inside this pass's box
gfx_ln_cnt      resw 1
gfx_ln_flg      resb 1          ; ...and LF_STEEP / LF_WHITE / LF_XPOS, which
                                ; pick which of 5.6.4.2's eight walks runs
%endif
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
gfx_ls_vx       resw 1          ; ...and the point in VIRTUAL space, which is
gfx_ls_vy       resw 1          ; what wm_clip_tab is in (SPEC.md 39.14.9)
gfx_ls_cseg     resw 1          ; ...and where the caller's block came from,
gfx_ls_coff     resw 1          ; because ES cannot hold it across the walk

vgas_col         resw 1          ; gfx_scroll (SPEC.md 5.5): left byte column
vgas_nb          resw 1          ;   bytes per row
vgas_sdy         resw 1          ;   signed dy (positive = content up)
vgas_ydst        resw 1          ;   first destination row
vgas_step        resw 1          ;   +1 (up: top-down) / -1 (down: bottom-up)
vgas_rows        resw 1          ;   rows to copy = height - |dy|
vgas_left        resw 1          ;   per-pass row countdown

; -----------------------------------------------------------------------------
section .cold                   ; gfx_blit1's body, and its far entry below it
%ifdef KERN_BIG
; =============================================================================
; gfx_blit1_x - a composed 1bpp band, straight into the framebuffer (SPEC.md
;               5.4.2). kern_big only; kern_small's thunk is stc/retf.
;
; in:       ES:SI = the band, BP = its stride in bytes per row, AX = x (a
;           multiple of 8, SIGNED), BX = y (SIGNED), CX = width in pixels (a
;           multiple of 8), DX = rows (1..255); the gfx lock held
; out:      CF = 0 drawn or clipped exactly, CF = 1 refused and nothing drawn
; clobbers: NOTHING - every register, ES and BP included
;
; The whole routine is the arriving. The caller has already decided what every
; bit is, so there is no colour to reduce, no plane to select and no glyph to
; look up: what is left is a rep movsb a row. SPEC.md 5.4.2 is the contract and
; the reasoning; this is the order of operations it pins, in the order it pins
; them - refuse, resolve the display, clip, emit.
;
; The band step, the row step and the row count are pushed and read BP-relative
; because BP addresses SS (SPEC.md 1) and DS spends the emit pointed at the
; CALLER's band. This body's CS is COLD_SEG, so sw_xfer's [cs:vid_rowadd] idiom
; does not transfer either - the frame is the way to reach a geometry word once
; DS has moved.
;
; THAT RULE COVERS THE EXIT TOO, and it did not: the nest count came down at
; `.done`, which is ABOVE `pop ds`, so `cmp byte [vid_ndisp]` and `dec byte
; [gfx_dnest]` both addressed the CALLER's segment. The read decided the write
; - a package whose image happens to hold anything above 1 at vid_ndisp's
; offset fell through the guard - and the write then decremented one byte of
; that package's own IMAGE, once per blit. Nothing in the kernel or the package
; is checking, so it is silent until the rotted byte is CODE the package
; executes: Word (SPEC.md 68.13) hit it at 0x1698, the head of wd_rflush's
; leading-gap fill, which only a face taller than the 8x8 cell ever runs.
; SPEC.md 5.4.2.1 is the account.
; =============================================================================
gfx_blit1_x:
    test ax, 7                  ; the two argument refusals, and they are the
    jnz .refuse                 ; ONLY two: an x or a width off the byte grid
    test cx, 7                  ; is a caller error, not a shape to clip
    jnz .refuse
    or cx, cx
    jz .refuse
    or dx, dx
    jz .refuse
    cmp dx, 255                 ; ...and a band taller than wm_clip_rows' own
    ja .refuse                  ; byte answer can express (SPEC.md 11.3.2)

    call KERNEL_SEG:cw_cur_unlazy   ; this may write any pixel on the screen,
                                    ; so the deferred hide is owed (SPEC.md
                                    ; 7.1.4) - and owed HERE, above the two
                                    ; questions below, so nothing that answers
                                    ; them can be read stale afterwards

    ; --- resolve the display BEFORE anything meets a screen extent (39.14.8)
    cmp byte [vid_ndisp], 1
    jbe .spanok
    cmp byte [gfx_dnest], 0
    jne .spanok                 ; an outer whole-shape hook already chose one
    push cx
    push dx
    add cx, ax
    dec cx                      ; CX = x2, DX = y2, both still VIRTUAL
    add dx, bx
    dec dx
    call KERNEL_SEG:cw_vid_span_one
    pop dx
    pop cx
    jc .percol                  ; it crosses a seam: per band column, each
.spanok:                        ; resolving its own display (SPEC.md 39.14.6)

    ; --- ...and the clip region, in the same VIRTUAL space wm_clip_tab is in
    cmp word [wm_clip_n], 0
    je .clipok
    push cx
    push dx
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call KERNEL_SEG:cw_wm_clip_rows
    pop dx
    pop cx
    jc .percol                  ; a VERTICAL cut: wm_clip_rows refuses a
.clipok:                        ; part-width row, so the columns ask singly

    push es                     ; from here the caller's registers are banked
    push ds                     ; and the body is free with all nine
    push bp
    push si
    push di
    push dx
    push cx
    push bx
    push ax

    cmp word [wm_clip_n], 0
    je .rowsall
    push ax                     ; x, which this block spends AX to compute
    mov al, [wm_clip_r0]        ; skip the rows the region cut off the top:
    xor ah, ah                  ; a row OFFSET is the same number in either
    or ax, ax                   ; coordinate space, which is why this may be
    jz .r0z                     ; applied before the translation below
    add bx, ax
    push dx
    mul bp                      ; ...and the band skips r0 whole strides
    add si, ax
    pop dx
.r0z:
    mov al, [wm_clip_rn]
    xor ah, ah
    mov dx, ax                  ; DX = the rows one fragment can offer
    pop ax
.rowsall:

    cmp byte [vid_ndisp], 1
    jbe .noenter
    call KERNEL_SEG:cw_gfx_disp_enter   ; AX/BX become this display's own
.noenter:

    ; --- clip to THAT display's extent. A negative x or y is the ordinary
    ; case here and not an error: a window dragged half off the left edge has
    ; its content at one, and whole 8-pixel columns simply drop.
    or ax, ax
    jns .xleft
    add cx, ax                  ; CX loses the columns left of the screen
    jle .none
    push ax
    neg ax
    shr ax, 1                   ; three single shifts: 6 clocks against the
    shr ax, 1                   ; 20 an 8088 charges for a shift by CL
    shr ax, 1
    add si, ax                  ; ...and the band starts that many bytes in
    pop ax
    xor ax, ax
.xleft:
    cmp ax, [vid_cw]
    jae .none
    mov di, ax
    add di, cx
    cmp di, [vid_cw]            ; vid_cw is 640 or 720, both multiples of 8,
    jbe .xright                 ; so the truncation stays on the byte grid
    mov cx, [vid_cw]
    sub cx, ax
.xright:
    or bx, bx
    jns .ytop
    add dx, bx
    jle .none
    push ax
    mov ax, bx
    neg ax
    push dx
    mul bp                      ; the band skips |y| whole strides
    add si, ax
    pop dx
    pop ax
    xor bx, bx
.ytop:
    cmp bx, [vid_ch]
    jae .none
    mov di, bx
    add di, dx
    cmp di, [vid_ch]
    jbe .ybot
    mov dx, [vid_ch]
    sub dx, bx
.ybot:

    ; --- the frame the row loop reads, and then DS leaves KERNEL_SEG --------
    mov di, cx
    shr di, 1
    shr di, 1
    shr di, 1                   ; DI = bytes per row
    push dx                     ; [bp+10] rows remaining
    push di                     ; [bp+8]  bytes per row
    mov cx, bp
    sub cx, di
    push cx                     ; [bp+6]  the band's step, less what rep moved
    push word [vid_rowadd]      ; [bp+4]  gfx_nextrow, inlined (Part 9 Set 3)
    push word [vid_wrapbit]     ; [bp+2]
    push word [vid_wrapfix]     ; [bp+0]
    mov bp, sp

    mov cx, ax                  ; AX is still x, and x is a multiple of 8
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov ax, bx
    push cx
    call KERNEL_SEG:cw_gfx_rowbase  ; AX = rowbase(y); it clobbers CX and DX
    pop cx
    add ax, cx
    mov di, ax                  ; DI = the band's first framebuffer byte
    mov ax, [vid_seg]           ; the UNIFORM cell, and NOT vid_rseg, which is
    mov bx, es                  ; 0 on VGA (viddet.inc:115) - and already the
    mov es, ax                  ; resolved display's, the enter above having
    mov ds, bx                  ; run first (SPEC.md 39.12)
    cld
.row:
    mov cx, [bp+8]
    push di
    shr cx, 1                   ; WORDS: `rep movsw` is 12.5 clocks a byte on
    rep movsw                   ; an 8088 against `rep movsb`'s 17, and the
    jnc .even                   ; carry out of the shift is the odd byte. A
    movsb                       ; string op writes no flag and neither does
.even:                          ; `rep`, so it survives the move (SPEC.md
    pop di                      ; 5.4.2). No segment override on a rep: an
    add si, [bp+6]              ; 8086 loses the prefix to an interrupt (5.7)
    add di, [bp+4]
    test di, [bp+2]
    jz .nowrap
    add di, [bp+0]
.nowrap:
    dec word [bp+10]
    jnz .row
    mov sp, bp
    add sp, 12
.done:
    pop ax
    pop bx
    pop cx
    pop dx
    pop di
    pop si
    pop bp
    pop ds                      ; DS IS KERNEL_SEG AGAIN, AND NOT ONE BYTE
    pop es                      ; SOONER: the emit above spends it on the
                                ; CALLER's band, so the two bytes below have to
                                ; wait for this pop. They did not, and both of
                                ; them landed in the CALLER'S IMAGE - see below.
    cmp byte [vid_ndisp], 1
    jbe .noleave
    dec byte [gfx_dnest]        ; ...and NOTHING restores the display
.noleave:                       ; (SPEC.md 39.14.3)
    clc
    ret
.none:                          ; clipped away entirely: drawn, and nothing
    jmp .done                   ; drawn, which is CF = 0 either way

    ; --- per 8-pixel band column, untranslated (SPEC.md 5.4.2 step 4) -------
    ; Each column re-enters this routine and resolves its own display and its
    ; own row range, which is font_run's per-cell answer one granularity
    ; coarser. A column that is refused a second time is a column that cannot
    ; be drawn - it is skipped, and the caller never letters the row twice.
.percol:
    cmp cx, 8
    jbe .okquiet                ; already one column: this is that skip
    push ax
    push cx
    push si
    push di
    mov di, cx
    shr di, 1
    shr di, 1
    shr di, 1                   ; DI = columns to place
    mov cx, 8
.pcol:
    push ax
    push cx
    push si
    push di
    call gfx_blit1_x
    pop di
    pop si
    pop cx
    pop ax
    add ax, 8
    inc si
    dec di
    jnz .pcol
    pop di
    pop si
    pop cx
    pop ax
.okquiet:
    clc
    ret
.refuse:
    stc
    ret

; -----------------------------------------------------------------------------
; The far entry (SPEC.md 2.6): kernel.asm's resident thunk far-calls this, and
; this near-calls the body above.
; -----------------------------------------------------------------------------
gbz_gfx_blit1:        call gfx_blit1_x
                  retf
%endif
section .text
