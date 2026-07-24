; =============================================================================
; wm.inc - window manager (SPEC.md 11)
;
; Window records, z-order, frame drawing, hit testing, paint-all. Every used
; window is always present in wm_zord (index 0 = backmost); membership is set
; at wm_create and removed only by wm_destroy - wm_hide only clears the
; visible bit, and wm_show/wm_front only reorder. The visible flag alone
; decides whether a window is painted, hit-tested, or counted by
; wm_top/wm_obscured.
;
; All drawing here goes through the vga12/font primitives with [gfx_color];
; every caller of a repainting routine already holds the gfx lock (SPEC 1.6).
; =============================================================================

; --- window record, 18 bytes, fixed offsets (SPEC.md 11) ---------------------
W_FLAGS equ 0                   ; word: bit0 = used, bit1 = visible
W_X     equ 2                   ; word, frame left (screen coords)
W_Y     equ 4                   ; word, frame top (below menu bar: y >= MBAR_H)
W_W     equ 6                   ; word, frame width  (outer, includes border)
W_H     equ 8                   ; word, frame height (includes title bar)
W_TITLE equ 10                  ; word: near ptr to NUL title string
W_PAINT equ 12                  ; word: near ptr, in: SI = window ptr
W_ONKEY equ 14                  ; word: near ptr or 0; AL=ascii AH=scan SI=win
W_ONCLICK equ 16                ; word: near ptr or 0; CX=x DX=y SI=win, called
                                ; under the gfx lock for content clicks on the
                                ; front window (SPEC.md 13)
WIN_SIZE equ 18                 ; NOT a power of two: never shift, multiply
MAX_WIN  equ 8

; -----------------------------------------------------------------------------
; wm_init - zero the window table, z-order array and count
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
wm_init:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    xor ax, ax
    mov di, wm_wins             ; wins + zord + zn are contiguous in .bss
    mov cx, MAX_WIN*WIN_SIZE + MAX_WIN + 1
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_create - allocate a window from a template
; in:  SI -> 16-byte template {x, y, w, h, title, paint, onkey, onclick}
;      (8 words)
; out: BX = window ptr, CF clear; CF set (BX = 0) if the table is full
;
; The window is created hidden (used bit only); its index is appended to
; wm_zord - frontmost position - and wm_zn is incremented. Does not repaint,
; so it is callable without the gfx lock (SPEC.md 11).
; -----------------------------------------------------------------------------
wm_create:
    push ax
    push cx
    push si
    push di
    push es
    mov bx, wm_wins
    mov cx, MAX_WIN
.slot:
    test word [bx+W_FLAGS], 1
    jz .free
    add bx, WIN_SIZE
    loop .slot
    xor bx, bx                  ; table full
    stc
    jmp .out
.free:
    push ds
    pop es
    mov word [bx+W_FLAGS], 1    ; used, not visible
    lea di, [bx+W_X]
    mov cx, 8
    rep movsw                   ; copy the template into x..onclick
    mov ax, bx                  ; index = (ptr - wm_wins) / WIN_SIZE
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    mov ch, 0
    mov cl, [wm_zn]
    mov di, wm_zord
    add di, cx
    mov [di], al                ; append: new window is frontmost
    inc byte [wm_zn]
    clc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_destroy - free a window record and repaint everything
; in:  BX = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; Clears W_FLAGS (used + visible), removes the window's index from wm_zord
; (compacting the byte array over it, decrementing wm_zn), then repaints all.
; The record slot becomes reusable by wm_create.
; -----------------------------------------------------------------------------
wm_destroy:
    push ax
    push cx
    push si
    mov word [bx+W_FLAGS], 0    ; free the record slot
    mov ax, bx                  ; AL = window index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    mov ch, 0
    mov cl, [wm_zn]
    jcxz .paint                 ; empty z-order: nothing to remove
    mov si, wm_zord
.find:
    cmp [si], al
    je .remove
    inc si
    loop .find
    jmp .paint                  ; not in z-order (cannot happen when used)
.remove:
    dec cx                      ; entries above us; compact them down a slot
    jcxz .count
.shift:
    mov ah, [si+1]
    mov [si], ah
    inc si
    loop .shift
.count:
    dec byte [wm_zn]
.paint:
    call wm_paint_all
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_show - make a window visible, raise it, repaint everything
; in:  BX = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
wm_show:
    or word [bx+W_FLAGS], 2
    jmp wm_front                ; raise + repaint; same register contract

; -----------------------------------------------------------------------------
; wm_hide - clear the visible bit and repaint everything
; in:  BX = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
wm_hide:
    and word [bx+W_FLAGS], 0xFFFD
    jmp wm_paint_all            ; same register contract

; -----------------------------------------------------------------------------
; wm_front - raise a window to the front of the z-order, repaint everything
; in:  BX = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
wm_front:
    push ax
    push cx
    push si
    mov ax, bx                  ; AL = window index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    mov ch, 0
    mov cl, [wm_zn]
    jcxz .paint                 ; empty z-order: nothing to reorder
    mov si, wm_zord
.find:
    cmp [si], al
    je .lift
    inc si
    loop .find
    jmp .paint                  ; not found (cannot happen for a used window)
.lift:
    dec cx                      ; entries above us; shift them back one slot
    jcxz .place
.shift:
    mov ah, [si+1]
    mov [si], ah
    inc si
    loop .shift
.place:
    mov [si], al                ; and we land on top
.paint:
    call wm_paint_all
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_top - find the frontmost visible window
; in:  nothing
; out: BX = window ptr, or 0 if no window is visible
; -----------------------------------------------------------------------------
wm_top:
    push ax
    push cx
    push si
    mov ch, 0
    mov cl, [wm_zn]
    mov si, wm_zord
    add si, cx
    jcxz .none
.next:
    dec si
    mov al, [si]
    call wm_idx2ptr
    test word [bx+W_FLAGS], 2
    jnz .done
    loop .next
.none:
    xor bx, bx
.done:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_hit - find the topmost visible window containing a point
; in:  CX = x, DX = y
; out: BX = window ptr (0 if none)
;      AL = 0 content, 1 title bar, 2 close box
;
; AL=2 is reported only when BX is the frontmost visible window - the only
; one with a close box drawn; the same region on any other window is title
; bar (AL=1).
; -----------------------------------------------------------------------------
wm_hit:
    push si
    push di
    push bp
    push ax                     ; AH is preserved; AL patched on the way out
    mov al, [wm_zn]
    mov ah, 0
    mov si, wm_zord
    add si, ax                  ; walk front to back
    mov di, 1                   ; 1 until the first visible window is passed
.next:
    cmp si, wm_zord
    je .none
    dec si
    mov al, [si]
    call wm_idx2ptr             ; BX = candidate
    test word [bx+W_FLAGS], 2
    jz .next                    ; hidden windows don't count for anything
    mov bp, di                  ; BP = "candidate is the frontmost visible"
    xor di, di
    mov ax, [bx+W_X]            ; point-in-frame-rect test
    cmp cx, ax
    jl .next
    add ax, [bx+W_W]
    cmp cx, ax
    jge .next
    mov ax, [bx+W_Y]
    cmp dx, ax
    jl .next
    add ax, [bx+W_H]
    cmp dx, ax
    jge .next
    mov ax, dx                  ; hit: AX = dy within the frame
    sub ax, [bx+W_Y]
    cmp ax, TITLE_H
    jge .content
    test bp, bp                 ; close box exists on the frontmost only
    jz .title
    cmp ax, 4                   ; box rows W_Y+4 .. W_Y+14
    jl .title
    cmp ax, 14
    jg .title
    mov ax, [bx+W_X]            ; box columns W_X+8 .. W_X+18
    add ax, 8
    cmp cx, ax
    jl .title
    add ax, 10
    cmp cx, ax
    jg .title
    mov al, 2
    jmp .out
.title:
    mov al, 1
    jmp .out
.content:
    mov al, 0
    jmp .out
.none:
    xor bx, bx
    mov al, 0
.out:
    mov bp, sp                  ; patch AL into the saved AX, keeping AH
    mov [bp], al
    pop ax
    pop bp
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; wm_paint_all - full screen repaint
; in:  nothing (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; Desktop gray dither below the menu bar, then the menu bar, then every
; visible window back to front: frame, white content fill, W_PAINT.
; -----------------------------------------------------------------------------
wm_paint_all:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    xor ax, ax                  ; desktop: 50% dither under the bar
    mov bx, MBAR_H
    mov cx, SCREEN_W-1
    mov dx, SCREEN_H-1
    call gfx_fill_gray
    call menu_draw_bar
    call wm_top
    mov bp, bx                  ; BP = frontmost visible (0 if none)
    mov si, wm_zord
    mov al, [wm_zn]
    mov ah, 0
    mov di, si
    add di, ax                  ; DI = one past the front
.win:
    cmp si, di
    je .done
    mov al, [si]
    inc si
    call wm_idx2ptr
    test word [bx+W_FLAGS], 2
    jz .win
    call wm_draw_win
    jmp .win
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_content - content-area origin of a window
; in:  BX = window ptr
; out: AX = content left (W_X+1), DX = content top (W_Y+TITLE_H)
; -----------------------------------------------------------------------------
wm_content:
    mov ax, [bx+W_X]
    inc ax
    mov dx, [bx+W_Y]
    add dx, TITLE_H
    ret

; -----------------------------------------------------------------------------
; wm_obscured - is any visible window above this one overlapping it?
; in:  BX = window ptr
; out: CF = 1 if a visible window later in wm_zord overlaps BX's frame rect
;
; The window above is tested with its drop shadow included - rect
; (x, y, w+1, h+1) - so an "unobscured" answer really means none of its
; pixels, shadow included, sit over ours. Only trustworthy while the caller
; holds the gfx lock (the UI task mutates wm_zord/rects under it).
; -----------------------------------------------------------------------------
wm_obscured:
    push ax
    push cx
    push dx
    push si
    push di
    mov ax, bx                  ; AL = our index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    mov ch, 0
    mov cl, [wm_zn]
    mov si, wm_zord
    jcxz .clear
.find:
    cmp [si], al
    je .found
    inc si
    loop .find
    jmp .clear                  ; not in z-order (cannot happen)
.found:
    inc si                      ; first entry above us
    dec cx
.scan:
    jcxz .clear
    dec cx
    mov al, [si]
    inc si
    push bx
    call wm_idx2ptr
    mov di, bx                  ; DI = window above
    pop bx
    test word [di+W_FLAGS], 2
    jz .scan
    mov ax, [di+W_X]            ; above.x <= our right edge?
    mov dx, [bx+W_X]
    add dx, [bx+W_W]
    cmp ax, dx
    jge .scan
    mov ax, [di+W_X]            ; above right edge + shadow >= our left?
    add ax, [di+W_W]
    cmp ax, [bx+W_X]
    jl .scan
    mov ax, [di+W_Y]            ; above.y <= our bottom edge?
    mov dx, [bx+W_Y]
    add dx, [bx+W_H]
    cmp ax, dx
    jge .scan
    mov ax, [di+W_Y]            ; above bottom edge + shadow >= our top?
    add ax, [di+W_H]
    cmp ax, [bx+W_Y]
    jl .scan
    stc                         ; overlap: we are obscured
    jmp .out
.clear:
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_idx2ptr - window index to record pointer (module internal)
; in:  AL = window index
; out: BX = wm_wins + index*WIN_SIZE
; -----------------------------------------------------------------------------
wm_idx2ptr:
    push ax
    push cx
    mov ah, 0                   ; index*18 = index*16 + index*2 (8086-legal:
    mov bx, ax                  ; no shift-by-imm other than 1)
    mov cl, 4
    shl ax, cl                  ; AX = index*16
    shl bx, 1                   ; BX = index*2
    add bx, ax
    add bx, wm_wins
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_draw_win - draw one window's frame and content (module internal)
; in:  BX = window ptr (visible), BP = frontmost visible window ptr (or 0)
; out: nothing (all registers preserved; caller holds the gfx lock)
;
; Exact frame per SPEC.md 11: 1px black outline; drop shadow offset (+1,+1)
; along the right and bottom edges; title bar rows W_Y+1..W_Y+TITLE_H-2
; white; frontmost window gets 6 black pinstripes inset 3px and an 11x11
; close box at W_X+8, rows W_Y+4..W_Y+14, stripes broken by white 2px around
; the box and 6px around the centered black title; black separator at row
; W_Y+TITLE_H-1; content white-filled, then W_PAINT is called with SI=window.
; -----------------------------------------------------------------------------
wm_draw_win:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, bx                  ; DI = window ptr for the duration

    mov byte [gfx_color], CBLACK
    mov ax, [di+W_X]            ; 1px outline around the whole frame
    mov bx, [di+W_Y]
    mov cx, ax
    add cx, [di+W_W]
    dec cx
    mov dx, bx
    add dx, [di+W_H]
    dec dx
    call gfx_frame

    mov ax, [di+W_X]            ; drop shadow, right edge: x+w, y+1..y+h
    add ax, [di+W_W]
    mov bx, [di+W_Y]
    inc bx
    mov dx, [di+W_Y]
    add dx, [di+W_H]
    call gfx_vline
    mov ax, [di+W_X]            ; drop shadow, bottom edge: y+h, x+1..x+w
    inc ax
    mov bx, [di+W_X]
    add bx, [di+W_W]
    mov dx, [di+W_Y]
    add dx, [di+W_H]
    call gfx_hline

    mov byte [gfx_color], CWHITE
    mov ax, [di+W_X]            ; title bar interior, 16 white rows
    inc ax
    mov bx, [di+W_Y]
    inc bx
    mov cx, [di+W_X]
    add cx, [di+W_W]
    sub cx, 2
    mov dx, [di+W_Y]
    add dx, TITLE_H-2
    call gfx_fill

    mov byte [gfx_color], CBLACK
    mov ax, [di+W_X]            ; separator under the title bar
    inc ax
    mov bx, [di+W_X]
    add bx, [di+W_W]
    sub bx, 2
    mov dx, [di+W_Y]
    add dx, TITLE_H-1
    call gfx_hline

    cmp di, bp                  ; pinstripes + close box: frontmost only
    jne .title
    mov dx, [di+W_Y]            ; 6 stripes, rows y+3,5,7,9,11,13
    add dx, 3
    mov cx, 6
.stripe:
    mov ax, [di+W_X]            ; inset 3px from each side
    add ax, 3
    mov bx, [di+W_X]
    add bx, [di+W_W]
    sub bx, 4
    call gfx_hline
    add dx, 2
    loop .stripe

    mov byte [gfx_color], CWHITE
    mov ax, [di+W_X]            ; break the stripes 2px around the close box
    add ax, 6
    mov bx, [di+W_Y]
    add bx, 2
    mov cx, [di+W_X]
    add cx, 20
    mov dx, [di+W_Y]
    add dx, 16
    call gfx_fill

    mov byte [gfx_color], CBLACK
    mov ax, [di+W_X]            ; close box: 11x11 black frame, white inside
    add ax, 8                   ; (interior is white from the break fill)
    mov bx, [di+W_Y]
    add bx, 4
    mov cx, [di+W_X]
    add cx, 18
    mov dx, [di+W_Y]
    add dx, 14
    call gfx_frame

.title:
    mov si, [di+W_TITLE]        ; centered title on a 6px white gap
    test si, si
    jz .content
    call font_width             ; AX = text width, px
    mov cx, [di+W_W]
    sub cx, ax
    sar cx, 1
    add cx, [di+W_X]            ; CX = text left
    push cx
    add ax, cx                  ; AX = gap right = text left + width + 5
    add ax, 5
    xchg ax, cx                 ; AX = text left, CX = gap right
    sub ax, 6                   ; AX = gap left
    mov bx, [di+W_Y]
    add bx, 2
    mov dx, [di+W_Y]
    add dx, 15
    mov byte [gfx_color], CWHITE
    call gfx_fill               ; white gap (covers the stripe rows)
    mov byte [gfx_color], CBLACK
    pop cx
    mov dx, [di+W_Y]
    add dx, 5                   ; 8px glyphs centered in the 16-row bar
    call font_str

.content:
    mov byte [gfx_color], CWHITE
    mov ax, [di+W_X]            ; content area, filled white before W_PAINT
    inc ax
    mov bx, [di+W_Y]
    add bx, TITLE_H
    mov cx, [di+W_X]
    add cx, [di+W_W]
    sub cx, 2
    mov dx, [di+W_Y]
    add dx, [di+W_H]
    sub dx, 2
    call gfx_fill

    mov si, di                  ; SI = window ptr for the paint proc
    mov ax, [di+W_PAINT]
    test ax, ax
    jz .done
    call ax
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm.inc .bss - window table + z-order (wm_init zeroes all of it as one run,
; so these three must stay contiguous and in this order)
; -----------------------------------------------------------------------------
section .bss
wm_wins:    resb MAX_WIN * WIN_SIZE ; 8 windows x 18 bytes
wm_zord:    resb MAX_WIN            ; window indices, [0] = backmost
wm_zn:      resb 1                  ; entries in wm_zord
section .text
