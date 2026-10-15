; =============================================================================
; os8088 - winmod.inc
;
; The Windows title style (SPEC.md 76.14): its active Color preset supplies
; the semantic colors, while WIN95.DRV supplies what a palette cannot say -
; buttons on the RIGHT, the X, a maximize button, a caption on the left, a size
; grip, a pull-down that stays open on a click. It is an on-demand module
; (SPEC.md 2.8) that takes five of the kernel's chrome routines over for as
; long as the theme is live.
;
; THE SHAPE IS DOCK.DRV'S (SPEC.md 30.5). The kernel keeps a table of far
; pointers, `thv`, whose resting values are its own bodies, and each door is a
; four-byte `jmp far [thv + i]` standing at the routine's old name - so every
; caller is untouched and a machine that never picks the theme pays one far
; indirect jump per door and nothing else (42 cycles, SPEC.md 30.5). The
; module's two entries write its own landing pads in, and the kernel's bodies
; back out, and it knows the kernel's offsets because a module is refused
; unless it was assembled with this very kernel (SPEC.md 2.8.2).
;
; NOTHING HERE CHANGES A GEOMETRY. TITLE_H stays 18 - which is Windows 95's
; own caption height at 640x480 - and the frame, the shadow, the content rect
; and every package's arithmetic over them are the kernel's, unchanged. That is
; what makes this a theme rather than an ABI change (SPEC.md 76.14).
;
; kern_big only: kern_small has no theme at all (SPEC.md 76.13).
; =============================================================================
; --- the geometry (SPEC.md 76.14.3), OUTSIDE the gate ------------------------
; Constants cost no byte, and tools/os88geom.py mirrors them on BOTH arms.
; Three 16x14 buttons in the title bar's 16 interior rows, measured from the
; window's RIGHT edge: close 2px inside the frame, a 2px gap, then maximize
; and minimize touching, as Windows 95 lays them out. Each is a band of
; WNB_W x WNB_H - the button without its top row and left column, which are
; its lit edge and are drawn as two lines.
WN_BTY      equ 2               ; button top, below W_Y
WN_BTH      equ 14
WN_CLOSE    equ 19              ; button LEFT edge = W_X + W_W - this
WN_MAX      equ 37
WN_MIN      equ 53
WN_CAPX     equ 5               ; caption pen, right of W_X
WN_CAPY     equ 5               ; ...and below W_Y: the kernel's own row
WNB_W       equ 15
WNB_H       equ 13
WNB_STRIDE  equ 2

%ifdef KERN_BIG

; --- the doors (resident) ----------------------------------------------------
THV_TITLE equ 0                 ; wm_draw_title: the whole title bar
THV_HIT   equ 4                 ; wm_hit's TITLE-ROW arm: which button
THV_GRIP  equ 8                 ; wm_grow_paint's glyph: the grow box's face
THV_STICK equ 12                ; menu_drop's release: may the menu stay up?
THV_CRECT equ 16                ; wm_chrome_rect: a held button's interior
THV_N     equ 5
WN_NENT   equ 2                 ; WIN95.DRV's mod_fp entries: hook, unhook

; THE TABLE, at rest: every slot the kernel's own body. Four of the doors are
; NEAR routines and land on a near body; the fifth, the held box's rect, is
; reached by a FAR call from ui.inc and lands on a far body, which is why its
; resting segment is COLD_SEG and the other four KERNEL_SEG.
thv:
    dw wm_draw_title_b, KERNEL_SEG      ; THV_TITLE
    dw wm_hit_ttl_b,    KERNEL_SEG      ; THV_HIT
    dw wm_grow_glyph_b, KERNEL_SEG      ; THV_GRIP
    dw kret_ret,        KERNEL_SEG      ; THV_STICK - a release closes the menu
    dw wmz_crect_b,     COLD_SEG        ; THV_CRECT
%if ($ - thv) != THV_N * 4
  %error "thv and the THV_* offsets disagree"
%endif

wm_draw_title:  jmp far [thv + THV_TITLE]
wm_hit_ttl:     jmp far [thv + THV_HIT]
wm_grow_glyph:  jmp far [thv + THV_GRIP]
menu_sticky:    jmp far [thv + THV_STICK]

section .cold
wmz_wm_chrome_rect:             ; ui_lit_go far-calls this name, as it always
    jmp far [thv + THV_CRECT]   ; did; DS is KERNEL_SEG on every path here

; -----------------------------------------------------------------------------
; thf_apply - mount and hook WIN95.DRV, or unhook and drop it (SPEC.md 76.14.1)
; in:  AL = the THM_* kind thm_set is about to adopt
; out: CF = 0 done; CF = 1 THM_WIN was asked for and the module could not be
;      mounted - nothing is hooked, and thm_set adopts a fallback
; clobbers: nothing else (flags)
;
; dkf_apply's shape. Idempotent both ways: mod_need is six compares when the
; image is already held and the hook rewrites the same five pointers, which
; is what lets vid_switch re-assert a theme through thm_set (SPEC.md 76.12.1)
; without a disk access; and with nothing mounted the unhook is mod_gone's
; refusal and the drop finds no claim, so every other theme pays two calls.
; The caller holds the gfx lock or is the boot, so no door can be executing
; while the table is rewritten.
; -----------------------------------------------------------------------------
thf_apply:
    push ax
    cmp al, THM_WIN
    mov al, MOD_WIN             ; (writes no flag: the compare still stands)
    jne .basic
    call mod_need
    jc .out                     ; no system disk, no heap, a bad image: CF = 1
    call far [WNFP]             ; wnx_hook: the image takes thv over, CF = 0
    jmp short .out
.basic:
    call far [WNFP+4]           ; wnx_unhook - or mod_gone, with nothing held
    call mod_drop
    clc
.out:
    pop ax
    retf
section .text

; =============================================================================
; WIN95.DRV - the module image (SPEC.md 76.14.2)
;
; DS = KERNEL_SEG throughout, so the kernel's variables are plain references
; and this image's own are `[cs:...]`. Kernel routines are reached by WNK
; (extmod.inc's EXK, the same five bytes and the same hand-built frame) or,
; for the hot primitives, straight through the resident far shim a call site
; already uses.
; =============================================================================
section .modw
modw_hdr:
    dw 0x384F                   ; MOD_H_MAGIC
    db MOD_VER, MOD_WIN         ; MOD_H_VER, MOD_H_ID
    dw BUILD_NUM, MOD_STAMP     ; MOD_H_BUILD, MOD_H_STAMP (SPEC.md 2.8.2)
    dw modw_end, WN_NENT        ; MOD_H_IMG, MOD_H_NENT
    dw wnx_hook, wnx_unhook

; WNK name - far-call the KERNEL routine `name` (a near proc in .text) from
; this image. extmod.inc's EXK, whose exk_go lives in EXTD.DRV's image and so
; cannot be shared: each image carries its own.
%macro WNK 1
    call wnk_go
    dw %1
%endmacro

; -----------------------------------------------------------------------------
; wnx_hook / wnx_unhook - take thv over, and give it back (SPEC.md 76.14.2)
; in:  nothing; the gfx lock is held by thf_apply's caller, or it is the boot
; out: CF = 0; every register preserved
; -----------------------------------------------------------------------------
wnx_hook:
    push si
    mov si, wnp_tab             ; this image's pads...
    jmp short wnx_set
wnx_unhook:
    push si
    mov si, wnr_tab             ; ...or the kernel's own bodies
wnx_set:
    push ax
    push cx
    push di
    mov di, thv                 ; DS is KERNEL_SEG: the writes are plain, the
    mov cx, THV_N * 2           ; source is ours and is read through CS
    cld
.l:
    cs lodsw
    cmp ax, WN_SELF             ; the pad table names THIS segment by a token,
    jne .w                      ; because no `dw` can know where the image
    mov ax, cs                  ; will be loaded
.w:
    mov [di], ax
    inc di
    inc di
    loop .l
    pop di
    pop cx
    pop ax
    pop si
    clc
    retf

WN_SELF     equ 0xFFFF          ; a segment no claim can have: the IVT's top
wnp_tab:
    dw wnp_title, WN_SELF
    dw wnp_hit,   WN_SELF
    dw wnp_grip,  WN_SELF
    dw wnp_stick, WN_SELF
    dw wnx_crect, WN_SELF
wnr_tab:
    dw wm_draw_title_b, KERNEL_SEG
    dw wm_hit_ttl_b,    KERNEL_SEG
    dw wm_grow_glyph_b, KERNEL_SEG
    dw kret_ret,        KERNEL_SEG
    dw wmz_crect_b,     COLD_SEG
%if (wnr_tab - wnp_tab) != THV_N * 4 || ($ - wnr_tab) != THV_N * 4
  %error "WIN95.DRV's two tables and thv disagree"
%endif

; The landing pads. The kernel far-JUMPS here with its caller's near return
; address still on top of the stack, so a pad near-calls the body and goes
; home to a bare `ret` in the kernel - dockmod.inc's dkxf_home, one per pad.
; Nothing in a pad touches a flag.
wnp_title:
    call wn_title
    jmp KERNEL_SEG:kret_ret
wnp_hit:
    call wn_hit
    jmp KERNEL_SEG:kret_ret
wnp_grip:
    call wn_grip
    jmp KERNEL_SEG:kret_ret
wnp_stick:
    call wn_stick
    jmp KERNEL_SEG:kret_ret

; --- the image's own state ---------------------------------------------------
; A module may write its own image (MODULE-SELFCONTAIN-PLAN), and none of this
; has to outlive a mod_drop: it is scratch for one title bar or one menu.
wn_pair:    dw 0                ; AL = this bar's ground, AH = its ink
wn_tmp:     times 3 * WNB_H db 0    ; one band, shifted to its screen phase
wn_lastx:   dw 0                ; the sticky menu's last pointer position
wn_lasty:   dw 0
wn_cap:     times 81 db 0       ; the caption, truncated to fit (80 cells)

; -----------------------------------------------------------------------------
; wn_title - one window's title bar, Windows-fashion (SPEC.md 76.14.3)
; in:  DI = window, BP = the frontmost visible window (or 0); gfx lock held
; out: nothing; every register preserved
;
; ELEVEN drawing calls on every window, focused or not: the ground, three
; button bands, their lit edges in five lines, the caption and the rule. The
; Mac bar is fifteen on the front window and four on the others, so a
; background bar is dearer and the front one cheaper - and Windows draws its
; buttons on every window, which is the owner's decision (SPEC.md 76.14.5).
; -----------------------------------------------------------------------------
wn_title:
    test byte [di+W_FLAGS], WF_FULL
    jnz .none                   ; a fullscreen window HAS no title bar
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es

    ; --- the pens come from the selected Color preset -----------------------
    mov ax, [thm_tbgc]          ; AL = ground, AH = ink (adjacent, in order)
    cmp di, bp
    je .pens
    mov ax, [thm_ibgc]          ; unfocused title pair, also from that preset
.pens:
    mov [cs:wn_pair], ax

    ; --- the ground: the bar's 16 interior rows, edge to edge ---------------
    mov [gfx_color], al
    WNK wm_bar_span             ; AX = x1, CX = x2 (both edge columns on a
    mov bx, [di+W_Y]            ; window with no side borders, 11.95.3)
    inc bx
    lea dx, [bx+TITLE_H-3]
    call KERNEL_SEG:cw_gfx_fill

    ; --- the three buttons --------------------------------------------------
    call wn_buttons

    ; --- the caption, LEFT-aligned and cut short of the buttons -------------
    mov si, [di+W_TITLE]
    or si, si
    jz .sep
    WNK wm_strseg               ; ES = the owner's segment
    mov cx, [di+W_W]            ; cells that fit: from the pen to 4px short of
    sub cx, WN_MIN + WN_CAPX + 4    ; the minimize button
    jle .nocap
    shr cx, 1
    shr cx, 1
    shr cx, 1
    jz .nocap
    cmp cx, 80
    jbe .cut
    mov cx, 80
.cut:
    push di
    mov di, wn_cap              ; copy at most CX characters into our own
.cp:                            ; buffer: the owner's string is not ours to
    mov al, [es:si]             ; terminate early
    or al, al
    jz .cpend
    mov [cs:di], al
    inc si
    inc di
    loop .cp
.cpend:
    mov byte [cs:di], 0
    pop di
    push cs
    pop es
    mov si, wn_cap
    mov cx, [di+W_X]
    add cx, WN_CAPX
    mov dx, [di+W_Y]
    add dx, WN_CAPY
    mov ax, [cs:wn_pair]
    xchg al, ah                 ; font_run: AL = ink, AH = ground
    WNK font_run_x              ; ONE opaque pass (SPEC.md 6.1)
.nocap:
.sep:
    ; --- the rule under the bar, as the kernel draws it -----------------------
    mov al, [thm_inkc]
    mov [gfx_color], al
    WNK wm_bar_span
    mov bx, cx
    mov dx, [di+W_Y]
    add dx, TITLE_H-1
    call KERNEL_SEG:cw_gfx_hline

    ; --- a button being HELD DOWN stays down across a repaint (13.8.1) -------
    cmp byte [ui_armlit], 0
    je .out
    cmp di, [ui_armw]
    jne .out
    mov si, di
    mov ah, [ui_armr]
    call wn_crect
    call KERNEL_SEG:cw_gfx_xor_fill
.out:
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.none:
    ret

; -----------------------------------------------------------------------------
; wn_buttons - minimize, maximize (or restore) and close, right to left
; in:  DI = window; gfx lock held
; out: nothing; AX..DX, SI, BP clobbered, ES preserved
;
; EACH BUTTON IS ONE BAND AND TWO LINES (SPEC.md 76.14.3). The band is the
; button without its top row and left column - face, bottom and right edge,
; glyph - in ONE gfx_blit1, its pen face-and-black; the lit edge is the top
; row and the left column, which the band does not cover, so no pixel is
; written twice. The top edges of minimize and maximize touch and are one
; hline. A band is SET where the face is, which is screen polarity on a 1bpp
; display: lit is white there, and the pen is not consulted (SPEC.md 5.4.2.2).
; -----------------------------------------------------------------------------
wn_buttons:
    push es
    push cs
    pop es
    push word [gfx_b1ink]       ; THE PEN, banked and put back: a package's
    test byte [thm_cstrp], 2     ; Classic restores main's original Windows
    jnz .classic                ; button colors, but keeps this theme's bar
    mov ax, [cs:wn_pair]        ; current layout: buttons follow the selected
    mov [gfx_b1ink], ax         ; preset's title pair
    jmp short .draw
.classic:
    mov al, [thm_bgc]           ; original main layout used the UI chrome face
    mov ah, CBLACK              ; and black glyphs, independent of title color
    mov [gfx_b1ink], ax
.draw:

    mov ax, WN_MIN
    mov si, wnb_min
    call wn_band
    mov ax, WN_MAX
    call wn_maxart              ; SI = maximize, restore, or greyed
    call wn_band
    mov ax, WN_CLOSE
    mov si, wnb_close
    call wn_band
    pop word [gfx_b1ink]

    test byte [thm_cstrp], 2
    jnz .classic_edges
    mov al, [cs:wn_pair+1]      ; --- the new style uses the title's ink
    jmp short .edges
.classic_edges:
    mov al, CWHITE              ; original main button bevel
.edges:
    mov [gfx_color], al
    mov ax, WN_MIN              ; ONE top line across minimize AND maximize,
    call wn_bx                  ; which touch: minimize's left to the last
    mov bx, ax                  ; column before maximize's dark one
    add bx, WN_MIN - WN_MAX + WNB_W - 1
    call wn_hl
    mov ax, WN_CLOSE            ; ...close's own
    call wn_bx
    mov bx, ax
    add bx, WNB_W - 1
    call wn_hl
    mov ax, WN_MIN              ; ...and the three left edges
    call wn_vl
    mov ax, WN_MAX
    call wn_vl
    mov ax, WN_CLOSE
    call wn_vl
    pop es
    ret

; wn_bx - AX = a button's offset from the right edge -> AX = its left x
wn_bx:
    neg ax
    add ax, [di+W_X]
    add ax, [di+W_W]
    ret
; wn_by - BX = the buttons' top y
wn_by:
    mov bx, [di+W_Y]
    add bx, WN_BTY
    ret
; wn_hl - AX = x1, BX = x2: the buttons' top row, in [gfx_color]
wn_hl:
    mov dx, [di+W_Y]
    add dx, WN_BTY
    call KERNEL_SEG:cw_gfx_hline    ; AX = x1, BX = x2, DX = y
    ret
; wn_vl - AX = a button's offset: its left edge, the rows under its top one
wn_vl:
    call wn_bx
    call wn_by
    inc bx
    lea dx, [bx+WN_BTH-3]
    call KERNEL_SEG:apic_gfx_vline  ; AX = x, BX = y1, DX = y2
    ret
; wn_band - AX = a button's offset, SI = its art: the band, one gfx_blit1
wn_band:
    call wn_bx
    inc ax                      ; the band starts one column in...
    call wn_by
    inc bx                      ; ...and one row down
    mov dx, WNB_H
    mov cx, WNB_W
; wn_blit - AX = x, BX = y, CX = width, DX = rows, SI = art (two bytes a row,
; bit 7 of the first leftmost): ONE gfx_blit1 at any x
;
; THE BAND IS SCREEN-ALIGNED, NOT ANCHORED AT x (SPEC.md 5.4.2.8): its first
; byte is the byte column HOLDING x, and the first x & 7 pixels of it are not
; drawn. So the art is shifted right by that phase into wn_tmp - three bytes a
; row, the art's 16 bits plus the up-to-7 they spill - and blitted from there.
; ES is this image (the caller's). Everything but the flags preserved.
wn_blit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ax
    mov cx, ax
    and cl, 7                   ; CL = the phase
    mov di, wn_tmp
.row:
    mov ah, [cs:si]             ; AH = the row's left byte, AL its right one
    mov al, [cs:si+1]
    xor bp, bp                  ; BP's low byte collects what spills off
    push cx
    xor ch, ch
    jcxz .put
.sh:
    shr ax, 1
    rcr bp, 1
    loop .sh
.put:
    pop cx
    mov [cs:di], ah
    mov [cs:di+1], al
    mov ax, bp
    mov [cs:di+2], ah           ; (RCR filled BP from bit 15 down)
    inc si
    inc si
    add di, 3
    dec dx
    jnz .row
    pop ax                      ; x, and the rest as the caller passed them
    mov bp, sp
    mov bx, [ss:bp+10]          ; y (the stack: bp di si dx cx bx ax)
    mov cx, [ss:bp+8]           ; width
    mov dx, [ss:bp+6]           ; rows
    mov si, wn_tmp
    mov bp, 3
    call COLD_SEG:gbz_gfx_blit1 ; CF = refused: an argument error, and these
    pop bp                      ; arguments are constants
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wn_maxart - SI = the maximize button's art for window DI: RESTORE when it is
; already zoomed, GREYED when it cannot be (SPEC.md 76.14.4)
wn_maxart:
    mov si, wnb_maxd
    test byte [di+W_FLAGS], WF_SIZABLE
    jz .out
    push ax
    push bx
    push cx
    push dx
    mov ax, [vid_band_x0]       ; ZOOMED is DERIVED, never tracked (SPEC.md
    mov cx, [vid_band_xe]       ; 11.95): wm_zoom's standard rect - the band,
    sub cx, ax                  ; bar to one short of the dock...
    mov si, MBAR_H
    mov dx, [vid_dock_y0]
    sub dx, MBAR_H + 1
    mov bx, di
    EXTCALL EXT_SPAN            ; ...on the window's OWN display (39.17.2).
                                ; NOT wm_ask_size: that calls the package,
                                ; and a title paint must not
    mov bx, wnb_max
    cmp [di+W_Y], si
    jne .std
    sub cx, 7                   ; width and height give or take 7px: a
    cmp [di+W_W], cx            ; WF_SNAP origin's, and a negotiator's small
    jl .std                     ; quantum
    sub dx, 7
    cmp [di+W_H], dx
    jl .std
    mov bx, wnb_rest
.std:
    mov si, bx
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; wn_hit - which button of a title bar is under a point (SPEC.md 76.14.4)
; in:  AX = y - W_Y (0 .. TITLE_H-1), BX = window, CX = x, BP = 1 frontmost
; out: AL = 1 the title bar, 2 close, 3 minimize, 5 maximize - on EVERY
;      window, not the frontmost alone: Windows acts on a background
;      window's button at once (SPEC.md 76.14.5). A window that cannot be
;      resized answers 1 over its greyed maximize, so the press drags it
; clobbers: AH
; -----------------------------------------------------------------------------
wn_hit:
    sub ax, WN_BTY              ; every range test is ONE unsigned compare,
    cmp ax, WN_BTH - 1          ; wm_hit's idiom: below the range wraps high
    ja .title
    mov ax, [bx+W_X]
    add ax, [bx+W_W]
    sub ax, cx                  ; AX = how far left of the right edge
    sub ax, WN_CLOSE - (WNB_W)  ; close: WN_CLOSE .. WN_CLOSE - 15
    cmp ax, WNB_W
    jbe .close
    sub ax, WN_MAX - WN_CLOSE
    cmp ax, WNB_W
    jbe .max
    sub ax, WN_MIN - WN_MAX
    cmp ax, WNB_W
    jbe .min
.title:
    mov al, 1
    ret
.close:
    mov al, 2
    ret
.min:
    mov al, 3
    ret
.max:
    test byte [bx+W_FLAGS], WF_SIZABLE
    jz .title
    mov al, 5
    ret

; -----------------------------------------------------------------------------
; wnx_crect - a held button's interior (SPEC.md 13.8.1, 76.14.4); FAR
; in:  SI = window, AH = the region (2 close, 3 minimize, 5 maximize)
; out: AX, BX, CX, DX = x1, y1, x2, y2 inclusive: the face inside both edges,
;      so the inversion leaves them standing; SI preserved
; -----------------------------------------------------------------------------
wnx_crect:
    call wn_crect
    retf
wn_crect:
    mov al, WN_CLOSE
    cmp ah, 2
    je .x
    mov al, WN_MIN
    cmp ah, 3
    je .x
    mov al, WN_MAX
.x:
    xor ah, ah
    neg ax
    add ax, [si+W_X]
    add ax, [si+W_W]
    inc ax                      ; x1: inside the lit column
    mov cx, ax
    add cx, WNB_W - 2           ; x2: inside the dark one
    mov bx, [si+W_Y]
    add bx, WN_BTY + 1
    lea dx, [bx+WN_BTH - 3]
    ret

; -----------------------------------------------------------------------------
; wn_grip - the grow box's face: Windows' size grip (SPEC.md 76.14.3)
; in:  DI = the frontmost resizable window (wm_grow_paint's gates passed)
; out: nothing; every register preserved
; ONE band where the box was three fills and three frames: face with three
; diagonal ridges, in the same 13x13 the box stood in, so wm_hit's test and
; wm_grow_erase's rect are unchanged.
; -----------------------------------------------------------------------------
wn_grip:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    push word [gfx_b1ink]       ; the pen, banked as wn_buttons banks it
    mov al, [thm_bgc]           ; AL = ink = the face, AH = paper = the ridges
    mov ah, CDGRAY
    mov [gfx_b1ink], ax
    WNK wm_grow_rect            ; AX, BX = its top-left (CX, DX spent)
    push cs
    pop es
    mov si, wnb_grip
    mov cx, 13
    mov dx, 13
    call wn_blit
    pop word [gfx_b1ink]
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wn_stick - may a released pull-down STAY UP? (SPEC.md 76.14.7)
; in:  menu_drop's state at its release: the menu is on the screen, the gfx
;      lock held, [menu_sel] the item the release landed on (0xFFFF none)
; out: [menu_sel] = what to run (0xFFFF = nothing); the menu still drawn
; clobbers: nothing
;
; A SUPERSET OF PRESS-DRAG-RELEASE. A release that DRAGGED onto an item fires
; it, exactly as before - so every Mac habit and every test that drives a menu
; that way still works. A release that is a CLICK - within 4px of the press -
; leaves the menu up WHATEVER is under it, and so does one that landed on
; nothing inside the menu: then the highlight follows the pointer with no
; button held, the next click on an item fires it, a click anywhere else
; closes it and goes no further, Esc closes it, and Up/Down/Enter walk and
; choose (SPEC.md 76.14.8).
;
; The click test comes FIRST because a context menu opens AT the pointer
; (SPEC.md 12.4): its first item is a pixel or two from the press, so a
; right-click that let go where it pressed ran that item, and a context menu
; could never be left open at all (76.14.7.1). Nothing is highlighted when a
; click leaves a menu up, as in Windows - Enter then runs nothing until a key
; or the pointer chooses.
;
; THE LOCK IS HELD THROUGHOUT, which is the freeze a held menu has always
; been (SPEC.md 12.8.3) - decided for this phase (76.14.9). So the ISR will
; not move the arrow, and this loop does it: the arrow is shown at the
; pointer, and hidden around every highlight change, which is what a held
; menu never needed because there the highlight WAS the feedback.
; -----------------------------------------------------------------------------
wn_stick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [mouse_x]
    mov dx, [mouse_y]
    mov ax, cx                  ; a CLICK: within 4px of the press, which is
    sub ax, [ui_ev+EV_A]        ; still the record ui_task popped to dispatch
    add ax, 4                   ; it (the title, or a context menu's anchor)
    cmp ax, 8
    ja .drag
    mov ax, dx
    sub ax, [ui_ev+EV_B]
    add ax, 4
    cmp ax, 8
    ja .drag
    mov ax, 0xFFFF              ; ...stays up with nothing lit, whatever item
    call wn_sel                 ; the press opened the menu over (menu_drop's
    jmp short .stick            ; own highlights are drawn arrow and all)
.drag:
    cmp word [menu_sel], 0xFFFF
    jne .done                   ; dragged onto an item: run it, as ever
    call wn_inmenu
    jc .done                    ; dragged off the menu: closed, nothing run
.stick:
    WNK cur_unlazy              ; the arrow: any deferred hide spent, then
    WNK cursor_show             ; drawn at the pointer, which the ISR will not
    mov [cs:wn_lastx], cx       ; move while we hold the lock
    mov [cs:wn_lasty], dx

    ; --- UP: wait for the next click ------------------------------------------
.wait:
    mov ah, 1
    int 0x16
    jnz .key
    WNK kbm_poll                ; empty: still the keyboard mouse's turn -
    jmp short .nokey            ; PEEK-ONLY, so a key landing between the
                                ; two peeks waits for the next pass rather
                                ; than being eaten (kbm_pollm's) unseen
.key:
    cmp ah, 0x01                ; Esc: close
    je .k_esc
    cmp ah, 0x48                ; Up
    je .k_up
    cmp ah, 0x50                ; Down
    je .k_down
    cmp ah, 0x1C                ; Enter
    je .k_enter
    WNK kbm_pollm               ; anything else is the modal loop's (the
    jmp short .nokey            ; keyboard mouse may release or press)
.k_esc:
    call wn_take
    call wn_desel
    jmp .close
.k_enter:
    call wn_take
    cmp word [menu_sel], 0xFFFF
    jne .close                  ; ...run the highlighted item
    jmp short .nokey
.k_up:
    call wn_take
    mov ax, -1
    jmp short .step
.k_down:
    call wn_take
    mov ax, 1
.step:
    call wn_step
.nokey:
    call wn_track               ; follow the pointer, if it moved
    mov al, [menu_btn]
    test [mouse_btn], al
    jnz .press
    WNK task_yield
    jmp .wait

    ; --- a PRESS: on the menu it tracks to its release; off it, it closes ------
.press:
    mov cx, [mouse_x]
    mov dx, [mouse_y]
    call wn_inmenu
    jnc .held
    call wn_desel               ; off the menu: nothing runs, and the click
.drain:                         ; goes no further (drained below)
    WNK kbm_pollm
    WNK task_yield
    mov al, [menu_btn]
    test [mouse_btn], al
    jnz .drain
    jmp short .close
.held:
    call wn_track
    WNK kbm_pollm
    WNK task_yield
    mov al, [menu_btn]
    test [mouse_btn], al
    jnz .held
    call wn_track
    cmp word [menu_sel], 0xFFFF
    je .wait                    ; released on a separator or a greyed item:
                                ; still up, as Windows leaves it
.close:
    WNK cursor_hide             ; the arrow down again: menu_drop's restore
    push di                     ; and gfx_unlock then do what they always did
    mov di, ui_ev
    WNK evq_drain               ; every press and release this phase made, so
    pop di                      ; the dismissing click reaches nothing - each
                                ; wake still OWED (SPEC.md 74.1.1)
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; wn_take - remove the key int 16h peeked
wn_take:
    push ax
    xor ah, ah
    int 0x16
    pop ax
    ret

; wn_inmenu - CF = 0 when CX, DX is inside the menu's rect
wn_inmenu:
    cmp cx, [menu_x1]
    jb .no
    cmp cx, [menu_x2]
    ja .no
    cmp dx, [menu_y1]
    jb .no
    cmp dx, [menu_y2]
    ja .no
    clc
    ret
.no:
    stc
    ret

; wn_track - the highlight follows the pointer, IF the pointer moved (so a
; key's choice is not undone by a mouse that is standing still) - and so does
; the arrow
wn_track:
    mov cx, [mouse_x]
    mov dx, [mouse_y]
    cmp cx, [cs:wn_lastx]
    jne .moved
    cmp dx, [cs:wn_lasty]
    je .out
.moved:
    mov [cs:wn_lastx], cx
    mov [cs:wn_lasty], dx
    WNK cursor_hide
    WNK menu_hover              ; AX = the item under it, 0xFFFF none
    call wn_sel
    WNK cursor_show
.out:
    ret

; wn_sel - make AX the highlighted item (the arrow is the CALLER's to hide)
wn_sel:
    cmp ax, [menu_sel]
    je .out
    xchg ax, [menu_sel]
    cmp ax, 0xFFFF
    je .new
    WNK menu_item_xor           ; the old one off
.new:
    mov ax, [menu_sel]
    cmp ax, 0xFFFF
    je .out
    WNK menu_item_xor           ; ...and the new one on
.out:
    ret

; wn_desel - nothing highlighted
wn_desel:
    WNK cursor_hide
    mov ax, 0xFFFF
    call wn_sel
    WNK cursor_show
    ret

; wn_step - AX = -1 or +1: the next live item that way, wrapping, skipping
; separators and greyed items (menu_item_dis)
wn_step:
    mov bx, ax
    mov ax, [menu_sel]
    mov cx, [menu_cnt]
    jcxz .out
    cmp ax, 0xFFFF
    jne .go
    mov ax, -1                  ; nothing yet: Down starts at the top...
    or bx, bx
    jns .go
    mov ax, cx                  ; ...Up at the bottom
.go:
    add ax, bx
    jns .lo
    mov ax, [menu_cnt]
    dec ax
.lo:
    cmp ax, [menu_cnt]
    jb .test
    xor ax, ax
.test:
    WNK menu_item_dis           ; CF = 1 greyed or a separator
    jnc .found
    loop .go
    jmp short .out              ; nothing in it is live
.found:
    WNK cursor_hide
    call wn_sel
    WNK cursor_show
.out:
    ret

; -----------------------------------------------------------------------------
; wnk_go - the far call WNK names (extmod.inc's exk_go, to the instruction):
; the call left the address of `dw target` on the stack, and this turns it
; into a far return to just past it with cw_kretf's near return on top, then
; RETFs into KERNEL_SEG:target. Every register and the flags preserved.
; -----------------------------------------------------------------------------
wnk_go:                         ; ovlchk: builds its own far frame
    push ax                     ; STKBALANCE-OK: a far frame built by hand, its
    push ax                     ; four slots spent by the RETF below and by the
    push ds                     ; target's own return through cw_kretf. The
    push ax                     ; segment slot is PUSHED: DS is KERNEL_SEG
    push bp                     ; throughout the image
    mov bp, sp
    push bx
    mov bx, [bp+10]
    mov [bp+10], cs
    mov word [bp+6], cw_kretf
    lea bx, [bx+2]
    mov [bp+8], bx
    mov bx, [cs:bx-2]
    mov [bp+2], bx
    pop bx
    pop bp
    retf

; --- the art (SPEC.md 76.14.3), generated, 16 bits a row, bit 7 leftmost ------
; A SET bit is FACE, a clear one dark. Each button band is the button without
; its top row and left column (its lit edge, drawn as lines):
;
;   close            maximize         restore          minimize
;   WWWWWWWWWWWWWWW# WWWWWWWWWWWWWWW# WWWWWWWWWWWWWWW# WWWWWWWWWWWWWWW#
;   W..............# W..............# W..............# W..............#
;   W..............# W..XXXXXXXXX...# W....XXXXXXX...# W..............#
;   W...XX....XX...# W..XXXXXXXXX...# W....XXXXXXX...# W..............#
;   W....XX..XX....# W..X.......X...# W....X.....X...# W..............#
;   W.....XXXX.....# W..X.......X...# W..XXXXXXX.X...# W..............#
;   W......XX......# W..X.......X...# W..XXXXXXX.X...# W..............#
;   W.....XXXX.....# W..X.......X...# W..X.....XXX...# W..............#
;   W....XX..XX....# W..X.......X...# W..X.....X.....# W..............#
;   W...XX....XX...# W..X.......X...# W..X.....X.....# W...XXXXXX.....#
;   W..............# W..XXXXXXXXX...# W..XXXXXXX.....# W...XXXXXX.....#
;   W..............# W..............# W..............# W..............#
;   W..............# W..............# W..............# W..............#
;   ################ ################ ################ ################
;
; The greyed maximize is the maximize glyph on a checkerboard; the grip is
; three diagonal ridges in the bottom-right of a 13x13 face.
wnb_min:
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 11100000b, 01111100b
    db 11100000b, 01111100b
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 00000000b, 00000000b
wnb_max:
    db 11111111b, 11111100b
    db 11000000b, 00011100b
    db 11000000b, 00011100b
    db 11011111b, 11011100b
    db 11011111b, 11011100b
    db 11011111b, 11011100b
    db 11011111b, 11011100b
    db 11011111b, 11011100b
    db 11011111b, 11011100b
    db 11000000b, 00011100b
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 00000000b, 00000000b
wnb_rest:
    db 11111111b, 11111100b
    db 11110000b, 00011100b
    db 11110000b, 00011100b
    db 11110111b, 11011100b
    db 11000000b, 01011100b
    db 11000000b, 01011100b
    db 11011111b, 00011100b
    db 11011111b, 01111100b
    db 11011111b, 01111100b
    db 11000000b, 01111100b
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 00000000b, 00000000b
wnb_maxd:
    db 11111111b, 11111100b
    db 11101010b, 10111100b
    db 11010101b, 01011100b
    db 11111111b, 11111100b
    db 11011111b, 11011100b
    db 11111111b, 11111100b
    db 11011111b, 11011100b
    db 11111111b, 11111100b
    db 11011111b, 11011100b
    db 11101010b, 10111100b
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 00000000b, 00000000b
wnb_close:
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 11100111b, 10011100b
    db 11110011b, 00111100b
    db 11111000b, 01111100b
    db 11111100b, 11111100b
    db 11111000b, 01111100b
    db 11110011b, 00111100b
    db 11100111b, 10011100b
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 11111111b, 11111100b
    db 00000000b, 00000000b
wnb_grip:
    db 11111111b, 11111000b
    db 11111111b, 11110000b
    db 11111111b, 11100000b
    db 11111111b, 11001000b
    db 11111111b, 10011000b
    db 11111111b, 00110000b
    db 11111110b, 01100000b
    db 11111100b, 11001000b
    db 11111001b, 10011000b
    db 11110011b, 00110000b
    db 11100110b, 01100000b
    db 11001100b, 11001000b
    db 10011001b, 10011000b
section .text
%endif                          ; KERN_BIG
