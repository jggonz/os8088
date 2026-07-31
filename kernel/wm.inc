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
W_FLAGS equ 0                   ; word: bit0 = used, bit1 = visible,
                                ; bit2 = resizable, bit3 = fullscreen
W_X     equ 2                   ; word, frame left (screen coords)
W_Y     equ 4                   ; word, frame top (below menu bar: y >=
                                ; MBAR_H - except under WF_FULL, SPEC 11.2)
W_W     equ 6                   ; word, frame width  (outer, includes border)
W_H     equ 8                   ; word, frame height (includes title bar)
W_TITLE equ 10                  ; word: near ptr to NUL title string
W_PAINT equ 12                  ; word: near ptr, in: SI = window ptr
W_ONKEY equ 14                  ; word: near ptr or 0; AL=ascii AH=scan SI=win
W_ONCLICK equ 16                ; word: near ptr or 0; CX=x DX=y SI=win, called
                                ; under the gfx lock for content clicks on the
                                ; front window (SPEC.md 13)
WIN_SIZE equ 18                 ; NOT a power of two: never shift, multiply
MAX_WIN  equ 12                 ; mirrored in apps/os88api.inc (SPEC.md 11)

WF_SIZABLE equ 4                ; W_FLAGS bit2: resizable (SPEC.md 11.1)
WF_FULL    equ 8                ; W_FLAGS bit3: fullscreen (SPEC.md 11.2)
WMIN_W     equ 96               ; smallest frame a resize can leave, outer px
WMIN_H     equ 64               ; (SPEC.md 11.1; mirrored in apps/os88api.inc)

; -----------------------------------------------------------------------------
; wm_init - zero the window table, z-order array and count; orphan wm_owner
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
    mov al, 0xFF                ; wm_owner is NOT part of the zero run: its
    mov di, wm_owner            ; empty value is 0xFF (unowned, SPEC.md 11)
    mov cx, MAX_WIN
    rep stosb
    mov word [wm_fs], 0         ; no fullscreen window (SPEC.md 11.2); .bss
                                ; is never zeroed at boot, so this is not
                                ; redundant
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
; Clears W_FLAGS (used + visible), resets the slot's wm_owner entry to 0xFF,
; removes the window's index from wm_zord (compacting the byte array over
; it, decrementing wm_zn), then repaints all. The record slot becomes
; reusable by wm_create.
; -----------------------------------------------------------------------------
wm_destroy:
    push ax
    push cx
    push si
    call wm_fs_drop             ; a dying fullscreen window must release the
                                ; screen (SPEC.md 11.2)
    mov word [bx+W_FLAGS], 0    ; free the record slot
    mov ax, bx                  ; AL = window index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    mov si, ax                  ; orphan the slot's owner entry so a future
    mov byte [wm_owner+si], 0xFF; wm_create reuse starts unowned (SPEC.md 11)
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
    call wm_fs_drop             ; a hidden fullscreen window releases the
                                ; screen and re-shows windowed (SPEC.md 11.2)
    and word [bx+W_FLAGS], 0xFFFD
    jmp wm_paint_all            ; same register contract

; -----------------------------------------------------------------------------
; wm_fs_drop - release the fullscreen latch if BX holds it (module internal)
; in:  BX = window ptr
; out: nothing (all registers preserved)
; If BX is [wm_fs]: restore the saved windowed geometry into the record,
; clear WF_FULL, zero the latch (SPEC.md 11.2). Does not repaint - every
; caller repaints right after.
; -----------------------------------------------------------------------------
wm_fs_drop:
    cmp bx, [wm_fs]
    jne .out
    push ax
    mov ax, [wm_fs_save]
    mov [bx+W_X], ax
    mov ax, [wm_fs_save+2]
    mov [bx+W_Y], ax
    mov ax, [wm_fs_save+4]
    mov [bx+W_W], ax
    mov ax, [wm_fs_save+6]
    mov [bx+W_H], ax
    and word [bx+W_FLAGS], 0xFFF7
    mov word [wm_fs], 0
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; wm_sizable - set or clear a window's WF_SIZABLE bit (public; API 0x008C)
; in:  BX = window ptr, AL = 0 clear / non-zero set
; out: nothing (all registers preserved)
; No repaint: the grow box appears at the next paint. UI-task context only
; (entry procs and window callbacks qualify, SPEC.md 11.1).
; -----------------------------------------------------------------------------
wm_sizable:
    or al, al
    jz .clear
    or word [bx+W_FLAGS], WF_SIZABLE
    ret
.clear:
    and word [bx+W_FLAGS], 0xFFFB
    ret

; -----------------------------------------------------------------------------
; wm_fullscreen - enter or leave fullscreen (public; API 0x0090, SPEC.md 11.2)
; in:  AL = 1 enter (BX = window ptr) / 0 exit; caller HOLDS the gfx lock
; out: CF = 1 enter refused (another window owns the screen), else CF = 0.
;      All registers preserved.
; Enter saves the windowed geometry, grows the frame to the whole screen,
; sets WF_FULL and raises (wm_front repaints under the held lock).
; Re-entering with the same window is a CF=0 no-op, as is exiting with no
; owner. Exit restores through wm_fs_drop and repaints.
; -----------------------------------------------------------------------------
wm_fullscreen:
    push ax
    push bx
    or al, al
    jz .exit
    mov ax, [wm_fs]
    or ax, ax
    jz .claim
    cmp ax, bx
    je .ok                      ; already ours: no-op
    stc                         ; someone else owns the screen
    jmp .out
.claim:
    mov [wm_fs], bx
    mov ax, [bx+W_X]
    mov [wm_fs_save], ax
    mov ax, [bx+W_Y]
    mov [wm_fs_save+2], ax
    mov ax, [bx+W_W]
    mov [wm_fs_save+4], ax
    mov ax, [bx+W_H]
    mov [wm_fs_save+6], ax
    xor ax, ax
    mov [bx+W_X], ax
    mov [bx+W_Y], ax
    mov word [bx+W_W], SCREEN_W
    mov word [bx+W_H], SCREEN_H
    or word [bx+W_FLAGS], WF_FULL
    call wm_front               ; raise + repaint, lock already held
    jmp .ok
.exit:
    mov bx, [wm_fs]
    or bx, bx
    jz .ok                      ; nothing fullscreen: no-op
    call wm_fs_drop             ; restore geometry, clear latch
    call wm_paint_all
.ok:
    clc
.out:
    pop bx
    pop ax
    ret

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
;      AL = 0 content, 1 title bar, 2 close box, 3 minimize box
;
; AL=2/AL=3 are reported only when BX is the frontmost visible window - the
; only one with the boxes drawn; the same regions on any other window are
; title bar (AL=1).
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
    test word [bx+W_FLAGS], WF_FULL
    jnz .plain                  ; fullscreen: no chrome - every point is
                                ; content (SPEC.md 11.2)
    mov ax, dx                  ; hit: AX = dy within the frame
    sub ax, [bx+W_Y]
    cmp ax, TITLE_H
    jge .content
    test bp, bp                 ; the boxes exist on the frontmost only
    jz .title
    cmp ax, 4                   ; both boxes occupy rows W_Y+4 .. W_Y+14
    jl .title
    cmp ax, 14
    jg .title
    mov ax, [bx+W_X]            ; close box columns W_X+8 .. W_X+18
    add ax, 8
    cmp cx, ax
    jl .minbox
    add ax, 10
    cmp cx, ax
    jg .minbox
    mov al, 2
    jmp .out
.minbox:
    mov ax, [bx+W_X]            ; minimize box: W_X+W_W-19 .. W_X+W_W-9
    add ax, [bx+W_W]            ; (the close box mirrored, SPEC.md 11)
    sub ax, 19
    cmp cx, ax
    jl .title
    add ax, 10
    cmp cx, ax
    jg .title
    mov al, 3
    jmp .out
.title:
    mov al, 1
    jmp .out
.content:
    test bp, bp                 ; grow box: frontmost + WF_SIZABLE only
    jz .plain                   ; (SPEC.md 11.1; WF_FULL was peeled off
    test word [bx+W_FLAGS], WF_SIZABLE ; above)
    jz .plain
    mov ax, [bx+W_X]            ; 13x13 rect: cols W_X+W_W-14 .. W_X+W_W-2
    add ax, [bx+W_W]
    sub ax, 14
    cmp cx, ax
    jl .plain
    add ax, 12
    cmp cx, ax
    jg .plain
    mov ax, [bx+W_Y]            ; rows W_Y+W_H-14 .. W_Y+W_H-2
    add ax, [bx+W_H]
    sub ax, 14
    cmp dx, ax
    jl .plain
    add ax, 12
    cmp dx, ax
    jg .plain
    mov al, 4
    jmp .out
.plain:
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
    mov bx, [wm_fs]             ; a visible fullscreen window covers the
    or bx, bx                   ; desktop, dock and menu bar - painting
    jz .chrome                  ; them under it is pure waste (SPEC.md 11.2)
    test word [bx+W_FLAGS], 2
    jnz .wins
.chrome:
    xor ax, ax                  ; desktop: 50% dither under the bar
    mov bx, MBAR_H
    mov cx, SCREEN_W-1
    mov dx, SCREEN_H-1
    call gfx_fill_gray
    call desk_paint             ; drive icons live on the desktop, under
                                ; every window (SPEC.md 26)
    call dock_paint             ; so does the dock strip (SPEC.md 30)
    call menu_draw_bar
.wins:
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
; out: AX = content left (W_X+1), DX = content top (W_Y+TITLE_H);
;      WF_FULL set: AX = W_X, DX = W_Y - no border, no title bar (SPEC 11.2)
; -----------------------------------------------------------------------------
wm_content:
    mov ax, [bx+W_X]
    mov dx, [bx+W_Y]
    test word [bx+W_FLAGS], WF_FULL
    jnz .full
    inc ax
    add dx, TITLE_H
.full:
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
; wm_ptr2idx - window record pointer to index (public, SPEC.md 11)
; in:  BX = window ptr (record-aligned)
; out: AL = window index, AH = 0
; clobbers: nothing else
; -----------------------------------------------------------------------------
wm_ptr2idx:
    push cx
    mov ax, bx
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    pop cx
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
; white; frontmost window gets 6 black pinstripes inset 3px, an 11x11
; close box at W_X+8, rows W_Y+4..W_Y+14, and an 11x11 minimize box
; mirrored at W_X+W_W-19 with a collapse bar across row W_Y+9 - stripes
; broken by white 2px around each box and 6px around the centered black
; title; black separator at row W_Y+TITLE_H-1; content white-filled, then
; W_PAINT is called with SI=window - bracketed by task_cycles/inst_charge so
; the paint is billed to the window's instance, not to the painting task
; (SPEC.md 8.1/28).
; -----------------------------------------------------------------------------
wm_draw_win:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, bx                  ; DI = window ptr for the duration

    test word [di+W_FLAGS], WF_FULL
    jz .chrome                  ; fullscreen: no chrome at all - the whole
    mov byte [gfx_color], CWHITE; frame is content, white-filled, then
    mov ax, [di+W_X]            ; W_PAINT (SPEC.md 11.2)
    mov bx, [di+W_Y]
    mov cx, ax
    add cx, [di+W_W]
    dec cx
    mov dx, bx
    add dx, [di+W_H]
    dec dx
    call gfx_fill
    jmp .paint

.chrome:
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

    mov byte [gfx_color], CWHITE
    mov ax, [di+W_X]            ; break the stripes around the minimize box
    add ax, [di+W_W]            ; (mirror of the close-box break, SPEC.md 11)
    sub ax, 21
    mov bx, [di+W_Y]
    add bx, 2
    mov cx, [di+W_X]
    add cx, [di+W_W]
    sub cx, 7
    mov dx, [di+W_Y]
    add dx, 16
    call gfx_fill

    mov byte [gfx_color], CBLACK
    mov ax, [di+W_X]            ; minimize box: 11x11 black frame at
    add ax, [di+W_W]            ; W_X+W_W-19 .. W_X+W_W-9, rows +4..+14
    sub ax, 19
    mov bx, [di+W_Y]
    add bx, 4
    mov cx, [di+W_X]
    add cx, [di+W_W]
    sub cx, 9
    mov dx, [di+W_Y]
    add dx, 14
    call gfx_frame

    mov ax, [di+W_X]            ; collapse glyph: black bar across row
    add ax, [di+W_W]            ; W_Y+9, cols W_X+W_W-17 .. W_X+W_W-11
    sub ax, 17
    mov bx, [di+W_X]
    add bx, [di+W_W]
    sub bx, 11
    mov dx, [di+W_Y]
    add dx, 9
    call gfx_hline

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

.paint:
    cmp word [di+W_PAINT], 0
    je .growbox
    mov bx, di                  ; the paint is the owning instance's CPU
    call inst_win_owner         ; time, not the painting task's (SPEC.md 8.1)
    push di
    push word [snd_inst]        ; stamp the dispatched instance for sound
    call snd_disp_set           ; grants made inside the paint (SPEC 34.3)
    call task_cycles            ; DX:AX = start stamp
    push dx
    push ax
    mov si, bx                  ; SI = window ptr for the paint proc
    call [bx+W_PAINT]
    pop ax                      ; start stamp
    pop dx
    pop word [snd_inst]         ; the outer dispatch's stamp, back
    pop di                      ; owning instance record, 0 = unowned window
    or di, di
    jz .uncharged
    call inst_charge
.uncharged:
    mov di, bx                  ; DI = window ptr again (BX survived the
                                ; paint proc - SPEC.md 1 register discipline)
.growbox:
    mov bx, di                  ; grow box AFTER W_PAINT (SPEC.md 11.1) -
    call wm_grow_paint          ; a no-op unless frontmost + WF_SIZABLE
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_grow_paint - draw a window's grow box if it deserves one (SPEC.md 11.1)
; in:  BX = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; Draws only when BX is the frontmost visible window with WF_SIZABLE set
; and WF_FULL clear - a no-op otherwise, so it is always safe to call.
; wm_draw_win calls it after W_PAINT, and a resizable window's
; self-initiated content repaint must end with it: the white-fill idiom
; (SPEC.md 22) erases the corner, and wm_hit reports AL=4 from geometry
; alone, so the box must never be left invisible-but-clickable.
; -----------------------------------------------------------------------------
wm_grow_paint:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, bx
    call wm_top                 ; BX = frontmost visible (0 if none)
    cmp di, bx
    jne .out
    mov ax, [di+W_FLAGS]
    and ax, WF_SIZABLE | WF_FULL
    cmp ax, WF_SIZABLE
    jne .out
    mov ax, [di+W_X]            ; 13x13 white square, black frame, at
    add ax, [di+W_W]            ; (W_X+W_W-14, W_Y+W_H-14)..(+W_W-2, +W_H-2)
    sub ax, 14
    mov bx, [di+W_Y]
    add bx, [di+W_H]
    sub bx, 14
    mov cx, ax
    add cx, 12
    mov dx, bx
    add dx, 12
    mov byte [gfx_color], CWHITE
    call gfx_fill
    mov byte [gfx_color], CBLACK
    call gfx_frame
    add ax, 4                   ; the big square of the glyph: 8x8 frame
    add bx, 4                   ; at inset (+4,+4)..(+11,+11)
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call gfx_frame
    sub ax, 2                   ; the small square: 7x7 at inset (+2,+2)..
    sub bx, 2                   ; (+8,+8), white-filled (it overlaps the
    mov cx, ax                  ; big one's top-left), black-framed
    add cx, 6
    mov dx, bx
    add dx, 6
    mov byte [gfx_color], CWHITE
    call gfx_fill
    mov byte [gfx_color], CBLACK
    call gfx_frame
.out:
    pop di
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
wm_wins:    resb MAX_WIN * WIN_SIZE ; 12 windows x 18 bytes
wm_zord:    resb MAX_WIN            ; window indices, [0] = backmost
wm_zn:      resb 1                  ; entries in wm_zord
wm_owner:   resb MAX_WIN            ; instance index owning each window slot
                                    ; (SPEC.md 29), 0xFF = unowned; NOT part
                                    ; of wm_init's zero run (fill is 0xFF)
wm_fs:      resw 1                  ; fullscreen window ptr, 0 = none
                                    ; (SPEC.md 11.2); zeroed by wm_init
wm_fs_save: resw 4                  ; its windowed x/y/w/h, restored on exit
section .text
